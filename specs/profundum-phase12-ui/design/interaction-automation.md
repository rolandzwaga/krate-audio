# D6 — Interaction and Automation

**Status: RATIFIED by the user, 2026-10-23 (spec Clarifications "Session 2026-10-23 (12a ratification)").**

**Deliverable:** D6 of Phase 12 stage 12a (FR-002). **Carries:** FR-009, FR-041 – FR-043, FR-047, FR-049, FR-098.
**Sources:** `specs/profundum-phase12-ui/plan.md` §3.5 (D6 sections), §4.5, §4.6.3, §4.6.9, §4.7 (session-tag table);
`specs/profundum-phase12-ui/spec.md` Clarifications (CQ-4, OQ-9, OQ-10 / CQ-7, plan-stage ruling P-4).

This document adds no behaviour. It classifies every control that D1 (`design/main-page.md`) and D2
(`design/advanced-page.md`) place, and states the rules that 12b implements and the Phase 12 tests check. Each claim about code
that exists today cites the `file:line` it was read from (phase base `40f6ebca`). Each claim about code that 12b adds cites
the plan section that specifies it.

---

## 1. Control classification

A control is one of two kinds:

- **host parameter (ID)**: it is bound to a registered `ParamID` below 9000. The host sees the parameter, can save it in the
  project, and automates it unless the row says otherwise.
- **UI-only (session tag)**: its tag is 9000 or higher (`kSessionTagBase = 9000`, plan §4.7; the highest registered ID is 1002,
  `plugins/profundum/src/plugin_ids.h:204`). The host never sees it. `ProfundumPanelSubController` handles its value and
  swallows its begin and end edits (plan §4.7, "`valueChanged` for session tags").

### 1.1 UI-only controls (the complete list)

These 13 tags match plan §4.7 exactly. T027 copies this list into `kD6UiOnlyTags` (`ui_strings_test_table.h`).
`Profundum_UiPlacement` (f) asserts two things: every tagged `CControl` in the built tree is bound to an ID below 9000 or
carries one of these tags, and every tag on this list is present in the tree (plan §3.5, "Drift guard").

<!-- D6-UI-ONLY-BEGIN -->
| Session tag | Control | Page / place | Action (plan §4.7) | Persisted? |
|---|---|---|---|---|
| 9100 | page strip MAIN / ADVANCED | header (D1) | `setVisible` on the `page-main` / `page-advanced` containers; controller `activePage_` | no (session) |
| 9110 | Advanced tab strip SECTIONS / MODULATION / SETTINGS | Advanced page (D2) | `setVisible` on the three tab containers; controller `activeTab_` | no (session) |
| 9200 | compare segment A / A·B / B (index 0 → A, 1 → A·B, 2 → B) | Main, `morph-band` | `owner->pressCompare(mode)`, then every compare segment is reset to `compareMode()` | no (§2) |
| 9201 | Copy A → B | Main, `morph-band` | `owner->copyEndpoint(A)` | no; the result is endpoint state (§2.3) |
| 9202 | Copy B → A | Main, `morph-band` | `owner->copyEndpoint(B)` | no; the result is endpoint state (§2.3) |
| 9203 | compare segment A / A·B / B (same index mapping as 9200) | Advanced, `section-morph-behaviour` | as 9200 | no (§2) |
| 9204 | Copy A → B | Advanced, `section-morph-behaviour` | as 9201 | as 9201 |
| 9205 | Copy B → A | Advanced, `section-morph-behaviour` | as 9202 | as 9202 |
| 9210 | endpoint name A (`CTextEdit`) | Main, `morph-band` | on commit, `owner->setEndpointName(A, text)` | **yes**: the name is in state v3 (§5.2) |
| 9211 | endpoint name B (`CTextEdit`) | Main, `morph-band` | on commit, `owner->setEndpointName(B, text)` | **yes**: the name is in state v3 (§5.2) |
| 9220 | caption label A (name and three-word descriptor) | Main, `morph-band` | display only (plan §4.6.6) | no; the descriptor is derived (FR-048) |
| 9221 | caption label B (name and three-word descriptor) | Main, `morph-band` | display only (plan §4.6.6) | no; the descriptor is derived (FR-048) |
| 9300 | zoom segment 100 / 125 / 150 / 200 % | header (D1) and the SETTINGS tab (D2) | `owner->setZoomStep(i)` (plan §4.6.9) | no (session) |
<!-- D6-UI-ONLY-END -->

