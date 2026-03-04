# Innexus — Harmonic Analysis & Synthesis Engine Roadmap

A step-by-step plan for implementing the Innexus additive synthesis instrument using the existing KrateDSP layered architecture.

## Executive Summary

Innexus is a new VST3 instrument plugin in the Krate Audio monorepo. Its oscillator is not internally defined (sine/saw/etc.) but **derived from the harmonic structure of another sound source** — analysis-driven synthesis in the tradition of Risset [1966] and Serra & Smith [1990]. The full DSP architecture is documented in `specs/Innexus_harmonic_oscillator_dsp_plan.md`.

**New Plugin:** `plugins/innexus/` (alongside Iterum, Disrumpo, Ruinae)

**Estimated New Components:**
- Layer 0 (Core): 0 new files (all exist)
- Layer 1 (Primitives): 1–2 new files (YIN pitch detector, possibly enhanced peak detection utils)
- Layer 2 (Processors): 2–3 new files (partial tracker, harmonic oscillator bank, noise resynthesizer)
- Layer 3 (Systems): 1–2 new files (harmonic model builder, analysis pipeline orchestrator)
- Layer 4 / Plugin-local: 1 new file (full Innexus engine wiring all stages)

**Milestones:**
- **Milestone 1 (Phases 1–9):** Core playable instrument — sample analysis → MIDI-driven oscillator bank ✅ COMPLETE
- **Milestone 2 (Phases 10–11):** Residual/noise model — deterministic + stochastic decomposition ✅ COMPLETE
- **Milestone 3 (Phase 12):** Live sidechain mode — real-time continuous analysis
- **Milestone 4 (Phases 13–14):** Musical control layer — freeze, morph, harmonic filtering
- **Milestone 5 (Phases 15–16):** Harmonic Memory — snapshot capture, recall, preset integration
- **Milestone 6 (Phases 17–21):** Creative extensions — cross-synthesis, stereo spread, evolution engine, modulators, multi-source blending
- **Milestone 7 (Phase 22):** Plugin UI — full VSTGUI interface

---

## Current State: What Already Exists

### Layer 0 (Core) — Utilities
| Component | File | Reusable For |
|-----------|------|--------------|
| dB/gain conversion | `db_utils.h` | Amplitude scaling, partial gain |
| Math constants (Pi, TwoPi) | `math_constants.h` | Coefficient calculations |
| NaN/Inf detection | `db_utils.h` | Stability checks |
| Denormal flushing | `db_utils.h` | Post-processing safety |
| Window functions | `window_functions.h` | STFT windowing (Hann, Blackman, Kaiser) |
| Phase utilities | `phase_utils.h` | Phase wrapping, increment calculation |
| Pitch utilities | `pitch_utils.h` | MIDI↔Hz, semitone conversion |
| Interpolation | `interpolation.h` | Linear, cubic, Hermite — parameter smoothing |
| Random (Xorshift32) | `random.h` | Phase randomization |
| SIMD spectral math | `spectral_simd.h` | Cartesian↔Polar bulk conversion (Google Highway) |
| Grain envelopes | `grain_envelope.h` | Crossfade envelopes for partial birth/death |

### Layer 1 (Primitives) — Building Blocks
| Component | File | Reusable For |
|-----------|------|--------------|
| FFT (pffft backend) | `fft.h` | YIN acceleration, spectral analysis |
| STFT (streaming) | `stft.h` | Multi-resolution analysis windows |
| OverlapAdd | `stft.h` | Noise resynthesis (future) |
| SpectralBuffer | `spectral_buffer.h` | Spectrum storage with cached polar |
| Spectral utilities | `spectral_utils.h` | Bin↔frequency, phase unwrapping |
| DC Blocker (1st/2nd order) | `dc_blocker.h` | Pre-processing |
| Biquad (TDF2) | `biquad.h` | HPF for pre-processing |
| SmoothedBiquad | `biquad.h` | Smoothed filter transitions |
| OnePoleSmoother | `smoother.h` | Amplitude smoothing, parameter smoothing |
| LinearRamp | `smoother.h` | Portamento/glide |
| SlewLimiter | `smoother.h` | Rate-limited parameter changes |
| PitchDetector | `pitch_detector.h` | Autocorrelation-based pitch (reference/fallback) |

