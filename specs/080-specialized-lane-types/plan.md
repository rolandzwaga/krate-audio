# Implementation Plan: Specialized Arpeggiator Lane Types

**Branch**: `080-specialized-lane-types` | **Date**: 2026-02-24 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/080-specialized-lane-types/spec.md`

## Summary

Extend the Ruinae arpeggiator's Phase 11a lane framework with 4 new lane types: Pitch (bipolar bar chart, -24..+24 semitones), Ratchet (discrete stacked blocks, 1-4), Modifier (4-row toggle dot grid for Rest/Tie/Slide/Accent bitmask), and Condition (18-value enum popup per step). Introduce the `IArpLane` pure virtual interface for polymorphic container management, extract `ArpLaneHeader` as a shared helper, and integrate all 6 lanes into the existing `ArpLaneContainer`.

## Technical Context

**Language/Version**: C++20 (MSVC, Clang, GCC)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+ (CControl, COptionMenu, CDrawContext, CGraphicsPath, ViewCreatorAdapter)
**Storage**: N/A (parameters stored via VST3 state serialization, already implemented in arpeggiator_params.h)
**Testing**: Catch2 (shared_tests target for UI components, ruinae_tests for integration) *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform via VSTGUI
**Project Type**: Monorepo -- VST3 plugin (shared UI components + Ruinae plugin wiring)
**Performance Goals**: Zero allocations in draw/mouse/playhead paths (SC-009). All drawing at UI thread rates (~30fps refresh).
**Constraints**: No platform-specific UI code (Constitution VI). Header-only implementations preferred for shared UI components.
**Scale/Scope**: 4 new files + 4 modified files in shared UI, 4 modified files in Ruinae, 3 new test files.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] All new code is UI-thread only (custom CView/CControl classes)
- [x] No processor changes needed (lane parameters already registered)
- [x] Parameter communication via existing IParameterChanges infrastructure

**Principle II (Real-Time Audio Thread Safety):**
- [x] No audio-thread code in this spec (UI-only feature)
- [x] No allocations in draw/mouse paths (pre-allocated arrays, constexpr lookup tables)

**Principle III (Modern C++ Standards):**
- [x] std::array for fixed-size step storage (no raw arrays)
- [x] std::function for callbacks (no raw function pointers)
- [x] constexpr for lookup tables (condition abbreviations, tooltips)

**Principle V (VSTGUI Development):**
- [x] UIDescription XML for uidesc color registration
- [x] ViewCreator registrations for all new custom views (FR-030, FR-042)
- [x] IControlListener pattern for parameter callbacks

**Principle VI (Cross-Platform Compatibility):**
- [x] All rendering via VSTGUI CDrawContext (no platform-specific drawing)
- [x] COptionMenu for popups (cross-platform)
- [x] No Win32/Cocoa/AppKit APIs

**Principle VIII (Testing Discipline):**
- [x] Tests written BEFORE implementation code
- [x] Each task group ends with build verification

**Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) -- no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Post-Design Re-Check:**
- [x] IArpLane is a new interface -- no existing IArpLane found in codebase
- [x] ArpLaneHeader is a new helper -- no existing ArpLaneHeader found in codebase
- [x] ArpModifierLane is a new class -- no existing ArpModifierLane found in codebase
- [x] ArpConditionLane is a new class -- no existing ArpConditionLane found in codebase
- [x] All designs use VSTGUI cross-platform APIs exclusively

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: IArpLane, ArpLaneHeader, ArpModifierLane, ArpConditionLane, ArpModifierLaneCreator, ArpConditionLaneCreator

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| IArpLane | `grep -r "class IArpLane" plugins/` | No | Create New |
| ArpLaneHeader | `grep -r "class ArpLaneHeader\|struct ArpLaneHeader" plugins/` | No | Create New |
| ArpModifierLane | `grep -r "class ArpModifierLane" plugins/` | No | Create New |
| ArpConditionLane | `grep -r "class ArpConditionLane" plugins/` | No | Create New |
| ArpModifierLaneCreator | `grep -r "ArpModifierLaneCreator" plugins/` | No | Create New |
| ArpConditionLaneCreator | `grep -r "ArpConditionLaneCreator" plugins/` | No | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| darkenColor() | plugins/shared/src/ui/color_utils.h | UI | Derive normal/ghost colors from accent |
| lerpColor() | plugins/shared/src/ui/color_utils.h | UI | Potential use in playhead overlay |
| ArpLaneEditor | plugins/shared/src/ui/arp_lane_editor.h | UI | Extend with bipolar/discrete modes |
| ArpLaneContainer | plugins/shared/src/ui/arp_lane_container.h | UI | Generalize to IArpLane* |
| StepPatternEditor | plugins/shared/src/ui/step_pattern_editor.h | UI | Base class (read-only reference) |
| ArpStepFlags | dsp/include/krate/dsp/processors/arpeggiator_core.h | L2 | Reference for modifier bitmask encoding |
| TrigCondition | dsp/include/krate/dsp/processors/arpeggiator_core.h | L2 | Reference for condition enum values |

### Files Checked for Conflicts

- [x] `plugins/shared/src/ui/` -- All arp_* files reviewed; no conflicts
- [x] `plugins/ruinae/src/plugin_ids.h` -- Playhead IDs 3296-3299 not yet defined; kArpEndId = 3299 accommodates them
- [x] `plugins/ruinae/src/parameters/arpeggiator_params.h` -- All lane step params already registered
- [x] `dsp/include/krate/dsp/processors/arpeggiator_core.h` -- ArpStepFlags and TrigCondition stable

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All 4 planned new types (IArpLane, ArpLaneHeader, ArpModifierLane, ArpConditionLane) are unique and not found anywhere in the codebase. The only modifications to existing types (ArpLaneEditor, ArpLaneContainer) are extensions, not redefinitions. All new types are in the `Krate::Plugins` namespace, consistent with existing UI components.

## Dependency API Contracts (Principle XV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| StepPatternEditor | setStepLevel | `void setStepLevel(int index, float level)` | yes |
| StepPatternEditor | getStepLevel | `[[nodiscard]] float getStepLevel(int index) const` | yes |
| StepPatternEditor | setNumSteps | `void setNumSteps(int count)` | yes |
| StepPatternEditor | getNumSteps | `[[nodiscard]] int getNumSteps() const` | yes |
| StepPatternEditor | setPlaybackStep | `void setPlaybackStep(int step)` | yes |
| StepPatternEditor | getBarArea | `[[nodiscard]] VSTGUI::CRect getBarArea() const` | yes |
| StepPatternEditor | getStepFromPoint | `[[nodiscard]] int getStepFromPoint(const VSTGUI::CPoint& point) const` | yes |
| StepPatternEditor | getLevelFromY | `[[nodiscard]] float getLevelFromY(float y) const` | yes |
| StepPatternEditor | setBarAreaTopOffset | `void setBarAreaTopOffset(float offset)` | yes |
| StepPatternEditor | setParameterCallback | `void setParameterCallback(ParameterCallback cb)` | yes |
| StepPatternEditor | setBeginEditCallback | `void setBeginEditCallback(EditCallback cb)` | yes |
| StepPatternEditor | setEndEditCallback | `void setEndEditCallback(EditCallback cb)` | yes |
| StepPatternEditor | setStepLevelBaseParamId | `void setStepLevelBaseParamId(uint32_t baseId)` | yes |
| ArpLaneEditor | setLaneType | `void setLaneType(ArpLaneType type)` | yes |
| ArpLaneEditor | setAccentColor | `void setAccentColor(const VSTGUI::CColor& color)` | yes |
| ArpLaneEditor | setLaneName | `void setLaneName(const std::string& name)` | yes |
| ArpLaneEditor | setLengthParamId | `void setLengthParamId(uint32_t paramId)` | yes |
| ArpLaneEditor | setPlayheadParamId | `void setPlayheadParamId(uint32_t paramId)` | yes |
| ArpLaneEditor | setCollapseCallback | `void setCollapseCallback(std::function<void()> cb)` | yes |
| ArpLaneEditor | setLengthParamCallback | `void setLengthParamCallback(std::function<void(uint32_t, float)> cb)` | yes |
| ArpLaneEditor | isCollapsed | `[[nodiscard]] bool isCollapsed() const` | yes |
| ArpLaneEditor | getExpandedHeight | `[[nodiscard]] float getExpandedHeight() const` | yes |
| ArpLaneEditor | getCollapsedHeight | `[[nodiscard]] float getCollapsedHeight() const` | yes |
| ArpLaneContainer | addLane | `void addLane(ArpLaneEditor* lane)` (will change to IArpLane*) | yes |
| darkenColor | darkenColor | `[[nodiscard]] inline VSTGUI::CColor darkenColor(const VSTGUI::CColor& color, float factor)` | yes |
| CView | setTooltipText | `void setTooltipText(UTF8StringPtr text)` | yes |
| COptionMenu | popup | `void popup(CFrame*, CPoint)` (pattern from ArpLaneEditor::openLengthDropdown) | yes |

### Header Files Read

- [x] `plugins/shared/src/ui/step_pattern_editor.h` -- StepPatternEditor class (full read)
- [x] `plugins/shared/src/ui/arp_lane_editor.h` -- ArpLaneEditor class (full read)
- [x] `plugins/shared/src/ui/arp_lane_container.h` -- ArpLaneContainer class (full read)
- [x] `plugins/shared/src/ui/color_utils.h` -- darkenColor, lerpColor (full read)
- [x] `plugins/ruinae/src/plugin_ids.h` -- Parameter IDs (full read)
- [x] `plugins/ruinae/src/parameters/arpeggiator_params.h` -- Parameter handling (full read)
- [x] `plugins/ruinae/src/controller/controller.h` -- Controller lane pointers (full read)
- [x] `plugins/ruinae/src/controller/controller.cpp` -- Lane wiring pattern (full read)
- [x] `dsp/include/krate/dsp/processors/arpeggiator_core.h` -- ArpStepFlags, TrigCondition (partial read)
- [x] `extern/vst3sdk/vstgui4/vstgui/lib/cview.h` -- setTooltipText (partial read)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| StepPatternEditor | `setStepLevel()` clamps to 0.0-1.0 | Normalized values only. Pitch/ratchet need internal conversion. |
| StepPatternEditor | `drawBars()` is private, not virtual | Must add new draw methods in ArpLaneEditor, dispatch from overridden `draw()` |
| ArpLaneEditor | Header drawing is inline in private methods | Must refactor to ArpLaneHeader before extending |
| ArpLaneContainer | `addView()` is called inside `addLane()` | IArpLane::getView() needed to pass CView* to addView |
| Pitch normalization | 0.5 normalized = 0 semitones | `pitch = -24 + round(normalized * 48)` |
| Modifier normalization | Default = 1 (kStepActive) not 0 | `bitmask = round(normalized * 255)`, so default normalized = 1/255 |
| Ratchet normalization | 1 (no subdivision) = normalized 0.0 | `ratchet = 1 + round(normalized * 3)` |
| COptionMenu::popup() | Synchronous call, reads result after return | Pattern already proven in openLengthDropdown() |

## Layer 0 Candidate Analysis

*This feature is UI-only (no DSP Layer involvement). No Layer 0 extraction needed.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| drawBipolarBars() | ArpLaneEditor-specific rendering |
| drawDiscreteBlocks() | ArpLaneEditor-specific rendering |
| handleDiscreteClick() | ArpLaneEditor-specific interaction |

**Decision**: No Layer 0 extraction. All new code is UI-specific.

## SIMD Optimization Analysis

**Verdict**: NOT APPLICABLE

**Reasoning**: This feature is entirely UI-layer code (VSTGUI custom views). There are no DSP algorithms to optimize with SIMD. The UI draw/interaction methods run at ~30fps on the UI thread, well within budget.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: UI Components (plugins/shared/src/ui/)

**Related features at same layer**:
- Phase 11c (Interaction Polish): Transform buttons, copy/paste, playhead trail
- Future plugins with lane-based sequencers

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| IArpLane interface | HIGH | Any future lane-based UI | Extract now (already shared) |
| ArpLaneHeader | HIGH | Phase 11c (transform buttons) | Extract now (spec requires it) |
| ArpModifierLane | MEDIUM | Future toggle-grid UIs | Keep in shared, ViewCreator registered |
| ArpConditionLane | MEDIUM | Future enum-popup lane UIs | Keep in shared, ViewCreator registered |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| IArpLane in shared | Required by FR-044 for heterogeneous container |
| ArpLaneHeader in shared | Required by FR-051, Phase 11c will add transform buttons here |
| ViewCreator registrations | Required by FR-030, FR-042 for uidesc configurability |

### Review Trigger

After implementing **Phase 11c (Interaction Polish)**, review this section:
- [ ] Does 11c need ArpLaneHeader? -> YES, transform buttons are added there
- [ ] Does 11c use the same IArpLane interface? -> YES, for per-lane transforms
- [ ] Any duplicated code? -> Check for shared transform logic

## Project Structure

### Documentation (this feature)

```text
specs/080-specialized-lane-types/
+-- plan.md              # This file
+-- research.md          # Phase 0 research
+-- data-model.md        # Phase 1 data model
+-- quickstart.md        # Phase 1 quickstart
+-- contracts/           # Phase 1 API contracts
|   +-- arp_lane_interface.h
|   +-- arp_lane_header.h
|   +-- arp_modifier_lane.h
|   +-- arp_condition_lane.h
+-- tasks.md             # Phase 2 (NOT created by plan)
```

### Source Code (repository root)

```text
plugins/shared/src/ui/
+-- arp_lane.h                 # NEW: IArpLane interface
+-- arp_lane_header.h          # NEW: ArpLaneHeader helper
+-- arp_lane_editor.h          # MODIFIED: bipolar/discrete modes, IArpLane, ArpLaneHeader
+-- arp_lane_container.h       # MODIFIED: IArpLane* vector
+-- arp_modifier_lane.h        # NEW: ArpModifierLane + ViewCreator
+-- arp_condition_lane.h       # NEW: ArpConditionLane + ViewCreator

