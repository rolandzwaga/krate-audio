# Research: 010-preset-system

**Date**: 2026-01-30
**Spec**: `specs/010-preset-system/spec.md`

## 1. Shared Library Architecture (FR-034)

### Decision: Static CMake library at `plugins/shared/`

**Rationale**: The spec explicitly requires `plugins/shared/` with namespace `Krate::Plugins`. A static library (OBJECT or STATIC) linked by both plugin targets avoids symbol export complexity while sharing compiled code. Since both plugins already link `sdk`, `vstgui_support`, and `KrateDSP`, the shared library only needs to declare dependencies on these same targets.

**Alternatives Considered**:
- **INTERFACE library (header-only)**: Rejected because several components (preset_paths.cpp, preset_manager.cpp, mode_tab_bar.cpp, preset_data_source.cpp, preset_browser_view.cpp, save_preset_dialog_view.cpp) have `.cpp` files with substantial implementation.
- **Shared dynamic library (.dll/.dylib)**: Rejected because it adds export/import complexity, ABI stability requirements, and deployment burden. Both consumers are built in the same CMake project.
- **OBJECT library**: Suitable, but STATIC is more conventional and handles transitive dependencies better with `target_link_libraries`.

**Implementation**:
```cmake
# plugins/shared/CMakeLists.txt
add_library(KratePluginsShared STATIC
    src/ui/search_debouncer.h         # Header-only
    src/ui/preset_browser_logic.h     # Header-only
    src/platform/preset_paths.h
    src/platform/preset_paths.cpp
    src/preset/preset_info.h
    src/preset/preset_manager_config.h
    src/preset/preset_manager.h
    src/preset/preset_manager.cpp
    src/ui/category_tab_bar.h
    src/ui/category_tab_bar.cpp
    src/ui/preset_data_source.h
    src/ui/preset_data_source.cpp
    src/ui/preset_browser_view.h
    src/ui/preset_browser_view.cpp
    src/ui/save_preset_dialog_view.h
    src/ui/save_preset_dialog_view.cpp
)

target_include_directories(KratePluginsShared
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(KratePluginsShared
    PUBLIC sdk vstgui_support
)
```

Both plugins then add: `target_link_libraries(${PLUGIN_NAME} PRIVATE KratePluginsShared)`

---

## 2. Namespace Migration Strategy (FR-034, FR-043)

### Decision: New namespace `Krate::Plugins` for shared code

**Rationale**: The existing code uses `Iterum` namespace. The shared library needs a plugin-agnostic namespace. `Krate::Plugins` follows the monorepo convention where `Krate::DSP` is the DSP library namespace.

**Migration Pattern**:
1. Move files from `plugins/iterum/src/` to `plugins/shared/src/`
2. Change `namespace Iterum` to `namespace Krate::Plugins`
3. In Iterum, update all `#include` paths and add `using namespace` or explicit `Krate::Plugins::` qualifiers
4. Update Iterum test files: change includes and namespace references
5. Remove duplicate source files from Iterum's CMakeLists.txt

**Alternatives Considered**:
- Keep `Iterum` namespace with aliases: Rejected because it creates confusing ownership semantics.
- Use `Krate::Shared`: Rejected because `Plugins` is more specific about what is being shared.

---

## 3. PresetInfo Generalization (FR-038)

### Decision: Replace `DelayMode mode` with `std::string subcategory`

**Rationale**: The current `PresetInfo` has `DelayMode mode` which is an Iterum-specific enum. For generic use, a string-based subcategory derived from the parent directory name is more flexible.

**Migration Impact**:
- `PresetInfo.mode` (type `DelayMode`) becomes `PresetInfo.subcategory` (type `std::string`)
- `PresetManager::getPresetsForMode(DelayMode)` becomes `getPresetsForSubcategory(const std::string&)`
- `PresetManager::savePreset(name, category, DelayMode, description)` becomes `savePreset(name, subcategory, description)`
- `PresetDataSource::setModeFilter(int)` stays as `setSubcategoryFilter(const std::string&)` -- empty string means "All"
- Iterum creates a thin wrapper or mapping function: `DelayMode -> std::string` and `std::string -> DelayMode`

**Iterum Adapter Pattern**:
```cpp
// In Iterum's controller, provide the mode-to-string mapping:
static const std::vector<std::string> kIterumSubcategories = {
    "Granular", "Spectral", "Shimmer", "Tape", "BBD",
    "Digital", "PingPong", "Reverse", "MultiTap", "Freeze", "Ducking"
};
```

---

