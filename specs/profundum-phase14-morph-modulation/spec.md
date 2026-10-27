# Feature Specification: Profundum Phase 14 — Morph Modulation

**Spec slug:** `profundum-phase14-morph-modulation`
**Roadmap:** `specs/Profundum-roadmap.md`, Part B, **Phase 14** (lines 1075–1115), the Phase 8 status and handoff block
(lines 575–609), the Phase 11 / 12 / 13 status blocks (lines 811–836, 1029–1056) and the cross-cutting constraints
(lines 1135–1158). Concept: `specs/Profundum-concept.md` (§10, the A/B morph).
**Depends on:** Phases 1–13 (all ✅ in the roadmap). Branch `feat/profundum-preset-morph`, HEAD `3dfc5b7d` when this spec was
written.
**Release:** part of Profundum **1.0.0** (roadmap line 1081). `plugins/profundum/version.json` already reads `1.0.0` and no
`profundum` git tag exists, so the `[1.0.0]` CHANGELOG entry is still open and takes the new bullets; `version.json` is not edited.
**Status:** DRAFT. Specification only.
**Date:** 2026-10-26

---

## Overview

Today Bass Morph moves only when the user moves it. The roadmap describes three gaps (line 1079–1081), and each was checked
against the code this session:

- **Morph Position (105)** reaches the engine once per block as one value. `Processor::pushParamsToEngine`
  (`plugins/profundum/src/processor/processor.cpp:957-980`) ends with `engine_->setMorphPosition(position)`. The engine passes it
  straight to `BassMorphEngine::setPosition` (`dsp/include/krate/dsp/systems/profundum_engine.h:270`).
- **Motion** routes only to Mass, Depth, Body, Edge and Shift. Its `Target` enum is `{ Mass, Depth, Body, Edge, Shift }`
  (`dsp/include/krate/dsp/systems/morph_state.h:83`), and `ProfundumMotion::advanceInterval` routes to exactly those five
  (`profundum_motion.h:281-298`).
- **The mod wheel and channel pressure** push Mass only. `Processor::applyEndpoints` computes
  `c = clamp(Mod Wheel + Channel Pressure, 0, 1)` and adds `c · (hi − lo)` to each endpoint's Mass (`processor.cpp:921-950`).
- **Velocity** is used only for note handling. "velocity is forwarded to MonoHandler / VoiceAllocator and otherwise unused"
  (`profundum_engine.h:221`).

So a per-note gesture such as "pluck at A, settle into B" needs a host automation lane drawn for every note.

This phase adds one **global Morph Modulation** section with five sources: **Off · Envelope · Cycle · Velocity · Mod Wheel**
(roadmap line 1085). It computes `clamp(Morph Position + Amount × source, 0, 1)` once per 32-sample control interval and feeds the
result through the existing Morph Ramp Time slew (line 1089–1090). It sits outside both endpoints (line 1086–1088). It also brings
the surface the Phase 10–13 rules require (line 1104–1108): host parameters, the state round trip, the editor, tooltips, the
manual, and factory presets that show off every source with measurements.

## Scope

1. **One new KrateDSP component**, `ProfundumMorphModulator` (Layer 3, `systems/profundum_morph_modulator.h`). It holds the
   Envelope (`ADSREnvelope`), Cycle (`LFO`), Velocity and Mod Wheel sources at fs / 32 and produces one modulation offset per
   interval.
2. **Append-only engine edits.** `ProfundumEngine` (`systems/profundum_engine.h`) gains the modulation setters and the per-interval
   composition (C-2). `ProfundumGridRecord` (`systems/profundum_engine_observer.h:52-60`) gains appended tap fields. The test
   driver `dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h` gains appended script fields so the existing preset and
   param-path harnesses can render a modulated preset faithfully (C-17). No other `dsp/` file changes (C-1).
3. **Plugin surface.** Nine host parameters in 1003–1011 (C-12), state format version 3 grown by one 36-byte block (C-10), the
   processor's Mod Wheel routing (C-7), Compare A / B bypass (C-8), the controller mirror, and the editor on the Main and Advanced
   pages (C-13), and the live resolved-position indicator on the Main-page Morph slider (C-18: new view, one float appended to
   `ProfundumSpectrumFrame`, FR-035–FR-037).
4. **The factory library.** Four new coverage columns, one per non-Off source, each the primary of at least one preset. Every
   Phase 13 gate stays green afterwards (C-14, C-15).
5. **Documentation.** Tooltips, `MANUAL.md` and the open `[1.0.0]` CHANGELOG entry.

## Non-goals

- **Motion → Morph.** Motion's configuration is per-endpoint and is itself morphed, so routing Motion to Morph would form a
  feedback loop (roadmap line 1086–1088). The `MotionConfig::Target` enum is unchanged.
- **Per-voice morph.** Morph stays global: one modulated value drives every voice (line 1087–1088).
- **No other new modulation features.** There is no new LFO waveform set (line 1094: "reuse, no new waveform set") and no Cycle
  phase, retrigger, fade-in or polarity control (C-5). The envelope has no release stage (line 1091: "Attack - Decay - Sustain").
  There is no modulation matrix (roadmap line 894: "No general modulation matrix in 1.0"), and channel pressure gets no Morph
  route.
- **No state-format version bump** (line 1098–1100), and no reader for a v3 stream without the new block (line 1099).
- **No change to `IMidiMapping`** (line 1096–1097). The mod wheel still arrives on hidden parameter 6 (`plugin_ids.h:123`).
- **No change to `BassMorphEngine`, `ProfundumMotion`, `LFO`, `ADSREnvelope`** or any other existing `dsp/include/` header besides
  the two named in Scope item 2 (the only other `dsp/` edits are the append-only test-driver change of C-17 and the test-side
  additions of the C-1 list as amended by R5 / plan P-8).
- **No change to the existing morph laws, ramp bounds or Anchor curves** (Phase 8, roadmap lines 575–609).

---

## Existing components (verified this session)

| Component | Header | What is reused (real signature) |
|---|---|---|
| `BassMorphEngine` | `dsp/include/krate/dsp/systems/bass_morph_engine.h` | `void setPosition(float p) noexcept` (`:140-147`): a non-finite value keeps the previous target, `p < kDenormalThreshold` becomes +0 and `p >= 1` becomes 1. `void setPositionRampMs(float ms) noexcept` (`:155-163`), bounded by `kMinPositionRampMs = 5.0f`, `kMaxPositionRampMs = 2000.0f` and `kDefaultPositionRampMs = 40.0f` (`:67-69`). `const Resolved& advanceInterval() noexcept` (`:205-225`) steps the position slew once per 32-sample interval (`kControlInterval = 32`, `:62`). `serializeRuntime` / `restoreRuntime` (`:235-335`) write and read the 20-byte blob: version, **target**, ramped p, ramped p_a, mode, ramp, masks (`:339-347`). `float targetPosition() const` (`:150`). The per-interval step is `Δp_max = 32 000 / (rampMs · fs)` (`:369-373`). |
| `ProfundumEngine` | `systems/profundum_engine.h` | `void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept` (`:223-235`) queues the note, drops it if the queue is full and treats velocity 0 as note-off. `applyNoteOn` (`:793-836`) handles MONO through `MonoHandler::noteOn` and POLY through `VoiceAllocator::noteOn`. `void setMorphPosition(float p) noexcept { morph_.setPosition(p); }` (`:270`). `void setTempoBpm(float)` (`:258-262`) feeds `motion_.setTempoBpm` (`:768-771`). The grid step (`:889-936`, order fixed by `:22-25` "never reordered"): 1 events → 2 `morph_.advanceInterval()` (`:905`) → 3 macros → 4 Motion → …. The pending / applied `Settings` pattern is at `:453-467`. `reset()` keeps the pending setters (`:199-208`). |
| `ProfundumEngineObserver` records | `systems/profundum_engine_observer.h` | `ProfundumGridRecord` (`:52-60`) is the per-interval tap. `ProfundumCall` (`:44-48`) is "normative: SC-032 compares call sequences" (`:43`), so this phase adds no call enumerator (C-2). |
| `ProfundumMotion` (pattern only) | `systems/profundum_motion.h` | Pattern for an fs / 32 source bank: `prepare` clocks `ADSREnvelope` and `LFO` at `sampleRate / 32` (`:141-151`), `setConfig` calls component setters change-only (`:172-230`), `setTempoBpm` (`:233-239`), `noteRestart()` (`:243-246`), `advanceInterval()` advances every source whichever is selected (`:249-279`, C-13 there), and `class Output final : public ModulationSource` (`:126-134`). It is not composed or edited here. |
| `LFO` | `dsp/include/krate/dsp/primitives/lfo.h` | `enum class Waveform : uint8_t { Sine, Triangle, Sawtooth, Square, SampleHold, SmoothRandom }` (`:47-54`), `kMinFrequency = 0.01f`, `kMaxFrequency = 20.0f` (`:34-35`), `void prepare(double sampleRate)` (`:83-92`), `void reset()` with the fixed random seed `randomState_ = 12345u` (`:95-108`), `float process()` (`:114`), `setWaveform` (`:189`), `setFrequency` (`:218`), `setTempoSync(bool)` (`:237`), `setTempo(float bpm)` (`:246`), `setNoteValue(NoteValue, NoteModifier)` (`:255`). The synced rate is clamped to the same range (`:498`). |
| `ADSREnvelope` | `primitives/adsr_envelope.h` | `void prepare(float sampleRate)` (`:68-73`), `void reset()` (output 0, Idle, `:75-80`), `void gate(bool on)` (`:86-109`). In Hard mode `gate(true)` enters Attack from the current output (`enterAttack`, `:451-456`). `setAttack` / `setDecay` / `setSustain` (`:115-130`): times are clamped to `[kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs] = [0.1, 10000]` ms (`envelope_utils.h:51-52`) and sustain to [0, 1]. `float process()` (`:232-253`). Default curves are Exponential (`:526-527`). |
| `ModulationSource` | `core/modulation_source.h` | `virtual float getCurrentValue() const noexcept = 0;` and `virtual std::pair<float, float> getSourceRange() const noexcept = 0;` (`:31-42`). |
| Note values | `core/note_value.h` | `kNoteValueDropdownCount = 30` (`:123`), `kNoteValueDropdownMapping[]` (`:136`); index 13 = `{Quarter, None}` (`:155`), which is Motion's default (`morph_state.h:92-93`). |
| Motion parameter rows (labels reused) | `plugins/profundum/src/parameters/motion_params.h` | `kMotionCycleShapeLabels` (Sine, Triangle, Sawtooth, Square, S&H, Smooth Random), `kMotionCycleSyncLabels` (`:41-43`), and the 30-entry Sync Rate row `kNoteValueDropdownStrings` (`:63-65`). |
| Morph control pack | `parameters/morph_params.h` | `MorphControlParams` (`:190-195`). `sanitizeMorphPosition` (`:201-212`). Registration `registerMorphControlParams` (`:246-270`): 105 and 1002 continuous `kCanAutomate`; 1000 a list (`kCanAutomate \| kIsList`) with its registered default pinned by hand; 1001 `kIsList` only. The 16-byte stream and its EOF-safe, keep-prior load rules (`:301-345`). Controller loader (`:352-386`). |
| Endpoint mirror (controller) | `plugins/profundum/src/controller/controller.cpp`, `parameters/sound_param_table.h` | What an endpoint is: the 60-row `kSoundRows` table (`sound_param_table.h:63`) routed as `RouteKind::Sound` (`:94`). `redisplayEditTarget` re-displays exactly those rows from `leafBitsOf(mirror_[editTarget])` (`controller.cpp:466-467`). `copyEndpoint` (`:394`) sends the copy request. `kMorphStateBytes = 201` with `static_assert(kMorphStateBytes == 201)` "visible edit on any change" (`morph_state.h:135`, `:442`). |
| Preset / param-path render harnesses | `dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h`, `plugins/profundum/tests/preset_render_support.h`, `param_path_audibility_helpers.h` | `struct VoiceEngineScript` (`:139-155`) carries a static `float position` and no modulation field; `voiceEngineRender` calls `e->setMorphPosition(s.position)` (`:377-400`); `voiceEngineTwinScript` copies the script and sets `forceLinearCore` (`:438-444`). `effectiveScriptFor` (`preset_render_support.h:144-160`) overlays macros, voice mode, polyphony, seed, note fields and 1000 / 1002 only (`:12-15`); the D_nl twin and the fidelity arm are engine-direct renders of that script (`:508-544`). `ppRenderProcessor` (`param_path_audibility_helpers.h:577`) builds its twin with `t.position = fx->engine()->morphPosition()` (`:631`), which is `morph_.targetPosition()` (`profundum_engine.h:350`). `decodePresetState` (`preset_test_support.h:115`). |
| Performance pack | `parameters/performance_params.h` | `PerformanceParams::modWheel` / `channelPressure` (`:38-39`). IDs 6 and 7 are `kIsHidden` only (`:75-80`). `resetPerformanceParams` on `setState` (`:109-115`). |
| Processor state | `plugins/profundum/src/processor/processor.cpp`, `plugin_ids.h` | `kCurrentStateVersion = 3` (`plugin_ids.h:23`); v3 = 574 bytes (`:54-63`). `setState` (`processor.cpp:600-660`): future versions are refused, v1 resets every Phase 11 field, v2 / v3 keep prior values after a cut, and v3 reads the names. `getState` (`:665-700`) writes the runtime blob from precedence 1 → 2 → 3 (`:681-687`). The preset-load message accepts up to 4096 bytes (`:747`). The route table covers IDs `0..1002` (`plugin_ids.h:223-226`). Velocity is quantised to [1, 127] (`processor.cpp:82-84`). |
| Editor | `plugins/profundum/resources/editor.uidesc` | Main page `morph-band` (`:173-188`, 636 × 204: endpoint names, the Morph slider, Compare, Copy, Edit). Advanced **Modulation** tab (`:348-382`): the Motion block uses the top ~185 px of a 516 px tab. Every surface ID must be bound (`Profundum_UidescBindsEverySurfaceId`, `tests/unit/controller/editor_layout_test.cpp:495`). |
| Generator definitions | `tools/profundum_preset_defs.h` | `enum class Capability` (`:64-69`, 50 AT + 3 reference columns). `ProfundumPresetDef` (`:145-158`), whose `controls` list allows "1, 100-103, 105, 410-416, 1000, 1002" (`:153`). |
| Fingerprints | `tests/test_helpers/render_fingerprint.h` | `fingerprintRender(std::span<const float>)` (`:73`), `compareFingerprints(actual, reference, metricTol, sampleTol)` (`:122`), `kSampleTolerance = 5.0e-4f`, `kMetricTolerance = 2.5e-4` (`:58-61`). |

