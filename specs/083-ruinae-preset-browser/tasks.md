# Tasks: Ruinae Preset Browser Integration

**Input**: Design documents from `specs/083-ruinae-preset-browser/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/controller-api.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

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
   - Add the file to the `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/controller/preset_browser_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

This check prevents CI failures on macOS/Linux that pass locally on Windows.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Register Test File)

**Purpose**: Wire the new test file into the build system so it is discovered before any implementation exists

- [X] T001 Register `plugins/ruinae/tests/unit/controller/preset_browser_test.cpp` in `plugins/ruinae/tests/CMakeLists.txt` (add to ruinae_tests sources list)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core controller additions that block all three user stories. These private helpers and field declarations must exist before any user-story-level wiring can be written or tested.

**CRITICAL**: No user story work can begin until this phase is complete.

### 2.1 Controller Header: New Fields and Method Declarations

- [X] T002 Add raw pointer fields `presetBrowserView_` and `savePresetDialogView_` to `plugins/ruinae/src/controller/controller.h` (initialized to nullptr; follow contracts/controller-api.md "New Private Fields" section exactly)
- [X] T003 Add public method declarations `openPresetBrowser()`, `closePresetBrowser()`, `openSavePresetDialog()`, `createCustomView()` to `plugins/ruinae/src/controller/controller.h` (follow contracts/controller-api.md "New Public Methods" section)
- [X] T004 Add private method declarations `createComponentStateStream()`, `loadComponentStateWithNotify()`, `editParamWithNotify()` to `plugins/ruinae/src/controller/controller.h` (follow contracts/controller-api.md "New Private Methods" section)
- [X] T005 Add required `#include` directives to `plugins/ruinae/src/controller/controller.h` for `PresetBrowserView`, `SavePresetDialogView`, and VST3 SDK stream types needed by the new method signatures

### 2.2 Build Verification

