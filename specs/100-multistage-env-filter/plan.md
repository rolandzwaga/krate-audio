# Implementation Plan: MultiStage Envelope Filter

**Branch**: `100-multistage-env-filter` | **Date**: 2026-01-25 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/100-multistage-env-filter/spec.md`

**Note**: This is spec #100 - a milestone for Iterum DSP development!

## Summary

MultiStageEnvelopeFilter is a Layer 2 processor providing complex, programmatic envelope shapes (beyond ADSR) driving filter movement for evolving pads and textures. It composes an SVF filter with a multi-stage envelope generator supporting up to 8 stages, each with independent target frequency, transition time, and curve shape (logarithmic/linear/exponential). The component supports looping, velocity-sensitive modulation, and independent release timing.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- `SVF` (Layer 1) - TPT State Variable Filter for filtering
- `OnePoleSmoother` (Layer 1) - Parameter smoothing for release transitions
- `flushDenormal`, `isNaN`, `isInf` (Layer 0) - Real-time safety utilities

**Storage**: N/A (stateful DSP component, no persistence)
**Testing**: Catch2 via `dsp_tests` target
**Target Platform**: Cross-platform (Windows/macOS/Linux)
**Project Type**: DSP library component (header-only)
**Performance Goals**: < 0.5% single-core CPU at 96kHz for 1024-sample block (SC-006)
**Constraints**: Zero memory allocations in process methods (real-time safe)
**Scale/Scope**: Single component with comprehensive test suite

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Gate: PASS**

**Post-Design Gate: PASS** (verified 2026-01-25)
- All design decisions comply with constitution principles
- No real-time violations in proposed architecture
- Layer dependencies correct (Layer 2 using Layer 0/1 only)
- No ODR conflicts detected

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] All process methods will be `noexcept`
- [x] No memory allocations in audio path (pre-allocated arrays)
- [x] No locks, mutexes, or blocking primitives
- [x] No exceptions thrown
- [x] Denormals will be flushed after filter processing

**Required Check - Principle III (Modern C++ Standards):**
- [x] C++20 features where appropriate (constexpr, std::array)
- [x] RAII for resource management
- [x] Value semantics for state
- [x] `[[nodiscard]]` on getters

**Required Check - Principle IX (Layered DSP Architecture):**
- [x] Layer 2 processor - depends only on Layers 0-1
- [x] No circular dependencies
- [x] Independent testability

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created (verified via grep)

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `MultiStageEnvelopeFilter`, `EnvelopeStage` (internal struct)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| MultiStageEnvelopeFilter | `grep -r "MultiStageEnvelopeFilter" dsp/ plugins/` | No | Create New |
| EnvelopeStage | `grep -r "EnvelopeStage" dsp/ plugins/` | No | Create New (internal struct) |
| FilterType enum | `grep -r "enum.*FilterType" dsp/include/krate/dsp/processors/` | Yes - in EnvelopeFilter | Define own enum or reuse SVFMode |

**Decision**: Use `SVFMode` directly from `svf.h` for filter type selection (Lowpass, Bandpass, Highpass). This avoids enum duplication and maintains consistency with other filter processors.

**Utility Functions to be created**: None - all needed utilities exist in Layer 0

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| curve interpolation | `grep -r "pow.*curve" dsp/` | No | N/A | Implement as private method |
| flushDenormal | `grep -r "flushDenormal" dsp/` | Yes | db_utils.h | Reuse |
| isNaN/isInf | `grep -r "isNaN\|isInf" dsp/` | Yes | db_utils.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| SVF | `dsp/include/krate/dsp/primitives/svf.h` | 1 | Internal filter - direct composition |
| SVFMode | `dsp/include/krate/dsp/primitives/svf.h` | 1 | Filter type enumeration |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Release phase smoothing |
| flushDenormal | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Denormal prevention |
| isNaN/isInf | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Input validation |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (EnvelopeFilter exists, different purpose)
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No existing `MultiStageEnvelopeFilter` class. The existing `EnvelopeFilter` serves a different purpose (amplitude-following auto-wah). The new component has a distinct name and non-overlapping functionality.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| SVF | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SVF | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| SVF | setResonance | `void setResonance(float q) noexcept` | Yes |
| SVF | setMode | `void setMode(SVFMode mode) noexcept` | Yes |
| SVF | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| SVF | reset | `void reset() noexcept` | Yes |
| SVF | kMinQ | `static constexpr float kMinQ = 0.1f` | Yes |
| SVF | kMaxQ | `static constexpr float kMaxQ = 30.0f` | Yes |
| SVF | kMaxCutoffRatio | `static constexpr float kMaxCutoffRatio = 0.495f` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | isComplete | `[[nodiscard]] bool isComplete() const noexcept` | Yes |
| detail::flushDenormal | - | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |
| detail::isNaN | - | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | - | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/svf.h` - SVF class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - Utility functions
- [x] `dsp/include/krate/dsp/core/grain_envelope.h` - Reference for curve patterns

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| SVF | setResonance takes Q directly, not percentage | `filter_.setResonance(q_)` where q_ is [0.1, 30.0] |
| SVF | setCutoff clamps to Nyquist internally | No need to clamp before calling |
| SVF | Returns input unchanged if not prepared | Check `isPrepared()` or always call `prepare()` |
| OnePoleSmoother | snapTo() sets both current AND target | Use for initialization |
| OnePoleSmoother | setTarget() may reset to 0 on NaN | Input validation handled internally |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| applyCurve(t, curve) | Potentially reusable for other modulation sources | curve_utils.h | MultiStageEnvelopeFilter, future LFO shapes |

