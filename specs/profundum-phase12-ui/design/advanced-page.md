# D2 — Advanced page

**Status: RATIFIED by the user, 2026-10-23 (spec Clarifications "Session 2026-10-23 (12a ratification)").**

**Deliverable:** D2 of FR-002 (`specs/profundum-phase12-ui/spec.md`). **Task:** T003 (`tasks.md:283–325`).
**Covers:** FR-004, FR-013, FR-024, FR-025, FR-044, FR-048, FR-050 – FR-052, FR-070, FR-073, FR-074, FR-076, FR-080.
**Plan sections:** §3.1 "Advanced page (D2)", "Spectrum drawing", "Tooltip statements", "Endpoint descriptor table";
§4.3.2; §4.6.8; §4.7; §4.8; §4.9; §4.12.

Every control below is labelled with its registered parameter ID, or with `UI-only (session tag N)` from the session-tag
table of plan §4.7 (`plan.md:1218–1234`). The control class of each ID comes from the class table of plan §4.12
(`plan.md:1370–1379`), apart from the two corrections listed in §2.0. Registered titles and list strings are quoted from
the parameter packs and cited per row. Nothing in this document changes any parameter's ID, type, range, default, law or
display string (FR-096).

---

## 1. Sections, tabs and containers (FR-004, FR-013)

### 1.1 Container tree

```
editor  (960 x 600, minSize = maxSize, sub-controller="ProfundumPanel")
+-- header                     (D1 rules it; page strip UI-only (session tag 9100), zoom UI-only (session tag 9300))
+-- page-main                  (D1)
+-- page-advanced              uidesc-label="page-advanced"
    +-- tab strip              CSegmentButton, 3 cells SECTIONS / MODULATION / SETTINGS -- UI-only (session tag 9110)
    +-- tab-sections           uidesc-label="tab-sections"
    |   +-- section-core-spectrum
    |   +-- section-excitation-fold
    |   +-- section-shape-character
    |   +-- section-mass
    |   +-- section-sub-anchor
    |   +-- section-stereo-output
    |   +-- section-pitch-envelopes
    |   +-- section-morph-behaviour
    +-- tab-modulation         uidesc-label="tab-modulation"     (Motion detail 900 - 914)
    +-- tab-settings           uidesc-label="tab-settings"       (zoom, run-loop note)
```

The tab strip and the three tab containers are children of `page-advanced` (FR-013: "The tab strip lives inside the
Advanced page container", `spec.md:362–363`). Page and tab selection are session state and are never persisted (plan §4.7,
`plan.md:1256`). There is no `UIViewSwitchContainer` (FR-030; plan §4.12, `plan.md:1359`).

### 1.2 Section membership (exactly FR-004, `spec.md:286–303`)

| Container label | Bound IDs and views |
|---|---|
| `section-core-spectrum` | 200, 201, 202, 203, 204, 205, 500, 102, `HarmonicBarView` #2 (`bar-variant="core"`) |
| `section-sub-anchor` | 302, 300, 301 (under the text `ANCHOR MODE`), 303, 304, 305, 306, 1000, the `Held at A` label |
| `section-excitation-fold` | 207, 206, 208, 500 |
| `section-shape-character` | 501, 600, 601, 602 |
| `section-mass` | 700, 701 |
| `section-pitch-envelopes` | 400, 401, 402, 403; `ADSRDisplay` for 420 – 426 plus `ArcKnob`s 420 – 426; 410, 411, 412, 413, 414, 415, 416, 417 |
| `section-stereo-output` | 800, the `SpreadCurveView`, 801, 802, 803, 804, 805, 806, 807, 808, 809; global band 0, 2, 3 |
| `section-morph-behaviour` | 1000, 1002, compare UI-only (session tag 9203), Copy A → B UI-only (session tag 9204), Copy B → A UI-only (session tag 9205) |
| `tab-modulation` | 900, 901, 902, 903, 904, 905, 906, 907, 908, 909, 910, 911, 912, 913, 914 |
| `tab-settings` | zoom UI-only (session tag 9300); the note `No host run loop: the spectrum view stays empty` |

- **Bound in two sections** (FR-004 names both): Harmonic Fold 500 in `section-core-spectrum` and `section-excitation-fold`;
  Anchor Morph Mode 1000 in `section-sub-anchor` and `section-morph-behaviour` (plan §3.1, `plan.md:448–449`). Each
  instance is its own control bound to the same tag, and both follow the parameter.
- **Advanced-only IDs** (FR-011, `spec.md:358`): Spectral Shift 205, Tight ↔ Loose 102 and Anchor Mode 301. All three are
  bound here and nowhere on Main (SC-031 (a), (b)).
- **Not bound anywhere:** the hidden performance IDs 4 – 7 (plan §4.12, `plan.md:1350–1351`). Weight 100, Dark ↔ Bright 101,
  Clean ↔ Violent 103, Mass 104, Bass Morph 105, voice mode 1 and Edit Target 1001 are Main-only (D1).
- Note behaviour 410 – 417 stays in Pitch & Envelopes and the global band 0 / 2 / 3 stays in Stereo & Output. D2 moves no ID
  out of its FR-004 container (plan §3.1, `plan.md:445–446`).

---

## 2. Wireframes

### 2.0 Geometry and class corrections

- The frame is 960 × 600 at 100 % (plan §3.1 "Designed size", `plan.md:376–383`). The header height belongs to D1. The
  figures below assume a 40 px header, a 24 px tab strip (y 40 – 64) and a content area at y 64 – 600 (536 px). If D1 rules
  a different header height, the content rect moves and the column contents stay the same.
- Three columns: x 8 – 312, x 328 – 632 and x 648 – 952 (304 px each, 16 px gutters). Each control row is 28 px (plan
  §3.1's 28 px knob-row pitch). A row holds either one full-width control or two half-width cells of 152 px (label, a 24 px
  control, value). Section headers are 20 px `CTextLabel`s. The minimum hit target is 20 × 20 px and segment cells are
  24 × 24 px (D7).