## New components

| Class | Layer | Header | ODR sweep (`grep -rEn "(class\|struct\|enum class\|using) <Name>\b" dsp/ plugins/ tools/ tests/`, 2026-10-26) |
|---|---|---|---|
| `ProfundumMorphModulator` (with nested `Config`, `Source`, `Output`) | 3 (systems; includes L0 `db_utils.h`, `modulation_source.h`, `note_value.h` and L1 `adsr_envelope.h`, `envelope_utils.h`, `lfo.h` only — downward) | `dsp/include/krate/dsp/systems/profundum_morph_modulator.h` | **0 hits** for `ProfundumMorphModulator`, `MorphModulator`, `MorphModulation`, `MorphModSource`, `MorphModConfig`. |
| `MorphModParams` (plugin atomics struct) | plugin | `plugins/profundum/src/parameters/morph_mod_params.h` (or appended to `morph_params.h`, the plan decides) | **0 hits** for `MorphModParams`, `MorphModulationParams`. |

The roadmap's Phase 14 text proposes no class name. The roadmap's hazard names (`CharacterProcessor`, `SubOscillator`,
`StereoField`, `MorphEngine`, `MorphNode`, `TransientDetector`, `HarmonicCloud`, `SpectralState`, `MacroMapper`; line 174–176)
are not used.

**Layer note.** `ProfundumMorphModulator` needs only L0 and L1, so it could live in L2. It is placed in `systems/` beside its only
consumer and the `profundum_*` family, as `ProfundumMotion` is (`profundum_motion.h:1-2`). Its includes point downward only.

---

## Decisions

### C-1: The modulator lives in the engine, and the `dsp/` scope is fenced to three files

A processor-side modulator would update the morph target once per host block. That cannot satisfy two requirements:

- the envelope restart must land on the note's grid point. The engine applies a note "at the first grid point at or after it"
  (`profundum_engine.h:17-20`);
- Phase 10's block-size invariance (SC-009 `PartitionInvariance`, `tests/unit/midi_event_test.cpp:211`, host blocks
  {1, 7, 32, 65, 4096} against a 512 reference, `:10`, `:240-241`) must keep holding.

The modulator is therefore an engine member that advances in the grid step. The user confirmed (2026-10-26) that this phase edits
`dsp/` from the start: the build stage MUST NOT stop at a `dsp/` scope fence for the three files named below (FR-019). The
`dsp/` change set is exactly:

- the new header `systems/profundum_morph_modulator.h`;
- append-only edits to `profundum_engine.h` (including a read-only accessor for the resolved, modulated morph position, C-18)
  and `profundum_engine_observer.h`;
- append-only edits to the test driver `dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h` (C-17);
- new unit tests under `dsp/tests/unit/systems/`;
- (R5 / plan P-8, user 2026-10-26) the registration lines for the new TUs in `dsp/tests/CMakeLists.txt`, and appended
  TEST_CASEs (or, for `Profundum_EngineDisplayAccessorNeutral`, an appended SECTION) in the existing Profundum test TUs
  `profundum_engine_perf_test.cpp`, `profundum_engine_display_test.cpp` and `profundum_voice_engine_helpers_test.cpp`; no
  existing case in them changes.

Both edited production headers are Profundum-only. They are not in `mass_dynamics.h`'s include closure, so the Phase 7 mono-sum grid re-run
rule (roadmap line 685–687) is not triggered.

### C-2: The composition point and the exact-transparency branch

The grid step gains a sub-step **1b** between step 1 (events) and step 2 (`morph_.advanceInterval()`, `profundum_engine.h:905`).
Steps 1 to 8 keep their order. Sub-step 1b runs as follows:

1. Apply any latched modulation settings, change-only, as step 1 does for `Settings`.
2. Advance the modulator by exactly one interval. Every source advances whichever is selected (the `ProfundumMotion` C-13
   pattern, `profundum_motion.h:250-255`).
3. Compute the morph target. **If** Source is Off **or** Amount is exactly ±0 **or** the bypass flag is set (C-8), the target is
   the stored base position `b`, reached by a **branch** and never by arithmetic. **Otherwise** it is
   `clamp(b + Amount · s, 0, 1)`.
4. Call `morph_.setPosition(target)`.

Here `b` is the last value given to `setMorphPosition`, and `s` is the selected source value (C-3).
`setMorphPosition` MUST store the sanitised base `b` and forward it; the per-interval target replaces it before every
`advanceInterval`, and `prepare` / `reset` re-hand `b` immediately before the morph snap (plan P-1; R1, user 2026-10-26).

The branch in step 3 follows the root `CLAUDE.md` fast-math rule: "a value that must be exact at a neutral point … by a branch or
a clamp, not by arithmetic". `ProfundumCall` gains no enumerator, because SC-032 compares call sequences
(`profundum_engine_observer.h:43`). `ProfundumGridRecord` gains three appended fields: `morphModSource` (s), `morphModOffset`
(Amount · s, clamped to [−1, 1]) and `morphTarget` (the value handed to `setPosition`).

### C-3: Source values

| Source | `s` | Range |
|---|---|---|
| Off | 0 (unused, see C-2) | — |
| Envelope | the ADSR output | [0, 1] |
| Cycle | the LFO output, bipolar as `LFO::process` returns it | [−1, 1] |
| Velocity | `v / 127` of the latest accepted note-on (C-6) | [1/127, 1]; 0 before the first note |
| Mod Wheel | the engine's latched wheel value (C-7) | [0, 1] |

Cycle is bipolar: it oscillates around wherever the slider sits, and the result clamps at 0 and 1, which is the ruled formula
(line 1089). The other sources are one-sided, so Amount's sign alone chooses toward B or back toward A (line 1089–1090).

### C-4: Envelope — A·D·S, restart from 0 on every accepted note-on, no gate-off

- `ADSREnvelope` runs at fs / 32 in Hard retrigger mode with its default curves. Attack and Decay span [0.1, 10 000] ms
  (logarithmic law, the `motion.envAttackMs` / `envDecayMs` ranges, `morph_state.h:282-285`). Sustain spans [0, 1].
- **Restart:** every note-on the engine *accepts* restarts the envelope **from 0**, in MONO and in POLY (line 1091–1093).
  "Accepts" means a velocity > 0 that is not dropped by a full queue and is applied in step 1. Every such note-on counts, including
  a MONO note-on that does not change the sounding pitch under Low / High priority, a legato overlap and a latched-key re-strike.
  The restart is `reset()` followed by `gate(true)`. Several note-ons at one grid point give one restart.

  The restart is from 0 because Hard mode's `enterAttack` starts from the current output (`adsr_envelope.h:451-456`). With
  sustain 1 a second note would otherwise never re-rise, which contradicts "restarts on every new note". The step from the
  current value down to 0 is absorbed by the Morph Ramp Time slew (C-9).
- **Where the restart is driven:** the engine calls the modulator's `noteOn` at the **top of `applyNoteOn`**
  (`profundum_engine.h:793`), before the MONO branch's `MonoHandler::noteOn` and its `if (!mev.isNoteOn) return;` early return
  (`:798-800`), so a note-on that MonoHandler does not sound still restarts. It does **not** use the per-voice
  `consumeEnvelopeRestart()` → `motion_.noteRestart()` path that Motion uses (`:918-925`): that path follows voice events, not
  accepted note-ons.
- **The restart observable.** `advanceInterval` steps every source and then returns the stepped value (FR-001), so the interval
  that applies a restart already holds one Attack step from 0: `output = attackBase + 0 · attackCoef` (`adsr_envelope.h:303`),
  `attackBase = (1 + 0.3)(1 − coef)` (`envelope_utils.h:54`, `:99-102`). That is ≈ 0.121 / 0.131 / 0.062 for a 10 ms attack at
  48 / 44.1 / 96 kHz, and 1.0 for a 0.1 ms attack (rate clamped to 1 interval). A restart is therefore observed as **s at the
  restart grid point equals the first `process()` of a freshly `reset()` + `gate(true)` reference `ADSREnvelope` with the same
  settings, within 1e-6**, never as "s ≈ 0".
- **Stage times, as `ADSREnvelope` defines them.** Attack is the time from 0 to the peak (the one-pole crosses 1 after
  `rate = A · fs/32 / 1000` steps). Decay is the time of the **full 1 → 0 ramp** ("Decay time = full 1.0->0.0 ramp",
  `adsr_envelope.h:417`), not the time to reach Sustain: with Sustain S > 0 the Decay stage ends earlier, when the output crosses S,
  and the output is then set to S exactly (`:341-344`).
- **Sustain smoothing.** In the Sustain stage the output follows a one-pole toward `sustainLevel` with
  `sustainSmoothCoef_ = exp(−5000 / (kSustainSmoothTimeMs · fs/32))` (`adsr_envelope.h:72`, `:242-245`; `kSustainSmoothTimeMs =
  5`, `envelope_utils.h:53`), a 1 ms time constant at any rate. It matters only when Sustain changes during the Sustain stage
  (entry sets the output to S exactly). Computed from the formula at fs / 32, a unit step settles to within 1e-3 in 11 / 10 / 21
  intervals (7.33 / 7.26 / 7.00 ms) at 48 / 44.1 / 96 kHz; the plan's probe confirms these counts before SC-002 (d) is written.
- **No release:** the envelope is never gated off. It rests at Sustain after every note-off, until the next note-on. This covers
  the ruled gestures "rise from A into B and stay" (attack T, sustain 1) and "hit B and settle back" (attack 0, decay T,
  sustain 0) (line 1091–1092).
- Before the first note after `prepare` / `reset` the envelope is Idle and `s = 0`.

### C-5: Cycle — the Motion CYCLE shapes and tempo sync, free-running

- The control set is Shape (the six `Waveform`s), Rate [0.01, 20] Hz (logarithmic, `lfo.h:34-35`), Sync (Off / On) and Sync Rate
  (the 30-entry note list). It runs at fs / 32 and follows the host tempo the engine already forwards (`profundum_engine.h:768-771`).
- The LFO is **free-running**: no note retriggers it, its phase offset is 0 and its fade-in is 0. The ruling names the shapes and
  the tempo sync only (line 1094), so no Phase, Retrigger or Fade-In control is added.
- **This differs from Motion's CYCLE default.** Motion's CYCLE retriggers on note-on by default (`MotionConfig::cycleRetrigger =
  true`, `morph_state.h:95`; `noteRestart()` calls `lfo_.retrigger()`, `profundum_motion.h:243-246`). The Morph Cycle deliberately
  does not: it is a global gesture shared by every voice, so a per-note re-phase would jump the morph position of the held notes
  on every new note. This is put to the user (Review note 3).
- Determinism: `LFO::reset` seeds its random shapes with the constant `12345u` (`lfo.h:97-98`), so S&H and Smooth Random are
  identical on every run and independent of the engine seed.

### C-6: Velocity — the latest accepted note-on

`s = velocity / 127` from the latest accepted note-on (the C-4 definition), set in sub-step 1b of the interval that applies it. It
is held through note-offs until the next accepted note-on (line 1095). It is 0 after `prepare` / `reset`. The mapping is linear.
The processor quantises host velocity to [1, 127] (`processor.cpp:82-84`), so the smallest value after a note is 1/127.

### C-7: Mod Wheel — the processor decides the Mass route, the engine owns the Morph source

- The engine gains `setMorphModWheel(float)`. A non-finite value keeps the previous one, and the value is clamped to [0, 1] and
  latched to the next grid point like every other setter. The processor forwards Mod Wheel (6) to it every block.
- `Processor::applyEndpoints` (`processor.cpp:927-950`) changes in **one** term (user, 2026-10-26: the hand-over needs a non-zero Amount):
  - when Source = Mod Wheel **and Amount is not 0** (`!= 0.0f`, so -0 counts as 0), `c = clamp(Channel Pressure, 0, 1)`;
  - otherwise `c = clamp(Mod Wheel + Channel Pressure, 0, 1)`, exactly as today.

  So with Source = Mod Wheel and Amount != 0 the wheel drives Morph only and stops pushing Mass (line 1096–1097). With Amount 0
  (the default) the wheel keeps pushing Mass even when Mod Wheel is selected, so the wheel is never dead. Channel pressure keeps
  pushing Mass in every source.
- A Source change, or an Amount change between 0 and non-zero, while the wheel is non-zero changes `c`. That marks both endpoints
  dirty and re-sends them through the existing path, and `MassLaw`'s slew smooths the Mass step, as for any wheel move today.

