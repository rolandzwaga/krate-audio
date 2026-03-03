# UI Description Changes Contract

**Date**: 2026-02-15
**File**: `plugins/ruinae/resources/editor.uidesc`

## 1. Color Palette Addition

Add after `<color name="master" .../>` (line 33):
```xml
<color name="global-filter" rgba="#C8649Cff"/>
```

## 2. Control-Tag Additions

### Global Filter Tags (after Spread tag, before OSC A tags)

Insert after `<control-tag name="Spread" tag="5"/>` (line 68):
```xml
<!-- Global Filter -->
<control-tag name="GlobalFilterEnabled" tag="1400"/>
<control-tag name="GlobalFilterType" tag="1401"/>
<control-tag name="GlobalFilterCutoff" tag="1402"/>
<control-tag name="GlobalFilterResonance" tag="1403"/>
```

### Trance Gate Sync Tag (in Trance Gate section)

Insert after `<control-tag name="TranceGateRelease" tag="605"/>` (line 144):
```xml
<control-tag name="TranceGateSync" tag="606"/>
```

## 3. Editor Template Size Change

Change (lines 1720-1722):
```
minSize="900, 830"  -->  minSize="900, 866"
maxSize="900, 830"  -->  maxSize="900, 866"
size="900, 830"     -->  size="900, 866"
```

## 4. Row Container Y-Coordinate Shifts (+36px for Rows 3-5)

| Row | Current Origin | New Origin | Line |
|-----|---------------|------------|------|
| Row 3 (Trance Gate) | `origin="0, 334"` | `origin="0, 370"` | 1777 |
| Row 4 (Modulation) | `origin="0, 496"` | `origin="0, 532"` | 1785 |
| Row 5 (Effects) | `origin="0, 658"` | `origin="0, 694"` | 1793 |

## 5. FieldsetContainer Y-Coordinate Shifts (+36px for Rows 3-5)

| Section | Current Origin | New Origin | Line |
|---------|---------------|------------|------|
| Trance Gate | `origin="8, 334"` | `origin="8, 370"` | 2119 |
| Modulation | `origin="8, 496"` | `origin="8, 532"` | 2243 |
| Effects | `origin="8, 658"` | `origin="8, 694"` | 2293 |

## 6. New Global Filter FieldsetContainer

Insert between Row 2 (Timbre, ending at ~line 2112) and Row 3 (Trance Gate, starting at ~line 2114):

```xml
<!-- Row 2.5: Global Filter -->

<!-- GLOBAL FILTER (rose accent) -->
<view
    class="FieldsetContainer"
    origin="8, 334"
    size="884, 36"
    fieldset-title="GLOBAL FILTER"
    fieldset-color="global-filter"
    fieldset-radius="4"
    fieldset-line-width="1"
    fieldset-font-size="10"
    transparent="true"
>
    <!-- On/Off toggle -->
    <view class="ToggleButton" origin="8, 8" size="40, 18"
          control-tag="GlobalFilterEnabled" default-value="0"
          tooltip="Enable global filter"
          transparent="false"/>

    <!-- Type dropdown -->
    <view class="COptionMenu" origin="56, 8" size="90, 18"
          control-tag="GlobalFilterType"
          font="~ NormalFontSmaller" font-color="text-menu"
          back-color="bg-dropdown" frame-color="frame-dropdown"/>

    <!-- Cutoff knob + label (beside, to the right) -->
    <view class="ArcKnob" origin="200, 4" size="24, 24"
          control-tag="GlobalFilterCutoff" default-value="0.574"
          arc-color="global-filter" guide-color="knob-guide"/>
    <view class="CTextLabel" origin="228, 10" size="40, 12"
          font="~ NormalFontSmaller" font-color="text-secondary"
          text-alignment="left" transparent="true" title="Cutoff"/>

    <!-- Resonance knob + label (beside, to the right) -->
    <view class="ArcKnob" origin="320, 4" size="24, 24"
          control-tag="GlobalFilterResonance" default-value="0.020"
          arc-color="global-filter" guide-color="knob-guide"/>
    <view class="CTextLabel" origin="348, 10" size="36, 12"
          font="~ NormalFontSmaller" font-color="text-secondary"
          text-alignment="left" transparent="true" title="Reso"/>
</view>
```

## 7. Trance Gate Toolbar Modifications

### Add Sync Toggle (insert after On/Off toggle, shift NoteValue and NumSteps right)