- **Two class corrections to plan §4.12's table, from the registered list counts.** Mono Priority 410 has **three** entries
  (`Last / Low / High`, `note_params.h:305`), so it is a three-cell `CSegmentButton`, not a two-segment control. Portamento
  Mode 413 has **two** entries (`Always / Legato Only`, `note_params.h:311`), so it is a two-segment control. Both remain
  FR-032 list classes. T027 follows this document.
- Abbreviations in the drawings: `AK` = `ArcKnob`, `BS` = `BipolarSlider`, `SEG(n)` = `CSegmentButton` with n cells,
  `OM` = `COptionMenu`, `TG` = two-cell `CSegmentButton`, `BTN` = `CTextButton`, `TL` = `CTextLabel`.

### 2.1 SECTIONS tab: whole page

```
 x=8                         x=312  x=328                       x=632  x=648                       x=952
+--------------------------------------------------------------------------------------------------------+ y=0
| header (D1)                                                                                            |
+--------------------------------------------------------------------------------------------------------+ y=40
| [ SECTIONS | MODULATION | SETTINGS ]  SEG(3)  UI-only (session tag 9110)                                 |
+--------------------------------------------------------------------------------------------------------+ y=64
| CORE SPECTRUM               (228) | SUB ANCHOR                  (160) | PITCH & ENVELOPES          (400) |
| EXCITATION & FOLD            (76) | STEREO & OUTPUT             (324) |                                  |
| SHAPE & CHARACTER            (76) |                                   | MORPH BEHAVIOUR             (76) |
| MASS                         (48) |                                   |                                  |
+--------------------------------------------------------------------------------------------------------+ y=600
 column heights incl. 8 px gaps: 452 / 492 / 484 of 536
```

### 2.2 Core spectrum (`section-core-spectrum`, x 8, y 64, 304 × 228)

```
+-- TL "CORE SPECTRUM" ----------------------------------------------+
| HarmonicBarView #2  bar-variant="core"  (304 x 96, display-only)   |
|  ANCHOR |  Sub | Body (2-8)        | Presence (9-64)               |
|   lane  |  n=1 |                   |                               |
+--------------------------------------------------------------------+
| Depth          200  AK             | Body           201  AK        |
| Body Curvature 202  AK             | Body Emphasis  203  AK        |
| Edge           204  AK             | Spectral Shift 205  BS        |
| Harmonic Fold  500  AK             | Tight/Loose    102  BS        |
+--------------------------------------------------------------------+
```

Titles: `core_params.h:24–32`, `fold_shape_params.h:23`, `macro_params.h:119`.

### 2.3 Excitation & Fold (`section-excitation-fold`, x 8, y 300, 304 × 76)

```
+-- TL "EXCITATION & FOLD" ------------------------------------------+
| Excitation Drive 207  AK           | Excitation Mix 206  AK        |
| Excitation Asymmetry 208  AK       | Harmonic Fold  500  AK        |
+--------------------------------------------------------------------+
```

### 2.4 Shape & Character (`section-shape-character`, x 8, y 384, 304 × 76)

```
+-- TL "SHAPE & CHARACTER" ------------------------------------------+
| Shape          501  AK             | Character      600  OM (6)    |
| Character Amount 601  AK           | Character Tone 602  AK        |
+--------------------------------------------------------------------+
```

600's menu holds the registered strings `Tube / Wavefold / Rectify / Bite / Crush / Fracture` (`character_params.h:25–27`).

### 2.5 Mass (`section-mass`, x 8, y 468, 304 × 48)

```
+-- TL "MASS" -------------------------------------------------------+
| Transient Attack 700  AK           | Transient Sustain 701  AK     |
+--------------------------------------------------------------------+
```

Titles: `dynamics_params.h:22–23`.

### 2.6 Sub Anchor (`section-sub-anchor`, x 328, y 64, 304 × 160)

```
+-- TL "SUB ANCHOR" -------------------------- TL "Held at A" (hidden unless dimmed, §8) --+
| Anchor Waveform 300  OM (5)              | Anchor Level   302  AK                      |
| TL "ANCHOR MODE"  301  SEG(4)  Lock | Follow | Punch | Ghost                           |
| Punch Boost    303  AK                   | Punch Time     304  AK                      |
| Ghost Time     305  AK                   | Anchor Fold Feed 306  AK                    |
| TL "MORPH BEHAVIOUR"  1000  SEG(3)  Hold | Blend | Yield                               |
+-----------------------------------------------------------------------------------------+
```

- 300's menu: `Sine / Triangle / Rounded Sine / Asymmetric Sine / Octave Down` (`anchor_params.h:24–25`).
- 301 carries `Lock / Follow / Punch / Ghost` (`anchor_params.h:28–29`) under the text `ANCHOR MODE` (FR-012, SC-005).
- 1000 carries `Hold / Blend / Yield` (`morph_params.h:179–180`). The 301 and 1000 label sets are disjoint, and the two are
  separate widgets (FR-045). This 1000 instance is labelled `MORPH BEHAVIOUR`, as on Main (FR-012), so the same control
  has the same name on both pages.

### 2.7 Stereo & Output (`section-stereo-output`, x 328, y 232, 304 × 324)

