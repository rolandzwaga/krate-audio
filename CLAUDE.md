# CLAUDE.md - VST Plugin Development Guidelines

This file provides guidance for AI assistants working on this VST3 plugin project. All code contributions must comply with the project constitution at `.specify/memory/constitution.md`.

## Project Overview

This is a **monorepo** for Krate Audio plugins, featuring:
- **KrateDSP**: Shared DSP library at `dsp/` (namespace: `Krate::DSP`)
- **Iterum**: Delay plugin at `plugins/iterum/`
- **Steinberg VST3 SDK** (not JUCE or other frameworks)
- **VSTGUI** for user interface
- **Modern C++20**
- **CMake 3.20+** build system

### Monorepo Structure
```
├── dsp/                      # Shared KrateDSP library
│   ├── include/krate/dsp/    # Public headers (use <krate/dsp/...>)
│   └── tests/                # DSP unit tests
├── plugins/iterum/           # Iterum plugin
│   ├── src/                  # Plugin source
│   ├── tests/                # Plugin tests
│   └── resources/            # UI, presets, installers
├── tests/                    # Shared test infrastructure
├── extern/vst3sdk/           # VST3 SDK (shared)
└── extern/pffft/             # SIMD-optimized FFT (BSD, marton78 fork)
```

## Critical Rules (Non-Negotiable)

### No Amending Commits

**NEVER use `git commit --amend`.** Always create a new commit. Amending is ONLY allowed when the user explicitly asks for it.

### No Background Agents

**NEVER run Task agents in the background.** Always use `run_in_background: false` (or omit the parameter). The user needs to monitor agent progress in real-time. Background execution obscures what's happening and is forbidden.

### Windows Path Workaround

For Edit/Glob/Grep/Read tools: Use Windows backslash paths (`C:\path\file.txt`). Expand `~` to full path (e.g., `C:\Users\name`). Does NOT apply to Bash.

### Cross-Platform Requirement

**This plugin MUST be fully cross-platform (Windows, macOS, Linux).** Platform-specific UI solutions are FORBIDDEN.

- NEVER use Win32 APIs, Cocoa/AppKit, or native popups for UI
- ALWAYS use VSTGUI's cross-platform abstractions (COptionMenu, CFileSelector, etc.)
- If a VSTGUI feature doesn't work, the fix must also use VSTGUI
- Platform-specific code is ONLY acceptable for debug logging (guarded by `#ifdef`) or documented bug workarounds with user approval

### Build-Before-Test Discipline

**NO TESTS WITHOUT A CLEAN BUILD. PERIOD.**

After ANY code changes:
1. Build: `cmake --build build --config Release --target <target>`
2. Check for compilation errors and warnings
3. Fix errors and warnings BEFORE running tests
4. Only then run tests

If tests don't appear/run, the FIRST action is to check build output for errors, not blame CMake cache.

## Real-Time Audio Thread Safety

The audio thread has **hard real-time constraints**. No allocations, locks, exceptions, or I/O on audio thread. See `dsp-architecture` skill for details.

## VST3 Architecture Separation

Processor and Controller are **separate components**:

```cpp
// Processor (audio thread) - plugins/iterum/src/processor/
class Processor : public Steinberg::Vst::AudioEffect { /* Audio ONLY */ };

// Controller (UI thread) - plugins/iterum/src/controller/
class Controller : public Steinberg::Vst::EditControllerEx1 { /* UI ONLY */ };
```

**Rules:** Never cross-include headers. Use `IMessage` for communication. Processor must work without controller. State flows: Host → Processor → Controller.

## Parameter Handling

All parameters at VST boundary are **normalized (0.0 to 1.0)**. Denormalize in `processParameterChanges()`.

## Cross-Platform Compatibility

See constitution section "Cross-Platform Compatibility" for complete reference. Key points:

**NaN Detection:** `-ffast-math` breaks `std::isnan()`. Use bit manipulation with `-fno-fast-math` on source file.

**Floating-Point:** MSVC/Clang differ at 7th-8th decimal. Use `Approx().margin()` in tests, `std::setprecision(6)` in approval tests.

**Denormals:** Enable FTZ/DAZ on x86: `_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);`

**Narrowing:** Clang errors on narrowing in brace init. Use designated initializers: `BlockContext{.sampleRate = 44100.0, .tempoBPM = 120.0}`.

