# Research: OscillatorTypeSelector

**Feature**: 050-oscillator-selector | **Date**: 2026-02-11

## Research Tasks and Findings

### R1: VSTGUI Popup Overlay Pattern (CFrame addView/removeView)

**Decision**: Use `CFrame::addView()` / `CFrame::removeView()` to create a floating popup overlay, matching how VSTGUI's own `COptionMenu` and generic option menu implementations work.

**Rationale**: The VSTGUI SDK's own `genericoptionmenu.cpp` uses `CFrame::addView()` to overlay popup content on top of the frame, registering a mouse observer for click-outside dismissal. This is the established, cross-platform pattern. No platform-specific windowing is needed.

**Implementation Pattern**:
1. When popup opens: create a `CViewContainer` with the grid content, compute position relative to the frame, call `getFrame()->addView(popupView)`.
2. The popup container handles its own drawing (background, grid cells, icons).
3. When popup closes: `getFrame()->removeView(popupView)`.
4. Memory: The popup is created on open and destroyed on close. The control owns the popup pointer while it is open.

**Alternatives Considered**:
- COptionMenu: Too limited -- only supports text items, no custom grid layout or waveform icons.
- Separate CFrame/window: Overkill for a simple popup, not cross-platform friendly.
- Child CViewContainer within the control's own bounds: Cannot overlay other UI elements, clips to parent.

---

### R2: Modal Mouse Hook Pattern (IMouseObserver)

**Decision**: Register an `IMouseObserver` on `CFrame` when the popup opens, intercept mouse-down events outside the popup to close it. Unregister when popup closes.

**Rationale**: This is exactly what VSTGUI's `genericoptionmenu.cpp` does (verified in source at `extern/vst3sdk/vstgui4/vstgui/lib/platform/common/genericoptionmenu.cpp` lines 106-116). The `IMouseObserver::onMouseEvent()` callback receives all mouse events on the frame, allowing the control to intercept clicks outside the popup area.

**Implementation Pattern**:
```
class OscillatorTypeSelector : public CControl, public IMouseObserver, public IKeyboardHook {
    // IMouseObserver
    void onMouseEvent(MouseEvent& event, CFrame* frame) override {
        if (event.type == EventType::MouseDown && popupOpen_) {
            CPoint pos = event.mousePosition;
            if (!popupRect_.pointInside(pos)) {
                closePopup();
                event.consumed = true;
            }
        }
    }
    void onMouseEntered(CView*, CFrame*) override {}
    void onMouseExited(CView*, CFrame*) override {}
};
```

**Alternatives Considered**:
- `CView::hitTest()` override on a transparent overlay: More complex, unreliable across platforms.
- Timer-based polling: Fragile, not event-driven.

---

### R3: Keyboard Hook Pattern (IKeyboardHook)

**Decision**: Register an `IKeyboardHook` on `CFrame` when the popup opens, intercept Escape key to close the popup and arrow keys for navigation. Unregister when popup closes.

**Rationale**: The `IKeyboardHook` interface (verified in `cframe.h` lines 347-354) provides `onKeyboardEvent(KeyboardEvent&, CFrame*)`. Setting `event.consumed = true` prevents the event from propagating further. This is the standard VSTGUI pattern for modal keyboard interception.

**Implementation Pattern**:
```
void onKeyboardEvent(KeyboardEvent& event, CFrame* frame) override {
    if (!popupOpen_ || event.type != EventType::KeyDown) return;

    if (event.virt == VirtualKey::Escape) {
        closePopup();
        event.consumed = true;
    } else if (event.virt == VirtualKey::Return || event.virt == VirtualKey::Space) {
        selectFocusedCell();
        closePopup();
        event.consumed = true;
    } else if (event.virt == VirtualKey::Left/Right/Up/Down) {
        navigateFocus(direction);
        event.consumed = true;
    }
}
```

**Alternatives Considered**:
- `onKeyDown()` override on the control itself: Only receives events when the control has focus, but during popup the focus model is unclear.
- Old `VstKeyCode` API: Deprecated, new `KeyboardEvent` is preferred.

---

### R4: Programmatic Waveform Icon Drawing (CGraphicsPath)

**Decision**: Use `CDrawContext::createGraphicsPath()` to create `CGraphicsPath` objects for each waveform icon. Icons are defined as static point arrays (5-10 points per icon) that are scaled to the target rect at draw time.

**Rationale**: `CGraphicsPath` is VSTGUI's cross-platform vector drawing API, used extensively by existing controls (ArcKnob uses `addArc`, ADSRDisplay uses `addLine` and curves). Path objects are created per-draw-call using `context->createGraphicsPath()` and managed with `VSTGUI::owned()`.

