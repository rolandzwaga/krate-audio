# Tasks: Krate Audio Monorepo Refactor

**Input**: Design documents from `/specs/044-monorepo-refactor/`
**Prerequisites**: plan.md (required), spec.md (required for user stories)

**Type**: Refactoring (no new DSP code - existing tests verify correctness)

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ IMPORTANT: Refactoring Workflow

This is a **refactoring task**, not new feature development. The workflow differs from test-first:

1. **Git History Preservation**: Use `git mv` for all file movements
2. **Incremental Verification**: Build and test after each major move
3. **Namespace/Include Updates**: Batch update with search-replace
4. **Existing Tests**: All 847+ existing tests must pass (no new tests needed)

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Create Directory Structure)

**Purpose**: Create new monorepo directory structure before moving files

- [x] T001 Create feature branch `044-monorepo-refactor` if not already on it
- [x] T002 [P] Create `dsp/` directory structure: `dsp/include/krate/dsp/{core,primitives,processors,systems,effects}/`
- [x] T003 [P] Create `dsp/tests/unit/` directory structure: `dsp/tests/unit/{core,primitives,processors,systems,effects}/`
- [x] T004 [P] Create `plugins/iterum/src/` directory structure: `plugins/iterum/src/{processor,controller}/`
- [x] T005 [P] Create `plugins/iterum/tests/` directory structure: `plugins/iterum/tests/{integration,approval}/`
- [x] T006 [P] Create `plugins/iterum/resources/` directory
- [x] T007 [P] Create `plugins/iterum/installers/` directory structure: `plugins/iterum/installers/{windows,linux,macos}/`
- [x] T008 [P] Create `plugins/iterum/docs/` directory

**Checkpoint**: Directory structure ready for file moves

---

## Phase 2: Foundational (DSP Library Migration)

**Purpose**: Move DSP source files to new location, preserving git history

**⚠️ CRITICAL**: Use `git mv` for all file movements to preserve history

### 2.1 Move DSP Source Files

- [x] T009 Move DSP Layer 0 (core): `git mv src/dsp/core/* dsp/include/krate/dsp/core/`
- [x] T010 Move DSP Layer 1 (primitives): `git mv src/dsp/primitives/* dsp/include/krate/dsp/primitives/`
- [x] T011 Move DSP Layer 2 (processors): `git mv src/dsp/processors/* dsp/include/krate/dsp/processors/`
- [x] T012 Move DSP Layer 3 (systems): `git mv src/dsp/systems/* dsp/include/krate/dsp/systems/`
- [x] T013 Move DSP Layer 4 (features→effects): `git mv src/dsp/features/* dsp/include/krate/dsp/effects/`

### 2.2 Move DSP Test Files

- [x] T014 Move DSP Layer 0 tests: `git mv tests/unit/core/* dsp/tests/unit/core/`
- [x] T015 Move DSP Layer 1 tests: `git mv tests/unit/primitives/* dsp/tests/unit/primitives/`
- [x] T016 Move DSP Layer 2 tests: `git mv tests/unit/processors/* dsp/tests/unit/processors/`
- [x] T017 Move DSP Layer 3 tests: `git mv tests/unit/systems/* dsp/tests/unit/systems/`
- [x] T018 Move DSP Layer 4 tests: `git mv tests/unit/features/* dsp/tests/unit/effects/`

### 2.3 Update DSP Namespaces

- [x] T019 Update namespace declarations in all DSP headers: `Iterum::DSP` → `Krate::DSP` in `dsp/include/krate/dsp/**/*.h`
- [x] T020 Update namespace usages in all DSP sources: `Iterum::DSP` → `Krate::DSP` in `dsp/include/krate/dsp/**/*.cpp` (if any)
- [x] T021 Update namespace usages in all DSP tests: `Iterum::DSP` → `Krate::DSP` in `dsp/tests/unit/**/*.cpp`

### 2.4 Update DSP Include Paths

- [x] T022 Update include paths in DSP headers: `#include "dsp/..."` → `#include <krate/dsp/...>` in `dsp/include/krate/dsp/**/*.h`
- [x] T023 Update include paths in DSP tests: `#include "../../src/dsp/..."` → `#include <krate/dsp/...>` in `dsp/tests/unit/**/*.cpp`

### 2.5 Create DSP CMakeLists.txt

- [x] T024 Create `dsp/CMakeLists.txt` defining KrateDSP static library target with include directories

### 2.6 Create DSP Tests CMakeLists.txt

- [x] T025 Create `dsp/tests/CMakeLists.txt` for DSP unit tests

**Checkpoint**: DSP library files moved and updated (build will fail until Phase 3 complete)

