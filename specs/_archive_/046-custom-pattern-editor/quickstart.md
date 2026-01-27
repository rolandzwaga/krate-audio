# Quickstart: Custom Tap Pattern Editor

**Feature**: 046-custom-pattern-editor
**Date**: 2026-01-04

## Prerequisites

Before starting implementation:

1. Read `specs/TESTING-GUIDE.md` - Test patterns and practices
2. Read `specs/VST-GUIDE.md` - VSTGUI pitfalls and patterns
3. Verify branch: `git checkout 046-custom-pattern-editor`
4. Build current state: `cmake --build build --config Debug`

---

## Implementation Order

### Phase 1: Core View Framework (3-4 hours)

**Goal**: Get a basic view rendering in the UI.

```bash
# Files to create
plugins/iterum/src/ui/tap_pattern_editor.h
plugins/iterum/src/ui/tap_pattern_editor.cpp
```

**Steps**:
1. Create `TapPatternEditor` class inheriting from `CControl`
2. Implement minimal `draw()` with placeholder rectangle
3. Register in `Controller::createCustomView()`
4. Add to `editor.uidesc` with `custom-view-name="TapPatternEditor"`
5. Build and verify view appears

**Test**: View renders as colored rectangle in MultiTap panel.

---

### Phase 2: Parameter Integration (4-5 hours)

**Goal**: Wire up 32 VST3 parameters for custom pattern data.

```bash
# Files to modify
plugins/iterum/src/plugin_ids.h          # Add parameter IDs
plugins/iterum/src/parameters/multitap_params.h  # Add storage + handlers
plugins/iterum/src/controller/controller.cpp     # Register parameters
plugins/iterum/src/processor/processor.cpp       # Handle in audio thread
```

**Steps**:
1. Add parameter ID constants (950-981)
2. Add `customTimeRatios[16]` and `customLevels[16]` atomics to MultiTapParams
3. Add parameter handlers in `handleMultiTapParamChange()`
4. Register parameters in `registerMultiTapParams()`
5. Add state save/load in `saveMultiTapParams()`/`loadMultiTapParams()`
6. Build and verify parameters appear in host

**Test**: Parameters visible in host automation list.

---

### Phase 3: Drawing Implementation (4-5 hours)

**Goal**: Render taps as vertical bars with grid.

**Steps**:
1. Implement `drawBackground()` - panel with border
2. Implement `drawGridLines()` - vertical time divisions
3. Implement `drawTaps()` - vertical bars for each tap
4. Add tap count awareness (only draw active taps)
5. Add labels (0%, 100%, time axis)

**Test**: Taps render at correct positions based on parameter values.

---

### Phase 4: Mouse Interaction (4-5 hours)

**Goal**: Enable tap dragging to modify time and level.

**Steps**:
1. Implement `hitTestTap()` - detect which tap is clicked
2. Implement `onMouseDown()` - select tap, begin edit
3. Implement `onMouseMoved()` - update time/level during drag
4. Implement `onMouseUp()` - end edit
5. Add grid snapping logic
6. Add visual feedback (selected tap highlight)

**Test**: Dragging tap updates parameter values and produces audible changes.

---

### Phase 5: DSP Integration (3-4 hours)

**Goal**: Apply custom levels in DSP processing.

```bash
# Files to modify
dsp/include/krate/dsp/effects/multi_tap_delay.h
```

**Steps**:
1. Add `customLevels_` array to MultiTapDelay
2. Modify `setCustomTimingPattern()` to accept levels
3. Modify `applyCustomTimingPattern()` to apply levels
4. Add `setCustomLevelRatio(size_t tap, float level)` method
5. Wire processor to call DSP when custom level params change

**Test**: Custom levels produce expected volume changes in output.

---

### Phase 6: Visibility & Polish (3-4 hours)

**Goal**: Show/hide editor based on pattern selection, add buttons.

**Steps**:
1. Add visibility controller (show when pattern == Custom)
2. Add "Reset" button functionality
3. Add "Copy from Pattern" button
4. Add snap division dropdown
5. Test visibility toggling

**Test**: Editor appears only when Custom pattern selected.

---

## Key Code Patterns

### Creating Custom View (from ModeTabBar)

```cpp
// tap_pattern_editor.h
#pragma once

#include "vstgui/lib/ccontrol.h"
#include "vstgui/lib/cdrawcontext.h"

namespace Iterum {

class TapPatternEditor : public VSTGUI::CControl {
public:
    explicit TapPatternEditor(const VSTGUI::CRect& size);

    void draw(VSTGUI::CDrawContext* context) override;
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseUp(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;

private:
    static constexpr int kMaxTaps = 16;

    float tapTimeRatios_[kMaxTaps] = {};
    float tapLevels_[kMaxTaps] = {};
    int activeTapCount_ = 4;
    int selectedTap_ = -1;

    int hitTestTap(VSTGUI::CPoint point) const;
    VSTGUI::CRect getTapRect(int tapIndex) const;
};

} // namespace Iterum
```

