# API Contract: OscillatorTypeSelector

**Feature**: 050-oscillator-selector | **Date**: 2026-02-11

## Class: OscillatorTypeSelector

**Header**: `plugins/shared/src/ui/oscillator_type_selector.h`
**Namespace**: `Krate::Plugins`
**Base**: `VSTGUI::CControl`
**Interfaces**: `VSTGUI::IMouseObserver`, `VSTGUI::IKeyboardHook`

---

### Constructors

```cpp
// Default constructor for ViewCreator
OscillatorTypeSelector(const VSTGUI::CRect& size,
                       VSTGUI::IControlListener* listener = nullptr,
                       int32_t tag = -1);

// Copy constructor (VSTGUI requirement for ViewCreator)
OscillatorTypeSelector(const OscillatorTypeSelector& other);
```

---

### Public API

#### Identity Configuration

```cpp
// Set the oscillator identity (determines highlight color)
// identity: "a" = blue rgb(100,180,255), "b" = orange rgb(255,140,100)
void setIdentity(const std::string& identity);

// Get the current identity string
[[nodiscard]] const std::string& getIdentity() const;

// Get the resolved identity color
[[nodiscard]] VSTGUI::CColor getIdentityColor() const;
```

#### State Query

```cpp
// Get the current oscillator type index (0-9)
[[nodiscard]] int getCurrentIndex() const;

// Get the current oscillator type enum value
[[nodiscard]] Krate::DSP::OscType getCurrentType() const;

// Whether the popup is currently open
[[nodiscard]] bool isPopupOpen() const;
```

---

### CControl Overrides

```cpp
// Drawing
void draw(VSTGUI::CDrawContext* context) override;

// Mouse events on the collapsed control
VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
void onMouseWheelEvent(VSTGUI::MouseWheelEvent& event) override;
void onMouseEnterEvent(VSTGUI::MouseEnterEvent& event) override;
void onMouseExitEvent(VSTGUI::MouseExitEvent& event) override;
void onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) override;

// Focus
bool getFocusPath(VSTGUI::CGraphicsPath& outPath) override;

// Keyboard (collapsed control, when focused)
void onKeyboardEvent(VSTGUI::KeyboardEvent& event) override;

// VSTGUI class methods
CLASS_METHODS(OscillatorTypeSelector, CControl)
```

---

### IMouseObserver Overrides (Modal Popup Dismissal)

```cpp
// Called for ALL mouse events on the frame (while popup is open)
void onMouseEvent(VSTGUI::MouseEvent& event, VSTGUI::CFrame* frame) override;

// Required but unused
void onMouseEntered(VSTGUI::CView* view, VSTGUI::CFrame* frame) override;
void onMouseExited(VSTGUI::CView* view, VSTGUI::CFrame* frame) override;
```

---

### IKeyboardHook Overrides (Modal Keyboard Interception)

```cpp
// Called for ALL keyboard events on the frame (while popup is open)
// Intercepts: Escape (close), Enter/Space (select), Arrow keys (navigate)
void onKeyboardEvent(VSTGUI::KeyboardEvent& event, VSTGUI::CFrame* frame) override;
```

Note: There is a name collision between `CView::onKeyboardEvent` (single arg) and `IKeyboardHook::onKeyboardEvent` (two args). These are separate overrides distinguished by parameter count.

---

### ViewCreator Custom Attributes

| Attribute | Type | Default | Description |
|-----------|------|---------|-------------|
| `osc-identity` | string | `"a"` | `"a"` = blue identity, `"b"` = orange identity |

Usage in editor.uidesc XML:
```xml
<view class="OscillatorTypeSelector"
      origin="10, 50"
      size="180, 28"
      control-tag="OSC A Type"
      osc-identity="a"
      default-value="0"
      min-value="0"
      max-value="1" />
```

---

## Free Functions: Value Conversion

**Location**: `plugins/shared/src/ui/oscillator_type_selector.h` (namespace `Krate::Plugins`)

```cpp
// Convert normalized parameter value to integer index with NaN/inf defense
// FR-042: NaN/inf treated as 0.5, value clamped to [0,1], then round(value * 9)
[[nodiscard]] inline int oscTypeIndexFromNormalized(float value);

// Convert integer index to normalized parameter value
[[nodiscard]] inline float normalizedFromOscTypeIndex(int index);

// Get the full display name for a type index
[[nodiscard]] inline const char* oscTypeDisplayName(int index);

// Get the abbreviated popup label for a type index
[[nodiscard]] inline const char* oscTypePopupLabel(int index);
```

---

## Namespace: OscWaveformIcons (Humble Object)

**Location**: `plugins/shared/src/ui/oscillator_type_selector.h` (nested namespace `Krate::Plugins::OscWaveformIcons`)

### Testable (No VSTGUI Dependency)

```cpp
// A normalized 2D point (x, y in [0, 1])
struct NormalizedPoint {
    float x;
    float y;
};

// A waveform icon path as a sequence of normalized points
struct IconPath {
    std::array<NormalizedPoint, 12> points;  // Max 12 points per icon
    int count = 0;                           // Actual number of points
    bool closePath = false;                  // Whether to close the path
};

// Get the normalized point data for a given oscillator type's waveform icon
// Returns points in [0,1] x [0,1] coordinate space
// FR-038: This is the testable function -- no VSTGUI dependency
[[nodiscard]] IconPath getIconPath(Krate::DSP::OscType type);
```

### VSTGUI-Dependent (Drawing)

```cpp
// Draw a waveform icon into the given rectangle
// Uses CGraphicsPath for cross-platform vector drawing
// FR-005: 1.5px anti-aliased stroke, no fill
// FR-007: Same function for collapsed (20x14) and popup (48x26) sizes
void drawIcon(VSTGUI::CDrawContext* context,
              const VSTGUI::CRect& targetRect,
              Krate::DSP::OscType type,
              const VSTGUI::CColor& strokeColor);
```

---

## ViewCreator Registration

**Struct**: `OscillatorTypeSelectorCreator`
**Global**: `inline OscillatorTypeSelectorCreator gOscillatorTypeSelectorCreator;`

Follows the established pattern from `ArcKnobCreator` (`plugins/shared/src/ui/arc_knob.h`). Include in plugin's `entry.cpp` and testbench `control_registry.cpp` to auto-register.

---

## Integration Points

### Ruinae Plugin (`plugins/ruinae/src/entry.cpp`)

Add include:
```cpp
#include "ui/oscillator_type_selector.h"
```

### Shared CMakeLists (`plugins/shared/CMakeLists.txt`)

Add to UI Components section:
```cmake
src/ui/oscillator_type_selector.h
```

### Control Testbench (`tools/control_testbench/src/control_registry.cpp`)

Add include:
```cpp
#include "ui/oscillator_type_selector.h"
```

Register two demo instances in the registry or createView function.

### Shared Tests (`plugins/shared/tests/`)

Add test file:
```
test_oscillator_type_selector.cpp
```

Add to `plugins/shared/tests/CMakeLists.txt`:
```cmake
test_oscillator_type_selector.cpp
```

---

## Parameter IDs (Existing)

| Parameter | ID | Location |
|-----------|----|----------|
| OSC A Type | `kOscATypeId = 100` | `plugins/ruinae/src/plugin_ids.h` |
| OSC B Type | `kOscBTypeId = 200` | `plugins/ruinae/src/plugin_ids.h` |

Both registered as `StringListParameter` with 10 entries in the Ruinae controller.
