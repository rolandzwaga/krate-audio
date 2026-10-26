# Tasks: Profundum Phase 5 — Shape & Character

**Spec:** `specs/profundum-phase5-shape-character/spec.md` (Clarified 2026-10-15) · **Plan:** `specs/profundum-phase5-shape-character/plan.md`
(S0–S14, C-1…C-22, review notes) · **Model:** `specs/profundum-phase5-shape-character/shape-character-model.js` · **Phase base:** commit
`40205b86` on `synth/profundum`
**Status:** Task list (2026-10-15). The build stage has not started. **Plan S1 (C-1…C-22) and the plan's nine open questions are RATIFIED (2026-10-15)**, every item as the recommended option; the rulings are in the spec FR/SC rows and in Clarifications "Plan S1 rulings". The precondition below is satisfied.

## Read this first (every executor)

### Precondition: ratification of plan S1 and the open questions — SATISFIED 2026-10-15 (all items ratified as assumed below)

The plan says its S1 items must be ratified before `tasks.md` runs (`plan.md:7`, `## S1` at `plan.md:81`) and ends with nine open
questions (`plan.md:1384`). The spec's Clarifications (`spec.md:768`) hold Q1–Q8 only. **The tasks below encode the plan's
recommended option for every item.** The main loop must ratify them with the user and write each ruling **into the spec FR/SC row
first, then Clarifications** (workflow header rule 4, `.claude/workflows/profundum-phase.js`) **before T002 starts**. T001 (bench) may
run before ratification. Every test that relies on a C-item cites it in a comment (`// plan S1 C-5`). If the user rules differently,
the main loop rewrites the listed task sections before they run; an executor never relaxes a bar on its own.

| Item | What the tasks assume (plan recommendation) | Spec row(s) to amend on ratification | Tasks affected if ruled otherwise |
|---|---|---|---|
| C-1 | Three extra headers: `core/closed_form_adaa2.h` (L0), `processors/keytracked_tilt.h`, `processors/oversampled_residual_path.h` (L2) | "New components" table (`spec.md:197`) | T002–T005, T007, T009, T010 |
| C-2 ⚠ | Ambiguity = best vs second-best of the candidates {f̂/2, f̂, 2·f̂} < **1 dB** (20·log10 of the SHS salience ratio) | SC-012 ambiguity rule (`spec.md:537`), FR-036 | T008, T018, T019, T024 |
| C-3 ⚠ | (a)(d)(e) and SC-024(a) gate only cells whose **input** reads f₀ unambiguously; (f) gates r_ref = f̂_out/f̂_in on every cell of all four fixtures raw and Fold-100 with zero ambiguous cells | SC-012 (a)(d)(e)(f), SC-024(a) (`spec.md:537, 549`) | T012, T018, T019 |
| C-4 ⚠ | Hysteresis edges = intersection band [max A_lo, min A_hi] over sine/triangle × notes, per Tone | SC-026(a) (`spec.md:551`) | T009, T017, T018, T024 |
| C-5 ⚠ | Fracture = combined law (polarity alternation + bias-flip tanh, plan S5.4); observer stated for Fold-output inputs with hysteresis edges from the Fold-100 saw/sine/triangle intersection | FR-050 (`spec.md:419`), FR-033 (`:385`), SC-012(d), SC-024(a) | T015, T018, T024 |
| C-6 ⚠ | Loudness table calibrated on the mean of kSawAnchor, kTriangleAnchor, kSineAnchor; ±2.5 dB gates those three; kGrowl recorded | SC-002 (`spec.md:527`), FR-056 (`:454`) | T010, T012, T024 |
| C-7 ⚠ | SC-006 span ≥ 0.25 oct gated on kSawAnchor; monotonicity on both fixtures; sine span recorded | SC-006 (`spec.md:531`) | T016 |
| C-8 | Tube and Bite Tone = pre tilt (k = 1, ±6 dB) **and** post tilt (k = 2, ±6 dB); SC-028(d) de-tilts | FR-055 (`spec.md:447`), SC-028(d) | T011, T016 |
| C-9 ⚠ | Fracture SC-010 gate on the n·f₀/2 lattice; n·f₀ value recorded | SC-010 Fracture clause (`spec.md:535`) | T015, T016 |
| C-10 ⚠ | Tube at a branch-local 2× (8× total), input gain −6…+12 dB | FR-021 Tube (no bar change) | T011 |
| C-11, C-12 | ADAA2 in double, ε = 1e-5; Fold θ_max = 2.5 rad | — | T003, T014 |
| C-13…C-18 | Reused-processor choices, SC-027 reference, group-delay record, crossfade laws/lengths, per-channel bypass, observer reads targets | SC-027 wording (C-14) | T009–T017 |
| C-19 ⚠ | Rectify composes `Rectifier`'s laws inside the stage's domain; never runs a `Rectifier` | FR-021 Rectify row (`spec.md:323`) | T009 |
| C-20 ⚠ | **Superseded by the user ruling of 2026-10-15 (Clarifications "T001 CPU bench ruling"):** SC-015 gates the **mono-linked** cell (Phase 2 precedent, bar 2.0 % unchanged) and records the unlinked cell for Phases 7 / 9; C-23 (linear parts at base rate) and C-24 (Fracture phasor functions) are REQUIRED in T009–T015; T001 epoch 2 re-benches first | SC-015 (amended), FR-065 | T001, T009–T015, T021, T027 |
| C-21 | A request equal to `target_` during a crossfade clears `pending_` | SC-029(c) reading | T009, T017 |
| C-22 | ShapeMorph's processed path runs warm while disabled | — | T010, T013 |
| **New (this task list)** | SC-015 runs the one case pinned through `tools/pin-perf-cores.ps1`, not `node tools/run-cpu-tests.js dsp_systems_tests` (deviation 7) | SC-015 "via `node tools/run-cpu-tests.js`" | T021, T027 |

### Deviations from the plan made while cutting these tasks (surface them; do not silently revert)

1. **CMake registration is the first code task (T002), not the last.** Every later task is test-first and its TU must be in the build
   to fail; an unregistered TU silently drops out (`dsp/tests/CMakeLists.txt` lists are ENUMERATED). T002 creates compiling stubs;
   the last group's single CMake task (T023) audits the registration. Phase 1–4 precedent
   (`specs/profundum-phase4-drop-note-behaviour/tasks.md` deviation 1).
2. **`dsp/CMakeLists.txt` gains the six new headers** in the core / processors / systems header lists (plan S11.1 lists only the test
   CMake). Phase 4 did the same.
3. **One extra test header and one extra TU at the processors level:** `dsp/tests/unit/processors/shape_character_metric_helpers.h`
   (differential click count, D-11 ratio, steady delta, centroid C, inharmonic fraction I, level L, seeded noise) and its self-test
   `shape_character_metric_helpers_test.cpp`. Reason: `rectifier_test.cpp` (processors) needs them, and no processors TU includes a
   systems helper (grep 2026-10-15: no `"../systems/` include under `dsp/tests/unit/processors/`). The systems helper (plan S9.2)
   includes this header and adds the SHS, fixture and separation helpers.
4. **`Rectifier_RLawEvidence` (SC-012(c)) lives in `bass_character_rlaw_test.cpp` (systems), not `rectifier_test.cpp`:** the FR-035
   estimator `ProfundumTest::anchorShsF0` is in `dsp/tests/unit/systems/anchor_oscillator_test_helpers.h:227`, which includes
   `anchor_oscillator.h` and the core helpers (`:15–19`).
5. **Probes live in shared test headers** `dsp/tests/unit/systems/bass_character_stage_probe.h` and `shape_morph_probe.h`, because
   ≥ 2 TUs use each probe and defining `detail::XProbe` with different bodies in two TUs is an ODR violation. `detail::RectifierProbe`
   is defined only in `rectifier_test.cpp`. Precedent for the friend: `systems/bloom_engine.h:1666`.
6. **The plan's harvests (T030 r-law, T040 loudness) run at close in the main loop** (lane rule 2026-10-14). The headers ship the plan's
   model seeds between `// HARVEST-BEGIN <name>` / `// HARVEST-END <name>` markers; T024 writes the harvest lane and the paste script.
   No per-push (non-`[long]`) case depends on a harvested value. Plan "T020" (C++ aliasing harvest) becomes per-algorithm non-long
   smoke cells at the model's worst cell (T011, T012, T014, T015) plus the `[long]` grids at close.
7. **SC-015 runs the single case pinned:** `pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe …/dsp_systems_tests.exe -ExeArgs
   "ShapeCharacter_CpuBudget"`. `node tools/run-cpu-tests.js dsp_systems_tests` (plan S11.2, spec SC-015) would also run every `[long]`
   case of the exe (its filter, `tools/run-cpu-tests.js:56`, includes `[long]`; the dsp_systems CPU lane has measured 5 h 17 min). The
   pinning is the one run-cpu-tests uses on Windows (`tools/pin-perf-cores.ps1:18`). Needs the SC-015 row amended (precondition table,
   last row).
8. **Executors do not build or run the test tree** (workflow rule; the gate after each group builds and runs every non-`[long]`,
   non-perf case of the touched exes). Every `[long]` case therefore has a non-long `…Smoke` sibling so each gate exercises the path.
   Tag `[long]` only where the task says so; if a gate log shows an untagged case > 15 s, T022 tags it `[long]` and adds a smoke.
9. **Bite / BITE gain normalisation is written explicitly:** y = P·adaa(v) / `diodeH1Coefficient(g)` (plan S5.4 writes
   y = adaa(v)/c_bite with c_bite "the h1 coefficient"; the P factor makes the branch's h1 equal the input's on the calibration sine,
   which is the stated purpose). Tube, Fold and Fracture keep the plan's wording; each normalisation is tested (h1 transfer at the
   calibration level).
10. **Branch API inside `BassCharacterStage`** (plan S5.2 names the structs, not their methods): every branch struct has
    `prepare(double processRate)`, `reset()`, `setFundamental(float f0Hz)`, `configure(float amount, float tone)` and
    `BranchOut process(float u) noexcept` with `struct BranchOut { float y; float lin; }` (y after the post tilt; lin = ℓ). The probe
    drives a branch directly through these (C-19 law test, SC-027, SC-028).
11. **`KeytrackedTilt` gains two read-only accessors** `pivotHz()` and `tone()` (not in plan S6.1) so the re-key rule (plan S5.3 step 1)
    is testable through the probes.
12. **Run logs are one file per task:** `specs/profundum-phase5-shape-character/evidence/run-log/<Tnnn>.txt`, created by that task.
    Parallel tasks never edit a shared document; a defect found in another task's file is written there as a `DEFECT:` line (T022
    collects them).
13. **Constructor defaults (not ruled by spec or plan):** `ShapeMorph` shape 0 (ROUND), enabled `true`; `BassCharacterStage` algorithm
    Tube, Amount 0, Tone 0.5, offsets 0, f₀ 65.40639 Hz (C2), descriptor `false`. Phase 11 owns parameter defaults; D-1's "default
    SHAPE by listening" is an open question for the user.

### File map

| File | Kind | Layer / target | Created / edited by |
|---|---|---|---|
| `dsp/include/krate/dsp/core/closed_form_adaa2.h` | new header (`core/db_utils.h`, stdlib) | L0 | T002 stub; T003 |
| `dsp/include/krate/dsp/processors/keytracked_tilt.h` | new header (`primitives/one_pole.h`) | L2 | T002 stub; T004 |
| `dsp/include/krate/dsp/processors/oversampled_residual_path.h` | new header (`primitives/polyphase_halfband_oversampler.h`, `biquad.h`, `dc_blocker.h`) | L2 | T002 stub; T005 |
| `dsp/include/krate/dsp/processors/rectifier.h` | new header (L0 + L1 only) | L2 | T002 stub; T007 |
| `dsp/include/krate/dsp/systems/bass_character_stage.h` | new header (L0–L2) | L3 | T002 stub; T009, T011, T014, T015 |
| `dsp/include/krate/dsp/systems/shape_morph.h` | new header (L0–L2) | L3 | T002 stub; T010 |
| `dsp/tests/unit/core/closed_form_adaa2_test.cpp` | new TU | `dsp_core_tests` | T002 stub; T003 |
| `dsp/tests/unit/processors/keytracked_tilt_test.cpp` | new TU | `dsp_processors_tests` | T002 stub; T004 |
| `dsp/tests/unit/processors/oversampled_residual_path_test.cpp` | new TU | `dsp_processors_tests` | T002 stub; T005 |
| `dsp/tests/unit/processors/shape_character_metric_helpers.h` | new test header | — | T006 (read-only after) |
| `dsp/tests/unit/processors/shape_character_metric_helpers_test.cpp` | new TU | `dsp_processors_tests` | T002 stub; T006 |
| `dsp/tests/unit/processors/rectifier_test.cpp` | new TU | `dsp_processors_tests` | T002 stub; T007 |
| `dsp/tests/unit/processors/rectifier_nonfinite_test.cpp` | new TU, **`-fno-fast-math`** | `dsp_processors_tests` | T002 stub; T007 |
| `dsp/tests/unit/systems/shape_character_test_helpers.h` | new test header | — | T008 (read-only after) |
| `dsp/tests/unit/systems/bass_character_stage_probe.h` | new test header | — | T009 (read-only after) |
| `dsp/tests/unit/systems/shape_morph_probe.h` | new test header | — | T010 (read-only after) |
| `dsp/tests/unit/systems/shape_morph_test.cpp` | new TU | `dsp_systems_tests` | T002 stub; T010, T013 |
| `dsp/tests/unit/systems/shape_morph_spectral_test.cpp` | new TU | `dsp_systems_tests` | T002 stub; T012 |
| `dsp/tests/unit/systems/bass_character_stage_test.cpp` | new TU | `dsp_systems_tests` | T002 stub; T009, T014, T017 |
| `dsp/tests/unit/systems/bass_character_spectral_test.cpp` | new TU | `dsp_systems_tests` | T002 stub; T008, T011, T014, T015, T016 |
| `dsp/tests/unit/systems/bass_character_rlaw_test.cpp` | new TU | `dsp_systems_tests` | T002 stub; T018 |
| `dsp/tests/unit/systems/shape_character_chain_test.cpp` | new TU | `dsp_systems_tests` | T002 stub; T019 |
| `dsp/tests/unit/systems/shape_character_nonfinite_test.cpp` | new TU, **`-fno-fast-math`** | `dsp_systems_tests` | T002 stub; T020 |
| `dsp/tests/unit/systems/shape_character_perf_test.cpp` | new TU, `[performance][.perf]`, **not** `-fno-fast-math` | `dsp_systems_tests` | T002 stub; T021 |
| `dsp/CMakeLists.txt`, `dsp/tests/CMakeLists.txt` | existing | — | T002 only (T023 audits) |
| `specs/profundum-phase5-shape-character/bench/` | bench source + lane script | — | T001 |
| `specs/profundum-phase5-shape-character/evidence/` | CSV/TXT written by tests; `close/` lane scripts; `run-log/` | — | tests; T021, T024–T027 |

**Untouchable (zero diff lines vs `40205b86`, FR-064):** everything under `dsp/include/` except the six new headers — in particular
`processors/harmonic_fold.h`, `systems/profundum_core.h`, `systems/anchor_oscillator.h`, `processors/tube_stage.h`,
`processors/wavefolder_processor.h`, `primitives/*`, and `core/*` other than the new file; and every existing test file and helper
(`profundum_core_test_helpers.h`, `anchor_oscillator_test_helpers.h`, `harmonic_fold_test_helpers.h`, `low_frequency_metrics.h`,
`artifact_detection.h`, `allocation_detector.h`). If a reused header looks unusable, STOP AND SURFACE (FR-064's append-only fallback
needs a ruling and the five-plugin regression of SC-023).

### Verified facts these tasks depend on (each read on 2026-10-15 in this session)

Paths relative to `dsp/include/krate/dsp/` unless stated. Re-derive every line anchor from the live file before editing.

- **`PolyphaseHalfbandOversampler`** (`primitives/polyphase_halfband_oversampler.h`): `kMaxFactor = 4` `:34`; `setFactor(std::size_t)`
  `:49` (2 → 2, anything else → 4); `factor()` `:54`; `reset()` `:57`; `resetDownsampler()` `:64`; `upsample(float x, float* out)` `:70`
  (writes `factor()` samples); `template <class AtTwoX> float downsample(const float* in, AtTwoX&& atTwoX)` `:83–84` (hook applied to
  each 2fs-rate sample); `downsample(const float* in)` `:100`; `stateFinite()` `:105`.
- **`HarmonicFold`** (`processors/harmonic_fold.h`): `kCalibrationPeak = 0.3552343859f` `:83`; `kDcCornerHz = 2.0f` `:91`;
  `kOutputCeiling = 6.0f` `:93`; `kMinFundamentalHz = 8`, `kMaxFundamentalHz = 20000` `:102–103`; `prepare(double, std::size_t)` `:121`
  (`ch.dc.prepare(sampleRate, kDcCornerHz)` `:139`); `setAmount(float)` `:168`; mono link `:203–208`; `preCeilingPeak()` `:293`,
  `clearPreCeilingPeak()` `:295`; `static float sanitizeControl(float v, float previous, float lo, float hi)` `:331` (private);
  `reset()` re-links `:382`; members `linked_`, `preCeilingPeak_` `:610–611`. Its notch constants are private (plan S0, C-1).
- **`SVF`** `primitives/svf.h`: `struct SVFOutputs { float low; float high; float band; float notch; }` `:60–65` — read by name only.
- **Smoothers** `primitives/smoother.h`: `OnePoleSmoother` `:134` — `configure(float smoothTimeMs, float sampleRate)` `:160`,
  `setTarget(float)` `:170`, `process()` `:197`, `isComplete()` `:232`, `snapToTarget()` `:257`, `snapTo(float)` `:263`,
  `setSampleRate(float)` `:282`; `LinearRamp` `:305` — `configure(float rampTimeMs, float sampleRate)` `:329`, `setTarget` `:342`,
  `process()` `:370`, `isComplete()` `:409`, `snapTo` `:421`.
- **`OnePoleLP`** `primitives/one_pole.h:56`: `prepare(double)` `:65`, `setCutoff(float hz)` `:75`, `process(float)` `:98`, `reset()` `:128`.
- **`BitCrusher`** `primitives/bit_crusher.h`: `prepare(double)` `:66`, `reset()` `:72` (restores the seed), `setSeed(uint32_t)` `:79`,
  `process(float)` `:91`, `setBitDepth(float)` `:151`, `setDither(float)` `:158`.
- **`TanhADAA`** `primitives/tanh_adaa.h`: `setDrive(float)` `:101`, `process(float)` `:131`.
- **`Biquad`** `primitives/biquad.h:308`, `configure(...)` `:330` (plan S0: `configure(FilterType, float freq, float Q, float gainDb,
  float sampleRate)`). **`DCBlocker`** `primitives/dc_blocker.h`: `prepare(double sampleRate, float cutoffHz = 10.0f)` `:135`,
  `process(float)` `:190`.
- **`TubeStage`** `processors/tube_stage.h`: `prepare(double, size_t)` `:117`, `setInputGain(float dB)` `:171`, `setOutputGain` `:182`,
  `setBias` `:195`, `setSaturationAmount` `:208`, `process(float*, size_t)` `:255`.
- **`WavefolderProcessor`** `processors/wavefolder_processor.h`: `enum class WavefolderModel : uint8_t { Simple, Serge, Buchla259,
  Lockhart }` `:56–61`; `prepare(double, size_t)` `:157`; `setModel` `:207`; `setFoldAmount` `:260`; `setSymmetry` `:273`; `setMix` `:285`;
  `process(float*, size_t)` `:325`.
- **`Asymmetric::tube`** `core/sigmoid.h:333`, **`Asymmetric::diode`** `:358`. **`equalPowerGains`** `core/crossfade_utils.h:50`,
  **`crossfadeIncrement(float durationMs, double sampleRate)`** `:89`. **`detail::isFinite(float)`** `core/db_utils.h:118`,
  **`detail::isFinite(double)`** `:125` (fast-math safe; use them instead of bit-mask clones, `:115–116`).
- **`DropEnvelope`** `processors/drop_envelope.h:29`: `kMaxAmountSemitones = 48` `:31`; `prepare(double)` `:57`; `setAmount(float st)`
  `:83`; `setTime(float ms)` `:87`; `setShape(float)` `:91`; `setRebound(float)` `:95`; `trigger(bool snap)` `:103`; `process()` `:124`
  (d[n] in semitones, 0 when idle).
- **`ProfundumCore`** `systems/profundum_core.h`: `kPitchUpdateInterval = 16` `:98`; `prepare(double, int)` `:129`; `setControls` `:173`;
  `noteOn(float)` `:208`; `processBlock(float* left, float* right, std::size_t n, const float* f0PerSample = nullptr)` `:237` (samples
  the pointer at every 16-sample grid phase, `:257–262`).
