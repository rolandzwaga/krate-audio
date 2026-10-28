# PingPong panel spec (template `PingPongPanel`)

Authority: `design-system.md` sections 2-8 (4 = cells, 7.7 = this mode, 8 = controllers). Every coordinate is in VSTGUI
points, **parent-relative** (a view's origin is relative to the view that contains it, in uidesc nesting order).
Cell geometry comes from 4.1 (knob), 4.3 (slider), 4.6 (menu). Body cells are direct children of their fieldset
(no cell containers); only the core strip has cell containers.

Sources: `audits/pingpong.md`, `shots/before/editor-pingpong.png`, today's `PingPongPanel` (860 x 400, 3 fieldsets,
9 bound controls + 702 through the knob swap).

## 1. Layout at a glance (panel-relative, 900 x 480)

```
y   0 +------------------------------------------------------------------+
     12| core 1: TIME + Sync | core 2: FEEDBACK  | core 3: MIX     h 72    |
       |  (12,12,284,72)       (308,12,284,72)     (604,12,284,72)         |
     96| PING PONG (12,96,580,372)                | MODULATION  (604,96,284,180)
       |   L/R Ratio (half L)  Cross-Feedback (R) +-------------------------
       |   rest of section is bordered empty      | STEREO      (604,288,284,180)
    468+------------------------------------------------------------------+
```

Right edge of PING PONG = 592, column 3 starts at 604 (12 pt gutter). Bottom of every body section = 468 (12 pt margin
to 480).

## 2. Views in uidesc nesting order

Defaults every view inherits from the design system and that are **not** repeated per row:

- Label (L): `CTextLabel font="label-font" font-color="text-dim" text-alignment="left" transparent="true"`.
- Slider readout (R): `CParamDisplay font="value-font" font-color="text" text-alignment="right" back-color="~ TransparentCColor" frame-color="~ TransparentCColor" style-no-frame="true"`.
- Slider (S): `CSlider orientation="horizontal" draw-frame="true" draw-back="true" draw-value="true" draw-frame-color="slider-frame" draw-back-color="slider-track" draw-value-color="accent" frame-width="1" transparent="true"`. No `back-color`, `value-color` or `frame-color` on any CSlider.
- Menu attribute set (M, 4.7): `font="value-font" font-color="text" back-color="slider-track" frame-color="border" frame-width="1" style-round-rect="true" round-rect-radius="3" text-inset="6, 0" menu-popup-style="true" mouse-enabled="true" wants-focus="true"`.
- Section (F, 4.11): `FieldsetContainer fieldset-color="border" fieldset-title-color="text-dim" fieldset-font-size="11" fieldset-radius="4" fieldset-line-width="1" transparent="true"`.