Notes:

- The name controls 9210 and 9211 are UI-only *controls*: they bind to no parameter, are not automatable, and are not
  host-visible. The *data* they edit, the endpoint name, is plugin state (FR-047; §5.2).
- Tag 9300 appears on two controls, one in the D1 header and one in the D2 SETTINGS tab (tasks.md T002 item 2, T003 item 1).
  Both call the same `setZoomStep`, and both show the one session `zoomIndex_` (plan §4.6.9). **For ratification:** the user
  can keep both or drop one of them. The tag list above does not change either way.
- These are not controls and carry no tag: the "Held at A" dimming label (FR-044), the MORPH BEHAVIOUR and ANCHOR MODE section
  labels (FR-012), the "no run loop" note (FR-076), and the custom views `HarmonicBarView` ×2 and `SpreadCurveView`. The custom
  views are display-only, bound to no ID, and fed by the frame or the controller (FR-025, FR-080).
- The macro highlight and the macro rings are not controls. They are UI-only *state* (§4).

### 1.2 Host-parameter controls

Every **visible** registered ID (84 minus the hidden 4 – 7, so 80 IDs) is bound to at least one control (FR-031). The table
below has one row per ID. It gives the page or section where D1 / D2 bind the ID (the FR-011 Main set and the FR-004 sections)
and the automation flag as registered today.

**Flag sources (read this session):**

- Continuous sound rows are `kCanAutomate` (`plugins/profundum/src/parameters/sound_param_row.h:329`).
- List sound rows are built by `createDropdownParameterWithDefault` (`sound_param_row.h:321–325`), and that helper sets
  `kCanAutomate | kIsList` (`plugins/shared/src/ui/parameter_helpers.h:107`; the initializer-list overloads at `:32` and
  `:57` set the same flags).
- Global 0 – 3: `plugins/profundum/src/parameters/global_params.h:136–152`.
- Macros 100 – 103: `macro_params.h:122–125`.
- Morph control 105, 1000, 1001 and 1002: `morph_params.h:252–270`.
- Note behaviour 410 – 417: `note_params.h:298–323`.

**The only visible ID that is not automatable is Edit Target 1001** (`morph_params.h:264`,
`editTarget->getInfo().flags = ParameterInfo::kIsList;  // FR-004: NOT kCanAutomate`).

