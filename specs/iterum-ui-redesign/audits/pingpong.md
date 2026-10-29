# PingPong panel audit

Template `PingPongPanel` (860 x 400), `editor.uidesc` lines 444-481.
Screenshot: `specs/iterum-ui-redesign/shots/before/editor-pingpong.png` (2x render).
Inventory: `inventory-before.json`, template `PingPongPanel` (28 views, 16 bound).
Template-absolute rects below are `x,y,w,h` in panel coordinates.

## Controls

Every parameter in 700-709 has one control, except `PingPongNoteValue` (702), which is
bound through the sync sub-controller (see note 1). No visibility controllers touch this mode
(grep of `controller.cpp` and `visibility_controller.h` finds only the `PingPongTimeSyncController` hook at controller.cpp:824).

| Tag (id) | Label shown | Canonical label | Role | Widget, size | Group | Visibility |
|---|---|---|---|---|---|---|
| PingPongDelayTime (700) | value readout only, no name (shows "1/8") | Time | time | ArcKnob 45x45 at 25,53 + CParamDisplay 55x13 at 20,40 | TIME & SYNC | none; tag swaps to NoteValue when sync is on |
| PingPongTimeMode (701) | "sync" | Sync | mode | ToggleButton 49x10 at 23,95, font 7 | TIME & SYNC | none |
| PingPongNoteValue (702) | none (no view) | Note | time | no own view; sub-controller `PingPongTimeSyncController` swaps the knob onto it | TIME & SYNC | none |
| PingPongFeedback (704) | "Feedback" | Feedback | feedback | CSlider 195x20 at 85,58 | TIME & SYNC | none |
| PingPongMix (709) | "Mix" | Mix | mix | CSlider 260x20 at 20,130 | TIME & SYNC | none |
| PingPongLRRatio (703) | "L/R Ratio" | L/R Ratio | mode-specific | COptionMenu 75x22 at 310,58 | STEREO | none |
| PingPongWidth (706) | "Wid" | Width | width | CSlider 75x20 at 395,58 | STEREO | none |
| PingPongCrossFeedback (705) | "XFB" | Cross-Feedback | mode-specific (feedback) | CSlider 80x20 at 480,58 | STEREO | none |
| PingPongModDepth (707) | "Depth" | Mod Depth | modulation | CSlider 120x20 at 590,58 | MODULATION | none |
| PingPongModRate (708) | "Rate" | Mod Rate | modulation | CSlider 120x20 at 720,58 | MODULATION | none |

Each slider has a CTextLabel and a CParamDisplay readout above it, in the same group.

Notes
1. The only controls bound to 700 are the knob and its readout; 702 has no view of its own. In sync mode the knob drives the note value (`delay_time_sync_controller.h`).
2. Other fixed views: title label "PING PONG DELAY" 200x22 at 10,5 (section-font, accent).

## Issues

### High

1. **CSlider colour defect, all 6 sliders** (Feedback 85,58; Mix 20,130; Width 395,58; XFB 480,58; Depth 590,58; Rate 720,58).
   The uidesc sets `back-color`, `value-color` and `frame-color`. VSTGUI's CSlider reads `draw-back-color`, `draw-value-color` and `draw-frame-color`. The screenshot shows a black track with a white fill.
   At 0% (Depth) the track is solid black, so empty and full are hard to tell apart.
2. **Panel uses about 40% of its height.** Content ends at y=157 of a 400 px panel (`FieldsetContainer`s at 10,30 / 300,30 / 580,30, each 127 high). Everything from y=157 to y=400 (about 240 px, 860 wide) is empty. Controls are small and top-heavy.
3. **Unlabeled primary time control.** The delay-time ArcKnob (25,53 45x45) has no name label. Only a value readout ("1/8") above it and a tiny "sync" toggle below. A user cannot tell it is Time. Other panels share this pattern, so it needs one shared Time control group.
4. **ArcKnob / sync toggle overlap** (inventory defect, views 4 and 5): ArcKnob 25,53,45,45 spans y 53-98 and the ToggleButton 23,95,49,10 starts at y=95, a 3 px overlap. In the screenshot the "sync" word sits on the arc's lower end.

### Medium

5. **Inconsistent widget sizes for the same parameter kind.** Sliders are 195, 260, 75, 80, 120 and 120 wide, all 20 high. Feedback is 195 wide beside the knob, Mix is 260, Width 75 and XFB 80. Equivalent percent parameters have no common width.
6. **Mix is the second row of TIME & SYNC.** Mix (an output parameter) sits in the group with Time and Feedback, 260 wide at 20,130, while its label is left-aligned at 20,112 and its readout right-aligned at 150,112 (130 wide). The Feedback readout (150,40, 65 wide) is right-aligned at x=215, not on the slider's right edge x=280. The two rows do not share a right edge for values.
7. **Label and readout alignment varies per row.** Feedback label x=85 w65 / readout x=150 w65; L/R Ratio label x=310 w75; Wid label x=395 w35 / readout x=430 w40; XFB label x=480 w40 / readout x=520 w40; Depth label x=590 w60 / readout x=650 w60. Readouts sit right-aligned at different offsets from their slider, so values do not line up under one grid.
8. **Truncated width on narrow sliders.** Width (75 px) and XFB (80 px) are the narrowest controls in the mode, yet Width has the longest range (0-200%). Fine adjustment is cramped.
9. **Group splits related controls.** STEREO mixes a menu (L/R Ratio) with two sliders (Width, Cross-Feedback). Cross-Feedback is a feedback parameter but lives away from Feedback in TIME & SYNC, so the two feedback controls are 230 px apart.
10. **Groups are unevenly sized.** Fieldsets are 280, 270 and 270 wide with gutters of 10 px (x 290-300 and 570-580) and 10 px margins left (10) and right (850). The row is full width but each group is 127 high regardless of content: STEREO and MODULATION have one row of controls and leave about 60 px empty under them (y 80-157, screenshot shows blank lower halves of both).

### Low

11. **Abbreviations**: "Wid" (canonical Width), "XFB" (Cross-Feedback), "Depth" and "Rate" (canonical Mod Depth, Mod Rate), "sync" (lowercase, 7 pt).
12. **Selector widget inconsistency**: L/R Ratio is a COptionMenu 75x22 while Digital/Reverse use segment buttons for small enumerations. Menu shows "1:1" as flat grey with no dropdown affordance.
13. **Font sizes**: labels 9 pt, values 10 pt, the sync toggle 7 pt. 7 pt is below readable size at 1x.
14. **Title label** "PING PONG DELAY" at 10,5 (section-font, 200x22) is inconsistent with the mode name "PingPong" in the mode selector. The fieldsets start 3 px below its baseline, so the title crowds the fieldset titles in the screenshot.
15. **Fieldset titles** overlap the group border line and sit close to the controls below ("TIME & SYNC" at y=30 against the readout at y=40).
16. **Stacked views**: none (`stacked: []`). Bounds defects: only the overlap in issue 4.

## Summary

PingPong has ten parameters with one control each (702 via the knob). Problems are the slider colour defect (all six sliders), a 240 px empty lower area, an unlabeled Time knob overlapping its sync toggle, abbreviated labels, and inconsistent slider widths and label/readout offsets. Cross-Feedback is split from Feedback, and Mix is grouped with Time.
