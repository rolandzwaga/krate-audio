# Quickstart: Ruinae Preset Browser Integration

**Date**: 2026-02-27

## What This Feature Does

Adds a full preset browser and save dialog to the Ruinae synthesizer plugin, matching the functionality already present in Iterum and Disrumpo. Users can browse factory presets by category, save user presets, search by name, delete user presets, and import .vstpreset files.

## Architecture Overview

```
+------------------------------------------------------------------+
|                    Ruinae Controller                              |
|  +------------------------------------------------------------+  |
|  | initialize()                                                |  |
|  |   presetManager_ (already exists)                          |  |
|  |   + stateProvider callback  --> createComponentStateStream()|  |
|  |   + loadProvider callback   --> loadComponentStateWithNotify|  |
|  +------------------------------------------------------------+  |
|  | didOpen()                                                   |  |
|  |   + PresetBrowserView (added to frame)                     |  |
|  |   + SavePresetDialogView (added to frame)                  |  |
|  +------------------------------------------------------------+  |
|  | createCustomView()                                          |  |
|  |   + "PresetBrowserButton" --> PresetBrowserButton class     |  |
|  |   + "SavePresetButton"    --> SavePresetButton class        |  |
|  +------------------------------------------------------------+  |
|  | willClose()                                                 |  |
|  |   presetBrowserView_ = nullptr                             |  |
|  |   savePresetDialogView_ = nullptr                          |  |
|  +------------------------------------------------------------+  |
+------------------------------------------------------------------+
```

## Key Files to Modify

| File | Changes |
|------|---------|
| `plugins/ruinae/src/controller/controller.h` | Add fields (presetBrowserView_, savePresetDialogView_), method declarations (openPresetBrowser, closePresetBrowser, openSavePresetDialog, createCustomView, createComponentStateStream, loadComponentStateWithNotify, editParamWithNotify) |
| `plugins/ruinae/src/controller/controller.cpp` | Add PresetBrowserButton/SavePresetButton classes (anon namespace), stateProvider/loadProvider wiring in initialize(), overlay creation in didOpen(), cleanup in willClose(), all new method implementations |
| `plugins/ruinae/resources/editor.uidesc` | Add two `<view>` elements with `custom-view-name` in top bar |
| `plugins/ruinae/tests/unit/controller/preset_browser_test.cpp` | New test file for preset browser wiring |
| `plugins/ruinae/tests/CMakeLists.txt` | Register new test file |

## Implementation Order

> **CRITICAL (Constitution Principle XIII)**: Tests MUST be written and confirmed failing BEFORE any implementation code is written. Within each phase below, write the failing tests first, then implement to make them pass. "Testing and Validation" is not a separate final phase -- it is the first step of every phase.

### Phase 1: Core State Management (FR-003, FR-013)

1. Write failing tests for `editParamWithNotify`, `createComponentStateStream`, `loadComponentStateWithNotify`, and stateProvider/loadProvider callback wiring
2. Confirm tests fail (no implementation yet)
3. Add `editParamWithNotify` method
4. Add `createComponentStateStream` (delegates to host `getComponentState()`)
5. Add `loadComponentStateWithNotify` (reuses loadXxxParamsToController helpers)
6. Wire stateProvider and loadProvider callbacks in `initialize()`
7. Run tests and confirm they pass

### Phase 2: UI Integration (FR-001, FR-002, FR-012)

1. Write failing tests for overlay creation, openPresetBrowser, closePresetBrowser, openSavePresetDialog lifecycle
2. Confirm tests fail (no implementation yet)
3. Add `presetBrowserView_` and `savePresetDialogView_` fields to controller.h
4. Add overlay creation in `didOpen()`
5. Add cleanup in `willClose()`
6. Add `openPresetBrowser`, `closePresetBrowser`, `openSavePresetDialog` methods
7. Run tests and confirm they pass

### Phase 3: Button Views (FR-002)

1. Write failing tests for `createCustomView` handling of PresetBrowserButton and SavePresetButton
2. Confirm tests fail (no implementation yet)
3. Add `PresetBrowserButton` and `SavePresetButton` classes in anon namespace
4. Add `createCustomView` override
5. Add button views to editor.uidesc top bar
6. Run tests and confirm they pass

### Phase 4: Quality Gates

1. Build full plugin and verify zero errors and warnings
2. Run pluginval at strictness level 5
3. Run clang-tidy on all modified files

## Pattern Reference

The implementation follows the exact pattern from Disrumpo:

| Pattern | Disrumpo Location | Ruinae Target |
|---------|-------------------|---------------|
| PresetBrowserButton class | controller.cpp:1159-1182 | controller.cpp (anon namespace) |
| SavePresetButton class | controller.cpp:1187-1210 | controller.cpp (anon namespace) |
| stateProvider/loadProvider wiring | controller.cpp:1256-1267 | initialize() |
| didOpen overlay creation | controller.cpp:4134-4147 | didOpen() |
| willClose cleanup | controller.cpp:4282-4283 | willClose() |
| createCustomView handling | controller.cpp:3838-3856 | createCustomView() |
| openPresetBrowser | controller.cpp:4401-4407 | openPresetBrowser() |
| openSavePresetDialog | controller.cpp:4409-4413 | openSavePresetDialog() |
| closePresetBrowser | controller.cpp:4415-4419 | closePresetBrowser() |
| editParamWithNotify | controller.cpp:4696-4705 | editParamWithNotify() |

**Key difference from Disrumpo**: The `createComponentStateStream` and `loadComponentStateWithNotify` use a cleaner approach:
- `createComponentStateStream`: Delegates to host `getComponentState()` instead of re-serializing all params (Disrumpo's approach at lines 4425-4690 is 265 lines of manual serialization)
- `loadComponentStateWithNotify`: Reuses existing `loadXxxParamsToController` template helpers with `editParamWithNotify` as the callback, instead of manual parameter-by-parameter reading

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests

# Run tests
build/windows-x64-release/bin/Release/ruinae_tests.exe

# Pluginval (after building the plugin)
"$CMAKE" --build build/windows-x64-release --config Release
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```
