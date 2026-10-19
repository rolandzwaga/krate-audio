# Tasks: Profundum Phase 7 — Bass Stereo Field

**Spec:** `specs/profundum-phase7-stereo-field/spec.md` (Clarified 2026-10-18, Q1–Q8) · **Plan:** `specs/profundum-phase7-stereo-field/plan.md`
(S0–S16, C-1…C-18, R-1…R-11, handoffs, review notes) · **Model:** `specs/profundum-phase7-stereo-field/stereo-field-model.js` · **Phase base:**
commit `4599d180` on `synth/profundum`
**Status:** Task list (2026-10-18). The build stage has not started. Plan S1 was **ratified 2026-10-18** (spec Clarifications "Session 2026-10-18 (plan-stage rulings)"): C-1,
C-2 and C-17 ruled to the recommended reading, C-9 (A), C-10, C-15 and C-18 ratified, P-0 fixed in the workflow. The
switches below are built at their RULED values. No commit tasks: commits happen outside this workflow.

## Read this first (every executor)

### Precondition: plan S1 (C-1…C-18)

The plan marked three items ⚠ RE-RULE (C-1, C-2, C-17), one ⚠ RISK (C-13) and three "for ratification" (C-9, C-15, C-18). **All were ruled
before the build stage (2026-10-18; spec rows and Clarifications carry the rulings).** Every switch below is built at its RULED value; the
spec-as-written reading is still computed and printed beside the gate, never gated. The other refinements are encoded exactly as the plan
writes them. No case is red by design any more: a red in the cases named below is a defect for T016.

| Item | What the tasks build (spec-bar default) | The one constant a ruling flips | Spec row | Task(s) |
|---|---|---|---|---|
| C-1 RULED | `BassStereoField_SafetyStageMidBitExact` asserts fl((L_out + R_out)·0.5f) bit-equal to fl((L_in + R_in)·0.5f) on every sample (red on any non-zero side). The numeric bound 2⁻²³·(\|M\| + \|S'\|) is asserted unconditionally in `BassStereoField_SafetyStageGates` | `kGateMidBitExact = false` (ruled 2026-10-18) in `bass_stereo_field_test.cpp` | FR-025 b2, SC-005 | T006 |
| C-2 RULED | `BassStereoField_StageLrInLrOutDirtyState` and `BassStereoField_AnchorBitIdenticalDirtyState` assert L == R from the first L == R input sample after L ≠ R content (red while the side biquads ring down). The from-reset and after-SOLID arms are separate, always-gated cases | `kGateLrBitExactAnyState = false` (ruled 2026-10-18) in `bass_stereo_field_test.cpp` | FR-025 b3, FR-062, SC-004(b)(c) | T006 |
| C-17 RULED | `BassStereoField_CurveInterpolationMixedPairs` gates the 0.5 dB endpoint bar on mixed pairs, the plan's counter-example included (red). Single-control and same-direction pairs are gated in `BassStereoField_CurveContinuous` | `kGateMixedInterpolationPairs = false` (ruled 2026-10-18) in `bass_stereo_field_test.cpp` | SC-021 | T004 |
| C-13 RULED (G8-1, 2026-10-18) | The harvest (T013 / T015) chooses K_wide in {1.0, 0.75} on SC-007 and the SC-010 WIDE lever (mono sum recorded), then g_floor in {0.71, 0.5, 0.35, 0.25, 0.18, 0.125, 0.1, 0.071, 0.05, 0.035, 0.03} as the largest passing every loud, non-exempt cell and the Gravity lever; escalation floor 0.03; octave Q recorded on the nonlinear arms; anti-phase cells recorded; bands read above −60 dBFS | the `HARVEST-BEGIN wide-floor` block in `bass_stereo_field.h` | FR-021, FR-041, SC-001, SC-002, SC-020 | T013, T015 |
| C-9 (ratified 2026-10-18, option A) | Option (A), the octave-folded synced tempo, is built; `evidence/sc008-sync-fold.csv` records the aliasing | `kCycleSyncFold = true` in `bass_stereo_field.h`. A `false` ruling is option (B): the fold is skipped, and the SC-008 sync arm asserts the ≤ 1 Hz bar only for the note values whose unfolded rate is ≤ 1 Hz at the tempo under test (Phase 11 exposes that subset) | FR-036, SC-008 | T003, T011 |
| C-15 (ratified 2026-10-18) | SOLID's zero vector reached by slew: target exactly 0 at once, emitted vector bit-zero from the 334th query on | — (a contrary ruling rewrites T004) | FR-011 | T004 |
| C-18 (ratified 2026-10-18) | `TrackingMode::Efficient`, combined corner bound ε_s(fs) + 0.1 Hz, checked by computation | — | FR-024, SC-009(c) | T006 |
| C-3…C-8, C-10…C-12, C-14, C-16 | Encoded as written: inline M/S formulas (C-3), `reset()` calls `CrossoverLR4::prepare` (C-4), the warped raised cosine (C-5), Δp_max = 0.003 (C-6), explicit `advanceControl()` (C-7), the partial-count argument (C-8), offset signs {+K, −K} (C-10), g_side = floor·(1 − G) (C-11), a new chain helper that only reads Phase 6 headers (C-12), the phase-check skip rule (C-14), the fitted references (C-16) | — | — | T003–T012 |
| R-1 | T001 measures the per-voice pan-forwarding cost before the law is built; the only lever is cadence (FR-016), which bends Cycle above ≈ 0.7 Hz and therefore needs a ruling | — | SC-013(b) | T001 |

**P-0, RESOLVED 2026-10-18 before the build stage: `~[.]` was appended to `PHASE_FILTER`.** The workflow's gate filter
`[profundum]~[performance]~[perf]~[benchmark]~[!benchmark]~[long]` (`.claude/workflows/profundum-phase.js`, `PHASE_FILTER`) **selects hidden
cases**. Measured 2026-10-18 on the 12:44 build of `dsp_systems_tests.exe`: `--list-tests` with that filter lists 295 cases, five of them hidden
(`ProfundumCore_ListeningRenders`, `ProfundumEdgeFold_ListeningRenders`, `ShapeMorph_LoudnessTableHarvest`, `BassCharacter_RLawHarvest`,
`MassLaw_TableHarvest`); appending `~[.]` lists 290 and drops exactly those five. `MassLaw_TableHarvest` alone took 10–30 min in Phase 6, so every
Phase 7 gate pays that unless the filter gains `~[.]`. The workflow script is outside this task list; the main loop decides before the build
stage. This phase's own harvest is tagged without `[profundum]` (deviation 4), so it adds nothing to that cost either way.

### Deviations from the plan made while cutting these tasks (surface them; do not silently revert)

1. **CMake registration is the first code task (T002), not the last.** Every later task is test-first and its TU must be in the build to fail;
   `dsp/tests/CMakeLists.txt` enumerates TUs (the Phase 6 block at `:718–742`), so an unregistered TU silently drops out. T002 creates compiling
   stubs; the last group's single CMake task (T018) audits the registration. Phase 6 `tasks.md` deviation 1 is the precedent.
2. **`dsp/CMakeLists.txt` gains the new header.** Plan S14.1 leaves this to T002 to check: the systems header list exists and Phases 3, 5 and 6
   added their headers there (`dsp/CMakeLists.txt:206–213`), so `include/krate/dsp/systems/bass_stereo_field.h` goes after `mass_law.h` (`:213`).
3. **Plan S13.1's `bass_stereo_field_spectral_test.cpp` is split in two, and the harvest gets its own TU,** so each parallel task owns one TU and a
   red localises: `bass_stereo_field_monosum_test.cpp` (SC-001 + SC-002 shards and smoke, SC-020 rendered), `bass_stereo_field_spectral_test.cpp`
   (SC-003, SC-005 record, SC-006, SC-010, SC-016), `bass_stereo_field_harvest_test.cpp` (S9.1). Nine TUs in all (file map).
4. **The harvest TEST_CASEs do not carry `[profundum]`.** A Catch2 tag filter that matches a hidden case runs it (measured, P-0), so a
   `[profundum]`-tagged harvest would run inside every gate. They are tagged `[stereo-field-harvest][.calibration]` (Phase 6 precedent:
   `dsp/tests/unit/systems/mass_compressor_chain_test.cpp:6–10, 345`). Every other TEST_CASE of the phase carries `[profundum]`.
5. **A test probe, `detail::BassStereoFieldProbe`** (`dsp/tests/unit/systems/bass_stereo_field_probe.h`), befriended by the field. The harvest
   must evaluate candidate g_floor / K_wide values without editing the header per candidate. The field therefore holds `sideFloorGain_` and
   `wideOffsetScale_` members initialised from `kSideFloorGain` / `kWideOffsetScale`; only the probe changes them, and `reset()` leaves them
   alone. Precedent: `BassCharacterStage` befriends `detail::BassCharacterStageProbe` (`systems/bass_character_stage.h:71, 513`). The ODR sweep covers the name (every `BassStereoField*`: 0 hits).
6. **Every `[long]` case has a non-long `…Smoke` sibling** (one cell) so each gate exercises the path (gates exclude `[long]`). Phase 6
   deviation 10 is the precedent. No bar changes: the `[long]` shards gate the full grid.
7. **The harvest's g_floor step re-runs only the stage on recorded stage inputs** (`StereoFieldTest::restage`, T007). The stage is the last
   component that depends on g_floor, so one chain render per cell serves all seven candidates exactly. The step-2 grid is the SC-001 grid at
   Gravity 0 with MOTION depth 0 (MOTION only pulls width inward, FR-030); the `[long]` SC-001 shards then gate every MOTION cell with the
   committed value. A MOTION-cell red there is a defect for T016 (re-harvest), never a relaxed bar.
8. **Mono-sum descriptors are computed in the frequency domain** (Parseval over the masked bins of one Hann-tapered power-of-two window).
   This equals "zero-phase band mask, then time-domain energies and zero-lag correlation" without the inverse FFT; `ProfundumTest::fftDouble`
   takes power-of-two sizes only (`profundum_core_test_helpers.h:173`). The helper self-test checks the equivalence.
9. **SC-013 runs the one case pinned** through `tools/pin-perf-cores.ps1` (Phase 6 deviation 9): `node tools/run-cpu-tests.js dsp_systems_tests`
   would also run every `[long]` case of the exe.
10. **SC-004's bit-identity grid and SC-016's re-prepare arm stay per-push** (root CLAUDE.md: never tag bit-identity or lifecycle cases
    `[long]`), with short renders sized to the grid (T012, T010).
11. **SC-002's spec case name `BassStereoField_SubBandCorrelation`** is realised as the SC-001 shard set carrying `[sc-002]` (plan review notes,
    the last bullet of `plan.md`).

### File map

| File | Kind | Layer / target | Created / edited by |
|---|---|---|---|
| `dsp/include/krate/dsp/systems/bass_stereo_field.h` | new header (L0 `core/db_utils.h`, `core/modulation_source.h`, `core/note_value.h`; L1 `primitives/smoother.h`, `primitives/lfo.h`; L2 `processors/brownian_drift.h`, `processors/crossover_filter.h`, `processors/harmonic_types.h`; L3 peer `systems/bass_character_stage.h` for `Algorithm` / `kMaxChannelOffset` only) | L3 | T002 stub; T003 skeleton; T004 law; T005 WIDE; T006 stage; T015 (harvest paste only) |
| `dsp/tests/unit/systems/bass_stereo_field_probe.h` | new test header | — | T003 (read-only after) |
| `dsp/tests/unit/systems/bass_stereo_field_test_helpers.h` | new test header (namespace `Krate::DSP::StereoFieldTest`) | — | T007 (read-only after) |
| `dsp/tests/unit/systems/bass_stereo_field_test.cpp` | new TU (interface, law, WIDE and stage unit cases) | `dsp_systems_tests` | T002 stub; T003; T004; T005; T006 (each appends) |
| `dsp/tests/unit/systems/bass_stereo_field_helpers_test.cpp` | new TU | `dsp_systems_tests` | T002 stub; T007 |
| `dsp/tests/unit/systems/bass_stereo_field_nonfinite_test.cpp` | new TU, **`-fno-fast-math`** | `dsp_systems_tests` | T002 stub; T008 |
| `dsp/tests/unit/systems/bass_stereo_field_monosum_test.cpp` | new TU (shards `[long]`) | `dsp_systems_tests` | T002 stub; T009 |
| `dsp/tests/unit/systems/bass_stereo_field_spectral_test.cpp` | new TU (grids `[long]`) | `dsp_systems_tests` | T002 stub; T010 |
| `dsp/tests/unit/systems/bass_stereo_field_motion_test.cpp` | new TU (grids `[long]`) | `dsp_systems_tests` | T002 stub; T011 |
| `dsp/tests/unit/systems/bass_stereo_field_chain_test.cpp` | new TU (SC-007 `[long]`) | `dsp_systems_tests` | T002 stub; T012 |
| `dsp/tests/unit/systems/bass_stereo_field_harvest_test.cpp` | new TU, hidden cases only | `dsp_systems_tests` | T002 stub; T013 |
| `dsp/tests/unit/systems/bass_stereo_field_perf_test.cpp` | new TU, `[performance][.perf]`, **not** `-fno-fast-math` | `dsp_systems_tests` | T002 stub; T014 |
| `dsp/CMakeLists.txt`, `dsp/tests/CMakeLists.txt` | existing | — | T002 only (T018 audits) |
| `specs/profundum-phase7-stereo-field/bench/` | bench source + script | — | T001 |
| `specs/profundum-phase7-stereo-field/evidence/` | TXT/CSV written by tests; `close/` lane scripts; `run-log/<Tnnn>.txt` | — | tests; T001, T013–T015, T017, T019–T021 |

**Untouchable (zero diff lines vs `4599d180`, FR-073, SC-015):** everything under `dsp/include/` except the one new header — in particular
`processors/harmonic_oscillator_bank.h`, `processors/harmonic_oscillator_bank_simd.h`, `systems/profundum_core.h`,
`systems/bass_character_stage.h`, `processors/crossover_filter.h`, `processors/midside_processor.h`, `processors/brownian_drift.h`,
`primitives/lfo.h`, `primitives/smoother.h`, `systems/stereo_field.h`; and every existing test file and helper (`mass_dynamics_test_helpers.h`,
`profundum_core_test_helpers.h`, `shape_character_test_helpers.h`, `../processors/shape_character_metric_helpers.h`,
`../processors/harmonic_fold_test_helpers.h`, `tests/test_helpers/*`). C-12: the new chain helper **includes** the Phase 6 helper read-only. If a
reused header looks unusable, STOP AND SURFACE: FR-073 has no exception.

### Verified facts these tasks depend on (each read on 2026-10-18 in this session)

Paths relative to `dsp/include/krate/dsp/` unless stated. Re-derive every line anchor from the live file before editing.

- **CMake.** `dsp/tests/CMakeLists.txt`: the Phase 6 systems comment block is `:718–730` and its TUs `:731–742`; the `dsp_systems_tests` list
  closes with `)` at `:743`. The `-fno-fast-math` block's Phase 6 lines are `:1235–1237` (`# Profundum Phase 6: …`,
  `unit/processors/transient_shaper_nonfinite_test.cpp`, `unit/systems/mass_dynamics_nonfinite_test.cpp`), then
  `PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"` at `:1238`. `KRATE_DSP_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}"` is a
  compile definition (`:833`). `dsp/CMakeLists.txt:211–214`: `# Profundum Phase 6 (…)`, `mass_dynamics.h`, `mass_law.h`, `)`.
- **`ProfundumCore`** (`systems/profundum_core.h`): `kDefaultPartials = 64` `:96`, `kControlInterval = 32` `:97`, `kDefaultF0Hz = 55.0f` `:100`;
  `prepare(double, int numPartials = kDefaultPartials)` `:129`; `reset()` `:161`; `noteOn(float)` `:208`;
  `setPartialPanOffsets(const std::array<float, kMaxPartials>&)` `:218`; `deliveredGains()` `:298`; `currentFrequency()` `:302`;
  `numPartials()` `:305`; `ExcitationControls { mix = 0, drive = 0.5, asymmetry = 0 }` `:334–338`; `static const ExcitationControls
  kDefaultExcitation` `:340`; `setExcitation(const ExcitationControls&)` `:356`.
- **Bank** (`processors/harmonic_oscillator_bank.h`): `applyPanOffsets` loops over all `kMaxPartials`, `angle = π/4 + newPos·π/4`,
  `panLeft_ = cos`, `panRight_ = sin`, no smoothing `:647–659`; "Odd harmonics (1,3,5,...) pan left (-), even (2,4,6,...) pan right (+)"
  `:1155–1157`. `kMaxPartials = 96` (`processors/harmonic_types.h:20`).
- **`CrossoverLR4`** (`processors/crossover_filter.h`): namespace-scope `enum class TrackingMode` `:41`; constants `:102–106`; `prepare(double)`
  configures the smoother from the stored smoothing time, snaps it to the clamped stored frequency, sets `lastCoefficientFreq_`, updates the
  coefficients and resets the 4 biquads, with no allocation `:169–192`; `reset()` `:200`; `setCrossoverFrequency` `:218`; `setSmoothingTime`
  `:231`; `setTrackingMode` `:245`; `getCrossoverFrequency()` `:255`; `process(float)` returns {0, 0} unprepared, advances the smoother **once per
  call**, HighAccuracy recomputes while `!isComplete()`, Efficient recomputes when `|current − lastCoefficientFreq_| >= kHysteresisThreshold`
  `:289–323`. Consequence used in T006: while the side path idles (SOLID) `process` is not called, so the crossover's smoother does not advance.
- **Smoothers** (`primitives/smoother.h`): `kMinSmoothingTimeMs = 0.1f` `:58`; `OnePoleSmoother` `:134`, `configure(float ms, float sampleRate)`
  `:160` (the rate is a **float**), `getTarget` `:185`, `getCurrentValue` `:191`, `isComplete` `:232`, `snapToTarget` `:257`, `snapTo` `:263`;
  `LinearRamp` `:305`, `configure(float, float)` `:329`, `getTarget` `:358`, `isComplete` `:409`, `snapTo` `:421`.
