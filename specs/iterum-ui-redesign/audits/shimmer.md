# Shimmer panel audit (ShimmerPanel, 860 x 400)

Sources: `shots/before/editor-shimmer.png`, `editor.uidesc` lines 275-312, `inventory-before.json` (ShimmerPanel: 31 views, 18 bound, no stacked pairs), `controller.cpp:818`, `plugin_ids.h` 103-116.
Coordinates: fieldset-relative origins are given with the fieldset origin; panel-absolute = fieldset origin + child origin.

## Controls

Fieldsets: TIME & MIX (10,30 280x127), PITCH SHIFT (300,30 270x127), DIFFUSION & FILTER (580,30 270x127). Title label "SHIMMER DELAY" at (10,5 200x22). Every slider has a separate CTextLabel and CParamDisplay readout, paired by position only.

| Tag (id) | Label today | Canonical label | Role | Widget, size (origin inside fieldset) | Group | Visibility |
|---|---|---|---|---|---|---|
| ShimmerDelayTime (300) | none (readout only, "499") | Time | time | ArcKnob 45x45 (5,13 inside 55x68 sub-container at 10,10); readout CParamDisplay 55x13 | TIME & MIX | none; `ShimmerTimeSyncController` retags the knob to ShimmerNoteValue (311) when synced |
| ShimmerTimeMode (310) | "sync" | Sync | mode | ToggleButton 49x10 (3,55 in container), font-size 7 | TIME & MIX | none |
| ShimmerNoteValue (311) | none | Note Value | time | no control of its own; knob is retagged by DelayTimeSyncController | TIME & MIX | n/a |
| ShimmerFeedback (304) | "Feedback" | Feedback | feedback | CSlider 195x20 (75,28); label 65x16, readout 65x16 | TIME & MIX | none |
| ShimmerDryWet (309, id kShimmerMixId) | "Dry/Wet" | Mix | mix | CSlider 260x20 (10,100); label 130x16, readout 130x16 | TIME & MIX | none |
| ShimmerPitchSemitones (301) | "Semi" | Pitch (semitones) | character | CSlider 75x20 (10,28); label 40x16, readout 35x16 | PITCH SHIFT | none |
| ShimmerPitchCents (302) | "Cent" | Fine Pitch (cents) | character | CSlider 75x20 (95,28); label 35x16, readout 40x16 | PITCH SHIFT | none |
| ShimmerShimmerMix (303, id kShimmerPitchBlendId) | "Shim" | Pitch Blend | mix | CSlider 80x20 (180,28); label 40x16, readout 40x16 | PITCH SHIFT | none |
| ShimmerDiffusionSize (306) | "Size" | Diffusion Size | character | CSlider 75x20 (10,28); label 35x16, readout 40x16 | DIFFUSION & FILTER | none |
| ShimmerFilterEnabled (307) | "Filter" | Filter | filter (on/off) | CCheckBox 40x20 (60,55), empty title | DIFFUSION & FILTER | none |
| ShimmerFilterCutoff (308) | "Cut" | Filter Cutoff | filter | CSlider 90x20 (170,55); label 35x16, readout 25x16 | DIFFUSION & FILTER | none (not dimmed when filter is off) |
| Gain (footer) | "Gain" | Gain | output | shared footer ArcKnob | Editor footer | out of scope |

No visibility controller touches this panel. `controller.cpp` references Shimmer only for param registration, formatting, state and the time-sync sub-controller (line 818).

## Issues

### High

