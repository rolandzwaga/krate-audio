# Contract: Controller Wiring for Main Layout

**Spec**: 051-main-layout | **Date**: 2026-02-11

## Overview

The Ruinae controller (`plugins/ruinae/src/controller/controller.cpp`) needs additions to wire the new layout components. The existing wiring for StepPatternEditor, XYMorphPad, ADSRDisplay, ModMatrixGrid, ModHeatmap, and ModRingIndicator is already in place.

## New Wiring Required

### 1. OscillatorTypeSelector Wiring (2 instances)

In `verifyView()`:
```cpp
auto* oscSelector = dynamic_cast<Krate::Plugins::OscillatorTypeSelector*>(view);
if (oscSelector) {
    // Identity is set via XML attribute "osc-identity" ("a" or "b")
    // Tag binding handles parameter sync automatically
    // No manual wiring needed -- OscillatorTypeSelector uses
    // standard CControl value/beginEdit/endEdit for parameter changes
}
```

The OscillatorTypeSelector already handles its own value changes via the standard CControl mechanism. The `osc-identity` attribute is applied by the ViewCreator. No controller wiring is needed beyond what VSTGUI provides automatically.

### 2. FX Detail Panel Expand/Collapse

New member variables:
```cpp
VSTGUI::CViewContainer* fxDetailFreeze_ = nullptr;
VSTGUI::CViewContainer* fxDetailDelay_ = nullptr;
VSTGUI::CViewContainer* fxDetailReverb_ = nullptr;
int expandedFxPanel_ = -1;  // -1=none, 0=freeze, 1=delay, 2=reverb
```

In `verifyView()`, detect detail panels by custom-view-name:
```cpp
auto* container = dynamic_cast<VSTGUI::CViewContainer*>(view);
if (container) {
    auto name = attributes.getAttributeValue("custom-view-name");
    if (name) {
        if (*name == "FreezeDetail") {
            fxDetailFreeze_ = container;
            container->setVisible(false);
        } else if (*name == "DelayDetail") {
            fxDetailDelay_ = container;
            container->setVisible(false);
        } else if (*name == "ReverbDetail") {
            fxDetailReverb_ = container;
            container->setVisible(false);
        }
    }
}
```

Chevron buttons use dedicated action tags (not VST parameters):
```cpp
// In plugin_ids.h, add:
kActionFxExpandFreezeTag = 10010,
kActionFxExpandDelayTag = 10011,
kActionFxExpandReverbTag = 10012,

// In valueChanged() handler:
case kActionFxExpandFreezeTag:
    toggleFxDetail(0);
    break;
case kActionFxExpandDelayTag:
    toggleFxDetail(1);
    break;
case kActionFxExpandReverbTag:
    toggleFxDetail(2);
    break;
```

Helper method:
```cpp
void Controller::toggleFxDetail(int panelIndex) {
    auto panels = {fxDetailFreeze_, fxDetailDelay_, fxDetailReverb_};
    int idx = 0;
    for (auto* panel : panels) {
        if (panel) {
            panel->setVisible(idx == panelIndex && expandedFxPanel_ != panelIndex);
        }
        ++idx;
    }
    expandedFxPanel_ = (expandedFxPanel_ == panelIndex) ? -1 : panelIndex;
}
```

### 3. CategoryTabBar for Modulation Global/Voice Tabs

In `verifyView()`:
```cpp
auto* tabBar = dynamic_cast<Krate::Plugins::CategoryTabBar*>(view);
if (tabBar && modMatrixGrid_) {
    tabBar->setSelectionCallback([this](int tab) {
        if (modMatrixGrid_) {
            modMatrixGrid_->setActiveTab(tab);
        }
    });
}
```

### 4. willClose() Cleanup

Add to existing willClose():
```cpp
fxDetailFreeze_ = nullptr;
fxDetailDelay_ = nullptr;
fxDetailReverb_ = nullptr;
expandedFxPanel_ = -1;
```

## Existing Wiring (No Changes Needed)

| Component | Wiring Location | Status |
|-----------|----------------|--------|
| StepPatternEditor | verifyView() | Already wired |
| XYMorphPad | verifyView() | Already wired |
| ADSRDisplay (x3) | verifyView() -> wireAdsrDisplay() | Already wired |
| ModMatrixGrid | verifyView() -> wireModMatrixGrid() | Already wired |
| ModHeatmap | verifyView() | Already wired |
| ModRingIndicator | verifyView() -> wireModRingIndicator() | Already wired |
| Action buttons (presets, transforms) | verifyView() + valueChanged() | Already wired |

## entry.cpp Includes

Verify these shared UI headers are included in entry.cpp (triggers ViewCreator registration):
- `ui/arc_knob.h` -- already included
- `ui/fieldset_container.h` -- already included
- `ui/step_pattern_editor.h` -- already included
- `ui/xy_morph_pad.h` -- already included
- `ui/adsr_display.h` -- already included
- `ui/oscillator_type_selector.h` -- already included

Missing (need to add):
- `ui/bipolar_slider.h` -- needed for ModMatrixGrid route amounts
- `ui/mod_ring_indicator.h` -- needed for knob overlays
- `ui/mod_matrix_grid.h` -- needed for route list
- `ui/mod_heatmap.h` -- needed for route overview

Note: ModMatrixGrid, ModRingIndicator, and ModHeatmap may be included transitively via controller.cpp already. Check if their ViewCreators are registered by verifying they appear in the VSTGUI view factory at runtime. If not, add explicit includes to entry.cpp.