**Implementation Pattern (Humble Object - FR-038)**:
```
namespace OscWaveformIcons {
    // Testable: returns normalized points [0,1] for a given OscType
    struct PathPoint { float x; float y; };
    using PointList = std::array<PathPoint, 10>; // max points per icon

    struct WaveformPath {
        std::array<PathPoint, 10> points;
        int count;
    };

    WaveformPath getIconPoints(Krate::DSP::OscType type);

    // VSTGUI-dependent: draws the icon using CGraphicsPath
    void drawIcon(CDrawContext* ctx, const CRect& rect,
                  Krate::DSP::OscType type, const CColor& strokeColor);
}
```

The `getIconPoints()` function is testable without VSTGUI. The `drawIcon()` function scales the normalized points to the target rect and draws with `CGraphicsPath`.

**Alternatives Considered**:
- Bitmap icons: Violates FR-004 (must be programmatic), won't scale with DPI.
- SVG parsing: Overkill for 5-10 point paths, adds dependency.
- Pre-computed `CGraphicsPath` cache: Unnecessary for simple paths; `createGraphicsPath()` per draw call is fine for 10-20 icons at UI frame rates.

---

### R5: ViewCreator Registration Pattern

**Decision**: Follow the established `ViewCreatorAdapter` pattern with `inline` global variable for auto-registration. The `osc-identity` attribute will be a string type (`"a"` or `"b"`) that maps to the identity color.

**Rationale**: All existing shared controls (ArcKnob, XYMorphPad, ADSRDisplay, StepPatternEditor, etc.) use this exact pattern:
1. A struct inheriting from `VSTGUI::ViewCreatorAdapter`
2. Constructor calls `UIViewFactory::registerViewCreator(*this)`
3. An `inline` C++17 global variable at the end of the header triggers registration on include
4. Each plugin's `entry.cpp` includes the header to trigger registration

**Implementation Pattern**:
```
struct OscillatorTypeSelectorCreator : VSTGUI::ViewCreatorAdapter {
    OscillatorTypeSelectorCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }
    IdStringPtr getViewName() const override { return "OscillatorTypeSelector"; }
    IdStringPtr getBaseViewName() const override { return UIViewCreator::kCControl; }
    UTF8StringPtr getDisplayName() const override { return "Oscillator Type Selector"; }

    CView* create(...) const override {
        return new OscillatorTypeSelector(CRect(0, 0, 180, 28), nullptr, -1);
    }

    bool apply(CView* view, const UIAttributes& attributes, ...) const override {
        auto* sel = dynamic_cast<OscillatorTypeSelector*>(view);
        if (!sel) return false;

        // Custom attribute: osc-identity ("a" or "b")
        if (auto identity = attributes.getAttributeValue("osc-identity")) {
            sel->setIdentity(*identity);
        }
        return true;
    }
};
inline OscillatorTypeSelectorCreator gOscillatorTypeSelectorCreator;
```

**Alternatives Considered**:
- IController/DelegationController sub-controller: Overkill for a single attribute. ViewCreator is simpler.
- Color attribute instead of string identity: Less intuitive in uidesc XML. The string maps to a well-known color pair.

---

### R6: Smart Popup Positioning (4-Corner Fallback)

**Decision**: Compute the popup rect in all 4 positions relative to the collapsed control, test each against the frame bounds, and use the first one that fits entirely.

**Rationale**: FR-015 requires 4-corner priority: (1) below-left, (2) below-right, (3) above-left, (4) above-right. This is a simple geometry calculation with no VSTGUI API dependencies beyond getting the frame's view size.

**Implementation Pattern**:
```
CRect computePopupRect() const {
    CRect controlRect = getViewSize();
    CRect frameRect = getFrame()->getViewSize();
    CCoord popW = 260, popH = 94;

    // 4 candidate positions
    CRect candidates[4] = {
        // Below-left: popup top-left at control bottom-left
        CRect(controlRect.left, controlRect.bottom,
              controlRect.left + popW, controlRect.bottom + popH),
        // Below-right: popup top-right at control bottom-right
        CRect(controlRect.right - popW, controlRect.bottom,
              controlRect.right, controlRect.bottom + popH),
        // Above-left: popup bottom-left at control top-left
        CRect(controlRect.left, controlRect.top - popH,
              controlRect.left + popW, controlRect.top),
        // Above-right: popup bottom-right at control top-right
        CRect(controlRect.right - popW, controlRect.top - popH,
              controlRect.right, controlRect.top),
    };

    for (auto& rect : candidates) {
        if (frameRect.rectInside(rect))  // Use rectInside or manual bounds check
            return rect;
    }
    return candidates[0]; // Default: below-left, clip gracefully
}
```