| # | Parent | Class | Origin | Size | control-tag | Label / title | Attributes that differ from or add to the defaults |
|---|---|---|---|---|---|---|---|
| 0 | (root) | template `PingPongPanel` (`CViewContainer`) | 0, 0 | 900, 480 | | | `background-color="panel" transparent="false"` |
| **Core 1: Time + Sync** | | | | | | | |
| 1 | 0 | `CViewContainer` | 12, 12 | 284, 72 | | | `sub-controller="PingPongTimeSyncController" transparent="true"` |
| 2 | 1 | `ArcKnob` | 8, 8 | 56, 56 | `PingPongDelayTime` | | `arc-color="accent" guide-color="slider-frame" arc-width="3"`; no `default-value` (design 4.1) |
| 3 | 1 | `CTextLabel` | 76, 10 | 120, 16 | | "Time" | L |
| 4 | 1 | `CParamDisplay` | 76, 30 | 120, 24 | `PingPongDelayTime` | | `font="readout-font" font-color="text" text-alignment="left" back-color="~ TransparentCColor" frame-color="~ TransparentCColor" style-no-frame="true"` |
| 5 | 1 | `ToggleButton` | 200, 32 | 76, 22 | `PingPongTimeMode` | "Sync" | `title="Sync" title-position="right" icon-style="power" icon-size="0.6" font="label-font" on-color="accent" off-color="text-dim"`; no `default-value` (the parameter registers Synced as its default, so this panel opens synced: covered by the design 9.3 `verifyView` row) |
| **Core 2: Feedback** | | | | | | | |
| 6 | 0 | `CViewContainer` | 308, 12 | 284, 72 | | | `transparent="true"` |
| 7 | 6 | `ArcKnob` | 8, 8 | 56, 56 | `PingPongFeedback` | | `arc-color="accent" guide-color="slider-frame" arc-width="3"` |
| 8 | 6 | `CTextLabel` | 76, 10 | 120, 16 | | "Feedback" | L |
| 9 | 6 | `CParamDisplay` | 76, 30 | 120, 24 | `PingPongFeedback` | | readout-font variant of view 4 |
| **Core 3: Mix** | | | | | | | |
| 10 | 0 | `CViewContainer` | 604, 12 | 284, 72 | | | `transparent="true"` |
| 11 | 10 | `ArcKnob` | 8, 8 | 56, 56 | `PingPongMix` | | `arc-color="accent" guide-color="slider-frame" arc-width="3"` |
| 12 | 10 | `CTextLabel` | 76, 10 | 120, 16 | | "Mix" | L |
| 13 | 10 | `CParamDisplay` | 76, 30 | 120, 24 | `PingPongMix` | | readout-font variant of view 4 |
| **Section PING PONG (primary, spans columns 1-2, tall)** | | | | | | | |
| 14 | 0 | `FieldsetContainer` | 12, 96 | 580, 372 | | fieldset-title "PING PONG" | F |
| 15 | 14 | `CTextLabel` | 12, 24 | 272, 16 | | "L/R Ratio" | L (width 272 = half L, 4.6 label row `0 0 w 16`) |
| 16 | 14 | `COptionMenu` | 12, 44 | 272, 24 | `PingPongLRRatio` | | M (7 entries, strings come from the parameter) |
| 17 | 14 | `CTextLabel` | 296, 24 | 208, 16 | | "Cross-Feedback" | L (label w = 272 - 64) |
| 18 | 14 | `CParamDisplay` | 504, 24 | 64, 16 | `PingPongCrossFeedback` | | R |
| 19 | 14 | `CSlider` | 296, 46 | 272, 20 | `PingPongCrossFeedback` | | S |
| **Section MODULATION (standard, column 3 row 1)** | | | | | | | |
| 20 | 0 | `FieldsetContainer` | 604, 96 | 284, 180 | | fieldset-title "MODULATION" | F |
| 21 | 20 | `CTextLabel` | 12, 24 | 196, 16 | | "Mod Depth" | L |
| 22 | 20 | `CParamDisplay` | 208, 24 | 64, 16 | `PingPongModDepth` | | R |
| 23 | 20 | `CSlider` | 12, 46 | 260, 20 | `PingPongModDepth` | | S |
| 24 | 20 | `CTextLabel` | 12, 76 | 196, 16 | | "Mod Rate" | L |
| 25 | 20 | `CParamDisplay` | 208, 76 | 64, 16 | `PingPongModRate` | | R |
| 26 | 20 | `CSlider` | 12, 98 | 260, 20 | `PingPongModRate` | | S |
| **Section STEREO (standard, pinned column 3 bottom-most)** | | | | | | | |
| 27 | 0 | `FieldsetContainer` | 604, 288 | 284, 180 | | fieldset-title "STEREO" | F |
| 28 | 27 | `CTextLabel` | 12, 24 | 196, 16 | | "Width" | L |
| 29 | 27 | `CParamDisplay` | 208, 24 | 64, 16 | `PingPongWidth` | | R |
| 30 | 27 | `CSlider` | 12, 46 | 260, 20 | `PingPongWidth` | | S |

Notes on the table:

- Slider cell geometry (4.3): label `0 0 (w-64) 16`, readout `(w-64) 0 64 16`, slider `0 22 w 20`, offset by the cell origin
  (row y = 24 / 76, so slider y = row + 22). Menu cell (4.6): label `0 0 w 16`, control `0 20 w 24`, so the menu is at
  row + 20 = 44. Cells are 44 high: rows 24-68, 76-120 (a third row at 128 is unused here).
- XML order = visual order: core 1, 2, 3, PING PONG, MODULATION, STEREO. There are no stacked visibility variants in this panel.
- No title label ("PING PONG DELAY" goes, 4.11). No `PingPongNoteValue` view: the sub-controller swaps views 2 and 4
  onto it when Sync is on (4.9).
- No visibility controller touches PingPong (audit), so no anchor-tag constraint applies; the only load-bearing structure is
  that views 2, 4 and 5 are children of view 1 (8.1).
- The menu has no `segment-names`; COptionMenu takes its 7 strings from the `StringListParameter` (6.3).
- No bipolar cell, so no centre tick. Counts for the structure test (design 9.4): 3 plain `CViewContainer`s (the core
  cells), 3 `FieldsetContainer`s.
- `formatPingPongParam` already prints every 6.4 format (seconds switch included), so PingPong has no 9.3 formatter row.

## 3. Coordinate check

Sibling overlaps (each pair of siblings in the same parent):

- Panel root: containers 1 (x 12-296, y 12-84), 6 (308-592), 10 (604-888); fieldsets 14 (12-592 x 96-468), 20 (604-888 x 96-276),
  27 (604-888 x 288-468). Columns are separated by 12 pt gutters; rows 84 -> 96 and 276 -> 288 (12 pt). No overlap.
