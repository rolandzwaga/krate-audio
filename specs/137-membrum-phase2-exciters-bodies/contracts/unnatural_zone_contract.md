# Unnatural Zone Contract

**Applies to:** `Membrum::UnnaturalZone` and its sub-modules: Mode Stretch (direct), Decay Skew (direct), Mode Inject, Nonlinear Coupling, Material Morph.

## Default-off guarantee (FR-055)

When ALL of the following hold:

- `modeStretch_ == 1.0f` (physical)
- `decaySkew_ == 0.0f` (natural damping)
- `modeInject.amount_ == 0.0f`
- `nonlinearCoupling.amount_ == 0.0f`
- `materialMorph.enabled_ == false`

Then the DrumVoice's output is bit-identical (within −120 dBFS RMS noise floor) to the equivalent Phase 2 patch with Unnatural Zone disabled. The default values above MUST produce NO audible contribution and NO processing overhead (early-out paths).

This is verified by the Phase 1 regression test: default Membrane + Impulse patch plays identically to Phase 1 (SC-005).

## Mode Stretch

### Mechanism
- Wired directly to `ModalResonatorBank::stretch` via the body mapping helper (`MapperResult.stretch`).
- For String body, maps to `WaveguideString::setBrightness` or a string-inharmonicity control (TBD in implementation — documented there).
- For Bell body, uses the bank's stretch on all partials.

### Range and defaults
- Range: 0.5..2.0
- Default: 1.0 (physical)
- Unity value: 1.0 — MUST produce the same output as Unnatural Zone disabled.

### Stretch math
- `stretch < 1.0`: partials compressed (closer together in frequency).
- `stretch = 1.0`: natural physical ratios.
- `stretch > 1.0`: partials spread apart.

### Real-time safety
- Pure scalar parameter flow through to the bank. No new DSP. No allocation.

## Decay Skew

### Mechanism
- Range: -1.0..+1.0, default 0.0 (natural Chaigne-Lambourg).
- **Phase 2 implementation**: approximated by scalar-biasing `brightness` and per-mode decay-time multipliers in the body mapping helper. Full per-mode override requires an API extension on `ModalResonatorBank` which is **out of scope** for Phase 2 per FR-101 (no `dsp/` changes).
- **Fallback plan**: if scalar approximation fails the US6-2 test tolerance, escalate to a per-block `updateModes()` refresh with computed per-mode damping (documented in `research.md` §9).

### Semantics
- `decaySkew = 0.0`: natural damping (high modes decay faster, positive b3).
- `decaySkew = +1.0`: maximum natural tilt.
- `decaySkew = -1.0`: inverted — fundamental decays fast, high modes sustain.

### Test (US6-2)
- At `decaySkew = -1.0`, Impulse + Membrane, measure envelope of mode 0 (fundamental) and mode 7 (highest Phase 1 mode) over 500 ms.
- Assert `t60(mode7) > t60(mode0)` (inverted from natural).

## Mode Inject

### Mechanism
- Wraps `Krate::DSP::HarmonicOscillatorBank`.
- Phase 2 supports ONE preset: **harmonic** — integer ratios 1, 2, 3, 4, 5, 6, 7, 8 at the body's fundamental.
- Output summed with the body output at a user-controlled amount (0..1).

### Phase randomization (spec 135 mandate)
- On every `trigger()`, Mode Inject **MUST** randomize the starting phase of each injected partial independently via `XorShift32` (`dsp/core/xorshift32.h`).
- Seed: per-voice `voiceId` (same pattern as `ImpactExciter::prepare(sampleRate, voiceId)` and `WaveguideString::prepareVoice(voiceId)`).
- **Test (US6-3)**: trigger the same voice twice with `amount_ = 0.5`; assert the injected partial waveforms (first 10 ms) differ in phase between the two triggers, while the body output's physical modes are the same phase both times (since the body resonator is deterministic on re-trigger).

### Bypass when `amount_ == 0.0f`
- Early-out: when amount is exactly zero, `process()` returns `0.0f` immediately without calling the oscillator bank.
- Zero leak: no leftover state from the previous frame can contribute output.

### Real-time safety
- `HarmonicOscillatorBank` is pre-allocated in `prepare()`.
- `trigger()` writes new phase offsets; no allocation.
- `XorShift32` is per-voice, thread-local to the audio thread.

## Nonlinear Coupling

### Mechanism (simplified — spec 135 FR-053 + clarifications)

```cpp
float processSample(float bodyOutput) {
    if (amount_ == 0.0f) return bodyOutput;   // early-out bypass

    float env = envFollower_.processSample(bodyOutput);
    float couplingStrength = velocity_ * amount_;
    float dEnv = env - previousEnv_;
    previousEnv_ = env;

    float modulated = bodyOutput * (1.0f + couplingStrength * dEnv);
    return energyLimiter_.process(modulated);  // TanhADAA soft-clip
}
```

