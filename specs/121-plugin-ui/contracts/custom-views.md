# Custom View Contracts

## Registration

All custom views are created via `Controller::createCustomView()` based on the `custom-view-name` attribute in `editor.uidesc`:

```xml
<view class="CView" custom-view-name="HarmonicDisplay" origin="10, 55" size="500, 140" />
```

The controller matches the name string and returns the appropriate `CView` subclass.

## View Registry

| `custom-view-name` | Class | Header |
|---|---|---|
| `"HarmonicDisplay"` | `HarmonicDisplayView` | `controller/views/harmonic_display_view.h` |
| `"ConfidenceIndicator"` | `ConfidenceIndicatorView` | `controller/views/confidence_indicator_view.h` |
| `"MemorySlotStatus"` | `MemorySlotStatusView` | `controller/views/memory_slot_status_view.h` |
| `"EvolutionPosition"` | `EvolutionPositionView` | `controller/views/evolution_position_view.h` |
| `"ModulatorActivity"` | `ModulatorActivityView` | `controller/views/modulator_activity_view.h` |

## Common Interface

All custom views follow this pattern:

```cpp
class CustomView : public VSTGUI::CView {
public:
    explicit CustomView(const VSTGUI::CRect& size);

    // Called by controller's timer callback with latest display data
    void updateData(const DisplayData& data);

    // VSTGUI draw callback
    void draw(VSTGUI::CDrawContext* context) override;

    CLASS_METHODS_NOCOPY(CustomView, CView)
};
```

## HarmonicDisplayView

**Size**: ~500 x 140 pixels
**Content**: 48 vertical bars representing partial amplitudes

**Drawing Rules**:
1. Background: Dark fill (#0d0d1a or similar)
2. Active partials: Cyan/teal bars (#00bcd4) at height proportional to dB value
3. Attenuated partials (filtered): Dark gray bars (#333333) at same height
4. Empty state: Centered text "No analysis data" in medium gray
5. dB range: -60 dB (bar hidden) to 0 dB (full height)
6. Bar width: `(viewWidth - 2*padding) / 48` with 1px gap between bars

## ConfidenceIndicatorView

**Size**: ~150 x 140 pixels (matches the display zone height so both custom views span the same vertical region; bar and text occupy the upper portion of the available space)
**Content**: Horizontal confidence bar + detected note text

**Drawing Rules**:
1. Bar height: ~8px, width proportional to confidence value
2. Color zones: Red (<0.3) -> Yellow (0.3-0.7) -> Green (>0.7)
3. Below bar: Note name + frequency text (e.g., "A4 - 440 Hz")
4. No confidence (0.0): Bar empty, text shows "--"

## MemorySlotStatusView

**Size**: ~120 x 20 pixels
**Content**: 8 small circles in a row

**Drawing Rules**:
1. Circle diameter: ~12px, spacing: ~3px
2. Occupied: Filled circle in accent color (#00bcd4)
3. Empty: Hollow circle (stroke only) in dim color (#555555)

## EvolutionPositionView

**Size**: ~180 x 20 pixels
**Content**: Horizontal track with playhead

**Drawing Rules**:
1. Track: Rounded rectangle, 4px height, dark gray (#333333)
2. Playhead: Vertical line, 2px wide, full view height, cyan (#00bcd4)
3. Ghost marker: Same as playhead but 30% opacity (when evolution active)
4. Position: `x = padding + position * (viewWidth - 2*padding)`

## ModulatorActivityView

**Size**: ~30 x 20 pixels per instance
**Content**: Pulsing dot or mini waveform

**Drawing Rules**:
1. Active: Filled circle that pulses (alpha oscillates with LFO phase)
2. Inactive: Dim hollow circle
3. Color: Accent color (#00bcd4) when active, gray (#555555) when inactive
