# Research: Arpeggiator Layout Restructure & Lane Framework

**Feature**: 079-layout-framework
**Date**: 2026-02-24

## R-001: StepPatternEditor Extension Strategy

**Decision**: Subclass StepPatternEditor directly. ArpLaneEditor IS-A StepPatternEditor with additional lane-specific features (collapsible header, accent color derivation, value range labels, per-lane playhead parameter, miniature preview).

**Rationale**: StepPatternEditor already provides all bar-chart rendering, mouse interaction (click/drag/paint/right-click), zoom/scroll at 24+ steps, playback indicator, parameter callbacks, and ViewCreator registration. The base class has well-defined protected members (`stepLevels_`, `numSteps_`, `playbackStep_`, `isPlaying_`, colors, callbacks) and public APIs. Subclassing avoids code duplication of ~1000 lines.

**Alternatives considered**:
- Composition (wrapping a StepPatternEditor): Rejected because the draw() method needs intimate access to bar rendering internals (getBarArea, getBarRect, getColorForLevel) for the miniature preview, and mouse events would need complex forwarding.
- Forking StepPatternEditor: Rejected because it would create maintenance burden -- any future Trance Gate improvements would need dual application.

**Key insight**: StepPatternEditor members are `private`, not `protected`. ArpLaneEditor must use the public API exclusively. The existing public methods (`getBarArea()`, `getBarRect()`, `getStepFromPoint()`, `getLevelFromY()`, `getColorForLevel()`, `getVisibleStepCount()`) are sufficient for the collapsible header miniature preview and override needs. For the draw override, ArpLaneEditor can call the base `StepPatternEditor::draw()` for the bar body and then add the header on top. However, since the header region is separate from the bar area, the strategy is:
1. ArpLaneEditor owns the full view rect (header + body)
2. The body area IS the StepPatternEditor (positioned below the header)
3. ArpLaneEditor intercepts draw() to add the header and delegates the body to the base class

**Correction**: Since StepPatternEditor::draw() uses getViewSize() for its full layout, and getBarArea() computes relative to getViewSize(), the cleanest approach is NOT to override draw() directly but to compose: ArpLaneEditor is a CViewContainer that contains a StepPatternEditor instance for the body and draws its own header. However, this changes the IS-A relationship to HAS-A.

**Final decision**: Use composition (HAS-A) rather than inheritance (IS-A). ArpLaneEditor extends CViewContainer and holds a StepPatternEditor child for the body. The header is drawn by ArpLaneEditor itself. This is cleaner because:
- No need to fight StepPatternEditor's layout assumptions
- The header collapse/expand simply hides/shows the body child
- The miniature preview reads step data from the StepPatternEditor child
- Mouse events naturally route to the StepPatternEditor child via CViewContainer
- The StepPatternEditor remains unmodified

**BUT**: FR-004 explicitly says "ArpLaneEditor MUST be a subclass of StepPatternEditor". The spec mandates inheritance. We must follow the spec.

**Revised approach**: Subclass StepPatternEditor. Override draw() to:
1. If expanded: draw the header in the top 16px, then offset and delegate bar rendering to the base class methods (or reimplement the bar-area calculation to account for the header offset)
2. If collapsed: draw only the header with miniature preview

The bar area calculation in StepPatternEditor::getBarArea() uses getViewSize() -- it deducts fixed heights for phase offset, step labels, playback indicator, etc. ArpLaneEditor must override getBarArea() to also deduct the 16px header height. Since getBarArea() is public but NOT virtual, ArpLaneEditor cannot polymorphically override it. However, since ArpLaneEditor overrides draw() entirely, it can compute its own bar area internally.

**Final final approach**: Subclass StepPatternEditor. The key members are private but we can:
1. Override `draw()` completely (reimplementing with header awareness)
2. Use the public API (`getStepLevel()`, `getNumSteps()`, `getColorForLevel()`) for all data access
3. Use the existing mouse handlers unchanged (they call `getStepFromPoint()` which uses `getBarArea()` -- but since those are non-virtual and use `getViewSize()`, we need the mouse handlers to work in the body area)

