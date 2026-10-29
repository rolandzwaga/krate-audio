# MultiTap tap editor: code facts

Scope: what `TapPatternEditor` (Custom timing pattern) does today. Everything below was read from the source at
the cited file:line on branch `ui/iterum-consistency`. Nothing was run. Items that need a runtime probe to be
certain say so ("READ-ONLY CLAIM"). Paths are relative to `F:\projects\iterum`.

Abbreviations: TPE = `plugins/iterum/src/ui/tap_pattern_editor.cpp`, TPL = `.../tap_pattern_editor_logic.h`,
TPH = `.../tap_pattern_editor.h`, CTL = `plugins/iterum/src/controller/controller.cpp`,
MTP = `plugins/iterum/src/parameters/multitap_params.h`, DSP = `dsp/include/krate/dsp/effects/multi_tap_delay.h`,
PROC = `plugins/iterum/src/processor/processor.cpp`.

## 0. Baseline model (what the numbers mean)

- Tap time is stored as a ratio 0..1 (16 params, IDs 950-965, plain `Parameter`, default `(i+1)/17`, MTP:295-305).
  Tap level is a ratio 0..1 (IDs 966-981, default 1.0, MTP:315-323).
- DSP maps time ratio to ms as `clamp(maxDelayMs_ * ratio, kMinDelayMs, maxDelayMs_)` (DSP:855).
  `maxDelayMs_` = 5000 (PROC:96 `prepare(..., 5000.0f)`), `kMinDelayMs` = 1.0 (DSP:167).
  So the editor's x axis is **0..5000 ms, absolute, tempo independent**. 852 px wide = 5.87 ms per pixel. Ratio 0
  becomes 1 ms (not 0), and the clamp also eats any ratio below 0.0002.
- DSP maps level ratio to dB as `level > 0.001 ? gainToDb(level) : -96`, clamped to [-96, +6] (DSP:859-863).
  So the y axis is linear gain: 1.0 = 0 dB, 0.5 = -6.0 dB, 0.25 = -12 dB, 0.1 = -20 dB, 0.001 = -60 dB, and anything
  at or below 0.001 jumps to -96 dB. The top third of the travel (0.667..1.0) covers only 0 to -3.5 dB; the top half
  covers 6 dB. Labels in the view say "100%" and "0%" (TPE:152-158) and a gridline sits at 50% (TPE:69-73), which is
  -6 dB.
- The custom pattern ignores tempo and Note Value entirely. `applyCustomTimingPattern` (DSP:852-868) reads only
  `customTimeRatios_`, `customLevels_`, `activeTapCount_`. `process()` still calls `setBaseTimeMs` for pattern >=
  GoldenRatio when tempo is known (DSP:628-636), but that re-applies the same custom pattern (DSP:354-355). **A saved
  custom pattern is therefore the same milliseconds at every tempo.** A tempo change does nothing to it. Stored form:
  16 plain floats of time ratio + 16 of level + snap int, appended after the older MultiTap fields (MTP:393-415);
  loaded "if available" so older streams keep defaults (MTP:451-467). The state stream has no version field.

## 1. Findings by severity

### HIGH

**H1. Any write to the Mode parameter nulls the controller's editor pointer, even when the mode does not change.**
CTL:601-603 `if (id == kModeId) tapPatternEditor_ = nullptr;` sits before the bulk-load early return (CTL:607). It is
reset only inside `createCustomView` (CTL:790). Every preset/state load goes through
`editParamWithNotify(kModeId, ...)` (CTL:1260-1262) which calls `setParamNormalized(kModeId, ...)`. If the preset is
also MultiTap, `UIViewSwitchContainer` should keep the existing MultiTap template (and the editor view inside it),
but the controller no longer points at it. Consequences while the view stays open: `syncAllViews` skips it
(CTL:1293), Snap and Tap Count changes no longer reach it (CTL:617, 623), Reset does nothing (CTL:1187), Copy cannot
update it (CTL:1166). READ-ONLY CLAIM: whether the switch container really keeps the template on an equal value was
not traced into VSTGUI; confirm with a probe that loads a MultiTap state twice with the editor open.

**H2. Raising the tap count overwrites stored custom taps in every pattern mode, and on every preset load.**
`setParamNormalized` forwards Tap Count to the view with no check of the Timing Pattern (CTL:622-626).
`setActiveTapCount` (TPE:537-565) then, for every index in `[oldCount, newCount)`, writes a new position and level to
the view arrays AND calls the parameter callback (TPE:559-560 -> CTL:793-799: `setNormalized` + `performEdit`). So:
- A user who built an 8-tap custom pattern, switched to "Quarter", dropped the count to 4 and raised it to 8 has
  taps 5-8 of the custom pattern silently replaced (Quarter does not use them, the editor is just hidden).
