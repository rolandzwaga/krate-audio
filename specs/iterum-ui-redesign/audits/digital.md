# Digital mode audit (template DigitalPanel, 860 x 400)

Sources: `shots/before/editor-digital.png` (2x render at default values), `editor.uidesc` lines 394-446, `inventory-before.json` (templates.DigitalPanel), `controller.cpp` (lines 822, 870-873), `plugin_ids.h` (600-614).
Coordinates below are template-absolute (x, y, w, h); the screenshot is 2x.

## Control table

| Tag (id) | Label shown | Canonical label | Role | Widget, size | Group | Visibility |
|---|---|---|---|---|---|---|
| DigitalDelayTime (600) | none; value readout above knob (CParamDisplay 20,40,55,13) | Time | time | ArcKnob 45x45 at 25,53; CParamDisplay 55x13 | TIME & SYNC | none. Sub-controller `DigitalTimeSyncController` swaps this knob and readout to DigitalNoteValue (602) when synced |
| DigitalTimeMode (601) | "sync" | Sync | time | ToggleButton 49x10 at 23,95, font-size 7 | TIME & SYNC | none |
| DigitalNoteValue (602) | none (no view of its own; reached by tag swap on the knob) | Note Value | time | shares the ArcKnob | TIME & SYNC | shown instead of Time while synced |
| DigitalFeedback (603) | "Feedback" | Feedback | feedback | CSlider 195x20 at 85,58 | TIME & SYNC | none |
| DigitalMix (610) | "Mix" | Mix | mix | CSlider 260x20 at 20,130 | TIME & SYNC | none |
| DigitalEra (605) | "Era" | Era | mode-specific | CSegmentButton 250x22 at 310,60 (Pristine / 80s Digital / Lo-Fi) | CHARACTER | none |
| DigitalAge (606) | "Age" (label tag 9902) | Age | character | CSlider 250x20 at 310,120 | CHARACTER | hidden while Era is Pristine: `VisibilityController` (controller.cpp:870) hides tags 9902 and 606 when Era < 0.25 |
| DigitalLimiterCharacter (604) | "Limiter" | Limiter | output | CSegmentButton 250x22 at 310,170 (Soft / Medium / Hard) | CHARACTER | none |
| DigitalModDepth (607) | "Dep" | Mod Depth | modulation | CSlider 120x20 at 590,128 | MODULATION | none |
| DigitalModRate (608) | "Rate" | Mod Rate | modulation | CSlider 120x20 at 720,130 | MODULATION | none |
| DigitalModWaveform (609) | "Wave" | Mod Waveform | modulation | COptionMenu 80x22 at 590,174 | MODULATION | none |
| DigitalWidth (612) | "Width" | Width | width | CSlider 170x20 at 670,40 | STEREO | none |
| DigitalWavefoldAmount (611) | "Amount" | Wavefold Amount | character | CSlider 160x20 at 20,258 | WAVEFOLD | none |
| DigitalWavefoldSymmetry (614) | "Sym" | Wavefold Symmetry | character | CKnob 50x50 at 210,262 | WAVEFOLD | none |
| DigitalWavefoldType (613) | "Type" | Wavefold Type | character | CSegmentButton 220x22 at 60,320 (Simple / Serge / Buchla / Lockhart) | WAVEFOLD | none |
| Gain (footer, shared) | "Gain" | Gain | output | ArcKnob in the Editor footer | footer | none |

Every Digital parameter (600-614) has a control. Value readouts (CParamDisplay) are separate views bound to the same tag as their control (600, 603, 610, 606, 607, 608, 612, 611, 614).

## Issues

### High

