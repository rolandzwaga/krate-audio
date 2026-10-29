# Prior art: how shipping multi-tap delays present a tap editor

Scope: MultiTap Custom pattern editor in Iterum. Method: vendor manuals and review articles fetched 2026-10-29. Where a manual page did not state something, the report says "not documented" and does not guess. Products that were searched but turned out not to have a tap editor are listed in "Products without a tap editor".

## Findings by severity

### High: things most products with a real tap editor agree on, and Iterum lacks

**H1. Time is shown as a musical grid with a snap toggle that a modifier key overrides.**
- Steinberg MultiTap Delay: "taps are quantized to the grid when added or moved"; holding Shift moves a tap freely even with a grid set. The grid length is set by the Delay setting (1/1 = one bar, 2/1 = two bars; without Sync it is in ms). https://archive.steinberg.help/nuendo_plugin_reference/v12/en/_shared/topics/plug_ref/multitap_delay/multitapdelay_r.html and https://www.soundonsound.com/techniques/cubase-multitap-delay-tricks
- Logic Delay Designer: with sync on, existing taps move to the nearest grid value and new or moved taps snap; a Grid pop-up sets the resolution (at 120 bpm, 1/16 = 125 ms per step). With sync off, taps do not snap. https://help.apple.com/logicpro-effects/mac/10.2/en.lproj/lgcef18b94fb.html and https://support.apple.com/guide/logicpro-ipad/lpip7b4c2daa/ipados
- Output Multi Tap Delay: Snap aligns taps to session BPM, Snap off allows free movement, grid resolution is a dropdown in the Grid. https://support.output.com/en/articles/10297683
- Waves SuperTap: grid divisions Free, Quarters, Quarter Triplets, Eighths, Eighth triplets, Sixteenths; a Snap-to-grid button. https://manualzilla.com/doc/5994232/supertap-user-manual
- Soundtoys EchoBoy Rhythm Echo: grid up to four bars of 4/4. https://www.bestservice.com/echoboy.html
- Implication for Iterum: the existing Snap menu is the right idea. Convention gap: no temporary override key (Shift in Steinberg) is documented in Iterum's list of behaviours.

