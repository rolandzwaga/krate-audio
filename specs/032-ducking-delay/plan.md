# Implementation Plan: Ducking Delay

**Branch**: `032-ducking-delay` | **Date**: 2025-12-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/032-ducking-delay/spec.md`

## Summary

DuckingDelay is a Layer 4 user feature that automatically reduces delay output when input signal is present. It composes the existing FlexibleFeedbackNetwork (Layer 3) with DuckingProcessor (Layer 2) to create sidechain-triggered gain reduction on delay output, feedback path, or both. The architecture follows the established ShimmerDelay/FreezeMode pattern with external processor composition rather than FFN modification.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: FlexibleFeedbackNetwork (Layer 3), DuckingProcessor (Layer 2), OnePoleSmoother (Layer 1)
**Storage**: N/A (real-time audio processing)
**Testing**: Catch2 v3 (test-first per Constitution Principle XII)
**Target Platform**: Windows/macOS/Linux (VST3 plugin)
**Project Type**: Single project - header-only DSP with layered architecture
**Performance Goals**: <1% additional CPU overhead at 44.1kHz stereo (SC-005)
**Constraints**: Zero-latency envelope follower (SC-006), click-free transitions (SC-004)
**Scale/Scope**: Single Layer 4 feature composing existing Layer 2-3 components

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Additional Checks:**

- [x] **Principle IX (Layered Architecture)**: DuckingDelay at Layer 4 composes only lower layers
- [x] **Principle VI (Real-Time Safety)**: No allocations in process() - all buffers pre-allocated in prepare()
- [x] **Principle XV (Honest Completion)**: Compliance table in spec.md will be filled with evidence

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| DuckingDelay | `grep -r "class DuckingDelay" src/` | No | Create New |
| DuckTarget | `grep -r "enum.*DuckTarget" src/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| percentToDepth | `grep -r "percentToDepth" src/` | No | N/A | Create as private member |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DuckingProcessor | src/dsp/processors/ducking_processor.h | 2 | Core ducking logic - instantiate 2 (output + feedback) |
| FlexibleFeedbackNetwork | src/dsp/systems/flexible_feedback_network.h | 3 | Delay engine with feedback and filter |
| OnePoleSmoother | src/dsp/primitives/smoother.h | 1 | Parameter smoothing (dry/wet, output gain, delay time) |
| BlockContext | src/dsp/core/block_context.h | 0 | Tempo sync for delay time |
| dbToGain | src/dsp/core/db_utils.h | 0 | Output gain conversion |
| clamp | Standard library | N/A | Parameter range enforcement |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No DuckingDelay or DuckTarget types
- [x] `src/dsp/core/` - No conflicting utilities
- [x] `src/dsp/features/` - No existing ducking delay feature
- [x] `ARCHITECTURE.md` - DuckingProcessor documented at Layer 2

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: DuckingDelay and DuckTarget are unique names not found in codebase. All core ducking logic already exists in DuckingProcessor - we're composing it, not duplicating it.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| DuckingProcessor | prepare | `void prepare(double sampleRate, std::size_t maxBlockSize) noexcept` | Yes |
| DuckingProcessor | reset | `void reset() noexcept` | Yes |
| DuckingProcessor | processSample | `[[nodiscard]] float processSample(float main, float sidechain) noexcept` | Yes |
| DuckingProcessor | setThreshold | `void setThreshold(float dB) noexcept` | Yes |
| DuckingProcessor | setDepth | `void setDepth(float dB) noexcept` | Yes |
| DuckingProcessor | setAttackTime | `void setAttackTime(float ms) noexcept` | Yes |
| DuckingProcessor | setReleaseTime | `void setReleaseTime(float ms) noexcept` | Yes |
| DuckingProcessor | setHoldTime | `void setHoldTime(float ms) noexcept` | Yes |
| DuckingProcessor | setSidechainFilterEnabled | `void setSidechainFilterEnabled(bool enabled) noexcept` | Yes |
| DuckingProcessor | setSidechainFilterCutoff | `void setSidechainFilterCutoff(float hz) noexcept` | Yes |
| DuckingProcessor | getCurrentGainReduction | `[[nodiscard]] float getCurrentGainReduction() const noexcept` | Yes |
| FlexibleFeedbackNetwork | prepare | `void prepare(double sampleRate, std::size_t maxBlockSize) noexcept` | Yes |
| FlexibleFeedbackNetwork | reset | `void reset() noexcept` | Yes |
| FlexibleFeedbackNetwork | process | `void process(float* left, float* right, std::size_t numSamples, const BlockContext& ctx) noexcept` | Yes |
| FlexibleFeedbackNetwork | setDelayTimeMs | `void setDelayTimeMs(float ms) noexcept` | Yes |
| FlexibleFeedbackNetwork | setFeedbackAmount | `void setFeedbackAmount(float amount) noexcept` | Yes |
| FlexibleFeedbackNetwork | setFilterEnabled | `void setFilterEnabled(bool enabled) noexcept` | Yes |
| FlexibleFeedbackNetwork | setFilterType | `void setFilterType(FilterType type) noexcept` | Yes |
| FlexibleFeedbackNetwork | setFilterCutoff | `void setFilterCutoff(float hz) noexcept` | Yes |
| FlexibleFeedbackNetwork | getLatencySamples | `[[nodiscard]] std::size_t getLatencySamples() const noexcept` | Yes |
| OnePoleSmoother | prepare | `void prepare(double sampleRate, float timeMs) noexcept` | Yes |
| OnePoleSmoother | setTarget | `void setTarget(float value) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| BlockContext | sampleRate | `double sampleRate = 44100.0` | Yes |
| BlockContext | tempoBPM | `double tempoBPM = 120.0` | Yes |
| BlockContext | isPlaying | `bool isPlaying = false` | Yes |

### Header Files Read

- [x] `src/dsp/processors/ducking_processor.h` - Full API verified
- [x] `src/dsp/systems/flexible_feedback_network.h` - Full API verified
- [x] `src/dsp/primitives/smoother.h` - OnePoleSmoother API verified
- [x] `src/dsp/core/block_context.h` - BlockContext struct verified

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| DuckingProcessor | Uses `setDepth(dB)` not percentage | Convert percent to dB: `-48 * (percent/100)` |
| DuckingProcessor | Depth range is -48 to 0 (dB, negative values) | 0% duck = 0dB, 100% duck = -48dB |
| BlockContext | Member is `tempoBPM` not `tempo` | `ctx.tempoBPM` |
| FlexibleFeedbackNetwork | Feedback amount 0-1.2 not percentage | Convert: `feedbackPercent / 100.0f` |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| (none) | N/A | N/A | N/A |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| percentToDepth | Feature-specific mapping, only used by DuckingDelay |

**Decision**: No new Layer 0 utilities needed. The percent-to-depth conversion is feature-specific and belongs in DuckingDelay as a private helper.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 4 - User Features

**Related features at same layer** (from ROADMAP.md):
- TapeDelay: Analog tape emulation
- BBDDelay: Bucket-brigade emulation
- ShimmerDelay: Pitch-shifted feedback
- ReverseDelay: Reverse playback
- FreezeMode: Infinite hold

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| DuckTarget enum | LOW | Only DuckingDelay | Keep local |
| Duck parameter mapping | LOW | Only DuckingDelay | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | Ducking is unique modifier concept - other delays don't need it |
| Keep DuckTarget local | Enum specific to this feature's target selection |

### Review Trigger

After implementing next delay feature with "modifier" concept:
- [ ] Does new feature need similar target selection? -> Consider shared enum pattern
- [ ] Any duplicated composition patterns? -> Document shared pattern

## Project Structure

### Documentation (this feature)

```text
specs/032-ducking-delay/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Research findings
├── data-model.md        # Class definitions and signal flow
└── tasks.md             # Implementation tasks (via /speckit.tasks)
```

### Source Code (repository root)

```text
src/dsp/
├── core/                    # Layer 0: Core utilities (REUSE)
│   ├── db_utils.h           #   dbToGain for output gain
│   └── block_context.h      #   BlockContext for tempo sync
├── primitives/              # Layer 1: DSP primitives (REUSE)
│   └── smoother.h           #   OnePoleSmoother for parameters
├── processors/              # Layer 2: DSP processors (REUSE)
│   └── ducking_processor.h  #   Core ducking implementation
├── systems/                 # Layer 3: System components (REUSE)
│   └── flexible_feedback_network.h  # Delay engine
└── features/                # Layer 4: User features (CREATE)
    └── ducking_delay.h      #   NEW: DuckingDelay class

