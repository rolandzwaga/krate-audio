# Tape mode panel audit

Scope: template `TapePanel` (860 x 400) in `plugins/iterum/resources/editor.uidesc` (line 312), screenshot
`specs/iterum-ui-redesign/shots/before/editor-tape.png`, inventory `inventory-before.json` (45 views, 26 bound controls, 0 geometry defects, 0 stacked pairs).
All rects below are template-absolute (x, y, w, h) in the 860 x 400 panel (fieldset origin + child origin). Screenshot is 2x, panel origin at (20, 60) of the editor.

Controller scope: no Tape-specific code. `controller.cpp` only registers/formats/loads Tape params (`registerTapeParams`, `formatTapeParam`, `loadTapeParamsToController`). No VisibilityController, ContainerVisibilityController or TimeSyncController references any Tape tag or container, so nothing here is load-bearing for container structure. There is no `TapeTimeSyncController` and no Tape time-mode / note-value parameter exists in `plugin_ids.h` (kTapeBaseId 400 block), so Tape has no sync option by design; the time control is `TapeMotorSpeed`.

## Control table

Parameter ranges from `plugin_ids.h` comments. Every control is paired with a CParamDisplay readout except where noted.

| Tag | Label shown | Canonical label | Role | Widget, size | Group | Rect | Visibility |
|---|---|---|---|---|---|---|---|
| TapeFeedback | Feedback | Feedback | feedback | CSlider 120x20 (+CParamDisplay 60x16) | TIME & MIX | slider (20,58,120,20) | always |
| TapeMix | Mix | Mix | mix | CSlider 130x20 (+CParamDisplay 65x16) | TIME & MIX | slider (150,58,130,20) | always |
| TapeMotorSpeed | Spd | Delay Time (it is the delay time, 20-2000 ms) | time | CSlider 80x20 (+CParamDisplay 40x16) | CHARACTER | slider (20,218,80,20) | always |
| TapeMotorInertia | Iner | Inertia (or Motor Inertia) | character | CSlider 80x20 (+CParamDisplay 40x16) | CHARACTER | slider (110,218,80,20) | always |
| TapeWear | Wear | Wear | character | CSlider 80x20 (+CParamDisplay 40x16) | CHARACTER | slider (200,218,80,20) | always |
| TapeSaturation | Sat | Saturation | character | CSlider 80x20 (+CParamDisplay 40x16) | CHARACTER | slider (20,263,80,20) | always |
| TapeAge | Age | Age | character | CSlider 80x20 (+CParamDisplay 40x16) | CHARACTER | slider (110,263,80,20) | always |
| TapeSpliceEnabled | Enable (label above) | Splice Enable | mode-specific | CCheckBox 50x22, no title | SPLICE | (590,58,50,22) | always |
| TapeSpliceIntensity | Intens | Splice Intensity | mode-specific | CSlider 180x20 (+CParamDisplay 130x16) | SPLICE | slider (660,58,180,20) | always (not dimmed when splice is off) |
| TapeHead1Enabled | Head 1 | Head 1 | mode-specific | CCheckBox 30x18, no title | TAPE HEADS | (365,40,30,18) | always |
| TapeHead1Level | none (column header "Level" at (400,115,75,16)) | Head 1 Level | mode-specific | CSlider 75x18, NO readout | TAPE HEADS | (400,40,75,18) | always |
| TapeHead1Pan | none (column header "Pan" at (485,115,75,16)) | Head 1 Pan | mode-specific | CSlider 75x18, NO readout | TAPE HEADS | (485,40,75,18) | always |
| TapeHead2Enabled | Head 2 | Head 2 | mode-specific | CCheckBox 30x18 | TAPE HEADS | (365,65,30,18) | always |
| TapeHead2Level | as above | Head 2 Level | mode-specific | CSlider 75x18, no readout | TAPE HEADS | (400,65,75,18) | always |
| TapeHead2Pan | as above | Head 2 Pan | mode-specific | CSlider 75x18, no readout | TAPE HEADS | (485,65,75,18) | always |
| TapeHead3Enabled | Head 3 | Head 3 | mode-specific | CCheckBox 30x18 | TAPE HEADS | (365,90,30,18) | always |
| TapeHead3Level | as above | Head 3 Level | mode-specific | CSlider 75x18, no readout | TAPE HEADS | (400,90,75,18) | always |
| TapeHead3Pan | as above | Head 3 Pan | mode-specific | CSlider 75x18, no readout | TAPE HEADS | (485,90,75,18) | always |

Other views: panel title `TAPE DELAY` CTextLabel (10,5,200,22); fieldsets TIME & MIX (10,30,280,122), CHARACTER (10,190,280,122), TAPE HEADS (300,30,270,122), SPLICE (580,30,270,122). Gain (footer ArcKnob) is outside this template. All 19 Tape parameters (400-408, 410-418) have a control; none may be dropped.

## Issues

### High