- A host automating Tap Count rewrites 950-981 on the UI thread and emits `performEdit` for them.
- Preset load with the editor open: `syncAllViews` (CTL:1292-1306) calls `setActiveTapCount(presetCount)` against
  the view's STALE `activeTapCount_`. If the preset has more taps than the view had, the loaded custom values for
  the extra taps are overwritten through the callback just after being loaded. (Together with H1 this may not even
  be reached; either way the view never reads 950-981 again, see M9.)

**H3. "Copy" (CopyPatternButton) always copies as if the tap count were 2.**
CTL:1135-1139: `tapCount = int(param->toPlain(param->getNormalized()))`. Tap Count is a plain `Parameter`
(MTP:234-240; SDK `addParameter` does `new Parameter(info)`, `vstparameters.cpp:373`) and `Parameter::toPlain`
returns the normalized value (`vstparameters.cpp:112-115`). So `int(0.143)` = 0, clamped to 2 (CTL:1139). Then
ratios are computed for 2 taps and every tap `i >= 2` is written as 0.0 (CTL:1154) = 1 ms. Every other site decodes
the count with `2 + norm*14 + 0.5` (CTL:624, 761, 1302; MTP:118). READ-ONLY CLAIM from SDK source; no test covers
`copyCurrentPatternToCustom` (tests cover `calculatePatternRatios` only, `tap_pattern_editor_test.cpp:635+`).

**H4. No edit gesture reaches the host for any tap write.**
The parameter callback (CTL:793-799) calls `param->setNormalized` + `performEdit(paramId, v)` and never
`beginEdit(paramId)` / `endEdit(paramId)`. The view calls its own `CControl::beginEdit()/endEdit()` (TPE:293/298,
322/387, 426) but those go to `VST3Editor::controlBeginEdit` (vstgui `vst3editor.cpp:704-711`), which forwards only
when the listener has a Parameter. Tag 920 is not a parameter, so `ParameterChangeListener::beginEdit` is a no-op
(`vst3editor.cpp:192-196`, `if (parameter)`). Per path:

| Path | Writes | begin/end for the 32 param IDs |
|---|---|---|
| Drag a tap (TPE:328-377) | time+level of one tap on EVERY mouse move, unconditionally, even when nothing changed | none |
| Escape / cancelDrag (TPE:413-433) | restores time+level | none |
| Double-click (TPE:288-302) | time+level of one tap | none |
| `setActiveTapCount` increase (TPE:559-560) | time+level of each new tap | none |
| `resetToDefault` (TPE:602-627) | time+level of each active tap (up to 32 `performEdit`) | none |
| `copyCurrentPatternToCustom` (CTL:1153-1182) | 16 time params + Timing Pattern | yes, per param, through `componentHandler` directly (not the editor callback) |
| Controller `editParamWithNotify` (CTL:1220-1231) | other bulk loads | yes |

The Copy path writes only times: levels are untouched.

**H5. A tap at level 0 cannot be grabbed again.**
`hitTestTap` builds the bar from `levelToYPosition(level)` to `viewHeight` where `viewHeight` is the tap area
(TPL:115-134, called with `tapAreaHeight`, TPE:258-263). At level 0, `tapTop == tapBottom == tapAreaHeight`, a
zero-height box; the bar and its 8 px handle (TPE:110-114) draw below the tap area, over the ruler, where the hit
test never looks. Level 0 is reachable by dragging the handle to the bottom (`yToLevel` clamps, TPL:68-73). Recovery
is only Reset or lowering the tap count. Taps at levels below ~0.06 have a handle taller than the bar, so grabbing the
handle region near the bottom also falls outside the box (`y <= tapBottom` fails for the lower part of the handle).

### MEDIUM

**M1. Add-tap placement and its trace (Taps 4 -> 8 at Snap 1/4).**
Rule: new tap `i` (0-based) gets `(i+1)/(newCount+1)`, then `snapToGrid(.., snapDivision_)` (TPE:551-553); level
1.0 (`kDefaultTapLevel`, TPL:210).
Snap 1/4 = `N4` = 4 divisions (TPL:272); default Snap is index 14 = 1/4 (MTP:48, 198).
Starting state of a fresh instance: taps 1-4 are at the param defaults `(i+1)/17` = 0.0588, 0.1176, 0.1765, 0.2353
(294, 588, 882, 1176 ms), NOT on any grid, because snapping happens only on drag (TPE:364). The view's constructor
fills `calculateDefaultTapTime(i, 16)` = `(i+1)/17` too (TPE:18-21), the same numbers.