tests/unit/
└── features/
    └── ducking_delay_test.cpp  # NEW: Test file
```

**Structure Decision**: Single header file at Layer 4 (`ducking_delay.h`) composing existing Layer 2-3 components. One test file following existing pattern.

## Complexity Tracking

No constitution violations - standard Layer 4 feature composition.

## Implementation Phases

### Phase 1: Core Infrastructure
1. Create DuckingDelay class skeleton with DuckTarget enum
2. Implement prepare/reset/lifecycle methods
3. Wire up FlexibleFeedbackNetwork for delay functionality
4. Wire up DuckingProcessor instances (2x: output + feedback)

### Phase 2: Parameter Control
1. Implement all ducking parameters (threshold, amount, attack, release, hold)
2. Implement sidechain filter controls
3. Implement delay parameters (time, feedback, filter)
4. Add parameter smoothing for zipper-free changes

### Phase 3: Target Selection Logic
1. Implement Output Only mode (duck after FFN, before mix)
2. Implement Feedback Only mode (copy unducked, duck for feedback)
3. Implement Both mode (duck both paths)
4. Test all three modes independently

### Phase 4: Processing and Output
1. Implement main process() method with all routing
2. Implement dry/wet mixing
3. Implement output gain
4. Implement gain reduction metering

### Phase 5: Integration and Testing
1. Test basic ducking (US1 scenarios)
2. Test feedback path ducking (US2 scenarios)
3. Test sidechain filtering (US3 scenarios)
4. Test smooth transitions (US4 scenarios)
5. Verify all FR-xxx requirements
6. Verify all SC-xxx success criteria

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| DuckingProcessor API mismatch | Low | Medium | API signatures verified above |
| FFN feedback timing issues | Low | Medium | Follow FreezeMode pattern |
| Click artifacts on transitions | Medium | High | DuckingProcessor has 5ms gain smoother built-in |
| CPU overhead exceeds 1% | Low | Medium | DuckingProcessor already optimized |

## Success Metrics

From spec.md:
- SC-001: Ducking engages within attack time
- SC-002: Ducking releases within release time after hold
- SC-003: At 100% duck amount, -48dB attenuation
- SC-004: Click-free transitions
- SC-005: <1% CPU overhead at 44.1kHz stereo
- SC-006: Zero-latency envelope follower
- SC-007: Smoothed parameter changes
- SC-008: Works at 44.1kHz to 192kHz

## Next Steps

Run `/speckit.tasks` to generate detailed implementation tasks based on this plan.
