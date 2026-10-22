# Implementation Plan: Profundum Phase 11 — Full Parameter Surface & State

**Spec:** `specs/profundum-phase11-parameters/spec.md` (reviewed; Clarifications Q1–Q8, 2026-10-22)
**Roadmap:** `specs/Profundum-roadmap.md` Phase 11 (lines 794–809)
**Branch:** `synth/profundum` (verified with `git branch --show-current` this session)
**Date:** 2026-10-22
**Status:** PLAN — no code written

Every `file:line` below was opened and read in this planning session. Paths under `dsp/` are relative to
`dsp/include/krate/dsp/` unless they start with `dsp/tests/`.

---

## 0. Summary

Phase 11 writes **no DSP** and touches **no file under `dsp/`** (FR-070). It is plugin work in `plugins/profundum/`, plus one
user-ratified move of shared code (`SustainLatch`, FR-061). The design rests on one fact verified in `systems/morph_state.h`:
the 62 `MorphState` leaves already have one constexpr descriptor table, a compile-time visitor and a validated serialiser.
Every morphed parameter is therefore a *view* of `kFieldTable[leaf]`. The plugin stores each endpoint as 62 atomic 32-bit
leaf words and rebuilds a `MorphState` with the existing visitor.

| Area | What changes |
|---|---|
| `plugins/shared/src/midi/` | **New** `sustain_latch.h` (`Krate::Plugins::SustainLatch`, moved verbatim) |
| `plugins/vorago/src/processor/sustain_latch.h` | Becomes an alias header (no call-site change) |
| `plugins/profundum/src/plugin_ids.h` | 76 new IDs, state v2 constants, message IDs, band-end constants |
| `plugins/profundum/src/parameters/` | **New** `sound_param_row.h`, `sound_param_table.h`, nine section packs, `performance_params.h` |
| `plugins/profundum/src/processor/` | Routing, endpoint slots and assembly, copy / compare, state v2, runtime-blob seqlock, sustain, tail, controller sync |
| `plugins/profundum/src/controller/` | 84-parameter registration, mirror, Edit Target re-display, copy / compare operations, `IMidiMapping`, `notify` |
| `plugins/profundum/tests/` | 12 new TUs, 5 amended Phase 10 TUs, `band_level_metric.h`, `param_path_audibility_helpers.h`, fixture additions |
| Docs | Roadmap band map (FR-072), plugin `CLAUDE.md`, `README.md` (FR-023), `CHANGELOG.md` 0.1.0 bullets |

---

## 1. Verified reuse (real signatures, read this session)

### 1.1 `MorphState` and its table — `systems/morph_state.h`

- `struct MorphState` (:43–122) is a plain aggregate; `static_assert(std::is_trivially_copyable_v<MorphState>)` (:123).
  Nested types:
  - `enum class Law : std::uint8_t { Linear, Logarithmic, EqualPower, Stepped }` (:45);
  - `enum class Kind : std::uint8_t { F32, Enum8, Bool }` (:47);
  - `struct FieldInfo { const char* name; Kind kind; Law law; Axis axis; SelectionGroup group; bool anchorBlock; float lo;
    float hi; bool hiOpen; float logOffset; std::uint8_t maxEnum; std::uint8_t bytes; }` (:56–69).
- `kMorphStateFormatVersion = 1` (:131), `kMorphLeafFieldCount = 62` (:133), `kMorphStateBytes = 201` (:135).
- `detail::morph_state_io::kFieldTable` (:142) holds 62 rows in member order. The leaf order, read from the visitor
  (:336–399), is:
  - 0–5 `recipe.{depth, body, bodyCurvature, bodyEmphasis, edge, shift}`;
  - 6–8 `excitation.{mix, drive, asymmetry}`;
  - 9 `foldAmount`, 10 `shape`;
  - 11–13 `character{Algorithm, Amount, Tone}`;
  - 14–17 `drop{AmountSt, TimeMs, Shape, Rebound}`;
  - 18–24 `ampEnv.{attackMs, decayMs, sustain, releaseMs, attackCurve, decayCurve, releaseCurve}`;
  - 25–35 `field.{mode, spread, gravity, crossoverHz, curve, motionDepth, motionRate, motionSource, cycleSync, cycleNote,
    cycleModifier}`;
  - 36–51 `motion.{source, target, amount, depth, cycleRateHz, cycleShape, cycleSync, cycleNote, cycleModifier,
    cyclePhaseDeg, cycleRetrigger, cycleFadeInMs, polarity, driftSmoothness, envAttackMs, envDecayMs}`;
  - 52 `mass`, 53–54 `transient{Attack, Sustain}Db`;
  - 55–60 `anchor.{waveform, mode, level, punchBoostDb, punchTimeMs, ghostTimeMs}`, 61 `anchorFoldFeed`.
- `template <class F, class... S> constexpr void forEachMorphLeaf(F&& f, S&... s)` (:334) calls `f(Leaf<i>{}, s.<leaf i>...)`
  for i = 0..61, where `template <std::size_t I> using Leaf = std::integral_constant<std::size_t, I>;` (:330).
  `static_assert(leavesMatchTable())` (:440) proves the visitor matches the table.
- Row-shape invariants (`static_assert`, :448 onward): a `Stepped` row is exactly a non-F32 row; `Bool` rows have
  `maxEnum == 1`; a `Logarithmic` row has `lo + logOffset > 0`.
- `[[nodiscard]] inline bool leafBitsEqual(const MorphState& a, const MorphState& b) noexcept` (:484, in `detail::morph_state_io`).
- `[[nodiscard]] inline bool isValidMorphState(const MorphState& s) noexcept` (:513) checks each leaf's box (−0 passes
  `[0, x]`, `hiOpen` is honoured) **and** `BassStereoField::isValidControls(s.field)`. That function
  (`systems/bass_stereo_field.h:263–270`) checks only the same per-field boxes.
  **Consequence used by this plan:** a `MorphState` whose leaves are each inside their own box is always valid. Any
  combination of slot values the plugin can write is therefore valid.
- `serializeMorphState(const MorphState& s, std::byte* dest, std::size_t capacity) noexcept` (:541) returns 201, or 0.
- `deserializeMorphState(const std::byte* src, std::size_t size, MorphState& out) noexcept` (:574) leaves `out` untouched when
  it rejects a stream (bad version byte, a bool byte > 1, an invalid state).

### 1.2 `BassMorphEngine` — `systems/bass_morph_engine.h`

- `enum class Endpoint : std::uint8_t { A, B };` (:48) and `enum class AnchorMorphMode : std::uint8_t { Lock, Blend, Punch };` (:49).
- Constants:
  - `kMinPositionRampMs = 5`, `kMaxPositionRampMs = 2000`, `kDefaultPositionRampMs = 40` (:67–69);
  - `kThetaUp = 0.52f`, `kThetaDown = 0.48f` (:70–71);
  - `kDefaultAnchorMorphMode = Blend` (:73);
  - `static constexpr const auto& kLawTable = detail::morph_state_io::kFieldTable;` (:74);
  - `kMorphRuntimeFormatVersion = 1` (:75), `kMorphRuntimeBytes = 20` (:76).
- `bool setEndpoint(Endpoint e, const MorphState& s) noexcept` (:113) rejects an invalid state. Storing a bit-identical state
  is a no-op. Otherwise it refreshes the log cache, "11 std::log2 calls" (:111).
- `static float curve(AnchorMorphMode m, float p) noexcept` (:188): Lock gives 0, Blend gives p, Punch gives p³.
- The `serializeRuntime` (:235) layout uses **private** offsets (:340–347). The layout is:
  - version at byte 0 (u8);
  - target at 1, p at 5 and p_a at 9 (f32 each);
  - mode at 13 (u8);
  - ramp at 14 (f32);
  - main mask at 18 (u8; bits Character 0, ImageMode 1, FieldMotion 2, Motion 3);
  - Anchor selection at 19 (u8).
- `restoreRuntime` (:274) rejects the blob if any of these holds:
  - version ≠ 1;
  - a position that is non-finite, outside [0, 1], −0, or 0 < |x| < 1e-15;
  - a ramp outside [5, 2000];
  - mode > Punch, mask > 0x0F, or Anchor byte > 1;
  - **a selection that contradicts the FR-034 rule for the current endpoints** (`ruleSelection`, :394). Its doc comment says
    "call setEndpoint first".

### 1.3 `ProfundumEngine` — `systems/profundum_engine.h`

- **Constants:** `kMaxVoices = 8` (:98), `kLimiterLookaheadSamples = 64` (:107), `kDefaultBendRangeSt = 2.0f` (:112),
  `kMaxBendRangeSt = 24.0f` (:113), `kDefaultSeed = 0xB455u` (:114).
- **Lifecycle:**
  - `void prepare(double sampleRate, const ProfundumEngineConfig& cfg = {}) noexcept` (:147) seeds the Motion and field
    drift from `seed_` and ends in `reset()` (:196).
  - `void reset() noexcept` (:201) re-derives every seed from `seed_` and keeps "the pending setter values, macros, endpoints
    and morph target".
  - `void setObserver(ProfundumEngineObserver* o) noexcept` (:252) is configuration-time.
- **Pitch bend:** `void setPitchBend(float bipolar) noexcept` (:292) clamps to [−1, 1]; a non-finite value is kept out.
- **Morph surface (:310–319):**
  - `bool setEndpoint(BassMorphEngine::Endpoint e, const MorphState& s) noexcept`;
  - `void setMorphPosition(float p) noexcept`, `void setMorphRampMs(float ms) noexcept`;
  - `void setAnchorMorphMode(BassMorphEngine::AnchorMorphMode m) noexcept`;
  - `std::size_t serializeMorphRuntime(std::byte* dst, std::size_t cap) const noexcept`;
  - `bool restoreMorphRuntime(const std::byte* src, std::size_t size) noexcept`.
- **Non-morphed setters (:331–373):**
  - `setMonoPriority(MonoMode)`, `setLegato(bool)`;
  - `setPortamentoTimeMs(float)`, which clamps to `MonoHandler::kMinPortamentoTimeMs … kMaxPortamentoTimeMs` = [0, 10000]
    (`processors/mono_handler.h:111–112`);
  - `setPortamentoMode(PortaMode)`, `setGlideMode(GlideMode)`, `setNoteMemoryMode(NoteMemory::Mode)`,
    `setNoteRetrigger(NoteMemory::Retrigger)`;
  - `setPitchBendRange(float)`, which clamps to [0, 24] (:358–362);
  - `setSeed(std::uint32_t)` (:373): "stored; takes effect at the next prepare / reset".
- **Read-backs (:379–397):** `pitchBend()`, `getEndpoint(Endpoint)` (:381), `morphPosition()` (the target position),
  `morphRampMs()`, `anchorMorphMode()`, `monoPriority()`, `legato()`, `portamentoTimeMs()`, `portamentoMode()`, `glideMode()`,
  `noteMemoryMode()`, `noteRetrigger()`, `pitchBendRange()`.
- **Defaults:** `struct Settings` (:460–474) is **private**. Its defaults are LastNote, legato false, 0 ms, Always,
  ConstantTime, AlwaysRetrigger, Reset, bend 0 and bend range `kDefaultBendRangeSt`. The note pack re-states them, and SC-007
  pins them through the public getters.
- **Enums:**
  - `MonoMode` (`processors/mono_handler.h:53`), `PortaMode` (:64), `GlideMode` (:74);
  - `NoteMemory::Mode {AlwaysRetrigger = 0, Legato = 1, EnvelopeRetriggerOnly = 2, OscillatorContinuous = 3}` and
    `NoteMemory::Retrigger {Reset = 0, Continue = 1}` (`processors/note_memory.h:24–32`).

### 1.4 Observer taps — `systems/profundum_engine_observer.h`

- `ProfundumVoiceTaps` (:61–66): `coreL, coreR, charL, charR, anchorL, anchorR, gAmp, gD, fRes, fCore, …`.
- `ProfundumEngineTaps` (:67–70): `sumL, sumR, dynL, dynR, fieldL, fieldR, dcL, dcR, outL, outR`.
- Virtual callbacks `onGrid / onCall / onVoiceChunk / onEngineChunk` (:74–77).
- `ProfundumGridRecord::afterMacros` is "a copy kept by the engine only while an observer is attached" (:55). The CPU arms
  therefore never attach an observer.

### 1.5 Macro matrix — `systems/profundum_macro_matrix.h`

- `class ProfundumMacroMatrix` (:65), `kNeutral = 0.5f` (:67), `void setMacros(const ProfundumMacroValues&) noexcept` (:175).
- `std::array<float, kNumTargets> contributions() const noexcept` (:193) is pure.
- `apply()` (:204) scales the release with `scaleTimeClamped(s.ampEnv.releaseMs, at(T::AmpReleaseOct))` (:218). That
  function (:260–270) computes `v = t * std::exp2(c)` in **float**. It skips the change when
  `isNonZero(c)` is false (:245: `(bit_cast(c) & 0x7FFFFFFF) == 0`), and clamps to `[kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs]`
  = [0.1, 10000] (`primitives/envelope_utils.h:51–52`).
- The row `TightLoose → AmpReleaseOct` has amount 5.0 (:85).
- `MassLaw::Targets` (`systems/mass_law.h:82–90`) has no release target, so Mass never lengthens the tail.

### 1.6 Tempo-sync dropdown — `core/note_value.h`

- `NoteValue` (:38–49, `QuadrupleWhole = 0 … SixtyFourth`) and `NoteModifier {None, Dotted, Triplet}` (:54–58).
- `kNoteValueDropdownCount = 30` (:123) and `kNoteValueDropdownMapping[]` (:136). Index 13 is {Quarter, None}; index 19 is
  {Whole, None}.
- The field defaults `cycleNote = Whole, cycleModifier = None` (`bass_stereo_field.h:109–110`) give index 19. The Motion
  defaults `Quarter / None` (`morph_state.h:92–93`) give index 13.
- All 10 × 3 pairs appear once, so (note, modifier) → index is a bijection.

### 1.7 Plugin and SDK

- **Phase 10 packs:**
  - `global_params.h` has the contract handle / register / format / save / load / loadToController, and `kDefault*`
    constants derived from engine constants;
  - `macro_params.h:103–182`;
  - `plugin_ids.h:19–24` has `kCurrentStateVersion = 1` and `kStateV1Bytes = 36`.
- **Phase 10 processor (`processor.cpp`):**
  - `setupProcessing` :111–142 (prepare :132, `pushParamsToEngine` :138);
  - `setActive` :145–152 (`engine_->reset()` :149);
  - `process` :155–262 (slice loop :239–258);
  - `setState` :278–294 and `getState` :296–305;
  - `processParameterChanges` :312–343 (last point, `detail::isFinite`, clamp, band routing);
  - `pushParamsToEngine` :347–355 and `dispatchEvent` :393–417.