---

## Phase 3: Foundational (Plugin Migration)

**Purpose**: Move plugin-specific files to new location, preserving git history

### 3.1 Move Plugin Source Files

- [x] T026 Move processor source: `git mv src/processor/* plugins/iterum/src/processor/`
- [x] T027 Move controller source: `git mv src/controller/* plugins/iterum/src/controller/`
- [x] T028 Move entry point: `git mv src/entry.cpp plugins/iterum/src/`
- [x] T029 Move plugin IDs: `git mv src/plugin_ids.h plugins/iterum/src/`
- [x] T030 Move version header: N/A (generated file, not in git)

### 3.2 Move Plugin Resources and Assets

- [x] T031 Move resources: `git mv resources/* plugins/iterum/resources/`
- [x] T032 Move Windows installer: `git mv installers/windows/* plugins/iterum/installers/windows/`
- [x] T033 Move Linux installer: `git mv installers/linux/* plugins/iterum/installers/linux/`
- [x] T034 Move macOS installer: N/A (directory does not exist)
- [x] T035 Move docs: `git mv docs/* plugins/iterum/docs/`

### 3.3 Move Plugin Metadata

- [x] T036 Move version.json: `git mv version.json plugins/iterum/`
- [x] T037 Move CHANGELOG.md: `git mv CHANGELOG.md plugins/iterum/`

### 3.4 Move Plugin Tests

- [x] T038 Move integration tests: N/A (directory empty)
- [x] T039 Move approval tests: `git mv tests/regression/* plugins/iterum/tests/approval/`
- [x] T039a Identify and move any plugin-related unit tests (controller, parameters, preset, processor, ui, vst)

### 3.5 Update Plugin Include Paths

- [x] T040 Update include paths in processor: `#include "dsp/..."` → `#include <krate/dsp/...>`
- [x] T041 Update include paths in all plugin source: `#include "dsp/..."` → `#include <krate/dsp/...>`

### 3.6 Update Plugin Namespace Usages

- [x] T042 Update namespace usages: `DSP::` → `Krate::DSP::` in plugin source
- [x] T043 Update using declarations: `using DSP::` → `using Krate::DSP::`
- [x] T044 Update namespace usages in plugin tests: `Iterum::DSP` → `Krate::DSP`

### 3.7 Create Plugin CMakeLists.txt

- [x] T045 Create `plugins/iterum/CMakeLists.txt` defining Iterum plugin target, linking KrateDSP

### 3.8 Create Plugin Tests CMakeLists.txt

- [x] T046 Create `plugins/iterum/tests/CMakeLists.txt` for unit and approval tests

**Checkpoint**: Plugin files moved and updated (build will fail until Phase 4 complete)

---

## Phase 4: Foundational (CMake Restructuring)

**Purpose**: Update root CMake configuration for monorepo structure

### 4.1 Update Root CMakeLists.txt

- [x] T047 Update root `CMakeLists.txt` to:
  - Configure VST3 SDK from `extern/vst3sdk/` (shared by all plugins)
  - Add `dsp/` subdirectory (builds KrateDSP library)
  - Add `plugins/iterum/` subdirectory (builds Iterum plugin)

### 4.2 Update CMake Presets

- [x] T048 Update `CMakePresets.json` - no changes needed, preset names preserved

### 4.3 Clean Up Old Directories

- [x] T049 Remove empty `src/dsp/` directory after verification
- [x] T050 Remove empty `tests/unit/` directory completely (all DSP and plugin tests have been moved)
- [x] T051 Remove empty `src/processor/`, `src/controller/` directories, and entire `src/` after verification
- [x] T052 Update `tests/CMakeLists.txt` to reference new structure (test_helpers only)

**Checkpoint**: CMake structure complete, ready for build verification

---

## Phase 5: User Story 1 - Developer Builds Plugin After Refactor (Priority: P1) 🎯 MVP

**Goal**: Verify that plugin builds and all tests pass after refactoring

**Independent Test**: Run `cmake --preset windows-x64-release && cmake --build --preset windows-x64-release` and verify Iterum.vst3 output is functional

### 5.1 Build Verification

- [ ] T053 [US1] Configure CMake: `cmake --preset windows-x64-release`
- [ ] T054 [US1] Build all targets: `cmake --build --preset windows-x64-release` and measure build time (target: under 5 minutes for SC-001)
- [ ] T055 [US1] Fix any compilation errors in DSP library
- [ ] T056 [US1] Fix any compilation errors in plugin

### 5.2 Test Verification

