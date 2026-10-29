# MultiTap tap pattern editor: a musician's critique

The question behind this report: a producer opens MultiTap, picks Custom and tries to build an echo pattern. What gets
in the way? The evidence is the six before-shots (`specs/iterum-tap-editor/shots/before/`, rendered at 2x) and a reading of
`plugins/iterum/src/ui/tap_pattern_editor.{h,cpp}`, `tap_pattern_editor_logic.h`, the controller wiring in
`plugins/iterum/src/controller/controller.cpp`, `pattern_calculator.h`, `multitap_params.h` and
`dsp/include/krate/dsp/effects/multi_tap_delay.h`. I wrote this list before reading the seed facts in the header of
`.claude/workflows/iterum-tap-editor.js`. The last section compares the two.

Numbers used throughout:

- The editor is 852 x 140 pt. The ruler takes 15 pt, so the tap area is 125 pt high.
- The time axis runs 0..5000 ms, so 1 pt is 5.87 ms. A 16 pt tap bar covers 94 ms.
- The level axis is linear gain, at 0.8 % per pt.

**Shot order.** The order differs from the task's list. `tap_editor_screenshot_test.cpp:174-206` takes
`custom-16taps.png` last, after the levels shot, so the decaying levels of taps 1-4 in that image come from
`custom-8taps-levels.png`.

---

## High: the editor cannot do its job, or the display lies

### H1. Raising Taps adds taps you cannot see, and you cannot reach them

**Evidence:** `custom-8taps.png` shows bars numbered 1, 2, 3, 4, 6 and a clipped 8, with Taps = 8. Taps 5 and 7 are missing. `custom-16taps.png` shows 1, 2, 3, 4, 6 and a clipped "1" (it is 16), with Taps = 16.

**Cause:** `setActiveTapCount` (`tap_pattern_editor.cpp:548-561`) places each new tap at `(i+1)/(newCount+1)` and then
snaps it with the current Snap. Snap defaults to 1/4, which gives only five positions (0, 1250, 2500, 3750, 5000 ms):

- Tap 5 lands on 5/6 = 0.833 and snaps to 0.75. Tap 6 lands on 6/7 = 0.857 and also snaps to 0.75.
- Tap 7 (7/8) and tap 8 (8/9) both snap to 1.0.
- Every tap from 9 to 16 has a raw position of at least 0.9, so all of them snap to 1.0.

At 16 taps, taps 7-16 (ten taps) sit at exactly 5000 ms on top of one another, and taps 5-6 share 3750 ms.

**What the musician meets:**

- **Nothing changes on screen.** The Taps readout climbs and the display stays almost the same. Since the stacked taps are all at full level, you hear the same note louder, not more echoes.
- **Hidden taps cannot be grabbed.** `hitTestTap` walks from the highest index down (`tap_pattern_editor_logic.h:124`), so only the top bar of a stack can be clicked. To reach tap 7 you first have to drag tap 8 away, and you have no clue that tap 7 exists.
- **Stacks are ambiguous.** In `custom-16taps.png` the bar at 3750 ms is two-tone: tap 5's 25 % bar shows above tap 6's 10 % bar. It reads as one odd-looking bar with the number 6.
- **The taps you placed get overwritten.** Lowering Taps and raising it again rewrites every tap from the old count to the new one (`if (newCount > oldCount)` loop), so the positions you had set for taps 7-8 are gone after 8 -> 6 -> 8.

### H2. The time axis has no musical meaning, and the grid says it does

**Evidence:** In every shot the four background columns are identical, whatever Snap says. Compare `custom-8taps-snap8.png` (1/8) and `custom-8taps-snap-off.png` (Off) with `custom-8taps.png` (1/4).