1. **CSlider color defect on all 18 sliders** (every `CSlider`, e.g. TapeFeedback slider (20,58,120,20)). The uidesc sets `back-color`/`value-color`/`frame-color`; VSTGUI reads `draw-back-color`/`draw-value-color`/`draw-frame-color`. Screenshot confirms black track with white fill, ignoring `slider-track` (#ccc) / `accent` (#3a80c9) / `border`. On a light panel a black bar reads as "empty or disabled", and the fill direction is not obvious (white = value, black = remainder).
2. **The delay time control is mislabelled and misplaced.** `TapeMotorSpeed` (the 20-2000 ms delay time) is labelled "Spd" and sits in CHARACTER at (20,218,80,20), while TIME & MIX at (10,30,280,122) holds only Feedback and Mix. Every other mode puts a Delay Time control first in its TIME group (Digital, BBD use a 45x45 ArcKnob + sync toggle at fieldset (10,10)). A user looking for time on Tape finds "Spd", which reads as tape transport speed. Its readout shows "499.2 ms" in a 40x16 display at (60,200,40,16), which is too narrow for the text and visually collides with the "Spd" label (screenshot: label and value touch). There is no sync mode for Tape (no param), so this must stay a free-time control; use the canonical label "Delay Time".
3. **Tape Heads Level and Pan sliders have no value readout and no per-row label** (9 sliders at x=400 and x=485, rows y=40/65/90). The user cannot read dB (-96..+6) or L/R pan; they only get a black bar. The "Level" and "Pan" column headers sit BELOW the rows at (400,115,75,16) and (485,115,75,16), which is the reverse of the usual header-above convention, and nothing else in the plugin does this. Pan is bipolar but drawn as a unipolar bar (center is not indicated).
4. **Heavy inconsistency of widget size and layout for the same kind of parameter (0-100% sliders):** widths 120, 130, 80, 180 and 75 (heights 20 and 18); label/readout widths 40, 50, 60, 65, 130. Readout 130 wide at (710,40,130,16) vs 40 wide in CHARACTER. Same-role controls (Feedback and Mix) differ by 10 px (120 vs 130) and sit in a 280-wide group that wastes the leftover.

### Medium

5. **Abbreviations:** "Spd" (should be Delay Time), "Iner" (Inertia), "Sat" (Saturation), "Intens" (Splice Intensity), "Enable" with no subject (Splice Enable). Labels in the 40x16 slots are too narrow to hold the full words; relayout needed.
6. **Related controls split / wrong grouping.** Time (Spd) and Inertia (motor behaviour) are in CHARACTER, with Wear/Sat/Age (tone character). Feedback+Mix are the only occupants of "TIME & MIX". Groups mix concerns: CHARACTER has two motor controls (Spd, Iner) plus three tone controls. Suggested split: TIME (Delay Time, Inertia, Feedback), CHARACTER (Wear, Saturation, Age), HEADS, SPLICE, with Mix in the shared output position used by the other modes.
7. **Inconsistent checkbox handling.** Splice: label "Enable" at (590,40,60,16) ABOVE a 50x22 box at (590,58,50,22) (label-over-control pattern). Heads: label LEFT of a 30x18 box at (365,y,30,18) (label-beside pattern). Two different patterns and two box sizes in one panel. Both CCheckBox views set no color attributes, so they draw the VSTGUI default (red check, black frame in the screenshot) instead of the accent color. Other modes use ToggleButton/segment controls for on/off; Tape is the only mode with plain checkboxes.
8. **Misaligned rows.** CHARACTER row 2 (Sat, Age) has only two of three columns at (20,263) and (110,263); the third slot (200,263) is empty, so the 3-column grid looks unfinished. TAPE HEADS rows have a 25 px pitch with 18 px sliders, while the other fieldsets use a 45 px pitch with 20 px sliders; slider top edges across groups (y=58, y=40, y=58) do not align between TIME & MIX/SPLICE (58) and HEADS (40).
9. **Splice Intensity is always active** even when `TapeSpliceEnabled` is off (no visibility/dim controller); the slider looks the same in both states. Likewise Head 2/3 Level and Pan stay fully drawn when their head is disabled (screenshot: heads 2 and 3 are unchecked yet look identical to head 1).
10. **Panel title label offset.** `TAPE DELAY` CTextLabel is 200 wide at (10,5) but renders centered at about x=130 in the screenshot (text starts about x=87 px/2), not aligned with the fieldset edges at x=10 or the fieldset titles at x~22. It also duplicates the Mode selector tab ("Tape") directly above.

### Low

11. **Wasted space.** TIME & MIX (10,30,280,122) uses only y=40..78; about 70 px of its height is empty. SPLICE (580,30,270,122) uses y=40..80; about 70 px empty. Below y=312 the panel is empty down to 400 (88 px) and the area right of CHARACTER (x 300..850, y 152..400) is empty, about 550x250 px, so the panel reads as top-heavy while CHARACTER floats alone at row 2.
12. **Inconsistent group geometry.** Group widths 280/270/270/280 and gaps of 10 px horizontally but 38 px vertically (152 to 190). CHARACTER is at y=190 while the upper row ends at y=152. Heights are all 122 though content heights are 48, 53, 90, 48 px.
13. **Fonts.** Three fonts in use (label-font, value-font, section-font) - consistent, but the value readout sits above-right of each slider while label sits above-left, so slider + label + value take 3 separate views per parameter; ArcKnob (used for time on Digital/BBD) is not used for any Tape parameter, so Tape looks different from every mode that has a time knob.
14. **No tooltips** on the abbreviated controls (Spd, Iner, Sat) to explain tape-specific meaning.
15. Inventory `defects`: none reported for Tape (no overlaps, nothing out of bounds, no stacked pairs). The issues above are layout/consistency defects the geometry check cannot see.

## Notes for the redesign

- Keep all tags and param IDs. Delay Time stays `TapeMotorSpeed` (no sync control may be invented; there is no TapeTimeMode parameter).
- Heads: add a readout per Level/Pan, put the Level/Pan headers above the rows, consider dimming controls when the head is off (needs a visibility/dim controller; none exists today).
- Fix the slider attributes (`draw-back-color`, `draw-value-color`, `draw-frame-color`) in the shared stylesheet pass.
