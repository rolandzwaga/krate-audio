# Changelog

All notable changes to Membrum will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.9.0] - 2026-04-26

### Added

- **Master output gain** (right column, top of the Selected-Pad column).
  RangeParameter `[-24..+12] dB`, default **-6 dB**, applied to the main
  stereo bus only (aux buses are pre-master). Out-of-the-box hits now
  peak at -10 dBFS for kicks / -9 dBFS for toms instead of the old
  -4..-3 dBFS, leaving 6 dB of headroom for layering and bus
  processing. State persists in the kit blob (`v13`+).
- **Per-knob value displays in the Master and Coupling sections** --
  `CParamDisplay` cells under each knob render formatted strings
  (`-6.0 dB`, `37 %`, `1.00 ms`) using a new
  `formatLinearDb(min, max)` formatter and explicit `formatPercent` /
  `formatLinearMs` cases for the four coupling globals + master gain.
  Coupling section grew 92 -> 106 px to fit the readouts; Meter and
  Matrix sections shifted down by 14 px.
- **Coupling-knob registration with proper ranges + units** --
  Global / Snare / Tom Resonance now register as `[0, 100] %` with
  precision 0; Coupling Delay registers as `[0.5, 2.0] ms` with
  precision 2; Master Gain registers with precision 1. ArcKnob popups
  and the new `CParamDisplay` readouts both pick up the formatted
  strings via `getParamStringByValue`.
- **Output-level measurement test (`[.measure]` tag)** --
  `test_output_level_measure.cpp` renders 2 s of every default-kit
  pad-type at vel=1.0 and vel=0.5 and prints peak / RMS dBFS per pad.
  Excluded from the default test run; useful as a baseline for
  level-balancing work.

### Changed

- **State blob bumped to v14**. v13 added a `float64` master-gain slot
  at the end (after macros). v14 drops the Tier 2 coupling-matrix
  override block (`uint16` count + `[uint8 src, uint8 dst, float32
  coeff]` entries) since the Matrix UI was removed -- nothing
  produces overrides anymore. Legacy v6..v13 readers parse-and-discard
  the override bytes so older blobs still load. Net blob delta vs
  v0.8.0: `+8` (master gain) `-2` (override count) = `+6` bytes.

### Removed

- **32x32 Coupling Matrix UI and the Tier 2 override layer** -- the
  per-cell painting tool was effectively unused (no factory preset
  populated it, the cells were too small to paint by hand, and the
  audible payoff per cell was tiny because each coefficient capped at
  0.05). Stripped:
  - `editor.uidesc`: the Matrix fieldset, the 32x32
    `CouplingMatrixView`, and the Solo / Reset buttons.
  - Source files: `src/ui/coupling_matrix_view.{h,cpp}` and
    `src/dsp/matrix_activity_publisher.h` deleted entirely.
  - Controller: the `uiCouplingMatrix_` mirror, the
    `sendCouplingMatrixEdit` / `requestCouplingMatrixSnapshot` IMessage
    bridge, the `CouplingMatrixSnapshot` reception path, and the three
    custom-view registrations (`CouplingMatrixView`, `MatrixSoloButton`,
    `MatrixResetButton`).
  - Processor: the `MatrixActivityPublisher` member + per-block
    publish loop, and the `CouplingMatrixEdit` /
    `CouplingMatrixSnapshotRequest` `notify()` handlers.
  - `CouplingMatrix` class: the `setOverride` / `clearOverride` /
    `clearAllOverrides` API, the `hasOverride_` / `overrideGain_`
    storage, the `getOverrideCount` / `forEachOverride` /
    `hasOverrideAt` / `getOverrideGain` accessors, and the `Solo`
    state (`setSoloPath` / `clearSolo` / `hasSolo` / `soloSrc` /
    `soloDst` + the lock-free `std::atomic<int>` pair). The class is
    now a pure single-tier resolver: globals + categories + per-pad
    amounts -> `effectiveGain[][]`.
  - State codec: `KitSnapshot::overrides` field and the
    `TierTwoOverride` struct.
  - Tests: `test_matrix_activity_publisher.cpp` and
    `test_coupling_matrix_view.cpp` deleted; the override-related
    `TEST_CASE`s in `test_coupling_matrix.cpp`,
    `test_coupling_state.cpp`, `test_state_codec.cpp`, and
    `test_kit_switch_infinite_ring.cpp` removed; size assertions in
    `test_state_v6_migration.cpp` / `test_ui_mode_session_scope.cpp`
    updated for the 2-byte savings.

  The Tier 1 coupling section (Global / Snare Buzz / Tom Resonance /
  Coupling Delay) is unchanged -- those were always doing the useful
  work via pad-category dispatch (kick->snare, tom->tom).

## [0.8.0] - 2026-04-26

### Fixed

