# Tasks: Profundum Phase 9 — Voice & Engine

**Spec:** `specs/profundum-phase9-voice-engine/spec.md` (Draft 2026-10-19; Q1–Q8 ruled, OQ-2 / OQ-8 / OQ-9 resolved) · **Plan:**
`specs/profundum-phase9-voice-engine/plan.md` (S0–S15, C-1…C-21, R-A…R-L, review notes 2026-10-20) · **Roadmap:**
`specs/Profundum-roadmap.md` Phase 9 (lines 647–697) · **Phase base:** commit `0ae33b87` on `synth/profundum`
**Status:** Task list (2026-10-20). The build stage has not started. Four plan clauses were **FOR RATIFICATION** and are **RATIFIED by the main loop, 2026-10-20** (spec Clarifications, plan-stage session) (C-1, C-3's 192 kHz
clause, C-7, C-21) and none has a recorded ruling yet (spec Clarifications holds only the specify-stage session). The tasks build every
item at the plan's reading (precondition table). Four ⚠ RISKs (R-A … R-D) carry probes with stop rules (T006, T013). No commit tasks:
commits happen outside this workflow.

## Read this first (every executor)

### Precondition: plan S1 (C-1…C-21, R-A…R-D)

| Item | What the tasks build | If the ruling goes the other way | Spec row | Task(s) |
|---|---|---|---|---|
| C-1 **RATIFIED 2026-10-20** | No `TruePeakLimiter` accessor; at the SOLID re-sync only `dcR_ = dcL_` (DCBlocker's defaulted copy, `primitives/dc_blocker.h:114–117`). The limiter's outputs are L == R whenever its inputs are (one shared gain, plan C-1) | An append-only accessor on `processors/true_peak_limiter.h` is added in a new sequential task gated by the limiter's consumer suites; SC-030's list grows by that file | FR-029, FR-084, SC-015, SC-030 | T008, T017, T026 |
| C-2 | "Crossfade end" = the first grid point at which `BassStereoField::sideIdle()` reads true (`bass_stereo_field.h:500`); one `DcResync` call record per SOLID entry | — | FR-029, SC-015 | T008, T017 |
| C-3 (192 kHz clause **RATIFIED 2026-10-20**) | `MassDynamics::setOversampling(bool)`, append-only, off by default; active only when 2·fs ≤ 192 kHz; a recorded no-op at 192 kHz | Running at 192 kHz needs `BassCompressor` to accept 384 kHz — a second shared-header change; STOP AND ASK | FR-082, FR-084, SC-021(b), SC-030 | T005, T006, T018 |
| C-4 (D-8, ruled by the plan) | No second steal mechanism: the steal-fade factor in g_d is identically 1.0f; a Hard steal is `Transition::VoiceReuse` and the Note Memory declick dip fades it; `disableStealFade` suppresses the dip for steal-originated transitions, `disableDeclick` for every other | — | FR-015, FR-016, SC-005 | T007, T015 |
| C-5 | `HarmonicBalanceMeter`: period-synchronous DFT on the Fold-output mid; flag true at ≤ −22 dB, false at ≥ −18 dB, held between | — | FR-008, SC-033 | T007, T010 |
| C-6 | a₁ via cached `evaluateShape` / `evaluateMask` / `applyMask` over spans of exactly 64 | — | FR-005 | T007, T011 |
| C-7 **RATIFIED 2026-10-20** | Compression row handed post-law: `clamp(t.compressionAmount + c_TL, 0, 1)` (add skipped at ±0); SC-027(a) compares the compression receiver with that expression | Offsetting the law's base makes the Loose half inert (plan C-7); T011's SC-027(a) arm and `compressionContribution` would be rewritten | FR-052, SC-027(a) | T002, T008, T011 |
| C-8 | Rows whose base is A use **A_s** = A with `ampEnv.sustain = 1`; the CSV names "+ sustain 1 (C-8)". **B10-1 (2026-10-20):** Dark ↔ Bright's harvest, SC-007 sweep and AT-48 row use **B_s** = B with `ampEnv.sustain = 1` (`voiceEngineBs`), named "B + sustain 1 (B10-1)" in the CSV; its A_s reading is recorded beside it | — | FR-080, SC-007, SC-010 | T009, T014, T016 |
| C-9 | Events latched at the engine's current sample, applied at the next grid point; fixed queue `kMaxQueuedEvents = 256` (a full queue drops the note-on and counts it); note-offs never dropped (128-entry pending-release flags) | — | FR-063, FR-020 | T008 |
| C-10 | Voice output `(c·g_amp)·g_d + (a·g_amp)·g_d` literally; the fold feed is skipped (not ×0) when both ramp ends are +0 or `anchorDetached` is on | — | FR-015, FR-004, SC-019 | T007, T010 |
| C-11 | Pitch arithmetic fixed (`semitonesToRatio(b·range)`, MONO `processPortamento()·ratio`, POLY `midiNoteToFrequency(note)·ratio`, `f_core = f_res * std::exp2(d_c / 12.0f)`); SC-020 gated **bitwise** | — | SC-020 | T007, T008, T010 |
| C-12 | Velocity unused (recorded for Phase 11) | — | FR-022 | T008, T023 |
| C-13 | Motion sources run at fs / 32; all three advance every interval whichever is selected | — | FR-040, FR-045, FR-046 | T003 |
| C-14 | `ProfundumEngineConfig::placement ∈ {PerVoice (default), PostSum}`; PostSum semantics as plan C-14; MONO always PerVoice | — | FR-014, SC-004 | T008, T020, T027 |
| C-15 | The engine renders in ≤ 32-sample chunks; every component prepared with `maxBlockSize = 32`; the config carries no `maxBlockSize` | — | FR-025, FR-064, SC-024 | T008 |
| C-16 | SC-001 / SC-002 gate the worse of **Tube** and **Fracture** | — | SC-001, SC-002 | T020 |
| C-17 | Drafted values adopted (FR-080 bars; SC-007 ε 0.25 dB / 0.02 oct / 2 %; SC-014 sources; D-3; no POLY glide; FR-023; a seed change at the next prepare / reset) | — | several | all |
| C-18 | Every `MotionConfig` and `AmpEnvelopeFields` field realised; no `MorphState` append, no version bump | — | FR-047, FR-009 | T003, T007 |
| C-19 | `kDefaultSeed = 0xB455u`; salts Motion drift 1, field drift 2, voice base 16 (voice seeds derived and stored, consumed by nothing) | — | FR-032 | T008 |
| C-20 | Every setter the voice and the engine issue is change-only (bitwise); the first interval after prepare / reset hands everything | — | FR-013, FR-054 | T007, T008 |
| C-21 **RATIFIED 2026-10-20** | SC-021(c) (48 st Drop, −96 dBFS) gated on the **core tap**; the Sum tap recorded | Gating the Sum at −96 dBFS is a different bar on a nonlinear chain; STOP AND ASK if ruled so | SC-021(c) | T018 |
| ⚠ R-A | P-1 CPU probe (T013) before any gated render is trusted; levers L1 (built in) → L2 PostSum (FR-014's own criterion) → L3 `db_utils.h` fast path **only with a user ruling** (R-D) | — | SC-001, SC-002 | T013, T027 |
| ⚠ R-B | P-2 declick-reference probe (T013) | — | SC-005(a), SC-006(a) | T013 |
| ⚠ R-C | P-3 output-alias probe on `MassDynamics` alone (T006), before the voice and engine exist | — | SC-021(b) | T006 |
| ⚠ R-D | No change to `core/db_utils.h` without a user ruling | — | SC-030 | T026 |

### Deviations from the plan made while cutting these tasks (surface them; do not silently revert)

1. **CMake registration is the first code task (T001), not the last.** Every later task is test-first and its TU must be in the build to
   fail; `dsp/tests/CMakeLists.txt` enumerates TUs, so an unregistered TU silently drops out. T001 creates compiling stubs for the five
   headers and all fourteen TUs of plan S10.1. The last group's single CMake task (T024) audits the registration. Phases 6–8 deviation 1 is
   the precedent.
2. **`ProfundumVoice` is test-first against its own public API (T007).** Plan S5.1 says tests drive the voice through the engine, which does
   not exist when the voice is written. T007 writes direct-voice cases (`prepare` / `noteEvent` / `beginInterval` / `render` with a hand-built
   `IntervalInputs`) for the voice-local properties (the FR-015 identity, the C-10 skip, the FR-010 `static_assert`s, the meter bound, a₁).
   The plan's engine-driven voice cases (SC-019, SC-020, SC-033, SC-034, `VoiceEngine_DropLatchedAtTrigger`, `VoiceEngine_MeterBoundedAtTopNote`)
   land in T010 through the engine, as the plan writes them.
3. **The engine's fast cases are split in two tasks.** T008 writes the cases that drive the engine directly (lifecycle, setters, getters,
   `static_assert`s); T011 writes the cases that need the T009 driver and observer. The helpers depend on the engine API, so they cannot
   precede it (plan S14 orders T006 helpers before T007 / T008).
4. **The probe TU is filled by four tasks in four sequential groups** (T006 P-3; T013 P-1, P-2, P-4, H-13; T014 harvests; T021 listening
   renders). Each appends; none edits another's cases.
5. **Every `[long]` TEST_CASE has a non-long `…Smoke` sibling** (one small cell, the same assertions and the same bar), so each gate
   exercises the path (the gate filter excludes `[long]`). Phase 6 deviation 10, Phase 7 deviation 6 and Phase 8 deviation 5 are the
   precedent. No bar changes; the `[long]` shards gate the full grid.
6. **The SC-011 cross-build fingerprint is its own TEST_CASE, `VoiceEngine_DeterminismFingerprint`, written by T014 after the harvest.**
   `VoiceEngine_DeterminismHarness` (T011) holds the in-process bitwise arms only, so no case ever runs against an unharvested placeholder.
7. **Hidden cases carry `[profundum]`.** The gate filter ends in `~[.]` (`.claude/workflows/profundum-phase.js:551`), so a hidden case never
   runs inside a gate; it carries the phase tag so the tree-by-tag rule holds for every TEST_CASE of this phase.
8. **The CPU lane uses `tools/pin-perf-cores.ps1` per case name (Phase 8 `cpu-lane.sh` structure), not `node tools/run-cpu-tests.js
   dsp_systems_tests`.** Plan S11.2 names the runner "filtered to [voice-engine]", but the runner takes no filter, and its fixed filter
   `[performance],[perf],[.perf],[benchmark],[!benchmark],[long]~[vorago-sweep]` (`tools/run-cpu-tests.js:56`) would run every `[long]` case
   of `dsp_systems_tests`. The Phase 6 SC-017 CPU case (its TU consumes the modified header) is named in the lane, as plan S11.2 asks.
9. **The consumer `[long]` lane is computed, not hand-kept.** A Node script (T025) takes the transitive include closure of
   `dsp/include/krate/dsp/systems/mass_dynamics.h` over `dsp/tests/**/*.{h,cpp}` (today 16 direct includers: the two CMake lists,
   `bass_morph_test_helpers.h`, `bass_stereo_field_test_helpers.h`, `mass_dynamics_test_helpers.h`, `mass_dynamics_probe.h` and ten TUs),
   extracts the TEST_CASE names of every consumer `.cpp`, and shards the `[long]` ones. Plan S11.2 says "listed with --list-tests"; Catch2
   cannot filter by file, so the name list is derived from the sources and cross-checked against `--list-tests "[long]"`.
10. **P-1's per-stage breakdown is a test-side replica.** Plan S10.4 asks for stages "switched off one at a time by probe-only seams in the
    test, never in the header". The probe composes the same components in the S5.4 / S5.6 / S6.5 order in test code and times it with one
    stage removed at a time; the full-engine number comes from the real `ProfundumEngine`. The replica's total is printed beside the engine's,
    so the replica's fidelity is visible.
11. **Phase 4's Drop descriptors and sample-delta bound are ported, not included.** `t22CoreDescriptors`
    (`profundum_note_behaviour_spectral_test.cpp:2005`), `t22DeltaBound` (`:1293`) and `t22DeclickSlope` (`:1190`) are TU-local in a `.cpp`.
    T009 ports them into `profundum_voice_engine_test_helpers.h` under the `voiceEngine` prefix, citing the source lines, with self-tests on
    known signals.
12. **Macro harvest markers are placed by T002** (`// HARVEST-BEGIN macro-rows` / `// HARVEST-END macro-rows` around `kRows`), so T014's
    paste changes only the lines between them.

### File map

| File | Kind | Layer / target | Created / edited by |
|---|---|---|---|
| `dsp/include/krate/dsp/systems/profundum_macro_matrix.h` | new header (plan S4) | L3 | T001 stub; T002; T014 (between the harvest markers only) |
| `dsp/include/krate/dsp/systems/profundum_motion.h` | new header (plan S3) | L3 | T001 stub; T003 |
| `dsp/include/krate/dsp/systems/profundum_engine_observer.h` | new header (plan S8: seams, call / grid / tap records, observer) | L3 | T001 stub; T004 |
| `dsp/include/krate/dsp/systems/profundum_voice.h` | new header (plan S5) | L3 | T001 stub; T007 |
| `dsp/include/krate/dsp/systems/profundum_engine.h` | new header (plan S6; `ProfundumEngineConfig`, `ProfundumNonlinearPlacement`) | L3 | T001 stub; T008 |
| `dsp/include/krate/dsp/systems/mass_dynamics.h` | **existing shared header**, append-only opt-in (plan S7) | L3 | T005 only |
| `dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h` | new test header (driver, observer, twin runner, readers, click machinery; plan S10.2) | — | T009 (read-only after) |
| `dsp/tests/unit/systems/profundum_voice_engine_helpers_test.cpp` | new TU | `dsp_systems_tests` | T001 stub; T004; T009 |
| `dsp/tests/unit/systems/profundum_motion_test.cpp` | new TU | `dsp_systems_tests` | T001 stub; T003 |
| `dsp/tests/unit/systems/profundum_macro_matrix_test.cpp` | new TU | `dsp_systems_tests` | T001 stub; T002 |
| `dsp/tests/unit/systems/profundum_voice_test.cpp` | new TU | `dsp_systems_tests` | T001 stub; T007; T010 |
| `dsp/tests/unit/systems/profundum_engine_test.cpp` | new TU | `dsp_systems_tests` | T001 stub; T008; T011; T014 |
| `dsp/tests/unit/systems/profundum_engine_nonfinite_test.cpp` | new TU, **`-fno-fast-math`** | `dsp_systems_tests` | T001 stub; T012 |
| `dsp/tests/unit/systems/profundum_engine_click_test.cpp` | new TU (`[long]` shards + smokes) | `dsp_systems_tests` | T001 stub; T015 |
| `dsp/tests/unit/systems/profundum_engine_audibility_test.cpp` | new TU (`[long]` shards + smokes) | `dsp_systems_tests` | T001 stub; T016 |
| `dsp/tests/unit/systems/profundum_engine_chain_test.cpp` | new TU | `dsp_systems_tests` | T001 stub; T017 |
| `dsp/tests/unit/systems/profundum_engine_aliasing_test.cpp` | new TU (`[long]` shards + smoke) | `dsp_systems_tests` | T001 stub; T018 |
| `dsp/tests/unit/systems/profundum_engine_soak_test.cpp` | new TU (`[long]` + smoke) | `dsp_systems_tests` | T001 stub; T019 |
| `dsp/tests/unit/systems/profundum_engine_probe_test.cpp` | new TU, hidden cases only | `dsp_systems_tests` | T001 stub; T006; T013; T014; T021 |
| `dsp/tests/unit/systems/profundum_engine_perf_test.cpp` | new TU, `[performance][.perf]`, **not** `-fno-fast-math` | `dsp_systems_tests` | T001 stub; T020 |
| `dsp/tests/unit/systems/mass_dynamics_oversampling_test.cpp` | new TU | `dsp_systems_tests` | T001 stub; T005 |
| `dsp/CMakeLists.txt`, `dsp/tests/CMakeLists.txt` | existing | — | T001 only (T024 audits) |
| `specs/profundum-phase9-voice-engine/spec.md` | existing | — | T016 (an FR-080 per-row setting, only for a row inert at both endpoints); T027 (the FR-014 ruling text from SC-004's readings) |
| `specs/profundum-phase9-voice-engine/evidence/` | TXT / CSV written by tests; `close/` lane scripts; `probes/`; `run-log/<Tnnn>.txt`; `listening-sheet.md`; `phase-handoffs.txt` | — | tests; every task's run-log |

**Untouchable (zero diff lines vs `0ae33b87`, FR-084 / SC-030):** every existing file under `dsp/include/`, `dsp/src/`, `plugins/`,
`tests/test_helpers/`, and every existing test file and helper, **except** `dsp/include/krate/dsp/systems/mass_dynamics.h` (T005, append-only
plus the two documented branch points) and the two CMake lists (T001). In particular `processors/true_peak_limiter.h` (C-1), `core/db_utils.h`
(R-D), `processors/mono_handler.h`, `systems/voice_allocator.h`, `systems/voice_mod_router.h`, `systems/voice_mod_types.h` (FR-048, Q1), the
harmonic oscillator bank headers, `systems/morph_state.h`, `systems/bass_morph_engine.h`, every Phase 1–8 component header and every Phase 1–8
test helper header. If a reused header looks unusable, STOP AND SURFACE: FR-084 allows only a ruled append-only change gated by its consumer
suites, and that is the user's decision.

### Verified facts these tasks depend on (read 2026-10-20 in this session; plan S0 holds the full ledger)

Paths relative to `dsp/include/krate/dsp/` unless stated. Re-derive every line anchor from the live file before editing.

- **CMake.** `dsp/CMakeLists.txt:217–219`: `# Profundum Phase 8 (specs/profundum-phase8-bass-morph)`, `include/krate/dsp/systems/morph_state.h`,
  `include/krate/dsp/systems/bass_morph_engine.h`, then `)` at `:220`. `dsp/tests/CMakeLists.txt`: the Phase 8 systems block comment starts at
  `:763` (`# Profundum Phase 8 (specs/profundum-phase8-bass-morph) - ENUMERATED, not globbed:`); its TUs end with
  `unit/systems/bass_morph_perf_test.cpp` at `:786`, closed by `)` at `:787`. The `-fno-fast-math` block: `:1284`
  (`# Profundum Phase 8: injects NaN/Inf bit patterns (SC-012, FR-081). The perf TU stays out (shipping FP mode).`), `:1285`
  (`unit/systems/bass_morph_nonfinite_test.cpp`), `:1286` (`PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"`).
  `KRATE_DSP_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}"` is a compile definition (`:877`).
- **Gate filter:** `PHASE_TAG = '[profundum]'`, `PHASE_FILTER = '[profundum]~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]'`
  (`.claude/workflows/profundum-phase.js:550–551`).
- **`MassDynamics`** (`systems/mass_dynamics.h`): class `:469`; `kControlInterval = 32` `:471`; `prepare(double sampleRate, std::size_t
  maxBlockSize)` `:482–488` (`shaper_.prepare` → `comp_.prepare` → `trimGainS_.configure(kTrimSmoothMs, float(sampleRate))` → `reset()` →
  `prepared_ = true`); `reset()` `:491–497`; `processBlock` `:539`; `latencySamples()` `:564`; taps `shaper()` / `compressor()` `:570–571`;
  `stateFinite()` `:574`; `private:` `:581`; members `shaper_`, `comp_`, `trimGainS_`, `trimGainTarget_`, `pendingDurationMs_`,
  `intervalPhase_`, `prepared_` `:585–591`; the closing `static_assert`s `:594–598`; includes `:17–29`. `BassCompressor::prepare` clamps fs to
  [22050, 192000], `kMaxRuleIntervals = 1800` (plan S0.3: `mass_dynamics.h:72–73, 110–142`).
- **`PolyphaseHalfbandOversampler`** (`primitives/polyphase_halfband_oversampler.h`): class `:32`, `setFactor(std::size_t)` `:49`, `reset()` `:57`,
  `upsample(float x, float* out)` `:70`, `downsample(const float* in)` `:100`, `stateFinite()` `:105`.
- **Test helpers:** `MassDynamicsTest::ChainTaps` `:195–211`, `coreControlsFor` `:275`, `unbiasedH1Amplitude(base, f0, fs)` `:288`, `renderChain`
  `:372`, `aliasResidualDb` `:865` (`mass_dynamics_test_helpers.h`); Phase 6's aliasing grid `mdaRunGrid` and its cases
  `MassDynamics_AliasingE0E2_{44k1,48k,96k}` (`mass_dynamics_aliasing_test.cpp:299–312`). `StereoFieldTest::kSubBandQBarDb` `:95`,
  `kSubBandRhoBar` `:97`, `FieldChainTaps` `:136`, `bandReading` `:736` (`bass_stereo_field_test_helpers.h`). `BassMorphTest::MorphChainTaps`
  `:170` (adds `linearCoreL/R`, `resolvedPerInterval`, `anchorLevelHanded`, `advanceSamples`), `renderMorphChain` `:679`, `dCxDb` `:755`,
  `dNlDb(const MorphChainTaps&, f0, fs)` `:771`, `dStDb` `:781`, `dBrOct` `:808`, `lKDb` `:816`, `eSubDb` `:829`, `dTMs` `:919`
  (`bass_morph_test_helpers.h`); `bitEqual(const MorphState&, const MorphState&)` `:40`, `showcasePair()` `:167`, `maxNonlinearPair()` `:245`,
  `algorithmCrossingPair()` `:263` (`bass_morph_state_helpers.h`). `ProfundumTest::goertzelPhase(x, L, fs, hz)` `:157`, `aliasFftLength(fs, f0)`
  `:257`, `aliasedPower(x, fftLen, fs, f0)` `:273` (`profundum_core_test_helpers.h`); `anchorMidiHz(int)` `:88`, `anchorShsF0(x, fs, f0Hint)` `:227`
  (`anchor_oscillator_test_helpers.h`); `ShapeCharacterTest::maxDeltaAround` `:53`, `differentialClicks` `:76`
  (`dsp/tests/unit/processors/shape_character_metric_helpers.h`); `noteBehaviourMatrixScript` `:955` (`profundum_note_behaviour_test_helpers.h`);
  Vorago `kDeterminismSchedule` (`vorago_engine_test.cpp:1418`). `tests/test_helpers/`: `allocation_detector.h`, `enable_ftz_daz.h`,
  `render_fingerprint.h`, `artifact_detection.h` (`ClickDetector`).
- **Tags in the consumer TUs** (for the T025 lane): Phase 6 `[mass-dynamics]` / `[mass-law]`, Phase 7 `[stereo-field]`, Phase 8 `[bass-morph]`,
  each with `[systems][profundum]`.
- **Close-lane templates:** `specs/profundum-phase8-bass-morph/evidence/close/{probe.sh, suites.sh, tidy.sh, portability.sh, cpu-lane.sh,
  harvest-showcase.sh, aggregate-showcase.js}`, Phase 7's `paste-harvest.js`, and `tools/pin-perf-cores.ps1`. `tools/run-close-lanes.js` is
  **not** used: it runs every `[long]` case of `dsp_systems_tests`.
- Every other fact (component signatures and their line anchors) is plan S0 / spec "Existing components"; executors re-open the header
  before calling anything.

### Commands (main loop and gates; executors do not build or run the test tree)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests > <log> 2>&1; tail -5 <log>
B=build/windows-x64-release/bin/Release
$B/dsp_systems_tests.exe "[profundum]~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]" > <log> 2>&1; tail -5 <log>   # the gate
$B/dsp_systems_tests.exe "[voice-engine]~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]" 2>&1 | tail -5                # this phase only
$B/dsp_systems_tests.exe "VoiceEngine_AudibilityTable_Core" 2>&1 | tail -5                                                          # one [long] shard
```

Never `ctest -R`. A positional `"[a]~[long]"` is one AND-term; separate arguments OR. Hidden cases run only when a filter names them. Capture
any run longer than ~30 s to a log and read the log; never re-run a slow suite for its output. One build of a tree at a time. Only
`dsp_systems_tests` contains Phase 9 TUs and the consumers of `mass_dynamics.h`.

### Rules for every task

1. **Failing test first:** write the test cases before the implementation in the same task. Do NOT build or run the test tree (the group's
   gate does). Re-read every file you wrote and fix anything that obviously would not compile. Zero warnings is a gate criterion (MSVC C4244 /
   C4267 / C4100: `f` suffixes, explicit casts, `[[maybe_unused]]`).
2. Header-only, `namespace Krate::DSP {` (nested form). Banner: "Layer 3: System", "Profundum Phase 9 (specs/profundum-phase9-voice-engine)",
   the FRs / SCs it serves and the plan C-items it encodes. Includes are **exactly** the plan S2 table's list for the header (every used symbol
   included directly, never transitively), all downward or L3 peers; never an `effects/` header. New namespace-scope names: exactly
   `ProfundumMotion`, `ProfundumMacro`, `ProfundumMacroTarget`, `ProfundumMacroRow`, `ProfundumMacroValues`, `ProfundumMacroMatrix`,
   `ProfundumEngineSeams`, `ProfundumCall`, `ProfundumCallRecord`, `ProfundumGridRecord`, `ProfundumVoiceTaps`, `ProfundumEngineTaps`,
   `ProfundumEngineObserver`, `ProfundumVoice`, `ProfundumEngine`, `ProfundumEngineConfig`, `ProfundumNonlinearPlacement`. Everything else is
   nested (`ProfundumVoice::HarmonicBalanceMeter`, `ProfundumVoice::IntervalInputs`, `ProfundumEngine::VoiceMode`, `ProfundumMotion::Offsets`,
   `ProfundumMotion::Output`). Never a hazard name (`CharacterProcessor`, `SubOscillator`, `StereoField`, `MorphEngine`, `TransientDetector`,
   `MacroMapper`). Always write `Krate::DSP::VoiceState` and `BassStereoField::VoiceState` in full (F-6, R-H).
3. PascalCase classes, camelCase functions, `trailing_` members, `kPascalCase` constants. Designated initialisers in every aggregate brace init;
   copy-then-assign for `SpectralShapeRecipe::Controls` and `AnchorOscillator::Controls` (plan S5.4 step 7, S6.5); every double → float via
   `static_cast`; no narrowing. Finiteness only via `detail::isFinite` (`core/db_utils.h`); never `std::isnan` / `std::isfinite`. Float bit
   comparisons via `std::bit_cast<std::uint32_t>`; never `memcmp` of `MorphState`. Never hand a non-finite value to `std::clamp`.
4. Every audio-thread method is `noexcept`, allocation-, lock-, exception- and I/O-free; fixed-size members only; no `static` mutable data; no
   `std::function`; the only virtuals are `ProfundumEngineObserver`'s callbacks (called only when an observer is attached) and
   `ProfundumMotion::Output`'s `ModulationSource` overrides (never called by the engine; Vorago AR-2). `prepare` and `setObserver` are
   configuration-time; `reset` is control-thread, allocation-free and bounded (plan S5.3, S6.4).
5. Test helpers are `inline` functions in namespace `Krate::DSP::ProfundumTest` with a `VoiceEngine` / `voiceEngine` prefix; never an anonymous
   namespace in a header. TU-local helpers go in an anonymous namespace with a `ve` prefix. **Qualify every call into `MassDynamicsTest::`,
   `StereoFieldTest::`, `ShapeCharacterTest::`, `BassMorphTest::`, `ProfundumTest::`** (same-named helpers exist, e.g. `bitEqual`); no
   `using namespace` in any Phase 9 file.
6. **Tags.** Every TEST_CASE carries `[systems][profundum][voice-engine]` plus its criterion tag(s) (`[sc-001]` …, `[fr-012]` …). The
   `MassDynamics` opt-in cases carry `[systems][profundum][mass-dynamics][voice-engine]`. The non-finite TU adds `[nonfinite]`; perf cases add
   `[performance][.perf]`; probes `[.probe]`, harvests `[.harvest]`, listening renders `[.listen]`. `[long]` only on the cases named `[long]`
   below, each with its `…Smoke` sibling (deviation 5). **Never `[long]`:** NaN / Inf guards, bit-identity, bounded-grid, RT safety, lifecycle,
   call-order and state / serialisation cases.
7. No bit-exact float goldens. Bit comparisons only between two runs of the same build in the same process (engine twins, seam on / off,
   observer on / off, direct-setter references, test-side references executing the same expressions), or L against R of one render. The one
   cross-build pin is a `render_fingerprint.h` fingerprint harvested inside `dsp_systems_tests` (T014).
8. Notes: E0 / E1 / E2 / C3 = MIDI 16 / 28 / 40 / 48 (`ProfundumTest::anchorMidiHz`: 20.60172 / 41.20344 / 82.40689 / 130.81278 Hz); A1 = 33,
   G1 = 31, B1 = 35, E3 = 52, A2 = 45. fs = 48 kHz and host block 64 unless a case says otherwise. Every measured value is printed with
   `std::setprecision(6)`.
9. **FTZ / DAZ:** every render test sets it for its scope through `tests/test_helpers/enable_ftz_daz.h` (restore on exit); the engine relies
   on it (spec edge case "Denormals").
10. Engines are built with `std::make_unique<ProfundumEngine>()` (each voice ≈ 100 KB, plan S5.2). Seeds are fixed literals, as named in each
    case. Random schedules come only from `std::mt19937` raw words; never `std::uniform_*_distribution` (cross-platform determinism).
11. Evidence: tests write TXT / CSV under `specs/profundum-phase9-voice-engine/evidence/` through `KRATE_DSP_TESTS_DIR` (it points at
    `dsp/tests`, so the repo root is `KRATE_DSP_TESTS_DIR "/../.."`); create directories with `std::filesystem::create_directories`, outside any
    `AllocationScope`.
12. If a test you write exposes a defect in a file another task owns, do not edit that file: add a `DEFECT: <case> <expected/observed>
    <diagnosis> <owning file>` line to your run-log. T022 resolves them.
13. Never relax a threshold or shrink a workload to pass. A bar proven unreachable is STOP AND SURFACE (status "blocked", the measured table in
    notes).
14. Write `specs/profundum-phase9-voice-engine/evidence/run-log/<Tnnn>.txt` for your task: files written, every decision taken, any deviation,
    any `DEFECT:` line.
15. **Lane rule (2026-10-14):** suites, `[long]` shards, clang-tidy sweeps, CPU / perf lanes, probes and harvests longer than ~5 minutes are
    main-loop work. A task that owns one writes the lane script under `specs/profundum-phase9-voice-engine/evidence/close/` and returns done
    with notes "deferred to the main loop at close: run <script>" (or "blocked" where its section says STOP AND SURFACE).
16. Every test that relies on a plan C-item cites it in a comment (`// plan S1 C-7`); every SC test cites its SC id in a comment above the
    TEST_CASE. The plan's S10.3 / S10.6 TEST_CASE names are used verbatim.
17. Scripts are Node.js or bash (root CLAUDE.md: never Python). Bash scripts start with `set -uo pipefail`, resolve the repo from
    `${BASH_SOURCE[0]}` and are checked with `bash -n`; Node scripts with `node --check`.

---

## Group 1 — CMake registration with compiling stubs (sequential, single task)

### T001 Register every Phase 9 file (deviation 1)

**Files:** the five header stubs (`systems/profundum_macro_matrix.h`, `profundum_motion.h`, `profundum_engine_observer.h`, `profundum_voice.h`,
`profundum_engine.h`), the fourteen TU stubs of the file map, `dsp/CMakeLists.txt`, `dsp/tests/CMakeLists.txt`,
`specs/profundum-phase9-voice-engine/evidence/run-log/T001.txt`.

- **Re-run the ODR sweep** and record its output in the run-log: `grep -rnw -E "ProfundumVoice|ProfundumEngine|ProfundumEngineConfig|
  ProfundumEngineSeams|ProfundumEngineObserver|ProfundumMotion|ProfundumMacro|ProfundumMacroTarget|ProfundumMacroRow|ProfundumMacroValues|
  ProfundumMacroMatrix|ProfundumVoiceTaps|ProfundumEngineTaps|ProfundumCall|ProfundumCallRecord|ProfundumGridRecord|ProfundumNonlinearPlacement|
  HarmonicBalanceMeter|VoiceEngineRender|VoiceEngineScript|VoiceEngineTaps|VoiceEngineEvent" dsp plugins tests tools` (`.h`, `.cpp`, `.js`) and
  `find dsp plugins tests -iname "profundum_voice*" -o -iname "profundum_engine*" -o -iname "profundum_motion*" -o -iname "profundum_macro*" -o
  -iname "mass_dynamics_oversampling*"`. Expected: exactly one hit, the comment at `dsp/tests/unit/systems/profundum_note_behaviour_test_helpers.h:11`
  ("Phase 9's ProfundumVoice replaces it"). Any other hit → STOP AND SURFACE.
- **Stub headers:** `#pragma once`, the banner (rule 2, "stub — filled by T00n") and an empty `namespace Krate::DSP {}`.
- **Stub TUs** under `dsp/tests/unit/systems/` (a file comment naming the SCs it will hold and the task that fills it,
  `#include <catch2/catch_test_macros.hpp>`, no TEST_CASE): `profundum_voice_engine_helpers_test.cpp`, `profundum_motion_test.cpp`,
  `profundum_macro_matrix_test.cpp`, `profundum_voice_test.cpp`, `profundum_engine_test.cpp`, `profundum_engine_nonfinite_test.cpp`,
  `profundum_engine_click_test.cpp`, `profundum_engine_audibility_test.cpp`, `profundum_engine_chain_test.cpp`,
  `profundum_engine_aliasing_test.cpp`, `profundum_engine_soak_test.cpp`, `profundum_engine_probe_test.cpp`, `profundum_engine_perf_test.cpp`,
  `mass_dynamics_oversampling_test.cpp`. **Every stub TU includes all five new headers** (so each header is compiled from the first group on,
  and its later filling is compiled by every gate) plus `<krate/dsp/systems/vorago_engine.h>`, `<krate/dsp/systems/seraphis_voice.h>` and
  `<krate/dsp/systems/voice_allocator.h>` in `profundum_engine_test.cpp` only, so the new names are proven to coexist with the voice / engine
  precedents and the two `VoiceState`s.
- **Edit `dsp/CMakeLists.txt`:** after `include/krate/dsp/systems/bass_morph_engine.h` (`:219`), before `)` (`:220`), add
  `# Profundum Phase 9 (specs/profundum-phase9-voice-engine)` and the five header lines in the order `profundum_motion.h`,
  `profundum_macro_matrix.h`, `profundum_engine_observer.h`, `profundum_voice.h`, `profundum_engine.h` (plan S11.1).
- **Edit `dsp/tests/CMakeLists.txt`**, each insertion preceded by a comment in the Phase 8 style (`:763–775`):
  - `dsp_systems_tests`: after `unit/systems/bass_morph_perf_test.cpp` (`:786`), before `)` (`:787`): the comment
    `# Profundum Phase 9 (specs/profundum-phase9-voice-engine) - ENUMERATED, not globbed:` with one row per TU — helpers_test (fixture
    self-tests, observer defaults), motion_test (FR-040–FR-049, SC-026 unit), macro_matrix_test (FR-050–FR-056, SC-008 unit), voice_test
    (SC-019, SC-020, SC-033, SC-034, FR-010 / FR-015), engine_test (SC-008, SC-011, SC-012, SC-022, SC-024, SC-026–SC-029, SC-031, SC-032,
    SC-036), engine_nonfinite_test (SC-023 - in the -fno-fast-math block below), engine_click_test (SC-005, SC-006, SC-025 [long]),
    engine_audibility_test (SC-007, SC-009, SC-010 [long]), engine_chain_test (SC-015/016 [long], SC-017, SC-018), engine_aliasing_test (SC-021
    [long]), engine_soak_test (SC-013/014 [long]), engine_probe_test (P-1..P-4, H-13, harvests, listening renders - hidden), engine_perf_test
    (SC-001..SC-004 [.perf]), mass_dynamics_oversampling_test (FR-082 opt-in) — then the fourteen `unit/systems/…` lines in that order.
  - `-fno-fast-math` block: after `unit/systems/bass_morph_nonfinite_test.cpp` (`:1285`), before `PROPERTIES` (`:1286`):
    `# Profundum Phase 9: injects NaN/Inf bit patterns (SC-023, FR-061). The perf TU stays out (shipping FP mode).` and
    `unit/systems/profundum_engine_nonfinite_test.cpp`. Nothing else.
- **Create** `specs/profundum-phase9-voice-engine/evidence/{run-log, run-log/close, close, probes}/` (a `.gitkeep` in each).
- **Verify (read-only):** re-read both CMake files around each insertion; `git diff --stat` shows only the two CMake files plus new files.
  The Group 1 gate builds `dsp_systems_tests` (it reconfigures automatically) and runs the gate filter; the Phase 1–8 case count is unchanged.

---

## Group 2 — control-layer components and the observer types (parallel; disjoint new files)

### T002 [P] `ProfundumMacroMatrix` (FR-050–FR-056; SC-008 unit arm; plan S4, S9.3, C-7; deviation 12)

**Files:** `dsp/include/krate/dsp/systems/profundum_macro_matrix.h`, `dsp/tests/unit/systems/profundum_macro_matrix_test.cpp`, run-log `T002.txt`.

Tests first, in `profundum_macro_matrix_test.cpp` (tags `[systems][profundum][voice-engine]` + the criterion tag):

- `ProfundumMacro_TableInvariants` `[fr-050]`: over `ProfundumMacroMatrix::kRows` (16 rows): every `ProfundumMacroTarget` below `Count` appears
  exactly once (`REQUIRE(count == 1)` per target); no row has `ModCurve::Stepped`; each row's macro is its target's documented owner (Weight →
  RecipeDepth / RecipeBody / AnchorLevel; DarkBright → RecipeEdge / RecipeShift / Shape / CharacterTone; TightLoose → AmpAttackOct / AmpDecayOct /
  AmpReleaseOct / TransientAttackDb / CompressionAmount; CleanViolent → ExcitationMix / ExcitationDrive / FoldAmount / CharacterAmount); the
  `amount` sign is negative exactly for TransientAttackDb and CompressionAmount and positive for the other fourteen. Also `STATIC_REQUIRE` that
  the same predicates hold at compile time (the header's `static_assert`s).
- `ProfundumMacro_NeutralIsIdentity` `[sc-008][fr-051]`: `ProfundumMacroMatrix m;` (defaults 0.5 each); for 1000 states from
  `BassMorphTest::randomValidState(rng)` with `std::mt19937 rng{0x9008u}`: `MorphState s2 = s; m.apply(s2); REQUIRE(BassMorphTest::bitEqual(s, s2));`
  and every element of `m.contributions()` is ±0 (`std::bit_cast<std::uint32_t>(c) & 0x7FFFFFFFu) == 0`) and `compressionContribution()` is ±0.
- `ProfundumMacro_Directions` `[fr-052]`: base = A_s (`showcasePair().first` with `ampEnv.sustain = 1`, plan C-8) and B; for each macro set to 1
  (others 0.5): each of its FR-052 fields moves in its documented direction (`>=` original, `>` original unless the clamp binds, in which case
  `==` the bound), the three amp times move by exactly `clamp(t · exp2(c), kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs)`; **no other field changes**
  (compare every other leaf bitwise through `BassMorphTest::bitEqual` on a copy with the macro's own fields restored). Set to 0: the opposite
  direction. TransientAttackDb is lowered by Loose (row 10 negative) and `compressionContribution()` < 0 at Loose, > 0 at Tight.
- `ProfundumMacro_ValuesRoundTrip` `[fr-055][fr-061]`: `setMacros({.weight = 0.1f, .darkBright = 0.9f, .tightLoose = 0.3f, .cleanViolent = 0.7f})`
  then `macros()` equals it bitwise; `setMacro(ProfundumMacro::Weight, NaN / +Inf / −Inf)` keeps 0.1 (bit patterns built via `std::bit_cast`
  from `0x7FC00000u` / `0x7F800000u` / `0xFF800000u`, so this TU needs no `-fno-fast-math`); `setMacro(…, 1.5f)` reads back 1.0f, `−0.5f` →
  0.0f; `setMacro(ProfundumMacro::Count, 0.2f)` changes nothing.
- `ProfundumMacro_ContributionArithmetic` `[fr-050]`: for m ∈ {0, 0.25, 0.5, 0.75, 1} and each row, `contributions()[target]` equals
  `amount · applyModCurve(curve, |u|) · (u < 0 ? −1 : 1)` with `u = (m − 0.5f) · 2.0f`, bitwise (plan S9.3, the Vorago form
  `vorago_macro_matrix.h:1170–1173`).

Implementation (plan S4.1–S4.3, S9.3): the types of S4.1 verbatim; `kRows` = the S4.2 seed table, wrapped in `// HARVEST-BEGIN macro-rows` /
`// HARVEST-END macro-rows` lines (deviation 12); the `static_assert`ed invariants as constexpr loops; `setMacro` / `setMacros` (non-finite keeps,
clamp [0, 1], undeclared enum ignored); `macros()`; `contributions()` (pure); `apply(MorphState&)` per the S9.3 table, adding only non-zero
contributions (bit test), clamping per field, amp times in the log domain via `std::exp2`, never touching `CompressionAmount`;
`compressionContribution()` (row 11). Includes exactly plan S2's list (`core/db_utils.h`, `core/modulation_curves.h`, `core/modulation_types.h`,
`primitives/envelope_utils.h`, `systems/morph_state.h`) — re-open `core/modulation_curves.h:37–50` for `applyModCurve` and
`primitives/envelope_utils.h:50–51` for `kMinEnvelopeTimeMs` / `kMaxEnvelopeTimeMs` before writing the call. The test TU includes
`bass_morph_state_helpers.h`. Verify: the Group 2 gate.

### T003 [P] `ProfundumMotion` (FR-040–FR-049; SC-026 unit arm; plan S3, S9.1, C-13, C-18)

**Files:** `dsp/include/krate/dsp/systems/profundum_motion.h`, `dsp/tests/unit/systems/profundum_motion_test.cpp`, run-log `T003.txt`.

Tests first, in `profundum_motion_test.cpp` (fs 48 kHz; `prepare(48000.0)`; one `advanceInterval()` per 32 samples):

- `ProfundumMotion_Sources` `[fr-040][fr-046]`: with `depth = 1`, `amount = 1`, Bipolar, for each source selected alone over 2000 intervals:
  Off → every `sourceValue()` is +0 (bit pattern 0); Env (after `noteRestart()`) → values in [−1, 1] (Bipolar maps 2e − 1) with the maximum
  reached ≥ 0.99 and the last value −1 (rests at e = 0, plan S9.1 step 3); Cycle (0.5 Hz, Sine) → values in [−1, 1], max ≥ 0.99, min ≤ −0.99;
  Drift (smoothness 0.5) → values in [−1, 1] and not constant. **Clock independence (C-13):** select Off for 500 intervals then Cycle; the
  first Cycle value equals bitwise that of a standalone `LFO` prepared at `48000.0 / 32` with the same settings and advanced 500 + 1 times.
- `ProfundumMotion_PolarityAndScale` `[fr-042]`: for each source × polarity × target (5 targets), drive the source to a known value (Cycle with
  `cyclePhaseDeg` chosen so the first `process()` returns a known v; or read `sourceValue()` and recompute): `offsets()` has exactly one non-zero
  field, equal bitwise to `v_pol * amount * depth * span[target]` evaluated left to right in float, span = {50, 0.5, 0.5, 0.5, 1} for {Mass,
  Depth, Body, Edge, Shift}; Unipolar Cycle maps `(raw + 1) / 2`, Bipolar Env maps `2e − 1`; `outputValue()` == `clamp(m, −1, 1)`.
- `ProfundumMotion_DepthZeroIsZero` `[sc-026][fr-042]`: depth 0, amount 1, every source × target × polarity, 500 intervals: every offset field
  is ±0 (`(bits & 0x7FFFFFFF) == 0`).
- `ProfundumMotion_ConfigChangeOnly` `[fr-046][fr-054]`: instance A gets `setConfig(c)` before every one of 1000 intervals, instance B once;
  `sourceValue()`, `outputValue()` and every offset are bitwise equal at every interval (for Cycle with `cycleRetrigger` on and a `cycleFadeInMs`
  of 200, so a re-arming setter would show).
- `ProfundumMotion_ModulationSource` `[fr-048]`: `const ModulationSource& s = motion.output();` `s.getSourceRange()` == {−1, 1};
  `s.getCurrentValue()` == `outputValue()` bitwise at each of 100 intervals; `static_assert(std::is_base_of_v<ModulationSource,
  ProfundumMotion::Output>)`.
- `ProfundumMotion_TempoAndSeed` `[fr-028][fr-032]`: `setTempoBpm(NaN)` / `0` / `−10` keep the previous tempo (a synced Cycle's period is
  unchanged over 2000 intervals); two instances with `setSeed(1)` vs `setSeed(2)` give different Drift values after `prepare` and equal ones
  with the same seed; `setSeed` after `prepare` changes nothing until `reset()`.

Implementation (plan S3.1–S3.4, S9.1, C-13, C-18): the S3.1 API verbatim; state per S3.2 (no heap); `prepare` / `reset` / `setConfig` /
`setTempoBpm` / `noteRestart` / `advanceInterval` per S3.3 and S9.1, each component setter on a bitwise change only; undeclared enums and
non-finite floats keep the previous value; floats clamp to the `MorphState` field-table ranges (`detail::morph_state_io::kFieldTable`,
`morph_state.h` — re-open it for the ranges). The header includes exactly plan S2's list, which has neither the core nor the law, so its
grid `static_assert` is `kControlInterval == BrownianDrift::kControlRateInterval` (`processors/brownian_drift.h:105`); the engine (T008) asserts
`ProfundumMotion::kControlInterval == ProfundumCore::kControlInterval`. Verify: the Group 2 gate.

### T004 [P] `profundum_engine_observer.h` (FR-086; plan S2, S8)

**Files:** `dsp/include/krate/dsp/systems/profundum_engine_observer.h`, `dsp/tests/unit/systems/profundum_voice_engine_helpers_test.cpp` (first
cases), run-log `T004.txt`.

Tests first, in `profundum_voice_engine_helpers_test.cpp`:

- `ProfundumObserver_SeamDefaults` `[fr-086]`: a value-initialised `ProfundumEngineSeams` has all eight bools false (`macroBypass`,
  `anchorDetached`, `forceLinearCore`, `disableStealFade`, `disableDeclick`, `followNoBypass`, `lockReadsCore`, `anchorPhaseTap`);
  `static_assert(std::is_trivially_copyable_v<ProfundumEngineSeams>)`, the same for `ProfundumCallRecord` and `ProfundumGridRecord`.
- `ProfundumObserver_DefaultCallbacksAreNoOps` `[fr-086]`: a `ProfundumEngineObserver` base object accepts each of the four callbacks with a
  default-constructed record; `static_assert(noexcept(o.onGrid(ProfundumGridRecord{})))` and likewise for `onCall`, `onVoiceChunk`,
  `onEngineChunk`; `ProfundumCall` has 37 enumerators (`static_cast<int>(ProfundumCall::DynKey) == 36`), in the S8 order.

Implementation: plan S8's code block verbatim (`ProfundumCall` order is normative — SC-032 compares sequences), with `ProfundumEngineSeams` from
S6.1 moved here as S8's comment says. Includes exactly `systems/morph_state.h`, `systems/bass_morph_engine.h`, `systems/mass_law.h`, `<cstdint>`,
`<cstddef>`, `<span>`. It includes neither the voice nor the engine. Verify: the Group 2 gate; the T026 lint `node tools/lint-layers.js`.

---

## Group 3 — `MassDynamics` opt-in (sequential, single task; the only shared-header change)

### T005 `MassDynamics::setOversampling` and its unit cases (FR-082, FR-084, H-15, Q4; plan S7, C-3, R-J)

**Files:** `dsp/include/krate/dsp/systems/mass_dynamics.h` (append-only), `dsp/tests/unit/systems/mass_dynamics_oversampling_test.cpp`, run-log
`T005.txt`, `specs/profundum-phase9-voice-engine/evidence/os-dynamics-delta.txt` (written by a test).

Tests first, in `mass_dynamics_oversampling_test.cpp` (tags `[systems][profundum][mass-dynamics][voice-engine]`); input = the Phase 6 chain's
sum tap at E2, Fold 100, Mass 100 (`MassDynamicsTest::renderChain` with Phase 6's own Fold-100 `ChainConfig`, read from
`mass_dynamics_aliasing_test.cpp` `mdaRunGrid`), fed to fresh `MassDynamics` instances at Mass-law targets for Mass 100:

- `MassDynamics_OversamplingOffUnchanged` `[fr-084][sc-030]`: instance X never touched by the opt-in, instance Y with `setOversampling(false)`
  before `prepare`; 2 s at 48 kHz, host block 64: L and R bitwise equal sample for sample; `oversamplingRequested() == false`,
  `oversamplingActive() == false`.
- `MassDynamics_OversamplingActiveRange` `[fr-082]`: `setOversampling(true)` then `prepare(fs, 64)`: `oversamplingActive()` true at 22050, 44100,
  48000, 96000; false at 192000 (C-3); `setOversampling(true)` after `prepare` changes `oversamplingActive()` only at the next `prepare`.
- `MassDynamics_OversamplingNoAllocation` `[fr-060]`: `TestHelpers::AllocationScope` around a second `prepare(48000, 64)` with the opt-in on,
  `reset()` and 10 s of `processBlock(…, 64)`: allocation count 0.
- `MassDynamics_OversamplingPartition` `[fr-063]`: opt-in on, the same 2 s input in host blocks {1, 7, 32, 64, 511}: outputs bitwise equal to the
  block-64 run.
- `MassDynamics_OversamplingTransfer` `[fr-082]`: opt-in on vs off, compression amount 0, trim 0, transient gains 0: steady sines at 40 / 200 /
  1000 / 5000 Hz, amplitude 0.25, 1 s settle then 1 s read: the RMS gain of on over off within ±0.05 dB at each frequency; the group delay at
  40 Hz (cross-correlation peak, parabolic interpolation, in base samples) printed and written to `os-dynamics-delta.txt` (plan S7 expects
  ≈ 3.8). **R-J record:** steady GR and makeup (`compressor().currentGainReductionDb()`) at Mass 0 / 50 / 100 on the Fold-100 input, on vs off,
  appended to the same file. Recorded, not gated.
- `MassDynamics_OversamplingStateFinite` `[fr-061]`: opt-in on, 10 s: `oversamplerStateFinite()` and `stateFinite()` true at every 32-sample grid
  point; the output finite (bit test).

Implementation (plan S7, verbatim): the public append after `currentOutputTrimGain()` (`:579`) — `kOversamplingFactor = 2`,
`setOversampling(bool)`, `oversamplingRequested()`, `oversamplingActive()`, `oversamplerStateFinite()` with the S7 comments; private members
appended after `prepared_` (`:591`): `PolyphaseHalfbandOversampler osL_, osR_;`, `std::array<float, 2 * kControlInterval> upL_{}, upR_{};`,
`bool oversamplingRequested_ = false;`, `bool osActive_ = false;`; one include appended to the include block:
`<krate/dsp/primitives/polyphase_halfband_oversampler.h>`. **The two branch points**, written so the off path executes exactly the existing
statements: (1) `prepare`: compute `osActive_ = oversamplingRequested_ && 2.0 * sampleRate <= <BassCompressor's max rate constant>` (re-open
`:110–112` for its name) and `inner = osActive_ ? 2.0 * sampleRate : sampleRate`; prepare shaper, compressor and trim smoother at `inner` (and
with `2 * maxBlockSize` when active — re-open `TransientShaper::prepare` and `BassCompressor::prepare` to confirm what `maxBlockSize` sizes; off
path: identical arguments); when active `osL_.setFactor(2); osR_.setFactor(2);`. (2) `processBlock`: one branch at the top; off → the existing
body unmodified; on → the same grid-run split; per run of `len` base samples: sanitise each input sample as the existing path does,
`osL_.upsample(x, &upL_[2k])` (R likewise; only L when `right == left`), latch the duration at base `intervalPhase_ == 0`,
`shaper_.processBlock(upL_.data(), upR_.data(), 2 * len)`, `comp_.processBlock(…, 2 * len)`, the trim per 2× sample, then
`L[k] = osL_.downsample(&upL_[2k])`. `reset()` appends `osL_.reset(); osR_.reset();`. `latencySamples()` stays 0. The header's banner gains one
line "Profundum Phase 9 (FR-082, Q4): opt-in 2x gain application + 8x detection, off by default".

**Diff discipline:** `git diff 0ae33b87 -- dsp/include/krate/dsp/systems/mass_dynamics.h` shows only added lines plus the two branch points;
paste it into the run-log. **Gate:** the Group 3 gate runs the `[profundum]` per-push filter, which is Phase 6–8's per-push consumer gate
(SC-030); the consumer `[long]` cases run in the T025 lane.

---

## Group 4 — R-C checkpoint: output aliasing with the opt-in (single task; STOP AND SURFACE)

### T006 Probe P-3 `MassDynamics_OversamplingAliasProbe` (plan R-C, S10.4; C-3)

**Files:** `dsp/tests/unit/systems/profundum_engine_probe_test.cpp` (first cases), `specs/profundum-phase9-voice-engine/evidence/close/probe.sh`,
run-log `T006.txt`.

- `MassDynamics_OversamplingAliasProbe` `[systems][profundum][mass-dynamics][voice-engine][.probe]`: Phase 6's Fold-100 E2 cells at 44.1 and
  48 kHz (the `mdaRunGrid` cell set of `mass_dynamics_aliasing_test.cpp:299–312`, E2 rows only), through `MassDynamics` alone, opt-in off and on:
  (i) Phase 6's residual method `MassDynamicsTest::aliasResidualDb` exactly as `mdaRunGrid` calls it; (ii) SC-021's estimator on the same
  output: `ProfundumTest::aliasedPower(y, ProfundumTest::aliasFftLength(fs, f0), fs, f0)` after a 1 s settle, worst channel. Print per cell
  `P3 <fs> <cell> off=<dB> on=<dB> estimator_off=<dBFS> estimator_on=<dBFS>` and a last line `P3 VERDICT GREEN|RED <worst on dBFS>`: GREEN iff
  every on-reading of (ii) ≤ −80 dBFS. Write `evidence/probes/p3-output-alias.txt`. `CHECK`, never `REQUIRE`, so the whole table prints.
- `probe.sh` (copy the structure of `specs/profundum-phase8-bass-morph/evidence/close/probe.sh`): refuse to run if another `dsp_systems_tests`
  or build is running; run `dsp_systems_tests.exe "MassDynamics_OversamplingAliasProbe"` to `evidence/run-log/close/p3.log`; echo the verdict
  line. Leave room (a `case "$1"`) for the T013 probes, which T013 adds.
- **Executor:** write the case and the script, `bash -n` it, return done with notes "deferred to the main loop: run `bash
  specs/profundum-phase9-voice-engine/evidence/close/probe.sh p3` after the Group 4 gate build; a `P3 VERDICT RED` is STOP AND SURFACE to the
  user with the table (R-C; Q4's −80 dBFS bar is not moved)". **No later group's gated render is trusted until P-3 reads GREEN.**

---

## Group 5 — the voice (sequential, single task)

### T007 `ProfundumVoice` and its direct-voice cases (FR-001–FR-016; plan S5, S9.4–S9.7, C-4, C-5, C-6, C-10, C-11, C-18, C-20; deviation 2)

**Files:** `dsp/include/krate/dsp/systems/profundum_voice.h`, `dsp/tests/unit/systems/profundum_voice_test.cpp` (first cases), run-log `T007.txt`.

Tests first, in `profundum_voice_test.cpp`, driving `ProfundumVoice` directly: `prepare(48000.0, A_s)`; a test-side `BassStereoField` prepared
at 48 kHz / 32 with `advanceControl()` per interval; `IntervalInputs` built as `{ .state = &s, .coreControls = <s.recipe copy-then-assign>,
.foldAmount = s.foldAmount, .anchorLevel = s.anchor.level, .field = &field, .forceLinearCore = false }`; `fRes` a 32-float array at the note
frequency; a test observer (subclass of `ProfundumEngineObserver`) attached through `setObserverSlot(&obs, 0, &seams)` that copies each
`onVoiceChunk` span into vectors.

- `ProfundumVoice_PhasePolicyMapping` `[fr-010]`: `STATIC_REQUIRE` that `NoteMemory::PhasePolicy`, `ProfundumCore::RetriggerPhase` and
  `AnchorOscillator::RetriggerPhase` agree on `Reset == 0` and `FreeRunning == 1` (the header carries the same `static_assert`s).
- `ProfundumVoice_OutputIdentity` `[fr-015][sc-019]`: one note (FromSilence) at E1 on A_s + Anchor level 1, 1 s, then a `Tail` transition with
  Note Memory `AlwaysRetrigger` / `Reset` at 0.5 s (so a dip runs): for every sample and channel, the sum contribution equals bitwise
  `(c[i] * gAmp[i]) * gD[i] + (a[i] * gAmp[i]) * gD[i]` from the voice taps (sum buffers zeroed per chunk by the test); at least one sample has
  `gD[i] != 1.0f` (the dip ran).
- `ProfundumVoice_FeedSkipAndRamp` `[fr-003][c-10]`: g = 0 in every interval: the `feed` tap is +0 everywhere and the core and Character taps are
  bitwise equal to a second voice with `anchorDetached` on; then g steps 0 → 1 between two intervals: inside that interval
  `feed[k] == feedFrom + (feedTo − feedFrom) * float(k + 1) / 32.0f` bitwise, `feed[31] == 1.0f`.
- `ProfundumVoice_A1MatchesUnbiasedH1` `[fr-005][c-6]`: for 20 intervals with the recipe stepping (`depth` 0.1 → 0.9) and f changing (E1, A1,
  C3): the `a1` tap equals bitwise `MassDynamicsTest::unbiasedH1Amplitude(s.recipe, fAnchor[0], 48000.0)`.
- `ProfundumVoice_ChangeOnlySetters` `[fr-013][c-20]`: the call log (`onCall`) over 200 unchanged intervals after the first: no record with
  `issued == true` except the per-interval steps that the plan marks always-issued (none in S5.4 — every step is change-only); the first
  interval after `prepare` / `reset` issues every step.
- `ProfundumVoice_MeterBounded` `[fr-008][c-5]`: f_res = 50 kHz (note 127 + a 24 st bend, above fs/2), 2 s at 44.1 and 48 kHz: `meterTheta` in
  [0, 1) at every chunk, `meterFreqHz` == `SpectralShapeRecipe::capFrequency(fs)` (the clamp), output finite (bit test).
- `ProfundumVoice_IdleNotActive` `[fr-015]`: after a release and the envelope's end, `active()` is false and `audible()` false; while a dip
  is pending, `active()` is true.

Implementation: plan S5.1–S5.7 verbatim, with S9.4 (pitch), S9.5 (feed ramp), S9.6 (`HarmonicBalanceMeter`, nested, in double, f clamped to
`[ProfundumCore::kMinF0Hz, capHz_]`, hold at f/fs ≥ 0.5, `θ −= std::floor(θ)`), S9.7 (a₁ spans of exactly `kNumPartials`); the transition flow is
the Phase 4 harness's `applyTransition` (`profundum_note_behaviour_test_helpers.h:541–592`) as S5.5 transcribes it; the steal fade is 1.0f
(C-4) and the matching seam suppresses the dip; per-sample Anchor calls only with `anchorPhaseTap`; observer callbacks only when the slot is
set. Includes exactly plan S2's list for `profundum_voice.h`. Re-open each component header before each call (plan S0.1 anchors). Verify: the
Group 5 gate.

---

## Group 6 — the engine (sequential, single task)

### T008 `ProfundumEngine` and its direct cases (FR-020–FR-032, FR-060–FR-066; plan S6, S8, C-1, C-2, C-9, C-11, C-12, C-14, C-15, C-19, C-20; deviation 3)

**Files:** `dsp/include/krate/dsp/systems/profundum_engine.h`, `dsp/tests/unit/systems/profundum_engine_test.cpp` (first cases), run-log
`T008.txt`.

Tests first, in `profundum_engine_test.cpp`, driving the engine directly (`auto e = std::make_unique<ProfundumEngine>(); e->prepare(48000.0);`,
endpoints from `BassMorphTest::showcasePair()` with A → A_s, `processBlock` in host blocks of 64, notes issued between blocks):

- `VoiceEngine_StaticContract` `[fr-020][fr-025][sc-022]`: `STATIC_REQUIRE(ProfundumEngine::latencySamples() == 0)`; `kMaxVoices == 8`,
  `kDefaultPolyphony == 4`, `kControlInterval == 32`, `kMaxQueuedEvents == 256`; `static_assert(noexcept(...))` for every audio-thread method
  of plan S6.2 (`noteOn`, `noteOff`, `setPitchBend`, `setTempoBpm`, the morph surface, every FR-031 setter, `setMacro(s)`, `processBlock`,
  `droppedEvents`); `reset()` is not in the list (control-thread, S6.4).
- `VoiceEngine_GetterReadBack` `[fr-066][fr-031][fr-030]`: every setter of S6.2 with an in-range value, then its getter returns it bitwise
  (enums equal); `setPolyphony(0)` → 1, `setPolyphony(9)` → 8; `setPitchBendRange(30)` → 24; `setOutputCeilingDb(3)` → 0, `(−20)` → −12;
  `getEndpoint(e)` equals the state passed to `setEndpoint` (`BassMorphTest::bitEqual`), `morphPosition()` the position set.
- `VoiceEngine_RendersNonSilent` `[fr-020]`: MONO default, a held E1 for 1 s on A_s: output finite (bit test) and RMS over [0.1 s, 1 s] ≥ −40
  dBFS; `processBlock` overwrites (a buffer pre-filled with 7.0f is fully overwritten).
- `VoiceEngine_EventsLatchedToGrid` `[fr-063][c-9]`: a note-on issued after 5 samples of a block vs at the start of that interval: outputs
  bitwise equal (the event applies at the next grid point either way); 300 note-ons in one interval → `droppedEvents() == 44`, and every
  note-off issued afterwards is honoured (no voice left sounding after their releases end).
- `VoiceEngine_ReprepareKeepsSettings` `[fr-064]`: set non-default polyphony, ceiling, macros, Note Memory mode, bend range; `prepare(96000.0)`:
  every getter returns the same value.
- `VoiceEngine_PostSumPlacementRuns` `[fr-014][c-14]`: `ProfundumEngineConfig{.placement = ProfundumNonlinearPlacement::PostSum}`, POLY 4 notes
  for 1 s: output finite, RMS ≥ −40 dBFS; MONO with PostSum renders bitwise equal to MONO PerVoice (C-14: MONO always PerVoice).

Implementation: plan S6.1–S6.11 verbatim (S6.1 config types here; `ProfundumEngineSeams` from the observer header), S8 tap / record emission,
S9.1–S9.3 / S9.8 values, the S6.5 grid step in FR-026's order (steps 2–8 never reordered), `coreControlsFor` copy-then-assign, C-7's compression
expression, the C-2 / S6.10 SOLID re-sync with `dcR_ = dcL_` and a `DcResync` record, C-14's PostSum path, C-15's chunking with every component
prepared at `maxBlockSize = 32`, `TruePeakLimiter::setCeilingDb` and `NoteProcessor::setPitchBendRange` guarded here (no finite guard in them),
seeds per C-19 (`deriveStreamSeed`, `core/random.h:96–110`). Includes exactly plan S2's list for `profundum_engine.h`. The engine never calls
`ProfundumMotion::output()`'s virtuals. Verify: the Group 6 gate.

---

## Group 7 — the render fixture (sequential, single task)

### T009 `profundum_voice_engine_test_helpers.h` and its self-tests (plan S10.2, S9.9, S9.11; C-8; deviation 11)

**Files:** `dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h`, `dsp/tests/unit/systems/profundum_voice_engine_helpers_test.cpp`
(append), run-log `T009.txt`.

The header (namespace `Krate::DSP::ProfundumTest`, every name `VoiceEngine…` / `voiceEngine…`; includes `bass_morph_test_helpers.h`,
`bass_morph_state_helpers.h`, `profundum_core_test_helpers.h`, `anchor_oscillator_test_helpers.h`, the engine header, `allocation_detector.h`,
`enable_ftz_daz.h`, `artifact_detection.h`):

- `VoiceEngineEvent { std::uint64_t sample; VoiceEngineEventKind kind; std::uint8_t note; std::uint8_t velocity; float value; int index; }` with
  kinds NoteOn, NoteOff, PitchBend, Tempo, MorphPosition, MorphRampMs, AnchorMorphMode, VoiceMode, Polyphony, Macro, NoteMemoryMode,
  NoteRetrigger, PortamentoTimeMs, PortamentoMode, GlideMode, MonoPriority, Legato, Endpoint (index = 0 / 1 into the script's `endpointStates`);
  `VoiceEngineScript { double fs = 48000.0; std::size_t hostBlock = 64; std::uint64_t totalSamples; std::uint32_t seed =
  ProfundumEngine::kDefaultSeed; ProfundumEngineConfig config{}; MorphState a, b; std::vector<MorphState> endpointStates; float position = 0.0f;
  ProfundumMacroValues macros{}; ProfundumEngine::VoiceMode mode = Mono; std::size_t polyphony = 4; std::vector<VoiceEngineEvent> events; bool
  attachObserver = true; }`.
- `VoiceEngineTaps : BassMorphTest::MorphChainTaps` adding: per-voice vectors (`std::array<VoiceTapSet, ProfundumEngine::kMaxVoices>` with
  core / char / anchor L R, gAmp, gD, fRes, fCore, fAnchor, anchorPhase, feed per sample; followRatio, a1, anchorLevel, halfWave, meterTheta,
  meterFreqHz per chunk), the grid records (copied `Resolved`, `afterMacros`, `afterMotion` values), the call log, and the engine taps (sum,
  dyn, field, dc, out). The inherited `outL/R` hold the **output** tap; `characterL/R` the summed Character tap; `linearCoreL/R` filled by the
  twin runner.
- `VoiceEngineObserver final : ProfundumEngineObserver` appending into **preallocated** vectors (reserved from `totalSamples` before any
  `AllocationScope`).
- `VoiceEngineTaps voiceEngineRender(const VoiceEngineScript&)`: `std::make_unique<ProfundumEngine>()`, set seed / config / endpoints / macros /
  mode / polyphony, `prepare(fs, config)`, then render in host blocks, **splitting each block at every event's sample** and issuing the event
  before rendering that sample (C-9).
- `voiceEngineRenderWithTwin(script)`: renders the script and a twin with `config.seams.forceLinearCore = true`; copies the twin's per-voice
  core tap into `linearCoreL/R` (summed, or of one voice by argument), so `BassMorphTest::dNlDb` reads D_nl unchanged (FR-086).
- `asFieldTaps(taps, VoiceEngineTap t)` copying Sum / Dynamics / Field / Output into `outL/R` for the Phase 8 readers.
- `voiceEngineAs(const MorphState& a)` → A_s (C-8) and the note constants of rule 8.
- **Click machinery (S9.9):** `voiceEngineWindow(steal grid point, fade end, f0old, f0new, fs)` (W per SC-005); `voiceEngineDeltaRatio(y, ref,
  W)` = max |Δy| over W of y over that of ref; `voiceEngineDeltaBound(steady renders, S_max)` = 1.1 · (G_max·D_max + S_max·P_max) per S9.9, with
  the dip slope `π / (2·L_d)`, `L_d = ceil(ProfundumVoice::kDeclickMs · fs)` = 3 ms (ruling R-B, 2026-10-20), ported from `t22DeltaBound` / `t22DeclickSlope` (cite their lines); the Drop and
  glide terms of S9.9.
- **Drop descriptors:** `voiceEngineDropDescriptors(d_core, A, N)` → {start, t_s, α, ε, ρ}, ported from `t22CoreDescriptors` (`:2005`).
- **Frame-wise readers (S9.11):** frame = 8 whole periods, hop 960, `E_sub`, `D_nl` via P_{≥9} = P_total − P_DC − Σ_{n=1..8} P_n, `L_K` via
  `TestUtils::kWeightedLevelDb`, and the boxcar attenuation `|sinc(π·f_m·T_frame)|`.

Self-tests, appended to `profundum_voice_engine_helpers_test.cpp`:

- `VoiceEngineHelpers_DriverSplitsAtEvents` `[fr-063]`: a script with events at samples 5, 37, 100 rendered at host block 64 and 4096: output
  taps bitwise equal; the call log's `Events` records fall on grid multiples (32, 64, 128).
- `VoiceEngineHelpers_TwinDiffersOnlyInExcitation` `[fr-086]`: on A_s + excitation mix 1, the twin's grid-record `afterMotion` states differ from
  the main render's only in `excitation.mix` (0 in the twin's handed core controls), and the main render's core tap differs from the twin's.
- `VoiceEngineHelpers_FrameReaders` `[fr-086]`: a synthetic steady 41.20344 Hz sine at 0.5 amplitude: every frame's E_sub equals
  `BassMorphTest::eSubDb` of the same signal within 0.01 dB; a sine plus its 10th harmonic at −20 dB re h1, read against a sine carrying its
  10th harmonic at −60 dB re h1 as the "twin": frame D_nl reads 40 dB within ±0.1 dB (the analytic
  value); the boxcar attenuation at 0.1 Hz with an 8-period E1 frame reads 0.999380 within 1e-6.
- `VoiceEngineHelpers_DropDescriptorsKnownCurve` `[fr-086]`: the ported descriptors on an analytic d[n] = 24·e^{−n/(0.05·48000)} (A = 24 st,
  τ = 50 ms, 48 kHz): start == 24 exactly; t_s (the 1-cent threshold of `t22CoreDescriptors`, `:2004`) within 2 samples of
  τ·ln(24 / 0.01)·fs = 18 679.7 samples; α equal (within 1e-12) to the sample mean of d / A over [0, round(N)] recomputed in the test.
- `VoiceEngineHelpers_DeltaBoundOnKnownSignal` `[fr-086]`: the S9.9 bound on a pure sine with a raised-cosine 72-sample dip at 48 kHz bounds every
  |Δy| of that signal (margin ≥ 1.0), and the same signal with the dip replaced by a hard step to 0 exceeds it.

Verify: the Group 7 gate.

---

## Group 8 — fast-lane criteria through the engine (parallel; each task owns one TU)

### T010 [P] Voice criteria through the engine (SC-019, SC-020, SC-033, SC-034; FR-006, FR-008; plan S10.3, S10.6; deviation 2)

**Files:** `dsp/tests/unit/systems/profundum_voice_test.cpp` (append), run-log `T010.txt`.

- `VoiceEngine_AnchorJoin` `[sc-019]`: (a) `anchorDetached` vs attached at g = 0, A_s + Anchor level 1, E1, 2 s: per-voice core and Character
  taps bitwise equal; (b) one voice sounding: per sample and channel the Sum tap equals bitwise `(c·g_amp)·g_d + (a·g_amp)·g_d` from that voice's
  taps, at g ∈ {0, 0.5, 1} and across an interval where g steps 0 → 1, in a render with one Reset retrigger (AlwaysRetrigger / Reset) and one
  POLY steal (5 notes into 4 voices, the stolen voice read alone after the others have released); intervals with g_d ≠ 1 are counted (≥ 1)
  and tagged in `evidence/sc019-join.txt`; (c) at g = 1 the Anchor tap is bitwise equal to the g = 0 render's; (d) the feed ramp identity of
  `ProfundumVoice_FeedSkipAndRamp`, read through the engine.
- `VoiceEngine_DropRouting` `[sc-020][fr-006]`: a 24 st Drop (time 200 ms), MONO with a 200 ms glide (legato E1 → E2) and a pitch bend ramp
  0 → +1 → −1 at range 2 st during the Drop; and POLY 4 voices (E1, A1, E2, C3) each with the Drop. Reference: a test-side `MonoHandler` (MONO
  only) + `NoteProcessor` + `DropEnvelope` + `DropPhaseLock`, configured identically, driven at the same grid points, computing
  `fRes_ref = processPortamento() * semitonesToRatio(processPitchBend() * range)` (POLY: `midiNoteToFrequency(note) * ratio`) and
  `fCore_ref = fRes_ref * std::exp2(lock.process(drop.process()) / 12.0f)` (C-11), with the lock bypassed in FOLLOW only. Gate, per sample:
  the core pitch tap == `fCore_ref` bitwise; the Anchor pitch tap == `fRes_ref` bitwise in LOCK / PUNCH / GHOST and `fCore_ref` in FOLLOW;
  print the max cent difference (expected 0). Positive controls: `followNoBypass` (FOLLOW) and `lockReadsCore` (LOCK) each produce ≥ 1 sample
  with a non-equal Anchor pitch tap (`CHECK_FALSE(allEqual)`).
- `VoiceEngine_DropLatchedAtTrigger` `[fr-006]`: a held E1 with a 24 st Drop; 50 ms after note-on the morph (5 ms ramp) moves Drop Amount 24 → 12,
  Time 200 → 60, Shape 0 → 1, Rebound 0 → 1 and CYCLE → Depth is active: that note's `fCore` tap is bitwise equal to a run without the change;
  the next note's Drop trajectory differs from the old one (max |Δf_core| > 1 cent).
- `VoiceEngine_RectifySymmetry` `[sc-033][fr-008]`: states whose Character-input h2/h1 (offline Goertzel on the mid of the Fold output, i.e. the
  per-voice Character-input tap, over 8 whole periods) reads −30, −25, −15 and −10 dB, each within ±0.5 dB, found by a bisection on Fold amount
  (then excitation asymmetry) in a TU-local helper: the `halfWave` tap is true at −30 and −25, false at −15 and −10, from either previous flag
  value. Hysteresis: a −20 dB state reached from the −25 state keeps true, and from the −15 state keeps false, over ≥ 20 periods each.
  Phase sync: `SyncPhase` records with `issued == true` occur exactly at the Reset-branch effective samples and the first note after prepare /
  reset (compared with the list the test computes from its events and the `samplesUntilSwitch` of each dip); at each, the core tap's
  `ProfundumTest::goertzelPhase` over 4 periods equals that of a fresh prepared `ProfundumCore` given the same Reset note-on within 1°, under
  both retrigger policies.
- `VoiceEngine_PitchBend` `[sc-034][fr-024]`: full bend +1 and −1 at ranges 2 and 12 st, after the bend smoother settles: f_res moves by the
  range within 0.1 cent, in MONO and POLY (4 voices, each voice's f_res).
- `VoiceEngine_MeterBoundedAtTopNote` `[fr-008][c-5]`: note 127, bend range 24, bend +1, 2 s at 44.1 and 48 kHz, through the engine: per chunk
  `meterTheta` ∈ [0, 1), `meterFreqHz` == `SpectralShapeRecipe::capFrequency(fs)`, output finite, and no `HalfWave` call issued inside any period
  shorter than 2 samples.

### T011 [P] Engine criteria (SC-008, SC-011 in-process, SC-012, SC-022, SC-024, SC-026, SC-027, SC-028, SC-029, SC-031, SC-032, SC-036; FR-005, FR-021, FR-028, FR-030, FR-045; deviations 3, 6)

**Files:** `dsp/tests/unit/systems/profundum_engine_test.cpp` (append), run-log `T011.txt`.

All through `ProfundumTest::voiceEngineRender`. The **SC-011 schedule** = a 6-event `kDeterminismSchedule` shape (Vorago
`vorago_engine_test.cpp:1418`): note-ons E1 @ 0, C3 @ 0.25 s, E2 @ 0.5 s, note-offs @ 1.0 / 1.25 / 1.5 s, 3 s total, plus a morph ramp 0 → 1
(500 ms) at 0.2 s and CYCLE → Depth on B.

- `VoiceEngine_MacroNeutral` `[sc-008]`: (a) all four macros 0.5 vs `macroBypass`: output tap bitwise equal over the SC-011 schedule, MONO and
  POLY; (b) macros {0.2, 0.8, 0.3, 0.9} set by an event at every interval vs once at sample 0: bitwise equal.
- `VoiceEngine_DeterminismHarness` `[sc-011][fr-062]`: MONO and POLY: two engine instances, and two runs of the same instance after `reset()`,
  bitwise equal; seed `0x1111u` vs `0x2222u`: with DRIFT on (Motion source Drift → Depth, amount 1, depth 1) the residual
  `20·log10(rms(a − b) / rms(a))` > −60 dB; with DRIFT off, bitwise equal (field Drift off too).
- `VoiceEngine_Partition` `[sc-012][fr-063]`: the SC-011 schedule plus a pitch-bend ramp and a tempo change, host blocks {1, 7, 32, 64, 511, 4096}:
  outputs bitwise equal.
- `VoiceEngine_RealTimeSafety` `[sc-022][fr-060]`: the observer detached; prepare outside the scope; inside one `TestHelpers::AllocationScope`:
  10 s of notes (MONO and POLY), 5-into-4 steals, two MONO ↔ POLY switches, morph / macro / Motion automation, Note Memory changes, bend, tempo,
  one `reset()`: allocation count 0; `droppedEvents() == 0`.
- `VoiceEngine_SampleRates` `[sc-024][fr-064]`: 22050 / 44100 / 48000 / 96000 / 192000: held E1 on A_s, output finite and RMS over 1 s after a
  100 ms settle ≥ −40 dBFS; the `MorphAdvance` records fall every 32 samples; a re-`prepare` at a new rate inside `AllocationScope`: 0
  allocations (C-15).
- `VoiceEngine_MotionDepthZero` `[sc-026]`: every source × target at Motion Depth 0 (amount 1) vs Motion Off, the SC-011 schedule: output
  bitwise equal (20 renders + reference).
- `VoiceEngine_MassSplit` `[sc-027][fr-027]`: (a) macros neutral, Mass automated 0 → 100 over 1 s and the morph ramping: per interval the
  handed main-voice core depth / body, fold amount and the `MassDynamics` duration / compression / trim (grid-record `targets`,
  `compressionHanded`) equal bitwise the fields of `MassLaw::evaluate(smoothedMass, base)` recomputed from the grid record (compression per C-7;
  the Tight ↔ Loose 0.2 arm checks `clamp(t.compressionAmount + c_TL, 0, 1)` with `c_TL = compressionContribution()`); (b) Anchor morph Lock,
  Motion Off: the handed Anchor level is bitwise constant at its p = 0 value while Mass and p move; (c) Lock with CYCLE → Mass (0.5 Hz, amount 1,
  depth 1): bitwise constant; (d) Blend and Punch with the same automation: per interval the handed level equals bitwise
  `MassLaw::evaluateRow(<AnchorLevel row>, resolved.anchorMass + o.mass, base.anchorLevel)` (row located by `row.target ==
  MassLaw::Target::AnchorLevel`), and its peak-to-peak over one 2 s period > 0.
- `VoiceEngine_MorphGlobal` `[sc-028][fr-030]`: 4 voices, a morph ramp: every voice's handed state in each interval comes from the one `Resolved`
  (the per-voice `CoreControls` / `AnchorControls` call values equal across voices where the voice state does not enter); `MorphAdvance`
  records == ⌈n / 32⌉ and all on grid multiples.
- `VoiceEngine_ModeSwitch` `[sc-029][fr-023]`: MONO ↔ POLY with held notes: after the longest release every voice is idle (`gAmp` 0), no stuck
  voice after the later note-offs; click gate by the SC-005 form (`voiceEngineDeltaRatio` ≤ 10^(0.5/20) against the reference = the same render
  without the switch and with the notes released at the switch's grid point; `voiceEngineDeltaBound` holds over W from the switch to the end of
  the longest release); `differentialClicks` recorded.
- `VoiceEngine_StealOrder` `[sc-031][fr-022]`: 4 voices, a seeded (`0x5031u`) 1000-event schedule of note-ons / note-offs; at each `Steal`, the
  victim equals a test-side model (oldest Releasing by note-on time, else oldest Active).
- `VoiceEngine_CallOrder` `[sc-032][fr-026]`: one interval's call log, MONO and 4 voices, equals exactly the S6.5 / S5.4 sequence (Events,
  MorphAdvance, Macros, Motion, LawSetBase, LawSetMass, LawAdvance, FieldSetControls, FieldAdvance, [DcResync], per voice VoicePan … AmpCurve,
  then DynDuration … DynKey), compared as a vector of `ProfundumCall`.
- `VoiceEngine_TapTransparency` `[sc-036][fr-086]`: the SC-011 schedule, MONO and POLY: observer attached with seams off, and with
  `anchorPhaseTap` on, each bitwise equal to the no-observer render; no observer: `AllocationScope` 0 over 3 s.
- `VoiceEngine_A1MatchesUnbiasedH1` `[fr-005]`: MONO and 4 voices, a morph ramp plus CYCLE → Depth: per voice and interval the `a1` tap equals
  bitwise `MassDynamicsTest::unbiasedH1Amplitude(afterMotion.recipe, fAnchor[0], fs)`, in LOCK and FOLLOW.
- `VoiceEngine_MonoPriorityAndPortaMode` `[fr-021]`: overlapping keys under LastNote / LowNote / HighNote: the f_res tap equals bitwise a
  standalone `MonoHandler` fed the same events times the bend ratio; `PortaMode::LegatoOnly` glides only for overlapped notes, `Always` for both.
- `VoiceEngine_HostTempo` `[fr-028]`: CYCLE `cycleSync` on (1/4) and a synced field Motion, WIDE, one held E1: the CYCLE period (zero crossings of
  `motionSource`) and the field Motion period (side-channel level period of the Field tap) track 90 / 120 / 150 BPM within one interval; with no
  `setTempoBpm`, both equal the 120 BPM period.
- `VoiceEngine_MorphRuntimeRoundTrip` `[fr-030]`: mid-ramp, `serializeMorphRuntime` equals byte for byte a standalone `BassMorphEngine` driven by
  the same calls; restoring into a second engine with the same endpoints gives a bitwise-equal output thereafter; a corrupt blob (one byte
  flipped in the version) returns false and changes nothing.
- `VoiceEngine_PolyEnvRetrigger` `[fr-045]`: POLY, Motion ENV: a second note on another voice while the first is held restarts the source
  (`motionSource` returns to its attack start at that grid point); a Continue / non-restarting cell does not.

### T012 [P] Non-finite safety (SC-023; FR-061)

**Files:** `dsp/tests/unit/systems/profundum_engine_nonfinite_test.cpp`, run-log `T012.txt`.

- `VoiceEngine_NonFinite` `[sc-023][fr-061][nonfinite]`: for each float setter of S6.2 (pitch bend, tempo, morph position, ramp ms, portamento
  time, bend range, ceiling, each macro) and each endpoint leaf class reachable through `setEndpoint` (the engine relies on `BassMorphEngine`'s
  own validation): NaN (`0x7FC00000u`), +Inf, −Inf and a denormal (`0x00000001u`) built with `std::bit_cast`, issued mid-render of a held E1
  on B: every output sample finite (bit test `(bits & 0x7F800000u) != 0x7F800000u`); the getter returns the previous value bitwise (denormal:
  the clamped or kept value the setter documents); the output is bitwise equal to a render without the bad call for the non-finite cases.

---

## Group 9 — R-A / R-B checkpoint, FOLLOW flip and the H-13 records (single task; STOP AND SURFACE)

### T013 Probes P-1, P-2, P-4 and `VoiceEngine_CharacterRecords` (plan S10.4, R-A, R-B, H-13; deviations 4, 10)

**Files:** `dsp/tests/unit/systems/profundum_engine_probe_test.cpp` (append), `specs/profundum-phase9-voice-engine/evidence/close/probe.sh`
(extend), run-log `T013.txt`.

- `VoiceEngine_CpuProbe` `[systems][profundum][voice-engine][.probe][.perf]` (P-1): the SC-001 setting (excitation 1 / 1, SHAPE 0.5, Fold 100,
  Character Tube and Fracture at Amount 1, Anchor FOLLOW, feed 1, Drop 24 st every 250 ms, CYCLE → Mass depth 1, WIDE with field Motion,
  Mass 100, the morph ramping 0 ↔ 1 on the showcase pair, block 64), MONO and 4 voices (E1 / A1 / E2 / C3), 10 s timed after 1 s warm-up,
  median of 3, % = wall / audio time; then the deviation-10 test-side replica with one stage removed at a time (core + excitation, Shape,
  Fold, Character, Anchor, pitch stage, a₁ + meter, amp env, MassDynamics, field stage, DC + limiter, morph + law + Motion + macros), and
  PostSum at 4 voices. Print `P1 MONO <tube%> <fracture%>`, `P1 POLY4 <…>`, `P1 POSTSUM4 <…>`, the breakdown, and
  `P1 VERDICT GREEN|RED`: GREEN iff max(MONO) ≤ 8 and (max(POLY4) ≤ 25 or max(POSTSUM4) ≤ 25). Write `evidence/probes/p1-cpu.txt`.
- `VoiceEngine_DeclickReferenceProbe` `[.probe]` (P-2): SC-005(a) / SC-006(a) on the worst cells — A_s, E1 and C3, Reset, held victim, SOLID,
  host block 1 — `voiceEngineDeltaRatio` against the polyphony-5 (SC-005) and POLY-2 (SC-006) references, and the (b) margin. Print per cell
  and `P2 VERDICT GREEN|RED`: RED iff any cell fails (a) while (b) passes. Write `evidence/probes/p2-declick.txt`.
- `VoiceEngine_FollowFlipProbe` `[.probe]` (P-4): sweep Amount 0 → 1 in 101 steps for Rectify (half-wave-symmetric input: Fold 0, excitation 0,
  a sine-like recipe so `halfWave` is true) and Fracture, at E1 and C3: the follow-ratio tap per step; print the first Amount at which
  `perceivedPitchRatio()` ≠ 1 per (algorithm, note), or "NO FLIP". Write `evidence/probes/p4-follow-flip.txt`. If neither stage flips at any
  Amount, write "FOLLOW INERT" with the table (SC-018 / AT-13 then carry the flag; roadmap 688–689).
- `VoiceEngine_CharacterRecords` `[.probe]` (H-13 Record): E0 / E1 / E2 / C3 × six algorithms at Amount 1, Fold 0 (so the Character input is
  the Shape output): complex Goertzel h1 of the Character tap over the Character input (gain dB, phase); the group delay at f0 from the phase
  across f0 ± 1 % notes; the growl level swing (L_K of the Character tap with Amount 0 across SHAPE 0 … 1 in 11 steps). Write
  `evidence/h13-character-records.txt` beside Phase 5's figures (`specs/profundum-phase5-shape-character/evidence/sc015-cpu.txt` and its
  compliance row).
- `probe.sh`: add cases `p1` (run **alone, pinned**: refuse if any build / test / lane runs; `pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe
  <exe> -ExeArgs "VoiceEngine_CpuProbe"`), `p2`, `p4`, `h13` (each by name to its own log under `evidence/run-log/close/`), and `all-but-p1`
  running p2 / p4 / h13 concurrently.
- **Executor:** write the cases and the script, `bash -n` it, return done with notes "deferred to the main loop: after the Group 9 gate build,
  run `probe.sh all-but-p1`, then `probe.sh p1` alone. `P1 VERDICT RED` → STOP AND SURFACE with the breakdown (R-A stop rule: options are L3
  with a user ruling, an append-only cost fix in a Phase 2 / 5 component with a ruling, or another ruling; a ceiling is never relaxed).
  `P2 VERDICT RED` → STOP AND SURFACE with the readings (R-B — **ruled 2026-10-20: the voice dips over 3 ms via the append-only `RetriggerDeclick::prepare(fs, ms)`; a (b) failure is a DEFECT, fixed in the main loop by the switch-sample chain reset, FR-011**). P-4 'FOLLOW INERT' is recorded and
  carried to T016 / T017." **No Group 11 gated render is trusted until P-1 and P-2 read GREEN.**

---

## Group 10 — harvests (sequential, single task; edits the macro header between its markers)

### T014 Macro-row harvest, the SC-011 fingerprint, `VoiceEngine_FoldPathRecord` (plan S10.5, H-29; deviations 6, 12)

**Files:** `dsp/tests/unit/systems/profundum_engine_probe_test.cpp` (append), `dsp/tests/unit/systems/profundum_engine_test.cpp` (append),
`dsp/include/krate/dsp/systems/profundum_macro_matrix.h` (between the `HARVEST` markers only, by the paste script), `evidence/close/harvest.sh`,
`evidence/close/paste-harvest.js`, run-log `T014.txt`.

- `VoiceEngine_MacroHarvest` `[.harvest]`: per macro, starting from the S4.2 seed amounts, raise |amount| (signs fixed) in steps of 25 % of the
  seed (and, if the row stays below its bar at 3× the seed, try `Exponential` then `SCurve`) until the AT-47 … AT-50 row (the SC-007 axis
  descriptor, macro 0 → 1, others neutral, base A_s — **B_s for Dark ↔ Bright, ruling B10-1; its SHAPE row is dropped (amount 0), ruling B10-2**) passes its family bar at E1 and C3 **and** the 21-step SC-007 sweep is monotone within ε;
  Weight → E_sub (Sum tap) ≥ 3 dB; Dark ↔ Bright → D_br (Sum tap) ≥ 0.25 oct; Tight ↔ Loose → D_t ratio ≥ 2; Clean ↔ Violent → D_nl ≥ 3 dB. Write
  the 16-row table in the S4.2 format to `evidence/macro-rows.txt` and the C++ initialiser lines to `evidence/macro-rows.inc`. A macro whose row
  cannot pass at 4× the seed with any of the three curves → write `HARVEST RED <macro>`; that is STOP AND SURFACE, never a bar change.
- `VoiceEngine_FingerprintHarvest` `[.harvest]`: the SC-011 schedule (T011), MONO and POLY; `fingerprintRender` (`render_fingerprint.h`) of the
  output; write the fingerprint initialisers to `evidence/fingerprint.inc`.
- `paste-harvest.js` (the Phase 7 script's structure): replaces the lines between `// HARVEST-BEGIN macro-rows` / `// HARVEST-END macro-rows` in
  `profundum_macro_matrix.h` with `macro-rows.inc`, and between `// HARVEST-BEGIN fingerprint` / `// HARVEST-END fingerprint` in
  `profundum_engine_test.cpp` with `fingerprint.inc`; refuses if a marker pair is missing or a sign differs from the seed table; prints the
  diff. `harvest.sh`: runs both harvest cases (to logs under `evidence/run-log/close/`), then `node paste-harvest.js`.
- `VoiceEngine_DeterminismFingerprint` `[sc-011]` (in `profundum_engine_test.cpp`, deviation 6): the SC-011 schedule's fingerprint compared with
  the harvested constants between `// HARVEST-BEGIN fingerprint` / `// HARVEST-END fingerprint` through `compareFingerprints` at
  `kSampleTolerance` (5e-4). The executor writes the markers with an empty initialiser and a `FAIL("not harvested")` guard; the main loop
  runs `harvest.sh` (which pastes the constants) and rebuilds **before** the Group 10 gate, so no gate ever runs the unharvested case
  (deviation 6). The guard stays as the defence against a lost paste.
- `VoiceEngine_FoldPathRecord` `[sc-011]` (in `profundum_engine_test.cpp`; H-29 Record): one held E1 note reaching Fold 100 by two paths (held at
  100 from the note-on vs a morph ramp 0 → 100 over 500 ms): the steady level and h1 differences in dB of the Character tap over [3 s, 4 s];
  written to `evidence/h29-fold-path.txt` beside Phase 8's ≈ 0.13 dB. Recorded, not gated (the readings are checked finite by the bit
  test, never `std::isfinite`).
- **Executor:** write the cases and scripts, `bash -n` / `node --check`, return done with notes "deferred to the main loop: after the Group 10
  build, run `bash specs/profundum-phase9-voice-engine/evidence/close/harvest.sh`, rebuild once, then run the Group 10 gate. A `HARVEST RED` is
  STOP AND SURFACE".

---

## Group 10b — CPU optimisation (ruling R-A, 2026-10-20; run from the MAIN LOOP, not by the workflow dispatcher)

Probe P-1 (alone, pinned, SC-001 setting, median of 3; `evidence/probes/p1-cpu.txt`, `p1-cpu-static-morph.txt`,
`p1-cpu-static-all.txt`, `p1-cpu-static-breakdown.txt`):

| Operating point | Measured | Bar | Morph held static |
|---|---|---|---|
| MONO, everything on | 13.6 % | 8 % | 10.5 % |
| 4 voices, per-voice placement | 46.3 % | 25 % | 31 % |
| 4 voices, post-sum placement | 53.0 % | 25 % | — |

Static-morph replica breakdown (Tube; % of one core; POLY4 per voice in brackets): core + excitation 4.3 (3.8), field
stage + pan + WIDE 4.05 (3.8), Character 3.7 (3.5), Mass dynamics 2.8 (1.2 global), Shape 2.0 (1.9), DC + limiter 1.9
(0.65), Fold 1.2 (1.8), a₁ + meter 1.0 (0.56), pitch 0.85 (0.43), morph + law + Motion + macros 0.67 (1.4). The continuous
morph's control path adds 3 % MONO / 15 % at 4 voices. Every stage sits above its own phase bar; the stereo modes run
the whole per-voice chain unlinked (Phase 5's unlinked figure is 2.1× its linked one) and the engine's per-interval
setter work is paid once per voice.

### T028 Profile and hot-spot list (main loop) — DONE 2026-10-20

**Files:** `evidence/probes/p1-hotspots.md`; no product code.

> Outcome: sampled profile (`wpr -start CPU`, user-elevated; three traces — the first two resolved only the C runtime
> because the Release exe has no PDB, the third ran a `/DEBUG`-linked copy from `build/windows-x64-prof` with the same
> `/O2 /Ob2` codegen) + the replica breakdown + the lever table, ranked in `evidence/probes/p1-hotspots.md`: halfband
> oversampling 23 %, CRT scalar math 17.8 % (F1 twice per sample 7.7, Fold sine 4.5, pan trig 2.5), limiter IIR
> upsampler 4.7 %, bank continuity pass 3.7 %; the replica's "field" row is the unlinked-stereo cost.

- Measure, under the SC-001 setting with the morph static and ramping, where the time goes inside each stage: the
  replica's per-stage removal (done), plus per-function timing or a sampling profile (`wpr -start CPU -filemode` needs an
  elevated shell; the main loop's unelevated attempt failed with 0xc5585011). Separate per-sample audio work from
  per-interval control work (setter calls, recipe evaluation, coefficient updates, pan vectors) and linked from unlinked
  L / R processing.
- Deliverable: a ranked list of hot spots with the mechanism and the candidate fix for each, the predicted saving, and
  whether the fix is engine-internal or a Phase 1–8 header (append-only / bit-identical, with its gate named).

### T029 Fixes (main loop, or speckit-implement agents dispatched by the main loop; one agent per header)

**Files:** named per hot spot in T028's list; every shared header touched is added to FR-084's exception list in the spec
and to the SC-030 list, with its gate (the owning phase's TUs plus the `[long]` cases of its include closure).

- Each fix is behaviour-preserving: for a component header the existing TUs must stay green and a bit-identity or the
  phase's own tolerance is asserted before / after on a fixed render; for the engine the Phase 9 fast-lane cases stay
  green. No bar changes anywhere. Candidate levers from the breakdown: one control resolve shared by all voices;
  change-only setters at every interval; the stereo-mode chain's unlinked cost (the WIDE per-channel offsets are
  small — the plan may run Shape / Fold linked where the field has not split the voice); the Character stage's and the
  excitation's oversampler cost; the field's per-partial pan vector work per interval; the limiter's 4× upsampling.
- Build once per batch of independent fixes; run the touched phases' suites concurrently (never the CPU lane).

> **Outcome (2026-10-20).** Batch 1, bit-identical (fingerprint harvest digits unchanged, t029-batch1-gates.log;
> owner suites green: `[tanh_adaa],[polyphase-halfband]` 46 cases, bank / fold / residual-path 103 cases, the
> Profundum per-push lane 467 / 468 with only the stale T017 fingerprint red): `tanh_adaa.h` F1-of-previous-sample
> cache; `harmonic_oscillator_bank.h` continuity-pass dirty flag; `oversampled_residual_path.h` `lastLinearBase()` +
> `shape_morph.h` HOLLOW tap from it (second decimator removed); `polyphase_halfband_oversampler.h` branch chains
> interleaved. Paired, pinned, static morph: MONO 13.6 → 11.4 %, POLY4 42.2 → 34.7 % (p1-t029-batch1-ab.log).
> Batch 2: `true_peak_limiter.h` append-only `setHalfbandDetector(bool)` (default off; the engine opts in; new case
> in `true_peak_limiter_test.cpp`); `harmonic_fold.h` `foldSine` (quadrant reduction + Cephes pair, ≤ 2 ulp, Phase 2
> bars gate it). Paired: MONO 10.9 → 10.1 %, POLY4 within noise (p1-t029-batch2-ab.log). Not taken: the bank's pan
> trig (shared, not bit-identical under an approximation), the per-sample f₀ re-keys (cadence is a Phase 9 row), the
> lock-step L / R rewrite of the stage bodies (decided on T030).

### T031 Lock-step L / R through the oversampled stages (main loop; ruled by the user 2026-10-20 on T030's idle RED)

**Files:** `primitives/polyphase_halfband_oversampler.h`, `processors/oversampled_residual_path.h`,
`processors/harmonic_fold.h`, `systems/shape_morph.h`, `systems/bass_character_stage.h`, `systems/profundum_core.h`
(pair entry points, append-only; the single-channel ones stay and keep their bits); evidence `run-log/close/p1-t031-*.log`.

- Mechanism: in SPREAD / WIDE the voice's channels run one after the other through four 4× residual paths and their
  nonlinear bodies; each body is a long serial dependency chain (first-order allpass sections, ADAA, biquads) that the
  core's out-of-order window cannot overlap with the other channel's. A pair entry steps both channels' state objects
  in one loop, statement by statement, so the two independent chains fill each other's latency. Each lane keeps
  exactly its own operation sequence — bit-identical per lane by construction, no reassociation, no contraction.
- Order (smallest body first, paired static-morph A/B after each, the owner's suites + the fingerprint digits as the
  gate): (1) halfband pair helpers; (2) Shape `processChannel` pair; (3) Fold stage pair; (4) Character branch pair
  (Tube, Fracture first — the SC-001 endpoints); (5) core excitation pair. Stop early when a step measures nothing.
- Exit: T030 re-run on the idle machine; the bars stay 8 % / 25 %; the result is reported whichever way it falls.

> **Outcome (2026-10-20 19:10): the premise failed at the primitive, measured before any stage body was touched.**
> Step 1 added pair entries to `PolyphaseHalfbandOversampler` (bit-equality case green at both factors, 20 000 samples,
> stateful hooks) and a hidden bench (two 4× objects, up + nonlinearity + down, 2 M samples, best of 5, pinned):
> scalar sequential **32.4 ns/sample**, scalar pair interleaved **37.0** (1.14×); the stage rewritten with the two
> branches as SSE2 lanes (same sub → mul → add per lane): single calls **35–36 ns**, four-lane pair **38.5–42.9**
> (1.06–1.22× of two singles); with unaligned storage 44.5. The chains are latency-bound and the out-of-order core
> already overlaps a channel's two objects, so neither statement interleaving nor lane packing recovers time, and
> the stage-body rewrite built on it would not pay. Nothing kept: the header and its TU are back at the batch-1
> state (`git diff` shows only the T029 branch interleave). Surfaced to the user.

### T032 Second lever round (main loop; ruled by the user 2026-10-20 after T031 measured dead)

**Files:** `systems/profundum_voice.h` / `profundum_engine.h` (per-interval setter work as the control-path trace ranks
it), `systems/mass_dynamics.h` (a `BassCompressor`-local dB path replacing the series `gainToDb` / `dbToGain` of
`DynamicsProcessor` per sample; Phase 6 tolerance-gated, `[mass-dynamics]`), `processors/harmonic_oscillator_bank.h`
(append-only `setPanCoefficients(left, right)`; the core computes the constant-power pair itself) + `systems/profundum_core.h`
(Phase 1 / 7 TUs gate); evidence `run-log/close/prof/p1-full.etl` (user, elevated, the full probe on the symbolised exe),
`p1-t032-*.log`.

- Order: control-path trace → ranked setter fixes (engine-internal, bit-identical where the call is merely skipped) →
  dynamics dB path → pan coefficients; paired A/B after each; T030 on the idle machine at the end. Expected reach MONO
  ≈ 8, 4 voices ≈ 26–27 (the 25 % bar may still be missed; reported either way, bars unchanged).

> **Outcome (2026-10-20).** Control-path trace (`prof/p1-full.etl`, first 100 s of the full probe = the MONO and
> 4-voice engine trials with the morph ramping; `prof/p1-full-summary.txt`): `KeytrackedTilt::updateGains` + `pow`
> 5.8 % (per-sample smoothed Tone), recipe `evaluateShape` + its exp / exp2 / log2 ≈ 7 % (identical controls evaluated
> by every voice), `constexprLn` / `constexprExp` 1.7 % (compressor dB series), `lround` 0.8 % (the fold sine),
> bank continuity pass down to 0.4 % (T029). Three batches, each built once:
> - Batch 3: engine-side pan pair + `setPanCoefficients`; `processDetectionOnly`; detector `setPeakOversampling`
>   (P-3 GREEN −85.1 dBFS worst, `p3-t032.log`). Gates: processors 86 cases green; per-push lane 467 / 468 (stale
>   T017 fingerprint only).
> - Batch 4: tilt gains as a float exp2 pair (+ the matching `computeInverse`); fold sine without `lround`; the
>   detection-only path's dB through `std::log2`. Same gates green.
> - Batch 5: one `evaluateShape` per changed interval for all voices (`setControlsWithShape`), log2 table in the
>   recipe — fingerprint digits identical before / after.
> Paired, pinned, static morph, idle machine (`p1-t032-ab.log`): MONO Tube 8.2 → 6.9 %, Fracture 6.7 → 6.8 %,
> 4 voices 24.4 → 24.0 % (the static setting does not exercise the control path). T030 follows.

### T030 Re-probe P-1 (main loop; alone, pinned)

**Files:** `evidence/run-log/close/p1.log`, `evidence/probes/p1-cpu.txt` (overwritten), run-log `T013.txt` (appended).

- `bash evidence/close/probe.sh p1` with nothing else running. GREEN → Group 11 runs. RED → STOP AND SURFACE to the user
  with the residual and the breakdown (the ceilings are never relaxed).

> **T030 reading (2026-10-20 18:25, after batches 1 + 2): RED** — MONO 13.23 % (bar 8), 4 voices 44.57 % per-voice
> placement / 49.57 % post-sum (bar 25); `run-log/close/p1.log`, `probes/p1-cpu.txt`, `run-log/T013.txt`. The same
> protocol on the pre-batch exe run immediately after read MONO 14.28 % / 4 voices 43.99 % (p1-t030-pair-before.log),
> i.e. the full probe moved only −7 % MONO and nothing at 4 voices, while the paired static-morph A/Bs read −20 % MONO /
> −18 % at 4 voices. The machine was not idle: 15–21 % of all cores were busy in other processes during both runs
> (PhoneExperienceHost, dllhost, chrome; sampled with Get-Counter) and the replica breakdown shows stage "costs" of −1 to
> −3 % (noise), so neither reading is a gate verdict. Surfaced to the user per this task: an idle re-run comes first; the
> remaining structural lever is the lock-step L / R rewrite of the oversampled stage bodies.
>
> **T030 idle re-run (user closed Phone Link and Chrome; other processes ≤ 0.6 of one core; 18:55): RED** — MONO
> **9.20 %** (bar 8; Fracture endpoint 8.36), 4 voices **30.21 %** per-voice (bar 25; Fracture 27.66), post-sum 33.94 %
> (`run-log/close/p1.log`, `probes/p1-cpu.txt`, `run-log/T013.txt`). Against the group's starting point (13.61 / 46.32,
> idle this morning): **−32 % MONO, −35 % at 4 voices.** The batch-1 exe on the same idle machine right before read
> 11.13 / 32.97 (p1-t030-pair-batch1.log), so batch 2 alone is −17 % MONO / −8 % at 4 voices on the full setting.
> Residual over the bar: MONO +1.2 % (15 % over), 4 voices +5.2 % (21 % over). Static breakdown now (MONO Tube, % of a
> core): field row = unlinked stereo 2.98, Character 2.88, core + excitation 2.55, morph + law + Motion + macros 1.79
> (4 voices: 4.91 total), Mass dynamics 1.74, DC + limiter 1.72, Shape 1.21, Fold 0.83. Surfaced to the user for the
> structural decision.
>
> **T030 after T032 batches 3–5 (idle machine, alone, pinned; 20:25): MONO 6.71 / 6.77 % (Tube / Fracture; bar 8 —
> GREEN), 4 voices 25.71 / 23.37 % (bar 25 — RED by 0.71 at the Tube endpoint), post-sum 29.56 / 27.70 %**
> (`run-log/close/p1.log` of that run kept as `p1-t030-idle-batch5.log`). Against the group's start (13.61 / 46.32):
> −50 % MONO, −45 % at 4 voices. The 4-voice replica breakdown reads stage costs of −1.5 … −3.6 % (the removal noise
> at this level), so the per-stage figures are not quoted; batch 6 (the a₁ recipe evaluated once for all voices)
> follows and T030 is re-run.
>
> **Batch 6** (`profundum_engine.h` / `profundum_voice.h`, `IntervalInputs::sharedA1Shape`): the FR-005 a₁ recipe
> (`state_.recipe`, the same for every voice) evaluated once per changed interval by the engine; the voice copies it.
> Same function, same input, but the fingerprint digits moved in the 9th significant figure (rms 0.286573494 →
> 0.286573496): under /fp:fast MSVC vectorises the recipe's exp calls at one inlined call site and not the other, so
> this one is a tolerance-level change on a₁ (≈ 1e-8 relative), not bit-identical; the per-push lane stays 467 / 468
> (stale T017 fingerprint only, re-harvested at the group's close).
>
> **T030 FINAL (idle machine, alone, pinned; 20:45): GREEN** — MONO **7.26 / 6.96 %** (Tube / Fracture; bar 8), 4 voices
> **23.03 / 23.93 %** (bar 25), post-sum 28.30 / 28.37 % (recorded, SC-004); `run-log/close/p1-t030-final.log`,
> `probes/p1-cpu.txt`, `run-log/T013.txt`. Against the group's start (13.61 / 46.32, idle this morning): −47 % MONO,
> −48 % at 4 voices. Group 11 runs.

---

## Group 11 — rendered criteria, perf and listening renders (parallel; each task owns exactly one TU; no header edits)

Every task here reads `evidence/probes/p1-cpu.txt` / `p2-declick.txt` / `p3-output-alias.txt` first: if any verdict is RED or absent, return
"blocked" (the checkpoints gate this group). Each `[long]` case has its `…Smoke` sibling in the same TU (deviation 5).

### T015 [P] Clicks (SC-005, SC-006, SC-025; FR-016; plan C-4, S9.9)

**Files:** `dsp/tests/unit/systems/profundum_engine_click_test.cpp`, run-log `T015.txt`.

- `VoiceEngine_StealClickless_{Solid,Spread,Wide}` `[sc-005][long]`: 5 notes into 4 voices at E1 / C3; victim {held, releasing} × Note Memory
  mode {AlwaysRetrigger, Legato, EnvelopeRetriggerOnly, OscillatorContinuous} × Retrigger {Reset, Continue} × host block {1, 37, 512} × base
  {A_s, B}. Sum tap; W = from the steal's grid point to the dip's end (`samplesUntilSwitch` + L_d) plus one period of the slower f0; reference
  = the same engine, seed and events at polyphony 5. (a) `voiceEngineDeltaRatio` ≤ 10^(0.5/20) = 1.059254; (b) `voiceEngineDeltaBound` holds
  over W (S_max = π / (2·L_d)); (c) `ShapeCharacterTest::differentialClicks` recorded. Positive controls (in each shard, at E1, A_s, held,
  AlwaysRetrigger, Reset, block 1): `disableStealFade` steal and `disableDeclick` restart each fail (a) or (b). CSV
  `evidence/steal-clicks.csv` (cell id, (a) ratio, (b) margin, (c) count). Smoke `VoiceEngine_StealClicklessSmoke`: SOLID, E1, A_s, held,
  AlwaysRetrigger, Reset, block 37, plus the two positive controls.
- `VoiceEngine_MonoReplaceClickless_{Legato,Retrigger}` `[sc-006][long]`: Phase 4's legato × Retrigger × Note Memory × GlideMode matrix
  (`noteBehaviourMatrixScript`, `profundum_note_behaviour_test_helpers.h:955`, ported to the engine script) on the full chain, intervals {1, 12,
  24} st both directions; reference POLY-2; (a) gated except where portamento > 0 (recorded there); (b) gated everywhere with the glide S_max
  (Phase 4 SC-010(c)) where it applies; positive controls as SC-005. **FR-009 record:** one arm per matrix slice where the morph (5 ms ramp, at
  the replacement) moves attack, decay, sustain and release; column `ampStep` = max |g_amp[n] − g_amp[n−1]| over the 64 samples after each grid
  point where a time / sustain receiver changed, beside `ampStep_ref` of the no-morph run. CSV `evidence/mono-replace-clicks.csv`. Smoke:
  AlwaysRetrigger / Reset / ConstantTime, 12 st up, E1.
- `VoiceEngine_MotionSelectorsClickFree` `[sc-025][long]`: every ordered pair of `source`, `target`, `cycleShape`, `polarity`, `cycleSync`,
  `cycleRetrigger` and the representative `cycleNote` × `cycleModifier` subset (Phase 8 handoff 9.8: re-open
  `specs/profundum-phase8-bass-morph/evidence/phase-handoffs.txt` §9.8 for the subset), switched through the morph (endpoints differing in that
  selector only, Motion amount 1, depth 1, 5 ms ramp), held E1 on A_s: max |Δy| over the transition window ≤ 10^(0.5/20) × the direct-setter
  reference's (the same selector switched by `setEndpoint` of A to the new value at the switch grid point g_sw, p held at 0); differential
  click count ≤ the reference's; both recorded to `evidence/motion-selector-clicks.csv`. Smoke: source Off → Cycle and target Mass → Depth.

### T016 [P] Audibility table, macro monotonicity, Motion → Mass (SC-010, SC-007, SC-009; FR-080; plan C-8, C-17, S9.11)

**Files:** `dsp/tests/unit/systems/profundum_engine_audibility_test.cpp`, run-log `T016.txt`; may append an FR-080 per-row setting to
`spec.md`'s table (only for a row inert at both endpoints, with the reason).

- `VoiceEngine_AudibilityTable_{Core,Character,Anchor,Pitch,Envelope,Mass,Field,Motion,Morph,NoteMemory,Macros}` `[sc-010][long]`: every FR-080
  row AT-01 … AT-50 in its shard (Core AT-01–03; Character AT-04–09; Anchor AT-10–18; Pitch AT-19–23; Envelope AT-24–25; Mass AT-26–28; Field
  AT-29–33; Motion AT-34–40; Morph AT-41–42; NoteMemory AT-43–46; Macros AT-47–50) at E1 and C3, on the row's base (A → A_s, C-8, named
  "+ sustain 1 (C-8)" in the CSV; **AT-48 Dark ↔ Bright on B_s, ruling B10-1, named "B + sustain 1 (B10-1)", with its A_s reading recorded in
  a second, ungated line**), contrast, descriptor and tap exactly as the spec's FR-080 table; bars: dB ≥ 3, D_br ≥ 0.25 oct, time ratio
  ≥ 2, Anchor ΔE_sub ≥ 2 dB, Drop rows by Phase 4 SC-021 (start ≥ 6 st, t_s ratio ≥ 2, α ≥ 0.1, ρ ≥ 0.05·A_drop) through
  `voiceEngineDropDescriptors`, Note Memory ≥ 3 dB or ≥ 30°, AT-13 within ±5 cents of the flipped ratio at the P-4 Amount (or the row reads
  "FOLLOW INERT" with the P-4 evidence). D_nl always the Character tap over the twin's core tap (`voiceEngineRenderWithTwin`). `REQUIRE(pass)`
  per cell. CSV `evidence/audibility-table.csv`: row, contrast, base, note, descriptor, tap, value, bar, pass, the output-tap reading, ampStep.
  **FR-009 record (Envelope shard):** per envelope row, a morph-driven arm moving the row's time / sustain field during a held note; `ampStep`
  per T015's definition; other rows write `n/a`. **H-17 record (Mass shard):** 4 voices detuned E1 ± 7 / ± 13 cents at Mass 100 on base B,
  steady `compressorGrDb` (median over 1 s after 500 ms) to `evidence/h17-poly-gr.txt` beside Phase 6's −10.1 dB. A row inert at both
  endpoints gets a documented per-row setting written into the spec table and the CSV with the reason (never removed, bar never lowered).
  Smoke `VoiceEngine_AudibilityTableSmoke`: AT-03, AT-10, AT-26 at E1. **AT-26's A cell runs on the per-row setting A_s + anchor
  level 0.5 (ruling G11-2), the plain A_s line recorded ungated.**
- `VoiceEngine_MacroMonotone` `[sc-007][long]`: each macro 0 → 1 in 21 steps, others neutral, E1 and C3, base A_s (**B_s for Dark ↔ Bright,
  ruling B10-1**): Weight → E_sub (Sum); Dark ↔ Bright → D_br (Sum); Tight ↔ Loose → D_t (Sum, gate render); Clean ↔ Violent → D_nl; non-decreasing against the running maximum within ε
  (0.25 dB / 0.02 oct / 2 %): `D(p_j) ≥ max_{i<j} D(p_i) − ε`. CSV `evidence/macro-monotone.csv`. Smoke: Clean ↔ Violent at E1, 5 steps.
- `VoiceEngine_MotionMassSwell` `[sc-009][long]`: CYCLE → Mass, amount 1, depth 1, Mass 50, E1, rates {0.1, 0.5, 2} Hz; 1 s settle + max(3
  cycles, 3 s); frames per S9.11; gate: D_nl peak-to-peak at 0.1 Hz ≥ 3 dB; recorded: E_sub, D_nl and Sum-tap L_K peak-to-peak per rate, the
  boxcar attenuation beside each, and ENV (retriggered every 2 s) / DRIFT (smoothness 0.5) on Mass with the same frame. Write
  `evidence/motion-mass-swell.txt`. Smoke: 2 Hz, 3 s.

### T017 [P] Chain criteria (SC-015, SC-016, SC-017, SC-018; FR-029, FR-081; plan C-1, C-2, S9.10)

**Files:** `dsp/tests/unit/systems/profundum_engine_chain_test.cpp`, run-log `T017.txt`.

- `VoiceEngine_MonoSum_{Solid,Spread,Wide}` `[sc-015][sc-016][long]`: MONO / 4 voices × A_s / B × E1 / C3; 2 s settle: Q_sub ≤
  `StereoFieldTest::kSubBandQBarDb` (1.0 dB) and ρ ≥ `kSubBandRhoBar` (0.99) by `StereoFieldTest::bandReading`; SOLID: output L == R bitwise from
  the `DcResync` grid point on (C-2; the case also enters SOLID from WIDE mid-render to exercise the re-sync); the Anchor tap L == R bitwise in
  every chunk (SC-016); the H-20 anti-phase residue above the partial range recorded to `evidence/monosum.csv`. Smoke: SOLID, MONO, A_s, E1.
- `VoiceEngine_AnchorThroughChain` `[sc-017]`: E0, E1, E2 × six algorithms at Amount 1 × Fold 100 × excitation 1, Anchor level 1, LOCK; R = Fold
  0, Amount 0, excitation 0; gated waveforms Sine, Triangle, RoundedSine, AsymmetricSine: (a) Anchor-tap w₁ within 1 dB S vs R; (b) |E_sub(S) −
  E_sub(R)| ≤ 4.1 dB for Tube, Fold and Bite, recorded for Rectify, Crush and Fracture under the floor E_sub(S) ≥ E_sub(A_S) − 2 dB (ruling
  G11-3); (c) |ΔE_meas − ΔE_pred| ≤ 0.1 dB with ΔE_pred = 20·log10(|1 + ρ_S·e^{jθ_S}| / |1 + ρ_R·e^{jθ_R}|) (S9.10, G11-3); premises
  asserted: |20·log10(|A_S| / |A_R|)| ≤ 0.01 dB and steady gains; ρ_R / θ_R recorded (ShapeMorph rotates h1 on every chain); the
  tap-sum identity printed only; OctaveDownSine recorded. ρ, θ, ρ_R, θ_R, ΔE_pred, ΔE_meas per cell to `evidence/anchor-chain.csv`. (Not
  `[long]` if it measures under 15 s; otherwise shard per note with a smoke, deviation 5.)
- `VoiceEngine_FollowTracking` `[sc-018]`: Rectify (half-wave-symmetric input, the P-4 setting) and Fracture at E1 / C3; (i) sweep Amount 0 → 1
  over 10 s then held ≥ 6 s; (ii) step at one grid point with ≥ 6 s holds either side; event = the interval where the follow-ratio tap changes;
  Anchor frequency per sample from `anchorPhase` differences (`anchorPhaseTap` seam); reference `ProfundumTest::anchorShsF0` on frames wholly
  inside the holds; gate: settling exists and |error| ≤ 5 cents from it to the end of the hold; latency (settling − event, samples) recorded;
  H-12 (Fracture at Tone ≥ 0.75) recorded. If `evidence/probes/p4-follow-flip.txt` reads "FOLLOW INERT", the case asserts that no event occurs
  and writes the flag. Write `evidence/follow-tracking.csv`. `[long]` with a smoke (step case, Fracture, E1) if it exceeds 15 s.

### T018 [P] Anti-aliasing (SC-021; FR-082; plan C-3, C-21)

**Files:** `dsp/tests/unit/systems/profundum_engine_aliasing_test.cpp`, run-log `T018.txt`.

- `VoiceEngine_Aliasing_{44k1,48k,96k}_{Tube,Fold,Rectify,Bite,Crush,Fracture}` `[sc-021][long]` (18 shards): E0, E1, E2; excitation 1 / 1 /
  asymmetry 1; SHAPE ∈ {0, 1}; Fold 100; the shard's algorithm at Amount 1, Tone ∈ {0, 1}; Anchor level 1 in LOCK and FOLLOW; feed 1; WIDE spread
  1; Mass 100; MONO; held note, no Drop, no glide, Motion Off, morph static; 1 s settle, one frame of `aliasFftLength(fs, f_m)` on L and R, worst
  channel; f_m = f₀, except f₀/2 for Fracture and FOLLOW with r = 0.5. (a) Sum tap ≤ −80 dBFS; (b) output tap ≤ −80 dBFS with the opt-in on
  (default config), the same cells with `dynamicsOversampling = false` recorded and the dynamics-added term (output − Sum, Phase 5 SC-010
  attribution) written; every cell gated, the worst reported. CSV `evidence/aliasing-<fs>.csv`.
- `VoiceEngine_AliasingDropTail` `[sc-021][long]`: (c) a 48 st Drop with the steepest tail (0.418 st/sample, H-9), Phase 4's rendered
  aliased-power method on the **core tap** ≤ −96 dBFS (C-21), the Sum reading recorded; 44.1 / 48 / 96 kHz.
- Smoke `VoiceEngine_AliasingSmoke`: 48 kHz, E1, Tube, Tone 0, SHAPE 0, LOCK: (a) and (b).

### T019 [P] Soak (SC-013, SC-014; FR-061, FR-062; H-2)

**Files:** `dsp/tests/unit/systems/profundum_engine_soak_test.cpp`, run-log `T019.txt`.

- `VoiceEngine_Soak30Min_{Mono,Poly}` `[sc-013][sc-014][long]` (15 min each, seed `0x5013u` / `0x5014u`): each half holds the four SC-014 lock
  segments (Depth and Body by CYCLE 0.5 Hz, Edge and Shift by DRIFT, amount 1, depth 1; each ≥ 122 s of a held LOCK Sine note with no note
  event, Drop, glide, retrigger or morph motion inside; MONO E1, POLY E1 / A1 / E2 / C3) and seeded note and automation activity elsewhere with
  Motion on and the morph moving. Gates: every sample finite (bit test); true peak (test-side 4× halfband) ≤ ceiling + 0.1 dB; DC per 10 s
  window ≤ −80 dBFS at the DC-blocker tap (ruling G11-4; the output's windows recorded); at the output last-5-min mean ≤ first-5-min mean + 1 dB; per interval the `stateFinite()` voices / components and
  `oversamplerStateFinite()`, and every other component's FR-086 taps through `detail::isFinite` (SC-013 list); SC-014: per voice, per segment,
  windows at s₀ + 1 s and s₀ + 61 s, |Δθ(t₀ + 60 s) − Δθ(t₀)| ≤ 0.5° by `ProfundumTest::goertzelPhase` of the Anchor tap vs that voice's core-tap
  h1; max and trend recorded. H-2 (0.59° / 60 s through the Fold at C3) recorded. Write `evidence/soak-<half>.txt`. The observer copies only
  the grid record and the per-voice phase windows (R-K), never every sample.
- Smoke `VoiceEngine_SoakSmoke`: 30 s MONO, one 25 s lock segment (Depth), the same gates with one window; two DC
  windows without note events carry the G14-2 trend (G14-11).

### T020 [P] CPU (SC-001 … SC-004; FR-083, FR-014) — **lane deferred to the main loop**

**Files:** `dsp/tests/unit/systems/profundum_engine_perf_test.cpp`, `specs/profundum-phase9-voice-engine/evidence/close/cpu-lane.sh`, run-log
`T020.txt`.

- `VoiceEngine_CpuMono` `[sc-001][performance][.perf]`: the P-1 setting (MONO), Tube and Fracture cells, 10 s after 1 s warm-up, median of 3;
  gate max ≤ 8 %; per-stage breakdown printed; `SC001 VERDICT GREEN|RED <%>` to `evidence/sc001-cpu.txt`.
- `VoiceEngine_CpuPoly4` `[sc-002][performance][.perf]`: four held notes E1 / A1 / E2 / C3; gate ≤ 25 %; the 8-voice cell (E1 / C3 / A1 / E2 / G1 /
  B1 / E3 / A2) printed as `RECORD`; `evidence/sc002-cpu.txt`.
- `VoiceEngine_WorstInterval` `[sc-003][performance][.perf]`: plan S10.6's SC-003 row verbatim (4 voices, block 64, ramp 5 ms, the five-group
  pair Tube ↔ Fracture / SOLID ↔ WIDE / field Motion source / Motion ENV ↔ CYCLE / Anchor Sine ↔ Triangle with every governing amount non-zero,
  Anchor morph Blend, all three amp curves −1 ↔ +1); each `processBlock` timed with `std::chrono::steady_clock`; gate max(block wall / 1.333 ms)
  ≤ 1.0; worst 32-sample interval recorded against 81.6 %; coincidence and coverage asserts from a second, untimed observer run (all five
  `selection(g)` change in one `MorphAdvance` interval with `anchorPosition == position` bitwise; `AmpCurve` records issued on all 4 voices inside
  the worst block); the H-13 crossfade-window cost per voice printed against 2.65 %.
- `VoiceEngine_PlacementCost` `[sc-004][performance][.perf]`: PerVoice vs PostSum at 4 voices under SC-002's protocol, and FOLLOW per voice vs
  off; written to `evidence/placement.txt` with the two readings the FR-014 ruling needs.
- `cpu-lane.sh` (Phase 8 `cpu-lane.sh` structure; deviation 8): busy-check refusal; each case by name through `pwsh -NoProfile -File
  tools/pin-perf-cores.ps1 -Exe <exe> -ExeArgs "<case>"`, in order `VoiceEngine_CpuMono`, `VoiceEngine_CpuPoly4`, `VoiceEngine_WorstInterval`,
  `VoiceEngine_PlacementCost`, then the Phase 6 SC-017 CPU case (find its name in `mass_dynamics_perf_test.cpp`; a consumer of the modified
  header), 20 s settle between cases; log `evidence/run-log/close/cpu-lane.log`; verdict lines appended to `evidence/run-log/T027.txt`.
- **Executor:** write the TU and the script, `bash -n` it, return done with notes "deferred to the main loop at close: run `bash
  specs/profundum-phase9-voice-engine/evidence/close/cpu-lane.sh` last, alone (T027)".

### T021 [P] Listening renders and the sheet (FR-071, SC-035)

**Files:** `dsp/tests/unit/systems/profundum_engine_probe_test.cpp` (append), `specs/profundum-phase9-voice-engine/evidence/listening-sheet.md`,
`evidence/close/listen.sh`, run-log `T021.txt`.

- `VoiceEngine_ListeningRenders` `[.listen]`: one WAV per Listen row of FR-070 — H-1 (excitation split hump, 500–550 Hz), H-2 (C3 through the
  Fold, 60 s), H-8 (hard snaps, E0, A 48, T 10 ms), H-10 (a note after a Drop vs a fresh note), H-14 (sub-only voice at Mass 100; reference / saw
  voices), H-19 (Fold / Rectify at the WIDE offset ceilings), H-28 ×2 (Fracture → Crush / Rectify at C3; OctaveDownSine → each other waveform at
  E1) — rendered through the engine at 48 kHz to `evidence/listen/<row>.wav` (32-bit float, written by a TU-local writer; no new shared helper).
- `listening-sheet.md`: one entry per row with the render path, the setting, the question to answer, and an empty "Verdict (user)" line. The
  verdicts are the user's (SC-035); an agent never fills them.
- `listen.sh`: runs the case by name. **Executor:** write the three files, return done with notes "deferred to the main loop: run `listen.sh`
  (≈ 2 min) and present the sheet to the user at comply".

---

## Group 12 — defect resolution and the handoff record (sequential)

### T022 Resolve every `DEFECT:` line, gate-documented red and wall-clock re-tag

- `grep -rn DEFECT specs/profundum-phase9-voice-engine/evidence/run-log/`; for each: write or locate the failing reproduction, diagnose against
  the owning file, fix the owner, record under "Defects found" below. Never relax a bar; a bar proven unreachable is STOP AND SURFACE.
- **Wall-clock re-tags:** from the Group 11 gate log (run with `-D 15`, captured), every per-push Phase 9 case over 15 s that is
  toolchain-independent and not in rule 6's "never `[long]`" list gets `[long]` and a `…Smoke` sibling; record each with its seconds. The T025
  shard list is generated, so it picks them up.
- A Phase 1–8 component behaving outside its own recorded contract inside the engine is STOP AND SURFACE (FR-084), never an engine-side
  compensation.

### T023 Phase 10 / 11 / 12 / 13 handoff record (FR-085; plan S15)

**File:** `specs/profundum-phase9-voice-engine/evidence/phase-handoffs.txt`. From plan S15 with the measured values filled in, each cited
(evidence file + line): the roadmap amendment text for Phase 9 line 666 (`ProfundumMotion` is the Motion bus, Q1); Phase 10 — the prepare /
process contract, latency 0, the ≈ 3.8-sample dynamics group delay (`evidence/os-dynamics-delta.txt`), events at the next grid point (C-9), the
192 kHz opt-in no-op (C-3), FTZ / DAZ; Phase 11 — every engine setter with range, default and unit (S6.2), `ProfundumMacroValues`, polyphony
1–8 default 4, the state budget (422-byte morph blob + the FR-031 values), velocity unused (C-12), the compression row after the law (C-7);
Phase 12 — the harvested macro table (`evidence/macro-rows.txt`), the audibility CSV, the listening verdicts, Bipolar ENV resting at −1; Phase
13 — ρ / θ per SC-017 cell. The comply stage copies it into the roadmap.

---

## Group 13 — integration (sequential; last)

### T024 CMake registration audit (single task)

- `grep -c` each of the fourteen Phase 9 TU paths in `dsp/tests/CMakeLists.txt`: exactly 1 each, except
  `unit/systems/profundum_engine_nonfinite_test.cpp` = 2 (the list and the `-fno-fast-math` block); `profundum_engine_perf_test.cpp` in no
  `set_source_files_properties` block. `dsp/CMakeLists.txt` lists the five new headers exactly once each, in the systems list (and, since G14-12, `primitives/lowpass_biquad_d.h` in the primitives list).
  `git diff 0ae33b87 -- dsp/CMakeLists.txt dsp/tests/CMakeLists.txt` shows only added lines. Record in `evidence/run-log/T024.txt`. A missing
  or duplicated line is fixed here (the only task allowed to edit the CMake files after T001).

### T025 Suites lane, the `[long]` lanes and clang-tidy (FR-084, SC-030) — **deferred to the main loop**

**Files:** `specs/profundum-phase9-voice-engine/evidence/close/suites.sh`, `evidence/close/consumer-cases.js`, `evidence/close/tidy.sh`.

- `consumer-cases.js` (deviation 9): computes the transitive include closure of `dsp/include/krate/dsp/systems/mass_dynamics.h` over
  `dsp/tests/**/*.{h,cpp}` (follow `#include "…"` and `<krate/…>` lines), lists the consumer `.cpp` files that are **not** Phase 9 TUs, extracts
  every `TEST_CASE("<name>", "<tags>")` whose tags contain `[long]` and not `[performance]` / `[perf]` / `[benchmark]` / `[!benchmark]`, checks
  each name appears in `dsp_systems_tests.exe --list-tests "[long]"`, and prints one name per line (and the file list to stderr).
- `suites.sh` (structure of `specs/profundum-phase8-bass-morph/evidence/close/suites.sh`, **with the scope rule of 2026-10-18**, Phase 6 ruling
  G9-4; runs **detached**, writing `evidence/run-log/close/status.txt`, one line per lane — name, exit code, wall clock — so it survives the 2 h
  background-task ceiling):
  1. One rebuild of `dsp_systems_tests` (wall clock recorded; the count of `warning C|warning:` lines must be 0).
  2. **Change-set scope** (`BASE_COMMIT=0ae33b87`): `git diff --name-only $BASE_COMMIT -- dsp/include dsp/src plugins tests/test_helpers` must be
     exactly `dsp/include/krate/dsp/systems/mass_dynamics.h`; `git ls-files --others --exclude-standard -- dsp/include` exactly the five new
     headers. Any other path is RED, written to the status file with the consumer executables whose full suites it requires (Ruinae, Seraphis,
     Vorago, Innexus, other `dsp_*_tests` per the file); those consumers are then rebuilt in one build call and their full per-push suites run
     in step 3.
  3. Concurrently:
     (a) the per-push lane of the phase's executable (which is also the full per-push suite of the one consumer executable):
     `dsp_systems_tests.exe "~[long]~[performance]~[perf]~[benchmark]~[!benchmark]" -D 15` to `evidence/run-log/close/perpush-dsp_systems_tests.log`;
     (b) the `[long]` lane of the **phase's own TUs**: one shard per TEST_CASE of `dsp_systems_tests.exe --list-tests
     "[voice-engine][long]~[performance]~[perf]~[benchmark]~[!benchmark]" --verbosity quiet` (generated, never hand-kept: about 3 SC-005, 2
     SC-006, 1 SC-025, 11 SC-010, 1 SC-007, 1 SC-009, 3 SC-015, 18 + 1 SC-021, 2 SC-013, plus T017 / T022 re-tags), longest first (by the
     previous run's wall clock if a status file exists), through `xargs -P <min(shards, cores − 4)>`, each to
     `evidence/run-log/close/long/<case>.log`;
     (c) the `[long]` cases of the **consumers of `mass_dynamics.h`** (the modified shared header): one shard per name from
     `node consumer-cases.js`, in the same `xargs` pool. **Never every `[long]` case of the executable.**
  4. Summary block: per-shard verdict and wall clock, the worst SC-005 / SC-006 ratio per family, the minimum audibility margin per shard, the
     worst SC-021 dBFS per fs, the soak maxima.
  A timing-budget assertion of an untouched (non-Phase-9) case that fails under this concurrent lane is recorded as load, not re-run.
- `tidy.sh` (structure of the Phase 8 `tidy.sh`): reconfigure `build/windows-ninja` inside vcvars64 (the test CMake list changed — a stale
  compile database gives phantom warnings), check all fourteen Phase 9 TUs and `mass_dynamics_oversampling_test.cpp` are in
  `compile_commands.json`, then `"/c/Program Files/LLVM/bin/clang-tidy.exe" -p build/windows-ninja <TU>` per TU, concurrently, 600 s ceiling each
  (headers checked through the TUs), to `evidence/run-log/close/tidy.log`, last line `TIDY GREEN|RED …`. Not the whole-`dsp` sweep.
- **Executor:** write the three scripts, `bash -n` / `node --check` them, run `node consumer-cases.js` once (seconds; it reads sources and one
  `--list-tests` call against the existing binary if present) and paste its output into `evidence/run-log/T025.txt`; return done with notes
  "deferred to the main loop at close: after T022, run `suites.sh` detached and `tidy.sh` concurrently; quote each lane's last lines into
  `evidence/run-log/T025.txt`; any Phase 9 red is owned (fix, re-run only the affected shard or case)".

### T026 Portability, lints and the SC-030 diff check (FR-084, SC-030; cross-cutting portability)

**Files:** `specs/profundum-phase9-voice-engine/evidence/close/portability.sh`, `evidence/close/sc030-diff.js`, run-log `T026.txt`.

- `portability.sh` (structure of the Phase 8 `portability.sh`): concurrently, each with a 300 s ceiling: `node tools/check-portability.js` over the
  fifteen Phase 9 TUs (fourteen + the helpers header is checked through them); `node tools/lint-apple-globals.js` over the five new headers, the
  helpers header and the TUs; `node tools/lint-layers.js`; `node tools/lint-odr.js`; `node tools/lint-nonfinite-symbols.js`;
  `node tools/lint-float-bit-goldens.js`; `node tools/lint-simd-aligned-loadstore.js` (no new SIMD is expected; a 0 is the evidence); then
  `wsl --shutdown`. Output `evidence/run-log/close/portability.log`, last line `PORTABILITY GREEN|RED`. Re-open each tool's usage line before
  writing its arguments.
- `sc030-diff.js`: `git diff --name-only 0ae33b87 -- dsp/include dsp/src plugins tests/test_helpers` must list only
  `dsp/include/krate/dsp/systems/mass_dynamics.h`; in `git diff -U0 0ae33b87 -- <that file>`, every removed line must belong to the two
  documented branch points (the first statement of `prepare` and the top of `processBlock`, plan S7) — print every hunk with its
  classification; last line `SC030 GREEN|RED`. **Main loop, 2026-10-21 (T026 finding, R-A applied):** the name list is the SC-030 row's
  (R-B, R-A, T029, T032, `profundum_core.h`), and a removed line outside the S7 branch points is a RECORD with its function and tag,
  not a red — the gate is the consumer suites (FR-084). (C-1 ratified: `true_peak_limiter.h` absent; R-D: `core/db_utils.h` absent.)
- **Executor:** write both, run them once (minutes at most). All green → quote the logs into `evidence/run-log/T026.txt` and return done. A
  finding in a Phase 9 file is fixed here (the group gate rebuilds). If the run fails with `wsl: Failed to translate` (the sandbox does not
  mount the repo drive) or exceeds 5 minutes, stop it and return done with notes "deferred to the main loop at close: run `bash
  specs/profundum-phase9-voice-engine/evidence/close/portability.sh` unsandboxed".

### T027 CPU lane alone and the FR-014 ruling (SC-001 … SC-004, FR-014, FR-083) — **deferred to the main loop**

- Precondition: T025 and T026 lanes finished, machine idle. Executor: return done with notes "deferred to the main loop at close: run `bash
  specs/profundum-phase9-voice-engine/evidence/close/cpu-lane.sh` (T020) last, alone; quote the figures in `evidence/run-log/T027.txt` and in
  `evidence/phase-handoffs.txt`; then write the FR-014 ruling into spec.md's FR-014 row (and Clarifications, build session) with both SC-004
  readings: per voice is shipped unless PerVoice breaches 25 % at 4 voices while PostSum meets it". A red verdict is re-run once alone after the
  machine has idled; only then is it a defect, and R-A's stop rule applies. Never relax a ceiling.

### Close procedure (main loop, in this order)

1. `probe.sh p3` at the T006 checkpoint (Group 4). A `P3 VERDICT RED` goes to the user before anything else runs.
2. `probe.sh all-but-p1`, then `probe.sh p1` alone, at the T013 checkpoint (Group 9). A `P1` / `P2` RED goes to the user.
3. `harvest.sh`, rebuild, Group 10 gate (T014).
4. Group 11 gate with `-D 15` captured to a log (T022's re-tag input); `listen.sh` (T021).
5. `suites.sh` detached + `tidy.sh` concurrently (T025); `portability.sh` if T026 deferred it; `sc030-diff.js`.
6. Any red → triage into "Defects found", fix, re-run only the affected shard or case.
7. `cpu-lane.sh` alone (T020 / T027), after the machine has idled; the FR-014 ruling written from SC-004.
8. Comply (outside this task list) quotes the evidence files and run-log readings, collects the user's listening verdicts (SC-035) and the
   four FOR RATIFICATION rulings, and copies `evidence/phase-handoffs.txt` (T023) into the roadmap.

---

## Group 14 — close remediation (main loop, 2026-10-21; from the first full [long] run at comply, compliance.md INCOMPLETE 86 / 17)

The comply stage ran every Phase 9 `[long]` shard for the first time (run-log/close/reverify/status.txt) and ten SCs went
red. Triage first (hidden probes, paired A/B builds), then test-side refinements under the standing rules (bars never
relaxed; readings refined with the reason in the row), then the rulings that remain. Each item is a G14 entry under
"Defects found" below; the outcome readings are appended there.

- **T033 Attribution A/B (done).** The two T029 / T032 levers (limiter halfband detector, period-peak detector at 1×
  on the 4× sidechain) flipped back in one build: the mono soak's true peak (+1.017 dBTP at sample 3554595) and the 48 kHz
  Tube shard's (b) readings (E0 −70.9 on / −88.9 off, E2 −46.66 / −47.33) are identical to the base build
  (run-log/close/g14-soak-mono-B.log, g14-aliasing-48k-Tube-B.log vs the -A copies). Neither lever is a cause.
- **T034 Round 1 (test side + probes; one build):** G14-3 soak lock state, G14-7 SC-003 coverage, G14-8 audibility
  (harness fixes, drafted-descriptor replacements, per-row bases measured on both endpoints), probes for G14-4 / G14-5 /
  G14-6. Runs: soak Mono + Poly, the 11 audibility shards, the three probes, `VoiceEngine_WorstInterval` (coverage only;
  its number re-reads in the CPU lane at close).
- **T035 Rulings (done 2026-10-21, all recommended options):** G14-1 look-ahead limiter; G14-2 DC windows with note
  events recorded; G14-4 SC-021(b) on the standalone dynamics grid, output recorded; G14-5 pan ramp (a) and D_max
  from note-on (b); G14-9 FR-021 amended. Rows first, then Clarifications.
- **T036 Close lanes:** per-push lane, the consumer `[long]` include-closure shards, clang-tidy on the phase TUs, the
  CPU lane alone (SC-001 … SC-004 re-read on the final bits), compliance rewritten from the final logs, roadmap, memory,
  commit on `synth/profundum` (no push).

## Defects found

(None yet. T022 records each resolved `DEFECT:` line here: case, expected / observed, diagnosis, owning file, fix, re-run reading.)

- **G11-1 (fixed, Group 11 fixer 2026-10-20) `VoiceEngine_SoakSmoke` "Unknown exception" / 2 endpoint rejections / SC-014
  158.9°.** The streamed soak driver (profundum_engine_soak_test.cpp:731) read `sched.states[ev.index]`, but
  `vePushEndpoints` encodes `index = 2 * stateIndex + (0 A / 1 B)`. Every Endpoint event read the wrong state, past the
  end of the vector for the last ones: an out-of-bounds read (an SEH fault under Release `/EHa`, which Catch2 reports as
  "Unknown exception"; order-dependent), garbage states rejected by `setEndpoint`, and lock segments rendered on an
  activity state (hence the 158.9° drift). Fix: `sched.states[index / 2]`. Re-run, smoke alone: endpoint rejections 0,
  SC-014 max window 0.0318° (bar 0.5°), true peak −0.931 dBTP. This exposed G11-4.
- **G11-2 (RULED 2026-10-20 → per-row setting A_s + anchor level 0.5, plain A_s recorded; spec AT-26 row, Clarifications) AT-26 A_s cell, `VoiceEngine_AudibilityTableSmoke`:** E_sub(Sum) moves
  +1.78 dB for Mass 0 → 100 (bar 3 dB); the B cell passes (+4.11 dB), so "Showcase inert" does not apply. Measured
  decomposition at E1 (A_s, Sum tap h1): Mass 0 −5.44 dB, Mass 50 −2.96 dB, Mass 100 −3.66 dB, so E_sub is not monotone
  in Mass on A_s. A_s's `anchor.level` is 1.0, the top of the AnchorLevel row (mass_law.h kRows: lo 0, hi 1, ±0.5), so
  Mass 0 → 0.5 but Mass 100 clamps at 1.0; meanwhile the Phase 6 Depth row (−0.5 at Mass 100) and Fold 61.3 drop the
  core tap h1 by 3.2 dB. The engine follows the Phase 6 law; no Phase 9 defect found. Needs a base ruling for the A
  cell (e.g. a per-row anchor level below 0.5) or a ruling that A is recorded.
  **Outcome (main loop, 2026-10-20, run-log/close/g11-smokes.log):** A + anchor level 0.5 reads **+5.25 dB** at E1
  (E_sub −11.92 → −6.67 dB; bar 3) GREEN; plain A_s recorded +1.78 dB; B +4.11 dB GREEN.
- **G11-3 (RULED 2026-10-20 → (c) two-chain form with ρ_R / θ_R recorded; (b) 4.1 dB for Tube / Fold / Bite, floor E_sub(S) ≥ E_sub(A_S) − 2 dB for Rectify / Crush / Fracture; spec SC-017 row, plan S9.10, Clarifications) SC-017 premise and (b), `VoiceEngine_AnchorThroughChainSmoke`.**
  (i) The plan S9.10 R-coherence premise (|arg C_R − arg A_R| ≤ 2°, |ρ_R| ≤ 0.05 dB) cannot hold on any base: the core
  is coherent with the Anchor (core − Anchor 0.0014°, −0.00006 dB at E1), and the whole −9.91° / +0.080 dB comes from
  ShapeMorph ROUND at SHAPE 0, identical for all six algorithms (E2: −11.06° / +0.078 dB). This is Phase 5's recorded
  FR-040 transfer (sc020-h1-transfer.csv ShapeMorph 0: E1 −9.82°, E2 −11.02°). ShapeMorph has no transparent SHAPE
  setting (0.5: −19.8°, 1: +40.9°). (ii) (b)'s 4.1 dB is unreachable for any algorithm that removes h1 at Amount 1,
  because ΔE → 20·log10(1/2) = −6.02 dB as ρ → 0. Measured at E1 Sine, S vs R (ρ, θ, ΔE): Tube 0.49 / 1.2° / −2.56;
  Fold 0.70 / 1.5° / −1.44; Rectify 0.18 / 172.7° / −7.77; Bite 0.80 / −11.3° / −0.98; Crush 0.42 / −118.0° / −7.10;
  Fracture 6e-6 / −176.1° / −6.03 dB. (c) holds where measured (Rectify |err| 0.0077 dB), so the sum is correct.
  **Outcome (main loop, 2026-10-20, run-log/close/g11-smokes.log):** E1 Rectify Sine under the two-chain form: ΔE meas
  −7.7677 / pred −7.7677 dB, |err| 4.7e-9 dB (bar 0.1); ρ_R 0.080 dB / θ_R −9.91° recorded; (b) floor E_sub(S) −
  E_sub(A_S) = **−1.74 dB** (bar ≥ −2) GREEN; (a) 0.0 dB. The full E0 / E1 / E2 grid runs in the Group 11 gate.
- **G11-4 (RULED 2026-10-20 → the −80 dBFS window bar gated at the DC-safety tap, output windows recorded, trend at the output; spec SC-013 row, Clarifications) SC-013 DC, `VoiceEngine_SoakSmoke` after G11-1:** window 0 reads −67.97
  dBFS (bar −80). Per-tap BH7 means of that window: Sum −67.6, dynamics/field −68.6, DCBlocker −115.0, TruePeakLimiter
  output −67.97 dBFS. The DC is added after the DC blocker by the limiter. All of it comes from [2.5 s, 10 s), the
  lock note's onset; [0, 2.5 s) contributes nothing. Window 1 (steady lock) reads −126.8 dBFS. The limiter
  (true_peak_limiter.h: instant attack, 80 ms release, no look-ahead) dips its gain on the onset peaks (true peak
  −0.931 dBTP at 5.013 s) of an asymmetric voice (excitation asymmetry 1, Fold 100), which leaves a DC lobe. FR-025
  fixes the order DCBlocker → TruePeakLimiter, so no engine-side fix is available without a ruling.
  **Outcome (main loop, 2026-10-20, evidence/soak-smoke.txt):** DC-safety tap window 0 **−114.96 dBFS**, window 1
  −126.77 dBFS (bar −80) GREEN, 480000 samples per window; output recorded −67.97 / −126.77 dBFS; trend at the output
  last ≤ first + 1 dB GREEN; true peak −0.931 dBTP (ceiling −1 + 0.1) GREEN; SC-014 window 0.032° GREEN.
- **G13-1 (fixed, main loop 2026-10-21) `lint-float-bit-goldens` red on Phase 3's `profundum_anchor_chain_test.cpp`:** an
  FNV-1a over the float bits of a 1 s core render, printed per Highway target, never compared (:470–484). The lint is a CI step
  (ci.yml "Bit-exact float golden lint"), so the tree was CI-red since Phase 3. Fix in that TU only: `coreRenderAbsSum()`
  prints the double sum of \|x\| (%.12g). Lint: clean (1791 files). `AnchorOscillator_PhaseLockEveryIsaSmoke` re-run after the
  rebuild (run-log/close/g13-isa-smoke.log).
- **G14-1 (RULED 2026-10-21 → opt-in look-ahead on `TruePeakLimiter`, 64 samples in the engine, `latencySamples()`
  reports it; re-read: MONO **−0.953 dBTP**, POLY **−0.975 dBTP**, bar −0.9; limiter suite 10 cases green incl. three
  new look-ahead cases, run-log/close/g14-limiter-tests.log) SC-013 true peak:** the 30 min soak reads **+1.017 dBTP** (MONO, sample 3554595 =
  74.05 s, inside the Depth lock segment: a held E1 at excitation 1 / 1 / 1, Fold 100, CYCLE on Depth) and
  +0.211 dBTP (POLY) against ceiling −1 + 0.1 dB; the 20 s smoke read −0.931. T033 shows the reading identical with the
  T029 halfband detector off, so it is the `TruePeakLimiter`'s design: an instant-attack, zero-look-ahead limiter
  bounds *sample* peaks exactly (true_peak_limiter.h:189–194 clamps every sample to the ceiling) but a gain that
  tracks ceiling / tp sample by sample is a clipper on hot asymmetric material, and the band-limited reconstruction of
  its output overshoots between samples. A true-peak bar on the output cannot be met without look-ahead (latency) or
  gain application at the oversampled rate.
- **G14-2 (RULED 2026-10-21 → the −80 dBFS bar over the windows without note events, the rest recorded; re-read:
  MONO 46 quiet windows worst **−116.1 dBFS**, POLY **−95.7 dBFS**, 44 note windows recorded −68.6 / −63.6; the trend
  clause reads the same quiet windows, the all-window trend recorded) SC-013 DC at the DC-safety tap in activity windows:** MONO window 45 reads
  −68.6 dBFS, POLY window 27 −63.6 dBFS (bar −80; the lock windows read −97 … −133). `kDcCutoffHz` is 5 Hz
  (profundum_engine.h:103), τ = 31.8 ms, so a DC step Δ at the blocker's input (an asymmetric Fold-100 voice's note
  on / off) leaves a transient whose 10 s-window mean is Δ·τ / 10 s: Δ = 0.21 gives −63.6 dBFS. The −80 dBFS
  per-window bar is reachable only in windows without note events on asymmetric voices (the lock segments) or with a
  blocker cutoff that would move the sub's phase.
- **G14-3b (ENGINE DEFECT fixed, main loop 2026-10-21) POLY Shift segment 94°:** two voices entered the segment
  3.3 / 33.5 st off f_res for 1–2.5 s; `VoiceEngine_SoakPolyLockProbe` (isolated segments green; the preceding seeded
  context reproduces it; the grid's resolved Drop reads 0 st / 100 ms at the note-on) located it in `ProfundumVoice`:
  the transition's `drop_.trigger` ran before the idle voice's interval Drop setters, so the Drop fired with the
  activity's last amount. Fix: the Drop setters at the interval head, the trigger deferred to `flushDropTrigger()`
  after them (immediate at the declick switch). Gap before each lock segment 3 → 8 s (kept). Readings below.
- **G14-3 (fixed in the test, main loop 2026-10-21) SC-014 Edge / Shift segments 4.15° / 16.65° (Depth / Body 0.16° /
  0.08°):** the core tap is read after the edge excitation, whose h1 phase rotates with its drive, and DRIFT on Edge /
  Shift (amount 1) moves that drive; the Anchor's reference is the linear core's phase (Phase 3). The lock segments now
  run at excitation mix 0 (`veLockState`); the activity portions keep the hot state. Measurand refinement, bar
  unchanged; written into the SC-014 row with this reading.
- **G14-4 (RULED 2026-10-21 → (b) on the standalone dynamics grid with the release pinned, the engine output
  recorded per cell; probe: limiter input −71.1 → output −47.8 dBFS at E2 hot, dynamics tap −66.4 at E0 Mass 100 vs
  −105 at Mass 50 and −76 in SOLID, standalone on4x −85 / −92 / −93 dBFS at E2 / E1 / E0) SC-021(b) output −46.7 dBFS on the hot cells (E2 Tube SHAPE 1 Tone 1, Mass 100,
  WIDE spread 1), −70.9 at E0 Tube SHAPE 0 Tone 0 with the opt-in on vs −88.9 off:** T033 rules out the T032 detector
  change; the E2 reading is opt-in-independent (−46.66 on / −47.33 off), so the content is added after MassDynamics'
  sidechain. `VoiceEngine_AliasingChainProbe` reads the aliased power tap by tap (Sum, dynamics, field, DC = limiter
  input, output).
- **G14-5 (RULED 2026-10-21 → (a) first a pan ramp on `ProfundumCore` — measured no effect (1.064 → 1.068), reverted;
  the per-channel probe put the excess on WIDE's +0.15 Character channel (B/Spread 0.957, B/Solid 0.78): re-ruled v2,
  (a) recorded for legato jumps ≥ 24 st on WIDE bases, gated everywhere else; (b) D_max / P_max from note-on, the
  after-settle bound recorded — re-read min margin 1.125; probe: identical across host blocks 1 / 32 / 37 / 41 / 64, (a) 0.78
  in SOLID, the POLY-2 reference itself reads 0.681 against the 0.653 bound) SC-006 ratio 1.064 / (b) margin 0.956 at block 37 only (replace B C3 +24 st legato, every
  Note Memory mode; bar 1.059):** `VoiceEngine_ReplaceBlockProbe` reads the cell across host blocks 1 / 32 / 37 / 41 /
  64 with the max-|Δy| sample relative to the replacement and g_d / g_amp there.
- **G14-6 (fixed in the test, main loop 2026-10-21 → the direct-setter reference issued at the morph's switch grid;
  re-read **worst ratio 1.000**, 80 pairs, 34 s) SC-025 selector switches through the morph read 1.28–1.34× the direct-setter reference
  (source Off → Env 1.276, Drift → Env 1.287, cycleNote SixtyFourth → Quarter 1.344; bar 1.059; zero clicks on both
  paths):** `VoiceEngine_SelectorProbe` prints the grid's Motion source / output and Mass around the switch for both
  paths and the max-|Δy| sample of each.
- **G14-7 (fixed in the test, main loop 2026-10-21) SC-003 coverage "AmpCurve issued voices = 0" in the worst timed
  block:** the number is green (0.407 of the 1.333 ms budget) but the coverage assert read the costliest block, which
  held no switch. AmpCurve records are issued on a bitwise curve change, i.e. at the switch grid points, so the
  coverage is now asserted on every switch block (min over switches ≥ 4) and the worst block's mask is recorded beside
  whether it is a switch block.
- **G14-8 (fixed in the test, main loop 2026-10-21; readings follow) FR-080 / SC-010, 9 of 11 shards red:**
  (i) harness: `auBm::lKDb` (the H-14 output record) needs the Phase 8 read window (1 s + 262144) and aborted the
  Anchor / Pitch / Note Memory shards on their 1–2 s renders (bass_morph_test_helpers.h:708) — a short-window K-weighted
  level replaces it on those rows; (ii) AT-29 / AT-31 read SOLID's side (exactly 0, the −120 dBFS floor, G8-1
  "invalid") — for an off → on contrast the floor is the reading and the contrast is read against it; (iii) AT-30 ran
  WIDE at spread 0, where `wideOffsets` is {+0, +0} by Phase 7 FR-040 — the row runs WIDE at spread 1; (iv) AT-32
  read D_st above 2 f_c, blind by construction to the band Gravity governs (0.25 / 0.07 dB on A_s / B_s) — the row reads
  side / mid below f_c (FR-080 "Drafted descriptors"); (v) AT-24 read D_t, the gate render's fall time (108 / 161 ms
  for attack 0.1 / 500 ms, ratio 1.49) — the row reads t_90, the Sum's rise time (same family, ratio ≥ 2); (vi)
  AT-37 / AT-38's frame-wise D_br used the S9.11 fit's h1..h8 only, blind to Edge / Shift (0.07 / 0.04 oct on B_s) —
  full-band Goertzel centroid; (vii) rows inert on A_s (a sine: AT-01 edge 0, AT-02, AT-09 Fracture near-silent,
  AT-35 … AT-38, AT-33 at E1) are measured on A_s and B_s and gated by FR-080's rule (B10-1 form: B_s gated when it
  reaches the bar, A_s recorded); (viii) the Mass rows (AT-26 B at C3 1.84 dB, AT-34, AT-39, AT-40) run on the AT-26
  per-row setting (anchor level 0.5), plain readings recorded; AT-27 (B 2.92 dB at C3) measured on B and B_s.
- **G14-9 (RULED 2026-10-21 → the Note Memory transition and policy decide; MonoHandler's legato bit gates glide only;
  FR text amended) FR-021 deviation:** `applyNoteOn` (profundum_engine.h:750–759) never reads
  `MonoNoteEvent::retrigger`; the Note Memory transition (FromSilence / Overlap / Tail, from `audible` and
  `wasHeld`) decides the voice's actions. `MonoHandler`'s legato bit therefore gates only its own `PortaMode::LegatoOnly`
  glide; the Legato Note Memory policy (Phase 4) carries retrigger suppression. The FR text names both.
- **T025-2 (lane defect fixed, main loop 2026-10-21) `evidence/close/suites.sh` scheduled 0 of 315 [long] shards:** the
  ordering step (`awk 'NR == FNR { ... next } ...' prev-wall.txt union.txt`) read an empty prev-wall file (the previous
  status.txt held no `long:` lines), and with an empty first file awk's NR == FNR idiom swallows every line of the second;
  the status line read "long-list cases=0 (phase 47, consumers 268)" and "long-lane shards=0" while the per-push and
  consumer lanes ran. Fix: key the first file by `FILENAME == ARGV[1]`, and a guard that turns "scheduled != union" into a
  RED status line (`list_fail`). The lane was stopped and relaunched on the fixed script; the earlier reverify run (whose
  status.prev.txt carried wall clocks) had not hit it.
- **G14-12 (RULED 2026-10-21 → the excitation and Fold split low-passes in double precision, new L1 header
  `primitives/lowpass_biquad_d.h`; spec SC-021 row, Clarifications) SC-021(a) 102 of 432 cells red (−46 … −79 dBFS):**
  attributed over nine probe rounds (hidden `VoiceEngine_AliasingSumProbe`, `VoiceEngine_AliasingCoreProbe{,2,3,4}`,
  run-log/close/g14-aliasing-sum-probe-r{1..5}.log, g14-aliasing-core-probe*.log) to float32 rounding in the two 500 Hz
  split TDF2 biquads (fc / fs = 0.01 … 0.0026): present at the core tap at drive 0 and any partial count, absent at
  mix 0, reproduced by the path's linear skeleton and gone with the skeleton's low-passes in double (−108 → −132 dBFS
  at 48 kHz, −100 → −124 at 96 kHz). Fix: `ProfundumCore` (profundum_core.h prepareExcitation) and `HarmonicFold`
  (harmonic_fold.h prepare) use `LowpassBiquadD` for splitBase / splitOs (same RBJ design, double coefficients and
  state, float interface, non-finite input resets). Prototype re-read: 10 failing cells left (Crush WIDE 44.1 kHz E2,
  Rectify FOLLOW ratio flip at 96 kHz, E2 Rectify Tone 1 at −78 … −80), triaged as G14-13.
- **G14-13 (RULED 2026-10-21, three items; spec SC-021 / FR-007 / FR-084 rows, Clarifications) the ten SC-021(a)
  cells left after G14-12:** (1) 96 kHz E1 Rectify FOLLOW: the FR-008 flag flipped every period (112 / 112.5,
  `VoiceEngine_AliasingG1413Probe`, run-log/close/g14-13-probe*.log) — the Anchor at ratio 2 fed into the Fold is the
  even harmonic the meter reads; ruled a 4-period dwell on the FOLLOW ratio handoff (profundum_voice.h step 6,
  `kFollowRatioDwellPeriods`) — measured alone: a 5-period cycle (23 / 112) and the 48 kHz E2 FOLLOW cell joining it
  (22 flips); second ruling: the FR-008 meter reads the Shape output before the Anchor feed (profundum_voice.h step
  5b), the dwell kept — 0 flips on every cell, SC-018 / SC-033 green (run-log/close/g14-13-probe-{dwell,meter}.log,
  g14-13-{rectifysymmetry,followtracking}.log); (2) 44.1 kHz E2 Crush WIDE −46.5 dBFS: the Fold detector's 0.5 % ripple toggling a
  Crush quantiser slot; ruled `kDetectorReleaseHysteresis` 0.01 (re-read −121.8; Phase 2 per-push 3469 cases and
  six `[long]` cases green on 0.5 %, re-run on 1 %); (3) E2 Rectify Tone 1 at 44.1 / 48 kHz −78 … −80: recorded
  (profundum_engine_aliasing_test.cpp `recordedA`). Readings on the final bits in the SC-021 row.
- **T025-3 (lane defect fixed, main loop 2026-10-21) `consumer-cases.js` took the include closure of mass_dynamics.h
  only:** the close lane's 268 consumer shards covered one of the 14 modified headers; over all 14 (git diff against
  0ae33b87) the closure holds 354 consumer [long] shards, 86 of which had not run (HarmonicFold's five in
  dsp_processors_tests, Rectifier_AliasingE0E2, VoragoComposed_DepthMacroAxis in dsp_effects_tests, the Vorago engine
  soaks — the bank, TanhADAA and the limiter are Vorago's — ProfundumNoteBehaviour / ShapeMorph / BassCharacter
  cases). Fix: the script seeds the closure with every modified header under dsp/include (same base commit as
  suites.sh) and names the executable of each case; the lane re-runs on the G14-12 bits. The user ruled the four
  long Vorago cases (4 h + 4 h + 70 min + 55 min) out of the lane on the same day (bit-identical path, per-push green;
  spec FR-084); `consumer-cases.js` carries the exclusion with the reason.
- **G14-10 (fixed in the test, main loop 2026-10-21) SC-015 `VoiceEngine_MonoSumSmoke` cell SOLID,MONO,A_s,E1 "L != R
  samples after the DcResync: 64":** G14-1's 64-sample look-ahead delay line in `TruePeakLimiter` holds the last 64
  pre-copy samples at the DC-blocker copy, so the identity holds from the copy plus the engine's latency (FR-025); the
  cell counted from the copy. The read starts at `resyncAt + kLimiterLookaheadSamples`; the FR-029 and SC-015 rows say so.
- **G14-11 (fixed in the test, main loop 2026-10-21) SC-013 `VoiceEngine_SoakSmoke` "trendWindows 0 > 0":** the G14-2
  round-6 trend reads the DC-tap windows without note events, first against last, and the 20 s smoke held one such
  window (10–20 s); the lock segment is 25 s (30 s render, two quiet windows). The [long] halves read 24 (T029 r11).
- **G13-2 (resolved, main loop 2026-10-21) SC-030 diff script RED on `mass_dynamics.h` hunk 9 (`configureDetector` reads
  `scRate_`) and the relocated `processBlock` loop body:** plan S7's drafted "exactly the existing statements" read literally.
  R-A rules bit-identical fixes gated by the consumer suites; the SC-030 row now says so and names `profundum_core.h`;
  `sc030-diff.js` step 2 records the hunks (function + relocated / rewritten tag) instead of reddening. Evidence: Phase 6
  suites green at T029 batch 2 (`t029-batch2-gates.log`) and T032 batches 3–4 (`t032-*-gates.log`).
- **T019-1 (fixed in T022, 2026-10-20; re-run reading pending the group gate) SC-013 state poll,
  `VoiceEngine_SoakSmoke` / `VoiceEngine_Soak30Min_{Mono,Poly}`:** expected the soak to poll the voices' and
  MassDynamics' `stateFinite()` and `oversamplerStateFinite()` per interval (plan S10.6 SC-013 row, R-E); observed
  `WARN "SC-013 state poll unavailable"` (run-log/close/g11-smokes.log:25), because `ProfundumEngine` declared no poll
  and `voices_` / `dynamics_` are private. Owner `dsp/include/krate/dsp/systems/profundum_engine.h`. Fix (append-only,
  after `droppedEvents()`): `stateFinite()` = every `ProfundumVoice::stateFinite()` (profundum_voice.h:670) &&
  `MassDynamics::stateFinite()` (mass_dynamics.h:648); `oversamplerStateFinite()` = `dynamics_.oversamplerStateFinite()`
  (mass_dynamics.h:672). The soak TU's requires-expression (`vePollEngineState`, profundum_engine_soak_test.cpp:181)
  picks both up with no test edit and gates `stateBad == 0` / `oversamplerBad == 0`. Re-run: `VoiceEngine_SoakSmoke`
  after the group rebuild (no WARN expected; both counters 0).
