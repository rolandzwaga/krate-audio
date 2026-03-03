# Quickstart: OscillatorTypeSelector Implementation

**Feature**: 050-oscillator-selector | **Date**: 2026-02-11

## Overview

This guide provides the implementation order, key patterns, and gotchas for building the OscillatorTypeSelector control.

## Implementation Order

### Phase 1: Core Logic (Testable, No VSTGUI)

1. **Value conversion functions** -- `oscTypeIndexFromNormalized()`, `normalizedFromOscTypeIndex()`, display name tables
2. **NaN/inf sanitization** -- FR-042 defensive handling
3. **Grid hit testing** -- `hitTestPopupCell()` pure geometry function
4. **Waveform icon points** -- `OscWaveformIcons::getIconPath()` for all 10 types
5. **Unit tests for all of the above** (Catch2, in `plugins/shared/tests/test_oscillator_type_selector.cpp`)

### Phase 2: Collapsed Control

6. **Class skeleton** -- `OscillatorTypeSelector : public CControl` with constructor, CLASS_METHODS
7. **draw() for collapsed state** -- background, border, icon, name, arrow
8. **onMouseDown** -- toggle popup open/close
9. **onMouseWheelEvent** -- increment/decrement with wrap
10. **Hover state** -- border highlight on mouse enter/exit
11. **Focus indicator** -- dotted border when focused
12. **ViewCreator** -- `OscillatorTypeSelectorCreator` struct + inline global

### Phase 3: Popup Overlay

13. **openPopup()** -- create CViewContainer, compute smart position, add to CFrame
14. **closePopup()** -- remove from CFrame, cleanup
15. **IMouseObserver** -- register on open, intercept clicks outside popup
16. **IKeyboardHook** -- register on open, handle Escape/Enter/arrows
17. **Popup drawing** -- background, grid cells, icons, labels, selection highlight, hover
18. **Click cell** -- select type, begin/perform/endEdit, close
19. **Scroll in popup** -- change selection, keep open (FR-020)
20. **Multi-instance** -- static sOpenInstance_ tracker (FR-041)
21. **Per-cell tooltips** -- onMouseMoved grid arithmetic + setTooltipText (FR-043)

### Phase 4: Integration

22. **CMakeLists** -- add header to `KratePluginsShared`
23. **Ruinae entry.cpp** -- add include for ViewCreator registration
24. **Control testbench** -- add include + demo instances
25. **Manual verification** -- build, run testbench, verify all visual states

## Key Patterns to Follow

### ViewCreator (Copy from ArcKnob)

```cpp
// At bottom of oscillator_type_selector.h, after the class definition:

struct OscillatorTypeSelectorCreator : VSTGUI::ViewCreatorAdapter {
    OscillatorTypeSelectorCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override {
        return "OscillatorTypeSelector";
    }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Oscillator Type Selector";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new OscillatorTypeSelector(VSTGUI::CRect(0, 0, 180, 28), nullptr, -1);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* /*description*/) const override {
        auto* sel = dynamic_cast<OscillatorTypeSelector*>(view);
        if (!sel) return false;

        if (auto identity = attributes.getAttributeValue("osc-identity"))
            sel->setIdentity(*identity);

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("osc-identity");
        return true;
    }

    AttrType getAttributeType(const std::string& attributeName) const override {
        if (attributeName == "osc-identity") return kStringType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* /*desc*/) const override {
        auto* sel = dynamic_cast<OscillatorTypeSelector*>(view);
        if (!sel) return false;
        if (attributeName == "osc-identity") {
            stringValue = sel->getIdentity();
            return true;
        }
        return false;
    }
};

inline OscillatorTypeSelectorCreator gOscillatorTypeSelectorCreator;
```

### Popup Overlay (CFrame addView/removeView)

