# Implementation Plan: Filter Step Sequencer

**Branch**: `098-filter-step-sequencer` | **Date**: 2026-01-25 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/098-filter-step-sequencer/spec.md`

## Summary

Implement a 16-step filter parameter sequencer synchronized to host tempo. The FilterStepSequencer (Layer 3 System) composes the existing SVF filter with parameter smoothing to create rhythmic filter sweeps. Steps control cutoff, Q, filter type, and gain. Features include multiple playback directions (Forward, Backward, PingPong, Random), swing timing, per-step glide, and gate length control.

## Technical Context

**Language/Version**: C++20 (as per constitution)
**Primary Dependencies**: SVF (Layer 1), OnePoleSmoother (Layer 1), NoteValue/NoteModifier (Layer 0), BlockContext (Layer 0)
**Storage**: N/A (runtime state only)
**Testing**: Catch2 (as per constitution)
**Target Platform**: Windows, macOS, Linux (cross-platform)
**Project Type**: Single (DSP library component)
**Performance Goals**: < 0.5% CPU single core @ 48kHz (per SC-007)
**Constraints**: Zero allocations in process(), noexcept processing, real-time safe
**Scale/Scope**: Layer 3 system component in KrateDSP library

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-Design Check (PASSED)

**Required Check - Principle II (Real-Time Safety):**
- [x] No memory allocation in process methods
- [x] All processing methods will be noexcept
- [x] No locks, mutexes, or blocking primitives in audio path
- [x] No exception handling in audio path
- [x] Pre-allocated state for all 16 steps

**Required Check - Principle IX (Layered Architecture):**
- [x] Layer 3 system - can use Layers 0-2
- [x] SVF (Layer 1 primitive) - dependency approved
- [x] OnePoleSmoother (Layer 1 primitive) - dependency approved
- [x] NoteValue/BlockContext (Layer 0 core) - dependency approved

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

### Post-Design Check (PASSED - 2026-01-25)

**Principle II (Real-Time Safety) - Verified in data-model.md:**
- [x] LinearRamp used for glide (process() is noexcept, zero allocation)
- [x] SVF used for filtering (process() is noexcept, zero allocation)
- [x] Fixed array steps_[16] - no dynamic allocation
- [x] xorshift PRNG for random - no syscalls or allocation

**Principle III (Modern C++):**
- [x] std::array for fixed-size step storage
- [x] enum class for Direction type
- [x] [[nodiscard]] on getter methods
- [x] noexcept on all processing methods

**Principle IX (Layered Architecture) - Verified in data-model.md:**
- [x] Only Layer 0-1 dependencies in FilterStepSequencer
- [x] SVF (Layer 1)
- [x] LinearRamp (Layer 1)
- [x] NoteValue/BlockContext (Layer 0)
- [x] dbToGain (Layer 0)

**Principle X (DSP Processing Constraints):**
- [x] SVF handles denormal flushing internally
- [x] NaN input handling documented (reset filter, return 0)
- [x] Parameter clamping prevents invalid filter states

**Principle XVI (Honest Completion):**
- [x] All FR-xxx requirements mapped to API methods
- [x] All SC-xxx success criteria have corresponding test cases planned
- [x] No placeholder values in design
- [x] No features quietly removed

**Constitution Compliance Summary**: All applicable principles verified. Design is ready for implementation.

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: FilterStepSequencer, SequencerStep, SequencerDirection

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| FilterStepSequencer | `grep -r "class FilterStepSequencer" dsp/ plugins/` | No | Create New |
| FilterSequencer | `grep -r "class FilterSequencer" dsp/ plugins/` | No | Create New |
| StepSequencer | `grep -r "class StepSequencer" dsp/ plugins/` | No | Create New |
| SequencerStep | `grep -r "struct SequencerStep" dsp/ plugins/` | No | Create New |
| SequencerDirection | `grep -r "enum.*Direction" dsp/ plugins/` | No (only Direction in spec references) | Create New |

**Utility Functions to be created**: None - all timing utilities exist in NoteValue

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| noteToDelayMs | `grep -r "noteToDelayMs" dsp/` | Yes | note_value.h | Reuse |
| getBeatsForNote | `grep -r "getBeatsForNote" dsp/` | Yes | note_value.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| SVF | dsp/include/krate/dsp/primitives/svf.h | 1 | Core filter - direct composition |
| SVFMode | dsp/include/krate/dsp/primitives/svf.h | 1 | Filter type enum - use directly |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Glide for cutoff, Q, gain |
| LinearRamp | dsp/include/krate/dsp/primitives/smoother.h | 1 | Alternative glide (linear) |
| NoteValue | dsp/include/krate/dsp/core/note_value.h | 0 | Tempo sync note divisions |
| NoteModifier | dsp/include/krate/dsp/core/note_value.h | 0 | Dotted/triplet modifiers |
| getBeatsForNote | dsp/include/krate/dsp/core/note_value.h | 0 | Beat duration calculation |
| BlockContext | dsp/include/krate/dsp/core/block_context.h | 0 | Tempo/transport info from host |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | Per-step gain conversion |
| detail::isNaN | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN detection |
| detail::flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal prevention |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems (no sequencer exists)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 (PatternScheduler exists but different purpose)
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No existing FilterStepSequencer, FilterSequencer, or StepSequencer classes found. PatternScheduler exists but serves a different purpose (Euclidean pattern triggering for slice playback) and has different API/semantics. Our FilterStepSequencer controls filter parameters over time, not slice triggering.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| SVF | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| SVF | processBlock | `void processBlock(float* buffer, size_t numSamples) noexcept` | Yes |
| SVF | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SVF | reset | `void reset() noexcept` | Yes |
| SVF | setMode | `void setMode(SVFMode mode) noexcept` | Yes |
| SVF | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| SVF | setResonance | `void setResonance(float q) noexcept` | Yes |
| SVF | setGain | `void setGain(float dB) noexcept` | Yes |
| SVF | kMinCutoff | `static constexpr float kMinCutoff = 1.0f` | Yes |
| SVF | kMaxCutoffRatio | `static constexpr float kMaxCutoffRatio = 0.495f` | Yes |
| SVF | kMinQ | `static constexpr float kMinQ = 0.1f` | Yes |
| SVF | kMaxQ | `static constexpr float kMaxQ = 30.0f` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | isComplete | `[[nodiscard]] bool isComplete() const noexcept` | Yes |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| LinearRamp | configure | `void configure(float rampTimeMs, float sampleRate) noexcept` | Yes |
| LinearRamp | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| LinearRamp | process | `[[nodiscard]] float process() noexcept` | Yes |
| LinearRamp | snapTo | `void snapTo(float value) noexcept` | Yes |
| BlockContext | tempoBPM | `double tempoBPM = 120.0` | Yes |
| BlockContext | sampleRate | `double sampleRate = 44100.0` | Yes |
| BlockContext | isPlaying | `bool isPlaying = false` | Yes |
| getBeatsForNote | signature | `[[nodiscard]] inline constexpr float getBeatsForNote(NoteValue note, NoteModifier modifier = NoteModifier::None) noexcept` | Yes |
| dbToGain | signature | `[[nodiscard]] constexpr float dbToGain(float dB) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/svf.h` - SVF class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother, LinearRamp classes
- [x] `dsp/include/krate/dsp/core/note_value.h` - NoteValue, NoteModifier, getBeatsForNote
- [x] `dsp/include/krate/dsp/core/block_context.h` - BlockContext struct
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dbToGain, flushDenormal

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| BlockContext | Member is `tempoBPM` not `tempo` | `ctx.tempoBPM` |
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| SVF | Q is "resonance" in method name | `svf.setResonance(q)` |
| SVF | Gain only affects Peak/Shelf modes | Only set gain when using SVFMode::Peak |
| LinearRamp | Glide truncation requires manual handling | Calculate samples to target, adjust if > stepDuration |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

All timing utilities already exist in note_value.h. No new Layer 0 extraction needed.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| advanceStep() | Tightly coupled to sequencer state (direction, numSteps, currentStep) |
| calculateStepDuration() | Uses member state (noteValue_, modifier_, sampleRate_) |
| applySwing() | Simple formula, only one consumer |

**Decision**: No extraction to Layer 0 needed. All timing utilities exist. Step advancement and swing logic are specific to this sequencer's state machine.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 3 - System Components

**Related features at same layer** (from FLT-ROADMAP.md):
- Phase 17.2: Filter Arpeggiator (may share step data structures)
- Phase 17.3: Multi-Parameter Sequencer (may generalize sequencer concept)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| SequencerStep struct | HIGH | Filter Arpeggiator, Multi-Param Sequencer | Keep in FilterStepSequencer for now; extract after 2nd use |
| Direction enum | HIGH | Filter Arpeggiator, any rhythmic effect | Keep local; extract if reused |
| Swing calculation | MEDIUM | Any tempo-synced effect | Keep as member; formula is trivial |
| PingPong state machine | MEDIUM | Pattern generators | Keep local; behavior may vary |

### Detailed Analysis (for HIGH potential items)

**SequencerStep struct** provides:
- Cutoff frequency (20Hz-20kHz)
- Q/resonance (0.5-20.0)
- Filter type (SVFMode)
- Gain in dB (-24 to +12)

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Filter Arpeggiator | MAYBE | May need pitch/note data instead of filter params |
| Multi-Param Sequencer | YES | Would template/generalize the step struct |

**Recommendation**: Keep SequencerStep local to this feature. If Filter Arpeggiator needs similar structure, extract to common header with template or variant approach.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | First sequencer at this layer - patterns not established |
| Keep step struct local | Only one consumer; Multi-Param Sequencer would likely use different approach (generic parameter slots) |
| Direction enum local | May need different semantics for other effects |

### Review Trigger

After implementing **Filter Arpeggiator**, review this section:
- [ ] Does Arpeggiator need SequencerStep or similar? -> Extract to shared location
- [ ] Does Arpeggiator use same Direction enum? -> Extract Direction to common header
- [ ] Any duplicated timing/swing code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/098-filter-step-sequencer/
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
│   └── systems/
│       └── filter_step_sequencer.h   # Header-only Layer 3 system
└── tests/
    └── unit/systems/
        └── filter_step_sequencer_tests.cpp  # Catch2 unit tests
```

