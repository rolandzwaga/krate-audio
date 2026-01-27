# Implementation Plan: VowelSequencer with SequencerCore Refactor

**Branch**: `099-vowel-sequencer` | **Date**: 2026-01-25 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/099-vowel-sequencer/spec.md`

## Summary

Extract ~160 lines of timing, direction, swing, transport sync, and gate logic from FilterStepSequencer into a reusable **SequencerCore** (Layer 1 primitive). Then create **VowelSequencer** (Layer 3 system) that composes SequencerCore with FormantFilter for rhythmic vowel effects. Finally, refactor FilterStepSequencer to use SequencerCore, maintaining backward compatibility and passing all 33 existing tests.

## Technical Context

**Language/Version**: C++20 (targeting C++17 compatibility)
**Primary Dependencies**:
- FormantFilter (Layer 2) - vowel sound generation via 3 parallel bandpass filters
- LinearRamp (Layer 1) - morph time and gate crossfade smoothing
- NoteValue/NoteModifier (Layer 0) - tempo sync timing calculations
- SVF (Layer 1) - used by FilterStepSequencer for filtering
- BlockContext (Layer 0) - host tempo information

**Storage**: N/A (DSP only, no persistence)
**Testing**: Catch2 (existing test framework)
**Target Platform**: Windows, macOS, Linux (cross-platform VST3)
**Project Type**: DSP library (monorepo structure)
**Performance Goals**:
- SequencerCore < 0.1% CPU single core @ 44.1kHz (Layer 1 budget)
- VowelSequencer + FormantFilter < 1% CPU single core @ 48kHz (SC-007)

**Constraints**:
- Zero allocations in tick()/process() methods (real-time safety)
- All process methods must be noexcept
- Backward compatibility for FilterStepSequencer public API

**Scale/Scope**:
- SequencerCore: ~200 lines extracted/refactored code
- VowelSequencer: ~300 lines new code
- FilterStepSequencer refactor: ~150 lines modified

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check:**

- [x] **Principle II (Real-Time Safety)**: All new components will be noexcept with zero allocations in process paths
- [x] **Principle IX (Layered Architecture)**: SequencerCore at Layer 1 (primitives), VowelSequencer at Layer 3 (systems)
- [x] **Principle XII (Test-First Development)**: Tests written before implementation for each component
- [x] **Principle XIV (ODR Prevention)**: Searches performed, no existing SequencerCore or VowelSequencer found
- [x] **Principle XV (Pre-Implementation Research)**: All dependencies verified in codebase

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

**Classes/Structs to be created**: SequencerCore, VowelStep, VowelSequencer

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| SequencerCore | `grep -r "class SequencerCore" dsp/ plugins/` | No | Create New (Layer 1) |
| VowelStep | `grep -r "struct VowelStep" dsp/ plugins/` | No | Create New |
| VowelSequencer | `grep -r "class VowelSequencer" dsp/ plugins/` | No | Create New (Layer 3) |
| Direction | `grep -r "enum class Direction" dsp/ plugins/` | Yes (filter_step_sequencer.h) | Move to SequencerCore |

**Utility Functions to be created**: None (all timing logic extracted from FilterStepSequencer)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| getBeatsForNote | `grep -r "getBeatsForNote" dsp/` | Yes | note_value.h | Reuse |
| applySwingToStep | in FilterStepSequencer | Yes | filter_step_sequencer.h | Extract to SequencerCore |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| FormantFilter | `dsp/include/krate/dsp/processors/formant_filter.h` | 2 | Vowel sound generation in VowelSequencer |
| LinearRamp | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Morph time and gate crossfade in VowelSequencer |
| NoteValue/NoteModifier | `dsp/include/krate/dsp/core/note_value.h` | 0 | Tempo sync timing in SequencerCore |
| getBeatsForNote() | `dsp/include/krate/dsp/core/note_value.h` | 0 | Step duration calculation |
| Vowel enum | `dsp/include/krate/dsp/core/filter_tables.h` | 0 | Vowel selection per step |
| BlockContext | `dsp/include/krate/dsp/core/block_context.h` | 0 | Tempo/transport info |
| SVF | `dsp/include/krate/dsp/primitives/svf.h` | 1 | FilterStepSequencer (unchanged) |
| Direction enum | `dsp/include/krate/dsp/systems/filter_step_sequencer.h` | 3 | Extract and move to SequencerCore (Layer 1) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 DSP processors
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 system components
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (SequencerCore, VowelStep, VowelSequencer) are unique and not found in codebase. Direction enum will be moved (not duplicated) from filter_step_sequencer.h to sequencer_core.h with a using directive for backward compatibility.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| FormantFilter | setVowelMorph | `void setVowelMorph(float position) noexcept` | Yes |
| FormantFilter | setFormantShift | `void setFormantShift(float semitones) noexcept` | Yes |
| FormantFilter | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| FormantFilter | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| FormantFilter | reset | `void reset() noexcept` | Yes |
| FormantFilter | setSmoothingTime | `void setSmoothingTime(float ms) noexcept` | Yes |
| LinearRamp | configure | `void configure(float rampTimeMs, float sampleRate) noexcept` | Yes |
| LinearRamp | setTarget | `void setTarget(float target) noexcept` | Yes |
| LinearRamp | process | `[[nodiscard]] float process() noexcept` | Yes |
| LinearRamp | snapTo | `void snapTo(float value) noexcept` | Yes |
| LinearRamp | reset | `void reset() noexcept` | Yes |
| BlockContext | tempoBPM | `double tempoBPM = 120.0` | Yes |
| BlockContext | isPlaying | `bool isPlaying = false` | Yes |
| getBeatsForNote | - | `[[nodiscard]] inline constexpr float getBeatsForNote(NoteValue note, NoteModifier modifier = NoteModifier::None) noexcept` | Yes |
| Vowel | enum values | `enum class Vowel : uint8_t { A = 0, E = 1, I = 2, O = 3, U = 4 }` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/formant_filter.h` - FormantFilter class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - LinearRamp class
- [x] `dsp/include/krate/dsp/core/note_value.h` - NoteValue, NoteModifier, getBeatsForNote
- [x] `dsp/include/krate/dsp/core/filter_tables.h` - Vowel enum
- [x] `dsp/include/krate/dsp/core/block_context.h` - BlockContext struct
- [x] `dsp/include/krate/dsp/systems/filter_step_sequencer.h` - Direction enum, timing logic

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| BlockContext | Member is `tempoBPM` not `tempo` | `ctx.tempoBPM` |
| FormantFilter | setVowelMorph uses 0-4 range (A=0, E=1, I=2, O=3, U=4) | `filter.setVowelMorph(static_cast<float>(vowelIndex))` |
| LinearRamp | configure() must be called before setTarget() for correct ramp times | Call `configure()` in `prepare()` |
| Vowel | Cast to float for morphing: A=0.0, E=1.0, I=2.0, O=3.0, U=4.0 | `static_cast<float>(static_cast<uint8_t>(vowel))` |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| Direction enum | Used by multiple sequencer types | SequencerCore (Layer 1) | FilterStepSequencer, VowelSequencer, future sequencers |
| - | - | - | - |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| applySwingToStep | SequencerCore-specific, encapsulates swing calculation with internal state |
| calculateNextStep | SequencerCore-specific, uses direction and PRNG state |
| calculatePingPongStep | SequencerCore-specific for PPQ sync |

