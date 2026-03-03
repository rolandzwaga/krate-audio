# Harmonic‑Driven Oscillator — Full Concept & DSP Architecture

## 1. Core Idea

Create a **VST instrument** whose oscillator is not internally defined (sine/saw/etc.) but instead **derived from the harmonic structure of another sound source**.

The source can be:

1. A loaded audio sample
2. A live sidechain input

**Input assumption: monophonic.** The analysis pipeline assumes a single dominant fundamental frequency (F0). This is an architectural decision, not a limitation — the instrument is designed for monophonic or dominant‑pitch source material (solo instruments, voice, monophonic synth lines, sustained tones). Polyphonic input (chords, piano with sustain pedal, dense pads) will produce unreliable harmonic models because the harmonic sieve cannot disambiguate overlapping harmonic series. Polyphonic decomposition is a potential future extension but is explicitly out of scope for v1.

The plugin analyzes incoming audio in real time (or offline for samples), extracts harmonic information, and uses that data to *drive synthesis* rather than merely resynthesize audio.

This is **analysis‑driven synthesis**, not spectral playback. The paradigm traces to Jean‑Claude Risset's trumpet resynthesis at Bell Labs (1966), where acoustic analysis yielded oscillator parameters for a playable instrument — the first time a sound's timbral structure was extracted and re‑used for synthesis rather than playback [Risset 1966].

---

## 2. Conceptual Philosophy

Traditional synthesis:

```
Oscillator → Filter → Envelope → Output
```

This instrument:

```
External Sound → Harmonic Analysis → Harmonic Model → Oscillator Generator → Musical Control → Output
```

Key shift:

> The oscillator becomes a *function of another sound*.

The source acts as a **spectral DNA provider**.

---

## 3. High‑Level Goals

### Musical Goals
- Create sounds that inherit timbral identity without copying audio
- Allow cross‑synthesis that remains playable
- Preserve harmonic character while enabling pitch control
- Enable evolving, organic oscillators

### Technical Goals
- Stable real‑time tracking
- Low latency live mode
- Predictable musical behavior
- Graceful degradation on noisy input

---

## 4. Input Modes

**Both modes expect monophonic source material** — a single dominant pitch at any given time. The system will track the strongest fundamental and build a harmonic model around it. If multiple pitches are present, the F0 tracker will lock onto the dominant one; secondary pitches will be ignored or misassigned as partials, producing unpredictable results.

### A. Sample Mode
- Load audio file
- Offline analysis possible
- High accuracy allowed
- Precomputed harmonic model

### B. Live Sidechain Mode
- Continuous real‑time analysis
- Low latency constraints
- Adaptive tracking required
- Must tolerate noise and transients

---

## 5. DSP Architecture Overview

```
Audio Input
     ↓
Pre‑Processing
     ↓
Fundamental Detection
     ↓
Partial Tracking
     ↓
Harmonic Model Builder
     ↓
Oscillator Bank
     ↓
Musical Control Layer
     ↓
Output Engine
```

Each stage is independent and testable.

### KrateDSP Layer Mapping

| Stage | Component | KrateDSP Layer | Notes |
|-------|-----------|---------------|-------|
| 1 | Pre‑processing | Layer 0–1 (core/primitives) | DC blocker, HPF already exist in KrateDSP |
| 2 | F0 detection | Layer 2 (processors) | New component, uses FFT from Layer 1 |
| 3 | Partial tracking | Layer 2 (processors) | New component, depends on FFT |
| 4 | Harmonic model | Layer 3 (systems) | Orchestrates analysis pipeline, dual timescale blending |
| 5 | Oscillator bank | Layer 2 (processors) | Adapt from ParticleOscillator pattern (Gordon‑Smith) |
| 6 | Musical control | Layer 3 (systems) | Freeze, morph, harmonic filtering |
| — | Full engine | Layer 4 (effects) or plugin‑local | Wires all stages into a playable instrument |

Layer rules apply: each component may only depend on layers below it. The analysis path (Stages 1–4) and synthesis path (Stages 5–6) share Layer 1 primitives (FFT, filters) but are otherwise independent, enabling separate testing.

---

## 6. Stage 1 — Pre‑Processing

Purpose: prepare signal for reliable harmonic extraction.

### Steps
- DC offset removal
- High‑pass filtering (~20–40 Hz)
- Optional noise gate
- Transient suppression / smoothing
- Windowing for spectral analysis

### Design Insight
Analysis signal ≠ playback signal.
A "cleaned" analysis stream improves stability dramatically.

### Scientific Basis
DC removal and high‑pass filtering are indispensable preprocessing for spectral analysis — DC offset causes spectral leakage that corrupts all FFT bins [Smith 2007, Ch. "DC Blocker"]. Transient suppression before harmonic analysis is standard in SMS systems, where transient energy is separated into the stochastic residual [Serra & Smith 1990]. Onset detection for transient identification is surveyed in [Bello et al. 2005].

---

## 7. Stage 2 — Fundamental Detection (F0 Tracking)

Goal: estimate pitch continuously.

### Algorithm Selection

**Primary: YIN** [de Cheveigné & Kawahara 2002]
- Well-documented, widely implemented real-time pitch detector
- Cumulative mean normalized difference function (CMNDF) substantially reduces octave errors that plague plain autocorrelation — the original paper reports gross error rates dropping from ~17% (raw autocorrelation) to ~0.5% (full YIN), approximately 3x lower than the best competing methods at the time
- Difference function can be accelerated via FFT using the Wiener‑Khinchin theorem, reducing per‑frame cost from O(N²) to O(N log N) [de Cheveigné & Kawahara 2002, Section III.E] (leverages existing pffft backend)
- Good balance of accuracy and computational cost — benchmarks show YIN can handle ~38 parallel channels in real time [sevagh/pitch-detection benchmarks]
- Note: pYIN [Mauch & Dixon 2014] adds a probabilistic HMM layer atop YIN to further reduce remaining octave errors; worth considering if vanilla YIN proves insufficient on target material