1. **CSlider colour defect, 7 sliders.** Views 8, 11, 17, 23, 26, 32, 36 (Feedback 85,58,195,20; Mix 20,130,260,20; Age 310,120,250,20; Mod Depth 590,128,120,20; Mod Rate 720,130,120,20; Width 670,40,170,20; Wavefold Amount 20,258,160,20) set `back-color`, `value-color` and `frame-color`. VSTGUI reads `draw-back-color`, `draw-value-color` and `draw-frame-color`. In the screenshot every track is black with a white fill, which clashes with the light panel. (Age is hidden at the default Era, so only 6 show in the screenshot.)
2. **Overlap: ArcKnob and sync ToggleButton.** DigitalDelayTime knob at 25,53,45,45 overlaps the "sync" toggle at 23,95,49,10 (inventory defect; the knob ends at y=98, the toggle starts at y=95). In the screenshot the knob arc runs into the "sync" text.
3. **Time knob has no label.** The only text near it is the value readout (20,40,55,13, shows "1/8"); there is no "Time" caption, so the most important control in the panel is the only one without a name. The readout also uses `label-font` while all other readouts use `value-font`.
4. **Inconsistent widget choice for the same kind of parameter.** Wavefold Symmetry is a plain CKnob (210,262,50,50), not the shared ArcKnob used for Time and Gain, so it looks different (black ring, blue corona), while every other percent/bipolar parameter is a slider. The main controls use three styles: ArcKnob (Time), sliders (Feedback, Mix), CKnob (Symmetry).
5. **Unbalanced layout, large dead areas.** The panel uses y=30 to 352 of 400. The right column ends at y=202 while the left runs to y=352; the area 300-850 x 202-400 is empty except footer space (screenshot: lower-right half blank). CHARACTER (300,30,270,172) holds, with Age hidden in Pristine (the default), only Era and Limiter with about 85px of empty space between them (screenshot y=300-430 at 2x).

### Medium

6. **Abbreviations and stale names.** "Dep" (590,110,35,16) should be "Mod Depth"; "Rate" (720,112,35,16) should be "Mod Rate"; "Sym" (190,240,40,16) should be "Symmetry"/"Wavefold Symmetry"; "sync" is lower case at font-size 7 (about 10px high, hard to read). In the uidesc, the WavefoldType `segment-names` list "Lock" (view 38) and Era lists "Pristine,80s,Lo-Fi", but the render shows "Lockhart" and "80s Digital" (names come from the parameter), so the uidesc names are stale.
7. **Groups split or isolate related controls.** Mix (an output/blend control) sits in TIME & SYNC with Feedback. Width is alone in a one-row STEREO box (580,30,270,42), separated by a 28px gap from MODULATION (580,100,270,102). Wavefold's three controls (slider, knob, segment button) use different label placements.
8. **Row alignment and sizes are inconsistent.**
   - Slider widths: 195, 260, 250, 120, 120, 170, 160.
   - Feedback slider starts at x=85 while Mix starts at x=20: two left edges in one group.
   - Mod Depth slider y=128 vs Mod Rate slider y=130, a 2px mismatch between neighbours; Mod Depth readout (670,112,40,16) is only 10px from the Rate label (720,112).
   - Readout placement varies: right-aligned above the slider for Feedback, Mix, Age, Mod; beside the label for Width (634,40,30,20; 20 high versus 16 elsewhere) and Wavefold Amount (70,240,40,16, floating mid-row instead of at the slider's right edge; screenshot "0%" next to "Amount").
   - Segment buttons: 250x22 at x=310 (labels centred above) in CHARACTER, 220x22 at x=60 with a left-side "Type" label in WAVEFOLD.
9. **Cramped, misaligned Wavefold box (10,230,280,122).** Knob at x=210 sits under the "Sym" label at x=190 (20px off); Sym label and readout are 22px above the knob; Amount slider (160 wide) leaves a gap to the knob column.
10. **COptionMenu "Wave" is small and styled differently.** 80x22 at 590,174 in an otherwise empty row, a pale rounded box unlike the dark segment buttons (screenshot "Sine"), half the width of the Mod sliders, leaving 190px of the MODULATION bottom row empty.

### Low

11. **Panel heading.** "DIGITAL DELAY" (10,5,200,22) duplicates the selected mode tab; the screenshot shows its text starting at x≈78 rather than at its origin (10) because it is centred in a 200px box, so it does not line up with the TIME & SYNC box edge. Same style in other modes.
12. **Fieldset titles in capitals with an ampersand** ("TIME & SYNC") while the tab bar uses title case.
13. **Four font sizes inside TIME & SYNC** (label-font, value-font, label-font readout, font-size 7 "sync").
14. **Hidden Age leaves a layout hole** (Age label 310,102,50,16; readout 510,102,50,16; slider 310,120,250,20 vanish in Pristine; CHARACTER keeps full height).
15. **Controller couplings to preserve.** `VisibilityController` hides tags 9902 (Age label) and 606 (Age slider and readout) by tag. `DigitalTimeSyncController` is a `sub-controller` on the 55x68 container (20,40,55,68) wrapping the Time readout, knob and sync toggle; these three views must stay in one container with that sub-controller name, and Age views must keep tags 9902 and 606.

## Cross-mode note
PingPongPanel uses the same TIME & SYNC block coordinates but its fieldsets are 127 high versus 172 in Digital, and its labels are abbreviated differently ("Wid", "XFB"), so the same block is not one fixed size across modes.
