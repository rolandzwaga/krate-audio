# Feature Specification: Profundum Phase 11 — Full Parameter Surface & State

**Spec slug:** `profundum-phase11-parameters`
**Roadmap:** `specs/Profundum-roadmap.md` → Phase 11 (lines 794–809, including the amendments of 2026-10-11, 2026-10-12 and
2026-10-13); Open Questions 2, 8 and 10 (lines 1070, 1081, 1085); the Phase 9 handoff §11.1–§11.6
(`specs/profundum-phase9-voice-engine/evidence/phase-handoffs.txt`); the Phase 10 handoff (roadmap Phase 10 status block and
`specs/profundum-phase10-plugin-scaffold/spec.md:73, :214–246`)
**Depends on:** Phase 10 (`plugins/profundum/` scaffold, 8 parameters, 36-byte state v1) ✅ COMPLETE 2026-10-22
**Status:** DRAFT — specification only, no implementation
**Date:** 2026-10-22 · verified against `synth/profundum` at `565f195d`

---

## Overview

Phase 10 shipped Profundum as a working instrument with eight parameters: output gain, voice mode, polyphony, output ceiling
and the four Personality macros. Every sound-shaping control of the Phase 1–9 engine is still unreachable from a host. The
engine's two Bass Morph endpoints are both `MorphState{}`, so the Morph has no effect (`bass_morph_engine.h:509–510`).

Phase 11 registers, denormalises and persists **every** engine control, and puts the state format under a version bump.
It settles the **morph editing model** on the VST3 surface (roadmap OQ-2): each sound parameter is registered **once**. It
addresses the endpoint named by an **Edit A / Edit B** target, the engine runs the resolved state, and both snapshots live
in the state. It adds:

- Copy A → B / B → A and the A / A·B / B compare, as controller-side operations that are not parameters;
- `IMidiMapping` (mod wheel and channel pressure → two hidden performance parameters that offset Mass at endpoint assembly, pitch bend with a registered range, sustain);
- `getTailSamples()`;
- per-section parameter packs.

Every sound parameter is a 1:1 view of a `MorphState` leaf. Its range, law and enumerator count come from the
`MorphState` field table, which is already the single source of truth for validation, serialisation and morph laws
(`morph_state.h:139–317`). The plugin writes no new range literal for a morphed control. This phase writes **no new DSP**
and modifies **no file under `dsp/`**. The one shared-code change, promoting Vorago's `SustainLatch` to
`plugins/shared/` (FR-061), is a **spec-level scope extension beyond roadmap Phase 11**. The user ratified it
(Clarifications 2026-10-22, Q5); Vorago keeps an alias so its call sites are unchanged.

---

## Clarifications

### Session 2026-10-22

- Q1: What does a MIDI mod wheel / channel pressure move do to Mass (104)? → (c) Mod wheel (CC 1) and channel pressure drive two hidden, non-automatable, non-persisted performance parameters (IDs 6 and 7, Vorago precedent). The processor adds their combined value as a clamped offset to Mass at endpoint assembly, applied to both endpoints so it acts at every morph position. A controller gesture never writes the persisted Mass leaf of either endpoint and never enters saved state; no `dsp/` change. [FR-050, FR-053, FR-055, FR-056, SC-005, SC-008, SC-015, SC-024]
- Q2: Should the controller tell the host to re-read the E-parameters after an Edit Target switch? → (b) After the re-display it calls `restartComponent(kParamValuesChanged)`; no `performEdit` is issued and nothing is recorded as automation; SC-009 asserts the restart call and zero `performEdit`. [FR-022, FR-024, SC-009]
- Q3: What is the source of truth for the controller's endpoint mirror and compare mode? → (b) The processor is authoritative. After any change (edit, MIDI-mapped edit, Copy, `setState`, compare reset) it sends the changed 201-byte endpoint blob and the compare mode to the controller by IMessage / DataExchange from a non-audio thread; the mirror is replaced from those messages, never derived from the controller's own observed edits alone. [FR-021, FR-025, FR-030, FR-031, FR-033, SC-013, SC-014, SC-025]
- Q4: Which names ship for the collision set? → (a) ID 500 "Harmonic Fold"; Character entry "Fold" displays as "Wavefold"; "Bite" only as a Character entry (Shape shows no corner name); Anchor Morph Mode "Hold / Blend / Yield"; Anchor Mode "Lock / Follow / Punch / Ghost"; SC-005 checks the exact strings. [FR-009, FR-001, SC-005, SC-018]
- Q5: Is Vorago's `SustainLatch` promoted to `plugins/shared/`? → (a) Yes, as `Krate::Plugins::SustainLatch` with a Vorago alias; the move is gated as a pure code move (user ruling 2026-10-22): `shared_tests` per-push, the Vorago rebuild at 0 warnings, the two `sustain_test.cpp` cases (`vorago_tests.exe "Vorago_SustainLatchUnit,Vorago_SustainLatch"`), Vorago pluginval and the shared + Vorago clang-tidy. The full `vorago_tests` suite and every `[long]` lane are not run (follow-up ruling 2026-10-22). [FR-061, FR-052, SC-001, SC-002, SC-003, SC-023]
- Q6: How is Edit Target (1001) exposed? → (a) Registered parameter 1001, not `kCanAutomate`, persisted in state (restores which endpoint a reloaded project edits). [FR-004, FR-040, SC-005, SC-010]
- Q7: Does Phase 11 map note velocity? → (a) No; velocity stays unused and no ID is reserved. [FR-054, SC-005]
- Q8: Where do the amp-envelope IDs live? → (c) 420–426, and the roadmap band map is amended to record 400–499 as "drop / note behaviour / amp envelope". [FR-072, SC-026, SC-005]
- Q9 (plan OQ-2): How does the controller mirror stay correct on Linux, where the SDK has no timer? → Pull on Edit Target switch: the controller sends `kMsgRequestSync` before re-displaying and the processor answers from `notify`; a late reply re-displays and restarts again. The 50 ms timer stays on Windows / macOS; on Linux the remaining gap is display-only until Phase 12's editor timer. [FR-025, SC-025]
- Q10 (plan OQ-1): Which p_a does the synthesized pre-render runtime blob carry? → `BassMorphEngine::curve(mode, position)`, what the engine itself holds; no Anchor glide on reload in Hold / Yield. [FR-041, SC-012]
- Q13 (close, SC-011): After a reset the engine's morph-resolved sound settles over ~20 ms, so a note at the start (the first note of an offline bounce) has a different attack. → Fix in `dsp/` as an FR-070 exception. The probe found two defects, not a settle: the idle-voice amp gate ran before the interval's amp setters, and a restore before the first block left the selection transitions to run under the first note; both fixed; gated by dsp_systems_tests [profundum] per-push + profundum_tests; SC-011 stays as written. [FR-074, SC-011, SC-027]
- Q14 (close, SC-020): Identical code reads P / D 0.79–1.18 and the bare engine POLY 21.7–32.5 % across pinned isolated runs; the bars flip on noise. → Tighten the measurement, bars unchanged: 160 x 10-block trials alternated P / D, best per side, plus an identical-engine control arm whose ratio outside 1.05 marks the run INVALID. Second step: 640 trials and a 1.02 control window, since 160 trials left a ~3 % floor (MAX-MONO flipped 0.970 -> 1.056). Third step: the window back to 1.05 (the same path twice reads 0.958-1.026, so 1.02 invalidated most runs); 640 trials kept. [SC-020]
- Q12 (build, T032): check-portability skipped two TUs for a missing include path. → Fix the tool: a profundum branch adding `-I dsp/tests/unit/systems`, and the scope fence allows `tools/check-portability.js`. [FR-073, SC-001]
- Q11 (plan §10): Accept the FR-041 pending-loaded-blob precedence and the FR-006 Hz format? → Both accepted: a loaded, unapplied runtime blob is saved back as loaded; Hz below 1 Hz prints `"%.2f Hz"`, one decimal at 1 Hz and above. [FR-041, FR-006, SC-012, SC-005]

---

## Scope

**In scope**

1. Registration, denormalisation, display formatting, processing and persistence of all 62 `MorphState` leaves. These are
   60 parameter IDs, because each pair of note-value + modifier leaves is one tempo-sync dropdown (FR-012).
2. The 8 non-morphed note-behaviour setters of `ProfundumEngine` (phase-handoffs §11.1) that Phase 10 left unregistered:
   mono priority, legato, portamento time, portamento mode, glide mode, Note Memory mode, Note Memory retrigger and
   pitch-bend range.
3. The morph surface: Morph Position, Morph Ramp Time (roadmap line 805: "registered if Phase 8 exposes it"; it does,
   `profundum_engine.h:312`), Anchor Morph Mode and Edit Target.
4. Four hidden performance parameters, Pitch Bend, Sustain Pedal, Mod Wheel and Channel Pressure, behind `IMidiMapping`. Mod Wheel and Channel Pressure are non-automatable and non-persisted, and offset Mass at endpoint assembly (FR-053, FR-055, FR-056).
5. State version 2. The v1 stream of Phase 10 loads unchanged.
6. The controller mirror of both endpoints, Copy A → B / B → A, and the A / A·B / B compare (roadmap lines 803–805).
7. `getTailSamples()`, deferred here by Phase 10 (`specs/profundum-phase10-plugin-scaffold/spec.md:228`).
8. The seed surface, deferred here by Phase 10 (`…phase10…/spec.md:217`). It is persisted and not registered (FR-043).
9. Tests in `profundum_tests`, plus pluginval strictness 5.

**Non-goals, owned by later phases**

| Deferred to | What |
|---|---|
| Phase 12 (`profundum-phase12-ui`) | Every view and label beyond `getParamStringByValue`, the A / A·B / B and Copy buttons, MIDI learn and the "UI interaction and automation specification" (roadmap line 842). Phase 11 provides the controller operations the buttons will call (FR-030, FR-031) |
| Phase 13 (`profundum-phase13-presets-release`) | Factory presets, preset categories and the preset × capability matrix |
| Not planned | A general modulation matrix (roadmap line 806: "no general matrix"), velocity mapping (ruled out, Clarifications Q7), any new DSP control the engine does not already expose |

---

## Existing components (verified this session)

Paths under `dsp/` are relative to `dsp/include/krate/dsp/`. Every signature below was read at the cited line.

