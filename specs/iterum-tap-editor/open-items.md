# Open items: Iterum tap editor design

User rulings of 2026-10-29 (given as multiple choice, the recommended option taken in every case), followed by the
seven challenge findings the design stage left unresolved after two revisions. The reconcile stage resolves every
item in design.md and appends "Resolved: <section(s)>" (or "Open: <reason>") under each one here.

## Rulings on design.md section 9

1. **Default Length of a fresh instance: A, `1/1` (synced).** Matches the release's rule that every tempo-capable
   mode starts synced. Saved states are unaffected (7.2).

   Resolved: design.md 9 R1; 2.1, 7.2, 7.4, row 12.
2. **Tempo transport: A, the hidden read-only output parameter 915.** No DataExchange block.

   Resolved: design.md 9 R2; 2.4 (with the synchronous-delivery correction of item 4), rows 2, 3, 4, 6.
3. **Registered Custom Time defaults: C.** Taps 1-4 at 0.25, 0.5, 0.75, 1.0; taps 5-8 at the odd eighths (0.125,
   0.375, 0.625, 0.875); taps 9-16 at the odd sixteenths. A fresh 4-tap pattern equals Reset, a fresh 8 is every
   eighth, a fresh 16 every sixteenth, all distinct. The index-order cost (Cascade pan and Decaying levels go by
   index, so they no longer follow time order) is accepted and must be stated honestly in 3.1 and 9.

   Resolved: design.md 9 R3; section 1 (P1, P5, 'Left alone'), 3.1 (table, shared `kDefaultCustomTimeRatios`, the
   cost for C and A), 3.3 (worked examples), 3.4 (double-click examples), 7.2, 7.4, rows 3, 8, 9, 11, 12. The cost
   is stated as found in the source: the index-based pans (Cascade, Alternating, Widening Stereo) are affected;
   Decaying Level goes by index too but never reaches a Custom pattern, whose custom levels override it every block.
4. **Length menu: A.** Free, 1/8, 1/4, 1/2, 1/1, 2/1. No 4/1.

   Resolved: design.md 9 R4; 2.1 (already this set, now attributed).
5. **Per-tap reset gesture (from unresolved item 3 below): Ctrl/Cmd-click resets the tap's level to 0 dB** (bracketed
   begin / perform / end on the level id, intercepted ahead of CControl's default-value listener); **double-click keeps
   the time-and-level move of 3.4** through placeTap. Both stated in 4.2 and 3.4, with the view cases the finding asks for.

   Resolved: design.md 9 R5; P16 row, 3.4, 3.5 (Ctrl/Cmd-click row), 4.2 (`dispatchEvent` override), 5.9, rows 9
   and 12.

## Unresolved challenge findings

Ruling for every item below: **accept the challenger's suggested fix as written**, unless the ruling line under the
item says otherwise. Each needs its sections, implementation rows and Catch2 cases in design.md updated.

### Item 1

[challenge, unresolved after 2 revisions] design.md 3.1, 2.2, 4.2, 9 Q3 (the recommended option A): With the design's choice, a fresh instance contradicts its own default grid. The taps sit at (i+1)/16 (125, 250, 375, 500 ms at 1/1 and 120 BPM: a cluster in the first beat, with 3/4 of the plot empty), while Snap defaults to 1/4. The snap lines are {0.25, 0.5, 0.75, 1.0}, ratio 0 is never a line, and a drag snaps to the nearest line, so the first real move of tap 1, 2 or 3 (any target below 0.375) lands on 0.25, on top of tap 4. That recreates the stacking P1 set out to remove, in the out-of-box state. Double-click on tap 1 sends it to 0.5, past taps 2-4 (the design's own example in 3.4). OQ3 rejects option C because 'tap numbers no longer run left to right', but option A has the same cost. The design's own 3.3 example (Reset at 4 taps, then 4 -> 8) gives the time order 1, 5, 6, 7, 2, 3, 8, 4: three revealed taps squeezed onto off-grid sixteenths between beats 1 and 2. A stored value that is free is kept even when it is off the active snap grid. Cascade pan, and the order in which lowering Taps removes taps, then jump around in time either way. (suggested: Make C the design's recommendation in OQ3: 1-4 on quarters (fresh = Reset), 5-8 on the odd eighths, 9-16 on the odd sixteenths. This puts every fresh and revealed tap on the default 1/4 or a finer grid, and the 3.3 example becomes evenly spaced eighths. Restate the index-order cost honestly for both A and C. Or keep A but say plainly that the first snapped drag of a fresh tap stacks it on tap 4, and let the user rule on that. Either way, add a view case: 'fresh pattern, Snap 1/4: a 3 px body drag of tap 1 does not land on tap 4's ratio', or document that it does.)

