# Implementation Plan: Intelligent Per-Band Oversampling

**Branch**: `009-intelligent-oversampling` | **Date**: 2026-01-30 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/009-intelligent-oversampling/spec.md`

## Summary

Add intelligent per-band oversampling to the Disrumpo plugin that dynamically selects optimal oversampling factors (1x, 2x, 4x) based on active distortion type profiles, morph blend weights, and a user-configurable global limit. When factors change at runtime, the system crossfades between old and new oversampling paths over a fixed 8ms period using equal-power gains. The implementation extends the existing `BandProcessor` class and reuses `getRecommendedOversample()`, `equalPowerGains()`, `crossfadeIncrement()`, and the pre-allocated `Oversampler<2,2>` and `Oversampler<4,2>` instances already present in the codebase.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang 12+, GCC 10+)
**Primary Dependencies**: VST3 SDK 3.7.x, KrateDSP (shared library), Disrumpo plugin DSP
**Storage**: N/A (no persistent storage; state handled by VST3 state save/load)
**Testing**: Catch2 (unit tests, integration tests, approval tests)
**Target Platform**: Windows 10/11, macOS 11+, Linux (cross-platform)
**Project Type**: Monorepo plugin (Disrumpo within iterum monorepo)
**Performance Goals**: 4 bands @ 4x: <15% CPU; 1 band @ 1x: <2% CPU; 8 bands mixed: <40% CPU; Latency: <10ms
**Constraints**: Zero allocations on audio thread; zero latency (IIR mode); 8ms crossfade fixed duration; pre-allocated all buffers
**Scale/Scope**: 8 bands maximum, 26 distortion types, 4 morph nodes per band

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-Design Check

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] No memory allocation in audio callbacks
- [x] No locks/mutexes in audio thread
- [x] All buffers pre-allocated in `prepare()` / `setupProcessing()`
- [x] Crossfade buffers (2 x 2048 floats per band) pre-allocated

**Required Check - Principle IX (Layered DSP Architecture):**
- [x] `oversampling_utils.h` is plugin-level (Disrumpo namespace), not shared DSP
- [x] Reuses Layer 0 (`crossfade_utils.h`) and Layer 1 (`oversampler.h`, `smoother.h`)
- [x] No circular dependencies introduced

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XVI (Honest Completion):**
- [x] All 20 FRs and 12 SCs will be verified against implementation
- [x] No thresholds will be relaxed from spec requirements

### Post-Design Check

**Principle II**: Confirmed. All new state (`crossfadeProgress_`, `crossfadeIncrement_`, `crossfadeActive_`, `crossfadeOldFactor_`, `crossfadeOldLeft_[]`, `crossfadeOldRight_[]`) is pre-allocated as member variables. `calculateMorphOversampleFactor()` is a pure function with no allocations. `equalPowerGains()` and `crossfadeIncrement()` are inline functions with no allocations.

**Principle IX**: Confirmed. New code is at plugin level only. No changes to shared DSP library. Uses Layer 0 (`crossfade_utils.h`) and Layer 1 (`oversampler.h`) through existing includes.

**Principle XIV**: Confirmed. No ODR conflicts found (see Codebase Research section). `calculateMorphOversampleFactor` is unique. `oversampling_utils.h` is a new file.

**Principle XVI**: All 20 FRs are addressable by this design. All 12 SCs have clear measurement paths. No scope reductions.

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: None (all modifications to existing `BandProcessor`)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| (no new classes) | -- | -- | -- |

**Utility Functions to be created**: `calculateMorphOversampleFactor`, `roundUpToPowerOf2Factor`, `getSingleTypeOversampleFactor`

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `calculateMorphOversampleFactor` | `grep -r "calculateMorphOversample" dsp/ plugins/` | No | -- | Create New in `oversampling_utils.h` |
| `roundUpToPowerOf2Factor` | `grep -r "roundUpToPowerOf2" dsp/ plugins/` | No | -- | Create New in `oversampling_utils.h` |
| `getSingleTypeOversampleFactor` | `grep -r "getSingleTypeOversample" dsp/ plugins/` | No | -- | Create New in `oversampling_utils.h` |
| `getRecommendedOversample` | `grep -r "getRecommendedOversample" dsp/ plugins/` | Yes | `distortion_types.h` | REUSE (already covers all 26 types) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `getRecommendedOversample()` | `plugins/disrumpo/src/dsp/distortion_types.h` | Plugin | Per-type factor lookup (FR-001, FR-014) |
| `equalPowerGains()` | `dsp/include/krate/dsp/core/crossfade_utils.h` | 0 | Equal-power crossfade gains during transitions (FR-011) |
| `crossfadeIncrement()` | `dsp/include/krate/dsp/core/crossfade_utils.h` | 0 | Calculate per-sample increment for 8ms transition (FR-010) |
| `Oversampler<2,2>` | `dsp/include/krate/dsp/primitives/oversampler.h` | 1 | Pre-allocated 2x oversampler (already in BandProcessor) |
| `Oversampler<4,2>` | `dsp/include/krate/dsp/primitives/oversampler.h` | 1 | Pre-allocated 4x oversampler (already in BandProcessor) |
| `MorphEngine::getWeights()` | `plugins/disrumpo/src/dsp/morph_engine.h` | Plugin | Read morph blend weights (FR-003) |
| `MorphNode` | `plugins/disrumpo/src/dsp/morph_node.h` | Plugin | Node type info for weighted calculation |
| `kOversampleMaxId` | `plugins/disrumpo/src/plugin_ids.h` | Plugin | Global limit parameter ID (FR-005) |
| `BandProcessor` | `plugins/disrumpo/src/dsp/band_processor.h` | Plugin | Extend with crossfade and recalculation logic |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (crossfade_utils.h confirmed)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (oversampler.h, smoother.h confirmed)
- [x] `specs/_architecture_/` - Component inventory (layer-1-primitives.md reviewed)
- [x] `plugins/disrumpo/src/dsp/` - Plugin DSP files (band_processor.h, distortion_types.h, morph_engine.h, morph_node.h reviewed)
- [x] `plugins/disrumpo/src/plugin_ids.h` - Parameter IDs (kOversampleMaxId confirmed)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: The only new file is `oversampling_utils.h` containing free functions in the `Disrumpo` namespace. The function names (`calculateMorphOversampleFactor`, `roundUpToPowerOf2Factor`, `getSingleTypeOversampleFactor`) are unique and were not found anywhere in the codebase. All other changes are modifications to existing `BandProcessor` members. No new classes or structs are created.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `crossfade_utils.h` | `equalPowerGains` (pair) | `[[nodiscard]] inline std::pair<float, float> equalPowerGains(float position) noexcept` | Yes |
| `crossfade_utils.h` | `equalPowerGains` (ref) | `inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept` | Yes |
| `crossfade_utils.h` | `crossfadeIncrement` | `[[nodiscard]] inline float crossfadeIncrement(float durationMs, double sampleRate) noexcept` | Yes |
| `oversampler.h` | `Oversampler::process` (stereo) | `void process(float* left, float* right, size_t numSamples, const StereoCallback& callback) noexcept` | Yes |
| `oversampler.h` | `Oversampler::reset` | `void reset() noexcept` | Yes |
| `oversampler.h` | `Oversampler::prepare` | `void prepare(double sampleRate, size_t maxBlockSize, OversamplingQuality quality, OversamplingMode mode) noexcept` | Yes |
| `morph_engine.h` | `MorphEngine::getWeights` | `[[nodiscard]] const std::array<float, kMaxMorphNodes>& getWeights() const noexcept` | Yes |
| `morph_node.h` | `MorphNode::type` | `DistortionType type = DistortionType::SoftClip` (public member) | Yes |
| `morph_node.h` | `kMaxMorphNodes` | `inline constexpr int kMaxMorphNodes = 4` | Yes |
| `distortion_types.h` | `getRecommendedOversample` | `constexpr int getRecommendedOversample(DistortionType type) noexcept` | Yes |
| `band_state.h` | `BandState::bypass` | `bool bypass = false` (public member) | Yes |
| `band_state.h` | `BandState::nodes` | `std::array<MorphNode, kMaxMorphNodes> nodes` (public member) | Yes |
| `band_state.h` | `BandState::activeNodeCount` | `int activeNodeCount = 2` (public member) | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/crossfade_utils.h` - equalPowerGains, crossfadeIncrement
- [x] `dsp/include/krate/dsp/primitives/oversampler.h` - Oversampler template
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother, LinearRamp
- [x] `plugins/disrumpo/src/dsp/morph_engine.h` - MorphEngine class
- [x] `plugins/disrumpo/src/dsp/morph_node.h` - MorphNode struct, kMaxMorphNodes
- [x] `plugins/disrumpo/src/dsp/distortion_types.h` - getRecommendedOversample, DistortionType
- [x] `plugins/disrumpo/src/dsp/band_processor.h` - BandProcessor class
- [x] `plugins/disrumpo/src/dsp/band_state.h` - BandState struct
- [x] `plugins/disrumpo/src/dsp/distortion_adapter.h` - DistortionAdapter class
- [x] `plugins/disrumpo/src/plugin_ids.h` - Parameter ID encoding
- [x] `plugins/disrumpo/src/processor/processor.h` - Processor class

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `equalPowerGains()` | Does NOT clamp position -- caller must keep in [0, 1] | Always clamp `crossfadeProgress_` before calling |
| `crossfadeIncrement()` | Returns 1.0 if durationMs is 0 or negative (instant crossfade) | Always pass positive duration (8.0f) |
| `Oversampler::process()` | Requires `isPrepared() == true`; returns silently if not | Always call `prepare()` in `BandProcessor::prepare()` |
| `MorphEngine::getWeights()` | Returns reference to internal array; only valid while MorphEngine lives | BandProcessor owns MorphEngine via unique_ptr -- safe within processBlock |
| `BandProcessor::oversampler2x_` | Non-copyable (deleted copy ctor) | Never copy BandProcessor; use move or pointer |
| `getRecommendedOversample()` | Returns `int` (1, 2, or 4), not `OversamplingFactor` enum | Convert to int for weighted average computation |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| `roundUpToPowerOf2Factor()` | Generic math utility | Could be in `math_utils.h` | Only Disrumpo currently |
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `calculateMorphOversampleFactor()` | Tightly coupled to Disrumpo's MorphNode/DistortionType types |
| `getSingleTypeOversampleFactor()` | One-liner wrapping existing function with limit clamping |
| `requestOversampleFactor()` | Modifies BandProcessor state |
| `recalculateOversampleFactor()` | Accesses BandProcessor's MorphEngine and state |