- **Jump 4 -> 8 in one `setActiveTapCount(8)`** (preset load / automation / fast knob drag): `(i+1)/9` for i=4..7 =
  0.5556, 0.6667, 0.7778, 0.8889; `round(x*4)/4` -> **0.50, 0.75, 0.75, 1.00** = 2500, 3750, 3750, 5000 ms. Taps 6
  and 7 coincide; tap 8 sits at the right edge, half clipped.
- **Stepwise 4 -> 5 -> 6 -> 7 -> 8** (how a knob drag with 15 steps normally arrives, each step calls
  `setParamNormalized` -> `setActiveTapCount`, CTL:623-626): tap 5 `5/6`=0.833 -> 3.33 -> **0.75**; tap 6 `6/7`=0.857
  -> 3.43 -> **0.75**; tap 7 `7/8`=0.875 -> 3.5 -> `std::round` half away from zero -> **1.00**; tap 8 `8/9`=0.889 ->
  **1.00**. Result 0.75, 0.75, 1.00, 1.00: the seed's claim holds for single steps, not for a jump.
- Stepwise growth to 16: taps 7..16 all land at 1.00 (the formula `(i+1)/(i+2)` tends to 1; with 4 grid cells everything
  above 0.875 rounds to 1.0). Ten identical bars at 5000 ms. With Snap Off the positions are distinct but crowd the
  right end (0.833, 0.857, 0.875, ...).
- Whether a drag arrives stepwise or with skips depends on the mouse events: the knob is not quantized in the
  controller, only `int(2 + v*14 + 0.5)` is, so the result of "4 -> 8" is not deterministic across gestures.
- At every Snap value the add rule is "append toward the right edge", not "spread across the line".

**M2. Decrease then increase.** Decrease only sets `activeTapCount_` (TPE:539-540) and writes nothing; the removed
taps' values stay in the view arrays and in params 950-981. The next increase overwrites them (M1 rule) and notifies
(TPE:548-562). Placed taps are not remembered. Same in the DSP: `customTimeRatios_` keeps the old values until the
params change.

**M3. Four (five) different "default position" rules; they disagree.**

| Rule | Where | Formula (count n, tap i) | n=4 |
|---|---|---|---|
| Parameter / view-constructor default | MTP:295-305, 63-69; TPE:18-21 | `(i+1)/17` regardless of n | 0.059 0.118 0.176 0.235 |
| Add tap (count increase) | TPE:551-553 | `snap((i+1)/(n+1))`, n = NEW count | see M1 |
| Double-click a tap | TPE:289-291, TPL:203-207 | `(i+1)/(n+1)`, no snap | 0.2 0.4 0.6 0.8 |
| Reset button | TPE:602-627 (via CTL:1185-1190) | `i/(n-1)` (n=1 -> 0.5), level 1.0; inactive taps set to 0 locally, params NOT touched | 0 0.333 0.667 1.0 |
| Copy button (pattern -> custom) | `pattern_calculator.h:109-128`, CTL:1141-1155 | `(i+1)/n` for rhythmic and Linear/Custom; last tap always 1.0 | 0.25 0.5 0.75 1.0 |

Double-click and add agree before snapping only when count is the same; Reset puts the first tap at ratio 0 (1 ms)
and the last at 1.0. Double-click ignores Snap. Reset sets levels to 1.0 and writes 2n `performEdit`.

**M4. The Snap grid is not tied to the tempo or to the visible background.**
- `snapToGrid` = `round(r*N)/N` with N from the table (TPL:292-302); the full width is "one whole note" by
  convention (1/1 = 1 division, TPL:280), but the width is 5000 ms, not four beats. 1/4 snap = 1250 ms steps at every
  tempo. It equals a real quarter note only at 48 BPM. At 120 BPM the 5000 ms width is 2.5 whole notes.
- The background vertical lines are fixed at 1/4, 2/4, 3/4 of the width plus one 50 % horizontal line, whatever Snap
  says (TPE:61-73). Only the ruler ticks follow Snap (TPE:179-211). With Off the ruler shows quarters (TPE:183-185).
  The ruler has no numbers. Major ticks are those on quarter positions when N >= 4; for N < 4 only the two ends
  (TPE:196-203). The ruler for N=96 puts a tick every 8.9 px.