- **Recipe states** `processors/spectral_shape_recipe.h:83–84` (`kSineAnchor`, `kTriangleAnchor`, `kSawAnchor`; `kHeavy`, `kHollow`,
  `kGrowl`), definitions `:231–247`.
- **Test helpers.** `dsp/tests/unit/processors/harmonic_fold_test_helpers.h`, namespace `Krate::DSP::HarmonicFoldTest` `:40`:
  `renderedHarmonicPowers(const float*, double fs, double f0, int N)` `:151`; `aliasFftLength(double fs, double f0)` `:168`;
  `struct AliasResult { aliasedDbfs, excludedFraction, totalDb }` `:176`; `aliasedPower(const float*, std::size_t fftLen, double fs,
  double f0)` `:184` (BH7, ±8 bins around every n·f0 < fs/2 and bins 0–8 excluded); `goertzelPhasor(const float*, std::size_t L,
  double fs, double hz)` `:224`; `integerPeriodLength(std::size_t n, double fs, double f0, double minSeconds)` `:240`;
  `calibrationSine(double fs, double f0, double seconds, double peak = kCalibrationPeak)` `:252`; `recipeAdditive(const
  SpectralShapeRecipe::Controls&, double f0, double fs, double seconds, int N = 64)` `:268`; `renderFold(HarmonicFold&, const
  std::vector<float>& in, std::size_t block)` `:379`. `dsp/tests/unit/systems/profundum_core_test_helpers.h`, namespace
  `Krate::DSP::ProfundumTest` `:47`: `bh7PowerSpectrum` `:206`, `renderedHarmonicPowers` `:235`, `aliasFftLength` `:257`,
  `aliasedPower` `:273`, `rmsDbfs(const float*, std::size_t)` `:453`. `anchor_oscillator_test_helpers.h`: `anchorShsF0(const float* x,
  double fs, double f0Hint)` `:227` (x must hold 262144 samples; 15 harmonics, weight 0.84, grid [f0Hint/2.2, 2.2·f0Hint], stage-2
  exact Hann-DTFT refinement `:280ff`); includes Catch2 `:19`.
- **Repo-root `tests/test_helpers/`** (included by bare name): `artifact_detection.h` — `ClickDetectorConfig` `:38` (sampleRate,
  frameSize 512, hopSize 256, …), `ClickDetection` `:72`, `ClickDetector(const ClickDetectorConfig&)` `:99–101`,
  `detect(const float*, size_t)` `:130`; `allocation_detector.h` — `AllocationScope` `:111` (`getAllocationCount()` `:121`).
- **CMake.** `dsp/tests/CMakeLists.txt`: `add_executable(dsp_core_tests` `:22`, last core unit entry `unit/core/xorshift32_test.cpp`
  `:62`; `add_executable(dsp_processors_tests` `:161`, Phase 4 entry `unit/processors/drop_note_behaviour_perf_test.cpp` `:276`;
  `add_executable(dsp_systems_tests` `:348`, last entry `unit/systems/profundum_core_pitch_continuity_test.cpp` `:677`, `)` `:678`;
  `KRATE_DSP_TESTS_DIR` for processors and systems `:767–770`; `-fno-fast-math` block, last entry
  `unit/processors/drop_note_behaviour_nonfinite_test.cpp` `:1166`, `PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"`
  `:1167`. `dsp/CMakeLists.txt`: `KRATE_DSP_CORE_HEADERS` `:82`, last entry `include/krate/dsp/core/drop_curve.h` `:102`, `)` `:103`;
  `KRATE_DSP_PROCESSORS_HEADERS` `:134`, last entry `include/krate/dsp/processors/note_memory.h` `:169`, `)` `:170`;
  `KRATE_DSP_SYSTEMS_HEADERS` `:173`, last entry `include/krate/dsp/systems/anchor_oscillator.h` `:198`, `)` `:199`.
- **Tools.** `tools/run-cpu-tests.js` filter includes `[long]` `:56`, settle `SETTLE_MS` default 60000 `:70`; `tools/pin-perf-cores.ps1`
  usage `-Exe <path> [-ExeArgs ...]` `:17–21`; `tools/run-clang-tidy.ps1` params `-BuildDir -Fix -Target -Quiet -Jobs -FileTimeoutSec`
  `:63–70` (no file-list parameter); `CMakePresets.json` preset `windows-ninja` `:50`; `tools/check-portability.js` and
  `tools/lint-apple-globals.js` exist.
- **Model** (`shape-character-model.js`): salience ratios are `db20` (`:35`, `:102`); `separation()` `:371–377` is the OQ-S rule the
  helper mirrors (R_oe counts when either component of either render is above −40 dB; components clamped to −60 before the ratio).
- **ODR sweep (2026-10-15, this session):** `grep -rnE "(class|struct|enum class|enum|using|namespace) (Rectifier|ShapeMorph|BassCharacterStage|OversampledResidualPath|KeytrackedTilt|ClosedFormAdaa2|DiodeAdaaLaw|RectifierProbe|ShapeMorphProbe|BassCharacterStageProbe|ShapeCharacterTest|HoldEngine|TubeBranch|FoldBranch|RectifyBranch|BiteBranch|CrushBranch|FractureBranch|ShsCandidates|ClickCount|Separation)\b" dsp plugins tests tools`
  → **0 hits**; no `*rectif*`, `*shape_morph*`, `*bass_character*`, `*keytracked*`, `*residual_path*` or `*closed_form*` header exists.
  Hazards never reused: `CharacterProcessor`, `CharacterMode` (`systems/character_processor.h:85, 53`), `MorphEngine`, `SubOscillator`,
  `StereoField`, `TransientDetector`.

### Commands (main loop and gates; executors do not build or run the test tree)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_core_tests dsp_processors_tests dsp_systems_tests > <log> 2>&1; tail -5 <log>
B=build/windows-x64-release/bin/Release
$B/dsp_processors_tests.exe "[rectifier]~[long]" 2>&1 | tail -5
$B/dsp_systems_tests.exe "[shape-character]~[long]" 2>&1 | tail -5
$B/dsp_systems_tests.exe "BassCharacter_RLawMatchesReference_Sine" 2>&1 | tail -5     # one [long] case by name
```

Never `ctest -R`. A positional `"[a]~[long]"` is one AND-term; separate arguments OR; never write `"[a],[b]~[long]"`. Hidden cases
(`[.perf]`, `[.calibration]`) run only when named. Capture any run longer than ~30 s to a log and read the log; never re-run a slow suite
for its output. One build of a tree at a time.

### Rules for every task

1. **Failing test first:** write the test cases before the implementation in the same task. Do NOT build or run the test tree (the
   group's gate does). Re-read every file you wrote and fix anything that obviously would not compile. Zero warnings is a gate
   criterion (MSVC C4244/C4267/C4100: `f` suffixes, explicit casts, `[[maybe_unused]]`).
2. Header-only, `namespace Krate::DSP {` (nested form). Banner comment: layer, "Profundum Phase 5 (specs/profundum-phase5-shape-character)",
   the FRs/SCs it serves. Includes point **downward only**: L0 → stdlib + core; L2 → core + primitives; L3 → core + primitives +
   processors; neither L3 header includes the other or any L3 header. New namespace-scope names: exactly `ClosedFormAdaa2`,
   `DiodeAdaaLaw`, `diodeH1Coefficient`, `KeytrackedTilt`, `OversampledResidualPath`, `Rectifier`, `ShapeMorph`, `BassCharacterStage`,
   and the forward-declared `detail::RectifierProbe`, `detail::ShapeMorphProbe`, `detail::BassCharacterStageProbe`. Everything else is
   nested.
3. PascalCase classes, camelCase functions, `trailing_` members, `kPascalCase` constants. Designated initialisers in brace init; every
   double→float via `static_cast`; no narrowing. Finiteness only via `detail::isFinite`; never `std::isnan` / `std::isfinite`; bit
   comparisons via `std::bit_cast` / `std::memcmp`. No `std::cyl_bessel_j` (libc++ lacks it): compute Bessel-type integrals numerically.
4. Audio-thread methods (`processBlock`, every setter, `reset`) are `noexcept` and allocation-, lock-, exception- and I/O-free; fixed-size
   members only (`std::array`); no `static` mutable data; no SIMD; no `std::function`, no `Oversampler<>`, no `LadderFilter`
   (plan S8, C-13). Setters sanitise with `HarmonicFold::sanitizeControl`'s rule (non-finite keeps the previous value, a denormal
   becomes 0, then clamp) — implement it locally; it is private there.
5. Test helpers are `inline` functions in namespace `Krate::DSP::ShapeCharacterTest`; never an anonymous namespace in a header.
   TU-local helpers go in an anonymous namespace with TU-specific prefixes. When a TU uses both `HarmonicFoldTest` and `ProfundumTest`
   (they define same-named functions), qualify every call.
6. Tags: core `[core][profundum][closed-form-adaa2]`; processors `[processors][profundum][shape-character-parts]` (tilt, path, metric
   helpers) and `[processors][profundum][rectifier]`; systems `[systems][profundum][shape-character]` plus `[shape-morph]` or
   `[bass-character]`; non-finite TUs add `[nonfinite]`; perf `[systems][profundum][shape-character][performance][.perf]`; harvests
   `[.calibration]`. `[long]` only on the cases named `[long]` below, each with its `…Smoke` sibling. **Never `[long]`:** NaN/Inf guards,
   L == R identity, bit-identity, RT safety, state-machine and lifecycle cases.
7. No bit-exact float goldens. `memcmp` only between two renders of the same build in the same process.
8. Notes (compute 440·2^((m − 69)/12) in double, cast once): E0 = MIDI 16 = 20.60172 Hz, E1 = 28 = 41.20344, E2 = 40 = 82.40689,
   C1 = 24 = 32.70320, C2 = 36 = 65.40639, C3 = 48 = 130.81278. P = `HarmonicFold::kCalibrationPeak`.
9. **Fixture conventions** (all spectral tests): input = `HarmonicFoldTest::recipeAdditive(controls, f0, fs, seconds)` (core-equivalent
   at the −12 dBFS RMS calibration); Fold-100 input = a fresh `HarmonicFold`, `prepare(fs, 512)`, `setAmount(100)`,
   `setFundamentalHz(f0)`, `renderFold(fold, in, 512)`. Steady analysis: settle **1 s**, then the analysis frame is the last
   `TestUtils::kLowFrequencyFftSize` = 262144 samples for w_n and SHS, the last `aliasFftLength(fs, f0)` samples for A, I and C, and
   the last `integerPeriodLength(…, 1.0)` samples for L. Render length = 1 s + the longest frame needed. Stereo stages get L == R copies
   unless the case says otherwise. Host block 512 unless stated.
10. Evidence: tests write CSV/TXT under `specs/profundum-phase5-shape-character/evidence/`, addressed as
    `std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase5-shape-character/evidence/<file>"`; create directories with
    `std::filesystem::create_directories`, outside any `AllocationScope`. `dsp_core_tests` has no `KRATE_DSP_TESTS_DIR`: core tests
    write nothing.
11. If a test you write exposes a defect in a file another task owns, do not edit that file: add a `DEFECT: <case> <expected/observed>
    <diagnosis> <owning file>` line to your run-log. T022 resolves them.
12. Never relax a threshold or shrink a workload to pass (FR-070). A bar proven unreachable is STOP AND SURFACE (status "blocked",
    measured table in notes).
13. Write `evidence/run-log/<Tnnn>.txt` for your task: files written, every decision taken, any deviation, any `DEFECT:` line.
14. **Lane rule (2026-10-14):** suites, `[long]` shards, clang-tidy sweeps, CPU/perf lanes and harvests are main-loop work. A task that
    owns one writes the lane script under `evidence/close/` and returns done with notes "deferred to the main loop at close: run
    <script>".

---

## Group 0 — CPU feasibility bench (alone; STOP AND SURFACE)

### T001 Microbench the unlinked Shape + Character cost before any header is written (plan C-20, R-1, S10.7, S14 step 1)

- **Precondition:** nothing else running on the machine (no build, suite, model, other agent).
- **Create** `specs/profundum-phase5-shape-character/bench/shape-character-bench.cpp`. It includes the **real** reused headers
  (`-Idsp/include`: `PolyphaseHalfbandOversampler`, `Biquad`, `DCBlocker`, `OnePoleLP`, `SVF`, `TanhADAA`, `TubeStage`,
  `WavefolderProcessor`, `BitCrusher`, `core/sigmoid.h`) and prototypes the new pieces locally, in an anonymous namespace with a
  `Bench` prefix, exactly as plan S3–S6 specify: the residual path (S3: 1 up, 2 down, three notches {0.50, 0.52, 0.55}·fs with Q
  {1.5, 2.0, 1.5} configured at 2fs, 2 Hz `DCBlocker` on the residual), the double-precision ADAA2 of S4.1 with ε = 1e-5 and the
  `DiodeAdaaLaw` / ramp / abs laws, the first-order `KeytrackedTilt` of S6.1, the six Character branches of S5.4 (Tube with its
  branch 2× and `TubeStage` at 2·F·fs; Fold; Rectify; Bite; Crush with two hold engines and the B-spline step ring; Fracture with
  `TanhADAA`), and the three ShapeMorph regimes of S6.3 with linear weights.
- **Cells** (48 kHz, F = 4, host block 64, kSawAnchor-like input = a 64-partial saw at E1 at P peak; **both channels processed
  independently**, i.e. unlinked, with R offset by 0.37 period): Character at each algorithm (Amount 1, Tone 0.5) including its own
  path; ShapeMorph at SHAPE ∈ {0, 0.25, 0.5, 0.75, 1} including its own path; a **ramp cell** at the worst Character × worst Shape where
  Amount, Tone and SHAPE ramp continuously (every base sample recomputes `KeytrackedTilt::setTone`, `TubeStage::setInputGain` /
  `setOutputGain`, the `BitCrusher` bit depth, `SVF::setCutoff` and g_B, as plan S8 lists); a **Drop cell** (f₀ falling 48 st over 60 ms,
  re-triggered every 250 ms, so every keytracked member is re-keyed per 16-sample grid step).
- Report per cell ns per base sample (stereo) and % of one core = ns·48000/1e7. **Total = worst Character + worst Shape** (each with
  its own path). Compile-time variants: `-DP5_CTRL_INTERVAL=16` (coefficient updates quantised to a 16-sample grid anchored to the
  absolute sample count, plan S8) and `-DP5_RECT_ADAA_FLOAT` (float ADAA2 for the Rectify polynomial laws only, R-1 lever 4).
  A `--quick` flag runs each cell for 0.2 s.
- **Create** `specs/profundum-phase5-shape-character/bench/run-bench.sh` (Git Bash): compile with
  `"/c/Program Files/LLVM/bin/clang++.exe" -std=c++20 -O2 -ffast-math -march=x86-64-v2 -DNDEBUG -Idsp/include <bench.cpp> -o <scratch>/p5-bench.exe`
  (and the two variant binaries); run the base binary **3 times, 60 s apart** (the `SETTLE_MS` default, `tools/run-cpu-tests.js:70`),
  each through `pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe <scratch>/p5-bench.exe`; print the median per cell and the decision
  line below; write everything to `specs/profundum-phase5-shape-character/evidence/run-log/T001-bench.txt`.
- **Executor (allowed in a few minutes):** write both files, compile the base binary once and run it with `--quick` to prove it runs
  and prints every cell (no timing decision from this run). Record compile and quick output in `evidence/run-log/T001.txt`. Then
  **return status "blocked"** — STOP AND SURFACE: "main loop runs `bash specs/profundum-phase5-shape-character/bench/run-bench.sh`
  alone (≈ 4 min), applies the decision rule, records it in `evidence/run-log/T001.txt` and as a 'Measured' paragraph in plan S10.7, then
  resumes the build with `unblock: [T001]`".
- **Decision rule (main loop):** median unlinked Total ≤ 2.0 % → proceed with the plan's design (the planned levers "skip inactive
  regimes" and "F2 caching" are part of it). Total > 2.0 % → re-run with `-DP5_CTRL_INTERVAL=16`, then additionally
  `-DP5_RECT_ADAA_FLOAT`; the first variant ≤ 2.0 % becomes **REQUIRED** in T009–T015 (write it into those task sections and plan S8).
  Still > 2.0 % → return to the user with the table (C-20). The ramp and Drop cells are recorded; if either exceeds 2.0 % the
  control-interval quantisation is REQUIRED regardless. Relaxing the bar or gating a linked cell is never the lever.

**EPOCH 1 RESULT (main loop, 2026-10-15, pinned, median of 3, unlinked):** base Total **4.40 %** (Fracture 2.65 + Shape 0.25
1.74; Tube 2.01, ramp 4.86, Drop 4.52), `ctrl16` 4.67 %, `ctrl16-rectfloat` 4.44 % — the levers do nothing. Returned to the
user per C-20. **User ruling** (spec Clarifications "T001 CPU bench ruling"; SC-015 and FR-065 amended; plan C-20 superseded,
C-23 / C-24 added): SC-015 gates the **mono-linked** cell (Phase 2 precedent, bar 2.0 % unchanged) and records the unlinked
cell; the two optimisations below enter the design and are re-benched before any header.

### T001 — EPOCH 2 (2026-10-15, after the user ruling; this block supersedes the executor instructions above)

**Files:** `specs/profundum-phase5-shape-character/bench/shape-character-bench.cpp`, `specs/profundum-phase5-shape-character/bench/run-bench.sh`,
`specs/profundum-phase5-shape-character/evidence/run-log/T001.txt` (append an "EPOCH 2" section; never delete epoch 1).

- **Add a `--linked` mode** to the bench (and a `linked` / `unlinked` selector to `run-bench.sh`, default `linked`, the other
  recorded): L == R input, zero offsets, ONE channel processed and copied to the other, which is the mono link of plan S2. The
  `P5BENCH` banner and the `total_*` line name the mode. Keep every existing cell, variant and the `--quick` flag.
- **Prototype plan C-24 in the Fracture branch:** s = sin(πφ) from a rotating phasor (complex rotation per oversampled sample,
  re-normalised on every f₀ change and once per block), σ(φ) and tanh(g·β·s) from a rational tanh approximation (absolute error
  ≤ 1e-4 on [−4, 4]; state the polynomial / rational form in a comment with its measured max error over a 10001-point grid,
  printed once at start-up). No `std::sin` / `std::tanh` inside the per-sample loop; `TanhADAA` stays.
- **Prototype plan C-23 in every branch and regime:** pre-tilts (Tube, Bite, BITE emphasis) run at base rate on the input
  **before** `upsample`; post-tilts (all six), ROUND's SVF low-pass and HOLLOW's notch + band run at base rate on the
  **round-tripped** outputs after `downsample` (HOLLOW filters the decimated linear path, weighted by w_H, and never enters the
  oversampled loop). The ADAA / tube / fold / hold nonlinearities are the only per-F-sample work. Keep the residual split
  (lin / res) so the phase match stands.
- Re-measure the per-cell cost structure: print, per Character cell, the F-rate nonlinearity cost and the base-rate filter cost
  separately if cheap to do (optional); the per-cell totals are what the decision uses.
- **Executor:** edit both files, compile the base binary once (same flags), run `--quick` in linked and in unlinked mode to prove
  every cell prints, append the compile line and both quick outputs to `evidence/run-log/T001.txt` under "EPOCH 2", then
  **return status "blocked"** — STOP AND SURFACE: "main loop runs `bash specs/profundum-phase5-shape-character/bench/run-bench.sh
  linked` and `... unlinked` alone, applies the epoch-2 decision rule, records both in `evidence/run-log/T001.txt` and plan S10.7,
  then resumes with `unblock: [T001]`".
- **Decision rule, epoch 2 (main loop):** median **linked** Total ≤ 2.0 % → proceed; C-23 and C-24 are REQUIRED in T009–T015 as
  prototyped. Linked Total > 2.0 % → return to the user with both tables (never relax, never gate a different cell than SC-015
  now names). The unlinked Total, ramp and Drop cells are recorded in `evidence/run-log/T001.txt` and plan S10.7 whatever they read.

---

## Group 1 — CMake registration with compiling stubs (sequential, single task)

### T002 Register every Phase 5 file (deviations 1, 2)

**Precondition: the main loop has recorded the S1 / open-question rulings in the spec rows (see top) and T001's decision.**

- **Create stub headers**, each `#pragma once`, a banner (layer, "Profundum Phase 5 (specs/profundum-phase5-shape-character)", the
  FR/SC rows it will carry) and an empty `namespace Krate::DSP {}`:
  `core/closed_form_adaa2.h` ("Layer 0: Core Utility"), `processors/keytracked_tilt.h`, `processors/oversampled_residual_path.h`,
  `processors/rectifier.h` ("Layer 2: DSP Processor"), `systems/shape_morph.h`, `systems/bass_character_stage.h` ("Layer 3: System").