### Layer 2 (Processors) — Signal Processors
| Component | File | Reusable For |
|-----------|------|--------------|
| AdditiveOscillator | `additive_oscillator.h` | IFFT-based additive (reference; Innexus uses MCF bank instead) |
| ParticleOscillator | `particle_oscillator.h` | Gordon-Smith MCF pattern (proven, benchmarked) |
| EnvelopeFollower | `envelope_follower.h` | Global amplitude tracking |
| PitchFollowerSource | `pitch_follower_source.h` | Pitch tracking control signal |
| MultiStageEnvelope | `multi_stage_envelope.h` | ADSR for voice amplitude |

### Existing Ruinae Synth Infrastructure
| Component | Location | Reusable For |
|-----------|----------|--------------|
| Voice management | `plugins/ruinae/src/engine/` | Polyphonic voice allocation pattern |
| Effects chain | `plugins/ruinae/src/engine/` | Post-oscillator FX pattern |
| Parameter helpers | `plugins/ruinae/src/parameters/` | Per-section parameter registration pattern |
| VST3 processor/controller | `plugins/ruinae/src/` | Plugin shell pattern |

---

## Implementation Phases

### Phase 1: Plugin Scaffold & Build Integration ✅

**Goal:** Create the Innexus plugin entry in the monorepo with an empty but buildable processor/controller pair.

```
Location: plugins/innexus/
Dependencies: VST3 SDK, shared plugin infrastructure
```

**Deliverables:**
- `plugins/innexus/CMakeLists.txt` — build target, linked to KrateDSP and shared infra
- `plugins/innexus/src/entry.cpp` — VST3 factory registration
- `plugins/innexus/src/plugin_ids.h` — GUIDs and initial parameter IDs
- `plugins/innexus/src/version.h` — version constants
- `plugins/innexus/src/processor/processor.h/.cpp` — empty AudioEffect, passes silence
- `plugins/innexus/src/controller/controller.h/.cpp` — empty EditControllerEx1
- Root `CMakeLists.txt` updated to include `plugins/innexus/`
- Builds and passes pluginval at strictness level 5

**Pattern:** Follow Ruinae's plugin structure as template.

---

### Phase 2: Pre-Processing Pipeline (Architecture Stage 1) ✅

**Goal:** Prepare incoming audio for reliable harmonic extraction.

```
Location: plugin-local (plugins/innexus/src/dsp/) or KrateDSP depending on reusability
Layer: Reuses Layer 0–1 components
Dependencies: Phase 1
```

**Components:**
- DC offset removal — reuse `DCBlocker2` (2nd-order Bessel, 13ms settling)
- High-pass filter at ~30 Hz — reuse `Biquad` with `FilterType::Highpass`
- Optional noise gate — simple threshold gate on RMS level
- Transient suppression — envelope follower + gain reduction on fast transients
- Windowing — reuse `window_functions.h` (Hann or Blackman-Harris for analysis)

**Notes:**
- Analysis signal is separate from any passthrough — "cleaned" stream for analysis only
- Most components already exist; this phase is primarily wiring and configuration
- Pre-processing runs at the analysis frame rate, not per-sample (except DC blocker)

---

### Phase 3: F0 Tracking — YIN Algorithm (Architecture Stage 2) ✅

**Goal:** Implement real-time fundamental frequency estimation.

```
Location: dsp/include/krate/dsp/processors/yin_pitch_detector.h
Layer: 2 (Processors)
Dependencies: fft.h (Layer 1), Phase 2
```

**New Component: `YinPitchDetector`**
- Cumulative Mean Normalized Difference Function (CMNDF)
- FFT-accelerated difference function via Wiener-Khinchin theorem (O(N log N) vs O(N²))
- Absolute threshold for pitch candidate selection
- Parabolic interpolation on CMNDF minima for sub-sample precision

