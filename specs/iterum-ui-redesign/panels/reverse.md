# Reverse panel spec

Template `ReversePanel`, `CViewContainer`, `origin="0, 0" size="900, 480" background-color="panel" transparent="false"`.
Follows `design-system.md` sections 2-8 (Reverse is section 7.8). All coordinates are in points and parent-relative.
Every view that has a `control-tag` uses today's tag name unchanged.

Reconciled 2026-10-28 against design-system.md 4.1, 6.4, 7.8, 9.3 and 11: design 7.8 was amended to this spec's FILTER
order (toggle 24, Filter Cutoff 76, Filter Type 128), no view carries `default-value`, the Crossfade panel-absolute rect
below is corrected.

Layout (panel-relative):

```
y  12 [core 1: Chunk Size + Sync 284x72] [core 2: Feedback 284x72] [core 3: Mix 284x72]
y  96 [PLAYBACK 580x372 (x 12)                                   ] [FILTER 284x372 (x 604)]
y 468 bottom edge of both sections (panel height 480, 12 margin)
```

Defaults inherited from the design system and **not repeated per view**:

- Every `CTextLabel` label: `font="label-font" font-color="text-dim" text-alignment="left" transparent="true"`.
- Every readout `CParamDisplay` in a slider cell: `font="value-font" font-color="text" text-alignment="right" back-color="~ TransparentCColor" frame-color="~ TransparentCColor" style-no-frame="true"`.
- Every `CSlider` (4.3): `orientation="horizontal" draw-frame="true" draw-back="true" draw-value="true" draw-frame-color="slider-frame" draw-back-color="slider-track" draw-value-color="accent" frame-width="1" transparent="true"`.
  No `back-color`, `value-color` or `frame-color` anywhere on a CSlider.
- Every `CSegmentButton` (4.5): `style="horizontal" selection-mode="kSingle" font="value-font" text-color="text" text-color-highlighted="text-light" frame-color="border" frame-width="1" round-radius="3" gradient="segment-bg" gradient-highlighted="segment-bg-selected"`.
- Every `ToggleButton` (4.8): `title-position="right" icon-style="power" icon-size="0.6" font="label-font" on-color="accent" off-color="text-dim"`.
- Every `FieldsetContainer` (4.11): `fieldset-color="border" fieldset-title-color="text-dim" fieldset-font-size="11" fieldset-radius="4" fieldset-line-width="1" transparent="true"`.
- Every `ArcKnob` (4.1): `arc-color="accent" guide-color="slider-frame" arc-width="3"`.
- Every core-cell readout (4.1): `font="readout-font" font-color="text" text-alignment="left" back-color="~ TransparentCColor" frame-color="~ TransparentCColor" style-no-frame="true"`.

The template has no title label ("REVERSE DELAY" is removed, 4.11).

## 1. Views, in uidesc nesting order

Columns: view, class, origin, size, control-tag, label/title, attributes that differ from or add to the defaults above.

### 1.1 Core strip

| # | Parent | Class | Origin | Size | control-tag | Label / title | Extra attributes |
|---|---|---|---|---|---|---|---|
| 1 | ReversePanel | CViewContainer (core cell 1) | 12, 12 | 284, 72 | | | `sub-controller="ReverseTimeSyncController" transparent="true"` |
| 1a | 1 | ArcKnob | 8, 8 | 56, 56 | ReverseChunkSize | | no `default-value` (design 4.1) |
| 1b | 1 | CTextLabel | 76, 10 | 120, 16 | | "Chunk Size" | |
| 1c | 1 | CParamDisplay | 76, 30 | 120, 24 | ReverseChunkSize | | core-cell readout set (above); reads "499.5 ms" at the default and "2.00 s" at the maximum once design 9.3 "Core time readouts switch to seconds" lands |
| 1d | 1 | ToggleButton | 200, 32 | 76, 22 | ReverseTimeMode | title "Sync" | no `default-value` |
| 2 | ReversePanel | CViewContainer (core cell 2) | 308, 12 | 284, 72 | | | `transparent="true"`; no sub-controller |
| 2a | 2 | ArcKnob | 8, 8 | 56, 56 | ReverseFeedback | | no `default-value` |
| 2b | 2 | CTextLabel | 76, 10 | 120, 16 | | "Feedback" | |
| 2c | 2 | CParamDisplay | 76, 30 | 120, 24 | ReverseFeedback | | core-cell readout set |
| 3 | ReversePanel | CViewContainer (core cell 3) | 604, 12 | 284, 72 | | | `transparent="true"` |
| 3a | 3 | ArcKnob | 8, 8 | 56, 56 | ReverseDryWet | | no `default-value` |
| 3b | 3 | CTextLabel | 76, 10 | 120, 16 | | "Mix" | |
| 3c | 3 | CParamDisplay | 76, 30 | 120, 24 | ReverseDryWet | | core-cell readout set |

