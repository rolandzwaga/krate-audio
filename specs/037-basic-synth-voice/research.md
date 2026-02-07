# Research: Basic Synth Voice (037)

## Research Task 1: RetriggerMode Enum Discrepancy

**Context**: The spec references `RetriggerMode::Continue` for smooth retrigger behavior (attack from current level, no reset to zero). The existing `RetriggerMode` enum in `envelope_utils.h` only has `Hard` and `Legato`.

**Findings**:
- `RetriggerMode::Hard`: Resets to zero and re-enters attack from 0.0 on every gate-on.
- `RetriggerMode::Legato`: On gate-on while already active, if output > sustain target it enters decay, otherwise enters sustain. It does NOT re-enter attack from current level.
- Neither mode matches "attack from current level" behavior described in the spec.

**Decision**: Use `RetriggerMode::Legato` as the closest approximation, combined with manual gate control. Since the SynthVoice manages its own noteOn/noteOff lifecycle, it can implement the "attack from current level" behavior by simply calling `gate(true)` again while the envelope is already active. Looking at the ADSREnvelope code more carefully:
- In `Legato` mode, when gate is called with `on=true` while already active (not Idle, not Release), it does nothing (the if-else chain only handles Idle and Release cases).
- When in Release state and gate(true) is called in Legato mode: if output > sustainTarget, it enters Decay (from current level); otherwise enters Sustain.

This means for the "attack from current level" behavior specified, we actually need to manually enter attack from the current output level. The simplest approach: just use `gate(false)` then `gate(true)` on retrigger with Legato mode, but this would skip the attack-from-current-level behavior.

**Revised Decision**: Use `RetriggerMode::Hard` mode, but the critical insight is that calling `gate(true)` in Hard mode calls `enterAttack()`, which in turn for Exponential/Linear curves just starts the attack one-pole formula from the current `output_` value (it does NOT reset output_ to 0). The `enterAttack()` function preserves the current `output_` and starts the attack formula `output_ = attackBase_ + output_ * attackCoef_`. Since the one-pole converges asymptotically, starting from a non-zero output_ means the attack starts from the current level. For Logarithmic curves, `enterAttack()` explicitly records `logStartLevel_ = output_` and interpolates from there.

**Verification**: Reading `enterAttack()` at line 356:
```cpp
void enterAttack() noexcept {
    stage_ = ADSRStage::Attack;
    if (attackCurve_ == EnvCurve::Logarithmic) {
        logStartLevel_ = output_;  // Preserves current level
        ...
    } else {
        calcAttackCoefficients();   // output_ NOT zeroed
    }
}
```

The one-pole attack formula `output_ = attackBase_ + output_ * attackCoef_` will start from whatever `output_` is currently at. So `RetriggerMode::Hard` actually implements "attack from current level" for ALL curve types.

**Final Decision**: Use `RetriggerMode::Hard` (default). It already attacks from current level without resetting to zero. The spec's mention of `RetriggerMode::Continue` is a name that does not exist in the enum, but the behavior described (attack from current level) is naturally provided by the one-pole `enterAttack()` approach used in Hard mode.

**Rationale**: The existing ADSREnvelope implementation inherently preserves the current output level when entering attack in Hard mode. No changes to the ADSREnvelope are needed.

**Alternatives Considered**:
- Adding a `RetriggerMode::Continue` enum value: Rejected because Hard mode already provides this behavior.
- Using Legato mode: Rejected because Legato mode skips the attack phase in some cases (goes to Decay or Sustain instead).

---

## Research Task 2: Frequency-to-MIDI-Note Computation for Key Tracking

**Context**: Key tracking (FR-020, FR-021) needs to compute `midiNoteEquivalent` from frequency. The existing `pitch_utils.h` has `frequencyToNoteClass()` (returns 0-11 note class) and `frequencyToCentsDeviation()`, but no function returning continuous MIDI note number.

**Findings**:
- `frequencyToNoteClass()` uses: `midiNote = 12.0f * log2(hz / 440.0f) + 69.0f`, then rounds and takes mod 12.
- For key tracking, we need the continuous (unrounded) MIDI note number.
- Formula: `midiNote = 12.0f * log2(hz / 440.0f) + 69.0f`
- This is a simple one-liner that could be extracted to `pitch_utils.h` as `frequencyToMidiNote()`.

**Decision**: Extract a `frequencyToMidiNote(float hz)` function to `pitch_utils.h` (Layer 0) as a reusable utility. This benefits future voice types (FMVoice key tracking, wavetable voice key tracking).

**Rationale**: Follows Constitution Principle XIV (reuse). The formula is pure, stateless, and likely to be reused by the Polyphonic Synth Engine and any other voice type needing key tracking.

**Alternatives Considered**:
- Inline the formula in SynthVoice: Rejected because the function has audio-specific semantics and will be needed by future voices.
- Use `frequencyToNoteClass()` with deviation: Rejected because it loses octave information.

---

## Research Task 3: SVF Per-Sample vs Per-Block Coefficient Updates

**Context**: FR-019 says "per-sample modulation preferred but per-block acceptable". Need to determine the right approach for the SynthVoice.

