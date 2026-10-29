# BBD panel audit

Scope: template `BBDPanel` (860 x 400, `plugins/iterum/resources/editor.uidesc` L363-L399).
Screenshot: `specs/iterum-ui-redesign/shots/before/editor-bbd.png` (2x; divide pixel positions by 2 for uidesc units).
Inventory: `inventory-before.json` > templates.BBDPanel (25 views, 14 bound, 1 defect, no stacked pairs).

Controller: only `BBDTimeSyncController` (controller.cpp L820) wires this panel (DelayTimeSyncController on kBBDDelayTimeId / kBBDTimeModeId / kBBDNoteValueId). No VisibilityController or ContainerVisibilityController references any BBD tag, so nothing is conditionally hidden. `BBDNoteValue` (id 508) has no view of its own; the sync sub-controller swaps it in for the time parameter.

Parameter IDs (plugin_ids.h L145-155): 500 DelayTime, 501 Feedback, 502 ModDepth, 503 ModRate, 504 Age, 505 Era, 506 Mix, 507 TimeMode, 508 NoteValue. The uidesc control-tags are named `BBDModulationDepth` / `BBDModulationRate` (tag numbers 502 / 503); tag names stay frozen, only labels may change.

## Control table

Rects are template-absolute (x, y, w, h) from the inventory. Every control is unconditionally visible.

| Control-tag (id) | Label shown | Canonical label | Role | Widget, size | Group | Rect (abs) |
|---|---|---|---|---|---|---|
| BBDDelayTime (500) | none (value readout only, "300.3 ms") | Time | time | ArcKnob 45x45 + CParamDisplay 55x13 above | TIME & MIX | knob 25,53,45,45; readout 20,40,55,13 |
| BBDTimeMode (507) | "sync" | Sync | time | ToggleButton 49x10, font-size 7 | TIME & MIX | 23,95,49,10 |
| BBDNoteValue (508) | none (no own view) | Note | time | none, driven by the sync sub-controller | TIME & MIX | n/a |
| BBDFeedback (501) | "Feedback" | Feedback | feedback | CSlider 195x20 + label 65x16 + readout 65x16 | TIME & MIX | slider 85,58,195,20 |
| BBDMix (506) | "Mix" | Mix | mix | CSlider 260x20 + label 130x16 + readout 130x16 | TIME & MIX | slider 20,130,260,20 |
| BBDAge (504) | "Age" | Age | character | CSlider 122x20 + label 50x16 + readout 52x16 | CHARACTER | slider 310,60,122,20 |
| BBDEra (505) | "Era" | Era (or Chip) | mode-specific | CSegmentButton 245x22, 4 segments (MN3005, MN3007, MN3205, SAD1024) | CHARACTER | 310,110,245,22 |
| BBDModulationDepth (502) | "Depth" | Mod Depth | modulation | CSlider 120x20 + label 60x16 + readout 60x16 | MODULATION | slider 590,58,120,20 |
| BBDModulationRate (503) | "Rate" | Mod Rate | modulation | CSlider 120x20 + label 60x16 + readout 60x16 | MODULATION | slider 720,58,120,20 |

Non-parameter view: section title "BBD DELAY" (CTextLabel 200x22 at 10,5, section-font, accent).
BBD has no output level or width parameter, so nothing is missing; Gain lives in the shared footer.

## Issues

### High