plugins/ruinae/src/
+-- plugin_ids.h               # MODIFIED: 4 new playhead param IDs
+-- parameters/arpeggiator_params.h  # MODIFIED: register playhead params
+-- controller/controller.h    # MODIFIED: 4 new lane pointers
+-- controller/controller.cpp  # MODIFIED: construct/wire 4 new lanes
+-- resources/editor.uidesc    # MODIFIED: 4 new named colors

plugins/shared/tests/
+-- test_arp_lane_header.cpp   # NEW: ArpLaneHeader tests
+-- test_arp_modifier_lane.cpp # NEW: ArpModifierLane tests
+-- test_arp_condition_lane.cpp # NEW: ArpConditionLane tests
+-- test_arp_lane_editor.cpp   # MODIFIED: bipolar/discrete mode tests
+-- test_arp_lane_container.cpp # MODIFIED: IArpLane integration tests
+-- CMakeLists.txt             # MODIFIED: add new test files
```

**Structure Decision**: Monorepo layout. All shared UI components in `plugins/shared/src/ui/`. Plugin-specific wiring in `plugins/ruinae/src/`. Tests mirror the shared component structure.

## Complexity Tracking

No constitution violations. No complexity justifications needed.

---

## Detailed Implementation Design

### Phase 1: Foundation -- IArpLane Interface + ArpLaneHeader Extraction

**Files**: `arp_lane.h` (new), `arp_lane_header.h` (new), `arp_lane_editor.h` (modified), `arp_lane_container.h` (modified)

#### 1.1 IArpLane Interface (`plugins/shared/src/ui/arp_lane.h`)

See `contracts/arp_lane_interface.h` for the complete interface. Key points:
- Pure virtual destructor (`virtual ~IArpLane() = default`)
- `getView()` returns the CView* for container addView/removeView
- `getExpandedHeight()` / `getCollapsedHeight()` for layout calculations
- `isCollapsed()` / `setCollapsed()` for collapse state
- `setPlayheadStep()` for playback position
- `setLength()` for step count
- `setCollapseCallback()` for container relayout notification

#### 1.2 ArpLaneHeader Extraction (`plugins/shared/src/ui/arp_lane_header.h`)

Extract from `ArpLaneEditor`'s private methods into a reusable helper:

**From ArpLaneEditor (to be removed/delegated)**:
- `drawHeader()` -> `ArpLaneHeader::draw()`
- `drawCollapseTriangle()` -> internal to `ArpLaneHeader::draw()`
- `openLengthDropdown()` -> `ArpLaneHeader::openLengthDropdown()`
- Header click handling in `onMouseDown()` -> `ArpLaneHeader::handleMouseDown()`

**ArpLaneHeader stores**:
- `laneName_`, `accentColor_`, `isCollapsed_`, `numSteps_`
- `lengthParamId_`, `collapseCallback_`, `lengthParamCallback_`

**ArpLaneHeader::draw(CDrawContext*, CRect headerRect)**:
1. Fill header background (#1E1E21)
2. Draw collapse triangle (right-pointing if collapsed, down-pointing if expanded)
3. Draw lane name in accent color
4. Draw step count + dropdown indicator triangle

**ArpLaneHeader::handleMouseDown(CPoint, CRect headerRect, CFrame*) -> bool**:
1. Check if click is in the header area
2. If click in collapse zone (left ~24px): toggle collapse, fire callback, return true
3. If click in length dropdown zone: open COptionMenu, fire length callback, return true
4. Otherwise return false

#### 1.3 ArpLaneEditor Refactor

**Add IArpLane implementation**:
```cpp
class ArpLaneEditor : public StepPatternEditor, public IArpLane {
    // IArpLane overrides
    VSTGUI::CView* getView() override { return this; }
    float getExpandedHeight() const override { /* existing logic */ }
    float getCollapsedHeight() const override { return ArpLaneHeader::kHeight; }
    bool isCollapsed() const override { return header_.isCollapsed(); }
    void setCollapsed(bool c) override { header_.setCollapsed(c); /* resize + callback */ }
    void setPlayheadStep(int32_t step) override { setPlaybackStep(step); }
    void setLength(int32_t length) override { setNumSteps(length); }
    void setCollapseCallback(std::function<void()> cb) override { header_.setCollapseCallback(cb); }
};
```

**Replace inline header logic with ArpLaneHeader member**:
- Replace `drawHeader()` call with `header_.draw(context, headerRect)`
- Replace header click handling with `header_.handleMouseDown(where, headerRect, getFrame())`
- Remove `drawCollapseTriangle()`, `openLengthDropdown()` private methods
- Forward `setLaneName()`, `setAccentColor()`, `setLengthParamId()`, etc. to `header_`

#### 1.4 ArpLaneContainer Generalization

**Change `lanes_` from `std::vector<ArpLaneEditor*>` to `std::vector<IArpLane*>`**:

```cpp
void addLane(IArpLane* lane) {
    lanes_.push_back(lane);
    addView(lane->getView());
    lane->setCollapseCallback([this]() { recalculateLayout(); });
    recalculateLayout();
}