## 4. PresetManagerConfig Design (FR-039, Key Entity)

### Decision: Configuration struct injected at construction

**Rationale**: The PresetManager currently hardcodes the processor UID (`Iterum::kProcessorUID`) and plugin name. A configuration struct allows each plugin to provide its own values.

**Design**:
```cpp
struct PresetManagerConfig {
    Steinberg::FUID processorUID;        // For .vstpreset file headers
    std::string pluginName;              // For directory paths ("Iterum" or "Disrumpo")
    std::string pluginCategoryDesc;      // For metadata ("Delay" or "Distortion")
    std::vector<std::string> subcategoryNames; // Directory names for scanning
};
```

**Key Observations from Existing Code**:
- `preset_manager.cpp` line 218 uses `Iterum::kProcessorUID` directly -- must be replaced with `config.processorUID`
- `preset_manager.cpp` lines 262-271 uses `modeNames[]` array -- must be replaced with `config.subcategoryNames`
- `preset_manager.cpp` lines 282-283 hardcodes `"Iterum"` and `"Delay"` in XML metadata -- must use `config.pluginName` and `config.pluginCategoryDesc`
- `preset_paths.cpp` hardcodes `"Iterum"` in all path functions -- must accept `pluginName` parameter

---

## 5. CategoryTabBar Generalization (FR-040)

### Decision: Accept tab labels as constructor parameter, remove compile-time constant

**Rationale**: The existing `ModeTabBar` has `static constexpr int kNumTabs = 12` and `getTabLabels()` returns a hardcoded static vector. For generic use, labels must be passed in.

**Key Changes**:
- Constructor: `CategoryTabBar(const CRect& size, std::vector<std::string> labels)`
- Remove `kNumTabs` constant; use `labels_.size()` instead
- Remove `getTabLabels()` static method; use instance member `labels_`
- Keep all drawing, selection, and callback logic identical

**Verified from source** (`mode_tab_bar.cpp`):
- `getTabRect()` uses `kNumTabs` -- change to `labels_.size()`
- `draw()` iterates `kNumTabs` -- change to `labels_.size()`
- `onMouseDown()` iterates `kNumTabs` -- change to `labels_.size()`
- `setSelectedTab()` validates `tab < kNumTabs` -- change to `tab < static_cast<int>(labels_.size())`

---

## 6. Disrumpo Serialization Status (FR-001 through FR-008)

### Decision: Serialization is ALREADY IMPLEMENTED in Disrumpo processor

**Rationale**: Reading `plugins/disrumpo/src/processor/processor.cpp` lines 441-712, the complete versioned serialization (v1-v6) is already implemented:
- v1: Global params (inputGain, outputGain, globalMix)
- v2: Band management (bandCount, 8x bandState with gain/pan/solo/bypass/mute, 7x crossover)
- v3: All ~450 VSTGUI parameters (implied by version history)
- v4: Sweep system (core, LFO, envelope, custom curve breakpoints)
- v5: Modulation system (all sources, macros, 32 routing slots)
- v6: Morph node state (per-band morphX/Y, mode, activeNodeCount, per-node type/drive/mix/tone/bias/folds/bitDepth)

**This spec does NOT need to re-implement serialization**. The work is:
1. Verify round-trip fidelity (FR-013, FR-014, FR-015, SC-001)
2. Test version migration (FR-009 through FR-012, SC-003)
3. Stress-test with corrupted/truncated streams (edge cases)

---

## 7. Disrumpo Controller setComponentState (FR-001)

### Decision: Already implemented at controller.cpp line 2135

**Rationale**: The controller mirrors the processor's state reading to sync UI parameters on preset load. This is the standard VST3 pattern and is already in place.

---

## 8. Factory Preset Generator Pattern (FR-028)

### Decision: Follow Iterum pattern with `tools/disrumpo_preset_generator.cpp`

**Rationale**: The existing `tools/preset_generator.cpp` (for Iterum) demonstrates the pattern:
- `BinaryWriter` class mimics IBStreamer (writes int32, float as raw bytes)
- Hardcoded C++ structs define each preset's parameter values
- Generated .vstpreset files are placed in category subdirectories
- The generator is a standalone CMake executable