**Output:**
```cpp
struct F0Estimate {
    float frequency;      // Hz (0 if unvoiced)
    float confidence;     // 0..1
    bool voiced;          // confidence > threshold
};
```

**Stability behaviors:**
- Confidence gating — reject estimates below threshold (e.g. 0.3)
- Hysteresis — ±2% frequency band to prevent jitter on stable pitches
- Hold-previous — when confidence drops, hold last known-good F0

**Configurable parameters:**
- Analysis window size (2048–4096 samples for low-frequency accuracy)
- Minimum/maximum F0 range (default: 40–2000 Hz)
- Confidence threshold (default: 0.3)

**Secondary algorithm (future):** McLeod Pitch Method (MPM) [McLeod & Wyvill 2005] — uses NSDF normalization, operates on as few as two periods of signal. Worth benchmarking against YIN on target source material; could serve as fallback or user-selectable option. Note: pYIN [Mauch & Dixon 2014] adds a probabilistic HMM layer atop YIN to further reduce octave errors; worth considering if vanilla YIN proves insufficient.

**Rejected algorithms:**
- *Autocorrelation* — YIN's CMNDF + absolute threshold achieves ~34x lower error rates than raw autocorrelation at similar computational cost (with FFT acceleration); redundant to include both
- *SWIPE* [Camacho & Harris 2008] — template-matching pitch estimator requiring multiple FFTs per candidate frequency; handles only ~7 channels in real time vs YIN's ~38. RT-SWIPE [Meier et al. 2025] closes the gap to ~20 channels but still at significant throughput penalty. Unnecessary for this application.

**References:** de Cheveigné & Kawahara [2002], architecture doc Section 7

---

### Phase 4: Multi-Resolution STFT Analysis (Architecture Stage 3, part 1) ✅

**Goal:** Run dual-window spectral analysis for the partial tracker.

```
Location: plugin-local analysis pipeline (reuses STFT from Layer 1)
Layer: Reuses Layer 1
Dependencies: Phase 2, existing STFT infrastructure
```

**Dual-Window Configuration:**

| Band | Window | FFT Size | Hop | Purpose |
|------|--------|----------|-----|---------|
| Low (F0 + partials 1–4) | 2048–4096 | Same | 25–50% | Accurate F0 and low partial resolution |
| High (partials 5+) | 512–1024 | Same | 25–50% | Low-latency upper harmonic tracking |

**Deliverables:**
- Configure two `STFT` instances with different window sizes
- Long window runs at slower update rate
- Short window runs at full hop rate
- Both produce `SpectralBuffer` output for peak detection
- Window functions: Blackman-Harris (good sidelobe rejection for peak detection)

**Notes:**
- Existing `STFT` class handles the streaming circular buffer and windowing
- This phase configures and runs them; peak detection is Phase 5

---

### Phase 5: Partial Detection & Tracking (Architecture Stage 3, part 2) ✅

**Goal:** Detect spectral peaks, assign them to harmonic tracks, and maintain frame-to-frame continuity.

```
Location: dsp/include/krate/dsp/processors/partial_tracker.h
Layer: 2 (Processors)
Dependencies: Phase 3 (F0), Phase 4 (STFT), spectral_buffer.h, spectral_utils.h
```

**New Component: `PartialTracker`**

**Per-partial data:**
```cpp
struct Partial {
    int index;              // harmonic number (1-based)
    float frequency;        // Hz (actual measured, not idealized n*F0)
    float amplitude;        // linear
    float phase;            // radians
    float relativeFreq;     // frequency / F0 — the ratio stored for synthesis
    float stability;        // tracking confidence (0..1)
    int age;                // frames since track birth
};
```

**Sub-stages:**
1. **Peak detection** — find local maxima in magnitude spectrum, parabolic interpolation for sub-bin precision [Smith & Serra 1987, PARSHL]
2. **Harmonic sieve** — map peaks near integer multiples of F0; tolerance window scales with harmonic number
3. **Frame-to-frame matching** — frequency proximity matching [McAulay & Quatieri 1986], with temporal smoothing [Lagrange et al. 2007]
4. **Birth/death** — new peaks fade in, disappeared peaks held for grace period (3–5 frames) before fade out
5. **Partial count management** — hard cap at 48; rank by `energy * stability`; hysteresis on active set