**Structure Decision**: Single header in `dsp/include/krate/dsp/systems/` following existing Layer 3 patterns (FilterFeedbackMatrix, DistortionRack). Tests in `dsp/tests/unit/systems/`.

## Architecture Design

### Component Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                    FilterStepSequencer (Layer 3)                │
├─────────────────────────────────────────────────────────────────┤
│  State:                                                         │
│  ┌─────────────┐  ┌─────────────┐                              │
│  │ steps_[16]  │  │ Timing      │                              │
│  │ SequencerStep│  │ - noteValue │                              │
│  │ - cutoffHz  │  │ - modifier  │                              │
│  │ - q         │  │ - swing %   │                              │
│  │ - type      │  │ - glideMs   │                              │
│  │ - gainDb    │  │ - gate %    │                              │
│  └─────────────┘  └─────────────┘                              │
│                                                                 │
│  Processing:                                                    │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ Per-sample flow:                                         │   │
│  │                                                          │   │
│  │ BlockContext -> Timing Engine -> Step Selection          │   │
│  │                      │                                   │   │
│  │                      v                                   │   │
│  │              ┌───────────────┐                           │   │
│  │              │ Smoothers     │ (cutoff, Q, gain)         │   │
│  │              │ LinearRamp x3 │ for glide                 │   │
│  │              └───────┬───────┘                           │   │
│  │                      │                                   │   │
│  │                      v                                   │   │
│  │              ┌───────────────┐                           │   │
│  │              │     SVF       │ (filter processing)       │   │
│  │              │  (Layer 1)    │                           │   │
│  │              └───────┬───────┘                           │   │
│  │                      │                                   │   │
│  │                      v                                   │   │
│  │              ┌───────────────┐                           │   │
│  │              │ Gate Crossfade│ (5ms wet/dry fade)        │   │
│  │              └───────┬───────┘                           │   │
│  │                      │                                   │   │
│  │                      v                                   │   │
│  │                   Output                                 │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