Ruling: resolved by ruling 3 above (option C). Restate the index-order cost for C in 3.1 and 9, make the 3.3 example evenly spaced eighths, and add the view case "fresh pattern, Snap 1/4: a 3 px body drag of tap 1 does not land on tap 4's ratio".

Resolved: design.md 9 F1 (with R3); 3.1, 3.3, row 8 ("the registered defaults reveal without a move"), row 9 ("fresh pattern, Snap 1/4: a 3 px body drag of tap 1 does not land on tap 4's ratio").

### Item 2

[challenge, unresolved after 2 revisions] design.md 4.3 (cursor), compared with tap_pattern_editor.cpp:495-498 and critique.md 'What is fine': Today the body shows kCursorHSize, the one cue that a body drag moves time only, and the critique names it as something that works. The design swaps it for kCursorHand on bodies and grips. A hand says 'grab and move anywhere'. The new split (body = time only, handle = level only) goes against the prior-art convention that a vertical drag sets level (PA table: Logic, FabFilter, Output, SuperTap, frctl). With the hand cursor, a producer who grabs a bar and pulls it up gets no level change and no hint why. On this point the editor is less discoverable than today. (suggested: Keep kCursorHSize over a body and kCursorVSize over a handle. Use kCursorSizeAll over a grip (short or silent tap), whose axis is chosen by the first move. Add a case in the view test that checks the cursor per zone, or at least name it in the visual review.)

Ruling: accept. kCursorHSize over a body, kCursorVSize over a handle, kCursorSizeAll over a grip; a view case checks the cursor per zone.

Resolved: design.md 9 F2; 4.3 (`cursorForZone`), row 9 (`cursorForZone`, `hitZoneAt`, case "the cursor per zone").

### Item 3

[challenge, unresolved after 2 revisions] design.md 4.2 'Ctrl / Cmd-click' and 3.4 double-click: The design says Ctrl/Cmd-click 'cannot be made to' do anything. That is false. CView::dispatchEvent is virtual (cview.h:96), and it calls the view-event listeners with forEachReverse (cview.cpp:666-675). CControl registers its default-value listener in its constructor (ccontrol.cpp:68-72), so a listener the TapPatternEditor registers in its own constructor runs first and can take the Ctrl/Cmd-click; overriding dispatchEvent also works. Ctrl/Cmd-click is the reset gesture on every other Iterum control (kDefaultValueModifier), and Option-click is Logic's (PA M3). The design brings in the CSlider convention for Shift but leaves this one dead. The only per-tap reset is a double-click, which also moves the tap in time to an index-based position that can lie far from where it was (tap 1 -> 0.5). A quick double-click while grabbing a tap therefore teleports it. (suggested: Correct the premise in 4.2. Make Ctrl/Cmd-click on a tap reset its level to 0 dB (bracketed begin / perform / end on the level id), intercepted ahead of CControl's listener. Then either limit double-click to the same level reset, or keep the time move of 3.4 on double-click only and say so. Add view cases: 'Ctrl-click on tap i writes level 1.0 only' and 'Ctrl-click never reaches CControl's default-value path'.)

Ruling: resolved by ruling 5 above. Correct the premise in 4.2 (dispatchEvent is virtual; a listener registered in the view's constructor runs before CControl's).

Resolved: design.md 9 F3 (with R5); 4.2 (premise corrected from cview.h:96, cview.cpp:666-675, ccontrol.cpp:68-72; the view overrides `dispatchEvent`), 3.5, row 9 (cases "Ctrl-click on tap i writes level 1.0 only", "Ctrl-click never reaches CControl's default-value path", "Ctrl-click in read-only mode writes nothing and is consumed").

### Item 4

