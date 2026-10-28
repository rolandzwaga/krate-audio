# Open items after the design stage (2026-10-28)

The reconcile stage resolves every item below in design-system.md and the panel specs.

Reconciled 2026-10-28: every ruling is applied and every item is resolved; the "Resolved:" line under each one names
the design-system.md sections and the panel specs that now carry it. Nothing is left for the user.

## User rulings (2026-10-28)

1. Dark theme (section 5.1 as designed; drop kIterumLightButtonColors).
   Resolved: design-system.md 10 (ruling 1), 5.1 unchanged, 9.3 "Drop the light button triple".
2. Fixed window 940 x 608, min = max.
   Resolved: design-system.md 10 (ruling 2), 2.1 unchanged.
3. Knobs for Time, Feedback, Mix (and Gain); sliders for every other continuous parameter.
   Resolved: design-system.md 10 (ruling 3), 4.1, 4.3, principle 2.
4. Tape: the kTapeMotorSpeedId control is labelled "Time".
   Resolved: design-system.md 10 (ruling 4), 6.2, 7.4; panels/tape.md 1.1 (view 1b) and section 4 (the "Motor Speed"
   alternative is gone).
5. Spectral: STEREO is the standard pinned cell (604,288,284,180), not a tall column-3 box; Width sits where it sits in every other mode.
   Resolved: design-system.md 10 (ruling 5), 2.4, 3.2 #5, 3.3, 6.2 "Width", 7.2 (header, rows 209 and 210, closing
   note), 9.1, 11 first bullet; panels/spectral.md header, 2.4, section 5 (no longer "supersedes" the design).
6. MultiTap: Snap label, Snap menu and Reset on the CUSTOM PATTERN fieldset title line; the tap editor keeps 852 x 140.
   Resolved: design-system.md 10 (ruling 6); 4.10, 7.9 and 8.2 unchanged; panels/multitap.md 2.7, 2.8 and 6.
