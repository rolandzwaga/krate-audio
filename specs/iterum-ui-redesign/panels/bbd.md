# BBD panel spec (template `BBDPanel`)

Governed by `design-system.md` sections 2-8 (section 7.5 is the mapping this spec expands). All numbers are VSTGUI
points. Rects are `x, y, w, h`. Where this file and the design system disagree, the design system wins.

Replaces today's `BBDPanel` (860 x 400, 25 views, 14 bound; `inventory-before.json` > templates.BBDPanel).

## 1. Template

| Item | Value |
|---|---|
| Template name | `BBDPanel` (unchanged) |
| Class / origin / size | `CViewContainer`, `0, 0`, **`900, 480`** |
| Attributes | `background-color="panel"`, `transparent="false"` |
| Panel title label ("BBD DELAY") | removed (design 4.11) |
| Sections | core strip (3 cell containers), CHIP (tall, spans columns 1-2, 12,96,580,372), MODULATION (standard, column 3, design 2.4 / 7.5) |
| Empty cells | CHIP rows 128-284 are bordered empty space (principle 6). Below MODULATION, (604,288,284,180) is plain panel background, no fieldset: BBD has no Width, so the STEREO cell that Digital and PingPong fill stays empty here (design 2.4). That is the only plain cell. |

No `VisibilityController` or `ContainerVisibilityController` references a BBD tag, so nothing in this panel is
conditional. Reconciled 2026-10-28 against design-system.md 4.1, 6.4, 7.5, 9.3 and 11: CHIP follows 7.5 (tall section
spanning two columns; Era row 24 full 556, Age row 76 half L 272), MODULATION is the standard section, no view carries
`default-value`.

## 2. View list (uidesc nesting order = visual order)

Rects of children are relative to their immediate parent. "Defaults" below are the design-system attribute sets
(4.1 knob cell, 4.3 slider cell, 4.5 segment cell, 4.11 section); only attributes that complete or differ from
them are called out.

### 2.1 Core strip

#### Core 1 - Time cell (design 4.1, 4.9)

| # | Template | Class | Origin | Size | control-tag | Label / title | Attributes |
|---|---|---|---|---|---|---|---|
| 1 | BBDPanel | `CViewContainer` | 12, 12 | 284, 72 | none | none | `sub-controller="BBDTimeSyncController"`, `transparent="true"` |
| 1.1 | BBDPanel > #1 | `ArcKnob` | 8, 8 | 56, 56 | `BBDDelayTime` | none | `arc-color="accent"`, `guide-color="slider-frame"`, `arc-width="3"`; no `default-value` (design 4.1) |
| 1.2 | BBDPanel > #1 | `CTextLabel` | 76, 10 | 120, 16 | none | `Time` | `font="label-font"`, `font-color="text-dim"`, `text-alignment="left"`, `transparent="true"` |
| 1.3 | BBDPanel > #1 | `CParamDisplay` | 76, 30 | 120, 24 | `BBDDelayTime` | none | `font="readout-font"`, `font-color="text"`, `text-alignment="left"`, `back-color="~ TransparentCColor"`, `frame-color="~ TransparentCColor"`, `style-no-frame="true"`; today's `value-precision="0"` is dropped (the text is the controller's string: "300.3 ms", "1.00 s" at the maximum once design 9.3 "Core time readouts switch to seconds" lands) |
| 1.4 | BBDPanel > #1 | `ToggleButton` | 200, 32 | 76, 22 | `BBDTimeMode` | `Sync` | `title-position="right"`, `icon-style="power"`, `icon-size="0.6"`, `font="label-font"`, `on-color="accent"`, `off-color="text-dim"`; no `default-value` |

The knob (1.1) and readout (1.3) both carry the free-time tag and the toggle (1.4) carries the TimeMode tag, all
inside the one container that names the sub-controller; `DelayTimeSyncController::verifyView` therefore sees all
three. `BBDNoteValue` has no view (it is swapped in onto 1.1 and 1.3 while synced), as today.