| Component | Header : line | What Phase 11 reuses (real signature) |
|---|---|---|
| `MorphState` | `systems/morph_state.h:43–122` | The plain aggregate holding every sound-shaping control of one patch, in serialised member order: `recipe`, `excitation`, `foldAmount`, `shape`, `characterAlgorithm`, `characterAmount`, `characterTone`, `drop*` ×4, `ampEnv` (7 fields, :71–79), `field` (`BassStereoField::Controls`), `motion` (`MotionConfig`, 16 fields, :81–101), `mass` (:117), `transientAttackDb`, `transientSustainDb`, `anchor` (`AnchorOscillator::Controls`), `anchorFoldFeed`. It is `trivially_copyable` and an aggregate (:123–125). **The registered sound surface is exactly these leaves** |
| Field table | `morph_state.h:56–69, :142–317` | `struct FieldInfo { const char* name; Kind kind; Law law; Axis axis; SelectionGroup group; bool anchorBlock; float lo; float hi; bool hiOpen; float logOffset; std::uint8_t maxEnum; std::uint8_t bytes; }`. `inline constexpr std::array<MorphState::FieldInfo, kMorphLeafFieldCount> kFieldTable` holds 62 rows in member order. It is re-exported publicly as `BassMorphEngine::kLawTable` (`systems/bass_morph_engine.h:74`). **It is the source of every morphed parameter's range, law, log offset and enumerator count** |
| Leaf constants | `morph_state.h:131, :133, :135` | `kMorphStateFormatVersion = 1`, `kMorphLeafFieldCount = 62` ("46 floats, 13 enums, 3 bools"), `kMorphStateBytes = 201` |
| Validation / IO | `morph_state.h:513, :541, :574` | `bool isValidMorphState(const MorphState&) noexcept`; `std::size_t serializeMorphState(const MorphState& s, std::byte* dest, std::size_t capacity) noexcept` returns 201, or 0 having written nothing; `bool deserializeMorphState(const std::byte* src, std::size_t size, MorphState& out) noexcept` leaves `out` untouched on rejection. All three are real-time safe (header comments at each) |
| `BassMorphEngine` | `systems/bass_morph_engine.h:46–528` | `enum class Endpoint : std::uint8_t { A, B }` (:48), `enum class AnchorMorphMode : std::uint8_t { Lock, Blend, Punch }` (:49), `kMinPositionRampMs = 5`, `kMaxPositionRampMs = 2000`, `kDefaultPositionRampMs = 40` (:67–69), `kDefaultAnchorMorphMode = Blend` (:73), `kMorphRuntimeBytes = 20` (:76). `bool setEndpoint(Endpoint, const MorphState&) noexcept` (:113) rejects an invalid state, and storing a bit-identical state is a no-op. `void setPosition(float)` (:140), `void setPositionRampMs(float)` (:155), `void setAnchorMorphMode(AnchorMorphMode)` (:168). `serializeRuntime` / `restoreRuntime` (:235, :274): version, target, ramped p, ramped p_a, mode, ramp, selection mask and Anchor selection. **`restoreRuntime` must follow `setEndpoint`**: "call setEndpoint first" (doc comment above :274) |
| `ProfundumEngine` morph surface | `systems/profundum_engine.h:310–319` | `bool setEndpoint(BassMorphEngine::Endpoint e, const MorphState& s) noexcept`, `void setMorphPosition(float p) noexcept`, `void setMorphRampMs(float ms) noexcept`, `void setAnchorMorphMode(BassMorphEngine::AnchorMorphMode m) noexcept`, `std::size_t serializeMorphRuntime(std::byte* dst, std::size_t cap) const noexcept`, `bool restoreMorphRuntime(const std::byte* src, std::size_t size) noexcept`; read-back `const MorphState& getEndpoint(BassMorphEngine::Endpoint e) const noexcept` (:381) |
| `ProfundumEngine` non-morphed setters | `profundum_engine.h:292, :331–373` | `void setPitchBend(float bipolar) noexcept` ([−1, 1], non-finite keeps), `void setMonoPriority(MonoMode m) noexcept`, `void setLegato(bool on) noexcept`, `void setPortamentoTimeMs(float ms) noexcept` (clamps to `MonoHandler::kMinPortamentoTimeMs … kMaxPortamentoTimeMs` = [0, 10000], `processors/mono_handler.h:111–112`), `void setPortamentoMode(PortaMode m) noexcept`, `void setGlideMode(GlideMode m) noexcept`, `void setNoteMemoryMode(NoteMemory::Mode m) noexcept`, `void setNoteRetrigger(NoteMemory::Retrigger r) noexcept`, `void setPitchBendRange(float semitones) noexcept` ([0, `kMaxBendRangeSt` = 24], :113), `void setSeed(std::uint32_t seed) noexcept` (takes effect at the next prepare / reset, :372–373). An undeclared enum is ignored and a non-finite float keeps the previous value (section comment above :326) |
| Note-behaviour enums | `processors/mono_handler.h:53, :64, :74`; `processors/note_memory.h:24–32` | `MonoMode` {LastNote, LowNote, HighNote}, `PortaMode` {Always, LegatoOnly}, `GlideMode` {ConstantTime, ConstantRate, Quantized} (enumerators per phase-handoffs §11.1); `NoteMemory::Mode` {AlwaysRetrigger = 0, Legato, EnvelopeRetriggerOnly, OscillatorContinuous}, `NoteMemory::Retrigger` {Reset = 0, Continue} |
| Leaf enums | `systems/bass_character_stage.h:78`; `systems/anchor_oscillator.h:57–58`; `systems/bass_stereo_field.h:64–65`; `primitives/lfo.h:47–54`; `morph_state.h:82–84` | `Algorithm` {Tube, Fold, Rectify, Bite, Crush, Fracture}; `AnchorOscillator::Waveform` {Sine, Triangle, RoundedSine, AsymmetricSine, OctaveDownSine}, `Mode` {Lock, Follow, Punch, Ghost}; `ImageMode` {Solid, Spread, Wide}, `MotionSource` {Drift, Cycle}; `Waveform` {Sine, Triangle, Sawtooth, Square, SampleHold, SmoothRandom}; `MotionConfig::Source` {Off, Env, Cycle, Drift}, `Target` {Mass, Depth, Body, Edge, Shift}, `Polarity` {Bipolar, Unipolar} |
| Leaf defaults | `processors/spectral_shape_recipe.h:240–241`; `systems/profundum_core.h:354–358, :1244`; `systems/anchor_oscillator.h:72, :99–106`; `systems/bass_stereo_field.h:72, :99–111`; `processors/drop_envelope.h:36–39`; `processors/transient_shaper.h:82–83`; `systems/mass_law.h:74`; `morph_state.h:71–101` | Recipe 0.5 / 0.5 / 0.5 / 0 / 0.5 / 0; excitation mix 0, drive 0.5, asymmetry 0; Anchor Sine, Lock, level 0.5, punch boost 6 dB, punch 40 ms, ghost 15 ms; field Spread, spread 0.5, gravity 0.5, crossover 120 Hz, curve 0.5, motion depth 0, rate 0.3, source Drift, sync off, Whole / None; drop 0 st, 100 ms, shape 0, rebound 0; transient +6 / 0 dB; Mass 50; amp 10 / 50 / 0.5 / 100 ms, curves 0.7; Motion Off, Mass, amount 0, depth 1, 1 Hz, Sine, sync off, Quarter / None, 0°, retrigger on, fade-in 0, Bipolar, smoothness 0.5, env 10 / 300 ms |
| Tempo-sync dropdown | `core/note_value.h:118–175`; `plugins/shared/src/ui/note_value_ui.h:21`; `plugins/shared/src/ui/parameter_helpers.h:138` | `kNoteValueDropdownCount = 30`, `struct NoteValueMapping { NoteValue note; NoteModifier modifier; }`, `kNoteValueDropdownMapping[]` (every one of the 10 × 3 note/modifier pairs, so the mapping is lossless), `getNoteValueFromDropdown(int)`, `kNoteValueDropdownStrings[]`, `createNoteValueDropdown(title, id, strings, count, defaultIndex)` |
| Parameter helpers | `plugins/shared/src/ui/parameter_helpers.h:47, :80, :85, :118` | `createDropdownParameterWithDefault(title, id, defaultIndex, strings, count)`, `double logMapFromNormalized(double normalized, double mn, double mx)`, `double logMapToNormalized(double units, double mn, double mx)` (both clamp; they need `mn > 0`) |
| Phase 10 packs | `plugins/profundum/src/parameters/global_params.h:110–140`, `macro_params.h:103–184` | The pack contract (`handle…Change`, `register…`, `format…`, `save…`, `load…`, `load…ToController`), the defaults derived from engine constants (P-8, `global_params.h:48–52`) and the 36-byte v1 layout (`plugin_ids.h:20–24`) |
| Phase 10 processor | `plugins/profundum/src/processor/processor.cpp:155–262, :278–305, :312–355` | The render loop with event-delimited slices, last-point-wins parameter handling with a bit-pattern finiteness check, the version-gated `setState` / `getState` that touch atomics only, and `pushParamsToEngine()` once per rendered block |
| `SustainLatch` (Vorago) | `plugins/vorago/src/processor/sustain_latch.h:22–79` | `class SustainLatch` with `noteOn(std::uint8_t)`, `bool noteOff(std::uint8_t)`, `template <typename Release> void setPedal(bool down, Release&&)`, `releaseAll`, `clearWithoutRelease`, `isDown`. It is fixed-size and allocation-free (`std::bitset<128>` ×2). Plugin-local in namespace `Vorago` |
| `IMidiMapping` precedent | `plugins/vorago/src/controller/controller.cpp:280–296` | `getMidiControllerAssignment(int32 busIndex, int16, CtrlNumber, ParamID& id)`: bus 0 only, `kCtrlSustainOnOff` → hidden sustain parameter, `kAfterTouch` → a parameter, otherwise `kResultFalse` |
| SDK MIDI constants | `extern/vst3sdk/pluginterfaces/vst/ivstmidicontrollers.h:28, :52, :104, :105` | `kCtrlModWheel = 1`, `kCtrlSustainOnOff = 64`, `kAfterTouch = 128`, `kPitchBend = 129` |
| Dirty flag | `extern/vst3sdk/pluginterfaces/vst/ivsteditcontroller.h:314` | `IComponentHandler2::setDirty(TBool state)` |
| Engine observer | `systems/profundum_engine.h:252`; `systems/profundum_engine_observer.h:61–77` | `void setObserver(ProfundumEngineObserver* o) noexcept` is "configuration-time" (FR-086 comment above :252). `ProfundumVoiceTaps` carries `coreL, coreR, charL, charR, anchorL, anchorR, …, fRes, fCore, …` spans per voice and chunk (:61–65). `ProfundumEngineTaps` carries `sumL, sumR, dynL, dynR, fieldL, fieldR, dcL, dcR, outL, outR` per chunk (:67–69). The callbacks are `onGrid`, `onCall`, `onVoiceChunk` and `onEngineChunk` (:74–77). **These are the taps the Phase 9 audibility rows read** |
| Macro release scaling | `systems/profundum_macro_matrix.h:85, :193, :218, :260–267` | Row `TightLoose → AmpReleaseOct`, amount 5.0 (:85). `std::array<float, kNumTargets> contributions() const noexcept` is pure (:193). `apply()` calls `scaleTimeClamped(s.ampEnv.releaseMs, at(T::AmpReleaseOct))` (:218), which computes `t' = clamp(t · 2^c, kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs)` and skips `c = ±0` (:260–267). The limits are 0.1 ms and 10000 ms (`primitives/envelope_utils.h:51–52`) |
| FFT | `primitives/fft.h:128, :147, :186` | `class FFT`, `void prepare(size_t fftSize) noexcept` (power of 2 in [256, 8192], allocates), `void forward(const float* input, Complex* output) noexcept`. Used by the test-only band metric below |
| Allocation detection | `tests/test_helpers/allocation_detector.h:48, :70, :111` | `AllocationDetector`, `getAllocationCount()`, `AllocationScope` (as cited by the Phase 10 spec, line 144) |
| Mono-sum bars | `dsp/tests/unit/systems/bass_stereo_field_test_helpers.h:95, 97` | `kSubBandQBarDb = 1.0`, `kSubBandRhoBar = 0.99` (as cited by the Phase 9 spec, line 181) |

**Not reused, with the reason:**

- `plugins/seraphis/src/parameters/morph_params.h:113` `struct MorphParams` is Seraphis's spectral-morph pack. It is a
  different model in another namespace, and its name is avoided (ODR sweep below).
- `VoiceModRouter` is not composed. Phase 9 FR-048 is a recorded deviation; Motion targets are the five fixed
  `MotionConfig::Target` values, as roadmap line 806 says.

---

## New components

All new types are plugin-local in namespace `Profundum`, except the promoted latch. The ODR sweep command was
`grep -rnE "(class|struct|enum class|using) <Name>\b" dsp/ plugins/`, run 2026-10-22 at `565f195d`.

