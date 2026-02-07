# Research: Trance Gate (039)

**Date**: 2026-02-07 | **Status**: Complete

## Research Tasks

### R-001: Asymmetric One-Pole Smoother Pattern

**Question**: How to implement two-smoother asymmetric attack/release with state synchronization?

**Decision**: Use two `OnePoleSmoother` instances -- one configured with attack time, one with release time. Switch active smoother based on gate direction (target level increasing = attack, decreasing = release). Synchronize the inactive smoother's state to the active smoother's output after each sample to ensure continuity when switching direction.

**Rationale**: The existing `OnePoleSmoother` is well-tested and handles NaN/Inf/denormal edge cases. The two-instance approach is simpler than dynamically reconfiguring a single instance per-sample, and the state sync ensures no discontinuities when the gate direction reverses mid-transition.

**Alternatives considered**:
- Single `OnePoleSmoother` with dynamic coefficient switching per-sample: Adds complexity and the coefficient switch itself could cause a subtle discontinuity.
- `SlewLimiter` with asymmetric rise/fall rates: Uses linear ramp (constant rate) rather than exponential approach, which sounds less natural for amplitude gating.
- Custom implementation: Unnecessary when `OnePoleSmoother` already provides exactly the right behavior.

**Implementation detail**: After each `process()` call on the active smoother, call `snapTo(activeOutput)` on the inactive smoother. This is 1 extra function call per sample (negligible cost) and guarantees both smoothers are always at the same state.

---

### R-002: Standalone Timing vs SequencerCore Composition

**Question**: Should TranceGate compose `SequencerCore` internally or implement its own minimal timing?

**Decision**: Implement standalone minimal timing (~10 lines: sample counter + step advancement). Do NOT compose `SequencerCore`.

**Rationale**: `SequencerCore` provides swing, gate length, direction, sync, and crossfade ramp -- features that TranceGate does not need. TranceGate's timing is fundamentally simpler: advance a sample counter, when it exceeds `samplesPerStep`, move to the next step. The per-step gain level (float, not boolean) and edge smoothing are handled by the pattern array and OnePoleSmoother, not by gate length/crossfade logic.

**Alternatives considered**:
- Compose `SequencerCore`: Would require ignoring most of its API surface (swing, gate length, direction, ramp), and the `tick()` return value (step-changed boolean) is the only useful output. Adds unnecessary coupling and a dependency on a Layer 1 primitive that may evolve independently.
- Fork/extend `SequencerCore`: Violates DRY and creates maintenance burden.

**Timing implementation**:
```cpp
// ~10 lines of timing logic
sampleCounter_++;
if (sampleCounter_ >= samplesPerStep_) {
    sampleCounter_ = 0;
    currentStep_ = (currentStep_ + 1) % numSteps_;
    // Update smoother target to new step level
}
```

---

### R-003: NoteValue Enum Usage for Tempo Sync

**Question**: How to use `NoteValue` and `NoteModifier` enums for step timing calculation?

**Decision**: Store `NoteValue` and `NoteModifier` in `TranceGateParams`. Use `getBeatsForNote(noteValue, noteModifier)` to get beats-per-step, then calculate `samplesPerStep = (60.0 / bpm) * beatsPerNote * sampleRate`.

**Rationale**: This is the established pattern used by `BlockContext::tempoToSamples()` and `SequencerCore::updateStepDuration()`. Using the same Layer 0 infrastructure ensures consistency across the codebase.

**Verified API signatures**:
- `getBeatsForNote(NoteValue, NoteModifier)` returns `float` (beats per note)
- `NoteValue` enum: `Quarter`, `Eighth`, `Sixteenth`, `ThirtySecond` (and others)
- `NoteModifier` enum: `None`, `Dotted`, `Triplet`

---

### R-004: EuclideanPattern Integration for Phase Offset

**Question**: How does phase offset interact with Euclidean pattern generation?

**Decision**: Phase offset is applied via the `rotation` parameter of `EuclideanPattern::generate(pulses, steps, rotation)`. The rotation is calculated as `static_cast<int>(phaseOffset * numSteps)`. For manual patterns, phase offset shifts the step read index: `effectiveStep = (currentStep_ + rotationOffset) % numSteps_`.