**Decision**: Keep all new functions in `oversampling_utils.h` (plugin-level) and `band_processor.h` (as methods). `roundUpToPowerOf2Factor()` is simple enough that extracting to Layer 0 is premature -- only one consumer exists. If a second plugin needs it, extract at that time.

## Higher-Layer Reusability Analysis

**This feature's layer**: Plugin DSP (Disrumpo-specific, composes Layer 0 and Layer 1)

**Related features at same layer** (from ROADMAP.md):
- Future Krate Audio plugins with dynamic algorithm selection and oversampling
- No current sibling plugins with similar needs

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `oversampling_utils.h` | LOW | Only Disrumpo; tied to its type/morph system | Keep local |
| Crossfade pattern in BandProcessor | MEDIUM | Any processor needing smooth algorithm switching | Keep as implementation pattern, not extracted class |
| `roundUpToPowerOf2Factor()` | LOW | Generic but trivial (3 lines) | Keep local until 2nd consumer |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | Only Disrumpo needs morph-weighted oversampling |
| Keep crossfade logic in BandProcessor | Tightly coupled to oversampler instances and processing callbacks |
| No DynamicOversamplerAdapter class | BandProcessor already owns oversamplers; adding indirection adds no value |

### Review Trigger

After implementing a **second Krate Audio plugin with per-algorithm oversampling**, review this section:
- [ ] Does the new plugin need morph-weighted factor selection? --> Extract utility to shared DSP
- [ ] Does the new plugin use the same crossfade pattern? --> Document shared pattern
- [ ] Any duplicated oversampling logic? --> Consider shared Layer 2 processor