| Type / file | Layer | Path | ODR sweep result |
|---|---|---|---|
| `SoundParamRow` (constexpr row: `ParamID`, leaf index, title, units, list labels) + the ID ↔ leaf lookup and the normalised ↔ plain mapping driven by `BassMorphEngine::kLawTable` | plugin | `plugins/profundum/src/parameters/sound_param_table.h` | 0 hits |
| `EndpointParams` (62 leaf slots as `std::atomic<std::uint32_t>` bit patterns + a dirty flag; one per endpoint) | plugin (processor) | `plugins/profundum/src/parameters/sound_param_table.h` | 0 hits |
| Section packs (row tables + format functions; no type name of their own): core, anchor, note (drop + note behaviour + amp envelope), fold / shape, character, dynamics, field, motion, morph | plugin | `plugins/profundum/src/parameters/{core,anchor,note,fold_shape,character,dynamics,field,motion,morph}_params.h` | file names only |
| `NoteParams` (8 non-morphed note-behaviour atomics) | plugin | `plugins/profundum/src/parameters/note_params.h` | 0 hits |
| `MorphControlParams` (position, ramp, Anchor Morph Mode, Edit Target atomics) | plugin | `plugins/profundum/src/parameters/morph_params.h` | 0 hits (`MorphParams` exists in Seraphis, so this name differs on purpose) |
| `SustainLatch`, promoted to `Krate::Plugins::SustainLatch` (user-ratified, Clarifications Q5) | plugin-shared | `plugins/shared/src/midi/sustain_latch.h` | 1 hit, `plugins/vorago/src/processor/sustain_latch.h:22` (`Vorago::SustainLatch`): **the class being moved**, not a collision. After the move, Vorago's header aliases the shared class and no second definition exists (FR-061). |
| Processor test seams `setEngineObserverForTest`, `sustainDownForTest`, `endpointRejectionsForTest`, `compareModeForTest` (FR-067) | plugin (processor) | `plugins/profundum/src/processor/processor.h` | 0 hits (member functions) |
| `bandLevelDifferenceDb` + `BandLevelReading` (the test-only band metric, see Conventions) | test | `plugins/profundum/tests/band_level_metric.h` | 0 hits |

Other names swept with 0 hits and kept in reserve: `CoreParams`, `AnchorParams`, `FoldShapeParams`, `CharacterParams`,
`DynamicsParams`, `FieldParams`, `MotionParams`, `PerformanceParams`. `AmpEnvParams` exists
(`plugins/ruinae/src/parameters/env_params.h:85`) and must not be used. Roadmap hazard names (`CharacterProcessor`,
`SubOscillator`, `StereoField`, `MorphEngine`, `MorphNode`, `TransientDetector`, `HarmonicCloud`, `SpectralState`,
`MacroMapper`): none is proposed.

---

## Conventions decided in this spec

### Parameter-ID table (permanent once shipped)

Bands follow `plugin_ids.h:46–49`. IDs 0–3 and 100–103 are Phase 10's and are unchanged. "Leaf" is the `kFieldTable` row
name. "E" means the parameter addresses the endpoint selected by Edit Target (FR-020). Every morphed range and default is
read from `kFieldTable` / `MorphState{}`. The values shown here are the verified current ones.

| ID | Title (permanent once shipped) | Leaf / setter | Plain range · law | Default | E |
|---|---|---|---|---|---|
| 4 | Pitch Bend (hidden) | `setPitchBend(2v − 1)` | [−1, 1] linear | 0.5 norm (centre) | – |
| 5 | Sustain Pedal (hidden) | `SustainLatch` | ≥ 0.5 = down | 0 | – |
| 6 | Mod Wheel (hidden, not automatable, not persisted) | Mass offset at endpoint assembly (FR-055) | [0, 1] linear | 0 | – |
| 7 | Channel Pressure (hidden, not automatable, not persisted) | Mass offset at endpoint assembly (FR-055) | [0, 1] linear | 0 | – |
| 104 | Mass | `mass` | [0, 100] linear | 50 | E |
| 105 | Morph Position | `setMorphPosition` | [0, 1] linear | 0 | – |
| 200–205 | Depth · Body · Body Curvature · Body Emphasis · Edge · Spectral Shift | `recipe.*` | [0,1] ×3 · [−1,0] · [0,1] · [−1,1] | 0.5 · 0.5 · 0.5 · 0 · 0.5 · 0 | E |
| 206–208 | Excitation Mix · Excitation Drive · Excitation Asymmetry | `excitation.*` | [0, 1] | 0 · 0.5 · 0 | E |
| 300–302 | Anchor Waveform · Anchor Mode · Anchor Level | `anchor.waveform/mode/level` | list 5 · list 4 · [0, 1] | Sine · Lock · 0.5 | E |
| 303–305 | Punch Boost · Punch Time · Ghost Time | `anchor.punchBoostDb/punchTimeMs/ghostTimeMs` | [0, 12] dB lin · [5, 250] ms log · [1, 100] ms log | 6 · 40 · 15 | E |
| 306 | Anchor Fold Feed | `anchorFoldFeed` | [0, 1] linear | 0 | E |
| 400–403 | Drop Amount · Drop Time · Drop Shape · Drop Rebound | `drop*` | [0, 48] st · [1, 4000] ms log · [0, 1] · [0, 1] | 0 · 100 · 0 · 0 | E |
| 410–417 | Mono Priority · Legato · Portamento · Portamento Mode · Glide Mode · Note Memory · Note Retrigger · Pitch Bend Range | engine setters (§11.1) | list 3 · on/off · [0, 10000] ms, log on (t + 1 ms) · list 2 · list 3 · list 4 · list 2 · [0, 24] st, 25 steps | LastNote · off · 0 · Always · ConstantTime · AlwaysRetrigger · Reset · 2 | – |
| 420–426 | Attack · Decay · Sustain · Release · Attack Curve · Decay Curve · Release Curve | `ampEnv.*` | [0.1, 10000] ms log ×2 · [0, 1] · [0.1, 10000] ms log · [−1, 1] ×3 | 10 · 50 · 0.5 · 100 · 0.7 · 0.7 · 0.7 | E |
| 500–501 | Harmonic Fold · Shape | `foldAmount`, `shape` | [0, 100] · [0, 1] | 0 · 0 | E |
| 600–602 | Character · Character Amount · Character Tone | `characterAlgorithm/Amount/Tone` | list 6 · [0, 1] · [0, 1] | Tube · 0 · 0.5 | E |
| 700–701 | Transient Attack · Transient Sustain | `transientAttackDb/SustainDb` | [−12, 12] dB | +6 · 0 | E |
| 800–806 | Image Mode · Spread · Gravity · Crossover · Curve · Field Motion Depth · Field Motion Rate | `field.*` | list 3 · [0,1] ×2 · [60, 250] Hz log · [0, 1] ×3 | Spread · 0.5 · 0.5 · 120 · 0.5 · 0 · 0.3 | E |
| 807–809 | Field Motion Source · Field Sync · Field Sync Rate | `field.motionSource`, `field.cycleSync`, `field.cycleNote` + `field.cycleModifier` | list 2 · on/off · list 30 (`kNoteValueDropdownMapping`) | Drift · off · 1/1 (index 19) | E |
| 900–903 | Motion Source · Motion Target · Motion Amount · Motion Depth | `motion.source/target/amount/depth` | list 4 · list 5 · [−1, 1] · [0, 1] | Off · Mass · 0 · 1 | E |
| 904–906 | Cycle Rate · Cycle Shape · Cycle Sync | `motion.cycleRateHz/cycleShape/cycleSync` | [0.01, 20] Hz log · list 6 · on/off | 1 · Sine · off | E |
| 907 | Cycle Sync Rate | `motion.cycleNote` + `motion.cycleModifier` | list 30 | 1/4 (index 13) | E |
| 908–910 | Cycle Phase · Cycle Retrigger · Cycle Fade-In | `motion.cyclePhaseDeg/cycleRetrigger/cycleFadeInMs` | [0, 360) deg · on/off · [0, 5000] ms, log on (t + 1 ms) | 0 · on · 0 | E |
| 911–914 | Motion Polarity · Drift Smoothness · Env Attack · Env Decay | `motion.polarity/driftSmoothness/envAttackMs/envDecayMs` | list 2 · [0, 1] · [0.1, 10000] ms log ×2 | Bipolar · 0.5 · 10 · 300 | E |
| 1000 | Anchor Morph Mode | `setAnchorMorphMode` | list 3 | Blend | – |
| 1001 | Edit Target | processor + controller routing | list 2 (A, B) | A | – |
| 1002 | Morph Ramp Time | `setMorphRampMs` | [5, 2000] ms log | 40 | – |

The counts are 60 E-parameters covering all 62 leaves, plus 8 note-behaviour, 4 morph-control and 4 hidden
performance parameters (Pitch Bend, Sustain Pedal, Mod Wheel, Channel Pressure), for **76 new IDs and 84 registered in total**.

The amp envelope sits in band 400–499 ("drop / note behaviour"). The roadmap's band map (lines 762–765) named no envelope
band; FR-072 amends it to record 400–499 as "drop / note behaviour / amp envelope" (user ruling, Clarifications Q8), and the amp envelope is per-note contour like Drop. Mass (104) sits in the macro band as roadmap line 763 says, and is
still a morphed leaf (FR-055).

### State layout, version 2 (`kCurrentStateVersion = 2`)

All values are little-endian `IBStreamer`, as in Phase 10 (`processor.cpp:282`).

| Bytes | Block | Content |
|---|---|---|
| 0–3 | version | `int32` = 2 |
| 4–19 | global (v1) | unchanged from Phase 10 (`global_params.h:14–15`) |
| 20–35 | macros (v1) | unchanged |
| 36–67 | note behaviour | priority `i32`, legato `i32`, portamento ms `f32`, portamento mode `i32`, glide mode `i32`, Note Memory mode `i32`, retrigger `i32`, bend range `f32` |
| 68–83 | morph control | position `f32`, ramp ms `f32`, Anchor Morph Mode `i32`, Edit Target `i32` |
| 84–87 | seed | `uint32` |
| 88–288 | endpoint A | `serializeMorphState` output, 201 bytes |
| 289–489 | endpoint B | 201 bytes |
| 490–509 | morph runtime | `serializeMorphRuntime` output, 20 bytes |

The total is **510 bytes**. Hidden performance parameters (pitch bend, sustain) are never written. This follows the Vorago
precedent, `plugins/vorago/src/plugin_ids.h:28`.

### Band-level difference metric (SC-011, SC-014, SC-022)

`tests/test_helpers/render_fingerprint.h` has no bands. `RenderFingerprint` holds rms, peak, meanAbs, totalVariation and
32 checkpoints (:63–69), and `compareFingerprints` returns a relative metric error and a worst-sample error (:122), not dB
per band. Phase 11 therefore defines a **new test-only helper**, `plugins/profundum/tests/band_level_metric.h`:

- **Channels:** mid M = (L + R) / 2 and side S = (L − R) / 2 of the processor output.
- **Bands (Hz):** [20, 80), [80, 160), [160, 320), [320, 1280), [1280, min(20000, fs/2)). These are five bands with the
  sub split at the 80 / 160 Hz bass octaves the E1–C3 showcase notes occupy.
- **Method:** a Welch power spectrum using `Krate::DSP::FFT` (`fft.h:128`) at size 8192, a Hann window and 50 %
  overlap. Bin power is summed per band, and the level is `10·log10(E + 1e-20)` dB.
- **Aggregate mode** (SC-011, SC-014): one reading over the whole analysis window. The difference is the **max over the
  5 bands × {M, S}** of |ΔdB|. A band is excluded when **both** renders read below −100 dB re full scale in it, so a
  silent band (S in SOLID, for example) cannot dominate through noise-floor ratios.
- **Frame-wise mode** (SC-022): readings per 8192-sample frame with 50 % overlap, and the difference is the max over
  frames × bands × {M, S}.
- **Window:** each SC states its start offset and length. All renders are at 48 kHz.

---

## Functional Requirements

### A. Registration and the parameter table