- Taps are not re-snapped when Snap changes (CTL:615-620 only calls `setSnapDivision`).
- Dotted/triplet columns against the true lengths (fraction of a whole note; `columns = 1/length`):

| Snap | True length | True columns | Table (TPL:252-286) | Error |
|---|---|---|---|---|
| 1/64T | 1/96 | 96 | 96 | 0 |
| 1/64D | 3/128 | 42.67 | 43 | 0.8 % |
| 1/32T | 1/48 | 48 | 48 | 0 |
| 1/32D | 3/64 | 21.33 | 21 | 1.6 % |
| 1/16T | 1/24 | 24 | 24 | 0 |
| 1/16D | 3/32 | 10.67 | 11 | 3 % |
| 1/8T | 1/12 | 12 | 12 | 0 |
| 1/8D | 3/16 | 5.33 | 5 | 6 % (line 1 at 0.2, true 0.1875 = 312 ms off at 5000 ms) |
| 1/4T | 1/6 | 6 | 6 | 0 |
| 1/4D | 3/8 | 2.67 | 3 | 12 % |
| 1/2T | 1/3 | 3 | 3 | 0 |
| 1/2D | 3/4 | 1.33 | 1 | 25 % (grid is only 0 and 1.0; true lines 0, 0.75) |
| 1/1T | 2/3 | 1.5 | 2 | 33 % |
| 1/1D | 3/2 | 0.67 | 1 | grid 0 and 1.0; true line past the end |

Straight values and all triplets except 1/1T are exact; every dotted value is rounded, and the grid
always ends at 1.0 even where the true last line is not at 1.0. The test file pins the rounded numbers
(`tap_pattern_editor_test.cpp:599-629`, comments "Rounded").

**M5. Morphing into Custom takes the wrong path.** Timing Pattern change with the same tap count calls
`morphToPattern` (PROC:717-722). Its targets come from `calculatePatternTimes`, whose Custom case falls into
`default: n * baseTimeMs_` (DSP:1035-1036), not the custom ratios. `currentTimingPattern_` stays the old pattern
during the morph, so the per-block `setCustomTimeRatio` calls (PROC:733-741) only store values (DSP:315-321). At morph
end `updateMorph` snaps to `n * baseTimeMs_` and sets Custom (DSP:1050-1057); the next block's `setCustomTimeRatio`
then jumps the taps to the real custom times. Tap count changes use `loadTimingPattern` directly and apply the custom
pattern immediately (PROC:709-716). READ-ONLY CLAIM: the audible effect (taps glide to the wrong place, then jump)
follows from the code, not measured.

**M6. Copy does not reproduce the sound of the source pattern.** `calculatePatternRatios` normalizes so the last tap
is at 1.0 (`pattern_calculator.h:121-128`) and treats all 14 rhythmic patterns as linear `(i+1)/n`. The DSP rhythmic
patterns are `n * tempoDerivedBase` (DSP:1024) (quarter at 120 BPM: 500, 1000, 1500, 2000 ms), Copy yields
1250, 2500, 3750, 5000 ms for 4 taps. LinearSpread, Primes, Exponential in the DSP use `baseTimeMs_` and
different formulas (DSP:802-849). The Copy comment says it "mirrors" the DSP (`pattern_calculator.h:7`); it does not.
(Currently moot because of H3.)

**M7. Tap bars at ratio 0 and 1 draw half outside the view.** Bar rect = `centerX +- 8` (TPE:91-102,
`kTapBarWidth` 16, TPL:27); at ratio 0/1 half of it lies outside the view rect and is clipped; the handle and the
number label likewise (TPE:110-137). The hit area is 20 px wide (TPL:24) and also extends past the edge. A tap at the
left edge is a 1 ms delay (see section 0). Reset produces exactly this (M3).

**M8. Overlap, labels, drawing details.**
- Draw order is ascending index, so the highest index paints over (TPE:86); the hit test goes highest first
  (TPL:124). Identical (time, level) taps stack: the lower index is unreachable until the top one moves. Bars closer
  than 16 px (0.019 ratio = 94 ms) overlap; there is no transparency.
- The number label sits at the foot of the bar (`barBottom - 12 .. barBottom - 1`, TPE:132-139), white alpha 200,
  8 pt. When the bar is shorter than 12 px (level < 0.096) the label spills above the bar; when shorter than 20 px
  the 8 px handle overlaps the label. Overlapping bars bury each other's numbers.