void removeLane(IArpLane* lane) {
    auto it = std::find(lanes_.begin(), lanes_.end(), lane);
    if (it != lanes_.end()) lanes_.erase(it);
    removeView(lane->getView(), true);
    recalculateLayout();
}
```

**Update `recalculateLayout()`** to use IArpLane interface methods (already compatible since ArpLaneEditor had the same method names).

---

### Phase 2: Pitch Lane -- ArpLaneEditor Bipolar Mode

**Files**: `arp_lane_editor.h` (modified)

#### 2.1 Bipolar Rendering (FR-001, FR-002, FR-007, FR-008)

Add `drawBipolarBars()` method to ArpLaneEditor:

```cpp
void drawBipolarBars(VSTGUI::CDrawContext* context) {
    VSTGUI::CRect barArea = getBarArea();
    float barHeight = barArea.getHeight();
    float centerY = barArea.top + barHeight / 2.0f;

    // Draw center line at 0 semitones
    context->setFrameColor(gridColor_);
    context->drawLine(CPoint(barArea.left, centerY), CPoint(barArea.right, centerY));

    // Draw grid labels: "+24" top, "0" center, "-24" bottom
    // ...

    for (int i = visibleStart; i < visibleEnd; ++i) {
        float normalized = getStepLevel(i);  // 0.0-1.0
        float signedValue = (normalized - 0.5f) * 2.0f;  // -1.0 to +1.0

        if (std::abs(signedValue) < 0.001f) {
            // Zero: draw outline at center
            continue;
        }

        float barTop, barBottom;
        if (signedValue > 0) {
            barTop = centerY - (signedValue * barHeight / 2.0f);
            barBottom = centerY;
        } else {
            barTop = centerY;
            barBottom = centerY - (signedValue * barHeight / 2.0f);
        }

        // Draw bar from center line
        CColor barColor = getColorForLevel(std::abs(signedValue));
        context->setFillColor(barColor);
        CRect bar(barLeft, barTop, barRight, barBottom);
        context->drawRect(bar, kDrawFilled);
    }
}
```

#### 2.2 Bipolar Interaction (FR-003, FR-004, FR-005, FR-006)

Override mouse handling for pitch mode:
- **Click**: Map Y position to signed value (-1.0 to +1.0), snap to nearest semitone (round to 1/48th), store as normalized (0.0-1.0 where 0.5 = zero)
- **Drag**: Same mapping, paint across steps
- **Right-click**: Reset to 0.5 (0 semitones)
- **Snapping**: `snapped = round(signedValue * 24.0f) / 24.0f` then convert back to normalized

#### 2.3 Bipolar Miniature Preview (FR-010)

Draw mini bars relative to center (miniPreviewHeight / 2.0):
- Positive values: tiny bars above center
- Negative values: tiny bars below center
- Color: accent color

---

### Phase 3: Ratchet Lane -- ArpLaneEditor Discrete Mode

**Files**: `arp_lane_editor.h` (modified)

#### 3.1 Discrete Rendering (FR-011, FR-012, FR-016, FR-018)

Add `drawDiscreteBlocks()` method:

```cpp
void drawDiscreteBlocks(VSTGUI::CDrawContext* context) {
    VSTGUI::CRect barArea = getBarArea();
    float barHeight = barArea.getHeight();
    float blockGap = 2.0f;
    float blockHeight = (barHeight - 3.0f * blockGap) / 4.0f;

    for (int i = visibleStart; i < visibleEnd; ++i) {
        float normalized = getStepLevel(i);  // 0.0-1.0
        int count = std::clamp(static_cast<int>(1.0f + std::round(normalized * 3.0f)), 1, 4);

        float barLeft = /* step column left */;
        float barRight = /* step column right */;

        for (int b = 0; b < count; ++b) {
            float blockBottom = barArea.bottom - b * (blockHeight + blockGap);
            float blockTop = blockBottom - blockHeight;

            CRect block(barLeft + kBarPadding, blockTop,
                        barRight - kBarPadding, blockBottom);
            context->setFillColor(getColorForLevel(static_cast<float>(count) / 4.0f));
            context->drawRect(block, kDrawFilled);
        }
    }
}
```

#### 3.2 Discrete Interaction (FR-013, FR-014, FR-015)

- **Click**: Cycle value 1->2->3->4->1. Convert to normalized: `(count - 1) / 3.0f`. A "click" is defined as a mouse-down + mouse-up with total vertical movement <4px (consistent with StepPatternEditor's drag threshold). Record the mouse-down Y on onMouseDown; only fire the cycle in onMouseUp if abs(deltaY) < 4.0f.
- **Drag**: Track cumulative vertical delta. Every 8px up: increment (clamp at 4). Every 8px down: decrement (clamp at 1). No wrapping on drag. Drag mode is entered once abs(deltaY) >= 4.0f on the first onMouseMoved.
- **Right-click**: Reset to 1 (normalized 0.0)

State for discrete drag:
```cpp
float discreteDragAccumY_ = 0.0f;  // Accumulated vertical drag distance
int discreteDragStartValue_ = 1;    // Value at drag start
float discreteClickStartY_ = 0.0f; // Y position at mouse-down (for click detection)
```

#### 3.3 Discrete Miniature Preview (FR-019)

Tiny block indicators: height proportional to value (1=25%, 2=50%, 3=75%, 4=100% of mini preview height).

---

### Phase 4: Modifier Lane -- ArpModifierLane Custom View

**Files**: `arp_modifier_lane.h` (new)

#### 4.1 Class Structure

```cpp
class ArpModifierLane : public VSTGUI::CControl, public IArpLane {
    static constexpr int kRowCount = 4;
    static constexpr float kLeftMargin = 40.0f;
    static constexpr float kDotRadius = 4.0f;
    static constexpr float kBodyHeight = 44.0f;
    static constexpr float kRowHeight = 11.0f;  // 44 / 4

