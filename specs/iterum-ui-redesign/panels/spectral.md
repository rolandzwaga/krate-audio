# Spectral panel spec (template `SpectralPanel`)

Authority: `design-system.md` (sections 2-8; section 7.2 is this mode's row table). All numbers are VSTGUI points.
Today's template is 860 x 400 with 3 standard fieldsets; the new one is 900 x 480 with the core strip and two **tall**
sections (372 high) in the cells of section 2.4: SPREAD (col 1), CHARACTER (col 2), plus STEREO in the standard pinned
cell (604, 288, 284, 180) of column 3 (ruling 5, design 10). The cell (604, 96, 284, 180) above it is plain panel
background, as the pin leaves cells empty in Granular and Digital.

Reconciled 2026-10-28 against design-system.md 4.1, 4.4, 6.2, 6.4, 7.2, 9.3 and 10: the Freeze toggle sits in core
cell 3 beside Mix, the Feedback Tilt cell carries a centre tick, no view carries `default-value`, the readout formats
are the 6.4 standard. Design 7.2 now says exactly what this spec says.

Coordinates: "panel" = template-relative, "fs" = relative to the section fieldset. Body cells are direct children of
their fieldset (no cell containers, design 4 / 8.1). Row origins inside a tall section: fs y = 24, 76, 128 (rows 180-284
are empty bordered space); inside STEREO (180 high): fs y = 24.

## 1. Attribute sets (defaults named once, referenced below)

- **LBL** (label): `CTextLabel font="label-font" font-color="text-dim" text-alignment="left" transparent="true"`
- **RDO-S** (slider readout): `CParamDisplay font="value-font" font-color="text" text-alignment="right" back-color="~ TransparentCColor" frame-color="~ TransparentCColor" style-no-frame="true"`
- **RDO-K** (knob readout): as RDO-S but `font="readout-font" text-alignment="left"`
- **KNOB**: `ArcKnob arc-color="accent" guide-color="slider-frame" arc-width="3"`
- **SLIDER**: `CSlider orientation="horizontal" draw-frame="true" draw-back="true" draw-value="true" draw-frame-color="slider-frame" draw-back-color="slider-track" draw-value-color="accent" frame-width="1" transparent="true"` (bipolar adds `draw-value-from-center="true"`). No `back-color`, `value-color` or `frame-color` anywhere.
- **TICK** (bipolar cells only, design 4.4 / ruling 7): `CViewContainer background-color="text-dim" transparent="false" mouse-enabled="false"`, no children, size 1 x 20 at the slider's centre x and the slider's y, declared immediately after the slider.
- No view carries `default-value` (design 4.1).
- **SEGMENT**: `CSegmentButton style="horizontal" selection-mode="kSingle" font="value-font" text-color="text" text-color-highlighted="text-light" frame-color="border" frame-width="1" round-radius="3" gradient="segment-bg" gradient-highlighted="segment-bg-selected"`
- **TOGGLE**: `ToggleButton title-position="right" icon-style="power" icon-size="0.6" font="label-font" on-color="accent" off-color="text-dim"`
- **FIELDSET**: `FieldsetContainer fieldset-color="border" fieldset-title-color="text-dim" fieldset-font-size="11" fieldset-radius="4" fieldset-line-width="1" transparent="true"`

## 2. Views, in uidesc nesting order

Template: `SpectralPanel`, class `CViewContainer`, origin `0, 0`, size `900, 480`, `background-color="panel"`, `transparent="false"`.
The old "SPECTRAL DELAY" title label is removed (design 4.11).

### 2.1 Core strip

| # | Parent | Class | Origin | Size | control-tag | Label / title | Differs from defaults |
|---|---|---|---|---|---|---|---|
| 1 | panel | CViewContainer | 12, 12 | 284, 72 | | | `sub-controller="SpectralTimeSyncController"`, `transparent="true"` |
| 1a | 1 | ArcKnob (KNOB) | 8, 8 | 56, 56 | SpectralBaseDelay | | |
| 1b | 1 | CTextLabel (LBL) | 76, 10 | 120, 16 | | Time | |
| 1c | 1 | CParamDisplay (RDO-K) | 76, 30 | 120, 24 | SpectralBaseDelay | | |
| 1d | 1 | ToggleButton (TOGGLE) | 200, 32 | 76, 22 | SpectralTimeMode | Sync | |
| 2 | panel | CViewContainer | 308, 12 | 284, 72 | | | `transparent="true"` |
| 2a | 2 | ArcKnob (KNOB) | 8, 8 | 56, 56 | SpectralFeedback | | |
| 2b | 2 | CTextLabel (LBL) | 76, 10 | 120, 16 | | Feedback | |
| 2c | 2 | CParamDisplay (RDO-K) | 76, 30 | 120, 24 | SpectralFeedback | | |
| 3 | panel | CViewContainer | 604, 12 | 284, 72 | | | `transparent="true"` |
| 3a | 3 | ArcKnob (KNOB) | 8, 8 | 56, 56 | SpectralDryWet | | |
| 3b | 3 | CTextLabel (LBL) | 76, 10 | 120, 16 | | Mix | |
| 3c | 3 | CParamDisplay (RDO-K) | 76, 30 | 120, 24 | SpectralDryWet | | |
| 3d | 3 | ToggleButton (TOGGLE) | 200, 32 | 76, 22 | SpectralFreeze | Freeze | the core-3 toggle slot of design 4.1 / 6.2; replaces the CCheckBox and its label |

Knob and readout 1a/1c carry the same tag, and 1d is the TimeMode toggle, all inside container 1, so
`DelayTimeSyncController` sees all three (knob and readout are swapped to SpectralNoteValue while Sync is on, from
the panel's creation on once design 9.3 "Read the Sync state at panel build" lands). Container 1 contains no other
control-tag. The Freeze toggle 3d sits in the Mix cell at the same cell-relative rect as Sync in the Time cell; no
controller touches it. Cell internals: knob 8..64, label/readout 76..196, toggle 200..276, all inside 284 x 72.

### 2.2 SPREAD section (panel 12, 96, 284, 372)

| # | Parent | Class | Origin (fs) | Size | control-tag | Label / title | Differs from defaults |
|---|---|---|---|---|---|---|---|
| 4 | panel | FieldsetContainer (FIELDSET) | 12, 96 | 284, 372 | | fieldset-title="SPREAD" | |
| 4a | 4 | CTextLabel (LBL) | 12, 24 | 196, 16 | | Spread | |
| 4b | 4 | CParamDisplay (RDO-S) | 208, 24 | 64, 16 | SpectralSpread | | |
| 4c | 4 | CSlider (SLIDER) | 12, 46 | 260, 20 | SpectralSpread | | |
| 4d | 4 | CTextLabel (LBL) | 12, 76 | 260, 16 | | Spread Direction | |
| 4e | 4 | CSegmentButton (SEGMENT) | 12, 96 | 260, 24 | SpectralSpreadDirection | | `segment-names="Low-&gt;High,High-&gt;Low,Center Out"` |
| 4f | 4 | CTextLabel (LBL) | 12, 128 | 260, 16 | | Spread Curve | |
| 4g | 4 | CSegmentButton (SEGMENT) | 12, 148 | 260, 24 | SpectralSpreadCurve | | `segment-names="Linear,Logarithmic"` (was "Linear,Log", now equals the StringListParameter) |

Rows 180 / 232 / 284 are empty bordered space (principle 6). Spread Curve is a full cell (260), like the two-entry segments of Freeze (Slice Mode, Envelope Shape); each of its two segments is 130 pt, so "Logarithmic" has room.

### 2.3 CHARACTER section (panel 308, 96, 284, 372)

| # | Parent | Class | Origin (fs) | Size | control-tag | Label / title | Differs from defaults |
|---|---|---|---|---|---|---|---|
| 5 | panel | FieldsetContainer (FIELDSET) | 308, 96 | 284, 372 | | fieldset-title="CHARACTER" | |
| 5a | 5 | CTextLabel (LBL) | 12, 24 | 196, 16 | | Feedback Tilt | |
| 5b | 5 | CParamDisplay (RDO-S) | 208, 24 | 64, 16 | SpectralFeedbackTilt | | |
| 5c | 5 | CSlider (SLIDER, bipolar) | 12, 46 | 260, 20 | SpectralFeedbackTilt | | `draw-value-from-center="true"` |
| 5c' | 5 | CViewContainer (TICK) | 142, 46 | 1, 20 | | | centre tick, declared after 5c |
| 5d | 5 | CTextLabel (LBL) | 12, 76 | 196, 16 | | Diffusion | |
| 5e | 5 | CParamDisplay (RDO-S) | 208, 76 | 64, 16 | SpectralDiffusion | | |
| 5f | 5 | CSlider (SLIDER) | 12, 98 | 260, 20 | SpectralDiffusion | | |
| 5g | 5 | CTextLabel (LBL) | 12, 128 | 260, 16 | | FFT Size | |
| 5h | 5 | CSegmentButton (SEGMENT) | 12, 148 | 260, 24 | SpectralFFTSize | | `segment-names="512,1024,2048,4096"` (65 per segment) |

Rows 180 / 232 / 284 of CHARACTER are empty bordered space: the Freeze toggle moved to core cell 3 (view 3d; design
6.2, section 11 "Freeze in the core strip").

### 2.4 STEREO section (panel 604, 288, 284, 180), pinned (design 2.4)

| # | Parent | Class | Origin (fs) | Size | control-tag | Label / title | Differs from defaults |
|---|---|---|---|---|---|---|---|
| 6 | panel | FieldsetContainer (FIELDSET) | 604, 288 | 284, 180 | | fieldset-title="STEREO" | |
| 6a | 6 | CTextLabel (LBL) | 12, 24 | 196, 16 | | Width | |
| 6b | 6 | CParamDisplay (RDO-S) | 208, 24 | 64, 16 | SpectralStereoWidth | | |
| 6c | 6 | CSlider (SLIDER) | 12, 46 | 260, 20 | SpectralStereoWidth | | |

STEREO holds Width only, at row 24, so its slider sits at panel y 334 exactly as in Granular, Digital and PingPong. The cell (604, 96, 284, 180) above STEREO is plain panel background, no view. XML order is exactly the order of the tables (core 1-3, SPREAD, CHARACTER, STEREO), which is the visual order.

## 3. Geometry check

- Core cells (12/308/604, 12, 284 x 72): span x 12..888, y 12..84; gutters 12; all inside 900 x 480.
- Sections: SPREAD and CHARACTER (12/308, 96, 284 x 372) span y 96..468 (12 pt bottom margin); STEREO (604, 288, 284 x 180) spans y 288..468. x up to 888; gutters 12; none overlaps a core cell (y 84 < 96) or another section; the cell (604, 96, 284 x 180) is empty.
- Inside every section the widest child ends at fs x 272 (12 + 260) of 284 (12 pt right pad); lowest child bottom: SPREAD 172, CHARACTER 172 (of 372), STEREO 66 (of 180), all with a bottom pad well over 8.
- Sibling checks per section: slider rows (label 24..40, slider 46..66) do not touch the next row (76); Direction (76..120) vs Curve label (128): clear; Curve segment 148..172 vs nothing below; CHARACTER FFT segment 148..172 vs nothing below. Label/readout pairs abut (196 + 64 = 260) without overlap. The tick 5c' (142..143 x 46..66) lies inside slider 5c: the one intersecting pair, exempted by the inventory (design 9.5, third row).
- Containers 1 and 3: knob 8..64 x 8..64, label 76..196 x 10..26, readout 76..196 x 30..54, toggle 200..276 x 32..54: no overlap (today's knob/Sync overlap is gone).
- No stacked variants, no conditional visibility: `visibility_controller.h` has no Spectral references.

## 4. Mapping table (every bound control-tag of today's SpectralPanel)

| Today's tag (id) | Today | New location | View # |
|---|---|---|---|
| SpectralBaseDelay (201) | ArcKnob 45x45 + readout, TIME & MIX | Core 1 knob + readout, label "Time" | 1a, 1c |
| SpectralTimeMode (211) | "sync" ToggleButton 49x10 | Core 1 Sync toggle, "Sync" | 1d |
| SpectralNoteValue (212) | no own view (knob swap) | unchanged: reached through the knob and readout 1a/1c while Sync is on | none (1a, 1c) |
| SpectralFeedback (204) | CSlider "Feedback" | Core 2 knob + readout, "Feedback" | 2a, 2c |
| SpectralDryWet (208) | CSlider "Dry/Wet" | Core 3 knob + readout, "Mix" | 3a, 3c |
| SpectralSpread (202) | CSlider 115 wide, SPECTRAL ANALYSIS | SPREAD row 24, slider cell, "Spread" | 4b, 4c |
| SpectralSpreadDirection (203) | CSegmentButton "Direction" | SPREAD row 76, SEG 3, "Spread Direction" | 4e |
| SpectralSpreadCurve (209) | CSegmentButton "Curve" | SPREAD row 128 full, SEG 2, "Spread Curve" | 4g |
| SpectralFeedbackTilt (205) | CSlider "FB Tilt" | CHARACTER row 24, bipolar cell, "Feedback Tilt" | 5b, 5c |
| SpectralDiffusion (207) | CSlider "Diffusion" | CHARACTER row 76, slider cell | 5e, 5f |
| SpectralFFTSize (200) | CSegmentButton, SPECTRAL ANALYSIS | CHARACTER row 128, SEG 4, "FFT Size" | 5h |
| SpectralFreeze (206) | CCheckBox + label | core 3 toggle slot beside Mix, "Freeze" | 3d |
| SpectralStereoWidth (210) | CSlider "Width" | STEREO row 24, slider cell, "Width" | 6b, 6c |

Unmapped: none. All 13 tags keep a place; every tag with an interactive view today (12) still has one, so the `lost-control` check passes (212 has no view in the baseline, so it is never in the check's set).

## 5. Deviations and notes

Deviations from the design system: none. Design 2.4, 3.2, 3.3, 6.2, 7.2 and 9.1 were amended on 2026-10-28 (ruling 5,
section 11) to say what this spec says: STEREO is the pinned standard cell (604, 288, 284, 180), (604, 96) is empty,
Spread Curve is a full cell, Freeze is in core cell 3. Nothing here supersedes the design any more.

Notes the build stage must act on:

1. **Spectral readouts need units (design 9.3 row "Complete the Spectral formatter", required).**
   `formatSpectralParam` (`plugins/iterum/src/parameters/spectral_params.h`, lines 307-357) returns bare numbers
   ("250", "0", "+0") for 201, 202, 204, 205, 207, 208, 210, and formats 201 and 202 in one branch. The 6.4 formats,
   verbatim, with min / default / max strings for the test-first `[spectral][format]` case:

   | Id | Tag | 6.4 format | min / default / max |
   |---|---|---|---|
   | 201 | SpectralBaseDelay | core time: `"%.1f ms"` below 1000 ms, `"%.2f s"` from 1000 ms (n*2000) | "0.0 ms" / "250.0 ms" (0.125) / "2.00 s" |
   | 202 | SpectralSpread | `"%.0f ms"` (body milliseconds; it is 0-2000 ms, `spectral_params.h:79-83`, not a percentage) | "0 ms" / "0 ms" / "2000 ms" |
   | 204 | SpectralFeedback | `"%.0f%%"` of n*120 | "0%" / "0%" / "120%" |
   | 205 | SpectralFeedbackTilt | `"%+.0f%%"` (as Digital 614) | "-100%" / "+0%" / "+100%" |
   | 207, 208, 210 | Diffusion, DryWet (Mix), StereoWidth | `"%.0f%%"` | "0%" / "0%", "50%", "0%" / "100%" |

   The readouts 1c, 2c, 3c, 4b, 5b, 5e, 6b then match Digital, PingPong, BBD, Reverse and MultiTap. Parameter IDs,
   ranges, units metadata and state are untouched (display strings only). 64 pt readouts fit "2000 ms", "+100%",
   "120%"; the 120 pt core readout fits "2.00 s". The Sync note-value path is unchanged.
2. **Curve segment width.** Spread Curve is a full cell (260, 130 per segment), like Freeze's Slice Mode and Envelope
   Shape (design 4.5: a segment alone on its row takes the full cell), so "Logarithmic" (about 57 pt in Arial 11) has
   ample room.
3. **No `default-value` on any view** (design 4.1): VST3Editor sets the knobs' and toggles' defaults from the
   registered parameter defaults (SpectralBaseDelay 0.125, SpectralFeedback 0, SpectralDryWet 0.5, TimeMode Free,
   Freeze off); today's `0.25` on the Time knob was overwritten at bind time and is dropped.
4. The `back-color`/`value-color`/`frame-color` defect is removed for the four sliders in this panel by using the
   `draw-*-color` names above (the global stylesheet renames them tree-wide).
5. Plain `CViewContainer` count for the structure test (design 9.4): 4 (three core cells and the tick 5c');
   `FieldsetContainer` count 3.