**H2. A tap's time is shown as a number somewhere (Logic, Waves, FabFilter).**
- Logic: the Tap parameter bar shows the selected tap's number/name and its delay time; the Identification bar labels each tap with a letter and indicates its time position. https://support.apple.com/guide/final-cut-pro-logic-effects/lgex92de3b36/mac and https://help.apple.com/logicpro-effects/mac/10.2/en.lproj/lgcef18b9820.html
- Waves SuperTap: the delay slider shows beats or ms (switchable with the Grid Mode button); with Snap off, a +/- sign shows whether the delay is slower or faster than the nearest beat, no sign means on the grid. Tap Gain readout 0 to -24 dB in 0.1 dB steps. https://manualzilla.com/doc/5994232/supertap-user-manual
- FabFilter Timeless 3: a parameter value display appears on hover and shows time factor, level and pan; double-click a tap or a value to type it, Tab moves between parameters. https://fabfilter.com/help/timeless/using/delaydisplay
- Level is shown in dB in Waves (0 to -24 dB) and as a relative value in Logic; none of the documented editors describes a linear-gain-only axis.
- Not documented: a floating readout that follows the cursor during the drag (FabFilter's is on hover). The SuperTap snap-offset sign is the closest to a live drag readout.

**H3. Taps can be typed, not only dragged.** FabFilter (double-click to type time factor, level, pan), Waves (keypad entry, arrow keys tweak values), Logic (edit the Delay Time field). Sources as above.

**H4. A background action set for whole-pattern operations.** FabFilter: right-click background for create random pattern, reset panning, delete all, space evenly, ramp up or down; the same actions from a tap menu beside the TAPS button, which also loads and saves tap patterns. Steinberg: Tap Rhythm button (click a rhythm), Quantize, Randomise. frctl dlay: 12 built-in rhythm patterns (Tresillo, Clave), ripple knobs. Sources: FabFilter URL above; https://www.soundonsound.com/techniques/cubase-multitap-delay-tricks; https://www.kvraudio.com/news-print.php?id=66936
- Implication: Iterum's Reset and Copy buttons are in line with this, but there is no evenly-space, ramp or random action.

### Medium

**M1. Add and remove gestures.**
- FabFilter: double-click anywhere adds a tap, and if the mouse is held after the second click it can be dragged immediately to set time and level; a floating "+" near the top adds a tap at level 0 dB; right-click then Add Tap. Delete: hover delete control, or right-click then delete. Up to 15 extra taps.
- Logic: click on the Identification bar creates a tap at that position; Delete key removes selected taps; dragging a tap's letter out of the display deletes it; right-click then "Delete tap(s)"; Option-drag copies. Also three tap-pad "play the rhythm" creation (Start, Tap, Last Tap; stops at 10 s or the 26th tap). https://support.apple.com/guide/final-cut-pro-logic-effects/lgex92de2bbf/mac and https://help.apple.com/logicpro-effects/mac/10.2/en.lproj/lgcef18b8ed2.html
- Steinberg: a Taps count control (up to 8) plus Tap Rhythm; the Taps control can be automated to add or remove repeats live. Output: a Taps count control (up to 8). Both are count-driven like Iterum.
- Steinberg: tap numbers need not follow time order (tap 7 can sit before tap 3).
- Waves SuperTap: fixed set of taps with an on/off LED each (lit in the on button, delay slider and Pan Graph).

**M2. Selection and multi-edit.** FabFilter: click, rectangle on background, Ctrl/Cmd-click to add, Shift-click for a range. Logic: click, drag across the background, Shift-click for non-adjacent; at least one tap is always selected; Command-drag across several taps draws a value curve; multiple taps change relative to each other. SuperTap: rectangle selection of Gain controls. frctl dlay: rubber-band selection and group drag. Not applicable if Iterum keeps a fixed tap count with no selection concept, but value-curve drawing is the fast way to set 16 levels.

**M3. A reset-to-default modifier on values.** Logic: Option-click a value in the Tap parameter bar resets it to default (all selected taps if several). FabFilter: right-click a tap to reset its panning.

**M4. Per-tap parameter views.** Logic's tap display has view buttons (Cutoff, Reso, Transp, Pan, Level) with a toggle-bar row, and Cmd-Option temporarily switches to Level. frctl dlay switches one surface between gain, pan, pitch, formant, filter, saturation. FabFilter and SuperTap show level and pan on the tap itself (SuperTap: vertical = gain, horizontal = rotation in the Pan Graph). Iterum edits time and level only, so this is context, not a requirement.

**M5. Time-axis scale.**
- Logic: autozoom button zooms so all taps are visible, plus an overview display; zoom is therefore part of the model.
- FabFilter: axis is 0 to 100 percent of the main delay time (time factor), leftmost bar is the dry input, rightmost bar the main output tap; no axis labels documented beyond that.
- Steinberg / Output / EchoBoy: bars of the host tempo grid.
- SuperTap: up to about 4958 ms of delay (per retailer text in search results), shown as beats or ms.
- Iterum's axis is a fixed fraction of 5000 ms; the position unit shown to the user is the open question for the design.

### Low

**L1. Presets versus custom.** No vendor documents showing a preset pattern read-only in the same editor. Observed: EchoBoy has preset rhythm patterns that can also be saved and drawn manually in the same graph (reviewer text, https://www.bestservice.com/echoboy.html); FabFilter keeps loaded patterns as ordinary editable taps with a tap menu to load/save; SuperTap presets simply set the taps ("Full SuperTap Reset" is default). The common pattern is: a preset loads into the editable taps, the display always shows the taps. Showing Iterum's mathematical patterns read-only would be new, not a convention, though it follows from "the display always shows the taps".

**L2. Modulation.** FabFilter allows dragging a modulation source onto a tap. Not relevant to Iterum.

**L3. Edge cases not documented by any vendor:** clipping of taps at the edge of the display, minimum delay at the left edge, overlap of numbers at low levels. No source found; treat as Iterum's own design problem.

## Products without a tap editor (so no lessons)
- Native Instruments Replika XT: two delay lines A/B and a display of how repeats unfold; no tap grid. https://native-instruments.com/ni-tech-manuals/replika-xt-manual/en/delay-section
- Native Instruments Molekular, u-he Colour Copy, Melda MMultiTap/MMultiBandDelay, Ableton Echo and Beat Repeat: no tap-editor documentation was found. Echo has Sync/Time delay mode and note-value divisions; Beat Repeat has Grid, Gate and Chance but no per-tap editing. (https://www.musicradar.com/how-to/ableton-live-10-echo-explained). Not claimed as studied.
- Could not fetch: the Steinberg v12 reference text beyond the search snippet (the page fetch was by summary), Waves PDF page for modifier keys (SuperTap documents none), Logic Mac "Align tap values" page.

## Table: conventions that recur versus product-specific

| Aspect | Recurring (users expect) | Product-specific |
|---|---|---|
| Time axis | Musical grid at host tempo with a grid-resolution menu, or ms when not synced (Steinberg, Logic, Output, SuperTap, EchoBoy) | FabFilter 0-100% of main delay; Logic autozoom/overview; EchoBoy up to 4 bars |
| Snap | On/off toggle plus resolution menu (Logic, Output, SuperTap, Steinberg); a modifier lets you bypass it (Steinberg Shift) | SuperTap +/- offset sign when snap is off |
| Grid labels | Time or note value of the selected tap shown in a number field (Logic Delay field, SuperTap slider, FabFilter hover) | Logic letter labels on an Identification bar; Steinberg numbered taps |
| Tap level axis | Vertical drag = level (Logic, FabFilter, Output, SuperTap, frctl) | SuperTap gain 0 to -24 dB; others undocumented |
| Add | Drag-able existing taps with a count control (Steinberg, Output); click/double-click on empty space (FabFilter, Logic bar) | FabFilter hover "+"; Logic tap pads |
| Remove | Delete key or context menu (Logic, FabFilter) | Logic drag-out; count control (Steinberg, Output) |
| Selection | Click, rectangle, Shift/Ctrl-click for multi (Logic, FabFilter, frctl, SuperTap) | Logic at-least-one-selected rule |
| Readout while dragging | Numeric value somewhere on screen while a tap is touched (FabFilter hover, Logic parameter bar, SuperTap) | A cursor-following tooltip: not documented in any |
| Typed entry | Double-click or field entry (FabFilter, Logic, SuperTap) | SuperTap arrow keys |
| Whole-pattern actions | Random, even spacing, ramp, quantize, rhythm patterns, load/save (FabFilter, Steinberg, frctl) | Tap-tempo rhythm entry (Logic, Steinberg, SuperTap Pattern mode) |
| Reset to default | Option-click / menu reset (Logic, FabFilter) | SuperTap "Full SuperTap Reset" preset |
| Preset vs custom | Presets load into the same editable taps | EchoBoy preset patterns drawn in the same graph |

## Source list
- Logic Pro Delay Designer: https://help.apple.com/logicpro-effects/mac/10.2/en.lproj/lgcef18b9820.html , .../lgcef18b8ed2.html , .../lgcef18b94fb.html , .../lgcef18b9dd4.html ; https://support.apple.com/guide/final-cut-pro-logic-effects/lgex92de311f/mac , .../lgex92de2bbf/mac , .../lgex92de3088/mac , .../lgex92de3b36/mac ; https://support.apple.com/guide/logicpro-ipad/lpip7b4c2daa/ipados
- FabFilter Timeless 3: https://fabfilter.com/help/timeless/using/delaydisplay
- Waves SuperTap manual: https://manualzilla.com/doc/5994232/supertap-user-manual
- Steinberg MultiTap Delay: https://archive.steinberg.help/nuendo_plugin_reference/v12/en/_shared/topics/plug_ref/multitap_delay/multitapdelay_r.html ; https://www.soundonsound.com/techniques/cubase-multitap-delay-tricks
- Output Multi Tap Delay: https://support.output.com/en/articles/10297683
- Soundtoys EchoBoy: https://www.bestservice.com/echoboy.html
- frctl dlay: https://www.kvraudio.com/news-print.php?id=66936
- NI Replika XT: https://native-instruments.com/ni-tech-manuals/replika-xt-manual/en/delay-section
- Ableton Echo: https://www.musicradar.com/how-to/ableton-live-10-echo-explained
