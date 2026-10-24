# D1 — Main page

**Status: RATIFIED by the user, 2026-10-23 (spec Clarifications "Session 2026-10-23 (12a ratification)").**

**Deliverable:** D1 of FR-002 (`specs/profundum-phase12-ui/spec.md`, FR-002). **Requirements carried:** FR-003, FR-006, FR-011,
FR-012, FR-014, FR-020, FR-022, FR-080. **Plan source:** `plan.md` §3.1 "Designed size", "Main page (D1)", §3.3, §4.7
(session tags), §4.12 (class table). **Base:** `synth/profundum` at `40f6ebca`.

This document is a proposal. Nothing in it is ratified until the user's dated entry in the spec's Clarifications says so
(SC-001). Values the user changes at ratification are edited here, and 12b builds the ratified value (tasks.md "Ratified
values").

Every control in the wireframe is labelled with its registered parameter ID, or with **UI-only (session tag N)** using the
session-tag table of plan §4.7 (tags ≥ 9000; the highest registered ID is 1002, `plugins/profundum/src/plugin_ids.h:204`).

---

## 1. Designed size

- **960 × 600 px at 100 %** (plan §3.1 "Designed size"). At 200 % the window is 1920 × 1200 px.
- Zoom steps: **100 / 125 / 150 / 200 %** through `VSTGUI::VST3Editor::setZoomFactor` (FR-014; spec FR-014 cites
  `vst3editor.h:127`), chosen by the UI-only zoom control (session tag 9300). No free resize: the template sets
  `minSize = maxSize = 960, 600` and the editor constrains its size to the zoomed designed size (plan §3.1).
- The Main page and the Advanced page share this one frame (plan §3.1).

## 2. Wireframe

Proposed geometry at 100 % (x, y, w, h in px; the editor's coordinate system, origin top-left). The ASCII drawing is to scale
horizontally at roughly 8.7 px per character and is a layout proposal, not a pixel specification; T027 places the controls
inside these rects and SC-019 gates overlap, hit target and font size.

| Region | Container (`uidesc-label`) | Rect (x, y, w, h) |
|---|---|---|
| Header | — (inside the root template) | 0, 0, 960, 56 |
| Main page | `page-main` | 0, 56, 960, 544 |
| Upper row, THE MASS | `mass-group` | 8, 64, 944, 316 |
| Lower row | `morph-anchor` | 8, 388, 944, 204 |
| — left child, THE ANCHOR | `anchor-group` | 8, 388, 300, 204 (relative to `page-main`: inside `morph-anchor`) |
| — right child, BASS MORPH | `morph-band` | 316, 388, 636, 204 |

```text
 0                                                                                                           960
 ┌──────────────────────────────────────────────────────────────────────────────────────────────────────────────┐ 0
 │ PROFUNDUM        VOICE [ MONO | POLY ]      GLIDE (◉)  [ 0.0 ms ]        [ MAIN | ADVANCED ]   ZOOM [100%▾] │
 │ (title text)     ID 1  two-segment          ID 412     ID 412 readout     UI-only (session     UI-only        │
 │                                             ArcKnob    CParamDisplay      tag 9100)            (session 9300) │
 ├══════════════════════════════════════════ page-main ═════════════════════════════════════════════════════════┤ 56
 │ ┌─ THE MASS ─────────────────────────────────────────────── mass-group ────────────────────────────────────┐ │
 │ │ ┌────────────────────────────────────────────────────────────┐   MACROS                                  │ │
 │ │ │ ANCHOR │ ▂ ▃ ▅ █ █ █ ▆ ▅ ▃ ▂ ▂ ▁ ▁                          │                                           │ │
 │ │ │  lane  │  SUB │   BODY   │        PRESENCE                  │   WEIGHT (◉)        ID 100  ArcKnob        │ │
 │ │ │   █    │                                                   │                                           │ │
 │ │ └────────────────────────────────────────────────────────────┘   DARK ──────●────── BRIGHT               │ │
 │ │  HarmonicBarView #1  (bar-variant="main"; display-only,          ID 101  BipolarSlider (neutral 0.5)     │ │
 │ │  binds no parameter; FR-070)                                                                             │ │
 │ │                                                                  CLEAN ─────●────── VIOLENT              │ │
 │ │  DEPTH    BODY     EDGE     FOLD     MASS     DROP                ID 103  BipolarSlider (neutral 0.5)    │ │
 │ │   (◉)      (◉)      (◉)      (◉)      (◉)      (◉)                                                       │ │
 │ │  ID 200   ID 201   ID 204   ID 500   ID 104   ID 400              MOTION [ OFF | ENV | CYCLE | DRIFT ]    │ │
 │ │  ArcKnob  ArcKnob  ArcKnob  ArcKnob  ArcKnob  ArcKnob             ID 900  four-segment                   │ │
 │ └──────────────────────────────────────────────────────────────────────────────────────────────────────────┘ │ 380
 │ ┌─ morph-anchor ───────────────────────────────────────────────────────────────────────────────────────────┐ │ 388
 │ │ ┌─ THE ANCHOR ── anchor-group ──┐ ┌─ BASS MORPH ── morph-band ─────────────────────────────────────────┐ │ │
 │ │ │                               │ │  [ name A......... ]                      [ name B......... ]      │ │ │
 │ │ │  LEVEL (◉)     ID 302 ArcKnob │ │  UI-only (session tag 9210)               UI-only (session 9211)   │ │ │
 │ │ │                               │ │  Round · Warm · Clean                     Dense · Bright · Driven  │ │ │
 │ │ │  CHARACTER [ Sine        ▾ ]  │ │  UI-only (session tag 9220)               UI-only (session 9221)   │ │ │
 │ │ │            ID 300 COptionMenu │ │                                                                    │ │ │
 │ │ │                               │ │  A ━━━━━━━━━━━━━━━━●━━━━━━━━━━━━━━━━━━━ B        [ 0% ]            │ │ │
 │ │ │  MORPH BEHAVIOUR              │ │  ID 105  CSlider (writes 105)                    ID 105 readout    │ │ │
 │ │ │  (CTextLabel)                 │ │                                                  CParamDisplay     │ │ │
 │ │ │  [ HOLD | BLEND | YIELD ]     │ │  COMPARE [ A | A·B | B ]   [ COPY A → B ] [ COPY B → A ]           │ │ │
 │ │ │  ID 1000  three-segment       │ │  UI-only (session 9200)    UI-only (9201)  UI-only (9202)          │ │ │
 │ │ │                               │ │                                                                    │ │ │
 │ │ │                               │ │  EDIT [ A | B ]   ID 1001  two-segment                             │ │ │
 │ │ └───────────────────────────────┘ └────────────────────────────────────────────────────────────────────┘ │ │
 │ └──────────────────────────────────────────────────────────────────────────────────────────────────────────┘ │
 └──────────────────────────────────────────────────────────────────────────────────────────────────────────────┘ 600
```

Notes on the drawing:

- **Header** (plan §3.1): title text PROFUNDUM; **no preset field in 12b** (plan P-9 / OQ-7; Appendix A not built); voice mode 1
  two-segment; glide 412 as an `ArcKnob` that writes 412 plus a display-only `CParamDisplay` bound to 412 beside it, whose unit
  follows Glide Mode 414 (FR-035; the controller formats 412 from the current 414,
  `plugins/profundum/src/controller/controller.cpp:165–167`); page strip MAIN / ADVANCED, UI-only session tag 9100; zoom,
  UI-only session tag 9300.
- **Upper row, THE MASS** (`uidesc-label="mass-group"`): `HarmonicBarView` #1 with `bar-variant="main"` and the ANCHOR lane
  left of the Mass bars (the drawing rules are D2's, `design/advanced-page.md`, "Spectrum drawing"; FR-070). Below it the six
  `ArcKnob`s Depth 200, Body 201, Edge 204, Fold 500, Mass 104, Drop 400. Macros: Weight 100 (`ArcKnob`), Dark ↔ Bright 101
  and Clean ↔ Violent 103 (`BipolarSlider`, neutral 0.5 = `ProfundumMacroMatrix::kNeutral`,
  `dsp/include/krate/dsp/systems/profundum_macro_matrix.h:67`). Motion source 900 as a four-segment control (Off / Env / Cycle /
  Drift, `plugins/profundum/src/parameters/motion_params.h:37–38`).
- **Lower row** (`uidesc-label="morph-anchor"`) holds **exactly two** child containers side by side:
  - `anchor-group` (THE ANCHOR): level 302 (`ArcKnob`), character 300 (`COptionMenu`, five entries Sine / Triangle / Rounded
    Sine / Asymmetric Sine / Octave Down, `plugins/profundum/src/parameters/anchor_params.h:24–25`), and the `CTextLabel`
    **MORPH BEHAVIOUR** above the 1000-bound three-segment control (Hold / Blend / Yield,
    `plugins/profundum/src/parameters/morph_params.h:179–180`) (FR-012, SC-005);
  - `morph-band` (BASS MORPH, the morph component of FR-040): A name (UI-only 9210) and its descriptor caption (UI-only 9220);
    the 105 `CSlider` (writes 105) and the display-only 105 readout (`CParamDisplay`, registered string `"%.0f%%"`,
    `morph_params.h:280–284`); B name (UI-only 9211) and caption (UI-only 9221); compare A / A·B / B (UI-only 9200); Copy A → B
    (UI-only 9201) and Copy B → A (UI-only 9202); Edit Target 1001 two-segment (A / B, `morph_params.h:181`).
- The descriptor captions in the drawing ("Round · Warm · Clean", "Dense · Bright · Driven") are placeholders that show the
  three-word form; the words come from D2's endpoint descriptor table (FR-048) and are computed, never stored.
- Image-mode 800 is **not** drawn: see §5.

## 3. Control table

One row per Main control. "Label" is the visible text on the page (a `CTextLabel` beside or above the control, or the
segment / menu strings, which are the registered strings, FR-036). "Tooltip" is the exact `tooltip="…"` attribute T027 copies;
every bound control has one (SC-019; D7). Ranges and defaults are the registered ones, cited for the FR-006 render set (§8).

### 3.1 Bound to a registered ID (the FR-011 set)

| ID | Class | Label | Tooltip | Registered range / default (source) |
|---|---|---|---|---|
| 1 | `CSegmentButton`, two segments | VOICE · MONO / POLY | `Mono plays one note at a time, with glide and legato. Poly plays chords; the voice count is on the Advanced page.` | list Mono / Poly, default Mono (`plugins/profundum/src/parameters/global_params.h:50, :139–141`) |
| 100 | `ArcKnob` | WEIGHT | `Weight: more depth, more body and a louder anchor together. Centre is neutral.` | 0–100 %, default 0.5 (`plugins/profundum/src/parameters/macro_params.h:118–127`; `kNeutral` `profundum_macro_matrix.h:67`) |
| 101 | `BipolarSlider` (neutral 0.5) | DARK ↔ BRIGHT | `Dark ↔ Bright: moves the edge, the spectral shift and the character tone together. Centre is neutral.` | 0–100 %, default 0.5 (as 100) |
| 103 | `BipolarSlider` (neutral 0.5) | CLEAN ↔ VIOLENT | `Clean ↔ Violent: more drive, more fold and more character together. Centre is neutral.` | 0–100 %, default 0.5 (as 100) |
| 104 | `ArcKnob` | MASS | `Mass: how heavy the note feels. Above 50 heavier, below 50 lighter; 50 is neutral.` | 0–100, default 50 (`dsp/include/krate/dsp/systems/mass_law.h:72–74`; row `morph_params.h:51`) |
| 105 | `CSlider` (writes 105) + display-only `CParamDisplay` (P-8) | A … B (slider ends); readout shows the registered percent | `Bass Morph: moves the sound from endpoint A to endpoint B.` (both controls) | 0–100 %, default 0 (`morph_params.h:183, :252–253`) |
| 200 | `ArcKnob` | DEPTH | `Depth: high keeps the energy in the fundamental; low spreads it up through the harmonics.` | 0–1 (shown %), default 0.5 (`morph_state.h:160–161`; `dsp/include/krate/dsp/processors/spectral_shape_recipe.h:240–241`) |
| 201 | `ArcKnob` | BODY | `Body: the weight of the low harmonics that make the note sound full.` | 0–1 (shown %), default 0.5 (`morph_state.h:162–163`; `spectral_shape_recipe.h:241`) |
| 204 | `ArcKnob` | EDGE | `Edge: the bite of the upper harmonics.` | 0–1 (shown %), default 0.5 (`morph_state.h:168–169`; `spectral_shape_recipe.h:241`) |
| 300 | `COptionMenu` | CHARACTER | `Anchor Waveform: the shape of the sub tone under the mass.` | list of 5, default Sine (`anchor_params.h:24–25, :32–33`; `dsp/include/krate/dsp/systems/anchor_oscillator.h:57, :100`) |
| 302 | `ArcKnob` | LEVEL | `Anchor Level: how loud the stable sub tone under the mass is.` | 0–1 (shown %), default 0.5 (`anchor_params.h:36`; `anchor_oscillator.h:72`; `morph_state.h:303–304`) |
| 400 | `ArcKnob` | DROP | `Drop: each note starts this many semitones above its pitch and falls onto it.` | 0–48 st, default 0 (`note_params.h:64`; `dsp/include/krate/dsp/processors/drop_envelope.h:31, :36`) |
| 412 | `ArcKnob` (writes 412) + display-only `CParamDisplay` (P-8) | GLIDE | `Glide: time to slide between notes in Mono. The readout unit follows Glide Mode on the Advanced page.` (both controls) | 0–10000 ms, offset-log law on (t + 1 ms), default 0 ms (`note_params.h:226–231, :308–309`) |
| 500 | `ArcKnob` | FOLD | `Harmonic Fold: folds the sound back on itself to grow new harmonics.` | 0–100, default 0 (`fold_shape_params.h:23`; `morph_state.h:105, :180–181`) |
| 900 | `CSegmentButton`, four segments | MOTION · OFF / ENV / CYCLE / DRIFT | `Motion: what moves the sound over time. Its target, amount and timing are on the Advanced page.` | list Off / Env / Cycle / Drift, default Off (`motion_params.h:37–38, :52–53`; `morph_state.h:82, :85`) |
| 1000 | `CSegmentButton`, three segments, under the `CTextLabel` MORPH BEHAVIOUR | MORPH BEHAVIOUR · HOLD / BLEND / YIELD | `Morph Behaviour: how the anchor follows the Bass Morph. Hold keeps A's anchor, Blend follows the morph, Yield hands over late.` | list Hold / Blend / Yield, default Blend (`morph_params.h:179–180, :185–188`) |
| 1001 | `CSegmentButton`, two segments | EDIT · A / B | `Edit Target: which endpoint the knobs edit. Not automatable.` | list A / B, default A (`morph_params.h:181, :187, :262–265`) |

The bound-ID set of the Main page is exactly the FR-011 set:

**{1, 100, 101, 103, 104, 105, 200, 201, 204, 300, 302, 400, 412, 500, 900, 1000, 1001}** — 17 IDs (spec FR-011; OQ-1 ruling).

**205, 102 and 301 are not on the Main page.** Spectral Shift 205 and Tight ↔ Loose 102 are Advanced-only (FR-011); Anchor
Mode 301 is Advanced-only under the label ANCHOR MODE (FR-012). No ID in 901 – 914 is bound on Main (SC-019, FR-050). Image
mode 800 is not mirrored (§5), so SC-031 (a) expects exactly these 17.

The 1000 tooltip's verbs (Hold keeps A's anchor, Blend follows the morph, Yield hands over late) describe the Anchor curves
c = 0, c = p, c = p³ (`dsp/include/krate/dsp/systems/bass_morph_engine.h:72` `kPunchExponent = 3`; spec FR-007); D4
(`design/morph-laws.md`) is the authority for those laws and may refine the wording at ratification.

### 3.2 UI-only controls on the Main page (session tags, plan §4.7)

| Session tag | Class | Label | Tooltip |
|---|---|---|---|
| 9100 | `CSegmentButton`, two segments | MAIN / ADVANCED | `Switch between the Main and Advanced pages.` |
| 9200 | `CSegmentButton`, three segments | COMPARE · A / A·B / B | `Compare: hear endpoint A, the morph, or endpoint B. Pressing A or B also sets Edit Target.` |
| 9201 | `CTextButton` | COPY A → B | `Copy endpoint A's sound onto B. The name is not copied.` |
| 9202 | `CTextButton` | COPY B → A | `Copy endpoint B's sound onto A. The name is not copied.` |
| 9210 | `CTextEdit` | (shows name A) | `Name of endpoint A. Saved with the preset.` |
| 9211 | `CTextEdit` | (shows name B) | `Name of endpoint B. Saved with the preset.` |
| 9220 | `CTextLabel` | (descriptor of A) | `How endpoint A sounds: shape, colour and force.` |
| 9221 | `CTextLabel` | (descriptor of B) | `How endpoint B sounds: shape, colour and force.` |
| 9300 | `CSegmentButton` or `COptionMenu`, four entries | ZOOM · 100% / 125% / 150% / 200% | `Editor zoom.` |

The compare tooltip follows CQ-4 (spec Clarifications: A or B also switches Edit Target 1001; A·B leaves it unchanged; FR-049).
The Copy tooltips follow plan OQ-4 (copy the sound only, not the name); if the user rules names are copied, the sentence
"The name is not copied." is removed. The `HarmonicBarView` #1 is a display-only custom view and binds no parameter (FR-070);
its tooltip is `Measured harmonics of the sound you hear, with the anchor in its own lane.`

## 4. FR-022 container rule (ruled here for D1)

Verbatim from plan §3.1:

> **FR-022 container, ruled here for D1.** The container FR-022 and SC-019 mean is the nearest ancestor of a 105-bound control
> that carries `uidesc-label="morph-anchor"`. SC-019 asserts that this ancestor exists for every 105-bound control, and that a
> 1000-bound control descends from it (§6.2). SC-005's container is `anchor-group`, a child of `morph-anchor`, so the one 1000
> control on Main satisfies both.

On Main both 105-bound controls (the `CSlider` and its `CParamDisplay` readout) sit in `morph-band`, a child of
`morph-anchor`; the one 1000-bound control sits in `anchor-group`, the other child of `morph-anchor`, together with the
`CTextLabel` MORPH BEHAVIOUR. `morph-anchor` holds exactly these two children.

## 5. Image-mode mirror on Main: **no**

Proposed: image mode 800 is **not** mirrored on the Main page (FR-080 allows the mirror only if the FR-011 layout leaves room;
D1 states yes or no). Reason (plan §3.1): the lower row is already full, and SOLID / SPREAD / WIDE shown without the SPREAD
curve invites a misreading of mono safety. The selector and the SPREAD mini-spectrum live only in the Advanced Stereo & Output
section (FR-080, OQ-4 ruling). **SC-031 (a) follows this line:** the Main bound-ID set is the 17 IDs of §3.1 and does not
include 800.

## 6. Forbidden-word ruling (FR-020)

No Main-page label or tooltip contains oscillator, filter, LFO or partial; no word is ruled in.

(If the user rules a word in at ratification, it is listed here.) Every string of §3.1, §3.2 and the `HarmonicBarView` #1
tooltip of §3.2 was written against this rule; SC-005 checks it case-insensitively on the built tree. The registered display
strings shown on Main (Mono / Poly, Off / Env / Cycle / Drift, Hold / Blend / Yield, A / B, the five Anchor Waveform names, the
percent / ms / st readouts) contain none of the four words.

## 7. Concept §14 delta table (FR-003)

Source: `specs/Profundum-concept.md:647–678` (§14 "The UI I'd actually build", the Main screen drawing at lines 651–674).
Exactly the 11 rows of plan §3.1; the verdicts and reasons are the plan's proposal, ratified or changed by the user together
with D1 (SC-001).

| # | §14 element | Verdict | Where in D1 | Reason |
|---|---|---|---|---|
| 1 | BASS title | replaced | header title PROFUNDUM | the product name; both roadmap wireframes title the header PROFUNDUM (`specs/Profundum-roadmap.md:927, :955`) |
| 2 | Spectrum centrepiece | kept | `HarmonicBarView` #1, upper row | FR-070: measured harmonic bars with an Anchor lane, never decoration (FR-025) |
| 3 | Weight knob | kept | macro Weight 100 | FR-003 macros |
| 4 | Body knob | kept | Body 201 | FR-003 "shape the mass" |
| 5 | Edge knob | kept | Edge 204 | FR-003 "shape the mass" |
| 6 | Mass knob | kept | Mass 104 | FR-003 "shape the mass" |
| 7 | Anchor knob | replaced | THE ANCHOR group: level 302, character 300, MORPH BEHAVIOUR 1000 | Concept B's Anchor region; one knob cannot carry level, waveform and morph behaviour (FR-003, FR-012) |
| 8 | Shift knob | moved to Advanced | Spectral Shift 205, Core spectrum section | FR-011 makes 205 Advanced-only; on Main, Dark ↔ Bright 101 drives it (`kRows` RecipeShift row, `dsp/include/krate/dsp/systems/profundum_macro_matrix.h:80`) |
| 9 | Fold knob | kept | Fold 500 | FR-003 "shape the mass" |
| 10 | Drop knob | kept | Drop 400 | FR-003 "shape the mass" |
| 11 | CLEAN ─ MORPH ─ VIOLENT bar | replaced | Clean ↔ Violent 103 (macros) and the BASS MORPH component with 105 (lower row) | the bar merged a macro axis with the A ↔ B morph; FR-003 names them as two controls (macro 103; Bass Morph 105 with its component) |

## 8. FR-006 render set (plan §3.3)

T010 (`Profundum_MainPageRenderSet`, `[.listen]`) and T031 (`Profundum_MainPageSanity`, SC-006 (c)) read this list.

### 8.1 Common settings

- **Base:** the default patch — the `Processor` defaults with **every Advanced-only ID at its registered default**. Only the
  Main controls of §3.1 are varied (FR-006; SC-006 (b)).
- **Notes:** E1 (MIDI 28) and C3 (MIDI 48), each in its own render (tasks.md T010 item 1).
- **Format:** 48 kHz, 3 s held + 1 s tail, 32-bit float stereo WAV plus `manifest.txt`, in
  `specs/profundum-phase12-ui/evidence/listen/` (plan §3.3). File names `<id>_<min|def|max|patch>_<E1|C3>.wav` (T010).
- **Write order inside a render:** Edit Target 1001 first, then every other value. The processor applies 1001 in a first pass
  and routes the sound-row IDs (104, 200, 201, 204, 300, 302, 400, 500, 900) to the endpoint it selects
  (`plugins/profundum/src/processor/processor.cpp:672–682, :719–720`); the macros 100 – 103, 1, 412, 105 and 1000 are not
  endpoint-scoped (`plugins/profundum/src/parameters/sound_param_table.h:87–92`).

### 8.2 Single-control renders (min, default, max)

One render per Main control at its registered min, default and max, every other Main control at its default. For the two
two-value lists, Edit Target 1001 and voice mode 1, both list values are rendered instead.

| ID | Control | min | default | max |
|---|---|---|---|---|
| 1 | Voice mode | Mono | — | Poly *(both list values)* |
| 100 | Weight | 0.0 | 0.5 | 1.0 |
| 101 | Dark ↔ Bright | 0.0 | 0.5 | 1.0 |
| 103 | Clean ↔ Violent | 0.0 | 0.5 | 1.0 |
| 104 | Mass | 0 | 50 | 100 |
| 105 | Bass Morph | 0 % | 0 % | 100 % |
| 200 | Depth | 0 % | 50 % | 100 % |
| 201 | Body | 0 % | 50 % | 100 % |
| 204 | Edge | 0 % | 50 % | 100 % |
| 300 | Anchor character | Sine | Sine | Octave Down |
| 302 | Anchor level | 0 % | 50 % | 100 % |
| 400 | Drop | 0 st | 0 st | 48 st |
| 412 | Glide | 0 ms | 0 ms | 10000 ms |
| 500 | Fold | 0 | 0 | 100 |
| 900 | Motion source | Off | Off | Drift |
| 1000 | Morph behaviour | Hold | Blend | Yield |
| 1001 | Edit Target | A | — | B *(both list values)* |

Where min equals default (105, 300, 400, 412, 500, 900) the two renders are the same patch; both files are still written so
the manifest has one row per (ID, min/def/max) and T031 iterates a regular grid.

### 8.3 Four combined "patch" renders

Each lists every Main control's value. Values not named by the plan's proposal are at their registered default (§8.2
"default" column).

| ID | *Deep sub* | *Round bass* | *Growl* | *Morph and motion* |
|---|---|---|---|---|
| 1 Voice mode | Mono | Mono | Mono | Mono |
| 100 Weight | **0.8** | 0.5 | 0.5 | 0.5 |
| 101 Dark ↔ Bright | 0.5 | 0.5 | 0.5 | 0.5 |
| 103 Clean ↔ Violent | **0.3** | 0.5 | **0.8** | 0.5 |
| 104 Mass | **30** | **50** | **70** | 50 |
| 105 Bass Morph | 0 % | 0 % | 0 % | **50 %** |
| 200 Depth | 50 % | 50 % | 50 % | 50 % |
| 201 Body | 50 % | **70 %** | 50 % | 50 % |
| 204 Edge | 50 % | **30 %** | **70 %** | 50 % |
| 300 Anchor character | Sine | Sine | Sine | Sine |
| 302 Anchor level | 50 % | 50 % | 50 % | 50 % |
| 400 Drop | 0 st | 0 st | 0 st | 0 st |
| 412 Glide | 0 ms | 0 ms | 0 ms | 0 ms |
| 500 Fold | **0** | 0 | **60** | 0 |
| 900 Motion source | Off | Off | Off | **Cycle** |
| 1000 Morph behaviour | Blend | Blend | Blend | Blend |
| 1001 Edit Target | A | A | A | A |

Bold = the plan §3.3 / tasks.md T002 item 8 proposal.

### 8.4 Renders expected to equal the base (for the user's attention at ratification)

These follow from the registered defaults; they are recorded so the listening verdict (SC-006 (b)) is not read as a failure
of the control, and so the user can rule a change before T010 runs. **None of these is changed in this proposal.**

1. **105, 1000 and 1001 alone, and *Morph and motion*'s morph.** In the default patch endpoints A and B hold the same sound
   (both start from `MorphState{}`; `plugins/profundum/src/parameters/sound_param_row.h:113` `kDefaultLeafBits`), so moving
   Bass Morph 105, changing Morph Behaviour 1000 or switching Edit Target 1001 changes nothing audible. *Proposal for the
   user:* give *Morph and motion* a B endpoint by writing 1001 = B, then B's values (for example the *Growl* column's Edge 70 %,
   Fold 60, Mass 70), then 1001 = A, then 105 = 50 %; and render 105 and 1000 against that same A / B pair. This uses only Main
   controls, so FR-006's "only D1's main-page controls varied" still holds.
2. **900 alone, and *Morph and motion*'s Motion = Cycle.** The registered default Motion Amount is 0
   (`dsp/include/krate/dsp/systems/morph_state.h:86`, `float amount = 0.0f`), and Amount 902 is Advanced-only (FR-050: Main
   binds 900 and no ID in 901 – 914). With every Advanced-only ID at its default, Motion source 900 alone moves nothing.
   *Decision needed from the user:* either accept that Motion on Main is a source selector whose effect needs Advanced, or rule
   a change outside 12a's remit (a non-zero registered default for 902 is a frozen-surface change, spec FR-096; a Main binding
   of 902 contradicts FR-050 / SC-019). This also bears on D3's SC-006 (a) row for 900.