```
+-- TL "STEREO & OUTPUT" ------------------------------------------------------------------+
| TL "Image"  800  SEG(3)  Solid | Spread | Wide                                          |
| SpreadCurveView  (304 x 80, display-only)  w(f) 20 Hz - 10 kHz, MONO (0) .. WIDE (1)   |
|   knee marks: lowerKneeHz (moves with Gravity), upperKneeHz = 4*f_c                     |
|   in WIDE: TL "WIDE: Character offsets not drawn"                                       |
|   TL "Edit target: A" / "Edit target: B"  (10 px, under the curve, §6)                  |
| Spread         801  AK                   | Gravity        802  AK                      |
| Crossover      803  AK                   | Curve          804  AK                      |
| Field Motion Depth 805  AK               | Field Motion Rate 806  AK                   |
| Field Motion Source 807  TG Drift|Cycle  | Field Sync     808  TG Off|On               |
| Field Sync Rate 809  OM                                                                 |
| TL "OUTPUT"   Output Gain 0  AK          | Polyphony      2  OM                         |
|               Output Ceiling 3  AK                                                      |
+-----------------------------------------------------------------------------------------+
```

- 800: `Solid / Spread / Wide` (`field_params.h:37–38`); 807: `Drift / Cycle` (`:39`); 808: `Off / On` (`:40`); 809 is
  the registered Field Sync Rate list (`field_params.h:59`). 807 sits in plan §4.12's "Lists" row, and a two-cell segment
  is a list class.
- Global band titles: `Output Gain` (`global_params.h:136`), `Polyphony` (`:145`), `Output Ceiling` (`:151`).
- The SPREAD mini-spectrum is in this section and nowhere on Main (FR-080, SC-031 (d)).

### 2.8 Pitch & Envelopes (`section-pitch-envelopes`, x 648, y 64, 304 × 400)

```
+-- TL "PITCH & ENVELOPES" ---------------------------------------------------------------+
| Drop Amount    400  AK                   | Drop Time      401  AK                      |
| Drop Shape     402  AK                   | Drop Rebound   403  AK                      |
| ADSRDisplay  (304 x 72)  bound to 420 - 426 through the FR-034 adapter (plan §4.6.10)   |
| Attack         420  AK                   | Decay          421  AK                      |
| Sustain        422  AK                   | Release        423  AK                      |
| Attack Curve   424  AK                   | Decay Curve    425  AK                      |
| Release Curve  426  AK                                                                  |
| Mono Priority  410  SEG(3) Last|Low|High | Legato         411  TG Off|On               |
| Portamento     412  AK                   | Portamento Mode 413  TG Always|Legato Only  |
| Glide Mode     414  SEG(3)  Constant Time | Constant Rate | Quantized                    |
| Note Memory    415  OM (4)                                                              |
| Note Retrigger 416  TG Reset|Continue    | Pitch Bend Range 417  AK (stepped)          |
+-----------------------------------------------------------------------------------------+
```

- 400 – 403 and 420 – 426 titles: `note_params.h:64–74`. 410 – 417 registration: `note_params.h:305–322`. 415's
  registered strings are `Always Retrigger / Legato / Envelope Retrigger / Oscillator Continuous`
  (`note_params.h:314–316`). The word "Oscillator" is allowed here: FR-020 governs the Main page only, and FR-036 requires
  the registered strings.
- 417 honours its step count: the panel sets `CKnobBase` stepping from `getParameterInfo(417).stepCount` (plan §4.12,
  `plan.md:1378`).
- The 412 `ArcKnob` writes 412. No separate 412 readout is placed on Advanced.

### 2.9 Morph behaviour (`section-morph-behaviour`, x 648, y 472, 304 × 76)

```
+-- TL "MORPH BEHAVIOUR" ------------------------------------------------------------------+
| TL "MORPH BEHAVIOUR" 1000 SEG(3) Hold|Blend|Yield | Morph Ramp Time 1002  AK           |
| TL "Compare" SEG(3) A | A·B | B  UI-only (session tag 9203)                             |
|   BTN "Copy A → B" UI-only (session tag 9204)  BTN "Copy B → A" UI-only (session tag 9205) |
+------------------------------------------------------------------------------------------+
```

- The 9203 segment shows `compareMode()` and nothing else. A click alone changes nothing: every compare segment is reset to
  the mode the controller reports (FR-041; plan §4.7, `plan.md:1257`). Segment index ↔ mode: 0 → A, 1 → AB, 2 → B
  (`plan.md:1224`).
- 1002 shows its registered display string (Morph Ramp Time, ms; `morph_params.h:268–270`).

### 2.10 MODULATION tab (`tab-modulation`, content y 64 – 600)

```
+-- TL "SOURCE" ----------------------+-- TL "CYCLE" -----------------------+-- TL "ENV & DRIFT" ----------------+
| Motion Source  900  SEG(4)          | Cycle Rate     904  AK              | Motion Polarity 911  TG            |
|   Off | Env | Cycle | Drift         | Cycle Shape    905  OM (6)          |   Bipolar | Unipolar              |
| Motion Target  901  OM (5)          | Cycle Sync     906  TG Off|On       | Env Attack     913  AK             |
| Motion Amount  902  BS              | Cycle Sync Rate 907  OM             | Env Decay      914  AK             |
| Motion Depth   903  AK              | Cycle Phase    908  AK              | Drift Smoothness 912  AK           |
|                                     | Cycle Retrigger 909  TG Off|On      |                                    |
|                                     | Cycle Fade-In  910  AK              |                                    |
+-------------------------------------+-------------------------------------+------------------------------------+
```

Registered strings: 900 `Off / Env / Cycle / Drift` (`motion_params.h:37–38`), 901 `Mass / Depth / Body / Edge / Shift`
(`:39–40`), 905 `Sine / Triangle / Sawtooth / Square / S&H / Smooth Random` (`:41–42`), 906 and 909 `Off / On` (`:43–44`),
911 `Bipolar / Unipolar` (`:45`). Titles: `motion_params.h:52–74`. The three column headings are plain `CTextLabel`s,
not containers. The FR-004 container of 900 – 914 is `tab-modulation` (FR-050).

### 2.11 SETTINGS tab (`tab-settings`)

```
+-- TL "SETTINGS" ------------------------------------------------------------------------+
| TL "Zoom"  SEG(4)  100% | 125% | 150% | 200%   UI-only (session tag 9300)              |
| TL "No host run loop: the spectrum view stays empty"                                    |
+-----------------------------------------------------------------------------------------+
```

