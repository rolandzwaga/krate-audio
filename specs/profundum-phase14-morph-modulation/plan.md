# Implementation Plan: Profundum Phase 14 — Morph Modulation

**Spec:** `specs/profundum-phase14-morph-modulation/spec.md` (reviewed; Clarifications session 2026-10-26, Q1–Q8, C1, C10)
**Roadmap:** `specs/Profundum-roadmap.md` Part B, Phase 14 (`:1075-1115`), the Phase 8 handoff (`:575-609`), the Phase 9 status and
handoff (`:647-744`), cross-cutting constraints (`:1135-1158`)
**Branch:** `feat/profundum-preset-morph`, HEAD `3dfc5b7d` when this plan was written
**Date:** 2026-10-26
**Status:** PLAN (revised after the plan challenge, 2026-10-26). Nothing below is implemented. Nine plan-stage findings (§2,
P-1 … P-9) amend or sharpen spec rows; each is written into its FR/SC row first, then Clarifications, before the first task that
depends on it (P-9 after the user rules §11 Q2). Every file:line cited below was read in this planning session.

---

## 0. Summary

Phase 14 adds **one KrateDSP class** (`ProfundumMorphModulator`, L3), **append-only edits to two Profundum engine headers**, an
append-only edit of the Phase 9 test driver, and the plugin surface (nine parameters 1003–1011, a 36-byte block in state v3, the
Mod Wheel hand-over, the Compare bypass, the editor and a live resolved-position indicator), plus the factory-library conversions.

| # | Deliverable | Where | Lane that proves it |
|---|---|---|---|
| 1 | `ProfundumMorphModulator` (Envelope / Cycle / Velocity / Mod Wheel at fs / 32, one offset per interval) | `dsp/include/krate/dsp/systems/profundum_morph_modulator.h` (new) | `dsp_systems_tests [morphmod]` |
| 2 | Engine sub-step 1b, base position, modulation setters, restart hook, blob-target patch, read-backs | `systems/profundum_engine.h` (append-only) | `dsp_systems_tests [morphmod]` + every Profundum consumer TU |
| 3 | Three appended grid-record fields | `systems/profundum_engine_observer.h` (append-only) | same |
| 4 | Driver: appended script fields + three appended event kinds + three appended grid-tap fields | `dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h` (append-only) | `VoiceEngineHelpers_MorphModFields` |
| 5 | Parameter pack (atomics, laws, registration, display, stream, controller mirror) | `plugins/profundum/src/parameters/morph_mod_params.h` (new) | `profundum_tests` per push |
| 6 | IDs 1003–1011, `kNumRoutedIds` 1012, `RouteKind::MorphMod`, state v3 = 610 bytes | `plugin_ids.h`, `parameters/sound_param_table.h` | per push |
| 7 | Processor: routing, C-7 Mass rule, push, C-8 bypass, state block | `processor/processor.h`, `processor.cpp` | per push |
| 8 | Controller: registration, display, mirror, indicator plumbing | `controller/controller.h`, `controller.cpp`, `ui/panel_sub_controller.cpp` | per push |
| 9 | Frame + producer: resolved position at offset 288, frame 320 bytes | `processor/spectrum_frame.h`, `processor/spectrum_producer.h`, `processor.cpp:352-355` | per push |
| 10 | `MorphIndicatorSlider` (display-only marker on the Main-page Morph slider) | `plugins/profundum/src/ui/morph_indicator_slider.h/.cpp` (new) | per push |
| 11 | Editor: Main `morph-band` (1003, 1004), Advanced → Modulation block (1003–1011), tooltips | `resources/editor.uidesc` via XSLT | per push |
| 12 | Plugin harness: decode the block, overlay it, write it into the rendered stream, carry the wheel, refuse what has no carrier, drive the twins; `renderPresetSweep` override; the four M-* showcase cases and their trajectory reader | `tests/preset_test_support.h`, `preset_render_support.h`, `param_path_audibility_helpers.h` | per push + `[long]` |
| 13 | Library: four `MorphMod` columns, `controls` may carry 1003–1011, probe → conversions → regenerate | `tools/profundum_preset_defs.h`, `tests/profundum_preset_host.h`, `resources/presets/` | `[.probe]`, `[long]` |
| 14 | Pre-phase harvest + close comparison (FR-062 / SC-008) | `tests/integration/morph_mod_preset_test.cpp` (new) | G0 + close gate |
| 15 | Tooltips, `MANUAL.md`, CHANGELOG `[1.0.0]` | plugin | per push + review |

---

## 1. What was read (this session)

| Area | Files / lines |
|---|---|
| Roadmap | `specs/Profundum-roadmap.md:1-200` (philosophy, decisions, reuse inventory), `:573-752` (Phase 8, Phase 9 + handoffs), `:1050-1184` (Phase 13, Phase 14, dependency graph, constraints, OQs) |
| Spec | `specs/profundum-phase14-morph-modulation/spec.md:1-786` (whole file) |
| Engine | `dsp/include/krate/dsp/systems/profundum_engine.h:1-1334` (whole file) |
| Observer | `systems/profundum_engine_observer.h:1-80` (whole file) |
| Morph engine | `systems/bass_morph_engine.h:40-429` (API, `setPosition` `:140-147`, `reset` `:99-105`, `advanceInterval` `:205-225`, runtime blob `:235-347`, `configureRate` `:370-373`), `:516-519` (defaults: `target_ = 0.0f`) |
| Motion (pattern) | `systems/profundum_motion.h:1-325` (whole file) |
| Morph state | `systems/morph_state.h:76-102` (`MotionConfig`), `:247-285` (motion rows of the field table), `:135`, `:441-442` |
| Sources | `primitives/lfo.h:25-130`, `:185-300`, `:480-594`; `primitives/adsr_envelope.h:55-135`, `:225-350`, `:400-470`, `:515-565`; `primitives/envelope_utils.h:40-110`; `primitives/smoother.h:495-575` (`SlewLimiter`) |
| L0 | `core/modulation_source.h:1-45`; `core/note_value.h:20-200` |
| Driver | `dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h:120-450` |
| Readers | `dsp/tests/unit/systems/profundum_audibility_readers.h:105-112`, `:1975-1995` (AT-41 / AT-42 cells) |
| DSP CMake | `dsp/tests/CMakeLists.txt:760-800`, `:860-940` (`dsp_systems_tests`, `dsp_ieee_tests`) |
| Plugin IDs / params | `plugins/profundum/src/plugin_ids.h:1-231`; `parameters/morph_params.h:1-397`; `parameters/motion_params.h:1-189`; `parameters/performance_params.h:25-116`; `parameters/sound_param_row.h:1-389`; `parameters/sound_param_table.h:1-257` |
| Processor | `processor/processor.cpp:1-1308` (whole file); `processor/spectrum_frame.h:1-53`; `processor/spectrum_producer.h:40-60`, `:95-130`, `:205-240`, `:325-371` |
| Controller | `controller/controller.cpp:60-360`, `:600-1100`; `controller/controller.h:255-360` (grep of members) |
| UI | `ui/offset_ring_knob.h:1-90`; `ui/harmonic_bar_view.h:1-80`; `ui/panel_sub_controller.cpp:121`, `:234-274`; `extern/vst3sdk/vstgui4/vstgui/lib/controls/cslider.h:14-160` |
| Editor | `resources/editor.uidesc:40-49` (control-tags), `:165-188` (Main `morph-band`), `:340-382` (Advanced Modulation tab) |
| Plugin tests | `tests/profundum_test_fixture.h:1060-1125`; `tests/preset_test_support.h:40-80`, `:115`, `:182`; `tests/preset_render_support.h:1-30`, `:130-215`, `:1520-1700`; `tests/param_path_audibility_helpers.h:570-645`; `tests/profundum_preset_host.h:228-245`, `:395-440`; `tests/unit/controller/editor_layout_test.cpp:535-640`; `tests/integration/main_page_audibility_test.cpp:1-93`; `tests/CMakeLists.txt:1-81`; `plugins/profundum/CMakeLists.txt:20-62` |
| Generator | `tools/profundum_preset_defs.h:55-175`; `tools/profundum_preset_generator.cpp:1-60` |
| Fingerprints | `tests/test_helpers/render_fingerprint.h:30-140` |
| Review round (2026-10-26) | `tests/preset_render_support.h:110-145` (`PresetArmContext`), `:195-340` (`processorCarrierParam`, `processorSampleZeroParams`, `processorRenderRefusal`), `:365-420` (`renderScriptThroughProcessor` steps 1-3), `:575-615` (`ShowcasePlan`), `:780-800` (`steadyPlan`), `:820-830`, `:1155-1300` (`showcaseArmPlan` AT-41 / AT-42, macros, reverse arms, `C::Count`, `showcaseColumn`), `:1600-1700` (`renderPresetSweep`); `integration/preset_showcase_test.cpp:1-150` (`scRunShowcaseShard`, `scCoverageDir`); `dsp/tests/unit/systems/profundum_audibility_readers.h:55-100` (`render::scriptRenderHook`, `ScriptRenderScope`), `:245-290` (`auOn`, `auScript`, `auSteadyTotal`), `:339-350` (`auSteady`), `:614-700` (`AuCell`, `auSteadyCell`); `bass_morph_test_helpers.h:700-775` (`readWindow`, `dNlDb`); `profundum_core_test_helpers.h:235-242`; `tests/test_helpers/low_frequency_metrics.h:64`; `profundum_voice_engine_test_helpers.h:120-138`, `:176-192`, `:445-465`; `unit/phase11_alloc_test.cpp:80-90`, `:192-200`; `processor.cpp:295-335`, `:649-652`, `:715-726`, `:955-980`; `core/db_utils.h:38`, `:259-265`; `adsr_envelope.h:66-73`, `:236-250`; `envelope_utils.h:53`; `profundum_engine.h:100-104`, `:224-236`, `:258-261`, `:413`, `:768-770`; `editor_layout_test.cpp:498`, `:517-518`, `:535`, `:656`, `:1449`, `:1501`; `preset/preset_library_test.cpp:243`, `:695`, `:1237-1263`, `:1312`; `integration/preset_browser_test.cpp:465-468`; `tools/profundum_preset_defs.h:140-160`; `profundum_engine_perf_test.cpp:596`; `profundum_engine_display_test.cpp:368`; `profundum_voice_engine_helpers_test.cpp:191`; `dsp/tests/CMakeLists.txt:786-792`; spec `:114-135`, `:292-372`, `:408-440`, `:455-500`, `:565-650` |
| Lints present | `tools/lint-layers.js`, `lint-odr.js`, `lint-finite-bitmask.js`, `lint-nonfinite-symbols.js`, `lint-apple-globals.js`, `lint-float-bit-goldens.js`, `lint-plugin-editor-baseline.js`, `check-portability.js` (directory listing) |

---

## 2. Plan-stage findings (each amends its spec row first, then Clarifications)