#### Core 2 - Feedback cell (design 4.1)

| # | Template | Class | Origin | Size | control-tag | Label / title | Attributes |
|---|---|---|---|---|---|---|---|
| 2 | BBDPanel | `CViewContainer` | 308, 12 | 284, 72 | none | none | `transparent="true"` (no sub-controller) |
| 2.1 | BBDPanel > #2 | `ArcKnob` | 8, 8 | 56, 56 | `BBDFeedback` | none | `arc-color="accent"`, `guide-color="slider-frame"`, `arc-width="3"`; no `default-value` (design 4.1: VST3Editor binds the registered default, 0.333) |
| 2.2 | BBDPanel > #2 | `CTextLabel` | 76, 10 | 120, 16 | none | `Feedback` | label attribute set as 1.2 |
| 2.3 | BBDPanel > #2 | `CParamDisplay` | 76, 30 | 120, 24 | `BBDFeedback` | none | readout attribute set as 1.3 |

#### Core 3 - Mix cell (design 4.1)

| # | Template | Class | Origin | Size | control-tag | Label / title | Attributes |
|---|---|---|---|---|---|---|---|
| 3 | BBDPanel | `CViewContainer` | 604, 12 | 284, 72 | none | none | `transparent="true"` |
| 3.1 | BBDPanel > #3 | `ArcKnob` | 8, 8 | 56, 56 | `BBDMix` | none | as 2.1 |
| 3.2 | BBDPanel > #3 | `CTextLabel` | 76, 10 | 120, 16 | none | `Mix` | label attribute set as 1.2 |
| 3.3 | BBDPanel > #3 | `CParamDisplay` | 76, 30 | 120, 24 | `BBDMix` | none | readout attribute set as 1.3 |

### 2.2 Section CHIP - tall, spans columns 1-2 (design 7.5)

| # | Template | Class | Origin | Size | control-tag | Label / title | Attributes |
|---|---|---|---|---|---|---|---|
| 4 | BBDPanel | `FieldsetContainer` | 12, 96 | 580, 372 | none | `CHIP` | `fieldset-title="CHIP"`, `fieldset-color="border"`, `fieldset-title-color="text-dim"`, `fieldset-font-size="11"`, `fieldset-radius="4"`, `fieldset-line-width="1"`, `transparent="true"` |

Cells are direct children of #4 (not wrapped; design 4). Cell widths in a two-column section: full = 556, half L = 272.

**Era - segment cell, row 24, full (x 12, w 556), design 4.5 / 7.5**

| # | Parent | Class | Origin | Size | control-tag | Label / title | Attributes |
|---|---|---|---|---|---|---|---|
| 4.1 | #4 | `CTextLabel` | 12, 24 | 556, 16 | none | `Era` | `font="label-font"`, `font-color="text-dim"`, `text-alignment="left"`, `transparent="true"` |
| 4.2 | #4 | `CSegmentButton` | 12, 44 | 556, 24 | `BBDEra` | none | `style="horizontal"`, `selection-mode="kSingle"`, `segment-names="MN3005,MN3007,MN3205,SAD1024"`, `font="value-font"`, `text-color="text"`, `text-color-highlighted="text-light"`, `frame-color="border"`, `frame-width="1"`, `round-radius="3"`, `gradient="segment-bg"`, `gradient-highlighted="segment-bg-selected"` (SEG 4: 4 segments of 139; a segment alone on its row takes the full cell, design 4.5) |

**Age - slider cell, row 76, half L (x 12, w 272), design 4.3 / 7.5**

