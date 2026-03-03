# Research: PolyBLEP Oscillator

**Branch**: `015-polyblep-oscillator` | **Date**: 2026-02-03

## Research Tasks and Findings

### R-001: PolyBLEP Correction Application Pattern

**Decision**: Use 2-point `polyBlep(t, dt)` from `core/polyblep.h` for all discontinuity corrections (sawtooth wrap, square/pulse edges). Do NOT use 4-point `polyBlep4` in the initial implementation.

**Rationale**: The 2-point variant is the standard choice for real-time oscillators per the Valimaki & Pekonen (2012) research. It provides sufficient alias suppression (40+ dB below fundamental at reasonable frequencies) with minimal computational cost. The 4-point variant can be offered as a future quality option.

**Alternatives considered**:
- `polyBlep4(t, dt)` -- Higher quality but wider correction region (2*dt each side). Deferred to future enhancement.
- `polyBlamp(t, dt)` -- For direct triangle generation without leaky integrator. Not chosen because the leaky integrator approach is more standard and produces better results at variable frequencies.
- minBLEP (precomputed table) -- Higher quality, but Phase 4 in the roadmap. Not available yet.

---

### R-002: Leaky Integrator for Triangle Waveform

**Decision**: Use leaky integrator with coefficient `leak = 1.0 - (4.0 * frequency / sampleRate)` applied as: `integrator = leak * integrator + (4.0 * dt) * squareSample + kAntiDenormal`.

**Rationale**: This is the standard approach for PolyBLEP triangle generation. The frequency-dependent leak coefficient maintains consistent amplitude across the frequency range. The factor of 4 normalizes the triangle amplitude to approximately [-1, 1]. The anti-denormal constant (1e-18f) prevents denormal accumulation in the feedback loop per FR-035.

**Key implementation details**:
1. The leaky integrator is applied to the PolyBLEP-corrected square wave output (NOT the raw square)
2. The leak coefficient must be recalculated when frequency changes
3. At very high frequencies (near Nyquist), the leak coefficient approaches 0, which is correct behavior (triangle degrades gracefully)
4. At very low frequencies, the leak coefficient approaches 1.0, providing minimal DC rejection (correct -- slow triangles should be accurate)
5. The scaling factor `4.0 * dt` (where dt = frequency/sampleRate) normalizes the integrated square to unit amplitude

**Amplitude normalization analysis**:
- A square wave of amplitude 1.0 integrated over one period produces a triangle with peak = 0.25 * period
- With `dt = freq/sr`, one period = `1/dt` samples, so raw integration peak = `0.25/dt`
- Multiplying by `4.0 * dt` normalizes to approximately 1.0
- The leak factor slightly reduces amplitude at higher frequencies; within +/-20% per SC-013

**Alternatives considered**:
- Direct PolyBLAMP triangle: Apply `polyBlamp(t, dt)` corrections directly to a naive triangle. This avoids the integrator state but requires more complex correction logic at both peaks. Rejected because the leaky integrator approach is more established and the spec explicitly requires it (FR-015).
- Differentiated parabolic wave (DPW): Generates a parabola and differentiates. Rejected because it requires a different anti-aliasing paradigm and is not specified.

---

### R-003: Frequency Clamping for PolyBLEP Precondition

**Decision**: Clamp effective frequency (base + FM) to `[0, sampleRate/2 - epsilon)` where epsilon ensures `dt < 0.5`. Use `std::min` and `std::max` for branchless-friendly clamping.

**Rationale**: The PolyBLEP functions require `dt < 0.5` as a precondition (documented in `core/polyblep.h`). When `dt >= 0.5`, the correction regions overlap and produce undefined behavior. Clamping is the correct approach per FR-006 (silent clamping, no error).

**Implementation**: The clamping happens in two places:
1. In `setFrequency(float hz)` -- clamps base frequency
2. In `process()` -- clamps effective frequency after adding FM offset

**Alternatives considered**:
- Returning silence above Nyquist: Rejected because it creates audible artifacts when frequency approaches Nyquist during FM synthesis.
- Assertion-only: Rejected because runtime violations would cause undefined behavior in release builds.

---

### R-004: Phase Modulation Implementation

**Decision**: Convert radians to normalized phase by dividing by `2*pi` (i.e., `kTwoPi`), then add to the current phase position for waveform lookup only. Do NOT modify the underlying `PhaseAccumulator` phase.

