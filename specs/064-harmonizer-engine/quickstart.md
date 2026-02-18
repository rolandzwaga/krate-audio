# Quickstart: Multi-Voice Harmonizer Engine (064)

## What This Feature Does

Orchestrates existing DSP components into a complete multi-voice harmonizer system. Generates up to 4 pitch-shifted harmony voices from a mono input, with two intelligence modes (Chromatic and Scalic), per-voice level/pan/delay/detune, shared pitch tracking, click-free transitions, and mono-to-stereo constant-power panning.

## Architecture

```
Layer 3 (Systems)
    harmonizer_engine.h  -- NEW file, header-only

    Depends on:
    +-- pitch_shift_processor.h (Layer 2) -- per-voice pitch shifting (4 modes)
    +-- pitch_tracker.h (Layer 1) -- shared pitch detection (Scalic mode)
    +-- smoother.h (Layer 1) -- OnePoleSmoother for click-free transitions
    +-- delay_line.h (Layer 1) -- per-voice onset delay
    +-- scale_harmonizer.h (Layer 0) -- diatonic interval computation
    +-- db_utils.h (Layer 0) -- dB-to-linear conversion
    +-- math_constants.h (Layer 0) -- kPi for constant-power panning
```

## Signal Flow

```
Input (mono) -----------------------------------------------+---> Dry Path
  |                                                         |
  +--> PitchTracker (shared, Scalic only)                   |
  |       |                                                 |
  |       +--> ScaleHarmonizer (shared)                     |
  |               |                                         |
  +--> Voice 0: [DelayLine] -> [PitchShiftProcessor] -> [Level/Pan] --+
  +--> Voice 1: [DelayLine] -> [PitchShiftProcessor] -> [Level/Pan] --+-> Harmony Bus
  +--> Voice 2: [DelayLine] -> [PitchShiftProcessor] -> [Level/Pan] --+     |
  +--> Voice 3: [DelayLine] -> [PitchShiftProcessor] -> [Level/Pan] --+     |
                                                                      v     v
                                                   outputL = dryGain*input + wetGain*harmonyL
                                                   outputR = dryGain*input + wetGain*harmonyR
```

## Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/systems/harmonizer_engine.h` | Header-only HarmonizerEngine class + HarmonyMode enum |
| `dsp/tests/unit/systems/harmonizer_engine_test.cpp` | Catch2 unit tests |

## Files to Modify

| File | Change |
|------|--------|
| `dsp/CMakeLists.txt` | Add `harmonizer_engine.h` to `KRATE_DSP_SYSTEMS_HEADERS` |
| `dsp/tests/CMakeLists.txt` | Add `harmonizer_engine_test.cpp` to test sources and `-fno-fast-math` list |
| `specs/_architecture_/layer-3-systems.md` | Add HarmonizerEngine documentation |

## Usage Example

```cpp
#include <krate/dsp/systems/harmonizer_engine.h>

Krate::DSP::HarmonizerEngine harmonizer;

// In setupProcessing():
harmonizer.prepare(sampleRate, maxBlockSize);

// Configure harmony mode and scale:
harmonizer.setHarmonyMode(Krate::DSP::HarmonyMode::Scalic);
harmonizer.setKey(0);  // C
harmonizer.setScale(Krate::DSP::ScaleType::Major);

// Configure pitch shift algorithm:
harmonizer.setPitchShiftMode(Krate::DSP::PitchMode::PhaseVocoder);
harmonizer.setFormantPreserve(true);

// Configure voices:
harmonizer.setNumVoices(2);

harmonizer.setVoiceInterval(0, 2);      // Voice 0: 3rd above (diatonic)
harmonizer.setVoiceLevel(0, 0.0f);      // 0 dB
harmonizer.setVoicePan(0, -0.5f);       // Slightly left
harmonizer.setVoiceDetune(0, 5.0f);     // +5 cents

harmonizer.setVoiceInterval(1, 4);      // Voice 1: 5th above (diatonic)
harmonizer.setVoiceLevel(1, -3.0f);     // -3 dB
harmonizer.setVoicePan(1, 0.5f);        // Slightly right
harmonizer.setVoiceDetune(1, -5.0f);    // -5 cents

// Set dry/wet levels:
harmonizer.setDryLevel(0.0f);           // Dry at unity
harmonizer.setWetLevel(-3.0f);          // Wet at -3 dB

// In processAudio() callback (every block):
harmonizer.process(monoInput, outputL, outputR, numSamples);

// Optional: UI feedback
if (harmonizer.getDetectedNote() >= 0) {
    int midiNote = harmonizer.getDetectedNote();
    float freq = harmonizer.getDetectedPitch();
    float confidence = harmonizer.getPitchConfidence();
}

// Latency compensation:
std::size_t latency = harmonizer.getLatencySamples();
```

