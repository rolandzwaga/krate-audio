# Granular panel spec (template GranularPanel, 900 x 480)

Governed by `design-system.md` sections 2-8 (7.1 is the placement table). All numbers are VSTGUI points.
Rects are `x, y, w, h`. "Panel" rects are template-relative; views inside a fieldset are **fieldset-relative**
(cell origin = x 12, row y; row pitch 52). Body cells are direct children of their fieldset, never wrapped in a
container (design-system 4, 8.1). Nothing in GranularPanel is hidden by a visibility controller.

Reconciled 2026-10-28 against design-system.md sections 4.1, 4.4, 6.2, 6.4, 7.1, 9.3 and 10 (rulings): the Freeze
toggle sits in core cell 3 beside Mix, the Pitch cell carries a centre tick, no view carries `default-value`, and the
readout formats are the 6.4 standard.

Defaults that every view below inherits from design-system section 4 and are therefore not repeated per row:

- Label: `CTextLabel font="label-font" font-color="text-dim" text-alignment="left" transparent="true"`.
- Slider readout: `CParamDisplay font="value-font" font-color="text" text-alignment="right" back-color="~ TransparentCColor" frame-color="~ TransparentCColor" style-no-frame="true"`, same `control-tag` as its slider. The old `value-precision="0"` is dropped. The text comes from `formatGranularParam`, which today returns bare numbers (no unit) for 100, 101, 102, 103, 104-107, 109, 110, 115, 117 and 118; design-system 9.3 ("Complete the Granular formatter", restated in section 4 below) adds the 6.4 formats, and the readouts are not final until it lands.
- Slider: `CSlider orientation="horizontal" draw-frame="true" draw-back="true" draw-value="true" draw-frame-color="slider-frame" draw-back-color="slider-track" draw-value-color="accent" frame-width="1" transparent="true"`. This replaces the defective `back-color` / `value-color` / `frame-color`; none of those three may remain.
- Bipolar slider: the slider above plus `draw-value-from-center="true"`, followed by its centre tick (design 4.4): `CViewContainer` (no children) at cell x + w/2, slider y, size 1 x 20, `background-color="text-dim" transparent="false" mouse-enabled="false"`, declared immediately after the slider.
- No view carries `default-value` (design 4.1: VST3Editor sets every bound control's default from the parameter).
- Segment: `CSegmentButton style="horizontal" selection-mode="kSingle" font="value-font" text-color="text" text-color-highlighted="text-light" frame-color="border" frame-width="1" round-radius="3" gradient="segment-bg" gradient-highlighted="segment-bg-selected"`.
- Menu: `COptionMenu font="value-font" font-color="text" back-color="slider-track" frame-color="border" frame-width="1" style-round-rect="true" round-rect-radius="3" text-inset="6, 0" menu-popup-style="true" mouse-enabled="true" wants-focus="true"`.
- Toggle: `ToggleButton title-position="right" icon-style="power" icon-size="0.6" font="label-font" on-color="accent" off-color="text-dim"`.

## 1. Views in uidesc nesting order

### 1.0 Template

| # | Class | Origin | Size | Attributes |
|---|---|---|---|---|
| 1 | template `GranularPanel`, class `CViewContainer` | 0, 0 | 900, 480 | `background-color="panel" transparent="false"`. Size changes from 860 x 400. No panel title label (the old "GRANULAR DELAY" CTextLabel is removed). |

### 1.1 Core strip (panel-relative; three container cells, y = 12, each 284 x 72)

| # | Class | Origin | Size | Control-tag | Label / title | Attributes that matter |
|---|---|---|---|---|---|---|
| 2 | CViewContainer (Time cell) | 12, 12 | 284, 72 | none | | `transparent="true" sub-controller="GranularTimeSyncController"` |
| 3 | ArcKnob | 8, 8 | 56, 56 | GranularDelayTime | | `arc-color="accent" guide-color="slider-frame" arc-width="3"`; no `default-value` |
| 4 | CTextLabel | 76, 10 | 120, 16 | none | "Time" | label defaults |
| 5 | CParamDisplay | 76, 30 | 120, 24 | GranularDelayTime | | `font="readout-font" font-color="text" text-alignment="left"` plus transparent back/frame, `style-no-frame="true"` |
| 6 | ToggleButton | 200, 32 | 76, 22 | GranularTimeMode | title "Sync" | toggle defaults; no `default-value` |
| 7 | CViewContainer (Feedback cell) | 308, 12 | 284, 72 | none | | `transparent="true"` (no sub-controller) |
| 8 | ArcKnob | 8, 8 | 56, 56 | GranularFeedback | | same arc attributes as view 3 |
| 9 | CTextLabel | 76, 10 | 120, 16 | none | "Feedback" | label defaults |
| 10 | CParamDisplay | 76, 30 | 120, 24 | GranularFeedback | | as view 5 |
| 11 | CViewContainer (Mix cell) | 604, 12 | 284, 72 | none | | `transparent="true"` |
| 12 | ArcKnob | 8, 8 | 56, 56 | GranularDryWet | | same arc attributes as view 3 |
| 13 | CTextLabel | 76, 10 | 120, 16 | none | "Mix" | label defaults |
| 14 | CParamDisplay | 76, 30 | 120, 24 | GranularDryWet | | as view 5 |
| 14a | ToggleButton | 200, 32 | 76, 22 | GranularFreeze | title "Freeze" | toggle defaults; the core-3 toggle slot of design 4.1 / 6.2 (replaces the CCheckBox and its label) |

The knob, the readout and the Sync toggle all live inside the Time cell (views 3-6), which the
`GranularTimeSyncController` needs (`verifyView` collects the two GranularDelayTime controls and the TimeMode
toggle; it retags knob and readout to GranularNoteValue while synced, from the panel's creation on once design 9.3
"Read the Sync state at panel build" lands). GranularNoteValue has no view of its own. The Freeze toggle (14a) sits in
the Mix cell's toggle slot, the same cell-relative rect as Sync in the Time cell; no controller touches it.
Fit: readout right edge 196 < toggle left edge 200; toggle right edge 276 <= 284; knob 8..64 < label x 76.

### 1.2 Section GRAIN (row 1, column 1)

| # | Class | Origin (panel / fieldset-rel) | Size | Control-tag | Label / title | Attributes |
|---|---|---|---|---|---|---|
| 15 | FieldsetContainer | 12, 96 | 284, 180 | none | "GRAIN" | `fieldset-title="GRAIN" fieldset-color="border" fieldset-title-color="text-dim" fieldset-font-size="11" fieldset-radius="4" fieldset-line-width="1" transparent="true"` |
| 16 | CTextLabel | 12, 24 | 196, 16 | none | "Grain Size" | label defaults |
| 17 | CParamDisplay | 208, 24 | 64, 16 | GranularGrainSize | | readout defaults |
| 18 | CSlider | 12, 46 | 260, 20 | GranularGrainSize | | slider defaults |
| 19 | CTextLabel | 12, 76 | 196, 16 | none | "Density" | label defaults |
| 20 | CParamDisplay | 208, 76 | 64, 16 | GranularDensity | | readout defaults |
| 21 | CSlider | 12, 98 | 260, 20 | GranularDensity | | slider defaults |
| 22 | CTextLabel | 12, 128 | 260, 16 | none | "Grain Envelope" | label defaults |
| 23 | CSegmentButton | 12, 148 | 260, 24 | GranularEnvelopeType | | `segment-names="Hann,Trapezoid,Sine,Blackman"` (65 per segment) plus segment defaults |

### 1.3 Section PITCH (row 1, column 2)

| # | Class | Origin (fieldset-rel) | Size | Control-tag | Label / title | Attributes |
|---|---|---|---|---|---|---|
| 24 | FieldsetContainer | 308, 96 (panel) | 284, 180 | none | "PITCH" | section container attributes as view 15 |
| 25 | CTextLabel | 12, 24 | 196, 16 | none | "Pitch" | label defaults |
| 26 | CParamDisplay | 208, 24 | 64, 16 | GranularPitch | | readout defaults |
| 27 | CSlider | 12, 46 | 260, 20 | GranularPitch | | bipolar slider (`draw-value-from-center="true"`, -24..+24 st, zero at 0.5) |
| 27a | CViewContainer (centre tick) | 142, 46 | 1, 20 | none | | `background-color="text-dim" transparent="false" mouse-enabled="false"`, no children; declared after view 27 (design 4.4, ruling 7) |
| 28 | CTextLabel | 12, 76 | 260, 16 | none | "Pitch Quantize" | label defaults |
| 29 | COptionMenu | 12, 96 | 260, 24 | GranularPitchQuant | | menu defaults; strings come from the parameter (5 entries, no list in the uidesc) |
| 30 | CTextLabel | 12, 128 | 196, 16 | none | "Pitch Spray" | label defaults |
| 31 | CParamDisplay | 208, 128 | 64, 16 | GranularPitchSpray | | readout defaults |
| 32 | CSlider | 12, 150 | 260, 20 | GranularPitchSpray | | slider defaults |

### 1.4 Section SPRAY (row 1, column 3)

| # | Class | Origin (fieldset-rel) | Size | Control-tag | Label / title | Attributes |
|---|---|---|---|---|---|---|
| 33 | FieldsetContainer | 604, 96 (panel) | 284, 180 | none | "SPRAY" | section container attributes as view 15 |
| 34 | CTextLabel | 12, 24 | 196, 16 | none | "Position Spray" | label defaults |
| 35 | CParamDisplay | 208, 24 | 64, 16 | GranularPositionSpray | | readout defaults |
| 36 | CSlider | 12, 46 | 260, 20 | GranularPositionSpray | | slider defaults |
| 37 | CTextLabel | 12, 76 | 196, 16 | none | "Pan Spray" | label defaults |
| 38 | CParamDisplay | 208, 76 | 64, 16 | GranularPanSpray | | readout defaults |
| 39 | CSlider | 12, 98 | 260, 20 | GranularPanSpray | | slider defaults |
| 40 | CTextLabel | 12, 128 | 196, 16 | none | "Reverse Probability" | label defaults |
| 41 | CParamDisplay | 208, 128 | 64, 16 | GranularReverseProb | | readout defaults |
| 42 | CSlider | 12, 150 | 260, 20 | GranularReverseProb | | slider defaults |

### 1.5 Section CHARACTER (row 2, column 1)

| # | Class | Origin (fieldset-rel) | Size | Control-tag | Label / title | Attributes |
|---|---|---|---|---|---|---|
| 43 | FieldsetContainer | 12, 288 (panel) | 284, 180 | none | "CHARACTER" | section container attributes as view 15 |
| 44 | CTextLabel | 12, 24 | 196, 16 | none | "Texture" | label defaults |
| 45 | CParamDisplay | 208, 24 | 64, 16 | GranularTexture | | readout defaults |
| 46 | CSlider | 12, 46 | 260, 20 | GranularTexture | | slider defaults |
| 47 | CTextLabel | 12, 76 | 196, 16 | none | "Timing Jitter" | label defaults |
| 48 | CParamDisplay | 208, 76 | 64, 16 | GranularJitter | | readout defaults |
| 49 | CSlider | 12, 98 | 260, 20 | GranularJitter | | slider defaults |

Row 128 of CHARACTER is empty bordered space: the Freeze toggle moved to the core strip (view 14a; design 6.2,
section 11 "Freeze in the core strip").

### 1.6 Section STEREO (row 2, column 3, pinned)

| # | Class | Origin (fieldset-rel) | Size | Control-tag | Label / title | Attributes |
|---|---|---|---|---|---|---|
| 51 | FieldsetContainer | 604, 288 (panel) | 284, 180 | none | "STEREO" | section container attributes as view 15 |
| 52 | CTextLabel | 12, 24 | 196, 16 | none | "Width" | label defaults |
| 53 | CParamDisplay | 208, 24 | 64, 16 | GranularStereoWidth | | readout defaults |
| 54 | CSlider | 12, 46 | 260, 20 | GranularStereoWidth | | slider defaults |

Cell (308, 288, 284, 180) is deliberately empty panel background (design-system 2.4, 7.1). 55 views in the
template in total (views 2-49, 51-54, 14a and 27a, plus the template itself); 4 of them are plain `CViewContainer`s
(three core cells and the tick), 5 are `FieldsetContainer`s.

## 2. Mapping: every bound control-tag in today's GranularPanel

| Control-tag (id) | Today | New label | Where | Widget |
|---|---|---|---|---|
| GranularDelayTime (102) | ArcKnob 45 + readout in TIME & MIX sub-controller container | Time | core cell 1, view 3 + readout view 5 | ArcKnob 56 |
| GranularTimeMode (113) | ToggleButton "sync" 7 pt | Sync | core cell 1, view 6 (inside the sub-controller container) | ToggleButton |
| GranularNoteValue (114) | no view (knob swap) | none | reached through the knob/readout retag of views 3 and 5; still has no view | none |
| GranularFeedback (109) | CSlider in TIME & MIX | Feedback | core cell 2, views 8 + 10 | ArcKnob |
| GranularDryWet (110) | CSlider "Dry/Wet" | Mix | core cell 3, views 12 + 14 | ArcKnob |
| GranularGrainSize (100) | CSlider "Size" | Grain Size | GRAIN row 24, views 17-18 | slider |
| GranularDensity (101) | CSlider | Density | GRAIN row 76, views 20-21 | slider |
| GranularEnvelopeType (112) | CSegmentButton "Envelope" | Grain Envelope | GRAIN row 128, view 23 | segment (4) |
| GranularPitch (103) | CSlider | Pitch | PITCH row 24, views 26-27 | bipolar slider |
| GranularPitchQuant (116) | CSegmentButton (5) | Pitch Quantize | PITCH row 76, view 29 | COptionMenu |
| GranularPitchSpray (104) | CSlider "Pitch" | Pitch Spray | PITCH row 128, views 31-32 | slider |
| GranularPositionSpray (105) | CSlider "Pos" | Position Spray | SPRAY row 24, views 35-36 | slider |
| GranularPanSpray (106) | CSlider "Pan" | Pan Spray | SPRAY row 76, views 38-39 | slider |
| GranularReverseProb (107) | CSlider "Rev" | Reverse Probability | SPRAY row 128, views 41-42 | slider |
| GranularTexture (117) | CSlider "Tex" | Texture | CHARACTER row 24, views 45-46 | slider |
| GranularJitter (115) | CSlider "Jitter" in GRAIN PARAMETERS | Timing Jitter | CHARACTER row 76, views 48-49 | slider |
| GranularFreeze (108) | CCheckBox + label | Freeze | core cell 3, toggle slot, view 14a | ToggleButton |
| GranularStereoWidth (118) | CSlider "Wid" | Width | STEREO row 24, views 53-54 | slider |

Unmapped: none. Each of the 17 tags that has a view today keeps an interactive view (the `lost-control` check:
ArcKnob, CSlider, CSegmentButton/COptionMenu, ToggleButton), and 114 stays viewless.

## 3. Coordinate checks

- Core cells: x 12..296, 308..592, 604..888, y 12..84. No sibling overlap (12 pt gutters); 888 + 12 = 900 and
  84 < 96 (section row 1 starts 12 below).
- Sections: row 1 y 96..276 at x 12..296, 308..592, 604..888; row 2 y 288..468 at x 12..296 and 604..888.
  Row 1 to row 2 gap 12 (276 to 288); bottom margin 480 - 468 = 12. All inside 900 x 480; no overlaps.
- Inside a 284 x 180 fieldset: cells x 12..272 (260 wide, right pad 12). Rows: slider cell 24..66 (label 24..40,
  slider 46..66), 76..118, 128..170 (pad 10). Segment/menu cells end at row + 44: 24..68, 76..120,
  128..172 (pad 8). Gaps between rows 8 or more; no vertical overlap.
- Readout (208..272) abuts label (12..208): label right edge = readout left edge, no overlap.
- The PITCH section stacks a slider cell, a menu cell and a slider cell: 46..66, 96..120, 150..170. The centre tick
  (142..143 x 46..66) lies inside the Pitch slider (12..272 x 46..66): the one intersecting pair in the template, and
  the pair the inventory exempts (design 9.5, third row).
- Core cell 3: knob 8..64, label/readout 76..196, Freeze toggle 200..276 x 32..54: the same fit as core cell 1.

## 4. Build note: complete `formatGranularParam` (design-system 9.3 row "Complete the Granular formatter")

`plugins/iterum/src/parameters/granular_params.h` `formatGranularParam` (lines 405-467) returns `"%.0f"`, `"%.1f"` or
`"%+.1f"` with no unit, so Time would read "500" here but "500.0 ms" in Digital, BBD and Reverse, and Feedback "0" where
Digital reads "0%". The change is controller formatting only; the mapping from normalized value is untouched, and the
processor, state, ranges and units metadata are frozen. The formats are the 6.4 standard, verbatim:

| Id | Tag | Today | 6.4 format | min / default / max |
|---|---|---|---|---|
| 100 | GranularGrainSize | `%.0f` (10 + n*490) | `"%.0f ms"` (body milliseconds, as Freeze's Grain Size) | "10 ms" / "100 ms" (0.184) / "500 ms" |
| 101 | GranularDensity | `%.1f` (1 + n*99) | `"%.1f Hz"` (density) | "1.0 Hz" / "10.0 Hz" (0.091) / "100.0 Hz" |
| 102 | GranularDelayTime | `%.0f` (n*2000) | core time: `"%.1f ms"` below 1000 ms, `"%.2f s"` from 1000 ms | "0.0 ms" / "500.0 ms" (0.25) / "2.00 s" |
| 103 | GranularPitch | `%+.1f` | `"%+.1f st"` | "-24.0 st" / "+0.0 st" / "+24.0 st" |
| 104, 105, 106, 107 | PitchSpray, PositionSpray, PanSpray, ReverseProb | `%.0f` (n*100) | `"%.0f%%"` | "0%" / "0%" / "100%" |
| 109 | GranularFeedback | `%.0f` (n*120) | `"%.0f%%"` of n*120, as Digital formats its Feedback | "0%" / "0%" / "120%" |
| 110 | GranularDryWet (Mix) | `%.0f` | `"%.0f%%"` | "0%" / "50%" / "100%" |
| 115, 117, 118 | Jitter, Texture, StereoWidth | `%.0f` | `"%.0f%%"` | "0%" / "50%", "0%", "100%" / "100%" |

Ids 108 (On/Off), 112, 113, 114 and 116 already format as words and stay. Read the existing 109 branch and the Digital
Feedback branch before editing and match them exactly. Test first: a `[granular][format]` Catch2 case in plugin_tests
asserting the strings above at minimum, default and maximum of each id (defaults from `registerGranularParams`),
failing before the change.

## 5. Deviations

- None from the design system. Placement follows 7.1 verbatim, including the empty cell (308, 288) that the STEREO
  pin leaves between CHARACTER and STEREO, the Freeze toggle in core cell 3 (6.2) and the centre tick in the Pitch
  cell (4.4).
- No view carries `default-value` (design 4.1): VST3Editor binds every control's default to the parameter's registered
  default (GranularDelayTime 0.25, GranularFeedback 0, GranularDryWet 0.5, GranularTimeMode Free, GranularFreeze off),
  so the attribute would be overwritten.