**Secondary: McLeod Pitch Method (MPM)** [McLeod & Wyvill 2005]
- Uses Normalized Squared Difference Function (NSDF), a different normalization than YIN's CMNDF
- Can operate on as few as two periods of signal, enabling smaller analysis windows — potentially better for rapid pitch changes (vibrato) and instruments with strong high-harmonic content (violin)
- Known minimum frequency limitation (~80 Hz floor in common implementations)
- Worth benchmarking against YIN on target source material
- Can serve as fallback or user-selectable option

**Rejected:**
- *Autocorrelation* — YIN's CMNDF + absolute threshold achieves ~34x lower error rates than raw autocorrelation at similar computational cost (with FFT acceleration); redundant to include both
- *SWIPE* [Camacho & Harris 2008] — template‑matching pitch estimator requiring multiple FFTs per candidate frequency; standard implementation handles only ~7 channels in real time vs YIN's ~38 [CMMR 2025 benchmarks]. RT‑SWIPE [Meier et al. 2025] closes the gap to ~20 channels but still at significant throughput penalty. Unnecessary for this application.

### Output

```
struct Fundamental {
  frequency: Hz
  confidence: 0..1
}
```

### Important Behaviors
- Confidence gating
- Hysteresis to prevent jitter
- Hold previous F0 during instability

This stage stabilizes the entire system.

---

## 8. Stage 3 — Partial Detection & Tracking

Detect individual harmonics and follow them across time. The foundational approach was established by McAulay & Quatieri [1986] for speech and extended to musical sounds by Serra & Smith [1987, 1990].

### Process
1. Spectral analysis (see multiresolution strategy below)
2. Peak detection via parabolic interpolation on spectral maxima [Smith & Serra 1987, PARSHL]
3. Map peaks near integer multiples of F0 (harmonic sieve)
4. Track peaks frame‑to‑frame using frequency proximity matching [McAulay & Quatieri 1986]

### Multi‑Resolution Analysis Strategy

A single FFT window cannot satisfy both low-frequency resolution and low latency — this is a consequence of the Heisenberg‑Gabor uncertainty principle: Δt · Δf ≥ 1 for the STFT [Gabor 1946]. At 44.1kHz, a 512-sample window (~11.6ms) yields frequency resolution of Δf = fs/N ≈ 86 Hz per bin — insufficient to reliably distinguish harmonics of a 100 Hz fundamental (the second harmonic at 200 Hz is only ~1.3 bins from the third at 300 Hz). A 2048-sample window (~46.4ms, Δf ≈ 21.5 Hz) provides adequate resolution but adds unacceptable latency for live performance.

**Solution: Multiresolution STFT** [Levine 1998; Smith 2011, Ch. "Multiresolution STFT"]

| Band | Window Size | Latency | Purpose |
|------|------------|---------|---------|
| Low (F0 + partials 1–4) | 2048–4096 samples | ~46–93ms | Accurate F0 and low partial resolution |
| High (partials 5+) | 512–1024 samples | ~11–23ms | Low-latency upper harmonic tracking |

- The long window runs at a slower update rate (overlapping) and feeds the F0 tracker and low partial amplitudes
- The short window runs at full rate and tracks upper harmonics with minimal latency
- Results are merged in the Harmonic Model Builder (Stage 4), which already operates on dual timescales
- This approach is a simplified two-band version of the general multiresolution STFT technique demonstrated by Levine at Stanford CCRMA [Levine 1998], which used five FFT passes with window lengths from 4–64ms across frequency bands from 0–4050 Hz. Related: the Constant‑Q Transform [Brown 1991] formalizes the same principle with logarithmically spaced frequency bins.
- Existing KrateDSP STFT/overlap-add infrastructure supports this directly

### Per‑Partial Data

```
struct Partial {
  index
  frequency
  amplitude
  phase
  stability
}
```

### Partial Count Management

A hard cap on active partials is required. Rich source material may produce far more spectral peaks than the oscillator bank can (or should) synthesize. Without a cap and selection strategy, partial assignment becomes unpredictable and frame‑to‑frame churn degrades timbral stability.

**Hard cap:** `maxPartials = 48`

This provides rich timbre while keeping the oscillator bank within CPU budget (~0.38% CPU for 64 oscillators benchmarked; 48 is comfortably within that).

**Selection heuristic:** When detected peaks exceed `maxPartials`, rank by:

```
score_n = energy_n * stability_n
```

- `energy_n`: amplitude of the partial (preserves timbral importance — louder partials matter more)
- `stability_n`: tracking stability from the partial tracker (rejects noise peaks and transient artifacts)

Select the top `maxPartials` peaks by score. Ties broken by lower harmonic index (prefer fundamentally‑related partials).

**Hysteresis:** A partial currently in the active set should not be immediately dropped if it falls to rank `maxPartials + 1` in one frame. Apply a grace period (e.g. 3–5 frames) before replacing it, to prevent rapid partial swapping that causes timbral instability.

### Key Challenges
These are the three canonical challenges in partial tracking literature [Nunes & Esquef 2008; Lagrange et al. 2007]:
- **Peak jumping** — tracking algorithm incorrectly assigns a peak from one partial to an adjacent partial's track, especially during vibrato or crossing partials
- **Missing harmonics** — a partial present in one frame may be absent in the next due to masking or amplitude dips; the algorithm must decide whether to terminate the track or interpolate across the gap
- **Noise peaks** — spectral noise produces spurious local maxima indistinguishable from real partials at low SNR

### Solutions
- **Frequency proximity constraints** — the foundational heuristic from McAulay & Quatieri [1986]: peaks in adjacent frames are matched if frequency difference falls below a threshold
- **Temporal smoothing** — enforcing that partial parameters evolve slowly over time; formalized with linear prediction (AR models) by Lagrange et al. [2003, 2005, 2007] at IRCAM, replacing ad‑hoc frequency thresholds with principled frequency trajectory prediction
- **Kalman‑style prediction filters** — state‑space models for partial frequency/amplitude evolution, allowing robust tracking through vibrato and noise; demonstrated by Satar‑Boroujeni & Shafai [2005]. Linear prediction [Lagrange et al. 2003] is a simpler and more commonly implemented variant of the same predictive approach.

---

## 9. Stage 4 — Harmonic Model Builder

Convert raw measurements into a stable musical representation.

### Harmonic Frame

