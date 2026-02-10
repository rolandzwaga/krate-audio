# API Contract: ADSRDisplay Custom Control

**Feature**: 048-adsr-display | **Date**: 2026-02-10

## ADSRDisplay Public API

**File**: `plugins/shared/src/ui/adsr_display.h`
**Namespace**: `Krate::Plugins`
**Base class**: `VSTGUI::CControl`

### Construction

```cpp
// Primary constructor (used by ViewCreator)
ADSRDisplay(const VSTGUI::CRect& size,
            VSTGUI::IControlListener* listener,
            int32_t tag);

// Copy constructor (required by VSTGUI ViewCreator system)
ADSRDisplay(const ADSRDisplay& other);
```

### Parameter Communication (Callback-based)

```cpp
using ParameterCallback = std::function<void(uint32_t paramId, float normalizedValue)>;
using EditCallback = std::function<void(uint32_t paramId)>;

void setParameterCallback(ParameterCallback cb);
void setBeginEditCallback(EditCallback cb);
void setEndEditCallback(EditCallback cb);
```

### Parameter ID Configuration

```cpp
// Set the base ADSR parameter IDs (attack, decay, sustain, release are consecutive)
void setAdsrBaseParamId(uint32_t baseId);   // e.g., 700 for Amp

// Set curve parameter IDs (attack curve, decay curve, release curve are consecutive)
void setCurveBaseParamId(uint32_t baseId);  // e.g., 704 for Amp

// Set Bezier mode flag parameter ID
void setBezierEnabledParamId(uint32_t paramId);  // e.g., 707

// Set Bezier control point base ID (12 consecutive: 3 segments x 4 values)
void setBezierBaseParamId(uint32_t baseId);  // e.g., 710 for Amp
```

### Parameter Value Setters (called by controller for sync)

```cpp
void setAttackMs(float ms);        // [0.1, 10000]
void setDecayMs(float ms);         // [0.1, 10000]
void setSustainLevel(float level);  // [0.0, 1.0]
void setReleaseMs(float ms);       // [0.1, 10000]
void setAttackCurve(float curve);  // [-1.0, +1.0]
void setDecayCurve(float curve);   // [-1.0, +1.0]
void setReleaseCurve(float curve); // [-1.0, +1.0]
void setBezierEnabled(bool enabled);
void setBezierHandleValue(int segment, int handle, int axis, float value);
  // segment: 0=Attack, 1=Decay, 2=Release
  // handle: 0=cp1, 1=cp2
  // axis: 0=x, 1=y
  // value: [0.0, 1.0]
```

### Playback Visualization

```cpp
void setPlaybackState(float outputLevel, int stage, bool voiceActive);
```

### ViewCreator Color Attributes

```cpp
void setFillColor(const VSTGUI::CColor& color);
VSTGUI::CColor getFillColor() const;

void setStrokeColor(const VSTGUI::CColor& color);
VSTGUI::CColor getStrokeColor() const;

void setBackgroundColor(const VSTGUI::CColor& color);
VSTGUI::CColor getBackgroundColor() const;

void setGridColor(const VSTGUI::CColor& color);
VSTGUI::CColor getGridColor() const;

void setControlPointColor(const VSTGUI::CColor& color);
VSTGUI::CColor getControlPointColor() const;

void setTextColor(const VSTGUI::CColor& color);
VSTGUI::CColor getTextColor() const;
```

### CControl Overrides

```cpp
void draw(VSTGUI::CDrawContext* context) override;
VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
VSTGUI::CMouseEventResult onMouseMoved(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
VSTGUI::CMouseEventResult onMouseCancel() override;
int32_t onKeyDown(VstKeyCode& keyCode) override;
CLASS_METHODS(ADSRDisplay, CControl)
```

---

## ADSRDisplayCreator (ViewCreator Registration)

**Pattern**: Same as ArcKnobCreator, StepPatternEditorCreator

```cpp
struct ADSRDisplayCreator : VSTGUI::ViewCreatorAdapter {
    // Registered name: "ADSRDisplay"
    // Base view: kCControl
    // Display name: "ADSR Display"
};

// Global registration (inline to prevent multiple-definition errors)
inline ADSRDisplayCreator gADSRDisplayCreator;
```

### ViewCreator Attributes

| Attribute Name | Type | Maps To | Default |
|---------------|------|---------|---------|
| `fill-color` | Color | `setFillColor()` | rgba(80,140,200,77) |
| `stroke-color` | Color | `setStrokeColor()` | rgb(80,140,200) |
| `background-color` | Color | `setBackgroundColor()` | rgb(30,30,33) |
| `grid-color` | Color | `setGridColor()` | rgba(255,255,255,25) |
| `control-point-color` | Color | `setControlPointColor()` | rgb(255,255,255) |
| `text-color` | Color | `setTextColor()` | rgba(255,255,255,180) |

---

## Curve Table Utility API (Layer 0)

**File**: `dsp/include/krate/dsp/core/curve_table.h`
**Namespace**: `Krate::DSP`

