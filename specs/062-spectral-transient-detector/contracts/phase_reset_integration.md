# API Contract: Phase Reset Integration

**Feature**: 062-spectral-transient-detector
**Target**: `PhaseVocoderPitchShifter` in `dsp/include/krate/dsp/processors/pitch_shift_processor.h`

> **Test Subject**: Integration tests in `phase_reset_test.cpp` drive the feature through the `PitchShiftProcessor` public API (not `PhaseVocoderPitchShifter` directly). The `PhaseVocoderPitchShifter` changes documented here are internal implementation; callers always use `PitchShiftProcessor`.

## New Public Methods

```cpp
/// @brief Enable or disable transient-aware phase reset.
/// When enabled, synthesis phases are reset to analysis phases at transient frames.
/// Independent of phase locking -- both can be enabled simultaneously.
/// Phase reset is disabled by default.
void setPhaseReset(bool enabled) noexcept;

/// @brief Get phase reset state
[[nodiscard]] bool getPhaseReset() const noexcept;
```

## New Private Members

```cpp
// Transient detection for phase reset
SpectralTransientDetector transientDetector_;  // Spectral flux onset detector
bool phaseResetEnabled_ = false;               // Independent toggle (default: off)
```

## New Include

```cpp
#include <krate/dsp/primitives/spectral_transient_detector.h>
```

## Integration Point in processFrame()

The transient detection and phase reset must be inserted **after** Step 1a (formant envelope extraction, line ~1151) and **before** Step 1c (phase locking setup, line ~1154). This is named "Step 1b-reset" to avoid renumbering existing steps.

### Pseudocode

```cpp
void processFrame(float pitchRatio) noexcept {
    const std::size_t numBins = kFFTSize / 2 + 1;

    // Step 1: Extract magnitude and compute instantaneous frequency
    // (existing code, lines 1127-1146)
    for (std::size_t k = 0; k < numBins; ++k) {
        magnitude_[k] = analysisSpectrum_.getMagnitude(k);
        // ... phase diff, deviation, frequency computation ...
    }

    // Step 1a: Formant envelope extraction (existing, lines 1149-1151)
    // ...

    // >>> NEW: Step 1b-reset: Transient detection and phase reset
    if (phaseResetEnabled_) {
        bool isTransient = transientDetector_.detect(magnitude_.data(), numBins);
        if (isTransient) {
            // Reset synthesis phases to analysis phases (Duxbury et al. 2002)
            for (std::size_t k = 0; k < numBins; ++k) {
                synthPhase_[k] = prevPhase_[k];
            }
        }
    }
    // <<< END NEW

    // Step 1c: Phase locking setup (existing, lines 1154-1193)
    // ...

    // Step 2: Pitch shift (existing, lines 1203-1314)
    // ... (this uses synthPhase_[], which may have been reset above)

    // Step 3: Formant preservation (existing, lines 1317-1339)
    // ...
}
```

### Ordering Rationale

1. **After magnitude extraction**: The detector consumes `magnitude_[]`, which must be computed first.
2. **Before phase locking setup**: Phase reset overwrites `synthPhase_[]`. Phase locking then operates on the reset phases (for peaks) or derives locked phases from them (for non-peaks). This means transient frames get both phase reset AND phase locking, producing the cleanest possible transient reconstruction.
3. **Independent of phase locking**: If phase locking is disabled, the reset phases feed directly into the basic per-bin accumulation path. If phase locking is enabled, the reset phases serve as the starting point for locked phase propagation.

## Lifecycle Integration

### prepare()

```cpp
void prepare(double sampleRate, std::size_t /*maxBlockSize*/) noexcept {
    // ... existing prepare code ...

    const std::size_t numBins = kFFTSize / 2 + 1;

    // Prepare transient detector
    transientDetector_.prepare(numBins);

    // ... rest of existing prepare ...
}
```

### reset()

```cpp
void reset() noexcept {
    // ... existing reset code ...

    transientDetector_.reset();

    // ... rest of existing reset ...
}
```

## PitchShiftProcessor Public API Extensions

The `PitchShiftProcessor` wrapper must expose the phase reset toggle through its public API.

### New Methods on PitchShiftProcessor

```cpp
/// @brief Enable or disable transient-aware phase reset for PhaseVocoder mode.
/// Only effective when mode is PitchMode::PhaseVocoder.
/// @param enable true to enable, false to disable
void setPhaseReset(bool enable) noexcept;

/// @brief Get phase reset state
/// @return true if phase reset is enabled
[[nodiscard]] bool getPhaseReset() const noexcept;
```

### Implementation (inline, in PitchShiftProcessor)

```cpp
inline void PitchShiftProcessor::setPhaseReset(bool enable) noexcept {
    pImpl_->phaseVocoderShifter.setPhaseReset(enable);
}

inline bool PitchShiftProcessor::getPhaseReset() const noexcept {
    return pImpl_->phaseVocoderShifter.getPhaseReset();
}
```
