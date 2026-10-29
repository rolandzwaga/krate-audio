# MultiTap panel audit (template `MultiTapPanel`, 860 x 400, placed at 20,60 in the 900 x 600 editor)

Sources: `shots/before/editor-multitap.png` (2x, default parameters), `editor.uidesc` lines 510-554, `inventory-before.json` (MultiTapPanel: 37 views, 17 bound controls, `stacked` empty, 3 overlap defects), `controller.cpp` (createCustomView ~L705-775, visibility controllers ~L875-905), `parameters/multitap_params.h`.
Coordinates are template-absolute (x, y, w, h) unless stated.

## State shown in the screenshot

At default values (Timing = "Quarter") two containers are hidden by controllers: NOTE VALUE (shown only when Timing is one of the Mathematical patterns, normalized 14/19 to 18/19) and CUSTOM PATTERN EDITOR (shown only when Timing = Custom, normalized >= 0.99). The default view therefore shows four fieldsets in two rows, an empty top-left at 10,30, and an empty lower half (y 190-400). The hidden containers still have to be designed for.

## Control table

| Control-tag | Id | Label shown | Canonical label | Role | Widget, size, origin | Group (fieldset rect) | Visibility |
|---|---|---|---|---|---|---|---|
| MultiTapTimingPattern | 900 | Timing | Timing Pattern | mode-specific | COptionMenu 80x22 at 310,58 | PATTERN 300,30,270,62 | always |
| MultiTapSpatialPattern | 901 | Spatial | Spatial Pattern | mode-specific | COptionMenu 80x22 at 400,58 | PATTERN | always |
| MultiTapTapCount | 902 | Taps (+ CParamDisplay 35x16 at 525,40) | Taps | mode-specific | CSlider 70x20 at 490,58 | PATTERN | always |
| MultiTapFeedback | 905 | Feedback (+ display 60x16 at 80,130) | Feedback | feedback | CSlider 120x20 at 20,148 | MIX 10,120,280,67 | always |
| MultiTapDryWet | 909 | Dry/Wet (+ display 65x16 at 215,130) | Mix | mix | CSlider 130x20 at 150,148 | MIX | always |
| MultiTapFeedbackLPCutoff | 906 | LP Cut (+ display 60x16 at 370,130) | LP Cutoff | filter | CSlider 120x20 at 310,148 | FEEDBACK FILTERS 300,120,270,67 | always |
| MultiTapFeedbackHPCutoff | 907 | HP Cut (+ display 60x16 at 500,130) | HP Cutoff | filter | CSlider 120x20 at 440,148 | FEEDBACK FILTERS | always |
| MultiTapMorphTime | 908 | Time (+ display 40x16 at 800,42) | Morph Time | time | CSlider 250x20 at 590,62 | MORPHING 580,30,270,62 | always |
| MultiTapNoteValue | 911 | Note | Note Value | time | COptionMenu 70x22 at 20,58 | NOTE VALUE (Mathematical Patterns) 10,30,280,57 | hidden unless Timing is Mathematical (ContainerVisibilityController, container found by tag 911) |
| MultiTapNoteModifier | 912 | Modifier | Note Modifier | time | COptionMenu 80x22 at 100,58 | NOTE VALUE | same container |
| MultiTapSnapDivision | 913 | Snap (label 35x18 at 705,212) | Snap | mode-specific | COptionMenu 100x22 at 740,212 | CUSTOM PATTERN EDITOR 10,212,840,162 | only when Timing = Custom (container found by TapPatternEditor tag 920) |
| ResetPatternButton (custom view, tag 923) | none | drawn by the view | Reset | action | CView 60x22 at 220,212 | CUSTOM PATTERN EDITOR | with container |
| TapPatternEditor (custom view, tag 920) | edits 950-981 | none | Tap Pattern | mode-specific | CView 820x135 at 18,222 | CUSTOM PATTERN EDITOR | with container |
| CopyPatternButton (custom view, tag 921) | none | not placed in uidesc | Copy to Custom | action | none (controller builds 90x22 if asked) | none | controller expects it visible when Timing is not Custom |

Load-bearing for the redesign: the container holding tag 911 is hidden for non-Mathematical patterns, so nothing else that must stay visible may share it; the container holding TapPatternEditor (tag 920) is hidden unless Custom, so Snap and Reset must stay inside it and nothing else may.

## Issues

### High

