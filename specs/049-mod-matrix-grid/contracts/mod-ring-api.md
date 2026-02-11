# Contract: ModRingIndicator API

**Spec**: 049-mod-matrix-grid | **Type**: Component API

## Overview

ModRingIndicator is a custom CView overlay placed as a sibling of destination ArcKnob instances in `.uidesc`. It renders colored modulation range arcs and supports click-to-select and hover tooltips. It observes modulation parameters via IDependent (no timer).

## Class Definition

```cpp
class ModRingIndicator : public VSTGUI::CView {
public:
    ModRingIndicator(const VSTGUI::CRect& size);
    ModRingIndicator(const ModRingIndicator& other);

    static constexpr int kMaxVisibleArcs = 4;

    struct ArcInfo {
        float amount;           // [-1.0, +1.0] bipolar modulation amount
        VSTGUI::CColor color;   // Source color from kModSources[]
        int sourceIndex;        // ModSource enum value
        int destIndex;          // ModDestination enum value
        bool bypassed;          // If true, arc is excluded from rendering
    };

    // --- Data API ---

    // Set the base value of the destination parameter (knob position, normalized)
    void setBaseValue(float normalizedValue);

    // Set modulation arcs (sorted by creation order, most recent last)
    // Bypassed arcs are filtered out before rendering.
    // If more than kMaxVisibleArcs non-bypassed arcs, the oldest are
    // merged into a single composite gray arc.
    void setArcs(const std::vector<ArcInfo>& arcs);

    // --- Controller Integration ---

    // Set controller reference for click-to-select mediation
    void setController(Steinberg::Vst::EditController* controller);

    // --- ViewCreator Attributes ---

    void setStrokeWidth(float width);   // Arc stroke width in pixels (default: 3.0)
    float getStrokeWidth() const;

    // --- CView Overrides ---

    void draw(CDrawContext* context) override;
    CMouseEventResult onMouseDown(CPoint& where, const CButtonState& buttons) override;
    CMouseEventResult onMouseMoved(CPoint& where, const CButtonState& buttons) override;

    CLASS_METHODS(ModRingIndicator, CView)
};
```

## Arc Rendering Rules

### Geometry

Arcs are drawn using `CGraphicsPath::addArc()` with the view's bounding rect, following ArcKnob's angle convention:

- **Min angle**: 135 degrees (7 o'clock position)
- **Max angle**: 405 degrees (5 o'clock position, or equivalently 45 degrees)
- **Direction**: Clockwise from min to max
- **Conversion**: `angle = 135.0 + normalizedValue * 270.0`

### Arc Position

Each arc's angular span is computed from the destination's current (base) value:

```
startAngle = valueToAngleDeg(baseValue)
endAngle   = valueToAngleDeg(clamp(baseValue + normalizedAmount, 0.0, 1.0))
```

Where `normalizedAmount = amount / 2.0` (converting bipolar [-1,+1] to normalized offset).

For negative amounts, `endAngle < startAngle`, and the arc extends counter-clockwise from the base value.

### Arc Radius

The arc radius is slightly smaller than the knob's main value arc to visually distinguish modulation arcs from the value indicator:

```
modArcRadius = knobArcRadius - strokeWidth - 1.0
```

### Stacking Rules (FR-022, FR-023, FR-025)

1. Up to `kMaxVisibleArcs = 4` individual arcs, each drawn in its source color
2. Most recently added route renders on top (drawn last)
3. Bypassed routes are excluded from rendering (FR-024)
4. If more than 4 non-bypassed arcs target the same destination:
   - The 4 most recent arcs are drawn individually in their source colors
   - All remaining (older) arcs are merged into a single composite gray arc
   - The composite arc's amount = sum of the merged routes' amounts (clamped)

### Clamping (FR-021)

Arc end angle is always clamped to the knob's min/max range:

```
clampedEnd = clamp(baseValue + normalizedAmount, 0.0, 1.0)
```

If the modulation would push beyond the knob's range, the arc stops at the boundary.

## Mouse Interaction

### Click-to-Select (FR-027)

When the user clicks on a visible arc:

1. Perform hit testing to determine which arc (if any) was clicked
2. Hit test uses angular distance from the arc path (tolerance = strokeWidth + 2px)
3. If an arc is hit, call `controller_->selectModulationRoute(sourceIndex, destIndex)`
4. Controller mediates to ModMatrixGrid, which highlights and scrolls to the route

### Hover Tooltips (FR-028)

On mouse move:

1. Perform hit testing to determine which arc the cursor is over
2. If over an arc: `setTooltipText("ENV 2 -> Filter Cutoff: +0.72")`
3. If not over any arc: `setTooltipText("")`

Format: `"{SourceFullName} -> {DestFullName}: {signedAmount}"`

## IDependent Integration

ModRingIndicator does NOT directly implement IDependent. Instead, the Controller:

1. Registers as dependent on all modulation parameters (source, dest, amount, bypass for each slot)
2. When a modulation parameter changes, Controller rebuilds the ArcInfo list for each affected destination
3. Controller calls `indicator->setArcs(arcs)` which triggers a redraw

This keeps ModRingIndicator decoupled from parameter IDs and the IDependent mechanism.

### Refresh Triggers

ModRingIndicator redraws when:
- Any modulation route's source, destination, or amount changes
- A route is added or removed
- A route's bypass state changes
- The destination knob's base value changes

ModRingIndicator does NOT use CVSTGUITimer (FR-030). All redraws are event-driven.

## ViewCreator Registration

```cpp
struct ModRingIndicatorCreator : VSTGUI::ViewCreatorAdapter {
    ModRingIndicatorCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    IdStringPtr getViewName() const override { return "ModRingIndicator"; }
    IdStringPtr getBaseViewName() const override { return UIViewCreator::kCView; }

    CView* create(const UIAttributes& attrs, const IUIDescription* desc) const override {
        return new ModRingIndicator(CRect(0, 0, 50, 50));
    }

    bool apply(CView* view, const UIAttributes& attrs,
               const IUIDescription* desc) const override {
        auto* indicator = dynamic_cast<ModRingIndicator*>(view);
        if (!indicator) return false;

        // stroke-width attribute
        double strokeWidth;
        if (attrs.getDoubleAttribute("stroke-width", strokeWidth))
            indicator->setStrokeWidth(static_cast<float>(strokeWidth));

        return true;
    }

    bool getAttributeNames(StringList& names) const override {
        names.emplace_back("stroke-width");
        return true;
    }

    AttrType getAttributeType(const std::string& name) const override {
        if (name == "stroke-width") return kFloatType;
        return kUnknownType;
    }

    bool getAttributeValue(CView* view, const std::string& name,
                           std::string& value, const IUIDescription* desc) const override {
        auto* indicator = dynamic_cast<ModRingIndicator*>(view);
        if (!indicator) return false;

        if (name == "stroke-width") {
            value = std::to_string(indicator->getStrokeWidth());
            return true;
        }
        return false;
    }
};

static ModRingIndicatorCreator __gModRingIndicatorCreator;
```

## .uidesc Placement

ModRingIndicator is placed as a sibling view after each destination knob in the `.uidesc` file. It must have identical bounds to the knob it overlays:

```xml
<!-- Destination knob -->
<view class="ArcKnob" origin="10,10" size="50,50"
      control-tag="FilterCutoff" ... />

<!-- Modulation overlay (same bounds, draws on top) -->
<view class="ModRingIndicator" origin="10,10" size="50,50"
      transparent="true" mouse-enabled="true"
      stroke-width="3.0" />
```

The `transparent="true"` attribute ensures the overlay does not paint a background, leaving the knob visible underneath. The `mouse-enabled="true"` attribute allows click and hover detection on the arcs.

## Thread Safety

- `setArcs()` and `setBaseValue()` must be called on the UI thread only
- `draw()` is called on the UI thread by VSTGUI
- Controller mediation (selectModulationRoute) happens on the UI thread
- No shared mutable state with the audio thread