- **The x axis is 0..5000 ms of absolute time.** The DSP computes `maxDelayMs_ x ratio` (`multi_tap_delay.h:855`) and never consults the tempo for Custom (`:616` excludes Custom from the tempo re-apply).
- **The snap divisions treat the full width as one whole note, at no tempo.** `getSnapDivisions` (`tap_pattern_editor_logic.h:252`) maps "1/4" to 4 columns of 1250 ms each. At 120 BPM a quarter note is 500 ms. The menu promises a note value and delivers a fraction of five seconds.
- **The grid ignores Snap.** `drawGridLines` always draws three lines at 1/4, 1/2 and 3/4 (`tap_pattern_editor.cpp:62`). Only the 4-pt ruler ticks follow Snap.
- **Off looks like 1/4.** With Snap = Off the ruler still draws quarter ticks (`:183-185`), so the two settings look the same.
- **Several Snap entries are duplicates:**
  - 1/4 dotted and 1/2 triplet are both 3 columns.
  - 1/1 triplet and 1/2 are both 2 columns.
  - 1/2 dotted, 1/1 and 1/1 dotted are all 1 column, which leaves only 0 ms and 5000 ms.
- **Dotted values are rounded.** 1/8 dotted is 5 columns (true value 5.33), 1/16 dotted is 11 (10.67), 1/32 dotted 21 (21.33) and 1/64 dotted 43 (42.67). The grid points stop lining up with each other across the width.
- **The default positions sit on no grid.** The registered times are `(i+1)/17` (`multitap_params.h:297`): 294, 588, 882 and 1176 ms in `custom-4taps.png`. That is no note value at any common tempo, and none of these taps sits on the 1/4 grid the menu shows.

A producer's first question is "is this tap on the beat, and which beat?" The display cannot answer it. The Note Value and
Note Modifier parameters, which give the mathematical patterns their tempo, are hidden for Custom. So Custom is the one
musical-looking pattern that cannot follow the song's tempo.

### H3. Grabbing a tap moves it

**Evidence:** `onMouseMoved` (`tap_pattern_editor.cpp:342-364`) sets the time to the cursor's absolute x and, when the drag started on the handle, the level to the cursor's absolute y. It then snaps the time. It does not apply the movement since mouse-down to the tap's old value.

- **With Snap on (the default), any touch snaps the tap to the grid.** On the first mouse move the tap jumps to the nearest grid point, even if you only meant to change its level. In `custom-4taps.png`, grabbing tap 1 (294 ms) to turn it down sends it to ratio 0 (1 ms). Taps 2-4 go to 0 or 1250 ms.
- **With Snap off, the tap jumps by the click offset.** The hit area is 20 pt wide, so the time jumps by up to ±10 pt (±59 ms). The handle is 8 pt tall, so the level jumps by up to 8 pt (6.4 %) when you click its lower part.
- **The cursor is misleading.** Over a handle the cursor is a vertical-resize arrow (`:495`), yet a handle drag moves the tap in time as well. Only the body drag is single-axis, and it is time only.

You cannot touch a tap without losing where it was. Escape is meant to undo this, but it is unlikely to work (M6).

### H4. There are no numbers anywhere

**Evidence:** All six shots. The only text in the editor is "100%", "0%" and the tap numbers.

- **No readout.** Neither a drag nor a hover shows a tap's time or level. The ruler has ticks but no labels: no ms, no beats, no bar numbers.
- **The host shows the wrong unit.** A host's automation lane shows Custom Time as a percentage (`multitap_params.h:374-377`, `"%.0f%%"`), so tap 1 reads "6%". One percent is 50 ms, which is too coarse to set a time from the lane, and the unit is wrong for a time. Custom Level also reads as a percentage of gain, where the rest of the editor uses dB for levels (design-system 6.4: Gain and Head Level use `"%.1f dB"`).
- **The design system asks for a value on every control.** Design-system 1.4 ("every readout shows the parameter's plain value with its unit") and 6.4 ("Every slider, bipolar and knob has a readout") make this the editor's largest inconsistency with the rest of Iterum. Thirty-two parameters are edited here and none of them shows its value.

### H5. The display stops matching what you hear

These findings come from reading the code. No shot shows them, so the design should confirm each one with a test.

