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
`-ffast-math` breaks `std::isnan()` — detect NaN via bit manipulation on a `-fno-fast-math` TU.
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

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
# Build + run just the layer you touched, e.g. core:
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_core_tests
build/windows-x64-release/bin/Release/dsp_core_tests.exe 2>&1 | tail -5
# ...or all five: --target dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests
```

Run one suite by passing the Catch2 case name positionally (`dsp_core_tests.exe "SomeTest*"`), NOT
`ctest -R dsp_core_tests` (that matches individual case names, not the exe — returns zero tests, reports success).