```cpp
inline constexpr size_t kCurveTableSize = 256;
inline constexpr float kCurveRangeK = 3.0f;

/// Generate a 256-entry power curve lookup table.
/// Formula: output = lerp(startLevel, endLevel, phase^(2^(curveAmount * k)))
/// @param table Output array
/// @param curveAmount Curve shape [-1, +1], 0=linear
/// @param startLevel Level at phase=0
/// @param endLevel Level at phase=1
void generatePowerCurveTable(
    std::array<float, kCurveTableSize>& table,
    float curveAmount,
    float startLevel = 0.0f,
    float endLevel = 1.0f) noexcept;

/// Generate a 256-entry cubic Bezier curve lookup table.
/// Control points are normalized within the segment bounding box.
/// @param table Output array
/// @param cp1x, cp1y Control point 1 (normalized [0,1])
/// @param cp2x, cp2y Control point 2 (normalized [0,1])
/// @param startLevel Level at phase=0
/// @param endLevel Level at phase=1
void generateBezierCurveTable(
    std::array<float, kCurveTableSize>& table,
    float cp1x, float cp1y,
    float cp2x, float cp2y,
    float startLevel = 0.0f,
    float endLevel = 1.0f) noexcept;

/// Linearly interpolate a value from the curve table.
/// @param table The 256-entry lookup table
/// @param phase Normalized phase [0, 1]
/// @return Interpolated value
[[nodiscard]] inline float lookupCurveTable(
    const std::array<float, kCurveTableSize>& table,
    float phase) noexcept;

/// Convert EnvCurve enum to continuous curve amount.
/// Exponential -> +0.7, Linear -> 0.0, Logarithmic -> -0.7
[[nodiscard]] inline float envCurveToCurveAmount(EnvCurve curve) noexcept;

/// Derive the simple curve amount from a Bezier curve by sampling at phase 0.5.
/// @param cp1x, cp1y, cp2x, cp2y Bezier control points (normalized)
/// @param startLevel, endLevel Segment boundary levels
/// @return Best-fit curve amount [-1, +1]
[[nodiscard]] float bezierToSimpleCurve(
    float cp1x, float cp1y,
    float cp2x, float cp2y,
    float startLevel = 0.0f,
    float endLevel = 1.0f) noexcept;

/// Generate Bezier control points that approximate a power curve.
/// @param curveAmount Simple curve amount [-1, +1]
/// @param[out] cp1x, cp1y, cp2x, cp2y Output control points
void simpleCurveToBezier(
    float curveAmount,
    float& cp1x, float& cp1y,
    float& cp2x, float& cp2y) noexcept;
```

---

## Parameter Struct Extensions

### AmpEnvParams (Modified)

**File**: `plugins/ruinae/src/parameters/amp_env_params.h`

```cpp
struct AmpEnvParams {
    // Existing
    std::atomic<float> attackMs{10.0f};
    std::atomic<float> decayMs{100.0f};
    std::atomic<float> sustain{0.8f};
    std::atomic<float> releaseMs{200.0f};

    // New: Curve amounts
    std::atomic<float> attackCurve{0.0f};   // [-1, +1]
    std::atomic<float> decayCurve{0.0f};    // [-1, +1]
    std::atomic<float> releaseCurve{0.0f};  // [-1, +1]

    // New: Bezier mode
    std::atomic<float> bezierEnabled{0.0f}; // 0 or 1

    // New: Bezier control points (3 segments x 2 handles x 2 axes = 12 values)
    std::atomic<float> bezierAttackCp1X{0.33f};
    std::atomic<float> bezierAttackCp1Y{0.33f};
    std::atomic<float> bezierAttackCp2X{0.67f};
    std::atomic<float> bezierAttackCp2Y{0.67f};
    std::atomic<float> bezierDecayCp1X{0.33f};
    std::atomic<float> bezierDecayCp1Y{0.67f};
    std::atomic<float> bezierDecayCp2X{0.67f};
    std::atomic<float> bezierDecayCp2Y{0.33f};
    std::atomic<float> bezierReleaseCp1X{0.33f};
    std::atomic<float> bezierReleaseCp1Y{0.67f};
    std::atomic<float> bezierReleaseCp2X{0.67f};
    std::atomic<float> bezierReleaseCp2Y{0.33f};
};
```

Same pattern for `FilterEnvParams` and `ModEnvParams`.

---

## editor.uidesc Integration

```xml
<!-- Per-envelope ADSRDisplay instance -->
<view
    class="ADSRDisplay"
    origin="10, 10"
    size="140, 90"
    control-tag="kAmpEnvAttackId"
    fill-color="80, 140, 200, 77"
    stroke-color="80, 140, 200, 255"
    background-color="30, 30, 33, 255"
    grid-color="255, 255, 255, 25"
    control-point-color="255, 255, 255, 255"
    text-color="255, 255, 255, 180"
/>
```

Note: The `control-tag` identifies this instance. The controller detects the tag in `verifyView()` and wires the appropriate parameter IDs and callbacks.