3. **412 and voice mode 1 with one held note.** Glide only acts between two notes, and one held note sounds the same in Mono
   and Poly. *Proposal for the user:* add, for 412 at min / max and for 1 at both values, one legato pair render (E1 held 1.5 s,
   then C3 overlapping, 3 s total + 1 s tail). This adds files to T010's set; it needs the user's ruling here first.

---

## Citations re-opened for this document (this session, at `40f6ebca` + the uncommitted `specs/profundum-phase12-ui/`)

| Claim | File:line |
|---|---|
| Highest registered ID 1002; Main IDs | `plugins/profundum/src/plugin_ids.h:100–207` |
| Voice Mode list, default Mono | `plugins/profundum/src/parameters/global_params.h:50, :139–141` |
| Macro titles, unit %, default kNeutral | `plugins/profundum/src/parameters/macro_params.h:118–127`; `dsp/include/krate/dsp/systems/profundum_macro_matrix.h:67` |
| `kRows` (RecipeShift at :80); `apply(MorphState&)` | `profundum_macro_matrix.h:75–92, :204` |
| Mass row, range, neutral | `plugins/profundum/src/parameters/morph_params.h:51`; `dsp/include/krate/dsp/systems/mass_law.h:72–74` |
| 105 / 1000 / 1001 registration, labels, defaults, 105 display | `morph_params.h:179–188, :252–265, :280–284` |
| Depth / Body / Edge law rows and defaults | `dsp/include/krate/dsp/systems/morph_state.h:160–169`; `dsp/include/krate/dsp/processors/spectral_shape_recipe.h:240–241` |
| Fold row, range, default | `plugins/profundum/src/parameters/fold_shape_params.h:23`; `morph_state.h:105, :180–181` |
| Drop row, range, default | `plugins/profundum/src/parameters/note_params.h:64`; `dsp/include/krate/dsp/processors/drop_envelope.h:31, :36` |
| Portamento law and default | `note_params.h:226–231, :308–309` |
| 412 readout follows 414 | `plugins/profundum/src/controller/controller.cpp:165–167` |
| Anchor waveform labels, level default | `plugins/profundum/src/parameters/anchor_params.h:24–25, :32–36`; `dsp/include/krate/dsp/systems/anchor_oscillator.h:57, :72, :100` |
| Motion source labels; MotionConfig defaults (amount 0) | `plugins/profundum/src/parameters/motion_params.h:37–38, :52–53`; `morph_state.h:82–86` |
| Anchor curve exponent | `dsp/include/krate/dsp/systems/bass_morph_engine.h:72` |
| Edit Target first pass; sound rows to the selected endpoint | `plugins/profundum/src/processor/processor.cpp:672–682, :719–720` |
| Route kinds (macro / morph-control / note not endpoint-scoped) | `plugins/profundum/src/parameters/sound_param_table.h:87–92` |
| Engine defaults as leaf words | `plugins/profundum/src/parameters/sound_param_row.h:113` |
| Concept §14 | `specs/Profundum-concept.md:647–678` |
| Roadmap wireframe titles | `specs/Profundum-roadmap.md:927, :955` |