**P-1 (amends FR-010, C-2): `setMorphPosition` keeps forwarding; prepare / reset re-hand the base.** FR-010 says
`setMorphPosition` "MUST store the base `b` … instead of forwarding it directly". That breaks FR-060 (a)/(b):
- `BassMorphEngine::reset()` snaps p to `target_` (`bass_morph_engine.h:99-101`) and `prepare()` ends in `reset()` (`:88-95`);
- the processor pushes the parameters **before** `engine_->prepare` (`processor.cpp:275-280`: "prepare resolves its initial state …
  from the endpoints and position the engine holds"), and `setActive(false)` calls `engine_->reset()` (`processor.cpp:335`).

A store-only setter would leave `target_` at its old value at those snaps, so the first block after `setupProcessing` would ramp
from the stale target: an audible, pre-phase-divergent glide. **Ruling proposed:** `setMorphPosition(p)` stores `b` **and** forwards
`p` (as today); sub-step 1b re-hands the computed target every interval; `prepare` and `reset` call `morph_.setPosition(morphBase_)`
immediately before the morph snap, so a snap never lands on a modulated target. With Source Off the forwarded value, the re-handed
value and the snap value are the same float, and `SlewLimiter::setTarget` only stores it (`smoother.h:519-529`), so the Off path is
the pre-phase path instruction for instruction. FR-010 is rewritten: "stores the sanitised base `b` and forwards it; the per-interval
target replaces it before every `advanceInterval`".

**P-2 (sharpens C-11, FR-016): the blob-target mechanism is an engine byte patch.** `ProfundumEngine::serializeMorphRuntime`
(`profundum_engine.h:273-275`) writes the morph blob, then overwrites bytes 1–4 with `morphBase_`. The offset is
`BassMorphEngine`'s private `kRtTargetOffset = 1` (`bass_morph_engine.h:340`), which the plugin already re-states as
`kRtTargetOffset` (`plugin_ids.h:71`, "SC-012 arm 5 checks them against a real engine"). The engine re-states it as
`ProfundumEngine::kMorphRuntimeTargetOffset = 1` and a unit test pins it (§6, SC-013 (e)). `restoreMorphRuntime` sets
`morphBase_ = morph_.targetPosition()` on success, so engine-level round trips keep `b`. This is the only mechanism that also keeps
the processor's `applyPendingRestore` check "target bits == registered position" (`processor.cpp:1263-1266`) true while the
modulation runs.

**P-3 (sharpens C-11): a successful restore resets the modulator, also mid-stream.** C-11 says "On restore, the modulator restarts
from its reset state". `restoreMorphRuntime` re-snaps dependents only when nothing rendered since reset (`profundum_engine.h:280-285`);
the plan resets the modulator on **every** successful restore (the literal C-11 reading). A preset loaded under a held note therefore
restarts the envelope, zeroes Velocity and re-phases the Cycle; the Phase 13 FR-048 held-load click bound covers it.

**P-4 (adds to C-12's list of hard-coded counts):** the C-12 list misses these literals, which this phase moves:
- the visible-ID count (84 − 4 hidden = 80) in `plugins/profundum/tests/unit/controller/editor_layout_test.cpp`. It becomes
  **89** at four sites: `:517` `CHECK(expected.size() == 80u)`, `:518` `CHECK(boundTags.size() == 80u)`, `:535`
  `CHECK(tagMap.size() == 80u)` and `:656` `CHECK(boundCount >= 80u)`. The same file's `== 84` sites (`:498`, `:1449`, `:1501`)
  are already in C-12's list and become 93;
- `processor.cpp:355` `static_assert(kBlockSize == 288u, …)` becomes 320 (the frame grows, FR-036);
- `plugin_ids.h:61-63` `kStateV3Bytes … == 574` becomes 610 (named offsets §4.6). The literal `574`s in
  `plugins/profundum/tests/unit/preset/preset_library_test.cpp:243`, `:1262-1263` (and the `:1237` comment) and
  `plugins/profundum/tests/integration/preset_browser_test.cpp:467` become `610`.
The listed sites are a starting point, not the authority. The build task re-greps `84`, `80u`, `288` and `574` under
`plugins/profundum/` before editing. It edits only the sites this phase moves, and records any site the list missed in the P-4 row.

**P-5 (sharpens C-18, FR-037): the indicator updates at the frame cadence.** The producer stages a frame only when a window
completes or, in silence, every `kSilenceRepublishSeconds = 0.25` (`spectrum_producer.h:47`, `:205-225`). The resolved position rides
those frames, so during silence a Cycle-driven morph is shown at 4 Hz. Adding a morph-only republish would change the Phase 12 frame
counts (`framesPublished_`, SC-030). **Proposed:** accept the cadence and document it in the manual; the alternative is put to the
user (§11 Q1).

**P-6 (records a confound in FR-031's 1003 row):** the 1003 arm pins "lo = Off, hi = Mod Wheel, with Amount 1, Mod Wheel (6) 1". In
the lo arm the wheel still pushes Mass (C-7: `c = clamp(1 + 0, 0, 1) = 1`, so Mass goes to 100 on both endpoints,
`processor.cpp:928-944`); in the hi arm it does not. The 1003 contrast therefore measures the whole Source switch (wheel moves from
Mass to Morph), which is the user-facing lever, not Morph alone. The row is kept as pinned; the confound is recorded in the
row's notes and the reading is logged with both arms' grid-record Mass (`smoothedMass`). **Superseded by R7 (2026-10-27):** the
confound cancelled the row (G15: 1.28 / 1.24 dB against 3 dB), so the 1003 row is re-pinned to Off → Velocity (§5.6).

**P-7 (adds to C-17): the driver gains three appended event kinds.** C-17 adds script fields applied before the first block. SC-004,
SC-010 (g), SC-012 (c)/(d) need mid-render wheel, bypass and config changes, so `VoiceEngineEventKind` gains `MorphModulation`
(index into an appended `std::vector<ProfundumMorphModulator::Config> morphModStates`), `MorphModWheel` (`value`) and `MorphModBypass`
(`index`), appended after `Endpoint` (`profundum_voice_engine_test_helpers.h:123-126`). Existing scripts never use them.

**P-8 (amends FR-019 and C-1): the `dsp/` change set names the existing test TUs it appends to.** FR-019 fences `dsp/` to "the
new header, the two engine headers, the test driver and new unit tests". This plan, and the spec's own SC rows, also edit four
existing `dsp/` files:
- `dsp/tests/CMakeLists.txt`: two registration lines in the `dsp_systems_tests` list after
  `unit/systems/profundum_engine_display_test.cpp` (`:790`), §7;
- `dsp/tests/unit/systems/profundum_engine_perf_test.cpp`: an appended `VoiceEngine_WorstIntervalMorphMod` (`[.perf]`) beside
  `VoiceEngine_WorstInterval` (`:596`), SC-014;
- `dsp/tests/unit/systems/profundum_engine_display_test.cpp`: `Profundum_EngineDisplayAccessorNeutral` (`:368`) gains the
  `morphResolvedPosition()` arm, SC-024 (d), which spec SC-024 names;
- `dsp/tests/unit/systems/profundum_voice_engine_helpers_test.cpp`: an appended `VoiceEngineHelpers_MorphModFields` beside
  `VoiceEngineHelpers_DriverSplitsAtEvents` (`:191`), SC-023 (d), which spec SC-023 names.

**Ruling proposed:** FR-019 / C-1 are amended in the row first. The C-1 list gains "the registration lines for the new TUs in
`dsp/tests/CMakeLists.txt`, and appended TEST_CASEs (or, for `Profundum_EngineDisplayAccessorNeutral`, an appended SECTION) in the
existing Profundum test TUs `profundum_engine_perf_test.cpp`, `profundum_engine_display_test.cpp` and
`profundum_voice_engine_helpers_test.cpp`; no existing case in them changes". Moving the cases into the two new TUs was considered.
It was rejected because spec SC-023 and SC-024 name the existing files and cases, so the move would amend two SC rows instead of
one FR row. No other `dsp/` file is edited. In particular, the §5.5 trajectory reader lives in plugin test code (§4.12), not in
`profundum_audibility_readers.h`.

**P-9 (amends FR-046 after §11 Q2; adds an SC-018 assertion): the conversion set is a rule applied to the probe CSV.** FR-046
reads "a conversion happens wherever the hidden probe shows the source audible". §5.7 step 4 proposes one conversion per column,
so the plan currently departs from the row without amending it. Before G5's conversion task, the user's §11 Q2 ruling is written
into the FR-046 row first, then Clarifications. The ruling is either (i) "one conversion per column: the eligible carrier with
the largest margin" or (ii) "every eligible carrier, each preset converting for at most one column, by largest margin". SC-018
gains a matching assertion: the set of converted presets (defs whose primary is in the `MorphMod` group, excluding the presets
added under §5.7 step 4) equals the §5.7 selection rule, as ruled, applied to `evidence/probe-morphmod.csv` (§6 SC-018 row).

---

## 3. Verified reuse inventory

| Reused | Real signature (file:line) | Use here |
|---|---|---|
| `ADSREnvelope` | `void prepare(float sampleRate)` (`adsr_envelope.h:68-73`), `void reset()` (`:75-80`), `void gate(bool on)` (`:86-109`), `setAttack/Decay/Sustain(float)` (`:115-130`, `ITERUM_NOINLINE`, NaN keeps, clamp), `void setRetriggerMode(RetriggerMode)` (used by Motion, `profundum_motion.h:145`), `float process()` (`:232-253`) | Envelope source at fs / 32, Hard mode, never gated off |
| `LFO` | `void prepare(double)` (`lfo.h:83-92`), `void reset()` (`:95-108`, seed `12345u`), `float process()` (`:114`), `setWaveform(Waveform)` (`:189`, 10 ms crossfade once processing started), `setFrequency(float)` (`:218`, clamp [0.01, 20]), `setTempoSync(bool)` (`:237`), `setTempo(float)` (`:246`, clamp [1, 999]), `setNoteValue(NoteValue, NoteModifier)` (`:255`) | Cycle source at fs / 32; `retrigger()` is never called (free-running) |
| `ModulationSource` | `getCurrentValue() const noexcept`, `getSourceRange() const noexcept` (`modulation_source.h:31-42`) | `ProfundumMorphModulator::Output` (FR-007) |
| `BassMorphEngine` | `setPosition(float)` (`:140-147`), `position()` (`:149`), `targetPosition()` (`:150`), `serializeRuntime` / `restoreRuntime` (`:235-335`), `kMorphRuntimeBytes = 20` (`:76`) | unchanged; driven by sub-step 1b |
| `SlewLimiter` | `setTarget(float)` stores only (`smoother.h:519-529`); `process()` steps by ±rate and snaps inside it (`:542-559`) | the only smoother (C-9) |
| `ProfundumMotion` (pattern) | change-only `setConfig` (`profundum_motion.h:172-230`), `setTempoBpm` bit-change rule (`:233-239`), `advanceInterval` advances every source (`:249-279`), `Output` (`:126-134`) | copied pattern, not composed |
| Note values | `kNoteValueDropdownMapping[30]` (`note_value.h:136-178`), `kNoteValueDropdownCount = 30` (`:123`) | Sync Rate index ↔ (note, modifier) |
| Laws / display | `PlainLaw`, `plainFromNormalized`, `normalizedFromPlain`, `indexFromNormalized` (`sound_param_row.h:121-169`); display formats `Time` / `Hz` / `Percent` / `BipolarPercent` (`:345-374`) | 1004–1009 laws and strings |
| Label arrays | `kMotionCycleShapeLabels`, `kMotionCycleSyncLabels` (`motion_params.h:41-43`), `Krate::Plugins::Parameters::kNoteValueDropdownStrings` (`:63-65`) | 1008, 1010, 1011 |
| List registration | `Krate::Plugins::createDropdownParameterWithDefault(...)` then pin `defaultNormalizedValue` (`morph_params.h:255-260`) | 1003, 1008, 1010, 1011 |
| `CSliderBase::calculateHandleRect(float) const` | protected (`cslider.h:67`) | indicator x position |
| Render harness | `voiceEngineRender`, `voiceEngineTwinScript` (`profundum_voice_engine_test_helpers.h:377-444`), `renderPresetSweep(comp, notes, fs, maxMass, seed)` (`preset_render_support.h:1635`), `fingerprintRender`, `compareFingerprints` (`render_fingerprint.h:73`, `:122`) | tests, harvest, comparison |

ODR sweep (`grep -rEn "(class|struct|enum class|using) <Name>\b" dsp/ plugins/ tools/ tests/`, this session): `ProfundumMorphModulator`,
`MorphModulator`, `MorphModParams`, `MorphModConfig`, `MorphModSource`, `MorphIndicatorSlider`, `MorphIndicatorSliderCreator` — **0
hits each**. No roadmap hazard name is used. `node tools/lint-odr.js` runs at close.

---

## 4. Component design

### 4.1 `ProfundumMorphModulator` — `dsp/include/krate/dsp/systems/profundum_morph_modulator.h` (Layer 3)

Placed in `systems/` beside its only consumer (spec "Layer note"). Includes, downward only:
`core/db_utils.h` (L0, `detail::isFinite`), `core/modulation_source.h` (L0), `core/note_value.h` (L0), `primitives/adsr_envelope.h`
(L1), `primitives/envelope_utils.h` (L1, `RetriggerMode`, `kMin/MaxEnvelopeTimeMs`), `primitives/lfo.h` (L1, `Waveform`,
`kMinFrequency`, `kMaxFrequency`); std `<algorithm> <bit> <cstddef> <cstdint> <utility>`. No `morph_state.h` (the config is not a
`MorphState` field, FR-018).

```cpp
namespace Krate::DSP {

/// Phase 14 Morph Modulation (FR-001 - FR-008). One global source at fs / 32; one offset per interval.
class ProfundumMorphModulator {
public:
    enum class Source : std::uint8_t { Off = 0, Envelope, Cycle, Velocity, ModWheel };   // C-3, list order of 1003

    struct Config {                                   // FR-002: exactly the C-12 fields (Sync Rate as note + modifier)
        Source source = Source::Off;
        float amount = 0.0f;                          // [-1, 1]
        float envAttackMs = 10.0f;                    // [kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs]
        float envDecayMs = 300.0f;
        float envSustain = 0.0f;                      // [0, 1]
        Waveform cycleShape = Waveform::Sine;
        float cycleRateHz = 1.0f;                     // [kMinFrequency, kMaxFrequency]
        bool cycleSync = false;
        NoteValue cycleNote = NoteValue::Quarter;     // dropdown index 13 with cycleModifier None
        NoteModifier cycleModifier = NoteModifier::None;
    };

    class Output final : public ModulationSource {   // FR-007 (profundum_motion.h:126-134 pattern)
    public:
        [[nodiscard]] float getCurrentValue() const noexcept override { return value_; }
        [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override { return {-1.0f, 1.0f}; }
    private:
        friend class ProfundumMorphModulator;
        float value_ = 0.0f;
    };

    static constexpr std::size_t kControlInterval = 32;
    static constexpr float kMinAmount = -1.0f, kMaxAmount = 1.0f;
    static constexpr std::uint8_t kMaxVelocity = 127;

    ProfundumMorphModulator() noexcept = default;
    ProfundumMorphModulator(const ProfundumMorphModulator&) = delete;            // owns an LFO (lfo.h:73)
    ProfundumMorphModulator& operator=(const ProfundumMorphModulator&) = delete;

    void prepare(double sampleRate) noexcept;            // control thread
    void reset() noexcept;
    void setConfig(const Config& c) noexcept;            // change-only, sanitising
    void setTempoBpm(float bpm) noexcept;
    void noteOn(std::uint8_t velocity) noexcept;         // C-4 restart + C-6 latch
    void setModWheel(float w) noexcept;
    float advanceInterval() noexcept;                    // returns s after the step (FR-001)

    [[nodiscard]] float sourceValue() const noexcept { return s_; }
    [[nodiscard]] float offsetValue() const noexcept { return output_.value_; }   // clamp(amount * s, -1, 1)
    [[nodiscard]] const Config& config() const noexcept { return applied_; }      // the sanitised, applied config
    [[nodiscard]] const ModulationSource& output() const noexcept { return output_; }

private:
    ADSREnvelope env_;
    LFO lfo_;
    Config applied_{};
    bool configured_ = false;
    float velocity_ = 0.0f;      // C-6, [1/127, 1] after a note, 0 after reset
    float wheel_ = 0.0f;         // FR-006, [0, 1]
    float s_ = 0.0f;
    Output output_;
    float tempoBpm_ = 120.0f;
};
}  // the engine asserts ProfundumEngine::kControlInterval == ProfundumMorphModulator::kControlInterval (§4.2)
```

**Contract.**
- `prepare(fs)`: `const double cr = fs / 32.0; env_.prepare(static_cast<float>(cr)); env_.setRetriggerMode(RetriggerMode::Hard);
  lfo_.prepare(cr); reset();` — the Motion pattern (`profundum_motion.h:141-151`). `fs` is the engine's already clamped rate.
- `reset()`: `env_.reset()` (Idle, output 0); `lfo_.reset()` (phase 0, random seed `12345u`); `velocity_ = 0`; `s_ = 0`;
  `output_.value_ = 0`; `configured_ = false` (the next `setConfig` hands every field). `wheel_` and `tempoBpm_` are kept (the wheel
  is a pending engine value re-handed each interval; the tempo lives in the LFO, which `reset()` does not touch, `lfo.h:95-108`).
- `setConfig(c)`: per field, sanitise against `applied_` — non-finite float keeps the previous value (`detail::isFinite`), finite
  floats clamp (amount [−1, 1]; times [`kMinEnvelopeTimeMs`, `kMaxEnvelopeTimeMs`]; sustain [0, 1]; rate [`kMinFrequency`,
  `kMaxFrequency`]); an enum above its last enumerator keeps the previous value (`Source::ModWheel`, `Waveform::SmoothRandom`,
  `NoteValue::SixtyFourth`, `NoteModifier::Triplet`). Then, on a bitwise change or `!configured_`, in this order:
  `env_.setAttack`, `env_.setDecay`, `env_.setSustain`, `lfo_.setWaveform`, `lfo_.setFrequency`, `lfo_.setTempoSync`,
  `lfo_.setNoteValue`. `source` and `amount` are stored only.
- `setTempoBpm(bpm)`: non-finite or `<= 0` keeps; otherwise on a bitwise change `lfo_.setTempo(bpm)` (the `profundum_motion.h:233-239`
  rule).
- `noteOn(v)`: `env_.reset(); env_.gate(true);` (C-4: restart **from 0**; Hard mode's `enterAttack` would otherwise start from the
  current output, `adsr_envelope.h:451-456`); `velocity_ = (v >= kMaxVelocity) ? 1.0f : static_cast<float>(v) / 127.0f` (the 127
  endpoint exact by branch, the root `CLAUDE.md` fast-math rule); `v == 0` is never passed (the engine turns it into a note-off,
  `profundum_engine.h:226-229`), but the modulator treats 0 as "no change" defensively. Several calls before one
  `advanceInterval()` leave the same state as one call (reset + gate is idempotent): one restart per grid point (C-4).
- `setModWheel(w)`: non-finite keeps; else `wheel_ = std::clamp(w, 0.0f, 1.0f)`.
- `advanceInterval()` (§5.1 step 2): `const float e = detail::flushDenormal(env_.process()); const float lRaw = lfo_.process();
  const float l = detail::isFinite(lRaw) ? std::clamp(lRaw, -1.0f, 1.0f) : 0.0f;` then select `s` by `applied_.source`
  (Off 0, Envelope `e`, Cycle `l`, Velocity `velocity_`, ModWheel `wheel_`); `s_ = s; output_.value_ = std::clamp(applied_.amount *
  s, -1.0f, 1.0f); return s_;`. Every source advances whichever is selected (C-2 step 2; Motion C-13).
  The flush (`detail::flushDenormal`, `core/db_utils.h:263`, already included; `|x| < kDenormalThreshold = 1e-15f` (`:38`) → `+0`)
  covers the Sustain-stage tail. While held in Sustain, `ADSREnvelope` runs `output_ = sustainTarget + sustainSmoothCoef_ *
  (output_ - sustainTarget)` (`adsr_envelope.h:245`) with `sustainSmoothCoef_ = exp(-5000 / (kSustainSmoothTimeMs · fc))`
  (`:72`, `kSustainSmoothTimeMs = 5`, `envelope_utils.h:53`). At `fc = fs / 32` that coefficient is 0.513 / 0.484 / 0.717 at
  48 / 44.1 / 96 kHz. If Morph Env Sustain is moved to 0 while the envelope is held, the output decays geometrically through
  the subnormal range. Where the coefficient is > 0.5 (48 and 96 kHz), it then sticks at `denorm_min`, because
  `denorm_min · c` rounds back to `denorm_min`. Without the flush, `s`, the offset and the grid record would carry that subnormal
  for the rest of the note, and the Envelope source would never read exactly 0 unless the host's FTZ / DAZ happened to be on. The
  flush is a branch that leaves every normal value bit-unchanged, so SC-002 (a)'s 1e-6 trace bound is unaffected. The envelope's
  internal `output_` is not flushed, because `ADSREnvelope` is not edited (spec Non-goals). That costs at most one subnormal
  multiply per 32 samples, inside one object, and the value is never observed (§10).

**RT.** Every member but `prepare` is `noexcept`; fixed-size members only (`ADSREnvelope` holds three 256-float tables by value,
`adsr_envelope.h:536-538`; `LFO` reads shared static wavetables through a pointer, `lfo.h:572`); no allocation, lock, exception or
I/O. `prepare` is control-thread (it computes coefficients with `std::exp`).

**State size.** ≈ 3.2 KB (dominated by the ADSR tables); the engine is heap-constructed already (`profundum_engine.h:93-94`).

### 4.2 Engine edits — `dsp/include/krate/dsp/systems/profundum_engine.h` (append-only)

Includes: add `#include <krate/dsp/systems/profundum_morph_modulator.h>` (L3 peer) and `<cstring>` (`std::memcpy`). Add
`static_assert(kControlInterval == ProfundumMorphModulator::kControlInterval);` beside `:125-131`.

**New members** (appended to the "working state" group, `:1283-1290`):

```cpp
struct MorphModSettings {                       // pending only: read at the grid point (C-9 latch)
    ProfundumMorphModulator::Config config{};
    float wheel = 0.0f;
    bool bypass = false;
};
ProfundumMorphModulator morphMod_;
MorphModSettings morphModPending_{};
float morphBase_ = 0.0f;                         // b (== BassMorphEngine's default target_, bass_morph_engine.h:516)
float morphTarget_ = 0.0f;                       // the value handed to setPosition at the last grid point
static constexpr std::size_t kMorphRuntimeTargetOffset = 1;   // P-2: bass_morph_engine.h:340 (plugin_ids.h:71)
```

**Public setters / read-backs** (appended after `setAnchorMorphMode`, `:272`, and after the FR-066 read-backs, `:345-380`):

```cpp
void setMorphPosition(float p) noexcept {                   // body changed (P-1)
    if (!detail::isFinite(p)) return;                      // BassMorphEngine keeps its target (bass_morph_engine.h:141-143)
    morph_.setPosition(p);
    morphBase_ = morph_.targetPosition();                  // the sanitised p (bass_morph_engine.h:144-145)
}
void setMorphModulation(const ProfundumMorphModulator::Config& c) noexcept { morphModPending_.config = c; }
void setMorphModWheel(float w) noexcept {
    if (!detail::isFinite(w)) return;
    morphModPending_.wheel = std::clamp(w, 0.0f, 1.0f);
}
void setMorphModBypass(bool on) noexcept { morphModPending_.bypass = on; }

[[nodiscard]] float morphBasePosition() const noexcept { return morphBase_; }
[[nodiscard]] float morphResolvedPosition() const noexcept { return morph_.position(); }   // FR-037; const, render-neutral
[[nodiscard]] const ProfundumMorphModulator::Config& morphModulation() const noexcept { return morphModPending_.config; }
[[nodiscard]] float morphModWheel() const noexcept { return morphModPending_.wheel; }
[[nodiscard]] bool morphModBypass() const noexcept { return morphModPending_.bypass; }
```

`morphPosition()` (`:350`, `morph_.targetPosition()`) is **not** changed (append-only); with Source Off it equals `b`, with a source
running it reads the modulated target. Every new caller that means `b` uses `morphBasePosition()` (C-17 item 3).

**Lifecycle edits.**
- `prepare` step 2 (`:154`): insert `morph_.setPosition(morphBase_);` immediately before `morph_.prepare(fs);` (P-1). Step 5
  (`:168-169`): append `morphMod_.prepare(fs);` after `motion_.prepare(fs);`.
- `reset()` (`:201-208`): insert `morph_.setPosition(morphBase_);` immediately before `morph_.reset();` (P-1).
- `resetDependents()` (`:618-662`): append `morphMod_.reset();` after `motion_.reset();` (`:630`). `reset` therefore keeps the pending
  config, wheel and bypass (FR-014, the `:199-200` rule).
- `restoreMorphRuntime` (`:280-285`): on `ok`, `morphBase_ = morph_.targetPosition(); morphMod_.reset();` before the existing
  `resetDependents()` branch (P-2, P-3).
- `serializeMorphRuntime` (`:273-275`): `const std::size_t n = morph_.serializeRuntime(dst, cap); if (n == BassMorphEngine::kMorphRuntimeBytes)
  std::memcpy(dst + kMorphRuntimeTargetOffset, &morphBase_, sizeof(float)); return n;` (P-2). With Source Off the patched bytes equal
  the written ones.
- `applySettings` tempo block (`:768-772`): append `morphMod_.setTempoBpm(p.tempoBpm);` inside the existing branch (FR-014). No
  `logCall` is added.
- `applyNoteOn` (`:793`): first statement `morphMod_.noteOn(velocity);` before the MONO branch and its `if (!mev.isNoteOn) return;`
  (`:798-800`) (FR-013). Only accepted note-ons reach `applyNoteOn` (`noteOn` drops at a full queue, `:230-233`; velocity 0 is a
  note-off, `:226-229`). `consumeEnvelopeRestart()` (`:920-923`) is not touched.

**Grid step, sub-step 1b** — inserted between `logCall(ProfundumCall::Events, …)` (`:902`) and `morph_.advanceInterval()` (`:905`):

```cpp
// 1b. Morph Modulation (Phase 14 C-2): latched settings, one modulator step, the target, setPosition. No logCall (SC-032).
morphMod_.setConfig(morphModPending_.config);
morphMod_.setModWheel(morphModPending_.wheel);
const float s = morphMod_.advanceInterval();
const ProfundumMorphModulator::Config& mc = morphMod_.config();
const bool transparent = mc.source == ProfundumMorphModulator::Source::Off || !nonZero(mc.amount) || morphModPending_.bypass;
morphTarget_ = transparent ? morphBase_ : std::clamp(morphBase_ + mc.amount * s, 0.0f, 1.0f);   // FR-012: branch, not arithmetic
morph_.setPosition(morphTarget_);
```

`nonZero` is the engine's existing ±0 test (`:561-563`). Steps 1–8 keep their order (FR-011).

**Grid record** (`:1097-1109`): append `.morphModSource = morphMod_.sourceValue(), .morphModOffset = morphMod_.offsetValue(),
.morphTarget = morphTarget_`.

### 4.3 Observer — `systems/profundum_engine_observer.h` (append-only)

`ProfundumGridRecord` (`:52-60`) gains, after `targets`: `float morphModSource = 0.0f; float morphModOffset = 0.0f; float
morphTarget = 0.0f;`. The default member initializers keep `ProfundumGridRecord{}` (used by `profundum_voice_engine_helpers_test.cpp:84-97`)
and the engine's designated initializer valid. `ProfundumCall` is unchanged (FR-015).

### 4.4 Test driver — `dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h` (append-only, C-17 + P-7)

- `VoiceEngineEventKind` (`:123-126`): append `MorphModulation, MorphModWheel, MorphModBypass` after `Endpoint`.
- `VoiceEngineScript` (`:139-155`): append `ProfundumMorphModulator::Config morphMod{}; float morphModWheel = 0.0f; bool
  morphModBypass = false; std::vector<ProfundumMorphModulator::Config> morphModStates;`.
- `voiceEngineIssue` (`:342-372`): three cases — `e.setMorphModulation(s.morphModStates[index])`, `e.setMorphModWheel(ev.value)`,
  `e.setMorphModBypass(ev.index != 0)`; `voiceEngineRender` validates `MorphModulation` indices like `Endpoint` (`:388-394`).
- `voiceEngineRender` (`:403-406`): after `e->setMorphPosition(s.position);` add `e->setMorphModulation(s.morphMod);
  e->setMorphModWheel(s.morphModWheel); e->setMorphModBypass(s.morphModBypass);`.
- `voiceEngineTwinScript` (`:438-444`) copies the whole script already (`VoiceEngineScript t = s;`), so the twin carries the
  modulation with no edit.
- `VoiceEngineGridRecord` (`:176-184`): append `float morphModSource = 0.0f, morphModOffset = 0.0f, morphTarget = 0.0f;` and copy
  them in `VoiceEngineObserver::onGrid` (`:240-262`).
With the defaults, every existing script hands the engine Source Off / Amount 0 / wheel 0 / bypass off, which is the engine's own
default: renders are unchanged (FR-060, SC-023 (d)).

### 4.5 Parameter pack — `plugins/profundum/src/parameters/morph_mod_params.h` (new)

A separate header (not appended to `morph_params.h`) because the pack has its own atomics, stream block and route kind; it includes
`plugin_ids.h`, `parameters/sound_param_row.h` (laws), `parameters/motion_params.h` (label arrays), `ui/note_value_ui.h`,
`ui/parameter_helpers.h`, `<krate/dsp/systems/profundum_morph_modulator.h>`, `<krate/dsp/core/note_value.h>`.

```cpp
namespace Profundum {
using MorphMod = Krate::DSP::ProfundumMorphModulator;

inline constexpr int kNumMorphModSources = 5;                       // Off, Envelope, Cycle, Velocity, Mod Wheel
inline constexpr std::array<const Steinberg::Vst::TChar*, kNumMorphModSources> kMorphModSourceLabels{
    STR16("Off"), STR16("Envelope"), STR16("Cycle"), STR16("Velocity"), STR16("Mod Wheel")};
inline constexpr int kMorphModSourceModWheel = static_cast<int>(MorphMod::Source::ModWheel);   // 4
inline constexpr int kNumMorphCycleShapes = 6;                      // == std::size(kMotionCycleShapeLabels)
inline constexpr int kDefaultMorphModSyncRateIndex = 13;            // {Quarter, None} (note_value.h:155)

inline constexpr PlainLaw kMorphModAmountLaw{-1.0, 1.0, 0.0, false};
inline constexpr PlainLaw kMorphModTimeLaw{double(Krate::DSP::kMinEnvelopeTimeMs), double(Krate::DSP::kMaxEnvelopeTimeMs), 0.0, true};
inline constexpr PlainLaw kMorphModSustainLaw{0.0, 1.0, 0.0, false};
inline constexpr PlainLaw kMorphModRateLaw{double(Krate::DSP::kMinFrequency), double(Krate::DSP::kMaxFrequency), 0.0, true};
// static_asserts: the time and rate laws equal plainLawOf(lawOf(50)) / plainLawOf(lawOf(40)) (motion.envAttackMs,
// motion.cycleRateHz, morph_state.h:257-259, :281-284), so 1005 / 1009 map exactly as 913 / 904 do.

struct MorphModParams {                                             // processor atomics (FR-020)
    std::atomic<int> source{0};
    std::atomic<float> amount{0.0f};
    std::atomic<float> attackMs{10.0f};
    std::atomic<float> decayMs{300.0f};
    std::atomic<float> sustain{0.0f};
    std::atomic<int> cycleShape{0};
    std::atomic<float> cycleRateHz{1.0f};
    std::atomic<int> cycleSync{0};
    std::atomic<int> cycleSyncRate{kDefaultMorphModSyncRateIndex};
};

bool handleMorphModParamChange(MorphModParams&, Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue) noexcept;   // value finite, [0,1]
void registerMorphModParams(Steinberg::Vst::ParameterContainer&);
Steinberg::tresult formatMorphModParam(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue, Steinberg::Vst::String128);
[[nodiscard]] MorphMod::Config morphModConfigOf(const MorphModParams&) noexcept;   // index 1011 -> kNoteValueDropdownMapping
void saveMorphModParams(const MorphModParams&, Steinberg::IBStreamer&);
bool loadMorphModParams(MorphModParams&, Steinberg::IBStreamer&);                  // EOF-safe, keep prior from the cut on
template <typename SetParamFunc> void loadMorphModParamsToController(Steinberg::IBStreamer&, SetParamFunc);
void resetMorphModParams(MorphModParams&) noexcept;
[[nodiscard]] bool morphModHandsWheelToMorph(const MorphModParams&) noexcept;       // C-7: source == 4 && amount != 0.0f
}
```

- **Denormalisation** (`handleMorphModParamChange`): lists via `indexFromNormalized(v, max)` (1003 max 4, 1008 max 5, 1010 max 1,
  1011 max 29); continuous via `static_cast<float>(plainFromNormalized(law, v))` clamped to the law box. 1004 at v = 0.5 is exactly
  `-1 + 0.5 · 2 = 0` in double (`sound_param_row.h:137`), so the registered default stores +0.
- **Registration** (`registerMorphModParams`, called after `registerMorphControlParams`, `controller.cpp:102`): continuous
  `parameters.addParameter(title, units, 0, defaultNorm, ParameterInfo::kCanAutomate, id)`; lists through
  `createDropdownParameterWithDefault` with `flags = kCanAutomate | kIsList` and `defaultNormalizedValue` pinned by hand (the 1000
  pattern, `morph_params.h:255-260`). Defaults: 1003 Off (0), 1004 0.5 (Amount 0), 1005 `normalizedFromPlain(kMorphModTimeLaw, 10)`,
  1006 `…(300)`, 1007 0, 1008 Sine (0), 1009 `normalizedFromPlain(kMorphModRateLaw, 1)`, 1010 Off (0), 1011 13/29. For 1005, 1006,
  1009 the default normalised value is nudged by ≤ 8 `nextafter` steps until `float(plainFromNormalized(law, n))` is bit-equal to the
  plain default (the `defaultNormalizedOf` rule, `sound_param_row.h:233-252`), as a local helper `defaultNormalizedFor(law, plain)`.
- **Titles / units / display** (C-12): "Morph Mod Source", "Morph Mod Amount" (`%`, `"%+.0f%%"` of 100x, + 0.0 to print +0), "Morph
  Env Attack" / "Morph Env Decay" (`ms`, the `Time` rule `"%.1f ms"` below 1000 else `"%.2f s"`), "Morph Env Sustain" (`%`,
  `"%.0f%%"`), "Morph Cycle Shape", "Morph Cycle Rate" (`Hz`, the `Hz` rule), "Morph Cycle Sync", "Morph Cycle Sync Rate". The
  four format strings are those of `formatSoundRow` (`sound_param_row.h:345-374`); a test asserts 1005 vs 913, 1009 vs 904 and 1004
  vs 902 print identical strings at the same normalised value (they share the law).
- **Stream** (C-10, 36 bytes, little-endian, after the names): `writeInt32(source) writeFloat(amount) writeFloat(attackMs)
  writeFloat(decayMs) writeFloat(sustain) writeInt32(cycleShape) writeFloat(cycleRateHz) writeInt32(cycleSync)
  writeInt32(cycleSyncRate)`. `loadMorphModParams` mirrors `loadMorphControlParams` (`morph_params.h:312-346`): stop at the first
  failed read (later fields keep their prior values); a non-finite float or an out-of-range int is not stored; a finite float clamps.
  `loadMorphModParamsToController` applies the same rules and calls `setParam(id, normalized)` only for a stored value (FR-024).

### 4.6 IDs, routes, state constants — `plugin_ids.h`, `parameters/sound_param_table.h`

- `ParameterIDs` (`plugin_ids.h:218-224`): `kMorphModSourceId = 1003, kMorphModAmountId, kMorphModAttackId, kMorphModDecayId,
  kMorphModSustainId, kMorphModCycleShapeId, kMorphModCycleRateId, kMorphModCycleSyncId, kMorphModCycleSyncRateId` (1003–1011),
  `kNumRoutedIds = 1012`; the static_assert at `:226` becomes `kMorphModCycleSyncRateId + 1 == kNumRoutedIds`.
- State v3 (`:54-63`): `kStateNamesEnd = kStateNamesOffset + 2 * kEndpointNameBlockBytes` (574), `kStateMorphModOffset =
  kStateNamesEnd`, `kStateMorphModBytes = 36`, `kStateV3Bytes = kStateMorphModOffset + kStateMorphModBytes` with
  `static_assert(kStateV3Bytes == 610)`. `kCurrentStateVersion` stays 3 (`:23`); a test `static_assert(kCurrentStateVersion == 3)`
  pins it (SC-013 (g)).
- `RouteKind` (`sound_param_table.h:71`): append `MorphMod`. `buildParamRoutes` (`:80-97`): `setRange(kMorphModSourceId,
  kMorphModCycleSyncRateId, RouteKind::MorphMod)`. `routedCount() == 93` (`:191`). `kSoundRows.size() == 60` is untouched
  (FR-018).

### 4.7 Processor — `processor/processor.h`, `processor.cpp`

- Member `MorphModParams morphModParams_;` beside `morphParams_`.
- `routeParamChange` (`:893-919`): `case RouteKind::MorphMod: static_cast<void>(handleMorphModParamChange(morphModParams_, id,
  value)); return;`.
- `applyEndpoints` (`:927-930`), the one-term C-7 change (FR-022):
  ```cpp
  const float cp = perfParams_.channelPressure.load(std::memory_order_relaxed);
  const float c = morphModHandsWheelToMorph(morphModParams_)
                      ? std::clamp(cp, 0.0f, 1.0f)                                                     // wheel drives Morph only
                      : std::clamp(perfParams_.modWheel.load(std::memory_order_relaxed) + cp, 0.0f, 1.0f);   // today's expression
  ```
  The `else` arm is today's expression with the same operand order, so every other source (and Mod Wheel at Amount ±0) assembles
  bit-identical endpoints. A hand-over change flips `c`'s bits and re-sends both endpoints through the existing dirty path (`:931-935`).
- `pushParamsToEngine` (`:957-980`), appended after `engine_->setMorphPosition(position)`:
  `engine_->setMorphModulation(morphModConfigOf(morphModParams_)); engine_->setMorphModWheel(perfParams_.modWheel.load(...));
  engine_->setMorphModBypass(cmp != CompareMode::AB);` (FR-021, C-8).
- `getState` (`:665-700`): after the two `writeEndpointName` calls, `saveMorphModParams(morphModParams_, s);` → 610 bytes.
- `setState` (`:600-660`): v1 branch (`:615-626`) and the v2 sub-branch (`:633-634`) call `resetMorphModParams(morphModParams_)`.
  The v3 name loop (`:639-646`) gains `bool namesRead = true;` set false at its `break`; after it, `if (namesRead)
  static_cast<void>(loadMorphModParams(morphModParams_, s));`. A chain stopped earlier (blob or names) keeps every modulation field
  (C-10). The future-version refusal (`:609-611`) is unchanged, so a version-4 stream is refused (SC-013 (g)).
- `kMaxPresetStateMessageBytes = 4096` (`:747`) holds; the comment changes to "the 610-byte v3 stream".
- The DataExchange block (`:352-355`): `static_assert(kBlockSize == 320u, …)`; the rounding expression is unchanged (FR-036).

### 4.8 Frame and producer — `processor/spectrum_frame.h`, `processor/spectrum_producer.h`

- `ProfundumSpectrumFrame` (`spectrum_frame.h:32-44`): append `float morphPosition = 0.0f; // 288 resolved morph position (FR-036)`
  and `std::uint8_t reserved2[28] = {}; // 292 explicit padding to 320`. Static asserts: `sizeof == 320`, `% 32 == 0`,
  `offsetof(morphPosition) == 288`, the two existing offsets unchanged (`:47-50`).
- `SpectrumProducer::onInterval(const ProfundumEngine&)` (`spectrum_producer.h:98`): first statement
  `const float m = engine.morphResolvedPosition(); morphPos_ = detail::isFinite(m) ? std::clamp(m, 0.0f, 1.0f) : 0.0f;`. Both staging
  sites (`feedSilence`, `:214-224`, and the window completion, `:330-340`) set `f.morphPosition = morphPos_;`. `reset()` zeroes
  `morphPos_`. The producer reads only the const accessor (render-neutral, SC-024 (c)/(d)).

### 4.9 `MorphIndicatorSlider` — `plugins/profundum/src/ui/morph_indicator_slider.h/.cpp` (new)

A `VSTGUI::CSlider` subclass (the `OffsetRingKnob` precedent, `offset_ring_knob.h:1-90`: a display decoration on a real control
that never moves it), registered by a `ViewCreatorAdapter` named `"MorphIndicatorSlider"` with base view `"CSlider"`, so every
CSlider attribute and the `control-tag` of the uidesc still apply.

```cpp
namespace Profundum {
class MorphIndicatorSlider : public VSTGUI::CSlider {
public:
    MorphIndicatorSlider(const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag);
    void setIndicator(float resolved) noexcept;    // clamps to [0, 1]; invalid() only on a bitwise change
    void clearIndicator() noexcept;                // fall back to the slider's own value (C-18)
    [[nodiscard]] float indicatorPosition() const noexcept;   // the drawn position: the cached value, or getValueNormalized()
    [[nodiscard]] bool hasIndicator() const noexcept;
    void draw(VSTGUI::CDrawContext* context) override;          // CSlider::draw, then a 2 px marker
    CLASS_METHODS(MorphIndicatorSlider, VSTGUI::CSlider)
private:
    float indicator_ = 0.0f;
    bool has_ = false;
};
struct MorphIndicatorSliderCreator : VSTGUI::ViewCreatorAdapter { … "MorphIndicatorSlider", base "CSlider" … };
inline MorphIndicatorSliderCreator gMorphIndicatorSliderCreator;
}
```

- The marker's x is the horizontal centre of `calculateHandleRect(indicatorPosition())` (`cslider.h:67`, protected; the rect CSlider
  itself uses for its handle), drawn full-height as a 2 px line in a fixed member colour (`CColor(255, 255, 255, 220)`, no new uidesc
  attribute, so the creator needs no attribute code). `setIndicator` never calls `setValue`, `valueChanged`, `beginEdit` or `performEdit` (FR-035).
- Wiring (UI thread only): `ProfundumPanelSubController::verifyView` (`panel_sub_controller.cpp:252`) registers it with
  `owner_->registerMorphIndicator(slider)` (the `registerRingKnob` route, `:234-235`). `Controller` holds
  `MorphIndicatorSlider* morphIndicator_ = nullptr; bool frameSinceOpen_ = false;`:
  - `queueOpened` (`controller.cpp:819-830`): `frameSinceOpen_ = false;` and `clearIndicator()`;
  - `onDataExchangeBlocksReceived` (`:839-851`): after the newest valid copy, `frameSinceOpen_ = true;` and
    `setIndicator(cachedFrame_.morphPosition)`;
  - `registerMorphIndicator`: store, then `setIndicator` if `frameSinceOpen_` else `clearIndicator`;
  - `willClose` (`:1006-1027`): `morphIndicator_ = nullptr;`; `viewPointersNullForTest` (`:1089-1099`) includes it.
- Both plugin `.cpp` lists gain the file (`plugins/profundum/CMakeLists.txt:44-51`, `tests/CMakeLists.txt:60-66`).

### 4.10 Editor — `resources/editor.uidesc` (XSLT, `xslt3`)

One stylesheet `specs/profundum-phase14-morph-modulation/evidence/uidesc/phase14.xsl`, run with Saxon-JS (`npx xslt3
-xsl:… -s:editor.uidesc -o:editor.uidesc`), an identity transform plus:
- nine `<control-tag>` rows after `MorphRampTime` (`MorphModSource` 1003 … `MorphModCycleSyncRate` 1011);
- the Main-page `CSlider control-tag="MorphPosition"` (`editor.uidesc:180`) becomes `class="MorphIndicatorSlider"` (every other
  attribute kept); `Profundum_UiClassRule` accepts it as a `CSlider` (`editor_layout_test.cpp:617-619`, `dynamic_cast<CSlider*>`);
- `morph-band` (`:173`, 636 × 204; existing controls end at x 448): `CTextLabel "MORPH MOD"` (456, 104, 120 × 16);
  `COptionMenu control-tag="MorphModSource"` (456, 122, 124 × 20); `ArcKnob control-tag="MorphModAmount"` (588, 104, 40 × 40);
  `CParamDisplay control-tag="MorphModAmount"` (456, 150, 124 × 16) — the P-8 readout rule needs a writer on the same page, which the
  ArcKnob is. `ArcKnob` has no bipolar arc (`arc_knob.h` has only a modulation range, `:72`), so the bipolar reading is the display
  string;
- `tab-modulation` (`:348`, 960 × 516; the Motion block ends near y 181): a `CTextLabel "MORPH MODULATION"` (12, 196) section header
  and three columns mirroring the Motion block's grid (label at x + 0, control at x + 100, row pitch 26):
  - col 1 (x 12): Source `COptionMenu` (112, 219, 196 × 20); Amount `BipolarSlider` (112, 243, 196 × 24) (the Motion Amount control,
    `:355`);
  - col 2 (x 332): Env Attack / Env Decay / Env Sustain `ArcKnob`s (452, 217 / 243 / 269, 24 × 24);
  - col 3 (x 652): Cycle Shape `COptionMenu` (752, 219, 196 × 20); Cycle Rate `ArcKnob` (772, 243, 24 × 24); Cycle Sync
    `CSegmentButton` (752, 269, 196 × 24); Cycle Sync Rate `COptionMenu` (752, 297, 196 × 20).
  Exact pixels are adjusted only if `_UiLayout` reports an overlap at any zoom step.
- Tooltips (FR-032; `&amp;` escaped by the XSLT writer): 1003 "Morph Mod Source: what moves the Bass Morph by itself: Envelope per
  note, Cycle repeating, Velocity of the last note, or the Mod Wheel. With Mod Wheel and a non-zero Amount the wheel moves the morph
  and stops pushing Mass; at Amount 0 the wheel keeps pushing Mass." — contains `stops pushing Mass`; 1009 "… faster than Morph Ramp
  Time allows becomes a smoothed triangle." — contains `Morph Ramp Time`; 1002's existing tooltip (the Advanced ArcKnob, `editor.uidesc:343`) gains "… also smooths
  Morph Mod movement." — contains `Morph Mod`. Every other new control gets a one-sentence tooltip naming its range.
- The editor baseline (one `UpdateBanner`, `CheckForUpdatesButton`, `VersionLabel`) is untouched; `node
  tools/lint-plugin-editor-baseline.js` runs at close.

### 4.11 Controller — `controller/controller.cpp`, `controller.h`

- `initialize` (`:96-102`): `registerMorphModParams(parameters);` after `registerMorphControlParams` (comment 84 → 93).
- `getParamStringByValue` (`:306-332`): `formatMorphModParam` before the base call; the four lists format themselves.
- `setComponentState` (`:142-217`): v1 branch: `setToRegisteredDefault` for 1003–1011; v2 (`version == kStateV2Version`, `:188-189`):
  the same; v3: the name loop gains `namesRead`, then `if (namesRead) loadMorphModParamsToController(s, set);`. No `setParam` for a
  value the processor would not store (FR-024). The nine IDs are never touched by `redisplayEditTarget` (they are not `kSoundRows`,
  `:466-467` iterate that table) or by `copyEndpoint` (FR-018).

### 4.12 Plugin harness — `tests/preset_test_support.h`, `preset_render_support.h`, `param_path_audibility_helpers.h` (append-only)

- `DecodedPresetState` (`preset_test_support.h:40-67`): append `struct MorphMod { std::int32_t source = 0; float amount = 0, attackMs =
  10, decayMs = 300, sustain = 0; std::int32_t cycleShape = 0; float cycleRateHz = 1; std::int32_t cycleSync = 0, cycleSyncRate =
  13; bool present = false; } morphMod{};`. `decodePresetState` (`:115`) reads the 36 bytes at `kStateMorphModOffset` when
  `bytes >= kStateV3Bytes`; `out.ok` (`:182`) already requires `bytes == kStateV3Bytes` (now 610).
- `PresetArmContext` (`preset_render_support.h:118-140`) gains three appended fields:
  `std::optional<DecodedPresetState::MorphMod> morphModCandidate;` (the §5.7 probe's candidate settings),
  `std::optional<std::int32_t> forceMorphModSource;` (the "forced Off" arms of FR-060 (b), C-15 and §5.8) and
  `std::optional<float> morphModWheel;` (the M-WHEEL arms).
- `effectiveScriptFor` (`preset_render_support.h:144-195`) overlays, in this order. (1) Preset context only:
  `e.morphMod = configOf(d.morphMod)`, the same mapping as `morphModConfigOf`, through `kNoteValueDropdownMapping`. In probe
  context the script's own `e.morphMod` stands. (2) Both contexts: if `ctx.morphModCandidate` is set, `e.morphMod =
  configOf(*ctx.morphModCandidate)`. (3) If `ctx.forceMorphModSource` is set, `e.morphMod.source` takes it. (4) If
  `ctx.morphModWheel` is set, `e.morphModWheel` takes it. The engine-direct twin and the fidelity arm render this `e`, so they carry
  exactly what the processor is handed below.
- **The processor carries the effective script's modulation (FR-044, FR-041, FR-045; SC-017, SC-023).**
  `renderScriptThroughProcessor` carries a script's state only through the stream it loads (step 2, `:392-409`) and the one
  parameter-only block before prepare (step 3, `:411-420`; `processorSampleZeroParams`, `:254-283`). `processorCarrierParam`
  (`:211-251`) returns false for any kind it does not list, and `processorRenderRefusal` refuses a script holding such a kind
  (`:323-331`). Three appended edits close that path:
  1. **The C-10 block goes into the stream, bitwise.** Step 2 gains `bytes = withMorphMod(std::move(bytes), e.morphMod);`
     immediately after `withEndpoints(…)` (`:407`), in both contexts. `writeV2Stream(*fresh->proc)` is the fresh processor's own
     `getState` (`profundum_test_fixture.h:1030-1032`), so it is 610 bytes after G3. `withMorphMod` is appended to
     `preset_render_support.h` and uses `patchI32` / `patchF32` (`profundum_test_fixture.h:1054-1055`). It writes the nine fields at
     `kStateMorphModOffset` in C-10 order: source, shape and sync as i32, the floats as their bits, and Sync Rate as the dropdown
     index `i` with
     `kNoteValueDropdownMapping[i] == {cycleNote, cycleModifier}`. That is the inverse of the table (`note_value.h:136-178`); a pair
     not in the table fails a `REQUIRE`. `setState` → `loadMorphModParams` stores in-range finite floats unchanged, so the
     processor runs the same floats as the twin, not a normalised → plain round trip of them. This is how the FR-044 probe's
     candidates (`ctx.morphModCandidate`) and every forced source reach the processor. A forced source needs no separate byte
     patch, because `forceMorphModSource` acts on `e.morphMod` (overlay step 3) and this write carries it.
  2. **The wheel is a parameter.** `processorSampleZeroParams` appends `(kModWheelId, e.morphModWheel)` after the polyphony point
     when `!(e.morphModWheel == 0.0f)`. Parameter 6 stores its normalised value as-is (`performance_params.h:55`), so 0 and 1 are
     exact, and every pre-phase script's
     parameter block is unchanged. `processorCarrierParam` gains `case K::MorphModWheel: id = P::kModWheelId; norm =
     std::clamp(double(ev.value), 0.0, 1.0); return true;`, so a mid-render wheel move rides the existing carrier path.
  3. **Bypass and mid-render config changes are refused by name.** `MorphModBypass` has no parameter carrier. Compare is an
     IMessage (`kMsgCompare`, `plugin_ids.h:85`, handled at `processor.cpp:715-726`), and Compare A / B also forces the morph
     position (`processor.cpp:974-976`), so carrying the script's bypass through it would change more than the bypass. A
     `MorphModulation` event would have to become nine parameter points, and no processor-path script in this phase uses one: its
     users (SC-004, SC-010 (g), SC-012 (c)/(d)) are engine-direct. `processorRenderRefusal` therefore gains, before the event loop:
     `if (e.morphModBypass) return "no processor carrier: Morph Mod bypass (Compare is an IMessage that also selects the
     endpoint; use renderPresetSweep's MorphModOverride)";`. The event loop's refusal names the kind for `MorphModulation` and
     `MorphModBypass`. Processor-path Compare renders go through `renderPresetSweep` (below).
  SC-023 (e) pins the result. After the sample-0 block and prepare, the processor's pushed config equals the twin's, for a
  probe-context script and a preset-context M-WHEEL arm (§6).
- `ppRenderProcessor` (`param_path_audibility_helpers.h:631`): the twin takes `t.position = fx->engine()->morphBasePosition();
  t.morphMod = fx->engine()->morphModulation(); t.morphModWheel = fx->engine()->morphModWheel(); t.morphModBypass =
  fx->engine()->morphModBypass();` and keeps the script's own note events (velocities included), so a Velocity-source twin sees the
  same velocities.
- **`renderPresetSweep` override (SC-007, SC-008; FR-060 (a)/(b)).** `renderPresetSweep(comp, notes, fs, maxMass, seed)`
  (`:1635-1665`) can patch only the seed word. Its `maxMass` path writes Mass 100 under A and B together with Mod Wheel 1. It
  gains one trailing defaulted parameter (append-only; every existing caller is unchanged):
  ```cpp
  struct MorphModOverride {
      std::optional<std::int32_t> source;            // i32 at kStateMorphModOffset, patched into the copied Comp before setState
      std::optional<float> amount;                   // f32 at kStateMorphModOffset + 4, same patch
      std::optional<double> wheel;                   // one parameter-only block {6: wheel}, no Mass / Edit Target writes
      std::optional<::Profundum::CompareMode> compare;   // kMsgCompare "mode" via proc->notify, after setState, before prepare
  };
  renderPresetSweep(comp, notes, fs, maxMass, seed = std::nullopt, const MorphModOverride& mm = {});
  ```
  The source and amount are patched with `patchI32` / `patchF32` (`profundum_test_fixture.h:1054-1055`) next to the seed patch (`:1646-1648`). The wheel block runs after the
  `maxMass` blocks, and `REQUIRE(!(maxMass && mm.wheel))` stops two wheel writers. The compare is sent after `setState`, because
  `setState` resets the compare to A·B (`processor.cpp:649-652`). The message is built as `phase11_alloc_test.cpp:81-89` builds
  it (a `HostMessage`, `setMessageID(kMsgCompare)`, `setInt("mode", …)`), sent with `REQUIRE(fx->proc->notify(msg) ==
  kResultOk)`, and after `fx->prepare` a `REQUIRE(fx->proc->compareModeForTest() == *mm.compare)` proves that prepare did not reset
  it. SC-007 and §5.8 call only this override. They do not patch bytes by hand.
- **The four M-* showcase cases (FR-041, C-15; SC-017 (b)).** `showcaseArmPlan` (`:1225-1262`, ending in `case C::Count:
  pl.applicable = false;`) gains, before `C::Count`:
  ```cpp
  case C::MEnv: case C::MCycle: case C::MVel: case C::MWheel: {
      const auRd::AuSetting s = sd::settingOf(pl.state);
      pl.declared = {{s.a, s.b}};                 // the stored pair: no arm edits an endpoint (the AT-41 / AT-42 declaration)
      pl.descriptor = "D_nl";
      pl.windowMs = kMorphModWindowMs;            // W = kLowFrequencyFftSize / 48 kHz = 5461.33 ms (§5.5)
      pl.cell = [c](const DecodedPresetState& q, std::uint8_t note) { return morphModTrajectoryCell(c, q, note); };
      break;
  }
  ```
  `pl.notes` keeps its default `{kAuE1, kAuC3}` (`:824`). A primary arm records no override (Phase 13 FR-012). The lo / hi arms
  (C-15) are built inside the reader:

  | Column | lo arm | hi arm |
  |---|---|---|
  | M-ENV | `ctx.forceMorphModSource = 0` | as stored (`forceMorphModSource` unset) |
  | M-CYCLE | `ctx.forceMorphModSource = 0` | as stored |
  | M-VEL | as stored, the script's note-on `velocity = 1` | as stored, `velocity = 127` |
  | M-WHEEL | as stored, `ctx.morphModWheel = 0` | as stored, `ctx.morphModWheel = 1` |

  **The reader** `[[nodiscard]] inline auRd::AuCell morphModTrajectoryCell(Capability c, const DecodedPresetState& q,
  std::uint8_t note)` is appended to `preset_render_support.h`. It is plugin test code (C-17 item 4), not in
  `profundum_audibility_readers.h` (P-8). It implements §5.5:
  1. It takes the active context, `auto* ctx = static_cast<PresetArmContext*>(auRd::render::scriptRenderHook().ctx)`
     (`profundum_audibility_readers.h:60-67`), and requires `ctx != nullptr && ctx->preset != nullptr`, because a showcase cell
     runs only in preset context (`showcaseColumn` installs the scope). It saves `forceMorphModSource` / `morphModWheel` and
     restores them on return.
  2. It builds `sc = auRd::auScript(sd::settingOf(q), note, kMorphModArmTotal)` (`:262-278`: host block 64, MONO, the note on at
     0). For M-VEL it sets `sc.events[0].velocity` (`profundum_voice_engine_test_helpers.h:131-138`).
  3. For each arm it sets the lever on `*ctx` and renders `auRd::render::voiceEngineRenderWithTwin(sc)` (`:72-75`). The hook
     routes that to `processorRenderHook` → the processor render plus the engine-direct twin, both carrying the arm's
     modulation (items 1-2 above), and records the C-5 fidelity arm as today.
  4. It places the window from both arms' `taps.grid[i].resolved.position` (§5.5).
  5. It reads D_nl with the AT-41 reader unchanged. `auBm::dNlDb` (`bass_morph_test_helpers.h:771`) reads at
     `readWindow`'s fixed start `kSettleSeconds = 1.0 s` (`:703-707`). The reader therefore hands it a copy of the arm's taps
     whose `characterL/R` and `linearCoreL/R` start at sample `t0 − llround(1.0 · fs)`, so the reader's frame lands on
     `[t0, t0 + W)`.
  6. It fills the `AuCell` as `auSteadyCell` does (`profundum_audibility_readers.h:669-697`). The row is `"M-ENV"` …
     `"M-WHEEL"`, the contrast names the arm pair, the tap is `"Char / twin"`, the bar is `kAuBarDb` / `">= 3 dB"`, and
     `value = |hi − lo|`. `pass` requires both readings valid and finite, `value ≥ bar`, and the §5.5 window precondition. The
     remark carries `t0`, `p_lo` and `p_hi`. On a precondition failure it carries "no window: resolved-position range lo [..]
     hi [..]" and `pass = false`.
  These cells go to the Phase 13 coverage matrix through the existing showcase writer, not a new one.
  `scRunShowcaseShard` (`integration/preset_showcase_test.cpp:85-147`) iterates every def's primary and secondaries and writes
  each `ShowcaseArm` to `specs/profundum-phase13-presets-release/evidence/coverage-matrix/shard-<k>.csv` (`scCoverageDir`,
  `:79-83`). `evidence/merge-csv.js` merges the shards into `specs/profundum-phase13-presets-release/evidence/coverage-matrix.csv`
  (FR-041). The four new columns therefore appear there as soon as a def claims them. The SC-018 row checks the merged file
  for the four labels.

### 4.13 Factory library — `tools/profundum_preset_defs.h`, `tests/profundum_preset_host.h`

- `CapabilityGroup` gains `MorphMod`; `Capability` (`profundum_preset_defs.h:64-68`) gains `MEnv, MCycle, MVel, MWheel` before
  `Count`; `kNumCapabilities == 57` (`:70`); `capabilityTable()` rows "M-ENV Morph Mod Envelope", "M-CYCLE Morph Mod Cycle",
  "M-VEL Morph Mod Velocity", "M-WHEEL Morph Mod Wheel", `atId = 0`.
- `ProfundumPresetDef::controls` comment (`:153`) and `isAllowedControlId` (`profundum_preset_host.h:234-240`) allow 1003–1011. The
  host's size check (`:432-435`) follows `kStateV3Bytes` (610).
- Conversions follow §5.7; converted presets' `description` is rewritten to name the new primary (Phase 13 FR-065,
  `preset_description_test.cpp`). Every factory file is regenerated by `profundum_preset_generator`; no hand-edited `Comp`.
- The four columns' showcase arms are the `showcaseArmPlan` cases and the `morphModTrajectoryCell` reader of §4.12. Their cells
  reach the Phase 13 coverage matrix through `scRunShowcaseShard` (FR-041).
- FR-063 has a pre-phase reference. G0 writes `evidence/prephase-claims.csv` from the pre-phase `allPresets()`
  (`tools/profundum_preset_defs.h:145-160`: `primary`, `secondaries`). Each row is `preset name, primary label, secondary labels
  sorted and joined by ';'`. The labels are `capabilityTable()` labels (for example `AT-01`), not enumerator integers, so the
  appended `MorphMod` enumerators cannot shift a row. §6 SC-018 compares against it.

### 4.14 Documentation

`plugins/profundum/MANUAL.md`: a "Morph Modulation" section with all nine registered titles (SC-020; `manual_coverage_test.cpp:82`
→ 93), the formula, the five sources, the envelope restart (every accepted note-on, from 0) and rest at Sustain, the free-running
Cycle and its sync, the Velocity hold, the Mod Wheel / Mass hand-over (non-zero Amount), the Compare bypass, the slew consequence
(C-9) and "modulation is silent where the clamp holds the base at an endpoint". The open `[1.0.0]` CHANGELOG entry gains one bullet
and its counts read 93 parameters / 610-byte state. `version.json` is not edited.

---

## 5. Algorithms (exact)

### 5.1 Per-interval composition (C-2, FR-011, FR-012)

At each grid point, after step 1 (settings, then queued notes in arrival order, each accepted note-on calling `morphMod_.noteOn(v)`):

1. `setConfig(pending.config)` (change-only); `setModWheel(pending.wheel)`.
2. `e = env.process()`, `l = clamp(lfo.process())`; `s` = the selected source's value (§4.1).
3. `transparent = (source == Off) ∨ (bits(amount) & 0x7FFFFFFF == 0) ∨ bypass`.
   `target = transparent ? b : clamp(b + amount · s, 0, 1)` — single float multiply-add; under `-ffp-contract=fast` the macOS leg may
   fuse it into an FMA, which changes `target` by ≤ 1 ulp and is covered by every tolerance below (no bit-equality claim is made on
   the non-transparent arm).
4. `morph_.setPosition(target)`; then step 2 `morph_.advanceInterval()` slews p toward it by at most `Δp_max = 32 000 / (rampMs · fs)`
   (`bass_morph_engine.h:369-373`).

The grid record carries `s`, `clamp(amount · s, −1, 1)` and `target`.

### 5.2 Envelope (C-4) at the control rate `fc = fs / 32`

ADSREnvelope's one-pole (`envelope_utils.h:88-110`): `rate = max(1, T_ms · 0.001 · fc)`, attack `coef = exp(−ln((1 + 0.3)/0.3)/rate)`,
`base = 1.3 (1 − coef)`; decay ratio `1e-4`, `base = (0 − 1e-4)(1 − coef)`; Sustain stage `out = S + c_s (out − S)` with
`c_s = exp(−5000 / (5 · fc))` (`adsr_envelope.h:72`, `:242-245`). A float32 emulation of these recursions (scratch probe, this
session) gives:

| fs | first Attack step, A = 10 ms | A = 0.1 ms | intervals to peak, A = 10 / 400 / 10 000 ms (nominal) | decay 1→0, D = 10 / 400 / 10 000 ms (nominal) | Sustain step settle to 1e-3 |
|---|---|---|---|---|---|
| 48 000 | 0.1211 | 1.0 | 15 / 600 / 15 000 (15 / 600 / 15 000) | 16 / 600 / 15 000 (15 / 600 / 15 000) | 11 |
| 44 100 | 0.1312 | 1.0 | 14 / 552 / 13 782 (13.78 / 551.25 / 13 781.25) | 14 / 552 / 13 782 | 10 |
| 96 000 | 0.0620 | 1.0 | 30 / 1 201 / 30 001 (30 / 1 200 / 30 000) | 31 / 1 201 / 30 001 | 21 |

All counts sit within ±1 interval of nominal, so SC-002 (b)/(b′) hold as written, and the C-4 sustain counts (11 / 10 / 21) are
confirmed. The unit test re-measures them in C++ and prints the counts (the emulation used double `exp` rounded to float; the test is
authoritative).

### 5.3 Cycle (C-5)

`LFO` at `fc`, free-running; `retrigger()` never called. Free rate period `1 / f` (clamped [0.01, 20] Hz, `lfo.h:218-223`). Synced:
`f = (bpm / 60) / getBeatsForNote(note, mod)` clamped to the same range (`lfo.h:489-499`). Phase resolution at `fc` = 1500 Hz: a
20 Hz rate is 75 intervals per cycle, so SC-003 (b)'s 0.5 % period bar is measured by zero-crossing interpolation over ≥ 10 cycles
(linear interpolation between grid points, as the rate is constant).

### 5.4 Velocity, Mod Wheel (C-6, C-7)

Velocity: `s = (v == 127) ? 1 : v / 127` latched in `noteOn`, held until the next accepted note-on; 0 after reset. Mod Wheel: the
engine's pending wheel, clamped [0, 1], handed at the grid point. Processor Mass rule: §4.7.

### 5.5 C-15 window placement

Implemented by `morphModTrajectoryCell` (§4.12), for the showcase cells and the §5.7 probe alike.

- **Window length is the reader's frame.** The AT-41 / AT-42 reader reads D_nl from one `kLowFrequencyFftSize = 262 144`-sample
  frame (`tests/test_helpers/low_frequency_metrics.h:64`; `renderedHarmonicPowers`, `profundum_core_test_helpers.h:235-237`)
  starting at `readWindow`'s `kSettleSeconds = 1.0 s` (`bass_morph_test_helpers.h:703-707`). The window is therefore
  `W = 262 144` samples, which is 5.461 s at 48 kHz. An earlier draft used a 1.0 s window, which that reader cannot read (Review
  notes).
- **Render.** Each arm renders `kMorphModArmTotal = grid-round(3.0 s · fs + W) + 32` samples (8.46 s) at 48 kHz, host block 64, MONO.
  Each arm plays one note: E1 or C3 (the Phase 13 notes), held for the whole render, at velocity 100 (M-VEL: 1 / 127). The arm
  records the grid-record `resolved.position` per interval.
- **Placement.** The window is the earliest `[t0, t0 + W)` with `t0 ∈ [1.0 s, 3.0 s]` on the 32-sample grid inside which `max − min`
  of the resolved position is ≤ 0.01 in **both** arms. The hi arm must also satisfy `|p̄_hi − p̄_lo| ≥ 0.05`, the window means
  (non-vacuity). With no such window the arm fails its precondition and reports the trajectory's range. It is never re-windowed by
  hand.
- **Reading.** D_nl is read on the Char / twin tap with `auBm::dNlDb` unchanged (`bass_morph_test_helpers.h:771`), on taps shifted
  so that its fixed 1.0 s start lands on `t0` (§4.12 reader step 5). The contrast is `|D_nl(hi) − D_nl(lo)|`, as `auSteadyCell`
  forms the AT-41 / AT-42 cells (`profundum_audibility_readers.h:669-697`, `:1981-1990`). The bar is ≥ 3 dB at E1 and at C3.

### 5.6 Main-page rows (FR-031)

Through `ppRenderProcessor` (§4.12), window `[1.0 s, 2.0 s)` (`main_page_audibility_test.cpp:33-34`), endpoints `auStateAs()` /
`auStateB()` (the AT-41 pair, `profundum_audibility_readers.h:1983`) loaded via `setState`; the lever and held values are normalised
parameter changes: 1003 row `{1003: 0 → 0.75 (Off → Velocity), 1004: 1.0 (Amount +1), 105: 0}` with every script note at
velocity 127 (R7, 2026-10-27: replaces Off → Mod Wheel, whose lo arm the wheel's Mass push confounded, P-6); 1004 row `{1003: 0.75
(Velocity), 1004: 0.5 → 1.0, 105: 0}` with every script note at velocity 127. Preconditions (FR-031): the resolved position over
the window varies ≤ 0.01 and is ≥ 0.9 in the hi arm, ≤ 0.01 in the lo arm. P-6's confound is logged.

### 5.7 Probe and conversion selection (FR-044, FR-046, C-14)

1. **Probe (hidden `[.probe]`, before any definition opts in):** for every existing factory preset P (Source Off as stored) and every
   source column X, render X's C-15 arms over a fixed candidate grid, at E1 and C3:
   - M-ENV: Amount `σ · {1, 0.6}` where `σ = +1` if P's stored position ≤ 0.5 else −1; Attack {0.1, 150} ms; Decay 600 ms; Sustain
     {1, 0.6} (8 candidates; "rise into B and stay");
   - M-CYCLE: Amount {+1, −1}; Shape {Square, S&H}; Rate 0.05 Hz free (4 candidates). A Square half-period and an S&H hold
     last 10 s and 20 s, which covers `[1.0 s, 1.0 s + W)` with W = 5.461 s (§5.5). Both signs are tried because which extreme
     the free-running LFO holds first is a property of `LFO::reset()`'s phase, not of σ;
   - M-VEL: Amount `σ · {1, 0.6}` (2 candidates);
   - M-WHEEL: Amount `σ · {1, 0.6}` (2 candidates).
   Each candidate reaches both the processor and the twin through `ctx.morphModCandidate` (§4.12 harness item 1). The candidate
   is written into the rendered stream bitwise, so the processor arm runs it; it is not left as Source Off as stored.
   Each row of `evidence/probe-morphmod.csv`: run id (git short hash + UTC date), preset, column, candidate settings, note, window
   t0, `p_lo`, `p_hi`, D_nl lo / hi, contrast, pass.
2. **Carriable:** P carries X if some candidate passes at both notes; its margin is `min(contrast_E1, contrast_C3) − 3 dB`.
3. **Eligibility (C-14, FR-042, FR-046):** P is eligible for X only if both hold:
   - P claims no single-state column (Phase 13 FR-007 list), as primary or as secondary. Single-state secondaries make a preset
     ineligible; they are never dropped to make it eligible;
   - P's current primary keeps ≥ 1 other primary after P moves.
4. **Selection (rule (i) proposed; the ruling is written into FR-046 first, P-9, §11 Q2):**
   - Rule (i): per column, the eligible carrier with the largest margin (ties: definition order), one conversion per column.
   - Rule (ii): every eligible carrier converts, each for the column of its largest margin.
   - Under either rule a preset converts for at most one column.
   - A column with no eligible carrier gets **one new preset**. It is built from the probe row that cleared the bar with the
     largest margin, on a base the probe names. Its "no existing preset could carry it" proof is the column's probe rows (SC-018).
     **R9 (user, 2026-10-27):** the new preset keeps the probe row's nine Morph Mod values but carries its own designed endpoints,
     never a copy of the base's (a copy reads distinctness d 0 against the base at rest); it clears FR-041 and FR-043 on them.
   - **R8 (user, 2026-10-27):** `Profundum_PresetMorphEndpoints_*` measures a preset that stores a source other than Off with A
     rendered as a both-endpoints-A preset (as B is both-endpoints-B), so the modulation cannot move the designed sounds; every
     other Phase 13 gate measures the preset as stored.
5. The converted definition adds the winning candidate's nine `controls` values (1003–1011, normalised by the §4.5 laws) and
   moves its primary to X. Its old primary is not re-claimed as a secondary; step 3 already guarantees that the old primary column
   keeps another primary. Its secondaries are kept unchanged: by step 3 none of them is single-state. Its description is rewritten.
   SC-018 checks the converted set against this rule applied to `probe-morphmod.csv` (P-9).

### 5.8 Pre-phase harvest and close comparison (FR-062, SC-008)

- **G0 harvest** (`Profundum_MorphModPrePhaseHarvest`, `[.harvest][morphmod]`, built and run at HEAD before any engine or generator
  edit): for every factory preset, `renderPresetSweep(comp, {note}, 48000, false)` for note ∈ {E1, C3} (`preset_render_support.h:1635`),
  `fingerprintRender` of `capturedL` and `capturedR` separately; written to `evidence/prephase-fingerprints.csv` as `preset, note,
  channel, rms, peak, meanAbs, totalVariation` in `%a` hex-float plus 32 checkpoint bit patterns as hex `u32`; and every preset's decoded
  pre-existing fields (globals, macros, note block, morph-control block, seed, both endpoints' 62 leaf words, runtime blob, both names)
  as hex bits to `evidence/prephase-decoded.csv`. It also writes every pre-phase definition's claims to
  `evidence/prephase-claims.csv` (§4.13; FR-063). The CSVs are evidence read by a test, never C++ literals
  (`lint-float-bit-goldens.js` scans sources).
- **Close comparison** (`Profundum_MorphModPrePhaseFingerprints`, `[.harvest][morphmod]`, run by the close lane): every unconverted
  preset as stored (`renderPresetSweep(comp, {note}, 48000, false)`) and every converted preset forced to Source Off
  (`renderPresetSweep(comp, {note}, 48000, false, std::nullopt, {.source = 0})`, the §4.12 `MorphModOverride`), the same renders, `compareFingerprints(actual, ref)`
  with the shared tolerances; `withinTolerance()` for every row, 0 failures; worst metric / sample errors printed. Every preset's
  decoded pre-existing fields bit-equal to `prephase-decoded.csv` (FR-061).

---

## 6. Test plan (FR / SC → TU, TEST_CASE, assertion)

All new cases carry `[profundum][morphmod]`. New DSP TUs: `dsp/tests/unit/systems/profundum_morph_modulator_test.cpp`,
`profundum_engine_morphmod_test.cpp`. New plugin TUs: `plugins/profundum/tests/integration/morph_mod_processor_test.cpp`,
`morph_mod_preset_test.cpp`.

| SC | TU → TEST_CASE | Assertion strategy |
|---|---|---|
| SC-001 (a) | `profundum_morph_modulator_test.cpp` → `ProfundumMorphModulator_ApiAndRt` | `auto [a,b,c,d,e,f,g,h,i,j] = Config{};` compiles (exactly 10 members: the 9 C-12 fields with Sync Rate as note + modifier); `STATIC_REQUIRE(std::is_base_of_v<ModulationSource, Output>)`; `getSourceRange() == {-1, 1}`; FR-007 value contract: per source (Envelope mid-Attack, Cycle, Velocity 64, Mod Wheel 0.3) × Amount ∈ {−1, 0.5, 1}, after each `advanceInterval()`, `output().getCurrentValue()` and `offsetValue()` are bit-equal (the same member) and `|getCurrentValue() − std::clamp(amount · sourceValue(), −1, 1)| ≤ 1e-7` (a tolerance, not bit-equality, because the test TU's product is a separate computation); FR-006 at modulator level: `setModWheel(1.5f)` → `s == 1.0f`, `setModWheel(-0.2f)` → `s == 0.0f`, and a quiet-NaN bit pattern made via `std::bit_cast` of a `volatile std::uint32_t` (`0x7FC00000u`) keeps the previous `s`; every member `noexcept` (`STATIC_REQUIRE(noexcept(...))`); `AllocationScope` around 10 000 calls of `setConfig` / `noteOn` / `setModWheel` / `advanceInterval` per source → 0. Layer check: `node tools/lint-layers.js` in the close lane. |
| SC-001 (b) | `profundum_engine_morphmod_test.cpp` → `ProfundumEngine_MorphModNoAlloc` | `VoiceEngineScript::countAllocations = true`, each non-Off source × Amount ±1, a `MorphModulation` + `MorphModWheel` + `MorphModBypass` event every host block → `taps.allocations == 0`. |
| SC-001 (c) | `unit/phase11_alloc_test.cpp` → `Profundum_Phase11PathsDoNotAllocate` (extended) | routed-ID list (`:64`) gains 1003–1011; Source rotated through 0–4, Amount ±1, Mod Wheel moved every block, Compare toggled; 0 allocations in `process()`; `:182` count 93. |
| SC-002 | `profundum_morph_modulator_test.cpp` → `ProfundumMorphModulator_Envelope` | per fs ∈ {48 000, 44 100, 96 000}: (a) trace vs a reference `ADSREnvelope` prepared at `fs / 32`, Hard, same A/D/S, `reset(); gate(true)` at the same interval: `|Δ| ≤ 1e-6` per interval; (b) first interval reading 1.0 within `round(A · fc / 1000)` ± 1 (A = 0.1 → interval 0); (b′) Sustain 0: intervals from peak to first 0 within ± 1 of `D · fc / 1000`; (c) Sustain ∈ {0.5, 1}: Decay ends at `s == S` (bits) and `|s − S| ≤ 1e-6` after; S = 0: `|s| ≤ 1e-3` from A + D + 1 interval; (d) Sustain 0 → 1 mid-Sustain: settle count ≤ {11, 10, 21}, trace vs `out = S + c_s (out − S)` to 1e-6 (counts printed); (e) restart at an interval with `s > 0.5`: `s` equals a fresh reference's first `process()` ±1e-6 and is lower than the previous interval for A ≥ 10 ms; (f) Sustain-stage tail (§4.1 flush): Sustain 1, held into Sustain, then Sustain 1 → 0 mid-Sustain. On x86 the case clears FTZ / DAZ for its duration and restores them on exit, guarded by `#if` like the existing `enableFTZDAZ` helpers. A leg whose runtime forces FTZ still passes; it just exercises the flush less directly. `s` reads bitwise `+0` (`std::bit_cast<std::uint32_t>(s) == 0u`) no later than `⌈ln(1e-15) / ln(c_s)⌉ + 1` intervals after the step: 53 / 49 / 105 at 48 / 44.1 / 96 kHz, with `c_s` = 0.513 / 0.484 / 0.717. It stays `+0` for the next 1 000 intervals, and the grid-level offset reads `+0` too. The count is printed. |
| SC-003 | same TU → `ProfundumMorphModulator_Cycle` | (a) 6 shapes vs reference `LFO` at `fs / 32` (same setters, same order) ±1e-6; (b) zero-crossing period over ≥ 10 cycles (≥ 2 at 0.01 Hz) within 0.5 % for {0.01, 0.1, 1, 10, 20} Hz, synced 90 / 120 BPM × {1/4 (13), 1/8T (9), 1/1 (19)} vs `beats · 60 / bpm`; (c) range ⊂ [−1, 1]; (d) S&H and Smooth Random: two runs bit-identical (same binary, same code), and identical across two `ProfundumEngine` seeds at engine level; (e) ≥ 8 irregular `noteOn`s with varied velocities: trace equals the no-note reference ±1e-6, all shapes, free and synced. |
| SC-004 | `profundum_engine_morphmod_test.cpp` → `ProfundumEngine_MorphModVelocity` | MONO and POLY4: grid-record `morphModSource == v/127 ± 1e-6` for v ∈ {1, 64, 127} (127 → exactly 1.0); 0 before the first note; unchanged through note-off + 2 s silence; changes at the next note's grid point. Dropped note-on (FR-013): with the queue empty at the start of one grid interval (`kMaxQueuedEvents = 256`, `profundum_engine.h:102`), 257 note-ons are issued at one sample inside that interval. Notes 1-256 cycle the note numbers `36 + (i % 48)` (all `< kNumMidiNotes`, so none is dropped by the range check, `:224-225`) at velocity 64. Note 257 is at velocity 120. `noteOn` drops only at `queueCount_ >= 256` (`:230-233`), so the test REQUIREs `droppedEvents()` (`:413`) to rise by exactly 1, then asserts `morphModSource == 64/127 ± 1e-6` at that grid point. The dropped velocity 120 never reaches the source. |
| SC-005 | same → `ProfundumEngine_MorphModComposition` | (a) `morphTarget == clamp(b + A · s, 0, 1)` recomputed from the record's `s`, ±1e-6; (b) `bit_cast<u32>(morphTarget) == bit_cast<u32>(b)` when Source Off, A = ±0, or bypass; (c) `resolved->position ∈ [0, 1]`; (d) `|Δp| ≤ Δp_max + 0.5 ulp(p)` per interval for every source × A ∈ {−1, −0.5, 0.5, 1} × b ∈ {0, 0.5, 1} × ramp ∈ {5, 40, 2000} ms (the Phase 8 B3-1 bar); (e) FR-006 at engine level (Mod Wheel source, Amount 1, b 0): `setMorphModWheel(1.5f)` → the next grid record's `morphModSource == 1.0f`; `setMorphModWheel(-0.2f)` → `0.0f`; after a wheel of 0.4, a quiet-NaN bit pattern (via `volatile std::uint32_t`) keeps `morphModSource == 0.4f` and `morphModWheel()` unchanged. |
| SC-006 | same → `ProfundumEngine_MorphModTransparentAtZero` | MONO and POLY4 × {default, Phase 8 showcase pair `auStateAs()` / `auStateB()`, a Fold + Wide pair} × {E1, C3}, 2 s: Off vs each source at Amount 0 (wheel 0), and Off vs bypass with Amount 1: `outL`, `outR` bit-identical (`std::memcmp`). Same code, same binary: the transparent branch makes the computations identical. |
| SC-007 | `morph_mod_preset_test.cpp` → `Profundum_MorphModPresetTransparency_kofN` (N shards, §8) + `preset_library_test.cpp` `Profundum_FactoryPresetStateFormat` (extended) | every preset × {E1, C3} through `renderPresetSweep(comp, {note}, 48000, false, std::nullopt, mm)` with the §4.12 `MorphModOverride` `mm`. A stored-Off preset renders as stored (`mm = {}`) and, for each source X, as `{.source = X, .amount = 0.0f, .wheel = w}` with w ∈ {0, 1}. A converted preset renders as `{.source = 0}` and as `{.source = X, .amount = 0.0f, .wheel = w}` for each X and w. Every preset renders as `{.source = 0, .compare = C}` and as `{.source = S, .amount = 1.0f, .compare = C}` with C ∈ {A, B}, where S is the stored source (or every non-Off source for a stored-Off preset). Every pair is `memcmp` bit-identical; every `Comp` reads literal `3` as its first int32 and is 610 bytes; decoded pre-existing fields vs `prephase-decoded.csv`. Shard wall clock recorded. |
| SC-008 | `morph_mod_preset_test.cpp` → `Profundum_MorphModPrePhaseFingerprints` (`[.harvest]`) | §5.8; 0 failures. |
| SC-009 | `unit/midi_mapping_test.cpp` → `Profundum_MorphModWheelRouting` | (a) Source 4, Amount ∈ {1, −0.5}: wheel 0 → 1 leaves both engine endpoints' `mass` bits unchanged (`engine()->getEndpoint`), grid `morphModSource == 1` at the next grid point; (a′) Amount ±0: Mass pushed bitwise as `Profundum_MassPerformanceOffset` (`:78`, unedited) computes; (a″) Amount 0 → 0.5 / 0.5 → 0 with wheel 0.25: the engine endpoints' `mass` bits (`engine()->getEndpoint`) change at that block and not on the next one, and the Mass push stops / resumes; (b) every other source: `Profundum_MassPerformanceOffset` arithmetic bitwise; (c) pressure 0.5 → Mass +50 under every source; (d) Source change to / from 4 at Amount ≠ 0 with wheel 0.25 → both endpoints re-sent once. |
| SC-010 | `profundum_engine_morphmod_test.cpp` → `ProfundumEngine_MorphModRetrigger`; `morph_mod_processor_test.cpp` → `Profundum_MorphModPedalRestrike` | Preconditions: A ≥ 10 ms, S = 1, `s > 0.5` the interval before. (a)–(e), (g), (h) as spec SC-010, each restart compared to a fresh reference's first step ±1e-6; (c) the chord's trace equals a single note-on's ±1e-6 and velocity is the last note's; (f) processor fixture with pedal (5) points: second note-on of the latched key restarts. |
| SC-011 | `profundum_engine_morphmod_test.cpp` → `ProfundumEngine_MorphModClickFree_kofN` | showcase pair, E1, every source × A ±1 × time extremes (A / D ∈ {0.1, 10 000} ms; Cycle {0.01, 20} Hz × 6 shapes; ramp {5, 2000} ms): (a) the SC-005 (d) bound; (b) `max |y[n] − y[n−1]| ≤ 2 ×` the max of the same metric on static renders at the trajectory's min and max p; (c) 0 non-finite (`detail::isFinite`). Sharded ≤ ~15 s each. |
| SC-012 | `unit/midi_event_test.cpp` → `Profundum_MidiEventTranslation` (four new `PartitionInvariance` sections); `profundum_engine_morphmod_test.cpp` → `ProfundumEngine_MorphModLifecycle`; `morph_mod_processor_test.cpp` → `Profundum_MorphModCompareBypass` | (a) existing script and partitions (`:240-241`), per source Amount 1, Morph 0.5, wheel 1 from sample 0; non-vacuity: some `morphTarget != b`; 0 mismatches; (b) same seed twice bit-identical; (c) re-`prepare` at 96 kHz keeps `morphModulation()`; `reset` zeroes Envelope / Velocity `s`, keeps wheel and config; (c′) tempo forwarding (FR-014): Cycle Sine synced to 1/4 (index 13), Amount 1, `engine.setTempoBpm(90.0f)` (`profundum_engine.h:258-261`) before the first block. The grid-record `morphModSource` zero-crossing period over ≥ 10 cycles (§5.3 interpolation) is within 0.5 % of `60 / 90 = 0.6667 s`. Without the `applySettings` forwarding line (`:768-770`) it would read 0.5 s, the LFO's own 120 BPM default (`lfo.h:548`). A second arm at 120 BPM reads 0.5 s ± 0.5 % (non-vacuity); (d) Compare A·B → A for 0.5 s and back, Envelope (A 400 ms, S 1) and Cycle 1 Hz: during bypass `morphTarget` bits == b; `morphModSource` ±1e-6 of a no-bypass run at every grid point. |
| SC-013 | `unit/state_v3_test.cpp` → `Profundum_StateV3MorphMod`; extended `Profundum_StateVersions` (`state_v2_test.cpp:421`), `Profundum_StateRoundTrip` (`state_roundtrip_test.cpp:230`) | (a) nine non-defaults round-trip at 610 bytes (floats bitwise, ints equal); (b) v1 / v2 load defaults; (c) cut at each offset 574–609: prior values from the cut on, processor and controller; (d) NaN, ±inf patterns, source −1 / 5, shape 6, index 30, sync 2 not stored; finite out-of-range floats clamp; (e) engine-level: `serializeMorphRuntime` while Envelope runs → bytes 1–4 == `bit_cast(b)`, and a fresh `BassMorphEngine` restored from it reads `targetPosition() == b` (pins `kMorphRuntimeTargetOffset`); processor-level getState blob target == registered position; (f) load-then-save byte-stable; (g) `STATIC_REQUIRE(kCurrentStateVersion == 3)`, first int32 literal 3, a valid 610-byte body with first int32 4 → `kResultFalse`, no parameter changed; (h) engine-level restore reset (C-11, P-2, P-3; in `ProfundumEngine_MorphModLifecycle`). The blob comes from a second engine at position 0.75 with Source Off (`serializeMorphRuntime`). Velocity source, Amount 1, a note at v = 100 rendered until `morphModSource == 100/127 ± 1e-6`, then `restoreMorphRuntime(blob)` returns true mid-stream. Assertions: the next grid record's `morphModSource == 0.0f` (Velocity reset); `morphBasePosition()` is bit-equal to the float at blob bytes 1–4 (copied, not recomputed); with Envelope source in the same setup, `morphModSource == 0.0f` at the next interval with no note-on (envelope Idle); with Cycle source (1 Hz Sine, free), the next interval's `morphModSource` equals a freshly `prepare`d modulator's first `advanceInterval()` ± 1e-6 (LFO re-phased). |
| SC-014 | `integration/processor_cpu_test.cpp` → `Profundum_ProcessorCpuMorphMod`; `profundum_engine_perf_test.cpp` → `VoiceEngine_WorstIntervalMorphMod`; `Profundum_PresetCpu` | `[.perf]`, run alone by `node tools/run-cpu-tests.js`; the Phase 11 SC-020 method; bars 8 % MONO / 25 % 4 voices; worst 64-sample block ≤ 100 %; modulator ns per `advanceInterval` printed (expected tens of ns: one ADSR and one LFO step). |
| SC-015 | `unit/param_table_test.cpp` → `Profundum_ParamTableConformance` (`:380`), `Profundum_SoundParamTableRouting` (`:46`) | 93 registered; the nine rows' titles / units / flags / defaults / list strings; normalised ↔ plain round trip; 1005 vs 913, 1009 vs 904, 1004 vs 902 identical display strings; routes cover 0..1011, 93 routed, `kSoundRows` 60; every moved literal (C-12, P-4) reads 93 / 89. |
| SC-016 | `unit/controller/editor_layout_test.cpp` (`_UidescBindsEverySurfaceId`, `_UidescTagIntegrity`, `_UiLayout`, `_UiPlacement`, `_UiZoom`, `_UiLabels`, `_UiClassRule`) | all pass; `morph-band` binds exactly {1003, 1004} of the nine; `tab-modulation` binds all nine; tooltip substrings; shared-view count unchanged (`editor_lifecycle_test.cpp:224` → 93). |
| SC-017 | `integration/preset_probe_test.cpp` → `Profundum_MorphModProbe` (`[.probe]`); `integration/preset_showcase_test.cpp` → `Profundum_PresetShowcase_<k>of8` (unchanged; the cells come from the four §4.12 `showcaseArmPlan` cases via `morphModTrajectoryCell`); `integration/main_page_audibility_test.cpp` → `Profundum_MainPageAudibility` (two rows) | (a) probe CSV rows exist for each primary's (source, settings) with value ≥ bar; (b) C-15 contrast ≥ 3 dB at E1 and C3 on the §5.5 placed window, `arm.overrides.empty()` for primaries, `arm.fidelityPass`, margin printed; the merged `specs/profundum-phase13-presets-release/evidence/coverage-matrix.csv` holds an `M-ENV`, `M-CYCLE`, `M-VEL` and `M-WHEEL` primary row at E1 and C3 (FR-041); (c) §5.6 rows with preconditions. |
| SC-018 | `unit/preset/preset_library_test.cpp` → `Profundum_CoverageMatrixComplete` (`:1312`, 57 columns), `Profundum_PresetDefsRules` (`:695`, extended) + Phase 13 `[long]` gates | 0 empty columns; one primary per preset; the 53 pre-existing columns still covered; library size printed; each added preset cites its probe rows; 0 single-state claimants storing Source ≠ Off. **FR-042 ranges:** for every factory `Comp`, `decodePresetState(...).morphMod` has `source ∈ [0, 4]`, `cycleShape ∈ [0, 5]`, `cycleSync ∈ {0, 1}` and `cycleSyncRate ∈ [0, 29]`. Every float is `detail::isFinite` and inside its §4.5 law box: amount [−1, 1]; attack and decay [`kMinEnvelopeTimeMs`, `kMaxEnvelopeTimeMs`]; sustain [0, 1]; rate [`kMinFrequency`, `kMaxFrequency`]. **FR-063:** every def whose generated `Comp` decodes `morphMod.source == 0` has a row in `evidence/prephase-claims.csv` with the same primary label and the same sorted secondary labels. A pre-phase row with no def fails (no preset is removed). A def with no row must be one of the §5.7 step 4 additions. **P-9:** the converted set (defs whose primary is in the `MorphMod` group, excluding the step 4 additions) equals the §5.7 selection rule, as ruled into FR-046, applied to `evidence/probe-morphmod.csv`. FR-043 gates green with margins vs the pre-phase run. |
| SC-019 | `Profundum_PresetMonoSum` shards (existing); `profundum_engine_morphmod_test.cpp` → `ProfundumEngine_MorphModAnchorMono` | presets as stored ρ ≥ 0.99, \|Q\| ≤ 1 dB; engine: Cycle A = 1 sweeping a SOLID ↔ WIDE pair, every voice `anchorL == anchorR` bitwise per sample. |
| SC-020 | `unit/manual_coverage_test.cpp` → `Profundum_ManualCoversSurface` | 93 titles; CHANGELOG reviewed; `git diff --quiet plugins/profundum/version.json`. |
| SC-021 | close lane | `node tools/check-portability.js`, `lint-apple-globals.js`, `lint-finite-bitmask.js`, `lint-nonfinite-symbols.js`, `lint-layers.js`, `lint-odr.js`, `lint-plugin-editor-baseline.js`; `./tools/run-clang-tidy.ps1 -Target dsp` and `-Target profundum`; pluginval strictness 5; 0 warnings; every Profundum consumer TU of the two engine headers keeps its verdict incl. Phase 9 SC-032. |
| SC-022 | `integration/copy_compare_test.cpp` → `Profundum_MorphModOutsideEndpoints` | compile time `STATIC_REQUIRE(kSoundRows.size() == 60)` and no 1003–1011 route is `Sound`; nine non-defaults; Copy A→B, B→A, Edit Target A→B→A: controller normalised values and processor atomics bit-unchanged; a recording controller sees no `setParamNormalized` for 1003–1011 from `redisplayEditTarget`. |
| SC-023 | `morph_mod_preset_test.cpp` → `Profundum_MorphModHarnessFidelity`; `profundum_voice_engine_helpers_test.cpp` → `VoiceEngineHelpers_MorphModFields` | (a) each new column's primary: processor vs engine-direct within the shared tolerances on sumL/R, characterL/R; (b) twin vs processor grid `resolved->position` ±1e-6 over the window; (c) the `ppRenderProcessor` twin on the 1003 hi arm, same; (d) default fields vs explicit Off / Amount 1 / wheel 1 bit-identical; (e) processor carriage (§4.12 harness items 1-3). For a probe-context script (Envelope, A 150 ms, D 600 ms, S 0.6, Amount 0.8) and for a preset-context M-WHEEL hi arm (wheel 1), after the sample-0 block and `prepare`, `fx->engine()->morphModulation()` equals the effective script's `e.morphMod`. Enums and the note pair must be equal, and every float bit-equal: it is the same float carried through the stream, not recomputed. `fx->engine()->morphModWheel()` must be bit-equal to `e.morphModWheel`. A script with `morphModBypass = true`, or with a `MorphModulation` or `MorphModBypass` event, returns the named refusal and renders nothing (`ctx.stop` non-empty). |
| SC-024 | `integration/spectrum_frame_test.cpp` → `Profundum_MorphIndicatorFrame`; `unit/controller/ui_views_test.cpp` → `Profundum_UiMorphIndicator`; extended `Profundum_SpectrumFrameDoesNotChangeAudio`, `_SpectrumFrameAllocationFree`, `Profundum_EngineDisplayAccessorNeutral` (`dsp/tests/unit/systems/profundum_engine_display_test.cpp:368`, an appended SECTION, P-8) | (a) layout asserts; block size multiple of 32 and ≥ sizeof; (b) published `morphPosition == engine.morphResolvedPosition()` at production ±1e-6, Off and each source at Amount 1; [0, 1]; (c) audio bit-identical, 0 allocations; (d) `morphResolvedPosition()` render-neutral; (e) the view's `indicatorPosition()` follows frames, its `getValueNormalized()` and the parameter are bit-unchanged, `beginEdit` count 0, falls back before the first frame and after `queueOpened`; (f) `Profundum_EditorLifecycle` cycles, ASan clean. |

Engine tests that only compare within one render and one binary (SC-006, SC-012 (b), SC-019 Anchor) stay in `dsp_systems_tests`
(fast-math): no cross-compilation bit claim is made (root `CLAUDE.md`). No new case goes to `dsp_ieee_tests`.

---

## 7. Build integration

- `dsp/tests/CMakeLists.txt`: a "Profundum Phase 14 (specs/profundum-phase14-morph-modulation) - ENUMERATED" comment block in the
  `dsp_systems_tests` list after `profundum_engine_display_test.cpp` (`:790`), adding `unit/systems/profundum_morph_modulator_test.cpp`
  and `unit/systems/profundum_engine_morphmod_test.cpp`. `VoiceEngine_WorstIntervalMorphMod` goes into the existing
  `profundum_engine_perf_test.cpp` (`[.perf]`). `VoiceEngineHelpers_MorphModFields` is appended to
  `profundum_voice_engine_helpers_test.cpp`, and `Profundum_EngineDisplayAccessorNeutral` gains its SECTION in
  `profundum_engine_display_test.cpp`. All four existing-file `dsp/` edits are inside the amended FR-019 / C-1 list (P-8); no other
  `dsp/` file is touched.
- `plugins/profundum/CMakeLists.txt` (`:44-62`): `src/ui/morph_indicator_slider.h/.cpp`, `src/parameters/morph_mod_params.h`.
- `plugins/profundum/tests/CMakeLists.txt` (`:7-66`): `integration/morph_mod_processor_test.cpp`,
  `integration/morph_mod_preset_test.cpp`, and the second-compilation entry `../src/ui/morph_indicator_slider.cpp`.
- Root `CMakeLists.txt` `profundum_preset_generator` (`:746-801`): no new `.cpp` (the pack is header-only).
- Targets: `dsp_systems_tests`, `profundum_tests`, `Profundum` (plugin), `profundum_preset_generator`. Build:
  `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_systems_tests profundum_tests Profundum profundum_preset_generator -- /nr:false`
  (one batched rebuild; `/nr:false` per the MSBuild node-reuse note).

---

## 8. Lanes, cost and concurrency (C-16; the root `CLAUDE.md` change-set rule)

- **Per change** (gates): `dsp_systems_tests.exe "[morphmod]"`; the Profundum TUs of `dsp_systems_tests` that include
  `profundum_engine.h` (consumers of a modified header — run by tag `[profundum]` excluding `[.perf]`, sharded by TEST_CASE name
  across processes); `profundum_tests.exe "~[long]~[.perf]~[.probe]~[.harvest]"` sharded the same way; the `dsp_ieee_tests`
  Profundum cases (`profundum_engine_test.cpp`, `profundum_voice_test.cpp` include the engine header).
- **Library lanes:** the Phase 13 `[long]` preset shards (showcase 8, sweep / endpoints 8, mono-sum, distinctness, load) run
  concurrently, one process per shard.
- **Shard sizing:** before writing `Profundum_MorphModPresetTransparency_kofN` and `ProfundumEngine_MorphModClickFree_kofN`, one
  shard's wall clock is measured; N = ceil(total / 10 s) so each reads ≤ 60 % of the 15 s per-push bar on the dev box (root
  `CLAUDE.md` wall-clock rule).
