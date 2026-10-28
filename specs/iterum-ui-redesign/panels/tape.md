# Tape panel spec (template `TapePanel`)

Authority: `design-system.md` sections 2-8 (7.4 for placement). Everything is in VSTGUI points. The template is
**900 x 480**, `class="CViewContainer" origin="0, 0" background-color="panel" transparent="false"`, name unchanged.
Tape has no TimeMode / NoteValue parameter: core cell 1 is a knob cell without the Sync toggle and **no sub-controller**.
No controller code references a Tape tag or container (audit), so nothing here is load-bearing for visibility.
There is no panel title label (the old "TAPE DELAY" label is removed). Body cells are flat children of their fieldset (no cell containers).

Colour/font/gradient names are the tokens of design-system 5.2-5.4; no literal hex anywhere.

Reconciled 2026-10-28 against design-system.md 4.1, 4.4, 6.4, 7.4, 9.3 and 10: "Time" is the final label (ruling 4),
the three Head Pan cells carry a centre tick, no view carries `default-value`, and the Time readout gains the 6.4
seconds switch.

## 1. Views, in uidesc nesting order

Coordinates are relative to the parent in that row's "Parent" column. "DS 4.x" = the attribute set of that design-system section, applied to the letter; only differences are listed in the last column.

### 1.1 Core strip (3 containers, template-relative)

| # | View | Class | Origin | Size | control-tag | Label / title | Differs from DS defaults |
|---|---|---|---|---|---|---|---|
| 1 | core cell 1 | CViewContainer | 12, 12 | 284, 72 | none | | `transparent="true"`; no `sub-controller` (no TimeMode) |
| 1a | knob (parent 1) | ArcKnob | 8, 8 | 56, 56 | TapeMotorSpeed | | DS 4.1; no `default-value` (VST3Editor binds the registered 0.242) |
| 1b | label (parent 1) | CTextLabel | 76, 10 | 120, 16 | none | "Time" | DS 4.1; the label is final (ruling 4) |
| 1c | readout (parent 1) | CParamDisplay | 76, 30 | 120, 24 | TapeMotorSpeed | | DS 4.1; no Sync toggle in this cell; reads "499.2 ms" at the default and "2.00 s" at the maximum once DS 9.3 "Core time readouts switch to seconds" lands |
| 2 | core cell 2 | CViewContainer | 308, 12 | 284, 72 | none | | `transparent="true"` |
| 2a | knob | ArcKnob | 8, 8 | 56, 56 | TapeFeedback | | DS 4.1; no `default-value` (registered 0.333) |
| 2b | label | CTextLabel | 76, 10 | 120, 16 | none | "Feedback" | DS 4.1 |
| 2c | readout | CParamDisplay | 76, 30 | 120, 24 | TapeFeedback | | DS 4.1 |
| 3 | core cell 3 | CViewContainer | 604, 12 | 284, 72 | none | | `transparent="true"` |
| 3a | knob | ArcKnob | 8, 8 | 56, 56 | TapeMix | | DS 4.1; no `default-value` (registered 0.5) |
| 3b | label | CTextLabel | 76, 10 | 120, 16 | none | "Mix" | DS 4.1 |
| 3c | readout | CParamDisplay | 76, 30 | 120, 24 | TapeMix | | DS 4.1 |

### 1.2 MOTOR (12, 96, 284, 180)

`FieldsetContainer`, `fieldset-title="MOTOR"`, DS 4.11 attributes. Slider cells are full (260). Children are fieldset-relative.
Slider cell geometry (DS 4.3, cell origin (12, row), w = 260): label (12,row,196,16), readout (208,row,64,16), slider (12,row+22,260,20).

| View | Class | Origin | Size | control-tag | Label |
|---|---|---|---|---|---|
| label | CTextLabel | 12, 24 | 196, 16 | none | "Inertia" |
| readout | CParamDisplay | 208, 24 | 64, 16 | TapeMotorInertia | |
| slider | CSlider | 12, 46 | 260, 20 | TapeMotorInertia | |
| label | CTextLabel | 12, 76 | 196, 16 | none | "Wear" |
| readout | CParamDisplay | 208, 76 | 64, 16 | TapeWear | |
| slider | CSlider | 12, 98 | 260, 20 | TapeWear | |

Row 128 is unused (bordered empty space).

### 1.3 CHARACTER (308, 96, 284, 180)

`fieldset-title="CHARACTER"`. Same slider-cell geometry as 1.2.

| View | Class | Origin | Size | control-tag | Label |
|---|---|---|---|---|---|
| label | CTextLabel | 12, 24 | 196, 16 | none | "Saturation" |
| readout | CParamDisplay | 208, 24 | 64, 16 | TapeSaturation | |
| slider | CSlider | 12, 46 | 260, 20 | TapeSaturation | |
| label | CTextLabel | 12, 76 | 196, 16 | none | "Age" |
| readout | CParamDisplay | 208, 76 | 64, 16 | TapeAge | |
| slider | CSlider | 12, 98 | 260, 20 | TapeAge | |

### 1.4 SPLICE (604, 96, 284, 180)

