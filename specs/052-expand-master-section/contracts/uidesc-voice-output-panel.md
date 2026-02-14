# Contract: Voice & Output Panel UIDESC Layout

**Feature**: 052-expand-master-section
**File**: `plugins/ruinae/resources/editor.uidesc`

---

## Panel Container

The panel is a `FieldsetContainer` at `origin="772, 32"` `size="120, 160"` with `fieldset-title="Voice &amp; Output"`. See [spec.md Proposed Layout](spec.md#proposed-layout-reference) for the canonical ASCII layout diagram.

**Note**: The `&` in "Voice & Output" must be XML-escaped as `&amp;` in uidesc XML.

---

## Controls (top to bottom)

### Polyphony Dropdown (existing, repositioned)

```xml
<view class="COptionMenu" origin="8, 14" size="60, 18"
      control-tag="Polyphony"
      font="~ NormalFontSmaller"
      font-color="master"
      back-color="bg-dropdown"
      frame-color="frame-dropdown-dim"
      tooltip="Polyphony"
      transparent="false"/>
<view class="CTextLabel" origin="8, 32" size="60, 10"
      title="Poly"
      font="~ NormalFontSmaller"
      font-color="text-secondary"
      text-alignment="center"
      transparent="true"/>
```

### Gear Icon (new, inert placeholder)

```xml
<view class="ToggleButton" origin="72, 14" size="18, 18"
      icon-style="gear"
      on-color="master"
      off-color="text-secondary"
      icon-size="0.65"
      stroke-width="1.5"
      tooltip="Settings"
      transparent="true"/>
```

### Output Knob (existing, repositioned)

```xml
<view class="ArcKnob" origin="42, 48" size="36, 36"
      control-tag="MasterGain"
      default-value="0.5"
      arc-color="master"
      guide-color="knob-guide"/>
<view class="CTextLabel" origin="34, 84" size="52, 12"
      title="Output"
      font="~ NormalFontSmaller"
      font-color="text-secondary"
      text-alignment="center"
      transparent="true"/>
```

### Width Knob (new, placeholder)

```xml
<view class="ArcKnob" origin="14, 100" size="28, 28"
      arc-color="master"
      guide-color="knob-guide"/>
<view class="CTextLabel" origin="10, 128" size="36, 10"
      title="Width"
      font="~ NormalFontSmaller"
      font-color="text-secondary"
      text-alignment="center"
      transparent="true"/>
```

### Spread Knob (new, placeholder)

```xml
<view class="ArcKnob" origin="62, 100" size="28, 28"
      arc-color="master"
      guide-color="knob-guide"/>
<view class="CTextLabel" origin="58, 128" size="40, 10"
      title="Spread"
      font="~ NormalFontSmaller"
      font-color="text-secondary"
      text-alignment="center"
      transparent="true"/>
```

### Soft Limit Toggle (existing, repositioned)

```xml
<view class="ToggleButton" origin="20, 142" size="80, 16"
      control-tag="SoftLimit"
      title="Soft Limit"
      font="~ NormalFontSmaller"
      font-color="master"
      text-color="master"
      tooltip="Limit output level"
      transparent="true"/>
```

**Note**: All `origin` values use `"x, y"` format (e.g., `origin="20, 142"`). All coordinates are relative to the panel's content area.

---

## Spacing Verification

| Gap | From | To | Distance | Min 4px? |
|-----|------|----|----------|----------|
| Poly dropdown to Gear | x=68 (8+60) | x=72 | 4px | Yes |
| Poly label to Output knob | y=42 (32+10) | y=48 | 6px | Yes |
| Output label to Width/Spread | y=96 (84+12) | y=100 | 4px | Yes |
| Width to Spread (horizontal) | x=42 (14+28) | x=62 | 20px | Yes |
| Width/Spread labels to Soft Limit | y=138 (128+10) | y=142 | 4px | Yes |
| Soft Limit bottom to panel bottom | y=158 (142+16) | y=160 | 2px | N/A (edge) |

All inter-control gaps meet the 4px minimum constraint.

---

## Boundary Verification

| Control | Left Edge | Right Edge | Top Edge | Bottom Edge | Within 120x160? |
|---------|-----------|------------|----------|-------------|-----------------|
| Poly dropdown | 8 | 68 | 14 | 32 | Yes |
| Poly label | 8 | 68 | 32 | 42 | Yes |
| Gear icon | 72 | 90 | 14 | 32 | Yes |
| Output knob | 42 | 78 | 48 | 84 | Yes |
| Output label | 34 | 86 | 84 | 96 | Yes |
| Width knob | 14 | 42 | 100 | 128 | Yes |
| Width label | 10 | 46 | 128 | 138 | Yes |
| Spread knob | 62 | 90 | 100 | 128 | Yes |
| Spread label | 58 | 98 | 128 | 138 | Yes |
| Soft Limit | 20 | 100 | 142 | 158 | Yes |

All controls fit within the 120x160px boundary.
