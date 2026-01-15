# Implementation Plan: DistortionRack System

**Branch**: `068-distortion-rack` | **Date**: 2026-01-15 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/068-distortion-rack/spec.md`

## Summary

Layer 3 DistortionRack system providing a chainable 4-slot distortion processor rack. Uses `std::variant` with compile-time dispatch via template visitor for zero-overhead processor abstraction. Composes Layer 2 distortion processors (TubeStage, DiodeClipper, WavefolderProcessor, TapeSaturator, FuzzProcessor, BitcrusherProcessor) and Layer 1 primitives (Waveshaper, DCBlocker, Oversampler, OnePoleSmoother). Features stereo processing, per-slot enable/mix/gain controls with 5ms smoothing, per-slot DC blocking, and global oversampling (1x/2x/4x).

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- Layer 2: TubeStage, DiodeClipper, WavefolderProcessor, TapeSaturator, FuzzProcessor, BitcrusherProcessor
- Layer 1: Waveshaper, DCBlocker, Oversampler<Factor, 2>, OnePoleSmoother
- Layer 0: dbToGain (db_utils.h)
**Storage**: N/A (DSP-only)
**Testing**: Catch2 (dsp_tests target)
**Target Platform**: Windows/macOS/Linux (cross-platform)
**Project Type**: DSP library component (dsp/include/krate/dsp/systems/)
**Performance Goals**: < 2% CPU @ 44.1kHz stereo with 4x oversampling and all 4 slots active (SC-008)
**Constraints**:
- Real-time safe: no allocations in process() (FR-041)
- Slot type changes allocate immediately during setSlotType() call (FR-003a)
- 5ms parameter smoothing on enable/mix/gain (FR-009, FR-015, FR-046)
- Per-slot DC blocking after enabled slots (FR-048)
**Scale/Scope**: Single header-only class with ~500-700 LOC

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):** N/A - Pure DSP component

**Principle II (Real-Time Audio Thread Safety):**
- [x] No allocations in process() - All buffers pre-allocated in prepare()
- [x] No locks/mutexes - Atomic-free design, parameters set from control thread
- [x] setSlotType() allocates on control thread before audio use (FR-003a)

**Principle III (Modern C++ Standards):**
- [x] C++20 required for std::variant template visitor pattern
- [x] RAII via unique_ptr not needed - variant manages lifetime
- [x] constexpr/const used where appropriate

**Principle IX (Layered DSP Architecture):**
- [x] Layer 3 (Systems) - composes Layer 2 processors and Layer 1 primitives
- [x] No Layer 4 dependencies

**Principle X (DSP Processing Constraints):**
- [x] Oversampling provided globally around chain (FR-020-FR-027)
- [x] DC blocking per-slot after saturation (FR-048-FR-050)
- [x] No soft limiting needed (individual processors handle their own)

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, dsp-architecture)
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: DistortionRack, SlotType enum, SlotState struct (internal)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| DistortionRack | `grep -r "class DistortionRack" dsp/ plugins/` | No | Create New |
| SlotType | `grep -r "enum.*SlotType" dsp/ plugins/` | No | Create New |
| SlotState | `grep -r "struct SlotState" dsp/ plugins/` | No | Create New (internal) |
| ProcessorVariant | `grep -r "ProcessorVariant" dsp/ plugins/` | No | Create New (internal) |

**Utility Functions to be created**: None needed - all utilities exist in Layer 0/1

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Waveshaper | dsp/include/krate/dsp/primitives/waveshaper.h | 1 | SlotType::Waveshaper processor |
| TubeStage | dsp/include/krate/dsp/processors/tube_stage.h | 2 | SlotType::TubeStage processor |
| DiodeClipper | dsp/include/krate/dsp/processors/diode_clipper.h | 2 | SlotType::DiodeClipper processor |
| WavefolderProcessor | dsp/include/krate/dsp/processors/wavefolder_processor.h | 2 | SlotType::Wavefolder processor |
| TapeSaturator | dsp/include/krate/dsp/processors/tape_saturator.h | 2 | SlotType::TapeSaturator processor |
| FuzzProcessor | dsp/include/krate/dsp/processors/fuzz_processor.h | 2 | SlotType::Fuzz processor |
| BitcrusherProcessor | dsp/include/krate/dsp/processors/bitcrusher_processor.h | 2 | SlotType::Bitcrusher processor |
| DCBlocker | dsp/include/krate/dsp/primitives/dc_blocker.h | 1 | Per-slot DC blocking |
| Oversampler | dsp/include/krate/dsp/primitives/oversampler.h | 1 | Global oversampling |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | Gain conversion |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 DSP processors
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 system components
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: "DistortionRack", "SlotType" (in context of distortion), "SlotState", and "ProcessorVariant" are unique names not found in codebase. All processor types being composed already exist and are tested.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Waveshaper | process | `[[nodiscard]] float process(float x) const noexcept` | Yes |
| Waveshaper | processBlock | `void processBlock(float* buffer, size_t numSamples) noexcept` | Yes |
| TubeStage | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| TubeStage | reset | `void reset() noexcept` | Yes |
| TubeStage | process | `void process(float* buffer, size_t numSamples) noexcept` | Yes |
| DiodeClipper | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| DiodeClipper | reset | `void reset() noexcept` | Yes |
| DiodeClipper | process | `void process(float* buffer, size_t numSamples) noexcept` | Yes |
| WavefolderProcessor | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| WavefolderProcessor | reset | `void reset() noexcept` | Yes |
| WavefolderProcessor | process | `void process(float* buffer, size_t numSamples) noexcept` | Yes |
| TapeSaturator | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| TapeSaturator | reset | `void reset() noexcept` | Yes |
| TapeSaturator | process | `void process(float* buffer, size_t numSamples) noexcept` | Yes |
| FuzzProcessor | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| FuzzProcessor | reset | `void reset() noexcept` | Yes |
| FuzzProcessor | process | `void process(float* buffer, size_t numSamples) noexcept` | Yes |
| BitcrusherProcessor | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| BitcrusherProcessor | reset | `void reset() noexcept` | Yes |
| BitcrusherProcessor | process | `void process(float* buffer, size_t numSamples) noexcept` | Yes |
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker | reset | `void reset() noexcept` | Yes |
| DCBlocker | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| Oversampler<2,2> | prepare | `void prepare(double sampleRate, size_t maxBlockSize, OversamplingQuality quality = Economy, OversamplingMode mode = ZeroLatency) noexcept` | Yes |
| Oversampler<2,2> | process | `void process(float* left, float* right, size_t numSamples, const StereoCallback& callback) noexcept` | Yes |
| Oversampler<2,2> | reset | `void reset() noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapToTarget | `void snapToTarget() noexcept` | Yes |
| dbToGain | dbToGain | `[[nodiscard]] constexpr float dbToGain(float dB) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/waveshaper.h` - Waveshaper class (mono only)
- [x] `dsp/include/krate/dsp/processors/tube_stage.h` - TubeStage class (mono only)
- [x] `dsp/include/krate/dsp/processors/diode_clipper.h` - DiodeClipper class (mono only)
- [x] `dsp/include/krate/dsp/processors/wavefolder_processor.h` - WavefolderProcessor class (mono only)
- [x] `dsp/include/krate/dsp/processors/tape_saturator.h` - TapeSaturator class (mono only)
- [x] `dsp/include/krate/dsp/processors/fuzz_processor.h` - FuzzProcessor class (mono only)
- [x] `dsp/include/krate/dsp/processors/bitcrusher_processor.h` - BitcrusherProcessor class (mono only)
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `dsp/include/krate/dsp/primitives/oversampler.h` - Oversampler template
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dbToGain function

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| All Layer 2 processors | process() is MONO only (single buffer) | Need 2 instances per slot for stereo L/R |
| Waveshaper | No prepare() method (stateless) | Use directly without prepare() |
| Oversampler | Template parameters `<Factor, NumChannels>` | Use `Oversampler<2, 2>` for 2x stereo, `Oversampler<4, 2>` for 4x stereo |
| Oversampler | Factor must be 2 or 4 | Cannot use factor=1 with Oversampler - bypass it entirely |
| OnePoleSmoother | Uses `snapTo()` for both current+target | Use `snapToTarget()` to snap current to target only |
| DCBlocker | Default cutoff is 10Hz | Pass explicit 10.0f to prepare() for clarity |

