# Tasks: Disrumpo Preset System

**Input**: Design documents from `specs/010-preset-system/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/path/to/your_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

**When is this check needed?** Only when test files use IEEE 754 functions (`std::isnan`, `std::isfinite`, `std::isinf`, NaN/infinity detection). Phases that only do namespace/include changes (like Phase 3) typically don't need this, but should be checked as a precaution. The check is listed after each user story as a reminder.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US0, US1, US2)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create the shared preset library structure and CMake configuration

- [X] T001 Create plugins/shared/ directory structure (src/preset/, src/platform/, src/ui/)
- [X] T002 Create plugins/shared/CMakeLists.txt with KratePluginsShared STATIC library target
- [X] T003 Add KratePluginsShared to root CMakeLists.txt and update plugin links

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core shared library components that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Pure C++ Components (No VSTGUI dependencies)

- [X] T004 [P] Move SearchDebouncer from plugins/iterum/src/ui/search_debouncer.h to plugins/shared/src/ui/search_debouncer.h (namespace change to Krate::Plugins)
- [X] T005 [P] Move PresetBrowserLogic from plugins/iterum/src/ui/preset_browser_logic.h to plugins/shared/src/ui/preset_browser_logic.h (namespace change to Krate::Plugins)

### 2.2 Platform Paths (Parameterized)

- [X] T006 Move preset_paths from plugins/iterum/src/platform/ to plugins/shared/src/platform/ with parameterized pluginName argument
- [X] T007 Create unit tests for Platform::getUserPresetDirectory and getFactoryPresetDirectory in plugins/shared/tests/unit/platform/preset_paths_test.cpp
- [X] T008 Verify preset_paths_test passes for both "Iterum" and "Disrumpo" plugin names
- [X] T009 Build shared library and verify compilation

### 2.3 PresetManagerConfig

- [X] T010 Create plugins/shared/src/preset/preset_manager_config.h with PresetManagerConfig struct (processorUID, pluginName, pluginCategoryDesc, subcategoryNames)

### 2.4 PresetInfo (Generalized)

- [X] T011 Move PresetInfo from plugins/iterum/src/preset/preset_info.h to plugins/shared/src/preset/preset_info.h (replace DelayMode mode with std::string subcategory)
- [X] T012 Create unit tests for PresetInfo in plugins/shared/tests/unit/preset/preset_info_test.cpp
- [X] T013 Verify preset_info_test passes with string subcategory
- [X] T014 Build and verify compilation

### 2.5 PresetManager (Generalized)

- [X] T015 Move PresetManager from plugins/iterum/src/preset/ to plugins/shared/src/preset/ with PresetManagerConfig parameter
- [X] T016 Update PresetManager constructor to accept PresetManagerConfig and use config.processorUID, config.pluginName, config.subcategoryNames
- [X] T017 Update PresetManager::savePreset signature to accept string subcategory instead of DelayMode
- [X] T018 Update PresetManager::getPresetsForMode to getPresetsForSubcategory with string parameter (empty string = return all presets, i.e. "All" filter; non-empty = filter to matching subcategory)
- [X] T019 Create unit tests for PresetManager in plugins/shared/tests/unit/preset/preset_manager_test.cpp
- [X] T020 Verify preset_manager_test passes with both Iterum and Disrumpo configs
- [X] T021 Build and verify compilation

### 2.6 CategoryTabBar (Generalized UI Component)

- [X] T022 Move ModeTabBar from plugins/iterum/src/ui/mode_tab_bar.* to plugins/shared/src/ui/category_tab_bar.* (rename class, accept labels parameter)
- [X] T023 Update CategoryTabBar constructor to accept std::vector<std::string> labels and replace kNumTabs constant with labels_.size()
- [X] T024 Create unit tests for CategoryTabBar in plugins/shared/tests/unit/ui/category_tab_bar_test.cpp
- [X] T025 Verify category_tab_bar_test passes with different label counts
- [X] T026 Build and verify compilation

### 2.7 PresetDataSource (Generalized UI Component)

- [X] T027 Move PresetDataSource from plugins/iterum/src/ui/preset_data_source.* to plugins/shared/src/ui/preset_data_source.* (use string subcategory filter)
- [X] T028 Update PresetDataSource::setModeFilter to setSubcategoryFilter with string parameter (empty = "All")
- [X] T029 Create unit tests for PresetDataSource in plugins/shared/tests/unit/ui/preset_data_source_test.cpp
- [X] T030 Verify preset_data_source_test passes
- [X] T031 Build and verify compilation

### 2.8 SavePresetDialogView (Move to Shared)

- [X] T032 Move SavePresetDialogView from plugins/iterum/src/ui/save_preset_dialog_view.* to plugins/shared/src/ui/save_preset_dialog_view.* (namespace change only)
- [X] T033 Build and verify compilation

### 2.9 PresetBrowserView (Generalized UI Component)

- [X] T034 Move PresetBrowserView from plugins/iterum/src/ui/preset_browser_view.* to plugins/shared/src/ui/preset_browser_view.* (config-driven)
- [X] T035 Update PresetBrowserView constructor to accept PresetManager* and std::vector<std::string> tabLabels
- [X] T036 Update PresetBrowserView::open signature to accept string subcategory instead of int mode
- [X] T037 Replace ModeTabBar with CategoryTabBar in PresetBrowserView
- [X] T038 Build and verify compilation

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 0 - Shared Preset Infrastructure (Priority: P0)

**Goal**: Extract Iterum's preset infrastructure into a shared library that both Iterum and Disrumpo can use, with all existing Iterum tests continuing to pass.

**Independent Test**: All 11 existing Iterum preset tests pass after refactoring with only include path and namespace changes.

### 3.1 Iterum Adapter Layer

- [X] T039 [US0] Create plugins/iterum/src/preset/iterum_preset_config.h with makeIterumPresetConfig(), delayModeToSubcategory(), and subcategoryToDelayMode()
- [X] T040 [US0] Update plugins/iterum/CMakeLists.txt to remove moved source files and link KratePluginsShared

### 3.2 Update Iterum Controller Integration

- [X] T041 [US0] Update plugins/iterum/src/controller/controller.cpp to use shared PresetManager with Iterum config
- [X] T042 [US0] Update all include paths in controller.cpp from local paths to shared library paths (e.g., #include "ui/preset_browser_view.h" → #include "ui/preset_browser_view.h")
- [X] T043 [US0] Add using declarations or explicit Krate::Plugins:: qualifiers where needed

### 3.3 Update Iterum Tests (11 Test Files)

- [X] T044 [P] [US0] Update plugins/iterum/tests/unit/platform/preset_paths_test.cpp (includes and namespaces)
- [X] T045 [P] [US0] Update plugins/iterum/tests/unit/ui/preset_data_source_test.cpp (includes and namespaces)
- [X] T046 [P] [US0] Update plugins/iterum/tests/unit/preset/preset_info_test.cpp (includes and namespaces)
- [X] T047 [P] [US0] Update plugins/iterum/tests/unit/ui/preset_browser_logic_test.cpp (includes and namespaces)
- [X] T048 [P] [US0] Update plugins/iterum/tests/integration/preset/preset_search_e2e_test.cpp (includes and namespaces)
- [X] T049 [P] [US0] Update plugins/iterum/tests/unit/preset/preset_search_test.cpp (includes and namespaces)
- [X] T050 [P] [US0] Update plugins/iterum/tests/unit/preset/preset_selection_test.cpp (includes and namespaces)
- [X] T051 [P] [US0] Update plugins/iterum/tests/unit/ui/search_debounce_test.cpp (includes and namespaces)
- [X] T052 [P] [US0] Update plugins/iterum/tests/integration/preset/search_integration_test.cpp (includes and namespaces)
- [X] T053 [P] [US0] Update plugins/iterum/tests/unit/preset/preset_manager_test.cpp (includes and namespaces)
- [X] T054 [P] [US0] Update plugins/iterum/tests/integration/preset/preset_loading_consistency_test.cpp (includes and namespaces)

### 3.4 Verify Iterum Tests Pass

- [X] T055 [US0] Build Iterum tests: cmake --build build/windows-x64-release --config Release --target iterum_tests
- [X] T056 [US0] Run Iterum tests and verify all 11 preset test suites pass (SC-009)
- [X] T057 [US0] Verify no ODR violations or linker errors (SC-010)

### 3.5 Cross-Platform Verification (MANDATORY)

- [X] T058 [US0] Verify IEEE 754 compliance: Check if any test files use std::isnan/std::isfinite/std::isinf → add to -fno-fast-math list in tests/CMakeLists.txt

### 3.6 Commit (MANDATORY)

- [ ] T059 [US0] Commit completed User Story 0 work (shared library refactoring)

**Checkpoint**: User Story 0 should be fully functional - shared library works for Iterum with all tests passing

---

## Phase 4: User Story 1 - Reliable Preset Save and Recall (Priority: P1) 🎯 MVP

**Goal**: Verify Disrumpo's existing versioned serialization (v1-v6) round-trips all ~450 parameters faithfully without data loss.

**Independent Test**: Configure all parameters to non-default values, serialize via getState(), deserialize via setState(), and verify all values match within floating-point precision (1e-6).

### 4.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T060 [US1] Create plugins/disrumpo/tests/unit/preset/serialization_round_trip_test.cpp with test case for v6 full round-trip
- [X] T061 [US1] Write test that configures all ~450 parameters (global, bands, crossovers, sweep, modulation, morph) to non-default values
- [X] T062 [US1] Write test that calls Processor::getState() to serialize to MemoryStream
- [X] T063 [US1] Write test that calls Processor::setState() to deserialize from same stream
- [X] T064 [US1] Write test assertions comparing all parameter values with Approx().margin(1e-6)
- [X] T065 [US1] Build and verify test FAILS (no changes to processor yet)

### 4.2 Verify Existing Serialization

- [X] T066 [US1] Review plugins/disrumpo/src/processor/processor.cpp getState() implementation (lines 441-712)
- [X] T067 [US1] Review plugins/disrumpo/src/processor/processor.cpp setState() implementation (lines 714+)
- [X] T068 [US1] Verify version 6 is current (kPresetVersion in plugin_ids.h)
- [X] T069 [US1] Run serialization_round_trip_test and verify it passes (FR-013, SC-001)

### 4.3 Edge Cases Testing

- [X] T070 [P] [US1] Add test case for empty stream (0 bytes) - verify plugin uses defaults without crashing
- [X] T071 [P] [US1] Add test case for truncated stream - verify plugin loads partial data with defaults
- [X] T072 [P] [US1] Add test case for version 0 (invalid) - verify plugin rejects and uses defaults (FR-012)
- [X] T073 [US1] Build and verify all edge case tests pass

### 4.4 Enumerated Type Round-Trip

- [X] T074 [US1] Add test cases verifying all enum types round-trip correctly: distortion type, morph mode, sweep falloff mode, modulation source, modulation curve, chaos model (FR-014)
- [X] T075 [US1] Build and verify enum tests pass

### 4.5 Cross-Platform Verification (MANDATORY)

- [X] T076 [US1] Verify IEEE 754 compliance: Check if serialization_round_trip_test uses std::isnan/std::isfinite/std::isinf → add to -fno-fast-math list in tests/CMakeLists.txt

### 4.6 Commit (MANDATORY)

- [ ] T077 [US1] Commit completed User Story 1 work (serialization verification)

**Checkpoint**: User Story 1 should be fully functional - serialization verified to round-trip all parameters

---

## Phase 5: User Story 2 - Version Migration for Future-Proofing (Priority: P2)

**Goal**: Verify Disrumpo can load presets from all historical versions (v1-v6) with appropriate defaults for parameters added in later versions.

**Independent Test**: Create presets at each version level (v1-v6), load with current version, verify known parameters load correctly and new parameters receive appropriate defaults.

### 5.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T078 [P] [US2] Add test case in serialization_round_trip_test.cpp for v1 preset (globals only) - verify loads with defaults for all other params (FR-009, FR-010)
- [X] T079 [P] [US2] Add test case for v2 preset (globals + bands + crossovers) - verify loads with defaults for sweep/modulation/morph
- [X] T080 [P] [US2] Add test case for v4 preset (+ sweep) - verify loads with defaults for modulation/morph
- [X] T081 [P] [US2] Add test case for v5 preset (+ modulation) - verify loads with defaults for morph
- [X] T082 [P] [US2] Add test case for future version v99 - verify loads known params and ignores trailing data (FR-011)
- [X] T083 [US2] Build and verify version migration tests FAIL (before any fixes)

### 5.2 Verify Version Migration Logic

- [X] T084 [US2] Review setState() version branching logic in processor.cpp
- [X] T085 [US2] Verify each version level applies correct defaults for parameters added in later versions
- [X] T086 [US2] Run version migration tests and verify all pass (SC-003)

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T087 [US2] Verify IEEE 754 compliance: Version migration tests should already be covered by T076, but double-check

### 5.4 Commit (MANDATORY)

- [ ] T088 [US2] Commit completed User Story 2 work (version migration verification)

**Checkpoint**: User Stories 1 AND 2 should both work - serialization verified for all versions

---

## Phase 6: User Story 3 - Browsing and Loading Factory Presets (Priority: P3)

**Goal**: Integrate Disrumpo controller with shared preset browser, allowing users to browse and load presets from the UI.

**Independent Test**: Open preset browser, navigate through categories, load presets, and verify plugin parameters update correctly.

### 6.1 Disrumpo PresetManager Configuration

- [X] T089 [US3] Create plugins/disrumpo/src/preset/disrumpo_preset_config.h with makeDisrumpoPresetConfig() and getDisrumpoTabLabels()
- [X] T090 [US3] Verify config specifies 11 subcategories: Init, Sweep, Morph, Bass, Leads, Pads, Drums, Experimental, Chaos, Dynamic, Lo-Fi (FR-027)
- [X] T091 [US3] Build and verify compilation

### 6.2 Disrumpo Controller Integration (Write Tests FIRST)

- [X] T092 [US3] Create plugins/disrumpo/tests/unit/controller/preset_integration_test.cpp
- [X] T093 [US3] Write test that creates PresetManager with Disrumpo config and verifies directory paths (SC-011)
- [X] T094 [US3] Write test for StateProvider callback (creates MemoryStream with processor state)
- [X] T095 [US3] Write test for LoadProvider callback (applies state via performEdit)
- [X] T096 [US3] Build and verify tests FAIL (before controller changes)

### 6.3 Implement Disrumpo Controller Integration

- [X] T097 [US3] Add PresetManager* member to plugins/disrumpo/src/controller/controller.h
- [X] T098 [US3] Add PresetBrowserView* member to controller.h
- [X] T099 [US3] Update plugins/disrumpo/CMakeLists.txt to link KratePluginsShared
- [X] T100 [US3] Implement Controller::createComponentStateStream() using MemoryStream and processor->getState()
- [X] T101 [US3] Implement Controller::applyComponentState() using performEdit mechanism
- [X] T102 [US3] Initialize PresetManager in Controller::initialize() with makeDisrumpoPresetConfig()
- [X] T103 [US3] Set StateProvider and LoadProvider callbacks in Controller::initialize()
- [X] T104 [US3] Create PresetBrowserView in editor creation with Disrumpo tab labels
- [X] T105 [US3] Add openPresetBrowser() and openSavePresetDialog() methods to Controller
- [X] T106 [US3] Build and verify preset_integration_test passes

### 6.4 Integration Testing

- [X] T107 [US3] Create plugins/disrumpo/tests/integration/preset/preset_browser_e2e_test.cpp
- [X] T108 [US3] Write test that opens preset browser and verifies 12 tabs (All + 11 categories) are displayed (FR-016, FR-019)
- [X] T109 [US3] Write test that simulates category selection and verifies filtering works
- [X] T110 [US3] Write test that verifies scanPresets() completes asynchronously (FR-019b)
- [X] T110a [US3] Write test that saves a Disrumpo preset and verifies XML metadata contains PlugInName="Disrumpo" and PlugInCategory="Distortion", NOT "Iterum"/"Delay" (verifies PresetManagerConfig is correctly applied)
- [X] T110b [US3] Write test that verifies factory presets are read-only: PresetManager::deletePreset() and overwritePreset() MUST return false with error message when preset.isFactory==true. Verify browser UI disables Delete/Overwrite buttons for factory presets (FR-031)
- [X] T111 [US3] Build and run integration tests

### 6.5 Cross-Platform Verification (MANDATORY)

- [X] T112 [US3] Verify IEEE 754 compliance: Check if preset integration tests use std::isnan/std::isfinite/std::isinf → add to -fno-fast-math list

### 6.6 Commit (MANDATORY)

- [ ] T113 [US3] Commit completed User Story 3 work (preset browser integration)

**Checkpoint**: User Story 3 complete - preset browser functional in Disrumpo

---

## Phase 7: User Story 4 - Saving User Presets (Priority: P4)

**Goal**: Enable users to save custom presets through the shared save dialog.

**Independent Test**: Create custom parameter settings, click Save button, enter name and category, verify .vstpreset file is created and appears in browser.

### 7.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T114 [US4] Create plugins/disrumpo/tests/integration/preset/preset_save_test.cpp
- [X] T115 [US4] Write test that simulates save dialog workflow: open dialog, enter name, select category, confirm save
- [X] T116 [US4] Write test that verifies .vstpreset file is created in correct user preset directory (FR-020, FR-021, FR-022)
- [X] T117 [US4] Write test that verifies preset appears in browser after save (FR-016)
- [X] T118 [US4] Write test for overwrite confirmation when preset with same name exists (FR-023)
- [X] T119 [US4] Write test for save failure in read-only directory with error message (FR-023a)
- [X] T120 [US4] Build and verify tests FAIL (save dialog not wired yet)

### 7.2 Implement Save Dialog Integration

- [X] T121 [US4] Verify shared SavePresetDialogView supports subcategory selection
- [X] T122 [US4] Wire Save button in Disrumpo editor to Controller::openSavePresetDialog()
- [X] T123 [US4] Implement save confirmation and overwrite prompts in PresetBrowserView
- [X] T124 [US4] Add error handling for save failures (permission denied, invalid name, etc.)
- [X] T125 [US4] Build and verify preset_save_test passes

### 7.3 Save Validation Tests

- [X] T126 [P] [US4] Add test for empty preset name validation (must reject)
- [X] T127 [P] [US4] Add test for special characters in preset name (must sanitize)
- [X] T128 [US4] Build and verify validation tests pass

### 7.4 Cross-Platform Verification (MANDATORY)

- [X] T129 [US4] Verify IEEE 754 compliance: Check if preset_save_test uses std::isnan/std::isfinite/std::isinf → add to -fno-fast-math list

### 7.5 Commit (MANDATORY)

- [ ] T130 [US4] Commit completed User Story 4 work (save user presets)

**Checkpoint**: User Story 4 complete - users can save custom presets

---

## Phase 8: User Story 5 - Searching and Filtering Presets (Priority: P5)

**Goal**: Enable users to search and filter presets by name and category using the shared search infrastructure.

**Independent Test**: Type search terms in search field, verify preset list updates correctly with results matching search criteria.

### 8.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T131 [US5] Create plugins/disrumpo/tests/integration/preset/preset_search_test.cpp
- [X] T132 [US5] Write test that types search term and verifies filtered results (FR-024, FR-026)
- [X] T133 [US5] Write test that combines search with category filtering (FR-025)
- [X] T134 [US5] Write test that verifies search updates within 100ms (SC-007)
- [X] T135 [US5] Write test for "no results" case with clear message
- [X] T136 [US5] Build and verify tests FAIL (search not wired yet)

### 8.2 Verify Search Infrastructure

- [X] T137 [US5] Verify shared SearchDebouncer is integrated into PresetBrowserView
- [X] T138 [US5] Verify PresetManager::searchPresets() works with Disrumpo config
- [X] T139 [US5] Verify PresetDataSource filtering works with string subcategory
- [X] T140 [US5] Build and verify preset_search_test passes

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T141 [US5] Verify IEEE 754 compliance: Check if preset_search_test uses std::isnan/std::isfinite/std::isinf → add to -fno-fast-math list

### 8.4 Commit (MANDATORY)

- [ ] T142 [US5] Commit completed User Story 5 work (search and filter)

**Checkpoint**: User Story 5 complete - search and filter functional

---

## Phase 9: User Story 6 - Factory Preset Quality and Coverage (Priority: P6)

**Goal**: Create 120 factory presets across 11 categories that demonstrate Disrumpo's sonic capabilities.

**Independent Test**: Load every factory preset and verify it loads without error, produces audio output, and matches its category's sonic intention.

### 9.1 Factory Preset Generator Tool (Write Tests FIRST)

- [ ] T143 [US6] Create tools/disrumpo_preset_generator.cpp following Iterum pattern (tools/preset_generator.cpp)
- [ ] T144 [US6] Implement BinaryWriter class with writeInt32(), writeFloat(), writeInt8() methods
- [ ] T145 [US6] Create DisrumpoPresetState struct matching Processor::getState() binary layout (v6 format)
- [ ] T146 [US6] Implement DisrumpoPresetState::serialize() method
- [ ] T147 [US6] Add disrumpo_preset_generator executable to CMakeLists.txt
- [ ] T148 [US6] Build and verify generator compiles

### 9.2 Init Presets (5 Presets)

- [ ] T149 [US6] Implement Init 1 Band preset (1 band, Soft Clip, drive 0.0, mix 100%, bypass-equivalent) (FR-030)
- [ ] T150 [P] [US6] Implement Init 2 Bands preset (2 bands, Soft Clip, drive 0.0)
- [ ] T151 [P] [US6] Implement Init 3 Bands preset (3 bands, Soft Clip, drive 0.0)
- [ ] T152 [P] [US6] Implement Init 4 Bands preset (4 bands, Soft Clip, drive 0.0)
- [ ] T153 [P] [US6] Implement Init 5 Bands preset (5 bands, Soft Clip, drive 0.0)
- [ ] T154 [US6] Generate Init presets and verify .vstpreset files are created in Init/ directory (FR-027, FR-028)

### 9.3 Factory Preset Round-Trip Validation

- [ ] T155 [US6] Create plugins/disrumpo/tests/integration/preset/factory_preset_validation_test.cpp
- [ ] T156 [US6] Write test that loads each factory preset, serializes to stream, deserializes, and compares all values (FR-015)
- [ ] T157 [US6] Build and verify factory preset validation test FAILS (only Init presets exist so far)

### 9.4 Remaining Factory Presets (115 Presets - Sound Design Task)

**Note**: This is a sound design deliverable requiring ~60+ hours of manual preset creation. Each preset must be crafted with meaningful parameter values that showcase the plugin's capabilities.

**Acceptance criteria for all factory presets**: Each preset MUST be musically useful and sonically distinct within its category. Avoid duplicates (e.g., 3 presets with identical settings except drive). Prioritize variety across distortion types, sweep modes, morph configurations, band counts, and modulation routings. Each category should demonstrate the breadth of that category's sonic palette, not just variations of a single approach.

- [ ] T158 [US6] Implement 15 Sweep category presets showcasing sweep system features (variety across sweep modes, linking curves, frequency ranges)
- [ ] T159 [US6] Implement 15 Morph category presets (variety across morph modes: 1D linear, 2D planar, 2D radial; different node counts and cross-family transitions)
- [ ] T160 [US6] Implement 10 Bass category presets (variety across distortion types optimized for low-end: tube, tape, fuzz, wavefold; different band counts)
- [ ] T161 [US6] Implement 10 Leads category presets (variety across aggressive distortion types: hard clip, fuzz, feedback, chaos; with modulation)
- [ ] T162 [US6] Implement 10 Pads category presets (variety across subtle processing: soft clip, tape, allpass resonant; with slow morph and sweep)
- [ ] T163 [US6] Implement 10 Drums category presets (variety across transient-friendly types: hard clip, bitcrush, temporal; fast modulation)
- [ ] T164 [US6] Implement 15 Experimental category presets (variety across experimental types: spectral, granular, fractal, stochastic, formant; creative parameter combos)
- [ ] T165 [US6] Implement 10 Chaos category presets (variety across chaos models, attractor types, and chaos→morph modulation routings)
- [ ] T166 [US6] Implement 10 Dynamic category presets (variety across envelope follower, transient detector, and pitch follower modulation sources)
- [ ] T167 [US6] Implement 10 Lo-Fi category presets (variety across bitcrush, sample reduce, quantize, aliasing, and bitwise mangler; different bit depths and rates)

### 9.5 Factory Preset Installation

- [ ] T168 [US6] Update CMakeLists.txt to run disrumpo_preset_generator during build
- [ ] T169 [US6] Verify generated .vstpreset files are placed in staging directory for installer (FR-032)
- [ ] T170 [US6] Build and verify factory_preset_validation_test passes for all 120 presets (SC-004)

### 9.6 Naming Convention Verification

- [ ] T171 [US6] Verify all factory preset names are descriptive and evocative (not technical jargon) (FR-029)
- [ ] T172 [US6] Verify no two presets in same category share the same name
- [ ] T173 [US6] Verify preset count per category matches spec: Init(5), Sweep(15), Morph(15), Bass(10), Leads(10), Pads(10), Drums(10), Experimental(15), Chaos(10), Dynamic(10), Lo-Fi(10) = 120 total

### 9.7 Cross-Platform Verification (MANDATORY)

- [ ] T174 [US6] Verify IEEE 754 compliance: Check if factory_preset_validation_test uses std::isnan/std::isfinite/std::isinf → add to -fno-fast-math list

### 9.8 Commit (MANDATORY)

- [ ] T175 [US6] Commit completed User Story 6 work (factory presets)

**Checkpoint**: All user stories complete - 120 factory presets generated and validated

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T176 [P] Performance optimization: Verify preset load completes within 50ms using high-resolution timer wrapping full setState→performEdit sequence at max config (8 bands, 4 nodes, 32 mod routes) (SC-002)
- [X] T177 [P] Performance optimization: Verify search results update within 100ms with 500+ presets. Add timing assertion in preset_search_test.cpp (SC-007)
- [ ] T178 [US3] Write test and implement debounce/coalesce pattern for rapid preset load requests: if new load arrives during in-progress load, cancel in-progress and apply only the most recent (FR-019a)
- [ ] T179 [P] [US3] Write test and implement refresh button in PresetBrowserView that triggers manual rescan of preset directories. Verify async scanning at startup does not block UI thread (FR-019b)
- [X] T180 Verify preset system passes pluginval at strictness level 5 (SC-008)
- [ ] T180a [US4] Write test and implement save error handling: verify PresetManager returns clear error message on permission denied, read-only filesystem, or sandboxed environment. Verify UI displays the error to the user with no silent fallbacks (FR-023a)

---

## Phase 11: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 11.1 Architecture Documentation Update

- [ ] T181 Update specs/_architecture_/ with new shared preset library components:
  - Add KratePluginsShared library to appropriate architecture document
  - Document PresetManagerConfig pattern for plugin configuration
  - Document shared UI components (CategoryTabBar, PresetBrowserView, etc.)
  - Include usage examples for future plugins
  - Add to quickstart for preset system integration

### 11.2 Final Commit

- [ ] T182 Commit architecture documentation updates
- [ ] T183 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 12: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 12.1 Run Clang-Tidy Analysis

- [ ] T184 Run clang-tidy on all modified/new source files:
  ```bash
  # Windows (PowerShell)
  ./tools/run-clang-tidy.ps1 -Target all

  # Linux/macOS
  ./tools/run-clang-tidy.sh --target all
  ```

### 12.2 Address Findings

- [ ] T185 Fix all errors reported by clang-tidy (blocking issues)
- [ ] T186 Review warnings and fix where appropriate (use judgment for DSP code)
- [ ] T187 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason). If a clang-tidy check is a false positive or conflicts with VST3 SDK/VSTGUI patterns (e.g., framework-required raw pointer usage, SDK callback signatures), add `// NOLINT(check-name) - reason` with a clear justification. Document all suppressions in the commit message.

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 13.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T188 Review ALL FR-xxx requirements (FR-001 through FR-044) from spec.md against implementation
- [ ] T189 Review ALL SC-xxx success criteria (SC-001 through SC-011) and verify measurable targets are achieved
- [ ] T190 Search for cheating patterns in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 13.2 Fill Compliance Table in spec.md