**Rationale**: `EuclideanPattern::generate()` already supports rotation as its third parameter. The rotation shifts the bitmask, so step 0 still starts at the beginning of the pattern but the "hit" positions are rotated. This is efficient (computed once at set-time, not per-sample) and consistent with the existing API.

**Verified from `euclidean_pattern.h`**:
- `generate(int pulses, int steps, int rotation = 0)` -- rotation wraps modulo steps
- `isHit(uint32_t pattern, int position, int steps)` -- O(1) bit test
- `kMaxSteps = 32` -- matches TranceGate max step count

---

### R-005: Per-Voice vs Global Mode Implementation

**Question**: How to implement per-voice vs global clock modes?

**Decision**: Controlled by a `perVoice` boolean flag in `TranceGateParams`. In per-voice mode (default), `reset()` resets the sample counter and step position to 0. In global mode, `reset()` is a no-op for the timing engine (clock continues). The caller (voice orchestration) calls `reset()` on note-on; the TranceGate decides whether to actually reset based on the mode flag.

**Rationale**: This is the simplest approach -- the TranceGate does not need to know about other voices or share state. Each voice has its own TranceGate instance. In per-voice mode, they diverge because they reset at different times. In global mode, they stay in sync because they never reset (assuming they all start from the same initial state and receive the same tempo updates).

**Alternatives considered**:
- Shared clock object: Adds complexity and shared mutable state between voices, violating real-time safety principles.
- External clock signal: Over-engineered for this use case.

---

### R-006: OnePoleSmoother Coefficient Formula Verification

**Question**: What is the exact coefficient formula and does the spec's SC-002 threshold match?

**Decision**: The coefficient formula in `smoother.h` is `coeff = exp(-5000.0 / (timeMs * sampleRate))`, where 5000 = 5 time constants * 1000 ms/s. The 99% settling time interpretation means: after `timeMs` milliseconds, the smoother has reached 99% of the target.

**Verification for SC-002**: For attackMs = 2.0 at 44100 Hz:
- `coeff = exp(-5000.0 / (2.0 * 44100)) = exp(-0.0567) = 0.9449`
- Max per-sample change from 0.0 to 1.0: `1.0 - coeff = 0.0551`
- SC-002 threshold: `< 0.056` -- this matches (0.0551 < 0.056).

The maximum sample-to-sample change occurs on the first sample after a target change, and equals `1.0 - coefficient` for a full 0-to-1 transition.

---

### R-007: Free-Running Mode Implementation

**Question**: How to implement free-running (Hz-based) step timing?

**Decision**: When `tempoSync` is false, calculate `samplesPerStep = sampleRate / rateHz`. The `rateHz` parameter specifies the rate at which the full pattern cycles... wait, the spec says `samplesPerStep = sampleRate / rateHz` (FR-006), meaning `rateHz` is the rate per STEP, not per pattern. So at 8 Hz and 44100 Hz, each step lasts 5512.5 samples.

**Rationale**: This is consistent with the acceptance scenario in User Story 5, SC-003: "Given a TranceGate in free-run mode with rate = 8.0 Hz, each step lasts approximately 5512 samples (44100 / 8 = 5512.5)."

---

### R-008: Default Behavior Without prepare()

**Question**: What should happen when `process()` is called before `prepare()`?

**Decision**: Per FR-014, the gate defaults to 44100 Hz sample rate and unity gain (passthrough). The constructor initializes `sampleRate_ = 44100.0`, `currentGainValue_ = 1.0f`, and the smoother states to 1.0 (unity). All step levels default to 1.0. This means unprepared processing produces output == input.

**Rationale**: Silence on unprepared state would be worse (could mask bugs in orchestration code). Unity gain passthrough is safe and audible, making it obvious if prepare() was missed while not destroying audio.

---

## Summary

All NEEDS CLARIFICATION items have been resolved. Key decisions:

1. **Asymmetric smoothing**: Two `OnePoleSmoother` instances with state sync
2. **Timing engine**: Standalone (~10 lines), not composing `SequencerCore`
3. **Tempo sync**: `NoteValue`/`NoteModifier` enums with `getBeatsForNote()`
4. **Phase offset**: Pattern rotation via `EuclideanPattern::generate(rotation)` parameter
5. **Per-voice/global**: Boolean flag controlling `reset()` behavior
6. **No new Layer 0 utilities needed**: All required math/conversion functions exist