**SIMD:** SSE needs 16-byte alignment, AVX needs 32-byte. Use `alignas()` or `_mm_malloc`.

**Atomics:** Only `std::atomic_flag` is guaranteed lock-free. Verify with `is_lock_free()`.

## Code Style

### Naming Conventions

```cpp
class AudioProcessor {};           // Classes: PascalCase
void processAudio();               // Functions: camelCase
float sampleRate_;                 // Members: trailing underscore
constexpr float kDefaultGain;      // Constants: kPascalCase
namespace Krate::DSP {}            // Namespaces: PascalCase
enum { kBypassId = 0, kGainId };   // Parameter IDs: kNameId
```

### Parameter ID Naming Convention

All parameter IDs in `plugin_ids.h` MUST follow this pattern:

**Pattern:** `k{Mode}{Parameter}Id`

- `Mode`: The delay mode prefix (Granular, Spectral, Shimmer, Tape, BBD, Digital, PingPong, Reverse, MultiTap, Freeze)
- `Parameter`: The parameter name in PascalCase

**Standard Parameter Names (use these exact names):**

| Parameter | ID Suffix | Description |
|-----------|-----------|-------------|
| Delay Time | `DelayTimeId` | Main delay time in ms |
| Feedback | `FeedbackId` | Feedback amount (0-120%) |
| Mix | `MixId` | Dry/Wet mix (NOT "DryWet") |
| Time Mode | `TimeModeId` | Free/Synced selector |
| Note Value | `NoteValueId` | Tempo sync note value |
| Mod Depth | `ModDepthId` | Modulation depth (NOT "ModulationDepth") |
| Mod Rate | `ModRateId` | Modulation rate (NOT "ModulationRate") |
| Stereo Width | `StereoWidthId` | Stereo decorrelation amount |
| Width | `WidthId` | Pan width (use when NOT stereo decorrelation) |
| Age | `AgeId` | Component aging amount |
| Era | `EraId` | Era/model selector |
| Freeze | `FreezeId` | Freeze toggle |
| Filter Enabled | `FilterEnabledId` | Filter on/off toggle |
| Filter Cutoff | `FilterCutoffId` | Filter cutoff frequency |
| Filter Type | `FilterTypeId` | Filter type selector |
| Diffusion | `DiffusionId` | Diffusion amount |

**Compound Parameters:** Use descriptive sub-component names:
- `kTapeHead1EnabledId`, `kTapeHead1LevelId`, `kTapeHead1PanId`
- `kShimmerPitchSemitonesId`, `kShimmerPitchCentsId`
- `kSpectralFeedbackTiltId`

**AVOID:**
- Redundant prefixes: `kShimmerShimmerMixId` → `kShimmerMixId`
- Inconsistent abbreviations: Use `Mod` not `Modulation`
- Inconsistent terms: Use `Mix` not `DryWet`

### Modern C++ Requirements

Use smart pointers, RAII, constexpr, move semantics. Avoid raw `new`/`delete`.

## Layered DSP Architecture

5-layer architecture: Layer 0 (core) → Layer 1 (primitives) → Layer 2 (processors) → Layer 3 (systems) → Layer 4 (effects). Each layer can only depend on layers below. See `dsp-architecture` skill for details.

## File Organization

```
dsp/                              # Shared KrateDSP library (Krate::DSP namespace)
├── include/krate/dsp/
│   ├── core/                     # Layer 0
│   ├── primitives/               # Layer 1
│   ├── processors/               # Layer 2
│   ├── systems/                  # Layer 3
│   └── effects/                  # Layer 4
└── tests/                        # DSP unit tests

plugins/iterum/                   # Iterum plugin
├── src/
│   ├── entry.cpp, plugin_ids.h, version.h
│   ├── processor/processor.{h,cpp}
│   └── controller/controller.{h,cpp}
├── tests/                        # Plugin tests (unit, integration, approval)
└── resources/                    # UI, presets, installers
```

**Include patterns:**
- DSP headers: `#include <krate/dsp/primitives/delay_line.h>`
- Plugin headers: `#include "processor/processor.h"`

## Common Patterns

### Adding a New Parameter

1. Add ID in `plugin_ids.h`
2. Add atomic in `processor.h`
3. Handle in `processParameterChanges()` in `processor.cpp`
4. Register in `Controller::initialize()` in `controller.cpp`
5. Add to state save/load
6. Add UI control in `editor.uidesc`