| # | Parent | Class | Origin | Size | control-tag | Label / title | Attributes |
|---|---|---|---|---|---|---|---|
| 4.3 | #4 | `CTextLabel` | 12, 76 | 208, 16 | none | `Age` | label set as 4.1 |
| 4.4 | #4 | `CParamDisplay` | 220, 76 | 64, 16 | `BBDAge` | none | `font="value-font"`, `font-color="text"`, `text-alignment="right"`, `back-color="~ TransparentCColor"`, `frame-color="~ TransparentCColor"`, `style-no-frame="true"`; today's `value-precision="0"` is dropped |
| 4.5 | #4 | `CSlider` | 12, 98 | 272, 20 | `BBDAge` | none | `orientation="horizontal"`, `draw-frame="true"`, `draw-back="true"`, `draw-value="true"`, `draw-frame-color="slider-frame"`, `draw-back-color="slider-track"`, `draw-value-color="accent"`, `frame-width="1"`, `transparent="true"` |

Era and Age keep the rows they have in Digital's CHARACTER (Era 24, Age 76), and Age has the same row in Tape's
CHARACTER (76). Rows 128-284 stay bordered empty space (BBD has only two CHIP parameters; principle 6).

### 2.3 Section MODULATION - column 3, standard (design 3.2 #3, 7.5)

| # | Template | Class | Origin | Size | control-tag | Label / title | Attributes |
|---|---|---|---|---|---|---|---|
| 5 | BBDPanel | `FieldsetContainer` | 604, 96 | 284, 180 | none | `MODULATION` | fieldset attribute set as #4 with `fieldset-title="MODULATION"` |

Full cell: x 12, w 260. The area (604,288,284,180) below it has no view (plain panel background). MODULATION is
standard, not tall, so Mod Depth and Mod Rate sit at the same panel coordinates as in Digital and PingPong (design
2.4, 7.5).

| # | Parent | Class | Origin | Size | control-tag | Label / title | Attributes |
|---|---|---|---|---|---|---|---|
| 5.1 | #5 | `CTextLabel` | 12, 24 | 196, 16 | none | `Mod Depth` | label set as 4.1 |
| 5.2 | #5 | `CParamDisplay` | 208, 24 | 64, 16 | `BBDModulationDepth` | none | readout set as 4.4 |
| 5.3 | #5 | `CSlider` | 12, 46 | 260, 20 | `BBDModulationDepth` | none | slider set as 4.5 |
| 5.4 | #5 | `CTextLabel` | 12, 76 | 196, 16 | none | `Mod Rate` | label set as 4.1 |
| 5.5 | #5 | `CParamDisplay` | 208, 76 | 64, 16 | `BBDModulationRate` | none | readout set as 4.4 |
| 5.6 | #5 | `CSlider` | 12, 98 | 260, 20 | `BBDModulationRate` | none | slider set as 4.5 |

Both are unipolar linear-style sliders (not bipolar); no `draw-value-from-center`.

## 3. Coordinate check

Panel 900 x 480, all in-panel rects:

| Sibling set | Rects | Result |
|---|---|---|
| Panel children | #1 (12,12,284,72) ends 296 / 84; #2 (308,12) ends 592 / 84; #3 (604,12) ends 888 / 84; #4 (12,96,580,372) ends 592 / 468; #5 (604,96,284,180) ends 888 / 276 | gutters 12; no overlap; all within 900 x 480 (right 888, bottom 468, 12 margin); (604,288,284,180) is the only uncovered background |
| In #1 | knob 8-64 x, 8-64 y; label 76-196 x, 10-26 y; readout 76-196 x, 30-54 y; toggle 200-276 x, 32-54 y | no overlap; inside 284 x 72 (knob and readout margin 8 / 18 to bottom) |
| In #2 and #3 | as #1 without the toggle | no overlap |
| In #4 | Era label 24-40 y (x 12-568); segment 44-68 y (x 12-568); Age label 76-92 y (x 12-220); Age readout 76-92 y (x 220-284); slider 98-118 y (x 12-284) | no overlap (segment bottom 68 < Age label top 76; label and readout abut at 220); all within 580 x 372; bottom of content 118 |
| In #5 | rows at y 24-66 and 76-118; x 12-272 | no overlap; within 284 x 180 (bottom pad 62) |

