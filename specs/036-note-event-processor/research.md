# Research: Note Event Processor (036)

**Date**: 2026-02-07 | **Status**: Complete

## Research Tasks & Findings

### R1: Existing `midiNoteToFrequency()` API and Reusability

**Decision**: Reuse `midiNoteToFrequency()` from `midi_utils.h` as-is for FR-001.

**Rationale**: The existing function already implements the 12-TET formula with a configurable A4 reference. It is `constexpr`, `noexcept`, real-time safe, and uses the project's `detail::constexprExp()` for cross-platform constexpr support. Its signature `float midiNoteToFrequency(int midiNote, float a4Frequency = kA4FrequencyHz)` matches exactly what `NoteProcessor::getFrequency()` needs internally.

**Alternatives considered**:
- Reimplementing the formula inline in NoteProcessor: rejected because it duplicates tested code and risks ODR issues.
- Using `std::pow(2.0f, ...)` directly: rejected because it is not constexpr on MSVC and the existing function already solves this.

---

### R2: `semitonesToRatio()` for Pitch Bend Frequency Computation

**Decision**: Reuse `semitonesToRatio()` from `pitch_utils.h` for computing the pitch bend frequency multiplier.

**Rationale**: The function computes `2^(semitones/12)` which is exactly the ratio needed to convert bend semitones to a frequency multiplier. It is `inline`, `noexcept`, and real-time safe. The `VoiceAllocator::computeFrequency()` already uses this exact pattern: `bendRatio = semitonesToRatio(pitchBendSemitones_)`.

**Alternatives considered**:
- Using `detail::constexprExp(semitones * kLn2Over12)` directly: rejected because `semitonesToRatio()` already wraps this and is more readable.
- Note: `semitonesToRatio()` uses `std::pow` not `constexprExp`, which is fine since NoteProcessor is not constexpr.

---

### R3: `OnePoleSmoother` for Pitch Bend Smoothing

**Decision**: Reuse `OnePoleSmoother` from `smoother.h` for pitch bend smoothing (FR-007, FR-008).

**Rationale**: OnePoleSmoother provides exactly the one-pole exponential filter required by FR-007. Key API methods:
- `configure(smoothTimeMs, sampleRate)` -- sets coefficient
- `setTarget(value)` -- NaN-safe target setting (resets to 0.0 on NaN)
- `process()` -- advances one sample, returns smoothed value
- `snapTo(value)` -- instant state reset (used by `reset()`)
- `setSampleRate(sampleRate)` -- recalculates coefficient, preserves current/target (FR-003)

The MonoHandler (spec 035) already uses `LinearRamp` from the same file for portamento. Using `OnePoleSmoother` for pitch bend follows the established pattern.

**Important API detail**: `OnePoleSmoother::setTarget()` handles NaN by resetting both target and current to 0.0f, and handles Inf by clamping. However, FR-020 specifies that NaN/Inf pitch bend inputs should be *ignored* (maintaining last valid state). This means NoteProcessor must guard against NaN/Inf *before* calling `setTarget()`, not rely on OnePoleSmoother's built-in behavior.

**Alternatives considered**:
- LinearRamp: rejected because spec requires exponential convergence (FR-007), not constant-rate.
- SlewLimiter: rejected because spec requires time-based convergence, not rate-limited.
- Custom smoother: rejected because OnePoleSmoother already provides the exact behavior.

---

### R4: Velocity Curve Functions -- New Code Needed

**Decision**: Add velocity curve mapping functions to `midi_utils.h` (Layer 0) as free functions, alongside the existing `velocityToGain()`.

**Rationale**: The spec requires four velocity curves (Linear, Soft, Hard, Fixed). The existing `velocityToGain()` in `midi_utils.h` already implements the Linear curve. Adding the other curves as peer functions in the same file:
1. Keeps all velocity-related conversions in one location (Layer 0)
2. Makes them reusable by VoiceAllocator, MonoHandler, and future voice engines
3. Follows the existing pattern of pure, `constexpr`, `noexcept` utility functions

The approach is a single dispatching function `mapVelocity(int velocity, VelocityCurve curve)` that delegates to:
- `VelocityCurve::Linear` -- reuses existing `velocityToGain()` directly (no alias needed)
- `VelocityCurve::Soft` -- `(velocity/127)^0.5` (square root, exponent 0.5)
- `VelocityCurve::Hard` -- `(velocity/127)^2.0` (squared)
- `VelocityCurve::Fixed` -- returns 1.0 for any non-zero velocity

Note: `velocityToGainSoft` and `velocityToGainHard` use `std::pow`/`std::sqrt` which are not constexpr on MSVC. These functions will be `inline` rather than `constexpr`.

