# Feature Specification: Profundum Phase 12 — UI

**Spec slug:** `profundum-phase12-ui`
**Roadmap:** `specs/Profundum-roadmap.md` → Phase 12 (lines 837–1027, including the 2026-10-11 design-conversation amendment and
the reference wireframes); Open Questions 9 and 10 (lines 1109–1114); the Phase 11 → Phase 12 handoff 12.1–12.4 (lines
827–835); the Reuse Inventory row "Plugin infrastructure" (line 172); the Phase 7, 8 and 9 Phase-12 handoffs
(`specs/profundum-phase7-stereo-field/evidence/phase-handoffs.txt:96–107`,
`specs/profundum-phase8-bass-morph/evidence/phase-handoffs.txt:189–226`,
`specs/profundum-phase9-voice-engine/evidence/phase-handoffs.txt:155–210`). Concept reference: `specs/Profundum-concept.md` §3
(lines 144–206), §8 (lines 390–420), §14 (lines 647–678).
**Depends on:** Phase 11 (84 registered parameters, Edit A / Edit B, state v2, copy / compare messages) ✅ COMPLETE 2026-10-22
**Status:** DRAFT — specification only, no implementation. Stage **12a** (design) must be ratified by the user before stage
**12b** (implementation) starts (roadmap lines 839–840).
**Date:** 2026-10-22 · verified against `synth/profundum` at `40f6ebca`

---

## Overview

Profundum has a complete engine and a complete host surface, but its editor is still the Phase 10 placeholder: one 420 × 360
window with eight stock `CSlider` / `COptionMenu` controls (`plugins/profundum/resources/editor.uidesc:1–81`; its banner, line 3,
reads "Phase 12 replaces this file wholesale"). 72 of the 80 visible registered parameters have no control. The Bass Morph has
no A / A·B / B compare and no Copy buttons: Phase 11 shipped those as controller operations with "no UI until Phase 12"
(`plugins/profundum/CLAUDE.md`). Nothing shows the harmonic structure.

Phase 12 designs and builds the instrument's interface. The roadmap's design principle governs (lines 843–849): **the UI follows
the engine**. A **Main** performance page carries a small number of powerful controls that work without DSP knowledge. An
**Advanced** sound-design page carries the mechanisms. Nothing may read as a conventional oscillator / filter / envelope synth.
The UI shows the relationship between a stable, centred low end (**THE ANCHOR**) and a transformable harmonic voice
(**THE MASS**).

The phase is one spec in two stages:

- **12a — UI design.** The roadmap lists seven design deliverables (lines 850–870). They are written as documents in this spec
  folder and ratified by the user. 12a also resolves the main-page questions the roadmap carries into it (lines 872–884) and the
  decisions it defers to it (lines 915–916, 1109–1114). 12a writes no code.
- **12b — implementation.** The ratified design is built with VSTGUI only. That means a replaced `editor.uidesc`, the custom
  views and sub-controllers the design needs, the morph component wired to the Phase 11 messages, a DataExchange producer for
  the spectrum view, the Linux editor-side sync timer, tests and pluginval.

No parameter is added, removed, retyped or re-ranged (roadmap line 916: "No param-type swaps on registered IDs, ever"). The one
state-format change is storage for the endpoint names (OQ-8, ruled 2026-10-23, FR-047). It is appended
after byte 510 under `kCurrentStateVersion = 3`, as the Phase 11 rule requires (`plugins/profundum/CLAUDE.md`, State: "The next format
change appends after byte 510").

---

## Clarifications

### Session 2026-10-23 (user interview on the specify-stage questions)

Each line is the question, the user's decision, and the FR / SC ids that carry every behavioural clause of it. The body of this
spec states each behaviour; this log is not the only carrier.

- **OQ-1 — Main-page knob set.** Decided: the conversation set plus Mass 104 and Drop 400. Depth 200, Body 201, Edge 204,
  Fold 500; macros Weight 100, Dark ↔ Bright 101, Clean ↔ Violent 103; Anchor level 302 / character 300 / behaviour 1000;
  voice mode 1 + glide 412; Motion mode 900; Mass 104; Drop 400. Shift 205 and Tight ↔ Loose 102 move to Advanced.
  [FR-003, FR-011, FR-004, SC-031]
- **OQ-2 / CQ-8 — Spectrum placement and interactivity.** Decided (b): the harmonic spectrum view is display-only in 1.0 and
  appears twice, on the main page (signature visual, Anchor / Mass split drawn in it) and in the Advanced Core spectrum section;
  both instances are fed the same frame (SC-016 counts two). No draggable energy point. [FR-070, SC-016, SC-024, SC-031]
- **OQ-3 / CQ-1 — Spectrum implementation, data source, capacity, cadence.** Decided (a): a new harmonic-bar view
  (`HarmonicBarView`) fed by measured per-partial levels. An append-only dsp accessor exposes the focus voice's post-Character
  samples; an audio-thread Goertzel bank at n·f0 (n = 1..64) over K = 8 f0 periods publishes one frame per completed window
  (about 5 Hz at E1, 33 Hz at C3); frame capacity 64 partials (about 300 bytes); harmonics above 64 are not shown. CPU stays inside
  the unchanged SC-013 bars (8 % / 25 %, P/D 1.05). Disrumpo's `SpectrumDisplay` is not promoted. [FR-025, FR-070, FR-075,
  FR-095, SC-010, SC-013, SC-022, SC-030]
- **CQ-2 — Transport and Linux.** Decided: the same mechanism as Vorago and Membrum, nothing extra: `DataExchangeHandler` in the
  processor, `DataExchangeReceiverHelper` in the controller whose `notify()` decodes the SDK IMessage fallback
  (`plugins/vorago/src/controller/controller.cpp:330–337`). Correction of the spec's premise: on Linux the SDK plugin factory
  injects a run-loop timer whenever the host passes `Linux::IRunLoop` via `setHostContext`
  (`extern/vst3sdk/public.sdk/source/main/pluginfactory.cpp:316–319`), so the fallback works in real Linux hosts; it stays closed
  only in a host with neither the DataExchange API nor a run loop (e.g. a bare test harness), which is documented, not worked
  around. Tests follow the Vorago pattern. The same correction applies to Phase 11's Linux timer note (the pull-on-switch stays).
  [FR-071, FR-076, FR-090, SC-010, SC-011, SC-012, SC-015, SC-023]
- **OQ-8 / CQ-3 — Endpoint name and descriptor.** Decided (a): the user types the endpoint name; it persists in state v3
  appended after byte 510 (`kCurrentStateVersion = 3`; v1 and v2 still load), so presets carry names. The three-word descriptor is
  derived from the endpoint's settings through a documented table and is never stored. [FR-040, FR-047, FR-048, FR-096, SC-004,
  SC-027]
- **CQ-4 — Compare / Edit Target / Morph Position coupling.** Decided (b): pressing compare A or B also switches Edit Target 1001
  to that endpoint (one begin / perform / end on 1001 per press); A·B leaves Edit Target unchanged; touching Morph Position does
  not change compare. [FR-009, FR-041, FR-049, SC-007]
- **CQ-5 — Sub-band bar in `Profundum_MainPageSanity`.** Decided (c): each guarantee at its own stage. Phase 7's generator
  guarantee ρ ≥ 0.9989 on the generator tap through the observer, and Phase 9's full-engine pair ρ ≥ 0.99 with Q_sub ≤ 1.0 dB on
  the processor output, using the Phase 9 SC-015 band definition. [FR-006, FR-024, SC-006]
- **CQ-6 — Macro / Motion modulation on knobs.** Decided (b): macro offsets are drawn as an `ArcKnob` modulation ring
  (`setModulationRange`), computed in the controller from `ProfundumMacroMatrix::kRows` and the current macro values;
  deterministic, no new audio-thread data; Motion is not drawn; base values stay the knob position. [FR-005, FR-061, SC-028]
- **OQ-10 / CQ-7 — Scaling and host resizing.** Decided (b): one designed size with uniform zoom steps 100 / 125 / 150 / 200 %
  via `setZoomFactor`, chosen by a UI-only control (session state, not saved in the plugin state); SC-019 tests each step.
  [FR-009, FR-010, FR-014, SC-019, SC-032]
- **OQ-4 — Image-mode selector and SPREAD mini-spectrum.** Decided: both live in the Advanced Stereo & Output section; the image
  mode is mirrored on the main page only if the OQ-1 set leaves room. [FR-004, FR-080, SC-031]
- **OQ-5 — MODULATION and SETTINGS.** Decided: tabs of the Advanced page. [FR-013, FR-030, SC-031]
- **OQ-6 — Advanced placement of unnamed IDs.** Decided: the spec default. SHAPE 501 and Character 600–602 in a new Shape &
  Character section beside Excitation & Fold; transient gains 700–701 in a Mass section; every other unnamed ID in its band's
  section (202–203 core, 303–306 anchor, 410–411 / 413 / 415–417 note, 2 and 3 global). [FR-004, SC-031]
- **OQ-7 — Anchor labels.** Decided: the main page labels Anchor Morph Mode 1000 MORPH BEHAVIOUR inside the Anchor group; Anchor
  Mode 301 stays on the Advanced page under ANCHOR MODE. [FR-003, FR-012, FR-045, SC-005, SC-031]
- **OQ-9 — MIDI learn.** Decided: host-level MIDI learn only in 1.0. Macros 100–103 and Morph 105 are automatable host
  parameters; `IMidiMapping` stays as Phase 11 shipped it; no `IMidiLearn`. [FR-009, FR-096, FR-098, SC-029]

Still owed by the user (not answered in this session; an agent never fills them): the dated listening verdict on the FR-006
render set (SC-006 (b)), the eight Phase 9 listening verdicts (FR-097), and the ratification of D1–D7 (SC-001).

---


### Session 2026-10-23 (plan-stage rulings)

- P-1: May a hidden render case land before ratification? → Yes: one `[.listen]` test-only case + `render.sh`; no plugin source. [FR-001, SC-006]
- P-2: Reword SC-004 for v3? → Yes: every `getState` length assertion moves to `kStateV3Bytes`; v1 / v2 load expectations unchanged. [SC-004]
- P-3: C3 cadence → about 16.4 Hz (C3 = 130.81 Hz here), not about 33 Hz; no gate changes. [FR-075]
- P-5: Macro ring shape → one-sided (base → base + offset), plugin-local view, no shared-header change. [FR-061]
- P-4: Does Copy copy the name? → No, sound only. [FR-042]
- P-8: Display-only readouts → allowed beside the writing control on the same page. [FR-032]
- P-9: Header preset field in Phase 12 → no; Phase 13 adds the browser. [FR-001 scope]
- Deviation 6: Run the 46 `[long]` dsp cases of the two headers? → No: read-only, behaviour-neutral accessors gate on the `[profundum]` per-push set + the bit-identity test. [FR-095]

### Session 2026-10-24 (close: T001 citations)

- T001 step 5 reported four citation mismatches (M1 to M4) in the tasks.md "Verified API surface" table. All four are line drift only, and every cited symbol and value exists. Main loop: M1 (plugin_ids.h:100-208), M2 (13 TUs + 1) and M4 (CMakeLists.txt:23-64) were corrected on 2026-10-23, and M3 (editor_lifecycle_test.cpp:169-172, :71-91 at 40f6ebca) on 2026-10-24. No requirement changes. [none: documentation]

### Session 2026-10-24 (close: SC-013 CPU)

- The producer's Goertzel bank is at the SSE2 floor (MSVC vectorises it 2-wide), about 3.5 % of MONO; P-ON / D read 1.038 – 1.053
  against 1.05 → move the bank into a plugin-local Highway kernel with runtime dispatch (AVX2 + FMA, NEON, scalar), keeping
  the double recurrence and every bar unchanged (user, 2026-10-24). [FR-075, SC-013]

### Session 2026-10-24 (close: SC-006 (a))

- Comply found six Main IDs with no gated audibility row (D3: 1, 200, 201, 204, 1000, 1001) → add gated processor-path rows
  for 1, 200, 201, 204 and 1000 (`Profundum_MainPageAudibility`, T039). 1001 is exempt as a routing control; its audibility
  is AT-41 / AT-42 (user, 2026-10-24). [SC-006]

### Session 2026-10-24 (close: T035 scope fence)

- T035 fence red on 29 paths outside FR-095's allow-list → the nine test-only helper headers (behaviour-neutral `u`
  suffixes clearing MSVC warnings in profundum_tests), `.gitignore` and the two Phase 9 verdict files join the allow-list. They
  are gated by a zero-warning rebuild of every executable that compiles them, and the full `dsp_processors_tests` suite is not
  run. The 17 regenerated earlier-phase evidence files are restored to HEAD (main loop, applying the standing zero-warning,
  evidence-restore and pure-code-move rules). [FR-095, SC-021]

### Session 2026-10-23 (12a ratification)

- Ratify D1–D7 and the OQ-5 / §3.1 values? → Ratified as written (user, 2026-10-23). [SC-001, FR-002–FR-010]
- Zoom: SETTINGS tab or header? → Both, one session tag 9300. [FR-010, FR-014, SC-019]
- D1 §8.4 render proposals → follow all recommendations: a B endpoint for the morph renders, legato pairs for 412 and 1. [FR-006, SC-006]
- Motion on Main with Amount 0 → selector + tooltip note; no default changes. [FR-050]
- SC-006 (b) listening verdict: "The patch renders all sound pretty damned amazing, very deep and tight low end." [SC-006]
- FR-097: the eight Phase 9 verdicts below; none asks for a change (H-8 prefers the 100 ms time, which is already the default). [FR-097]

### Phase 9 listening verdicts (FR-097)

Source: `specs/profundum-phase9-voice-engine/evidence/phase-handoffs.txt:201–205`; line numbers refer to
`specs/profundum-phase9-voice-engine/evidence/listening-sheet.md`. Only the user replaces a status with
"given (YYYY-MM-DD): verdict"; a verdict that asks for a change is written first as a ruling in its owning FR / SC row.

| Row | `listening-sheet.md` row | Verdict line | Status |
|---|---|---|---|
| H-1 | :21 | :35 | given (2026-10-23): "colour I would keep" |
| H-2 | :37 | :43 | given (2026-10-23): "I do not hear a slow movement in the timbre, it just sounds like a continuous note to me" |
| H-8 | :45 | :57 | given (2026-10-23): "It sounds like a pump to me, it fits the character, the 100ms version is more musical I think" |
| H-10 | :59 | :72 | given (2026-10-23): "The held note sounds the same for both" |
| H-14 | :74 | :87 | given (2026-10-23): "No pumping, all three sound good" |
| H-19 | :89 | :104 | given (2026-10-23): "Anchor brings back the sub to a solid center, no hollow sounds" |
| H-28(1) | :106 | :119 | given (2026-10-23): "No clicks or thumps" |
| H-28(2) | :121 | :136 | given (2026-10-23): "No clicks or thumps" |

