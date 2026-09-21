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
| Mix | 0.5 | Dry / wet |
| Spread | 0.7 | How far L/R cents and delays diverge |
| Humanize | 0.5 | Random walk on pitch/delay plus delayed vibrato from the tracker |
| Width | 1.0 | Stereo placement of the two wet voices |

Base offsets at full spread: L `+12` cents / `20` ms, R `-9` cents / `28` ms.

Input: mono or stereo. Output: stereo.

## DSP

YIN tracks pitch. Each voice is transposed with Signalsmith Stretch (`presetCheaper`), then cubic-interpolated delay. Dry is delayed by Stretch latency so mix=0 is the original, aligned.

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