### Components
- `Krate::DSP::EnvelopeFollower` — tracks body output RMS energy.
- `Krate::DSP::TanhADAA` — the mandatory energy limiter.

### Stability guarantee (FR-053, SC-008)
- The energy limiter via `TanhADAA` **MUST** guarantee `|output| ≤ 1.0` for any `bodyOutput`, any velocity, any amount setting.
- Test: every body × every exciter × velocity 127 × amount 1.0; assert peak ≤ 0 dBFS over 1 s of audio.

### Bypass when `amount_ == 0.0f`
- Early-out returns the input unchanged. No envelope follower update, no saturation, no limiter. Bit-identical to Unnatural Zone disabled.

### Real-time safety
- Envelope follower and saturator are pre-allocated. No allocation in `processSample()`.

## Material Morph

### Mechanism
- A per-voice time-varying envelope that modulates the `Material` parameter from `startMaterial_` to `endMaterial_` over `durationMs_` milliseconds.
- Triggered on every note-on.
- Output is a scalar in `[0, 1]` that DrumVoice uses to override the static `Material` when calling the body mapping helper.

### Curve options
- Linear: `material(t) = lerp(start, end, t/duration)`
- Exponential: `material(t) = start * pow(end/start, t/duration)` (default for natural-feeling change)

### Disabled state (FR-054)
- When `enabled_ == false` OR `durationMs_ == 0`, `process()` returns the DrumVoice's static Material value unchanged.
- No morph → no processing overhead.

### Completion
- When `sampleCounter_ >= totalSamples_`, the morph holds at `endMaterial_` until the next `trigger()`.
- Morph completes naturally before the amp envelope's release phase (enforced by choosing `durationMs_` in the spec range 10..2000 ms).

### Edge case: duration shorter than a process block
- If `durationMs_` corresponds to fewer samples than a single process call, the morph completes in that call. No hang, no static timbre.

### Real-time safety
- Pure scalar math. No allocation. No RNG.

## Unnatural Zone aggregate contract

### `UnnaturalZone::prepare`
- Calls `prepare()` on all sub-modules.
- Must be called once before any audio thread processing.

### Parameter flow to body mapper
- `modeStretch_` and `decaySkew_` are **control-plane** values read by the body mapping helper during `DrumVoice::noteOn()`. They are NOT audio-rate processed inside `UnnaturalZone`.
- `materialMorph.process()` is called once per sample (or per block) to produce a time-varying Material value, which is passed to the body mapping helper.

### Parameter flow to audio-rate chain
- `modeInject.process()` output is **added** to the body output before `nonlinearCoupling.processSample()`.
- `nonlinearCoupling.processSample(bodyOutput + modeInjectOutput)` is the final pre-ToneShaper stage.

Integration diagram inside `DrumVoice::process()`:

```
  exciterOut = exciterBank_.process(bodyBank_.getLastOutput())
  pitchHz = toneShaper_.processPitchEnvelope()
  bodyBank_.updateFundamental(pitchHz)  // control-plane per-sample pitch
  bodyOut = bodyBank_.processSample(exciterOut)
  injectedOut = bodyOut + unnaturalZone_.modeInject.process()
  coupledOut = unnaturalZone_.nonlinearCoupling.processSample(injectedOut)
  toneOut = toneShaper_.processSample(coupledOut)
  envOut = toneOut * ampEnvelope_.process() * level_
  return envOut
```

## Test coverage requirements

1. **Defaults-off identity** — all Unnatural parameters at defaults; assert the output is bit-identical to a Phase 2 patch with Unnatural disabled (within −120 dBFS RMS). FR-055.
2. **Mode Stretch ratio** — set `modeStretch_ = 1.5`; measure partial ratios; assert they are multiplied by ~1.5 (within bank interpolation tolerance).
3. **Decay Skew inversion** — set `decaySkew_ = -1.0`; measure t60 of fundamental vs highest mode; assert fundamental < highest (inverted).
4. **Mode Inject phase randomization** — trigger twice with same voice; compare phases of injected partials; assert they differ (deterministic per voice-id + trigger-count).
5. **Mode Inject bypass** — `amount = 0.0`; assert bit-identical to direct body output.
6. **Nonlinear Coupling evolution** — `amount = 0.5`, velocity = 1.0, Plate body; measure spectral centroid over 500 ms; assert it varies by > 10% (not static).
7. **Nonlinear Coupling energy limiter** — every body × every exciter × amount = 1.0 × velocity = 1.0; assert peak ≤ 0 dBFS over 1 s.
8. **Nonlinear Coupling bypass** — `amount = 0.0`; assert `processSample(x) == x` for any x.
9. **Material Morph envelope** — enabled, start = 1.0, end = 0.0, duration = 500 ms; measure decay tilt envelope; assert it changes monotonically.
10. **Material Morph disabled** — enabled = false; assert Material is static throughout the note.
11. **Allocation detector** — all sub-modules' `process()` and `trigger()` zero heap activity.
