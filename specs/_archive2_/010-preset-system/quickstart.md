# Quickstart: 010-preset-system

**Branch**: `010-preset-system`
**Spec**: `specs/010-preset-system/spec.md`
**Plan**: `specs/010-preset-system/plan.md`

## Overview

This feature has three major workstreams executed in order:

1. **Phase A: Shared Library Refactoring** (P0) -- Extract ~2,000 lines of Iterum's preset infrastructure into `plugins/shared/` as a generic library
2. **Phase B: Serialization Verification** (P1-P2) -- Verify Disrumpo's existing serialization round-trip fidelity and version migration
3. **Phase C: Disrumpo Integration** (P3-P6) -- Wire Disrumpo's controller to the shared browser, create factory preset generator, and produce 120 presets

## Prerequisites

- All M1-M6 milestones complete for Disrumpo (band management, distortion, morph, sweep, modulation)
- Disrumpo processor `getState`/`setState` already implemented (v1-v6)
- Iterum preset system fully functional with 11 test files passing

## Build Commands

```bash
# Windows (Git Bash)
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build everything
"$CMAKE" --build build/windows-x64-release --config Release

# Run Iterum plugin tests (verify preset tests pass after refactor)
"$CMAKE" --build build/windows-x64-release --config Release --target iterum_tests
build/windows-x64-release/plugins/iterum/tests/Release/iterum_tests.exe

# Run Disrumpo plugin tests
"$CMAKE" --build build/windows-x64-release --config Release --target disrumpo_tests
build/windows-x64-release/plugins/disrumpo/tests/Release/disrumpo_tests.exe

# Run preset generator
build/windows-x64-release/bin/Release/disrumpo_preset_generator.exe [output_dir]

# Pluginval (after plugin code changes)
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Disrumpo.vst3"
```

## File Locations

### New Files (Created by This Spec)

```
plugins/shared/                         # NEW: Shared preset library
    CMakeLists.txt
    src/
        preset/
            preset_manager_config.h     # Configuration struct
            preset_info.h               # Generalized from Iterum
            preset_manager.h            # Generalized from Iterum
            preset_manager.cpp
        platform/
            preset_paths.h              # Parameterized from Iterum
            preset_paths.cpp
        ui/
            search_debouncer.h          # Moved from Iterum (namespace change only)
            preset_browser_logic.h      # Moved from Iterum (namespace change only)
            category_tab_bar.h          # Renamed/generalized from ModeTabBar
            category_tab_bar.cpp
            preset_data_source.h        # Generalized from Iterum
            preset_data_source.cpp
            preset_browser_view.h       # Generalized from Iterum
            preset_browser_view.cpp
            save_preset_dialog_view.h   # Moved from Iterum
            save_preset_dialog_view.cpp

plugins/iterum/src/preset/
    iterum_preset_config.h              # NEW: Config + DelayMode adapter

plugins/disrumpo/src/preset/
    disrumpo_preset_config.h            # NEW: Config for Disrumpo

tools/
    disrumpo_preset_generator.cpp       # NEW: Factory preset generator
```

### Modified Files

```
CMakeLists.txt                          # Add plugins/shared/, disrumpo_preset_generator
plugins/iterum/CMakeLists.txt           # Remove moved files, link KratePluginsShared
plugins/disrumpo/CMakeLists.txt         # Link KratePluginsShared
plugins/iterum/src/controller/controller.cpp  # Update includes/namespace
plugins/iterum/tests/unit/preset/*.cpp        # Update includes/namespace (11 files)
plugins/iterum/tests/unit/ui/*.cpp            # Update includes/namespace (6 files)
```

### Existing Files (Reference Only)

```
plugins/disrumpo/src/processor/processor.cpp  # getState/setState already implemented
plugins/disrumpo/src/controller/controller.cpp # setComponentState already implemented
plugins/disrumpo/src/plugin_ids.h              # kProcessorUID, kPresetVersion = 6
tools/preset_generator.cpp                     # Iterum pattern reference
```

## Implementation Order

### Phase A: Shared Library (Tasks 1-8)

1. Create `plugins/shared/` directory structure and CMakeLists.txt
2. Move `SearchDebouncer` (header-only, namespace change)
3. Move `PresetBrowserLogic` (header-only, namespace change)
4. Move/parameterize `preset_paths` (add `pluginName` parameter)
5. Create `PresetManagerConfig` struct
6. Move/generalize `PresetInfo` (replace `DelayMode mode` with `std::string subcategory`)
7. Move/generalize `PresetManager` (use config, string subcategory)
8. Move/generalize `CategoryTabBar` (accept labels parameter)
9. Move/generalize `PresetDataSource` (string subcategory filter)
10. Move/generalize `PresetBrowserView` (config-driven, tab labels)
11. Move/generalize `SavePresetDialogView` (namespace change, subcategory list)
12. Create Iterum adapter (`iterum_preset_config.h`)
13. Update Iterum CMakeLists.txt (remove moved files, link shared)
14. Update all 11 Iterum test files (includes/namespaces)
15. Build and verify all Iterum preset tests pass (SC-009)

### Phase B: Serialization Verification (Tasks 9-12)

16. Write round-trip test for v6 (all ~450 params)
17. Write version migration tests (v1 through v6)
18. Write edge case tests (empty stream, truncated, future version)
19. Run pluginval at strictness level 5

### Phase C: Disrumpo Integration (Tasks 13-18)

20. Create `disrumpo_preset_config.h`
21. Wire PresetManager into Disrumpo controller
22. Create StateProvider/LoadProvider callbacks
23. Add PresetBrowserView to Disrumpo editor
24. Create `disrumpo_preset_generator.cpp` with BinaryWriter
25. Implement 5 Init presets in generator
26. Implement remaining 115 factory presets (sound design task)
27. Update Disrumpo CMakeLists.txt (link shared, add generator)
28. Integration tests: browser opens, presets load
29. Final pluginval validation (SC-008)

## Critical Considerations

1. **ODR Safety**: The shared library uses `Krate::Plugins` namespace. No classes should remain in `Iterum` namespace after move -- Iterum uses the shared classes directly or through thin wrappers.

2. **Backward Compatibility**: Iterum's existing preset directory structure (`Documents/Krate Audio/Iterum/`) must not change. Users with existing presets must still find them.

3. **Test Discipline**: After EVERY file move, build and run tests. Do NOT batch multiple moves without verification.

4. **Include Paths**: Shared library uses `#include "preset/preset_info.h"` (relative to `plugins/shared/src/`). Plugin code uses `#include <shared/preset/preset_info.h>` or relies on `target_include_directories`.
