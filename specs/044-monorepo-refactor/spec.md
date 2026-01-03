# Feature Specification: Krate Audio Monorepo Refactor

**Feature Branch**: `044-monorepo-refactor`
**Created**: 2026-01-03
**Status**: Draft
**Input**: User description: "Refactor Iterum repository to Krate Audio monorepo structure supporting multiple plugins sharing a common DSP library"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Developer Builds Plugin After Refactor (Priority: P1)

A developer clones the refactored repository and builds the Iterum plugin. The build process should work identically to before, producing the same plugin binary with unchanged functionality.

**Why this priority**: This is the core acceptance criterion - if the build doesn't work after refactoring, nothing else matters.

**Independent Test**: Can be fully tested by running `cmake --preset windows-x64-release && cmake --build --preset windows-x64-release` and verifying the Iterum.vst3 output is functional.

**Acceptance Scenarios**:

1. **Given** a fresh clone of the refactored repository, **When** the developer runs the standard build commands, **Then** the Iterum.vst3 plugin is produced successfully
2. **Given** the built plugin, **When** loaded in a VST3 host, **Then** all 11 delay modes function identically to before
3. **Given** the refactored codebase, **When** running the full test suite, **Then** all existing tests pass

---

### User Story 2 - Developer Modifies DSP Library (Priority: P2)

A developer makes changes to the shared KrateDSP library. The CI system detects the change and rebuilds all plugins that depend on it.

**Why this priority**: The monorepo's value comes from code sharing; this validates that shared code changes propagate correctly.

**Independent Test**: Can be tested by modifying a file in `dsp/` and verifying CI builds all plugins.

**Acceptance Scenarios**:

1. **Given** a change to any file in `dsp/`, **When** CI runs, **Then** all plugins are rebuilt and tested
2. **Given** a change only to `plugins/iterum/`, **When** CI runs, **Then** only Iterum is rebuilt (not other future plugins)

---

### User Story 3 - Developer Releases a Plugin (Priority: P2)

A developer triggers a release for a specific plugin (e.g., Iterum). The release workflow builds only that plugin, creates a tagged release, and uploads installers.

**Why this priority**: Independent plugin releases are essential for the monorepo to scale to multiple plugins.

**Independent Test**: Can be tested by triggering the release workflow with `plugin: iterum` and verifying only Iterum artifacts are produced.

**Acceptance Scenarios**:

1. **Given** the release workflow is triggered with `plugin: iterum`, **When** the workflow completes, **Then** a GitHub release is created with tag `iterum/v{version}`
2. **Given** the release, **When** viewing the release assets, **Then** only Iterum installers are attached (Windows .exe, macOS .pkg, Linux .tar.gz)
3. **Given** version.json in `plugins/iterum/` contains version "0.8.0", **When** release is triggered, **Then** the tag is `iterum/v0.8.0`

---

### User Story 4 - Developer Adds a New Plugin (Priority: P3)

A future developer adds a new plugin to the monorepo. They can reuse the KrateDSP library by linking against it in CMake.

**Why this priority**: This validates the monorepo architecture supports growth, but is not immediately needed for Iterum.

**Independent Test**: Can be tested by creating a minimal new plugin in `plugins/test-plugin/` that links KrateDSP and builds successfully.

**Acceptance Scenarios**:

1. **Given** a new plugin directory `plugins/new-plugin/` with CMakeLists.txt, **When** linking against KrateDSP target, **Then** the plugin builds and can use `Krate::DSP::` classes
2. **Given** a new plugin is added, **When** CI runs on changes to that plugin, **Then** only the new plugin is rebuilt

---

### Edge Cases

- What happens when a developer has uncommitted changes during refactor? (Answer: Refactor should be done on clean working tree)
- How does the system handle circular dependencies between plugins? (Answer: Not supported; each plugin depends only on shared libraries, not other plugins)
- What if VST3 SDK version needs to differ per plugin? (Answer: Not supported; all plugins must use the same SDK version from root `extern/`)

## Requirements *(mandatory)*

### Functional Requirements

**Directory Structure**

- **FR-001**: Repository MUST have `dsp/` directory at root containing the KrateDSP library
- **FR-002**: Repository MUST have `plugins/iterum/` directory containing the Iterum plugin source
- **FR-003**: Repository MUST have `extern/vst3sdk/` at root level shared by all plugins
- **FR-004**: DSP source files MUST be in `dsp/include/krate/dsp/` with layer subdirectories (core, primitives, processors, systems, effects)
- **FR-005**: DSP unit tests MUST be in `dsp/tests/unit/`
- **FR-006**: Plugin-specific tests (integration, approval) MUST be in `plugins/iterum/tests/`

**Namespace Refactoring**

- **FR-007**: All DSP code MUST use `Krate::DSP::` namespace instead of `Iterum::DSP::`
- **FR-008**: Include paths MUST change from `#include "dsp/..."` to `#include <krate/dsp/...>`

**CMake Structure**