- **`LFO`** (`primitives/lfo.h`): `class LFO` with no base `:63`, copy deleted and move defaulted `:69–76`, `prepare(double)` `:83`, `reset()`
  `:95`, `[[nodiscard]] float process()` `:114`, `setWaveform` `:189`, `setFrequency` `:218`, `setTempoSync` `:237`, `setTempo` `:246`,
  `setNoteValue(NoteValue, NoteModifier = None)` `:255`.
- **`BrownianDrift`** (`processors/brownian_drift.h`): `: public ModulationSource` `:94`; `kTauMin = 0.2f` `:97`, `kTauMax = 30.0f` `:99`,
  `kDefaultDriftSeed = 0xB17Eu` `:109`; `prepare(double)` `:121`, `reset()` `:133`, `setSeed` `:145`, `setSmoothness` `:152`, `setDepth` `:159`,
  `setMean` `:165`, `process()` `:178`, `processBlock(size_t)` `:194`, `getCurrentValue()` `:212`, `getSourceRange()` `:217`.
- **`note_value.h`** (`core/`): `NoteValue { QuadrupleWhole = 0, TripleWhole, DoubleWhole, Whole, Half, Quarter, Eighth, Sixteenth, ThirtySecond,
  SixtyFourth }` `:38–49`; `NoteModifier { None = 0, Dotted, Triplet }` `:54–58`; constexpr `getBeatsForNote` `:95`; `kMinTempoSyncBPM = 20.0`
  `:200`, `kMaxTempoSyncBPM = 300.0` `:203`.
- **`BassCharacterStage`** (`systems/bass_character_stage.h`): nested `Algorithm { Tube, Fold, Rectify, Bite, Crush, Fracture }` `:78`;
  `kParamSmoothMs = 10.0f` `:84`; `kMaxChannelOffset = 0.15f` `:85`; `prepare(double, size_t)` `:176`; `reset()` `:204`; `setAlgorithm` `:241`;
  `setAmount` `:258`; `setTone` `:265`; `setChannelOffsets(float amountOffset, float toneOffset)` (L = centre + offset, a non-zero offset
  unlinks the channels) `:271–281`; `setFundamentalHz` `:287`.
- **`ShapeMorph`** (`systems/shape_morph.h`): `prepare(double, size_t)` `:135`, `reset` `:172`, `setShape` `:194`, `setEnabled` `:202`,
  `setFundamentalHz` `:209`, `processBlock(float*, float*, size_t, …)` `:221`. **`AnchorOscillator`** (`systems/anchor_oscillator.h`):
  `kDefaultLevel = 0.5f` `:72`, `Controls` `:99`, `setCoreH1Amplitude` `:262`, `setRetriggerPhase` `:269`, `noteOn(float, bool rearm = true)`
  `:276`. **`HarmonicFold::kCalibrationPeak = 0.3552343859f`** (`processors/harmonic_fold.h:83`).
- **Phase 6 chain helper** (`dsp/tests/unit/systems/mass_dynamics_test_helpers.h`, namespace `Krate::DSP::MassDynamicsTest` `:74`): `kGrid`
  `:81` (static_assert against the core, MassDynamics and Anchor intervals `:82–84`), `kSettleSeconds = 1.0` `:87`, `descriptorSeconds` `:90`,
  `renderLength` `:95`, `gridDriver(total, hostBlock, fn(start, len, atGrid))` `:103–114`, `ChainConfig` `:173–193` (`base`, `f0`, `fs`,
  `seconds`, `hostBlock = 512`, `attachAnchor = true`, `noteOnSample`, …), `ChainTaps` `:195–211`, `allocateTaps` `:213`, `chainTargets`
  `:248`, `coreControlsFor` `:275`, `unbiasedH1Amplitude` `:288`, `anchorControlsFor` `:294`, `processChainTail` `:302–340` (sum → pre-gain →
  MassDynamics → DC ×2 → limiter; no insertion point), `ChainDynamics` / `makeChainDynamics` `:342–362`, `prepareChainLaw` `:364`,
  `renderChain` `:372–437` (per run: `core->processBlock`, `anchor->setCoreH1Amplitude` + `processBlock`, copy to the fold tap,
  `fold->processBlock`, then the tail), `renderPoly` `:454`, `inverseFftDouble` `:829`.
- **Phase 5 helpers.** `ShapeCharacterTest::noteHz` and `kE0Hz … kC3Hz` (`systems/shape_character_test_helpers.h:48–55`), `kFixtures`
  (`SpectralShapeRecipe::kSawAnchor` = "saw", `kGrowl` = "growl") `:66–71`; `ClickCount` (`processors/shape_character_metric_helpers.h:44`),
  `differentialClicks(transition, refStart, refEnd, n, fs, f0)` `:76` and `(transition, reference, n, fs, f0)` `:137`. Phase 5's aliasing
  measure is `HarmonicFoldTest::aliasedPower` on the last `HarmonicFoldTest::aliasFftLength(fs, f)` samples after the settle
  (`systems/bass_character_spectral_test.cpp:140–149`), with the lattice frequency f0/2 for Fracture (`:998`).
- **Core test helpers** (`systems/profundum_core_test_helpers.h`, namespace `Krate::DSP::ProfundumTest`): `requireLREqual(L, R, n)` `:61`,
  `goertzelPhase(x, L, fs, hz)` `:157`, `fftDouble` (radix-2, power-of-two sizes only) `:173`, `bh7PowerSpectrum(x, L)` `:206`,
  `renderedHarmonicPowers(x, fs, f0, N)` (a `TestUtils::kLowFrequencyFftSize` frame) `:235`, `aliasFftLength` `:257`, `aliasedPower(x, fftLen,
  fs, f0)` `:273`, `SidebandResult` `:307`, `sidebandPower(x, fs, f0, controlInterval, pitchInterval, amRateHz, vibratoPeakHz, totalRef)` (no
  length argument: read its body for the frame it reads) `:323`, `rmsDbfs(x, n)` `:453`, `renderReference(…)` `:498`.
  `TestUtils::kLowFrequencyFftSize = 262144` (`tests/test_helpers/low_frequency_metrics.h:64`).
- **`AllocationScope`** (`tests/test_helpers/allocation_detector.h:111–127`): the count is latched in the **destructor**; read
  `AllocationDetector::instance().getAllocationCount()` before the scope closes (`:145–147`). Included as `<allocation_detector.h>`.
- **Tools.** `tools/pin-perf-cores.ps1` takes `-Exe` (mandatory) and `-ExeArgs` (`:19–21`); `tools/check-portability.js`;
  `tools/lint-apple-globals.js`. Phase 6 close scripts to copy: `specs/profundum-phase6-mass-dynamics/evidence/close/{suites,tidy,portability,
  cpu-lane,harvest}.sh` and `paste-harvest.js`; Phase 6 bench script: `specs/profundum-phase6-mass-dynamics/bench/run-bench.sh`.
- **ODR sweep (2026-10-18, this session).** `grep -rn "BassStereoField\|StereoFieldTest\|FieldChainConfig\|renderFieldChain\|PanReference\|
  MotionOutput\|FieldChainTaps\|renderFieldPoly\|renderPanReference\|zeroPhaseBandMask\|monoSumRatioDb" dsp plugins tests tools` → **0 hits**;
  `goertzelComplex`, `octaveBands`, `thirdOctaves`, `sideToMidDb`, `widthDb`, `correlation(`, `bandReading` → 0 hits in any test header. Hazards
  never reused: `StereoField` / `StereoMode` (`systems/stereo_field.h`), `CharacterProcessor`, `SubOscillator`, `MorphEngine`,
  `TransientDetector`; the namespace-scope `TrackingMode` (crossover_filter.h:41) and `Waveform` / `kMinFrequency` (lfo.h).