| ID | Registered name | Main (D1) | Advanced (D2) section | Automatable |
|---|---|---|---|---|
| 0 | Output Gain | — | Stereo & Output (global band) | yes |
| 1 | Voice Mode | header | — | yes |
| 2 | Polyphony | — | Stereo & Output (global band) | yes |
| 3 | Output Ceiling | — | Stereo & Output (global band) | yes |
| 100 | Weight | THE MASS (macro) | — | yes |
| 101 | Dark/Bright | THE MASS (macro) | — | yes |
| 102 | Tight/Loose | — (Advanced-only, FR-011) | Core spectrum | yes |
| 103 | Clean/Violent | THE MASS (macro) | — | yes |
| 104 | Mass | THE MASS | — | yes |
| 105 | Morph Position | `morph-band` (`CSlider` and a display-only readout, P-8) | — | yes |
| 200 | Depth | THE MASS | Core spectrum | yes |
| 201 | Body | THE MASS | Core spectrum | yes |
| 202 | Body Curvature | — | Core spectrum | yes |
| 203 | Body Emphasis | — | Core spectrum | yes |
| 204 | Edge | THE MASS | Core spectrum | yes |
| 205 | Spectral Shift | — (Advanced-only, FR-011) | Core spectrum | yes |
| 206 | Excitation Mix | — | Excitation & Fold | yes |
| 207 | Excitation Drive | — | Excitation & Fold | yes |
| 208 | Excitation Asymmetry | — | Excitation & Fold | yes |
| 300 | Anchor Waveform | THE ANCHOR | Sub Anchor | yes |
| 301 | Anchor Mode | — (Advanced-only, FR-012) | Sub Anchor (label ANCHOR MODE) | yes |
| 302 | Anchor Level | THE ANCHOR | Sub Anchor | yes |
| 303 | Punch Boost | — | Sub Anchor | yes |
| 304 | Punch Time | — | Sub Anchor | yes |
| 305 | Ghost Time | — | Sub Anchor | yes |
| 306 | Anchor Fold Feed | — | Sub Anchor | yes |
| 400 | Drop Amount | THE MASS | Pitch & Envelopes | yes |
| 401 | Drop Time | — | Pitch & Envelopes | yes |
| 402 | Drop Shape | — | Pitch & Envelopes | yes |
| 403 | Drop Rebound | — | Pitch & Envelopes | yes |
| 410 | Mono Priority | — | Pitch & Envelopes | yes |
| 411 | Legato | — | Pitch & Envelopes | yes |
| 412 | Portamento | header (`ArcKnob` and a display-only readout, P-8) | Pitch & Envelopes | yes |
| 413 | Portamento Mode | — | Pitch & Envelopes | yes |
| 414 | Glide Mode | — | Pitch & Envelopes | yes |
| 415 | Note Memory | — | Pitch & Envelopes | yes |
| 416 | Note Retrigger | — | Pitch & Envelopes | yes |
| 417 | Pitch Bend Range | — | Pitch & Envelopes | yes |
| 420 | Attack | — | Pitch & Envelopes (`ADSRDisplay` and `ArcKnob`) | yes |
| 421 | Decay | — | Pitch & Envelopes (`ADSRDisplay` and `ArcKnob`) | yes |
| 422 | Sustain | — | Pitch & Envelopes (`ADSRDisplay` and `ArcKnob`) | yes |
| 423 | Release | — | Pitch & Envelopes (`ADSRDisplay` and `ArcKnob`) | yes |
| 424 | Attack Curve | — | Pitch & Envelopes (`ADSRDisplay` and `ArcKnob`) | yes |
| 425 | Decay Curve | — | Pitch & Envelopes (`ADSRDisplay` and `ArcKnob`) | yes |
| 426 | Release Curve | — | Pitch & Envelopes (`ADSRDisplay` and `ArcKnob`) | yes |
| 500 | Harmonic Fold | THE MASS | Core spectrum and Excitation & Fold | yes |
| 501 | Shape | — | Shape & Character | yes |
| 600 | Character | — | Shape & Character | yes |
| 601 | Character Amount | — | Shape & Character | yes |
| 602 | Character Tone | — | Shape & Character | yes |
| 700 | Transient Attack | — | Mass | yes |
| 701 | Transient Sustain | — | Mass | yes |
| 800 | Image Mode | — (no Main mirror, D1) | Stereo & Output | yes |
| 801 | Spread | — | Stereo & Output | yes |
| 802 | Gravity | — | Stereo & Output | yes |
| 803 | Crossover | — | Stereo & Output | yes |
| 804 | Curve | — | Stereo & Output | yes |
| 805 | Field Motion Depth | — | Stereo & Output | yes |
| 806 | Field Motion Rate | — | Stereo & Output | yes |
| 807 | Field Motion Source | — | Stereo & Output | yes |
| 808 | Field Sync | — | Stereo & Output | yes |
| 809 | Field Sync Rate | — | Stereo & Output | yes |
| 900 | Motion Source | THE MASS | MODULATION tab | yes |
| 901 | Motion Target | — | MODULATION tab | yes |
| 902 | Motion Amount | — | MODULATION tab | yes |
| 903 | Motion Depth | — | MODULATION tab | yes |
| 904 | Cycle Rate | — | MODULATION tab | yes |
| 905 | Cycle Shape | — | MODULATION tab | yes |
| 906 | Cycle Sync | — | MODULATION tab | yes |
| 907 | Cycle Sync Rate | — | MODULATION tab | yes |
| 908 | Cycle Phase | — | MODULATION tab | yes |
| 909 | Cycle Retrigger | — | MODULATION tab | yes |
| 910 | Cycle Fade-In | — | MODULATION tab | yes |
| 911 | Motion Polarity | — | MODULATION tab | yes |
| 912 | Drift Smoothness | — | MODULATION tab | yes |
| 913 | Env Attack | — | MODULATION tab | yes |
| 914 | Env Decay | — | MODULATION tab | yes |
| 1000 | Anchor Morph Mode (Hold / Blend / Yield) | THE ANCHOR (label MORPH BEHAVIOUR) | Sub Anchor and Morph behaviour | yes |
| 1001 | Edit Target (A / B) | `morph-band` | — | **no** (`kIsList` only) |
| 1002 | Morph Ramp Time ("Morph smoothing", D4) | — | Morph behaviour | yes |