**Key challenge mitigations:**
- Peak jumping during vibrato → frequency trajectory prediction (linear extrapolation from previous frames; Kalman-style prediction filters [Satar-Boroujeni & Shafai 2005] as a more robust future option)
- Missing harmonics → interpolation across gaps of ≤3 frames before track termination
- Noise peaks → stability score filters out short-lived spurious detections

---

### Phase 6: Harmonic Model Builder (Architecture Stage 4) ✅

**Goal:** Convert raw partial measurements into a stable, musically useful representation.

```
Location: dsp/include/krate/dsp/systems/harmonic_model_builder.h
Layer: 3 (Systems)
Dependencies: Phase 5 (PartialTracker), Phase 3 (F0)
```

**New Component: `HarmonicModelBuilder`**

**Output structure:**
```cpp
struct HarmonicFrame {
    float f0;                           // fundamental frequency (Hz)
    float f0Confidence;                 // from YIN
    std::array<Partial, 48> partials;   // active partials
    int numPartials;                    // active count (≤ 48)
    float spectralCentroid;             // amplitude-weighted mean frequency
    float brightness;                   // perceptual brightness descriptor
    float noisiness;                    // ratio of residual to harmonic energy
    float globalAmplitude;              // smoothed RMS of source
    // normalizedAmps computed on demand: amp_n / sqrt(Σ amp_n²)
};
```

**Key responsibilities:**
- **L2 normalization** — separate spectral shape (timbre) from loudness contour
- **Dual timescale blending** — fast layer (captures articulation) + slow layer (captures identity), blended via `responsiveness` parameter: `model = lerp(slowModel, fastFrame, responsiveness)`
- **Spectral descriptors** — centroid and brightness computed per frame [Grey 1977; Peeters 2004]
- **Median filtering** — apply median filter to per-partial amplitudes to reject impulsive outliers while preserving step edges [Hirsch & Ehrlicher 1995; Klingbeil 2009]
- **Global amplitude** — smoothed RMS using `OnePoleSmoother`, independent of per-partial amplitudes

---

### Phase 7: Harmonic Oscillator Bank (Architecture Stage 5) ✅

**Goal:** Synthesize audio from the harmonic model using Gordon-Smith MCF oscillators.

```
Location: dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h
Layer: 2 (Processors)
Dependencies: Phase 6 (HarmonicFrame output), smoother.h, pitch_utils.h
```

**New Component: `HarmonicOscillatorBank`**

**Design (adapted from ParticleOscillator pattern):**
- 48 MCF oscillators (Gordon-Smith magic circle)
- SoA layout for cache efficiency (sinState[], cosState[], epsilon[], amplitude[], targetAmplitude[])
- Per-partial: `freq_n = (n + deviation_n * inharmonicityAmount) * targetPitch`
- Epsilon: `epsilon_n = 2 * sin(π * freq_n / sampleRate)`

**Frequency update policy:**
- MIDI-driven (note-on, pitch bend): apply new epsilon immediately — no smoothing needed, MCF transitions cleanly
- Optional portamento: epsilon interpolated via `LinearRamp` over glide time
- Analysis-driven (model update): inherently gradual at analysis frame rate

**Anti-aliasing:**
```
fadeStart = 0.8 * nyquist
fadeEnd   = nyquist
antiAliasGain = clamp(1.0 - (partialFreq - fadeStart) / (fadeEnd - fadeStart), 0, 1)
```
Recalculated on pitch change, not per sample.

**Phase management:**
- Default: phase-continuous (only epsilon and amplitude update)
- On large discontinuity (F0 jump > 1 semitone): crossfade old→new oscillator state over 2–5ms
- Amplitude smoothing: one-pole lowpass per partial (reuse `OnePoleSmoother` pattern)
- **Rejected: blanket phase reset** — resetting phase on every model update introduces a waveform derivative discontinuity (perceptually equivalent to an impulse), causing audible clicks. Phase reset is only acceptable as part of the crossfade path (new oscillators initialized with reset phase, faded in) or on explicit MIDI note-on events (see optional enhancements below).

