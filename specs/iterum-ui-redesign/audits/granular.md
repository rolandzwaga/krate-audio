# Granular mode audit (template GranularPanel, 860 x 400)

Sources: `shots/before/editor-granular.png`, `inventory-before.json` (GranularPanel: 50 views, 30 bound controls), `editor.uidesc` lines 179-232, `controller.cpp` (L814-815), `plugin_ids.h` (100-118).
Coordinates are template-absolute (x, y, w, h). The screenshot is 2x: divide by 2 and subtract the panel origin (20, 60) to compare.

## Control table

| Tag (id) | Label today | Canonical label | Role | Widget, size at (x,y) | Group | Visibility |
|---|---|---|---|---|---|---|
| GranularDelayTime (102) | none (value readout only) | Delay Time | time | ArcKnob 45x45 at 25,53; CParamDisplay 55x13 at 20,40 | Time & Mix | always |
| GranularTimeMode (113) | "sync" | Sync | time | ToggleButton 49x10 at 23,95, font-size 7 | Time & Mix | always |
| GranularNoteValue (114) | none | Note Value | time | no view of its own; the sync sub-controller swaps the knob to this tag | Time & Mix | n/a |
| GranularFeedback (109) | "Feedback" | Feedback | feedback | CSlider 195x20 at 85,58; label 65x16 at 85,40; display 65x16 at 150,40 | Time & Mix | always |
| GranularMix (110; control-tag is named GranularDryWet) | "Dry/Wet" | Mix | mix | CSlider 260x20 at 20,130; label 130x16 at 20,112; display 130x16 at 150,112 | Time & Mix | always |
| GranularGrainSize (100) | "Size" | Grain Size | character | CSlider 75x20 at 310,58 | Grain Parameters | always |
| GranularDensity (101) | "Density" | Density | character | CSlider 75x20 at 395,58 | Grain Parameters | always |
| GranularPitch (103) | "Pitch" | Pitch | character | CSlider 80x20 at 480,58 | Grain Parameters | always |
| GranularJitter (115) | "Jitter" | Timing Jitter | modulation | CSlider 240x20 at 310,100 | Grain Parameters | always |
| GranularEnvelopeType (112) | "Envelope" | Grain Envelope | character | CSegmentButton 240x22 at 310,150 (4 segments) | Grain Parameters | always |
| GranularPitchQuant (116) | "Pitch Quantize" | Pitch Quantize | character | CSegmentButton 240x22 at 310,190 (5 segments) | Grain Parameters | always |
| GranularPitchSpray (104) | "Pitch" | Pitch Spray | modulation | CSlider 55x20 at 590,58 | Spray & Randomization | always |
| GranularPositionSpray (105) | "Pos" | Position Spray | modulation | CSlider 55x20 at 655,58 | Spray & Randomization | always |
| GranularPanSpray (106) | "Pan" | Pan Spray | modulation | CSlider 55x20 at 720,58 | Spray & Randomization | always |
| GranularReverseProb (107) | "Rev" | Reverse Probability | modulation | CSlider 55x20 at 785,58 | Spray & Randomization | always |
| GranularFreeze (108) | "Freeze" | Freeze | mode-specific | CCheckBox 50x22 at 590,101; label 50x16 at 590,85 | Spray & Randomization | always |
| GranularTexture (117) | "Tex" | Texture | character | CSlider 55x20 at 650,101 | Spray & Randomization | always |
| GranularStereoWidth (118) | "Wid" | Width | width | CSlider 55x20 at 715,101 | Spray & Randomization | always |

Wiring: no control in GranularPanel is shown or hidden by a visibility controller. The only controller involvement is `GranularTimeSyncController` on the 55x68 container at 20,40 (controller.cpp L814-815), which swaps the knob between 102 and 114 and must keep the knob, the readout and the sync toggle inside that container (`verifyView`). Gain and Mode live in the shared header and footer.

## Issues

### High