- [ ] T057 [US1] Run all unit tests: `ctest --preset windows-x64-release`
- [ ] T058 [US1] Verify all 847+ tests pass (no regressions)
- [ ] T059 [US1] Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Iterum.vst3"`

### 5.3 Git History Verification

- [ ] T060 [US1] Verify git history preserved for key DSP files: `git log --follow dsp/include/krate/dsp/primitives/delay_line.h`
- [ ] T061 [US1] Verify git blame works for refactored files

### 5.4 Cross-Platform Verification (FR-024)

- [ ] T061a [US1] Push branch and verify CI builds pass on all platforms (Windows, macOS, Linux)
- [ ] T061b [US1] If CI fails on any platform, fix issues before proceeding

### 5.5 Commit

- [ ] T062 [US1] **Commit completed monorepo restructure**: "refactor: restructure to Krate Audio monorepo"

**Checkpoint**: User Story 1 complete - plugin builds and tests pass

---

## Phase 6: User Story 2 - Developer Modifies DSP Library (Priority: P2)

**Goal**: CI detects DSP changes and rebuilds all plugins

**Independent Test**: Modify a file in `dsp/` and verify CI builds all plugins

### 6.1 Update CI Workflow

- [ ] T063 [US2] Update `.github/workflows/ci.yml` with path-based trigger filters:
  - Changes to `dsp/**` trigger all plugin builds
  - Changes to `plugins/iterum/**` trigger only Iterum build
  - Changes to `extern/**` trigger all builds
  - Changes to root CMake files trigger all builds

### 6.2 Test CI Configuration

- [ ] T064 [US2] Create test commit touching `dsp/` file to verify all plugins rebuild
- [ ] T065 [US2] Create test commit touching `plugins/iterum/` file to verify only Iterum rebuilds

### 6.3 Commit

- [ ] T066 [US2] **Commit CI workflow updates**: "ci: add path-based plugin detection"

**Checkpoint**: User Story 2 complete - CI path detection works

---

## Phase 7: User Story 3 - Developer Releases a Plugin (Priority: P2)

**Goal**: Release workflow builds only the selected plugin and creates properly tagged release

**Independent Test**: Trigger release workflow with `plugin: iterum` and verify only Iterum artifacts are produced

### 7.1 Update Release Workflow

- [ ] T067 [US3] Update `.github/workflows/release.yml` with:
  - `workflow_dispatch` trigger with `plugin` dropdown parameter
  - Read version from `plugins/<plugin>/version.json`
  - Create tag in format `<plugin>/v<version>` (e.g., `iterum/v0.8.0`)
  - Build only the selected plugin
  - Upload only that plugin's installers

### 7.2 Update Installer Paths

- [ ] T068 [US3] Update Windows installer script to use new paths: `plugins/iterum/installers/windows/setup.iss`
- [ ] T069 [US3] Update macOS installer script to use new paths: `plugins/iterum/installers/macos/create-pkg.sh`
- [ ] T070 [US3] Update Linux installer script to use new paths: `plugins/iterum/installers/linux/create-tarball.sh`

### 7.3 Commit

- [ ] T071 [US3] **Commit release workflow updates**: "ci: parameterize release workflow for plugin selection"

**Checkpoint**: User Story 3 complete - release workflow parameterized

---

## Phase 8: User Story 4 - Developer Adds a New Plugin (Priority: P3)

**Goal**: Validate architecture supports adding new plugins easily

**Independent Test**: Creating a minimal new plugin in `plugins/test-plugin/` that links KrateDSP builds successfully

### 8.1 Validate Architecture

- [ ] T072 [US4] Document how to add a new plugin in `plugins/README.md`:
  - Create directory structure
  - Create CMakeLists.txt linking KrateDSP
  - Add to root CMakeLists.txt
  - Configure CI/release workflows

### 8.2 Optional Validation (Minimal Test Plugin)

- [ ] T073 [US4] (Optional) Create minimal `plugins/test-plugin/` with:
  - Basic CMakeLists.txt linking KrateDSP
  - Minimal source file using `Krate::DSP::` classes
  - Verify it compiles
  - Remove after validation (do not commit)

### 8.3 Commit

- [ ] T074 [US4] **Commit plugin documentation**: "docs: add new plugin creation guide"

**Checkpoint**: User Story 4 complete - architecture validated for future plugins

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Final cleanup and documentation updates

- [ ] T075 [P] Update root README.md with new project structure
- [ ] T076 [P] Update CLAUDE.md with new file paths if needed
- [ ] T077 [P] Update any hardcoded paths in tools/ scripts
- [ ] T078 Remove any leftover empty directories from old structure
- [ ] T079 Verify no broken symlinks or references remain

