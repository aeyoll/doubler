#include "dsp/Doubler.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static void fail(const char* msg)
{
    std::fprintf(stderr, "FAIL: %s\n", msg);
    std::exit(1);
}

static float rmsErr(const float* a, const float* b, int n)
{
    double e = 0;
    for (int i = 0; i < n; ++i)
    {
        const double d = (double) a[i] - (double) b[i];
        e += d * d;
    }
    return (float) std::sqrt(e / std::max(n, 1));
}

static float corr(const float* a, const float* b, int n)
{
    double aa = 0, bb = 0, ab = 0;
    for (int i = 0; i < n; ++i)
    {
        aa += (double) a[i] * a[i];
        bb += (double) b[i] * b[i];
        ab += (double) a[i] * b[i];
    }
    const double d = std::sqrt(aa * bb);
    return d > 1e-12 ? (float) (ab / d) : 0.f;
}

// Naive delay-LFO copy of the same waveform (classic chorus).
static void delayLfo(const float* in, float* out, int n, float sr, float delayMs, float depthMs, float hz)
{
    const int cap = (int) (sr * 0.1f) + 8;
    std::vector<float> buf((size_t) cap, 0.f);
    int w = 0;
    for (int i = 0; i < n; ++i)
    {
        buf[(size_t) (w % cap)] = in[i];
        const float lfo = std::sin(2.f * 3.14159265f * hz * (float) i / sr);
        const float d = (delayMs + depthMs * lfo) * 0.001f * sr;
        float idx = (float) w - d;
        while (idx < 0.f)
            idx += (float) cap;
        const int i0 = (int) idx;
        const float f = idx - (float) i0;
        const float y0 = buf[(size_t) (i0 % cap)];
        const float y1 = buf[(size_t) ((i0 + 1) % cap)];
        out[i] = y0 + (y1 - y0) * f;
        ++w;
    }
}

int main()
{
    constexpr float sr = 48000.f;
    constexpr int n = 48000 * 3;
    std::vector<float> in((size_t) n), dryL((size_t) n), dryR((size_t) n);
    std::vector<float> wetL((size_t) n), wetR((size_t) n), chorus((size_t) n);

    for (int i = 0; i < n; ++i)
        in[(size_t) i] = 0.3f * (2.f * std::fmod(440.f * (float) i / sr, 1.f) - 1.f);

    doubler::Engine dryEngine;
    dryEngine.prepare(sr, 512);
    dryEngine.setParams(0.f, 1.f, 0.f, 1.f);
    dryEngine.process(in.data(), in.data(), dryL.data(), dryR.data(), n);

    const int lat = dryEngine.latencySamples();
    if (lat <= 0 || lat > n / 4)
        fail("latency out of range");

    const int use = n - lat - 2048;
    if (rmsErr(dryL.data() + lat, in.data(), use) > 1e-3f)
        fail("mix=0 is not latency-compensated dry");

    doubler::Engine wetEngine;
    wetEngine.prepare(sr, 512);
    wetEngine.setParams(1.f, 1.f, 0.f, 1.f);
    wetEngine.process(in.data(), in.data(), wetL.data(), wetR.data(), n);

    const int yinN = doubler::kYinSize;
    const int off = lat + (int) (0.4f * sr);
    const auto pitchAt = [&](const float* x) {
        return doubler::yinDetect(x + off, yinN, sr, (int) (sr / 800.f), (int) (sr / 80.f));
    };

    const auto pL = pitchAt(wetL.data());
    const auto pR = pitchAt(wetR.data());
    const float expectL = 440.f * std::pow(2.f, doubler::kSpreadCentsL / 1200.f);
    const float expectR = 440.f * std::pow(2.f, doubler::kSpreadCentsR / 1200.f);

    if (pL.confidence < 0.4f || pR.confidence < 0.4f)
        fail("wet pitch tracker not confident");
    if (std::abs(pL.f0Hz - expectL) > 5.f)
        fail("left voice cents not applied (still a delay copy?)");
    if (std::abs(pR.f0Hz - expectR) > 5.f)
        fail("right voice cents not applied (still a delay copy?)");
    if (std::abs(pL.f0Hz - pR.f0Hz) < 2.f)
        fail("L/R voices not independently pitched");

    delayLfo(in.data(), chorus.data(), n, sr, 20.f, 0.f, 0.8f);
    const auto pChorus = pitchAt(chorus.data());
    if (std::abs(pChorus.f0Hz - 440.f) > 3.f)
        fail("control: constant delay should keep 440 Hz");

    const int dSamp = (int) (0.02f * sr);
    const int wetAlign = lat + (int) (doubler::kDelayMsL * 0.001f * sr);
    const float cWet = std::abs(corr(wetL.data() + off, in.data() + off - wetAlign, yinN * 4));
    const float cDelay = std::abs(corr(chorus.data() + off, in.data() + off - dSamp, yinN * 4));
    if (cDelay < 0.95f)
        fail("aligned delay copy should match input");
    if (cWet > 0.85f)
        fail("wet still as correlated as a delay copy");

    std::printf("ok  lat=%d  f0L=%.2f (want %.2f)  f0R=%.2f (want %.2f)  corr wet=%.3f delay=%.3f\n",
                lat, pL.f0Hz, expectL, pR.f0Hz, expectR, cWet, cDelay);
    return 0;
}