### C-8: Compare A / B bypasses the modulation

`pushParamsToEngine` already pins the position to 0 or 1 under Compare A / B (`processor.cpp:973-978`). Under A / B it also sets
the engine's modulation bypass, so the user hears the pure endpoint. Under A·B the modulation runs as registered. The sources keep
advancing under the bypass (C-2 step 2), so returning to A·B continues the gesture rather than restarting it (SC-012 (d)). The
roadmap does not ask for this exception; it is put to the user (Review note 8).

### C-9: Morph Ramp Time is the only smoother

The modulated target passes through the existing `SlewLimiter` at `Δp_max = 32 000 / (rampMs · fs)` per interval
(`bass_morph_engine.h:369-373`): "through the existing Morph Ramp Time smoothing contract" (line 1090).

This has a documented consequence. The resolved position moves at most `1 / rampMs` per millisecond. A Cycle faster than the
ramp allows becomes a slew-limited triangle. Square, S&H and Sawtooth resets, a Velocity change and an envelope restart become
ramps that last `rampMs`. The manual states this, and the tooltips for Cycle Rate and Morph Ramp Time point to each other.

### C-10: State — version 3, grown by one 36-byte block

The block is appended after byte 574 (the v3 names), giving **v3 = 610 bytes**. `kCurrentStateVersion` stays 3 (line 1098–1100).
Each field is little-endian, 4 bytes, in this order:

| Offset | Field | Type |
|---|---|---|
| 574 | Source | i32, 0–4 |
| 578 | Amount | f32 |
| 582 | Env Attack ms | f32 |
| 586 | Env Decay ms | f32 |
| 590 | Env Sustain | f32 |
| 594 | Cycle Shape | i32, 0–5 |
| 598 | Cycle Rate Hz | f32 |
| 602 | Cycle Sync | i32, 0–1 |
| 606 | Cycle Sync Rate | i32, dropdown index 0–29 |

Load rules (the Phase 11 FR-044 / FR-045 rules, `morph_params.h:301-345`):

- A **v1 or v2** stream predates the block. Every modulation field takes its default, as v1 / v2 already do for the endpoint names
  (`processor.cpp:615-626`, `:633-634`).
- A **v3** stream cut anywhere inside the block keeps the prior value of every field from the cut on (user, 2026-10-26: the format stays v3, 574 to 610 bytes). There is **no special
  reader** for a 574-byte v3 stream: the ruling says there is none to keep (line 1099). The regenerated factory library has no
  574-byte files (FR-061).
- A non-finite float is not stored. A finite float is clamped to its range. An out-of-range enum or index is not stored.
- The controller mirrors these rules: a value it does not store makes no `setParam` call.

The 4096-byte preset-load message cap (`processor.cpp:747`) still holds.

### C-11: The runtime blob's target is the base position