- **Phase 10 `processor.h`:** ":58 getTailSamples(): NOT overridden"; `engineForTest()` is const (:61);
  `static_assert(sizeof(Processor) < 64u * 1024u)` (:91).
- **Phase 10 controller:** no `IMidiMapping` (`controller.h:10, :24`); `setComponentState` :58–74;
  `getParamStringByValue` :76–87.
- **Shared helpers (`plugins/shared/src/ui/parameter_helpers.h`):**
  - `createDropdownParameterWithDefault(title, id, defaultIndex, strings, count)` (:118–131) sets only the current value,
    not `defaultNormalizedValue`;
  - `logMapFromNormalized` (:80–83) and `logMapToNormalized` (:85–88);
  - `kNoteValueDropdownStrings[]` (`plugins/shared/src/ui/note_value_ui.h:21`).
- **`Vorago::SustainLatch`** (`plugins/vorago/src/processor/sustain_latch.h:22–79`):
  - API: `noteOn(uint8)`, `[[nodiscard]] bool noteOff(uint8)`, `template <typename Release> void setPedal(bool down,
    Release&&)`, `releaseAll(Release&&)`, `clearWithoutRelease()`, `isDown()`;
  - storage: `std::bitset<128>` ×2;
  - consumers: Vorago `processor.h:34, :147, :313`, `vorago/CMakeLists.txt:34`, and
    `vorago/tests/integration/sustain_test.cpp:14, :53`.
- **Vorago pedal drive (`plugins/vorago/src/processor/processor.cpp`):**
  - `collectPedalPoints` (:803–829) keeps every point, skips non-finite values, clamps, sets `down = value >= 0.5`, and
    coalesces past `kMaxPedalPoints = 128`;
  - the slice loop has a pedal cursor (:419–447, "equal offsets, notes first");
  - early returns call `applyPendingPedalPointsImmediately` (:839–846);
  - `releaseAll` runs in `setActive(false)` (:250) and `clearWithoutRelease` at :214.
- **Vorago MIDI mapping:** the `IMidiMapping` precedent is `controller.cpp:280–296`; the interface is declared with
  `DEFINE_INTERFACES … DEF_INTERFACE(IMidiMapping)` (`controller.h:105–108`).
- **SDK:**
  - MIDI constants `kCtrlModWheel = 1`, `kCtrlSustainOnOff = 64`, `kAfterTouch = 128`, `kPitchBend = 129`
    (`pluginterfaces/vst/ivstmidicontrollers.h:28, :52, :104, :105`);
  - `kParamValuesChanged = 1 << 2` (`ivsteditcontroller.h:134`) and `IMidiMapping::getMidiControllerAssignment` (:671);
  - `EditController::setDirty(TBool)` (`public.sdk/source/vst/vsteditcontroller.h:91`);
  - `ComponentBase::allocateMessage / sendMessage / notify` (`vstcomponentbase.h:50, :53, :72`);
  - `AudioEffect::getTailSamples` returns `kNoTail` (`vstaudioeffect.h:84`);
  - `IBStreamer::readRaw / writeRaw` return `TSize` (`base/source/fstreamer.h:212–213`);
  - `RangeParameter` (`vstparameters.h:95`);
  - `Steinberg::Timer::create(ITimerCallback*, uint32)` (`base/source/timer.h:136`) **returns nullptr on Linux without an
    injected factory** (`base/source/timer.cpp:346–351`). The same caveat is recorded at
    `plugins/vorago/tests/integration/ecosystem_frame_test.cpp:27–30`.
- **Test helpers:**
  - `AllocationScope` and the per-thread filter (`tests/test_helpers/allocation_detector.h:41, :111`);
  - `Krate::DSP::FFT::prepare(size_t)` (`primitives/fft.h:147`; a power of 2 in [256, 8192]) and
    `forward(const float*, Complex*)` (:186);
  - `StereoFieldTest::bandReading(L, R, n, fs, fLo, fHi)` (:738), with `kSubBandQBarDb = 1.0` and `kSubBandRhoBar = 0.99`
    (`dsp/tests/unit/systems/bass_stereo_field_test_helpers.h:95, :97`). Phase 9 SC-015 uses them over `[0, f_c)`
    (`dsp/tests/unit/systems/profundum_engine_chain_test.cpp:323–351`);
  - the Phase 9 base states `voiceEngineAs / voiceEngineBs`
    (`dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h:105, :113`).

### 1.8 Not reused, with the reason

- **The Phase 9 audibility descriptors.** They are anonymous-namespace functions inside
  `dsp/tests/unit/systems/profundum_engine_audibility_test.cpp` (the namespace opens at :90; for example `auWindowESubDb`
  :464, `auLkOutShort` :784, AT-27 at :1496–1508). They cannot be included, and FR-070 forbids editing `dsp/`. SC-018
  therefore **ports** the descriptors of the fifteen rows into a plugin test header, with a fidelity self-check (§6.4).
- **`render_fingerprint.h`.** It has no band metric (spec Conventions), so the new `band_level_metric.h` is used.
- **`VoiceModRouter` and Seraphis's `MorphParams`.** See the spec's "Not reused".

---

## 2. ODR sweep (run 2026-10-22)

Command: `grep -rnE "(class|struct|enum class|enum|using) <Name>\b" dsp/ plugins/ tests/`.

| Name | Hits | Use |
|---|---|---|
| `SoundParamRow`, `EndpointParams`, `NoteParams`, `MorphControlParams`, `PerformanceParams`, `CompareMode`, `ParamRoute`, `RouteKind`, `DisplayUnit`, `PlainLaw`, `LeafBits`, `BandLevelReading` | 0 each | New, all in namespace `Profundum`; the test types are in `ProfundumPluginTest` |
| `SustainLatch` | 1 (`vorago/src/processor/sustain_latch.h:22`) | The class being moved (FR-061) |
| `PedalPoint` | 1 (a private nested struct of `Vorago::Processor`) | Profundum's is a private nested struct of `Profundum::Processor`. The enclosing classes differ, so there is no collision |

`PerformanceParams` and `CompareMode` were already in the spec's swept-and-reserved list and in FR-067. No roadmap hazard name
is used. (`ParamRouteEntry` has a hit and is not used.)

**File-layout refinement over the spec's "New components" table.** The row type and the mapping math move into their own
header, `parameters/sound_param_row.h`. Each section pack needs the row type, and `sound_param_table.h` needs every section
pack (it holds the aggregate). No new type name results: `SoundParamRow` simply lives in `sound_param_row.h`.

---

## 3. Parameter IDs and constants (`plugin_ids.h`)

### 3.1 Enumerators (appended to `enum ParameterIDs`)

The names follow `k{Section}{Parameter}Id` and the standard names table (`Mix`, `Mod`, …).

```cpp
    kPitchBendId = 4, kSustainPedalId = 5, kModWheelId = 6, kChannelPressureId = 7,
    kMassId = 104, kMorphPositionId = 105,
    kCoreDepthId = 200, kCoreBodyId, kCoreBodyCurvatureId, kCoreBodyEmphasisId, kCoreEdgeId, kCoreShiftId,
    kCoreExcitationMixId, kCoreExcitationDriveId, kCoreExcitationAsymmetryId,                       // 206-208
    kCoreParamRangeEnd = 300,
    kAnchorWaveformId = 300, kAnchorModeId, kAnchorLevelId, kAnchorPunchBoostId, kAnchorPunchTimeId,
    kAnchorGhostTimeId, kAnchorFoldFeedId,                                                          // 300-306
    kAnchorParamRangeEnd = 400,
    kDropAmountId = 400, kDropTimeId, kDropShapeId, kDropReboundId,                                 // 400-403
    kNoteMonoPriorityId = 410, kNoteLegatoId, kNotePortamentoId, kNotePortamentoModeId, kNoteGlideModeId,
    kNoteMemoryId, kNoteRetriggerId, kNotePitchBendRangeId,                                         // 410-417
    kAmpAttackId = 420, kAmpDecayId, kAmpSustainId, kAmpReleaseId, kAmpAttackCurveId, kAmpDecayCurveId,
    kAmpReleaseCurveId,                                                                             // 420-426
    kNoteParamRangeEnd = 500,
    kFoldAmountId = 500, kFoldShapeId = 501, kFoldParamRangeEnd = 600,
    kCharacterId = 600, kCharacterAmountId, kCharacterToneId, kCharacterParamRangeEnd = 700,
    kDynamicsTransientAttackId = 700, kDynamicsTransientSustainId, kDynamicsParamRangeEnd = 800,
    kFieldImageModeId = 800, kFieldSpreadId, kFieldGravityId, kFieldCrossoverId, kFieldCurveId, kFieldMotionDepthId,
    kFieldMotionRateId, kFieldMotionSourceId, kFieldSyncId, kFieldSyncRateId,                      // 800-809
    kFieldParamRangeEnd = 900,
    kMotionSourceId = 900, kMotionTargetId, kMotionAmountId, kMotionDepthId, kMotionCycleRateId, kMotionCycleShapeId,
    kMotionCycleSyncId, kMotionCycleSyncRateId, kMotionCyclePhaseId, kMotionCycleRetriggerId, kMotionCycleFadeInId,
    kMotionPolarityId, kMotionDriftSmoothnessId, kMotionEnvAttackId, kMotionEnvDecayId,            // 900-914
    kMotionParamRangeEnd = 1000,
    kMorphAnchorModeId = 1000, kMorphEditTargetId = 1001, kMorphRampTimeId = 1002,
    kMorphParamRangeEnd = 1100,
    kNumRoutedIds = 1003,   // route table size: IDs 0..1002
```

The band comment above the enum (:44–49) is amended for FR-072: `400-499 Drop / note behaviour / amp envelope`.

### 3.2 State v2 constants

```cpp
constexpr Steinberg::int32 kCurrentStateVersion = 2;          // FR-040
constexpr Steinberg::int32 kStateV1Version = 1;               // a Phase 10 stream
constexpr std::size_t kStateV1Bytes = 36;                     // unchanged
constexpr std::size_t kStateNoteBlockBytes = 32;              // bytes 36-67
constexpr std::size_t kStateMorphControlBytes = 16;           // bytes 68-83
constexpr std::size_t kStateSeedBytes = 4;                    // bytes 84-87
constexpr std::size_t kStateEndpointAOffset = 88;
constexpr std::size_t kStateEndpointBOffset = 289;
constexpr std::size_t kStateRuntimeOffset = 490;
constexpr std::size_t kStateV2Bytes = 510;
static_assert(kStateEndpointAOffset == kStateV1Bytes + kStateNoteBlockBytes + kStateMorphControlBytes + kStateSeedBytes);
static_assert(kStateEndpointBOffset == kStateEndpointAOffset + Krate::DSP::kMorphStateBytes);
static_assert(kStateRuntimeOffset == kStateEndpointBOffset + Krate::DSP::kMorphStateBytes);
static_assert(kStateV2Bytes == kStateRuntimeOffset + Krate::DSP::BassMorphEngine::kMorphRuntimeBytes);
```

`plugin_ids.h` gains `#include <krate/dsp/systems/bass_morph_engine.h>` for those asserts. The processor and the controller
already include DSP headers through the packs, so no processor ↔ controller cross-include results.

### 3.3 Message IDs (processor ↔ controller; FR-025, FR-030, FR-031)

```cpp
inline constexpr const char* kMsgCopyEndpoint = "ProfundumCopyEndpoint";   // ctl -> proc, int attr "from" (0 A, 1 B)
inline constexpr const char* kMsgCompare      = "ProfundumCompare";        // ctl -> proc, int attr "mode"
inline constexpr const char* kMsgEndpoint     = "ProfundumEndpoint";       // proc -> ctl, int "endpoint", binary "blob" (201 B)
inline constexpr const char* kMsgCompareMode  = "ProfundumCompareMode";    // proc -> ctl, int "mode"
inline constexpr const char* kMsgRequestSync  = "ProfundumRequestSync";    // ctl -> proc, no attributes (FR-025 pull, ruling 2026-10-22)
```

---

## 4. Components

### 4.1 `parameters/sound_param_row.h` — row type, leaf words, laws

Plugin layer; included by the processor and the controller.

```cpp
namespace Profundum {
using MorphState = Krate::DSP::MorphState;
using FieldInfo = Krate::DSP::MorphState::FieldInfo;
inline constexpr std::size_t kNumLeaves = Krate::DSP::kMorphLeafFieldCount;            // 62 (morph_state.h:133)
inline constexpr std::uint8_t kNoLeaf = 0xFF;
using LeafBits = std::array<std::uint32_t, kNumLeaves>;

enum class DisplayUnit : std::uint8_t { List, Percent, BipolarPercent, Time, Hz, Db, Semitone, Mass100, Degrees };

struct SoundParamRow {
    Steinberg::Vst::ParamID id;
    std::uint8_t leaf;                              // kLawTable index
    std::uint8_t leaf2;                             // the modifier leaf of a Sync Rate pair, else kNoLeaf
    const Steinberg::Vst::TChar* title;
    const Steinberg::Vst::TChar* units;             // ParameterInfo::units
    const Steinberg::Vst::TChar* const* labels;     // list rows; nullptr for continuous
    std::uint8_t labelCount;                        // 0 for continuous
    DisplayUnit display;
};

[[nodiscard]] constexpr const FieldInfo& lawOf(std::uint8_t leaf) noexcept {
    return Krate::DSP::BassMorphEngine::kLawTable[leaf];                                   // bass_morph_engine.h:74
}

/// One 32-bit word per leaf: float -> bit pattern, enum -> underlying uint8, bool -> 0/1.
[[nodiscard]] constexpr LeafBits leafBitsOf(const MorphState& s) noexcept;
/// The inverse; the caller guarantees each word is a valid encoding for its leaf.
[[nodiscard]] constexpr MorphState morphStateFromLeafBits(const LeafBits& b) noexcept;
inline constexpr LeafBits kDefaultLeafBits = leafBitsOf(MorphState{});

/// F32 rows: normalized v in [0, 1] -> plain, in DOUBLE (SC-006's 1e-9 is unreachable through a float).
[[nodiscard]] double plainFromNormalized(const FieldInfo& f, double v) noexcept;
[[nodiscard]] double normalizedFromPlain(const FieldInfo& f, double plain) noexcept;
/// Any leaf: normalized -> stored word (float cast and hiOpen fix for F32; rounded index for Enum8 / Bool).
[[nodiscard]] std::uint32_t leafWordFromNormalized(std::uint8_t leaf, double v) noexcept;
[[nodiscard]] double normalizedFromLeafWord(std::uint8_t leaf, std::uint32_t word) noexcept;
[[nodiscard]] constexpr int indexFromNormalized(double v, int maxIndex) noexcept;   // min(maxIndex, int(v * maxIndex + 0.5)), v clamped
[[nodiscard]] constexpr int syncRateIndexOf(std::uint32_t noteWord, std::uint32_t modWord) noexcept;  // search of the 30 entries

/// Non-leaf continuous laws (Portamento, Morph Ramp Time, the performance parameters).
struct PlainLaw { double lo; double hi; double logOffset; bool logarithmic; };
[[nodiscard]] double plainFromNormalized(const PlainLaw& l, double v) noexcept;
[[nodiscard]] double normalizedFromPlain(const PlainLaw& l, double plain) noexcept;
}
```