**Rationale**: PM synthesis (Yamaha-style) adds a phase offset for waveform lookup while keeping the underlying phase accumulation unmodified. This prevents PM from accumulating over time (per FR-020: "does NOT accumulate between samples"). The radians-to-normalized conversion is `offset = radians / kTwoPi`.

**Implementation pattern**:
```
effectivePhase = wrapPhase(phaseAcc_.phase + pmOffset_normalized)
// Use effectivePhase for waveform computation
// phaseAcc_.phase is NOT modified by PM
pmOffset_ = 0.0f;  // Reset after use
```

---

### R-005: Frequency Modulation Implementation

**Decision**: Add FM offset in Hz to the base frequency before computing the phase increment for the current sample. Clamp the result to `[0, sampleRate/2)`. Reset FM offset after each sample.

**Rationale**: Linear FM adds a frequency offset in Hz, which is conceptually simple but must be clamped to prevent negative frequencies or exceeding Nyquist. Per FR-021, the modulation does not accumulate.

**Implementation pattern**:
```
effectiveFreq = clamp(baseFrequency_ + fmOffset_, 0.0f, sampleRate_ * 0.5f - epsilon)
dt = effectiveFreq / sampleRate_
// Use dt for phase advance and PolyBLEP corrections this sample
fmOffset_ = 0.0f;  // Reset after use
```

---

### R-006: Waveform Switching and Integrator State

**Decision**: When switching from Triangle to any other waveform, clear the leaky integrator state. When switching to Triangle from any other waveform, also clear the integrator state. Maintain phase continuity across all waveform switches.

**Rationale**: Per FR-007, stale integrator state from Triangle would contaminate other waveforms. Clearing the integrator when entering or leaving Triangle ensures clean transitions. Phase continuity (FR-007, User Story 7) means the PhaseAccumulator is never reset on waveform change.

**Alternatives considered**:
- Crossfade between old and new waveform: More complex, provides smoother transition. Rejected for initial implementation per spec priority (P3 for waveform switching). Can be added later.

---

### R-007: Output Sanitization Strategy

**Decision**: Use branchless sanitization at the end of `process()` based on bit manipulation for NaN detection and range clamping.

**Rationale**: Per FR-036, the output must be sanitized branchlessly. The approach:
1. Check for NaN using `detail::isNaN()` from `db_utils.h` (bit manipulation, works with -ffast-math)
2. If NaN, output 0.0f
3. Clamp to [-2.0f, 2.0f] range
4. Use conditional assignment pattern that compilers optimize to CMOV instructions

**Implementation sketch**:
```cpp
float sanitize(float x) noexcept {
    // Branchless NaN -> 0.0
    const auto bits = std::bit_cast<uint32_t>(x);
    const bool nan = ((bits & 0x7F800000u) == 0x7F800000u) && ((bits & 0x007FFFFFu) != 0);
    x = nan ? 0.0f : x;
    // Clamp to [-2, 2]
    x = x < -2.0f ? -2.0f : x;
    x = x > 2.0f ? 2.0f : x;
    return x;
}
```

Note: We include `<bit>` and `<cstdint>` for `std::bit_cast`. This compiles cleanly under MSVC C++20, Clang, and GCC per FR-026.

---

### R-008: SIMD-Friendly Design Without SIMD Intrinsics

**Decision**: Use function pointer dispatch for waveform selection (set once per `setWaveform()` call), keeping the per-sample `process()` loop branchless. Align member data to cache-line-friendly layout. Use `alignas(32)` on buffer parameters where applicable.

**Rationale**: Per FR-038-FR-043, the implementation is scalar-only but must be designed for future SIMD. Key design choices:
1. **No branching in inner loop**: The waveform `switch` happens via a function pointer set in `setWaveform()`, not per-sample.
2. **Contiguous state**: All oscillator state members are packed in a cache-line-friendly order (hot path data first).
3. **SoA readiness**: The `processBlock()` loop processes samples sequentially with no inter-sample dependencies except phase accumulation and integrator state (inherently sequential for scalar, but parallelizable for multi-oscillator SIMD).

**Alternatives considered**:
- Template-based waveform dispatch (compile-time polymorphism): Would eliminate all dispatch overhead but requires the waveform type to be known at compile time. Rejected because users need runtime waveform selection.
- Virtual function dispatch: Rejected due to virtual call overhead in tight loop per Constitution Principle IV.

---

