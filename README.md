# Doubler

Stereo doubling plugin. Two voices with independent pitch and delay, not a chorus LFO on a delayed copy. Dry stays latency-compensated; wet sits L/R so the mix does not comb.

Formats: AU, VST3, Standalone. GUI is JUCE `GenericAudioProcessorEditor`.

## Requirements

- CMake 3.24+
- C++17 compiler
- Git (first configure fetches [JUCE](https://github.com/juce-framework/JUCE) 8.0.8 and [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch))

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

`COPY_PLUGIN_AFTER_BUILD` is on. After a successful build on macOS:

- AU: `~/Library/Audio/Plug-Ins/Components/Doubler.component`
- VST3: `~/Library/Audio/Plug-Ins/VST3/Doubler.vst3`

Binaries also land in `build/Doubler_artefacts/Release/`.

Plugin codes: manufacturer `Aeyl`, plugin `Dblr`.

## Parameters

| Param | Default | Role |
| --- | --- | --- |
| Mix | 0.5 | Wet level (dry stays; not a replace blend) |
| Spread | 0.7 | How far L/R cents and delays diverge |
| Humanize | 0.5 | Random walk on pitch/delay plus delayed vibrato from the tracker |
| Width | 1.0 | Stereo placement of the two wet voices |

Base offsets at full spread: L `+12` cents / `20` ms, R `-9` cents / `28` ms.

Input: mono or stereo. Output: stereo.

## How it works

A classic chorus delays one copy of the same waveform and wobbles that delay with an LFO. The copy stays correlated with the dry signal, so the sum combs: notches that move with the LFO. Doubler instead builds two *other performances* of the input: each voice has its own pitch trajectory and its own delay, so the wet is a re-interpretation, not a sliding echo of the same samples.

```
in L/R ──► copy to src, fold to mono
              │
              ├── YIN pitch + envelope (every 256 samples)
              ├── humanizer → cents + delay per voice
              │
              ├── Stretch L  (pitch only, same in/out length)
              ├── Stretch R
              │         │
              │         ▼
              │    cubic delay (12–28 ms, independent)
              │
              └── dry delay = Stretch latency
                        │
                        ▼
                   mix + width matrix → out L/R
```

### 1. Split

Each block is copied so processing can run in-place. A mono sum `(L+R)/2` feeds pitch tracking and both shifters. Dry L/R stay stereo.

### 2. Pitch tracking (YIN)

Classic YIN (de Cheveigné & Kawahara) on a 1024-sample window, hop 256. Search is limited to about 80–800 Hz. A hit is kept only if confidence is high enough; otherwise confidence decays.

A slow envelope of `f0` is the “intended” pitch. Instantaneous `f0` minus that envelope, in cents, is treated as vibrato. That vibrato is stored in a short history so each voice can replay it *late* (L by 3 hops, R by 6). Two singers do not lock vibrato to the same sample.

Peak vs slow RMS envelope flags onsets. An onset bumps extra milliseconds onto each voice’s delay, then that bump decays — attacks do not land in perfect unison.

### 3. Humanizer

Every hop, each voice gets its own random walks (xorshift, independent seeds):

- two pitch walks (fast + slow), clamped to ±15 cents
- one delay walk, ±5 ms

Voice pitch in cents:

`baseCents * Spread + walks * Humanize + delayedVibrato * Humanize`

Voice delay:

`12 ms + (baseDelay − 12 ms) * Spread + delayWalk * Humanize + onset bump`

At full spread the bases are L `+12` cents / `20` ms and R `−9` cents / `28` ms. Asymmetric on purpose so L and R are not a mirror pair (a mirror pair still combs in mono).

### 4. Pitch shift (not a delay Doppler)

Each voice runs its own [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) instance (`presetCheaper`, split computation). `process()` is called with equal input and output lengths, so the engine pitch-shifts and does **not** time-stretch. Transpose is `cents / 100` semitones, updated every hop.

That is the difference vs chorus: chorus pitch comes from delaying faster or slower (Doppler on the same waveform). Here the spectrum is actually remapped.

### 5. Per-voice delay

After Stretch, each voice goes through a delay line with Catmull–Rom interpolation. The delay target is smoothed (~70 ms) so it does not zipper. Floor is 12 ms, so wet never sits on top of dry even at Spread = 0.

### 6. Mix (anti-comb)

Dry is read back from a delay equal to Stretch input+output latency. The host is told that latency (`setLatencySamples`), so Mix = 0 is the original, in time.

Dry is **not** faded out as Mix goes up. Mix only adds the two wet voices. A 50/50 replace (`dry*(1-mix)+wet*mix`) is the deepest comb: wherever the 12–28 ms copy is out of phase, the original gets a hole.

Wet is high-passed at 220 Hz before the add. A 20 ms delay puts a notch at 25 Hz; that hole would eat the bass. The double lives in mids/highs, bass stays the dry signal.

At Width = 1, the L voice stays left and the R voice stays right. Lower Width crossfades them toward the center.

```
outL = dryL + (wL * lToL + wR * rToL) * Mix
outR = dryR + (wL * lToR + wR * rToR) * Mix
```

Reported plugin latency is Stretch only. The extra 12–28 ms on the wet voices is the doubling, not compensation.

If a track is still phasey, Mix = 0 and use the plugin 100% wet on a **send** instead (no dry in the same insert).

## Check

```sh
./build/check_doubler
```

Offline self-check: mix=0 is dry, wet voices sit at the expected cents, correlation vs a delay copy is lower.

## AU cache

If `auval -v aufx Dblr Aeyl` fails with Component Name / Version errors after a rebuild, flush the Audio Unit cache:

```sh
killall -9 AudioComponentRegistrar
auval -v aufx Dblr Aeyl
```