`getState` writes the base position `b` in the runtime blob's **target** field (bytes 491–494). The ramped `p` and `p_a` are the
engine's actual values. A load therefore never installs a modulated target as the user's position. On restore, the modulator
restarts from its reset state (C-4, C-6) and the next interval recomputes the target. This keeps Phase 13 FR-047 ("target and
ramped position both equal the stored Morph Position") satisfiable for every factory preset. The plan chooses the mechanism
(an engine accessor for `b`, or a byte patch in `ProfundumEngine::serializeMorphRuntime`). `BassMorphEngine` is not edited (C-1).

**Ruled (R2, R5 / P-2, user 2026-10-26):** `restoreMorphRuntime` sets `b` to the restored target and resets the modulator on every
successful restore, also mid-stream (plan P-2, P-3); `serializeMorphRuntime` patches bytes 1–4 with `b`. A preset or state loaded
under a held note therefore restarts the envelope, zeroes Velocity and re-phases the Cycle; the Phase 13 FR-048 held-load click
bound covers it.

### C-12: Parameters (1003–1011)

| ID | Name `k…Id` | Title | Kind / range / law | Default | Flags |
|---|---|---|---|---|---|
| 1003 | `kMorphModSourceId` | Morph Mod Source | list: Off, Envelope, Cycle, Velocity, Mod Wheel | Off | `kCanAutomate \| kIsList` |
| 1004 | `kMorphModAmountId` | Morph Mod Amount | [−1, 1] linear, bipolar % display | 0 (centre, as Motion Amount, `morph_state.h:87`) | `kCanAutomate` |
| 1005 | `kMorphModAttackId` | Morph Env Attack | [0.1, 10 000] ms log | 10 ms (Motion ENV default, `morph_state.h:99`) | `kCanAutomate` |
| 1006 | `kMorphModDecayId` | Morph Env Decay | [0.1, 10 000] ms log | 300 ms (`morph_state.h:100`) | `kCanAutomate` |
| 1007 | `kMorphModSustainId` | Morph Env Sustain | [0, 1] linear, % | 0 | `kCanAutomate` |
| 1008 | `kMorphModCycleShapeId` | Morph Cycle Shape | list: the six `kMotionCycleShapeLabels` | Sine | `kCanAutomate \| kIsList` |
| 1009 | `kMorphModCycleRateId` | Morph Cycle Rate | [0.01, 20] Hz log | 1 Hz (`morph_state.h:89`) | `kCanAutomate` |
| 1010 | `kMorphModCycleSyncId` | Morph Cycle Sync | list: Off, On | Off | `kCanAutomate \| kIsList` |
| 1011 | `kMorphModCycleSyncRateId` | Morph Cycle Sync Rate | list: the 30 `kNoteValueDropdownStrings` | index 13 (1/4) | `kCanAutomate \| kIsList` |

- All nine IDs are in the Morph control band after 1002 (line 1101). "A list stays a list" (line 1102): lists are registered
  `kCanAutomate | kIsList`, as 1000 is (`morph_params.h:246-270`).
- None of the nine is an endpoint (sound) parameter (FR-018). They are not `kSoundRows` rows (`sound_param_table.h:63`), so Edit
  Target does not re-display them, Copy A↔B does not copy them, and `MorphState` / `MotionConfig` / `kMorphStateBytes = 201` are
  unchanged.
- `kNumRoutedIds` becomes 1012. The registered count becomes 93 (84 + 9; the CHANGELOG says 84 today). The hard-coded 84
  assertions that move to 93 are: `editor_layout_test.cpp:498`, `:1449`, `:1501`; `param_table_test.cpp:76`, `:383`, `:384`,
  `:422`; `param_wiring_test.cpp:416`; `editor_lifecycle_test.cpp:224`; `editor_sync_test.cpp:169`;
  `manual_coverage_test.cpp:82`; `param_denorm_test.cpp:257`; `phase11_alloc_test.cpp:182` (and its routed-ID list, `:64`);
  `preset/preset_library_test.cpp:77` (all under `plugins/profundum/tests/`). The explicit SC-005 row table
  (`param_table_test.cpp:218`) gains the nine C-12 rows.
- **Further hard-coded sites this phase moves (R5: plan P-4 and tasks Deviation 7, user 2026-10-26).** The lists are a starting
  point; the build task re-greps `84`, `80u`, `288` and `574` under `plugins/profundum/` before editing, and the re-grep, not any
  list, is the authority:
  - P-4: the visible-ID count 80 → **89** in `tests/unit/controller/editor_layout_test.cpp` `:517`, `:518`, `:535`, `:656`;
    `src/processor/processor.cpp:355` `static_assert(kBlockSize == 288u, …)` → 320 (FR-036); `src/plugin_ids.h:61-63`
    `kStateV3Bytes … == 574` → 610; the literal `574`s in `tests/unit/preset/preset_library_test.cpp:243`, `:1262-1263` (and the
    `:1237` comment) and `tests/integration/preset_browser_test.cpp:467` → 610;
  - tasks Deviation 7: `tests/unit/controller/editor_lifecycle_test.cpp:224-226` (84 / 80u / 80u → 93 / 89u / 89u),
    `tests/unit/phase11_alloc_test.cpp:175` (`84u` → 93u), `tests/unit/state_v3_test.cpp:396`, `:401` (`574u` → 610u),
    `tests/integration/spectrum_frame_test.cpp:95`, `:107` (`288u` → 320u), `tests/unit/spectrum_producer_test.cpp:49`
    (`288` → 320), `src/parameters/sound_param_table.h:191` (`routedCount() == 84` → 93), and the comments
    `src/controller/controller.cpp:9`, `:96`, `src/controller/controller.h:13`, `src/parameters/sound_param_table.h:12`,
    `src/processor/processor.cpp:663`, `:747`, `tests/preset_test_support.h:7`, `tests/profundum_test_fixture.h:1029`.
- The Phase 11 SC-005 fixed strings (Hold / Blend / Yield, Harmonic Fold, Wavefold) are untouched.

### C-13: Editor placement (the roadmap defers this to the spec, line 1105–1106)

- **Main page, `morph-band`:**
  - add **Morph Mod Source** (a 5-entry list control) and **Morph Mod Amount** (an `ArcKnob` with the bipolar display) in the
    free right part of the Compare / Edit rows (`editor.uidesc:183-188` occupy x 8–448);
  - that is the "small number of powerful controls" rule (roadmap line 843): the per-note gesture is reachable without the
    Advanced page. The Main page carries Source and Amount **only** (user, 2026-10-26); no time or Cycle control is on it (FR-030);
  - the Main-page Morph slider also shows the live resolved-position indicator of C-18.
- **Advanced → Modulation tab:**
  - add a **MORPH MODULATION** block below the Motion block (y ≥ 190 of the 516 px tab, `editor.uidesc:348-382`);
  - it holds all nine controls, Source and Amount included. That binds every new ID on the Advanced page as well and satisfies
    `Profundum_UidescBindsEverySurfaceId`.
- **Uidesc edits** are made with the repo's XSLT transform workflow, not by hand. Every new control has a tooltip. The editor
  baseline (exactly one `UpdateBanner`, `CheckForUpdatesButton` and `VersionLabel`) is unchanged.

### C-14: Factory library — four new columns, primary rule, transparency of everything else

- `Capability` (`tools/profundum_preset_defs.h:64-69`) gains four columns in a new group `MorphMod`: **M-ENV, M-CYCLE, M-VEL,
  M-WHEEL**.
- The Phase 13 primary rule applies to each new column: "each column is the primary of at least one preset", and each preset
  has exactly one primary (Phase 13 C-2.2, FR-011).
- **Convert first (user, 2026-10-26).** Existing factory presets are converted to the four new source columns wherever the hidden
  probe (FR-044) shows the source audible at the C-15 bar. A **new** preset is added ONLY for a column that no existing preset can
  carry. The library stays near its current 58 presets. A conversion moves the preset's primary to the new column and is allowed only if every column
  that preset was primary for keeps at least one other primary (the coverage matrix keeps every pre-existing column covered,
  FR-046). The plan derives the list from the probe (FR-044), never from a quota.
- A converted preset changes sound by design. It is excluded from the pre-phase fingerprint comparison as stored, and is
  compared only forced to Source Off (FR-060 (b), SC-008). Every converted preset still passes every Phase 13 gate (FR-043).
- `ProfundumPresetDef::controls` may carry IDs 1003–1011.
- A preset that stores Source ≠ Off may not claim a single-state column (Phase 13 FR-007's list), because the modulation moves
  its morph position off 0.
- Every preset that stores Source = Off keeps its decoded `Comp` fields and its renders (FR-062, FR-063).

### C-15: The showcase arm of a source column

- **Descriptor and bar:** the Phase 9 AT-41 / AT-42 Morph row descriptor, D_nl (dB) on the Char / twin tap, with bar ≥ 3 dB
  (`specs/profundum-phase9-voice-engine/evidence/audibility-table.csv`, rows AT-41 / AT-42). It is read through the moved Phase 9
  readers (Phase 13 FR-057).
- **Window:** D_nl is a static descriptor. Each arm therefore reads it over a window where the resolved morph position (grid
  record `resolved->position`) varies by ≤ 0.01. The plan places the window from the recorded trajectory.
- **Twin:** the linear-core twin and the fidelity arm carry the same modulation as the processor render (C-17), so the twin's
  resolved position follows the processor's instead of sitting at the static base position.
- **The arms:**

| Column | lo arm | hi arm |
|---|---|---|
| M-ENV | the preset with Source forced Off | as stored, in a held span of the envelope (sustain, or a decay long enough to hold the window) |
| M-CYCLE | Source forced Off | as stored, in a held extreme (Square, S&H, or a rate slow enough for the window) |
| M-VEL | as stored, velocity 1 | as stored, velocity 127 |
| M-WHEEL | as stored, wheel 0 | as stored, wheel 1 |

- Every arm runs at E1 and C3, the Phase 13 notes.
- A primary arm carries no precondition override (Phase 13 FR-012). A source whose arm cannot clear the bar from the shipped
  surface stops the build and is surfaced with its measured value (the Phase 13 FR-023 rule).

### C-16: Lanes

- **Scope:** the change set is the new header, `profundum_engine.h`, `profundum_engine_observer.h`, the test driver
  `profundum_voice_engine_test_helpers.h` (C-17; its consumers are Profundum TUs of `dsp_systems_tests` and `profundum_tests`,
  both already in the lanes below), the plugin and the generator.
  The phase's lanes are:
  - the `[morphmod]` cases;
  - every Profundum TU of `dsp_systems_tests` (the consumers of the two modified engine headers, by the root `CLAUDE.md`
    change-set rule);
  - the full `profundum_tests` per-push lane;
  - the Phase 13 `[long]` preset cases (the library changes);
  - the CPU lane, alone (the root `CLAUDE.md` timing-sensitive rule).
- **Not re-run:** untouched `[long]` cases.
- **Phase 9 sentinels:** SC-032 call sequence, the determinism harness and the audibility rows that include `profundum_engine.h`
  are in scope because that header is modified.

### C-18: Live resolved-position indicator on the Main-page Morph slider

The editor shows a **display-only** indicator of the resolved (modulated, ramped) morph position on the Main-page Morph slider,
following the `OffsetRingKnob` precedent (`ui/offset_ring_knob.h`: a ring drawn beside a control that never moves the control).
The slider's own value stays the user's base position; the indicator never moves it and never writes a parameter.

- **Data path:** one float, the resolved morph position, is appended to `ProfundumSpectrumFrame`
  (`processor/spectrum_frame.h:32`) after `partialPower`, over the existing DataExchange queue (no new queue, no new message, same
  user context `'PRFD'`). The frame grows from 288 to 320 bytes: the float sits at offset 288 and the remaining 28 bytes are
  explicit padding, so `sizeof % 32 == 0` holds (the existing `static_assert`s are updated, `<= 320` still holds).
  The processor's DataExchange block size keeps the aligned-alloc rounding (`processor.cpp:349-356`: rounded up to the alignment),
  so macOS `aligned_alloc` never returns null.
- **Source of the value:** the engine's resolved morph position (the ramped position after `morph_.advanceInterval()`), read through
  the append-only accessor of C-1, sampled when the frame is produced. With Source Off it equals the base position.
- **Cadence (R3, user 2026-10-26):** the indicator updates at the frame cadence (each completed window while a note sounds, every
  0.25 s in silence, `spectrum_producer.h:47`); the manual says so. There is no morph-only republish, so the Phase 12 frame-count
  contract (`Profundum_SpectrumFrameCadence`) is unchanged (plan P-5).
- **View:** a new view class (ODR-swept in the plan) registered like `OffsetRingKnob`, bound in `editor.uidesc` to the Main-page
  Morph slider. Cached on the UI thread from the newest frame with or without an open editor; before the first frame, and in a host
  with no DataExchange, it shows the base position.

### C-17: The preset and param-path harnesses carry the modulation

Without this, the Phase 13 gates cannot measure an opted-in preset (FR-043). `renderScriptThroughProcessor` loads the `Comp`, so
the processor runs the modulation, but its D_nl twin and its fidelity arm are engine-direct renders of the effective script
(`preset_render_support.h:508-544`), and `VoiceEngineScript` holds only a static `position` (`profundum_voice_engine_test_helpers.h:139-155`).
For a preset that stores Source ≠ Off, the fidelity arm would compare a modulated render with an unmodulated one, and the D_nl
twin would sit at the base position while the Char tap sits at the modulated one. The Phase 11 processor-path twin has the same
flaw: `t.position = fx->engine()->morphPosition()` (`param_path_audibility_helpers.h:631`) reads `morph_.targetPosition()`
(`profundum_engine.h:350`) at render end, a value the read window need not hold.

The fix, all append-only:

1. `VoiceEngineScript` gains appended fields `ProfundumMorphModulator::Config morphMod{}` (default Source Off),
   `float morphModWheel = 0.0f` and `bool morphModBypass = false`. `voiceEngineRender` hands them to the engine
   (`setMorphModulation`, `setMorphModWheel`, `setMorphModBypass`) next to `setMorphPosition(s.position)` before the first block.
   `voiceEngineTwinScript` copies them with the rest of the script, so a twin follows the same trajectory. With the defaults
   every existing script renders exactly as before (FR-060).
2. `decodePresetState` (`preset_test_support.h:115`) decodes the C-10 block, and `effectiveScriptFor` overlays it on the
   effective script, with the arm's wheel value, in preset context.
3. `ppRenderProcessor` builds its twin with the processor's modulation config, wheel and bypass, so its twin is driven, not read
   back as a static position.
4. The plugin harness files (`preset_render_support.h`, `preset_test_support.h`, `param_path_audibility_helpers.h`) are plugin
   test code and are edited under the same append-only rule.
5. **The driver gains three appended event kinds (R5 / plan P-7, user 2026-10-26).** `VoiceEngineEventKind` gains
   `MorphModulation` (index into an appended `std::vector<ProfundumMorphModulator::Config> morphModStates`), `MorphModWheel`
   (`value`) and `MorphModBypass` (`index`), appended after `Endpoint` (`profundum_voice_engine_test_helpers.h:123-126`), for the
   mid-render wheel, bypass and config changes SC-004, SC-010 (g) and SC-012 (c)/(d) need. Existing scripts never use them.

---

## Functional Requirements

### Modulator component (DSP)

- **FR-001** `ProfundumMorphModulator` (L3, `systems/profundum_morph_modulator.h`) MUST provide:
  - `prepare(double sampleRate)`, which clocks the envelope and the LFO at `sampleRate / 32` and then calls `reset()`;
  - `reset()`: envelope Idle (output 0), LFO reset, Velocity value 0;
  - change-only `setConfig(const Config&)` that sanitises every field (a non-finite float keeps the previous value, ranges clamp,
    an undeclared enum keeps the previous value);
  - `setTempoBpm(float)`: a non-finite or ≤ 0 value keeps the previous tempo;
  - `noteOn(std::uint8_t velocity)`, the C-4 restart plus the C-6 velocity latch;
  - `setModWheel(float)`;
  - `advanceInterval()`, which advances every source once and then returns the selected `s` after that step (so the interval
    that applies a restart returns the first Attack step from 0, the C-4 restart observable);
  - `sourceValue()` and `offsetValue()` accessors.

  [roadmap 1085, 1089; C-2–C-7]
- **FR-002** `Config` MUST hold exactly the C-12 fields: Source, Amount, Env Attack / Decay / Sustain, Cycle Shape / Rate / Sync,
  and the note value + modifier of the Sync Rate. Nothing else. [roadmap 1085, 1091, 1094; non-goals]
- **FR-003** The Envelope source MUST follow C-4: an A·D·S contour on `ADSREnvelope`, restarted from 0 at every accepted note-on,
  never gated off, resting at Sustain. [roadmap 1091–1093]
- **FR-004** The Cycle source MUST follow C-5: the six `Waveform`s, free rate or tempo-synced note value, bipolar output,
  free-running, no new waveform. [roadmap 1094]
- **FR-005** The Velocity source MUST follow C-6: the linear `v / 127` of the latest accepted note-on, held until the next one, 0
  after reset. [roadmap 1095]
- **FR-006** The Mod Wheel source MUST read the latched wheel value in [0, 1]. [roadmap 1096]
- **FR-007** The modulator MUST expose `const ModulationSource& output() const`. Its `getCurrentValue()` is the clamped offset
  `Amount · s` and its `getSourceRange()` is `{−1, 1}`, so `ModulationEngine` / `VoiceModRouter` could route it unchanged.
  [cross-cutting: ModulationSource concept; `profundum_motion.h:126-134` pattern]
- **FR-008** Every member except `prepare` MUST be `noexcept` and free of allocation, locks, exceptions and I/O, with fixed-size
  storage only. [cross-cutting RT]

### Engine integration (DSP, append-only)

- **FR-010** `ProfundumEngine` MUST gain these pending setters, latched to the next grid point and applied change-only in the C-2
  sub-step:
  - `setMorphModulation(const ProfundumMorphModulator::Config&)`;
  - `setMorphModWheel(float)`;
  - `setMorphModBypass(bool)`.

  `setMorphPosition(float)` MUST store the sanitised base `b` and forward it; the per-interval target replaces it before every
  `advanceInterval`, and `prepare` / `reset` re-hand `b` immediately before the morph snap (plan P-1). The sanitising rule is
  `BassMorphEngine::setPosition`'s (`bass_morph_engine.h:140-147`). [C-2, C-7, C-8; R1, user 2026-10-26]
- **FR-011** The grid step MUST run sub-step 1b exactly as C-2 specifies, after step 1 and before `morph_.advanceInterval()`. The
  numbered steps 1–8 MUST NOT be reordered. [`profundum_engine.h:22-25`; roadmap 1089–1090]
- **FR-012** The target MUST equal `b` by branch when Source is Off, Amount is ±0 or the bypass is set. Otherwise it is
  `clamp(b + Amount · s, 0, 1)`, computed in float and handed to `morph_.setPosition`. [roadmap 1089; C-2; fast-math rule]
- **FR-013** The engine MUST call the modulator's `noteOn` for every accepted note-on as the first statement of `applyNoteOn`
  (`profundum_engine.h:793`), before the MONO branch's `MonoHandler::noteOn` early return (`:798-800`), in MONO and POLY alike,
  and before sub-step 1b of the same grid point. It MUST NOT be driven through `consumeEnvelopeRestart()` / Motion's restart path
  (`:918-925`). A note-on dropped by the full queue (`profundum_engine.h:230-233`) MUST NOT reach it. [roadmap 1092–1093, 1095;
  C-4, C-6]
- **FR-014** `prepare` MUST prepare the modulator. `reset` MUST reset it and keep its pending config, wheel and bypass (the
  `profundum_engine.h:199-200` rule for pending setters). The engine's tempo forwarding MUST also reach the modulator.
  [C-4, C-5]
- **FR-015** `ProfundumGridRecord` MUST gain the three appended fields of C-2. No `ProfundumCall` enumerator is added and no field
  is reordered. [`profundum_engine_observer.h:43`]
- **FR-016** `restoreMorphRuntime` sets `b` to the restored target and resets the modulator on every successful restore, also
  mid-stream (plan P-2, P-3); `serializeMorphRuntime` patches bytes 1–4 with `b`. `restoreMorphRuntime` otherwise keeps its
  Phase 11 FR-074 behaviour (dependents re-snapped only when nothing rendered since reset, `profundum_engine.h:280-285`).
  [C-11; Phase 13 FR-047; R2, R5 (P-2), user 2026-10-26]
- **FR-017** Every engine edit MUST be append-only. Every existing Phase 9 / 10 / 11 / 12 / 13 test that includes
  `profundum_engine.h` MUST stay green with unchanged verdicts. [cross-cutting shared-header rule; C-16]
- **FR-019** The `dsp/` change set MUST be exactly the C-1 list (the new header `systems/profundum_morph_modulator.h`,
  append-only edits to `profundum_engine.h` and `profundum_engine_observer.h`, the append-only test-driver edit of C-17, and new
  unit tests), plus (R5 / plan P-8) the registration lines for the new TUs in `dsp/tests/CMakeLists.txt`, and appended
  TEST_CASEs (or, for `Profundum_EngineDisplayAccessorNeutral`, an appended SECTION) in the existing Profundum test TUs
  `profundum_engine_perf_test.cpp`, `profundum_engine_display_test.cpp` and `profundum_voice_engine_helpers_test.cpp`; no
  existing case in them changes. The build stage is authorised to edit these files from its first task and MUST NOT stop at a
  `dsp/` scope fence for them. Any other `dsp/` file edit is out of scope, except the R11 optimisation (FR-053). [user
  2026-10-26; C-1; R5 (P-8); R11]
- **FR-018** Morph Modulation MUST sit outside both endpoints:
  - `MorphState` and `MotionConfig` (`morph_state.h`) MUST be unchanged, and `kMorphStateBytes` stays 201 (`morph_state.h:442`);
  - the nine 1003–1011 parameters MUST NOT be `kSoundRows` rows or routed as `RouteKind::Sound`, and `kSoundRows` keeps 60 rows
    (`sound_param_table.h:63`);
  - an Edit Target switch (1001) MUST NOT re-display them;
  - Copy A→B and Copy B→A MUST leave them, and the processor's `MorphModParams`, unchanged;
  - the modulation config is one global value, identical at every Morph position.

  [roadmap 1086–1088; C-12]

### Plugin: parameters, processor, state

- **FR-020** The nine C-12 parameters MUST be registered with the C-12 titles, ranges, laws, defaults, flags and display strings.
  The route table MUST cover 0..1011, and the processor MUST denormalise, sanitise and store them in `MorphModParams` atomics.
  [roadmap 1101–1102, 1104]
- **FR-021** `pushParamsToEngine` MUST hand the engine the modulation config, the Mod Wheel value and the bypass (C-8) on every
  push, and `setMorphPosition` the registered position (the Compare A / B pinning is unchanged). [C-7, C-8]
- **FR-022** `applyEndpoints` MUST compute `c` by the C-7 rule: `c = clamp(Channel Pressure, 0, 1)` only when Source = Mod Wheel
  AND Amount != 0, otherwise `c = clamp(Mod Wheel + Channel Pressure, 0, 1)`. With any Source other than Mod Wheel, and with
  Source = Mod Wheel at Amount 0, the assembled endpoint states MUST be bit-identical to today's (`processor.cpp:921-950`).
  [roadmap 1096–1097; user 2026-10-26]
- **FR-023** `getState` MUST write the C-10 block after the names (610 bytes). `setState` MUST read it by the C-10 load rules.
  The processor's v1 / v2 paths MUST reset the block to defaults. `kCurrentStateVersion` MUST stay 3, `getState`'s first int32
  MUST be 3, no version-4 writer or reader may exist, and a version-4 stream MUST be refused as a future version
  (`processor.cpp:609-611`). [roadmap 1098–1100; user 2026-10-26: "We don't need a new saved state version"; C-10]
- **FR-024** The controller's `setComponentState` MUST mirror FR-023: same rules, no `setParam` for a value that is not stored.
  [Phase 11 FR-045 pattern]
- **FR-025** Mod Wheel (6) and Channel Pressure (7) MUST stay hidden, unsaved and reset by `setState`
  (`performance_params.h:109-115`). `IMidiMapping` MUST be unchanged. [roadmap 1096–1097]

### Plugin: editor, tooltips, documentation

- **FR-030** The editor MUST place the C-13 controls: the Main-page `morph-band` holds Morph Mod Source (1003) and Morph Mod
  Amount (1004) only and none of 1005–1011; Advanced → Modulation holds all nine (1003–1011). Every new control MUST be bound to its tag, carry a tooltip, and pass the
  Phase 12 layout, placement, zoom (100–200 %), class, label and tag-integrity tests with no overlap. [roadmap 1105; C-13]
- **FR-031** The two new Main-page controls (1003, 1004) MUST each gain a gated row in `Profundum_MainPageAudibility` (the
  Phase 12 SC-006 (a) rule, roadmap line 1039–1040), pinned as follows:
  - **1003 Morph Mod Source:** lo = Off, hi = Velocity, with Amount 1, every note at velocity 127, Mod Wheel (6) at its default
    0 and Morph Position 0, as normalised parameter changes (re-pinned by R7; it was Off → Mod Wheel with the wheel at 1);
  - **1004 Morph Mod Amount:** lo = 0, hi = 1, with Source = Velocity, every note at velocity 127 and Morph Position 0;
  - **both:** the endpoints are the AT-41 showcase pair (`audibility-table.csv` rows AT-41, "showcase pair (A_s (C-8), B)"),
    loaded through `setState`; the descriptor is D_nl (dB) on the Char / twin tap, the twin carrying the same modulation (C-17);
    the read window is the D_nl reader's frame `[1.0 s, 1.0 s + W)`, W = 262 144 samples (`bass_morph_test_helpers.h:703-707`),
    so each arm renders 1.0 s + W + 0.25 s; notes E1 and C3; bar ≥ 3 dB;
  - **precondition (non-vacuity):** in the hi arm, the grid-record resolved position varies by ≤ 0.01 inside the D_nl reader's
    frame `[1.0 s, 1.0 s + W)` and is ≥ 0.9 (the modulation reached B), and in the lo arm it is ≤ 0.01 over the same frame;
  - **note, 1003 pin (R7, replaces the R5 / plan P-6 confound note):** the Off → Mod Wheel pin could not clear the bar with a
    correct implementation. In its lo arm (Off) the wheel at 1 still pushed Mass (C-7: `c = clamp(1 + 0, 0, 1) = 1`, Mass 100 on
    both endpoints, `processor.cpp:928-944`), which on its own lifted D_nl to 13.07 dB (E1) against the hi arm's 14.35 dB: G15
    gate a1 and a3 read |diff| 1.28 dB (E1) and 1.24 dB (C3) against 3 dB. Velocity has no Mass hand-over, so the Off → Velocity
    contrast moves Morph alone. The reading still logs both arms' grid-record Mass (`smoothedMass`), which must now be equal.

  [roadmap 1104, 1114–1115; C-15; R5 (P-6), R6 (tasks Deviation 1), R7 (G15 ruling), user 2026-10-26, 2026-10-27]
- **FR-035** The Main-page Morph slider MUST carry a display-only live indicator of the resolved (modulated, ramped) morph
  position (C-18). The indicator MUST NOT move the slider, change its value or write any parameter. [user 2026-10-26; C-18]
- **FR-036** `ProfundumSpectrumFrame` MUST gain one appended float, the resolved morph position, at offset 288, with padding to
  320 bytes; `sizeof == 320`, `sizeof % 32 == 0`, trivially copyable and standard layout, and every existing field offset
  unchanged. It travels over the existing DataExchange queue (no new queue or message). The processor's block size MUST stay
  rounded up to the alignment (`processor.cpp:349-356`). The producer sanitises it to [0, 1]. [C-18]
- **FR-037** The engine MUST expose a read-only accessor for the resolved morph position (append-only, FR-017), with no effect on
  the render. The processor writes it into each published frame; the controller caches it with the frame and the new view reads
  it. Before any frame, or with no DataExchange, the view shows the base position. The indicator updates at the frame cadence
  (each completed window while a note sounds, every 0.25 s in silence, `spectrum_producer.h:47`); the manual says so.
  [C-18; R3 (plan P-5), user 2026-10-26]
- **FR-032** The tooltips MUST contain these literal substrings:
  - Morph Mod Source (1003): `stops pushing Mass` (the C-7 hand-over, which needs a non-zero Amount; the tooltip also says the
    wheel keeps pushing Mass while Amount is 0);
  - Morph Cycle Rate (1009): `Morph Ramp Time` (the C-9 slew interaction);
  - Morph Ramp Time (1002): `Morph Mod` (the C-9 slew interaction, from the other side).

  [C-7, C-9]
- **FR-033** `plugins/profundum/MANUAL.md` MUST document all nine controls by their registered titles. That covers the five
  sources, the formula, the envelope restart and rest behaviour, the Cycle sync, the Velocity hold, the Mod Wheel / Mass
  hand-over, the Compare bypass and the C-9 slew consequence. `Profundum_ManualCoversSurface` MUST pass with 93 titles.
  [roadmap 1106]
- **FR-034** The open `[1.0.0]` CHANGELOG entry MUST gain one bullet for Morph Modulation, and its parameter count and state size
  statements MUST be corrected. `version.json` MUST NOT be edited. [roadmap 1081; user 2026-10-26: "all part of version 1.0.0"]

### Factory library

- **FR-040** `Capability` MUST gain the four C-14 columns, each the primary of at least one preset. `Profundum_CoverageMatrixComplete`
  MUST count 57 columns with 0 empty. [roadmap 1106–1108; C-14]
- **FR-046** The factory library MUST convert existing presets to the new columns (C-14):
  - a conversion happens wherever the hidden probe (FR-044) shows the source audible at the C-15 bar (D_nl >= 3 dB at E1 and C3);
  - a new preset is added ONLY for a column that no existing preset can carry, and the probe row proving that is recorded;
  - the library stays near 58 presets;
  - the coverage matrix keeps every pre-existing column covered (no pre-existing column becomes empty);
  - every converted preset keeps its decoded pre-existing fields (FR-061) and passes every FR-043 gate as stored;
  - **selection rule, ruled (ii) (R4 / plan P-9, §5.7 step 4, user 2026-10-26):** every eligible carrier converts, each for the
    column of its largest margin (margin = `min(contrast_E1, contrast_C3) − 3 dB`, plan §5.7 step 2; eligibility per plan §5.7
    step 3). A preset converts for at most one column. A column with no eligible carrier gets one new preset, built from the probe
    row that cleared the bar with the largest margin, on a base the probe names; its "no existing preset could carry it" proof is
    the column's probe rows. The constant `kMorphModConversionRule` names rule (ii).
  - **new presets are designed sounds (R9, user 2026-10-27):** each added preset keeps the probe row's Morph Mod settings but
    carries its OWN endpoints A and B, not a copy of the base's, so it is distinct from every other preset at rest (wheel 0, no
    modulation). It must clear FR-041 on its own endpoints and pass every FR-043 gate. (The first cut copied Fed Fold's
    endpoints into all three: Wheel Fold read distinctness d 0 against Fed Fold, bar 2.)
  - **a conversion a Phase 13 gate measures red is replaced by the next carrier (plan §10):** the (preset, column) pair and its
    gate reading go in `evidence/gate-exclusions.csv`, which `select.js` and SC-018's re-derivation both read; that pair is not
    carriable, so the preset falls to its next column. Applied 2026-10-27: Long Tail read 5.37 / 5.37 / 4.95 dB from the
    loudness median (bar 4) on M-ENV / M-CYCLE / M-VEL and 2.55 dB on M-WHEEL, so it carries M-WHEEL and the M-WHEEL new
    preset (Wheel Fold) is not added: the library is 60.

  [user 2026-10-26, 2026-10-27; C-14; R4; R9]
- **FR-041** Each new column's primary preset MUST clear the C-15 arm at E1 and C3. The cells are written to the Phase 13
  coverage-matrix CSV. [roadmap 1107, 1115; C-15]
- **FR-042** The C-14 restrictions MUST hold:
  - single-state claimants store Source Off;
  - re-primaried presets leave no column empty;
  - stored modulation values are inside the C-12 ranges and are not non-finite.
- **FR-043** After the library change every Phase 13 gate MUST be green. That covers:
  - claims (`Profundum_PresetShowcase_*`);
  - loudness (`Profundum_PresetLoudnessMatch`);
  - A-B endpoints (`Profundum_PresetMorphEndpoints_*`);
  - the morph sweep (`Profundum_PresetMorphSweep`);
  - held load (`Profundum_PresetLoadUnderHeldNote`);
  - distinctness, breadth, the render guards, mono sum, determinism, the state format, round trip and CPU.

  Opted-in presets are measured as stored, with the modulation running, except the A-B endpoint check
  (`Profundum_PresetMorphEndpoints_*`: distance and level delta). For a preset that stores a source other than Off it renders
  endpoint A as a preset whose two endpoints are both A, as it already renders B, so the modulation cannot move either and the
  two designed sounds are measured (R8). A Source-Off preset is rendered as stored, so its readings are unchanged.
  [roadmap 1107–1108; R8, user 2026-10-27]
- **FR-045** The render harnesses MUST carry the modulation as C-17 specifies: `VoiceEngineScript` gains the appended
  `morphMod` / `morphModWheel` / `morphModBypass` fields that `voiceEngineRender` applies and `voiceEngineTwinScript` copies;
  `decodePresetState` decodes the C-10 block and `effectiveScriptFor` overlays it; `ppRenderProcessor` builds its twin from the
  processor's modulation config, not from `morphPosition()`. With the default fields every existing script MUST render as
  before. [C-17; FR-043]
- **FR-044** The Phase 13 audibility probe pattern applies: a hidden probe renders each source's C-15 arm on candidate settings
  **before** any preset definition opts in, and writes `evidence/probe-morphmod.csv`. [Phase 13 FR-020 / FR-022 rule]

### Theme FRs

- **FR-050 (audibility)** Every source is audible on its showcase preset (FR-041), and the two Main-page controls have gated rows
  (FR-031). Nothing ships inert. [cross-cutting]
- **FR-051 (mono compatibility)** The modulation creates no stereo path. Every opted-in or new preset passes the Phase 13 FR-042
  mono-sum read as stored, and the Anchor stays L == R bitwise with the modulation running. [cross-cutting]
- **FR-052 (anti-aliasing)** No new nonlinear stage is added, so no new E0–E2 measurement is owed. The Phase 9 SC-021 verdicts
  carry over unchanged. This is recorded, not re-measured. [cross-cutting]
- **FR-053 (CPU)** The CPU bars hold with the modulation running (SC-014). The bars are never relaxed: when the
  modulation's selection-group switches push an arm over its bar, the engine is optimised with behaviour unchanged
  within the render tolerances (R11). The R11 edit set: `primitives/tanh_adaa.h` (opt-in `processFast` / `F1Fast`;
  `process()` unchanged), the new `core/log_cosh_table.h` (generated by `tools/gen-log-cosh-table.js`),
  `processors/tube_stage.h` (append-only linear-gain setters), `systems/bass_character_stage.h` and
  `systems/shape_morph.h` (call sites), and `dsp/tests/unit/primitives/tanh_adaa_test.cpp` (two appended cases).
  [roadmap 1114–1115; cross-cutting; R11]
- **FR-054 (portability and lint)** `node tools/check-portability.js`, `tools/lint-apple-globals.js`,
  `tools/lint-finite-bitmask.js`, `tools/lint-nonfinite-symbols.js`, clang-tidy (`dsp` and `profundum`) and pluginval at
  strictness 5 MUST be clean, with zero compiler warnings. Finiteness is checked only with `detail::isFinite`, and fast-math code
  has no `infinity()` / `quiet_NaN()`. [cross-cutting]

### Transparency

- **FR-060** The roadmap's "Off and Amount 0 are bit-transparent against today's renders of every factory preset" is met by two
  parts (the refinement is recorded under Deviations and put to the user, Review note 9):
  - **(a) same binary, bitwise:** with any source at Amount 0 (including Mod Wheel at any wheel value, since at Amount 0 the
    wheel keeps pushing Mass, C-7), and with any source at Amount 1 under Compare A / B (the bypass, compared with
    Source Off under the same Compare), the output MUST be bit-identical to the Source-Off output of the same binary. That
    holds for **every** factory preset (a stored-Off preset as stored; an opted-in preset forced to Source Off), and on the
    engine-level grids;
  - **(b) against today's build:** every unconverted Source-Off preset (as stored) and every converted preset forced to Source Off
    MUST agree with the pre-phase harvest (FR-062) within `kSampleTolerance` / `kMetricTolerance`, with 0 failures. A converted
    preset as stored changes sound by design and is excluded from this comparison. There is no bit-exact cross-build golden.

  [roadmap 1112–1113; C-2; "no bit-exact float goldens", line 1152–1153]
- **FR-061** Every factory `Comp` MUST be 610 bytes, version 3, and decode every pre-existing field identically to its
  pre-phase file. The reference is the pre-phase decoded-field snapshot that FR-062's harvest writes before the generator
  changes. A test asserts the identity for every preset, converted or not (a conversion changes only the C-10 block, which is
  what lets a converted preset forced to Source Off reproduce its pre-phase render). [C-10; Phase 13 FR-006 amended from 574 to 610]
- **FR-062** Before the first engine edit, a hidden harvest case in `profundum_tests` MUST record:
  - `render_fingerprint.h` fingerprints of every factory preset (E1 and C3, the Phase 13 FR-046 span). That is a harvest inside
    the consuming binary;
  - every factory preset's decoded pre-existing fields, as the hex of each field's bits, to
    `specs/profundum-phase14-morph-modulation/evidence/prephase-decoded.csv`.

  At close the same binary's comparison case MUST compare every unconverted preset (as stored) and every converted preset forced
  to Source Off against the fingerprints within the shared tolerances, and every preset's decoded pre-existing fields against the CSV. This
  is a **close gate with 0 failures**, not optional evidence and not a per-push golden. [roadmap 1112–1113; "no bit-exact float
  goldens", line 1152–1153]
- **FR-063** Every preset that stores Source Off MUST keep the same primary and secondaries. A converted preset's primary moves
  to its new column only under C-14's rule (FR-046).

---

## Success Criteria

| SC | Requirement | Metric, threshold, method (test name sketch) | Lane |
|---|---|---|---|
| **SC-001** | FR-001, FR-002, FR-008 | The modulator's API compiles with exactly the FR-002 config fields (static_asserts). The layer include check passes: only L0 / L1 includes. 0 allocations across `setConfig`, `noteOn`, `setModWheel` and `advanceInterval` in every source (`AllocationScope`). `ProfundumMorphModulator_ApiAndRt` (new). **Engine and processor paths:** (b) an engine render with `VoiceEngineScript::countAllocations` (`profundum_voice_engine_test_helpers.h:154`) for each non-Off source at Amount ±1, with `setMorphModulation` / `setMorphModWheel` / `setMorphModBypass` called every block, reads `taps.allocations == 0` (`ProfundumEngine_MorphModNoAlloc`, new). (c) `Profundum_Phase11PathsDoNotAllocate` (`plugins/profundum/tests/unit/phase11_alloc_test.cpp:154`, extended) routes all 93 IDs including 1003–1011 with Source rotating through every source and Amount ±1, moves Mod Wheel (6) every block, and toggles Compare inside the existing scopes: 0 allocations in `process()`. | `dsp_systems_tests` / `profundum_tests`, per push |
| **SC-002** | FR-003 | Envelope at fs / 32 = 1500, 1378.125 and 3000 Hz (48, 44.1 and 96 kHz). (a) The trace matches a reference `ADSREnvelope` set up identically to within 1e-6 absolute per interval. (b) Attack: time to peak is within ±1 control interval of A for A ∈ {0.1, 10, 400, 10 000} ms (A = 0.1 ms: peak in the first interval). (b′) Decay: with Sustain 0, the time from the peak to the interval where s first reads 0 (the Decay stage's end, `adsr_envelope.h:341-344`; Decay is the full 1 → 0 ramp, `:417`) is within ±1 control interval of D for D ∈ {10, 400, 10 000} ms. (c) With Sustain S ∈ {0.5, 1} the Decay stage ends at s = S exactly and \|s − S\| ≤ 1e-6 from then on; with S = 0, \|s\| ≤ 1e-3 from A + D + 1 interval onward. (d) Sustain smoothing: a Sustain step 0 → 1 during the Sustain stage settles to within 1e-3 of the new S in at most the C-4 counts (11 / 10 / 21 intervals at 48 / 44.1 / 96 kHz, confirmed by the plan's probe) and matches the one-pole `exp(−5000 / (5 · fs/32))` to 1e-6. (e) Restart: after `noteOn` at an interval where s > 0.5, s at that interval equals the first `process()` of a freshly `reset()` + `gate(true)` reference `ADSREnvelope` with the same settings within 1e-6 (C-4), and for A ≥ 10 ms it is lower than the previous interval's s. `ProfundumMorphModulator_Envelope` (new) | per push |
| **SC-003** | FR-004 | Cycle: (a) the s trace matches a reference `LFO` at the same control rate within 1e-6 for all 6 shapes. (b) Over ≥ 10 cycles the free rate is within 0.5 % of {0.01 (over ≥ 2 cycles), 0.1, 1, 10, 20} Hz. Synced at 90 and 120 BPM for 1/4, 1/8T and 1/1, the period is within 0.5 % of `beats · 60 / bpm`. (c) The range is within [−1, 1]. (d) Two runs of S&H / Smooth Random are identical, and the output does not depend on the engine seed. (e) Free-running: with `noteOn` called at irregular intervals (non-periodic, at least 8 over the render, velocities varied), the s trace equals the no-note reference `LFO` trace within 1e-6 for all 6 shapes, free and synced. `ProfundumMorphModulator_Cycle` (new) | per push |
| **SC-004** | FR-005, FR-013 | Velocity: s = v / 127 within 1e-6 for v ∈ {1, 64, 127}. It is 0 before the first note. It is held through note-off and a 2 s silence, and it changes at the grid point of the next accepted note-on. A dropped note-on (full queue) leaves it unchanged. MONO and POLY4. `ProfundumEngine_MorphModVelocity` (new) | per push |
| **SC-005** | FR-010–FR-012 | Engine: (a) grid-record `morphTarget` equals the test's recomputed `clamp(b + A · s, 0, 1)` within 1e-6. (b) `morphTarget` bits equal `b`'s bits whenever Source is Off, A = ±0 or the bypass is set. (c) The ramped position never leaves [0, 1]. (d) Per interval \|Δp\| ≤ `Δp_max` + 0.5 ulp (the Phase 8 B3-1 bar) for every source × A ∈ {−1, −0.5, 0.5, 1} × b ∈ {0, 0.5, 1} × ramp ∈ {5, 40, 2000} ms. `ProfundumEngine_MorphModComposition` (new) | per push |
| **SC-006** | FR-060 (a) (engine) | Engine renders: Source Off vs each of Envelope, Cycle, Velocity and Mod Wheel (wheel 0) at Amount 0, and Off vs bypass with Amount 1. The output L and R are bit-identical over 2 s in MONO and POLY4, on 3 settings (default, the Phase 8 showcase pair, a Fold + Wide pair), with notes E1 and C3. `ProfundumEngine_MorphModTransparentAtZero` (new) | per push |
| **SC-007** | FR-023, FR-060 (a), FR-061 (plugin) | **Every** factory preset, at E1 and C3 over the FR-046 span: a stored-Off preset as stored vs forced to each source at Amount 0 (wheel 0 and wheel 1); a converted preset forced to Source Off vs forced to each source at Amount 0 (wheel 0 and wheel 1); and, for every preset, Source Off vs its stored (or each) source at Amount 1 under Compare A and under Compare B: processor output bit-identical. Every `Comp` reads the **literal** 3 as its first int32 (not the symbol) and is 610 bytes. Every preset, converted or not, decodes every pre-existing field bit-equal to `evidence/prephase-decoded.csv` (FR-062, FR-061). `Profundum_MorphModPresetTransparency_kofN` (new, `[long]`), sharded so each case is ≤ ~15 s, wall clock recorded; `Profundum_FactoryPresetStateFormat` (`preset/preset_library_test.cpp:1239`, extended: `d.bytes == 610u`). | transparency: nightly (`[long]`, all three OSes; R10, user 2026-10-27: 120 render pairs, about 15 min of CPU per push); state format: per push |
| **SC-008** | FR-060 (b), FR-062 | The close gate: every unconverted preset as stored, and every converted preset forced to Source Off (converted presets as stored are excluded), has E1 and C3 fingerprints within `kMetricTolerance` / `kSampleTolerance` of the pre-phase harvest, which was taken in the consuming test binary before the first engine edit. There is no bit-exact cross-build golden. **0 failures required** to close the phase; the worst metric and sample errors are reported. `Profundum_MorphModPrePhaseFingerprints` (new, hidden `[.harvest]`; run by the close lane, not optional) | close gate (Windows) |
| **SC-009** | FR-006, FR-022, FR-025 | Processor. (a) With Source = Mod Wheel and Amount != 0 (for example 1 and -0.5), Mod Wheel 0 → 1 changes neither endpoint's Mass bits in the engine, and grid-record s = 1 at the next grid point. (a′) With Source = Mod Wheel and Amount 0 (and Amount -0), Mod Wheel 0 → 1 pushes Mass exactly as with Source Off (`Profundum_MassPerformanceOffset` arithmetic, bitwise): the wheel is not dead. (a″) Amount moving 0 → 0.5 with the wheel at 0.25 under Mod Wheel re-sends both endpoints once and the Mass push stops; 0.5 → 0 re-sends once and it resumes. (b) With every other source, the existing `Profundum_MassPerformanceOffset` arithmetic (`tests/unit/midi_mapping_test.cpp:78`, not edited) passes, bitwise. (c) Channel Pressure 0.5 pushes Mass by 50 under every source. (d) A Source change (to or from Mod Wheel, at Amount != 0) with the wheel at 0.25 re-sends both endpoints once. `Profundum_MorphModWheelRouting` (new, `tests/unit/midi_mapping_test.cpp`) | per push |
| **SC-010** | FR-003, FR-013 | Retrigger (engine level unless stated). Non-vacuity for every restart check: Attack ≥ 10 ms, Sustain 1, and s > 0.5 in the interval before the restarting note-on, so a restart reads lower than the interval before. (a) MONO, Legato on, three overlapping notes: at each note's grid point s equals the C-4 restart observable (a fresh reference envelope's first step) within 1e-6. (b) MONO with High priority, where a lower note is pressed: s at that grid point equals the restart observable (the note restarts although MonoHandler does not sound it). (c) POLY4, a 3-note chord at one offset: the s trace equals the trace of a single note-on at that grid point within 1e-6, and Velocity equals the last note in arrival order. (d) POLY4, three staggered notes: s equals the restart observable at each of the 3 grid points. (e) A note-off changes no s. (f) **Processor fixture**, Sustain Pedal (5) points: pedal down, note-on, note-off (latched, `processor.cpp:1037-1043`), note-on of the same key: s equals the restart observable at the second note-on's grid point. (g) A MONO ↔ POLY switch with notes held: the s trace equals a no-switch run's within 1e-6 (0 restarts). (h) Rest: a real engine `noteOff` after the envelope reached Sustain, then 5 s with no note: grid-record `morphModSource` stays within 1e-6 of S. All held voices read the same resolved position. `ProfundumEngine_MorphModRetrigger` (new); (f) `Profundum_MorphModPedalRestrike` (new, processor) | per push |
| **SC-011** | FR-011, FR-012, C-9 | Click bound, for every source × A = ±1 × the time extremes (Attack / Decay 0.1 and 10 000 ms; Cycle 0.01 and 20 Hz in all 6 shapes; ramp 5 and 2000 ms) on the Phase 8 showcase pair at E1. (a) The SC-005 (d) per-interval bound holds. (b) Over the render, max \|y[n] − y[n−1]\| ≤ 2 × max(the same metric on the static renders at the trajectory's min and max p), the Phase 13 FR-048 factor-2 rule. (c) 0 non-finite samples. `ProfundumEngine_MorphModClickFree` (new) | per push (sharded ≤ ~15 s) |
| **SC-012** | FR-014, C-8, block-size and determinism | (a) `Profundum_MidiEventTranslation` (`tests/unit/midi_event_test.cpp:210`) gains one `PartitionInvariance` section per non-Off source, reusing the existing script and partition list unchanged (host blocks {1, 7, 32, 65, 4096} against the 512 reference, `:240-241`). Each arm sets Amount = 1, Morph Position 0.5 and, for Mod Wheel, a constant Mod Wheel 1 from sample 0 (no mid-render wheel move: a wheel change applies at a block start and is not partition-invariant by design). Precondition (non-vacuity): in the 512 reference the grid-record `morphTarget` differs from b at ≥ 1 grid point. 0 mismatches. (b) Two renders with the same seed are bit-identical. (c) `prepare` at a new rate keeps the pending config; `reset` zeroes s for Envelope and Velocity and keeps the wheel and config. (d) Compare bypass continuity (C-8): with Envelope (Attack 400 ms, Sustain 1) and with Cycle (1 Hz) at Amount 1, Compare goes A·B → A for 0.5 s mid-gesture and back. During the bypass `morphTarget` equals b's bits; at every grid point `morphModSource` equals a no-bypass run's value within 1e-6 (the sources kept advancing, no envelope restart occurred). `ProfundumEngine_MorphModLifecycle` (new), `Profundum_MorphModCompareBypass` (new, processor) | per push |
| **SC-013** | FR-016, FR-023, FR-024 | State. (a) Round trip of all nine fields at 610 bytes: floats bit-equal, ints equal. (b) v1 and v2 streams load all nine as defaults. (c) A v3 stream truncated at each of the 36 offsets 574–609 keeps the prior value of every field from the cut on, in both processor and controller. (d) Hostile values (NaN, ±inf bit patterns, source −1 / 5, shape 6, index 30, sync 2) are not stored, and finite out-of-range floats clamp. (e) A blob written while the modulation is active carries target = `b`. (f) Load-then-save is byte-stable. (g) No version bump: `static_assert(kCurrentStateVersion == 3)`; `getState`'s first int32 reads the literal 3; a stream whose first int32 is 4 (otherwise a valid 610-byte body) is refused by `setState` with `kResultFalse` and changes no parameter. `Profundum_StateV3MorphMod` (new, `tests/unit/state_v3_test.cpp`, beside `Profundum_EndpointNames` `:198`); the existing `Profundum_StateVersions` (`state_v2_test.cpp:421`) and `Profundum_StateRoundTrip` (`state_roundtrip_test.cpp:230`) are extended for the 610-byte size. | per push |
| **SC-014** | FR-053 | CPU, run alone via `node tools/run-cpu-tests.js` (the Phase 11 SC-020 method: 640 × 10-block trials, P / D alternated, control arm within 1.05 or the run is INVALID). (a) With Cycle at A = 1, 20 Hz, ramp 5 ms on a pair whose four selection groups switch, MONO ≤ 8 % and 4 voices ≤ 25 % of one core at 48 kHz. (b) Phase 9 SC-003's worst 64-sample host block at 4 voices with the same modulation is ≤ 100 % of one core. (c) Every factory preset (opted-in as stored) meets Phase 13 FR-045. (d) The modulator's `advanceInterval` ns per interval is recorded. `Profundum_ProcessorCpuMorphMod`, `VoiceEngine_WorstIntervalMorphMod`, `Profundum_PresetCpu` | `[.perf]`, alone |
| **SC-015** | FR-020 | Parameter table: 93 parameters. The nine C-12 rows have exact titles, units, defaults, flags and list strings. Normalized ↔ plain round trips within the existing law tolerances, and the display strings match. The route table covers 0..1011 with no gap. Every hard-coded 84 listed in C-12 reads 93. `Profundum_ParamTableConformance` (`param_table_test.cpp:380`, extended), `Profundum_SoundParamTableRouting` (`:46`, extended: routed 93, `kSoundRows` still 60) | per push |
| **SC-016** | FR-030, FR-032 | The UI tests pass with the new views: `Profundum_UidescBindsEverySurfaceId` (`editor_layout_test.cpp:495`; all 93 bound, `:498` reads 93), `_UidescTagIntegrity`, `_UiLayout` (0 overlaps at every zoom step), `_UiPlacement`, `_UiZoom`, `_UiLabels` and `_UiClassRule`. The Main-page `morph-band` binds 1003 and 1004 and none of 1005–1011, and the Advanced → Modulation tab binds all nine (FR-030). The Main-page Morph slider carries the new indicator view (SC-024). Every new control has a non-empty tooltip; the 1003 tooltip contains `stops pushing Mass`, the 1009 tooltip contains `Morph Ramp Time`, and the 1002 tooltip contains `Morph Mod` (FR-032). The editor-lifecycle shared-view count is unchanged. | per push |
| **SC-024** | FR-035, FR-036, FR-037 | Live indicator. (a) `sizeof(ProfundumSpectrumFrame) == 320`, `% 32 == 0`, trivially copyable, standard layout, the appended float at `offsetof == 288`, `windowEndSample` still 24 and `partialPower` still 32; the DataExchange block size is a multiple of the alignment and `>= sizeof` (the `processor.cpp:349-356` rounding kept). (b) The published frame's resolved-position field equals the engine's resolved position at frame production within 1e-6, for Source Off (equals base) and for each non-Off source at Amount 1; it is sanitised into [0, 1]. (c) Publishing the field changes no audio sample (bit-identical output to a run with the frame path unchanged, as `Profundum_SpectrumFrameDoesNotChangeAudio`) and allocates nothing (`Profundum_SpectrumFrameAllocationFree`, extended). (d) The engine accessor is render-neutral (extends `Profundum_EngineDisplayAccessorNeutral`). (e) The view draws the indicator at the cached resolved position, never moves the Morph slider (its value and the parameter are bit-unchanged across frames and `edit` count is 0), and falls back to the base position before the first frame. (f) `Profundum_EditorLifecycle` open/close cycles with the new view leave the shared-view count unchanged and are clean under ASan. `Profundum_MorphIndicatorFrame` (new, `integration/spectrum_frame_test.cpp`), `Profundum_UiMorphIndicator` (new, `unit/controller/ui_views_test.cpp`) | per push |
| **SC-017** | FR-031, FR-041, FR-044, FR-050 | (a) `evidence/probe-morphmod.csv` holds one row per opted-in preset's (source, settings) arm with the probe's run id and its measured D_nl, and the showcase case REQUIREs, for each new column's primary, a probe row with matching source and settings whose value is ≥ the bar. (b) Each new column's primary preset clears D_nl ≥ 3 dB at E1 and C3 under C-15 with no precondition override, and its minimum margin is reported. (c) The FR-031 Main-page rows for 1003 and 1004 clear D_nl ≥ 3 dB at E1 and C3 with their resolved-position preconditions met. `Profundum_PresetShowcase_*` (new cells), `Profundum_MainPageAudibility` (`integration/main_page_audibility_test.cpp`, extended) | `[long]` showcase; per push main-page |
| **SC-018** | FR-040, FR-042, FR-043 | 57 columns with 0 empty and exactly 1 primary per preset; every pre-existing column (the 53 columns of Phase 13: 50 AT and 3 reference) is still covered after the conversions. Library size is read and reported (near 58); each preset added beyond the existing library has a probe row (SC-017 (a)) proving no existing preset could carry its column (FR-046). 0 single-state claimants storing Source ≠ Off. Every converted preset passes every FR-043 gate as stored. All FR-043 Phase 13 gates are green, with their worst margins reported against the pre-phase run. The converted set (defs whose primary is in the `MorphMod` group, excluding presets added for a column no existing preset can carry) equals the ruled rule (ii) (FR-046, R4) applied to `evidence/probe-morphmod.csv`. | per push + `[long]` |
| **SC-019** | FR-051 | Every opted-in or new preset passes the FR-042 mono-sum read as stored (ρ ≥ 0.99, \|Q\| ≤ 1.0 dB, read gated). At engine level, with Cycle at A = 1 sweeping a SOLID ↔ WIDE pair, every voice reads `anchorL == anchorR` bitwise. `Profundum_PresetMonoSum` (shards), `ProfundumEngine_MorphModAnchorMono` | `[long]`; per push |
| **SC-020** | FR-033, FR-034 | `Profundum_ManualCoversSurface` passes with 93 titles. The CHANGELOG `[1.0.0]` entry carries the bullet and corrected counts. `version.json` is unchanged (`git diff --quiet`). | per push + review |
| **SC-021** | FR-054, FR-017 | check-portability, lint-apple-globals, lint-finite-bitmask, lint-nonfinite-symbols and clang-tidy (`dsp`, `profundum`) report 0 findings on the change set. pluginval strictness 5 passes. Builds have 0 warnings. Every consumer case of the two modified engine headers keeps its verdict, including Phase 9 SC-032 (call sequence identical). | tools, close |
| **SC-022** | FR-018 | Outside the endpoints. (a) Compile time: the existing `static_assert(kMorphStateBytes == 201)` (`morph_state.h:442`) is unchanged; `static_assert(kSoundRows.size() == 60)`; none of 1003–1011 routes as `RouteKind::Sound`. (b) Processor + controller: the nine set to non-defaults (Source Cycle, Amount −0.6, Attack 50 ms, Decay 900 ms, Sustain 0.4, Shape Square, Rate 3 Hz, Sync On, Sync Rate index 7); then Copy A→B, Copy B→A, and Edit Target A → B → A. After each step all nine normalized values in the controller and the processor's `MorphModParams` atomics are bit-unchanged, and `redisplayEditTarget` issues no `setParamNormalized` for 1003–1011. `Profundum_MorphModOutsideEndpoints` (new, `integration/copy_compare_test.cpp`, beside `Profundum_CopyEndpoint` `:226`) | per push |
| **SC-023** | FR-045, FR-043 | Harness fidelity with the source running (C-17). (a) For one preset per new column (its primary), the Phase 13 fidelity arm passes: processor vs engine-direct within `kMetricTolerance` / `kSampleTolerance` on sumL / sumR / characterL / characterR. (b) Over the C-15 window, the D_nl twin's grid-record resolved position equals the processor render's within 1e-6 at every grid point. (c) The `ppRenderProcessor` twin, on the FR-031 1003 hi arm, satisfies (b) too. (d) In the same binary, a Phase 9 showcase script with the appended fields left at their defaults renders bit-identical to the same script with Source explicitly Off, Amount 1 and wheel 1 (the C-2 Off branch), and every existing consumer case of the helper keeps its verdict. `Profundum_MorphModHarnessFidelity` (new), `VoiceEngineHelpers_MorphModFields` (new, in `dsp/tests/unit/systems/profundum_voice_engine_helpers_test.cpp` beside `VoiceEngineHelpers_DriverSplitsAtEvents` `:191`) | per push + `[long]` |

---

## Edge cases

- **RT boundaries.** Every new engine path runs in the grid step: no allocation, lock or I/O. `prepare` is control-thread only.
  The modulation setters are pending stores, so a burst of automation points costs O(1) per block.
- **Amount ±0 and −0.** `−0.0f` takes the C-2 branch, because `== 0.0f` matches ±0. A negative Amount on a one-sided source pulls
  toward A and clamps at 0.
- **Base at an extreme.** With Morph at 0 and a negative Amount, or at 1 and a positive Amount, the clamp holds the target at the
  endpoint, so the modulation is silent there. This is documented in the manual, not treated as a defect.
- **Attack 0.1 ms.** At fs / 32 the minimum attack is one interval (`adsr_envelope.h` per-step `max(1, …)`). The audible rise is
  then the ramp (C-9).
- **Envelope at 10 000 ms with ramp 2000 ms.** The trajectory is envelope-bound. SC-002 measures the source and SC-005 measures the
  slew separately.
- **Cycle above the ramp's slope.** It is triangle-limited (C-9). SC-011 covers 20 Hz at ramp 2000 ms.
- **Tempo changes mid-cycle.** The synced LFO follows `LFO::setTempo`'s own continuity. A non-finite or ≤ 0 tempo keeps the
  previous one (`profundum_engine.h:258-262`).
- **Sample-rate change.** `prepare` at a new rate re-clocks the envelope and LFO at the new fs / 32 and resets them (LFO
  `prepare` recomputes its increments, `lfo.h:83-92`). The pending config, wheel and bypass survive (`profundum_engine.h:145-146`
  rule).
- **Rates outside [22 050, 192 000].** The processor refuses them with silence (Phase 10 Q6). No modulator path runs.
- **Seed determinism.** No source draws on the engine seed: Envelope, Velocity and Mod Wheel are deterministic, and the LFO
  random shapes use the fixed `12345u` (C-5). Phase 13 FR-044 `seedDependent` declarations are therefore unchanged by opting in a
  Cycle S&H source.
- **Mode switch (MONO ↔ POLY).** It does not restart the envelope, because only an accepted note-on does (SC-010 (g)).
- **Polyphony change, voice steal.** A steal follows a note-on, which restarts the envelope.
- **Note-on in the same block as `setState`.** The restore hand-off (Phase 11 FR-042) installs the endpoints. The modulator state
  is not part of the runtime blob, so it starts from reset values. The Phase 13 FR-048 held-load click bound covers the load.
- **Compare toggles** (C-8). Switching to A / B pins the position and bypasses. Back to A·B, the target rejoins the running source
  through the ramp (SC-012 (d)).
- **Source switches mid-note.** s jumps to the new source's current value. The ramp bounds the step (SC-005 (d), SC-011).
- **Queue overflow.** A dropped note-on neither restarts the envelope nor changes Velocity (FR-013).
- **Hidden wheel at load.** `setState` resets Mod Wheel to 0 (`performance_params.h:109-115`). A Mod-Wheel-source preset loads
  with s = 0 and its morph at the stored position.
- **Maximum-Mass gates with a Mod-Wheel-source preset** (Phase 13 FR-041 / FR-046 write Mod Wheel 1). Wheel 1 now moves Morph
  instead of Mass. Mass is already 100 by the gate's own writes, so the gate's bound is unchanged and still checked on the
  modulated state.


## Deviations from the roadmap

- **D-1: "bit-transparent against today's renders" is refined** (roadmap line 1112–1113). A pre-phase render can only be kept as
  a reference by storing it, and the roadmap's own cross-cutting rule forbids bit-exact float goldens (line 1152–1153). The spec
  therefore meets the criterion in two parts (FR-060): (a) **same-binary bit-identity** of Source Off vs every source at Amount 0
  vs the Compare bypass, for every factory preset (SC-006 per push, SC-007 nightly per R10); and (b) **agreement with the pre-phase build**
  within `kSampleTolerance` = 5e-4 / `kMetricTolerance` = 2.5e-4 (`render_fingerprint.h:58-61`) for every Source-Off preset,
  as a close gate with 0 failures (SC-008). Part (b) is the only link to today's build, and it is tolerance-based, not bitwise.
  This needs a user ruling (Review note 9).

## Review notes (for the challenge and clarify stages)

Decisions with user-facing consequences that the roadmap's rulings do not literally fix. Each is decided above and listed here so
the clarify scan can put it to the user:

1. **C-4: the envelope restart is from 0, not from the current value.** Otherwise a sustain-1 rise never repeats. The cost is a
   ramp-length dip toward A at each fast legato note.
2. **C-4 / C-6: "new note" means every accepted note-on.** That includes a MONO note-on that priority does not sound and a
   latched re-strike.
3. **C-3 / C-5: Cycle is bipolar around the slider and free-running.** It has no retrigger, phase or fade-in control. This is
   the opposite of Motion's CYCLE default, which retriggers on every note-on (`MotionConfig::cycleRetrigger = true`,
   `morph_state.h:95`; `profundum_motion.h:243-246`). The Morph Cycle deliberately does not, because a per-note re-phase of a
   global value jumps every held voice's morph position (SC-003 (e)).
4. **C-12: Amount defaults to 0.** Choosing a source alone changes nothing until Amount moves (the Motion Amount precedent).
5. **C-14: the source showcases convert existing presets where the probe supports it,** with a new preset only where none fits
   (ruled, 2026-10-26).
6. **C-1: this phase edits `dsp/` from the start.** The fence is the new header plus append-only edits to `profundum_engine.h`
   and `profundum_engine_observer.h`; the plugin phases 10–13 ran behind a `dsp/` scope fence.
7. **C-10: a v1 / v2 load resets the block to defaults, while a cut v3 keeps prior values.** No 574-byte v3 reader exists.
8. **C-8: Compare A / B bypasses the modulation, and the sources keep advancing under the bypass.** The roadmap formula
   (line 1089) has no Compare exception; this is a new user-facing decision. The user hears the pure endpoint under A / B, and
   returning to A·B continues the running gesture rather than restarting it (SC-012 (d)). The alternative is to let the
   modulation run under Compare, so A / B would no longer be the pure endpoint.
9. **D-1: the roadmap's "bit-transparent against today's renders" is met as same-binary bit-identity plus tolerance-based
   agreement with the pre-phase build** (FR-060, SC-007, SC-008), because the roadmap forbids bit-exact float goldens. The user
   should confirm this reading.
10. **C-17: the phase edits the shared test driver `profundum_voice_engine_test_helpers.h`** (append-only), widening the C-1
    fence beyond the two engine headers, so the Phase 13 gates can measure opted-in presets at all.

## Clarifications

### Rulings carried from the roadmap (user, 2026-10-26; roadmap lines 1083–1102)

- All five sources: Off · Envelope · Cycle · Velocity · Mod Wheel.
- Global, outside both endpoints; not a `MorphState` field; one value drives every voice. Enforced by FR-018 / SC-022.
- Result = clamp(Morph Position + Amount × source, 0, 1), Amount bipolar, through the Morph Ramp Time smoothing.
- Envelope A·D·S, restarting on every new note in MONO and POLY.
- Cycle reuses Motion CYCLE's shapes and tempo sync.
- Velocity = latest note-on's velocity, held until the next.
- Mod Wheel as source drives Morph only and stops pushing Mass; otherwise the wheel pushes Mass as today; `IMidiMapping`
  unchanged.
- No state-format version bump: the parameters join v3 and its byte count grows. Defaults are Source Off.
- Parameters at 1003+, `kCanAutomate` except where Phase 11's rules say otherwise.
- User, 2026-10-26: "I want all the sources. We don't need a new saved state version since the plugin isn't released yet. this
  is all part of version 1.0.0". Enforced by FR-001–FR-006 (all five sources), FR-023 / SC-013 (g) / SC-007 (version stays 3,
  literal) and FR-034 / SC-020 (`version.json` unchanged).

