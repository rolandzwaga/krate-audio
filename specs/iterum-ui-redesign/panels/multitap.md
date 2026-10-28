# MultiTap panel spec (template `MultiTapPanel`, 900 x 480)

Authority: `design-system.md` (sections 2-8, esp. 4.1-4.11, 7.9, 8.2). Every coordinate is in VSTGUI points.
"Defaults" below means the attribute sets of design-system section 4 for that cell kind; only differences are listed
per view. Origins are parent-relative. Template-relative = panel-absolute (the template sits at 20,64 in the Editor).

Template: `<template name="MultiTapPanel" class="CViewContainer" origin="0, 0" size="900, 480" background-color="panel" transparent="false">`.
No panel title label (the old "MULTI-TAP DELAY" CTextLabel is removed).

## 1. Layout at a glance (template-relative x y w h)

```
y   0
   12  [Timing Pattern select 12,12,284,72] [Feedback knob 308,12,284,72] [Mix knob 604,12,284,72]
   96  [PATTERN 12,96,284,180] [NOTE VALUE 308,96,284,180 (cond.)] [FEEDBACK FILTER 604,96,284,180]
  288  [CUSTOM PATTERN 12,288,876,180 (cond.; Copy wrapper stacked at 12,288,876,24)]
  480
```

Placement follows 2.3/2.4: three standard sections in row 1, one 3-column section in row 2. No STEREO section.
Every slider and menu here is a full cell (260 wide).

## 2. Views in uidesc nesting order

### 2.1 Core cell 1: Timing Pattern (select cell, 4.2)

| # | Parent | Class | Origin | Size | control-tag | Label / title | Differs from defaults |
|---|---|---|---|---|---|---|---|
| 1 | MultiTapPanel | CViewContainer | 12, 12 | 284, 72 | none | | `transparent="true"`; **no** `sub-controller` (no TimeMode parameter) |
| 1a | 1 | CTextLabel | 8, 10 | 200, 16 | none | "Timing Pattern" | defaults |
| 1b | 1 | COptionMenu | 8, 30 | 200, 24 | `MultiTapTimingPattern` | | 4.7 menu set; the 20 entries come from the parameter; 200 pt fits "Golden Ratio", "Triplet 16th" |

### 2.2 Core cell 2: Feedback (knob cell, 4.1)

| # | Parent | Class | Origin | Size | control-tag | Label | Differs |
|---|---|---|---|---|---|---|---|
| 2 | MultiTapPanel | CViewContainer | 308, 12 | 284, 72 | none | | `transparent="true"` |
| 2a | 2 | ArcKnob | 8, 8 | 56, 56 | `MultiTapFeedback` | | `arc-color="accent" guide-color="slider-frame" arc-width="3"`; no `default-value` (design 4.1: VST3Editor binds the registered 0.455) |
| 2b | 2 | CTextLabel | 76, 10 | 120, 16 | none | "Feedback" | defaults |
| 2c | 2 | CParamDisplay | 76, 30 | 120, 24 | `MultiTapFeedback` | | knob-cell readout set (readout-font, text, left, transparent back/frame, `style-no-frame="true"`) |

### 2.3 Core cell 3: Mix (knob cell, 4.1)

| # | Parent | Class | Origin | Size | control-tag | Label | Differs |
|---|---|---|---|---|---|---|---|
| 3 | MultiTapPanel | CViewContainer | 604, 12 | 284, 72 | none | | `transparent="true"` |
| 3a | 3 | ArcKnob | 8, 8 | 56, 56 | `MultiTapDryWet` | | arc attributes as 2a; no `default-value` |
| 3b | 3 | CTextLabel | 76, 10 | 120, 16 | none | "Mix" | defaults |
| 3c | 3 | CParamDisplay | 76, 30 | 120, 24 | `MultiTapDryWet` | | as 2c |

### 2.4 PATTERN (section 1, row 1 column 1)

| # | Parent | Class | Origin | Size | control-tag | Label / title | Differs |
|---|---|---|---|---|---|---|---|
| 4 | MultiTapPanel | FieldsetContainer | 12, 96 | 284, 180 | none | PATTERN | 4.11 attribute set |
| 4a | 4 | CTextLabel | 12, 24 | 260, 16 | none | "Spatial Pattern" | defaults |
| 4b | 4 | COptionMenu | 12, 44 | 260, 24 | `MultiTapSpatialPattern` | | 4.7 menu set (7 entries from the parameter) |
| 4c | 4 | CTextLabel | 12, 76 | 196, 16 | none | "Taps" | defaults |
| 4d | 4 | CParamDisplay | 208, 76 | 64, 16 | `MultiTapTapCount` | | slider-cell readout (value-font, right) |
| 4e | 4 | CSlider | 12, 98 | 260, 20 | `MultiTapTapCount` | | slider-cell set with `draw-*-color` (4.3) |
| 4f | 4 | CTextLabel | 12, 128 | 196, 16 | none | "Morph Time" | defaults |
| 4g | 4 | CParamDisplay | 208, 128 | 64, 16 | `MultiTapMorphTime` | | slider-cell readout ("2000 ms" fits 64) |
| 4h | 4 | CSlider | 12, 150 | 260, 20 | `MultiTapMorphTime` | | slider-cell set (bottom y 170, inside 180) |