- [ ] T191 Update spec.md "Implementation Verification" section with compliance status for each requirement
- [ ] T192 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 13.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T193 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 14: Final Completion

**Purpose**: Final commit and completion claim

### 14.1 Final Commit

- [ ] T194 Commit all spec work to feature branch
- [ ] T195 Verify all tests pass

### 14.2 Completion Claim

- [ ] T196 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-9)**: All depend on Foundational phase completion
  - User Story 0 (P0) must complete before User Stories 1-6
  - User Stories 1-2 (serialization) can proceed independently of User Story 3-6 (browser)
  - User Stories 3-5 can proceed in parallel after US0
  - User Story 6 (factory presets) depends on US1-2 completion (serialization verified)
- **Polish (Phase 10)**: Depends on all desired user stories being complete
- **Documentation (Phase 11)**: Must complete before verification
- **Static Analysis (Phase 12)**: Must complete before verification
- **Completion Verification (Phase 13-14)**: Final phases

### User Story Dependencies

- **User Story 0 (P0)**: Can start after Foundational (Phase 2) - BLOCKS all other stories
- **User Story 1 (P1)**: Can start after US0 - Serialization verification (independent)
- **User Story 2 (P2)**: Can start after US1 - Version migration (depends on serialization)
- **User Story 3 (P3)**: Can start after US0 - Browser integration (independent of US1-2)
- **User Story 4 (P4)**: Can start after US3 - Save dialog (depends on browser)
- **User Story 5 (P5)**: Can start after US3 - Search (depends on browser)
- **User Story 6 (P6)**: Can start after US1-2 complete - Factory presets (depends on serialization verification)

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Models/entities before services
- Services before UI integration
- Core implementation before integration
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- All Setup tasks marked [P] can run in parallel
- All Foundational tasks marked [P] can run in parallel (within Phase 2)
- Once US0 completes:
  - US1-2 can proceed in parallel (serialization workstream)
  - US3-5 can proceed in parallel (browser workstream)