**Decision**: Direction enum will be defined in sequencer_core.h (Layer 1). All timing logic extracted from FilterStepSequencer will remain in SequencerCore as member functions.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 1 (SequencerCore) and Layer 3 (VowelSequencer)

**Related features at same layer**:
- FilterStepSequencer (Layer 3) - existing, will use SequencerCore
- Future ArpeggiatorSequencer (Layer 3) - may use SequencerCore
- Future MultiParameterSequencer (Layer 3) - may use SequencerCore

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| SequencerCore | HIGH | FilterStepSequencer, VowelSequencer, future sequencers | Extract now (this is the primary goal) |
| VowelStep struct | LOW | VowelSequencer only | Keep in VowelSequencer |
| Direction enum | HIGH | All sequencer-based effects | Move to SequencerCore |

### Detailed Analysis (for HIGH potential items)

**SequencerCore** provides:
- Step timing calculation with tempo sync (NoteValue/NoteModifier)
- Swing timing (even steps longer, odd steps shorter)
- Direction modes (Forward, Backward, PingPong, Random)
- PPQ transport sync
- Gate length control with crossfade
- Manual trigger functionality
- tick() method returning step change events

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| FilterStepSequencer | YES | Primary refactor target |
| VowelSequencer | YES | New feature, compose SequencerCore + FormantFilter |
| Future ArpeggiatorSequencer | YES | Same timing/direction needs |