### Registering in Controller

```cpp
// In Controller::createCustomView()
if (name == "TapPatternEditor") {
    return new TapPatternEditor(
        VSTGUI::CRect(0, 0, 400, 150)
    );
}
```

### Parameter Update Pattern

```cpp
// In TapPatternEditor, when tap is dragged:
void TapPatternEditor::updateTapTimeRatio(int tap, float ratio) {
    if (listener_) {
        beginEdit();
        tapTimeRatios_[tap] = ratio;
        setDirty();

        // Notify controller to update parameter
        auto controller = dynamic_cast<Controller*>(listener_);
        if (controller) {
            Steinberg::Vst::ParamID paramId =
                kMultiTapCustomTime0Id + tap;
            controller->setParamNormalized(paramId, ratio);
            controller->performEdit(paramId, ratio);
        }

        endEdit();
    }
}
```

---

## Testing Commands

```bash
# Build
cmake --build build --config Debug

# Run plugin tests
./build/bin/Debug/plugin_tests.exe "[multitap]"

# Run DSP tests
./build/bin/Debug/dsp_tests.exe "[multitap]"

# Validate plugin
tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Debug/Iterum.vst3"
```

---

## Edge Case Handling Patterns

### Value Clamping (Drag Outside Bounds)

```cpp
// In onMouseMoved() - clamp values when mouse goes outside bounds
CMouseEventResult TapPatternEditor::onMouseMoved(CPoint& where, const CButtonState& buttons) {
    if (selectedTap_ < 0) return kMouseEventNotHandled;

    // Convert to local coordinates
    CPoint local = where;
    local.offset(-getViewSize().left, -getViewSize().top);

    // Calculate ratios (may be outside 0-1 range if mouse outside bounds)
    float timeRatio = static_cast<float>(local.x) / getWidth();
    float levelRatio = 1.0f - static_cast<float>(local.y) / getHeight();

    // CRITICAL: Clamp to valid range
    timeRatio = std::clamp(timeRatio, 0.0f, 1.0f);
    levelRatio = std::clamp(levelRatio, 0.0f, 1.0f);

    // Apply (with optional Shift constraint)
    if (buttons.getModifierState() & CButtonState::kShift) {
        // Constrain to dominant axis
        float dx = std::abs(timeRatio - preDragTimeRatio_);
        float dy = std::abs(levelRatio - preDragLevelRatio_);
        if (dx > dy) {
            levelRatio = preDragLevelRatio_;  // Horizontal only
        } else {
            timeRatio = preDragTimeRatio_;    // Vertical only
        }
    }

    updateTapValues(selectedTap_, timeRatio, levelRatio);
    return kMouseEventHandled;
}
```

### Escape Key Cancellation

```cpp
// Store pre-drag values in onMouseDown()
void TapPatternEditor::onMouseDown(...) {
    if (selectedTap_ >= 0) {
        preDragTimeRatio_ = tapTimeRatios_[selectedTap_];
        preDragLevelRatio_ = tapLevels_[selectedTap_];
        isDragging_ = true;
    }
}

// Handle Escape in onKeyDown()
int32_t TapPatternEditor::onKeyDown(VstKeyCode& keyCode) {
    if (isDragging_ && keyCode.virt == VKEY_ESCAPE) {
        // Restore pre-drag values
        tapTimeRatios_[selectedTap_] = preDragTimeRatio_;
        tapLevels_[selectedTap_] = preDragLevelRatio_;
        isDragging_ = false;
        selectedTap_ = -1;
        endEdit();
        invalid();
        return 1;  // Key handled
    }
    return -1;  // Key not handled
}
```

### Double-Click Reset

```cpp
// In onMouseDown() - detect double-click
CMouseEventResult TapPatternEditor::onMouseDown(CPoint& where, const CButtonState& buttons) {
    if (buttons.isRightButton()) return kMouseEventNotHandled;  // Ignore right-click

    int tapIndex = hitTestTap(where);
    if (tapIndex < 0) return kMouseEventNotHandled;

    if (buttons.isDoubleClick()) {
        // Reset to default: evenly spaced, full level
        float defaultTime = static_cast<float>(tapIndex + 1) / (activeTapCount_ + 1);
        float defaultLevel = 1.0f;
        updateTapValues(tapIndex, defaultTime, defaultLevel);
        return kMouseEventHandled;
    }

    // Normal click - start drag
    selectedTap_ = tapIndex;
    // ... rest of drag setup
}
```

---

## Thread-Safe Visibility (FR-008) ⚠️ CRITICAL

**See: specs/VST-GUIDE.md Section 6 & 7**