- The Main set is the 17 IDs of FR-011: {1, 100, 101, 103, 104, 105, 200, 201, 204, 300, 302, 400, 412, 500, 900, 1000, 1001}.
- The hidden IDs 4 Pitch Bend, 5 Sustain Pedal, 6 Mod Wheel and 7 Channel Pressure are bound to no control (FR-031). They are
  `kIsHidden` (`performance_params.h:70–78`) and reach the plugin only through `IMidiMapping` (§3.2).
- The registered names in the table are the parameter titles, read from:
  - `global_params.h:136–151`;
  - `macro_params.h:119`;
  - `morph_params.h:51`, `:252`, `:256` and `:262–268`;
  - `core_params.h:24–32`, `anchor_params.h:32–40` and `note_params.h:64–74` and `:305–320`;
  - `fold_shape_params.h:23–24`, `character_params.h:30–33` and `dynamics_params.h:22–23`;
  - `field_params.h:47–59` and `motion_params.h:52–74`.

  Labels and tooltips on the page are D1 / D2's. They do not rename a parameter (FR-036).

---

## 2. Compare and Copy (FR-009, FR-041, FR-042, FR-049)

### 2.1 Compare and Copy are messages, not parameters

- Compare and Copy are UI-only messages (Phase 11 handoff 12.2, `specs/Profundum-roadmap.md:831–832`):
  - `kMsgCompare` = `"ProfundumCompare"`, controller → processor, int attribute `"mode"`
    (`plugins/profundum/src/plugin_ids.h:71`);
  - `kMsgCopyEndpoint` = `"ProfundumCopyEndpoint"`, controller → processor, int attribute `"from"` (`plugin_ids.h:70`).
- The compare modes are `CompareMode { AB = 0, A = 1, B = 2 }` (`morph_params.h:154`).
- **The compare writes no state.**
  - The processor holds the mode in an atomic (`processor.cpp:582`).
  - `getState` does not write it (`processor.cpp:537–562`).
  - `setState` resets it to A·B (`processor.cpp:524`), and so does `setActive(false)` (`processor.cpp:287–289`).
  - A reloaded project therefore always opens at A·B.

### 2.2 Pressing compare (FR-041, FR-049; plan §4.6.3)

The compare segments 9200 and 9203 call `Controller::pressCompare(m)`, which 12b adds:

| Press | `kMsgCompare` sent | Gesture on 1001 | Other parameters written |
|---|---|---|---|
| **A** | exactly one, `mode = 1` | exactly one `beginEdit(1001)` / `performEdit(1001, 0.0)` / `endEdit(1001)` (and `setParamNormalized(1001, 0.0)`) | none |
| **B** | exactly one, `mode = 2` | exactly one `beginEdit(1001)` / `performEdit(1001, 1.0)` / `endEdit(1001)` (and `setParamNormalized(1001, 1.0)`) | none |
| **A·B** | exactly one, `mode = 0` | none: 1001 is left unchanged | none |

- The message itself goes out through the existing `Controller::setCompare` (`controller.cpp:222–231`).
- The 1001 write is the FR-043 path. `setParamNormalized(1001, …)` runs the Phase 11 Edit Target hook
  (`controller.cpp:183–207`). The hook sends `kMsgRequestSync`, re-displays the 60 E-parameters from the selected mirror, and
  calls `restartComponent(kParamValuesChanged)` once.
- The hook comment "No beginEdit / performEdit / endEdit" (`controller.cpp:180–182`) is about the hook. The hook emits no
  gesture of its own. The one gesture of a compare press, or of an Edit Target click, comes from the UI entry point that sets
  1001.
- The 1001 value that a press writes is a parameter write. It is persisted in the morph-control block (`morph_params.h:300–305`:
  bytes 68 – 83, Edit Target i32). This is the only state a compare press changes, and it changes only for A and B.
