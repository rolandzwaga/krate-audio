# Research: Step Pattern Editor

**Feature Branch**: `046-step-pattern-editor`
**Date**: 2026-02-09

## R1: VSTGUI CControl Custom View Pattern for Self-Contained Views

**Decision**: Extend `VSTGUI::CControl` for the StepPatternEditor, following the same pattern as `TapPatternEditor` (Iterum) and `ArcKnob` (shared).

**Rationale**:
- CControl provides `beginEdit()`/`endEdit()` for host undo integration (FR-011), `setValue()`/`getValue()` for parameter binding, and `IControlListener` for value change notifications.
- CControl inherits from CView, providing `draw()`, `onMouseDown()`, `onMouseMoved()`, `onMouseUp()`, `onMouseCancel()`, `onKeyDown()`, and `setDirty()` -- all required for the step editor.
- The existing TapPatternEditor in Iterum (`plugins/iterum/src/ui/tap_pattern_editor.h`) demonstrates this exact pattern with drag state, pre-drag values for cancellation, and `ParameterCallback` for multi-parameter updates from a single CControl.
- CView (without CControl) would lack `beginEdit()`/`endEdit()` and the host parameter binding mechanism.

**Alternatives Considered**:
- `CViewContainer` with child CControls (one per step): Rejected because 32 separate parameter-bound controls creates excessive overhead, complicates paint-mode drag across steps, and makes single beginEdit/endEdit per gesture (FR-011) difficult.
- `CView` (non-control): Rejected because it lacks `beginEdit()`/`endEdit()` integration for host undo.

## R2: Multi-Parameter Updates from Single CControl

**Decision**: Use a `ParameterCallback` pattern (same as TapPatternEditor) where the StepPatternEditor uses a single control tag (-1 or a sentinel) but communicates individual step parameter changes via a callback function. The controller wires this callback to call `beginEdit()`/`performEdit()`/`endEdit()` on the appropriate per-step parameter IDs.

**Rationale**:
- CControl natively binds to ONE parameter via its tag. We need to control 32+ parameters (step levels, plus step count, euclidean hits, etc.).
- The TapPatternEditor solves this identically: `setParameterCallback(ParameterCallback cb)` where callback receives `(ParamID, float normalizedValue)`.
- The controller's `verifyView()` or `didOpen()` hooks wire the callback to `performEdit()`.
- beginEdit/endEdit pairs are managed by the view's drag logic: one `beginEdit` at gesture start, individual `performEdit` per step during drag, one `endEdit` at gesture end (FR-011).

**Alternatives Considered**:
- Sub-controller with `getTagForName()` remapping: Over-engineered for a single view that manages its own parameters internally.
- Direct `EditController` pointer in the view: Violates separation and makes the shared component plugin-dependent (contradicts FR-037).

## R3: Timer Pattern for Playback Position Updates

**Decision**: Use `VSTGUI::CVSTGUITimer` with `VSTGUI::SharedPointer` at ~30fps (33ms interval), following the SpectrumDisplay pattern in Disrumpo.

**Rationale**:
- SpectrumDisplay (`plugins/disrumpo/src/controller/views/spectrum_display.h:239`) uses `VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> analysisTimer_` with 33ms interval for ~30fps updates.
- Timer creation: `analysisTimer_ = VSTGUI::makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) { ... }, 33);`
- Timer cleanup: `analysisTimer_ = nullptr;` (SharedPointer releases automatically)
- Timer starts when transport is playing, stops when transport stops (FR-026).
- The timer callback calls `invalid()` to trigger redraw only when needed.

**Alternatives Considered**:
- `CFrame::registerAnimator()`: More complex, designed for property animations rather than periodic data polling.
- Host-driven refresh via parameter changes: Insufficient because playback position updates at audio rate, not parameter rate.

## R4: Processor-to-Controller Communication for Playback State

**Decision**: Use `IMessage` for transport state (playing/stopped) and current step position, sent from Processor to Controller. The StepPatternEditor receives this data through a setter API called by the controller.

**Rationale**:
- Constitution Principle I mandates IMessage for processor-controller communication.
- The Ruinae processor currently has no IMessage usage, so this will be the first implementation.
- Message pattern: Processor sends `"TranceGatePlayback"` message containing `int32 currentStep` and `int32 isPlaying` in `processOutputParameterChanges()` or a timer-based approach.
- Controller receives in `notify()` override and forwards to StepPatternEditor via `setPlaybackStep(int step)` and `setPlaying(bool playing)`.
- Alternative: Use output-only parameters (kIsOutput flag) for currentStep. This is simpler and more idiomatic for VST3.

**Alternatives Considered**:
- Shared atomic variables: Violates Constitution Principle I (processor/controller separation).
- SPSC queue: More complex than IMessage and requires custom lifetime management.
- Output parameters: Simpler than IMessage but limits step position to normalized 0-1 range. Could work with integer parameter (0-31).

## R5: Color Utility Extraction

**Decision**: Extract `lerpColor`, `darkenColor`, and `brightenColor` into a shared header `plugins/shared/src/ui/color_utils.h` in namespace `Krate::Plugins`.