## Layer 0 Candidate Analysis

*No new Layer 0 utilities needed - all existing utilities from db_utils.h are sufficient.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Slot processing logic | Specific to DistortionRack, not general-purpose |

**Decision**: No Layer 0 extraction needed.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 3 - System Components

**Related features at same layer** (from layer-3-systems.md):
- AmpChannel (spec 065) - Multi-stage amp with tone stack
- TapeMachine (spec 066) - Complete tape emulation
- FuzzPedal (spec 067) - Fuzz with gate and buffer

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| std::variant processor wrapper pattern | MEDIUM | Future multi-slot systems | Keep local - pattern can be copied |
| Per-slot enable/mix smoothing | LOW | Unlikely - each system has unique mix semantics | Keep local |
| Global oversampling wrapper | MEDIUM | AmpChannel already has own oversampling | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep variant wrapper local | First use of this pattern; extract after 2nd concrete consumer |
| No shared SlotMixer class | Each system has different slot semantics (mix vs blend vs crossfade) |

### Review Trigger

After implementing **next multi-slot effect system**, review this section:
- [ ] Does sibling need variant wrapper pattern? -> Consider extraction to common/
- [ ] Does sibling use similar per-slot smoothing? -> Document shared pattern
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/068-distortion-rack/
├── plan.md              # This file
├── research.md          # Phase 0 output (std::variant patterns)
├── data-model.md        # Phase 1 output (entity definitions)
├── quickstart.md        # Phase 1 output (usage guide)
├── contracts/           # Phase 1 output (API contracts)
│   └── distortion_rack_api.md
└── tasks.md             # Phase 2 output (implementation tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── systems/
│       └── distortion_rack.h    # NEW: DistortionRack class
└── tests/
    └── systems/
        └── distortion_rack_tests.cpp  # NEW: Unit tests
```

**Structure Decision**: Header-only implementation in `systems/` directory following existing Layer 3 patterns (AmpChannel, TapeMachine, FuzzPedal).

## Complexity Tracking

> No Constitution violations requiring justification.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| -- | -- | -- |