### State Machine

```
                    ┌───────────────────────────────────────┐
                    │         Direction Mode                 │
                    └───────────────────────────────────────┘
                              │
          ┌───────────────────┼───────────────────┬──────────────────┐
          │                   │                   │                  │
          v                   v                   v                  v
    ┌──────────┐       ┌──────────┐       ┌──────────┐       ┌──────────┐
    │ Forward  │       │ Backward │       │ PingPong │       │ Random   │
    │ 0->1->...│       │ N-1->...0│       │ bounce   │       │ no repeat│
    └──────────┘       └──────────┘       └──────────┘       └──────────┘

    PingPong Detail (4 steps):
    ┌───┐   ┌───┐   ┌───┐   ┌───┐   ┌───┐   ┌───┐   ┌───┐
    │ 0 │ → │ 1 │ → │ 2 │ → │ 3 │ → │ 2 │ → │ 1 │ → │ 0 │ → ...
    └───┘   └───┘   └───┘   └───┘   └───┘   └───┘   └───┘
    (endpoints visited once per cycle)
```

### Glide Truncation Strategy

```
Case 1: stepDuration >= glideTime (normal)
├───────────────────────────────────────────┤ step duration
├─────────────────────┤                      glide (reaches target mid-step)
                      ├─────────────────────┤ hold at target

Case 2: stepDuration < glideTime (truncated)
├───────────────────────────────────────────┤ step duration
├─────────────────────|                      glide (truncated at boundary)
                      ^ reaches target exactly here
```

