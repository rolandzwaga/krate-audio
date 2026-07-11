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

## Build & test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5
```

Run one suite by passing the Catch2 case name positionally (`dsp_tests.exe "SomeTest*"`), NOT
`ctest -R dsp_tests` (that matches individual case names, not the exe — returns zero tests, reports success).
