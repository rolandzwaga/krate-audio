# Spectral mode panel audit

Scope: template `SpectralPanel` (860 x 400, hosted at 20,60 in the Editor), screenshot `shots/before/editor-spectral.png` (2x), `inventory-before.json` (34 views, 19 bound controls).
Coordinates below are template-absolute (fieldset origin + child origin) unless marked "local".

## Control table

| Tag (id) | Label shown today | Canonical label | Role | Widget, size | Group | Visibility |
|---|---|---|---|---|---|---|
| SpectralBaseDelay (201) | none (readout "250" above knob, no unit) | Delay Time | time | ArcKnob 45x45 at 25,53, plus CParamDisplay 55x13 at 20,40 | TIME & MIX (10,30 280x157) | always; sub-controller `SpectralTimeSyncController` re-tags this knob and display to SpectralNoteValue (212) while sync is on |
| SpectralTimeMode (211) | "sync" (7pt) | Sync (Time Mode) | mode | ToggleButton 49x10 at 23,95 | TIME & MIX | always |
| SpectralNoteValue (212) | none (reached only via the time knob when sync is on; no own control) | Note Value | time | shares the knob above | TIME & MIX | only while SpectralTimeMode = 1 |
| SpectralFeedback (204) | "Feedback" (0-120 %, shown "0") | Feedback | feedback | CSlider 195x20 at 85,58 | TIME & MIX | always |
| SpectralDryWet (208) | "Dry/Wet" | Mix | mix | CSlider 260x20 at 20,130 | TIME & MIX | always |
| SpectralFFTSize (200) | "FFT Size" (centred) | FFT Size | mode-specific | CSegmentButton 245x22 at 310,58 (512/1024/2048/4096) | SPECTRAL ANALYSIS (300,30 270x157) | always |
| SpectralSpreadDirection (203) | "Direction" (centred) | Spread Direction | mode-specific | CSegmentButton 245x22 at 310,103 (Low->High / High->Low / Center Out) | SPECTRAL ANALYSIS | always |
| SpectralSpread (202) | "Spread" (0-2000 ms, shown "0", no unit) | Spread | time | CSlider 115x20 at 310,148 | SPECTRAL ANALYSIS | always |
| SpectralSpreadCurve (209) | "Curve" | Spread Curve | mode-specific | CSegmentButton 120x22 at 445,148 (uidesc says Linear,Log; the StringList shows "Logarithmic") | SPECTRAL ANALYSIS | always |
| SpectralFeedbackTilt (205) | "FB Tilt" (-100..+100 %) | Feedback Tilt | feedback / filter | CSlider 120x20 at 590,58 | SPECTRAL CHARACTER (580,30 270x157) | always |
| SpectralDiffusion (207) | "Diffusion" | Diffusion | character | CSlider 120x20 at 720,58 | SPECTRAL CHARACTER | always |
| SpectralFreeze (206) | "Freeze" (label) + untitled checkbox | Freeze | mode-specific | CCheckBox 50x22 at 590,103 | SPECTRAL CHARACTER | always |
| SpectralStereoWidth (210) | "Width" | Stereo Width | width | CSlider 159x20 at 680,103 | SPECTRAL CHARACTER | always |

Controller wiring for this scope: only `SpectralTimeSyncController` (controller.cpp:816, `DelayTimeSyncController(kSpectralBaseDelayId, kSpectralTimeModeId, kSpectralNoteValueId)`). `visibility_controller.h` has no Spectral references, so no container is load-bearing for visibility. The sync controller needs the knob, its readout and the toggle inside the 55x68 sub-controller container at local 10,10 (template-absolute 20,40). It must stay a single group.
All 13 params have controls (212 via the sync knob). Unit suffixes are not shown by the readouts (formatSpectralParam returns bare numbers).

## Issues

### High
1. **CSlider colour defect, all 6 sliders** (Feedback 85,58; Dry/Wet 20,130; Spread 310,148; FB Tilt 590,58; Diffusion 720,58; Stereo Width 680,103). They use `back-color` / `value-color` / `frame-color`, which CSlider ignores (it reads `draw-back-color`, `draw-value-color`, `draw-frame-color`). Screenshot: black track with white fill. Bipolar FB Tilt and default-50 Dry/Wet read as a white half-bar with no accent colour. Fix is attribute renaming on all 6 (XSLT).
2. **Panel uses 187 of 400 px (53 % empty).** Everything sits in one row of three fieldsets ending at y=187; y=187..400 is empty (visible in the screenshot as a large blank grey area). The three fieldsets are also crowded inside (row gaps of 5 px) while the space below is unused. The row layout should be re-spread (or the panel/editor shrunk) with consistent row heights.
3. **Unrelated or split grouping.**
   - SPECTRAL ANALYSIS (300,30) holds only one analysis control (FFT Size). Direction, Spread and Curve are one spread unit and have nothing to do with analysis.
   - Feedback (85,58) is in TIME & MIX while Feedback Tilt (590,58) is in SPECTRAL CHARACTER. Related controls are split across two fieldsets.
   - Freeze and Stereo Width are mixed with Diffusion/Tilt in CHARACTER; Freeze is a performance toggle with no neighbour related to it.