- **Automation, undo and preset loads never reach the view.** `Controller::setParamNormalized` forwards only Snap and Taps (`controller.cpp:617-626`), and `syncAllViews` re-syncs only those two (`:1292-1306`). A tap time or level changed from outside the view leaves the bars where they were. The screenshot case has to call `tapEditor->setTapLevel` by hand next to the parameter write to make the levels shot render (`tap_editor_screenshot_test.cpp:191-198`). That call is the proof.
- **After a preset load in MultiTap mode the view stops updating.** Both bulk loads set `kModeId` (`controller.cpp:282`, `:1261`), and that sets `tapPatternEditor_ = nullptr` (`:601-603`). A `UIViewSwitchContainer` does not rebuild its view when the index is unchanged (`uiviewswitchcontainer.cpp:44`), so the old editor stays on screen with nothing connected to it. Taps and Snap no longer reach it, and drags on the stale bars still write parameters through the installed callback. The display recovers only after a mode switch or reopening the window.
- **The sound depends on whether the editor is open.** With the editor open, raising Taps writes the stacked, snapped positions of H1 to the parameters. With it closed, nothing writes them, so the new taps keep their stored values: the `(i+1)/17` defaults, or 0 (1 ms) after a Copy. The same automation of Taps, in the same session, gives a different pattern depending on whether the window was open.

---

## Medium: it works, but it misleads or fights you

### M1. Edits from the view carry no edit gesture

**Evidence:** The view brackets its drags with `beginEdit()` / `endEdit()` on itself (`tap_pattern_editor.cpp:293, 322, 387`), but its tag 920 is not a parameter. That makes `VST3Editor::controlBeginEdit` a no-op (`vst3editor.cpp:704-711`, no listener for 920). The parameter callback calls only `performEdit` (`controller.cpp` at the `setParameterCallback` lambda).

Every drag, double-click, Reset and tap added by the Taps slider therefore reaches the host as bare `performEdit`
calls on parameters 950-981. A host recording automation in touch or latch mode does not record a tap move, and undo
groups the edits unpredictably. Copy is the exception: it brackets each parameter correctly (`controller.cpp:1158-1162`).

### M2. The level axis spends its height on the loudest 6 dB

**Evidence:** `custom-8taps-levels.png`, with levels 1.0, 0.7, 0.5, 0.35, 0.25, 0.1, 0.05 and 0.0.

- **The useful quiet range gets almost no room.** On linear gain the top half of the editor covers only 0 to -6 dB. Echo levels a producer actually sets (-12 to -30 dB, gain 0.25 to 0.03) share the bottom quarter, about 28 pt. Tap 6 (-20 dB) is a 12-pt bar and tap 7 (-26 dB) a 6-pt one, smaller than the 8-pt handle drawn on top.
- **A silent tap is hard to grab.** A tap at level 0 draws its handle below its bar top, inside the ruler strip (`handleRect` from `barTop` to `barTop + 8`, `:110-115`), and can only be grabbed there.
- **The labels are in the wrong unit.** "100%" and "0%" are percent of gain. There is no label at the 50 % line, which is -6 dB, not "half as loud".

### M3. Taps at the edges draw half clipped, and the left edge is not an echo

**Evidence:** `custom-8taps.png` and `custom-16taps.png`, right edge: the bar at 5000 ms is cut in half and its number reads "8" or "1".

- **Edge taps are half off the view.** A bar is centred on its time (`:91-102`), so a tap at ratio 0 or 1 loses half its width and label to the view's edge.
- **Ratio 0 is a 1 ms delay.** The DSP clamps it to `kMinDelayMs` (`multi_tap_delay.h:167, 855`), which gives a comb filter, not an echo. Yet the display offers 0 as a full position, and with Snap on it is the position an early tap snaps to (H3).
- **Reset and Copy both put taps on the edges.** Reset uses `i/(n-1)` (`tap_pattern_editor.cpp:607-609`), so tap 1 lands at 0 (1 ms) and the last tap at 5000 ms, both clipped. Copy also puts the last tap at 1.0.

### M4. "Copy to Custom" does not copy the sound

**Evidence:** `calculatePatternRatios` (`pattern_calculator.h:120-127`) divides every time by the last tap's time, so the last tap always lands at ratio 1.0, which is 5000 ms.

- **The rhythm keeps its shape but not its timing.** Quarter Note with 4 taps at 120 BPM plays 500, 1000, 1500 and 2000 ms. Copied, it becomes 1250, 2500, 3750 and 5000 ms: 2.5 times slower and no longer tied to tempo.
- **Steep patterns lose their early taps.** Exponential with 16 taps puts tap 1 at 1/32768 of 5 s, clamped to 1 ms, and several more taps are crushed against the left edge.
- **Levels are not copied.** Copy writes only the times (`controller.cpp:1153-1170`). Custom levels then override the Spatial pattern's levels, because the processor applies the spatial pattern first and the custom levels after it (`processor.cpp:726-741`). A Decaying Level pattern therefore loses its decay when copied.

