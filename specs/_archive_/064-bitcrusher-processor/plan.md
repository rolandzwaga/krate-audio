# Implementation Plan: BitcrusherProcessor

**Branch**: `064-bitcrusher-processor` | **Date**: 2026-01-14 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/064-bitcrusher-processor/spec.md`

## Summary

Layer 2 processor composing existing Layer 1 primitives (BitCrusher, SampleRateReducer, DCBlocker, OnePoleSmoother) into a unified bitcrushing effect with gain staging, dither gating, configurable processing order, and parameter smoothing for click-free automation.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Layer 0 (db_utils.h), Layer 1 (bit_crusher.h, sample_rate_reducer.h, dc_blocker.h, smoother.h), Layer 2 (envelope_follower.h for dither gate)
**Storage**: N/A (DSP processor, no persistence)
**Testing**: Catch2 (via dsp_tests target)
**Target Platform**: Windows, macOS, Linux (cross-platform VST3)
**Project Type**: DSP library component
**Performance Goals**: < 0.5% CPU per mono channel at 44.1kHz (Layer 2 budget)
**Constraints**: Real-time safe (noexcept, no allocations in process), header-only
**Scale/Scope**: Single header file, composing 4-5 existing primitives

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check:**
- [x] Principle II (Real-Time Safety): All primitives are already noexcept, no allocations in process
- [x] Principle III (Modern C++): Will use C++20, constexpr, [[nodiscard]]
- [x] Principle IX (Layered Architecture): Layer 2, only depends on Layer 0/1
- [x] Principle X (DSP Constraints): DC blocking after processing (FR-012, FR-013)
- [x] Principle XI (Performance Budget): Target < 0.5% CPU
- [x] Principle XII (Test-First Development): Tests before implementation
- [x] Principle XIV (ODR Prevention): No duplicate classes (search complete)
- [x] Principle XVI (Honest Completion): All FR/SC requirements tracked

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

**Classes/Structs to be created**: BitcrusherProcessor, ProcessingOrder

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| BitcrusherProcessor | `grep -r "BitcrusherProcessor" dsp/ plugins/` | No | Create New |
| ProcessingOrder | `grep -r "ProcessingOrder" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (reusing existing dbToGain)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| dbToGain | `grep -r "dbToGain" dsp/` | Yes | db_utils.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| BitCrusher | dsp/include/krate/dsp/primitives/bit_crusher.h | 1 | Bit depth reduction + TPDF dither |
| SampleRateReducer | dsp/include/krate/dsp/primitives/sample_rate_reducer.h | 1 | Sample rate decimation |
| DCBlocker | dsp/include/krate/dsp/primitives/dc_blocker.h | 1 | DC offset removal post-processing |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing (gains, mix) |
| EnvelopeFollower | dsp/include/krate/dsp/processors/envelope_follower.h | 2 | Dither gate signal detection |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | dB to linear conversion |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors
- [x] `specs/_architecture_/` - Component inventory
- [x] `specs/_architecture_/layer-2-processors.md` - Verified no BitcrusherProcessor

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: BitcrusherProcessor is a new class not found in codebase. ProcessingOrder enum is unique to this processor. All primitives being composed are existing Layer 1 components with stable APIs.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| BitCrusher | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| BitCrusher | reset | `void reset() noexcept` | Yes |
| BitCrusher | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| BitCrusher | setBitDepth | `void setBitDepth(float bits) noexcept` | Yes |
| BitCrusher | setDither | `void setDither(float amount) noexcept` | Yes |
| BitCrusher | setSeed | `void setSeed(uint32_t seed) noexcept` | Yes |
| SampleRateReducer | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SampleRateReducer | reset | `void reset() noexcept` | Yes |
| SampleRateReducer | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| SampleRateReducer | setReductionFactor | `void setReductionFactor(float factor) noexcept` | Yes |
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker | reset | `void reset() noexcept` | Yes |
| DCBlocker | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| EnvelopeFollower | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| EnvelopeFollower | processSample | `[[nodiscard]] float processSample(float input) noexcept` | Yes |
| EnvelopeFollower | setMode | `void setMode(DetectionMode mode) noexcept` | Yes |
| EnvelopeFollower | setAttackTime | `void setAttackTime(float ms) noexcept` | Yes |
| EnvelopeFollower | setReleaseTime | `void setReleaseTime(float ms) noexcept` | Yes |
| dbToGain | function | `[[nodiscard]] constexpr float dbToGain(float dB) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/bit_crusher.h` - BitCrusher class
- [x] `dsp/include/krate/dsp/primitives/sample_rate_reducer.h` - SampleRateReducer class
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/processors/envelope_follower.h` - EnvelopeFollower class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dbToGain function

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| BitCrusher | Bit depth range is [4, 16] | `setBitDepth(std::clamp(bits, 4.0f, 16.0f))` |
| BitCrusher | setSeed needed for L/R decorrelation | Use different seeds per channel |
| SampleRateReducer | Factor range is [1, 8] | `setReductionFactor(std::clamp(factor, 1.0f, 8.0f))` |
| DCBlocker | Default cutoff is 10Hz | `prepare(sr, 10.0f)` |
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| EnvelopeFollower | Needs `prepare()` before use | Call in BitcrusherProcessor::prepare() |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| None identified | All needed utilities exist | N/A | N/A |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| processBitCrushFirst | Processing order specific to this processor |
| processSampleReduceFirst | Processing order specific to this processor |

**Decision**: No new Layer 0 utilities needed. All required functions (dbToGain, flushDenormal, etc.) already exist.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from ROADMAP.md or known plans):
- CharacterProcessor (spec 021): Uses BitCrusher/SampleRateReducer in larger context
- LoFiProcessor (potential future): Similar composition pattern
- VinylProcessor (potential future): Would use similar gain staging pattern

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Gain staging pattern | MEDIUM | CharacterProcessor, LoFiProcessor | Keep local for now |
| Dither gate pattern | LOW | Only bitcrusher needs dither gating | Keep in this processor |
| Processing order enum | LOW | Specific to bitcrusher ordering | Keep in this processor |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | First feature at this layer using this specific composition |
| Keep dither gate local | Unique requirement for bitcrusher, other processors don't need it |
| Reuse EnvelopeFollower | Already exists at Layer 2, good for signal detection |

### Review Trigger

After implementing **CharacterProcessor (spec 021)**, review this section:
- [ ] Does CharacterProcessor use same gain staging pattern? -> Consider extracting if yes
- [ ] Does CharacterProcessor need processing order control? -> Document if pattern emerges
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/064-bitcrusher-processor/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── contracts/           # Phase 1 output (API contracts)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── bitcrusher_processor.h    # NEW: Main implementation (header-only)
└── tests/
    └── processors/
        └── bitcrusher_processor_tests.cpp  # NEW: Unit tests
```

