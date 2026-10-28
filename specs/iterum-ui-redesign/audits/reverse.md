# Reverse mode audit

Template `ReversePanel` (860 x 400, `editor.uidesc` L478-L520). Screenshot: `shots/before/editor-reverse.png` (2x). Inventory: `inventory-before.json` (26 views, 14 bound views, 4 geometry defects, no stacked pairs).
Coordinates are template-absolute (x, y, w, h) unless stated "local".

## Controller wiring

- `ReverseTimeSyncController` (controller.cpp L826) is `DelayTimeSyncController(kReverseChunkSizeId 800, kReverseTimeModeId 808, kReverseNoteValueId 809)`. It is attached to the 55x68 container at (20,40). It retags the knob and the readout between ChunkSize (800) and NoteValue (809) when sync is on. The knob, the readout and the sync toggle must stay under that container (or the sub-controller must be re-attached).
- Param 809 (ReverseNoteValue) has no standalone control; it is reached only through the retagged knob.
- Nothing in `controller.cpp` or `visibility_controller.h` hides or shows any Reverse control. No conditional visibility.
- Tags: ReverseChunkSize 800, ReverseCrossfade 801, ReversePlaybackMode 802, ReverseFeedback 803, ReverseFilterEnabled 804, ReverseFilterCutoff 805, ReverseFilterType 806, ReverseDryWet 807 (C++ id is `kReverseMixId`; the uidesc tag name stays), ReverseTimeMode 808, ReverseNoteValue 809.

## Control table

| Tag (id) | Label shown | Canonical label | Role | Widget, size | Group | Rect | Visibility |
|---|---|---|---|---|---|---|---|
| ReverseChunkSize (800) | none (readout only, "499.5 ms") | Chunk Size | time | ArcKnob 45x45 + CParamDisplay 55x13 | CHUNK | knob (25,53,45,45); readout (20,40,55,13) | always; retagged to 809 when sync on |
| ReverseTimeMode (808) | "sync" | Sync | time | ToggleButton 49x10, font-size 7 | CHUNK | (23,95,49,10) | always |
| ReverseNoteValue (809) | none | Note Value | time | no own control (knob retag) | CHUNK | n/a | only while sync on |
| ReversePlaybackMode (802) | "Play" (label 200x16 at (20,87)) | Playback Mode | mode-specific | CSegmentButton 250x22 (Full Reverse / Alternating / Random) | CHUNK | (20,105,250,22) | always |
| ReverseCrossfade (801) | "Crossfade" | Crossfade | character | CSlider 250x20 + CParamDisplay 125x16 | CHUNK | slider (20,151,250,20); readout (145,133,125,16) | always |
| ReverseFeedback (803) | "Feedback" | Feedback | feedback | CSlider 120x20 + CParamDisplay 60x16 | TIME & MIX | slider (300,58,120,20); readout (360,40,60,16) | always |
| ReverseDryWet (807) | "Dry/Wet" | Mix | mix | CSlider 130x20 + CParamDisplay 65x16 | TIME & MIX | slider (430,58,130,20); readout (495,40,65,16) | always |
| ReverseFilterEnabled (804) | "Enable" | Filter Enable | filter | CCheckBox 40x20, empty title | FILTER | (640,40,40,20); label (590,40,50,16) | always |
| ReverseFilterType (806) | "Type" | Filter Type | filter | CSegmentButton 250x22 (uidesc names LP,HP,BP) | FILTER | (590,102,250,22); label (590,82,30,16) | always |
| ReverseFilterCutoff (805) | "Cut" | Filter Cutoff | filter | CSlider 250x20 + CParamDisplay 40x20 | FILTER | slider (590,152,250,20); readout (800,132,40,20); label (590,132,30,20) | always |

Other views: title "REVERSE DELAY" (10,5,200,22). Three FieldsetContainers: CHUNK (10,30,270,152), TIME & MIX (290,30,280,152), FILTER (580,30,270,152). TIME & MIX is declared first in the XML, so XML order is middle, left, right.

## Issues

### High

1. **Slider colour defect, all 4 CSliders**: Feedback (300,58,120,20), Dry/Wet (430,58,130,20), Crossfade (20,151,250,20), Filter Cutoff (590,152,250,20). The uidesc sets `back-color`, `value-color`, `frame-color`; CSlider reads `draw-back-color`, `draw-value-color`, `draw-frame-color`. The screenshot shows black tracks with a white fill (Feedback at 0% is a solid black bar). It clashes with the light theme and is the most visible defect on the panel.
2. **Sync toggle overlaps the knob** (inventory views 11/12): ArcKnob (25,53,45,45) ends at y=98, ToggleButton "sync" is at (23,95,49,10). In the screenshot the "sync" text sits on the knob's lower arc. The toggle is 10 px high with 7 pt text: unreadable and a tiny hit target.
3. **Time container overlaps its siblings** (inventory views 9/13 and 9/14): container (20,40,55,68) runs to y=108 and overlaps the "Play" label (20,87,200,16) and the playback segment button (20,105,250,22). The container can intercept mouse events at the top edge of the segment button, and "Play" is drawn next to the knob.
4. **Unbalanced layout and large empty areas.** The time group uses 55 px of the 270 px CHUNK fieldset; x 75-270, y 40-100 is empty. TIME & MIX holds two sliders in its top 50 px; about 280x100 px (y 78-182) is empty. Below the fieldsets, y 182-400 (860x218, 54% of the panel) is empty.