### R-009: Existing Component Reuse Verification

**Decision**: Reuse all identified Layer 0 components. No new Layer 0 utilities needed.

**Components verified**:

| Component | Header | Verified API | Status |
|-----------|--------|-------------|--------|
| `polyBlep(t, dt)` | `core/polyblep.h` | `[[nodiscard]] constexpr float polyBlep(float t, float dt) noexcept` | Reuse |
| `PhaseAccumulator` | `core/phase_utils.h` | struct with `phase`, `increment`, `advance()`, `reset()`, `setFrequency()` | Reuse |
| `calculatePhaseIncrement()` | `core/phase_utils.h` | `[[nodiscard]] constexpr double calculatePhaseIncrement(float, float) noexcept` | Reuse |
| `wrapPhase()` | `core/phase_utils.h` | `[[nodiscard]] constexpr double wrapPhase(double) noexcept` | Reuse |
| `kPi`, `kTwoPi` | `core/math_constants.h` | `inline constexpr float kPi`, `inline constexpr float kTwoPi` | Reuse |
| `detail::isNaN()` | `core/db_utils.h` | `constexpr bool isNaN(float x) noexcept` | Reuse for sanitization |
| `detail::isInf()` | `core/db_utils.h` | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Reuse for sanitization |

**ODR Check Results**:
- `OscWaveform` -- Not found in codebase. Safe to create.
- `PolyBlepOscillator` -- Not found in codebase. Safe to create.
- `Waveform` enum in `lfo.h` -- Different name, no conflict.
- `FMWaveform` enum in `audio_rate_filter_fm.h` -- Different name, no conflict.

---

### R-010: Test Infrastructure for Alias Measurement

**Decision**: Reuse the existing `SpectralAnalysis` test helper at `tests/test_helpers/spectral_analysis.h` for FFT-based alias measurement (SC-001 through SC-003).

**Rationale**: The spectral analysis test helper already provides:
- `frequencyToBin()` -- Convert frequency to FFT bin index
- `calculateAliasedFrequency()` -- Compute where aliased harmonics land
- `willAlias()` -- Check if a harmonic will alias
- `AliasingTestConfig` -- Configuration struct for test parameters
- `getHarmonicBins()` / `getAliasedBins()` -- Bin collection utilities

These were built for ADAA specs and are directly applicable to PolyBLEP alias measurement. The oscillator tests will generate output buffers, apply windowed FFT, and measure alias component magnitudes relative to the fundamental.

**Test pattern**: For each waveform and frequency:
1. Generate N samples (4096+) into a buffer
2. Apply Hann window
3. FFT the buffer
4. Measure fundamental magnitude
5. For each harmonic that should alias (frequency > Nyquist), find the aliased bin
6. Verify aliased bin magnitude is >= 40 dB below fundamental

---

### R-011: Performance Considerations

**Decision**: Target 50 cycles/sample for PolyBLEP waveforms, ~15-20 for Sine. Measure with RDTSC in a separate performance test.

**Key optimizations**:
1. Function pointer dispatch for waveform (avoids switch in hot path)
2. Pre-compute `dt` (phase increment as float) once per frequency change, not per sample
3. Use `std::sin()` for sine (compiler optimizes to hardware instruction on modern x86)
4. Minimize memory access in `process()` by keeping hot-path state in first cache line
5. The `processBlock()` simply loops `process()` -- auto-vectorization may help for sine

**Performance measurement**: Add a tagged test case `[!benchmark]` that measures cycles/sample using RDTSC or `std::chrono::high_resolution_clock` over 10,000 samples. This is informational, not a hard pass/fail gate.

---

### R-012: PhaseAccumulator.advance() Return Value Usage

**Decision**: Use the `bool advance()` return value from `PhaseAccumulator` directly for phase wrap detection, storing it in `phaseWrapped_` member for later query via `phaseWrapped()`.

**Rationale**: `PhaseAccumulator::advance()` already returns `true` when the phase wraps (crosses 1.0). This is exactly what `phaseWrapped()` needs to expose. No need for `detectPhaseWrap()` since `advance()` handles it.

**Important note**: The phase value AFTER `advance()` and wrap is the value used for PolyBLEP correction (`t` parameter). The `dt` parameter is the phase increment (frequency / sampleRate). The PolyBLEP functions check if `t < dt` (just past wrap) or `t > 1-dt` (approaching wrap) to determine if correction is needed.