- **Probe and M-* showcase cost:** each §5.5 arm renders 8.46 s of audio twice, once on the processor and once as the engine-direct
  twin. The arms are longer than the 1.0 s window an earlier draft assumed, because the D_nl reader's frame is 5.46 s. The
  `[.probe]` case is therefore sharded by preset (`Profundum_MorphModProbe_kofN`) across concurrent processes. N is sized from
  one preset's measured wall clock before the shard count is written. The four new columns' cells ride the existing 8 showcase
  shards; their added wall clock is read from the first run and recorded.
- **CPU lane:** `node tools/run-cpu-tests.js profundum_tests` and `dsp_systems_tests`, alone, after an idle, never right after a
  clang-tidy lane (Phase 12 handoff 13.5).
- **Not re-run:** untouched `[long]` cases. The Phase 7 mono-sum grid rule is not triggered (neither edited header is in
  `mass_dynamics.h`'s include closure, spec C-1).
- Every run's wall clock is recorded in `evidence/run-log.md`; scratch builds and traces are removed with
  `node tools/clean-test-output.js --apply` at close.

---

## 9. Execution order (gated)

| Gate | Work | Proof before moving on |
|---|---|---|
| G0 | Build `profundum_tests` at HEAD; run `Profundum_MorphModPrePhaseHarvest` (the case is added first, with no other change) | `evidence/prephase-fingerprints.csv`, `prephase-decoded.csv`, `prephase-claims.csv` written; P-1 … P-8 written into their rows (P-9 after §11 Q2, before G5) |
| G1 | `profundum_morph_modulator.h` + `profundum_morph_modulator_test.cpp` (tests first, red) | SC-001 (a), SC-002, SC-003 green |
| G2 | Observer fields, engine edits, driver edits, `profundum_engine_morphmod_test.cpp` | SC-001 (b), SC-004 – SC-006 (incl. SC-005 (e)), SC-010, SC-011, SC-012 (b)(c)(c′), SC-013 (h), SC-019 engine, SC-023 (d); every Profundum consumer TU of the engine header green (incl. SC-032) |
| G3 | IDs, routes, pack, processor, controller, state | SC-009, SC-012 (a)(d), SC-013, SC-015, SC-022, SC-001 (c); `profundum_tests` per push green |
| G4 | Frame, producer, `MorphIndicatorSlider`, uidesc XSLT, tooltips | SC-016, SC-024, SC-017 (c) |
| G5 | Harness (§4.12: overlay, `withMorphMod` stream write, wheel carrier, named refusals, `renderPresetSweep` `MorphModOverride`, the four `showcaseArmPlan` cases and `morphModTrajectoryCell`); SC-023 (e) green **before** the probe runs; P-9 in FR-046; `[.probe]` probe → `probe-morphmod.csv` → conversions (§5.7) → generator → library regenerated | SC-007, SC-017 (a)(b), SC-018 (incl. FR-042 ranges, FR-063, P-9), SC-019 presets, SC-023 (a)–(e); Phase 13 `[long]` gates green |
| G6 | Manual, CHANGELOG | SC-020 |
| G7 | Close: lints, clang-tidy, pluginval, CPU lane alone, SC-008 comparison | SC-008 (0 failures), SC-014, SC-021 |

---

## 10. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Off path diverges from today (snap to a stale target) | P-1: forward in `setMorphPosition`, re-hand `b` before every morph snap; SC-006 / SC-007 bitwise in the same binary; SC-008 against the G0 harvest |
| `morphPosition()` reads the modulated target while a source runs | unchanged by design (append-only); new callers use `morphBasePosition()`; `ppRenderProcessor` switched (§4.12) |
| Restore refused because the blob target is modulated | P-2 engine patch; SC-013 (e) pins the offset against a real `BassMorphEngine` |
| Fast-math: `b + A · s` fused into an FMA on macOS | no bit claim on the modulated arm (tolerances 1e-6); the transparent arm is a branch (root `CLAUDE.md`) |
| Fast-math: finiteness | only `detail::isFinite` (modulator, wheel, LFO guard, producer); no `infinity()` / `quiet_NaN()`; `lint-finite-bitmask.js`, `lint-nonfinite-symbols.js` at close |
| Velocity 127 not exactly 1 under `/fp:fast` reciprocal | endpoint by branch (§4.1) |
| Denormals | ADSR decay lands on S exactly (`adsr_envelope.h:341-344`). The Sustain stage does **not** flush: `output_ = S + c_s (output_ − S)` (`:245`) with `c_s` = 0.513 / 0.484 / 0.717 at 48 / 44.1 / 96 kHz (`:72`). A Sustain move to 0 while held decays through the subnormal range, and with `c_s > 0.5` (48 and 96 kHz) it sticks at `denorm_min`. `advanceInterval` flushes the envelope reading with `detail::flushDenormal` (`core/db_utils.h:263`, §4.1), so `s`, the offset and the grid record read `+0`, and SC-002 (f) proves the bound. The envelope's own `output_` stays subnormal (`ADSREnvelope` is not edited): one subnormal multiply per 32 samples, never observed. `SlewLimiter::process` flushes (`smoother.h:556`); `setPosition` maps < 1e-15 to +0 (`bass_morph_engine.h:144`); FTZ / DAZ is otherwise the wrapper's (Phase 9 handoff 10.7) and is not relied on |
| LFO waveform switch mid-render | `setWaveform` crossfades 10 ms once processing started (`lfo.h:189-214`); SC-011 bounds it through the ramp |
| Envelope stage times ±1 interval at boundaries | float32 emulation (§5.2) inside ±1 at all three rates; the C++ test prints the counts |
| Brace-init narrowing (clang) | copy-then-assign for `Config` built from atomics; designated initializers with matching types only |
| `ProfundumGridRecord` aggregate change breaks a construction site | appended members have defaults; the only constructions (`profundum_engine.h:1097`, `profundum_voice_engine_helpers_test.cpp:84-97`) stay valid |
| Hard-coded counts missed | P-4 list (incl. `editor_layout_test.cpp:517`, `:518`, `:535`, `:656` → 89) + re-grep of `84`, `80u`, `288`, `574` under `plugins/profundum/` before G3/G4; the re-grep, not the list, is the authority |
| Processor arm of a probe / showcase / fidelity render runs without the modulation (the stream or the parameter block never carries it) | §4.12 harness items 1-3: the C-10 block is written into the rendered stream bitwise, the wheel rides parameter 6, and bypass / `MorphModulation` are refused by name. SC-023 (e) compares the processor's pushed config with the twin's before the probe runs (G5 order) |
| `dsp/` scope fence trips on the existing test TUs | P-8 amends FR-019 / C-1 to list them before G0 |
| macOS `aligned_alloc` null for the frame block | the rounding expression is kept and asserted (`processor.cpp:352-355`); 320 is a multiple of 32 |
| `_UiClassRule` rejects the new slider class | it accepts any `CSlider` via `dynamic_cast` (`editor_layout_test.cpp:617-619`) |
| Indicator lags in silence | P-5: 4 Hz silence cadence documented; §11 Q1 |
| A source column no existing preset can carry | §5.7 step 4 adds one preset with its probe proof; the build stops and surfaces the measured value if no candidate clears the bar at all (Phase 13 FR-023 rule) |
| Converted preset breaks a Phase 13 gate (loudness, sweep, held load) | conversions are chosen by margin; every FR-043 gate re-runs as stored; a failing conversion is replaced by the next carrier |
| Long lanes serialised | sharding by TEST_CASE name in concurrent processes; CPU lane alone (§8) |

---

## 11. Open questions for the user (before the dependent task)

1. **P-5, before G4:** accept the frame cadence for the indicator (≈ window rate while a note sounds, 4 Hz in silence), or add a
   morph-only frame republish (changes the Phase 12 frame-count contract)?
2. **§5.7 step 4, before G5:** convert exactly **one** existing preset per new column (the best-margin eligible carrier), or convert
   **every** eligible carrier the probe finds? The spec says "wherever the probe shows the source audible" and "the library stays near
   58"; the plan proposes one per column (rule (i), §5.7) to keep the shipped library's sound changes minimal. The ruling goes into
   the FR-046 row first (P-9), and SC-018 asserts the converted set against it.
3. **P-3, before G2:** a successful restore resets the modulator also mid-stream (a preset loaded under a held note restarts the
   envelope and re-phases the Cycle) — confirm the literal C-11 reading.
4. **P-1, before G2:** confirm the FR-010 amendment (store **and** forward).

## Review notes

- The spec cites "FR-064" in C-14 ("the plan derives the list from the probe (FR-064)"); no FR-064 exists in this spec. The plan reads it
  as the Phase 13 probe-before-definitions rule (spec FR-044 here).
- `MorphState`, `MotionConfig`, `BassMorphEngine`, `ProfundumMotion`, `LFO` and `ADSREnvelope` are not edited (spec Non-goals). The
  `dsp/` change set is exactly C-1's list as amended by P-8.
- **Plan challenge, 2026-10-26.** All fourteen issues were applied. Issue 1 (blocker) has two deviations from its suggested
  mechanism; both reach the same end:
  - **1003-1011 are carried through the stream, not as sample-0 parameters.** Emitting them through `processorSampleZeroParams`
    would hand the processor `plainFromNormalized(normalizedFromPlain(x))`. On the log laws (1005, 1006, 1009) that is not
    bit-equal to the twin's `x`, and in preset context it would overwrite the Comp's stored floats. `withMorphMod` writes the
    effective script's block into the rendered stream, so processor and twin run the same floats. SC-023 (e) asserts bit-equality.
    The inverse `kNoteValueDropdownMapping` lookup the issue names is used there, for the stream's Sync Rate index.
  - **`MorphModulation` events are refused, not mapped.** One event would be nine parameter points, and `processorCarrierParam`
    maps one event to one point. Every user of the event is an engine-direct DSP test. Mapping `MorphModBypass` to Compare was
    also rejected: Compare A / B forces the morph position (`processor.cpp:974-976`), so it is not a pure bypass. Both kinds, and
    a script-level `morphModBypass`, get a named refusal. Processor Compare renders go through `renderPresetSweep`'s override.
- **Correction found while defining the §5.5 reader (issue 2).** The earlier §5.5 placed a 1.0 s window in a 3.0 s render. The
  AT-41 / AT-42 D_nl reader that C-15 names reads one 262 144-sample frame (5.461 s at 48 kHz) from a fixed 1.0 s start
  (`bass_morph_test_helpers.h:703-707`, `:771`; `low_frequency_metrics.h:64`), so it could not read that window. The window is
  now the reader's frame (W = 5.461 s), the arms render 8.46 s, and the M-CYCLE probe candidates moved from 0.25 Hz to 0.05 Hz with
  both signs (§5.7), so a held extreme spans W. The bar (3 dB), the descriptor, the stability bound (0.01) and the non-vacuity
  bound (0.05) are unchanged.
- **Issue 12 (denormals), one figure corrected.** The issue gives the 44.1 kHz Sustain coefficient as about 0.50. It is
  `exp(−1000 / 1378.125) = 0.484`, which is below 0.5, so at 44.1 kHz the tail reaches +0 on its own. The stick at `denorm_min`
  happens at 48 kHz (0.513) and 96 kHz (0.717). The flush is applied at every rate regardless, and SC-002 (f) asserts all three.
- **Issue 10 (flood).** The 257 note-ons cycle note numbers inside 0-127. "Notes 1-256" read literally would include numbers
  ≥ 128, which `noteOn` drops by its range check (`profundum_engine.h:224-225`) without counting them in `droppedEvents()`.