The pattern editor must show/hide based on pattern selection. **NEVER call `setVisible()` from `setParamNormalized()`** - it can be called from any thread (automation, state loading), causing crashes.

### The WRONG Pattern (Causes Crashes)

```cpp
// BROKEN - DO NOT DO THIS
tresult PLUGIN_API Controller::setParamNormalized(ParamID id, ParamValue value) {
    auto result = EditControllerEx1::setParamNormalized(id, value);
    if (id == kMultiTapPatternId) {
        bool isCustom = (static_cast<int>(value * 20) == 19);
        patternEditor_->setVisible(isCustom);  // CRASH! Thread safety violation!
    }
    return result;
}
```

### The CORRECT Pattern (IDependent + Deferred Updates)

```cpp
// In controller.cpp - use VisibilityController from VST-GUIDE.md Section 6
class PatternEditorVisibilityController : public Steinberg::FObject {
public:
    PatternEditorVisibilityController(
        Steinberg::Vst::Parameter* patternParam,
        TapPatternEditor* editor)
    : patternParam_(patternParam), editor_(editor) {
        if (patternParam_) {
            patternParam_->addRef();
            patternParam_->addDependent(this);
            patternParam_->deferUpdate();  // Trigger initial visibility
        }
    }

    void deactivate() {
        if (isActive_.exchange(false, std::memory_order_acq_rel)) {
            if (patternParam_) {
                patternParam_->removeDependent(this);
            }
        }
    }

    ~PatternEditorVisibilityController() override {
        deactivate();
        if (patternParam_) {
            patternParam_->release();
            patternParam_ = nullptr;
        }
    }

    // Called on UI thread via UpdateHandler (30Hz)
    void PLUGIN_API update(FUnknown*, int32 message) override {
        if (!isActive_.load(std::memory_order_acquire)) return;
        if (message != IDependent::kChanged || !patternParam_ || !editor_) return;

        float normalized = patternParam_->getNormalized();
        int patternIndex = static_cast<int>(normalized * 20);
        bool isCustom = (patternIndex == 19);

        editor_->setVisible(isCustom);  // SAFE - UI thread
        if (editor_->getFrame()) editor_->invalid();
    }

    OBJ_METHODS(PatternEditorVisibilityController, FObject)

private:
    std::atomic<bool> isActive_{true};
    Steinberg::Vst::Parameter* patternParam_;
    TapPatternEditor* editor_;
};
```

### Lifecycle Safety in willClose()

```cpp
void Controller::willClose(VSTGUI::VST3Editor* editor) {
    // PHASE 1: Deactivate ALL controllers FIRST (stops pending updates)
    if (auto* vc = dynamic_cast<PatternEditorVisibilityController*>(
            patternEditorVisController_.get())) {
        vc->deactivate();
    }

    // PHASE 2: Clear editor pointer
    activeEditor_ = nullptr;

    // PHASE 3: Destroy controllers (now safe - no pending updates)
    patternEditorVisController_ = nullptr;
}
```

**Key Rules:**
- `removeDependent()` in `deactivate()`, NOT destructor
- Call `deactivate()` in `willClose()` BEFORE destroying
- Use atomic flag for double-check safety

---

## Common Pitfalls

1. **Forgetting beginEdit/endEdit**: Always wrap parameter changes
2. **Not setting font before drawString**: Causes crash or no text
3. **Using wrong parameter ID range**: Double-check 950-981
4. **Not invalidating on parameter change**: Call `invalid()` to trigger redraw
5. **Thread safety**: Don't access atomics with wrong memory order
6. **Not clamping values**: Mouse can go outside bounds; always clamp to 0.0-1.0
7. **Missing pre-drag storage**: Store values at drag start for Escape cancellation
8. **Ignoring modifier keys**: Check `buttons.getModifierState()` for Shift, etc.
9. **Calling setVisible() from wrong thread**: NEVER call from `setParamNormalized()` - use IDependent pattern (see above)
10. **removeDependent() in destructor**: Call in `deactivate()` method, not destructor - prevents use-after-free
11. **Missing willClose() cleanup**: Deactivate visibility controllers BEFORE destroying them
12. **Rapid editor open/close crashes**: Sign of lifecycle bugs - test with AddressSanitizer

---

## Reference Files

| Purpose | File |
|---------|------|
| CView example | `plugins/iterum/src/ui/mode_tab_bar.h` |
| Parameter patterns | `plugins/iterum/src/parameters/multitap_params.h` |
| createCustomView | `plugins/iterum/src/controller/controller.cpp` |
| DSP class | `dsp/include/krate/dsp/effects/multi_tap_delay.h` |
| Detailed plan | `specs/custom-pattern-editor-plan.md` |
| **VST pitfalls & patterns** | `specs/VST-GUIDE.md` (Sections 6-7 critical!) |