### 12a ratification checklist (SC-001)

- D1 (Main page), with its §14 delta table — user: Roland Zwaga (date: 2026-10-23)
- D2 — user: Roland Zwaga (date: 2026-10-23)
- D3 — user: Roland Zwaga (date: 2026-10-23)
- D4 — user: Roland Zwaga (date: 2026-10-23)
- D5 — user: Roland Zwaga (date: 2026-10-23)
- D6 — user: Roland Zwaga (date: 2026-10-23)
- D7 — user: Roland Zwaga (date: 2026-10-23)
- FR-006 render-set listening verdict (SC-006 (b)) — user: Roland Zwaga (date: 2026-10-23): "The patch renders all sound pretty damned amazing, very deep and tight low end."
- FR-097 account (the eight Phase 9 listening verdicts above) — user: Roland Zwaga (date: 2026-10-23)
- D2-ruled strings and values (tooltips, descriptor table, 300 ms decay) — user: Roland Zwaga (date: 2026-10-23)

### Plan questions awaiting a ruling

Verbatim from plan.md §10 Open Questions. Where the "Session 2026-10-23 (plan-stage rulings)" list above already records an
answer on the same topic, the entry names that bullet; the blank is left for the user to confirm.

1. **P-1.** May the hidden FR-006 render TU (test-only, `[.listen]`) land before ratification, as an exception to FR-001?
   Without it, the FR-006 render set cannot exist before ratification. The same ruling covers running Phase 9's
   `evidence/close/listen.sh` before ratification, so the user can give the FR-097 verdicts (§3.4). (See plan-stage bullet
   "P-1".) — ruling: yes (plan-stage bullet P-1)
2. **P-2.** Should SC-004's wording be amended to cover the current-format length assertions (`kStateV2Bytes` →
   `kStateV3Bytes` on `getState` results)? Every v1 and v2 load expectation stays unchanged either way. (See plan-stage
   bullet "P-2".) — ruling: yes (plan-stage bullet P-2)
3. **P-5.** Accept `ArcKnob`'s symmetric ring (it shows the macro's reach on both sides) for 1.0, or add a plugin-local
   one-sided ring? The one-sided ring means re-implementing `draw`: more code, but no shared-header change. (See plan-stage
   bullet "P-5".) — ruling: one-sided plugin-local ring (plan-stage bullet P-5)
4. Should Copy A → B and Copy B → A also copy the endpoint **name**? The plan's default is no: names identify endpoints, and
   the copy is the sound. (See plan-stage bullet "P-4".) — ruling: no, sound only (plan-stage bullet P-4)
5. Ratify the §3.1 proposals, or replace them in D1, D2 and D7:
   - the 960 × 600 designed size;
   - no image-mode mirror on Main;
   - the descriptor table;
   - the 300 ms bar decay;
   - the D7 values;
   - the 100 ms editor sync period;
   - the Main layout with the `morph-anchor` row (§3.1, FR-022) and the §14 delta table verdicts.

   — ruling: ratified 2026-10-23: all §3.1 proposals as written in D1, D2 and D7; zoom in the header and in SETTINGS (FR-010)
6. **P-8.** Rule into the FR-032 row that a display-only `CParamDisplay` bound to a continuous ID is allowed when the same page
   also binds that ID to a conforming writing control (the 105 and 412 readouts)? (See plan-stage bullet "P-8".)
   — ruling: yes (plan-stage bullet P-8)
7. **P-9.** Is a header preset field wanted in 12b? The plan's default is no: no FR or SC requires it, and Phase 13 owns
   presets. A yes needs a new FR / SC row first, and then Appendix A applies. (See plan-stage bullet "P-9".) — ruling: no (plan-stage bullet P-9)
- **P-3.** FR-075 says about 33 Hz at C3; the cadence is 16.35 Hz (plan §5.4); SC-030 counts windows and is unaffected — text
  correction awaiting acknowledgment. (See plan-stage bullet "P-3".) — ruling: acknowledged (plan-stage bullet P-3)
- **tasks.md Deviation 6.** Consumer `[long]` cases of the two modified `dsp/` headers are listed, not run. (See plan-stage
  bullet "Deviation 6".) — ruling: not run (plan-stage bullet Deviation 6)

## Scope

**In scope**

1. **12a deliverables** D1–D7 (FR-003 – FR-010): the main-page wireframe, the Advanced-page wireframe by section, the
   macro-to-parameter mapping table, the Bass Morph interpolation specification as the user sees it, the voice-mode /
   note-handling cross-reference, the UI interaction and automation specification, and the accessibility / scaling /
   host-resizing requirements.
2. Resolution in 12a of every decision the roadmap defers to this phase (Resolved Questions below).
3. **12b**:
   - `editor.uidesc` replaced, with the Main and Advanced pages;
   - the morph component: Edit Target, position readout, A / A·B / B compare, Copy A → B / B → A, endpoint names and
     descriptors, Lock dimming;
   - the Motion section and macro discoverability;
   - the harmonic spectrum view and its DataExchange producer;
   - the image-mode selector with the SPREAD mini-spectrum;
   - the editor-side sync request for Linux;
   - scaling, tests and pluginval.
4. Recording of the Phase 9 listening verdicts the user still owes (Phase 9 handoff 12.3,
   `…phase9…/evidence/phase-handoffs.txt:201–205`) when they arrive. A verdict that asks for a change becomes a ruling in its
   owning FR / SC row first.

**Non-goals, owned elsewhere**

| Owner | What |
|---|---|
| Phase 13 (`profundum-phase13-presets-release`) | Factory presets, preset categories, the preset × capability matrix, the endpoint names / descriptors of factory presets (this phase provides only the field and its editor), release readiness |
| Not adopted (roadmap lines 909–913, 889–890) | User-facing **Oversampling / Quality** settings; a **Low-End Protection [ENABLED]** toggle; a user-selectable global morph "parameter curve" |
| Not planned for 1.0 (roadmap lines 806, 892–894) | A general modulation matrix; Motion targets beyond the registered five (Mass, Depth, Body, Edge, Shift) |
| Phases 1–11 (frozen) | Any DSP behaviour; any parameter's ID, type, range, default, law or display string (Phase 11 SC-005 strings: Harmonic Fold, Wavefold, Hold / Blend / Yield; roadmap line 835) |

---

## Existing components (verified this session)

Paths under `dsp/` are relative to `dsp/include/krate/dsp/`. Every signature below was read at the cited line this session.