**Decision**: Keep curve interpolation as a private method in MultiStageEnvelopeFilter initially. If a second consumer emerges (e.g., LFO curve shaping), extract to Layer 0.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateCurvedValue(t, curve) | Only one consumer currently, tightly coupled to envelope logic |
| msToSamples() | One-liner, class stores sampleRate_ |

**Decision**: Implement curve interpolation as private inline method. No Layer 0 extraction at this time.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from architecture docs):
- FilterStepSequencer (Layer 3 - already exists, different approach: tempo-synced discrete steps)
- Future LFO-driven filters (Layer 2 - continuous modulation)
- Future MSEG modulation (Layer 2 - similar multi-stage concept)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Multi-stage envelope generator | HIGH | MSEG modulation, other envelope-driven effects | Keep local / Extract after 2nd use |
| Curve interpolation | MEDIUM | LFO shapes, crossfade curves | Keep local / Extract after 2nd use |

### Detailed Analysis (for HIGH potential items)

**Multi-stage envelope generator** provides:
- Up to 8 configurable stages with target, time, curve
- Loop section support
- Velocity scaling
- Release phase handling

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| MSEG Modulation | YES | Same core envelope logic, different output (generic modulation vs filter) |
| Envelope-driven Pitch | MAYBE | Similar concept but different scaling needs |
| Step Sequencer | NO | Already exists, different approach (discrete steps vs continuous) |

**Recommendation**: Keep envelope logic internal to MultiStageEnvelopeFilter. If MSEG modulation is implemented, consider extracting `MultiStageEnvelope` as a Layer 1 primitive that both can compose.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared envelope primitive | First use case - wait for concrete 2nd consumer |
| Use SVFMode directly | Avoids enum duplication, consistent with other processors |
| Header-only implementation | Standard for DSP components in this codebase |

### Review Trigger

After implementing **MSEG modulation** or **envelope-driven pitch**, review this section:
- [ ] Does the new feature need similar envelope logic? -> Extract to shared primitive
- [ ] Does it use same curve interpolation? -> Extract to Layer 0
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/100-multistage-env-filter/
├── plan.md              # This file
├── research.md          # Phase 0 output (curve algorithms, state machine design)
├── data-model.md        # Phase 1 output (structs, enums, state)
├── quickstart.md        # Phase 1 output (usage examples)
├── contracts/           # Phase 1 output (API contract)
└── tasks.md             # Phase 2 output (implementation tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── multistage_env_filter.h    # Header-only implementation
└── tests/
    └── processors/
        └── multistage_env_filter_tests.cpp  # Unit tests
```

**Structure Decision**: Single header file in `processors/` following established pattern. All implementation inline in header (standard for this codebase's DSP components).

## Architecture Design

### State Machine for Envelope Progression

```
                                    ┌─────────────────┐
                                    │     IDLE        │
                                    │  (isComplete)   │
                                    └────────┬────────┘
                                             │ trigger()
                                             v