4. **Mixed widget choice for the same kind of parameter.** Time uses an ArcKnob; Feedback, Mix, Spread, Tilt, Diffusion and Width, all 0..1 amounts, use horizontal sliders. Freeze is a bare CCheckBox with a separate label. Direction, FFT Size and Curve are segment buttons. Sync is a 49x10 ToggleButton in 7pt. Other modes follow the same mix, so the language is not consistent across modes either.

### Medium
5. **Time knob group is overlapped and unlabeled.** Knob (25,53,45x45) overlaps the sync toggle (23,95,49x10) by 3 px (inventory defect, views 4/5). The knob has no label ("Delay Time"); the number above it ("250", 55x13 at 20,40) has no unit, and the sync toggle is 10 px high with 7pt text, below a usable hit size. Readout is `label-font`, centred; all other readouts are `value-font` right-aligned.
6. **Abbreviations and non-canonical labels.** "Dry/Wet" (130x16 at 20,112) should be "Mix". "FB Tilt" (60x16 at 590,40) should be "Feedback Tilt". "Width" (75x16 at 680,85) should be "Stereo Width". "Direction" (245x16 at 310,85) should be "Spread Direction"; "Curve" (120x16 at 445,130) should be "Spread Curve"; "sync" should read "Sync".
7. **Curve segment text overflow.** The Curve CSegmentButton (445,148, 120x22) is authored with "Linear,Log" but the controller's StringList supplies "Logarithmic"; in the screenshot "Logarithmic" fills its cell and touches the right edge. The 120 px width does not fit two long segment names.
8. **Inconsistent slider widths and label/readout boxes.** Sliders are 195, 260, 115, 120, 120 and 159 px wide; label+readout boxes are 65+65, 130+130, 55+60, 60+60, 60+60 and 75+80. Readout boxes in the same fieldset do not share a right edge with their slider (e.g. Spread readout right edge 435 vs slider right edge 425; Width readout right edge 835 vs slider 839). Spread (115) and Curve (120) sit side by side with different widths and the 10 px gap does not match the other fieldsets' 10 px / 15 px gaps.
9. **Misaligned rows between fieldsets.** TIME & MIX rows at y=40/58 (Feedback), 112/130 (Mix); ANALYSIS rows at y=40/58, 85/103, 130/148; CHARACTER rows at y=40/58, 85/103. No two fieldsets share a baseline except row 1; the Mix slider (y=130) lines up with nothing in the other fieldsets, and the knob group spans rows 1-2 of its own box.
10. **Value readouts have no units.** Time "250" (ms), Spread "0" (ms), Feedback "0" (%), Diffusion/Width "0" (%), Tilt "+0" (%): the readouts show bare numbers. Display units are produced by `formatSpectralParam` (spectral_params.h:307ff) or the uidesc; a format change needs a controller edit, not just a label.

### Low
11. **Section title offset.** "SPECTRAL DELAY" (CTextLabel 200x22 at 10,5, centred by default) sits only 3 px above the fieldsets (y=30) and is not left-aligned with the fieldset border (x=10): the centred text starts at about x=67 in the screenshot.
12. **Freeze checkbox.** 50x22 at 590,103, untitled; label 80x16 at 590,85. The checkbox draws as a small black-outline box with no on-state colour from the palette; the label wastes a row for a 1-bit control.
13. **Right margin asymmetry inside fieldsets.** Content spans x=10..255 (left margin 10, right margin 15) in ANALYSIS (310..555 of 300..570); Diffusion slider ends 840 vs fieldset right edge 850 (10), Width slider 839 (11).
14. **Empty cells.** CHARACTER row 2 right of Width, TIME & MIX right of the knob below Feedback (x=85..280, y=80..110), and ANALYSIS below the Spread/Curve row (y=170..187) are unused while other cells are tight.
15. **ASCII arrows.** Direction segment text "Low->High / High->Low" (entities `-&gt;` in the uidesc) is hand-written ASCII; a word or arrow glyph would read better.

## Inventory defects for this template
- overlap: ArcKnob SpectralBaseDelay 5,13,45,45 vs ToggleButton SpectralTimeMode 3,55,49,10 (local to the 55x68 sub-controller container); same pattern appears in every mode's time group, so it is a shared-layout defect.
- No out-of-bounds, lost-binding or stacked-pair defects for SpectralPanel.

## Notes for the redesign
- Keep the time sub-controller group intact (knob, readout, toggle in one container named for `SpectralTimeSyncController`).
- All tags and the 13 bound controls must stay; SpectralNoteValue stays reachable only through the sync-retagged knob.
- Sizes used here (sliders 6 widths, CSegmentButtons 245x22 and 120x22, labels 8 widths) should collapse to the shared per-mode template.