### Commands (main loop and gates; executors do not build or run the test tree)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests > <log> 2>&1; tail -5 <log>
B=build/windows-x64-release/bin/Release
$B/dsp_systems_tests.exe "[stereo-field]~[performance]~[perf]~[benchmark]~[!benchmark]~[long]" 2>&1 | tail -5   # the phase's per-push cases
$B/dsp_systems_tests.exe "BassStereoField_MonoSumPerBand_Spread_DirtyRectify_E1" 2>&1 | tail -5                  # one [long] shard by name
```

Never `ctest -R`. A positional `"[a]~[long]"` is one AND-term; separate arguments OR. Hidden cases (`[.perf]`, `[.calibration]`) run when a
filter names them or one of their tags. Capture any run longer than ~30 s to a log and read the log; never re-run a slow suite for its output.
One build of a tree at a time. Only `dsp_systems_tests` contains Phase 7 TUs.

### Rules for every task

1. **Failing test first:** write the test cases before the implementation in the same task. Do NOT build or run the test tree (the group's gate
   does). Re-read every file you wrote and fix anything that obviously would not compile. Zero warnings is a gate criterion (MSVC C4244 /
   C4267 / C4100: `f` suffixes, explicit casts, `[[maybe_unused]]`).
2. Header-only, `namespace Krate::DSP {` (nested form). Banner: "Layer 3: System", "Profundum Phase 7 (specs/profundum-phase7-stereo-field)",
   the FRs / SCs it serves and the plan C-items it encodes. Includes point **downward only** (file map row). Never `profundum_core.h` (C-8),
   never `midside_processor.h` (C-3: its formulas are cited in a comment, `midside_processor.h:200–201, 225–226` per plan S0), never an L4
   header. New namespace-scope names: exactly `BassStereoField` and the forward-declared `detail::BassStereoFieldProbe`. Everything else
   (`ImageMode`, `MotionSource`, `Controls`, `VoiceState`, `ChannelOffsets`, `MotionOutput`) is nested.
3. PascalCase classes, camelCase functions, `trailing_` members, `kPascalCase` constants. Designated initialisers in every aggregate brace init
   with mixed types; every double → float via `static_cast`; no narrowing. Finiteness only via `detail::isFinite` (`core/db_utils.h`); never
   `std::isnan` / `std::isfinite`. Bit comparisons via `std::bit_cast<std::uint32_t>` / `std::memcmp`, never `==` where −0 or NaN matter. Never
   hand a non-finite value to `std::clamp`, `OnePoleSmoother::setTarget` or `LinearRamp::setTarget`. Every constant with a namespace-scope
   twin (`kMinFrequency`, `kMaxFrequency`) is referenced with its class qualifier.
4. **Zero is +0.0f.** Every emitted pan entry and every offset that is zero is the bit pattern 0, never −0.0f: compute `s · scale · c` only
   when both `scale` and `c` are non-zero, else write `0.0f`. FR-011's gate and the core's bit compare (`profundum_core.h:218–235`) depend on it.
5. Audio-thread methods (`processBlock`, `setControls`, `setTempoBpm`, `setDriftSeed`, `advanceControl`, `computePanVector`, `wideOffsets`,
   `reset`) are `noexcept` and allocation-, lock-, exception- and I/O-free; fixed-size members only (`std::array`); no `static` mutable data; no
   SIMD; no `std::function` in product code. `processBlock` with a null `left` or `right`, or with 0 samples, is a no-op that leaves the buffers untouched.
6. Test helpers are `inline` functions in namespace `Krate::DSP::StereoFieldTest`; never an anonymous namespace in a header. TU-local helpers go
   in an anonymous namespace with a `bsf` prefix. **Qualify every call into `MassDynamicsTest::`, `ShapeCharacterTest::`, `ProfundumTest::` and
   `HarmonicFoldTest::`** (same-named helpers exist across them); no `using namespace` in any Phase 7 file.
7. **Tags.** Every TEST_CASE carries `[systems][profundum][stereo-field]` plus its SC tag (`[sc-001]` …), except the harvest cases
   (`[stereo-field-harvest][.calibration]`, deviation 4). The non-finite TU adds `[nonfinite]`; perf adds `[performance][.perf]`. `[long]` only on
   the cases named `[long]` below, each with its `…Smoke` sibling. **Never `[long]`:** NaN/Inf guards, L == R identity, bit-identity, RT safety,
   lifecycle and state cases.
8. No bit-exact float goldens. `memcmp` only between two renders of the same build in the same process, or L against R of one render.
9. Notes (440·2^((m − 69)/12) in double via `ShapeCharacterTest::noteHz`, cast once): E0 = MIDI 16 = 20.60172 Hz, E1 = 28 = 41.20344,
   C2 = 36 = 65.40639, E2 = 40 = 82.40689, C3 = 48 = 130.81278, C4 = 60 = 261.62557. fs = 48 kHz and host block 512 unless a case says otherwise.
10. **Fixtures** (spec descriptor block). Clean core: `SpectralShapeRecipe::kSawAnchor` and `kGrowl`, excitation off. Dirty arm: core →
    `ShapeMorph` at shape 1 (BITE) → `HarmonicFold` at 100 → `BassCharacterStage` at the algorithm, Amount 1, Tone 0.5. Anchor arm: the dirty
    arm plus the Anchor on Sine / LOCK at `AnchorOscillator::kDefaultLevel`, a₁ = `MassDynamicsTest::unbiasedH1Amplitude` (the Phase 6 feed).
    Dirty and Anchor arms use `kSawAnchor`. Every measured value is printed with `std::setprecision(6)`.
11. Evidence: tests write TXT/CSV under `specs/profundum-phase7-stereo-field/evidence/` through `KRATE_DSP_TESTS_DIR` (it points at `dsp/tests`,
    so the repo root is `KRATE_DSP_TESTS_DIR "/../.."`); create directories with `std::filesystem::create_directories`, outside any
    `AllocationScope`.
12. If a test you write exposes a defect in a file another task owns, do not edit that file: add a `DEFECT: <case> <expected/observed>
    <diagnosis> <owning file>` line to your run-log. T016 resolves them.
13. Never relax a threshold or shrink a workload to pass. A bar proven unreachable is STOP AND SURFACE (status "blocked", measured table in
    notes).
14. Write `specs/profundum-phase7-stereo-field/evidence/run-log/<Tnnn>.txt` for your task: files written, every decision taken, any deviation,
    any `DEFECT:` line.
15. **Lane rule (2026-10-14):** suites, `[long]` shards, clang-tidy sweeps, CPU/perf lanes and harvests are main-loop work. A task that owns one
    writes the lane script under `specs/profundum-phase7-stereo-field/evidence/close/` and returns done with notes "deferred to the main loop at
    close: run <script>" (or "blocked" where its section says STOP AND SURFACE).
16. Every test that relies on a plan C-item cites it in a comment (`// plan S1 C-6`); every SC test cites its SC id in a comment above the
    TEST_CASE.

---

## Group 0 — CPU feasibility bench (alone; STOP AND SURFACE)

### T001 Microbench the per-voice pan forwarding and the global stage before any header is written (plan R-1, S12.6, S16 step 1)

**Files:** `specs/profundum-phase7-stereo-field/bench/stereo-field-bench.cpp`, `specs/profundum-phase7-stereo-field/bench/run-bench.sh`,
`specs/profundum-phase7-stereo-field/evidence/model-2026-10-18.txt`, `specs/profundum-phase7-stereo-field/evidence/run-log/T001.txt`.

- **Precondition:** nothing else running (no build, suite or other agent).
- **Model evidence (executor, < 1 s):** run `node specs/profundum-phase7-stereo-field/stereo-field-model.js` once and save its stdout to
  `evidence/model-2026-10-18.txt` (plan S12). Sizing evidence, not compliance evidence.
- **Create `stereo-field-bench.cpp`.** It includes the **real** headers (`-Idsp/include`): `systems/profundum_core.h`,
  `processors/spectral_shape_recipe.h`, `processors/crossover_filter.h`, `primitives/smoother.h`, `primitives/lfo.h`,
  `processors/brownian_drift.h`, `core/db_utils.h`. It prototypes the new pieces locally, in an anonymous namespace with a `Bench` prefix,
  exactly as plan S4–S7 specify:
  - `BenchLaw`: the S4.1 per-interval precompute and the S4.2 / S4.3 per-partial target and slew over k = 1 … 63 (log2 table, x, γ = 4^curve,
    c = 0.5 − 0.5·cos(π·x^γ), s = (k even) ? −1 : +1, +0 for zero entries, Δ = 0.003 slew, bit-compare handoff count);
  - `BenchStage`: the S7.3 per-sample loop (guard, M/S encode, `LinearRamp` k, `OnePoleSmoother` g, one `CrossoverLR4` on S in `Efficient` mode at
    0.1 ms smoothing, decode, mask), the S7.1 per-interval targets, and per interval `BrownianDrift::processBlock(32)` and one `LFO::process()`
    (the LFO prepared at fs/32, Sine).
- **Cells** (48 kHz, host block 64, `kSawAnchor`, 64 partials):
  1. one `ProfundumCore` at E1 with `setPartialPanOffsets(allZero)` every interval (the baseline);
  2. the same core with `BenchLaw` evaluated every interval at Spread 1, MOTION Cycle depth 1 at 1 Hz (κ = 0.5) and the vector handed every
     interval (SC-013(b)'s worst cadence);
  3. cells 1 and 2 with four cores at E1, E2, C3 and A1 (55 Hz);
  4. `BenchStage` + MOTION advance, Gravity 1, static crossover 120 Hz, stereo distinct L / R (a kSawAnchor E1 render, R delayed by 0.37 period);
  5. cell 4 with the crossover control as a triangle 60 ↔ 250 Hz with 100 ms legs, through the 20 ms control-rate law smoother, so the stage
     recomputes coefficients every interval (plan S12.6);
  6. cell 5 at 192 kHz (recorded only).
- Report per cell ns per sample and % of one core = ns · 48000 / 1e7 (cell 6: ns · 192000 / 1e7). The per-voice figure is cell 2 − cell 1; the
  4-voice figure is cell 3 (law) − cell 3 (baseline). A `--quick` flag runs each cell for 0.2 s.
- **Create `run-bench.sh`** (Git Bash; copy the structure of `specs/profundum-phase6-mass-dynamics/bench/run-bench.sh`): compile with
  `"/c/Program Files/LLVM/bin/clang++.exe" -std=c++20 -O2 -ffast-math -march=x86-64-v2 -DNDEBUG -Idsp/include <bench.cpp> -o <scratch>/p7-bench.exe`;
  refuse to start while `tasklist | grep -ciE "cl.exe|dsp_.*_tests.exe|clang-tidy"` is non-zero; run the binary **3 times, 60 s apart**, each
  through `pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe <scratch>/p7-bench.exe`; print the median per cell and the decision line; append
  everything to `evidence/run-log/T001-bench.txt`.
- **Executor (allowed in a few minutes):** run the model (above), write both files, compile once and run with `--quick` to prove every cell
  prints (no timing decision from this run). Record the compile line and the quick output in `evidence/run-log/T001.txt`. Then **return status
  "blocked"** — STOP AND SURFACE: "main loop runs `bash specs/profundum-phase7-stereo-field/bench/run-bench.sh` alone (≈ 4 min), applies the
  decision rule, records it in `evidence/run-log/T001.txt` and as a 'Measured' paragraph in plan S12.6, then resumes the build with
  `unblock: [T001]`".
- **Decision rule (main loop):** per-voice ≤ 0.25 % **and** 4-voice ≤ 1.0 % **and** cells 4 and 5 each ≤ 0.5 % → proceed with the plan's design.
  Otherwise return to the user with the table and R-1's options: FR-073 freezes the bank, so the only lever is the handoff cadence of FR-016,
  which bends Cycle above ≈ 0.7 Hz and so changes SC-008's ruled 1 Hz bar. Relaxing a CPU ceiling is never the lever.

---

## Group 1 — CMake registration with compiling stubs (sequential, single task)

### T002 Register every Phase 7 file (deviations 1, 2, 3)

**Files:** `dsp/include/krate/dsp/systems/bass_stereo_field.h` (stub), the nine TUs of the file map (stubs), `dsp/CMakeLists.txt`,
`dsp/tests/CMakeLists.txt`, `specs/profundum-phase7-stereo-field/evidence/run-log/T002.txt`.

- **Re-run the ODR sweep** of the fact list (`grep -rn "BassStereoField" dsp plugins tests tools` and `find dsp plugins tests tools -iname
  "*bass_stereo*"`) and record the output in the run-log. Any hit other than a file this phase creates → STOP AND SURFACE.
- **Create the stub header** `systems/bass_stereo_field.h`: `#pragma once`, the banner (rule 2) and an empty `namespace Krate::DSP {}`.
- **Create stub TUs** under `dsp/tests/unit/systems/` (a file comment naming the SCs it will hold and the task that fills it,
  `#include <catch2/catch_test_macros.hpp>` and `#include <krate/dsp/systems/bass_stereo_field.h>`, no TEST_CASE): `bass_stereo_field_test.cpp`,
  `bass_stereo_field_helpers_test.cpp`, `bass_stereo_field_nonfinite_test.cpp`, `bass_stereo_field_monosum_test.cpp`,
  `bass_stereo_field_spectral_test.cpp`, `bass_stereo_field_motion_test.cpp`, `bass_stereo_field_chain_test.cpp`,
  `bass_stereo_field_harvest_test.cpp`, `bass_stereo_field_perf_test.cpp`. `bass_stereo_field_test.cpp` additionally includes
  `<krate/dsp/systems/profundum_core.h>`, `<krate/dsp/systems/mass_dynamics.h>`, `<krate/dsp/systems/bass_character_stage.h>`,
  `<krate/dsp/systems/anchor_oscillator.h>` and `<krate/dsp/systems/stereo_field.h>`, proving the new header coexists with its peers and with the
  name hazards.
- **Edit `dsp/CMakeLists.txt`:** after `include/krate/dsp/systems/mass_law.h` (`:213`), before `)` (`:214`), add
  `# Profundum Phase 7 (specs/profundum-phase7-stereo-field)` and `include/krate/dsp/systems/bass_stereo_field.h`.
- **Edit `dsp/tests/CMakeLists.txt`**, each insertion preceded by a "Profundum Phase 7 (specs/profundum-phase7-stereo-field) - ENUMERATED, not
  globbed:" comment block mapping TU → SCs (the Phase 6 block style at `:718–730`):
  - `dsp_systems_tests`: after `unit/systems/mass_dynamics_perf_test.cpp` (`:742`), before `)` (`:743`): the nine TUs (`unit/systems/…`), with
    comment rows `bass_stereo_field_test.cpp` (FR-011, FR-034, FR-053, SC-004(b)(c), SC-005 gates, SC-009(a′)(c), SC-011, SC-012, SC-013(c),
    SC-017, SC-018/019/020/021 law), `bass_stereo_field_helpers_test.cpp` (helper self-tests, C-12 chain equality),
    `bass_stereo_field_nonfinite_test.cpp` (SC-014 - in the -fno-fast-math block below), `bass_stereo_field_monosum_test.cpp` (SC-001, SC-002,
    SC-020 rendered; shards [long]), `bass_stereo_field_spectral_test.cpp` (SC-003, SC-005 record, SC-006, SC-010, SC-016; grids [long]),
    `bass_stereo_field_motion_test.cpp` (SC-008, SC-009(a); [long]), `bass_stereo_field_chain_test.cpp` (SC-004(a), SC-007 [long], SC-009(b),
    SC-018/019 rendered), `bass_stereo_field_harvest_test.cpp` (S9.1 harvest, hidden), `bass_stereo_field_perf_test.cpp` (SC-013 [.perf]).
  - `-fno-fast-math` block: after `unit/systems/mass_dynamics_nonfinite_test.cpp` (`:1237`), before `PROPERTIES` (`:1238`):
    `# Profundum Phase 7: injects NaN/Inf bit patterns (SC-014, FR-072). The perf TU stays out (shipping FP mode).` and
    `unit/systems/bass_stereo_field_nonfinite_test.cpp`. Nothing else.
- **Create** the directories `specs/profundum-phase7-stereo-field/evidence/run-log/` and `evidence/close/` (a `.gitkeep` in each).
- **Verify (executor, read-only):** re-read both CMake files around each insertion; `git diff --stat` shows only the two CMake files plus new
  files. The Group 1 gate builds `dsp_systems_tests` (it reconfigures automatically) and runs the `[profundum]` filter; the Phase 1–6 case
  count is unchanged (no Phase 7 case exists yet).

---
## Group 2 — `BassStereoField` skeleton: controls, lifecycle, control rate, MOTION (sequential, single task)

### T003 Header skeleton, probe and the control / lifecycle / MOTION tests (FR-001–FR-004, FR-020, FR-028, FR-030 source side, FR-031, FR-033–FR-036; SC-008 law parts, SC-020 law; plan S3, S5, S4.1, S7.1, C-4, C-7, C-9, C-11)

**Files:** `dsp/include/krate/dsp/systems/bass_stereo_field.h` (replace the stub), `dsp/tests/unit/systems/bass_stereo_field_probe.h` (new),
`dsp/tests/unit/systems/bass_stereo_field_test.cpp` (fill the stub). Target `dsp_systems_tests`. Depends on T002 (and T001 unblocked).

**Tests first** (all `[systems][profundum][stereo-field]` + the tag shown; include `<krate/dsp/systems/profundum_core.h>`,
`<krate/dsp/systems/mass_dynamics.h>` for the interval static_asserts):

- `BassStereoField_ConstantsMatchPlan` `[fr-001]`: `static_assert`s — `BassStereoField::kControlInterval == ProfundumCore::kControlInterval`
  (32) and `== MassDynamics::kControlInterval`; `kDefaultPartials == ProfundumCore::kDefaultPartials` (64); `kMinCrossoverHz == 60.0f`,
  `kMaxCrossoverHz == 250.0f`, `kDefaultCrossoverHz == 120.0f`, `kStageCrossoverMultiple == 2.0f`, `kUpperKneeOctaves == 2.0f`,
  `kLowerKneeMaxOctaves == 0.25f`, `kMaxCurveExponent == 4.0f`, `kMaxPanStepPerInterval == 0.003f`, `kMotionExcursion == 0.5f`,
  `kCycleMinHz == 0.05f`, `kCycleMaxHz == 1.0f`, `kDriftTauMaxSec == 30.0f`, `kDriftTauMinSec == 0.5f`, `kDefaultTempoBpm == 120.0f`,
  `kWideCeilingRectifyFracture == 0.10f`, `kSideGainSmoothMs == 10.0f`, `kModeCrossfadeMs == 10.0f`, `kCrossoverSmoothMs == 20.0f`,
  `kStageCrossoverSmoothMs == kMinSmoothingTimeMs`, `kDefaultDriftSeed == BrownianDrift::kDefaultDriftSeed`, `latencySamples() == 0`,
  `kSideFloorGain > 0.0f && kSideFloorGain < 1.0f`, `kWideOffsetScale > 0.0f && kWideOffsetScale <= 1.0f`, `kCycleSyncFold == true`.
- `BassStereoField_ControlsDefaults` `[fr-002]`: `Controls{}` has `mode == ImageMode::Spread`, `spread == 0.5f`, `gravity == 0.5f`,
  `crossoverHz == 120.0f`, `curve == 0.5f`, `motionDepth == 0.0f`, `motionRate == 0.3f`, `motionSource == MotionSource::Drift`,
  `cycleSync == false`, `cycleNote == NoteValue::Whole`, `cycleModifier == NoteModifier::None`; `std::is_aggregate_v<Controls>`.
- `BassStereoField_SetControlsClampsAndKeeps` `[fr-001][fr-002]`: on a prepared (48 kHz) field, each from a valid start: `spread` 1.5 → 1 and
  −0.2 → 0; `gravity` −1 → 0 and 2 → 1; `crossoverHz` 30 → 60 and 400 → 250; `curve` 2 → 1 and −1 → 0; `motionDepth` 3 → 1; `motionRate` −1 → 0
  and 5 → 1 (read back through `controls()`). A NaN, +Inf or −Inf in **each** float field keeps that field's previous value bit-for-bit
  (`std::bit_cast`) and changes no other field. `static_cast<ImageMode>(7)` and `static_cast<MotionSource>(9)` keep the previous enum. **Latched:**
  after `setControls({.crossoverHz = 250})` and before `advanceControl()`, `smoothedCrossoverHz()` and `stageCrossoverHz()` are unchanged
  (bit-equal); after one `advanceControl()` they have moved.
- `BassStereoField_IsValidControls` `[fr-003][sc-021]`: `isValidControls(Controls{})`; every float at its exact bounds (0, 1; crossover 60, 250)
  is valid; one ulp outside (`std::nextafter`) is invalid; NaN / ±Inf invalid; enum 7 invalid. For 20 random pairs of valid `Controls`
  (seeded `std::mt19937{7}`, floats uniform in range, enums equal within the pair) the field-wise linear interpolation at t ∈ {0.25, 0.5, 0.75}
  is valid.
- `BassStereoField_MotionRateMaps` `[fr-031][sc-008]` (plan C-9, S5): prepared at 48 kHz, after `setControls` + `advanceControl`:
  `cycleFrequencyHz()` at `motionRate` 0, 0.5, 1 = 0.05, 0.223607, 1.0 (±1e-5); `driftTauSeconds()` at 0, 0.5, 1 = 30, 3.87298, 0.5 (±1e-4); τ ∈
  [0.5, 30] at rate {0, 0.25, 0.5, 0.75, 1}.
- `BassStereoField_TempoSyncFold` `[fr-036][sc-008]` (C-9 option A): `cycleSync = true`. `setTempoBpm(120)`, Whole → `cycleFrequencyHz()` 0.5;
  Quarter → 1.0; SixtyFourth + Triplet → 0.75; `setTempoBpm(200)`, SixtyFourth + Triplet → 0.625 (all ±1e-5, after `advanceControl`). Over every
  `NoteValue` × `NoteModifier` at 20, 120, 200 and 300 BPM, `cycleFrequencyHz() <= 1.0f + 1e-6f`. `setTempoBpm(NaN)`, `(0)`, `(−1)`, `(+Inf)` each
  keep the previous tempo (the 120 BPM Whole reading stays 0.5); `setTempoBpm(10)` clamps to 20 (Whole → 0.083333); `setTempoBpm(400)` clamps to
  300 (Whole → unfolded 1.25 → folded 0.625). Write `evidence/sc008-sync-fold.csv` (columns bpm, note, modifier, unfolded_hz, folded_hz, aliases_with)
  for every note value × modifier at 120 and 200 BPM.
- `BassStereoField_DriftSeedDeterminism` `[fr-033][sc-012]`: two fields at 48 kHz, Drift, depth 1, rate 1: 2000 intervals of `motionValue()`
  from `reset()` are bit-identical between them; `setDriftSeed(0x1234u)` + `reset()` on one makes at least one interval differ; `reset()` again
  on both (seeds kept) reproduces each field's own first sequence bit-for-bit.
- `BassStereoField_MotionOutputConformance` `[fr-034]`: for each source at rate 1, depth 1, 10 000 intervals: through a
  `const ModulationSource& src = field.motionSource()`, `src.getCurrentValue()` is bit-equal to `field.motionValue()` on every interval, and both
  lie in [−1, 1]; `src.getSourceRange()` equals `{-1.0f, 1.0f}` exactly. Non-vacuity: Cycle's max |m| ≥ 0.99; Drift's max − min ≥ 0.1. **Both
  sources advance every interval:** a field that switches Drift → Cycle at interval 5000 emits from interval 5000 on the bit-same `motionValue()`
  as a field that ran Cycle from interval 0.
- `BassStereoField_SideGainLaw` `[sc-020][fr-021]` (C-11): `static_assert(BassStereoField::sideGainForGravity(1.0f) == 0.0f)`;
  `sideGainForGravity(0.0f) == kSideFloorGain`; over g = i/10, i = 0 … 10, the values are non-increasing and `sideGainForGravity(1.0f)` has the bit
  pattern 0. After `prepare` + `reset()` at Gravity 0.5, `currentSideGain()` equals `sideGainForGravity(0.5f)` bit-for-bit.
- `BassStereoField_CrossoverLawSmoothing` `[fr-024]`: at 48 kHz from `reset()` at 120 Hz, `setControls({.crossoverHz = 250})`, then per
  `advanceControl()` `smoothedCrossoverHz()` equals a reference `OnePoleSmoother` configured `(20.0f, 1500.0f)`, snapped to 120, target 250, one
  `process()` per interval, within 1e-3 Hz for 200 intervals; `stageCrossoverHz() == kStageCrossoverMultiple * smoothedCrossoverHz()` exactly on
  every interval. After `reset()`, `smoothedCrossoverHz() == 250.0f` and `stageCrossoverHz() == 500.0f` exactly (C-4 snap).
- `BassStereoField_LifecycleBeforePrepare` `[fr-001][sc-011]`: a freshly constructed field: `reset()`, `setControls(Controls{})`,
  `advanceControl()` ×3 do not crash; `smoothedCrossoverHz() == 120.0f`; `motionValue()` finite. (The `processBlock` / law arms are added by
  T004 / T006.)

**Implementation (plan S3 verbatim, plus deviation 5):**
- The full public API of plan S3: every constant (add `static constexpr bool kCycleSyncFold = true; // plan S1 C-9 (A), ratified 2026-10-18`),
  `ImageMode`, `MotionSource`, `Controls`, `VoiceState` (its `reset()` leaves `handoffCount` alone), `ChannelOffsets`, `MotionOutput`, the deleted
  copy operations, every method signature. Put `kSideFloorGain` and `kWideOffsetScale` on their own lines between
  `// HARVEST-BEGIN wide-floor` and `// HARVEST-END wide-floor` (seeds 0.5f and 1.0f, plan C-11), each with the comment
  `// seed (plan S1 C-11), harvested in T015 (plan S9.1)`.
- Forward-declare `namespace detail { struct BassStereoFieldProbe; }` before the class; `friend struct detail::BassStereoFieldProbe;` inside it.
  Members `float sideFloorGain_ = kSideFloorGain;` and `float wideOffsetScale_ = kWideOffsetScale;` — the instance's per-interval side-gain target
  is `sideFloorGain_ * (1.0f - gravity)`; `sideGainForGravity` stays the static constexpr product law on the constant. `reset()` and `prepare()`
  never touch the two members.
- `setControls`, `isValidControls`, `setTempoBpm` (non-finite or ≤ 0 keeps the previous value, then clamp to [`kMinTempoSyncBPM`,
  `kMaxTempoSyncBPM`]), `setDriftSeed`, `prepare` (S3 steps 1–7; `OnePoleSmoother::configure` takes float rates: `static_cast<float>(fs / 32.0)`),
  `reset` (S3 steps 1–6, C-4: `crossover_.setCrossoverFrequency(2 · f_c)` then `crossover_.prepare(sampleRate_)` **only if `prepared_`**),
  `advanceControl` (S4.1 cache, S5 both sources advance every interval, change caches so the reused sources' setters run only on a change, S7.1
  targets with `modeRamp_.setTarget` only when the target changes), every observer.
- MOTION (S5): Drift τ = 30·(0.5/30)^rate passed as `setSmoothness((τ − 0.2f) / 29.8f)`; Cycle f = 0.05·20^rate via `setFrequency`, Sine,
  `lfo_.prepare(fs / 32)`, one `lfo_.process()` per interval; synced: `setTempoSync(true)`, `setNoteValue(note, mod)` and
  `setTempo(bpm_eff)`, bpm_eff = bpm / 2^j with the smallest j ≥ 0 (j ≤ 16) such that (bpm_eff / 60) / `getBeatsForNote(note, mod)` ≤
  `kCycleMaxHz` (skip the fold when `kCycleSyncFold` is false). `cycleFrequencyHz()` returns the rate the LFO is set to (free or folded synced).
  `m_` is the selected source's value; `motionOutput_.value_ = m_`.
- Temporary bodies for later tasks: `computePanVector` fills `out` with +0 (T004), `widthCurve` returns 0 (T004), `wideOffsets` returns {0, 0}
  (T005), `processBlock` returns immediately (T006). Each carries `// T00x` so the later task finds it.
- `bass_stereo_field_probe.h`: `namespace Krate::DSP::detail { struct BassStereoFieldProbe { static void setSideFloorGain(BassStereoField& f,
  float g) noexcept; static void setWideOffsetScale(BassStereoField& f, float k) noexcept; static float sideFloorGain(const BassStereoField& f)
  noexcept; static float wideOffsetScale(const BassStereoField& f) noexcept; }; }` — inline definitions that write / read the two members.
- **Verify:** Group 2 gate (`dsp_systems_tests`, `[profundum]` filter). All T003 cases green; the stubs make no other case red.

---

## Group 3 — the generation-domain width law (sequential, single task; edits the shared header and TU)

### T004 `computePanVector`, `widthCurve`, slew and handoff count (FR-010–FR-019, FR-030 vector side; FR-011 / C-15; SC-009(a′), SC-013(c), SC-018 law, SC-019 law, SC-021; plan S4.2, S4.3, C-5, C-6, C-8, C-15, C-17)

**Files:** `dsp/include/krate/dsp/systems/bass_stereo_field.h` (replace the T004 stubs), `dsp/tests/unit/systems/bass_stereo_field_test.cpp`
(append). Target `dsp_systems_tests`. Depends on T003.

**Tests first** (append; `[systems][profundum][stereo-field]` + the tag shown). Unless stated: 48 kHz, `prepare` then `setControls(c)` then
`reset()` (so the law crossover is snapped), then `advanceControl()` before each query; a fresh `VoiceState` per configuration; N = 64.
Analytic width for law-level W_n: W(p) = max(−120, 20·log₁₀|tan(π·p/4)|) dB (the constant-power identity of the bank's pan law,
`harmonic_oscillator_bank.h:647–659`).

- `BassStereoField_WidthCurveValues` `[fr-012][sc-021]` (C-5): crossover 120, Gravity 0, curve 0: `widthCurve(120) = 0.0301537`,
  `widthCurve(240) = 0.5868241` (±1e-5), `widthCurve(480) == 1.0f` and `widthCurve(1000) == 1.0f` exactly, `widthCurve(90) == 0.0f` exactly.
  Gravity 1, curve 0: `widthCurve(120) == 0.0f` exactly, `widthCurve(240) = 0.5` (±1e-5). Gravity 0, curve 0.5: `widthCurve(240) = 0.2171966`;
  Gravity 0.5, curve 0.5: `0.1816232`; Gravity 0, curve 1: `0.0222237` (±1e-5). Crossover 60 / 250, Gravity 0, curve 0: `widthCurve(60)` and
  `widthCurve(250)` = 0.0301537 (±1e-5).
- `BassStereoField_PanVectorShape` `[fr-010][fr-013][fr-014][fr-017][fr-018]`: `Controls{}` (Spread 0.5, Gravity 0.5, curve 0.5, crossover 120),
  f0 = E1, first (unprimed) query: `out[0]` has the bit pattern 0; out[1] = +0 (bit 0); expected k → value: 2 → −0.0000479, 3 → +0.006948,
  4 → −0.039453, 5 → +0.104321, 6 → −0.193907, 7 → +0.292827, 8 → −0.384166, 9 → +0.453827, 10 → −0.492837, 11 … 63 → +0.5 (k odd) / −0.5 (k even)
  (all ±2e-5; compute the expectations in the test in double from the C-5 formula, and assert the table above matches them within 1e-4 so a
  transcription slip shows); every `out[k]` for k ≥ 64 is bit 0; every |out[k]| ≤ 1; every non-zero entry has sign − for even k and + for odd k.
  `numPartials` 32 → entries k ≥ 32 are bit 0; `numPartials` 0 → all bit 0; `numPartials` 200 → clamped to 96 (entries up to 95 follow the law).
  f0 guard: a NaN, 0 or −5 f0 on a fresh `VoiceState` emits the bit-same vector as f0 = 55 Hz (`kDefaultF0Hz`) on a fresh state; after a valid E1
  query, a NaN query on that state emits the same vector as a second E1 query on a copy of the state.
- `BassStereoField_PanVectorMonotone` `[fr-012][sc-003]`: for Gravity {0, 0.5, 1} × crossover {60, 120, 250} × curve {0, 0.5, 1}:
  `widthCurve` over f = 10·2^(i/48) Hz up to 20 kHz is non-decreasing (`c[i+1] >= c[i] - 1e-7f`); `widthCurve(f) == 1.0f` for every
  f ≥ 4·crossover; raising Gravity (0 → 0.5 → 1) or curve (0 → 0.5 → 1) never raises `widthCurve(f)` (tolerance 1e-7). At E1 and C2, for every k,
  |p_k| over Spread i/10 (unprimed queries) is non-decreasing.
- `BassStereoField_SolidZeroVector` `[fr-011][fr-015][fr-051]` (C-15): (a) Start in SOLID at Spread 1: every entry of every query bit 0 (`std::bit_cast<std::uint32_t>(x) == 0`)
  from the first query, for 400 intervals. (b) SPREAD, Spread 1, E1 (so |p| = 1 at k ≥ 11), 10 settled intervals, then `setControls(mode Solid)`,
  `advanceControl()` and query, repeatedly: at query 333 after the switch at least one entry is non-zero; at query 334 and every query to 400,
  every entry is bit 0. (c) The same from Spread 1 to Spread 0 in SPREAD. Print the landing query index of (b) and (c).
- `BassStereoField_SlewBound` `[fr-015][fr-035]` (C-6): (a) E1 settled at Spread 0, step to Spread 1: for 400 intervals every
  |out_k − prev_k| ≤ `kMaxPanStepPerInterval` + 1e-7, no entry changes sign (sign ∈ {s_k, 0}), and by query 334 the vector is bit-equal to an
  unprimed query on a fresh state at the same field state. (b) A 48-semitone Drop: f0 from C3 to C3·2^(−4) linearly in semitones over 200
  intervals at Spread 1: every step ≤ Δ + 1e-7. (c) Cycle, rate 1, depth 1, Spread 1, 3000 intervals: every step ≤ Δ + 1e-7.
- `BassStereoField_HandoffCount` `[fr-016][sc-013]`: E1, `Controls{}` at depth 0: `handoffCount` is 1 after the first query and still 1 after 10 000
  intervals. Spread 0: 0 after 10 000 intervals. Cycle rate 1, depth 1, Spread 1: ≥ 9900 of 10 000. `VoiceState::reset()` leaves the count unchanged.
- `BassStereoField_ResetRetriggerOrder` `[sc-009]` (SC-009(a′), plan S10 order): E1, Spread 1 for 500 intervals; at interval 500 `setControls`
  Spread 0.2 + crossover 250, `advanceControl()`, `vs.reset()`, then the query: the emitted vector is bit-equal to an unprimed query on a fresh
  `VoiceState` at that field state; over the next 400 intervals no step exceeds Δ + 1e-7. **Positive control** (the old order: query first, then
  `vs.reset()`): the step from the note-on interval's vector to the next interval's is > Δ on at least one k.
- `BassStereoField_MotionInwardLaw` `[fr-030][fr-014]`: Cycle rate 1, depth 1, Spread 1, E1, 1500 intervals after the first: with P_k = |the
  depth-0 unprimed target|, every |p_k(t)| ≤ P_k + 1e-6; no sign change; `out[0]` bit 0 throughout; every k with P_k == 0 is bit 0 throughout.
- `BassStereoField_H1CentredLaw` `[sc-018][fr-013][fr-061]`: every mode × Spread {0, 0.5, 1} × Gravity {0, 1} × crossover {60, 250} × curve {0, 1} ×
  (depth 0 | depth 1 Drift | depth 1 Cycle) × notes E0, E1, C2, E2, C3, C4: `out[0]` has the bit pattern 0 on each of 50 intervals.
- `BassStereoField_PanOnlyDirectionLaw` `[sc-019][fr-014]`: every mode × Spread {0.5, 1} × (depth 0 | 1 Drift | 1 Cycle) × E1 and C2, 90 000
  intervals (60 s): for every interval and every k with a non-zero entry, the sign bit is set iff k is even. Count the checked (interval, k) pairs:
  > 0 per configuration in SPREAD and WIDE, == 0 in SOLID. The set {k : out_k ≠ 0, sign} is identical across a `reset()`, across two `VoiceState`
  instances, and across Drift seeds 0xB17E and 0x1234 at depth 0.
- `BassStereoField_CurveContinuous` `[sc-021]`: curve i/10 (i = 0 … 10) × crossover {60, 120, 250} × Gravity {0, 1}: `isValidControls`, every entry
  finite and |p| ≤ 1; print the largest adjacent-step change of W_k over the 11 curve steps per (crossover, Gravity). Pairs — single-control
  (Spread 0 → 1; Gravity 0 → 1; crossover 60 → 250; curve 0 → 1; the others at `Controls{}`) and same-direction ((Spread 0, Gravity 1,
  crossover 250, curve 1) → (Spread 1, Gravity 0, crossover 60, curve 0)): at t ∈ {0.25, 0.5, 0.75} the interpolated `Controls` is valid and for
  every k at E1 and C2, W_k(t) ∈ [min(W_k(0), W_k(1)) − 0.5, max(W_k(0), W_k(1)) + 0.5] dB.
- `BassStereoField_CurveInterpolationMixedPairs` `[sc-021]` (C-17): `constexpr bool kGateMixedInterpolationPairs = false; // plan S1 C-17, ruled 2026-10-18:
  mixed pairs recorded, not bounded (true = spec bar as written)`. Mixed pairs: (Spread 0, crossover 60) → (Spread 1, crossover 250) (the plan's counter-example); (Spread 1,
  Gravity 1) → (Spread 0.2, Gravity 0); (curve 0, crossover 250) → (curve 1, crossover 60). Always asserted: validity, finite, |p| ≤ 1, and the
  W_k path printed per 0.05 of t with its largest step. Always printed: each pair's excursion beyond the endpoints (dB). If the switch is true,
  the SC-021 bar of the previous case is REQUIREd on these pairs too (ruled false: printed only).

**Implementation (plan S4.2, S4.3):** `computePanVector(VoiceState& state, float f0Hz, std::array<float, kMaxPartials>& out, int numPartials =
kDefaultPartials) const noexcept` — clamp `numPartials` to [0, kMaxPartials]; the f0 guard (`detail::isFinite(f0Hz) && f0Hz > 0` else
`state.lastF0Hz`; store a valid one); one `std::log2(f0)`; for k = 1 … N−1: x, c, s as S4.2 using `log2Harmonic_` (filled in the constructor),
`uLow_`, `invSpan_`, `gamma_`, `magnitudeScale_`; target per rule 4 (+0 for zero); `out[0] = 0.0f` and entries k ≥ N are 0; S4.3 slew
(unprimed lands; else step by `std::copysign(Δ, d)` or land when |d| ≤ Δ), bit-compare against `state.previous` for `handoffCount`, then store
`previous` and `lastCrossoverHz = fcS_`. `widthCurve(f)`: the same c for an arbitrary frequency, 0 for a non-finite or non-positive f. Neither
method writes any member of the field.
- **Verify:** Group 3 gate. Expected red until ruled: `BassStereoField_CurveInterpolationMixedPairs` (P-3). Everything else green.

---

## Group 4 — WIDE offsets (sequential, single task; edits the shared header and TU)

### T005 `wideOffsets` and `wideCeiling` (FR-040–FR-044; SC-017; plan S6, C-10)

**Files:** `dsp/include/krate/dsp/systems/bass_stereo_field.h` (replace the T005 stub), `dsp/tests/unit/systems/bass_stereo_field_test.cpp`
(append). Target `dsp_systems_tests`. Depends on T004.

**Tests first:**
- `BassStereoField_WideOffsetCeilings` `[sc-017][fr-041][fr-042]`: `wideCeiling(a)` is `BassCharacterStage::kMaxChannelOffset` (0.15f) for Tube,
  Fold, Bite, Crush and exactly 0.10f for Rectify, Fracture. For each algorithm × Spread i/10 in WIDE (after `advanceControl`): `o =
  wideOffsets(a)`; `std::abs(o.amount) <= wideCeiling(a)` and `std::abs(o.tone) <= wideCeiling(a)` (exact float compares); `o.amount` equals
  `wideCeiling(a) * spread * kWideOffsetScale` within 1e-6; `o.tone == -o.amount` (C-10); at Spread 0 both have the bit pattern 0. SOLID and SPREAD
  at every Spread: both bit 0. Every ordered algorithm pair (36): the offsets returned after the switch are the new algorithm's (so ≤ 0.10 for
  Rectify / Fracture). **Latched:** `setControls(mode Wide)` without `advanceControl()` still returns the previous mode's offsets.
- `BassStereoField_WideVectorEqualsSpread` `[fr-044][sc-017]`: two fields identical except `mode` (Spread vs Wide), Drift depth 1 rate 1 and a
  Spread ramp 0 → 1 over the first 1500 intervals, E1, 3000 intervals, each with its own `VoiceState`: the emitted vectors are `memcmp`-equal on
  every interval.

**Implementation (S6):** `wideCeiling` static; `wideOffsets(a)` returns {0, 0} unless `applied_.mode == Wide` and `applied_.spread > 0`, else
`K = wideCeiling(a) * applied_.spread * wideOffsetScale_` and `{.amount = K, .tone = -K}`. `const`, pure, reads only applied state.
**G7-2 (ruled 2026-10-18, applied by the main loop):** K is further multiplied by `wideEngage_` (0 … 1), which `advanceControl()` slews toward
1 in WIDE and toward 0 otherwise by `kWideEngageStep = kMaxPanStepPerInterval` per interval with exact landing (≤ 334 intervals); `reset()`
snaps it to the applied mode; `wideOffsets` returns {0, 0} while it is 0 (no −0). The "Latched" arm of `BassStereoField_WideOffsetCeilings` now
checks: one interval after entering WIDE the offsets are positive and ≤ ceiling · K_wide · kWideEngageStep (+1e-6); after 334 intervals they
are bit-equal to ceiling · K_wide; one interval after setting SOLID they are ceiling · K_wide · (1 − kWideEngageStep) within 1e-6; after 334
intervals both are bit 0.
- **Verify:** Group 4 gate.

---

## Group 5 — the post-sum safety stage (sequential, single task; edits the shared header and TU)

### T006 `processBlock`, the stage gates, determinism and RT safety (FR-001, FR-020–FR-028, FR-050–FR-053, FR-062, FR-071, FR-074; SC-004(b)(c), SC-005 gates, SC-009(c), SC-011, SC-012, SC-020 stage; plan S7, S8, C-1, C-2, C-3, C-4, C-18, R-11)

**Files:** `dsp/include/krate/dsp/systems/bass_stereo_field.h` (replace the T006 stub), `dsp/tests/unit/systems/bass_stereo_field_test.cpp`
(append; add `#include "mass_dynamics_test_helpers.h"` for `MassDynamicsTest::gridDriver` / `anchorControlsFor`, `#include
<allocation_detector.h>`). Target `dsp_systems_tests`. Depends on T005.

Fixtures used below: **noise** = seeded `std::mt19937{0x5EED}`, independent uniform [−0.5, 0.5] on L and R; **anchor** = an
`AnchorOscillator` (prepared at fs, Reset retrigger, `MassDynamicsTest::anchorControlsFor(kDefaultLevel)`, `setCoreH1Amplitude(0.25f)`,
`noteOn(f0)`) rendered into L and R; each case first asserts its input is what it says (anchor: `ProfundumTest::requireLREqual`). The "settings
grid" is mode {Solid, Spread, Wide} × Gravity {0, 0.5, 1} × crossover {60, 120, 250} × Spread {0, 1} × (depth 0 | 1 Drift | 1 Cycle).
Per-sample probes call `processBlock(L + i, R + i, 1)` and read observers after each call (the loop has no internal grid, so this equals a block
call). Drive `advanceControl()` at every multiple of 32 with `MassDynamicsTest::gridDriver`.

**Tests first:**
- `BassStereoField_SafetyStageGates` `[sc-005][fr-001][fr-022]` (C-1 numeric bound, always gated): `latencySamples() == 0` (static_assert plus a
  runtime check in each mode). Noise, 1 s, each mode × Gravity {0, 0.5, 1} × crossover {60, 120, 250}, host blocks 512: per sample, in double,
  |(L_out + R_out)/2 − (L_in + R_in)/2| ≤ 2⁻²³·(|M_in| + |S'_out|), with M_in = (L_in + R_in)/2 and S'_out = (L_out − R_out)/2. Print the largest
  ratio of error to bound.
- `BassStereoField_SafetyStageMidBitExact` `[sc-005][fr-025]` (C-1 spec bar): `constexpr bool kGateMidBitExact = false; // plan S1 C-1, ruled 2026-10-18: numeric bound gated (true = spec bar as written)`.
  The same renders: count samples where `fl((L_out + R_out) * 0.5f)` and `fl((L_in + R_in) * 0.5f)` differ in bits; print the count and the
  largest |Δ|. If the switch is true, `REQUIRE(count == 0)` (ruled false: count printed); SOLID once settled is bit-exact either way and is REQUIREd
  unconditionally from the sample after `modeFade()` first reads 0.
- `BassStereoField_StageLrInLrOut` `[sc-004][fr-025]` (SC-004(b), from reset): the settings grid, an L == R input (the sum of sines at 41.2, 123.6
  and 400 Hz, 0.2 each), 1 s from `reset()`, blocks 512: output L == R (`requireLREqual`) and `memcmp`-equal to the input.
- `BassStereoField_StageLrInLrOutDirtyState` `[sc-004][fr-025]` (C-2 spec bar): `constexpr bool kGateLrBitExactAnyState = false; // plan S1 C-2,
  ruled 2026-10-18: decay recorded (true = spec bar as written)`. SPREAD, Gravity 0, crossover {60, 120, 250}: 0.5 s noise, then 1 s of the L == R input. Record, per crossover, the first sample
  index after the switch from which every output pair is bit-equal; write `evidence/side-decay-record.csv` (crossover, samples, ms). If the
  switch is true, REQUIRE bit-equality from the first L == R sample (ruled false: recorded). Always gated (the recommended reading): the same render
  with a switch to SOLID at the first L == R sample is L == R bit-identical from the first sample with `modeFade() == 0` + 1.
- `BassStereoField_AnchorBitIdentical` `[sc-004][fr-062]` (SC-004(c)): the anchor at E0, E1, E2, C3, 1 s, into the stage from `reset()` over the
  settings grid: output bit-equal to the input and L == R. After a SOLID crossfade (0.5 s noise in SPREAD, switch to SOLID, then the anchor from
  the crossfade's last sample + 1): L == R and output == input bit-for-bit from there.
- `BassStereoField_AnchorBitIdenticalDirtyState` `[sc-004][fr-062]` (C-2 spec bar): 0.5 s noise in SPREAD then the anchor without a mode switch;
  gated by `kGateLrBitExactAnyState` exactly like the dirty-state case above (ruled false: recorded); the decay length is printed.
- `BassStereoField_SolidForcesZeroSide` `[fr-051][fr-052][sc-004]` (R-11): at 44.1, 48 and 96 kHz, noise in SPREAD for 0.5 s, switch to SOLID at a
  grid point: `modeFade()` reaches exactly 0.0f; the crossfade length (from the switch's first sample to the first sample with `modeFade() ==
  0.0f`) is within ±1 sample of round(0.010·fs); from that sample + 1 every output pair is bit-equal and equals `fl((l + r) * 0.5f)` of the
  input; `sideIdle()` is true from the crossfade's end. Print whether the pair is already equal at the crossfade's last sample. Leaving SOLID:
  `modeFade()` rises to exactly 1.0f within round(0.010·fs) ± 1 samples and `sideIdle()` is false.
- `BassStereoField_GravityAttenuation` `[sc-020][sc-006][fr-021]` (stage-level positive controls): side-only tones (L = x, R = −x, amplitude 0.25) at
  0.95·f_c and √(20·f_c) Hz, crossover {60, 120, 250}, 2 s, measured over the last 1 s: at Gravity 1 the side RMS out / in ≤ −24 dB; at Gravity 0
  it is < 0 dB (strictly attenuated); over Gravity i/10 the attenuation is non-decreasing (0.01 dB tolerance). Print all readings beside the
  analytic LR4 values (−26.30 dB at 0.95·f_c, model S12.1). After a Gravity 0 → 1 step, `currentSideGain()` has the bit pattern 0 within 4800
  samples at 48 kHz (10 × `kSideGainSmoothMs`).
- `BassStereoField_ContinuousControlsNoCrossfade` `[fr-053]`: in SPREAD and in WIDE, settled, noise input: step Spread, Gravity, crossover, curve,
  motionDepth, motionRate and motionSource, each alone and then all at once, at grid points over a 2 s render: `modeFade() == 1.0f` (bit) on every
  sample (per-sample probe).
- `BassStereoField_CrossoverAgreement` `[sc-009][fr-024]` (SC-009(c), C-18): (a) after a 60 → 250 → 60 Hz step pair, from one interval after each
  step: `stageCrossoverHz()` equals `kStageCrossoverMultiple * smoothedCrossoverHz()` within 0.1 Hz (exact by construction) and
  `VoiceState::lastCrossoverHz` of a query in that interval equals `smoothedCrossoverHz()` bit-for-bit. (b) **Corner by computation:** record the
  per-interval handed sequence; drive a real `OnePoleSmoother` configured `(kStageCrossoverSmoothMs, fs)` and snapped to the first handed value,
  one `process()` per sample, `setTarget` at each interval start; apply the `Efficient` rule of `crossover_filter.h:306–311` per sample
  (lastCoeff = smoothed when |smoothed − lastCoeff| ≥ 0.1). At 44.1, 48, 88.2, 96 and 192 kHz, on the full-range step and on a slow sweep (the law
  crossover moved by 2.5e-5 Hz per interval for 10 s, i.e. the stage target by 5e-5 Hz per interval): after each interval |smoothed − handed| ≤
  0.1 Hz, and |lastCoeff − handed| ≤ residual + 0.1 Hz + 1e-4 at every interval end. Print the worst per rate, and beside it, on the slow sweep,
  the same simulation with the HighAccuracy rule (`:298–303`) and its unbounded drift.
- `BassStereoField_Deterministic` `[sc-012][fr-074]`: a field-only grid render, 4 s of noise, with a mode switch SPREAD → WIDE → SOLID → SPREAD
  at 1, 2, 3 s, a Spread ramp 0 → 1 over 0–1 s and MOTION depth 1 (one run per source), and a `VoiceState` queried every interval at E1: at host
  blocks {1, 17, 32, 64, 512} the outputs and the emitted vectors are `memcmp`-equal to the block-512 run; two runs from `reset()` are
  `memcmp`-equal, including after a first render that moved the crossover 60 → 250 (C-4); seeds 0xB17E and 0x1234 (Drift) give different outputs.
- `BassStereoField_NoAllocation` `[sc-011][fr-071]`: prepared at 48 kHz and warmed by one render; inside one `AllocationScope`: `setControls`,
  `advanceControl`, `computePanVector`, `wideOffsets`, `processBlock` with 1, 32 and 4096 samples, `reset()`, a switch through every mode, a
  source switch, `setTempoBpm`, `setDriftSeed`; `AllocationDetector::instance().getAllocationCount() == 0` read before the scope closes.
  **Fresh-field arm:** on a never-prepared field, inside a scope: `reset()`, `advanceControl()`, `computePanVector`, `processBlock(L, R, 64)` →
  0 allocations, every output finite and equal to M ± 0 of the input (the unprepared crossover returns {0, 0}); a following `prepare` + `reset`
  render is `memcmp`-equal to one from a field that never saw the early calls.
- `BassStereoField_ProcessEdgeCases` `[fr-001][fr-072]` (leave T003's `BassStereoField_LifecycleBeforePrepare` as it is):
  `processBlock(nullptr, R, 64)` and `processBlock(L, R, 0)` leave both buffers untouched;
  `right == left` with finite input returns the input unchanged and with a NaN sample writes 0.0f there.

**Implementation (plan S7.3 verbatim):** the per-sample loop with the FR-072 guard and output mask, the inline M/S formulas
(`midside_processor.h:200–201, 225–226`, cited in a comment: C-3), `k = modeRamp_.process()`, `g = sideGainSm_.process()`, the idle branch
`Sp = 0`, otherwise `crossover_.process(S)` and `Sp = k * (g * lo + hi)`, decode, the idle transition (`k == 0.0f && modeRamp_.getTarget() ==
0.0f` → `sideIdle_ = true; crossover_.reset();`), and the `right == left` path (both smoothers advanced, `badL ? 0 : lIn` written). The
crossover is set up per S3 step 3 (`setSmoothingTime(kStageCrossoverSmoothMs)`, `setTrackingMode(TrackingMode::Efficient)`). No scratch buffers.
- **Verify:** Group 5 gate. Expected red until ruled: `BassStereoField_SafetyStageMidBitExact` (P-1), `BassStereoField_StageLrInLrOutDirtyState` and
  `BassStereoField_AnchorBitIdenticalDirtyState` (P-2). Everything else green.

---
## Group 6 — validation-chain helper and the non-finite TU (parallel; disjoint new files)

### T007 [P] `bass_stereo_field_test_helpers.h` (plan S10, C-12, C-16; deviations 7, 8) and its self-tests

**Files:** `dsp/tests/unit/systems/bass_stereo_field_test_helpers.h` (new), `dsp/tests/unit/systems/bass_stereo_field_helpers_test.cpp` (fill the
stub). Target `dsp_systems_tests`. Depends on T006.

The header opens `namespace Krate::DSP::StereoFieldTest`, includes **read-only** `mass_dynamics_test_helpers.h`, `profundum_core_test_helpers.h`,
`shape_character_test_helpers.h`, `../processors/shape_character_metric_helpers.h`, `../processors/harmonic_fold_test_helpers.h`,
`bass_stereo_field_probe.h`, `<krate/dsp/systems/bass_stereo_field.h>`, `<krate/dsp/systems/shape_morph.h>` and
`<krate/dsp/systems/bass_character_stage.h>`, and qualifies every call into the other helper namespaces (rule 6).

**Tests first** (`bass_stereo_field_helpers_test.cpp`, `[systems][profundum][stereo-field][helpers]`):
- `BassStereoField_HelpersSelfTest`:
  - `bandReading` of a hard-panned in-phase tone (L = sin, R = 0, 300 Hz, band [200, 400)): Q = −3.0103 dB (±0.001); of an anti-phase pair
    (L = x, R = −x): Q ≤ −100 dB (floored at −120) and ρ = −1 (±1e-9); of L == R: Q = 0 (±1e-9) and ρ = 1 (±1e-12); of L = x, R = 0.5·x: ρ = 1
    (±1e-9) and Q = −0.4576 dB (±0.001).
  - **Mask equivalence** (deviation 8): a two-tone fixture (60 Hz L-only + 300 Hz on both, R phase-shifted 0.3 rad): `bandReading`'s energies over
    [0, 120) equal the time-domain energies of the zero-phase-masked signals (mask by `ProfundumTest::fftDouble`, zero the bins, then
    `MassDynamicsTest::inverseFftDouble`) within 1e-9 relative, and ρ within 1e-9.
  - The band mask passes a 100 Hz tone in [50, 200) within 0.01 dB and removes a 1 kHz tone from that band by ≥ 120 dB.
  - `widthDb` of a constant-power pan p = 0.5 rendered analytically reads 20·log₁₀|tan(π·0.5/4)| = −7.6555 dB (±0.01).
  - `renderPanReference` with a constant vector gives held == interpolated (max |Δ| ≤ 1e-12).
  - `goertzelComplex` recovers amplitude 0.3 (±1e-6) and phase 0.7 rad (±1e-6) of a 41.2 Hz tone over whole periods.
- `BassStereoField_ChainMatchesPhase6` (C-12): `FieldChainConfig` with `bypassField = true`, `shapeOn = false`, no algorithm, no `foldAmount`
  (Fold from the law), default excitation, `chain = {.base = kSawAnchor, .f0 = E1, .fs = 48000, .seconds = 2.0}`: `renderFieldChain(cfg).outL/outR`
  are `memcmp`-equal to `MassDynamicsTest::renderChain(cfg.chain).outL/outR`, and so are `coreL/R`, `foldL/R`, `dynL/R`.
- `BassStereoField_FieldChainSmoke`: the dirty arm (Tube) with the Anchor in SPREAD at `Controls{}`, E1, 2 s: every tap finite; `stageOutL/R` differ
  from `dynL/R` (the stage is in the path); `vectorPerInterval.size()` equals the number of grid points; in SOLID from reset `stageOutL == stageOutR`
  bit-for-bit.

**Implementation (plan S10 exactly, plus):**
- `FieldChainConfig` and `FieldChainTaps` as plan S10, plus `std::function<double(std::size_t sample)> f0At` (empty → `chain.f0`; per-sample f0
  is passed to `core.processBlock(..., f0PerSample)` and the law query reads the core's `currentFrequency()`), `bool attachCharacter` implied by
  `algorithm.has_value()`, `float characterTone = 0.5f`, and `std::optional<float> sideFloorOverride` / `std::optional<float> wideScaleOverride`
  (applied to **both** field instances through `detail::BassStereoFieldProbe` after construction).
  Two more taps: `bool perSampleStage = false` (the stage is called one sample at a time and `std::vector<float> modeFadePerSample` records
  `modeFade()` after each sample; the output is identical, the loop has no grid) and `std::vector<float> h1DeliveredPerInterval`
  (`core.deliveredGains()[0]` read at each grid point after the core run that follows it; SC-018).
- `renderFieldChain`: the per-grid-point order of plan S10 steps 1–6 (controls → `perInterval` → `advanceControl`; Phase 6 targets via
  `MassDynamicsTest::chainTargets`; note-on and `vs.reset()` **before** the law query; `computePanVector` with `core.currentFrequency()`, or the
  note's f0 at and before the first note-on; `forceSubCentre` sets every entry with (k + 1)·f0 < `smoothedCrossoverHz()` of the generator field
  to +0; `generatorGravity` evaluates the vector on a second field identical but for Gravity; `core.setPartialPanOffsets`;
  `character.setChannelOffsets(o.amount, o.tone)` from `wideOffsets(alg)`). Per run: `core.processBlock` (tap `coreL/R`) → `shapeMorph` (if
  `shapeOn`) → `fold.processBlock` (tap `foldL/R`) → `character.processBlock` (if an algorithm; tap `characterL/R`) → + Anchor → the Phase 6 sum /
  pre-gain / MassDynamics (tap `dynL/R`) → `field.processBlock` unless `bypassField` (tap `stageOutL/R`) → DC ×2 → limiter (`outL/R`), using
  `MassDynamicsTest::makeChainDynamics`. Write the tail inline (copying `processChainTail`'s statements, `mass_dynamics_test_helpers.h:302–340`,
  with the stage inserted) — the Phase 6 header is never edited. With every Phase 7 stage off, the call sequence is `renderChain`'s exactly
  (core → Anchor → Fold → tail), so the C-12 self-test is bit-exact; call `core.setExcitation` only when `cfg.excitation` differs bitwise from
  `ProfundumCore::kDefaultExcitation`.
- `renderFieldPoly(f0s, common)`: one voice chain per f0 (core → ShapeMorph → Fold → character, own `VoiceState`), summed, + Anchor of the lowest
  voice, then the shared tail and one field stage (Edge case "POLY"; record only).
- Descriptor helpers (offline, double): `struct BandReading { double q; double rho; double sideToMidDb; double meanEnergy; bool valid; };`
  `bandReading(L, R, n, fs, fLo, fHi)` — one Hann-tapered window of the largest power of two ≤ n (≤ 2^18), the same taper on L and R, FFT,
  Parseval over the bins in [fLo, fHi): Q = Σ|L+R|²/4 / ((Σ|L|² + Σ|R|²)/2), ρ = Re ΣL·R̄ / √(Σ|L|²·Σ|R|²), side-to-mid = Σ|L−R|² / Σ|L+R|²
  in dB; `valid = meanEnergy ≥ 1e-10` (−100 dBFS); Q and side-to-mid floored at −120 dB. `windows(n, fs, settleSeconds, lenPow2, hop)` for moving
  renders. `octaveBands(fc)` = [fc·2^i, fc·2^(i+1)) up to 10 kHz (the last band truncated at 10 kHz); `thirdOctaves(20, fc)`.
  `widthDb(L, R, fs, f0, N)` = W_n from `ProfundumTest::renderedHarmonicPowers` of M and S (floored −120 dB). `goertzelComplex(x, L, fs, hz)`.
- `struct MonoSumCell` (per tap ∈ {core, character, dyn, stageOut, out}: the sub-band reading [0, f_c) and the octave readings, worst over
  windows) and `MonoSumCell evaluateMonoSumCell(const FieldChainTaps&, const FieldChainConfig&)`: depth 0 → one window over
  [`MassDynamicsTest::kSettleSeconds`, end); depth > 0 → windows of 2^17 samples (scaled to the rate: the power of two nearest 2.73 s) with hop
  2^17 from 1 s, and the worst window per reading. `bool monoSumCellPasses(const MonoSumCell&)`: |Q_sub| ≤ 1 dB at core, stageOut and out; every
  valid octave Q ≥ −3.11 dB at core, stageOut and out; ρ_sub ≥ 0.99 at core and stageOut. `std::string monoSumCsvLine(...)` prints every tap.
- `restage(const FieldChainTaps& t, const FieldChainConfig& cfg, float floorGain)` (deviation 7): a fresh field with the same `fieldAt` /
  `perInterval` sequence and `advanceControl` cadence and `sideFloorGain_ = floorGain` (probe) runs `processBlock` on copies of `dynL/R`, then fresh
  DC ×2 + limiter from `makeChainDynamics(cfg.chain)`; returns new `stageOutL/R` and `outL/R`. Self-test it in `BassStereoField_HelpersSelfTest`:
  `restage` at the field's own floor reproduces `stageOutL/R` and `outL/R` bit-for-bit.
- `wideAliasedDbfs(const std::vector<float>& y, double fs, double f0, BassCharacterStage::Algorithm a)`: Phase 5's measure
  (`bass_character_spectral_test.cpp:140–149`): `HarmonicFoldTest::aliasedPower` on the last `HarmonicFoldTest::aliasFftLength(fs, f)` samples,
  f = f0/2 for Fracture and f0 otherwise (`:998`).
- `struct LeverMove { double sideToMidDb; double rho; }` and `LeverMove staticLeverMove(const FieldChainTaps& a, const FieldChainTaps& b, fs, fc)`:
  the largest |Δ side-to-mid| (dB) and |Δρ| over the octave bands at `stageOutL/R`.
- `renderPanReference(fs, total, f0At, harmonics, vectorsPerInterval, PanReferenceMode::{Held, Interpolated})` (C-16): harmonics as (a_n, φ_n),
  fitted by `goertzelComplex` at n·f₀ over whole periods of a settled Spread 0 render; synthesised in double as a_n·sin(φ_n + 2π·n·∫f₀); Held
  applies each interval's vector for the whole interval, Interpolated interpolates p linearly between interval centres and evaluates cos / sin
  per sample. `PanReferenceMode` is nested in the namespace (0 hits in the sweep for `PanReference`).
- **Verify:** Group 6 gate.

### T008 [P] Non-finite safety (SC-014; FR-072)

**Files:** `dsp/tests/unit/systems/bass_stereo_field_nonfinite_test.cpp` (fill the stub; it is in the `-fno-fast-math` block). Target
`dsp_systems_tests`. Depends on T006. Tags `[systems][profundum][stereo-field][nonfinite][sc-014]`.

- `BassStereoField_NonFinite`: NaN, +Inf and −Inf built from bit patterns (`std::bit_cast<float>(0x7FC00000u)`, `0x7F800000u`, `0xFF800000u`)
  through a `volatile` (memory: fast-math NaN in tests). A 16-sample burst at 0.5 s into a 2 s noise render (SPREAD, Gravity 0.5, crossover 120,
  48 kHz, block 64) on L only, R only, and both; and a reference render with no burst.
  - every output sample is finite (`detail::isFinite`);
  - every output sample of a channel whose input sample was non-finite is exactly +0.0f (`std::bit_cast<std::uint32_t>(y) == 0`;
    −0.0f fails), including the one-channel-bad case;
  - the block after the burst is finite on both channels (state finite within one block);
  - from 100 ms after the burst's end, the output matches the reference within −60 dB (RMS of the difference over the next 0.5 s re the
    reference's RMS), per channel.
- `BassStereoField_NonFiniteControls`: a NaN / ±Inf in each float `Controls` field, and in `setTempoBpm`, keeps the previous value
  (`controls()` bit-equal; `cycleFrequencyHz()` unchanged) and the following render is bit-equal to a render that never saw the bad calls.
- **Verify:** Group 6 gate.

---

## Group 7 — the grid, chain, harvest and perf TUs (parallel; each task owns exactly one TU; no header edits)

Shared rules for this group: every render goes through `StereoFieldTest::renderFieldChain` (T007) unless the case says field-only; each
`[long]` case has the named `…Smoke` sibling (deviation 6); each case prints a CSV line per cell to stdout and to its evidence file. The
**harvest-dependent** per-push cases (`BassStereoField_MonoSumPerBandSmoke`, `BassStereoField_GravityFloorSmoke`, `BassStereoField_LeversAudibleSmoke`)
read the committed `kSideFloorGain` / `kWideOffsetScale`; with the seeds they may be red until T015 (expected red, P-4 only if they are).

### T009 [P] Mono sum and sub-band correlation (SC-001, SC-002, SC-020 rendered; FR-021, FR-077)

**Files:** `dsp/tests/unit/systems/bass_stereo_field_monosum_test.cpp`. Target `dsp_systems_tests`. Depends on T007.

- **Shards** `BassStereoField_MonoSumPerBand_<Mode>_<Arm>_<Note>` `[long][sc-001][sc-002]`, one TEST_CASE each, no SECTIONs: Mode ∈ {Solid, Spread,
  Wide}; Arm ∈ {CleanSaw, CleanGrowl, Dirty<Alg> ×6, Anchor<Alg> ×6} (14); Note ∈ {E0, E1, E2, C3}: 168 TEST_CASEs, one `BSF_MONOSUM_SHARD(Mode, Arm,
  Note)` macro line each over one shared body function (no hand-copied bodies). Each shard renders its cells: Spread {0, 0.5, 1} × Gravity {0, 0.5, 1} × crossover {60, 120, 250} × curve {0, 1} ×
  MOTION (depth 0 | depth 1 Drift | depth 1 Cycle), at rate 1 (162 cells). Depth 0 cells render `MassDynamicsTest::descriptorSeconds(fs)`; depth 1
  cells render 61 s. Clean arms: `shapeOn = false`, no algorithm, no Anchor, excitation off. Dirty: shape 1, Fold 100, the algorithm, no Anchor.
  Anchor: Dirty + Anchor. In WIDE the offsets come from `wideOffsets` (the field's own law).
  - **Gated per cell** (`monoSumCellPasses`): (a) |Q_sub| ≤ 1 dB at `stageOutL/R`, `outL/R` and `coreL/R`; (b) every valid octave band f_c … 10 kHz
    Q ≥ −3.11 dB at the same three taps; SC-002: ρ_sub ≥ 0.99 at `stageOutL/R` and `coreL/R`.
  - **Recorded per cell** (FR-077): the same readings at `characterL/R` and `dynL/R`, ρ at `outL/R`, the sub-band side-to-mid ratio; in WIDE dirty
    shards the per-cell difference of the `characterL/R` reading against the same cell in SPREAD (re-render the SPREAD cell inside the shard).
    Print the worst-Q and worst-ρ cell per shard. CSV `evidence/sc001-grid/<shard>.csv` (one line per cell and tap).
- `BassStereoField_MonoSumPerBandSmoke` `[sc-001][sc-002]` (per-push): two cells — SPREAD DirtyRectify E1 Spread 1 Gravity 0 crossover 120 curve 0
  depth 0, and WIDE AnchorFracture E0 Spread 1 Gravity 0 crossover 60 curve 1 depth 0 — gated as above (harvest-dependent).
- `BassStereoField_GravityFloor_<Arm>` `[long][sc-020]` (14 shards, Arm as above, SPREAD and WIDE inside the shard): at Gravity 0, crossover {60,
  120, 250}, Spread 1, every note: SC-001(a) and SC-002 as above, and A(B) (the SC-006 measure: side energy in [20 Hz, f_c) at `dynL/R` minus at
  `stageOutL/R`, on cells whose input band is valid, ≥ −100 dBFS) **> 0 dB**. Print the committed floor and the harvest commit from
  `evidence/harvest-wide-floor.txt`.
- `BassStereoField_GravityFloorSmoke` `[sc-020]`: DirtyRectify E1 crossover 120 WIDE, the same asserts.
- `BassStereoField_PolyMonoSumRecord` `[long][sc-001]` (record only, Edge case POLY): `renderFieldPoly` at {E1, E2, C3, A2} in SPREAD and WIDE
  (Tube): the SC-001 readings at the stage output, printed for Phase 9; no REQUIRE beyond finiteness.
- **Executor:** write the TU only. The shards are main-loop work (T019's lane). **Verify:** Group 7 gate (per-push smoke cases only).

### T010 [P] Width monotonicity, phase record, side attenuation, audibility, sample rates (SC-003, SC-005 record, SC-006, SC-010, SC-016; FR-012, FR-021–FR-024, FR-075, FR-076)

**Files:** `dsp/tests/unit/systems/bass_stereo_field_spectral_test.cpp`. Target `dsp_systems_tests`. Depends on T007.

- `BassStereoField_WidthMonotonic` `[long][sc-003]`: Gravity {0, 0.5, 1} × crossover {60, 120, 250} × curve {0, 1}, depth 0, E1 and C2 (N up to
  Nyquist), SPREAD and WIDE, clean core (`kSawAnchor`), readings at `coreL/R` and `stageOutL/R`: (a) W_n non-decreasing in n·f₀ (1e-3 dB tolerance,
  floored values) and the octave-band ρ non-increasing in band centre (1e-4); (b) over Spread i/10, W_n non-decreasing at fixed n (1e-3 dB) and the
  octave-band ρ non-increasing (1e-4); (c) Gravity 0 → 0.5 → 1 never raises W_n or an octave side-to-mid ratio (0.01 dB), and at n·f₀ ≥ 5·f_c the
  drop from Gravity 0 to 1 is ≤ 0.5 dB; (d) Spread 0: every W_n at −120 dB and `requireLREqual` on both taps. In WIDE the clean-core vector is
  SPREAD's (FR-044), so WIDE's W_n equal SPREAD's within 1e-6 dB. **WIDE dirty arm** (every algorithm, offsets at the ceilings = Spread 1 ·
  committed K_wide), at `stageOutL/R`: the octave-band side-to-mid ratio is non-decreasing in band centre and in Spread (11 steps), each within
  0.5 dB. CSV `evidence/sc003-width.csv`.
- `BassStereoField_WidthMonotonicSmoke` `[sc-003]`: E1, Gravity 0.5, crossover 120, curve 0, SPREAD, (a)–(d) on the clean core.
- `BassStereoField_SafetyStagePhase` `[long][sc-005]` (record; FR-023): field-only. For crossover {60, 120, 250} × Gravity {0, 1}: the impulse
  response of a pure-S input (L = δ, R = −δ) and of a pure-M input (L = R = δ), 2^16 samples; the phase of S_out/S_in and M_out/M_in from 20 Hz to
  10 kHz (1/12-octave points) and the group delay of S at f_s, printed beside S7.4's analytic values (all-pass (s² − √2 s + 1)/(s² + √2 s + 1) at
  g = 1, group delay 2√2/(2π f_s): 3.751 / 1.876 / 0.900 ms); Q_band per octave on seeded stereo noise with ρ = 0.5 (L = a, R = 0.5·a +
  0.866·b). CSV `evidence/sc005-phase.csv`. Gated here: finite; M_out/M_in magnitude within 1e-6 of 1 at every point.
- `BassStereoField_SafetyStageAttenuation` `[long][sc-006]`: fixtures (i) the WIDE dirty arm (every algorithm, offsets at the ceilings, Spread 1);
  (ii) the core with excitation mix 1, drive 1, Spread 1 (`ProfundumCore::ExcitationControls{.mix = 1.0f, .drive = 1.0f, .asymmetry = 0.0f}`);
  (iii) field-only side tones at √(20·f_c) and 0.95·f_c. (i) and (ii) run with `forceSubCentre = true` and `generatorGravity = 1.0f`. Cells:
  {E0, E1, E2, C3} × f_c {60, 120, 250} × arm, the stage at Gravity 1. **Validity:** D_side(B) at `dynL/R` < −100 dBFS → print "no
  distortion-born content in B", count, do not gate (the same per third octave); print each cell's validity beside the expectation (content
  expected at E0 and E1 for every f_c, E2 for f_c ∈ {120, 250}, C3 for 250; empty: E2 at 60, C3 at 60 and 120) and flag a mismatch.
  **Non-vacuity:** REQUIRE at least one valid gated cell per crossover for (i) and for (ii). **Gate:** every valid cell A(B) ≥ 24 dB on B = [20 Hz,
  f_c) and on each valid third octave in B; every (iii) tone ≥ 24 dB at every crossover. **Monotone:** A(B) over Gravity i/10 (input fixed by
  `generatorGravity`) non-decreasing (0.01 dB). Print the Gravity 0 readings. CSV `evidence/sc006-attenuation.csv`. If a reading lands within
  0.1 dB of 24, print the per-bin table (R-3).
- `BassStereoField_SafetyStageAttenuationSmoke` `[sc-006]`: (i) Tube E1 f_c 120 and (iii) at f_c 60.
- `BassStereoField_LeversAudible` `[long][sc-010]` (FR-076): SPREAD, `kSawAnchor`, E1 and C3; each lever alone between its extremes from
  `Controls{}`: Spread 0 ↔ 1, Gravity 0 ↔ 1, crossover 60 ↔ 250, curve 0 ↔ 1 (clean core), WIDE vs SPREAD (dirty arm, Tube): `staticLeverMove` ≥ 3 dB
  side-to-mid **or** ≥ 0.1 ρ. MOTION depth 0 → 1 (Cycle rate 0.5, Spread 1): the standard deviation over time of W_oct(t) — the short-window
  side-to-mid ratio of the octave band with the largest static width, 4096-sample BH7 window, hop 1024 at 48 kHz (scaled with fs), 60 s — rises by
  ≥ 1 dB. MOTION rate 0 ↔ 1: the dominant modulation frequency of W_oct(t) (Cycle) and the fitted 1/e autocorrelation time (Drift) change by a ratio
  ≥ 4. One table of every lever printed.
- `BassStereoField_LeversAudibleSmoke` `[sc-010]`: Spread and WIDE-vs-SPREAD at E1 only (harvest-dependent for WIDE).
- `BassStereoField_SampleRates` `[long][sc-016]` (FR-075): at 44.1, 48, 88.2, 96 and 192 kHz: output finite and |y| ≤ 4; E1 clean-core W_n per
  harmonic within 0.25 dB of the 48 kHz value (`Controls{}`, Spread 1); A(B) of fixture (i) Tube E1 f_c 120 within 1 dB of 48 kHz.
- `BassStereoField_SampleRatesSmoke` `[sc-016]`: 44.1 and 96 kHz, E1 W_n only.
- `BassStereoField_SampleRatesReprepare` `[sc-016]` (per-push; deviation 10): field-only noise render after `prepare(48k) → prepare(96k) →
  prepare(48k)` (+ `reset()`) is `memcmp`-equal to a fresh 48 kHz field's render, with MOTION on (each source) and a crossover step.
- **Executor:** write the TU only. **Verify:** Group 7 gate (per-push cases).

### T011 [P] MOTION bounds and the zipper references (SC-008, SC-009(a); FR-015, FR-030–FR-036; plan C-9, C-16)

**Files:** `dsp/tests/unit/systems/bass_stereo_field_motion_test.cpp`. Target `dsp_systems_tests`. Depends on T007.

- `BassStereoField_MotionBounded` `[long][sc-008]` (law-level, no audio): `advanceControl` + `computePanVector` per interval for 60 s (90 000
  intervals at 48 kHz), μ ∈ {0.5, 1} × Spread {0.5, 1} × source {Drift, Cycle} × rate {0, 0.5, 1}, E1. With P_k the depth-0 static magnitude and
  p̄_k = s_k·P_k·(1 − 0.5·μ): **excursion** max_t |p_k(t) − p̄_k| ≤ 0.5·μ·P_k + 1e-6 and max_t |p_k(t)| ≤ P_k + 1e-6; Cycle non-vacuity: max excursion ≥
  0.99·0.5·μ·P_k on every k with P_k > 0.01. **Cycle rate:** BH7 spectrum of p_k(t) (control rate, zero-padded to 2^17): peak ≤ 1 Hz and within one
  bin of `cycleFrequencyHz()`. **Drift τ:** `driftTauSeconds()` ∈ [0.5, 30] at rate {0, 0.25, 0.5, 0.75, 1}; the fitted 1/e autocorrelation time of
  p_k(t) within a factor 2 of τ where τ ≤ 6 s (printed above). **Fixed partials:** h1 and every c_k = 0 partial bit-equal throughout. **No sign
  change** for any k with P_k > 0. **Inward motion:** mean_t |p_k(t)| printed for μ {0, 0.5, 1}; for Cycle within 1e-3 of P_k·(1 − 0.5·μ).
  **Sync** (C-9 (A)): 120 and 200 BPM, SixtyFourth + Triplet (the fastest note value): spectral peak within one bin of `cycleFrequencyHz()`
  (0.75 / 0.625 Hz) and ≤ 1 Hz; a NaN or −1 tempo keeps the previous one. CSV `evidence/sc008-motion.csv`.
- `BassStereoField_MotionBoundedSmoke` `[sc-008]`: 10 s, μ 1, Spread 1, Cycle rate 1 and Drift rate 1: excursion, no sign change, fixed partials.
- `BassStereoField_NoZipper` `[long][sc-009]` (SC-009(a), C-16): clean core `kSawAnchor` 64 partials at 48 kHz through `renderFieldChain` (no
  shape, no character, no Anchor, `bypassField = true` so only the vector is handed), reading `coreL/R` and `vectorPerInterval`. Arms: (1) Spread
  sweep 0 → 1 at 1 s and 1 → 0 at 3 s, static E1; (2) MOTION Cycle rate 1 depth 1 at Spread 1, static E1; (3) a 48-semitone Drop C3 → C3·2^(−4)
  over 1 s (`f0At`), Spread 1. Fit (a_n, φ_n) from a settled Spread 0 render of the same note; build held and interpolated references
  (`renderPanReference`); residual = held − interpolated per channel. (1)(2): `ProfundumTest::sidebandPower(residual, fs, f0, 32, 0, 0.0, 0.0,
  productTotal)` ≤ −60 dB on L and R (size the render to the frame `sidebandPower` reads, `profundum_core_test_helpers.h:323`). (3): the
  broadband residual power re the product's total ≤ −60 dB per channel, and `ShapeCharacterTest::differentialClicks` of the product render
  against the held reference reports `counted == 0`. **Validity:** a static E1 note at Spread 1: product − held ≤ −60 dB re total on each channel.
  Print every reading.
- `BassStereoField_NoZipperSmoke` `[sc-009]`: the validity arm and arm (1) at 2 s.
- **Executor:** write the TU only. **Verify:** Group 7 gate (per-push cases).

### T012 [P] Chain: SOLID bit-identity, WIDE aliasing, transitions, h1 and pan-only rendered (SC-004(a1)(a2)(a3), SC-007, SC-009(b), SC-018, SC-019 rendered; FR-051, FR-052, FR-061, FR-078)

**Files:** `dsp/tests/unit/systems/bass_stereo_field_chain_test.cpp`. Target `dsp_systems_tests`. Depends on T007.

- `BassStereoField_SolidBitIdentical` `[sc-004]` (per-push; deviation 10): the full chain — `kSawAnchor` core with excitation mix 0.5 / drive 0.5,
  shape 1, Fold 100, the character stage, Anchor, MassDynamics, stage, DC, limiter. **(a1)** From `reset()` in SOLID: every setting of the SC-001
  control grid (Spread {0, .5, 1} × Gravity {0, .5, 1} × crossover {60, 120, 250} × curve {0, 1} × (depth 0 | 1 Drift | 1 Cycle)) × every algorithm ×
  host blocks {1, 17, 32, 512}, 0.1 s each at E1: `stageOutL == stageOutR` and `outL == outR` (`requireLREqual`) from sample 0. Print the wall
  clock; if the case exceeds 60 s in the gate log, T016 splits it by algorithm into six per-push TEST_CASEs (never `[long]`). **(a2)** Renders
  started in SPREAD and in WIDE (offsets at the ceilings, so every upstream stage runs L ≠ R), at Spread 1 / Gravity {0, 1} / crossover {60, 250} ×
  every algorithm × blocks {64, 512}, 1.5 s at E1, switched into SOLID at the grid point at 0.5 s: `stageOutL/R` bit-identical from the
  crossfade's last sample + 1 (the crossfade end = the first sample with `modeFadePerSample == 0.0f`, from a render with
  `perSampleStage = true`, T007). **(a3)** The same renders extended to 2.5 s: at `outL/R`, max |L − R| ≤
  −120 dBFS from 500 ms after the crossfade end; print the first time it crosses −120 dBFS. CSV `evidence/sc004-a3-convergence.csv`.
- `BassStereoField_WideAliasing` `[long][sc-007]` (FR-078): the WIDE dirty arm, every algorithm, Amount 1, offsets at the ceilings, Fold 100, E0,
  E1, E2, at 44.1, 48 and 96 kHz: `wideAliasedDbfs` of `characterL` and of `characterR` ≤ −80 dBFS.
- `BassStereoField_WideAliasingSmoke` `[sc-007]`: Rectify and Fracture at E1, 48 kHz.
- `BassStereoField_TransitionsClickFree` `[sc-009]` (SC-009(b), per-push; **RULED 2026-10-18, G7-1: the long-ramp form — rewrite this case; keep
  the rest of the TU, including the already-fixed `bsfControlGridPoint` positive-control placement**): `kSawAnchor` Anchor arm (Tube) at E0, E2,
  C3, host blocks {64, 512}, 3 s; steps at 1 s and 2 s: SOLID → SPREAD → WIDE, WIDE → SPREAD → SOLID, Gravity 0 → 1 → 0, crossover 60 → 250 → 60.
  For each step and each of `outL`, `outR`:
  **(i) GATE, sample-delta bound:** every |Δy| from the step sample to the end of its settle (the later of the FR-015 slew landing at 334
  intervals, the 20 ms f_c smoother and the 10 ms mode fade, plus one period of f₀) ≤ **1.1 ×** the maximum steady-state |Δy| over a grid of
  ≥ 21 settings along the traversed path, endpoints included: for SOLID ↔ SPREAD the held setting is SPREAD at Spread = s_i (the slewed vector
  is the SPREAD vector scaled); for SPREAD ↔ WIDE it is WIDE with the offset scale at K_wide · t_i through `detail::BassStereoFieldProbe`
  (the vector is unchanged, FR-044); for Gravity and crossover steps it is the control at 21 points; each steady render is the same note from
  `reset()`, measured after its own settle (≥ 0.5 s), max |Δy| taken over ≥ 1 s. Print the ratio per cell. **(ii) RECORDED:** the
  two-reference differential count (`ShapeCharacterTest::differentialClicks(transition, refStart, refEnd, n, fs, f0)`, references held at
  the start and the end setting) per cell, `counted` and its detection positions, to `evidence/sc009b-clicks.csv`; not gated on `outL/R`.
  **(iii) GATE, counted == 0:** the same differential count on `coreL` and `coreR` (the generator tap, linear in the vector) for every step
  and block size. **Positive controls** (gated: `counted ≥ 1` AND the transition's max |Δy| within ±N > 1.1 × the references'), flagged per
  arm and note: an unsmoothed full-range step (a hand-made pan vector jump 0 → ±1 handed to the core with the field bypassed) and a hard
  switch (L/R swapped at one sample), placed by `bsfControlGridPoint`. No bar on the field changes; the previous endpoint-only count on the
  chain output was ill-posed through Fold 100 (spec SC-009(b), Clarifications "Session 2026-10-18 (build rulings)").
- `BassStereoField_H1Centred` `[sc-018]` (FR-013, FR-061): every mode × Spread {0.5, 1} at E0, E1, C2, E2, C3, C4: Goertzel h1 on `coreL` and on
  `coreR` equals `deliveredGains()[0] · kCenterPanGain` within 0.01 dB (from the `h1DeliveredPerInterval` tap, T007); the h1 power in M at `coreL/R` and the `AnchorOscillator` output power (`anchorL`, the Anchor arm) each within 0.1 dB of
  the Spread 0 render in every mode; the post-character sub-band M level per mode printed (WIDE read against Phase 5's −0.56 … −0.61 dB at 0.10).
- `BassStereoField_PanOnlyDirection` `[sc-019]` (rendered): clean core at `coreL/R`, Spread {0.5, 1}, SPREAD and WIDE, depth {0, 1} each source, E1
  and C2, 6.5 s: for every harmonic n whose weaker channel is within 60 dB of the stronger (C-14 skip rule; skipped count printed), |phase_L −
  phase_R| ≤ 0.01 rad (`ProfundumTest::goertzelPhase`, wrapped to (−π, π]); the harmonic-resolved Q ≥ −3.11 dB for every harmonic with energy.
- **Executor:** write the TU only. **Verify:** Group 7 gate (per-push cases).

### T013 [P] The calibration harvest cases (plan S9.1, C-13; deviations 4, 7)

**Files:** `dsp/tests/unit/systems/bass_stereo_field_harvest_test.cpp`. Target `dsp_systems_tests`. Depends on T007. Tags
`[stereo-field-harvest][.calibration]` only (deviation 4).

- `BassStereoField_WideScaleHarvest` (step 1; **G8-1, 2026-10-18**): for K_wide ∈ {1.0, 0.75} (via `wideScaleOverride`) with g_floor 0.5
  (`sideFloorOverride`): the WIDE dirty arm, every algorithm × Gravity 0 × crossover {60, 120, 250} × notes {E0, E1, E2, C3} × Spread {0.5, 1},
  depth 0: `monoSumCellPasses` **recorded** (pass count per candidate; the floor step owns the sub band); **gated:** SC-007 `wideAliasedDbfs`
  ≤ −80 dBFS on every algorithm at E0–E2, 48 kHz, and SC-010's WIDE-vs-SPREAD lever ≥ 3 dB or ≥ 0.1 at E1 and C3. Choose the largest passing K.
  Print one table row per candidate (pass count / cells, worst Q, worst ρ, worst alias, lever) and the line `HARVEST wide-scale <K> PASS|FAIL`.
  If 0.75 fails, print `STOP AND SURFACE: K_wide < 0.75 needed (plan C-13)`.
- `BassStereoField_SideFloorHarvest_<Arm>` (step 2, 14 shards: CleanSaw, CleanGrowl, Dirty<Alg> ×6, Anchor<Alg> ×6): K_wide = the committed
  `kWideOffsetScale` (pasted after step 1); the SC-001 control grid at **Gravity 0**, depth 0 (deviation 7), every mode, every note. One chain
  render per cell; then `restage` at each candidate g_floor ∈ {0.71, 0.5, 0.35, 0.25, 0.18, 0.125, 0.1, 0.071, 0.05, 0.035, 0.03} (G8-1);
  `monoSumCellPasses` per candidate at the restaged `stageOutL/R` and `outL/R` and the original `coreL/R` (G8-1 readings: octave Q gated on
  clean arms only, anti-phase cells exempt and counted, bands read above −60 dBFS). Print per candidate `HARVEST side-floor <arm> <g> PASS|FAIL <worst Q> <worst ρ>`
  and the wall clock per cell.
- `BassStereoField_SideFloorLeverHarvest`: SC-010's Gravity lever (Gravity 0 ↔ 1, `staticLeverMove` ≥ 3 dB or ≥ 0.1) at E1 (gated) and C3
  (recorded; G8-1 refinement) on the showcase, per candidate (`restage`). Print `HARVEST side-floor-lever <g> PASS|FAIL`.
- **Executor:** write the TU only; the harvest runs in the main loop (T015). **Verify:** the Group 7 gate builds it (hidden; not run).

### T014 [P] CPU budget TU and lane script (SC-013; FR-079; deviation 9) — **lane deferred to the main loop**

**Files:** `dsp/tests/unit/systems/bass_stereo_field_perf_test.cpp`, `specs/profundum-phase7-stereo-field/evidence/close/cpu-lane.sh`. Target
`dsp_systems_tests`. Depends on T006.

- `BassStereoField_Cpu` `[systems][profundum][stereo-field][sc-013][performance][.perf]`: 48 kHz, host block 64, `std::chrono::steady_clock`,
  best of 5, % = wall / audio × 100.
  - **(a)** the stage at Gravity 1 + MOTION (the dearer source, chosen by a first timing pass of both) + `advanceControl` every 32 samples, a
    distinct L / R `kSawAnchor` E1 pair (R delayed by round(0.37·period)), 60 s of audio: **≤ 0.5 %**. **(a-sweep)** the same with the crossover
    control a triangle 60 ↔ 250 Hz with 100 ms legs: **≤ 0.5 %** at 48 kHz; 192 kHz printed; a step-train arm (60 ↔ 250 every 50 ms) printed.
  - **(b)** one and four `ProfundumCore`s (E1, E2, C3, A1) at Spread 1, depth 1, rate 1 (each source), law query + `setPartialPanOffsets` every
    interval, minus the same at Spread 0: **≤ 0.25 %** per voice and **≤ 1.0 %** for four voices.
  - **(c)** printed: handoffs per second on a static note with MOTION off after settling (expected 0).
  - Write `evidence/sc013-cpu.txt`; print `SC013 VERDICT PASS|FAIL <a> <a-sweep> <b1> <b4>` and `SC013 GATED <cells>`.
- `cpu-lane.sh` (copy `specs/profundum-phase6-mass-dynamics/evidence/close/cpu-lane.sh`): refuses to start while `tasklist | grep -ciE
  "cl.exe|dsp_.*_tests.exe|clang-tidy"` is non-zero; runs `pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe <exe> -ExeArgs "BassStereoField_Cpu"`
  to `evidence/run-log/close/cpu-lane.log`; tails it; appends the verdict line to `evidence/run-log/T014.txt`. A red reading is re-run once alone
  after the machine has idled; only then is it a defect (never relax).
- **Executor:** write both files, `bash -n cpu-lane.sh`; return done with notes "deferred to the main loop at close: run `bash
  specs/profundum-phase7-stereo-field/evidence/close/cpu-lane.sh` after everything else (T021)".

---
## Group 8 — calibration checkpoint (single task; STOP AND SURFACE)

### T015 Harvest lane and paste script; the main loop runs the harvest (FR-021, FR-041, SC-001, SC-002, SC-020; plan S9, C-13; deviations 4, 7)

**Files:** `specs/profundum-phase7-stereo-field/evidence/close/harvest.sh`, `specs/profundum-phase7-stereo-field/evidence/close/paste-harvest.js`,
`specs/profundum-phase7-stereo-field/evidence/close/aggregate-harvest.js`, `specs/profundum-phase7-stereo-field/evidence/run-log/T015.txt`.
Depends on T013 (and every Group 7 task, so the dependent cases exist).

- `harvest.sh` (Git Bash; copy the structure of `specs/profundum-phase6-mass-dynamics/evidence/close/harvest.sh`; every step to its own log under
  `evidence/run-log/close/`, a status line per step with exit code and wall clock to `evidence/run-log/close/harvest-status.txt`):
  1. One rebuild of `dsp_systems_tests` (full CMake path; the build-log count of `warning C|warning:` lines must be 0).
  2. `dsp_systems_tests.exe "BassStereoField_WideScaleHarvest"` → log. A `STOP AND SURFACE` line → print it, exit 3.
  3. `node …/aggregate-harvest.js wide-scale <log>` writes `evidence/harvest-wide-floor.txt` with the chosen K_wide and the currently committed
     floor between `BEGIN-INITIALIZER wide-floor` / `END-INITIALIZER wide-floor`; `node …/paste-harvest.js wide-floor`; one rebuild.
  4. **Concurrently** (`xargs -P 15`): the 14 `BassStereoField_SideFloorHarvest_<Arm>` shards and `BassStereoField_SideFloorLeverHarvest`, each to
     its own log (the names generated from `--list-tests "[stereo-field-harvest]"`, never hand-kept).
  5. `node …/aggregate-harvest.js side-floor <logs…>`: the chosen g_floor is the largest candidate that is `PASS` in **every** shard and in the
     lever case; none ≥ 0.03 (G8-1; was 0.1) → print `STOP AND SURFACE: no g_floor >= 0.03 passes (plan C-13)` with the per-candidate table and exit 3. Otherwise
     rewrite `evidence/harvest-wide-floor.txt` (the candidate table, both chosen values, the commit `git rev-parse --short HEAD`, the date) and
     `node …/paste-harvest.js wide-floor`; one rebuild.
  6. Run the harvest-dependent per-push cases by name to one log and print each summary line: `BassStereoField_MonoSumPerBandSmoke`,
     `BassStereoField_GravityFloorSmoke`, `BassStereoField_LeversAudibleSmoke`, `BassStereoField_WideOffsetCeilings`, `BassStereoField_ConstantsMatchPlan`,
     `BassStereoField_SideGainLaw`; then the phase's per-push filter `"[stereo-field]~[performance]~[perf]~[benchmark]~[!benchmark]~[long]"`.
- `paste-harvest.js` (Node, CommonJS, no dependencies; copy `specs/profundum-phase6-mass-dynamics/evidence/close/paste-harvest.js`): for the name
  `wide-floor`, reads the lines between `BEGIN-INITIALIZER wide-floor` and `END-INITIALIZER wide-floor` of `evidence/harvest-wide-floor.txt` and
  replaces the text between `// HARVEST-BEGIN wide-floor` and `// HARVEST-END wide-floor` in `dsp/include/krate/dsp/systems/bass_stereo_field.h`,
  keeping the marker lines and the indentation. Exit 1 unless the marker pair occurs exactly once, the block has exactly 2 lines (one
  `static constexpr float kSideFloorGain = …f;`, one `static constexpr float kWideOffsetScale = …f;`), both numbers parse as finite floats with an
  `f` suffix, 0 < kSideFloorGain < 1 and 0 < kWideOffsetScale ≤ 1. Adds `// harvested <ISO date> from evidence/harvest-wide-floor.txt (plan S9.1)`
  inside the markers. `--self-test` runs it on synthetic copies in a temp directory and checks the exit codes of a good file, a missing marker, a
  duplicated marker, a wrong line count and a malformed value.
- `aggregate-harvest.js` (Node, no dependencies): parses the `HARVEST …` lines of T013's cases; `--self-test` on synthetic logs (all pass, one shard
  failing the top candidate, no candidate passing).
- **Executor:** write the three files; `bash -n harvest.sh`; run `node …/paste-harvest.js --self-test` and `node …/aggregate-harvest.js --self-test`
  (seconds) and record their output in `evidence/run-log/T015.txt`. Then **return status "blocked"** — STOP AND SURFACE: "main loop runs `bash
  specs/profundum-phase7-stereo-field/evidence/close/harvest.sh` (estimate from the per-cell wall clocks the harvest prints; step 4 is 14 concurrent
  shards of 648 depth-0 chain renders each plus 7 restages per cell), takes any `STOP AND SURFACE` line to the user (C-13's options: lower K_wide
  further, accept a lower floor, or re-read SC-002 on the WIDE arm), otherwise confirms step 6 is green, records the readings in
  `evidence/run-log/T015.txt`, and resumes with `unblock: [T015]`". This group's gate then runs the per-push tree on the harvested constants;
  P-4 no longer applies.

---

## Group 9 — defect resolution and the handoff record (sequential)

### T016 Resolve every `DEFECT:` line and gate-documented red

**Files:** any Phase 7 file named by a `DEFECT:` line (the header included); this file's "Defects found" section.

- Read `evidence/run-log/T001.txt` … `T015.txt` and the gate notes of Groups 1–8. Copy every `DEFECT:` line into "Defects found" below, then fix
  the root cause in the owning file with a failing test first. Rules: never weaken a bar or delete a failing test; a bar proven unreachable is
  STOP AND SURFACE with the measurement; FR-073 forbids any edit outside the new files.
- If `BassStereoField_SolidBitIdentical` exceeded 60 s in a gate log, split it by algorithm into six per-push TEST_CASEs (`…_Tube` … `…_Fracture`) in
  `bass_stereo_field_chain_test.cpp` (deviation 10). Re-tag any other untagged case above 15 s as `[long]` and add its `…Smoke` sibling — never a
  bit-identity, NaN / Inf, RT-safety or lifecycle case.
- A MOTION-cell red in the `[long]` SC-001 shards after T015 (deviation 7) is a defect: re-run the harvest with the failing cells added to the
  step-2 grid, never a relaxed bar.
- Pre-triaged items P-1…P-4 below are user rulings or harvest outcomes, not code defects: leave them unless the main loop has recorded a ruling,
  then apply exactly the one-constant change the precondition table names.
- If there are no `DEFECT:` lines and no reds, write "none" under "Defects found" and return done.

### T017 Phase 8 / 9 / 11 / 12 handoff record (FR-080)

**Files:** `specs/profundum-phase7-stereo-field/evidence/phase-handoffs.txt` (new). Depends on T015 (the harvested values).

- Write the plan's "Handoffs (FR-080)" section as a standalone record, with the measured values filled in: the committed `kSideFloorGain` and
  `kWideOffsetScale` and their harvest commit (`evidence/harvest-wide-floor.txt`); the Phase 9 call order (`advanceControl` once per interval →
  per voice note-on and `VoiceState::reset()` → `computePanVector` → `setPartialPanOffsets` → `wideOffsets` → `setChannelOffsets`); the stage's
  position (after `MassDynamics`, before DC ×2 and the limiter); the per-voice CPU line "stereo-field law + pan forwarding" with T001's and
  SC-013(b)'s measured figures (or "pending T021" if the CPU lane has not run); SC-004(a3)'s converged time from `evidence/sc004-a3-convergence.csv`
  (the re-synchronise decision); the C-9 synced-aliasing note with a pointer to `evidence/sc008-sync-fold.csv`; Phase 12's mono-guarantee labels.
  Every number cites its evidence file and line. The comply stage moves it into the roadmap's Phase 8 / 9 notes.

---

## Group 10 — integration (sequential; last)

### T018 CMake registration audit (single task)

- `grep -c` each of the nine Phase 7 TU paths in `dsp/tests/CMakeLists.txt`: exactly 1 each, except
  `unit/systems/bass_stereo_field_nonfinite_test.cpp` = 2 (the list + the `-fno-fast-math` block); `bass_stereo_field_perf_test.cpp` is in no
  `set_source_files_properties` block. `dsp/CMakeLists.txt` lists `include/krate/dsp/systems/bass_stereo_field.h` exactly once, in the systems
  list. `git diff 4599d180 -- dsp/CMakeLists.txt dsp/tests/CMakeLists.txt` shows only added lines. Record in `evidence/run-log/T018.txt`. A missing
  or duplicated line is fixed here (the only task allowed to edit the CMake files after T002).

### T019 Suites lane, phase `[long]` lane and clang-tidy (SC-015; plan S14.2) — **deferred to the main loop**

**Files:** `specs/profundum-phase7-stereo-field/evidence/close/suites.sh`, `specs/profundum-phase7-stereo-field/evidence/close/tidy.sh`.

- `suites.sh` (copy the structure of `specs/profundum-phase6-mass-dynamics/evidence/close/suites.sh`, **with the scope rule of 2026-10-18**, Phase 6
  ruling G9-4; designed to run **detached**, writing `evidence/run-log/close/status.txt` with one line per lane — name, exit code, wall clock — so it
  survives the 2 h background-task ceiling):
  1. One rebuild of `dsp_systems_tests` (record the wall clock; the count of `warning C|warning:` lines must be 0).
  2. **Change-set scope** (`BASE_COMMIT=4599d180`): `git diff --name-only $BASE_COMMIT -- dsp/include dsp/src plugins/shared tests/test_helpers` plus
     `git ls-files --others --exclude-standard -- dsp/include` must name **only** `dsp/include/krate/dsp/systems/bass_stereo_field.h` (FR-073, SC-015).
     Any other path is RED, written to the status file with the consumer executables it would pull in, and the script still runs the lanes below
     (it does not add consumer suites itself: an unexpected modified shared header is a defect, not a lane). With the expected diff, no consumer
     suite (Ruinae, Seraphis, Vorago, Innexus, other `dsp_*_tests`) runs.
  3. Concurrently:
     (a) the per-push lane of the phase's executable: `dsp_systems_tests.exe "~[long]~[performance]~[perf]~[benchmark]~[!benchmark]" -D 15` to
     `evidence/run-log/close/perpush-dsp_systems_tests.log` (every case over 15 s printed, so an untagged slow case shows up);
     (b) the `[long]` lane of the **phase's own TUs only**: one shard per TEST_CASE in the list generated by `dsp_systems_tests.exe --list-tests
     "[stereo-field][long]~[performance]~[perf]~[benchmark]~[!benchmark]" --verbosity quiet` (never hand-kept; about 191 shards: 168 SC-001 / SC-002,
     14 SC-020, and one each of SC-003, SC-005, SC-006, SC-007, SC-008, SC-009(a), SC-010, SC-016 and the POLY record), run through
     `xargs -P 28`, each shard to `evidence/run-log/close/long/<case>.log`. **Never every `[long]` case of the executable.**
  4. Summary block: the C-1 / C-2 / C-17 readings (from the per-push log: the mid-mismatch count, the side-decay lengths, the mixed-pair
     excursions), the SC-006 validity table, the SC-001 worst cells per shard, and the wall clock per shard (for the next run's sharding).
  A timing-budget assertion of an untouched (non-Phase-7) case that fails under this concurrent lane is recorded as load, not re-run (root CLAUDE.md).
- `tidy.sh` (copy the structure of `specs/profundum-phase6-mass-dynamics/evidence/close/tidy.sh`): reconfigure `build/windows-ninja` inside vcvars64
  (`"$CMAKE" --preset windows-ninja`; the test CMake list changed — stale compile database otherwise), check all nine Phase 7 TUs are in
  `compile_commands.json`, then `"/c/Program Files/LLVM/bin/clang-tidy.exe" -p build/windows-ninja <TU>` per TU, concurrently, with a 600 s ceiling
  each (the header, the probe and the helper header are checked through the TUs), to `evidence/run-log/close/tidy.log`, last line
  `TIDY GREEN|RED …`. Not the whole-`dsp` sweep.
- **Executor:** write both scripts and `bash -n` them; return done with notes "deferred to the main loop at close: after T015 and T016, run
  `suites.sh` detached and `tidy.sh` concurrently; quote each lane's last lines into `evidence/run-log/T019.txt`; any Phase 7 red is owned (fix,
  re-run only the affected shard or case)".

### T020 Portability and Apple globals (FR-073 portability clause; SC-015)

**Files:** `specs/profundum-phase7-stereo-field/evidence/close/portability.sh`, `specs/profundum-phase7-stereo-field/evidence/run-log/T020.txt`.

- `portability.sh` (copy the structure of `specs/profundum-phase6-mass-dynamics/evidence/close/portability.sh`): concurrently, each with a 300 s
  ceiling, `node tools/lint-apple-globals.js` over `systems/bass_stereo_field.h`, `bass_stereo_field_probe.h`, `bass_stereo_field_test_helpers.h` and
  the nine TUs, and `node tools/check-portability.js` over the nine TUs; then `wsl --shutdown` (check-portability boots WSL; WSLg traces otherwise
  keep writing to C:). Output `evidence/run-log/close/portability.log`, last line `PORTABILITY GREEN|RED`.
- **Executor:** write it and run it once (each lane is minutes at most). If both report 0 findings, quote the log in `evidence/run-log/T020.txt` and
  return done. A finding in a Phase 7 file is fixed here (the group gate rebuilds). If the run fails with `wsl: Failed to translate` (the sandbox
  does not mount the repo drive) or exceeds 5 minutes, stop it and return done with notes "deferred to the main loop at close: run `bash
  specs/profundum-phase7-stereo-field/evidence/close/portability.sh` unsandboxed".

### T021 CPU lane alone (SC-013) — **deferred to the main loop**

- Precondition: T015, T019 and T020 lanes finished, machine idle. Executor: return done with notes "deferred to the main loop at close: run
  `bash specs/profundum-phase7-stereo-field/evidence/close/cpu-lane.sh` (T014) last, alone; quote the figures in `evidence/run-log/T021.txt` and
  in `evidence/phase-handoffs.txt` (T017)".

### Close procedure (main loop, in this order)

1. `run-bench.sh` alone (T001) before Group 1.
2. `harvest.sh` (T015) at the Group 8 checkpoint. A `STOP AND SURFACE` line goes to the user before anything else runs.
3. `suites.sh` detached + `tidy.sh` concurrently (T019); `portability.sh` if T020 deferred it.
4. Any red → triage into "Defects found", fix, re-run only the affected shard or case. The C-1 / C-2 / C-17 readings go to the user as measurements
   (P-1…P-3) unless already ruled.
5. `cpu-lane.sh` alone (T014 / T021), after the machine has idled.
6. Comply (outside this task list) quotes the evidence files, the harvested constants and the run-log readings; `evidence/phase-handoffs.txt` (T017)
   goes into the roadmap's Phase 8 / 9 / 11 / 12 notes there.

---

## Defects found

*(T016 and sequential tasks append here: case, output, diagnosis, owning file.)*

**Pre-triaged (predicted by the plan; harvest outcomes, not code defects — gate fixers leave these alone):**

- **P-1, P-2, P-3: CLOSED 2026-10-18 before the build.** C-1, C-2 and C-17 were ruled to the recommended reading and the three switches are
  built `false` (see the precondition table). `BassStereoField_SafetyStageMidBitExact`, `BassStereoField_StageLrInLrOutDirtyState`,
  `BassStereoField_AnchorBitIdenticalDirtyState` and `BassStereoField_CurveInterpolationMixedPairs` print the spec-as-written reading and
  gate the ruled one; a red in any of them is a real defect.
- **P-4 (plan S1 C-13, harvest):** before T015, `BassStereoField_MonoSumPerBandSmoke`, `BassStereoField_GravityFloorSmoke` and
  `BassStereoField_LeversAudibleSmoke` read the seeds (g_floor 0.5, K_wide 1.0) and may be red; T015 harvests the constants. After T015 a red in
  them is a real defect (or C-13's STOP AND SURFACE).
  **CLOSED 2026-10-18:** harvested g_floor 0.035, K_wide 1.0 (three lane runs; ruling G8-1 and its refinements; see
  evidence/run-log/T015.txt). The six named cases and the [stereo-field] per-push filter are green on the pasted constants.
- **P-0 (workflow): CLOSED 2026-10-18.** `~[.]` appended to the workflow's `PHASE_FILTER`, so the gates no longer run the five hidden Phase 1–6 cases.

**Group 10 close (2026-10-19), long lane 6 / 194 red, tidy 34 warnings — all resolved by the main loop (spec Clarifications
"Session 2026-10-19 (close rulings)"):** G10-1 SC-003 dirty-arm octave clause recorded (user); G10-2 sub-band gates only where a harmonic
lies in [20, f_c) (two Fracture C3 / 60 Hz MOTION shards); G10-3 truncated top bands and W below −100 dB not read (WidthMonotonic); G10-4
SC-016 W_n compared where ≥ −100 dB (192 kHz residue); G10-5 SC-006 fixture (ii) recorded (vacuous below −60 dBFS); G10-6 SC-010 W_oct
window ≥ 8 periods (Drift lever at E1). The 34 tidy warnings were all in test TUs (forwarding references, DeMorgan, nested conditionals,
memcmp on floats, an int-based enum, unchecked optionals) and were fixed without changing any assertion.

**Group 7 gate (2026-10-18), `BassStereoField_TransitionsClickFree` (chain_test.cpp):**

- **FIXED (test defect, T012 deviation 3): positive controls MISSED.** Both controls put the step at the grid point of the largest |a − b|,
  which lands on a waveform edge where the references already have a detection inside ±N, so the differential count matched it away by
  definition (pan jump MISSED at E0/E2/C3, hard switch MISSED at E0). `bsfControlGridPoint` now skips grid points with any reference
  detection within ±N and picks the largest min-over-L/R of |a − b| ÷ the references' max |Δy| over ±max(N, 512). After the fix all 9
  controls read `DETECTED` (counted 1/1). No bar or definition changed.
- **RULED 2026-10-18 (G7-1, user; T012 re-dispatched with the long-ramp form of SC-009(b) — see the T012 section): 14 transition cells
  counted clicks with the endpoint-only differential count.** Original diagnosis: E2 SOLID→SPREAD outL 1 /
  outR 4; C3 SOLID→SPREAD outL 5 / outR 23; C3 SPREAD→SOLID outL 3; C3 FC60→FC250 outR 1 and FC250→FC60 outR 1, identical at blocks 64 and
  512. Diagnosis (temporary probe, removed): every counted detection falls 5–170 ms after the step, inside the FR-015 pan slew; the same
  differential count on `coreL/R` is **0** in every cell, and the counts first appear at `characterL/R` (Fold 100 → Tube, the Anchor arm),
  then carry unchanged through dyn, stage and out. They recur once per period: the folded waveform at an intermediate pan vector has
  steeper edges than at either held endpoint (C3 R: transition max |Δy| 0.46 against reference maxima 0.14 / 0.34 within ±N). Steady renders
  held at intermediate Spread reach the same slopes: C3 outR max |Δy| is 0.648 at Spread 0.8 against 0.640 in the transition window; E2 is
  0.548 against 0.489; C3 FC60→FC250 outL is 0.722 against 0.684 steady at f_c 100, a ratio of 1.055. So on the Phase 5 SC-003(b)
  sample-delta form (≤ 1.1 × the steady max over the traversed range) every cell passes. The field does what FR-015 and FR-052 specify. The
  endpoint-only differential count cannot align a nonlinear stage's intermediate waveform, which is the Phase 5 SC-003 single-step precedent
  ("recorded, not counted, and arm (b) is that cell's gate", user ruling 2026-10-16). Phase 7's SC-009(b) adopts the definition "unchanged",
  so the gate stays red until the user rules. Options: (1) adopt the Phase 5 SC-003(b) exemption for SC-009(b), recording detections inside
  the slew window and gating on a sample-delta bound against steady renders over the traversed path; (2) gate SC-009(b) at a pre-nonlinear
  tap (core or clean arm) and record the Anchor arm. Owning files: spec.md SC-009(b) and chain_test.cpp. The header is not at fault.

**Group 7 gate, epoch after the G7-1 rewrite (2026-10-18), `BassStereoField_TransitionsClickFree` (chain_test.cpp:831) — RULED 2026-10-18
(G7-2, user: the WIDE offsets now engage and release over the FR-015 slew length; header + SC-017 test edited by the main loop, T005 section
amended):** original diagnosis: C3 SOLID>SPREAD>WIDE step 2 (SPREAD → WIDE) outR ratio **1.1148** > 1.1 (tr_max_dy 0.680 against the
wide_scale path max 0.610), identical at blocks 64 and 512; every other cell passes (worst E0/E2 SPREAD↔WIDE well under). Diagnosis
(temporary hidden probe in chain_test.cpp, removed; C3, block 64, per-20 ms max |Δy| from the step): `coreR` unchanged (0.216);
`characterR` 0.704 in [0, 20 ms) against held-WIDE 0.708 and `sumR` 0.701 against 0.706 — the field and the character stage stay
inside the steady path; **`dynR` jumps to 0.788** (held-WIDE 0.706), so the excess is born in Phase 6 `MassDynamics`: its
`TransientShaper` gain (`shaperGainDb` tap) goes from ≤ 0.04 dB (held WIDE) to **+1.24 dB** in the first 20 ms and decays over
~220 ms (1.13 → 0.08 dB); the compressor stays at 0 dB. The cause is the SPREAD → WIDE step itself: FR-044 keeps the vector, so this
step is NOT the ~220 ms pan-slew ramp the G7-1 clause assumes ("every mode step a ~220 ms ramp") — it is the FR-042 / plan :757
offset onset 0 → ±K through the character stage's 10 ms smoothing, which raises R's edge level fast enough for the shaper to treat it
as a transient. Steady renders from `reset()` cannot reproduce a history-dependent dynamics response, so the bound is unmeetable by
construction for this one step while every component behaves as its own spec says. The test matches spec SC-009(b) as written, so it
was not changed. Options for the user: (1) ramp the WIDE offsets in/out over the FR-015 slew length (field change to FR-042/S6, makes
SPREAD↔WIDE a genuine long ramp); (2) for SPREAD↔WIDE, gate the sample-delta bound at the pre-dynamics tap (`sumR`/`characterR`,
ratio ≈ 1.0) and record outL/R; (3) accept the shaper's ≤ 1.3 dB attack response on a mode step and widen nothing else (a bar
change — the user's call only). Owning files: spec.md SC-009(b) / FR-042, chain_test.cpp; bass_stereo_field.h is not at fault.

**T016 (2026-10-18): defect resolution.**

- **`DEFECT:` lines:** none. `grep -rn DEFECT evidence/run-log/` finds only "none" lines (T003.txt:77, T004.txt:69, T007.txt:75,
  T009.txt:67, T010.txt:104-105, T012.txt:70/109, T013.txt:67-84, T015.txt:78-79). The gate notes above (Group 7 G7-1, G7-2) are RULED and
  encoded; the Group 8 harvest-step-6 red (`BassStereoField_WidthMonotonicSmoke` / `sc003c_gravity_never_raises_octave_s2m`, -115.70 dB
  float residue against the 0.01 dB tolerance) was resolved by the main loop under T015 (the ratio read only where >= -100 dB, spec SC-003(c));
  the re-run of the `[stereo-field]` per-push filter is green, "All tests passed (60810 assertions in 58 test cases)"
  (`evidence/run-log/close/harvest-step6-perpush-rerun.log`, last lines). No MOTION-cell red: the `[long]` SC-001 shards have not run yet
  (T019), so the re-harvest clause has nothing to act on.
- **Wall-clock re-tags (T016 bullet 2; readings from `evidence/run-log/close/harvest-step6-perpush-rerun.log`).** Test-structure changes
  only, no bar, grid or definition changed; none is a code defect, so there is no failing-test-first step:
  - `BassStereoField_SolidBitIdentical`: `SC004_A1,renders,3888,wall_s,173.094` (:437) > 60 s, plus `SC004_A2A3 ... wall_s,32.4106` (:438).
    Split by algorithm into six per-push TEST_CASEs `BassStereoField_SolidBitIdentical_Tube` / `_Fold` / `_Rectify` / `_Bite` / `_Crush` /
    `_Fracture` (`[sc-004]`, never `[long]`, deviation 10), each running (a1) and (a2)/(a3) for its algorithm over the unchanged grid
    (expected ~29 s + ~5 s each). `evidence/sc004-a3-convergence.csv` keeps its name and header; each case replaces its own algorithm's rows
    and keeps the others' (rows in kBsfAlgorithms order), so a full run still writes all 96 rows. The SC-004 spec / plan rows still name
    `BassStereoField_SolidBitIdentical`: comply should read it as the six `…_SolidBitIdentical_<Algorithm>` cases.
  - `BassStereoField_TransitionsClickFree`: `SC009B ... wall_s,103.791` (:1290) > 15 s -> `[long]` (a click-free render, root CLAUDE.md's
    `[long]` class; not bit-identity, NaN / Inf, RT-safety or lifecycle). Smoke sibling `BassStereoField_TransitionsClickFreeSmoke`
    `[sc-009]`: E2, block 512, G0>G1>G0, all three SC-009(b) gates plus both positive controls, record `evidence/sc009b-clicks-smoke.csv`
    (the `[long]` case keeps `sc009b-clicks.csv`). The body became `bsfTransitionsClickFree(notes, blocks, sequences, csv)`, which renders only
    the held settings and steady paths the selected sequences read; the full case selects every sequence, so its renders are unchanged.
  - `BassStereoField_H1Centred`: `SC018,wall_s,16.3986` (:1327) > 15 s -> `[long]`; smoke `BassStereoField_H1CentredSmoke` `[sc-018]`: E1,
    every mode, Spread 1, against the Spread 0 reference.
  - `BassStereoField_PanOnlyDirection`: `SC019,wall_s,18.7473` (:1352) > 15 s -> `[long]`; smoke `BassStereoField_PanOnlyDirectionSmoke`
    `[sc-019]`: E1, WIDE, Spread 1, MOTION 1 Cycle.
  - The per-push log was run without `-D`, so cases that print no wall clock were not timed. T019's per-push lane runs with `-D 15`; a case
    it shows above 15 s is re-tagged under the same rule there. T019's `[long]` shard list is generated, so it picks up the three new
    `[long]` cases (about 194 shards, not 191).
- `bass_stereo_field.h`: not edited (no defect names it).
