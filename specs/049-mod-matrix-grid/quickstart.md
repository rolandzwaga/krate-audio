# Quickstart: ModMatrixGrid -- Modulation Routing UI

**Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Build and Test

```bash
# 1. Configure
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --preset windows-x64-release

# 2. Build
"$CMAKE" --build build/windows-x64-release --config Release

# 3. Run tests
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe

# 4. Run pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

## Implementation Order

### Phase 1: Foundation (no UI rendering)

1. Add parameter IDs to plugin_ids.h (1300-1355, base params 1300-1323 already exist; add detail params 1324-1355)
2. Create mod_source_colors.h (shared color/name registry with ModSource, ModDestination enums)
3. Create ModRoute/VoiceModRoute structs in mod_source_colors.h
4. Register 56 parameters in Controller::initialize()
5. Add parameter handling in Processor::processParameterChanges()
6. Add state save/load for mod matrix parameters
7. Write parameter round-trip tests

### Phase 2: Core Controls

8. Implement BipolarSlider (CControl with centered fill, fine adjustment)
9. Write BipolarSlider tests (value mapping, fine adjustment, escape cancel)
10. Implement ModMatrixGrid skeleton (CViewContainer, tab bar, route row layout)
11. Implement route row rendering (source color dot, dropdowns, BipolarSlider, remove button)
12. Implement add/remove route logic (max 8 global, max 16 voice)
13. Write ModMatrixGrid interaction tests

### Phase 3: Advanced Controls

14. Implement expandable detail rows (Curve, Smooth, Scale, Bypass)
15. Implement Global/Voice tab switching
16. Implement IMessage protocol for voice routes (bidirectional with processor)
17. Implement ModHeatmap (grid rendering, cell click, tooltips)
18. Write tab switching and heatmap tests

### Phase 4: ModRingIndicator

19. Implement ModRingIndicator (arc rendering, stacked arcs up to 4, clamping)
20. Implement click-to-select (controller mediation via selectModulationRoute)
21. Implement hover tooltips (dynamic setTooltipText based on arc hit testing)
22. Write ModRingIndicator rendering tests

### Phase 5: Integration

23. Add all 3 views to editor.uidesc layout
24. Implement ModMatrixSubController for slot tag remapping
25. Wire up IDependent observation for all components
26. Place ModRingIndicator overlays on destination knobs in .uidesc
27. Cross-component integration testing
28. Pluginval validation
29. Architecture documentation update

## Key Files to Modify

| File | Change |
|---|---|
| `plugins/ruinae/src/plugin_ids.h` | Add 32 detail param IDs (1324-1355); base params 1300-1323 already exist |
| `plugins/ruinae/src/controller/controller.h` | Add mod matrix parameter objects, voice route cache, IMessage handling |
| `plugins/ruinae/src/controller/controller.cpp` | Register params, handle IMessages, mediate cross-component communication |
| `plugins/ruinae/src/processor/processor.h` | Add voice route IMessage handling, mod matrix atomic storage |
| `plugins/ruinae/src/processor/processor.cpp` | Handle voice route messages, save/load mod matrix state |
| `plugins/ruinae/resources/editor.uidesc` | Add modulation panel, route templates, knob overlays |
| `plugins/shared/CMakeLists.txt` | Add new .h files (header-only, but for IDE visibility) |

## New Files to Create

| File | Purpose |
|---|---|
| `plugins/shared/src/ui/mod_source_colors.h` | ModSource/ModDestination enums, ModRoute/VoiceModRoute structs, ModSourceInfo/ModDestInfo registries |
| `plugins/shared/src/ui/mod_matrix_grid.h` | ModMatrixGrid CViewContainer + ViewCreator |
| `plugins/shared/src/ui/mod_ring_indicator.h` | ModRingIndicator CView overlay + ViewCreator |
| `plugins/shared/src/ui/mod_heatmap.h` | ModHeatmap CView + ViewCreator |
| `plugins/shared/src/ui/bipolar_slider.h` | BipolarSlider CControl + ViewCreator |
| `plugins/ruinae/tests/mod_matrix_tests.cpp` | Unit tests for data model + parameter round-trip |

## Critical Patterns to Follow

1. **ViewCreator Registration**: Every new CView/CControl/CViewContainer must have a static ViewCreatorAdapter struct. See ArcKnobCreator, StepPatternEditorCreator for examples.

2. **ParameterCallback Pattern**: ModMatrixGrid uses `ParameterCallback = std::function<void(int32_t, float)>` for notifying the controller of parameter changes (same as StepPatternEditor and ADSRDisplay).

3. **beginEdit/endEdit Wrapping**: All slider drags must call beginEdit before the first value change and endEdit after the last. See XYMorphPad::onMouseDownEvent and onMouseUpEvent.

4. **IDependent for UI Updates**: ModRingIndicator and ModHeatmap register as dependents on the modulation parameters. When Parameter::changed() fires, update() is called on the UI thread (deferred), triggering setDirty() and redraw.

5. **No Timer for ModRingIndicator**: Per spec FR-030, ModRingIndicator does NOT use CVSTGUITimer. It redraws only when IDependent notifies of parameter changes.

6. **Controller Mediation**: ModRingIndicator and ModHeatmap call controller->selectModulationRoute(source, dest) for route selection. Controller forwards to ModMatrixGrid.

7. **DelegationController for Route Slots**: ModMatrixSubController uses getTagForName() to remap generic tag names ("Route::Source", "Route::Amount") to slot-specific parameter IDs.

## Parameter ID Formulas

For global slot N (0-7):
- Source ID = 1300 + N * 3
- Destination ID = 1301 + N * 3
- Amount ID = 1302 + N * 3
- Curve ID = 1324 + N * 4
- Smooth ID = 1325 + N * 4
- Scale ID = 1326 + N * 4
- Bypass ID = 1327 + N * 4

## Normalized to Plain Value Mapping

| Parameter | Normalized [0,1] | Plain Range | Formula |
|---|---|---|---|
| Source | [0.0, 1.0] | 0-9 (Global) | Automatic via StringListParameter stepCount |
| Destination | [0.0, 1.0] | 0-10 (Global) | Automatic via StringListParameter stepCount |
| Amount | [0.0, 1.0] | [-1.0, +1.0] | plain = normalized * 2.0 - 1.0 |
| Curve | [0.0, 1.0] | 0-3 | Automatic via StringListParameter stepCount |
| Smooth | [0.0, 1.0] | [0.0, 100.0] ms | plain = normalized * 100.0 |
| Scale | [0.0, 1.0] | 0-4 | Automatic via StringListParameter stepCount |
| Bypass | [0.0, 1.0] | 0 or 1 | plain = normalized >= 0.5 ? 1 : 0 |