1. **Every CSlider draws VSTGUI defaults (black track, white fill).** Views: Feedback 85,58,195,20; Mix 20,130,260,20; Age 310,60,122,20; Depth 590,58,120,20; Rate 720,58,120,20. They set `back-color` / `value-color` / `frame-color`, which CSlider does not read; it reads `draw-back-color` / `draw-value-color` / `draw-frame-color`. In the screenshot Depth at 0% is a solid black bar and Rate at 0.5 Hz is black with a sliver of white, so the value is hard to read and ignores the accent/border scheme. Fix with XSLT (the defect spans all sliders in the uidesc).
2. **More than half of the panel is empty.** All content ends at y = 157 (fieldsets are 127 high from y=30) in a 400 high panel, so y 157-400 is blank, with a further empty band below the panel before the footer (screenshot y 920-1120 px). The mode looks unfinished beside Digital, whose fieldsets reach y 352. Use the space with larger controls or shrink the panel.
3. **Knob group collides with the fieldset border and with itself.** Inventory defect (overlap): ArcKnob BBDDelayTime (25,53,45,45, ends y=98) overlaps ToggleButton BBDTimeMode (23,95,49,10). The time readout (20,40,55,13) sits on the fieldset's top edge, so "300.3 ms" touches the "TIME & MIX" title line in the screenshot. The sync toggle is 10 px high at 7 pt, a poor hit target.

### Medium

4. **Time has no caption and a tiny knob.** The most important parameter is labelled only by a 13 px readout, while Feedback and Mix get label rows. The 45 px knob is the smallest control in the group beside a 260 px Mix slider. Widget choice differs per row for the same kind of continuous parameter: Time = knob, Feedback/Mix/Age/Mod = slider.
5. **Slider widths are inconsistent.** Feedback 195, Mix 260, Age 122, Depth 120, Rate 120. Age fills only half of its 245 px usable fieldset width (310-432), leaving the right half empty in the screenshot, while Era below it is 245 wide. Digital's Age is a full-width 250 px slider, so BBD is the outlier.
6. **Label rows do not share one geometry.** Label rects are 65, 130, 50 and 60 wide and centre-aligned (the CTextLabel default), so "Feedback", "Mix", "Age", "Depth" and "Rate" float inside their rects while their readouts are right-aligned; the label-to-readout pairs look unrelated to the slider's left edge. Readout widths are 65, 130, 52, 60 and 60. The time readout uses `label-font`, the others `value-font`.
7. **MODULATION fieldset is mostly empty.** MODULATION (580,30,270x127) holds two side-by-side 120 px sliders in its first ~50 px; the lower 75 px are blank (screenshot x 1200-1740, y 300-430 px). Depth and Rate sit side by side while Feedback and Mix are stacked.
8. **Grouping.** TIME & MIX mixes time controls with Mix and Feedback (the BBD regeneration), while CHARACTER holds only Age and Era. The fieldsets are 280/270/270 wide with 10 px gaps, so equal-weight groups are not on an even grid.
9. **Section title is centred.** "BBD DELAY" (10,5,200x22) is centre-aligned, so its text starts near x=90 (screenshot x 180 px) while fieldset titles and rows start at x=10-20. It floats over the middle of the first fieldset.

### Low

10. **Abbreviated or non-canonical labels.** "Depth" and "Rate" should read "Mod Depth" and "Mod Rate" (plugins/iterum/CLAUDE.md canonical table); "sync" is lower case and does not say what it toggles; "Era" with chip part numbers as segment names gives no hint that it selects the BBD chip (consider "Chip").
11. **Sizes and fonts vary within one panel.** Sync toggle 7 pt where all other text is label-font/value-font; readout heights 13 (Time) vs 16 (others); Era segment 22 high vs sliders 20.
12. **CHARACTER spacing.** Age at y=60 and Era at y=110-132 leave ~25 px unused below the Era control (fieldset ends y=157) while the group's right half beside Age is empty.
13. **Feedback range cue.** Feedback runs 0-120% but the display gives no visual cue past 100% (self-oscillation).

## Cross-mode notes

The Time & Mix block (knob group at 10,10; Feedback slider 195 wide at 75,28; Mix slider 260 wide at 10,100) is copy-pasted per mode (Digital, PingPong, others), with per-mode tags (BBD*, Digital*), so a single shared layout is a per-mode template stamped ten times, never one control. BBD is the simplest panel (nine params), so it fits one row of three groups at a much smaller height than 400.