- This zoom and D1's header zoom carry the same session tag 9300 and select the same step (FR-014). Plan §4.7 lists one
  9300 control, so the panel must reset **every** collected 9300 segment to the current step after a change, as it does
  for the compare segments (`plan.md:1257`). **Ruled 2026-10-23: both** (the header zoom and the SETTINGS zoom stay; same tag 9300). Was: keep the SETTINGS zoom (the plan's choice,
  `plan.md:444`), or keep only the header zoom.
- The run-loop note (FR-076, `spec.md:532–543`). The transport is the SDK DataExchange mechanism with its IMessage
  fallback. The fallback queue opens wherever the host supplies a run loop
  (`extern/vst3sdk/public.sdk/source/main/pluginfactory.cpp:316–319`, as FR-076 cites). In a host that provides neither
  the DataExchange API nor a run loop, no frame arrives and both `HarmonicBarView`s stay empty. This is documented, not
  worked around.

---

## 3. Spectrum drawing (FR-025, FR-070, FR-073, FR-074; plan §3.1 "Spectrum drawing", §4.8)

Both `HarmonicBarView` instances (Main `bar-variant="main"`, Advanced `bar-variant="core"`) draw the same frame
(FR-070, SC-031 (e)).

| Aspect | Ruling |
|---|---|
| Vertical axis | dB relative to the frame's strongest component, the Anchor included: P_ref = max(max_n P_n, anchorPower) and h_n = clamp((10·log10(P_n / P_ref) + 60) / 60, 0, 1). The top is 0 dB and the floor −60 dB (plan §4.8, `plan.md:1299`) |
| Horizontal axis | log₂ n for n = 1 … 64, the recipe's own axis; one bar per n ≤ `numPartials` |
| Region shading | Sub (n = 1), Body (n = 2 – 8), Presence (n = 9 – 64). Body ≈ 2 – 8 is the 2026-10-09 ruling (`specs/Profundum-roadmap.md:225`); the three names are the roadmap's descriptors (`Profundum-roadmap.md:113–114`) |
| Anchor | in its own lane, left of the Mass bars, labelled `ANCHOR`; its height uses the same formula with `anchorPower`. The `main` variant also draws the ANCHOR / MASS split |
| Silence | on a silent frame the bars **decay linearly in drawn height to 0 over 300 ms**: each tick subtracts dt / 0.300 from every bar, floored at 0 (plan §4.8, `plan.md:1301–1302`). 300 ms is the decay time SC-026 measures against |
| Pitch-unstable frames | drawn normally (`kFrameFlagPitchUnstable`, plan §5.2), because each is still a real measurement at the latched f0 |
| Above Nyquist | partials above the core's cap are absent and never drawn. The frame's `count = min(64, ⌊(fs/2) / f0⌋)` also excludes a partial exactly at Nyquist (FR-073; plan §5.3) |
| Non-finite values | never sent, so never drawn (FR-073) |
| Interaction | none. The view is display-only: `onMouseDown` returns not-handled and it never calls `performEdit` (FR-070, plan §4.8) |
| Refresh | a 33 ms `CVSTGUITimer`, created in `attached()` and released in `removed()` (plan §4.8) |

**What the bars mean.**

- The bars show harmonic **structure measured before the amp envelope** (plan P-7, `plan.md:338–341`). The measured taps
  are taken before `g_amp` and `g_d` are applied, so the bars show the shape of the spectrum, not loudness over time. They
  include the harmonics the Fold (500 – 501) and Character (600 – 602) stages generate, up to partial 64 (FR-025, FR-075).
- **Edit-Target endpoint vs resolved signal** (plan P-6, `plan.md:333–337`). The knobs, the SPREAD curve (§6), the macro
  rings and the Anchor dimming (§8) describe the **Edit-Target endpoint** that 1001 selects. The spectrum is the only
  display of the **resolved, modulated** signal, with morph position, macros and Motion applied. A label under the SPREAD
  curve states the first; the bar view's tooltip states the second (§5.2).

---

## 4. Focus rule (FR-074), verbatim from plan §4.3.2 (`plan.md:746–763`)

> The focus is the sounding voice whose note was pressed most recently and is still held. When no voice is held, the last
> focus is kept while that voice still sounds (its release tail). When it stops, the frame reports silence.

- **MONO** (`appliedVoiceMode() == Mono`): `focus = 0` if `voiceRenderedThisInterval(0)`, else −1.
- **POLY:**
  - Candidates are the voices with `voiceRenderedThisInterval(v)`, `voiceForDisplay(v).held()` and
    `note = voiceMidiNote(v) ≥ 0`.
  - Among the candidates, pick the one with the greatest `noteSeq_[note]`. Ties cannot occur, because the counter only
    increases.
  - If no voice is held, keep `focus_` while `voiceRenderedThisInterval(focus_)` holds; otherwise the focus is −1.

Consequences (plan §4.3.2):

- a same-note retrigger refreshes the sequence, and the allocator retriggers the same voice
  (`dsp/include/krate/dsp/systems/voice_allocator.h:238–244`, "FR-012: Same-note retrigger");
- a steal moves the focus to the stealing note's voice;
- releasing the newest held note returns the focus to the newest note that is still held.

A focus change restarts the measurement window. No partial-window frame is published.

---

## 5. Tooltips (exact UTF-8 literals; SC-005 compares them)

### 5.1 Ruled statements

These substrings are **ruled literals**. T027 copies them byte for byte, and SC-005 checks that each carrier's tooltip
contains its literal. Non-ASCII code points used: `–` U+2013 EN DASH, `−` U+2212 MINUS SIGN, `↔` U+2194, `ρ` U+03C1,
`≥` U+2265, `·` U+00B7, `→` U+2192.