**Rationale**:
- `lerpColor` is currently duplicated identically in both `arc_knob.h:149-157` and `fieldset_container.h:134-142`.
- `darkenColor` exists in `arc_knob.h:139-146`.
- `brightenColor` exists in `fieldset_container.h:124-131`.
- The StepPatternEditor will need all three (gradient bars, accent highlighting, grid dimming).
- Extracting prevents further ODR risk from yet another copy.
- Header-only with `inline` functions, no .cpp needed.

**Alternatives Considered**:
- Keep duplicated per-view: More ODR risk as views multiply. Rejected.
- Move to DSP layer: These are UI-specific utilities (VSTGUI CColor), not DSP. Rejected.

## R6: Parameter ID Allocation for Step Levels

**Decision**: Use IDs 668-699 for the 32 step level parameters, within the existing TranceGate range (600-699). Use ID 608 for Euclidean enabled, 609 for Euclidean hits, 610 for Euclidean rotation, 611 for phase offset.

**Rationale**:
- Spec explicitly states IDs 668-699 for step levels.
- Current TranceGate parameters use 600-607 (8 IDs used).
- Remaining IDs 608-667 are available for new control parameters (Euclidean mode, phase offset, etc.).
- The 32 contiguous step level IDs enable simple offset-based addressing: `kTranceGateStepLevel0Id + stepIndex`.

**Layout**:
```
600: kTranceGateEnabledId (existing)
601: kTranceGateNumStepsId (existing, changed from dropdown to integer 2-32)
602: kTranceGateRateId (existing)
603: kTranceGateDepthId (existing)
604: kTranceGateAttackId (existing)
605: kTranceGateReleaseId (existing)
606: kTranceGateTempoSyncId (existing)
607: kTranceGateNoteValueId (existing)
608: kTranceGateEuclideanEnabledId (new)
609: kTranceGateEuclideanHitsId (new)
610: kTranceGateEuclideanRotationId (new)
611: kTranceGatePhaseOffsetId (new)
668-699: kTranceGateStepLevel0Id through kTranceGateStepLevel31Id (new, 32 IDs)
```

## R7: NumSteps Parameter Change from Dropdown to Integer

**Decision**: Change `kTranceGateNumStepsId` from a 3-value dropdown (8, 16, 32) to an integer parameter with range 2-32, default 16.

**Rationale**:
- Spec FR-015 requires continuous 2-32 step support.
- Current implementation uses `StringListParameter` with 3 values and `numStepsFromIndex()` lookup.
- New implementation: `RangeParameter` with min=2, max=32, stepCount=30, default=16.
- Processor denormalization: `int steps = 2 + static_cast<int>(normalized * 30.0 + 0.5)`.
- Preset migration: Old normalized values (0.0=8, 0.5=16, 1.0=32) need to be mapped to new range during state loading. This is handled by the versioned state persistence system.

**Alternatives Considered**:
- Keep dropdown, add more values: Cluttered UI for 31 items. Rejected.
- Free-form text entry: Not standard VSTGUI pattern. Rejected.

## R8: Shared Component Architecture

**Decision**: Place StepPatternEditor in `plugins/shared/src/ui/step_pattern_editor.h` as a header-only shared component, registered via ViewCreator pattern.

**Rationale**:
- FR-037 requires the editor to be a shared component with no plugin-specific dependencies.
- ArcKnob and FieldsetContainer demonstrate the pattern: header-only, namespace `Krate::Plugins`, `inline` ViewCreator registration variable.
- The ViewCreator registers the view as `"StepPatternEditor"` with configurable color attributes.
- Any plugin includes the header in its `entry.cpp` to register the view type.
- The controller wires the `ParameterCallback` to its own parameter IDs, making the view plugin-agnostic.

## R9: Euclidean Pattern Integration

**Decision**: Reuse `Krate::DSP::EuclideanPattern` directly from the shared DSP library (Layer 0).

**Rationale**:
- `EuclideanPattern` (`dsp/include/krate/dsp/core/euclidean_pattern.h`) provides exactly what we need: `generate(pulses, steps, rotation)` returning a bitmask, and `isHit(pattern, position, steps)` for O(1) lookup.
- All methods are `static constexpr noexcept` -- safe to call from UI thread.
- kMinSteps=2, kMaxSteps=32 match our requirements exactly.
- The UI can include DSP Layer 0 headers because they are pure computation with no audio-thread dependencies.

**Alternatives Considered**:
- Re-implement Bjorklund in UI code: Duplicates existing tested code, ODR risk. Rejected.
- Generate on processor side, send via message: Unnecessary complexity for a pure mathematical function. Rejected.

## R10: View Size and Layout Zones

**Decision**: The StepPatternEditor divides its view rectangle into functional zones.

**Rationale**:
- Minimum size: 350x180 pixels (spec edge case: 350px width with 32 steps = ~10px per bar).
- Default recommended size: 500x200 pixels.