1. **CSlider colour defect, all 7 sliders.** Feedback (abs 85,58), Dry/Wet (20,130), Semi (310,58), Cent (395,58), Shim (480,58), Size (590,58), Cut (750,85). All set `back-color`/`value-color`/`frame-color`; CSlider reads `draw-back-color`/`draw-value-color`/`draw-frame-color`. Screenshot shows black tracks with white fill. Fix by renaming the attributes.
2. **Cutoff readout clipped and mis-placed.** CParamDisplay ShimmerFilterCutoff 25x16 at fieldset (145,55) (abs 725,85) is too narrow for "4.0k" or "20.0k" and butts into the slider at (170,55) (shot: "4.0k" touching the track). It is also on the slider row, while every other slider has its readout above it.
3. **Time knob is anonymous and its sync toggle overlaps it.** ArcKnob 45x45 at container (5,13) spans y 13-58; ToggleButton at (3,55) 49x10 overlaps it by 3 px (inventory overlap, views 4/5). The toggle's "sync" text is font-size 7, the smallest in the editor, and is drawn larger than its 10 px box. The knob has only a unit-less "499" readout and no "Time" caption; the synced note value is also unlabelled.
4. **Non-canonical and cryptic labels.** "Dry/Wet" should be "Mix" (plugins/iterum/CLAUDE.md). "Semi", "Cent", "Shim", "Cut" are abbreviations; "Size" does not say of what; "Shim" hides that the control is the pitch-shifted blend.

### Medium

5. **Mixed widgets for the same kind of parameter.** Time is an ArcKnob while every other continuous parameter is a slider. Two different on/off widgets: ToggleButton (sync) and a bare, untitled CCheckBox (Filter). The primary Mix control is a plain slider.
6. **Inconsistent sizes and column rhythm.** Slider widths: 195, 260, 75, 75, 80, 90, 75. In PITCH SHIFT the three columns are 75/75/80 wide at x 10/95/180 (Shim is 5 px wider, the right margin differs); label widths 40/35/40 and readout widths 35/40/40 differ per column, and each readout starts exactly where its label ends with no gap. Semi and Cent readouts show "+12"/"+0" without units.
7. **Cramped groups next to a large empty area.** PITCH SHIFT holds one row (y 10-48) with about 79 px empty below it inside the 127 px fieldset; DIFFUSION & FILTER has two sparse rows with about 50 px empty. Meanwhile panel area y 157-400 (860x243, 61 % of the panel) is completely empty.
8. **Related controls split, unrelated ones grouped.** Mix (TIME & MIX) and Pitch Blend (PITCH SHIFT) are both blends but live in different groups; Mix is bundled with Time and Feedback. Fieldset height 127 differs from Tape's 122, so rows do not align across modes. Fieldsets end at x=850, leaving a 10 px margin on the right but 10 px on the left, with 10 px gaps (consistent) but widths 280/270/270.
9. **Filter enable is detached from its group.** Cutoff stays fully drawn when Filter is off. "Filter [x]  Cut ---" reads as two separate items on one row; the enable belongs in the group header or ahead of the slider.
10. **Left edges inside TIME & MIX do not align.** Feedback slider starts at x=75 (shot x 210) while Dry/Wet starts at x=10 (shot x 80); the knob column pushes only the first row.

### Low

11. **Heading "SHIMMER DELAY" (10,5 200x22) duplicates the highlighted mode button** and sits left of the fieldset content in the shot; shared across modes, so a removal/merge candidate rather than a Shimmer-only defect.
12. **Fieldset legend crowds the first row.** First row at y=10 is within the title legend band; in the shot the "499" readout and "Semi" labels sit directly under the title text.
13. **Shared regions visible in this shot:** VersionLabel runs into the Gain knob (shot x 54-238, y 1160), and Check for Updates is plain text beside two bordered buttons. Out of scope for the panel but worth noting for the footer audit.
14. **Inline font-size.** The sync toggle's `font-size="7"` bypasses the label-font / value-font / section-font set used everywhere else.

## Summary

Eleven bound controls (plus the retagged NoteValue) in three fieldsets, each a label/readout/slider triplet except the time knob and filter checkbox. The most visible defects are the black/white slider rendering, the unlabelled time knob with the overlapping sync toggle, the clipped cutoff readout, and 61 % of the panel empty while the groups are cramped.