| Key | Literal | Carried by |
|---|---|---|
| FIELD-SYNC (FR-051) | `Synced rates above 1 Hz fold into 0.5–1 Hz.` | 809 |
| BIPOLAR-REST (FR-052) | `Bipolar ENV rests at −amount between notes; Unipolar rests at the set value.` | 911 |
| MONO-GUARANTEE (FR-024) | `Width is pan-only and constant-power; the generator's sub band stays at ρ ≥ 0.9989. Gravity 0 still attenuates side content below 2·f_c by −29 dB.` | each of 800, 801, 802, 803, 804, 805, 806, 807, 808, 809 |
| WIDE-CAVEAT (FR-024) | `In WIDE the sub-band correlation depends on the Character offset pair.` | each of 600, 601, 602 |
| COMPRESSION (FR-060) | `Compression` (the word; CompressionAmount has no control) | 102 |

- FR-051 is ruled as **stating the folding** rather than computing the effective rate, because the fold depends on the
  tempo (plan §3.1, `plan.md:467–469`). The engine folds synced rates above 1 Hz into (0.5, 1] Hz (FR-051,
  `spec.md:473–474`).
- The MONO-GUARANTEE figures are Phase 7's measured guarantees as FR-024 states them (`spec.md:377–384`): ρ ≥ 0.9989, and
  ×0.035 (−29.1 dB) on side content below 2·f_c at Gravity 0. The tooltip rounds the latter to −29 dB.
- No tooltip or label anywhere contains `Oversampling`, `Quality`, `Low-End Protection` or `Parameter Curve` (FR-093,
  SC-005).

### 5.2 Full tooltip table (every Advanced control; T027 copies, never composes)

The Label column is the text the page shows beside the control. It equals the registered title unless the wireframe
says otherwise. For 800 – 809, "+ MONO-GUARANTEE" means the §5.1 literal is appended after one space.