The spectral centroid and brightness descriptors are among the best‑established timbral features in psychoacoustics. Grey [1977] identified spectral energy distribution (effectively spectral centroid) as the primary perceptual axis of timbre. Krimphoff, McAdams & Winsberg [1994] formalized the spectral centroid as an acoustic correlate of perceived brightness. Schubert & Wolfe [2006] confirmed strong correlation (r = 0.85). Computational formulations standardized by Peeters [2004, IRCAM].

```
struct HarmonicFrame {
  f0
  partials[]
  spectral_centroid    // amplitude-weighted mean frequency [Grey 1977; Peeters 2004]
  brightness           // perceptual correlate of spectral centroid [Schubert & Wolfe 2006]
  noisiness
  globalAmplitude      // smoothed RMS or perceptual loudness of the source
  normalizedPartials[] // partials with L2-normalized amplitudes (spectral shape only)
}
```

### Global Amplitude Model

The harmonic model must separate **timbral identity** (relative partial amplitudes) from **overall loudness** (amplitude envelope). Without this separation, quiet source passages produce weak oscillator output and loud passages produce loud output — but when played from MIDI, the amplitude behavior feels wrong because velocity has no independent loudness control.

**Extraction:**

```
globalAmplitude = smoothedRMS(inputSignal)   // or perceptual loudness (e.g. ITU-R BS.1770)
normalizedAmp_n = A_n / sqrt(Σ A_n²)        // L2 normalization preserves spectral shape
```

- `normalizedAmp_n` encodes the **timbral identity** — the relative balance of partials, independent of overall level
- `globalAmplitude` encodes the **loudness contour** — how loud the source is over time

**Playback:**

```
outputAmp_n = normalizedAmp_n * globalAmplitude * midiVelocityScale
```

- MIDI velocity scales `globalAmplitude`, not individual partial amplitudes
- Spectrum (timbre) remains stable regardless of velocity
- This makes the instrument *playable* rather than merely *reactive*

**Snapshot normalization:** When freezing a harmonic frame for Harmonic Memory, always store L2‑normalized amplitudes. This ensures that morphing between two snapshots captured at different loudness levels produces smooth timbral interpolation without volume jumps.

### Dual Timescales

Fast layer:
- captures articulation

Slow layer:
- captures timbral identity

Blend:

```
model = lerp(slowModel, fastFrame, responsiveness)
```

This prevents chaotic motion while preserving life. The approach is a form of multi‑timescale exponential moving average (EMA) blending — a general signal processing pattern with precedent in noise estimation [Cohen & Berdugo 2002, MCRA] and double exponential smoothing [Holt 1957]. Not a single named canonical technique, but a well‑understood design pattern applied to audio feature streams.

---

## 10. Stage 5 — Harmonic Oscillator Bank

Core innovation.

