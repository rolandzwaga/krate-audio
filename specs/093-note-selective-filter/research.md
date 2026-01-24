# Research: Note-Selective Filter

**Date**: 2026-01-24 | **Spec**: 093-note-selective-filter

## Overview

Research conducted to resolve technical unknowns before implementing the NoteSelectiveFilter processor.

---

## Research Task 1: PitchDetector API and Usage Patterns

### Question
How does the existing PitchDetector work, and how should it be integrated for block-rate note matching?

### Findings

The PitchDetector is a Layer 1 primitive located at `dsp/include/krate/dsp/primitives/pitch_detector.h`:

```cpp
class PitchDetector {
public:
    static constexpr std::size_t kDefaultWindowSize = 256;  // ~5.8ms at 44.1kHz
    static constexpr float kMinFrequency = 50.0f;
    static constexpr float kMaxFrequency = 1000.0f;
    static constexpr float kConfidenceThreshold = 0.3f;

    void prepare(double sampleRate, std::size_t windowSize = kDefaultWindowSize) noexcept;
    void reset() noexcept;
    void push(float sample) noexcept;         // Pushes sample, auto-detects every windowSize/4
    void pushBlock(const float* samples, std::size_t numSamples) noexcept;
    void detect() noexcept;                   // Force detection now

    [[nodiscard]] float getDetectedFrequency() const noexcept;
    [[nodiscard]] float getConfidence() const noexcept;
    [[nodiscard]] bool isPitchValid() const noexcept;
};
```

Key observations:
1. Detection runs automatically every `windowSize/4` samples (~64 at default)
2. This aligns with spec requirement for block-rate updates (~512 samples)
3. Confidence threshold is configurable but defaults to 0.3
4. Detection range is 50-1000Hz

### Decision
Use PitchDetector as-is. Block-rate matching will query getDetectedFrequency() once per block start.

### Alternatives Rejected
- Custom pitch detector: Unnecessary duplication
- Sample-rate detection: Spec explicitly requires block-rate for stability

---

## Research Task 2: SVF API for Filter Type

### Question
What filter types are available and how should they be exposed?

### Findings

The SVF is a Layer 1 primitive at `dsp/include/krate/dsp/primitives/svf.h`:

```cpp
enum class SVFMode : uint8_t {
    Lowpass,    // 12 dB/oct lowpass
    Highpass,   // 12 dB/oct highpass
    Bandpass,   // Constant 0 dB peak
    Notch,      // Band-reject
    Allpass,    // Flat magnitude, phase shift
    Peak,       // Parametric EQ bell (uses gainDb)
    LowShelf,   // Boost/cut below cutoff
    HighShelf   // Boost/cut above cutoff
};

class SVF {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setMode(SVFMode mode) noexcept;
    void setCutoff(float hz) noexcept;
    void setResonance(float q) noexcept;
    void setGain(float dB) noexcept;  // For Peak/Shelf modes
    [[nodiscard]] float process(float input) noexcept;
};
```

Key observations:
1. TPT topology provides modulation-stable filtering (no clicks on parameter changes)
2. All 8 modes are relevant for creative sound design
3. setMode() can be called at any time safely

### Decision
Expose full SVFMode enum to users. Use SVF's setMode() directly.

### Alternatives Rejected
- Limited mode subset: Full flexibility is more valuable
- Custom filter wrapper: Unnecessary, SVF already has ideal API

---

## Research Task 3: OnePoleSmoother for Crossfade

### Question
How should crossfade smoothing be implemented using OnePoleSmoother?

### Findings

The OnePoleSmoother is a Layer 1 primitive at `dsp/include/krate/dsp/primitives/smoother.h`:

```cpp
class OnePoleSmoother {
public:
    void configure(float smoothTimeMs, float sampleRate) noexcept;
    void setTarget(float target) noexcept;
    [[nodiscard]] float process() noexcept;
    [[nodiscard]] float getCurrentValue() const noexcept;
    [[nodiscard]] bool isComplete() const noexcept;
    void snapTo(float value) noexcept;
    void reset() noexcept;
};
```

Key observations:
1. smoothTimeMs represents time to reach 99% of target (5 time constants)
2. This matches spec's definition: "5 time constants for exponential settling" (FR-014)
3. isComplete() returns true when within kCompletionThreshold (0.0001) of target
4. Exponential approach is natural for crossfades (fast initial response)

### Decision
Use OnePoleSmoother with 5ms default. Crossfade value: 0.0 = dry, 1.0 = filtered.

### Alternatives Rejected
- LinearRamp: Exponential is more natural for audio crossfades
- SlewLimiter: Fixed rate limiting not needed for this use case