| ID / tag | Section | Class | Label | Tooltip |
|---|---|---|---|---|
| 200 | core | AK | Depth | `Depth: how steeply the harmonics fall away above the fundamental. Higher is rounder, lower is richer.` |
| 201 | core | AK | Body | `Body: lifts the low harmonics (about 2 to 8) that carry the bass's body.` |
| 202 | core | AK | Body Curvature | `Body Curvature: narrows or broadens the region Body lifts.` |
| 203 | core | AK | Body Emphasis | `Body Emphasis: at 0 all harmonics sound; toward −100 % the even harmonics fade for a hollower tone.` |
| 204 | core | AK | Edge | `Edge: lifts the upper harmonics that give the bass its bite.` |
| 205 | core | BS | Spectral Shift | `Spectral Shift: moves the Body and Edge regions down (darker) or up (brighter) together.` |
| 500 | core, excitation-fold | AK | Harmonic Fold | `Harmonic Fold: folds the waveform back on itself to add new harmonics. 0 is clean.` |
| 102 | core | BS | Tight/Loose | `Tight ↔ Loose: toward Tight, shorter attack, decay and release, a harder transient and more Compression; toward Loose the reverse. Compression has no control of its own.` |
| `HarmonicBarView` #2 | core | view | — | `The measured harmonics of the sounding note after Fold and Character, before the amp envelope, in dB below the strongest component. Shows the resolved sound, including morph, macros and Motion.` |
| 207 | excitation-fold | AK | Excitation Drive | `Excitation Drive: how hard the excitation stage is driven.` |
| 206 | excitation-fold | AK | Excitation Mix | `Excitation Mix: blends the driven excitation into the clean tone. 0 is clean.` |
| 208 | excitation-fold | AK | Excitation Asymmetry | `Excitation Asymmetry: biases the drive for uneven, even-harmonic grit.` |
| 501 | shape-character | AK | Shape | `Shape: morphs the curve of the SHAPE stage.` |
| 600 | shape-character | OM | Character | `Character: the type of colouring stage. In WIDE the sub-band correlation depends on the Character offset pair.` |
| 601 | shape-character | AK | Character Amount | `Character Amount: how much of the Character stage is heard. In WIDE the sub-band correlation depends on the Character offset pair.` |
| 602 | shape-character | AK | Character Tone | `Character Tone: darkens or brightens the Character stage. In WIDE the sub-band correlation depends on the Character offset pair.` |
| 700 | mass | AK | Transient Attack | `Transient Attack: boosts or softens the start of each note.` |
| 701 | mass | AK | Transient Sustain | `Transient Sustain: boosts or softens the body that follows the start.` |
| 302 | sub-anchor | AK | Anchor Level | `Anchor Level: level of the pure sub tone under the bass.` |
| 300 | sub-anchor | OM | Anchor Waveform | `Anchor Waveform: the shape of the sub tone.` |
| 301 | sub-anchor | SEG(4) | ANCHOR MODE | `Anchor Mode: how the sub tone tracks the note: Lock, Follow, Punch or Ghost.` |
| 303 | sub-anchor | AK | Punch Boost | `Punch Boost: extra sub level at the note start in Punch mode.` |
| 304 | sub-anchor | AK | Punch Time | `Punch Time: how long the Punch boost lasts.` |
| 305 | sub-anchor | AK | Ghost Time | `Ghost Time: the timing of the dip in Ghost mode.` |
| 306 | sub-anchor | AK | Anchor Fold Feed | `Anchor Fold Feed: sends the sub tone into Harmonic Fold.` |
| 1000 | sub-anchor, morph-behaviour | SEG(3) | MORPH BEHAVIOUR | `Morph behaviour of the sub tone: Hold keeps A's sub, Blend follows the morph, Yield hands over late.` |
| `Held at A` | sub-anchor | TL | Held at A | `Hold is on and you are editing B: B's sub settings are stored but not heard.` |
| 800 | stereo-output | SEG(3) | Image | `Image Mode: Solid is mono, Spread widens above the crossover, Wide adds the Character offsets.` + MONO-GUARANTEE |
| `SpreadCurveView` | stereo-output | view | — | `How wide each frequency is for the endpoint being edited: 0 is mono, 1 is fully wide. Field Motion is not drawn.` |
| 801 | stereo-output | AK | Spread | `Spread: overall stereo width above the crossover.` + MONO-GUARANTEE |
| 802 | stereo-output | AK | Gravity | `Gravity: how far below the crossover the width begins. At 0 the width reaches lowest.` + MONO-GUARANTEE |
| 803 | stereo-output | AK | Crossover | `Crossover: the frequency below which the bass stays centred.` + MONO-GUARANTEE |
| 804 | stereo-output | AK | Curve | `Curve: how quickly the width rises between the low and high knees.` + MONO-GUARANTEE |
| 805 | stereo-output | AK | Field Motion Depth | `Field Motion Depth: how much the width moves over time.` + MONO-GUARANTEE |
| 806 | stereo-output | AK | Field Motion Rate | `Field Motion Rate: speed of the width movement.` + MONO-GUARANTEE |
| 807 | stereo-output | TG | Field Motion Source | `Field Motion Source: Drift wanders, Cycle repeats.` + MONO-GUARANTEE |
| 808 | stereo-output | TG | Field Sync | `Field Sync: lock the width movement to the host tempo.` + MONO-GUARANTEE |
| 809 | stereo-output | OM | Field Sync Rate | `Field Sync Rate: note value of the synced width movement. Synced rates above 1 Hz fold into 0.5–1 Hz.` + MONO-GUARANTEE |
| 0 | stereo-output | AK | Output Gain | `Output Gain: final level.` |
| 2 | stereo-output | OM | Polyphony | `Polyphony: how many notes can sound at once in Poly.` |
| 3 | stereo-output | AK | Output Ceiling | `Output Ceiling: the highest true-peak level the output reaches.` |
| 400 | pitch-envelopes | AK | Drop Amount | `Drop Amount: how far the pitch falls into each note, in semitones.` |
| 401 | pitch-envelopes | AK | Drop Time | `Drop Time: how long the pitch drop takes.` |
| 402 | pitch-envelopes | AK | Drop Shape | `Drop Shape: the curve of the pitch drop.` |
| 403 | pitch-envelopes | AK | Drop Rebound | `Drop Rebound: how much the pitch overshoots and settles at the end of the drop.` |
| `ADSRDisplay` | pitch-envelopes | ADSRDisplay | — | `Amp envelope: drag the points to set Attack, Decay, Sustain and Release.` |
| 420 | pitch-envelopes | AK | Attack | `Attack: time to full level.` |
| 421 | pitch-envelopes | AK | Decay | `Decay: time to fall to the Sustain level.` |
| 422 | pitch-envelopes | AK | Sustain | `Sustain: level held while the key is down.` |
| 423 | pitch-envelopes | AK | Release | `Release: time to silence after the key is released.` |
| 424 | pitch-envelopes | AK | Attack Curve | `Attack Curve: bends the attack segment.` |
| 425 | pitch-envelopes | AK | Decay Curve | `Decay Curve: bends the decay segment.` |
| 426 | pitch-envelopes | AK | Release Curve | `Release Curve: bends the release segment.` |
| 410 | pitch-envelopes | SEG(3) | Mono Priority | `Mono Priority: which held note sounds in Mono: Last, Low or High.` |
| 411 | pitch-envelopes | TG | Legato | `Legato: overlapping notes glide without restarting the envelope.` |
| 412 | pitch-envelopes | AK | Portamento | `Portamento: glide time between notes.` |
| 413 | pitch-envelopes | TG | Portamento Mode | `Portamento Mode: glide on every note, or only between overlapping notes.` |
| 414 | pitch-envelopes | SEG(3) | Glide Mode | `Glide Mode: Constant Time, Constant Rate or Quantized glide.` |
| 415 | pitch-envelopes | OM | Note Memory | `Note Memory: what a new note restarts.` |
| 416 | pitch-envelopes | TG | Note Retrigger | `Note Retrigger: Reset restarts the note's movement, Continue carries it on.` |
| 417 | pitch-envelopes | AK | Pitch Bend Range | `Pitch Bend Range: bend wheel range in semitones.` |
| 1002 | morph-behaviour | AK | Morph Ramp Time | `Morph smoothing: how quickly the sound follows a change of the morph position.` |
| 9203 | morph-behaviour | SEG(3) | Compare | `Compare: hear A, the morph, or B. Choosing A or B also edits that side.` |
| 9204 | morph-behaviour | BTN | Copy A → B | `Copy A's sound to B. Names are not copied.` |
| 9205 | morph-behaviour | BTN | Copy B → A | `Copy B's sound to A. Names are not copied.` |
| 900 | modulation | SEG(4) | Motion Source | `Motion Source: Off, Env, Cycle or Drift.` |
| 901 | modulation | OM | Motion Target | `Motion Target: what Motion moves.` |
| 902 | modulation | BS | Motion Amount | `Motion Amount: how far and in which direction Motion moves its target.` |
| 903 | modulation | AK | Motion Depth | `Motion Depth: overall Motion depth.` |
| 904 | modulation | AK | Cycle Rate | `Cycle Rate: speed of the Cycle source.` |
| 905 | modulation | OM | Cycle Shape | `Cycle Shape: waveform of the Cycle source.` |
| 906 | modulation | TG | Cycle Sync | `Cycle Sync: lock the Cycle to the host tempo.` |
| 907 | modulation | OM | Cycle Sync Rate | `Cycle Sync Rate: note value of the synced Cycle.` |
| 908 | modulation | AK | Cycle Phase | `Cycle Phase: start phase of the Cycle.` |
| 909 | modulation | TG | Cycle Retrigger | `Cycle Retrigger: restart the Cycle on each note.` |
| 910 | modulation | AK | Cycle Fade-In | `Cycle Fade-In: time for the Cycle to reach full depth.` |
| 911 | modulation | TG | Motion Polarity | `Motion Polarity: Bipolar ENV rests at −amount between notes; Unipolar rests at the set value.` |
| 912 | modulation | AK | Drift Smoothness | `Drift Smoothness: how smoothly the Drift source wanders.` |
| 913 | modulation | AK | Env Attack | `Env Attack: rise time of the Env source.` |
| 914 | modulation | AK | Env Decay | `Env Decay: fall time of the Env source.` |
| 9110 | (page) | SEG(3) | Sections / Modulation / Settings | `Advanced page sections.` |
| 9300 | settings | SEG(4) | Zoom | `Window size.` |

