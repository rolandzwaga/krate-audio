# Contract: ToggleButton Gear Icon Extension

**Feature**: 052-expand-master-section
**File**: `plugins/shared/src/ui/toggle_button.h`

---

## API Contract

### Enum Extension

```cpp
enum class IconStyle { kPower, kChevron, kGear };
```

### String Conversion

```cpp
// iconStyleFromString: "gear" -> IconStyle::kGear
// iconStyleToString: IconStyle::kGear -> "gear"
```

### UIDESC Attribute

```xml
<view class="ToggleButton"
      icon-style="gear"
      on-color="master"
      off-color="text-secondary"
      icon-size="0.65"
      stroke-width="1.5"
      transparent="true"/>
```

### Draw Method Signature

```cpp
private:
    void drawGearIcon(VSTGUI::CDrawContext* context,
                      const VSTGUI::CColor& color) const;

    void drawGearIconInRect(VSTGUI::CDrawContext* context,
                            const VSTGUI::CRect& rect,
                            const VSTGUI::CColor& color) const;
```

### Gear Icon Geometry

The gear is drawn as a polygon with alternating inner/outer radius vertices forming teeth:

- **numTeeth**: 6 (good visual clarity at 18x18px)
- **outerRadius**: `dim / 2.0` (based on iconSize_ * min(viewW, viewH))
- **innerRadius**: `outerRadius * 0.65` (tooth depth ratio)
- **toothHalfAngle**: `PI / numTeeth * 0.45` (tooth width as fraction of sector)
- **centerHoleRadius**: `outerRadius * 0.3` (small center circle for visual detail)

The path is:
1. For each tooth i in [0, numTeeth):
   - Compute base angle = i * (2*PI / numTeeth)
   - Add 4 vertices: inner-leading, outer-leading, outer-trailing, inner-trailing
2. Close the path (fills the gear body)
3. Optionally draw a center circle (stroke or fill with background color)

### ViewCreator Registration

The `getPossibleListValues` for `"icon-style"` must include `"gear"`:

```cpp
static const std::string kGear = "gear";
values.emplace_back(&kGear);
```

---

## Behavioral Contract

- When `icon-style="gear"` and no `control-tag`: the button renders the gear icon but clicking has no parameter effect (tag is -1, VST3Editor ignores it).
- The gear icon color follows the same on/off state logic as power and chevron icons: `onColor_` when value >= 0.5, `offColor_` when value < 0.5.
- When combined with `title-position`, the gear icon renders in its sub-rect alongside the title text, following the same layout logic as power and chevron icons.