**Findings**:
- The existing `SVF` class (Cytomic TPT topology) is specifically designed for stable per-sample coefficient updates. It uses trapezoidal integration which makes coefficient changes glitch-free.
- Each `setCutoff()` call triggers `updateCoefficients()` which recomputes `g_`, `k_`, `a1_`, `a2_`, `a3_`. This is: `tan()` + 3 divisions/multiplies. Cost is ~15-20ns per call.
- For a 512-sample block at 44.1kHz: per-sample coefficient update = 512 `tan()` calls. Per-block = 1 `tan()` call.
- CPU budget: SC-001 requires < 1% CPU. At 44.1kHz, 1% CPU = ~227ns per sample budget. The SVF `process()` is ~10 ops (~5ns). Adding `setCutoff()` per sample adds ~15ns. Total ~20ns, well within budget.
- However, the envelope changes continuously per-sample. The filter cutoff formula involves `pow(2, x/12)` which is a `pow()` call per sample.

**Decision**: Use per-sample filter coefficient updates. The cost is affordable within the 1% CPU budget and provides the smoothest filter sweeps. The `pow(2, x/12)` for the cutoff modulation formula can be approximated with `exp2f()` which is fast on modern CPUs.

**Rationale**: Per-sample updates eliminate any zipper noise or stepping artifacts in filter sweeps, which is critical for a synth voice where filter envelope modulation is the primary timbral shaping tool.

**Alternatives Considered**:
- Per-block updates with smoothing: Acceptable per spec but unnecessary given the SVF's design and CPU budget.
- Per-block updates without smoothing: Would produce audible stepping at low block sizes.

---

## Research Task 4: processBlock vs process() Bit-Identical Guarantee

**Context**: FR-030 and SC-004 require processBlock() to be bit-identical to N calls of process().

**Findings**:
- All existing components (PolyBlepOscillator, ADSREnvelope, SVF) implement their processBlock as a simple loop calling process(). This trivially guarantees bit-identical results.
- SynthVoice should follow the same pattern.

**Decision**: Implement `processBlock()` as a simple loop calling `process()`. This guarantees bit-identical results with zero additional complexity.

---

## Research Task 5: Effective Cutoff Clamping

**Context**: FR-018 requires clamping effective cutoff to [20 Hz, 49.5% of sample rate]. The SVF has its own clamping: [1 Hz, 49.5% of sample rate].

**Findings**:
- SVF constants: `kMinCutoff = 1.0f`, `kMaxCutoffRatio = 0.495f`.
- Spec requires clamping to 20 Hz minimum (more conservative than SVF's 1 Hz).
- SynthVoice should clamp the computed effective cutoff before passing to the SVF.

**Decision**: Clamp effective cutoff to [20.0f, sampleRate * 0.495f] in SynthVoice before calling `SVF::setCutoff()`. This matches the spec and provides an additional safety margin over the SVF's own clamping.

---

## Research Task 6: ADSREnvelope Gate vs RetriggerMode Behavior

**Context**: FR-007 says "envelopes MUST re-enter attack from current level". Need to verify this is what happens with gate(true) in Hard mode.

**Findings (reading ADSREnvelope source)**:

For Hard mode (`retriggerMode_ == RetriggerMode::Hard`):
- `gate(true)` always calls `enterAttack()` regardless of current state.
- `enterAttack()` for Exponential/Linear: sets stage to Attack, recalculates coefficients. Critically, it does NOT reset `output_` to 0. The next `processAttack()` call will compute `output_ = attackBase_ + output_ * attackCoef_`, starting from the current `output_` value.
- `enterAttack()` for Logarithmic: explicitly saves `logStartLevel_ = output_` and starts the quadratic interpolation from there.

**Conclusion**: Hard mode with gate(true) naturally provides "attack from current level" behavior for all curve types. This matches the spec requirement perfectly.

For velocity handling on retrigger:
- Must call `setVelocity()` with the new velocity before `gate(true)` to update the peak level.
- `setVelocity()` calls `updatePeakLevel()` which recalculates coefficients.
- The attack will then target the new `peakLevel_` from the current `output_`.

---

## Research Task 7: SVF Mode Mapping

**Context**: The spec requires lowpass, highpass, bandpass, and notch modes. The SVF has SVFMode enum with more modes.

**Findings**:
- `SVFMode::Lowpass` - matches spec
- `SVFMode::Highpass` - matches spec
- `SVFMode::Bandpass` - matches spec
- `SVFMode::Notch` - matches spec
- Additional modes (Allpass, Peak, LowShelf, HighShelf) are available but not required.

**Decision**: Expose only the four required modes (LP, HP, BP, Notch) through the SynthVoice API using the SVFMode enum directly. No wrapper enum needed.

---

## Research Task 8: NaN/Inf Input Handling Strategy

**Context**: FR-032 requires all setters to silently ignore NaN and Inf inputs.

**Findings**:
- The existing `detail::isNaN()` and `detail::isInf()` functions in `db_utils.h` use bit manipulation and work with `-ffast-math`.
- Pattern used throughout the codebase: early return without modifying parameter if NaN/Inf detected.
- All sub-components (PolyBlepOscillator, ADSREnvelope, SVF) already handle NaN/Inf in their own setters.

**Decision**: Add NaN/Inf guards in all SynthVoice setters using the `detail::isNaN()` / `detail::isInf()` pattern. This provides a double-safety layer (voice-level + component-level).