**Implementation**: When setting new target, calculate remaining samples to step boundary. If glide would exceed, use LinearRamp with adjusted rate to reach target exactly at boundary.

### Gate Crossfade Strategy

```
┌────────────────────────────────────────────────────────────┐
│                         Step Duration                      │
├───────────────────────────────────────────┬────────────────┤
│           Active Filter (gateLength %)    │    Bypass      │
│                                           │                │
│ ├─────────────────────────┬───────────────┤                │
│                           │<--- 5ms --->│ │                │
│                           │  crossfade   │ │                │
│                           │   wet→dry    │ │                │
└───────────────────────────────────────────┴────────────────┘

Signal flow during crossfade:
  output = wet * fadeGain + dry * (1 - fadeGain)
  fadeGain ramps from 1.0 → 0.0 over 5ms (220 samples @ 44.1kHz)
```

## Risk Assessment

### Technical Risks

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Glide truncation drift | Medium | Medium | Use exact sample-counting; test with fast tempo + slow glide |
| Click on filter type change | Low | High | SVF is designed for parameter modulation; preserve filter state |
| PingPong off-by-one | Medium | Low | Comprehensive unit tests for boundary conditions |
| Random repetition prevention | Low | Low | Simple comparison with previous step |
| Performance at extreme settings | Low | Medium | Benchmark with 32 notes/beat at 300 BPM |

### Dependency Risks

| Dependency | Risk | Mitigation |
|------------|------|------------|
| SVF | None - stable, well-tested | N/A |
| LinearRamp | None - stable | N/A |
| BlockContext | None - stable API | N/A |

### Integration Risks

| Integration Point | Risk | Mitigation |
|-------------------|------|------------|
| Plugin parameter mapping | Medium | Document parameter ranges clearly in quickstart |
| Host tempo changes | Low | Test with tempo automation |
| PPQ sync during seek | Medium | Test sync() with various PPQ positions |

## Test Strategy

### Test Categories

1. **Lifecycle Tests** (prepare, reset, isPrepared)
2. **Step Configuration Tests** (16 steps, parameter ranges, defaults)
3. **Timing Tests** (step duration accuracy, tempo sync, swing)
4. **Direction Tests** (Forward, Backward, PingPong, Random)
5. **Glide Tests** (smooth transitions, truncation, filter type instant change)
6. **Gate Tests** (gate length, crossfade, no clicks)
7. **Processing Tests** (sample/block processing, NaN handling)
8. **PPQ Sync Tests** (transport lock)
9. **Performance Tests** (CPU budget)

### Key Test Cases (from spec)

| SC | Test Description | Acceptance Criteria |
|----|------------------|---------------------|
| SC-001 | Step timing accuracy | < 1ms deviation at 120 BPM / 1/4 notes |
| SC-002 | Glide completion | Within 1% of specified time; truncation reaches target at boundary |
| SC-003 | No clicks on cutoff glide | Zero peaks > 0.5 in diff between samples |
| SC-004 | Swing ratio | 2.9:1 to 3.1:1 at 50% swing |
| SC-005 | Step recall | All 16 steps preserved after prepare()/reset() |
| SC-006 | Random fairness | All N steps visited within 10*N iterations, no immediate repeats |
| SC-007 | CPU budget | < 0.5% @ 48kHz for 1 second |
| SC-008 | PPQ sync accuracy | Within 1 sample of correct position |
| SC-009 | Gate crossfade | No clicks (peak diff < 0.1 during transition) |
| SC-010 | Per-step gain accuracy | Within 0.1dB of specified |

## Complexity Tracking

No constitution violations. All design decisions align with principles.

## Phase Completion Status

### Phase 0: Research (COMPLETE)
- [x] research.md generated with all technical decisions documented
- [x] All NEEDS CLARIFICATION items resolved
- [x] Glide strategy: LinearRamp with truncation
- [x] Swing formula: (1+swing)/(1-swing) for 3:1 at 50%
- [x] PingPong state machine designed
- [x] Random mode rejection sampling
- [x] Gate crossfade strategy (5ms linear)
- [x] PPQ sync implementation

### Phase 1: Design (COMPLETE)
- [x] data-model.md generated with all entities
- [x] contracts/api.md generated with full API
- [x] quickstart.md generated with usage examples
- [x] Agent context updated via update-agent-context.ps1
- [x] Post-design constitution check passed

### Phase 2: Task Breakdown (PENDING)
- [ ] Generate tasks.md via /speckit.tasks command
- [ ] Implementation ready to begin after task breakdown