`leafBitsOf` and `morphStateFromLeafBits` use `Krate::DSP::detail::morph_state_io::forEachMorphLeaf` (morph_state.h:334) and
`std::bit_cast`, which is constexpr in C++20 on all three toolchains. `kDefaultLeafBits` is therefore a compile-time
constant, and the leaf order cannot drift from the DSP table: `leavesMatchTable()` already asserts the visitor against the
table (:440).

**Mapping laws (FR-002), exact formulas.** Let `lo = double(f.lo)`, `hi = double(f.hi)` and `o = double(f.logOffset)`.

| `f.law` / kind | `plainFromNormalized(v)` | `normalizedFromPlain(x)` |
|---|---|---|
| `Linear`, `EqualPower` (F32) | `v <= 0 ? lo : v >= 1 ? hi : lo + v·(hi − lo)` | `clamp((x − lo)/(hi − lo), 0, 1)` |
| `Logarithmic`, `o == 0` | `v <= 0 ? lo : v >= 1 ? hi : logMapFromNormalized(v, lo, hi)` (parameter_helpers.h:80) | `logMapToNormalized(x, lo, hi)` (:85) |
| `Logarithmic`, `o > 0` | `v <= 0 ? lo : v >= 1 ? hi : (lo + o)·((hi + o)/(lo + o))^v − o` | `clamp(ln((x + o)/(lo + o)) / ln((hi + o)/(lo + o)), 0, 1)` |
| `Stepped` (Enum8 / Bool) | index `= indexFromNormalized(v, maxEnum)` | `index / maxEnum` |

Notes on the laws:

- The explicit endpoint snap makes "lo maps to exactly 0, hi to exactly 1" (SC-006) independent of `pow` rounding.
- `EqualPower` describes the **morph** law on the engine side. The knob itself is linear (FR-002).
- For F32 rows, `leafWordFromNormalized` computes `float x = static_cast<float>(plain)`. If `f.hiOpen && !(x < f.hi)`, it
  sets `x = std::nextafter(f.hi, f.lo)`; only `motion.cyclePhaseDeg` (leaf 45) has `hiOpen = true`. The word is
  `std::bit_cast<std::uint32_t>(x)`.
- A `v` of −0.0 takes the `v <= 0` branch to `lo`, which is +0 for `[0, x]` rows. The plugin therefore never writes −0.
- The non-leaf laws:
  - Portamento (412) is `PlainLaw{0, 10000, 1, true}`, the offset-log law "on (t + 1 ms)" of the spec ID table;
  - Morph Ramp Time (1002) is `PlainLaw{5, 2000, 0, true}`;
  - Pitch Bend Range (417) is a `RangeParameter` over [0, 24] with `stepCount = 24`, plain `= round(24·v)`.

**Defaults (FR-003).** A registered E-parameter's default is `normalizedFromLeafWord(leaf, kDefaultLeafBits[leaf])`. The
conformance test proves `leafWordFromNormalized(leaf, defaultNorm) == kDefaultLeafBits[leaf]` for all 62 leaves. If a log
row fails that bitwise check, registration computes the default with a bounded nudge (`std::nextafter` on the double
normalised value, at most 8 steps each way) until the word matches. The test still pins equality (Risk R2).

### 4.2 Section packs (one header per band, FR-005)

Each pack header holds its row table, its list-label arrays, its `static_assert`s and the FR-005 entry points of the
Phase 10 contract for its band: `register<Section>Params(ParameterContainer&)`,
`handle<Section>ParamChange(EndpointParams&, ParamID, double) noexcept` and `format<Section>Param(ParamID, double,
String128)` (for example `registerCoreParams`, `handleCoreParamChange`, `formatCoreParam`). For morphed rows each entry
point is a thin wrapper that applies the generic row path (`registerSoundRow`, `writeSoundRow`, `formatSoundRow`, declared
in `sound_param_row.h`, §4.3) to the pack's own row span, because the rows differ only by table data. The aggregate
functions in `sound_param_table.h` call the nine pack wrappers: registration in band order, handle and format dispatched on
`id / 100` (band 1 → `morph_params.h`, which owns 104). The note and morph packs add `save` / `load` / `loadToController`
for their non-endpoint fields, as Phase 10 does. This is FR-005's contract as written, so no spec amendment is needed.

| File | Rows (ID → leaf) | `static_assert` coverage |
|---|---|---|
| `core_params.h` | 200→0, 201→1, 202→2, 203→3, 204→4, 205→5, 206→6, 207→7, 208→8 | leaves {0..8} exactly |
| `anchor_params.h` | 300→55 (list of 5), 301→56 (list: Lock, Follow, Punch, Ghost), 302→57, 303→58, 304→59, 305→60, 306→61 | {55..61} |
| `note_params.h` | 400→14, 401→15, 402→16, 403→17; 420→18 … 426→24; **plus** `NoteParams` (the non-morphed IDs 410–417) | {14..24} |
| `fold_shape_params.h` | 500→9 "Harmonic Fold", 501→10 "Shape" | {9, 10} |
| `character_params.h` | 600→11 (list: Tube, Wavefold, Rectify, Bite, Crush, Fracture), 601→12, 602→13 | {11..13} |
| `dynamics_params.h` | 700→53, 701→54 | {53, 54} |
| `field_params.h` | 800→25 (Solid, Spread, Wide), 801→26, 802→27, 803→28, 804→29, 805→30, 806→31, 807→32 (Drift, Cycle), 808→33 (Off, On), 809→34 + 35 (30 labels) | {25..35} |
| `motion_params.h` | 900→36 (Off, Env, Cycle, Drift), 901→37 (Mass, Depth, Body, Edge, Shift), 902→38, 903→39, 904→40, 905→41 (Sine, Triangle, Sawtooth, Square, S&H, Smooth Random), 906→42, 907→43 + 44, 908→45, 909→46, 910→47, 911→48 (Bipolar, Unipolar), 912→49, 913→50, 914→51 | {36..51} |
| `morph_params.h` | 104→52 "Mass" (E); **plus** `MorphControlParams` (105, 1000–1002) and `CompareMode` | {52} |

**Label rules.**

- Every list asserts `labelCount == lawOf(leaf).maxEnum + 1`.
- A Sync Rate row asserts `labelCount == (lawOf(leaf).maxEnum + 1) · (lawOf(leaf2).maxEnum + 1) == kNoteValueDropdownCount`
  (10 × 3 = 30). It uses `Krate::Plugins::Parameters::kNoteValueDropdownStrings` (note_value_ui.h:21).
- Titles are exactly those of the spec ID table. FR-009 fixes ID 500 "Harmonic Fold", the Character list above, and
  Anchor Morph Mode (1000) "Hold, Blend, Yield".
- Bool rows register the list `Off`, `On`.
- The other list strings are not checked by SC-005. Phase 12 owns later wording, but these become permanent once shipped:
  - Anchor Waveform: `Sine, Triangle, Rounded Sine, Asymmetric Sine, Octave Down`;
  - Mono Priority: `Last, Low, High`; Portamento Mode: `Always, Legato Only`;
  - Glide Mode: `Constant Time, Constant Rate, Quantized`;
  - Note Memory: `Always Retrigger, Legato, Envelope Retrigger, Oscillator Continuous`; Note Retrigger: `Reset, Continue`;
  - Edit Target: `A, B`.

**`NoteParams`** (in `note_params.h`):

```cpp
struct NoteParams {
    std::atomic<int> monoPriority{0};          // MonoMode::LastNote
    std::atomic<int> legato{0};
    std::atomic<float> portamentoMs{0.0f};
    std::atomic<int> portamentoMode{0};        // PortaMode::Always
    std::atomic<int> glideMode{0};             // GlideMode::ConstantTime
    std::atomic<int> memoryMode{0};            // NoteMemory::Mode::AlwaysRetrigger
    std::atomic<int> retrigger{0};             // NoteMemory::Retrigger::Reset
    std::atomic<float> bendRangeSt{Krate::DSP::ProfundumEngine::kDefaultBendRangeSt};   // engine.h:112
};
void handleNoteParamChange(NoteParams&, Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue) noexcept;
void registerNoteParams(Steinberg::Vst::ParameterContainer&);          // 410-417 only (E rows register generically)
void saveNoteParams(const NoteParams&, Steinberg::IBStreamer&);        // 32 bytes, in the order of spec bytes 36-67
bool loadNoteParams(NoteParams&, Steinberg::IBStreamer&);              // false at the first failed read; enums only in range
template <typename SetParamFunc> void loadNoteParamsToController(Steinberg::IBStreamer&, SetParamFunc);
void pushNoteParams(const NoteParams&, Krate::DSP::ProfundumEngine&) noexcept;   // the 8 setters of FR-014
```

Load sanitising follows Phase 10 (`global_params.h:177–212`):

- a non-finite float keeps its atomic;
- a finite float is clamped to its range;
- an integer enum outside `[0, maxEnum]` is not stored.

**`MorphControlParams` and `CompareMode`** (in `morph_params.h`):

```cpp
enum class CompareMode : std::uint8_t { AB = 0, A = 1, B = 2 };
struct MorphControlParams {
    std::atomic<float> position{0.0f};
    std::atomic<float> rampMs{Krate::DSP::BassMorphEngine::kDefaultPositionRampMs};                       // 40
    std::atomic<int> anchorMorphMode{static_cast<int>(Krate::DSP::BassMorphEngine::kDefaultAnchorMorphMode)};  // Blend = 1
    std::atomic<int> editTarget{0};                                                                       // Endpoint::A
};
// handle / register / format / save (16 B, bytes 68-83) / load / loadToController, following the Phase 10 contract
void resetMorphControlParams(MorphControlParams&) noexcept;   // v1 load (FR-044): 0 / 40 / Blend / A
/// The engine's own position rule (BassMorphEngine::setPosition, bass_morph_engine.h:139-146), applied before storing.
[[nodiscard]] float sanitizeMorphPosition(float p, float previous) noexcept;
```

**Position sanitising (FR-041, FR-042; Risk R10).** The 105 handler, `loadMorphControlParams` and
`loadMorphControlParamsToController` store `sanitizeMorphPosition(v, previous)`, which copies the engine's rule
(`BassMorphEngine::setPosition`, `bass_morph_engine.h:139–146`) exactly:

- a non-finite value returns `previous`, so the atomic keeps its value (the Phase 10 load rule);
- `p < Krate::DSP::kDenormalThreshold` (1e-15f, `core/db_utils.h:38`) returns the literal `+0.0f`. This covers −0,
  negatives, denormals and tiny normals such as 1e-20, which the [0, 1] clamp alone would keep;
- `p >= 1` returns `1.0f`; any other value is returned unchanged.

The stored position is therefore always bit-identical to the engine's `targetPosition()` after `setMorphPosition`
(compare A·B). Two consequences follow. The FR-041 synthesized blob never carries a target, p or p_a in
`0 < |x| < 1e-15`, which `restoreRuntime` rejects (`bass_morph_engine.h:270–300`). And step 9's bitwise target compare
(§4.5.1) matches a blob the engine published from that same position. SC-010's tiny-position arm pins both.

**`PerformanceParams`** (`performance_params.h`, IDs 4–7, never persisted):

```cpp
struct PerformanceParams {
    std::atomic<float> pitchBendNorm{0.5f};    // centre; the engine gets 2v - 1 (FR-051)
    std::atomic<float> sustainNorm{0.0f};      // display mirror only; pedal points drive the latch
    std::atomic<float> modWheel{0.0f};         // [0, 1]
    std::atomic<float> channelPressure{0.0f};  // [0, 1]
};
void handlePerformanceParamChange(PerformanceParams&, Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue) noexcept;
void registerPerformanceParams(Steinberg::Vst::ParameterContainer&);
void resetPerformanceParams(PerformanceParams&) noexcept;   // setState: centre / up / 0 / 0 (FR-046)
```

`note_params.h` likewise gains `void resetNoteParams(NoteParams&) noexcept`, which stores the `Settings` defaults listed
above. It is used by the v1 load (FR-044, §4.5.3).

**Registration flags (FR-004).**

- IDs 4 and 5 are `kCanAutomate | kIsHidden`.
- IDs 6 and 7 are `kIsHidden` only.
- ID 1001 is registered with `kIsList` and **without** `kCanAutomate`: it is built with `createDropdownParameter`, then
  `getInfo().flags = kIsList`.

### 4.3 `parameters/sound_param_table.h` — aggregate, route table, endpoint slots

```cpp
inline constexpr std::array<SoundParamRow, 60> kSoundRows = /* concatenation of the nine packs' rows */;

enum class RouteKind : std::uint8_t { None, Global, Macro, Performance, Sound, Note, MorphControl };
struct ParamRoute { RouteKind kind; std::uint8_t index; };   // index into kSoundRows for Sound
inline constexpr std::array<ParamRoute, kNumRoutedIds> kParamRoutes = /* constexpr builder */;

// EndpointParams is declared in sound_param_row.h, because the packs' handle wrappers take it (§4.2):
struct EndpointParams {
    std::array<std::atomic<std::uint32_t>, kNumLeaves> leaf;   // initialised from kDefaultLeafBits in the ctor
    std::atomic<bool> dirty{true};                             // audio: assemble + setEndpoint
    std::atomic<bool> syncPending{false};                      // sender: report to the controller (FR-025)
    EndpointParams() noexcept;                                 // stores kDefaultLeafBits[i], relaxed
    [[nodiscard]] LeafBits load() const noexcept;              // 62 relaxed loads
    void store(const LeafBits&) noexcept;                      // 62 relaxed stores
};

// Generic row path (declared in sound_param_row.h; called only through the nine pack wrappers, §4.2):
/// FR-011 / FR-012: writes 1 leaf word (2 for a Sync Rate pair), sets dirty + syncPending.
void writeSoundRow(EndpointParams& target, const SoundParamRow& row, double v) noexcept;
void registerSoundRow(Steinberg::Vst::ParameterContainer&, const SoundParamRow& row);    // default per §4.1
Steinberg::tresult formatSoundRow(const SoundParamRow& row, double v, Steinberg::Vst::String128);   // FR-006

// Aggregate entry points (sound_param_table.h), dispatching to the pack wrappers on id / 100:
void handleSoundParamChange(EndpointParams& target, Steinberg::Vst::ParamID id, double v) noexcept;
void registerSoundParams(Steinberg::Vst::ParameterContainer&);                // the nine registerXxxParams, band order
Steinberg::tresult formatSoundParam(Steinberg::Vst::ParamID, double v, Steinberg::Vst::String128);   // FR-006
[[nodiscard]] const SoundParamRow* soundRowOf(Steinberg::Vst::ParamID) noexcept;                     // via kParamRoutes
```

