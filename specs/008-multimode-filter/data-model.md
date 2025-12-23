# Data Model: Multimode Filter

**Feature**: 008-multimode-filter
**Created**: 2025-12-23
**Layer**: 2 (DSP Processors)

This document defines the component structure and relationships for the Multimode Filter processor.

---

## Entity Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                        MultimodeFilter                               │
│  ─────────────────────────────────────────────────────────────────  │
│  - filterType_: FilterType                                          │
│  - slope_: FilterSlope                                              │
│  - cutoff_: float (Hz)                                              │
│  - resonance_: float (Q)                                            │
│  - gain_: float (dB, for Shelf/Peak)                                │
│  - drive_: float (dB)                                               │
│  - smoothingTime_: float (ms)                                       │
│  - sampleRate_: float                                               │
│  ─────────────────────────────────────────────────────────────────  │
│  + prepare(sampleRate, maxBlockSize) noexcept                       │
│  + reset() noexcept                                                 │
│  + process(buffer, numSamples) noexcept                             │
│  + processSample(input) noexcept → float                            │
│  + setType(FilterType) noexcept                                     │
│  + setSlope(FilterSlope) noexcept                                   │
│  + setCutoff(hz) noexcept                                           │
│  + setResonance(q) noexcept                                         │
│  + setGain(dB) noexcept                                             │
│  + setDrive(dB) noexcept                                            │
│  + setSmoothingTime(ms) noexcept                                    │
│  + getType() const noexcept → FilterType                            │
│  + getSlope() const noexcept → FilterSlope                          │
│  + getCutoff() const noexcept → float                               │
│  + getResonance() const noexcept → float                            │
│  + getGain() const noexcept → float                                 │
│  + getDrive() const noexcept → float                                │
│  + getLatency() const noexcept → size_t                             │
└─────────────────────────────────────────────────────────────────────┘
        │ composes
        ▼
┌─────────────────────┬─────────────────────┬─────────────────────────┐
│                     │                     │                         │
│  FilterCore         │  ParameterSmooth    │  DriveProcessor         │
│  (internal)         │  (internal)         │  (internal)             │
│                     │                     │                         │
│  - biquad1_         │  - cutoffSmooth_    │  - oversampler_         │
│  - biquad2_         │  - resonanceSmooth_ │  - driveGain_           │
│  - biquad3_         │  - gainSmooth_      │  - bypassDrive_         │
│  - biquad4_         │  - driveSmooth_     │                         │
│  - activeStages_    │                     │                         │
│                     │                     │                         │
└─────────────────────┴─────────────────────┴─────────────────────────┘
        │                     │                     │
        │ uses                │ uses                │ uses
        ▼                     ▼                     ▼
┌───────────────────────────────────────────────────────────────────────┐
│                         Layer 1 Primitives                            │
├───────────────────┬───────────────────┬───────────────────────────────┤
│ Biquad            │ OnePoleSmoother   │ Oversampler2xMono             │
│ SmoothedBiquad    │                   │                               │
│ BiquadCoefficients│                   │                               │
│ FilterType        │                   │                               │
│ butterworthQ()    │                   │                               │
└───────────────────┴───────────────────┴───────────────────────────────┘
```

---

## Entities

### FilterSlope (Enumeration)

New enum to select filter slope (applies to LP/HP/BP/Notch only).

```cpp
namespace Iterum::DSP {
    enum class FilterSlope : uint8_t {
        Slope12dB = 1,  // 12 dB/oct (1 stage)
        Slope24dB = 2,  // 24 dB/oct (2 stages)
        Slope36dB = 3,  // 36 dB/oct (3 stages)
        Slope48dB = 4   // 48 dB/oct (4 stages)
    };

    // Helper to get number of stages
    [[nodiscard]] constexpr size_t slopeToStages(FilterSlope slope) noexcept {
        return static_cast<size_t>(slope);
    }

    // Helper to get dB/octave
    [[nodiscard]] constexpr float slopeTodBPerOctave(FilterSlope slope) noexcept {
        return static_cast<float>(static_cast<size_t>(slope) * 12);
    }
}
```

**Validation**: Ignored for Allpass, LowShelf, HighShelf, Peak (always 1 stage).

---

### MultimodeFilter (Main Class)

The primary processor class composing Layer 1 primitives.

```cpp
namespace Iterum::DSP {
    class MultimodeFilter {
    public:
        // Lifecycle
        void prepare(double sampleRate, size_t maxBlockSize) noexcept;
        void reset() noexcept;

        // Block processing (efficient, smoothed parameters)
        void process(float* buffer, size_t numSamples) noexcept;

        // Sample-by-sample processing (for modulation sources)
        [[nodiscard]] float processSample(float input) noexcept;

        // Parameter setters (all real-time safe)
        void setType(FilterType type) noexcept;
        void setSlope(FilterSlope slope) noexcept;  // Ignored for Allpass/Shelf/Peak
        void setCutoff(float hz) noexcept;          // Clamped to [20, Nyquist/2]
        void setResonance(float q) noexcept;        // Clamped to [0.1, 100]
        void setGain(float dB) noexcept;            // For Shelf/Peak only [-24, +24]
        void setDrive(float dB) noexcept;           // [0, 24] - 0 = bypass
        void setSmoothingTime(float ms) noexcept;   // [0, 100] - 0 = instant

        // Parameter getters
        [[nodiscard]] FilterType getType() const noexcept;
        [[nodiscard]] FilterSlope getSlope() const noexcept;
        [[nodiscard]] float getCutoff() const noexcept;
        [[nodiscard]] float getResonance() const noexcept;
        [[nodiscard]] float getGain() const noexcept;
        [[nodiscard]] float getDrive() const noexcept;

