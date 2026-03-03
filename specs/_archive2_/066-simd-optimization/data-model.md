# Data Model: SIMD-Accelerated Math for KrateDSP Spectral Pipeline

**Date**: 2026-02-18 | **Spec**: 066-simd-optimization

## Entities

### Constants (New - `spectral_simd.h`)

| Constant | Type | Value | Namespace | Purpose |
|----------|------|-------|-----------|---------|
| `kMinLogInput` | `inline constexpr float` | `1e-10f` | `Krate::DSP` | Minimum input for log operations; clamps zero/negative to avoid NaN/inf. Single source of truth replacing `FormantPreserver::kMinMagnitude` for log/pow paths. |
| `kMaxPow10Output` | `inline constexpr float` | `1e6f` | `Krate::DSP` | Maximum output for pow10 operations; prevents overflow to infinity. Matches existing `FormantPreserver::reconstructEnvelope()` clamp. |

### Free Functions (New - `spectral_simd.h/.cpp`)

| Function | Layer | Input | Output | Constraints |
|----------|-------|-------|--------|-------------|
| `batchLog10(const float* input, float* output, std::size_t count)` | 0 | Array of floats | Array of `log10(max(x, 1e-10f))` | noexcept, zero allocs, SIMD+scalar tail |
| `batchPow10(const float* input, float* output, std::size_t count)` | 0 | Array of float exponents | Array of `clamp(10^x, 1e-10f, 1e6f)` | noexcept, zero allocs, SIMD+scalar tail |
| `batchWrapPhase(const float* input, float* output, std::size_t count)` | 0 | Array of phase values (radians) | Array of phases wrapped to [-pi, +pi] | noexcept, zero allocs, branchless |
| `batchWrapPhase(float* data, std::size_t count)` | 0 | Array of phase values (radians) | Same array, wrapped in-place | noexcept, zero allocs, branchless |

### Modified Entities

| Entity | Location | Layer | Modification |
|--------|----------|-------|-------------|
| `FormantPreserver::extractEnvelope()` | `processors/formant_preserver.h` | 2 | Scalar `std::log10` loop replaced with `batchLog10()` call. Pre-clamp removed. |
| `FormantPreserver::reconstructEnvelope()` | `processors/formant_preserver.h` | 2 | Scalar `std::pow(10.0f, x)` loop replaced with staging copy + `batchPow10()` call. Post-clamp removed. |

### Unchanged Entities (Preserved)

| Entity | Location | Layer | Note |
|--------|----------|-------|------|
| `FormantPreserver::kMinMagnitude` | `processors/formant_preserver.h` | 2 | Retained for `applyFormantPreservation()` use |
| `wrapPhase()` | `primitives/spectral_utils.h` | 1 | Unchanged scalar function for single-value use |
| `wrapPhaseFast()` | `primitives/spectral_utils.h` | 1 | Unchanged scalar function for single-value use |
| `computePolarBulk()` | `core/spectral_simd.h/.cpp` | 0 | Unchanged |
| `reconstructCartesianBulk()` | `core/spectral_simd.h/.cpp` | 0 | Unchanged |
| `computePowerSpectrumPffft()` | `core/spectral_simd.h/.cpp` | 0 | Unchanged |

## Validation Rules

### `batchLog10`
- `count == 0`: Return immediately, no memory access
- Non-positive inputs: Clamped to `kMinLogInput` (1e-10f) before log10
- Output: Always finite (never NaN or -inf for valid inputs)
- Non-SIMD-width counts: Scalar tail handles remaining elements

### `batchPow10`
- `count == 0`: Return immediately, no memory access
- Output overflow (x > ~38.5): Clamped to `kMaxPow10Output` (1e6f)
- Output underflow (very negative x): Clamped to `kMinLogInput` (1e-10f)
- Output: Always in `[1e-10f, 1e6f]`, never infinity

### `batchWrapPhase`
- `count == 0`: Return immediately, no memory access
- Output: Always in `[-pi, +pi]` for any finite float input
- At exact +/-pi boundary: May return either +pi or -pi (both valid)
- Large inputs (e.g., 1e6 radians): Handled correctly in O(1) per element

## State Transitions

### FormantPreserver Pipeline (unchanged flow, modified implementation)

```
extractEnvelope(magnitudes):
  Step 1: magnitudes[] --batchLog10()--> logMag_[0..numBins_-1]
  Step 1b: Mirror logMag_ to negative frequencies
  Step 2: logMag_ --IFFT--> cepstrum_
  Step 3: cepstrum_ * lifterWindow_ --> cepstrum_ (liftered)
  Step 4: cepstrum_ --FFT--> complexBuf_
          complexBuf_[k].real --copy--> logMag_[0..numBins_-1]
          logMag_ --batchPow10()--> envelope_
  Output: envelope_ contains formant envelope
```

## Relationships

```
spectral_simd.h (Layer 0)
  |-- defines: kMinLogInput, kMaxPow10Output
  |-- declares: batchLog10(), batchPow10(), batchWrapPhase()
  |
  +-- spectral_simd.cpp (Layer 0, Highway self-inclusion)
       |-- implements: BatchLog10Impl, BatchPow10Impl, BatchWrapPhaseImpl
       |-- dispatches: HWY_DYNAMIC_DISPATCH to best ISA at runtime
       |
       +-- formant_preserver.h (Layer 2)
            |-- calls: batchLog10() in extractEnvelope()
            |-- calls: batchPow10() in reconstructEnvelope()
            |-- dependency direction: Layer 2 -> Layer 0 (valid)
```
