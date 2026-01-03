# VSTGUI Controls Reference

**Created**: 2026-01-03
**Purpose**: Quick reference for UI control selection in Iterum plugin development

---

## Control Selection Guidelines

### Decision Matrix: Which Control to Use

| # Options | Visibility Need | Recommended Control |
|-----------|-----------------|---------------------|
| 2 (on/off) | Always visible | `COnOffButton` or `CCheckBox` |
| 2 (A vs B) | Always visible | `CSegmentButton` (2 segments) |
| 2-5 | Always visible | `CSegmentButton` |
| 2-5 | Compact/bitmap | `CHorizontalSwitch` / `CVerticalSwitch` |
| 5+ | Space-limited | `COptionMenu` (dropdown) |
| Continuous | Always visible | `CKnob` or `CSlider` |
| 2D control | Always visible | `CXYPad` |

### UX Best Practices

1. **Binary on/off**: Use `COnOffButton` - immediate visual feedback
2. **Mutually exclusive options (2-5)**: Use `CSegmentButton` - all options visible
3. **Many options (5+)**: Use `COptionMenu` - saves space
4. **Stepped values with knob feel**: Use `CAnimKnob` with matching frame count
5. **Never use dropdown for 2-3 options** - wastes clicks, hides choices

---

## Available Control Classes

### Toggle/Binary Controls

#### COnOffButton
- **Purpose**: Simple two-state toggle (on/off, enabled/disabled)
- **States**: 2 (uses bitmap with 2 sub-images)
- **Best for**: Bypass, enable/disable, true/false parameters
- **XML**: `<view class="COnOffButton" />`

#### CCheckBox
- **Purpose**: Checkbox with optional title, supports 3 states
- **States**: 2-3 (off, on, mixed)
- **Best for**: Feature toggles with labels
- **XML**: `<view class="CCheckBox" />`

### Segmented/Multi-Option Controls

#### CSegmentButton
- **Purpose**: Multiple clickable segments (like iOS segmented control)
- **States**: Configurable via `segment-names`
- **Selection Modes**:
  - `kSingle` - Only one segment selected at a time
  - `kSingleToggle` - Single selection; clicking selected advances to next
  - `kMultiple` - Multiple segments can be selected
- **Styles**: `kHorizontal`, `kVertical`, `kHorizontalInverse`, `kVerticalInverse`
- **Best for**: 2-5 mutually exclusive options that should all be visible
- **XML Attributes**:
  ```xml
  <view class="CSegmentButton"
        segment-names="Option1,Option2,Option3"
        style="horizontal"
        font="~ NormalFont"
        text-color="~ BlackCColor"
        frame-color="~ GreyCColor" />
  ```

#### CHorizontalSwitch / CVerticalSwitch
- **Purpose**: Bitmap-based multi-position switch
- **Requires**: Bitmap with N sub-images (one per position)
- **Attribute**: `height-of-one-image` defines sub-image height
- **Best for**: Compact multi-option selectors with custom graphics
- **XML**:
  ```xml
  <view class="CHorizontalSwitch"
        bitmap="switch-3pos"
        height-of-one-image="30" />
  ```

#### CRockerSwitch
- **Purpose**: 3-position momentary rocker (snaps back to center)
- **States**: 3 (left, center, right)
- **Uses**: 3 sub-bitmaps
- **Best for**: Increment/decrement, navigation, pitch bend
- **Note**: Momentary - returns to center on mouse release

### Dropdown/Menu Controls

#### COptionMenu
- **Purpose**: Popup dropdown menu
- **Best for**: 5+ options, or when space is very limited
- **Attributes**:
  - `menu-popup-style` - Use popup appearance
  - `menu-check-style` - Show checkmarks for selected item
- **XML**:
  ```xml
  <view class="COptionMenu"
        menu-popup-style="true"
        menu-check-style="true" />
  ```
- **Note**: Items added programmatically via controller

### Knob Controls

#### CKnob
- **Purpose**: Rotary knob control (vector-drawn)
- **Range**: -45° to +225° (270° total rotation)
- **Features**:
  - Alt+click resets to default
  - Supports circular and linear drag modes
- **Best for**: Continuous parameters with custom handle graphics

#### CAnimKnob
- **Purpose**: Bitmap-based rotary knob with animation frames
- **Requires**: `CMultiFrameBitmap` with N frames
- **How it works**: Displays frame based on normalized value (0.0-1.0)
- **Best for**:
  - Continuous params with detailed knob graphics
  - Stepped/discrete params (use frame count = step count + 1)
- **Stepped knob implementation**:
  1. Create bitmap with N frames (one per discrete value)
  2. Use `StringListParameter` in VST3 with stepCount = N-1
  3. CAnimKnob automatically snaps to frames

### Slider Controls

#### CSlider / CHorizontalSlider / CVerticalSlider
- **Purpose**: Linear slider control
- **Best for**: Volume faders, pan, parameters where linear motion is intuitive
- **Variants**: Horizontal or vertical orientation