No intentionally stacked views in this template. Counts for the structure test (design 9.4): 3 plain `CViewContainer`s
(the core cells; BBD has no bipolar cell, so no tick), 2 `FieldsetContainer`s.

## 4. Mapping: every bound control-tag of today's BBDPanel

| Today's tag (id) | Today | Goes to | Notes |
|---|---|---|---|
| BBDDelayTime (500) | ArcKnob 45 + CParamDisplay 55x13 | #1.1 knob + #1.3 readout, label #1.2 "Time" | core cell 1; sub-controller preserved |
| BBDTimeMode (507) | ToggleButton "sync" 49x10, 7 pt | #1.4 ToggleButton "Sync" 76x22 | same container as the knob |
| BBDNoteValue (508) | no view (sync swap) | none (via #1.1 / #1.3 swap) | unchanged |
| BBDFeedback (501) | CSlider 195x20 + label + readout | #2.1 ArcKnob + #2.3 readout, label #2.2 "Feedback" | core cell 2; widget changes slider to knob per design 3.1 |
| BBDMix (506) | CSlider 260x20 + label + readout | #3.1 ArcKnob + #3.3 readout, label #3.2 "Mix" | core cell 3 |
| BBDAge (504) | CSlider 122x20 + label + readout | #4.5 CSlider + #4.4 readout, label #4.3 "Age" | CHIP row 76 half L, width 272 |
| BBDEra (505) | CSegmentButton 245x22 | #4.2 CSegmentButton 556x24, label #4.1 "Era" | CHIP row 24 full, width 556 |
| BBDModulationDepth (502) | CSlider 120x20 + label "Depth" | #5.3 CSlider + #5.2 readout, label #5.1 "Mod Depth" | MODULATION row 24 full |
| BBDModulationRate (503) | CSlider 120x20 + label "Rate" | #5.6 CSlider + #5.5 readout, label #5.4 "Mod Rate" | MODULATION row 76 full |

Removed non-bound views: title label "BBD DELAY"; fieldsets "TIME & MIX" and "CHARACTER" (replaced by the core strip
and CHIP); the old 280/270/270 fieldsets and all `back-color` / `value-color` / `frame-color` slider attributes
(the known defect; the new sliders use `draw-*-color`).

Control-tag names and numbers are untouched. Every interactive tag (BBDDelayTime, BBDTimeMode, BBDFeedback, BBDMix,
BBDAge, BBDEra, BBDModulationDepth, BBDModulationRate) keeps an interactive view, satisfying `lost-control`.

unmapped: none.

## 5. Deviations from the design system

1. **CHIP follows design 7.5: tall, spanning columns 1-2 (12,96,580,372); Era row 24 full 556, Age row 76 half L
   272** (design 7.5, 9.1 and 11, "BBD CHIP follows 7.5"). An earlier draft put Era and Age side by side on row 24,
   and a later one made CHIP a single column leaving column 2 as an unbordered empty column; both are withdrawn. Not a
   deviation. The label stays "Era" (dictionary 6.2), not the audit's "Chip".
2. **MODULATION is a standard section (604,96,284,180)**, not the tall one the first draft of design 7.5 had. Design
   2.4, 7.5 and 9.1 were amended on 2026-10-28 to say so (section 11); not a deviation.
3. **No view carries `default-value`** (design 4.1): VST3Editor binds the registered defaults (BBDDelayTime 0.286,
   BBDFeedback 0.333, BBDMix 0.5, TimeMode Free); today's `0.25` on the Time knob was overwritten at bind time.
4. **CHIP rows 128-284 are empty** because BBD has only two CHIP parameters. This is the design's tall-section rule
   (principle 6: empty space sits inside the bordered section), not a missing control.
5. **Readouts.** `formatBBDParam` already prints the 6.4 formats except the core time's seconds switch: design 9.3
   "Core time readouts switch to seconds" makes 500 read "1.00 s" at its maximum ("20.0 ms" / "300.3 ms" / "1.00 s").

No placement outside the design system's slots was needed.
