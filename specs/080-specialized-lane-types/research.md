# Research: Specialized Lane Types (080)

**Date**: 2026-02-24 | **Branch**: `080-specialized-lane-types`

---

## R-001: ArpLaneEditor Extension Architecture (Bipolar + Discrete Modes)

**Decision**: Extend `ArpLaneEditor` with new rendering/interaction modes keyed by `ArpLaneType`, rather than creating new subclasses.

**Rationale**: The existing `ArpLaneEditor` already holds `laneType_` with `kPitch` and `kRatchet` enum values defined as placeholders. The class delegates bar rendering to `StepPatternEditor::drawBars()` which can be overridden. Rather than introducing additional inheritance layers, the cleanest approach is to:

1. Override `draw()` in `ArpLaneEditor` to dispatch to mode-specific rendering methods (`drawBipolarBars`, `drawDiscreteBlocks`) based on `laneType_`.
2. Override `onMouseDown()` and `onMouseMoved()` to dispatch to mode-specific interaction methods (bipolar level-from-Y, discrete click-cycle, discrete drag).
3. Override `drawMiniaturePreview()` to draw bipolar or block mini previews.

**Alternatives considered**:
- **Subclasses (ArpPitchLane, ArpRatchetLane)**: More OOP-pure but adds class proliferation. Since pitch and ratchet are "bar-like" controls that share 90% of the code (header, collapse, length dropdown, step storage, parameter callbacks), modes within the same class are cleaner.
- **Strategy pattern**: Overkill for 4 modes. The mode switch in draw/mouse handlers is straightforward.

---

## R-002: IArpLane Interface Design

**Decision**: Introduce `IArpLane` as a pure virtual interface in `plugins/shared/src/ui/arp_lane.h`.

**Rationale**: The container currently holds `std::vector<ArpLaneEditor*>`. The new modifier and condition lanes are fundamentally different CView subclasses (not ArpLaneEditor subclasses). A lightweight interface allows the container to polymorphically manage all lane types without `dynamic_cast`.

**Interface**:
```cpp
class IArpLane {
public:
    virtual ~IArpLane() = default;
    virtual VSTGUI::CView* getView() = 0;
    virtual float getExpandedHeight() const = 0;
    virtual float getCollapsedHeight() const = 0;
    virtual bool isCollapsed() const = 0;
    virtual void setCollapsed(bool collapsed) = 0;
    virtual void setPlayheadStep(int32_t step) = 0;
    virtual void setLength(int32_t length) = 0;
    virtual void setCollapseCallback(std::function<void()> cb) = 0;
};
```

**Alternatives considered**:
- **Template-based container**: Would require variant or type erasure. Adds complexity without benefit.
- **dynamic_cast at call sites**: Forbidden by FR-044. Also brittle and violates OCP.

---

## R-003: ArpLaneHeader Extraction

**Decision**: Extract header rendering and interaction into a non-CView `ArpLaneHeader` helper struct, owned by composition in each lane class.

**Rationale**: Three lane classes (ArpLaneEditor, ArpModifierLane, ArpConditionLane) need identical header behavior: collapse triangle, accent-colored name label, length dropdown with COptionMenu. Duplicating this across three classes would create maintenance debt. Extracting to a helper ensures Phase 11c transform buttons are added in one place.

**Design**:
- `ArpLaneHeader` is a plain struct/class (not a CView).
- Owns: laneName, accentColor, isCollapsed flag, lengthParamId, collapseCallback, lengthParamCallback.
- Provides: `draw(context, headerRect)`, `handleMouseDown(where, buttons) -> bool`, `getHeight()`.
- Each lane class creates one `ArpLaneHeader` member and delegates header drawing/hit-testing to it.
- `ArpLaneEditor` is refactored to delegate its existing `drawHeader()` and header click handling to `ArpLaneHeader`.

**Alternatives considered**:
- **CView subclass header**: Would require complex parent/child relationships. A non-CView helper is simpler and avoids VSTGUI lifecycle complexity.
- **Free functions**: Would require passing too many parameters. A struct with state is cleaner.

---

## R-004: ArpModifierLane Custom View Design

**Decision**: New `ArpModifierLane` class inheriting from `VSTGUI::CControl`, implementing `IArpLane`.

**Rationale**: The modifier lane is a 4-row toggle grid, fundamentally different from bar charts. It cannot be a mode of `ArpLaneEditor` because:
- Interaction is per-dot toggle (not click-to-set-level).
- Values are bitmasks (not 0.0-1.0 float levels).
- Layout is 2D (steps x 4 rows) not 1D (steps x level).

**Design details**:
- Inherits `VSTGUI::CControl` for parameter callbacks and dirty tracking.
- Stores `std::array<uint8_t, 32> stepFlags_` for bitmask values.
- Row layout: Rest (top), Tie, Slide, Accent (bottom). Left margin (~40px) for row labels.
- Each dot: filled circle (active) or outline circle (inactive).
- Hit-testing: determine step from x, row from y, toggle corresponding bit.
- Parameter encoding: each step value is a uint8_t bitmask (0x01=Active, 0x02=Tie, 0x04=Slide, 0x08=Accent).
- Rest toggle: toggling Rest = toggling bit 0 (kStepActive). Active=1 means note fires, so "Rest" dot active means kStepActive is OFF.
- The step parameters use normalized encoding: `normalized = bitmask / 255.0`. The controller denormalizes: `bitmask = round(normalized * 255)`.

---

## R-005: ArpConditionLane Custom View Design