The header proves these at compile time:

- every `kSoundRows` ID is unique and lies in its band;
- the union of `leaf` / `leaf2` over the 60 rows is `{0..61}`, each exactly once;
- `kParamRoutes` holds exactly 84 entries that are not None (8 from Phase 10, 76 new);
- the routed IDs in 400–499 are exactly {400–403, 410–417, 420–426} (FR-054).

**Sync Rate write (FR-012).** `idx = indexFromNormalized(v, 29)` and `m = kNoteValueDropdownMapping[idx]` (note_value.h:136).
The handler stores `uint8(m.note)` into `leaf` and `uint8(m.modifier)` into `leaf2`. For display, the read-back is
`syncRateIndexOf(word(leaf), word(leaf2)) / 29.0`.

**Display (FR-006, FR-007).** `formatSoundParam` and the note and morph formatters use ASCII only (the Phase 10 rule):

| `DisplayUnit` | Format |
|---|---|
| `Time` | `r = round(10·x) / 10`, the value as `"%.1f"` prints it. `r < 1000` → `"%.1f ms"`; else `"%.2f s"` of x / 1000. So 999.94 → `"999.9 ms"` and 999.96 → `"1.00 s"`, never `"1000.0 ms"` |
| `Hz` | `r = round(100·x) / 100`. `r < 1` → `"%.2f Hz"`; else `"%.1f Hz"`. The only Hz row is Cycle Rate (904), whose `lo` is `kMinFrequency = 0.01` (`primitives/lfo.h:34`), so the smallest string is `"0.01 Hz"` |
| `Db` | `"%+.1f dB"` of `x + 0.0`, so −0 prints `"+0.0 dB"` |
| `Semitone` | `"%.1f st"` |
| `Percent` | `"%.0f%%"` of `100·x` (plain [0, 1] controls) |
| `BipolarPercent` | `"%+.0f%%"` of `100·x` ([−1, 1] and [−1, 0] controls) |
| `Mass100` | `"%.0f"` (Mass 104 and Harmonic Fold 500, both 0–100) |
| `Degrees` | `"%.0f deg"` (Cycle Phase 908) |

- Portamento (412) uses the Time rule, but its suffix follows the **current** Glide Mode (FR-007). The controller reads
  `getParamNormalized(kNoteGlideModeId)` as an index: ConstantTime gives `ms`, ConstantRate `ms/oct`, Quantized `ms`. Under
  ConstantRate a value of 1000 or more formats as `"%.2f s/oct"`.
- **FR-006's "Hz with two significant decimals below 1 Hz"** is read as two decimal places (`"%.2f"`: 0.3 →
  `"0.30 Hz"`, 0.012 → `"0.01 Hz"`). `"%.2g"` was dropped: it prints 0.3 as `"0.3"` and 0.012 as `"0.012"`, so its digit
  count varies. This reading is listed for the FR-006 row in §10. `Profundum_ParamDisplayFormats` pins the strings.
- Shape (501) uses a plain Percent display, with no corner names (FR-009).
- Buffers are `char8 text[64]`, a margin against GCC's `-Wformat-truncation`.

### 4.4 `plugins/shared/src/midi/sustain_latch.h` (FR-061)

The new file is a verbatim copy of `plugins/vorago/src/processor/sustain_latch.h:16–79`, with the namespace changed to
`Krate::Plugins` and the banner updated. `plugins/vorago/src/processor/sustain_latch.h` becomes:

```cpp
#pragma once
#include "midi/sustain_latch.h"   // plugins/shared/src/midi/sustain_latch.h
namespace Vorago {
using SustainLatch = Krate::Plugins::SustainLatch;   // FR-061: no second definition, no call-site change
}
```

`plugins/shared/CMakeLists.txt` lists `src/midi/sustain_latch.h` next to `midi_event_dispatcher.h` (:35).

### 4.5 Processor (`processor.h` / `processor.cpp`)

**New members.** All are safe on the audio thread. About 2 KiB is added, far below the 64 KiB `sizeof` assert (:91).

```cpp
class Processor : public Steinberg::Vst::AudioEffect, public Steinberg::ITimerCallback {
    // ... Phase 10 members ...
    std::array<EndpointParams, 2> endpoints_{};                 // [A, B]: the authoritative slots (FR-025, FR-040)
    NoteParams noteParams_{};
    MorphControlParams morphParams_{};
    PerformanceParams perfParams_{};
    std::atomic<std::uint32_t> seed_{Krate::DSP::ProfundumEngine::kDefaultSeed};   // FR-043
    std::atomic<std::int32_t> copyRequest_{-1};                 // -1 none, 0 A->B, 1 B->A (FR-030)
    std::atomic<std::uint8_t> compareMode_{0};                  // CompareMode::AB (FR-031)
    std::atomic<bool> compareSyncPending_{false};
    // FR-041 runtime blob: seqlock (one writer, the audio thread)
    std::atomic<std::uint32_t> rtSeq_{0};
    std::array<std::atomic<std::uint32_t>, 5> rtWords_{};
    std::atomic<bool> rtPublished_{false};
    // FR-042: hand-off of the loaded blob, setState -> audio thread: a second seqlock (§4.5.2, "Restore hand-off")
    std::atomic<std::uint32_t> restoreSeq_{0};                  // even = stable, odd = a write in progress
    std::array<std::atomic<std::uint32_t>, 5> restoreWords_{};
    std::atomic<std::uint32_t> restoreAppliedSeq_{0};           // stored by the audio thread only, at the end of process()
    std::uint32_t restoreSeenSeq_ = 0;                          // audio only: the sequence consumed this block
    std::atomic_flag restoreWriterBusy_ = ATOMIC_FLAG_INIT;     // serialises setState writers (non-audio side only)
    std::array<std::byte, 20> lastRestoreBlob_{};               // audio-written test seam; read after the thread joins
    std::uint32_t restoreApplyCount_ = 0;                       // idem: restoreMorphRuntime calls that returned true
    std::atomic<bool> clearLatchPending_{false};                // FR-052, setState path
    std::atomic<std::uint32_t> endpointRejections_{0};          // FR-013 / FR-067
    std::atomic<double> tailSampleRate_{0.0};                   // FR-060: 0 until setupProcessing succeeds
    float lastPerfOffsetC_ = 0.0f;                              // audio only (FR-055 change detection)
    Krate::Plugins::SustainLatch latch_{};
    struct PedalPoint { std::int32_t offset; bool down; };
    static constexpr std::size_t kMaxPedalPoints = 128;         // as Vorago processor.h:206
    std::array<PedalPoint, kMaxPedalPoints> pedalPoints_{};
    std::size_t numPedalPoints_ = 0;
    Steinberg::IPtr<Steinberg::Timer> syncTimer_;               // nullptr on Linux (timer.cpp:346-351)
    std::atomic_flag flushBusy_ = ATOMIC_FLAG_INIT;
};
```

**New or changed public API:**

```cpp
Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;     // copy / compare requests
Steinberg::uint32 PLUGIN_API getTailSamples() override;                                // FR-060
void onTimer(Steinberg::Timer*) override;                                              // -> flushControllerSync()
/// FR-025 sender. NON-AUDIO threads only (timer, notify, setState, setActive, tests). Allocates IMessages.
void flushControllerSync() noexcept;
// FR-067 seams (never called by process(), allocation-free):
void setEngineObserverForTest(Krate::DSP::ProfundumEngineObserver* o) noexcept { engine_->setObserver(o); }
[[nodiscard]] std::uint32_t endpointRejectionsForTest() const noexcept;
[[nodiscard]] bool sustainDownForTest() const noexcept { return latch_.isDown(); }
[[nodiscard]] CompareMode compareModeForTest() const noexcept;
[[nodiscard]] const std::array<std::byte, 20>& lastRestoreBlobForTest() const noexcept { return lastRestoreBlob_; }
[[nodiscard]] std::uint32_t restoreApplyCountForTest() const noexcept { return restoreApplyCount_; }
[[nodiscard]] bool hasSyncTimerForTest() const noexcept { return syncTimer_ != nullptr; }
```

#### 4.5.1 `process()` order (FR-010 – FR-014, FR-030 – FR-032, FR-042, FR-052, FR-055)

```
 1. ScopedDenormalMode (unchanged, first statement); then const CompareMode cmp = compareMode_.load(relaxed).
                                    The compare mode is read ONCE per call, here, and steps 8 and 12 both use
                                    the local cmp. A notify() that switches A -> AB between them therefore can
                                    never publish a blob rendered under compare A (FR-032).
 2. applyPendingCopy()              copyRequest_.exchange(-1): dst.leaf[i] = src.leaf[i] for the 62 slots;
                                    dst.dirty = dst.syncPending = true. It runs before the parameters, so a
                                    same-block edit of the destination lands on top of the copy.
 3. processParameterChanges()       pass 1: find queue 1001 and apply its LAST point (FR-011, any queue order);
                                    pass 2: every other queue. ID 5 -> collectPedalPoints (every point);
                                    the rest: last point wins, detail::isFinite, clamp [0, 1], route by
                                    kParamRoutes[id] (id >= kNumRoutedIds, or a None route -> ignored).
                                    Sound route -> handleSoundParamChange(endpoints_[editTarget], id, v).
 4. guards 2-6 (unchanged). Each early return after step 3 calls applyPendingPedalPointsImmediately()
    (Vorago precedent :839-846: state only, and only when prepared).
 5. guard 7 (unprepared): silence and return (pedal points dropped, as in Vorago).
 6. if clearLatchPending_.exchange(false): latch_.clearWithoutRelease()
 7. applyEndpoints()                c = clamp(modWheel + channelPressure, 0, 1); if bits(c) != bits(lastPerfOffsetC_),
                                    both endpoints are marked dirty. For e in {A, B} with dirty.exchange(false):
                                    s = morphStateFromLeafBits(endpoints_[e].load()); if c != 0,
                                    s.mass = clamp(s.mass + c * (hi - lo), lo, hi) on the Mass row of kLawTable;
                                    if (!engine_->setEndpoint(e, s)) endpointRejections_++.
 8. pushParamsToEngine()            the four Phase 10 setters + pushNoteParams + setPitchBend(2v - 1) +
                                    setMorphRampMs + setAnchorMorphMode + setMorphPosition, compare-aware on the
                                    local cmp: A -> 0, B -> 1, AB -> the registered (sanitised, §4.2) position.
 9. applyPendingRestore()           ONE seqlock read try of restoreWords_ ("Restore hand-off", §4.5.2). Nothing
                                    is pending if the sequence is odd or equals restoreSeenSeq_; a torn read
                                    (s1 != s2) does nothing and is retried next block. It never spins. On a clean
                                    read of a new sequence: restoreSeenSeq_ = s1; then, if the version byte is 1
                                    and the blob's target (bytes 1-4), mode (byte 13) and ramp (bytes 14-17) equal
                                    the morphParams_ position / anchorMorphMode / rampMs BITWISE, call
                                    engine_->restoreMorphRuntime(blob, 20). A true return copies the blob into
                                    lastRestoreBlob_ and increments restoreApplyCount_; a false one is ignored.
10. tempo, output gain (unchanged)
11. slice loop: the events due at the slice start (latch-aware dispatch), THEN the pedal points at offsets <= cursor
    (at equal offsets, notes first); slice end = min(next event, next pedal point, total).
12. if cmp == AB: publishRuntime()  (seqlock write of engine_->serializeMorphRuntime); then
    restoreAppliedSeq_.store(restoreSeenSeq_, release). The store comes after the publication, so getState keeps
    writing the loaded blob until the restored engine's own blob has been published.
```

- Steps 7–9 implement FR-042's order: endpoints, then the explicit morph fields, then the runtime blob.
- `setupProcessing` also calls `applyEndpoints()` before its existing `pushParamsToEngine()` (:138), so a freshly prepared
  engine already holds the slots.
- **Latch-aware dispatch.** The `dispatchEvent` bodies (:393–417) take the Vorago form (`vorago/processor.cpp:1131–1153`):
  - a note-on calls `latch_.noteOn(p)`, then `engine_->noteOn`;
  - a note-off, or a velocity-0 note-on, runs `if (latch_.noteOff(p)) engine_->noteOff(p)`;
  - a pedal point runs `latch_.setPedal(p.down, [this](std::uint8_t n) noexcept { engine_->noteOff(n); })`.
- **Edit Target (FR-011).** The target is read once per call, **after** pass 1 (`morphParams_.editTarget`, relaxed), and is
  used for every Sound route in pass 2. An Edit Target change alone writes nothing else: no engine call, no slot write
  (SC-009 #2).

#### 4.5.2 Runtime blob publication (FR-041): a Boehm seqlock

The writer is the audio thread, once per rendered block while the compare is AB:

```cpp
std::byte buf[20]; engine_->serializeMorphRuntime(buf, 20);
std::uint32_t w[5]; std::memcpy(w, buf, 20);
const std::uint32_t s = rtSeq_.load(std::memory_order_relaxed);
rtSeq_.store(s + 1, std::memory_order_relaxed);
std::atomic_thread_fence(std::memory_order_release);
for (i) rtWords_[i].store(w[i], std::memory_order_relaxed);
rtSeq_.store(s + 2, std::memory_order_release);
rtPublished_.store(true, std::memory_order_release);
```

The reader is `getState`, on any non-audio thread. It makes up to 1024 tries of:

1. `s1 = rtSeq_.load(acquire)`; skip the try if `s1` is odd;
2. read the 5 words, relaxed;
3. `atomic_thread_fence(acquire)`, then `s2 = rtSeq_.load(relaxed)`;
4. accept if `s1 == s2`.

If every try fails, it falls back to the synthesized blob. That needs a writer far faster than the reader, which cannot
happen at one write per block. There is no lock, and the audio thread never waits.

**Restore hand-off (FR-042), the second seqlock.** The loaded blob's five words must agree with one another (its
selections are checked against its own position), so the per-leaf argument of §5 does not cover it. A second `setState`
during the audio thread's read could otherwise install p and p_a from one save with the target and selection bytes of
another.

- **Writer:** `publishRestore(words)`, called by `setState` only, on a non-audio thread. `setState` calls are serialised
  by `restoreWriterBusy_`: `while (test_and_set(acquire)) std::this_thread::yield();`. The spin is on the non-audio side
  only and contends only with another `setState`. Under the flag the writer does `s = restoreSeq_.load(relaxed)`,
  `store(s + 1, relaxed)`, `atomic_thread_fence(release)`, the five relaxed word stores, then `store(s + 2, release)`, and
  finally `clear(release)`.