## Chromatic Mode Example

```cpp
// Simple fixed-interval pitch shifting (no pitch tracking needed):
harmonizer.setHarmonyMode(Krate::DSP::HarmonyMode::Chromatic);
harmonizer.setNumVoices(1);
harmonizer.setVoiceInterval(0, 7);   // Perfect fifth up (always +7 semitones)
harmonizer.setVoiceLevel(0, 0.0f);
harmonizer.setVoicePan(0, 0.0f);     // Center

// PitchTracker is NOT fed audio in Chromatic mode -- zero CPU overhead
harmonizer.process(monoInput, outputL, outputR, numSamples);
```

## Key Design Decisions

1. **Block-per-voice architecture**: PitchShiftProcessor has a block-level `process()` API, so each voice processes its entire block through the delay line and pitch shifter before mixing. Two shared scratch buffers (`delayScratch_`, `voiceScratch_`) are pre-allocated in `prepare()`.

2. **Independent dry/wet smoothing**: Two separate `OnePoleSmoother` instances at 10ms. Smoothing a single mix ratio is forbidden (FR-007) because it makes equal-power compensation harder and automation nonlinear.

3. **Constant-power panning**: Uses the same formula as `UnisonEngine`: `angle = (pan + 1) * kPi * 0.25f`, `leftGain = cos(angle)`, `rightGain = sin(angle)`. Ensures `leftGain^2 + rightGain^2 = 1` at all pan positions.

4. **Double smoothing accepted for pitch**: HarmonizerEngine applies a 10ms OnePoleSmoother, and PitchShiftProcessor has its own internal 10ms smoother. The combined effect slightly softens transitions, which is musically desirable.

5. **Mute threshold at -60 dB**: Voices with level <= -60 dB skip all processing (no PitchShiftProcessor call). This is both correct (inaudible) and an optimization.

6. **Shared-analysis FFT deferred**: FR-020 specifies shared FFT analysis in PhaseVocoder mode, but PitchShiftProcessor's opaque pImpl API prevents injecting an external analysis spectrum. Phase 1 uses independent per-voice instances. A future Layer 2 spec will add the shared-analysis optimization.

7. **Pre-unprepared guard**: `process()` zero-fills outputs and returns if `isPrepared()` is false (FR-015). This prevents undefined behavior in hosts that call process before setup.

## Smoothing Time Constants

| Parameter | Time Constant | Rationale |
|-----------|---------------|-----------|
| Pitch shift (interval changes) | 10ms | Fast glide, prevents clicks on note changes |
| Per-voice level | 5ms | Small magnitude changes, fast response |
| Per-voice pan | 5ms | Small magnitude changes, fast response |
| Dry level | 10ms | Full-range swings, comb-filtering risk |
| Wet level | 10ms | Full-range swings, comb-filtering risk |

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run HarmonizerEngine tests
build/windows-x64-release/bin/Release/dsp_tests.exe "HarmonizerEngine*"
```
