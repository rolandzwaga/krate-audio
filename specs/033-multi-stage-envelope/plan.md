# Implementation Plan: Multi-Stage Envelope Generator

**Branch**: `033-multi-stage-envelope` | **Date**: 2026-02-07 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/033-multi-stage-envelope/spec.md`

## Summary

Implement a configurable multi-stage envelope generator (4-8 stages) as a Layer 2 DSP processor. The envelope supports per-stage time/level/curve, sustain point selection, loop points for LFO-like behavior, retrigger modes (hard/legato), and real-time parameter changes. It builds on the existing ADSREnvelope infrastructure by extracting shared coefficient calculation utilities into a new `envelope_utils.h` at Layer 1. The implementation uses the EarLevel Engineering one-pole iterative method for exponential/linear curves and quadratic phase mapping for logarithmic curves, with time-based stage completion for deterministic timing.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: `envelope_utils.h` (new, extracted from `adsr_envelope.h`), `db_utils.h` (Layer 0)
**Storage**: N/A (pure DSP processor, no persistence)
**Testing**: Catch2 (via `dsp_tests` target)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: Shared DSP library component (monorepo)
**Performance Goals**: < 0.05% CPU per instance at 44,100Hz (SC-003). Per-sample: 1 mul + 1 add (exp/linear) or 2 mul + 1 add (logarithmic) + stage transition check.
**Constraints**: Real-time safe (zero allocations, no locks, no exceptions in process path). Header-only or minimal cpp.
**Scale/Scope**: Single class with ~300-500 lines. 8 stages max (fixed array, compile-time bound).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | N/A | Pure DSP component, no VST3 involvement |
| II. Real-Time Audio Thread Safety | PASS | All process methods noexcept, zero allocations, no locks. Fixed-size arrays. |
| III. Modern C++ Standards | PASS | C++20, constexpr, std::array, value semantics, no raw new/delete |
| IV. SIMD & DSP Optimization | PASS | SIMD assessed as NOT BENEFICIAL (serial feedback loop). Scalar-only. |
| V. VSTGUI Development | N/A | No UI component |
| VI. Cross-Platform Compatibility | PASS | No platform-specific code. Uses std::exp, std::log (portable). |
| VII. Project Structure & Build System | PASS | Layer 2 processor at correct location, CMake integration |
| VIII. Testing Discipline | PASS | Test-first development, Catch2, all FRs will have tests |
| IX. Layered DSP Architecture | PASS | Layer 2, depends only on Layer 0-1 |
| X. DSP Processing Constraints | PASS | No saturation/oversampling needed (control signal, not audio) |
| XI. Performance Budgets | PASS | Layer 2 budget < 0.5%, target < 0.05% |
| XII. Debugging Discipline | PASS | N/A for planning phase |
| XIII. Test-First Development | PASS | Tests written before implementation |
| XIV. Living Architecture Documentation | PASS | Will update layer-2-processors.md |
| XV. Pre-Implementation Research (ODR) | PASS | All searches performed, no conflicts |
| XVI. Honest Completion | PASS | Will verify each FR/SC individually |
| XVII. Framework Knowledge Documentation | N/A | No framework debugging needed |
| XVIII. Spec Numbering | PASS | Spec 033, correct |

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

**Classes/Structs to be created**: MultiStageEnvelope, EnvStageConfig, MultiStageEnvState

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| MultiStageEnvelope | `grep -r "class MultiStageEnvelope[^F]" dsp/ plugins/` | No | Create New |
| EnvStageConfig | `grep -r "struct EnvStageConfig" dsp/ plugins/` | No | Create New |
| MultiStageEnvState | `grep -r "MultiStageEnvState" dsp/ plugins/` | No | Create New |
| StageCoefficients | `grep -r "struct StageCoefficients" dsp/` | Yes (private in ADSREnvelope) | Extract to envelope_utils.h |

**Utility Functions to be created**: calcEnvCoefficients (renamed from calcCoefficients to avoid shadowing)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| calcEnvCoefficients | `grep -r "calcCoefficients" dsp/` | Yes (private static in ADSREnvelope) | adsr_envelope.h:341 | Extract to envelope_utils.h, rename to calcEnvCoefficients |
| getAttackTargetRatio | `grep -r "getAttackTargetRatio" dsp/` | Yes (private static in ADSREnvelope) | adsr_envelope.h:360 | Extract to envelope_utils.h |
| getDecayTargetRatio | `grep -r "getDecayTargetRatio" dsp/` | Yes (private static in ADSREnvelope) | adsr_envelope.h:369 | Extract to envelope_utils.h |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| EnvCurve enum | adsr_envelope.h:71-75 | 1 | Extract to envelope_utils.h, used for per-stage curve selection |
| RetriggerMode enum | adsr_envelope.h:77-80 | 1 | Extract to envelope_utils.h, used for retrigger mode |
| kEnvelopeIdleThreshold | adsr_envelope.h:51 | 1 | Extract to envelope_utils.h, release-to-idle threshold |
| kSustainSmoothTimeMs | adsr_envelope.h:54 | 1 | Extract to envelope_utils.h, sustain smoothing coefficient |
| calcCoefficients() | adsr_envelope.h:341-358 | 1 | Extract to envelope_utils.h, per-stage coefficient calculation |
| detail::isNaN() | db_utils.h:54-58 | 0 | Input validation in setters |
| detail::flushDenormal() | db_utils.h:156-158 | 0 | Denormal prevention in process |
| detail::constexprExp() | db_utils.h:114-145 | 0 | Coefficient calculation fallback |
| ITERUM_NOINLINE macro | adsr_envelope.h:37-45 | 1 | NaN-safe setters (guarded #ifndef) |

### Potential ODR Conflicts Identified

| Existing Type | Location | Risk | Mitigation |
|---------------|----------|------|------------|
| `EnvelopeState` | multistage_env_filter.h:37 | LOW | Different name: `MultiStageEnvState` |
| `EnvelopeStage` | multistage_env_filter.h:53 | LOW | Different name: `EnvStageConfig` |
| `MultiStageEnvelopeFilter` | multistage_env_filter.h:109 | NONE | Different class name |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors
- [x] `specs/_architecture_/` - Component inventory
- [x] `dsp/include/krate/dsp/primitives/adsr_envelope.h` - Source for extraction
- [x] `dsp/include/krate/dsp/processors/multistage_env_filter.h` - Similar name, different purpose

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned type names (`MultiStageEnvelope`, `EnvStageConfig`, `MultiStageEnvState`) are unique in the codebase. The extraction of `StageCoefficients` and `calcCoefficients()` from ADSREnvelope to a shared header is a relocation, not a duplication -- the original private members will be replaced by includes. The function is renamed to `calcEnvCoefficients` at the shared level to be self-documenting.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| ADSREnvelope | calcCoefficients | `static StageCoefficients calcCoefficients(float timeMs, float sampleRate, float targetLevel, float targetRatio, bool rising) noexcept` | Yes (adsr_envelope.h:341-358) |
| ADSREnvelope | kEnvelopeIdleThreshold | `inline constexpr float kEnvelopeIdleThreshold = 1e-4f` | Yes (adsr_envelope.h:51) |
| ADSREnvelope | kSustainSmoothTimeMs | `inline constexpr float kSustainSmoothTimeMs = 5.0f` | Yes (adsr_envelope.h:54) |
| ADSREnvelope | EnvCurve | `enum class EnvCurve : uint8_t { Exponential = 0, Linear, Logarithmic }` | Yes (adsr_envelope.h:71-75) |
| ADSREnvelope | RetriggerMode | `enum class RetriggerMode : uint8_t { Hard = 0, Legato }` | Yes (adsr_envelope.h:77-80) |
| db_utils | detail::isNaN | `constexpr bool isNaN(float x) noexcept` | Yes (db_utils.h:54) |
| db_utils | detail::flushDenormal | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes (db_utils.h:156) |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/adsr_envelope.h` - Full ADSREnvelope class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - Layer 0 utilities
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother reference
- [x] `dsp/include/krate/dsp/processors/multistage_env_filter.h` - ODR conflict check

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| ADSREnvelope | `calcCoefficients` is private static | Must extract to shared header before MultiStageEnvelope can use |
| ADSREnvelope | Sustain smoothing uses raw coefficient, not OnePoleSmoother | `output_ = target + coef * (output_ - target)` inline formula |
| ADSREnvelope | Release uses constant-rate (1.0->0.0 time, not current->0.0 time) | Coefficient calculated for full range; partial release is shorter |
| db_utils | `detail::isNaN()` needs `-fno-fast-math` on the source file | Add test file to CMake set_source_files_properties list |
| ITERUM_NOINLINE | Guarded by `#ifndef` -- already defined if adsr_envelope.h included | Both headers guard the macro, no conflict |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