- **The displayed compare state comes only from the controller's compare mode** (FR-041, FR-046):
  - the processor's `kMsgCompareMode` reply sets that mode (`controller.cpp:291–302`);
  - 12b adds the public getter `compareMode()` and one change notification per accepted message, `panel_->onCompareModeChanged`
    (plan §4.6.3);
  - right after a click, the panel resets every compare segment (9200 and 9203) to `compareMode()`, so a click alone changes
    nothing on screen (plan §4.7);
  - both segments always show the same mode.
- **Touching Morph Position 105 never changes the compare mode** and sends no `kMsgCompare` (FR-049; CQ-4,
  `spec.md:80–82`).
  - While the compare is A or B, the processor overrides the position it pushes to the engine: 0 for A, 1 for B
    (`processor.cpp:786–791`).
  - A 105 move is still stored and automated. It is heard once the compare returns to A·B.
- Gate: SC-007 (a) and (b), `Profundum_MorphComponentUi`.

### 2.3 Copy (FR-042)

- Copy A → B (9201 and 9204) calls `copyEndpoint(A)`. Copy B → A (9202 and 9205) calls `copyEndpoint(B)`.
- `copyEndpoint` sends one `kMsgCopyEndpoint` with `from` 0 or 1, then calls `setDirty(1u)` (`controller.cpp:210–219`).
- The UI does not edit the mirror. The processor applies the copy on the audio thread (`applyPendingCopy`,
  `processor.cpp:647–657`), marks the destination for a report, and the `kMsgEndpoint` reply re-displays it
  (`controller.cpp:265–290`).
- **Copy copies the sound, not the name.**
  - The processor's copy is `EndpointParams`-only (`processor.cpp:653–656`).
  - The user ruled on this question (plan §10 Open Question 4, "P-4"): "Does Copy copy the name? → No, sound only. [FR-042]"
    (`spec.md:115`).
  - Each endpoint keeps its own name.
- Gate: SC-007 (c).

---

## 3. Automation and MIDI learn (FR-009, FR-098)

### 3.1 Edit Target 1001 is registered and not automatable

- 1001 is a registered list "A, B" with flags `kIsList` only (`morph_params.h:262–266`; Phase 11 Q6,
  `specs/profundum-phase11-parameters/spec.md:48`).
- It is persisted in state, so a reloaded project edits the same endpoint (`morph_params.h:300–305`).
- A host does not offer it for automation. The UI changes it only through one begin / perform / end gesture: an Edit Target
  click (FR-043) or a compare A / B press (§2.2).
- When the host delivers it, the processor reads the last point of queue 1001 before every other queue
  (`processor.cpp:660`).

### 3.2 Macros and Morph Position are automatable; MIDI learn is host-level only

- The four macros 100 – 103 and Morph Position 105 are automatable host parameters. They carry `kCanAutomate` and not
  `kIsReadOnly` (`macro_params.h:122–125`, `morph_params.h:252–253`).
- **MIDI learn is host-level only** (OQ-9 ruling, `spec.md:100–101`; FR-098). A user maps a hardware controller to 100 – 103 or
  105 with the host's own MIDI-learn or remote-control feature, the same as any automatable parameter.
- The controller does **not** implement `IMidiLearn`. Its interface list holds `IMidiMapping` only (`controller.h:74–76`), and
  12b adds `IDataExchangeReceiver` and the editor delegate, never `IMidiLearn` (plan §4.6).
- The editor offers no MIDI-learn control, label or menu entry.
- **`IMidiMapping` is unchanged** from Phase 11 (FR-096, FR-098). On bus 0 and every channel it maps:
  - CC 1 → Mod Wheel 6;
  - CC 64 → Sustain Pedal 5;
  - channel pressure → Channel Pressure 7;
  - pitch bend → Pitch Bend 4.

  Anything else returns `kResultFalse` (`controller.cpp:309–329`). No macro or 105 mapping is added.
- Gate: SC-029, `Profundum_MidiLearnHostLevel`.

### 3.3 Every other control

- Every other visible ID is automatable as registered (§1.2). Phase 12 changes no flag, range, type or default (FR-096).
- A UI gesture on a host-parameter control produces the usual `beginEdit` / `performEdit` / `endEdit`.
- The custom views keep the registered law of their ID (FR-034). The `ADSRDisplay` writes 420 – 426 through the law adapter of
  plan §4.6.10.