The one feature meant to say "start from what you hear" changes what you hear.

### M5. Four different rules for "default"

- **Registered default:** `(i+1)/17` (`multitap_params.h:297`).
- **Adding a tap:** `(i+1)/(count+1)`, snapped (`tap_pattern_editor.cpp:551-553`).
- **Double-click:** `(i+1)/(count+1)`, not snapped (`calculateDefaultTapTime`, `:289-291`).
- **Reset:** `i/(count-1)`, with the ends at 0 and 1 (`:607-609`).

So double-click "reset" moves a tap to a position that changes with the tap count and matches neither the add rule nor
Reset. The user cannot predict where it will go. Double-click on a stack (H1) resets only the top tap.

### M6. The modifier keys mean something else here than in the rest of the editor

- **Shift.** On every CSlider in Iterum, Shift is fine adjust (`ccontrol.h:104` `kZoomModifier = kShift`). Here Shift is an axis lock. The locked axis is chosen again on every mouse move from the total movement (`determineConstraintAxis`, `tap_pattern_editor.cpp:349-361`), so it can flip during a drag.
- **Ctrl.** Ctrl+click returns a VSTGUI control to its default (`kDefaultValueModifier = kControl`). Here Ctrl does nothing, and the default is reached by double-click instead (with the rule problems of M5).
- **No fine adjust.** With 5.87 ms per pt, placing a flam or a slapback by hand is guesswork.
- **Escape probably never arrives.** `CFrame` sends keys to the focus view and its parents (`cframe.cpp:548-566`). A click gives focus only to a control that wants focus (`cviewcontainer.cpp:1076`). Neither `CControl` nor this view sets `wantsFocus`, and the uidesc does not either. So "Escape cancels the drag" (`:404-410`) is most likely dead code in a host. Hosts often take Escape for themselves anyway.
- **Right-click is ignored** (`:275`), so the host's parameter context menu (automate, enter a value) is not available for a tap either.

### M7. Taps are numbered by index, not by time

You can drag tap 3 before tap 1, and the numbers then read 3, 1, 2 from left to right. The Spatial pattern works by
index (Cascade sweeps pan from tap 1 to tap N, `multi_tap_delay.h` `applyCascadePattern`), so a reordered pattern no
longer pans in time order. Nothing on screen explains why the stereo image jumps.

### M8. You cannot add or remove a tap where you want it

Clicking empty space does nothing (`onMouseDown` returns not-handled, `:283-285`). The only way to add or remove a tap
is the Taps slider in the PATTERN section, which adds a tap where H1 puts it and always removes the highest-numbered
tap, not the one you want gone.

### M9. Half the panel is empty for 19 of the 20 patterns

**Evidence:** In every shot the top-middle cell is empty. That is where the NOTE VALUE fieldset sits, and it is hidden for Custom. The panel spec (`panels/multitap.md` row 7) shows the CUSTOM PATTERN row only for Custom, so for every other pattern the lower half of the 480-pt panel is blank. Design principle 6 ("sparse modes use tall sections so empty space sits inside bordered groups") is broken twice.

The editor could serve every pattern if a read-only view drew the taps of the selected preset pattern. That would show
what "Golden Ratio" or "Dotted Eighth" actually does, and it would turn Copy into a visible "edit this one". One fact
for the design: the rhythmic and mathematical patterns are tap multiples of a note value (`multi_tap_delay.h:747-790`),
so an axis in beats can draw them without the host tempo. An axis in ms cannot, because the controller never sees the
tempo and Iterum has no processor-to-controller message.

---

## Low: polish and consistency

### L1. Colours and fonts are hard-coded

`tap_pattern_editor.h:132-137` and `.cpp` hard-code every colour and font. Design-system 5.2 tokens should replace them:

| Hard-coded value | Where | Design-system token |
|---|---|---|
| `35,35,38` (`#232326`) | background | `slider-track` `#272730` or `panel` |
| `60,60,65` | border | `border` `#30303a` |
| `50,50,55` | grid | none; `border` |
| `30,30,33` | ruler | none |
| `80,140,200` (`#508cc8`) | taps | `accent` `#5aa0e6` |
| `120,180,240` and `120,180,220` | selected tap and handle | none; two blues that are not tokens |
| white | hover | `text-light` |
| `180,180,185` | text | `text-dim` `#9c9caa` |