**Structure Decision**: Single header-only implementation at Layer 2, following established patterns from TapeSaturator, SaturationProcessor, and DiodeClipper.

## Post-Design Constitution Re-Check

*Verification after Phase 1 design completion.*

**Post-Design Check:**
- [x] Principle II (Real-Time Safety): API contract specifies noexcept on all methods
- [x] Principle III (Modern C++): Design uses [[nodiscard]], constexpr, enum class
- [x] Principle IX (Layered Architecture): Confirmed Layer 2 dependencies only (Layer 0/1 + EnvelopeFollower at Layer 2)
- [x] Principle X (DSP Constraints): DCBlocker at 10Hz confirmed in data model
- [x] Principle XI (Performance Budget): Composition of lightweight primitives < 0.5% budget
- [x] Principle XIV (ODR Prevention): No conflicts found in codebase search

**Constitution Compliance Status**: PASS - No violations, design follows established patterns.

## Complexity Tracking

No violations identified. Design follows established Layer 2 processor patterns.

---

## Generated Artifacts

| Artifact | Path | Status |
|----------|------|--------|
| Implementation Plan | `specs/064-bitcrusher-processor/plan.md` | Complete |
| Research Document | `specs/064-bitcrusher-processor/research.md` | Complete |
| Data Model | `specs/064-bitcrusher-processor/data-model.md` | Complete |
| API Contract | `specs/064-bitcrusher-processor/contracts/bitcrusher_processor_api.h` | Complete |
| Quickstart Guide | `specs/064-bitcrusher-processor/quickstart.md` | Complete |

## Next Steps

This plan is complete. The next phase is task generation via `/speckit.tasks` which will create `tasks.md` with implementation steps following test-first development (Principle XII).
