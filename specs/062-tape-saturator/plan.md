# Implementation Plan: TapeSaturator Processor

**Branch**: `062-tape-saturator` | **Date**: 2026-01-14 | **Spec**: [specs/062-tape-saturator/spec.md](spec.md)
**Input**: Feature specification from `/specs/062-tape-saturator/spec.md`

## Summary

Implement a Layer 2 tape saturation processor with two models: Simple (tanh + pre/de-emphasis) and Hysteresis (Jiles-Atherton). The Hysteresis model provides four numerical solvers (RK2, RK4, NR4, NR8) for different quality/CPU tradeoffs. Expert mode exposes J-A parameters for advanced users. Sample rate independence via T-scaling and click-free model switching via 10ms crossfade.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Layer 0/1 DSP components (Biquad, DCBlocker, OnePoleSmoother, Sigmoid::tanh)
**Storage**: N/A (stateless except per-sample hysteresis state)
**Testing**: Catch2 (unit tests in `dsp/tests/unit/processors/`)
**Target Platform**: Cross-platform (Windows, macOS, Linux)
**Project Type**: Audio DSP library (monorepo structure)
**Performance Goals**: Simple < 0.3% CPU, Hysteresis/RK4 < 1.5% CPU @ 44.1kHz/2.5GHz baseline
**Constraints**: Real-time safe (no allocations in process), 10ms model crossfade
**Scale/Scope**: Layer 2 processor for Layer 4 TapeDelay integration

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] No memory allocation in process() path
- [x] No exceptions in audio path (noexcept)
- [x] No locks/mutexes in audio path
- [x] No I/O operations in audio path
- [x] Denormal handling via flushDenormal()

**Required Check - Principle III (Modern C++):**
- [x] C++20 features (constexpr, [[nodiscard]], etc.)
- [x] RAII for all resources
- [x] Value semantics where appropriate

**Required Check - Principle IX (Layer Architecture):**
- [x] Layer 2 depends only on Layer 0 and Layer 1
- [x] No circular dependencies

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: TapeSaturator, TapeModel, HysteresisSolver

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| TapeSaturator | `grep -r "class TapeSaturator" dsp/ plugins/` | No | Create New |
| TapeModel | `grep -r "enum.*TapeModel" dsp/ plugins/` | No | Create New |
| HysteresisSolver | `grep -r "enum.*HysteresisSolver" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: langevin(), hysteresisRK2(), hysteresisRK4(), hysteresisNR()

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| langevin | `grep -r "langevin" dsp/ plugins/` | No | — | Create New (private) |
| hysteresis* | `grep -r "hysteresis" dsp/ plugins/` | No | — | Create New (private) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Sigmoid::tanh | dsp/include/krate/dsp/core/sigmoid.h | 0 | Simple model saturation |
| FastMath::fastTanh | dsp/include/krate/dsp/core/fast_math.h | 0 | Langevin function |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | Convert drive dB to linear gain |
| flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Prevent denormal CPU spikes |
| equalPowerGains | dsp/include/krate/dsp/core/crossfade_utils.h | 0 | Model crossfade |
| crossfadeIncrement | dsp/include/krate/dsp/core/crossfade_utils.h | 0 | Calculate crossfade rate |
| Biquad | dsp/include/krate/dsp/primitives/biquad.h | 1 | Pre/de-emphasis filters |
| DCBlocker | dsp/include/krate/dsp/primitives/dc_blocker.h | 1 | DC offset removal |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no tape_saturator.h)
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (TapeSaturator, TapeModel, HysteresisSolver) are unique. The langevin() and hysteresis*() functions are private implementation details within the TapeSaturator class, not exposed in any namespace. No conflicts found in codebase search.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Sigmoid::tanh | tanh | `[[nodiscard]] constexpr float tanh(float x) noexcept` | Yes |
| FastMath::fastTanh | fastTanh | `[[nodiscard]] constexpr float fastTanh(float x) noexcept` | Yes |
| dbToGain | dbToGain | `[[nodiscard]] constexpr float dbToGain(float dB) noexcept` | Yes |
| flushDenormal | flushDenormal | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |
| equalPowerGains | equalPowerGains | `inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept` | Yes |
| crossfadeIncrement | crossfadeIncrement | `[[nodiscard]] inline float crossfadeIncrement(float durationMs, double sampleRate) noexcept` | Yes |
| Biquad | configure | `void configure(FilterType type, float frequency, float Q, float gainDb, float sampleRate) noexcept` | Yes |
| Biquad | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/sigmoid.h` - Sigmoid namespace
- [x] `dsp/include/krate/dsp/core/fast_math.h` - FastMath namespace
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dbToGain, flushDenormal
- [x] `dsp/include/krate/dsp/core/crossfade_utils.h` - equalPowerGains, crossfadeIncrement
- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad class
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| Biquad | Uses `configure(FilterType, freq, Q, gainDb, sr)` order | `biquad.configure(FilterType::HighShelf, freq, Q, gainDb, sr)` |
| DCBlocker | Uses `prepare(sampleRate, cutoffHz)` with double sampleRate | `dcBlocker.prepare(sampleRate, 10.0f)` |
| crossfadeIncrement | Returns per-sample increment, not total samples | `position += increment` per sample |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| — | — | — | — |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| langevin() | J-A specific, only used in TapeSaturator |
| hysteresisRK2/RK4/NR() | J-A specific, only used in TapeSaturator |

