# Tasks: Profundum Phase 3 — Sub Anchor

**Spec:** `specs/profundum-phase3-anchor/spec.md` (Clarified 2026-10-11) · **Plan:** `specs/profundum-phase3-anchor/plan.md`
(S0–S13 + review notes) · **Model:** `specs/profundum-phase3-anchor/anchor-model.js`, `bench/anchor-kernel-bench.cpp` ·
**Phase base:** commit `9b1a4cf5` on `synth/profundum`
**Status:** Task list (2026-10-11). The build stage has not started. **Blocked on plan S1 ratification (below).**

## Read this first (every executor)

### Precondition: ratification of plan S1 (BLOCKING)

Plan S1 says its fourteen items (C-1 … C-14) must be ratified **before** tasks run (`plan.md:6`, heading `:57`). On 2026-10-11
the spec's Clarifications log (`spec.md` "## Clarifications", Session 2026-10-11) holds Q1–Q7 and D7 only: **no S1 item is
ratified yet.** The tasks below encode the plan's **recommended** resolution of each. The main loop must ratify them with
the user and write each ruling into the spec's Clarifications log **before T004 baselines the bars**, including the
SC-024(c) bound in the SC-024 row (the spec requires "the ratified bound is written into this row before build starts").
Every test that relies on a C-item cites it in a comment (`// plan S1 C-9`). If the user rules differently, change the
listed tasks before you execute them; never relax a bar on your own.

| Item | What the tasks assume (plan S1) | Tasks affected if ruled otherwise |
|---|---|---|
| C-1 | Triangle = Clenshaw over `kTriangleTerms` = 16 odd terms (n ≤ 31), each weighted by the recipe cap taper at n·f_T | T005, T011, T016, T017 |
| C-2 | `kAnchorToH1` = 1.0, `kDefaultLevel` = 0.5, SC-024(c) bound **RMS rise at the default level ≤ 3.6 dB** on every listed state; Phase 9's a₁ source is option (a) `deliveredGains()[0] · kCenterPanGain` | T007, T012, T018, T021 |
| C-3 ⚠ | FR-041 re-worded: the Hermite bridge length N_b is **shortened** to the largest N ≤ N_nominal whose cubic stays in [0, max(B, g₀)] (integer bisection) | T013 |
| C-4 | `modeGain()` returns `double`; extra observers `phaseIncrement()`, `isSounding()`, `pendingControls()`, test-only `mirrorState()` | T007, T013, T014, T015 |
| C-5 | The S5 coefficient mirror is bit-faithful to the core/bank float chain (not a double re-derivation) | T007, T009 |
| C-6 | Asymmetric-sine onset ramp = raised cosine over one period of f₀ on the h2 term only | T011 |
| C-7 | Before the first `noteOn` the Anchor writes zeros and keeps only trajectory bookkeeping | T007 |
| C-8 | Controls latch at offset 0 of the next block (after a pending noteOn); a waveform switch during a crossfade is queued (latest wins); `punchBoostDb` / `punchTimeMs` / `ghostTimeMs` read at arm time; re-arm beats mode switch in one block; no standalone `rearm()` | T011, T013 |
| C-9 ⚠ | SC-012(b) span gated at **≥ 40 dB − 1e-4 dB** (re-ruling with evidence under FR-065) | T004, T019 |
| C-10 | SC-022-pass amendment for the "Phase 11 300–399 block" goes into roadmap Phase 11's paragraph | T003, T021 |
| C-11 | Sanitise = `isFinite(x) ? clamp(flushDenormal(x), lo, hi) : previous`; SC-015 denormal read-back per field via `pendingControls()` (`level`, `punchBoostDb` → 0.0f; `punchTimeMs` → 5.0f; `ghostTimeMs` → 1.0f) | T015 |
| C-12 | SC-004(b) measured as: first sample after the peak with g == 1.0 **bitwise** is N_rise + N_time ± 1, g[N_rise + N_time − 1] > 1, every later sample within 0.01 dB | T013 |
| C-13 | SC-024(d) is a structural assertion in Phase 3; its teeth are SC-024(b) at level 1 and at `kDefaultLevel` plus the static API check | T012 |
| C-14 | SC-012(a): harmonic dB values floored at −90 dB re w₁ before differencing; sine/octave-down separated by the octave clause; positive control "same waveform twice = not separated" | T008, T019 |

### Deviations from the plan made while cutting these tasks (surface them; do not silently revert)

1. **CMake registration is the first code task (T006), not the last.** Every later task is test-first and must build its
   new TU; an unregistered TU cannot fail. This is the Phase 1 and Phase 2 precedent
   (`specs/profundum-phase2-edge-fold/tasks.md` "Deviations" item 1, Group 2 T006). The final group re-audits the
   registration as a single task (T022).
2. **`dsp/CMakeLists.txt` gains the new header** in `KRATE_DSP_SYSTEMS_HEADERS` (after `include/krate/dsp/systems/profundum_core.h`,
   `dsp/CMakeLists.txt:188`, before the closing `)` at `:189`). Plan S10.1 says "No library CMake change", but Phases 1 and 2
   registered their headers there (`:158`, `:160–161`, `:187–188`). No target change.