| Component | Header : line | What Phase 12 reuses (real signature) |
|---|---|---|
| Profundum parameter IDs | `plugins/profundum/src/plugin_ids.h:100–208` | `enum ParameterIDs : Steinberg::Vst::ParamID`, by band: 0–7 global, of which 4 Pitch Bend, 5 Sustain, 6 Mod Wheel and 7 Channel Pressure are the hidden performance IDs; 100–105 macros / Mass / Morph Position; 200–208 core; 300–306 anchor; 400–403 Drop; 410–417 note; 420–426 amp envelope; 500 `kFoldAmountId`, 501 `kFoldShapeId`; 600–602 character; 700–701 transient gains; 800–809 field; 900–914 Motion; 1000 `kMorphAnchorModeId`, 1001 `kMorphEditTargetId`, 1002 `kMorphRampTimeId`. `kNumRoutedIds = 1003`. **84 registered, 80 visible** |
| Message IDs | `plugin_ids.h:69–73` | `kMsgCopyEndpoint` (ctl → proc, int `"from"`), `kMsgCompare` (ctl → proc, int `"mode"`), `kMsgEndpoint` (proc → ctl, int `"endpoint"`, binary `"blob"` 201 B), `kMsgCompareMode` (proc → ctl, int `"mode"`), `kMsgRequestSync` (ctl → proc, no attributes) |
| Profundum controller | `plugins/profundum/src/controller/controller.h:49–114`; `controller.cpp:184–304, :332–337` | `class Controller : public Steinberg::Vst::EditControllerEx1, public Steinberg::Vst::IMidiMapping` (:49). It is **not** a `VST3EditorDelegate` and **not** an `IDataExchangeReceiver`. Members used here: `void copyEndpoint(Krate::DSP::BassMorphEngine::Endpoint from)` (:81; cpp :210–219 sends `kMsgCopyEndpoint` and calls `setDirty(1u)`); `void setCompare(CompareMode m)` (:83; cpp :222–231 sends `kMsgCompare`); the `setParamNormalized` hook for 1001 (cpp :184–207: sends `kMsgRequestSync`, re-displays the 60 E-parameters, calls `restartComponent(kParamValuesChanged)` once); `notify` (cpp :260–304: `kMsgEndpoint` replaces `mirror_[e]` at :281, `kMsgCompareMode` sets `compare_` at :291–298 and fires no change notification); `std::array<Krate::DSP::MorphState, 2> mirror_{}` (:104); `CompareMode compare_ = CompareMode::AB` (:105). Both are **private**: the only reader of `compare_` is the test accessor `compareModeForTest()` (:92), so a production view needs FR-046. `createView` returns `new VSTGUI::VST3Editor(this, "editor", "editor.uidesc")` (cpp :332–337) |
| `CompareMode`, morph labels | `plugins/profundum/src/parameters/morph_params.h:154, :168–181` | `enum class CompareMode : std::uint8_t { AB = 0, A = 1, B = 2 }` (:154); `kNumAnchorMorphModes = 3`, labels "Hold", "Blend", "Yield" (:168, :180); `kNumEditTargets = 2`, labels "A", "B" (:169, :181) |
| Profundum processor sync | `plugins/profundum/src/processor/processor.h:51, :54, :86–88, :129, :201`; `processor.cpp:182–186` | `class Processor : public Steinberg::Vst::AudioEffect, public Steinberg::ITimerCallback`; `kSyncTimerIntervalMs = 50`; `Steinberg::IPtr<Steinberg::Timer> syncTimer_` ("nullptr on Linux (timer.cpp:346-351)"); `hasSyncTimerForTest()`. **No `connect` / `disconnect` override and no DataExchange member** (grep `DataExchange` in `processor.h` and `processor.cpp`: 0 matches) |
| Placeholder editor | `plugins/profundum/resources/editor.uidesc:1–81` | 420 × 360, fixed (`minSize` = `maxSize`, :32–33), eight `control-tag`s for 0–3 and 100–103 (:19–28). **Replaced wholesale** |
| Editor lifecycle test | `plugins/profundum/tests/unit/controller/editor_lifecycle_test.cpp:95, :151, :163–203` | `TEST_CASE("Profundum_EditorLifecycle", "[profundum][controller][ui][lifecycle]")` with section `HarnessCycles`. Section `EditorBindsSurface` asserts `controlCount == 8u` and tags `{0, 1, 2, 3, 100, 101, 102, 103}` (:197–199); **this phase supersedes it** |
| Plugin entry | `plugins/profundum/src/entry.cpp:12` | Banner "FR-018: no ui/*.h include in Phase 10 (stock VSTGUI views only)". Rewritten when the shared view creators are included |
| `MorphState` field table | `systems/morph_state.h:47, :76–78, :142–317`; `systems/bass_morph_engine.h:74` | `enum class Kind : std::uint8_t { F32, Enum8, Bool }` (:47). 62 rows carry `law`, `axis`, `lo`, `hi`, re-exported as `static constexpr const auto& kLawTable = detail::morph_state_io::kFieldTable;` (:74). The Enum8 / Bool leaves, registered as `StringListParameter`s, are `characterAlgorithm`, `field.mode`, `field.motionSource`, `field.cycleSync`, `field.cycleNote` + `cycleModifier` (one 30-entry dropdown), `motion.source`, `motion.target`, `motion.cycleShape`, `motion.cycleSync`, `motion.cycleNote` + modifier, `motion.cycleRetrigger`, `motion.polarity`, `anchor.waveform` and `anchor.mode` (:185–300). Amp curves are linear over `[-1, +1]` (:212–217). Amp times are **Logarithmic** over `[kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs]` (:156–157, :204–211). **This table is the source of D4** |
| `BassMorphEngine` | `systems/bass_morph_engine.h:48–49, :70–72, :188` | `enum class Endpoint : std::uint8_t { A, B }`; `enum class AnchorMorphMode : std::uint8_t { Lock, Blend, Punch }`; `kThetaUp = 0.52f`, `kThetaDown = 0.48f`, `kPunchExponent = 3`; `[[nodiscard]] static float curve(AnchorMorphMode m, float p) noexcept` (Lock 0, Blend p, Punch p³; Phase 8 handoff 12.2) |
| `ProfundumMacroMatrix` | `systems/profundum_macro_matrix.h:38–47, :75–92, :193, :204` | `enum class ProfundumMacro : std::uint8_t { Weight = 0, DarkBright, TightLoose, CleanViolent, Count }`; `enum class ProfundumMacroTarget` (16 targets, :41–47); `static constexpr std::array<ProfundumMacroRow, kNumTargets> kRows` (:75–92, harvested 2026-10-21). **Row `DarkBright → Shape` has `.amount = 0.0f`** (:81; "dropped, ruling B10-2"). Row `TightLoose → CompressionAmount` (:86) has **no registered parameter**: it is handed to the engine post-law (C-7). `contributions() const noexcept` (:193), `apply(MorphState&) const noexcept` (:204). **This table is the source of D3** |
| Anchor / field / character / Motion enums | `systems/anchor_oscillator.h:57–58`; `systems/bass_stereo_field.h:64–65`; `systems/bass_character_stage.h:78`; `systems/morph_state.h:82–84` | `Waveform { Sine, Triangle, RoundedSine, AsymmetricSine, OctaveDownSine }`, `Mode { Lock, Follow, Punch, Ghost }`; `ImageMode { Solid, Spread, Wide }`, `MotionSource { Drift, Cycle }`; `Algorithm { Tube, Fold, Rectify, Bite, Crush, Fracture }`; `Source { Off, Env, Cycle, Drift }`, `Target { Mass, Depth, Body, Edge, Shift }`, `Polarity { Bipolar, Unipolar }` |
| `BassStereoField` width law | `systems/bass_stereo_field.h:78, :86, :232–256, :293, :397–403, :486–488, :496, :632–644, :647–657` | `static constexpr float kMotionExcursion = 0.5f` (:78); `static constexpr float kSideFloorGain = 0.035f`; `void setControls(const Controls& c) noexcept`, which only latches into `pending_` until the next `advanceControl()` or `reset()` (:232–256); `void advanceControl() noexcept`; `[[nodiscard]] float widthCurve(float frequencyHz) const noexcept` (:398), whose doc comment reads "c(f) of the current interval (plan S1 C-5; tests, **Phase 12**)". It returns `lawCurve` (:632–644), a function of the smoothed crossover, Gravity (`uLow_`, :649–650) and curve (`gamma_`, :653) only, and exactly 1 above the upper knee. Mode, Spread and Motion depth enter only the separate `magnitudeScale_` (:655–657: 0 in SOLID, else `spread · (1 − κμ + κμ·m)`), which no public observer exposes; `motionValue()` (:496) exposes m. `static constexpr float sideGainForGravity(float gravity) noexcept`. **Candidate source of the SPREAD mini-spectrum (FR-080)** |
| `ProfundumCore` spectrum | `systems/profundum_core.h:97, :315–319, :325`; `systems/profundum_voice.h:700, :973`; `systems/profundum_engine.h:424, :1231` | `kDefaultPartials = 64`; `std::span<const float> shapeGains() const noexcept`; `std::span<const float> deliveredGains() const noexcept` (:318, size `numPartials`); `int numPartials() const noexcept`. **`ProfundumEngine`'s public API cannot reach these.** Its public const accessors (`profundum_engine.h:345–422`) expose endpoints, controls, macros, seed and diagnostics, but no voice, no core and no resolved state (`state_ = morph_.resolved().state` is private, :640). The engine's `std::array<ProfundumVoice, kMaxVoices> voices_{}` is private (:1231; `private:` at :424), and each voice's `ProfundumCore core_` is private too (`profundum_voice.h:973`; `private:` at :700) |
| `ProfundumEngineObserver` | `systems/profundum_engine_observer.h:55, :61–77`; `profundum_engine.h:211` | Test seam. `ProfundumVoiceTaps` carries `coreL/R`, `charL/R`, `anchorL/R`, …; `ProfundumEngineTaps` carries `sumL/R` … `outL/R`. `void setObserver(ProfundumEngineObserver* o) noexcept` is configuration-time, and the engine keeps state copies while an observer is attached (:55: "a copy kept by the engine only while an observer is attached"). **Not a production data path** |
| `ArcKnob` | `plugins/shared/src/ui/arc_knob.h:49, :53–55, :555–560, :715–716` | `class ArcKnob : public VSTGUI::CKnobBase`; ctor `(const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag)`; `void setModulationRange(float range) noexcept`; creator view name `"ArcKnob"`; `inline ArcKnobCreator gArcKnobCreator;` with the note "Include this header from each plugin's entry.cpp to register the view type" |
| `BipolarSlider` | `plugins/shared/src/ui/bipolar_slider.h:37, :50–56, :245` | `class BipolarSlider : public VSTGUI::CControl`; the ctor sets value 0.5 ("center = 0 bipolar"); view name `"BipolarSlider"`. A candidate for the three bipolar macros (Dark ↔ Bright, Tight ↔ Loose, Clean ↔ Violent; neutral 0.5) and for Shift (205, `[-1, 1]`) |
| `ADSRDisplay` | `plugins/shared/src/ui/adsr_display.h:57, :152, :310–314, :330–334, :355, :1413–1417` | `void setAdsrBaseParamId(uint32_t baseId)` binds A, D, S, R to base … base+3; Profundum's 420–423 are contiguous. `void setCurveBaseParamId(uint32_t baseId)` binds three curves; 424–426 are contiguous. Curves are `[-1, +1]`, normalised as `(c + 1) / 2`. `void setParameterCallback(ParameterCallback cb)`. **Hazard:** its time normalisation is cubic, `static float timeMsToNormalized(float ms, float maxMs) { return std::cbrt(clamped / maxMs); }` (:1413–1417), while Profundum registers the amp times with the **logarithmic** law (`morph_state.h:204–211`). Wiring it directly would write wrong values (FR-034) |
| `PitchEnvelopeDisplay` | `plugins/shared/src/ui/pitch_envelope_display.h:57, :108, :200–206` | Param-ID setters for Start / End / Time / Curve (+ knee): Membrum's model. Profundum's Drop is Amount / Time / Shape / Rebound (400–403). Whether to reuse it is a 12a judgement (FR-004), not an assumption |
| `ToggleButton`, `ActionButton`, `IconSegmentButton`, `FieldsetContainer`, `OutlineBrowserButton` | `toggle_button.h:57`; `action_button.h:49`; `icon_segment_button.h:48`; `fieldset_container.h:43`; `outline_button.h:79` | Shared VSTGUI controls with registered creators (`toggle_button.h:591`, `action_button.h:405`, `icon_segment_button.h:959`, `fieldset_container.h:335`). `OutlineBrowserButton : public VSTGUI::CControl` is the Vorago / Seraphis precedent for the header preset button |
| `PresetManager` / `PresetBrowserView` | `plugins/shared/src/preset/preset_manager.h:35, :126, :129`; `plugins/shared/src/ui/preset_browser_view.h:53, :58` | `setStateProvider`, `setLoadProvider`. The Profundum controller already owns `presetManager_` (`controller.cpp:69–70`). Serves the header preset field of both wireframes (roadmap lines 927, 955) |
| DataExchange producer pattern | `plugins/membrum/src/processor/processor.cpp:1137–1172`; `plugins/membrum/src/processor/meters_block.h:20–38` | `connect` builds a `DataExchangeHandler::Config` with `numBlocks = 4`, a `userContextID`, and `blockSize` rounded **up** to the 32-byte alignment (the macOS `aligned_alloc` fix, :1143–1152), then calls `onConnect(other, getHostContext())`. `disconnect` calls `onDisconnect` and `reset()`. The payload is a POD with `static_assert(sizeof)`. Existing user-context IDs: `0x4D425452` (Membrum), `0x53434C44` (Seraphis, `cloud_frame.h:46`), `0x5645434F` (Vorago, `ecosystem_frame.h:25`) |
| DataExchange consumer pattern | `plugins/membrum/src/controller/controller.h:44, :130–141`; `controller.cpp:1743` | `public Steinberg::Vst::IDataExchangeReceiver`; `queueOpened(…, TBool& dispatchOnBackgroundThread)`, `queueClosed`, `onDataExchangeBlocksReceived(…)`; `notify` pass-through for the IMessage fallback |
| Page switching pattern | `plugins/vorago/src/ui/panel_sub_controller.h:53`; `plugins/vorago/src/controller/controller.h:95–99`; Vorago spec C-5 (`specs/vorago-phase13-ui/spec.md:456–463`) | `class VoragoPanelSubController : public VSTGUI::DelegationController`. Pages are sibling `CViewContainer`s toggled with `setVisible`, never a `UIViewSwitchContainer` (which builds only the active template) |
| Disrumpo spectrum views | `plugins/disrumpo/src/controller/views/spectrum_display.h:5–21, :50–52`; `spectrum_analyzer.h:45` | `class SpectrumDisplay : public VSTGUI::CView` draws "frequency band regions with draggable crossover dividers, real-time FFT spectrum curves" on a 20 Hz – 20 kHz log axis, fed from `Krate::DSP::SpectrumFIFO`. `class SpectrumAnalyzer` is a UI-thread windowed FFT. Both are plugin-local, with band-editor semantics and audio-FFT data. Input to OQ-3 |
| Resizing API | `extern/vst3sdk/vstgui4/vstgui/plugin-bindings/vst3editor.h:106, :123, :127` | `bool setEditorSizeConstrains(const CPoint& newMinimumSize, const CPoint& newMaximumSize)`; `void setZoomFactor(double factor)`. Precedent: Disrumpo constrains its editor to 834 × 500 … 1400 × 840 (`plugins/disrumpo/src/controller/controller.cpp:737–739`) |
| MIDI learn interface | `extern/vst3sdk/pluginterfaces/vst/ivstmidilearn.h:47, :90` | `class IMidiLearn : public FUnknown`, `onLiveMIDIControllerInput(int32 busIndex, int16 channel, CtrlNumber midiCC)`. **No plugin in the repo implements it** (grep `IMidiLearn` in `plugins/*/src`: 0 matches). Input to OQ-9 |
| Test helpers | `tests/test_helpers/uidesc_reachability.h:43, :88`; `tests/test_helpers/editor_lifecycle_harness.h:102`; `tests/test_helpers/allocation_detector.h` | `extractControlTagMap(const std::string& xml)`; `unreachableParams(const std::string& uidescXml, const std::vector<int>& registeredIds, …allowlist)`; `exerciseEditorLifecycle(EditController&, const char* templateName, const std::string& uidescAbsolutePath, int cycles = 3)`, which never attaches the frame (Vorago spec SC-016 analysis); `AllocationScope` |
| CPU gates | `plugins/profundum/tests/integration/processor_cpu_test.cpp:68–72, :214, :573` | `Profundum_ProcessorCpu` and `Profundum_ProcessorCpuFullSurface` (`[.perf]`); `kMonoPctBar = 8.0`, `kPolyPctBar = 25.0`, `kWrapperOverheadCeiling = 1.05`, `kControlRatioCeiling = 1.05` |

**Not reused, with the reason**

- `XYMorphPad` (`plugins/shared/src/ui/xy_morph_pad.h:52`): a two-axis control, while Bass Morph is one position (105). The
  roadmap's infrastructure row (line 172) lists it as available, not as required.
- `MacroRingKnob` (`plugins/seraphis/src/ui/macro_ring_knob.h:55`): Seraphis-local. The shared `ArcKnob` / `BipolarSlider`
  cover the macros.
- `ProfundumEngineObserver` as a spectrum source: it is a configuration-time test seam that makes the engine keep state copies.

---

## New components

### ODR sweep — run this session

Command: `grep -rnE "\b<Name>\b" dsp/ plugins/ tools/ tests/`. This is identifier-wide, so stricter than `class <Name>`.

| Candidate name | Result | Disposition |
|---|---|---|
| `HarmonicSpectrumView`, `HarmonicBarView`, `HarmonicView` | **0 matches each** | `HarmonicBarView` is claimed (OQ-3 ruling): plugin-local, namespace `Profundum::UI`, `plugins/profundum/src/ui/` |
| `HarmonicFrame` | **283 matches**, including `struct HarmonicFrame` at `dsp/include/krate/dsp/processors/harmonic_types.h:54` | **FORBIDDEN**. Use `ProfundumSpectrumFrame` |
| `ProfundumSpectrumFrame`, `ProfundumHarmonicFrame`, `SpectrumFrame`, `ProfundumFrame`, `MassFrame` | **0 matches each** | `ProfundumSpectrumFrame` proposed for the DataExchange payload (`plugins/profundum/src/processor/spectrum_frame.h`) |
| `kProfundumFrameUserContextId`, `kHarmonicFrameUserContextId` | **0 matches each** | `kProfundumFrameUserContextId` proposed. Its value must differ from `0x4D425452`, `0x53434C44` and `0x5645434F` (proposed `0x50524644`, 'PRFD') |
| `publishHarmonicFrame` | **0 matches** | Proposed name is `publishSpectrumFrame` (re-swept at plan) |
| `ProfundumPanelSubController`, `ProfundumPageSubController`, `PageSubController`, `ProfundumSubController` | **0 matches each** | `ProfundumPanelSubController` proposed (a `VSTGUI::DelegationController` for page switching) |
| `MorphBarView`, `BassMorphView`, `MorphSliderView`, `MorphControlView`, `MorphPositionView` | **0 matches each** | One is claimed only if 12a's morph component cannot be composed from stock / shared controls |
| `SpreadCurveView`, `SpreadMiniSpectrumView`, `StereoWidthView` | **0 matches each** | `SpreadCurveView` proposed for the SPREAD mini-spectrum |
| `MacroHighlighter`, `MacroTargetMap`, `kMacroTargetParamIds` | **0 matches each** | `kMacroTargetParamIds` proposed (constexpr table, FR-060) |
| `EndpointLabels`, `EndpointInfo`, `MorphEndpointLabel`, `EndpointNameBlock`, `kMsgEndpointNames` | **0 matches each** | Reserved for OQ-8 |
| `ProfundumEditor`, `ProfundumUiState`, `AnchorMassSplitView`, `EnergyBarView` | **0 matches each** | Swept, not claimed |
| `SpectrumFIFO` | **51 matches** (`dsp/include/krate/dsp/primitives/spectrum_fifo.h:41`, Disrumpo) | Existing. Not reused (OQ-3 option (c) not adopted) |
| `MacroRingKnob` | **47 matches** (Seraphis) | Not claimed |

None of the roadmap's near-name hazards (`CharacterProcessor`, `SubOscillator`, `StereoField`, `MorphEngine`, `MorphNode`,
`TransientDetector`, `HarmonicCloud`, `SpectralState`, `MacroMapper`; roadmap lines 174–176) is a candidate here. The plan
re-sweeps every name it finally claims.

### New and changed components (12b)

| Component | Layer / file | Nature |
|---|---|---|
| `editor.uidesc` | plugin `resources/` (replaced) | Main and Advanced pages per the ratified D1 / D2. Edits after the first write use XSLT (repo practice for changes touching more than 3 controls) |
| `HarmonicBarView` | plugin `src/ui/` (OQ-3 ruling; not shared) | `VSTGUI::CView` drawing the measured harmonic bars, display-only, two instances (FR-070) |
| `SpreadCurveView` | plugin `src/ui/` | Draws c(f) over 20 Hz – 10 kHz (FR-080) |
| `ProfundumPanelSubController` | plugin `src/ui/` | Page switching, morph-component buttons, macro highlight, Lock dimming |
| `ProfundumSpectrumFrame`, `kProfundumFrameUserContextId` | plugin `src/processor/spectrum_frame.h` | POD payload with `static_assert(sizeof)`; block size rounded up to the alignment |
| Processor | plugin `src/processor/processor.{h,cpp}` (extended) | `DataExchangeHandler` member, `connect` / `disconnect`, activate / deactivate, `publishSpectrumFrame()`, the Goertzel bank of FR-075 |
| Controller | plugin `src/controller/controller.{h,cpp}` (extended) | `VST3EditorDelegate` (`createCustomView`, `createSubController`, `willClose`), `IDataExchangeReceiver` + `DataExchangeReceiverHelper` (FR-076), cached frame, endpoint-name editing (FR-047), descriptor table (FR-048), macro modulation rings (FR-061), zoom control (FR-014), editor sync timer (FR-090), public compare-mode getter and change notification (FR-046) |
| `entry.cpp` | plugin (extended) | Shared view-creator includes; the FR-018 banner rewritten |
| Engine read accessor | `dsp/systems/profundum_engine.h` **and** `dsp/systems/profundum_voice.h` (the core is private in the voice); `dsp/systems/profundum_core.h` only if OQ-3 option (b) needs a read the core does not expose. **Required: OQ-3 option (b) is ruled (FR-075)** | Append-only `const noexcept` accessor(s), no behaviour change, gated by the consumer rule (FR-095) |
| `BassStereoField` magnitude observer | `dsp/systems/bass_stereo_field.h`, **only if the plan picks it** over controller-side derivation (FR-080) | Append-only `const noexcept` read of the width magnitude m, no behaviour change, gated by the consumer rule (FR-095) |