**Recommendation**: Extract to Layer 1 primitive now. This is the explicit goal of the feature.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| SequencerCore at Layer 1 | Must be usable by multiple Layer 3 systems |
| Direction enum in SequencerCore | Shared by all sequencer types |
| Backward compat via using directive | FilterStepSequencer can still use `Direction::Forward` after refactor |
| `previousStep_` internal to SequencerCore | Only used for Random no-repeat logic; consumers (FilterStepSequencer, VowelSequencer) track their own previous state for morph/glide purposes since their "previous" may differ from timing's "previous" |
| VowelSequencer owns its own `gateRamp_` | Separate from SequencerCore's `isGateActive()` because VowelSequencer needs to apply its bypass-safe formula (`wet * ramp + dry`) rather than just using a boolean gate state |

### Review Trigger

After implementing VowelSequencer, review this section:
- [x] Does FilterStepSequencer need SequencerCore? -> Yes, refactor required
- [ ] Does future ArpeggiatorSequencer use same pattern? -> Review when implemented
- [ ] Any duplicated code? -> Will be eliminated by SequencerCore extraction

## Project Structure

### Documentation (this feature)

```text
specs/099-vowel-sequencer/
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
│   ├── primitives/
│   │   └── sequencer_core.h       # NEW: Layer 1 - timing/direction logic
│   └── systems/
│       ├── filter_step_sequencer.h  # MODIFIED: Use SequencerCore
│       └── vowel_sequencer.h        # NEW: Layer 3 - vowel stepping
└── tests/
    ├── unit/
    │   ├── primitives/
    │   │   └── sequencer_core_tests.cpp  # NEW: SequencerCore tests
    │   └── systems/
    │       ├── filter_step_sequencer_tests.cpp  # EXISTING: Must pass
    │       └── vowel_sequencer_tests.cpp  # NEW: VowelSequencer tests
```

**Structure Decision**: Standard DSP library structure with header-only implementations in `include/` and tests in `tests/unit/`.

## Architecture Overview

### Component Diagram

```
                    +-----------------------+
                    |   Layer 0 (Core)      |
                    |  - NoteValue          |
                    |  - NoteModifier       |
                    |  - getBeatsForNote()  |
                    |  - Vowel              |
                    |  - BlockContext       |
                    +-----------------------+
                              |
                              v
+------------------------------------------------------------+
|                    Layer 1 (Primitives)                     |
|                                                             |
|  +-------------------+     +-------------+     +----------+ |
|  |  SequencerCore    |     | LinearRamp  |     |   SVF    | |
|  |  - timing         |     | (existing)  |     |(existing)| |
|  |  - direction      |     +-------------+     +----------+ |
|  |  - swing          |                                      |
|  |  - transport sync |                                      |
|  |  - gate           |                                      |
|  +-------------------+                                      |
+------------------------------------------------------------+
              |                           |
              v                           v
+------------------------------------------------------------+
|                    Layer 2 (Processors)                     |
|                                                             |
|  +-------------------+                                      |
|  |  FormantFilter    |                                      |
|  |  (existing)       |                                      |
|  +-------------------+                                      |
+------------------------------------------------------------+
              |                           |
              v                           v
+------------------------------------------------------------+
|                    Layer 3 (Systems)                        |
|                                                             |
|  +-------------------+     +---------------------+          |
|  | VowelSequencer    |     | FilterStepSequencer |          |
|  | - SequencerCore   |     | - SequencerCore     |          |
|  | - FormantFilter   |     | - SVF               |          |
|  | - LinearRamp x2   |     | - LinearRamp x5     |          |
|  +-------------------+     +---------------------+          |
+------------------------------------------------------------+
```

### Data Flow

**VowelSequencer Processing:**
```
Input Audio -> [FormantFilter] -> [Gate Crossfade] -> Output
                    ^                    ^
                    |                    |
              [Morph Ramp]          [Gate Ramp]
                    ^                    ^
                    |                    |
              [step vowel]         [gate active?]
                    ^                    ^
                    |                    |
             +------+--------------------+------+
             |         SequencerCore           |
             | tick() -> step change event     |
             +----------------------------------+
```

### Implementation Order

1. **SequencerCore** (Layer 1) - Extract from FilterStepSequencer
   - Direction enum
   - Timing calculations (tempo, note value, swing)
   - Step advancement logic (forward, backward, pingpong, random)
   - Transport sync (PPQ)
   - Gate length with crossfade
   - tick() method returning step events

2. **VowelSequencer** (Layer 3) - New implementation
   - VowelStep struct (vowel, formantShift)
   - Compose SequencerCore for timing
   - Compose FormantFilter for sound
   - Compose LinearRamp for morph transitions
   - setPreset() for built-in patterns

3. **FilterStepSequencer Refactor** (Layer 3) - Update existing
   - Replace inline timing logic with SequencerCore composition
   - Maintain exact same public API
   - Ensure all 33 tests pass