- "100%" and "0%" level labels draw after the taps (TPE:143-159), over any tap near the left edge.
- Fonts: `CFontDesc("Arial", 8)` and `("Arial", 9)` constructed on every draw (TPE:83, 147). The design system's
  smallest text is 11 pt (`design-system.md:367`, fonts at 358-365; uidesc fonts lines 17-20).
- Colors are `constexpr` literals: background 35,35,38; border 60,60,65; grid 50,50,55; tap 80,140,200; selected
  120,180,240; text 180,180,185 (TPH:132-137); handle colors (255,255,255 / 180,220,255 / 120,180,220, TPE:120-124);
  label (255,255,255,200, TPE:138); ruler fill (30,30,33, TPE:169). The uidesc tokens are `background #121216`, `panel
  #1b1b22`, `border #30303a`, `slider-track #272730`, `accent #5aa0e6`, `text #e6e6ec`, `text-dim #9c9caa` (editor.uidesc
  lines 4-14). None are used.
- Selection color shows only during a drag (`selectedTap_` reset to -1 on mouse up, TPE:391); there is no persistent
  selection. Hover affects only the handle (TPE:117).

**M9. The view never learns new time/level values after it is created.** `setTapTimeRatio`/`setTapLevel` are called
only in `createCustomView` (CTL:775-784), by Copy (times only, CTL:1166-1171) and by double-click. No override
forwards IDs 950-981 (CTL:615-626 forward only Snap and Tap Count). Automation of a custom time/level param, undo in a
host, a preset load with the MultiTap panel open: the DSP changes, the picture does not. Switching modes recreates the
view (CTL:601-603 comment) so the picture is right after a mode round trip.

**M10. Dragging semantics.**
- Level can change only if the press landed in the top 8 px of the bar (`dragStartedOnHandle_`, TPE:314-320); the body
  moves time only. The hit-area handle in the cursor code uses `kTapHandleWidth` = 20 px (TPE:455-479) while the drawn
  handle is 16 px.
- Both axes use the absolute pointer position, not a delta (TPE:342-346): a press 10 px off the bar center jumps the tap
  by up to 10 px (59 ms), a press at the bottom of the 8 px handle jumps the level by up to 0.064.
- Every mouse move calls both `notifyTimeRatioChanged` and `notifyLevelChanged` (TPE:372-373) even when one value
  has not changed: 2 `performEdit` per event.
- Shift locks to the dominant axis after 5 px (TPE:348-361, TPL:161-171). Snap applies to time only, after the shift
  constraint (TPE:364). Escape restores both values (TPE:404-433) but only works if the view receives key events (not
  traced). Right click is ignored (TPE:275-277). Double-click on empty space does nothing (TPE:282-285).

### LOW

**L1. Editor is only a Custom-pattern tool.** The CUSTOM PATTERN fieldset is hidden unless Timing Pattern >= 0.99
(`CTL:902-910`, uidesc:591-595 is the fieldset at 12,288,876,180 holding Snap label/menu, Snap menu, Reset and the editor
at 12,28,852,140). For all 19 other patterns, the lower half of the MultiTap panel is empty and nothing shows
where the taps are.

**L2. DSP cost of the Custom path.** Every audio block while pattern == Custom: 32 atomic loads and 32 setter calls,
each of which re-runs `applyCustomTimingPattern` (16 taps, `gainToDb` per tap) (PROC:733-741, DSP:333-339):
16 x 16 = 256 loop bodies and 256 `gainToDb` calls per block. Not an audio-safety problem; wasteful and a reason to
move the apply out of the setters in the redesign.

**L3. Resetting inactive taps is local.** `resetToDefault` sets inactive taps to 0 in the view arrays only
(TPE:618-622); params 950-981 for those taps keep their values, so view and params disagree until the next
count increase overwrites both.

**L4. Escape/drag guards.** `setActiveTapCount` cancels a drag when the selected tap falls out of range (TPE:543-545);
`onPatternChanged` cancels a drag but is never called (grep: no caller outside the class).

## 2. Edit gestures, one place (see H4 table)

Nothing in the editor ever calls `beginEdit(id)`/`endEdit(id)` for 950-981 and all writes use
`performEdit` through `Controller::performEdit` (CTL:797). Copy and `editParamWithNotify` are the only compliant paths.

## 3. Tests that exist