### Medium

5. **Inconsistent widget sizes and types for the same kind of parameter.** Feedback slider 120 wide, Dry/Wet 130, Crossfade 250, Cutoff 250. Time is an ArcKnob but all other continuous parameters are sliders. Siblings in one fieldset (Feedback, Dry/Wet) differ in width by 10 px. Other modes place Feedback and Mix as knobs or in other widths, so a user cannot find them in a predictable place (check the other screenshots when designing the shared layout).
6. **Label/readout alignment is inconsistent.**
   - "Feedback" label (300,40,60,16) and readout (360,40,60,16); "Dry/Wet" label (430,40,65,16) and readout (495,40,65,16); "Crossfade" label (20,133,125,16) and readout (145,133,125,16).
   - "Cut" label is 30x20 and its readout is 40x20 (every other label/readout is 16 high).
   - The chunk readout is 55x13, `label-font`, centred, above the knob with no caption; every other readout uses `value-font` right-aligned.
   - "Play" label is centred in a 200 px box at (20,87) over a 250 px control; every other label is left-aligned. In the screenshot it floats right of the segment's left edge.
7. **Rows do not share a baseline across fieldsets.** CHUNK local rows: knob y=10, Play label 57, segment 75, Crossfade label 103, slider 121. FILTER local rows: Enable 10, Type label 52, segment 72, Cut label 102, slider 122. The two segmented controls are at abs y=105 and y=102, the two label rows at 133 and 132, the sliders at 151 and 152: 1-3 px off, visible in the screenshot as stepped rows.
8. **Abbreviations and non-canonical labels.** "Dry/Wet" should be "Mix" (plugins/iterum/CLAUDE.md canonical table; id is already `kReverseMixId`). "Cut" should be "Cutoff". "Enable" should read "Filter Enable" or be the group header. "Play" should be "Playback Mode". "sync" is lowercase, 7 pt, unlike every other label. The Chunk knob has no caption. The uidesc filter-type `segment-names` ("LP,HP,BP") differ from the text drawn in the screenshot ("LowPass / HighPass / BandPass"), so the uidesc names are not what users see; verify which source wins before relabelling.
9. **FILTER group is loose.** Enable is a 40x20 plain CCheckBox with a separate "Enable" label (590,40,50,16); the black-outlined box does not match the shared ToggleButton style. Rows y 60-82 are empty so Enable looks detached from Type and Cutoff. The type, cutoff controls show no disabled state when the filter is off.
10. **Groups do not describe their contents.** "TIME & MIX" holds Feedback and Dry/Wet (no time control; time sits in CHUNK). Playback Mode and Crossfade sit in CHUNK with the time knob; they are playback behaviour rather than chunk time.
11. **XML (focus/z) order differs from visual order**: TIME & MIX (L482) is declared before CHUNK (L492).

### Low

12. **Title position.** "REVERSE DELAY" (10,5,200,22) renders centred in its box (about 36 px from the CHUNK left edge), not at the fieldset left edge; 3 px gap to the fieldsets below (title ends y=27, fieldsets start y=30).
13. **Fieldset widths differ.** CHUNK 270, TIME & MIX 280, FILTER 270, so the middle column is 10 px wider than the others.
14. **ToggleButton font-size 7** versus `label-font` / `value-font` elsewhere.
15. **Inert attribute.** All CParamDisplay views set `value-precision="0"`, but the chunk readout shows "499.5 ms", so the param formatter wins and the attribute is misleading.

## Cross-mode notes

- The empty lower half and the top-strip layout are shared with other modes; a global panel grid would fix it for Reverse too.
- The time group (container 55x68 with knob, readout, sync toggle and sub-controller) is the same pattern in every mode, so its overlap should be fixed once in the shared layout.
- The FILTER group (Enable, Type, Cutoff) should use the same layout as other modes that have a filter.

## Redesign constraints from this audit

- Keep tags 800-809 and the `ReverseTimeSyncController` container pattern (knob + readout + sync toggle inside it).
- After the redesign `uidesc-inventory --check` must show none of the four Reverse overlaps (9/13, 9/14, 11/12 and the container overlaps) and no lost bindings; the 4 sliders need the `draw-*-color` attributes.
