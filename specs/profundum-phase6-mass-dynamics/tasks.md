# Tasks: Profundum Phase 6 — Mass & Dynamics

**Spec:** `specs/profundum-phase6-mass-dynamics/spec.md` (Clarified 2026-10-17) · **Plan:** `specs/profundum-phase6-mass-dynamics/plan.md`
(S0–S16, C-1…C-13, R-1…R-11, review notes) · **Model:** `specs/profundum-phase6-mass-dynamics/mass-dynamics-model.js` · **Phase base:**
commit `02fecee1` on `synth/profundum`
**Status:** Task list (2026-10-17). The build stage has not started. Plan S1 is **not ratified** (plan status block, `plan.md:6–31`); see the
precondition below. No commit tasks: commits happen outside this workflow.

## Read this first (every executor)

### Precondition: plan S1 (C-1…C-13)

The plan marks five items ⚠ RE-RULE (C-1, C-2, C-3, C-7, C-13) and calls the rest refinements "listed so the main loop can accept it in one
pass" (`plan.md:26–27`). **These tasks build every RE-RULE item at the spec bar as written** (FR-048: an unratified build never ships a relaxed
bar) and encode every refinement exactly as the plan writes it. A ruling changes one constant and nothing else: the main loop writes the
ruling into the spec FR/SC row first, then Clarifications, then flips the constant in the task section named below (and, if the task already
ran, in the file). T001 may run before any ruling.

| Item | What the tasks build (spec-bar default) | The one constant a ruling flips | Spec row | Task(s) |
|---|---|---|---|---|
| C-1 ✅ RULED (a) | SC-003 K-weighted span ≤ 3 dB and \|ΔP\| ≤ 0.2 dB/unit gated at E2 and C3; E0 / E1 recorded; unweighted 6 dB at every note | `kGateKWeightAt = {false, false, true, true}` in `mass_law_spectral_test.cpp` (user ruling 2026-10-17) | SC-003 | T014 |
| C-2 ✅ RULED (a) | SC-008(c) 5 ms point gated at C3, recorded at E1 | `kGate5msAt = {false, true}` for {E1, C3} in `transient_shaper_test.cpp` (user ruling 2026-10-17) | SC-008(c) | T006 |
| C-3 ✅ RULED | SC-010 by the residual method (steady aliasFftLength frame from 1 s; moving windows as the residual against the ideal input × gain product; positive control); the spec-literal method may be printed beside it, not gated | `kSc010Method = Sc010Method::Residual` (user ruling 2026-10-17) in `mass_dynamics_aliasing_test.cpp` | SC-010 | T016 |
| C-7 ✅ RULED (i) | Per-interval bound from 20 ms, traversal 40 ms, no row capped (the S8.2 Fold / compression seeds return to +60 / +1); SC-004 carries a 1e-6 × range float margin | `MassLaw::kSlewBoundRefMs = 20.0f`, `kMassSweepMs = 40.0f` in `mass_law.h` (user ruling 2026-10-17) | SC-004 | T009, T014 |
| C-13 ✅ RULED | SC-007 recovery measured from the plateau end (last sample within 0.1 dB of peak GR); the Peak reading printed beside it | `kRecoveryFrom = RecoveryFrom::PlateauEnd` in `mass_compressor_test.cpp` (user ruling 2026-10-17) | SC-007 | T007 |
| C-4…C-6, C-8…C-12 | Encoded as written: `PeriodPeakDetector` (C-4, a recorded duplication of HarmonicFold's private detector), slow EF ticked /8 (C-5), HP → true-peak → one-period hold → `DynamicsProcessor` Peak detection (C-6), `systems/mass_dynamics.h` holding `BassCompressor` + `MassDynamics` (C-8), the asymmetric row form (C-9), the C-10 constants, the 32-sample grid and sample 8704 (C-11), the C-12 smoothing table | — | — | T004–T018 |
| **New (this task list)** | SC-017 runs the one case pinned through `tools/pin-perf-cores.ps1` instead of `node tools/run-cpu-tests.js dsp_systems_tests` (deviation 9) | the lane command in `cpu-lane.sh` | SC-017 "It runs in the isolated CPU lane (`node tools/run-cpu-tests.js` …)" | T018, T024 |

### Deviations from the plan made while cutting these tasks (surface them; do not silently revert)

1. **CMake registration is the first code task (T002), not the last.** Every later task is test-first and its TU must be in the build to
   fail; `dsp/tests/CMakeLists.txt` enumerates TUs (`:280–290`, `:692–708`), so an unregistered TU silently drops out. T002 creates compiling
   stubs; the last group's single CMake task (T021) audits the registration. Phase 4 / Phase 5 precedent
   (`specs/profundum-phase5-shape-character/tasks.md` deviation 1).
2. **`dsp/CMakeLists.txt` gains the four new headers** in the processors and systems header lists. Plan S14.1 says "No `dsp/CMakeLists.txt`
   change", but the lists exist and Phases 1–5 added their headers there (`dsp/CMakeLists.txt:164–175`, `:201–207`).
3. **A processors-level test helper header and its self-test TU:** `dsp/tests/unit/processors/transient_shaper_test_helpers.h` (burst and step
   fixtures, transient time, D_tr, ripple, notes) and `transient_shaper_helpers_test.cpp`. Reason: `transient_shaper_test.cpp` lives in
   `dsp_processors_tests`, and no processors TU includes a systems helper (Phase 5 deviation 3). The systems helper (T011) includes it.
4. **Probes are split.** `detail::TransientShaperProbe` lives in `dsp/tests/unit/processors/transient_shaper_probe.h`; `detail::MassDynamicsProbe`
   in `dsp/tests/unit/systems/mass_dynamics_probe.h` (plan S13.1 puts both in the systems header, which a processors TU cannot include).
   `MassDynamics` also befriends `detail::MassDynamicsProbe` (plan S6 omits it; the chain's positive and negative controls need the composed
   `BassCompressor`). Precedent: `systems/bass_character_stage.h:71, 513`.
5. **Plan S13.1's `mass_chain_test.cpp` is split into five TUs, plus one helper self-test TU,** so that each parallel task owns exactly one TU
   and a gate red localises: `mass_dynamics_test.cpp` (MassDynamics alone), `mass_chain_test.cpp` (chain: SC-014/015/018/021, FR-035),
   `mass_compressor_chain_test.cpp` (SC-006, SC-022, SC-011 compressor, SC-024(d), amount harvest), `mass_dynamics_aliasing_test.cpp`
   (SC-010), `mass_law_automation_test.cpp` (SC-002, SC-005, SC-011 Mass, SC-023 rendered), and `mass_dynamics_helpers_test.cpp`.
6. **The three calibration harvests run in the main loop at one mid-build checkpoint (T019)**, not inside executors (lane rule 2026-10-14;
   executors do not build or run the tree). Each header ships the plan's model seed between `// HARVEST-BEGIN <name>` / `// HARVEST-END <name>`
   markers; T019 writes `harvest.sh` and `paste-harvest.js` and returns **blocked** so the main loop runs them. The per-push cases that depend
   on a harvested value are named in their task sections as expected-red until T019. Plan "T010 / T020 / T030" (S8) map to the harvest
   TEST_CASEs written in T006 / T013 / T014 and run in T019.
7. **The duration table has 11 entries from the start** (S8.1 harvests 11 durations; the S4.1 seed lists 6). The five extra seeds are the
   log-log interpolation of the neighbouring model seeds (values in T006).
8. **Probe and fixture extensions the plan does not list** (each needed by a named gate or harvest): `TransientShaperProbe::setSlowAttackArgMs`
   (duration harvest); `MassDynamicsProbe::overrideAmountRow` (amount harvest); `ChainConfig::{noteOnSample, preDynamicsGainDb,
   configureDynamics, rowsOverride, unsmoothedMass}` and the per-sample `ChainTaps::compGrDb` (SC-001(b) repeat noise, ΔL_step, SC-006(c)(e),
   the law harvest, SC-005(a) positive control).
9. **SC-017 runs the single case pinned:** `pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe …/dsp_systems_tests.exe -ExeArgs
   "MassDynamics_CpuBudget"`. `node tools/run-cpu-tests.js dsp_systems_tests` (spec SC-017, plan S14.2) would also run every `[long]` case of
   the exe: its filter includes `[long]` (`tools/run-cpu-tests.js:56`), and the dsp_systems CPU lane has measured 5 h 17 min. Needs the SC-017
   row amended (precondition table, last row).
10. **Every `[long]` case has a non-long `…Smoke` sibling** so each gate exercises the path (gates exclude `[long]`). SC-003's smoke runs at E2
    and C3 only: C-1 predicts that the E0 / E1 K-weighted span fails the spec bar, and that measurement belongs to the `[long]` shards at close
    (an FR-048 report), not to a per-push gate. No bar changes: the `[long]` shards gate all four notes.
11. **SC-008(c)'s 5 ms cells are their own TEST_CASE** (`TransientShaper_DurationShortestCell`) so the C-2 predicted shortfall (E1 sine
    +30 %, plan S12.2) cannot mask a real red in the other durations.
12. **SC-004's per-interval bound carries a float allowance of `1e-6 · (hi − lo)`** instead of the plan's "+ 1 ulp" (S13.3). Under C-7 (ii)
    the bound is met with equality by construction (Depth: 0.5 · 1.6667 / 50 = 0.016667 = 1 · (32 / 48000) / 0.040), so float rounding of the
    slew step and of u can exceed one ulp of the target. The allowance is six orders below the bar; the measured excess is printed.
13. **Per-class arms of the cross-cutting SCs:** SC-012, SC-013, SC-016, SC-018 and SC-021 each have a shaper arm (T006), a compressor arm
    (T007) and a MassDynamics / chain arm (T010, T012), each its own TEST_CASE named `<Class>_<suffix>`; the spec's TEST_CASE name is used
    for the MassDynamics / chain arm.

### File map

| File | Kind | Layer / target | Created / edited by |
|---|---|---|---|
| `dsp/include/krate/dsp/processors/period_peak_detector.h` | new header (`core/db_utils.h`, `primitives/polyphase_halfband_oversampler.h`) | L2 | T002 stub; T004 |
| `dsp/include/krate/dsp/processors/transient_shaper.h` | new header (L0, L1, peer L2 `envelope_follower.h`, `period_peak_detector.h`) | L2 | T002 stub; T006 |
| `dsp/include/krate/dsp/systems/mass_dynamics.h` | new header (L0–L2 only) | L3 | T002 stub; T007 (`BassCompressor`); T010 (`MassDynamics`) |
| `dsp/include/krate/dsp/systems/mass_law.h` | new header (L0–L2 only) | L3 | T002 stub; T009 |
| `tests/test_helpers/k_weighting.h` | new test header (INTERFACE library, no CMake change: `tests/test_helpers/CMakeLists.txt:7–12`) | — | T003 |
| `dsp/tests/unit/processors/transient_shaper_test_helpers.h` | new test header | — | T005 (read-only after) |
| `dsp/tests/unit/processors/transient_shaper_probe.h` | new test header | — | T006 (read-only after) |
| `dsp/tests/unit/systems/mass_dynamics_probe.h` | new test header | — | T007; T010 appends; read-only after |
| `dsp/tests/unit/systems/mass_dynamics_test_helpers.h` | new test header | — | T011 (read-only after) |
| `dsp/tests/unit/processors/period_peak_detector_test.cpp` | new TU | `dsp_processors_tests` | T002 stub; T004 |
| `dsp/tests/unit/processors/transient_shaper_helpers_test.cpp` | new TU | `dsp_processors_tests` | T002 stub; T005 |
| `dsp/tests/unit/processors/transient_shaper_test.cpp` | new TU | `dsp_processors_tests` | T002 stub; T006 |
| `dsp/tests/unit/processors/transient_shaper_nonfinite_test.cpp` | new TU, **`-fno-fast-math`** | `dsp_processors_tests` | T002 stub; T008 |
| `dsp/tests/unit/systems/k_weighting_helper_test.cpp` | new TU | `dsp_systems_tests` | T002 stub; T003 |
| `dsp/tests/unit/systems/mass_compressor_test.cpp` | new TU | `dsp_systems_tests` | T002 stub; T007 |
| `dsp/tests/unit/systems/mass_law_test.cpp` | new TU | `dsp_systems_tests` | T002 stub; T009 |
| `dsp/tests/unit/systems/mass_dynamics_test.cpp` | new TU | `dsp_systems_tests` | T002 stub; T010 |
| `dsp/tests/unit/systems/mass_dynamics_helpers_test.cpp` | new TU | `dsp_systems_tests` | T002 stub; T011 |
| `dsp/tests/unit/systems/mass_chain_test.cpp` | new TU | `dsp_systems_tests` | T002 stub; T012 |
| `dsp/tests/unit/systems/mass_compressor_chain_test.cpp` | new TU | `dsp_systems_tests` | T002 stub; T013 |
| `dsp/tests/unit/systems/mass_law_spectral_test.cpp` | new TU (grids `[long]`) | `dsp_systems_tests` | T002 stub; T014 |
| `dsp/tests/unit/systems/mass_law_automation_test.cpp` | new TU (grids `[long]`) | `dsp_systems_tests` | T002 stub; T015 |
| `dsp/tests/unit/systems/mass_dynamics_aliasing_test.cpp` | new TU (grids `[long]`) | `dsp_systems_tests` | T002 stub; T016 |
| `dsp/tests/unit/systems/mass_dynamics_nonfinite_test.cpp` | new TU, **`-fno-fast-math`** | `dsp_systems_tests` | T002 stub; T017 |
| `dsp/tests/unit/systems/mass_dynamics_perf_test.cpp` | new TU, `[performance][.perf]`, **not** `-fno-fast-math` | `dsp_systems_tests` | T002 stub; T018 |
| `dsp/CMakeLists.txt`, `dsp/tests/CMakeLists.txt` | existing | — | T002 only (T021 audits) |
| `specs/profundum-phase6-mass-dynamics/bench/` | bench source + lane script | — | T001 |
| `specs/profundum-phase6-mass-dynamics/evidence/` | TXT/CSV written by tests; `close/` lane scripts; `run-log/<Tnnn>.txt` | — | tests; T001, T018, T019, T022–T024 |

**Untouchable (zero diff lines vs `02fecee1`, FR-044):** everything under `dsp/include/` except the four new headers — in particular
`processors/dynamics_processor.h`, `processors/envelope_follower.h`, `processors/true_peak_limiter.h`, `processors/harmonic_fold.h`,
`processors/spectral_shape_recipe.h`, `primitives/*`, `core/*`, `systems/profundum_core.h`, `systems/anchor_oscillator.h`; and every existing test
file and helper (`profundum_core_test_helpers.h`, `harmonic_fold_test_helpers.h`, `shape_character_test_helpers.h`,
`shape_character_metric_helpers.h`, `low_frequency_metrics.h`, `allocation_detector.h`, `artifact_detection.h`). If a reused header looks
unusable, STOP AND SURFACE: FR-044's append-only fallback needs a ruling and the FR-044 suites (`dsp_effects_tests`, `plugin_tests`,
`approval_tests`, `ruinae_tests`, `seraphis_tests`, `vorago_tests`, `innexus_tests`).

### Verified facts these tasks depend on (each read on 2026-10-17 in this session)

Paths relative to `dsp/include/krate/dsp/` unless stated. Re-derive every line anchor from the live file before editing.

- **`DynamicsProcessor`** (`processors/dynamics_processor.h`): `enum class DynamicsDetectionMode : uint8_t` `:38`; `kDefaultAttackMs = 10.0f` `:83`;
  `kMinSidechainHz = 20.0f`, `kMaxSidechainHz = 500.0f` `:97–98`; `void prepare(double sampleRate, size_t maxBlockSize) noexcept` `:121`; `reset()`
  `:150`; `[[nodiscard]] float processSample(float input) noexcept` `:166` — NaN → 0 `:168–170`, ±Inf → ±1e10 `:171–173`, `detectionSample =
  sidechainFilter_.process(input)` only when the sidechain is enabled `:176–179`, `currentGainReduction_ = detail::flushDenormal(-smoothedGR)`
  `:195`, and the discarded audio output costs `dbToGain(-smoothedGR)` and `dbToGain(effectiveMakeup)` `:205–211`. Setters `setThreshold`
  `:243`, `setRatio` `:251`, `setKneeWidth` `:258`, `setAttackTime` `:265`, `setReleaseTime` `:272`, `setMakeupGain` `:279`, `setAutoMakeup`
  `:285`, `setDetectionMode` `:291`, `setLookahead` `:299`, `setSidechainEnabled` `:306`, `setSidechainCutoff` (clamps to 20–500) `:312–313`.
  Getters `getThreshold` `:327`, `getRatio` `:328`, `getKneeWidth` `:329`, `getAttackTime` `:330`, `getReleaseTime` `:331`, `getMakeupGain`
  `:332`, `getDetectionMode` `:334`, `getLookahead` `:335`, `getSidechainCutoff` `:337`; `[[nodiscard]] float getCurrentGainReduction() const
  noexcept` (0 = none, negative = reduction) `:343–347`; `getLatency()` `:355`.
- **`EnvelopeFollower`** (`processors/envelope_follower.h`): `enum class DetectionMode : uint8_t` `:41`; `kMinAttackMs = 0.1f`, `kMaxAttackMs =
  500.0f` `:88–89`; `prepare(double, size_t)` `:106`; `reset()` `:128`; `[[nodiscard]] float processSample(float input) noexcept` `:164`;
  `getCurrentValue()` `:192`; `setMode` `:202`; `setAttackTime(float ms)` clamps with `std::clamp` `:220–221`; `setReleaseTime` `:227`; Peak mode
  is instant only when `attackTimeMs_ <= kMinAttackMs + 0.01f` `:335`; coefficient `constexprExp(-kTwoPi / (timeMs · 0.001 · fs))` `:355–365`
  (the time is a ~99 % settling time, τ = T / 2π).
- **`HarmonicFold`** (`processors/harmonic_fold.h`): class `:76`, `public:` `:77`, `private:` `:297`; public constants `kCalibrationPeak =
  0.3552343859f` `:83`, `kAmountSmoothMs = 10.0f` `:92`, `kDetectorHoldMs = 125.0f` `:95`, `kDetectorSegments = 8` `:96`,
  `kDetectorPeakOversampling = 4` `:100`; `prepare(double, std::size_t)` `:121`; segment length `max(1, ceil(round(hold · fs) / 8))` `:145–147`;
  `setAmount(float)` `:168`; `setFundamentalHz(float)` `:178`; `processBlock(float*, float*, std::size_t)` `:188`; `latencySamples()` `:271`;
  `detectorLevel()` `:278`; `sanitizeControl` is **private** `:331`; `advanceDetector` (true-peak estimate `:403–417`, 8-segment ring `:418–427`)
  is **private** `:402–436`.
- **HarmonicFold D-4 fixture** (`dsp/tests/unit/processors/harmonic_fold_test.cpp`): `HarmonicFold_DetectorSteadyOnNonIntegerPeriod` `:1675` — E1,
  fs {44.1, 48, 96} kHz, 64 partials and a wide saw to 0.45·fs, `y = 0.2 · Σ sin(2π h f0 n / fs) / h`, ripple bar 1e-4; `rippleOf(first, last) =
  (max − min) / mean` `:864–873`.
- **`PolyphaseHalfbandOversampler`** (`primitives/polyphase_halfband_oversampler.h`): class `:32`; `kMaxFactor = 4` `:34`; `setFactor` `:49–50`;
  `factor()` `:54`; `reset()` `:57`; `upsample(float x, float* out)` `:70`; `downsample` `:84, :100`; `stateFinite()` `:105`.
- **Smoothers** (`primitives/smoother.h`): `OnePoleSmoother` `:134`, `configure(float smoothTimeMs, float sampleRate)` `:160`, `setTarget` `:170`,
  `process()` `:197`, `snapTo` `:263`, `reset` `:275`; `SlewLimiter` `:468`, `configure(float rise, float fall, float sampleRate)` `:501`,
  `configure(float ratePerMs, float sampleRate)` `:512`, `setTarget` `:519`, `process()` `:545`, `snapTo` `:591`, `reset` `:603`. Both
  `configure` overloads take a **float** rate: cast `fs` (or `fs / 32.0`) with `static_cast<float>` (C4244).
- **`Biquad`** (`primitives/biquad.h`): `kButterworthQ` `:56`; `FilterType::Highpass` `:70`; class `:308`; `configure(...)` `:330`; `process(float)`
  `:352`; `reset()` `:385`; `BiquadCascade` `:414`, `setButterworth(FilterType, float frequency, float sampleRate)` `:427`.
- **`db_utils.h`** (`core/`): `kSilenceFloorDb = -144.0f` `:33`; `detail::isNaN` `:99`; `detail::isFinite(float)` `:118`; `detail::isInf` `:136`;
  `detail::flushDenormal` `:263`; `dbToGain(float)` `:289`.
- **Curves:** `applyModCurve(ModCurve, float)` `core/modulation_curves.h:38`; `enum class ModCurve : uint8_t { Linear = 0, Exponential = 1,
  SCurve = 2, Stepped = 3 }` `core/modulation_types.h:79–84`.