**Alternatives Considered**:
- Always below-left: Fails when control is near window bottom.
- Separate CFrame window: Overkill, cross-platform issues.

---

### R7: Multi-Instance Atomic Popup Switching (FR-041)

**Decision**: Use a `static` class variable to track the currently open popup instance. When any instance opens a popup, it first closes the existing one.

**Rationale**: Standard dropdown behavior requires only one popup open at a time. A static pointer to the currently-open instance is the simplest solution. When OSC B's collapsed control is clicked while OSC A's popup is open, the click handler first calls `sOpenInstance_->closePopup()`, then opens the new popup.

**Implementation Pattern**:
```
class OscillatorTypeSelector : public CControl, ... {
    static inline OscillatorTypeSelector* sOpenInstance_ = nullptr;

    void openPopup() {
        if (sOpenInstance_ && sOpenInstance_ != this)
            sOpenInstance_->closePopup();
        // ... open this popup
        sOpenInstance_ = this;
    }

    void closePopup() {
        // ... close popup
        sOpenInstance_ = nullptr;
    }
};
```

**Alternatives Considered**:
- Frame-level popup manager: Unnecessary complexity for 2 instances.
- Event broadcasting: No existing event bus in the codebase.

---

### R8: Dynamic Per-Cell Tooltips (FR-043)

**Decision**: Override `onMouseMoved()` in the popup handling, compute the hovered cell index via grid arithmetic, and call `setTooltipText()` with the display name for that cell.

**Rationale**: VSTGUI's `CView::setTooltipText()` (verified in `cview.h` line 397) allows changing the tooltip text at any time. Since the popup is a single view (not 10 child views), we use `onMouseMoved()` to determine which cell the cursor is over and update the tooltip dynamically.

**Implementation Pattern**:
The popup overlay view (or the control itself while popup is open) tracks mouse position, computes cell index, and calls `popupView_->setTooltipText(oscTypeDisplayName(index))`. The tooltip text changes as the user moves between cells. Moving outside any cell clears the tooltip.

**Alternatives Considered**:
- 10 separate child CView objects each with a tooltip: More memory, more complex hit testing, harder to maintain.
- Custom tooltip rendering: Overkill; VSTGUI's built-in tooltip system handles positioning and display.

---

### R9: Scroll Wheel Behavior on Collapsed vs Popup

**Decision**: On the collapsed control, scroll wheel increments/decrements selection by 1 step with wrapping, issues full begin/perform/end edit gesture, and does NOT open the popup. When the popup IS open, scroll wheel changes selection by 1 step, keeps the popup open, and updates the visual highlight in the grid.

**Rationale**: FR-013 specifies closed-state scroll changes selection without opening popup. FR-020 specifies popup-open scroll keeps popup open for rapid auditioning. Both wrap at boundaries (0 wraps to 9, 9 wraps to 0).

**Implementation**: `onMouseWheelEvent()` handler:
1. Determine scroll direction (positive = increment, negative = decrement)
2. Clamp delta to +/- 1 regardless of magnitude
3. Compute new index with wrapping: `newIndex = (currentIndex + delta + 10) % 10`
4. Issue beginEdit/performEdit/endEdit
5. If popup is open: update selection highlight, do NOT close popup
6. If popup is closed: just update collapsed display

**Alternatives Considered**:
- Scroll opens popup: Rejected by clarification (user wants quick cycling without popup).
- Scroll magnitude maps to step count: Rejected -- each scroll event = exactly 1 step (edge case handling).

---

### R10: Control Testbench Integration

**Decision**: Add two demo instances to the control testbench registry: one configured as OSC A (blue, `osc-identity="a"`) and one as OSC B (orange, `osc-identity="b"`), each bound to mock parameter IDs.

**Rationale**: The testbench `control_registry.cpp` includes shared control headers at the top for ViewCreator auto-registration, then uses `ControlRegistry::registerControl()` to add demo entries. Each entry provides a factory lambda that creates and configures a control instance.

**Integration Points**:
1. Add `#include "ui/oscillator_type_selector.h"` to `control_registry.cpp`
2. Register two demo controls in the registry
3. Add mock `kTestOscATypeId` and `kTestOscBTypeId` constants if needed (or reuse existing testbench parameter allocation)

**Alternatives Considered**: None -- this is the established pattern for all shared controls in the testbench.
