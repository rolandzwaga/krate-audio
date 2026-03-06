# Modulator Template & Sub-Controller Contract

## Template Definition (FR-046)

A single VSTGUI template `"modulator_panel"` is defined once in `editor.uidesc` and instantiated twice (once for Mod 1, once for Mod 2).

### Template XML Structure

```xml
<template name="modulator_panel" class="CViewContainer"
          sub-controller="ModulatorController" size="185, 120">
    <!-- Enable toggle -->
    <view class="Krate::Plugins::ToggleButton"
          control-tag="Mod.Enable" origin="5, 5" size="24, 24"
          icon-style="power" on-color="#00bcd4" off-color="#555555" />

    <!-- Section label -->
    <view class="CTextLabel" title="Modulator" origin="32, 5" size="100, 20"
          font="~ NormalFontSmall" text-alignment="left" font-color="#cccccc" />

    <!-- Waveform selector -->
    <view class="COptionMenu" control-tag="Mod.Waveform"
          origin="5, 32" size="80, 20" font="~ NormalFontVerySmall" />

    <!-- Rate knob -->
    <view class="Krate::Plugins::ArcKnob" control-tag="Mod.Rate"
          origin="5, 55" size="40, 40" arc-color="#00bcd4" />

    <!-- Depth knob -->
    <view class="Krate::Plugins::ArcKnob" control-tag="Mod.Depth"
          origin="50, 55" size="40, 40" arc-color="#00bcd4" />

    <!-- Range Start knob -->
    <view class="Krate::Plugins::ArcKnob" control-tag="Mod.RangeStart"
          origin="95, 55" size="40, 40" arc-color="#00bcd4" />

    <!-- Range End knob -->
    <view class="Krate::Plugins::ArcKnob" control-tag="Mod.RangeEnd"
          origin="140, 55" size="40, 40" arc-color="#00bcd4" />

    <!-- Target selector -->
    <view class="CSegmentButton" control-tag="Mod.Target"
          origin="5, 98" size="130, 18" segment-names="Amp,Freq,Pan" />

    <!-- Activity indicator (custom view) -->
    <view class="CView" custom-view-name="ModulatorActivity"
          origin="155, 5" size="25, 20" />
</template>
```

### Instantiation in Editor

```xml
<!-- In the editor template, bottom section: -->
<view template="modulator_panel" origin="10, 450" />   <!-- Mod 1 -->
<view template="modulator_panel" origin="200, 450" />  <!-- Mod 2 -->
```

## Sub-Controller Contract

### Class: `ModulatorSubController`

**Base**: `VSTGUI::DelegationController`

**Constructor**: `ModulatorSubController(int modIndex, VSTGUI::IController* parent)`

### Tag Remapping

| Template Tag | Resolved ID |
|---|---|
| `"Mod.Enable"` | `kMod1EnableId + modIndex * 10` |
| `"Mod.Waveform"` | `kMod1WaveformId + modIndex * 10` |
| `"Mod.Rate"` | `kMod1RateId + modIndex * 10` |
| `"Mod.Depth"` | `kMod1DepthId + modIndex * 10` |
| `"Mod.RangeStart"` | `kMod1RangeStartId + modIndex * 10` |
| `"Mod.RangeEnd"` | `kMod1RangeEndId + modIndex * 10` |
| `"Mod.Target"` | `kMod1TargetId + modIndex * 10` |

### Factory

```cpp
// In Controller::createSubController():
if (std::strcmp(name, "ModulatorController") == 0) {
    return new ModulatorSubController(modInstanceCounter_++, this);
}
```

**Counter Reset**: `modInstanceCounter_` is reset to 0 in `willClose()` so editor re-open works correctly.

### verifyView Override

The sub-controller's `verifyView()` checks for `ModulatorActivityView` instances and sets their `modIndex_` field so the controller knows which modulator data to feed.

```cpp
CView* verifyView(CView* view, const UIAttributes& attrs, const IUIDescription* desc) override {
    if (auto* actView = dynamic_cast<ModulatorActivityView*>(view)) {
        actView->setModIndex(modIndex_);
    }
    return DelegationController::verifyView(view, attrs, desc);
}
```