## Risk Mitigation

### Refactor Risk: FilterStepSequencer Backward Compatibility

**Risk**: Breaking existing FilterStepSequencer behavior during refactor.

**Mitigation**:
1. Run all 33 existing tests (181 assertions) before starting refactor
2. Extract SequencerCore logic incrementally, running tests after each step
3. Direction enum kept accessible via `using Direction = SequencerCore::Direction;`
4. Public API remains identical - only internal composition changes

### Risk: Timing Accuracy Degradation

**Risk**: Extracted SequencerCore timing may diverge from FilterStepSequencer behavior.

**Mitigation**:
1. Copy exact timing formulas from FilterStepSequencer
2. Add explicit timing accuracy tests to SequencerCore (SC-001 equivalent)
3. Cross-check swing ratio tests (SC-004) against existing implementation

### Risk: Gate Crossfade Behavior Change

**Risk**: Gate bypass behavior differs from spec (dry at unity, wet fades out).

**Mitigation**:
1. Spec clarification already resolved: "dry passes through continuously at full level"
2. VowelSequencer gate crossfade: `output = wet * gateRamp + input` (dry always unity)
3. Compare with FilterStepSequencer which uses: `output = wet * gateGain + input * (1 - gateGain)`

**Note**: VowelSequencer gate spec differs from FilterStepSequencer. VowelSequencer dry is always unity; FilterStepSequencer crossfades between wet/dry. This is intentional per spec clarification.

## Complexity Tracking

> No Constitution Check violations requiring justification.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| - | - | - |

## Test Strategy

### SequencerCore Tests (New)

| Test Category | Tests | Assertions | Priority |
|---------------|-------|------------|----------|
| Lifecycle | prepare, reset, isPrepared | ~10 | P1 |
| Timing | step duration accuracy (SC-001 equivalent) | ~15 | P1 |
| Direction | Forward, Backward, PingPong, Random | ~20 | P1 |
| Swing | ratio at 50%, edge cases | ~10 | P2 |
| Transport | PPQ sync (SC-008 equivalent) | ~10 | P3 |
| Gate | length control, crossfade behavior | ~10 | P3 |
| **Total** | | ~75 | |

### VowelSequencer Tests (New)

| Test Category | Tests | Assertions | Priority |
|---------------|-------|------------|----------|
| Lifecycle | prepare, reset, defaults | ~10 | P1 |
| Vowel Pattern | step through AEIOU, palindrome default | ~15 | P1 |
| Morph | smooth transitions (SC-002, SC-003) | ~15 | P2 |
| Direction | inherited from SequencerCore | ~10 | P2 |
| Presets | aeiou, wow, yeah, unknown | ~10 | P2 |
| Formant Shift | per-step shift, clamping | ~10 | P3 |
| Gate | dry at unity behavior | ~10 | P3 |
| Performance | CPU budget (SC-007) | ~5 | P3 |
| **Total** | | ~85 | |

### FilterStepSequencer Regression Tests (Existing)

| Test Category | Tests | Assertions | Status |
|---------------|-------|------------|--------|
| All existing | 33 | 181 | Must Pass |

## Implementation Steps

### Phase 1: SequencerCore (Layer 1)

1. Create `sequencer_core.h` with Direction enum
2. Write failing tests for tick() step advancement
3. Extract timing logic from FilterStepSequencer
4. Implement step advancement (Forward direction first)
5. Add Backward, PingPong, Random directions
6. Add swing timing
7. Add PPQ transport sync
8. Add gate length with isGateActive()
9. Verify all SequencerCore tests pass

### Phase 2: VowelSequencer (Layer 3)

1. Create `vowel_sequencer.h` with VowelStep struct
2. Write failing tests for basic vowel stepping
3. Compose SequencerCore for timing
4. Compose FormantFilter for sound
5. Add morph time with LinearRamp
6. Add per-step formant shift
7. Add setPreset() with built-in patterns
8. Add gate bypass behavior (dry at unity)
9. Verify all VowelSequencer tests pass

### Phase 3: FilterStepSequencer Refactor

1. Add SequencerCore as member
2. Delegate timing methods to SequencerCore
3. Remove duplicated timing logic
4. Verify all 33 existing tests pass
5. Add using directive for Direction backward compatibility

### Phase 4: Documentation & Cleanup

1. Update `specs/_architecture_/layer-1-primitives.md` with SequencerCore
2. Update `specs/_architecture_/layer-3-systems.md` with VowelSequencer
3. Final test run across all components