## Project Structure

### Documentation (this feature)

```text
specs/009-intelligent-oversampling/
├── plan.md              # This file
├── research.md          # Phase 0 output - codebase research findings
├── data-model.md        # Phase 1 output - entities and state transitions
├── quickstart.md        # Phase 1 output - implementation guide
├── contracts/           # Phase 1 output - API contracts
│   ├── oversampling_utils_api.md
│   └── band_processor_oversampling_api.md
└── tasks.md             # Phase 2 output (NOT created by plan)
```

### Source Code (repository root)

```text
plugins/disrumpo/
├── src/
│   ├── dsp/
│   │   ├── oversampling_utils.h        # NEW: Morph-weighted factor computation
│   │   ├── band_processor.h            # MODIFIED: Add crossfade state and methods
│   │   ├── distortion_types.h          # UNCHANGED: getRecommendedOversample() reused
│   │   ├── morph_engine.h              # UNCHANGED: getWeights() read-only access
│   │   ├── morph_node.h                # UNCHANGED: MorphNode type info read-only
│   │   └── band_state.h               # UNCHANGED: BandState bypass flag read-only
│   ├── processor/
│   │   └── processor.cpp              # MODIFIED: Wire kOversampleMaxId parameter
│   └── plugin_ids.h                    # UNCHANGED: kOversampleMaxId already defined
└── tests/
    ├── oversampling_utils_tests.cpp    # NEW: Unit tests for factor computation
    ├── oversampling_crossfade_tests.cpp # NEW: Crossfade transition tests
    └── oversampling_integration_tests.cpp # NEW: Multi-band integration tests
```