---

## Functional Requirements

### A. Stage gate (roadmap lines 839–840)

- **FR-001** — 12a and 12b run in sequence. Until the user has ratified D1–D7 and every question under Resolved Questions has a ruling
  recorded in Clarifications and in its owning FR / SC row, Phase 12 changes no file outside this spec folder: nothing under
  `plugins/profundum/`, `dsp/` or `plugins/shared/`. **Exception (user ruling 2026-10-23, P-1):** before ratification, one
  hidden test-only case `plugins/profundum/tests/integration/main_page_render_set_test.cpp` (tag `[.listen]`, never in a
  gate), its CMake registration and `render.sh` in this spec folder may land, to render the FR-006 main-page sound set for the
  user's listening. Running Phase 9's `listen.sh` is allowed too. No plugin source changes before ratification.
- **FR-002** — The 12a deliverables are files in `specs/profundum-phase12-ui/design/`:

  | Deliverable | File |
  |---|---|
  | D1 | `main-page.md` |
  | D2 | `advanced-page.md` |
  | D3 | `macro-mapping.md` |
  | D4 | `morph-laws.md` |
  | D5 | `voice-note.md` |
  | D6 | `interaction-automation.md` |
  | D7 | `accessibility-scaling.md` |

  Wireframes are text / ASCII or SVG, kept in the repo. Each wireframe labels every control with its parameter ID, or with
  "UI-only".

### B. 12a deliverables (roadmap lines 850–870)

- **FR-003 (D1, main page)** — The working basis is Concept A (functional, macro-first, Bass Morph as the visual centrepiece),
  using Concept B's terminology and visual split into two regions: **THE ANCHOR** (weight) and **THE MASS** (character) (lines
  851–853). The page covers its three jobs (lines 853–855):
  - *shape the mass*: Depth 200, Body 201, Edge 204, Fold 500, Mass 104, Drop 400;
  - *macros*: Weight 100, Dark ↔ Bright 101, Clean ↔ Violent 103;
  - *control the weight*: Anchor level 302, Anchor character 300 (waveform), and Anchor behaviour **1000** (the morph
    behaviour Hold / Blend / Yield). The roadmap identifies the conversation's main-page "ANCHOR MODE LOCK / BLEND / PUNCH" as
    the morph behaviour (lines 899–900), not as the Anchor's own mode 301;
  - *transform the sound*: Bass Morph 105 with its component, voice mode 1 + glide 412, Motion mode 900.

  The exact main-page control set is FR-011, the spectrum placement FR-070, the image-mode placement FR-080, and the two Anchor
  section labels with the placement of 301 are FR-012.
  - **Delta against concept §14.** The concept §14 screen remains the reference the amendment is measured against (roadmap
    line 841). D1 contains a delta table with one row per §14 element (`specs/Profundum-concept.md:647–678`): the BASS title,
    the spectrum centrepiece, each of the eight knobs Weight · Body · Edge · Mass · Anchor · Shift · Fold · Drop, and the
    CLEAN ─ MORPH ─ VIOLENT bar. Each row says kept / moved to Advanced / replaced / dropped, and why. D1 is ratified
    together with this table (SC-001).
- **FR-004 (D2, Advanced page)** — The roadmap's sections (lines 856–861):

  | Section | Controls (IDs) |
  |---|---|
  | **Core spectrum** | Depth 200, Body 201, curvature / emphasis 202–203, Edge 204, Spectral Shift 205, Fold 500, Tight ↔ Loose 102, the second `HarmonicBarView` instance (FR-070) |
  | **Sub Anchor** | level 302, character / waveform 300, mode 301 (section label ANCHOR MODE, FR-012), punch / ghost / fold-feed 303–306, morph behaviour 1000 |
  | **Excitation & Fold** | edge drive 207, excitation mix 206, asymmetry 208, fold amount 500 |
  | **Shape & Character** | SHAPE 501, Character 600–602 (a new section beside Excitation & Fold) |
  | **Mass** | transient gains 700–701 |
  | **Pitch & Envelopes** | Drop 400–403, amp envelope 420–426, note behaviour 410–417 |
  | **Stereo & Output** | image mode 800 and the SPREAD mini-spectrum (FR-080), Spread 801, Gravity 802 / crossover 803, curve 804, field Motion 805–809, and the global band: output 0, polyphony 2, output ceiling 3 |
  | **Morph behaviour** | 1000, 1002, compare, copy |
  | **Motion** detail | 900–914 |

  Every registered ID the roadmap list does not name is placed by the rows above (OQ-6 ruling): 202–203 in Core spectrum,
  303–306 in Sub Anchor, 410–411 / 413 / 415–417 in Pitch & Envelopes, 2 and 3 in the global band of Stereo & Output, 501 and
  600–602 in Shape & Character, 700–701 in Mass. Mass 104 is on the Main page (FR-011). MODULATION and SETTINGS are tabs of this
  page (FR-013). SC-031 checks the placement.
- **FR-005 (D3, macro mapping)** — D3 reproduces `ProfundumMacroMatrix::kRows` (`profundum_macro_matrix.h:75–92`) row by row as
  the UI contract: macro, target, amount, curve and unit (the Phase 9 handoff 12.1 table). It maps each target to the
  registered ID it offsets. It names the rows with no control (`CompressionAmount`) and the inert rows (`DarkBright → Shape`,
  amount 0). The table is generated from `kRows`, not retyped. D3 also states that macro offsets act on the resolved state and
  **do not move the knobs** of their targets (`apply(MorphState&)`, :204).
- **FR-006 (D3, main page sufficiency)** — The main page must make a good sound with its visible controls alone (line 863).
  Proving each main-page lever audible is necessary but not sufficient, so FR-006 has three parts (SC-006):
  - D3 lists every main-page control with the Phase 9 audibility-table row that proves it audible
    (`specs/profundum-phase9-voice-engine/evidence/audibility-table.csv`);
  - 12a renders a main-page set from the default patch: every Advanced-only ID at its registered default, and only D1's
    main-page controls varied (D1 lists each render's settings). The user records a dated listening verdict on the set in
    Clarifications before ratification. An agent never fills the verdict. **Ruled 2026-10-23 (D1 §8.4, "follow all
    recommendations"):** the Bass Morph 105 / Morph Behaviour 1000 / Edit Target 1001 renders and *Morph and motion* first write
    a B endpoint through 1001 = B (the *Growl* values Edge 70 %, Fold 60, Mass 70), then 1001 = A and 105 = 50 %; Glide 412 at min /
    max and Voice Mode 1 at both values add one legato-pair render each (E1 held 1.5 s, then C3 overlapping; 3 s + 1 s tail);
  - the same renders pass an automated sanity gate at E1 and C3 (SC-006 (c)).
- **FR-007 (D4, morph as the user sees it)** — D4 restates Phase 8's per-parameter laws (`kLawTable`, 62 rows; Phase 8 handoff
  12.1 summary by axis) as what the user sees while morphing:
  - the four laws: linear, logarithmic, equal-power, stepped;
  - the 0.52 / 0.48 stepped hysteresis (`bass_morph_engine.h:70–71`);
  - the Anchor curves: Lock c = 0, Blend c = p, Punch c = p³ (`:72`, `:188`);
  - the position ramp, set by Morph Ramp Time 1002 (Morph smoothing). Phase 11 registered it, so the roadmap's condition "if
    Phase 11 registers it" (line 889) is met.

  There is no user-selectable global curve (lines 889–890).
- **FR-008 (D5, voice / note)** — D5 is a cross-reference to Phases 4 and 9 and adds no behaviour (lines 866–867). It covers
  deterministic, documented stealing and retrigger, the POLY steal order (oldest released, then oldest active) and the Note
  Memory modes.
- **FR-009 (D6, interaction and automation)** — D6 lists every UI control as either a host parameter (with its ID) or UI-only
  state, and specifies:
  - A / A·B / B compare and Copy are UI-only messages (Phase 11 handoff 12.2), and the compare never writes state; a compare A or B
    press additionally drives Edit Target 1001 through its parameter gesture (FR-049);
  - Edit Target 1001 is a registered, non-automatable parameter (Phase 11 Q6);
  - the automation and MIDI-learn behaviour of the four macros (100–103) and Morph Position (105) (lines 868–869): host-level
    MIDI learn only, FR-098;
  - the page selection, the Advanced tab selection, the zoom step (FR-014) and the highlight states are session UI state and are
    never persisted.
- **FR-010 (D7)** — D7 sets the accessibility, scaling and host-resizing requirements (line 870; FR-014):
  - window size and resize policy: one designed size with uniform zoom steps (FR-014), no free resize; the zoom control appears
    both in the header and in the SETTINGS tab, one UI-only session tag 9300 shared by both (user ruling 2026-10-23);
  - minimum text size and minimum hit-target size;
  - colour contrast of text against its background;
  - every control carries a textual name (tooltip or label).

  The values are ruled in 12a and gated by SC-019.

### C. Interaction principles (binding on 12b; roadmap lines 901–907)

- **FR-011 (main-page control set)** — The Main page binds this set of registered IDs, and no other (OQ-1 ruling), except the
  optional image-mode mirror of FR-080:

  | Group | IDs |
  |---|---|
  | Shape the mass | Depth 200, Body 201, Edge 204, Fold 500, Mass 104, Drop 400 |
  | Macros | Weight 100, Dark ↔ Bright 101, Clean ↔ Violent 103 |
  | Anchor | level 302, character 300, behaviour 1000 (FR-012) |
  | Transform | voice mode 1, glide 412, Motion mode 900, Bass Morph 105 with Edit Target 1001 (the morph component, FR-040) |

  Spectral Shift 205 and Tight ↔ Loose 102 are Advanced-only. Anchor Mode 301 is Advanced-only (FR-012). SC-031 checks the set.
- **FR-012 (Anchor labels on the Main page)** — The Main page labels Anchor Morph Mode 1000 **MORPH BEHAVIOUR** inside the Anchor
  group. Anchor Mode 301 is not on the Main page; it stays on the Advanced page under the label **ANCHOR MODE** (OQ-7 ruling).
  The two controls keep disjoint label sets (FR-045).
- **FR-013 (Advanced tabs)** — MODULATION and SETTINGS are tabs of the Advanced page, not separate pages (OQ-5 ruling). The
  tab strip lives inside the Advanced page container.
- **FR-014 (scaling and zoom)** — The editor is built at one designed size and offers uniform zoom steps 100 / 125 / 150 / 200 %
  through `VSTGUI::VST3Editor::setZoomFactor` (`vst3editor.h:127`), chosen by a UI-only control (OQ-10 ruling). The zoom is
  session state: it is not a registered parameter, not in the plugin state, and bound to no parameter ID. The editor offers no
  free resize. SC-019 tests every step; SC-032 tests the control.

- **FR-020** — Main-page controls give musical results without DSP knowledge. No main-page label names a DSP mechanism (such as
  "oscillator", "filter", "LFO" or "partial") unless D1 rules otherwise.
- **FR-021** — Macros drive several parameters only through the D3 mapping (the engine's `kRows`). The UI adds no mapping of
  its own.
- **FR-022** — Wherever the morph component appears, the Anchor's morph behaviour (1000) is visible beside it. On the Main page
  that is the MORPH BEHAVIOUR control inside the Anchor group (FR-012).
- **FR-023** — Per-voice envelopes and note state stay independent in POLY, and voice stealing and retrigger behave as D5
  documents. The UI adds no voice behaviour.
- **FR-024** — **No Advanced control may silently invalidate the stable, centred low end.** The controls that affect mono
  compatibility are image mode 800, Spread 801, Gravity 802, crossover 803, curve 804, field Motion 805–809, and Character
  600–602 in WIDE. Each one says so in its tooltip / label and states the guarantee Phase 7 measures (Phase 7 handoff §4):
  - the generation vector is pan-only and constant-power, and the generator's sub band sits at ρ ≥ 0.9989;
  - Gravity 0 still attenuates side content below 2·f_c by ×0.035 (−29.1 dB);
  - WIDE's sub-band correlation depends on the harvested offset pair.

  There is no "low-end protection" switch (lines 905–906, 911–913).
- **FR-025** — The spectrum display represents the actual signal and is never decoration (lines 880–881, 907). Every drawn value
  comes from the FR-071 frame, or from a deterministic function of the current parameters that D2 names. The spectrum view's
  source includes the harmonics the Fold (500–501) and Character (600–602) stages generate. OQ-3 option (a) (the pre-nonlinear `deliveredGains()`) is **not adopted**: the user ruled option (b) on 2026-10-23 (FR-075), so
  this FR carries no deviation and the fold fixture of SC-010 stands.

### D. Layout, binding and pages (12b)

- **FR-030** — VSTGUI only (line 915), with no platform API. Pages do not use a `UIViewSwitchContainer`. Main and Advanced
  are the two sibling `CViewContainer`s in the built tree, exactly one visible at a time,
  toggled by `ProfundumPanelSubController` (the Vorago C-5 pattern).
- **FR-031** — Every **visible** registered ID (84 minus {4, 5, 6, 7} = 80) is bound to at least one control in the built tree.
  The hidden IDs 4–7 are bound to none. D1 / D2 list every ID bound on both pages (e.g. Depth).
- **FR-032** — Per-view class rule:
  - a `StringListParameter` ID (1, 2, 300, 301, 410, 411, 413–416, 600, 800, 807, 808, 809, 900, 901, 905, 906, 907, 909,
    911, 1000, 1001) binds to a `COptionMenu`, a segment control, or (two-label lists only) a two-state toggle
    (`note_params.h:298–316` registers 410, 411 and 413–416 as lists; 412 Portamento is a plain continuous parameter);
  - a continuous ID binds to an `ArcKnob`, `BipolarSlider`, `CSlider`, or a custom view that writes it. Pitch Bend Range 417
    is a stepped `RangeParameter` (0–24 st, `note_params.h:318–323`) and binds to a knob or slider that honours its steps.

  No `Parameter` object is changed, re-registered or retyped (roadmap line 916; `plugins/profundum/CLAUDE.md`: "never swap a
  type"). A display-only `CParamDisplay` may also bind an ID when the same page binds that ID to a writing control (value
  readouts; user ruling 2026-10-23, P-8); it never writes.
- **FR-033** — The `<control-tags>` block maps each tag name to its `ParamID` exactly. The eight Phase 10 tag names
  (`editor.uidesc:20–27`) keep their IDs.
- **FR-034** — A custom view that writes a parameter converts through that ID's registered law (the `kLawTable` row, via the
  pack's `normalizedFromPlain`), never through a law of its own. In particular, an `ADSRDisplay` bound to 420–426 must not emit
  its cubic `timeMsToNormalized` value (`adsr_display.h:1413–1417`) for the logarithmic amp-time IDs. The plan chooses between
  an adapter and a different control; this FR fixes only the invariant.
- **FR-035** — The portamento readout (412) re-formats when Glide Mode (414) changes (Phase 11 handoff 12.4; Phase 11 FR-007).
- **FR-036** — Display strings are the registered ones (Phase 11 SC-005): "Harmonic Fold" (500), the Character entry
  "Wavefold", Anchor Morph Mode "Hold / Blend / Yield", Anchor Mode "Lock / Follow / Punch / Ghost".

### E. Morph component (roadmap lines 886–891; Phase 11 handoff 12.1–12.3)

- **FR-040** — The morph component shows:
  - the Morph Position control (105), with a position readout in percent (the registered display string, SC-007 (d));
  - the two endpoints, each with a user-typed **name** (FR-047) and a derived **three-word descriptor** (FR-048), e.g.
    *A — DEEP PRESSURE · Round · Clean · Heavy*. The component always displays a name and a three-word descriptor per endpoint,
    never a name alone;
  - an **A / A·B / B compare** control;
  - **Copy A → B** and **Copy B → A** buttons;
  - the Edit Target selector (1001), showing which endpoint the sound controls edit.
- **FR-041** — The compare control calls `setCompare(CompareMode)` (`controller.h:83`). Its displayed state comes **only** from
  the controller's compare mode, which `kMsgCompareMode` sets (`controller.cpp:291–298`), never from the click itself. It
  reads that mode through the FR-046 getter. The compare control itself writes no state; pressing compare A or B additionally
  switches Edit Target 1001 (FR-049), and no other parameter is written.
- **FR-042** — The Copy buttons call `copyEndpoint(from)` (`controller.h:81`). The UI does not edit the mirror: the displayed
  values change when the processor's `kMsgEndpoint` reply re-displays them. Copy copies the sound only; each endpoint keeps its
  own name (user ruling 2026-10-23, P-4).
- **FR-043** — Switching Edit Target from the UI goes through `beginEdit` / `performEdit` / `endEdit` on 1001. The controller's
  Phase 11 hook (`controller.cpp:184–207`) then re-displays the 60 E-parameters and restarts once.
- **FR-044 (Lock dimming)** — While Anchor Morph Mode (1000) is Hold (engine `Lock`) and Edit Target is B, the Anchor controls
  (300–306) are dimmed or marked "held at A". B's Anchor fields are stored but inaudible in that state (Phase 8 handoff 12.3).
  The controls stay editable.
- **FR-045** — Anchor naming: the Anchor oscillator mode (301: Lock / Follow / Punch / Ghost) and the Anchor morph behaviour
  (1000: Hold / Blend / Yield) are two controls with two disjoint label sets. They never share one widget (roadmap lines
  897–900 and roadmap Open Question 10; Phase 8 handoff 12.4). FR-012 rules each one's section label and placement.
- **FR-046 (compare read path)** — Today the compare mode is private (`compare_`, `controller.h:105`). Its only reader is the
  test accessor `compareModeForTest()` (:92), and `notify` sets it without any change notification (`controller.cpp:291–298`).
  12b appends to the controller:
  - a public `const noexcept` getter for the compare mode;
  - a UI-thread change notification that `notify` fires on every accepted `kMsgCompareMode` (an `IDependent`-style
    `changed()` on a controller-owned object, or a listener; the plan names one).

  The compare indicator reads the getter and redraws on the notification. A production view never calls a `…ForTest`
  accessor.

- **FR-047 (endpoint names)** — Each endpoint's name is typed by the user in a VSTGUI text-edit control in the morph component
  (OQ-8 ruling). The names persist in the processor state, **appended after byte 510 under `kCurrentStateVersion = 3`**; v1 and v2
  streams still load, with the names defaulting to "A" and "B". Presets therefore carry names. The committed name reaches the
  processor by a controller → processor message the plan names (`kMsgEndpointNames` is reserved by the ODR sweep) and returns to
  the controller on state load, so a reopened editor shows the saved names. The name length bound and its byte layout are fixed in
  the plan and gated by SC-027.
- **FR-048 (endpoint descriptor)** — The three-word descriptor is **derived** from the endpoint's settings (the controller's
  `mirror_[e]`) through a documented table that D2 writes out (field → word sets and thresholds, e.g. Round / Clean / Heavy from
  Body / Fold / Depth). It is never stored: it is not in the state, not in any message, and not supplied by presets. It updates
  whenever the endpoint's settings change. Phase 13 factory presets supply names only.
- **FR-049 (compare drives Edit Target)** — Pressing compare **A** or **B** also switches Edit Target 1001 to that endpoint, by one
  `beginEdit` / `performEdit` / `endEdit` on 1001 per press (the FR-043 path), in addition to the one `kMsgCompare`. Pressing
  **A·B** sends `kMsgCompare` only and leaves Edit Target unchanged. Touching Morph Position 105 does not change the compare mode
  and sends no `kMsgCompare`.

### F. Motion (roadmap lines 892–894; Phase 7 handoff §4; Phase 9 handoff 12.4)

- **FR-050** — The main page shows the Motion source only (900: Off / Env / Cycle / Drift); its tooltip says the amount is set on
  the Advanced page, since Motion Amount 902 defaults to 0 and a source alone moves nothing (user ruling 2026-10-23; no registered
  default changes; the *Morph and motion* render sets 902 so its Motion is audible). The Advanced page shows:
  - source 900, target 901, amount 902, global Motion Depth 903, rate 904, shape 905, sync 906 + division 907;
  - as advanced detail: phase 908, retrigger 909, fade-in 910, polarity 911;
  - drift smoothness 912 and ENV attack / decay 913–914.
- **FR-051** — The field-Motion sync division (809) does not promise a distinct rate per note value: synced rates above 1 Hz fold
  into (0.5, 1] Hz (Phase 7 handoff §4, C-9). D2 rules whether the readout shows the effective rate or states the folding.
- **FR-052** — The polarity control states that a Bipolar ENV rests at −amount between notes, while a Unipolar one rests at the
  unmodulated value (Phase 9 handoff 12.4).

### G. Macro discoverability (roadmap lines 895–896)

- **FR-060** — Hovering or moving a macro (100–103) highlights the controls of the targets it drives. Nothing is highlighted
  otherwise, and no permanent matrix is drawn.
  - The highlight set of macro m is `kMacroTargetParamIds` restricted to m's `kRows` rows with `amount != 0`, so Dark ↔ Bright
    does not highlight SHAPE 501.
  - `CompressionAmount` has no control and is named in the macro's tooltip instead.
  - The table is derived from `kRows` at compile time, with a `static_assert` that every non-compression target maps to a
    registered ID.

- **FR-061 (macro offsets on knobs)** — Macro offsets are drawn as a **one-sided** modulation ring from the base value to base +
  offset, in the macro's direction (user ruling 2026-10-23, P-5): a plugin-local view in `plugins/profundum` (an `ArcKnob`
  subclass or sibling), because the shared `ArcKnob` ring (`setModulationRange`, `arc_knob.h`; CQ-6) draws both sides of the
  value. No shared-header change. The ring is computed in the controller from `ProfundumMacroMatrix::kRows` and the current values of
  macros 100–103 (the same table FR-060 uses), converted through the target's registered law as D3 states. It is deterministic:
  no new audio-thread data and no frame field. Rows with amount 0 (`DarkBright → Shape`) and `CompressionAmount` (no knob) draw
  nothing, and a target bound to a control that is not an `ArcKnob` draws no ring. **Motion modulation is not drawn.** The knob
  keeps showing the base value: the ring never moves the knob position (FR-005).

