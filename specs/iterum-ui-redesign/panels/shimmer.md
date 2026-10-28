# Shimmer panel spec (template `ShimmerPanel`, 900 x 480)

Authority: `design-system.md` (sections 2-8; 7.3 is this mode's row table). All numbers are VSTGUI points. Coordinates
are parent-relative: core-strip children are relative to their cell container, section children relative to their
fieldset. Today's template (860 x 400, 31 views, 3 fieldsets) is replaced wholesale. Shimmer has 3 body sections, so
it uses **tall** sections (372 high), 3 columns, no spans, no stacked variants, and no visibility controller.

Reconciled 2026-10-28 against design-system.md 4.1, 4.4, 6.4, 7.3, 9.3 and 10: the Pitch and Fine Pitch cells carry a
centre tick, no view carries `default-value`, and the readout formats are the 6.4 standard.

## 1. Attribute shorthand (every attribute is the design system's default for that widget)

Written once here; the view list below names a shorthand and lists only what differs (x, y, w, h, tag, text).

| Shorthand | Class | Attributes (section 4) |
|---|---|---|
| **LBL** | CTextLabel | `font="label-font" font-color="text-dim" text-alignment="left" transparent="true"` |
| **VAL** | CParamDisplay | `font="value-font" font-color="text" text-alignment="right" back-color="~ TransparentCColor" frame-color="~ TransparentCColor" style-no-frame="true"`, same `control-tag` as its control |
| **KREAD** | CParamDisplay | `font="readout-font" font-color="text" text-alignment="left" back-color="~ TransparentCColor" frame-color="~ TransparentCColor" style-no-frame="true"`, same `control-tag` as its knob |
| **KNOB** | ArcKnob | `arc-color="accent" guide-color="slider-frame" arc-width="3"`; no `default-value` (section 4.1) |
| **SLD** | CSlider | `orientation="horizontal" draw-frame="true" draw-back="true" draw-value="true" draw-frame-color="slider-frame" draw-back-color="slider-track" draw-value-color="accent" frame-width="1" transparent="true"` (no `back-color` / `value-color` / `frame-color`: that was the defect) |
| **SLD-C** | CSlider | SLD plus `draw-value-from-center="true"` (bipolar, section 4.4), always followed by a TICK |
| **TICK** | CViewContainer | `background-color="text-dim" transparent="false" mouse-enabled="false"`, no children, 1 x 20 at the slider's centre x and y, declared immediately after its SLD-C (section 4.4, ruling 7) |
| **SYNC** | ToggleButton | `title="Sync" title-position="right" icon-style="power" icon-size="0.6" font="label-font" on-color="accent" off-color="text-dim"` |
| **TGL** | ToggleButton | `title-position="right" icon-style="power" icon-size="0.6" font="label-font" on-color="accent" off-color="text-dim"` |
| **FS** | FieldsetContainer | `fieldset-color="border" fieldset-title-color="text-dim" fieldset-font-size="11" fieldset-radius="4" fieldset-line-width="1" transparent="true"` |

Slider cell (full width, w = 260) at row y: LBL at (12, y, 196, 16); VAL at (208, y, 64, 16); SLD at (12, y+22, 260, 20).
Toggle cell: TGL at (12, 44, 124, 24) (cell row y = 24, control at cell +20; the label row is empty, the title is the label).

## 2. View list (uidesc nesting order)

Template `ShimmerPanel`: `class="CViewContainer" origin="0, 0" size="900, 480" background-color="panel" transparent="false"`.
No panel title label ("SHIMMER DELAY" is removed, section 4.11).

| # | Parent | Class | Origin | Size | control-tag | Label / title | Extra attributes |
|---|---|---|---|---|---|---|---|
| 1 | ShimmerPanel | CViewContainer (core 1, Time cell) | 12, 12 | 284, 72 | - | - | `transparent="true" sub-controller="ShimmerTimeSyncController"` |
| 1a | 1 | ArcKnob | 8, 8 | 56, 56 | ShimmerDelayTime | - | KNOB |
| 1b | 1 | CTextLabel | 76, 10 | 120, 16 | - | Time | LBL |
| 1c | 1 | CParamDisplay | 76, 30 | 120, 24 | ShimmerDelayTime | - | KREAD |
| 1d | 1 | ToggleButton | 200, 32 | 76, 22 | ShimmerTimeMode | Sync | SYNC |
| 2 | ShimmerPanel | CViewContainer (core 2, Feedback cell) | 308, 12 | 284, 72 | - | - | `transparent="true"` (no sub-controller) |
| 2a | 2 | ArcKnob | 8, 8 | 56, 56 | ShimmerFeedback | - | KNOB |
| 2b | 2 | CTextLabel | 76, 10 | 120, 16 | - | Feedback | LBL |
| 2c | 2 | CParamDisplay | 76, 30 | 120, 24 | ShimmerFeedback | - | KREAD |
| 3 | ShimmerPanel | CViewContainer (core 3, Mix cell) | 604, 12 | 284, 72 | - | - | `transparent="true"` |
| 3a | 3 | ArcKnob | 8, 8 | 56, 56 | ShimmerDryWet | - | KNOB |
| 3b | 3 | CTextLabel | 76, 10 | 120, 16 | - | Mix | LBL |
| 3c | 3 | CParamDisplay | 76, 30 | 120, 24 | ShimmerDryWet | - | KREAD |
| 4 | ShimmerPanel | FieldsetContainer (row 1, col 1, tall) | 12, 96 | 284, 372 | - | `fieldset-title="PITCH SHIFT"` | FS |
| 4a | 4 | CTextLabel | 12, 24 | 196, 16 | - | Pitch | LBL |
| 4b | 4 | CParamDisplay | 208, 24 | 64, 16 | ShimmerPitchSemitones | - | VAL |
| 4c | 4 | CSlider | 12, 46 | 260, 20 | ShimmerPitchSemitones | - | SLD-C |
| 4c' | 4 | CViewContainer | 142, 46 | 1, 20 | - | - | TICK |
| 4d | 4 | CTextLabel | 12, 76 | 196, 16 | - | Fine Pitch | LBL |
| 4e | 4 | CParamDisplay | 208, 76 | 64, 16 | ShimmerPitchCents | - | VAL |
| 4f | 4 | CSlider | 12, 98 | 260, 20 | ShimmerPitchCents | - | SLD-C |
| 4f' | 4 | CViewContainer | 142, 98 | 1, 20 | - | - | TICK |
| 5 | ShimmerPanel | FieldsetContainer (row 1, col 2, tall) | 308, 96 | 284, 372 | - | `fieldset-title="CHARACTER"` | FS |
| 5a | 5 | CTextLabel | 12, 24 | 196, 16 | - | Pitch Blend | LBL |
| 5b | 5 | CParamDisplay | 208, 24 | 64, 16 | ShimmerShimmerMix | - | VAL |
| 5c | 5 | CSlider | 12, 46 | 260, 20 | ShimmerShimmerMix | - | SLD |
| 5d | 5 | CTextLabel | 12, 76 | 196, 16 | - | Diffusion Size | LBL |
| 5e | 5 | CParamDisplay | 208, 76 | 64, 16 | ShimmerDiffusionSize | - | VAL |
| 5f | 5 | CSlider | 12, 98 | 260, 20 | ShimmerDiffusionSize | - | SLD |
| 6 | ShimmerPanel | FieldsetContainer (row 1, col 3, tall) | 604, 96 | 284, 372 | - | `fieldset-title="FILTER"` | FS |
| 6a | 6 | ToggleButton | 12, 44 | 124, 24 | ShimmerFilterEnabled | Filter | TGL |
| 6b | 6 | CTextLabel | 12, 76 | 196, 16 | - | Filter Cutoff | LBL |
| 6c | 6 | CParamDisplay | 208, 76 | 64, 16 | ShimmerFilterCutoff | - | VAL |
| 6d | 6 | CSlider | 12, 98 | 260, 20 | ShimmerFilterCutoff | - | SLD |

XML order is the visual order: core cells left to right (1, 2, 3), then the three sections left to right. Body cells are
direct children of their fieldset (no cell containers). The area below the last control in each section (fieldset y
118 to 372) is bordered empty space by design (principle 6); the section titles are the 11 pt upper-case FS legend.
Sections are 372 high so the panel fills 480: 96 + 372 = 468, 12 pt bottom margin.

Why this grouping (design system 7.3): PITCH SHIFT holds the two transposition values (both bipolar); CHARACTER holds
the two blend/texture sliders (Pitch Blend moved out of PITCH SHIFT: it is the pitch-shifted-to-dry balance, a character
control, and Mix is now in the core strip, resolving audit issue 8); FILTER holds the enable toggle ahead of its cutoff
(audit issue 9). Shimmer has no Modulation or Stereo section.

## 3. Geometry check

- Panel: 900 x 480. Core cells x 12-296, 308-592, 604-888, y 12-84; sections x 12-296, 308-592, 604-888, y 96-468.
  All inside 12 pt margins; gutters 12; no cell touches another.
- Core cell interior (284 x 72): knob 8-64 x 8-64; label 76-196 x 10-26; readout 76-196 x 30-54; Sync 200-276 x 32-54
  (Time cell only). Readout right edge 196 < Sync left edge 200, knob right edge 64 < 76: no sibling overlap; all
  within 284 x 72.
- Section interior (284 x 372, content width 260 = 12..272): row 24 label 12-208 / readout 208-272 (edge-touching,
  no overlap), slider y 46-66; row 76 label/readout y 76-92, slider y 98-118; toggle (Filter section) 12-136 x 44-68,
  which clears row 76 (starts 76) and nothing sits at row 24 in that section. Every right edge is 272 <= 284 and
  every bottom is <= 118 <= 372.
- Sibling overlaps: none except the two centre ticks (142..143 x 46..66 and 142..143 x 98..118), each inside its
  bipolar slider: the pair the inventory exempts (design 9.5, third row). Stacked variants: none (Shimmer has no
  conditional group).
- Out of bounds: none.
- Counts for the structure test (design 9.4): 5 plain `CViewContainer`s (three core cells, two ticks), 3
  `FieldsetContainer`s.

## 4. Mapping table (today's ShimmerPanel -> new)

| Today's control-tag (id) | Today's view | New place | View # |
|---|---|---|---|
| ShimmerDelayTime (300) | ArcKnob 45x45 + CParamDisplay in TIME & MIX sub-container | core 1 knob + readout, label "Time" | 1a, 1c |
| ShimmerTimeMode (310) | ToggleButton "sync" font-size 7 | core 1 Sync toggle, label "Sync" | 1d |
| ShimmerNoteValue (311) | no view of its own | none; reached through the knob/readout retag by `ShimmerTimeSyncController` (exempt from `lost-control`, 7.11) | - |
| ShimmerFeedback (304) | CSlider + label + readout in TIME & MIX | core 2 knob + readout, label "Feedback" | 2a, 2c |
| ShimmerDryWet (309) | CSlider "Dry/Wet" in TIME & MIX | core 3 knob + readout, label "Mix" | 3a, 3c |
| ShimmerPitchSemitones (301) | CSlider "Semi" in PITCH SHIFT | PITCH SHIFT row 24, bipolar, label "Pitch" | 4b, 4c |
| ShimmerPitchCents (302) | CSlider "Cent" in PITCH SHIFT | PITCH SHIFT row 76, bipolar, label "Fine Pitch" | 4e, 4f |
| ShimmerShimmerMix (303) | CSlider "Shim" in PITCH SHIFT | CHARACTER row 24, label "Pitch Blend" | 5b, 5c |
| ShimmerDiffusionSize (306) | CSlider "Size" in DIFFUSION & FILTER | CHARACTER row 76, label "Diffusion Size" | 5e, 5f |
| ShimmerFilterEnabled (307) | CCheckBox, empty title | FILTER toggle, title "Filter" | 6a |
| ShimmerFilterCutoff (308) | CSlider "Cut" + 25 pt readout | FILTER row 76, label "Filter Cutoff" | 6c, 6d |

Removed non-control views: title label "SHIMMER DELAY", the old fieldset names TIME & MIX / DIFFUSION & FILTER, all
hand-placed labels. Controls mapped: 10 bound control-tags with views (plus NoteValue via the swap). Unmapped: none.
Controller/CPP: one change for Shimmer, the formatter completion of section 6 (no view or controller-wiring change);
`ShimmerTimeSyncController` finds knob + readout (tag ShimmerDelayTime) and the
Sync toggle (ShimmerTimeMode) inside container 1 as the design requires; there are no visibility controllers on this
panel.

## 5. Deviations from the design system

- None. No view carries `default-value` (design 4.1): VST3Editor binds every control's default to the registered
  parameter default (ShimmerDelayTime 0.098, ShimmerFeedback 0.417, ShimmerDryWet 0.5, TimeMode Free, Filter off), so
  today's `0.25` on the Time knob was overwritten at bind time and is dropped, and the knobs that replace sliders
  need no attribute.
- `value-precision` (today `0` on CParamDisplay) is dropped: section 4 does not list it and the readout text is the
  controller's `getParamStringByValue` string (the formatter as completed in section 6; today's `formatShimmerParam`
  prints bare numbers, so the strings carry no units until that change lands).