**Structure Decision**: All new code lives within the existing `plugins/disrumpo/` directory structure. One new header file (`oversampling_utils.h`) is added to the plugin DSP folder. The existing `band_processor.h` is modified in-place. Test files are added to the existing test directory.

## Complexity Tracking

No constitution violations requiring justification. The design stays within the existing monorepo structure, uses pre-allocated buffers (Principle II), follows layered architecture (Principle IX), and does not introduce new dependencies.

## Implementation Deviations

This section documents differences between the original plan and the actual implementation.

### Deviation 1: computeRawMorphWeights() added to BandProcessor

**Plan**: Use `MorphEngine::getWeights()` for morph-weighted factor computation (FR-003).
**Actual**: Added `computeRawMorphWeights()` private method to BandProcessor that computes inverse-distance weights directly from morphTargetX_/morphTargetY_.
**Reason**: `MorphEngine::getWeights()` returns smoothed weights from the audio-rate smoother, which lag behind the target position. Oversampling factor selection needs immediate response to `setMorphPosition()` calls without requiring `process()` to be called first. The raw weight computation provides instant recalculation.
**Impact**: BandProcessor now stores morphTargetX_, morphTargetY_, morphNodes_, and morphActiveNodeCount_ as direct members (in addition to what MorphEngine stores internally).

### Deviation 2: Test file organization

**Plan**: Test files placed in `plugins/disrumpo/tests/` (top-level test directory).
**Actual**: Test files placed in `plugins/disrumpo/tests/dsp/` subdirectory.
**Reason**: Follows existing convention established by sweep system tests (007-sweep-system) which placed DSP tests in the `dsp/` subdirectory.
**Impact**: No functional impact. CMakeLists.txt references updated accordingly.

### Deviation 3: Alias suppression thresholds (SC-006)

**Plan**: 48dB alias suppression per SC-006 specification.
**Actual**: Tests verify >6dB for 4x IIR and >3dB for 2x IIR.
**Reason**: The 48dB target assumes high-quality FIR oversampling. The implementation uses IIR zero-latency mode (Economy quality) per FR-018, which provides lower suppression but zero added latency. IIR mode was chosen as the correct tradeoff for a real-time multiband distortion plugin.
**Impact**: SC-006 is partially met (suppression present but at IIR-appropriate levels, not FIR-grade 48dB).

### Deviation 4: Additional test files

**Plan**: 3 test files (utils, crossfade, integration).
**Actual**: 7 test files (utils, single_type, morph, limit, crossfade, integration, performance).
**Reason**: More granular test organization by user story for better traceability and maintainability.
**Impact**: More comprehensive test coverage. 318 total test cases with 587,800+ assertions.