**Decision**: New `ArpConditionLane` class inheriting from `VSTGUI::CControl`, implementing `IArpLane`.

**Rationale**: The condition lane uses popup menus per step (COptionMenu), fundamentally different from both bar charts and toggle grids.

**Design details**:
- Inherits `VSTGUI::CControl` for parameter callbacks.
- Stores `std::array<uint8_t, 32> stepConditions_` (0-17 TrigCondition index).
- One cell per step, displaying abbreviated label text.
- Click opens COptionMenu with 18 items.
- Right-click resets to Always (0).
- Tooltip on hover shows full condition name + description.
- VSTGUI tooltip: call `setTooltipText()` dynamically on `onMouseMoved()` to update per-step tooltip text.
- Parameter encoding: `normalized = conditionIndex / 17.0`. Controller denormalizes: `conditionIndex = round(normalized * 17)`.

---

## R-006: VSTGUI COptionMenu Popup Pattern

**Decision**: Use the same `COptionMenu::popup()` pattern already used in `ArpLaneEditor::openLengthDropdown()`.

**Rationale**: The existing codebase has a working pattern:
1. Create `COptionMenu` on the stack with a degenerate rect.
2. Call `addEntry()` for each item.
3. Call `setCurrent()` for the current selection.
4. Call `popup(frame, where)` for synchronous popup.
5. Read `getCurrentIndex()` after popup returns.
6. Call `forget()` to release.

This pattern is proven and cross-platform.

---

## R-007: Parameter Normalization for New Lane Types

**Decision**: Reuse existing normalization patterns from arpeggiator_params.h.

**Verified encoding (from arpeggiator_params.h)**:
- **Pitch**: normalized 0.0-1.0 maps to -24..+24 semitones. `pitch = -24 + round(normalized * 48)`. Normalized 0.5 = 0 semitones.
- **Ratchet**: normalized 0.0-1.0 maps to 1-4. `ratchet = 1 + round(normalized * 3)`. Normalized 0.0 = 1 (no subdivision).
- **Modifier**: normalized 0.0-1.0 maps to bitmask 0-255. `bitmask = round(normalized * 255)`. Default = 1 (kStepActive only).
- **Condition**: normalized 0.0-1.0 maps to 0-17 TrigCondition. `index = round(normalized * 17)`. Default = 0 (Always).
- **Length (all lanes)**: normalized 0.0-1.0 maps to 1-32. `length = 1 + round(normalized * 31)`.

---

## R-008: Playhead Parameter Registration

**Decision**: Add 4 new hidden, non-persisted parameters for the new lane playheads at IDs 3296-3299.

**Rationale**: The existing velocity (3294) and gate (3295) playhead parameters use a hidden, non-persisted pattern. The processor writes `stepIndex / 32.0` as normalized, and the controller polls and decodes. The 4 new lane playheads follow the same pattern.

**Registration**: Same pattern as `registerArpPlayheadParams()` in arpeggiator_params.h. The parameters are registered with `kNoFlags` (hidden from host, not persisted).

---

## R-009: ViewCreator Registration Pattern

**Decision**: Both `ArpModifierLane` and `ArpConditionLane` must have ViewCreator registrations following the `ArpLaneEditorCreator` pattern.

**Rationale**: FR-030 and FR-042 require uidesc configurability. The existing pattern uses `VSTGUI::ViewCreatorAdapter` with static registration via a global instance.

**Attributes for ArpModifierLaneCreator**:
- `accent-color`: CColor
- `lane-name`: string
- `step-flag-base-param-id`: string (uint32_t)
- `length-param-id`: string (uint32_t)
- `playhead-param-id`: string (uint32_t)

**Attributes for ArpConditionLaneCreator**:
- `accent-color`: CColor
- `lane-name`: string
- `step-condition-base-param-id`: string (uint32_t)
- `length-param-id`: string (uint32_t)
- `playhead-param-id`: string (uint32_t)

---

## R-010: Tooltip Dynamic Update Strategy

**Decision**: Override `onMouseMoved()` in ArpConditionLane to call `setTooltipText()` based on the step under the cursor.

**Rationale**: VSTGUI supports per-view tooltips via `CView::setTooltipText()`. The CTooltipSupport system reads this text when the mouse hovers. By updating the tooltip text on every mouse move, each step can show its own condition description.

**Tooltip text format**: "{FullName} -- {Description}"
- Always: "Always -- Step fires unconditionally"
- Prob50: "50% -- ~50% probability of firing"
- Ratio_1_2: "Every 2 -- Fires on 1st of every 2 loops"
- Fill: "Fill -- Fires only when fill mode is active"
- etc.

---

## R-011: Existing Code Reuse Analysis

**Components to reuse directly**:
- `darkenColor()` from `color_utils.h` for accent color variants
- `StepPatternEditor::getBarArea()` computation pattern for step column layout
- `ArpLaneEditor::openLengthDropdown()` pattern for COptionMenu
- Parameter callback pattern (`setParameterCallback`, `setBeginEditCallback`, `setEndEditCallback`)
- Playhead polling timer pattern from `controller.cpp`

**Components to refactor**:
- `ArpLaneEditor::drawHeader()` and header click handling -> extract to `ArpLaneHeader`
- `ArpLaneEditor::drawMiniaturePreview()` -> extend for bipolar/block previews
- `ArpLaneContainer::addLane()` -> generalize from `ArpLaneEditor*` to `IArpLane*`
- `ArpLaneContainer::lanes_` -> change from `std::vector<ArpLaneEditor*>` to `std::vector<IArpLane*>`