3. **SC-013's header path reuses the existing `KRATE_DSP_TESTS_DIR`** compile definition of `dsp_systems_tests`
   (`dsp/tests/CMakeLists.txt:720–721`, value `${CMAKE_CURRENT_SOURCE_DIR}` = `dsp/tests`): the path is
   `std::string(KRATE_DSP_TESTS_DIR) + "/../include/krate/dsp/systems/anchor_oscillator.h"`. Plan S10.1 allowed this ("unless
   the T001 sweep finds an existing helper macro to reuse"); no new `COMPILE_DEFINITIONS` line is added.
4. **Catch2 filter fix.** Plan S10.2's per-push filter `"[anchor],[anchor-chain]~[long]~[.perf]"` parses as `[anchor]` **or**
   (`[anchor-chain]` and not `[long]`), so it would run every `[anchor][long]` case. The tasks use
   `"[anchor]~[long],[anchor-chain]~[long]"` (hidden `[.perf]` cases never run unless named).
5. **A FreeRunning noteOn updates δ at offset 0.** The core applies a FreeRunning noteOn's new f to the bank immediately
   (`profundum_core.h:587–596`: `setBase(f)`, `refreshMultipliers(f)`, `deliverToBank` inside `applyNoteOn`, which runs at
   offset 0, `:242–243`), not at the next grid sample. Plan S4.6 does not say when δ follows a FreeRunning noteOn. To keep
   the lock, T009 makes `applyNoteOn`'s FreeRunning branch call `frequencyUpdate(heldRatio_)` after
   `mirrorFreeRunningNoteOn(f)` so the new δ is in force from offset 0 (a Reset noteOn already gets it from the grid sample
   at offset 0, since the grid restarts there).
6. **Intermediate unsmoothed gain.** T007 applies `level` and a₁ as snapped values so T012 can add the `OnePoleSmoother`s
   test-first (a failing click/delta test). The final design is plan S4.9 unchanged.
7. **The CPU slow-path bench runs first (T005)**, before the header exists, instead of at plan step 10. Plan S9.8 requires
   it "before the SC-018 gate"; doing it first means the cents-domain weight cache (the plan's remedy) is decided before
   T011 writes `refreshTaper`.
8. **SC-025 lives entirely in T014** (FOLLOW) instead of being split with plan step 3. SC-003(d) (octave-down lock) and the
   non-sine arms of SC-026(b) land in T011 with the waveforms.
9. **Line anchors corrected.** Plan S0 cites `kCenterPanGain` `:51` and `kPowerTarget` `:52` in `spectral_shape_recipe.h`;
   the file has them at `:52` and `:53` (`:51` is `kCoreOutputRmsDb`). Use the corrected numbers.
10. **`clang++` is not on PATH.** It is at `/c/Program Files/LLVM/bin/clang++.exe` (listed 2026-10-11). Use that full path in
    T005.

### File map

| File | Kind | Layer / target | Created by / edited by |
|---|---|---|---|
| `dsp/include/krate/dsp/systems/anchor_oscillator.h` | new header (includes L0–L2 only) | L3 | T006 stub; T007, T009, T011–T014; T017/T020 only on a red gate |
| `dsp/tests/unit/systems/anchor_oscillator_test_helpers.h` | new test header, namespace `Krate::DSP::ProfundumTest` | — | T007, T008 (append only after) |
| `dsp/tests/unit/systems/anchor_oscillator_test.cpp` | new TU | `dsp_systems_tests` | T006 stub; T007, T008, T009, T011, T012, T013, T014, T019 |
| `dsp/tests/unit/systems/anchor_oscillator_spectral_test.cpp` | new TU | `dsp_systems_tests` | T006 stub; T011, T012, T014, T019 |
| `dsp/tests/unit/systems/anchor_oscillator_nonfinite_test.cpp` | new TU, **`-fno-fast-math`** | `dsp_systems_tests` | T006 stub; T015 only |
| `dsp/tests/unit/systems/profundum_anchor_chain_test.cpp` | new TU (core + Fold chain), `[anchor-chain]`; 60 s cases `[long]` | `dsp_systems_tests` | T006 stub; T009, T011, T012, T018 |
| `dsp/tests/unit/systems/anchor_oscillator_perf_test.cpp` | new TU, `[.perf]`, **not** `-fno-fast-math` | `dsp_systems_tests` | T006 stub; T016 only |
| `dsp/CMakeLists.txt` | existing, one list entry | — | T006 |
| `dsp/tests/CMakeLists.txt` | existing, systems list + `-fno-fast-math` block | — | T006 |
| `specs/profundum-phase3-anchor/check-footprint.js` | new Node script | SC-019 + layer grep | T002 |
| `specs/profundum-phase3-anchor/check-handoff.js` | new Node script | SC-021 | T003 |
| `specs/profundum-phase3-anchor/check-bars.js`, `bars-baseline.json` | new Node script + baseline | SC-020 | T004 |
| `specs/profundum-phase3-anchor/bench/anchor-kernel-bench.cpp` | existing phase artifact | S9.8 | T005 |
| `specs/Profundum-roadmap.md` | existing roadmap | FR-066 | T021 |

**Untouchable (zero diff lines vs `9b1a4cf5`, FR-063 / SC-019) unless T010 (the FR-032 fallback) runs:** everything under
`dsp/include/` except the new `anchor_oscillator.h` — in particular `systems/profundum_core.h`, `processors/harmonic_fold.h`,
`processors/harmonic_oscillator_bank.h`, `processors/harmonic_oscillator_bank_simd.h`/`.cpp`, `processors/harmonic_types.h`,
`processors/spectral_shape_recipe.h`, `primitives/smoother.h`, `primitives/polyphase_halfband_oversampler.h`,
`core/db_utils.h`, `core/math_constants.h`, `core/phase_utils.h`, `processors/sub_oscillator.h`; and every Phase 1/2 test
file: `dsp/tests/unit/systems/profundum_core_test.cpp`, `profundum_core_spectral_test.cpp`, `profundum_core_nonfinite_test.cpp`,
`profundum_core_perf_test.cpp`, `profundum_core_excitation_test.cpp`, `profundum_core_excitation_spectral_test.cpp`,
`profundum_core_excitation_nonfinite_test.cpp`, `profundum_edge_fold_chain_test.cpp`, `profundum_edge_fold_perf_test.cpp`,
`profundum_core_test_helpers.h`, `profundum_excitation_test_helpers.h`, `profundum_phase1_core_ref.h`;
`dsp/tests/unit/processors/spectral_shape_recipe_test.cpp`, `spectral_shape_recipe_test_helpers.h`, `harmonic_fold_test.cpp`,
`harmonic_fold_nonfinite_test.cpp`, `harmonic_fold_test_helpers.h`; `dsp/tests/unit/primitives/polyphase_halfband_oversampler_test.cpp`.

### Verified facts these tasks depend on (each read on 2026-10-11)

Paths relative to `dsp/include/krate/dsp/` unless stated.

- **`ProfundumCore`** (`systems/profundum_core.h`, 1190 lines): banner `:2` "Layer 3: System Component"; FTZ/DAZ precondition
  `:89–91`; `class ProfundumCore {` `:92`; `enum class RetriggerPhase : std::uint8_t { Reset, FreeRunning }` `:94`;
  `kDefaultPartials = 64` `:96`; `kControlInterval = 32` `:97`; `kPitchUpdateInterval = 16` `:98`; `kMinF0Hz = 8.0f` `:99`;
  `kDefaultF0Hz = 55.0f` `:100`; `kMaxBaseStepSemitones = 0.9f` `:103`; `kMinSampleRate = 22050.0` / `kMaxSampleRate = 192000.0`
  `:104–105`; `prepare(double, int = kDefaultPartials)` `:129`; `reset()` `:157`; `setControls(const SpectralShapeRecipe::Controls&)`
  `:169`; `setFrequency` `:191`; `setRetriggerPhase` `:197`; `noteOn(float)` `:204`; `processBlock(float*, float*, std::size_t,
  const float* f0PerSample = nullptr)` `:233–234` (null/zero no-op `:235–236`, zero-fill before prepare `:237–241`,
  `applyNoteOn` at offset 0 `:242–243`, pitch sample at every multiple of U `:253–260`, `controlUpdate` iff `intervalPhase_ == 0`
  else `pitchUpdate` `:261–264`, run never crosses a U boundary `:266`, end-of-block trajectory bookkeeping `:285–287`);
  observers `deliveredGains()` `:294` (span, size numPartials), `maxShapeStepPerInterval()` `:297`, `currentFrequency()` `:298`,
  `maskFrequency()` `:299`, `baseFrequency()` `:300`, `isPrepared()` `:302`; `ExcitationControls` `:330`, `setExcitation` `:352`;
  `controlUpdate(float)` `:448–507` (step 3 `fChanged` / `chaseBase` `:473–475`, step 4 mask `:478–482`, step 6 `:492–502`);
  `pitchUpdate` `:511–517`; `applyNoteOn()` `:520–604` (Reset branch `:528–561`: `bank_.reset()`, `detuneShadow_.fill(1.0f)`,
  `intervalPhase_ = 0`, `buildMask`, `setBase(f)`, `refreshMultipliers(f)` `:560`, `deliverToBank(true)`; FreeRunning branch
  `:562–598` with `setBase(f)` (no chase), `refreshMultipliers(f)` `:593`/`:596`; `sounding_ = true` `:602`); `chaseBase` `:666–687`;
  `setBase` `:690–697` (ε_base in double); `computeEpsilonTargets` `:702–731` (ε* in double, frozen at the cap;
  `dTarget = |eb| > 1e-9 ? float(ε*/eb) : 1.0f`; `multiplierScratch_[k] = dTarget / detuneShadow_[k]`); `refreshMultipliers`
  `:733–739` (`detuneShadow_[k] = detuneShadow_[k] * multiplierScratch_[k]`); `sanitizeF0` `:795–800`; `trajectoryF0` `:805–811`;
  `kPiDouble = 3.14159265358979323846` `:813`; `kMaxBankEpsilon = 1.99` `:814`.
- **Bank** (`processors/harmonic_oscillator_bank.h`): `prepare` sets `inverseSampleRate_ = 1.0f / static_cast<float>(sampleRate)`
  `:132–135`; scalar effective coefficient `std::clamp(epsilon_[i] * detuneMultiplier_[i], -1.99f, 1.99f)` `:732`;
  `recalculateFrequencies` `:1057–1067` (`eps = 2.0f * std::sin(kPi * freq * inverseSampleRate_)`, clamp ±1.99f).
- **`HarmonicFold`** (`processors/harmonic_fold.h`): `kCalibrationPeak = 0.3552343859f` `:83`; API per spec ledger
  (`prepare(double, std::size_t)` `:121`, `setAmount` `:168`, `setFundamentalHz` `:178`, `processBlock(float*, float*, std::size_t)` `:188`).
- **Recipe** (`processors/spectral_shape_recipe.h`): `kCoreOutputRmsDb = -12.0f` `:51`; `kCenterPanGain = 0.7071067811865476f`
  `:52`; `kPowerTarget = 0.25238293779207720f` `:53`; `kNyquistCapFraction = 0.8f`, `kCapTaperCents = 1430.0f`,
  `kCapTailCents = 10.0f`, `kCapTaperDbPerCent = 0.09f` `:72–75`; named states declared `:82–84`; `capFrequency(double)`
  `:93–95` (`0.8f · float(fs) · 0.5f`); `evaluateMask` `:149–185` (taper start `capHz · exp2(−kCapTaperCents/1200)` `:162`,
  tail floor `:163`, per-harmonic cap branch `:173–182`, low-note guard `:155–157`/`:171–172` which the Anchor must **not**
  inherit); `kDefaultControls` "= mid grid" `:225–228`; `kSineAnchor` `:231`, `kSawAnchor` `:237`, `kHeavy` `:241`, `kHollow` `:244`,
  `kGrowl` `:247`.
- **L0/L1:** `detail::flushDenormal` `core/db_utils.h:263`, `kDenormalThreshold = 1e-15f` `:38`, `detail::isFinite` in the same
  header (used by `profundum_core.h:796`); `kPi = 3.14159265358979323846f` `core/math_constants.h:28`;
  `KRATE_HAS_SSE_DENORMAL_CONTROL` `core/scoped_denormal_mode.h:39/43`; `OnePoleSmoother` `primitives/smoother.h:134`,
  `configure(float ms, float fs)` `:160`, `setTarget` `:170` (`ITERUM_NOINLINE`), `getCurrentValue()` `:191`, `process()` `:197`
  (exact snap within `kCompletionThreshold = 0.0001f` `:55`), `snapToTarget()` `:257`.
- **Phase 1 systems helper** (`dsp/tests/unit/systems/profundum_core_test_helpers.h`, includes `systems/profundum_core.h` `:32`,
  namespace `Krate::DSP::ProfundumTest` `:47`): `requireLREqual` `:61`, `coreSampleRate` `:69`, `struct Render` `:78`,
  `renderCore(core, controls, f0, seconds, policy, record, block = kControlInterval, f0Trajectory)` `:90`,
  `rmsDbOverPeriods(x, n, fs, f0, minSeconds = 1.0)` `:126`, `goertzelPhase(x, L, fs, hz)` `:157` (BH7, `atan2`, sample 0 = t 0),
  `renderedHarmonicPowers(x, fs, f0, N)` `:235` (one 262144 frame, Hann main lobe), `describeRendered` `:251`,
  `aliasFftLength(fs, f0)` `:257`, `aliasedPower(x, fftLen, fs, f0)` `:273` (`.aliasedDbfs`, dB re 0.5), `rmsDbfs` `:453`.
  Phase 2 helper `profundum_excitation_test_helpers.h:82` `renderExcited(core, controls, ex, f0, seconds, policy, block, traj)`.
  Processors helper `spectral_shape_recipe_test_helpers.h`: `samplesBitEqual` `:60`, `struct Descriptors` `:77` (`.centroidOct`
  `:85`), `describePowers` `:92`, `gridPoint` `:183`, `midGrid()` `:195`.
- **Shared test helpers** (repo-root `tests/test_helpers/`, included by bare name): `allocation_detector.h` — `startTracking`
  `:53`, `AllocationScope` `:111` whose count is stored **in the destructor** (`:117–119`), so inside a scope use
  `AllocationDetector::instance().startTracking()` / `stopTracking()` (Phase 2 deviation 5); `artifact_detection.h` —
  `ClickDetectorConfig` defaults `:38–44` (`sampleRate = 44100`, `frameSize = 512`, `hopSize = 256`, `detectionThreshold = 5`,
  `energyThresholdDb = −60`, `mergeGap = 5`), `ClickDetector(const ClickDetectorConfig&)` `:101`, `detect` `:130`;
  `low_frequency_metrics.h` — `kLowFrequencyFftSize = 262144` `:64`, `magnitudeSpectrum` `:118`, `harmonicMainLobePower` `:180`;
  `render_fingerprint.h` — `fingerprintRender` `:73`, `compareFingerprints` `:122`; `enable_ftz_daz.h` — `enableFTZDAZ()` `:26–28`.
  Phase 1 FTZ assertion pattern: `dsp/tests/unit/systems/profundum_core_test.cpp:830–832`.
- **CMake** (`dsp/tests/CMakeLists.txt`): `add_executable(dsp_systems_tests` `:331`; Phase 2 systems TUs `:627–631`, list closes
  `:632`; `KRATE_DSP_TESTS_DIR` on `dsp_systems_tests` `:720–721`; the `-fno-fast-math` `set_source_files_properties(` block is
  inside `if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")` `:735` and its last entries are the Profundum lines `:1109–1113`, closed
  by `PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"` `:1114`. `COMPILE_FLAGS` is one string per file: a file may
  appear in only one such call (comment `:1131–1134`). `dsp/CMakeLists.txt`: `KRATE_DSP_SYSTEMS_HEADERS` `:165`, Phase 1 entry
  `:187–188`, closing `)` `:189`.
- **Tools:** `tools/run-cpu-tests.js:56` filter `'[performance],[perf],[.perf],[benchmark],[!benchmark],[long]~[vorago-sweep]'`
  (it runs every `[long]` case of the named exe too), `SETTLE_MS` default 60000 `:69`; `tools/pin-perf-cores.ps1`;
  `tools/check-portability.js`, `tools/lint-apple-globals.js`, `tools/run-clang-tidy.ps1` exist. Phase 2's
  `specs/profundum-phase2-edge-fold/check-footprint.js` and `check-bars.js` (modes `baseline`/`check` with an optional spec
  path, baseline next to the script, `:5–24`) are the patterns for T002 and T004.
- **Roadmap amendment anchors** (`specs/Profundum-roadmap.md`): Phase 4 Note Memory bullet `:349–350` (Retrigger bullet
  `:351–352`); Phase 5 `BassCharacterStage` bullet `:374–379`; Phase 6 Mass law bullet `:400–404` ("the sub/body relationship
  (anchor level)" `:403`); Phase 8 `MorphState` bullet `:452–455`; Phase 9 `ProfundumVoice` bullet `:490–494`, success-criteria
  paragraph `:511–515`; Phase 10 300–399 anchor block `:534`; Phase 11 paragraph `:551–559`.
- **Evidence reproduction (2026-10-11, this session):** all seven `anchor-model.js` modes run in < 1 s each (`mcf`, the slowest,
  0.64 s) and print the plan's figures: `eps` worst double-law 0.168°, worst 1-ulp mirror 0.307°, mirror 0.0000 everywhere;
  `mcf` max |Δθ(60 s) − Δθ(1 s)| 2.57e-3° (C3 48 kHz FMA); `cal` the C-2 table and `kMaxCoreH1Amplitude = 0.3552343859`;
  `tri` K = 16 THD −18.335 dB, crest 1.7101; `onset` P = 1 worst 1.0000, first sample 0; `bridge` min N_b 1 / 95 / 240,
  out-of-range 0; `alias` −74.6 / −62.6 / −77.7 / −95.7 dBFS.
- **ODR sweep (2026-10-11):** `grep -rn "class AnchorOscillator\|AnchorOscillator\b" dsp/include` → 0 hits; no file
  `systems/anchor_oscillator*` exists.

### Commands (Git Bash, full CMake path, always)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests 2>&1 | tee "$LOG" | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "AnchorOscillator_*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[anchor]~[long],[anchor-chain]~[long]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[anchor-chain][long]" 2>&1 | tail -5
```

Filter by test name with a positional argument (tags in `[brackets]`). Never `ctest -R`. Capture any run longer than about
30 s to a log in the scratchpad (`… > "$LOG" 2>&1`) and read the log; never re-run a slow suite for its output. All Phase 3
TUs are in **one** target (`dsp_systems_tests`): never run two builds of it concurrently.

### Rules for every task

1. Failing test **first**: write it, build, watch it fail (a compile error on a missing symbol counts). Then implement,
   rebuild with **zero warnings**, and run the named cases until they pass.
2. Header-only, `namespace Krate::DSP`; the header includes `core/`, `primitives/` and `processors/` headers only (plan S3:
   `core/db_utils.h`, `core/math_constants.h`, `primitives/smoother.h`, `processors/spectral_shape_recipe.h`, plus the standard
   library). It never includes `systems/profundum_core.h`. Every new type is nested in `AnchorOscillator`; no namespace-scope
   helper.
3. PascalCase classes, camelCase functions, `trailing_` members, `kPascalCase` constants. Designated initialisers with `f`
   literals; every double→float conversion an explicit `static_cast`; explicit casts for `size_t`→`uint32_t`; no narrowing in
   brace init. Finiteness via `detail::isFinite` only, never `std::isnan`/`std::isfinite`. Bit comparisons via `std::bit_cast`.
4. Audio-thread methods are `noexcept`, allocation-, lock-, exception- and I/O-free; fixed-size members only; no SIMD.
5. Test helpers are `inline` functions in `Krate::DSP::ProfundumTest`, `anchor`-prefixed; never an anonymous namespace in a header.
6. Tags: `[systems][profundum][anchor]` (anchor-alone TUs), `[systems][profundum][anchor-chain]` (chain TU),
   `[systems][profundum][anchor][.perf]` (perf). `[long]` only for the 60 s SC-003 renders (and any other case measured
   > 15 s), each with a per-push `…Smoke` sibling. **Never `[long]`:** NaN/Inf guards, L == R identity, RT safety, state and
   retrigger rules, the C-13 structural check.
7. `std::memcmp` only for in-process bit identity. No bit-exact float goldens; cross-rate comparisons via `render_fingerprint.h`.
8. Every anchor render goes through `renderAnchor` (which calls `requireLREqual`); every core render with zero pan calls
   `requireLREqual` (SC-007, spec "Every fixture asserts L == R").
9. Notes: E0 = MIDI 16 = 20.6017 Hz, E1 = 28 = 41.2034 Hz, E2 = 40 = 82.4069 Hz, C2 = 36 = 65.4064 Hz, C3 = 48 = 130.8128 Hz,
   C4 = 60 = 261.6256 Hz, C5 = 72 = 523.2511 Hz; compute as `440·2^((m − 69)/12)` in double, cast once to float. Rates
   44.1 / 48 / 96 kHz unless stated; renders at 48 kHz where the spec says nothing.
10. Sample lengths are computed in the test from the header constants: N = max(1, round(ms·fs/1000)). At 48 kHz:
    `kPunchRiseMs` 1 → 48; `punchTimeMs` 5 / 40 / 250 → 240 / 1920 / 12000; `ghostTimeMs` 1 / 5 / 20 / 100 → 48 / 240 / 960 / 4800;
    `kGhostDipMs` 2 → 96; `kModeBridgeMs` 5 → 240; `kWaveformCrossfadeMs` 5 → 240. B = 10^(boost/20): 3 / 6 / 12 dB →
    1.412538 / 1.995262 / 3.981072.
11. Spectral frames follow the spec's descriptor rule: ≥ 262144 settle samples, then one 262144-sample frame into
    `renderedHarmonicPowers`. THD = Σₙ≥₂ wₙ / w₁ (dB).
12. If a test exposes a defect in a file that a **different** task owns and you are in a `[P]` task, do not edit that file:
    record the case, its output and your diagnosis under "Defects found" at the bottom. T020 fixes it.
13. Never relax a threshold or shrink a workload to pass (FR-064, FR-065, CLAUDE.md). A bar proven unreachable goes to the
    main loop with the measurement.

---

## Group 0 — pre-flight (parallel; read-only or new script files only)

### T001 [P] ODR sweep and evidence reproduction (read-only)

- ODR: `grep -rnE "(class|struct|enum class|enum|using) (AnchorOscillator|AnchorRender|AnchorSegmentSpec|C1Report|MirrorState)\b" dsp plugins tests tools`
  and `grep -rn "renderAnchor\|anchorRaisedCosine\|anchorHermite\|anchorCheckC1\|anchorInstFreqHz\|anchorWindowEnergy\|anchorShsF0\|anchorFindMutableStatics\|anchorWaveformsSeparated\|kAnchorToH1\|anchorFoldFeed\|AnchorTest" dsp plugins tests tools`.
  Both must print nothing. `MirrorState`, `C1Report`, `AnchorSegmentSpec` and `AnchorRender` are nested or test-namespace names;
  if any hit appears in `Krate::DSP::ProfundumTest`, rename with the `Anchor` prefix and record it. Confirm no new name is a
  roadmap hazard (`CharacterProcessor`, `SubOscillator`, `StereoField`, `MorphEngine`, `MorphNode`, `TransientDetector`,
  `HarmonicCloud`, `SpectralState`, `MacroMapper`) and that the waveform enum is **not** spelled `SubWaveform`
  (`processors/sub_oscillator.h:60`).
- Evidence: run `node specs/profundum-phase3-anchor/anchor-model.js <mode>` for `eps mcf cal tri onset bridge alias`
  **concurrently**, each to its own scratchpad log, then `wait` (each takes < 1 s). Every figure must match plan S9.1–S9.7 and
  S1 C-1/C-2/C-3 to the printed precision (see "Verified facts" — evidence reproduction). Any mismatch: stop and report.
- Record the sweep commands, their (empty) output and the evidence figures under "Run log".

### T002 [P] SC-019 footprint script `check-footprint.js`

- **Create** `specs/profundum-phase3-anchor/check-footprint.js` (Node, CommonJS, no dependencies), following
  `specs/profundum-phase2-edge-fold/check-footprint.js` (its `execFileSync('git', …)` pattern, one PASS/FAIL/SKIP line per
  check, non-zero exit on any FAIL). Usage: `node specs/profundum-phase3-anchor/check-footprint.js [baseRef] [--fallback]`,
  default `9b1a4cf5`.
- Checks:
  1. **Shared headers:** `git diff --numstat <base> -- dsp/include` prints nothing, and
     `git ls-files --others --exclude-standard dsp/include` lists only `dsp/include/krate/dsp/systems/anchor_oscillator.h`
     (or nothing → SKIP before T006). With `--fallback`: `harmonic_oscillator_bank.h` and `profundum_core.h` may differ but
     `git diff -U0 <base> -- <file>` must contain **no removed line** (no line starting with `-` other than the `---` header)
     — append-only; every other path still 0 lines.
  2. **Phase 1/2 tests:** zero diff lines (`git diff --numstat` empty) for every test file in the "Untouchable" list above
     (17 files). A missing file = FAIL.
  3. **Layer grep:** every `#include` line of `anchor_oscillator.h` is either `<krate/dsp/core/…>`, `<krate/dsp/primitives/…>`,
     `<krate/dsp/processors/…>` or a standard header (no `/`); none contains `systems/` or `effects/`. Missing header → SKIP.
  4. **API grep (C-13 static teeth):** `anchor_oscillator.h` contains neither the token `ProfundumCore` outside comments nor any
     public method whose parameter list takes `const float*` other than `processBlock`'s two trajectory pointers (scan the
     public section between `public:` and `private:`; report each signature).
  5. Print `git diff --stat <base> -- dsp/include` and the untracked list for comply.
- **Verify now:** on today's tree checks 1, 2 and 5 PASS and 3–4 report SKIP. Positive control: append a comment line to
  `dsp/include/krate/dsp/primitives/dc_blocker.h`, run, confirm check 1 FAILs, then `git checkout -- dsp/include/krate/dsp/primitives/dc_blocker.h`
  (that file only). Record both outputs under "Run log".

### T003 [P] SC-021 handoff script `check-handoff.js`

- **Create** `specs/profundum-phase3-anchor/check-handoff.js` (Node). Usage:
  `node specs/profundum-phase3-anchor/check-handoff.js <pass|fail> [roadmapPath]`, default `specs/Profundum-roadmap.md`;
  the first argument is SC-022's recorded outcome.
- Section scoping (by headings, never by line numbers): a phase section runs from its `### Phase N:` heading to the next
  `### ` heading. Within it:
  - **P9-SC** = from the line starting `**Success criteria:**` to the next blank line in Phase 9's section;
  - **P5-BCS** = the bullet starting ``- `BassCharacterStage` `` up to the next line starting `- ` or `**Success`;
  - **P4-NM** = the bullet starting `- **Note Memory**` up to the next line starting `- ` or `**Success`;
  - **P6-MASS** = Phase 6 section text before its `**Success criteria:**` line;
  - **P8-MS** = the bullet starting ``- `MorphState` `` up to the next `- ` line;
  - **P11** = Phase 11's whole section; **P9** = Phase 9's whole section.
- Required strings (case-sensitive; `–` is U+2013): P9-SC contains `within 1 dB`, `every Character algorithm`, `E0–E2`,
  `FOLLOW`, `pitch-moving stage`, `capability`, `after Character`, `Drop`. P5-BCS contains one sentence (split on `. `)
  holding `r-law` **and** each of `Rectify`, `Crush`, `Fracture`. P4-NM contains `re-arm`. P6-MASS contains `h1 amplitude`.
  With `pass`: `anchorFoldFeed` occurs in P9, P8-MS and P11. With `fail`: `anchorFoldFeed` occurs in **none** of the three.
- Output one PASS/FAIL line per string per section; exit non-zero on any FAIL.
- **Verify now** (teeth): on today's roadmap, `check-handoff.js fail` must FAIL every P9-SC / P5-BCS / P4-NM / P6-MASS line
  (none of the strings is in its scoped section yet; note `after Character` already exists at `:492` in the voice bullet,
  outside P9-SC, and must still FAIL) and PASS the three `anchorFoldFeed`-absent lines. Record the output under "Run log".

### T004 [P] SC-020 bar-integrity script `check-bars.js` and baseline

**Run only after the main loop has written the C-1 … C-14 rulings into spec.md's Clarifications log, including the C-9
re-ruling of SC-012(b) and the ratified SC-024(c) bound in the SC-024 row.**

- **Copy** `specs/profundum-phase2-edge-fold/check-bars.js` to `specs/profundum-phase3-anchor/check-bars.js`. Keep the
  `baseline` / `check` modes and the optional spec-path argument (`:5–16`); delete Phase 2's `sc023` mode and its references
  (Phase 3 has no SC-023 mechanism record). It reads this folder's `spec.md` and writes `bars-baseline.json` next to itself.
- Run `node specs/profundum-phase3-anchor/check-bars.js baseline`, then `… check`: every SC prints `SAME`.
- Positive control: copy `spec.md` to the scratchpad, change SC-001's "−90 dB" (a) to "−80 dB", run `check <copy>`: SC-001
  reads `CHANGED-UNRULED` with a non-zero exit. Record both outputs under "Run log".

---

## Group 1 — CPU slow-path estimate (alone; nothing else running)

### T005 Bench the real taper loop before the header is written (plan S9.8 coverage gap)

- Precondition: Group 0 finished; no build, suite, model or agent running.
- **Edit** `specs/profundum-phase3-anchor/bench/anchor-kernel-bench.cpp`: add a third timed configuration `K = 16, taper band`
  that runs the S4.5 `refreshTaper(f_T)` **with real `std::log2` / `std::pow` per band term** (plan S4.5: terms with
  n·f_T > taperStartHz, n odd ≤ 31, plus T(2f_T) and T(3f_T)) at every grid sample, with f₀ = 494 Hz under the 5 Hz ±1-semitone
  vibrato at 48 kHz, and the FOLLOW ratio 1.25 (r_eff·f₀ in the band: 9 triangle terms at the worst f_T). Keep the two
  existing E1 configurations unchanged.
- Compile with the line at the top of the file using the full path:
  `"/c/Program Files/LLVM/bin/clang++.exe" -std=c++20 -O2 -ffast-math -march=x86-64-v2 -DNDEBUG -Idsp/include specs/profundum-phase3-anchor/bench/anchor-kernel-bench.cpp -o <scratchpad>/anchor-bench.exe`.
- Run 5 times, 20 s apart, through `pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe <scratchpad>/anchor-bench.exe`.
- Paste the five figures per configuration and their medians into plan S9.8 under a new line
  "Re-run 2026-…, T005, real taper loop, pinned, isolated, median of 5".
- **Decision recorded under "Run log":** if the taper-band median exceeds **0.20 %** (two-thirds of the 0.3 % budget, leaving
  the MSVC 1.2–1.5× factor of plan S11 inside it), T011 implements the plan's remedy (per-term weights cached in the cents
  domain, recomputed only for terms whose band membership or cents value changed). Otherwise T011 implements the plain S4.5
  slow path. The 0.3 % budget is never relaxed either way.

---

## Group 2 — CMake registration with compiling stubs (sequential, single task)

### T006 Register every Phase 3 file in CMake

- **Create stubs** (each with a file comment naming the layer, "Profundum Phase 3 (specs/profundum-phase3-anchor)" and the
  SCs it will carry from plan S8.1):
  - `dsp/include/krate/dsp/systems/anchor_oscillator.h`: `#pragma once`, banner "Layer 3: System Component — Profundum Phase 3
    (specs/profundum-phase3-anchor)" and "Includes Layer 0, Layer 1 and Layer 2 only", `namespace Krate::DSP {}`.
  - TUs `anchor_oscillator_test.cpp`, `anchor_oscillator_spectral_test.cpp`, `anchor_oscillator_nonfinite_test.cpp`,
    `profundum_anchor_chain_test.cpp`, `anchor_oscillator_perf_test.cpp` in `dsp/tests/unit/systems/`: file comment,
    `#include <krate/dsp/systems/anchor_oscillator.h>`, `#include <catch2/catch_test_macros.hpp>`, no test cases. The chain stub
    also includes `<krate/dsp/systems/profundum_core.h>` and `<krate/dsp/processors/harmonic_fold.h>` (proves they coexist).
- **Edit `dsp/CMakeLists.txt`**: after `include/krate/dsp/systems/profundum_core.h` (`:188`), before `)` (`:189`), add
  `# Profundum Phase 3 (specs/profundum-phase3-anchor)` and `include/krate/dsp/systems/anchor_oscillator.h` (deviation 2).
- **Edit `dsp/tests/CMakeLists.txt`**:
  - `dsp_systems_tests`: after `unit/systems/profundum_edge_fold_perf_test.cpp` (`:631`), before `)` (`:632`), a comment block in
    the Phase 1/2 style ("ENUMERATED, not globbed") mapping each TU to its SCs (plan S8.1 table), then the five TUs in the
    order `anchor_oscillator_test.cpp`, `anchor_oscillator_spectral_test.cpp`, `anchor_oscillator_nonfinite_test.cpp`,
    `profundum_anchor_chain_test.cpp`, `anchor_oscillator_perf_test.cpp`.
  - `-fno-fast-math` block: after `unit/systems/profundum_core_excitation_nonfinite_test.cpp` (`:1113`), before the
    `PROPERTIES` line (`:1114`), add `# Profundum Phase 3: injects NaN/Inf bit patterns (SC-015). The perf and chain TUs stay out (shipping FP mode, plan S7).`
    and `unit/systems/anchor_oscillator_nonfinite_test.cpp`. Nothing else.
  - No compile-definition change (deviation 3).
- **Verify:** reconfigure (`"$CMAKE" --preset windows-x64-release`), build `dsp_systems_tests` with zero warnings, record the
  build wall clock, and run `dsp_systems_tests.exe "[profundum]~[long]" 2>&1 | tail -3`: green, Phase 1/2 cases unchanged.

---

## Group 3 — `AnchorOscillator` skeleton: API, lifecycle, mirror, sine/LOCK path (sequential)

### T007 Header API, lifecycle, sanitising, S5 coefficient mirror, sine/LOCK `processBlock`, base helpers (plan S3, S4.1–S4.4, S4.11, S5; FR-001–FR-008, FR-031, FR-033, FR-050, FR-053; C-4, C-5, C-7)

Files: `anchor_oscillator.h`, `anchor_oscillator_test_helpers.h` (new), `anchor_oscillator_test.cpp`.

**Failing tests first** (`anchor_oscillator_test.cpp`, tag `[systems][profundum][anchor]`; also includes
`<krate/dsp/systems/profundum_core.h>`, `<krate/dsp/processors/harmonic_fold.h>`, `"anchor_oscillator_test_helpers.h"`):
- `AnchorOscillator_CalibrationConstants`: `static_assert(AnchorOscillator::kMaxCoreH1Amplitude == HarmonicFold::kCalibrationPeak)`;
  `static_assert(AnchorOscillator::kOutputCeiling <= 2.0f)`; `static_assert(AnchorOscillator::kAnchorToH1 == 1.0f)` and
  `kDefaultLevel == 0.5f` (C-2); `REQUIRE(std::abs(std::sqrt(double(kPowerTarget))·double(kCenterPanGain) − double(kMaxCoreH1Amplitude)) <= 1e-7)`.
- `AnchorOscillator_EnumsAndTraits`: `static_assert` underlying values Waveform `Sine`=0 … `OctaveDownSine`=4, Mode `Lock`=0 …
  `Ghost`=3 (FR-003, append-only); `static_assert(static_cast<std::uint8_t>(AnchorOscillator::RetriggerPhase::Reset) == static_cast<std::uint8_t>(ProfundumCore::RetriggerPhase::Reset))`
  and the same for `FreeRunning` (SC-017 enum mapping, D-6); `static_assert(std::is_copy_constructible_v<AnchorOscillator> && std::is_nothrow_move_constructible_v<AnchorOscillator>)`
  (FR-001); `static_assert(AnchorOscillator::latencySamples() == 0)` (FR-033); `static_assert(noexcept(...))` on
  `setControls`, `setFrequency`, `setFollowRatio`, `setCoreH1Amplitude`, `setRetriggerPhase`, `noteOn`, `processBlock` and every
  observer (SC-014); `static_assert(std::is_same_v<decltype(a.modeGain()), double>)` (C-4).
- `AnchorOscillator_FtzPrecondition`: under `#if KRATE_HAS_SSE_DENORMAL_CONTROL`, `REQUIRE(_MM_GET_FLUSH_ZERO_MODE() == _MM_FLUSH_ZERO_ON)`
  (FR-057, pattern `profundum_core_test.cpp:830–832`; include `<krate/dsp/core/scoped_denormal_mode.h>` for the macro).
- `AnchorOscillator_LifecycleNoOps` (FR-004, FR-007, C-7): (i) before `prepare`: buffers pre-filled with 1.0f become all 0.0f
  after `processBlock(L, R, 64)`; every setter and `noteOn` is a no-op (`stateFinite()` true, `phase() == 0.0`, `isSounding() == false`).
  (ii) After `prepare(48000)` and before any `noteOn`: 512 samples are exactly 0.0f and `isSounding() == false`. (iii)
  `processBlock(nullptr, R, 64)`, `processBlock(L, nullptr, 64)`, `processBlock(L, R, 0)` leave a 1.0f-filled non-null buffer
  untouched and `phase()`/`modeGain()` unchanged. (iv) `prepare(10000.0)` clamps: a subsequent C3 sine render's per-sample
  `phaseIncrement()` equals that of an instance prepared at 22050 (bitwise).
- `AnchorOscillator_MirrorTracksCore` (C-5, plan-internal): a `ProfundumCore` (`prepare(fs)`, `setControls(midGrid())`) and an
  Anchor (`prepare(fs)`) receive identical `setRetriggerPhase` / `noteOn` / `setFrequency` / `f0PerSample` calls, both rendered
  in **1-sample blocks** for 2 s at fs ∈ {48000, 96000}: Reset noteOn E1; 0–0.5 s a 5 Hz ±1-semitone vibrato trajectory;
  at 0.5 s a FreeRunning noteOn +7 semitones; at 1.0 s a +12-semitone 200 ms bend trajectory (raised-cosine up and back); at
  1.5 s a Reset noteOn C3. After **every** block: `std::bit_cast<std::uint32_t>(a.mirrorState().baseHz) == std::bit_cast<std::uint32_t>(core.baseFrequency())`,
  the same for `.currentHz` vs `core.currentFrequency()` and `.maskHz` vs `core.maskFrequency()`. Print the number of blocks
  checked.
- `AnchorOscillator_SineLockBasics` (FR-010, FR-016, FR-035): sine, LOCK, a₁ default, Reset noteOn C3 at 48 kHz: the first
  output sample is exactly 0.0f; after 1 s, |RMS dB − 20·log₁₀(kAnchorToH1·kDefaultLevel·kMaxCoreH1Amplitude/√2)| ≤ 0.1 dB
  (`rmsDbOverPeriods`, ≥ 1 s of whole periods); `phase()` after n samples equals the double sum of `phaseIncrement()` values
  modulo 1 within 1e-12 (recorded per sample in 1-sample blocks over 4800 samples).
- `AnchorOscillator_RealTimeSafe` (SC-014, sine/LOCK arm; T019 extends it): around `setControls`, `setFrequency`,
  `setRetriggerPhase`, `noteOn` (both policies), `setFollowRatio`, `setCoreH1Amplitude` and `processBlock` with blocks
  {1, 37, 64, 512, 4096} (buffers allocated before tracking), `AllocationDetector::instance().startTracking()` …
  `stopTracking()` returns **0**.

**Implementation** (`anchor_oscillator.h`):
- The class exactly as plan S3 (all constants, nested `Waveform`, `Mode`, `RetriggerPhase`, `Controls` with designated
  defaults, `MirrorState` marked `// TEST-ONLY diagnostic: not a Phase 8/9/11 contract, never serialised`, every observer of
  S3, `latencySamples()` 0, private `kPiDouble` = the core's literal, `kTwoPiD`), state per S4.1 (fields for later tasks may be
  added when those tasks need them).
- `prepare` / `reset` / "before prepare" per S4.2 (no smoothers yet: deviation 6 — hold `levelHeld_ = active_.level` and
  `a1Held_`, both applied directly).
- Setters and sanitising per S4.3, including C-11's flush-then-clamp on every float field, enum range check, `controlsDirty_`
  via `std::bit_cast`, `setFrequency` = the core's `sanitizeF0` (`profundum_core.h:795–800`) with `capHz_`, `setFollowRatio`
  (non-finite → previous, clamp [0.25, 4]), `setCoreH1Amplitude` (non-finite → previous, clamp [0, `kMaxCoreH1Amplitude`]),
  `noteOn(f, rearm = true)` storing `noteOnF0_ = sanitizeF0(f, heldF0_)`, `noteOnRearm_`, `noteOnPending_` (last call wins).
- `processBlock` in the S4.4 normative order, with `trajectoryF0` / `trajectoryRatio` copies of `profundum_core.h:805–811` and
  the end-of-block bookkeeping of `:285–287` for both f₀ and r; C-7 silence before the first noteOn.
- `applyNoteOn` per S4.6 for both policies (phase, grid, mirror; envelope = Hold(1) for now; Controls latch per C-8).
- The S5 mirror verbatim (S5.1 primitives `bankEps`, `epsBase`, `epsStar`, `refresh`, `chase`; S5.2 Reset; S5.3
  `mirrorControlUpdate`, `mirrorPitchUpdate`, `mirrorFreeRunningNoteOn`), keeping each source line's operand order and types.
- `frequencyUpdate(r)` with r_eff = 1 for every mode (FOLLOW lands in T014): `δ_tgt = min(δ_lock, capHz_/fs)`, `delta_ = δ_tgt`.
- Sine waveform only (S4.5 angle reduction, `u = std::sin(a)` float, unit-RMS scale √2); every other waveform renders the sine
  until T011. `y = static_cast<float>(gainScale_ · levelHeld_ · a1Held_ · 1.0 · w)` in double, `gainScale_ = kAnchorToH1 · 0.70710678`.
- Observers per S4.11 (`modeGain()` returns `g_` = 1.0).

**Helpers** (`anchor_oscillator_test_helpers.h`, `inline`, namespace `Krate::DSP::ProfundumTest`, includes
`"profundum_core_test_helpers.h"` and `<krate/dsp/systems/anchor_oscillator.h>`): `struct AnchorRender { std::vector<float> L, R; std::vector<double> g, phase, inc; }`;
`renderAnchor(AnchorOscillator&, std::size_t n, std::size_t block, const float* f0Traj = nullptr, const float* rTraj = nullptr, bool perSample = false)`
(perSample forces 1-sample blocks and records `modeGain()`, `phase()`, `phaseIncrement()` after each sample; always calls
`requireLREqual`); `anchorInstFreqHz(const std::vector<double>& phase, std::size_t n, double fs)` = wrap(phase[n] − phase[n−1]) · fs
with wrap into [−0.5, 0.5); `anchorWindowEnergy(std::span<const float>, std::size_t from, std::size_t to)` (Σ x² in double);
`anchorMidiHz(int m)`.

**Verify:** build `dsp_systems_tests`, zero warnings; `dsp_systems_tests.exe "AnchorOscillator_*"` green; Phase 1/2
`"[profundum]~[long]~[anchor]~[anchor-chain]"` still green.

---

## Group 4 — analytic test toolkit (sequential; helper header + test TU)

### T008 Analytic curve, C¹ checker, SHS estimator, source scanner, separation rule — with their controls (plan S8.2; SC-006(a) positive control, SC-008(r), SC-012(a) C-14, SC-013)

Files: `anchor_oscillator_test_helpers.h` (append), `anchor_oscillator_test.cpp`.

**Failing tests first** (`anchor_oscillator_test.cpp`; they exercise only the helpers on synthetic data):
- `AnchorOscillator_AnalyticCurves`: `anchorRaisedCosine(g0, a, n, len)` = g0 + a·½(1 − cos(π·n/len)) at n = 0 → g0, n = len → g0 + a,
  n = len/2 → g0 + a/2 (each within 1e-15). `anchorHermite(g0, gp0, g1, n, len)`: value g0 at n = 0, g1 at n = len, slope
  (central difference over n ± 1 on len = 10⁶) at n = 0 equal to gp0 within 1e-6 relative, slope 0 at n = len.
  `anchorHermitePeakD2(g0, gp0, g1, len)` = max(|6Δ − 4m₀|, |6Δ − 2m₀|)/len² with Δ = g1 − g0, m₀ = len·gp0: for (g0 = 0, gp0 = 0,
  g1 = 1, len = 100) it equals 6e-4 exactly; and it bounds the brute-force max |Δ²| of the sampled curve within 1 % on 20 random
  (seeded `std::mt19937 rng(3)`) parameter sets.
- `AnchorOscillator_C1CheckerPositiveControl` (SC-006(a)): `anchorCheckC1(g, segs, lo, hi)` on a sampled raised cosine (A = 2.98,
  N = 240) reports `worstRatio ≤ 1.0` and passes; on a **linear ramp** of the same height and length followed by a hold it reports
  `worstBoundaryJump` > the in-segment bound and the checker FAILS (returns a report the test asserts as failing); on a raised
  cosine scaled to leave [lo, hi] = [0, 1] it reports `inRange == false`.
- `AnchorOscillator_ShsEstimatorControls` (SC-008(r) tool): `anchorShsF0(x, fs, f0Hint)` on a double-synthesised, float-stored
  signal of harmonics n = 1..40 with amplitudes 1/n at f₀ ∈ {20.6017, 41.2034, 82.4069, 130.8128} Hz, 48 kHz, 262144 samples
  after 262144 settle: |1200·log₂(f̂/f₀)| ≤ **0.1 cent**. Octave-trap control: the same signal with h1 removed (amplitude 0)
  still returns f₀ within 1 cent (missing-fundamental), and a signal of only even harmonics of f₀ (i.e. harmonics of 2f₀)
  returns 2f₀ within 1 cent (the estimator is not biased to the hint).
- `AnchorOscillator_StaticScannerControls` (SC-013): `anchorFindMutableStatics(source)` flags each positive control
  `static double x_;`, `static float table_[16];`, `static bool once_ = false;`, `static double y_ = std::sqrt(2.0);`,
  `void f() { static int n = 0; }`, `static thread_local float t_;` (6 of 6) and flags none of the negative controls
  `static constexpr float k = 1.0f;`, `static const int m = 2;`,
  `[[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }`, `static float bankEps(float b) noexcept;`,
  `x = static_cast<int>(y);`, `static_assert(true);`, `// static int commented;`, `const char* s = "static int inString;";`.
- `AnchorOscillator_SeparationRuleControls` (C-14): `anchorWaveformsSeparated(wA, wB)` on synthetic 15-element power vectors:
  sine-like (w₁ = 1, rest 1e-13) vs sine-like with different noise (1e-14) → **not separated**; triangle-like 1/n⁴ odd vs the
  same → not separated; sine-like vs rounded-like (h3 = 0.0625, i.e. −12.04 dB) → separated; triangle-like (h3 −19.08 dB) vs
  rounded-like → separated (7.04 dB at h3).

**Implementation** (append to the helpers, plan S8.2): `anchorRaisedCosine`, `anchorHermite` (cubic Hermite basis
h00/h10/h01 with t = n/len, m₀ = len·gp0, zero end slope), `anchorHermitePeakD2`; `struct AnchorSegmentSpec { enum class Kind { Hold, Cosine, Hermite } kind; double g0, a, gp0, g1; std::size_t start, len; }`;
`struct C1Report { double worstRatio; double worstBoundaryJump; bool inRange; bool pass; }`;
`anchorCheckC1(std::span<const double> g, std::span<const AnchorSegmentSpec> segs, double lo, double hi)` (per segment max |Δ²g|
over its interior vs 1.05 × its analytic bound — raised cosine |A|·π²/(2N²), Hermite `anchorHermitePeakD2` — and at each boundary
|Δg[n] − Δg[n−1]| ≤ the larger adjacent in-segment bound; Hold bound 0 means the neighbour's bound applies);
`anchorShsF0` (plan S8.2: Hann magnitude spectrum of one 262144 frame via `lowFreq::magnitudeSpectrum`, S(f) = Σ_{h=1}^{15} 0.84^{h−1}·M(h·f)
with M linearly interpolated, 0.05-cent log grid over [f₀/2.2, 2.2·f₀], parabolic refinement through the best three points,
double precision); `anchorFindMutableStatics(std::string_view)` (strip `//` and `/* */` comments and string literals; flag every
declaration containing the token `\bstatic\b` unless it is `static constexpr`, `static const`, `static_cast`, `static_assert`
or a function declaration — a `(` before the first `=`, `[`, `{` or `;`); `anchorWaveformsSeparated` (each entry
max(10·log₁₀(wₙ/w₁), −90 dB); separated iff some n ≥ 2 has |dB_A − dB_B| ≥ 6 with at least one of the two above −90 dB).

**Verify:** build, zero warnings; the five TEST_CASEs green.

---

## Group 5 — phase lock (sequential; header + chain TU + test TU)

### T009 Phase-lock gates SC-003 (a)(b)(c)(e), SC-026 (a)(b sine)(c), FreeRunning bit-identity (FR-030–FR-036; D-1; deviation 5; FR-032 decision point)

Files: `anchor_oscillator.h` (deviation 5 only), `profundum_anchor_chain_test.cpp`, `anchor_oscillator_test.cpp`.

Δθ measurement (all arms): render the core (`renderCore`-style loop, `setControls(midGrid())`, excitation off, `requireLREqual`)
and the Anchor (sine, LOCK) with **identical** calls and host blocks; Δθ(t) = wrap(θ_a − θ₁) with both phases from
`goertzelPhase` on the same window of round(k·fs/f₀) samples, k = ceil(f₀) periods (≥ 1 s), centred at t; wrap to (−180°, 180°].

**Failing tests first** (chain TU, tag `[systems][profundum][anchor-chain]`):
- `AnchorOscillator_PhaseLockStaticSmoke` (per push) and `AnchorOscillator_PhaseLockStatic60s` `[long]` (SC-003(a)): smoke E1,
  48 kHz, 11 s render, Δθ at 1 s and 10 s; long: E0, E1, E2, C3 × 44.1 / 48 / 96 kHz, 61 s render. Host blocks 37 and 512.
  **|Δθ(1 s)| ≤ 1°** and **|Δθ(T) − Δθ(1 s)| ≤ 0.5°** (T = 10 s smoke, 60 s long). `std::printf` every figure.
- `AnchorOscillator_PhaseLockTrajectorySmoke` / `AnchorOscillator_PhaseLockTrajectory` `[long]` (SC-003(b)): `f0PerSample` =
  f₀·2^(sin(2π·5·t)/12) plus every 5 s a 200 ms bend to +12 semitones (alternating −12) shaped as a raised cosine up and down;
  the same two calls to both objects. Δθ sampled with windows starting 100 ms after each bend ends; same bars. Smoke: E1,
  48 kHz, 11 s; long: E0, E1, E2, C3 at 48 kHz, 61 s; blocks 37 and 512.
- `AnchorOscillator_PhaseLockRetrigger` (SC-003(c), per push, 10 s): at 2 s a Reset noteOn (same f₀), separately a FreeRunning
  noteOn (same f₀), and a FreeRunning noteOn +7 semitones, all at E1 and C3, 48 kHz, blocks 37 and 512: Δθ measured on windows
  starting 1 s after the noteOn, bars as (a) with T = 9 s after the noteOn.
- `ProfundumAnchor_PhaseLockProcessedVoiceSmoke` / `ProfundumAnchor_PhaseLockProcessedVoice` `[long]` (SC-003(e)): the core with
  `setExcitation({.mix = 1.0f, .drive = 1.0f, .asymmetry = 1.0f})`; Δθ against (i) h1 of the core's output and (ii) h1 after a
  `HarmonicFold` (`prepare(fs, 4096)`, `setAmount(100.0f)`, `setFundamentalHz(f0)`) applied to it. Smoke E1 48 kHz 11 s;
  long E0, E1, E2, C3 at 48 kHz, 61 s. **|Δθ(T) − Δθ(1 s)| ≤ 0.5°** for both; the static |Δθ(1 s)| is printed beside Phase 2's
  0.07° figure, not gated.

**Failing tests first** (`anchor_oscillator_test.cpp`):
- `AnchorOscillator_PhaseContinuity` (SC-026(a), arms available now; T011 and T014 append arms): `perSample` renders at C2 48 kHz
  across (1) a FreeRunning noteOn +5 semitones with `rearm = true`, (2) the same with `rearm = false`, (3) a 200 ms ±7-semitone
  glide trajectory, (4) a −12-semitone 100 ms Drop-shaped dip and return, (5) a switch from `setFrequency` (held) to an
  `f0PerSample` trajectory and back. At every sample |wrap(phase[n] − phase[n−1]) − expected(n)| ≤ **1e-9 cycles**, with
  expected(n) = f_grid(n)/fs in double, f_grid the sanitised f sampled at the last grid point (U = 16, grid restarting at a
  Reset noteOn) — and from offset 0 of the noteOn block for a FreeRunning noteOn (deviation 5). Only a Reset-policy noteOn
  restarts φ (assert phase() == 0 then).
- `AnchorOscillator_PhaseConvention` (SC-026(b), sine arm): Reset noteOn at E1 and C3, 48 kHz, core and Anchor same calls: first
  Anchor sample == 0.0f; Goertzel phase of the Anchor minus core h1 over the 1 s window starting at 1 s: **|Δ| ≤ 1°**.
- `AnchorOscillator_PhaseReference` (SC-026(c)): core fed a Drop trajectory (−12 semitones over 100 ms and back, C2, 48 kHz), the
  Anchor in LOCK fed the pre-Drop (constant C2) trajectory: φ(end of Drop) − φ(start) (unwrapped from per-sample `phase()`) vs
  Σ f_grid/fs over the same samples: **≤ 1e-6 cycles**.
- `AnchorOscillator_FreeRunningSameNoteIsSeamless` (SC-017 FreeRunning clause): LOCK, sine, C2; a FreeRunning `noteOn(sameF0, true)`
  at sample 24000 vs an uninterrupted render: `std::memcmp` equal over 48000 samples (blocks 37 and 512).

**Implementation:** deviation 5 — in `applyNoteOn`'s FreeRunning branch call `frequencyUpdate(heldRatio_)` after
`mirrorFreeRunningNoteOn(f)`. Fix any other divergence that `MirrorTracksCore` localises (base / current / mask) in the mirror
only; never touch `profundum_core.h`.

**FR-032 decision (record under "Run log"):** if every arm is green, record "FR-032 fallback not invoked; shared-header diff
zero" and skip T010. If any SC-003 arm is red after the mirror matches bitwise in `MirrorTracksCore`, record the figures and go
to T010. Never re-rule SC-003.

**Verify:** build; per-push lane `"[anchor]~[long],[anchor-chain]~[long]"` green; `[long]` shards concurrently to separate
logs: `"AnchorOscillator_PhaseLock*"` and `"ProfundumAnchor_PhaseLock*"` (both include their `[long]` cases by name). Record
wall clocks and every Δθ figure.

---

## Group 6 — FR-032 fallback (conditional, sequential; shared headers)

### T010 Append-only h1 coefficient observer (plan S5.4) — ONLY if T009 recorded a red SC-003

- Append to `processors/harmonic_oscillator_bank.h` (public, after the last public method):
  `[[nodiscard]] float effectiveEpsilon(std::size_t i) const noexcept { return std::clamp(epsilon_[i] * detuneMultiplier_[i], -1.99f, 1.99f); }`;
  append to `systems/profundum_core.h`: `[[nodiscard]] float h1EffectiveEpsilon() const noexcept { return bank_.effectiveEpsilon(0); }`.
  No existing line changes.
- The Anchor gains an optional trailing `const float* h1EpsilonPerGrid = nullptr` on `processBlock`: when non-null, the value for
  the current grid index replaces `mEpsEff_` before δ_lock is computed. The T009 tests feed it from the core per grid interval.
- Failing test first: re-run the red SC-003 arm with the pointer supplied; it must pass; the arm without the pointer stays as
  evidence (printed).
- **Verify:** `node specs/profundum-phase3-anchor/check-footprint.js 9b1a4cf5 --fallback` PASS (append-only); then one build of
  `ruinae_tests seraphis_tests vorago_tests innexus_tests dsp_processors_tests dsp_systems_tests` and the six suites run
  concurrently with `"~[.perf]~[long]"`, every one green (FR-063).

---

## Group 7 — waveforms and crossfade (sequential; header + spectral TU + test TU + chain TU)

### T011 Triangle (Clenshaw + cap taper keyed on f_T), rounded, asymmetric (+ onset ramp), octave-down; waveform crossfade (plan S4.5, S4.8; FR-010–FR-017, FR-061; C-1, C-6, C-8 E-7)

Files: `anchor_oscillator.h`, `anchor_oscillator_spectral_test.cpp`, `anchor_oscillator_test.cpp`, `profundum_anchor_chain_test.cpp`.

**Failing tests first** (spectral TU, tag `[systems][profundum][anchor]`; level 1, LOCK, a₁ = `kMaxCoreH1Amplitude`):
- `AnchorOscillator_WaveformThd` (SC-001 (a)–(e)): five waveforms × {E1, C2, C3} × {44.1, 48, 96} kHz, 262144 settle + one 262144
  frame, f_a = f₀ (octave-down f₀/2), N = 31. **(a)** sine THD ≤ **−90 dB**. **(b)** triangle: for odd n ≤ 15,
  |10·log₁₀(wₙ/w₁) + 40·log₁₀(n)| ≤ **0.5 dB** (−19.08 / −27.96 / −33.80 dB at n = 3, 5, 7); every even n ≤ **−90 dB** re w₁; total
  THD = **−18.3 ± 0.3 dB** (model −18.335). **(c)** rounded sine: h3 = **−12.04 ± 0.25 dB** re h1, every other n ≥ 2 ≤ −90 dB;
  rounded h3 − triangle h3 ≥ **7.0 dB**. **(d)** asymmetric sine: h2 = **−12.04 ± 0.25 dB**, every other n ≥ 2 ≤ −90 dB. **(e)**
  octave-down: THD re f₀/2 ≤ **−90 dB** and w at f₀ (n = 2 of f₀/2) ≤ **−90 dB** re f₀/2.
- `AnchorOscillator_ThdToolFloor` (SC-001(f)): a test-local float `std::sin` sine of amplitude `kMaxCoreH1Amplitude` at every note
  and rate reads THD ≤ **−100 dB**; the triangle at f₀ × 1.0003 (C2, 48 kHz) reproduces (b) within ±0.5 dB.
- `AnchorOscillator_TriangleCapTaper` (FR-012, E-2): triangle at C5 (523.2511 Hz), 44.1 kHz (capHz 17640, taper start
  17640·2^(−1430/1200) ≈ 7723 Hz): for odd n ≤ 31 with n·f < capHz, |10·log₁₀(wₙ/w₁) − 20·log₁₀(T(n·f)/n²)| ≤ **0.5 dB**
  whenever the expected value is above −90 dB, T computed in the test from the recipe constants (`spectral_shape_recipe.h:72–75`,
  cap branch `:173–182`); every n with n·f ≥ capHz and n·f < fs/2 ≤ **−90 dB** re w₁. Repeat at E1 (fast path): all 16 terms at
  their untapered values (n ≤ 15 within 0.5 dB).
- `AnchorOscillator_RmsCalibration` (SC-002, waveform arms; T012 appends the zero arm): every waveform × {E1, C3} × {44.1, 48, 96}
  kHz, level 1, a₁ ∈ {`kMaxCoreH1Amplitude`, 0.25·`kMaxCoreH1Amplitude`}: RMS over whole f_a periods ≥ 1 s after 1 s settle:
  |RMS dB − 20·log₁₀(kAnchorToH1·a₁/√2)| ≤ **0.1 dB**; every pair of waveforms within **0.1 dB**; RMS(a₁)/RMS(0.25·a₁) =
  20·log₁₀(4) ± **0.05 dB**.
- `AnchorOscillator_NoAliasing` + `AnchorOscillator_AliasGatePositiveControl` (SC-010, LOCK arms; T014 appends the FOLLOW arm):
  every waveform, level 1, LOCK, E0 / E1 / E2 × 44.1 / 48 / 96 kHz, fftLen = `aliasFftLength(fs, f_a)`, settle ≥ fftLen:
  `aliasedPower(x, fftLen, fs, f_a).aliasedDbfs + 12.0 ≤ −80` (the +12.0 dB is the PUNCH peak, plan "SC-010 note"). Positive
  control: a test-local naive triangle 4·|φ − 0.5| − 1 in sine phase, same gain, C4 / 44.1 kHz, reads **> −80 dBFS** at 0 dB and
  at +12 dB (model −74.6 / −62.6). Recorded (printf, not gated): every waveform per octave E2 → C5 at 48 kHz.
- `AnchorOscillator_NoDc` (SC-011): every waveform × {E0, E1, C3}, 48 kHz, integer-period window ≥ 1 s after 1 s settle:
  20·log₁₀|mean| ≤ **−80 dBFS**.

**Failing tests first** (`anchor_oscillator_test.cpp`):
- `AnchorOscillator_OnsetClickFree` (SC-017 onset; FR-016, C-6): every waveform × {E0, E1, C3}, 48 kHz, Reset noteOn, level 1:
  first sample `== 0.0f` (all five, including the asymmetric sine); max |Δy| over the first 10 ms ≤ **1.1 ×** the max |Δy| over a
  steady 1 s window starting at 1 s (model 1.0000).
- `AnchorOscillator_WaveformCrossfade` (FR-014, C-8): C2, 48 kHz, level 1; at sample 48000 switch each of the 20 ordered pairs.
  Reference renders of the old and new waveform alone (same calls, same φ). For n ∈ [0, 240): |y − (y_old + c(n)(y_new − y_old))| ≤
  1e-6 with c(n) = ½(1 − cos(π·n/240)) in double; from n = 241 on, y is bitwise equal to the new-waveform reference
  (`std::memcmp` over 4800 samples); peak |y| over the crossfade ≤ max(peak_old, peak_new) + 1e-6. **E-7:** a second switch
  issued at n = 100 is queued: samples [0, 240) match the first crossfade, the queued crossfade starts at n = 240 and ends at
  n = 480 (bitwise equal to the final waveform's reference from 481); a Reset noteOn mid-crossfade completes it (the next
  block renders the new waveform only).
- `AnchorOscillator_PhaseContinuity` gains arm (6): a waveform switch mid-cycle (SC-026(a)); `AnchorOscillator_PhaseConvention`
  gains triangle, rounded and asymmetric (|Δ| ≤ 1° re core h1) and octave-down (|2·θ_a − θ₁| ≤ 1°, Goertzel at f₀/2 on a window
  of whole f₀/2 periods) (SC-026(b)).

**Failing tests first** (chain TU): `AnchorOscillator_PhaseLockStaticSmoke` / `AnchorOscillator_PhaseLockStatic60s` gain the
octave-down arm (SC-003(d)): Δθ = wrap(2·θ_a(f₀/2) − θ₁(f₀)), the same bars.

**Implementation** (`anchor_oscillator.h`, plan S4.5 / S4.8 / C-6): the table of S4.5 (triangle Clenshaw in double over
`triActive_` ≤ 16 tapered coefficients; rounded u + a₃ₑ(3u − 4u³); asymmetric u − a₂ₑ·r(n)·(1 − 2u²); octave-down
`std::sin(float(π·(φ₂ ≥ 1 ? φ₂ − 2 : φ₂)))`; unit-RMS scales √2, 1/√(Σa_k²/2), 1/√0.53125, 1/√0.53125, √2); T(nf) = the recipe's
cap branch restated without the low-note guard, `taperStartHz_` and `tailFloor_` computed in float in `prepare` exactly as
`evaluateMask` `:162–163`; `refreshTaper(f_T)` keyed on f_T at grid samples per S4.4 (fast path when 31·f_T ≤ taperStartHz_;
cents-domain cache if T005 decided so); the C-6 onset ramp (N_on = clamp(round(fs/f), 1, 4·fs), on Reset noteOns and FreeRunning
noteOns from not sounding, asymmetric sine only); the S4.8 crossfade with the rotation recurrence, queue (latest wins) and
completion on a Reset noteOn.

**Verify:** build; per-push lane green; the octave-down `[long]` arm in its shard; record SC-001/SC-010 figures.

---

## Group 8 — level and a₁ smoothing (sequential; header + spectral TU + test TU + chain TU)

### T012 `OnePoleSmoother` on level and a₁; SC-002 zero arm; SC-024 (a)(b)(d) (plan S4.9; FR-020–FR-025, E-13; C-2, C-13)

Files: `anchor_oscillator.h`, `anchor_oscillator_spectral_test.cpp`, `anchor_oscillator_test.cpp`, `profundum_anchor_chain_test.cpp`.

**Failing tests first:**
- `AnchorOscillator_LevelAndA1Smoothing` (`anchor_oscillator_test.cpp`; FR-020, FR-023, deviation 6): sine, C2, 48 kHz, 1-sample
  blocks. (i) A level step 0 → 1 at a steady note: every |y[n] − y[n−1]| ≤ **1.1 × (G_max·D_max + S_max·P_max)** where D_max and
  P_max are the max |Δy| and peak |y| of a 1 s steady render at level 1, LOCK, the same a₁ (so they carry k·a₁/√2), G_max = 1 (the
  largest level × g reached) and S_max = (1 − e^(−1000/fs))·ΔL with ΔL = 1 (the smoother's first step: coefficient
  exp(−5000/(5·fs)), `smoother.h:77–92`; 0.020618 at 48 kHz) — this fails on the T007 instantaneous step. (ii) The same for an a₁
  step 0 → `kMaxCoreH1Amplitude` and `kMaxCoreH1Amplitude` → 0 at level 1 (S_max = (1 − e^(−1000/fs))·Δa₁/`kMaxCoreH1Amplitude`,
  D_max / P_max at a₁ = `kMaxCoreH1Amplitude`) and for a level step 1 → 0. (iii) `ClickDetector` with
  default config (`sampleRate` = 48000) reports **0** clicks on each step render; positive control: the same render with a spliced
  instantaneous level step reports ≥ 1.
- `AnchorOscillator_RmsCalibration` gains the zero arm (SC-002, FR-021): after `setCoreH1Amplitude(0)` every sample is exactly
  0.0f from 12 ms after the call, and the first exact-zero sample is ≤ ln(`kMaxCoreH1Amplitude`/1e-4)·(`kLevelSmoothMs`/5) ms
  ≈ **8.18 ms** + 1 sample; the same for level 1 → 0 (≤ ln(1/1e-4) ms ≈ **9.21 ms** + 1 sample). Finite out-of-range
  `setCoreH1Amplitude(1.0f)` renders bitwise equal to `setCoreH1Amplitude(kMaxCoreH1Amplitude)`.
- `ProfundumAnchor_LevelCalibration` (chain TU; SC-024 (a)(b)): states `kSineAnchor`, `kSawAnchor`, `kHeavy`, `kHollow`, `kGrowl`,
  `kDefaultControls` (assert its fields equal `midGrid()`'s, so the spec's "mid grid" row is the same state), at E1 and C2,
  48 kHz. After 1 s, a₁ = `core.deliveredGains()[0] · SpectralShapeRecipe::kCenterPanGain` (plan S4.9, option (a) of C-2), fed to
  the Anchor by `setCoreH1Amplitude` once per host block (block 512). **(a)** A test-local float sine of amplitude a₁ at f₀ through
  the same `renderedHarmonicPowers` frame: |10·log₁₀(w₁(core L)/w₁(ref))| ≤ **0.1 dB** and the same for R; Anchor RMS (level 1)
  / (a₁/√2) = kAnchorToH1 within **0.1 dB**; the spread of that ratio across the six states ≤ **0.1 dB**. **(b)** Sine, LOCK,
  in phase (same Reset calls): 10·log₁₀(w₁(core + anchor)/w₁(core)) = 20·log₁₀(1 + k) = **6.02 ± 0.25 dB** at level 1 and
  20·log₁₀(1 + 0.5·k) = **3.52 ± 0.25 dB** at `kDefaultLevel`; printed per state.
- `ProfundumAnchor_NoCompensation` (chain TU; SC-024(d), C-13 structural): the core rendered with and without an Anchor
  instance processed beside it (interleaved block by block): `std::memcmp` equal; `summed[i] == core[i] + anchor[i]` for every i
  (a plain float add). A comment states this is the C-13 structural assertion; its teeth are (b) and T002's API grep.

**Implementation:** replace `levelHeld_` / `a1Held_` application with `levelS_` / `a1S_` (`OnePoleSmoother`, `configure(kLevelSmoothMs, fs)`
in `prepare`; `setTarget` on latch and on `setCoreH1Amplitude`; `snapToTarget` on a Reset noteOn and in `reset`), per plan S4.9.
The gain product is formed in double and rounded once.

**Verify:** build; per-push lane green.

---

## Group 9 — mode envelope and re-arm (sequential; header + test TU)

### T013 Segment program, raised cosine, Hermite bridge (C-3), snap/bridged arms, mode-switch bridge, `rearm` semantics (plan S4.6–S4.7; FR-040–FR-044, FR-048, E-6, E-8, E-13; C-3, C-8, C-12)

Files: `anchor_oscillator.h`, `anchor_oscillator_test.cpp`.

All envelope tests render in 1-sample blocks (`perSample`) and read g from `modeGain()`; "gain trace" = y_mode/y_lock where
|y_lock| ≥ 0.1·peak(y_lock), y_lock the same calls in LOCK.

**Failing tests first:**
- `AnchorOscillator_PunchBoostAndDuration` (SC-004, C-12): boost {3, 6, 12} dB × `punchTimeMs` {5, 40, 250} × {E1, C3}, 48 kHz,
  Reset noteOn. **(a)** max g = 10^(boost/20) within **±0.1 dB** (observer) and the gain trace's max within ±0.1 dB. **(b)** the
  first n after the peak with g == 1.0 bitwise is N_rise + N_time ± 1 (48 + {240, 1920, 12000}); g[N_rise + N_time − 1] > 1;
  every later sample |20·log₁₀ g| ≤ 0.01 dB; g at N_rise + 0.5·N_time > 10^(0.1/20); the whole trace from the arm to the return
  equals `anchorRaisedCosine` (rise g0 = 1, a = B − 1, N_rise; fall g0 = B, a = 1 − B, N_time) within **1e-9** absolute at every
  sample. **(c)** gain trace vs observer within **0.05 dB** at every evaluated sample.
- `AnchorOscillator_GhostEmergence` (SC-005): `ghostTimeMs` {1, 5, 20, 100} × {E1, C3}, 48 kHz. **P1:** 1 s of a sounding note,
  then a Reset `noteOn(f, true)` (t = 0 at the noteOn), plus a fresh instance with the same note: `std::memcmp` equal after the
  noteOn. **P2:** at g = 1 a FreeRunning `noteOn(f, true)`; the dip reaches g == 0.0 within 96 + 1 samples; t = 0 at the dip's end.
  On P1 and P2: **(a)** g(0) = 0 and 20·log₁₀ g ≤ **−30 dB** for t ≤ 0.1·T (analytic −32.2 dB at 0.1·T); **(b)** |20·log₁₀ g| ≤
  **0.01 dB** for t ≥ T + 1 sample; **(c)** g non-decreasing on [0, T]; **(d)** 10·log₁₀(Σ_{[0,T]} y_ghost² / Σ y_lock²) equals
  10·log₁₀(Σ g_D4²·y_lock² / Σ y_lock²) within **±0.25 dB**, g_D4 = `anchorRaisedCosine(0, 1, n, N_ghost)` in double; print it
  beside the spec's orientation figures (E1 −1.63 / −1.88 / −3.68 / −4.41 dB, C3 −1.73 / −5.87 / −4.50 / −4.30 dB for P1, sine).
  **P3:** a `noteOn(f, false)` under Reset and under FreeRunning while mid-emergence: the g trace is bitwise equal to the
  uninterrupted render's and no emergence restarts.
- `AnchorOscillator_EnvelopesAreC1` (SC-006(a)): a table-driven fixture of named transitions (each with its expected
  `AnchorSegmentSpec` program and N_b read from the analytic rule) at C2 48 kHz, B = 3.981 (12 dB), `punchTimeMs` 5 and 250,
  `ghostTimeMs` 1 and 100: PUNCH and GHOST snap arms; PUNCH and GHOST bridged arms at mid-rise, mid-fall and steady state;
  `rearm = false` noteOns under both policies mid-envelope (g unchanged); mode switches between every ordered pair of
  {Lock, Punch, Ghost} at mid-rise, mid-fall and steady state (T014 adds Follow); a level-0 PUNCH (E-13: output exactly 0.0f
  throughout). `anchorCheckC1` must pass: per raised cosine max |Δ²g| ≤ **1.05 × |A|·π²/(2N²)**, per bridge ≤ **1.05 ×**
  `anchorHermitePeakD2`, boundary jumps ≤ the larger adjacent bound, g ∈ [0, max(B, g₀)].
- `AnchorOscillator_TransitionsClickFree` (SC-006(b)): every transition of the fixture plus level steps 0 → 1 → 0, a₁ steps
  across [0, `kMaxCoreH1Amplitude`], waveform switches between every ordered pair mid-cycle, a LOCK Glide (200 ms ±7 st) and a
  Drop-shaped trajectory (−12 st 100 ms), and a slow upward f₀ sweep of the triangle from 2 kHz to capHz over 10 s at 44.1 kHz
  (E-2): `ClickDetector` (defaults, `sampleRate` set) reports **0** clicks on each render.
- `AnchorOscillator_ClickDetectorPositiveControl` (SC-006(b)): a spliced instantaneous level step 0 → 1 in a sine render reports
  **≥ 1** click.
- `AnchorOscillator_TransitionDeltaBound` (SC-006(c)): on every render of the two lists above, max |y[n] − y[n−1]| during the
  transition ≤ **1.1 × (G_max·D_max + S_max·P_max)**: D_max / P_max from 1 s steady unit-gain renders of each waveform involved
  (the E-2 sweep at its top frequency); G_max = B·level for any PUNCH arm, else level; S_max analytic only: |A|·π/(2N) for a
  raised cosine, the Hermite's analytic peak slope, (1 − e^(−1000/fs))·ΔL for a level step, Δc_max·max|w_new − w_old| with
  Δc_max = π/(2·240) for a crossfade.
- `AnchorOscillator_RearmEvent` (SC-027): **(a)** a re-arm noteOn delivered with host blocks {1, 37, 512} (setter before the block
  containing the target sample): g traces bitwise equal, and the first g change is at that block's offset 0. **(b)** PUNCH bridged
  arm: max g = B **± 0.1 dB**, return per SC-004(b) timings measured from the arm (N_b + N_time ± 1). **(c)** GHOST bridged arm:
  g == 0.0 within N_dip + 1 samples, then SC-005 (a)–(c). **(d)** `rearm = false` × {Reset, FreeRunning} × four modes (Follow
  behaves as Lock until T014): g trace bitwise equal to the uninterrupted one; `phase()` trace bitwise equal between `rearm = true`
  and `rearm = false` calls of the same policy. **(e)** `noteOn(f, false)` from not sounding arms (g trace equals
  `noteOn(f, true)`'s).
- `AnchorOscillator_RetriggerPolicies` (SC-017): renders after a Reset `noteOn(f, true)` from different prior states — other f₀
  and Controls, a running waveform crossfade, a moving level smoother, g mid-PUNCH, g mid-GHOST, 0.3 s vs 2 s elapsed — are
  `std::memcmp` equal in every mode (same pending Controls and ratio); with `rearm = false` under both policies the g trace equals
  the uninterrupted g trace, and under Reset the phase restart (phase() == 0) is the only discontinuity.

**Implementation** (plan S4.7, S4.6): nested private `Segment` (Hold / Cosine / Hermite, rotation recurrence evaluated once at
segment start with `std::cos`/`std::sin` in double), `seg_` program of ≤ 3 segments, `envelopeStep()` returning g at n then
advancing, exact stored end value at each segment end (literal 1.0 for every return to 1, C-12), `gSlope_` analytic; the C-3
bridge rule (`fits(N)` from the closed-form interior extrema; integer bisection ≤ 1 + ⌈log₂ N_nom⌉ evaluations); the program
table of S4.7 for snap arms, bridged arms and mode switches (FR-044: bridge to 1 over `kModeBridgeMs`, nothing if g = 1 and
g′ = 0); `punchBoostDb` / `punchTimeMs` / `ghostTimeMs` read at arm time; re-arm wins over a mode switch in one block (C-8);
`noteOn`'s `rearm` acting on g only (S4.6). y gains the factor g.

**Verify:** build; per-push lane green; record the bridge N_b figures beside plan S9.6.

---

## Group 10 — FOLLOW (sequential; header + test TU + spectral TU)

### T014 Engine-derived ratio r, 16-sample glide, trajectory contract (plan S4.10; FR-045, FR-047, FR-049; SC-008 (a)(b)(c), SC-025, SC-010 FOLLOW arm, SC-006 / SC-026 FOLLOW rows)

Files: `anchor_oscillator.h`, `anchor_oscillator_test.cpp`, `anchor_oscillator_spectral_test.cpp`.

**Failing tests first** (spectral TU):
- `AnchorOscillator_FollowRatioAccuracy` (SC-008(a)): FOLLOW, r ∈ {0.5, 0.75, 1.5, 2} × {E1, C2, C3}, 48 kHz (r·f₀ below the cap):
  the fundamental measured by the Goertzel phase slope over two 1 s windows 1 s apart (f = f_a + Δθ/(2π·Δt), f_a = r·f₀) equals
  r·f₀ within **1 cent**. Along a ±12-semitone 200 ms f₀ glide at r = 1.5, `anchorInstFreqHz` is within **±5 cents** of
  1.5·f₀(grid-sampled t) from 50 ms after the glide's end. Clamp: `setFollowRatio(8.0f)` renders bitwise equal to
  `setFollowRatio(4.0f)` (C2), and `setFollowRatio(0.1f)` equal to 0.25.
- `AnchorOscillator_NoAliasing` gains the FOLLOW arm (SC-010): triangle, rounded sine and asymmetric sine in FOLLOW at r = 4,
  E2 / C3 / C4 × 44.1 kHz (f_a = 4·f₀), plus an r step 1 → 4 at constant f₀ (C3) once via `setFollowRatio` and once via
  `followRatioPerSample`, frame taken after the glide: `aliasedDbfs + 12.0 ≤ −80`, **and** every wₙ of
  `renderedHarmonicPowers(x, fs, f_a, N)` with n·f_a ≥ `capFrequency(fs)` and n·f_a < fs/2 is ≤ **−90 dB** re w₁.

**Failing tests first** (`anchor_oscillator_test.cpp`):
- `AnchorOscillator_FollowLatency` (SC-008(b)): 1-sample blocks, C2, 48 kHz. After a step in f₀ (a fifth, FOLLOW r = 1.5) the
  first n with |cents(f_inst, 1.5·f₀new)| ≤ 5 is ≤ **32** samples after the step; after a step in r (1 → 1.5) ≤
  **kFollowGlideSamples + 16 = 32**; f_inst stays within [min, max] of the old and new frequencies at every sample.
- `AnchorOscillator_FollowEqualsLockAtUnity` (SC-008(c)): FOLLOW r = 1 vs LOCK, sine and triangle, on a static C2 note and on
  the SC-003(b) vibrato-plus-bend trajectory (both fed the same trajectory), blocks 37 and 512: `std::memcmp` equal.
- `AnchorOscillator_TrajectoryContract` (SC-025): E1 and C2, 48 kHz. **(1)** a fifth up via `setFrequency`: LOCK and FOLLOW
  (r = 1) reach the new f₀ within **1 cent** by ≤ **32** samples. **(2)** 200 ms ±7-semitone glide trajectory: both within
  **±5 cents** of the trajectory from 50 ms after its end. **(3)** Drop (−12 semitones over 100 ms and back): a core is fed the
  dropping trajectory; the Anchor in LOCK fed the pre-Drop (constant) trajectory has f_inst = note f₀ within **1 cent** at every
  sample of the Drop; the Anchor in FOLLOW (r = 1.5) fed the post-Drop trajectory equals 1.5·f₀(grid t) within **5 cents** from
  32 samples after each grid update. SC-003's Δθ bar (≤ 1° static) holds on the 1 s window before the Drop begins; no bar during it.
- `AnchorOscillator_PhaseContinuity` gains arm (7): LOCK ↔ FOLLOW switches at r = 1.5 (expected law r_eff·f_grid/fs with the
  16-sample linear δ glide where r_eff changes). `AnchorOscillator_EnvelopesAreC1`, `_TransitionsClickFree`,
  `_TransitionDeltaBound` gain rows: mode switches to and from Follow (every ordered pair with Lock, Punch, Ghost), a LOCK ↔
  FOLLOW trajectory-input switch, and a FOLLOW Drop-shaped trajectory; `_RearmEvent` (d) now runs Follow as itself.

**Implementation** (plan S4.10): r_eff = (mode == Follow) ? r : 1.0f; δ_tgt = min(double(r_eff)·δ_lock, capHz_/fs); glide over
`kFollowGlideSamples` = 16 when r_eff changed bitwise (snap on a Reset noteOn via `snapRatio_`); the taper key f_T = the larger δ of
an active glide (S4.4). A mode switch to or from Follow changes r_eff and therefore glides (FR-044).

**Verify:** build; per-push lane green; record SC-008(a)(b) figures.

---

## Group 11 — single-owner verification TUs (parallel: each task writes only its own TU; the header is read-only)

> Both TUs were created as stubs by T006 and are owned by exactly one task each. Write in parallel; build **once** for both
> after both are written (or let one task build while the other writes) — never two concurrent builds of `dsp_systems_tests`.
> A failure caused by a header defect goes under "Defects found" (rule 12); T020 fixes it.

### T015 [P] Non-finite and denormal inputs — `anchor_oscillator_nonfinite_test.cpp` (SC-015, SC-007 non-finite rows; FR-054, FR-055; C-11)

- `AnchorOscillator_NonFiniteInputs` (`[systems][profundum][anchor]`, TU in the `-fno-fast-math` block): NaN (`std::bit_cast<float>(0x7FC00000u)`),
  +Inf (`0x7F800000u`), −Inf (`0xFF800000u`) into each Controls float field, `noteOn`, `setFrequency`, `setFollowRatio`,
  `setCoreH1Amplitude`, and as elements of `f0PerSample` and `followRatioPerSample`: the render (C2, FOLLOW r = 1.5 for the ratio
  inputs, else LOCK; then PUNCH for the punch fields, GHOST for ghost) is `std::memcmp` equal to the run without the bad call.
  An out-of-range enum (`static_cast<Waveform>(7)`, `static_cast<Mode>(9)`) keeps the previous value (bitwise equal render).
- Denormal `std::bit_cast<float>(0x00000001u)` and 1e-40f (by bit pattern `0x000116C2u`) in each Controls float field, read back
  through `pendingControls()` (C-11): `level` and `punchBoostDb` read exactly 0.0f; `punchTimeMs` reads `kMinPunchTimeMs` (5.0f);
  `ghostTimeMs` reads `kMinGhostTimeMs` (1.0f).
- Over every render of this TU: every sample finite (bit test), |y| ≤ `kOutputCeiling` (1.75), `stateFinite()` true, L == R
  bitwise and L + L == L + R (mono sum = 2·L, SC-007).
- Bound arm (FR-055): triangle (the maximum crest), level 1, PUNCH +12 dB at a₁ = `kMaxCoreH1Amplitude`, C2 and E0, every rate:
  max |y| ≤ **1.75** (`kOutputCeiling`). Print the measured max beside the analytic worst case (k·a₁/√2)·B·crest = 1.0·1.7101
  (truncated-triangle crest, plan C-1); it is not a lower bar, because the 1 ms PUNCH peak need not meet a waveform peak.
- **Verify:** build (coordinated), zero warnings; `dsp_systems_tests.exe "AnchorOscillator_NonFiniteInputs"` green.

### T016 [P] CPU budget TU — `anchor_oscillator_perf_test.cpp` (SC-018; FR-064; C-1)

- `AnchorOscillator_CpuBudget` (`[systems][profundum][anchor][.perf]`, shipping FP mode): for each of the 20 waveform × mode cells
  and **two notes** — E1 (fast path) and f₀ = 494 Hz (taper band at 48 kHz) — 10 s of audio at 48 kHz, host block 64, a waveform
  switch and a PUNCH re-arm (FreeRunning `noteOn(f, true)`) every 100 ms, a 5 Hz ±1-semitone vibrato `f0PerSample`, FOLLOW cells
  with `followRatioPerSample` ≡ 1.25. Time with `std::chrono::steady_clock`, 5 repetitions per cell, take the median; cost =
  time / audio time. **REQUIRE worst cell ≤ 0.3 %** (0.003). Print the E1 and taper-band cells side by side (40 cells) and the
  worst. Buffers allocated before timing. One warm-up repetition discarded.
- **Verify:** build (coordinated), zero warnings. Do **not** run it here (it is timing-sensitive); T017 runs it alone.

---

## Group 12 — CPU budget early (alone; nothing else running; may edit the header)

### T017 Measure SC-018 alone; fix the header if red

- Precondition: Group 11 built; no build, suite, agent or model running; the machine idle.
- Run only the perf case, pinned: `pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe build/windows-x64-release/bin/Release/dsp_systems_tests.exe -ExeArgs "AnchorOscillator_CpuBudget"`,
  output to a scratchpad log.
- Green (worst cell ≤ 0.3 %): record the 40 cells and the worst under "Run log" and in plan S9.8 ("SC-018, T017, MSVC, pinned").
- Red: confirm nothing else ran, idle 60 s, re-run once. Still red: implement the plan's remedy in `anchor_oscillator.h` (cents-domain
  per-term weight cache, if T011 did not already; otherwise profile which branch dominates and record it), rebuild, re-run the per-push
  lane (to prove no regression) and then the perf case alone. Never relax the 0.3 % budget or shrink the workload; if no remedy
  reaches it, stop and take the figures to the main loop.

---

## Group 13 — chain gates (sequential; chain TU)

### T018 SC-008(r), SC-009, SC-022 (record the verdict), SC-024(c) — `profundum_anchor_chain_test.cpp` (FR-047, FR-052, FR-067; C-2)

Chain = core (`setControls(state)`) → `setExcitation({.mix = 1.0f, .drive = 1.0f, .asymmetry = 1.0f})` → `HarmonicFold`
(`prepare(fs, 4096)`, `setAmount(a)`, `setFundamentalHz(f0)`). Steady state from t_settle = max(1.0 s, 8·τ_release) with τ_release
from the Fold's detector at the test note (Phase 2 tasks rule 9: E0 1.55 s); then 262144 settle samples + one 262144 frame. Every
render asserts `requireLREqual`.

- `ProfundumAnchor_FollowReferenceOnFoldedVoice` (SC-008(r)): states `kSawAnchor` and `kGrowl`, Fold 100, E0 / E1 / E2 / C3, 48 kHz:
  `anchorShsF0` on the chain's L channel = f₀ within **1 cent** (1200·|log₂(f̂/f₀)| ≤ 1). A comment states this is FR-047's evidence
  that r ≡ 1 for the core, excitation and Fold — not a tracking test.
- `ProfundumAnchor_SubPreservedThroughMaxFold` (SC-009): S = chain(Fold 100) + Anchor (level 1, LOCK); R = core with excitation
  mix 0, no Fold + Anchor; the same a₁ in both (the unprocessed core's `deliveredGains()[0]·kCenterPanGain`). Core states mid grid
  and `kSawAnchor`. **Gated:** sine, triangle, rounded, asymmetric at E0, E1, E2: |10·log₁₀(w₁(S)/w₁(R))| ≤ **1 dB** at f₀.
  Printed per row: the core-only w₁ change (dB) and the `goertzelPhase` of core h1 relative to the Anchor in both chains.
  Octave-down rows printed only.
- `ProfundumAnchor_FoldFeedAudible`, `ProfundumAnchor_FoldFeedSubPreserved`, `ProfundumAnchor_FoldFeedMono` (SC-022, FR-067):
  the Anchor (sine, LOCK, level 1) rendered **once** into A; core (mid grid) + g·A → `HarmonicFold` {65, 100} → + A, g ∈ {0, 1}, E1
  and C2, 48 kHz. **(1)** `CHECK` (not `REQUIRE`) that |THD(feed 1) − THD(feed 0)| ≥ **3 dB** or |centroidOct(feed 1) −
  centroidOct(feed 0)| ≥ **0.25 oct** (`describeRendered`) at **both** Fold settings; print `SC-022 VERDICT: PASS|FAIL` with the
  figures. **(2)** `REQUIRE` |10·log₁₀(w₁(feed 1)/w₁(feed 0))| ≤ **1 dB** at f₀. **(3)** `REQUIRE` L == R bitwise. Record the verdict
  under "Run log": it decides T021's branch.
- `ProfundumAnchor_LevelCalibration` gains SC-024(c): for each state at E1 and C2, the summed-signal RMS rise at `kDefaultLevel`
  over the core alone (whole periods, ≥ 1 s) is printed and gated at the **ratified** bound (plan C-2 proposes **≤ 3.6 dB**; model
  0.00 … 3.52 dB). If the spec row still has no ratified bound, stop: that is the T004 precondition.
- **Verify:** build; `"[anchor-chain]~[long]"` green except SC-022(1), which is a recorded outcome.

---

## Group 14 — determinism, independence, audibility, full RT/L==R matrices (sequential; test TU + spectral TU)

### T019 SC-013, SC-016, SC-012, SC-007 matrix, SC-014 matrix and FR-007 (FR-051, FR-056, FR-060; C-9, C-14)

Files: `anchor_oscillator_test.cpp`, `anchor_oscillator_spectral_test.cpp`.

**Failing tests first** (`anchor_oscillator_test.cpp`):
- `AnchorOscillator_InstancesIndependent` (SC-013): instance A (C2, Reset, LOCK, triangle) and B (E1, FreeRunning, PUNCH,
  asymmetric) processed interleaved in blocks {1, 37, 512} vs each alone: `std::memcmp` equal. Source scan: read
  `std::string(KRATE_DSP_TESTS_DIR) + "/../include/krate/dsp/systems/anchor_oscillator.h"` (deviation 3) with `std::ifstream`,
  `REQUIRE` it opened and is non-empty, `REQUIRE(anchorFindMutableStatics(text).empty())` (print any hit).
- `AnchorOscillator_Deterministic`, `AnchorOscillator_BlockSizeIndependent` (SC-016): two instances with one call sequence: `memcmp`
  equal; partitions {512, 64, 37, 1} with no setter between blocks, including a pending PUNCH envelope and an `f0PerSample` vibrato:
  `memcmp` equal.
- `AnchorOscillator_SampleRateChange` (SC-016): one instance `prepare`d 48 → 96 → 44.1 → 22.05 → 192 kHz; at each rate: the SC-001
  E1 sine (THD ≤ −90 dB) and triangle ((b) ratios) smoke, the SC-002 RMS bar (±0.1 dB), and the SC-003(a) 10 s E1 smoke against a core
  prepared at the same rate (≤ 1° / ≤ 0.5°); `fingerprintRender` / `compareFingerprints` used only as a cross-rate smoke on the
  sine render, never as a golden.
- `AnchorOscillator_LrBitIdentical` (SC-007 matrix): 5 waveforms × 4 modes × 2 policies × blocks {1, 37, 64, 512, 4096}, with a
  mode transition and a waveform switch inside each render: L == R bitwise and L + L == L + R.
- `AnchorOscillator_RealTimeSafe` extended (SC-014): the full waveform × mode matrix with transitions active (crossfade, PUNCH
  re-arm, r glide, level step), blocks {1, 37, 64, 512, 4096}: 0 allocations. **FR-007:** (i) `processBlock(L, L, n)` equals the L
  channel of the same sequence into two buffers (`memcmp`), every waveform and mode with transitions active; (ii)
  `processBlock(nullptr, R, n)`, `processBlock(L, nullptr, n)` and `processBlock(L, R, 0)` interleaved mid-PUNCH, mid-crossfade and
  mid-glide leave the subsequent output, `phase()` and `modeGain()` traces bitwise equal to the sequence without them and leave the
  non-null buffer's sentinel untouched.

**Failing tests first** (spectral TU):
- `AnchorOscillator_AudibilityGate` (SC-012) at C2 and E1, 48 kHz: **(a)** per C-14: harmonic-power vectors n ≤ 15 re own w₁,
  floored at −90 dB; `anchorWaveformsSeparated` true for the nine pairs other than sine/octave-down; sine/octave-down separated by
  octave-down's w₁ at f₀/2 ± 1 cent with power at f₀ ≤ −90 dB re it; positive control: two renders of sine and two of triangle
  report **not separated**. Print rounded vs triangle at h3 (analytic 7.04 dB) and asymmetric vs each other waveform at h2
  (analytic ≥ 12 dB). **(b)** 33-point level sweep (linear 0.01 … 1): E_sub strictly increasing; span ≥ **40 dB − 1e-4 dB** (C-9,
  as ratified). **(c)** boost 0 … 12 dB in 13 points: energy over [0, N_rise + N_time] strictly increasing, span ≥ **6 dB**.
  **(d)** 9 `punchTimeMs` values from 5 to 250 (log-spaced): energy over [N_rise, N_rise + 12000] strictly increasing, span ≥ **3 dB**.
  **(e)** 9 `ghostTimeMs` values from 1 to 100 (log-spaced): energy in the first 10 ms strictly decreasing, span ≥ **10 dB**.
  **(f)** FOLLOW r ∈ {0.5, 2}: fundamental at r·f₀ within **1 cent**. **(g)** at the D-4 defaults, same calls: GHOST/LOCK over
  [0, `ghostTimeMs`] ≤ **−3 dB**; PUNCH/LOCK over [0, `kPunchRiseMs` + `punchTimeMs`] ≥ **+3 dB**; PUNCH/GHOST over the same window
  ≥ **+3 dB**; the analytic values recomputed in double in the test and printed beside the spec's (E1 / C2 / C3: −6.32 / −4.11 /
  −4.13, +3.90 / +3.87 / +3.76, +5.17 / +5.02 / +4.87 dB).

**Verify:** build; per-push lane green. If any of these exposes a header defect, fix it here (the header is not owned by another
running task) and re-run the whole per-push lane.

---

## Group 15 — defect fix-up (sequential, conditional)

### T020 Resolve every entry under "Defects found"

- For each entry: reproduce with the named case alone, diagnose against `anchor-model.js` or the plan section cited, fix
  `anchor_oscillator.h`, rebuild once, and re-run the per-push lane plus the affected `[long]` shard concurrently. If the fix touches
  anything SC-018 measures (waveform evaluation, taper, envelope, mirror), re-run T017's measurement alone afterwards.
- A defect whose only fix is a bar change: stop; take the evidence to the main loop (FR-065, SC-020). Never relax.
- Mark each entry RESOLVED with the fix, the re-run case names and their results. No entries: record "no-op" under "Run log".

---

## Group 16 — roadmap handoff (sequential; shared doc)

### T021 Write the FR-066 amendments into `specs/Profundum-roadmap.md` (plan S13; C-10; branch on SC-022)

- Precondition: T018 recorded the SC-022 verdict.
- Paste plan S13's sentences, adjusted only to fit the prose, at these anchors (re-locate by heading text, not line number):
  - Phase 9 success-criteria paragraph (`:511–515`): append the three spec sentences (Anchor sub energy within 1 dB through
    maximum Fold + every Character algorithm at maximum Amount, at E0–E2; FOLLOW through a real pitch-moving stage with the
    capability-table row or inert flag; the Anchor joins after Character, direct path always present, Drop routed to the core
    only) plus the two plan additions (C-5 residual soak with Motion; C-13 bit-identity attached/detached and plain add). Keep it
    one paragraph (no blank line inside), so T003's P9-SC scope sees it.
  - Phase 5 `BassCharacterStage` bullet (`:374–379`): the one-sentence r-law deliverable naming Rectify, Crush and Fracture.
  - Phase 4 Note Memory bullet (`:349–350`): the re-arm and common-phase-reference sentence.
  - Phase 6 Mass law bullet (`:400–404`): the h1-amplitude / no-compensation sentence.
  - **Only if SC-022 = PASS:** the `anchorFoldFeed` sentence at the end of the Phase 9 `ProfundumVoice` bullet (`:490–494`); add
    `anchorFoldFeed` to the Phase 8 `MorphState` field list (`:452–455`); add "`anchorFoldFeed` is registered in the 300–399 anchor
    block." to the Phase 11 paragraph (`:551–559`, C-10). **If FAIL:** write none of the three.
- **Verify:** `node specs/profundum-phase3-anchor/check-handoff.js <pass|fail>` (the recorded verdict) → every line PASS; quote the
  output under "Run log".

---

## Group 17 — integration (sequential; last)

### T022 CMake registration audit (single task)

- `grep -c` each of the five Phase 3 TU paths in `dsp/tests/CMakeLists.txt`: exactly 1 each, except
  `unit/systems/anchor_oscillator_nonfinite_test.cpp` = 2 (systems list + `-fno-fast-math` block); the perf and chain TUs absent from
  that block. `dsp/CMakeLists.txt` lists `include/krate/dsp/systems/anchor_oscillator.h` exactly once.
- Every TEST_CASE named in spec SC-001 … SC-027 (method column) and in this file exists:
  `grep -rhoE 'TEST_CASE\("[A-Za-z0-9_]+' dsp/tests/unit/systems/anchor_oscillator_*.cpp dsp/tests/unit/systems/profundum_anchor_chain_test.cpp | sort | uniq -d`
  prints nothing (no duplicate names), and a script-checked list of the spec-named cases has no missing entry (the `…Smoke`
  siblings and plan-internal cases are additions, not renames).
- No build needed if nothing changed; if anything did, rebuild `dsp_systems_tests` once.

### T023 Full-suite run (concurrent lanes)

- One build: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests`,
  zero warnings.
- Then **concurrently**, each to its own scratchpad log: the five DSP exes with `"~[.perf]~[long]"`; the Phase 3 `[long]` shards
  `dsp_systems_tests "AnchorOscillator_PhaseLock*"` and `dsp_systems_tests "ProfundumAnchor_PhaseLock*"`; Phase 1/2 `[long]`
  (`dsp_systems_tests "[profundum][long]~[anchor]~[anchor-chain]"`, `dsp_processors_tests "[edge-fold][long]"`). `wait`, then
  `tail -5` each log. Every lane green.
- Shared-header proof: `git diff --stat 9b1a4cf5 -- dsp/include` is empty and the only untracked file under `dsp/include` is
  `anchor_oscillator.h`. If T010 ran, `ruinae_tests seraphis_tests vorago_tests innexus_tests` are built in the same build call and
  run concurrently (FR-063). No pluginval: no plugin code changes.
- Record every lane's wall clock and the machine's core use; note what to shard or skip next time.

### T024 Footprint, bars, portability and static analysis (concurrent)

Run concurrently, each to its own log, after T023:
- `node specs/profundum-phase3-anchor/check-footprint.js` (add `--fallback` only if T010 ran) → PASS (SC-019, layer grep, API grep);
  quote the output.
- `node specs/profundum-phase3-anchor/check-bars.js check` → no `CHANGED-UNRULED` (SC-020).
- `node specs/profundum-phase3-anchor/check-handoff.js <verdict>` → PASS (SC-021).
- `node tools/check-portability.js` → clean (FR-058, SC-023); afterwards `wsl --shutdown` if it booted WSL.
- `node tools/lint-apple-globals.js` → clean (SC-023).
- `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` (regenerate the ninja preset first so the five new TUs are in
  `compile_commands.json`) → zero findings in the Phase 3 files; fix any (rebuild + re-run the per-push lane).
Quote every output under "Run log" for comply.

### T025 CPU lane (run ALONE, last)

- Precondition: T023 and T024 finished, nothing else running, the machine idle.
- Estimate the wall clock from the previous `run-cpu-tests.js` log for `dsp_systems_tests` before launching (it runs every `[long]`
  case of that exe serially as well as the perf cases, `tools/run-cpu-tests.js:56`). If it exceeds the 2 h background ceiling,
  launch it detached with a status file.
- `node tools/run-cpu-tests.js dsp_systems_tests` → green: `AnchorOscillator_CpuBudget` worst cell ≤ **0.3 %**, Phase 1's
  `ProfundumCore_CpuBudget` and Phase 2's `ProfundumEdgeFold_CpuBudget` still within their budgets.
- If a perf case fails: confirm nothing else ran, idle the machine, re-run that suite alone once; only then treat it as a defect
  (T020 → main loop). Never relax a budget.
- Record figures and wall clock under "Run log" for comply.

---

## Run log

*(Executors append dated entries here: task id, command, wall clock, key figures, outcome.)*

## Defects found

*(Entries from `[P]` tasks per rule 12: case name, output, diagnosis, owning file. T020 resolves them.)*
