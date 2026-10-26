# Tasks: Profundum Phase 1 — Harmonic Core

**Spec:** `specs/profundum-phase1-harmonic-core/spec.md` · **Plan:** `specs/profundum-phase1-harmonic-core/plan.md`
(S0–S11) · **Calibration model:** `specs/profundum-phase1-harmonic-core/recipe-model.js`
**Status:** Task list (2026-10-09). The build stage has not started.

## Read this first (every executor)

**Precondition: ratification.** Plan S1 lists nine spec amendments (C-1 … C-9). Four of them change what a test
asserts: C-1 (SC-004 Body span at the 5 low-headroom points), C-2 (the SC-004 Shift point set and the FR-016
wording), C-8 (dB-linear cap taper, plus the SC-001 measurability rule) and C-9 (the FR-042 grid restarts at a
`Reset` `noteOn`). The tasks below encode the **amended** versions. Every test that relies on one cites the C-item
in a comment (`// plan S1 C-1`). If the user rejects an amendment, the affected tasks are T007, T008, T010, T013
and T016. Change them before you execute them; do not relax a bar on your own.

**Layer and file map** (plan header table, S8.1):

| File | Kind | Layer / target |
|---|---|---|
| `dsp/include/krate/dsp/processors/spectral_shape_recipe.h` | new header | L2 |
| `dsp/include/krate/dsp/systems/profundum_core.h` | new header | L3 (includes L0 + L2 only) |
| `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h` | **existing shared header**, append-only | L2 |
| `dsp/tests/unit/processors/spectral_shape_recipe_test.cpp` | new TU | `dsp_processors_tests` |
| `dsp/tests/unit/processors/spectral_shape_recipe_test_helpers.h` | new test header | — |
| `dsp/tests/unit/processors/harmonic_oscillator_bank_tests.cpp` | existing TU | `dsp_processors_tests` |
| `dsp/tests/unit/systems/profundum_core_test.cpp` | new TU | `dsp_systems_tests` |
| `dsp/tests/unit/systems/profundum_core_spectral_test.cpp` | new TU | `dsp_systems_tests` |
| `dsp/tests/unit/systems/profundum_core_nonfinite_test.cpp` | new TU, **`-fno-fast-math` list** | `dsp_systems_tests` |
| `dsp/tests/unit/systems/profundum_core_perf_test.cpp` | new TU, **not** in the `-fno-fast-math` list | `dsp_systems_tests` |
| `dsp/tests/unit/systems/profundum_core_test_helpers.h` | new test header | — |
| `specs/profundum-phase1-harmonic-core/check-footprint.js` | new Node script | SC-023 |