**User controls:**
- Inharmonicity Amount (0–100%, default 100%)

**Performance target:** ≤0.5% CPU for 48 oscillators at 44.1 kHz (based on ParticleOscillator benchmark: 0.38% for 64)

**Optional enhancements (from architecture doc):**
- **Phase reset on note-on** — reinitialize oscillator phases to deterministic state (zero or locked-to-fundamental) on MIDI key events for consistent attack shape. User-selectable; default off.
- **Phase randomization** — random initial phases for smoother, less transient-like onsets; phase relationships affect perceived roughness [ICMC 2004]
- **Harmonic stretching** — models acoustic inharmonicity of stiff strings: `f_n = n * f0 * sqrt(1 + B * n²)` where B is the inharmonicity coefficient (0.0001–0.004 for piano) [Fletcher 1964]. Synthetic alternative to the analysis-derived inharmonicity preservation.
- **Partial dropout** — selective muting of partials for timbral variation

---

### Phase 8: Sample Mode Integration ✅

**Goal:** Load an audio file, run it through the analysis pipeline, and store the resulting harmonic model for playback.

```
Location: plugins/innexus/src/dsp/sample_analyzer.h
Dependencies: Phases 2–6
```

**Deliverables:**
- Audio file loading (WAV/AIFF, mono or stereo-to-mono downmix)
- Feed audio through the real-time analysis pipeline (per architecture doc Section 17: "Real-Time First" — same codepath, relaxed constraints)
- Store sequence of `HarmonicFrame` objects indexed by time position
- Analysis runs on background thread (not audio thread)
- Playback: oscillator bank reads frames at appropriate rate based on source tempo/position

**Design principle:** No offline-only code paths. The analyzer uses the same pre-processing → YIN → partial tracker → model builder chain as live mode will later use.

---

### Phase 9: Basic MIDI Integration & Playback ✅

**Goal:** Wire MIDI input to the oscillator bank for a playable instrument.

```
Location: plugins/innexus/src/processor/
Dependencies: Phase 7 (oscillator bank), Phase 8 (sample analysis)
```

**Deliverables:**
- MIDI note-on → start oscillator bank at target pitch, load harmonic model from analyzed sample
- MIDI note-off → release (amplitude fade-out)
- Velocity → scales `globalAmplitude`, NOT individual partial amplitudes (timbre stays stable)
- Pitch bend → recalculate epsilon for all partials (immediate, with anti-alias recalc)
- Confidence-gated freeze — hold last good frame when F0 confidence drops below threshold, crossfade recovery on confidence return, auto-freeze hold time parameter (100ms–∞, default ∞)

**Monophonic for Phase 1.** Polyphonic voice management deferred.

**Input assumption:** The analysis pipeline assumes monophonic source material — a single dominant fundamental frequency. Polyphonic input (chords, dense pads) will produce unreliable harmonic models because the harmonic sieve cannot disambiguate overlapping harmonic series.

**This completes Milestone 1: the core playable instrument.** ✅

---

### Phase 10: Residual Analysis (Architecture Stage 5B, part 1) ✅

**Goal:** Extract the stochastic (noise) component from analyzed samples.

```
Location: dsp/include/krate/dsp/processors/residual_analyzer.h
Layer: 2 (Processors)
Dependencies: Phases 5–6, stft.h
```

**Sample mode — subtraction method:**
```
residual = originalSignal - resynthesizedHarmonics
```
- Resynthesis uses **tracked partial frequencies** (not idealized n*F0) for tight cancellation
- Analyze residual spectral envelope: piecewise-linear approximation, 8–16 breakpoints

**Per-frame output:**
```cpp
struct ResidualFrame {
    std::array<float, 16> bandEnergies;   // spectral envelope
    float totalEnergy;                     // overall residual level
    bool transientFlag;                    // onset detected
};
```

---

### Phase 11: Residual Resynthesis (Architecture Stage 5B, part 2) ✅

**Goal:** Resynthesize the noise component and blend with the harmonic oscillator bank.

