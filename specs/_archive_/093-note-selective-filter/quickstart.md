# Quickstart: Note-Selective Filter Implementation

**Date**: 2026-01-24 | **Spec**: 093-note-selective-filter

## Overview

Step-by-step guide to implementing the NoteSelectiveFilter processor.

---

## Prerequisites

Before starting:
1. Read [spec.md](spec.md) for requirements
2. Read [data-model.md](data-model.md) for entity definitions
3. Read [research.md](research.md) for design decisions

---

## Implementation Order

### Step 1: Add Layer 0 Utilities (pitch_utils.h)

**File**: `dsp/include/krate/dsp/core/pitch_utils.h`

Add after existing functions:

```cpp
/// Convert frequency in Hz to note class (0-11)
/// Note class: 0=C, 1=C#, 2=D, 3=D#, 4=E, 5=F, 6=F#, 7=G, 8=G#, 9=A, 10=A#, 11=B
/// @param hz Frequency in Hz
/// @return Note class (0-11) or -1 if frequency is invalid
[[nodiscard]] inline int frequencyToNoteClass(float hz) noexcept {
    if (hz <= 0.0f) return -1;
    // MIDI note = 12 * log2(hz/440) + 69, where A440 = MIDI 69
    float midiNote = 12.0f * std::log2(hz / 440.0f) + 69.0f;
    // Note class 0-11 (C=0)
    int noteClass = static_cast<int>(std::round(midiNote)) % 12;
    if (noteClass < 0) noteClass += 12;  // Handle negative modulo
    return noteClass;
}

/// Calculate cents deviation from nearest note center
/// @param hz Frequency in Hz
/// @return Cents deviation (-50 to +50), or 0 if invalid
[[nodiscard]] inline float frequencyToCentsDeviation(float hz) noexcept {
    if (hz <= 0.0f) return 0.0f;
    float midiNote = 12.0f * std::log2(hz / 440.0f) + 69.0f;
    float roundedNote = std::round(midiNote);
    return (midiNote - roundedNote) * 100.0f;
}
```

**Tests to write first** (`dsp/tests/unit/core/pitch_utils_test.cpp`):
- frequencyToNoteClass(440.0f) == 9 (A)
- frequencyToNoteClass(261.63f) == 0 (C)
- frequencyToNoteClass(0.0f) == -1
- frequencyToCentsDeviation(440.0f) ~= 0.0f
- frequencyToCentsDeviation(452.89f) ~= 50.0f (A + 50 cents)

---

### Step 2: Create NoteSelectiveFilter Header

**File**: `dsp/include/krate/dsp/processors/note_selective_filter.h`

```cpp
// ==============================================================================
// Layer 2: DSP Processor - Note-Selective Filter
// ==============================================================================
// Applies filtering only to audio matching specific note classes (C, C#, D, etc.),
// passing non-matching notes through dry. Uses pitch detection to identify the
// current note, then crossfades between dry and filtered signal.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, atomics)
// - Principle IX: Layer 2 (depends only on Layer 0/1)
// - Principle X: DSP Constraints (filter always hot, denormal prevention)
// - Principle XIII: Test-First Development
//
// Reference: specs/093-note-selective-filter/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/primitives/pitch_detector.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/svf.h>

#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// ... (see contracts/note_selective_filter.h for full implementation)

} // namespace DSP
} // namespace Krate
```

---

### Step 3: Implement Core Processing

Key implementation details:

#### prepare() Method

```cpp
void prepare(double sampleRate, int maxBlockSize) noexcept {
    sampleRate_ = (sampleRate >= 1000.0) ? sampleRate : 1000.0;

    // Configure pitch detector
    pitchDetector_.prepare(sampleRate_, PitchDetector::kDefaultWindowSize);

    // Configure filter
    filter_.prepare(sampleRate_);
    filter_.setMode(static_cast<SVFMode>(filterType_.load(std::memory_order_relaxed)));
    filter_.setCutoff(cutoffHz_.load(std::memory_order_relaxed));
    filter_.setResonance(resonance_.load(std::memory_order_relaxed));

    // Configure crossfade smoother
    crossfadeSmoother_.configure(
        crossfadeTimeMs_.load(std::memory_order_relaxed),
        static_cast<float>(sampleRate_));
    crossfadeSmoother_.snapTo(0.0f);  // Start dry

    // Set block update interval (~512 samples or provided)
    blockUpdateInterval_ = static_cast<size_t>(maxBlockSize > 0 ? maxBlockSize : 512);

    prepared_ = true;
}
```

#### process() Method

```cpp
[[nodiscard]] float process(float input) noexcept {
    if (!prepared_) return input;

    // Handle NaN/Inf
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();
        return 0.0f;
    }

    // Push to pitch detector
    pitchDetector_.push(input);
    ++samplesSinceNoteUpdate_;

    // Block-rate note matching update
    if (samplesSinceNoteUpdate_ >= blockUpdateInterval_) {
        updateNoteMatching();
        samplesSinceNoteUpdate_ = 0;
    }

    // Always process through filter (keeps state hot)
    float filtered = filter_.process(input);

    // Apply crossfade
    float crossfade = crossfadeSmoother_.process();
    float output = (1.0f - crossfade) * input + crossfade * filtered;

    return detail::flushDenormal(output);
}
```