- **`SpectralShapeRecipe`** (`processors/spectral_shape_recipe.h`): `struct Controls` `:41`; `kCoreOutputRmsDb = -12.0f` `:51`; `kCenterPanGain =
  0.7071067811865476f` (a static member of the class) `:52`; `kDefaultControls` `:82`; `kSineAnchor, kTriangleAnchor, kSawAnchor` `:83` (defined
  `:231`, `:237`).
- **`ProfundumCore`** (`systems/profundum_core.h`): class `:92`; `kDefaultPartials = 64` `:96`; `kControlInterval = 32` `:97`; `prepare(double, int)`
  `:129`; `reset()` `:161`; `setControls` `:173`; `noteOn(float)` `:208`; `processBlock(float*, float*, std::size_t, const float* = nullptr)`
  `:237`; `intervalPhase_` advanced across calls `:285–286`; `deliveredGains()` `:298`.
- **`AnchorOscillator`** (`systems/anchor_oscillator.h`): class `:55`; `Waveform` `:57`; `Mode` `:58`; `kControlInterval = 32` `:62`;
  `kMaxCoreH1Amplitude` `:71`; `kDefaultLevel = 0.5f` `:72`; `kLevelSmoothMs = 5.0f` `:75`; `struct Controls` `:99`, `level` `:102`;
  `prepare(double)` `:123`; `setControls` `:224`; `setCoreH1Amplitude(float)` `:262`; `noteOn(float, bool = true)` `:276`; `processBlock` `:285`;
  `pendingControls()` `:361`. No public a₁ accessor.
- **`TruePeakLimiter`** (`processors/true_peak_limiter.h`): `kDefaultCeilingDb = -1.0f`, `kDefaultReleaseMs = 80.0f` `:46–47`; `prepare(double,
  std::size_t)` (allocates) `:59`; `reset()` `:78`; `setCeilingDb` `:85`; `setReleaseMs` `:91`; `getCeilingLinear()` `:98`; `processBlock(float*
  left, float* right, int numSamples)` `:104`.
- **`DCBlocker`** (`primitives/dc_blocker.h`): class `:94`; `prepare(double sampleRate, float cutoffHz = 10.0f)` `:135`; `reset()` `:155`;
  `process(float)` `:200`; `processBlock(float*, size_t)` `:228`.
- **Probe precedent:** `struct detail::BassCharacterStageProbe;` `systems/bass_character_stage.h:71`, its `friend` `:513`.
- **Test helpers:** `TestHelpers::AllocationScope` `tests/test_helpers/allocation_detector.h:111` latches its count in its destructor; read the
  live count inside the scope with `TestHelpers::AllocationDetector::instance().getAllocationCount()` and add a `new int` positive control
  (`dsp/tests/unit/systems/shape_character_chain_test.cpp:1027–1047`). `TestUtils::kLowFrequencyFftSize = 262144`
  `tests/test_helpers/low_frequency_metrics.h:64`. `HarmonicFoldTest` (`dsp/tests/unit/processors/harmonic_fold_test_helpers.h`): namespace `:40`,
  `aliasFftLength` `:168`, `aliasedPower` `:184`, `integerPeriodLength` `:240`, `calibrationSine(fs, f0, seconds, peak = kCalibrationPeak)` `:252`,
  `recipeAdditive(c, f0, fs, seconds, N = 64)` (−12 dBFS RMS per channel, REQUIREd) `:268`, `renderFold` `:362`, `:379`. `ShapeCharacterTest`
  (`dsp/tests/unit/systems/shape_character_test_helpers.h`): namespace `:42`, notes `kE0Hz…kC3Hz` `:48–55`, `kFixtures` `:66`, `fixture` `:74`,
  `fold100` `:80`, `Descriptors` `:93`, `describe` `:107` (REQUIREs `n >= kLowFrequencyFftSize` `:111–113`), `separate` `:171`; it includes
  `"../processors/shape_character_metric_helpers.h"` `:26`, which holds `ClickCount` `:44`, `differentialClicks` `:76`, `:137`, `steadyDeltaMax`
  `:147`, `centroidOct` `:193`, `levelDb` `:227`. `ProfundumTest` (`dsp/tests/unit/systems/profundum_core_test_helpers.h`): namespace `:47`,
  `renderCore` `:90`, `renderedHarmonicPowers` `:235`, `aliasFftLength` `:257`, `aliasedPower` `:273`, `rmsDbfs` `:453`.
- **CMake anchors:** `dsp/CMakeLists.txt`: processors list ends `include/krate/dsp/processors/rectifier.h` `:175`; systems list ends
  `include/krate/dsp/systems/bass_character_stage.h` `:207`. `dsp/tests/CMakeLists.txt`: processors Phase 5 block `:280–290` (last
  `unit/processors/rectifier_nonfinite_test.cpp` `:290`); systems Phase 5 block `:692–708` (last `unit/systems/shape_character_perf_test.cpp`
  `:708`, `)` `:709`); `-fno-fast-math` Phase 5 lines `:1198–1200`, `PROPERTIES` `:1201`; `KRATE_DSP_TESTS_DIR` defined for both exes `:798–801`.
  Evidence paths are `std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase6-mass-dynamics/evidence/<file>"`.
- **Tools:** `tools/run-cpu-tests.js:56` filter includes `[long]`, `:69` `SETTLE_MS` default 60000; `tools/pin-perf-cores.ps1:17–21` (`-Exe`,
  `-ExeArgs`); `tools/run-clang-tidy.ps1:70` `FileTimeoutSec = 600`. Phase 5 close scripts to copy the structure from:
  `specs/profundum-phase5-shape-character/evidence/close/{suites,tidy,portability,cpu-lane,harvest}.sh` and `paste-harvest.js`.
- **ODR sweep (2026-10-17, this session):** `grep -rnE "(class|struct|enum class|enum|using|namespace) (TransientShaper|MassLaw|MassDynamics|BassCompressor|PeriodPeakDetector|TransientShaperProbe|MassDynamicsProbe|BassCompressorProbe|MassLawProbe|MassChainFixture|KWeightingFilter|MassDynamicsTest|ChainConfig|ChainTaps|DurationPoint|AmountRow)\b" dsp plugins tests tools`
  → **0 hits**; no file matching `*transient_shaper*`, `*mass_law*`, `*mass_dynamics*`, `*period_peak*`, `*k_weight*`, `*mass_compressor*` or
  `*mass_chain*` exists; `kWeightedLevelDb` and `lowestSoundingF0` have 0 hits. Hazards never reused: `TransientDetector`
  (`processors/transient_detector.h:34`), `SidechainFilter`, `CharacterProcessor`, `StereoField`, `MorphEngine`, `SubOscillator`.