- **Cancel:** `publishRestore` of five zero words. Version byte 0 is not `kMorphRuntimeFormatVersion`, so the reader never
  calls the engine with it, and `restoreRuntime` would reject it anyway (`bass_morph_engine.h`, the version check of
  `restoreRuntime`).
- **Reader (audio):** step 9, one try per block. `s1 = restoreSeq_.load(acquire)`, the five relaxed loads,
  `atomic_thread_fence(acquire)`, then `s2 = restoreSeq_.load(relaxed)`. It does nothing unless `s1 == s2`, `s1` is even
  and `s1 != restoreSeenSeq_`.
- **No lost update.** The audio thread consumes a *sequence*, not a flag. A second `setState` that lands after a clean read
  moves the sequence to `s1 + 2`, and the next block installs that blob.
- **Wrap.** The sequence advances by 2 per `setState` and is compared only for equality, so a 32-bit wrap needs 2³¹ loads
  between two blocks.
- **Test.** `Profundum_RestoreHandOffNoTear` (§6.2).

**Which blob `getState` writes,** in order of precedence:

1. **A loaded blob is pending.** `getState` makes a bounded seqlock read of `restoreWords_` (1024 tries, as below; on
   failure this rule is skipped). The read sequence differs from `restoreAppliedSeq_.load(acquire)` and the version byte
   is 1: write those words. A `setState` → `getState` with no render in between therefore writes the loaded blob, so a
   load-then-save round trip is byte-stable. This **departs from FR-041's literal text** ("before the first rendered
   block, `getState` writes the blob of the registered morph-control values"). The FR-041 amendment is listed in §10, and
   the SC-012 k = 510 arm pins the behaviour;
2. `rtPublished_` is set: write the seqlock read;
3. otherwise the **synthesized** blob (FR-041):
   - `[0] = kMorphRuntimeFormatVersion`;
   - `[1..4] = [5..8] = [9..12] =` the bits of `morphParams_.position`;
   - `[13] = anchorMorphMode`, `[14..17] = rampMs`;
   - `[18] = 0` (all A) and `[19] = 0` (Anchor A).

The byte offsets are re-stated as plugin constants (`kRtTargetOffset = 1` …) because the engine's are private
(`bass_morph_engine.h:340–347`). SC-012 checks them against a real engine's `serializeMorphRuntime` (§6.2). See Open
Question 1 on `p_a`.

#### 4.5.3 State v2 (FR-040, FR-042 – FR-046)

**`getState`** writes, little-endian through `IBStreamer` (as `processor.cpp:300` does):

1. `int32 2`;
2. `saveGlobalParams`, `saveMacroParams`;
3. `saveNoteParams` (32 bytes), `saveMorphControlParams` (16 bytes);
4. `writeInt32u(seed_)`;
5. endpoint A, then B. Each is `serializeMorphState(morphStateFromLeafBits(endpoints_[e].load()), buf, 201)` followed by
   `writeRaw(buf, 201)`;
6. the 20-byte blob.

The endpoints come from the slots, never from the engine (FR-040). The bytes therefore depend neither on the audio thread
nor on Mod Wheel / Channel Pressure (FR-056). `serializeMorphState` cannot return 0 here (§1.1 consequence). If it ever did,
the serialisation of `kDefaultLeafBits` is written instead, so the layout stays 510 bytes.

**`setState`:**

```
read version; refuse > 2 (unchanged rule)
loadGlobalParams && loadMacroParams                               (v1 and v2)
if version == 1 (FR-044: "every Phase 11 field keeps its default, and both endpoints stay MorphState{}"):
   endpoints_[A].store(kDefaultLeafBits), endpoints_[B].store(kDefaultLeafBits); both dirty + syncPending
   resetNoteParams(noteParams_); resetMorphControlParams(morphParams_)   (position 0, ramp 40, Blend, Edit Target A)
   seed_ = kDefaultSeed
   publishRestore(cancel)                                         (a blob pending from an earlier setState is dropped)
if version >= 2, as a chain where each step runs only if every earlier one read completely:
   loadNoteParams -> loadMorphControlParams -> readInt32u(seed) -> seed_.store
   -> readRaw(bufA, 201) == 201 && deserializeMorphState(bufA, 201, local)
        ? endpoints_[A].store(leafBitsOf(local)), dirty, syncPending : A unchanged (a short read stops the chain)
   -> the same for B
   -> readRaw(blob, 20) == 20 ? publishRestore(blob) : publishRestore(cancel)
   a chain stopped before the blob step also ends in publishRestore(cancel)
always, for both versions:
   compareMode_ = AB; compareSyncPending_ = true;
   resetPerformanceParams (pitch bend centre); clearLatchPending_ = true;
   endpoints_[A/B].syncPending = true; flushControllerSync()
```

- **v1 resets, v2 cuts keep (FR-044).** A v1 stream is a Phase 10 project. It carries no Phase 11 field, so every Phase 11
  field takes its **default**, whatever the instance held before (a host preset switch, for example). The project then
  sounds as it did in Phase 10. A *cut v2* stream keeps the **prior** value of every field after the cut. The two rules
  differ on purpose, and SC-012 runs both arms on a processor whose prior state is a non-default v2 state, so it can tell
  "reset" from "keep".
- **The v1 runtime.** `setState` makes no engine call, so after a v1 load the engine's p slews from the prior position to
  the reset position 0 over the reset ramp. Both endpoints are `MorphState{}`, so the slew is inaudible. SC-012's v1 arm
  therefore leaves the blob bytes out of its byte comparison.

- A deserialisation **rejection** (bad version byte, bool > 1, out-of-box value) leaves that endpoint unchanged. The chain
  continues, because all 201 bytes were read.
- A short read (`readRaw` returns < 201) stops the chain: "every field after the cut keeps its prior in-memory value"
  (FR-044).
- `setState` makes no engine call anywhere (FR-042, the Phase 10 rule).

**Seed (FR-043).** `setupProcessing` calls `engine_->setSeed(seed_.load())` immediately before `engine_->prepare` (:132).
`setActive(false)` calls it immediately before `(*engine_).reset()` (:149).

#### 4.5.4 Controller sync sender (FR-025)

The audio thread only sets `syncPending` and `compareSyncPending_`: relaxed stores made after the slot stores. The sender
takes each flag with `exchange(false, acq_rel)` and then reads the slots. `flushControllerSync()`:

```
if (flushBusy_.test_and_set(acquire)) return;          // re-entrancy / cross-thread guard; the flags stay set
for e in {A, B}: if endpoints_[e].syncPending.exchange(false, acq_rel):
    s = morphStateFromLeafBits(endpoints_[e].load()); serializeMorphState(s, buf, 201);
    IPtr<IMessage> m = owned(allocateMessage()); if (!m) { set the flag again; continue; }
    m->setMessageID(kMsgEndpoint); attrs->setInt("endpoint", e); attrs->setBinary("blob", buf, 201); sendMessage(m);
if compareSyncPending_.exchange(false): send kMsgCompareMode with int "mode"
flushBusy_.clear(release)
```

- It sends the **persisted** slots only, never the Mass offset (FR-056). A Mod Wheel or Channel Pressure change sets no
  `syncPending` (SC-024, SC-025).
- **Call sites, all non-audio:**
  - `onTimer`, from a 50 ms `Steinberg::Timer` created in `initialize` and released in `terminate`. `onTimer` does nothing
    but call `flushControllerSync()`;
  - the end of `notify`, `setState`, `setActive` (both branches) and `getState`;
  - most tests call `flushControllerSync()` directly, for determinism. The production path is tested separately: SC-025's
    timer arm drives `onTimer(nullptr)` and checks `hasSyncTimerForTest()` after `initialize` and `terminate`. Deleting
    the timer creation or the `onTimer` body therefore turns a case red.
- **Pull on Edit Target switch (user ruling 2026-10-22, FR-025).** The controller's 1001 hook sends `kMsgRequestSync`
  before the re-display; `notify` sets both `syncPending` flags and `compareSyncPending_`, then calls
  `flushControllerSync()` (§4.5.5). A reply that replaces the Edit Target endpoint after the re-display re-displays again and
  calls `restartComponent(kParamValuesChanged)` again. This closes the harmful stale-mirror case on every OS.
- **Linux (RULED 2026-10-22, see the pull above).** `Timer::create` returns nullptr there (timer.cpp:346–351). An *unobserved* edit (host automation the controller
  never sees) then reaches the mirror at the next notify / setState / getState / setActive call, instead of within 50 ms.
  As written, FR-025 ("after any change … the processor sends") is not met there between those calls. **This needs a user
  ruling, recorded in the FR-025 row, before the build stage starts** (Open Question 2, §10). The plan's default is
  option 1, together with the FR-025 amendment text given in §10. The build does not start on the default alone.

#### 4.5.5 `notify` (FR-030, FR-031)

- `kMsgCopyEndpoint`: `getInt("from", v)`; if `v ∈ {0, 1}`, then `copyRequest_.store(v)`.
- `kMsgCompare`: `getInt("mode", m)`; if `m ∈ {0, 1, 2}`, then `compareMode_.store(m)` and `compareSyncPending_ = true`.
- `kMsgRequestSync`: set `endpoints_[A].syncPending`, `endpoints_[B].syncPending` and `compareSyncPending_` (FR-025 pull).
- Either way, `flushControllerSync()` follows, which reports the compare mode the processor holds.
- Any other message goes to `AudioEffect::notify(message)`.

#### 4.5.6 `setActive(false)` (FR-033, FR-052)

Before the engine reset:

1. `latch_.releaseAll([this](std::uint8_t n) noexcept { engine_->noteOff(n); })`. Processing has stopped, as in the Vorago
   precedent (:250);
2. `compareMode_ = AB` and `compareSyncPending_ = true`;
3. `engine_->setSeed(seed_)`.

Then the reset, then `flushControllerSync()`. Separately, `setupProcessing` calls `latch_.clearWithoutRelease()` (FR-052).

#### 4.5.7 `getTailSamples` (FR-060)

```cpp
uint32 Processor::getTailSamples() {
    const double fs = tailSampleRate_.load(std::memory_order_acquire);
    if (!(fs > 0.0)) return 0u;                                                   // before setupProcessing
    const float R = std::max(bit_cast<float>(endpoints_[0].leaf[21].load()),      // ampEnv.releaseMs (leaf 21)
                             bit_cast<float>(endpoints_[1].leaf[21].load()));
    Krate::DSP::ProfundumMacroMatrix m;  m.setMacros(macroValues(macroParams_));  // stack-local, pure (:193)
    const float c = m.contributions()[std::size_t(Krate::DSP::ProfundumMacroTarget::AmpReleaseOct)];
    float rEff = R;
    if ((std::bit_cast<std::uint32_t>(c) & 0x7FFFFFFFu) != 0u) {                   // apply()'s +-0 skip (isNonZero, :245)
        const float v = R * std::exp2(c);                                         // float, as scaleTimeClamped (:264)
        if (Krate::DSP::detail::isFinite(v)) rEff = std::clamp(v, kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs);
    }
    return uint32(std::ceil((double(rEff) + 1250.0) * fs / 1000.0)) + uint32(kLimiterLookaheadSamples);
}
```

- `setupProcessing` stores `tailSampleRate_` on success; `terminate` clears it. `getTailSamples()` therefore returns 0 on
  an initialised but unprepared processor and again after `terminate`. SC-017 asserts both, so a stale sample rate cannot
  report a tail.
- Leaf index 21 is a named constant, asserted at compile time against `kLawTable[21].name == "ampEnv.releaseMs"` (a constexpr
  string-compare helper).

### 4.6 Controller (`controller.h` / `controller.cpp`)

```cpp
class Controller : public Steinberg::Vst::EditControllerEx1, public Steinberg::Vst::IMidiMapping {
public:
    tresult PLUGIN_API initialize(FUnknown*) override;            // + registerPerformance/Sound/Note/MorphControl
    tresult PLUGIN_API setComponentState(IBStream*) override;     // v1 / v2 mirror (FR-045)
    tresult PLUGIN_API setParamNormalized(ParamID, ParamValue) override;   // Edit Target hook (FR-022, FR-024)
    tresult PLUGIN_API getParamStringByValue(ParamID, ParamValue, String128) override;
    tresult PLUGIN_API notify(IMessage*) override;                // kMsgEndpoint / kMsgCompareMode (FR-025)
    tresult PLUGIN_API getMidiControllerAssignment(int32 busIndex, int16 channel, CtrlNumber, ParamID& id) override;
    void copyEndpoint(Krate::DSP::BassMorphEngine::Endpoint from);   // FR-030
    void setCompare(CompareMode m);                                   // FR-031
    [[nodiscard]] const Krate::DSP::MorphState& mirrorForTest(Krate::DSP::BassMorphEngine::Endpoint) const noexcept;
    [[nodiscard]] CompareMode compareModeForTest() const noexcept;
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IMidiMapping)
    END_DEFINE_INTERFACES(EditControllerEx1)
    REFCOUNT_METHODS(EditControllerEx1)
private:
    void redisplayEditTarget();                                  // 60 x setParamNormalized from the mirror
    std::array<Krate::DSP::MorphState, 2> mirror_{};
    CompareMode compare_ = CompareMode::AB;
    bool inStateLoad_ = false;
};
```

- **Edit Target switch (FR-022, FR-024).** `setParamNormalized(tag, v)` calls the base. If `tag == kMorphEditTargetId`, the
  index changed and `!inStateLoad_`, it calls `redisplayEditTarget()`. Then, if `componentHandler` is set, it calls
  `componentHandler->restartComponent(kParamValuesChanged)` once. This path issues no `beginEdit / performEdit / endEdit`.
- **`redisplayEditTarget`.** For each of the 60 rows it calls `EditControllerEx1::setParamNormalized(row.id,
  normalizedFromLeafWord(…mirror_[target]…))`. Sync Rate rows go through `syncRateIndexOf`.
- **`notify(kMsgEndpoint)`.** It reads `getInt("endpoint")` and `getBinary("blob", data, size)`.
  - If `size == 201 && deserializeMorphState(data, 201, local)`, then `mirror_[e] = local`. If `e` is the current Edit
    Target, it calls `redisplayEditTarget()`, with no restart and no performEdit.
  - A rejected blob leaves the mirror unchanged.
  - `kMsgCompareMode` sets `compare_`.
- **`copyEndpoint(from)`.** It sends `kMsgCopyEndpoint` with `"from"`, then calls `setDirty(true)`
  (vsteditcontroller.h:91). It does not edit the mirror locally.
