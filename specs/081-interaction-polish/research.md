# Research: Arpeggiator Interaction Polish (081)

**Date**: 2026-02-25
**Spec**: `specs/081-interaction-polish/spec.md`

## Research Summary

This spec is primarily a UI-layer feature (VSTGUI custom views + controller wiring) with a small processor-to-controller IMessage extension for skip events. No new DSP algorithms are introduced. All engine features (Euclidean, Spice/Dice, Humanize, Fill, Ratchet Swing) already exist from Phases 7-9.

---

## R-001: Playhead Trail Rendering Strategy

**Decision**: Per-lane trail buffer (fixed-size array of 4 step indices with alpha levels), updated by a single shared CVSTGUITimer at ~30fps owned by the controller.

**Rationale**: The existing playhead mechanism uses per-lane playhead parameters (e.g., `kArpVelocityPlayheadId = 3294`) polled by the controller. A trail simply requires remembering the previous 2-3 playhead positions per lane. A shared timer (one for all 6 lanes) is more efficient than per-lane timers. The timer fires, reads all 6 playhead parameters, shifts the trail buffer, and invalidates dirty lanes.

**Alternatives considered**:
- Per-lane timer: Rejected; 6 timers unnecessary when one suffices
- Push-based from processor via IMessage: Rejected; the polling mechanism already works for playhead position
- Storing trail in StepPatternEditor base class: Rejected; trail is arp-specific and should live in IArpLane extensions

**Implementation approach**:
- Add `trailSteps_[4]` array and `trailAlphas_[4]` array to each lane class (or via a `PlayheadTrailState` helper struct)
- Trail alpha levels: index 0 = current step (full accent alpha ~40), index 1 = 60% alpha, index 2 = 35% alpha, index 3 = 15% alpha
- On each timer tick: shift trail, insert new playhead position at index 0, mark dirty
- On transport stop: clear all trail entries and skipped-step overlays
- Trail wraps correctly because it stores absolute step indices, not offsets

---

## R-002: Skip Event IMessage Architecture

**Decision**: Pre-allocate 6 IMessage instances at processor initialization (one per lane index 0-5). On skip events, set the "lane" and "step" int attributes and send. Controller receives in `notify()` and updates the UI.

**Rationale**: The spec explicitly requires no audio-thread allocation. Pre-allocating IMessage instances follows the existing pattern in the Ruinae processor (see `processor.cpp:383-447` for existing IMessage allocation in `initialize()`). The VST3 IMessage mechanism supports the expected message rate (~100-200 messages/second at high BPM with ratchets).

**Alternatives considered**:
- Lock-free ring buffer with timer polling: More complex, not needed given IMessage capacity
- Parameter-based encoding (like playhead): Would need 6 extra parameters and the skip is a transient event, not a continuous value
- Shared atomic array: Would require polling on UI side; IMessage is push-based and lower latency

**Implementation approach**:
- Processor: In `initialize()`, allocate 6 IMessage instances (one per lane) with message ID "ArpSkipEvent"
- Processor: In the arp engine step evaluation, when a step is skipped, set the lane/step attributes on the pre-allocated message and call `sendMessage()`
- Processor: Only send skip messages when `editorOpen_` flag is true (set via IMessage from controller on didOpen/willClose)
- Controller: In `notify()`, handle "ArpSkipEvent" messages, extract lane/step, update the corresponding lane's skip overlay state
- Note: The IMessage pre-allocation pattern is already established in the processor -- verify if `allocateMessage()` in `initialize()` is safe or if we should allocate in `setupProcessing()`

**Critical detail**: The arpeggiator core engine needs to expose skip information. Currently `ArpeggiatorCore::processBlock()` returns ArpEvent structs. We need to verify whether the engine already reports skips or if we need to add a `kSkip` event type.

---

## R-003: IArpLane Interface Extension Strategy

**Decision**: Add `setTrailState()`, `setSkippedStep()`, `clearOverlays()`, and transform callback methods to the `IArpLane` interface. Add `setTransformCallback()` for header button wiring.