    // Row definitions
    static constexpr const char* kRowLabels[4] = {"Rest", "Tie", "Slide", "Accent"};
    static constexpr uint8_t kRowBits[4] = {0x01, 0x02, 0x04, 0x08};
    // Note: Row 0 (Rest) is INVERTED: dot active = bit OFF
};
```

#### 4.2 Drawing (FR-020, FR-021, FR-022, FR-024, FR-027)

```
draw():
  1. header_.draw(context, headerRect)
  2. if collapsed: drawMiniPreview(), return
  3. Draw body background
  4. Draw row labels in left margin
  5. For each step i (0..numSteps-1):
     For each row r (0..3):
       - Compute dot center (x, y)
       - If flag active: filled circle in accent color
       - If flag inactive: outline circle in dim color
  6. Draw playhead overlay if active
```

#### 4.3 Interaction (FR-022)

**Click handling**:
1. Check header first: `if (header_.handleMouseDown(...)) return handled`
2. If collapsed: return handled (no body interaction)
3. Determine step from x: `step = (x - bodyLeft - kLeftMargin) / stepWidth`
4. Determine row from y: `row = (y - bodyTop) / kRowHeight`
5. If valid step and row: toggle flag bit
   - Row 0 (Rest): `flags ^= kStepActive` (invert: active dot = bit OFF)
   - Row 1-3: `flags ^= kRowBits[row]`
   - Always mask result: `flags &= 0x0F`
6. Notify parameter callback with new bitmask: `normalized = (flags & 0x0F) / 15.0f`

#### 4.4 ViewCreator Registration (FR-030)

Same pattern as `ArpLaneEditorCreator`:
- Attributes: `accent-color`, `lane-name`, `step-flag-base-param-id`, `length-param-id`, `playhead-param-id`
- Global instance: `inline ArpModifierLaneCreator gArpModifierLaneCreator;`

---

### Phase 5: Condition Lane -- ArpConditionLane Custom View

**Files**: `arp_condition_lane.h` (new)

#### 5.1 Class Structure

```cpp
class ArpConditionLane : public VSTGUI::CControl, public IArpLane {
    static constexpr int kConditionCount = 18;
    static constexpr float kBodyHeight = 28.0f;