- **FR-001** The controller registers exactly the 84 parameters of the ID table, with the titles, units, step counts, list
  labels and defaults it gives. IDs 0–3 and 100–103 are unchanged from Phase 10. *(Roadmap line 798: "All engine
  parameters registered".)*
- **FR-002** Every E-parameter's plain range, mapping law, log offset and enumerator count are **derived** from
  `BassMorphEngine::kLawTable[leaf]` (`bass_morph_engine.h:74`). The mappings are:
  - `Linear` and `EqualPower` rows use a linear knob;
  - `Logarithmic` rows use `lo · (hi/lo)^v` via `logMapFromNormalized`, or `(lo + o) · ((hi + o)/(lo + o))^v − o` when
    `logOffset = o > 0`;
  - `Enum8` rows are lists with `maxEnum + 1` entries;
  - `Bool` rows are 2-entry lists.

  A `static_assert` per section pack proves that the pack's rows cover its leaves and that every list's label count
  equals `maxEnum + 1`. No morphed range literal appears in `plugins/profundum/`. *(Roadmap line 798.)*
- **FR-003** Every registered default equals the engine's default bit-for-bit after denormalisation and the cast to the
  leaf type. The E-parameters default to `MorphState{}` and the others to the engine's `Settings` defaults
  (`profundum_engine.h`, `struct Settings`). The Phase 10 per-block push therefore changes nothing on a fresh instance
  (Phase 10 P-8 rule, `global_params.h:48`).
- **FR-004** Parameter flags:
  - Every sound, note-behaviour and morph-control parameter is `kCanAutomate`.
  - Lists are `kIsList`.
  - Pitch Bend and Sustain Pedal are `kCanAutomate | kIsHidden` and are not persisted.
  - Mod Wheel (6) and Channel Pressure (7) are `kIsHidden` only (**not** `kCanAutomate`) and are not persisted.
  - Edit Target (1001) is registered as a parameter, is **not** `kCanAutomate`, and **is persisted in state** (FR-040), so a reloaded project edits the endpoint it was saved on.
- **FR-005** There is one section pack per band, each following the Phase 10 contract (`register`, `handle`, `format`, and
  for non-endpoint packs `save` / `load` / `loadToController`): core 200, anchor 300, note 400 (drop + note behaviour +
  amp envelope), fold / shape 500, character 600, dynamics 700, field 800, motion 900, and morph (104, 105, 1000–1002).
  E-parameter persistence is the endpoint blob (FR-040), never per-pack fields. *(Roadmap line 801: "per-section
  parameter packs".)*
- **FR-006** `getParamStringByValue` formats every continuous parameter in its unit. The rules are:
  - ms below 1000, otherwise s with two decimals;
  - Hz with two decimal places below 1 Hz (`"%.2f Hz"`, e.g. `0.25 Hz`) and one decimal at 1 Hz and above (user ruling 2026-10-22);
  - dB with one decimal and a sign;
  - st with one decimal;
  - plain [0, 1] controls as %;
  - Mass as 0–100;
  - Cycle Phase in degrees.
- **FR-007** **The portamento label follows Glide Mode** (roadmap lines 807–809, Phase 4 FR-027 / SC-027). Portamento
  (412) displays "ms" under ConstantTime (the whole glide), "ms/oct" under ConstantRate and "ms" under Quantized (the whole
  underlying ramp). The value and ID never change with the mode; only the display does. `MonoHandler` has no second glide
  setter.
- **FR-008** Pitch Bend Range (417) is a stepped parameter, [0, 24] st in 1-semitone steps (`kMaxBendRangeSt`,
  `profundum_engine.h:113`), default 2.

- **FR-009** **Names (user ruling, Clarifications Q4; IDs are permanent).**
  - ID 500 is titled **"Harmonic Fold"**.
  - The Character (600) list is, in order, **Tube, Wavefold, Rectify, Bite, Crush, Fracture**: the engine's `Algorithm::Fold` entry displays as "Wavefold", and "Bite" exists only as a Character entry.
  - Shape (501) is a continuous control whose display shows no corner name (no "Bite" or other corner label).
  - Anchor Morph Mode (1000) entries are **Hold, Blend, Yield** (engine `Lock, Blend, Punch`, in that order).
  - Anchor Mode (301) keeps **Lock, Follow, Punch, Ghost**.

### B. Processing (audio thread)

- **FR-010** `processParameterChanges` keeps Phase 10's last-point-wins rule and bit-pattern finiteness check
  (`processor.cpp:309–343`). It routes an ID to its pack in O(1) through one constexpr ID → leaf / handler table. An
  unregistered ID is ignored.
- **FR-011** For an E-parameter, the denormalised plain value is written to the leaf slot of `EndpointParams[target]`,
  where `target` is the Edit Target in effect, and that endpoint's dirty flag is set. **Within one `process()` call, an
  Edit Target change in the same block is applied before every other queue**, whatever the queue order.
- **FR-012** Each Sync Rate parameter (809, 907) writes **two** leaves (note and modifier) from one
  `kNoteValueDropdownMapping` entry (`note_value.h`, `kNoteValueDropdownMapping[]`), so the 30-entry list maps losslessly
  onto the 10 × 3 leaf pairs.
- **FR-013** Once per rendered block, before rendering, for each dirty endpoint: the processor assembles a `MorphState`
  from the 62 slots, adds the Mass performance offset (FR-055) and calls `engine_->setEndpoint(e, state)` (`profundum_engine.h:310`). It then clears the flag. Assembly
  is allocation-free. Its cost is bounded by the 62 leaves (SC-020). A `false` return (an invalid state) leaves the engine's
  endpoint unchanged and is counted in a test-visible diagnostic (`endpointRejectionsForTest()`, FR-067).
- **FR-014** Non-morphed parameters are pushed with the engine's change-only setters every block, extending
  `pushParamsToEngine()` (`processor.cpp:347–355`). The note behaviour uses `setMonoPriority`, `setLegato`,
  `setPortamentoTimeMs`, `setPortamentoMode`, `setGlideMode`, `setNoteMemoryMode`, `setNoteRetrigger` and
  `setPitchBendRange`. The morph control uses `setMorphPosition`, `setMorphRampMs` and `setAnchorMorphMode`. Pitch Bend
  uses `setPitchBend`.
- **FR-015** No engine call in Phase 11 is made from any thread except the audio thread, outside `setupProcessing` /
  `setActive` (Phase 10 rule, `processor.cpp:273–277`).

### C. The morph editing model (roadmap lines 798–801, OQ-2)

- **FR-020** Each sound parameter is registered once and addresses the endpoint selected by Edit Target (1001). The engine
  always runs the state resolved at Morph Position. Both endpoint snapshots are persisted (FR-040).
- **FR-021** The controller keeps a mirror `std::array<Krate::DSP::MorphState, 2>` of both endpoints:
  - it is filled by `setComponentState`;
  - it is replaced from the processor's endpoint messages (FR-025). The processor is authoritative for both endpoint slots, and the mirror is never derived from the controller's own observed edits alone (an observed edit may update the display at once, and the processor's message overrides it).

- **FR-022** When Edit Target changes, the controller re-displays all 60 E-parameters from the newly selected mirror with
  `setParamNormalized`, which does not notify the host. The processor receives only the Edit Target change, and no
  endpoint value changes. Switching the target never changes the sound. After the re-display the controller calls `restartComponent(kParamValuesChanged)` (FR-024).
- **FR-023** Host automation of an E-parameter writes the endpoint selected at the time it plays back. The automation
  model is target-relative, and this consequence is documented in the plugin's `README.md` parameter section.

- **FR-024** After re-displaying the 60 E-parameters on an Edit Target switch, the controller calls `IComponentHandler::restartComponent(kParamValuesChanged)` (when a handler is attached) so the host re-reads its cached values. It issues **no** `beginEdit` / `performEdit` / `endEdit` for those parameters, so nothing is recorded as automation. *(User ruling, Clarifications Q2.)*
- **FR-025** **The processor is authoritative** for both endpoint slots (`EndpointParams`) and the compare mode. After any change (an E-parameter edit, including one that arrives through MIDI-mapped or host automation, Copy A → B / B → A, `setState`, and the compare reset on `setState` / `setActive(false)`), the processor sends the changed endpoint's 201-byte `serializeMorphState` blob and the compare mode to the controller by `IMessage` or DataExchange:
  - the send runs on a **non-audio thread**. The audio thread only sets lock-free change flags and hand-off slots (FR-062) and never calls the host message API;
  - the controller replaces its mirror (FR-021) from the message with `deserializeMorphState`; a rejected blob leaves the mirror unchanged;
  - when the replaced endpoint is the Edit Target, the controller re-displays its 60 E-parameters with `setParamNormalized`, issuing no `performEdit`;
  - the blob carries the persisted slots, never the Mass performance offset (FR-056). A change of Mod Wheel or Channel Pressure alone sends no endpoint message;
  - **pull on Edit Target switch (user ruling 2026-10-22, every OS).** When Edit Target (1001) changes in the controller, it
    sends `kMsgRequestSync` to the processor before re-displaying. The processor's `notify` marks both endpoints and the
    compare mode pending and runs the sender at once, so the re-display uses the processor's slots, never a stale mirror.
    If a reply replaces the Edit Target endpoint after the re-display (a host that delivers messages later), the controller
    re-displays again and calls `restartComponent(kParamValuesChanged)` again (FR-024), so no stale value stays in the host
    cache. Copy needs no pull: the processor copies from its own slots (FR-030);
  - **periodic send.** A 50 ms SDK timer drives the sender on Windows and macOS. On Linux, where the SDK provides no timer, an
    edit the controller does not observe reaches the mirror at the next `notify`, `setState`, `getState` or `setActive`
    call (the Edit Target pull is a `notify`); Phase 12's editor timer closes the remaining display-only gap.

  *(User ruling, Clarifications Q3.)*

### D. Copy and compare (roadmap lines 803–805; not parameters)

- **FR-030** **Copy A → B / B → A** is a controller operation `copyEndpoint(from)`. It:
  1. sends an `IMessage` (`"ProfundumCopyEndpoint"`, attribute `from`) to the processor; the controller does not edit its mirror locally;
  2. calls `IComponentHandler2::setDirty(true)` when the host offers it.

  The processor's `notify()` stores the request in an atomic. The audio thread performs the slot copy at the start of the
  next `process()` and marks the destination dirty. The processor then reports the changed destination endpoint to the controller (FR-025), which replaces its mirror and, if the destination is the Edit Target, re-displays the E-parameters. Copy writes state. It changes neither Edit Target nor Morph Position.
- **FR-031** **A / A·B / B compare** is a controller operation `setCompare(A | AB | B)`, sent as an `IMessage`
  (`"ProfundumCompare"`) and stored by the processor in an atomic. While the mode is A or B, the processor drives
  `setMorphPosition(0)` or `setMorphPosition(1)` in place of the registered position, and the engine's ramp applies. Back
  at A·B, it drives the registered position again. The processor reports the compare mode it holds to the controller (FR-025).
- **FR-032** **Compare never writes state** (roadmap line 804):
  - Morph Position keeps its registered value.
  - `getState` during a compare writes the same morph-control fields as before it.
  - `getState` writes the **last runtime blob published while the compare was A·B**. The audio thread publishes the
    20-byte runtime blob once per block (FR-041), and freezes that publication while the compare is A or B.
- **FR-033** The compare resets to A·B on `setState` and on `setActive(false)`. It is not persisted. The processor sends the reset mode to the controller (FR-025). Both resets are tested (SC-014, SC-025).

### E. State (roadmap line 798: "persisted with `kCurrentStateVersion`")

- **FR-040** `kCurrentStateVersion` becomes 2, and `getState` writes the 510-byte v2 layout above. Both endpoints come from
  the processor's `EndpointParams` slots through `serializeMorphState` (`morph_state.h:541`), never from the engine. An unchanged instance therefore saves identical bytes regardless of the audio thread. Edit Target (1001) is persisted in the morph-control block (bytes 80–83) and restored by `setState`, so a reloaded project edits the endpoint it was saved on (SC-010).
- **FR-041** The runtime blob is produced by `engine_->serializeMorphRuntime` (`profundum_engine.h:314`) on the audio
  thread once per block. It is published lock-free (a sequence counter around five `std::atomic<std::uint32_t>` words) for
  `getState` to read. A runtime blob loaded by `setState` and not yet applied is written as loaded, so load-then-save is
  byte-stable (user ruling 2026-10-22; SC-012 "Pending loaded blob"). Otherwise, before the first rendered block, `getState`
  writes the blob of the registered morph-control values: target = p = position, p_a = `BassMorphEngine::curve(mode,
  position)` (`bass_morph_engine.h:188`; Lock 0, Blend p, Punch p³), every selection A (user ruling 2026-10-22: what the
  engine itself would hold, so a reload in Hold or Yield mode gives no Anchor glide). This synthesized blob is tested
  byte-for-byte (SC-012).
- **FR-042** `setState` touches atomics and lock-free handoff slots only; it makes no engine call (Phase 10 rule). The
  next `process()`, before rendering, applies in this order:
  1. both endpoints, via `setEndpoint`;
  2. the explicit morph-control fields, via `setMorphRampMs`, `setAnchorMorphMode` and `setMorphPosition`;
  3. `restoreMorphRuntime(blob)` (`profundum_engine.h:317`), **only if** the blob's target, ramp and mode equal the
     explicit fields bitwise. Otherwise the blob is ignored and the explicit fields stand.

  The endpoints-first order is the `restoreRuntime` precondition (`bass_morph_engine.h`, doc comment above :274).
- **FR-043** The seed is persisted (4 bytes) and **not registered**. A host knob over a `uint32` seed has no musical
  meaning. The engine applies a seed only at prepare / reset: `setSeed` only stores it (`profundum_engine.h:373`), and
  `reset()` derives the drift seeds from it (:200–215). `prepare` ends in `reset()` (:196). The processor resets the
  engine in exactly two places: `setupProcessing` (`engine_->prepare`, `processor.cpp:133`) and `setActive(false)`
  (`engine_->reset()`, `processor.cpp:148`). **Each of the two calls `engine_->setSeed(seed atomic)` immediately before
  the prepare / reset** (FR-015 allows engine calls there). A seed loaded by `setState` therefore takes effect at the next
  `setupProcessing` or `setActive(false)`. In the host order setState → setupProcessing → setActive(true), the restored
  seed is already in force for the first rendered block. Phase 13 presets may carry a seed through state.
- **FR-044** **Version handling:**
  - A v1 stream (36 bytes) loads its global and macro blocks unchanged. Every Phase 11 field keeps its default, and both
    endpoints stay `MorphState{}`.
  - A version above 2 is refused before any field is read (Phase 10 rule, `processor.cpp:287–289`).
  - A v2 stream cut short applies every field whose bytes were **completely** read before the cut. **Every field after
    the cut keeps its prior in-memory value; nothing is reset to its default.** In particular:
    - a 4-byte field split by the cut is discarded;
    - an endpoint whose 201 bytes are incomplete is unchanged;
    - a missing or short runtime blob is ignored, so FR-042 step 3 is skipped and the explicit morph-control fields that
      were read stand.
  - An endpoint blob that `deserializeMorphState` rejects (`morph_state.h:574`: bad version, bool byte > 1, out-of-box
    value) leaves that endpoint unchanged.
- **FR-045** `Controller::setComponentState` mirrors the stream:
  - it decodes both endpoints into the mirror;
  - it displays the Edit-Target endpoint's 60 E-parameters and every other registered value;
  - it applies the FR-044 rules identically.
- **FR-046** All 76 new parameters are covered by the save / load / mirror paths. The hidden Pitch Bend, Sustain Pedal, Mod Wheel and Channel Pressure
  are reset to centre / up / 0 / 0 on `setState` and are never read from a stream.

### F. MIDI mapping and performance (roadmap lines 801–802)

- **FR-050** The controller implements `IMidiMapping::getMidiControllerAssignment` for bus 0 only, on every channel:
  - `kCtrlModWheel` (1) → Mod Wheel (6);
  - `kAfterTouch` (128, channel pressure) → Channel Pressure (7);
  - `kPitchBend` (129) → Pitch Bend (4);
  - `kCtrlSustainOnOff` (64) → Sustain Pedal (5);
  - any other controller, or bus ≠ 0, → `kResultFalse`.

  This removes the Phase 10 FR-019 exclusion (`controller.h:10`). *(Roadmap line 801: "mod wheel and channel pressure
  → Mass by default, pitch-bend range, sustain"; realised through the performance offset of FR-055.)*
- **FR-051** Pitch Bend's normalised v maps to `setPitchBend(2v − 1)`. The bend's depth in semitones is Pitch Bend Range
  (417) → `setPitchBendRange`.
- **FR-052** Sustain uses the `SustainLatch` of FR-061 (the promoted `Krate::Plugins::SustainLatch`), driven exactly as Vorago drives it:
  - **every** point of the Sustain Pedal queue is applied in order, at its offset, interleaved with note events
    (Vorago FR-030 precedent, `plugins/vorago/src/processor/processor.cpp:726–827`);
  - a note-off while the pedal is down is deferred, and pedal-up releases every latched note whose key is not held;
  - `setActive(false)` calls `releaseAll`, and `setState` / `setupProcessing` call `clearWithoutRelease`.
- **FR-053** Mod Wheel (6) and Channel Pressure (7) are hidden, non-automatable, non-persisted performance parameters (Vorago precedent, `plugins/vorago/src/controller/controller.cpp:292–294`), plain [0, 1] linear, default 0. Mass (104) stays an ordinary E-parameter. A controller gesture (CC 1, channel pressure) **never writes the persisted Mass leaf of either endpoint** and never enters saved state. It needs no `dsp/` change. *(User ruling, Clarifications Q1.)*
- **FR-054** Velocity remains unused (phase-handoffs §11.5). No parameter or mapping consumes it, and no ID is reserved for a future velocity depth: the registered IDs in 400–499 are exactly those of the ID table. *(User ruling, Clarifications Q7.)*
- **FR-055** **Mass performance offset.** At endpoint assembly (FR-013) the processor computes the combined value `c = clamp(modWheel + channelPressure, 0, 1)` and adds `c · (hi − lo)` of the Mass row of `kLawTable` (that is, 100 × c) to the Mass leaf of the **assembled** `MorphState` of **both** endpoints, clamping the result to [lo, hi]. The offset therefore acts at every Morph position. A change of either performance parameter marks both endpoints dirty, so the next block calls `setEndpoint` for both. At `c = 0` the assembled Mass is bit-identical to the persisted leaf.
- **FR-056** **The offset is never persisted.** It is applied to the assembled copy only. The `EndpointParams` Mass slots, the `getState` endpoint blobs (FR-040), the controller mirror and the FR-025 messages carry the persisted Mass leaf only. `getState` bytes do not depend on Mod Wheel or Channel Pressure, and neither parameter is ever read from a stream (FR-046).

### G. Tail, safety, determinism

- **FR-060** `getTailSamples()` returns `ceil((R_eff + 1250) · fs / 1000) + 64`, where R_eff is the **effective** amp
  release after the Personality macros:
  - R = max(releaseMs_A, releaseMs_B), read from the two endpoint slots;
  - c = `ProfundumMacroMatrix::contributions()[AmpReleaseOct]` (`profundum_macro_matrix.h:193`) at the current macro
    atomic values, evaluated on a stack-local matrix (the call is pure and allocation-free);
  - R_eff = R when c = ±0, otherwise `clamp(R · 2^c, kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs)`. This is the same law
    `apply()` uses on the resolved release (`profundum_macro_matrix.h:218, :260–267`).

  Tight↔Loose at 1.0 therefore lengthens the reported tail with the release (100 ms → up to 32×; 5000 ms → capped at
  10000 ms). The morph law places the resolved release between A's and B's, so R bounds every position. The 1250 ms term
  is Phase 10 ruling D-1's **exact-zero** reading of the 5 Hz DC blocker after voice silence (`…phase10…/spec.md:995`),
  and SC-017 asserts exact zero against it. The 64 term is the limiter look-ahead (`profundum_engine.h:107`). The call
  reads the processor's atomics, is valid on any thread, and returns 0 before `setupProcessing`.
- **FR-061** *(Spec-level scope extension beyond roadmap Phase 11, lines 794–809, which ask for sustain via
  `IMidiMapping` only; ratified by the user, Clarifications Q5.)* `SustainLatch` moves verbatim to
  `plugins/shared/src/midi/sustain_latch.h` as `Krate::Plugins::SustainLatch`.
  `plugins/vorago/src/processor/sustain_latch.h` becomes `using SustainLatch = Krate::Plugins::SustainLatch;` in namespace
  `Vorago`, so no Vorago call site changes and no second definition exists. The consumer gates: the move is gated as a pure code move (user ruling 2026-10-22): `shared_tests` per-push, the Vorago rebuild at 0 warnings, the two `sustain_test.cpp` cases (`vorago_tests.exe "Vorago_SustainLatchUnit,Vorago_SustainLatch"`), Vorago pluginval and the shared + Vorago clang-tidy. The full `vorago_tests` suite and every `[long]` lane are not run (SC-002, SC-003, SC-023). *(Reuse-first: the
  latch exists. Phase 10's precedent of re-implementing plugin-local Vorago code (`…phase10…/spec.md:126`) is not
  repeated for a self-contained class.)*
- **FR-062** Real-time safety: no allocation, lock, exception or I/O in `process()` on any Phase 11 path. The paths are
  parameter routing, endpoint assembly, Edit Target, copy, compare, state handoff, runtime-blob publication, pitch bend, sustain, the Mass performance offset and the
  endpoint / compare hand-off to the sending thread.
- **FR-063** Determinism: two processors given the same state bytes and the same event / parameter stream render the
  same output (SC-022).
- **FR-064** Every new parameter reaches the engine and changes its read-back (SC-008). Every band's lever moves the
  rendered sound through the parameter path by at least its Phase 9 audibility bar (SC-018). The bands are the macro
  band's Mass, core, anchor, drop / note behaviour (Drop, glide, amp envelope, Note Memory), fold / shape, character,
  dynamics, field, motion and morph. This is the theme-level audibility FR (roadmap line 1044) applied to the host
  surface.
- **FR-065** Image Mode and the field parameters set through the host keep the Phase 9 full-engine mono-sum bars. The
  Anchor path is bit-identical L == R **in every image mode** (SOLID, SPREAD, WIDE), and SOLID's output is L == R once its
  crossfade has settled (SC-019). This is the theme-level mono FR (roadmap line 1047).
- **FR-066** The Phase 9 and Phase 10 CPU bars still hold through the full parameter surface (SC-020). That includes all
  four Phase 10 arms, MONO, POLY and the user-ruled MONO-MAX / POLY-MAX (all four macros at 1.0; Phase 10 ruling Q-A,
  `…phase10…/spec.md:775–785, :991`), plus an automation-heavy arm. *(Cross-cutting constraint: CPU budgets are FRs.)*
- **FR-067** **Processor test seams.** None is called by `process()`, and all are allocation-free:
  - `void setEngineObserverForTest(Krate::DSP::ProfundumEngineObserver*) noexcept` forwards to `engine_->setObserver`
    (`profundum_engine.h:252`, configuration-time). It may be called only between `initialize` and `setupProcessing`,
    or while inactive. It is the tap seam of SC-015, SC-018 and SC-019: the existing `engineForTest()` is const
    (`processor.h:61`) and cannot attach an observer.
  - `std::uint32_t endpointRejectionsForTest() const noexcept` is the FR-013 diagnostic.
  - `bool sustainDownForTest() const noexcept` returns the latch's `isDown()`.
  - `CompareMode compareModeForTest() const noexcept` returns the FR-031 atomic.

### H. Scope fences

- **FR-070** No file under `dsp/` is modified. If a need for a DSP change appears, it is a stop-and-ask, not an edit.
  **Exception FR-074 (user ruling 2026-10-22).**
- **FR-071** No UI view, uidesc control or preset is added beyond what pluginval needs. The Phase 10 placeholder editor
  stays.
- **FR-072** The roadmap's Phase 10 ID-band map (`specs/Profundum-roadmap.md`) is amended to record 400–499 as "drop / note behaviour / amp envelope", so Phase 12 and 13 do not treat the band as drop-only. The amp-envelope IDs are 420–426 (user ruling, Clarifications Q8). This is a documentation edit and touches no `dsp/` file.
- **FR-074** *(User ruling 2026-10-22, SC-011 close finding; FR-070 exception.)* A running instance and a freshly reset
  or restored one render the same note alike (evidence/run-log/close/sc011-probe.txt; SC-011 read 1.09 dB). Two `dsp/`
  defects were found and fixed, with no change to any output that does not involve them:
  1. **Idle-voice amp gate** (`profundum_voice.h`). `applyTransition` gated the amp envelope at the note event, before the
     interval's amp setters; the ADSR table path latches the attack increment in `enterAttack`, so an idle voice's first
     note played the attack time the voice last applied (any envelope change made while silent: preset, morph,
     Tight↔Loose). The gate is deferred to after the amp setters, or made at once at the declick switch (the G14-3
     pattern for Drop); `audible()` / `active()` / note-memory resolution read the pending gate as active.
  2. **Restore before the first block** (`profundum_engine.h`). A runtime blob restored before any block has rendered
     since prepare / reset re-snaps the dependent state (`reset()` without the morph snap and the event queue), so the
     restored selections do not run their transitions (the image-mode fade) under the first note. Restored mid-stream,
     the engine transitions as for any setter change.

  Gate: `dsp_systems_tests` `[profundum]` per-push (the only consumer TUs of the two headers outside `plugins/profundum`)
  plus `profundum_tests`. Proven by SC-011 as written and by SC-027.
- **FR-073** *(User ruling 2026-10-22, T032.)* `tools/check-portability.js` gets a `profundum` branch in `pluginFlagsFor()`
  that adds `-I dsp/tests/unit/systems`, mirroring `plugins/profundum/tests/CMakeLists.txt:69` (Vorago precedent, same
  function). Without it the two TUs that include `bass_stereo_field_test_helpers.h` are skipped, and SC-001 cannot read
  "no skipped suffix". The T030 scope fence allows exactly this one `tools/` file.

---

## Success Criteria

Every case lives in `profundum_tests` unless named otherwise. The test names are sketches for the plan.

- **SC-001 — Build and portability.** The build has 0 lines matching `warning C[0-9]|warning:` for
  `--target Profundum profundum_tests shared_tests vorago_tests`. `node tools/check-portability.js <every new or changed
  .cpp>` prints `all clear -- N compiled.` with no skipped suffix, and N equals the file count. The macOS and Linux CI legs
  are green (PENDING-CI until push, as in Phase 10).
- **SC-002 — Suites green.** For `profundum_tests.exe`, `shared_tests.exe` and `vorago_tests.exe "Vorago_SustainLatchUnit,Vorago_SustainLatch"` (the two cases that exercise
  the moved class; user ruling 2026-10-22), the last line of `2>&1 | tail -5` reads `All tests passed`. Every SC case name
  below appears in `--list-tests`. (`shared_tests` and the two Vorago cases are here as the consumers of the promoted
  `SustainLatch`; see FR-061. The full `vorago_tests` suite is not run.)
- **SC-003 — pluginval.** `tools/pluginval.exe --strictness-level 5 --validate
  "build/windows-x64-release/VST3/Release/Profundum.vst3"` exits 0, and the same command on `Vorago.vst3` exits 0 (consumer of the promoted latch, FR-061). *(Roadmap line 802.)*
- **SC-004 — auval.** `auval -v aumu Prfd KrAt` reports `AU VALIDATION SUCCEEDED` in macOS CI.
- **SC-005 — Table conformance** (`Profundum_ParamTableConformance`):
  - `getParameterCount() == 84`.
  - For every ID of the table: title, units, `stepCount`, flags (FR-004) and default normalised value match exactly.
  - No registered ID lies outside the documented bands.
  - The IDs and defaults of 0–3 and 100–103 are unchanged from Phase 10.
  - **Names (FR-009).** ID 500 is titled exactly "Harmonic Fold". The Character (600) list reads exactly Tube, Wavefold, Rectify, Bite, Crush, Fracture, and "Bite" appears in no parameter title. Shape (501) `getParamStringByValue` at normalised 0, 0.5 and 1 contains no corner name ("Bite" or other). Anchor Morph Mode (1000) entries read exactly Hold, Blend, Yield. Anchor Mode (301) entries read exactly Lock, Follow, Punch, Ghost.
  - **Flags (FR-004).** Mod Wheel (6) and Channel Pressure (7) carry `kIsHidden` and not `kCanAutomate`. Edit Target (1001) is registered without `kCanAutomate`.
  - **No reserved ID (FR-054).** The registered IDs in 400–499 are exactly {400–403, 410–417, 420–426}; no velocity ID exists.
- **SC-006 — Denorm round trip** (`Profundum_ParamDenormRoundTrip`):
  - For each continuous parameter at 101 evenly spaced normalised points, `toNormalized(toPlain(v))` is within
    **1e-9** of v.
  - Plain values at lo and hi map to exactly 0 and 1.
  - Every list maps index → normalised → index exactly.
  - For each E-parameter, the plain range equals `kLawTable[leaf].lo / hi` bitwise as float. The one exception is
    `motion.cyclePhaseDeg`, whose hi is open.
- **SC-007 — Default identity** (`Profundum_DefaultsMatchEngine`). On a fresh, prepared processor after one block:
  - both `getEndpoint(A/B)` are `leafBitsEqual` to `MorphState{}`;
  - every non-morphed getter (`profundum_engine.h:376–410`) equals its engine default;
  - `endpointRejectionsForTest()` (FR-067) is 0.
- **SC-008 — Wiring, no cross-talk** (`Profundum_EveryParamReachesEngine`). Each of the 76 new IDs is checked against an
  explicit per-ID oracle:
  - **72 engine read-backs.** These are the 60 E-parameters via `getEndpoint(target)` (`profundum_engine.h:381`), the 8
    note-behaviour parameters via their getters (:389–396), Morph Position / Ramp / Anchor Morph Mode via
    `morphPosition()` / `morphRampMs()` / `anchorMorphMode()`, and Pitch Bend via `pitchBend()`. The ID is set to
    normalised 0 and one block is rendered, then it is set to 1 and one block is rendered. The read-back equals lo and hi,
    or the first and last list entry. Each Sync Rate list checks **both** of its leaves against
    `kNoteValueDropdownMapping[0]` and `[29]`. The one exception is Cycle Phase (908), whose hi is open: normalised 1.0
    reads `std::nextafter(360.0f, 0.0f)`. Pitch Bend reads −1 and +1.
  - **Edit Target (1001), via routing.** After 1001 is set from 0 to 1, a following Depth (200) edit lands in endpoint B
    and A is unchanged (the SC-009 oracle, run here too).
  - **Sustain Pedal (5), via SC-015.**
  - **No cross-talk.** For every E-parameter, the other 61 leaves of that endpoint and all 62 leaves of the other endpoint
    are bit-identical before and after.

  The pass count is **72 engine read-backs + 1001 via routing + ID 5 via SC-015 + IDs 6 and 7 via SC-024 = 76 / 76**.
- **SC-009 — Edit Target addressing** (`Profundum_EditTargetAddressing`):
  1. With target A, an edit of Depth changes A only.
  2. Switching to B changes no engine leaf and no render sample, compared with a run that has no switch (bitwise; same
     binary, same process).
  3. A block whose queues hold [Depth = 0.9, Edit Target = B] in that order writes B (FR-011).
  4. In the controller, after the switch, all 60 E-parameter values equal the B mirror's normalised values within 1e-9.
  5. A test `IComponentHandler` attached to the controller records **0** `beginEdit` / `performEdit` / `endEdit` calls
     for the 60 E-parameters across the re-display (FR-022: the re-display does not notify the host).
  6. The same handler records exactly **one** `restartComponent(kParamValuesChanged)` call per Edit Target switch, issued after the re-display (FR-024), and still **0** `performEdit` calls.
- **SC-010 — State round trip, both morph states** (`Profundum_StateRoundTripV2`). Setup:
  - A and B are two distinct seeded-random valid `MorphState`s;
  - every note-behaviour field is non-default;
  - position 0.5 is reached by ramping down from 1 (so the selections are B inside the hysteresis band
    `kThetaDown`–`kThetaUp`, `bass_morph_engine.h:70–71`);
  - Edit Target is B, the seed is 0x1234, and 2 s are rendered.

  Then `getState` → `setState` on a fresh processor (before its `setupProcessing` / `setActive(true)`, the host order) →
  one block → `getState`, and:
  - the two streams are **byte-identical** (510 bytes);
  - the restored engine's endpoints are `leafBitsEqual` to the originals;
  - `morphPosition()` and the restored runtime blob match;
  - `Controller::setComponentState` displays B's values within 1e-9 normalised;
  - the restored Edit Target (1001) reads B: on the restored processor, a following Depth (200) edit lands in endpoint B and A is unchanged (FR-040).

  *(Roadmap line 802: "full round-trip tests including both morph states".)*
- **SC-011 — Restored render continuity** (`Profundum_StateRestoreRender`). The fixture is pinned so that the morph
  runtime blob is the only state that can differ:
  - **Setup.** As SC-010, except for two constraints. The seeded-random endpoints have `motion.source` ∈ {Off, Env} and
    `field.motionDepth` = 0. The reason is that Motion Drift / Cycle phase and field drift advance through the 2 s
    render and are not in the 510-byte state.
  - **Save point.** No voice sounds: every note is released and the FR-060 tail (`getTailSamples()`) has elapsed before
    `getState`.
  - **Restore order.** The fresh processor runs `setState` **before** `setupProcessing` / `setActive(true)`, the host
    order, so the restored seed is in force (FR-043).
  - **Stream.** Both instances then receive an identical stream that begins with **new note-ons** (E1, MONO, held 2 s).
  - **Measure.** The band-level difference (Conventions, aggregate mode) between the restored render and the original's
    continued render is ≤ **0.1 dB**, over the window [100 ms, 2.1 s) after the first new note-on. The selected Character
    algorithm and image mode read back identical through the runtime blob's selection mask
    (`engineForTest()->serializeMorphRuntime`).
  - **Control arm (teeth).** A restored instance whose blob is ignored (FR-042 step 3 forced off) reads its selection
    mask byte from `serializeMorphRuntime` as **all-A**, against the original's all-B. The flip is measured on the mask
    byte, not on audio.
- **SC-012 — Versions and corruption** (`Profundum_StateVersions`):
  - The Phase 10 36-byte v1 stream loads: globals and macros are restored and every Phase 11 field is at its default.
  - Each v2 stream is cut at every byte length 0…509 and loaded into a processor whose prior state is a **non-default**
    v2 state (every field differs from both its default and the cut stream's value). Each load completes without a crash
    or an allocation on the audio thread. After one block, `getState` shows, field by field:
    - every field completely inside the cut has the stream's value;
    - every field after the cut, or split by it, has its **prior** value (FR-044), not the default;
    - an endpoint with fewer than 201 bytes is the prior endpoint;
    - when the runtime blob is short, the explicit morph-control fields that were read stand.
  - **Pre-render blob.** `getState` on a prepared processor that has rendered no block writes, at bytes 490–509, exactly
    the FR-041 synthesized blob (version, target = p = the registered position, p_a = `curve(mode, position)`, mode, ramp,
    selection mask all-A, Anchor selection A), compared byte-for-byte with a blob built from those fields; run in each of
    Lock, Blend and Punch at position 0.7 so p_a = 0, 0.7 and 0.343 respectively.
  - **Pending loaded blob** (FR-041). `setState` of a v2 stream S, then `getState` before any block: bytes 490–509 equal
    S's runtime blob byte-for-byte (load-then-save is byte-stable).
  - Version 3 → `kResultFalse` with no field changed.
  - A flipped endpoint version byte or bool byte > 1 leaves that endpoint at its prior value.
- **SC-013 — Copy** (`Profundum_CopyEndpoint`). After `copyEndpoint(A)` and one block:
  - the processor's B equals A (`leafBitsEqual`) and so does the controller mirror, which was replaced from the processor's destination-endpoint message (FR-030, FR-025) and not copied locally;
  - `setDirty(true)` was observed exactly once on a test component handler;
  - Edit Target and Morph Position are unchanged.

  The same holds for B → A.
- **SC-014 — Compare** (`Profundum_CompareDoesNotWriteState`). At position 0.6, enter compare A and render past the
  40 ms ramp:
  - the band-level difference (Conventions, aggregate mode) against a position-0 reference processor fed the same stream
    is ≤ **0.1 dB** over the window [140 ms, 1140 ms) after entering compare (the 40 ms ramp plus 100 ms settle, then 1 s);
  - `getState` during the compare is **byte-identical** to `getState` taken just before entering it;
  - after A·B, the engine target position returns to 0.6;
  - after `setState`, `compareModeForTest()` reads A·B;
  - in a second arm, after compare B then `setActive(false)`, `compareModeForTest()` reads A·B (FR-033).
- **SC-015 — MIDI mapping** (`Profundum_MidiMapping`):
  - The assignment table is exact on channels 0 and 15: CC 1 → 6, CC 64 → 5, CC 128 → 7, CC 129 → 4. Bus 1 and CC 7 return `kResultFalse`.
  - Pitch Bend at 1.0 with range 12 gives `pitchBend() == 1` and `pitchBendRange() == 12`. The voice-0 `fCore` tap
    (`ProfundumVoiceTaps`, read through `setEngineObserverForTest`, FR-067) rises by 12 st ± 5 cents. The rise is
    12·log2 of the ratio of the tap's mean over [200 ms, 700 ms) after the bend to its mean over the same length before
    it (E1, MONO). The tap is a per-sample frequency, so no spectral estimator is involved.
  - **Sustain: same-binary equivalence**, with max |diff| = **0** in every case:
    - A note-off at 0.5 s with the pedal down from before the note-off until pedal-up at 1.0 s renders identically to a
      pedal-free render whose note-off is moved to the pedal-up sample offset.
    - A note-off with the pedal up renders identically to the plain note-off render.
    - Slicing stays identical in each pair: the reference carries stand-ins at the moved events' offsets (a Sustain
      Pedal point of value 0, and a note-off for a key not sounding).
  - **Multi-point queue (FR-052).** One block carries a Sustain queue [down @ 64, up @ 192, down @ 320] and a note-off
    @ 128. It renders identically (max |diff| = 0) to a reference whose note-off sits at the up offset (192), with the
    same slicing stand-ins. A second note-off @ 384 stays latched until a later pedal-up.
  - **Reset on load (FR-046).** After Pitch Bend 1.0 and pedal down, `setState` and one block give `pitchBend() == 0`
    and `sustainDownForTest() == false`.
  - `getState` bytes do not depend on the pedal or bend state.
- **SC-016 — Portamento label** (`Profundum_PortamentoLabelFollowsGlideMode`). The same normalised value formats with
  suffix "ms", "ms/oct" and "ms" under ConstantTime, ConstantRate and Quantized.
- **SC-017 — Tail** (`Profundum_TailCoversRelease`):
  - `getTailSamples()` equals the FR-060 formula for releases {100, 5000} ms on A / B, at 44.1 / 48 / 96 kHz, with the
    macros neutral (c = 0, R_eff = R).
  - **Macro arm.** With Tight↔Loose (102) at 1.0 and A / B releases {100, 5000} ms, `getTailSamples()` equals the
    formula with c from `contributions()`, and it exceeds the neutral-macro value.
  - In every arm (neutral and Tight↔Loose 1.0, both release pairs, E1, MONO, position 0 and 1), after a note-off
    **every sample beyond `getTailSamples()` is exactly 0.0f** on both channels. This is the D-1 exact-zero premise the
    1250 ms term cites. A non-zero sample is a failure reported to the user; the term is not re-fitted to pass.
- **SC-018 — Audibility through the parameter path** (`Profundum_ParamPathAudibility`, `[long]` if over 15 s). For each
  band, Phase 9 audibility rows are re-run through the **processor**:
  - **Base.** The row's base state (A_s, B, B_s or the per-row setting named in the CSV `base` column) is loaded through
    the host surface, as a v2 `setState` stream holding the base endpoints, or as E-parameter writes with Edit Target A
    then B.
  - **Lever.** The lever is moved **only** by the normalised parameter change named below; no engine setter is called.
  - **Tap.** The descriptor is read from the **same tap** Phase 9 read (the CSV `tap` column), through the
    `setEngineObserverForTest` seam (FR-067, `ProfundumVoiceTaps` / `ProfundumEngineTaps`). The descriptor, note
    (E1 and C3), MONO / voice setting and bar are those of the row's **gated** entries in
    `specs/profundum-phase9-voice-engine/evidence/audibility-table.csv`.
  - **Output column.** The `out_descriptor` column is computed on the processor output as well and recorded, not gated.

  | Band | Row (CSV contrast) | Lever: ID and plain move (other IDs held) | Tap |
  |---|---|---|---|
  | macro: Mass | AT-27 (Mass 0 → 100) | Mass 104: 0 → 100 (norm 0 → 1) | dynamics output and Sum (`dynL/R`, `sumL/R`) |
  | core | AT-01 (Excitation mix 0 → 1, drive 1) | Excitation Mix 206: 0 → 1, with Excitation Drive 207 = 1 and Edge 204 = 1 (gated per-row setting) | Char / twin (`charL/R`) |
  | anchor | AT-10 (level 0 → 1, Sine, LOCK) | Anchor Level 302: 0 → 1 | Sum |
  | anchor PUNCH | AT-11 (LOCK → PUNCH, 6 dB, 40 ms) | Anchor Mode 301: Lock → Punch (index 0 → 2, norm 0 → 2/3), with Anchor Level 302 = 1 | Sum |
  | drop | AT-21 (Drop shape 0 → 1, 24 st) | Drop Shape 402: 0 → 1, with Drop Amount 400 = 24 st | f_core pitch tap (`fCore`, `fRes`, voice 0) |
  | glide | AT-23 (portamento 0 → 200 ms, legato E1 → E2, MONO) | Portamento 412: 0 → 200 ms (offset-log law), Voice Mode 1 = Mono | f_res pitch tap (`fRes`, voice 0) |
  | amp envelope | AT-25 (release 50 → 2000 ms) | Release 423: 50 → 2000 ms | Sum |
  | note memory | AT-43 (AlwaysRetrigger vs EnvelopeRetriggerOnly, Reset, MONO) | Note Memory 415: EnvelopeRetriggerOnly → AlwaysRetrigger (index 2 → 0), Note Retrigger 416 = Reset | core tap (`coreL`, voice 0) |
  | fold | AT-03 (foldAmount 0 → 100) | Harmonic Fold 500: 0 → 100 | Char / twin |
  | shape | AT-02 (SHAPE 0 → 1) | Shape 501: 0 → 1, on the gated per-row recipe (kSawAnchor at edge 0.5 for E1, written through 200–205) | Sum |
  | character | AT-05 (Character Fold amount 0 → 1) | Character Amount 601: 0 → 1, with Character 600 = Wavefold (index 1, engine `Algorithm::Fold`) | Char / twin |
  | dynamics | AT-28 (transientAttackDb 0 → +12) | Transient Attack 700: 0 → +12 dB (norm 0.5 → 1) | dynamics output (`dynL/R`) |
  | field | AT-33 (Field MOTION depth 0 → 1, SPREAD, spread 1) | Field Motion Depth 805: 0 → 1, with Image Mode 800 = Spread and Spread 801 = 1 | field stage (`fieldL/R`) |
  | motion | AT-38 (Off → CYCLE 0.1 Hz, target Shift, amount 1, depth 1) | Motion Source 900: Off → Cycle (index 0 → 2, norm 0 → 2/3), with Target 901 = Shift, Amount 902 = 1, Depth 903 = 1, Cycle Rate 904 = 0.1 Hz | Sum |
  | morph | AT-42 (position 0 → 0.5, showcase pair) | Morph Position 105: 0 → 0.5, endpoints A = A_s, B = B | Char / twin |

  Bars are as in `specs/profundum-phase9-voice-engine/evidence/phase-handoffs.txt` §12.2 and the CSV `bar` column. The
  result is **15 / 15 rows ≥ bar** at every (row, note) pair the CSV gates, written to
  `evidence/param-path-audibility.csv` with the Phase 9 value beside each reading.
- **SC-019 — Mono sum through the host surface** (`Profundum_MonoSumViaParams`, `[long]`). The grid is Image Mode
  SOLID / SPREAD / WIDE × MONO / 4 voices, at E1, with distinct A / B endpoints.
  - **Mode under test on both endpoints.** ID 800 is written with Edit Target A, and again with Edit Target B, so both
    endpoints carry the mode. `field.mode` is a selection-group leaf (`morph_state.h:219`, group ImageMode).
  - **Position.** Position 0.5 is reached by ramping up from 0, so selections stay A inside the hysteresis band
    `kThetaDown`–`kThetaUp` (`bass_morph_engine.h:70–71`).
  - **Mono-sum bars.** Sub-band Q_sub within **1.0 dB** and correlation ≥ **0.99** (Phase 9 SC-015 bars:
    `kSubBandQBarDb`, `kSubBandRhoBar`).
  - **SOLID output L == R.** The output is L == R bitwise from (the start of the block that set the mode +
    ceil(`kModeCrossfadeMs` · fs / 1000) + 64) on. `kModeCrossfadeMs` is 10 ms (`bass_stereo_field.h:91`), the
    crossfade into SOLID (:192, :330–336). The 64 samples are the limiter look-ahead. This is the Phase 9 SC-015 rule:
    from the crossfade end plus the latency.
  - **Anchor L == R in every mode.** In **every** mode, including SPREAD and WIDE, and both voice counts, every voice's
    `anchorL` / `anchorR` tap (`ProfundumVoiceTaps`, through `setEngineObserverForTest`, FR-067) is bit-identical in
    every chunk. This is the Phase 9 SC-016 check, re-run through the host surface.
- **SC-020 — CPU** (`Profundum_ProcessorCpuFullSurface`, hidden `[.perf]`, run alone via `node tools/run-cpu-tests.js
  profundum_tests`). It runs at 48 kHz, 512-sample blocks, with distinct A / B endpoints, position 0.5 and Motion on.
  Thresholds:
  - MONO **≤ 8 %** and 4 voices **≤ 25 %** of one core;
  - **MONO-MAX ≤ 8 %** and **POLY-MAX (4 voices) ≤ 25 %**, the same configuration with all four macros (100–103) at 1.0.
    These are Phase 10's user-ruled gated arms (Q-A, `…phase10…/spec.md:775–785, :991`), carried forward;
  - `process()` / engine-direct ratio **≤ 1.05×** in all four arms (Phase 9 / Phase 10 bars, never relaxed);
  - an automation arm, with one E-parameter change per block alternating endpoints so that `setEndpoint` runs every
    block, meets the same bars at both macro settings (neutral and all at 1.0).

  Every arm's percentage is printed and recorded. A reading over a bar is a failure reported to the user, never a
  recorded number.

  **Measurement (user ruling 2026-10-22, close finding):** pinned isolated runs of identical code read P / D from 0.79 to
  1.18 and the bare engine POLY from 21.7 % to 32.5 % (evidence/run-log/close/cpu-ab.txt), so the original 16 trials of
  100 blocks measured the machine. Bars unchanged; the method is: 640 trials of 10 blocks (user ruling 2026-10-22, second step: 160 left a ~3 % floor), P and D alternated trial by
  trial with the order flipped on odd trials, best per side. The ratio is read on ONE engine instance: P = the
  processor's `process()`, D = that processor's own engine driven directly (`processBlock` plus the same output-gain loop
  and, in the automation arms, the same per-block `setEndpoint`), so memory layout cannot enter the ratio (a first
  control run read two identical separate instances 0.947–1.094 apart: cpu-lane-r3.log). The control times the D path
  twice in the same interleave; the two readings outside [1 / 1.05, 1.05] mark the run **INVALID** (noise floor above the ratio
  bar). A 1.02 window was tried and withdrawn (user ruling 2026-10-22, third step): the same path twice reads 0.958–1.026
  on this machine even with 640 trials (cpu-lane-r5.log, cpu-lane-r6.log), so 1.02 invalidated most runs. An invalid run fails and is re-run alone; it never passes. A separate engine-direct instance is still timed
  and its percentage recorded. Phase 10's `Profundum_ProcessorCpu` (SC-015)
  in the same file uses the same trial shape.
- **SC-021 — No allocation** (`Profundum_Phase11PathsDoNotAllocate`). Under `AllocationScope`, a 64-block run exercises
  every FR-062 path. The allocation count is **0**.
- **SC-022 — Determinism** (`Profundum_Determinism`). Two processors with identical state and streams give a per-sample
  max |difference| of **0** in the same binary and process. Changing the seed and then calling `setActive(false)` / `setActive(true)`
  (the FR-043 reset point) changes a DRIFT-sourced Motion render: Motion Source 900 = Drift, Target 901 = Mass,
  Amount 902 = 1, Depth 903 = 1, E1 MONO, 4 s. The band-level difference (Conventions, frame-wise mode) over [0.5 s, 4 s)
  is ≥ **0.5 dB** in at least one frame and band. The same arm with the seed unchanged reads 0. No golden is compared
  across binaries.
- **SC-023 — Close lane scoped to the change set.** The lane runs:
  - `profundum_tests`, including its `[long]` cases;
  - the `shared_tests` per-push suite and the two `sustain_test.cpp` cases (`vorago_tests.exe "Vorago_SustainLatchUnit,Vorago_SustainLatch"`), the consumers of the moved
    `SustainLatch` (a pure code move, user ruling 2026-10-22; the full `vorago_tests` suite and its `[long]` lanes are not
    run);
  - `Vorago.vst3` pluginval at strictness 5 (SC-003);
  - clang-tidy targets `profundum`, `shared` and `vorago`, with 0 warnings.

  No `dsp/` header is modified (FR-070), so no DSP consumer lane runs.

- **SC-024 — Mass performance offset** (`Profundum_MassPerformanceOffset`):
  - **Arithmetic, both endpoints.** With persisted Mass A = 20 and B = 80 (loaded through `setState`), Mod Wheel (6) = 0.25 and one block: `getEndpoint(A).mass == 45` and `getEndpoint(B).mass == 100` (80 + 25 clamped). Channel Pressure (7) = 0.5 with Mod Wheel 0.25 gives A = 95, B = 100 (c = 0.75). Mod Wheel 0.8 with Channel Pressure 0.7 gives A = 100, B = 100 (c clamped to 1). Each of 6 and 7 alone at 0.5 on a base Mass of 0 reads 50 (the SC-008 oracle for IDs 6 and 7).
  - **Every Morph position.** With both endpoints at Mass 0, Mod Wheel 0 → 1 meets the AT-27 bar of SC-018 (same tap, same descriptor) at Morph Position 0, 0.5 and 1 (settled).
  - **Never written.** After Mod Wheel and Channel Pressure move, the `EndpointParams` Mass slot of each endpoint is bit-identical to before and `getState` is **byte-identical** to the stream taken before the move. Returning both to 0 restores the engine's Mass to the persisted values.
  - **Mirror.** The controller mirror's Mass equals the persisted leaf, not the offset value, and a Mod Wheel / Channel Pressure change alone sends no endpoint message (FR-056, FR-025).
  - **Reset on load (FR-046).** After Mod Wheel 1.0 and Channel Pressure 1.0, `setState` and one block give `getEndpoint(A/B).mass` equal to the persisted values (offset 0).
- **SC-025 — Processor-authoritative sync** (`Profundum_ProcessorControllerSync`), with the processor and controller connected through a test message sink that records each send's thread:
  - **Unobserved edit.** A Depth (200) edit delivered to the processor only (the controller never sees `setParamNormalized` / `performEdit`) leads, after one block and message delivery, to a controller mirror whose leaf equals the processor slot bitwise.
  - **Copy.** After `copyEndpoint(A)` the sink receives one 201-byte message for endpoint B, and the mirror's B equals A bitwise.
  - **setState.** After `setState`, both endpoint blobs (201 bytes each) and the compare mode A·B arrive, and the mirror equals the loaded endpoints bitwise.
  - **Compare reset.** After compare B then `setActive(false)`, a compare-mode message A·B arrives and the controller's compare mode reads A·B.
  - **Edit Target pull** (FR-025, every OS, no timer involved). With the controller mirror stale (a Depth edit to B delivered
    to the processor only, no flush), `setParamNormalized(1001, 1.0)` on the controller sends exactly one `kMsgRequestSync`;
    the processor replies with both endpoints and the compare mode before the re-display, and the re-displayed Depth equals
    the processor's B slot (leaf bitwise). A delayed-reply arm (the recorder holds the reply until after the re-display)
    shows a second re-display and a second `restartComponent(kParamValuesChanged)`, and the final displayed values equal the
    slots.
  - **Thread.** Every send is made on a thread other than the one that calls `process()`; the audio thread never calls the message API, and the SC-021 allocation count stays 0 with the sync enabled.
  - **Mass offset.** A Mod Wheel / Channel Pressure change alone produces zero endpoint messages.
- **SC-027 — Running vs reset / restored** (FR-074, `dsp_systems_tests`, tag `[fr-074]`):
  - `VoiceEngine_IdleVoiceFirstNoteUsesCurrentAmpEnvelope`: an engine prepared with the 10 ms default attack, given a
    500 ms attack while idle, then a note: first-100 ms level within **0.1 dB** of an engine prepared with 500 ms.
    Before the fix: −9.58 dB against −75.54 dB (the teeth).
  - `VoiceEngine_RestoreBeforeFirstBlockLandsSettled`: Solid → Wide (spread 1), Tight↔Loose 0.8, Character and Anchor
    selections differing; an engine restored from a settled original's blob before its first block plays the note's
    first 200 ms side energy within **0.1 dB** of the original. Before the fix: −33.29 against −33.98 dB (the teeth); the
    pre-restore Character selection is asserted A.
- **SC-026 — Band-map amendment** (FR-072). `grep -n "drop / note behaviour / amp envelope" specs/Profundum-roadmap.md` has a hit in the Phase 10 ID-band map, and that map no longer lists 400–499 as drop-only.

---

## Edge cases

- **Edit Target and edits in the same block:** the target applies first (FR-011). A target change with no other edit
  changes no sound (SC-009).
- **Host re-sends every parameter after a state load:** the values land on the selected endpoint after a normalised round
  trip. Bounded by SC-006 (1e-9 normalised) and inaudible.
- **Host cache after an Edit Target switch:** the controller calls `restartComponent(kParamValuesChanged)` (FR-024), so the host re-reads the newly displayed endpoint's values instead of re-sending stale ones. No automation is recorded.
- **Wheel or pressure at rest:** Mod Wheel / Channel Pressure at 0 add a Mass offset of 0, write nothing, and never touch the persisted Mass leaf (FR-055, FR-056). A pressure release therefore cannot clobber a saved patch.
- **Copy while comparing:** both requests are honoured at the next block, copy first. Compare still writes no state, but
  the copy does.
- **Compare during a morph ramp:** the engine ramps toward 0 / 1 at the registered ramp time. The registered position is
  untouched.
- **Sample-rate change:** `setupProcessing` re-prepares the engine (Phase 10). Endpoints and every pending value survive
  (`profundum_engine.h:144–146`). The sustain latch clears without release, and the tail is recomputed for the new fs.
- **Seed:** a seed loaded mid-session takes effect at the next prepare / reset only (FR-043). The render is unchanged
  until then.
- **Parameter extremes:**
  - Cycle Phase is `hiOpen` [0, 360). Normalised 1.0 maps to the largest float below 360, so it never fails
    `isValidMorphState`.
  - Portamento and Cycle Fade-In use the offset-log law, so normalised 0 is exactly 0 ms.
  - `−0.0f` passes `[0, x]` boxes (`morph_state.h:512`).
- **Invalid assembled endpoint:** impossible through the clamped mappings. If it happens, `setEndpoint` returns `false`,
  the engine keeps the previous endpoint, and the diagnostic counts it (SC-007 expects 0).
- **v1 project opened:** sounds exactly as in Phase 10, because both endpoints are `MorphState{}` and Morph has no effect
  until edited.
- **Legato vs Note Memory "Legato":** two existing, independent engine controls (MonoHandler legato,
  `setLegato`, and `NoteMemory::Mode::Legato`). Both are registered as they are, and only the titles distinguish them.
- **RT boundaries:** `notify()`, `setState`, `getState`, `getTailSamples` and the controller operations run off the audio
  thread and touch only atomics and handoff slots. The audio thread never waits on them.

---

## Traceability

| Roadmap statement (line) | FR | SC |
|---|---|---|
| All engine parameters registered / denormalized / persisted with `kCurrentStateVersion` (798) | FR-001–FR-008, FR-010–FR-014, FR-040–FR-046 | SC-005–SC-008, SC-010, SC-012 |
| Morph editing model, Edit A / Edit B, both snapshots in state (798–801, OQ-2) | FR-020–FR-025, FR-040 | SC-009, SC-010, SC-011, SC-025 |
| Per-section parameter packs (801) | FR-005 | SC-005 |
| `IMidiMapping`: mod wheel + pressure → Mass, pitch-bend range, sustain (801–802) | FR-050–FR-056 | SC-015, SC-024 |
| *(Spec-level scope extension beyond roadmap Phase 11, user-ratified)* Promote Vorago's `SustainLatch` to `plugins/shared/` | FR-061 | SC-001, SC-002, SC-023 (Vorago / shared lanes) |
| pluginval + full round-trip incl. both morph states (802) | FR-040–FR-045 | SC-003, SC-010, SC-011 |
| Copy / compare controller-side, compare writes no state (803–805) | FR-030–FR-033 | SC-013, SC-014, SC-025 |
| Morph ramp time registered if Phase 8 exposes it (805) | ID 1002, FR-014 | SC-008 |
| Motion target set: Mass + core spectral, no general matrix (805–806) | IDs 900–914 | SC-008, SC-018 |
| `anchorFoldFeed` in 300–399 (806–807) | ID 306 | SC-008 |
| Portamento label per Glide Mode (807–809) | FR-007 | SC-016 |
| OQ-10 name collisions (1085) | FR-009 | SC-005 (titles) |
| Velocity unused, no reserved ID (Phase 9 handoff §11.5) | FR-054 | SC-005 |
| Band map: amp envelope in 400–499 (762–765) | FR-072 | SC-026 |
| Audibility / mono / CPU theme FRs (1044, 1047, cross-cutting constraints) | FR-064–FR-067 | SC-018–SC-020 |
| Phase 10 handoffs: tail, seed (Phase 10 spec lines 217, 228) | FR-060, FR-043 | SC-017, SC-022 |

---

## Review notes

- **Field lever ID.** One review item suggested ID 806 for AT-33. 806 is Field Motion **Rate** in the ID table, and the
  AT-33 contrast moves `motionDepth`, which is ID **805** (Field Motion Depth). The table uses 805.
- **SC-018 row count.** One review item proposed 14 rows. The table has 15: the fold / shape band keeps both its levers,
  Harmonic Fold (AT-03, 500) and Shape (AT-02, 501). AT-02 moves Shape, not a core-band ID, so the core band gets AT-01
  (Excitation Mix 206) as the review's band-mapping item asks.
- **SC-022 metric.** The original "≥ 0.5 dB in at least one band" had no defined unit. A drift trajectory change can
  average out over a whole render, so SC-022 uses the frame-wise mode of the new band metric. The threshold stays
  0.5 dB, and a same-seed null arm is added.
- **FR-060 / SC-017.** Of the two offered resolutions, this revision keeps the 1250 ms term and tightens SC-017 to
  exact zero. It does not lower the term to D-1's 500 ms ≤ 1e-6 reading, so no threshold is relaxed.
