# Implementation Plan: Disrumpo Preset System

**Branch**: `010-preset-system` | **Date**: 2026-01-30 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/010-preset-system/spec.md`

## Summary

Extract Iterum's ~2,000-line preset infrastructure into a shared library (`plugins/shared/`, namespace `Krate::Plugins`) that both Iterum and Disrumpo use. Verify Disrumpo's existing versioned serialization (v1-v6) for round-trip fidelity and version migration. Build Disrumpo's preset browser using the shared components, create a factory preset generator tool, and produce 120 factory presets across 11 categories. The shared library uses a `PresetManagerConfig` struct for plugin-specific customization, replacing hardcoded values with parameterized configuration.

## Technical Context

**Language/Version**: C++20 (MSVC 2022, Clang 12+, GCC 10+)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, std::filesystem
**Storage**: .vstpreset binary files on filesystem (VST3 standard format)
**Testing**: Catch2 3.x (Constitution Principle XIII: Test-First Development)
**Target Platform**: Windows 10/11, macOS 11+, Linux (cross-platform required per Constitution VI)
**Project Type**: VST3 plugin monorepo with shared library extraction
**Performance Goals**: Preset load < 50ms (SC-002), search results < 100ms (SC-007)
**Constraints**: UI thread only for all preset operations (FR-033a), zero audio thread involvement
**Scale/Scope**: ~450 Disrumpo parameters, 120 factory presets, 8 shared components, 11 existing test files to migrate

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check (PASSED)**:

**Required Check - Principle I (VST3 Architecture Separation):**
- [x] Preset operations are controller-side only (UI thread)
- [x] Processor getState/setState is separate from PresetManager operations
- [x] No cross-include between processor and controller preset code

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] No preset operations on audio thread (FR-033a)
- [x] StateProvider/LoadProvider callbacks execute on UI thread only
- [x] PresetBrowserView is a UI component with no audio thread access

**Required Check - Principle VI (Cross-Platform Compatibility):**
- [x] Platform paths use std::filesystem with preprocessor detection
- [x] UI uses VSTGUI abstractions only (no native dialogs)
- [x] Factory preset paths follow per-platform conventions

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XVI (Honest Completion):**
- [x] All 44 FR-xxx and 11 SC-xxx requirements documented in spec
- [x] Compliance table in spec ready for completion verification

**Post-Design Re-Check (PASSED)**:
- [x] Shared library namespace `Krate::Plugins` is unique in codebase
- [x] `PresetManagerConfig` is a new type not found anywhere in codebase
- [x] `CategoryTabBar` is a new name (renamed from `ModeTabBar`)
- [x] No constitution violations in the design -- all changes use VSTGUI, std::filesystem, UI thread

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Pattern | Existing? | Action |
|--------------|----------------|-----------|--------|
| PresetManagerConfig | `class PresetManagerConfig` / `struct PresetManagerConfig` | No | Create New |
| CategoryTabBar | `class CategoryTabBar` | No | Create New (rename from ModeTabBar) |

**Note**: All other shared library types (PresetInfo, PresetManager, PresetDataSource, PresetBrowserView, SearchDebouncer, PresetBrowserLogic, SavePresetDialogView) are MOVES from `Iterum` namespace to `Krate::Plugins` namespace, not new creations. The original Iterum versions will be removed after the move.

**Utility Functions to be created**:

| Planned Function | Search Pattern | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| makeIterumPresetConfig | `makeIterumPresetConfig` | No | iterum_preset_config.h | Create New |
| makeDisrumpoPresetConfig | `makeDisrumpoPresetConfig` | No | disrumpo_preset_config.h | Create New |
| delayModeToSubcategory | `delayModeToSubcategory` | No | iterum_preset_config.h | Create New |
| subcategoryToDelayMode | `subcategoryToDelayMode` | No | iterum_preset_config.h | Create New |

### Existing Components to Reuse

| Component | Location | How It Will Be Used |
|-----------|----------|---------------------|
| SearchDebouncer | plugins/iterum/src/ui/search_debouncer.h | MOVE to plugins/shared/ (namespace change only) |
| PresetBrowserLogic | plugins/iterum/src/ui/preset_browser_logic.h | MOVE to plugins/shared/ (namespace change only) |
| preset_paths | plugins/iterum/src/platform/preset_paths.h/.cpp | MOVE to plugins/shared/ (parameterize pluginName) |
| PresetInfo | plugins/iterum/src/preset/preset_info.h | MOVE to plugins/shared/ (replace DelayMode with string) |
| PresetManager | plugins/iterum/src/preset/preset_manager.h/.cpp | MOVE to plugins/shared/ (add config parameter) |
| ModeTabBar | plugins/iterum/src/ui/mode_tab_bar.h/.cpp | MOVE as CategoryTabBar (parameterize labels) |
| PresetDataSource | plugins/iterum/src/ui/preset_data_source.h/.cpp | MOVE to plugins/shared/ (string subcategory) |
| PresetBrowserView | plugins/iterum/src/ui/preset_browser_view.h/.cpp | MOVE to plugins/shared/ (config-driven) |
| SavePresetDialogView | plugins/iterum/src/ui/save_preset_dialog_view.h/.cpp | MOVE to plugins/shared/ (namespace change) |
| Disrumpo getState/setState | plugins/disrumpo/src/processor/processor.cpp | ALREADY IMPLEMENTED (v1-v6 serialization) |
| Disrumpo setComponentState | plugins/disrumpo/src/controller/controller.cpp | ALREADY IMPLEMENTED |
| BinaryWriter pattern | tools/preset_generator.cpp | REUSE pattern for disrumpo_preset_generator.cpp |

### Files Checked for Conflicts

- [x] `plugins/iterum/src/preset/` - PresetInfo, PresetManager (will be moved)
- [x] `plugins/iterum/src/ui/` - All browser components (will be moved)
- [x] `plugins/iterum/src/platform/` - preset_paths (will be moved)
- [x] `plugins/disrumpo/src/` - No preset/ directory exists yet
- [x] `plugins/shared/` - Directory does not exist yet (will be created)
- [x] Global namespace search for `Krate::Plugins` - not found anywhere

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: This is primarily a MOVE-and-generalize operation. The original Iterum files are removed from Iterum's CMakeLists.txt and replaced with the shared library link. The shared library uses a new namespace (`Krate::Plugins`) that does not exist anywhere in the codebase. The only new types (`PresetManagerConfig`, `CategoryTabBar`) are confirmed unique via grep. The risk is managing the transition correctly (include paths, namespace references), not ODR collisions.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| PresetInfo | isValid() | `[[nodiscard]] bool isValid() const` | Yes |
| PresetInfo | operator< | `bool operator<(const PresetInfo& other) const` | Yes |
| PresetManager | scanPresets() | `PresetList scanPresets()` | Yes |
| PresetManager | loadPreset() | `bool loadPreset(const PresetInfo& preset)` | Yes |
| PresetManager | savePreset() | `bool savePreset(const std::string& name, const std::string& category, DelayMode mode, const std::string& description = "")` | Yes (will change signature) |
| PresetManager | setStateProvider() | `void setStateProvider(StateProvider provider)` | Yes |
| PresetManager | setLoadProvider() | `void setLoadProvider(LoadProvider provider)` | Yes |
| ModeTabBar | setSelectedTab() | `void setSelectedTab(int tab)` | Yes |
| ModeTabBar | setSelectionCallback() | `void setSelectionCallback(SelectionCallback cb)` | Yes |
| PresetDataSource | setModeFilter() | `void setModeFilter(int mode)` | Yes (will change to string) |
| PresetDataSource | setSearchFilter() | `void setSearchFilter(const std::string& query)` | Yes |
| PresetFile::loadPreset | static | `static bool loadPreset(IBStream*, const FUID&, IComponent*, IEditController*)` | Yes |
| PresetFile::savePreset | static (stream) | `static bool savePreset(IBStream*, const FUID&, IBStream*, IBStream*, const char*, int32)` | Yes |
| Platform::getUserPresetDirectory | - | `std::filesystem::path getUserPresetDirectory()` | Yes (will add pluginName param) |
| Platform::getFactoryPresetDirectory | - | `std::filesystem::path getFactoryPresetDirectory()` | Yes (will add pluginName param) |
| Platform::ensureDirectoryExists | - | `bool ensureDirectoryExists(const std::filesystem::path& path)` | Yes |

### Header Files Read

- [x] `plugins/iterum/src/preset/preset_info.h` - PresetInfo struct
- [x] `plugins/iterum/src/preset/preset_manager.h` - PresetManager class
- [x] `plugins/iterum/src/preset/preset_manager.cpp` - PresetManager implementation (549 lines)
- [x] `plugins/iterum/src/platform/preset_paths.h` - Platform path declarations
- [x] `plugins/iterum/src/platform/preset_paths.cpp` - Platform path implementation (63 lines)
- [x] `plugins/iterum/src/ui/mode_tab_bar.h` - ModeTabBar declaration
- [x] `plugins/iterum/src/ui/mode_tab_bar.cpp` - ModeTabBar implementation (99 lines)
- [x] `plugins/iterum/src/ui/preset_data_source.h` - PresetDataSource declaration
- [x] `plugins/iterum/src/ui/preset_browser_view.h` - PresetBrowserView declaration
- [x] `plugins/iterum/src/ui/search_debouncer.h` - SearchDebouncer (header-only, 114 lines)
- [x] `plugins/iterum/src/ui/preset_browser_logic.h` - PresetBrowserLogic (header-only, 155 lines)
- [x] `plugins/iterum/src/ui/save_preset_dialog_view.h` - SavePresetDialogView declaration
- [x] `plugins/iterum/src/delay_mode.h` - DelayMode enum
- [x] `plugins/iterum/src/plugin_ids.h` - kProcessorUID
- [x] `plugins/disrumpo/src/plugin_ids.h` - kProcessorUID, kPresetVersion = 6
- [x] `plugins/disrumpo/src/processor/processor.cpp` - getState/setState (lines 441-739+)
- [x] `tools/preset_generator.cpp` - BinaryWriter pattern

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| PresetManager | Uses `Iterum::kProcessorUID` directly in preset save/load | Must replace with `config_.processorUID` |
| PresetManager | Hardcodes `"Iterum"` in XML metadata | Must use `config_.pluginName` |
| PresetManager | Uses `modeNames[]` array for subdirectory creation | Must use `config_.subcategoryNames` |
| preset_paths | Hardcodes `"Iterum"` in all 6 path returns | Must accept `pluginName` parameter |
| ModeTabBar | `static constexpr int kNumTabs = 12` | Must use `labels_.size()` |
| ModeTabBar | `getTabLabels()` returns hardcoded static vector | Must use constructor-injected labels |
| PresetDataSource | `modeFilter_` is `int` (-1 = All, 0-10 = modes) | Must change to `std::string` (empty = All) |
| PresetBrowserView | `open(int currentMode)` takes mode index | Must change to `open(const std::string& subcategory)` |

## Layer 0 Candidate Analysis

Not applicable. This feature operates at the plugin/UI layer, not the DSP layer. No audio-processing utilities are being created.

### Utilities to Extract to Layer 0

None -- this feature is entirely UI/controller-side.

### Utilities to Keep as Member Functions

N/A

**Decision**: No Layer 0 extraction needed. All code is plugin-layer (preset management and UI).

## Higher-Layer Reusability Analysis

**This feature's layer**: Plugin Infrastructure (shared across plugins)

**Related features at same layer** (from roadmap):
- Future Krate Audio plugins that need preset management
- Iterum already benefits from this refactoring

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| KratePluginsShared library | HIGH | All future Krate Audio plugins | Extract now (this is the primary purpose) |
| PresetManagerConfig pattern | HIGH | Any plugin needing presets | Extract now |
| BinaryWriter (preset generator) | MEDIUM | Any plugin needing factory presets | Keep in tools/, copy pattern |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Extract 8 components to shared library NOW | Two concrete consumers (Iterum, Disrumpo) already identified. Constitution Principle XIV mandates avoiding duplication. |
| Use STATIC library, not OBJECT or INTERFACE | Components have .cpp files; STATIC handles transitive deps cleanly |
| String-based subcategory over generic enum | Each plugin has different categories; strings are the lowest common denominator |
| Parameterized config struct over inheritance | Composition over inheritance; simpler, no virtual dispatch needed |

### Review Trigger

After implementing any future Krate Audio plugin:
- [x] Does the new plugin need preset management? -> Use KratePluginsShared with a new config
- [ ] Does the shared browser need new features? -> Extend shared components, not plugin-specific code
- [ ] Any duplicated preset code? -> Should not happen if shared library is used

## Project Structure

### Documentation (this feature)

```text
specs/010-preset-system/
    plan.md              # This file
    research.md          # Phase 0 output
    data-model.md        # Phase 1 output
    quickstart.md        # Phase 1 output
    contracts/           # Phase 1 output
        shared-library-api.md
        disrumpo-preset-api.md