The knob and the readout in cell 1 both carry `ReverseChunkSize`, so `ReverseTimeSyncController` can retag them to
`ReverseNoteValue` when `ReverseTimeMode` (the Sync toggle) is on. `ReverseNoteValue` gets no view of its own.

### 1.2 PLAYBACK (primary, tall, two columns)

Fieldset at panel (12, 96), size 580 x 372. Children are direct children of the fieldset (no cell containers, section 4).
Row pitch: row 1 at y = 24. This section uses only row 1; rows 76 to 284 are intentionally empty (principle 6).

| # | Parent | Class | Origin | Size | control-tag | Label / title | Extra attributes |
|---|---|---|---|---|---|---|---|
| 4 | ReversePanel | FieldsetContainer | 12, 96 | 580, 372 | | `fieldset-title="PLAYBACK"` | |
| 4a | 4 | CTextLabel | 12, 24 | 272, 16 | | "Playback Mode" | |
| 4b | 4 | CSegmentButton | 12, 44 | 272, 24 | ReversePlaybackMode | | `segment-names="Full Reverse,Alternating,Random"` (equals the parameter's strings; 90.7 pt per segment) |
| 4c | 4 | CTextLabel | 296, 24 | 208, 16 | | "Crossfade" | |
| 4d | 4 | CParamDisplay | 504, 24 | 64, 16 | ReverseCrossfade | | slider readout set |
| 4e | 4 | CSlider | 296, 46 | 272, 20 | ReverseCrossfade | | slider defaults only |

Cell check: half L = x 12..284, half R = x 296..568, inside the fieldset inner width 556 (12..568). Gutter 12.

### 1.3 FILTER (tall, column 3)

Fieldset at panel (604, 96), size 284 x 372. Inner width 260 (x 12..272).

| # | Parent | Class | Origin | Size | control-tag | Label / title | Extra attributes |
|---|---|---|---|---|---|---|---|
| 5 | ReversePanel | FieldsetContainer | 604, 96 | 284, 372 | | `fieldset-title="FILTER"` | |
| 5a | 5 | ToggleButton | 12, 44 | 124, 24 | ReverseFilterEnabled | title "Filter" | toggle cell, label row (12,24) left empty because the title is the label; no `default-value` |
| 5b | 5 | CTextLabel | 12, 76 | 196, 16 | | "Filter Cutoff" | |
| 5c | 5 | CParamDisplay | 208, 76 | 64, 16 | ReverseFilterCutoff | | slider readout set |
| 5d | 5 | CSlider | 12, 98 | 260, 20 | ReverseFilterCutoff | | slider defaults only |
| 5e | 5 | CTextLabel | 12, 128 | 260, 16 | | "Filter Type" | |
| 5f | 5 | CSegmentButton | 12, 148 | 260, 24 | ReverseFilterType | | `segment-names="LowPass,HighPass,BandPass"` (86.7 pt per segment) |

Rows used: 24 (toggle cell: toggle at 24 + 20 = 44), 76 (slider cell: label and readout at 76, slider at 98, same row as
Shimmer's Filter Cutoff), 128 (segment cell: label at 128, control at 148). Rows 180 to 284 are empty. This is the
order of design 7.8 as amended on 2026-10-28 (Filter Cutoff 76, Filter Type 128).

## 2. Mapping table (every bound control-tag in today's ReversePanel)

| Today's tag (id) | Today's view | Goes to | New rect |
|---|---|---|---|
| ReverseChunkSize (800) | ArcKnob 45x45 + CParamDisplay 55x13 in container 55x68 | core cell 1: ArcKnob 1a + CParamDisplay 1c, inside container 1 (sub-controller kept) | knob (12+8, 12+8, 56, 56); readout (88, 42, 120, 24) panel-absolute |
| ReverseTimeMode (808) | ToggleButton "sync" 49x10 | core cell 1: ToggleButton 1d "Sync" | (212, 44, 76, 22) panel-absolute |
| ReverseNoteValue (809) | no own control (knob retag) | unchanged: reached through the retagged knob and readout of cell 1 | n/a |
| ReverseFeedback (803) | CSlider 120x20 + CParamDisplay in TIME & MIX | core cell 2: ArcKnob 2a + CParamDisplay 2c | knob (316, 20, 56, 56); readout (384, 42, 120, 24) |
| ReverseDryWet (807) | CSlider 130x20 + CParamDisplay "Dry/Wet" in TIME & MIX | core cell 3: ArcKnob 3a + CParamDisplay 3c, label "Mix" | knob (612, 20, 56, 56); readout (680, 42, 120, 24) |
| ReversePlaybackMode (802) | CSegmentButton 250x22 + "Play" label in CHUNK | PLAYBACK row 24, half L: label 4a + CSegmentButton 4b | (24, 140, 272, 24) panel-absolute for the segment |
| ReverseCrossfade (801) | CSlider 250x20 + CParamDisplay 125x16 in CHUNK | PLAYBACK row 24, half R: label 4c + CParamDisplay 4d + CSlider 4e | slider (308, 142, 272, 20) panel-absolute (fieldset 12 + 296, 96 + 46) |
| ReverseFilterEnabled (804) | CCheckBox 40x20 + "Enable" label | FILTER row 24, half L: ToggleButton 5a "Filter" (CCheckBox gone) | (616, 140, 124, 24) panel-absolute |
| ReverseFilterType (806) | CSegmentButton 250x22 ("LP,HP,BP") + "Type" label | FILTER row 128, full: label 5e + CSegmentButton 5f | (616, 244, 260, 24) panel-absolute |
| ReverseFilterCutoff (805) | CSlider 250x20 + CParamDisplay 40x20 + "Cut" label | FILTER row 76, full: label 5b + CParamDisplay 5c + CSlider 5d | slider (616, 194, 260, 20) panel-absolute |

unmapped: none. All 10 control-tags (800 to 809) are placed; 809 has no view today and none after, by design (4.9).
Interactive views kept per tag (for the `lost-control` check): 800 ArcKnob, 801 CSlider, 802 CSegmentButton,
803 ArcKnob, 804 ToggleButton, 805 CSlider, 806 CSegmentButton, 807 ArcKnob, 808 ToggleButton.

## 3. Geometry check

Sibling overlap (template-relative, panel 900 x 480):

- Top level: core 1 (12..296 x 12..84), core 2 (308..592 x 12..84), core 3 (604..888 x 12..84), PLAYBACK (12..592 x 96..468),
  FILTER (604..888 x 96..468). Gutters 12 horizontally between cells and between PLAYBACK and FILTER (592 to 604); core strip to
  body 12 (84 to 96). No pair overlaps. Bottom edge 468, right edge 888, both inside 480 / 900.
- Core cell 1 children: knob 8..64 x 8..64; label 76..196 x 10..26; readout 76..196 x 30..54; Sync 200..276 x 32..54. No pair
  intersects; all inside 284 x 72.
- Core cells 2 and 3: knob, label and readout as above (no Sync). Inside 284 x 72.
- PLAYBACK children: label 4a 12..284 x 24..40; segment 4b 12..284 x 44..68; label 4c 296..504 x 24..40; readout 4d
  504..568 x 24..40; slider 4e 296..568 x 46..66. No pair intersects; all inside 580 x 372 (max x 568, max y 68).
- FILTER children: toggle 5a 12..136 x 44..68; label 5b 12..208 x 76..92; readout 5c 208..272 x 76..92; slider 5d 12..272 x 98..118; label 5e
  12..272 x 128..144; segment 5f 12..272 x 148..172. No pair intersects; all inside 284 x 372 (max x 272,
  max y 172).
- No stacked variants exist in this template (nothing conditional), so no stacked wrapper is needed. No bipolar cell,
  so no centre tick. Counts for the structure test (design 9.4): 3 plain `CViewContainer`s (the core cells), 2
  `FieldsetContainer`s.

Anchors for controllers: nothing in `controller.cpp` or `visibility_controller.h` hides or shows any Reverse control; the only
load-bearing structure is the `ReverseTimeSyncController` container, which holds the knob, readout and Sync toggle (all
direct children of container 1).

XML order is the visual order: core cell 1, 2, 3, then PLAYBACK, then FILTER (today's file declares the middle fieldset
first).

## 4. Deviations

1. **Playback Mode segment is a half cell although it has 3 entries.** Section 4.5 says 3 entries use a full cell, but
   section 7.8 places it in PLAYBACK half L (272 wide, 90.7 per segment, above the 65 pt minimum) so it shares row 24 with
   Crossfade. Section 7 governs placement, so this follows 7.8. Not a departure from the design.
2. **PLAYBACK is mostly empty (rows 76 to 284).** Not a deviation: principle 6 and section 7.8 specify the tall two-column
   section so the empty space sits inside a bordered group.
3. **Toggle width is 124, not 120.** Section 4.8 gives the toggle the half-cell width (124); section 7.4 quotes 120 only for
   Tape's Head toggles. This spec follows 4.8.
4. **No view carries `default-value`** (design 4.1): VST3Editor binds the registered defaults (ReverseChunkSize 0.246,
   ReverseFeedback 0, ReverseDryWet 0.5, TimeMode Free, Filter off), so today's `0.25` on the knob, which disagreed with
   the registered 0.246, is dropped rather than carried. The registered default is not touched.
5. **FILTER rows are Filter Cutoff 76, Filter Type 128.** The first draft of design 7.8 had them the other way round;
   design 7.8 was amended on 2026-10-28 to this spec's order (section 11), so this is not a deviation.
6. **Readouts.** `formatReverseParam` already prints the 6.4 formats except the core time's seconds switch: design 9.3
   "Core time readouts switch to seconds" makes 800 read "2.00 s" at its maximum ("10.0 ms" / "499.5 ms" / "2.00 s").

No control lacks a slot in the design system, so there is no placement-by-rule deviation.