- [X] T006 Build `ruinae_tests` target to confirm header changes compile cleanly with zero warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests`

**Checkpoint**: Foundation ready - all three user story phases can now proceed in sequence

---

## Phase 3: User Story 1 - Browse and Load Factory Presets (Priority: P1) MVP

**Goal**: Wire `stateProvider`/`loadProvider` callbacks into `presetManager_`, implement `editParamWithNotify`/`createComponentStateStream`/`loadComponentStateWithNotify`, and create the overlay views in `didOpen()` with cleanup in `willClose()`. After this phase the preset browser opens, shows all 13 tabs, and loads factory presets including full arpeggiator lane state.

**Independent Test**: Build and run `ruinae_tests`. Tests in `preset_browser_test.cpp` covering state roundtrip, preset discovery, and overlay lifecycle must pass. Manual verification: open the plugin UI, click Presets, select a category, double-click a factory preset, confirm all parameters update.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [X] T007 [US1] Create `plugins/ruinae/tests/unit/controller/preset_browser_test.cpp` with Catch2 test suite skeleton, includes for controller.h and required mocks
- [X] T008 [US1] Write test: `editParamWithNotify` calls `beginEdit`, `setParamNormalized`, `performEdit`, `endEdit` in that order with a known ParamID and clamped value (use mock/spy component handler)
- [X] T009 [US1] Write test: `createComponentStateStream` returns a non-null `MemoryStream` containing valid versioned binary data when the host component handler provides a state (mock host returning a known stream)
- [X] T010 [US1] Write test: `loadComponentStateWithNotify` with a serialized v1 state stream calls `editParamWithNotify` for every parameter and returns true; a zero-byte stream returns false without crashing
- [X] T011 [US1] Write test: `stateProvider` callback is non-null after `initialize()` completes (verifies wiring without needing a live host)
- [X] T012 [US1] Write test: `loadProvider` callback is non-null after `initialize()` completes
- [X] T013 [US1] Write test: `openPresetBrowser` calls `PresetBrowserView::open("")` when browser is not already open; is a no-op when already open (use mock view)
- [X] T014 [US1] Write test: `closePresetBrowser` calls `PresetBrowserView::close()` when browser is open; is a no-op when already closed
- [X] T015 [US1] Build and verify all new tests FAIL (no implementation yet): `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe "[preset browser]"`

### 3.2 Implementation for User Story 1

- [X] T016 [US1] Implement `editParamWithNotify` in `plugins/ruinae/src/controller/controller.cpp`: clamp value to [0.0, 1.0], then call `beginEdit` / `setParamNormalized` / `performEdit` / `endEdit` (follow contracts/controller-api.md "editParamWithNotify" contract exactly)
- [X] T017 [US1] Implement `createComponentStateStream` in `plugins/ruinae/src/controller/controller.cpp`: query component handler for `IComponent`, create `Steinberg::MemoryStream`, call `getState(stream)`, seek to stream start, return stream. Return `nullptr` on failure. Do NOT re-implement serialization (follow research.md decision #1 and contracts/controller-api.md "createComponentStateStream" contract)
- [X] T018 [US1] Implement `loadComponentStateWithNotify` in `plugins/ruinae/src/controller/controller.cpp`: mirror `setComponentState()` deserialization order exactly (data-model.md "Serialization Order" items 1-37), but replace every `setParamNormalized` call with `editParamWithNotify`; read and discard voice routes (item 20); read int8 FX enable flags (items 21, 22, 24, 36) and convert to 0.0/1.0 before passing to `editParamWithNotify`; return false on invalid version or truncated stream (follow contracts/controller-api.md "loadComponentStateWithNotify" contract)
- [X] T019 [US1] Wire `stateProvider` and `loadProvider` callbacks in `Controller::initialize()` in `plugins/ruinae/src/controller/controller.cpp`: add lambdas after existing `presetManager_` construction, following contracts/controller-api.md "Initialization Wiring" section exactly
- [X] T020 [US1] Add overlay creation in `Controller::didOpen()` in `plugins/ruinae/src/controller/controller.cpp`: create `PresetBrowserView` and `SavePresetDialogView` with frame size and `getRuinaeTabLabels()`, add both to frame; guard with `if (presetManager_)` and `if (frame)`; follow contracts/controller-api.md "didOpen() Additions" exactly
- [X] T021 [US1] Add cleanup in `Controller::willClose()` in `plugins/ruinae/src/controller/controller.cpp`: null out `presetBrowserView_` and `savePresetDialogView_` BEFORE `activeEditor_ = nullptr`; follow contracts/controller-api.md "willClose() Additions" exactly
- [X] T022 [US1] Implement `openPresetBrowser` in `plugins/ruinae/src/controller/controller.cpp`: guard with view existence and `!isOpen()` check, call `presetBrowserView_->open("")` (follow contracts/controller-api.md "openPresetBrowser" contract)
- [X] T023 [US1] Implement `closePresetBrowser` in `plugins/ruinae/src/controller/controller.cpp`: guard with view existence and `isOpen()` check, call `presetBrowserView_->close()`
- [X] T024 [US1] Implement `openSavePresetDialog` in `plugins/ruinae/src/controller/controller.cpp`: guard with view existence and `!isOpen()` check, call `savePresetDialogView_->open("")`

### 3.3 Verify User Story 1

- [X] T025 [US1] Build `ruinae_tests` target and verify zero compilation errors/warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests`
- [X] T026 [US1] Run ruinae_tests and verify all User Story 1 tests pass (T007-T014): `build/windows-x64-release/bin/Release/ruinae_tests.exe "[preset browser]"`
- [X] T027 [US1] Run full ruinae_tests suite and verify zero regressions: `build/windows-x64-release/bin/Release/ruinae_tests.exe`

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T028 [US1] Verify IEEE 754 compliance: check if `plugins/ruinae/tests/unit/controller/preset_browser_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf`; if so, add to `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt`

### 3.5 Commit (MANDATORY)

- [ ] T029 [US1] Commit completed User Story 1 work (state management wiring and overlay lifecycle)

**Checkpoint**: User Story 1 complete - preset browser opens, 13 tabs show, factory presets load with full state including arp lanes

---

## Phase 4: User Story 2 - Save User Presets (Priority: P2)

**Goal**: Wire the Save button view class (`SavePresetButton`) and `createCustomView` override so users can save the current plugin state as a .vstpreset file in the user preset directory. Depends on Phase 3 state management being complete.