`plugins/iterum/tests/unit/ui/tap_pattern_editor_test.cpp`: 25 cases, all on the pure functions in TPL and
`calculatePatternRatios`. Nothing exercises `TapPatternEditor` (setActiveTapCount, resetToDefault, drag), the
controller wiring, or the DSP custom path. The 4 -> 8 behavior, H2, H3 and M5 are untested.

## 4. Seed facts from `.claude/workflows/iterum-tap-editor.js` (header lines 8-20) and the maintainer critique

| # | Seed | Verdict | Evidence |
|---|---|---|---|
| S1 | `setActiveTapCount` places a new tap at `(n+1)/(count+1)` snapped; 5th and 6th both snap to 0.75, 7th and 8th to 1.0 | **Confirmed for step-by-step growth; refuted for a jump 4 -> 8**, which gives 0.50, 0.75, 0.75, 1.00. The mechanism (identical snapped positions, invisible stack) holds. | M1, TPE:551-553 |
| S2 | x axis is 0..5000 ms; Snap table treats the width as one whole note; 1/4 is a quarter note at no tempo | **Confirmed.** Exact only at 48 BPM. | section 0, M4 |
| S3 | Background lines are fixed quarters whatever Snap says; only ruler ticks follow it | **Confirmed.** | TPE:61-67 vs 179-211 |
| S4 | Dotted divisions are rounded (1/8D = 5 columns, true 5.33) | **Confirmed**, and the table of every dotted/triplet value is in M4 (1/8D, 1/4D, 1/2D, 1/1T and 1/1D are the visibly wrong ones). | TPL:252-286 |
| S5 | The controller has no tempo; Iterum has no processor -> controller message | **Confirmed for the controller side that was read**: the controller reads no tempo and the custom DSP path does not use it. Not grepped for an `IMessage` sender in the processor (the claim about the lack of any message is the main loop's). | PROC:699-742, DSP:852-868 |
| S6 | Three different "default" tap positions exist (add, double-click, Reset) | **Confirmed and understated: five** (adds the parameter default `(i+1)/17` and Copy `(i+1)/n`). | M3 |
| S7 | Shrinking then growing the tap count overwrites placed taps | **Confirmed**, also in non-Custom patterns and on preset load (H2). | M2, H2 |
| S8 | Taps at ratio 0 and 1 draw half clipped | **Confirmed.** | M7 |
| S9 | No readout of time or level while dragging; ruler has no labels | **Confirmed.** Nothing in `draw` prints ms or dB; only the static "100%"/"0%". | TPE:28-36, 143-159 |
| S10 | Level axis is linear gain; useful levels crowd the top third | **Confirmed.** 0.667..1.0 = 0 to -3.5 dB; below 0.001 jumps from -60 to -96 dB. | section 0 |
| S11 | Colors and fonts hard-coded, not the design system's tokens | **Confirmed.** | M8 |
| S12 | Adding a tap writes parameters without a begin/end edit gesture | **Confirmed, and broader**: every editor path, drag included (H4). | H4 |
| S13 | Lower half of the panel is empty for every pattern except Custom | **Confirmed.** | L1 |
| S14 | A tap at the left edge is a 1 ms delay | **Confirmed.** | DSP:855, 167 |
| S15 | Tap numbers sit at the bar's foot, where bars overlap at low levels | **Confirmed.** | M8 |

## 5. New facts that are not in any seed

H1 (editor pointer orphaned), H2 (tap-count increase clobbers params in any pattern and on preset load), H3 (Copy
tap count decoded with `toPlain` on a plain Parameter), H5 (level-0 tap lost), M5 (morph into Custom heads for
`n * baseTime`), M6 (Copy does not match the DSP), M9 (view never refreshed from 950-981), L2, L3. H1, H3 and M5 are
READ-ONLY CLAIMS and need a probe before the design relies on them.

## 6. What a redesign must keep (from this reading)

- 16 time params (ratio of 5000 ms, 950-965), 16 level params (linear gain 0..1, 966-981), Tap Count (902), Snap
  (913), Timing Pattern index 19 = Custom. A saved Custom must mean the same milliseconds and linear gain after the
  change, because the stream stores raw ratios with no version field (MTP:393-415).
- A mapping change on the level axis (for example dB) is a display mapping over the same stored linear gain, or it has
  to say exactly how old values move.
- The only compatible way to make "Snap" tempo-aware without touching the stream is for the view to know the tempo,
  which today it cannot (no message from the processor); a Snap meaning change would also change the meaning of a saved
  Snap index only for the grid, not for stored tap positions.