**Layout zones** (top to bottom):
```
+----------------------------------------------+
| Phase offset indicator row (12px)            |
+----------------------------------------------+
|                                              |
| Step bars area (main editing zone)           |
| - Grid lines at 0.25, 0.50, 0.75           |
| - Color-coded bars                          |
|                                              |
+----------------------------------------------+
| Euclidean dot indicators (10px, when active) |
+----------------------------------------------+
| Playback position indicator (8px)            |
+----------------------------------------------+
| Step number labels (12px)                    |
+----------------------------------------------+
| Quick action buttons row (20px)              |
+----------------------------------------------+
| Euclidean controls row (20px, when active)   |
+----------------------------------------------+
| Zoom/scroll indicator (6px, when >24 steps)  |
+----------------------------------------------+
```

## R11: External Controls Architecture (Quick Actions, Euclidean Controls)

**Decision**: ~~REVISED~~ Quick action buttons and Euclidean controls are **separate standard VSTGUI controls** (CTextButton, COnOffButton, etc.) in the parent container, per the roadmap's component boundary breakdown. The StepPatternEditor is a focused CControl that only renders bars, dots, indicators, and handles mouse interaction for step editing.

**Rationale** (updated to match roadmap architecture):
- The roadmap's component boundary breakdown (section 1.13) explicitly shows the Euclidean toolbar and quick action buttons as separate rows of standard VSTGUI controls outside the StepPatternEditor.
- This keeps the StepPatternEditor simple, reusable, and focused on its core responsibility (visual pattern editing).
- The editor exposes public API methods (`applyPresetAll()`, `regenerateEuclidean()`, etc.) that external buttons call via controller wiring.
- Standard VSTGUI controls benefit from built-in accessibility, theming, and uidesc editor support.
- The CControl base class is preserved (no need for CViewContainer), maintaining beginEdit/endEdit semantics for step editing.

**Alternatives Considered**:
- Integrated hit regions within the view: Simpler but contradicts the roadmap's explicit component boundaries. Makes the editor less reusable and harder to theme. Rejected.
- CViewContainer with child CControls: Loses CControl semantics for step editing. Rejected.

## R12: Zoom and Scroll Implementation (P3)

**Decision**: Implement zoom/scroll as internal state within StepPatternEditor, using a visible range (startStep, visibleStepCount) that adjusts bar rendering.

**Rationale**:
- FR-032/FR-033/FR-034 require zoom/scroll only when step count >= 24.
- Mouse wheel scrolls horizontally when zoomed in.
- Ctrl+mouse wheel zooms in/out.
- Default: all steps visible (fit-all mode).
- A thin scroll indicator bar shows the visible portion relative to the full pattern.
- No external scrollbar needed -- self-contained within the view.

## R13: beginEdit/endEdit Gesture Management

**Decision**: For paint-mode drag across multiple steps (FR-011), issue one `beginEdit()` per affected parameter at drag start, individual `performEdit()` calls during drag, and one `endEdit()` per parameter at gesture end.

**Rationale**:
- VST3 requires `beginEdit()`/`endEdit()` pairs for host undo grouping.
- Paint mode can affect many steps in one gesture. The view must track which parameters were modified during the gesture.
- Implementation: `std::bitset<32> dirtySteps_` tracks which steps were touched during the current gesture. At gesture start, `beginEdit()` is called for each step as it is first touched. At gesture end, `endEdit()` is called for all dirty steps.
- Pre-drag values: `std::array<float, 32> preDragLevels_` stores levels at gesture start for Escape cancellation (FR-010).

**Alternatives Considered**:
- Single beginEdit/endEdit on a "pattern" parameter: VST3 doesn't support this for 32 independent parameters. Rejected.
- Begin/end all 32 at gesture start: Wasteful and may confuse hosts that track which parameters were actually edited. Rejected.

## R14: Random Number Generation for UI

**Decision**: Use `<random>` library with `std::mt19937` seeded from `std::random_device` for the "Random" quick action (FR-031).

**Rationale**:
- FR-031 explicitly requires a controller-thread random source, not the DSP-layer generator.
- `std::mt19937` with `std::uniform_real_distribution<float>(0.0f, 1.0f)` provides good quality random values.
- Seed once on view construction from `std::random_device`.
- This runs on the UI thread, no real-time safety concerns.

## R15: Header-Only vs .h/.cpp Split

**Decision**: Use header-only implementation for StepPatternEditor, following ArcKnob and FieldsetContainer patterns.

**Rationale**:
- ArcKnob (466 lines) and FieldsetContainer (461 lines) are both header-only with `inline` ViewCreator registration.
- StepPatternEditor will be larger (~800-1200 lines estimated) but still manageable as header-only.
- Header-only simplifies build system integration -- just include in entry.cpp.
- If the file grows beyond ~1500 lines during implementation, it can be split into header + cpp at that point.

**Alternatives Considered**:
- Split into .h + .cpp from the start: Would require adding the .cpp to KratePluginsShared CMakeLists.txt and managing include paths. More effort for marginal benefit at this size. Can be done later if needed.