- **MacroMapper preset-load destruction (the "808 toms all sound the
  same pitch" bug)** -- Every `applyTightness/Brightness/BodySize/
  Punch/Complexity` was overwriting the per-pad `cfg.field` with
  `defaults_.byOffset[X] + delta` on each invocation, and the per-pad
  cache was initialised to a -1.0 sentinel that forced the FIRST apply
  to fire. When a kit preset arrived via the host's parameter-dispatch
  path, every pad's macros (typically at neutral 0.5) triggered the
  first-apply, which clobbered `cfg.size`, `cfg.material`, `cfg.decay`,
  etc. with `defaults + 0`, collapsing the per-pad preset values to
  their registered defaults. For the 808 kit's six toms (sizes
  0.85..0.40), this turned every tom into mode 0 = 158 Hz exactly
  (= 500·0.1^0.5). MacroMapper now applies *incremental* deltas
  `(linDelta(newM) - linDelta(oldM))` onto `cfg.field` directly, so
  neutral macros produce zero adjustment and a freshly loaded preset
  is left untouched. `Processor::setState` no longer calls
  `reapplyAll` (which would drift bytes on every save-load cycle with
  the new contract); it instead syncs the cache to the loaded macro
  values via the new `MacroMapper::syncCacheFromCfg` so subsequent
  knob movements see the correct previous state.
- **Tone Shaper / Pitch Envelope / Unnatural Zone / Material Morph
  not propagating into voices on note-on** -- `applyPadConfigToSlot`
  in `VoicePool` was missing the per-pad propagation block for these
  fields entirely. The processor's `applyPadConfigToVoice` helper
  carried the correct logic but was never invoked. Voices played
  with whatever Tone Shaper state happened to be set when the plugin
  booted, so per-pad pitch envelopes (the iconic 808 boom-thud
  glide), per-pad filter envelopes, mode stretch, decay skew, mode
  inject, nonlinear coupling, and material morph all silently
  dropped on every kit-preset load. Fix: the missing block is now
  in `applyPadConfigToSlot` and runs on every voice allocation.
- **Dangling pointer crash on rapid Simple/Advanced view toggles** --
  Cached views inside the editor controller could be deleted by the
  framework while still referenced; the controller now subscribes
  via `IViewListener` and nulls the cached pointers on
  `viewWillDelete`.
- **Factory preset categories aligned to the four hardcoded slots** --
  Subdirectories and XML metadata now match the only categories the
  preset browser exposes (Acoustic, Electronic, Percussive,
  Unnatural). The earlier "Experimental" subdir wouldn't load.

### Added

- **Per-pad Enabled toggle (Phase 8F)** -- New `kPadEnabled` global
  proxy + per-pad parameter (offset 59). Disabled pads silently
  short-circuit `VoicePool::noteOn`: no allocator events, no choke
  iteration, no audition. The pad-grid view shows a bottom-right
  power glyph; clicking it toggles enable. State blob version bumped
  to v12 to carry the new slot. Legacy v6..v11 kits load with all
  pads enabled by default.
- **Three factory kits regenerated end-to-end** -- Acoustic Studio,
  808 Electronic, Experimental FX. Each uses the full Phase 7+
  feature set (parallel noise + click layers, per-mode damping law,
  air-loading, head/shell coupling, tension modulation) for
  per-pad character. Uncrafted slots are explicitly disabled via
  the new Phase 8F toggle so each kit reads as "12-13 sounds wired"
  rather than "32 pads with 19 default beeps".
- **Membrum-fit "disable un-generated pads" mode** -- The offline
  fitter now writes `enabled = 0` for any pad it didn't fit, so a
  partial-coverage source (e.g. 6 sample WAVs) produces a kit
  where the un-fitted slots are silenced rather than left as the
  generic default voice.

### Changed

- **Editor right-column reorganised into fieldsets** -- Kit Browser,
  Pad Browser, Voices, Coupling, Meter, Matrix each in its own
  fieldset starting at y = 28 to match the left-column layout.
- **Character fieldset split** -- Material / Size / Decay /
  StrikePos / Level (Character) and Material Morph (Material Morph)
  are now separate fieldsets in Advanced view. Material Morph has
  its own power toggle; disabled controls dim out.
- **Controller-side KitSnapshot bridge unified** -- The three
  formerly-divergent kit-blob translation paths
  (`setComponentState`, `kitPresetLoadProvider`,
  `kitPresetStateProvider`) collapse to a single
  `controller_state_codec` module. Eliminates ~250 lines of
  duplicated code and four missing-global writes that previously
  silently dropped Phase 7+ params on the controller side.

## [0.7.0] - 2026-04-23

### Added

- **Parallel noise + click layers (Phase 7)** -- Every voice now renders an
  always-on filtered noise path and an attack "click" transient (2-5 ms
  raised-cosine filtered-noise burst) alongside the modal body. Research-
  backed realism ingredients (Cook SNT, Serra/Smith SMS, Chromaphone /
  Microtonic): the modal body alone sounds "glass tap"; the noise + click
  layers restore the stochastic residual that real drum recordings always
  carry.
- **Per-mode damping law (Phase 8A)** -- `bodyDampingB1` + `bodyDampingB3`
  are now first-class per-pad params driving `R_k = b1 + b3·f²` on every
  mode independently (Chaigne & Askenfelt 1993; Aramaki / KM 2011). The
  primary material-perception axis: the same body can be dialled from
  metallic ring (low b3) to woody thump (high b3) without touching its
  fundamental. Legacy `decay` / `material` still work as convenience
  derivations when the B1/B3 sentinels are untouched.
- **Mode count bump (Phase 8B)** -- Membrane 16 -> 48 modes, Plate 16 ->
  48, Shell 12 -> 32 (Chromaphone's "High" density band). Bell stays at
  16 (source data limit). Extended Bessel table covers m = 0..14; series
  evaluation bumped from 12 to 20 terms for high-order stability.
- **Air-loading correction (Phase 8C)** -- Structured low-mode frequency
  depression tabulated from Rossing's timpani data (5 % at k = 0, tapering
  to near-zero by k ~ 12). Closes the "whistly / detuned-bar" failure
  mode on Membrane bodies and lets the lowest modes drop into realistic
  kick / tom sub-bass territory. Per-pad `airLoading` knob blends between
  pure Bessel and full Rossing curve. Separate `modeScatter` knob wires
  the bank's existing sinusoidal dither for a small "natural imperfection"
  layer on top.
- **Head <-> shell coupling with secondary modal bank (Phase 8D)** --
  `DrumVoice` now carries a second `ModalResonatorBank` (24 modes, shell
  ratios) that runs in parallel and exchanges energy with the head via a
  scalar coupling coefficient at block rate. Matches Chromaphone 3's
  bidirectional-coupling idiom; closes the "no body weight" gap on kicks
  and toms. Stability-clamped to a 0.25 effective maximum so the two-bank
  feedback loop's eigenvalue stays below 1 across all decay combinations.
  Four new per-pad params: `couplingStrength`, `secondaryEnabled`,
  `secondarySize`, `secondaryMaterial`. Default off so Phase 1 bit-identity
  holds at neutral settings.
- **Nonlinear tension modulation (Phase 8E)** -- Energy-dependent pitch
  glide reproducing the tom-tom "kerthump" character (JASA 2021 Kirby &
  Sandler; Avanzini & Rocchesso 2012). One-pole energy follower (20 ms
  time constant) drives a block-rate frequency scale on the modal bank's
  state-preserving `updateModes()`. Depth is scaled by velocity² at
  noteOn -- soft hits sound the same, hard hits bend up to ~2 semitones
  down during the note. Per-pad `tensionModAmt` knob; orthogonal to the
  existing scripted pitch-envelope (`tsPitchEnvStart/End/Time`) so 808-
  style transient sweeps remain independent.
- **9 new per-pad parameters** -- Global proxy IDs 300..308 (Body
  Damping B1, Body Damping B3, Air Loading, Mode Scatter, Coupling
  Strength, Secondary Enabled, Secondary Size, Secondary Material,
  Tension Mod Amount). Each is surfaced as an ArcKnob in the selected-pad
  extended panel. Total VST3 parameter count 1652 -> 1949.
- **State version 7 -> 11** -- PadSnapshot sound array extended 42 -> 51
  slots. Back-compat loader migrates v6/v7/v8/v9/v10 blobs by filling the
  new slots with PadConfig defaults. Pad-preset version bumped alongside
  the kit blob.
- **`tools/membrum-fit` offline sample-to-preset fitter** -- A
  stand-alone CLI (`membrum_fit.exe per-pad | kit`) that takes drum WAVs
  (or an SFZ / kit JSON) and produces Membrum `.vstpreset` files via
  modal-decomposition (Matrix Pencil / ESPRIT), per-body mapper inversion
  (Membrane / Plate / Shell / Bell / String / NoiseBody), Tone Shaper
  and Unnatural Zone fitting, and BOBYQA / CRS loss refinement (MSS +
  MFCC + log-envelope). A `--body-override MIDI=body` flag bypasses the
  body classifier for mis-classified samples. With `--max-evals > 300`
  the refinement extends from the original 6D core-param subset to the
  full 14D Phase 8 set so fitted kits carry the new DSP character rather
  than only the legacy decay / material axis.

### Changed

- **Global `selfCoupling` proxy (v6 artefact) replaced** with the
  per-pad `secondaryEnabled` + `couplingStrength` pair. The old
  single-knob design could not express the per-pad-per-body physics the
  Phase 8D two-bank architecture requires.
- **Air-loading applied globally to voice pitch envelope output** so
  scripted pitch sweeps (`tsPitchEnvStart/End/Time`) land on
  air-loaded fundamentals rather than pure-Bessel ones -- otherwise the
  scripted sweep starts at a different frequency than the steady-state
  body.

### Fixed

- **Amp envelope decoupled from voice lifetime (Phase 8A.5)** -- Voices
  now track amplitude envelope independently of their "alive" flag, so
  a freshly retriggered voice doesn't cut off a still-ringing tail from
  the previous note.
- **Phase 8B Bessel table series stability** -- Series term count raised
  12 -> 20 so the amplitude evaluation remains accurate across the
  higher-order Bessel zeros the 48-mode membrane now uses.

## [0.6.0] - 2026-04-13

### Added

- **Custom VSTGUI editor** -- Full replacement for the host-generic editor,
  featuring a 4x8 pad grid, selected-pad parameter panel, kit column with
  preset browsers, voice-management UI, output-routing UI, and a Tier 2
  coupling matrix editor. Editor size is session-scoped (not persisted to
  state). Cross-platform (Windows, macOS, Linux) via VSTGUI only.
- **5 per-pad macros** -- Tightness, Brightness, Body Size, Punch, and
  Complexity. Each macro maps to a curated set of underlying DSP parameters
  via the MacroMapper, allowing single-knob sound shaping per pad while
  leaving all underlying parameters independently automatable.
- **Acoustic / Extended UI mode** -- Session-scoped UI mode toggle that
  filters the selected-pad panel to show either the Acoustic (natural
  drum) parameter subset or the full Extended parameter set. Does not
  affect DSP or automation; all parameters remain reachable via host
  automation regardless of mode. Mode is not persisted to plugin state.
- **4x8 pad grid** -- Visual pad layout mapping MIDI notes 36-67 to a
  4-row, 8-column grid. Per-pad glow reflects voice activity via a
  lock-free PadGlowPublisher (audio -> UI), with level and category
  color coding.
- **Kit and per-pad preset browsers** -- Integrated browsers in the
  custom editor for loading kit presets (all 32 pads) and individual
  pad presets (single pad, preserves other pads). Kit preset files
  now round-trip the UI mode alongside DSP state.
- **Tier 2 coupling matrix editor** -- Per-pair coupling override UI
  (32x32 matrix) layered on top of the Tier 1 Snare Buzz / Tom
  Resonance knobs from Phase 5. Activity indicators driven by a
  lock-free MatrixActivityPublisher.
- **Pitch envelope promotion** -- Pitch envelope (start/end/time/curve)
  is now a primary selected-pad control in the Acoustic UI mode,
  surfaced alongside Material / Size / Decay rather than being buried
  in the Tone Shaper subsection.
- **State version 6** -- Membrum binary state bumped to v6 with
  migration paths from v1-v5. UI mode and editor size are explicitly
  NOT persisted to state (session-scoped only).

## [0.3.0] - 2026-04-12

### Added

- **Multi-voice polyphony** -- 16 pre-allocated DrumVoice instances in a
  fixed-size VoicePool, with a user-configurable `maxPolyphony` parameter
  (range 4-16, stepped, default 8). Unused slots are idle (zero CPU).
- **3 voice-stealing policies** -- Oldest (default, allocator-driven),
  Quietest (processor-layer amplitude scan), and Priority (highest-pitch
  stolen first, protects kick/snare). All deterministic with slot-index
  tiebreaker.
- **Click-free fast-release** -- 5 ms exponential decay envelope
  (k = exp(-ln(1e6)/(0.005*sr)), floor 1e-6f) applied by VoicePool on
  scratch buffer. Stolen/choked voices crossfade naturally with incoming
  voice. Peak click artifact <= -30 dBFS across all policies and sample
  rates (22050-192000 Hz).
- **8 choke groups** -- ChokeGroupTable with 32-entry fixed lookup
  (MIDI 36-67). Group-wide fast-release on note-on (SoundFont 2.04
  Exclusive Class semantics). Cross-group isolation verified.
- **State version 3** -- 302-byte binary state with v1->v3 and v2->v3
  migration paths. Corruption clamping on load (maxPoly [4,16],
  policy [0,2], choke [0,8]).
- **3 new parameters** -- Max Polyphony (kMaxPolyphonyId=250),
  Voice Stealing (kVoiceStealingId=251), Choke Group
  (kChokeGroupId=252). Visible in host-generic editor.
- **CPU budget verified** -- 8-voice worst-case 5.952% (budget 12%),
  16-voice stress 0 xruns over 10s. Zero heap allocations on audio
  thread across 10-second fuzz with steals, chokes, and param changes.
- **Phase 2 regression** -- maxPolyphony=1 output is byte-identical
  to Phase 2 single-voice reference (maxDiff=0.0).

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