- The UI-only controls of §1.1 produce **no** host gesture: the panel swallows their begin and end edits (plan §4.7).

---

## 4. Session UI state: never persisted (FR-009, FR-014)

The following are **session state**. They live in the controller or the panel for the life of the plugin instance. They are not
registered parameters, not in `getState` / `setState`, not sent to the processor, and bound to no parameter ID.

| State | Owner (plan) | Survives editor close / reopen? | Gate |
|---|---|---|---|
| page selection, Main or Advanced (9100) | controller `activePage_` (§4.7) | yes, within the session | SC-017: `getState` bytes are identical before and after page switches |
| Advanced tab selection (9110) | controller `activeTab_` (§4.7) | yes, within the session | SC-017 |
| zoom step, 100 / 125 / 150 / 200 % (9300) | controller `zoomIndex_`, `kZoomSteps{1.0, 1.25, 1.5, 2.0}` (§4.6.9); `didOpen` re-applies it (§4.6.2) | yes, within the session | SC-032: `getState` bytes are identical before and after zoom changes; the parameter count stays 84 |
| macro highlight (hover, or an open gesture on 100 – 103) | panel `setHighlight` (§4.7) | no: it clears on mouse exit or `endEdit` | SC-009 |

- Derived displays are recomputed and never stored: the macro rings (FR-061), the Lock dimming (FR-044), the endpoint
  descriptors (FR-048) and the SPREAD curve (FR-080).
- The compare mode is processor runtime state that is never persisted (§2.1). It is not session state of the UI either: the UI
  only mirrors it.
- A new plugin instance, or a reloaded project, opens on the Main page, the first Advanced tab and zoom 100 %. Nothing about
  them is saved.

---

## 5. Endpoint names: persisted, not session state (FR-047)

### 5.1 Editing

- The user types each endpoint's name in the `CTextEdit` 9210 (A) or 9211 (B) in the morph component.
- On commit, `Controller::setEndpointName(e, text)` (plan §4.5) does five things, in order:
  1. sanitises the text: control bytes and invalid UTF-8 are dropped, and the text is truncated at a code-point boundary to at
     most 31 bytes; an empty result means the default "A" / "B";
  2. stores the result for the UI;
  3. sends the one new controller → processor message `kMsgEndpointName` = `"ProfundumEndpointName"` (int `"endpoint"`, binary
     `"name"`, 0 – 31 bytes);
  4. calls `setDirty(1u)`;
  5. refreshes the captions 9220 and 9221.
- A name is not a parameter: it is not automatable, not MIDI-learnable, and not host-visible.

### 5.2 Persistence

- The names are plugin state in **state v3** (FR-047, FR-096; plan §4.5): `kCurrentStateVersion = 3`, `kStateV3Bytes = 574`.

  | Bytes | Content |
  |---|---|
  | 0 – 509 | the v2 body, unchanged except the version word |
  | 510 – 541 | name A: a u8 length (0 – 31), then the UTF-8 bytes, zero-padded |
  | 542 – 573 | name B: the same layout |

- v1 and v2 streams still load, and their names default to "A" and "B".
- Presets therefore carry names, and a reopened editor shows the saved names.
- The three-word descriptor beside each name is derived from the endpoint's settings and is **never** stored (FR-048).
- Gate: SC-027 (state v3 round trip, v1 / v2 defaults, the sanitiser, the descriptor table) and SC-004 (P-2 ruling,
  `spec.md:112`).

---

## 6. Summary of the rules (what 12b must not do)

- No compare press writes any parameter other than 1001, and A·B writes none (FR-041, FR-049).
- No click on a compare segment changes its displayed state by itself (FR-041).
- Moving 105 never sends `kMsgCompare` (FR-049).
- Copy never copies a name (FR-042, P-4).
- 1001 never gains `kCanAutomate` (Phase 11 Q6), and no registered parameter changes (FR-096).
- No `IMidiLearn`, no MIDI-learn control, and no change to `IMidiMapping` (FR-098).
- Page, tab, zoom and highlight are never written to state (FR-009, FR-014; SC-017, SC-032).
- The UI-only tag set is exactly §1.1, and it equals `kD6UiOnlyTags` (plan §3.5, drift guard).