**Checkpoint**: Repository clean and documented

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 10.1 Architecture Documentation Update

- [ ] T080 **Update ARCHITECTURE.md** with new monorepo structure:
  - Update directory tree to reflect `dsp/` and `plugins/iterum/` structure
  - Update include path documentation (`#include <krate/dsp/...>`)
  - Update namespace documentation (`Krate::DSP::`)
  - Document KrateDSP as shared library target

### 10.2 Final Commit

- [ ] T081 **Commit ARCHITECTURE.md updates**
- [ ] T082 Verify all spec work is committed to feature branch

**Checkpoint**: ARCHITECTURE.md reflects new monorepo structure

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T083 **Review ALL FR-xxx requirements** from spec.md against implementation:
  - FR-001 through FR-024 (directory structure, namespaces, CMake, CI/CD, git history, compatibility)
- [ ] T084 **Review ALL SC-xxx success criteria**:
  - SC-001: Build time under 5 minutes
  - SC-002: All 847+ tests pass
  - SC-003: Pluginval passes at level 5
  - SC-004: CI build time reduced for plugin-only changes
  - SC-005: Adding new plugin requires only directory + CMakeLists.txt
  - SC-006: 100% git history preserved

### 11.2 Fill Compliance Table in spec.md

- [ ] T085 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T086 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

- [ ] T087 **All self-check questions answered "no"** (or gaps documented honestly):
  1. Did I change ANY test threshold from what the spec originally required?
  2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
  3. Did I remove ANY features from scope without telling the user?
  4. Would the spec author consider this "done"?
  5. If I were the user, would I feel cheated?

**Checkpoint**: Honest assessment complete

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Commit

- [ ] T088 **Commit all spec work** to feature branch
- [ ] T089 **Verify all tests pass** one final time

### 12.2 Completion Claim

- [ ] T090 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup) ─────────────────────────────────────────┐
                                                          │
Phase 2 (DSP Migration) ──────────────────────────────────┤
        ↓                                                 │
Phase 3 (Plugin Migration) ───────────────────────────────┤ FOUNDATIONAL
        ↓                                                 │  (Sequential)
Phase 4 (CMake Restructuring) ────────────────────────────┘
        ↓
Phase 5 (US1: Build Verification) ─── MVP CHECKPOINT ────┐
        ↓                                                 │
Phase 6 (US2: CI Path Detection) ─────────────────────────┤ USER STORIES
        ↓                                                 │  (Sequential)
Phase 7 (US3: Release Workflow) ──────────────────────────┤
        ↓                                                 │
Phase 8 (US4: New Plugin Validation) ─────────────────────┘
        ↓
Phase 9-12 (Polish & Verification) ──────────────────────── COMPLETION
```

### Within Each Phase

- **File Moves (T009-T018, T026-T039)**: Can be parallelized within their groups
- **Namespace Updates (T019-T021, T042-T044)**: Must follow file moves
- **Include Updates (T022-T023, T040-T041)**: Must follow file moves
- **CMake Changes (T024-T025, T045-T048)**: Must follow all file moves
- **Build Verification (T053-T059)**: Must follow CMake changes
- **CI/CD Updates (T063-T071)**: Can proceed after build verification

### Parallel Opportunities

- T002-T008: All directory creation can run in parallel
- T009-T013: DSP source moves can run in parallel (different layers)
- T014-T018: DSP test moves can run in parallel
- T026-T030: Plugin source moves can run in parallel
- T031-T035: Plugin resource moves can run in parallel
- T075-T077: Polish documentation can run in parallel

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phases 1-4: Setup + Foundational migrations
2. Complete Phase 5: Build verification (US1)
3. **STOP and VALIDATE**: Plugin builds, all tests pass
4. This is a deployable state - refactoring complete

### Incremental Delivery

1. Phases 1-5 → Build works, tests pass (MVP!)
2. Phase 6 → CI path detection works
3. Phase 7 → Release workflow parameterized
4. Phase 8 → Architecture validated
5. Phases 9-12 → Polish and completion

---

## Notes

- **Git History Critical**: Always use `git mv`, never copy-and-delete
- **Namespace Search Pattern**: `grep -r "Iterum::DSP" --include="*.h" --include="*.cpp"`
- **Include Search Pattern**: `grep -r '#include "dsp/' --include="*.h" --include="*.cpp"`
- **Build Often**: After each major phase, attempt a build to catch issues early
- **~100 files to move**: Be systematic, verify each layer before proceeding
- **847+ tests**: All must pass - no exceptions
- **Cross-platform**: Verify builds work on all platforms in CI before completion