```cpp
void openPopup() {
    if (popupOpen_) return;

    // Close any other open instance
    if (sOpenInstance_ && sOpenInstance_ != this)
        sOpenInstance_->closePopup();

    auto* frame = getFrame();
    if (!frame) return;

    // Compute popup position (smart 4-corner fallback)
    CRect popupRect = computePopupRect();

    // Create popup view
    popupView_ = new CViewContainer(popupRect);
    popupView_->setBackground(nullptr);  // We draw manually
    // ... configure popup ...

    frame->addView(popupView_);

    // Register modal hooks
    frame->registerMouseObserver(this);
    frame->registerKeyboardHook(this);

    popupOpen_ = true;
    sOpenInstance_ = this;
    focusedCell_ = getCurrentIndex();  // Start focus on current selection
}

void closePopup() {
    if (!popupOpen_) return;

    auto* frame = getFrame();
    if (frame) {
        frame->unregisterKeyboardHook(this);
        frame->unregisterMouseObserver(this);
        if (popupView_) {
            frame->removeView(popupView_, true);  // true = delete
            popupView_ = nullptr;
        }
    }

    popupOpen_ = false;
    if (sOpenInstance_ == this)
        sOpenInstance_ = nullptr;

    hoveredCell_ = -1;
    focusedCell_ = -1;
    invalidRect(getViewSize());  // Redraw collapsed state
}
```

### beginEdit/performEdit/endEdit Gesture

```cpp
void selectType(int index) {
    float newValue = normalizedFromOscTypeIndex(index);
    beginEdit();
    setValue(newValue);
    valueChanged();
    endEdit();
    invalid();  // Trigger redraw
}
```

### NaN Defense (FR-042)

```cpp
inline int oscTypeIndexFromNormalized(float value) {
    if (std::isnan(value) || std::isinf(value))
        value = 0.5f;
    value = std::clamp(value, 0.0f, 1.0f);
    return static_cast<int>(std::round(value * 9.0f));
}
```

## Critical Gotchas

1. **IMouseObserver::onMouseEvent vs CControl::onMouseDown**: The IMouseObserver callback fires for ALL frame mouse events. The CControl methods only fire for events on the control itself. When the popup is open, you need BOTH: the IMouseObserver to detect clicks outside, and the popup view to handle clicks inside.

2. **IKeyboardHook has 2 params, CView::onKeyboardEvent has 1**: These are separate virtual functions. The hook version receives a CFrame* as well. Make sure both are implemented when the class inherits both CControl and IKeyboardHook.

3. **CFrame::removeView with withForget=true**: Pass `true` as the second argument to `removeView()` to also `forget()` (release) the view. Since the frame took ownership when `addView()` was called, this properly cleans up.

4. **Popup coordinates are in FRAME space**: When adding a view to CFrame, the rect must be in frame coordinates, not parent-relative coordinates. Use `localToFrame()` or `getViewSize()` (which is already in frame coords for views added to CFrame).

5. **std::isnan with -ffast-math**: On some compilers with fast-math enabled, `std::isnan()` may be optimized away. If this becomes an issue, use bit manipulation. However, the shared UI code is unlikely to have fast-math enabled.

6. **Static sOpenInstance_ lifetime**: Since this is `static inline`, it persists across editor open/close cycles. Always null-check and validate that the instance is still valid. The destructor should clear it.

7. **Scroll wheel delta normalization**: Different platforms report different scroll deltas. Use only the sign of the delta (positive = up = increment, negative = down = decrement). Each event = exactly 1 step regardless of delta magnitude.

## Build and Test Commands

```bash
# Build shared library and tests
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target KratePluginsShared

# Build and run shared tests
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target shared_tests
build/windows-x64-release/plugins/shared/tests/Release/shared_tests.exe

# Build control testbench
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target control_testbench

# Build Ruinae plugin
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae
```

## File Checklist

| File | Action | Status |
|------|--------|--------|
| `plugins/shared/src/ui/oscillator_type_selector.h` | CREATE | New control header |
| `plugins/shared/tests/test_oscillator_type_selector.cpp` | CREATE | Unit tests |
| `plugins/shared/tests/CMakeLists.txt` | MODIFY | Add test file |
| `plugins/shared/CMakeLists.txt` | MODIFY | Add header to source list |
| `plugins/ruinae/src/entry.cpp` | MODIFY | Add `#include "ui/oscillator_type_selector.h"` |
| `tools/control_testbench/src/control_registry.cpp` | MODIFY | Add include + demo instances |
