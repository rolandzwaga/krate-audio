# UI Description Contract: Mod Source Dropdown

**Date**: 2026-02-14 | **Spec**: [../spec.md](../spec.md)

## Overview

This contract defines the XML structure changes in `plugins/ruinae/resources/editor.uidesc` for the mod source dropdown migration.

## Control Tag (existing, unchanged)

```xml
<control-tag name="ModSourceViewMode" tag="10019"/>
```

## Parameter Registration (modified)

**File**: `plugins/ruinae/src/parameters/chaos_mod_params.h`
**Function**: `registerChaosModParams()`

Before (3 entries):
```cpp
auto* modViewParam = new StringListParameter(
    STR16("Mod Source View"), kModSourceViewModeTag);
modViewParam->appendString(STR16("LFO 1"));
modViewParam->appendString(STR16("LFO 2"));
modViewParam->appendString(STR16("Chaos"));
parameters.addParameter(modViewParam);
```

After (10 entries):
```cpp
auto* modViewParam = new StringListParameter(
    STR16("Mod Source View"), kModSourceViewModeTag);
modViewParam->appendString(STR16("LFO 1"));
modViewParam->appendString(STR16("LFO 2"));
modViewParam->appendString(STR16("Chaos"));
modViewParam->appendString(STR16("Macros"));
modViewParam->appendString(STR16("Rungler"));
modViewParam->appendString(STR16("Env Follower"));
modViewParam->appendString(STR16("S&H"));
modViewParam->appendString(STR16("Random"));
modViewParam->appendString(STR16("Pitch Follower"));
modViewParam->appendString(STR16("Transient"));
parameters.addParameter(modViewParam);
```

## COptionMenu Dropdown (replaces IconSegmentButton)

**Replaces**:
```xml
<view class="IconSegmentButton" origin="0, 14" size="158, 18"
      control-tag="ModSourceViewMode" default-value="0"
      segment-names="LFO 1,LFO 2,Chaos"
      selected-color="modulation" unselected-color="text-secondary"
      frame-color="frame-dropdown" highlight-color="bg-dropdown"
      round-radius="3" font-size="9" stroke-width="1.5"/>
```

**With**:
```xml
<view class="COptionMenu" origin="8, 14" size="140, 18"
      control-tag="ModSourceViewMode" default-value="0"
      font="~ NormalFontSmaller" font-color="text-menu"
      back-color="bg-dropdown" frame-color="frame-dropdown"/>
```

Note: Menu items are populated automatically from the `StringListParameter` by VSTGUI's VST3Editor binding.

## UIViewSwitchContainer (replaces 3 inline CViewContainers)

**Replaces** three inline `CViewContainer` blocks (lines 2019-2259):
- `ModLFO1View` container (`origin="0, 36" size="158, 120"`)
- `ModLFO2View` container (`origin="0, 36" size="158, 120"`)
- `ModChaosView` container (`origin="0, 36" size="158, 106"`)

**With**:
```xml
<view class="UIViewSwitchContainer"
      origin="0, 36" size="158, 120"
      template-names="ModSource_LFO1,ModSource_LFO2,ModSource_Chaos,ModSource_Macros,ModSource_Rungler,ModSource_EnvFollower,ModSource_SampleHold,ModSource_Random,ModSource_PitchFollower,ModSource_Transient"
      template-switch-control="ModSourceViewMode"
      transparent="true"/>
```

## Named Templates (10 total, placed before `<template name="editor">`)

### ModSource_LFO1

Extracted verbatim from the inline LFO1 view (uidesc lines 2019-2107), but as a named template:

```xml
<template name="ModSource_LFO1" size="158, 120" class="CViewContainer" transparent="true">
    <!-- [All LFO1 controls from the inline view, unchanged] -->
</template>
```

### ModSource_LFO2

Extracted verbatim from the inline LFO2 view (uidesc lines 2112-2201):

```xml
<template name="ModSource_LFO2" size="158, 120" class="CViewContainer" transparent="true">
    <!-- [All LFO2 controls from the inline view, unchanged] -->
</template>
```

### ModSource_Chaos

Extracted from the inline Chaos view (uidesc lines 2206-2259). Template size adjusted to `158, 120` (from `158, 106`) to match UIViewSwitchContainer size:

```xml
<template name="ModSource_Chaos" size="158, 120" class="CViewContainer" transparent="true">
    <!-- [All Chaos controls from the inline view, unchanged] -->
</template>
```

### Placeholder Templates (7 total)

```xml
<template name="ModSource_Macros" size="158, 120" class="CViewContainer" transparent="true"/>
<template name="ModSource_Rungler" size="158, 120" class="CViewContainer" transparent="true"/>
<template name="ModSource_EnvFollower" size="158, 120" class="CViewContainer" transparent="true"/>
<template name="ModSource_SampleHold" size="158, 120" class="CViewContainer" transparent="true"/>
<template name="ModSource_Random" size="158, 120" class="CViewContainer" transparent="true"/>
<template name="ModSource_PitchFollower" size="158, 120" class="CViewContainer" transparent="true"/>
<template name="ModSource_Transient" size="158, 120" class="CViewContainer" transparent="true"/>
```

## Controller Code Removals

### Header (`controller.h`)

Remove member variables:
```cpp
// REMOVE these 4 lines (including comment):
/// Mod source view containers - switched by ModSourceViewMode segment button
VSTGUI::CView* modLFO1View_ = nullptr;
VSTGUI::CView* modLFO2View_ = nullptr;
VSTGUI::CView* modChaosView_ = nullptr;
```

### valueChanged (`controller.cpp`)

Remove the mod source visibility toggle block:
```cpp
// REMOVE this block:
// Toggle mod source view (LFO1 / LFO2 / Chaos) based on segment button
if (tag == kModSourceViewModeTag) {
    int sel = static_cast<int>(std::round(value * 2.0));
    if (modLFO1View_)  modLFO1View_->setVisible(sel == 0);
    if (modLFO2View_)  modLFO2View_->setVisible(sel == 1);
    if (modChaosView_) modChaosView_->setVisible(sel == 2);
}
```

### verifyView (`controller.cpp`)

Remove the mod source view registration branches:
```cpp
// REMOVE these branches (lines 824-840):
// Mod source view containers (switched by segment button)
else if (*name == "ModLFO1View") {
    modLFO1View_ = container;
    auto* viewParam = getParameterObject(kModSourceViewModeTag);
    int sel = viewParam ? static_cast<int>(std::round(viewParam->getNormalized() * 2.0)) : 0;
    container->setVisible(sel == 0);
} else if (*name == "ModLFO2View") {
    modLFO2View_ = container;
    auto* viewParam = getParameterObject(kModSourceViewModeTag);
    int sel = viewParam ? static_cast<int>(std::round(viewParam->getNormalized() * 2.0)) : 0;
    container->setVisible(sel == 1);
} else if (*name == "ModChaosView") {
    modChaosView_ = container;
    auto* viewParam = getParameterObject(kModSourceViewModeTag);
    int sel = viewParam ? static_cast<int>(std::round(viewParam->getNormalized() * 2.0)) : 0;
    container->setVisible(sel == 2);
}
```

Note: The `custom-view-name` attributes `ModLFO1View`, `ModLFO2View`, `ModChaosView` are removed from the uidesc XML (they were on the inline containers). The inner custom-view-names (`LFO1RateGroup`, `LFO1NoteValueGroup`, etc.) are preserved in the extracted templates and continue to work.
