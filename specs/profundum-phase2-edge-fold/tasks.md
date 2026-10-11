# Tasks: Profundum Phase 2 — Edge Excitation & Harmonic Fold

**Spec:** `specs/profundum-phase2-edge-fold/spec.md` (Clarified 2026-10-10) · **Plan:** `specs/profundum-phase2-edge-fold/plan.md`
(S0–S12, review round 2) · **Model:** `specs/profundum-phase2-edge-fold/edge-fold-model.js`, `halfband-design.js`, `evidence/*.js`,
`bench/*.cpp` · **Phase base:** commit `47e43918`
**Status:** Task list (2026-10-10). The build stage has not started.

## Read this first (every executor)

### Precondition: ratification of plan S1 (BLOCKING)

Plan S1 lists eleven items (C-1 … C-11) that the plan says must be ratified **before** tasks run. On 2026-10-10 (later the same day) ALL ELEVEN WERE RATIFIED by the user — see spec Clarifications "Session 2026-10-10 — plan S1 ratification" and plan S1's RATIFIED line; C-4 as per-channel. The precondition is met. Original note: on 2026-10-10 none of them
appears in the spec's Clarifications log (`spec.md` "## Clarifications", the last section). The tasks below encode the plan's
**recommended** resolution of each. The main loop must ratify them with the user and write each ruling into the spec's
Clarifications log **before T003 baselines the bars**. Every test that relies on a C-item cites it in a comment
(`// plan S1 C-2`). If the user rules differently, change the listed tasks before you execute them; never relax a bar on
your own.