---

## Research Task 4: Frequency-to-Note Conversion

### Question
How should frequency be converted to note class, and does this utility exist?

### Findings

Examined `dsp/include/krate/dsp/core/pitch_utils.h`:

```cpp
// Existing functions:
[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept;
[[nodiscard]] inline float ratioToSemitones(float ratio) noexcept;
[[nodiscard]] inline float quantizePitch(float semitones, PitchQuantMode mode) noexcept;
```

**No frequencyToNoteClass function exists.** Need to create one.

### Algorithm
Standard frequency-to-MIDI-to-note conversion:

```
MIDI note number = 12 * log2(frequency / 440) + 69
Note class = MIDI note mod 12
```

Where note class 0 = C, 1 = C#, 2 = D, ..., 9 = A, 10 = A#, 11 = B

### Implementation

```cpp
/// Convert frequency in Hz to note class (0-11)
/// @param hz Frequency in Hz
/// @return Note class (0=C, 1=C#, ..., 11=B) or -1 if invalid
[[nodiscard]] inline int frequencyToNoteClass(float hz) noexcept {
    if (hz <= 0.0f) return -1;
    // MIDI note = 12 * log2(hz/440) + 69
    float midiNote = 12.0f * std::log2(hz / 440.0f) + 69.0f;
    // Note class 0-11
    int noteClass = static_cast<int>(std::round(midiNote)) % 12;
    if (noteClass < 0) noteClass += 12;  // Handle negative modulo
    return noteClass;
}

/// Calculate deviation from nearest note center in cents
/// @param hz Frequency in Hz
/// @return Cents deviation from nearest note center (-50 to +50)
[[nodiscard]] inline float frequencyToCentsDeviation(float hz) noexcept {
    if (hz <= 0.0f) return 0.0f;
    float midiNote = 12.0f * std::log2(hz / 440.0f) + 69.0f;
    float roundedNote = std::round(midiNote);
    return (midiNote - roundedNote) * 100.0f;  // Semitones to cents
}
```

### Decision
Add `frequencyToNoteClass()` and `frequencyToCentsDeviation()` to pitch_utils.h.

### Verification
- A440 Hz: midiNote = 69, noteClass = 69 % 12 = 9 (A) - Correct
- C4 (261.63 Hz): midiNote = 60, noteClass = 60 % 12 = 0 (C) - Correct
- C0 (16.35 Hz): midiNote = 12, noteClass = 12 % 12 = 0 (C) - Correct

---

## Research Task 5: Atomic Parameter Patterns

### Question
How should thread-safe parameter updates be implemented?

### Findings

Examined `dsp/include/krate/dsp/processors/crossover_filter.h`:

```cpp
class CrossoverLR4 {
    // Atomic parameters (thread-safe setters)
    std::atomic<float> crossoverFrequency_{kDefaultFrequency};
    std::atomic<float> smoothingTimeMs_{kDefaultSmoothingMs};
    std::atomic<int> trackingMode_{static_cast<int>(TrackingMode::Efficient)};

    // Non-atomic state
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
};
```

Key observations:
1. std::atomic<float> with std::memory_order_relaxed for parameters
2. Enums stored as std::atomic<int> and cast on read
3. State variables (filter states, smoothers) are non-atomic
4. Pattern: UI thread writes atomics, audio thread reads atomics

### Challenge: Bitset<12>
std::bitset is not atomic. Options:
1. Use std::atomic<uint16_t> and convert
2. Use std::atomic<uint32_t> for future expansion
3. Use mutex (violates real-time safety)

### Decision
Use std::atomic<uint16_t> for target notes bitset. 16 bits > 12 notes needed.

```cpp
// Thread-safe parameter storage
std::atomic<uint16_t> targetNotes_{0};

// Setter (UI thread)
void setTargetNotes(std::bitset<12> notes) noexcept {
    targetNotes_.store(static_cast<uint16_t>(notes.to_ulong()),
                       std::memory_order_relaxed);
}

// Getter (audio thread)
std::bitset<12> getTargetNotes() const noexcept {
    return std::bitset<12>(targetNotes_.load(std::memory_order_relaxed));
}
```

---

## Summary of Decisions

| Area | Decision |
|------|----------|
| Pitch Detection | Use existing PitchDetector, query block-rate |
| Filter | Use SVF with full SVFMode enum |
| Crossfade | Use OnePoleSmoother, 5ms default |
| Frequency-to-Note | Add frequencyToNoteClass() to pitch_utils.h |
| Atomics | std::atomic<float> for params, std::atomic<uint16_t> for notes |

---

## Open Questions (None)

All technical unknowns have been resolved through research.