### Button Controls

#### CKickButton
- **Purpose**: Momentary button (pressed while held)
- **Best for**: Trigger actions, tap tempo
- **Behavior**: Value 1 while pressed, 0 when released

#### CTextButton
- **Purpose**: Text-labeled button (no bitmap required)
- **Best for**: Actions with text labels, when no custom graphics needed

#### CMovieButton
- **Purpose**: Two-state button with 2 sub-bitmaps
- **Similar to**: COnOffButton but with movie-style bitmap

### Display Controls

#### CTextLabel
- **Purpose**: Static text display
- **Best for**: Labels, titles, read-only values

#### CParamDisplay
- **Purpose**: Parameter value display
- **Best for**: Showing current parameter value as text

#### CVuMeter
- **Purpose**: Level meter display
- **Best for**: Audio level visualization

### Special Controls

#### CXYPad
- **Purpose**: 2D control pad for X/Y parameter pairs
- **Controls**: Two parameters simultaneously (one per axis)
- **Features**:
  - Draggable handle bitmap
  - Independent X/Y default values
  - Mouse wheel support
- **Best for**: Filter cutoff/resonance, delay time/feedback, position controls

#### CDataBrowser
- **Purpose**: Scrollable data browser/list
- **Best for**: Preset browsers, file lists

---

## Discrete/Stepped Parameters in VST3

### Parameter Step Count Values
- `0`: Continuous parameter (any normalized value valid)
- `1`: Discrete with 2 states (on/off, yes/no) - 1 step
- `N`: Discrete with N+1 states - N steps between them

### StringListParameter
Use for enum-style parameters with named options:
```cpp
auto* param = new StringListParameter(
    USTRING("Filter Type"),  // name
    kFilterTypeId,           // tag
    nullptr                  // units
);
param->appendString(USTRING("Lowpass"));
param->appendString(USTRING("Highpass"));
param->appendString(USTRING("Bandpass"));
```

### Matching UI Control to Stepped Parameters
1. **2 options**: COnOffButton, CSegmentButton (2), or CHorizontalSwitch (2 frames)
2. **3-5 options**: CSegmentButton or CHorizontalSwitch
3. **Stepped knob**: CAnimKnob with frameCount = stepCount + 1
4. **6+ options**: COptionMenu

---

## Bitmap Requirements

### Multi-Frame Bitmaps
For switch and animated knob controls:
- **CAnimKnob**: Vertical strip, N frames for N positions
- **CHorizontalSwitch**: Vertical strip, N frames, set `height-of-one-image`
- **CVerticalSwitch**: Vertical strip, N frames, set `height-of-one-image`
- **CRockerSwitch**: 3 frames (left, center, right positions)
- **COnOffButton**: 2 frames (off, on)
- **CMovieButton**: 2 frames

### Frame Order
- Frame 0 = value 0.0 (minimum/off)
- Frame N-1 = value 1.0 (maximum/on)

---

## Common Patterns in Audio Plugins

### Filter Type Selector (3-4 options)
```xml
<view class="CSegmentButton"
      control-tag="FilterType"
      segment-names="LP,HP,BP,Notch"
      style="horizontal" />
```

### On/Off Toggle with Custom Graphics
```xml
<view class="COnOffButton"
      control-tag="Bypass"
      bitmap="bypass-switch" />
```

### Era/Model Selector (5+ options)
```xml
<view class="COptionMenu"
      control-tag="Era"
      menu-popup-style="true" />
```

### Stepped Rotary Selector (e.g., BBD Chip Model)
Use CAnimKnob with 4 frames for 4 chip models:
```xml
<view class="CAnimKnob"
      control-tag="BBDChipModel"
      bitmap="chip-selector-4pos" />
```

---

## Sources

- [VSTGUI Controls Reference](https://steinbergmedia.github.io/vst3_doc/vstgui/html/group__controls.html)
- [VSTGUI UI XML Attributes](https://steinbergmedia.github.io/vst3_doc/vstgui/html/uidescription_attributes.html)
- [CSegmentButton Class Reference](https://steinbergmedia.github.io/vst3_doc/vstgui/html/class_v_s_t_g_u_i_1_1_c_segment_button.html)
- [VST3 Parameters and Automation](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Parameters+Automation/Index.html)
- [StringListParameter Class](https://steinbergmedia.github.io/vst3_doc/vstsdk/classSteinberg_1_1Vst_1_1StringListParameter.html)
- [CAnimKnob Class Reference](https://steinbergmedia.github.io/vst3_doc/vstgui/html/class_v_s_t_g_u_i_1_1_c_anim_knob.html)
- [CXYPad Class Reference](https://steinbergmedia.github.io/vst3_doc/vstgui/html/class_v_s_t_g_u_i_1_1_c_x_y_pad.html)