```
Location: dsp/include/krate/dsp/processors/residual_synthesizer.h
Layer: 2 (Processors)
Dependencies: Phase 10, fft.h, stft.h (OverlapAdd)
```

**Method:**
- Generate white noise
- Shape with stored spectral envelope (FFT-domain multiplication or filter bank)
- Scale by per-frame energy estimate

**User controls:**
- Harmonic / Residual Mix (default: both at unity)
- Residual Brightness (tilt toward high/low)
- Transient Emphasis (boost residual during transient frames)

**This completes Milestone 2: deterministic + stochastic decomposition (SMS).** ✅

---

### Phase 12: Live Sidechain Mode (Architecture Stage 3 alternate path)

**Goal:** Enable real-time continuous analysis from sidechain input.

```
Location: plugins/innexus/src/dsp/
Dependencies: Phases 2–7 (full analysis + synthesis pipeline)
```

**Key differences from sample mode:**
- Analysis runs on audio thread (or dedicated thread with lock-free queue)
- Low-latency mode: short window only (512–1024 samples), ~15–25ms effective latency, minimum detectable F0 ~80–100 Hz
- High-precision mode: full dual-window, ~50–100ms latency, minimum detectable F0 ~40 Hz
- Residual estimation via **spectral coring** (zeroing harmonic bins) instead of full subtraction — lower CPU, zero additional latency

**Latency mode is user-selectable.** Default: low-latency for live performance.

**Latency perception thresholds (from architecture doc Section 14):**
- Below 10ms: generally imperceptible
- 10–20ms: threshold range where skilled players begin to notice
- Above 30ms: consistently disruptive
- Instrument-specific tolerances: vocalists <3ms, drummers <6ms, pianists <10ms, guitarists <12ms, keyboardists <20ms [AES 2024 JND study]
- Physical lower bound for pitch detection: at least one full period required (25ms for 40 Hz, 10ms for 100 Hz) [Derrien 2014]

**CPU budget per-stage (from architecture doc Section 13):**

| Stage | Cost Driver | Notes |
|-------|-------------|-------|
| YIN F0 tracker | FFT-accelerated autocorrelation per frame | O(N log N) per frame |
| Dual STFT | Two overlapping FFT passes (short + long window) | Dominant analysis cost |
| Partial tracker | Peak detection + frame-to-frame matching | Scales with peak count |
| Oscillator bank | 48 MCF oscillators per sample | Benchmarked: ~0.38% CPU for 64 |
| Noise model | FFT-domain envelope shaping per frame | Comparable to one STFT pass |

The oscillator bank is well within budget. The **analysis pipeline** (YIN + dual STFT + partial tracking) running continuously in live sidechain mode is the primary CPU concern. Sample mode is inherently cheaper (analysis is precomputed, only the oscillator bank runs in real time).

**CPU quality levers (if profiling reveals bottlenecks):**
- Partial count reduction — lower `maxPartials` from 48 to 24 or 16
- Analysis update rate — reduce STFT hop rate (update every 2nd or 4th buffer)
- Window size reduction — drop the long STFT window entirely (force low-latency mode)
- F0 tracker duty cycling — run YIN every Nth frame, interpolate between updates

**This completes Milestone 3: live sidechain analysis.**

---

### Phase 13: Musical Control Layer — Freeze & Morph (Architecture Stage 6, part 1)

**Goal:** Add expressive harmonic manipulation controls.

```
Location: plugins/innexus/src/dsp/ or Layer 3 (Systems) if generally reusable
Dependencies: Phases 6–7
```

**Harmonic Freeze:**
- Manual freeze toggle (distinct from confidence-gated auto-freeze)
- Captures current HarmonicFrame as frozen state
- Oscillator bank plays from frozen frame until unfrozen

**Morphing:**
- Blend between two harmonic states (e.g., frozen snapshot A ↔ live analysis)
- Interpolate `normalizedAmps`, `relativeFreqs`, `residualBands` independently
- Morph position: 0.0 = state A, 1.0 = state B

---

### Phase 14: Musical Control Layer — Harmonic Filtering (Architecture Stage 6, part 2)