- All tests for a user story marked [P] can run in parallel
- Different user stories can be worked on in parallel by different team members

---

## Parallel Example: User Story 0

```bash
# Launch all Iterum test updates together (after shared library is complete):
Task T044: "Update preset_paths_test.cpp (includes and namespaces)"
Task T045: "Update preset_data_source_test.cpp (includes and namespaces)"
Task T046: "Update preset_info_test.cpp (includes and namespaces)"
# ... all 11 test files can be updated in parallel
```

---

## Implementation Strategy

### MVP First (User Story 0 + 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 0 (shared library refactoring)
4. Complete Phase 4: User Story 1 (serialization verification)
5. **STOP and VALIDATE**: Test serialization independently
6. This gives you a working shared library and verified serialization - foundation for all remaining work

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 0 → Shared library refactored, Iterum tests pass → Deploy/Demo
3. Add User Story 1 → Serialization verified → Test independently
4. Add User Story 2 → Version migration verified → Test independently
5. Add User Story 3 → Browser integrated → Deploy/Demo (users can browse presets!)
6. Add User Story 4 → Save dialog → Deploy/Demo (users can save custom presets!)
7. Add User Story 5 → Search → Deploy/Demo (users can search presets!)
8. Add User Story 6 → Factory presets → Deploy/Demo (complete preset system with 120 presets!)

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 0 (shared library) - BLOCKS others
3. Once US0 is done:
   - Developer A: User Story 1-2 (serialization verification)
   - Developer B: User Story 3-5 (browser integration)
4. After US1-2 complete:
   - Developer C: User Story 6 (factory presets)
5. Stories complete and integrate independently

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

---

## Build Commands Reference

```bash
# Windows (Git Bash) - Use full CMake path
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build everything
"$CMAKE" --build build/windows-x64-release --config Release

# Build specific targets
"$CMAKE" --build build/windows-x64-release --config Release --target KratePluginsShared
"$CMAKE" --build build/windows-x64-release --config Release --target iterum_tests
"$CMAKE" --build build/windows-x64-release --config Release --target disrumpo_tests

# Run tests
build/windows-x64-release/plugins/iterum/tests/Release/iterum_tests.exe
build/windows-x64-release/plugins/disrumpo/tests/Release/disrumpo_tests.exe

# Run preset generator
build/windows-x64-release/bin/Release/disrumpo_preset_generator.exe [output_dir]

# Pluginval (after plugin code changes)
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Disrumpo.vst3"

# Clang-tidy (static analysis)
./tools/run-clang-tidy.ps1 -Target all
```