Tooltip notes:

- Every bound control has a tooltip (D7, SC-019). The two views and the `ADSRDisplay` carry one as well.
- 102's tooltip is the only place `Compression` is named. FR-060: `CompressionAmount` has no control. Its row is
  `kRows[11]` (`dsp/include/krate/dsp/systems/profundum_macro_matrix.h:87`), amount −1.25 toward Loose, so Tight gives more
  compression.
- The 1000 tooltip words Hold / Blend / Yield for the user. They are the engine's Lock / Blend / Punch
  (`morph_params.h:170–173`: Hold = Lock, Yield = Punch); D4 documents the curves.

---

## 6. SPREAD curve (FR-080; plan §4.6.8, §4.9)

**Drawn quantity:** w(f) = c(f) · m.

- c(f) = `BassStereoField::widthCurve(f)` (`dsp/include/krate/dsp/systems/bass_stereo_field.h:398–403`), which returns
  `lawCurve` (:632–644): 1 at and above 4·f_c,s, otherwise 0.5 − 0.5·cos(π·x^γ) with x = (log₂ f − uLow) / span.
- m = 0 in SOLID; otherwise m = spread · (1 − κ·μ + κ·μ·m_mot) (`updateLawCache`, :647–658, the expression at :655–657),
  with κ = `kMotionExcursion` = 0.5 (:78), spread = 801 and μ = 805. **The static curve draws m_mot = 0.**
- The controller computes the curve on the UI thread from the displayed values of 800 – 805 through a never-prepared
  `BassStereoField` (`setControls`, then `reset()`): plan §4.6.8 steps 1 – 6 (`plan.md:1156–1183`).

**Drawing rules.**