Current toolbar controls:
```
[On/Off] at (8, 14)    size (40, 18)
[NoteValue] at (56, 14) size (70, 18)
[NumSteps] at (130, 14) size (56, 18)
```

New toolbar controls:
```
[On/Off] at (8, 14)      size (40, 18)    -- unchanged
[Sync] at (56, 14)       size (50, 18)    -- NEW
[NoteValue] at (110, 14) size (70, 18)    -- shifted right
[NumSteps] at (184, 14)  size (56, 18)    -- shifted right
```

Sync toggle XML:
```xml
<view class="ToggleButton" origin="56, 14" size="50, 18"
      control-tag="TranceGateSync"
      default-value="1.0"
      title="Sync"
      title-position="right"
      font="~ NormalFontSmaller"
      font-color="trance-gate"
      text-color="trance-gate"
      tooltip="Enable tempo sync"
      transparent="true"/>
```

### Wrap Rate Knob + Label in Visibility Container

Current Rate knob (at origin 380, 108 in FieldsetContainer):
```xml
<view class="ArcKnob" origin="380, 108" size="28, 28"
      control-tag="TranceGateRate" .../>
<view class="CTextLabel" origin="374, 136" size="40, 10"
      ... title="Rate"/>
```

Wrap in container (initially hidden because default sync=on):
```xml
<!-- Rate (hidden when sync active — default sync=on) -->
<view class="CViewContainer" origin="380, 108" size="48, 38"
      custom-view-name="TranceGateRateGroup" transparent="true"
      visible="false">
    <view class="ArcKnob" origin="0, 0" size="28, 28"
          control-tag="TranceGateRate" default-value="0.5"
          arc-color="trance-gate" guide-color="knob-guide"/>
    <view class="CTextLabel" origin="-6, 28" size="40, 10"
          font="~ NormalFontSmaller" font-color="text-secondary"
          text-alignment="center" transparent="true" title="Rate"/>
</view>
```

### Add NoteValue Visibility Container (at same position as Rate)

```xml
<!-- Note Value (visible when sync active — default sync=on) -->
<view class="CViewContainer" origin="380, 108" size="80, 38"
      custom-view-name="TranceGateNoteValueGroup" transparent="true">
    <view class="COptionMenu" origin="0, 4" size="70, 20"
          control-tag="TranceGateNoteValue"
          font="~ NormalFontSmaller"
          font-color="trance-gate"
          back-color="bg-dropdown"
          frame-color="frame-dropdown"
          transparent="false"/>
    <view class="CTextLabel" origin="0, 28" size="70, 10"
          font="~ NormalFontSmaller" font-color="text-secondary"
          text-alignment="center" transparent="true" title="Note"/>
</view>
```

## 8. Controller Changes Contract

### controller.h -- Add pointer declarations

After `phaserNoteValueGroup_` (line 232):
```cpp
/// Trance Gate Rate/NoteValue groups - toggled by sync state
VSTGUI::CView* tranceGateRateGroup_ = nullptr;
VSTGUI::CView* tranceGateNoteValueGroup_ = nullptr;
```

### controller.cpp -- setParamNormalized() visibility toggle

After the `kPhaserSyncId` block (line 533):
```cpp
// Toggle TranceGate Rate/NoteValue visibility based on sync state
if (tag == kTranceGateTempoSyncId) {
    if (tranceGateRateGroup_) tranceGateRateGroup_->setVisible(value < 0.5);
    if (tranceGateNoteValueGroup_) tranceGateNoteValueGroup_->setVisible(value >= 0.5);
}
```

### controller.cpp -- verifyView() pointer capture

After the `PhaserNoteValueGroup` block (line 875):
```cpp
// TranceGate Rate/NoteValue groups (toggled by sync state)
else if (*name == "TranceGateRateGroup") {
    tranceGateRateGroup_ = container;
    auto* syncParam = getParameterObject(kTranceGateTempoSyncId);
    bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
    container->setVisible(!syncOn);
} else if (*name == "TranceGateNoteValueGroup") {
    tranceGateNoteValueGroup_ = container;
    auto* syncParam = getParameterObject(kTranceGateTempoSyncId);
    bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
    container->setVisible(syncOn);
}
```

### controller.cpp -- willClose() cleanup

After `phaserNoteValueGroup_ = nullptr;` (line 607):
```cpp
tranceGateRateGroup_ = nullptr;
tranceGateNoteValueGroup_ = nullptr;
```