**No Layer 0 extraction needed.** The shared utilities are envelope-specific domain concepts that belong at Layer 1. The `calcEnvCoefficients()` function uses the EarLevel Engineering formula with envelope-specific target ratios -- these are not general math utilities.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `enterStage()` (stage entry helper) | Stateful, references member fields, not reusable outside MultiStageEnvelope |
| `processRunning()` | Per-state processing, tightly coupled to FSM |
| `processReleasing()` | Per-state processing, tightly coupled to FSM |

**Decision**: Extract envelope coefficient calculation and constants to Layer 1 shared header (`envelope_utils.h`). Keep all FSM logic and per-sample processing as member functions of `MultiStageEnvelope`.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES | One-pole formula: output[n] depends on output[n-1] |
| **Data parallelism width** | 1 | Single envelope instance, one sample at a time |
| **Branch density in inner loop** | MEDIUM | Stage completion check, state machine switch, loop boundary check |
| **Dominant operations** | Arithmetic | 1 mul + 1 add per sample (exp/linear), 2 mul + 1 add (log) |
| **Current CPU budget vs expected usage** | < 0.5% vs < 0.05% | Massive headroom; optimization unnecessary |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The one-pole iterative formula creates a serial dependency between consecutive samples, making intra-envelope SIMD impossible. The only viable SIMD approach would be multi-instance (processing 4-8 envelopes in parallel), which is a voice-allocator-level concern, not an envelope-level concern. The per-sample cost is already minimal (2-3 FLOPs), well under budget.