### Session 2026-10-26 (user interview of the open clarifications)

- Q1 Envelope on a new note while above 0: restart from 0 (reset then gate on) on EVERY note-on the engine accepts, MONO and POLY, including a MONO note-on that note priority does not sound and a sustain-pedal re-strike. [FR-003, FR-013, SC-002 (e), SC-010]
- Q2 Morph Cycle scope: free-running with four controls only (Shape, Rate, Sync, Sync Rate); no Retrigger, Phase or Fade-In; 93 parameters, IDs 1003-1011. [FR-002, FR-004, FR-020, SC-003 (e), SC-015]
- Q3 Mod Wheel hand-over: the wheel stops pushing Mass only when Source = Mod Wheel AND Amount != 0; at Amount 0 (the default) the wheel keeps pushing Mass so it is never dead. [FR-022, FR-060 (a), SC-009 (a), (a-prime), (a-double-prime), (d), SC-007]
- Q4 Compare A / B: bypasses the morph modulation so the pure endpoint is heard; sources keep advancing while bypassed and A-B resumes the running gesture without a restart. [FR-012, FR-021, SC-005 (b), SC-007, SC-012 (d)]
- Q5 Live modulated Morph position in the editor: a display-only indicator on the Main-page Morph slider (OffsetRingKnob precedent), fed by one resolved-position float appended to ProfundumSpectrumFrame over the existing DataExchange path (frame 288 to 320 bytes, aligned-alloc rounding kept), with a new view and UI plus lifecycle tests. [FR-035, FR-036, FR-037, SC-024, SC-016]
- Q6 Main-page controls: Source and Amount only on the morph band; all nine controls on Advanced -> Modulation. [FR-030, SC-016]
- Q7 Factory library: convert existing presets to the four new source columns wherever the probe shows the source audible at the AT-41/42 bar; a new preset only for a column no existing preset can carry; library stays near 58; converted presets are excluded from the pre-phase fingerprint comparison except forced to Source Off; every converted preset passes every Phase 13 gate; every pre-existing column stays covered. [FR-046, FR-040, FR-043, FR-060 (b), FR-061, FR-062, FR-063, SC-008, SC-018]
- Q8 Transparency: same-binary bit-identity (Source Off vs Amount 0 vs bypass) on every preset plus a tolerance comparison against fingerprints harvested in the consuming binary before the first engine edit, as a 0-failure close gate; no bit-exact cross-build golden. [FR-060, FR-062, SC-006, SC-007, SC-008]
- C1 dsp/ scope: confirmed; the phase edits dsp/ from the start (new profundum_morph_modulator.h, append-only profundum_engine.h and profundum_engine_observer.h) and the build stage must not stop at a dsp/ scope fence for them. [FR-019, FR-017, C-1]
- C10 State: confirmed; stays format v3 (plugin unreleased, part of 1.0.0), grows 574 to 610 bytes; v1/v2 load the block as defaults; a truncated v3 keeps prior values; no 574-byte v3 reader. [FR-023, FR-024, FR-061, SC-013]

