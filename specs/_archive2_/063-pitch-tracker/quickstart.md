# Quickstart: Pitch Tracking Robustness (063)

## What This Feature Does

Wraps the existing `PitchDetector` with a 5-stage post-processing pipeline to produce stable, musically coherent pitch tracking suitable for driving a diatonic harmonizer. The pipeline eliminates jitter, octave jumps, and noise-induced pitch errors.

## Architecture

```
Layer 1 (Primitives)
    pitch_tracker.h  -- NEW file, header-only

    Depends on:
    ├── pitch_detector.h (Layer 1) -- wrapped, not reimplemented
    ├── smoother.h (Layer 1) -- OnePoleSmoother for stage 5
    ├── pitch_utils.h (Layer 0) -- frequencyToMidiNote()
    └── midi_utils.h (Layer 0) -- midiNoteToFrequency()
```

## Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/primitives/pitch_tracker.h` | Header-only PitchTracker class |
| `dsp/tests/unit/primitives/pitch_tracker_test.cpp` | Catch2 unit tests |

## Files to Modify

| File | Change |
|------|--------|
| `dsp/CMakeLists.txt` | Add `pitch_tracker.h` to `KRATE_DSP_PRIMITIVES_HEADERS` |
| `dsp/tests/CMakeLists.txt` | Add `pitch_tracker_test.cpp` to test sources and fno-fast-math list |
| `specs/_architecture_/layer-1-primitives.md` | Add PitchTracker documentation |

## Usage Example

```cpp
#include <krate/dsp/primitives/pitch_tracker.h>

Krate::DSP::PitchTracker tracker;

// In setupProcessing():
tracker.prepare(sampleRate, 256);

// Optional configuration:
tracker.setConfidenceThreshold(0.5f);
tracker.setHysteresisThreshold(50.0f);
tracker.setMinNoteDuration(50.0f);
tracker.setMedianFilterSize(5);

// In process() audio callback:
tracker.pushBlock(inputSamples, numSamples);

if (tracker.isPitchValid()) {
    int midiNote = tracker.getMidiNote();    // Committed, stable note
    float freq = tracker.getFrequency();      // Smoothed Hz for pitch shift
    float conf = tracker.getConfidence();     // Raw detector confidence
}
```

## Key Design Decisions

1. **Hop-aligned pipeline**: Runs once per `windowSize/4` samples, synchronized with PitchDetector's internal detection rhythm.

2. **Median filter fed only by confident frames**: Low-confidence frames skip the ring buffer entirely, preventing noise from contaminating the median.

3. **Hysteresis uses MIDI note difference**: `abs(frequencyToMidiNote(median) - committedNote) * 100` gives cents distance from committed note specifically (not from nearest note).

4. **First detection bypasses hysteresis + min duration**: Provides immediate musical responsiveness on startup.

5. **getMidiNote() is independent of smoother**: Returns the committed note directly from stage 4. The smoother (stage 5) only affects `getFrequency()`.

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run PitchTracker tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "PitchTracker*"
```