- **`setCompare(m)`.** It sends `kMsgCompare`. Only the processor's reply updates `compare_`.
- **`setComponentState`:**
  1. version gate (≤ 2);
  2. global and macro blocks (the Phase 10 functions);
  3. if v == 1 (FR-044, the same reset as the processor): `mirror_ = {MorphState{}, MorphState{}}`, and IDs 410–417, 105
     and 1000–1002 are set to their registered `defaultNormalizedValue` (1001 to A, with `inStateLoad_ = true`);
  4. if v ≥ 2: the note block, then the morph-control block (IDs 105 and 1000–1002; 1001 is set with `inStateLoad_ = true`;
     105 goes through `sanitizeMorphPosition`); skip the 4-byte seed (`readInt32u`); read endpoints A and B into `mirror_`
     under the FR-044 rules (a cut or rejected endpoint keeps its prior mirror, a cut before a field keeps that field's
     prior displayed value); skip the 20-byte blob;
  5. always: hidden ID 4 → 0.5, 5 → 0, 6 → 0, 7 → 0;
  6. finally `redisplayEditTarget()`. There is no restart call, because the host is loading state.
- **`getMidiControllerAssignment` (FR-050).** `busIndex != 0` returns `kResultFalse`. On bus 0, every channel maps the same
  way: `kCtrlModWheel` → 6, `kAfterTouch` → 7, `kPitchBend` → 4, `kCtrlSustainOnOff` → 5. Anything else returns
  `kResultFalse`.
- **`getParamStringByValue`.** It tries, in order: global → macro → sound → note (with the Portamento label rule) →
  morph control → performance → the base class (lists).

### 4.7 RT-safety contract (FR-062)

| Path | Thread | Allocation / lock / exception / IO |
|---|---|---|
| Routing, passes 1 and 2, `handleSoundParamChange` | audio | none: constexpr table, atomic stores |
| `applyPendingCopy`, `applyEndpoints` (62 loads, straight-line visitor, `setEndpoint` with ≤ 11 `log2`) | audio | none |
| Pedal points, latch, bend, Mass offset, compare-aware position | audio | none (fixed arrays, bitsets) |
| `publishRuntime` (runtime-blob seqlock, audio is the writer) | audio | none (20 bytes on the stack, seqlock stores) |
| `applyPendingRestore` (restore seqlock, audio is the reader) | audio | none. One read try per block; a torn or odd read is retried next block, never spun on |
| Compare snapshot (`cmp`, read once per call) | audio | none |
| `setState`'s `publishRestore` (restore seqlock writer, `restoreWriterBusy_` spin) | non-audio | none; the spin contends only with another `setState` |
| `notify`, `setState`, `getState`, `getTailSamples`, `flushControllerSync`, controller operations | non-audio | `flushControllerSync` allocates `IMessage`s; no path from `process()` reaches it |

The audio thread never waits: every hand-off is an atomic store, an `exchange` or a single seqlock read try. The loops
over shared data (the bounded `getState` seqlock reads and the `restoreWriterBusy_` spin) run on non-audio threads only.

---

## 5. Algorithm notes

- **Leaf words as the storage unit.** One 32-bit word per leaf, rather than separate floats and ints, gives:
  - one code path for the 46 floats, 13 enums and 3 bools;
  - a copy that is a loop over 62 words;
  - trivial "bit-identical" comparisons.

  Two writers can interleave: host automation on the audio thread and `setState` on the UI thread. The result is a mix of
  leaves that are each valid, and so valid as a whole (§1.1); the next write resolves it. This is the Phase 10
  per-field-atomic semantics, unchanged. The argument does **not** extend to the 20-byte runtime blob, whose fields must
  agree with one another; that blob crosses threads only through the two seqlocks of §4.5.2.
- **Mass offset (FR-055).** `c = clamp(modWheel + pressure, 0, 1)` and `mass' = clamp(mass + c·(hi − lo), lo, hi)`, where
  `lo, hi` come from `kLawTable[52]` (`MassLaw::kMinMass` / `kMaxMass` = 0 / 100).
  - The add is skipped when `c == 0`, so the assembled leaf is bit-identical to the slot.
  - Change detection on `bit_cast(c)` marks both endpoints dirty ("a change of either marks both endpoints dirty").
  - SC-024's numbers: A 20 + 25 = 45 and B 80 + 25 → 100; c = 0.75 gives 95 / 100; c = 1.5 clamps to 1 and gives 100 / 100.
- **Last point wins, Edit Target first.** Two linear passes over the `getParameterCount()` queues, O(Q).
- **Pedal.** Vorago's per-point model, verbatim (§1.7), because SC-015 requires sample-exact equivalence (max |diff| = 0).
- **Tail.** The FR-060 formula runs in the same float precision as the engine (`std::exp2` on float, :264), then in double
  for the sample count. The reported tail and the engine's resolved release therefore agree exactly.

---

## 6. Test plan

Every case lives in `profundum_tests` unless stated otherwise. The conventions are:

- every case carries `[profundum]` and lives in namespace `ProfundumPluginTest`;
- plugin types are spelled `::Profundum::…`, and `Krate::DSP::ProfundumTest` is never opened (the fixture banner's hazard);
- no bit-exact float golden crosses binaries: every equality is same-binary and same-process;
- non-finite inputs come from bit patterns through a `volatile` (the Phase 10 rule).

### 6.1 Fixture additions (`profundum_test_fixture.h`)

- **`ControllerFixture`:** an initialised `Controller` plus a `RecordingComponentHandler`. The handler implements
  `IComponentHandler` and `IComponentHandler2`. It counts `beginEdit / performEdit / endEdit` per ID, `restartComponent`
  flags and `setDirty(true)` calls.
- **`RecordingConnectionPoint`:** an `IConnectionPoint` that records each message's ID, attributes and
  `std::this_thread::get_id()`, then forwards it to a target (`controller->notify`). It is connected as the processor's peer,
  and in the reverse direction to `processor->notify`, for SC-013, SC-014 and SC-025.
- **`randomValidMorphState(std::uint32_t seed, const RandomStateConstraints&)`:** a `std::mt19937` over every leaf. It draws
  a normalised value and writes `leafWordFromNormalized`, so the state is valid by construction.
  - Constraints for SC-011: `motion.source ∈ {Off, Env}`, `field.motionDepth = 0`.
  - Seeds: SC-010 uses A = 0x11A and B = 0x11B; SC-011 uses 0x11C / 0x11D; SC-019 uses 0x119A / 0x119B.
- **`writeV2Stream(proc)` / `withEndpoints(stream, A, B)`:** take a real `getState` stream and overwrite bytes 88 and 289
  with `serializeMorphState` output, using the layout constants from `plugin_ids.h`. No second hand-written layout exists.
- **`setParam(id, v)` / `setParamsInBlock({…})`:** helpers over the existing `MultiParamChanges` (fixture :124–193).

### 6.2 Cases

| SC | File | `TEST_CASE` (tags) | Assertion strategy |
|---|---|---|---|
| SC-005 | `unit/param_table_test.cpp` | `Profundum_ParamTableConformance` `[profundum][params]` | `getParameterCount() == 84`. A per-ID expected table in the test is compared exactly: title, units, `stepCount`, flags, default. Band check. Phase 10 IDs 0–3 and 100–103 unchanged. FR-009 strings. Shape `getParamStringByValue` at 0 / 0.5 / 1 contains neither "Bite" nor any Character label. IDs 6 / 7 hidden and not automatable; 1001 not automatable. Routed set in 400–499 == {400–403, 410–417, 420–426} |
| FR-006 | `unit/param_table_test.cpp` | `Profundum_ParamDisplayFormats` `[profundum][params]` | Controller `getParamStringByValue` compared as **exact strings**, one representative ID per `DisplayUnit`, at the normalised value `normalizedFromPlain(x)` of each chosen plain x (the double round trip is within 1e-9, so no chosen x sits on a rounding edge). Release 423: 999.9 → `"999.9 ms"`, 999.96 → `"1.00 s"`, 1000 → `"1.00 s"`, 2500 → `"2.50 s"`. Cycle Rate 904: 0.01 → `"0.01 Hz"`, 0.3 → `"0.30 Hz"`, 2.5 → `"2.5 Hz"`, 20 → `"20.0 Hz"`. Transient Attack 700: −12 / 0 / +12 → `"-12.0 dB"`, `"+0.0 dB"`, `"+12.0 dB"`. Drop Amount 400: 24 → `"24.0 st"`. Spread 801 (plain [0, 1]): 0.5 → `"50%"`. Motion Amount 902 (bipolar): −1 / 1 → `"-100%"`, `"+100%"`. Mass 104: 50 → `"50"`. Harmonic Fold 500: 100 → `"100"`. Cycle Phase 908: 90 → `"90 deg"` |
| SC-006 | `unit/param_denorm_test.cpp`: the existing case `Profundum_ParamDenormRoundTrip` (:116) is **extended with new SECTIONs**, because Phase 10 already uses the name | same | 101 points `|toNormalized(toPlain(v)) − v| ≤ 1e-9` with the **double** pair (§4.1), for every continuous row (the continuous E rows, plus 412, 1002, 105, 6, 7, 4). lo → 0.0 and hi → 1.0 exactly. Every list index → norm → index exact. `plain lo/hi == kLawTable[leaf].lo/hi` bitwise as float, except leaf 45's hi: `leafWordFromNormalized(45, 1.0) == bit_cast(nextafter(360.f, 0.f))`. Default round trip `leafWordFromNormalized(leaf, defaultNorm) == kDefaultLeafBits[leaf]` for the 62 leaves (FR-003) |
| SC-016 | `unit/param_table_test.cpp` | `Profundum_PortamentoLabelFollowsGlideMode` `[profundum][params]` | Set 414 to 0, 0.5 and 1 on the controller; format 412 at 0.3 → suffix `" ms"`, `" ms/oct"`, `" ms"`. The value string is identical apart from the suffix |
| SC-007 | `integration/param_wiring_test.cpp` | `Profundum_DefaultsMatchEngine` `[profundum][params]` | Fresh prepared processor, one 512-sample block: `leafBitsEqual(getEndpoint(A/B), MorphState{})`; every getter (:387–397) equals the engine default; `endpointRejectionsForTest() == 0`. **SECTION "host re-sends defaults" (FR-003):** on a prepared processor, every registered parameter's `getInfo().defaultNormalizedValue` (read from a controller) is sent in one block, then one more block renders. Every engine getter then equals its default **bitwise** as float: `morphRampMs() == 40.0f`, `morphPosition() == 0.0f`, `anchorMorphMode() == Blend`, `portamentoTimeMs() == 0.0f`, `pitchBendRange() == 2.0f`, `pitchBend() == 0.0f`, and the six enum / bool getters at their `Settings` values. Both endpoints are `leafBitsEqual` to `MorphState{}`, under Edit Target A (the default) |
| SC-008 | `integration/param_wiring_test.cpp` | `Profundum_EveryParamReachesEngine` `[profundum][params]` | An explicit per-ID oracle table of 72 rows. For each: norm 0 → block → read-back == lo / first entry; norm 1 → block → hi / last entry. Cycle Phase reads `nextafter(360, 0)`; Sync Rate checks both leaves against `kNoteValueDropdownMapping[0]` / `[29]`; Pitch Bend reads −1 / +1. Cross-talk: `leafBitsOf` of the other 61 / 62 leaves identical before and after. 1001 via the SC-009 oracle. The counter reaches 72 + 1; ID 5 is covered by SC-015 and IDs 6 / 7 by SC-024, whose case names the close lane checks in `--list-tests`. **SECTION "Sync Rate, all 30" (FR-012)**, for both 809 and 907: for idx = 0 … 29, send `idx / 29.0` → one block → both engine leaves equal `kNoteValueDropdownMapping[idx]` (note and modifier); `syncRateIndexOf(word(leaf), word(leaf2)) == idx`; and, through the connected controller after `flushControllerSync()`, `getParamNormalized(id) == idx / 29.0` exactly |
| SC-009 | `integration/param_wiring_test.cpp` | `Profundum_EditTargetAddressing` `[profundum][params]` | (1) A Depth edit changes A only. (2) Two processors, same stream, one switches 1001 → 1: every engine leaf `leafBitsEqual` and every rendered sample bit-identical. (3) One block with queues `[200 = 0.9, 1001 = 1]` in that order → B's depth is the mapped 0.9, A unchanged. (4) Controller: after `setParamNormalized(1001, 1)`, the 60 values equal the B mirror normalised within 1e-9. (5) `performEdit / beginEdit / endEdit` count for the 60 IDs == 0. (6) `restartComponent(kParamValuesChanged)` count == 1 per switch, issued after the 60 sets (the handler records the order) |
| SC-010 | `unit/state_v2_test.cpp` | `Profundum_StateRoundTripV2` `[profundum][state]` | Setup per spec: random A / B, non-default note fields, ramp 1 → 0.5 at the 40 ms ramp, target B, seed 0x1234, 2 s. `getState` (510 B) → a fresh processor's `setState` **before** `setupProcessing` / `setActive(true)` → one block → `getState`: `memcmp == 0` over 510 B; endpoints `leafBitsEqual`; `morphPosition()` equal; blob bytes 490–509 equal; `restoreApplyCountForTest() == 1`. The controller's `setComponentState` displays B within 1e-9, **and** `getParamNormalized` of 410–417, 105 and 1000–1002 equals the saved value's normalised form (exact for lists and stepped rows, 1e-9 for 412, 1002 and 105). A Depth edit after the restore lands in B. **Tiny-position arm (FR-041, R10):** position set through the 105 handler to `1e-20f`, and in a second run to the denormal bit pattern `0x00000001` (through a `volatile`), with a ramp to it rendered and settled. The stored position reads `+0` (bit pattern 0); save → fresh `setState` → one block gives `restoreApplyCountForTest() == 1` (the blob was accepted and applied, not rejected); save-load-save is byte-identical over 510 B. The same two values written straight into the position field of a hand-edited stream (bytes 68–83) give the same result |
| SC-012 | `unit/state_v2_test.cpp` | `Profundum_StateVersions` `[profundum][state]` | **Prior state P** (every arm): a non-default v2 state with random A / B (0x12A / 0x12B), non-default note fields, position 0.7, ramp 200 ms, Punch, Edit Target B and seed 0x5EED, rendered 1 s, so the runtime blob has been published. **(1) v1 arm (FR-044 reset).** `setState` of a v1 36-byte stream (header `kStateV1Version`), then one block. Globals and macros equal the stream. Both slots and both engine endpoints are `leafBitsEqual` to `MorphState{}`. Every note and morph-control atomic and engine getter is at its default, and Edit Target is A. `getState` bytes 84–87 == `kDefaultSeed`. `getState` bytes 0–489 equal those of a fresh processor that loaded the same v1 stream and rendered one block. Bytes 490–509 are excluded here: the prior engine's p is still slewing to 0 (§4.5.3), which is inaudible. A twin controller holding P's state then gets `setComponentState(v1)`: its mirror is `MorphState{}` for both endpoints, and 410–417, 105 and 1000–1002 display their defaults. **(2) Cut arm, k = 0 … 509 (FR-044 keep).** `setState(cut_k)` of a different non-default v2 stream S. **Before any block**, `getState` bytes 0–489 follow the field oracle: fields fully inside `[0, k)` take S's value, all others P's, and an endpoint needs all 201 bytes. Bytes 490–509 equal P's last published blob, because no loaded blob is pending (§4.5.2 precedence 2). Then one block under `AllocationScope` (count 0). The engine's `serializeMorphRuntime` (via `engineForTest()`) must then equal that of a **twin** processor with the identical history P, which received a full v2 stream holding the oracle's expected explicit fields and endpoints and a blob whose version byte is 0xFF. That blob is rejected, so FR-042 step 3 is skipped in the twin, and equality proves no runtime restore was applied for k < 510. `restoreApplyCountForTest()` is unchanged. **The post-block bytes 490–509 are never part of the byte oracle**: they are the live engine blob (§4.5.2 precedence 2). **(3) k = 510.** S's blob comes from a source processor saved mid-ramp (p ≠ target). Before any block, `getState` bytes 490–509 equal S's blob (pending-blob precedence, FR-041 amendment §10). After one block, `restoreApplyCountForTest()` has risen by 1 and `lastRestoreBlobForTest()` equals S's blob. A precondition asserts that the 0xFF-blob twin's engine blob differs from the restored engine's in p, p_a or the mask byte, so the arm cannot pass vacuously. **(4) Controller cut arm (FR-045).** On a controller holding P's state, `setComponentState` of S cut at k ∈ {50 (note block), 150 (endpoint A), 300 (endpoint B), 500 (the blob)}, and of a full S with endpoint A's version byte flipped. The mirror and the displayed 410–417, 105 and 1000–1002 values follow the FR-044 rule: complete fields take S's value, and the rest, including a cut or rejected endpoint, keep P's. **(5) Unchanged items.** The pre-render blob of a fresh processor equals the hand-built synthesized blob. For a Blend / position-0 processor it also equals a real engine's `serializeMorphRuntime` after `prepare`, which proves the re-stated offsets. Version 3 → `kResultFalse` with `getState` unchanged. A flipped endpoint version byte, or a bool byte of 2, leaves the prior endpoint |
| FR-042 | `unit/state_v2_test.cpp` | `Profundum_RestoreHandOffNoTear` `[profundum][state][rt]` | Two full v2 streams S1 and S2 with identical explicit fields and endpoints but different blobs. Each blob is saved mid-ramp, so words 1–4 differ (p and p_a inside the hysteresis band, mask 0x0F vs 0x00); word 0 is equal, since it carries the version byte and the low target bytes (build note T018). For 2000 rounds, `process()` runs on a dedicated thread for 8 blocks while the test thread calls `setState(S1)` and then `setState(S2)`. After the join, `lastRestoreBlobForTest()` equals S1's blob or S2's blob byte for byte, never a mix; after one more block it equals S2's (no lost update). `AllocationScope` on the audio thread only: count 0 |
| SC-011 | `integration/state_restore_render_test.cpp` | `Profundum_StateRestoreRender` `[profundum][state][long]` | Spec setup; save after `getTailSamples()` of silence; host restore order; identical new-note stream (E1 MONO, 2 s). `bandLevelDifferenceDb`, aggregate mode, over [100 ms, 2.1 s) ≤ 0.1 dB; mask byte equal. Control arm: blob version byte 490 forced to 0xFF in the stream → restored mask byte 0x00 against the original 0x0F |
| SC-013 | `integration/copy_compare_test.cpp` | `Profundum_CopyEndpoint` `[profundum][morph]` | Connected pair; `copyEndpoint(A)`; one block; `flushControllerSync()`. The processor's B `leafBitsEqual` A; `mirrorForTest(B)` equals A, and the recorder shows it came from a `kMsgEndpoint` message; `setDirty(true)` count 1; 1001 and 105 unchanged. The same for B → A |
| SC-014 | `integration/copy_compare_test.cpp` | `Profundum_CompareDoesNotWriteState` `[profundum][morph]` | Position 0.6, `setCompare(A)`. Band-level difference against a position-0 reference over [140 ms, 1140 ms) ≤ 0.1 dB. `getState` byte-identical to the pre-compare stream. After AB, `morphPosition() == 0.6f`. `setState` → `compareModeForTest() == AB`. Second arm: compare B → `setActive(false)` → AB |
| SC-025 | `integration/copy_compare_test.cpp` | `Profundum_ProcessorControllerSync` `[profundum][morph]` | `process()` runs on a dedicated `std::thread` (the "audio" thread) and `flushControllerSync()` on the test thread. An unobserved Depth edit → mirror leaf == slot, bitwise. Copy → exactly one 201-byte message, for B. `setState` → two blobs and compare AB. Compare B + `setActive(false)` → an AB message. Every recorded send thread id ≠ the audio thread id. Mod Wheel / Pressure moves → 0 endpoint messages. `AllocationScope`, with `tAllocationTrackThisThread` set on the audio thread only, around the processed blocks → 0. **Timer arm (production path):** an unobserved Depth edit, one block on the audio thread, then `proc->onTimer(nullptr)` on the test thread, never `flushControllerSync()` directly. The mirror's depth leaf then equals the slot. `hasSyncTimerForTest()` is true after `initialize` on Windows and macOS. On Linux that check is `SKIP`ped with the reason "SDK Timer::create returns nullptr without a factory (timer.cpp:346–351)"; the `onTimer` assertion still runs there. After `terminate`, `hasSyncTimerForTest()` is false on every OS |
| SC-015 | `unit/midi_mapping_test.cpp` | `Profundum_MidiMapping` `[profundum][midi]` | The assignment table on channels 0 and 15; bus 1 and CC 7 → `kResultFalse`. Bend 1.0 with range 12 → `pitchBend() == 1`, `pitchBendRange() == 12`; the voice-0 `fCore` tap's mean over [200, 700) ms after against before gives `12·log2(ratio)` within 12 st ± 0.05 st. The sustain equivalence pairs and the multi-point queue per spec, max |diff| == 0, including the slicing stand-ins. Reset on load. `getState` bytes independent of pedal and bend |
| SC-024 | `unit/midi_mapping_test.cpp` | `Profundum_MassPerformanceOffset` `[profundum][midi]` | The spec arithmetic with exact float compares (45, 100, 95, 100, 100, 100, 50, 50). Slot words and `getState` unchanged. Mirror Mass == the persisted value. Reset on load. This TU renders no audibility arm and includes no `dsp/tests` helper (R7) |
| SC-024 | `integration/param_path_audibility_test.cpp` | `Profundum_MassOffsetAudibility` `[profundum][audibility][long]` | The SC-024 audibility arms, moved out of the per-push TU: the ported AT-27 descriptor (§6.4) ≥ its CSV bar at positions 0, 0.5 and 1 after settling, with the offset driven only through IDs 6 / 7. The case's wall clock is recorded at its first run in the gate log |
| SC-017 | `integration/tail_test.cpp` | `Profundum_TailCoversRelease` `[profundum][tail][long]` | `getTailSamples() == 0` on an initialised, unprepared processor, and again after `terminate()` (FR-060). Formula equality for {100, 5000} ms × 44.1 / 48 / 96 kHz, neutral macros. The Tight↔Loose 1.0 arm equals the formula with `contributions()` and exceeds the neutral value. 8 render arms (macro × release pair × position 0 / 1; E1 MONO; 48 kHz): after the note-off, every sample beyond `getTailSamples()` satisfies `== 0.0f` on both channels (+0 and −0 both compare equal) |
| SC-018 | `integration/param_path_audibility_test.cpp` | `Profundum_ParamPathAudibility` `[profundum][audibility][long]` | The 15 rows of the spec table, at every gated (row, note). Base loaded via `withEndpoints` + `setState`; the lever moved only by a parameter change; the ported descriptor read on the row's tap through `setEngineObserverForTest`; reading ≥ the CSV `bar`; `out_descriptor` recorded. The CSV goes to `specs/profundum-phase11-parameters/evidence/param-path-audibility.csv`, with the Phase 9 `value` beside each reading |
| SC-019 | `integration/mono_sum_params_test.cpp` | `Profundum_MonoSumViaParams` `[profundum][mono][long]` | Grid SOLID / SPREAD / WIDE × MONO / 4 voices at E1, distinct A / B. ID 800 written under target A and under target B. Position ramped 0 → 0.5. `StereoFieldTest::bandReading(…, 0, f_c)` on the field and output taps, gated as in Phase 9 (`valid`, sub in band): `|Q| ≤ 1.0 dB`, `ρ ≥ 0.99`. SOLID: L == R bitwise from block start + `ceil(10 ms · fs / 1000) + 64`. Every voice's `anchorL == anchorR` bitwise in every chunk, in every mode |
| SC-020 | `integration/processor_cpu_test.cpp` | `Profundum_ProcessorCpuFullSurface` `[profundum][.perf][performance]` | Extends the Phase 10 interleaved harness (:54–200): arms P / D × {MONO, POLY, MAX-MONO, MAX-POLY} with distinct endpoints, position 0.5 and Motion on, plus the automation arm (each block is `[1001 = k % 2, 200 = v_k]`) at neutral and at macros 1.0. Bars 8 % / 25 %, ratio ≤ 1.05×; every percentage printed |
| SC-021 | `unit/phase11_alloc_test.cpp` | `Profundum_Phase11PathsDoNotAllocate` `[profundum][rt]` | 64 blocks under `AllocationScope`, exercising every FR-062 path: all 84 IDs routed, a copy requested via `notify` before the scope, compare, a pending `setState` hand-off, a multi-point pedal, bend, the Mass offset, sync flags set. Count == 0 |
| SC-022 | `integration/determinism_test.cpp` | `Profundum_Determinism` `[profundum][determinism]` | Two processors, identical state and stream → max |diff| == 0. Drift arm (900 = Drift, 901 = Mass, 902 = 1, 903 = 1; E1 MONO; 4 s): the seed changed through the state stream plus `setActive(false/true)` → the frame-wise band difference over [0.5, 4) s ≥ 0.5 dB somewhere; the same-seed arm reads exactly 0 |
| metric | `integration/band_level_metric_test.cpp` | `Profundum_BandLevelMetricSelfTest` `[profundum][metric]` | Identical signals → 0 dB. A 100 Hz sine scaled by 2 → 6.02 dB ± 0.01 in the band [80, 160). A band below −100 dB in both renders is excluded |
| FR-061 | `plugins/shared/tests/test_sustain_latch.cpp` | `SustainLatch_SharedSemantics` `[shared][midi]` | The Vorago unit semantics against `Krate::Plugins::SustainLatch`: latch while the pedal is down, ascending release on pedal-up, a re-strike clears the mark, `releaseAll`, `clearWithoutRelease`, notes ≥ 128 pass through |

SC-001 (warnings, portability), SC-002 (suites), SC-003 (pluginval), SC-004 (auval, the macOS CI step of §7.2), SC-023 (the
close lane) and SC-026 (the `grep`) are verified by the commands in §7, not by test cases.

### 6.3 `tests/band_level_metric.h` (test-only)

```cpp
namespace ProfundumPluginTest {
struct BandLevelReading { std::array<std::array<double, 5>, 2> db{}; };   // [M/S][band]
inline constexpr std::array<double, 6> kBandEdgesHz{20, 80, 160, 320, 1280, 20000};   // last edge: min(20000, fs/2)
[[nodiscard]] BandLevelReading bandLevels(std::span<const float> L, std::span<const float> R, double fs);  // Welch
[[nodiscard]] double bandLevelDifferenceDb(std::span<const float> L1, std::span<const float> R1,
                                           std::span<const float> L2, std::span<const float> R2,
                                           double fs, bool frameWise);
}
```

- **Welch estimate:** `Krate::DSP::FFT` at size 8192 (fft.h:147, :186), a Hann window and 50 % overlap. Bin power is summed
  per band, and the level is `10·log10(E + 1e-20)`.
- **Aggregate mode:** one reading over the window. The difference is the max over 5 bands × {M, S} of |ΔdB|. A band is
  excluded when **both** readings are below −100 dB re full scale; full scale is a 0 dBFS sine's band level, computed once
  in the metric.
- **Frame-wise mode:** one reading per 8192-sample frame (50 % hop); the difference is the max over frames × bands × {M, S}.
- Allocation happens on the test side only.

### 6.4 SC-018 descriptor port (`tests/param_path_audibility_helpers.h`)

- **What is ported.** The fifteen descriptors are ported **verbatim** from the anonymous namespace of
  `dsp/tests/unit/systems/profundum_engine_audibility_test.cpp`: for example `auWindowESubDb` :464, `auLkOutShort` :784,
  AT-27 :1496–1508, AT-01 :1066–1074, AT-38 :1816, AT-42 :1913–1919, AT-43 :1994. Each carries a
  `// ported from …:<line>` comment.
- **What is included.** FR-070 forbids edits to `dsp/`, not includes, so shared public helpers are **included read-only**:
  - `profundum_voice_engine_test_helpers.h` (`voiceEngineAs/Bs`, frame readers, K-weighting);
  - `bass_stereo_field_test_helpers.h` (`bandReading`).

  They are reached through the test include directory `${CMAKE_SOURCE_DIR}/dsp/tests/unit/systems`. The helpers include
  `"../processors/…"` relative to themselves, which resolves.
- **Fidelity teeth.** Each row is also evaluated engine-direct: a `ProfundumEngine` driven exactly as Phase 9 drove it, with
  the same helper headers. The port's reading must be within 10 % (relative, in descriptor units) of the CSV `value`
  column. A larger gap fails as "descriptor port drift", before the processor-path reading is trusted.
- **Bars.** The bars come from the CSV `bar` column of each row. They are transcribed into a constexpr table in the helper,
  with the CSV line cited, and never re-derived.

### 6.5 Amended Phase 10 tests

State v2 and `IMidiMapping` make these assertions wrong; they are not flaky.

| File:line | Today | Change |
|---|---|---|
| `unit/lifecycle_test.cpp:523` | `IMidiMapping` → `kNoInterface` | → `kResultOk`: FR-050 removes Phase 10 FR-019's exclusion. `INoteExpressionController` stays `kNoInterface` |
| `unit/lifecycle_test.cpp:238, :247, :491, :501` | writes `kCurrentStateVersion` + a v1 body; size == `kStateV1Bytes` | v1 streams write `kStateV1Version`; the processor's `getState` size == `kStateV2Bytes` |
| `unit/state_roundtrip_test.cpp:93, :112, :233–281, :426–433` | `ByteIdentical36` against `getState` | v1-format sections build v1 streams with `kStateV1Version`. `getState` comparisons become: size 510, and bytes 4–35 equal the v1 body |
| `unit/param_denorm_test.cpp:233` | `getParameterCount() == 8` | `== 84`; the per-parameter loop (:236) is reviewed for the new IDs |
| `integration/param_flow_test.cpp:385` | header `kCurrentStateVersion` + v1 body | write `kStateV1Version` (the intent is a Phase 10 stream) |
| `unit/controller/editor_lifecycle_test.cpp:199` | 8 uidesc controls | unchanged (FR-071: no new UI) |

Each amendment keeps the case's behavioural assertion. None is deleted or relaxed.

---

## 7. Build integration and gates

### 7.1 CMake

- **`plugins/profundum/CMakeLists.txt`** (the :40–41 block): add `sound_param_row.h`, `sound_param_table.h`,
  `core_params.h`, `anchor_params.h`, `note_params.h`, `fold_shape_params.h`, `character_params.h`, `dynamics_params.h`,
  `field_params.h`, `motion_params.h`, `morph_params.h` and `performance_params.h`. The plugin gains no new `.cpp`.
- **`plugins/profundum/tests/CMakeLists.txt`:**
  - add `unit/param_table_test.cpp`, `unit/state_v2_test.cpp`, `unit/midi_mapping_test.cpp`, `unit/phase11_alloc_test.cpp`,
    `integration/param_wiring_test.cpp`, `integration/state_restore_render_test.cpp`, `integration/copy_compare_test.cpp`,
    `integration/tail_test.cpp`, `integration/param_path_audibility_test.cpp`, `integration/mono_sum_params_test.cpp`,
    `integration/determinism_test.cpp` and `integration/band_level_metric_test.cpp`;
  - add every new TU except the CPU one to the `-fno-fast-math` list (:61–70);
  - add `${CMAKE_SOURCE_DIR}/dsp/tests/unit/systems` to `target_include_directories` (for §6.4 and SC-019 only).
- **Shared:** `plugins/shared/CMakeLists.txt` gains `src/midi/sustain_latch.h`; `plugins/shared/tests/CMakeLists.txt`
  (:7–38) gains `test_sustain_latch.cpp`.
- **clang-tidy:** reconfigure `build/windows-ninja` after the test-CMake change and before clang-tidy (the stale compile
  database hazard).

### 7.2 Targets and run plan (parallel by default; CPU alone)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target Profundum Vorago profundum_tests shared_tests vorago_tests -- /nr:false
```

One build covers all five targets, which share the KrateDSP and shared objects. Then these run **concurrently**, each
logging to a file whose wall clock is recorded for the next gate:

- `profundum_tests.exe "~[long]~[.perf]"` and `shared_tests.exe`;
- `vorago_tests.exe "Vorago_SustainLatchUnit,Vorago_SustainLatch"`: the moved class's own two cases only (pure code move, user ruling 2026-10-22; never the full suite);
- the five `[long]` Profundum cases as five separate processes, by name: `"Profundum_ParamPathAudibility"`,
  `"Profundum_MassOffsetAudibility"`, `"Profundum_MonoSumViaParams"`, `"Profundum_TailCoversRelease"`,
  `"Profundum_StateRestoreRender"`;
- pluginval on `Profundum.vst3` and on `Vorago.vst3`;
- `node tools/check-portability.js` on every new or changed `.cpp`;
- clang-tidy `-Target profundum`, `shared` and `vorago`.

Afterwards, **alone and last**: `node tools/run-cpu-tests.js profundum_tests` (SC-020). No `dsp/` header is modified, so no
DSP consumer lane runs (SC-023).

**SC-004 (auval).** This cannot run on Windows. It is verified by the macOS CI job's existing Profundum step,
`auval -v aumu Prfd KrAt`, which must report `AU VALIDATION SUCCEEDED`. Until the branch is pushed (pushing needs the
user's permission), the compliance row reads **PENDING-CI**, as in the Phase 10 precedent. The row is closed by citing
the CI run's annotation or log line.

### 7.3 Implementation order (each step test-first)

1. Shared latch move, alias and `SustainLatch_SharedSemantics` → `shared_tests` and the two Vorago sustain cases green.
2. `plugin_ids.h`, `sound_param_row.h`, the packs and `sound_param_table.h` → SC-005, SC-006, SC-016 and
   `Profundum_ParamDisplayFormats` (controller only).
3. Processor routing, slots, assembly, and the note / morph / performance push → SC-007, SC-008, SC-009, SC-024.
4. State v2, both seqlocks, restore, position sanitising and seed → SC-010, SC-012 and `Profundum_RestoreHandOffNoTear`,
   then SC-011 and SC-022; amend the Phase 10 tests (§6.5).
5. Copy, compare, the sync sender and controller `notify` → SC-013, SC-014, SC-025.
6. `IMidiMapping` and the pedal → SC-015. Tail → SC-017; run the exact-zero render arms first (Risk R3).
7. The audibility port and the mono test → SC-018 and SC-019; then SC-021; then SC-020, alone.
8. Docs:
   - the roadmap band map at line 781 (FR-072 / SC-026);
   - `plugins/profundum/CLAUDE.md`: band table :32, state v2, 84 parameters, `IMidiMapping`, new test cases;
   - the `README.md` parameter section, with the target-relative automation note (FR-023);
   - `CHANGELOG.md` bullets under the unreleased 0.1.0 entry.

---

## 8. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R1 | SC-006's 1e-9 is below float resolution (about 6e-8 at 1.0) | The mapping pair is double end to end (§4.1). The float cast happens only when a leaf word is written, and SC-006 tests the double pair |
| R2 | A log row's default may not survive normalised → plain → float bit-exactly (FR-003) | Defaults are computed from `kDefaultLeafBits`. SC-006 pins the round trip for all 62 leaves. A failing row gets the bounded `nextafter` nudge at registration (§4.1); the engine default never changes |
| R3 | SC-017's exact zero after `ceil((R_eff + 1250)·fs/1000) + 64` assumes the voice is silent at `R_eff`. D-1 measured exact zero 1.25 s after voice silence (roadmap Phase 10 status block). If `ADSREnvelope`'s release ends later than `releaseMs`, the case goes red | Run the 8 render arms early (step 6), as the first SC-017 execution. A non-zero sample is reported to the user, as the spec requires; the 1250 ms term is not re-fitted |
| R4 | `Steinberg::Timer::create` returns nullptr on Linux (timer.cpp:346–351), so an unobserved edit reaches the mirror only at the next notify / state / activation call | Most tests drive `flushControllerSync()` explicitly, which is deterministic on every OS. SC-025's timer arm covers the production path (`onTimer`, timer creation on Windows / macOS, release on terminate). The Linux gap is a blocking ruling (Open Question 2, §10), and the outcome is recorded in the FR-025 row before the build. It is also documented in `README.md` |
| R5 | Seqlock memory-ordering mistakes are silent | The Boehm form with explicit fences, in both directions (§4.5.2). SC-010 and SC-012 byte-compare the blob. `Profundum_RestoreHandOffNoTear` races two `setState` calls against a running `process()` thread. The `getState` reader is bounded and falls back; the audio reader makes one try per block and retries next block |
| R6 | `detail::morph_state_io::forEachMorphLeaf` is a `detail` API | It is the only leaf visitor, and using it keeps the plugin in step with `leavesMatchTable()`. A DSP-side rename would break the build, not the behaviour |
| R7 | Including `dsp/tests/unit/systems/*_test_helpers.h` pulls the Phase 1–9 helper graphs into `profundum_tests` (compile time, namespace hazard) | Only the two `[long]` TUs include them (`param_path_audibility_test.cpp`, which now also holds the SC-024 AT-27 arms, and `mono_sum_params_test.cpp`; `midi_mapping_test.cpp` includes none), with namespace qualification only (no `using namespace Krate::DSP::ProfundumTest`). If a helper needs a symbol that is not in a header, that function is ported into `param_path_audibility_helpers.h` instead; still no `dsp/` edit |
| R8 | The ported descriptors drift from Phase 9's | The engine-direct fidelity check (§6.4, 10 % of the CSV value) fails before the processor path is trusted |
| R9 | Portability: constexpr `std::bit_cast`, designated initializers, `char16_t` tables, `snprintf` truncation warnings, `std::nextafter` under FTZ | All are standard C++20. Buffers are `char8 text[64]`. `nextafter(360.f, 0.f)` is a normal float, so FTZ does not affect it. `check-portability.js` and the 3-OS CI cover the rest. No `std::isnan`: finiteness uses the bit-pattern `detail::isFinite` |
| R10 | Denormals, tiny values or −0 written into slots **or into the Morph Position** | Slots: `v <= 0` snaps to `lo` (+0), so the plugin never writes −0. A tiny positive linear value may be denormal; `isValidMorphState` accepts it (comment above :513), and the engine runs under `ScopedDenormalMode`. Position: the engine flushes `p < 1e-15f` to +0 (`bass_morph_engine.h:139–146`), and `restoreRuntime` rejects a blob with `0 < |x| < 1e-15` (:270–300). A raw stored 1e-20 would therefore poison the synthesized blob and fail step 9's bitwise target compare. `sanitizeMorphPosition` (§4.2) applies the engine's rule before storing. SC-010's tiny-position arm (1e-20 and a denormal) requires the restore to be applied and the stream to be byte-stable |
| R11 | Automation-arm CPU: `setEndpoint` every block (≤ 11 `log2`, bass_morph_engine.h:111) plus the 62-word assembly | SC-020's automation arm measures it against the unchanged bars, which are never relaxed |
| R12 | Phase 10 tests that assert the v1 size, or the absence of `IMidiMapping`, go red the moment v2 lands | §6.5 lists every site. They are amended in the same step that bumps the version |
| R13 | A host re-sends every parameter after `setState`. Each E-parameter lands on the Edit-Target endpoint after a normalised round trip, and may move a log leaf by ≤ 1 ulp | Bounded by SC-006 (1e-9 normalised) and inaudible. The processor's endpoint message then re-syncs the mirror |
| R14 | `restoreRuntime` rejects the FR-041 synthesized blob when the position is ≥ `kThetaUp` (all-A contradicts the rule), or when a rule-2 endpoint pair forces B | The rejection is harmless under FR-042: the explicit fields stand. SC-012 covers the bytes, not the acceptance |

---

## 9. Open questions (left to the user)

**All ruled 2026-10-22 (spec Clarifications Q9–Q11):** OQ-1 → `p_a = curve(mode, position)`; OQ-2 → pull on Edit Target
switch plus the 50 ms timer on Windows / macOS (FR-025); §10's FR-041 and FR-006 amendments accepted and written into the rows.

1. **The FR-041 synthesized blob's `p_a`.** The spec writes `p_a = position`, but the engine itself holds
   `p_a = curve(mode, p)` (`bass_morph_engine.h:188`; Lock gives 0, Punch gives p³).
   - Consider a project saved before any render and restored in Lock or Punch mode, at a position below `kThetaUp`. The
     restore would snap `p_a` to `position`, which would then slew to `curve(mode, p)` over the ramp: a short Anchor morph
     glide on load.
   - Options: keep the spec's `p_a = position` (the plan's default), or use `BassMorphEngine::curve(mode, position)`. The
     two are identical in Blend, the default mode.
2. **Controller sync on Linux (BLOCKING: must be ruled before the build stage).** Linux has no SDK timer, so an endpoint
   edit that arrives only through automation reaches the controller mirror at the next notify / state / activation call.
   - Option 1 (the plan's default): accept this documented gap for Phase 11, and amend FR-025 with the text in §10.
     Phase 12's editor timer can call a sync request later.
   - Option 2: require a different transport now.

   The project rule puts a ruling in the FR/SC row first. The ruling, whichever it is, goes into the FR-025 row of
   `spec.md` before the build stage starts.

---

## 10. Spec amendments this plan needs (to be written into the FR rows before the build)

**Applied 2026-10-22** to FR-006, FR-025 (pull-on-switch ruling, which includes the option-1 Linux sentence) and FR-041.

The plan cannot rule these by itself. Each needs the user's ruling recorded in the named FR row; the build stage checks
the row first.

| FR | Plan behaviour | Proposed amendment text |
|---|---|---|
| FR-025 | On Linux an unobserved edit reaches the mirror at the next notify / setState / getState / setActive call (no SDK timer, `timer.cpp:346–351`) | Under option 1, append: "On Linux, where the SDK provides no timer, an edit the controller does not observe reaches the mirror at the next `notify`, `setState`, `getState` or `setActive` call; Phase 12's editor timer closes the gap." Under option 2, a new transport is planned before the build |
| FR-041 | A loaded blob that is still pending (`setState`, no render yet) takes precedence over the synthesized blob in `getState` (§4.5.2 precedence 1), so load-then-save is byte-stable | Insert before "Before the first rendered block": "A runtime blob loaded by `setState` and not yet applied is written as loaded." SC-012 arm (3) pins it |
| FR-006 | "Two significant decimals below 1 Hz" is implemented as two decimal places (`"%.2f Hz"`, §4.3) | Replace with "Hz with two decimal places below 1 Hz (one decimal at 1 Hz and above)" |

FR-044 needs no amendment: §4.5.3 implements the v1 reset as written. FR-005 needs none either: the packs carry their own
register / handle / format entry points (§4.2).

---

## Review notes

Review round of 2026-10-22. Every issue was applied in full except for the parts below, which the plan stage cannot
settle by itself:

- **FR-025 / SC-025 (major), "get the Open Question 2 ruling recorded in the FR-025 row before the build."** The test arms
  were added (§4.5.4, §6.2 SC-025). The ruling itself belongs to the user, and the plan cannot write it. It is marked
  BLOCKING in Open Question 2, with proposed FR-025 text in §10. It is not decided silently.
- **FR-041 (minor), "amend FR-041 or record a ruling."** The plan keeps the pending-loaded-blob precedence, as the review
  judged it the better behaviour, and adds the SC-012 arm (3). This stage edits `plan.md` only, so the FR-041 text change
  is listed in §10 for the ruling and is not made in `spec.md`.
- **FR-006 "%.2g" (part of a major).** The review asked for the strings to be pinned. Pinning them showed that `"%.2g"`
  does not give a fixed digit count, so the plan changes the format to `"%.2f"` and lists the reading of FR-006's wording
  in §10. No threshold was relaxed.
- **FR-032 compare snapshot (minor).** Applied (§4.5.1 step 1). No dedicated race test was added: the defect needed a
  `notify` landing between two reads in one `process()` call, and with a single read per call that window no longer
  exists. The single read is a structural property, checked in code review.