Rows 24 / 76 / 128 of the standard section.

### 2.5 NOTE VALUE (section 2, row 1 column 2; conditional)

| # | Parent | Class | Origin | Size | control-tag | Label / title | Differs |
|---|---|---|---|---|---|---|---|
| 5 | MultiTapPanel | FieldsetContainer | 308, 96 | 284, 180 | none | NOTE VALUE | 4.11 set. Hidden by `ContainerVisibilityController` except for Mathematical patterns (timing 14-18); the cell stays empty otherwise |
| 5a | 5 | CTextLabel | 12, 24 | 260, 16 | none | "Note Value" | defaults |
| 5b | 5 | COptionMenu | 12, 44 | 260, 24 | `MultiTapNoteValue` | | 4.7 menu set (10 entries). **Anchor: a direct child of 5** |
| 5c | 5 | CTextLabel | 12, 76 | 260, 16 | none | "Note Modifier" | defaults |
| 5d | 5 | CSegmentButton | 12, 96 | 260, 24 | `MultiTapNoteModifier` | | 4.5 set; `segment-names="None,Triplet,Dotted"` (about 86 pt per segment) |

### 2.6 FEEDBACK FILTER (section 2, row 1 column 3)

| # | Parent | Class | Origin | Size | control-tag | Label / title | Differs |
|---|---|---|---|---|---|---|---|
| 6 | MultiTapPanel | FieldsetContainer | 604, 96 | 284, 180 | none | FEEDBACK FILTER | 4.11 set; always visible |
| 6a | 6 | CTextLabel | 12, 24 | 196, 16 | none | "LP Cutoff" | defaults |
| 6b | 6 | CParamDisplay | 208, 24 | 64, 16 | `MultiTapFeedbackLPCutoff` | | slider-cell readout ("20.00 kHz" fits 64) |
| 6c | 6 | CSlider | 12, 46 | 260, 20 | `MultiTapFeedbackLPCutoff` | | slider-cell set |
| 6d | 6 | CTextLabel | 12, 76 | 196, 16 | none | "HP Cutoff" | defaults |
| 6e | 6 | CParamDisplay | 208, 76 | 64, 16 | `MultiTapFeedbackHPCutoff` | | slider-cell readout |
| 6f | 6 | CSlider | 12, 98 | 260, 20 | `MultiTapFeedbackHPCutoff` | | slider-cell set |

### 2.7 CUSTOM PATTERN (row 2, columns 1-3; conditional)

| # | Parent | Class | Origin | Size | control-tag / custom-view-name | Label / title | Differs |
|---|---|---|---|---|---|---|---|
| 7 | MultiTapPanel | FieldsetContainer | 12, 288 | 876, 180 | none | CUSTOM PATTERN | 4.11 set. Shown only when Timing = Custom (v >= 0.99); the controller finds it through the TapPatternEditor (tag 920) |
| 7a | 7 | CTextLabel | 548, 4 | 40, 16 | none | "Snap" | label-font, text-dim, `text-alignment="right"`, transparent; title line, the border passes behind it |
| 7b | 7 | COptionMenu | 592, 0 | 110, 24 | `MultiTapSnapDivision` | | 4.7 menu set (22 entries); title line |
| 7c | 7 | CView | 784, 0 | 80, 24 | `custom-view-name="ResetPatternButton"` | "Reset" drawn by the view | title line, right end (right edge 864) |
| 7d | 7 | CView | 12, 28 | 852, 140 | `custom-view-name="TapPatternEditor"` | | the controller sets tag 920; bottom y 168, 12 pad |

Snap label (x 548-588), Snap menu (592-702), Reset (784-864) and the editor (y 28-168) are pairwise disjoint; the
title line (y 0-24) never meets the editor (y 28 and below).

### 2.8 Copy wrapper (last child of the template, stacked on 7)

| # | Parent | Class | Origin | Size | custom-view-name | Differs |
|---|---|---|---|---|---|---|
| 8 | MultiTapPanel | CViewContainer | 12, 288 | 876, 24 | none | `transparent="true"`, **no** `background-color`; same origin as 7, so the inventory classes it as "stacked", not an overlap |
| 8a | 8 | CView | 714, 0 | 150, 24 | `CopyPatternButton` | panel-absolute 726,288,150,24; the controller sets tag 921 and shows it when Timing is not Custom |

Copy and Reset share the title-line slot at the right end (Copy panel-absolute x 726-876, Reset 796-876) and are never
visible together: Reset hides with 7, Copy hides when Timing = Custom. Copy does not touch the Snap label (560-600) or
the Snap menu (604-714).

