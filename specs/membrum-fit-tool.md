# `membrum-fit` — Research & Execution Plan

**Status:** Pre-speckit brainstorm (not a formal `/speckit.specify` document).
**Target:** Offline native-C++ command-line tool that ingests WAV drum samples,
performs analysis-by-synthesis against the live Membrum engine, and emits
Membrum kit (`.vstpreset`) and per-pad (`.vstpreset`) preset files.
**Date:** 2026-04-14
**Branch:** TBD (will be created by `/speckit.specify` after this plan is reviewed).

---

## 1. Vision & Scope

### 1.1 What the tool does

`membrum-fit` is an offline, native-C++ command-line executable (no GUI)
that:

1. Reads one WAV file (per-pad mode) or a directory/SFZ of WAV files
   (kit mode).
2. For each input sample, estimates a full Membrum `PadConfig` (42
   normalised parameters as of Phase 6) that, when rendered through the
   real Membrum DSP, produces a sound perceptually close to the input.
3. Writes a Membrum binary preset:
   - `per-pad` mode → 284-byte v1 per-pad preset
     (`specs/139-membrum-phase4-pads/data-model.md` §7) and optionally
     extended per-pad presets that carry the Phase 6 macro block.
   - `kit` mode → full v6 kit preset: 9036-byte Phase-4 body + Phase-5
     appended block (coupling globals + 32 × float64 per-pad coupling +
     override list) + Phase-6 appended block (160 × float64 macros =
     1280 bytes). The total `IBStream` is layout-compatible with
     `kCurrentStateVersion = 6` (see `plugin_ids.h:30` and
     `specs/141-membrum-phase6-ui/data-model.md` §9).
4. Optionally writes a human-inspectable JSON intermediate (mapping
   parameter IDs to normalised values) for debugging and golden-test
   regeneration. JSON is ALSO accepted as a Membrum kit preset scope
   (see spec 141 §10), so the same JSON can be loaded back into the
   plugin.

The tool is **native C++20** by explicit user preference: Membrum's DSP
is almost entirely header-only (every mapper, every exciter, ToneShaper,
and UnnaturalZone live under `plugins/membrum/src/dsp/`), and KrateDSP
is a normal CMake library target. Linking them both into the tool makes
the analysis-by-synthesis loop renderer bit-identical to the shipping
plugin — the most rigorous sanity check available.

### 1.2 What the tool does NOT do

- No real-time / live operation. Purely offline batch.
- No MIDI remapping beyond the trivial SFZ `key`/`lokey`/`hikey` →
  GM pad slot translation.
- No sample-layer playback. Membrum is synthesis-only (spec 135 line 18);
  the tool produces SYNTH presets that *approximate* a sample, not a
  sample-playback patch.
- No multi-velocity round-robin fitting in Phase 1. A single hit per
  pad is fitted; round-robin is listed under deferred work
  (§9 Phase 5+).
- No neural / DDSP pipeline. We investigated differentiable modal
  synthesis (see §2d) and rejected it for the first-class path because
  it would require autodiff support that is not present in KrateDSP.
  A pure gradient-free analysis-by-synthesis loop is sufficient for
  Membrum's ≤42-parameter surface.
- No reverb / room decorrelation. The tool assumes the input is a
  close-miked, mostly-dry drum hit. Preroll and reverb tail handling is
  limited to onset/offset windowing (§5.1) — if the user feeds a wet
  sample, the fit will try to model the tail inside the body's decay,
  which is a known failure mode and will be flagged in a per-sample
  `quality` report.

### 1.3 Non-goals driven by constitution

- **KrateDSP purity:** The tool MUST NOT introduce VST3-SDK dependencies
  into the analysis core. Anything that needs the VST3 `IBStream` layout
  lives in a separate `membrum_preset_io` static library so KrateDSP
  and the analysis core remain SDK-free.
- **Layered DSP:** The extracted Membrum DSP library (see §7.2) stays
  strictly on top of KrateDSP layers 0–3; it cannot reach into the
  plugin's Processor or Controller.

---

## 2. Research (deep, cited)

### 2a. Modal parameter estimation from audio

The inner kernel of the tool is estimating `{f_k, γ_k, a_k, φ_k}` — the
frequency, decay rate, amplitude and phase of every partial — from the
decay portion of a drum hit. After surveying the literature, the
viable method families for this job are:

| Family | Representative | Pros | Cons |
|--------|---------------|------|------|
| **Subspace (ESPRIT)** | Roy & Kailath 1989; Badeau et al. 2002–2006 | Best accuracy on damped sinusoids at moderate SNR; analytic perturbation bounds; Gabor-domain variant for non-stationary data. | Sensitive to model order; needs SVD of large Hankel matrix. |
| **Matrix Pencil (MP)** | Hua & Sarkar 1990 | More noise-robust than ESPRIT on unknown damping for short records; simpler eigenvalue formulation; widely used in EM & audio. | Still needs model order; QR conditioning on Hankel can fail at very low SNR. |
| **Prony / SVD-Prony** | Classical; regularised variants | Closed-form; cheap. | Highly noise-sensitive; deprecated for audio by mid-1990s. |
| **MUSIC / Min-Norm** | Schmidt 1986 | Pseudo-spectrum visualisation useful for model-order selection. | Not a direct parameter estimator; pairs with ESPRIT. |
| **NLS / MLE** | Li & Stoica | Lowest asymptotic error. | Non-convex, needs good initial guess — a perfect refinement step after ESPRIT/MP. |

**Decision:** The tool will implement **both ESPRIT (Total Least
Squares variant) and Matrix Pencil** on top of Eigen, gated by a CLI
flag (`--modal-method esprit|mp`). Default: **Matrix Pencil**, because
Hua & Sarkar (1990) explicitly targeted short, unknown-damping records
— which is exactly a drum hit — and the method is "less sensitive to
noise than the polynomial method" (paper §IV-A). A MUSIC
pseudo-spectrum is produced only for diagnostic plots and for
model-order selection (ITC/MDL criteria).

**Model-order selection:** Minimum Description Length (MDL) and
Information Theoretic Criterion (ITC) over candidate `N ∈ [8, 64]`.
Membrum itself uses 16 modes per body (12 for Shell, 32 for
NoiseBody — see `shell_modes.h:30`, `noise_body_mapper.h:64`), so the
extracted partial count will be capped at 32 and down-sampled to fit
the target body's mode slots.

**Gabor-domain ESPRIT for impact sounds:** Matane, Vanderveken et al.
published a specific workflow "Modal Analysis of Impact Sounds with
ESPRIT in Gabor Transforms" that handles the non-stationary attack
smoothly by running ESPRIT on each Gabor slice and tracking partials
across frames. This is the approach we adopt for any sample whose
envelope decays more than 40 dB in the first 20 ms (i.e. kicks and
cymbals); for slower-decay samples (toms, tonal perc) a single-shot MP
on a 200–500 ms decay window is enough.

