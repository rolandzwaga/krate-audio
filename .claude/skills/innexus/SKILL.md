---
name: innexus
description: Domain knowledge for the Innexus harmonic-resynthesis instrument — the analysis→resynthesis signal path, the two analysis front-ends, load-bearing invariants (mono harmonic-sieve, per-partial bandwidth), and where the deep docs live. Use when working on Innexus, harmonic/additive resynthesis, sample or live spectral analysis, or Innexus presets — especially outside plugins/innexus/ where the leaf CLAUDE.md won't auto-load.
allowed-tools: Read, Glob, Grep
---

# Innexus — Harmonic Analysis / Resynthesis Instrument

Innexus (AU `aumu`) analyzes audio into per-frame harmonic snapshots (`HarmonicFrame` + `ResidualFrame`)
then additively resynthesizes them with evolution/physics/modulation shaping. When editing under
`plugins/innexus/`, its leaf `CLAUDE.md` auto-loads with build/test/param facts — this skill carries the
domain lore that applies when reasoning about Innexus from elsewhere (DSP library, presets, tooling).

## Signal path (per voice)

Two input sources feed one frame contract, selected by `inputSource_`:
- **Sample mode** (≤0.5): drag-dropped WAV → `SampleAnalyzer` (background `std::thread`) → immutable
  `SampleAnalysis`, published to the audio thread by atomic pointer swap; voice advances `currentFrameIndex`.
- **Sidechain mode** (>0.5): aux bus 0 downmixed to mono → `LiveAnalysisPipeline` **on the audio thread**,
  emitting one frame per STFT hop.

Frame selection (`voice_.morphedFrame`): Recall│Capture│ManualFreeze+morph(lerp)│confidence-gated freeze,
then `applyModulatorAmplitude` → `applyHarmonicPhysics` → `broadcastFrameToVoices` (copy to each voice).
Per voice: `HarmonicOscillatorBank` (up to `kMaxPartials` = 96 Gordon-Smith MCF sines; freq/amp/**phase** seeded from partials,
phase state NOT reset across frames = continuity) + exciter{Impact│Bow│Residual} → resonator{Modal│Waveguide,
equal-power xfade} → BodyResonance → `PhysicalModelMixer(residual↔physical)`. Σ voices × 1/√activeCount →
global `SympatheticResonance` → master gain → tanh soft-limiter → out.

## Analysis pipeline

Both paths share: `PreProcessingPipeline` (DC → 30 Hz HPF → transient suppress → noise gate) → YIN pitch →
dual Blackman-Harris STFT (**short 1024/512** for temporal tracking, **long 4096/2048** for low-freq
resolution) → `PartialTracker` (Hungarian match + harmonic sieve) → `HarmonicModelBuilder` (dual-timescale
smooth) → residual bands. Offline adds a **two-pass polyphony guard**: if F0 is unstable/noisy, pass 2
re-runs with a fixed F0 and `voiced=false` (sieve OFF) so all peaks survive — the conditional poly fix.

## Load-bearing constraints (violating these breaks presets, tests, or sound)

- **Never globally disable the mono harmonic sieve.** The sieve runs only when `f0.voiced`; poly/pass-2 code
  turns it off *conditionally* via a neutral `voiced=false` F0. The monophonic sieved path is load-bearing.
- **Per-partial `bandwidth` (0=sine..1=noise) is the historical noise bug.** Inflated bandwidth injects noise
  on *every* partial. Physics/modulators touch amplitudes only; pass-2 poly sets `bandwidth=0` (unreliable
  without the sieve). Treat any bandwidth write with suspicion.
- **No frees on the audio thread.** Superseded `SampleAnalysis` goes to `pendingDeletion_`, freed only in
  `setActive`/dtor. `currentAnalysis_` is an atomic acquire/release swap.
- **UI data bridge:** `sendDisplayData` piggybacks a `DisplayData` block on the DataExchange handler
  (~30 Hz throttle) with a Tier-3 `sharedDisplay_` polling fallback — don't add new IMessage loops.
- **Param IDs:** section bases — 200s = envelope, 400s = harmonic. `kPartialCountId`=202 is a
  **StringListParameter** ("48"/"64"/"80"/"96"), not a range — never swap a registered VST3 param type.
- **Preset categories are FIXED (7):** `Brass and Winds`, `Drums and Perc`, `Found Sound`, `Keys`,
  `Pads and Drones`, `Strings`, `Voice`. Never invent new ones.

## Deep tooling (read these for real work)

- **Plugin specifics:** [`plugins/innexus/CLAUDE.md`](../../../plugins/innexus/CLAUDE.md) — skeleton,
  param-ID scheme, `innexus_tests` target (unit `tests/unit/{processor,vst}/` + `tests/integration/`,
  run both), pluginval path, AU config.
- **Frame contract & shared DSP:** `dsp/include/krate/dsp/processors/harmonic_types.h`
  (`HarmonicFrame`, `Partial`, `kMaxPartials`), `residual_types.h`, `harmonic_oscillator_bank.h`.
- **Analysis internals:** `plugins/innexus/src/dsp/` (`sample_analyzer`, `live_analysis_pipeline`,
  `pre_processing_pipeline`, `dual_stft_config.h`) and shared `partial_tracker.h` / `harmonic_model_builder.h`.