- Core 1 (container 1): knob 8-64 x 8-64; label 76-196 x 10-26; readout 76-196 x 30-54; toggle 200-276 x 32-54. Knob/label gap
  12 pt; label/readout 4 pt apart; readout ends x 196, toggle starts 200. No overlap (the before-defect of a 3 pt
  knob/toggle overlap is gone). Cells 2 and 3: knob, label, readout as above. No overlap.
- PING PONG: label 15 (12-284 x 24-40), menu 16 (12-284 x 44-68); label 17 (296-504 x 24-40), readout 18 (504-568 x 24-40),
  slider 19 (296-568 x 46-66). Half L ends 284, half R starts 296 (12 pt). Readout bottom 40 < slider top 46. No overlap.
- MODULATION: row 24: label 12-208 x 24-40, readout 208-272 x 24-40, slider 12-272 x 46-66; row 76: label/readout y 76-92,
  slider y 98-118. Row 1 ends 66, row 2 starts 76. No overlap.
- STEREO: same as MODULATION row 24. No overlap.

Containment (every view lies inside its parent):

- Template 900 x 480: furthest right edge 888 (core 3 and column-3 sections), furthest bottom 468.
- Containers 1, 6, 10 (284 x 72): max x 276, max y 64.
- PING PONG (580 x 372): max x 568 (inner pad 12), max y 68.
- MODULATION and STEREO (284 x 180): max x 272 (284 - 12), max y 118 / 66.

## 4. Mapping: today's bound control-tags -> new placement

| Today (PingPongPanel) | Tag (id) | New view(s) | Where |
|---|---|---|---|
| CParamDisplay 55x13 + ArcKnob 45x45 in the TIME & SYNC sub-container | PingPongDelayTime (700) | ArcKnob (view 2) + CParamDisplay (view 4), label "Time" (view 3) | core 1 container (sub-controller kept) |
| ToggleButton "sync" 49x10 | PingPongTimeMode (701) | ToggleButton "Sync" (view 5) | core 1 |
| none (via knob swap) | PingPongNoteValue (702) | no view; reached through the sub-controller swap of views 2 and 4 | core 1 |
| CSlider 195x20 "Feedback" + readout | PingPongFeedback (704) | ArcKnob (view 7) + CParamDisplay (view 9), label "Feedback" (view 8) | core 2 |
| CSlider 260x20 "Mix" + readout | PingPongMix (709) | ArcKnob (view 11) + CParamDisplay (view 13), label "Mix" (view 12) | core 3 |
| COptionMenu 75x22 "L/R Ratio" | PingPongLRRatio (703) | COptionMenu (view 16), label (view 15) | PING PONG, row 24, half L |
| CSlider "XFB" + readout | PingPongCrossFeedback (705) | CSlider (view 19) + CParamDisplay (view 18), label "Cross-Feedback" (view 17) | PING PONG, row 24, half R |
| CSlider "Depth" + readout | PingPongModDepth (707) | CSlider (view 23) + CParamDisplay (view 22), label "Mod Depth" (view 21) | MODULATION, row 24 |
| CSlider "Rate" + readout | PingPongModRate (708) | CSlider (view 26) + CParamDisplay (view 25), label "Mod Rate" (view 24) | MODULATION, row 76 |
| CSlider "Wid" + readout | PingPongWidth (706) | CSlider (view 30) + CParamDisplay (view 29), label "Width" (view 28) | STEREO, row 24 |

All 10 PingPong tags (700-709) are accounted for: 9 with views, 702 through the swap. Interactive views for the
`lost-control` check: 700 ArcKnob, 701 ToggleButton, 703 COptionMenu, 704 ArcKnob, 705/706/707/708 CSlider, 709 ArcKnob.

unmapped: none.

Dropped (non-bound): the title label "PING PONG DELAY" (4.11); the old TIME & SYNC and STEREO fieldsets (the new STEREO holds
Width only; L/R Ratio and Cross-Feedback moved to PING PONG; Time, Feedback and Mix moved to the core strip).

## 5. Deviations from the design system

1. **Feedback and Mix change widget** (CSlider -> ArcKnob). This is the design system's core-strip rule (4.1), not a
   departure. No view carries `default-value` (design 4.1, reconciled 2026-10-28): VST3Editor binds the registered
   defaults (PingPongDelayTime 0.050, PingPongFeedback 0.417, PingPongMix 0.5, TimeMode Synced), so today's `0.25` on
   the Time knob and `0` on the Sync toggle, both wrong against `pingpong_params.h`, are simply dropped.
2. No other deviation: every control had a slot; sections are exactly 7.7. The PING PONG tall section is mostly empty below
   row 24 (principle 6: empty space sits inside a bordered group).