### H. Spectrum view and its data (roadmap lines 878–881, 915; Reuse row line 172)

- **FR-070** — The spectrum view is a new plugin-local **harmonic-bar view**, `HarmonicBarView`
  (`plugins/profundum/src/ui/`, namespace `Profundum::UI`; OQ-3 ruling). It shows the **actual harmonic structure** of the signal
  (lines 880–881): bars for the measured per-partial levels of the FR-071 frame (FR-075), with the Anchor reported separately from
  the Mass. Disrumpo's `SpectrumDisplay` is not promoted and `plugins/shared/` is not touched for it. The view is
  **display-only** in 1.0 (OQ-2 ruling): no drag, click or other gesture writes a registered ID, it issues no `beginEdit` /
  `performEdit`, and there is no draggable energy point. It appears **twice**, both instances fed the same frame: on the Main
  page as the signature visual, with the Anchor / Mass split drawn in it, and in the Advanced page's Core spectrum section. The
  controller holds both instances, or fans each frame out to both. This clause holds whatever drawing detail D1 / D2 rule.- **FR-071** — The data crosses from processor to controller by DataExchange (`ProfundumSpectrumFrame`; the Vorago / Membrum mechanism, FR-076), with
  the IMessage fallback:
  - `blockSize` is `sizeof` rounded **up** to the 32-byte alignment;
  - the producer runs on the audio thread with no allocation, lock, exception or I/O;
  - it publishes at most once per `process()`, and is a no-op when no queue is open.
- **FR-072** — The producer is read-only: rendering with the producer active and inactive gives identical output in the same
  binary.