**Independent Test**: Build and run `ruinae_tests`. Save dialog tests must pass. Manual verification: adjust parameters, click Save, enter a name, confirm; reopen browser and verify the saved preset appears and reloads all parameters correctly.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T030 [US2] Write test: `createCustomView("SavePresetButton", ...)` returns a non-null `CView*` (a `SavePresetButton` instance) with the correct rect derived from UIAttributes origin/size
- [ ] T031 [US2] Write test: `createCustomView` returns `nullptr` for an unknown view name (no crash, no fallback)
- [ ] T032 [US2] Write test: `openSavePresetDialog` is a no-op when `savePresetDialogView_` is null (no crash when called before `didOpen`)
- [ ] T033 [US2] Build and verify new tests FAIL: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests`

### 4.2 Implementation for User Story 2

- [ ] T034 [US2] Add `SavePresetButton` class in anonymous namespace at the top of `plugins/ruinae/src/controller/controller.cpp`: extends `VSTGUI::CTextButton`, stores `Ruinae::Controller*`, overrides `onMouseDown` to call `controller_->openSavePresetDialog()` on left-click (follow contracts/controller-api.md "Anonymous Namespace Classes" and Disrumpo pattern at controller.cpp:1187-1210)
- [ ] T035 [US2] Implement `createCustomView` in `plugins/ruinae/src/controller/controller.cpp`: handle `"SavePresetButton"` case reading origin/size from UIAttributes and returning a new `SavePresetButton`; return `nullptr` for all other names (follow contracts/controller-api.md "createCustomView Handler" exactly)

### 4.3 Verify User Story 2

- [ ] T036 [US2] Build `ruinae_tests` and verify zero errors/warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests`
- [ ] T037 [US2] Run ruinae_tests and verify all User Story 2 tests pass (T030-T032): `build/windows-x64-release/bin/Release/ruinae_tests.exe "[preset browser]"`
- [ ] T038 [US2] Run full ruinae_tests suite and verify zero regressions

### 4.4 Cross-Platform Verification (MANDATORY)

- [ ] T039 [US2] Verify IEEE 754 compliance: new tests are UI wiring only and do not use IEEE 754 functions; confirm no `-fno-fast-math` changes needed

### 4.5 Commit (MANDATORY)

- [ ] T040 [US2] Commit completed User Story 2 work (SavePresetButton and createCustomView)

**Checkpoint**: User Story 2 complete - Save button view class exists and createCustomView handles it

---

## Phase 5: User Story 3 - Search, Delete, and Import Presets (Priority: P3)

**Goal**: Wire the `PresetBrowserButton` custom view class so the "Presets" button appears in the top bar and opens the browser. Add both button placements to `editor.uidesc`. The shared `PresetBrowserView` already handles search (SearchDebouncer, 200ms), delete (with confirmation, factory preset protection), and import (VSTGUI file selector). This phase connects the entry points so these features are accessible from the UI.

**Independent Test**: Build and run `ruinae_tests`. Browser button tests must pass. Manual verification: open the plugin UI, confirm the Presets and Save buttons appear in the top bar at the correct positions, click Presets, type in the search field (results update within 200ms), select a user preset and delete it, click Import and choose a .vstpreset file.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T041 [US3] Write test: `createCustomView("PresetBrowserButton", ...)` returns a non-null `CView*` with the correct rect derived from UIAttributes origin/size
- [ ] T042 [US3] Write test: `createCustomView` handles both `"PresetBrowserButton"` and `"SavePresetButton"` in a single call sequence without crash or state corruption
- [ ] T043 [US3] Write test: `openPresetBrowser` is a no-op when `presetBrowserView_` is null (no crash when called before `didOpen`)
- [ ] T044 [US3] Build and verify new tests FAIL: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests`

### 5.2 Implementation for User Story 3

- [ ] T045 [US3] Add `PresetBrowserButton` class in anonymous namespace at the top of `plugins/ruinae/src/controller/controller.cpp`: extends `VSTGUI::CTextButton`, stores `Ruinae::Controller*`, overrides `onMouseDown` to call `controller_->openPresetBrowser()` on left-click (follow contracts/controller-api.md "Anonymous Namespace Classes" and Disrumpo pattern at controller.cpp:1159-1182)
- [ ] T046 [US3] Extend `createCustomView` in `plugins/ruinae/src/controller/controller.cpp` to also handle `"PresetBrowserButton"` case reading origin/size from UIAttributes and returning a new `PresetBrowserButton` (add before the SavePresetButton case; follow contracts/controller-api.md "createCustomView Handler")
- [ ] T047 [US3] Add `PresetBrowserButton` view element to the top bar `CViewContainer` in `plugins/ruinae/resources/editor.uidesc`: `<view custom-view-name="PresetBrowserButton" origin="460, 8" size="80, 25"/>` (follow contracts/controller-api.md "Top Bar Button Definitions" and research.md decision #4)
- [ ] T048 [US3] Add `SavePresetButton` view element to the top bar `CViewContainer` in `plugins/ruinae/resources/editor.uidesc`: `<view custom-view-name="SavePresetButton" origin="550, 8" size="60, 25"/>` (follow contracts/controller-api.md "Top Bar Button Definitions")

### 5.3 Verify User Story 3

- [ ] T049 [US3] Build full plugin (not just tests) to verify uidesc changes compile and link: `"$CMAKE" --build build/windows-x64-release --config Release`
- [ ] T050 [US3] Build `ruinae_tests` and verify zero errors/warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests`
- [ ] T051 [US3] Run ruinae_tests and verify all User Story 3 tests pass (T041-T043): `build/windows-x64-release/bin/Release/ruinae_tests.exe "[preset browser]"`
- [ ] T052 [US3] Run full ruinae_tests suite and verify zero regressions: `build/windows-x64-release/bin/Release/ruinae_tests.exe`
- [ ] T053 [US3] Run full shared_tests suite to verify no regressions in shared preset infrastructure: `build/windows-x64-release/bin/Release/shared_tests.exe`

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T054 [US3] Verify IEEE 754 compliance: new tests are UI wiring only; confirm no `-fno-fast-math` changes needed