    static constexpr const char* kAbbrev[18] = {
        "Alw", "10%", "25%", "50%", "75%", "90%",
        "Ev2", "2:2", "Ev3", "2:3", "3:3",
        "Ev4", "2:4", "3:4", "4:4",
        "1st", "Fill", "!F"
    };
};
```

#### 5.2 Drawing (FR-031, FR-032, FR-037)

```
draw():
  1. header_.draw(context, headerRect)
  2. if collapsed: drawMiniPreview(), return
  3. Draw body background
  4. For each step i (0..numSteps-1):
     - Get condition index (0-17)
     - Draw cell background (slightly lighter for non-Always)
     - Draw abbreviated label centered in cell
     - Draw playhead overlay if i == playheadStep_
```

#### 5.3 Interaction (FR-033, FR-034, FR-035)

**Click**:
1. Check header first
2. If collapsed: return handled
3. Determine step from x position
4. Open COptionMenu with 18 full-name entries
5. On selection: update stepConditions_[step], notify parameter callback

**Right-click**:
1. Determine step from x
2. Reset to 0 (Always), notify parameter callback

**Hover/Tooltip** (FR-035):
Override `onMouseMoved()`:
```cpp
CMouseEventResult onMouseMoved(CPoint& where, const CButtonState& buttons) override {
    int step = getStepFromPoint(where);
    if (step >= 0 && step < numSteps_) {
        uint8_t cond = stepConditions_[step];
        if (cond < kConditionCount) {
            setTooltipText(kConditionTooltips[cond]);
        }
    }
    return kMouseEventHandled;
}
```

#### 5.4 ViewCreator Registration (FR-042)

Attributes: `accent-color`, `lane-name`, `step-condition-base-param-id`, `length-param-id`, `playhead-param-id`

---

### Phase 6: Ruinae Integration -- Parameters, Controller, Colors

**Files**: `plugin_ids.h`, `arpeggiator_params.h`, `controller.h`, `controller.cpp`, `editor.uidesc`

#### 6.1 Add Playhead Parameter IDs (`plugin_ids.h`)

```cpp
// --- Playhead Parameters (079-layout-framework + 080-specialized-lane-types) ---
kArpVelocityPlayheadId    = 3294,
kArpGatePlayheadId        = 3295,
kArpPitchPlayheadId       = 3296,   // NEW
kArpRatchetPlayheadId     = 3297,   // NEW
kArpModifierPlayheadId    = 3298,   // NEW
kArpConditionPlayheadId   = 3299,   // NEW
```

#### 6.2 Register Playhead Parameters (`arpeggiator_params.h`)

Add registration for the 4 new playhead parameters following the existing hidden non-persisted pattern.

#### 6.3 Controller Wiring (`controller.h` + `controller.cpp`)

**controller.h**: Add 4 new lane pointers:
```cpp
Krate::Plugins::ArpLaneEditor* pitchLane_ = nullptr;
Krate::Plugins::ArpLaneEditor* ratchetLane_ = nullptr;
Krate::Plugins::ArpModifierLane* modifierLane_ = nullptr;
Krate::Plugins::ArpConditionLane* conditionLane_ = nullptr;
```

**controller.cpp verifyView()**: After the existing velocity/gate lane construction, add 4 new lane constructions. NOTE (C1/FR-049): The modifier lane's step-content x-origin must equal the bar left-edge of other lanes at the same step count. ArpModifierLane achieves this via its kLeftMargin=40.0f constant which offsets the row labels without shifting the step columns. Do not override or misconfigure this constant. Alignment is verified by T087/T088. NOTE (I1): Before adding new kPitch/kRatchet mode handling to ArpLaneEditor, search for and remove any existing placeholder stubs for these modes (the spec assumption states they exist but have no rendering logic).

```cpp
// Pitch lane
pitchLane_ = new ArpLaneEditor(CRect(0, 0, 500, 86), nullptr, -1);
pitchLane_->setLaneName("PITCH");
pitchLane_->setLaneType(ArpLaneType::kPitch);
pitchLane_->setAccentColor(CColor{108, 168, 160, 255});  // Sage
pitchLane_->setDisplayRange(-24.0f, 24.0f, "+24", "-24");
pitchLane_->setStepLevelBaseParamId(kArpPitchLaneStep0Id);
pitchLane_->setLengthParamId(kArpPitchLaneLengthId);
pitchLane_->setPlayheadParamId(kArpPitchPlayheadId);
// ... wire callbacks ...
arpLaneContainer_->addLane(pitchLane_);