[challenge, unresolved after 2 revisions] design.md 3.6 (bridge, bulk loads), 2.4 last paragraph, row 6 case 'setParamNormalized does not touch the view before the deferred update': The design rests on the claim that a Parameter change reaches `TapEditorParamBridge::update` later, through the SDK's deferred updates on the UI thread. The SDK does not do that. `EditController::setParamNormalized` calls `Parameter::setNormalized` (vsteditcontroller.cpp:146-154). That calls `FObject::changed()` (vstparameters.cpp:73-77), which calls `UpdateHandler::triggerUpdates` (fobject.cpp:196-202). triggerUpdates runs every dependent's update synchronously, on the calling thread, with no thread check (updatehandler.cpp:352-420). Only `deferUpdate()` queues. So: (a) the row 6 case 'setParamNormalized does not touch the view before the deferred update' cannot pass, because the view changes inside the call. (b) 'setParamNormalized no longer touches the view at all' is false: it touches the view through the bridge. (c) 'a bulk load is delivered to the view after it' is false: setComponentState pushes about 40 per-id updates into the view while bulkParamLoad_ is set, along with 5 read-only recomputes. (d) 2.4's 'the bridge of 3.6 moves the view update to the UI thread' is false. The same wrong premise is in .claude/skills/vst-guide/THREAD-SAFETY.md:121, which the design cites. (suggested: Choose one option and state it. (1) Rely on the VST3 threading contract: setParamNormalized is marked [UI-thread & Connected] in ivsteditcontroller.h. Then state that delivery is synchronous. Skip the bridge's per-id pushes while bulkParamLoad_ is set and do one full push from syncAllViews, keeping those lines but rewritten to use tapEditor(). Replace the row 6 case with 'a set of 950 or 966 is reflected in the view immediately' and add 'a bulk load pushes once, after the load'. (2) Build real deferral: the bridge records dirty ids in an atomic bitmask and flushes it on the UI thread from a CVSTGUITimer or the editor's idle update. Keep the row 6 case only under option 2.)

Ruling: accept option (1): rely on the VST3 threading contract, state that delivery is synchronous, skip per-id pushes while bulkParamLoad_ is set and do one full push from syncAllViews via tapEditor(); replace the row 6 case as suggested. Also correct .claude/skills/vst-guide/THREAD-SAFETY.md:121 in the build stage (name it in an implementation row).

Resolved: design.md 9 F4; P12 row, 2.4, 3.3, 3.6 (bridge and bulk-load bullets), row 6 (`isBulkParamLoad`, `pushToTapEditor` / `pushAllToTapEditor`, `syncAllViews` rewritten; cases "a set of 950 or 966 is reflected in the view immediately" and "a bulk load pushes once, after the load"), row 6a (THREAD-SAFETY.md:121).

### Item 5

[challenge, unresolved after 2 revisions] design.md 2.6 (patternTap, Linear bullet, mirror test claim), row 5 mirror test: `patternTap` returns beats and takes no tempo, but the DSP's timing for two pattern groups depends on clamps applied in milliseconds. Linear: `applyLinearSpreadPattern` spreads the taps evenly from B to maxTime = min(B x n, 5000) (multi_tap_delay.h:835-848). It does not clamp each tap to 5 s. The design's 'the copy ignores [the clamp] and the 5 s clamp re-applies' is therefore wrong. Example: Linear, Note Value Quarter, 16 taps, 90 BPM: the DSP plays tap 2 at 666.7 + 4333/15 = 955.6 ms, the formula gives 1333 ms. The row 5 mirror test runs exactly this combination (Quarter, 16 taps, 90 BPM), so it goes red against the design's own formula. Second, `baseTimeMs_` is clamped to [1, 5000] ms before the ratios are applied (multi_tap_delay.h:628-635). With Note Value Whole below 48 BPM, Primes tap 1 plays 5000 x 0.5 = 2500 ms and the Fibonacci taps play fib x 0.25 x 5000, while `B_beats x ratio` clamped gives 5000 ms. Copy and the read-only display then show a different pattern from the one that plays. The mirror test (90/140 BPM, Quarter/Eighth) never reaches this case. (suggested: Give `patternTap(..., bpm)` the tempo and compute ms exactly as the DSP does: clamp B to [1, 5000] ms first, and use Linear's maxTime = min(B x n, 5000). Convert to beats only to choose a synced Length, and only when no clamp is engaged at that tempo; otherwise copy to Free. Add mirror cases for Linear with 16 taps at 90 BPM and for Note Value Whole at 30 BPM (Primes, Fibonacci, Linear).)

Ruling: accept. patternTap takes the tempo and computes milliseconds exactly as the DSP clamps them; synced Length only when no clamp engages; the extra mirror cases.