| Item | What the tasks assume | Tasks affected if ruled otherwise |
|---|---|---|
| C-1 ⚠ | (a) new Layer 1 `PolyphaseHalfbandOversampler` replaces the shared `Oversampler`; FR-041/FR-048 amended; no heap anywhere. (b-i) the allpass section is written inline (no `one_pole_allpass.h` include) | T002, T006, T007, T008, T009, T011, T012 |
| C-2 ⚠ | SC-012(b): ±2° at asymmetry 0 on both inputs and at every setting on `kSineAnchor`; `kSawAnchor` at asymmetry > 0 \|∠T₁\| ≤ **20°**. SC-013(b): inter-channel h1 phase ≤ 2° at asymmetry 0, ≤ **15°** at asymmetry 1; the 0.5 dB power clause unchanged. New recorded SC-022(e) | T014, T015 |
| C-3 ⚠ | SC-024(b): periodicity residual kept **without** its teeth claim; new (b′): `detectorLevel()` in-cycle ripple (max − min)/mean ≤ **1e-4**, with an `x/env(x)` 1 ms-release `double` reference that must show ripple ≥ **0.1** | T010 |
| C-4 | **RATIFIED 2026-10-10: per-channel detectors** (identical constants, instant attack `kDetectorAttackMs = 0`, detector input \|x_ch\| per channel). SC-013(c)'s detector clause is "L/R output ratio within ±0.1 dB of the input ratio at amounts {47.5, 82.5, 100}, both levels, through the step". T008 and T010 below are written to this ruling. | T008, T010 |
| C-5, C-6, C-7, C-8 | Refinements: split bypass (C-5); SC input choices (C-6); drive law −12…+12 dB (C-7); Fold level on non-sine inputs recorded as SC-022(f) (C-8) | T011, T012, T014, T015 |
| C-9 ⚠ | SC-002(c) blend clause measured between non-zero mixes: residual of y(0.55) − ½(y(0.1) + y(1)) ≤ **−60 dB** re output power, at Edge 0 and Edge 1 separately; the as-written y(0.5) − ½(y(0) + y(1)) residual is printed as SC-022(g) | T014 |
| C-10 ⚠ | SC-013(c) first clause replaced by (c-i) Fold at 100 adds ≤ **2°** to the inter-channel h1 difference of the **mix-0** panned core; (c-ii) at max excitation, asymmetry 0 and 1, mid grid and saw, E1 and C2, h1 power of (L+R)/2 within **0.5 dB** of ((\|h1L\| + \|h1R\|)/2)²; (c-iii) phase and L/R imbalance printed as SC-022(h) | T015 |
| C-11 | `kFoldIndexMax = 3.0` (2.7 fails SC-006's mid-grid centroid clause) | T008, T010 |

### Deviations from the plan made while cutting these tasks (surface them; do not silently revert)

1. **CMake registration is the first code task (T006), not the last.** Every later task is test-first and must build its
   new TU; an unregistered TU cannot fail, so test-first would be vacuous. This is the Phase 1 precedent
   (`specs/profundum-phase1-harmonic-core/tasks.md`, "Group 1 — CMake registration"). The final group re-audits the
   registration as a single task (T017).
2. **`dsp/CMakeLists.txt` header lists gain the two new headers.** Plan S10.1 says "No production CMake changes", but
   Phase 1 registered its headers in `KRATE_DSP_PRIMITIVES/PROCESSORS/SYSTEMS_HEADERS` (`dsp/CMakeLists.txt:156–157,
   183–184`). T006 follows that precedent (two list entries, no target change).
3. **`halfband-design.js` prints coefficients to 4 decimals** (`halfband-design.js:36`, `v.toFixed(4)`), not the
   "9-digit values" plan S3.2 says it prints, and it does not print the inner `design(4, 0.25)` row. T007 therefore uses
   the 9-significant-digit values already written in plan S3.1 and lets the analytic response test decide. Its printed
   stopband for `N=8 tbw=0.05` reads `−100.6 dB` at 0.55·fs **including the script's +6.0 dB passband gain**; re the
   passband that is the plan's −106.6 dB, so there is no contradiction, but the 0.6 dB headroom against an absolute
   −100 dB reading is why T007 normalises every response to its own DC/passband gain.
4. **`applyNoteOn` starts at `profundum_core.h:378`**, not `:386` as plan S0/S5.6 cite (the Reset branch body is
   `:386–411`). Use the signature line when brace-matching (T002).
5. **`AllocationScope::getAllocationCount()` reads 0 until the scope is destroyed** (`allocation_detector.h:111–123`: the
   count is stored in the destructor). Inside a scope use `AllocationDetector::instance().startTracking()` /
   `stopTracking()` (`:53`, `:59`), as Phase 1 does (`dsp/tests/unit/systems/profundum_core_test.cpp:1960`).
6. **Chain-arm TEST_CASE names get a suffix.** SC-010/011/012(d) have a Fold arm in `dsp_processors_tests` and a core-input
   arm in `dsp_systems_tests`. The systems arms are named `…CoreInput` / `…OnExcitationOutput` so a name never means two
   things.

### File map

| File | Kind | Layer / target | Created by / edited by |
|---|---|---|---|
| `dsp/include/krate/dsp/primitives/polyphase_halfband_oversampler.h` | new header | L1 (includes `core/` only) | T006 stub, T007 |
| `dsp/include/krate/dsp/processors/harmonic_fold.h` | new header | L2 (includes L0 + L1 only) | T006 stub, T008–T010 |
| `dsp/include/krate/dsp/systems/profundum_core.h` | **existing** Profundum header, append-only declarations | L3 | T011, T012 |
| `dsp/tests/unit/primitives/polyphase_halfband_oversampler_test.cpp` | new TU | `dsp_primitives_tests` | T006 stub, T007 |
| `dsp/tests/unit/processors/harmonic_fold_test_helpers.h` | new test header, **no `systems/` include** | — | T008 |
| `dsp/tests/unit/processors/harmonic_fold_test.cpp` | new TU | `dsp_processors_tests` | T006 stub, T008–T010 |
| `dsp/tests/unit/processors/harmonic_fold_nonfinite_test.cpp` | new TU, **`-fno-fast-math`** | `dsp_processors_tests` | T006 stub, T008 |
| `dsp/tests/unit/systems/profundum_phase1_core_ref.h` | **generated** test header | — | T004 |
| `dsp/tests/unit/systems/profundum_excitation_test_helpers.h` | new test header | — | T011 |
| `dsp/tests/unit/systems/profundum_core_excitation_test.cpp` | new TU | `dsp_systems_tests` | T006 stub, T011, T012 |
| `dsp/tests/unit/systems/profundum_core_excitation_spectral_test.cpp` | new TU | `dsp_systems_tests` | T006 stub, T014 |
| `dsp/tests/unit/systems/profundum_edge_fold_chain_test.cpp` | new TU | `dsp_systems_tests` | T006 stub, T015 |
| `dsp/tests/unit/systems/profundum_core_excitation_nonfinite_test.cpp` | new TU, **`-fno-fast-math`** | `dsp_systems_tests` | T006 stub, T011 |
| `dsp/tests/unit/systems/profundum_edge_fold_perf_test.cpp` | new TU, `[.perf]`, **not** `-fno-fast-math` | `dsp_systems_tests` | T006 stub, T013 |
| `specs/profundum-phase2-edge-fold/extract-phase1-ref.js` | new Node script | SC-004(a) | T004 |
| `specs/profundum-phase2-edge-fold/check-footprint.js` | new Node script | SC-021, SC-026 grep, S8.1 grep | T002 |
| `specs/profundum-phase2-edge-fold/check-bars.js` | new Node script | SC-027, SC-023 presence | T003 |

Untouchable (zero diff lines, FR-048 / SC-021): `tanh_adaa.h`, `hard_clip_adaa.h`, `oversampler.h`, `biquad.h`,
`wavefolder.h`, `wavefold_math.h`, `wavefolder_processor.h`, `sigmoid.h`, `fast_math.h`, `dc_blocker.h`, `smoother.h`,
`envelope_follower.h`, `harmonic_oscillator_bank.h`, `harmonic_oscillator_bank_simd.h`, `harmonic_types.h`,
`spectral_shape_recipe.h`, every Phase 1 TU (`profundum_core_test.cpp`, `profundum_core_spectral_test.cpp`,
`profundum_core_nonfinite_test.cpp`, `profundum_core_perf_test.cpp`, `spectral_shape_recipe_test.cpp`) and both Phase 1
helpers (`profundum_core_test_helpers.h`, `spectral_shape_recipe_test_helpers.h`).

### Verified facts these tasks depend on (each read on 2026-10-10)

Paths relative to `dsp/include/krate/dsp/` unless stated.

- **`ProfundumCore`** (`systems/profundum_core.h`, 704 lines): banner lines `:5–6` read "Includes Layer 0 / and Layer 2
  only."; header comment `:11` "no heap allocation: every buffer is a fixed-size member (FR-040)"; FTZ note `:37–38`;
  `class ProfundumCore {` `:39`; `RetriggerPhase` `:41`; `kControlInterval = 32` `:44`; `kPitchUpdateInterval = 16` `:45`;
  `kBankHeadroom = 4.0f` `:61`; copy deleted `:67–68` (the core is non-copyable, movable); `prepare(double, int)` `:77`;
  `reset()` `:99`; `setControls` `:108`; `setRetriggerPhase` `:136`; `noteOn` `:143`; `setPartialPanOffsets` `:153`;
  `processBlock` `:172–221` (run loop `:189–210`, `bank_.processStereoBlock(left + i, right + i, run)` `:206`, headroom
  rescale after the loop `:212–217` with the `right != left` guard); `stateFinite()` `:238–256`; `initialiseState` `:261`;
  `controlUpdate` `:312` (step 1 latch `:314–319`); `applyNoteOn` `:378` (Reset branch `:386–411`); `forwardPan` `:454`;
  `sameBits` `:628–630`; `sanitizeField` `:633–635` (non-finite → previous, else clamp, no denormal flush);
  `panAppliedNonZero_` `:687`.
- **Phase base**: `git show 47e43918:dsp/include/krate/dsp/systems/profundum_core.h` has 704 lines, `namespace Krate::DSP {`
  at `:33` and `}  // namespace Krate::DSP` at `:704`. `git diff --stat 47e43918 -- dsp/include` is empty today.
- **L1 primitives**: `Biquad` `primitives/biquad.h:308`, `configure(FilterType, float frequency, float Q, float gainDb, float sampleRate)`
  `:330`, `process` `:352` (non-finite input resets and returns 0, `:354–357`), `reset()` `:385`, `getZ1()`/`getZ2()`
  `:391/:394`; `enum class FilterType { Lowpass, Highpass, Bandpass, Notch, … }` `:68–77`. `DCBlocker` `primitives/dc_blocker.h:94`,
  `prepare(double, float cutoffHz)` `:135`, `reset()` `:154`, `process` `:190`, `x1_`/`y1_` private. `OnePoleSmoother`
  `primitives/smoother.h:134`: `configure(ms, fs)` `:160`, `setTarget` `:170`, `getCurrentValue()` `:191`, `process()` `:197`
  (snaps to target within 1e-4), `snapToTarget()` `:257`, `snapTo(float)` `:263`.
- **L0**: `Sigmoid::recipSqrt(float)` `core/sigmoid.h:188`; `WavefoldMath::sineFold(float x, float gain)` `core/wavefold_math.h:366`,
  `kSineFoldGainEpsilon = 0.001f` `:45`; `detail::isFinite(float)` `core/db_utils.h:118`; `detail::flushDenormal(float)` `:263`.
- **Recipe**: `SpectralShapeRecipe::kCoreOutputRmsDb = -12.0f` (`processors/spectral_shape_recipe.h:51`), `kPowerTarget` `:53`,
  named coordinates `:82–84` (defined `:227–264`). Processors test helper
  (`dsp/tests/unit/processors/spectral_shape_recipe_test_helpers.h`, namespace `Krate::DSP::ProfundumTest` `:35`):
  `struct Descriptors` `:77`, `describePowers(std::span<const double>)` `:92`, `fullOf(c, f0, fs, N)` `:175`,
  `gridPoint` `:183`, `midGrid()` `:195` (== `kDefaultControls`).
- **Phase 1 systems helper** (`dsp/tests/unit/systems/profundum_core_test_helpers.h`, namespace `:47`): `requireLREqual` `:61`,
  `coreSampleRate` `:69`, `struct Render` `:78`, `renderCore(ProfundumCore&, Controls, f0, seconds, policy, record, block, f0Trajectory)`
  `:90` (it calls `setRetriggerPhase`, `setControls`, `noteOn`, then renders), `bh7Window` `:142`, `goertzelPhase` `:157`,
  `fftDouble` `:173`, `bh7PowerSpectrum` `:206`, `renderedHarmonicPowers` `:235`, `aliasFftLength` `:257`,
  `struct AliasResult { aliasedDbfs; excludedFraction; totalDb; }` `:265`, `aliasedPower` `:273`, `rmsDbfs` `:453`.
  It includes `<krate/dsp/systems/profundum_core.h>`, so **no processors-layer file may include it**.
- **Shared test helpers** (repo-root `tests/test_helpers/`, included by bare name): `allocation_detector.h`
  (`startTracking` `:53`, `stopTracking` `:59`, `AllocationScope` `:111`, see deviation 5), `render_fingerprint.h`
  (`fingerprintRender` `:73`, `compareFingerprints` `:122`), `low_frequency_metrics.h` (`kLowFrequencyFftSize = 262144` `:64`,
  `calculateCorrelation` `:478`), `enable_ftz_daz.h`.
- **CMake** (`dsp/tests/CMakeLists.txt`): `add_executable(dsp_primitives_tests` `:87`, `oversampler_test.cpp` `:99`;
  `add_executable(dsp_processors_tests` `:156`, Phase 1 recipe comment + TU `:253–254`; `add_executable(dsp_systems_tests`
  `:326`, Phase 1 comment block `:564–593`, TUs `:594–597`, list closes `:598`; the `-fno-fast-math`
  `set_source_files_properties(` block opens `:702`, Phase 1 entry `:1075–1076`, closes with
  `PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"` `:1077`. `dsp/CMakeLists.txt`:
  `KRATE_DSP_PRIMITIVES_HEADERS` `:104–126`, `KRATE_DSP_PROCESSORS_HEADERS` `:128` (Phase 1 entry `:156–157`),
  `KRATE_DSP_SYSTEMS_HEADERS` `:161` (Phase 1 entry `:183–184`).
- **Tools**: `tools/run-cpu-tests.js:56` filter `'[performance],[perf],[.perf],[benchmark],[!benchmark],[long]~[vorago-sweep]'`
  (it runs every `[long]` case too); `tools/pin-perf-cores.ps1` usage `-Exe <path> [-ExeArgs a b ...]` (`:17`);
  `tools/check-portability.js`, `tools/lint-apple-globals.js`, `tools/run-clang-tidy.ps1` exist. Phase 1's
  `specs/profundum-phase1-harmonic-core/check-footprint.js` is the pattern for T002.
- **ODR sweep** (2026-10-10): `grep -rn "class PolyphaseHalfbandOversampler\|PolyphaseHalfband\|HarmonicFoldTest\|class HarmonicFold\|Phase1Ref" dsp plugins tests tools`
  returned **0 hits**. No `namespace … detail` exists inside `ProfundumTest`, so the Phase1Ref copy's unqualified
  `detail::isFinite` still resolves to `Krate::DSP::detail`.

### Commands (Git Bash, full CMake path, always)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_primitives_tests dsp_processors_tests dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_primitives_tests.exe "[polyphase-halfband]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "HarmonicFold_*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ProfundumCore_Excitation*" 2>&1 | tail -5
```

Filter by test name with a positional argument (tags in `[brackets]`). Never `ctest -R`. Capture any run longer than about
30 s to a log in the scratchpad (`… > "$LOG" 2>&1`) and read the log; never re-run a slow suite for its output. Build only
the targets a task touches, and batch independent rebuilds into one `--target a b c` call.

### Rules for every task

1. Failing test **first**: build and watch it fail (a compile error on a missing symbol counts). Then implement, rebuild with
   **zero warnings**, and run the named cases until they pass.
2. Header-only, `namespace Krate::DSP`. PascalCase classes, camelCase functions, `trailing_` members, `kPascalCase`
   constants. Designated initialisers with `f` literals; every double→float conversion an explicit `static_cast`; no
   narrowing in brace init. Finiteness via `detail::isFinite` only, never `std::isnan`/`std::isfinite`; denormal setter
   values detected by bit pattern (`(bits & 0x7F800000u) == 0`).
3. Audio-thread methods are `noexcept`, allocation-, lock-, exception- and I/O-free; no `std::function`; storage is
   fixed-size members; every table read uses a clamped index.
4. Scalar code only: no SIMD intrinsics, no aligned loads.
5. Test helpers are `inline` functions in a named namespace, never an anonymous namespace inside a header.
6. Tags: `[primitives][polyphase-halfband]`; `[processors][profundum][edge-fold]`; `[systems][profundum][edge-fold]`.
   `[long]` only where a case exceeds about 15 s and its failure mode is toolchain-independent, each with a per-push
   `…Smoke` sibling. **Never `[long]`:** NaN/Inf guards, L == R identity, zero-mix bit-identity, zero-in-zero-out, state
   rules. Perf: `[systems][profundum][edge-fold][.perf]`. Listening: `[systems][profundum][edge-fold][.listen]`.
7. `memcmp` only for in-process bit identity. No bit-exact float goldens; cross-toolchain comparisons via
   `render_fingerprint.h`.
8. Every core render with an all-zero pan vector calls `ProfundumTest::requireLREqual(L, R, n)`.
9. **Steady state** starts at t_settle = max(0.5 s, 8·τ_slow) after the last control change / note start, computed in the
   test from the header constants: DC corner 2 Hz → τ = 79.6 ms → 0.637 s; Fold detector release τr =
   `kDetectorReleasePeriods`/f₀ (E0: 194 ms → 1.55 s); smoothers ≤ 10 ms.
10. Descriptors exactly as spec "Descriptor definitions": THD = Σₙ≥₂ wₙ/w₁ (dB); H = count of n ≥ 2, n·f₀ < fs/2, with
    wₙ ≥ w₁·10⁻⁶; E_even; L (RMS re input over integer periods ≥ 1 s); CF = 20·log₁₀(peak/RMS); C = Σ log₂(n)·wₙ/Σ wₙ;
    T₁ via BH7 Goertzel at f₀ (complex). Calibration sine = `double` sine at P = `HarmonicFold::kCalibrationPeak`
    (0.3552343859, −12 dBFS RMS). Notes: E0 = MIDI 16 (20.6017 Hz), E1 = 28 (41.2034 Hz), E2 = 40 (82.4069 Hz),
    C2 = 36 (65.4064 Hz), C0…C5 = MIDI 12…72. Rates 44.1 / 48 / 96 kHz unless stated.
11. If a test exposes a defect in a header that a **different** task owns, do not edit that header from a [P] task: record
    the case, its output and your diagnosis under "Defects found" at the bottom. T016 fixes it.
12. Never relax a threshold or shrink a workload to pass (FR-055, CLAUDE.md). A bar proven unreachable goes to the main
    loop with the measurement (SC-027).

---

## Group 0 — pre-flight (parallel; read-only or new script files only)

### T001 [P] ODR sweep and evidence reproduction (read-only)

- ODR: run `grep -rnE "(class|struct|enum class|using) (PolyphaseHalfbandOversampler|HarmonicFold|ExcitationControls)\b" dsp plugins tests tools`
  and `grep -rn "HarmonicFoldTest\|Phase1Ref\|profundum_phase1_core_ref\|excitationDriveLaw\|edgeDriveGain" dsp plugins tests tools`.
  Both must print nothing. Confirm no name in this task list is one of the roadmap hazards (`CharacterProcessor`,
  `SubOscillator`, `StereoField`, `MorphEngine`, `MorphNode`, `TransientDetector`, `HarmonicCloud`, `SpectralState`,
  `MacroMapper`).
- Evidence (plan S12 step 2), as **concurrent** background processes, each to its own log in the scratchpad, then `wait`:
  `node specs/profundum-phase2-edge-fold/edge-fold-model.js` with each of `phase`, `exc`, `fold`, `alias`;
  `node specs/profundum-phase2-edge-fold/halfband-design.js`; and every `specs/profundum-phase2-edge-fold/evidence/*.js`
  except `lib.js` (`validate.js` once each with `44100`, `48000`, `96000`). `ctlphase.js` alone takes about 5 min; it sets
  the wall clock.
- Compare against the plan: S4.6 table (THD at a = 30 −33.6 dB, 65 −19.6 dB, 100 +4.5 dB; H(100) = 17), S9.2, S9.3
  (`validate.js`), S9.4 (worst chain −84.8 dBFS at E2/44.1 kHz; positive control −48.0), C-4 step table (0.000 dB with
  instant attack), C-9 (`mixlin.js`), C-10 (`foldpan*.js`), C-11 (`foldmid.js`: rise 0.600 oct at 3.0), S5.3
  (`ctlphase.js`: worst 1.038). Every figure must match to the printed precision.
- Record the per-script wall clocks and the total under "Run log". Any mismatch: stop and report; later tasks assume the
  baseline.

### T002 [P] SC-021 footprint script `check-footprint.js`

- **Create** `specs/profundum-phase2-edge-fold/check-footprint.js` (Node, CommonJS, no dependencies), following
  `specs/profundum-phase1-harmonic-core/check-footprint.js` (its `execFileSync('git', …)` pattern, one PASS/FAIL line per
  check, non-zero exit on any FAIL). Usage: `node specs/profundum-phase2-edge-fold/check-footprint.js [baseRef]`, default
  `47e43918`.
- Checks:
  1. **Zero diff lines** (`git diff --numstat base -- file` shows `0 0` or nothing) for every path in the "Untouchable"
     list above (all 15 shared headers + `spectral_shape_recipe.h` + 5 Phase 1 TUs + 2 Phase 1 helpers). Missing file =
     FAIL.
  2. **`profundum_core.h`**: (i) every line of the base file that is a public declaration of Phase 1 (each line between
     `public:` `:40` and `private:` that starts, after trimming, with `void `, `[[nodiscard]]`, `static constexpr`,
     `enum class`, `ProfundumCore(` or `ProfundumCore&`) is present verbatim in the working file; (ii) every **removed**
     line (from `git diff -U0`) lies either in the two banner lines `:5–6` or inside the body of one of `prepare(`,
     `reset()`, `stateFinite()`, `controlUpdate(`, `applyNoteOn()`, `processBlock(` in the **base** file, located by
     brace-matching from each signature line (`applyNoteOn` starts at `:378`); (iii) the FTZ note lines `:37–38` are
     present verbatim.
  3. **SC-026 grep**: neither `polyphase_halfband_oversampler.h`, `harmonic_fold.h` nor the Phase 2 additions to
     `profundum_core.h` contain the tokens `Standard`, `High`, `LinearPhase` or `oversampler.h"`/`oversampler.h>`
     (the shared `Oversampler` include).
  4. **Layer grep (plan S8.1)**: no file under `dsp/tests/unit/processors/` that is new since `base`
     (`git diff --name-status base -- dsp/tests/unit/processors` status `A`, plus untracked via `git ls-files --others --exclude-standard`)
     contains `systems/` in an `#include` line; `harmonic_fold.h` includes nothing from `processors/` or `systems/`;
     `polyphase_halfband_oversampler.h` includes nothing outside `core/` and the standard library.
  5. Print `git diff --stat base -- dsp/include` for comply.
- **Verify now:** on today's tree checks 1, 2 and 5 PASS, and 3–4 PASS vacuously (files absent are reported as `SKIP`, not
  FAIL, until T006 creates them). Positive control: append a comment line to `dsp/include/krate/dsp/primitives/dc_blocker.h`,
  run, confirm FAIL, then `git checkout -- dsp/include/krate/dsp/primitives/dc_blocker.h` (that file only). Record both
  outputs under "Run log".

### T003 [P] SC-027 bar-integrity script `check-bars.js` and baseline

**Run this task only after the main loop has written the C-1…C-11 rulings into spec.md's Clarifications log.**

- **Create** `specs/profundum-phase2-edge-fold/check-bars.js` (Node, no dependencies). Modes:
  - `node check-bars.js baseline` → parses `spec.md`'s "## Success Criteria" table: for each row whose first cell is
    `**SC-0NN**`, extracts every numeric token in the threshold cell (regex for signed decimals with optional unit:
    `[−-]?\d+(\.\d+)?\s*(dB|dBFS|%|°|oct|ms|s|×)?`, normalising `−` U+2212 to `-`) and writes
    `specs/profundum-phase2-edge-fold/bars-baseline.json` as `{ "SC-001": ["20 dB", …], … }`.
  - `node check-bars.js check` → re-extracts and diffs against the baseline. Each changed/added/removed token is allowed
    only if the Clarifications section contains a line that names that SC id **and** the word `re-rul` (case-insensitive)
    **and** a measurement figure. Print one line per SC (`SAME` / `CHANGED+RULED` / `CHANGED-UNRULED`), exit non-zero on any
    `CHANGED-UNRULED`.
  - `node check-bars.js sc023` → asserts `plan.md` contains a "SC-023 — compression mechanism record" section with a table
    holding at least one row whose Status cell contains `Chosen`.
- **Verify:** `baseline` then `check` prints all `SAME`; edit a copy of spec.md in the scratchpad (pass its path as an
  optional 2nd argument) changing one SC-001 number, run `check` on the copy, confirm `CHANGED-UNRULED` and a non-zero exit.
  Record outputs under "Run log".

### T004 [P] Pinned Phase 1 reference: `extract-phase1-ref.js` → `profundum_phase1_core_ref.h`

- **Create** `specs/profundum-phase2-edge-fold/extract-phase1-ref.js` (Node). It runs
  `git show 47e43918:dsp/include/krate/dsp/systems/profundum_core.h`, and writes
  `dsp/tests/unit/systems/profundum_phase1_core_ref.h` with exactly these changes:
  - a 4-line header comment prepended **before** the original line 1: `// GENERATED by specs/profundum-phase2-edge-fold/extract-phase1-ref.js`,
    `// from 47e43918:dsp/include/krate/dsp/systems/profundum_core.h - DO NOT EDIT.`, `// SC-004(a) pinned Phase 1 reference;`
    `// only the enclosing namespace differs (Krate::DSP::ProfundumTest::Phase1Ref).`;
  - `namespace Krate::DSP {` (base `:33`) → `namespace Krate::DSP::ProfundumTest::Phase1Ref {`;
  - `}  // namespace Krate::DSP` (base `:704`) → `}  // namespace Krate::DSP::ProfundumTest::Phase1Ref`.
  The script fails (non-zero) if either anchor line is not found exactly once. It then prints a unified diff of the base
  file against the generated file (computed in-script; no external `diff`), which must show only the 4 added comment lines
  and the 2 namespace lines. Includes stay outside the namespace, unchanged.
- **Verify:** run it; quote the printed diff under "Run log"; run it a second time and confirm the output file is
  byte-identical (`git hash-object` before/after). Compilation is verified in T006 (the stub excitation TU includes it).

---

## Group 1 — CPU estimate refresh (alone; nothing else running)

### T005 Re-run `bench/candidate-design-bench-v2.cpp` pinned and isolated (plan S12 step 2, S9.5)

- Precondition: Group 0 finished; no build, suite, model or agent running.
- Compile with the line at the top of the file:
  `clang++ -std=c++20 -O2 -ffast-math -march=x86-64-v2 -DNDEBUG -Idsp/include specs/profundum-phase2-edge-fold/bench/candidate-design-bench-v2.cpp -o <scratchpad>/bench2.exe`.
  If `clang++` is absent, record that and skip (the C++ gate is SC-018 in T013).
- Run 5 times, 20 s apart, through `pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe <scratchpad>/bench2.exe`.
- Paste the five "both / excitation / Fold" figures and their median into plan S9.5 under a new line
  "Re-run 2026-…, T005, pinned, isolated, median of 5". Expected near 1.31 % (both). A median above 1.45 % is not a failure
  here; flag it in "Run log" as an early R-1 warning for T013.

---

## Group 2 — CMake registration with compiling stubs (sequential, single task)

### T006 Register every Phase 2 file in CMake

- **Create stubs** (each with a file comment naming the layer, "Profundum Phase 2 (specs/profundum-phase2-edge-fold)" and
  the SCs it will carry from plan S8.1/S8.2):
  - `dsp/include/krate/dsp/primitives/polyphase_halfband_oversampler.h`: `#pragma once`, `namespace Krate::DSP {}`
    ("Layer 1: DSP Primitive").
  - `dsp/include/krate/dsp/processors/harmonic_fold.h`: same ("Layer 2: DSP Processor").
  - TUs `polyphase_halfband_oversampler_test.cpp`, `harmonic_fold_test.cpp`, `harmonic_fold_nonfinite_test.cpp`,
    `profundum_core_excitation_test.cpp`, `profundum_core_excitation_spectral_test.cpp`, `profundum_edge_fold_chain_test.cpp`,
    `profundum_core_excitation_nonfinite_test.cpp`, `profundum_edge_fold_perf_test.cpp`: the file comment, the matching
    `#include <krate/dsp/…>` of the header under test, `#include <catch2/catch_test_macros.hpp>`, no test cases. The
    stub `profundum_core_excitation_test.cpp` also includes `"profundum_phase1_core_ref.h"` and
    `<krate/dsp/systems/profundum_core.h>` together, which proves the two classes coexist (T004).
- **Edit `dsp/CMakeLists.txt`**: add `include/krate/dsp/primitives/polyphase_halfband_oversampler.h` to
  `KRATE_DSP_PRIMITIVES_HEADERS` (before the closing `)` at `:126`) and `include/krate/dsp/processors/harmonic_fold.h` to
  `KRATE_DSP_PROCESSORS_HEADERS` after `:157`, each under `# Profundum Phase 2 (specs/profundum-phase2-edge-fold)`.
- **Edit `dsp/tests/CMakeLists.txt`**:
  - `dsp_primitives_tests`: `unit/primitives/polyphase_halfband_oversampler_test.cpp` after `oversampler_test.cpp` (`:99`).
  - `dsp_processors_tests`: after `:254`, the comment `# Profundum Phase 2 (specs/profundum-phase2-edge-fold): HarmonicFold (L2) - SC-006/007/008/010/011/012(d)/013(a)(c)/015/016/017/019/024/025/026`
    then `unit/processors/harmonic_fold_test.cpp` and `unit/processors/harmonic_fold_nonfinite_test.cpp`.
  - `dsp_systems_tests`: after `:597` (before the closing `)` at `:598`), a Vorago-style comment block mapping each TU to
    its SCs (plan S8.1/S8.2), "ENUMERATED, not globbed", then the five `unit/systems/profundum_*` Phase 2 TUs.
  - `-fno-fast-math` block: after `:1076` (before the `PROPERTIES` line `:1077`), add
    `# Profundum Phase 2: injects NaN/Inf bit patterns and NaN table sentinels (SC-016, S4.3/S5.3 lookups). The perf TU stays out.`
    then `unit/processors/harmonic_fold_nonfinite_test.cpp` and `unit/systems/profundum_core_excitation_nonfinite_test.cpp`.
    Nothing else.
- **Verify**: reconfigure `"$CMAKE" --preset windows-x64-release`; build the three targets in **one** call with zero
  warnings; `dsp_primitives_tests.exe 2>&1 | tail -3`, `dsp_processors_tests.exe "[profundum]" 2>&1 | tail -3` and
  `dsp_systems_tests.exe "[profundum]" 2>&1 | tail -3` run green (Phase 1 cases unchanged). Record the build wall clock.

---

## Group 3 — oversampling primitive (sequential)

### T007 `PolyphaseHalfbandOversampler` (L1, plan S3; C-1)

Files: `primitives/polyphase_halfband_oversampler.h`, `dsp/tests/unit/primitives/polyphase_halfband_oversampler_test.cpp`.

**Failing tests first** (tag `[primitives][polyphase-halfband]`; the shared test main already sets FTZ/DAZ,
`dsp/tests/CMakeLists.txt:88–89`):

- `PolyphaseHalfband_DesignResponse`: compute each stage's decimation response **analytically in double from the class's
  float coefficient arrays** (branch A = even-index coefficients, B = odd-index; each section H(z) = (a + z⁻²)/(1 + a·z⁻²) at
  the stage's input rate; H(ω) = ½(A(e^{jω}) + e^{−jω}·B(e^{jω}))), 8192 points over [0, π], normalised to |H(0)|. A base
  frequency f maps to ω = π·f/fs for the outer stage (input rate 2fs) and ω = π·f/(2fs) for the inner stage (input rate 4fs):
  - outer (`kOuterCoefficients`): |H| within **±0.01 dB** for f ≤ 0.40·fs; **−3.01 ± 0.05 dB** at f = 0.5·fs;
    **≤ −100 dB** for every f in [0.55·fs, fs];
  - inner (`kInnerCoefficients`, stage input rate 4fs): within ±0.01 dB for f ≤ 0.5·fs; **≤ −100 dB** for every f in
    [1.5·fs, 2·fs].
  Print the worst stopband figure of each (plan: −106.6 / −116.8 dB).
- `PolyphaseHalfband_UnityPassband`: F = 4 and F = 2 at 48 kHz: a 1 kHz `double`-generated sine at −12 dBFS RMS, upsample
  then `downsample` (identity hook) sample by sample for 1 s; after 0.1 s, output RMS within **±0.01 dB** of input RMS,
  and the output BH7 spectrum shows no component above −120 dB re the tone outside ±8 bins of 1 kHz (Phase 1-style BH7 not
  available in primitives; implement a local `double` Goertzel at 3, 5, 7 kHz instead and assert each ≤ −120 dB re tone).
- `PolyphaseHalfband_RenderMatchesDesign`: F = 2: feed a unit impulse then 8191 zeros, collect the 2·8192 upsampled
  outputs, FFT in double (local radix-2), magnitude normalised to DC: matches the analytic outer response (×2 path gain
  removed) within 0.01 dB for f ≤ 0.40·fs and is ≤ −100 dB for f in [0.55·fs, fs]. This proves the C++ branch order
  (plan S3.2: downsampler feeds `in[1]` to A and `in[0]` to B).
- `PolyphaseHalfband_ZeroInZeroOut`: after `reset()`, 4096 zero samples through up and down at F = 2 and 4 give exactly
  `0.0f` everywhere (bit test).
- `PolyphaseHalfband_CopyContinuesIdentically`: render 1000 samples of a sine, copy the object, render 1000 more on both:
  `memcmp == 0`.
- `PolyphaseHalfband_ResetDeterministic`: two instances with different histories, `reset()` both, identical input →
  `memcmp == 0`; `setFactor(3)` → `factor() == 4`; `setFactor(2)` resets state.
- `PolyphaseHalfband_HookOrder`: a counting hook records how many times and in what order it is called per `downsample`:
  F = 4 → 2 calls (the two 2fs samples), F = 2 → 2 calls.
- `PolyphaseHalfband_GroupDelayRecorded`: print the low-frequency group delay (phase slope at 20 and 80 Hz, 48 kHz) of the
  up→down round trip for F = 2 and 4 (plan: ≈ 1.9 base samples per direction). `REQUIRE` finite; no threshold.
- `PolyphaseHalfband_NoexceptAndNoHeap`: `static_assert(noexcept(...))` on `upsample`, `downsample`, `reset`, `setFactor`;
  `static_assert(std::is_trivially_copyable_v<PolyphaseHalfbandOversampler>)`.

**Implement** exactly plan S3.1/S3.3: includes `core/db_utils.h` + `<array>`, `<cstddef>`, `<algorithm>` only (no L1
include, C-1(b-i)); the coefficients verbatim from plan S3.1; inline `Section { float a, x1, y1; process: y = a·(x − y1) + x1 }`;
`Stage2x<N>` with separate up/down branch arrays; `upsample(x, out)` writes `factor()` samples; `downsample(in, hook)`;
`stateFinite()` over every section's `x1`/`y1` via `detail::isFinite`; `kMaxFactor = 4`; factor default 4. Doc comment
carries the FTZ/DAZ precondition (S3.1). No per-call NaN reset (it would desynchronise a branch pair, C-1(b)).

**Verify:** build `dsp_primitives_tests`, zero warnings; `dsp_primitives_tests.exe "[polyphase-halfband]"` green.

---

## Group 4 — HarmonicFold API, tables, detector (sequential)

### T008 `HarmonicFold` declarations, static map tables, detector; Fold test helpers (plan S4.1–S4.4; FR-020/021/022/031/034/037/042/043; SC-016 setters; SC-024(a); C-4, C-11)

Files: `processors/harmonic_fold.h`, new `dsp/tests/unit/processors/harmonic_fold_test_helpers.h`,
`harmonic_fold_test.cpp`, `harmonic_fold_nonfinite_test.cpp`.

**Test helper first** — `harmonic_fold_test_helpers.h`, namespace `Krate::DSP::HarmonicFoldTest`. Includes only
`"spectral_shape_recipe_test_helpers.h"`, `"low_frequency_metrics.h"`, `<krate/dsp/processors/harmonic_fold.h>` and the
standard library. **No `systems/` include, directly or indirectly.** Contents:
- verbatim transcriptions (same bodies, same arithmetic order) of Phase 1's `kBh7Coefficients`, `safeDb10`, `bh7Window`,
  `goertzelPhase`, `fftDouble`, `bh7PowerSpectrum`, `renderedHarmonicPowers`, `aliasFftLength`, `AliasResult`,
  `aliasedPower` (`profundum_core_test_helpers.h:52–305`); plus a new `std::complex<double> goertzelPhasor(const float*, std::size_t L, double fs, double hz)`
  (BH7-windowed, the same window and normalisation as `goertzelPhase`, returning the complex value);
- `std::vector<float> calibrationSine(double fs, double f0, double seconds, double peak = HarmonicFold::kCalibrationPeak)`
  generated in `double` (`peak·sin(2π·f0·n/fs)`), cast once;
- `std::vector<float> recipeAdditive(const SpectralShapeRecipe::Controls&, double f0, double fs, double seconds, int N = 64)`:
  the masked recipe (`ProfundumTest::fullOf`, Σ aₙ² = P₀) rendered as √½·Σ aₙ·sin(2π·n·f0·t) in `double` (√½ = the bank's
  centre-pan gain, so the result sits at the core's −12 dBFS RMS per channel: P₀/2·½ = 0.0631 → −12.0 dBFS); REQUIRE the
  generated RMS within 0.1 dB of −12 dBFS;
- `struct FoldDescriptors { double thdDb; int h; double levelDb; double crestDb; double centroidOct; std::complex<double> t1; }`
  and `FoldDescriptors describeFold(const float* in, const float* out, std::size_t n, double fs, double f0)` over the
  largest integer number of periods ≥ 1 s;
- `std::vector<float> renderFold(HarmonicFold&, const std::vector<float>& in, std::size_t block)` (stereo L = R = input,
  returns L) and a stereo overload.

**Failing tests first:**
- `harmonic_fold_test.cpp`:
  - `HarmonicFold_ApiAndConstants`: `static_assert`s: `kSoftEnd == 30`, `kCompressionEnd == 65`, `kNormalisationRampEnd == 40`,
    `kFoldIndexMax == 3.0f` (C-11), `kSplitHz == 500`, `kSplitEngageAmount == 10`, `kDcCornerHz == 2`, `kOutputCeiling == 6`
    and `kOutputCeiling < 8.0f` (SC-019), `kTablePoints == 401`, `kDetectorAttackMs == 0.0f`, `kDetectorReleasePeriods == 4`,
    `latencySamples() == 0` (SC-026); `noexcept` static_asserts on `setAmount`, `setFundamentalHz`, `processBlock`,
    `prepare`, `reset`; `std::is_copy_constructible_v<HarmonicFold>` and `std::is_nothrow_move_constructible_v<HarmonicFold>`.
    Default `amount() == 0.0f`, `fundamentalHz() == kDefaultFundamentalHz`. Clamp: `setAmount(-5)` → 0, `setAmount(250)` → 100.
    `oversamplingFactor()` is 4 after `prepare(44100|48000|96000)` and 2 after `prepare(192000)` (S3.3 factor table).
  - `HarmonicFold_DetectorTimingConstants` (SC-024(a)): after `prepare(48000)`, `releaseSeconds() >= kDetectorReleasePeriods / 20.6f`
    (default E0) and `== Approx(4.0 / 20.6017223).epsilon(1e-6)`; after `setFundamentalHz(41.2034f)`, `releaseSeconds()`
    ≈ 4/41.2034. `setFundamentalHz(1.0f)` clamps to 8; `setFundamentalHz(30000.0f)` clamps to 20000.
  - `HarmonicFold_StaticMapTables`: through a test-only friend-free path — the tables are verified by output: at amount 30
    the steady output of the calibration sine at C2/48 kHz has THD within ±1 dB of plan S4.6 (−33.6 dB) and at 65 within
    ±1 dB of −19.6 dB; at 100, H ≥ 15 (plan 17). (These are anchors, not the SC gates; they catch a transcription error early.
    They need `processBlock`, so they stay red until T009.)
- `harmonic_fold_nonfinite_test.cpp` (`-fno-fast-math`):
  - `HarmonicFold_TableLookupAtEndpoint` (S4.3): a test struct `{ std::array<float, HarmonicFold::kTablePoints> t; float sentinel; }`
    with `static_assert(offsetof(S, sentinel) == sizeof(std::array<float, 401>))`, `t[i] = float(i)`, `sentinel = quiet NaN`
    (from bits `0x7FC00000`). `detail::lerpAmountTable(s.t, 100.0f) == 400.0f` exactly; `(s.t, 99.75f) == 399.0f`; `(s.t, 0.0f) == 0.0f`;
    all three `detail::isFinite`. `static_assert(detail::amountTableIndex(400.0f) == 399)`, `(0.0f) == 0`.
  - `HarmonicFold_NonFiniteSetters` (SC-016 setter part): NaN (`0x7FC00000`), +Inf (`0x7F800000`), −Inf (`0xFF800000`) passed to
    `setAmount` after `setAmount(42)` leave `amount() == 42.0f`; denormals from bits `0x00000001` and `1e-40f` give
    `amount() == 0.0f` exactly. The same for `setFundamentalHz` (non-finite keeps; denormal → 0 → clamped to 8.0f).
    Setters called **before** `prepare` store sanitised values (FR-042): `HarmonicFold f; f.setAmount(250); REQUIRE(f.amount() == 100.0f)`.

**Implement** (plan S4.1–S4.4 verbatim):
- Includes (L0 + L1 only): `core/db_utils.h`, `core/sigmoid.h`, `core/wavefold_math.h`, `primitives/biquad.h`,
  `primitives/dc_blocker.h`, `primitives/smoother.h`, `primitives/polyphase_halfband_oversampler.h`, std headers. Class
  `@note` FTZ/DAZ precondition (S4.1 text).
- All constants of S4.1 including `kCalibrationPeak = 0.3552343859f`, `kSoftDriveAtSoftEnd = 0.43f`,
  `kCompressionDriveAtEnd = 1.20f`, `kFoldIndexMax = 3.0f`, `kAmountSmoothMs = 10`, `kDetectorHoldMs = 125`,
  `kDetectorSegments = 8`, `kDefaultFundamentalHz = 20.6017223f`, `kMinFundamentalHz = 8`, `kMaxFundamentalHz = 20000`,
  `kDetectorFloor = 1e-6f`.
- `detail::amountTableIndex` (`constexpr`) and `detail::lerpAmountTable` exactly as S4.3, with the two `static_assert`s.
- `prepare(double fs, std::size_t)`: computes the five `std::array<float, 401>` tables in `double` over a = 0, 0.25 … 100:
  g(a), θ(a), V(a) = sat(P), mk(a) = P / max_{0≤q≤P}|z(q)| (4096 points on q), c(a) = mk·(2/K)·Σ z(P·sin φₖ)·sin φₖ / P with
  K = 512 — formulas of S4.3 (`sat` uses `Sigmoid::recipSqrt` semantics `(P/g)·recipSqrt(g·q/P)`, identity when g < 1e-6;
  `z` uses `WavefoldMath::sineFold` semantics, identity when θ < `kSineFoldGainEpsilon`; do the table math in `double`
  with the same formulas, not by calling the float functions). It also sets the factor (4 for fs ≤ 100 kHz, else 2),
  configures per channel `splitBase` (`FilterType::Lowpass`, 500, 0.70710678f, 0, fs), `splitOs` (same at F·fs), three
  notches (`FilterType::Notch` at {0.50, 0.52, 0.55}·fs with Q {1.5, 2.0, 1.5}, at 2·fs), `DCBlocker` (`kDcCornerHz`),
  the amount smoother (`kAmountSmoothMs`), the detector (`segLen = ceil(round(0.125·fs)/8)`, αr from `fundamentalHz_`),
  `idle_ = true`, snaps the smoother to the pending amount, `preCeilingPeak_ = 0`, `prepared_ = true`. No heap.
- `reset()`: zero every filter/oversampler/detector state and `preCeilingPeak_`; snap the smoother to the pending amount;
  keep the pending amount and fundamental; `idle_ = (pending amount == 0)`.
- Setters/observers of S4.1 with FR-043 sanitising (non-finite keeps; denormal bit pattern → 0.0f; then clamp).
- Detector state and the S4.4 update as a private per-sample method, **per channel** (`d_ch = |x_ch|`, two independent
  detector states with identical constants — C-4 ratified 2026-10-10 as per-channel; leave a one-line comment naming the
  rejected shared variant), `static_assert(kDetectorAttackMs == 0.0f)` beside the instant-attack branch, `env` flushed with
  `detail::flushDenormal` on each release step.
- `processBlock` is a **stub** in this task: returns before prepare (buffers untouched, FR-042) and otherwise leaves the
  buffers untouched. T009 implements it.

**Verify:** build `dsp_processors_tests`, zero warnings; `HarmonicFold_ApiAndConstants`, `HarmonicFold_DetectorTimingConstants`,
`HarmonicFold_TableLookupAtEndpoint`, `HarmonicFold_NonFiniteSetters` pass; `HarmonicFold_StaticMapTables` fails (expected,
until T009). `dsp_processors_tests.exe "[profundum]"` otherwise green.

---

## Group 5 — HarmonicFold processing (sequential)

### T009 `HarmonicFold::processBlock`: split stage, oversampling, notches, DC, clamp, idle (plan S2.1, S4.5; FR-021/026/027/028/033/040/042/043/044; SC-010, SC-011 zero-in, SC-013(a), SC-015, SC-016, SC-017, SC-019, SC-026)

Files: `processors/harmonic_fold.h`, `harmonic_fold_test.cpp`, `harmonic_fold_nonfinite_test.cpp`.

**Failing tests first** (`harmonic_fold_test.cpp` unless noted):
- `HarmonicFold_TransparentAtZero` (SC-010, Fold arm): amount 0, calibration sine at C0…C5 × {44.1, 48, 96} kHz, 2 s:
  residual power of (output − input) ≤ **−90 dB** re input power (expected exactly 0: idle is a bit-exact copy; also
  `memcmp == 0`).
- `HarmonicFold_ZeroInZeroOut` (SC-011 zero clause): for amount ∈ {0, 30, 65, 100}: `setAmount(a)`, `reset()` (snaps), one
  block of 4096 zeros → every output sample on both channels `== 0.0f`.
- `HarmonicFold_LREqualForIdenticalInput` (SC-013(a)): identical L/R calibration sine at E1 through amounts {0, 15, 47.5,
  82.5, 100} and through a 0→100 ramp: `ProfundumTest`-style bitwise L == R (implement `requireLREqual` locally in
  `HarmonicFoldTest`). Also `right == left` aliased buffers produce the same samples as the L of a non-aliased run.
- `HarmonicFold_ZeroLatency` (SC-026): `static_assert(HarmonicFold::latencySamples() == 0)`; amount 2, calibration sine at E1
  and C2, 48 kHz, steady window: the integer lag in −8…+8 maximising the cross-correlation of output and input is **0**.
- `HarmonicFold_BlockSizeIndependent` (SC-017): input = calibration sine at E1 with `setAmount(0)` then a single
  `setAmount(100)` before the render (a pending ramp spans block boundaries): host partitions {512, 64, 37, 1} →
  `memcmp == 0`. Two instances with the same calls → `memcmp == 0`.
- `HarmonicFold_NoAllocation` (SC-015): `prepare(48000, 512)` outside the tracked region; then with
  `AllocationDetector::instance().startTracking()`: `setAmount` sweeps, `setFundamentalHz`, `processBlock` with blocks
  1, 37, 64, 512, 4096 and **8192** (> maxBlockSize); `stopTracking() == 0`. Include a liveness check that the detector
  counts (allocate a `std::vector<int>(16)` inside a separate tracked region and require count > 0), as Phase 1 does.
- `HarmonicFold_RangeAndRates` (SC-016): `prepare(22050)` and `prepare(192000)`, amount 100, recipe-additive SC-009 state
  (`kSawAnchor` with `edge = 1`, `shift = 1`) at E1 for 2 s: all samples finite and |y| ≤ `kOutputCeiling`;
  `processBlock(l, r, 0)`, `processBlock(nullptr, r, 64)`, `processBlock(l, nullptr, 64)` are no-ops (buffers and
  `stateFinite()` unchanged); before `prepare`, `processBlock` leaves a non-zero buffer unchanged.
- `HarmonicFold_OutputBounded` (SC-019, Fold arm): for every L2 fixture input (calibration sine at E0–C5, recipe-additive
  `kSawAnchor`, mid grid, `kGrowl` and the SC-009 state at MIDI 12, 24, 36, 48, 60) at amounts {30, 65, 82.5, 100}:
  `clearPreCeilingPeak()` before each fixture, then `preCeilingPeak() < kOutputCeiling` (strict) and |y| ≤ `kOutputCeiling`.
  Print the worst `preCeilingPeak()` (plan ≤ 2.40).
- `harmonic_fold_nonfinite_test.cpp`: `HarmonicFold_NonFinite` (SC-016 sample part): a calibration-sine block with NaN, +Inf,
  −Inf at positions 10, 500, 2047 of L and 11, 501 of R, amount 100 → output finite, `stateFinite()` true, and bitwise equal
  to a twin Fold (same history) fed the same block with 0.0f at those positions.
- `HarmonicFold_StaticMapTables` (from T008) now passes.

**Implement** plan S4.5 + S2.1 per sample: (1) sanitise each channel's input (non-finite → 0.0f); (2) detector advance, then
`a = amountSmoother_.process()`; (3) idle (a == 0 and target == 0): output = sanitised input, residual-path state
untouched except `splitBase` which keeps running; on leaving idle reset `os`, `splitOs`, notches, `dc` per channel; (4)
otherwise per channel the S2.1 stage: `lo = splitBase(x)`, `base = c·(lo + (1 − w)(x − lo))`, `os.upsample(x, buf)`,
for each oversampled `u`: `r = Φ_fold(u) − c·(splitOs(u) + (1 − w)(u − splitOs(u)))` with
`Φ_fold(u) = mk·z(G·u)/G`, `G = (1 − β) + β·P/max(env, kDetectorFloor)`, β = smoothstep((a − 30)/10), w = smoothstep(a/10),
g/θ/V/mk/c from `lerpAmountTable`; then `y = base + dc.process(os.downsample(buf, notchHook))`; (5)
`preCeilingPeak_ = max(preCeilingPeak_, |y|)`, clamp ±`kOutputCeiling`. `right == left`: run channel 0 only. No
`std::function`; the notch hook is a lambda passed to the template `downsample`. Any `numSamples` (per-sample core, no
internal block ceiling). `stateFinite()` covers every oversampler (`stateFinite()`), biquad (`getZ1/getZ2`), the DC
blocker (mirror its last input/output in `dcIn_`/`dcOut_` per channel, since `x1_/y1_` are private), the smoother's
`getCurrentValue()`, `env_` and the ring.

**Verify:** build `dsp_processors_tests`, zero warnings; `dsp_processors_tests.exe "HarmonicFold_*"` green (T008 + T009
cases). Phase 1 recipe cases still green.

---

## Group 6 — HarmonicFold descriptor gates (sequential; may fix `harmonic_fold.h`)

### T010 Fold audibility, regimes, continuity, detector, level behaviour (SC-006, SC-007, SC-008, SC-011, SC-012(d), SC-013(c) shared-detector clause, SC-024(b)+C-3, SC-025; FR-022–FR-037)

**RE-EXECUTION 2026-10-11 (epoch 5):** the Group 6 gate was red on `HarmonicFold_RegimeContinuityStaticSmoke`. The tests of this task already exist and stay as written; apply the main-loop ruling recorded under "Defects found" → **D-1** (ease-out fold-index law, p chosen by `edge-fold-model.js` measurement, θ(100) = 3.0 unchanged, no bar moves), update the plan and spec as D-1 says, then re-verify this task's whole gate set (`"[edge-fold]"` and `"[edge-fold][long]"` on the rebuilt `dsp_processors_tests`).

Files: `harmonic_fold_test.cpp` (tests); `harmonic_fold.h` only for defects these tests expose (this task owns it now).
All renders 48 kHz unless stated; `setFundamentalHz(f0)` is called with the played f₀ in every dynamic test; windows
start at t_settle (rule 9).

**Failing tests first:**
- `HarmonicFold_ThdAndCountMonotonic` (SC-006) `[long]` + `HarmonicFold_ThdAndCountMonotonicSmoke`: calibration sine at E0,
  E1, E2, C2 (44.1 / 96 kHz added in `[long]`); amounts 0, 0.25, 0.5 … 100 (401 points; smoke: 41 points step 2.5 **plus**
  0.25): THD strictly increasing (including THD(0.25) > THD(0)), H non-decreasing. Print the minimum THD step
  (plan +0.079 dB at 65).
- `HarmonicFold_RegimesAudible` (SC-006): same inputs: THD rises ≥ **6 dB** within each of [0, 30], [30, 65], [65, 100]
  (plan 42.3 / 14.0 / 24.1 dB); H(100) ≥ **4 × H(30)** (plan 17 vs 1). Centroid: `recipeAdditive(midGrid())` at C2,
  C(100) − C(0) ≥ **0.5 octave** (plan 1.270 → 1.870, C-11).
- `HarmonicFold_CompressionRegimeDensityBeforeLevel` (SC-007(a)): calibration sine at E1 and C2, amounts in [30, 65]: THD(65) −
  THD(30) ≥ **10 dB** (plan +14.0), H(65) ≥ **1.5 × H(30)** (plan 1 → 3), and at every amount in [30, 65] (0.25 grid) L and CF
  within **±1 dB** of their values at 30 (plan ΔL +0.68, ΔCF −0.68).
- `HarmonicFold_CompressionLevelIndependent` (SC-007(b)): amount 47.5, input RMS −24 / −18 / −12 / −6 dBFS at E1 and C2:
  max − min THD ≤ **3 dB**. Teeth in the same test: `std::tanh(g·x)` in `double` with g bisected so its THD at −12 dBFS
  equals the Fold's THD at −12 dBFS (±0.1 dB): its THD spread over the four levels ≥ **12 dB**.
- `HarmonicFold_TrajectoryDistinctFromTanh` (SC-007(c)): calibration sine at E1; reference sweep `std::tanh(g·x)` in
  `double`, g log-spaced **0.1…100**, 401 points, parameterised by THD (monotone). Over the THD interval the Fold covers on
  [30, 65], take 33 THD-equispaced Fold points; for each, interpolate the reference's L and CF at that THD; FAIL if any Fold
  THD lies outside the reference THD range; median of √(ΔL² + ΔCF²) ≥ **3 dB** (plan 5.49). Print the RMS-matched variant's
  median (`tanh(g·x)·k(g)`, k making RMS equal) as **SC-022(d)**.
- `HarmonicFold_RegimeContinuityStatic` (SC-008(a)) `[long]` (401 points) + `…Smoke` (the 0.25 pairs straddling 0, 30, 65 and
  100, i.e. 0→0.25, 29.75→30→30.25, 64.75→65→65.25, 99.75→100, plus a 41-point grid): inputs calibration sine at E1 and C2 and
  `recipeAdditive(midGrid())` at C2; every adjacent pair |ΔL| ≤ **0.25 dB**, |ΔCF| ≤ **0.25 dB**, |ΔC| ≤ **0.05 oct**; for pairs
  with both THD ≥ −60 dB, |ΔTHD| ≤ max(3 × median adjacent |ΔTHD| over those pairs, **1 dB**). Print worst values (plan sine
  0.191 dB; mid 0.081 dB, 0.033 oct).
- `HarmonicFold_ContinuousInAmount` (SC-008(b)): same inputs; for every grid amount a (41-point per-push; 401 in a `[long]`
  sibling) and ε = 0.05: steady renders y(a+ε), y(a) from the same `reset()` and identical input: RMS(y(a+ε) − y(a)) /
  RMS(y(a)) ≤ **0.02**.
- `HarmonicFold_AutomationNoJump` (SC-008(c)): inputs as above. Scenarios: a 2 s 0 → 100 → 0 linear `setAmount` sweep
  (set per 32-sample block; evaluated as its two monotone segments), steps 0 ↔ 5, and steps ±2 around 30 and around 65, each
  with 0.5 s holds. **Sample delta:** every |y[k] − y[k−1]| ≤ **1.1 ×** the maximum steady-state |Δy| over every 0.25-grid
  amount the segment/step traverses (endpoints included; steady renders made in the same test). **Frame level:** frames of
  round(4·fs/f₀) samples hopped by round(fs/f₀): frame-to-frame RMS change (dB) ≤ J + max(**1 dB**, 1.5 × the |ΔL|
  accumulated on the (a) grid over the amount range one hop traverses), J = the maximum frame-to-frame RMS change of the
  steady renders of the segment's two endpoints under the same framing.
- `HarmonicFold_DcFreeWithEvenHarmonicInput` (SC-011, Fold arm): amounts {30, 65, 100}, inputs (i) calibration sine and (ii) a
  `double` asymmetric input (calibration sine + h2 at −20 dB + h4 at −30 dB re h1, sine phase, rescaled to −12 dBFS RMS),
  at E0, E1, E2: |mean| over ≥ 1 s integer-period window from t_settle ≤ **−80 dBFS** (20·log₁₀|mean| re 1.0). The real
  max-excitation input is the chain arm in T015.
- `HarmonicFold_FundamentalPreserved` (SC-012(d), Fold arm): amounts 0 … 100 (41-point per push, 401 in a `[long]` sibling) at
  E0, E1, E2 × {44.1, 48, 96} kHz on the calibration sine and `recipeAdditive(kSawAnchor)`: T₁ = output h1 phasor / input h1
  phasor; |∠T₁| ≤ **5°** and Re(T₁) > 0 (no polarity flip) at every amount; |T₁| ≥ **−6 dB** for a < 65 and ≥ **−12 dB** for
  a ∈ [65, 100] (plan min −9.46 dB on the sine at 100).
- `HarmonicFold_StereoDetectorPerChannel` (SC-013(c) detector clause, **C-4 ratified per-channel 2026-10-10**): amounts
  {47.5, 82.5, 100}, E1, L = calibration sine at −24 dBFS RMS, R = L − 6.02 dB (same phase), 1 s hold, then both step to
  L = −6 dBFS (R −6.02 dB below), 1 s hold. Steady L/R output RMS ratio within **±0.1 dB** of the 6.02 dB input ratio at
  every amount and both levels (plan: per-channel preserves it exactly); through the step, 4-period frames hopped by one
  period: the frame L/R ratio stays within ±0.1 dB of the input ratio (plan excursion 0.000 dB). Teeth: the same fixture
  with a shared max(|L|, |R|) detector built in the test must read outside ±0.1 dB at amount 82.5 (plan 1.51 dB).
- `HarmonicFold_NoInCycleGainModulation` (SC-024(b) + C-3): calibration sine at f₀ ∈ {20, 40, 80} Hz (48 kHz: periods 2400 /
  1200 / 600), amounts 47.5 and 82.5, steady before and after a −24 → −6 dBFS step: (i) periodicity residual
  RMS(y[k+P] − y[k]) / RMS(y) ≤ **−50 dB** over ≥ 1 s (no teeth claim, C-3); (ii) C-3's (b′): render with 1-sample blocks,
  record `detectorLevel()` over one steady period: (max − min)/mean ≤ **1e-4**; teeth: a `double` reference env with instant
  attack and a 1 ms release one-pole on |x| shows (max − min)/mean ≥ **0.1** over the same period.
- `HarmonicFold_SoftRegimeLevelDependent` (SC-025(a)): amount 15, E1 and C2, levels −24/−18/−12/−6 dBFS RMS: THD spread ≥
  **12 dB**.
- `HarmonicFold_FoldRegimeLevelNormalised` (SC-025(b)): amount 82.5, same: THD spread ≤ **3 dB**.

**Implement / fix:** these tests mostly validate T008/T009. If one fails, diagnose against the model (`edge-fold-model.js
fold`, `evidence/steps.js`, `hmargin.js`, `foldmid.js`, `sc013step.js`) before touching code. Allowed retunes, each recorded
under "Run log" with the model and C++ figures: R-3 (`kSoftDriveAtSoftEnd` 0.43 → 0.40 if an H count flips), R-11 (θ in
0.05 steps if the centroid misses, re-running S9.3/S9.4 evidence). Anything else → main loop.

**Verify:** build `dsp_processors_tests`, zero warnings; run `dsp_processors_tests.exe "[edge-fold]~[long]"` and, concurrently,
`dsp_processors_tests.exe "[edge-fold][long]"` to separate logs. All green. Record wall clocks.

---

## Group 7 — core excitation API and zero-mix identity (sequential; existing header)

### T011 `ProfundumCore` excitation declarations, laws, tables, sanitising, `prepare`/`reset`/`stateFinite` bodies; excitation test helpers (plan S5.1, S5.2, S5.6; FR-002/004/006/041/043; SC-002(a), SC-004(a) at rest, SC-016 core; C-1, C-7)

Files: `systems/profundum_core.h`; new `dsp/tests/unit/systems/profundum_excitation_test_helpers.h`;
`profundum_core_excitation_test.cpp`; `profundum_core_excitation_nonfinite_test.cpp`.

**Test helper first** — `profundum_excitation_test_helpers.h`, namespace `Krate::DSP::ProfundumTest` (new names only; run the
ODR grep on each name before adding). Includes `"profundum_core_test_helpers.h"`, `"profundum_phase1_core_ref.h"`,
`<krate/dsp/systems/profundum_core.h>`. Contents:
- `template <class Core> Render renderAnyCore(Core&, const SpectralShapeRecipe::Controls&, float f0, double seconds, policy, std::size_t block, const float* traj)`
  — the `renderCore` body (`profundum_core_test_helpers.h:90–124`) generalised so it drives both `ProfundumCore` and
  `Phase1Ref::ProfundumCore`;
- `Render renderExcited(ProfundumCore&, const Controls&, const ProfundumCore::ExcitationControls&, float f0, double seconds, policy = Reset, block = 32, traj = nullptr)`
  (calls `setExcitation` before `renderCore`'s sequence);
- `std::complex<double> goertzelPhasor(const float*, std::size_t L, double fs, double hz)` — identical body to the
  `HarmonicFoldTest` one (T008), BH7;
- `std::complex<double> t1Transfer(const float* out, const float* ref, std::size_t L, double fs, double f0)`;
- `double generatedEvenEnergyDb(const float* out, const float* ref, std::size_t L, double fs, double f0)` = Σₙ even |Yₙ −
  Yₙᴿ|² / w₁ (dB), n·f₀ < fs/2;
- `std::size_t aliasSafeFftLength(double fs, double f0)`: smallest 2ᵏ ≥ `aliasFftLength(fs, f0)` such that for m = 1…4 the
  distance from m·fs to the nearest multiple of f₀ is ≥ 16·fs/L Hz; `REQUIRE(L <= (1u << 22))`;
- a twin-core helper that renders the mix-0 reference with the same call sequence (`ExcitationControls` with `mix = 0`).

**Failing tests first:**
- `profundum_core_excitation_test.cpp`:
  - `ProfundumCore_ExcitationApi`: `static_assert`s on the S5.1 constants (`kEdgeDriveRangeDb == 12`, `kExcitationDriveMinDb == -12`,
    `kExcitationDriveMaxDb == 12`, `kExcitationAsymmetryBias == 0.5f`, `kExcitationSplitHz == 500`,
    `kExcitationSplitEngageMix == 0.1f`, `kExcitationDcCornerHz == 2`, `kExcitationSmoothMs == 5`, `kExcitationCeiling == 6`,
    `kExcitationCeiling < 2·kBankHeadroom`); `kDefaultExcitation` is {mix 0, drive 0.5, asymmetry 0}; `noexcept` on
    `setExcitation`, `excitation`, `effectiveExcitationDrive`, `excitationDriveLaw`, `edgeDriveGain`; after `prepare(48000)`,
    `excitation()` equals the defaults; `setExcitation({.mix = 2, .drive = -1, .asymmetry = 0.5f})` → {1, 0, 0.5};
    `excitationOversamplingFactor()` 4 at 44.1/48/96 kHz, 2 at 192 kHz.
  - `ProfundumCore_EdgeDriveLaw` (SC-002(a)): `kEdgeDriveFloor` vs 10^(−12/20) within **1e-6** relative;
    `edgeDriveGain(0) == kEdgeDriveFloor` and `edgeDriveGain(1) == 1` within 1e-6 relative; 33-point edge sweep: strictly
    increasing and |20·log₁₀(edgeDriveGain(e)) − 12·(e − 1)| ≤ **1e-4 dB**; `excitationDriveLaw` strictly increasing over 33
    points, `excitationDriveLaw(0)` ≈ 10^(−12/20) and `(1)` ≈ 10^(12/20) within 1e-6 (C-7). For every (drive, edge) on the
    5 × 5 grid {0, 0.25, 0.5, 0.75, 1}², set both, render to the next grid latch, read `effectiveExcitationDrive()` at
    offsets 0, 7 and 31 of the control interval: equals `excitationDriveLaw(drive)·edgeDriveGain(edge)` within **1e-6**
    relative.
  - `ProfundumCore_ExcitationZeroMixAtRest` (SC-004(a) first slice): with the default excitation (mix 0), the new core and
    `Phase1Ref::ProfundumCore`, same calls, `kSawAnchor` at E1, blocks 37 and 512, 1 s: `memcmp == 0` both channels. (T012
    extends this to the full fixture set and non-default drive/asymmetry.)
- `profundum_core_excitation_nonfinite_test.cpp` (`-fno-fast-math`):
  - `ProfundumCore_ExcitationNonFinite` (SC-016 core): for each field, after a valid `setExcitation`, NaN / +Inf / −Inf (bit
    patterns) keep the previous field; denormals (`0x00000001`, `1e-40f`) read back exactly `0.0f`; `setExcitation` before
    `prepare` is a no-op (Phase 1 FR-061 pattern) and `excitation()` returns the defaults. After a max-excitation render
    with a NaN field set, output finite, |y| ≤ `kExcitationCeiling`, `stateFinite()` true.
  - `ProfundumCore_ExcitationTableLookupAtCorners` (S5.3): the bilinear index helpers are `constexpr` free functions in
    `Krate::DSP::detail` (name them `excitationDriveIndex(float pi)` and `excitationBiasIndex(float pj)`; ODR-grep first):
    `static_assert(excitationDriveIndex(48.0f) == 47 && excitationBiasIndex(8.0f) == 7)`; a NaN-sentinel struct
    `{ std::array<float, 49 * 9> t; float sentinel; }` with `t[i*9 + j]`-style layout matching the implementation; a public
    `detail::bilinearExcitationTable(table, l2, b)` reads exactly the corner cells at (log₂ D, b) ∈ {−4, 2} × {0, 0.5}, all
    finite.

**Implement** (plan S5.1, S5.2, S5.6):
- Includes added: `core/sigmoid.h`, `primitives/biquad.h`, `primitives/dc_blocker.h`, `primitives/smoother.h`,
  `primitives/polyphase_halfband_oversampler.h`. **Do not** include `harmonic_fold.h`.
- Banner `:5–6` becomes "Includes Layer 0, Layer 1 and Layer 2 only." plus a line `//   Phase 2: specs/profundum-phase2-edge-fold`.
  Leave the `:11` "no heap allocation" comment true (C-1 makes it so). FTZ note `:37–38` verbatim plus one added line naming
  the excitation state (halfband sections, split/notch/DC filters, ADAA history).
- Append (never reorder, never edit Phase 1 declarations) the S5.1 public API and constants; `excitationDriveLaw(d)` =
  `static_cast<float>(std::pow(10.0, (-12.0 + 24.0·clamp(d,0,1)) / 20.0))`; `edgeDriveGain(e)` =
  `static_cast<float>(std::pow(10.0, 12.0·(clamp(e,0,1) − 1.0) / 20.0))`; `kEdgeDriveFloor = 0.251188643f`.
- Private `sanitizeExcitationField(x, prev, lo, hi)`: non-finite → prev; `(bits & 0x7F800000u) == 0` → 0.0f; else clamp.
  Phase 1's `sanitizeField` untouched.
- S5.2 state: per channel `PolyphaseHalfbandOversampler`, `Biquad splitBase, splitOs`, `std::array<Biquad, 3> notch`,
  `DCBlocker dc` + `dcIn_/dcOut_` mirrors, ADAA `v1, s1`, `std::array<float, 4> buf`; shared `pendingExcitation_`,
  `latchedExcitation_`, `excitationDirty_`, `excitationIdle_`, `excitationPreCeilingPeak_`, `OnePoleSmoother mixS_,
  log2DriveS_, biasS_`, `latchedDriveTarget_`, `std::array<float, 49 * 9> excMakeup_, excH1_`.
- `prepare` body (after the existing steps): filters at `sr` (split 500 Hz Butterworth at fs and F·fs, notches {0.50, 0.52,
  0.55}·fs with Q {1.5, 2.0, 1.5} at 2·fs, DC 2 Hz), factor (4 for fs ≤ 100 kHz else 2), tables in `double` (S5.4 "Tables":
  49 log₂D points over [−4, 2] × 9 bias points over [0, 0.5]; per cell K = 512 phases, yₖ = f(D·sin φₖ + b) − f(b) with
  f(v) = v/√(1+v²), mean removed, M = (P/√2)/rms(y), c = M·(2/K)·Σ yₖ sin φₖ / P), smoothers configured
  (`kExcitationSmoothMs`) and snapped to the pending coordinates, all excitation state reset, `excitationIdle_ = true`
  (pending mix is 0 by default; if non-zero the first latch engages), `excitationPreCeilingPeak_ = 0`.
- `reset` body: S5.6 row (state reset, ADAA `v1 = b`, `s1 = sqrt(1 + b·b)` from the pending bias, smoothers snapped,
  `excitationIdle_ = (pending mix == 0)`, peak cleared; `pendingExcitation_` kept).
- `stateFinite` body: the S5.6 additions appended to the existing expression (no Phase 1 term removed).
- `setExcitation` stores the sanitised pending value and sets `excitationDirty_`; `effectiveExcitationDrive()` returns
  `latchedDriveTarget_`. **No audio-path change in this task** (`processBlock` is untouched); the latch for SC-002(a) is the
  step 1b of T012 — so in this task implement step 1b's **latch arithmetic only** (latch + `latchedDriveTarget_` + smoother
  targets) inside `controlUpdate`, guarded so the audio path is unaffected. The T012 audio wiring builds on it.

**Verify:** build `dsp_systems_tests`, zero warnings; the four new cases pass; `dsp_systems_tests.exe "ProfundumCore_*"`
(all Phase 1 cases, unmodified) green; `node specs/profundum-phase2-edge-fold/check-footprint.js` PASS.

---

## Group 8 — core excitation rendering (sequential; existing header)

### T012 `exciteRun`, control timing, `applyNoteOn`, `processBlock` wiring (plan S5.3, S5.4, S5.6; FR-001/003/005/006/007/008/009/012/013; SC-004(a) full, SC-009, SC-013(a), SC-014, SC-015, SC-017, SC-026; C-5, C-6)

**RE-EXECUTION 2026-10-11 (epoch 5):** the Group 9 gate was red on `ProfundumCore_ExcitationZeroLatency` (mid grid lag −4). The implementation is correct and stays; apply the main-loop ruling under "Defects found" → **D-2** (SC-026 excitation arm re-scoped: sine argmax 0; mid grid h1 ≤ 0.5° and low-passed argmax 0; full-band lag and high-band group delay recorded ≤ 8 samples), then re-run this task's gate set on the rebuilt `dsp_systems_tests`.

**RE-EXECUTION 2026-10-11 (epoch 6):** the Group 9 gate was red again on `ProfundumCore_ExcitationZeroLatency`: the
250 Hz-band argmax read −1 at C2. Ruled in the main loop as **D-3** (under "Defects found"): the low band is the 125 Hz
4th-order low-pass, the 250 Hz figure is recorded. The main loop has ALREADY applied the test change (`kLowpassHz` 125,
`kRecordLowpassHz` 250 printed in the SC-022(i) line) and the spec / plan / Clarifications edits. Do not re-edit them;
verify the case on the rebuilt `dsp_systems_tests` (`ProfundumCore_ExcitationZeroLatency`), quote its printed figures
under D-3, then re-run this task's gate set.

Files: `systems/profundum_core.h`, `profundum_core_excitation_test.cpp`.

**Failing tests first** (`profundum_core_excitation_test.cpp`, 48 kHz unless stated, `requireLREqual` on every render):
- `ProfundumCore_ExcitationZeroMixBitIdentical` (SC-004(a), replaces/extends `…ZeroMixAtRest`): `mix = 0` with (drive,
  asymmetry) ∈ {(0, 0), (0.5, 0), (1, 1)}, against `Phase1Ref::ProfundumCore` driven by `renderAnyCore` with identical calls.
  Fixtures: every named state (`kSineAnchor`, `kTriangleAnchor`, `kSawAnchor`, `kHeavy`, `kHollow`, `kGrowl`,
  `kDefaultControls`) at E1 and C2; a D/B/E/S sweep (each field linear 0 → 1 → 0 over 2 s, one `setControls` per 32-sample
  block) with a 5 Hz ±50-cent `f0PerSample` vibrato around C2; a `Reset` and a `FreeRunning` `noteOn` mid-render; host blocks
  37 and 512. `memcmp == 0` on both channels, and the three (drive, asymmetry) renders `memcmp`-equal to each other.
- `ProfundumCore_ExcitationControlStepsNoClick` (SC-009): C2 and E1, mid grid and `kSawAnchor`. For each control ∈ {mix,
  drive, asymmetry, recipe edge}: (i) step 0 → 1 → 0 with 0.5 s holds, evaluated as two monotone segments; (ii) a 10 ms
  full-range linear sweep (one setter call per 32-sample block); the non-stepped excitation controls sit at mix 1, drive 1,
  asymmetry 1 (plan S5.3's evidence used asymmetry 1, mix 1); for the recipe-edge rows the recipe is the named input with
  only `edge` stepped. Steady ceiling = max |y[k] − y[k−1]| over steady renders of that control's 33-point grid
  within the traversed range (endpoints included), made in the same test. Every transition |Δy| ≤ **1.1 ×** ceiling. Each
  step is repeated at **16 step phases** spread over one f₀ period, each aligned to the 32-sample latch grid; gate the worst
  phase and print it (plan worst 1.038, C2 saw drive 0 → 1). An edge step moves both the recipe and the excitation drive,
  and its steady grid uses the same edge values; plan S5.3's edge rows modelled only the excitation part, so if an edge row
  fails, first run the same step at mix 0 (the recipe-only transition, which Phase 1 owns) before diagnosing the excitation.
- `ProfundumCore_ExcitationEngageNoClick` (SC-009 engage): `mix` 0 ↔ 0.05 and 0 → 1 steps from idle, same ceiling rule;
  additionally the first non-idle run produces no sample delta above the ceiling (FR-007: the reset-on-engage state is valid).
- `ProfundumCore_ExcitationLREqualAtZeroPan` (SC-013(a)): max excitation, every named state at E1 and C2, blocks 1/37/512:
  `requireLREqual`; and `left == right` aliased call equals the L of a separate-buffer render.
- `ProfundumCore_ExcitationRetriggerPolicy` (SC-014): max excitation. `Reset`: first output sample after `noteOn` `== 0.0f`;
  renders after `noteOn` from four different prior states (different f₀, Controls, ExcitationControls and elapsed time
  0.3 / 1.1 s) are `memcmp`-equal over 1 s. `FreeRunning`, same f₀ and controls: `memcmp`-equal to an uninterrupted render.
  A `FreeRunning` `noteOn` 7 semitones away (E-13): output finite and ≤ `kExcitationCeiling`.
- `ProfundumCore_ExcitationDeterministic` (SC-017): max excitation, `f0PerSample` vibrato, partitions {512, 64, 37, 1} with no
  setter calls between blocks → `memcmp == 0`; two instances, same call sequence → `memcmp == 0`.
- `ProfundumCore_ExcitationNoAllocation` (SC-015): after `prepare`, tracked region (`startTracking`/`stopTracking`) over
  `setExcitation`, `setControls`, `noteOn` (both policies), `processBlock` with the stage off, engaging (mix 0 → 1) and on,
  host blocks 1, 37, 64, 512, 4096: count `== 0`; liveness check as in T009; `static_assert(noexcept(...))` on every
  audio-thread method.
- `ProfundumCore_ExcitationZeroLatency` (SC-026): drive 0, mix 1, `kSineAnchor` and mid grid (C-6) at E1 and C2: cross-correlation
  against the twin mix-0 render, as re-scoped by **D-2** (spec SC-026): (a) `kSineAnchor` full-band argmax over lags −8…+8
  is **0**; (b) mid grid |∠T₁| ≤ **0.5°** and the argmax of both renders through an identical test-side 4th-order **125 Hz**
  Butterworth low-pass is **0** (D-3; 250 Hz before); (c) mid grid full-band argmax (searched ±16), the 250 Hz-band argmax
  (D-3) and the high-band group delay near 1 kHz and 4 kHz (adjacent-harmonic phase step, capped at the delivered
  partials) recorded as SC-022(i), full-band and high-band ≤ **8** base samples.

**Implement** (plan S5.3, S5.4, S5.6):
- `controlUpdate` step 1b (after step 1 `:314–319`): when `excitationDirty_` or step 1 re-latched the recipe, latch,
  compute D and `latchedDriveTarget_`, set targets of `mixS_`, `log2DriveS_` (log₂ D), `biasS_` (asymmetry ×
  `kExcitationAsymmetryBias`); if idle and mix > 0, reset the per-channel excitation state and clear idle. Coordinates only;
  M and c are never smoothed.
- `applyNoteOn`: Reset branch — same latch, `snapToTarget()` on the three smoothers, reset per-channel excitation state with
  `v1 = b`, `s1 = sqrt(1 + b·b)`; FreeRunning — latch only if dirty, no snap, no reset. Nothing touches bank state.
- `processBlock`: move the headroom rescale **into** the run loop (multiply `left + i … run` by `kBankHeadroom` right after
  `bank_.processStereoBlock`, same `right != left` guard), then `if (!excitationIdle_) exciteRun(left + i, right + i, run)`;
  set `excitationIdle_ = true` at the end of a run in which the mix smoother sits exactly at 0 with target 0.
- `exciteRun` per sample exactly plan S5.4: `m, l2, b` from the smoothers; `D = std::exp2(l2)`; `M`, `ce` from the clamped
  bilinear lookup (T011 helpers); `cm = 1 − m + m·ce`; `w = smoothstep(m / 0.1)`; `sb = std::sqrt(1 + b²)`, `fb = (b + b)/(sb + sb)`;
  per channel `lo = splitBase(x)`, `base = cm·(lo + (1 − w)(x − lo))`, `os.upsample(x, buf)`, per oversampled sample
  `v = D·buf[i]/P + b`, `s = sqrt(1 + v²)`, `y = (v + v1)/(s + s1)`, `v1 = v`, `s1 = s`, `ul = splitOs(buf[i])`,
  `buf[i] = m·M·(y − fb) + (1 − m)·buf[i] − cm·(ul + (1 − w)(buf[i] − ul))`; `r = os.downsample(buf, notchHook)`;
  `pre = base + dc.process(r)`; peak tracking; clamp ±`kExcitationCeiling`. P = `HarmonicFold`-independent constant
  `kCalibrationPeak`-equivalent: define a private `static constexpr float kExcitationCalibrationPeak = 0.3552343859f`
  (do not include `harmonic_fold.h`). `fb` and the per-sample ADAA share one `static` helper so zero-in-zero-out survives
  any FP contraction (R-2). `right == left`: channel 0 only.

**Verify:** build `dsp_systems_tests`, zero warnings; `dsp_systems_tests.exe "ProfundumCore_Excitation*"` and
`"ProfundumCore_EdgeDriveLaw"` green; all Phase 1 `ProfundumCore_*` cases green; `check-footprint.js` PASS (body edits only
inside the allowed members).

---

## Group 9 — CPU budget early (sequential; alone; may edit both headers)

### T013 `ProfundumEdgeFold_CpuBudget` and the R-1 ladder (SC-018; FR-054; plan S12 step 6, R-1)

Files: `profundum_edge_fold_perf_test.cpp`; `harmonic_fold.h` and `profundum_core.h` only if the ladder is needed.

**Test first** — `TEST_CASE("ProfundumEdgeFold_CpuBudget", "[systems][profundum][edge-fold][.perf]")`, built on the Phase 1 perf
harness pattern (`dsp/tests/unit/systems/profundum_core_perf_test.cpp:141`): 48 kHz, host block 64, 64 partials, 10 s audio per
run, one warm-up run, median of 5. Load: the core runs Phase 1 SC-013's load (D/B/E/S sweep + 5 Hz vibrato — reuse that TU's
load generator by copying it into a local `inline` function, since the Phase 1 TU is frozen); configurations measured with
the same core load:
- (A) core alone, mix 0;
- (B) core + excitation (mix 1, asymmetry 1, drive automated by a 0.5 Hz sine 0…1, one `setExcitation` per block);
- (C) core + Fold (Fold fed the core output, amount automated by a 0.5 Hz 0–100 sine, `setFundamentalHz` = played f₀);
- (D) core + excitation + Fold.
Gates: (A)/audio ≤ **1.0 %**; [(D) − (A)]/audio ≤ **1.5 %**; (D)/audio ≤ **2.5 %**. Print excitation-alone [(B) − (A)],
Fold-alone [(C) − (A)], total, and a stereo-forced variant of (D) (non-zero pan vector; the Fold fed L ≠ R) for the record.

**Run alone and pinned**, nothing else executing:
`pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe build/windows-x64-release/bin/Release/dsp_systems_tests.exe -ExeArgs "ProfundumEdgeFold_CpuBudget"`
(≈ 1–2 min). Log to the scratchpad; record figures under "Run log".

**If a gate fails**, apply the plan R-1 ladder in order, re-measuring (alone, pinned) after each step and re-running the
affected correctness suites (`HarmonicFold_*`, `ProfundumCore_Excitation*`) concurrently before the next measurement:
1. run the split LPo at the 2fs stage instead of F·fs (subtraction after the inner decimation); re-verify SC-012/SC-026;
2. when the core's pan is centred (`!panAppliedNonZero_`, `profundum_core.h:687`) process channel 0 and copy to channel 1;
   the Fold does the same when its block is bitwise L == R;
3. 2× for the Fold only if the model at θ 3.0 and the C++ chain gate both show aliasing margin.
Still over: stop; take the measurements to the main loop. Never relax the bar.

**Verify:** the case passes pinned; figures recorded.

---

## Group 10 — verification TUs (parallel: disjoint new files; headers read-only)

> Both tasks only read `harmonic_fold.h` and `profundum_core.h`. A failure caused by a header defect goes under "Defects
> found" (rule 11); T016 fixes it. Build once for both (`--target dsp_systems_tests`) after both TUs are written, or let
> one task build while the other writes — never two concurrent builds of the same target.

### T014 [P] Excitation descriptor gates — `profundum_core_excitation_spectral_test.cpp` (SC-001, SC-002(b)(c), SC-003, SC-011 excitation, SC-012(a)–(c), SC-020; C-2, C-6, C-9)

THD/H from `renderedHarmonicPowers`; "reference" = twin mix-0 render; windows from t_settle = 0.637 s; `requireLREqual`.
- `ProfundumCore_ExcitationDriveMonotonic` (SC-001) `[long]` + `…Smoke` (C2 only): mix 1, asymmetry 0, `kSineAnchor` with recipe
  edge ∈ {0, 1}, drive 33-point sweep at E0, E1, E2, C2: THD strictly increasing, H non-decreasing, THD(drive 1) − THD(drive 0)
  ≥ **20 dB** (plan 44.4 / 31.4 dB).
- `ProfundumCore_ExcitationMixMonotonic` (SC-001) `[long]` + `…Smoke`: drive 1, asymmetry 0, mix 33-point sweep: THD strictly
  increasing including THD(1/32) > THD(0); THD(1) − THD(0) ≥ **20 dB**. E0, E1, E2, C2.
- `ProfundumCore_EdgeDrivesExcitationRendered` (SC-002(b)): drive 0.5, mix 1, asymmetry 0, `kSineAnchor` with edge 33-point sweep,
  E1 and C2: G = THD_out ⊖ THD_ref (linear power difference, dB) strictly increasing; G(1) − G(0) ≥ **6 dB** (plan 20.7).
- `ProfundumCore_EdgeZeroStillExcites` (SC-002(c) first clause): edge 0, mix 1, drive 1, E1 and C2: THD(out) − THD(ref) ≥ **20 dB**.
- `ProfundumCore_MixIndependentOfEdge` (SC-002(c) blend clause per **C-9**): edge ∈ {0, 1} separately, drive 0.5, `kSineAnchor`,
  E1 and C2, 44.1/48/96 kHz, steady 16-period window: residual power of y(0.55) − ½(y(0.1) + y(1)) ≤ **−60 dB** re output
  power. Print the as-written y(0.5) − ½(y(0) + y(1)) residual as **SC-022(g)** (plan −53.6 … −60.5 dB).
- `ProfundumCore_ExcitationEvenHarmonicsMonotonic` (SC-003(a)): drive 0.5, mix 1, `kSineAnchor` with edge 0.5, asymmetry 33-point
  sweep at E0, E1, E2, C2: E_gen (`generatedEvenEnergyDb` vs the asymmetry-0 render) strictly increasing over the 32 non-zero
  points; E_gen(1) ≥ **−30 dB** re h1 (plan −17.6); symmetry: E_even(asymmetry 0) ≤ E_even(mix-0 ref) + **3 dB**.
- `ProfundumCore_ExcitationTriangleTail` (SC-003(b)): `kTriangleAnchor`, max excitation, E0, E1, E2, C2: odd/even ratio drops by ≥
  **20 dB** vs the reference and E_even ≥ **−30 dB** re h1 (plan −19.2).
- `ProfundumCore_ExcitationDcFree` (SC-011): (drive, asymmetry) ∈ {0.5, 1}², mix 1, `kSineAnchor` and `kSawAnchor`, E0, E1, E2:
  |mean| over ≥ 1 s integer-period window ≤ **−80 dBFS** per channel.
- `ProfundumCore_ExcitationFundamentalPreserved` (SC-012(a)–(c), **C-2** bars): (a) drive 0, mix 1, inputs `kSineAnchor`, mid grid,
  `kSawAnchor` (C-6), E0–E2 × 44.1/48/96 kHz: ||T₁| (dB)| ≤ **0.1 dB** and |∠T₁| ≤ **2°** (plan 0.06 dB / 0.13°). (b) drive ×
  asymmetry × mix ∈ {0, 0.5, 1}³ on `kSineAnchor` and `kSawAnchor`, E1 and C2: |∠T₁| ≤ **2°** at asymmetry 0 on both inputs
  and at every setting on `kSineAnchor`; on `kSawAnchor` at asymmetry > 0, |∠T₁| ≤ **20°** (plan max 15.04°). (c) mix
  33-point sweep at drive 1, asymmetry 1, E1 and C2: |T₁| never more than **0.5 dB** below min(|T₁(mix 0)|, |T₁(mix 1)|).
- `ProfundumCore_ExcitationLevelLaw` (SC-020): mix 1, drive 33-point sweep × asymmetry {0, 0.5, 1}, `kSineAnchor`, mid grid,
  `kSawAnchor`, E1 and C2: output RMS within **±1.5 dB** of reference RMS (plan worst −0.95 dB).

**Verify:** build `dsp_systems_tests` (coordinated with T015), zero warnings; per-push and `[long]` lanes run concurrently to
separate logs; all green or recorded under "Defects found".

### T015 [P] Chain TU — `profundum_edge_fold_chain_test.cpp` (SC-005, SC-010/011/012(d) core arms, SC-013(b)(c), SC-017 rate change, SC-019 core, SC-022, helper parity, listening; C-2, C-6, C-8, C-10)

Includes both `"profundum_excitation_test_helpers.h"` and `"../processors/harmonic_fold_test_helpers.h"` (downward include,
allowed) plus `<krate/dsp/processors/harmonic_fold.h>`. Chain = core → excitation → `HarmonicFold` (per channel, Fold
`setFundamentalHz` = played f₀). Windows from t_settle.
- `ProfundumEdgeFold_HelperParity` (S8.1 drift guard): for a 1 kHz sine, the SC-009-state core render (E1, 48 kHz) and the SC-005
  alias fixture, each duplicated function (`bh7Window`, `goertzelPhase`, `fftDouble`, `bh7PowerSpectrum`,
  `renderedHarmonicPowers`, `aliasFftLength`, `aliasedPower`, `goertzelPhasor`) in `HarmonicFoldTest` gives bit-identical
  results to its `ProfundumTest` original (`memcmp` on the double outputs).
- SC-005 alias gate, three fixture families: `ProfundumCore_ExcitationNoAliasing`, `HarmonicFold_NoAliasing`,
  `ProfundumEdgeFold_ChainNoAliasing`. Each spec name is the per-push smoke at E1/48 kHz; each family also has `[long]`
  siblings `…NoAliasingE0`, `…NoAliasingE1`, `…NoAliasingE2` (that note at 44.1/48/96 kHz), so the grid shards by note into
  concurrent processes.
  Fixtures: excitation at max and at (drive 1, asymmetry 0), mix 1; Fold at {30, 65, 100} fed the core with excitation
  off; chain max excitation → Fold 100. Inputs: SC-009 state (`kSawAnchor` + edge 1 + shift 1), `kGrowl`, `kSawAnchor`, mid
  grid, `kSineAnchor`. FFT length `aliasSafeFftLength`; `aliasedPower` on the last L samples after t_settle; REQUIRE
  `excludedFraction ≤ 0.5`; aliased power ≤ **−80 dBFS** per channel (plan worst −84.8 chain E2/44.1).
- `ProfundumEdgeFold_AliasGatePositiveControl` (SC-005, C-6): the SC-009-state core render (mix 0) at E1/48 kHz through a naive
  base-rate `std::tanh(3.981·x/P)` in double, same measurement: must read **> −80 dBFS** (plan −48.0).
- `HarmonicFold_TransparentAtZeroCoreInput` (SC-010 core arm): Fold amount 0 on core `kSawAnchor` and the SC-009 state at C0…C5
  × 44.1/48/96 kHz: residual ≤ **−90 dB** (expected exact).
- `HarmonicFold_DcFreeOnExcitationOutput` and `ProfundumEdgeFold_ChainDcFree` (SC-011): Fold at {30, 65, 100} on the
  max-excitation output (asymmetric), and the full chain, `kSineAnchor` and `kSawAnchor`, E0, E1, E2: DC ≤ **−80 dBFS**.
- `HarmonicFold_FundamentalPreservedCoreInput` (SC-012(d) chain arm): Fold 0…100 (41 points) on core `kSawAnchor`, E0–E2:
  |∠T₁| ≤ **5°**, no flip, |T₁| ≥ −6 dB below 65 and ≥ −12 dB from 65.
- `ProfundumCore_ExcitationMonoSum` (SC-013(b), **C-2**): pan vector +0.5 on even n, −0.5 on odd n ≥ 3, 0 on h1; max excitation
  and asymmetry 0 (drive 1, mix 1), E1 and C2, mid grid and `kSawAnchor`: inter-channel h1 phase ≤ **2°** at asymmetry 0 and
  ≤ **15°** at asymmetry 1 (plan 9.36°); h1 power of (L+R)/2 within **0.5 dB** of the mean of L and R h1 powers.
- `HarmonicFold_MonoSum` (SC-013(c) first clause, **C-10**): same pan vector. (c-i) Fold at 100 on the **mix-0** panned core, mid
  grid and saw, E1 and C2: |Δ(inter-channel h1 phase)| added by the Fold ≤ **2°** (plan worst 1.95°, R-11). (c-ii) Fold at 100
  on max excitation, asymmetry 0 and 1, mid and saw, E1 and C2: h1 power of (L+R)/2 within **0.5 dB** of
  ((|h1L| + |h1R|)/2)² (plan worst 0.16 dB). (c-iii) print inter-channel h1 phase and L/R h1 imbalance as **SC-022(h)**.
- `ProfundumEdgeFold_SampleRateChange` (SC-017): one core + one Fold, `prepare` 48 → 96 → 44.1 kHz; after each, re-run the E1
  SC-005 smoke (≤ −80 dBFS) and SC-012(a) (≤ 0.1 dB / 2°).
- `ProfundumCore_ExcitationOutputBounded` (SC-019 core + chain): SC-009 state at every MIDI note 12…60, max excitation, Fold 100,
  2 s each (`[long]`; smoke: notes 12, 28, 40, 60): `clearExcitationPreCeilingPeak()` / `clearPreCeilingPeak()` per note;
  pre-ceiling peaks strictly below 6.0 and |y| ≤ 6.0. Print worst (plan ≤ 1.42 / ≤ 2.40).
- Recorded figures (SC-022; print, `REQUIRE` finite, no threshold): `ProfundumEdgeFold_RecordedAliasingByOctave` `[long]` (a:
  aliased power per octave E2…C5 for excitation, Fold 100, chain); `ProfundumEdgeFold_RecordedAliasingRateExtremes` (b: 22.05 and
  192 kHz at E1); `ProfundumCore_ExcitationRecordedPhaseAt8Hz` (c: |T₁|, ∠T₁ at f₀ = 8 Hz, drive 0, mix 1);
  `ProfundumCore_ExcitationRecordedAsymmetryPhase` (e: ∠T₁ at max asymmetry for the five SC-005 inputs, C-2);
  `HarmonicFold_RecordedLevelNonSine` (f: Fold-100 level change on saw, mid grid, SC-009 state at C2, C-8; plan +1.70 / +2.71 /
  +6.51 dB). (d) is printed by T010, (g) by T014, (h) by `HarmonicFold_MonoSum`.
- `ProfundumEdgeFold_ListeningRenders` `[.listen]`: WAV files (16-bit or float via a local writer) to
  `build/windows-x64-release/profundum-phase2-listen/`: excitation drive, asymmetry and edge sweeps; a Fold 0 → 100 sweep on
  the six named states; the chain at maximum — at E0, E1, E2, C2. Not a gate.

**Verify:** build `dsp_systems_tests` (coordinated with T014), zero warnings; per-push lane, and the `[long]` alias shards as
three concurrent processes (`"*E0"`, `"*E1"`, `"*E2"` name filters with `[long]`), each to its own log. Green or recorded
under "Defects found". Record wall clocks for the estimate table (plan S10.4).

---

## Group 11 — defect fix-up (sequential, conditional)

### T016 Resolve every entry under "Defects found"

- For each entry: reproduce with the named case alone, diagnose against the model/evidence script cited in the plan, fix the
  owning header (`harmonic_fold.h`, `profundum_core.h` or `polyphase_halfband_oversampler.h`), rebuild once for all affected
  targets, and re-run every suite that includes the touched header concurrently (`dsp_primitives_tests "[polyphase-halfband]"`,
  `dsp_processors_tests "[profundum]"`, `dsp_systems_tests "[profundum]"`). If the fix touches anything SC-018 measures,
  re-run T013's measurement alone afterwards.
- A defect whose only fix is a bar change: stop; take the evidence to the main loop (SC-027). Never relax.
- Mark each entry RESOLVED with the fix, the re-run case names and their results. No entries: record "no-op" in "Run log".

---

## Group 12 — integration (sequential)

### T017 CMake registration audit (single task)

- Confirm `dsp/tests/CMakeLists.txt` lists exactly the 8 Phase 2 TUs from the file map in their target lists, and exactly the
  two `*_nonfinite_test.cpp` in the `-fno-fast-math` block (the perf TU absent from it); `dsp/CMakeLists.txt` lists the two
  new headers. `grep -c` each path; each must appear exactly once (twice for the two nonfinite TUs).
- Confirm every TEST_CASE named in spec SC-001…SC-026 (method column) or in this file exists:
  `grep -rhoE 'TEST_CASE\("[A-Za-z_]+' dsp/tests/unit/{primitives,processors,systems}/*{polyphase,harmonic_fold,profundum_core_excitation,profundum_edge_fold}* | sort | uniq -d`
  must print nothing (no duplicates within a binary), and a list of spec-named cases with no match must be empty (record the
  renames of deviation 6 as intentional).
- No build needed if nothing changed; if it did, rebuild the three targets in one call.

### T018 Full-suite run (concurrent lanes)

- One build: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests`, zero warnings.
- Then, **concurrently**, each to its own log in the scratchpad: the five DSP exes with `"~[.perf]~[.listen]~[long]"`; the Phase 2
  `[long]` shards (`dsp_processors_tests "[edge-fold][long]"`, `dsp_systems_tests "[edge-fold][long]"` split by the E0/E1/E2
  alias shards); `dsp_systems_tests "[profundum][long]~[edge-fold]"` (Phase 1 `[long]`, unmodified). `wait`, then `tail -5`
  each log. Every lane green.
- Shared-header proof: `git diff --stat 47e43918 -- dsp/include` lists only `polyphase_halfband_oversampler.h` (new),
  `harmonic_fold.h` (new) and `profundum_core.h`. If anything else appears, the Ruinae, Seraphis, Vorago, Innexus and Membrum
  suites must be built and run too (`ruinae_tests seraphis_tests vorago_tests innexus_tests membrum_tests`, one build, concurrent
  runs). No pluginval: no plugin code changes.
- Record every lane's wall clock and the machine's core use; note what to shard or skip next time.

**RE-EXECUTION 2026-10-11 (epoch 5, main-loop ruling):** T019 returned blocked on (A) `check-bars.js check` reading SC-026
CHANGED-UNRULED and (B) 11 clang-tidy warnings in the four Phase 2 test TUs. The main loop has ALREADY applied both
fixes: the spec Clarifications D-2 and D-3 entries now say "re-ruled" (they are main-loop rulings with figures), and the
11 warnings are fixed in `harmonic_fold_nonfinite_test.cpp` (bit_cast equality), `harmonic_fold_test.cpp` (enum
`std::uint8_t`, braced return, `yLeft`/`yRight`), `profundum_core_excitation_test.cpp` (`OnTick` by value,
`std::cmp_greater_equal`, braced return) and `profundum_edge_fold_chain_test.cpp` (`bit_cast` sameBits, if/else settle,
`Setter` by value); the two suites were rebuilt and `"[edge-fold]"` re-run green by the main loop. Do not re-edit those
files. Re-run the seven checks (clang-tidy dsp must read 0 warnings; `check-bars.js check` must pass), then
`wsl --shutdown`, and record the results in the Run log.

### T019 Footprint, bars, portability and static analysis (concurrent)

Run concurrently, each to its own log, after T018:
- `node specs/profundum-phase2-edge-fold/check-footprint.js` → PASS (SC-021, SC-026 grep, layer grep); quote the output.
- `node specs/profundum-phase2-edge-fold/check-bars.js check` → no `CHANGED-UNRULED` (SC-027); `node … check-bars.js sc023` → PASS,
  after pasting the C++ SC-007 (T010) and SC-018 (T013) figures into plan S9.7 (SC-023).
- `node specs/profundum-phase2-edge-fold/extract-phase1-ref.js` → output file unchanged (`git diff --quiet` on it) and the diff
  print (namespace lines only) quoted for SC-004(a).
- `node tools/check-portability.js` → clean (FR-046). Afterwards run `wsl --shutdown` if it booted WSL.
- `node tools/lint-apple-globals.js` → clean.
- `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` → zero findings in the Phase 2 files; fix any (rebuild +
  re-run the affected suites).

**RE-EXECUTION 2026-10-11 (epoch 5, main-loop ruling):** T020 returned blocked on `VoragoMacro_SweepAxes` (`[long]`,
Vorago) failing in the isolated CPU lane with rho 0.8667 (bar 0.9). Ruled: NOT a Phase 2 gate. The identical figure is in
the 2026-10-09 lane log before any Phase 2 change, the test file was last touched by 439e8e20, Phase 2 modifies no Vorago
file, and the Long Tests Nightly on `main` is red on 2026-10-09 and 2026-10-10 with no Profundum code on `main`. It is
reported to the user as a separate Vorago item on its own branch. SC-018 and the Phase 1 budget passed in the
2026-10-11 lane (stage cost 1.350 / 1.5, chain 1.985 / 2.5, core 0.634 / 1.0). **Do NOT re-run the CPU lane**: the
2026-10-11 run stands (the later clang-tidy fixes touch test TUs only, not the perf TU or any header). Mark this task done
with the ruling and the figures already in the Run log.

### T020 CPU lane (run ALONE, last)

- Precondition: T018/T019 finished, nothing else running, the machine idle.
- Estimate the wall clock from the previous `run-cpu-tests.js` log for `dsp_systems_tests` before launching (it runs every
  `[long]` case of that exe serially as well as the perf cases, `tools/run-cpu-tests.js:56`). If it exceeds the 2 h background
  ceiling, launch it detached with a status file.
- `node tools/run-cpu-tests.js dsp_systems_tests dsp_processors_tests dsp_primitives_tests` → green; SC-018 re-confirmed
  (≤ 1.0 % core alone, ≤ 1.5 % stages, ≤ 2.5 % chain) and Phase 1's `ProfundumCore_CpuBudget` still ≤ 1.0 %.
- If a perf case fails: confirm nothing else ran, idle the machine, re-run that suite alone once; only then treat it as a defect
  (T016 → main loop). Never relax a budget.
- Record figures and wall clock under "Run log" for comply.

---

## Run log

*(Executors append dated entries here: task id, command, wall clock, key figures, outcome.)*

### 2026-10-10 — T001 ODR sweep and evidence reproduction — PASS (no mismatch)

**ODR.** `grep -rnE "(class|struct|enum class|using) (PolyphaseHalfbandOversampler|HarmonicFold|ExcitationControls)\b" dsp plugins tests tools`
→ no output (rc 1). `grep -rn "HarmonicFoldTest\|Phase1Ref\|profundum_phase1_core_ref\|excitationDriveLaw\|edgeDriveGain" dsp plugins tests tools`
→ no output (rc 1). The nine roadmap hazard names occur in `tasks.md` only in T001's own hazard list (lines 191–193); none
is a name this task list creates.

**Evidence run.** 22 Node processes launched concurrently (32 cores), each to its own log under the session scratchpad
(`t001/<name>.log`), then `wait`. All exit 0. Per-script wall clock = log mtime − log birth time (the shell timer failed: no
`bc` in Git Bash; next time time with `node -e` or `date +%s%N` arithmetic). Scripts that print only at the end are exact;
the figure is a lower bound for any that prints early.

| Script | Wall s | Script | Wall s | Script | Wall s |
|---|---|---|---|---|---|
| `ctlphase.js` | **335.5** | `model alias` | 145.5 | `ctlsteps.js` | 119.0 |
| `model fold` | 16.2 | `validate.js 96000` | 8.5 | `foldpan2.js` | 8.4 |
| `posctl.js` | 7.1 | `validate.js 48000` | 5.3 | `validate.js 44100` | 5.0 |
| `model exc` | 5.0 | `halfband-design.js` | 4.7 | `steps.js` | 3.8 |
| `foldpan.js` | 3.7 | `mixlin.js` | 3.2 | `foldmid.js` | 1.5 |
| `staticphase.js` | 0.5 | `sc013c.js` | 0.3 | `panphase.js` | 0.2 |
| `foldmidC.js` | 0.1 | `hmargin.js` | 0.1 | `sc013step.js` | 0.1 |
| `model phase` | 0.05 | | | | |

**Total ≈ 336 s** (set by `ctlphase.js`; serial sum ≈ 674 s, so concurrency saved ≈ 50 %). Next time: the run is
`ctlphase`-bound; nothing else is worth sharding. Follow-up (≈ 0.1 s each, concurrent): `sc013step.js` with `ATTACK_MS=0.5`,
`INSTANT=1`, `PERCH=1`, `PERCH=1 INSTANT=1`, and `FS=44100`, to reproduce every row of the C-4 step table.

**Comparison with the plan (every figure matches to the printed precision):**
- S4.6 (`model fold`): THD a = 30 −33.56, 65 −19.58, 100 +4.52 dB; H(100) 17; full table rows 0/2.5/15/47.5/82.5 match;
  SC-006 rises 42.33 / 13.99 / 24.09; SC-007(a) +13.99 dB, H 1 → 3, ΔL 0.68, ΔCF −0.68; SC-007(c) median 5.49 dB.
- S9.1 (`model phase`): all 18 group-delay / h1-phase figures match.
- S9.2 (`model exc`): D_eff, THD, makeup and c for all five tabled rows match; level law mid / saw / sine match;
  SC-003(a) −46.31 / −28.34 / −22.58 / −17.58, SC-003(b) −19.19 dB (S5.5).
- S9.3 (`validate.js 44100`): every E2 cell (sine, mid, saw, SC-009) matches, incl. −10.21° / −15.04° and peaks
  0.36 / 0.64 / 0.52 / 2.26. Across 44.1 / 48 / 96 kHz the Fold-100 ∠T₁ on saw/mid/sine ≤ 1.58° (96 kHz E1 saw);
  S5.5: SC-012(a) worst 0.06 dB / 0.13°; pre-clamp peaks excitation 1.42, Fold 2.40; SC-020 worst −0.95 dB.
  `steps.js` 0.191 dB @ 99.5, min ΔTHD 0.0790 @ 65, min |T₁| −9.46; `hmargin.js` all eight quoted margins match.
- S9.4 (`model alias`): all 54 cells match to 0.1 dB; worst chain **−84.81 dBFS** (E2/44.1, SC-009); positive control
  **−48.02 dBFS**. C-6 (`posctl.js`): SC-009 −48.02 at D_eff 3.98.
- S3.2 (`halfband-design.js`): `N=8 tbw=0.05` stopband −100.6 dB including the +6.0 dB passband gain = 106.6 dB re
  passband (deviation note 3).
- C-2 (`staticphase.js`, `panphase.js`): saw −7.73 / −14.88°, mid −4.83 / −10.10°, SC-009 −60.26 / −74.99° (+17.20 dB);
  inter-channel 6.22° (saw) / 9.36° (mid) at asymmetry 1.
- C-4 (`sc013c.js`, `sc013step.js`): static shared 5.04 / 1.51 dB at 47.5 / 82.5, per-channel 6.02; step table 2 ms 0.453 dB
  (0.467 at 44.1 kHz), 0.5 ms 0.053, instant shared **0.000**, per-channel 2 ms 0.000. The ratified design
  (per-channel + instant attack, `PERCH=1 INSTANT=1`) reads 6.021 / 6.021 dB, range 6.021…6.021, excursion **0.000 dB**.
- C-9 (`mixlin.js`): as-written −58.4 / −57.8 / −57.5 / −60.4 / −53.6 / −60.5 dB; re-rule −300.9 … −311.0 dB; Edge 0 = Edge 1
  in the as-written column in every cell.
- C-10 (`foldpan.js`, `foldpan2.js`): all eight table rows match (e.g. E1 mid asym 0 −0.52 → −16.25°, −15.74°, −22.34 dB,
  −2.42, −0.023; core-only E1 mid −1.95°; worst coherent-sum −0.160 dB). Plan C-10's prose "per-channel moves every
  figure by ≤ 0.15°" holds at asymmetry 0 (the sentence's context: ≤ 0.07°) and on the mix-0 core rows (≤ 0.06°), but
  **not** at asymmetry 1 on the mid grid: added phase E1 1.57 → 1.06° (0.51°), C2 −3.57 → −3.84° (0.27°); L/R imbalance
  moves ≤ 1.81 dB. No tabled figure changes and no bar depends on it. Under the ratified per-channel detector (C-4) the
  (c-i) core-only worst is **1.89°** (E1 mid), not 1.95°: 0.11° margin to the 2° bar instead of 0.05°.
- C-11 (`foldmid.js`, `foldmidC.js`): C(0) 1.270, C(100) 1.870, rise **0.600 oct** at 44.1 and 48 kHz; |ΔL| = |ΔCF| 0.081,
  |ΔC| 0.0331, |ΔTHD| 0.267 vs 1.000; L(100) − L(0) 2.71 dB; foldmidC C(65) 1.218.
- S5.3 (`ctlphase.js`, `ctlsteps.js`): all six tabled rows match; worst planned ratio **1.038** (C2 saw drive); table
  interpolation error 0.0208 dB; down-steps ≤ 0.848.
- Not run by T001: `bench/candidate-design-bench-v2.cpp` (plan S12 step 2 asks for a pinned re-run; T001's list covers the
  Node scripts only, and the CPU figure needs an isolated machine — the 22 concurrent processes would have invalidated it).

## Defects found

*(Verification tasks append entries here: case name, output, diagnosis, owning header. T016 resolves them.)*

**D-1 (2026-10-11, Group 6 gate) `HarmonicFold_RegimeContinuityStaticSmoke` (SC-008(a)), RULED in the main loop.**
Output (`harmonic_fold_test.cpp:1124`, `CHECK(p.dC <= 0.05)`): calibration sine MIDI 28 and 36, amount
97.5 → 97.75 |ΔC| 0.0501 / 0.0501 oct, 99.75 → 100 |ΔC| 0.0750 / 0.0752 oct (bound 0.05); |ΔL|, |ΔCF| ≤ 0.19 dB and
|ΔTHD| ≤ 0.62 dB pass. Every other pair of the 43 passes. Diagnosis: plan S4.3's fold-index law θ(a) = θmax·(a − 65)/35 is
linear to the ratified endpoint θmax = 3.0 (C-11); the sine's centroid moves fastest near θmax, so the last 0.25-steps
exceed the per-step bound. The plan modelled |ΔC| only on the mid grid (0.033) and |ΔL| = |ΔCF| on the sine (0.191), never
|ΔC| on the sine. Owning header: `harmonic_fold.h` (the θ table).

**Ruling (main loop, 2026-10-11; a law change inside the ratified constant, no bar moves, θ(100) = 3.0 unchanged):**
replace the linear θ law over 65–100 by an ease-out with zero slope at 100:
`θ(a) = θmax · s(t)`, `t = (a − 65)/35`, `s(t) = 1 − (1 − t)^p`, p chosen by measurement in `edge-fold-model.js`
(candidates p ∈ {1.5, 2, 3}; smoothstep `t²(3 − 2t)` as a fallback if the steeper start at 65 breaks a bar). Choose the
smallest p for which, on the calibration sine at E0/E1/E2/C2 (44.1/48/96 kHz) AND on the mid grid at C2, every adjacent
0.25-step has |ΔC| ≤ 0.04 oct (20 % margin under the 0.05 bar) with |ΔL|, |ΔCF| ≤ 0.25 dB and |ΔTHD| ≤ 1 dB, while every
other Fold bar keeps its plan figure or better: SC-006 THD strictly increasing with min step ≥ +0.05 dB, H(100) ≥ 4·H(30),
mid-grid centroid rise ≥ 0.5 oct (plan 0.600, unchanged at the endpoint), SC-012(d) |T₁| ≥ −12 dB and ≤ 5°, aliasing
≤ −80 dBFS (the a = 100 map is unchanged so S9.4 holds), SC-013(c) and SC-025 unchanged. The soft/compression regimes, g(a),
mk, c and the detector are untouched. Then: implement the law in `harmonic_fold.h`'s θ table (prepare-time, `double`),
update plan S4.3's formula block and S9.3's figures with the measured p and the per-step table, record the ruling in
spec Clarifications, rebuild `dsp_processors_tests`, and run `"[edge-fold]"` plus `"[edge-fold][long]"` — all green with the
printed worst-step lines quoted here as the D-1 resolution. If no p meets the 0.04 margin with every bar intact, stop and
surface the measured table; do not relax any bound.

**D-1 implementation (2026-10-11, T010 epoch 5; model measured, C++ gate pending the build agent).**
`evidence/d1law.js` (new; one process per law, five concurrent, ≈ 4 s each; `edge-fold-model.js` gained `FOLD_LAW`,
default now `1.5`). The linear law reproduces the C++ failure (sine |ΔC| 0.0751 @ 100 vs C++ 0.0750 / 0.0752).
Per law, sine figures identical at E0/E1/E2/C2 × 44.1/48/96 kHz: linear |ΔC| 0.0751 FAIL; **p = 1.5 PASS** (|ΔC| 0.0319 @ 96,
|ΔL| = |ΔCF| 0.109 @ 93.25, min THD step +0.0693 @ 100, H 1 → 17, rises 42.3 / 14.0 / 24.1, min |T₁| −9.46 dB; mid C2
|ΔC| 0.0161, |ΔL| 0.072, |ΔTHD| 0.115 vs 1.000, rise 0.600 oct); p = 2 (+0.0210), p = 3 (+0.0165) and smoothstep (+0.0298)
fail the +0.05 dB minimum THD step at 100. Also under p = 1.5: SC-008(b) static 0.0055 / 0.0040 (bar 0.02); SC-013(c)
teeth 0.53 vs 6.02 dB. Implemented: `HarmonicFold::kFoldIndexEaseExponent = 1.5f` and the θ table in `computeTables`;
the test-side `staticMapModel` transcription follows the same law; plan S4.3 / S4.6 / S9.3 / C-4 table and spec
Clarifications updated. Still to quote here: the rebuilt `"[edge-fold]"` and `"[edge-fold][long]"` worst-step lines.

**D-2 (2026-10-11, Group 9 gate) `ProfundumCore_ExcitationZeroLatency` (SC-026), RULED in the main loop.**
Output (`profundum_core_excitation_test.cpp:935`): `REQUIRE(bestLag == 0)` → −4 on the mid grid at E1 (C2 reads −4 in the
model). Three fixer rounds proved the C++ matches the ratified C-5 design: everything below `kSplitHz` = 500 Hz, h1
included, is on the zero-delay path (h1 delay 0.09 samples, ∠T₁ 0.03° at E1); the band above 500 Hz takes the halfband
round trip (≈ 5 samples group delay: 4.40 at 500 Hz, 5.35 at 1 kHz, 4.95 at 4 kHz); the full-band cross-correlation is
f²-weighted, so 3–5 % of high-band power moves the argmax to −4. SC-026 as written contradicts ratified C-5 on any input
with partials above 500 Hz (the plan never modelled the lag). Owning header: none (test scope).

**Ruling (main loop, 2026-10-11; SC-026 measurement re-scoped, no design change, no bar relaxed on what the requirement
protects):** FR-008's zero latency protects the Anchor's phase lock to h1 and the attack's sample accuracy. The excitation
arm of SC-026 therefore asserts, at drive 0 / mix 1 vs the mix-0 render, E1 and C2:
(a) `kSineAnchor` (all energy below the split): full-band cross-correlation argmax lag **0** (unchanged);
(b) mid grid: h1 phase |∠T₁| ≤ **0.5°** (measured 0.03°) and the cross-correlation argmax of both renders passed through
    the same 4th-order Butterworth low-pass at 250 Hz (test-side, applied identically to both) is **0**;
(c) mid grid: the full-band argmax and the high-band group delay (Goertzel phase at 1 kHz and 4 kHz, in base samples) are
    **recorded** as SC-022(i) and bounded ≤ **8** base samples (measured −4 / 5.35 / 4.95).
`HarmonicFold_ZeroLatency` (calibration sine) is unchanged. T012: implement (b) and (c) in the test, keep (a); update spec
SC-026 (done in the main loop) and the T012 text that says "argmax is 0" for the mid grid; rebuild `dsp_systems_tests`,
run `"[edge-fold]"`; quote the printed figures here as the D-2 resolution.

**D-3 (2026-10-11, Group 9 gate, epoch 5) `ProfundumCore_ExcitationZeroLatency` (SC-026(b)), RULED in the main loop.**
Output (`profundum_core_excitation_test.cpp:1049`, `CHECK(lowLag == 0)`): mid grid C2 250 Hz-band argmax **−1**; E1 0. Printed
SC-022(i): C2 |∠T₁| 0.0736°, full-band −4, group delay 4.95 @ 981 Hz / 4.98 @ 3990 Hz; E1 |∠T₁| 0.0396°, −4, 4.98 / 4.78.
Diagnosis (main loop, `scratch sc026-lowband.js` on `evidence/lib.js ExcStage`, reproduces: 250 Hz-band peak +0.61 samples
at C2 → argmax 1 in the model's sign, −0.02 at E1): D-2's premise "only the band above 500 Hz is delayed" is a brick-wall
idealisation. The split's 2nd-order Butterworth crossover gives the linear path LP·(1 − Dos) + Dos a delay rising smoothly
from 0.09 samples at 65 Hz through 0.37 (131 Hz), 0.83 (196 Hz), 1.48 (262 Hz) to 4.5 above 600 Hz, plus a +1.5 dB hump
at 400–700 Hz; and the generated harmonics set the phase of the weak mid-grid partials (C2 per-harmonic T(n): h2 −6.8 dB /
−4.0 samples, h3 0.69, h4 0.89, h5 −2.25, h7 +13 dB / 4.6). A 250 Hz band therefore weighs h3/h4 at C2, which the ratified
design delays 0.7–0.9 samples. Owning header: none (test scope; the C++ matches ratified C-5).

**Ruling (main loop, 2026-10-11; measurement cutoff inside D-2's own scope, no design change, no bar relaxed):** the low
band of SC-026(b) is the 4th-order Butterworth low-pass at **125 Hz**, the highest cutoff where the design promises less
than half a sample (model peak −0.02 at E1 and C2); the 250 Hz-band argmax is recorded as SC-022(i). Applied by the main
loop in the test (`kLowpassHz` 125, `kRecordLowpassHz` 250), spec SC-026 / Clarifications and plan C-5. The +1.5 dB hump is
a C-5 property no bar covers, noted for Phase 9 listening. T012 epoch 6 verifies and quotes the printed figures here.

**T016 (2026-10-11) — status of D-1…D-3. No new header change was needed; the build agent still has to re-run the gates.**
- **D-1: fix in place in the owning header** (`harmonic_fold.h:87` `kFoldIndexEaseExponent = 1.5f`; `computeTables`
  `harmonic_fold.h:468, 479–481` θ = θmax·(1 − (1 − (a − 65)/35)^1.5) for a > 65, 0 otherwise, so θ(100) = θmax is unchanged).
  The law matches the ruling and the measured p = 1.5. Re-run still needed: `dsp_processors_tests "[edge-fold]"` and
  `"[edge-fold][long]"` (from the T010 epoch-5 rebuild), plus `"[profundum]"` on all three exes. Quote the worst-step lines
  here. Mark RESOLVED only when those runs are green. T013 is not affected: the θ table is computed in `prepare`, so the
  per-sample cost does not change.
- **D-2: no header fix (test scope, ruled).** The C++ matches C-5. The re-scoped SC-026 (a)/(b)/(c) is in
  `profundum_core_excitation_test.cpp` (T012). RESOLVED once `dsp_systems_tests "[edge-fold]"` is green.
- **D-3: no header fix (test scope, ruled).** `kLowpassHz = 125.0` and `kRecordLowpassHz = 250.0` are in the test
  (`profundum_core_excitation_test.cpp:981, 984`). RESOLVED once `dsp_systems_tests "[edge-fold]"` is green (T012 epoch 6).
- No bar was changed. T016 did not touch any header.

**D-4 (2026-10-11, Group 11 gate) SC-005 Fold 100 / chain at E1 / 48 kHz read −76.2 / −74.9 dBFS (bar −80). FIXED in
`harmonic_fold.h`; no bar changed. DESIGN CHANGE to plan S4.4 — CONFIRMED by the main loop 2026-10-11 (true-peak
detector, 4× per-channel, release hysteresis h = 0.005; every bar intact, SC-018 stage cost 1.350 / 1.5 measured alone);
user ratification requested in the Phase 2 report.**
- Failing: `HarmonicFold_NoAliasing` (Fold 100, SC-009 −76.17, kSawAnchor −75.99), `ProfundumEdgeFold_ChainNoAliasing`
  (SC-009 −74.87, kSawAnchor −76.74) and the same four cells in `ProfundumEdgeFold_SampleRateChange` at 48 kHz.
  The model (S9.4) reads −104.8 / −101.8 for these cells.
- Root cause: plan S4.4's premise "the windowed maximum of a periodic signal over ≥ one period is the global peak: a
  constant" is false for sampled signals whose period is not a whole number of samples (E1 at 48 kHz: 1164.95 samples).
  The sample maximum depends on where the samples land on the waveform. As the 8-segment window slid, `raw` stepped,
  the release followed it, and G was modulated at the sample-phase beat rate (about 2 Hz at E1 / 48 kHz). Those
  non-harmonic sidebands land outside the ±8-bin exclusion. The model holds env at the waveform peak, so it cannot show
  this. The SC-024(b) fixture uses integer periods only (2400 / 1200 / 600), which is why no earlier gate caught it.
- Evidence (scratch probes under F:/tmp/foldprobe, not committed; the Fold run alone at 100 on the probe input):
  - 64-partial saw at E1 / 48 kHz: env ripple 7.85e-4 and −82.65 dBFS. With the release frozen after settle: 4.9e-7 and
    −100.82 dBFS. At 44.1 kHz the ripple is 7.7e-6 (−100.97).
  - Real chain input (excited core, SC-009 state, max excitation): −82.59 dBFS live against −104.55 frozen.
  - Parabolic peak interpolation at the base rate only: the 64-partial case is fixed (1.3e-5), but the chain still read
    −79.1 dBFS and a 500-partial saw −45 dBFS. 2x interpolation leaves 1.7e-3 ripple on that saw.
  - Even 4x interpolation alone leaves about 2e-3 per-period peak variation on the excited core output, and the chain
    read −75.0 dBFS. With a release hysteresis of h = 0.002 to 0.01 on top, the chain reads −104.6 (the frozen floor).
- Fix (`harmonic_fold.h`, `advanceDetector` plus two constants):
  1. `d` is a true-peak estimate: max(|x|, parabolic-interpolated local maxima of |x| upsampled
     `kDetectorPeakOversampling` = 4x by a per-channel `detOs`).
  2. The release runs only while `raw < env·(1 − kDetectorReleaseHysteresis)`, with h = 0.005. After a real level drop,
     env settles at most h above the new peak, so G is at most 0.043 dB low. Attack is still instant.
  3. `d >= |x|`, so C-4's G·|x| ≤ P guarantee is unchanged.
- **CPU:** the per-channel 4x upsampler runs every sample, idle included. SC-018 (T013) must be re-measured alone, and
  T005 already flagged R-1.
- New tests in `harmonic_fold_test.cpp`:
  - `HarmonicFold_DetectorSteadyOnNonIntegerPeriod`: E1 at 44.1 / 48 / 96 kHz, 64-partial and 0.45·fs saws, C-3 ripple
    bar 1e-4. The old header reads 7.85e-4 at 48 kHz (probe), so the test fails without the fix.
  - `HarmonicFold_DetectorReleasesAfterLevelDrop`: a −12 → −24 dBFS step must release to within h of the new peak.
- Re-run results (2026-10-11):
  - Per-push lanes (`~[performance]~[perf]~[benchmark]~[!benchmark]~[long]`), one build, five exes run concurrently,
    zero warnings. All green: core 565 cases, primitives 1498, processors 3365, systems 1519, effects 495. The E1 / 48 kHz
    smokes now read worst Fold −98.0 and chain −91.6 dBFS (they were −76.0 and −74.9). `SampleRateChange` reads chain
    −91.6 / −88.4 / −95.9 at 48 / 96 / 44.1 kHz.
  - [long] shards, worst dBFS at 44.1 / 48 / 96 kHz:

    | Shard | 44.1 | 48 | 96 |
    |---|---|---|---|
    | `HarmonicFold_NoAliasingE0` | −101.5 | −99.2 | −92.8 |
    | `HarmonicFold_NoAliasingE1` | −99.5 | −98.0 | −93.3 |
    | `HarmonicFold_NoAliasingE2` | −97.8 | −94.9 | −92.7 |
    | `ProfundumEdgeFold_ChainNoAliasingE0` | −93.5 | −92.2 | −88.2 |
    | `ProfundumEdgeFold_ChainNoAliasingE1` | −95.9 | −91.6 | −88.4 |
    | `ProfundumEdgeFold_ChainNoAliasingE2` | −86.1 | −89.8 | **−83.1** |

  - The worst margin is now 3.1 dB (chain, E2 / 96 kHz), against the plan's 4.8 dB at E2 / 44.1. The C++ chain still
    reads about 10 dB above the model in several cells, so it is not yet clear that the detector is the only
    divergence. Recorded for comply.

### 2026-10-11 — T018 Full-suite run (concurrent lanes) — PASS, every lane green

- **Build:** one `cmake --build ... --parallel 32 --target dsp_core_tests dsp_primitives_tests dsp_processors_tests
  dsp_systems_tests dsp_effects_tests`, rc 0, 8 s (everything was up to date after the D-4 rebuild). Zero `warning C/LNK/MSB` lines.
- **Shared-header proof:** `git diff --stat 47e43918 -- dsp/include` lists only `profundum_core.h` (497+/10−).
  `git status` shows the only other `dsp/include` changes are the two new untracked headers,
  `polyphase_halfband_oversampler.h` and `harmonic_fold.h`. Nothing else is touched, so the Ruinae, Seraphis, Vorago,
  Innexus and Membrum suites are not required. No pluginval: no plugin code changed.
- **Lanes:** 37 processes started at once, one log each (session scratchpad `t018/`). Total wall clock **1034 s**. CPU was
  about 77–83 % on 32 cores during the first minute, falling to one core near the end.
  - Per-push (`"~[.perf]~[.listen]~[long]"`): core 573 cases (572 passed plus 1 `[!shouldfail]` in `sigmoid_test.cpp`,
    which is expected), 5 s; primitives 1513, 5 s; processors 3393, 241 s; systems 1536, **1033 s**; effects 499, 118 s.
  - Processors `[edge-fold][long]`, one lane per case: ThdAndCountMonotonic 290 s, FundamentalPreservedLong 251 s,
    ContinuousInAmountLong 99 s, RegimeContinuityStatic 98 s.
  - Systems `[edge-fold][long]`, one lane per case for all 17 (the E0/E1/E2 alias shards included): each finished in 5–163 s.
    The longest was ExcitationEvenHarmonicsMonotonic at 163 s.
  - Phase 1 `[profundum][long]~[edge-fold]`, one lane per case for all 11: each finished in 0–226 s. The longest was
    AudibilityGate_BodyEdgeShift at 226 s.
- **D-1 worst steps** (`HarmonicFold_RegimeContinuityStatic`, green; the ruling asks for these lines): sine MIDI 28 |dL| 0.109 dB,
  |dCF| 0.107 dB, |dC| **0.0319** oct, |dTHD| 0.679 dB; sine MIDI 36 0.109 / 0.105 / 0.0319 / 0.652; mid grid MIDI 36
  0.070 / 0.072 / 0.0156 / 0.112. These match the model (0.109 dB, 0.032 oct; mid 0.072 dB, 0.016 oct).
  `ContinuousInAmountLong` worst 0.00365 (bar 0.02). The D-2/D-3 gate `dsp_systems_tests "[edge-fold]"` is green inside the
  per-push systems lane.
- **Aliasing worst, dBFS at 44.1 / 48 / 96 kHz (bar −80):** Fold E0 −101.5/−99.2/−92.8, E1 −99.5/−98.0/−93.3,
  E2 −97.8/−94.9/−92.7. Excitation E0 −102.4/−100.0/−94.3, E1 −100.3/−99.0/−94.3, E2 −93.6/−95.2/−93.5.
  Chain E0 −93.5/−92.2/−88.2, E1 −95.9/−91.6/−88.4, E2 −86.1/−89.8/**−83.1**. The chain figures are identical to D-4's
  re-run, and the worst margin is still 3.1 dB.
- **Next time:** the per-push `dsp_systems_tests` lane alone sets the wall clock (1033 s against ≤ 290 s for every other lane).
  Shard it with Catch2 `--shard-count N --shard-index i` (for example 8 shards). That should bring the whole run to about
  5 min. The core and primitives lanes (5 s each) can stay as they are.

### 2026-10-11 — T020 CPU lane (alone, last) — SC-018 GREEN; the lane is NOT green (1 Vorago `[long]` failure, outside Phase 2)

- **Precondition:** before launch, `tasklist` showed no cmake, MSBuild, cl, clang, node, ninja, pluginval or `*_tests`
  process. Only `wslservice.exe` was resident; no `vmmem` or `wslhost`. The run log has no T019 entry, so its completion was
  taken from the workflow ordering.
- **Estimate:** the last `dsp_systems_tests` CPU lane log (2026-10-09) plus the memory note (5 h 17 min with Vorago's
  overnight soak) put the lane above the 2 h background ceiling, so it ran detached (`nohup … & disown`) with a status
  file in the session scratchpad (`t020/`). The 2026-10-09 log was saved there before the runner overwrote it.
- **Command:** `node tools/run-cpu-tests.js dsp_systems_tests dsp_processors_tests dsp_primitives_tests` (SETTLE_MS 60 s,
  pin mask from `pin-perf-cores.ps1`). Logs: `f:/tmp/cpu_dsp_{systems,processors,primitives}_tests.log`.
- **Wall clock:** **14 153 s (3 h 55 min 53 s)** in total: systems 03:22:53 → 07:09:47 (**3 h 46 min 54 s**), settle 60 s,
  processors ≈ 6 min 50 s, settle 60 s, primitives ≈ 10 s. The systems suite alone sets the wall clock.
- **Result:** processors PASS (77 cases, 91 377 assertions), primitives PASS (29 cases, 105 assertions), systems **FAIL**
  (127 cases, 126 passed; 1 397 666 assertions, 1 failed).
- **SC-018 (`ProfundumEdgeFold_CpuBudget`, `profundum_edge_fold_perf_test.cpp:216–218`), all within budget:**

  | Config | % of one core | Bar |
  |---|---|---|
  | A core, mix 0 (core alone) | **0.634** | ≤ 1.0 |
  | B core + excitation | 0.995 | — |
  | C core + Fold | 1.524 | — |
  | D core + excitation + Fold (chain) | **1.985** | ≤ 2.5 |
  | D stereo forced | 1.945 | — |
  | stage cost (D − A) | **1.350** | ≤ 1.5 |
  | stereo-forced stage cost | 1.311 | — |
  | excitation alone / Fold alone | 0.361 / 0.890 | — |

  The stage-cost margin is 0.150 points (10 %), so the D-4 detector (a 4x per-channel upsampler that runs every sample)
  still fits inside the budget.
- **Phase 1 `ProfundumCore_CpuBudget`** (`profundum_core_perf_test.cpp:155`, `REQUIRE(pct64 <= 1.0)`): 64 partials sweep
  **0.633 %**, 96 partials sweep 0.785 %, 64 partials static 0.343 %. Still ≤ 1.0 %.
- **The one failure is not Phase 2's.** `VoragoMacro_SweepAxes` (`[systems][vorago][long]`,
  `vorago_macro_test.cpp:1318`, `CHECK(rho * sign >= 0.9)`) reads Movement per-band total variation rho **0.866667**,
  endpoint 0.218562 (threshold 0.2). It is not a timing case: the 2026-10-09 CPU-lane log, taken before any Phase 2 change
  (saved as `t020/prev_cpu_dsp_systems_tests_2026-10-09.log`, line 2849), shows the identical assertion with the
  identical rho 0.8666666667. Phase 2 touches no Vorago file: `git status` shows the only modified `dsp/include` file
  is `profundum_core.h`, and the last commit to `vorago_macro_test.cpp` is `439e8e20`. Re-running the suite alone
  could not change a deterministic value, so the run was not repeated. The case is `[long]`, so per-push CI never
  runs it; only this lane and the nightly do. Who owns the fix, and whether it gates the Phase 2 close, is for the
  main loop to rule. No budget or threshold was touched.
- **Not reproduced from 2026-10-09:** the `sympathetic_resonance_test.cpp:2578` SIMD/scalar throughput failure from that
  run passed this time, consistent with it being machine-state-sensitive, as the runner's header warns.
- **Next time:** the lane takes ~3 h 47 min and is set by `dsp_systems_tests`, mostly Vorago's `[long]` soak cases.
  Re-confirming a Profundum budget only needs `dsp_systems_tests "[profundum][.perf]"`, which takes minutes. Keep the full
  lane for phase close only.

### 2026-10-11 — T019 re-execution (epoch 5) Footprint, bars, portability and static analysis — PASS, all seven checks clean

- **Run:** the seven checks were started at once, one log each (session scratchpad `t019e5/`), then `wait`. Total wall clock
  **444 s**, set by clang-tidy (32 jobs). The other checks took: check-portability 91 s, the rest ≤ 1 s each. No file was
  edited (the main loop had already applied the D-2/D-3 re-rulings and the 11 clang-tidy fixes).
- `node specs/profundum-phase2-edge-fold/check-footprint.js` → **OVERALL: PASS**. 23/23 zero-diff files pass (FR-048).
  `profundum_core.h`: 2(i) 34/34 Phase 1 declarations verbatim; 2(ii) +497 −10, removed lines only within the banner and
  member bodies; 2(iii) FTZ note verbatim. SC-026: no FIR quality/mode and no `oversampler.h` include in
  `polyphase_halfband_oversampler.h`, `harmonic_fold.h` or the Phase 2 additions to `profundum_core.h`. Layer grep: all 5 PASS.
  `git diff --stat 47e43918 -- dsp/include` → only `profundum_core.h | 507`, 1 file changed, 497 insertions(+), 10 deletions(-).
- `node … check-bars.js check` → **PASS**. SC-001…SC-025 and SC-027 read SAME. SC-026 reads `CHANGED+RULED`
  (it was CHANGED-UNRULED in the blocked run), so there is no `CHANGED-UNRULED` (SC-027).
- `node … check-bars.js sc023` → **PASS**: SC-023 table at `plan.md:1155`, 4 rows. Chosen: (a) envelope-normalised
  waveshaper (`x·G` → saturate → `/G`, β = 1 from a = 40).
- `node … extract-phase1-ref.js` → wrote `dsp/tests/unit/systems/profundum_phase1_core_ref.h` (32 429 bytes). The diff
  against `47e43918:profundum_core.h` shows only the 4-line GENERATED banner and the two namespace lines
  (`namespace Krate::DSP {` → `namespace Krate::DSP::ProfundumTest::Phase1Ref {`, and the same for the closing comment).
  This is SC-004(a). The file is untracked, so `git diff --quiet` cannot compare it with a committed copy. The generator is
  deterministic from `47e43918`, and the diff print is the evidence.
- `node tools/check-portability.js` → **all clear**, 11 TUs compiled with g++. These are the 8 Phase 2 test TUs plus 3 bench
  TUs. Afterwards `wsl --shutdown` rc 0, with no `vmmem`/`wslhost` resident.
- `node tools/lint-apple-globals.js` → **OK**: 973 files, no macOS-only name collisions.
- `pwsh tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` → 385 files analysed, **Errors: 0, Warnings: 0**.
  The 5 Phase 2 test TUs that had findings in the blocked run are in `compile_commands.json`. No rebuild was needed:
  nothing changed.
- **Next time:** clang-tidy over all of `dsp` (385 TUs) sets the wall clock. To re-confirm fixes confined to a few TUs,
  run it on those files only and keep the full `-Target dsp` pass for phase close.

### 2026-10-11 — T020 re-execution (epoch 5) CPU lane — DONE by main-loop ruling; the lane was not re-run

- **Ruling applied** (T020 re-execution note above the T020 section; spec Clarifications): `VoragoMacro_SweepAxes`
  (`[systems][vorago][long]`, rho 0.866667 against bar 0.9) is **not a Phase 2 gate**. It is reported to the user as a
  separate Vorago item. The 2026-10-11 CPU lane stands, and it was not re-run.
- **Check that the 2026-10-11 run still covers the shipped code:** every Phase 2 header and both perf TUs were last
  modified before the lane started (systems 03:22:53): `polyphase_halfband_oversampler.h` 2026-10-10 22:23:44,
  `profundum_core.h` 2026-10-10 22:24:21, `harmonic_fold.h` 2026-10-11 02:16:50, `profundum_edge_fold_perf_test.cpp`
  2026-10-10 21:53:22, `profundum_core_perf_test.cpp` 2026-10-10 11:13:57. The lane logs end at systems 07:09:47 and
  processors 07:17:36. `f:/tmp/cpu_dsp_systems_tests.log:3983` reads `test cases: 127 | 126 passed | 1 failed`.
- **Figures on record** (from the 2026-10-11 T020 entry): SC-018 core alone **0.634 %** (≤ 1.0), stage cost **1.350 %**
  (≤ 1.5), chain **1.985 %** (≤ 2.5). Phase 1 `ProfundumCore_CpuBudget` 64-partial sweep **0.633 %** (≤ 1.0). Processors
  and primitives lanes PASS. Wall clock 14 153 s. No budget or threshold was touched.

### 2026-10-11 — Comply fix: SC-018 margin (FR-054), SC-023 / FR-035 record

- **Cause:** comply's pinned re-runs read stage cost 1.76 / 1.42 / 1.55 % (bar 1.5). The fix session's baseline reproduced
  it: 1.621 / 1.444 / 1.435 %. The 10 % margin from the T020 lane (1.350 %) was not reliable.
- **Fix (`harmonic_fold.h`):** (1) at 4x the residual path reads the detector's interpolation of the same sample
  (`detBuf`) and does not run a second, identical interpolator. That interpolator stays warm through idle, as the core
  excitation's does. (2) Mono link: while both channels hold identical state and the sanitised inputs are bit-identical,
  only channel 0 runs and its output is copied. At the first differing sample channel 1 takes channel 0's state.
  (3) The detector ring max is cached when a segment completes. The output is bit-identical except for the first
  samples after engaging from idle at 4x.
- **Regression test:** `HarmonicFold_StereoDivergesAfterIdenticalInput` (each channel of an identical→different→identical
  stereo render is bit-equal to a one-channel render of its own stream, 6 amounts, host block 37).
- **Readings** (pinned, 3 runs 20 s apart, A/B, not the isolated gate): stage cost 0.848 / 0.961 / 0.899 %, chain
  1.519 / 1.587 / 1.558 %, core 0.670 / 0.626 / 0.659 %, stereo-forced stage cost 1.196 / 1.388 / 1.371 %. The full table
  is in plan.md S9.7.
- **Suites** (filter `~[performance]~[perf]~[benchmark]~[!benchmark]~[long]`; core, primitives and effects whole,
  processors 10 shards, systems 16 shards, all concurrent): all green, zero build warnings. Wall clock 664 s, set by one
  systems shard. Next time use more systems shards. Portability: all clear (11 TUs). clang-tidy on the three affected
  TUs: clean. check-footprint: PASS. check-bars sc023 / check: PASS.
- **Still owed before commit:** the isolated CPU lane on this code (`node tools/run-cpu-tests.js dsp_systems_tests`, or
  `dsp_systems_tests "[profundum][.perf]"` alone and pinned). The 2026-10-11 T020 lane predates this change.