### 5.5 Commit (MANDATORY)

- [ ] T055 [US3] Commit completed User Story 3 work (PresetBrowserButton, createCustomView, editor.uidesc button placements)

**Checkpoint**: User Story 3 complete - both buttons visible in top bar; search/delete/import accessible through shared PresetBrowserView

---

## Phase 6: Polish and Cross-Cutting Concerns

**Purpose**: Pluginval lifecycle verification and overall quality gate

- [ ] T056 [P] Run pluginval at strictness level 5 to verify preset browser opens/closes without memory leaks across lifecycle cycles (SC-004): `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"`
- [ ] T057 [P] Verify SC-002: open the preset browser, navigate to the "All" tab, confirm all 14 factory presets appear (Arp Classic: 3, Arp Acid: 2, Arp Euclidean: 3, Arp Polymetric: 2, Arp Generative: 2, Arp Performance: 2); then click the "Arp Acid" tab and confirm only the 2 Arp Acid presets are shown (verifies Acceptance Scenario 2 of User Story 1 -- tab filtering works correctly)
- [ ] T058 [P] Verify SC-001: open preset browser, select a category, double-click a factory preset, confirm parameters update within 5 seconds
- [ ] T059 [P] Verify SC-003: (1) adjust at least one synth parameter (e.g., oscillator A pitch), at least one arp step velocity value on lane 1, at least one arp pitch offset on lane 2, and at least one arp gate value; (2) save as a user preset; (3) change those same parameters to different values; (4) reopen browser and reload the saved preset; (5) confirm all four changed values are restored to their saved state -- verifies full state including arpeggiator lane data is correctly round-tripped (SC-003)
- [ ] T060 Verify SC-006: confirm preset browser appearance and behavior matches Iterum and Disrumpo -- check all of the following: (a) all 13 tabs are always visible including empty synth categories (Pads, Leads, Bass, Textures, Rhythmic, Experimental show empty list without error); (b) tab label text matches exactly the strings in `getRuinaeTabLabels()` with no truncation or substitution; (c) the browser overlay covers the full editor area (1400x800); (d) double-clicking a preset closes the browser (not just loads the preset, it also dismisses the overlay)
- [ ] T061 [P] Verify FR-014 (keyboard interaction): with the preset browser open, press Escape and confirm the browser closes without loading any preset; with the save dialog open, press Escape and confirm it closes without saving; confirm both behaviors work regardless of which category tab is active
- [ ] T062 [P] Verify FR-010 (delete user preset): save a user preset, reopen the browser, select the saved preset, click Delete and confirm in the dialog -- confirm the preset file is removed from disk and disappears from the browser list; then select a factory preset and click Delete -- confirm the action is disabled or shows a message preventing factory preset deletion
- [ ] T063 [P] Verify FR-011 (import preset): click Import in the preset browser, select a valid .vstpreset file from an arbitrary location on disk via the file selector, confirm the file is copied to the user preset directory and the imported preset appears in the browser list without requiring a manual refresh
- [ ] T064 Verify edge case behavior: (a) close the Ruinae editor while the preset browser is open -- confirm no crash and no memory leak (supplementary to SC-004); (b) enter a duplicate preset name in the save dialog -- confirm an overwrite confirmation dialog appears rather than silently overwriting; (c) if a corrupted .vstpreset file exists in the preset directory, confirm the plugin does not crash and the corrupted file is skipped during scanning
- [ ] T065 [P] Verify SC-005 (search timing): open the preset browser with multiple presets visible, type a character in the search field and measure (or observe) that the list updates within 200ms; then clear the search field and confirm the full list is restored immediately (0ms, no debounce delay)