**Disrumpo Generator Differences**:
- Uses Disrumpo's kProcessorUID instead of Iterum's
- Serialization format follows Disrumpo's v6 format (global + bands + sweep + modulation + morph)
- 120 presets across 11 categories (vs Iterum's mode-based organization)
- Preset parameter structs must match the exact binary layout of `Processor::getState()`

**Note**: The 120 factory presets (FR-027, User Story 6) are a sound design deliverable requiring ~60+ hours. The plan covers the generator tool and Init presets (5 presets, trivial parameter values). The remaining 115 presets are marked as a separate sound design task.

---

## 9. Preset Browser UI Integration (FR-016 through FR-019b)

### Decision: Disrumpo uses shared PresetBrowserView with configuration

**Rationale**: The shared PresetBrowserView (~1,100 lines) handles all UI concerns:
- Modal overlay layout
- CategoryTabBar integration (receives Disrumpo's 12 labels: "All" + 11 categories)
- Save/delete/overwrite dialogs
- Search polling with debouncer
- Keyboard hook management

Disrumpo's controller creates the browser by:
1. Creating a `PresetManagerConfig` with Disrumpo-specific values
2. Creating a `PresetManager` with that config
3. Setting up StateProvider/LoadProvider callbacks
4. Passing the PresetManager to `PresetBrowserView`
5. Opening the browser from the main editor view

**No Disrumpo-specific browser code is needed**. All customization happens through the configuration.

---

## 10. Threading Model (FR-033a)

### Decision: All preset operations on UI thread only (already established)

**Rationale**: Both the spec and the existing Iterum implementation confirm:
- `PresetManager` methods are UI-thread-only
- `StateProvider` callback creates a `MemoryStream`, calls `processor->getState()` on it
- `LoadProvider` callback applies state via controller's `performEdit` mechanism
- The browser view runs on the UI thread (VSTGUI requirement)
- Async scan at startup uses a timer callback on the UI thread (not a background thread)

---

## 11. Save Dialog Category Selection (FR-021)

### Decision: The shared save dialog uses the same category list from PresetManagerConfig

**Rationale**: The current Iterum `PresetBrowserView::showSaveDialog()` creates a save dialog with a name field but uses the current mode for category. For generic use, the save dialog needs a category dropdown populated from `config.subcategoryNames`. The `SavePresetDialogView` already exists as a separate component in Iterum. In the shared version, it will accept the subcategory list.

---

## 12. Platform Paths Convention (FR-032, FR-033)

### Decision: Parameterize with plugin name, keep "Krate Audio" as vendor prefix

**Rationale**: Current paths from `preset_paths.cpp`:
```
Windows User:    %USERPROFILE%\Documents\Krate Audio\{pluginName}
macOS User:      ~/Documents/Krate Audio/{pluginName}
Linux User:      ~/Documents/Krate Audio/{pluginName}
Windows Factory: %PROGRAMDATA%\Krate Audio\{pluginName}
macOS Factory:   /Library/Application Support/Krate Audio/{pluginName}
Linux Factory:   /usr/share/krate-audio/{pluginName}
```

**Note**: The spec's FR-032 specifies VST3 standard paths which differ from the current Iterum implementation. The current paths use `Documents/Krate Audio/...` rather than the VST3 standard `VST3 Presets/...`. This is an existing design choice that should be preserved for backward compatibility with Iterum's existing presets. Disrumpo will use the same convention.

---

## 13. Debounce/Coalesce for Rapid Preset Loading (FR-019a)

### Decision: Implement in shared PresetManager using a "pending load" flag

**Rationale**: When rapid arrow-key or MIDI program changes trigger multiple preset loads within 50ms, the system should cancel in-progress loads and apply only the most recent. This can be implemented with:
- A `pendingPresetIndex_` atomic or guarded variable
- Each `loadPreset()` call sets the pending index and invalidates any in-progress load
- The actual load executes on the next timer tick (using existing search poll timer infrastructure)

**Alternative**: Since preset loading is synchronous on the UI thread and completes within 50ms (SC-002), the "cancel in-progress" pattern may simply mean: check if a new load was requested after the current one started, and if so, load the new one immediately after. In practice, VST3 state loading via IBStream is fast enough that coalescing is primarily about preventing redundant loads, not canceling partial ones.

---

## Summary of NEEDS CLARIFICATION Items

All clarifications from the spec's Clarifications section are resolved. No remaining unknowns. The research confirms:

1. **Serialization**: Already implemented (v1-v6) in Disrumpo processor
2. **Shared library**: STATIC CMake library with `Krate::Plugins` namespace
3. **Configuration**: `PresetManagerConfig` struct injected at construction
4. **UI**: Shared `PresetBrowserView` with parameterized labels and manager
5. **Factory presets**: Generator tool following Iterum pattern
6. **Platform paths**: Parameterized with plugin name
7. **Threading**: UI thread only for all preset operations