### Session 2026-10-26 (plan rulings)

Recorded in `evidence/plan-rulings.md`; each is written into its row(s) above first.

- R1 (plan P-1, §11 Q4) Store and forward: `setMorphPosition` stores the sanitised base `b` AND forwards it; the per-interval target replaces it before every `advanceInterval`; `prepare` / `reset` re-hand `b` immediately before the morph snap. [FR-010, C-2, SC-006, SC-007, SC-008]
- R2 (plan P-3, §11 Q3) Always reset: every successful `restoreMorphRuntime` (preset or state load) sets `b` to the restored target and resets the modulator, also mid-stream under a held note (the envelope restarts, Velocity zeroes, the Cycle re-phases). [FR-016, C-11, SC-013 (e), SC-013 (h)]
- R3 (plan P-5, §11 Q1) Frame cadence: the live indicator updates when a spectrum frame is staged (each completed window while a note sounds, every 0.25 s in silence, `spectrum_producer.h:47`); no morph-only republish; the manual says so. [FR-037, C-18, SC-024, FR-033]
- R4 (plan P-9, §11 Q2) Rule (ii): every eligible carrier the probe finds converts, each for the column of its largest margin, at most one column per preset; `kMorphModConversionRule` = rule (ii); every converted preset passes every Phase 13 gate and every pre-existing column stays covered. [FR-046, SC-018, FR-043]
- R5 (plan P-2, P-4, P-6, P-7, P-8) Accepted as written: P-2 engine byte patch of blob bytes 1–4 with `b`; P-4 + tasks Deviation 7 hard-coded sites move (80 → 89, 288 → 320, 574 → 610, 84 → 93); P-6 confound recorded in the FR-031 1003 row with both arms' `smoothedMass` logged; P-7 three appended driver event kinds (`MorphModulation`, `MorphModWheel`, `MorphModBypass`); P-8 dsp/ fence names the existing test files appended to. [FR-016, C-11, C-12, SC-015, FR-031, C-17, FR-045, FR-019, C-1]
- R6 (tasks Deviation 1) Accepted: the FR-031 read window and precondition window are the D_nl reader's frame `[1.0 s, 1.0 s + W)`, W = 262 144 samples (`bass_morph_test_helpers.h:703-707`); bar unchanged (3 dB). [FR-031, SC-017 (c)]
- Plan review note: C-14's dangling "(FR-064)" corrected to "(FR-044)". [C-14, FR-044]

