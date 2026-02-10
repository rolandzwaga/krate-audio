# Iterum Project Overview

## Purpose
Monorepo for Krate Audio VST3 plugins. Primary plugin: Iterum (delay plugin).

## Tech Stack
- C++20, CMake 3.20+, VST3 SDK, VSTGUI
- Catch2 for testing, pffft for SIMD FFT
- Windows/macOS/Linux cross-platform

## Structure
- `dsp/` - Shared KrateDSP library (5-layer architecture: core/primitives/processors/systems/effects)
- `plugins/iterum/` - Iterum delay plugin (src/, tests/, resources/)
- `extern/vst3sdk/`, `extern/pffft/` - External dependencies
- `specs/` - Feature specifications
- `.specify/` - Spec templates and scripts
- `tools/` - Dev tools (pluginval, clang-tidy scripts)

## Code Style
- Classes: PascalCase, Functions: camelCase, Members: trailing underscore, Constants: kPascalCase
- Namespaces: Krate::DSP, Modern C++20 (smart pointers, RAII, constexpr)
- Parameter IDs: k{Mode}{Parameter}Id pattern

## Key Commands (Windows)
```bash
# Build
"C:/Program Files/CMake/bin/cmake.exe" --preset windows-x64-release
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release

# DSP tests
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# All tests
ctest --test-dir build/windows-x64-release -C Release --output-on-failure

# Clang-tidy
./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Iterum.vst3"
```

## Workflow
1. Write failing test
2. Implement to pass
3. Fix compiler warnings (zero warnings policy)
4. Verify tests pass
5. Run pluginval (if plugin code changed)
6. Run clang-tidy
7. Commit (never amend unless explicitly asked)
