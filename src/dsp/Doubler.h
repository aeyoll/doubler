#pragma once

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace doubler {

constexpr float kSpreadCentsL = 12.f;
constexpr float kSpreadCentsR = -9.f;
constexpr float kDelayMsL = 20.f;
constexpr float kDelayMsR = 28.f;
constexpr float kMinDelayMs = 12.f;
// 20 ms delay notches ~25 Hz; wet HP keeps that hole off the dry bass.
constexpr float kWetHpHz = 220.f;
constexpr int kYinSize = 1024;
constexpr int kHop = 256;

inline uint32_t xorshift32(uint32_t& s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

inline float urand(uint32_t& s)
{
    return (xorshift32(s) >> 8) * (1.0f / 16777216.0f);
}

struct Pitch
{
    float f0Hz = 0.f;
    float confidence = 0.f;
};

// Classic YIN (de Cheveigné & Kawahara). tau range limits the search.
inline Pitch yinDetect(const float* x, int n, float sr, int tauMin, int tauMax)
{
    Pitch out;
    if (n < 4 || sr <= 0.f)
        return out;

    tauMin = std::max(2, tauMin);
    tauMax = std::min(tauMax, n / 2);
    if (tauMin >= tauMax)
        return out;

    float best = 1.f;
    int bestTau = tauMin;
    float running = 0.f;
    constexpr float kThresh = 0.15f;

    for (int tau = 1; tau <= tauMax; ++tau)
    {
        float d = 0.f;
        const int last = n - tau;
        for (int j = 0; j < last; ++j)
        {
            const float diff = x[j] - x[j + tau];
            d += diff * diff;
        }

        running += d;
        if (tau < tauMin)
            continue;

        const float cmnd = d * (float) tau / std::max(running, 1e-12f);
        if (cmnd < best)
        {
            best = cmnd;
            bestTau = tau;
        }
        if (best < kThresh && cmnd > best)
            break;
    }

    if (bestTau <= tauMin || bestTau >= tauMax)
        return out;

    const auto dAt = [&](int tau) {
        float d = 0.f;
        const int last = n - tau;
        for (int j = 0; j < last; ++j)
        {
            const float diff = x[j] - x[j + tau];
            d += diff * diff;
        }
        return d;
    };

    const float s0 = dAt(bestTau - 1);
    const float s1 = dAt(bestTau);
    const float s2 = dAt(bestTau + 1);
    const float denom = 2.f * (s0 - 2.f * s1 + s2);
    float tau = (float) bestTau;
    if (std::abs(denom) > 1e-12f)
        tau += (s0 - s2) / denom;

    out.f0Hz = sr / tau;
    out.confidence = std::max(0.f, 1.f - best);
    return out;
}

inline float catmull(const float* b, int mask, float idx)
{
    const int i = (int) std::floor(idx);
    const float f = idx - (float) i;
    const float ym1 = b[(i - 1) & mask];
    const float y0 = b[i & mask];
    const float y1 = b[(i + 1) & mask];
    const float y2 = b[(i + 2) & mask];
    const float a0 = -0.5f * ym1 + 1.5f * y0 - 1.5f * y1 + 0.5f * y2;
    const float a1 = ym1 - 2.5f * y0 + 2.f * y1 - 0.5f * y2;
    const float a2 = -0.5f * ym1 + 0.5f * y1;
    return ((a0 * f + a1) * f + a2) * f + y0;
}

struct Voice
{
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    std::vector<float> delay;
    std::vector<float> shifted;
    int delayMask = 0;
    int delayW = 0;
    uint32_t rng = 1;
    float walkPitch = 0.f;
    float walkDelay = 0.f;
    float walkPitch2 = 0.f;
    float delayMsSmoothed = kMinDelayMs;
    float onsetMs = 0.f;
    float cents = 0.f;
    float delayMs = kMinDelayMs;
    float baseCents = 0.f;
    float baseDelayMs = kMinDelayMs;
    int vibDelayHops = 3;
};

class Engine
{
public:
    void prepare(double sampleRate, int maxBlock)
    {
        sr = (float) sampleRate;
        maxN = std::max(maxBlock, kHop);

        for (int v = 0; v < 2; ++v)
        {
            auto& voice = voices[v];
            // ponytail: Stretch latency ~50–150ms (presetCheaper). PSOLA sync if voice + latency too high.
            voice.stretch.presetCheaper(1, sr, true);
            voice.shifted.assign((size_t) maxN, 0.f);
            voice.rng = v == 0 ? 0xA341316Cu : 0xC0FFEE01u;
            voice.walkPitch = voice.walkDelay = voice.walkPitch2 = 0.f;
            voice.onsetMs = 0.f;
            voice.delayW = 0;
            voice.delayMsSmoothed = v == 0 ? kDelayMsL : kDelayMsR;
        }

        voices[0].baseCents = kSpreadCentsL;
        voices[1].baseCents = kSpreadCentsR;
        voices[0].baseDelayMs = kDelayMsL;
        voices[1].baseDelayMs = kDelayMsR;
        voices[0].vibDelayHops = 3;
        voices[1].vibDelayHops = 6;

        stretchLatency = voices[0].stretch.inputLatency() + voices[0].stretch.outputLatency();

        int delayLen = 1;
        const int need = stretchLatency + (int) (0.08f * sr) + maxN + 8;
        while (delayLen < need)
            delayLen <<= 1;

        dryL.assign((size_t) delayLen, 0.f);
        dryR.assign((size_t) delayLen, 0.f);
        dryMask = delayLen - 1;
        dryW = 0;

        for (auto& voice : voices)
        {
            voice.delay.assign((size_t) delayLen, 0.f);
            voice.delayMask = dryMask;
            voice.stretch.reset();
        }

        yinBuf.assign(kYinSize, 0.f);
        yinW = 0;
        hopFill = 0;
        f0 = 0.f;
        f0Smooth = 0.f;
        confidence = 0.f;
        env = 0.f;
        slowEnv = 0.f;
        std::fill(vibHist.begin(), vibHist.end(), 0.f);
        vibW = 0;
        mono.assign((size_t) maxN, 0.f);
        srcL.assign((size_t) maxN, 0.f);
        srcR.assign((size_t) maxN, 0.f);

        tauMin = std::max(2, (int) (sr / 800.f));
        tauMax = std::min(kYinSize / 2, (int) (sr / 80.f));
        hpA = 1.f - std::exp(-6.2831853f * kWetHpHz / sr);
        hpLp[0] = hpLp[1] = 0.f;
    }

    void reset()
    {
        if (sr <= 0.f)
            return;
        prepare(sr, maxN);
    }

    void setParams(float mixAmt, float spreadAmt, float humanAmt, float widthAmt)
    {
        mix = std::clamp(mixAmt, 0.f, 1.f);
        spread = std::clamp(spreadAmt, 0.f, 1.f);
        humanize = std::clamp(humanAmt, 0.f, 1.f);
        width = std::clamp(widthAmt, 0.f, 1.f);
    }

    int latencySamples() const { return stretchLatency; }

    void process(const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        if (n <= 0 || sr <= 0.f || maxN <= 0)
            return;

        const float* rightIn = inR ? inR : inL;
        int done = 0;
        while (done < n)
        {
            const int chunk = std::min(n - done, maxN);
            for (int i = 0; i < chunk; ++i)
            {
                srcL[(size_t) i] = inL[done + i];
                srcR[(size_t) i] = rightIn[done + i];
                mono[(size_t) i] = 0.5f * (srcL[(size_t) i] + srcR[(size_t) i]);
            }

            for (int start = 0; start < chunk; )
            {
                const int h = std::min(kHop, chunk - start);
                analyseHop(start, h);
                humanizeHop();

                for (int v = 0; v < 2; ++v)
                {
                    auto& voice = voices[v];
                    voice.stretch.setTransposeSemitones(voice.cents / 100.f);
                    float* inPtrs[1] = { mono.data() + start };
                    float* outPtrs[1] = { voice.shifted.data() };
                    voice.stretch.process(inPtrs, h, outPtrs, h);
                }

                mixHop(srcL.data() + start, srcR.data() + start,
                       outL + done + start, outR + done + start, h);
                start += h;
            }
            done += chunk;
        }
    }

private:
    void analyseHop(int start, int h)
    {
        for (int i = 0; i < h; ++i)
        {
            const float x = mono[(size_t) (start + i)];
            yinBuf[(size_t) yinW] = x;
            yinW = (yinW + 1) % kYinSize;
            const float ax = std::abs(x);
            const float decay = std::exp(-1.f / (0.025f * sr));
            env = ax > env ? ax : env * decay;
            const float slowA = 1.f - std::exp(-1.f / (0.12f * sr));
            slowEnv += slowA * (env - slowEnv);
        }

        hopFill += h;
        if (hopFill < kHop)
            return;
        hopFill = 0;

        float linear[kYinSize];
        for (int i = 0; i < kYinSize; ++i)
            linear[i] = yinBuf[(size_t) ((yinW + i) % kYinSize)];

        const Pitch p = yinDetect(linear, kYinSize, sr, tauMin, tauMax);
        if (p.confidence > 0.4f && p.f0Hz > 50.f && p.f0Hz < 1200.f)
        {
            f0 = p.f0Hz;
            confidence = p.confidence;
        }
        else
        {
            confidence *= 0.85f;
        }

        if (f0 > 0.f)
        {
            if (f0Smooth <= 1.f)
                f0Smooth = f0;
            const float a = 1.f - std::exp(-((float) kHop / sr) / 0.15f);
            f0Smooth += a * (f0 - f0Smooth);
        }

        float vib = 0.f;
        if (confidence > 0.6f && f0 > 0.f && f0Smooth > 0.f)
            vib = 1200.f * std::log2(f0 / f0Smooth);

        vibHist[(size_t) vibW] = vib;
        vibW = (vibW + 1) & 31;
    }

    void humanizeHop()
    {
        const float dt = (float) kHop / sr;
        const float onsetGate = env - slowEnv;
        const bool onset = onsetGate > 0.08f && env > 0.02f;

        for (int v = 0; v < 2; ++v)
        {
            auto& voice = voices[v];
            const float step = std::sqrt(dt);

            voice.walkPitch += (urand(voice.rng) * 2.f - 1.f) * step * 6.f * humanize;
            voice.walkPitch *= std::exp(-dt * 1.5f);
            voice.walkPitch2 += (urand(voice.rng) * 2.f - 1.f) * step * 2.f * humanize;
            voice.walkPitch2 *= std::exp(-dt * 0.35f);
            voice.walkDelay += (urand(voice.rng) * 2.f - 1.f) * step * 3.f * humanize;
            voice.walkDelay *= std::exp(-dt * 0.4f);

            const float walkCents = std::clamp(voice.walkPitch + voice.walkPitch2, -15.f, 15.f);
            const int delayHops = std::clamp(voice.vibDelayHops, 1, 30);
            const float delayedVib = vibHist[(size_t) ((vibW - delayHops) & 31)];
            const float vibAmt = confidence > 0.6f ? delayedVib * (0.55f + 0.25f * humanize) : 0.f;

            voice.cents = voice.baseCents * spread + walkCents * humanize + vibAmt * humanize;

            if (onset)
                voice.onsetMs = 2.f + 6.f * humanize;
            else
                voice.onsetMs *= 0.92f;

            const float delayTarget = kMinDelayMs + (voice.baseDelayMs - kMinDelayMs) * spread
                                    + std::clamp(voice.walkDelay, -5.f, 5.f) * humanize
                                    + voice.onsetMs;
            voice.delayMs = std::max(kMinDelayMs, delayTarget);
        }
    }

    void mixHop(const float* inL, const float* inR, float* outL, float* outR, int h)
    {
        const float lToL = 0.5f + 0.5f * width;
        const float lToR = 0.5f - 0.5f * width;
        const float rToR = 0.5f + 0.5f * width;
        const float rToL = 0.5f - 0.5f * width;

        for (int i = 0; i < h; ++i)
        {
            dryL[(size_t) (dryW & dryMask)] = inL[i];
            dryR[(size_t) (dryW & dryMask)] = inR[i];
            const int dryRidx = (dryW - stretchLatency) & dryMask;
            const float dL = dryL[(size_t) dryRidx];
            const float dR = dryR[(size_t) dryRidx];
            ++dryW;

            float wL = 0.f, wR = 0.f;
            for (int v = 0; v < 2; ++v)
            {
                auto& voice = voices[v];
                const float delayA = 1.f - std::exp(-1.f / (0.07f * sr));
                voice.delayMsSmoothed += delayA * (voice.delayMs - voice.delayMsSmoothed);
                voice.delay[(size_t) (voice.delayW & voice.delayMask)] = voice.shifted[(size_t) i];
                const float delaySamp = voice.delayMsSmoothed * 0.001f * sr;
                const float idx = (float) voice.delayW - delaySamp;
                const float s = catmull(voice.delay.data(), voice.delayMask, idx);
                ++voice.delayW;
                if (v == 0)
                    wL = s;
                else
                    wR = s;
            }

            hpLp[0] += hpA * (wL - hpLp[0]);
            hpLp[1] += hpA * (wR - hpLp[1]);
            wL -= hpLp[0];
            wR -= hpLp[1];

            // Dry stays full-level; Mix only brings in wet. 50/50 replace is the deepest comb.
            outL[i] = dL + (wL * lToL + wR * rToL) * mix;
            outR[i] = dR + (wL * lToR + wR * rToR) * mix;
        }
    }

    Voice voices[2];
    std::vector<float> dryL, dryR, mono, srcL, srcR, yinBuf;
    std::array<float, 32> vibHist {};
    float sr = 0.f;
    float mix = 0.5f, spread = 0.7f, humanize = 0.5f, width = 1.f;
    float f0 = 0.f, f0Smooth = 0.f, confidence = 0.f, env = 0.f, slowEnv = 0.f;
    int maxN = 0, stretchLatency = 0, dryMask = 0, dryW = 0;
    int yinW = 0, hopFill = 0, tauMin = 2, tauMax = 512, vibW = 0;
    float hpA = 0.f;
    float hpLp[2] = {};
};

} // namespace doubler