- Every control has a slot in sections 3-7.

## 6. Formatter completion (design-system 9.3 row "Complete the Shimmer formatter"; `plugins/iterum/src/parameters/shimmer_params.h`, `formatShimmerParam`, lines 282-340)

Design 6.4 states one format per kind of value. Today `formatShimmerParam` prints `"%.0f"` for Time, Feedback, Mix,
Pitch Blend and Diffusion Size, `"%+.0f"` for Pitch and Fine Pitch, and `"%.1fk"` / `"%.0f"` for Filter Cutoff, so the
Shimmer readouts have no units and the cutoff uses a format of its own (Reverse and MultiTap print `"%.2f kHz"` /
`"%.0f Hz"` for the same kind of value). It is controller formatting, not frozen state; no parameter ID, range, default,
units metadata or saved value changes. The 6.4 formats, verbatim, with the strings the test asserts:

| Parameter (id) | 6.4 format | min / default / max |
|---|---|---|
| ShimmerDelayTime (300) | core time: `"%.1f ms"` below 1000 ms, `"%.2f s"` from 1000 ms (10 + n*4990) | "10.0 ms" / "499.0 ms" (0.098) / "5.00 s" |
| ShimmerFeedback (304) | `"%.0f%%"` of n*120 (no space) | "0%" / "50%" (0.417) / "120%" |
| ShimmerDryWet (309), ShimmerShimmerMix (303, Pitch Blend), ShimmerDiffusionSize (306) | `"%.0f%%"` (no space) | "0%" / "50%", "100%", "50%" / "100%" |
| ShimmerPitchSemitones (301) | `"%+.1f st"` (the same label and widget as Granular's Pitch, same precision) | "-24.0 st" / "+12.0 st" (0.75) / "+24.0 st" |
| ShimmerPitchCents (302) | `"%+.0f ct"` | "-100 ct" / "+0 ct" / "+100 ct" |
| ShimmerFilterCutoff (308) | `"%.0f Hz"` below 1000 Hz, `"%.2f kHz"` from 1000 Hz (same rule as `reverse_params.h` 242-244); the value is the linear 20 + n*19980 the handler uses | "20 Hz" / "4.00 kHz" (0.199) / "20.00 kHz" |

The value compared with the thresholds (1000 ms, 1000 Hz) is the denormalised value the processor uses, not the
normalised value. Test first: a `[shimmer][format]` Catch2 case asserting the strings above for min, default and max of
each parameter, with the cutoff and the time checked on both sides of their thresholds; it must fail on the current
formatter before the change. The Time readout while Sync is on shows the note value through `ShimmerNoteValue`, which
is unchanged.