`fieldset-title="SPLICE"`.

| View | Class | Origin | Size | control-tag | Label / title | Notes |
|---|---|---|---|---|---|---|
| toggle | ToggleButton | 12, 44 | 124, 24 | TapeSpliceEnabled | title "Splice" | DS 4.8 (half L, row 24; label row empty, the title is the label) |
| label | CTextLabel | 12, 76 | 196, 16 | none | "Splice Intensity" | |
| readout | CParamDisplay | 208, 76 | 64, 16 | TapeSpliceIntensity | | |
| slider | CSlider | 12, 98 | 260, 20 | TapeSpliceIntensity | | |

### 1.5 HEADS (12, 288, 876, 180)

`fieldset-title="HEADS"`. Three head rows at y = 24, 76, 128. Per row: toggle (third 1), Level slider cell (third 2, x 300, w 276), Pan bipolar cell (third 3, x 588, w 276).
Level cell: label (300,row,212,16), readout (512,row,64,16), slider (300,row+22,276,20). Pan cell: label (588,row,212,16), readout (800,row,64,16), slider (588,row+22,276,20) with `draw-value-from-center="true"`, followed by its centre tick (DS 4.4 / ruling 7): `CViewContainer` (no children) at (726,row+22,1,20), `background-color="text-dim" transparent="false" mouse-enabled="false"` (726 = 588 + 276/2).

| Row y | View | Class | Origin | Size | control-tag | Label / title |
|---|---|---|---|---|---|---|
| 24 | toggle | ToggleButton | 12, 44 | 120, 24 | TapeHead1Enabled | title "Head 1" |
| 24 | label | CTextLabel | 300, 24 | 212, 16 | none | "Level" |
| 24 | readout | CParamDisplay | 512, 24 | 64, 16 | TapeHead1Level | |
| 24 | slider | CSlider | 300, 46 | 276, 20 | TapeHead1Level | |
| 24 | label | CTextLabel | 588, 24 | 212, 16 | none | "Pan" |
| 24 | readout | CParamDisplay | 800, 24 | 64, 16 | TapeHead1Pan | |
| 24 | slider (bipolar) | CSlider | 588, 46 | 276, 20 | TapeHead1Pan | `draw-value-from-center="true"` |
| 24 | centre tick | CViewContainer | 726, 46 | 1, 20 | none | declared after the Pan slider |
| 76 | toggle | ToggleButton | 12, 96 | 120, 24 | TapeHead2Enabled | title "Head 2" |
| 76 | label | CTextLabel | 300, 76 | 212, 16 | none | "Level" |
| 76 | readout | CParamDisplay | 512, 76 | 64, 16 | TapeHead2Level | |
| 76 | slider | CSlider | 300, 98 | 276, 20 | TapeHead2Level | |
| 76 | label | CTextLabel | 588, 76 | 212, 16 | none | "Pan" |
| 76 | readout | CParamDisplay | 800, 76 | 64, 16 | TapeHead2Pan | |
| 76 | slider (bipolar) | CSlider | 588, 98 | 276, 20 | TapeHead2Pan | `draw-value-from-center="true"` |
| 76 | centre tick | CViewContainer | 726, 98 | 1, 20 | none | declared after the Pan slider |
| 128 | toggle | ToggleButton | 12, 148 | 120, 24 | TapeHead3Enabled | title "Head 3" |
| 128 | label | CTextLabel | 300, 128 | 212, 16 | none | "Level" |
| 128 | readout | CParamDisplay | 512, 128 | 64, 16 | TapeHead3Level | |
| 128 | slider | CSlider | 300, 150 | 276, 20 | TapeHead3Level | |
| 128 | label | CTextLabel | 588, 128 | 212, 16 | none | "Pan" |
| 128 | readout | CParamDisplay | 800, 128 | 64, 16 | TapeHead3Pan | |
| 128 | slider (bipolar) | CSlider | 588, 150 | 276, 20 | TapeHead3Pan | `draw-value-from-center="true"` |
| 128 | centre tick | CViewContainer | 726, 150 | 1, 20 | none | declared after the Pan slider |

Attribute sets (not repeated per row): sliders DS 4.3 (`orientation="horizontal"`, `draw-frame/back/value="true"`, `draw-frame-color="slider-frame"`, `draw-back-color="slider-track"`, `draw-value-color="accent"`, `frame-width="1"`, `transparent="true"`); labels `label-font`/`text-dim`/left/transparent; readouts `value-font`/`text`/right/transparent colours/`style-no-frame="true"`; toggles `icon-style="power"`, `icon-size="0.6"`, `title-position="right"`, `font="label-font"`, `on-color="accent"`, `off-color="text-dim"`; fieldsets DS 4.11. Core-cell views use DS 4.1 (readout-font 15 bold for the knob readouts, `arc-color="accent"`, `guide-color="slider-frame"`, `arc-width="3"`).

XML order: core cells 1-3, MOTOR, CHARACTER, SPLICE (row 1), HEADS (row 2).

## 2. Geometry check

