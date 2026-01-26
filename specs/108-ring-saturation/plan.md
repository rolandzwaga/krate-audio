# Implementation Plan: Ring Saturation Primitive

**Branch**: `108-ring-saturation` | **Date**: 2026-01-26 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/108-ring-saturation/spec.md`

## Summary

Implement a Layer 1 DSP primitive that creates self-modulation distortion using the formula:
`output = input + (input * saturate(input * drive) - input) * depth`

This produces metallic, bell-like character by generating signal-coherent inharmonic sidebands (unlike traditional ring mod with external carrier). The implementation composes existing `Waveshaper` for saturation curves and `DCBlocker` for DC offset removal, with a crossfade mechanism for click-free curve switching.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- Waveshaper (Layer 1, `primitives/waveshaper.h`)
- DCBlocker (Layer 1, `primitives/dc_blocker.h`)
- OnePoleSmoother (Layer 1, `primitives/smoother.h`) - for crossfade
- sigmoid.h (Layer 0, for soft limiting)
**Storage**: N/A (stateless except for DC blocker state and crossfade position)
**Testing**: Catch2 with spectral_analysis.h and signal_metrics.h test infrastructure
**Target Platform**: Windows, macOS, Linux (cross-platform per constitution)
**Project Type**: KrateDSP library primitive
**Performance Goals**: < 0.1% CPU per instance (Layer 1 budget), < 1us single sample at 44.1kHz
**Constraints**: Real-time safe (no allocations in process), noexcept processing
**Scale/Scope**: Single-channel mono primitive, composable for stereo use

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle II - Real-Time Audio Thread Safety:**
- [x] No allocations in process() or processBlock()
- [x] No locks, mutexes, or blocking primitives
- [x] No exceptions (noexcept on all processing methods)
- [x] Pre-allocate all state in prepare()

**Principle IX - Layered DSP Architecture:**
- [x] Layer 1 primitive depends only on Layer 0 and other Layer 1 primitives
- [x] Will be placed in `dsp/include/krate/dsp/primitives/ring_saturation.h`

**Principle X - DSP Processing Constraints:**
- [x] DC blocking after asymmetric saturation (DCBlocker at 10Hz)
- [x] No internal oversampling (user wraps with Oversampler externally)
- [x] Soft limiting for output bounds (tanh-based approach)

**Principle XIV - ODR Prevention:**
- [x] Class name `RingSaturation` searched - no conflicts found
- [x] Using existing Waveshaper, DCBlocker - no duplication

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, dsp-architecture) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: RingSaturation

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| RingSaturation | `grep -r "class RingSaturation" dsp/ plugins/` | No | Create New |
| RingSaturation | `grep -r "RingSat" dsp/ plugins/` | No | Create New |
| CrossfadeState | Internal struct | No | Create as private struct within RingSaturation |

**Utility Functions to be created**: None - all utilities available in existing Layer 0/1

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Waveshaper | `dsp/include/krate/dsp/primitives/waveshaper.h` | 1 | Saturation curve application (FR-005, FR-007) |
| WaveshapeType | `dsp/include/krate/dsp/primitives/waveshaper.h` | 1 | Enum for curve selection |
| DCBlocker | `dsp/include/krate/dsp/primitives/dc_blocker.h` | 1 | DC offset removal at 10Hz (FR-012, FR-013) |
| LinearRamp | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Crossfade between curves (FR-006) |
| Sigmoid::tanh | `dsp/include/krate/dsp/core/sigmoid.h` | 0 | Soft limiting for output bounds (SC-005) |
| detail::flushDenormal | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Denormal prevention |
| detail::isNaN/isInf | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Input validation |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `specs/_architecture_/layer-1-primitives.md` - Component inventory
- [x] `dsp/include/krate/dsp/processors/saturation_processor.h` - Reference pattern

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: RingSaturation is a unique class name not found in codebase. The implementation composes existing primitives (Waveshaper, DCBlocker, LinearRamp) rather than duplicating functionality.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Waveshaper | setType | `void setType(WaveshapeType type) noexcept` | Yes |
| Waveshaper | setDrive | `void setDrive(float drive) noexcept` | Yes |
| Waveshaper | getType | `[[nodiscard]] WaveshapeType getType() const noexcept` | Yes |
| Waveshaper | getDrive | `[[nodiscard]] float getDrive() const noexcept` | Yes |
| Waveshaper | process | `[[nodiscard]] float process(float x) const noexcept` | Yes |
| Waveshaper | processBlock | `void processBlock(float* buffer, size_t numSamples) noexcept` | Yes |
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker | reset | `void reset() noexcept` | Yes |
| DCBlocker | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| DCBlocker | processBlock | `void processBlock(float* buffer, size_t numSamples) noexcept` | Yes |
| LinearRamp | configure | `void configure(float rampTimeMs, float sampleRate) noexcept` | Yes |
| LinearRamp | setTarget | `void setTarget(float target) noexcept` | Yes |
| LinearRamp | process | `[[nodiscard]] float process() noexcept` | Yes |
| LinearRamp | isComplete | `[[nodiscard]] bool isComplete() const noexcept` | Yes |
| LinearRamp | snapTo | `void snapTo(float value) noexcept` | Yes |
| LinearRamp | reset | `void reset() noexcept` | Yes |
| Sigmoid::tanh | tanh | `[[nodiscard]] constexpr float tanh(float x) noexcept` | Yes |
| detail::flushDenormal | flushDenormal | `[[nodiscard]] inline float flushDenormal(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/waveshaper.h` - Waveshaper class, WaveshapeType enum
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - LinearRamp class
- [x] `dsp/include/krate/dsp/core/sigmoid.h` - Sigmoid::tanh function
- [x] `dsp/include/krate/dsp/core/db_utils.h` - flushDenormal, isNaN, isInf

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Waveshaper | Drive=0 returns 0 regardless of input | Handle edge case where `input * saturate(0) = 0` |
| DCBlocker | Returns input unchanged if not prepared | Must call prepare() before processing |
| LinearRamp | setTarget recalculates increment from distance | Set target when curve changes, not every sample |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

**N/A** - This is a Layer 1 primitive. No new utilities to extract.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 1 - DSP Primitives

**Related features at same layer** (from specs):
- ChaosWaveshaper (implemented) - Different approach to time-varying distortion
- StochasticShaper (implemented) - Random modulation of waveshaping
- Wavefolder (implemented) - Different harmonic generation mechanism

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Crossfade pattern (LinearRamp for curve switching) | MEDIUM | Other primitives needing click-free type changes | Keep local - standard pattern already in LinearRamp |
| Multi-stage iteration pattern | LOW | Unlikely - specific to ring saturation formula | Keep local |
| Soft limiting approach | LOW | Other primitives already use tanh directly | Keep local |

**Decision**: No components to extract - all patterns either use existing primitives or are specific to ring saturation.

## Project Structure

### Documentation (this feature)

```text
specs/108-ring-saturation/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── contracts/           # Phase 1 output (API contract)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── primitives/
│       └── ring_saturation.h    # NEW: RingSaturation primitive
└── tests/
    └── unit/
        └── primitives/
            └── ring_saturation_test.cpp    # NEW: Unit tests
```

**Structure Decision**: Single header-only primitive in `primitives/` directory, tests in `tests/unit/primitives/`.

## Complexity Tracking

No constitution violations. Design follows established Layer 1 patterns.

---

# Phase 0: Research

## Research Tasks

### RT-001: Crossfade Implementation for Curve Switching (FR-006)

**Question**: How to implement click-free 10ms crossfade when setSaturationCurve() is called during processing?

**Research**:
The spec requires crossfading over a 10ms window. Looking at existing patterns:

1. **LinearRamp from smoother.h**: Provides constant-rate ramping with configurable time. Perfect for crossfade position.

2. **Dual Waveshaper approach**: During crossfade, maintain two Waveshaper instances (old and new curve) and blend outputs:
   ```cpp
   float crossfadePosition = crossfadeRamp_.process();  // 0.0 to 1.0
   float oldOutput = oldShaper_.process(x);
   float newOutput = newShaper_.process(x);
   float blended = std::lerp(oldOutput, newOutput, crossfadePosition);
   ```

3. **Memory consideration**: Waveshaper is stateless and trivially copyable (24 bytes). Keeping two instances is negligible.

**Decision**: Use LinearRamp for crossfade position, maintain two Waveshaper instances during active crossfade, complete crossfade when ramp reaches 1.0.

### RT-002: Soft Limiting for Output Bounds (SC-005)

**Question**: How to implement soft limiting that approaches +/-2.0 asymptotically?

**Research**:
The spec says "soft limit approaching +/-2.0 asymptotically". Options:

1. **Scaled tanh**: `2.0 * tanh(x)` - approaches +/-2.0 asymptotically, smooth
2. **Custom function**: `2.0 * x / sqrt(x^2 + 1)` - similar shape
3. **No limiting**: Let multi-stage self-modulation naturally saturate

Analysis of the formula with stages=4 and drive=10:
- Single stage: `input + (input * saturate(input * drive) - input) * depth`
- With saturate being tanh: maximum output ~2.0 when input=1.0, depth=1.0
- Multi-stage compounds but tanh bounds each stage

**Decision**: Apply `2.0 * Sigmoid::tanh(output / 2.0)` as final soft limiter after all stages. This maps any value to (-2, +2) asymptotically while preserving signal character near unity.

### RT-003: Shannon Spectral Entropy for Multi-Stage Verification (SC-003)

**Question**: How to measure Shannon spectral entropy for verifying multi-stage complexity increase?

**Research**:
The signal_metrics.h doesn't have spectral entropy implemented, but the formula is standard:

```cpp
// H = -sum(p_i * log2(p_i)) where p_i is normalized magnitude of each bin
// 1. Compute FFT
// 2. Get magnitude spectrum: mag[i] = sqrt(real[i]^2 + imag[i]^2)
// 3. Normalize to probability distribution: p[i] = mag[i] / sum(mag)
// 4. Compute entropy: H = -sum(p[i] * log2(p[i])) for p[i] > 0
```

**Decision**: Implement `calculateSpectralEntropy()` as a test helper function in the test file. This is test infrastructure, not production code.

### RT-004: Drive=0 Edge Case Behavior

**Question**: What happens when drive=0? Per spec edge case section.

**Research**:
From spec: "When drive is 0, the saturator produces 0 output, so the ring modulation term `(input * 0 - input)` equals `-input`, which when scaled by depth and added to input produces a reduced signal: `output = input * (1 - depth)`."

From Waveshaper: "When drive is 0.0, process() returns 0.0 regardless of input."

So: `saturate(input * 0) = 0`, therefore:
- Ring mod term = `input * 0 - input = -input`
- Output = `input + (-input) * depth = input * (1 - depth)`

**Decision**: This is naturally handled by the formula and Waveshaper behavior. No special case needed.

---

# Phase 1: Design

## Data Model

See [data-model.md](./data-model.md) for entity definitions.

## API Contracts

See [contracts/ring_saturation.h](./contracts/ring_saturation.h) for the full API specification.

## Implementation Notes

### Core Algorithm (FR-001)

The Waveshaper applies drive internally: `shape(drive * x)`. Configure Waveshaper with our drive parameter and call `process(input)`:

```cpp
// Single stage processing
float processStage(float input) const noexcept {
    // Waveshaper already has drive configured, applies: shape(drive * input)
    float saturated = waveshaper_.process(input);

    // Ring modulation term: input * saturated - input
    float ringModTerm = input * saturated - input;

    // Output: input + ringModTerm * depth
    return input + ringModTerm * depth_;
}
```

This implements: `output = input + (input * saturate(input * drive) - input) * depth`

### Crossfade Implementation (FR-006)

During curve change:
1. Store current Waveshaper config as "old"
2. Configure new Waveshaper with new curve
3. Start crossfade ramp (10ms)
4. During crossfade: blend old and new outputs
5. When complete: discard old shaper state

**Rapid curve changes**: If setSaturationCurve() is called while a crossfade is already in progress, the current crossfade position becomes the new "old" state and the ramp restarts from that position toward the new target. This ensures smooth transitions even with rapid parameter changes.

### Multi-Stage Processing (FR-002)

For stages > 1, iterate the formula:
```cpp
float signal = input;
for (int s = 0; s < stages_; ++s) {
    signal = processStage(signal);
}
return signal;
```

### Output Soft Limiting (SC-005)

After all stages, apply soft limit:
```cpp
// Soft limit approaching +/-2.0 asymptotically
signal = 2.0f * Sigmoid::tanh(signal * 0.5f);
```

---

# Quickstart

See [quickstart.md](./quickstart.md) for usage examples.