- **Create stub TUs** (file comment naming the SCs it will hold, per plan S9.1 and the task that fills it, `#include <catch2/catch_test_macros.hpp>`
  and the header(s) under test, no test case): `dsp/tests/unit/core/closed_form_adaa2_test.cpp`;
  `dsp/tests/unit/processors/{keytracked_tilt_test, oversampled_residual_path_test, shape_character_metric_helpers_test, rectifier_test,
  rectifier_nonfinite_test}.cpp`; `dsp/tests/unit/systems/{shape_morph_test, shape_morph_spectral_test, bass_character_stage_test,
  bass_character_spectral_test, bass_character_rlaw_test, shape_character_chain_test, shape_character_nonfinite_test,
  shape_character_perf_test}.cpp`. The systems stubs include all six new headers plus `<krate/dsp/systems/profundum_core.h>` and
  `<krate/dsp/processors/harmonic_fold.h>`, proving they coexist.
- **Edit `dsp/CMakeLists.txt`:** after `include/krate/dsp/core/drop_curve.h` (`:102`) add `# Profundum Phase 5
  (specs/profundum-phase5-shape-character)` and `include/krate/dsp/core/closed_form_adaa2.h`; after
  `include/krate/dsp/processors/note_memory.h` (`:169`) the same comment and `keytracked_tilt.h`, `oversampled_residual_path.h`,
  `rectifier.h` (under `include/krate/dsp/processors/`); after `include/krate/dsp/systems/anchor_oscillator.h` (`:198`) the comment and
  `include/krate/dsp/systems/shape_morph.h`, `include/krate/dsp/systems/bass_character_stage.h`.
- **Edit `dsp/tests/CMakeLists.txt`**, each insertion preceded by a "Profundum Phase 5 (specs/profundum-phase5-shape-character)"
  comment block mapping TU → SCs and saying "ENUMERATED, not globbed":
  - `dsp_core_tests`: after `unit/core/xorshift32_test.cpp` (`:62`): `unit/core/closed_form_adaa2_test.cpp`.
  - `dsp_processors_tests`: after `unit/processors/drop_note_behaviour_perf_test.cpp` (`:276`): `keytracked_tilt_test.cpp`,
    `oversampled_residual_path_test.cpp`, `shape_character_metric_helpers_test.cpp`, `rectifier_test.cpp`,
    `rectifier_nonfinite_test.cpp` (all `unit/processors/`).
  - `dsp_systems_tests`: after `unit/systems/profundum_core_pitch_continuity_test.cpp` (`:677`), before `)` (`:678`): the eight systems
    TUs above (`unit/systems/`).
  - `-fno-fast-math` block: after `unit/processors/drop_note_behaviour_nonfinite_test.cpp` (`:1166`), before `PROPERTIES` (`:1167`):
    `# Profundum Phase 5: injects NaN/Inf bit patterns (SC-017, FR-066). The perf TU stays out (shipping FP mode).`,
    `unit/processors/rectifier_nonfinite_test.cpp`, `unit/systems/shape_character_nonfinite_test.cpp`. Nothing else.
- **Verify (executor, read-only):** re-read both CMake files around each insertion; `git diff --stat` shows only the two CMake files
  plus new files. The Group 1 gate builds `dsp_core_tests dsp_processors_tests dsp_systems_tests` (it reconfigures automatically) and
  runs them; Phase 1–4 counts must be unchanged.

---

## Group 2 — Layer 0/2 parts and the metric helpers (parallel; disjoint new files)

### T003 [P] `ClosedFormAdaa2<Law>`, `DiodeAdaaLaw`, `diodeH1Coefficient` (plan C-1, C-11, S4.1)

**EPOCH 2 (2026-10-15, main-loop ruling; the header is done, only the test constants change).** The epoch-1 executor modelled the
exact loops in IEEE double and found four task-level tolerances below the design's own double-rounding floor (no spec SC sets these
bars; the spec's gates on the laws are SC-010 / SC-011 / SC-021 via the stages). Ruled, inside every spec bar:
1. `ClosedFormAdaa2_DiodeMatchesSigmoid`: the float reference `1 − expf(−1.5x)` cancels near 0, so the comparison is absolute:
   |float(f(x)) − diode(x)| ≤ 2·2⁻²⁴·max(1, |f(x)|) (2 ulp of max(1, |f|)); model worst case 0.9988 of the bar.