1. **CSlider color defect, all 12 sliders** (views 8, 11, 15, 18, 21, 24, 32, 35, 38, 41, 46, 49). They set `back-color`, `value-color` and `frame-color`; VSTGUI reads `draw-back-color`, `draw-value-color` and `draw-frame-color`. Every slider draws a black track with a white value bar (visible in the screenshot). The "slider-track", "accent" and "border" tokens are ignored.
2. **Zero and bipolar state unreadable.** With the defect above, sliders at 0 (Feedback, four sprays, Texture) look like empty black boxes. Pitch (bipolar, -24..+24, shows "+0.0") is half white with no center marker.
3. **Spray group labels are abbreviations, and "Pitch" appears twice.** "Pos" (655,40), "Rev" (785,40), "Tex" (650,85), "Wid" (715,85); "Pitch" at 590,40 (spray) and 480,40 (grain pitch) are indistinguishable.
4. **Label/value overlaps in the Spray group** (inventory defects): label 30x16 at 590,40 vs display at 615,40 (5 px overlap); same for Pos (655 vs 680), Rev (785 vs 810), Tex (650,85 vs 675,85), Wid (715,85 vs 740,85). Pan (720, width 25, display at 745) touches. The render shows "Tex 0" and "Wid 100" jammed.
5. **Time knob and Sync toggle overlap, and the knob has no name.** ArcKnob 25,53,45,45 (bottom y=98) vs ToggleButton 23,95,49,10 (inventory defect, views 4/5). The 7 pt "sync" text sits on the knob's lower arc, and the only text above the knob is a bare "500" readout.

### Medium

6. **Inconsistent widget for the same kind of parameter.** Delay Time is an ArcKnob; every other continuous parameter (12 of them) is a horizontal slider. Time and feedback/mix are the same role family but unlike the shared knobs used in other modes.
7. **Inconsistent slider sizes and left edges.** Slider sizes: 195, 260, 75 (x2), 80, 240, 55 (x6), all 20 tall. Pitch is 80 wide vs Size/Density 75. Feedback starts at x=85 (195 wide) but Mix at x=20 (260 wide) inside the same 280-wide fieldset.
8. **Spray rows misaligned.** Row 1 columns are x=590, 655, 720, 785; row 2 columns are 590, 650, 715 (Texture and Width sit 5 px left of the Pos and Pan columns), and the fourth column in row 2 is empty.
9. **Value-display heights vary.** CParamDisplay is 13 (Delay Time), 16 (most) and 17 (Jitter, 45x17); label widths 25/30/35/40/50/65 px. Readout fields are not on a common grid.
10. **Segment buttons cramped, vertically tight.** Envelope button bottom is y=172, which is exactly the top of the "Pitch Quantize" label (310,172). The 5-segment control is 240 wide; the render shows "Semitones" and "Octaves" touching their dividers. The uidesc `segment-names` use abbreviations ("Semis", "Octs", "5ths") while the render shows long names, so source and render disagree.
11. **Groups split related controls.** Pitch (grain group) and Pitch Quantize (grain group) are separated from Pitch Spray (spray group). Jitter (timing randomness) is in Grain Parameters while the other randomness controls are in Spray. Texture and Width are character/stereo controls inside "Spray & Randomization". Freeze is a mode switch inside the randomization group.
12. **Freeze uses a different idiom.** A bare CCheckBox (590,101,50,22) with a separate label above it, while the Sync on/off in the same panel is a ToggleButton.

### Low

13. **Empty area.** Content ends at y=222 (fieldsets are 192 tall at y=30), leaving 222..400 empty (about 45% of the panel; the screenshot shows a large blank block). Inside the fieldsets: below the Mix slider (20,150..222 on the left group) and below the Spray row 2 (y=123..222).
14. **Unequal fieldset widths.** TIME & MIX is 280 wide, the others 270 (x = 10, 300, 580).
15. **Heading crowding.** "GRANULAR DELAY" (10,5,200,22) ends at y=27, 3 px above the fieldsets at y=30.
16. **No units on readouts.** "500", "100", "10.0", "+0.0", "50", "0" with no ms, st, grains/s or %; Feedback range goes to 120% but shows a plain number.

## Summary

Granular is one knob plus 12 sliders in three columns, with every slider drawn in VSTGUI default colors by the draw-* attribute defect. The worst user-facing problems are the cryptic and overlapping Spray labels, the unlabeled Time knob overlapped by its Sync toggle, and the unreadable zero state of sliders. The panel also uses only the top 55% of its height. A redesign should fix the slider attributes (XSLT), use canonical names (Mix, Grain Size, Pitch Spray, Position Spray, Pan Spray, Reverse Probability, Texture, Width, Timing Jitter), regroup by role (Time / Grain / Pitch / Randomization / Output), pick one widget per parameter kind, and use the spare height. Constraints to keep: the sync sub-controller container structure, control-tags 100-118 (114 has no view and stays that way), and all 17 bound parameters.
