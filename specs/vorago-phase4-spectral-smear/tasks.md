# Tasks: Vorago Phase 4 — Spectral Smear

**Spec:** `specs/vorago-phase4-spectral-smear/spec.md` (1299 lines, read in full this session)
**Plan:** `specs/vorago-phase4-spectral-smear/plan.md` (2015 lines, read in full this session)
**Deliverable:** one new Layer 2 header `dsp/include/krate/dsp/processors/spectral_smear.h`, four new
test TUs under `dsp/tests/unit/processors/`, one new test-helper header
`tests/test_helpers/spectral_flux.h`, two edits in `dsp/tests/CMakeLists.txt`, one enumerated-include
edit in `dsp/lint_all_headers.cpp`, and one bounded set of edits to `spec.md` (plan S17's corrections
C-1, C-5, C-6, C-7, C-10, C-11, C-12, C-13, plus C-17 raised at this stage).
**Test target:** `dsp_processors_tests` (all four new TUs).
**Regression set that must stay green (FR-072):** `dsp_core_tests`, `dsp_primitives_tests`,
`dsp_processors_tests`, `dsp_systems_tests`, `dsp_effects_tests`, `seraphis_tests`, `innexus_tests`.
**Plugin work:** none. Vorago's plugin starts at Phase 11; phases 1–10 are KrateDSP-only.
**Shipped components amended:** none. `systems/atmosphere_engine.h` and `effects/aether_reverb.h`
must be byte-unchanged at the end of the phase (FR-070, FR-071, SC-014).

---

## How to read this file

* Tasks are grouped into **ordered groups**. A group starts only when every task in the previous
  group is green (builds warning-free, its own cases pass, `dsp_processors_tests` still passes).
* `[P]` marks tasks that are parallel-safe **within their group**: their file sets are fully
  disjoint. **Every task that edits `dsp/include/krate/dsp/processors/spectral_smear.h` is unmarked
  and sits in its own group** — that header is the single shared file of this phase, and so are
  `dsp/tests/CMakeLists.txt`, `spec.md` and each of the four test TUs.
* Each task is **self-contained**: exact files to create/edit, the failing test to write **first**
  (TU, `TEST_CASE` name, the numeric assertions), then the implementation intent with the plan
  section that carries the code shape, then the command that verifies it. An executor needs nothing
  beyond the spec/plan sections each task cites.
* Canonical order inside every task: **write the failing test → see it fail → implement → zero
  compiler warnings → the test passes → the suite still passes.**
* No commit tasks. Commits happen outside this workflow.

### Deviation from the requested layout, called out deliberately

**CMake registration is T001, not a final task.** `dsp/tests/CMakeLists.txt`'s
`dsp_processors_tests` source list is **enumerated, not globbed** — verified this session: the list
opens at `dsp/tests/CMakeLists.txt:156` (`add_executable(dsp_processors_tests`) and closes with the
`)` at `:294`, immediately after the Vorago Phase 1 block at `:287-293`. An unregistered TU compiles
into nothing and its cases silently never run, so registering all four TUs up front is the only
ordering under which every later task's "run the suite" step proves anything. The final group still
carries a **registration-completeness audit** (T014) that re-verifies the four entries and the
`-fno-fast-math` invariant, alongside the full-suite run (T015) and the portability/lint gate (T016).

**Parallelism is genuinely scarce here.** The phase is one header plus four TUs; almost every task
either edits that header or edits a TU another task also edits. Only two `[P]` pairs are honest:
T001/T002 (CMake + stubs vs. `spec.md`) and T004/T005 (header + main TU vs. the new flux helper +
the spectral TU). Nothing else is marked, and nothing should be.

### Build and run commands (Windows; the full CMake path is mandatory)

```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests
build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "SpectralSmear_*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "[long]" 2>&1 | tail -20
```

* Catch2 filters: test-case name as a **positional** argument (`"SpectralSmear_Latency"`), tags in
  `[brackets]`. Never `-c` (that filters SECTIONs).
* `[long]` cases run locally by default and are excluded from the per-push CI filter
  `~[performance]~[perf]~[benchmark]~[!benchmark]~[long]`.
* **Timing runs alone**: `node tools/run-cpu-tests.js dsp_processors_tests`, nothing else executing —
  no build, no clang-tidy, no second suite, no parallel agent.
* Capture slow runs (`[long]`, perf, clang-tidy) to a log file on the **first** run and read the log.
  Never re-run a suite just to grep its output.

### Conventions every task inherits

* Namespace `Krate::DSP`; classes PascalCase, methods camelCase, members trailing underscore,
  constants `kPascalCase`.
* Every new case is tagged `[spectral_smear]`, plus `[long]` or `[.perf]` where the task says so:
  `TEST_CASE("SpectralSmear_Xxx", "[spectral_smear]")`.
* Finiteness is `Krate::DSP::detail::isFinite` / `isNaN` / `isInf` (`core/db_utils.h:118`, `:99`,
  `:260`) — **never** `std::isnan`/`std::isinf`/`std::isfinite`
  (`tools/lint-nonfinite-symbols.js` gates this), in the header **or** in any test.
* **Non-finite values may be named in exactly one TU**, `spectral_smear_nonfinite_test.cpp`, and
  there they are built from bit patterns through a volatile sink — never
  `std::numeric_limits<float>::quiet_NaN()` / `infinity()`, which fold to finite garbage on the
  macOS/Linux `-ffast-math` legs. The transcribable idiom is
  `dsp/tests/unit/systems/resonance_drift_network_nonfinite_test.cpp:143-155`
  (`makeNonFinite(std::uint32_t bits)`, patterns `0x7FC00000` / `0x7F800000` / `0xFF800000` at
  `:130-134`).