Resolved: design.md 9 F5; P11 row, 2.6 (signatures, formulas with DSP lines, Copy step 3, examples), 5.9, row 5 (mirror test against `patternTap(...).ms`; cases for Linear 16 taps at 90 BPM and Note Value Whole at 30 BPM for Primes, Fibonacci, Linear), row 6 (Linear Copy case).

### Item 6

[challenge, unresolved after 2 revisions] design.md row 4 (multitap_tempo_output_test.cpp: processor tempo send, state round-trip, morph through Processor::process): All four row 4 cases construct and drive `Iterum::Processor`, but plugin_tests does not compile processor.cpp. plugins/iterum/tests/CMakeLists.txt lists controller.cpp, custom_views.cpp, delay_time_sync_controller.cpp, tap_pattern_editor.cpp and memorystream.cpp, but no processor.cpp, and no test in plugins/iterum/tests includes processor/processor.h. The plan names only processdata.cpp and parameterchanges.cpp as additions, so the cases do not link as written. The row also needs a test accessor for the processor's MultiTapDelay. The Processor object is large (every mode's DSP; MultiTapDelay alone has two 8192-float dry buffers), so a stack-allocated Processor in a Catch2 case risks the known stack-overflow 'Unknown exception'. (suggested: Add `${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor.cpp` to plugin_tests in row 4. Name the accessor, for example a `const MultiTapDelay& multiTapDelayForTest() const`. Require the test to heap-allocate the processor (`owned(new Processor)`), then call initialize, setupProcessing and setActive(true). Note that the processor test TU becomes a consumer of processor.cpp for gate scoping.)

Ruling: accept. processor.cpp compiled into plugin_tests, a const multiTapDelayForTest() accessor, heap-allocated Processor (owned(new Processor)), initialize + setupProcessing + setActive(true); note the TU as a consumer of processor.cpp for gate scoping.

Resolved: design.md 9 F6; row 4 (CMake line, `multiTapDelayForTest()`, heap-allocated processor and bring-up), section 8 gate paragraph (consumer of processor.cpp / processor.h).

### Item 7

[challenge, unresolved after 2 revisions] design.md section 6 (rows 8/8a 'unchanged', 'always visible' fieldset): The Copy caption 'Pattern' (multitap.xsl:148-149, editor.uidesc:598) has `transparent="true"`. That was fine only because the CUSTOM PATTERN fieldset is hidden whenever the caption shows. Section 6 makes the fieldset always visible. FieldsetContainer strokes its top edge at y = titleFontSize/2 + lineWidth/2, about 6 px, across the whole title line except the title gap (fieldset_container.h:107-116, 176-205). The caption box at y 4..20 sits on that line, so in 19 of 20 patterns the border runs through the top of 'Pattern'. The panel's own legend rule says title-line labels are opaque to mask the border (xsl comment at line 133; design-system 4.10). The design applies that rule to the new labels 7e and 7a but leaves the caption unchanged. (suggested: In the section 6 table and the XSLT step, give the Copy caption `transparent="false" back-color="panel"` (kept on tag 921). Confirm that CopyPatternButton paints an opaque fill over the border. Name the title line explicitly in the visual review of the preset-*.png shots.)

Ruling: accept. Copy caption transparent="false" back-color="panel" on tag 921; confirm CopyPatternButton paints an opaque fill; name the title line in the visual review of the preset shots.

Resolved: design.md 9 F7; section 6 (rows 8 and 8a, title-line paragraph, XSLT step; opaque fill confirmed from custom_views.h:34-45 and cbuttons.cpp:949-951), rows 7, 10 (case "the Copy caption masks the fieldset border"), 11 (visual review note).

## Implementability-check findings after reconcile (2026-10-29)

### Check 1

7.1 named the controller call sites for the Length tail but no section 8 row implemented them: an old project would play Free while the controller showed 1/1.

Ruling: accept. Resolved: 7.1 (wrapper named), row 3 (`syncMultiTapLengthParamsToController`, controller-sync case extended), row 6 (both call sites after CTL:299 and CTL:1277, state case added).

### Check 2

4.4 required `getParamValueByString` to parse "750.0 ms" and "-6.0 dB" back for 950-981 but no row implemented it; a typed host string would fall through to `Parameter::fromString`.

Ruling: accept. Resolved: 4.4 (`parseMultiTapParam` contract), row 3 (parser + parse cases), row 6 (the `getParamValueByString` branch + its case).
