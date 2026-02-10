# API Contract: XYMorphPad

**Date**: 2026-02-10

## Class: `Krate::Plugins::XYMorphPad`

**Inherits**: `VSTGUI::CControl`
**Header**: `plugins/shared/src/ui/xy_morph_pad.h`

### Construction

```cpp
XYMorphPad(const VSTGUI::CRect& size,
           VSTGUI::IControlListener* listener,
           int32_t tag);

// Copy constructor (for ViewCreator)
XYMorphPad(const XYMorphPad& other);
```

### Configuration API

```cpp
// Controller and secondary parameter (must be set before use)
void setController(Steinberg::Vst::EditControllerEx1* controller);
void setSecondaryParamId(Steinberg::Vst::ParamID id);

// Morph position (X = CControl value, Y = separate)
void setMorphPosition(float x, float y);
float getMorphX() const;
float getMorphY() const;

// Modulation range visualization (FR-026, FR-027, FR-028)
void setModulationRange(float xRange, float yRange);
float getModulationRangeX() const;
float getModulationRangeY() const;

// Gradient corner colors (FR-005, FR-031)
void setColorBottomLeft(VSTGUI::CColor color);
VSTGUI::CColor getColorBottomLeft() const;

void setColorBottomRight(VSTGUI::CColor color);
VSTGUI::CColor getColorBottomRight() const;

void setColorTopLeft(VSTGUI::CColor color);
VSTGUI::CColor getColorTopLeft() const;

void setColorTopRight(VSTGUI::CColor color);
VSTGUI::CColor getColorTopRight() const;

// Cursor and label colors (FR-031)
void setCursorColor(VSTGUI::CColor color);
VSTGUI::CColor getCursorColor() const;

void setLabelColor(VSTGUI::CColor color);
VSTGUI::CColor getLabelColor() const;

// Crosshair opacity (FR-015)
void setCrosshairOpacity(float opacity);
float getCrosshairOpacity() const;

// Grid resolution (FR-004)
void setGridSize(int size);
int getGridSize() const;
```

### Coordinate Conversion (FR-033, FR-034)

```cpp
// Convert normalized [0,1] to pixel coordinates (Y-inverted)
void positionToPixel(float normX, float normY,
                     float& outPixelX, float& outPixelY) const;

// Convert pixel to normalized [0,1] (Y-inverted, clamped)
void pixelToPosition(float pixelX, float pixelY,
                     float& outNormX, float& outNormY) const;
```

### CControl Overrides

```cpp
void draw(VSTGUI::CDrawContext* context) override;

// VSTGUI 4.11+ Event API
void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;
void onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) override;
void onMouseUpEvent(VSTGUI::MouseUpEvent& event) override;
void onMouseWheelEvent(VSTGUI::MouseWheelEvent& event) override;
void onKeyboardEvent(VSTGUI::KeyboardEvent& event) override;

CLASS_METHODS(XYMorphPad, CControl)
```

### Internal Drawing Methods

```cpp
private:
    void drawGradientBackground(VSTGUI::CDrawContext* context);
    void drawCrosshairs(VSTGUI::CDrawContext* context);
    void drawModulationRegion(VSTGUI::CDrawContext* context);
    void drawCursor(VSTGUI::CDrawContext* context);
    void drawLabels(VSTGUI::CDrawContext* context);
```

## Struct: `Krate::Plugins::XYMorphPadCreator`

**Inherits**: `VSTGUI::ViewCreatorAdapter`
**Registered name**: `"XYMorphPad"`
**Base view name**: `kCControl`

### ViewCreator Attributes

| Attribute Name | Type | Maps To |
|---------------|------|---------|
| `color-bottom-left` | kColorType | `setColorBottomLeft()` |
| `color-bottom-right` | kColorType | `setColorBottomRight()` |
| `color-top-left` | kColorType | `setColorTopLeft()` |
| `color-top-right` | kColorType | `setColorTopRight()` |
| `cursor-color` | kColorType | `setCursorColor()` |
| `label-color` | kColorType | `setLabelColor()` |
| `crosshair-opacity` | kFloatType | `setCrosshairOpacity()` |
| `grid-size` | kIntegerType | `setGridSize()` |
| `secondary-tag` | kTagType | `setSecondaryParamId()` |

### Registration

```cpp
inline XYMorphPadCreator gXYMorphPadCreator;
```

## Utility: `bilinearColor`

**Location**: `plugins/shared/src/ui/color_utils.h`

```cpp
[[nodiscard]] inline VSTGUI::CColor bilinearColor(
    const VSTGUI::CColor& bottomLeft,
    const VSTGUI::CColor& bottomRight,
    const VSTGUI::CColor& topLeft,
    const VSTGUI::CColor& topRight,
    float tx, float ty);
```

## Parameter Extension: kMixerTiltId

**Location**: `plugins/ruinae/src/plugin_ids.h`

```cpp
kMixerTiltId = 302,    // Spectral tilt [-12, +12] dB/oct
```

**Registration** (in `mixer_params.h`):

```cpp
parameters.addParameter(
    new RangeParameter(STR16("Spectral Tilt"), kMixerTiltId, STR16("dB/oct"),
        -12.0, 12.0, 0.0, 0, ParameterInfo::kCanAutomate));
```

## editor.uidesc Integration

```xml
<view class="XYMorphPad"
      origin="X, Y" size="W, H"
      control-tag="MixPosition"
      secondary-tag="MixerTilt"
      color-bottom-left="#304878"
      color-bottom-right="#846624"
      color-top-left="#508CC8"
      color-top-right="#DCAA3C"
      cursor-color="#FFFFFF"
      label-color="#AAAAAA"
      crosshair-opacity="0.12"
      grid-size="24" />
```