## 3. Geometry check

- Core cells: x 12-296, 308-592, 604-888, y 12-84. Contents: cell 1 max x 208 and y 54; cells 2-3 max x 196 and y 64. All inside 284 x 72.
- Row 1 sections y 96-276 at x 12-296 / 308-592 / 604-888 (12 gutters), no sibling overlap. Row 2 section y 288-468 at x 12-888, 12 below row 1. Everything is inside 900 x 480 with 12 margins.
- Inside the 284-wide sections the widest cell ends at x 272 (12 right pad). Lowest control bottoms: PATTERN 170, NOTE VALUE 120, FEEDBACK FILTER 118 (all <= 172); CUSTOM editor 168 (<= 180).
- Sibling overlaps: none, except the deliberately stacked pair 7 / 8. No bipolar cell, so no centre tick.
- Counts for the structure test (design 9.4): 4 plain `CViewContainer`s (three core cells and the Copy wrapper 8), 4
  `FieldsetContainer`s.

## 4. Mapping: every bound control-tag in today's MultiTapPanel

| Today (tag, id) | Today's place | New place | Widget |
|---|---|---|---|
| MultiTapTimingPattern (900) | PATTERN menu 80x22 | core cell 1 (1b) | select cell COptionMenu |
| MultiTapSpatialPattern (901) | PATTERN menu | PATTERN row 24 (4b) | menu cell |
| MultiTapTapCount (902) | PATTERN slider 70x20 + display | PATTERN row 76 (4d, 4e) | slider cell |
| MultiTapFeedback (905) | MIX slider + display | core cell 2 (2a, 2c) | ArcKnob + readout |
| MultiTapDryWet (909) | MIX slider + display | core cell 3 (3a, 3c) | ArcKnob + readout |
| MultiTapFeedbackLPCutoff (906) | FEEDBACK FILTERS slider + display | FEEDBACK FILTER row 24 (6b, 6c) | slider cell |
| MultiTapFeedbackHPCutoff (907) | FEEDBACK FILTERS slider + display | FEEDBACK FILTER row 76 (6e, 6f) | slider cell |
| MultiTapMorphTime (908) | MORPHING slider + display | PATTERN row 128 (4g, 4h) | slider cell |
| MultiTapNoteValue (911) | NOTE VALUE menu 70x22 | NOTE VALUE row 24 (5b) | menu cell |
| MultiTapNoteModifier (912) | NOTE VALUE menu 80x22 | NOTE VALUE row 76 (5d) | segment cell (3) |
| MultiTapSnapDivision (913) | CUSTOM editor, 730,0 | CUSTOM PATTERN title line (7b) | menu on the title line |
| ResetPatternButton (custom, 923) | CUSTOM editor, 210,0 | CUSTOM PATTERN title line (7c) | custom view 80x24 |
| TapPatternEditor (custom, 920) | CUSTOM editor, 8,10 820x135 | CUSTOM PATTERN (7d) 852x140 | custom view |
| CopyPatternButton (custom, 921) | not placed (dead controller) | Copy wrapper (8a) | custom view 150x24, new placement |

unmapped: none.

Removed views: the panel title label, the MORPHING and MIX fieldsets (Morph Time moves to PATTERN, Feedback and Mix to
the core strip) and the old per-control labels.

Label changes (6.2): Dry/Wet -> Mix, Time -> Morph Time, Timing -> Timing Pattern, Spatial -> Spatial Pattern,
Note -> Note Value, Modifier -> Note Modifier, LP Cut / HP Cut -> LP Cutoff / HP Cutoff. NOTE VALUE's title drops
"(Mathematical Patterns)"; "FEEDBACK FILTERS" becomes "FEEDBACK FILTER".

## 5. Controller / visibility contract (no C++ change for layout)

- `MultiTapNoteValue` (911) is a direct child of the NOTE VALUE fieldset; only 911, 912 and their labels live there.
- TapPatternEditor (920) is a direct child of CUSTOM PATTERN; only Snap, its label, Reset and the editor live there. Copy (921) is outside it, in wrapper 8.
- No `sub-controller` in this template (no TimeMode parameter).
- Frozen: control-tag names and numbers, parameter defaults, ranges, state.

## 6. Deviations

None from design-system 7.9, which ruling 6 (design 10) confirmed on 2026-10-28: Snap label, Snap menu and Reset on
the CUSTOM PATTERN title line, the tap editor 852 x 140. One detail this spec adds where the design system gives only
a rect: the Snap label is right-aligned so it sits against its menu. No view carries `default-value` (design 4.1).
`formatMultiTapParam` already prints every 6.4 format (Taps `"%d"`, cutoffs Hz/kHz, Morph Time `"%.0f ms"`, percents),
so MultiTap has no 9.3 formatter row.