**Decision**: All hysteresis-related functions remain as private methods of TapeSaturator. No Layer 0 extraction needed.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 (Processors)

**Related features at same layer**:
- TubeStage (060) - already implemented
- DiodeClipper (061) - already implemented
- WavefolderProcessor (062) - already implemented

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| TapeSaturator | MEDIUM | TapeDelay (Layer 4), BBDDelay (Layer 4) | Keep as standalone processor |
| HysteresisSolver enum | LOW | Only TapeSaturator | Keep local |

**Decision**: TapeSaturator is a complete processor designed for composition by Layer 3/4 components. No shared base class with other saturation processors (TubeStage, DiodeClipper) as their internal architectures differ significantly.

## Project Structure

### Documentation (this feature)

```text
specs/062-tape-saturator/
├── spec.md              # Feature specification (input)
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/
│   └── tape_saturator_api.h  # API contract
├── checklists/
│   └── requirements.md  # FR/NFR checklist
└── tasks.md             # Phase 2 output (to be created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/processors/
│   └── tape_saturator.h    # New: Layer 2 processor header
└── tests/unit/processors/
    └── tape_saturator_test.cpp  # New: Unit tests

plugins/iterum/src/
└── (no changes - integration happens at Layer 4)
```

**Structure Decision**: Single header implementation (header-only) following the pattern of other Layer 2 processors (DiodeClipper, TubeStage, WavefolderProcessor).

## Phase 0: Research Completed

See [research.md](research.md) for complete findings:
- Jiles-Atherton model equations and parameters
- Solver selection rationale (RK2/RK4/NR4/NR8)
- Pre/de-emphasis filter design (+9dB @ 3kHz)
- T-scaling for sample rate independence
- Model crossfade strategy (10ms equal-power)

## Phase 1: Design Completed

See [data-model.md](data-model.md) for:
- Entity diagram with all components
- Parameter ranges and defaults
- J-A parameter definitions
- Signal flow diagrams
- State transitions

See [contracts/tape_saturator_api.h](contracts/tape_saturator_api.h) for:
- Complete public API
- Method signatures with documentation
- Constants

See [quickstart.md](quickstart.md) for:
- Usage examples
- Integration patterns
- CPU budget reference

## Phase 2: Tasks (To Be Generated)

The `/speckit.tasks` command will generate:
1. Task breakdown following TDD workflow
2. Each task includes: failing test -> implementation -> warnings fix -> verify -> commit
3. Benchmark tasks for SC-005/SC-006 compliance
4. Integration verification with Layer 4 TapeDelay

## Complexity Tracking

### Constitution Exception: Oversampling (Principle X)

**Issue**: Constitution Principle X states "Oversampling (min 2x) for saturation/distortion/waveshaping" but this spec explicitly excludes internal oversampling (Out of Scope section).

**Justification**: TapeSaturator is a Layer 2 processor designed as a building block for composition in oversampled contexts at Layer 3/4. The constitution principle applies to complete effects (Layer 4), not individual primitives. When TapeSaturator is used in a TapeDelay effect (Layer 4), the delay system wraps it with `Oversampler<2>` or higher as appropriate.

**Evidence**: This pattern is consistent with other Layer 2 processors (DiodeClipper, WavefolderProcessor, TubeStage) which also omit internal oversampling per DST-ROADMAP design principle: "Distortion primitives are 'pure' - no internal oversampling."

**Resolution**: Documented exception - no code change required. Layer 4 consumers are responsible for oversampling.