- **FR-073** — Sanitising:
  - a non-finite value is never sent;
  - with no sounding voice, the frame says so and the view decays to empty;
  - the frame carries the sample rate or the f0 it describes, so a sample-rate change cannot mislabel partials;
  - partials above Nyquist (the core's cap) are absent, never drawn.
- **FR-074** — In POLY the frame describes one voice, chosen by a documented focus rule that D2 states: the most recently
  allocated sounding voice, retained while audible (the Seraphis / Vorago rule).

- **FR-075 (measurement, capacity, cadence)** — The frame carries measured levels (OQ-3 option (b), ruled 2026-10-23):
  - an append-only, `const noexcept`, behaviour-neutral dsp accessor (on `ProfundumEngine` and `ProfundumVoice`, FR-095) exposes
    the focus voice's **post-Character** samples (the signal `ProfundumVoiceTaps::charL / charR` describes);
  - an audio-thread Goertzel bank at n·f0, n = 1..64, runs over K = 8 f0 periods of those samples and publishes **one frame per
    completed window**: about 5 Hz at E1 and about 16.4 Hz at C3 (130.81 Hz in this repo; still at most one per
    `process()`, FR-071; corrected 2026-10-23 from "about 33 Hz", P-3);
  - the frame capacity is **64 partials**, about 300 bytes (`sizeof(ProfundumSpectrumFrame)` is at most 320 bytes before the
    32-byte rounding); harmonics above 64 are not measured and not shown; partials above Nyquist are absent (FR-073);
  - the frame therefore shows the Fold and Character harmonics (FR-025) up to partial 64;
  - the audio-thread cost stays inside the unchanged SC-013 bars (MONO 8 %, 4 voices 25 %, P/D 1.05). No capacity or cadence is
    changed to make a bar pass;
  - **the Goertzel bank is a plugin-local Highway kernel (user ruling 2026-10-24):**
    - it lives in `plugins/profundum/src/processor/goertzel_bank_simd.{h,cpp}`, uses Highway runtime dispatch (AVX2 + FMA on
      x86, NEON on ARM, scalar fallback; `LoadU` / `StoreU` only) and links the already-fetched `hwy` target. Nothing under
      `dsp/` changes;
    - the recurrence s[k] = x[k] + c_n s[k-1] - s[k-2] in double is unchanged. `Profundum_GoertzelBankSimdMatchesScalar`
      proves that the kernel equals a scalar reference within 1e-12 relative, for every count 1..64 and segment length 1..32;
    - why: on the SSE2 baseline, the 64 × 2 double bank cost about 3.5 % of the MONO engine, which left the P-ON / D bar about
      1 % of margin; isolated runs read 1.038 – 1.053.
- **FR-076 (transport; Linux)** — The same mechanism as Vorago and Membrum, nothing extra: a `DataExchangeHandler` in the
  processor and a `DataExchangeReceiverHelper` in the controller, whose `notify()` decodes the SDK IMessage fallback (the Vorago
  controller does so at `plugins/vorago/src/controller/controller.cpp:330–337`). There is no piggyback on `kMsgRequestSync` and no
  second path. **Linux:** the SDK plugin factory injects a run-loop timer whenever the host passes `Linux::IRunLoop` through
  `setHostContext` (`extern/vst3sdk/public.sdk/source/main/pluginfactory.cpp:316–319`), so the fallback queue opens in real Linux
  hosts. It stays closed only in a host that provides neither the DataExchange API nor a run loop (for example a bare test
  harness); D2 and D7 document that, and it is not worked around. The same correction applies to the Phase 11 note that Linux has
  no processor timer: the processor's SDK timer exists wherever the host supplies a run loop, and Phase 11's pull-on-switch
  (`kMsgRequestSync` on an Edit Target change) stays unchanged. Tests follow the Vorago pattern (SC-010 – SC-012, SC-023).

### I. Stereo field display (roadmap lines 882–884; concept §8)

- **FR-080** — The image-mode selector (800) and the SPREAD mini-spectrum (20 Hz – 10 kHz, MONO ↔ WIDE across frequency) sit
  in the **Stereo & Output** section of the Advanced page (OQ-4 ruling). The image mode 800 is mirrored on the Main page only if
  the FR-011 layout leaves room (D1 states yes or no; the mirror is the one permitted extra Main-page binding); the SPREAD
  mini-spectrum is not on the Main page.
  - The drawn quantity is the side magnitude **w(f) = c(f) · m**:
    - c(f) is `BassStereoField::widthCurve(f)` (`bass_stereo_field.h:397–403`, documented "tests, Phase 12"). It returns
      `lawCurve` (:632–644), so it depends only on the smoothed crossover 803, Gravity 802 (through `uLow_`, :649–650) and
      curve 804 (`gamma_`, :653), and it is 1 above the upper knee whatever the mode or Spread;
    - m is the magnitude `updateLawCache` multiplies in (:655–657): m = 0 in SOLID, otherwise
      m = spread · (1 − κ·μ + κ·μ·m_mot), with κ = `kMotionExcursion` = 0.5 (:78), spread = 801, μ = field Motion depth 805
      and m_mot the field Motion value in [−1, 1]. The static curve draws m_mot = 0. D2 rules whether the view also draws the
      Motion excursion band (m_mot = ±1).
  - Hence SOLID draws zero at every frequency, Spread 0 draws zero, and Spread scales the whole curve.
  - m comes either from a `const noexcept` magnitude observer appended to `BassStereoField` under the FR-095 fence, or from
    the controller computing the formula above from the registered values of 800, 801 and 805. The plan picks one. A
    controller-side `BassStereoField` calls `reset()` or `advanceControl()` after `setControls`, which only latches into
    `pending_` (:232–256).
  - WIDE's per-channel Character offsets (`wideOffsets`, :413–419) are not part of w(f). D2 states how WIDE is marked.
  - The crossover / Gravity knee is marked.
  - w(f) is evaluated off the audio thread. If the plan cannot evaluate it in the controller, it travels in the FR-071 frame.

### J. Editor lifecycle and sync (Phase 11 handoff 12.1)

- **FR-090** — While the editor is open, the controller sends `kMsgRequestSync` from an editor-side `CVSTGUITimer` (period
  ruled in the plan, ≤ 100 ms). This keeps an open editor fresh in any host where the processor's 50 ms SDK timer does not exist
  (`processor.h:201`; `Timer::create` returns nullptr when the host supplies no run loop, while Linux hosts that pass `IRunLoop`
  have the timer, FR-076). The timer stops in `willClose` and never runs while the editor is closed.
- **FR-091** — The controller clears every raw view pointer it holds in `willClose`. A view creates its timer in `attached()`
  and destroys it in `removed()`. A DataExchange block that arrives while the editor is closed is cached, not drawn.
- **FR-092** — `createView` keeps returning `new VSTGUI::VST3Editor(this, "editor", "editor.uidesc")` for
  `ViewType::kEditor` and `nullptr` otherwise. Unknown names passed to `createCustomView` / `createSubController` return
  `nullptr`.

### K. Not adopted (roadmap lines 909–913)

- **FR-093** — The built tree contains no Oversampling, Quality or Low-End Protection control or label, and no global
  morph-curve selector.

### L. Cross-cutting and scope fences

- **FR-094** — Build hygiene:
  - zero compiler warnings on `Profundum` and `profundum_tests`;
  - `node tools/check-portability.js` clean;
  - `tools/lint-apple-globals.js` clean (new UI names in a global or anonymous namespace are the documented macOS hazard);
  - clang-tidy `profundum` clean;
  - pluginval strictness 5.
- **FR-095** — (Gate, user ruling 2026-10-23: the accessors are read-only and behaviour-neutral, so the gate is the
  `dsp_systems_tests` `[profundum]` per-push set plus `Profundum_EngineDisplayAccessorNeutral`'s bit-identity proof; the 46
  `[long]` dsp cases that include the two headers are listed, not run.) `dsp/` is touched by the OQ-3-ruled engine accessor (FR-075) and, only if the plan picks it, the FR-080 magnitude observer. Each
  such change is append-only and `const noexcept`, and changes no output bit. The candidate modified headers are:
  - `systems/profundum_engine.h`: `voices_` is private (:1231; `private:` at :424);
  - `systems/profundum_voice.h`: `ProfundumCore core_` is private (:973; `private:` at :700), so a focus-voice accessor
    needs an append here as well;
  - `systems/profundum_core.h`: only if OQ-3 option (b) needs a read the core does not expose (`deliveredGains()` is :318);
  - `systems/bass_stereo_field.h`: only for the FR-080 magnitude observer.

  The close lane then runs the full per-push suites of every consumer of each modified header, found by `grep -rl` of the
  header name over `dsp/` and `plugins/` (at minimum `dsp_systems_tests` `[profundum]` and `profundum_tests`), per the
  change-set rule. `plugins/shared/` is not touched: no view is promoted (OQ-3 ruling).

  **Allow-list addition (2026-10-24, T035 fence red; applying the zero-warning rule SC-021 and the evidence-restore rule).**
  Three more path sets belong to the Phase 12 change set:
  - the nine test-only helper headers `processors/harmonic_fold_test_helpers.h`, `processors/shape_character_metric_helpers.h`, `processors/transient_shaper_test_helpers.h`, `systems/anchor_oscillator_test_helpers.h`, `systems/bass_morph_test_helpers.h`, `systems/bass_stereo_field_test_helpers.h`, `systems/mass_dynamics_test_helpers.h`, `systems/profundum_core_test_helpers.h`, `systems/profundum_voice_engine_test_helpers.h`
    (under `dsp/tests/unit/`). Phase 12's `profundum_tests` TUs include them, and MSVC raised Catch2 sign-compare warnings
    there (G18 fix). The only edit is an unsigned literal suffix in `REQUIRE` preconditions (`> 0` to `> 0u`), which is
    behaviour-neutral. Their gate is a zero-warning rebuild of every executable that compiles them (`dsp_processors_tests`,
    `dsp_systems_tests`, `profundum_tests`) plus the per-push lanes this row already runs. It does not include the
    full `dsp_processors_tests` suite, the same treatment the pure-code-move rule gives;
  - `.gitignore` (one line ignoring `evidence/listen/*.wav`);
  - `specs/profundum-phase9-voice-engine/compliance.md` and `evidence/listening-sheet.md` (the SC-035 user verdicts, FR-097).

  Evidence files of earlier phases that the consumer lanes regenerate are restored to HEAD before the fence runs and
  before the commit. They are never part of the change set.
- **FR-096** — No change to any registered parameter, to `IMidiMapping` (OQ-9: unchanged, FR-098) or to the Phase 11 message
  protocol, except that FR-047 may add one controller → processor message for the endpoint names. The only state change is the
  OQ-8 ruling (FR-047): the endpoint names, appended after byte 510 under `kCurrentStateVersion = 3`, with v1 and v2 streams
  still loading and the bytes before the names unchanged apart from the version number.
- **FR-098 (host-level MIDI learn only)** — MIDI learn is host-level only in 1.0 (OQ-9 ruling): the macros 100–103 and Morph
  Position 105 are automatable host parameters, `IMidiMapping` stays as Phase 11 shipped it, the Controller does not implement
  `IMidiLearn`, and the editor offers no MIDI-learn control.

### M. Inherited verdicts (Phase 9 handoff 12.3)

- **FR-097** — The Phase 9 listening verdicts (Phase 9 SC-035) are owed to this phase
  (`specs/profundum-phase9-voice-engine/evidence/phase-handoffs.txt:201–205`). The listening sheet has eight Listen rows:
  H-1, H-2, H-8, H-10, H-14, H-19, H-28(1) and H-28(2). Before 12b starts, Clarifications lists each of the eight as either a
  verdict the user gave (dated) or explicitly still pending. Every verdict that asks for a change appears as a ruling in its
  owning FR / SC row. An agent never fills a verdict (SC-001).

---

## Success Criteria

Unless stated otherwise, every SC runs in `profundum_tests` (namespace `ProfundumPluginTest`), tagged `[profundum]` plus the
listed tags.

- **SC-001 — Stage gate.** Before the first 12b commit, Clarifications holds a dated user ratification of D1–D7 and the rulings
  of every Resolved Question (recorded 2026-10-23), each also written into its owning FR / SC row. The ratification list includes:
  - D1 together with its concept §14 delta table (FR-003);
  - the dated user listening verdict on the FR-006 main-page render set (SC-006 (b));
  - the FR-097 account of the eight Phase 9 listening verdicts: each recorded as given by the user or listed as still pending,
    and each change-requesting verdict written as a ruling in its owning FR / SC row;
  - every D2-ruled string that an SC compares (SC-005).

  An agent never fills a user verdict. Checked at plan / tasks review: `git log` shows no change to a 12b-scope file before the
  ratification date.
- **SC-002 — Binding completeness.** `Profundum_UidescBindsEverySurfaceId` (`[ui]`):
  - `unreachableParams(xml, all84, allowlist {4, 5, 6, 7})` is empty;
  - no control in the built tree carries tag 4, 5, 6 or 7 (checked in the section of `Profundum_EditorLifecycle` that replaces
    `EditorBindsSurface`);
  - the set of bound tags equals the 80 visible IDs.
- **SC-003 — Tag integrity.** Every `<control-tag>` value is a registered ID, and every `control-tag=` attribute names a declared
  tag. The eight Phase 10 names keep their values (FR-033).
- **SC-004 — Class rule and frozen surface.** Every bound control's class satisfies FR-032 for its ID's registered kind.
  `Profundum_ParamTableConformance`, `Profundum_ParamDisplayFormats`, `Profundum_StateVersions` and
  `Profundum_StateRoundTripV2` pass with no edit to their v1 / v2 load expectations. The OQ-8 ruling adds the v3 arms (FR-047,
  SC-027); the assertion that names the current version moves from 2 to 3, and every `getState` length assertion moves from
  `kStateV2Bytes` to `kStateV3Bytes` (36 lines in 7 Phase 11 test files; user ruling 2026-10-23, P-2).
- **SC-005 — Labels and statements.** `Profundum_UiLabels` (`[ui]`). Every check is a plain string compare against a
  registered string or a D1 / D2-ruled string:
  - the label / menu strings shown for 301 and for 1000 are disjoint sets, and equal the registered strings (FR-036, FR-045);
  - the Main page holds the text `MORPH BEHAVIOUR` inside the Anchor group container that also holds the 1000-bound control, and
    no 301-bound control is on the Main page; the Advanced page holds the text `ANCHOR MODE` labelling the 301-bound control
    (FR-012);
  - no text node or tooltip in the built tree contains "Oversampling", "Quality", "Low-End Protection" or "Parameter Curve"
    (FR-093);
  - mono guarantees (FR-024): D2 rules the exact tooltip text for each of 800–809, and for 600–602 the WIDE caveat. Each of
    those controls' tooltips contains its ruled string;
  - no text node or tooltip on the Main page contains "oscillator", "filter", "LFO" or "partial", case-insensitive, except a
    word D1 explicitly rules in (FR-020);
  - the tooltip or label of 809 contains the D2-ruled field-sync folding statement (FR-051), and that of 911 contains the
    D2-ruled Bipolar-rest statement (FR-052).
- **SC-006 — Main page sufficiency** (FR-006):
  - (a) D3's table gives every main-page control a gated PASS row, either in the Phase 9 audibility table
    (`…phase9…/evidence/audibility-table.csv`) or in `Profundum_ParamPathAudibility`. A control with no gated row is a defect
    that must be ruled before 12b (checked at 12a review).
    **Ruled 2026-10-24 (user, "add 5 rows, exempt 1001"):** D3 found six Main IDs with no gated row (1, 200, 201, 204,
    1000, 1001). Five get a gated processor-path row in `Profundum_MainPageAudibility`
    (`plugins/profundum/tests/integration/main_page_audibility_test.cpp`, `[profundum][audibility]`), each at E1 and C3, 48 kHz,
    host block 64, every value written as a normalised parameter change, and each read over the held window [1.0 s, 2.0 s):
    - 1 Voice Mode, Mono → Poly: E1 is held from 0 s and C3 is added at 1.0 s (at C3 the pair is C3 + C4). The reading is
      E_sub at the first note's f0 on the Sum tap over [1.25 s, 2.0 s). Bar: ≥ 3 dB.
    - 200 Depth, 0 → 1, on the kSawAnchor recipe with Edge 0.5 (the AT-02 base): mean frame D_cx on the Sum tap,
      |difference| ≥ 3 dB.
    - 201 Body, 0 → 1, same base: mean frame D_cx, |difference| ≥ 3 dB.
    - 204 Edge, 0 → 1, same base: mean frame D_br, |difference| ≥ 0.25 oct.
    - 1000 Anchor Morph Mode, Hold (Lock) → Blend, at Morph Position 1, with endpoint A Anchor Level 1 and endpoint B Anchor
      Level 0 (nothing else differs): E_sub on the Sum tap, |difference| ≥ 2 dB, which is the AT-10 anchor-level bar.
    1001 Edit Target is exempt. It is a routing control, not a sound lever, and its audibility is AT-41 / AT-42 (105), whose B
    endpoint is written through 1001 = B. These bars are the Phase 9 descriptor families' own and are never relaxed.
  - (b) The FR-006 render set (default patch, every Advanced-only ID at its registered default, only D1's main-page controls
    varied) carries a dated user listening verdict in Clarifications before ratification (SC-001). An agent never fills it.
  - (c) `Profundum_MainPageSanity` (`[ui][render]`) renders the same set through the processor at E1 and C3, 48 kHz. For each
    render: every sample is finite (bit-pattern check, not `std::isnan`); the RMS after the attack is above −60 dBFS
    (non-silent); the sample peak does not exceed the output ceiling (ID 3 at its registered default); and the sub band is gated at each guarantee's own stage (user-ruled 2026-10-23): ρ ≥ 0.9989
    on the **generator tap** through the observer (the Phase 7 guarantee quoted in FR-024), and, on the **processor output**,
    ρ ≥ 0.99 with Q_sub ≤ 1.0 dB (`kSubBandRhoBar`, `kSubBandQBarDb`, `bass_stereo_field_test_helpers.h:95, 97`) using the Phase 9
    SC-015 band definition. The 0.9989 bar is not applied to the output.
- **SC-007 — Morph component.** `Profundum_MorphComponentUi` (`[ui][morph]`) runs with a mock component handler and a connected
  processor stand-in:
  - (a) each compare button sends exactly one `kMsgCompare` (modes A = 1, A·B = 0, B = 2). Pressing A or B also issues exactly one
    begin / perform / end on 1001 carrying the pressed endpoint (FR-049); pressing A·B issues zero `performEdit` and leaves 1001
    unchanged; moving Morph Position 105 sends no `kMsgCompare` and leaves the compare mode unchanged;
  - (b) the compare indicator reads the FR-046 public getter. A click alone leaves it unchanged; it changes only after
    `kMsgCompareMode` arrives, and the FR-046 notification fires exactly once per accepted message;
  - (c) each Copy button sends one `kMsgCopyEndpoint` with `from` 0 / 1 and calls `setDirty`;
  - (d) the position readout text equals the registered `getParamStringByValue(105, p)` for p ∈ {0, 0.005, 0.42, 0.995, 1}.
    That formatter prints `"%.0f%%"` of 100·p (`morph_params.h:280–282`), so the expected literals are "0%", "0%", "42%",
    "100%", "100%". The readout has no formatter of its own;
  - (e) selecting B in the Edit Target control produces one begin / perform / end on 1001; afterwards the 60 E-parameters equal
    `normalizedFromLeafWord` of `mirror_[B]`, and `restartComponent(kParamValuesChanged)` has been called once.
- **SC-008 — Lock dimming.** `Profundum_MorphLockDimming` (`[ui][morph]`) runs every combination of 1000 ∈ {Hold, Blend, Yield}
  and 1001 ∈ {A, B}. The controls of 300–306 report dimmed exactly when 1000 = Hold and 1001 = B, and dimmed controls still
  accept edits.
- **SC-009 — Macro highlight.** `Profundum_MacroHighlight` (`[ui]`):
  - hovering macro m highlights exactly the ID set derived from `kRows`: m's rows with `amount != 0`, minus `CompressionAmount`;
  - Dark ↔ Bright's set excludes 501;
  - un-hovering clears every highlight;
  - moving without hovering: a `beginEdit` / `performEdit` / `endEdit` drag on macro m whose pointer leaves the control
    mid-drag, and a keyboard or wheel edit of m, each highlight exactly m's set while the edit is open and clear on `endEdit`;
  - each set equals the D3 table, which is generated from the same `kRows`.
- **SC-010 — Spectrum frame correctness.** `Profundum_SpectrumFrameMatchesSignal` (`[ui][spectrum]`). Two arms:
  - **seam arm**, every OS: the producer's fill function is called directly on the processor's blocks;
  - **connected arm**: real processor and real controller through the DataExchange fallback. It first asserts that the queue
    opened (SC-023). On Linux only, a queue that did not open WARN-skips the arm (the Vorago precedent,
    `plugins/vorago/tests/integration/ecosystem_frame_test.cpp:25–29, :115–128`); on Windows and macOS a closed queue fails.

  **Reference**, independent of the frame's source: the level at n·f0 measured offline, in double precision, on the focus
  voice's post-Character tap (`ProfundumVoiceTaps::charL / charR`, `profundum_engine_observer.h:61–65`, test seam only). A
  Goertzel at each n·f0 runs over a Hann window spanning K = 8 f0 periods (FR-075) that ends at the frame's
  publish point; level = 10·log10(|G_L|² + |G_R|²). Over an integer number of periods every harmonic sits on a DFT bin, and a
  Hann window spreads a bin-centred tone only into its ±1 neighbours, so no partial leaks into another. The fixtures hold the
  pitch static (Motion 900 Off, Drop Amount 0, no bend). The Anchor reference is the RMS of the observer's `anchorL / anchorR`
  over the same window.

  **Fixtures** (MONO, after 0.5 s of a held note):
  - the Phase 8 showcase A and B and the default patch, at E1 and C3, 48 kHz;
  - a **fold fixture**: showcase B with Fold 500 = 100 and Character Amount 601 = 1, at E1 and C3, 48 kHz. The frame must report
    the Fold- and Character-generated partials within the same tolerance;
  - a **Nyquist fixture**: the default patch at C6 (1046.5 Hz), 44.1 kHz, where the 64 core partials (`kDefaultPartials`,
    `profundum_core.h:97`) would cross Nyquist.

  **Checks:**
  - every partial the frame reports at or above −60 dB relative to its strongest agrees with the reference within ±0.5 dB, and
    every reference partial at or above −60 dB, up to the frame's partial capacity of 64, is reported;
  - the number of partials the frame reports is ≤ floor(Nyquist / f0) (21 at the Nyquist fixture), and none lies above Nyquist;
  - the Anchor level is reported separately and agrees with its reference within ±0.5 dB;
  - with all voices silent, the frame reports silence within one publish after the voice ends.

  The reference never reads the producer's source, so the check cannot be circular. Nothing relaxes: OQ-3 option (a) was not
  adopted (FR-025), so the fold fixture's expectation stands.
- **SC-011 — Producer is read-only.** `Profundum_SpectrumFrameDoesNotChangeAudio` (`[ui][spectrum]`) renders 2 s (note on /
  off, a morph sweep, a POLY chord) twice in the same binary and compares the two live renders, not a stored golden: max |Δ| = 0
  on both channels. Two arms:
  - seam arm, every OS: the producer's fill function invoked on every block vs never invoked;
  - connected arm: queue asserted open (SC-023) vs no connection. On Linux a queue that did not open WARN-skips this arm, so
    the comparison is never between two closed-queue renders.
- **SC-012 — RT safety.** `Profundum_SpectrumFrameAllocationFree` (`[ui][rt]`) runs 2000 `process()` calls under
  `AllocationScope` with notes and parameter changes in flight: 0 allocations. Two arms: the seam arm (fill function invoked
  every block, every OS) and the connected arm (queue asserted open, SC-023; WARN-skip on Linux only). That no lock is taken is
  a code-review row.
- **SC-013 — CPU unchanged.** `Profundum_ProcessorCpu` and `Profundum_ProcessorCpuFullSurface` each gain a producer-on arm. Bars,
  unchanged and never relaxed: MONO ≤ 8 % and 4 voices ≤ 25 % of one core, P / D ≤ 1.05, control window 1.05. Run alone via
  `node tools/run-cpu-tests.js profundum_tests`.
- **SC-014 — SPREAD curve.** `Profundum_SpreadCurveMatchesField` (`[ui]`) covers Spread {0, 0.5, 1} × Gravity {0, 0.5, 1} ×
  crossover {60, 120, 250} Hz × curve {0, 0.5, 1} × image mode {Solid, Spread, Wide} × field Motion depth 805 {0, 1}. At 64
  log-spaced points over 20 Hz – 10 kHz, the view's sampled values equal the reference w(f) = c(f) · m (FR-080) within 1e-6.
  c(f) is `widthCurve(f)` of an independent `BassStereoField` given the same controls and then `reset()`, so the latched
  controls apply and the slew has converged; m is the FR-080 formula with m_mot = 0. Further checks:
  - SOLID draws 0 at every point, and Spread 0 draws 0 at every point;
  - with mode ≠ Solid and depth fixed, the value at 10 kHz (above the upper knee, c = 1) equals m, and the three Spread values
    give three distinct, linearly spaced results (the Spread axis is live);
  - depth 1 gives half the depth-0 value at the same Spread (κ = 0.5, m_mot = 0);
  - c(f) differs across the Gravity axis and across the crossover axis at some point below the upper knee (both axes are live);
  - Spread and Wide give the same w(f) (mode enters m only through SOLID; WIDE's Character offsets are not drawn, FR-080).
- **SC-015 — Linux sync.** `Profundum_EditorSyncTimer` (`[ui][lifecycle]`). There is no portable headless pump for
  `CVSTGUITimer` (Vorago R-5, `specs/vorago-phase13-ui/spec.md:111–114`), so the timer's wall-clock period is not measured.
  Every check runs headless:
  - the editor sync timer exists exactly while the editor is open (a new `hasEditorSyncTimerForTest()` is true after open and
    false after `willClose`); its configured interval equals the ruled period, and that period is ≤ 100 ms;
  - each invoked tick of the timer callback sends exactly one `kMsgRequestSync` to a counting peer. After `willClose` the timer
    is gone and no further request is sent;
  - with a connected real processor and **no** timer callback pumped on either side, one editor tick, then a processor-side
    endpoint change (a parameter change delivered through `process()`), then a second tick, update the displayed E-parameters.
    A unit test never fires the processor's SDK timer, which reproduces a host that supplies no run loop, where `Timer::create` returns nullptr
    (`processor.h:201`; real Linux hosts that pass `IRunLoop` have the timer, FR-076). The path under test is the synchronous one: `Processor::notify(kMsgRequestSync)` sets the sync flags
    and calls `flushControllerSync()` before returning (`processor.cpp:585–594`), and the resulting `kMsgEndpoint` reaches
    `Controller::notify` (`controller.cpp:265–289`).
- **SC-016 — Editor lifecycle.** `Profundum_EditorLifecycle`:
  - `HarnessCycles` runs 10 cycles;
  - the binding section replaces `EditorBindsSurface` and asserts the SC-002 counts;
  - each custom view class appears in the built tree exactly the number of times D1 / D2 rule (`HarmonicBarView` appears exactly twice: one instance on Main and one in the Advanced Core spectrum
    section, FR-070). The controller holds every instance, or fans each frame out to all of them, and all of its
    view pointers are null after `willClose`;
  - FR-091: a frame delivered while the editor is closed touches no view pointer and is cached; on the next open the views
    draw the cached frame;
  - unknown `createCustomView` / `createSubController` names return `nullptr`;
  - an attach / remove arm calls `attached` / `removed` on each timer-owning view for 10 cycles and asserts the timer exists
    only while attached.

  One ASan run (`-DENABLE_ASAN=ON`, Debug) of `profundum_tests "[lifecycle]"` is clean, and its log is recorded.
- **SC-017 — Page switch.** `Profundum_Editor_PageSwitch` (`[ui]`):
  - every page container is in the built tree;
  - selecting each page makes exactly that container visible;
  - the page selection survives an editor close / reopen within a session and is never written to state (`getState` bytes
    are identical before and after page switches).
- **SC-018 — Portamento readout.** `Profundum_UiPortamentoUnitFollowsGlideMode` (`[ui]`): stepping 414 through its three values
  changes the unit string of the 412 readout accordingly, with 412 itself untouched.
- **SC-019 — Scaling, accessibility and placement.** `Profundum_UiLayout` (`[ui]`). At every ruled zoom step (100 / 125 / 150 / 200 %, FR-014):
  - every bound control's rect lies inside the frame;
  - no two bound controls on the same visible page overlap;
  - every hit target is at least the D7 minimum, and every font at least the D7 minimum size;
  - every bound control has a non-empty tooltip or an adjacent label;
  - text colour against its background meets the D7 contrast ratio, computed from the uidesc colour table;
  - FR-022: on every page that shows a 105-bound control, a 1000-bound control sits in the same container;
  - FR-050: the Main page binds 900 and no ID in 901–914.
- **SC-020 — pluginval.** `tools/pluginval.exe --strictness-level 5 --validate
  "build/windows-x64-release/VST3/Release/Profundum.vst3"` passes, editor open / close included.
- **SC-021 — Build hygiene.** Zero MSVC warnings on `Profundum` and `profundum_tests`. `node tools/check-portability.js`,
  `tools/lint-apple-globals.js` and clang-tidy `profundum` are clean. The macOS / Linux CI legs go green after the push; until
  then they are recorded as PENDING-CI.
- **SC-022 — Engine accessor (OQ-3 option (b), ruled).** `dsp_systems_tests "[profundum]"` is green per-push, and the engine's
  existing determinism and render tests pass unchanged (the accessor is append-only, `const noexcept`, and changes no output bit).
  The accessor's post-Character samples for a render equal the observer's `charL / charR` tap of the same voice, and the
  per-partial levels the Goertzel bank (FR-075) computes from them agree with an offline double-precision Goertzel on that
  observer tap, over SC-010's window, within SC-010's ±0.5 dB for every partial at or above −60 dB. (The observer carries no
  per-partial gains: `ProfundumVoiceTaps` holds sample spans and scalars, `profundum_engine_observer.h:61–65`.)- **SC-023 — DataExchange handler lifecycle.** `Profundum_SpectrumFrameHandlerLifecycle` (`[ui][spectrum][lifecycle]`), real
  processor and real controller, no host `IDataExchangeHandler`, so the SDK fallback runs. Its `openQueue` fails when
  `Timer::create` returns nullptr (`extern/vst3sdk/public.sdk/source/vst/utility/dataexchange.cpp:77–85`), which is the Linux
  default without an injected factory (`extern/vst3sdk/base/source/timer.cpp:347–352`).
  - At the open point the plan names (connect, as Membrum does, or activate), the controller receives exactly one
    `queueOpened` with user context `kProfundumFrameUserContextId` and block size `sizeof(ProfundumSpectrumFrame)` rounded up
    to 32 bytes.
  - The matching close gives exactly one `queueClosed`. A re-prepare 48 → 96 kHz re-opens exactly once.
  - On Linux only, a queue that does not open WARN-skips the queue assertions; on Windows and macOS it fails. That Linux closure
    is the test harness's (no host run loop, FR-076); real Linux hosts that pass `IRunLoop` open the queue.
- **SC-024 — Custom-view laws** (FR-034). `Profundum_UiCustomViewLaws` (`[ui]`): for every custom view that writes a
  parameter (the envelope display bound to 420–426; the OQ-2 ruling gives `HarmonicBarView` no drag mapping, and the test asserts it issues no
  `performEdit` on any gesture), drive the view to the registered
  minimum, a mid value (100 ms for the amp times) and the registered maximum of each ID it writes. Each emitted normalized
  value equals the registered law's `normalizedFromPlain(plain)` within 1e-6, and the parameter's `toString` of the emitted
  value round-trips to the plain value at its display precision. An `ADSRDisplay` wired without an adapter fails this SC on
  420.
- **SC-025 — Focus voice** (FR-074). `Profundum_SpectrumFrameFocusVoice` (`[ui][spectrum]`), POLY, seam arm on every OS and a
  connected arm as in SC-010:
  - one note: the focus is its voice;
  - two notes: the focus moves to the later one;
  - release of the later note: the focus returns to the earlier one while it sounds;
  - a steal (polyphony 2, a third note): the focus is the stealing note's voice, identical across two runs;
  - all released: the focus is retained through the tail, and the frame reports silence once the tail ends.

  In every step the frame's f0 equals the focus note's f0.
- **SC-026 — Frame sanitising** (FR-073). `Profundum_SpectrumFrameSanitise` (`[ui][spectrum]`):
  - a non-finite value injected through the seam into the producer's input never reaches the frame: every float of every
    published frame is finite, checked by bit pattern (not `std::isnan`);
  - after a re-prepare 48 → 96 kHz, the first frame published after activate carries 96000 Hz, and no frame carrying 48000 Hz
    is read after it;
  - after a silence frame, the view's drawn bar heights decay monotonically to 0 within the decay time D2 rules.

- **SC-027 — Endpoint names and descriptor** (FR-040, FR-047, FR-048). `Profundum_EndpointNames` (`[ui][morph][state]`):
  - (a) committing a typed name in the A and B text-edit controls sends the FR-047 message exactly once per commit; after
    `getState` and a `setState` on a fresh processor and controller the names equal the typed ones (including a name of the
    maximum ruled length), and the controller shows them;
  - (b) `kCurrentStateVersion` is 3. A v2 stream and a v1 stream still load, the names are "A" and "B", and every other restored
    value equals its pre-phase result;
  - (c) the bytes before the names in a v3 stream equal the v2 stream of identical settings, apart from the version number;
  - (d) for the endpoint states D2 lists, the three words equal the documented table's; two endpoints with identical settings and
    different names give the same descriptor; changing a setting the table reads changes the words as the table says;
  - (e) the descriptor is not stored: two streams with identical names whose settings differ in a way that changes the
    descriptor have the same length and differ only inside the settings bytes;
  - (f) the morph component's text for each endpoint is non-empty and carries a name and exactly three descriptor words.
- **SC-028 — Macro modulation rings** (FR-061). `Profundum_MacroModulationRing` (`[ui]`): for each macro m in 100–103 and each
  `kRows` target bound to an `ArcKnob`, the ring equals the value computed from the row's amount, curve and unit and the macro's
  value, within 1e-6; a macro at neutral draws 0; the `DarkBright → Shape` row (amount 0) and `CompressionAmount` draw nothing;
  the ring never changes the knob's value; the rings are identical with Motion 900 Off and with Motion 900 on Cycle (Motion is
  not drawn); two evaluations with the same inputs give the same rings.
- **SC-029 — Host-level MIDI learn only** (FR-098). `Profundum_MidiLearnHostLevel` (`[ui]`): parameters 100–103 and 105 carry
  the automate flag and not the read-only flag; `Controller::queryInterface(IMidiLearn::iid)` returns `kNoInterface`; the
  Phase 11 `IMidiMapping` tests pass with no edit; no text node or control in the built tree is a MIDI-learn control.
- **SC-030 — Frame capacity and cadence** (FR-075). `Profundum_SpectrumFrameCadence` (`[ui][spectrum]`), seam arm on every OS:
  - holding a note at E1 and at C3 for 2 s at 48 kHz, the number of frames published equals the number of completed 8-period
    windows within ±1, and no `process()` call publishes more than one;
  - `sizeof(ProfundumSpectrumFrame)` is at most 320 bytes (`static_assert`), and the handler block size is that size rounded up to
    32 bytes;
  - on the fold fixture (SC-010) the frame reports exactly min(64, floor(Nyquist / f0)) partials and none above partial 64.
- **SC-031 — Placement** (FR-004, FR-011 – FR-013, FR-070, FR-080). `Profundum_UiPlacement` (`[ui]`):
  - (a) the set of IDs bound on the Main page equals the FR-011 set, plus 800 only if D1 rules the mirror; none of 205, 102 and
    301 is bound on the Main page;
  - (b) 205, 102 and 301 are bound on the Advanced page; each ID of the FR-004 rows (including 202–203, 303–306, 410–411 / 413 /
    415–417, 501, 600–602, 700–701, 2 and 3) is bound inside its named section container;
  - (c) MODULATION and SETTINGS are tab containers inside the Advanced page container, and the page containers are exactly Main
    and Advanced (SC-017);
  - (d) the 800-bound control and the SPREAD mini-spectrum are in the Stereo & Output section, and the SPREAD mini-spectrum is not
    on the Main page;
  - (e) the two `HarmonicBarView` instances (Main, Advanced Core spectrum) are drawn from the same frame and produce identical bar
    heights.
- **SC-032 — Zoom control** (FR-014). `Profundum_UiZoom` (`[ui]`): selecting each step calls `setZoomFactor` with 1.0, 1.25, 1.5
  and 2.0; the control is bound to no registered ID and the registered parameter count stays 84; `getState` bytes are identical
  before and after zoom changes; the editor offers no size other than the four steps.

---

## Edge cases

- **Editor closed while the audio runs.** Frames keep arriving into the cache, or an unopened queue drops them. No view pointer
  is dereferenced, and the editor timer is stopped.
- **Host without DataExchange.** The SDK fallback queue opens only when `Timer::create` succeeds (`dataexchange.cpp:77–85`). On
  Windows and macOS it does, and the controller's `DataExchangeReceiverHelper::notify` decodes the fallback's IMessage before its
  own message dispatch (Vorago / Membrum mechanism, FR-076). On Linux the SDK factory injects a run-loop timer whenever the host
  passes `IRunLoop`, so real Linux hosts open the queue too. The queue stays closed only in a host that provides neither the
  DataExchange API nor a run loop (for example a bare test harness): the spectrum view then stays empty, which D2 and D7 document
  and nothing works around. The morph component, the parameters and a controller-computed SPREAD curve (FR-080) still work.
  SC-010 – SC-012 and SC-023 WARN-skip their connected arms on Linux only, because the Linux test harness has no run loop.- **Sample-rate change / re-prepare.** The queue re-opens on activate, and a frame from the old rate is never read at the new one
  (FR-073). Outside [22050, 192000] Hz the processor renders silence and the view shows empty.
- **No sounding voice / note-off tail.** The frame reports silence once the tail ends. The view decays rather than freezing on a
  stale spectrum.
- **POLY chords.** One focus voice is shown (FR-074), and a voice steal moves the focus deterministically (SC-025).
- **Compare active while Morph Position sits elsewhere.** In compare A or B the engine plays an endpoint, while Morph Position
  (105) keeps its value. The UI shows both: the compare indicator lit and the position unchanged. Pressing compare A or B also moved Edit Target to that
  endpoint (FR-049); moving the position never changes the compare mode. `setState` and
  `setActive(false)` reset compare to A·B (Phase 11), and the indicator follows the processor's reply.
- **Edit Target switched mid-drag.** The drag's `endEdit` closes on the old target's ID before the switch, so no value lands on
  the wrong endpoint; the switch then re-displays from the mirror.
- **State load with the editor open.** `setComponentState` re-displays without `restartComponent` (Phase 11). The views redraw
  from the parameters and from the next `kMsgEndpoint`.
- **Motion and macros active.** Knobs show the base (endpoint) values: macro offsets and Motion modulation never move a knob
  (FR-005); macro offsets are drawn as an `ArcKnob` modulation ring (FR-061) and Motion is not drawn. The spectrum shows the
  modulated signal (FR-025).
- **Hold (Lock) with B edited.** B's Anchor edits are stored and shown dimmed, not lost (FR-044).
- **Seed determinism.** No UI action reseeds the engine or reorders its events. The seed is persisted and unregistered
  (Phase 11), and the producer is read-only (SC-011).
- **Parameter extremes.** Readouts at the registered limits (e.g. Morph Ramp 5 / 2000 ms, Drop Amount 48 st, portamento 0 /
  10000 ms) fit their label rects at every D7 size (SC-019).
- **Linux.** A host that supplies a run loop gives the processor its SDK timer (FR-076); where none exists, freshness depends on
  FR-090. A late `kMsgEndpoint` after an Edit Target switch
  re-displays again (Phase 11 behaviour, unchanged).
- **Bipolar ENV between notes.** A Motion ENV in Bipolar polarity holds its targets at −amount between notes, and the UI says so
  (FR-052).

---

## Resolved Questions (deferred to this spec by the roadmap; ruled 2026-10-23)

Every question below is decided. The body of the spec states each behaviour; the enforcing ids are in brackets.

- **OQ-1 — Main-page knob set.** Ruled: the conversation set plus Mass 104 and Drop 400; Shift 205 and Tight ↔ Loose 102 move to
  Advanced. [FR-003, FR-011, SC-031]
- **OQ-2 — Spectrum placement and interactivity.** Ruled: display-only, two instances (Main and Advanced Core spectrum), same
  frame, no draggable energy point. [FR-070, SC-016, SC-024, SC-031]
- **OQ-3 — Spectrum implementation and data source.** Ruled: a new plugin-local `HarmonicBarView` fed by option (b), measured
  per-partial levels (64 partials, K = 8 windows); options (a) and (c) not adopted; Disrumpo's view not promoted. [FR-025,
  FR-070, FR-075, FR-095, SC-010, SC-013, SC-022, SC-030]