```

### Source Code (repository root)

```text
plugins/
    shared/                             # NEW: Shared preset library
        CMakeLists.txt
        src/
            preset/
                preset_manager_config.h # NEW: Configuration struct
                preset_info.h           # MOVED from iterum (generalized)
                preset_manager.h        # MOVED from iterum (generalized)
                preset_manager.cpp
            platform/
                preset_paths.h          # MOVED from iterum (parameterized)
                preset_paths.cpp
            ui/
                search_debouncer.h      # MOVED from iterum (namespace change)
                preset_browser_logic.h  # MOVED from iterum (namespace change)
                category_tab_bar.h      # RENAMED from ModeTabBar (generalized)
                category_tab_bar.cpp
                preset_data_source.h    # MOVED from iterum (generalized)
                preset_data_source.cpp
                preset_browser_view.h   # MOVED from iterum (generalized)
                preset_browser_view.cpp
                save_preset_dialog_view.h   # MOVED from iterum
                save_preset_dialog_view.cpp

    iterum/
        src/
            preset/
                iterum_preset_config.h  # NEW: Config + DelayMode adapter

    disrumpo/
        src/
            preset/
                disrumpo_preset_config.h  # NEW: Config for Disrumpo

tools/
    disrumpo_preset_generator.cpp       # NEW: Factory preset generator
```

**Structure Decision**: Plugin infrastructure pattern with shared library at `plugins/shared/`. Both `plugins/iterum/` and `plugins/disrumpo/` link against `KratePluginsShared`. No changes to the DSP layer (`dsp/`). The shared library is NOT a DSP component -- it lives at the plugin infrastructure layer.

## Complexity Tracking

No constitution violations. The shared library adds a third CMake target in the `plugins/` directory, but this is explicitly required by the spec (FR-034) and mandated by Constitution Principle XIV (avoid duplication). The spec documents 8 components being extracted, which is a moderate refactoring scope but each component has clear boundaries and existing tests to validate correctness.
