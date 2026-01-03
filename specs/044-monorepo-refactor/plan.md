# Implementation Plan: Krate Audio Monorepo Refactor

**Branch**: `044-monorepo-refactor` | **Date**: 2026-01-03 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/044-monorepo-refactor/spec.md`

## Summary

Restructure the Iterum VST3 plugin repository into a monorepo supporting multiple plugins with a shared KrateDSP library. The refactoring involves moving DSP code to `dsp/`, plugin code to `plugins/iterum/`, changing namespace from `Iterum::DSP` to `Krate::DSP`, updating include paths, restructuring CMake, and updating CI/CD workflows for plugin-specific builds and releases.

## Technical Context

**Language/Version**: C++20, CMake 3.20+
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12, Catch2 (testing)
**Storage**: N/A (no database/persistence changes)
**Testing**: Catch2 via CTest - existing test suite must pass after refactor
**Target Platform**: Windows, macOS, Linux (unchanged)
**Project Type**: Monorepo with shared library + plugin targets
**Performance Goals**: N/A (refactoring, no performance changes)
**Constraints**: Git history must be preserved; all existing tests must pass
**Scale/Scope**: ~100 source files to move, ~847 tests to verify

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle VII (Project Structure & Build System)**:
- [x] Using Modern CMake 3.20+ with target-based configuration
- [x] Directory structure will follow documented patterns
- [x] Headers will use `#include <krate/dsp/...>` style
- [x] External dependencies in `extern/` with pinned versions
- [x] Build configurations support Debug, Release, RelWithDebInfo

**Principle VI (Cross-Platform Compatibility)**:
- [x] No platform-specific changes introduced
- [x] CI/CD continues to build on Windows, macOS, Linux

**Principle XII (Test-First Development)**:
- [x] Existing tests will verify refactoring correctness
- [x] No new DSP features requiring new tests

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include verification that all existing tests pass
- [x] Build must succeed before running tests
- [x] Each major phase will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] N/A - No new classes being created; only moving and renaming existing code

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: N/A for refactoring - no new types being created*

This is a refactoring task that moves existing code without creating new classes or functions. ODR prevention is not applicable because:
1. No new classes/structs are being created
2. Existing code is being relocated, not duplicated
3. Namespace is being changed uniformly (`Iterum::DSP` → `Krate::DSP`)

### Files to be Moved (not duplicated)

| Current Location | New Location | Action |
|------------------|--------------|--------|
| `src/dsp/core/` | `dsp/include/krate/dsp/core/` | git mv + namespace change |
| `src/dsp/primitives/` | `dsp/include/krate/dsp/primitives/` | git mv + namespace change |
| `src/dsp/processors/` | `dsp/include/krate/dsp/processors/` | git mv + namespace change |
| `src/dsp/systems/` | `dsp/include/krate/dsp/systems/` | git mv + namespace change |
| `src/dsp/features/` | `dsp/include/krate/dsp/effects/` | git mv + namespace change |
| `tests/unit/core/` | `dsp/tests/unit/core/` | git mv |
| `tests/unit/primitives/` | `dsp/tests/unit/primitives/` | git mv |
| `tests/unit/processors/` | `dsp/tests/unit/processors/` | git mv |
| `tests/unit/systems/` | `dsp/tests/unit/systems/` | git mv |
| `tests/unit/features/` | `dsp/tests/unit/effects/` | git mv |
| `src/processor/` | `plugins/iterum/src/processor/` | git mv |
| `src/controller/` | `plugins/iterum/src/controller/` | git mv |
| `src/entry.cpp` | `plugins/iterum/src/entry.cpp` | git mv |
| `src/plugin_ids.h` | `plugins/iterum/src/plugin_ids.h` | git mv |
| `src/version.h` | `plugins/iterum/src/version.h` | git mv |
| `resources/` | `plugins/iterum/resources/` | git mv |
| `installers/` | `plugins/iterum/installers/` | git mv |
| `docs/` | `plugins/iterum/docs/` | git mv |

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No new types are being created. Namespace change is uniform replacement. Git mv preserves history and prevents duplication.

## Dependency API Contracts (Principle XIV Extension)

*N/A for refactoring - no new API calls being introduced*

All existing API calls remain the same; only the namespace prefix changes:
- `Iterum::DSP::DelayLine` → `Krate::DSP::DelayLine`
- `Iterum::DSP::Biquad` → `Krate::DSP::Biquad`
- etc.

## Layer 0 Candidate Analysis

*N/A for refactoring - no new utility functions being created*

## Higher-Layer Reusability Analysis

*N/A for refactoring - this IS the reusability enablement*

The entire purpose of this refactoring is to enable reusability:
- KrateDSP library becomes consumable by future plugins
- Plugins link against shared library
- No sibling features exist yet; Iterum is the first plugin

## Project Structure

### Documentation (this feature)

```text
specs/044-monorepo-refactor/
├── spec.md              # Feature specification
├── plan.md              # This file
├── checklists/
│   └── requirements.md  # Quality checklist
└── tasks.md             # Implementation tasks (after /speckit.tasks)
```

### Source Code (after refactor)

```text
krate-audio/                      # Repository root
├── dsp/                          # KrateDSP library
│   ├── CMakeLists.txt            # Defines KrateDSP target
│   ├── include/
│   │   └── krate/
│   │       └── dsp/
│   │           ├── core/         # Layer 0: utilities
│   │           ├── primitives/   # Layer 1: delay lines, filters, etc.
│   │           ├── processors/   # Layer 2: saturation, dynamics, etc.
│   │           ├── systems/      # Layer 3: feedback network, etc.
│   │           └── effects/      # Layer 4: delay modes
│   └── tests/
│       └── unit/                 # DSP unit tests
│           ├── core/
│           ├── primitives/
│           ├── processors/
│           ├── systems/
│           └── effects/
├── plugins/
│   └── iterum/
│       ├── CMakeLists.txt        # Defines Iterum plugin, links KrateDSP
│       ├── version.json
│       ├── CHANGELOG.md
│       ├── src/
│       │   ├── processor/
│       │   ├── controller/
│       │   ├── entry.cpp
│       │   ├── plugin_ids.h
│       │   └── version.h
│       ├── resources/
│       │   ├── editor.uidesc
│       │   └── presets/
│       ├── installers/
│       │   ├── windows/
│       │   ├── linux/
│       │   └── macos/
│       ├── docs/
│       │   └── index.html
│       └── tests/
│           ├── integration/
│           └── approval/
├── extern/
│   └── vst3sdk/                  # Shared by all plugins
├── tools/                        # Shared tools (preset generator, etc.)
├── CMakeLists.txt                # Root: configures SDK, adds subdirectories
├── CMakePresets.json
└── .github/
    └── workflows/
        ├── ci.yml                # Path-based plugin detection
        └── release.yml           # Parameterized with plugin dropdown
```

**Structure Decision**: Flat monorepo with `dsp/` and `plugins/` at root level. No `src/` wrapper. VST3 SDK at root `extern/` shared by all plugins.

## Complexity Tracking

No Constitution violations. This refactoring follows all principles:
- Principle VII: Modern CMake, proper directory structure
- Principle VI: No platform-specific changes
- Principle XII: Existing tests verify correctness