**Sources:**
- [Roy & Kailath (1989), ESPRIT — IEEE T-ASSP, Vol. 37](https://dblp.org/rec/journals/tsp/HuaS90.html) (referenced)
- [Hua & Sarkar (1990), Matrix Pencil — IEEE T-ASSP PDF](https://intra.ece.ucr.edu/~yhua/MPM.pdf)
- [Hua & Sarkar (1990) IEEE Xplore](https://ieeexplore.ieee.org/document/56027/)
- [Badeau, David, Richard — Performance of ESPRIT for estimating mixtures of complex exponentials](https://inria.hal.science/hal-00945195/document)
- [Badeau et al. — Multidimensional ESPRIT for Damped and Undamped Signals (IEEE TSP)](https://hal.science/hal-01360438v3/document)
- [Badeau et al. — HR methods on audio (DAFx-02)](https://inria.hal.science/hal-00945272/file/dafx-02.pdf)
- [Matane et al. — Modal Analysis of Impact Sounds with ESPRIT in Gabor Transforms](https://www.researchgate.net/publication/261640125_Modal_Analysis_of_Impact_Sounds_with_ESPRIT_in_Gabor_Transforms)
- [EDS parametric modeling and tracking of audio signals](https://www.academia.edu/19361684/EDS_parametric_modeling_and_tracking_of_audio_signals)
- [Structure-Aware Matrix Pencil Method (arXiv 2025)](https://arxiv.org/html/2502.17047v2)

### 2b. Drum-specific modal analysis & resynthesis literature

We ground every body-classifier in the physics text that Membrum
itself cites:

- **Circular membrane Bessel ratios** — the Membrum `kMembraneRatios`
  table at `plugins/membrum/src/dsp/membrane_modes.h:22` is the
  standard `j_mn / j_01` table (Rossing & Fletcher "The Physics of
  Musical Instruments" Ch. 18, Leissa/NASA SP-160). This is also the
  reference table the classifier will score against for Membrane mode.

- **Kirchhoff square-plate modes** — `plate_modes.h:41` uses the
  Leissa 1969 eigenvalue list `f_{m,n} / f_{1,1} = (m²+n²)/2` with
  deliberately retained degenerate pairs. The classifier recognises
  these as integer-ratio "quadratic stacks".

- **Free-free Euler-Bernoulli beam modes** — `shell_modes.h:34`
  encodes the roots of `cos(βL)·cosh(βL)=1` (Fletcher & Rossing Ch. 2):
  1.000, 2.757, 5.404, 8.933, … Non-integer, growing approximately as
  `(2k+1)²`. Very recognisable ratio signature.

- **Bell Chladni inharmonic partials** — `bell_modes.h:39` — hum (0.25),
  prime (0.5), tierce (0.6), quint (0.75), nominal (1.0), then Hibberts
  big-bell extrapolation. The tierce ≈ 0.6 "minor-third" fingerprint is
  the classifier's Bell lock-in.

- **Snare drum FDTD model** — Bilbao (2012). Noted; we do NOT attempt
  a full Bilbao-style inverse because Membrum's Membrane body is a
  single-head modal approximation. Instead, a snare-type input is
  recognised by its dominant NoiseBurst transient over a short-decay
  Membrane body — which is exactly Membrum's GM Snare template
  (`default_kit.h:62`).

- **Cymbal ≈ 400 modes below 20 kHz** — spec 135 line 166 references
  the hybrid "sparse modes + filtered noise" treatment. We match that
  architecturally: cymbal inputs route to NoiseBody (32 modes + noise
  layer) rather than to plain Plate.

- **Avanzini, Serra SMS** — considered and rejected as an analysis
  backbone: SMS partial tracking is designed for harmonic tonal
  signals; damped-exponential fitting is the correct framing for a
  drum hit.

- **Differentiable modal synthesis (DiffSound, DiffMoog, Jin et al.,
  Hayes et al. 2023)** — excellent for shape / material inverse
  reasoning at research time but requires autodiff support (PyTorch /
  JAX) which we do not have in KrateDSP; rejected for v1.

**Sources:**
- [Bilbao — Time domain simulation and sound synthesis for the snare drum](https://pubmed.ncbi.nlm.nih.gov/22280714/)
- [Fischer — Modal Analysis of a Snare Drum (UIUC course project, good reproducible reference)](https://courses.physics.illinois.edu/phys406/sp2017/Student_Projects/Spring14/Matthew_Fischer_Physics_406_Final_Project_Sp14.pdf)
- [Torin — Percussion Instrument Modelling in 3D (PhD thesis)](https://www.albertotorin.it/files/ATorin_PhDThesis.pdf)
- [DiffSound: Differentiable Modal Sound Simulation for Inverse Reasoning (OpenReview)](https://openreview.net/forum?id=6jFjYmahxu)
- [Hayes et al. — A Review of Differentiable Digital Signal Processing (Frontiers 2023)](https://www.frontiersin.org/journals/signal-processing/articles/10.3389/frsip.2023.1284100/full)
- [DiffMoog: a Differentiable Modular Synthesizer for Sound Matching (arXiv 2024)](https://arxiv.org/html/2401.12570v1)
- [Differentiable Modal Synthesis for Physical Modeling of Planar String (arXiv 2024)](https://arxiv.org/html/2407.05516v1)
- [Masuda & Saito — Improving Semi-Supervised Differentiable Synthesizer Sound Matching (IEEE/ACM TASLP 2023)](https://dl.acm.org/doi/10.1109/TASLP.2023.3237161)

### 2c. Exciter / attack transient classification

A drum sample is time-decomposed into (i) the attack/transient window
(first ~20 ms after onset) and (ii) the decay window (everything after
the attack). The attack window is where Membrum's exciter type is
decided — Impulse vs Mallet vs NoiseBurst vs FM vs Feedback vs
Friction (6 classes, one-hot: `exciter_type.h`).

**Features (Peeters 2011 / Lerch 2012 nomenclature):**

- **Log-Attack Time (LAT):** `log10(t_stop − t_start)` where
  `t_start` = 2 % of peak, `t_stop` = 90 %. Short LAT (<1 ms) ⇒
  Impulse / FM. Medium (1–8 ms) ⇒ Mallet / NoiseBurst. Long
  monotonic-rising ⇒ Friction.
- **Spectral flatness** at t = 2 ms after onset: high (> 0.5) ⇒ noise
  class (NoiseBurst / Friction); low ⇒ deterministic impulse class.
- **Spectral centroid trajectory** over the first 10 ms: monotonically
  decreasing from >3 kHz ⇒ Impulse; rising-then-falling ⇒ Mallet;
  sustained high ⇒ Friction/FM.
- **Inharmonicity of attack window** (deviation from arithmetic partial
  spacing): high ⇒ FM Impulse (Chowning ratios); low ⇒ Mallet/Impulse.
- **Pre-onset feedback signature** (auto-correlation peak near
  `1/f0` *before* the attack peak): ⇒ Feedback exciter.

**Classifier:** v1 is a **hand-crafted rule tree** over the 5 features
above — each Membrum exciter has a distinctive attack fingerprint and
the class boundaries are physically motivated (see `mallet_exciter.h:50`
for how Mallet itself derives from Impulse's ImpactExciter backend but
with a lower hardness/brightness range). v2 may upgrade to a tiny
random-forest / XGBoost classifier trained on a Membrum-rendered
ground-truth corpus (see §5.3). No neural model — the feature space is
low-dimensional and hand-rules are inspectable.

**Sources:**
- [Peeters et al. (2011) — The Timbre Toolbox (JASA PDF)](https://www.mcgill.ca/mpcl/files/mpcl/peeters_2011_jasa.pdf)
- [Caetano — Audio Content Descriptors of Timbre (book chapter PDF)](https://comma.eecs.qmul.ac.uk/assets/pdf/Caetano_chap11.pdf)
- [Lerch — An Introduction to Audio Content Analysis (textbook PDF)](http://telit.etf.rs/download/An%20Introduction%20to%20Audio%20Content%20Analysis.pdf)
- [Physical and Perceptual Aspects of Percussive Timbre (eScholarship PDF)](https://escholarship.org/content/qt5bx4j1fj/qt5bx4j1fj_noSplash_4650521b252cbaf7fb55a36f88039016.pdf)
- [Schaefferian estimation of percussive qualities — Attack Profile / Mass / Harmonic Timbre](https://www.researchgate.net/publication/356285210_Estimation_of_Perceptual_Qualities_of_Percussive_Sounds_Inspired_by_Schaefferian_Criteria_Attack_Profile_Mass_and_Harmonic_Timbre)

### 2d. Analysis-by-synthesis / inverse problem

After the body classifier and the mapper-inversion give us an initial
`PadConfig`, we refine by rendering the real Membrum voice and
measuring perceptual distance to the input.

**Loss function:** **Multi-resolution STFT magnitude loss** (Yamamoto
et al., Parallel WaveGAN / auraloss) with window sizes
`{64, 128, 256, 512, 1024, 2048}` and 75 % overlap, log-magnitude L1,
summed over scales. **Plus** MFCC-L1 (Lerch, Peeters) on 20-coefficient
MFCCs as a timbral anchor. **Plus** a log-envelope L1 (Hilbert envelope
downsampled to 200 Hz) — critical for percussion where the onset
envelope is a dominant cue. Weights start at 0.6 / 0.2 / 0.2 and are
exposed as CLI flags.

MSS alone is known to provide poor gradients for frequency parameters
(Hayes et al. 2023 review, §4) — but we are NOT using gradients. We
are using derivative-free BOBYQA / CMA-ES, so the MSS gradient issue
does not apply. MSS is simply a robust timbral distance here.

**Optimizer:** Two-stage.

1. **Local refinement (default):** NLopt's `NLOPT_LN_BOBYQA`
   (derivative-free, bounded, quadratic-model-trust-region; Powell's
   successor to NEWUOA). Bounds are `[0, 1]` per parameter. Warm-start
   from the classifier/inverter's initial estimate. Typical budget:
   100–300 function evaluations. Each eval is a Membrum render of
   0.5–2 s at 44.1 kHz — cheap (<10 ms per eval).
2. **Global escape (optional `--global`):** libcmaes CMA-ES with a
   small population (λ=16, σ₀=0.15). Invoked only when BOBYQA's final
   loss exceeds a threshold — i.e. BOBYQA got stuck in a local minimum.

**Parameter subset optimised:** NOT all 42. We freeze discrete
selectors (ExciterType, BodyModel, filter type, pitch-env curve,
choke group, output bus) at the classifier's pick, and freeze the
Phase 6 macros and morph block at defaults. The live optimisation
vector is 18–22 continuous parameters depending on which Unnatural
Zone controls are engaged.

**Sources:**
- [Engel et al. (2020) — DDSP (ICLR paper PDF)](https://openreview.net/pdf?id=B1x1ma4tDr)
- [Parallel WaveGAN (arXiv 2019)](https://arxiv.org/pdf/1910.11480) — multi-resolution STFT loss recipe.
- [Schwär & Müller (2023) — Multi-Scale Spectral Loss Revisited (IEEE SPL PDF)](https://www.audiolabs-erlangen.de/content/05_fau/assistant/00_schwaer/01_publications/2023_SchwaerM_MultiScaleSpecLoss_IEEE-SPL.pdf)
- [Steinmetz — auraloss (PyTorch reference implementations)](https://github.com/csteinmetz1/auraloss)
- [NLopt algorithm list — BOBYQA](https://nlopt.readthedocs.io/en/latest/NLopt_Algorithms/)
- [NLopt in pagmo2 (C++ wrapper docs)](https://esa.github.io/pagmo2/docs/cpp/algorithms/nlopt.html)
- [Masuda & Saito (2023) — practical-applications sound matching](https://dl.acm.org/doi/10.1109/TASLP.2023.3237161)

### 2e. C++ library landscape

| Stage | Chosen library | License | Notes |
|---|---|---|---|
| WAV / AIFF I/O | **dr_wav** (single-header from `dr_libs`) | Public Domain / MIT-0 | Single-header, permissive, handles int16/int24/int32/float32 and 44.1/48/88.2/96/192 kHz. Already used in many JUCE-adjacent projects. `libsndfile` was considered but is LGPL — unsuitable for static linking into a permissively licensed tool. |
| Linear algebra | **Eigen 3.4** | MPL2 | SVD, eigenvalues, Hankel matrices, generalised eigenvalue solver (needed by Matrix Pencil). Already well-known; pairs with **Spectra** if large-scale sparse eigenvalue problems appear (unlikely here). |
| FFT | **pffft** (already in `extern/pffft/`) | BSD | SIMD-optimised; exposed through `krate/dsp/primitives/fft.h`. Reuse directly — no new FFT dependency. |
| STFT / windowing | existing `krate/dsp/primitives/stft.h` | project | Already validated in production for Iterum's spectral mode. |
| Derivative-free optimisation | **NLopt** (BOBYQA, COBYLA, CRS) | BSD / LGPL (algorithm-dependent) | BOBYQA is under the permissive part of NLopt. If we want the entire tool permissively licensed we vendor only the BOBYQA and Nelder-Mead source files. |
| CMA-ES (optional) | **libcmaes** | LGPL | Only needed when `--global` is set. Compile-time opt-in flag so we do not contaminate the default build with LGPL. Alternative: port Hansen's C++ reference `cmaes_interface.c` (BSD) directly — <1 kLOC. |
| Feature extraction | **in-house** (Layer 2/3 of KrateDSP + `plugins/innexus/src/dsp/` sample-analyzer primitives) | project | Spectral flatness, MFCC, centroid. Essentia was considered but is AGPL — inappropriate. We write the few features we need (<300 lines) on top of pffft. |
| CLI parser | **CLI11** | BSD-3 | Header-only, C++11. Already a project preference pattern. |
| JSON | **nlohmann/json** (single-header) | MIT | Only if we emit the human-readable intermediate. |
| Classification (future v2) | **mlpack** or **dlib** | BSD / BSL | Deferred. |
| SFZ ingestion | **sfizz** (parser only, not synth) | ISC/BSD | Mature, actively maintained, separable parser (see `sfztools/sfizz` `src/sfizz/Parser.cpp`). Alternative: write a minimal SFZ reader — the subset Membrum needs (regions with `sample` + `key`/`lokey`/`hikey` + optional `pitch_keycenter`) is <200 lines. |

**ESPRIT / Matrix Pencil in C++:** No widely-maintained C++ library
publishes ESPRIT or MP as a first-class API. The closest match is
`shantistewart/MUSIC-and-ESPRIT-Algorithms` (pedagogical). Decision:
**implement both algorithms directly on Eigen** (both are <200 lines
each on top of `JacobiSVD` and `GeneralizedSelfAdjointEigenSolver`).
Reference equations come from Hua & Sarkar 1990 §III and Badeau 2006
§III. Implementing in-house is the right call because we want to
tune the Hankel-matrix pencil parameter `L` per-sample based on
SNR and decay length; no black box gives us that control.

**Loris (reassigned bandwidth-enhanced additive model)** is notable
and mature — but it targets *harmonic* partial tracking, not damped
sinusoid parameter estimation of a short hit, and the
`tractal/loris` fork's last commit is 2010-era. We use it as a
reference implementation for the attack-window windowing choices
only.

**Sources:**
- [dr_libs / dr_wav — mackron/dr_libs](https://github.com/mackron/dr_libs/blob/master/dr_wav.h)
- [libsndfile (LGPL alternative we reject)](http://libsndfile.github.io/libsndfile/)
- [Eigen — GitLab / libeigen](https://libeigen.gitlab.io/)
- [Spectra — yixuan/spectra (eigenvalue problems on Eigen)](https://github.com/yixuan/spectra)
- [NLopt documentation](https://nlopt.readthedocs.io/en/latest/NLopt_Algorithms/)
- [CLI11 — cliutils/cli11](https://github.com/CLIUtils/CLI11)
- [sfizz — sfztools/sfizz](https://github.com/sfztools/sfizz)
- [MUSIC / ESPRIT reference impl (shantistewart)](https://github.com/shantistewart/MUSIC-and-ESPRIT-Algorithms)
- [Loris mirror — tractal/loris (maintenance status 2010-ish)](https://github.com/tractal/loris)
- [Loris — CERL Sound Group page](https://www.cerlsoundgroup.org/Loris/)

### 2f. Input drum-kit formats

| Format | Pros | Cons | Verdict |
|---|---|---|---|
| **WAV directory + MIDI-map JSON** | trivially parseable; zero dependencies | not a real-world distribution format | v1 primary |
| **SFZ** | plain text; open spec; widely used; sfizz parser exists | many features we don't need | v1 secondary via `sfizz::Parser` |
| Kontakt `.nki` | ubiquitous commercial format | closed, encrypted, reverse-engineering hostile | reject |
| NN-XT `.sxt`, EXS24 `.exs`, Battery `.nkr`, BFD | various commercial formats | closed or semi-closed, niche | reject |
| Ableton Drum Rack | ALS XML inside ZIP, feasible but licence-ambiguous | v2 consideration | defer |
| Geist / FL Studio FPC | closed | reject | reject |

**Decision:** v1 supports `WAV directory + JSON mapping` and `SFZ`.
For the WAV-directory path the JSON maps MIDI note → file:

```json
{ "36": "kick_01.wav", "38": "snare_01.wav", "42": "hat_closed.wav", … }
```

For SFZ we accept regions keyed by `key` or by `lokey/hikey` in the
GM range 36–67; any region outside the range is logged and skipped.
Velocity layers are handled by picking the loudest layer per note in
v1; multi-velocity round-robin fitting is deferred (§9 Phase 5+).

**Sources:**
- [SFZ Format — sfzformat.com](https://sfzformat.com/)
- [SFZ Format — lokey / hikey opcodes](https://sfzformat.com/opcodes/hikey/)
- [SFZ Wikipedia](https://en.wikipedia.org/wiki/SFZ_(file_format))
- [sfizz engine docs](https://sfztools.github.io/sfizz/engine_description/)

---

## 3. Pipeline architecture

```
                  +-----------------------------+
  input WAV  -->  |  1. Loader / Normalisation  | --> mono float buffer @ 44.1/48 kHz
                  +--------------+--------------+
                                 |
                  +--------------v--------------+
                  |  2. Onset/segmentation       | --> attack window [0..Ta]
                  |     (spectral flux + peak)   |     decay   window [Ta..Td]
                  +--------------+--------------+
                                 |
                  +--------------v--------------+
                  |  3. Attack-feature extractor | --> {LAT, flatness, centroid_traj,
                  |                              |      preOnsetAR, inharmonicity}
                  +--------------+--------------+
                                 |
                  +--------------v--------------+
                  |  4. Exciter classifier (rules)| --> ExciterType + velocity estimate
                  +--------------+--------------+
                                 |
                  +--------------v--------------+
                  |  5. Modal extractor           | --> {f_k, γ_k, a_k, φ_k}, N modes
                  |     (Matrix Pencil / ESPRIT)  |
                  +--------------+--------------+
                                 |
                  +--------------v--------------+
                  |  6. Body-model classifier     | --> BodyModelType + confidence
                  |     (mode-ratio scoring)      |
                  +--------------+--------------+
                                 |
                  +--------------v--------------+
                  |  7. Mapper inversion          | --> (material, size, decay,
                  |                               |      strikePos, level) initial
                  +--------------+--------------+
                                 |
                  +--------------v--------------+
                  |  8. Tone Shaper fit           | --> filter {cutoff, res, env},
                  |                               |     drive, fold, pitchEnv
                  +--------------+--------------+
                                 |
                  +--------------v--------------+
                  |  9. Unnatural Zone residual   | --> modeInject, decaySkew,
                  |                               |     nonlinearCoupling
                  +--------------+--------------+
                                 |
                  +--------------v--------------+
                  | 10. Analysis-by-synthesis     | --> refined PadConfig
                  |     refinement (BOBYQA)       |
                  +--------------+--------------+
                                 |
                  +--------------v--------------+
                  | 11. Preset writer             | --> .vstpreset / JSON
                  +-----------------------------+
```

Each stage has a **pure-function interface** (input → output struct,
no globals, no hidden state) so stages are independently testable
and replaceable.

---

## 4. Per-stage deep dive

### 4.1 Loader / Normalisation

- dr_wav reads any WAV (int16/24/32, float, 44.1–192 kHz, mono/stereo).
- Stereo → mono by mid-sum, with a warning if inter-channel correlation
  is < 0.7 (wide stereo samples are a modelling hazard for a
  mono-voice synth).
- Resample to a configurable target rate (default **44.1 kHz** to match
  Membrum's render path). Resampler: existing KrateDSP
  `primitives/resampler.h` or a drop-in Lanczos — the sample is
  offline, quality > speed.
- Normalise to peak = −1 dBFS. Store the original peak so we can
  recover `Level` (parameter 104).

**Contract:**
```cpp
struct LoadedSample {
    std::vector<float> samples;   // mono, float32, normalised
    double             sampleRate;
    float              originalPeakDbfs;
    std::string        sourcePath;
};
LoadedSample loadSample(const std::filesystem::path& wav);
```

### 4.2 Onset & segmentation

Purpose: pick `t_onset` and split the hit into an **attack window**
(`[t_onset, t_onset + 20 ms]`) and a **decay window**
(`[t_onset + 5 ms, t_onset + min(tailLength, 2 s)]`).

Algorithm: spectral flux peak picking (Dixon 2006 — the classical
approach) over a 512-sample hop, confirmed by a local maximum in the
full-band energy envelope. Trailing silence is trimmed via an RMS
gate at −60 dBFS.

Alternatives considered:
- Superflux (Böck et al. 2013): overkill for single-hit samples.
- Energy-threshold only: misses low-amplitude onsets of brush hits.

**Contract:**
```cpp
struct SegmentedSample {
    LoadedSample       src;
    std::size_t        onsetSample;
    std::size_t        attackEndSample;   // onset + 20 ms
    std::size_t        decayEndSample;    // RMS-gated tail
};
```

### 4.3 Attack-window feature extraction

Features computed over the attack window (20 ms window, optionally
Hann-tapered):

- Log-Attack Time (Peeters 2011 definition: time from 2 % to 90 % of
  peak amplitude, in log10 seconds).
- Spectral flatness (geometric mean / arithmetic mean of magnitude
  spectrum) @ centre of attack window.
- Spectral centroid **trajectory**: 5 hops × centroid → feature vector.
- Pre-onset autocorrelation peak in `[0.5 ms, 20 ms]` lag range
  (Feedback exciter fingerprint).
- Inharmonicity coefficient over the attack spectrum
  (`Σ |peak_k_f − k*f1|² / (N*f1²)`).

Reuses `plugins/innexus/src/dsp/` where possible — Innexus already
ships a SampleAnalyzer and live analysis pipeline with many of these
primitives (CLAUDE.md "Innexus harmonic resynthesis instrument").

### 4.4 Exciter classifier (rule tree)

```
if  preOnsetARPeak > 0.35 and decayTailEnergyRatio > 0.5
      → ExciterType::Feedback
elif LAT < -2.7 (<2 ms) and flatness < 0.2 and inharmonicity > 0.15
      → ExciterType::FM
elif LAT < -3.0 (<1 ms) and flatness < 0.15
      → ExciterType::Impulse
elif LAT >  0.0 (>1 s, continuous rise)
      → ExciterType::Friction
elif flatness > 0.45 and peakSpectralCentroid > 2 kHz
      → ExciterType::NoiseBurst
else
      → ExciterType::Mallet              (default — softest impact)
```

Thresholds are *initial hypotheses* grounded in spec 135 lines 108–130
and `mallet_exciter.h:50`. They will be calibrated on a corpus of
**Membrum-rendered** sounds in §5.3 (golden round-trip) before touching
real drum samples, so the calibration is objective.

### 4.5 Modal extraction (Matrix Pencil / ESPRIT)

Operates on the decay window, optionally pre-windowed with a Hann
taper and zero-padded to a power-of-two length. Full contract:

```cpp
struct Mode {
    float freqHz;      // estimated frequency
    float decayRate;   // γ (s⁻¹), so t60 = log(1000)/γ
    float amplitude;   // linear
    float phase;       // radians
    float quality;     // snr per mode, used for ranking
};
struct ModalDecomposition {
    std::vector<Mode> modes;   // sorted by amplitude, descending
    float             residualRms;
};
ModalDecomposition extractModes(std::span<const float> decay,
                                 double sampleRate,
                                 int    maxModes,
                                 ModalMethod method);
```

**Implementation notes:**

- Build Hankel matrix `Y ∈ ℂ^{(N-L)×L}` with `L = N/3` (Hua & Sarkar
  §III-C recommendation).
- For Matrix Pencil: form `Y₁ = Y(:,1:L-1)`, `Y₂ = Y(:,2:L)`, solve
  generalised eigenvalue `Y₂ = λ Y₁` via Eigen's
  `GeneralizedSelfAdjointEigenSolver` (with TLS preconditioning).
- Eigenvalues `λ_k = exp((−γ_k + j ω_k)/f_s)`; recover `ω_k = arg(λ_k)`
  and `γ_k = −f_s · log|λ_k|`.
- Amplitudes / phases by linear least-squares against the residual
  (Vandermonde fit). Reject modes with `γ < 0` (growing exponentials
  are numerical artefacts).
- Per-block ESPRIT-in-Gabor for fast-decay samples: STFT at
  `hop = 64`, run MP on each column of the analytic signal for the
  first 40 frames, then track modes across frames (simple frequency
  proximity matching).

Output is sorted by amplitude and clipped to the target body's mode
count (12 for Shell, 16 for Membrane/Plate/Bell, 32 for NoiseBody).

### 4.6 Body-model classifier

For each of the 6 body types, build a reference ratio vector `r_body`
(read straight from the `*_modes.h` tables). For the measured modes
(normalised to `f_k / f_min`), compute a matching score:

```
score(body) = min over circular shift s of
              Σ_k  w_k · | log(f_k / f_min) − log(r_body[k+s]) |
```

`w_k` = amplitude-weighted so strong modes dominate. Membrum's
`StringMapper` and `NoiseBodyMapper` are special-cased:

- String fingerprint: near-integer ratios (harmonic stack) with
  slight stretching from stiffness. Ratio deviation < 2 % from integer
  sequence ⇒ String.
- NoiseBody fingerprint: high modal density (> 20 modes in the first
  5 kHz) AND substantial broadband noise residual (residualRms / totalRms > 0.3).

Output:

```cpp
struct BodyScore {
    BodyModelType body;
    float         score;      // lower is better
    float         confidence; // margin over 2nd-best, 0..1
};
std::array<BodyScore, 6> classifyBody(const ModalDecomposition&,
                                      const AttackFeatures&);
```

Ambiguity policy: if top-1 and top-2 scores differ by < 10 %, route
both into the refinement stage and keep the one with lower final
analysis-by-synthesis loss.

### 4.7 Mapper inversion

Each body's forward mapper is a closed-form function of
`(material, size, decay, strikePos)`. Invert as follows:

- **MembraneMapper** (`membrane_mapper.h:47`):
  - `f0 = 500 · 0.1^size` → `size = log10(500/f0) / 1`.
  - `strikePos` from the amplitude ratio between the (m=0) and
    (m=2) modes. Analytical solution via Bessel `J_0`/`J_2` root-finding
    on `r/a ∈ [0, 0.9]`. Bisection against the measured amplitude
    ratio is sufficient (< 20 iterations).
  - `decayTime` known from `γ_fundamental` → solve
    `decay = (log(decayTime/baseDecay) − log(0.3)) / (log(3) − log(0.3))`.
  - `material` jointly affects decay and brightness; bisect on
    brightness = high/low mode amplitude ratio.

- **PlateMapper** (`plate_mapper.h:34`): analogous. `f0 = 800 · 0.1^size`.

- **ShellMapper** (`shell_mapper.h:24`): fundamental `1500 · 0.1^size`.
  Shell's mode growth factor 1 → 2.757 → 5.404 is the strongest
  fingerprint for inversion.

- **BellMapper** (`bell_mapper.h:24`): fundamental is the *nominal*
  partial. `f_nominal = 800 · 0.1^size`. Hum-to-nominal ratio (~0.25)
  is rigid → use the measured hum peak to anchor `size`.

- **StringMapper** (`string_mapper.h:32`): `frequencyHz` is the
  fundamental directly. Inversion of Membrum's semantic-inversion
  `brightness_ = 1 - material` (note explicit in `string_mapper.h:53`).

- **NoiseBodyMapper** (`noise_body_mapper.h:77`): Returns a hybrid
  `Result` with modal block + noise-filter parameters. Inversion
  splits: modal block uses plate-inversion; noise filter cutoff comes
  from the spectral centroid of the *residual* signal (input minus
  re-synthesised modes).

**Critical property (FR-055-like):** Every mapper's default-parameter
output must correspond to the ToneShaper identity / Phase-1 Membrane
case. The inversion routine MUST never write `modeStretch=0` or
`decaySkew=0` — it must emit the *normalised* neutral values
(`modeStretch=0.333333`, `decaySkew=0.5`) exactly as `PadConfig` does
by default. Unit-test on this directly (§8).

### 4.8 Tone Shaper fit

The post-body chain (`tone_shaper.h:19` — Drive → Wavefolder →
DCBlocker → SVF → Filter ADSR):

1. Compare input spectrum to re-synthesised body spectrum. If input has
   a lowpass-like tilt below 10 kHz that the body doesn't have →
   fit SVF cutoff + resonance (grid search 32 × 8 × 8 + BOBYQA
   refine).
2. Fit Filter envelope ADSR by regressing the time-varying centroid
   of the input against the ADSR shape; closed-form for simple
   exponential decay cases.
3. Detect presence of **Drive / Fold** by measuring the input's
   harmonic distortion vs the re-synthesised modal body. If
   THD > 5 %, enable drive; if harmonics above 3× are present with
   amplitude > 1st-harmonic fold, enable Fold.
4. **Pitch envelope** (critical for kicks — `default_kit.h:57` shows
   the Kick uses 160→50 Hz in 20 ms): fit by tracking the fundamental
   frequency over the first 100 ms. If the track falls by > 20 % in
   < 200 ms → enable pitch env, fit `pitchEnvStart / End / Time /
   Curve`. Uses a parabolic-interpolation peak tracker on the STFT
   magnitude spectrum (Brown & Puckette 1993).

Note that ToneShaper and pitch-env interact heavily with the body's
modal decay pattern — so this stage iterates with stage 4.10 rather
than being purely feed-forward.

### 4.9 Unnatural Zone residual fit

After stages 4.7–4.8, render a candidate voice and subtract
(time-aligned) from the input. The residual contains:

- Extra sinusoidal partials not in the body's natural ratios →
  `modeInjectAmount`. Measure the ratio of residual peak energy in
  non-body-natural frequency bands to total residual energy. Map that
  ratio (0..0.4) → `modeInjectAmount` (0..1) through a monotonic
  calibration curve computed by sweeping the control on a synthetic
  corpus.
- Apparent *inverted* decay (high modes louder than fundamental for
  t > 50 ms) → negative `decaySkew`.
- Cross-modulation sidebands — `nonlinearCoupling`.
- Generally: these are low-confidence; default is **zero** and the
  BOBYQA stage (§4.10) will bump them off zero only if the overall
  loss drops significantly.

### 4.10 Analysis-by-synthesis refinement

Builds on a **RenderableMembrumVoice**: a minimal harness that takes
a `PadConfig` and produces a mono float buffer of length L. It uses
**exactly** Membrum's production DSP (see §7.2 extraction plan) —
so the loss surface at the final BOBYQA step is the same one the
plugin will produce at runtime.

```cpp
struct RefineContext {
    const std::vector<float>&  target;
    double                      sampleRate;
    PadConfig                   initial;
    std::vector<ParameterIndex> optimisable;  // typically 18..22
    float                       wSTFT = 0.6f;
    float                       wMFCC = 0.2f;
    float                       wEnv  = 0.2f;
};
struct RefineResult {
    PadConfig final;
    float     finalLoss;
    int       evalCount;
    bool      convergedBOBYQA;
    bool      escapedCMAES;
};
RefineResult refine(const RefineContext&);
```

- Lazy MFCC and log-envelope cache for the target signal — computed
  once, re-used every eval.
- Per-eval render is 0.5–2 s of audio at 44.1 kHz; a single voice
  runs ~40× realtime on a modern CPU in KrateDSP's SIMD path → <15 ms
  per eval → 3–5 s for 300 BOBYQA iterations.
- Parameter bounds enforced by the optimiser; no re-parameterisation
  needed since all parameters are already `[0, 1]`.
- Early-exit when relative loss improvement < 1 % over 20 evals.

### 4.11 Preset writer

Three output scopes:

1. **Per-pad preset (`.vstpreset`)** — 284-byte v1 blob per
   `data-model.md` §7. One exciter int32, one body int32, 34 × float64
   sound params.
2. **Kit preset (`.vstpreset`)** — version-6 blob. Writer composes:
   - Phase-4 body (9036 bytes without `selectedPadIndex` —
     §6 "Kit Preset Binary Layout")
   - Phase-5 append (globalCoupling + snareBuzz + tomResonance +
     couplingDelay + 32 × perPadCoupling + overrideCount +
     override list). For default kits produced by fit, coupling is
     set to 0 (main-bus-only, no sympathetic interaction) because
     the tool has no way to guess coupling from independent samples.
   - Phase-6 append — 160 × float64 macros at 0.5 (neutral).
3. **JSON intermediate** (per spec 141 §10) — optional, but trivial
   and extremely useful for debugging and for feeding into
   golden-test fixtures. Format exactly matches the one the plugin
   already understands.

Writer shares code with `plugins/shared/src/preset/` via a new
`membrum_preset_io` static library (§7.2) so the binary layout is
*literally the same code path* as the plugin's save/load.

---

## 5. C++ project layout

### 5.1 Directory structure

```
tools/membrum-fit/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp                  # CLI11 entry point
│   ├── cli.h / cli.cpp           # argument parsing
│   ├── loader.h / loader.cpp     # dr_wav + resample + normalise
│   ├── segmentation.h / .cpp     # onset + windowing
│   ├── features.h / .cpp         # Peeters/Lerch feature set
│   ├── exciter_classifier.h/.cpp # rule tree
│   ├── modal/
│   │   ├── matrix_pencil.h/.cpp
│   │   ├── esprit.h/.cpp
│   │   └── mode_selection.h/.cpp # MDL/ITC model-order
│   ├── body_classifier.h / .cpp  # mode-ratio scoring
│   ├── mapper_inversion/
│   │   ├── membrane_inverse.h/.cpp
│   │   ├── plate_inverse.h/.cpp
│   │   ├── shell_inverse.h/.cpp
│   │   ├── bell_inverse.h/.cpp
│   │   ├── string_inverse.h/.cpp
│   │   └── noise_body_inverse.h/.cpp
│   ├── tone_shaper_fit.h/.cpp
│   ├── unnatural_fit.h/.cpp
│   ├── refinement/
│   │   ├── render_voice.h/.cpp   # RenderableMembrumVoice
│   │   ├── loss.h/.cpp           # MSS + MFCC + envelope
│   │   ├── bobyqa_refine.h/.cpp
│   │   └── cmaes_refine.h/.cpp   # compile-time optional
│   ├── ingestion/
│   │   ├── wav_dir.h/.cpp        # JSON-mapped WAV directory
│   │   └── sfz_ingest.h/.cpp     # sfizz parser wrapper
│   └── preset_io/
│       ├── kit_preset_writer.h/.cpp
│       ├── pad_preset_writer.h/.cpp
│       └── json_writer.h/.cpp
├── extern/
│   ├── dr_libs/dr_wav.h          # vendored single-header
│   └── CLI11/CLI11.hpp           # vendored single-header (or submodule)
└── tests/
    ├── unit/
    │   ├── modal/                # synthetic damped-sinusoid tests
    │   ├── mapper_inversion/     # round-trip on default kit
    │   ├── body_classifier/
    │   └── preset_io/
    ├── golden/                   # fit Membrum-rendered samples,
    │                             # assert recovered params ≈ originals
    └── data/                     # small WAV fixtures (<1 MB total)
```

### 5.2 CMake extraction of Membrum DSP

Currently the entire Membrum DSP tree is *header-only* inside
`plugins/membrum/src/dsp/`, added as public headers to the
`Membrum` VST3 plugin target (see `plugins/membrum/CMakeLists.txt:45`).

Because **`plugin_ids.h`** and the **`preset/`** code pull in VST3
SDK types (`Steinberg::FUID`, `Steinberg::Vst::ParamID`,
`IBStream`), the tool cannot simply `target_link_libraries(Membrum)`.
We introduce **one new static library**:

```cmake
# plugins/membrum/CMakeLists.txt (new block)
add_library(membrum_dsp INTERFACE)
target_sources(membrum_dsp INTERFACE ...header list...)
target_include_directories(membrum_dsp INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(membrum_dsp INTERFACE KrateDSP)
```

`membrum_dsp` is **INTERFACE** — purely header-only, no VST3 SDK
dependencies. It exposes:

- `Membrum::ExciterType` / `BodyModelType` / `PadConfig`
- All 6 body mappers and bodies
- All 6 exciter headers
- `ToneShaper`
- `UnnaturalZone`
- `VoiceCommonParams`
- `membrane_modes.h` / `plate_modes.h` / `shell_modes.h` / `bell_modes.h`

It **does not** expose `plugin_ids.h`, `processor/`, `controller/`,
`voice_pool/`, or `preset/`. Those remain inside the Membrum plugin
target.

The Membrum plugin target then adds `membrum_dsp` to its own
dependencies and drops the explicit header listing — a pure
refactor that preserves the build.

A second tiny target, `membrum_preset_io`, holds the binary layout
writer code. It links against `Membrum::PadConfig` types (from
`membrum_dsp`) and the VST3 SDK `IBStream` helpers, but stays
independent of Processor/Controller. Both the plugin and the tool
consume it.

Top-level `CMakeLists.txt` gets:

```cmake
add_subdirectory(tools/membrum-fit)
```

and tool registers an ordinary `add_executable` linking
`membrum_dsp`, `membrum_preset_io`, `KrateDSP`, `nlopt_bobyqa`,
and any optional `libcmaes`.

### 5.3 Golden-test corpus

Generated by a **companion tool** (`membrum-fit-gen`) that takes a
`PadConfig` via JSON, renders it offline through the SAME
`RenderableMembrumVoice` used by refinement, and writes the WAV.
The test harness then:

1. Picks 200 random `PadConfig`s, covering all 6 exciters × 6 bodies
   plus key UnnaturalZone settings.
2. For each, render → WAV → feed through `membrum-fit`.
3. Assert: (a) exciter/body selection round-trips 100 %, (b) the
   refined `PadConfig` reproduces a voice within −25 dB multi-resolution
   spectral distance from the ground truth (a bar calibrated against
   human perceptual JND).

This is the single most important test surface: it provides a ground
truth that the physical drum-sample corpus (IDMT-SMT, Slakh, Decoded
Forms) cannot give us.

---

## 6. External dependencies and licensing summary

| Dependency | Method | License | Impact |
|---|---|---|---|
| dr_wav | vendored `extern/dr_libs/dr_wav.h` | PD / MIT-0 | zero-license friction |
| CLI11 | vendored single header | BSD-3 | zero-license friction |
| nlohmann/json | vendored single header | MIT | zero-license friction |
| Eigen 3.4 | FetchContent (same pattern as Google Highway) | MPL-2 | compatible |
| NLopt (BOBYQA subset) | vendored subset OR FetchContent | MIT/BSD (BOBYQA itself) | keep to permissive subset |
| libcmaes (optional) | FetchContent, gated by `MEMBRUM_FIT_ENABLE_CMAES` | LGPL | off by default |
| sfizz parser (optional) | FetchContent, gated by `MEMBRUM_FIT_ENABLE_SFZ` | ISC/BSD | off by default; tool still works for WAV-dir |
| pffft | already in repo | BSD | reused |
| KrateDSP | CMake sibling target | project | reused |
| membrum_dsp | new INTERFACE target | project | created by §5.2 extraction |
| membrum_preset_io | new static lib | project | created by §5.2 extraction |

---

## 7. Testing strategy

Following the canonical pattern laid out in the project's
`testing-guide` skill and the mandatory build-before-test discipline
from `CLAUDE.md`.

- **Unit tests (`membrum_fit_tests` executable)**
  - Modal extraction on synthetic `Σ A_k · exp(−γ_k t) · cos(2π f_k t)`
    signals at SNR ∈ {∞, 40, 20, 10 dB}. Assert recovered
    `{f, γ, A}` within 1 % / 5 % / 10 % tolerances respectively.
  - Mapper inversion round-trip: for 1000 random `VoiceCommonParams`,
    run `Mapper::map` → inverse → assert `|params_in − params_out| < 1e-4`.
  - Body classifier: for every `BodyModelType`, synthesise a clean
    modal signal using the mapper, assert the classifier picks the
    original body.
  - Preset-writer byte-exact tests against a hand-constructed
    reference blob (like the v6-migration tests already in spec 141).

- **Golden tests** — see §5.3.

- **Integration tests** — Feed a small set of hand-picked real drum
  samples (Creative Commons / user-provided) and assert the tool
  completes, produces valid preset files, and the files load back into
  Membrum without error.

- **Perceptual A/B tests** (manual, not CI) — render input sample vs
  fitted preset side-by-side; used during tuning phases only.

- **Anti-pattern discipline** — no loop assertions, capture
  slow-command output to log files (testing-guide feedback
  "capture_slow_tool_output").

---

## 8. Staged delivery phases

Ordered so each phase delivers a **working end-to-end pipeline**, even
if narrow. No "MVP" framing — each phase is fully realised for its
scope (per memory note "always full feature").

### Phase 1 — Synthetic Membrane Kick, end-to-end

Narrowest possible happy path to wire the whole pipeline before we
start improving accuracy.

- Loader + Onset + Attack-feature extraction (minimal subset).
- Exciter classifier restricted to `{Impulse, Mallet, NoiseBurst}`.
- Modal extraction: Matrix Pencil only.
- Body classifier: Membrane only (single-body fast-path).
- Mapper inversion: MembraneMapper only.
- ToneShaper fit: pitch envelope + filter cutoff only.
- Refinement: BOBYQA over a 6-parameter subset.
- Preset writer: per-pad only.
- Target input: Membrum-rendered Kick samples (golden-test corpus).
- CMake: extract `membrum_dsp` INTERFACE target. Tool builds as
  `tools/membrum-fit/`. First-phase `membrum_fit_tests` executable
  with the modal-extraction and mapper-inversion unit tests.

Exit gate: Phase 1 Kick golden-test corpus round-trips with 100 %
body-class accuracy and < −25 dB spectral distance.

### Phase 2 — All bodies, acoustic kit end-to-end

- Add Plate, Shell, Bell, String, NoiseBody mappers and their
  inversions.
- Body classifier full 6-way.
- Tone Shaper fit: filter env + drive detection.
- Exciter classifier full 6-way including Friction and Feedback.
- Kit preset writer (v6 binary).
- Ingestion: WAV directory + JSON mapping; SFZ still pending.

Exit gate: golden-test corpus (all exciter × body combos) passes;
two real public acoustic kits fit end-to-end and sound *recognisable*
in Membrum.

### Phase 3 — Unnatural Zone residual fit

- Mode Inject, Decay Skew, Nonlinear Coupling fitting.
- Material Morph fitting.
- Residual-signal analysis.
- ESPRIT added alongside Matrix Pencil.
- Model-order selection (MDL/ITC).

Exit gate: fitted presets for real cymbal / bell samples (inharmonic,
residual-heavy) reach < −22 dB spectral distance.

### Phase 4 — SFZ + batch kit fitting

- `sfizz` parser ingestion.
- Multi-file batch mode.
- Chok​e-group / output-bus inference from filename heuristics +
  MIDI-note GM map.
- Optional CMA-ES global-escape phase.
- JSON intermediate output.

Exit gate: Feed an industry-standard public SFZ drum kit; produce a
ready-to-load Membrum kit preset.

### Phase 5 — Multi-velocity / multi-layer (deferred; v2 scope)

Multi-velocity round-robin, choke-group clustering, coupling matrix
inference from pairs of samples played together. This is genuinely
different hard research (joint estimation across samples) and is
scoped out of v1.

---

## 9. Open questions / risks

1. **SNR on cymbal tails** — Matrix Pencil degrades below 10 dB SNR
   per mode. Cymbal high modes may be at −40 dB relative to the
   fundamental, which sits below typical recording noise floors. The
   NoiseBody hybrid approach (modal + noise) helps — we fit the
   strong modes, dump the weak ones into the noise layer — but the
   acceptance criterion for cymbals will have to be perceptual
   rather than parametric. Open.
2. **Multi-modal ambiguity (Plate vs Shell)** — Shell ratios are
   roughly `{1, 2.76, 5.4, ...}`, Plate has
   `{1, 2.5, 4.0, 5.0, 6.5, ...}`. For short-decay samples the
   classifier can be bi-modal. Mitigation: tie-break via attack
   spectrum brightness (plates ring brighter) AND run both bodies
   through refinement, keep lower-loss winner.
3. **Pitch envelope vs body stretch confusion** — A downward-sweeping
   pitch envelope can look to the fundamental tracker identical to a
   detuned `modeStretch < 1`. Mitigation: the pitch envelope only
   affects the **fundamental**; if higher modes' frequencies are
   stable while the fundamental sweeps, it's pitch-env. If all modes
   sweep together, it's `modeStretch` modulation. Deterministic.
4. **Numerical conditioning of Hankel-matrix SVD at small N** — for
   very short samples (< 50 ms of decay), `L = N/3` may be < 8 and
   SVD becomes unstable. Mitigation: minimum decay-window length of
   100 ms; reject shorter samples with a clear error message.
5. **Integer param encoding** — `exciterType`, `bodyModel`,
   `filterType`, `pitchEnvCurve`, `morphCurve`, `chokeGroup`,
   `outputBus`, `morphEnabled` are discrete-typed at the VST surface
   but serialised as `float64`. The writer MUST use exactly the same
   cast/round conventions as the existing Membrum state writer (see
   `specs/139-membrum-phase4-pads/data-model.md` §5). Regression test
   via byte-exact comparison against a kit written by the plugin
   itself.
6. **Perceptual vs parametric fit tension** — A low spectral distance
   does not guarantee perceptual match. We will run a small
   listening test at the end of Phase 2 and adjust loss weights
   (`wSTFT / wMFCC / wEnv`) accordingly.
7. **Coupling inference from individual samples is impossible** —
   Phase 5 coupling (spec 140) describes pad-to-pad sympathetic
   resonance, which cannot be observed in isolated single-hit
   samples. The tool will emit `couplingAmount = 0.5` (neutral
   default, matching `pad_config.h:151`) on every pad and leave
   coupling global knobs at zero. Documented limitation.
8. **Phase-6 macros (spec 141)** — Macros are *deltas* on top of the
   base parameter surface; their neutral value 0.5 produces zero
   delta. The tool will always emit 0.5 for every macro. If we ever
   want to "compress" the found parameters into macro space, that's
   an explicit v2 feature.
9. **License constraints when bundling LGPL libcmaes** — we keep CMA-ES
   optional behind a CMake flag so default builds stay under purely
   permissive licenses. Documented, enforced in CMake.
10. **Sample-rate mismatch** — Membrum's internal path is
    host-sample-rate; the tool normalises everything to 44.1 kHz.
    The fitted preset is sample-rate-agnostic (all params are
    sample-rate-independent normalised values), but the
    analysis-by-synthesis render MUST happen at the SAME rate the
    modal extraction was done at, or the modal frequencies drift.
    Enforced in `RenderableMembrumVoice::prepare()`.

---

## References (full list)

**Academic papers (modal analysis / estimation)**
- Hua & Sarkar (1990) — Matrix Pencil: [PDF](https://intra.ece.ucr.edu/~yhua/MPM.pdf) / [IEEE](https://ieeexplore.ieee.org/document/56027/)
- Badeau, David, Richard — ESPRIT performance: [HAL](https://inria.hal.science/hal-00945195/document)
- Badeau et al. — Multidimensional ESPRIT: [HAL](https://hal.science/hal-01360438v3/document)
- Badeau et al. — HR methods for audio (DAFx-02): [PDF](https://inria.hal.science/hal-00945272/file/dafx-02.pdf)
- Matane et al. — ESPRIT in Gabor for impact sounds: [ResearchGate](https://www.researchgate.net/publication/261640125_Modal_Analysis_of_Impact_Sounds_with_ESPRIT_in_Gabor_Transforms)
- Structure-Aware Matrix Pencil (arXiv 2025): [arXiv](https://arxiv.org/html/2502.17047v2)
- EDS parametric modeling (Badeau et al.): [Academia](https://www.academia.edu/19361684/EDS_parametric_modeling_and_tracking_of_audio_signals)

**Drum / percussion physical modeling**
- Bilbao — Snare drum FDTD (JASA 2012): [PubMed](https://pubmed.ncbi.nlm.nih.gov/22280714/)
- Torin — 3D percussion PhD thesis: [PDF](https://www.albertotorin.it/files/ATorin_PhDThesis.pdf)
- Modal Analysis of a Snare Drum (Fischer UIUC): [PDF](https://courses.physics.illinois.edu/phys406/sp2017/Student_Projects/Spring14/Matthew_Fischer_Physics_406_Final_Project_Sp14.pdf)

**Timbre features**
- Peeters et al. (2011) — Timbre Toolbox (JASA): [PDF](https://www.mcgill.ca/mpcl/files/mpcl/peeters_2011_jasa.pdf)
- Lerch — Audio Content Analysis textbook: [PDF](http://telit.etf.rs/download/An%20Introduction%20to%20Audio%20Content%20Analysis.pdf)
- Caetano — Audio Content Descriptors of Timbre: [PDF](https://comma.eecs.qmul.ac.uk/assets/pdf/Caetano_chap11.pdf)
- Physical and Perceptual Aspects of Percussive Timbre: [PDF](https://escholarship.org/content/qt5bx4j1fj/qt5bx4j1fj_noSplash_4650521b252cbaf7fb55a36f88039016.pdf)

**Differentiable synthesis / sound matching**
- Engel et al. (2020) — DDSP (ICLR): [PDF](https://openreview.net/pdf?id=B1x1ma4tDr)
- Hayes et al. (2023) — DDSP review (Frontiers SP): [link](https://www.frontiersin.org/journals/signal-processing/articles/10.3389/frsip.2023.1284100/full)
- Masuda & Saito (2023) — semi-supervised DDSP sound matching: [IEEE](https://dl.acm.org/doi/10.1109/TASLP.2023.3237161)
- DiffSound — Differentiable modal (OpenReview): [OpenReview](https://openreview.net/forum?id=6jFjYmahxu)
- DiffMoog — modular synth sound matching (arXiv 2024): [arXiv](https://arxiv.org/html/2401.12570v1)
- Differentiable Modal Synthesis for String Sound (arXiv 2024): [arXiv](https://arxiv.org/html/2407.05516v1)
- Parallel WaveGAN — multi-resolution STFT loss: [arXiv](https://arxiv.org/pdf/1910.11480)
- Multi-Scale Spectral Loss Revisited — Schwär & Müller (2023): [PDF](https://www.audiolabs-erlangen.de/content/05_fau/assistant/00_schwaer/01_publications/2023_SchwaerM_MultiScaleSpecLoss_IEEE-SPL.pdf)
- auraloss — Steinmetz (PyTorch impl reference): [GitHub](https://github.com/csteinmetz1/auraloss)

**C++ libraries**
- dr_wav — mackron/dr_libs: [GitHub](https://github.com/mackron/dr_libs/blob/master/dr_wav.h)
- libsndfile (rejected LGPL alternative): [site](http://libsndfile.github.io/libsndfile/)
- Eigen: [GitLab](https://libeigen.gitlab.io/)
- Spectra (eigenvalue solver on Eigen): [GitHub](https://github.com/yixuan/spectra)
- NLopt: [docs](https://nlopt.readthedocs.io/en/latest/NLopt_Algorithms/)
- NLopt in pagmo2: [docs](https://esa.github.io/pagmo2/docs/cpp/algorithms/nlopt.html)
- CLI11: [GitHub](https://github.com/CLIUtils/CLI11)
- sfizz: [GitHub](https://github.com/sfztools/sfizz)
- SFZ spec: [sfzformat.com](https://sfzformat.com/)
- SFZ opcodes (lokey/hikey): [sfzformat.com](https://sfzformat.com/opcodes/hikey/)
- SFZ Wikipedia: [link](https://en.wikipedia.org/wiki/SFZ_(file_format))
- MUSIC / ESPRIT pedagogical impl: [GitHub](https://github.com/shantistewart/MUSIC-and-ESPRIT-Algorithms)
- Loris (CERL Sound Group): [site](https://www.cerlsoundgroup.org/Loris/) / [mirror](https://github.com/tractal/loris)

**Membrum internal references**
- `specs/135-membrum-drum-synth.md` — full design spec
- `specs/139-membrum-phase4-pads/data-model.md` §5, §7 — binary layouts
- `specs/140-membrum-phase5-coupling/data-model.md` §5 — v5 state extension
- `specs/141-membrum-phase6-ui/data-model.md` §9 — v6 migration
- `plugins/membrum/src/plugin_ids.h` — parameter IDs and state version
- `plugins/membrum/src/dsp/pad_config.h` — per-pad param offsets
- `plugins/membrum/src/dsp/bodies/*_mapper.h` — forward mapping functions
- `plugins/membrum/src/dsp/*_modes.h` (plate/bell/shell/membrane) — reference ratio tables
- `plugins/membrum/src/dsp/tone_shaper.h` — post-body chain
- `plugins/membrum/src/dsp/default_kit.h` — GM template defaults
- `plugins/membrum/CMakeLists.txt` — Membrum CMake target
