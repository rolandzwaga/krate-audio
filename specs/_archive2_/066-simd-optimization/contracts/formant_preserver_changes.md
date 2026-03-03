# Contract: FormantPreserver SIMD Integration Changes

**Date**: 2026-02-18 | **Spec**: 066-simd-optimization

## Public API (UNCHANGED - FR-010)

The following public API signatures MUST NOT change:

```cpp
class FormantPreserver {
public:
    void extractEnvelope(const float* magnitudes, float* outputEnvelope) noexcept;
    void extractEnvelope(const float* magnitudes) noexcept;
    [[nodiscard]] const float* getEnvelope() const noexcept;
    void applyFormantPreservation(
        const float* shiftedMagnitudes,
        const float* originalEnvelope,
        const float* shiftedEnvelope,
        float* outputMagnitudes,
        std::size_t numBins) const noexcept;
    [[nodiscard]] std::size_t numBins() const noexcept;
};
```

## Internal Changes

### New Include

```cpp
#include <krate/dsp/core/spectral_simd.h>
```

### `extractEnvelope()` - Step 1 (FR-007)

**Before**:
```cpp
// Step 1: Compute log magnitude spectrum (symmetric for real signal)
for (std::size_t k = 0; k < numBins_; ++k) {
    float mag = std::max(magnitudes[k], kMinMagnitude);
    logMag_[k] = std::log10(mag);
}
```

**After**:
```cpp
// Step 1: Compute log magnitude spectrum using SIMD batch log10
// batchLog10 clamps non-positive inputs to kMinLogInput (== kMinMagnitude)
batchLog10(magnitudes, logMag_.data(), numBins_);
```

**Behavioral equivalence**: `kMinLogInput == kMinMagnitude == 1e-10f`. The clamping is now internal to `batchLog10()` instead of explicit in the caller.

### `reconstructEnvelope()` (FR-008)

**Before**:
```cpp
void reconstructEnvelope() noexcept {
    fft_.forward(cepstrum_.data(), complexBuf_.data());

    for (std::size_t k = 0; k < numBins_; ++k) {
        float logEnv = complexBuf_[k].real;
        envelope_[k] = std::pow(10.0f, logEnv);
        envelope_[k] = std::max(kMinMagnitude, std::min(envelope_[k], 1e6f));
    }
}
```

**After**:
```cpp
void reconstructEnvelope() noexcept {
    fft_.forward(cepstrum_.data(), complexBuf_.data());

    // Stage: copy Complex::real fields into contiguous buffer for batchPow10
    for (std::size_t k = 0; k < numBins_; ++k) {
        logMag_[k] = complexBuf_[k].real;
    }

    // batchPow10 clamps output to [kMinLogInput, kMaxPow10Output] = [1e-10, 1e6]
    batchPow10(logMag_.data(), envelope_.data(), numBins_);
}
```

**Behavioral equivalence**: `batchPow10()` clamps output to `[kMinLogInput, kMaxPow10Output]` = `[1e-10f, 1e6f]`, which is identical to the original `std::max(kMinMagnitude, std::min(envelope_[k], 1e6f))`.

### Constants Retained

`FormantPreserver::kMinMagnitude` is NOT removed because it is still used in `applyFormantPreservation()`:
```cpp
float shiftedEnv = std::max(shiftedEnvelope[k], kMinMagnitude);
```