### Session 2026-10-27 (build ruling)

- R7 (G15 gate, Defects entry) Re-pin the FR-031 1003 row to lo = Off, hi = Velocity, Amount 1, every note at velocity 127, wheel 0, Morph Position 0. The Off → Mod Wheel pin read 1.28 / 1.24 dB against 3 dB because the wheel pushes Mass in the Off arm (C-7); Velocity has no Mass hand-over. Bar unchanged (3 dB); the row stays gated. [FR-031, SC-023 (c)]
- R8 (T026 a1) The A-B endpoint check measures a modulated preset's two designed sounds with the modulation bypassed (A rendered with both endpoints A, as B already is with both endpoints B). With the modulation running, every conversion moved endpoint A toward B before it was measured (Long Tail d 0.76, Poly Spread 1.81, Poly Swell 1.36, Poly Ignition 1.22, Tidal Fold 1.18, Struck Fold 7.72 against bar 10; Rectified Floor level delta 4.5 dB against 3). Every other Phase 13 gate still measures the preset as stored, modulation running. No bar changed. [FR-043]
- R9 (T026 a1) The three added presets get their own designed endpoints instead of Fed Fold's (Wheel Fold read d 0 against Fed Fold at rest, bar 2). They keep their probe rows' Morph Mod settings and must clear FR-041 and FR-043 on their own sounds. [FR-046, FR-041, FR-043]
- R10 (G17, after the re-shard) The SC-007 transparency shards move to the nightly lane (`[long]`, all three OSes): 64 shards, about 15 s each alone, about 15 min of CPU per push. The 610-byte state-format check stays per push in `Profundum_FactoryPresetStateFormat`. [SC-007, FR-060 (a)]
- R11 (G21 comply, SC-014 (a) MOD-POLY 27.18 % and 26.55 % against 25 %, control valid) Optimise the engine; the bar stays and the behaviour is unchanged. A main-loop probe (POLY, the SC-014 pair) read: full workload 25.77 %, modulation without group switches 11.58 %, static A 9.80 %, static B 17.47 %; removing the image switch 14.30 %, the character switch 20.06 %, the field-motion or motion-source switch 25.09 % / 25.30 %. The `dsp/` fence widens to the FR-053 R11 edit set. An ETW profile of that workload (symbolised /O2 copy, 100 s) put `TanhADAA::F1` (std::exp + std::log1p, from Fracture and ShapeMorph ROUND) at ~16 % of engine time and the per-sample Tube re-key (`dbToGain` via `constexprExp`, `log10f`) at ~7 %. Fix: an opt-in `TanhADAA::processFast` whose F1 comes from an 80-segment degree-5 polynomial table in double (|error| ≤ 2.4e-9; its ADAA RMS error against exact double ADAA 1.2e-8–9.2e-8 where `process` reads 1.2e-6–4.8e-4), and linear-gain Tube setters. Paired A/B on the probe: full workload 28.57 → 24.17 %, 26.43 → 22.95 % (F1), then 25.82 → 22.23 %, 29.73 → 24.56 % (F1 + Tube); the load during those pairs was not identified (the CrossDevice service held 27.8 % of the trace's samples, not proven to be the cause). [FR-053, SC-014, FR-019]
- R12 (R11 consumer gate, user 2026-10-27: "Keep fast, fix the measurements") Two checks read the float-ADAA rounding noise that `processFast` removes, on renders within −78.6 dB of the old ones (Fracture, 315 440 samples; max |difference| 3.2e-4); the bars stay. (a) Phase 5 SC-017 (`ShapeCharacter_NonFiniteRecovery`, Fracture): its clean E_high share moved −0.135 → −20.94 dB, so that harmonic set lies ≥ 58 dB under the signal (Fracture's ÷2 energy sits between the f0 harmonics); the burst residue (8.8e-5 at the frame, identical in both builds) then reads 0.115 dB against 0.1. An E_low / E_high delta now counts only where that band holds ≥ `kFloorCountDb` (−40 dB) of the frame's signal power, the same floor `separate()` applies to shares. (b) Phase 9 AT-33 C3 (`Profundum_ParamPathAudibility`): its depth-0 arm read 3.22 dB of frame-wise D_st swing that was noise (now 0.023 dB), so the contrast is 21.80 dB, not 11.24; the Phase 9 reference for that row is re-harvested to the engine-direct reading, every other row matching the pre-R11 exe to 5–6 digits. [FR-053, SC-014, R11]
- R13 (R11 consumer gate, user 2026-10-27: "None, leave to nightly") The 168 Phase 7 `BassStereoField_MonoSumPerBand_*` `[long]` shards reach the R11 code (character stage and ShapeMorph) but cost ~1 600 s each (~75 CPU-hours); they are not run locally for this phase. The nightly long lane (all three OSes) runs them. Every other consumer case of the R11 edit set runs before the commit. [FR-053, R11]
- R14 (SC-008 after R11, user 2026-10-27: "Re-harvest from R11 build") With R11 temporarily reverted (both `processFast` call sites and the Tube linear setters), the four `Profundum_MorphModPrePhaseFingerprints_<k>of4` shards read `failures=0 regionMismatches=0 worstMetric=0 worstSample=0` against the original pre-phase harvest: Phase 14 leaves every pre-existing preset bit-identical. That run is the SC-008 verdict. With R11, four presets read totalVariation 3.0e-4 – 5.2e-4 relative (tolerance 2.5e-4; Rebound, Glide Path, Spread Choir, Restrike), samples ≤ 1.1e-4 (tolerance 5e-4): the ADAA noise R11 removed. `evidence/prephase/fingerprints-<k>.csv` and `prephase-fingerprints.csv` were re-written from the comparison shards themselves (converted presets forced to Source Off, new presets skipped); `decoded-<k>.csv`, `prephase-decoded.csv` and `prephase-claims.csv` keep the pre-phase harvest. [SC-008, FR-060, R11]