### ODR Prevention (CRITICAL)

Before creating ANY new class/struct, search codebase:
```bash
grep -r "class ClassName" dsp/ plugins/
```

Two classes with same name in same namespace = undefined behavior (garbage values, mysterious test failures). Check `dsp_utils.h` and `specs/_architecture_/` before adding components.

## External Dependencies

### pffft (SIMD-Optimized FFT)

The `FFT` class (`dsp/include/krate/dsp/primitives/fft.h`) uses **pffft** ([marton78 fork](https://github.com/marton78/pffft), BSD license) as its backend. Source files live in `extern/pffft/`.

- **SIMD auto-detection**: SSE on x86/x64 (including MSVC via `_mm_*` intrinsics), NEON on ARM, scalar fallback
- **Build integration**: pffft is a static C library linked PUBLIC to KrateDSP (see `dsp/CMakeLists.txt`)
- **Public API unchanged**: The `Complex` struct and `FFT::forward()`/`inverse()` signatures are the same for all consumers (STFT, OverlapAdd, spectral processors, additive oscillator, etc.)
- **pffft output format**: For real transforms, ordered output is `[DC, Nyquist, Re(1), Im(1), Re(2), Im(2), ...]` — conversion to/from our `Complex[N/2+1]` format happens inside `fft.h`
- **Inverse normalization**: pffft does NOT normalize the inverse transform; `FFT::inverse()` applies `1/N` scaling

## DSP Implementation Rules

See `dsp-architecture` skill for interpolation selection, oversampling, DC blocking, feedback safety, and performance budgets.

## Workflow Requirements (MANDATORY)

### Canonical Todo List for Implementation Tasks

```
1. [ ] Write failing test for feature/change (skills auto-load: testing-guide, vst-guide)
2. [ ] Implement to make test pass
3. [ ] Fix all compiler warnings
4. [ ] Verify all tests pass
5. [ ] Run pluginval (if plugin code changed)
6. [ ] Run clang-tidy (./tools/run-clang-tidy.ps1 or .sh)
7. [ ] Commit
```

For **bug fixes**, step 1 is "Write test reproducing the bug" and verify it fails before fixing.

### Pluginval

Run after any plugin source changes:
```bash
tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Iterum.vst3"
```

Skip for docs-only, CI config, or test-only changes.

### Compiler Warnings

All code must compile with ZERO warnings. Common fixes:

| Warning | Fix |
|---------|-----|
| C4244 (double→float) | Add `f` suffix |
| C4267 (size_t→int) | Explicit cast |
| C4100 (unused param) | `[[maybe_unused]]` |

## Completion Honesty (MANDATORY)

**DO NOT fill the compliance table from memory or assumptions.** Every single row must be verified against actual code and test output.

Before claiming spec complete:
1. **For each FR-xxx**: Open the implementation file(s), read the relevant code, and confirm the requirement is met. Cite the file and line number.
2. **For each SC-xxx**: Run the specific test or measurement. Copy the actual output. Compare it against the spec threshold. Do not paraphrase — use real numbers.
3. **Fill the compliance table with this concrete evidence** (file paths, line numbers, test names, actual measured values). Generic claims like "implemented" or "test passes" without specifics are NOT acceptable.
4. **Self-check**: No relaxed thresholds? No placeholders? No quietly removed features? No ✅ without having just now verified the code/test?

If requirements aren't met, be honest and document gaps. A table full of ✅ that wasn't individually verified is worse than an honest ❌.

## Framework Debugging (MANDATORY)

When VSTGUI/VST3 SDK doesn't work as expected:

1. **DO NOT** immediately try different approaches
2. **DO NOT** assume framework bug or switch to native code
3. **DO** investigate: read logs, trace values, read framework source
4. Before pivoting, complete: debug logs checked, values traced, source read, divergence point identified

The `vst-guide` skill auto-loads with documented pitfalls and patterns.

## Build Commands

**CRITICAL: On Windows, the Python cmake wrapper in PATH does not work. You MUST use the full path to CMake:**

```bash
# Set alias for convenience (or use full path every time)
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure (Release)
"$CMAKE" --preset windows-x64-release

# Build (Release)
"$CMAKE" --build build/windows-x64-release --config Release

# Run DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run all tests via CTest
ctest --test-dir build/windows-x64-release -C Release --output-on-failure

# Debug build (same pattern)
"$CMAKE" --preset windows-x64-debug
"$CMAKE" --build build/windows-x64-debug --config Debug
```

**Note:** The plugin build may fail on the post-build copy step (permission error copying to `C:/Program Files/Common Files/VST3/`). This is fine - the actual compilation succeeded. The built plugin is at `build/windows-x64-release/VST3/Release/Iterum.vst3/`.

### AddressSanitizer (ASan)

Use ASan to detect memory errors (use-after-free, buffer overflows, etc.) that cause crashes but don't throw exceptions:

```bash
# Configure with ASan enabled
cmake -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON

# Build (use Debug for better stack traces)
cmake --build build-asan --config Debug

# Run tests - ASan will abort and report if memory errors occur
ctest --test-dir build-asan -C Debug --output-on-failure
```

**When to use ASan:**
- Investigating crashes that don't have clear stack traces
- After fixing editor lifecycle bugs (to verify no use-after-free)
- Before releases to catch latent memory issues

**Note:** ASan increases memory usage and slows execution. Use a separate build directory.

### Clang-Tidy (Static Analysis)

Use clang-tidy for static analysis to catch bugs, performance issues, and style violations before commit.

**One-Time Setup (Windows):**

1. Install LLVM (includes clang-tidy):
   ```powershell
   winget install LLVM.LLVM
   ```

2. Install Ninja (required for compile_commands.json):
   ```powershell
   winget install Ninja-build.Ninja
   ```

3. Generate compile_commands.json (run from VS Developer PowerShell):
   ```powershell
   # Open "Developer PowerShell for VS 2022" from Start Menu, then:
   cd F:\projects\iterum
   cmake --preset windows-ninja
   ```

**Running Analysis:**

```powershell
# Analyze all plugin source files
./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja

# Analyze specific targets
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
./tools/run-clang-tidy.ps1 -Target iterum -BuildDir build/windows-ninja
./tools/run-clang-tidy.ps1 -Target disrumpo -BuildDir build/windows-ninja

# Apply automatic fixes (use with caution, review changes)
./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja -Fix
```

**Linux/macOS:**
```bash
cmake --preset linux-release   # or macos-release (generates compile_commands.json)
./tools/run-clang-tidy.sh --target all
./tools/run-clang-tidy.sh --target dsp --fix
```

**Configuration:** The `.clang-tidy` file configures:
- Enabled: bugprone, performance, modernize, readability, concurrency, cppcoreguidelines
- Disabled: magic-numbers, short identifiers (DSP-friendly)
- Naming conventions matching this style guide

**When to run:**
- Before every commit (part of canonical todo list step 6)
- After significant refactoring
- As pre-commit quality gate in specs (Phase N-1.0)

**Re-run setup when:** CMakeLists.txt changes, new source files added, or compile flags change.

## Quick Reference

| Task | File(s) |
|------|---------|
| Add parameter | plugins/iterum/src/plugin_ids.h → processor → controller → uidesc |
| Add DSP component | dsp/include/krate/dsp/{layer}/ → dsp/tests/{layer}/ |
| Add plugin test | plugins/iterum/tests/ |
| Change UI | plugins/iterum/resources/editor.uidesc |

| Your Layer | Location | Can Include |
|------------|----------|-------------|
| 0 (core/) | dsp/include/krate/dsp/core/ | stdlib only |
| 1 (primitives/) | dsp/include/krate/dsp/primitives/ | Layer 0 |
| 2 (processors/) | dsp/include/krate/dsp/processors/ | 0, 1 |
| 3 (systems/) | dsp/include/krate/dsp/systems/ | 0, 1, 2 |
| 4 (effects/) | dsp/include/krate/dsp/effects/ | 0, 1, 2, 3 |

## References

- [VST3 Developer Portal](https://steinbergmedia.github.io/vst3_dev_portal/)
- [VSTGUI Documentation](https://steinbergmedia.github.io/vst3_doc/vstgui/html/)
- Project Constitution: `.specify/memory/constitution.md`

**Skills (auto-load when relevant):**
- `.claude/skills/claude-file/` - Display project CLAUDE.md with all development guidelines
- `.claude/skills/testing-guide/` - Test patterns, Catch2, DSP testing strategies
- `.claude/skills/vst-guide/` - VST3/VSTGUI patterns, thread safety, UI components
- `.claude/skills/dsp-architecture/` - Real-time safety, layers, interpolation, performance
