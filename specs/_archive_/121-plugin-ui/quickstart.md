# Quickstart: Innexus Plugin UI Implementation

## Prerequisites

- Milestones 1-6 complete (all DSP, parameters, processor logic)
- Build: `cmake --build build/windows-x64-release --config Release --target innexus_tests` passes
- `plugins/innexus/src/plugin_ids.h` has all 48 parameter IDs
- `plugins/innexus/src/controller/controller.h` exists with basic EditControllerEx1

## Implementation Order

### Phase 1: Display Data Pipeline

1. Create `DisplayData` struct in `plugins/innexus/src/controller/display_data.h`
2. Extend `Processor` to populate and send `DisplayData` via IMessage at end of `process()`
3. Extend `Controller::notify()` to receive and cache `DisplayData`
4. Write tests for send/receive

### Phase 2: Controller Infrastructure

1. Extend `Controller` to inherit `VST3EditorDelegate`
2. Implement `createView()` returning `VST3Editor` with `editor.uidesc`
3. Implement `didOpen()` / `willClose()` with CVSTGUITimer lifecycle
4. Implement `createCustomView()` stub returning all 5 custom views
5. Implement `createSubController()` for `ModulatorController`
6. Write tests for controller lifecycle

### Phase 3: Custom Views

1. Implement `HarmonicDisplayView` (draw 48 bars with dB scaling)
2. Implement `ConfidenceIndicatorView` (color-coded bar + note name)
3. Implement `MemorySlotStatusView` (8 circles)
4. Implement `EvolutionPositionView` (track + playhead + ghost)
5. Implement `ModulatorActivityView` (pulsing indicator)
6. Write unit tests for each view's drawing logic

### Phase 4: Modulator Sub-Controller

1. Implement `ModulatorSubController` with tag remapping
2. Write tests for tag resolution

### Phase 5: editor.uidesc Layout

1. Define control-tags for all 48 parameters
2. Build header section (Bypass, Gain, Source, Latency)
3. Build display section (Spectral + Confidence)
4. Build musical control section (Freeze, Morph, Filter, Responsiveness)
5. Build oscillator/residual section
6. Build creative extensions sections (Memory, Cross/Stereo, Evolution, Detune)
7. Build modulator template and instantiate twice
8. Build blend section with 8 slot weights

### Phase 6: Integration & Verification

1. Build and run pluginval at strictness level 5
2. Verify all 48 parameters have controls (SC-001)
3. Verify spectral display frame rate (SC-003)
4. Verify editor open/close cycling (SC-005)
5. Run full test suite

## Key Files to Create

| File | Purpose |
|------|---------|
| `src/controller/display_data.h` | DisplayData struct |
| `src/controller/modulator_sub_controller.h` | Sub-controller for mod template |
| `src/controller/views/harmonic_display_view.h/.cpp` | Spectral bar display |
| `src/controller/views/confidence_indicator_view.h/.cpp` | F0 confidence meter |
| `src/controller/views/memory_slot_status_view.h/.cpp` | Memory slot indicators |
| `src/controller/views/evolution_position_view.h/.cpp` | Evolution playhead track |
| `src/controller/views/modulator_activity_view.h/.cpp` | Modulator activity indicator |
| `resources/editor.uidesc` | Full UI definition (replaces placeholder) |

## Key Files to Modify

| File | Changes |
|------|---------|
| `src/controller/controller.h` | Add VST3EditorDelegate, fields for views/timer/data |
| `src/controller/controller.cpp` | Implement createView, createCustomView, createSubController, didOpen, willClose, notify extension |
| `src/processor/processor.h` | Add sendDisplayData() method |
| `src/processor/processor.cpp` | Call sendDisplayData() in process() |
| `tests/CMakeLists.txt` | Add new test source files |

## Build & Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build
"$CMAKE" --build build/windows-x64-release --config Release --target innexus_tests

# Run tests
build/windows-x64-release/bin/Release/innexus_tests.exe

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"
```

## Shared Components to Use

All standard controls use vector-drawn shared components -- no bitmap assets needed:

- `Krate::Plugins::ArcKnob` for continuous knobs
- `Krate::Plugins::ToggleButton` for on/off toggles
- `Krate::Plugins::ActionButton` for momentary buttons
- `Krate::Plugins::BipolarSlider` for bipolar controls
- `Krate::Plugins::FieldsetContainer` for labeled section borders
- `Krate::Plugins::lerpColor` / `darkenColor` for color interpolation