#### updateNoteMatching() Method

```cpp
void updateNoteMatching() noexcept {
    float frequency = pitchDetector_.getDetectedFrequency();
    float confidence = pitchDetector_.getConfidence();
    float threshold = confidenceThreshold_.load(std::memory_order_relaxed);

    float crossfadeTarget = 0.0f;  // Default: dry

    if (confidence >= threshold) {
        // Valid pitch detected
        int noteClass = frequencyToNoteClass(frequency);
        float centsDeviation = std::abs(frequencyToCentsDeviation(frequency));
        float tolerance = noteTolerance_.load(std::memory_order_relaxed);

        // Check if note is in target set and within tolerance
        std::bitset<12> targets(targetNotes_.load(std::memory_order_relaxed));
        bool noteMatches = noteClass >= 0 && noteClass < 12 &&
                          targets.test(static_cast<size_t>(noteClass)) &&
                          centsDeviation <= tolerance;

        crossfadeTarget = noteMatches ? 1.0f : 0.0f;
        lastDetectedNote_ = noteClass;
        lastFilteringState_ = noteMatches;
    } else {
        // No valid pitch - apply NoDetectionMode
        auto mode = static_cast<NoDetectionMode>(
            noDetectionMode_.load(std::memory_order_relaxed));

        switch (mode) {
            case NoDetectionMode::Dry:
                crossfadeTarget = 0.0f;
                break;
            case NoDetectionMode::Filtered:
                crossfadeTarget = 1.0f;
                break;
            case NoDetectionMode::LastState:
                crossfadeTarget = lastFilteringState_ ? 1.0f : 0.0f;
                break;
        }
        lastDetectedNote_ = -1;
    }

    crossfadeSmoother_.setTarget(crossfadeTarget);
}
```

---

### Step 4: Write Tests

**File**: `dsp/tests/unit/processors/note_selective_filter_test.cpp`

#### Test Structure

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <krate/dsp/processors/note_selective_filter.h>
#include <cmath>

using namespace Krate::DSP;
using Catch::Approx;

// Helper: generate sine wave at given frequency
std::vector<float> generateSine(float frequency, float sampleRate, size_t samples) {
    std::vector<float> buffer(samples);
    for (size_t i = 0; i < samples; ++i) {
        buffer[i] = std::sin(2.0f * 3.14159265f * frequency * i / sampleRate);
    }
    return buffer;
}

TEST_CASE("NoteSelectiveFilter - Note matching", "[processors]") {
    NoteSelectiveFilter filter;
    filter.prepare(44100.0, 512);

    SECTION("C4 is filtered when note C is enabled") {
        std::bitset<12> notes;
        notes.set(0);  // C
        filter.setTargetNotes(notes);
        filter.setCutoff(500.0f);

        auto input = generateSine(261.63f, 44100.0f, 4410);  // 100ms of C4
        // Process and verify filtering is applied...
    }

    SECTION("D4 passes through dry when only C is enabled") {
        std::bitset<12> notes;
        notes.set(0);  // C only
        filter.setTargetNotes(notes);

        auto input = generateSine(293.66f, 44100.0f, 4410);  // 100ms of D4
        // Process and verify output matches input...
    }
}

TEST_CASE("NoteSelectiveFilter - Crossfade smoothing", "[processors]") {
    // Test smooth transitions, no clicks...
}

TEST_CASE("NoteSelectiveFilter - No detection modes", "[processors]") {
    // Test Dry, Filtered, LastState modes...
}

TEST_CASE("NoteSelectiveFilter - Thread safety", "[processors]") {
    // Verify atomic parameter updates don't cause crashes...
}
```

---

## Test-First Checklist

Before implementing each component:

- [ ] Write test for frequencyToNoteClass()
- [ ] Write test for frequencyToCentsDeviation()
- [ ] Write test for note matching (US-1 scenarios)
- [ ] Write test for smooth transitions (US-2 scenarios)
- [ ] Write test for tolerance behavior (US-3 scenarios)
- [ ] Write test for no-detection modes (US-4 scenarios)
- [ ] Write test for edge cases (all notes, no notes, octave spanning)
- [ ] Write performance benchmark

---

## Common Pitfalls

1. **Forgetting to call prepare()**: Always check `prepared_` flag
2. **Atomic memory order**: Use `std::memory_order_relaxed` for performance
3. **Bitset conversion**: `std::bitset<12>` to/from `uint16_t` for atomics
4. **Filter always hot**: Don't bypass filter processing, only crossfade output
5. **Block-rate updates**: Don't check note matching every sample
6. **Negative modulo**: C++ % can return negative; add 12 if needed

---

## Build Verification

After implementation:

```bash
# Configure
cmake --preset windows-x64-release

# Build DSP tests
cmake --build build/windows-x64-release --config Release --target dsp_tests

# Run tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe --reporter compact
```

Expected: All tests pass with zero warnings.