2. `ClosedFormAdaa2_SlowRampEqualsLaw`, samples NOT straddling 0, both laws: bar **5e-6** (model: cubic 2.01e-6 at x = 3.84, diode
   1.86e-6 at x = −3.79, from F2 ≈ 51 / h² with h = 1e-4; the algorithm's own bias there is ≈ 2e-8). Straddling bars unchanged.
3. `ClosedFormAdaa2_FallbackBranches`, alternating input: the fallback returns f at the midpoint, so the reference is
   **f(0.3 + 2.5e-6)** within **1e-9** (not f(0.3)); the model matches it to 1.1e-16.
Executor: open the TU, change exactly those constants / references, re-read, return done. Write `evidence/run-log/T003.txt` with the
epoch-1 model table (the file is added to this task's list).

**Files:** `dsp/include/krate/dsp/core/closed_form_adaa2.h`, `dsp/tests/unit/core/closed_form_adaa2_test.cpp`, `specs/profundum-phase5-shape-character/evidence/run-log/T003.txt`. Target `dsp_core_tests`.

- **Tests first** (`[core][profundum][closed-form-adaa2]`; TU-local laws `TestCubicLaw` (f = x³, F1 = x⁴/4, F2 = x⁵/20) and
  `TestAbsLaw` (f = |x|, F1 = ½x|x|, F2 = x²|x|/6) in an anonymous namespace):
  - `ClosedFormAdaa2_LawIdentities`: for `DiodeAdaaLaw`, `TestCubicLaw`, `TestAbsLaw` on 3201 points in [−8, 8] skipping |x| < 1e-3:
    central differences with h = 1e-5 give |(F1(x+h) − F1(x−h))/2h − f(x)| ≤ 1e-9·max(1, |f(x)|) and the same for F2 → F1.
    `F1(0) == 0.0` and `F2(0) == 0.0` exactly; |F1(±1e-12)| ≤ 1e-12 and |F2(±1e-12)| ≤ 1e-12 (continuity at the kink).
  - `ClosedFormAdaa2_DiodeMatchesSigmoid` (C-1): on 10001 points in [−8, 8], `static_cast<float>(DiodeAdaaLaw::f(x))` and
    `Asymmetric::diode(static_cast<float>(x))` differ by ≤ 2 ulp (count `std::nextafter` steps).
  - `ClosedFormAdaa2_SlowRampEqualsLaw`: x[n] = −4 + 1e-4·n until x > 4. For `TestCubicLaw`: |y[n] − f(x[n−1])| ≤ 1e-6 at every n ≥ 2.
    For `DiodeAdaaLaw` and `TestAbsLaw`: the same bound wherever x[n−2], x[n−1], x[n] do not straddle 0; at straddling samples
    |y[n] − f(x[n−1])| ≤ 0.5·1e-4·|f′(0⁺) − f′(0⁻)| + 1e-6 (diode 0.5, abs 2).
  - `ClosedFormAdaa2_FallbackBranches`: a constant input 0.3 for 8 samples gives `y == f(0.3)` within 1e-12 from the third sample on
    (exercises the |x − x2| ≤ ε and |δ| ≤ ε branches); an input alternating 0.3 and 0.3 + 5e-6 (both difference quotients in the
    fallback) stays within 1e-6 of f(0.3) and finite.
  - `ClosedFormAdaa2_ResetAndState`: after 1000 random samples, `reset()` then `process(0.0) == 0.0` exactly; `stateFinite()` true.
  - `DiodeH1Coefficient_MatchesIntegral`: `diodeH1Coefficient(g)` vs a 65536-point midpoint reference of (1/π)∫₀^{2π} f(g sin φ) sin φ dφ
    within 1e-4 relative at g ∈ {0.25, 1, 4, 31.6} (the 256-point rule's error on a C⁰-kinked periodic integrand is O(N⁻²)); and
    `diodeH1Coefficient(1e-4) / 1e-4` within 1e-3 of 1.25 (small-signal slope mean of 1.5 and 1).
- **Implement** exactly plan S4.1: the template with `kEpsilon = 1.0e-5`, members `x1_, x2_, f2x1_, d1_` (double), the D(a, b) /
  ADAA2 recurrence with the cached F2, `reset()`, `stateFinite()` via `detail::isFinite(double)` (`core/db_utils.h:125`);
  `DiodeAdaaLaw::f/F1/F2` with the plan's piecewise formulas; `diodeH1Coefficient(double gain)` = (2/256)·Σ_{k=0}^{255}
  f(gain·sin φ_k)·sin φ_k, φ_k = 2π(k + 0.5)/256 (prepare-time only — say so in its comment). Includes: `core/db_utils.h`, `<cmath>`,
  `<cstddef>` only.

### T004 [P] `KeytrackedTilt` (plan C-1, C-13, S6.1; deviation 11)

**EPOCH 2 (2026-10-15, main-loop ruling; option A, plan S6.1 unchanged).** The epoch-1 executor modelled S6.1 in double and found three
task-level bars the design cannot meet (none is a spec bar; SC-028(a) 0.1 dB at Tone 0.5 and SC-028(b) 1.5 dB per harmonic both pass
with margin). The three amended bars are marked "epoch 2 ruling" in the test list below. Implement exactly as S6.1 says
(`OnePoleLP`, a = exp(−2πfc/fs)); do not switch to a TPT split. Write `evidence/run-log/T004.txt` with the model table.

**Files:** `dsp/include/krate/dsp/processors/keytracked_tilt.h`, `dsp/tests/unit/processors/keytracked_tilt_test.cpp`, `specs/profundum-phase5-shape-character/evidence/run-log/T004.txt`. Target
`dsp_processors_tests`.

- **Tests first** (`[processors][profundum][shape-character-parts]`; responses measured by driving a 1 s sine through a copy of the
  configured object and taking the RMS ratio over whole periods of the last 0.5 s):
  - `KeytrackedTilt_UnityAtPivot`: process rate 192000, `configure(2.0f, 12.0f)`, f₀ = C2: at Tone ∈ {0, 0.25, 0.5, 0.75, 1} the gain
    at 2·f₀ is 0 dB ± **0.05 dB** (epoch 2 ruling: the matched-pole one-pole is not power-complementary, so a fully tilted pivot
    misses unity by ≈ 0.017 dB at C2 and 0.035 dB at C3; SC-028(a) gates Tone 0.5, which is exact); `pivotHz()` == 2·f₀ exactly (float),
    `tone()` returns the last set value.
  - `KeytrackedTilt_FlatAtNeutral` (SC-028(a) part): Tone 0.5 on seeded white noise (xorshift32, seed 0x1234): max |y − x| ≤ 1e-6·max|x|.
  - `KeytrackedTilt_Keytracked` (SC-028(b) part): rate 192000, k = 2, ±12 dB, Tone ∈ {0, 0.25, 0.75, 1}: |H(n·f₀)| for n = 1…16 at C1
    and at C3 differ by ≤ **0.1 dB** per n (epoch 2 ruling from the measured design, worst 0.0835 dB at n = 16; spec SC-028(b) is 1.5 dB).
  - `KeytrackedTilt_ToneDirection`: at Tone > 0.5, |H(n·f₀)| strictly increases with n (n = 1…16, C2, rate 192000); at Tone < 0.5 it
    strictly decreases; at fixed n = 16 it increases with Tone; symmetry is taken over a range **centred on the pivot** (epoch 2 ruling:
    H(x, t) = H(1/x, 1 − t) with x = f/(2·f₀), so n = 1…16 spans x = 0.5…8, not centred): [H(x = 8) − H(x = 1/8)] at Tone 1 equals
    −[H(x = 8) − H(x = 1/8)] at Tone 0 within **0.06 dB** (Group 2 gate ruling: measured 0.0524 dB on the matched-pole design, the one
    cell of the epoch-2 set that had not been measured; the analog prototype is exactly symmetric).
  - `KeytrackedTilt_ClampAndReset`: rate 88200, k = 2, `setFundamentalHz(20000)` → output finite for 1 s of noise (the `OnePoleLP` clamp
    at 0.495·rate); `reset()` then `process(0.0f) == 0.0f`.
- **Implement** plan S6.1 verbatim plus `[[nodiscard]] float pivotHz() const noexcept` (pivotMultiple·f₀ as last set) and
  `[[nodiscard]] float tone() const noexcept`. `setTone` recomputes gL, gH, ν only when the value differs (bit compare) from the
  stored tone; `setFundamentalHz` calls `lp_.setCutoff(k_·f0)`; defaults k = 2, maxDb = 0, tone 0.5. Include `primitives/one_pole.h`
  and `<cmath>`, `<bit>`, `<cstdint>` only. Non-finite arguments are the owners' job (they sanitise first); document that.

### T005 [P] `OversampledResidualPath` (plan C-1, S2, S3)

**EPOCH 2 NOTE:** this task COMPLETED in epoch 1 (header and TU on disk, six cases). If re-dispatched by a cache replay: re-read both
files against this section, fix nothing unless it plainly contradicts the section, and return done with "already complete (epoch 1)".

**Files:** `dsp/include/krate/dsp/processors/oversampled_residual_path.h`, `dsp/tests/unit/processors/oversampled_residual_path_test.cpp`.
Target `dsp_processors_tests`.

- **Tests first** (`[processors][profundum][shape-character-parts]`):
  - `OversampledResidualPath_Constants`: `kNotchRatios == {0.50f, 0.52f, 0.55f}` and `kNotchQs == {1.5f, 2.0f, 1.5f}` literally (comment:
    origin `harmonic_fold.h:300–301`, private there, C-1); `kResidualDcHz == HarmonicFold::kDcCornerHz`; `kMaxFactor == 4`;
    `kFourTimesLimitHz == 100000.0`.
  - `OversampledResidualPath_FactorTable`: after `prepare` at 44100, 48000, 88200, 96000 → `factor() == 4`; at 192000 → 2.
  - `OversampledResidualPath_LinearRoundTripExact` (the SC-027 premise): with `res` all zero, the output equals a reference pair of
    `PolyphaseHalfbandOversampler` (one up, one down, no hook, same factor) **bit-identically** on 8192 samples of seeded white noise at
    48 kHz and at 192 kHz (`lin` = the upsampled input).
  - `OversampledResidualPath_ResidualNotches`: at 48 kHz, `lin` = 0 and `res` = a sine at r·fs (generated at the F·fs rate) for
    r ∈ {0.50, 0.52, 0.55}: after a 0.5 s settle the output RMS is ≥ 60 dB below the residual tone's RMS.
  - `OversampledResidualPath_ResidualDcBlocked`: `res` = 0.1 constant, `lin` = 0, 48 kHz: after 1 s, |mean of the output over 4800
    samples| ≤ 1e-4 (−80 dBFS).
  - `OversampledResidualPath_ResetAndFinite`: `reset()` then zero in → exactly zero out; `stateFinite()` true; at 192 kHz record (INFO)
    the effective DC corner implied by the `DCBlocker` R clamp (plan S3: 3.06 Hz).
- **Implement** plan S3 exactly (class body as printed there; `prepare` sets the factor table, configures the notches at 2·fs, prepares
  the DC blocker at fs with `kResidualDcHz`; `downsample` = `downLin_.downsample(lin)` + `dc_.process(downRes_.downsample(res, hook))`
  where the hook runs the three notches in series; `dcIn_`/`dcOut_` mirrors for `stateFinite`). Heap-free and copyable (the mono link
  copies it).

### T006 [P] Processors-level metric helpers + self-test (deviation 3)

**EPOCH 2 NOTE:** this task COMPLETED in epoch 1 (header and self-test TU on disk, including the extra `ShapeCharacterMetrics_NoiseAndLevel`
case). If re-dispatched by a cache replay: re-read both files against this section, fix nothing unless it plainly contradicts the
section, and return done with "already complete (epoch 1)".

**Files:** `dsp/tests/unit/processors/shape_character_metric_helpers.h`, `dsp/tests/unit/processors/shape_character_metric_helpers_test.cpp`.
Target `dsp_processors_tests`.

- **Header** (namespace `Krate::DSP::ShapeCharacterTest`, all `inline`; includes `"artifact_detection.h"`,
  `"harmonic_fold_test_helpers.h"`, `<cstdint>`, `<vector>`, `<cmath>`):
  - `struct ClickCount { std::size_t counted; std::size_t transitionDetections; std::size_t referenceDetections; std::size_t windowN; };`
    `ClickCount differentialClicks(const float* transition, const float* reference, std::size_t n, double fs, double f0)` — spec
    "Differential click count": `ClickDetector` with a default `ClickDetectorConfig` whose only change is `sampleRate = fs`; N =
    floor(fs/f0/8); a transition detection counts only if the reference has no detection within ±N samples.
  - `double steadyDeltaMax(const float* y, std::size_t begin, std::size_t end)` — max |y[k] − y[k−1]|.
  - `double d11Ratio(const float* y, const float* ga, const float* gb, double Da, double Db, double maxXa, double maxXb, std::size_t
    begin, std::size_t end)` — max over k of |y[k] − y[k−1]| / (ga[k]·Da + gb[k]·Db + |ga[k] − ga[k−1]|·maxXa + |gb[k] − gb[k−1]|·maxXb)
    (spec D-11, per sample).
  - `double centroidOct(const float* y, std::size_t n, double fs, double f0)` — spec C: power-weighted mean of log₂(f/f₀) over all BH7
    bins of the last `HarmonicFoldTest::aliasFftLength(fs, f0)` samples with 20 Hz ≤ f ≤ min(fs/2, 20000).
  - `double inharmonicFractionDb(const float* y, std::size_t n, double fs, double f0)` — spec I: power outside ±8 bins of every
    n·f₀ < fs/2 (bins 0–8 excluded) over total power, dB, on the same frame (the `aliasedPower` mask applied to the total).
  - `double levelDb(const float* y, std::size_t n, double fs, double f0)` — RMS dBFS over the last `integerPeriodLength(n, fs, f0, 1.0)`
    samples, unwindowed.
  - `std::vector<float> whiteNoise(std::size_t n, std::uint32_t seed, double rmsDbfs)` — xorshift32, uniform, scaled to the RMS.
- **Self-tests first** (`[processors][profundum][shape-character-parts]`):
  - `ShapeCharacterMetrics_StepIsCounted`: 0.5 s sine at E1, 48 kHz, peak 0.5; transition = the same plus a +0.5 step at sample 12000;
    reference = the sine: `counted ≥ 1`.
  - `ShapeCharacterMetrics_IdenticalIsZero`: transition == reference == `recipeAdditive(kSawAnchor, E1, 48000, 0.5)`: `counted == 0`;
    INFO `referenceDetections` (the spec premise "12–115 per 0.5 s").
  - `ShapeCharacterMetrics_D11Ratio`: a 10 ms linear crossfade between two sines (E1 and 3·E1) gives a ratio ≤ 1 + 1e-6; the same signal
    with a 0.1 spike at one sample gives a ratio > 1.
  - `ShapeCharacterMetrics_Centroid`: a pure sine at 4·f₀ (f₀ = C2, 48 kHz) reads C = 2.0 ± 0.01 oct; a sine at f₀ reads 0.0 ± 0.01.
  - `ShapeCharacterMetrics_InharmonicFraction`: `recipeAdditive(kSawAnchor, E1)` reads I ≤ −100 dB; plus a sine at 1.5·f₀ at −30 dB
    re the saw's RMS reads I within 1 dB of −30 dB.

---

## Group 3 — `Rectifier` and the systems helper (parallel; disjoint new files)

### T007 [P] `Rectifier` (FR-001–FR-005; SC-010/011/014/016/017/018/019/021 Rectifier arms; plan S4.2, S4.3, S5.5, C-1)

**Files:** `dsp/include/krate/dsp/processors/rectifier.h`, `dsp/tests/unit/processors/rectifier_test.cpp`,
`dsp/tests/unit/processors/rectifier_nonfinite_test.cpp`. Target `dsp_processors_tests`. Depends on T003, T005, T006.

- **Tests first** in `rectifier_test.cpp` (`[processors][profundum][rectifier]`; `namespace Krate::DSP::detail { struct RectifierProbe {
  static void snapBlend(Rectifier& r, float b); }; }` defined **only** here — it calls `blend_.snapTo(b)` after `setBlend(b)`):
  - `Rectifier_Constants` (C-1): `Rectifier::kNotchRatios`, `kNotchQs`, `kResidualDcHz`, `kFourTimesLimitHz` equal
    `OversampledResidualPath`'s; `static_assert(Rectifier::latencySamples() == 0)`.
  - `Rectifier_TransferLaw` (SC-014(a)): x on 4001 points in [−2, 2] (0 included) × b ∈ {0, 0.25, 0.5, 0.75, 1}: |`transfer(x, b)` −
    ((1 − b)·max(x, 0) + b·|x|) in double| ≤ 1e-6; and `transfer(x, b)` equals `linearCoefficient(b)·x + (1 + b)/2·|x|` within 1e-7.
  - `Rectifier_SineMakeup` (S5.5): `sineMakeup(0)` = 1.834 ± 0.001, `sineMakeup(1)` = 2.297 ± 0.001, and equals √(0.5/ac²(b)) with
    ac²(b) = (1 + b²)/4 − (1 + b)²/π² within 1e-6 at b ∈ {0, 0.25, 0.5, 0.75, 1}.
  - `Rectifier_ProcessedHarmonics` (SC-014(b)): `calibrationSine(48000, C3, 7.0)` (peak P), b ∈ {0, 0.5, 1}, after 1 s: for n = 1…8,
    10·log10(w_n / w_max) (`HarmonicFoldTest::renderedHarmonicPowers`, last 262144 samples) matches 20·log10(a_n / a_max) of the analytic
    series (h1 = (1 − b)/2·A, h(2k) = (1 + b)/2·A·4/(π(4k² − 1)), odd n ≥ 3 → 0) within 0.1 dB wherever the analytic level is above
    −60 dB re the largest; INFO the 2 Hz DC-pole attenuation at h2…h8 (plan S4.3: < 0.001 dB at C3).
  - `Rectifier_AliasingSmoke`: b = 1, E2, 44.1 kHz, Fold-100 kGrowl → A ≤ −80 dBFS; b = 0.5, raw kSawAnchor, same note/rate → A ≤ −80.
  - `Rectifier_AliasingE0E2` `[long]` (SC-010 Rectifier arm): b ∈ {0, 0.5, 1} × {E0, E1, E2} × {44100, 48000, 96000} × {kSawAnchor,
    kGrowl} × {raw, Fold-100}: A ≤ −80 dBFS. Per cell write A_in (same mask on the input), G (h1 power gain, w₁ out/in) and
    A_added = 10·log10(max(P_alias,out − G·P_alias,in, 0)) to `evidence/sc010-rectifier.csv`. A failing Fold-100 cell whose A_added ≤ −80
    still FAILS with the message "FR-070 candidate: input residual" (never passed silently).
  - `Rectifier_DcBound` (SC-011 arm): 48 kHz, b ∈ {0, 0.5, 1} × {E0, E1, E2} × {kSawAnchor, kGrowl} × {raw, Fold-100}: after 1 s,
    20·log10|mean over `integerPeriodLength(…, 1.0)`| ≤ −80 dBFS.
  - `Rectifier_BlendStepClickFree` (SC-021 arm, FR-005): b 0 → 1 and 1 → 0, as a single `setBlend` step and as a 20 ms ramp (setBlend
    every 16 samples, linearly interpolated), at E1 and C2 on kSawAnchor and kGrowl, 48 kHz; reference = b held at the start value,
    phase-aligned: `differentialClicks(...).counted == 0`. **Positive control:** the same 0 → 1 step applied through
    `RectifierProbe::snapBlend` at the input's negative peak must give `counted ≥ 1`.
  - `Rectifier_NoAllocation` (SC-016 arm): `AllocationScope` around `processBlock` (512, stereo), `setBlend`, `reset()` → 0.
  - `Rectifier_PartitionInvariant` (SC-018 arm): 1 s kGrowl E1 with a `setBlend(0.8f)` at sample 20000 (applied between blocks at that
    absolute index): block sizes {1, 17, 32, 64, 512} give bit-identical output (`memcmp`); two renders from `reset()` are bit-identical;
    prepare(48k) → prepare(96k) → prepare(48k) equals a fresh 48 kHz render bit-identically.
  - `Rectifier_SampleRates` (SC-019 arm): 44100, 48000, 88200, 96000, 192000 × b ∈ {0, 0.5, 1} on kSawAnchor E1: every sample finite and
    |y| ≤ 6.
  - `Rectifier_Lifecycle`: unprepared `processBlock` leaves the buffer unchanged; null pointers or n = 0 are no-ops; `right == left`
    equals channel 0 of a stereo render with L == R; L == R input → L == R output bit-identically; a differing R input unlinks
    (outputs differ) and `reset()` relinks; `preCeilingPeak()` ≥ max|y| of the last render.
- **Tests first** in `rectifier_nonfinite_test.cpp` (`[processors][profundum][rectifier][nonfinite]`; NaN/±Inf built from bit patterns
  with `std::bit_cast<float>(0x7FC00000u)`, `0x7F800000u`, `0xFF800000u`):
  - `Rectifier_NonFiniteRecovery` (SC-017 arm): kSawAnchor E1, b = 0.5, 48 kHz, a 16-sample NaN / +Inf / −Inf burst at 1.0 s: every
    output sample finite (`detail::isFinite`); `stateFinite()` true within 512 samples after the burst; on the frame starting 100 ms after
    the burst end, L, E_low, E_high (from `HarmonicFoldTest::renderedHarmonicPowers`) within 0.1 dB and C (`centroidOct`) within 0.01 oct
    of the same frame of a burst-free render.
  - `Rectifier_NonFiniteSetter`: `setBlend(NaN)`, `setBlend(+Inf)`, `setBlend(−Inf)` mid-render leave the output bit-identical to the
    render without those calls.
- **Implement** plan S4.2 (API as printed, plus the four public constants equal to `OversampledResidualPath`'s, C-1) and S4.3: per base
  sample sanitise → `blend_.process()` → `up.upsample` → per F-sample y = (1 − b)·ramp.process(u) + b·abs.process(u), lin = c(b)·uPrev,
  res = y − lin → `downLin` + DC(`downRes` with the three-notch hook) → clamp ±6 → peak. Channel struct and mono link as S4.2 / S2;
  smoother `OnePoleSmoother` 10 ms (`kBlendSmoothMs`). Includes only L0/L1: `core/closed_form_adaa2.h`, `core/db_utils.h`,
  `primitives/polyphase_halfband_oversampler.h`, `primitives/biquad.h`, `primitives/dc_blocker.h`, `primitives/smoother.h`.

### T008 [P] Systems helper `shape_character_test_helpers.h` + helper self-tests (plan S9.2; C-2, C-3; OQ-S)

**Files:** `dsp/tests/unit/systems/shape_character_test_helpers.h`, `dsp/tests/unit/systems/bass_character_spectral_test.cpp` (first
content). Target `dsp_systems_tests`. Depends on T006.

- **Header** (namespace `Krate::DSP::ShapeCharacterTest`, `inline`; includes `"../processors/shape_character_metric_helpers.h"`,
  `"../processors/harmonic_fold_test_helpers.h"`, `"anchor_oscillator_test_helpers.h"`, `<krate/dsp/processors/harmonic_fold.h>`,
  `<krate/dsp/processors/spectral_shape_recipe.h>`):
  - Note constants `kE0Hz … kC3Hz` (rule 8) and `kFixtures` (name + `Controls` for sine, triangle, saw, growl).
  - `struct Descriptors { double L, Elow, Ehigh, RoeNum, RoeDen, Thd, I, C; std::vector<double> w; }` and
    `Descriptors describe(const float* y, std::size_t n, double fs, double f0)`: w_n from `ProfundumTest::renderedHarmonicPowers` on the
    last 262144 samples (n·f₀ < fs/2); Elow = Σ_{2..8} w / Σ w, Ehigh = Σ_{n≥9} w / Σ w, RoeNum = Σ_{odd≥3} w / Σ w, RoeDen = Σ_{even≥2} w /
    Σ w, Thd = Σ_{n≥2} w / w₁ — all in dB, **unclamped**; I, C, L from the T006 helpers.
  - `struct Separation { double maxDb; const char* which; double dC; bool distinct; }`, `Separation separate(const Descriptors& a,
    const Descriptors& b)`: the OQ-S floor exactly as `shape-character-model.js:371–377` — Elow, Ehigh, I count if either side > −40 dB,
    differences of values clamped at −60; R_oe counts if any of the four components > −40 dB, value = clamp(num) − clamp(den); THD counts
    if either side > −40 dB re w₁. distinct = maxDb ≥ 3.0 or dC ≥ 0.25. `double amountMoved(a, b)` = the same rule over THD, I and R_oe
    only (SC-007).
  - `void levelMatch(std::vector<float>& y, double targetDb, double fs, double f0)` (scale so `levelDb` equals target).
  - `std::vector<float> fixture(const SpectralShapeRecipe::Controls&, double f0, double fs, double seconds)` = `recipeAdditive`;
    `std::vector<float> fold100(const std::vector<float>& in, double fs, double f0)` per rule 9.
  - `struct ShsCandidates { double fHat, sHalfDb, sOneDb, sDoubleDb, bestMinusSecondDb; bool ambiguous, readsF0; }`,
    `ShsCandidates shsCandidates(const float* x, double fs, double f0)`: fHat = `ProfundumTest::anchorShsF0(x, fs, f0)`; the saliences
    S(f̂/2), S(f̂), S(2f̂) use the stage-2 exact-DTFT score of `anchor_oscillator_test_helpers.h` (15 harmonics, weight 0.84, Hann over
    262144 — re-implemented here, same constants), each maximised by golden section over ±2 cents; dB = 20·log10 of the ratio to the
    best; ambiguous = best − second < **1 dB** (C-2); readsF0 = !ambiguous && |1200·log2(f̂/f₀)| ≤ 1 (C-3).
  - `double centsBetween(double a, double b)`.
- **Self-tests first** in `bass_character_spectral_test.cpp` (`[systems][profundum][shape-character][bass-character]`):
  - `SeparationBar_FloorControl` (SC-005 positive control, spec literal): `recipeAdditive(kSawAnchor, E1, 48000, 7)` plus an inharmonic
    sine at 1.5·f₀ at −70 dB re total vs the same with the tone at −130 dB → `separate(...).distinct == false`; the same pair with −30 dB vs
    −130 dB → `distinct == true`.
  - `ShapeCharacterShs_SineReadsF0`: kSineAnchor E1 and C3 → readsF0, not ambiguous, S(f̂/2) − S(f̂) = −1.51 ± 0.1 dB (plan C-2 table);
    `shsCandidates(...).fHat` equals `anchorShsF0` exactly.
  - `ShapeCharacterShs_FullWaveSineReadsOctave`: |x| of kSineAnchor (computed in the test) at E1 → f̂/f₀ = 2 within 1 cent,
    S(2f₀)/S(f₀) = +2.06 ± 0.2 dB (plan S10.1) — not ambiguous under the 1 dB rule.
  - `ShapeCharacterShs_GrowlReadsOctave`: raw kGrowl E1 → f̂/f₀ = 2 within 1 cent, readsF0 false (C-3 premise, +3.43 dB).
  - `ShapeCharacterDescriptors_Saw`: kSawAnchor C2 → Thd, Elow, Ehigh finite; `separate(d, d).distinct == false`.

---

## Group 4 — the two L3 skeletons (parallel; disjoint new files)

### T009 [P] `BassCharacterStage` skeleton + Rectify + probe (FR-020, FR-022, FR-026–FR-031, FR-036, FR-052–FR-054, FR-057; plan S5.1–S5.3, S5.4 Rectify, S5.6, S5.7, C-16–C-19, C-21; deviation 10)

**Files:** `dsp/include/krate/dsp/systems/bass_character_stage.h`, `dsp/tests/unit/systems/bass_character_stage_probe.h`,
`dsp/tests/unit/systems/bass_character_stage_test.cpp`. Target `dsp_systems_tests`. Depends on T003, T004, T005, T007, T008.

- **Header contract** — plan S5.1 API exactly (names, constants `kSwitchCrossfadeMs = 10`, `kBypassCrossfadeMs = 5`,
  `kSwitchCrossfadeExponent = 1.493f`, `kParamSmoothMs = 10`, `kMaxChannelOffset = 0.15f`, `kOutputCeiling = 6`, f₀ range [8, 20000]);
  no `setAlgorithmBlend`. Add public `struct FlipEdges { float lo; float hi; };` and
  `static constexpr std::array<FlipEdges, 5> kRectifyFlipEdges` / `kFractureFlipEdges` at Tone {0, 0.25, 0.5, 0.75, 1}, each wrapped in
  `// HARVEST-BEGIN kRectifyFlipEdges` … `// HARVEST-END kRectifyFlipEdges` (and the Fracture pair). Provisional seeds (plan S5.7, C-4,
  C-5): Rectify {0.837, 0.912}, {0.812, 0.8905}, {0.787, 0.869}, {0.755, 0.8455}, {0.723, 0.822}; Fracture {0.326, 0.342} at all five
  Tones. State layout plan S5.2: per channel an `OversampledResidualPath`, the six branch structs `TubeBranch tube; FoldBranch fold;
  RectifyBranch rect; BiteBranch bite; CrushBranch crush; FractureBranch frac;` with the members of the S5.2 table, the smoothers and
  `uDelay[3]`; no union, no variant. Branch API per deviation 10. **In this task only `RectifyBranch` gets its law (S5.4 Rectify);
  the other five branches return the identity `{ y = u[n−1], lin = u[n−1] }` until T011/T014/T015** — say so in a comment on each.
- **Seams** (private flags, default false, set only through the probe; one predictable branch each): `seamIdentityLaw_` (SC-027: the
  running branch returns y = lin = its own aligned input), `seamRawDry_` (SC-027 control: the dry share is mixed from the base-rate
  input), `seamNaiveBaseRate_` (SC-010 control: Bite at base rate, no path, no ADAA), `seamHardSwitch_` (switch without crossfade),
  `seamUnsmoothed_` (smoothers snap on every setter), `seamHardBypass_` (bypass edge without the 5 ms ramp), `seamPreToneNeutral_` /
  `seamPostToneNeutral_` (SC-028(d)), plus counters `maxRunningBranches_`, and (filled by T014) `crushMaxOccupancy_`,
  `crushRetirements_`.
- **Probe header** `bass_character_stage_probe.h`: `namespace Krate::DSP::detail { struct BassCharacterStageProbe { … }; }` with static
  functions for every seam setter; `runningBranchCount`, `maxRunningBranchCount`, `resetMaxRunning`; `linked`; `current`, `target`,
  `pending` (+ `hasPending`); `flippedRectify`, `flippedFracture`; `const KeytrackedTilt& preTilt(const BassCharacterStage&,
  BassCharacterStage::Algorithm, int ch)` and `postTilt(...)`; `BranchOut runBranch(BassCharacterStage&, Algorithm, int ch, float u)`
  (drives a branch directly at the current smoothed settings); `float branchLinearCoefficient(...)`; `crushMaxOccupancy`,
  `crushRetirements`; `float tubeSaturationAmount(...)`. Every accessor T011–T021 need is here; later tasks never edit this header
  (they record a `DEFECT:` if something is missing).
- **Tests first** (`[systems][profundum][shape-character][bass-character]`):
  - `BassCharacter_Lifecycle`: `static_assert(BassCharacterStage::latencySamples() == 0)`; unprepared `processBlock` leaves buffers
    unchanged; null / n = 0 no-op; `right == left` equals channel 0 of an L == R stereo render; prepare(48000, 512) then one 4096-sample
    call equals eight 512-sample calls bit-identically (long calls are split, Edge Cases).
  - `BassCharacter_AmountZeroBitIdenticalRectify` (SC-008 preview): Amount 0 from the start → output == input bit-identically from
    sample 0; Amount 0.7 for 0.5 s then 0 → bit-identical from 100 ms after the last change (C-16 settle).
  - `BassCharacter_RectifyLawMatchesRectifier` (C-19): via `runBranch` with smoothers snapped and the post tilt neutral, a slow ramp
    (Δ = 1e-4·P per F-sample over [−4P, 4P]) at Amount ∈ {0.2, 0.4, 0.55, 0.7, 0.85, 1}: y = (1 − wet)·x[n−1] + wet·mk(b)·
    `Rectifier::transfer(x[n−1], b)` within 1e-5 at every sample whose x[n−2..n] do not straddle 0 (wet = min(1, A/0.4), b =
    smoothstep((A − 0.4)/0.6), mk = `Rectifier::sineMakeup(b)`); `branchLinearCoefficient` == (1 − wet) + wet·mk·
    `Rectifier::linearCoefficient(b)` exactly.
  - `BassCharacter_SwitchMachine` (S5.6, D-7, C-21, FR-057): via the probe, sample by sample at 48 kHz (crossfade length =
    ceil(1 / `crossfadeIncrement(10, 48000)`) samples): `setAlgorithm(current)` starts nothing; A → B then requests C, D during the
    crossfade → pending D only, at the end current B and a new crossfade B → D starts at once; A → B then C then B → pending cleared,
    ends at B, no further crossfade; A → B then C then A → ends with B → A started at the end; `maxRunningBranchCount ≤ 2` throughout.
  - `BassCharacter_ObserverBasics` (FR-030, FR-036, C-18): Tube, Fold, Bite, Crush → r == 1 for Amount ∈ {0, 0.5, 1}; Rectify with the
    default descriptor → 1 everywhere; Rectify with `setInputHalfWaveSymmetric(true)`, Tone 0.5, Amount up in 0.005 steps → r becomes 2
    exactly once, at the first Amount ≥ `kRectifyFlipEdges[2].hi`, and down → 1 at the first Amount ≤ `.lo`; `reset()` at an Amount
    inside the band → r == 1; `setChannelOffsets(0.15f, 0.15f)` never changes r; during a Rectify → Tube crossfade r == 1 from the
    `setAlgorithm` call on; a queued (pending) request leaves r unchanged.
  - `BassCharacter_F0FeedRekey` (FR-052, S5.3 step 1): `setFundamentalHz(C3)` while Tube runs, then `setAlgorithm(Rectify)` → after the
    first processed sample the Rectify post tilt's `pivotHz()` == 2·C3; same after leaving bypass (Amount 0 → 0.5) and after `reset()`;
    `setFundamentalHz(5)` clamps to 8 and `setFundamentalHz(30000)` to 20000 (read back through the tilt pivot); an `f0PerSample` array of
    constant C2 gives output bit-identical to a null pointer with `setFundamentalHz(C2)` (SC-025(e) preview).
  - `BassCharacter_MonoLink`: L == R input with zero offsets → L == R bit-identical and `linked == true`; `setChannelOffsets(0.1f, 0)` →
    `linked == false`; `reset()` → `linked == true`.
- **Implement** plan S5.3 (f₀ step with the re-key rule; sanitise; bypass: if e == 0 and both effective Amount targets are 0 copy x;
  leaving bypass resets and re-keys the running branches and paths, then ramps e over 5 ms (`LinearRamp`); smoothers and switch machine;
  upsample, branch loop accumulating lin/res with the crossfade gains g_out = cos(πt/2)^α, g_in = sin(πt/2)^α; downsample;
  y = (1 − e)·x + e·z; clamp ±6; peak), S5.6 with C-21, S5.7 observer updated in the setters from the **centre targets** (C-18) with the
  hysteresis tables interpolated linearly in Tone, Rectify per S5.4 (wet, b, mk, ℓ), per-channel effective Amount/Tone = centre ±
  offset clamped to [0, 1] (FR-027, C-17), `prepare` prepares every member (TubeStage at 2·F·fs with maxBlock 2·F,
  WavefolderProcessor at F·fs with maxBlock F, tilts at F·fs) and fills nothing else yet. If T001 made a lever REQUIRED, implement it
  here.

### T010 [P] `ShapeMorph` complete + probe + lifecycle tests (FR-010–FR-016, FR-018, FR-019, FR-052, FR-056; plan S6.2–S6.4, C-6, C-22)

**C-23 design gap (from T001 epoch 2, resolve here):** for SHAPE in (0.5, 1) BITE and HOLLOW are both on, and with BITE's emphasis
tilt moved before the shared upsample (C-23), HOLLOW's base-rate input carries that emphasis. The bench costs the fix as one
base-rate first-order inverse shelf (the exact inverse of the emphasis tilt) on HOLLOW's input; use that form (a second upsample
is not costed and not allowed without re-benching). Record the choice in `evidence/run-log/T010.txt`.

**Files:** `dsp/include/krate/dsp/systems/shape_morph.h`, `dsp/tests/unit/systems/shape_morph_probe.h`,
`dsp/tests/unit/systems/shape_morph_test.cpp`. Target `dsp_systems_tests`. Depends on T003, T004, T005, T008.

- **Header contract:** plan S6.2 API and constants exactly; `kLoudnessTableDb` is a `static constexpr std::array<float, 101>` between
  `// HARVEST-BEGIN kLoudnessTableDb` / `// HARVEST-END kLoudnessTableDb`, provisionally the linear interpolation over SHAPE of the plan
  S10.6 seed (1.31, 1.26, 0.80, 3.08, 3.76 dB at 0 / 0.25 / 0.5 / 0.75 / 1). Regimes, weights, alignment, reset-on-entry with re-key,
  loudness after `downsample`, the warm path (C-22), keytracked recompute on every f₀ or smoothed-s change, SVF clamp: plan S6.3. State:
  plan S6.4 (including `cBiteShape_[65]` filled in `prepare` with `diodeH1Coefficient(g_B(t₂))`; BITE output = P·adaa(v)/
  `cBiteShape_(t₂)`, deviation 9). Seams (private, probe-only): `seamUnsmoothed_` (SHAPE snaps), `seamHardRegime_` (weights become the
  nearest regime only), `seamLoudnessBypassed_` (table not applied — the T024 harvest uses it).
- **Probe header** `shape_morph_probe.h`: `namespace Krate::DSP::detail { struct ShapeMorphProbe { … }; }` — seam setters; `std::array<float,
  3> weights(const ShapeMorph&)`; `runningRegimeCount`, `maxRunningRegimeCount`, `resetMaxRunning`; `linked`; `engage` (current e);
  `bool regimeStateWarm(const ShapeMorph&)` (every SVF / tilt / ADAA state finite and at least one nonzero); `float smoothedShape`.
  Later tasks never edit it.
- **Tests first** in `shape_morph_test.cpp` (`[systems][profundum][shape-character][shape-morph]`):
  - `ShapeMorph_Lifecycle`: `static_assert(ShapeMorph::latencySamples() == 0)`; unprepared no-op; null / 0 no-op; `right == left`;
    long calls split (one 4096 call == eight 512 calls bit-identical); prepare 48k → 96k → 48k equals a fresh 48k render bit-identically.
  - `ShapeMorph_RegimeWeights` (FR-011): with `seamUnsmoothed_` at SHAPE ∈ {0, 0.1, 0.25, 0.5, 0.6, 0.75, 1}: `weights` equal plan S6.3's
    w_R, w_H, w_B formulas within 1e-6, at most two are nonzero, and exactly one is nonzero at 0, 0.5 and 1.
  - `ShapeMorph_DisabledBitIdentical` (SC-008 Shape part, C-22): at SHAPE ∈ {0, 0.5, 1}, `setEnabled(false)` after 0.5 s of kSawAnchor E1;
    from 100 ms after the call, both output channels are bit-identical to the input; just before re-engaging, `regimeStateWarm == true`.
  - `ShapeMorph_LREqual` (SC-013(a) Shape part): L == R input at SHAPE ∈ {0, 0.25, 0.5, 0.75, 1} → L == R bit-identical, `linked == true`.
  - `ShapeMorph_NullPointerEqualsConstantFeed` (SC-025(e) Shape part): null pointer with `setFundamentalHz(C2)` vs a constant C2
    `f0PerSample` array → bit-identical, at SHAPE 0.5 with a SHAPE ramp 0 → 1 inside the render.
- **Implement** as above; `prepare` prepares every regime member at F·fs, fills `cBiteShape_`, resets, re-keys with the default f₀;
  nothing in `processBlock` evaluates a Fourier integral. If T001 made a lever REQUIRED, implement it here.

---

## Group 5 — Character Bite + Tube, ShapeMorph spectral gates, ShapeMorph automation (parallel; disjoint files)

### T011 [P] Character **Bite** and **Tube** (FR-021, FR-055, FR-061; plan S5.4 Bite / Tube, C-8, C-10, C-11; deviation 9)

**Files:** `dsp/include/krate/dsp/systems/bass_character_stage.h`, `dsp/tests/unit/systems/bass_character_spectral_test.cpp`. Target
`dsp_systems_tests`. Depends on T009.

- **Tests first** (`[systems][profundum][shape-character][bass-character]`), each at 48 kHz unless stated:
  - `BassCharacter_BiteSmoke`: (i) h1 transfer on `calibrationSine(48000, C2, 7)` at Amount ∈ {0.25, 1}, Tone 0.5: |20·log10(|h1_out| /
    |h1_in|)| ≤ 0.25 dB (`goertzelPhasor` over `integerPeriodLength`); (ii) A ≤ −80 dBFS on Fold-100 kSawAnchor at E0 / 44.1 kHz and on
    Fold-100 kGrowl at E2 / 44.1 kHz, Amount 1, Tone ∈ {0, 1} (model worst −83.3, plan S10.3); (iii) DC ≤ −80 dBFS on raw kSawAnchor E1,
    Amount 1; (iv) `amountMoved(describe(Amount 0), describe(Amount 1)) ≥ 3 dB` or ΔC ≥ 0.25 oct on kSawAnchor E1, Tone 0.5 (model
    6.27 dB).
  - `BassCharacter_TubeSmoke`: the same four with the Tube cells (model: A −85.3 at E2 / 44.1 kHz; SC-007 4.89 dB / 0.40 oct); plus
    `tubeSaturationAmount == 1.0f` after `prepare` and after `reset()` (the `tube_stage.h` bypass threshold is never crossed, spec Edge
    Case).
  - `BassCharacter_ToneTiltsCoupled` (C-8): for Tube and Bite at Tone ∈ {0, 1}, `preTilt(...).pivotHz()` == f₀ and
    `postTilt(...).pivotHz()` == 2·f₀; both `tone()` equal the channel's smoothed Tone.
- **Implement** plan S5.4 **Bite** (tp = pre (k = 1, ±6 dB); v = g·tp/P, g = 10^{(−12 + 30A)/20}; y = P·`ClosedFormAdaa2<DiodeAdaaLaw>`(v)
  / `diodeH1Coefficient(g)` via the 65-point `c_bite` table filled in `prepare`; post (k = 2, ±6 dB); ℓ = 1·u[n−1]) and **Tube** (pre
  (k = 1, ±6 dB) → branch `PolyphaseHalfbandOversampler` factor 2 up → `TubeStage::process(buf, 2)` → down → post (k = 2, ±6 dB);
  `setSaturationAmount(1)`, `setBias(0)` at prepare; input gain −6 + 18·A dB; output gain −20·log10 c_tube(A) clamped ±24 dB, c_tube from
  the 65-point table of the h1 coefficient of `Asymmetric::tube(g·P·sin θ)/P` over 256 phase points; ℓ = 1·u[n − 2]). Also the naive
  base-rate Bite path for `seamNaiveBaseRate_` (y = P·`Asymmetric::diode`(g·pre(x)/P)/c_bite at base rate, no path). Tables are filled
  only in `prepare`.

### T012 [P] ShapeMorph spectral gates (SC-001, SC-002, SC-004, SC-010/011 Shape, SC-012(f) Shape; plan S9.3, C-3, C-6, S6.5)

**Files:** `dsp/tests/unit/systems/shape_morph_spectral_test.cpp`. Target `dsp_systems_tests`. Depends on T008, T010. Test-only task
(no header edit).

- `ShapeMorph_RegimesPairwiseDistinct` (SC-001): SHAPE ∈ {0, 0.5, 1}, kSawAnchor and kGrowl, E1 and C2, 48 kHz, 7 s renders,
  level-matched to the input's L: all 3 pairs `distinct` (model margin ×1.48, saw HOLLOW–BITE 4.44 dB THD).
- `ShapeMorph_SweepContinuitySmoke`: kSawAnchor E1, SHAPE 0…1 in steps of 0.1: every sample finite, |y| ≤ 6, `maxRunningRegimeCount
  ≤ 2`; INFO the level per position.
- `ShapeMorph_SweepContinuity` `[long]` (SC-002 under C-6): 101 static positions × {E1, C2, C3} × four fixtures, 48 kHz: on kSawAnchor
  between adjacent positions |ΔL| ≤ 0.5 dB, |ΔC| ≤ 0.1 oct, |ΔE_low|, |ΔE_high| ≤ 1 dB; for kSawAnchor, kTriangleAnchor and kSineAnchor
  at each note, L within ±2.5 dB of that fixture's sweep mean; kGrowl's swing written to `evidence/sc002-growl.csv` (recorded); regime
  count ≤ 2 at every sample of every render.
- `ShapeMorph_HarmonicResponseTracksKeyboard` (SC-004): kSawAnchor at C1 and C3, SHAPE ∈ {0, 0.25, 0.5, 0.75, 1}, 48 kHz:
  |H(n)| = w_n(out)/w_n(in) for n ≤ 16 where n·f₀ < 0.45·fs at both notes: |ΔH| between notes ≤ 1.5 dB per harmonic.
- `ShapeMorph_AliasingSmoke`: SHAPE 1 and SHAPE 0, E2, 44.1 kHz, Fold-100 kGrowl → A ≤ −80 dBFS (model SHAPE 1 −89.9, S10.3b).
- `ShapeMorph_AliasingE0E2` `[long]` (SC-010 Shape): SHAPE ∈ {0, 0.25, 0.5, 0.75, 1} × {E0, E1, E2} × {44100, 48000, 96000} ×
  {kSawAnchor, kGrowl} × {raw, Fold-100}: A ≤ −80 dBFS; A_in, G, A_added per cell to `evidence/sc010-shape.csv`; a Fold-100 failure
  with A_added ≤ −80 fails with "FR-070 candidate: input residual".
- `ShapeMorph_DcBound` (SC-011 Shape): 48 kHz, SHAPE ∈ {0, 0.25, 0.5, 0.75, 1} × {E0, E1, E2} × {kSawAnchor, kGrowl} × {raw, Fold-100}:
  DC ≤ −80 dBFS after 1 s.
- `ShapeMorph_RIsOneSmoke`: SHAPE 0.5, raw kSawAnchor, E1: both readings unambiguous, |centsBetween(f̂_out, f̂_in)| ≤ 1.
- `ShapeMorph_RIsOne` `[long]` (SC-012(f) Shape under C-3): SHAPE ∈ {0, 0.25, 0.5, 0.75, 1} × all four fixtures × {raw, Fold-100} ×
  {E0, E1, E2, C3}, 48 kHz: f̂_in and f̂_out both unambiguous and r_ref = f̂_out / f̂_in within 1 cent in **every** cell; per-cell readings to
  `evidence/sc012f-shape.csv`. On any failing cell the message starts "S6.5 CONTINGENCY" (the plan returns to the user before any
  ShapeMorph observer is written).
- `ShapeMorph_LoudnessTableHarvest` `[.calibration]` (C-6; run by T024 at close): with `seamLoudnessBypassed_`, render 101 positions ×
  {E1, C2, C3} × {kSawAnchor, kTriangleAnchor, kSineAnchor}; table(s) = −mean over the nine (fixture, note) of (L_out(s) − L_in); print one
  line `HARVEST kLoudnessTableDb {v0, …, v100}` (`%.4f`) and write it to `evidence/harvest-loudness.txt`.

### T013 [P] ShapeMorph automation and engage click gates (SC-003; FR-016, FR-019; D-11, C-22)

**Files:** `dsp/tests/unit/systems/shape_morph_test.cpp`. Target `dsp_systems_tests`. Depends on T006, T010. Test-only task.

- `ShapeMorph_ClickCountPositiveControl`: kSawAnchor E2, 48 kHz: (i) a 0 → 1 SHAPE step with `seamUnsmoothed_`, (ii) a hard regime
  switch with `seamHardRegime_` → `differentialClicks(...).counted ≥ 1` for each (reference: SHAPE held at the start value, phase-aligned).
- `ShapeMorph_AutomationClickFreeSmoke`: E2, kSawAnchor, block 64, SHAPE 0 → 1 as a 50 ms ramp: counted == 0 and (b) holds.
- `ShapeMorph_AutomationClickFree` `[long]` (SC-003): moves 0 → 1 and 1 → 0, each as a 50 ms ramp (setShape every block, linearly
  interpolated) and as a single step at a block boundary, at block sizes {64, 512}; plus engage and disengage at SHAPE ∈ {0, 0.5, 1};
  at E0 and E2, kSawAnchor and kGrowl, 48 kHz. (a) differential count == 0 in every case; (b) for SHAPE moves, every |y[k] − y[k−1]|
  during the transition ≤ 1.1 × the maximum `steadyDeltaMax` over the 101-point SHAPE grid points inside the traversed range (steady
  renders in the same test); (c) engage / disengage: `d11Ratio ≤ 1` with g_a = 1 − e, g_b = e (e read sample by sample through the probe),
  D_a = steady delta of the bypass (the input), D_b = steady delta of the regime at that SHAPE; `regimeStateWarm == true` just before each
  engage; `maxRunningRegimeCount ≤ 2` throughout.

---

## Group 6 — Character Fold + Crush (sequential; shared header)

### T014 Character **Fold** and **Crush** (FR-021, FR-032, FR-051, FR-055; plan S5.4 Fold / Crush, C-12, D-6)

**Files:** `dsp/include/krate/dsp/systems/bass_character_stage.h`, `dsp/tests/unit/systems/bass_character_spectral_test.cpp`,
`dsp/tests/unit/systems/bass_character_stage_test.cpp`. Target `dsp_systems_tests`. Depends on T011.

- **Tests first:**
  - `BassCharacter_FoldSmoke` (spectral TU): (i) the probe's `branchLinearCoefficient` for Fold at Amount ∈ {0, 0.5, 1} equals
    c_fold(A) = [h1 coefficient of P·sin(θ·sin φ)/sin(min(θ, π/2))] / P with θ = 0.1 + 2.4·A, computed in the test by a 4096-point
    midpoint sum, within 1e-4; (ii) A ≤ −80 dBFS on Fold-100 kGrowl at E2 / 44.1 kHz, Amount 1, Tone ∈ {0, 1} (model −85.7); (iii) DC
    ≤ −80 dBFS; (iv) Amount audible on kSawAnchor E1 (model 15.9 dB).
  - `BassCharacter_CrushSmoke` (spectral TU): (i) A ≤ −80 dBFS on Fold-100 kGrowl at E2 / 44.1 kHz, Amount 1, Tone ∈ {0, 1} (model
    −100.5); (ii) DC ≤ −80; (iii) Amount audible on kSawAnchor E1; (iv) two renders from `reset()` bit-identical (dither 0, no RNG used).
  - `BassCharacter_CrushHoldRing` (stage TU; plan S5.4 unit case): 44.1 kHz with f₀ = 20000 Hz, Amount 1 for 0.2 s →
    `crushMaxOccupancy ≤ 2`, `crushRetirements == 0`; then E1 with an Amount ramp 0 → 1 over 0.5 s (m_eff crosses ≥ 3 integers):
    occupancy ≤ 2 at every F-sample, retirements == 0, differential click count across the ramp == 0 (reference: Amount held at 0.05
    so the hold runs; phase-aligned).
- **Implement** plan S5.4 **Fold** (`WavefolderProcessor` Serge, symmetry 0, mix 1 at prepare; `setFoldAmount(0.1 + 2.4·A)`; per F-sample
  buf = u/P → `process(buf, 1)` → ·P/sin(min(θ, π/2)) → post (k = 2, ±12 dB); ℓ = c_fold(A)·u from the 65-point table) and **Crush**
  (m(A) = 64·(1/16)^A; m_eff = min(m(A), F·fs/(4·f₀)); two `HoldEngine`s keyed by the parity of m with 3-slot step rings, the crossing
  rule with the current m on both sides, the causal integrated cubic B-spline S(v) of the plan, ring-full policy retire-oldest counted
  into `crushRetirements_`; bits B(A) = 12 − 8·A on two `BitCrusher`s with `setDither(0)` and `setSeed(0x5F3759DFu ^ channel)`;
  q = P/0.9·crusher(0.9·value/P); y = (1 − frac)·Hold(m_lo) + frac·Hold(m_hi) → post (k = 2, ±12 dB); ℓ = 0; the phasor follows f₀ by
  rate). Track `crushMaxOccupancy_`.

---

## Group 7 — Character Fracture (sequential; shared header)

### T015 Character **Fracture** (FR-021, FR-033, FR-050; plan S5.4 Fracture, S5.7, C-5, C-9)

**Files:** `dsp/include/krate/dsp/systems/bass_character_stage.h`, `dsp/tests/unit/systems/bass_character_spectral_test.cpp`. Target
`dsp_systems_tests`. Depends on T014.

- **Tests first** (spectral TU):
  - `BassCharacter_FractureSmoke`: (i) on Fold-100 kSawAnchor E1, Amount 1, Tone 0.5: the power at odd multiples of f₀/2 (BH7 bins within
    ±8 of (2k+1)·f₀/2) ≥ −40 dB re total (model 0.0 dB, SC-024(b)); (ii) A ≤ −80 dBFS on the **n·f₀/2 lattice**
    (`aliasedPower(x, aliasFftLength(fs, f0/2), fs, f0/2)`, C-9) on Fold-100 kGrowl at E2 / 44.1 kHz, Amount 1, Tone ∈ {0, 1} (model
    −99.3); INFO the n·f₀-mask value; (iii) DC ≤ −80 dBFS; (iv) Amount audible on kSawAnchor E1 (model 60 dB, I).
  - `BassCharacter_FracturePhasor` (FR-050 Edge Cases): via the probe's `runBranch`, φ starts at 0 after `reset()`; a
    `setFundamentalHz` step from E1 to C3 mid-stream changes the per-sample increment but the phasor value is continuous
    (|φ[n] − φ[n−1]| ≤ C3/(F·fs) + 1e-6); two renders from `reset()` are bit-identical.
  - `BassCharacter_FractureObserver` (C-5): Amount up in 0.005 steps at Tone 0.5 → r goes 1 → 0.5 exactly once, at the first Amount ≥
    `kFractureFlipEdges[2].hi`; down → 1 at the first ≤ `.lo`; r == 1 at Amount 0; `reset()` → 1.
- **Implement** plan S5.4 **Fracture** (C-5 combined law): φ ∈ [0, 2) in periods of f₀, φ += f₀/(F·fs); σ = tanh(4·sin πφ)/tanh 4;
  s = sin πφ; a = A; g = 0.5 + 1.5A; β = 0.5A; v = g·(u·((1 − a) + a·σ)/P + β·s); y = (P/g)·(`TanhADAA`(drive 1)(v) − tanh(g·β·s_mid)),
  s_mid = s at φ − Δφ/2; → post (k = 2, ±12 dB); ℓ = (1 − a)·c_frac(A)·(u + u_prev)/2 with c_frac from the 65-point table of the h1
  coefficient of (P/g)·tanh(g·sin φ) re P. f₀ moves the rate, never the phase. Observer per S5.7 (r = 0.5 while `flippedF_`).

---

## Group 8 — the remaining gates (parallel; each task owns exactly one TU; no header edits)

### T016 [P] Character spectral gates (SC-005, SC-006, SC-007, SC-010, SC-011, SC-022, SC-028; C-7, C-8, C-9)

**Files:** `dsp/tests/unit/systems/bass_character_spectral_test.cpp`. Target `dsp_systems_tests`. Depends on T015.

- `BassCharacter_AlgorithmsPairwiseDistinct` (SC-005): six algorithms at Amount 1 / Tone 0.5, kSawAnchor and kSineAnchor, E1 and C2,
  48 kHz, level-matched: all 15 pairs `distinct` (model margins ×1.87 saw Tube–Bite 5.62 dB R_oe; ×10.5 sine).
- `BassCharacter_ToneMonotonicCentroidSmoke`: Fold, kSawAnchor E1, Amount 1, Tone in 11 steps: every ΔC ≥ −0.005 and span ≥ 0.25 oct.
- `BassCharacter_ToneMonotonicCentroid` `[long]` (SC-006 under C-7): every algorithm × Tone in 11 steps × Amount ∈ {0.5, 1} × {kSawAnchor,
  kSineAnchor} × {E1, C2}. Per cell ε = max |ΔC| between renders of the identical setting started at 8 sub-period start offsets and at
  block sizes {1, 64, 512}; τ = min(2ε, 0.005); a cell with ε > 0.0025 FAILS with "harness, not bar" (spec). Every step ΔC ≥ −τ and the
  sum of negative steps ≥ −τ on both fixtures; span C(1) − C(0) ≥ 0.25 oct on kSawAnchor; kSineAnchor spans, ε and τ to
  `evidence/sc006.csv`.
- `BassCharacter_AmountAudible` (SC-007 endpoints): every algorithm, Amount 0 vs 1 at Tone 0.5, E1, kSawAnchor and kSineAnchor:
  `amountMoved ≥ 3 dB` or ΔC ≥ 0.25 oct. `BassCharacter_AmountTrajectory` `[long]`: the 11-step trajectories to `evidence/sc007.csv`.
- `BassCharacter_AliasingE0E2` `[long]` (SC-010 Character): every algorithm × Amount 1 × Tone ∈ {0, 1} × {E0, E1, E2} × {44100, 48000,
  96000} × {kSawAnchor, kGrowl} × {raw, Fold-100}: A ≤ −80 dBFS (Fracture on the n·f₀/2 lattice, C-9, with the n·f₀ value recorded);
  A_in, G, A_added per cell to `evidence/sc010-character.csv`; FR-070 candidates fail loudly as in T007.
- `ShapeCharacter_AliasGatePositiveControl` (SC-010 control): `seamNaiveBaseRate_` Bite, raw kSawAnchor, E2 / 44.1 kHz, Amount 1,
  Tone 0.5 → A **> −80 dBFS** (model −48.0); the same cell with the seam off ≤ −80.
- `BassCharacter_DcBound` (SC-011): 48 kHz, the SC-010 Character grid at 48 kHz only: DC ≤ −80 dBFS after 1 s.
- `ShapeCharacter_OutputBounded` (SC-022): inputs with peak 1 — Fold-100 kGrowl normalised to peak 1, full-scale sines at E0 and C3:
  every algorithm × Amount ∈ {0, 0.5, 1} × Tone ∈ {0, 0.5, 1}: |y| ≤ 6.0; INFO `preCeilingPeak()`. (The SHAPE arm is in T019.)
- `BassCharacter_ToneStructure` (SC-028, C-8): (a) for each tilt-Tone algorithm, a copy of `postTilt` (and `preTilt` for Tube/Bite) at
  Tone 0.5 is flat within 0.1 dB over 20 Hz–0.45·(F·fs) (sine probes at 30 log-spaced frequencies); (b) through `processBlock`: f₀ set to
  C1 then C3 by `setFundamentalHz`, and in a second run by a constant `f0PerSample` array that differs from the prepare default; render,
  then read the running branch's tilts through the probe; their response vs n (n ≤ 16, n·f₀ < 0.45·fs) differs between C1 and C3 by
  ≤ 1.5 dB per harmonic at Tone ∈ {0, 0.25, 0.75, 1}; one case switches algorithm after `setFundamentalHz`, one leaves bypass after it,
  one calls `reset()` after it; (c) Fold at Amount 1: output w_n at Tone 0 and at Tone 1 each divided by |H_post(n·f₀)|² (measured on the
  probe's tilt copy) agree for n = 1…16 within 0.1 dB; (d) Tube and Bite at Amount 1: de-tilted (÷ |H_post|²) THD or R_oe differ ≥ 3 dB
  (floored) between Tone 0 and Tone 1; corroborated with `seamPostToneNeutral_` (≥ 3 dB); **negative control** `seamPreToneNeutral_`
  with the post tilt active → de-tilted difference < 3 dB.

### T017 [P] Character behaviour gates (SC-008, SC-009, SC-013, SC-021, SC-026(b)(c)(d), SC-027, SC-029; C-14, C-16, C-17, C-21)

**Files:** `dsp/tests/unit/systems/bass_character_stage_test.cpp`. Target `dsp_systems_tests`. Depends on T015.

- `BassCharacter_AmountZeroBitIdentical` (SC-008): every algorithm, kSawAnchor E1: Amount 0.8 for 0.3 s then `setAmount(0)`; from
  100 ms after the call (max(100 ms, 5 × 10 ms), C-16; INFO the settle used) both channels bit-identical to the input.
- `BassCharacter_ClickCountPositiveControl` (SC-009 / SC-021 controls): Tube → Fracture with `seamHardSwitch_` and an Amount 0 → 1 step
  with `seamUnsmoothed_` → `counted ≥ 1` each.
- `BassCharacter_SwitchClickFreeSmoke`: Tube → Bite and Rectify → Fracture at E1 (the SC-009 assertions on two pairs).
- `BassCharacter_SwitchClickFree` `[long]` (SC-009): all 30 ordered pairs × {E1, C2}, kSawAnchor, Amount 1, Tone 0.5, 48 kHz, the switch
  requested mid host block (process 200 samples, `setAlgorithm`, process 312): `d11Ratio ≤ 1` with g_out = cos(πt/2)^α, g_in =
  sin(πt/2)^α (t from `crossfadeIncrement(10, fs)`, α = 1.493), D_a, D_b and max|x_a|, max|x_b| from steady renders of each algorithm
  alone; differential count == 0; the one-period sliding RMS over [switch − T, switch + crossfade + T] within [min − 1, max + 1] dB of the
  two steady one-period levels.
- `BassCharacter_MonoSum` (SC-013): (a) L == R input, zero offsets, every algorithm × Amount ∈ {0.5, 1} → L == R bit-identical;
  (b) `setChannelOffsets(0.15f, 0.15f)`, every algorithm, Amount 0.5 / Tone 0.5, E0, E1, E2, kSawAnchor: energy below 2·f₀ (BH7 bins
  9 … (2·f₀ − 8 bins) of `ProfundumTest::bh7PowerSpectrum` on the `aliasFftLength` frame) of (L + R)/2 within 1 dB of the mean of the L and
  R energies there; (c) for every algorithm |THD(L) − THD(R)| ≥ 1 dB (floored) or |C(L) − C(R)| ≥ 0.05 oct. Values to `evidence/sc013.csv`.
- `ShapeCharacter_ParameterStepsClickFreeSmoke`: Bite, kSawAnchor E1: Amount 0.2 → 1 step and Amount 0 ↔ 0.1 bypass edge both ways:
  counted == 0; hard-bypass control (`seamHardBypass_`) counted ≥ 1.
- `ShapeCharacter_ParameterStepsClickFree` `[long]` (SC-021): every algorithm × {Amount, Tone, amount offset, tone offset} × {full-range
  step, 20 ms ramp} × {E1, C2} × {kSawAnchor, kGrowl}; plus the bypass edge (Amount 0 ↔ 0.05, both directions, step and 20 ms ramp)
  for every algorithm; differential count == 0 everywhere; both positive controls and the hard-bypass control flagged.
- `BassCharacter_RLawHysteresis` (SC-026 (b)(c)(d), D-8, C-18): Rectify, descriptor `true`, Tone ∈ {0, 0.5, 1}: (b) Amount dithered by
  ±(band/4) around each edge of the table band for 1000 steps (band = hi − lo of `kRectifyFlipEdges` at that Tone) → r never toggles;
  (c) after `reset()` at an Amount inside the band r == 1; (d) one float, unchanged by `setChannelOffsets`; crossfade: Rectify → Fracture
  with Amount past both flips → `perceivedPitchRatio()` == Fracture's r from the `setAlgorithm` call on; a queued request leaves r
  unchanged until it starts.
- `BassCharacter_DryShareNoComb` (SC-027, C-14): `seamIdentityLaw_`, Rectify, Amount 0.5, Tone 0.5, seeded white noise at −12 dBFS RMS,
  48 kHz: output vs a standalone `OversampledResidualPath` fed lin = u[n−1] (the ADAA2 alignment) and res = 0 → residual ≤ −100 dBFS;
  magnitude response (Welch, 2^14, Hann, output/input cross-spectrum) flat within 0.1 dB over 20 Hz–0.45·fs; control `seamRawDry_` →
  ripple > 0.1 dB.
- `BassCharacter_AlgorithmQueue` (SC-029, C-21): ≥ 30 sampled ordered triples (A, B, C) plus D from all six algorithms (deterministic
  xorshift sampling, printed): during A → B, request C then D at distinct samples → output bit-identical to the reference schedule
  "A → B, then B → D at the crossfade end"; `maxRunningBranchCount ≤ 2` at every sample; a request equal to the then-current target
  starts no crossfade; the orderings "C then B" (ends at B, no further crossfade) and "C then A" (ends with B → A at the crossfade end)
  each bit-identical to their reference schedules; INFO the API review line "no setAlgorithmBlend" (a `requires` expression asserts
  `!requires(BassCharacterStage s) { s.setAlgorithmBlend(0.5f); }`).

### T018 [P] r-law gates (SC-012 (a)(b)(c)(d)(f)(g), SC-024, SC-026(a); harvest case; C-2–C-5; deviation 4)

**Files:** `dsp/tests/unit/systems/bass_character_rlaw_test.cpp`. Target `dsp_systems_tests`. Depends on T015. Every case at 48 kHz,
render = 1 s settle + 262144 samples; SHS via `shsCandidates`; input readings cached per (fixture, note, raw/Fold-100). All `[long]`
cases write their cells (Amount, Tone, fixture, note, derived b and wet for Rectify, f̂_in, f̂_out, r_obs, ambiguous, gated) to
`evidence/sc012-<clause>.csv`.

- `BassCharacter_RLawSmoke`: Rectify, descriptor `true`, kSineAnchor E1, Tone 0.5: Amount 0 → r_obs = 1 and f̂_out within 1 cent of f₀;
  Amount 1 → r_obs = 2 and f̂_out within 1 cent of 2·f₀ (both unambiguous).
- `BassCharacter_RLawMatchesReference_Sine`, `_Triangle`, `_Saw`, `_Growl` `[long]` (SC-012(a), one shard per fixture): Rectify,
  Amount 0, 0.1, …, 1 × Tone ∈ {0, 0.5, 1} × {E0, E1, E2, C2, C3}, run twice (descriptor as the voice would supply it — `true` on
  sine/triangle, `false` on saw/growl — and the default `false`). Gated cells (C-3: input readsF0): unambiguous → r_obs·f₀ within
  1 cent of f̂_out; ambiguous → r_obs·f₀ within 1 cent of one of the two best candidates; per (fixture, note) ambiguous fraction ≤ 20 %
  and ≥ 8 unambiguous cells. Default run: saw / growl gated cells as above; sine / triangle deviation recorded. kGrowl cells recorded,
  not gated (C-3).
- `BassCharacter_RectifyFlipPoint` `[long]` (SC-012(b), C-4): for {kSineAnchor, kTriangleAnchor} × {E0, E1, E2, C2, C3} × Tone ∈ {0, 0.5,
  1}: bisect Amount on the output's S(2f₀)/S(f₀) crossing of 0 dB to 0.01, and of −1 dB (A_lo) and +1 dB (A_hi); Amount 0 and Amount 1
  are unambiguous; the observer's up edge and down edge (`kRectifyFlipEdges` at that Tone) both lie in [A_lo, A_hi] of every cell. Widths
  recorded. (g) in the same case: with `setChannelOffsets(0.15f, 0.15f)` the L and R flip Amounts (bisected per channel) recorded against
  the centre flip — not gated.
- `BassCharacter_RLawHysteresisBandWidth` `[long]` (SC-026(a) under C-4): fixtures {sine, triangle} × {E1, C3}, Tone ∈ {0, 0.5, 1}:
  sweep Amount 0 → 1 → 0 in 0.005 steps: r steps exactly once per direction; up-flip ≥ down-flip; (up − down) equals
  (min A_hi − max A_lo) over the four cells within 0.01.
- `Rectifier_RLawEvidence` `[long]` (SC-012(c), recorded): `Rectifier` alone at b ∈ {0, 0.25, 0.5, 0.75, 1} × four fixtures × {E0, E1,
  E2, C2, C3}: the reference r per cell to `evidence/sc012c.csv`; asserts only finiteness.
- `BassCharacter_CrushFractureRLaw` `[long]` (SC-012(d), C-3, C-5): Crush and Fracture, Amount ∈ {0, 0.25, 0.5, 0.75, 1} × Tone ∈ {0, 0.5,
  1} × four fixtures × {E0, E1, E2, C3}. Crush: every gated (input readsF0) unambiguous cell r_obs·f₀ within 1 cent of f̂_out. Fracture:
  gated on **Fold-100** kSawAnchor, kSineAnchor, kTriangleAnchor (C-5(c)); raw-fixture cells recorded with their deviation; ambiguity
  caps as in (a).
- `BassCharacter_FractureRLawAndSidebands` `[long]` (SC-024 under C-5): Fracture, Amount in 11 steps × Tone ∈ {0, 0.5, 1} × four fixtures
  × {E0, E1, E2, C3}: (a) `perceivedPitchRatio()` == 1 exactly at Amount 0, values in {1, 0.5} ⊂ [0.25, 4], non-increasing in Amount,
  0.5 at Amount 1; measured move at Amount 1 ≥ 100 cents on the gated fixtures (expected 1200); (b) odd-f₀/2 power ≤ −60 dB re total at
  Amount 0 and ≥ −40 dB re total at Amount 1.
- `BassCharacter_RIsOneSmoke`: Tube, Amount 1, Tone 0.5, raw kSawAnchor E1: r_ref within 1 cent, both unambiguous.
- `BassCharacter_RIsOne` `[long]` (SC-012(f) under C-3): Tube, Fold, Bite × Amount 1 × Tone ∈ {0, 0.5, 1} × four fixtures × {raw,
  Fold-100} × {E0, E1, E2, C3}: zero ambiguous cells; r_ref = f̂_out / f̂_in within 1 cent in every cell. A failing cell's message starts
  "S6.5 CONTINGENCY" (Tube/Fold/Bite route).
- `BassCharacter_RLawHarvest` `[.calibration]` (plan T030; run by T024): Rectify bands for {sine, triangle} × {E0, E1, E2, C2, C3} and
  Fracture bands for Fold-100 {saw, sine, triangle} × {E0, E1, E2, C3}, at Tone ∈ {0, 0.25, 0.5, 0.75, 1} (Fracture crossing on
  S(f₀/2)/S(f₀)); per Tone the intersection [max A_lo, min A_hi]. Prints `HARVEST kRectifyFlipEdges {{lo, hi}, …}` and
  `HARVEST kFractureFlipEdges {…}` (`%.4f`) and writes `evidence/harvest-rlaw.txt`. **Fails with "STOP AND SURFACE (R-6)"** if any
  intersection is empty or if, per Tone, the A_lo or A_hi spread across notes exceeds 0.01 (the tables have no note axis, C-4).

### T019 [P] Chain gates (SC-012(e), SC-016, SC-018, SC-019, SC-020, SC-022 Shape arm, SC-025; C-15)

**Files:** `dsp/tests/unit/systems/shape_character_chain_test.cpp`. Target `dsp_systems_tests`. Depends on T015, T010.

- `BassCharacter_RLawUpstreamChainSmoke` / `BassCharacter_RLawUpstreamChain` `[long]` (SC-012(e)): `ProfundumCore` (prepare 48 kHz,
  `setControls` kSawAnchor / kGrowl / kTriangleAnchor, `noteOn(f0)`) → `ShapeMorph` SHAPE 1 → `HarmonicFold` 100 → `BassCharacterStage`
  Rectify, descriptor `false`; Amount in 11 steps × Tone ∈ {0, 0.5, 1} × {E0, E1, E2, C3} (smoke: kSawAnchor E1, Amount {0, 1}, Tone 0.5).
  Assert w₂ at the Rectify input within 40 dB of w₁ (the premise); gated cells (input readsF0) match within 1 cent; caps as in (a).
- `ShapeCharacter_NoAllocation` (SC-016): `AllocationScope` count == 0 over: `processBlock` with and without `f0PerSample`, every setter
  of both stages, `reset()`, an algorithm switch, a ShapeMorph engage, leaving the Character bypass — for every algorithm and SHAPE ∈
  {0, 0.5, 1}.
- `ShapeCharacter_PartitionInvariant` (SC-018): both stages, block sizes {1, 17, 32, 64, 512}, with and without a Drop-shaped
  `f0PerSample` array and with setter calls at fixed absolute sample indices: bit-identical (else the residual is printed and must be
  ≤ −120 dBFS); two renders from `reset()` bit-identical; prepare 48k → 96k → 48k equals a fresh 48k render.
- `ShapeCharacter_SampleRates` (SC-019): 44100, 48000, 88200, 96000, 192000 × every algorithm (Amount 1) and SHAPE ∈ {0, 0.5, 1}: finite
  and ≤ 6; at E1, C within 0.1 oct and L within 0.5 dB between 48 and 96 kHz.
- `ShapeCharacter_OutputBoundedShape` (SC-022 Shape arm): the SC-022 inputs through ShapeMorph at SHAPE ∈ {0, 0.25, 0.5, 0.75, 1}: ≤ 6.
- `ShapeCharacter_H1TransferRecord` (SC-020, FR-040, FR-041, C-15): `static_assert` `latencySamples() == 0` for all three components;
  the h1 transfer (dB and degrees, `goertzelPhasor` over `integerPeriodLength`) and the h1 group delay at E0, E1, E2 on kSawAnchor and
  kGrowl for every algorithm at Amount 1 × Tone ∈ {0, 0.5, 1} and every Shape anchor, to `evidence/sc020-h1-transfer.csv`; cells with the
  output h1 > 60 dB below the input's written as "h1 absent". Recorded, not gated.
- `ShapeCharacter_DropTracksHarmonicNumbersSmoke` / `ShapeCharacter_DropTracksHarmonicNumbers` `[long]` (SC-025): a `DropEnvelope`
  (prepare 48 kHz, `setAmount(48)`, `setTime(60)`, `setShape(0)`, `setRebound(0)`, `trigger(true)`) gives d[n]; f[n] = E2·2^{d[n]/12};
  the core runs with `f0PerSample = f`; the stages receive the core's staircase (f held over each 16-sample grid step from note on); host
  blocks {64, 512}; kSawAnchor and kGrowl; ShapeMorph at SHAPE ∈ {0, 0.5, 1} and every algorithm at Amount 1, Tone 0.5 (plus Tone ∈
  {0, 1} for every algorithm). (a) at 5 instants along the Drop (|H(n)| on a short BH7 frame of the instant's local period × 8 against a
  steady render at that f₀), n ≤ 16 where n·f₀ < 0.45·fs: within 1.5 dB per harmonic; (b) positive control: per-block `setFundamentalHz`
  at block 512 deviates > 1.5 dB for ShapeMorph or Crush (deviation recorded); (c) Crush I ≤ steady I + 3 dB; Fracture odd-f₀/2 sidebands
  sit on the instantaneous f₀/2 lattice; (d) differential count == 0 through the Drop with both controls flagged; (e) null pointer ==
  constant array, bit-identical. Smoke: SHAPE 0.5 + Crush, block 64, kSawAnchor, clauses (a) at 2 instants and (e).

### T020 [P] Non-finite robustness (SC-017; FR-052, FR-066)

**Files:** `dsp/tests/unit/systems/shape_character_nonfinite_test.cpp` (`-fno-fast-math`). Target `dsp_systems_tests`. Depends on T015,
T010. Tags add `[nonfinite]`; NaN/±Inf from bit patterns as in T007.

- `ShapeCharacter_NonFiniteRecovery`: a 16-sample NaN / +Inf / −Inf burst at 1.0 s, kSawAnchor E1, for every algorithm (Amount 1) and
  SHAPE ∈ {0, 0.5, 1}: every output finite; `stateFinite()` true within 512 samples after the burst; on the frame starting 100 ms after
  the burst end, L, E_low, E_high within 0.1 dB and C within 0.01 oct of the same frame of a burst-free render.
- `ShapeCharacter_NonFiniteSetters`: NaN, +Inf, −Inf passed to `setShape`, `ShapeMorph::setFundamentalHz`, `setAmount`, `setTone`,
  `setChannelOffsets` (each argument), `BassCharacterStage::setFundamentalHz` mid-render → output bit-identical to the render without
  those calls.
- `ShapeCharacter_NonFiniteF0Feed`: an `f0PerSample` array with NaN at index 3000 and +Inf at 7000 → output bit-identical to a render
  whose array holds the previous f₀ at those entries (both stages).

### T021 [P] CPU budget TU and lane script (SC-015; C-20; deviation 7) — **lane deferred to the main loop**

**Files:** `dsp/tests/unit/systems/shape_character_perf_test.cpp`, `specs/profundum-phase5-shape-character/evidence/close/cpu-lane.sh`.
Target `dsp_systems_tests`. Depends on T015, T010.

- `ShapeCharacter_CpuBudget` `[systems][profundum][shape-character][performance][.perf]`: 48 kHz, host block 64, **mono-linked**
  (SC-015 as amended by the user ruling of 2026-10-15): kSawAnchor E1 on L and R (L == R), zero channel offsets; assert through both
  probes that `linked == true` before timing. The **unlinked** cell (kSawAnchor E1 on L, the same fixture offset by 0.37 period on R,
  `setChannelOffsets(0.15f, 0.15f)`, `linked == false` asserted) is measured in the same case and **recorded** (printed, not gated)
  as the Phase 7 / Phase 9 handoff figure. Sweep all six algorithms × SHAPE ∈ {0, 0.25, 0.5, 0.75, 1} (Amount 1, Tone 0.5, no crossfade
  active): per cell 3 repetitions of 5 s of audio, steady_clock, % = wall / audio × 100, median of 3. Gate: **max over cells ≤ 2.0 %**;
  print the argmax cell, Shape alone and Character alone per cell. Record (print, not gated): the worst crossfade cell (alternating
  switches every 20 ms), a 48 st / 60 ms Drop with `f0PerSample` at the argmax cell, a 10 ms Amount + Tone + SHAPE ramp cell repeated
  every 50 ms, ShapeMorph disabled (warm, C-22), and the unlinked cell (see above). Write all figures to
  `evidence/sc015-cpu.txt`.
- `cpu-lane.sh`: refuses to start while `tasklist | grep -ciE "cl.exe|dsp_.*_tests.exe|clang-tidy"` is non-zero; then runs
  `pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe build/windows-x64-release/bin/Release/dsp_systems_tests.exe -ExeArgs
  "ShapeCharacter_CpuBudget"` to `evidence/run-log/close/cpu-lane.log`, tails it, and appends the verdict line to
  `evidence/run-log/T021.txt`. A red reading is re-run once alone after the machine has idled; only then is it a defect (never relax).
- **Executor:** write both files; return done with notes "deferred to the main loop at close: run `bash
  specs/profundum-phase5-shape-character/evidence/close/cpu-lane.sh` after everything else (T027)".

---

## Group 9 — defect resolution (sequential, conditional)

### T022 Resolve every `DEFECT:` line and gate-documented red

**Files:** any Phase 5 file named by a `DEFECT:` line (headers included); this file's "Defects found" section.

- Read `evidence/run-log/T001.txt` … `T021.txt` and the gate notes of Groups 2–8. Copy every `DEFECT:` line into "Defects found" below,
  then fix the root cause in the owning file. Rules: never weaken a bar or delete a failing test; a bar proven unreachable is STOP AND
  SURFACE with the measurement; a needed probe accessor is added to the probe header here (the only edit allowed to it after T009/T010).
- Re-tag any untagged case the gate logs show above 15 s as `[long]` and add its `…Smoke` sibling (deviation 8).
- If there are no `DEFECT:` lines and no reds, write "none" under "Defects found" and return done.

---

## Group 10 — integration (sequential; last)

### T023 CMake registration audit (single task)

- `grep -c` each of the 14 Phase 5 TU paths in `dsp/tests/CMakeLists.txt`: exactly 1 each, except
  `unit/processors/rectifier_nonfinite_test.cpp` and `unit/systems/shape_character_nonfinite_test.cpp` = 2 (list + `-fno-fast-math` block);
  `shape_character_perf_test.cpp` is in no `set_source_files_properties` block. `dsp/CMakeLists.txt` lists each of the six new headers
  exactly once, in its layer's list. `git diff 40205b86 -- dsp/CMakeLists.txt dsp/tests/CMakeLists.txt` shows only added lines. Record in
  `evidence/run-log/T023.txt`.

### T024 Close harvests and paste script (plan T030 / T040; C-4, C-5, C-6) — **deferred to the main loop**

**Files:** `specs/profundum-phase5-shape-character/evidence/close/harvest.sh`, `specs/profundum-phase5-shape-character/evidence/close/paste-harvest.js`.

- `harvest.sh`: runs `dsp_systems_tests.exe "ShapeMorph_LoudnessTableHarvest"` and `"BassCharacter_RLawHarvest"` **concurrently**, each to
  `evidence/run-log/close/harvest-*.log`; on a "STOP AND SURFACE" line, stops and prints it (R-6 / C-5(b): return to the user with
  `evidence/harvest-rlaw.txt`).
- `paste-harvest.js` (Node, CommonJS, no dependencies): reads `evidence/harvest-loudness.txt` and `evidence/harvest-rlaw.txt` and replaces
  the text between `// HARVEST-BEGIN <name>` and `// HARVEST-END <name>` in `systems/shape_morph.h` (`kLoudnessTableDb`) and
  `systems/bass_character_stage.h` (`kRectifyFlipEdges`, `kFractureFlipEdges`) with the harvested initialiser; fails (exit 1) unless every
  marker pair occurs exactly once and every value parses; prints a diff summary. `--self-test` runs it on synthetic copies in a temp
  directory and checks the exit codes of a good file, a missing marker and a malformed value.
- **Executor:** write both; run `node …/paste-harvest.js --self-test` (seconds) and record the output in `evidence/run-log/T024.txt`;
  return done with notes "deferred to the main loop at close: run `bash specs/profundum-phase5-shape-character/evidence/close/harvest.sh`,
  then `node specs/profundum-phase5-shape-character/evidence/close/paste-harvest.js`, then rebuild, BEFORE T025's lanes".

### T025 Full suites, `[long]` lane and clang-tidy (SC-023; plan S11.2) — **deferred to the main loop**

**Files:** `specs/profundum-phase5-shape-character/evidence/close/suites.sh`, `specs/profundum-phase5-shape-character/evidence/close/tidy.sh`.

- `suites.sh` (designed to run **detached**: writes `evidence/run-log/close/status.txt` with one line per lane — name, exit code, wall
  clock — so it survives the 2 h background-task ceiling): one rebuild of `dsp_core_tests dsp_processors_tests dsp_systems_tests`
  (record wall clock, `grep -ciE "warning C|warning:"` must be 0); then concurrently (a) the three per-push lanes with the filter
  `"~[long]~[performance]~[perf]~[benchmark]~[!benchmark]"`, (b) the `[long]` lane: one shard per `[long]` TEST_CASE of the three exes,
  the list generated with `<exe> --list-tests "[long]~[performance]~[perf]~[benchmark]~[!benchmark]" --verbosity quiet` (never
  hand-kept), run through `xargs -P 24`; every lane to its own log under `evidence/run-log/close/`. Then `git diff --stat 40205b86 --
  dsp/include` must list only the six new headers (SC-023), written to the status file.
- `tidy.sh`: reconfigure `build/windows-ninja` (`"$CMAKE" --preset windows-ninja`; the test CMake lists changed), then
  `"/c/Program Files/LLVM/bin/clang-tidy.exe" -p build/windows-ninja <the 14 Phase 5 TUs>` (the six headers and three test headers are
  checked through them) with a 600 s per-file ceiling, to `evidence/run-log/close/tidy.log`. Not the whole-`dsp` sweep (a 15 h hang on an
  unrelated TU is on record).
- **Executor:** write both scripts and `bash -n` them; return done with notes "deferred to the main loop at close: after T024, run
  `suites.sh` detached and `tidy.sh` concurrently; quote each lane's last lines into `evidence/run-log/T025.txt`; any red, Phase 5 or not,
  is owned (fix, re-run only the affected lane)".

### T026 Portability and Apple globals (FR-071; SC-023)

- Run `node tools/check-portability.js` and `node tools/lint-apple-globals.js` over the six new headers and 14 TUs (each a few minutes);
  both must report 0 findings; fix any finding in the Phase 5 files (then the group gate rebuilds). After `check-portability.js` (it boots
  WSL) run `wsl --shutdown`. Quote both outputs in `evidence/run-log/T026.txt`. If either run exceeds 5 minutes, stop it, write
  `evidence/close/portability.sh` and return done with notes "deferred to the main loop at close: run portability.sh".

### T027 CPU lane alone (SC-015) — **deferred to the main loop**

- Precondition: T024–T026 lanes finished, machine idle. Executor: return done with notes "deferred to the main loop at close: run
  `bash specs/profundum-phase5-shape-character/evidence/close/cpu-lane.sh` (T021) last, alone; quote the figures in
  `evidence/run-log/T027.txt`".

### Close procedure (main loop, in this order)

1. `harvest.sh` → `paste-harvest.js` → rebuild (T024). A STOP AND SURFACE line goes to the user before anything else runs.
2. `suites.sh` detached + `tidy.sh` concurrently (T025); `portability.sh` if T026 deferred it.
3. Any red → triage into "Defects found", fix, re-run only the affected lane.
4. `cpu-lane.sh` alone (T021 / T027), after the machine has idled.
5. Comply (outside this task list) quotes the evidence CSVs, the h1 table and the run-log readings; S13 handoffs go into the roadmap there.

---

## Defects found

*(T022 and sequential tasks append here: case, output, diagnosis, owning file.)*

- **DEFECT (Group 3 gate, needs a main-loop / user ruling; not a Rectifier code defect): `Rectifier_BlendStepClickFree` 1 → 0
  cells, E1 kSawAnchor (counted 7) and E1 kGrowl (counted 2), step and 20 ms ramp alike.** Diagnosed with a temporary probe
  (since removed): every counted detection lies ≥ 5000 samples after the step (53002, 55297, 55313, 55331, 63489, 65798, 65816
  for the saw; 62709, 65023 for the growl), long after the 10 ms blend smoother settles, and every one coincides (±N) with a
  detection in a steady render held at b = 0. A steady b = 0 render alone has 18 (saw) / 7 (growl) detections with no b = 1
  match over 1.5 s. They come from the ClickDetector's frame-relative 5σ test firing on the half-wave output's flat regions. So
  they belong to the end setting's steady state, not the transition. With the spec's literal reference ("the stage held at the
  transition's **start** setting", spec "Differential click count"), no correct rectifier can pass a step that goes **into** a
  regime with more steady detections. The same construction will hit SC-003 (SHAPE 1 → 0) and SC-021 (Amount steps) in later
  groups. Proposed ruling: a transition detection counts only if neither the start-setting nor the end-setting held reference
  has a detection within ±N. This is ruling B's own intent: discount steady, transition-free detections. The bar stays zero.
  Owning files: spec.md "Differential click count", tasks.md T007 / T006 (`differentialClicks` takes one reference).
  **RULED 2026-10-16 (main loop, Clarifications "Group 3 gate rulings"): adopted.** `differentialClicks(transition, refStart,
  refEnd, n, fs, f0)` added to the T006 helper (the two-argument form forwards with a null end reference); the Rectifier test
  renders the end-setting reference; the spec definition is amended. T013, T017 and every later click gate render BOTH held
  references and call the three-render form.
- **DEFECT (Group 3 gate, needs a ruling): `Rectifier_BlendStepClickFree` positive control, C2 kGrowl, counted 0 (snap at
  48237, x = −0.033).** C2 kGrowl is pulse-like: |x| > 0.08 only on its two edges per period (the minimum, −2.036 at 48431,
  sits on an edge). The b = 0 reference has detections there (48443/48449, 49150–49183), so the ruled ±N window
  (N = 91) masks a snap there by construction: the snap at 48431 is detected at 48434 and masked by 48443. Away from the
  edges, the largest negative sample is about −0.07. Snaps at −0.07 to −0.18 are not detected at all. Two re-selection rules
  were tried in the test and reverted: (a) the most negative sample ≥ 2N from any reference detection gave x = −0.039,
  counted 0; (b) ≥ N + 8 from one gave x = −0.073, counted 0, and it also lost the E1 kGrowl control that T007's edge mask
  flags. So no snap location in this cell is countable under the ruled method. The spec asks for the positive controls
  **per SC** ("each SC that uses the count"); tasks.md T007 is read per cell. Ruling needed: either the Rectifier arm's
  control is gated per arm (≥ 1 counted over the four cells, each cell recorded; 3 of 4 are flagged today), or the fixture
  set for this control changes. The test is left unchanged (it is stricter than the spec, not contradicting it).
  **RULED 2026-10-16 (main loop): per SC arm and per note.** At each note at least one fixture must flag the control; every
  cell is recorded with `WARN`. The Rectifier test now counts flagged controls per note and gates >= 1. Later click gates
  (T013, T017) follow the same per-arm-per-note rule.
- **DEFECT (Group 6 gate, needs a main-loop / user ruling; not a Crush code defect): `BassCharacter_CrushHoldRing` (b),
  E1 kSawAnchor, Amount ramp 0.05 → 1, differential clicks counted 5 (transition 215, references 77 + 171, N 145).**
  Occupancy (max 1) and retirements (0) pass. Diagnosed with a temporary probe (since removed): the five counted
  detections sit at 31894, 34815, 35680, 40647 and 41796 (m_eff ≈ 23.4, 17.0, 15.5, 9.0, 7.9; mid-ramp, none at an
  integer handoff that shows a larger delta). For each, a steady render held at the Amount the ramp is passing through
  (the block target, or that target minus 0.005 to 0.02 for the 10 ms smoother lag) has its own detection within ±N,
  and the transition's local max |Δy| (0.018, 0.020, 0.022, 0.046, 0.061) is within the held renders' (0.014–0.029,
  0.014–0.027, 0.018–0.029, 0.028–0.048, 0.032–0.065). Steady Crush renders at intermediate Amounts carry 100–700
  detections per 1.25 s against 77 at Amount 0.05 and 171 at Amount 1 (the 5σ frame-relative test fires on the
  staircase steps, whose density moves with m). So these are steady detections of the settings the ramp traverses,
  which neither the start nor the end reference can discount: the case T013 flagged as a risk for the SHAPE ramps.
  No correct pitch-synchronous hold can pass a long ramp under the Group 3 method. Proposed ruling: for a **ramp**,
  the reference set is extended to steady renders held at the settings the ramp traverses (the SC-003(b) pattern: a grid
  over the traversed range, e.g. 101 points, phase-aligned), and a transition detection counts only if none of them
  has a detection within ±N; the bar stays zero, steps keep the two-reference form. Owning files: spec.md
  "Differential click count", T006 `differentialClicks`, T014 `BassCharacter_CrushHoldRing`. Test and header left
  unchanged (the test follows the spec; it does not contradict it).
  **RULED 2026-10-16 (main loop, Clarifications "Group 6 gate ruling"): long ramps are gated by the sample-delta bound.** A
  first attempt with local grid references counted 8 (a hold's staircase positions depend on the exact Amount, so no held
  render is phase-aligned with the ramp). `BassCharacter_CrushHoldRing` (b) now gates every |Δy| over the ramp and its settle
  ≤ 1.1 × the max steady |Δy| over 21 held Amounts (SC-003(b) form) and records the two-reference count. **T017:** SC-021's
  20 ms ramps and steps keep the differential count as written; no SC cell changed.
- **Group 8 gate, 8 failing cases (fixer, 2026-10-16).** Four fixed at the root, one harness fix, three need a
  main-loop / user ruling (STOP AND SURFACE). No bar was changed and no assertion was removed.
  - **FIXED, `ShapeCharacter_NonFiniteSetters` (Fracture, first difference at sample 22127).** `setFundamentalHz(NaN)`
    kept f0 but still called `pushF0ToRunning()`, and Fracture's re-key re-seeds its rotor from φ, which moves the
    rotor's accumulated rounding. `BassCharacterStage::setFundamentalHz` is now a no-op when the sanitised value is
    bit-identical to the current f0, the same rule the f0PerSample feed already used. Owning file:
    `systems/bass_character_stage.h`.
  - **FIXED, `ShapeCharacter_SampleRates` (Crush L 48k −14.55 / 96k −15.72 dB),
    `ShapeCharacter_DropTracksHarmonicNumbersSmoke` (Crush max |dH| 11.04 / 11.48 dB at h2, h6, h10, h14) and
    `BassCharacter_DcBound` (Crush, growl Fold-100, E2, Tone 0: −71.9 dBFS).** One root cause. The hold grid sampled
    at θ = k/m. The phasor and the core both start at phase 0, so one sample point always sat on the core waveform's
    edge (sine-phase partials put the saw edge at θ = 0). A point on a band-limited edge makes the held value
    hypersensitive to sub-sample timing. In a 4-point hold model of a band-limited saw, a grid shift of 1e-4 to 1e-3
    cycles moves L by up to 5.6 dB and the Nyquist-bin harmonic (h2 of the staircase) from −240 to −11 dB. The
    rate-dependent path delay, the drop's phase trajectory and the f0 rounding each supply such shifts. Fix:
    `kCrushGridOffset = 0.5`, so the crossings are ⌊θ·m + ½⌋ − ⌊θ_prev·m + ½⌋ and the sample points sit at
    θ = (k + ½)/m, half a hold step off the edge. In the same model, shifts of 0.06 cycles then move L by 0.2 dB.
    After the fix: L 48k −12.578 / 96k −12.575 dB; drop max |dH| 1.022 / 1.023 dB (bar 1.5); that DC cell
    −109.2 dBFS. **Deviation from plan S5.4 text** (⌊θ·m⌋ → ⌊θ·m + ½⌋): no bar, constant or other rule changed.
    Owning file: `systems/bass_character_stage.h` (CrushBranch).
  - **FIXED (harness), `BassCharacter_ClickCountPositiveControl` C2 control (ii) flagged 0.** The control's event was
    placed by the proxy |a| + |b − a|. With `seamHardBypass_`, leaving bypass resets the branch and its round trip
    (S5.3 step 3), so the output steps from the bypass value a to the cold path's near-zero first sample and only
    then rises toward b. The proxy therefore picked a ≈ 0 (sample 48072, a = −0.0018, b = −0.498), where no step is
    made. Every C2 kSawAnchor candidate with |a| ≥ 0.0137 counted 1 or 2. C2 kGrowl counts 0 everywhere (pulse-like,
    the Group 3 C2-growl finding), and the per-note rule needs only one fixture. The bypass-leaving controls (ii) and
    the hard-bypass control now place the event by |a| (`t17ColdStartJumpProxy`). The bar (≥ 1 flagged fixture per
    note) is unchanged. Owning file: `tests/unit/systems/bass_character_stage_test.cpp`.
  - **STOP AND SURFACE, `BassCharacter_SwitchClickFreeSmoke` (Tube → Bite E1: L_tr max −10.567 vs steady max
    −11.740, +1.17 dB; bar +1 dB). SC-009's ±1 dB level bar is unreachable with C-16's fixed-exponent law.** A
    temporary probe (since removed) split the excess. The ideal crossfade g_a·y_A + g_b·y_B of the two held renders,
    with the stage's exact gains, already reads −10.699 (+1.04 dB). The incoming slot's cold residual DC blocker adds
    the rest: a −0.03 offset on Bite that decays with the 2 Hz corner. Sweeping α over all 30 ordered pairs on the
    ideal crossfade (kSawAnchor, Amount 1, Tone 0.5) gave the worst cells below. No α passes both sides; α = 2 still
    leaves −1.85 / −2.38 dB under. The coherent case needs a lower midpoint gain and the anti-correlated case a
    higher one, so no fixed law fits both.

    | α | E1 over | E1 under | C2 over | C2 under |
    |---|---|---|---|---|
    | 1.493 (C-16) | +1.30 dB (Crush → Fold) | −1.66 dB (Crush → Rectify) | +1.33 dB (Crush → Fold) | −2.14 dB (Rectify → Crush) |
    | 2.0 | +0.85 dB | −1.85 dB | +0.85 dB | −2.38 dB |

    Needs a ruling: the SC-009 level bar, or the C-16 law (for example a correlation-aware gain). Code and test are
    unchanged. Owning files: spec.md SC-009, plan C-16.
  - **STOP AND SURFACE, `BassCharacter_MonoSum` (b), Rectify and Fracture at E0, E1 and E2 (mid-band energy re the
    L/R mean −1.41 to −1.46 dB Rectify, −1.10 to −1.15 dB Fracture; bar 1 dB).** This is inherent to the ruled laws,
    not a stage defect. At ±0.15 offsets L runs Amount 0.65 / Tone 0.65 and R runs 0.35 / 0.35. Rectify's b (0.38 vs
    0) changes h1's phase, because abs(saw) has a cosine-phase h1. The ±12 dB post tilt (pivot 2·f₀, ±3.6 dB at these
    Tones) shifts h1's phase in opposite directions on L and R. An analytic model (40-harmonic saw, the shipped
    Rectify law, the analog KeytrackedTilt response) gives an h1 ratio of 1.33, Δphase 1.09 rad and a mono-sum of
    −1.40 dB, against −1.45 measured at E0. The same model gives Fold −0.60 dB, and Fold passes. Fracture was not
    modelled; its h1 also depends on Amount (the (1 − a) linear share and the polarity alternation), and it reads the
    same way. Needs a ruling on
    the SC-013(b) bar for Rectify / Fracture, or a Phase 7 hand-off (spec line 361 already defers the shipped offset
    values to Phase 7). Owning file: spec.md SC-013(b).
  - **STOP AND SURFACE, `BassCharacter_ToneStructure` (d), Bite (de-tilted move 2.595 dB, corroborating arm 2.595 dB;
    bar 3 dB).** Tube passes. The negative control reads 9.1e-7 dB, so the pre tilt is wired and is the only lever.
    With +18 dB of diode drive at Amount 1, the ruled ±6 dB k = 1 pre tilt moves the de-tilted THD by −8.08 → −5.58
    dB and R_oe by −2.75 → −5.34 dB. The plan model never measured this arm. Needs a ruling: Bite's pre-tilt depth
    (C-8 ±6 dB; it also moves SC-006 and the SC-010 margins) or the SC-028(d) bar for Bite. Owning files: plan C-8,
    spec.md SC-028(d).
  - **STOP AND SURFACE (introduced by the Crush grid fix), `BassCharacter_CrushSmoke` (iv) and
    `BassCharacter_AmountAudible` Crush kSawAnchor E1 (moved 1.30 dB, ΔC 0.084 oct; bar 3 dB or 0.25 oct).** With
    sample points off the edge, a 4-point hold of a saw is a saw-shaped staircase. Its THD (−2.01 → −2.67 dB), R_oe
    and I (floored) barely move. Any well-conditioned grid offset gives the same spectrum up to a time shift and DC.
    The old pass came from sampling on the edge, the same hypersensitivity that failed SC-019, SC-025 and SC-011. The
    sine cell and every other algorithm pass. Choice for the main loop / user: keep the conditioned grid and re-rule
    SC-007 for Crush on kSawAnchor (or Crush's A = 1 law, m = 4 / 4 bits), or revert `kCrushGridOffset` to 0 and take
    the three SC-019 / SC-025 / SC-011 reds back.
  - **Recorded for T025 (not this gate), [long] cases run alongside:** `BassCharacter_CrushFractureRLaw` and
    `BassCharacter_AliasingE0E2` pass with the new grid. `ShapeCharacter_DropTracksHarmonicNumbers`,
    `ShapeCharacter_ParameterStepsClickFree` and `BassCharacter_ToneMonotonicCentroid` are red both with the grid
    offset 0 and with 0.5. Examples: SHAPE 0 drop |dH| 2.0 dB at h15; Crush / Fracture SC-006 "harness, not bar"
    eps up to 1.96 oct; Crush bypass-edge counts 1 to 2. The Crush shares are similar either way (steps 22 → 26
    FAILED lines, drop 134 → 98).
- **RULED 2026-10-16 (user, Clarifications "Group 8 gate rulings") — the four Group 8 items above:** (1) SC-009 level window
  → [min − 2.5 dB, max + 1.5 dB], midpoint recorded per pair (`BassCharacter_SwitchClickFreeSmoke` and the SC-009 [long]
  case use it). (2) SC-013(b) → gated at ±0.15 for Tube / Fold / Bite / Crush; Rectify and Fracture gated at the largest
  offset on {0.05, 0.10} that meets 1 dB, the ±0.15 reading recorded, per-algorithm ceilings written to
  `evidence/sc013-offset-ceilings.txt` for Phase 7. (3) Bite pre-clip tilt ±6 → ±9 dB (`kBitePreTiltDb`; Tube and both
  post tilts stay ±6 dB), SC-010 / SC-006 / SC-007 re-measured. (4) Crush `kCrushMultipleHi` 4 → 2 (m = 64·32^−A) with the
  conditioned grid kept; `BassCharacter_CrushHoldRing` (a) premise re-derived for m = 2 at 20 kHz / 44.1 kHz; SC-007,
  SC-010, SC-012 re-measured. Applied by the main loop's implementer before the Group 8 gate re-measured (epoch 2).
- **RULED 2026-10-16 (user, Clarifications "Crush threshold flicker"):** the m = 2 DcBound red (Crush / Tone 0 / E2 / Fold-100 growl,
  −65.7 dBFS) is an 8 Hz quantiser flicker on a hold point straddling a 4-bit threshold, not DC. Fix: per-slot hysteresis of
  ¼ step in the Crush quantiser (`bass_character_stage.h`), FR-051 amended; SC-007 / SC-010 / SC-011 / SC-012 and the Crush
  CPU cell re-measured by the Group 8 gate (epoch 2). Applied by the main loop's implementer.
- **T022 (2026-10-16), DEFECT lines collected from `evidence/run-log/T001.txt` … `T021.txt`.** Two run-log lines, plus the three
  [long] reds the Group 8 gate handed to T022 / T025 (spec Clarifications "Group 8 gate rulings", last paragraph). No timed gate
  log records a per-case wall clock for an untagged case: the per-push systems lane (`F:/tmp/g8_run.log`, 13:59, all 1661 cases
  pass) has no `--durations`, and the only timed single-case runs (`F:/tmp/p5fix/long-*.log`) are already `[long]`. So no case was
  re-tagged. T022 built and ran nothing (executor rule); every fix below is unmeasured until the next gate.
  - **DEFECT (T015): `bass_character_stage_probe.h` has no Fracture phasor accessor.** FIXED. Added
    `BassCharacterStageProbe::fracturePhase(const Stage&, int ch)`, which returns `ch_[ch].frac.phase` (the φ the next F-sample uses;
    `bass_character_stage.h` FractureBranch `phase`). `BassCharacter_FracturePhasor` now also runs the literal T015 check after
    each render: φ[0] == 0 after prepare and after `reset()`, φ ∈ [0, 2), and every wrapped step is in [0, C3/(F·fs) + 1e-6].
    The law-equivalence checks stay. Owning files: `tests/unit/systems/bass_character_stage_probe.h` and
    `bass_character_spectral_test.cpp`.
  - **DEFECT (T017): SC-027 names Amount 0.5, where Rectify's dry share is 0.** wet = min(1, A / kRectHalfEnd), with
    kRectHalfEnd = 0.4, so at Amount 0.5 the raw-dry positive control cannot comb by construction. The test gates the
    identity-law residual and flatness at 0.5 and 0.2. It gates the control at 0.2 and records it at 0.5. **STOP AND SURFACE:**
    the spec row needs a ruling, "at Amount 0.5" → an Amount with a nonzero dry share (proposed: 0.2, dry share 0.5). The code
    and test are unchanged. Owning file: spec.md SC-027 row.
  - **[long] red, `BassCharacter_ToneMonotonicCentroid`, "SC-006 harness, not bar".** The cause is a harness defect, and per
    the SC-006 row ("the harness, not the bar, is fixed") T022 FIXED it. Readings from `F:/tmp/p5fix/long-tone.log`
    (epoch 1, 6 m 54 s): ε reached 0.0073 / 0.109 oct for Crush at Amount 0.5 / 1, and 0.55 / 1.96 oct for Fracture (saw E1);
    sine Fracture reached 1.48 oct. Monotonicity and span passed in every cell (neg sum 0). Diagnosis from code: the old
    `bcsCentroidCell` re-rendered from a source shifted by the sub-period offset. On `reset()`, Crush's θ and Fracture's φ
    restart at 0 (`bass_character_stage.h` CrushBranch / FractureBranch `reset()`), so the shift also moved the input against
    the phasor and changed the sound itself. ε was therefore reading the onset alignment, not window-phase noise. Fix: per block
    size, one render of the whole source from sample 0, and the 8 centroid frames end at renderLen + k·period/8 (3 renders and
    24 frames per Tone point, where there were 24 renders). τ, the bars and the cap are unchanged. **Deviation:** the spec's
    "renders … started at 8 sub-period start offsets" is now read as the parenthetical "(window phase)". The main loop
    should confirm this in the SC-006 row. Owning file: `bass_character_spectral_test.cpp`.
  - **STOP AND SURFACE (design finding exposed by the item above): Crush and Fracture timbre depends on the onset
    alignment of their phasors with the input waveform.** Both phasors are free-running from `reset()` / branch start
    (`startBranch` → `br.reset()`), not locked to the core's phase. In the epoch-1 SC-006 data, the same setting started at a
    different sub-period offset moved C by up to 1.97 oct (Fracture, A 1) and 0.11 oct (Crush, A 1). The Group 8 Crush
    grid fix (`kCrushGridOffset` 0.5, sample points at (k + ½)/m) conditions the grid only when the core waveform and θ
    both start at phase 0 together. A mid-note bypass exit or an unaligned note-on can put a sample point back on the edge,
    the hypersensitive case the Group 8 diagnosis measured (L up to 5.6 dB). Unmeasured on the current code (m = 2,
    hysteresis). Decision needed (user): accept onset-dependent timbre (and record it), or phase-lock Crush θ / Fracture φ
    to the core (e.g. seed from the core's phase on note-on / branch start: a new FR and an input-API change).
  - **STOP AND SURFACE, [long] red `ShapeCharacter_ParameterStepsClickFree` (SC-021), Crush cells only.** Epoch-1 counts are in
    `F:/tmp/p5fix/long-steps.log` (26 FAILED lines; 22 at grid offset 0). Every Crush failure involves the bypass edge
    (Amount 0): E1 saw bypass edge 0.05 → 0 ramp L/R 2; E1 growl Amount 1 → 0 step 1, 0 → 1 ramp 2, bypass edge 0 → 0.05 step 7.
    Hypothesis, from code only: entering Crush from bypass restarts θ at the event (`startBranch` → `CrushBranch::reset()`,
    `phase = 0.0`). The held end-setting reference started θ at sample 0, so the two staircases sit at different grid
    positions and the reference cannot discount the transition's steady staircase detections. This is the Group 6
    phase-alignment problem again, not a click. Not fixed: it needs a run (the epoch-1 reading predates m = 2 and the
    hysteresis), and any fix is either a measurement-scope ruling (e.g. a reference that enters the end setting at the event
    from bypass) or the phase-lock decision above. Owning files: `bass_character_stage_test.cpp` (t17CheckParamCell
    references) or `bass_character_stage.h` (Crush phasor).
  - **STOP AND SURFACE, [long] red `ShapeCharacter_DropTracksHarmonicNumbers` (SC-025).** `F:/tmp/p5fix/long-drop.log` (epoch 1)
    has 456 failed assertions: 370 × (a) |dH| > 1.5 dB, 54 × (c) Crush I > steady + 3 dB, 30 × (d) counted > 0, and the (d)
    ShapeMorph SHAPE-step and Character Amount-step controls flagged 0. (a) fails on nearly every cell, including SHAPE 0
    (ROUND), which has no phasor: kSawAnchor 2.00 dB at h15, and kGrowl 16.8 dB at h8 (h1 transfer +4.3 vs −6.4 dB). The
    instant d ≤ 6 st has fc8 = 82.41 Hz = E2, so most of its 8-cycle frame lies after the 60 ms Drop. Hypothesis (unverified):
    the Drop render's core output reaches the stage with different relative partial phases than the steady render. A
    memoryless or keytracked nonlinearity's per-harmonic out/in ratio then differs, and the input-referred |H(n)| is not
    phase-invariant for a nonlinear stage. A comparison bar the input cannot satisfy is a measurement-definition question,
    not a stage fix. Needs a diagnostic run on the current code (input-phase comparison of the two core renders at the
    frame) before any ruling. Not fixed. Owning files: `shape_character_chain_test.cpp` (sccCompareH / steady reference),
    spec.md SC-025(a).
- **RULED 2026-10-16 (Group 9, Clarifications "Group 9 rulings"):** (a) SC-027 row → Amount 0.2 (dry share 0.5), 0.5 recorded
  (main loop). (b) + (c) FR-072 / SC-030 (user): `BassCharacterStage::syncPhase(float coreCycles)` locks Crush θ and
  Fracture φ to the core's h1 phase; branch starts keep a supplied sync; `reset()` clears it; new case
  `BassCharacter_OnsetInvariance` with positive control; SC-021's Crush bypass-edge cells give the transition render the
  same sync as its references; Phase 9 handoff: call `syncPhase` at note-on, retrigger and bypass exit. (d) SC-025 [long]:
  diagnostic first (one failing SHAPE 0 ROUND growl cell), ruling after. Applied by the main loop's implementer; the
  T022 fixes (Fracture phasor probe, SC-006 harness) stand.
- **RULED 2026-10-16 (Group 9, round 2; Clarifications "Group 9 rulings, round 2"):** Crush `kCrushMultipleHi` → 2.5 (user);
  differential click count gains the 1.1× amplitude guard and Crush's SC-021 cells gate the sample-delta bound over the
  traversed grid with the count recorded (user); SC-025(a) gates the keytracked coefficients through the probes (0.1 %) and
  the HOLLOW audio bar, nonlinear |H(n)| recorded (user); SC-030 bounds fixed and Fracture on the f₀/2 lattice (main loop).
  Applied by the main loop's implementer; T022 then continues.
- **RULED 2026-10-16 (Group 9, round 3, main loop; Clarifications "Group 9 rulings, round 3"):** SC-025(c) Crush → probe gate on the
  phase increment and hold multiple (I recorded); SC-025(d) Crush → delta bound over 21 held f₀ values (count recorded);
  the (d) positive controls are re-sited where the forced step exceeds the local steady steps; SC-030 syncs to the exact
  phase of the rounded onset shift. Applied by the main loop's implementer.
- **RULED 2026-10-16 (Group 9, round 5, main loop):** SC-030 C bound 0.02 oct for Crush (hysteresis path dependence by one step,
  recorded per slot); Fracture stays 0.005 oct. SC-025(d) Crush growl under diagnosis (post-fall waveform vs steady E2 render).
- **RULED 2026-10-16 (Group 9, round 6, main loop):** SC-025(d) Crush bound also references the Drop render's own settled pre-fall
  and post-fall staircases; core post-Drop phase finding recorded for Phase 9 (plan S13). SC-030 green (7049 assertions).
- **STOP AND SURFACE (Group 9 gate, needs a user ruling; not a code defect): `BassCharacter_AlgorithmsPairwiseDistinct` (SC-005),
  Tube vs Crush on `kSawAnchor` E1 (maxDb 1.24 Elow, dC 0.065 oct) and C2 (1.09 Elow, 0.052 oct); bar 3 dB or 0.25 oct.**
  The other 58 pair-cells pass. Caused by the round-2 ruling `kCrushMultipleHi` 2.5, which was ruled on SC-007 only (SC-005
  was not re-measured). Probe (temporary diag case and constant edits, since reverted; logs `F:/tmp/p5sc005/`): the same
  case **passes** at m_min = 2, 3 and 4 and fails only at 2.5. At A = 1 the 2.5 law blends the 2-point hold (a square:
  even partials null) and the 3-point hold (h3, h6 null) half and half, so neither null survives and the post-tilted
  staircase of a saw has a smooth 1/n-like spectrum (dB re total, h1..h5: −0.7 −12.2 −16.0 −18.2 −25.2), the same shape
  as Tube's saturated saw (−0.8 −12.3 −14.5 −17.2 −19.2). Crush A = 1 descriptors (saw E1): m 2 → Roe +144, C 0.478; m 2.5
  → Elow −9.64, Roe −1.97, THD −7.76, C 0.302; m 3 → Elow −5.74, Roe −5.82, C 0.611; m 4 → Elow −5.34, C 0.675. Tube:
  Elow −8.40, Roe −1.78, THD −6.70, C 0.367. Options for the user: (i) m_min 3 (SC-005 passes; round 2 recorded SC-007
  saw passing by 0.3 dB), (ii) another m_min away from 2.5 with SC-007 / growl re-measured, (iii) change a different Crush
  A = 1 lever (bits, blend law) and re-measure, or (iv) an SC-005 ruling for this pair. Code and test unchanged.
  Owning files: spec.md FR-051 / SC-005, `systems/bass_character_stage.h` `kCrushMultipleHi`.
- **RULED 2026-10-16 (Group 9, round 7, user; Clarifications "Group 9 rulings, round 7"):** the SC-005 Tube vs Crush item above
  → Crush `kCrushMultipleHi` 2.5 → 3 (m = 64·(64/3)^−A), FR-051 amended, plan C-27; `BassCharacter_CrushHoldRing` (a) / (b)
  premises now derive m from the stage constants. Applied by the main loop; the Group 9 gate re-measures the tree.
- **RULED 2026-10-16 (Group 9, round 8, user; Clarifications "Group 9 rulings, round 8"):** `BassCharacter_SwitchClickFree`
  (SC-009 [long], first run) red on every switch into Rectify (+4.5 dB over the level window; cold-start residual DC 0.43
  draining over the 2 Hz blocker's ≈ 80 ms) and C2 Rectify → Crush (−0.07 dB under; Crush's empty first hold period).
  → `start(a)` and bypass exit pre-roll the incoming / running branch unmixed for one period of f₀ and prime its residual
  DC blocker to the measured mean before the crossfade (`DCBlocker::setState`, `OversampledResidualPath::primeDc`,
  `BassCharacterStage` pre-roll state; FR-026, FR-054, SC-009, SC-029 rows amended; plan S5.6, S5.3 step 3, C-28).
  Tests: new `BassCharacter_SwitchPreroll` and `OversampledResidualPath_PrimeDc`; `t17CheckSwitch`, the switch-machine,
  observer-queue and SC-029 cases measure from the crossfade's real start (`prerollLength` probe). Applied by the main
  loop; the Group 9 gate re-measures the tree.
- **NOTED 2026-10-16 (main loop, for the close lane T024):** `BassCharacter_RLawHarvest` `[.calibration]` (hidden; not in the
  gate filter) stops with R-6 STOP AND SURFACE: Fracture's harvested flip edges spread across notes by 0.03–0.07 on the saw
  (Tone 0 … 0.75) and 0.008–0.016 on the triangle against the 0.01 bar; Rectify's spread is within it. Independent of the
  round-7 / round-8 changes (seen before and after). Ruling owed when T024 runs the harvest lane.
- **RULED 2026-10-16 (close, user; Clarifications "Group 10 close rulings", SC-012(f)):** `BassCharacter_RIsOne` (72 growl
  cells red) and `ShapeMorph_RIsOne` (27 growl cells red), every non-growl cell green → C-3's input gate extends to clause
  (f) (gate `readsF0` inputs, record `kGrowl` with f̂_in / f̂_out, csv `gated` column); FR-034 and SC-012 rows amended, plan
  C-3 / C-29. No observer API. Applied by the main loop; both cases re-run.
- **RULED 2026-10-16 (close, user; Clarifications "Group 10 close rulings", R-6 Fracture edges):** `kFractureFlipEdges` =
  Fold-100 sine ∩ triangle per Tone over the notes; saw recorded (harvest rows kept; SC-012(d) and SC-024(a) gate sine and
  triangle only); harvest across-note spread recorded for Fracture, still a stop for Rectify (C-4 refinement, main loop);
  FR-033, FR-036, SC-012, SC-024 rows amended, plan C-5 / C-31. Then `BassCharacter_RLawHarvest` re-run and
  `paste-harvest.js` pastes all three tables.
- **RULED 2026-10-16 (close, user; Clarifications "Group 10 close rulings", SC-012(e) descriptor):** FR-031 descriptor rule:
  `true` when h2 re h1 < −20 dB, −24 … −16 dB recorded as the uncertainty band; `BassCharacter_RLawUpstreamChain` applies
  the rule from its measured w₂ and records in-band cells (the triangle chain at −19.9 dB); SC-012 row amended, plan C-30.
- **RULED 2026-10-16 (close, user; FR-064):** `DCBlocker::setState` stays under FR-064's append-only fallback (row amended;
  consumer suites green per comply).
- **FIXED 2026-10-16 (close, main loop; SC-003 [long], 9 saw cells at the s = 0.5 handover, plan C-32):** ShapeMorph applies
  the regime weight after the shared path and ROUND's low-pass and keeps HOLLOW's emphasis inverse running (flat while
  BITE is off, not reset at BITE entry). `ShapeMorph_AutomationClickFree`, `ShapeMorph_SweepContinuity`, the ShapeMorph
  fast set, the chain [long] cases and the CPU lane re-measured.
- **REFINED 2026-10-16 (close, main loop; SC-012(b) / SC-026(a) after the paste):** 33 red assertions traced to the harvest
  line printing four decimals (0.9102 for the exact bisection value 0.91015625), which put a pasted edge 0.00004 outside
  the measured band; the harvest now prints the exact k/128 values (%.8f) and `BassCharacter_RLawHysteresisBandWidth`'s
  observer sweep walks the 1/1280 lattice (lands exactly on the edges; a 0.005 grid read the up-flip 0.005 past an exact
  edge). Harvest re-run, re-pasted, both cases re-measured. No bar changed.
- **RULED 2026-10-16 (close, user; C-33, FR-016):** the SHAPE smoother is a restartable 10 ms raised-cosine ramp
  (`ShapeMorph::ShapeRamp`), replacing the 10 ms one-pole; plan S6 global line amended; SC-002 / SC-003 / fast / chain
  re-measured on it.
- **RULED 2026-10-16 (close, user; SC-003(a)):** single-step SHAPE cells record a detection that falls inside the 10 ms ramp
  window (step … step + 480 + N) and gate on arm (b); detections outside still count; `checkShapeMove` splits the count
  (`insideRamp` recorded, `gatedCount` gated). Row amended.