**Alternatives considered**:
- Adding curves as NoteProcessor member functions: rejected because they are pure stateless transforms better suited to Layer 0.
- Creating a separate `velocity_utils.h`: rejected because velocity is already in `midi_utils.h` and the functions are small.
- Using a lookup table for velocity curves: rejected as unnecessary -- 128 entries computed per note-on is negligible cost.

---

### R5: VelocityCurve Enum and VelocityOutput Struct Location

**Decision**: Define `VelocityCurve` enum in `midi_utils.h` (Layer 0). Define `VelocityOutput` struct in `note_processor.h` (Layer 2) since it is specific to NoteProcessor.

**Rationale**: The `VelocityCurve` enum (`Linear`, `Soft`, `Hard`, `Fixed`) is a general MIDI concept applicable beyond NoteProcessor. Placing it in Layer 0 makes it available to VoiceAllocator and other consumers. The `VelocityOutput` struct (amplitude, filter, envelope time destination values) is a NoteProcessor-specific aggregate that couples velocity output to the three-destination routing model; it belongs with the processor.

**Alternatives considered**:
- Both in `note_processor.h`: rejected because `VelocityCurve` has broader utility.
- Both in `midi_utils.h`: rejected because `VelocityOutput` is a processor concept, not a MIDI concept.

---

### R6: Tuning Reference Clamping (FR-002)

**Decision**: NoteProcessor will clamp the A4 reference to [400, 480] Hz for finite values, and reset to 440.0 Hz for NaN/Inf.

**Rationale**: The spec explicitly defines this behavior. The VoiceAllocator currently does not clamp its `a4Frequency_` (it only ignores NaN/Inf). NoteProcessor will add the full validation as specified. Implementation uses `detail::isNaN()` and `detail::isInf()` from `db_utils.h` for the NaN/Inf check, then `std::clamp` for the range.

---

### R7: NoteProcessor as Header-Only Class

**Decision**: Implement NoteProcessor as a header-only class in `dsp/include/krate/dsp/processors/note_processor.h`.

**Rationale**: All existing processors in the codebase are header-only. The NoteProcessor has no significant implementation complexity that would benefit from a separate .cpp file. Header-only:
- Follows established project conventions (MonoHandler, all other processors)
- Enables inlining of `getFrequency()` which is called per-voice per-block
- Simplifies build configuration (no need to add .cpp to CMakeLists.txt beyond the header list)

---

### R8: `prepare()` Behavior When Called Mid-Smoothing (FR-003)

**Decision**: Call `OnePoleSmoother::setSampleRate()` to preserve current/target and recalculate coefficient only.

**Rationale**: `OnePoleSmoother::setSampleRate(float sampleRate)` does exactly what FR-003 requires: it preserves the current smoothed value and target, recalculating only the coefficient. This is verified by reading the source (smoother.h:262-265):
```cpp
void setSampleRate(float sampleRate) noexcept {
    sampleRate_ = sampleRate;
    coefficient_ = calculateOnePolCoefficient(timeMs_, sampleRate_);
}
```
No additional code is needed in NoteProcessor beyond delegating to this method.

---

### R9: Performance Budget (SC-006)

**Decision**: `getFrequency()` will use simple arithmetic (1 function call + 1 multiply) and will easily meet the <0.1% CPU target.

**Rationale**: `getFrequency()` performs:
1. `midiNoteToFrequency(note, a4Reference_)` -- one `constexprExp()` call (~Taylor series)
2. `semitonesToRatio(currentBendSemitones_)` -- one `std::pow()` call
3. One multiply: `baseFreq * bendRatio`

This is essentially two transcendental function calls and one multiply per voice. At 44.1kHz with a 512-sample block, this is called once per block per voice. Even with 32 voices, the total per-sample amortized cost is negligible. The MonoHandler's `processPortamento()` (which is similar) operates well under budget.

**Alternative optimization if needed**: Pre-compute `bendRatio` once per block in `processPitchBend()` and cache it. This reduces `getFrequency()` to one function call + one multiply.

**Decision on optimization**: Pre-compute `bendRatio_` in `processPitchBend()` to minimize per-voice cost. This is the pattern used by VoiceAllocator's `computeFrequency()`.

---

### R10: NaN/Inf Handling Consistency (FR-020)

**Decision**: NoteProcessor will pre-validate all inputs before passing to OnePoleSmoother, using `detail::isNaN()` and `detail::isInf()`.

**Rationale**: The spec requires different NaN/Inf behavior than OnePoleSmoother's default:
- **Pitch bend**: Ignore NaN/Inf, maintain last valid state (FR-020). OnePoleSmoother's `setTarget()` resets to 0 on NaN.
- **Tuning reference**: NaN/Inf reset to 440 Hz; finite out-of-range values clamp to [400, 480] (FR-002).

NoteProcessor must guard inputs before delegating to the smoother to achieve the specified behavior.