**Verified facts these tasks depend on** (each read on 2026-10-09; the plan's S0 ledger has the rest):

- Bank: `class HarmonicOscillatorBank {` at `harmonic_oscillator_bank.h:75`. Its constants block runs from `:78`
  (`kDefaultCrossfadeTimeSec` `:82`, `kAmpSmoothTimeSec` `:85`, `kAntiAliasFadeStart = 0.8f` `:88`,
  `kOutputClamp = 2.0f` `:91`). `reset()` holds `constexpr float kCenterGain = 0.7071067811865476f; // sqrt(2)/2`
  (`:225`) and `panLeft_.fill(kCenterGain); panRight_.fill(kCenterGain);` (`:226–227`).
  `void applyPanOffsets(const std::array<float, kMaxPartials>& offsets) noexcept` (`:642–654`) and
  `void applyExternalFrequencyMultipliers(const std::array<float, kMaxPartials>& multipliers) noexcept`
  (`:663–670`, `detuneMultiplier_[i] *= multipliers[i];`).
- Test helpers live in the **repo-root** `tests/test_helpers/` and are included by bare name
  (`#include "artifact_detection.h"`, as `dsp/tests/unit/systems/atmosphere_ghost_longrun_test.cpp:74` does).
  - `ClickDetectorConfig{sampleRate, frameSize = 512, hopSize = 256, detectionThreshold = 5.0f, energyThresholdDb = -60.0f, mergeGap = 5}`
    (`artifact_detection.h:38–44`). `ClickDetector::prepare()` is at `:105` and `detect(...)` at `:130`.
  - `namespace lowFreq` (`low_frequency_metrics.h:82`): `kLowFrequencyFftSize = 262144` (`:64`);
    `[[nodiscard]] inline bool magnitudeSpectrum(const float* x, std::size_t n, std::vector<float>& mags)` (`:118`,
    Hann, returns false on failure); `[[nodiscard]] inline double harmonicMainLobePower(const std::vector<float>& mags, double harmonicHz, double binHz)`
    (`:180`). Every consumer REQUIREs `lowFreq::analysisFft(n).isPrepared()` (`:62`).
  - `AllocationDetector::instance().startTracking()` / `stopTracking()` returns the count (`allocation_detector.h:53, 59`).
  - `fingerprintRender(std::span<const float>)` (`render_fingerprint.h:73`).
- FFT (for BH7 analyses): `FFT::prepare(size_t fftSize) noexcept` (`primitives/fft.h:147`, any power of two),
  `forward(const float* input, Complex* output) noexcept` (`:186`), `numBins()` = size/2+1 (`:252`),
  `isPrepared()` (`:255`).
- FTZ guard macro: `KRATE_HAS_SSE_DENORMAL_CONTROL` (`core/scoped_denormal_mode.h:39/43`, used at `:63`).
- `dsp/tests/CMakeLists.txt`: `dsp_processors_tests` starts at `:156` (the bank TU is at `:252`).
  `dsp_systems_tests` starts at `:324`, and its source list closes with `)` at `:562`, right after
  `unit/systems/vorago_ecosystem_lever_longrun_test.cpp`. The `-fno-fast-math` `set_source_files_properties(` block
  opens at `:666`, lists `harmonic_oscillator_bank_tests.cpp` at `:903`, and closes with
  `PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"` at `:1039`.
- `dsp/CMakeLists.txt`: `set(KRATE_DSP_PROCESSORS_HEADERS` at `:128` and `set(KRATE_DSP_SYSTEMS_HEADERS` at
  `:159`. The Vorago entries are at `:177–180`, under a `# Vorago Phase 10 (specs/...)` comment.
- `tools/run-cpu-tests.js:56`: `FILTER = '[performance],[perf],[.perf],[benchmark],[!benchmark],[long]~[vorago-sweep]'`.
- `recipe-model.js` exports `{ P0, K, shape, capFactor, mask, full, desc, dist, NAMED, COLOURS }` (line 278).
  `shape(c, N)` is the double-precision S3.2 law and `full(c, f0, fs, N)` is shape → mask → renormalise.
- ODR sweep, 2026-10-09: `grep -rn "class ProfundumCore\|class SpectralShapeRecipe\|restoreCenterPan\|namespace ProfundumTest" dsp plugins tests`
  returned **0 hits**.

**Commands** (Git Bash, full CMake path, always):

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "SpectralShapeRecipe_*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ProfundumCore_*" 2>&1 | tail -5
```

Filter by test name with a positional argument. Do not use `ctest -R`, which matches Catch2 case names and not
exes. Capture any run longer than about 30 s to a log under the scratchpad (`… > "$LOG" 2>&1`) and read the log,
rather than re-running the run.

**Rules that apply to every task:**

1. Write the failing test **first**. Build, and watch it fail: a compile error for a missing symbol counts as a
   fail. Then implement, rebuild with **zero warnings**, and run the named cases until they pass.
2. Header-only, `namespace Krate::DSP`. Classes PascalCase, functions camelCase, members `trailing_`, constants
   `kPascalCase`. Use designated initialisers with `f` literals. Write every double→float conversion as an
   explicit `static_cast`. Finiteness checks use `detail::isFinite` (`core/db_utils.h:118`) only, never
   `std::isnan`/`std::isfinite`.
3. Audio-thread methods are `noexcept` and allocation-, lock-, exception- and I/O-free. Storage is fixed member
   arrays.
4. Test helpers are `inline` functions in `namespace Krate::DSP::ProfundumTest`, never in an anonymous namespace
   inside a header. Test-local random numbers use `std::mt19937{0x5EED}`.
5. Tags: Recipe `[processors][profundum]`; Core / Spectral / NonFinite `[systems][profundum]`. Add `[long]` only
   where a case exceeds about 15 s and its failure mode is toolchain-independent; give each `[long]` case a
   per-push `…Smoke` sibling. NaN/Inf, L == R, state-format and bounded-grid cases are **never** `[long]`. The
   perf case is `[systems][profundum][.perf]` and the listening case `[systems][profundum][.listen]` (plan C-5).
6. No bit-exact float goldens across toolchains. `memcmp` is used only for in-process bit-identity (two renders in
   one binary). Reference vectors from `recipe-model.js` are compared with a tolerance.
7. Every render with an all-zero pan vector calls `ProfundumTest::requireLREqual(L, R, n)` (SC-014(a)).
8. If a test exposes a defect in a header that a *different* task owns (for example, a [P] verification task
   finds a core bug), **do not edit that header from the [P] task**. Record the failing case, its output and
   your diagnosis under "Defects found" at the bottom of this file. T017 fixes it.
9. Never relax a threshold or shrink a workload to make a test pass (CLAUDE.md, FR-074).

---

## Group 0 — pre-flight (parallel)

### T001 [P] ODR sweep and calibration baseline (read-only)

- Run `grep -rn "class ProfundumCore\|struct ProfundumCore\|class SpectralShapeRecipe\|struct SpectralShapeRecipe\|restoreCenterPan\|kPitchUpdateInterval\|namespace ProfundumTest" dsp plugins tests`.
  It must print nothing. Confirm that none of the roadmap hazard names (`CharacterProcessor`, `SubOscillator`,
  `StereoField`, `MorphEngine`, `TransientDetector`) appears in any name this task list introduces.
- Run `node specs/profundum-phase1-harmonic-core/recipe-model.js`. The output must end with
  `ALL RECIPE-SIDE GATES PASS` and match plan S3.6. Record the wall clock (about 6 s).
- If anything differs, stop and report it. Every later task assumes this baseline.

### T002 [P] SC-023 footprint script

- **Create** `specs/profundum-phase1-harmonic-core/check-footprint.js`. It is a Node CommonJS script with no
  dependencies. Usage: `node specs/profundum-phase1-harmonic-core/check-footprint.js [baseRef]`, with `aa4788bb`
  (the commit that added the Profundum roadmap, the phase base) as the default.
- Behaviour:
  - Use `child_process.execFileSync('git', ['diff', '--numstat', base, '--', file])` and `git diff -U0`.
  - These four forbidden files must show **0 added and 0 removed** lines:
    `dsp/include/krate/dsp/processors/harmonic_oscillator_bank_simd.h`,
    `dsp/include/krate/dsp/processors/harmonic_oscillator_bank_simd.cpp`,
    `dsp/include/krate/dsp/processors/harmonic_types.h` and
    `dsp/include/krate/dsp/processors/additive_oscillator.h`.
  - For `harmonic_oscillator_bank.h`, every removed line (trimmed) must be one of
    `constexpr float kCenterGain = 0.7071067811865476f; // sqrt(2)/2`, `panLeft_.fill(kCenterGain);` or
    `panRight_.fill(kCenterGain);`, with at most 3 removed lines in total.
  - Every added line in that file that is not blank and not a comment (`//` or `///`) must be in this allow-list:
    `static constexpr float kCenterPanGain = 0.7071067811865476f;`, `panLeft_.fill(kCenterPanGain);`,
    `panRight_.fill(kCenterPanGain);`, `void restoreCenterPan() noexcept {`, `void restoreCenterPan() noexcept`,
    `{`, `}`.
  - Print one PASS/FAIL line per file and the overall verdict, and exit non-zero on any FAIL.
- **Verify now.** On the current tree the script must PASS, since nothing has changed yet. Then run the positive
  control:
  1. Add a one-line comment to `harmonic_types.h`.
  2. Run the script and confirm it FAILs.
  3. Revert that edit with `git checkout -- dsp/include/krate/dsp/processors/harmonic_types.h`, touching only
     that file.

  Record both outputs under "Run log".

---

## Group 1 — CMake registration (sequential, single task)

> **Deliberate ordering deviation (see open questions):** the CMake registration is the *first* code task, not
> the last. Every later task is test-first and must build its new TU. A TU that is not registered cannot fail,
> so the test-first rule would be vacuous. The final group re-audits the registration (T018).

### T003 Register every Phase 1 file in CMake, with compiling stubs

- **Create stubs** so that configure and build succeed before any real code exists:
  - `dsp/include/krate/dsp/processors/spectral_shape_recipe.h`: `#pragma once`, a file comment naming
    "Layer 2: DSP Processor — Profundum Phase 1 (specs/profundum-phase1-harmonic-core)", and
    `namespace Krate::DSP {}`.
  - `dsp/include/krate/dsp/systems/profundum_core.h`: the same, with "Layer 3: System Component".
  - The five TUs `spectral_shape_recipe_test.cpp`, `profundum_core_test.cpp`, `profundum_core_spectral_test.cpp`,
    `profundum_core_nonfinite_test.cpp` and `profundum_core_perf_test.cpp`: a file comment that names the TU's
    SCs from the plan S8.1 table, `#include <catch2/catch_test_macros.hpp>`, and no test cases yet. Copy the
    include style of `dsp/tests/unit/systems/vorago_engine_test.cpp`.
- **Edit `dsp/CMakeLists.txt`.** Add `include/krate/dsp/processors/spectral_shape_recipe.h` to
  `KRATE_DSP_PROCESSORS_HEADERS` (`:128`) and `include/krate/dsp/systems/profundum_core.h` to
  `KRATE_DSP_SYSTEMS_HEADERS` (`:159`). Put each under `# Profundum Phase 1 (specs/profundum-phase1-harmonic-core)`.
- **Edit `dsp/tests/CMakeLists.txt`:**
  - `dsp_processors_tests`: add `unit/processors/spectral_shape_recipe_test.cpp` next to the bank TU (`:252`).
  - `dsp_systems_tests`: before the closing `)` at `:562`, add a comment block in the Vorago style that maps each
    TU to its SCs (from plan S8.3), then the four `unit/systems/profundum_core_*.cpp` TUs.
  - `-fno-fast-math` block: just before `PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"`
    (`:1039`), add `unit/systems/profundum_core_nonfinite_test.cpp` under the comment
    `# Profundum Phase 1: injects NaN/Inf bit patterns (SC-016, SC-020(d), FR-002). The perf TU stays out so its figures reflect the shipping FP mode.`
    Add **only** that TU.
- **Verify:**
  - Reconfigure with `"$CMAKE" --preset windows-x64-release`.
  - Build `dsp_processors_tests` and `dsp_systems_tests` with zero warnings.
  - Both exes still run: `dsp_processors_tests.exe 2>&1 | tail -3`, and `dsp_systems_tests.exe "[vorago]" 2>&1 | tail -3`
    as a sanity sample. The full systems run happens in T019.
  - Record the build wall clocks.

---

## Group 2 — the shared-header change (sequential; existing shared file)

### T004 `HarmonicOscillatorBank::restoreCenterPan()` (FR-064, SC-014(d), plan S5)

Files: `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h` and the existing
`dsp/tests/unit/processors/harmonic_oscillator_bank_tests.cpp`, which is already in the `-fno-fast-math` list
(`:903`). Touch nothing else.

**Failing test first.** Add
`TEST_CASE("HarmonicOscillatorBank_RestoreCenterPan", "[processors][harmonic_oscillator_bank]")`, using the
includes the file already has.

Fixture:
- `prepare(48000.0)`.
- A `HarmonicFrame` with `numPartials = 16` and, for n = 1…16, `harmonicIndex = n`, `relativeFrequency = n`,
  `amplitude = 1/n`, `phase = 0`, `bandwidth = 0`.
- `loadFrame(frame, 110.0f, true)`.
- Render with `processStereoBlock`.

Assertions:
1. `STATIC_REQUIRE(HarmonicOscillatorBank::kCenterPanGain == 0.7071067811865476f)`.
2. **Teeth.** Bank A: `applyPanOffsets(o)` with `o[i] = (i % 2 ? 0.6f : -0.6f)`, then render 256 samples; at least
   one L sample differs from its R sample. Then `restoreCenterPan()` and render 4 096 samples:
   `std::memcmp(L, R, 4096 * sizeof(float)) == 0`.
3. **Twin.** Bank B has the same setup but no pan calls. Render A and B in lock-step: 256 samples; A only:
   `applyPanOffsets(o)`; 256 samples; A only: `restoreCenterPan()`; 4 096 samples. Over the final 4 096 samples,
   A.L and B.L are `memcmp`-equal, and so are A.R and B.R. This proves the method touches only the pan tables.
4. On a bank already at centre, `restoreCenterPan()` mid-render changes nothing: twin renders over 4 096 samples
   are `memcmp`-equal.
5. `reset()` behaviour is unchanged by the hoist. Make a twin bank that is `reset()` and re-loaded; it is
   `memcmp`-equal to a freshly prepared bank over 4 096 samples. Both run in the same binary, so this is
   in-process identity, not a golden.
6. `STATIC_REQUIRE(noexcept(std::declval<HarmonicOscillatorBank&>().restoreCenterPan()))`.

**Implement (append-only, exactly as plan S5):**
1. In the constants block (`:78–91`), add `/// Centre pan gain written by reset() and restoreCenterPan() (sqrt(2)/2)`
   and `static constexpr float kCenterPanGain = 0.7071067811865476f;`.
2. In `reset()` (`:225–227`), delete the local `kCenterGain` line and use `kCenterPanGain` in the two fills.
3. After `applyExternalFrequencyMultipliers` (ends `:670`), append `restoreCenterPan()` verbatim from plan S5,
   with its doc comment and `/// @note Real-time safe`. It refills `panLeft_` and `panRight_` only.

**Verify:**
- Build `dsp_processors_tests` with zero warnings.
- Run `dsp_processors_tests.exe "[harmonic_oscillator_bank]" 2>&1 | tail -5`; all pass.
- Run `node specs/profundum-phase1-harmonic-core/check-footprint.js`; it PASSes.
- Do **not** rebuild the plugin suites here. The consumer regression is batched into T019.

---

## Group 3 — recipe skeleton (sequential)

### T005 `SpectralShapeRecipe` API, constants, named coordinates, `sanitize` (FR-001/002/014/020, SC-011(e), SC-006 analytic clause)

Files: `spectral_shape_recipe.h` (replace the stub), `spectral_shape_recipe_test.cpp` and the **new**
`spectral_shape_recipe_test_helpers.h`.

**Failing tests first** (Recipe TU, tag `[processors][profundum]`):
- `SpectralShapeRecipe_GuardIsConstant` (SC-011(e)):
  - `static_assert(SpectralShapeRecipe::kLowNoteGuardOnsetHz == 32.70f)`.
  - `static_assert(sizeof(SpectralShapeRecipe::Controls) == 6 * sizeof(float))`.
  - Define `template<class T> concept Init6 = requires { T{0.f,0.f,0.f,0.f,0.f,0.f}; };` and `Init7` with seven
    floats, then `static_assert(Init6<Controls> && !Init7<Controls>)`.
  - `auto [a, b, c, d, e, f] = SpectralShapeRecipe::kDefaultControls;` compiles. Use the bindings in REQUIREs so
    they are not unused.
  - A default-constructed `Controls{}` equals {0.5, 0.5, 0.5, 0, 0.5, 0}.
  - No entry point takes a guard argument. This is asserted by the signatures used in the later tasks; document
    it in a comment.
- `SpectralShapeRecipe_AnchorCoordinatesAnalytic` (SC-006 clause, FR-018):
  - `static_assert(SpectralShapeRecipe::kSawAnchor.depth == 0.0f)` and
    `static_assert(SpectralShapeRecipe::kSineAnchor.depth == 1.0f)`.
  - `REQUIRE(SpectralShapeRecipe::baseExponent(SpectralShapeRecipe::kTriangleAnchor.depth) == 2.0f)`, exactly.
  - All three anchors have `body == 0` and `edge == 0`. The triangle has `bodyEmphasis == -1`; saw and sine have
    `0`.
  - A table-driven check of all 12 named coordinates of plan S3.5 (`kDefaultControls`, the 3 anchors, `kHeavy`,
    `kHollow`, `kGrowl` and the 5 `kBody*`), each of the six fields exactly equal to the table.
- `SpectralShapeRecipe_ControlsSanitize` (clamp arm):
  - For each field, the inputs −10, +10, `FLT_MAX` and `-FLT_MAX` clamp to the range end: depth, body, curvature
    and edge to [0, 1], emphasis to [−1, 0], shift to [−1, 1].
  - In-range values pass through bit-unchanged.
- `SpectralShapeRecipe_PowerTargetDerivation` (FR-005):
  - `kPowerTarget` == `Approx(2.0 * std::pow(std::pow(10.0, -12.0/20.0) / 0.7071067811865476, 2.0)).epsilon(1e-7)`,
    which is ≈ 0.2523829.
  - `kCenterPanGain == HarmonicOscillatorBank::kCenterPanGain`. Include the bank header in the test only; the
    recipe header must not include it.
  - `kNyquistCapFraction == HarmonicOscillatorBank::kAntiAliasFadeStart`.
  - `capFrequency(48000.0) == Approx(19200.0f)` and `capFrequency(44100.0) == Approx(17640.0f)`.

**Implement:**
- The public API of plan S3.1, verbatim: every constant with its value, the `Controls` aggregate, and the
  in-class `static const Controls kX;` declarations.
- After the class, the definitions
  `inline constexpr SpectralShapeRecipe::Controls SpectralShapeRecipe::kX{.depth = …, .body = …, .bodyCurvature = …, .bodyEmphasis = …, .edge = …, .shift = …};`
  with exactly the S3.5 values. Clang and GCC reject in-class nested-aggregate NSDMIs (plan S3.1, R-7).
- `sanitize`: a field with `!detail::isFinite(x)` takes `kDefaultControls`' value; then clamp.
- `baseExponent` is `constexpr` (`1 + 2·depth`), and
  `capFrequency(sr) = kNyquistCapFraction * static_cast<float>(sr) * 0.5f`.
- Declare `evaluateShape`, `evaluateMask`, `applyMask` and `evaluate` with their S3.1 signatures and **empty
  bodies** marked `// T006` / `// T008`.
- Includes are exactly those of S3.1 (`core/db_utils.h`, `processors/harmonic_types.h`, and the std headers), all
  L0 or L2.

**Helpers header** (`spectral_shape_recipe_test_helpers.h`, `namespace Krate::DSP::ProfundumTest`, all `inline`):
- `struct Descriptors { double eSub, eBody, ePres, eTotal, rBodyDb, rPresDb, h1RestDb, centroidOct, spreadOct, oddEvenDb, oddEvenClippedDb; };`
- `Descriptors describe(std::span<const float> a)` (wₙ = aₙ²) and `Descriptors describePowers(std::span<const double> w)`.
  Both compute in `double`, exactly per spec "Descriptor definitions":
  - E_sub = w₁, E_body = n 2–8, E_pres = n ≥ 9.
  - `h1RestDb` is +inf when rest == 0.
  - C = Σ log₂(n)·wₙ / Σ wₙ, and σ is the energy-weighted standard deviation of log₂ n.
  - Odd/even is Σ(odd n ≥ 3) / Σ(even n), in dB: +inf when evens are 0, −inf when the odd n ≥ 3 sum is 0. The
    clipped value is clamped to [−30, 30].
- `double descriptorDistance(const Descriptors&, const Descriptors&)`: Euclidean over (R_body dB, R_pres dB, 6·C,
  6·σ). `double bodyColourDistance(...)` adds the clipped odd/even axis.
- `std::vector<float> shapeOf(const SpectralShapeRecipe::Controls&, int N)` and
  `std::vector<float> fullOf(const Controls&, float f0, double fs, int N)`. These are test-side, so allocation is
  fine.
- `SpectralShapeRecipe::Controls gridPoint(int iD, int iB, int iE, int iS)`: {0, 0.5, 1} for D, B and E,
  {−1, 0, 1} for S, curvature 0.5, emphasis 0. Also `midGrid()`.
- `SpectralShapeRecipe_DescriptorHelpersSelfCheck`, in the Recipe TU:
  - a = {1, 0, …} (N = 16) gives `h1RestDb == +inf` and `centroidOct == 0`.
  - w with only w₂ = 1 gives `centroidOct == 1` and `spreadOct == 0`.
  - a = {1, 1} gives `oddEvenDb == -inf` and `oddEvenClippedDb == -30`.

**Verify:** build `dsp_processors_tests` with zero warnings, then run `"SpectralShapeRecipe_*"`: every case passes.

---

## Group 4 — shape law (sequential)

### T006 `evaluateShape`: the S3.2 law (FR-003/005/010/013/015/017/018/021/023, SC-002 recipe, SC-006 recipe, SC-022 recipe)

Files: `spectral_shape_recipe.h` and `spectral_shape_recipe_test.cpp`.

**Failing tests first:**
- `SpectralShapeRecipe_WaveformAnchors` (SC-006), at N = 64 and N = 96:
  - `kSineAnchor`: rest ≤ −60 dB re h1. The model gives −77.6.
  - `kTriangleAnchor`: odd n ≤ 15 within ±1.5 dB of 1/n² re h1. Every even element is exactly `0.0f`, so
    ≤ −50 dB.
  - `kSawAnchor`: n ≤ 16 within ±1.5 dB of 1/n; 17 ≤ n ≤ N within ±3 dB.
  - Triangle and saw: relative L2 error ‖a/a₁ − r‖₂/‖r‖₂ ≤ 0.05, with r₁ = 1.
- `SpectralShapeRecipe_DepthMonotonic` (SC-002): a 33-point depth sweep over [0, 1], plus `kDepthTriangle`
  inserted in order, at each of the 27 `gridPoint`s, N = 64. h1/rest dB is strictly increasing.
- `SpectralShapeRecipe_DepthWalksWaveformPath` (SC-002, FR-018), at body = edge = 0, emphasis 0, curvature 0.5,
  shift 0, N = 64:
  - The least-squares slope of ln aₙ against ln n over n = 1…15 is −1 ± 0.05 at depth 0 and −2 ± 0.05 at
    depth 0.5.
  - Rest ≤ −60 dB re h1 at depth 1.
- `SpectralShapeRecipe_DepthOneSineAtAnyShape` (FR-010): depth 1 over body, edge, curvature ∈ {0, 0.5, 1} × shift
  ∈ {−1, 0, 1} (81 points), at emphasis 0 and −1, for N = 64 and 96. Rest ≤ −30 dB re h1; the model's worst is
  −43.8. Print the worst value.
- `SpectralShapeRecipe_EmphasisActsOnWholeVector` (SC-022 recipe):
  - At emphasis −1, over the 27 grid points and over mid grid × edge ∈ {0, 0.5, 1} × curvature ∈ {0, 0.5, 1},
    every even element is exactly `0.0f`. Spot-check n ≥ 10 at edge = 1.
  - FR-023 floor: at every step of a 33-point emphasis sweep over [−1, 0] on the same grid, odd/even dB ≥ the
    emphasis-0 value at that point.
- `SpectralShapeRecipe_PowerNormalised` (shape arm, FR-005/FR-003): 10 000 random `Controls`, uniform per range
  with `mt19937{0x5EED}`, at N ∈ {1, 2, 16, 64, 96}:
  - |Σa² − kPowerTarget|/kPowerTarget ≤ 1e-4.
  - Every element is finite and ≥ 0.
  - a₁ > 0.
- `SpectralShapeRecipe_ControlsSanitize` (second arm): `evaluateShape` of each out-of-range input from T005 is
  `memcmp`-identical to `evaluateShape` of the clamped input.
- `SpectralShapeRecipe_ShapeEdgeCases`:
  - `out.size() == 1` gives a₁² = P0 ± 1e-4.
  - `out.size() == 0` writes nothing and does not crash.
  - `out.size() == 128` pre-filled with −1 writes the first 96 elements and leaves elements 96…127 at −1.

**Implement** plan S3.2 exactly:
- Sanitise first.
- u = `std::log2(static_cast<float>(n))`; p = 1 + 2·depth.
- R = 1 − (1 − kSineResidual)·S((depth − 0.5)/0.5), where S(x) = x²(3 − 2x) clamped to [0, 1].
- b = body².
- u_B = kBodyCentreOct + kShiftCentreOct·shift; u_E = kEdgeCentreOct + kShiftCentreOct·shift.
- σ_B = kBodyWidthBroadOct + (kBodyWidthNarrowOct − kBodyWidthBroadOct)·curvature.
- comp = exp2(kShiftGainOct·shift); peak = (kBodyWidthRefOct/σ_B)^kBodyPeakExponent.
- G_B = exp(−(u − u_B)²/(2σ_B²)). G_E = 1 if u ≥ u_E, otherwise exp(−(u − u_E)²/(2·kEdgeFlankOct²)).
- env = 1 + comp·(kBodyGain·b·peak·G_B + kEdgeGain·edge·G_E); par = 1 + emphasis on even n.
- g₁ = 1, and gₙ = R·exp2(−p·u)·env·par.
- Accumulate Σg² in `double` and scale by sqrt(P0/Σg²).
- Per-harmonic transcendentals stay `float`.

**Verify:** build `dsp_processors_tests` with zero warnings, then run `"SpectralShapeRecipe_*"`: all pass.

---

## Group 5 — vector audibility and Lipschitz gates (sequential)

### T007 SC-004 / SC-005 / SC-019 vector arms and the L_c ceiling (FR-006, FR-011/012/015/016, plan C-1/C-2/C-3)

Files: `spectral_shape_recipe_test.cpp`. Edit `spectral_shape_recipe.h` only for the R-1 fallback below.

**Failing tests first.** These are new cases. The law exists from T006, so a failure here is a law or constant
defect; diagnose it against `recipe-model.js`.
- `SpectralShapeRecipe_AudibilityGate_BodyEdgeShift` (SC-004 recipe arm, N = 64, curvature 0.5, emphasis 0).
  Each 33-point sweep runs at the 27 grid points of the *other* three controls:
  - **Body:**
    - R_body is strictly increasing at all 27 points.
    - Compute headroom = −R_body(body = 0) in the test. Where headroom ≥ 6 dB, span ≥ 6 dB; the model minimum is
      8.65 dB. At the other points, span ≥ 0.7 × headroom; the model minimum is 0.740.
    - Assert that exactly 5 points fall in the low-headroom set, so a constant change cannot silently move points
      between the sets. `// plan S1 C-1`
  - **Edge:** R_pres is strictly increasing with span ≥ 6 dB at all 27 points (model minimum 8.89 dB).
  - **Shift:**
    - Over depth ∈ {0, 0.5} × (body, edge) ∈ {0, 0.5, 1}², minus body = edge = 0 (16 points), C is strictly
      increasing with span ≥ 1 octave (model minimum 1.427).
    - At depth 1 with body + edge > 0 (8 points), C is strictly increasing on the float vectors.
    - At depth 1 with body = edge = 0, C is constant within 1e-6 relative. `// plan S1 C-2`
  - **Endpoints:** starting from mid grid, set body = 1, edge = 1 and shift = 1 in turn. The pairwise
    `descriptorDistance` between the three states is ≥ 6 dB (model minimum 7.07).
  - Print every minimum.
- `SpectralShapeRecipe_BodySubControls` (SC-005 vector arm, mid grid, N = 64):
  - σ is strictly **decreasing** over the 33-point curvature sweep, with |span| ≥ 0.25 octave. The model
    gives −0.375.
  - Odd/even is strictly decreasing over the 33-point emphasis sweep on [−1, 0]. The first point is +inf. From the
    second point to the last, the span is ≥ 20 dB (model: 34.8 → 4.7 dB).
- `ProfundumCore_BodyColoursDistinct` (SC-019 recipe arm; spec-mandated name; tag `[processors][profundum]`),
  N = 64:
  - Pairwise `bodyColourDistance` of the five `kBody*` coordinates is ≥ 6 dB (model minimum 16.24).
  - `kBodyRound` has the lowest C, by ≥ 0.05 octave (model 0.375).
  - `kBodyHollow` has the highest odd/even, by ≥ 1 dB, using the unclipped value (model 18.9).
  - `kBodyNasal` has the smallest σ, by ≥ 0.05 octave (model 0.064).
  - `kBodyThick` has the highest R_body, by ≥ 1 dB (model 1.27).
- `SpectralShapeRecipe_LipschitzBound` (control arm, SC-010(c), FR-006), N = 96:
  - 10 000 random base states (`mt19937{0x5EED}`). For each, choose one control at random and move it by
    Δ ∈ (0, 0.01], kept inside its range.
  - max|Δaₙ|/Δ ≤ `kLipschitzControlCeiling`·√P0, which is 8·√P0. The model measured 7.13. Print the worst value.

**R-1 fallback, only if the L_c case fails:**
- Change `kBodyWidthNarrowOct` from 0.15 to 0.17 in **both** `recipe-model.js` (`K.bodyWidthNarrow`) and the
  header.
- Re-run the model until it is ALL PASS, update plan S3.6, and re-run this group.
- Never raise the 8·√P0 ceiling.

**Verify:** run `dsp_processors_tests.exe "SpectralShapeRecipe_*"` and `"ProfundumCore_BodyColoursDistinct"`.
All pass. Record the printed minimums under "Run log".

---

## Group 6 — mask stage (sequential)

### T008 `evaluateMask`, `applyMask`, `evaluate` (FR-030/031/032, FR-051 stages; SC-003 / SC-007 / SC-010(c) f0 arm / SC-011(b–d) / SC-015 / SC-016 ceiling / SC-021(a), recipe side)

Files: `spectral_shape_recipe.h` and `spectral_shape_recipe_test.cpp`.

**Failing tests first:**
- `SpectralShapeRecipe_GuardMonotone` (FR-030): `evaluateMask` at 22 050, 48 000 and 192 000 Hz, N = 96, with f0
  from 32.70 Hz down to 8 Hz in 1-cent steps.
  - mask₁ == 1.0f exactly.
  - For every n ≥ 2, maskₙ is strictly decreasing as f0 falls.
  - maskₙ == 1.0f for f0 ∈ [32.70, 60] Hz at all three rates, because there is no cap overlap (S3.3).
  - At every f0, maskₙ is non-increasing in n.
  - After `applyMask`, h1's share of E_total is non-decreasing as f0 falls, at mid grid and at `kSawAnchor`.
- `SpectralShapeRecipe_LowNoteGuardContinuity` (SC-011(b–d)):
  - **(b)** For f0 ∈ {32.70, 40, 65.41, 130.81} Hz at 48 kHz, N = 64, mid grid, `evaluate` equals
    `evaluate(…, 32.70f, …)` within 1e-6 relative per element.
  - **(c)** Use the 14 states: the 6 named coordinates, the 5 colours, mid grid, and the corner
    `{.depth = 0, .body = 1, .bodyCurvature = 1, .bodyEmphasis = 0, .edge = 1, .shift = 1}`. Sweep MIDI 0–127 in
    1-cent steps (f0 = 440·2^((m − 69)/12), clamped to `capFrequency`) at 44.1, 48 and 96 kHz, N = 96.
    - If element n is ≥ −80 dB re a₁ in both adjacent vectors, |20·log10(aₙ′/aₙ)| ≤ 0.1 dB.
    - Otherwise |Δaₙ| ≤ 8·√P0/1200.
    - Print the worst values; the model gives 0.0900 dB and 0.001 × the bound. `// plan S1 C-8`
  - **(d)** At mid grid and at `kSawAnchor`, h1's share at f0 ∈ {8, 12, 16.35, 24, 32} Hz is ≥ its share at
    32.70 Hz.
- `SpectralShapeRecipe_CapTaperExact` (FR-032, SC-016 ceiling clause):
  - For n ≥ 2 with n·f0 ≥ capHz, maskₙ == 0.0f exactly.
  - At f0 = `capFrequency(fs)`, for fs ∈ {22 050, 44 100, 48 000, 96 000, 192 000}, `evaluate` is h1-only:
    elements n ≥ 2 are 0.0f and a₁² = P0 ± 1e-4 relative.
  - Taper start = capHz·2^(−1430/1200). At 48 kHz it is 8 406 ± 1 Hz.
  - A harmonic whose n·f0 is 1 cent below the taper start has mask == 1.0f (or the guard value).
- `SpectralShapeRecipe_ShapeMaskComposition` (SC-021(a)): 10 000 random Controls × f0 drawn log-uniform over
  [8, capHz] at 44.1, 48 and 96 kHz, N = 96. `evaluateShape` → `evaluateMask` → `applyMask` equals `evaluate`
  within 1e-6 relative per element, with an absolute floor of 1e-12 for zero elements.
- `SpectralShapeRecipe_PowerNormalised` (full arm, SC-003): the same 10 000-sample set through `evaluate` gives
  |Σa² − P0|/P0 ≤ 1e-4, with every element finite and ≥ 0.
- `SpectralShapeRecipe_LipschitzBound` (f0 arm, SC-010(c)): 10 000 random states and f0 pairs at most 1 cent
  apart, at 3 rates, N = 96.
  - Sample a third of the pairs straddling the onset (30–35 Hz), a third inside the cap taper of some n, and a
    third within 10 cents of the clamp ceiling.
  - max|Δaₙ|/Δoct ≤ 8·√P0 per octave (model 4.78).
- `SpectralShapeRecipe_NamedDistributions` (SC-007 recipe), at C1 32.70, C2 65.41 and C3 130.81 Hz, 48 kHz, N = 64:
  - `kHeavy`: E_sub > E_body > E_pres.
  - `kHollow`: E_body > E_sub > E_pres.
  - `kGrowl`: E_pres > E_body > E_sub.
  - The dominant region exceeds the second by ≥ 3 dB. The model's minimums are 7.5, 25.7 and 12.8 dB.
- `SpectralShapeRecipe_NoAllocation` (SC-015 recipe):
  - `AllocationDetector::instance().startTracking()`, then 1 000 calls each of `evaluateShape`, `evaluateMask`,
    `applyMask` and `evaluate` on `std::array` spans, then `REQUIRE(stopTracking() == 0)`.
  - A `static_assert(noexcept(...))` for each of the four calls.
- `SpectralShapeRecipe_MatchesCalibrationModel` (plan S11 step 2):
  - In the scratchpad, outside the repo, write a Node snippet that `require`s
    `specs/profundum-phase1-harmonic-core/recipe-model.js`. It prints `full(c, 65.41, 48000, 64)` with
    9 significant digits for mid grid, `kGrowl` and the +44.28 dB corner.
  - Paste the three vectors into the test as `constexpr std::array<double, 64>`.
  - Assert |a_cpp − a_model| ≤ 1e-5·√P0 per element. This is a tolerance, not a golden.
  - Add a comment: "values from recipe-model.js; regenerate after ANY constant change".

**Implement** plan S3.3 exactly:
- octBelow = max(0, log2(kLowNoteGuardOnsetHz/f0)). Compute guard(n) = exp2(−kGuardSlopePerOctave·octBelow·u)
  only when octBelow > 0.
- capHz = `capFrequency(fs)`, and y = 1200·log2(capHz/(n·f0)) for n ≥ 2. Compute the cap factor only for n with
  n·f0 > capHz·2^(−1430/1200). The cap factor is:
  - 1 for y ≥ 1430;
  - 10^(−0.09·(1430 − y)/20) on the dB-linear part;
  - 10^(−D/20)·y/10 on the tail y ≤ 10, with D = 0.09·1420 = 127.8 dB;
  - exactly 0 for y ≤ 0.
- mask₁ = 1.
- `applyMask`: oₙ = shapeₙ·maskₙ, renormalised to P0 with Σ in `double`. `out` may alias `shape`.
- `evaluate` is literally evaluateShape → evaluateMask → applyMask, with the mask in a stack
  `std::array<float, kMaxPartials>`.
- A non-finite or non-positive `f0Hz` is treated as the onset (identity guard). The core sanitises first; this
  only guarantees the recipe never writes NaN. Document it in the header.

**Verify:**
- `dsp_processors_tests.exe "SpectralShapeRecipe_*"`: all pass, with zero warnings.
- `node specs/profundum-phase1-harmonic-core/recipe-model.js`: still ALL PASS.

---

## Group 7 — core skeleton (sequential)

### T009 `ProfundumCore` API, state, `prepare`/`reset`, unprepared silence; core test helpers (FR-040/041/061, SC-016 counts, SC-023 static_assert, plan S4.1–S4.3)

Files: `profundum_core.h` (replace the stub), `profundum_core_test.cpp` and the **new**
`profundum_core_test_helpers.h`.

**Failing tests first** (Core TU, tag `[systems][profundum]`):
- `ProfundumCore_KMaxPartialsUntouched` (SC-023): `static_assert(Krate::DSP::kMaxPartials == 96)`.
- `ProfundumCore_PartialCountExtremes` (count arm):
  - A default-constructed core reports `isPrepared() == false`.
  - `prepare(48000, 1)` gives `numPartials() == 1`, and `prepare(48000, 96)` gives 96.
  - `prepare(48000, 128)` gives 96, and `prepare(48000, 0)` gives 1.
  - `prepare(48000)` gives 64 (`kDefaultPartials`).
- `ProfundumCore_SampleRateExtremes` (clamp arm):
  - `maxShapeStepPerInterval()` equals 375·√P0·64/fs within 1e-6 relative at 22 050, 48 000 and 192 000 Hz. At
    48 kHz that is 0.25119.
  - `prepare(16000)` is bit-equal to `prepare(22050)`, and `prepare(400000)` is bit-equal to `prepare(192000)`.
- `ProfundumCore_BeforePrepareSilent` (FR-061 / E-2):
  - An unprepared core, with L and R pre-filled with 1.0f, writes 0.0f to all 256 samples of both.
  - `processBlock(nullptr, R, 64)`, `processBlock(L, nullptr, 64)` and `processBlock(L, R, 0)` leave the
    pre-filled buffers untouched.
  - Calling every setter and `noteOn(110.0f)` before `prepare` is a no-op: afterwards, `prepare(48000)` plus the
    same calls renders `memcmp`-equal to a fresh core's render. Assert this from T010 on, once `noteOn` works; in
    this task, assert only that no state changed (`isPrepared() == false`).
- `ProfundumCore_NoexceptContract` (SC-015 static part): `static_assert(noexcept(...))` for `prepare`, `reset`,
  `setControls`, `setFrequency`, `setRetriggerPhase`, `noteOn`, `setPartialPanOffsets`, `processBlock` and every
  observer.
- `ProfundumCore_PrepareDefaults`:
  - After `prepare(48000)`, `shapeGains().size() == 64` and it equals `evaluateShape(kDefaultControls)` bitwise.
  - `currentFrequency() == 55.0f` and `baseFrequency() == 55.0f`.
  - A 1 024-sample render before any `noteOn` is all exact zeros, both channels.
  - `stateFinite()` is true.

**Implement:**
- The plan S4.1 public API, verbatim: the enum, every constant, deleted copy, defaulted move, each method, and the
  FTZ/DAZ `@note`.
- Includes: `core/db_utils.h`, `core/math_constants.h`, `processors/harmonic_oscillator_bank.h`,
  `processors/harmonic_types.h` and `processors/spectral_shape_recipe.h`. All are L0 or L2; nothing at L3 or L4.
- The S4.2 members.
- `prepare`: S4.3 steps 1–6.
- `reset`: `bank_.reset()`, **not** `bank_.prepare`, then steps 3–6, keeping `pendingControls_`.
- `processBlock`:
  - The E-2 guard first, then zero-fill when `!prepared_`.
  - Otherwise run the grid loop, rendering `bank_.processStereoBlock` in runs of
    ≤ `kPitchUpdateInterval − intervalPhase_ % kPitchUpdateInterval`, with `intervalPhase_` advancing modulo
    `kControlInterval`.
  - `controlUpdate`, `pitchUpdate` and `applyNoteOn` are stubs marked `// T010` / `// T011`.
- Observers return spans over the first `numPartials_` elements.

**Helpers header** (`profundum_core_test_helpers.h`, `namespace Krate::DSP::ProfundumTest`, all `inline`). It
includes `low_frequency_metrics.h`, `artifact_detection.h`, `allocation_detector.h`,
`<krate/dsp/primitives/fft.h>` and `"../processors/spectral_shape_recipe_test_helpers.h"`.
- `void requireLREqual(const float* L, const float* R, std::size_t n)`:
  `REQUIRE(std::memcmp(L, R, n * sizeof(float)) == 0)`.
- `struct Render { std::vector<float> L, R; std::vector<std::vector<float>> shapes, delivered; std::vector<float> maskF0, baseF0; };`
- `Render renderCore(ProfundumCore& core, const SpectralShapeRecipe::Controls& c, float f0, double seconds, ProfundumCore::RetriggerPhase policy = ProfundumCore::RetriggerPhase::Reset, bool record = false, std::size_t block = 64, const float* f0Trajectory = nullptr)`.
  The caller prepares the core.
  - It calls `setRetriggerPhase`, `setControls` and `noteOn(f0)`, then renders in `block`-sized blocks, passing
    the matching slice of `f0Trajectory` when that is non-null.
  - With `record`, it appends `shapeGains()`, `deliveredGains()`, `maskFrequency()` and `baseFrequency()` after
    every block.
  - A grid-aligned 64-sample block holds exactly one control update, at its first sample (plan S8.2).
- `double rmsDbOverPeriods(const float* x, std::size_t n, double fs, double f0, double minSeconds = 1.0)`: RMS in
  dBFS re 1.0, over the largest integer number of f0 periods that is ≥ minSeconds and fits in n.
- `std::vector<double> bh7Window(std::size_t L)`: periodic, `double`, with plan S8.2 coefficients
  {0.27105140069342, 0.43329793923448, 0.21812299954311, 0.06592544638803, 0.01081174209837, 0.00077658482522, 0.00001388721735}
  and w[k] = Σⱼ(−1)ʲaⱼcos(2πjk/L).
- `double goertzelPhase(const float* x, std::size_t L, double fs, double hz)`: a BH7-windowed single-bin complex
  DFT in `double`, returning `atan2`.
- `std::vector<double> renderedHarmonicPowers(const float* x, double fs, double f0, int N)`, on a frame of
  `lowFreq::kLowFrequencyFftSize` samples.
  - REQUIRE `lowFreq::magnitudeSpectrum(...)` to return true, and
    `lowFreq::analysisFft(lowFreq::kLowFrequencyFftSize).isPrepared()`.
  - wₙ = `harmonicMainLobePower(mags, n·f0, fs/262144.0)` for n·f0 < fs/2, else 0.
- `Descriptors describeRendered(const float* x, double fs, double f0, int N)` = `describePowers(renderedHarmonicPowers(...))`.
- `std::size_t aliasFftLength(double fs, double f0)`: the smallest power of two ≥ max(2¹³, 64·fs/f0).
- `struct AliasResult { double aliasedDbfs; double excludedFraction; double totalDb; };` and
  `AliasResult aliasedPower(const float* x, std::size_t fftLen, double fs, double f0)`:
  - Apply BH7 and run `FFT::prepare(fftLen)` + `forward`.
  - Scale bin powers so their sum equals mean((x·w)²)/mean(w²) (Parseval; spec SC-009).
  - Exclusions: ±8 bins around every n·f0 < fs/2, and bins 0–8.
  - `aliasedDbfs` = 10·log10(Σ non-excluded bins 1…L/2 / 0.5). `totalDb` is the same over all bins, for the
    SC-006 "re total" variant.
- `struct SidebandResult { double powerDbReTotal; double remainingFraction; };` and
  `SidebandResult sidebandPower(const float* x, double fs, double f0, std::size_t controlInterval, std::size_t pitchInterval /*0 = none*/, double amRateHz, double vibratoPeakHz /*0 = none*/)`.
  This is spec SC-010(b) exactly:
  - BH7 window, 2¹⁷ FFT.
  - Region = ±20 Hz around k·fs/I ± n·f0, and (when pitchInterval ≠ 0) k·fs/U ± n·f0, for k = 1, 2 and every n
    whose level is ≥ −80 dB re h1.
  - Minus the intended-modulation zones around every n·f0: ±(5·amRateHz + 7 bins) for the triangle AM (2 Hz → ±(10 Hz
    + 7 bins), first five odd orders), and for vibrato the Carson band ±(n·vibratoPeakHz + 2·5 Hz + 7 bins).
  - `remainingFraction` = the remaining bins over the nominal bins.
- `std::vector<float> logLinearRamp(float f0a, float f0b, std::size_t n)`.
- `ProfundumCore_TestHelpersSelfCheck`:
  - `bh7Window(1024)[512] == Approx(1.0)`, and `[0] == Approx(a0 − a1 + a2 − a3 + a4 − a5 + a6).margin(1e-12)`.
  - `aliasedPower` of a full-scale 1 kHz sine at 48 kHz (fftLen 8192, f0 = 1000) gives `aliasedDbfs ≤ -150`.
  - Adding a 0.001-amplitude tone at 1 333.3 Hz gives `aliasedDbfs > -80`. This is the teeth.
  - `goertzelPhase` of sin(2π·100t + 0.3) over 4 800 samples is 0.3 − π/2 ± 1e-6 rad.
  - `aliasFftLength(96000.0, 8.1758)` == 1 << 20.

**Verify:** build `dsp_systems_tests` with zero warnings, then run `"ProfundumCore_*"`: all pass.

---

## Group 8 — control path and `Reset` note-on (sequential)

### T010 Latch → slew → mask → deliver → `loadFrame`; `applyNoteOn` (`Reset` and first note) (FR-042/045/046/049/050/051; SC-003 render, SC-010(d), SC-012 Reset arm, SC-017 determinism, SC-018, SC-021(d,e); E-11; plan S4.4, S4.5, S4.7, C-9)

Files: `profundum_core.h` and `profundum_core_test.cpp`. Every all-zero-pan render calls `requireLREqual`.

**Failing tests first:**
- `ProfundumCore_FirstNoteSeedsPhases`: `prepare(48000)`, mid grid, `noteOn(65.41f)`, then one 64-block. Assert
  `L[0] == 0.0f` and `R[0] == 0.0f`. Repeat on a fresh core with `setRetriggerPhase(FreeRunning)`.
- `ProfundumCore_RetriggerPhasePolicy` (Reset arm, SC-012):
  - Core A renders 0.3 s at `kGrowl`, 130.81 Hz, in 64-blocks.
  - Core B renders 34 080 samples (0.71 s) at `kSawAnchor`, 32.70 Hz, in 37-sample blocks, so its grid phase
    differs.
  - Then both receive `setControls(midGrid())` and `noteOn(65.41f)` (Reset) and render 0.5 s in 64-blocks.
  - The two post-`noteOn` L buffers are `memcmp`-equal, and so are the R buffers. `// plan S1 C-9`
  - Post-`noteOn` `L[0] == 0.0f`.
  - Repeat the comparison with core B cleared by `reset()` instead of a long prior render (same `memcmp` result).
  - Also complete `ProfundumCore_BeforePrepareSilent`'s deferred arm: pre-prepare setter/`noteOn` calls are no-ops,
    so `prepare` + the same post-prepare calls render `memcmp`-equal to a fresh core.
- `ProfundumCore_NoteOnMaskImmediate` (SC-021(d)): f0 ∈ {1 000, 3 000, 9 000} Hz at 48 kHz (cap 19 200 Hz),
  N = 64, mid grid. In the first block after `noteOn`:
  - `deliveredGains()[n−1] == 0.0f` for every n ≥ 2 with n·f0 ≥ 19 200;
  - Σ = P0 ± 1e-4.
- `ProfundumCore_ShapeGainStepCeiling` (SC-010(d), control arm): C2 65.41 Hz, mid grid, 64-blocks, `record = true`.
  For each of D, B, E and S, run three fixtures:
  1. a 0 → 1 → 0 step with 0.5 s holds (shift: −1 → +1 → −1);
  2. a 10 ms full-range linear sweep, with the controls set before every block;
  3. a 2 s, 2 Hz full-range triangle.

  Assertions:
  - Over consecutive recorded shape vectors, max|Δ| ≤ `maxShapeStepPerInterval()`.
  - Every shape and delivered vector has |Σa² − P0|/P0 ≤ 1e-4.
  - The shape equals `evaluateShape(target)` bitwise within 4 intervals after each step (√2/0.5 ≈ 2.83 → 3, plus
    1).
  - **Reset exemption:** the first recorded shape after a `Reset` `noteOn` to a far target equals
    `evaluateShape(target)` bitwise.
- `ProfundumCore_OutputLevelConstant` (SC-018): the 6 named coordinates, 5 colours and mid grid, at C1, C2 and C3,
  at 44.1, 48 and 96 kHz.
  - Render 1.2 s, skip 150 ms, then `rmsDbOverPeriods(L, …, 1.0)` == −12.0 ± 0.1 dB.
  - If this case takes over 15 s, move the 96 kHz arm to a `[long]` sibling (toolchain-independent) and keep the
    48 kHz arm per-push.
- `ProfundumCore_DepthLoudnessFlat` (SC-003 render, `[long]`): 48 kHz, the 33-point depth sweep plus
  `kDepthTriangle`, at the 27 grid points, at C1/C2/C3.
  - Each step is a fresh `Reset` render of 1.15 s, measured with `rmsDbOverPeriods` over ≥ 1 s after 150 ms.
  - Every step is within ±0.5 dB of the sweep median at its grid point.
  - Per-push sibling `ProfundumCore_DepthLoudnessFlatSmoke`: C2, mid grid and the 8 grid corners only.
- `ProfundumCore_Deterministic` (SC-017): two cores receive the same scripted sequence (control changes, `noteOn`
  under both policies, a 2 s render in 37-blocks). L and R are `memcmp`-equal.
- `ProfundumCore_ZeroTargetLanesUnderFtz` (E-11):
  - `#if KRATE_HAS_SSE_DENORMAL_CONTROL`: `REQUIRE(_MM_GET_FLUSH_ZERO_MODE() == _MM_FLUSH_ZERO_ON)`.
  - Render `kTriangleAnchor` at C2 for 5 s, and C6 (1 046.5 Hz, capped lanes) for 5 s, at 48 kHz.
  - Every sample passes `detail::isFinite`, `stateFinite()` holds, and every even delivered gain is 0.0f for the
    triangle.
- `ProfundumCore_ControlSkipRule` (FR-042): after convergence, with nothing changing for 100 intervals:
  - `shapeGains()`, `deliveredGains()` and `baseFrequency()` are bit-unchanged from block to block;
  - `fingerprintRender` of seconds 5–6 and 8–9 compare within `kMetricTolerance` (`compareFingerprints`).
  - The CPU effect of the skip rule is measured by T021's static figure.

**Implement:**
- Plan S4.4 `controlUpdate`, steps 1, 2, 4, 5 and 6. Step 3 here is baseNew = f with no chase (T011), and step 7
  (pan) is T012.
- S4.5 `slewShapeTowardTarget`, in `double`:
  - chord = ‖t − s‖₂. If chord ≤ Δmax, snap and set `shapeConverged_ = true`.
  - Otherwise Ω = acos(clamp(⟨s,t⟩/P0, 0, 1)), φ = 2·asin(0.999·Δmax/(2√P0)), and
    s ← (sin(Ω − φ)·s + sin(φ)·t)/sin(Ω). Then renormalise to P0.
- Delivered |x| < 1e-12f is flushed to 0.0f.
- S4.7 `applyNoteOn`, for `Reset` or the first note (`!sounding_`):
  - Set `heldF0_ = currentF0_ = noteOnF0_`, then latch the controls and evaluate the target.
  - `bank_.reset()`, `detuneShadow_.fill(1.0f)`, `shape_ = targetShape_`, `intervalPhase_ = 0`,
    `panDirty_ = panNonZero_`.
  - Compute the mask at f and deliver; set `baseF0_ = f`; `frame_` amplitudes and frequencies; then
    `bank_.loadFrame(frame_, f, true)`.
  - Set `sounding_ = true` and clear `noteOnPending_`.
- `noteOn(f)` stores `noteOnF0_ = sanitizeF0(f, heldF0_)` and sets `noteOnPending_`. `setFrequency` sets `heldF0_`.
- `sanitizeF0`, per S4.8: if `!detail::isFinite(x)`, return `prev`; otherwise clamp to [8, capHz_].
- `setControls`, per S4.8: a non-finite field keeps the *pending* value; otherwise clamp. `controlsDirty_` is set
  only on a bitwise change.
- Until T011, a `FreeRunning` `noteOn` while sounding may take the Reset branch.

**Verify:** run `dsp_systems_tests.exe "ProfundumCore_*"`: all pass, with zero warnings. Record wall clocks and
apply `[long]` per rule 5.

---

## Group 9 — pitch path and `FreeRunning` (sequential)

### T011 Exact-ε multipliers, the 0.9-semitone base chase, `f0PerSample` decimation, `FreeRunning` note-on (FR-043/044/046; SC-010(a); SC-012 FreeRunning arms; SC-020(a–c); SC-021(b,c); E-7; plan S4.6, S4.7)

Files: `profundum_core.h` and `profundum_core_test.cpp`.

**Failing tests first:**
- `ProfundumCore_PitchTrajectoryNullptr` (SC-020(a)): after `setFrequency(98.0f)`, a 1 s render with `nullptr` is
  `memcmp`-equal to a twin render whose `f0PerSample` is filled with 98.0f.
- `ProfundumCore_PitchTrajectoryBlockSizeIndependent` (SC-020(b)): two trajectories, each 1 s:
  - a 10 ms log-linear 24-semitone drop, 130.81 → 32.70 Hz, starting at 0.2 s and then held;
  - a 5 Hz ±2-semitone vibrato around 65.41 Hz.

  Render each with host blocks of 512, 64 and 37 samples and no setter calls between blocks. The three renders
  are `memcmp`-equal for L and for R.
- `ProfundumCore_PitchTrajectorySampleAccurate` (SC-020(c)): `kSineAnchor`, 48 kHz.
  - A 130.81 → 32.70 Hz log-linear drop from sample 100 to sample 400 of one 512-block. Before that, hold
    130.81 Hz for 0.5 s.
  - Measure the h1 phase with `goertzelPhase` over a one-period window around each check point. Compare it with
    2π∫f0 dt, accumulated in `double` from the trajectory, using a constant offset fitted before the drop.
  - |error| ≤ 0.5·(16/48000)·|32.70 − 130.81| + 0.02 = 0.0364 cycle, at the end of the drop and at +100 ms.
- `ProfundumCore_DetuneShadowNoDrift` (plan-internal, `[long]`):
  - 10⁶ pitch updates (333.3 s at 48 kHz, U = 16) of a 5 Hz ±2-semitone vibrato at C2, in 512-blocks.
  - Then 2 s static at C2. The h1 frequency error is < 0.01 cent, by the SC-001 method: phase advance between two
    1 s BH7 frames whose starts are 1 s apart.
  - Per-push sibling `ProfundumCore_DetuneShadowNoDriftSmoke`: 10⁵ updates.
- `ProfundumCore_NoCrossfadeOnGlide` (FR-043): grid-aligned 64-blocks, `record = true`. Three fixtures:
  - the 24-semitone 10 ms drop;
  - the SC-009 bend legs, MIDI 24 → 36 in 1-semitone legs, each a 10 ms log-linear `f0PerSample` ramp plus a
    50 ms hold;
  - a 2 s, 5 Hz ±2-semitone vibrato.

  Assertions:
  - For consecutive intervals with no `noteOn` between them, |12·log2(base_k/base_{k−1})| ≤ 0.9
    (`kMaxBaseStepSemitones`).
  - In the bend and vibrato fixtures, `baseFrequency() == maskFrequency()` bitwise after every block.
  - **Positive control:** a `FreeRunning` `noteOn` 2 semitones away shows as a 2 ± 1e-4 semitone base step at
    that interval.
- `ProfundumCore_MaskNeverSlewedBend` (SC-021(b,e)), controls converged at mid grid. Three fixtures:
  - the SC-009 legs at 48 kHz: MIDI 24 → 60 → 24 per-push, with 24 → 108 → 24 in a `[long]` sibling;
  - a ±12-semitone 50 ms bend at C2;
  - a 24-semitone 10 ms drop from C2 through the guard onset.

  Per 64-block, |Δdelivered_n| ≤ 8·√P0·|log2(maskF0_k/maskF0_{k−1})| + 1e-6, and Σ = P0 ± 1e-4.
- `ProfundumCore_CapTracksPitchExactly` (SC-021(c)): over the same fixtures, `deliveredGains()[n−1] == 0.0f` for
  every n ≥ 2 with n·`maskFrequency()` ≥ capHz.
- `ProfundumCore_RetriggerPhasePolicy` (FreeRunning arms, SC-012):
  - **Same f0 and controls:** a mid-render `FreeRunning` `noteOn` is `memcmp`-equal to an uninterrupted render.
  - **+0.5 semitone,** at `kSawAnchor` and at mid grid, C2. Pre-render to choose a block-aligned k0 where
    |y| ≥ 0.25 × the saw anchor's peak. Then:
    - |y[k0] − y[k0−1]| ≤ the max adjacent delta over the preceding 100 ms;
    - `y[k0] != 0.0f`.
- `ProfundumCore_LargeFreeRunningJumpBounded` (E-7): a `FreeRunning` `noteOn` ±12 and ±24 semitones from C2, at
  `kSawAnchor` and mid grid.
  - Every sample is finite with |y| ≤ `HarmonicOscillatorBank::kOutputClamp`.
  - `stateFinite()` holds.
  - The first delivered vector is zero at the new cap.
- `ProfundumCore_NoZipperControlSweeps` (SC-010(a) control arm): C2, mid grid,
  `ClickDetectorConfig{.sampleRate = 48000.0f, .frameSize = 512, .hopSize = 256, .detectionThreshold = 5.0f, .energyThresholdDb = -60.0f, .mergeGap = 5}`,
  `prepare()`, then `REQUIRE(detect(L.data(), L.size()).empty())` for:
  - each D/B/E/S 0 → 1 → 0 step with 0.5 s holds (shift −1 → +1 → −1);
  - each 10 ms full-range sweep.
- `ProfundumCore_NoZipperPitchBend` (SC-010(a) pitch arm): a ±12-semitone bend in 50 ms around C2, mid grid, via
  `f0PerSample`. The detection list is empty.
- `ProfundumCore_ClickDetectorPositiveControl` (SC-010(a) teeth):
  - For each step, render the start and end states as 1 s steady `Reset` renders at C2.
  - Splice them at sample 24 000: the first half of the start render, then the second half of the end render.
  - `detect(...).size() >= 1` for all four steps.
- `ProfundumCore_SampleRateExtremes` (remaining arms, FR-041 / E-6), at 22 050 and 192 000 Hz:
  - Output finite and `stateFinite()`.
  - SC-018 level at mid grid / C2 is −12 ± 0.1 dB.
  - SC-021(c) exactness holds over a ±12-semitone 50 ms bend.
  - The SC-010(d) step ceiling holds.

**Implement** plan S4.6:
- θ = π·f/fs in `double`.
- The Chebyshev recurrence s₀ = 0, s₁ = sin θ, c2 = 2cos θ, s_{k+1} = c2·s_k − s_{k−1} gives ε*_n = 2·s_n. Freeze
  ε*_n at 2·sin(π·capHz/fs) for n·f ≥ capHz.
- ε_base_n = clamp(2·sin(π·n·baseF0_/fs), ±1.99) in `double` (`epsBase_`), recomputed only when `baseF0_`
  changes.
- dTarget = |ε_base| > 1e-9 ? `static_cast<float>(ε*/ε_base)` : 1.0f; m = dTarget/detuneShadow_.
- `bank_.applyExternalFrequencyMultipliers(m)`, then `detuneShadow_[i] = detuneShadow_[i] * m[i]`. This single
  float multiply keeps the mirror bit-exact.
- `pitchUpdate(f)`: if f is bit-equal to `currentF0_`, return. Otherwise set `currentF0_` and apply the
  multipliers.
- `controlUpdate` step 3: baseNew = clamp(f, baseF0_·2^(−0.9/12), baseF0_·2^(0.9/12)). Step 6 applies the
  multipliers **before** `loadFrame`. Otherwise, when only f changed, step 6 refreshes the multipliers alone.
- `processBlock` reads `f0PerSample[i]` only where `intervalPhase_ % 16 == 0` (global grid). At block end it sets
  `heldF0_ = sanitizeF0(f0PerSample[n − 1], currentF0_)`.
- S4.7 `FreeRunning` while sounding:
  - Keep the MCF state, `shape_` and the grid.
  - Mask at the new f0 and deliver.
  - `baseF0_ = f` with **no** chase clamp, then the multipliers, then `loadFrame`.

**Verify:** run `dsp_systems_tests.exe "ProfundumCore_*"`: all pass, with zero warnings. Record wall clocks and
apply `[long]` per rule 5.

---

## Group 10 — pan hook (sequential)

### T012 `setPartialPanOffsets` and the FR-048 state machine (FR-048, FR-072, SC-014(a–c), SC-015; plan S4.4 step 7, S4.8)

Files: `profundum_core.h` and `profundum_core_test.cpp`.

**Failing tests first:**
- `ProfundumCore_ZeroPanBitIdentical` (SC-014(a)):
  - `requireLREqual` holds on every block across the 12 named, colour and mid states at C2, a ±12-semitone bend,
    and a 2 Hz control sweep.
  - An explicitly set all-zero vector, including `-0.0f` elements, renders `memcmp`-equal to a twin that never
    called the setter. This proves `applyPanOffsets` is not called.
- `ProfundumCore_PanHookForwards` (SC-014(b)): `o[i] = (i % 2 ? 0.5f : -0.5f)`, mid grid, C2. After the first
  control interval, ≥ 99 % of samples have L != R.
- `ProfundumCore_PanReturnToCentreBitIdentical` (SC-014(c)), grid-aligned 64-blocks:
  - Core A: zeros → `o`, render 256 samples, → zeros.
  - Twin B gets every call except the excursion.
  - From the first control interval after the return, for 1 s: A.L == A.R, A.L == B.L and A.R == B.R, all
    bitwise.
- `ProfundumCore_PanLargeOffsetBounded`: an element of `1e30f` gives finite output, with the bank's clamp to ±1
  applied.
- `ProfundumCore_NoAllocationOnAudioThread` (SC-015): wrap each of these in `startTracking()` /
  `REQUIRE(stopTracking() == 0)`:
  - `setControls`, `setFrequency`, `setRetriggerPhase`;
  - `noteOn` (Reset and FreeRunning);
  - `setPartialPanOffsets` (zero, then non-zero, then zero);
  - `processBlock` with and without `f0PerSample`, during a slew, and across the pan return.

**Implement:**
- `setPartialPanOffsets` (S4.8):
  - A non-finite element (`!detail::isFinite`) becomes 0.0f.
  - Set `panDirty_` on any bitwise change.
  - `panNonZero_` = any element `!= 0.0f`, so −0.0 counts as zero.
- `controlUpdate` step 7: if `panDirty_`:
  - if `panNonZero_`, call `bank_.applyPanOffsets(panOffsets_)` and set `panAppliedNonZero_ = true`;
  - else if `panAppliedNonZero_`, call `bank_.restoreCenterPan()` and set `panAppliedNonZero_ = false`;
  - then `panDirty_ = false`.
- `applyNoteOn` `Reset` branch: `bank_.reset()` re-centres the tables, so set `panAppliedNonZero_ = false` and
  `panDirty_ = panNonZero_`.

**Verify:** run `dsp_systems_tests.exe "ProfundumCore_*"`: all pass, with zero warnings.

---

## Group 11 — verification TUs (parallel: disjoint new files, headers read-only)

Each task owns one new TU and only **reads** the component and helper headers. Record defects under "Defects
found" (rule 8); T017 fixes them. The three builds share the `dsp_systems_tests` target, so run its build once
when all three TUs are written. Coordinate this so that only one `cmake --build` of that target runs at a time.

### T013 [P] Spectral TU part 1: SC-001, SC-002 render, SC-004 render, SC-005, SC-006 render, SC-007 render

File: `profundum_core_spectral_test.cpp` only. Every render calls `requireLREqual`.
- `ProfundumCore_PartialFrequencyAccuracy` (SC-001, `[long]`): `kSawAnchor`, MIDI 12–60, at 44.1, 48 and 96 kHz.
  - Candidate n ∈ {1, 2, 3, 8, 16, 32, 64} is measured iff n·f0 < capHz and its post-mask `evaluate` gain is
    ≥ −60 dB re h1. `// plan S1 C-8 (ii)`
  - Assert that n ∈ {1, 2, 3} is measured at every note, and that each of 16, 32 and 64 is measured at ≥ 1 note in
    every octave C1–C4.
  - After a 0.2 s settle, take two 1 s BH7 frames whose starts are 1 s apart. Unwrap Δφ against 2π·n·f0·1 s,
    convert to Hz and then to cents, and `REQUIRE(std::abs(cents) < 0.1)`.
  - Smoke sibling: MIDI 36, 48 kHz, n = 1 and 8.
- `ProfundumCore_DepthRenderMonotonic` (SC-002, `[long]`): at C1, C2 and C3, rendered h1/rest (`describeRendered`,
  steady state ≥ 50 ms after convergence) is strictly increasing over the depth sweep at the 27 grid points.
  Smoke: C2, the 8 grid corners plus mid grid.
- `ProfundumCore_AudibilityGate_BodyEdgeShift` (SC-004 render arm, `[long]`, 48 kHz, N = 64, C1/C2/C3):
  - Use T007's point sets with **unchanged bars**: 6 dB, 0.7 × headroom (from the rendered R_body at body = 0),
    1 octave, and 6 dB endpoints.
  - Omit the depth-1 Shift points. `// plan S1 C-2`
  - Assert that no harmonic is in the cap taper (N = 64 at C1–C3 / 48 kHz).
  - A miss is a defect; never add an allowance.
  - Smoke: mid grid only.
- `ProfundumCore_AudibilityGate_BodyCurvatureEmphasis` (SC-005), at C2 and mid grid:
  - σ is strictly decreasing over curvature, with |span| ≥ 0.25 octave.
  - Odd/even is strictly decreasing over emphasis [−1, 0], starting from the measured first point, which must
    exceed the second. The span is ≥ 20 dB.
  - The FR-023 floor holds at each step.
- `ProfundumCore_WaveformAnchorsRendered` (SC-006), at C1, 48 kHz:
  - T006's tolerances, applied to rendered √w.
  - Aliasing: `aliasedPower` with `aliasFftLength` gives power outside the exclusions ≤ −60 dB re total, with
    `excludedFraction ≤ 0.5`.
- `ProfundumCore_NamedDistributionsRendered` (SC-007), at C1, C2 and C3:
  - FR-022 ordering, with ≥ 3 dB dominance.
  - Every harmonic ≥ −40 dB re the loudest is within ±3 dB of `evaluate`.
- **Verify:** run `"[profundum]~[long]"` per-push, then `"[profundum][long]"` once, logged. Record wall clocks.
  Never run these concurrently with a `[.perf]` run.

### T014 [P] NonFinite TU: SC-016, SC-020(d), FR-002/FR-062 (`-fno-fast-math`)

File: `profundum_core_nonfinite_test.cpp` only. It is already in the `-fno-fast-math` list (T003).
- Build bad values with `std::bit_cast<float>`: NaN `0x7FC00000u`, +Inf `0x7F800000u`, −Inf `0xFF800000u` and
  denormal `0x00000001u`.
- Check every value with `detail::isFinite`.
- Never use `[long]` in this TU.

Cases:
- `ProfundumCore_NonFiniteInputsRejected` (SC-016), at C2 / mid grid. Inject each bad value into `setFrequency`,
  `noteOn`, each of the six `setControls` fields, a pan-offset element, and `f0PerSample` elements.
  - Output is finite and `stateFinite()` holds.
  - A non-finite f0 leaves `currentFrequency()` unchanged.
  - A non-finite control renders `memcmp`-equal to a twin that never received that call.
  - A denormal or a negative f0 gives `currentFrequency() == 8.0f` after the next block.
  - 8 Hz and clamp-ceiling renders are finite. At the ceiling, the delivered vector is h1-only with
    a₁² = P0 ± 1e-4.
- `ProfundumCore_PitchTrajectoryNonFinite` (SC-020(d)): a 1 s vibrato trajectory with NaN at {16, 4 800} and +Inf
  over [9 600, 9 663].
  - Output is finite.
  - The render equals, by `memcmp`, a twin whose trajectory has each bad element replaced by the last good value
    before it. That is the hold.
- `SpectralShapeRecipe_SanitizeNonFinite` (FR-002):
  - NaN and ±Inf in each field give that field `kDefaultControls`' value and leave the others bit-unchanged.
  - `evaluate` with any non-finite field writes only finite values.
  - Also: a non-finite or ≤ 0 `f0Hz` passed to `evaluate` gives a finite vector (T008's documented behaviour).
- `ProfundumCore_PanNonFiniteSanitised`: a NaN pan element behaves exactly like 0.0f (`memcmp` against a twin).

**Verify:** build `dsp_systems_tests` with zero warnings, then run `"ProfundumCore_*NonFinite*"`,
`"ProfundumCore_PanNonFiniteSanitised"` and `"SpectralShapeRecipe_SanitizeNonFinite"`. All pass.

### T015 [P] Perf TU: SC-013 and the OQ-3 figures (build only; do NOT run here)

File: `profundum_core_perf_test.cpp` only. It is **not** in the `-fno-fast-math` list.
- `TEST_CASE("ProfundumCore_CpuBudget", "[systems][profundum][.perf]")`, at 48 kHz, host block 64,
  `prepare(48000, 64)`:
  - Load: full-range 2 Hz triangles on D, B, E and S, with phase offsets 0, ¼, ½ and ¾, set before every block,
    plus a 5 Hz ±2-semitone vibrato around C2 through `f0PerSample`.
  - Each run renders 10 s of audio and is timed with `std::chrono::steady_clock`. Do one warm-up run, then
    5 timed runs.
  - pct = median(wall)/10 s × 100, and `REQUIRE(pct <= 1.0)`.
  - Also print, without asserting, the same load at `prepare(48000, 96)` and the static case (64 partials,
    nothing changing).
  - Each figure prints as `std::printf("PROFUNDUM_PERF partials=%d load=%s pct=%.3f\n", …)`.
  - Accumulate a checksum of the output and `REQUIRE(detail::isFinite(sum))`, so the render cannot be elided.
- **Verify:**
  - Build with zero warnings.
  - `dsp_systems_tests.exe --list-tests "[.perf]" 2>&1 | grep ProfundumCore_CpuBudget` must list the case.
  - **Do not run it.** T021 runs it alone.

---

## Group 12 — Spectral TU part 2 (sequential: the same file as T013)

### T016 SC-008, SC-009, SC-010(b), SC-011(a), SC-017 rate arm, SC-019 render, SC-022 render, listening renders

File: `profundum_core_spectral_test.cpp` only (rule 8). Every all-zero-pan render calls `requireLREqual`.
- `ProfundumCore_SpectrumInvariantC1toC3` (SC-008, `[long]`): 25 semitones C1–C3 × (6 named + 5 colours + mid
  grid), 48 kHz, N = 64.
  - Every harmonic ≥ −40 dB re h1 is within ±0.5 dB of `evaluateShape`.
  - Assert that **no** harmonic is in the taper, so the case fails if a constant change shrinks the set.
    `// plan S1 C-8 (i)`
  - Smoke: C1, C2 and C3 at mid grid.
- `ProfundumCore_NoAliasingAllNotes` (SC-009 static, `[long]`): MIDI 0–127 at 44.1, 48 and 96 kHz, with
  `kSawAnchor` plus `edge = 1`, `shift = 1`.
  - The frame starts ≥ 50 ms after the note.
  - `aliasFftLength`, BH7, ±8-bin and 0–8 exclusions.
  - `REQUIRE(excludedFraction <= 0.5)` and `REQUIRE(aliasedDbfs <= -96.0)`.
  - Smoke: MIDI 0, 60 and 127 at 48 kHz.
- `ProfundumCore_NoAliasingDuringBend` (SC-009 bend, `[long]`), at 44.1 and 48 kHz, same state:
  - MIDI 24 → 108 → 24 in 1-semitone legs. Each leg is a 10 ms log-linear `f0PerSample` ramp followed by a hold of
    the frame length + 50 ms.
  - Each frame starts ≤ 2·64 samples after the leg ends, and the static rules apply at the held f0.
- `ProfundumCore_NoZipperControlSweepsSidebands` (SC-010(b)): f0 = 140.625 Hz at 48 kHz.
  - Run a 2 Hz full-range triangle on each of D, B, E and S in turn, and separately a 5 Hz ±2-semitone vibrato at
    `kSineAnchor`.
  - Use a BH7 2¹⁷ frame starting ≥ 1 s in, and
    `sidebandPower(..., kControlInterval = 64, pitchInterval = 16 for the vibrato only, ...)`.
  - `REQUIRE(remainingFraction >= 0.25)` and `REQUIRE(powerDbReTotal <= -60.0)`.
  - Print each value. The simulation predicts −69 dB worst (shift).
  - A failure goes to Defects. The pre-authorised fallback is `kControlInterval = 32` (plan S9.4).
- `ProfundumCore_LowNoteGuardReducesUpperEnergy` (SC-011(a)), public API only: rendered R_pres at 16.35 Hz is
  ≤ R_pres at 32.70 Hz − 6 dB, at `kSawAnchor` and mid grid (model 10.45 / 8.56 dB).
- `ProfundumCore_SampleRateChange` (SC-017 rate arm): one core, `prepare` 48 → 96 → 44.1 kHz. At each rate:
  - re-assert SC-001 for n = 1 and 8 at MIDI 36;
  - re-assert SC-008 at mid grid for C1, C2 and C3. At 44.1 kHz, exclude C3 harmonics above the 7 723 Hz taper
    start.
- `ProfundumCore_BodyColoursDistinctRendered` (SC-019 render arm, C2): T007's bars applied to rendered
  descriptors. The recipe arm keeps the spec name. Record this split in the T003 comment block, which T018
  audits.
- `ProfundumCore_EmphasisOddOnlyRendered` (SC-022 render, C2): at emphasis −1 over the SC-022 grid, every even
  n ≤ −50 dB re h1, including n ≥ 10 at edge = 1. The FR-023 floor holds at each emphasis step.
- `ProfundumCore_ListeningRenders` (`[systems][profundum][.listen]`, not a gate). Write WAVs to
  `build/windows-x64-release/profundum-listen/`:
  - C0–C4 static notes at the 12 named states;
  - the SC-009 bend;
  - the four 2 Hz sweeps;
  - **E1 (41.2 Hz) at 64 and at 96 partials** (OQ-3).

  Search `tests/test_helpers/` for an existing WAV writer first (`grep -rn "writeWav\|RIFF" tests/test_helpers`).
  Only if none exists, write a minimal 32-bit-float RIFF writer local to this TU.
- **Verify:** run `"[profundum]~[long]"`, then `"[profundum][long]"` (logged), then `"[.listen]"` once. Record
  wall clocks and the WAV paths.

---

## Group 13 — defect fix-up (sequential, conditional)

### T017 Resolve every entry under "Defects found"

Files: the owning header (`spectral_shape_recipe.h` or `profundum_core.h`; the bank only if the FR-064 method is
at fault), plus `recipe-model.js` and plan S3.6 when a recipe constant changes.
- For each defect:
  - reproduce it with its named case;
  - trace values against plan S3–S4 and find the divergence point;
  - fix it.
- Never change a test bar (rule 9).
- A recipe constant change must first pass `recipe-model.js`, and then T008's `MatchesCalibrationModel` vectors
  must be regenerated.
- Pre-authorised fallbacks:
  - **R-1:** `kBodyWidthNarrowOct` 0.15 → 0.17.
  - **S9.4:** `kControlInterval` 64 → 32. f0 = 140.625 Hz still satisfies the rule (1500/140.625 = 10⅔); re-run
    SC-010(b) and note the CPU change for T021.
- Any other constant change needs the user (see open questions).
- **Verify:** rebuild both DSP exes and re-run `"[profundum]"` per-push, plus `[long]` if a spectral case was
  involved. If "Defects found" is empty, mark this task as a no-op and cite the T013 and T016 logs.

**RULING 2026-10-10 on D-1 and D-2 (user; spec Clarifications "Session 2026-10-10 — build defects"):** both are
measurement defects, not core defects. The core and the headers are **not** changed; neither R-1 nor S9.4 is
applied. Re-implement the SC-010(a)/(b) measurement per the amended spec rows and plan S8.3 rows:
1. `dsp/tests/unit/systems/profundum_core_test_helpers.h`: add `renderIdeal(...)` — double-precision per-sample
   additive synthesis of the same control and pitch trajectory with recipe targets evaluated every sample,
   `Reset` start phases, the same frequency law and mask (the reference the D-1/D-2 diagnostic tables used) —
   `renderHeldRaw(...)` (ideal targets held for `kControlInterval` samples, no smoothing) and
   `residual(core, ideal)`. Self-check: the ideal of a static `kSineAnchor` at C2 differs from the core by
   ≤ −80 dBFS RMS after the bank's 2 ms one-pole settles.
2. `profundum_core_test.cpp` `ProfundumCore_NoZipperControlSweeps` / `ProfundumCore_NoZipperPitchBend`: run the
   `ClickDetector` on the residual; `REQUIRE(clicks.empty())` unchanged; `INFO` the residual RMS re core RMS.
   `ProfundumCore_ClickDetectorPositiveControl`: spliced signal minus the ideal of the un-spliced step,
   `REQUIRE(size ≥ 1)`.
3. `profundum_core_spectral_test.cpp` `ProfundumCore_NoZipperControlSweepsSidebands`: FFT of the residual,
   region power re the **core's** total power, `REQUIRE(≤ −60 dB)` unchanged, 25 % guard unchanged; add the
   held-raw positive control (`renderHeldRaw` − ideal reads above −60 dB on at least one lever).
4. Verify: rebuild `dsp_systems_tests`, run `"[profundum]"` and `"[profundum][long]"`; all three former reds
   green, both positive controls green; append the output excerpt under "Defects found" as the D-1/D-2
   resolution. Record the measured residual figures for the comply stage.

---

## Group 14 — integration (sequential)

### T018 CMake registration audit

- `grep -n "spectral_shape_recipe\|profundum_core" dsp/CMakeLists.txt dsp/tests/CMakeLists.txt` must show:
  - both headers in the IDE header lists;
  - the recipe TU in `dsp_processors_tests`;
  - all four core TUs in `dsp_systems_tests`;
  - **only** `profundum_core_nonfinite_test.cpp` in the `-fno-fast-math` block, with the perf TU absent from it.
- Compare the TU → SC comment block with `dsp_systems_tests.exe --list-tests "[profundum]"` and
  `dsp_processors_tests.exe --list-tests "[profundum]"`. Every plan S8.3 case name exists, or its rename is
  recorded (`…Sidebands`, `…Rendered`, `…Smoke` siblings).
- Correcting the comment block is the only CMake edit allowed here.

### T019 Full-suite run and shared-header regression (FR-064, SC-023, plan S9.2)

1. **Rebuild.** Read the previous build log's wall clock first. Then run **one batched rebuild** of every
   consumer of the changed bank header, logged:
   `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests ruinae_tests seraphis_tests vorago_tests innexus_tests membrum_tests`
   It must finish with zero warnings.
2. **Run the suites concurrently,** each with `"~[performance]~[perf]~[.perf]~[benchmark]~[long]"` and its own
   log. None of them is timing-sensitive:
   - the five `dsp_*_tests` exes;
   - `ruinae_tests`, `seraphis_tests`, `vorago_tests`, `innexus_tests` and `membrum_tests`;
   - also, in parallel, `dsp_systems_tests.exe "[profundum][long]"` and
     `dsp_processors_tests.exe "[profundum][long]"`.
3. **Report.** Every suite is green. Quote each suite's final `tail -3` line and its wall clock. Own every
   failure.
4. **Pluginval** is skipped, because no plugin source changed (CLAUDE.md: DSP-header and test changes). Record
   that decision.
5. `node specs/profundum-phase1-harmonic-core/check-footprint.js`: quote its full output (SC-023).
6. `node specs/profundum-phase1-harmonic-core/recipe-model.js`: quote its final line.

### T020 Portability and static analysis (FR-065)

- Run these concurrently with each other, but **not** alongside T021:
  - `node tools/check-portability.js`
  - `node tools/lint-apple-globals.js`
  - `node tools/lint-simd-aligned-loadstore.js`
  - `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`, from PowerShell; never the `.sh` on
    Windows.
- Quote the portability output lines showing that the Clang and GCC builds compiled the `inline constexpr`
  named-coordinate definitions (R-7).
- Fix every finding in the Phase 1 files, with one rebuild per fix batch, then re-run the affected
  `[profundum]` cases.
- Run `wsl --shutdown` after `check-portability.js` finishes.

### T021 CPU lane: SC-013 and the OQ-3 figures (run ALONE, last)

- Nothing else may be running: no build, suite, clang-tidy, WSL or other agent.
- Run `node tools/run-cpu-tests.js dsp_systems_tests`, logged. It runs every systems
  `[.perf]`/`[performance]`/`[long]` case one suite at a time, by the runner's contract.
- Quote the three `PROFUNDUM_PERF` lines and the `ProfundumCore_CpuBudget` verdict. The 64-partial full load
  must be ≤ 1.0 %.
- **On failure:**
  1. Confirm nothing else was running.
  2. Let the machine idle.
  3. Re-run that suite alone once.
  4. Only then treat it as a defect. Fix it in code; never relax the budget.
- Record the figures:
  - Fill plan S9.3: the SC-013 % row for 64 and for 96 partials.
  - Fill plan S9.5: the measured static and full figures beside the estimates.
- **Keep `kDefaultPartials = 64`** unless all three S9.3 conditions hold. Condition (2) is the user's listening
  verdict on T016's E1 WAVs, so the decision row stays "(user)" until they rule.

---

## Run log

*(Executors append here. T001: model output and wall clock. T002: positive-control output. T003: build wall
clocks. T007: printed minimums. T010, T011, T013 and T016: case wall clocks and `[long]` decisions. T019: suite
logs, wall clocks and footprint output. T021: perf lines.)*

**T017 (2026-10-10): no-op, provisional.** "Defects found" holds no entries, so there is nothing to resolve and no
header, `recipe-model.js` or plan S3.6 change was made; neither R-1 nor S9.4 was applied. The T013 and T016 logs
the no-op must cite **do not exist yet**: this Run log has no T013 or T016 entry, and
`build/windows-x64-release/bin/Release/dsp_systems_tests.exe` was last linked at 2026-10-10 00:10:57, before
`profundum_core.h` (03:30:33) and `profundum_core_spectral_test.cpp` (03:50:08) were last written, so no T013 or
T016 case has executed. The empty list is therefore "not yet run", not "ran green". When the post-group build runs
`"[profundum]"` per-push and `"[profundum][long]"`, any failure is appended under "Defects found" and T017 re-opens.

## Defects found

*(T013–T016 append here: case name, output excerpt, diagnosis, owning header. T017 resolves each one.)*

**D-1 (2026-10-10) `ProfundumCore_NoZipperControlSweeps` / `ProfundumCore_NoZipperPitchBend` (SC-010(a)), BLOCKED ON USER.**
Output: `axis depth / step: 133 clicks, first at sample 3` and `56 clicks, first at sample 734`. Diagnosis: the
spec-mandated `ClickDetector` (5σ on |Δx| per 512-sample frame) flags the **steady waveform itself**, once per
period, with no control change at all. A temporary hidden diagnostic (removed again) rendered static states for
1.5 s and also ran the detector on an ideal double-precision additive synthesis of the same `evaluateShape` vector:

| state | f0 | core clicks | ideal-additive clicks |
|---|---|---|---|
| mid grid | C1 / C2 / C3 | 51 / 99 / 196 | 53 / 100 / 197 |
| mid grid, depth 0 | C1 / C2 / C3 | 74 / 196 / 196 | 74 / 196 / 197 |
| mid grid, depth 1 (sine) | C1 / C2 / C3 | 0 / 0 / 0 | 0 / 0 / 0 |
| kSawAnchor | C1 / C2 / C3 | 65 / 116 / 196 | 66 / 116 / 197 |

The core matches the ideal reference to within ±2 detections, so no core defect exists, and no implementation that
renders the FR-required mid-grid/saw timbres can report zero clicks. "Zero clicks over the whole render" in
SC-010(a) contradicts the spec's own required waveforms. Needs a user ruling on the measurement. Candidates:
excess clicks over Reset-phase-aligned steady references of the start and end states, or a detector on a
period-differenced signal. The bar stays zero in either case. No test or header was changed.

**D-2 (2026-10-10) `ProfundumCore_NoZipperControlSweepsSidebands` (SC-010(b)), BLOCKED ON USER.** Output: depth
−50.52 dB re total (bar −60). The case aborts at depth, so the remaining levers were not gated, but the diagnostic
measured all four. In each row the core render is compared with three references: (i) an ideal per-sample
synthesis with targets evaluated at **every sample**, so there are no control-rate steps; (ii) interval-held
targets through the bank's 2 ms one-pole; (iii) interval-held targets with no smoothing:

| lever | core | (i) per-sample | (ii) held + one-pole | (iii) held raw |
|---|---|---|---|---|
| depth | −50.52 | −50.67 | −50.46 | −47.72 |
| body | −64.76 | −64.61 | −64.76 | −53.67 |
| edge | −61.98 | −62.39 | −61.98 | −60.36 |
| shift | **−45.46** | −44.98 | −45.46 | −43.60 |

The core equals (ii) to within 0.01 dB, and (i) is no better. The region power therefore comes from the
**intended 2 Hz triangle modulation's own spectral tail**, not from control-rate zipper. The triangle's corners,
pushed through the recipe's nonlinear maps, spread energy past the spec's "first five odd orders" exclusion
(±10 Hz + 7 bins) into a region that starts 26.9 Hz from each harmonic. The **S9.4 fallback (`kControlInterval` →
32) cannot fix this**: even per-sample control (I = 1) gives −50.7 / −45.0 dB. It was **not applied**. S9.4's
−69 dB simulation kept only the held-minus-continuous component and never measured the triangle's own tail.

Evidence for a fix: the same core with a 2 Hz **raised-cosine** (0.5 − 0.5·cos) modulator in place of the
triangle gives depth −65.71, body −73.11, edge −83.95 and shift −64.51 dB, all within −60. Needs a user ruling on
SC-010(b)'s modulator shape or its exclusion. No test, bar or header was changed.

**D-3 (2026-10-10, T017 epoch 5) the D-1/D-2 ruling's `renderIdeal` is undefined for the SC-010(a) step and
onset fixtures, BLOCKED ON USER.** No file other than this one was changed. The ruling (this file, "RULING
2026-10-10", and spec.md:560) defines the ideal as per-sample targets with **no smoothing**, Reset phases, and a
self-check taken "after the bank's 2 ms one-pole settles", i.e. the ideal has no onset ramp. Read against the code:
- **Onset:** the core starts every partial at `currentAmplitude_ = 0` and ramps it through the 2 ms one-pole
  (`harmonic_oscillator_bank.h:295`, `:140`, `:851`); the ideal starts at full amplitude. With Reset phases the
  saw-like mid-grid sum sits on its band-limited edge at t = 0, so the residual's first frame holds that edge at
  roughly −22 dBFS frame RMS, above the detector's −60 dB energy gate (`artifact_detection.h:196-201`). Every
  residual (step, sweep, bend) therefore inherits D-1's "first at sample 3" detection by construction.
- **Steps:** the step's control trajectory is a step, so the unsmoothed ideal changes gain instantaneously at
  sample 24000 (Reset phases, 24000 is a control update) while the core slews (`profundum_core.h`
  `slewShapeTowardTarget`, FR-050) and one-pole smooths. The residual then contains the ideal's one-sample jump
  Σ(bₙ−aₙ)·sin(nφ)·0.7071, nonzero at φ₁ = 0.705 cycle, which a per-frame 5σ |Δx| test flags. The step arm
  of `ProfundumCore_NoZipperControlSweeps` is red for any smoothing core.
- **Positive control:** the spliced signal is the instantaneous gain change of two Reset renders, which equals
  the unsmoothed ideal of the step after the onset settles. "Spliced − ideal" is therefore at the ≤ −80 dB floor at
  the splice, below the −60 dB energy gate, and its only detection would be the onset edge. The control would pass
  for the wrong reason (no teeth at the splice).
- The bend, the 10 ms sweeps (if the ideal's sweep is linear per sample) and the SC-010(b) sideband arms are
  well defined. The vibrato ideal needs a per-sample f, not the core's U-held f, or the pitch-update sidebands
  cancel. That is consistent with the ruling.

Decision needed. What is the ideal for a control **step**, and how is the **onset** treated? Candidates:
(1) The ideal runs its per-sample targets through the bank's 2 ms one-pole from zero. That fixes the onset, and
the instantaneous step becomes a 2 ms ramp. The splice − ideal then keeps the splice's jump (real teeth). The core
residual is then the FR-050 slew's lag (≈ 2 intervals), which is not zipper and may still read as clicks.
(2) The ideal follows FR-050's slew continuously (per-sample great-circle step of 0.999·Δmax/64) plus the 2 ms
one-pole, so the residual is the interval quantisation only.
(3) Gate the residual from 50 ms after noteOn and keep the unsmoothed ideal only for the continuous fixtures (bend,
sweep, SC-010(b)). The step arm and the positive control go back to an "excess clicks over the steady start/end
references" measure (D-1's first candidate).
The bars stay zero / ≥ 1 / −60 dB in every option.

**RULING on D-3 (2026-10-10, refinement of the D-1/D-2 ruling; recorded in spec Clarifications):** option **(2)**.
The ideal includes every smoothing the spec itself mandates, so the residual is the control-rate quantisation and
nothing else:
- `renderIdeal` evaluates the recipe target at every sample, then follows FR-050's slew **continuously** (the
  per-sample great-circle step of 0.999·Δmax/64 toward the per-sample target), then the bank's 2 ms one-pole
  **from zero at noteOn** (so the onset ramp is identical to the core's), with `Reset` start phases, the same
  frequency law (per-sample f for the vibrato and bend, never the core's U-held f) and the same mask.
- The step arm's residual is therefore only the 64-sample interval quantisation of the slew; the onset edge
  cancels; the vibrato residual is only the U = 16 pitch-update quantisation.
- Positive control: spliced signal (instantaneous gain change between two `Reset` renders) minus this smoothed
  ideal keeps the splice jump, so `REQUIRE(size ≥ 1)` has teeth at the splice.
- The self-check in item 1 of the T017 ruling is restated: core − ideal for a static `kSineAnchor` at C2 is
  ≤ −80 dBFS RMS **over the whole render including the onset**.
- Bars unchanged: zero clicks, ≥ 1 on the positive control, −60 dB sidebands with the held-raw control. No
  header, recipe constant or bar changes; R-1 and S9.4 not applied.

**T017 epoch 6 (2026-10-10): D-1 / D-2 / D-3 measurement re-implemented per the rulings; verification pending
the post-group build.** No core or recipe header, `recipe-model.js` or plan S3.6 change; R-1 and S9.4 not
applied; no bar changed. Files touched: `profundum_core_test_helpers.h`, `profundum_core_test.cpp`,
`profundum_core_spectral_test.cpp`, this file.
- Helpers: `renderReference` (shared body) with `renderIdeal` (per-sample `evaluateShape` → FR-050 slew
  followed per sample, chord `kSlewChordMargin`·Δmax/64, snap at Δmax/64, Δmax formed as `prepare` forms it →
  `evaluateMask` at the per-sample f + `applyMask` renormalisation + 1e-12 flush → the bank's 2 ms one-pole,
  coefficient formed as the bank forms it, from zero at noteOn; Reset phases, partial n at n·f(i), centre pan
  gain) and `renderHeldRaw` (targets and mask latched every 64 samples from noteOn, no slew, no one-pole);
  `residual(x, ideal)`; `rmsDbfs`. Precondition `numPartials·f < capHz` on every sample (AA gain 1, no frozen
  cap partial) is asserted. `sidebandPower` gained an optional `totalRef`: harmonic selection and total power
  come from the core's spectrum, so the result is the residual's region power re the core's total.
- `ProfundumCore_TestHelpersSelfCheck` gained the D-3 self-check section (static `kSineAnchor`, C2, 1 s,
  core − ideal ≤ −80 dBFS RMS over the whole render).
- `ProfundumCore_NoZipperControlSweeps` (step and 10 ms sweep per axis) and `ProfundumCore_NoZipperPitchBend`
  run the detector on core − ideal, `REQUIRE(clicks.empty())`, and `INFO`/`WARN` the residual RMS re the core
  RMS. The sweep's per-sample ideal is the linear ramp whose values at the core's latch instants equal the
  values the fixture sets there (t = (pos + 64)/480), i.e. the core holds the ideal's trajectory. The bend's
  ideal uses the per-sample trajectory, not the U-held f.
- `ProfundumCore_ClickDetectorPositiveControl`: spliced − ideal of the un-spliced lo → hi step at 24000,
  `REQUIRE(size ≥ 1)`.
- `ProfundumCore_NoZipperControlSweepsSidebands`: FFT of core − ideal, region re the core's total,
  `REQUIRE(≤ −60 dB)`, 25 % guard on every arm; held-raw − ideal per lever, `REQUIRE(loudest > −60 dB)`.
  The vibrato ideal follows the per-sample f.
- **Still to do (build agent):** rebuild `dsp_systems_tests`, run `"[profundum]"` and `"[profundum][long]"`,
  confirm the three former reds and both positive controls are green, then paste the output excerpt and the
  residual figures (the `SC-010(a)` / `SC-010(b)` WARN lines) here. Any red re-opens T017.

**T017 epoch 7 (2026-10-10): post-group build had 3 reds; two were measurement defects and are fixed, one is a core residual and is BLOCKED ON USER (D-4).**
No core or recipe header, constant or bar was changed. Files touched: `profundum_core_test_helpers.h`,
`profundum_core_test.cpp` (bend ideal only), `profundum_core_spectral_test.cpp` (vibrato ideal only), this file.
- **Reference oscillator law.** `renderReference` summed unit sines sin(n·θ). The bank is a Gordon-Smith MCF
  whose sin output has amplitude 1/cos(π·n·f/fs), compensated by `cos(π·n·f/fs)` inside `antiAliasGain_`
  (`harmonic_oscillator_bank.h:1084-1101`). The bank recomputes that gain in `loadFrame` (:365) but not in
  `applyExternalFrequencyMultipliers` (:665-672), so during a glide the correction is control-rate-held too.
  The reference now runs the MCF in double with per-sample exact ε and the per-sample correction in the
  one-pole target. Measured (hidden diagnostic, removed): bend, ideal fed the core's own U-held f: residual
  −46.7 dB re core (35 clicks) with unit sines, −83.0 dB (0 clicks) with the MCF law.
- **Pitch-hold latency.** The core samples `f0PerSample` at each multiple of U = 16 and holds it, a constant
  (U−1)/2 = 7.5-sample group delay that integrates into a permanent ≈ 0.65-cycle phase offset at h64 across the
  bend; that is latency, not zipper. The bend and vibrato ideals now use the per-sample trajectory delayed by
  (U−1)/2 (`pitchHoldAlignedF0`). Bend: delay 0 / 7.0 / 7.5 / 8.0 → −17.9 / −40.9 / **−59.8** / −40.9 dB,
  59 / 47 / **0** / 39 clicks.
- **SC-010(b) vibrato guard.** `sidebandPower` subtracted Carson bands around every n·f0 up to Nyquist. Because
  they widen with n, they tile the spectrum, so a sine's vibrato always read remaining fraction 0 (the reported
  "−3000 dB, fraction 0"). Read literally, spec SC-010(b)'s "any n·f₀" makes its own 25 % guard
  unsatisfiable. The zones now use the same loud-n set as the region, which subtracts fewer bins and so is
  stricter. For the triangle arms the AM zones (±12.6 Hz) cannot reach region bins (≥ 26.9 Hz from every
  harmonic), so those arms are unchanged. **Spec wording to confirm: "any n·f₀" → "every n in the region's set".**
- Result: `ProfundumCore_NoZipperPitchBend` green (−59.786 dB re core RMS, 0 clicks).
  `ProfundumCore_NoZipperControlSweepsSidebands` green: depth −65.61, body −73.83, edge −77.27, shift −60.85,
  vibrato −100.81 dB (bar −60). Held-raw control: depth −50.55, body −54.00, edge −64.10, shift −47.65
  (loudest > −60). `ProfundumCore_TestHelpersSelfCheck` and `ProfundumCore_ClickDetectorPositiveControl` green.

**D-4 (2026-10-10) `ProfundumCore_NoZipperControlSweeps` (SC-010(a) control arm), BLOCKED ON USER.** Still red:
`depth step: residual RMS −37.2173 dB re core RMS, 3 clicks`. The case aborts at depth, so the other axes did not
run. The diagnostic (hidden, removed) used the corrected reference and covered all four axes, both arms, and
the ideal shifted in time. The core moves Δmax at each interval start, so it leads the per-sample slew by about
half an interval.

| axis | step, ideal as ruled | step, ideal led 31 / 32 / 33 | sweep, as written (+64) | sweep, ideal led to +32.5 |
|---|---|---|---|---|
| depth | −37.2 dB, 3 clicks | −57.7 / −58.5 / −58.4 dB, 2 / 2 / 1 | −38.1 dB, 2 | −62.2 dB, 2 |
| body | −41.0 dB, 2 | −59.8 / −59.5 / −58.6 dB, 3 / 2 / 2 | −41.7 dB, 1 | −64.7 dB, 1 |
| edge | −56.5 dB, 1 | −69.9 / −69.0 / −68.1 dB, 0 / 0 / 0 | −50.1 dB, 1 | −74.1 dB, 0 |
| shift | −38.6 dB, 4 | −59.4 / −58.9 / −57.7 dB, 0 / 0 / 0 | −37.9 dB, 1 | −57.9 dB, 1 |

Even at the best alignment, the depth and body steps and the depth, body and shift sweeps keep 1-3 clicks. Their
loudest 512-sample residual frames read −49 to −58 dBFS, above the detector's −60 dBFS energy gate
(`artifact_detection.h` processFrame). The clicks sit at the waveform's edge (for example sample 24217 =
33 periods of C2), at the end of the slew. This is the 64-sample staircase of the FR-050 slew itself: Δmax =
0.5·√P₀ per interval, through the bank's 2 ms one-pole. It is not a measurement artefact, so under the D-3
definition it is core zipper.

Experiment (reverted): with `kControlInterval` = 32 (the S9.4 fallback) and the ideal led by 15-16 samples, all
four steps read 0 clicks at −65.6 / −69.6 / −75.1 / −69.8 dB. At a 16-sample lead depth still reads 1 click, so
the margin is thin. The D-1/D-2 ruling excluded S9.4, so it was not applied.

Decision needed:
1. Apply S9.4 (`kControlInterval` 32). Constants and tests derived from 64 then follow, for example the
   SampleRateExtremes 0.25119 literal and spec SC-010(d)'s "/ 64" wording.
2. Rule that the SC-010(a) control-arm ideal is aligned to the core's half-interval lead, and decide which
   lead. This alone does not make the arm green at I = 64.
3. Another core change, for example finer per-sample interpolation of the delivered vector, or another bar
   definition.

The bars stay zero clicks in every option.

**D-4 addendum (2026-10-10, fix-up epoch 8): re-measured, still BLOCKED ON USER, nothing changed.** A temporary
probe case was added, run and then removed. Header and test TU were restored from backups and `dsp_systems_tests`
was rebuilt clean. The I = 64 rows reproduce the D-4 table exactly. The probe then set `kControlInterval` to 32,
with the host block equal to the interval. The lead that centres the ideal on the core's zero-order hold is
(I−1)/2 for the step and (I+1)/2 for the ramp:

| axis | I=64 step lead 0 / 31 / 32 | I=64 sweep ramp +64 / +32.5 | I=32 step lead 0 / 15 / 16 | I=32 sweep ramp +32 / +16.5 |
|---|---|---|---|---|
| depth | −37.2 dB 3 / −57.7 dB 2 / −58.5 dB 2 | −38.1 dB 2 / −62.2 dB 2 | −43.4 dB 4 / −65.0 dB 0 / −65.0 dB 0 | −43.6 dB 1 / −72.1 dB 0 |
| body | −41.0 dB 2 / −59.8 dB 3 / −59.5 dB 2 | −41.7 dB 1 / −64.7 dB 1 | −47.2 dB 3 / −68.9 dB 0 / −69.2 dB 0 | −47.6 dB 2 / −75.5 dB 0 |
| edge | −56.5 dB 1 / −69.9 dB 0 / −69.0 dB 0 | −50.1 dB 1 / −74.1 dB 0 | −62.0 dB 1 / −75.2 dB 0 / −73.5 dB 0 | −56.2 dB 1 / −77.0 dB 0 |
| shift | −38.6 dB 4 / −59.4 dB 0 / −58.9 dB 0 | −37.9 dB 1 / −57.9 dB 1 | −44.7 dB 2 / −68.5 dB **1** / −70.0 dB 0 | −44.7 dB 1 / −69.7 dB 0 |

An ideal that follows the host's per-block staircase stays red at both intervals: 1-3 clicks on depth, body and
shift. Only the combination of option 1 (S9.4) and option 2 (half-interval alignment) is green on every axis and
both arms, and only at a 16-sample step lead: the shift step reads 1 click at 15. Neither change alone is green.
Both need the user: S9.4 against the D-1/D-2 ruling's "S9.4 not applied", the alignment as a measurement
definition. If both are ruled in, T021 also needs to re-measure the CPU cost of I = 32.

**RULING on D-4 (2026-10-10, user; spec Clarifications "Session 2026-10-10 — build defects"):** **interval 32 +
aligned ideal.** Plan S9.4's fallback `kControlInterval` 64 → 32 is adopted, extended from the sideband arm to the
click arm, and the ideal is centred by the zero-order hold's mean delay: a (I−1)/2 = 16-sample lead on steps and
(I+1)/2 = 16.5 samples on sweeps (the probe's green column at both). Consequences, all in this task:
- `profundum_core.h`: `kControlInterval` = 32. f0 = 140.625 Hz for SC-010(b) remains valid (1500 / 140.625 = 10⅔,
  plan S9.4); U = 16 unchanged.
- Tests: every hard-coded 64 that means the control interval becomes `kControlInterval` — the `kBlock = 64` in the
  click-sweep cases, the 0.25119 constant in `ProfundumCore_SampleRateExtremes` (re-derived from
  375·√P₀·kControlInterval/fs), and `maxShapeStepPerInterval()` expectations. Spec SC-010(d) now reads
  "kMaxGainSlewPerSec × kControlInterval / fs".
- Bars unchanged: zero clicks, ≥ 1 on the positive control, −60 dB sidebands, SC-010(d) ceiling at the new interval.
- Verify: rebuild `dsp_systems_tests`; `"[profundum]"` and `"[profundum][long]"` green including both positive
  controls; record the per-axis residual figures and clicks (all four axes, steps + sweeps + bend) under "Defects
  found" as the D-4 resolution. T021 re-measures CPU at the new interval; the 1 % budget is unchanged.

**T017 epoch 9 (2026-10-10): D-4 ruling implemented; verification pending the post-group build.** No recipe
header, recipe constant, `recipe-model.js` or plan S3.6 change; R-1 not applied; no bar changed.
- `profundum_core.h`: `kControlInterval` 64 → **32** (S9.4, extended to the click arm); the Δmax comment now
  reads 375·√P0·kControlInterval/fs = 0.125594 at 48 kHz. U = 16 unchanged.
- `profundum_core_test.cpp`:
  - `ProfundumCore_NoZipperControlSweeps`: block = `kControlInterval`; step ideal led by `kStepLead` =
    I/2 = 16 samples (the ruled value); sweep ideal is the ramp t = (i + (I+1)/2 − hold)/S, i.e. led to +16.5.
  - `ProfundumCore_SampleRateExtremes`: Δmax expectation 375·√P0·kControlInterval/fs; the 48 kHz literal
    re-derived 0.25119 → **0.125594** (margin 1e-5 unchanged); its SC-010(d) arm uses `kControlInterval` blocks.
  - `ProfundumCore_ShapeGainStepCeiling`: block = `kControlInterval`; `kConvergeIntervals` 4 → **7**
    (⌈(π/2 − 2·asin 0.125)/(2·asin(0.999·0.125))⌉ = 6 slew steps + the snap; the old formula gave 3 + 1 at 0.5·√P0).
  - Per-interval records (`maxBaseStepSemitones`, `requireMaskStepBound`, `requireCapExact`) now record every
    `kControlInterval` (new file-scope `kInterval`) instead of every 64 samples, which at I = 32 would have
    spanned two control updates: `NoCrossfadeOnGlide` (3 arms), `MaskNeverSlewedBend` (3), `…Long`,
    `CapTracksPitchExactly`, and the SampleRateExtremes bend arm.
  - `ProfundumCore_RetriggerPhasePolicy`: Core B's prior render 0.71 s (34 080 = 1065·32, grid phase 0 at
    I = 32, so the C-9 "different grid phase" arm had lost its teeth) → 0.7105 s (34 104 samples, phase 24),
    plus `REQUIRE(size % kControlInterval != 0)`.
- `profundum_core_test_helpers.h`: `renderCore`'s default block 64 → `kControlInterval` (renders are
  block-invariant, SC-020(b)); comments.
- `profundum_core_nonfinite_test.cpp`: `kBlock` = `kControlInterval` (its comment claimed equality); comments
  (4800 = 150·32).
- `profundum_core_spectral_test.cpp`: comment only (f0 = 140.625 Hz derivation at I = 32). The SC-010(b) arm
  already passes `kControlInterval` to `sidebandPower`, so its region moves to k·1500 Hz ± n·f0 automatically.
- Left at 64 on purpose (host block sizes, not the interval; output is block-invariant): pan, allocation,
  skip-rule, retrigger, bend and perf (`kPerfBlock`, SC-013) fixtures.
- Not edited (outside this task's file list, flagged for the comply stage): spec FR-050 text "(= 0.5·√P₀ per
  64-sample …)" and SC-010(b)'s "with `kControlInterval` = 64" derivation sentence; plan lines quoting 0.25119
  and "≈ 2.83 intervals" (S4.5).
- **Still to do (build agent):** rebuild `dsp_systems_tests`, run `"[profundum]"` and `"[profundum][long]"`,
  paste the per-axis `SC-010(a)` WARN lines (4 axes × step + sweep, plus the bend) and the `SC-010(b)` figures
  here as the D-4 resolution. T021 re-measures CPU at I = 32 against the unchanged 1 % budget. Any red re-opens
  T017.
