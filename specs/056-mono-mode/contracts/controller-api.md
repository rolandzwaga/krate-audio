# Controller API Contract: Mono Mode Visibility Wiring

**Date**: 2026-02-15 | **Spec**: [spec.md](../spec.md)

## Overview

This contract documents the exact code changes to the Ruinae Controller class for mono mode visibility wiring. No new public APIs are created. All changes are internal to the Controller class, extending existing methods with additional case branches.

## controller.h Changes

### New Fields (after tranceGateNoteValueGroup_, line 235)

```cpp
    /// Poly/Mono visibility groups - toggled by voice mode
    VSTGUI::CView* polyGroup_ = nullptr;
    VSTGUI::CView* monoGroup_ = nullptr;
```

**Contract**: Both fields are initialized to nullptr and managed identically to the 12 existing Rate/NoteValue group pointers.

## controller.cpp Changes

### setParamNormalized() -- New Case (after line 538)

```cpp
    // Toggle Poly/Mono visibility based on voice mode
    if (tag == kVoiceModeId) {
        if (polyGroup_) polyGroup_->setVisible(value < 0.5);
        if (monoGroup_) monoGroup_->setVisible(value >= 0.5);
    }
```

**Contract**: Follows the exact same pattern as lines 510-538. Null-checked, no blocking operations, safe to call from any thread.

### verifyView() -- New Cases (after TranceGateNoteValueGroup case, line 894)

```cpp
            // Poly/Mono visibility groups (toggled by voice mode)
            else if (*name == "PolyGroup") {
                polyGroup_ = container;
                auto* voiceModeParam = getParameterObject(kVoiceModeId);
                bool isMono = (voiceModeParam != nullptr) && voiceModeParam->getNormalized() >= 0.5;
                container->setVisible(!isMono);
            } else if (*name == "MonoGroup") {
                monoGroup_ = container;
                auto* voiceModeParam = getParameterObject(kVoiceModeId);
                bool isMono = (voiceModeParam != nullptr) && voiceModeParam->getNormalized() >= 0.5;
                container->setVisible(isMono);
            }
```

**Contract**: Follows the exact same pattern as lines 824-894. Captures pointer, reads current parameter value, sets initial visibility. Handles the case where the editor opens with Voice Mode already set to Mono (from a preset).

### willClose() -- New Cleanup (after line 614)

```cpp
        polyGroup_ = nullptr;
        monoGroup_ = nullptr;
```

**Contract**: Follows the exact same cleanup pattern as lines 603-614.

## uidesc XML Contract

### Control-Tags (after Global Filter section, line 75)

```xml
        <!-- Mono Mode -->
        <control-tag name="MonoPriority" tag="1800"/>
        <control-tag name="MonoLegato" tag="1801"/>
        <control-tag name="MonoPortamentoTime" tag="1802"/>
        <control-tag name="MonoPortaMode" tag="1803"/>
```

**Contract**: Tag values MUST match kMonoPriorityId (1800), kMonoLegatoId (1801), kMonoPortamentoTimeId (1802), kMonoPortaModeId (1803) in plugin_ids.h.

### PolyGroup Container (replaces bare Polyphony dropdown at lines 2736-2744)

```xml
            <!-- Row 2: Polyphony dropdown (visible when Voice Mode = Poly) -->
            <view class="CViewContainer" origin="8, 36" size="112, 18"
                  custom-view-name="PolyGroup" transparent="true">
                <view class="COptionMenu" origin="0, 0" size="60, 18"
                      control-tag="Polyphony"
                      font="~ NormalFontSmaller"
                      font-color="master"
                      back-color="bg-dropdown"
                      frame-color="frame-dropdown-dim"
                      tooltip="Polyphony"
                      transparent="false"/>
            </view>
```

**Contract**: The Polyphony COptionMenu is wrapped in a container. Its origin changes from (8,36) to (0,0) relative to the new container. All other attributes are preserved exactly.

### MonoGroup Container (new, immediately after PolyGroup)

See data-model.md for the full XML structure and child layout.

**Contract**: MonoGroup starts with `visible="false"`. All mono controls use `master` accent color. COptionMenu dropdowns auto-populate from StringListParameter. ArcKnob follows existing knob styling.