### Implementation Workflow

**Verdict is NOT BENEFICIAL**: Skip SIMD Phase 2. No alternative optimizations needed -- the algorithm is already minimal.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Early-out for Idle state | ~100% for inactive envelopes | LOW | YES |
| Block fill for Sustaining state (constant output) | ~80% for sustained notes | LOW | YES (if sustain smoother is settled) |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from synth-roadmap.md):
- Phase 2.2: Mono/Legato Handler -- uses envelope state queries for voice management
- Phase 2.3: Note Event Processor -- no direct code sharing expected
- MultiStageEnvelopeFilter (existing) -- could potentially use MultiStageEnvelope as its internal envelope source

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `envelope_utils.h` (shared constants/enums/coeff calc) | HIGH | ADSREnvelope, future envelope types | Extract now (required for this spec) |
| `MultiStageEnvelope` class | HIGH | SynthVoice (Phase 3.1), PolySynth (Phase 3.2), ModulationMatrix | Keep as-is; designed for composition |
| `EnvStageConfig` struct | MEDIUM | Potentially MultiStageEnvelopeFilter refactor | Keep in multi_stage_envelope.h for now |

### Detailed Analysis (for HIGH potential items)

**`envelope_utils.h`** provides:
- Envelope constants (idle threshold, time ranges, target ratios)
- Curve shape enum (`EnvCurve`)
- Retrigger mode enum (`RetriggerMode`)
- Coefficient calculation function (`calcEnvCoefficients`)
- Target ratio selection functions

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| ADSREnvelope | YES | Direct consumer after extraction (required refactor) |
| Future envelope types | YES | Any new envelope class needs the same coefficient math |
| MultiStageEnvelopeFilter | MAYBE | Could be refactored to use MultiStageEnvelope internally, separate task |

**Recommendation**: Extract now -- required for this spec and has an immediate second consumer (ADSREnvelope).

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Extract `envelope_utils.h` now | Two immediate consumers (ADSR + MultiStage); prevents duplication |
| Keep `EnvStageConfig` in `multi_stage_envelope.h` | Only one consumer so far; move to shared location when 2nd consumer appears |
| Rename `calcCoefficients` to `calcEnvCoefficients` | Self-documenting at namespace scope; avoids shadowing private member name |

### Review Trigger

After implementing **Phase 2.2 (Mono/Legato Handler)**, review this section:
- [ ] Does Mono/Legato Handler need `envelope_utils.h`? (likely yes for RetriggerMode)
- [ ] Does it use the same state query pattern? (likely `isActive()`, `isReleasing()`)
- [ ] Any duplicated envelope state management code?

## Project Structure

### Documentation (this feature)

```text
specs/033-multi-stage-envelope/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   ├── multi_stage_envelope_api.h
│   └── envelope_utils_api.h
└── tasks.md             # Phase 2 output (NOT created by plan)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   ├── primitives/
│   │   ├── envelope_utils.h         # NEW: Shared envelope utilities (Layer 1)
│   │   └── adsr_envelope.h          # MODIFIED: includes envelope_utils.h
│   └── processors/
│       └── multi_stage_envelope.h   # NEW: MultiStageEnvelope class (Layer 2)
└── tests/
    └── unit/
        └── processors/
            └── multi_stage_envelope_test.cpp  # NEW: Unit tests
```

**Structure Decision**: Standard monorepo DSP component structure. New header-only class at Layer 2, shared utilities at Layer 1, tests in the test mirror structure.

## Complexity Tracking

No constitution violations. No complexity tracking entries needed.
