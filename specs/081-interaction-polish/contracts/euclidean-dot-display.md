# EuclideanDotDisplay API Contract

## Location

`plugins/shared/src/ui/euclidean_dot_display.h`

## Registration

Registered as `"EuclideanDotDisplay"` via VSTGUI ViewCreator system.

## Class Definition

```cpp
namespace Krate::Plugins {

class EuclideanDotDisplay : public VSTGUI::CView {
public:
    explicit EuclideanDotDisplay(const VSTGUI::CRect& size);
    EuclideanDotDisplay(const EuclideanDotDisplay& other);

    // =========================================================================
    // Properties
    // =========================================================================

    void setHits(int hits);        // 0 to steps
    int getHits() const;

    void setSteps(int steps);      // 2 to 32
    int getSteps() const;

    void setRotation(int rotation); // 0 to steps-1
    int getRotation() const;

    void setDotRadius(float radius);
    float getDotRadius() const;

    void setAccentColor(const VSTGUI::CColor& color);
    VSTGUI::CColor getAccentColor() const;

    // =========================================================================
    // CView Overrides
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override;

    CLASS_METHODS(EuclideanDotDisplay, CView)

private:
    int hits_ = 0;
    int steps_ = 8;
    int rotation_ = 0;
    float dotRadius_ = 3.0f;
    VSTGUI::CColor accentColor_{208, 132, 92, 255};
    VSTGUI::CColor outlineColor_{80, 80, 85, 255};
};

} // namespace Krate::Plugins
```

## Draw Algorithm

```
1. Calculate center = (viewWidth/2, viewHeight/2)
2. Calculate ring radius = min(viewWidth, viewHeight)/2 - dotRadius - 2
3. For each step i in 0..steps-1:
   a. angle = -PI/2 + 2*PI*i/steps  (start from top, clockwise)
   b. x = center.x + ringRadius * cos(angle)
   c. y = center.y + ringRadius * sin(angle)
   d. Generate pattern: uint32_t pattern = EuclideanPattern::generate(hits, steps, rotation)
   e. If EuclideanPattern::isHit(pattern, i, steps):
      - Draw filled circle at (x, y) with dotRadius, accentColor
   f. Else:
      - Draw stroked circle at (x, y) with dotRadius, outlineColor
```

## ViewCreator Attributes

| Attribute | Type | Default | Description |
|-----------|------|---------|-------------|
| `hits` | int | 0 | Number of Euclidean hits |
| `steps` | int | 8 | Number of Euclidean steps |
| `rotation` | int | 0 | Pattern rotation offset |
| `accent-color` | color | #D0845C | Fill color for hit dots |
| `dot-radius` | float | 3.0 | Radius of each dot |
