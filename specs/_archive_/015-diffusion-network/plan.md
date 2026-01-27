# Implementation Plan: Diffusion Network

**Branch**: `015-diffusion-network` | **Date**: 2025-12-24 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/015-diffusion-network/spec.md`

## Summary

Implement an 8-stage Schroeder-style allpass diffusion network for creating smeared, reverb-like textures. The processor cascades allpass filters with mutually irrational delay times to temporally diffuse transients while preserving the frequency spectrum. Key parameters are Size (delay time scaling), Density (active stage count), Modulation (LFO on delay times), and Stereo Width (L/R decorrelation).

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: DelayLine (Layer 1), LFO (Layer 1), OnePoleSmoother (Layer 1)
**Storage**: N/A (stateless DSP processor, all state in delay lines)
**Testing**: Catch2 (per Constitution Principle XII: Test-First Development)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Single project - VST3 plugin DSP component
**Performance Goals**: < 0.5% CPU per instance at 44.1kHz stereo (Layer 2 budget)
**Constraints**: Real-time safe (noexcept, no allocations in process()), max 50ms delay per stage
**Scale/Scope**: 8 allpass stages × 2 channels = 16 delay lines, ~100KB memory at 192kHz

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| II. Real-Time Safety | ✅ PASS | No allocations in process(), noexcept |
| III. Modern C++ | ✅ PASS | C++20, RAII, value semantics |
| IX. Layered Architecture | ✅ PASS | Layer 2 composes Layer 1 primitives only |
| X. DSP Constraints | ✅ PASS | Allpass preserves spectrum, uses linear interpolation for modulated delays |
| XI. Performance Budget | ✅ PASS | Target < 0.5% CPU (Layer 2 limit) |
| XII. Test-First | ✅ PASS | Tests written before implementation |
| XIV. ODR Prevention | ✅ PASS | No duplicate classes found (see research below) |

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| AllpassStage | `grep -r "class AllpassStage" src/` | No | Create New |
| DiffusionNetwork | `grep -r "class DiffusionNetwork" src/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| (none planned) | - | - | - | - |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DelayLine | dsp/primitives/delay_line.h | 1 | Delay buffer for each allpass stage |
| LFO | dsp/primitives/lfo.h | 1 | Modulation source for delay times |
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Parameter smoothing (size, density, etc.) |
| dbToGain | dsp/core/db_utils.h | 0 | (may use for output gain) |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No diffusion/allpass classes
- [x] `src/dsp/core/` - No diffusion/allpass implementations
- [x] `src/dsp/primitives/biquad.h` - Has allpass mode but for phase-only filtering, not delay-based diffusion
- [x] `src/dsp/primitives/delay_line.h` - Has readAllpass() for fractional delay interpolation, not allpass filtering
- [x] ARCHITECTURE.md - No diffusion network listed

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (AllpassStage, DiffusionNetwork) are unique and not found in codebase. Existing "allpass" references are for different purposes (biquad allpass mode = phase rotation, delay readAllpass = fractional interpolation).

## Project Structure

### Documentation (this feature)

```text
specs/015-diffusion-network/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   └── diffusion_network.h  # API contract
├── checklists/
│   └── requirements.md  # Validation checklist
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
src/dsp/processors/
└── diffusion_network.h      # DiffusionNetwork class (Layer 2)

tests/unit/processors/
└── diffusion_network_test.cpp  # Unit tests
```

**Structure Decision**: Single header implementation in `src/dsp/processors/` following established Layer 2 processor pattern (see multimode_filter.h, saturation_processor.h, midside_processor.h).

## Complexity Tracking

No constitution violations to justify.

---

## Phase 0: Research

### Allpass Diffusion Algorithm

**Decision**: Use Schroeder-style allpass diffuser with the following structure per stage:

```
y[n] = -g * x[n] + x[n-D] + g * y[n-D]
```

Where:
- `g` = allpass coefficient (typically 0.5-0.7, we'll use 0.618 = golden ratio inverse)
- `D` = delay time in samples
- `x[n]` = input
- `y[n]` = output

**Implementation approach using DelayLine**:
```cpp
// Per-sample processing in AllpassStage
float process(float input) noexcept {
    float delayed = delayLine_.readLinear(delaySamples_);
    float output = -g_ * input + delayed + g_ * prevOutput_;
    delayLine_.write(input);
    prevOutput_ = output;
    return output;
}
```

**Rationale**: This standard Schroeder allpass structure is proven in reverb design. Using `readLinear()` allows smooth delay time modulation.

**Alternatives Considered**:
- Nested allpass (Gerzon): More complex, better for true reverbs but overkill for diffusion
- FDN (Feedback Delay Network): Too complex for simple diffusion, better for full reverbs

### Irrational Delay Time Ratios

**Decision**: Use the following delay ratios (multiply by base time from Size parameter):

| Stage | Delay Ratio | Samples @ 44.1kHz, Size=100% |
|-------|-------------|------------------------------|
| 1 | 1.000 | 142 (3.2ms) |
| 2 | 1.127 | 160 (3.6ms) |
| 3 | 1.414 | 201 (4.6ms) |
| 4 | 1.732 | 246 (5.6ms) |
| 5 | 2.236 | 318 (7.2ms) |
| 6 | 2.828 | 402 (9.1ms) |
| 7 | 3.317 | 471 (10.7ms) |
| 8 | 4.123 | 586 (13.3ms) |

Total diffusion spread at size=100%: ~57ms (sum of all delays), within spec target of 50-100ms

**Rationale**: These ratios are based on square roots and golden ratio multiples, which are mutually irrational and avoid integer relationships that cause comb filtering. The values are similar to those used in classic Lexicon reverbs.

**Base time at size=100%**: 3.2ms (142 samples @ 44.1kHz)
**Size parameter scales this**: size=0% → 0ms (bypass), size=50% → ~28ms, size=100% → ~57ms total (meets SC-002 target: 50-100ms)

### Stereo Decorrelation

**Decision**: Use different delay ratios for left and right channels:
- Left: Use ratios above
- Right: Multiply each ratio by 1.127 (√(1.27))

This creates approximately 12-15% delay time difference between channels, sufficient for decorrelation without phasiness.

**Rationale**: Simple multiplicative offset is CPU-efficient and provides consistent decorrelation across all size settings.

### Modulation Implementation

**Decision**:
- One LFO instance shared across all stages (per FR-016: single shared LFO with per-stage phase offsets)
- Each stage reads the LFO with a different phase offset: `stageIndex * (2π/8)` = 45° per stage (per FR-017)
- Modulation depth adds ±2ms maximum to delay time
- Modulation rate: 0.1Hz - 5Hz

**Rationale**: Single LFO is CPU-efficient. Phase offsets (45° per stage) ensure stages don't modulate in sync (which would sound like vibrato rather than diffusion).

### Density Parameter Implementation

**Decision**: Use crossfade between active and bypassed stages:
- density=0%: All stages bypassed (output = input)
- density=25%: Stages 1-2 active, 3-8 bypassed
- density=50%: Stages 1-4 active
- density=75%: Stages 1-6 active
- density=100%: All 8 stages active

Crossfade is applied per-stage using a OnePoleSmoother on the stage enable/bypass coefficient.

**Rationale**: Smooth crossfade prevents clicks when density changes. Bypassing later stages first maintains the core diffusion character at lower densities.