7. Bipolar sliders get a 1 pt centre tick (a CView behind each bipolar slider).
   Resolved: design-system.md 10 (ruling 7: drawn after the slider and mouse-disabled, because CSlider paints its
   track opaque), principle 2, 4.4 (tick row), 5.6, 7.1/7.2/7.3/7.4/7.6 notes, 7.11, 8.3, 9.2 (`bipolar-cell`), 9.4
   step 1, 9.5 third row (inventory exemption, to do test-first), 11 "centre tick"; panels/granular.md (27a),
   panels/spectral.md (5c'), panels/shimmer.md (4c', 4f'), panels/digital.md (16a), panels/tape.md (three ticks).

## Items to resolve (unresolved challenges and consistency findings, verbatim)

### 1

[challenge, unresolved after 2 revisions] Principle 4, 6.4 Readouts, 9.3 C++ changes (Granular, Spectral, Shimmer, Gain): The document says every readout shows its unit and that getParamStringByValue 'already formats nine modes with units' (6.4), and that the controller 'already formats dB' for Gain (2.2). The code does not do this. formatGranularParam (granular_params.h:416-459), formatSpectralParam (spectral_params.h:321-349) and formatShimmerParam (shimmer_params.h:293-334) return bare numbers such as "500", "+12.0", "80" and "12.5k". The Gain case (controller.cpp:410) prints "%.1f" with no "dB". VST3Editor adds no unit to a CParamDisplay; it shows getParamStringByValue verbatim (vst3editor.cpp ~252/300). So the new core-strip readouts would show Time "500" and Feedback "80" in Granular, Spectral and Shimmer, but "500.0 ms" and "80%" in BBD, Digital, PingPong, Reverse and Tape. That breaks the core promise that switching modes shows the same control the same way. Spectral Spread ("1000", really ms) and Granular Density ("12.5", really Hz) give no clue what they measure. Section 9.3 has no row to fix this. (suggested: Add a row to 9.3: complete formatGranularParam, formatSpectralParam and formatShimmerParam with units, using the same format as the other packs ("%.1f ms" or "%.0f ms" for time, "%.0f%%", "%+.1f st", "%+.0f ct", "%.1f Hz", "%.2f kHz" / "%.0f Hz"). Add " dB" to the Gain case; getParamValueByString's sscanf("%lf") still parses "-6.0 dB". Write a [granular]/[spectral]/[shimmer] Catch2 case first that asserts the strings at min, default and max. Correct 6.4 to say six modes are formatted with units, three must be completed and Freeze is handled separately.)

Resolved: design-system.md 6.4 (one format standard; the six formatted modes named; "nine modes" withdrawn), 2.2
(Gain readout), principle 4, 9.3 rows "Complete the Granular / Spectral / Shimmer formatter", "Gain in dB", "Core time
readouts switch to seconds", "Complete the Freeze formatter", 11 "Readouts"; panels/granular.md section 4,
panels/spectral.md section 5 note 1, panels/shimmer.md section 6.

### 2

[challenge, unresolved after 2 revisions] 2.4 STEREO pin, 3.3 'width', 7.2 Spectral: Width is supposed to be in one place, but it is not. In Granular, Digital and PingPong, STEREO is the standard section at (604,288), so the Width slider sits around panel y 312. In Spectral, STEREO is the tall column-3 section (604,96,284,372) and Width goes in its first row (24), around panel y 120. That is the top of column 3, the spot where MODULATION and FILTER sit in other modes. The document calls this 'pinned to column 3, bottom-most', but in Spectral the control lands at the top. A musician who learned where Width is in Digital will not find it there in Spectral. (suggested: Make Spectral's STEREO a standard section at (604,288,284,180) holding Width at row 24, and leave (604,96) as plain panel background, the same way the pin leaves cells empty in Granular and Digital. Alternatively, keep the tall section and put Width in its last row (284) so it lines up with the others. Update 2.4, 3.2, 3.3 and 7.2 to match.)

Resolved: by ruling 5; design-system.md 2.4, 3.2 #5, 3.3, 6.2, 7.2, 9.1, 10, 11; panels/spectral.md 2.4 and section 5.

### 3

[challenge, unresolved after 2 revisions] 6.2 'Freeze' row, 3.3 'freeze (hold)', 7.1, 7.2: The hold toggle is a live performance control shared by Granular and Spectral, but it lands in a different place in each. In Granular it is in CHARACTER at (12,288), row 128, around panel (24,436): column 1, bottom of the panel. In Spectral it is in CHARACTER at (308,96), row 180, around panel (320,296): column 2, middle height. The rule 'last row of CHARACTER, half L' is relative to a section, and that section itself sits in a different cell in each mode. So it does not give the 'same place' that 6.2 promises for shared meanings. (suggested: Give Freeze one place in the panel in both modes. The simplest is the core strip: put the Freeze ToggleButton in core cell 3 (Mix) at cell (200,32,76,22), the same slot and toggle-cell attributes as Sync in core cell 1, so both modes put hold beside Mix where it is always visible. If that is not wanted, give Freeze the same panel position in both modes, for example a fixed row and column in the column-1 section, and say so in 6.2, 3.3, 7.1 and 7.2.)

Resolved: the core-strip option; design-system.md principle 1, 3.1, 3.3, 4.1 (toggle slot), 4.8, 6.2, 7.1, 7.2, 11
"Freeze in the core strip"; panels/granular.md (view 14a, CHARACTER row 128 empty), panels/spectral.md (view 3d,
CHARACTER rows 180-284 empty).

### 4

[challenge, unresolved after 2 revisions] 9.4 Verification / 9.2 step 3 (per-stylesheet --check gate): The gate cannot pass as written. The document requires `uidesc-inventory.js --check --baseline` to print `UIDESC CLEAN` after 00-global and after each panel stylesheet, and requires each panel stylesheet to be dry-run on a private copy and checked the same way. 00-global replaces only the Editor template. The 17 baseline geometry defects stay in the panels that have not been rewritten yet: Granular 6, Spectral/Shimmer/BBD/Digital/PingPong 1 each, Reverse 3, MultiTap 3 (see `inventory-before.json` `defects`). `--check` reports defects for the whole file and exits 1 on any of them (tools/uidesc-inventory.js lines 138-159), and it has no per-template scope. So every check before the tenth panel stylesheet is red, and the fixer loop would end up editing panels that their own stylesheets are meant to replace. (suggested: Add a `--template <name>` option (test-first, alongside the 9.5 changes) that limits overlap and out-of-bounds to the named templates. lost-binding, lost-control and changed-tag stay file-wide. The per-panel dry-run then checks `--template <Mode>Panel` plus the file-wide binding checks, and after 00-global it checks `--template Editor`. Require a whole-file `UIDESC CLEAN` only after all ten panel stylesheets are applied, and state that order in 9.4.)

Resolved: no `--template` option; the gate reads the tool's per-template defect lines for the stylesheet's own
template and tags, and `UIDESC CLEAN` is required once at the end: design-system.md 9.2 step 3, 9.4 steps 1-2, 8.3,
11 "The inventory gate can never pass" and "Per-stylesheet gate scope".

### 5

[challenge, unresolved after 2 revisions] 4.9 Time with sync / 8.1 / 9.3 'Nothing else' (delay_time_sync_controller.*): The document says the readout shows "1/8" while synced and that `delay_time_sync_controller.*` needs no change. That is false whenever a panel is built while its Sync is already on, which happens on every editor open and every mode switch. `DelayTimeSyncController::verifyView` picks the initial tag through `control->getFrame()->getEditor()` (delay_time_sync_controller.cpp:61-75). verifyView runs while the view tree is still being built and not yet attached. `CView::getFrame()` is only set in `attached()` (cview.cpp:452), so it is null there and the swap to the NoteValue tag never happens. The knob and the new 15 pt readout then bind to the free-time parameter: the readout shows "500 ms" and turning the knob edits a hidden parameter until Sync is clicked twice. The redesign makes this readout the most prominent text in the panel. (suggested: Add a 9.3 row. In `verifyView`, read the sync state through the parent controller passed to the constructor (`dynamic_cast<VSTGUI::VST3Editor*>(controller)->getController()->getParamNormalized(syncTag_)`), not through `getFrame()`. Test first: open the real editor with GranularTimeMode = 1 and assert that the ArcKnob and the CParamDisplay in the time cell carry tag 114.)

Resolved: verified against delay_time_sync_controller.cpp 49-90, uidescription.cpp 657-745 (verifyView at 740 runs
before the parent's addView at 711), cview.cpp 452, delegationcontroller.h 30, controller.cpp 802-827;
design-system.md 4.9, 8.2 (Time sync row), 9.3 "Read the Sync state at panel build" (with the `[iterum][ui]` test),
9.4 step 3, 11 "Sync state is read before the view is attached"; panels/digital.md and panels/pingpong.md note that
their TimeMode defaults to Synced.

### 6

[challenge, unresolved after 2 revisions] Principle 4 / 4.3 readout widths / 6.4 Readouts / 2.2 Gain readout: 6.4 says `getParamStringByValue` "already formats nine modes with units" and that only Freeze needs completing. That is wrong. `formatGranularParam`, `formatSpectralParam` and `formatShimmerParam` return bare numbers ("%.0f", "%+.1f", "%.1f", "%.1fk": granular_params.h 413-460, spectral_params.h 317-349, shimmer_params.h 290-334). Gain is "%.1f" with no dB (controller.cpp ~410). Granular, Spectral and Shimmer readouts would show "500", "50", "+12", while Digital, BBD, Tape, PingPong, Reverse and MultiTap show "500 ms", "50%". That is the cross-mode inconsistency the redesign exists to remove. The 64 pt readout width was also sized for strings with units. A plain fix collides with metadata: some of these parameters register a units string (Granular Feedback registers "%", granular_params.h:315), so hosts that append units would show "50% %". (suggested: Add a 9.3 row that completes the Granular, Spectral and Shimmer formatters and the Gain case with units, matching the other modes ("%.0f ms", "%.0f%%", "%+.1f st", "%+.0f ct", "%.1f dB", Hz/kHz). Test first with min/default/max strings per id. For the parameters whose ParameterInfo carries a units string, put a ruling in section 10: either clear those units (metadata, not range/default/state) or keep them and accept doubled units in hosts. Without the row, 6.4 must stop claiming units for nine modes and principle 4 must be weakened.)

Resolved: as item 1, plus the units-metadata decision: the strings stay (the six already-formatted modes register the
same units strings and print units, `digital_params.h:163-274`, `tape_params.h:176-302`); design-system.md 9.3
(preamble and "No change to ... units strings"), 11 "Units metadata stays", 4.3 (readout widths against the 6.4
strings).

### 7

[challenge, unresolved after 2 revisions] 9.4 Verification ('the editor lifecycle, control-visibility and multitap-visibility cases exercise the real view tree'): No gate instantiates 9 of the 10 new panels. `exerciseEditorLifecycle` and `openEditorAndCountSharedViews` open the Editor at the default mode only (Granular). `control_visibility_test.cpp` and `multitap_visibility_test.cpp` test threshold arithmetic and IDs without loading the uidesc; multitap_visibility_test only mentions editor.uidesc in a comment. The only test that switches modes is the hidden `[.][screenshots]` case, which asserts nothing about views. None of the load-bearing structural rules is checked by a test: anchor control as a direct child of its fieldset (1020/1030/1040/1050/911/920), one Freeze variant visible at a time, Copy and Reset never visible together, every interactive tag present, the TimeSync container holding knob + readout + toggle. A wrong nesting in any panel stylesheet would pass build, tests and the inventory check. (suggested: Add a non-hidden `[iterum][ui]` Catch2 case modelled on editor_screenshot_test.cpp (setAnimationTime(0), switch modes, `triggerDeferedUpdates`, no PNG). For each mode it asserts that every interactive tag from section 7 has a visible-capable CControl. Freeze: for pattern values 0, 1/3, 2/3 and 1, exactly one of the four variant containers is visible. MultiTap: for patterns 2, 15 and 19, NOTE VALUE, CUSTOM PATTERN and Copy show the 8.2 states, and Copy and Reset are never both visible. Digital: Age hidden at Era 0, shown at 0.5. Toggling Sync retags the knob and readout to the NoteValue tag. List it in 9.4 as a gate.)

Resolved: design-system.md 9.4 step 3 (REQUIRED test-first `plugins/iterum/tests/unit/vst/editor_panels_test.cpp`,
`[iterum][ui]`: per-mode tag/class table, FieldsetContainer and plain-CViewContainer counts, conditional groups, Sync
retag), 9.4 step 4, 11 "No gate instantiates nine of the ten panels"; every panel spec states its container counts
for that test.

### 8

[challenge, unresolved after 2 revisions] 9.3 'Restyle Copy / Reset pattern buttons': The specified calls `setGradient(CGradient::create(...))` and `setGradientHighlighted(CGradient::create(...))` leak. `CGradient::create` returns a raw pointer with refcount 1 (cgradient.h:22), and `CTextButton::setGradient` stores it in a SharedPointer, which adds a reference. VSTGUI's own constructor wraps it in `owned(...)` (cbuttons.cpp:739-740). Copied to the letter, every MultiTap panel instantiation leaks two gradients. The leak tests open only the default mode, so they never catch it. (suggested: Specify `setGradient(VSTGUI::owned(VSTGUI::CGradient::create(0, 1, c, c)))` for both gradients. Extend the `[multitap]` test to check the gradient's reference count after construction, or run it under the ASan build.)

Resolved: verified against cgradient.h 19-22, cbuttons.h 267-268, cbuttons.cpp 736-743 and 807-818, vstguibase.h 273;
design-system.md 9.3 "Restyle Copy / Reset pattern buttons" (both calls wrapped in `VSTGUI::owned`, test asserts
`getNbReference() == 1`), 11 "Copy / Reset buttons unreadable".

### 9

[consistency, unresolved] spectral: The spec places STEREO in the standard pinned cell (604,288,284,180), leaves (604,96) empty, and makes Spread Curve '128 full'. Design-system 2.4, 3.2 #5, 3.3, 6.2 (Width row) and 7.2 say STEREO is the column-3 tall section (604,96,284,372) and Spread Curve is '128 half L'. The spec's section 5 says it 'supersedes' the design text, but design-system.md line 4 says the design wins, so an XSLT author following the rules would build the tall STEREO and the half-cell Curve. The two documents give opposite instructions. (suggested: Recommended: the design owner adopts the spec's version, because it is the more consistent one. It puts the Width slider at panel y 334 in Granular, Spectral, Digital and PingPong, and makes 2-entry segments full cells, as Freeze's Slice Mode and Envelope Shape already are. Edit design 2.4 (pin rule and the 'column-3 tall section in Spectral' wording), 3.2 #5, 3.3, 6.2 Width row, the 7.2 header ('2 tall sections + STEREO'), rows 209/210 and 9.1 (SpectralPanel line). Then move these items from the spec's 'supersedes' list into its Deviations list. If the design owner declines, revert the spec to 7.2 as written.)

Resolved: the design adopted the spec's version (ruling 5); design-system.md 2.4, 3.2 #5, 3.3, 4.5 (two-entry
segments alone on a row take the full cell), 6.2, 7.2, 9.1, 10, 11; panels/spectral.md section 5 (the "supersedes"
list is gone; deviations: none).

### 10

[consistency, unresolved] spectral: The formatter completion in section 5 (the 9.3 row) and in note 1 is wrong in one place and inconsistent in another. (a) kSpectralSpreadId is 0-2000 ms: spectral_params.h maps it at 79-83 and formats it in the same branch as BaseDelay at 317-321. The spec gives 202 '%.0f%%', and its test expects '50%', so the Spread readout would show a percentage for a time value. (b) Time (201) gets '%.0f ms', but every other core time readout is '%.1f ms': BBD 218, Reverse 219, Tape 350, Granular's proposed '%.1f ms', and Digital/PingPong, which switch to '%.2f s' at 1000 ms. (suggested: 201: '%.1f ms', matching the core-cell time readouts of the other modes (and the 1000 ms seconds switch if design 6.4 adopts it for all modes). 202: '%.0f ms', matching the body ms sliders (Tape Inertia, MultiTap Morph Time, Freeze Attack/Release). 204/207/208/210: '%.0f%%'. 205: '%+.0f%%'. Correct the [spectral] test expectations to match, and carry the row into design 9.3.)

Resolved: 201 core time with the seconds switch, 202 `"%.0f ms"`; design-system.md 6.4, 9.3 "Complete the Spectral
formatter"; panels/spectral.md section 5 note 1 (table with min / default / max strings).

### 11

[consistency, unresolved] shimmer: The section 6 formatter completion differs from every other mode in three ways. (a) Percent is '%.0f %%' with a space, so Shimmer would read '50 %' where Granular, Spectral, Tape, BBD, Digital, PingPong, Reverse, MultiTap and Freeze read '50%' (digital_params.h 306, tape_params.h 364 and so on). (b) Time is '%.0f ms', where the core time readouts of BBD, Reverse, Tape and Granular are '%.1f ms' and Digital/PingPong switch to '%.2f s'. Shimmer's range reaches 5000 ms, so it shows '5000 ms' beside Digital's '5.00 s'. (c) Pitch is '%+.0f st' while Granular's Pitch, the same label in the same widget, is '%+.1f st'. (suggested: Use '%.0f%%' (no space) for 304/309/303/306. Use '%.1f ms' for 300, or the Digital/PingPong '%.2f s' switch at 1000 ms if design 6.4 makes that the rule for every core time readout. Pick one Pitch format for both modes and record it in design 6.4: '%+.1f st' if Shimmer's semitone parameter is continuous, otherwise change Granular to '%+.0f st'. Update the [shimmer] test strings to match.)

Resolved: `"%.0f%%"` without a space, core time with the seconds switch ("5.00 s"), `"%+.1f st"` for both modes
(Shimmer's semitone parameter is continuous, stepCount 0, `shimmer_params.h:178-187`); design-system.md 6.4, 9.3
"Complete the Shimmer formatter"; panels/shimmer.md section 6.

### 12

[consistency, unresolved] granular: Granular, Spectral and Shimmer each say their readouts are bare numbers today and need a formatter completion, and each raises a 9.3 row 'for the design-system owner'. The design does not have those rows: 6.4 says nine modes are already formatted (only Tape, BBD, Digital, PingPong, Reverse and MultiTap are; Freeze is the 9.3 row), and 9.3 lists only the Freeze formatter, then 'Nothing else'. A build stage that follows the design would ship three modes whose readouts show '500' and '0' beside modes that show '500.0 ms' and '0%'. Separately, Granular's own Grain Size format '%.1f ms' disagrees with Freeze's Grain Size (design 9.3, id 1033, '%.0f ms'): same label, same widget, different precision. Freeze's Grain Size also matches the other body ms sliders (Tape Inertia, MultiTap Morph Time, Freeze Attack/Release). (suggested: Design owner: add 9.3 rows 'Complete the Granular / Spectral / Shimmer formatter' (granular_params.h, spectral_params.h, shimmer_params.h, each test-first), correct 6.4 to name the six modes that are already formatted, and state one unit rule there: core time readouts '%.1f ms' (with or without the >= 1000 ms '%.2f s' switch, decided once), body ms readouts '%.0f ms', percent '%.0f%%', Hz '%.1f Hz' or '%.2f Hz' by range. In the Granular spec, change 100 (Grain Size) to '%.0f ms'.)

Resolved: design-system.md 6.4 (the standard, decided once: seconds switch at 1000 ms for every core time readout,
body ms `"%.0f ms"`), 9.3 (three rows plus Gain, the seconds switch for Tape/BBD/Reverse, and the Freeze row), 11
"Readout standard"; panels/granular.md section 4 (100 is `"%.0f ms"`).

### 13

[consistency, unresolved] bbd: The spec says it follows 7.5 but does not. It puts Era at row 24 half L and Age at row 24 half R. Design 7.5 has Era at '24 full (556)' and Age at '76 half L (272)'. The design's order keeps Era on row 24 and Age on row 76, the slots they have in Digital CHARACTER (Era 24, Age 76), and Age is also row 76 in Tape CHARACTER. The spec's side-by-side layout moves Age to a slot no other mode uses. The spec also makes MODULATION standard (604,96,284,180), where 7.5 says tall (604,96,284,372), and lists neither change in its Deviations. It justifies the empty (604,288) cell 'as in Digital and PingPong', but in both of those modes that cell holds STEREO. (suggested: CHIP: follow 7.5. Era label (12,24,556,16) and segment (12,44,556,24); Age label (12,76,208,16), readout (220,76,64,16), slider (12,98,272,20). MODULATION: recommend the design owner amend 7.5 and 9.1 to standard (604,96,284,180), because Mod Depth and Mod Rate then sit at the same panel coordinates as in Digital and PingPong. Record it as a deviation in the spec, or else build the tall section of 7.5. Delete the 'as in Digital and PingPong' claim.)

Resolved: CHIP follows 7.5; MODULATION standard adopted by the design (2.4, 3.2 #3, 7.5, 9.1, 11 "MODULATION is
always a standard section", "BBD CHIP follows 7.5"); panels/bbd.md sections 1, 2.2, 2.3, 3, 4 and 5 (the "as in
Digital and PingPong" claim is gone).

### 14

[consistency, unresolved] reverse: The FILTER rows differ from design 7.8 without being listed as a deviation. 7.8 puts Filter Type (SEG 3) at row 76 and Filter Cutoff at row 128; the spec has Cutoff at row 76 (views 5b-5d) and Type at row 128 (5e-5f). The spec's order is the more consistent one, since Shimmer's FILTER also has Filter Cutoff at row 76 below the row-24 toggle. But with the design winning by rule, the XSLT author would receive two opposite instructions. Separately, the section 2 mapping table gives the Crossfade slider panel-absolute y 166; it should be 142 (96 + 46). (suggested: Recommended: the design owner amends 7.8 rows 805/806 (Filter Cutoff 76 full, Filter Type 128 full), and the spec adds this as deviation 5. Otherwise revert the spec to 7.8. Correct the Crossfade slider panel-absolute rect to (308,142,272,20).)

Resolved: design 7.8 amended to the spec's order (rows 805 at 76, 806 at 128), 11 "Reverse FILTER rows";
panels/reverse.md header, 1.3, section 2 (Crossfade slider (308,142,272,20)), section 4 item 5.

## Decisions taken while reconciling (design owner, 2026-10-28)

Recorded in design-system.md section 11, "Decisions of the reconcile stage", and applied to every panel spec:

- No `default-value` attribute on any bound control (VST3Editor binds the registered default, `vst3editor.cpp:265-287`):
  the ten specs' differing values are dropped.
- Menus take a full cell in a 284-wide section (Digital Mod Waveform full, 4.6 / 7.6).
- Two-entry segments alone on a row take the full cell (4.5).
- Units metadata stays as registered.
- The centre tick is drawn after the slider and mouse-disabled (ruling 7's "behind" cannot be seen through the opaque
  track).
