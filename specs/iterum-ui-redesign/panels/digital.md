# Digital panel spec (template DigitalPanel, 900 x 480)

Authority: `design-system.md` (sections 2-8; 7.6 is this mode's row). Inputs: `audits/digital.md`, `shots/before/editor-digital.png`,
`inventory-before.json` (templates.DigitalPanel), uidesc lines 394-443. All numbers are VSTGUI points.
Coordinates: core cells and fieldsets are **panel-relative**; views inside a cell container or fieldset are **parent-relative**.
The panel title label "DIGITAL DELAY" is removed (design 4.11).

Reconciled 2026-10-28 against design-system.md 4.1, 4.4, 4.6, 7.6 and 11: the Wavefold Symmetry cell carries a centre
tick, Mod Waveform is a full cell (design 7.6 amended), no view carries `default-value`. `formatDigitalParam` already
prints every 6.4 format, so Digital has no 9.3 formatter row.

## 1. Template

`<template name="DigitalPanel" class="CViewContainer" origin="0, 0" size="900, 480" background-color="panel" transparent="false">`

Layout: core strip (3 cells, y 12-84), row 1 (y 96-276): CHARACTER, WAVEFOLD, MODULATION; row 2 (y 288-468): cell (12,288) empty,
cell (308,288) empty, STEREO pinned at (604,288). 4 standard sections (180 high); 3 control rows each at y = 24, 76, 128.

### Attribute defaults (design system, repeated so every view line can say "defaults")

- **Label** (`CTextLabel`): `font="label-font" font-color="text-dim" text-alignment="left" transparent="true"`.
- **Slider readout** (`CParamDisplay`): `font="value-font" font-color="text" text-alignment="right" back-color="~ TransparentCColor" frame-color="~ TransparentCColor" style-no-frame="true"`.
- **Slider** (`CSlider`): `orientation="horizontal" draw-frame="true" draw-back="true" draw-value="true" draw-frame-color="slider-frame" draw-back-color="slider-track" draw-value-color="accent" frame-width="1" transparent="true"`. Bipolar adds `draw-value-from-center="true"` and is followed by its centre tick.
- **Centre tick** (`CViewContainer`, design 4.4 / ruling 7): `background-color="text-dim" transparent="false" mouse-enabled="false"`, no children, 1 x 20 at the slider's centre x and y, declared immediately after the bipolar slider.
- No view carries `default-value` (design 4.1).
- **Segment** (`CSegmentButton`): `style="horizontal" selection-mode="kSingle" font="value-font" text-color="text" text-color-highlighted="text-light" frame-color="border" frame-width="1" round-radius="3" gradient="segment-bg" gradient-highlighted="segment-bg-selected"` + `segment-names`.
- **Menu** (`COptionMenu`): `font="value-font" font-color="text" back-color="slider-track" frame-color="border" frame-width="1" style-round-rect="true" round-rect-radius="3" text-inset="6, 0" menu-popup-style="true" mouse-enabled="true" wants-focus="true"`.
- **Fieldset** (`FieldsetContainer`): `fieldset-color="border" fieldset-title-color="text-dim" fieldset-font-size="11" fieldset-radius="4" fieldset-line-width="1" transparent="true"`.
- **Knob cell container**: `CViewContainer transparent="true"`.
- Knob cell parts: `ArcKnob` `arc-color="accent" guide-color="slider-frame" arc-width="3"`; label as Label; readout `CParamDisplay font="readout-font" font-color="text" text-alignment="left" back-color="~ TransparentCColor" frame-color="~ TransparentCColor" style-no-frame="true"`; Sync `ToggleButton title="Sync" title-position="right" icon-style="power" icon-size="0.6" font="label-font" on-color="accent" off-color="text-dim"`.

Rows below give only what differs from these defaults ("defaults" = none differ).

## 2. View list (uidesc nesting order)

### 2.1 Core strip (3 knob-cell containers, panel-relative)

| # | Parent | Class | Origin | Size | control-tag | Label / title | Differs from defaults |
|---|---|---|---|---|---|---|---|
| 1 | DigitalPanel | CViewContainer | 12, 12 | 284, 72 | none | | `sub-controller="DigitalTimeSyncController"` (Time cell) |
| 1a | 1 | ArcKnob | 8, 8 | 56, 56 | DigitalDelayTime | | defaults (no `default-value`) |
| 1b | 1 | CTextLabel | 76, 10 | 120, 16 | none | "Time" | defaults |
| 1c | 1 | CParamDisplay | 76, 30 | 120, 24 | DigitalDelayTime | | knob-cell readout; shows the note value ("1/8") while synced, "501.0 ms" / "2.00 s" free (6.4) |
| 1d | 1 | ToggleButton | 200, 32 | 76, 22 | DigitalTimeMode | "Sync" | defaults (no `default-value`; the parameter registers Synced as its default, `digital_params.h`, so the editor opens this panel synced: the design 9.3 `verifyView` row and its `[iterum][ui]` test cover that) |
| 2 | DigitalPanel | CViewContainer | 308, 12 | 284, 72 | none | | no sub-controller |
| 2a | 2 | ArcKnob | 8, 8 | 56, 56 | DigitalFeedback | | defaults |
| 2b | 2 | CTextLabel | 76, 10 | 120, 16 | none | "Feedback" | defaults |
| 2c | 2 | CParamDisplay | 76, 30 | 120, 24 | DigitalFeedback | | knob-cell readout |
| 3 | DigitalPanel | CViewContainer | 604, 12 | 284, 72 | none | | no sub-controller |
| 3a | 3 | ArcKnob | 8, 8 | 56, 56 | DigitalMix | | defaults |
| 3b | 3 | CTextLabel | 76, 10 | 120, 16 | none | "Mix" | defaults |
| 3c | 3 | CParamDisplay | 76, 30 | 120, 24 | DigitalMix | | knob-cell readout |

Cell 1 holds exactly the knob + readout (tag DigitalDelayTime) and the Sync toggle (tag DigitalTimeMode), the three views
`DigitalTimeSyncController` collects. Sync ends at x = 276 of 284; label/readout end at 196, so no overlap with Sync.

### 2.2 CHARACTER (row 1, column 1)

Fieldset `FieldsetContainer origin="12, 96" size="284, 180" fieldset-title="CHARACTER"`. Children (fieldset-relative; full cell = 260 wide at x 12):

| # | Class | Origin | Size | control-tag | Label | Differs from defaults |
|---|---|---|---|---|---|---|
| 4 | CTextLabel | 12, 24 | 260, 16 | none | "Era" | defaults |
| 5 | CSegmentButton | 12, 44 | 260, 24 | DigitalEra | | `segment-names="Pristine,80s Digital,Lo-Fi"` (matches parameter strings; replaces stale "80s") |
| 6 | CTextLabel | 12, 76 | 196, 16 | **DigitalAgeLabel** (9902) | "Age" | defaults; tag kept for `VisibilityController` |
| 7 | CParamDisplay | 208, 76 | 64, 16 | DigitalAge | | slider readout |
| 8 | CSlider | 12, 98 | 260, 20 | DigitalAge | | defaults |
| 9 | CTextLabel | 12, 128 | 260, 16 | none | "Limiter" | defaults |
| 10 | CSegmentButton | 12, 148 | 260, 24 | DigitalLimiterCharacter | | `segment-names="Soft,Medium,Hard"` |

Views 6-8 (Age row, y 76-118) hide together while Era < 0.25 (Pristine); the row stays empty, nothing moves (design 5, 8.2).
All three are direct children of the fieldset; no container sits between them.

### 2.3 WAVEFOLD (row 1, column 2)

Fieldset `origin="308, 96" size="284, 180" fieldset-title="WAVEFOLD"`.

| # | Class | Origin | Size | control-tag | Label | Differs from defaults |
|---|---|---|---|---|---|---|
| 11 | CTextLabel | 12, 24 | 196, 16 | none | "Wavefold Amount" | defaults |
| 12 | CParamDisplay | 208, 24 | 64, 16 | DigitalWavefoldAmount | | slider readout |
| 13 | CSlider | 12, 46 | 260, 20 | DigitalWavefoldAmount | | defaults |
| 14 | CTextLabel | 12, 76 | 196, 16 | none | "Wavefold Symmetry" | defaults |
| 15 | CParamDisplay | 208, 76 | 64, 16 | DigitalWavefoldSymmetry | | slider readout |
| 16 | CSlider | 12, 98 | 260, 20 | DigitalWavefoldSymmetry | | bipolar: `draw-value-from-center="true"` (replaces the CKnob) |
| 16a | CViewContainer | 142, 98 | 1, 20 | none | | centre tick, declared after view 16 |
| 17 | CTextLabel | 12, 128 | 260, 16 | none | "Wavefold Type" | defaults |
| 18 | CSegmentButton | 12, 148 | 260, 24 | DigitalWavefoldType | | `segment-names="Simple,Serge,Buchla,Lockhart"` (65 pt per segment; replaces stale "Lock") |

### 2.4 MODULATION (row 1, column 3)

Fieldset `origin="604, 96" size="284, 180" fieldset-title="MODULATION"`.

| # | Class | Origin | Size | control-tag | Label | Differs from defaults |
|---|---|---|---|---|---|---|
| 19 | CTextLabel | 12, 24 | 196, 16 | none | "Mod Depth" | defaults |
| 20 | CParamDisplay | 208, 24 | 64, 16 | DigitalModDepth | | slider readout |
| 21 | CSlider | 12, 46 | 260, 20 | DigitalModDepth | | defaults |
| 22 | CTextLabel | 12, 76 | 196, 16 | none | "Mod Rate" | defaults |
| 23 | CParamDisplay | 208, 76 | 64, 16 | DigitalModRate | | slider readout |
| 24 | CSlider | 12, 98 | 260, 20 | DigitalModRate | | defaults |
| 25 | CTextLabel | 12, 128 | 260, 16 | none | "Mod Waveform" | defaults (full cell label) |
| 26 | COptionMenu | 12, 148 | 260, 24 | DigitalModWaveform | | menu defaults (6 entries from the parameter; full cell, design 4.6 / 7.6) |

### 2.5 STEREO (row 2, column 3, pinned)

Fieldset `origin="604, 288" size="284, 180" fieldset-title="STEREO"`.

| # | Class | Origin | Size | control-tag | Label | Differs from defaults |
|---|---|---|---|---|---|---|
| 27 | CTextLabel | 12, 24 | 196, 16 | none | "Width" | defaults |
| 28 | CParamDisplay | 208, 24 | 64, 16 | DigitalWidth | | slider readout |
| 29 | CSlider | 12, 46 | 260, 20 | DigitalWidth | | defaults |

Cells (12,288,284,180) and (308,288,284,180) have no view: plain panel background (design 2.4, 7.6).

## 3. Mapping: every bound control-tag in today's DigitalPanel

| Today's tag (views) | New place | Label |
|---|---|---|
| DigitalDelayTime (CParamDisplay, ArcKnob) | 1a + 1c, core cell 1 (inside the sub-controller container) | Time |
| DigitalTimeMode (ToggleButton "sync") | 1d, core cell 1 | Sync |
| DigitalFeedback (CParamDisplay, CSlider) | 2a + 2c, ArcKnob + readout, core cell 2 | Feedback |
| DigitalMix (CParamDisplay, CSlider) | 3a + 3c, ArcKnob + readout, core cell 3 | Mix |
| DigitalEra (CSegmentButton) | 5, CHARACTER row 24 | Era |
| DigitalAgeLabel 9902 (CTextLabel) | 6, CHARACTER row 76 | Age |
| DigitalAge 606 (CParamDisplay, CSlider) | 7 + 8, CHARACTER row 76 | Age |
| DigitalLimiterCharacter (CSegmentButton) | 10, CHARACTER row 128 | Limiter |
| DigitalWavefoldAmount (CParamDisplay, CSlider) | 12 + 13, WAVEFOLD row 24 | Wavefold Amount |
| DigitalWavefoldSymmetry (CParamDisplay, CKnob) | 15 + 16, WAVEFOLD row 76, bipolar CSlider | Wavefold Symmetry |
| DigitalWavefoldType (CSegmentButton) | 18, WAVEFOLD row 128 | Wavefold Type |
| DigitalModDepth (CParamDisplay, CSlider) | 20 + 21, MODULATION row 24 | Mod Depth |
| DigitalModRate (CParamDisplay, CSlider) | 23 + 24, MODULATION row 76 | Mod Rate |
| DigitalModWaveform (COptionMenu) | 26, MODULATION row 128 full | Mod Waveform |
| DigitalWidth (CParamDisplay, CSlider) | 28 + 29, STEREO row 24 | Width |
| DigitalNoteValue 602 (no view today) | none; reached by the knob/readout tag swap of `DigitalTimeSyncController` | n/a |

Unmapped: none. Every tag that has an interactive view today keeps one (lost-control check): DigitalDelayTime (ArcKnob),
DigitalTimeMode, DigitalFeedback (ArcKnob), DigitalMix (ArcKnob), DigitalEra, DigitalAge (CSlider), DigitalLimiterCharacter,
DigitalWavefoldAmount, DigitalWavefoldSymmetry (CSlider), DigitalWavefoldType, DigitalModDepth, DigitalModRate,
DigitalModWaveform, DigitalWidth. Control-tag names unchanged.

## 4. Geometry check

- Panel 900 x 480; core cells x 12-296, 308-592, 604-888, y 12-84; fieldsets y 96-276 (row 1), 288-468 (row 2); all inside the 12-pt margins (right edge 888, bottom 468).
- Siblings: core cells and fieldsets do not touch (gutter 12, strip-to-row gap 12). No stacked variants in this panel.
- Inside knob cells: knob 8-64 x 8-64; label 76-196 x 10-26; readout 76-196 x 30-54; Sync 200-276 x 32-54. No overlap; all inside 284 x 72.
- Inside fieldsets (284 x 180): row 24: label 12-208 x 24-40, readout 208-272 x 24-40, slider 12-272 x 46-66; row 76: same offsets, slider y 98-118; row 128: segments/menu y 148-172, labels y 128-144. Max extent x 272, y 172 (8 pt bottom pad). Label and readout abut at x = 208 and do not overlap.
- Age views (6-8) lie within row 76 and no other view uses y 76-118.
- The centre tick 16a (142-143 x 98-118) lies inside slider 16: the one intersecting pair, exempted by the inventory
  (design 9.5, third row).
- Counts for the structure test (design 9.4): 4 plain `CViewContainer`s (three core cells and the tick), 4
  `FieldsetContainer`s.

## 5. Deviations

- **Feedback and Mix change from sliders to knobs.** This is design 3.1/4.1 (core strip), not a departure, but listed because the audit had them as sliders. No view in the panel carries `default-value` (design 4.1: VST3Editor binds the registered defaults, DigitalDelayTime 0.050, DigitalFeedback 0.333, DigitalMix 0.5, TimeMode Synced); the Time knob's and Sync toggle's old attributes are dropped.
- **Wavefold Symmetry becomes a CSlider** (bipolar cell, 4.4, with the centre tick) instead of today's CKnob, per principle 2. Labels "Wavefold Amount / Symmetry / Type" are the full names of design 6.2.
- **Mod Waveform menu** takes a full cell (260), the same width as Granular's Pitch Quantize and MultiTap's Spatial Pattern and Note Value menus. Design 4.6 and 7.6 row 609 were amended on 2026-10-28 to say so (section 11); not a deviation.
- No other deviations; no control lacked a slot.