Instead of replaying audio, generate oscillators using the standard harmonic additive synthesis formula (Fourier's theorem applied to synthesis) [Roads 1996; Smith 2011]:

```
output = Σ A_n * sin(2π * relativeFreq_n * targetPitch * t + φ_n)
```

Where amplitudes/phases come from the harmonic model and `relativeFreq_n` is the partial's frequency ratio relative to the fundamental, captured from the source (equals `n` for perfect harmonics, `n + deviation` for inharmonic sources).

### Inharmonic Deviation Model

Traditional additive synthesis forces partials to exact integer multiples of the fundamental (`freq_n = n * targetPitch`). This discards the natural inharmonicity present in many acoustic sources — piano strings exhibit stiffness‑driven sharp partials [Fletcher 1964], bells and metallic objects have strongly non‑integer partial ratios, and even bowed strings deviate slightly from perfect harmonics.

The partial tracker (Stage 3) already measures actual partial frequencies, which the serialization format stores as `relativeFreq_n = freq_n / f0`. The deviation from perfect harmonicity is:

```
deviation_n = relativeFreq_n - n
```

**Playback with inharmonicity preservation:**

```
freq_n = (n + deviation_n * inharmonicityAmount) * targetPitch
```

- `inharmonicityAmount = 0.0`: pure harmonic synthesis (freq_n = n * targetPitch)
- `inharmonicityAmount = 1.0`: faithfully reproduces the source's inharmonic character
- Intermediate values blend between harmonic and captured inharmonicity

**User control: Inharmonicity Amount** (0–100%, default 100%)

This is a **core model feature**, not an optional enhancement. The deviation data is captured for free during analysis and stored in every snapshot. The only cost is one extra multiply per partial during frequency calculation.

The existing "Harmonic stretching" enhancement (below) remains available as a *synthetic* alternative — it applies piano‑model inharmonicity using a single coefficient `B` without needing source analysis.

### Implementation: Gordon‑Smith Magic Circle Phasor

Each partial uses the Modified Coupled Form (MCF) oscillator, introduced by Gordon & Smith [1985] and documented extensively in Smith's *Physical Audio Signal Processing* [Smith 2010, Ch. "Digital Sinusoid Generators"]. Already proven in KrateDSP's ParticleOscillator:

```
epsilon = 2 * sin(π * freq / sampleRate)
s_new = s + epsilon * c
c_new = c - epsilon * s_new    // uses updated s — this cross-feed is the MCF's defining feature
```

- **Cost per partial:** 2 multiplies + 2 adds per sample (no trig, no table lookup) — confirmed by [Smith & Cook 1992]
- **Amplitude-stable:** the MCF's effective transition matrix has determinant 1 + epsilon^2, which for audio frequencies is negligibly above 1 (e.g., 440 Hz at 44.1 kHz: epsilon ~ 0.063, det ~ 1.004). Critically, stability does not depend on maintaining cos^2 + sin^2 = 1 under quantization — the MCF uses only one coefficient (epsilon), avoiding the quantization instability of the standard coupled form [Gordon & Smith 1985; Smith 2010]. No drift correction needed in practice for float32 at audio rates.
- **Benchmarked:** 64 oscillators at ~0.38% CPU (44.1kHz) — well within budget for 32–64 partials
- **Frequency update:** recalculate epsilon when target pitch or partial frequency changes
  - **MIDI‑driven pitch changes** (note‑on, pitch bend): apply new epsilon immediately — the Gordon‑Smith resonator transitions smoothly to the new frequency without phase discontinuity, since updating epsilon simply changes the oscillation rate of the existing state. No per‑sample frequency smoothing is needed for instantaneous pitch jumps; the oscillator's continuous state (sin/cos pair) ensures a click‑free transition.
  - **Optional portamento/glide:** when enabled, epsilon is interpolated over a user‑defined glide time (e.g. 5–200ms) using per‑sample smoothing. This produces a continuous frequency sweep between notes — distinct from the analysis‑frame smoothing, which handles timbral evolution.
  - **Analysis‑driven frequency changes** (harmonic model updates): epsilon changes are inherently gradual since they track the source's spectral evolution at the analysis frame rate.
- **Amplitude update:** multiply output by model amplitude, smoothed to avoid clicks

### Anti‑Aliasing

When MIDI target pitch is higher than the source F0, upper partials are pushed toward (or above) Nyquist. For example, a 48‑partial model from a 100 Hz source would place partial 48 at 4800 Hz — but playing MIDI note C6 (1047 Hz) pushes it to ~50,000 Hz, far above Nyquist (22,050 Hz at 44.1 kHz). Without anti‑aliasing, these partials alias back into the audible range as inharmonic artifacts.

**Strategy: soft rolloff approaching Nyquist**

```
nyquist = sampleRate / 2
fadeStart = 0.8 * nyquist    // begin attenuation
fadeEnd   = nyquist           // fully muted

for each partial n:
    partialFreq = relativeFreq_n * targetPitch
    if (partialFreq >= fadeEnd)
        antiAliasGain = 0.0
    else if (partialFreq >= fadeStart)
        antiAliasGain = 1.0 - (partialFreq - fadeStart) / (fadeEnd - fadeStart)
    else
        antiAliasGain = 1.0

    effectiveAmp_n = amp_n * antiAliasGain
```

- **Soft rolloff** avoids timbral discontinuities — a hard cutoff at Nyquist would cause audible brightness jumps as the player moves up the keyboard
- Recalculated whenever target pitch changes (MIDI note-on, pitch bend) — not per sample
- Effectively reduces active partial count for high notes, which is physically correct (a piano at C7 has far fewer audible harmonics than at C2)
- Computationally negligible — one multiply per partial on pitch change

### Properties
- Fully pitchable
- MIDI controllable
- Independent from source pitch

### Phase Management

When the harmonic model updates, oscillator phases must transition cleanly to avoid clicks.

**Default: Phase‑Continuous** [McAulay & Quatieri 1986; Serra & Smith 1990]
- Keep each oscillator's phase accumulator running; only update epsilon (frequency) and amplitude
- Smoothest for the common case: sustained tones with gradual timbral evolution
- Phase continuity is a fundamental requirement in sinusoidal modeling — established in the McAulay‑Quatieri method, which uses cubic phase interpolation between analysis frames
- Amplitude changes applied via per-sample smoothing (one‑pole lowpass filter: `y[n] = a * x[n] + (1-a) * y[n-1]`) to prevent discontinuities — an industry‑standard technique [Zölzer 2011, DAFX]

**On Large Spectral Discontinuities: Crossfade**
- Triggered when the harmonic model detects a new onset or major spectral jump (e.g. F0 confidence drops then recovers on a different pitch)
- Run old and new oscillator states in parallel for a short crossfade window (~2–5ms)
- Based on the McAulay‑Quatieri "birth and death" model for sinusoidal tracks [McAulay & Quatieri 1986]: when a partial appears or disappears, amplitude is faded in/out smoothly
- Costs 2x oscillators during the crossfade, but these events are infrequent
- Detection heuristic: threshold on frame-to-frame spectral distance or F0 jump exceeding a semitone

**Rejected: Blanket Phase Reset**
- Resetting phase on every model update introduces a waveform derivative discontinuity, perceptually equivalent to an impulse — causes audible clicks
- Only acceptable as part of the crossfade path (new oscillators initialized with reset phase, faded in)

### Optional Enhancements
- **Phase reset on note‑on** — on MIDI note‑on, reinitialize all oscillator phases to a deterministic state: either zero phase (`φ_n = 0`) or locked‑to‑fundamental (`φ_n = n * φ_0`). This produces a consistent attack waveform shape on every key press, making the instrument feel tighter for melodic playing. Distinct from blanket phase reset on model update (which is rejected above) — this only triggers on MIDI key events, not on analysis frame updates. User‑selectable; default off (phase‑continuous is the default behavior).
- **Phase randomization** — random initial phases produce smoother, less transient‑like onsets; phase relationships directly affect perceived roughness [ICMC 2004, "Phase Models to Control Roughness in Additive Synthesis"]
- **Detune spread** — slight frequency offsets to partials create perceptual richness and chorus‑like effects; standard additive technique [Roads 1996]
- **Harmonic stretching** — models real acoustic inharmonicity of stiff strings: `f_n = n * f0 * sqrt(1 + B * n^2)` where B is the inharmonicity coefficient (typically 0.0001–0.004 for piano). Based on Fletcher's analysis of piano string vibration [Fletcher 1964] and the empirically measured Railsback stretch [Railsback 1938]
- **Partial dropout** — selective muting of partials for timbral variation

---

## 11. Stage 5B — Residual / Noise Model

The sinusoidal oscillator bank captures harmonic (deterministic) content, but the perceptual "life" of a sound — breath, bow noise, attack transients, consonant texture — lives in the **residual** (stochastic component): everything the harmonics don't explain. Without it, resynthesized sounds feel sterile. This is the foundational "deterministic plus stochastic" decomposition introduced by Serra in his 1989 Stanford PhD dissertation and published as Spectral Modeling Synthesis (SMS) [Serra 1989; Serra & Smith 1990].

### Analysis (Sample Mode)

```
residual = originalSignal - resynthesizedHarmonics
```

1. Subtract the harmonic resynthesis from the original input — the canonical SMS method [Serra & Smith 1990; Smith 2011, "Sines+Noise Modeling"]
2. Analyze the residual's spectral envelope using a low‑resolution piecewise‑linear approximation (8–16 breakpoints, not full FFT resolution) — standard SMS practice; Serra & Smith use line‑segment approximation to the upper spectral envelope, noting it is "more flexible than LPC" [Serra & Smith 1990]
3. Store envelope shape per frame alongside the harmonic model

**Critical: use tracked partial frequencies for subtraction.** The resynthesized harmonics used for subtraction must use the **actual measured partial frequencies** from the partial tracker (Stage 3), not idealized integer multiples of F0 (`n * f0`). If F0 estimation is slightly off (which it always is), idealized frequencies won't align with the real spectral peaks, producing inflated residual energy around misaligned partials. This causes the noise model to behave inconsistently, especially during pitch bends or vibrato. Using tracked frequencies ensures tight cancellation and a clean residual.

### Analysis (Live Sidechain Mode)

- Full subtraction requires one-frame latency (need the harmonic resynthesis to subtract)
- Alternative: **spectral coring** — estimate residual energy from spectral bins that fall between harmonic peaks (effectively zeroing out bins where harmonics are detected rather than performing phase‑aligned subtraction). Avoids the CPU cost and phase‑alignment sensitivity of full subtraction while providing a usable residual estimate.
- Less accurate but zero additional latency

### Resynthesis

- Generate white noise
- Shape with the stored spectral envelope (band-pass filter bank or FFT-domain multiplication)
- Scale by a per-frame energy estimate from the residual analysis

### Per‑Frame Data

```
struct ResidualFrame {
  bandEnergies[NUM_BANDS]   // spectral envelope of the residual
  totalEnergy               // overall residual level
  transientFlag             // true if onset/transient detected
}
```

### User Controls

- **Harmonic / Residual Mix**: blend between the two components (default: both at unity)
- **Residual Brightness**: tilt the noise envelope toward high or low frequencies
- **Transient Emphasis**: boost residual energy during detected transient frames for punchier attacks

---

## 12. Stage 6 — Musical Control Layer

Transforms analysis into expressive synthesis.

### Controls

#### Harmonic Freeze
Lock current harmonic snapshot.

#### Morphing
Blend between two harmonic states.

#### Harmonic Filtering
Scale partial ranges:

```
A_n *= harmonicMask(n)
```

#### Stability vs Responsiveness
Tradeoff between tracking accuracy and musical smoothness.

#### Residual Control
See Stage 5B — harmonic/residual mix, brightness, and transient emphasis are controlled here.

---

## 13. Real‑Time Stability Strategies

Essential for live sidechain mode.

- Confidence weighting — standard in all major pitch trackers (YIN, pYIN, CREPE, SPICE) [de Cheveigné & Kawahara 2002; Mauch & Dixon 2014; Kim et al. 2018; Gfeller et al. 2020]
- Median filtering of amplitudes — nonlinear technique that rejects impulsive outliers while preserving step edges; used in spectral noise floor estimation [Hirsch & Ehrlicher 1995] and partial tracking systems [Klingbeil 2009]
- Pitch hysteresis
- Frame interpolation
- Dropout recovery
- Predictive continuation of partials [Lagrange et al. 2007]

### Confidence‑Gated Freeze

Holding the last reliable estimate when confidence drops is standard practice in pitch tracking — every major pitch estimator (YIN, pYIN, SPICE [Gfeller et al. 2020], CREPE [Kim et al. 2018]) includes a voicing confidence score that gates output. In partial tracking, the McAulay‑Quatieri "death" mechanism holds tracks for a brief interpolation period before termination [McAulay & Quatieri 1986].

When F0 tracking confidence drops below a threshold (e.g. 0.3), automatically freeze the last known‑good harmonic frame:

```
if (f0.confidence < freezeThreshold) {
    currentModel = lastGoodModel;   // hold steady
} else {
    lastGoodModel = currentModel;   // update reference
}
```

- On recovery (confidence rises above threshold + hysteresis margin), crossfade from the frozen frame back to live tracking over ~5–10ms
- Prevents glitchy output during silence gaps, noisy passages, or transient confusion
- Musically equivalent to a brief sustain — far preferable to unstable oscillator behavior
- Nearly free to implement since freeze infrastructure is already needed for the Harmonic Freeze control
- **Auto‑freeze fade‑out:** when the frozen frame persists beyond a configurable hold time, the oscillator bank amplitude can optionally decay to silence rather than holding indefinitely. This is a musical control parameter (exposed in Stage 6), not a stability mechanism — the freeze itself holds the last good frame unconditionally, and the fade is an amplitude envelope applied on top. User control: **Auto‑Freeze Hold Time** (100ms–∞, default ∞ = hold indefinitely).

Goal: musical continuity over analytic accuracy.

### CPU Budget Awareness

The full analysis + synthesis pipeline involves multiple concurrent DSP stages:

| Stage | Cost Driver | Notes |
|-------|------------|-------|
| YIN F0 tracker | FFT‑accelerated autocorrelation per frame | O(N log N) per frame |
| Dual STFT | Two overlapping FFT passes (short + long window) | Dominant analysis cost |
| Partial tracker | Peak detection + frame‑to‑frame matching | Scales with peak count |
| Oscillator bank | 48 MCF oscillators per sample | Benchmarked: ~0.38% CPU for 64 |
| Noise model | FFT‑domain envelope shaping per frame | Comparable to one STFT pass |

The oscillator bank is well within budget. The **analysis pipeline** (YIN + dual STFT + partial tracking) running continuously in live sidechain mode is the primary CPU concern.

**Available quality levers** (for future implementation if profiling reveals bottlenecks):

- **Partial count reduction** — lower `maxPartials` from 48 to 24 or 16; direct impact on oscillator cost and partial tracking overhead
- **Analysis update rate** — reduce STFT hop rate (e.g. update every 2nd or 4th buffer instead of every buffer); trades tracking responsiveness for CPU
- **Window size reduction** — drop the long STFT window entirely (equivalent to forcing low‑latency mode); reduces frequency resolution but halves analysis cost
- **F0 tracker duty cycling** — run YIN every Nth frame and interpolate between updates; F0 typically changes slowly enough to tolerate this

**Design principle:** Sample mode is inherently cheaper (analysis is precomputed, only the oscillator bank runs in real time). Live sidechain mode is the demanding case. Specific quality tier definitions are deferred to implementation and profiling — premature optimization without measurements would constrain the design unnecessarily.

---

## 14. Latency Considerations

Latency sources:
- FFT window size
- pitch estimation buffering
- smoothing filters

### Latency Perception Thresholds

Below 10ms is generally imperceptible, 10–20ms is the threshold range where skilled players begin to notice, above 30ms is consistently disruptive [Turchet et al. 2018; Wessel & Wright 2002]. Instrument‑specific tolerances: vocalists <3ms, drummers <6ms, pianists <10ms, guitarists <12ms, keyboardists <20ms [AES 2024 JND study]. A physical lower bound exists for pitch detection — at least one full period is required (25ms for 40 Hz bass, 10ms for 100 Hz vocal range) [Derrien 2014].

### Mode Definitions

These two modes reflect real physical constraints. The multi‑resolution STFT strategy (Section 8) cannot deliver both low latency and high frequency resolution simultaneously — this is a direct consequence of the Heisenberg–Gabor uncertainty principle. The design must be explicit about what each mode delivers.

**Low‑Latency Mode (Live Sidechain)**

| Parameter | Value |
|-----------|-------|
| Analysis window | 512–1024 samples only (~11–23 ms at 44.1 kHz) |
| Effective latency | ~15–25 ms (analysis + smoothing) |
| Minimum detectable F0 | ~80–100 Hz (insufficient window length for lower pitches) |
| Use case | Live performance, real‑time sidechain tracking |

- Uses only the short window from the multi‑resolution strategy
- F0 tracker must be configured with appropriate minimum frequency
- Upper harmonics tracked with good resolution; low partials (1–4) have reduced accuracy
- Suitable for voice, guitar, most melodic instruments above ~E2

**High‑Precision Mode (Sample Analysis / Non‑Critical Latency)**

| Parameter | Value |
|-----------|-------|
| Analysis window | Full multi‑resolution (512–4096 samples) |
| Effective latency | ~50–100 ms |
| Minimum detectable F0 | ~40 Hz (bass instruments, low piano) |
| Use case | Sample analysis, studio use where latency is acceptable |

- Uses both short and long windows from the multi‑resolution strategy
- Long window runs at slower overlapping update rate, merged in Stage 4
- Full frequency range with accurate low‑partial resolution
- Precomputed analysis in sample mode eliminates latency concern entirely

**Note:** Sample mode analysis is effectively offline — the audio file is fed through the real‑time pipeline (per Section 17's "Real‑Time First" principle) but latency is irrelevant since results are precomputed before playback.

---

## 15. Creative Extensions (Discussed Ideas)

### Priority 1: Harmonic Memory
Store multiple harmonic snapshots as recallable presets. Capture a moment from a live mic or sample, freeze it, and play it as an oscillator from MIDI. Build this early; it validates the freeze and model storage infrastructure.

**Prior art:** The concept of storing harmonic analysis snapshots for playback as oscillator states has extensive precedent — NED Synclavier "timbre frame resynthesis" (early 1980s), Technos Acxel 1024‑oscillator resynthesizer (1987), Camel Audio/Apple Alchemy additive engine (2007/2015), and Rossum Electro Panharmonium freeze‑and‑play (2019). The differentiator here is the real‑time analysis → one‑button capture → MIDI‑playable workflow integration, not the snapshot concept itself.

#### Serialization Format

All snapshot data is stored in **normalized harmonic domain** — frequencies relative to F0, amplitudes L2‑normalized. Storing absolute Hz would break recall when the snapshot is played at a different pitch.

```
struct HarmonicSnapshot {
    f0Reference              // source F0 at capture time (informational, not used for playback)
    numPartials              // active partial count (≤ maxPartials)
    relativeFreqs[N]         // freq_n / f0 — equals n for perfect harmonics, n + deviation for inharmonic
    normalizedAmps[N]        // L2-normalized amplitudes (spectral shape, loudness-independent)
    phases[N]                // partial phases at capture (used only if phase reset mode is active)
    inharmonicDeviation[N]   // relativeFreq_n - n — the deviation from perfect harmonic (see Section 10)
    residualBands[M]         // spectral envelope of the residual (8–16 bands)
    residualEnergy           // overall residual level at capture
    globalAmplitude          // source loudness at capture (informational)
    spectralCentroid         // perceptual metadata — useful for UI display and morph sorting
    brightness               // perceptual metadata
}
```

- `relativeFreqs` and `inharmonicDeviation` are redundant (one derives from the other) but both are stored for clarity and fast access during playback
- `phases` are optional — only meaningful if the user enables phase‑reset‑on‑note‑on mode
- Morphing between two snapshots interpolates `normalizedAmps`, `relativeFreqs`, and `residualBands` independently
- Format should be binary for real‑time recall, with an optional human‑readable export (JSON/YAML) for debugging and preset sharing

### Priority 2: Harmonic Cross‑Synthesis
Carrier pitch controlled by MIDI, timbre from source. This is a form of **additive cross‑synthesis** — distinguished from the classic filter‑bank cross‑synthesis (vocoder approach) by operating on per‑partial frequency and amplitude trajectories rather than band‑averaged spectral envelopes [Smith 2011, Ch. "Cross‑Synthesis"]. Natural extension of the core architecture — essentially what the instrument already does, but explicitly framed as a performance feature.

### Priority 3: Stereo Partial Spread
The oscillator bank output is inherently monophonic — all partials sum to a single channel. A **Stereo Spread** control would distribute partials across the stereo field by assigning per‑partial pan positions, creating a wide spatial image from a single harmonic model.

**Implementation:** offset even‑numbered and odd‑numbered partials to opposite sides of the stereo field, scaled by a spread amount (0% = mono center, 100% = full L/R alternation). More sophisticated variants could use partial index modulo N for finer distribution or frequency‑based panning (low partials center, high partials wide). Cost: one pan coefficient per partial — negligible, since each partial already has an amplitude multiplier.

Architecturally trivial once the oscillator bank exists (each partial gains a pan position alongside its amplitude) but high perceptual impact — transforms the instrument from a mono reconstruction into a spatially rich sound source. Standard additive synthesis technique; used in Alchemy, Razor, and most modern additive synths.

### Priority 4: Evolution Engine
Slow drift through stored spectra. Requires Harmonic Memory as a prerequisite (need multiple stored snapshots to drift between). Powerful for ambient and cinematic sound design.

**Scientific basis:** Spectral morphing (smooth interpolation between spectral states) is a mature research area. Foundational work by Serra, Rubine & Dannenberg [1990] on "Analysis and Synthesis of Tones by Spectral Interpolation." Component‑matching across snapshots with unequal partial counts addressed by Tellman, Haken & Holloway [1995]. Modern approaches surveyed by Caetano & Rodet [2011, IRCAM]. Commercial implementations: Alchemy 4‑corner morph pad, Kyma MorphedSpectrum, Cameleon 5000 spectral morphing.

### Priority 5: Harmonic Modulators
Use LFOs to modulate individual partial groups. Straightforward to implement once the oscillator bank exposes per-partial amplitude control, which it already does.

### Priority 6: Multi‑Source Blending
Analyze multiple inputs and interpolate spectra. Most complex extension — requires running multiple analysis pipelines simultaneously. Defer until the single-source path is rock solid.

---

## 16. Why This Is Different From Vocoders

**Channel vocoder** [Dudley 1939, 1940]:
- Splits modulator through a bandpass filter bank (originally 10 bands); extracts per‑band amplitude envelopes
- Applies those envelopes to corresponding bands of a carrier signal
- Coarse spectral shape transfer — no individual harmonic tracking
- Carrier pitch is external (not extracted from modulator)

**Phase vocoder** [Flanagan & Golden 1966]:
- STFT‑based analysis/resynthesis — tracks amplitude and phase of each frequency bin
- Full spectral frame playback via inverse STFT (overlap‑add)
- Architecturally distinct from the channel vocoder; the basis of modern pitch‑shifting and time‑stretching tools

**This system:**
- Explicit per‑partial harmonic reconstruction — tracks individual partial frequencies and amplitudes [McAulay & Quatieri 1986; Serra & Smith 1990]
- Oscillator‑level additive synthesis — generates new audio from an abstract model, not from transformed spectral frames
- Pitch independent — harmonics are renormalized to a new fundamental (supplied by MIDI), not gated through bands

Closer to **additive resynthesis instrument** than effect processing. The key characteristic: analyzed timbre becomes a static or evolving model that is *synthesized* independently of the original audio. An effect requires the original signal; this instrument discards it after analysis. This instrument paradigm was established by Risset's trumpet resynthesis [1966] and commercialized by the Synclavier (1980s), Acxel (1987), and Alchemy (2007).

---

## 17. Development Roadmap (Conceptual)

### Architectural Principle: Real‑Time First

Design the **real‑time analysis pipeline first**. Sample mode should be implemented as "real‑time analysis of a looping buffer" — the same codepath with relaxed constraints (no latency pressure, clean input). This avoids building offline‑only assumptions into the analysis stages that later require rearchitecting for live use. One pipeline, two modes.

### Phases

1. Real‑time F0 tracking (core of the entire system)
2. Partial tracking on live input
3. Harmonic model builder with dual‑timescale smoothing
4. Oscillator bank with MIDI pitch control
5. Residual model (Stage 5B)
6. Sample mode (feed audio file through the real‑time pipeline)
7. Confidence‑gated freeze and stability hardening
8. Musical control layer (freeze, morph, harmonic filtering)
9. Harmonic Memory (snapshot capture and recall)
10. Creative extensions (evolution engine, modulators)

---

## 18. Key Research Areas

- sinusoidal modeling synthesis (SMS)
- spectral peak tracking
- additive synthesis optimization
- real‑time pitch detection
- perceptual smoothing models

## 18B. References

### Foundational SMS & Sinusoidal Modeling

1. **Serra, X.** (1989). *A System for Sound Analysis/Transformation/Synthesis based on a Deterministic plus Stochastic Decomposition*. PhD Dissertation, Stanford University.

2. **Serra, X. & Smith, J. O.** (1990). Spectral Modeling Synthesis: A Sound Analysis/Synthesis System Based on a Deterministic Plus Stochastic Decomposition. *Computer Music Journal*, 14(4), 12–24. DOI: 10.2307/3680788

3. **McAulay, R. J. & Quatieri, T. F.** (1986). Speech Analysis/Synthesis Based on a Sinusoidal Representation. *IEEE Transactions on Acoustics, Speech, and Signal Processing*, 34(4), 744–754. DOI: 10.1109/TASSP.1986.1164910

4. **Smith, J. O. & Serra, X.** (1987). PARSHL: An Analysis/Synthesis Program for Non-Harmonic Sounds Based on a Sinusoidal Representation. *Proceedings of ICMC 1987*, Champaign/Urbana, IL.

### Pitch Detection

5. **de Cheveigné, A. & Kawahara, H.** (2002). YIN, a fundamental frequency estimator for speech and music. *Journal of the Acoustical Society of America*, 111(4), 1917–1930.

6. **Mauch, M. & Dixon, S.** (2014). pYIN: A fundamental frequency estimator using probabilistic threshold distributions. *Proceedings of IEEE ICASSP 2014*, 659–663.

7. **McLeod, P. & Wyvill, G.** (2005). A Smarter Way to Find Pitch. *Proceedings of ICMC 2005*, Barcelona. University of Otago.

8. **Camacho, A. & Harris, J. G.** (2008). A sawtooth waveform inspired pitch estimator for speech and music. *Journal of the Acoustical Society of America*, 124(3), 1638–1652.

9. **Meier, P. et al.** (2025). Pitch Estimation in Real Time: Revisiting SWIPE with Causal Windowing. *17th International Symposium on CMMR*, London.

### Partial Tracking

10. **Lagrange, M., Marchand, S., Raspaud, M. & Rault, J.-B.** (2003). Enhanced Partial Tracking Using Linear Prediction. *Proceedings of DAFx-2003*.

11. **Lagrange, M., Marchand, S. & Rault, J.-B.** (2005). Tracking Partials for the Sinusoidal Modeling of Polyphonic Sounds. *Proceedings of IEEE ICASSP 2005*.

12. **Lagrange, M., Marchand, S. & Rault, J.-B.** (2007). Enhancing the Tracking of Partials for the Sinusoidal Modeling of Polyphonic Sounds. *IEEE Transactions on Audio, Speech, and Language Processing*, 15(5).

13. **Satar-Boroujeni, H. & Shafai, B.** (2005). Peak Extraction and Partial Tracking of Music Signals Using Kalman Filtering. *Proceedings of ICMC 2005*, Barcelona.

14. **Nunes, L. & Esquef, P. A. A.** (2008). Partial Tracking in Sinusoidal Modeling. *Proceedings of SIGMAP 2008*.

### Oscillator Design

15. **Gordon, J. W. & Smith, J. O.** (1985). A Sine Generation Algorithm for VLSI Applications. *Proceedings of ICMC 1985*, Vancouver, 244–251.

16. **Smith, J. O.** (2010). *Physical Audio Signal Processing*. W3K Publishing / CCRMA, Stanford. Online: https://ccrma.stanford.edu/~jos/pasp/

### Spectral Analysis

17. **Gabor, D.** (1946). Theory of Communication. *Journal of the Institution of Electrical Engineers*, 93(26), 429–457.

18. **Levine, S. N.** (1998). *Audio Representations for Data Compression and Compressed Domain Processing*. PhD Dissertation, Stanford University.

19. **Brown, J. C.** (1991). Calculation of a Constant Q Spectral Transform. *Journal of the Acoustical Society of America*, 89(1), 425–434.

20. **Smith, J. O.** (2011). *Spectral Audio Signal Processing*. W3K Publishing. Online: https://ccrma.stanford.edu/~jos/sasp/

### Timbre & Perception

21. **Grey, J. M.** (1977). Multidimensional Perceptual Scaling of Musical Timbres. *Journal of the Acoustical Society of America*, 61(5), 1270–1277.

22. **Krimphoff, J., McAdams, S. & Winsberg, S.** (1994). Caractérisation du timbre des sons complexes. II. *Journal de Physique*, 4(C5), 625–628.

23. **Schubert, E. & Wolfe, J.** (2006). Does Timbral Brightness Scale with Frequency and Spectral Centroid? *Acta Acustica*, 92(5), 820–825.

24. **Peeters, G.** (2004). A Large Set of Audio Features for Sound Description. IRCAM CUIDADO Project Report.

25. **Risset, J. C. & Wessel, D. L.** (1999). Exploration of Timbre by Analysis and Synthesis. In *The Psychology of Music* (2nd ed.), ed. Diana Deutsch, 113–168. Academic Press.

### Acoustic Inharmonicity

26. **Fletcher, H.** (1964). Normal Vibration Frequencies of a Stiff Piano String. *Journal of the Acoustical Society of America*, 36(1), 203–209.

27. **Railsback, O. L.** (1938). Scale Temperament as Applied to Piano Tuning. *Journal of the Acoustical Society of America*, 9(3), 274.

### Vocoder History

28. **Dudley, H.** (1939). The Vocoder. *Bell Laboratories Record*, 18(4), 122–126.

29. **Dudley, H.** (1940). Remaking Speech. *Journal of the Acoustical Society of America*, 11(2), 169–177.

30. **Flanagan, J. L. & Golden, R. M.** (1966). Phase Vocoder. *Bell System Technical Journal*, 45(9), 1493–1509.

### Spectral Morphing

31. **Serra, X., Rubine, D. & Dannenberg, R.** (1990). Analysis and Synthesis of Tones by Spectral Interpolation. *Journal of the Audio Engineering Society*, 38(3), 111–128.

32. **Tellman, E., Haken, L. & Holloway, B.** (1995). Timbre Morphing of Sounds with Unequal Numbers of Features. *Journal of the Audio Engineering Society*, 43(9), 678–689.

33. **Caetano, M. & Rodet, X.** (2011). Sound Morphing by Feature Interpolation. *Proceedings of IEEE ICASSP 2011*.

### Analysis Preprocessing & Stability

34. **Smith, J. O.** (2007). *Introduction to Digital Filters*. W3K Publishing. Ch. "DC Blocker."

35. **Bello, J. P. et al.** (2005). A Tutorial on Onset Detection in Music Signals. *IEEE Transactions on Speech and Audio Processing*, 13(5), 1035–1047.

36. **Cohen, I. & Berdugo, B.** (2002). Noise Estimation by Minima Controlled Recursive Averaging for Robust Speech Enhancement. *IEEE Signal Processing Letters*, 9(1), 12–15.

37. **Hirsch, H.-G. & Ehrlicher, C.** (1995). Noise Estimation Techniques for Robust Speech Recognition. *Proceedings of ICASSP 1995*.

### Latency Perception

38. **Turchet, L. et al.** (2018). Action-Sound Latency and the Perceived Quality of Digital Musical Instruments. *Music Perception*, 36(1), 109–128.

39. **Wessel, D. & Wright, M.** (2002). Problems and Prospects for Intimate Musical Control of Computers. *Computer Music Journal*, 26(3), 11–22.

40. **Derrien, O.** (2014). A Very Low Latency Pitch Tracker for Audio to MIDI Conversion. HAL Archives.

### Pioneer Work

41. **Risset, J. C.** (1966). Computer Study of Trumpet Tones. Bell Laboratories Technical Report.

42. **Risset, J. C.** (1969). An Introductory Catalog of Computer Synthesized Sounds. Bell Laboratories Technical Report.

### Textbooks

43. **Roads, C.** (1996). *The Computer Music Tutorial*. MIT Press. ISBN 0-262-68093-7.

44. **Zölzer, U.** (ed.) (2011). *DAFX: Digital Audio Effects*, 2nd ed. Wiley. ISBN 978-0-470-66599-2.

45. **Klingbeil, M.** (2009). *Spectral Analysis, Editing, and Resynthesis: Methods and Applications*. PhD Dissertation, Columbia University.

---

## 19. Design Principles Agreed During Discussion

- Musical stability > analytic correctness
- Analysis drives synthesis, never playback
- Separate fast vs slow timbral change
- Every DSP stage independently testable
- Oscillator must remain playable like a synth

---

## 20. Summary

The project is a **harmonic‑driven synthesizer**:

- analyzes external audio
- extracts harmonic structure
- converts structure into oscillator parameters
- produces a playable instrument whose timbre evolves from real sound.

It sits between:

- additive synthesis
- spectral modeling
- cross‑synthesis
- live analysis instruments

but is conceptually distinct from all of them.