* **No bit-exact float goldens** anywhere (`tools/lint-float-bit-goldens.js`). Where a render must be
  pinned, use `tests/test_helpers/render_fingerprint.h` (`kSampleTolerance = 5.0e-4f` `:58`,
  `kMetricTolerance = 2.5e-4` `:61`, `compareFingerprints` `:122`). The only exact comparisons
  allowed are "these bytes were **not** written" (SC-002 (d)'s bypass, the leading warm-up zeros) and
  "this stored value was not touched" (setter clamp reads).
* Brace-initialised aggregates use **designated initialisers**
  (`SpectralSmear::PrepareConfig{.fftSize = 1024, .enabled = true}`) — Clang errors on narrowing
  where MSVC does not.
* Tests include `tests/test_helpers/allocation_detector.h` only; **never**
  `allocation_operator_overrides.h` — `dsp_processors_tests` already has its single owner at
  `dsp/tests/unit/processors/brownian_drift_test.cpp:28`, and a second include is a duplicate-symbol
  link error (`tools/lint-allocation-operator-overrides.js` gates it).
* Every render discards `2 * fftSize` output samples before measuring: output `[0, fftSize)` is the
  warm-up counter's zeros and `[fftSize, 2·fftSize − hop)` is `OverlapAdd`'s COLA ramp-up (plan S11).
* **Zero compiler warnings is part of every task's definition of done**, not a later cleanup.

### The geometry table every task refers to

| `fftSize` | `hop` | `numBins` | `fifoCapacity` = `bit_ceil(fft+64+hop)` | `getAllocatedBytes()` |
|---|---|---|---|---|
| 512 | 128 | 257 | 1024 | 15 384 |
| 1024 | 256 | 513 | 2048 | 30 744 |
| 2048 (default) | 512 | 1025 | 4096 | 61 464 |
| 4096 | 1024 | 2049 | 8192 | 122 904 (120.0 KiB ≤ 128 KiB) |

Formula: `(4*numBins + 2*numBins + 2*fifoCapacity + 2*hop) * sizeof(float)` (plan S10).

---

## Group A — Registration and spec corrections

### T001 [P] — Create the four TU stubs and register them (`dsp/tests/CMakeLists.txt`)

**Files to create** (stubs that compile and link, no `TEST_CASE` yet):

* `dsp/tests/unit/processors/spectral_smear_test.cpp`
* `dsp/tests/unit/processors/spectral_smear_spectral_test.cpp`
* `dsp/tests/unit/processors/spectral_smear_perf_test.cpp`
* `dsp/tests/unit/processors/spectral_smear_nonfinite_test.cpp`

Each stub is exactly:

```cpp
// Vorago Phase 4 (specs/vorago-phase4-spectral-smear): <role of this TU>
#include <catch2/catch_test_macros.hpp>
```

**File to edit:** `dsp/tests/CMakeLists.txt`

1. Append inside `add_executable(dsp_processors_tests ...)` — the list opens at `:156` and closes
   with the `)` at `:294`; append **after** `unit/processors/vorago_p1_harness.cpp` (`:293`) and
   before that `)`:

```cmake
    # Vorago Phase 4 (specs/vorago-phase4-spectral-smear): SpectralSmear.
    # This list is ENUMERATED, not globbed - an unregistered TU silently drops
    # out of the build and its cases never run.
    #   spectral_smear_test.cpp           SC-002, SC-003, SC-007, SC-008, SC-009,
    #                                     SC-011, SC-012 (c), SC-017, SC-018 (a)
    #                                     + geometry / clamps / boundaries /
    #                                       pole-bounds / output-clamp cases
    #   spectral_smear_spectral_test.cpp  SC-001, SC-004, SC-005, SC-006, SC-010,
    #                                     SC-012 (a)(b), SpectralSmear_TimeConstantLaw,
    #                                     SpectralSmear_DcNyquistSmear, the flux
    #                                     helper sanity case  -- the [long] set
    #   spectral_smear_perf_test.cpp      SC-013 (a)-(d), [.perf] only
    #   spectral_smear_nonfinite_test.cpp SC-016, SC-018 (b), FR-009's non-finite
    #                                     setter arm -- the ONE -fno-fast-math TU
    unit/processors/spectral_smear_test.cpp
    unit/processors/spectral_smear_spectral_test.cpp
    unit/processors/spectral_smear_perf_test.cpp
    unit/processors/spectral_smear_nonfinite_test.cpp
```

2. Append **only** the non-finite TU to the `-fno-fast-math -fno-finite-math-only`
   `set_source_files_properties` block. That block's guard is at `:510`
   (`if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")`), `set_source_files_properties(` at `:511`, its
   current last entry `unit/systems/resonance_drift_network_nonfinite_test.cpp` at `:820`, and
   `PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"` at `:821`. Insert after `:820`,
   before the `PROPERTIES` line:

```cmake
        # Vorago Phase 4: this TU owns EVERY non-finite value in the phase -
        # SC-016's NaN/Inf audio injection, SC-018 (b)'s poison-then-reprime arm,
        # and FR-009's non-finite setter arm - because all three build their values
        # from bit patterns through a volatile sink and need IEEE semantics to
        # assert on them. ONLY this one of the four Phase 4 TUs is listed; the
        # other three must NOT be, and none of them may name a non-finite value:
        # std::numeric_limits<float>::quiet_NaN()/infinity() fold to finite garbage
        # on the -ffast-math legs, so such a test passes vacuously or reds at
        # random. spectral_smear_test.cpp and spectral_smear_spectral_test.cpp stay
        # out so the FR-008/FR-009 guards are proved in the /fp:fast + -ffast-math
        # mode the header actually ships in (the
        # resonance_drift_network_test.cpp:38-42 precedent). The perf TU stays out
        # too: -fno-fast-math would change the figures its baselines are pinned to.
        unit/processors/spectral_smear_nonfinite_test.cpp
```

**No** change to `dsp/CMakeLists.txt` (the component is header-only), **no** change to
`tests/test_helpers/CMakeLists.txt` (it is an INTERFACE library exporting its own directory,
`:7-12`, and `dsp_processors_tests` already links it at `dsp/tests/CMakeLists.txt:300`), and **no**
plugin, CI, clang-tidy-script or preset change anywhere in this phase.

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests
build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -5
```

Builds warning-free; the suite still reports all tests passing (the four stubs add no cases yet).

---

### T002 [P] — Apply the plan's spec corrections to `spec.md`

**File to edit:** `specs/vorago-phase4-spectral-smear/spec.md` — and nothing else. This task writes
no code and runs no test; it closes the gaps plan S17 recorded as "needs a spec amendment", so the
compliance pass is not scored against text the build deliberately contradicts.

Apply exactly these, each with a one-line "(corrected at the tasks stage, plan S17 C-n)" marker:

1. **C-1 — FR-071's enumerated diff list.** FR-071 currently permits changes only in "the new header,
   the four new test TUs, `dsp/tests/CMakeLists.txt`, `dsp/lint_all_headers.cpp` … and this spec
   directory". Add **two** files: `tests/test_helpers/spectral_flux.h` (new, required by SC-004 and
   Clarifications Q2) and `specs/Vorago-roadmap.md` (the Clarifications-Q4 amendment the plan stage
   already applied — verify with `git diff --stat -- specs/Vorago-roadmap.md`, which must show a
   3-line replacement at `:257-259` and nothing else). Without this edit FR-071's gate is red before
   implementation starts.
2. **C-5 — SC-018's measurement.** Replace the per-bin `|Δ| ≤ 1e-6` re-analysis with the
   sample-domain form the build implements (plan S13.2): arm (a) compares `out[fftSize,
   fftSize + hopSize)` of a `smearAmount = 1` render against the **same input through the same
   component at `smearAmount = 0`** and asserts `rms(actual − reference) / rms(reference) ≤ −60 dB`;
   arm (b) does the same after a poison clear. State why: FR-014 makes frame 0 unobservable after
   exactly `fftSize` pushed samples, and the only frame-0-exclusive output is one `w²`-tapered,
   COLA-incomplete window that cannot be re-analysed onto the bin grid at `1e-6`. Record that the
   replacement still fails a zero-initialised memory by ~30 dB, so no threshold was relaxed.
3. **C-6 — three API/Traceability corrections.**
   (i) FR-018's enumerated public query surface gains
   `[[nodiscard]] static float poleForTau(float tauSeconds, std::size_t hopSize, double sampleRate) noexcept`,
   and FR-023's sentence naming `poleTable()` as the white-box surface is corrected to name
   `poleForTau(...)` (`poleTable()` is private; a pure static is what lets the Edge-Case assertion
   reach synthetic extremes no render can).
   (ii) FR-061 gains an enforcing criterion, `SpectralSmear_OutputClamp` — its only current
   Traceability target, SC-005, is satisfied by a build with **no clamp and no counter** (arm (ii)
   passes because the peak is near 1.0 anyway, arm (iii) asserts the path is *not* taken).
   (iii) FR-025 gains an enforcing criterion, `SpectralSmear_DcNyquistSmear` — SC-004 (a) and
   SC-005 (v) both perturb by O(0.2 %) when 2 of 1025 bins are omitted, against gates with three
   orders of magnitude of slack.
4. **C-7 — FR-036's cost and cadence.** Replace "`3 * numBins` `exp` calls" and "control-thread
   cadence" with the true figures: `numBins` `log` + `3·numBins` `pow` + `3·numBins` `exp`
   (≈ 14 300 transcendentals at `fftSize = 4096`), and state that `setSmearTimeLow`/`setSmearTimeHigh`
   only **mark the tables dirty** while the rebuild runs at the top of the next `processBlock`, at
   most once per block, measured by SC-013 (d). FR-006 puts setters on the render thread, so there is
   no control thread to hide the cost on.
5. **C-10 — SC-012 (c)'s reference render.** "The un-stepped render" becomes "the **destination**
   setting held constant for the whole render, compared over the same absolute sample window".
   Against the origin setting the arm compares a random-phase narrowband process (RMS restored by
   FR-042's make-up) with a sinusoid and can exceed 6 dB on crest factor alone — failing a correct
   build.
6. **C-11 — SC-016 (iv).** Correct the prose "covered by 4 consecutive analysis frames" to **5**
   (`floor((L−1+N−1)/hop) + 1 = floor(2558/512) + 1 = 5` at `L = 512`, `N = 2048`, `hop = 512`; the
   bound `ceil(fftSize/hopSize) + 1 = 5` is exactly tight, not padded), and state the **measurement**,
   which the criterion omits: silent *frames* are not observable through any public API, so the
   assertion is on the maximal run of consecutive near-silent output **samples**,
   `(silentFrames − numOverlaps + 1) · hopSize = 2 · hopSize = 1024`.
7. **C-12 — SC-017's frame origin and comparison.** "Issue `setSmearAmount(1.0)` at a known sample
   index" becomes "issue it **before the first `processBlock` call**", so `f` counts frames from the
   same origin as the trajectory `1 − coeff^f`; record the generalisation
   `f = max(0, floor((samplesProcessed − callIndex − fftSize)/hopSize) + 1)` for any future
   mid-render variant. Change the comparison from `<` to `≤ 1e-4`: `OnePoleSmoother::process()`
   snaps once `|current_ − target_| < kCompletionThreshold = 1e-4f` (`primitives/smoother.h:199-201`,
   `:53`), so the tolerance is exactly tight at that frame and a strict `<` is a coin flip.
8. **C-13 — SC-013 (a)'s configuration.** "(a) defaults" becomes
   "(a) `PrepareConfig{.fftSize = kDefaultFftSize, .enabled = true}` with default **control** values
   (`smearAmount = 0`, `decoherence = 0`, `tilt = 0`) — both identity gates engaged, the full stereo
   STFT round trip still paid". Under revision 2's `enabled = false` default a literal reading
   measures a true bypass (tens of ns) and collides with SC-013's own anti-no-op floor
   `static_assert(kBaseline >= kReferenceNs / 50.0)` = 1 066 ns.
9. **C-17 — NEW, raised at this stage: SC-018 arm (b) moves TU.** SC-018 is listed as living in
   `spectral_smear_test.cpp`, but arm (b) requires injecting a non-finite sample to fire FR-062's
   poison clear, and that TU is deliberately **not** in the `-fno-fast-math` block and may not name a
   non-finite value at all (the `resonance_drift_network_test.cpp:38-42` house rule). Amend the
   "Planned test translation units" table and SC-018's text: **arm (a) stays in
   `spectral_smear_test.cpp`; arm (b) ships as `SpectralSmear_MagnitudePrimingAfterPoison` in
   `spectral_smear_nonfinite_test.cpp`.** Nothing about the measurement changes.

**Verify:** `git diff --stat -- specs/vorago-phase4-spectral-smear/spec.md` shows only that file; no
source file is touched by this task; `dsp_processors_tests` is unaffected.

---

## Group B — Header compile surface, lifecycle and the read surface

### T003 — Lifecycle, geometry, footprint and the control surface

**Files to create:** `dsp/include/krate/dsp/processors/spectral_smear.h`
**Files to edit:** `dsp/tests/unit/processors/spectral_smear_test.cpp`, `dsp/lint_all_headers.cpp`

**Write these cases FIRST** in `spectral_smear_test.cpp`. They will fail to **compile** — that is the
red state for this task, because there is no header yet; the whole point of this task is that every
later task's tests can be red *at runtime* instead. Add the shared fixture helpers now
(`kFs48 = 48000.0`, `kRefFft = 2048`, `kRefHop = 512`, a `makePrepared(fftSize, enabled = true)`
factory, and a `renderStereo(SpectralSmear&, const std::vector<float>& inL, const std::vector<float>& inR, std::vector<float>& outL, std::vector<float>& outR, partition)`
driver that copies input to output buffers and calls `processBlock` in the given partition).

1. `TEST_CASE("SpectralSmear_Geometry", "[spectral_smear]")`
   * Unprepared instance: `isPrepared() == false`, `isEnabled() == false`, `getFftSize() == 0`,
     `getHopSize() == 0`, `getNumBins() == 0`, `getSampleRate() == 0.0`,
     `getLatencySamples() == 0`, `getAllocatedBytes() == 0`, `getClampEngagements() == 0`,
     `getPoisonEngagements() == 0`, and the three applied reads are exactly `0.0f`.
   * `prepare(48000.0, {.fftSize = X, .enabled = true})` for `X ∈ {0, 1, 100, 513, 5000, SIZE_MAX}`
     gives `getFftSize() ∈ {512, 512, 512, 512, 4096, 4096}` respectively (clamp to `[512, 4096]`
     **then** `std::bit_floor`), with `getHopSize() == getFftSize()/4` and
     `getNumBins() == getFftSize()/2 + 1` in every case.
   * `prepare(0.0, …)` and `prepare(-48000.0, …)`: `isPrepared() == false` and **all four** geometry
     reads are `0` / `0.0` (plan S2 step 1's ordered guard list).
   * Re-prepare path: a good `prepare(48000.0, {.fftSize = 2048, .enabled = true})` followed by
     `prepare(0.0, …)` leaves `isPrepared() == false`, all four geometry reads at `0`, and
     `getAllocatedBytes() == 0` — which means "this instance will not render", **not** "this instance
     holds no heap"; the vectors are deliberately retained (plan S2 step 1, S9). Assert only the
     reads; do not assert anything about the heap.
   * Double `prepare()` at different rates and sizes (48 k/2048 → 96 k/512 → 44.1 k/4096): every
     geometry read follows the last call.
2. `TEST_CASE("SpectralSmear_Latency", "[spectral_smear]")` — **arms (a), (d), (e) only** in this
   task; arms (b)/(c) need the render path and land in T006.
   * (a) `getLatencySamples() == fftSize` for `fftSize ∈ {512, 1024, 2048, 4096}` when prepared
     enabled; `0` before `prepare()`.
   * (d) `PrepareConfig{.fftSize = 2048, .enabled = false}`: `isPrepared() == true`,
     `isEnabled() == false`, `getLatencySamples() == 0`, `getAllocatedBytes() == 0`, and a 10 s
     white-noise stereo render comes back **bit-identical** to the input (exact `==` per sample —
     this is "these bytes were not written", not a pinned computation, so
     `lint-float-bit-goldens.js` is satisfied).
   * (e) Two instances at `fftSize = 2048`, one enabled and one disabled: `getFftSize()`,
     `getHopSize()`, `getNumBins()` and `getSampleRate()` are **identical**. Set
     `setSmearAmount(0.7f)`, `setDecoherence(0.3f)`, `setSmearTilt(-0.4f)` on both: on the disabled
     one `getAppliedSmearAmount() == 0.7f`, `getAppliedDecoherence() == 0.3f`,
     `getAppliedSmearTilt() == -0.4f` **exactly** (no frame runs, so the applied reads mirror the
     targets); `getLatencySamples() == 0` and `getAllocatedBytes() == 0` stay put while the enabled
     twin reports `2048` and `61464`.
3. `TEST_CASE("SpectralSmear_AllocatedBytes", "[spectral_smear]")`
   * For each `fftSize ∈ {512, 1024, 2048, 4096}` prepared **enabled**, `getAllocatedBytes()` equals
     `(4*numBins + 2*numBins + 2*fifoCapacity + 2*hop) * sizeof(float)` **recomputed in the test from
     the public geometry reads** with `fifoCapacity = std::bit_ceil(fftSize + 64 + hop)` — do not
     hard-code the four literals; a geometry change must not silently invalidate the case. Cross-check
     against the table in this file's header (15 384 / 30 744 / 61 464 / 122 904).
   * `getAllocatedBytes() <= 128 * 1024` at `fftSize = 4096`.
   * **Report, never assert**, the sub-object figure via `WARN`: at `fftSize = 4096` it is
     ≈ 524 KiB (`STFT::inputBuffer_` `fftSize*8` floats per channel = 262 144 B, `stft.h:78`;
     `windowedFrame_`+`window_` = 65 536 B; `OverlapAdd::outputBuffer_` `fftSize*2` = 65 536 B plus
     `ifftBuffer_`+`synthesisWindow_` = 65 536 B; two `SpectralBuffer`s ≈ 65 568 B). It is
     `STFT`/`OverlapAdd`/`SpectralBuffer`/`FFT` policy, not this component's (FR-017).
   * Second arm: `enabled = false` ⇒ `getAllocatedBytes() == 0` at every geometry.
4. `TEST_CASE("SpectralSmear_ControlClamps", "[spectral_smear]")` — **no non-finite values in this
   TU** (see Conventions; that arm is T012's `SpectralSmear_NonFiniteSetters`).
   * Defaults after construction and after `prepare()`: `getSmearAmount() == 0.0f`,
     `getDecoherence() == 0.0f`, `getSmearTilt() == 0.0f`, `getSmearTimeLow() == 3.0f`,
     `getSmearTimeHigh() == 0.25f`.
   * Range ends: `setSmearAmount(-1.0f)` → `0.0f`, `setSmearAmount(2.0f)` → `1.0f`; same shape for
     `setDecoherence`; `setSmearTilt(-5.0f)` → `-1.0f`, `setSmearTilt(5.0f)` → `+1.0f`;
     `setSmearTimeLow(0.0f)` → `0.02f`, `setSmearTimeLow(100.0f)` → `10.0f`; same for
     `setSmearTimeHigh`.
   * Exactly `0.0f` and exactly `1.0f` round-trip through the target reads unchanged; so does
     `std::numeric_limits<float>::min()` (the smallest positive **normal** float — a legal finite
     value, not a non-finite one) and `std::nextafter(0.0f, 1.0f)`.
   * `tauLow == tauHigh` (both `1.0f`) and `tauLow < tauHigh` (`0.25f` / `3.0f`) are both stored
     **verbatim** — the component must never silently swap the endpoints (FR-031 Edge Cases; the
     behavioural consequence is T009's `SpectralSmear_TimeConstantLaw`).
   * Every setter called **before** `prepare()` stores and clamps, and does not crash (a table
     rebuild must never touch a zero-sized vector).
   * `setSeed(0)`: `getSeed() == 0`, and both derived streams stay live and distinct — assert this
     indirectly here by `deriveStreamSeed(0, 0x1000) != deriveStreamSeed(0, 0x2000)` and both
     non-zero (`core/random.h:102-113`); the render-level consequence is SC-001 arm (c) in T011.

**Then implement** `dsp/include/krate/dsp/processors/spectral_smear.h` exactly per plan S1, S2, S3
and S9:

* Banner `// Layer 2: DSP Processor - Spectral Smear`, `#pragma once`, the include list of plan S1.1
  (Layers 0–1 only: `core/audio_constants.h`, `core/db_utils.h`, `core/interpolation.h`,
  `core/math_constants.h`, `core/random.h`, `core/window_functions.h`, `primitives/smoother.h`,
  `primitives/spectral_buffer.h`, `primitives/stft.h`, plus
  `<algorithm> <array> <bit> <cassert> <cmath> <cstddef> <cstdint> <vector>`). **No Layer 3/4
  header.**
* **The naming hazard, recorded in a header comment:** `kMinFFTSize`/`kMaxFFTSize` are
  *namespace-scope* constants in `Krate::DSP` (`primitives/fft.h:44`, `:47`, values 256 / 8192).
  This component's bounds are spelled `kMinFftSize`/`kMaxFftSize` (lower-case `ft`), are class
  statics, and the header must never write the namespace spelling unqualified inside a member.
* All constants from plan S1.2, including the two `static_assert`s
  (`kMinFftSize >= kMinFFTSize && kMaxFftSize <= kMaxFFTSize`,
  `kMinFftSize % kOverlapFactor == 0`) and `static_assert(kSaltDecohereL != kSaltDecohereR)`.
* `PrepareConfig` per plan S1.3 — `fftSize = kDefaultFftSize`, `enabled = false`, with the
  **mandatory comment** recording that the `false` default deliberately diverges from
  `AtmosphereEngine::PrepareConfig::blurEnabled_`'s `true` (`atmosphere_engine.h:371`) because this
  stage sits on the global bus (FR-019, D-11, Clarifications Q6).
* The complete public API of plan S1.4 and the private state of plan S1.5, move-only.
* `prepare()`'s twelve ordered steps (plan S2) — including step 1's exact guard list, step 3's
  allocate-nothing disabled path, step 8's `warmupRemaining_ = fftSize_`, step 11's `setSeed(seed_)`
  **last**, and step 12's `allocatedBytes_` formula.
* `reset()` and `setSeed()` per plan S3, with the "NOT touched" comment block.
* The setters per plan S8's `sanitise()` shape — `std::clamp(detail::isFinite(v) ? v : dflt, lo, hi)`
  — carrying the **C-2 comment**: this is FR-009's *substitution* rule
  (`atmosphere_engine.h:911`), deliberately the opposite of `ResonanceDriftNetwork`'s FR-008
  *rejection* rule, so a reviewer arriving from Phase 3 is not misled.
* The three applied getters written **exactly** as plan S9 spells them (unprepared → `0.0f`,
  disabled → the target, enabled → `smearSm_.getCurrentValue()`). The prepare-time smoother snap is
  *not* the mechanism; relying on it fails SC-002 (e).
* `poleForTau()` and `coherenceMakeup()` fully implemented (plan S4.3, S7.2) — they are pure statics
  and T004's tests drive them.
* `rebuildPoleTables()` **declared and called from `prepare()` step 5**, with a body that fills the
  three tables (T004 proves the law; write the S4.3 shape now rather than a stub, so `prepare()` is
  never able to leave a zero-filled table).
* `processBlock()` present as an **inert stub**: the FR-003 entry guards (plan S5.1) followed by
  `return;`, with a `// TODO(T006): render path` comment. This is what makes SC-002 (d)'s bypass arm
  pass and every render-dependent case fail loudly rather than fail to build.
* `setSmearTimeLow`/`setSmearTimeHigh` carry the `@note` of plan S1.4 (allocation-free but not free;
  deferred rebuild; ~14 300 transcendentals at `fftSize = 4096`; SC-013 (d) prices it).

**Also edit `dsp/lint_all_headers.cpp`:** insert, alphabetically between
`#include <krate/dsp/processors/spectral_morph_filter.h>` (`:137`) and
`#include <krate/dsp/processors/spectral_tilt.h>` (`:138`):

```cpp
#include <krate/dsp/processors/spectral_smear.h>
```

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "SpectralSmear_*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -5
```

All four new cases pass; the whole suite still passes; zero warnings.

---

## Group C — The time-constant law, and the flux helper

### T004 — Pole tables, tilt law, deferred rebuild, per-frame resolve

**Files to edit:** `dsp/include/krate/dsp/processors/spectral_smear.h`,
`dsp/tests/unit/processors/spectral_smear_test.cpp`

**Write these cases FIRST:**

1. `TEST_CASE("SpectralSmear_PoleTableBounds", "[spectral_smear]")` — the white-box assertion FR-023
   needs, driving the public static `poleForTau(tauSeconds, hopSize, sampleRate)` (plan S4.3).
   * Every result is in `[0.0f, kMaxPole]` with `kMaxPole = 0.99999f`, for
     `tau ∈ {0.02f, 0.25f, 1.0f, 3.0f, 10.0f, 1.0e6f, 1.0e30f}` × `hopSize ∈ {128, 512, 1024}` ×
     `sampleRate ∈ {44100.0, 48000.0, 96000.0, 192000.0}`.
   * Degenerate products return `0.0f`: `poleForTau(0.0f, 512, 48000.0) == 0.0f` and
     `poleForTau(1.0f, 512, 0.0) == 0.0f` (the ordered `!(denom > 0.0f)` guard).
   * The two reachable extremes from plan S12, with numbers:
     `poleForTau(10.0f, 1024, 44100.0) ≈ 0.99768` (±1e-4) and
     `poleForTau(10.0f, 1024, 192000.0) ≈ 0.99947` (±1e-4) — three orders of magnitude **below**
     `kMaxPole`, which is the evidence that `kMaxSmearSeconds`, not `kMaxPole`, is the binding bound.
     `poleForTau(0.02f, 1024, 44100.0) ≈ exp(-1.1610) ≈ 0.31316` (±1e-4) — the only shipped
     configuration where `hop/(sampleRate·tau) > 1`; assert it is still in `(0, 1)`, i.e. the
     integrator stays a convex combination.
   * Monotonicity: `poleForTau` is strictly increasing in `tau` and strictly decreasing in `hopSize`.
2. `TEST_CASE("SpectralSmear_CoherenceMakeupInterpolant", "[spectral_smear]")` — the pure-function
   arm of FR-042 (the *measured* arm is SC-006 in T011).
   * `SpectralSmear::kCoherenceKnotCount == 5` and
     `kCoherenceMakeup == {1.0000f, 1.0799f, 1.3435f, 1.7746f, 1.9996f}` (transcribed from
     `effects/aether_reverb.h:2775`; exact equality is legitimate — these are the shipped literals,
     not a computation).
   * `coherenceMakeup(0.0f) == 1.0f` **exactly** (`cubicHermiteInterpolate` returns `y0` at `t == 0`,
     `core/interpolation.h:88`), and `coherenceMakeup(d)` reproduces `kCoherenceMakeup[i]` to within
     `1e-5` at each `d = i/4`.
   * `coherenceMakeup` is non-decreasing over 101 samples of `d ∈ [0, 1]`, and every value is in
     `[1.0f, 2.05f]`.
   * Out-of-range inputs clamp: `coherenceMakeup(-1.0f) == coherenceMakeup(0.0f)` and
     `coherenceMakeup(2.0f) == coherenceMakeup(1.0f)`.

**Then implement**, per plan S4:

* `tau(k)` per S4.1: `u(k) = clamp(log(f_k/20) / log(1000), 0, 1)` with `f_k = k·sampleRate/fftSize`
  and **bin 0 using `u = 0` directly** (never `log(0)`); `tau(k) = tauLow·pow(tauHigh/tauLow, u)`.
* `tilted(base, f, tilt)` per S4.2:
  `clamp(base · pow(kTiltPivotHz / max(f, kTauAnchorLowHz), tilt · kTiltExponentRange), kMinSmearSeconds, kMaxSmearSeconds)`.
  The header must document the **normative saturation** at the law's definition site: at the shipped
  defaults and `tilt = +1`, `tau(20 Hz) = 3.0 · 50^0.75 = 56.4 s` clamps to 10 s, so every bin below
  ≈ 95 Hz sits on the clamp (Clarifications Q5); `kTiltExponentRange` is **not** shrunk to avoid it.
* `rebuildPoleTables()` fills `poleNeg_`/`poleZero_`/`polePos_` and ends with
  `tiltScratchDirty_ = true; poleTablesDirty_ = false;` — it is the **only** writer of the three
  tables, which is what makes the `t != lastResolvedTilt_` guard safe (plan R15).
  **The `tilt = 0` row is written without the `pow`** (`clampTau(base)` directly): `std::pow(x, 0.0f)`
  is a library call the compiler need not fold.
* `setSmearTimeLow`/`setSmearTimeHigh` clamp, store and set `poleTablesDirty_ = true` — **nothing
  else**. The rebuild is deferred to the top of `processBlock` (T006 wires that call in; the guard
  already exists in the inert stub — add `if (poleTablesDirty_) rebuildPoleTables();` after the entry
  guards now).
* `resolveTiltScratch(t)` per S4.3: `poleScratch_[k] = (t <= 0) ? lerp(poleNeg_, poleZero_, t+1) : lerp(poleZero_, polePos_, t)`,
  guarded by `tiltScratchDirty_ || t != lastResolvedTilt_`. Two multiplies and an add per bin, no
  transcendental. The header states that **the interpolated form IS the specified law** (FR-034,
  D-7), not an approximation of one.
* Header comment carrying the S4.3 cost table (`numBins` `log` + `3·numBins` `pow` + `3·numBins`
  `exp` ≈ 7 transcendentals/bin, ~14 300 at `fftSize = 4096`) and the reason the rebuild is deferred.

**Verify:** as T003. Both new cases pass; the whole suite still passes; zero warnings.

---

### T005 [P] — The magnitude-flux test helper and its sanity case

**Files to create:** `tests/test_helpers/spectral_flux.h`
**Files to edit:** `dsp/tests/unit/processors/spectral_smear_spectral_test.cpp`

Parallel-safe with T004: disjoint file sets, and the helper depends on nothing from `SpectralSmear`.
**No CMake edit** — `test_helpers` is an INTERFACE library exporting its own directory
(`tests/test_helpers/CMakeLists.txt:7-12`) and `dsp_processors_tests` already links it
(`dsp/tests/CMakeLists.txt:300`).

**Create the helper** (plan S13.1) — a plain function in `namespace Krate::DSP::TestUtils`, not a
class:

```cpp
/// Mean normalised frame-to-frame L1 magnitude flux inside [lowHz, highHz].
///   flux = mean over frames f of ( sum_k |mag_f[k] - mag_{f-1}[k]| / sum_k mag_f[k] )
/// Frames whose denominator is below kFluxSilenceFloor are skipped (silence has no
/// flux); returns 0.0 if every frame is skipped.
///
/// "REDUCTION" IS ALWAYS THE RATIO flux(amount=0)/flux(amount=1), NEVER the
/// subtractive form 1 - flux(1)/flux(0): at SpectralSmear's shipped endpoints the
/// ratio reads ~4.8 at tilt 0 and ~114 at tilt +1, while the subtractive form reads
/// 1.10 and 1.91 and FAILS SC-004 (b)'s factor-of-2 gate on a correct build.
[[nodiscard]] double computeMagnitudeFlux(const float* signal, std::size_t numSamples,
                                          double sampleRate, std::size_t fftSize,
                                          std::size_t hopSize, float lowHz, float highHz);
```

Implementation notes that are **load-bearing, not style**:

* It runs its **own, independent** `STFT` + `SpectralBuffer` over the signal it is handed — never a
  `SpectralSmear`'s internal buffers.
* It **must push in chunks of at most `hopSize`** and drain with `while (stft.canAnalyze())`.
  `STFT::pushSamples` has **no overflow guard** (`primitives/stft.h:104-124`) and the ring is
  `fftSize * 8` (`:78`); a helper that pushes a 30-second render in one call corrupts memory. This is
  the single most likely way to get the helper wrong.
* Band → bin range: `kLo = ceil(lowHz * fftSize / sampleRate)`, `kHi = floor(highHz * fftSize / sampleRate)`,
  both clamped into `[0, numBins)`; `kLo > kHi` returns `0.0`.
* The first analysed frame has no predecessor and is used only to seed `mag_{f-1}`.
* Use `Krate::DSP::detail::isFinite` for any finiteness check — never `std::isfinite`.

**Write the sanity case** in `spectral_smear_spectral_test.cpp`:

`TEST_CASE("SpectralSmear_FluxHelperSanity", "[spectral_smear]")` (untagged beyond
`[spectral_smear]`; it is seconds, not minutes) — at 48 kHz, `fftSize = 1024`, `hop = 256`, full band
`[20, 20000]` Hz, 5 s signals:

* White noise scores `flux > 0.3`.
* A steady 1 kHz sine scores `flux < 0.05`.
* The **same white noise** whose per-bin magnitudes are pushed through a reference one-pole with
  `tau = 1.0 s` at the same frame rate (implement the 8-line reference smoother inside the case, on
  an independent STFT/OverlapAdd round trip — it is a *reference*, not the component) scores strictly
  between the two, and `flux(noise) / flux(smoothed noise) > 2`.
* All three figures are finite and `> 0`.

This exists so a helper bug can never be mistaken for a component bug in T009/T011.

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "SpectralSmear_FluxHelperSanity" 2>&1 | tail -5
```

---

## Group D — The render path

### T006 — Chunking, drain loop, FIFO, warm-up counter, make-up ramp, output clamp

**Files to edit:** `dsp/include/krate/dsp/processors/spectral_smear.h`,
`dsp/tests/unit/processors/spectral_smear_test.cpp`

This task builds the STFT round trip with the magnitude and phase passes **stubbed to identity**
(call sites present, bodies empty apart from the calls T007/T010 fill in). Everything it asserts must
therefore be true of a transparent round trip.

**Write these cases FIRST:**

1. `TEST_CASE("SpectralSmear_Latency", …)` — **extend** with arms (b) and (c).
   * (b) Kronecker impulse at input index `2 * fftSize` (**not** sample 0), defaults, prepared
     enabled at each `fftSize ∈ {512, 1024, 2048, 4096}`: the output's argmax index equals
     `2*fftSize + getLatencySamples()` **exactly** (no tolerance), and every sample in
     `[0, getLatencySamples())` is exactly `0.0f`.
     **The TU must carry the reason the impulse cannot sit at sample 0:** `Window::generateHann` is
     the *periodic* variant, so `window[0] == 0.5 − 0.5·cos(0) == 0.0f` exactly
     (`core/window_functions.h:111-121`); `STFT::analyze` reads the oldest `fftSize` samples from the
     stream origin (`primitives/stft.h:144-171`), so input sample 0 is covered by frame 0 only, at
     window index 0, where the window is zero. An impulse there is annihilated, the whole output is
     identically `0.0f`, and `argmax` of an all-zero buffer is index 0 — a correct implementation
     would "pass" for the wrong reason.
   * (c) Repeat (b) with `smearAmount = 1`, `decoherence = 1` and each tilt extreme:
     `getLatencySamples()` is unchanged and the first `getLatencySamples()` samples are still exactly
     `0.0f`. **No peak-index claim in this arm** — at `decoherence = 1` there is no impulse left to
     locate (and at this task's stage the phase pass is still identity, which is fine: the arm asserts
     only latency and leading zeros).
2. `TEST_CASE("SpectralSmear_NullAtZero", "[spectral_smear]")` — SC-003.
   * 20 s of white noise at −12 dBFS, and a second arm with a 5-partial tone; `smearAmount = 0`,
     `decoherence = 0`, tilt swept `{−1, 0, +1}` (tilt must be inert when amount is zero).
   * Align the output by `getLatencySamples()`, discard `2 * fftSize`, compare over the steady region.
   * **Threshold:** peak `|residual| ≤ 1.0e-4` and residual RMS ≤ **−80 dBFS** relative to input RMS.
     Hann at 75 % with the synthesis window is COLA (`stft.h:224-227`) so the round trip is
     analytically exact; the tolerance covers FFT-pair round-off only.
3. `TEST_CASE("SpectralSmear_PartitionInvariance", "[spectral_smear]")` — SC-011.
   * The same 60 s input (pink-ish noise plus a tone), `smearAmount = 0.5`, `decoherence = 0`,
     tilt 0, same seed, rendered (i) in uniform 512-sample blocks and (ii) with a pseudo-random block
     schedule drawn from `{1, 7, 30, 63, 64, 65, 300, 512, 1024, 4096}`.
   * `compareFingerprints` (`tests/test_helpers/render_fingerprint.h:122`) passes at the shipped
     tolerances, **sample-aligned with no offset correction**. A "drain when available"
     implementation fails this with a partition-dependent offset of `(ceil(fftSize/chunk) − 1)·chunk`
     (960 / 1020 / 1022 measured at chunk 64 / 30 / 7 in `aether_reverb.h:643-660`).
4. `TEST_CASE("SpectralSmear_ControlCadence", "[spectral_smear]")` — SC-017, the criterion that
   closes FR-013 (a)'s declared silent-failure mode.
   * Everything else static. Call `setSmearAmount(1.0f)` **before the first `processBlock` call**
     (spec correction C-12), then sample `getAppliedSmearAmount()` after each of the first 64 blocks.
   * Assert the trajectory matches `1 − coeff^f` to within **`≤ 1e-4`** (`≤`, not `<` — the smoother
     snaps once `|current_ − target_| < 1e-4f`, `primitives/smoother.h:199-201`, `:53`), where
     `coeff = calculateOnePolCoefficient(50.0f, sampleRate/hopSize)` (`primitives/smoother.h:77-93`;
     `exp(−5000/(50 · 93.75)) = 0.34424` at the reference geometry) and
     `f = max(0, floor((samplesProcessed − fftSize) / hopSize) + 1)`.
     **Not** `floor(samplesProcessed / hopSize)`, which over-counts by `fftSize/hopSize − 1 = 3`
     frames at every 75 % geometry, because the smoothers advance only inside
     `while (stft_[0].canAnalyze())` and `canAnalyze()` is `samplesAvailable_ >= fftSize_`
     (`stft.h:137`).
   * Run at **two partitions** (uniform 512 and case 3's pseudo-random schedule) and **two `fftSize`
     values** (2048 and 512). A double advance shows up immediately as `f` doubling; a per-block
     advance shows up as the trajectory decoupling from `hopSize`.
5. `TEST_CASE("SpectralSmear_NoAllocation", "[spectral_smear]")` — SC-007.
   * Include `tests/test_helpers/allocation_detector.h` **only**.
   * Inside an `AllocationScope` (`:111`): 5 000 `processBlock` calls at block sizes
     `{1, 7, 30, 64, 65, 511, 512, 2048}` cycled, interleaved with every FR-050 setter — including
     `setSmearTimeLow`/`setSmearTimeHigh`, which must also allocate nothing — and with `setSeed`.
   * **Threshold:** `getAllocationCount() == 0`. Read it **after** the scope closes, or inside it via
     `AllocationDetector::instance().getAllocationCount()`; `AllocationScope` latches in its
     destructor (`allocation_detector.h:117-119`).
6. `TEST_CASE("SpectralSmear_RenderPathBoundaries", "[spectral_smear]")`
   * `processBlock` with `numSamples ∈ {0, 1, 7, 63, 64, 65, 16384}` on a prepared enabled instance:
     no crash, every output sample finite, `getAllocatedBytes()` unchanged.
   * `processBlock` before `prepare()`, with a null `left`, and with a null `right`: the buffers come
     back **untouched** (fill them with a sentinel pattern first and assert it survives).
   * Disabled instance, then `prepare()` again with `enabled = true` on the same object: it allocates
     and renders exactly as a freshly prepared one (re-assert SC-002 (a) and SC-008's figure).
7. `TEST_CASE("SpectralSmear_OutputClamp", "[spectral_smear]")` — **the only thing that can detect
   FR-061 failing** (spec correction C-6 (ii)). SC-005's arms (ii) and (iii) are both satisfied by a
   build with no clamp and no counter.
   * Prepared enabled at the reference geometry, drive both channels with a signal of amplitude
     `≥ 6.0` (a 200 Hz sine at 6.5 amplitude is sufficient at this stage; once T010 lands, an
     amplitude `≥ 3.0` with `decoherence = 1` also reaches it through the ~2× make-up).
   * Assert (1) every output sample satisfies `|x| ≤ kOutputClamp` (`4.0f`), (2)
     `getClampEngagements() > 0`, (3) `reset()` returns the counter to `0`, and (4) a fresh
     `prepare()` also returns it to `0` (FR-054).

**Then implement**, per plan S5 and S7.3:

* `processBlock` entry guards (S5.1) then `if (poleTablesDirty_) rebuildPoleTables();` then the
  chunk loop (S5.2) at `kProcessChunkSamples = 64`. This bounds `STFT::samplesAvailable_` to
  `fftSize + 64`, which is what makes the unguarded `pushSamples` ring structurally safe.
* `pumpChunk` exactly as plan S5.3 spells it: push both channels → `while (stft_[0].canAnalyze())`
  { advance the three smoothers **once, outside the channel loop**, via `process()` (never
  `advanceSamples`) → `resolveTiltScratch(t)` → `g = coherenceMakeup(d)` → `for ch in {0,1}`
  { `analyze` → `smearMagnitudes(ch, a)` (stub: returns `false`) → `decohere(ch, …)` (stub: no-op)
  → `synthesize` → `pullSamples(hopScratch_[ch].data(), hopSize_)` → `writeHopToFifo(ch, g)` } →
  advance the shared FIFO cursors by `hopSize_` → `prevMakeup_ = g;` } → `popFifo(l, r, n)`.
* **Three header comments are mandatory at the loop**, because each invariant fails silently if
  disturbed (plan S5.3): (a) frame-major/channel-minor — the smoothers advance once per hop, not
  once per channel, and both channels share one control value (**SC-017 is the only criterion that
  can see this**); (b) the pull is inside the drain loop and is **always exactly `hopSize_`, never
  `n`** — `synthesize()` accumulates at offset 0 unconditionally (`stft.h:300-308`) and
  `pullSamples` returns **silently** on an oversized request without zeroing the destination
  (`stft.h:339-342`), so a wrong size is a stale-buffer read rather than a detectable failure;
  (c) L is always processed before R — part of the determinism contract, **bit-identical under swap
  today** (independent streams, no shared state), so it is discharged **by inspection at the
  compliance pass** and the implementer must not hunt for a test that cannot exist (plan S17 C-8).
* `popFifo` per plan S5.4 — **the warm-up counter is the rule**, not an emptiness test. The `else`
  branch (FIFO empty after warm-up) is unreachable in a prepared instance (plan S11's occupancy
  proof: occupancy `∈ (0, hop]` for every `n`) and exists so a future cadence change degrades to
  silence rather than to unwritten memory.
* `writeHopToFifo` per plan S7.3: the per-sample linear ramp from `prevMakeup_` to `g`
  (`w = (i+1)/hopSize`, so the ramp ends **exactly** at `g`), then `std::clamp(v, ±kOutputClamp)`
  with `++clampEngagements_` once per engaging **sample**. Header comment: `std::clamp` does **not**
  reject NaN (`v < lo` and `hi < v` are both false), which is why the finiteness backstop is
  upstream in FR-062's frame accumulator and explicitly not this clamp.
* `prevMakeup_` is updated **once per frame after both channels have consumed it** — a per-channel
  update would give R a different ramp from L.

**Verify:** as T003, plus `"SpectralSmear_PartitionInvariance"` and `"SpectralSmear_ControlCadence"`
individually (they are the slowest of this set). Zero warnings.

---

## Group E — The magnitude pass

### T007 — Leaky integrator, blend, priming, denormal flush, poison accumulator

**Files to edit:** `dsp/include/krate/dsp/processors/spectral_smear.h`,
`dsp/tests/unit/processors/spectral_smear_test.cpp`

**Write this case FIRST:**

`TEST_CASE("SpectralSmear_MagnitudePriming", "[spectral_smear]")` — SC-018 **arm (a)** (arm (b) is
T012, per spec correction C-17). The spec's original per-bin `1e-6` bin-domain form is **not
executable** (plan S13.2, spec correction C-5): FR-014 makes frame 0 unobservable after exactly
`fftSize` pushed samples, and the only frame-0-exclusive output is one `w²`-tapered, COLA-incomplete
window. The executed form is sample-domain:

* Controls: `smearAmount = 1`, `decoherence = 0`, `tilt = 0`, `tauLow = 3.0`, `tauHigh = 0.25`,
  reference geometry, 48 kHz.
* Call `reset()`, then render exactly `fftSize + hopSize` samples of a 1 kHz burst; take
  `out[fftSize, fftSize + hopSize)` — frame 0's exclusive contribution.
* **Reference:** the *same input through the same component at `smearAmount = 0`*, i.e. FR-021's
  exact-identity path, which leaves the analysed spectrum untouched and therefore reproduces the bare
  round trip **including the identical ramp-up taper**. (A bare `STFT` + `OverlapAdd` pair configured
  identically is an equally valid reference; the taper cancels either way, which is the point.)
* **Threshold:** `rms(actual − reference) / rms(reference) ≤ −60 dB`.
* Why this discriminates: a **primed** memory writes `state[k] = mag[k]` on frame 0, so at
  `amount = 1` the written magnitude *is* the analysed magnitude and the residual is FFT round-off,
  tens of dB below the gate. A **zero-initialised** memory writes `(1 − p)·mag[k]`, i.e. ≈ 1 % of
  amplitude (`p ≈ 0.99` across the band), leaving a residual ≈ 99 % of the reference — about
  −0.1 dB, a margin of more than 30 dB. The arm can neither pass by accident nor fail on round-off.

**Then implement** `smearMagnitudes(std::size_t ch, float amount)` exactly per plan S6, returning
`bool` (`true` = frame poisoned; the poison body itself is T012):

* One pass over `k ∈ [0, numBins_)` — **DC (bin 0) and Nyquist (`numBins_−1`) are INCLUDED**
  (FR-025). Their magnitude is a free real quantity and excluding them would leave two unsmeared
  spikes in the fog. **Do not copy FR-040's `for (k = 1; k + 1 < numBins; ++k)` bounds into this
  loop** — that is the most likely mistake in the phase (the two loops sit adjacent) and T009's
  `SpectralSmear_DcNyquistSmear` is the criterion that catches it.
* Three branches, in this order: **priming** (`primeNext_[ch]` → `state[k] = mag[k]`, clear the flag,
  write nothing — the write would be the identity); **exact-identity gate** (`amount == 0.0f` → the
  integrator still runs so the memory is never stale, but **no magnitude write at all**, which is
  more exactly transparent than writing `mag[k]` back); **full path** (integrator then
  `setMagnitude(k, m + amount * (state[k] − m))`).
* The integrator is `state[k] = m + pole[k] * (state[k] − m)` — one FMA, the fused form of
  `(1 − p)·mag + p·state`. **Normalisation by `(1 − p)` is a requirement, not a detail**: unity
  steady-state gain at every `p` is what lets FR-021's identity claim and SC-003's null test coexist
  with a knob that never changes the long-term average spectrum. Header comment: the continuous
  one-pole `dx/dt = (m − x)/tau` sampled at `hopSize/sampleRate` has the **exact** solution
  `exp(−hop/(sampleRate·tau))` = `poleForTau`; there is no bilinear/backward-Euler approximation
  anywhere, so `tau` means exactly what SC-004 (c) measures.
* `amount` is a **magnitude-domain blend, never a scale on the pole** (FR-021, D-12). The header
  carries the arithmetic: under `p(k) = amount·poleTable(k)`, at the reference geometry with the
  shipped defaults `poleTable(100 Hz) = 0.99366`, so `amount ∈ {0, .25, .5, .75, 1}` maps to
  effective time constants `0, 7.6 ms, 15 ms, 36 ms, 1.68 s` — four "no smearing" points and an
  endpoint, which makes a roadmap-declared modulation target do nothing over 99 % of a sweep.
* **Denormal flush** `if (state[k] < kDenormalFloor) state[k] = 0.0f;` with `kDenormalFloor = 1e-20f`,
  an **ordered comparison, not a bit test**, so `-ffast-math` cannot fold it and NaN (which fails an
  ordered `<`) passes through to the frame accumulator rather than being silently zeroed.
  **A mandatory comment at the flush site** (plan S6, spec correction C-9): the flush exists for
  hosts that have not set the MXCSR FTZ/DAZ bits; the repo's own test binaries enable FTZ/DAZ
  process-wide (`tests/test_helpers/enable_ftz_daz.h:27-32`, from `dsp/tests/dsp_test_main.cpp`), so
  **no render can observe its absence** — FR-024 is discharged **by inspection at the compliance
  pass**, not by a criterion, and no white-box helper is added for it.
* One `accum += m` per bin and **one** `detail::isFinite(accum)` test per frame per channel — never
  per bin (`atmosphere_engine.h:2250-2260` cost rule). On failure call `handlePoison(ch)` (T012) and
  return its `true`.
* **A mandatory comment at the ordering site** (FR-045): the phase pass runs **after** the magnitude
  write in the same frame so the memory always integrates the *analysed* magnitudes and never a value
  this component perturbed. The two orderings are **bit-identical today** (FR-040 writes only phase),
  so no render can distinguish them; it is specified anyway so a future magnitude-domain addition
  cannot silently create a feedback path. Discharged by inspection at the compliance pass.
* Header comment on cost: `SpectralBuffer` keeps a lazy dual representation with dirty flags
  (`primitives/spectral_buffer.h:7-12`, `:179-198`), so writing **both** magnitude and phase costs the
  same two bulk SIMD conversions as writing phase alone — the magnitude half is close to free
  relative to the round trip it shares.

**Verify:** as T003, and confirm `SpectralSmear_NullAtZero` and `SpectralSmear_OutputClamp` are
**still** green (the identity gate and the clamp must survive the magnitude pass landing). Zero
warnings.

---

## Group F — The magnitude-domain spectral criteria

### T008 — SC-004, SC-010 and the two Edge-Case criteria the plan added

**Files to edit:** `dsp/tests/unit/processors/spectral_smear_spectral_test.cpp` — and **only** that
file. No header change: if any case here fails, the defect is in T007's implementation and the fix
goes back through T007's task shape (test first, then header).

All arms use `Krate::DSP::TestUtils::computeMagnitudeFlux` from T005 and discard `2 * fftSize` output
samples (FR-022's priming rule, Clarifications Q1 — **not** `5 * tau`).

1. `TEST_CASE("SpectralSmear_MagnitudeMemory", "[spectral_smear][long]")` — SC-004.
   * **(a) Flux falls with amount, at every point.** Input: noise band-limited to **`[20 Hz, 16 kHz]`**
     (so both of (b)'s bands carry energy), amplitude-modulated at 4 Hz, ≥ 30 s. Full-band flux of
     the **output** at `smearAmount ∈ {0, 0.25, 0.5, 0.75, 1.0}`, tilt 0, `decoherence = 0`.
     **Threshold:** `flux` non-increasing as `amount` rises; `flux(1.0) ≤ 0.5 · flux(0)`; and the
     **anti-vacuity clause** — each of the four steps falls by at least `0.08 · flux(0)`, so an
     implementation where `smearAmount` does nothing below 1.0 fails.
   * **(b) Lows smear longer.** At `smearAmount = 1`, tilt 0, per-band **reduction**
     `flux(0)/flux(1)` for `[20, 200]` Hz and `[4k, 12k]` Hz.
     **Threshold:** `reduction([20,200]) / reduction([4k,12k]) ≥ 2`.
     **"Reduction" is always the ratio form** — the subtractive `1 − flux(1)/flux(0)` reads 1.10 and
     1.91 at the shipped endpoints and **fails this factor-of-2 gate on a correct build**.
   * **(c) The time constant is real.** Controls **pinned and restated in the TU**:
     `smearAmount = 1`, `decoherence = 0`, `tilt = 0`, `tauLow = 3.0`, `tauHigh = 0.25` — the quantity
     is undefined without them (at the shipped default `smearAmount = 0` the identity gate fires and
     the decay is zero; at any intermediate amount the output is a blend rather than the integrator).
     Render ≥ 25 s: gate a 100 Hz tone off after 10 s, measure the output's decay to −40 dB of its
     steady level. **Threshold:** within **±25 %** of **7.742 s**
     (`u(100 Hz) = ln(5)/ln(1000) = 0.23299`, `tau(100 Hz) = 3.0·(1/12)^0.23299 = 1.6814 s`,
     `× ln(100)`). The tolerance accommodates window spreading, not a wrong law.
   * **(d) Tilt moves the ratio in the documented direction.** Re-run (b) at `tilt = +1` and
     `tilt = −1`: the low/high reduction ratio is **strictly larger** at `+1` and **strictly smaller**
     at `−1` than at `0`.
   * **(e) The law survives the minimum geometry.** Re-run (b) with the component prepared at
     `fftSize = kMinFftSize = 512` but the **flux-measuring STFT fixed at `fftSize = 1024`, hop `256`
     on both sides** (Clarifications Q2), so the two runs are computed on the same grid and are
     numerically comparable: the factor-of-2 separation still holds. This is the criterion that
     justifies FR-011's raised minimum (D-3).
2. `TEST_CASE("SpectralSmear_SampleRateIndependence", "[spectral_smear][long]")` — SC-010.
   * Run (c)'s decay measurement at 44 100, 48 000 and 96 000 Hz with the controls **restated**
     (`smearAmount = 1`, `decoherence = 0`, `tilt = 0`, `tauLow = 3.0`, `tauHigh = 0.25`).
   * **Threshold:** the three measured −40 dB decay times agree within **±10 %** of one another, and
     each is within **±25 %** of **7.742 s** (unchanged across rates because `tau` is in seconds,
     FR-065).
   * Second arm: `getLatencySamples() == fftSize` at every rate (latency is a sample count, not a
     time).
3. `TEST_CASE("SpectralSmear_TimeConstantLaw", "[spectral_smear][long]")` — the endpoint-ordering
   behaviour, which **nothing else in the phase would fail on** (plan S13.2): an implementer who adds
   `if (tauLow_ < tauHigh_) std::swap(...)` to `rebuildPoleTables()` passes every SC-004 arm, since
   they all run at the shipped `tauLow = 3.0 > tauHigh = 0.25`. The behaviour is also load-bearing
   for SC-005 (v), which references `tauMax = max(tauLow, tauHigh)` "as configured for that pass".
   Both arms compute `R = reduction([20,200] Hz) / reduction([4k,12k] Hz)` exactly as SC-004 (b) does,
   at `smearAmount = 1`, `decoherence = 0`, `tilt = 0`:
   * (i) `tauLow = tauHigh = 1.0`: `R ∈ [0.8, 1.25]` — the separation **vanishes** and does not
     invert.
   * (ii) `tauLow = 0.25`, `tauHigh = 3.0`: `R ≤ 0.5` — strictly inverted. A silent endpoint swap
     would reproduce SC-004 (b)'s `R ≥ 2` and fail here immediately.
4. `TEST_CASE("SpectralSmear_DcNyquistSmear", "[spectral_smear][long]")` — FR-025, which otherwise
   has no criterion that can see it fail (SC-004 (a) and SC-005 (v) both perturb by O(0.2 %) when 2
   of 1025 bins are omitted, against gates with three orders of magnitude of slack).
   * Controls: `smearAmount = 1`, `decoherence = 0`, `tilt = 0`, shipped endpoints, reference
     geometry, 48 kHz, ≥ 20 s.
   * Input: a small band-limited noise floor (so no frame is degenerate) plus **a DC offset stepping
     `0 → 0.5` at `t = 5 s`** and **an alternating `±0.25` Nyquist component gated on at the same
     instant**.
   * Read both components out of the **output** by sliding-window means over `4 · hopSize` samples:
     DC as `mean(out[n])`, Nyquist as `mean(out[n] · (−1)^n)` — a demodulation, exactly the two
     quantities bins 0 and `numBins − 1` carry.
   * **Assert** each rises as a **first-order step**, not instantaneously: the time to reach
     `1 − 1/e` of the settled value is within **±25 %** of the `tau` FR-030 implies for that bin —
     `tau(bin 0) = tauLow = 3.0 s` (`u(0) = 0` by the bin-0 rule) and
     `tau(bin numBins−1) = tauHigh = 0.25 s` (`f = 24 kHz ⇒ u = 1` after the clamp).
   * The `k`-from-1 bug makes both rise times ≈ 0 and fails both assertions by orders of magnitude.

**Verify** (these are the slow ones — capture to a log on the **first** run, never re-run to grep):

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "SpectralSmear_MagnitudeMemory" 2>&1 | tee /tmp/p4_sc004.log | tail -20
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "SpectralSmear_SampleRateIndependence,SpectralSmear_TimeConstantLaw,SpectralSmear_DcNyquistSmear" 2>&1 | tee /tmp/p4_law.log | tail -20
```

**If an arm fails, the response is a fix, never a relaxed threshold.** Every number above is derived
in plan S4.1/S4.2/S13.2 from the shipped constants; a miss means the law, the bin bounds or the flux
geometry is wrong.

---

## Group G — The phase pass

### T009 — Decoherence, the two RNG streams, the FR-043 burn, the FR-046 ramp

**Files to edit:** `dsp/include/krate/dsp/processors/spectral_smear.h`,
`dsp/tests/unit/processors/spectral_smear_test.cpp`

**Write these cases FIRST:**

1. `TEST_CASE("SpectralSmear_SeedDeterminism", "[spectral_smear]")` — SC-009.
   * (a) Two instances, same seed, same `prepare()`, same control script (a 30 s render with
     `smearAmount` and `decoherence` both swept) ⇒ `compareFingerprints` passes at the shipped
     tolerances.
   * (b) **Anti-vacuity:** different seeds ⇒ the same comparison **fails**.
   * (c) Two control scripts differing **only** in the length of a mid-render park at
     `decoherence == 0` — both parks a whole number of frames, both **ending at or before absolute
     sample `S`** — produce fingerprints over the **fixed absolute window `[S + 2*fftSize, end)`**
     that pass `compareFingerprints`.
     **The window is pinned absolutely on purpose:** FR-043 burns one draw per bin per channel per
     frame regardless of the gate, so the stream position is a function of **elapsed frames**, not of
     dwell time. Stating it as "aligned to the moment the park ends" would fail a correct
     implementation and pass one that skipped the draws — exactly inverted.
   * (d) `reset()` followed by the same script reproduces (a).
2. `TEST_CASE("SpectralSmear_PreEchoAndClicks", "[spectral_smear]")` — **arm (c) only** in this TU
   (arms (a)/(b) are T011, in the spectral TU).
   * Step `smearAmount` 0→1, `decoherence` 0→1 and `tilt` −1→+1, each in a single block during a
     sustained 1 kHz tone at −6 dBFS.
   * `ClickDetection` via `ClickDetector` (`tests/test_helpers/artifact_detection.h:105`, `:130`,
     config `ClickDetectorConfig{48000, 512, 256, 5.0f}`) reports **zero clicks** on each stepped
     render.
   * The peak inter-sample delta exceeds the **reference** render's by **≤ 6 dB**, where the
     reference is the **destination setting held constant for the whole render** (spec correction
     C-10) — `decoherence = 1` set before `prepare()` and never stepped for the decoherence arm;
     `smearAmount = 1` held for the smear arm; `tilt = +1` held for the tilt arm — and the delta is
     taken over the **same absolute sample window** in both renders. Comparing against the *origin*
     setting measures crest factor, not the step, and can exceed 6 dB on a correct build: at
     `decoherence = 1` the output is a narrowband random-phase process whose RMS FR-042 restores.
   * **The `decoherence 0 → 1` arm is the one that fails without FR-046's ramp.** A per-hop-constant
     `g` jumps 1.000 → ~1.55 at a single sample boundary on the first frame after the step (the
     smoother's frame-clock coefficient is 0.34424, so one frame lands at `1 − 0.34424 = 0.656`),
     a ~55 % instantaneous amplitude step against a 1 kHz tone's peak inter-sample delta of
     `A·2π·1000/48000 = 0.131·A` — a textbook 5-sigma derivative outlier.

**Then implement**, per plan S7.1:

```
decohere(ch, d):
  if (d == 0.0f)  for k in [1, numBins_-1):  (void) rng_[ch].nextFloat();   // FR-043 BURN
  else            for k in [1, numBins_-1):  setPhase(k, getPhase(k) + d * kPi * rng_[ch].nextFloat());
```

* `nextFloat()` is **bipolar `[-1,+1]`** (`core/random.h:59-63`), so the perturbation is uniform on
  `±d·π`: `0` is the identity, `1` is full decoherence.
* **DC and Nyquist are excluded** — their phase is not free in a real spectrum
  (`atmosphere_engine.h:2325-2328`). The draw count is `numBins − 2` per channel per frame (1023 at
  the reference geometry).
* The gate is an **exact-value** gate on a settled float, never a threshold: any non-zero `d`,
  however small, takes the full path. It is reachable and stable because `OnePoleSmoother::process()`
  snaps on completion (`primitives/smoother.h:197-201`).
* The call site in `pumpChunk` becomes `decohere(ch, poisoned ? 0.0f : d);` — **called on every
  frame**, never conditionally (plan S5.3, S17 C-14). One rule governs both skips, so the draw count
  per channel per frame is `numBins − 2` whether the identity gate engaged, the poison path fired, or
  the full path ran. `if (!poisoned) decohere(ch, d);` would leave `rng_[ch]` `numBins − 2` draws
  behind a clean render **forever** after the first poisoned frame, and FR-062's no-latch design makes
  that reachable in normal operation.
* `setSeed` derives **two independent streams** (already written in T003):
  `deriveStreamSeed(seed_, kSaltDecohereL = 0x1000)` / `kSaltDecohereR = 0x2000`. Header comment: the
  derived seed is guaranteed non-zero (`core/random.h:112`), which is load-bearing because
  `Xorshift32::seed()` silently substitutes its own default for 0 (`:73-74`) and two salts hashing to
  0 would collapse the channels onto one stream.
* Confirm `writeHopToFifo`'s FR-046 ramp (already written in T006) is what arm (c) exercises; no
  change is needed there.

**Verify:** as T003, and re-run `SpectralSmear_NullAtZero`, `SpectralSmear_PartitionInvariance` and
`SpectralSmear_MagnitudePriming` — the phase pass must not disturb any of them at `decoherence = 0`.

---

## Group H — The phase-domain spectral criteria

### T010 — SC-001, SC-006 and SC-012 (a)(b)

**Files to edit:** `dsp/tests/unit/processors/spectral_smear_spectral_test.cpp` — and only that file.

1. `TEST_CASE("SpectralSmear_FlatnessVsDecoherence", "[spectral_smear][long]")` — SC-001.
   Input: 1 kHz sine at −12 dBFS, 40 s, five **separate** renders at
   `decoherence ∈ {0, 0.25, 0.5, 0.75, 1.0}`, `smearAmount = 0`, tilt 0.
   * **Arm (a) — tiled flatness, never one call on the whole buffer.**
     `calculateSpectralFlatness` (`tests/test_helpers/signal_metrics.h:326`) analyses **one**
     Hann-windowed frame of at most **4096 samples taken from the START** of whatever span it is
     handed (`:335-337` caps `fftSize` at 4096; `:349-352` windows `signal[0..fftSize)`), so handing
     it a 20 s span measures 85 ms of it and makes the render length, the span and the warm-up
     discard inoperative — and one 85 ms estimate of a per-frame-randomised signal is far too noisy
     to carry a monotonicity gate. The metric is the **mean over N ≥ 200 non-overlapping
     4096-sample windows tiling the last 20 s** (234 windows at 48 kHz), after discarding
     `2 * fftSize`. **Threshold:** the five values are non-decreasing and each of the four steps shows
     a relative increase **≥ 10 %**.
   * **Arm (b) — the line becomes a band, against an analytic prediction.** Do **not** use the
     floor-anchored ratio `flatness(1.0)/flatness(0)`: `flatness(0)` is the leakage/round-off floor of
     a windowed pure tone and differs between MSVC, GCC and AppleClang (`-ffast-math`), so a threshold
     over it is a threshold over a toolchain artefact. Measure the **out-of-mainlobe energy fraction**
     `ρ(d) = 1 − E(1 kHz ± 2·sampleRate/fftSize) / E(all bins above DC)`, averaging the power spectra
     of ≥ 100 non-overlapping 8192-sample Hann frames (5.86 Hz/bin) over the same steady region; the
     band is the Hann mainlobe half-width, ±46.9 Hz at the reference geometry.
     Per-frame phases drawn uniformly on `±dπ` leave a coherent carrier of `sin(dπ)/(dπ)`, so the
     prediction is `ρ(d) = 1 − (sin(dπ)/(dπ))²` = **`{0, 0.19, 0.59, 0.91, 1.00}`**.
     **Threshold:** `ρ` non-decreasing; `ρ(0) ≤ 0.02`; `ρ(1.0) ≥ 0.80`; each of the four steps
     ≥ 0.10 absolute; each measured `ρ(d)` within **±0.10** of the analytic value. Every quantity is
     an energy **fraction**, so it is invariant to the make-up gain and the drive level.
   * **Arm (c) — stereo.** Inter-channel correlation of the output falls monotonically as
     `decoherence` rises and reaches **≤ 0.3** at `decoherence = 1` — the observable consequence of
     FR-044's two independent streams.
2. `TEST_CASE("SpectralSmear_CoherenceMakeup", "[spectral_smear]")` — SC-006. `smearAmount = 0`;
   30 s of white noise at each of the five knot values; measure output RMS / input RMS **with the
   make-up divided out** through the component's own public `SpectralSmear::coherenceMakeup(d)`
   (which exists precisely so this criterion is executable — nothing else exposes `g(d)` and there is
   no setter that disables it).
   * **(a) Knots — the informative arm.** Each measured **raw** ratio at knot `i` is within **2 %** of
     `1 / SpectralSmear::kCoherenceMakeup[i]` **as shipped at the time the test runs**.
   * **(b) Interpolant.** With the make-up **applied**, output RMS is within **±0.5 dB** of input RMS
     at **ten intermediate** values of `decoherence` (the knots themselves are covered by (a)) — here
     the cubic-Hermite interpolant, not the table, is under test.
   * **(c) Transfer check.** Report each measured raw knot against `AetherReverb`'s shipped
     `kCoherenceMakeup` (`effects/aether_reverb.h:2774-2776`). Agreement within **2 %** confirms the
     `sqrt(Σw⁴)/Σw²` transfer argument (`:640-643`) and the transcribed values ship with a citation;
     **otherwise the measured table ships and the divergence is documented in the header with its
     cause** (FR-042) — and T004's `SpectralSmear_CoherenceMakeupInterpolant` literals are updated in
     the same edit.
   * Splitting (a) from (b) is what keeps the criterion non-circular: if the shipped knots were the
     reciprocals of values measured in this same test, "make-up applied ⇒ RMS restored" would be a
     tautology. At settled `d` the FR-046 ramp is from `g` to `g`, i.e. constant, so it does not
     perturb this measurement.
3. `TEST_CASE("SpectralSmear_PreEchoAndClicks", "[spectral_smear][long]")` — **arms (a) and (b)**
   (arm (c) lives in the main TU, T009; give this case a distinct name, e.g.
   `SpectralSmear_PreEcho`, so the two do not collide).
   * (a) Input: 2 s of digital silence, then a 1 kHz tone burst at −6 dBFS. Align by
     `getLatencySamples()`. Measure RMS in the window `[onset − 2·fftSize, onset − fftSize)` —
     strictly more than one analysis window before the onset — for every corner of
     `{smearAmount, decoherence} ∈ {0,1}²` and both tilt extremes. **Threshold: ≤ −90 dBFS relative
     to the burst's peak, at every setting.** The window `[onset − fftSize, onset)` is *expected* to
     carry energy — inherent STFT window spread, not an artifact — and is **reported via `WARN`, not
     asserted**.
   * (b) **Anti-vacuity:** the same render's decay after burst-off is **longer** at `smearAmount = 1`
     than at `0`, proving (a) does not pass merely because the stage does nothing.

**Verify:** build, then run each case individually, capturing to a log on the first run.

---

## Group I — The poison path

### T011 — `handlePoison`, the non-finite TU, and SC-018 arm (b)

**Files to edit:** `dsp/include/krate/dsp/processors/spectral_smear.h`,
`dsp/tests/unit/processors/spectral_smear_nonfinite_test.cpp`

**This TU is the only place in the phase where a non-finite value may be named.** Open it with a
banner transcribed from `dsp/tests/unit/systems/resonance_drift_network_nonfinite_test.cpp:36-66`
stating: values are built from bit patterns through a volatile sink, never
`std::numeric_limits<float>::quiet_NaN()` / `infinity()`; finiteness is read with
`Krate::DSP::detail::isFinite`; no bit-exact float goldens. Copy the helpers verbatim:
`makeNonFinite(std::uint32_t bits)` (`:143-155`) and the patterns
`{0x7FC00000 quiet NaN, 0x7F800000 +Inf, 0xFF800000 −Inf}` (`:130-134`), plus
`makeNonFiniteDouble` (`:158-165`) for the `prepare()` `sampleRate` arm.

**Write these cases FIRST:**

1. `TEST_CASE("SpectralSmear_NonFiniteSetters", "[spectral_smear]")` — FR-009's substitution rule
   (moved here from `SpectralSmear_ControlClamps`, plan S17 C-16).
   * For each of the three patterns, each setter (`setSmearAmount`, `setDecoherence`, `setSmearTilt`,
     `setSmearTimeLow`, `setSmearTimeHigh`) called with that value leaves the target read at the
     parameter's **documented default** (`0.0f`, `0.0f`, `0.0f`, `3.0f`, `0.25f`) — FR-009
     **substitutes**, it does not reject (deliberately the opposite of `ResonanceDriftNetwork`'s
     FR-008).
   * A non-finite write **after** a legal write also lands on the default, not on the previous value
     — that is what distinguishes substitution from rejection and is the arm a Phase-3 reader would
     get backwards.
   * `prepare(makeNonFiniteDouble(pattern), {.fftSize = 2048, .enabled = true})`:
     `isPrepared() == false` and all four geometry reads are `0`/`0.0` (plan S2 step 1).
2. `TEST_CASE("SpectralSmear_NonFinite", "[spectral_smear]")` — SC-016. Reference geometry, 48 kHz,
   `smearAmount = 1`; inject the pattern into **one channel for one 512-sample block**; render well
   past it; `E` = the index of the **last injected sample**.
   * (i) **No output sample after the injecting block is non-finite** (`detail::isFinite`).
   * (ii) `getPoisonEngagements() >= 1`.
   * (iii) The component **recovers on its own, with no `reset()` call**: output RMS over the
     **absolute** window `[E + 2·fftSize, E + 3·fftSize)` is within **0.5 dB** of an un-injected
     reference render's RMS over the **same absolute window**. Reachable in one frame only because
     FR-062's poison clear **re-arms the priming flag**; a zero-initialised recovery misses this by
     12–26 dB. Any window overlapping the silent gap would fail however correct the implementation,
     which is why the window sits where it does (plan S8).
   * (iv) **The silent gap is bounded in shape as well as length.** The observable is **not** a frame
     count — no public API exposes per-frame state — but the **maximal run of consecutive near-silent
     output samples** after the first injected sample, with the silence floor stated **relative to the
     un-injected reference's local RMS** (−80 dB of it). **Assert `runLength ≤ 2 * hopSize = 1024`.**
     The arithmetic, which goes in a comment beside the assertion: a `L = 512`-sample injection is
     overlapped by `floor((L − 1 + fftSize − 1)/hopSize) + 1 = floor(2558/512) + 1 = **5**` analysis
     frames (the exact maximum over alignments; window tapering does not reduce it, because
     `NaN * 0.0f` is `NaN`), and each output sample is covered by `numOverlaps = 4` frames, so a
     silent **run** is `(5 − 4 + 1) · hopSize = 2 · hopSize`. The two quantities differ by a factor of
     2.5 and the wrong one is a plausible transcription.
3. `TEST_CASE("SpectralSmear_MagnitudePrimingAfterPoison", "[spectral_smear]")` — SC-018 **arm (b)**,
   relocated here by spec correction C-17.
   * Prepare two instances identically, one at `smearAmount = 1` and one at `smearAmount = 0`, inject
     the **same** single non-finite sample into both at the same absolute index (FR-062's accumulator
     test is independent of `amount`, so both poison and both synthesise the same silent frames), then
     feed both the same 1 kHz burst.
   * Locate `G`, the last index of the maximal silent run after the injection in the **reference**
     (`smearAmount = 0`) render, and compare `[G + 1, G + 1 + hopSize)` — the priming frame's
     exclusive contribution — with the same **`rms(actual − reference)/rms(reference) ≤ −60 dB`** gate
     as T007's arm (a).
   * This is the direct assertion that FR-062 (a) **re-arms the priming flag** rather than merely
     zeroing the memory.

**Then implement** `handlePoison(std::size_t ch)` exactly per plan S8:

```
std::fill(magState_[ch].begin(), magState_[ch].end(), 0.0f);   // (a) clear
primeNext_[ch] = true;                                         // (a) RE-ARM the priming flag
for k in [0, numBins_): setMagnitude(k, 0.0f); setPhase(k, 0.0f);   // (b) synthesise silence
++poisonEngagements_;                                          // (c)
return true;                                                   // (d) CONTINUE - NO LATCH
```

* **Both magnitude and phase are zeroed, and that is not belt-and-braces** (plan S17 C-3):
  `computePolarBulk` produces `phase = atan2(imag, real)`, NaN for a NaN input, and
  `reconstructCartesianBulk` then computes `mag · cos(phase)` — `0.0f · NaN` is NaN. Zeroing
  magnitudes alone would still synthesise NaN.
* The caller does **not** skip the phase pass on this path — `decohere(ch, 0.0f)` still runs
  (already wired in T009), so the burn branch advances the stream by the same `numBins − 2` draws.
  The zeroed spectrum stays zeroed: the burn branch performs no `setPhase`.
* **It does not latch, and the header must say why this deliberately deviates from
  `AtmosphereEngine`** (`atmosphere_engine.h:2244-2260`, "no auto-resume: reset() … is the one
  documented recovery"): this component sits on the **global** bus post-voice-sum, where latching
  converts one poisoned frame into a dead instrument only `reset()` can revive. The magnitude memory
  is the only state that can carry poison forward, and (a) clears **and re-primes** it, so recovery is
  automatic within one frame after the poisoned one.
* One `detail::isFinite` call per frame per channel on the accumulator, **never per bin**. The
  accumulator sums the **analysed** magnitudes, which is sound because that is the only entry point
  for poison.

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "SpectralSmear_NonFinite*,SpectralSmear_MagnitudePrimingAfterPoison" 2>&1 | tail -10
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "SpectralSmear_*" 2>&1 | tail -5
```

The whole `SpectralSmear_*` set is green; zero warnings.

---

## Group J — The soak

### T012 — SC-005, the 30-minute boundedness soak

**Files to edit:** `dsp/tests/unit/processors/spectral_smear_spectral_test.cpp` — only.

`TEST_CASE("SpectralSmear_BoundednessSoak", "[spectral_smear][long]")`

**Configuration is fixed, not accelerated** (Clarifications Q7): 48 kHz, reference geometry
(`fftSize = 2048`, hop 512, `WindowType::Hann`), **a full 30 minutes of real audio**. At FR-060's own
~25 000 ns/block projection this is ≈ 168 750 blocks ≈ 4–5 s of wall clock, so there is no motive to
accelerate — doing so would change the bin spacing and the tau-in-frames ratio the soak exists to
measure.

* Input: pink noise at −6 dBFS with **60 s gaps of digital silence** (≥ 6 · `kMaxSmearSeconds` — the
  sizing rule clause (v) depends on).
* Controls driven to their extremes on a slow schedule: `smearAmount` and `decoherence` sweeping
  0↔1, tilt sweeping ±1, both time endpoints at their extremes on alternate passes.
* The schedule additionally **parks the controls at a fixed reference setting
  (`smearAmount = 1`, `decoherence = 0.5`, `tilt = 0`, default endpoints) for one full 60 s
  pink-noise-active window in each 5-minute pass** — **six reference windows** in all.

**Thresholds:**

* (i) every output sample finite (`detail::isFinite`);
* (ii) peak `≤ kOutputClamp` (4.0);
* (iii) `getClampEngagements() == 0` at −6 dBFS — the clamp is a backstop, **not** a working part
  here (its positive arm is T006's `SpectralSmear_OutputClamp`);
* (iv) the RMS of the **six reference windows** drifts by **≤ 0.5 dB** across the render. Measure
  **only over pink-noise-active audio** (a window landing in a silent gap reads −inf dBFS) and **only
  at equal control settings** — SC-006 allows the make-up ±0.5 dB of its own, so two windows at
  different `decoherence` may legitimately differ by ~1 dB and comparing them would measure the
  make-up curve rather than creep;
* (v) during each silent gap the output falls by **at least 40 dB relative to its pre-gap RMS within
  `5 · tauMax` seconds**, where `tauMax = max(tauLow, tauHigh)` **as configured for that pass**
  (`tauLow < tauHigh` is legal and unclamped, so `tauLow` alone is the wrong constant), and is
  monotonically non-increasing through the remainder of the gap to within a **1 dB ripple**
  allowance. At the extreme pass `tauMax = 10 s`, so the clause needs 50 s of the 60 s gap
  (`−40 dB ⇒ t = tau·ln(100) = 4.61·tau`).
  **Relative, never an absolute dBFS floor:** −80 dBFS absolute from a −6 dBFS steady level needs
  `8.5·tau = 85 s` at the extreme pass, longer than the gap — a correct implementation would fail it.

**Isolation:** although this case asserts levels rather than wall clock, it is a multi-minute render
like every other `[long]` case and **runs alone**, nothing else executing. The `[long]` tag already
keeps it out of the per-push lane; it runs in the nightly `[long]` lane.

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "SpectralSmear_BoundednessSoak" 2>&1 | tee /tmp/p4_soak.log | tail -20
```

---

## Group K — CPU

### T013 — SC-013: four configurations, four checked-in baselines, measured alone

**Files to edit:** `dsp/tests/unit/processors/spectral_smear_perf_test.cpp` — only.

`TEST_CASE("SpectralSmear_CpuBudget", "[spectral_smear][.perf]")`

**Two numbers, and they must not be confused.** The roadmap budget is 0.5 % of one core at 48 kHz =
`kReferenceNs = 10 666 667 × 0.005 = 53 333 ns` per 512-sample block. SC-013's own
`static_assert(kBaseline * kRegressionFactor <= kReferenceNs)` with `kRegressionFactor = 1.5`
(`vorago_p1_perf_test.cpp:108-110`) caps any checked-in baseline at
**`53 333 / 1.5 = 35 555 ns/block = 0.333 % of one core`**. **35 555 ns is the binding figure for
this phase and the figure each compliance row is measured against.** A measurement in
`[35 556, 53 333] ns` is **not shippable** even though it satisfies the roadmap sentence: no baseline
can be encoded for it.

**TU shape**, transcribed from `dsp/tests/unit/processors/vorago_p1_perf_test.cpp:76-125`
(read it before writing): `kBlockSize = 512`, `kBlockBudgetNs = 512/48000 · 1e9 = 10 666 667`,
`kReferenceNs = kBlockBudgetNs * 0.005`, `bestTrialNs` = **best-of-25 trials × 500 blocks after 400
warm-up blocks**. Each configuration gets its own `constexpr double kBaseline…` with a provenance
comment in the Phase-1 form (`:84-97`): machine, five consecutive otherwise-idle runs with their
figures and spread, the measured value as a percentage of one core, and a repeat of the 35 555 ns
effective ceiling. Each carries **both**
`static_assert(kBaseline * 1.5 <= kReferenceNs)` and the anti-no-op floor
`static_assert(kBaseline >= kReferenceNs / 50.0)` (= 1 066 ns); the runtime gate is
`REQUIRE(measured <= kBaseline * 1.5)`.

Four configurations:

* **(a) The transparent cost.** `PrepareConfig{.fftSize = kDefaultFftSize, .enabled = true}` with
  default **control** values (`smearAmount = 0`, `decoherence = 0`, `tilt = 0`) — both identity gates
  engaged, the full stereo STFT round trip still paid. **Spell the configuration out in the TU**
  (spec correction C-13): `PrepareConfig::enabled` defaults to `false`, so a literal reading of
  "defaults" prepares a true bypass that measures tens of ns and collides with the anti-no-op floor.
* **(b) The worst case.** Reference geometry, `smearAmount = 1`, `decoherence = 1`, tilt written
  every block. The time endpoints are **not** swept here — tilt is the modulation target; the
  endpoints get their own configuration.
* **(c) The highest frame-rate geometry.** `fftSize = 512`, same worst-case controls.
* **(d) The rebuild.** `fftSize = kMaxFftSize = 4096`, worst-case controls, **plus one
  `setSmearTimeLow()` call per block**, so the deferred pole-table rebuild fires once per block at the
  geometry where it is most expensive (`numBins = 2049` ⇒ ~14 300 transcendentals). This is the one
  operation in the component with a large, geometry-scaled **synchronous** cost that FR-006 puts on
  the audio thread; without (d) it is absent from every criterion.

**The projection this is measured against** (FR-060): Atmosphere's structurally identical stereo blur
stage (STFT↔OverlapAdd, fftSize 1024, 75 %, full per-bin phase randomisation) costs ~23 000 ns/block
(`dsp/tests/unit/systems/atmosphere_engine_perf_test.cpp:376`), corroborated by its own `(b) − (a)`
baseline difference of 25 510 ns (`:241-242`). This component adds, per bin, one FMA for the
integrator, one FMA for the blend, one ordered compare and one shared per-frame table lerp ⇒
**~26 667–32 000 ns/block, 10–33 % under the ceiling**.

**If a measurement exceeds 35 555 ns/block, reduce cost.** The pre-approved levers, in order:
(i) the two identity-gate skips (already specified); (ii) fuse the integrator and the blend into one
pass over the bins reading the shared per-frame pole table (already the S6 shape); (iii) raise the
default `fftSize` (cheaper per block by the `log N` term); for (d) specifically, a coarser rebuild
cadence. **If none suffices, STOP AND SURFACE to the user with the measured table.** Never raise a
baseline, never relax the budget, never shrink the workload.

**Measurement discipline — this is the one task with a hard isolation rule.** Run it **alone**:
nothing else executing, no build, no other suite, no clang-tidy, no parallel agent. A 6.4 %
run-to-run spread with ~14 % session drift under sustained benchmarking is the documented reality
(`noise_organism_perf_test.cpp:228-236`); **a number measured once is not evidence** — take five
consecutive idle runs and record the spread in the provenance comment. If a figure flips verdicts
between runs, it is measuring the machine, not the code.

**Verify (alone):**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests
node tools/run-cpu-tests.js dsp_processors_tests
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "SpectralSmear_CpuBudget" 2>&1 | tee /tmp/p4_perf.log | tail -40
```

---

## Group L — Integration gates

### T014 [P] — Registration-completeness audit

**Files to edit:** none (read-only verification; fix `dsp/tests/CMakeLists.txt` or
`dsp/lint_all_headers.cpp` if anything is missing).

Re-verify, by reading the files rather than trusting T001:

1. All four TUs appear **by name** in `add_executable(dsp_processors_tests ...)`. The list is
   enumerated, not globbed — an unregistered TU compiles into nothing and its cases silently never
   run.
2. **`spectral_smear_nonfinite_test.cpp` and only it** appears in the
   `-fno-fast-math -fno-finite-math-only` `set_source_files_properties` block. Grep the other three
   names against that block and confirm zero hits.
3. `#include <krate/dsp/processors/spectral_smear.h>` is present in `dsp/lint_all_headers.cpp`, in
   alphabetical position between `spectral_morph_filter.h` and `spectral_tilt.h`.
4. Every `TEST_CASE` this phase adds is present in the built binary: run
   `build/windows-x64-release/bin/Release/dsp_processors_tests.exe --list-tests 2>&1 | grep -c SpectralSmear`
   and confirm **every** name in the list below is discoverable (the list is the authority; do not
   assert a literal count, which a legitimate split or rename would invalidate):
   `SpectralSmear_Geometry`, `SpectralSmear_Latency`, `SpectralSmear_AllocatedBytes`,
   `SpectralSmear_ControlClamps`, `SpectralSmear_PoleTableBounds`,
   `SpectralSmear_CoherenceMakeupInterpolant`, `SpectralSmear_NullAtZero`,
   `SpectralSmear_PartitionInvariance`, `SpectralSmear_ControlCadence`,
   `SpectralSmear_NoAllocation`, `SpectralSmear_RenderPathBoundaries`, `SpectralSmear_OutputClamp`,
   `SpectralSmear_MagnitudePriming`, `SpectralSmear_SeedDeterminism`,
   `SpectralSmear_PreEchoAndClicks` (main TU, arm (c)) — plus, in the spectral TU:
   `SpectralSmear_FluxHelperSanity`, `SpectralSmear_MagnitudeMemory`,
   `SpectralSmear_SampleRateIndependence`, `SpectralSmear_TimeConstantLaw`,
   `SpectralSmear_DcNyquistSmear`, `SpectralSmear_FlatnessVsDecoherence`,
   `SpectralSmear_CoherenceMakeup`, `SpectralSmear_PreEcho`, `SpectralSmear_BoundednessSoak` — plus,
   in the non-finite TU: `SpectralSmear_NonFiniteSetters`, `SpectralSmear_NonFinite`,
   `SpectralSmear_MagnitudePrimingAfterPoison` — plus, in the perf TU:
   `SpectralSmear_CpuBudget` (visible only with `--list-tests`, since `[.perf]` is hidden by default).
   The audit is that **every** case named in a task body is discoverable, and that no task's cases
   silently vanished because its TU was never registered.

---

### T015 — Full-suite run and the SC-014 diff gate

**Files to edit:** none.

1. Build and run the whole FR-072 regression set:

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target \
    dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests \
    dsp_effects_tests seraphis_tests innexus_tests
for t in dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests \
         dsp_effects_tests seraphis_tests innexus_tests; do
  build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3
done
```

Every suite reports "All tests passed". **No failure may be dismissed as pre-existing** — if one
appears, halt and fix it before the phase is called done.

2. **SC-014 diff gate:**

```bash
git diff --stat -- dsp/include/krate/dsp/systems/atmosphere_engine.h \
                   dsp/include/krate/dsp/effects/aether_reverb.h
```

must be **empty** — both shipped engines byte-unchanged (FR-070, D-5: neither is a drop-in; the
roadmap's own conditional at line 253 is discharged on the "otherwise" branch).

3. **FR-071 diff scope:**

```bash
git diff --stat
```

must show changes **only** in: `dsp/include/krate/dsp/processors/spectral_smear.h`, the four new
TUs, `tests/test_helpers/spectral_flux.h`, `dsp/tests/CMakeLists.txt`,
`dsp/lint_all_headers.cpp`, `specs/vorago-phase4-spectral-smear/**` and `specs/Vorago-roadmap.md`
(the last two added to FR-071's list by T002 item 1). Anything else is a defect, not a convenience.

4. Zero compiler warnings across the whole build. Capture the build output to a log on the first run
   and read the log; do not re-run to grep it.

---

### T016 — Portability, lints and clang-tidy

**Files to edit:** none (fix the header/TUs if a gate fails).

```bash
node tools/check-portability.js
node tools/lint-layers.js
node tools/lint-odr.js
node tools/lint-nonfinite-symbols.js
node tools/lint-float-bit-goldens.js
node tools/lint-simd-aligned-loadstore.js
node tools/lint-allocation-operator-overrides.js
```

All seven must pass. What each is actually guarding here:

* **check-portability** — MSVC accepts what GCC/AppleClang reject. A green Windows build proves
  nothing about the Linux/macOS legs. **If this machine's WSL is still broken (it was in Phase 3),
  syntax-check the new TUs and the header against libstdc++ with MSYS2 `g++ 14.2` instead and say so
  explicitly in the compliance notes** — do not record a skipped gate as a pass.
* **lint-layers** — the header includes Layers 0–1 only. (Note for the reader, not a gate: a Layer 3
  or 4 engine *consuming* this Layer 2 header would be legal and routine — `lint-layers.js:74` fails
  only `layerIndex(to) > layerIndex(from)`. The direction FR-070 rules out is ruled out on behaviour,
  RNG contract and lifecycle grounds, not on layer.)
* **lint-odr** — `SpectralSmear` is a fresh name (sweep re-run at spec time: 0 hits in `dsp/`,
  `plugins/`, `tools/`). Near-name symbols that exist and are **not** shadowed: `SpectralGate` and its
  members `setSmearing`/`getSmearing` (a *frequency-axis* box average of gate gains — different axis,
  different object), `SpectralTilt`, `SpectralDistortion`, `SpectralMorphFilter`, `SpectralBuffer`,
  `SpectralTransientDetector`, and `AetherReverb`'s private `kSmearSaltL/R`.
* **lint-nonfinite-symbols** — no `std::isnan`/`std::isinf`/`std::isfinite` in the header or any of
  the four TUs.
* **lint-float-bit-goldens** — the only exact float comparisons in the phase are "these bytes were
  not written" (the bypass render, the warm-up zeros) and "this stored value was not touched" (setter
  reads). SC-009/SC-011 use `render_fingerprint.h` tolerances; SC-018 uses a relative-RMS gate.
* **lint-allocation-operator-overrides** — no TU in this phase includes
  `allocation_operator_overrides.h`; `dsp_processors_tests`' single owner stays
  `brownian_drift_test.cpp:28`.

Then clang-tidy, captured to a log on the **first** run:

```bash
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja 2>&1 | tee /tmp/p4_tidy.log
```

Fix **all** warnings it reports on the new files — not just "new code" ones that happen to be
convenient. If the ninja preset needs regenerating, the `clang-tidy-setup` skill carries the one-time
setup.

---

## Dependency summary

```
Group A   T001 [P] (CMake + 4 TU stubs)      T002 [P] (spec.md corrections C-1,5,6,7,10,11,12,13,17)
             |
Group B   T003 (header skeleton + lifecycle/geometry/footprint/clamp cases + lint_all_headers)
             |
Group C   T004 (pole tables, tilt law, deferred rebuild)   T005 [P] (spectral_flux.h + sanity case)
             |
Group D   T006 (render path: chunking, drain loop, FIFO, warm-up counter, make-up ramp, clamp)
             |
Group E   T007 (magnitude pass: integrator, blend, priming, denormal flush, poison accumulator)
             |
Group F   T008 (SC-004 a-e, SC-010, TimeConstantLaw, DcNyquistSmear  -- spectral TU only)
             |
Group G   T009 (phase pass: decoherence, two streams, FR-043 burn; SC-009, SC-012 (c))
             |
Group H   T010 (SC-001, SC-006, SC-012 (a)(b)  -- spectral TU only)
             |
Group I   T011 (poison path; SC-016, SC-018 (b), FR-009 non-finite setters -- nonfinite TU)
             |
Group J   T012 (SC-005 soak, [long], alone)
             |
Group K   T013 (SC-013 perf, [.perf], ALONE - five idle runs, record the spread)
             |
Group L   T014 [P] (registration audit)  ->  T015 (full suite + SC-014/FR-071 diff gates)
                                         ->  T016 (portability + 6 lints + clang-tidy)
```

**Two items stay measurement-contingent to the end, exactly as the spec's Open Questions say —
neither may be resolved by quietly picking a number:**

1. **The coherence-make-up knots** (FR-042, SC-006, T010). `AetherReverb`'s shipped table is
   transcribed as the *expectation*; if any measured knot is off by more than **2 %**, our measured
   table ships, the header documents the divergence and its cause, and T004's interpolant literals
   are updated in the same edit.
2. **The CPU baselines** (FR-060, SC-013, T013). Projected 26 667–32 000 ns/block against the
   **35 555 ns effective ceiling**. Over ⇒ the ordered levers, then **stop and surface**. Never a
   relaxed budget, never a raised baseline, never a shrunk workload.
