# Changelog

All notable changes to Membrum will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-04-11

### Added

- **6 exciter types** -- Impulse (Phase 1 carry-over), Mallet, Noise Burst,
  Friction (transient mode), FM Impulse (1:1.4 ratio default), and Feedback
  with energy limiter. Each exciter implements a velocity-controlled spectral
  response so soft hits are darker than hard hits.
- **6 body models** -- Membrane (16-mode Bessel, Phase 1 carry-over), Plate
  (16 modes), Shell (12 modes), String (waveguide), Bell (16 Chladni modes),
  and Noise Body (hybrid up to 40 modes plus broadband layer).
- **Swap-in architecture** -- ExciterBank and BodyBank pre-allocate every
  exciter/body variant up front. Voice switches selectors via tagged-union
  dispatch with no audio-thread allocation, no virtual calls, and no
  per-sample branching in the hot path. Silent switches take effect
  immediately; sounding switches defer to the next note-on.
- **Tone Shaper** -- Per-voice signal-shaping chain consisting of Drive
  (alias-safe waveshaper) → Wavefolder → DC blocker → State-Variable
  Filter (LP/HP/BP) with a dedicated 4-stage filter envelope, plus an
  absolute-Hz pitch envelope (start, end, time, exp/lin curve) for
  808-style transient pitch sweeps. Bypass identity is bit-exact within
  -120 dBFS of the dry path.
- **Unnatural Zone** -- Mode Stretch (0.5-2.0), Decay Skew (-1..+1), Mode
  Inject (with phase randomization), Nonlinear Coupling (with energy
  limiter), and Material Morph (2-point envelope, 10-2000 ms). All
  parameters are bypass-identity at their defaults.
- **29 new parameters** -- Exciter Type and Body Model selectors, plus
  Tone Shaper, Unnatural Zone, and Material Morph parameters. All
  follow the `k{Section}{Parameter}Id` naming convention.
- **State version 2** -- Phase 2 state layout with backward-compatible
  loader: a Phase-1 (version=1) state blob loads cleanly with all new
  parameters at their defaults (Impulse + Membrane + bypass).
- **144-combination CPU validation** -- Automated `[.perf]` benchmark
  iterating every (exciter × body × tone_shaper × unnatural) combination
  at 44.1 kHz to verify the 1.25% single-voice CPU budget. The
  Feedback + Noise Body + Tone Shaper + Unnatural cell carries a
  documented Phase 9 waiver up to the 2.0% hard ceiling; all other 143
  cells are gated at 1.25%.
- **Pluginval strictness 5** -- Membrum.vst3 passes pluginval at the
  highest strictness level on Windows.
- **auval (macOS CI)** -- `auval -v aumu Mbrm KrAt` is wired in the
  GitHub Actions macOS job; AU bus configuration (0 in / 2 out) is
  unchanged from Phase 1.

## [0.1.0] - 2026-04-08

### Added

- **Plugin scaffold** -- CMake target, entry point, processor/controller skeletons, AU configuration, Windows resources, CI integration
- **Single drum voice** -- ImpactExciter + ModalResonatorBank (16 Bessel membrane modes) + ADSREnvelope signal path
- **5 parameters** -- Material (woody/metallic), Size (small/large), Decay (short/long), Strike Position (center/edge), Level (volume)
- **MIDI note 36 trigger** -- Single voice responds to C1 note-on/off with velocity-sensitive excitation
- **Velocity response** -- Soft hits produce dark/muted tones, hard hits produce bright/punchy tones with wider bandwidth excitation
- **State save/load** -- Binary state format with version field for forward compatibility
- **Host-generic editor** -- No custom UI; parameters visible in DAW's built-in parameter editor
- **Cross-platform** -- VST3 (Windows, macOS, Linux) + Audio Unit (macOS)