---

## Phase 7: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIV**: Every spec implementation MUST update architecture documentation as a final task.

### 7.1 Architecture Documentation Update

- [ ] T066 Update `specs/_architecture_/` to document that Ruinae now uses the shared preset infrastructure: add Ruinae to any registry of plugins using `KratePluginsShared`, note the `createComponentStateStream` host-delegation pattern (vs Disrumpo's controller-side serialization), and document any Ruinae-specific integration gotchas (voice route skip, int8 FX enable flags, arp lane handling)

### 7.2 Final Commit

- [ ] T067 Commit architecture documentation updates
- [ ] T068 Verify all spec work is committed to feature branch `083-ruinae-preset-browser`

**Checkpoint**: Architecture documentation reflects Ruinae preset browser integration

---

## Phase 8: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before completion claim

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 8.1 Run Clang-Tidy Analysis

- [ ] T069 Run clang-tidy on all modified/new source files:
  ```bash
  # Windows (PowerShell)
  ./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja

  # Linux/macOS
  ./tools/run-clang-tidy.sh --target ruinae
  ```

### 8.2 Address Findings

- [ ] T070 Fix all errors reported by clang-tidy (blocking issues)
- [ ] T071 Review warnings and fix where appropriate; for VST3/VSTGUI framework-required patterns (raw pointer ownership of frame-owned views, SDK callback signatures), add `// NOLINT(check-name) - reason` with a clear justification
- [ ] T072 Document any suppressions in the commit message

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming done when requirements are not met is a violation of trust.

### 9.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T073 Review ALL FR-xxx requirements (FR-001 through FR-014) from `specs/083-ruinae-preset-browser/spec.md` against implementation; open each relevant file and confirm the code satisfies the requirement
- [ ] T074 Review ALL SC-xxx success criteria (SC-001 through SC-006) and verify measurable targets are achieved; record actual measured values not assumptions
- [ ] T075 Search for cheating patterns in new code:
  - [ ] No `// placeholder` or `// TODO` comments in `controller.h`, `controller.cpp`, `editor.uidesc`, `preset_browser_test.cpp`
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 9.2 Fill Compliance Table in spec.md

- [ ] T076 Update `specs/083-ruinae-preset-browser/spec.md` "Implementation Verification" section: for each FR-xxx row, record file path and line number of the implementing code; for each SC-xxx row, record the actual measured value and test name
- [ ] T077 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 9.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T078 All self-check questions answered "no" (or gaps documented honestly with user approval)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim

### 10.1 Final Commit

- [ ] T079 Commit all remaining spec work to feature branch `083-ruinae-preset-browser`
- [ ] T080 Verify all tests pass (ruinae_tests + shared_tests): `build/windows-x64-release/bin/Release/ruinae_tests.exe && build/windows-x64-release/bin/Release/shared_tests.exe`

### 10.2 Completion Claim

- [ ] T081 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies and Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - start immediately
- **Foundational (Phase 2)**: Depends on Phase 1 completion - BLOCKS all user stories
- **User Story 1 (Phase 3)**: Depends on Phase 2 - BLOCKS User Story 2 and 3 (state management must exist first)
- **User Story 2 (Phase 4)**: Depends on Phase 3 (uses `openSavePresetDialog` from US1)
- **User Story 3 (Phase 5)**: Depends on Phase 3 (uses `openPresetBrowser` from US1); can proceed after US1 even if US2 is in-progress
- **Polish (Phase 6)**: Depends on all three user stories complete
- **Documentation (Phase 7)**: Must complete before verification
- **Static Analysis (Phase 8)**: Must complete before verification
- **Completion Verification (Phase 9-10)**: Final phases

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Phase 2 Foundational - no story dependencies
- **User Story 2 (P2)**: Can start after User Story 1 - depends on `openSavePresetDialog` and `loadComponentStateWithNotify` from US1
- **User Story 3 (P3)**: Can start after User Story 1 - depends on `openPresetBrowser` from US1; is independent of US2

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XIII)
- Header declarations before implementation
- Helper methods before callers (`editParamWithNotify` before `loadComponentStateWithNotify`)
- `initialize()` wiring before `didOpen()` overlay creation
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` if needed
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

- T002, T003, T004, T005 (Phase 2 header additions) can be written in a single editing session as they touch the same file sequentially
- T007-T014 (Phase 3 test writing) can be written in parallel as they are all in the same new test file
- T016-T019 (Phase 3 core methods) can be drafted in parallel as they are independent implementations that compose in `initialize()`/`didOpen()`
- T047 and T048 (Phase 5 uidesc additions) can be done in a single file edit
- T056-T065 (Phase 6 Polish) are all read-only verification tasks that can be run in parallel

---

## Parallel Example: User Story 1

```bash
# Write all US1 tests together in preset_browser_test.cpp (T007-T014):
# - editParamWithNotify sequence test
# - createComponentStateStream returns non-null stream test
# - loadComponentStateWithNotify round-trip test
# - stateProvider/loadProvider non-null after initialize() tests
# - openPresetBrowser/closePresetBrowser toggle tests
# All can be written in a single session before any implementation begins