**Goal:** Selective scaling of partial ranges for timbral sculpting.

```
Dependencies: Phase 7 (oscillator bank)
```

**Harmonic Filter:**
```
effectiveAmp_n = amp_n * harmonicMask(n)
```
- Mask function: user-defined curve over harmonic indices (e.g., boost odds, cut evens)
- Presets: all-pass, odd-only, even-only, low harmonics, high harmonics
- Stability vs Responsiveness tradeoff control (adjusts dual-timescale blend)

**This completes Milestone 4: musical control layer.**

---

### Phase 15: Harmonic Memory — Capture & Storage (Architecture Section 15, Priority 1)

**Goal:** Store harmonic snapshots as recallable timbral presets.

```
Location: plugins/innexus/src/dsp/harmonic_memory.h
Dependencies: Phase 6 (HarmonicFrame), Phase 13 (freeze infrastructure)
```

**Serialization format:**
```cpp
struct HarmonicSnapshot {
    float f0Reference;                   // source F0 at capture (informational)
    int numPartials;                     // ≤ 48
    std::array<float, 48> relativeFreqs; // freq_n / F0
    std::array<float, 48> normalizedAmps;// L2-normalized
    std::array<float, 48> phases;        // optional (for phase-reset mode)
    std::array<float, 48> inharmonicDeviation; // relativeFreq_n - n
    std::array<float, 16> residualBands; // noise envelope
    float residualEnergy;
    float globalAmplitude;               // informational
    float spectralCentroid;              // metadata for UI/sorting
    float brightness;                    // metadata
};
```

**Storage:** Binary format for real-time recall; optional JSON export for debugging/sharing.

---

### Phase 16: Harmonic Memory — Recall & Preset Integration

**Goal:** Load snapshots and play them as oscillator states from MIDI.

```
Dependencies: Phase 15
```

**Deliverables:**
- Recall snapshot → oscillator bank plays from stored model
- Multiple snapshot slots (8–16 initially)
- One-button capture from live analysis or frozen state
- Integration with plugin preset system (snapshots stored within plugin state)

**This completes Milestone 5: Harmonic Memory.**

---

### Phase 17: Harmonic Cross-Synthesis (Architecture Section 15, Priority 2)

**Goal:** Explicitly frame the carrier-modulator paradigm as a performance feature.

```
Location: plugins/innexus/src/dsp/
Dependencies: Phase 9 (MIDI playback), Phase 15–16 (Harmonic Memory)
```

**Design:**
- Carrier pitch controlled by MIDI, timbre from analyzed source — this is what the core instrument already does, but cross-synthesis frames it as a deliberate workflow
- Blend controls: interpolate between the played pitch's natural harmonic series and the analyzed source's spectral shape
- Source switching: swap harmonic models in real time (e.g., morph from "violin timbre" to "voice timbre" while holding MIDI notes)
- Distinct from channel vocoder (bandpass filter bank) — operates on per-partial frequency and amplitude trajectories [Smith 2011, Ch. "Cross-Synthesis"]

**Notes:**
- Architecturally a natural extension — the oscillator bank already synthesizes from an abstract harmonic model independent of original audio
- Primary new work is UI/workflow integration and explicit source-as-modulator controls

---

### Phase 18: Stereo Partial Spread (Architecture Section 15, Priority 3)

**Goal:** Distribute partials across the stereo field for spatial width.

```
Dependencies: Phase 7 (oscillator bank)
```

**Implementation:**
- Per-partial pan position: even partials offset left, odd partials offset right
- Spread amount: 0% = mono center, 100% = full L/R alternation
- One pan coefficient per partial — negligible cost
- Oscillator bank output becomes stereo (two sum buffers)

---

### Phase 19: Evolution Engine (Architecture Section 15, Priority 4)

**Goal:** Slow autonomous drift through stored harmonic snapshots.

```
Dependencies: Phase 15–16 (Harmonic Memory)
```

**Design:**
- Interpolate between 2+ snapshots over time
- LFO or random walk drives morph position
- Component-matching across snapshots with unequal partial counts [Tellman et al. 1995]
- User controls: speed, depth, snapshot sequence