        // Query
        [[nodiscard]] size_t getLatency() const noexcept;  // From oversampler (0 for Economy)
        [[nodiscard]] bool isPrepared() const noexcept;
        [[nodiscard]] double sampleRate() const noexcept;

    private:
        // State
        FilterType type_ = FilterType::Lowpass;
        FilterSlope slope_ = FilterSlope::Slope12dB;
        float cutoff_ = 1000.0f;
        float resonance_ = 0.707f;  // Butterworth Q
        float gain_ = 0.0f;
        float drive_ = 0.0f;
        float smoothingTime_ = 5.0f;
        double sampleRate_ = 44100.0;
        bool prepared_ = false;

        // Filter stages (pre-allocated, not all used)
        // Using std::array<Biquad, 4> instead of BiquadCascade<N> template
        // to allow runtime slope changes without template complexity
        std::array<Biquad, 4> stages_;
        size_t activeStages_ = 1;

        // Parameter smoothing
        OnePoleSmoother cutoffSmooth_;
        OnePoleSmoother resonanceSmooth_;
        OnePoleSmoother gainSmooth_;
        OnePoleSmoother driveSmooth_;

        // Drive processing
        Oversampler<2, 1> oversampler_;  // 2x mono
        std::vector<float> oversampledBuffer_;

        // Internal methods
        void updateCoefficients() noexcept;
        void applyDrive(float* buffer, size_t numSamples) noexcept;
    };
}
```

---

## Parameter Ranges and Defaults

| Parameter | Type | Range | Default | Notes |
|-----------|------|-------|---------|-------|
| type | FilterType | 8 types | Lowpass | From existing enum |
| slope | FilterSlope | 12-48 dB | 12dB | Ignored for Allpass/Shelf/Peak |
| cutoff | float | 20 - Nyquist/2 Hz | 1000 Hz | Clamped at boundaries |
| resonance | float | 0.1 - 100 Q | 0.707 | Butterworth = sqrt(2)/2 |
| gain | float | -24 to +24 dB | 0 dB | Only for Shelf/Peak |
| drive | float | 0 - 24 dB | 0 dB | 0 = bypass (no oversampling) |
| smoothingTime | float | 0 - 100 ms | 5 ms | 0 = instant (may click) |

---

## Filter Type Behavior Matrix

| FilterType | Uses Slope | Uses Gain | Uses Resonance | Stages |
|------------|------------|-----------|----------------|--------|
| Lowpass | Yes | No | Yes | 1-4 |
| Highpass | Yes | No | Yes | 1-4 |
| Bandpass | Yes | No | Yes | 1-4 |
| Notch | Yes | No | Yes | 1-4 |
| Allpass | No (fixed 1) | No | Yes | 1 |
| LowShelf | No (fixed 1) | Yes | Yes | 1 |
| HighShelf | No (fixed 1) | Yes | Yes | 1 |
| Peak | No (fixed 1) | Yes | Yes | 1 |

---

## Memory Layout

```
MultimodeFilter (~1.5 KB typical)
├── Parameters: 32 bytes
│   ├── type_: 1 byte
│   ├── slope_: 1 byte
│   ├── cutoff_: 4 bytes
│   ├── resonance_: 4 bytes
│   ├── gain_: 4 bytes
│   ├── drive_: 4 bytes
│   ├── smoothingTime_: 4 bytes
│   ├── sampleRate_: 8 bytes
│   └── prepared_: 1 byte
├── Filter Stages: 256 bytes (4 x Biquad @ ~64 bytes each)
│   └── Each Biquad: coefficients (5 floats) + state (2 floats) + padding
├── Parameter Smoothers: 64 bytes (4 x OnePoleSmoother @ ~16 bytes each)
├── Oversampler: ~512 bytes (IIR filters + small buffer)
└── Oversampled Buffer: maxBlockSize * 2 * sizeof(float)
```

---

## State Transitions

```
                    ┌──────────────┐
                    │ Unprepared   │
                    │ (default)    │
                    └──────┬───────┘
                           │ prepare()
                           ▼
                    ┌──────────────┐
        ┌──────────▶│  Prepared    │◀──────────┐
        │           │  (Ready)     │           │
        │           └──────┬───────┘           │
        │                  │                   │
        │     setType()    │    process()      │
        │     setCutoff()  │    processSample()│
        │     setSlope()   │                   │
        │     etc.         ▼                   │
        │           ┌──────────────┐           │
        │           │  Processing  │───────────┤
        │           │              │  reset()  │
        │           └──────────────┘           │
        │                                      │
        └──────────────────────────────────────┘
                        (stays prepared)
```

---

## Dependencies (Layer 1 → Layer 2)

```
MultimodeFilter (Layer 2)
    │
    ├── Biquad (Layer 1) ─── biquad.h
    ├── SmoothedBiquad (Layer 1) ─── biquad.h (optional, for future)
    ├── BiquadCoefficients (Layer 1) ─── biquad.h
    ├── FilterType (Layer 1) ─── biquad.h
    ├── butterworthQ() (Layer 1) ─── biquad.h
    ├── OnePoleSmoother (Layer 1) ─── smoother.h
    ├── Oversampler<2,1> (Layer 1) ─── oversampler.h
    └── dbToGain() (Layer 0) ─── db_utils.h
```

All dependencies are from lower layers (0 and 1), satisfying Constitution Principle IX.
