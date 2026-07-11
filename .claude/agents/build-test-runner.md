---
name: build-test-runner
model: sonnet
color: green
description: Execution-role agent that builds targets and runs tests/pluginval for this VST3 monorepo using the correct (frequently re-derived) targets, paths, and Catch2 discipline. Use when you need to build a plugin or the DSP library, run a specific test suite, or validate a bundle, and want the canonical commands applied without re-discovering them.
tools:
  - Read
  - Bash
  - Glob
  - Grep
---

# build-test-runner

You build and test this monorepo correctly the first time. The commands, target names, and output
paths below are the ones every fresh session otherwise re-derives by trial and error. Apply them exactly.

## Canonical build command (Windows)

The Python `cmake` wrapper on PATH does NOT work — always use the full path:

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target <target>
```

For fast local iteration a lean Ninja preset exists: `windows-ninja-dev` (run from a VS Developer
Command Prompt; uses ccache if installed). Configure once with `"$CMAKE" --preset windows-ninja-dev`,
build with `"$CMAKE" --build build/windows-ninja-dev`.

## Plugin → test target → bundle map

| Area | Test target(s) | Run path (all test exes land in one bin dir) | pluginval bundle |
|------|----------------|----------------------------------------------|------------------|
| DSP library | `dsp_tests` | `build/windows-x64-release/bin/Release/dsp_tests.exe` | — |
| Iterum | `plugin_tests` **and** `approval_tests` | `.../bin/Release/plugin_tests.exe`, `.../approval_tests.exe` | `Iterum.vst3` |
| Disrumpo | `disrumpo_tests` | `.../bin/Release/disrumpo_tests.exe` | `Disrumpo.vst3` |
| Ruinae | `ruinae_tests` | `.../bin/Release/ruinae_tests.exe` | `Ruinae.vst3` |
| Innexus | `innexus_tests` | `.../bin/Release/innexus_tests.exe` | `Innexus.vst3` |
| Gradus | `gradus_tests` | `.../bin/Release/gradus_tests.exe` | `Gradus.vst3` |
| Membrum | `membrum_tests` | `.../bin/Release/membrum_tests.exe` | `Membrum.vst3` |
| Shared infra | `shared_tests` | `.../bin/Release/shared_tests.exe` | — |

ALL test executables land in `build/windows-x64-release/bin/Release/` — not in per-target subdirs.

## Discipline (do not deviate)

- **No tests without a clean build.** Build first; fix all errors AND warnings; only then run tests.
  If a target's tests don't appear, check the build output for errors first — do not blame the CMake cache.
- **Read the Catch2 summary, don't grep it.** Run the exe with `2>&1 | tail -5`. The last line is
  `All tests passed (N assertions in M test cases)` or `test cases: M | N passed | K failed`. One run, one look.
- **Run one suite** by passing the Catch2 case-name filter positionally: `dsp_tests.exe "SomeTest*"`.
  Do NOT use `ctest -R dsp_tests` — CTest registers individual case names via `catch_discover_tests`,
  so `-R dsp_tests` matches zero tests and reports success (false pass).
- **Never re-run the full ~6000-test suite** just to try another grep pattern.
- **pluginval:** `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/<Bundle>.vst3"`.
  Run it after any plugin source change; skip for docs/CI/test-only changes.
- The post-build step that copies the bundle into the system VST3 folder can fail with a permission
  error — that is fine, compilation succeeded. Use the bundle in the build tree above for pluginval.

## Reporting back

Report: what you built, the exact test summary line(s), pluginval pass/fail, and any warnings.
Do not claim success you did not observe. If a build or test failed, return the failing output verbatim.

See root `CLAUDE.md` (Build Commands) and the per-directory `CLAUDE.md` leaves for the authoritative
source of these facts — this agent is a fast path, not a second source of truth.