| Aspect | Ruling |
|---|---|
| Sampling | 64 log-spaced points, `hz[i] = 20 · (10000 / 20)^(i / 63)` for i = 0 … 63, joined by straight segments |
| Horizontal | log frequency, 20 Hz – 10 kHz |
| Vertical | 0 (label `MONO`) to 1 (label `WIDE`) |
| Knee marks | two vertical ticks. `lowerKneeHz = f_c·2^(−kLowerKneeMaxOctaves·(1 − G))` moves with Gravity 802 (`kLowerKneeMaxOctaves` = 0.25, :75; the law's `uLow_` at :650). `upperKneeHz = f_c·2^kUpperKneeOctaves = 4·f_c` (`kUpperKneeOctaves` = 2, :74) |
| Motion excursion band (m_mot = ±1) | **not drawn** (the plan's proposal, `plan.md:1183`, which FR-080 lets D2 rule) |
| WIDE | the curve is still drawn, plus the label `WIDE: Character offsets not drawn` |
| SOLID | draws 0 at every frequency; Spread 0 also draws 0 (SC-014) |
| Endpoint | the curve describes the **Edit-Target endpoint** (plan P-6). A 10 px `CTextLabel` under it reads `Edit target: A` or `Edit target: B` |
| Refresh | no timer: the controller calls `invalid()` after `refreshSpreadModel()` (plan §4.9) |

---

## 7. Endpoint descriptor table (FR-048; plan §3.1, §4.11)

Three words, one per axis, computed in the controller from `mirror_[e]` (the endpoint's `MorphState`). They are pure
functions of the endpoint: never stored, never in a message, never supplied by a preset (FR-048).

### 7.1 Inputs (field paths re-read in `dsp/include/krate/dsp/systems/morph_state.h`)

| Input | `MorphState` field path | Declared at | Range |
|---|---|---|---|
| depth | `recipe.depth` | `morph_state.h:103` (`SpectralShapeRecipe::Controls recipe`); member at `spectral_shape_recipe.h:42` | [0, 1] |
| body | `recipe.body` | `morph_state.h:103`; `spectral_shape_recipe.h:43` | [0, 1] |
| edge | `recipe.edge` | `morph_state.h:103`; `spectral_shape_recipe.h:46` | [0, 1] |
| shift | `recipe.shift` | `morph_state.h:103`; `spectral_shape_recipe.h:47` | [−1, +1] |
| foldAmount | `foldAmount` | `morph_state.h:105` | [0, 100] |
| characterAmount | `characterAmount` | `morph_state.h:108` | [0, 1] |
| mix | `excitation.mix` | `morph_state.h:104` (`ProfundumCore::ExcitationControls excitation`); member at `profundum_core.h:355` | [0, 1] |
| drive | `excitation.drive` | `morph_state.h:104`; `profundum_core.h:356` | [0, 1] |

### 7.2 Table

| Axis | Score s (float, evaluated as written) | s < 0.33 | 0.33 ≤ s < 0.66 | s ≥ 0.66 |
|---|---|---|---|---|
| Shape | `0.5f * (1.0f - depth) + 0.5f * body` | Round | Full | Dense |
| Colour | `0.5f * edge + 0.25f * (shift + 1.0f)` | Dark | Warm | Bright |
| Force | `max(foldAmount / 100.0f, characterAmount, mix * drive)` | Clean | Driven | Aggressive |

- Named constants: `kDescriptorLowerThreshold = 0.33f` and `kDescriptorUpperThreshold = 0.66f` (plan §4.11: "The
  thresholds are named constants that D2 cites").
- **Each threshold belongs to the upper band:** s = 0.33 gives the second word and s = 0.66 the third. The comparisons are
  `s < 0.33f` and `s < 0.66f`.
- **Fold is divided, not multiplied.** IEEE division is correctly rounded, so `66.0f / 100.0f` equals the literal `0.66f`
  and `33.0f / 100.0f` equals `0.33f`. `66.0f * 0.01f` does not equal `0.66f` (checked in float32).
- Caption word order: Shape, Colour, Force (for example `Full Warm Clean`).

### 7.3 Six worked cases (SC-027 (d))

The scores were evaluated in IEEE float32 with the expressions above. For the threshold cases they were also evaluated as
`0.5f * ((1.0f - depth) + body)` and `0.5f * edge + 0.25f * shift + 0.25f`, and every threshold case lands exactly on its
threshold in all of these orders. Fields not listed keep their `MorphState{}` defaults: `recipe` =
`SpectralShapeRecipe::kDefaultControls` (`spectral_shape_recipe.h:240–241`: depth 0.5, body 0.5, edge 0.5, shift 0);
`excitation` = `ProfundumCore::kDefaultExcitation` (`profundum_core.h:354–358` and `:1244`: mix 0, drive 0.5);
foldAmount 0 and characterAmount 0 (`morph_state.h:105`, `:108`).

| # | State | Inputs that matter | Shape s | Colour s | Force s | Words |
|---|---|---|---|---|---|---|
| 1 | `MorphState{}` | depth 0.5, body 0.5, edge 0.5, shift 0; fold 0, char 0, mix 0 · drive 0.5 | 0.5 | 0.5 | 0 | **Full Warm Clean** |
| 2 | Phase 8 showcase A: `BassMorphTest::showcasePair().first` (`dsp/tests/unit/systems/bass_morph_state_helpers.h:167–189`) | recipe = `kSineAnchor` (depth 1, body 0, edge 0, shift 0; `spectral_shape_recipe.h:244–245`); mix 0, drive 0.5; fold 0; char 0 | 0 | 0.25 | 0 | **Round Dark Clean** |
| 3 | Phase 8 showcase B: `BassMorphTest::showcasePair().second` (`bass_morph_state_helpers.h:190–216`) | recipe = `kHeavy` (depth 0.5, body 0.3, shift −1; `spectral_shape_recipe.h:254–255`) with edge set to 1.0; mix 1, drive 1; fold 80; char 0.5 | 0.4 | 0.5 | 1.0 (mix · drive) | **Full Warm Aggressive** |
| 4 | Constructed: every score exactly 0.33 | depth 1, body 0.66, edge 0.66, shift −1; characterAmount 0.33 (fold 0, mix 0) | 0.33 (== `0.33f`) | 0.33 (== `0.33f`) | 0.33 (== `0.33f`) | **Full Warm Driven** |
| 5 | Constructed: every score exactly 0.66 | depth 0.4, body 0.72, edge 0.72, shift 0.2; foldAmount 66 (char 0, mix 0) | 0.66 (== `0.66f`) | 0.66 (== `0.66f`) | 0.66 (== `0.66f`) | **Dense Bright Aggressive** |
| 6 | Constructed: every score 0.6599 (just below the upper threshold) | depth 0, body 0.3198, edge 1, shift −0.3604; characterAmount 0.6599 (fold 0, mix 0) | 0.6599 | 0.6599 | 0.6599 | **Full Warm Driven** |

- Case 4 fails an implementation that puts 0.33 in the lower band (it would give `Round Dark Clean`). Case 5 fails one that
  puts 0.66 in the middle band (`Full Warm Driven`). Case 6 fails one that rounds or uses a tolerance at 0.66
  (`Dense Bright Aggressive`).
- **Trap found while constructing case 5:** depth 0, body 0.32 evaluates to 0.65999997 in float32. That is below `0.66f`,
  so it reads `Full`, not `Dense`. Threshold states must use the inputs above, whose float results equal the literals.
- SC-027 (d) also requires that two endpoints with identical settings and different names give the same words, and that
  changing a field the table reads changes the words as the table says. Example: case 1 with edge 1.0 gives Colour 0.75,
  so the words become `Full Bright Clean`.

---

## 8. Lock dimming (FR-044; plan §4.7 `refreshAnchorDimming()`, `plan.md:1275–1280`)

- **Condition:** dimmed **if and only if** Anchor Morph Mode 1000 = `Hold` (engine `Lock`, index 0; `morph_params.h:170–180`)
  **and** Edit Target 1001 = `B` (index 1; `morph_params.h:181`).
- **Effect:** every control bound to 300 – 306, on both pages, gets `setAlphaValue(0.45f)`; otherwise `1.0f`.
- **Label:** the `CTextLabel` `Held at A` in the `section-sub-anchor` header is visible while dimmed and hidden otherwise.
- **Editability:** `setMouseEnabled` is never touched, so the controls stay editable. B's Anchor fields are stored but
  inaudible in that state (Phase 8 handoff 12.3, as FR-044 states, `spec.md:436–438`).
- The condition is re-evaluated on every change of 1000 or 1001 and on state load.

---

## 9. Mono compatibility (FR-024)

- **Controls that affect mono compatibility:** image mode 800, Spread 801, Gravity 802, Crossover 803, Curve 804, field
  Motion 805 – 809, and Character 600 – 602 **in WIDE**. All are on the Advanced page: 800 – 809 in Stereo & Output and
  600 – 602 in Shape & Character.
- Each says so in its tooltip: 800 – 809 carry the MONO-GUARANTEE literal and 600 – 602 the WIDE-CAVEAT literal (§5.1).
- The SPREAD curve (§6) shows the static width across frequency for the edited endpoint. In SOLID it is flat at 0.
- There is no low-end protection switch.
