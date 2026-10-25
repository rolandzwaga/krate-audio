# dsp/ — KrateDSP Shared Library (`Krate::DSP`)

Area-specific guidance for the shared DSP library. Auto-loads when working under `dsp/`.
Root `CLAUDE.md` still applies — this adds detail, it does not override.

## Layer Architecture (STRICT — enforced by convention, not yet by CI)

5 layers, each may only include from layers **below** it:

| Layer | Folder | May include |
|-------|--------|-------------|
| 0 core | `include/krate/dsp/core/` | stdlib only |
| 1 primitives | `include/krate/dsp/primitives/` | core |
| 2 processors | `include/krate/dsp/processors/` | core, primitives |
| 3 systems | `include/krate/dsp/systems/` | core, primitives, processors |
| 4 effects | `include/krate/dsp/effects/` | all below |

Include with angle brackets: `#include <krate/dsp/primitives/delay_line.h>`.
Unit tests mirror the layer tree under `dsp/tests/unit/{core,primitives,processors,systems,effects}/`.

## ODR Prevention (highest-severity failure in this codebase)

Two classes with the same name in the same namespace = silent UB (garbage values, mystery
test failures). **Before creating ANY class/struct**, search first:

```bash
grep -rn "class ClassName\|struct ClassName" dsp/ plugins/
```

Also check `dsp_utils.h` and `specs/_architecture_/` for an existing component before adding one.

## Header-only + SIMD conventions

- DSP is largely **header-only** (templates, inlining). A change to a widely-included core
  header rebuilds most of the library — prefer adding to a narrow header over a core one.
- Heavy non-template bodies may live in a `.cpp` (layer-legal), e.g. `spectral_simd.cpp`.
- SIMD math uses Google Highway (linked PRIVATE — no Highway headers in public API) and pffft
  (fetched via FetchContent, **not** vendored in `extern/`). SSE needs 16-byte alignment, AVX 32 —
  use `alignas()`.

## Real-time safety (audio thread = hard real-time)

No allocations, locks, exceptions, or I/O on the audio path. Enable FTZ/DAZ for denormals.
`-ffast-math` breaks `std::isnan()` and hand-written bit-mask checks alike — classify through `detail::isFinite` /
`isNaN` / `isInf` (`core/db_utils.h`). See "The macOS leg builds with -ffast-math" below.
See the `dsp-architecture` skill for interpolation choice, oversampling, DC blocking, perf budgets.

## Cross-platform: never arch-guard a krate include

macOS CI builds **universal (x86_64 + arm64)**. A Windows-only local build passes the x86
branch of every `#if defined(__x86_64__)` and can never catch the arm64 side.

**Include krate headers unconditionally.** Headers that wrap intrinsics (e.g.
`core/scoped_denormal_mode.h`) do the architecture split internally and become a zero-cost
no-op elsewhere. Guarding the `#include` drops the declaration on arm64 while the using code
stays unconditional → `no type named 'X' in namespace 'Krate::DSP'`, macOS leg only.

```bash
node tools/lint-arch-guarded-includes.js   # gate; also runs in CI
```

When replacing an `#include`, **read the lines around it first** — the one you are swapping may
itself sit inside an arch `#if` that a grep for the include line will not show.

To syntax-check a header for arm64 without a Mac:
```bash
"/c/Program Files/LLVM/bin/clang++.exe" --target=aarch64-unknown-linux-gnu \
  -std=c++20 -fsyntax-only -I dsp/include <file>.cpp
```

## MSVC accepts what GCC and Clang reject — check before pushing

A green Windows build is **not** evidence the Linux and macOS legs will compile.
MSVC is lenient about constructs the other two refuse, so the first sign of trouble
is a red CI job ~7 minutes in. Real example that broke both legs:

```cpp
// MSVC: fine.   GCC + Clang: error, not a constant expression.
constexpr Steinberg::FIDString kPlatformType = Steinberg::kPlatformTypeHWND;
```

The SDK declares those as plain `const FIDString`. Use `const`, not `constexpr`,
for anything initialized from an SDK constant.

Syntax-check changed translation units with GCC before committing — seconds, versus
a CI round-trip:

```bash
node tools/check-portability.js            # everything this branch changed
node tools/check-portability.js --staged   # staged only
```

A `guard-portability.js` PreToolUse hook runs the `--staged` form before every
`git commit` and blocks on failure. It needs WSL with g++ and skips silently
without it — so on a machine lacking WSL, run the check by hand or expect CI to
find it for you.

## Never pin a render with a bit-exact digest over float samples

Hashing the raw bits of rendered samples (FNV over `memcpy(&bits, &sample, 4)`) asserts
**bit-identical floating-point math on every compiler that will ever build the test**. It
cannot hold: MSVC, GCC and Apple Clang differ in the last bits of every transcendental, and
the macOS leg builds with `-ffast-math`. One 1-ULP difference anywhere in the buffer changes
the digest completely, so a digest generated on Windows is *guaranteed* red on Linux and
macOS. This broke both legs once already.