**Rationale**: The IArpLane interface is the polymorphic lane management contract used by ArpLaneContainer. All 6 lane types implement it. Adding trail and skip overlay methods here ensures uniform behavior across all lane types. Transform buttons live in the shared ArpLaneHeader, so the callback wiring is natural.

**Alternatives considered**:
- Separate IPlayheadTrail interface: Rejected; all lanes need trail support, so it belongs in IArpLane
- Mixin class via CRTP: Rejected; adds complexity without benefit since all 6 lanes already implement IArpLane

---

## R-004: Transform Button Placement in ArpLaneHeader

**Decision**: Add 4 ActionButton instances (or their equivalent drawing) to the right side of ArpLaneHeader, between the length dropdown and the right edge.

**Rationale**: ArpLaneHeader is a non-CView helper class used by composition in all 6 lane types. It already handles the collapse triangle (left) and length dropdown (center-left). Adding transform buttons on the right side follows the same delegation pattern: the header draws the buttons and handles click detection, then fires callbacks.

**Implementation approach**:
- ArpLaneHeader does NOT use actual ActionButton CControl instances (it is not a CView container). Instead, it draws small icon glyphs directly using CGraphicsPath (matching ActionButton's rendering style).
- Alternatively, the header could store 4 ActionButton* created by the owning lane and positioned within the header rect. However, since ArpLaneHeader is a helper class (not a CView), the drawing approach is simpler.
- **Chosen approach**: Direct drawing in ArpLaneHeader with hit-testing in handleMouseDown(). Fire transform callbacks: `onInvert()`, `onShiftLeft()`, `onShiftRight()`, `onRandomize()`.
- Each lane type implements the transform callbacks differently based on its value semantics (velocity mirrors around 0.5, pitch negates, etc.).

**Button layout**: Right-aligned in header, each 12x12px with 2px gap, positioned at `headerRight - 60px` to `headerRight - 4px`.

---

## R-005: Copy/Paste Architecture

**Decision**: A `LaneClipboard` struct stored as a member of the Ruinae Controller. Right-click on lane header opens COptionMenu with Copy/Paste options. Cross-type paste uses range normalization.

**Rationale**: The clipboard is scoped to the editor instance and lost on editor close. Storing it in the Controller (which outlives individual editor opens) means it persists across editor open/close cycles within a single plugin instance. COptionMenu is the standard VSTGUI cross-platform popup menu mechanism already used throughout the codebase.

**Implementation approach**:
- `LaneClipboard` struct: `std::array<float, 32> values{}; int length = 0; ArpLaneType sourceType = kVelocity; bool hasData = false;`
- For modifier lane: store bitmask as float (cast from uint8_t / 15.0f)
- For condition lane: store condition index as float (cast from uint8_t / 17.0f)
- Cross-type paste: normalize source to 0-1, then denormalize to target range
- Header adds "Copy"/"Paste" to right-click handler (separate from left-click transform buttons)

---

## R-006: EuclideanDotDisplay Design

**Decision**: Standalone CView subclass in `plugins/shared/src/ui/euclidean_dot_display.h`, registered as "EuclideanDotDisplay" via ViewCreator. Renders a circular ring of dots.

**Rationale**: The spec explicitly requires this as a standalone CView for independent testability and positioning. The existing `EuclideanPattern::generate()` and `isHit()` provide the pattern data. The linear overlay already exists in StepPatternEditor via `drawEuclideanDots()`.

**Implementation approach**:
- CView subclass (not CControl -- it is display-only, no interaction)
- Properties: `hits`, `steps`, `rotation` (integers), `accentColor`, `dotRadius`
- draw(): Calculate circle center and radius from view size. For each step 0..steps-1, compute angle = 2*PI*i/steps, position = center + radius*(cos, sin). Draw filled circle if isHit, outline if not.
- ViewCreator attributes: "hits", "steps", "rotation", "accent-color", "dot-radius"
- Controller wires the Euclidean parameter changes to update this view's properties

---

## R-007: Bottom Bar Layout Strategy

**Decision**: A CViewContainer in the editor.uidesc positioned below the ArpLaneContainer, containing all generative controls.

**Rationale**: The bottom bar is a static layout (not dynamic like the lane container). Using standard VSTGUI controls (ArcKnob, ToggleButton, ActionButton) positioned in XML is the simplest approach. The EuclideanDotDisplay is embedded within the Euclidean section of the bottom bar.

**Implementation approach**:
- Bottom bar height: ~80px as specified in the spec
- Euclidean section (left): Enable toggle + Hits knob + Steps knob + Rotation knob + EuclideanDotDisplay
- Center: Humanize knob + Spice knob + Ratchet Swing knob
- Right: Dice button (ActionButton, regen icon) + Fill toggle (ToggleButton)
- All controls bind to existing parameter IDs via control-tag in uidesc
- Euclidean section visibility: toggled by kArpEuclideanEnabledId (show/hide group)

---

## R-008: Dice Button Trigger Behavior

**Decision**: Controller sends 1.0 then 0.0 in the same beginEdit/endEdit block (as clarified in the spec).

**Rationale**: The Dice button is a momentary trigger, not a toggle. The ActionButton already fires valueChanged(1.0) on mouse up and resets to 0.0. The controller needs to wrap this in beginEdit/performEdit(1.0)/performEdit(0.0)/endEdit so the host sees a spike and the processor detects the rising edge.

**Implementation approach**:
- In the controller's `valueChanged()` handler for the Dice button's tag:
  1. `beginEdit(kArpDiceTriggerId)`
  2. `performEdit(kArpDiceTriggerId, 1.0)`
  3. `performEdit(kArpDiceTriggerId, 0.0)`
  4. `endEdit(kArpDiceTriggerId)`
- The processor's parameter handler detects the 0-to-1 transition and fires the one-shot dice variation

---

## R-009: ArpeggiatorCore Skip Event Reporting

**Decision**: Extend ArpeggiatorCore's processBlock to report skip events alongside normal ArpEvents.

**Rationale**: Currently `ArpeggiatorCore::processBlock()` returns `ArpEvent` structs for NoteOn/NoteOff. Skip events (condition failure, probability failure, rest) are evaluated inside the engine but not exposed to the caller. We need to add a skip event mechanism.

**Implementation approach**:
- Option A: Add `kSkip` to the ArpEvent::Type enum and include skip events in the output buffer
- Option B: Add a separate skip callback or output buffer
- **Chosen**: Option A (add `kSkip` type). This is the simplest extension. The ArpEvent already has `step` and `lane` fields (or equivalent). The processor iterates the event buffer and sends IMessage for kSkip events.
- Need to verify the ArpEvent struct fields to confirm step/lane are available.

---

## R-010: Color Scheme Constants

**Decision**: Codify all 6 lane accent colors and their derived variants as named constants or uidesc color definitions.

**Rationale**: The spec requires all color variants (accent, dim, fill, trail) to use lane-specific colors. Centralizing them prevents inconsistency.

**Color palette** (from existing ArpLaneEditor code):
| Lane | Accent | Hex |
|------|--------|-----|
| Velocity | Copper | #D0845C (208, 132, 92) |
| Gate | Sand | #C8A464 (200, 164, 100) |
| Pitch | Sage | #6CA8A0 (108, 168, 160) |
| Ratchet | Lavender | #9880B0 (152, 128, 176) |
| Modifier | Rose | #C0707C (192, 112, 124) |
| Condition | Slate | #7C90B0 (124, 144, 176) |

Derived variants (see `data-model.md PlayheadTrailState::kTrailAlphas` for authoritative alpha values):
- Dim (collapsed preview): accent at ~40% alpha (static, independent of trail)
- Fill (playhead): accent at ~25% alpha
- Trail step 0 (current): 160/255 alpha (~63%)
- Trail step 1: 100/255 alpha (~39%)
- Trail step 2: 55/255 alpha (~22%)
- Trail step 3: 25/255 alpha (~10%)
- Skip X: desaturated lighter variant (brightenColor(accent, 1.3) at 80% alpha)
- Bottom bar: neutral gray #606068