### Commands (main loop and gates; executors do not build or run the test tree)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests dsp_systems_tests > <log> 2>&1; tail -5 <log>
B=build/windows-x64-release/bin/Release
$B/dsp_processors_tests.exe "[transient-shaper]~[long]" 2>&1 | tail -5
$B/dsp_systems_tests.exe "[mass-dynamics]~[long]" 2>&1 | tail -5
$B/dsp_systems_tests.exe "MassLaw_LevelSlopeBounded_Saw_E0" 2>&1 | tail -5     # one [long] shard by name
```

Never `ctest -R`. A positional `"[a]~[long]"` is one AND-term; separate arguments OR. Hidden cases (`[.perf]`, `[.calibration]`) run only when
named. Capture any run longer than ~30 s to a log and read the log; never re-run a slow suite for its output. One build of a tree at a time.

### Rules for every task

1. **Failing test first:** write the test cases before the implementation in the same task. Do NOT build or run the test tree (the group's gate
   does). Re-read every file you wrote and fix anything that obviously would not compile. Zero warnings is a gate criterion (MSVC C4244 /
   C4267 / C4100: `f` suffixes, explicit casts, `[[maybe_unused]]`).
2. Header-only, `namespace Krate::DSP {` (nested form). Banner comment: layer, "Profundum Phase 6 (specs/profundum-phase6-mass-dynamics)", the
   FRs / SCs it serves. Includes point **downward only**: the two L2 headers include L0, L1 and peer L2 only; `mass_dynamics.h` and `mass_law.h`
   include L0–L2 only and **never** an L3 header (no `profundum_core.h`, no `anchor_oscillator.h`; plan S11). New namespace-scope names: exactly
   `PeriodPeakDetector`, `TransientShaper`, `BassCompressor`, `MassDynamics`, `MassLaw`, and the forward-declared `detail::TransientShaperProbe`,
   `detail::MassDynamicsProbe`. Everything else is nested.
3. PascalCase classes, camelCase functions, `trailing_` members, `kPascalCase` constants. Designated initialisers in every aggregate brace init
   (`DurationPoint`, `AmountRow`, `Row`, `Targets`); every double → float via `static_cast`; no narrowing. Finiteness only via
   `detail::isFinite`; never `std::isnan` / `std::isfinite`; bit comparisons via `std::bit_cast` / `std::memcmp`. Never hand a non-finite value
   to `std::clamp`, `OnePoleSmoother::setTarget` or `SlewLimiter::setTarget` (the smoothers zero their state on NaN).
4. Audio-thread methods (`processBlock`, every setter, `reset`, `MassLaw::advanceInterval` / `evaluate`) are `noexcept` and allocation-, lock-,
   exception- and I/O-free; fixed-size members only (`std::array`); no `static` mutable data; no SIMD; no `std::function` in product code.
   `DynamicsProcessor::setLookahead` is only ever called with `0.0f` (FR-014; `> 0` allocates). Setters sanitise: a non-finite argument keeps
   the previous value, then clamp (one exception: `setDetectorKeyHz`, where a non-finite or non-positive key means "no voice", FR-018). Before `prepare`, setters store the sanitised pending value and `prepare` applies it; `processBlock` before
   `prepare`, with a null channel or with 0 samples, is a no-op that leaves the buffers untouched and advances no phase (FR-047).
5. Test helpers are `inline` functions in namespaces `Krate::DSP::TransientShaperTest` (processors helper), `Krate::DSP::MassDynamicsTest`
   (systems helper) and `Krate::DSP::TestUtils` (`k_weighting.h`); never an anonymous namespace in a header. TU-local helpers go in an anonymous
   namespace with TU-specific prefixes. When a TU uses two helper namespaces that define same-named functions (`HarmonicFoldTest` and
   `ProfundumTest` both define `aliasedPower` and `aliasFftLength`), qualify every call.
6. Tags: processors `[processors][profundum][transient-shaper]` and `[processors][profundum][period-peak]`; systems
   `[systems][profundum][mass-dynamics]` (shaper / compressor / chain) and `[systems][profundum][mass-law]` (law); K-weighting
   `[systems][profundum][k-weighting]`; non-finite TUs add `[nonfinite]`; perf `[systems][profundum][mass-dynamics][performance][.perf]`; harvests
   `[.calibration]`. `[long]` only on the cases named `[long]` below, each with its `…Smoke` sibling. **Never `[long]`:** NaN/Inf guards,
   L == R identity, bit-identity, RT safety, lifecycle and state cases.
7. No bit-exact float goldens. `memcmp` only between two renders of the same build in the same process.
8. Notes (compute 440·2^((m − 69)/12) in double, cast once): E0 = MIDI 16 = 20.60172 Hz, E1 = 28 = 41.20344, E2 = 40 = 82.40689, C3 = 48 =
   130.81278. P = `HarmonicFold::kCalibrationPeak` (the core's −12 dBFS RMS sine peak). fs = 48 kHz and host block 512 unless a case says
   otherwise.
9. **Fixtures.** Sine = `HarmonicFoldTest::calibrationSine(fs, f0, s)`; kSawAnchor = `HarmonicFoldTest::recipeAdditive(SpectralShapeRecipe::kSawAnchor,
   f0, fs, s)`; both at −12 dBFS RMS per channel and both start at a zero crossing (sample 0 is 0). Stereo stages get L == R copies unless the
   case says otherwise. Descriptor renders hold `describe`'s full frame: 1 s settle + `kLowFrequencyFftSize` samples (`descriptorSeconds(fs)`,
   T011). Every descriptor and measured value is printed with `std::setprecision(6)`.
10. Evidence: tests write TXT/CSV under `specs/profundum-phase6-mass-dynamics/evidence/` via `KRATE_DSP_TESTS_DIR` (fact list above); create
    directories with `std::filesystem::create_directories`, outside any `AllocationScope`.
11. If a test you write exposes a defect in a file another task owns, do not edit that file: add a `DEFECT: <case> <expected/observed>
    <diagnosis> <owning file>` line to your run-log. T020 resolves them.
12. Never relax a threshold or shrink a workload to pass (FR-045, FR-048). A bar proven unreachable is STOP AND SURFACE (status "blocked",
    measured table in notes).
13. Write `specs/profundum-phase6-mass-dynamics/evidence/run-log/<Tnnn>.txt` for your task: files written, every decision taken, any deviation,
    any `DEFECT:` line.
14. **Lane rule (2026-10-14):** suites, `[long]` shards, clang-tidy sweeps, CPU/perf lanes and harvests are main-loop work. A task that owns one
    writes the lane script under `specs/profundum-phase6-mass-dynamics/evidence/close/` and returns done with notes "deferred to the main loop
    at close: run <script>" (or "blocked" where its section says STOP AND SURFACE).
15. Every test that relies on a plan C-item cites it in a comment (`// plan S1 C-6`); every SC test cites its SC id in a comment above the
    TEST_CASE.

---

## Group 0 — CPU feasibility bench (alone; STOP AND SURFACE)

### T001 Microbench the Mass dynamics cost before any header is written (plan R-1, S12.4, S16 step 1)

**Files:** `specs/profundum-phase6-mass-dynamics/bench/mass-dynamics-bench.cpp`, `specs/profundum-phase6-mass-dynamics/bench/run-bench.sh`,
`specs/profundum-phase6-mass-dynamics/evidence/model-2026-10-17.txt`, `specs/profundum-phase6-mass-dynamics/evidence/run-log/T001.txt`.

- **Precondition:** nothing else running (no build, suite, model, other agent).
- **Model evidence (executor, ≈ 20 s):** run `node specs/profundum-phase6-mass-dynamics/mass-dynamics-model.js all` once and save its full
  stdout to `evidence/model-2026-10-17.txt` (plan S8.4). Sizing evidence, not compliance evidence.
- **Create `mass-dynamics-bench.cpp`.** It includes the **real** reused headers (`-Idsp/include`: `processors/dynamics_processor.h`,
  `processors/envelope_follower.h`, `primitives/polyphase_halfband_oversampler.h`, `primitives/biquad.h`, `primitives/smoother.h`,
  `primitives/dc_blocker.h`, `processors/true_peak_limiter.h`, `core/db_utils.h`, `core/modulation_curves.h`) and prototypes the new pieces
  locally, in an anonymous namespace with a `Bench` prefix, exactly as plan S3–S7 specify: `BenchPeriodPeak` (4× `PolyphaseHalfbandOversampler`
  true-peak estimate + 8-segment ring, the port of `harmonic_fold.h:403–427`); `BenchShaper` (S4.3 steps 1–7: sanitise, held detector, fast EF
  Amplitude 0.5 / 40 ms per sample, slow EF ticked /8 with linear interpolation, c = clamp(gainToDb(f / s) / 12, 0, 1), two 10 ms
  `OnePoleSmoother`s, one `dbToGain`); `BenchCompressor` (S5.5: sanitise, per-channel `Biquad` HP at 2·f₀, held true-peak max, one
  `DynamicsProcessor` in Peak mode with the sidechain off, gain from `getCurrentGainReduction()` + a 5 ms makeup smoother, the S5.4 control
  step every 32 samples including the integer release-rule ring and `setReleaseTime` only when changed); `BenchMassLaw` (six rows,
  `applyModCurve`, one `SlewLimiter` step per 32 samples).
- **Cells** (48 kHz, host block 64, stereo **distinct** L / R: a 64-partial 1/n saw at E1, −12 dBFS RMS, R offset by 0.37 period — the SC-017
  setup of plan S13.3): (1) shaper alone, A = +12 / S = −6 dB, duration 40 ms; (2) compressor alone, the amount-1 row of the S5.1 seed, key E1;
  (3) **Total** = shaper → compressor + one `BenchMassLaw` step and six-row evaluation per 32 samples; (4) `DCBlocker` ×2 + `TruePeakLimiter`
  (recorded for Phase 9); (5) the linked cell (L == R, one channel processed and copied; recorded only, see the note below); (6) one second of
  digital silence through cell 3 (R-8, recorded).
- Report per cell ns per stereo sample and % of one core = ns · 48000 / 1e7. Compile-time variants (R-1 fallbacks 2 and 3): `-DP6_DYN_TICK4`
  (the `DynamicsProcessor` is prepared at fs / 4 and fed the held value once every 4 samples, its GR held between ticks) and `-DP6_TP2X` (both
  true-peak estimators at 2× instead of 4×). A `--quick` flag runs each cell for 0.2 s.
- **Note on R-1 fallback (1)** (a mono-link fast path): the SC-017 cell as written is a distinct L / R pair, so a mono-link path cannot lower
  the gated figure. It is measured as cell 5 and recorded. Say so in the run-log.
- **Create `run-bench.sh`** (Git Bash): compile with `"/c/Program Files/LLVM/bin/clang++.exe" -std=c++20 -O2 -ffast-math -march=x86-64-v2
  -DNDEBUG -Idsp/include <bench.cpp> -o <scratch>/p6-bench.exe` (and the two variant binaries); refuse to start while `tasklist | grep -ciE
  "cl.exe|dsp_.*_tests.exe|clang-tidy"` is non-zero; run the base binary **3 times, 60 s apart**, each through `pwsh -NoProfile -File
  tools/pin-perf-cores.ps1 -Exe <scratch>/p6-bench.exe`; print the median per cell and the decision line; append everything to
  `evidence/run-log/T001-bench.txt`.
- **Executor (allowed in a few minutes):** run the model (above), write both files, compile the base binary once and run it with `--quick` to
  prove every cell prints (no timing decision from this run). Record the compile line and the quick output in `evidence/run-log/T001.txt`.
  Then **return status "blocked"** — STOP AND SURFACE: "main loop runs `bash specs/profundum-phase6-mass-dynamics/bench/run-bench.sh` alone
  (≈ 4 min), applies the decision rule, records it in `evidence/run-log/T001.txt` and as a 'Measured' paragraph in plan S12.4, then resumes the
  build with `unblock: [T001]`".
- **Decision rule (main loop):** median Total (cell 3) ≤ 1.0 % → proceed with the plan's design. Total > 1.0 % → re-run with `-DP6_DYN_TICK4`,
  then additionally `-DP6_TP2X`; the first variant ≤ 1.0 % becomes **REQUIRED** in T006 / T007 (write it into those task sections and plan
  S4 / S5 first; `-DP6_TP2X` must then be re-checked against SC-009 / SC-010). Still > 1.0 % → return to the user with the table. Relaxing the
  bar (FR-045) is never the lever.

---

## Group 1 — CMake registration with compiling stubs (sequential, single task)

### T002 Register every Phase 6 file (deviations 1, 2, 5)

**Files:** the four new headers and the sixteen new TUs of the file map (as stubs), `dsp/CMakeLists.txt`, `dsp/tests/CMakeLists.txt`.

- **Create stub headers**, each `#pragma once`, a banner (layer, "Profundum Phase 6 (specs/profundum-phase6-mass-dynamics)", the FR / SC rows
  it will carry) and an empty `namespace Krate::DSP {}`: `processors/period_peak_detector.h` and `processors/transient_shaper.h` ("Layer 2: DSP
  Processor"); `systems/mass_dynamics.h` and `systems/mass_law.h` ("Layer 3: System").
- **Create stub TUs** (a file comment naming the SCs it will hold and the task that fills it, `#include <catch2/catch_test_macros.hpp>` and the
  header(s) under test, no TEST_CASE): processors `period_peak_detector_test.cpp`, `transient_shaper_helpers_test.cpp`,
  `transient_shaper_test.cpp`, `transient_shaper_nonfinite_test.cpp`; systems `k_weighting_helper_test.cpp`, `mass_compressor_test.cpp`,
  `mass_law_test.cpp`, `mass_dynamics_test.cpp`, `mass_dynamics_helpers_test.cpp`, `mass_chain_test.cpp`, `mass_compressor_chain_test.cpp`,
  `mass_law_spectral_test.cpp`, `mass_law_automation_test.cpp`, `mass_dynamics_aliasing_test.cpp`, `mass_dynamics_nonfinite_test.cpp`,
  `mass_dynamics_perf_test.cpp`. The systems stubs include all four new headers plus `<krate/dsp/systems/profundum_core.h>`,
  `<krate/dsp/systems/anchor_oscillator.h>`, `<krate/dsp/processors/harmonic_fold.h>`, `<krate/dsp/processors/true_peak_limiter.h>` and
  `<krate/dsp/primitives/dc_blocker.h>`, proving they coexist.
- **Edit `dsp/CMakeLists.txt`:** after `include/krate/dsp/processors/rectifier.h` (`:175`) add `# Profundum Phase 6
  (specs/profundum-phase6-mass-dynamics)`, `include/krate/dsp/processors/period_peak_detector.h`, `include/krate/dsp/processors/transient_shaper.h`;
  after `include/krate/dsp/systems/bass_character_stage.h` (`:207`) the same comment, `include/krate/dsp/systems/mass_dynamics.h`,
  `include/krate/dsp/systems/mass_law.h`.
- **Edit `dsp/tests/CMakeLists.txt`**, each insertion preceded by a "Profundum Phase 6 (specs/profundum-phase6-mass-dynamics) - ENUMERATED, not
  globbed:" comment block mapping TU → SCs (the Phase 5 block style at `:280–290`):
  - `dsp_processors_tests`: after `unit/processors/rectifier_nonfinite_test.cpp` (`:290`): the four processors TUs (`unit/processors/`).
  - `dsp_systems_tests`: after `unit/systems/shape_character_perf_test.cpp` (`:708`), before `)` (`:709`): the twelve systems TUs (`unit/systems/`).
  - `-fno-fast-math` block: after `unit/systems/shape_character_nonfinite_test.cpp` (`:1200`), before `PROPERTIES` (`:1201`):
    `# Profundum Phase 6: injects NaN/Inf bit patterns (SC-019). The perf TU stays out (shipping FP mode).`,
    `unit/processors/transient_shaper_nonfinite_test.cpp`, `unit/systems/mass_dynamics_nonfinite_test.cpp`. Nothing else.
- **Verify (executor, read-only):** re-read both CMake files around each insertion; `git diff --stat` shows only the two CMake files plus new
  files. The Group 1 gate builds `dsp_processors_tests dsp_systems_tests` (it reconfigures automatically) and runs them; the Phase 1–5 case
  counts must be unchanged.

---

## Group 2 — K-weighting, `PeriodPeakDetector`, processors test helpers (parallel; disjoint new files)

### T003 [P] BS.1770 K-weighting test helper and its reference test (FR-050; SC-026; plan S13.1)

**Files:** `tests/test_helpers/k_weighting.h` (new), `dsp/tests/unit/systems/k_weighting_helper_test.cpp` (fill the T002 stub). Target
`dsp_systems_tests`.

- **Failing test first** — `KWeighting_ReferenceLevels` `[systems][profundum][k-weighting]` (SC-026):
  1. `kWeightingCoefficients(48000.0)` matches the published BS.1770-4 48 kHz tables within **1e-6** absolute per coefficient: stage 1
     b = {1.53512485958697, −2.69169618940638, 1.19839281085285}, a = {1, −1.69065929318241, 0.73248077421585}; stage 2 b = {1, −2, 1},
     a = {1, −1.99004745483398, 0.99007225036621}.
  2. A full-scale 997 Hz sine (amplitude 1.0, generated in double, 10 s at 48 kHz), `kWeightedLevelDb(x, n, 48000.0, round(1.0 · fs))` =
     **−3.01 ± 0.1** LKFS (model −3.010, plan S12.1).
  3. An E1 sine (41.20344 Hz) of the same amplitude reads at least **1 dB** lower than (2) (model −5.32 dB re 997 Hz).
  4. At 44.1 kHz and 96 kHz the 997 Hz reading is within **0.1 dB** of the 48 kHz reading (the coefficient derivation works at any rate).
  Print every reading (`std::setprecision(6)`).
- **Implement `k_weighting.h`** (namespace `Krate::DSP::TestUtils`, test-side only, no product code; FR-050): `struct KWeightingCoefficients
  { std::array<double, 3> b1, a1, b2, a2; };` (a[0] = 1); `inline KWeightingCoefficients kWeightingCoefficients(double fs)` derived from the
  Recommendation's analog prototype for any fs: shelf f0 = 1681.974450955533 Hz, G = 3.999843853973347 dB, Q = 0.7071752369554196, with
  Vh = 10^(G/20), Vb = Vh^0.4996667741545416, K = tan(π f0 / fs), a0 = 1 + K/Q + K², b = {(Vh + Vb·K/Q + K²)/a0, 2(K² − Vh)/a0,
  (Vh − Vb·K/Q + K²)/a0}, a = {1, 2(K² − 1)/a0, (1 − K/Q + K²)/a0}; RLB high-pass f0 = 38.13547087602444 Hz, Q = 0.5003270373238773,
  K = tan(π f0 / fs), b = {1, −2, 1}, a = {1, 2(K² − 1)/(1 + K/Q + K²), (1 − K/Q + K²)/(1 + K/Q + K²)}. `class KWeightingFilter` (double,
  transposed direct form II, `prepare(double fs)`, `reset()`, `double process(double x)`). `inline double kWeightedLevelDb(const float* x,
  std::size_t n, double fs, std::size_t measureFrom = 0)`: runs a fresh filter over x[0, n), mean square over [measureFrom, n), returns
  −0.691 + 10·log10(mean square) (−144 for zero). REQUIRE `measureFrom < n`. Header comment: "test-side helper for spec FR-050 / SC-003;
  K-weighting rolls off below about 38 Hz and under-reads sub-level drift (spec Descriptor definitions)".
- **Verify:** `dsp_systems_tests` `"KWeighting_ReferenceLevels"`. Run-log `evidence/run-log/T003.txt`.

### T004 [P] `PeriodPeakDetector` (L2; plan C-4, S3, R-11)

**Files:** `dsp/include/krate/dsp/processors/period_peak_detector.h` (fill the T002 stub), `dsp/tests/unit/processors/period_peak_detector_test.cpp`
(fill the stub). Target `dsp_processors_tests`.

- **Failing tests first** (`[processors][profundum][period-peak]`; include `harmonic_fold_test_helpers.h` and `<krate/dsp/processors/harmonic_fold.h>`):
  1. `PeriodPeakDetector_ConstantsMatchHarmonicFold` (R-11): `static_assert(PeriodPeakDetector::kSegments == HarmonicFold::kDetectorSegments)`,
     `kPeakOversampling == HarmonicFold::kDetectorPeakOversampling`, `kMaxHoldMs == HarmonicFold::kDetectorHoldMs`; `kMinHoldMs == 1.0f`;
     `kMaxChannels == 2`.
  2. `PeriodPeakDetector_HeldBoundsInput`: on a 3 s kSawAnchor E1 and on a 3 s E0 sine (hold 50 ms), one channel: `held() >= |x[n]|` at every
     sample; two channels (R = the same tone ×0.5 delayed 100 samples): `process(xL, xR) >= max(|xL|, |xR|)` at every sample.
  3. `PeriodPeakDetector_SteadyOnNonIntegerPeriod` (the HarmonicFold D-4 fixture, `harmonic_fold_test.cpp:1675`): E1, fs {44.1, 48, 96} kHz,
     64 partials and a wide saw to 0.45·fs, `y = 0.2 · Σ sin(2π h f0 n / fs) / h` in double; hold 50 ms; after a 1 s settle, the held value's
     ripple `(max − min) / mean` over 2 s is **≤ 1e-4** on the 64-partial arm and **≤ 2.3e-3** (0.02 dB peak-to-peak) on the wide arm
     (RULED 2026-10-17, Group 2 gate: the D-4 1e-4 is a follower reading; spec FR-004 row). Also print (record, not gated) the ripple of kSawAnchor at E0, E1, E2, C3
     with the hold at exactly one period (1000 / f0 ms), the compressor's C-6 configuration.
  4. `PeriodPeakDetector_HoldLengthAndBoundary`: at 48 kHz, hold 10 ms (W = 480, segLen = 60): a single 1.0 impulse followed by zeros keeps
     `held() == 1.0f` for between **480 and 540** samples, then 0. `setHoldMs(20.0f)` issued mid-segment, then an impulse issued after the next
     segment boundary: held for between **960 and 1080** samples. `holdMs()` returns 20 immediately after the call.
  5. `PeriodPeakDetector_SettersAndLifecycle`: `setHoldMs(NaN / +Inf / −Inf)` keeps the previous value; `setHoldMs(0.1f)` → 1, `(500.0f)` → 125;
     `reset()` gives `held() == 0.0f` and `stateFinite()`; `prepare` and every call above run inside an `AllocationScope` with a live count of 0
     (and the `new int` positive control).
- **Implement** exactly plan S3: includes `core/db_utils.h` and `primitives/polyphase_halfband_oversampler.h` plus `<algorithm>`, `<array>`,
  `<cmath>`, `<cstddef>`. `class PeriodPeakDetector` with the S3 constants, `prepare(double sampleRate, float holdMs)` (allocation-free; each
  estimator's `os.setFactor(kPeakOversampling)`; sets segLen; `reset()`), `reset()`, `setHoldMs(float)` (non-finite keeps previous, clamp
  [1, 125], the new segLen pending until the next segment completes), `float process(float x)` and `float process(float xL, float xR)`, `held()`,
  `holdMs()`, `stateFinite()`. `truePeak` is a line-for-line port of `harmonic_fold.h:403–417` (d = |x|; upsample; per phase max of |u| and, at
  a local maximum, the parabolic vertex `b − 0.25 (a − d0) · offset` with `offset = 0.5 (a − d0) / (a − 2b + d0)`); `pushHeld` ports
  `:418–427` with `segLen_ = pendingSegLen_` taken when a segment completes and output `max(segMax_, ringMax_)`. segLen =
  `max(1, ceil(round(hold · fs) / 8))` (`harmonic_fold.h:145–147`). No follower here. Comment: "Recorded duplication of HarmonicFold's private
  detector (plan S1 C-4); a follow-up proposes HarmonicFold compose this class. HarmonicFold is not modified in Phase 6 (FR-044)." Inputs must be
  finite (callers sanitise); FTZ / DAZ is a precondition (the oversampler is unflushed).
- **Verify:** `dsp_processors_tests` `"[period-peak]"`. Run-log `evidence/run-log/T004.txt`.

### T005 [P] Processors-level transient test helpers (deviation 3)

**Files:** `dsp/tests/unit/processors/transient_shaper_test_helpers.h` (new), `dsp/tests/unit/processors/transient_shaper_helpers_test.cpp`
(fill the stub). Target `dsp_processors_tests`.

- **Failing tests first** (`[processors][profundum][transient-shaper]`):
  1. `TransientShaperHelpers_BurstFixture`: sine and saw at E1 and C3, 48 kHz, preRoll 0 and 2 s: the pre-roll samples are exactly 0.0f;
     `burst[onset] == 0.0f`; the raised-cosine onset is round(0.5 ms · fs) = 24 samples; the RMS over whole periods in
     [onset + 0.1 s, onset + hold] is **−12 ± 0.1 dBFS**; the length is preRoll + hold + tail samples.
  2. `TransientShaperHelpers_TwoLevelStep`: `twoLevelStep(Saw, E1, 48 kHz, 2.0, 2.0)`: whole-period level of [0.5 s, 2 s] = **−24 ± 0.1** dBFS
     and of [2.5 s, 4 s] = **−12 ± 0.1** dBFS.
  3. `TransientShaperHelpers_TransientTime`: a synthetic trace (0 dB before the onset; from the onset 12·exp(−t/20 ms) dB) gives
     `transientTimeMs` = 20 · ln(12/9) = **5.7536 ms within one sample period**; a trace that never falls 3 dB returns `+∞`.
  4. `TransientShaperHelpers_DTr`: y = 2·x over the first 100 ms after the onset and x after (x a steady E1 sine, 1.5 s), window 375 ms: `dTrDb`
     = 10·log10((0.1 · 4 + 0.275) / 0.375) = **2.553 ± 0.05 dB**; `dTrWindow(48000, 250)` = 18000 samples, `dTrWindow(48000, 20)` = 2400.
  5. `TransientShaperHelpers_SideMidAndBits`: `sideMidRatioDb` of (L, 0.5·L) = 10·log10(0.0625 / 0.5625) = −9.542 ± 1e-3 dB; `isFiniteBits`
     is false for the NaN / ±Inf bit patterns (`std::bit_cast<float>(0x7FC00000u)`, `0x7F800000u`, `0xFF800000u`) and true for 0, ±1e-30, ±3e38.
- **Implement** (namespace `Krate::DSP::TransientShaperTest`, all `inline`; includes `harmonic_fold_test_helpers.h`,
  `<krate/dsp/processors/spectral_shape_recipe.h>`, `<krate/dsp/processors/harmonic_fold.h>`): `noteHz(int midi)` and `kE0Hz`, `kE1Hz`,
  `kE2Hz`, `kC3Hz` (doubles, rule 8); `enum class Tone { Sine, Saw };` `steadyTone(Tone, f0, fs, seconds)` (= `HarmonicFoldTest::calibrationSine`
  or `HarmonicFoldTest::recipeAdditive(SpectralShapeRecipe::kSawAnchor, …)`); `burst(Tone, f0, fs, holdSeconds = 1.0, preRollSeconds = 0.0,
  tailSeconds = 0.25)` (pre-roll zeros, then the tone from its sample 0 — a zero crossing — weighted by a 0.5 ms raised cosine, held, then
  zeros); `onsetIndex(fs, preRollSeconds)`; `twoLevelStep(Tone, f0, fs, lowSeconds, highSeconds)` (the tone ×10^(−12/20) for `lowSeconds`, then
  ×1, joined by a 0.5 ms raised-cosine gain crossfade starting at round(lowSeconds · fs)); `transientTimeMs(gainDb, onset, fs, searchMs = 50)`
  (peak = max over [onset, onset + searchMs]; time = the first sample after the peak index with gain ≤ peak − 3 dB, minus the onset; `+∞` if
  none); `dTrWindow(fs, maxDurationMs)` = round(max(0.05, 1.5 · maxDurationMs / 1000) · fs); `dTrDb(y, n, onset, steadyStart, window)` =
  10·log10(energy [onset, onset + W) / energy [steadyStart, steadyStart + W)); `rippleDb(gainDb, begin, end)` = max − min;
  `meanOf(v, begin, end)`; `levelDbPeriods(y, n, fs, f0)` (RMS dBFS over `HarmonicFoldTest::integerPeriodLength(n, fs, f0, 0.0)` samples);
  `sideMidRatioDb(L, R, n)`; `isFiniteBits(float)` (exponent bits not all ones, via `std::bit_cast<std::uint32_t>`).
- **Verify:** `dsp_processors_tests` `"TransientShaperHelpers_*"`. Run-log `evidence/run-log/T005.txt`.

---

## Group 3 — `TransientShaper` and `BassCompressor` (parallel; disjoint new files)

### T006 [P] `TransientShaper` complete, its probe and its tests (FR-001–FR-009, FR-040–FR-042, FR-047, FR-049; SC-008, SC-009, SC-011 shaper, SC-012/013/016/018/021 shaper arms, SC-025; plan S4, C-5, C-10, S8.1)

**Files:** `dsp/include/krate/dsp/processors/transient_shaper.h` (fill the stub), `dsp/tests/unit/processors/transient_shaper_probe.h` (new),
`dsp/tests/unit/processors/transient_shaper_test.cpp` (fill the stub). Target `dsp_processors_tests`. Depends on T004, T005.

**Header (plan S4.1–S4.3 exactly).** Includes `core/db_utils.h`, `primitives/smoother.h`, `processors/envelope_follower.h`,
`processors/period_peak_detector.h`, `<array>`, `<cmath>`, `<cstddef>`. `namespace detail { struct TransientShaperProbe; }`. Constants
`kMinGainDb = -12`, `kMaxGainDb = 12`, `kMinDurationMs = 5`, `kMaxDurationMs = 250`, `kDefaultAttackDb = 6`, `kDefaultSustainDb = 0`,
`kDefaultDurationMs = 40`, `kDifferentialRangeDb = 12`, `kHoldMs = 50`, `kFastAttackMs = 0.5`, `kFastReleaseMs = 40`, `kSlowReleaseMs = 60`,
`kSlowTickSamples = 8`, `kGainSmoothMs = 10`, `kSilenceFloorLin = 1e-7f`. `struct DurationPoint { float durationMs; float slowAttackArgMs; };`
and `static constexpr std::array<DurationPoint, 11> kDurationTable` wrapped in `// HARVEST-BEGIN duration-table` / `// HARVEST-END duration-table`
(one initializer per line) holding the **seed** (model S12.2; the five bracketed values are log-log interpolations, deviation 7):
5 → 7.308, [7.5 → 11.281], 10 → 15.351, [15 → 24.153], 20 → 33.314, [30 → 50.435], 40 → 67.690, [60 → 103.140], 100 → 175.324,
[150 → 264.574], 250 → 444.323 (ms → ms), written `{.durationMs = 5.0f, .slowAttackArgMs = 7.308f},`. `static_assert`s: both columns
strictly increasing; first `durationMs == kMinDurationMs`, last `== kMaxDurationMs`; every argument in [`EnvelopeFollower::kMinAttackMs`,
`EnvelopeFollower::kMaxAttackMs`]. API: `prepare(double, std::size_t)`, `reset()`, `setAttackGainDb`, `setSustainGainDb`,
`setTransientDurationMs`, `processBlock(float* left, float* right, std::size_t)`, `static constexpr latencySamples()`, the taps
`currentGainDb()`, `transientFraction()`, `fastEnvelope()`, `slowEnvelope()`, `smoothedAttackDb()`, `smoothedSustainDb()`, `attackGainDb()`,
`sustainGainDb()`, `transientDurationMs()`, `stateFinite()`, and `friend struct detail::TransientShaperProbe;`. State per S4.2 including the
probe-only fields `probeHoldBypass_ = false`, `probeGainSmootherBypass_ = false`. Per-sample contract S4.3 steps 1–7 (sanitise to 0.0f;
`det_.process(xL, xR)` or `det_.process(xL)` when `right == left`; fast EF Amplitude per sample; slow EF Amplitude advanced when
`tickPhase_ == 0`, release argument `kSlowReleaseMs / kSlowTickSamples` = 7.5 ms, interpolated `s = slowPrev_ + (slowCur_ − slowPrev_) ·
(tickPhase_ / 8.0f)`, `tickPhase_ = (tickPhase_ + 1) & 7`; c = (s ≤ kSilenceFloorLin) ? (f > kSilenceFloorLin ? 1 : 0) :
clamp(gainToDb(f / s) / K, 0, 1); A = attackS_.process(), S = sustainS_.process() (probe bypass: the raw targets, smoothers still advanced);
gain_dB = A·c + S·(1 − c); g = dbToGain(gain_dB); one gain on both channels; taps written). `setTransientDurationMs` interpolates the table
in log-log and calls `slow_.setAttackTime(arg)`. `prepare` / `reset` / lifecycle per S4.3 (FR-047). Comments cite C-5 and S4.4.

**Probe** `transient_shaper_probe.h`: `namespace Krate::DSP::detail { struct TransientShaperProbe { static void setHoldBypass(TransientShaper&,
bool); static void setGainSmootherBypass(TransientShaper&, bool); static void setSlowAttackArgMs(TransientShaper&, float argMs); /* calls
slow_.setAttackTime(argMs) directly: the duration harvest's free variable */ }; }`.

**Failing tests first** (`[processors][profundum][transient-shaper]`; TU-local `tsRender(shaper, L, R, n, block)` that processes in host blocks
and, when asked, one sample at a time to record every tap per sample):
1. `TransientShaper_RangesAndDefaults` (SC-025, FR-001, FR-009): defaults 6 / 0 / 40; `setAttackGainDb(20)` → 12, `(−20)` → −12, the same for
   sustain; `setTransientDurationMs(1)` → 5, `(1000)` → 250; NaN, +Inf, −Inf keep the previous value for all three setters (before and after
   `prepare`); `static_assert(TransientShaper::latencySamples() == 0)`.
2. `TransientShaper_TransparentAtNeutral` (SC-013 shaper arm): A = S = 0, kSawAnchor and sine at E0 and C3, 2 s stereo L == R: from sample
   round(0.1 · fs) on, `std::memcmp(out, in) == 0` per channel.
3. `TransientShaper_GainsMatchParameters` (SC-008 (a)(b)(e)(f)): sine and kSawAnchor bursts at E1 and C3.
   (a) 5 × 5 grid A, S ∈ {−12, −6, 0, 6, 12} at duration 40 ms: |mean(gain tap) over [onset + 0.85 s, onset + 1.00 s] − S| ≤ **0.25 dB**.
   (b) S = 0, A ∈ {−12, −6, 6, 12}: the onset extreme of the gain tap over [onset, onset + 50 ms] (max for A > 0, min for A < 0) is within
   **±0.5 dB** of A.
   (f) A ∈ {−12, 6, 12} × S ∈ {−6, 0, 6}: at every sample of [onset, onset + 1 s], |tap − (A_s·c + S_s·(1 − c))| ≤ **0.1 dB**, with c recomputed
   in double from `fastEnvelope()` / `slowEnvelope()` by the S4.3 step-5 rule and A_s, S_s read from `smoothedAttackDb()` / `smoothedSustainDb()`.
   (e) 2 s silence pre-roll after `reset()`, A ∈ {6, 12}, S = 0: onset peak within **±0.5 dB** of A, and the max of the gain tap over the
   whole render ≤ **A + 0.5 dB**.
4. `TransientShaper_DurationMatches` (SC-008(c), the gated durations other than 5 ms): A = +12, S = 0, durations {20, 40, 100, 250} ms, sine and
   kSawAnchor at E1 and C3: |transientTimeMs / d − 1| ≤ **0.15**. Print every cell's error in %.
5. `TransientShaper_DurationShortestCell` (SC-008(c) at 5 ms; plan S1 C-2; deviation 11): `constexpr std::array<bool, 2> kGate5msAt{false,
   true};` for {E1, C3} (RULED 2026-10-17, plan S1 C-2 (a): C3 gated, E1 recorded). The four 5 ms cells; `CHECK` the ±15 % bar where gated,
   print all. The E1-sine cell is recorded (model +30.0 %, plan S12.2) (Defects
   found P-1); every gate up to and including T019's counts it green if that cell is its only failing assertion.
6. `TransientShaper_StepChangesClickFree` (SC-008(d)): a 3 s kSawAnchor note at E1 and C3, settings stepped at 1.5 s: attack 6 → 12, sustain
   0 → 6 and 0 → −6, duration 40 → 250. For each step, `ShapeCharacterTest`-style `differentialClicks(transition, refStart, refEnd, n, fs, f0)`
   from `shape_character_metric_helpers.h` (refStart / refEnd held at the start / end setting, same input) counts **0**. Positive control:
   `TransientShaperProbe::setGainSmootherBypass(true)` on the sustain 0 → +12 step is flagged **≥ 1** (`CHECK`, printed per note).
7. `TransientShaper_NoWaveformRateRipple` (SC-009, FR-004): steady sine and kSawAnchor at E0, E1, E2 (mono, `right == left`), all 25 grid
   settings, at durations 40 ms and 5 ms, 4 s renders: `rippleDb(gain tap, 3 s, 4 s)` ≤ **0.2 dB**. Negative control:
   `TransientShaperProbe::setHoldBypass(true)` at E0 sine, A = +12, S = 0, 5 ms, exceeds 0.2 dB (`CHECK`, printed).
8. `TransientShaper_Audibility` (SC-011 shaper; FR-043): kSawAnchor E1 burst held 2 s, `dTrWindow(fs, 250)` (375 ms), steady window = the last
   375 ms of the hold. D_tr(A = +12) − D_tr(A = −12) at S = 0, duration 40 ≥ **3 dB**; steady level(S = +12) − level(S = −12) at A = 0 ≥ **3 dB**;
   D_tr(250 ms) − D_tr(5 ms) at the defaults (+6 / 0) ≥ **3 dB**; print D_tr at A = S = 0 (the inert case, FR-009, recorded not gated).
9. `TransientShaper_MonoSum` (SC-012 shaper arm, FR-005, FR-041): (a) L == R input (kSawAnchor E1 burst), every SC-008 grid point at durations
   5 and 40 ms: `std::memcmp(outL, outR) == 0`; (b) kSawAnchor pair L = x, R = 0.5·x, A = +12, S = −6: |outL·inR − outR·inL| ≤
   4·FLT_EPSILON·|outL·inR| at every sample, and |sideMidRatioDb(out) − sideMidRatioDb(in)| ≤ **0.01 dB**.
10. `TransientShaper_NoAllocation` (SC-016 shaper arm): inside one `AllocationScope`: `processBlock` (stereo and mono), every setter with finite
    and non-finite arguments, `reset()`; live count == 0; `new int` positive control ≥ 1.
11. `TransientShaper_PartitionInvariant` (SC-018 shaper arm, FR-049): kSawAnchor E1 stereo 2 s, A = +12, S = −6; at absolute sample **8704**
    (plan C-11) `setTransientDurationMs(100)` and `setAttackGainDb(6)`; host blocks {1, 17, 32, 64, 512}: outputs memcmp-equal; two renders
    after `reset()` memcmp-equal.
12. `TransientShaper_SampleRates` (SC-021 shaper arm): fs ∈ {44.1, 48, 88.2, 96, 192} kHz: every output finite and |out| ≤ |in| · dbToGain(12) ·
    (1 + 1e-6); at E1, the SC-008(a) steady gain at (A = +12, S = −6) and the SC-008(b) onset peak at A = +12 are within **0.25 dB** of the
    48 kHz values; `prepare(48k)` → `prepare(96k)` → `prepare(48k)` then a render is memcmp-equal to a fresh 48 kHz render.
13. `TransientShaper_Lifecycle` (FR-047): (1) `processBlock` before `prepare` leaves a sentinel-filled buffer memcmp-equal and `currentGainDb()
    == 0`; (2) after `prepare`, a null `left` / `right` and `numSamples == 0` are no-ops, and a following render is memcmp-equal to one without
    the call; (3) `reset()` after 2 s of shaped kSawAnchor E1 (A = +12) then a render is memcmp-equal to the same render on a freshly prepared
    instance with the same setters.
14. `TransientShaper_DurationTableHarvest` `[.calibration]` (plan S8.1; **run only by the main loop in T019**): for each of the 11 durations,
    a geometric bisection (40 steps) of the slow argument over [0.2·a₀, 5·a₀] with a₀ = 2π·d / (0.438 · 8), set through
    `TransientShaperProbe::setSlowAttackArgMs`, so that the mean over the four cells (sine and kSawAnchor at E1 and C3, 48 kHz, A = +12, S = 0) of
    ln(transientTimeMs / d) is 0. Write `evidence/duration-table.txt`: a `BEGIN-INITIALIZER duration-table` line, the 11 initializer lines in the
    header's exact form (`std::setprecision(9)`, `f` suffix), `END-INITIALIZER duration-table`, then per-cell errors (%). Any cell beyond ±15 %
    is listed as a C-2 item ("FR-048 measurement"), never a retune.

- **Verify:** `dsp_processors_tests` `"[transient-shaper]~[long]"`. Run-log `evidence/run-log/T006.txt`.

### T007 [P] `BassCompressor` in `systems/mass_dynamics.h`, its probe and its standalone tests (FR-010–FR-019, FR-040, FR-047, FR-049; SC-007, SC-012/013/016/018/021 compressor arms, SC-024(a)(b)(c)(e); plan S5, C-6, C-10, C-11, C-13)

**Files:** `dsp/include/krate/dsp/systems/mass_dynamics.h` (fill the stub with `BassCompressor` only), `dsp/tests/unit/systems/mass_dynamics_probe.h`
(new), `dsp/tests/unit/systems/mass_compressor_test.cpp` (fill the stub). Target `dsp_systems_tests`. Depends on T004, T005.

**Header (plan S5.1–S5.5 exactly).** Includes `core/db_utils.h`, `primitives/biquad.h`, `primitives/smoother.h`,
`processors/dynamics_processor.h`, `processors/period_peak_detector.h`, `<array>`, `<cmath>`, `<cstddef>`, `<cstdint>`. **No L3 include.**
`namespace detail { struct MassDynamicsProbe; }`. `class BassCompressor` with the S5.1 constants (`kControlInterval = 32`, `kMinCornerHz = 20`,
`kMaxCornerHz = 500`, `kFallbackCornerHz = 80`, `kCornerSmoothMs = 20`, `kAttackMs = 10`, `kMinReleaseMs = 50`, `kMaxReleaseMs = 500`,
`kRuleWindowMs = 300`, `kRuleThresholdDb = 1`, `kMaxRuleIntervals = 1800`, `kMakeupSmoothMs = 5`), `struct AmountRow { float amount; float
thresholdDb; float slope; float kneeDb; float makeupDb; };` and `static constexpr std::array<AmountRow, 5> kAmountTable` wrapped in
`// HARVEST-BEGIN amount-table` / `// HARVEST-END amount-table` (one row per line) holding the S5.1 seed: {0, 0, 0, 0, 0}, {0.25, −12, 0.25, 6, 0},
{0.5, −16, 0.40, 6, 0}, {0.75, −19, 0.55, 6, 0}, {1, −22, 0.67, 6, 0}. `static_assert`s: amounts {0, 0.25, 0.5, 0.75, 1}; threshold
non-increasing; slope, knee, makeup non-decreasing; row 0 has slope 0 and makeup 0; every slope < 1; kMaxRuleIntervals ≥ round(0.3 · 192000 / 32);
`kMinCornerHz == DynamicsProcessor::kMinSidechainHz`, `kMaxCornerHz == DynamicsProcessor::kMaxSidechainHz`; `kAttackMs ==
DynamicsProcessor::kDefaultAttackMs`. API: `prepare`, `reset`, `setAmount`, `setDetectorKeyHz`, `processBlock`, `static constexpr
latencySamples()`, taps `currentGainDb()`, `currentGainReductionDb()`, `currentReleaseMs()`, `programFraction()`, `detectorCornerHz()`,
`detectorLevel()`, `amount()`, `stateFinite()`, `friend struct detail::MassDynamicsProbe;`. Members per S5.1 plus the probe fields
`probeFixedReleaseMs_ = 0.0f`, `probeHpBypass_ = false`, `probeRectifiedFeed_ = false`, `probeTableOverride_ = nullptr`
(`const std::array<AmountRow, 5>*`). `prepare` per S5.2 (sample rate clamped to [22050, 192000]; `dyn_` in Peak mode, internal sidechain off,
`setLookahead(0.0f)`, attack 10 ms, release 50 ms, makeup 0, auto makeup off; HP biquads at 80 Hz, `kButterworthQ`; `det_.prepare(fs, 25 ms)`;
`cornerLog2S_.configure(kCornerSmoothMs, static_cast<float>(fs / 32.0))`, `makeupS_.configure(kMakeupSmoothMs, static_cast<float>(fs))`;
ruleIntervals_ = clamp(round(0.3 · fs / 32), 1, 1800); pending amount and key applied; `reset()`). `reset` per S5.2 steps 1–7. Amount mapping
S5.3 (bracketing rows, linear in t, ratio = 1 / (1 − slope); amount 0 gives ratio exactly 1.0f and makeup 0.0f; setters on `dyn_` called only
when the value changed bitwise; the probe table, when set, replaces `kAmountTable` in the mapping). `controlStep` S5.4 (latch amount and key;
corner target log₂(clamp(2·key, 20, 500)) or log₂(80) for "no voice" — key ≤ 0 or non-finite; corner smoother; reconfigure both HP biquads and
`det_.setHoldMs(2000 / corner)` when the corner changed bitwise; release rule ring with integer counts, f = ruleSum_ / (ruleIntervals_ · 32),
releaseMs_ = 50 + 450·f or the probe's fixed value, `dyn_.setReleaseTime` only when changed). Per-sample S5.5 (sanitise to 0.0f; `controlStep`
at intervalPhase_ == 0; HP on signed samples (probe HP bypass: raw xL, xR); held = `det_.process(hL, hR)`; probe rectified feed: max(|xL|, |xR|)
fed straight to `dyn_` with `setSidechainEnabled(true)` and `setSidechainCutoff(corner)` — the struck FR-011 form, kept only for SC-006(e);
`(void)dyn_.processSample(held)`; gr = `dyn_.getCurrentGainReduction()`; `++ruleCount_` when gr < −1; gainDb_ = gr + makeupS_.process(); output
= input when gainDb_ == 0.0f, else ×dbToGain(gainDb_) on both channels; intervalPhase_ advances mod 32). Mono when `right == left`. Lifecycle
FR-047 (S5.2 last paragraph). Comments cite C-6, C-10, D-5, D-6, FR-011 (b) and the S5.5 cost note.

**Probe** `mass_dynamics_probe.h` (this task writes the `BassCompressor` half; T010 appends the `MassDynamics` accessors): `namespace
Krate::DSP::detail { struct MassDynamicsProbe { static void setFixedReleaseMs(BassCompressor&, float ms); /* 0 = the D-5 rule */ static void
setHpBypass(BassCompressor&, bool); static void setRectifiedFeed(BassCompressor&, bool); static void overrideAmountTable(BassCompressor&, const
std::array<BassCompressor::AmountRow, 5>* table); /* nullptr = kAmountTable */ static float threshold(const BassCompressor&); static float
ratio(const BassCompressor&); static float knee(const BassCompressor&); static float makeupTargetDb(const BassCompressor&); static float
releaseTimeInDynamics(const BassCompressor&); }; }` (the readers forward to `dyn_.getThreshold()`, `getRatio()`, `getKneeWidth()`,
`getReleaseTime()`, `dynamics_processor.h:327–331`).

**Failing tests first** (`[systems][profundum][mass-dynamics]`; input fixtures from `../processors/harmonic_fold_test_helpers.h` and
`../processors/transient_shaper_test_helpers.h`):
1. `MassCompressor_AmountMapping` (FR-016, FR-014): after one interval, amount 0 → `ratio() == 1.0f` exactly and `makeupTargetDb() == 0.0f`;
   amount 1 → the last row (threshold, ratio = 1 / (1 − 0.67), knee) within 1e-5; amount 0.6 → the linear interpolation of rows 0.5 and 0.75
   within 1e-5; over 101 amounts threshold non-increasing and ratio, knee, makeup non-decreasing; `setAmount(NaN / ±Inf)` keeps the previous
   value; `setAmount(−1)` → 0, `(2)` → 1; a `setAmount` issued mid-interval is not visible in `ratio()` until the next 32-sample boundary;
   `latencySamples() == 0`.
2. `MassCompressor_TransparentAtAmountZero` (SC-013 compressor arm): amount 0, kSawAnchor and sine at E0 and C3, 2 s stereo: from round(0.1·fs)
   on, `memcmp(out, in) == 0` and `currentGainDb() == 0.0f`.
3. `MassCompressor_ReleaseRuleLaw` (SC-007 law tap, D-5): amount 1, kSawAnchor E1 at +12 dB re the amount-1 threshold for 1 s → `programFraction()`
   ≥ 0.999 and `currentReleaseMs()` = 500 ± 1e-3; then input 30 dB lower until `programFraction()` reads 0 (REQUIRE it does within 3 s; the release itself keeps GR above 1 dB for part of that time) → `currentReleaseMs() == 50`;
   at every interval `currentReleaseMs() == 50 + 450 · programFraction()` within 1e-3 ms and `releaseTimeInDynamics()` equals it; over a rising
   level ramp the release is non-decreasing whenever f is; with the probe fixed release 275, `currentReleaseMs() == 275` at every interval.
4. `MassCompressor_ProgramDependentRelease` (SC-007; plan S1 C-13): `enum class RecoveryFrom { Peak, PlateauEnd }; constexpr RecoveryFrom
   kRecoveryFrom = RecoveryFrom::PlateauEnd;` (RULED 2026-10-17, plan S1 C-13: recovery starts at the last sample within 0.1 dB of peak GR; the Peak reading is printed beside it). Amount 1, kSawAnchor at E1 and C3, key = the note. Scale the
   base so `detectorLevel()` (mean dB over the last 0.5 s of a 1.5 s render) equals the amount-1 threshold (`MassDynamicsProbe::threshold`):
   scale = 10^((thr − d₁)/20). Two renders: 2 s of base, then an event of +12 dB (×3.98107, 0.5 ms raised-cosine edges) lasting 20 ms or 1 s, then
   2 s of base. Print the detector reading during the 1 s plateau (expected thr + 12 ± 0.5 dB). From the per-sample GR tap: p = min GR of the
   event; start = the first sample with GR == p (Peak) or the last sample within 0.1 dB of p (PlateauEnd); end = the first later sample with
   GR ≥ p / 2. Gate short ≤ **0.5** · long on the `kRecoveryFrom` reading at both notes; print both readings for both events.
5. `MassCompressor_FixedReleasePositiveControl` (SC-007): the same renders with `setFixedReleaseMs(275.0f)`: ratio > **0.5** on the gated reading
   at both notes (`CHECK`); print both readings.
6. `MassCompressor_KeytrackedCorner` (SC-024 (a)(b)(c)(e), FR-018): (a) key E1 → `detectorCornerHz()` within **1 %** of 2 · E1 by 200 ms;
   (b) key 5 Hz → 20 Hz and key 300 Hz → 500 Hz (within 1 % by 200 ms); a key ramp from C3 down 48 semitones (log-linear over 1 s, one call per
   interval) and a key step E1 → C3: at every interval k, |log₂ corner_k − log₂ corner_{k−1}| ≤ (1 − exp(−5000 / (20 · fs / 32))) ·
   |T_k − log₂ corner_{k−1}| + 1e-6, with T_k = log₂(clamp(2·key_k, 20, 500)); (c) keys 0, −1, NaN, +Inf → 80 Hz within 1 % by 200 ms; (e) key
   changed at sample 8704, host blocks {1, 17, 512}: outputs memcmp-equal.
7. `MassCompressor_MonoSum` (SC-012 compressor arm, FR-013): (a) L == R kSawAnchor E1 at amounts {0, 0.5, 1}: `memcmp(outL, outR) == 0`;
   (b) L = x, R = 0.5·x at amount 1: per-sample |outL·inR − outR·inL| ≤ 4·FLT_EPSILON·|outL·inR| and side / mid ratio within **0.01 dB** of the
   input's.
8. `MassCompressor_NoAllocation` (SC-016 compressor arm, FR-014): inside one `AllocationScope`: `processBlock`, every setter finite and
   non-finite, `reset()`, the probe's release toggle; live count 0; positive control.
9. `MassCompressor_PartitionInvariant` (SC-018 compressor arm): kSawAnchor E1 3 s, amount 0.5 → 1 and key E1 → E2 at sample 8704, probe fixed
   release 275 → 0 at sample 8704 + 32·64; host blocks {1, 17, 32, 64, 512}: memcmp-equal; two renders after `reset()` memcmp-equal.
10. `MassCompressor_SampleRates` (SC-021 compressor arm): {44.1, 48, 88.2, 96, 192} kHz: finite; |out| ≤ |in| · dbToGain(max table makeup) ·
    (1 + 1e-6); `programFraction()` reaches 1 under the 1 s +12 dB drive at every rate; `prepare(48k)` → `prepare(96k)` → `prepare(48k)` then a
    render is memcmp-equal to a fresh 48 kHz render.
11. `MassCompressor_Lifecycle` (FR-047; plan S13.3 FR-047 row (1)–(4)): (1) before `prepare` a sentinel buffer is untouched and the taps read
    their defaults; (2) null channel / 0 samples are no-ops and the next render is memcmp-equal to one without the call; (3) `reset()` after 2 s
    at amount 1 with f > 0.5 then a render equals a fresh instance's; (4) `prepare(96k)`, 2 s at f > 0.5, `prepare(48k)` → `programFraction() ==
    0`, `currentReleaseMs() == 50`, and a render memcmp-equal to a fresh 48 kHz one. All inside `AllocationScope` except the first `prepare`.

- **Verify:** `dsp_systems_tests` `"MassCompressor_*"`. Run-log `evidence/run-log/T007.txt`.

---

## Group 4 — shaper non-finite TU and `MassLaw` (parallel; disjoint new files)

### T008 [P] `TransientShaper` non-finite arm (SC-019 shaper arm; FR-007)

**Files:** `dsp/tests/unit/processors/transient_shaper_nonfinite_test.cpp` (fill the stub; it is in the `-fno-fast-math` block). Target
`dsp_processors_tests`. Depends on T006.

- `TransientShaper_NonFinite` `[processors][profundum][transient-shaper][nonfinite]`: kSawAnchor E1 stereo, A = +12, S = −6, 1 s steady; then
  16-sample bursts of NaN (`std::bit_cast<float>(0x7FC00000u)`), +Inf (`0x7F800000u`) and −Inf (`0xFF800000u`), first on L only, then on both
  channels, 0.5 s apart. Assert: every output sample is finite (`TransientShaperTest::isFiniteBits`); every output sample whose input sample was
  non-finite is exactly `0.0f`; `stateFinite()` holds 512 samples after each burst; 100 ms after each burst the whole-period level of L (one
  period at E1) is within **0.1 dB** of the same-length pre-burst window; `setAttackGainDb`, `setSustainGainDb`, `setTransientDurationMs` with
  each non-finite pattern leave `attackGainDb()`, `sustainGainDb()`, `transientDurationMs()` unchanged.
- **Implementation:** none expected (T006 implements FR-007). A red here is a `DEFECT:` line against `transient_shaper.h` (rule 11).
- **Verify:** `dsp_processors_tests` `"TransientShaper_NonFinite"`. Run-log `evidence/run-log/T008.txt`.

### T009 [P] `MassLaw` (L3, `systems/mass_law.h`) and its law-level tests (FR-020–FR-029, FR-049; SC-001(a), SC-004, SC-016 law arm, SC-023 law, SC-025 law; plan S7, C-7, C-9, C-12, S8.2 seed)

**Files:** `dsp/include/krate/dsp/systems/mass_law.h` (fill the stub), `dsp/tests/unit/systems/mass_law_test.cpp` (fill the stub). Target
`dsp_systems_tests`. Depends on T006 (duration range constants).

**Header (plan S7 exactly).** Includes `core/db_utils.h`, `core/modulation_curves.h`, `primitives/smoother.h`, `processors/transient_shaper.h`,
`<algorithm>`, `<array>`, `<cstddef>`, `<cstdint>`. No L3 include. `class MassLaw` with `enum class Target : std::uint8_t { Depth, Body,
TransientDuration, FoldAmount, CompressionAmount, AnchorLevel, OutputTrim };` (append-only; `OutputTrim` = the FR-021 trim, RULED 2026-10-17 G8-5), `kNumTargets = 7`, `kMinMass = 0`, `kMaxMass = 100`,
`kNeutralMass = 50`, `kControlInterval = 32`, `kMassSweepMs = 40.0f`, `kSlewBoundRefMs = 20.0f` (RULED 2026-10-17, plan S1 C-7 option (i): bound from the 20 ms half sweep, 40 ms traversal, no row capped; the S8.2 Fold / compression seeds at +60 / +1), `struct
Targets` (S7.1 defaults: depth 0.5, body 0.5, transientDurationMs `TransientShaper::kDefaultDurationMs`, foldAmount 0, compressionAmount 0,
anchorLevel 0.5), `struct Row { Target target; float lo; float hi; float amountDown; float amountUp; ModCurve curve; };` and `static constexpr
std::array<Row, kNumTargets> kRows` wrapped in `// HARVEST-BEGIN mass-law-rows` / `// HARVEST-END mass-law-rows` (one row per line) holding the
S8.2 seed: Depth {0, 1, −0.5, −0.5, Linear}; Body {0, 1, +0.5, +0.4, Linear}; TransientDuration {5, 250, +30, +110, Linear}; FoldAmount
{0, 100, +50, +50, Linear}; CompressionAmount {0, 1, +0.5, +0.5, Linear}; AnchorLevel {0, 1, +0.5, +0.5, Linear}. Compile-time checks (S7.2):
one row per target in enum order; lo < hi; amountUp · amountDown ≥ 0; amountUp ≤ 0 for Depth and ≥ 0 for the other five; curve ≠ Stepped;
max(|aUp|, |aDown|) · maxSlope(curve) ≤ (hi − lo) · kMassSweepMs / (2 · kSlewBoundRefMs) with maxSlope 1 / 2 / 1.5 for Linear / Exponential /
SCurve (a `constexpr` function); the TransientDuration row's [lo, hi] == [`TransientShaper::kMinDurationMs`, `kMaxDurationMs`]. API: pure
`static float evaluateRow(const Row&, float mass, float base) noexcept` and `static Targets evaluate(float mass, const Targets& base) noexcept`
(S7.2: non-finite mass → 50, non-finite base → the `Targets{}` default of that field; clamp mass; u = (mass − 50) · 0.02f; s =
`applyModCurve(curve, |u|)`; `std::clamp(base + (u >= 0 ? amountUp * s : -amountDown * s), lo, hi)`; no neutral fast path); stateful
`prepare(double sampleRate)` (`slew_.configure(100.0f / kMassSweepMs, static_cast<float>(sampleRate / 32.0))`), `reset()` (snap to the pending
Mass), `setMass` (non-finite keeps the previous pending value; clamp [0, 100]), `setBase` (per field: non-finite keeps the previous field; clamp
to the row's [lo, hi]), `advanceInterval()` (one `SlewLimiter` step; returns the smoothed Mass; before `prepare` it returns the current smoothed
value unchanged, initially 50 — record this decision), `currentTargets()`, `smoothedMass()`, `pendingMass()`, `base()`. Comment: "Phase 9's
engine and the Phase 6 fixture call advanceInterval() once per 32-sample interval of an absolute sample counter (plan C-11); the targets are
smoothed per plan C-12 (no target is double-smoothed)."

**Failing tests first** (`[systems][profundum][mass-law]`; this TU may include `anchor_oscillator.h` and `spectral_shape_recipe.h`):
1. `MassLaw_TableInvariants`: runtime mirror of every compile-time check; `static_assert(sizeof(MassLaw::Targets) == 6 * sizeof(float))` and
   `kNumTargets == 6` (FR-021, SC-025: no attack / sustain field); `Targets{}.anchorLevel == AnchorOscillator::kDefaultLevel`; `Targets{}.depth`
   / `.body` equal `SpectralShapeRecipe::kDefaultControls.depth` / `.body`; `Targets{}.transientDurationMs == TransientShaper::kDefaultDurationMs`;
   a `constexpr` count of rows with target `AnchorLevel` == 1 (FR-030).
2. `MassLaw_TargetsMonotonic` (SC-001(a)): the three bases (the reference = `Targets{}`; kSawAnchor = depth 0, body 0; kSineAnchor = depth 1,
   body 0; the non-recipe fields at the `Targets{}` defaults): `evaluate(m, base)` at m = 0, 0.1, …, 100 (1001 points, m = k / 10.0f); each
   field monotone non-strict in its D-2 direction (Depth non-increasing, the other five non-decreasing); print the clamped-run count per field.
3. `MassLaw_NeutralAtFifty` (SC-023 law arm, FR-027): `memcmp(evaluate(50, b), b) == 0` for the three bases and for 100 seeded in-range bases
   (`std::mt19937` seed 6006, test-side only); print `evaluate(0)` / `evaluate(100)` for the edge bases (depth 1 at Mass 100; fold 100 at Mass 0).
4. `MassLaw_SlewBounded` (SC-004, FR-025; deviation 12): prepared at 48 kHz, `setMass(0)`, `reset()`, `setMass(100)`, then `advanceInterval()`
   until the smoothed Mass is 100: for every target, each per-interval |Δtarget| ≤ (hi − lo) · (32 / fs) / (kSlewBoundRefMs · 0.001) +
   1e-6 · (hi − lo) (print the largest excess over the bar without the allowance); the step count equals ceil(kMassSweepMs · 0.001 · fs / 32) ± 1
   (60 at 48 kHz); the same per-interval target sequence (memcmp) results whether the intervals are driven from host blocks of 1, 17, 64 or 512
   samples (count 32-sample boundaries of an absolute counter). Repeat at 44.1 and 96 kHz.
5. `MassLaw_PureEvaluation` (FR-026): `evaluate` called twice with the same arguments, and from two different `MassLaw` instances in different
   states, returns memcmp-equal `Targets`.
6. `MassLaw_InputSafety` (FR-029): `setMass(NaN / ±Inf)` keeps `pendingMass()`; `setMass(−5)` → 0, `(150)` → 100; `setBase` with one non-finite
   field keeps that field and updates the others; out-of-range fields clamp to the row's [lo, hi]; `evaluateRow` with a non-finite mass equals
   the Mass 50 value and with a non-finite base uses the `Targets{}` default.
7. `MassLaw_Lifecycle`: before `prepare`, `advanceInterval()` returns 50 and changes nothing; `reset()` snaps to the pending Mass; after
   `prepare(96000)` a full sweep takes 120 intervals (± 1).
8. `MassLaw_NoAllocation` (SC-016 law arm): `setMass`, `setBase`, `advanceInterval`, `evaluate`, `evaluateRow`, `reset` inside an
   `AllocationScope`: live count 0; positive control.

- **Verify:** `dsp_systems_tests` `"[mass-law]~[long]"`. Run-log `evidence/run-log/T009.txt`.

---

## Group 5 — `MassDynamics` (sequential; edits the shared `mass_dynamics.h` and `mass_dynamics_probe.h`)

### T010 `MassDynamics` appended to `systems/mass_dynamics.h` and its tests (FR-013, FR-015, FR-041, FR-047, FR-049; SC-012, SC-013, SC-016, SC-018 dynamics arm; plan S6, C-8, C-11; deviation 4)

**Files:** `dsp/include/krate/dsp/systems/mass_dynamics.h` (append after `BassCompressor` and leave `BassCompressor` unchanged —
a needed change there is a `DEFECT:` line for T020), `dsp/tests/unit/systems/mass_dynamics_probe.h` (append the two accessors),
`dsp/tests/unit/systems/mass_dynamics_test.cpp` (fill the stub). Target `dsp_systems_tests`. Depends on T006, T007, T009.

- **Header.** Add `#include <krate/dsp/processors/transient_shaper.h>` (peer-lower L2, allowed). `class MassDynamics` exactly as plan S6:
  `kControlInterval = 32`; `prepare`, `reset`; `setTransientDurationMs` (pending, latched at the next interval), `setCompressionAmount` (→
  `BassCompressor::setAmount`), `setDetectorKeyHz` (→ `BassCompressor::setDetectorKeyHz`), `setTransientAttackDb` / `setTransientSustainDb`
  (immediate; smoothed in the shaper); `processBlock` splits the call into runs ending on its 32-sample grid, latches
  `shaper_.setTransientDurationMs(pendingDurationMs_)` at each run start with intervalPhase_ == 0, then runs `shaper_.processBlock(run)` and
  `comp_.processBlock(run)` — shaper → compressor, fixed (FR-015); `static constexpr latencySamples()`; `shaper()`, `compressor()` const
  accessors; `stateFinite()`; **`friend struct detail::MassDynamicsProbe;`** (deviation 4). `prepare` prepares both at fs, calls `reset()`,
  sets `prepared_`; `reset()` = `shaper_.setTransientDurationMs(pendingDurationMs_)`, `shaper_.reset()`, `comp_.reset()`, intervalPhase_ = 0.
  Lifecycle per FR-047 (S6). Mono when `right == left`. `static_assert(MassDynamics::kControlInterval == BassCompressor::kControlInterval)`.
- **Probe** (append): `static TransientShaper& shaper(MassDynamics&)` and `static BassCompressor& compressor(MassDynamics&)` (non-const access
  for the positive / negative controls).
- **Failing tests first** (`[systems][profundum][mass-dynamics]`; include `../processors/transient_shaper_probe.h` where needed):
  1. `MassDynamics_MonoSum` (SC-012, FR-041): (a) L == R kSawAnchor E1 burst, every SC-008 grid point (A, S ∈ {−12, −6, 0, 6, 12}) × amounts
     {0, 0.5, 1}, key E1: `memcmp(outL, outR) == 0`; (b) L = x, R = 0.5·x, A = +12, S = −6, amount 1: per sample |outL·inR − outR·inL| ≤
     4·FLT_EPSILON·|outL·inR| and |sideMidRatioDb(out) − sideMidRatioDb(in)| ≤ **0.01 dB**.
  2. `MassDynamics_TransparentAtNeutral` (SC-013): shaper 0 / 0 and amount 0, kSawAnchor and the reference voice
     (`HarmonicFoldTest::recipeAdditive(SpectralShapeRecipe::kDefaultControls, …)`) at E0 and C3, 2 s: from round(0.1 · fs) on, `memcmp(out, in)
     == 0` per channel.
  3. `MassDynamics_ShaperThenCompressor` (FR-015): a `MassDynamics` (A = +12, S = −6, duration 40, amount 1, key E1, host block 512) and a
     separately prepared `TransientShaper` → `BassCompressor` pair with the same settings fed the same kSawAnchor E1 3 s: outputs memcmp-equal.
  4. `MassDynamics_LatchesOnGrid` (FR-049, plan C-11): `setTransientDurationMs(250)` and `setCompressionAmount(0.5)` issued at absolute sample
     8690 give an output memcmp-equal to the same calls issued at 8704 (both latch at 8704); issued at 8705 they differ.
  5. `MassDynamics_PartitionInvariantDynamicsOnly` (SC-018 dynamics arm): kSawAnchor E1 3 s, duration 40 → 100, amount 0.5 → 1, key E1 → E2 and
     `MassDynamicsProbe::setFixedReleaseMs(compressor(md), 275)` at sample 8704; host blocks {1, 17, 32, 64, 512}: outputs memcmp-equal; two
     renders after `reset()` memcmp-equal.
  6. `MassDynamics_NoAllocation` (SC-016): inside one `AllocationScope`: `processBlock` of `TransientShaper`, `BassCompressor` and
     `MassDynamics`; every setter of the three with finite and non-finite arguments; `reset()` of each; `MassLaw::{setMass, setBase,
     advanceInterval, evaluate}` and a Mass 0 → 100 step; a forced release change (probe); live count 0; positive control.
  7. `MassDynamics_Lifecycle` (FR-047; plan S13.3 FR-047 row): (1) before `prepare` a sentinel buffer is untouched and `shaper().currentGainDb()
     == 0`, `compressor().currentGainDb() == 0`; (2) null channel / 0 samples are no-ops and the next render is memcmp-equal to one without the
     call; (3) `reset()` after 2 s of kSawAnchor E1 at A = +12, amount 1 with `programFraction() > 0.5`, then a render, equals a freshly
     prepared instance's; (4) `prepare(96k)`, 2 s at f > 0.5, `prepare(48k)` → `compressor().programFraction() == 0`,
     `compressor().currentReleaseMs() == 50`, and a render memcmp-equal to a fresh 48 kHz one. Inside `AllocationScope` except the first
     `prepare`.
  8. `MassDynamics_LatencyZero` (FR-014): `static_assert` that `TransientShaper::latencySamples()`, `BassCompressor::latencySamples()` and
     `MassDynamics::latencySamples()` are 0.
- **Verify:** `dsp_systems_tests` `"MassDynamics_*"` and `"MassCompressor_*"` (the appended header must leave T007's cases green). Run-log
  `evidence/run-log/T010.txt`.

---

## Group 6 — the validation-chain fixture (sequential, single task)

### T011 `mass_dynamics_test_helpers.h` (plan S9) and its self-tests (FR-031–FR-034; deviations 5, 8)

**Files:** `dsp/tests/unit/systems/mass_dynamics_test_helpers.h` (new), `dsp/tests/unit/systems/mass_dynamics_helpers_test.cpp` (fill the stub).
Target `dsp_systems_tests`. Depends on T003, T005, T007, T009, T010.

- **Implement** (namespace `Krate::DSP::MassDynamicsTest`, all `inline`; includes `shape_character_test_helpers.h`,
  `profundum_core_test_helpers.h`, `../processors/transient_shaper_test_helpers.h`, `k_weighting.h`, `mass_dynamics_probe.h`, the four new
  headers, `profundum_core.h`, `anchor_oscillator.h`, `harmonic_fold.h`, `dc_blocker.h`, `true_peak_limiter.h`, `<functional>`, `<optional>`,
  `<span>`):
  - `kSettleSeconds = 1.0`; `descriptorSeconds(fs)` = 1 + `TestUtils::kLowFrequencyFftSize` / fs.
  - `using TargetOverride = std::function<std::optional<float>(std::size_t sample)>;`
  - `struct ChainConfig` — plan S9 fields `base` (default `SpectralShapeRecipe::kDefaultControls`), `f0`, `fs = 48000`, `seconds = 0` (≤ 0 →
    `descriptorSeconds(fs)`), `hostBlock = 512`, `attachAnchor = true`, `massLawDetached = false`, `massAt` (empty → 50),
    `targetOverride` (`std::array<TargetOverride, MassLaw::kNumTargets>`, indexed by `MassLaw::Target`), plus (deviation 8)
    `std::optional<MassLaw::Targets> lawBase` (empty → depth / body from `base`, the other four at the `Targets{}` defaults), `noteOnSample = 0`,
    `std::function<float(std::size_t)> preDynamicsGainDb` (empty → 0 dB; applied to the sum before `MassDynamics`), `std::function<void(MassDynamics&)>
    configureDynamics` (called after `prepare`), `const std::array<MassLaw::Row, MassLaw::kNumTargets>* rowsOverride = nullptr` (targets from
    `MassLaw::evaluateRow(row, mass, base)` per row instead of `kRows`), `unsmoothedMass = false` (targets evaluated at `massAt(n)` directly,
    skipping the slew: SC-005(a)'s positive control), `float shaperAttackDb = TransientShaper::kDefaultAttackDb`, `float shaperSustainDb =
    TransientShaper::kDefaultSustainDb`, `bool perSampleDynamicsTaps = false`.
  - `struct ChainTaps` — plan S9 vectors (`coreL/R`, `foldL/R`, `anchorL/R`, `sumL/R`, `dynL/R`, `dcL/R` (after the DC blockers = the limiter input), `outL/R`, `a1PerInterval`,
    `anchorLevelPerInterval`, `anchorControlsPerInterval` from `AnchorOscillator::pendingControls()`), plus per-interval `massPerInterval` and
    `targetsPerInterval`, and — only when `perSampleDynamicsTaps` — per-sample `shaperGainDb`, `compGainDb`, `compGrDb`.
  - `ChainTaps renderChain(const ChainConfig&)` — plan S9 exactly: owns `ProfundumCore` (`prepare(fs)`), `AnchorOscillator` (`prepare(fs)`, Sine,
    LOCK), `HarmonicFold` (`prepare(fs, 512)`, `setFundamentalHz(f0)`), `MassLaw` (`prepare(fs)`, `setBase`, `setMass(massAt(0))`, `reset()`),
    `MassDynamics` (`prepare(fs, 512)`, shaper gains, then `configureDynamics`), two `DCBlocker`s (`prepare(fs, 5.0f)`, C-10) and a
    `TruePeakLimiter` (defaults, `prepare(fs, 512)`). `noteOn(f0)` on the core and the Anchor (re-arm) at `noteOnSample` (silence before). Host
    blocks are split at the absolute 32-sample grid. At each grid point: `law.setMass(massAt(n))`, `law.advanceInterval()`, targets (law,
    `rowsOverride`, `unsmoothedMass` or `massLawDetached` = `law.base()`), then each set `targetOverride[k]` replaces field k; then
    `core.setControls(base with depth, body)`, `fold.setAmount(foldAmount)`, `anchor.setControls({.waveform = Sine, .mode = Lock, .level =
    anchorLevel})` and the `pendingControls()` read-back; `md.setTransientDurationMs`, `md.setCompressionAmount`, `md.setDetectorKeyHz(f0)`. Per
    run: core block; a₁ = `SpectralShapeRecipe::evaluate(base, f0, fs)[0] · kCenterPanGain`, the voice's own controls before the Mass biases
    (FR-031 as amended, RULED 2026-10-17 G8-3; was `deliveredGains()[0]`) → `anchor.setCoreH1Amplitude(a1)` (the Phase 3
    order); Anchor block (skipped when `attachAnchor` is false, anchor taps 0); `fold.processBlock` on a copy of the core output; sum = fold + anchor
    as a plain float add (FR-032); the `preDynamicsGainDb` gain; `md.processBlock` (one sample at a time when `perSampleDynamicsTaps`, recording the
    taps); the DC blockers; `limiter.processBlock(L, R, static_cast<int>(run))`. Every stage copied to its tap.
  - `struct PolyVoice` + `renderPoly(std::span<const double> f0s, const ChainConfig& common)`: one core + Anchor + Fold per voice at the law's
    targets, summed, one `MassDynamics` keyed once per interval by `lowestSoundingF0` (SC-024(d), FR-035 poly record).
  - `lowestSoundingF0(std::span<const float> f0s)`: the minimum of the positive finite entries, 0 ("no voice") when none (FR-018, FR-035).
  - `struct SubBand { std::vector<double> dftDb, lpRmsDb; double lpGroupDelayPeriods; };` `subBand(const float* y, std::size_t n, double fs,
    double f0)`: s(t) as a sliding single-bin DFT at f₀ over exactly one (fractional) period P = fs / f0 — Σ_{k<⌊P⌋} x[k]·e^{−jωk} + frac(P)·x[⌊P⌋]·
    e^{−jω⌊P⌋}, amplitude 2|X| / P, in dB — with hop ≤ P / 8; and the second reading: RMS over one period of
    `BiquadCascade<2>::setButterworth(FilterType::Lowpass, 1.5·f0, fs)` (C-10), whose group delay at f₀ is measured (Goertzel phase difference of
    a steady f₀ sine in and out) and returned in periods. `mSub(trace, begin, end)` = max − min.
  - `struct PeriodDescriptors { double L, Elow, Ehigh, Thd, C, w1Frac; };` `periodDescriptors(y, start, fs, f0)`: harmonic powers by
    an exact periodic DFT of the one fractional period at `start` resampled onto ⌊P⌋ points (Kaiser-windowed sinc, half-width 32, β = 10;
    RULED 2026-10-17, Group 6 gate, spec SC-005(c) row); reads y[start − 32 … start + ⌈P⌉ + 32] (SC-005(c)).
  - `deltaLStepDb(outL, fs, f0, lowBegin, lowEnd, highBegin, highEnd)` = level(high) − level(low), whole periods (ΔL_step).
  - `aliasResidualDb(const float* x, const float* gainDb, const float* y, std::size_t n, double fs, std::size_t t0)` — plan C-3 exactly: in
    double, an 8× FFT zero-pad upsample of x and of the gain trace over [t0 − 50 ms, t0 + 150 ms] with 50 ms Hann tapers (FFT length the next
    power of two ≥ 8 × the segment; `ProfundumTest::fftDouble`, inverse by conjugation), their product, a brick-wall cut at fs / 2, decimation by
    8; r = y − that; returns 10·log10(mean(r²) / 0.5) over [t0, t0 + 100 ms] (the `aliasedPower` "re 0.5" convention).
  - `gridDriver(std::size_t total, std::size_t hostBlock, F&& fn)` calling `fn(start, len, atGrid)` for runs split at the absolute 32 grid.
- **Failing tests first** (`[systems][profundum][mass-dynamics]`):
  1. `MassDynamicsHelpers_LowestSoundingF0`: {E1, C3} → E1; {C3, E1} → E1; {C3, 0, E1} → E1; {NaN, C3} → C3; {} → 0; {−1, 0} → 0; {+Inf, E1} → E1.
     Each row is chosen so a first-voice, last-voice or maximum rule fails at least one: compute those three rules inline and `CHECK` each
     disagrees with the table somewhere.
  2. `MassDynamicsHelpers_SubBand`: an E1 sine amplitude-modulated by exactly 1.0 dB peak-to-peak at 0.5 Hz (3 s): `mSub` of `dftDb` over
     [0.5 s, 3 s] = **1.0 ± 0.05 dB**; a steady kSawAnchor at E0 (5 s): `mSub` of `dftDb` over [1 s, 5 s] ≤ **0.01 dB**; print the LP reading and
     `lpGroupDelayPeriods` (analog figure 0.361 periods, C-10).
  3. `MassDynamicsHelpers_AliasResidual`: a C3 kSawAnchor times a constant +6 dB gain → ≤ **−120 dBFS**; times a 1 dB / 100 ms gain ramp →
     ≤ **−100 dBFS**; positive control, a hard 0 → +12 dB gain step at t0 → **> −80 dBFS**. Print FFT length and bin Hz.
  4. `MassDynamicsHelpers_GridDriver`: host blocks {1, 17, 64, 512} over 8804 samples: runs tile [0, 8804) exactly, none crosses a multiple of
     32, `atGrid` is true exactly when `start % 32 == 0`, and one run starts at 8704.
  5. `MassDynamicsHelpers_PeriodDescriptors`: on a steady kSawAnchor and a steady sine at E1 and C3, `periodDescriptors` L, E_low, THD agree with
     `ShapeCharacterTest::describe` within **0.1 dB** after the OQ-S −60 dB floor clamp on both readings, C within **0.005 oct**, and the
     sine's raw THD reads **≤ −70 dB** (RULED 2026-10-17, Group 6 gate: the rectangular estimator floored near −50 dB on the sine).
  6. `MassDynamicsHelpers_DeltaLStep`: `TransientShaperTest::twoLevelStep(Saw, E1, 48k, 2, 2)` with no processing reads **12 ± 0.05 dB**.
  7. `MassDynamicsHelpers_RenderChainContract` (FR-031–FR-033): E1, `seconds = 1.0`: every tap has N = 48000 samples; `sumL == foldL + anchorL`
     (memcmp against the float add); the core taps are memcmp-equal with `attachAnchor` false; `a1PerInterval.size() == 1500`;
     `anchorControlsPerInterval[k].level == anchorLevelPerInterval[k]`; at Mass 50, `massLawDetached` gives every tap memcmp-equal to the law
     render; `descriptorSeconds(48000)` = 1 + 262144 / 48000 within 1e-12.
- **Verify:** `dsp_systems_tests` `"MassDynamicsHelpers_*"`. Run-log `evidence/run-log/T011.txt`.

---

## Group 7 — the chain, law and close-lane TUs (parallel; each task owns exactly one TU; no header edits)

**Gate rule for Groups 7–8 (deviation 6):** the cases listed under "Expected red until T019" in T012–T015 depend on a harvested table
(`kDurationTable`, `kAmountTable`, `kRows` still hold the model seeds). Until T019 has run, a gate counts each of them green **if it is the only
kind of failure in its case** (a bar on a calibrated quantity); a crash, a non-finite value, an allocation, a bit-identity failure or any other
case failing is a real red. From T019's gate on, they are ordinary cases.

### T012 [P] Chain validation (SC-014, SC-015, SC-018, SC-021; FR-030–FR-035, FR-049)

**Files:** `dsp/tests/unit/systems/mass_chain_test.cpp` (fill the stub). Target `dsp_systems_tests`. Depends on T011.

- `MassChain_ControlIntervalsAligned` (plan S11): `static_assert` that `BassCompressor::kControlInterval`, `MassDynamics::kControlInterval`,
  `MassLaw::kControlInterval` and `AnchorOscillator::kControlInterval` all equal `ProfundumCore::kControlInterval`; `static_assert(
  AnchorOscillator::kMaxCoreH1Amplitude == HarmonicFold::kCalibrationPeak)`.
- `MassChain_CoreAnchorValidated` (SC-014): Mass ∈ {0, 25, 50, 75, 100} (static, `massAt` constant) × {E0, E1, E2, C3}, reference voice, 5 s.
  (a) `anchorControlsPerInterval[k]` differs from the starting `Controls` only in `level`, and `level == targetsPerInterval[k].anchorLevel`;
  `static_assert(sizeof(MassLaw::Targets) == 6 * sizeof(float))`; a₁ independently: the h1 amplitude of `coreL` by a single-bin DFT over the last
  64 whole periods matches the recorded `a1PerInterval.back()` within **0.05 dB** (the recorded-value equality is a fixture self-check, not
  SC-014 evidence). (b) core taps memcmp-equal with `attachAnchor` false and true. (c) every `outL/R` sample finite (bit test) and |out| ≤
  `getCeilingLinear()`. (d) |mean(outL)| over the last 2 s → 20·log10 ≤ **−80 dBFS** (and outR). (e) RMS(anchorL over the last 2 s) /
  (a₁ / √2) is non-decreasing in the Anchor-level target across the five Mass points; `sumL` memcmp-equal to the float add of `foldL` and
  `anchorL`. Print w₁ of `outL` (recorded only).
- `MassChain_LimiterIdleSteadyState` (SC-015, FR-034): Mass ∈ {0, 25, 50, 75, 100} × the three fixtures (reference and kSawAnchor gated; kSineAnchor recorded — RULED 2026-10-17 G8-6) ×
  {E0, E1, E2, C3}, 3 s renders (`std::async` across cells is allowed). On the limiter's in / out taps (`dcL/R` → `outL/R`) for samples with |in| > 1e-4: (a) from 1 s, GR =
  20·log10(|out| / |in|) ≥ **−0.01 dB**; (b) every cell whose limiter never engaged from note-on (no sample with GR < 0) is memcmp-equal in == out
  from 1 s. Print per cell the attack GR peak (dB) and duration (ms). **Expected red until T019** (harvest-dependent levels).
- `MassDynamics_PartitionInvariant` (SC-018): the full chain at E1, 3 s, a Mass step 20 → 80 and `setFixedReleaseMs(…, 275)` via
  `configureDynamics` at sample **8704**, host blocks {1, 17, 32, 64, 512}: `outL/R` memcmp-equal; two renders from fresh fixtures memcmp-equal;
  no random source exists: the executor greps the four new headers for `<random>` / `rand` and records the (empty) result in the run-log.
- `MassDynamics_SampleRates` (SC-021): fs ∈ {44.1, 48, 88.2, 96, 192} kHz, chain at Mass {0, 50, 100}, E1, 2 s: finite and |out| ≤ ceiling;
  at E1 the `MassDynamics` SC-008(a)(b) gains (burst, A = +12, S = −6) and the SC-006(a) M_sub (the reference voice via `renderChain`, `massAt` ≡ 50, `targetOverride[CompressionAmount]` ≡ 1, keyed at
  E1, 3 s, `mSub` of `subBand(outL).dftDb` over [1 s, 3 s]) are
  within **0.25 dB** of the 48 kHz values; `MassDynamics` `prepare(48k)` → `prepare(96k)` → `prepare(48k)` then a render memcmp-equal to a fresh
  48 kHz one.
- `MassChain_Phase9HandoffRecord` (FR-035): writes `evidence/phase9-handoff.txt`: global latency (`MassDynamics::latencySamples()`), "SC-017 cell:
  see evidence/sc017-cpu.txt (T018 / T024)", the `kRows`, `kAmountTable` and `kDurationTable` contents, the 4-voice Mass 100 poly-sum steady GR
  (`renderPoly` with E1 detuned by {−6, −2, +2, +6} cents, mean GR over [1 s, 3 s], recorded not gated), the limiter attack GR at Mass 100 E1 per
  fixture, the FR-018 corner contract ("call `setDetectorKeyHz(lowestSoundingF0(voices))` once per 32-sample interval; 0 / non-finite = no voice
  → 80 Hz"), and the shaper parameter table (ranges, defaults, FR-009). Asserts only that every number written is finite and the file exists.
- **Verify:** `dsp_systems_tests` `"MassChain_*"`, `"MassDynamics_PartitionInvariant"`, `"MassDynamics_SampleRates"`. Run-log
  `evidence/run-log/T012.txt`.

### T013 [P] Compressor on the chain, and the amount-table harvest (SC-006, SC-011 compressor, SC-022, SC-024(d); FR-011, FR-016, FR-019; plan S8.3, R-4)

**Files:** `dsp/tests/unit/systems/mass_compressor_chain_test.cpp` (fill the stub). Target `dsp_systems_tests`. Depends on T011.

- `MassCompressor_NoSubPumping` (SC-006 (a)(b)(c)(d)): the reference voice through `renderChain`, `massAt` ≡ 50, `targetOverride[CompressionAmount]`
  ≡ 1, keyed at the note, at E0, E1, E2, C3. **Render A** (`seconds = 3.0`): (a) `mSub` over [1 s, 3 s] of both `subBand` readings of `outL`
  ≤ **0.5 dB** (print the LP group delay); (d) mean GR over [1 s, 3 s] (`perSampleDynamicsTaps`) ≥ **3 dB**. **Render B** (`seconds = 4.0`,
  `targetOverride[AnchorLevel]` = 0.25 for n < 2 s and 1.0 from 2 s; every other target unchanged): (b) |mean GR [1.5 s, 2 s] − mean GR [3 s,
  3.5 s]| ≤ **1 dB**. (c) Positive control: the same two renders with `MassDynamicsProbe::setHpBypass(compressor(md), true)` via
  `configureDynamics`: at E0, (a) or (b) exceeds its bound (`CHECK`, verdict printed). If (b) exceeds 1 dB at E0 with the HP, write a `DEFECT:`
  line naming plan R-4 (the `BiquadCascade<2>` fallback inside `mass_dynamics.h`, owned by T020), never a relaxed bar.
- `MassCompressor_RectifiedFeedNegativeControl` (SC-006(e)): the same renders with `setRectifiedFeed(true)`: it must fail (b) at E2 or (d) at E0
  or E1; print each note's verdict; if it passes everywhere, `FAIL("fixture lacks teeth")`.
- `MassCompressor_MakeupMatchesGr` (SC-022, FR-016): the S8.3 calibration voice (`massAt` ≡ 50, `targetOverride[CompressionAmount]` ≡ amount,
  `seconds = 3.0`, steady GR = mean over [1 s, 3 s]); for amount ∈ {0, 0.25, 0.5, 0.75, 1} (the five table rows) at E1 and C3: |row makeup −
  steady GR| ≤ **0.5 dB**; amount 0: makeup == 0.0f and GR == 0.0f.
- `MassCompressor_Audibility` (SC-011 compressor): base kSawAnchor at E1, `massAt` ≡ 50, `preDynamicsGainDb` = −12 dB for n < 2 s and 0 dB from
  2 s (the two-level step), 4 s, `targetOverride[CompressionAmount]` ≡ 0 and ≡ 1: ΔL_step(0) − ΔL_step(1) ≥ **3 dB** (segments [1 s, 2 s] and
  [3 s, 4 s]); print steady GR (recorded only).
- `MassCompressor_KeytrackedCornerPoly` (SC-024(d)): `renderPoly({E1, C3}, …)` and `renderPoly({C3, E1}, …)`: `detectorCornerHz()` settles within
  **1 %** of 2·E1 by 200 ms in both orders; `CHECK` that a maximum rule (2·C3) would miss the 1 % window (the test discriminates).
- `MassCompressor_AmountTableHarvest` `[.calibration]` (plan S8.3; **run only by the main loop in T019**): with
  `MassDynamicsProbe::overrideAmountTable` on a candidate table (via `configureDynamics`): (1) bisect the amount-1 threshold over [−60, 0] dB so the
  minimum over {E0, E1, E2, C3} of the calibration voice's steady GR is 4.0 ± 0.05 dB (S8.3 target 1); (2) the law's Mass 100 compression amount
  (`MassLaw::evaluate(100, Targets{}).compressionAmount`, 0.5 under C-7 (ii)) must give steady GR(100) ≥ 3.5 dB on the Mass 100 voice (all six
  targets from the law) at E1 and C3: lower that row's threshold (and, if needed to stay monotone, the rows above it) until met (target 2);
  (3) intermediate thresholds linear in amount between harvested rows, steady GR asserted monotone in amount; (4) makeupDb per row = the mean of
  the E1 and C3 steady GR, with the E1 / C3 spread ≤ 0.8 dB (target 3). Slope and knee keep the seed. Write `evidence/amount-table.txt`:
  `BEGIN-INITIALIZER amount-table`, the five rows in the header's exact form (`std::setprecision(9)`), `END-INITIALIZER amount-table`, then the
  steady GR per row at E0, E1, E2, C3, the Mass 100 GR at E1 and C3, and the makeup residual per row and note. A target the harvest cannot meet
  prints `STOP AND SURFACE: <target> <measurement>` (FR-048), never a moved bar.
- **Expected red until T019:** `MassCompressor_MakeupMatchesGr`, `MassCompressor_NoSubPumping`, `MassCompressor_RectifiedFeedNegativeControl`
  (seed thresholds and makeup).
- **Verify:** `dsp_systems_tests` `"MassCompressor_*"`. Run-log `evidence/run-log/T013.txt`.

### T014 [P] Mass law spectral grids and the law harvest (SC-001(b)(c), SC-003; FR-024, FR-028; plan C-1, S8.2, R-6)

**Files:** `dsp/tests/unit/systems/mass_law_spectral_test.cpp` (fill the stub). Target `dsp_systems_tests`. Depends on T003, T011.

- **Repeat noise ε** (plan S13.2): per cell and descriptor, max |Δ| over 8 `noteOnSample` offsets k · round(P / 8) × host blocks {64, 512} at the
  cell's Mass 0 and Mass 100 points, capped at **0.1 dB** (dB descriptors) / **0.005 oct** (centroid). Every ε is printed.
- `MassLaw_RenderedTargetsMonotonic_<Target>_<Note>` `[long]` (SC-001(b); 14 TEST_CASEs: Target ∈ {Depth, Body, Duration, Fold, Compression,
  AnchorLevel, OutputTrim (G8-5)} × Note ∈ {E1, C3}): `massAt` ≡ 0 and all seven targets overridden (the compression lever's others at their
  Mass 50 base values, G8-7) — the driven target k gets `evaluateRow(kRows[k], m, base_k)`,
  every other target j its Mass 0 value `evaluateRow(kRows[j], 0, base_j)`; m = 0, 1, …, 100; `descriptorSeconds(fs)` renders, descriptors on the
  last `kLowFrequencyFftSize` samples of `outL`: Depth → w₁/Σw (non-increasing), Body → E_low, Duration → D_tr (from note-on; window
  `dTrWindow(fs, 250)`; shaper +6 / 0), Fold → THD, Compression → ΔL_step on the two-level step (base kSawAnchor, `preDynamicsGainDb` −12 dB then
  0 dB at 1 s + 131072 samples, non-increasing; steady GR recorded only), AnchorLevel → w₁ of `sumL`. Every adjacent step has Δ ≥ −ε in the
  documented direction.
- `MassLaw_RenderedTargetsMonotonicSmoke`: the six targets at E1 at m ∈ {0, 50, 100} with ε = the caps; the documented direction holds across
  the three points.
- `MassLaw_FundamentalStrengthJointSweep_<Fixture>_<Note>` `[long]` (SC-001(c); 12 TEST_CASEs: Fixture ∈ {Ref, Saw, Sine} × Note ∈ {E0, E1, E2, C3}):
  all six targets from the law (no overrides), m = 0…100, w₁ of `sumL` (the fold + anchor sum tap; RULED 2026-10-17, Group 8 G8-2, spec SC-001(c) row)
  at every Mass point ≥ its Mass 0 value − 6 dB − ε (bounded fall; RULED 2026-10-17 G8-4, spec SC-001(c) row; ε for w₁ measured at Mass 50 of
  that cell); adjacent steps recorded.
- `MassLaw_FundamentalStrengthJointSweepSmoke`: Ref at E1, m ∈ {0, 25, 50, 75, 100}. **Expected red until T019.**
- `MassLaw_LevelSlopeBounded_<Fixture>_<Note>` `[long]` (SC-003; 12 TEST_CASEs; the Sine shards record, Ref and Saw gate — RULED 2026-10-17, Group 8
  G8-2, spec SC-003 row: the 2·f₀ sidechain cannot see a sub-only voice, so the makeup alone spans ≈ 5.7 dB): `constexpr std::array<bool, 4> kGateKWeightAt{false, false, true,
  true};` for {E0, E1, E2, C3} (RULED 2026-10-17, plan S1 C-1 (a): K-weighted span and slope gated at E2 and C3, E0 / E1 recorded; the unweighted 6 dB span gated at every note). m = 0…100: P = `TestUtils::kWeightedLevelDb(outL, n, fs,
  n − kLowFrequencyFftSize)`, L = whole-period RMS of the same frame; where gated, span(P) ≤ **3 dB** and max |P(m+1) − P(m)| ≤ **0.2 dB**; span(L)
  ≤ **6 dB** everywhere. P printed at every cell. The E0 / E1 shards are predicted to fail the K-weighted bar (C-1: ≈ 9 dB / 3.9 dB); that is an
  FR-048 measurement for the user (Defects found P-2).
- `MassLaw_LevelSlopeBoundedSmoke` (deviation 10): Ref at E2 and C3, m ∈ {0, 25, 50, 75, 100}: span(P) ≤ 3 dB, span(L) ≤ 6 dB, adjacent |ΔP| / 25
  ≤ 0.2 dB. **Expected red until T019.**
- `MassLaw_TableHarvest` `[.calibration]` (plan S8.2; **run only by the main loop in T019, after the amount table is pasted**): via
  `ChainConfig::rowsOverride` on a candidate `kRows` copy, on an 11-point lattice m ∈ {0, 10, …, 100} × 3 fixtures × 4 notes (`std::async` across
  the 12 cells): (0) the largest Anchor travel (`amountDown` = `amountUp`) in [0, 0.5] (12-step bisection) whose full-lattice unweighted
  span(L) stays ≤ 6 dB on every Ref / Saw cell (SC-003; Sine recorded, G8-2); (1) the largest Depth |`amountUp`| and |`amountDown`| in [0, 0.5]
  (direction falling, D-2; each half bisected separately) keeping w₁ of `sumL` (G8-2) within 5.5 dB of its Mass 0 value on every cell (SC-001(c) as re-ruled, G8-4; lower half first) — RULED 2026-10-17 (Group 8,
  R-6 / FR-021: the Anchor's level is relative to h1 and cannot carry a Depth → 0 fundamental; the 2026-10-17 seed run read w₁ falling
  21 dB from Mass 30 to 100); (2) the smallest Fold `amountUp` in [0, 50] and Body
  `amountUp` in [0, 0.5] meeting THD(100) ≥ THD(50) + 6.5 dB, THD(50) ≥ THD(0) + 6.5 dB and E_low(50) ≥ E_low(0) + 3.5 dB on the reference voice
  at E1 and C3 (SC-002 + 0.5 dB margin), then repeat (1), at most 3 rounds; (3) SC-003 against the spec bar on all four notes (K-weighted span
  ≤ 3 dB, |ΔP| ≤ 0.2 dB / unit measured at every Mass unit on the gated cells (M-1, 2026-10-18), unweighted span ≤ 6 dB), bending Fold or Anchor to `SCurve` only if |ΔP| needs it; record the best E0 / E1
  K-weighted span reached (FR-048 measurement). Directions never change (D-2); every candidate obeys the C-7 cap. Write `evidence/mass-law-table.txt`:
  `BEGIN-INITIALIZER mass-law-rows`, the six rows in the header's exact form, `END-INITIALIZER mass-law-rows`, then every check per cell. If no
  table meets SC-001(c) and SC-003 (unweighted) together, print `STOP AND SURFACE: FR-021 ruling needed (R-6) <measurements>` and write the best
  table found.
- **Verify:** `dsp_systems_tests` `"MassLaw_*Smoke"` (gates exclude `[long]`). Run-log `evidence/run-log/T014.txt` with the shard list.

### T015 [P] Mass anchors, automation continuity, audibility, neutral render (SC-002, SC-005, SC-011 Mass, SC-023 rendered; FR-023, FR-025, FR-027)

**Files:** `dsp/tests/unit/systems/mass_law_automation_test.cpp` (fill the stub). Target `dsp_systems_tests`. Depends on T011.

- `MassLaw_AnchorPoints` (SC-002): reference voice at E1 and C3, Mass {0, 50, 100}, `descriptorSeconds(fs)`, `perSampleDynamicsTaps`: THD(0) ≤
  **−30 dB** and GR(0) == 0.0f at every sample; THD(50) ≥ THD(0) + **6 dB** and E_low(50) ≥ E_low(0) + **3 dB**; THD(100) ≥ THD(50) + **6 dB**
  and mean GR(100) over [1 s, end] ≥ **3 dB**. **Expected red until T019.**
- `MassLaw_AutomationContinuous_<Note>` `[long]` (SC-005(a); Note ∈ {E0, E2, C3}): base kSawAnchor; Mass steps 0 → 100 and 100 → 0 at 2 s and
  20 ms host ramps (`massAt` linear over 20 ms), host blocks {64, 512}, 4 s: `differentialClicks(transition, refStart, refEnd, n, fs, f0).counted
  == 0` (refStart / refEnd = static renders at the start / end Mass, same note-on). Positive controls per arm and note, each flagged ≥ 1
  (`CHECK` on the transition render multiplied by a hard +12 dB switch at the step sample; the `unsmoothedMass = true` step is recorded — G8-7).
- `MassLaw_AutomationContinuousSmoke`: E2, the 0 → 100 step at block 512, plus the hard +12 dB switch positive control (gated) and the
  `unsmoothedMass` control (recorded, G8-7).
- `MassLaw_StaticLattice_<Note>` `[long]` (SC-005(b); E0, E2, C3): 101-point static sweep, base kSawAnchor, `descriptorSeconds(fs)`, descriptors on
  the last frame: adjacent |ΔL| ≤ **0.5 dB**, |ΔC| ≤ **0.1 oct**, |ΔE_low|, |ΔE_high| ≤ **1 dB**; every other SC-001(b) descriptor's largest
  adjacent step printed (w₁/Σw, THD, w₁ of the sum, GR).
- `MassLaw_StaticLatticeSmoke`: E2, m ∈ {0, 1, 2, 49, 50, 51, 98, 99, 100}, the same bars on the three adjacent pairs of each run.
- `MassLaw_AutomationDescriptorTrajectory_<Note>` `[long]` (SC-005(c); E0, E2, C3): computes the (b) lattice itself (shards run concurrently, so it
  does not read (b)'s output), then renders the 0 → 100 host step (the 40 ms law traversal) and the 20 ms host ramp; `periodDescriptors` over
  one-period windows sliding one period at a time across the traversal; each per-window change of every SC-001(b) descriptor ≤ (b)'s largest
  adjacent step × (Mass units traversed in that window) + ε.
- `MassLaw_AutomationDescriptorTrajectorySmoke`: E2, with the (b) maxima taken from a 9-point lattice {0, 12.5, …, 100} scaled to per-unit.
- `MassLaw_Audibility` (SC-011 Mass, FR-043): reference voice at E1: `ShapeCharacterTest::separate(describe(Mass 0), describe(Mass 100)).distinct`;
  each target alone (the SC-001(b) override scheme) moved from its Mass 0 to its Mass 100 value moves its SC-001(b) descriptor by ≥ **3 dB**
  (duration as D_tr at the FR-009 defaults; compression as ΔL_step on the step fixture). Print every figure. **Expected red until T019.**
- `MassLaw_NeutralAtFiftyRendered` (SC-023 rendered): the reference voice and kSawAnchor at E1 at Mass 50 vs `massLawDetached`: every tap
  memcmp-equal; edge bases (`lawBase` depth 1 at Mass 100; fold 100 at Mass 0) rendered and their targets printed.
- **Verify:** `dsp_systems_tests` `"MassLaw_AnchorPoints" "MassLaw_*Smoke" "MassLaw_Audibility" "MassLaw_NeutralAtFiftyRendered"`. Run-log
  `evidence/run-log/T015.txt` with the shard list.

### T016 [P] Anti-aliasing at E0–E2 (SC-010; FR-042; plan C-3)

**Files:** `dsp/tests/unit/systems/mass_dynamics_aliasing_test.cpp` (fill the stub). Target `dsp_systems_tests`. Depends on T011.

- `enum class Sc010Method { Spec, Residual }; constexpr Sc010Method kSc010Method = Sc010Method::Residual;` (RULED 2026-10-17, plan S1 C-3: the residual-against-ideal method gates; the spec-literal reading may be printed beside it).
- Settings (MassDynamics standalone, per-sample taps): shaper duration 5 ms, A ∈ {+12, −12}, S = 0; compressor amount 1 (fastest table attack,
  release floor 50 ms), key = f₀. Inputs: kSawAnchor and `ShapeCharacterTest::fold100(kSawAnchor)`, at E0, E1, E2. Three windows per cell: steady
  [1 s, 2 s] after onset; burst onset [0, 100 ms] (`TransientShaperTest::burst`); step transition [step, step + 100 ms] (`twoLevelStep`, step at
  2 s). Gain trace = shaper gain + compressor gain (dB).
- **Spec method:** `ProfundumTest::aliasedPower` on in and out at the same frame (frame = the largest power of two ≤ the window); gate A_out ≤
  max(**−80 dBFS**, A_in + **0.5 dB**). A frame with f₀ / binHz ≤ 17 (the ±8-bin lattice exclusion covers every bin) is flagged `vacuous`,
  printed, and fails its `CHECK` (FR-048: not a pass).
- **Residual method (printed beside it; the gate once C-3 is ruled):** steady `aliasedPower` on an `aliasFftLength(fs, f0)` frame starting at
  1 s, A_out ≤ max(−80, A_in + 0.5); `aliasResidualDb` ≤ **−80 dBFS** over both moving windows; the hard 0 → +12 dB positive control reads
  > −80 (the step is applied to the Fold-100 C3 saw: M-2, 2026-10-18). FFT length and bin Hz printed per cell for both methods. The Fold-100
  cells are recorded, not gated (user ruling 2026-10-18, G9-1; spec SC-010 row).
- `MassDynamics_AliasingE0E2_<Rate>` `[long]` (Rate ∈ {44k1, 48k, 96k}; one shard each). `MassDynamics_AliasingE0E2Smoke`: 48 kHz, E1, kSawAnchor,
  A = +12, both methods (the E1 spec frame is not vacuous: 32768 points, 1.46 Hz bins).
- **Verify:** `dsp_systems_tests` `"MassDynamics_AliasingE0E2Smoke"`. Run-log `evidence/run-log/T016.txt`.

### T017 [P] Non-finite safety: compressor, MassDynamics, MassLaw (SC-019; FR-017, FR-029)

**Files:** `dsp/tests/unit/systems/mass_dynamics_nonfinite_test.cpp` (fill the stub; in the `-fno-fast-math` block). Target `dsp_systems_tests`.
Depends on T010.

- `MassDynamics_NonFinite` `[systems][profundum][mass-dynamics][nonfinite]`: for `BassCompressor` (amount 1, key E1) and `MassDynamics` (A = +12,
  S = −6, amount 1, key E1): kSawAnchor E1 stereo 1 s steady, then 16-sample bursts of NaN, +Inf, −Inf (bit patterns as T008), on L only then on
  both, 0.5 s apart: every output finite (bit test); every output sample whose input was non-finite is exactly `0.0f`; `stateFinite()` after 512
  samples; 100 ms after each burst the one-period level of L is within **0.1 dB** of the pre-burst window (BassCompressor arm; the MassDynamics
  arm spaces its bursts 1 s apart and gates at **600 ms**, 100 ms past the longest D-5 release, with the 100 ms reading recorded — RULED
  2026-10-17, Group 7 gate, spec SC-019 row). Non-finite `setAmount`,
  `setDetectorKeyHz` (= "no voice": the corner goes to 80 Hz, it does not keep the previous key — FR-018), `setTransientDurationMs`,
  `setTransientAttackDb`, `setTransientSustainDb`, `setCompressionAmount` keep the previous value (except the key, as stated). `MassLaw`:
  `setMass` and each `setBase` field with each pattern keep the previous value; `evaluate(NaN, b)` memcmp-equal to `evaluate(50, b)`; a non-finite
  base field in `evaluate` uses the `Targets{}` default.
- **Implementation:** none expected; a red is a `DEFECT:` line (rule 11).
- **Verify:** `dsp_systems_tests` `"MassDynamics_NonFinite"`. Run-log `evidence/run-log/T017.txt`.

### T018 [P] CPU budget TU and lane script (SC-017; FR-045; deviation 9) — **lane deferred to the main loop**

**Files:** `dsp/tests/unit/systems/mass_dynamics_perf_test.cpp` (fill the stub), `specs/profundum-phase6-mass-dynamics/evidence/close/cpu-lane.sh`.
Target `dsp_systems_tests`. Depends on T010.

- `MassDynamics_CpuBudget` `[systems][profundum][mass-dynamics][performance][.perf]`: 48 kHz, host block 64, a distinct L / R kSawAnchor E1 pair
  (R = L delayed by round(0.37 · P) samples from a longer render); `MassDynamics` at A = +12, S = −6, duration 40, amount 1, key E1, plus one
  `MassLaw::advanceInterval()` + `evaluate` per 32 samples. 10 s of audio, best of 5, `std::chrono::steady_clock`; % = wall / audio × 100. Gate
  **≤ 1.0 %**. Record (not gated): `DCBlocker` ×2 + `TruePeakLimiter` per 10 s, the L == R cell, one silent second. Write
  `evidence/sc017-cpu.txt` and print `SC017 VERDICT PASS|FAIL <x.xxx> %` and `SC017 GATED <cell>`.
- `cpu-lane.sh` (copy the structure of `specs/profundum-phase5-shape-character/evidence/close/cpu-lane.sh`): refuses to start while `tasklist |
  grep -ciE "cl.exe|dsp_.*_tests.exe|clang-tidy"` is non-zero; runs `pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe
  build/windows-x64-release/bin/Release/dsp_systems_tests.exe -ExeArgs "MassDynamics_CpuBudget"` to `evidence/run-log/close/cpu-lane.log`; tails
  it; appends the verdict line to `evidence/run-log/T018.txt`. A red reading is re-run once alone after the machine has idled; only then is it a
  defect (never relax).
- **Executor:** write both files, `bash -n cpu-lane.sh`; return done with notes "deferred to the main loop at close: run `bash
  specs/profundum-phase6-mass-dynamics/evidence/close/cpu-lane.sh` after everything else (T024)".

---

## Group 8 — calibration checkpoint (single task; STOP AND SURFACE)

### T019 Harvest lane and paste script; the main loop runs the three harvests (FR-008, FR-016, FR-022, SC-022; plan S8; deviation 6)

**Files:** `specs/profundum-phase6-mass-dynamics/evidence/close/harvest.sh`, `specs/profundum-phase6-mass-dynamics/evidence/close/paste-harvest.js`,
`specs/profundum-phase6-mass-dynamics/evidence/run-log/T019.txt`. Depends on T006, T013, T014.

- `harvest.sh` (Git Bash; copy the structure of `specs/profundum-phase5-shape-character/evidence/close/harvest.sh`; every step to its own log
  under `evidence/run-log/close/`, a status line per step with exit code and wall clock to `evidence/run-log/close/harvest-status.txt`):
  1. One rebuild of `dsp_processors_tests dsp_systems_tests` (full CMake path; build-log warning count must be 0).
  2. **Concurrently:** `dsp_processors_tests.exe "TransientShaper_DurationTableHarvest"` and `dsp_systems_tests.exe
     "MassCompressor_AmountTableHarvest"`. Any `STOP AND SURFACE` line in either log → print it, exit 3.
  3. `node …/paste-harvest.js duration-table amount-table`, then one rebuild of both exes.
  4. `dsp_systems_tests.exe "MassLaw_TableHarvest"` (the law harvest needs the pasted amount table, S8.3 target 2). `STOP AND SURFACE` → print,
     exit 3.
  5. `node …/paste-harvest.js mass-law-rows`, then one rebuild of both exes.
  6. Run the harvest-dependent per-push cases by name to one log and print each summary line: `MassCompressor_MakeupMatchesGr`,
     `MassCompressor_NoSubPumping`, `MassCompressor_RectifiedFeedNegativeControl`, `MassChain_LimiterIdleSteadyState`, `MassLaw_AnchorPoints`,
     `MassLaw_Audibility`, `MassLaw_FundamentalStrengthJointSweepSmoke`, `MassLaw_LevelSlopeBoundedSmoke`, `TransientShaper_DurationMatches`,
     `TransientShaper_DurationShortestCell`.
- `paste-harvest.js` (Node, CommonJS, no dependencies): for each name given — `duration-table` → `evidence/duration-table.txt` →
  `dsp/include/krate/dsp/processors/transient_shaper.h`; `amount-table` → `evidence/amount-table.txt` → `systems/mass_dynamics.h`;
  `mass-law-rows` → `evidence/mass-law-table.txt` → `systems/mass_law.h` — reads the lines between `BEGIN-INITIALIZER <name>` and
  `END-INITIALIZER <name>` and replaces the text between `// HARVEST-BEGIN <name>` and `// HARVEST-END <name>` with them, keeping the marker lines
  and the indentation. Exit 1 unless each marker pair occurs exactly once in its header, the initializer count matches (11 / 5 / 6 lines) and
  every number parses as a finite float with an `f` suffix. Prints a before / after summary per line. Adds a header comment line inside the
  markers: `// harvested <ISO date> from evidence/<file> (plan S8)`. `--self-test` runs it on synthetic copies in a temp directory and checks
  the exit codes of a good file, a missing marker, a duplicated marker, a wrong line count and a malformed value.
- **Executor:** write both; `bash -n harvest.sh`; run `node …/paste-harvest.js --self-test` (seconds) and record its output in
  `evidence/run-log/T019.txt`. Then **return status "blocked"** — STOP AND SURFACE: "main loop runs `bash
  specs/profundum-phase6-mass-dynamics/evidence/close/harvest.sh` (estimate: the duration and amount harvests a few minutes concurrently, the law
  harvest 10–30 min with its 12-cell fan-out on the 32-core machine; measure and record the wall clocks), takes any `STOP AND SURFACE` line to
  the user (S8.2 R-6 / FR-021 ruling, S8.3 harvest targets), otherwise confirms step 6 is green, records the readings in
  `evidence/run-log/T019.txt`, and resumes with `unblock: [T019]`". This group's gate then runs the whole per-push tree on the harvested
  tables; the "expected red until T019" allowance no longer applies.

---

## Group 9 — defect resolution (sequential, conditional)

### T020 Resolve every `DEFECT:` line and gate-documented red

**Files:** any Phase 6 file named by a `DEFECT:` line (headers included); this file's "Defects found" section.

- Read `evidence/run-log/T001.txt` … `T019.txt` and the gate notes of Groups 1–8. Copy every `DEFECT:` line into "Defects found" below, then fix
  the root cause in the owning file with a failing test first. Rules: never weaken a bar or delete a failing test; a bar proven unreachable is
  STOP AND SURFACE with the measurement (FR-048); a needed probe accessor is added to its probe header here (the only edit allowed to a probe
  header after T007 / T010). The plan R-4 fallback (`BiquadCascade<2>` in place of each detector `Biquad`, inside `mass_dynamics.h` only, never a
  reused header) is applied here if T013 recorded the E0 SC-006(b) shortfall it names.
- Re-tag any untagged case the gate logs show above 15 s as `[long]` and add its `…Smoke` sibling (deviation 10).
- Pre-triaged items P-1…P-3 below are user rulings, not code defects: leave them unless the main loop has recorded a ruling, then apply exactly
  the one-constant change the precondition table names.
- If there are no `DEFECT:` lines and no reds, write "none" under "Defects found" and return done.

---

## Group 10 — integration (sequential; last)

### T021 CMake registration audit (single task)

- `grep -c` each of the sixteen Phase 6 TU paths in `dsp/tests/CMakeLists.txt`: exactly 1 each, except
  `unit/processors/transient_shaper_nonfinite_test.cpp` and `unit/systems/mass_dynamics_nonfinite_test.cpp` = 2 (list + `-fno-fast-math` block);
  `mass_dynamics_perf_test.cpp` is in no `set_source_files_properties` block. `dsp/CMakeLists.txt` lists each of the four new headers exactly once,
  in its layer's list. `git diff 02fecee1 -- dsp/CMakeLists.txt dsp/tests/CMakeLists.txt` shows only added lines. Record in
  `evidence/run-log/T021.txt`. A missing or duplicated line is fixed here (this is the only task allowed to edit the CMake files after T002).

### T022 Full suites, `[long]` lane and clang-tidy (SC-020; plan S14.2) — **deferred to the main loop**

**Files:** `specs/profundum-phase6-mass-dynamics/evidence/close/suites.sh`, `specs/profundum-phase6-mass-dynamics/evidence/close/tidy.sh`.

- `suites.sh` (copy the structure of `specs/profundum-phase5-shape-character/evidence/close/suites.sh`; designed to run **detached**, writing
  `evidence/run-log/close/status.txt` with one line per lane — name, exit code, wall clock — so it survives the 2 h background-task ceiling): one
  rebuild of `dsp_processors_tests dsp_systems_tests` (record the wall clock; `grep -ciE "warning C|warning:"` must be 0); then concurrently
  (a) both per-push lanes with the filter `"~[long]~[performance]~[perf]~[benchmark]~[!benchmark]"` and `-D 15` (every case over 15 s printed),
  (b) the `[long]` lane: one shard per `[long]` TEST_CASE of both exes, the list generated with `<exe> --list-tests
  "[long]~[performance]~[perf]~[benchmark]~[!benchmark]" --verbosity quiet` (never hand-kept; about 45 shards: SC-001(b) 12, SC-001(c) 12, SC-003
  12, SC-005 9, SC-010 3), run through `xargs -P 24`, each shard to `evidence/run-log/close/long/<exe>__<case>.log`. Then `git diff --stat 02fecee1
  -- dsp/include` (+ untracked files under `dsp/include`) must name only the four new headers (SC-020, FR-044), written to the status file. A
  summary block extracts the SC-003 E0 / E1 K-weighted spans and the SC-010 vacuous-cell list from the shard logs (the C-1 / C-3 FR-048 readings
  for the user).
- `tidy.sh` (copy the structure of the Phase 5 `tidy.sh`): reconfigure `build/windows-ninja` inside vcvars64 (`"$CMAKE" --preset windows-ninja`;
  the test CMake lists changed — memory: stale compile database), check all sixteen Phase 6 TUs are in `compile_commands.json`, then
  `"/c/Program Files/LLVM/bin/clang-tidy.exe" -p build/windows-ninja <TU>` per TU, concurrently, with a 600 s ceiling each (the four headers and
  the five test headers are checked through the TUs), to `evidence/run-log/close/tidy.log`, last line `TIDY GREEN|RED …`. Not the whole-`dsp`
  sweep (a 15 h hang on an unrelated TU is on record).
- **Executor:** write both scripts and `bash -n` them; return done with notes "deferred to the main loop at close: after T019 and T020, run
  `suites.sh` detached and `tidy.sh` concurrently; quote each lane's last lines into `evidence/run-log/T022.txt`; any red, Phase 6 or not, is
  owned (fix, re-run only the affected lane)".

### T023 Portability and Apple globals (FR-046; SC-020)

**Files:** `specs/profundum-phase6-mass-dynamics/evidence/close/portability.sh`, `specs/profundum-phase6-mass-dynamics/evidence/run-log/T023.txt`.

- `portability.sh` (copy the structure of the Phase 5 `portability.sh`): concurrently, each with a 300 s ceiling, `node tools/lint-apple-globals.js`
  over the four new headers, the five new test headers (`k_weighting.h`, `transient_shaper_test_helpers.h`, `transient_shaper_probe.h`,
  `mass_dynamics_probe.h`, `mass_dynamics_test_helpers.h`) and the sixteen TUs, and `node tools/check-portability.js` over the sixteen TUs; then
  `wsl --shutdown` (check-portability boots WSL; WSLg traces otherwise keep writing to C:). Output `evidence/run-log/close/portability.log`, last
  line `PORTABILITY GREEN|RED`.
- **Executor:** write it and run it once (each lane is minutes at most). If both report 0 findings, quote the log in `evidence/run-log/T023.txt`
  and return done. A finding in a Phase 6 file is fixed here (the group gate rebuilds). If the run fails with `wsl: Failed to translate` (the
  sandbox does not mount the repo drive) or exceeds 5 minutes, stop it and return done with notes "deferred to the main loop at close: run
  `bash specs/profundum-phase6-mass-dynamics/evidence/close/portability.sh` unsandboxed".

### T024 CPU lane alone (SC-017) — **deferred to the main loop**

- Precondition: T019 and T022–T023 lanes finished, machine idle. Executor: return done with notes "deferred to the main loop at close: run
  `bash specs/profundum-phase6-mass-dynamics/evidence/close/cpu-lane.sh` (T018) last, alone; quote the figures in `evidence/run-log/T024.txt`".

### Close procedure (main loop, in this order)

1. `run-bench.sh` alone (T001) before Group 1, if not already recorded.
2. `harvest.sh` (T019) at the Group 8 checkpoint. A `STOP AND SURFACE` line goes to the user before anything else runs.
3. `suites.sh` detached + `tidy.sh` concurrently (T022); `portability.sh` if T023 deferred it.
4. Any red → triage into "Defects found", fix, re-run only the affected lane. The C-1 / C-2 / C-3 readings go to the user as FR-048 measurements
   (P-1…P-3) unless already ruled.
5. `cpu-lane.sh` alone (T018 / T024), after the machine has idled.
6. Comply (outside this task list) quotes the evidence files, the harvested tables and the run-log readings; the FR-035 handoff
   (`evidence/phase9-handoff.txt`) goes into the roadmap's Phase 9 notes there.

---

## Defects found

*(T020 and sequential tasks append here: case, output, diagnosis, owning file.)*

**T020 (2026-10-17): none.** No `DEFECT:` line in `evidence/run-log/T001.txt` … `T019.txt` (T005, T008, T010, T012, T014, T015,
T017 "none"; T011, T016 "No DEFECT lines"; T013 names only the conditional R-4 line). R-4 not triggered: `MassCompressor_NoSubPumping` E0
with the HP reads SC-006(b) |diff| 0.288862 dB < 1 dB (`evidence/run-log/close/harvest-step6-cases.log:26`, `G8-5-rest.txt:46`), so the
`BiquadCascade<2>` fallback is not applied. Gate-documented reds G2-1, G6-1, G7-2, G7-3, G8-1…G8-7 are ruled below; G7-1 is fixed. Every gate
re-measure log ends "All tests passed" (G2-1, G6-1, G7-2, G7-3, G8-5-chain / -masslaw / -rest) and harvest step 6 is red 0. No gate log
shows an untagged case above 15 s, so nothing is re-tagged `[long]`. P-1…P-3 are ruled and left alone.

**Pre-triaged (predicted by the plan's model; user rulings, not code defects — gate fixers leave these alone):**

- **P-1 (plan S1 C-2, SC-008(c)):** `TransientShaper_DurationShortestCell` E1-sine cell, predicted +30.0 % against ±15 % (plan S12.2: the sine's
  first peak arrives 6.07 ms after the onset). RULED 2026-10-17 (C-2 (a)): the cell is recorded, `kGate5msAt = {false, true}`.
- **P-2 (plan S1 C-1, SC-003):** `MassLaw_LevelSlopeBounded_*_E0` and `_*_E1` `[long]` shards, K-weighted span predicted ≈ 9 dB (E0) and ≈ 3.9 dB
  (E1) against 3 dB, caused by the BS.1770 RLB high-pass. RULED 2026-10-17 (C-1 (a)): recorded at E0 / E1, `kGateKWeightAt = {false, false, true, true}`.
- **P-3 (plan S1 C-3, SC-010):** `MassDynamics_AliasingE0E2_*` spec-method cells at E0 (and any other frame with f₀ / binHz ≤ 17) are `vacuous` and
  fail by design (not a pass). RULED 2026-10-17 (C-3): the residual method gates, `kSc010Method = Sc010Method::Residual`.
- **P-4 (deviation 6):** the "Expected red until T019" cases of T012–T015 depend on harvested tables; they are real reds only from T019's
  gate on.

**Group 2 gate (2026-10-17), awaiting a user ruling. This is a test-bar premise conflict, not a code defect:**

- **G2-1 (T004, `PeriodPeakDetector_SteadyOnNonIntegerPeriod`, wide-saw arm):** the held ripple is 1.43e-3 at 44.1 kHz (481 partials),
  1.44e-3 at 48 kHz (524 partials) and 7.39e-4 at 96 kHz (1048 partials), against the 1e-4 bar. That is 0.012 / 0.012 / 0.006 dB peak-to-peak.
  The 64-partial arm passes: 1.25e-5, 1.08e-5, 1.55e-6.
  - **Diagnosis:** the 1e-4 bar was copied from `HarmonicFold_DetectorSteadyOnNonIntegerPeriod` (harmonic_fold_test.cpp:1675-1708). That test
    reads `detectorLevel()`, the output of the follower, at a 125 ms hold (harmonic_fold.h:95). The follower only releases while the raw level is
    below `env·(1 − 0.005)` (harmonic_fold.h:99, :433), so it hides raw ripple up to 5e-3. `PeriodPeakDetector` deliberately has no follower
    (plan S3; T004 "No follower here"), and T004 sets a 50 ms hold, which is about 2 periods at E1. Its true-peak and ring code matches
    harmonic_fold.h:403-427 line for line.
  - **Measured:** a scratch probe of this header (F:/tmp/ppd_probe) on the same wide-saw fixture, with only the hold changed to 125 ms, reads
    4.37e-5 / 4.55e-5 / 4.45e-5. All three pass.
  - The 1e-4 bar is not a spec SC. The spec-level ripple bounds downstream are SC-009 (0.2 dB) and SC-006(a). The test and the code are left
    unchanged.
  - **Options for the user** (as T004's run-log listed them):
    - (a) gate the wide arm at the 125 ms hold;
    - (b) gate the 64-partial arm and record the wide arm;
    - (c) set a bar for the raw held value, for example 2e-3.
  - **RULED 2026-10-17 (user, recommended option; spec FR-004 row first, Clarifications "Group 2 gate ruling", plan C-4 / R-11):** (c) with
    the bar at 2.3e-3 = 0.02 dB peak-to-peak (one tenth of SC-009's 0.2 dB) on the wide arm at the consumers' 50 ms hold; the
    64-partial arm keeps 1e-4. Test amended (`kBar64`, `kBarWide`); code unchanged. Gate re-measured with `gate_epochs {"Group 2": 5}`.

**Group 8 checkpoint (2026-10-17), T019 harvest STOP AND SURFACE, RULED:**

- **G8-1 (`MassLaw_TableHarvest`, R-6 / FR-021):** with the S8.2 seed no table met SC-001(c): the sum's w₁ fell 21 dB from Mass 30 to 100
  on the reference voice at every note (worst step −4.5 dB vs 0.1); Anchor at both interval ends and Fold amountUp 0 did not help; THD step
  17.7 dB vs 6.5 regardless. Root cause: `AnchorOscillator` scales its output by the core's h1 amplitude (anchor_oscillator.h:340), so the
  Anchor adds at most (1 + level) while Depth → 0 removes ≈ 20 dB of h1. Also red: SC-003 K-weighted span(P) 3.3 / 3.7 dB at E2 / C3, sine E0
  span(L) 8.2 dB (6.0 dB from the Anchor doubling a sine the 2·f₀ sidechain cannot see). **RULED 2026-10-17 (user, recommended option; spec
  FR-021 row first, Clarifications "Group 8 harvest ruling", plan S8.2 / R-6, T014):** Depth and Anchor travel are harvest levers, order
  Anchor travel (SC-003 L) → Depth travel (SC-001(c)) → Fold / Body (SC-002) → SC-003 K check; directions, targets and bars unchanged.
  `MassLaw_TableHarvest` steps (0) and (1) rewritten; `mlsBisectLargest`, `mlsWorstSpanL` added. Harvest re-run by the main loop.
- **G8-2 (second run, 226 s, 6456 renders):** Anchor travel squeezed to 0.18 by the Sine fixture's span(L) (6.0 dB: the 2·f₀ sidechain
  cannot see a sub-only voice, so the makeup's +5.84 dB is unopposed; Sine span(P) 5.65 dB at E2 / C3 gated FAIL), and the output-tap w₁ still
  fell with zero Depth travel (Ref −1.66 dB at Mass 50 from Body / Fold thinning; Saw −2.0 dB at Mass 90 from GR exceeding the reference-voice
  makeup). **RULED 2026-10-17 (user, recommended option; spec SC-001(c) and SC-003 rows first, Clarifications G8-2, plan S8.2 / S13.3):**
  SC-001(c) reads w₁ at the fold + anchor sum tap (`sumL`); SC-003 gates Ref and Saw and records Sine; step (0) bisects the Anchor travel
  against the Ref / Saw span(L). `MlsFixture::sc003Gated` added; `mlsHarvestRender` returns both taps.
- **G8-3 (third run, 30 s, 916 renders):** Depth travel bisected to zero and the sum's w₁ still fell (Ref −1.28 dB at Mass 40 → 50 from
  Body / Fold; Saw −2.22 dB at 90 → 100 from Body alone), Mass 0 no longer pure (Ref THD(0) −5.9 dB, SC-002 THD(50) − THD(0) 6.38 < 6.5).
  **RULED 2026-10-17 (user, recommended option; spec FR-031 / FR-032 / SC-014(a) rows first, Clarifications G8-3, plan S6 / S9, T012):**
  the chain keys the Anchor to the recipe's h1 of the voice's own controls (pre-bias) — `unbiasedH1Amplitude(base, f0, fs)` in the
  helpers, used by `renderChain` and `renderPoly`; `MassChain_CoreAnchorValidated` (a) now checks the fed a₁ is Mass-independent and equals
  the delivered h1 at Mass 50 within 0.05 dB, recording the ratio elsewhere. Harvest re-run by the main loop.
- **G8-4 (fourth run, 33 s, 916 renders):** Depth frozen, Anchor unbiased at full travel, the sum's w₁ still fell per step (Ref −0.78 dB at
  Mass 40 → 50; Fold → 100 cost the sine fixture −4.1 dB at Mass 100) and the arithmetic (sine-like Mass 0 ≈ 1.86 × a₁ in h1; Mass 100
  ≈ 0.09 × + Anchor ≤ 1 ×) shows a non-decreasing sum w₁ cannot coexist with SC-002's pure Mass 0. **RULED 2026-10-17 (user, recommended
  option; spec SC-001(c) row first, Clarifications G8-4, plan S8.2 / S13.3, T014):** SC-001(c) = bounded fall, w₁ ≥ w₁(Mass 0) − 6 dB at
  every point (harvest margin 0.5 dB), steps recorded. `mlsJointSweepRun`, `mlsWorstFallFromZero`, the harvest step (1) and the verdict
  amended (`kMlsW1FallMaxDb`, `kMlsHarvestW1FallDb`). Harvest re-run by the main loop.
- **G8-5 (fifth run, 35 s, 820 renders):** SC-001(c), SC-002 and SC-003 (L) pass; SC-003 K-weighted span(P) on the Saw fixture at E2 / C3
  4.03 / 3.84 dB (bar 3; Ref 2.87 / 2.73) with every lever spent. **RULED 2026-10-17 (user, recommended option; spec FR-021 row first, FR-024,
  D-2, FR-033, SC-001(a)(b), SC-011, SC-023, Clarifications G8-5, plan S6 / S7.1 / S8.2 / S13.3, T009 / T014):** the FR-021 output trim is the
  law's seventh, calibrated target: `Target::OutputTrim`, `kNumTargets = 7`, `MassDynamics::setOutputTrimDb` (5 ms smoothed gain after the
  compressor, exactly 1.0 at 0 dB), chain and poly fixtures call it per interval, the harvest's step (2c) fills the row from the reference
  voice's K-weighted P at the gated notes, two new SC-001(b) shards (`OutputTrim_E1 / _C3`, L non-increasing), SC-011 records the lever,
  every target table / switch / static_assert grew to seven, the paste script expects 7 rows (self-test 10 / 10).
- **G8-6 (seventh-target build):** `MassChain_LimiterIdleSteadyState` red on kSineAnchor at Mass 100 (steady GR −1.43 / −0.22 / −0.12 / −0.50 dB
  at E0 / E1 / E2 / C3 against −0.01; Ref and Saw idle): the G8-2 blind spot (makeup unopposed on a sub-only voice + the Anchor doubling).
  **RULED 2026-10-17 (user, recommended option; spec SC-015 row first, Clarifications G8-6, plan S13.3, T012):** the sine fixture is recorded
  for SC-015 (`McFixture::sc015Gated`). Ref / Saw attack GR at Mass 100 (7–9.5 dB for ≈ 0.5 s) recorded as a Phase 9 listening item.
- **G8-7 (main-loop measurement refinements, told to the user; spec SC-001(b), SC-005(a), SC-011 rows, Clarifications G8-7, T014 / T015):**
  `MassLaw_Audibility`'s compression lever read 0.03 dB (others at Mass 0 → the saw purified by the Depth bias, invisible to the 2·f₀
  sidechain under the harvested thresholds): the compression lever's others now sit at their Mass 50 base values in `mlaOverrideScheme` and
  `mlsDrivenConfig`. `MassLaw_AutomationContinuousSmoke`'s unsmoothed-Mass positive control counted 0 once the amount table was pasted: the
  hard +12 dB switch is the gated positive control (smoke and [long]), the unsmoothed step recorded.

**Group 6 gate (2026-10-17), T011 stop-and-surface, RULED:**

- **G6-1 (T011 `MassDynamicsHelpers_PeriodDescriptors`, sine arm):** the rectangular fractional-period DFT floors THD near −50 dB on a pure
  sine (E1 −54 … −73 dB, C3 −47 … −53 dB across window starts) against `describe()`'s float floor, so the 0.1 dB bar on E_low / THD could not
  be met; saw, L and C passed. **RULED 2026-10-17 (user, recommended option; spec SC-005(c) row first, Clarifications "Group 6 gate ruling",
  plan S13.3):** resample each one-period window onto ⌊P⌋ points (Kaiser sinc, half-width 32, β = 10) before an exact periodic DFT; bar kept
  on every arm after the −60 dB floor clamp, plus raw sine THD ≤ −70 dB. Model (scratch `pd-resample-model.js`): sine THD −122 dB, saw within
  1e-4 dB of analytic. Helper and self-test amended by the main loop; `periodBin` removed (unused after the change).

**Group 7 gate (2026-10-17):**

- **G7-1 (FIXED, code defect in a reused header): `MassChain_CoreAnchorValidated` SC-014(c) and `MassDynamics_SampleRates` SC-021,
  |out| above the limiter ceiling.** 4–14 samples per Mass 50 render were one float ulp (5.96e-8) above `getCeilingLinear()`.
  - **Diagnosis:** `TruePeakLimiter::processChunk` applies `currentGain_ = ceilingLin_ / tp` to a sample with |sample| == tp, and
    `inL * (ceilingLin_ / tp)` can round one ulp above `ceilingLin_`. This breaks the exact guarantee in the header comment
    (true_peak_limiter.h:12-14).
  - **Reproduced** by the new `TruePeakLimiter sample-level ceiling is exact (no float overshoot)` case in
    `dsp/tests/unit/processors/true_peak_limiter_test.cpp` (2000 settled DC levels): 1086 samples over, worst +5.96e-8.
  - **Fix:** the output product is clamped to ±`ceilingLin_`. The clamp does nothing when the gain is 1. Outputs change by at most
    1 ulp, and only on samples that were already over the ceiling.
  - **Shared header:** dsp_processors / systems / effects and the seraphis / membrum / vorago suites were rebuilt and re-run.
- **G7-2 (AWAITING A USER RULING; measurement premise, not a code defect): `MassDynamics_AliasingE0E2Smoke` burst residual −67.08 dBFS
  against −80.** A hidden scratch probe (removed afterwards) split the trace:
  - shaper gain only: −67.09; compressor gain only: −157.98.
  - The shaper gain steps 0 → +12 dB at onset + 1. This is plan S4.3 step 5's silence branch (`s ≤ 1e-7` → c = (f > 1e-7) ? 1 : 0;
    before the onset c = 0, so the sustain gain applies).
  - Every sample before that step is exactly 0, so y = 4·x exactly and the audio has no gain discontinuity. The residual metric
    still band-limits g on its own, and the step's Gibbs ringing lands in the "ideal" product.
  - With the trace held at its first post-onset value over the exact-zero pre-roll, the same render reads **−120.67 dBFS**. The
    output and the gain at every non-zero sample are unchanged. The c-clamp kink and the slow-follower tick kinks are below
    −120 dB.
  - Options:
    - (a) in `aliasResidualDb`, hold the gain trace over leading exact-zero input. The gain applied to a 0 sample cannot be
      observed. Recommended.
    - (b) change the shaper's silence branch to c = 1 (attack gain pre-armed in silence). This deviates from plan S4.3 step 5 and
      from the FR-008 "sustain outside the transient" reading of silence.
    - (c) record the burst window instead of gating it.
  - **REFINED 2026-10-17 (main loop, measurement scope under C-3; spec SC-010 row, Clarifications "Group 7 gate rulings", plan C-3):**
    (a) — `aliasResidualDb` holds the gain trace over leading exact-zero input at its value on the first non-zero sample. Code unchanged.
- **G7-3 (AWAITING A USER RULING; spec premise conflict): `MassDynamics_NonFinite` MassDynamics arm, burst 3 (NaN on L+R at 2.5 s):
  −0.42 dB at 100 ms against ±0.1 dB.**
  - Burst 3 lands at the saw's peak (x = 0.5086 → 16 zeros → 0.4445). FR-007 requires the detectors to see the substituted 0.0f.
    The resulting hard step makes the held true-peak overshoot +0.98 dB (m 0.5088 → 0.5703) for one 50 ms hold.
  - The shaper treats that as an onset with a proportional share (c ≈ 0.08, +1.47 dB for about 60 ms), as FR-008 specifies. The
    compressor downstream deepens its GR from −9.745 to −10.49 dB.
  - The D-5 release under sustained compression is about 500 ms, so the GR is still 0.51 dB deep at +100 ms and reaches 0.1 dB
    only at about +235 ms.
  - Each stage alone recovers: shaper alone back to −6.000 dB by +75 ms; the BassCompressor arm reads |Δ| ≤ 0.0043 dB on every
    burst. Bursts 4 and 5 (zeros at other saw phases) pass.
  - The 100 ms / 0.1 dB bar therefore conflicts with FR-007 (the detectors see 0.0f), FR-004 (true peak), FR-008 (proportional
    share) and D-5 (release) acting together on a peak-phase dropout.
  - Options:
    - (a) for the MassDynamics arm, measure recovery after the compressor's release, for example at 500 ms. The BassCompressor and
      shaper arms keep 100 ms.
    - (b) have the detectors skip sanitised samples (hold the last finite value). This changes FR-007's "the detector pair … see
      that same substituted 0.0f".
    - (c) record the arm instead of gating it.
  - **RULED 2026-10-17 (user, recommended option; spec SC-019 row first, Clarifications "Group 7 gate rulings", plan S13.3, T017):** the
    MassDynamics arm measures at 600 ms (100 ms past the longest D-5 release) with its bursts 1 s apart and the 100 ms reading recorded;
    the BassCompressor arm keeps 100 ms. `mdnfBurstArm(proc, label, spacingS, recoveryS)`; every burst is snapped to the saw's positive
    peak within the period after its nominal onset (at 1 s spacing burst 3 had moved off the peak and read −0.002 dB at 100 ms, which
    would have made the ruling vacuous). Code unchanged.

**Group 9 close (2026-10-18; spec Clarifications "Group 9 close rulings"):**

- **G9-1 (SC-010 / FR-042; RULED, user, recommended option; spec rows first):** `MassDynamics_AliasingE0E2_{44k1,48k,96k}` red on every Fold-100 cell
  (moving windows −63 … −78 dBFS at 44.1 / 48 kHz, −75 at 96 kHz; E2 steady −46 / −55 dBFS at 44.1 / 48 kHz), every plain `kSawAnchor` cell green by
  ≥ 14 dB. Cause: gain modulation of an input whose content reaches Nyquist, and the 4× true-peak estimate's sub-sample-phase wobble on the Fold-100 wave
  at E2 (period 582.47 samples at 48 kHz → gain at f₀ / 2). Ruled: `kSawAnchor` gated, Fold-100 recorded; Phase 9 handoff (listen; chain budget decides
  oversampled gain application). Owning file: `mass_dynamics_aliasing_test.cpp` (gating by fixture).
- **G9-2 (FR-008 / SC-008(c); RULED, user, recommended option):** `evidence/duration-table.txt` 7.5 ms E1-sine cell +19.17 % (bar ±15 %). Ruled: C-2
  extends to the 7.5 ms cell (recorded); the harvest prints `STOP AND SURFACE` for any other cell beyond 15 %. Owning file: `transient_shaper_test.cpp`
  (`TransientShaper_DurationTableHarvest` labels).
- **G9-3 (FR-044; RATIFIED by the user):** the G7-1 `TruePeakLimiter` clamp stays; `suites.sh` step 3 lists `true_peak_limiter.h` as expected.
- **M-1 (SC-003 / FR-028; main-loop refinement under plan S8.2 step 3):** `MassLaw_LevelSlopeBounded_Ref_C3` |ΔP| 0.2178 dB per unit at Mass 54 → 55
  (bar 0.2) while the harvest's lattice slope (10-unit average) read 0.10 and never bent. Fix: `MassLaw_TableHarvest` measures |ΔP| at every Mass unit on
  the gated cells; re-run of `harvest.sh`. Owning file: `mass_law_spectral_test.cpp`.
- **M-2 (SC-010 positive control; harness repair under plan C-3):** −82.5 dBFS at 96 kHz on the plain C3 saw (must exceed −80). Fix: the hard step is
  applied to the Fold-100 C3 saw. Owning file: `mass_dynamics_aliasing_test.cpp`.
- **M-3 (FR-045 / SC-017; main-loop implementation refinement):** shipped `MassDynamics_CpuBudget` 1.047 / 1.183 / 1.270 % (isolated, pinned) against
  1.0 %. Profile (`bench/stage-bench.cpp`): six run-time `gainToDb` / `dbToGain` series per sample pair ≈ 60 % of the cost. Fix: `detail::gainToDbFast` /
  `dbToGainFast` (`std::log2` / `std::exp2`) for the three conversions the Phase 6 headers own; `setTransientDurationMs` early-out. Profile after 0.802 %.
  Owning files: `transient_shaper.h`, `mass_dynamics.h`. Gate: `cpu-lane.sh` alone at close (T024).
- **M-4 (SC-005(c) smoke; main-loop harness refinement):** `MassLaw_AutomationDescriptorTrajectorySmoke` L 1.816 dB / ratio 1.05 on the E2 host step
  after the Fold SCurve bend (the `[long]` E2 shard, 101-point lattice, ratio 0.43). The smoke's 9-point proxy lattice becomes 21 points. Owning file:
  `mass_law_automation_test.cpp`.
- **G9-4 (SC-020; RULED, user 2026-10-18):** suites.sh ran every `[long]` case of both executables (175 shards, 14660 s); the one red,
  `EcosystemEngine_HostileFuzzStaysBounded`, failed only its 60-minute wall-clock budget (73.9 min) under the concurrent lane and does not
  consume `true_peak_limiter.h`. Ruled: the gate is scoped to the change set; the red is recorded as load, not re-run; next phase's suites.sh
  lists `[long]` cases from the phase's TUs only. Owning file: `evidence/close/suites.sh` (next phase's copy).