Measured cross-toolchain spread for the phaser/flanger renders (g++ -O3, g++ -O3 -ffast-math,
clang++ -O2):

| Quantity | Worst spread |
|---|---|
| per-sample absolute difference | 2.9e-5 (signal peak 2.17) |
| aggregate relative difference (RMS, peak, mean-abs, total variation) | 1.9e-7 |

Use `tests/test_helpers/render_fingerprint.h` instead: aggregate metrics plus spaced sample
checkpoints, at tolerances derived from those measurements. Total variation is the sharp
metric — it tracks waveform shape, so a stale filter cache or changed coefficient moves it
far outside tolerance (a deliberately injected stale-sweep-cache bug produced a 0.38 sample
error against a 1e-4 bound).

Know the limit: at a portable tolerance you can no longer detect changes *smaller* than the
toolchain spread. Swapping `std::tanh` for `FastMath::fastTanh` moves these renders by only
2.7e-6 and is invisible to the fingerprint — that change carries its own dedicated
error-bound test, which is the right way to cover it.

Digests over a **serialized byte stream** (saved plugin state) are fine and are the correct
way to pin a preset format — those bytes are stored values, not arithmetic results.

```bash
node tools/lint-float-bit-goldens.js   # gate; also runs in CI
```

### The same trap one step removed: integer timestamps derived from float math

A captured MIDI dump compared with `REQUIRE(actual == golden)` *looks* like a byte
comparison, but every sample offset in it is FP arithmetic **truncated** to an integer.
Truncation converts a 1-ULP difference into a whole sample, so the assertion silently
demands bit-exact FP math from every compiler — the same impossible requirement, just
disguised as text. `ArpeggiatorCore`'s durations land exactly on integers by construction,
which is the worst possible input to a truncating cast:

```cpp
baseDuration = (size_t)(secondsPerBeat * beatsPerStep * sampleRate);
swung        = (size_t)((double)baseDuration * (1.0 + swing));
// 11025 * 1.36 is exactly 14994 in real arithmetic, but evaluates to
// 14993.999999999998 or 14994.000000000002 depending on how the product is
// formed -> truncates to 14993 or 14994.
```

That is how the Ruinae SC-004b goldens went red on Linux and macOS while passing every
Windows gate (full suite, pluginval, clang-tidy). Measured on the real 60-second
`Tape_Shuffle` sequence: **504 events on both legs, 0 differing in kind/pitch/order, 146
differing in timing, worst 2 samples (~45 µs)** — bounded, not cumulative, because
durations are recomputed per step from the tempo rather than accumulated.

Compare structure exactly (count, order, kind, pitch, velocity) and timestamps at a
measured tolerance: `tests/test_helpers/midi_golden_compare.h` is the drop-in replacement.

```bash
node tools/lint-midi-timing-goldens.js   # gate; also runs in CI
```

## The macOS leg builds with -ffast-math: write for it, and reproduce it before pushing

The Xcode leg compiles every TU with `-ffast-math -ffp-contract=fast` (SMTG_PlatformToolset.cmake:46) on a CPU with
FMA. Windows' `/fp:fast` is far milder and Linux has no fast-math, so **a green Windows + Linux run is no evidence for
macOS floating point**. One CI round on 2026-10-25 had six classes of macOS-only red. Each rule below names one:

| Rule | Why (the red it cost) |
|---|---|
| Classify finiteness only through `detail::isFinite` / `isNaN` / `isInf`. Never `std::isnan`, never a local `(bits & 0x7F800000u) != 0x7F800000u` clone, in DSP or tests. | Apple clang folds the clone to "finite" even through `bit_cast` / `memcpy`: seven test helpers were vacuous. Gated by `tools/lint-finite-bitmask.js` (ratchet) and `tools/lint-nonfinite-symbols.js`. |
| Clamp the argument of `pow` / `sqrt` / `log` / `acos` at its domain edge whenever IEEE arithmetic only *happens* to land on the edge. | `HarmonicFold`: `pow(1 - (a - c) / span, p)` had base exactly 0 at amount 100 in IEEE, -4.4e-16 after reassociation: NaN, Fold 100 broken on Mac. |
| A value that must be exact at a neutral point or an endpoint (Mass 50, normalized 0 / 1) is produced by a branch or a clamp, or by a `#pragma clang fp contract(off) reassociate(off)` body, never by arithmetic that cancels in IEEE. | `MassLaw`: `(m - 50) * 0.02f` became `fma(m, 0.02f, -1)` = -2.2e-8 at 50. Profundum's ceiling: `(db + 12) / 12` became `fma(db, 1 / 12, 1)` = 5.6e-17 at the floor. |
| No bit-equality claim between two separately compiled computations (engine tap vs the test's recomputation, two helpers, a live object vs a frozen copy). Put such a TU in `dsp_ieee_tests`, or compare within a tolerance below the meaningful step. | Each inlined call site is rewritten on its own (`(s - 69) / 12` became `s * (1/12) - 5.75` in one copy only). MonoHandler, DropPhaseLock, SpectralShapeRecipe, MassSplit, AnchorJoin, C1CheckerCrossCheck. |
| A `-fno-fast-math` test TU is not IEEE for the inline DSP it calls in a mixed executable, and its expected values differ from the fast-math code it checks. NaN-injection TUs go in `dsp_ieee_tests`; a plugin test computing an expected `k / 24` compares within a tolerance. | COMDAT: the linker keeps one copy of an un-inlined inline function, often from a fast-math TU (FeedbackEcology_NonFinite). Profundum Pitch Bend Range: IEEE test `5 / 24` vs fast-math `5 * (1/24)`, one ulp. |
| A new test executable with allocation checks needs exactly one TU including `<allocation_operator_overrides.h>`. | Without it every `getAllocationCount() == 0` passes vacuously and every positive control reads 0 (`dsp_ieee_tests` lived like that for one commit). |

**Reproduce locally, minutes instead of a CI round:**

```bash
# DSP test TUs under the macOS flags (x86 clang in WSL); --ieee is the control build.
wsl -e bash -lc 'bash /mnt/f/projects/iterum/tools/macos-fastmath-repro.sh systems/mass_law_test.cpp -- "MassLaw_*"'
```

```powershell
# Plugin tests (VST SDK, VSTGUI): clang-cl + /fp:fast proxy tree. Blind spot: per-file -fno-fast-math test TUs keep the
# reciprocal transform here, so an IEEE-test-vs-fast-math-plugin split does not show (read the script header).
.\tools\clangcl-fastmath-build.ps1 -Targets profundum_tests
```

To attribute a CI red, first read which commit the run built (the log's `HEAD is now at <merge> Merge <sha> into ...`
line), then run the repro on a `git worktree` at that commit as well as at HEAD: red at the CI commit and green at HEAD
means a later commit already fixed it. Both tools are x86, not Apple clang on arm64; an arm64-only red (C1CheckerCrossCheck)
stays green here, so read the code for those.

## Verify platform-dependent BEHAVIOUR on Linux via WSL (Ubuntu + g++ 13 installed)

Compiling for another arch only catches *compile* errors. Runtime semantics that differ by
platform will still pass on Windows and fail in CI. Known divergence that has already bitten:

| Behaviour | Windows | Linux / glibc |
|---|---|---|
| MXCSR (FTZ/DAZ) in a newly created thread | fresh default | **inherits the creating thread's** |

A test that spawns a thread *after* changing FP state therefore proves nothing portable.
Model the real scenario instead (host audio thread already running), and check it on Linux:

```bash
# Fast path: compile just the TU under test as a standalone main, no CMake needed.
wsl -e bash -lc 'g++ -std=c++20 -O2 -fno-fast-math \
  -I /mnt/f/projects/iterum/dsp/include /tmp/probe.cpp -o /tmp/probe -pthread && /tmp/probe'
```

Reach for this whenever a test encodes an assumption about threads, FP environment, locales,
filesystem case-sensitivity, or `long double` — it takes seconds and CI takes ~40 min.

## Build & test

The suite is split into 5 per-layer executables (`dsp_core_tests`, `dsp_primitives_tests`,
`dsp_processors_tests`, `dsp_systems_tests`, `dsp_effects_tests`) so editing one layer only
relinks that layer's exe. A new test file MUST be added to the matching target in
`dsp/tests/CMakeLists.txt` (sources are listed explicitly, not globbed) or it silently drops.
A test that injects NaN / Inf and asserts on it (`*_nonfinite_test.cpp`), or asserts that two separately compiled
float computations agree bit for bit, goes in `dsp_ieee_tests`, whose every TU is `-fno-fast-math`: in a mixed
executable the linker may keep a fast-math copy of an inline DSP function, and Apple clang folds NaN out of it
(FeedbackEcology_NonFinite) and reassociates each inlined call site on its own (MonoHandler, macOS arm64, 2026-10-25).

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
# Build + run just the layer you touched, e.g. core:
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_core_tests
build/windows-x64-release/bin/Release/dsp_core_tests.exe 2>&1 | tail -5
# ...or all six: --target dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests dsp_ieee_tests
```

Run one suite by passing the Catch2 case name positionally (`dsp_core_tests.exe "SomeTest*"`), NOT
`ctest -R dsp_core_tests` (that matches individual case names, not the exe — returns zero tests, reports success).