// Ratchet lane (shorter height: ~52px = 16px header + 36px body)
ratchetLane_ = new ArpLaneEditor(CRect(0, 0, 500, 52), nullptr, -1);
ratchetLane_->setLaneName("RATCH");
ratchetLane_->setLaneType(ArpLaneType::kRatchet);
ratchetLane_->setAccentColor(CColor{152, 128, 176, 255});  // Lavender
// ...

// Modifier lane (~60px = 16px header + 44px body)
modifierLane_ = new ArpModifierLane(CRect(0, 0, 500, 60), nullptr, -1);
modifierLane_->setLaneName("MOD");
modifierLane_->setAccentColor(CColor{192, 112, 124, 255});  // Rose
// ...

// Condition lane (~44px = 16px header + 28px body)
conditionLane_ = new ArpConditionLane(CRect(0, 0, 500, 44), nullptr, -1);
conditionLane_->setLaneName("COND");
conditionLane_->setAccentColor(CColor{124, 144, 176, 255});  // Slate
// ...
```

**controller.cpp setParamNormalized()**: Add 4 new blocks for parameter sync from host to lanes.

**controller.cpp playbackPollTimer_**: Add 4 new playhead polling blocks.

#### 6.4 Named Colors in editor.uidesc (FR-045)

Add color definitions in the uidesc XML:
```xml
<color name="arp.pitch" rgba="#6CA8A0FF"/>
<color name="arp.ratchet" rgba="#9880B0FF"/>
<color name="arp.modifier" rgba="#C0707CFF"/>
<color name="arp.condition" rgba="#7C90B0FF"/>
```

---

### Phase 7: Testing Strategy

#### 7.1 New Test Files

**test_arp_lane_header.cpp**:
- Header drawing computes correct triangle direction
- Header click in collapse zone toggles state
- Header click in length dropdown zone fires callback
- Header click outside zones returns false

**test_arp_modifier_lane.cpp**:
- Default step flags = 0x01 (kStepActive)
- Toggle Rest flips bit 0 (kStepActive)
- Toggle Tie sets bit 1
- Toggle Slide sets bit 2
- Toggle Accent sets bit 3
- Multiple flags per step
- Bitmask parameter encoding: `(flags & 0x0F) / 15.0f` (denominator is 15, not 255)
- High-bit masking: input 0xFF stores as 0x0F; input 0xF0 stores as 0x00
- getExpandedHeight returns 60.0f (kBodyHeight 44.0f + kHeight 16.0f)
- Collapsed height = header height (16.0f)

**test_arp_condition_lane.cpp**:
- Default step conditions = 0 (Always)
- Set condition index 0-17
- Out-of-range index clamped to 0
- Abbreviation lookup correctness (kConditionAbbrev)
- Tooltip text correctness (kConditionTooltips -- distinct from kConditionFullNames)
- Normalized encoding: `index / 17.0f`
- getExpandedHeight returns 44.0f (kBodyHeight 28.0f + kHeight 16.0f)
- Collapsed height = header height (16.0f)

#### 7.2 Extended Existing Tests

**test_arp_lane_editor.cpp** additions:
- Bipolar mode: center line at 0.5 normalized
- Bipolar mode: positive values above center
- Bipolar mode: negative values below center
- Bipolar mode: snapping to integer semitones
- Discrete mode: click cycles 1->2->3->4->1
- Discrete mode: right-click resets to 1
- IArpLane interface methods work correctly

**test_arp_lane_container.cpp** additions:
- Container accepts IArpLane* (not just ArpLaneEditor*)
- Layout works with mixed lane types
- Collapse callback from IArpLane triggers relayout
- No dynamic_cast calls in container (static code review, T090b)

**ruinae_tests additions (G1, FR-047)**:
- VST3 state round-trip: getState() / setState() preserves all step values for all 4 new lane types
- setParamNormalized() dispatches correctly for step params, length params, and playhead params of all 4 new lanes

---

### Risk Mitigation

| Risk | Mitigation |
|------|------------|
| ArpLaneEditor refactor breaks existing velocity/gate | Run all existing tests after each refactor step. No behavior change for kVelocity/kGate modes. |
| ArpLaneContainer type change breaks existing controller code | The controller creates ArpLaneEditor* which now implements IArpLane. addLane(IArpLane*) accepts it. |
| COptionMenu popup behavior differences across platforms | Reuse proven pattern from openLengthDropdown(). Cross-platform tested via pluginval. |
| Tooltip update on hover lag | setTooltipText() is lightweight. CTooltipSupport reads on timer (~500ms delay is normal). |
| Header extraction introduces regression | Header behavior is unit-tested independently. Visual comparison before/after. |
| Modifier bitmask encoding mismatch with engine | Use same constants (kStepActive=0x01, etc.) and verify against arpeggiator_core.h. Normalization denominator is 15 (4-bit range), NOT 255. |
| Condition index normalization mismatch | Denominator is 17 (index range 0-17), yielding normalizedValue = index/17.0f. Decode: clamp(round(normalized * 17.0f), 0, 17). |
| State save/load regression for new lane types | Implement and run ruinae_tests getState()/setState() round-trip test (T089) before claiming FR-047 met. |
| Multiple inheritance (CControl + IArpLane) | IArpLane is a pure interface with virtual destructor. No diamond problem. Standard C++ pattern. |