**Problem**: The base class's `onMouseDown()` calls `getStepFromPoint()` which calls `getBarArea()` which is non-virtual. If ArpLaneEditor's view size includes the header, the bar area will be computed wrong (it won't account for the header height).

**Solution**: Make ArpLaneEditor's approach:
1. Store the header height offset
2. Override draw() entirely with lane-aware rendering
3. Override getBarArea() -- even though it's not virtual, in the context of ArpLaneEditor methods calling getBarArea() directly, the ArpLaneEditor version will be used. The issue is base class methods calling getBarArea() will use the base version.
4. Override onMouseDown/onMouseMoved/onMouseUp to adjust coordinates before delegating to base.

**Simpler solution**: Add a configurable `topOffset` to StepPatternEditor's getBarArea() calculation. This can be done by adding a `float barAreaTopOffset_ = 0.0f;` member with a setter, and adjusting `getBarArea()` to deduct this offset. Then ArpLaneEditor sets topOffset to kHeaderHeight (16px) and all base class calculations work correctly. This requires a small modification to StepPatternEditor but is minimal and backward-compatible.

**DECISION**: Modify StepPatternEditor to add a `barAreaTopOffset_` member (default 0.0f) and a public setter. ArpLaneEditor subclasses StepPatternEditor, sets the top offset to 16px, and overrides draw() to add the header. All mouse interaction and base class bar area calculations automatically work correctly.

---

## R-002: ArpLaneContainer CScrollView Architecture

**Decision**: ArpLaneContainer subclasses CViewContainer (NOT CScrollView directly) and manages scroll offset manually. CScrollView's API and internal child management are complex and designed for XML-declared children with automatic scrollbar drawing.

**Rationale**:
- FR-019 says children are added programmatically, not via XML
- CScrollView adds scrollbars automatically which may conflict with the desired aesthetic
- Manual scroll management is simpler: track scrollOffset_, handle onWheel(), and translate child positions
- The PresetBrowserView in this codebase already uses manual CViewContainer-based layout (not CScrollView)
- ArpLaneContainer only needs vertical scrolling, which is trivial to implement manually

**Alternatives considered**:
- CScrollView subclass: Rejected because CScrollView's internal container management (containerSize, getContainerSize, addView semantics) adds complexity for programmatic child management. Also CScrollView is not used in the current Ruinae editor.uidesc at all.
- CViewContainer with CScrollbar child: Rejected as over-engineered for 2-6 lanes.

**Implementation**:
- ArpLaneContainer extends CViewContainer
- Maintains a std::vector of ArpLaneEditor pointers
- Tracks scrollOffset_ (float, pixels)
- onWheel() adjusts scrollOffset_, clamps to [0, totalContentHeight - viewportHeight]
- draw() translates children by -scrollOffset_ vertically
- Mouse events offset by scrollOffset_ before hit-testing children
- Registers as "ArpLaneContainer" ViewCreator with viewport-height attribute

---

## R-003: Per-Lane Playhead Parameter Strategy

**Decision**: Two new parameter IDs for playhead position, allocated in the reserved gap after kArpRatchetSwingId (3293). Use IDs 3294 (velocity playhead) and 3295 (gate playhead).

**Rationale**:
- FR-027 specifies dedicated normalized parameter IDs per lane
- The spec says these are NOT exposed in host automation and NOT part of preset state
- The IDs fit in the reserved gap 3294-3299 before kArpEndId (3299)
- The processor writes the step index as normalized (index / maxSteps)
- ArpLaneEditor reads via the existing 30fps CVSTGUITimer poll (same as trance gate playback)
- Using parameter IDs (not IMessage) keeps the architecture consistent with how the spec requires it

**Alternatives considered**:
- IMessage for playhead: Rejected by spec clarification (Session 2026-02-24)
- Shared atomic pointers (like trance gate): Also viable but the spec explicitly says "dedicated parameter IDs". However, the trance gate uses atomic pointers via IMessage, not VST parameters. Using VST parameters for playhead has the drawback that they appear in the host's parameter list. The spec says "NOT exposed in host automation lanes" but VST3 parameters are inherently visible to the host.
- **Revised**: Use the same atomic pointer pattern as trance gate playback. The processor shares atomic<int> pointers via IMessage, and the controller polls them on the 30fps timer. This matches the existing architecture and avoids polluting the parameter list with UI-only data. The spec says "dedicated normalized parameter ID" but also says "NOT part of preset state and NOT exposed in host automation". VST3 parameters are by definition exposed to host automation. The atomic pointer approach satisfies the intent better.

**DECISION**: Follow the spec literally -- use dedicated parameter IDs (3294, 3295). Register them as non-automatable hidden parameters. VST3 SDK supports `ParameterInfo::kIsHidden` flag to hide from host automation. This satisfies both the spec requirement for parameter IDs and the non-automation requirement.

---

## R-004: Collapsible Header Design

**Decision**: ArpLaneEditor draws a 16px header area at the top of its view. The header contains: a collapse triangle (left), lane name label (next to triangle), and a length dropdown area (right side). When collapsed, the header shows a miniature bar preview.

**Rationale**:
- FR-010 specifies the header contents
- FR-011 specifies the miniature preview when collapsed
- The triangle icon reuses the same CGraphicsPath approach as ToggleButton's chevron drawing
- The length dropdown is a programmatically-created COptionMenu positioned within the header

**Key design choices**:
1. **Collapse state**: Stored as a bool `isCollapsed_` in ArpLaneEditor. NOT a parameter (spec says transient).
2. **Miniature preview**: Uses the same `getColorForLevel()` and step data, drawn at reduced scale (12px tall) within the header. All N bars at uniform width.
3. **Length dropdown**: Not a VSTGUI control embedded in the header (that would require sub-controller wiring). Instead, clicking the dropdown area opens a COptionMenu popup programmatically.
4. **Notification**: When collapse state changes, ArpLaneEditor calls a callback so ArpLaneContainer can relayout.

---

## R-005: Value Range Mapping for Lane Types

**Decision**: ArpLaneEditor stores a LaneType enum and a display range (minDisplay, maxDisplay). The normalized 0-1 parameter value maps linearly to the display range. Grid labels show the display range endpoints.

**Rationale**:
- FR-005 and FR-006 specify the mapping
- Velocity: display range [0.0, 1.0], identity mapping
- Gate: display range [0.0, 2.0] (representing 0%-200%), normalized 0.5 = display 1.0 = 100%
- Future Pitch: display range [-24, +24], bipolar
- The mapping is simple linear: displayValue = minDisplay + normalized * (maxDisplay - minDisplay)
- Grid labels format the display values as strings: "0.0"/"1.0" for velocity, "0%"/"200%" for gate

**Implementation**:
```cpp
enum class ArpLaneType { kVelocity, kGate, kPitch, kRatchet };

// In ArpLaneEditor:
float displayMin_ = 0.0f;   // e.g., 0.0 for velocity, 0.0 for gate
float displayMax_ = 1.0f;   // e.g., 1.0 for velocity, 2.0 for gate
std::string topLabel_ = "1.0";
std::string bottomLabel_ = "0.0";
```

**Note**: The actual bar rendering always works with normalized [0,1] values. The display range only affects grid labels and tooltips. The StepPatternEditor base class already works entirely in normalized space.

---

## R-006: Lane Color Registration

**Decision**: Register named colors in editor.uidesc for each lane type. The accent color is the primary bar color. Normal and ghost variants are derived algorithmically using ColorUtils::darkenColor().

**Rationale**:
- FR-030, FR-031, FR-032 specify color registration
- Existing pattern: uidesc already has named colors (e.g., "arpeggiator" = #C87850ff)
- The three-tier coloring (accent/normal/ghost) matches StepPatternEditor's existing getColorForLevel() thresholds:
  - level >= 0.80: accent (full color)
  - level >= 0.40: normal (dimmed)
  - level > 0.00: ghost (very dim)
  - level == 0.00: silent outline

**Color derivation**:
- Velocity copper #D0845C:
  - Accent: #D0845C (208, 132, 92)
  - Normal: darkenColor(accent, 0.6f) = approx (125, 79, 55)
  - Ghost: darkenColor(accent, 0.35f) = approx (73, 46, 32)
- Gate sand #C8A464:
  - Accent: #C8A464 (200, 164, 100)
  - Normal: darkenColor(accent, 0.6f) = approx (120, 98, 60)
  - Ghost: darkenColor(accent, 0.35f) = approx (70, 57, 35)

---

## R-007: Trance Gate Layout Compression

**Decision**: Compress the existing Trance Gate FieldsetContainer from 390px to ~100px. The toolbar row stays at ~26px, the StepPatternEditor bar area shrinks from ~326px to ~70px. Total: ~4px top padding + 26px toolbar + 70px bars = ~100px.

**Rationale**:
- FR-001 requires max 100px for trance gate
- The toolbar already fits in ~26px (knobs at 28px height + 10px label)
- StepPatternEditor at 70px height: bar area = 70 - 12 (phase offset) - 12 (step labels) - 8 (playback indicator) = 38px for bars. This is tight but functional.
- Actually, looking at layout zones: at 70px total, with all zones: scrollIndicator (0 when <24 steps), phaseOffset (12), bars (remaining), stepLabels (12), playbackIndicator (8). Bars = 70 - 12 - 12 - 8 = 38px. Minimum viable for bar editing.

**Implementation**: Change editor.uidesc FieldsetContainer size from "1384, 390" to "1384, 100" and StepPatternEditor size from "1368, 326" to "1368, 70" (origin adjusted).

---

## R-008: ViewCreator Attribute Design

**Decision**: ArpLaneEditor ViewCreator exposes these custom attributes:
- `lane-type` (list: "velocity", "gate", "pitch", "ratchet") - determines value range and labels
- `accent-color` (color) - primary bar color
- `step-level-base-param-id` (integer) - base parameter ID for step values
- `length-param-id` (integer) - parameter ID for lane length
- `playhead-param-id` (integer) - parameter ID for playhead position
- `lane-name` (string) - display name for header (e.g., "VEL", "GATE")

ArpLaneContainer ViewCreator exposes:
- `viewport-height` (float) - visible height of scroll area in pixels

**Rationale**: FR-012 and FR-019 specify these attributes. Using string attributes for IDs allows runtime configuration from uidesc. However, since FR-019 says children are added programmatically (NOT from XML), the ArpLaneEditor attributes are set programmatically too, making ViewCreator attributes less critical for ArpLaneEditor. But FR-012 still requires ViewCreator registration for potential use.

---

## R-009: Playback Poll Timer Architecture

**Decision**: Reuse the existing 30fps playbackPollTimer_ in the controller. Extend its callback to also poll arp lane playhead parameters and push to ArpLaneEditor instances.

**Rationale**:
- FR-028 requires ~30fps update
- The controller already has a 33ms CVSTGUITimer (playbackPollTimer_) that polls trance gate playback
- Adding arp lane playhead polling to the same timer avoids creating additional timers
- The controller needs pointers to ArpLaneEditor instances (set in verifyView or didOpen)

**Implementation**:
- Controller stores ArpLaneEditor pointers (velocity, gate)
- PlaybackPollTimer callback reads kArpVelocityPlayheadId and kArpGatePlayheadId parameters
- Converts normalized to step index and calls setPlaybackStep() on each lane
- Clears playhead when transport stops (setPlaybackStep(-1))