# After tests are written and confirmed failing, implement the four core methods together:
# T016: editParamWithNotify
# T017: createComponentStateStream
# T018: loadComponentStateWithNotify
# T019: initialize() wiring
# These implementations compose naturally - draft all four, then build once
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (register test file)
2. Complete Phase 2: Foundational (header declarations)
3. Complete Phase 3: User Story 1 (state management + overlay lifecycle)
4. **STOP and VALIDATE**: Run ruinae_tests, open plugin UI, verify browser opens and loads presets
5. The plugin now has a working preset browser - all core value is delivered

### Incremental Delivery

1. Complete Setup + Foundational (Phases 1-2) - build foundation
2. Add User Story 1 (Phase 3) - preset browser works, factory presets load - MVP
3. Add User Story 2 (Phase 4) - Save button view class wired - users can save presets
4. Add User Story 3 (Phase 5) - Presets button in top bar + uidesc wired - full entry points visible
5. Polish + Documentation + Static Analysis (Phases 6-8) - quality gates
6. Completion Verification (Phases 9-10) - honest sign-off

### Single-Developer Strategy

This is a focused single-file integration task. The recommended sequential order is:

1. Phase 1 (T001) - 5 minutes
2. Phase 2 (T002-T006) - 20 minutes
3. Phase 3 tests first (T007-T015) - 45 minutes
4. Phase 3 implementation (T016-T027) - 60 minutes
5. Phase 3 verification + commit (T028-T029) - 15 minutes
6. Phase 4 (T030-T040) - 30 minutes
7. Phase 5 (T041-T055) - 45 minutes
8. Phases 6-10 (T056-T081) - 90 minutes

---

## Build Commands Reference

```bash
# Windows (Git Bash) - Use full CMake path
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build ruinae tests only (fastest iteration loop)
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests

# Build full plugin (needed for uidesc changes and pluginval)
"$CMAKE" --build build/windows-x64-release --config Release

# Run ruinae tests
build/windows-x64-release/bin/Release/ruinae_tests.exe

# Run ruinae preset browser tests only
build/windows-x64-release/bin/Release/ruinae_tests.exe "[preset browser]"

# Run shared tests (regression check)
build/windows-x64-release/bin/Release/shared_tests.exe

# Pluginval (after full plugin build)
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"

# Clang-tidy (requires windows-ninja preset)
./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja
```

---

## Notes

- [P] tasks = different files or independent verification steps, no blocking dependencies
- [Story] label maps each task to a specific user story for traceability
- All new classes (`PresetBrowserButton`, `SavePresetButton`) are in anonymous namespace in controller.cpp - no ODR risk (same pattern as Disrumpo)
- `createComponentStateStream` delegates to host `getComponentState()` - do NOT re-implement serialization from scratch (FR-013, research.md decision #1)
- `loadComponentStateWithNotify` must exactly mirror `setComponentState()` deserialization order (data-model.md items 1-37) - voice routes (item 20) are read-and-discarded
- FX enable flags (items 21, 22, 24, 36) are stored as int8 - must convert to 0.0/1.0 before passing to `editParamWithNotify`
- Frame owns the overlay views - store raw pointers, null them in `willClose()`, never `delete` them
- All 13 tabs (including empty synth categories) must always be shown (FR-004) - empty categories show empty list without error
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XIII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
