# Quickstart: Step Pattern Editor Implementation

**Feature Branch**: `046-step-pattern-editor`
**Date**: 2026-02-09

## What We're Building

A shared VSTGUI custom view (`StepPatternEditor`) that provides visual and interactive editing of TranceGate step patterns. It is a bar chart control where users can click/drag to set gain levels for each step in a rhythmic gating pattern.

## Key Files to Create/Modify

### New Files

| File | Purpose |
|------|---------|
| `plugins/shared/src/ui/color_utils.h` | Shared color utility functions (extracted from ArcKnob/FieldsetContainer) |
| `plugins/shared/src/ui/step_pattern_editor.h` | The StepPatternEditor custom view (header-only, shared) |

### Modified Files

| File | Change |
|------|--------|
| `plugins/ruinae/src/plugin_ids.h` | Add new parameter IDs (608-611, 668-699) |
| `plugins/ruinae/src/parameters/trance_gate_params.h` | Add step level params, change numSteps to RangeParameter, add Euclidean params, update save/load |
| `plugins/ruinae/src/parameters/dropdown_mappings.h` | Remove or deprecate kNumStepsCount/kNumStepsStrings |
| `plugins/ruinae/src/processor/processor.h` | Add step level atomics, Euclidean atomics |
| `plugins/ruinae/src/processor/processor.cpp` | Handle new params in processParameterChanges, apply to TranceGate |
| `plugins/ruinae/src/controller/controller.cpp` | Wire StepPatternEditor callbacks, handle playback messages |
| `plugins/ruinae/src/entry.cpp` | Include step_pattern_editor.h for ViewCreator registration |
| `plugins/ruinae/resources/editor.uidesc` | Place StepPatternEditor in trance gate section |
| `plugins/shared/CMakeLists.txt` | Add color_utils.h and step_pattern_editor.h to sources |

### Test Files

| File | Purpose |
|------|---------|
| `plugins/shared/tests/test_color_utils.cpp` | Unit tests for color utilities |
| `plugins/shared/tests/test_step_pattern_editor.cpp` | Unit tests for editor logic (layout, hit testing, button regions) |

## Architecture Overview

```
+-------------------------------------------------------------------+
|  StepPatternEditor (plugins/shared/src/ui/)                       |
|  - CControl subclass, header-only                                 |
|  - draw(), onMouseDown/Moved/Up, onKeyDown, onWheel              |
|  - Internal zones: bars, buttons, euclidean controls, indicators  |
|  - ParameterCallback for multi-parameter communication            |
|  - Uses ColorUtils, EuclideanPattern (DSP Layer 0)                |
+-------------------------------------------------------------------+
         |                                    ^
         | ParameterCallback                 | setStepLevel()
         | (paramId, normalizedValue)         | setNumSteps()
         v                                    | setPlaybackStep()
+-------------------------------------------------------------------+
|  Ruinae Controller                                                 |
|  - Wires callback to beginEdit/performEdit/endEdit               |
|  - Receives IMessage for playback position                        |
|  - Updates StepPatternEditor from host parameter changes          |
+-------------------------------------------------------------------+
         |                                    ^
         | performEdit()                     | IMessage
         v                                    |
+-------------------------------------------------------------------+
|  Host (DAW)                                                        |
|  - Stores 32 step level parameters                                |
|  - Manages undo/redo via beginEdit/endEdit pairs                  |
+-------------------------------------------------------------------+
         |                                    ^
         | IParameterChanges                 | processOutputParameterChanges
         v                                    |
+-------------------------------------------------------------------+
|  Ruinae Processor                                                  |
|  - Reads step levels from parameter changes                       |
|  - Applies to TranceGate DSP processor                            |
|  - Sends playback step position via IMessage                      |
+-------------------------------------------------------------------+
```

## Implementation Order

### Phase 1: Foundation (P1 - Core Display + Interaction)

1. **Color utilities extraction** - Create `color_utils.h`, update ArcKnob and FieldsetContainer to use it
2. **Parameter IDs** - Add new IDs to `plugin_ids.h`
3. **StepPatternEditor core** - Implement the view with:
   - Bar chart rendering (FR-001, FR-002, FR-003, FR-004)
   - Click-and-drag level editing (FR-005, FR-006)
   - Double-click reset (FR-007), Alt+click toggle (FR-008)
   - Shift+drag fine mode (FR-009)
   - Escape cancel (FR-010)
   - beginEdit/endEdit gesture management (FR-011)
4. **Parameter registration** - Register 32 step level params in trance_gate_params.h
5. **Processor integration** - Handle step level changes in processParameterChanges
6. **Controller wiring** - Wire callback in controller, place view in uidesc

### Phase 2: Extended Features (P2 - Step Count + Euclidean)

7. **NumSteps parameter change** - Migrate from dropdown to RangeParameter
8. **Dynamic step count** - Implement FR-015, FR-016, FR-017 in the view
9. **Euclidean mode** - Internal controls, pattern generation, dot indicators (FR-018 through FR-023)

### Phase 3: Polish (P3 - Playback, Quick Actions, Zoom)

10. **Playback position** - Timer, IMessage, position indicator (FR-024 through FR-027)
11. **Phase offset indicator** - FR-028
12. **Quick action buttons** - Internal button hit regions (FR-029, FR-030, FR-031)
13. **Zoom/scroll** - Mouse wheel zoom/scroll for 24+ steps (FR-032, FR-033, FR-034)

## Critical Patterns to Follow

### ViewCreator Registration (from ArcKnob)
```cpp
struct StepPatternEditorCreator : VSTGUI::ViewCreatorAdapter {
    StepPatternEditorCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }
    VSTGUI::IdStringPtr getViewName() const override { return "StepPatternEditor"; }
    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }
    // ... apply(), getAttributeNames(), getAttributeType(), getAttributeValue()
};
inline StepPatternEditorCreator gStepPatternEditorCreator;
```

### ParameterCallback Wiring (from TapPatternEditor)
```cpp
// In controller verifyView() or didOpen():
if (auto* editor = dynamic_cast<StepPatternEditor*>(view)) {
    editor->setStepLevelBaseParamId(kTranceGateStepLevel0Id);
    editor->setParameterCallback([this](ParamID id, float value) {
        performEdit(id, value);
    });
    editor->setBeginEditCallback([this](ParamID id) {
        beginEdit(id);
    });
    editor->setEndEditCallback([this](ParamID id) {
        endEdit(id);
    });
}
```

### Timer Pattern (from SpectrumDisplay)
```cpp
// Start:
refreshTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
    [this](VSTGUI::CVSTGUITimer*) {
        invalid();  // Trigger redraw
    }, 33);  // ~30fps

// Stop:
refreshTimer_ = nullptr;  // SharedPointer releases
```

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build shared library + Ruinae plugin
"$CMAKE" --build build/windows-x64-release --config Release --target KratePluginsShared
"$CMAKE" --build build/windows-x64-release --config Release --target Ruinae

# Run tests
"$CMAKE" --build build/windows-x64-release --config Release --target shared_tests
build/windows-x64-release/plugins/shared/tests/Release/shared_tests.exe

# Run pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```