---

### Phase 20: Harmonic Modulators (Architecture Section 15, Priority 5)

**Goal:** LFO modulation of individual partial groups.

```
Dependencies: Phase 7 (oscillator bank per-partial amplitude control)
```

**Design:**
- Assign LFOs to partial ranges (e.g., "modulate partials 8–16 with 2 Hz triangle")
- Modulation targets: amplitude, frequency offset, pan position
- Detune spread: slight frequency offsets for chorus-like richness [Roads 1996]

---

### Phase 21: Multi-Source Blending (Architecture Section 15, Priority 6)

**Goal:** Analyze multiple inputs simultaneously and interpolate between their spectra.

```
Location: plugins/innexus/src/dsp/
Dependencies: Phase 12 (live sidechain), Phase 15–16 (Harmonic Memory)
```

**Design:**
- Run multiple analysis pipelines (or recall multiple stored snapshots) and blend their harmonic models
- Interpolation of `normalizedAmps`, `relativeFreqs`, and `residualBands` across 2+ sources
- Component-matching across snapshots with unequal partial counts [Tellman et al. 1995]
- User controls: blend position, per-source weight, crossfade time

**Notes:**
- Most complex creative extension — requires robust single-source path first
- Multiple live analysis pipelines multiply CPU cost; consider limiting to 2 simultaneous live sources + N stored snapshots
- Stored snapshots (from Harmonic Memory) are cheap to blend; live sources are expensive

**This completes Milestone 6: creative extensions.**

---

### Phase 22: Plugin UI

**Goal:** Full VSTGUI interface for all Innexus controls.

```
Dependencies: All previous phases (or incremental as features land)
```

**Scope:** Deferred to its own spec. UI design depends on which features are implemented and how they interact. Will follow VSTGUI cross-platform patterns established in Iterum/Disrumpo/Ruinae.

**This completes Milestone 7: full plugin.**

---

## Phase Dependencies

```
Phase 1 (scaffold)
  └→ Phase 2 (pre-processing)
       └→ Phase 3 (YIN F0)
       └→ Phase 4 (multi-res STFT)
            └→ Phase 5 (partial tracker) ← also needs Phase 3
                 └→ Phase 6 (harmonic model) ← also needs Phase 3
                      └→ Phase 7 (oscillator bank)
                           └→ Phase 8 (sample mode) ← also needs Phases 2–6
                                └→ Phase 9 (MIDI + playback) ← MILESTONE 1
                                     └→ Phase 10 (residual analysis)
                                          └→ Phase 11 (residual synth) ← MILESTONE 2 ✅
                                     └→ Phase 12 (live sidechain) ← MILESTONE 3
                                     └→ Phase 13 (freeze/morph)
                                          └→ Phase 14 (harmonic filter) ← MILESTONE 4
                                          └→ Phase 15 (memory storage)
                                               └→ Phase 16 (memory recall) ← MILESTONE 5
                                               └→ Phase 17 (cross-synthesis)
                                               └→ Phase 19 (evolution engine)
                                     └→ Phase 18 (stereo spread)
                                     └→ Phase 20 (modulators)
                                     └→ Phase 21 (multi-source blending) ← needs Phase 12 + 16
                                                                          ← MILESTONE 6
                      └→ Phase 22 (UI) ← MILESTONE 7
```

## Milestone → Spec Mapping

Each milestone is intended as one `speckit.specify` run:

| Milestone | Phases | Spec Scope |
|-----------|--------|------------|
| M1 | 1–9 | Core playable instrument (sample → analysis → MIDI synth) ✅ |
| M2 | 10–11 | Residual/noise model (SMS decomposition) ✅ |
| M3 | 12 | Live sidechain mode |
| M4 | 13–14 | Musical control layer (freeze, morph, harmonic filter) |
| M5 | 15–16 | Harmonic Memory (snapshot capture/recall) |
| M6 | 17–21 | Creative extensions (cross-synthesis, stereo, evolution, modulators, multi-source blending) |
| M7 | 22 | Plugin UI |
