# Tone Shaper Contract

**Applies to:** `Membrum::ToneShaper` — the per-voice post-body chain.

## Signal chain order (FR-040)

```
  body_output
    │
    ▼
  Waveshaper (Drive)         ─ alias-safe via ADAA
    │
    ▼
  Wavefolder                 ─ static waveshaper
    │
    ▼
  DC Blocker                 ─ removes asymmetric saturation offset
    │
    ▼
  SVF Filter                 ─ LP/HP/BP, modulated by filterEnv_
    │
    ▼
  (output passed to amp ADSR and Level in DrumVoice)
```

**Pitch Envelope** is **NOT** in the audio signal chain. It is a control-plane modulator that runs in parallel:

```
  pitchEnv_.process() ─────▶ body fundamental (updateModes / setFrequency)
```

`DrumVoice::process()` calls `toneShaper_.processPitchEnvelope()` FIRST (control plane), passes the result to `bodyBank_.updateFundamental(hz)` (or equivalent), THEN calls `bodyBank_.processSample()`, THEN passes the result to `toneShaper_.processSample()` for the audio-rate stages.

## Bypass semantics (FR-045 — regression guarantee)

When ALL of the following hold:

- `driveAmount_ == 0.0f`
- `foldAmount_ == 0.0f`
- `filterMode_ == SVFMode::Lowpass` (or equivalent pass-through)
- `filterCutoffHz_ >= 20000.0f`
- `filterEnvAmount_ == 0.0f`
- `pitchEnvTimeMs_ == 0.0f`

Then `processSample(bodyOutput) ≈ bodyOutput` within **−120 dBFS RMS** (deterministic input). Implementation must test this: a 1 kHz sine at 0 dBFS through an all-bypassed Tone Shaper must come out identical to −120 dBFS.

This is the Phase 1 regression guarantee (SC-005): a Phase 2 patch with Tone Shaper defaulted-off sounds bit-identical to a Phase 1 patch.

## Parameter ranges and defaults

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Filter Type | StringList: LP/HP/BP | LP | |
| Filter Cutoff | 20..20000 Hz | 20000 (bypass) | Exponential taper |
| Filter Resonance | 0..1 | 0 | |
| Filter Env Amount | -1..1 | 0 (bypass) | ± inverts envelope direction |
| Filter Env Attack | 0..500 ms | 0 ms | |
| Filter Env Decay | 0..2000 ms | 100 ms | |
| Filter Env Sustain | 0..1 | 0 | |
| Filter Env Release | 0..2000 ms | 200 ms | |
| Drive Amount | 0..1 | 0 (bypass) | |
| Fold Amount | 0..1 | 0 (bypass) | |
| Pitch Env Start | **20..2000 Hz (absolute)** | **160 Hz** | Clarification 2 |
| Pitch Env End | **20..2000 Hz (absolute)** | **50 Hz** | Clarification 2 |
| Pitch Env Time | 0..500 ms | 0 (disabled) | |
| Pitch Env Curve | StringList: Exp/Lin | Exp | |

## Pitch Envelope behavior (FR-044)

### Units: ABSOLUTE HZ (mandatory per spec clarification)

- Start and End are absolute Hz values, NOT semitones offset and NOT normalized-per-body.
- The envelope produces a frequency in Hz at each process call.
- The DrumVoice passes this Hz value to the body's note-on / per-block frequency update:
  - Modal bodies: used as the body fundamental `f0` in `setModes()` / `updateModes()`.
  - String body: passed to `WaveguideString::setFrequency(hz)`.

### Disabled state

- When `pitchEnvTimeMs_ == 0.0f`, the Pitch Envelope is **disabled**: `processPitchEnvelope()` returns a body-specific "natural" frequency (derived from `Size` via the body mapper), NOT `pitchEnvStartHz_`.
- This ensures that a default Membrum patch (all Tone Shaper off) uses the Size-derived fundamental exactly as Phase 1 did.

### Zero-duration graceful degradation (edge case)

- Setting `pitchEnvTimeMs_` to a tiny value (e.g., 0.01 ms) MUST NOT cause divide-by-zero or infinite slope. The envelope either:
  - Snaps to `pitchEnvEndHz_` within 1 sample, OR
  - Clamps the minimum time internally to 1 ms.

### 808 kick test (SC-009)

- Configure Pitch Envelope: Start = 160 Hz, End = 50 Hz, Time = 20 ms, Curve = Exp.
- Trigger Impulse + Membrane at velocity 100.
- Measure fundamental pitch glide via short-time FFT over the first 50 ms.
- At t = 20 ms, the measured fundamental MUST be within ±10% of 50 Hz.

## Real-time safety invariants

- All setters are `noexcept` and thread-safe from the audio thread (they write to plain members that are subsequently read by the audio thread at block start; since `processParameterChanges()` runs on the audio thread, no cross-thread data race occurs).
- `processSample()` and `processPitchEnvelope()` are `noexcept`, allocation-free, lock-free.
- `prepare()` may allocate (Phase setup only); called once before `setActive(true)`.
- FTZ/DAZ denormal protection is inherited from the processor.

## Sub-component reuse

| Stage | Backend | Header |
|-------|---------|--------|
| Drive | `Krate::DSP::Waveshaper` | `dsp/include/krate/dsp/primitives/waveshaper.h` |
| Wavefolder | `Krate::DSP::Wavefolder` | `dsp/include/krate/dsp/primitives/wavefolder.h` |
| DC Blocker | `Krate::DSP::DCBlocker` | `dsp/include/krate/dsp/primitives/dc_blocker.h` |
| Filter | `Krate::DSP::SVF` | `dsp/include/krate/dsp/primitives/svf.h` |
| Filter envelope | `Krate::DSP::ADSREnvelope` | `dsp/include/krate/dsp/primitives/adsr_envelope.h` |
| Pitch envelope | `Krate::DSP::MultiStageEnvelope` | `dsp/include/krate/dsp/processors/multi_stage_envelope.h` |

All pre-allocated, constructed in-place inside `ToneShaper`.

## Test coverage requirements

1. **Bypass identity** — 1 kHz sine at 0 dBFS through all-bypassed Tone Shaper; assert RMS difference from input ≤ −120 dBFS (FR-045).
2. **Filter envelope sweep** — set filter env amount = 1.0, attack = 5 ms, decay = 100 ms; process pink noise through LP; measure spectral centroid trajectory; assert it follows an attack-then-decay curve.
3. **Drive harmonic generation** — process 1 kHz sine through Drive = 1.0; assert THD increases monotonically from zero drive; assert peak ≤ 0 dBFS.
4. **Wavefolder odd harmonics** — process 1 kHz sine through Fold = 1.0; assert 3rd, 5th, 7th harmonic magnitudes increase.
5. **Pitch envelope glide** — SC-009 test described above.
6. **Pitch envelope disabled** — with `pitchEnvTimeMs_ = 0`, assert `processPitchEnvelope()` returns the Size-derived body fundamental and does NOT interpolate.
7. **Zero-duration edge case** — set `pitchEnvTimeMs_ = 0.001 ms`; assert no divide-by-zero, no NaN, no Inf.
8. **Parameter atomic updates** — set parameters from another thread while audio thread processes; assert no data race via ThreadSanitizer in debug builds. Note: ThreadSanitizer coverage for parameter setters is deferred to Phase 3 CI hardening — single-voice Phase 2 has no concurrent parameter writers (all writes come through `processParameterChanges()` on the audio thread).
9. **Allocation detector** — every method zero allocation.