The fonts are Arial 8 pt (tap numbers, `:83`) and 9 pt (axis labels, `:147`). Design-system 5.4 makes 11 pt the smallest text in the editor and removes 9 pt.

### L2. A black frame around the "Snap" label

**Evidence:** Every shot shows a black rectangle around "Snap", with the text pressed against its right side.

The uidesc gives the label `transparent="false" back-color="panel"` (to mask the fieldset line, legend-style) but
not `style-no-frame`, so `CTextLabel` draws its default black frame (`editor.uidesc:592`). The panel spec row 7a says
"transparent". This is not part of the tap editor view, but it sits on the editor's own title line.

### L3. The axis labels sit on top of the taps

"100%" and "0%" are drawn inside the plot, over the first ~30 pt (about 175 ms) where early taps live (`drawLabels`, `:152-158`).

### L4. Tap numbers are hard to read

The numbers sit at the bar's foot in 8 pt white at alpha 200. They print over each other when taps stack, clip at the edges, and cover the handle of a tap near 0 %.

### L5. Close taps merge into one bar

A 16-pt bar is 94 ms wide, so taps less than about 94 ms apart overlap into one bar. The first 100 ms, where flams, doubling and slapback live, is 17 pt wide.

### L6. Spatial "Custom" has no editor

Spatial Pattern offers a "Custom" entry (`multitap_params.h:230`). Iterum has no per-tap pan parameter, so the entry leaves the pans as the previous pattern set them (`applySpatialPatternInternal`, `Custom: break`), and the display shows no pan at all. Parameters are frozen, so this is outside the editor's reach, but the design should not suggest per-tap pan.

---

## What is fine

- **The bars are the right picture.** Height for level and position for time, with a handle for level and a body for time, is the step-sequencer or EchoBoy-style picture a producer already reads. The gesture split is sound; it is only undiscoverable (H3, M6).
- **The hover feedback is useful.** A white handle on hover and the horizontal-resize cursor over the body tell you what a drag will do.
- **Snap is saved with the preset** as a parameter. That is right.
- **The panel placement is tidy.** Snap and Reset on the fieldset's title line keep 140 pt of travel. 140 pt is enough height once the level axis is no longer linear.
- **Taps read well.** The blue bars on the dark ground are clear. The colour is only a token away from `accent`.
- **Changes are smooth on the DSP side.** Tap-count and pattern changes morph, so a drag does not click.

---

## Compared with the seed facts (read after the list above)

**Confirmed:**

- Stacked taps on add (H1).
- The axis is 5000 ms; the "1/4" line is not a quarter note; the background grid is fixed while the ruler follows Snap; dotted values are rounded; the controller has no tempo (H2, M9).
- Three default rules; I count four, including the registered `(i+1)/17` (M5).
- Shrinking then growing overwrites placed taps (H1).
- Half-clipped edge taps; the left edge is 1 ms (M3).
- No readout and no ruler labels (H4).
- Hard-coded colours and fonts (L1).
- Empty lower half (M9).
- No edit gesture when a tap is added. It is wider than that: no edit from the view carries a gesture, because tag 920 is not a parameter (M1).

**One correction.** The seed says the useful levels "crowd the top third". On a linear gain axis it is the other way
round: the top half spans only 0 to -6 dB, and the quiet levels echoes actually use (-12 to -30 dB) are squeezed into
the bottom quarter (M2, measured on `custom-8taps-levels.png`).

**Not in the seeds:**

- The display goes stale after automation, undo or preset load, and the sound depends on whether the editor is open (H5).
- Grabbing a tap moves it, and with Snap on it snaps on the first move (H3).
- Copy to Custom changes the sound and drops levels (M4).
- The Snap table has duplicate entries; Snap Off looks like 1/4 (H2).
- The modifier keys contradict the CSlider conventions, and Escape probably never arrives (M6).
- Index order is not time order, which breaks Cascade pan (M7).
- Custom Time shows as "%" in host lanes (H4).
- The Snap label's black frame (L2).
- Spatial "Custom" has no editor (L6).