- Core cells: x 12-296, 308-592, 604-888, y 12-84; gutters 12; children inside 284 x 72 (knob 8-64, label/readout end at x 196, y 54 max).
- Row 1 sections y 96-276 at x 12-296, 308-592, 604-888; row 2 HEADS y 288-468, x 12-888. No sibling overlaps (cores end y 84, row 1 starts 96, ends 276, row 2 starts 288); everything inside 900 x 480 with a 12 margin.
- Inside 284-wide sections: widest child right edge = 12 + 260 = 272 <= 284; lowest = 98 + 20 = 118 <= 180.
- Inside HEADS (876 x 180): widest right edge 588 + 276 = 864 <= 876; lowest 150 + 20 = 170 <= 180 (bottom pad 10 >= 8). Within a row: toggle 12-132, Level 300-576, Pan 588-864, no overlap; label (212) + readout (64) = 276 per cell.
- Heads rows: toggles at 44 / 96 / 148 (h 24) clear of the next row label at 76 / 128 (toggle ends 68 / 120 < 76 / 128).
- The three centre ticks (726-727 x 46-66 / 98-118 / 150-170) each lie inside their Pan slider: the only intersecting
  pairs, exempted by the inventory (DS 9.5, third row).
- Counts for the structure test (DS 9.4): 6 plain `CViewContainer`s (three core cells, three ticks), 4
  `FieldsetContainer`s.

## 3. Mapping: every bound control-tag of today's TapePanel

| Tag (id) | Today | New place |
|---|---|---|
| TapeMotorSpeed (400) | CSlider + CParamDisplay, "Spd", CHARACTER | core cell 1, ArcKnob (1a) + readout (1c), label "Time" |
| TapeFeedback (407) | CSlider + readout, TIME & MIX | core cell 2, ArcKnob (2a) + readout (2c) |
| TapeMix (408) | CSlider + readout, TIME & MIX | core cell 3, ArcKnob (3a) + readout (3c) |
| TapeMotorInertia (401) | "Iner", CHARACTER | MOTOR row 24, slider cell "Inertia" |
| TapeWear (402) | CHARACTER | MOTOR row 76, slider cell "Wear" |
| TapeSaturation (403) | "Sat", CHARACTER | CHARACTER row 24, "Saturation" |
| TapeAge (404) | CHARACTER | CHARACTER row 76, "Age" |
| TapeSpliceEnabled (405) | CCheckBox, SPLICE | SPLICE row 24, ToggleButton "Splice" |
| TapeSpliceIntensity (406) | "Intens", SPLICE | SPLICE row 76, slider cell "Splice Intensity" |
| TapeHead1Enabled (410) | CCheckBox, TAPE HEADS | HEADS row 24, ToggleButton "Head 1" |
| TapeHead1Level (413) | CSlider, no readout | HEADS row 24 third 2, slider + new readout |
| TapeHead1Pan (416) | CSlider, no readout | HEADS row 24 third 3, bipolar slider + new readout |
| TapeHead2Enabled (411) | CCheckBox | HEADS row 76, ToggleButton "Head 2" |
| TapeHead2Level (414) | CSlider | HEADS row 76 third 2 |
| TapeHead2Pan (417) | CSlider | HEADS row 76 third 3, bipolar |
| TapeHead3Enabled (412) | CCheckBox | HEADS row 128, ToggleButton "Head 3" |
| TapeHead3Level (415) | CSlider | HEADS row 128 third 2 |
| TapeHead3Pan (418) | CSlider | HEADS row 128 third 3, bipolar |

18 tags (400-408, 410-418), 18 mapped, **unmapped: none**. Every tag that has an interactive view today (all 18) keeps one (knob / slider / toggle). `Gain` and the shared views are in the Editor template, outside this panel.

## 4. Deviations and notes

- Head toggles are 120 wide, not a half cell (124 wide), and sit at x 12 (third 1): per design-system 7.4, the HEADS section is a 3-column grid where the toggle takes column 1 (a toggle title "Head 1" needs only ~120). SPLICE's toggle uses the standard half-L 124.
- The Level and Pan labels repeat on every head row (labels are per cell, DS 4.3) instead of one column header; this fixes the old "headers below the rows" defect (audit 3).
- Dimming of Splice Intensity / Head 2-3 controls when their toggle is off (audit issue 9) is not done: DS 5.6 states no parameter is disabled by another and no visibility controller is added.
- Head pan readout (L/R/C), Level in dB and every percent and body-ms readout come from the existing `formatTapeParam`
  and already match DS 6.4. The one Tape change in DS 9.3 is the core time's seconds switch for 400 ("Core time
  readouts switch to seconds", `tape_params.h:350`): "20.0 ms" / "499.2 ms" / "2.00 s", test-first `[tape][format]`.
- No view carries `default-value` (DS 4.1); the values the first draft listed (0.2424, 0.3333, 0.5) are the registered
  defaults VST3Editor binds anyway.
- Rows 128 of MOTOR / CHARACTER / SPLICE are empty bordered space, as DS principle 6 allows. The core cell 1 label is
  "Time" (ruling 4, DS 10); "Motor Speed" is no longer an option.