- **OQ-4 — Image-mode selector and SPREAD mini-spectrum.** Ruled: Advanced Stereo & Output; image mode mirrored on Main only if
  room. [FR-004, FR-080, SC-031]
- **OQ-5 — MODULATION and SETTINGS.** Ruled: tabs of Advanced. [FR-013, FR-030, SC-031]
- **OQ-6 — Placement of IDs the roadmap list does not name.** Ruled: the spec default (Shape & Character, Mass, band sections).
  [FR-004, SC-031]
- **OQ-7 — Anchor section labels.** Ruled: MORPH BEHAVIOUR (1000) on Main inside the Anchor group; ANCHOR MODE (301) on Advanced
  only. [FR-003, FR-012, FR-045, SC-005, SC-031]
- **OQ-8 — Endpoint name and descriptor.** Ruled: typed name persisted in state v3; descriptor derived, never stored. [FR-040,
  FR-047, FR-048, FR-096, SC-004, SC-027]
- **OQ-9 — MIDI learn.** Ruled: host-level only; no `IMidiLearn`. [FR-098, SC-029]
- **OQ-10 — Scaling and host resizing.** Ruled: one designed size with zoom steps 100 / 125 / 150 / 200 %, UI-only control,
  session state. [FR-010, FR-014, SC-019, SC-032]

