# Build-time instrumentation (F9)

Stop guessing header blast radius by grepping `#include` counts — measure it.

## Quick baseline (no rebuild, any Ninja build)

The aggregator reads `.ninja_log` for free, so you can rank the slowest translation units from any
Ninja build that has compiled at least once:

```bash
node tools/aggregate-build-times.js --build build/windows-ninja-dev
```

A committed snapshot lives at [`specs/_architecture_/build-times.md`](../specs/_architecture_/build-times.md).
Read the ranking, not the absolute milliseconds (they're machine- and parallelism-dependent).

## Header-level detail (opt-in instrumentation)

`.ninja_log` shows *which TU* is slow, not *which header inside it*. For that, build with the timing
preset (adds `-ftime-trace` on Clang/GCC, `/Bt+ /d2cgsummary` on MSVC via the `KRATE_BUILD_TIMING`
option — OFF by default, so normal builds are unaffected).

### Clang / GCC (Linux, macOS-clang)

```bash
cmake --preset linux-timing
cmake --build build/linux-timing
node tools/aggregate-build-times.js --build build/linux-timing
```

`-ftime-trace` drops a `<obj>.json` Chrome-trace next to every object; the aggregator sums the
`Source` events by included file to rank the **worst headers across all TUs**. For a deeper per-symbol
view, feed those JSONs to [ClangBuildAnalyzer](https://github.com/aras-p/ClangBuildAnalyzer).

### MSVC (Windows, from a VS Developer Command Prompt)

MSVC emits no trace JSONs — capture the build stdout instead:

```bash
cmake --preset windows-ninja-timing
cmake --build build/windows-ninja-timing 2>&1 | tee build-timing.log
node tools/aggregate-build-times.js --build build/windows-ninja-timing --msvc-log build-timing.log
```

`/Bt+` prints front-end (`c1xx`) + back-end (`c2`) time per file; the aggregator sums both and ranks.

## Reading the output

- **A header with high total time but included by few TUs** → a heavy include; splitting its body into
  a `.cpp` cuts that cost (see roadmap item D4).
- **A header with moderate time but included by many TUs** → a broad dependency; every edit to it
  reprices all of them.
- **A slow TU in `.ninja_log`** → often a monster test file (e.g. `arpeggiator_core_test.cpp`); splitting
  it parallelizes and shrinks incremental rebuilds (roadmap item D1).
