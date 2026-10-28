# Panel spec: Freeze (template `FreezePanel`)

Authority: `design-system.md` sections 2-8 (4.1-4.11 cell geometry, 7.10 mapping, 8.2 conditional groups). Where this
file and the design system disagree, the design system wins. All numbers are VSTGUI points. Coordinates are relative to
the view's parent (template-relative for top-level views, fieldset-relative inside a fieldset).

## 1. Template

`<template name="FreezePanel" class="CViewContainer" origin="0, 0" size="900, 480" background-color="panel" transparent="false">`

No title label (the old `PATTERN FREEZE MODE` label is dropped, 4.11). Every colour, font and gradient is a token name
from 5.2-5.4; no literal hex. XML order is visual order: core 1, core 2, core 3, SLICE, ENVELOPE, then the four
variants.

Shorthands used below (the design system's defaults, restated so the XSLT author need not look them up):

- **LABEL** = `CTextLabel font="label-font" font-color="text-dim" text-alignment="left" transparent="true"`
- **READOUT-S** (slider cell) = `CParamDisplay font="value-font" font-color="text" text-alignment="right" back-color="~ TransparentCColor" frame-color="~ TransparentCColor" style-no-frame="true"`
- **READOUT-K** (knob cell) = `CParamDisplay font="readout-font" font-color="text" text-alignment="left" back-color="~ TransparentCColor" frame-color="~ TransparentCColor" style-no-frame="true"`
- **SLIDER** = `CSlider orientation="horizontal" draw-frame="true" draw-back="true" draw-value="true" draw-frame-color="slider-frame" draw-back-color="slider-track" draw-value-color="accent" frame-width="1" transparent="true"` (never `back-color`/`value-color`/`frame-color`)
- **MENU** = `COptionMenu font="value-font" font-color="text" back-color="slider-track" frame-color="border" frame-width="1" style-round-rect="true" round-rect-radius="3" text-inset="6, 0" menu-popup-style="true" mouse-enabled="true" wants-focus="true"`
- **SEGMENT** = `CSegmentButton style="horizontal" selection-mode="kSingle" font="value-font" text-color="text" text-color-highlighted="text-light" frame-color="border" frame-width="1" round-radius="3" gradient="segment-bg" gradient-highlighted="segment-bg-selected"`
- **FIELDSET** = `FieldsetContainer fieldset-color="border" fieldset-title-color="text-dim" fieldset-font-size="11" fieldset-radius="4" fieldset-line-width="1" transparent="true"`

Slider cell at fieldset-relative `(x, y)`, width `w` (4.3): LABEL `(x, y, w-64, 16)`, READOUT-S `(x+w-64, y, 64, 16)`,
SLIDER `(x, y+22, w, 20)`. Menu cell (4.6): LABEL `(x, y, w, 16)`, MENU `(x, y+20, w, 24)`. Segment cell (4.5):
LABEL `(x, y, w, 16)`, SEGMENT `(x, y+20, w, 24)`.

Freeze has no TimeMode parameter and no Feedback: core 1 has no Sync toggle and no `sub-controller`; core 2 is a select
cell (4.2, 4.9).

## 2. Views, in uidesc nesting order

### 2.1 Core strip (three transparent containers, y = 12, 284 x 72)

| # | Template / parent | Class | Origin | Size | control-tag | Label / title | Attributes beyond defaults |
|---|---|---|---|---|---|---|---|
| 1 | FreezePanel | CViewContainer | 12, 12 | 284, 72 | | core 1 | `transparent="true"`; **no** `sub-controller` |
| 1a | core 1 | ArcKnob | 8, 8 | 56, 56 | FreezeSliceLength | | `arc-color="accent" guide-color="slider-frame" arc-width="3"`; no `default-value` (design 4.1) |
| 1b | core 1 | CTextLabel (LABEL) | 76, 10 | 120, 16 | | Slice Length | |
| 1c | core 1 | CParamDisplay (READOUT-K) | 76, 30 | 120, 24 | FreezeSliceLength | | core time format of design 6.4 once the 9.3 Freeze row lands: "10.0 ms" / "200.0 ms" / "2.00 s" |
| 2 | FreezePanel | CViewContainer | 308, 12 | 284, 72 | | core 2 | `transparent="true"` |
| 2a | core 2 | CTextLabel (LABEL) | 8, 10 | 200, 16 | | Pattern | |
| 2b | core 2 | COptionMenu (MENU) | 8, 30 | 200, 24 | FreezePatternType | | 4 entries from the parameter (Euclidean, Granular Scatter, Harmonic Drones, Noise Bursts); no list in the uidesc |
| 3 | FreezePanel | CViewContainer | 604, 12 | 284, 72 | | core 3 | `transparent="true"` |
| 3a | core 3 | ArcKnob | 8, 8 | 56, 56 | FreezeDryWet | | `arc-color="accent" guide-color="slider-frame" arc-width="3"` |
| 3b | core 3 | CTextLabel (LABEL) | 76, 10 | 120, 16 | | Mix | |
| 3c | core 3 | CParamDisplay (READOUT-K) | 76, 30 | 120, 24 | FreezeDryWet | | existing formatter prints "%.0f%%" |

### 2.2 SLICE (always visible, column 1 row 1)

| # | Parent | Class | Origin | Size | control-tag | Label / title | Attributes beyond defaults |
|---|---|---|---|---|---|---|---|
| 4 | FreezePanel | FieldsetContainer (FIELDSET) | 12, 96 | 284, 180 | | SLICE | `fieldset-title="SLICE"` |
| 4a | SLICE | CTextLabel (LABEL) | 12, 24 | 260, 16 | | Slice Mode | |
| 4b | SLICE | CSegmentButton (SEGMENT) | 12, 44 | 260, 24 | FreezeSliceMode | | `segment-names="Fixed,Variable"` |

Rows 76 and 128 of SLICE are empty (bordered space, principle 6). Slice Length is the core-1 knob directly above.

### 2.3 ENVELOPE (always visible, column 1 row 2)

| # | Parent | Class | Origin | Size | control-tag | Label / title | Attributes beyond defaults |
|---|---|---|---|---|---|---|---|
| 5 | FreezePanel | FieldsetContainer (FIELDSET) | 12, 288 | 284, 180 | | ENVELOPE | `fieldset-title="ENVELOPE"` |
| 5a | ENVELOPE | CTextLabel (LABEL) | 12, 24 | 196, 16 | | Attack | |
| 5b | ENVELOPE | CParamDisplay (READOUT-S) | 208, 24 | 64, 16 | FreezeEnvelopeAttack | | |
| 5c | ENVELOPE | CSlider (SLIDER) | 12, 46 | 260, 20 | FreezeEnvelopeAttack | | |
| 5d | ENVELOPE | CTextLabel (LABEL) | 12, 76 | 196, 16 | | Release | |
| 5e | ENVELOPE | CParamDisplay (READOUT-S) | 208, 76 | 64, 16 | FreezeEnvelopeRelease | | |
| 5f | ENVELOPE | CSlider (SLIDER) | 12, 98 | 260, 20 | FreezeEnvelopeRelease | | |
| 5g | ENVELOPE | CTextLabel (LABEL) | 12, 128 | 260, 16 | | Envelope Shape | |
| 5h | ENVELOPE | CSegmentButton (SEGMENT) | 12, 148 | 260, 24 | FreezeEnvelopeShape | | `segment-names="Linear,Exponential"` |

### 2.4 The four variants (columns 2-3, stacked at 308, 96, each 580 x 372, one visible)

All four FieldsetContainers (FIELDSET) are `origin="308, 96" size="580, 372"`, are siblings of one another, and are
declared in the order below. Each is a stacked visibility variant (not an overlap). Inside each, the anchor control is a
**direct child** of the fieldset (no nested cell containers), and every control of that pattern is in the same fieldset
(8.1). Variant cells use the half columns of a 580 section: half L x = 12, half R x = 296, w = 272. Slider half L:
LABEL `(12,y,208,16)`, READOUT-S `(220,y,64,16)`, SLIDER `(12,y+22,272,20)`. Slider half R: LABEL `(296,y,208,16)`,
READOUT-S `(504,y,64,16)`, SLIDER `(296,y+22,272,20)`. Row y values are 24, 76, 128. Rightmost extent 296+272 = 568 <= 580;
lowest extent 128+44 = 172 <= 372.

**EUCLIDEAN** (`fieldset-title="EUCLIDEAN"`, anchor FreezeEuclideanSteps 1020, shown when pattern < 0.166)

| Cell | y | Class | Rect (fieldset-relative) | control-tag | Label | Notes |
|---|---|---|---|---|---|---|
| Steps, half L | 24 | CTextLabel / CParamDisplay / CSlider | 12,24,208,16 / 220,24,64,16 / 12,46,272,20 | FreezeEuclideanSteps (readout and slider) | Steps | anchor slider is a direct child |
| Hits, half R | 24 | same pattern at x=296 | 296,24,208,16 / 504,24,64,16 / 296,46,272,20 | FreezeEuclideanHits | Hits | |
| Rotation, half L | 76 | same | 12,76,208,16 / 220,76,64,16 / 12,98,272,20 | FreezeEuclideanRotation | Rotation | |
| Pattern Rate, half R | 76 | CTextLabel + COptionMenu (MENU) | 296,76,272,16 / 296,96,272,24 | FreezePatternRate | Pattern Rate | 21 entries from the parameter |

**GRANULAR SCATTER** (`fieldset-title="GRANULAR SCATTER"`, anchor FreezeGranularDensity 1030, shown when 0.166 <= pattern < 0.5)

| Cell | y | Widget | Rect | control-tag | Label |
|---|---|---|---|---|---|
| Density, half L | 24 | slider cell | 12,24,208,16 / 220,24,64,16 / 12,46,272,20 | FreezeGranularDensity | Density |
| Grain Size, half R | 24 | slider cell | 296,24,208,16 / 504,24,64,16 / 296,46,272,20 | FreezeGranularGrainSize | Grain Size |
| Position Jitter, half L | 76 | slider cell, readout **added** | 12,76,208,16 / 220,76,64,16 / 12,98,272,20 | FreezeGranularPositionJitter | Position Jitter |
| Size Jitter, half R | 76 | slider cell, readout **added** | 296,76,208,16 / 504,76,64,16 / 296,98,272,20 | FreezeGranularSizeJitter | Size Jitter |

**HARMONIC DRONES** (`fieldset-title="HARMONIC DRONES"`, anchor FreezeDroneVoiceCount 1040, shown when 0.5 <= pattern < 0.833)

| Cell | y | Widget | Rect | control-tag | Label |
|---|---|---|---|---|---|
| Voices, half L | 24 | slider cell | 12,24,208,16 / 220,24,64,16 / 12,46,272,20 | FreezeDroneVoiceCount | Voices |
| Interval, half R | 24 | menu cell | 296,24,272,16 / 296,44,272,24 | FreezeDroneInterval | Interval (6 entries: Unison ... Octave, from the parameter) |
| Drift, half L | 76 | slider cell, readout **added** | 12,76,208,16 / 220,76,64,16 / 12,98,272,20 | FreezeDroneDrift | Drift |
| Drift Rate, half R | 76 | slider cell, readout **added** | 296,76,208,16 / 504,76,64,16 / 296,98,272,20 | FreezeDroneDriftRate | Drift Rate |

**NOISE BURSTS** (`fieldset-title="NOISE BURSTS"`, anchor FreezeNoiseColor 1050, shown when pattern >= 0.833)

| Cell | y | Widget | Rect | control-tag | Label |
|---|---|---|---|---|---|
| Noise Color, half L | 24 | menu cell | 12,24,272,16 / 12,44,272,24 | FreezeNoiseColor | Noise Color (8 entries, from the parameter) |
| Burst Rate, half R | 24 | menu cell | 296,24,272,16 / 296,44,272,24 | FreezeNoiseBurstRate | Burst Rate (21 entries) |
| Filter Type, half L | 76 | segment cell | 12,76,272,16 / 12,96,272,24 | FreezeNoiseFilterType | Filter Type; `segment-names="LowPass,HighPass,BandPass"` |
| Filter Cutoff, half R | 76 | slider cell, readout **added** | 296,76,208,16 / 504,76,64,16 / 296,98,272,20 | FreezeNoiseFilterCutoff | Filter Cutoff |
| Filter Sweep, half L | 128 | slider cell, readout **added** | 12,128,208,16 / 220,128,64,16 / 12,150,272,20 | FreezeNoiseFilterSweep | Filter Sweep |

The right half of row 128 and everything below row 172 inside each variant is empty bordered space; the four variants
share one silhouette so nothing moves when the pattern changes (principle 5).

## 3. Mapping table (every bound control-tag of today's FreezePanel)

| control-tag (id) | Goes to | Widget |
|---|---|---|
| FreezeSliceLength (1016) | core 1 (12,12): knob + readout, label "Slice Length" | ArcKnob + CParamDisplay |
| FreezePatternType (1015) | core 2 (308,12), label "Pattern" | COptionMenu |
| FreezeDryWet (1012) | core 3 (604,12): knob + readout, label "Mix" | ArcKnob + CParamDisplay |
| FreezeSliceMode (1017) | SLICE (12,96), y 24 full | CSegmentButton Fixed/Variable |
| FreezeEnvelopeAttack (1060) | ENVELOPE (12,288), y 24 full | CSlider + readout |
| FreezeEnvelopeRelease (1061) | ENVELOPE, y 76 full | CSlider + readout |
| FreezeEnvelopeShape (1062) | ENVELOPE, y 128 full | CSegmentButton Linear/Exponential |
| FreezeEuclideanSteps (1020) | EUCLIDEAN, y 24 half L | CSlider + readout |
| FreezeEuclideanHits (1021) | EUCLIDEAN, y 24 half R | CSlider + readout |
| FreezeEuclideanRotation (1022) | EUCLIDEAN, y 76 half L | CSlider + readout |
| FreezePatternRate (1023) | EUCLIDEAN, y 76 half R | COptionMenu |
| FreezeGranularDensity (1030) | GRANULAR SCATTER, y 24 half L | CSlider + readout |
| FreezeGranularGrainSize (1033) | GRANULAR SCATTER, y 24 half R | CSlider + readout |
| FreezeGranularPositionJitter (1031) | GRANULAR SCATTER, y 76 half L | CSlider + new readout |
| FreezeGranularSizeJitter (1032) | GRANULAR SCATTER, y 76 half R | CSlider + new readout |
| FreezeDroneVoiceCount (1040) | HARMONIC DRONES, y 24 half L | CSlider + readout |
| FreezeDroneInterval (1041) | HARMONIC DRONES, y 24 half R | COptionMenu |
| FreezeDroneDrift (1042) | HARMONIC DRONES, y 76 half L | CSlider + new readout |
| FreezeDroneDriftRate (1043) | HARMONIC DRONES, y 76 half R | CSlider + new readout |
| FreezeNoiseColor (1050) | NOISE BURSTS, y 24 half L | COptionMenu |
| FreezeNoiseBurstRate (1051) | NOISE BURSTS, y 24 half R | COptionMenu |
| FreezeNoiseFilterType (1052) | NOISE BURSTS, y 76 half L | CSegmentButton LowPass/HighPass/BandPass |
| FreezeNoiseFilterCutoff (1053) | NOISE BURSTS, y 76 half R | CSlider + new readout |
| FreezeNoiseFilterSweep (1054) | NOISE BURSTS, y 128 half L | CSlider + new readout |

24 of 24 bound tags placed; unmapped: none. Load-bearing anchors 1020, 1030, 1040, 1050 are each a direct child of the
fieldset that holds all of that pattern's controls. FreezePatternType (1015) drives the visibility thresholds
0.166 / 0.5 / 0.833 unchanged.

## 4. Geometry check

- Core cells: x = 12, 308, 604, each 284 wide, y = 12, height 72; gutters 12; right edge 888; bottom 84 < 96. Inside
  cells: knob 8..64, label/readout from x = 76 to 196 (<= 284), readout bottom 54 <= 72; select cell menu right edge 208.
- Body: SLICE (12..296, 96..276) and ENVELOPE (12..296, 288..468) do not overlap (12 pt vertical gutter) and
  end at y = 468 < 480. The variants (308..888, 96..468) do not overlap column 1 (296 < 308) and end at the 12 pt margin.
- The four variants share origin and size and are the only stacked pairs (6 stacked pairs, as in the baseline). No
  bipolar cell, so no centre tick. Counts for the structure test (design 9.4): 3 plain `CViewContainer`s (the core
  cells), 6 `FieldsetContainer`s (SLICE, ENVELOPE, four variants).
- Inside SLICE, last extent y = 44 + 24 = 68 <= 180, x = 12 + 260 = 272 <= 284. Inside ENVELOPE, last extent
  y = 148 + 24 = 172 <= 180; label + readout in each slider cell abut (196 + 64 = 260) without overlap.
- Inside a variant: label (w 208) and readout (w 64) abut at 12+208 = 220 and 296+208 = 504, end at 284 and 568
  (<= 580). Half L ends at 284, half R starts at 296 (12 pt gap). Menu rows end at y 68, 120.

## 5. Deviations from the design system

Placements follow design-system 7.10 exactly, so there are no unmapped controls. Items the XSLT/implementation author must
not "fix":

- **Mirrored body.** SLICE and ENVELOPE are in column 1 and the variants in columns 2-3: this is the documented 2.4/7.10
  exception to "primary is row 1 column 1", not a new deviation.
- **Core 2 is a select cell holding Pattern** (Freeze has no Feedback parameter), and core 1 holds Slice Length as a
  knob with no Sync (no TimeMode parameter): documented in 3.1 and 4.9.
- **SLICE contains Slice Mode only** (Slice Length is the core-1 knob); two of its three rows are empty, and ENVELOPE
  leaves no row empty. Bordered empty space is intended (principle 6).
- **No `default-value` on any view** (design 4.1, every mode): the parameter's registered default is what VST3Editor
  binds and what a reset restores.
- **Readout work outside the uidesc** (design-system 9.3 "Complete the Freeze formatter", not this spec): Slice
  Length (core time: "%.1f ms" / "%.2f s"), Steps, Hits, Rotation, Voices ("%d"), Density ("%.1f Hz"), Grain Size,
  Attack, Release ("%.0f ms"), the jitters, Drift and Sweep ("%.0f%%"), Drift Rate ("%.2f Hz") and Filter Cutoff
  ("%.0f Hz" / "%.2f kHz") need the `formatFreezeParam` completion and the Steps -> Hits/Rotation `deferUpdate()`,
  or they show raw normalized numbers. The formats are the 6.4 standard verbatim.
- **Segment names** use the parameter's full strings (Linear/Exponential, LowPass/HighPass/BandPass) instead of today's
  abbreviated `Lin,Exp` / `LP,HP,BP`; the parameter strings override the uidesc at runtime in any case.