┌─────────────────────────────────────────────────────────────────┐
│                        RUNNING                                   │
│  ┌─────────┐   ┌─────────┐   ┌─────────┐       ┌─────────┐     │
│  │ Stage 0 │-->│ Stage 1 │-->│ Stage 2 │--...->│ Stage N │     │
│  └─────────┘   └─────────┘   └─────────┘       └─────────┘     │
│       ^              ^                               │          │
│       │              │                               │          │
│       └──────────────┴────── loop enabled ───────────┘          │
└───────────────────────────────┬─────────────────────────────────┘
                                │ release() OR last stage complete (no loop)
                                v
                     ┌─────────────────┐
                     │    RELEASING    │
                     │  (decaying to   │
                     │  baseFrequency) │
                     └────────┬────────┘
                              │ release complete
                              v
                     ┌─────────────────┐
                     │    COMPLETE     │
                     │ (isComplete=true│
                     └─────────────────┘
```

### Envelope States

```cpp
enum class EnvelopeState : uint8_t {
    Idle,       // Not triggered, sitting at baseFrequency
    Running,    // Transitioning through stages
    Releasing,  // Decaying to baseFrequency after release()
    Complete    // Finished (non-looping) or waiting for retrigger
};
```

### Stage Data Structure

```cpp
struct EnvelopeStage {
    float targetHz = 1000.0f;   // Target cutoff frequency
    float timeMs = 100.0f;      // Transition time in milliseconds
    float curve = 0.0f;         // Curve shape: -1 (log) to +1 (exp), 0 = linear
};
```

### Curve Interpolation Algorithm

The curve parameter controls the shape of transitions between stages:

```
curve = -1.0 (logarithmic): Fast start, slow finish
curve =  0.0 (linear):      Constant rate
curve = +1.0 (exponential): Slow start, fast finish
```

**Algorithm** (power-based shaping):

```cpp
float applyCurve(float t, float curve) {
    // t is normalized phase [0, 1]
    // curve is [-1, +1]

    if (curve >= 0.0f) {
        // Exponential: pow(t, 1 + curve * k) where k controls steepness
        // At curve=0: t^1 = linear
        // At curve=1: t^(1+k) = exponential
        float exponent = 1.0f + curve * 3.0f;  // k=3 gives good range
        return std::pow(t, exponent);
    } else {
        // Logarithmic: 1 - pow(1-t, 1 + |curve| * k)
        // Mirror of exponential
        float exponent = 1.0f + (-curve) * 3.0f;
        return 1.0f - std::pow(1.0f - t, exponent);
    }
}
```

### Loop Handling with Smooth Transitions

When loop wraps from `loopEnd` to `loopStart`:

1. Current position is at end of `loopEnd` stage (cutoff = loopEnd's target)
2. Transition starts toward `loopStart`'s target
3. Use `loopStart`'s curve and time for the transition
4. This ensures seamless rhythmic patterns

```
Stage 0 ─> Stage 1 ─> Stage 2 ─> Stage 3
            ^           loopEnd    │
            │     loopStart        │
            └──────────────────────┘
                smooth transition
```

### Velocity Scaling Implementation

Velocity scales the total modulation range, not individual stage targets:

```cpp
// In trigger(float velocity):
float range = calculateMaxTarget() - baseFrequency_;
float effectiveRange = range * (1.0f - velocitySensitivity_ * (1.0f - velocity));

// Each stage target is scaled proportionally:
float scaleFactor = effectiveRange / range;
// effectiveTarget = baseFrequency_ + (stageTarget - baseFrequency_) * scaleFactor
```

### Release Behavior

Release has independent timing configured via `setReleaseTime(float ms)`:

1. `release()` called at any point
2. Save current cutoff frequency
3. Begin smooth decay from current cutoff to baseFrequency
4. Release uses OnePoleSmoother for smooth exponential decay
5. Loop is exited, state becomes `Releasing`

## Real-Time Safety Considerations

1. **No allocations**: All arrays (stages) are fixed-size `std::array<EnvelopeStage, 8>`
2. **No exceptions**: All methods are `noexcept`
3. **No locks**: State is single-threaded, no synchronization needed
4. **Denormal flushing**: After every filter sample via SVF's internal handling
5. **NaN/Inf handling**: Input validation returns 0 and resets state
6. **Bounded operations**: All loops bounded by `kMaxStages` (8)

## Test Strategy

### Unit Tests (Priority Order)

1. **Basic Lifecycle**
   - `prepare()` initializes correctly
   - `reset()` clears state without changing parameters
   - `isComplete()` / `isRunning()` status tracking

2. **Multi-Stage Progression (User Story 1)**
   - 4-stage sweep from baseFrequency through all targets
   - Stage timing accuracy (within 1% tolerance)
   - `getCurrentStage()` returns correct index

3. **Curve Shapes (User Story 2)**
   - Linear curve (0.0) produces constant rate
   - Exponential curve (+1.0) slow start, fast finish
   - Logarithmic curve (-1.0) fast start, slow finish
   - Intermediate values produce proportional curves

4. **Looping (User Story 3)**
   - Loop from stage 1 to 3 repeats correctly
   - Loop transition is smooth (no clicks)
   - `release()` exits loop

5. **Velocity Sensitivity (User Story 4)**
   - `velocity=0.5` with `sensitivity=1.0` produces 50% modulation depth
   - `sensitivity=0.0` ignores velocity
   - `velocity=1.0` produces full depth

6. **Release Behavior (User Story 5)**
   - `release()` during loop exits and decays
   - `release()` timing is independent of stage times
   - Smooth transition to baseFrequency

7. **Edge Cases**
   - `numStages=0` defaults to 1 stage
   - `stageTime=0ms` performs instant transition
   - `loopStart > loopEnd` clamped/ignored
   - `trigger()` while running restarts from stage 0
   - NaN/Inf input handled gracefully

8. **Performance**
   - 1024-sample block < 0.5% CPU at 96kHz (SC-006)
   - Zero allocations during processing (SC-007)

### Test Helpers Needed

- Signal ramp detector (verify monotonic increase/decrease)
- Timing measurement (samples to reach target percentage)
- Curve shape analyzer (derivative analysis for log/lin/exp)

## Complexity Tracking

No constitution violations. Standard Layer 2 processor implementation.

## References

- **Spec**: [spec.md](spec.md)
- **SVF**: `dsp/include/krate/dsp/primitives/svf.h`
- **Smoother**: `dsp/include/krate/dsp/primitives/smoother.h`
- **GrainEnvelope** (curve reference): `dsp/include/krate/dsp/core/grain_envelope.h`
- **FilterStepSequencer** (pattern reference): `dsp/include/krate/dsp/systems/filter_step_sequencer.h`
- **EnvelopeFilter** (existing, different purpose): `dsp/include/krate/dsp/processors/envelope_filter.h`