1. **CSlider colour defect, all six sliders:** Taps 490,58,70,20; Feedback 20,148,120,20; Dry/Wet 150,148,130,20; LP 310,148,120,20; HP 440,148,120,20; Morph Time 590,62,250,20. Each sets `back-color` / `value-color` / `frame-color`; VSTGUI reads the `draw-` prefixed names, so the screenshot shows a black track with a white fill instead of the slider-track and accent colours. These are the most prominent elements on the panel.
2. **Copy Pattern button has no view.** `createCustomView` builds `CopyPatternButton` and a VisibilityController (tag 921, visible when Timing is not Custom) exists, but `MultiTapPanel` contains no `custom-view-name="CopyPatternButton"`, so the controller is dead and users cannot copy a preset pattern into Custom. It must sit outside the custom-editor container (that container is hidden for non-Custom patterns), e.g. in or next to PATTERN.
3. **Large empty area and a layout that changes with state.** Default state leaves roughly y 190-400 (860 x 210) empty, and the top-left 10,30,280,57 empty. NOTE VALUE fills that slot only for Mathematical patterns; the 840x162 editor appears at 10,212 only for Custom. Space is not reserved, so the panel looks unfinished at default and controls jump when the pattern changes.
4. **Menus too narrow for their entries.** Timing menu 80x22 at 310,58 has entries such as "Triplet 8th", "Dotted 16th", "Golden Ratio", "Exponential"; Spatial menu 80x22 at 400,58 has "Alternating". Expect clipped text.

### Medium

5. **Mixed widgets for one kind of parameter.** Continuous parameters are all 20 px horizontal sliders with the value in a separate row, while the footer Gain is an ArcKnob. Taps (integer 2-16) is a 70 px slider, the shortest control on the panel, with its value display in a 35 px cell at 525,40. Compare with the other modes' shots before choosing one widget per role.
6. **Same-role widgets differ in size.** Slider widths 70, 120, 130, 120, 120, 250. Feedback (120) and Dry/Wet (130) share one fieldset with label cells of 60 and 65. Menu widths 70, 80, 80, 80, 100. Fieldset sizes 280x57, 270x62 (x2), 280x67, 270x67, 840x162; column widths 280 vs 270, row-1 heights 57 vs 62, with gutters of 10 px that are accidental.
7. **Baselines not aligned.** Row 1: menus and the Taps slider at y 58, but the Morph Time slider at y 62 (child origin 32 vs 28) and its display at y 42 (origin 12 vs 10 elsewhere). Fieldset bottoms in row 1 are 87, 92, 92. Vertical gaps: row 1 to row 2 is 28-33 px, row 2 to the editor 25 px.
8. **Groups split related controls.** Feedback sits in MIX although it belongs with LP/HP Cutoff in FEEDBACK FILTERS. Note Value and Note Modifier define the timing selected by Timing Pattern but live in a separate fieldset at 10,30. Taps belongs to the pattern but is squeezed into a 70 px slot, and Morph Time is alone in a 270x62 fieldset with a 250 px slider. PATTERN holds two menus and a slider in one 270 px row.
9. **NOTE VALUE fieldset title** "NOTE VALUE (Mathematical Patterns)" is a 34 character title in a 280 px frame; every other title is one or two words.
10. **Value formats and cells.** LP Cut shows "20.00 kHz" and HP Cut "20 Hz" in 60 px cells (the first nearly fills it); the Morph Time display (40 px) is tight for "2000 ms"; Taps display is 35 px.
11. **Three overlap defects in CUSTOM PATTERN EDITOR (inventory):** ResetPatternButton 220,212,60,22, the Snap label 705,212,35,18 and MultiTapSnapDivision 740,212,100,22 each overlap TapPatternEditor 18,222,820,135. They sit on the fieldset's top border by design, which draws them over the frame line. Snap's right edge is 840, the editor's 838, the fieldset's 850.

### Low

12. **Abbreviations and non-canonical labels:** "Dry/Wet" must read "Mix" (table in plugins/iterum/CLAUDE.md); "LP Cut" / "HP Cut" should be "LP Cutoff" / "HP Cutoff" or low-pass / high-pass words; "Time" should be "Morph Time"; "Note", "Spatial", "Timing" are short forms of Note Value, Spatial Pattern, Timing Pattern. Menu entry text is parameter data and frozen ("Eighth" in Timing vs "8th" in Note Value), but widths must fit it.
13. **Section title.** "MULTI-TAP DELAY" (CTextLabel 10,5,200,22, section-font) renders centred in its box (text starts near x 48 in template space, not at the 10 px margin), spells the mode differently from the "MultiTap" tab, and duplicates the selector.
14. **Label sizes are ad hoc:** CTextLabel 50x16, 60x16 (x4), 80x16 (x2), 35x16, 65x16, 40x16, and the Snap label 35x18.
15. **Reset button** is 60x22 with no caption in the uidesc and a different size from the Snap menu (100x22) and from the footer buttons.

## Recommended direction (MultiTap scope)

Fix the slider colour attributes; use one widget and one size per role; group as Pattern (Timing Pattern, Spatial Pattern, Taps, Note Value, Note Modifier, Morph Time, Copy), Feedback (Feedback, LP Cutoff, HP Cutoff) and Mix; make the Custom editor a full-width strip with Snap and Reset in a header row inside its container, not overlapping the editor; reserve space for the conditional groups; place the missing CopyPatternButton outside the editor container; keep every control-tag name and number unchanged.