---

## Traceability (roadmap statement → FR / SC)

| Roadmap (line) | FR | SC |
|---|---|---|
| Two stages, 12b after ratification (839–840) | FR-001, FR-002 | SC-001 |
| Concept §14 as the reference (841) | FR-003 | SC-001 |
| Design principle, Main + Advanced (843–849) | FR-003, FR-004, FR-020, FR-030 | SC-005, SC-017 |
| D1–D7 (850–870) | FR-003 – FR-010 | SC-001, SC-006, SC-019 |
| Main page makes a good sound alone (863) | FR-006 | SC-001, SC-006 |
| Main-page questions (872–884) | OQ-1, OQ-2, OQ-4, FR-070, FR-080 | SC-010, SC-014 |
| Morph component (886–891) | FR-007, FR-040 – FR-046 | SC-007, SC-008 |
| Motion (892–894) | FR-050 – FR-052 | SC-002, SC-005, SC-019 |
| Macro discoverability (895–896) | FR-060 | SC-009 |
| Anchor naming (897–900) | FR-003, FR-045, OQ-7 | SC-005 |
| Interaction principles (901–907) | FR-020 – FR-025 | SC-005, SC-010, SC-019, SC-024 |
| Not adopted (889–890, 909–913) | FR-093 | SC-005 |
| VSTGUI only, spectrum implementation, no type swaps (915–916) | FR-030, FR-032, FR-034, FR-070 – FR-074, OQ-3 | SC-004, SC-010 – SC-013, SC-023 – SC-026 |
| Phase 11 handoff 12.1–12.4 (827–835) | FR-035, FR-036, FR-041 – FR-043, FR-046, FR-090 | SC-007, SC-015, SC-018 |
| Phase 7 handoff §4 | FR-024, FR-051, FR-080 | SC-005, SC-014 |
| Phase 8 handoff 12.1–12.4 | FR-007, FR-044, FR-045 | SC-008 |
| Phase 9 handoff 12.1–12.4 | FR-005, FR-052, FR-097 | SC-001, SC-005, SC-009 |
| Editor lifecycle (Phase 11 handoff 12.1) | FR-090 – FR-092 | SC-015, SC-016 |
| Cross-cutting (RT, portability, CPU, change-set lanes; 1066–1089) | FR-071, FR-094 – FR-096 | SC-012, SC-013, SC-020 – SC-022 |
| Main-page set, Advanced placement and tabs, zoom (848–849, 872–884, 870) | FR-004, FR-010 – FR-014, FR-045 | SC-005, SC-019, SC-031, SC-032 |
| Endpoint names and descriptors (886–888) | FR-040, FR-047 – FR-049, FR-096 | SC-004, SC-007, SC-027 |
| Macro offsets on knobs (895–896) | FR-061 | SC-028 |
| Spectrum measurement, capacity, transport (878–881) | FR-025, FR-070, FR-075, FR-076 | SC-010, SC-016, SC-022, SC-023, SC-030 |
| Host-level MIDI learn (868–869) | FR-098 | SC-029 |
| Main-page sub-band bars (SC-006 (c)) | FR-006, FR-024 | SC-006 |

---

## Review notes

- **Inert macro row surfaced.** The `kRows` row `DarkBright → Shape` ships with amount 0 (`profundum_macro_matrix.h:81`, ruling
  B10-2). FR-060 keeps the UI honest about it, and D3 must show the row as dropped rather than hide it.
- **ADSRDisplay law mismatch.** The shared `ADSRDisplay` emits cubic-normalised times, but Profundum's amp times are
  logarithmic. FR-034 makes the conversion an invariant, so reusing the display cannot silently mis-set 420, 421 or 423.
- **The engine exposes no spectrum.** `ProfundumCore::deliveredGains()` exists, but nothing in `ProfundumEngine`'s public API
  reaches a voice or the resolved state. OQ-3 decides whether Phase 12 appends an accessor, and FR-095 fences that change.
- **Phase 9 listening verdicts** (Phase 9 SC-035) are still owed by the user. FR-097 and SC-001 now carry the obligation:
  before 12b each of the eight is recorded as given or listed as pending, and an agent never fills them.
- **Review round 1 (2026-10-23).** All issues applied. Notes on how:
  - *SPREAD curve (SC-014 / FR-080).* Accepted that `widthCurve` ignores mode, Spread and Motion depth (they live only in
    `magnitudeScale_`, `bass_stereo_field.h:655–657`), so the drawn quantity is now c(f) · m. **Rejected in part** the claim
    that Gravity cannot change `widthCurve`: `updateLawCache` sets `uLow_` from `applied_.gravity` (:649–650), and `lawCurve`
    reads `uLow_` (:636). The Gravity axis therefore stays in the SC-014 grid and is asserted live.
  - *SC-022.* The two issues on SC-022 (testability and reality lenses) are one defect and are resolved together: per-option
    references, neither of which claims an observer field that does not exist.
  - *OQ-3 option (a).* Not deleted from the table, because the user may still rule it, but it is no longer a recommendation or
    a fallback. It is admissible only as a ruled roadmap deviation (FR-025), and SC-010's fold fixture fails it otherwise.
  - *SC-015.* The ±50 % period check over pumped ticks was replaced by headless checks (configured interval, one request per
    invoked tick, the synchronous notify → flush path). This does not relax the requirement: the period was never measurable
    headless (Vorago R-5), and the ≤ 100 ms bound is still asserted on the configured interval.
  - *Controller citations.* Corrected to `controller.h:81, :83, :104, :105` and the range `:49–114`; the earlier numbers were
    wrong.