- **FR-009**: Root CMakeLists.txt MUST define `KrateDSP` as a static library target
- **FR-010**: Plugin CMakeLists.txt MUST link against `KrateDSP` target
- **FR-011**: VST3 SDK MUST be configured once at root level and available to all plugins
- **FR-012**: CMakePresets.json MUST support existing preset names (windows-x64-debug, etc.)

**CI/CD**

- **FR-013**: CI workflow MUST detect changed paths and build only affected plugins
- **FR-014**: Changes to `dsp/` MUST trigger rebuild of all plugins
- **FR-015**: Changes to `plugins/iterum/` MUST trigger rebuild of only Iterum
- **FR-016**: Release workflow MUST be triggered via `workflow_dispatch` with `plugin` dropdown parameter
- **FR-017**: Release workflow MUST read version from `plugins/<plugin>/version.json`
- **FR-018**: Release workflow MUST create tag in format `<plugin>/v<version>` (e.g., `iterum/v0.8.0`)
- **FR-019**: Release workflow MUST build and upload installers only for the selected plugin

**Git History**

- **FR-020**: Git history MUST be preserved using `git mv` for file movements
- **FR-021**: Refactoring MUST NOT break git blame for existing files

**Compatibility**

- **FR-022**: All existing tests MUST pass after refactoring
- **FR-023**: Plugin functionality MUST be unchanged (all 11 delay modes work identically)
- **FR-024**: Cross-platform builds MUST continue to work (Windows, macOS, Linux)

### Key Entities

- **KrateDSP Library**: Shared DSP code (Layers 0-4) consumed by all plugins
- **Plugin**: A VST3/AU plugin that links against KrateDSP (Iterum is the first)
- **Feature Branch Tag**: A git tag in format `<plugin>/v<version>` identifying a plugin release

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Developer can build Iterum plugin from fresh clone in under 5 minutes (same as before)
- **SC-002**: All 847+ existing unit tests pass without modification (except namespace changes)
- **SC-003**: Iterum plugin passes pluginval at strictness level 5 (unchanged from before)
- **SC-004**: CI build time for plugin-only changes is reduced by limiting scope to affected plugins (qualitative: only affected plugin rebuilds instead of full monorepo)
- **SC-005**: Adding a new plugin requires only creating a directory and CMakeLists.txt (no changes to shared infrastructure)
- **SC-006**: 100% of git history is preserved for all source files (verified via git log)

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Repository is on a clean working tree before refactoring begins
- All current tests pass before starting refactor
- Developer has CMake 3.20+ and appropriate compilers installed
- GitHub Actions runners have sufficient permissions for releases

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that will be moved:**

| Component | Current Location | New Location |
|-----------|------------------|--------------|
| DSP Layer 0-4 | `src/dsp/` | `dsp/include/krate/dsp/` |
| DSP unit tests | `tests/unit/` (dsp-related) | `dsp/tests/unit/` |
| Plugin source | `src/processor/`, `src/controller/`, etc. | `plugins/iterum/src/` |
| Plugin tests | `tests/unit/` (plugin-related), `tests/integration/`, `tests/approval/` | `plugins/iterum/tests/` |
| VST3 SDK | `extern/vst3sdk/` | `extern/vst3sdk/` (unchanged, but now explicitly shared) |
| Resources | `resources/` | `plugins/iterum/resources/` |
| Installers | `installers/` | `plugins/iterum/installers/` |
| Landing page | `docs/` | `plugins/iterum/docs/` |
| Tools | `tools/` | `tools/` (unchanged, shared) |

**Initial codebase search for key terms:**

```bash
# Identify all files using Iterum::DSP namespace
grep -r "namespace Iterum" src/dsp/
grep -r "Iterum::DSP" src/

# Identify include patterns to update
grep -r '#include "dsp/' src/
```

**Search Results Summary**: All DSP files use `Iterum::DSP` namespace and relative includes. These will be updated during refactor.

### Forward Reusability Consideration

**Sibling features at same layer** (if known):
- Future synth plugin (discussed but not yet specified)
- Other potential effect plugins

**Potential shared components** (preliminary, refined in plan.md):
- Entire KrateDSP library is designed for reuse
- CI/CD workflows are parameterized for multiple plugins
- CMake infrastructure supports adding new plugins

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| FR-003 | | |
| FR-004 | | |
| FR-005 | | |
| FR-006 | | |
| FR-007 | | |
| FR-008 | | |
| FR-009 | | |
| FR-010 | | |
| FR-011 | | |
| FR-012 | | |
| FR-013 | | |
| FR-014 | | |
| FR-015 | | |
| FR-016 | | |
| FR-017 | | |
| FR-018 | | |
| FR-019 | | |
| FR-020 | | |
| FR-021 | | |
| FR-022 | | |
| FR-023 | | |
| FR-024 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |

**Status Key:**
- MET: Requirement fully satisfied with test evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [ ] All FR-xxx requirements verified against implementation
- [ ] All SC-xxx success criteria measured and documented
- [ ] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: [COMPLETE / NOT COMPLETE / PARTIAL]

**If NOT COMPLETE, document gaps:**
- [Gap 1: FR-xxx not met because...]
- [Gap 2: SC-xxx achieves X instead of Y because...]

**Recommendation**: [What needs to happen to achieve completion]
