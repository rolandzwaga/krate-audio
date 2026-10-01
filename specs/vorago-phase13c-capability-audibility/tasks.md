# Tasks: Vorago Phase 13c — Capability Audibility

**Spec:** `specs/vorago-phase13c-capability-audibility/spec.md` (Clarifications Q1–Q6, 2026-10-01)
**Plan:** `plan.md` (same directory)
**Branch / base:** `feat/vorago-phase1-events-modulation` at `64f57e1a`
**Status:** TASKS. Dependency-ordered. No commit tasks; commits happen outside this workflow.

## How to read this file

- **Groups run strictly in order.** Inside a group, a task marked **[P]** may run in parallel with the other
  [P] tasks of the same group, because it creates **new files only** and no other task in the group touches
  them. An unmarked task runs alone, in listed order. This phase creates **no new source file** (plan §0
  item 1, §6): every code task edits a shared, already-registered file. So **no task in this list is [P]**,
  and every group is sequential.
- Every task is self-contained. It names the files, the test to write **first** (and why it fails before the
  change), the implementation intent, and the target that verifies it.
- Canonical loop for every code task: failing test → implement → **zero warnings** → tests pass.
- **Build** (Windows, always the full path):
  `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target <target>`
- **Run one suite directly** (never `ctest -R <exe>`):
  `build/windows-x64-release/bin/Release/<target>.exe "<TestName*>" 2>&1 | tail -5`
- **Per-push systems run:**
  `build/windows-x64-release/bin/Release/dsp_systems_tests.exe "~[.perf]~[long]" 2>&1 | tail -5`
- **Probe run** (one preset):
  `VORAGO_PILOT_PRESET="<Preset Name>" build/windows-x64-release/bin/Release/vorago_tests.exe "Vorago_PresetPilot_PrimaryProbe" > specs/vorago-phase13c-capability-audibility/artifacts/<name>.log 2>&1`
  The log's first line is `START <date time> <label> tree=<git short hash, plus "dirty" if so> <candidate>`.
- **Isolation.** Probe runs, `[long]` runs and CPU runs go to a log in `artifacts/`, **one at a time, with
  nothing else running** (no build, no other suite, no clang-tidy). Never re-run a slow run just to see its
  output again: read the log.
- **Every lever candidate is a rebuild** of a `constexpr` (plan §0 item 4; §2.9's seams are NOT built). A
  candidate is edited in, built, measured and logged, then kept or reverted as the ruling says.
- **RULING gates.** The user rules each lever's value on its logged readings (spec OQ-1, FR-010, FR-010b).
  A task titled **RULING** stops, puts the candidate table in front of the user, and waits. Nothing after it
  starts until the ruling is written to `artifacts/rulings.md`, one line per ruling: date, lever, ruled
  value, and the log lines it rests on.
- **R step (FR-010b).** After each ruling, every earlier ruled roster cell, and every §2.10 secondary that
  has verified at a ruled rung, that the new lever can reach is re-read. If one drops below its bar, its
  lever is re-opened and re-ruled before the next lever starts.
- **Stop-and-surface** items are listed in the task that can hit them. "Stop" means: record the measured
  figures; change no threshold, bar, window, candidate list or test file; report to the user. A cell is
  never recorded UNMET (FR-027).
- **Override scope (FR-025).** A roster gate's `VORAGO_PILOT_OVERRIDE` may set only the cell's own controls
  (the ablated parameter, the cell's macro, the route depth) plus level trims (master gain id `0`, section
  mixes). The 27 verified primaries and the FR-030b set are always read **as compiled**.
- **FR-034 surfaced list.** `artifacts/fr034_surfaced.md` (created at T022) holds one entry per pre-existing
  test that encodes the old voicing as data: name, file:line, reason, rung, proposed re-spec. A test is
  entered there **before** it is edited, never edited silently.

### Fixed facts every task relies on (read this session on `64f57e1a`)

| Fact | Where |
|---|---|
| `static constexpr EnvCurve kStageCurve = EnvCurve::Exponential;`, documented "The ONE curve every setStage() call uses" | `dsp/include/krate/dsp/systems/vorago_voice.h:304-305` |
| `kBloomChildSlots = 6`, `kMinParentSlots = 8`, `kMinCloudCapacity = kBloomChildSlots + kMinParentSlots` | `vorago_voice.h:346-355` |
| prepare: `setRichness(0.70f);` `:558`, `setBloomDepth(0.60f);` `:617`; `noteOn`: `mse_.gate(true);` `:944` | `vorago_voice.h` |
| `void setRichness(float r) noexcept { cloud_.setRichness(r); cloudRichness_ = cloud_.getRichness(); }`; `getRichness()` returns `cloud_.getRichness()` | `vorago_voice.h:1141-1145` |
| Envelope writers `mse_.setStage(st, stageLevel_[i], ms, kStageCurve);` `:1061`, `applyStage` `:1880` and `:1882`; `mse_.reset();` `:1797`; Standard loop `const float g = velocity_ * mse_.process();` `:2449` | `vorago_voice.h` |
| Accessors: `getGhostRequest()` `:1004`, `getTidalFogDepth()` `:1013`, `getBreathingGravityLane()` `:1022`, `getEnvelopeOutput()` `:1109`, `cloud()` `:1565`, `resonance()` `:1567`, `ecology()` and `bloom()` `:1568-1569` | `vorago_voice.h` |
| `publishLifeLanes()`: `breathGravityLane_ = breath_.getCurrentValue();`, `resonance_.setGravity(std::clamp(gravityBase_ + breathGravityLane_, -1.0f, 1.0f));`, `tidalFogDepth_ = std::max(0.0f, tide_.getCurrentValue());` | `vorago_voice.h:2230-2234` |
| `updateSpectrumTarget()`: capacity clamp, `parentCount_ = bloom_.reserveBase()`, parent amplitudes, `const std::size_t count = bloom_.processChunk(...)`, `wantTarget = (bloom_.getLiveChildCount() > 0u)` | `vorago_voice.h:2341-2385` |
| Partial-lane writes `:2124-2126`; `ghostRequest_ = combineWake(0.0f, lanes.eco[kGhost][0], lanes.sched[kGhost][0])` `:2128`; `kLeverInputGain` and its `static_assert` `:1665-1670` | `vorago_voice.h` |
| Getters used by tests: `BloomEngine::getDepth()` `bloom_engine.h:679`; `HarmonicCloud::getMutation()` `harmonic_cloud.h:493`; `FeedbackEcology::getWetGain()` (dB) `feedback_ecology.h:1350`; `AtmosphereEngine::getDensity()` `atmosphere_engine.h:872` | as cited |
| Engine: `atmos_.setDensity(0.30f);` `:375`; `kGhostTapMakeupDb = 12.0f`, `kGhostTapMakeupGain = 3.9810717f`, `kMaxGhostTapMakeupDb = 24.0f`, `setGhostTapMakeupDb` `:1001-1010`; `atmos_.setLevel(ghostPeak_ * ghost);` `:1545` | `dsp/include/krate/dsp/systems/vorago_engine.h` |
| Matrix: `kNumRows = 50` with its per-macro count comment `:273-276`; `kRows` `:315`; row-predicate `static_assert`s `:1215-1234` (incl. `VoragoMacroTarget::Count == 41`, 12 macros) | `dsp/include/krate/dsp/systems/vorago_macro_matrix.h` |
| Voice-TU helpers: `mutableBloom(VoragoVoice&)` `:1298`, `mutableCloud(VoragoVoice&)` `:1303`, `makeFastAttackVoice(seed)` `:1316`, `makeBloomDeterministic(BloomEngine&)` `:1328` | `dsp/tests/unit/systems/vorago_voice_test.cpp` |
| Voice-TU cases: `…SilenceClearsEcologyAudio` `:877`, `…BloomCapacityTracksCloud` `:1383`, `…SpectralTargetIsNeutral` `:1439`, `…ClearingPathsDropTheSpectralTarget` `:1589`, `…SpectralTargetEdge` `:1650`, `…LifeModulatorLanes` `:2910`, `…SetterContract` `:3430`, `…BloomCountsProbe` `[.probe]` `:4032` | `vorago_voice_test.cpp` |
| `VoragoEngine_SlotSeedReproducibility` `:1837`, `VoragoEngine_GhostConfiguration` `:2759`, `VoragoEngine_GhostLevelProbe` `[.probe]` `:3215` | `dsp/tests/unit/systems/vorago_engine_test.cpp` |
| Lever TU: `using Probe = Krate::DSP::detail::VoragoEcosystemLeverProbe;` `:213`; `Probe::injectEco` `:71`, `Probe::advanceLifeOnly` `:65`, `Probe::schedLanes` `:140`, `Probe::mutationBase` / `bloomDepthBase` `:127-128`; `uniformLanes(float)` `:235`; `VoragoVoice_EcosystemLeverMapping` `:979`, `…LeverNeutral` `:1137`, `…LaneShapingFidelity` `:1266` (asserts mutation `== clamp(base + raw)` and ghost `== combineWake(0, raw, sched)` at `:1324-1329`), `VoragoEngine_EcosystemLeverBounded` `:1755` | `dsp/tests/unit/systems/vorago_ecosystem_lever_test.cpp` |
| `VoragoMacro_SweepAxes` `[long]` `:1211`, `VoragoMacro_NeutralIsIdentity` `:1419`, `VoragoMacro_NoZipper` `[long]` `:1514` | `dsp/tests/unit/systems/vorago_macro_test.cpp` |
| `VoragoEngine_CpuBudget` `[.perf]` `:971`; `VoragoEngine_GhostExtensionWiring` `:415` | `vorago_perf_test.cpp`, `vorago_ghost_ext_test.cpp` |
| All the dsp TUs above are already in `dsp_systems_tests` (`dsp/tests/CMakeLists.txt:324`; voice/engine/macro `:511-515`, perf `:518`, ghost_ext `:538`, ecosystem lever `:560`) | `dsp/tests/CMakeLists.txt` |
| Pilot probe: `TEST_CASE("Vorago_PresetPilot_PrimaryProbe", "[.probe][vorago]")` `:845`; `VORAGO_PILOT_PRESET` `:847`; `VORAGO_PILOT_OVERRIDE` `:855-886`; route arms `:900-980`; stored-take line `  take: peak … arm4 tail …` `:949-959`; verdict line `  primary d … -> PASS/FAIL` `:991-995`; `printReach("P", …)`, `printReach("P_rev", …)` `:549-552` | `plugins/vorago/tests/integration/preset_pilot_test.cpp` (997 lines) |
| Harness: `kFloorF = 4.0`, `kSecondaryBar = 1.5` `:249-250`; `kRuledTakes = 4` `:265`; `sweepEnv(name)` `:438-455`; `struct TakeRecord` `:1288`; `kSweepPeakCeiling = 0.9661f` `:1730`; `kRunawayDb = -6.0` `:1732`; `kAttackReachBelowSusDb = 6.0` `:1740`; `renderTake` `:2013`; `attackWindowEndSeconds` `:2389-2405`; `computeVerificationVector` `:2510`; `kGestureAfterAttackSeconds = 65.0`, `kRate441 = 44100.0` `:2753-2754`; `scoreRateArm1` `:2841`; sweep 44.1 kHz render `:3176`, `:3192-3194` | `plugins/vorago/tests/preset_test_support.h` |
| `vorago_tests` lists `integration/{preset_sweep,preset_pilot,ecosystem_rule_probe,soak,processor_cpu}_test.cpp`, `unit/{param_table,state_v2,state_v3}_test.cpp` | `plugins/vorago/tests/CMakeLists.txt:17-60` |
| Param ids: `kMasterGainId = 0`, `kMacroAgeId 101`, `kMacroDensityId 102`, `kMacroMovementId 103`, `kMacroGravityId 104`, `kMacroFogId 108`, `kMacroLifeId 109`, `kMacroMassId 111`, `kResonanceGravityId 400`, `kEcosystemSelfAffinityId 902`, `kLifeBreathingDepthId 1500`, `kLifeTidalDepthId 1502` | `plugins/vorago/src/plugin_ids.h:93-232` |
| 13b probe options: `VORAGO_PROBE_SURFACE` (`109=1` = Life max), `VORAGO_PROBE_KNOBS` (`-` = none), `VORAGO_PROBE_REF` (`off` = true-off), `VORAGO_PROBE_SEEDS=6` (GATE1M) | `plugins/vorago/tests/integration/ecosystem_rule_probe_test.cpp:40-73`; 13b `compliance.md:68` |
| Sweep-4 coverage matrix and the default-state line | `specs/vorago-phase14-presets-release/artifacts/sweep4_aggregate.log:47`, `:445-450` |

### Roster (spec "Cell roster")

| Cell | Showcase preset | Kind | Sweep-4 reading |
|---|---|---|---|
| S4 | Feedback Mire | ablation | 1.7072 |
| S6 | Slow Bloom | ablation | 1.4453 |
| S9 | Choir of Absence | ablation | 1.8349 |
| M2 | Erosion | macro reset | 2.4545 |
| M3 | Crowded Dark | macro reset | 3.9226 |
| M4 | Drifting Strata | macro reset | 3.0007 |
| M5 | Stone Gravity | macro reset, plus arms 1 and 3 | 3.5289 |
| M9 | Fogbound | macro reset | 1.8229 |
| M10 | Teeming | macro reset | 0.3794 |
| M12 | Monolith | macro reset | 3.3670 |
| E1 | Bloom Colony | route arms | 0.0568 (attribBase 0.0568) |
| E3 | Swarm Breath | route arms | 0.6496 (attribBase 0.0760) |
| E4 | Feeding Loops | route arms | 1.2203 (attribBase 0.4488) |
| E5 | Haunted Colony | route arms | 1.1920 (attribBase 0.3296) |
| D9.1 | Sudden Chasm | attack window | d_att 2.7661, d_Sus 4.3038 |
| E7.hi, D13.2 | Colony Pulse | secondary | no verifier |
| D13.1 | Teeming | secondary (S7 conjunct) | no verifier |
| D14.1 | Slow Bloom | secondary | no verifier |
| D14.2 | Drifting Strata | secondary | no verifier |

---

## Group 0 — Baseline (sequential)

### T001 — Confirm the starting tree, build it, create the evidence directory

- **Files:** create `specs/vorago-phase13c-capability-audibility/artifacts/`, `artifacts/rulings.md` (a
  header line only) and `artifacts/odr_sweep.txt` (empty).
- **Steps:**
  1. `git rev-parse --short HEAD` must print `64f57e1a`. `git status --short` must show no change under
     `dsp/`, `plugins/` or `tools/`. Write both outputs to `artifacts/base_tree.txt`.
  2. Build `dsp_systems_tests vorago_tests`. Zero warnings. Write the exit code and warning count to
     `artifacts/base_build_status.txt`.
- **Stop-and-surface:** HEAD is not `64f57e1a`, a production file is dirty, or the build warns.

---

## Group 1 — Before-record step (i): the unmodified binary (sequential; one run at a time)

Every run in this group uses the T001 binary. **No source edit happens before T006 is done.**

### T002 — Probe the 15 roster primaries as compiled (FR-003 (i), SC-001)

- **Run**, alone, one preset at a time, no `VORAGO_PILOT_OVERRIDE`: Feedback Mire, Slow Bloom, Choir of
  Absence, Erosion, Crowded Dark, Drifting Strata, Stone Gravity, Fogbound, Teeming, Monolith, Bloom Colony,
  Swarm Breath, Feeding Loops, Haunted Colony, Sudden Chasm → `artifacts/before_<cell>.log` (for example
  `before_S6.log`, `before_D9.1.log`).
- **Check** each verdict value (`primary d`, `attribBase`; for D9.1 `d_att` and `d_Sus`) against the Roster
  table with the reproduction tolerance `|d_base − d_sweep4| ≤ max(0.01, 0.005·d_sweep4)`. Write the 15-row
  comparison (sweep 4, base, |Δ|, tolerance, ok, log line) to `artifacts/before_tolerance.txt`.
- **Stop-and-surface:** any value outside its tolerance. The tolerance is never widened.

### T003 — Repeat-run spread (FR-003 (i))

- **Run** Slow Bloom again on the same binary → `artifacts/before_repeat.log`.
- Append `|d_run1 − d_run2|` and the tolerance `max(0.01, 0.005·1.4453) = 0.01` to `before_tolerance.txt`.
- **Stop-and-surface:** spread > 0.01.

### T004 — `[long]` baselines with full assertion lists (FR-003 (iii), FR-031, SC-011)

- **Run** alone, each with `-s` so every assertion is listed:
  - `dsp_systems_tests.exe "VoragoMacro_SweepAxes" -s` → `artifacts/base_sweepaxes.log`;
  - `dsp_systems_tests.exe "VoragoMacro_NoZipper" -s` → `artifacts/base_nozipper.log`.
- Record each log's summary line. Any assertion that **fails** on the base tree goes to
  `artifacts/base_long_failures.txt`. SC-011 binds only the assertions that pass here.

### T005 — 13b gates and the default surface on the base tree (FR-003 (iii), FR-005, FR-032, SC-012, SC-012b, SC-018)

- **Run** alone, in this order:
  1. `VORAGO_PROBE_SEEDS=6 VORAGO_PROBE_KNOBS=- vorago_tests.exe "Vorago_EcosystemRuleProbe"` →
     `artifacts/gate1_base_default.log`; the same plus `VORAGO_PROBE_SURFACE=109=1` →
     `artifacts/gate1_base_lifemax.log`. Cite each `GATE1M … ratio=…` line (13b final: 0.656 / 0.638).
  2. The full 14-knob table (no `VORAGO_PROBE_KNOBS`) at the default surface →
     `artifacts/gate2_table_default_base.log`, and with `VORAGO_PROBE_SURFACE=109=1` →
     `artifacts/gate2_table_lifemax_base.log`. Match the run header of 13b's
     `specs/vorago-phase13b-ecosystem-audibility/artifacts/final2_table_default.log:1-4`.
  3. FR-005 default surface: `VORAGO_PROBE_KNOBS=-` at the default surface, single seed →
     `artifacts/default_before_13b.log` (t0, t0on, M1 RMS); `vorago_tests.exe "Vorago_PresetPilot_Calibrate"`
     → `artifacts/default_before_p0.log` (the P0 row: stored-seed descriptor, M1 RMS).
- **Stop-and-surface (FR-032):** on the base tree, syncRate does not count on the default surface, or
  selfAffinity or syncRate does not count at Life max (d/t0 ≥ 0.5, no KILL, not OFF-LIKE). That is a
  before-record item, not a 13c regression.

### T006 — CPU baseline (FR-033, SC-014)

- Machine idle and cool: `node tools/run-cpu-tests.js dsp_systems_tests` → `artifacts/cpu_base_dsp.log`;
  then `node tools/run-cpu-tests.js vorago_tests` → `artifacts/cpu_base_vorago.log`.
- Cite `VoragoEngine_CpuBudget` clause (i) and clause (ii), and `Vorago_ProcessorCpu` P/D, in ns.

---

## Group 2 — FR-010 bloom instrument (sequential; test-only edits)

These edits touch `dsp_systems_tests` only. The `vorago_tests` binary that produced Group 1 is unaffected.

### T007 — Extend `VoragoVoice_BloomCountsProbe` to report slot audibility (FR-010, FR-011, SC-008)

- **File:** `dsp/tests/unit/systems/vorago_voice_test.cpp`, the hidden case at `:4032`.
- **Change:** keep the existing printout. Extend its richness set to `{0.0f, 0.35f, 0.40f, 0.6f, 0.70f, 0.9f,
  1.0f}`. At the end of each render, for every `i < BloomEngine::kMaxChildren` with
  `b.getChildSlotIndex(i) != BloomEngine::kMaxSlots`, print `slot`, `getChildParentIndex(i)`,
  `getChildAmplitude(i)`, `voice->cloud().getActivePartialCount()`, `slot < active` (yes/no) and
  `voice->cloud().getPartialCurrentAmplitude(slot)`. Print per run `children live N, sounding M` (sounding =
  amplitude > `BloomEngine::kSilentParentAmplitude`). It stays `[.probe]`; no new assertion.
- **Run** on the base production tree, alone: build `dsp_systems_tests`, then
  `dsp_systems_tests.exe "VoragoVoice_BloomCountsProbe"` → `artifacts/bloom_mechanism_base.log`.
- **Expected (spec Overview):** at r ≤ 0.40 every live child prints `slot < active = no` and amplitude 0; at
  r = 1.0 every live child sounds. **Stop-and-surface** if the log contradicts this: FR-011's premise is then
  wrong, and the lever is not built.

### T008 — `VoragoVoice_CloudFloorCpuProbe` (FR-011 CPU cost, Q2, SC-014)

- **File:** `dsp/tests/unit/systems/vorago_perf_test.cpp` (append; name swept at plan §9).
- **TEST_CASE** `("VoragoVoice_CloudFloorCpuProbe", "[systems][vorago][.perf]")`: one `VoragoVoice`
  prepared at 48 kHz, bloom depth 0, at richness 0.40 and at 0.70; 1 s warm-up, then 30 s in
  `VoragoVoice::kControlChunkSamples` blocks; print ns/block per richness. No assertion (record only).
- **Run** alone on the base tree → `artifacts/cpu_floor_base.log`.
- **Verifies:** zero warnings; two figures printed.

---

## Group 3 — FR-004 probe reporting (sequential; every task edits `preset_pilot_test.cpp`)

Reporting only (FR-002): **no scoring, bar, window, take count or distance change**. The measured-reach
window waits for T046. Every new render applies the run's parsed `VORAGO_PILOT_OVERRIDE` exactly as the gate
renders do (`:856-886`). Each task's check is a probe run whose output shows the new line while its primary
verdict line stays identical to that preset's T002 log line.

### T009 — `VORAGO_PILOT_SECONDARY=1` and `VORAGO_PILOT_CELLS=<label,…>` (FR-004, SC-006, SC-007b)

- **File:** `plugins/vorago/tests/integration/preset_pilot_test.cpp`, inside `Vorago_PresetPilot_PrimaryProbe`,
  after the verdict line (`:991-995`).
- **Implement:** when `VoragoTest::sweepEnv("VORAGO_PILOT_SECONDARY")` is `"1"`, call
  `VoragoTest::computeVerificationVector(...)` (`preset_test_support.h:2510`) on the patched def with the
  stored take (the argument list in plan §3.1, row 1). For **each claimed secondary** of the def, plus each
  cell named in `VORAGO_PILOT_CELLS` (matched by `cellLabel` spelling), print:
  `  secondary <label>: d %.4f bar 1.5 state <ok|false> conjunct <ok|FAIL> attribBase %.4f skip "<reason>" -> <VERIFIED|no>`,
  the last field being `verifiedAt(o, cell, ClaimRole::Secondary)`. An unknown label: `FAIL("unknown cell label <x>")`.
- **Check:** build `vorago_tests` (zero warnings); `VORAGO_PILOT_PRESET="Colony Pulse" VORAGO_PILOT_SECONDARY=1`
  → `artifacts/t009_check.log` prints the E7.hi and D13.2 secondary lines.

### T010 — `VORAGO_PILOT_TAKES=4`: arms and `d` on every take (FR-004, FR-015, E-10, SC-005)

- **File:** same.
- **Implement:** when set to `4`, call `computeTakes(..., VoragoTest::kRuledTakes, ...)` instead of `1`
  (`:892-895`). Print one arm line per take in the `:955` format, prefixed `  take j=<j> seed <s>:`. Then, for
  each take j, re-run the twin and route scoring with `p.storedSeed = takeSeedIndex(stored, 0, j, 4)` and print
  `  take j=<j> d %.4f`. The stored-take `take:` line and the verdict line are unchanged and remain the gate.
- **Check:** Stone Gravity with `VORAGO_PILOT_TAKES=4` → `artifacts/t010_check.log`. It must reproduce the
  sweep-4 reds (`compliance.md:589`): arm 3 about +15.7 dB on take 0, arm 1 hi about −5.92 dB on take 3.

### T011 — Always-on 44.1 kHz arm-1 render and the `levels:` line (FR-004, FR-024, FR-024b, SC-020)

- **File:** same.
- **Implement:** after the stored-take arm line, render
  `detail::sustainSpec(comp, tl, storedSeed, VoragoTest::kRate441, false)` from the **patched** state and score
  it with `detail::scoreRateArm1(cap, tl.A + VoragoTest::kGestureAfterAttackSeconds, VoragoTest::kRate441, …)`
  (`preset_test_support.h:2841`). Print `  arm1@44.1k: finite <y|n> peak %.4f hi %.2f dB [PASS|NO]` against
  `kSweepPeakCeiling` / `kRunawayDb`. After the verdict line print `  levels: arms [y y y y] arm1@44.1k [y]`
  (stored-take arms 1–4, then this render).
- **Check:** Stone Gravity → `artifacts/t011_check.log` prints `hi` about −5.97 dB (`compliance.md:589`).

### T012 — `VORAGO_PILOT_MASTER_TRIM=-6` (FR-024c, SC-023, E-2)

- **File:** same.
- **Implement:** when set, after `VORAGO_PILOT_OVERRIDE` is applied and before the state is built, multiply
  the patched def's `kMasterGainId` (`0`) normalized value (its stored value, or `0.5` when absent) by
  `10^(trim/20)` (−6 → 0.501187), and print
  `[probe] master trim -6 dB: kMasterGainId norm <before> -> <after>`. Every render of the run (takes, twins,
  route arms, 44.1 kHz) uses it.
- **Check:** Slow Bloom with the trim → `artifacts/t012_check.log` prints `0.5000 -> 0.2506` (or the stored
  value × 0.501187).

### T013 — `VORAGO_PILOT_ITERATE=verified27` (FR-030, SC-007)

- **File:** same.
- **Implement:** a `constexpr std::array kRosterPrimaries` in the TU holding the 15 roster-primary
  `Capability` values (Roster table). Move the probe body into a function taking the def (no scoring change)
  so both paths share it. When the option is set: `REQUIRE(!VoragoTest::sweepEnv("VORAGO_PILOT_OVERRIDE"))`;
  derive `requiredPrimaryCells()` (`tools/vorago_preset_defs.h:297-322`) minus `kRosterPrimaries`;
  `REQUIRE(derived.size() == 27u)`; for each cell, find the def whose `primary` is it and run the body
  **as compiled**; print `verified27 <cell> <preset> d %.4f bar %.4f arms [....] -> PASS|FAIL`.
- **Check:** compile only here (the run is long); the `REQUIRE`s run at T057.

### T014 — Reporting-only checkpoint (FR-002, FR-003 (ii))

- Build `vorago_tests`, zero warnings. `git diff --name-only 64f57e1a` lists only `preset_pilot_test.cpp`,
  `vorago_voice_test.cpp`, `vorago_perf_test.cpp` and artifacts: no `dsp/include/**`, no
  `preset_test_support.h`. Write the list to `artifacts/i1_tree.txt`.

---

## Group 3b — Measurement seams (ruling P2, 2026-10-01; plan §2.9; sequential; lands before any ladder)

Adopted at the plan stage so every ladder rung in Groups 6–13 is a probe run, not a rebuild. The seams change
nothing a gate reads: a gate figure is taken with **no** `VORAGO_PILOT_LEVER` set, from the compiled constants
(FR-026), and the probe REQUIREs the tweak held on every capture. Each engine setter ships with the ruled value
as its default — the `setGhostTapMakeupDb` shape (`vorago_engine.h:998-1000`) — and is documented as existing
"so the lever can be MEASURED".

### T065 — Engine fan-outs and voice forwards (FR-010 third route, plan §2.9)

- **Files:** `dsp/include/krate/dsp/systems/vorago_engine.h` (after `setGhostTapMakeupDb`),
  `dsp/include/krate/dsp/systems/vorago_voice.h` (bare forwards).
- **Implement:** engine `setBloomChildGain(float)` / `getBloomChildGain()` (getter reads voice 0),
  `setEcologyWetMakeupDb(float)` / `getEcologyWetMakeupDb()`, `setGhostDensity(float)` / `getGhostDensity()`
  (rejects non-finite itself, FR-071 style), each a loop over `kMaxVoices` in the `:800-830` pattern; voice
  `setBloomChildGain` / `getBloomChildGain` → `bloom_.setChildGain` / `getChildGain`, `setEcologyWetMakeupDb` /
  `getEcologyWetMakeupDb` → `ecology_.setWetGain` (dB → linear in the voice). The owners reject non-finite input
  (`bloom_engine.h:562-564`, `feedback_ecology.h:1321-1322`). No voice member is added. ODR: every new name
  swept (plan §9).
- **Check:** `dsp_systems_tests` builds with zero warnings; a hidden `[.probe]` case sets each engine setter and
  reads it back on voice 0; the per-push `dsp_systems_tests` run is green (the defaults are the no-change
  values until a ruling moves them). Log `artifacts/t065_seams_dsp.log`.

### T066 — `PresetHost::engineForTweak()`, `RenderSpec::engineTweak`, `SweepCapture::tweakHeld` (plan §2.9)

- **Files:** `plugins/vorago/tests/vorago_preset_host.h`, `plugins/vorago/tests/preset_test_support.h`.
- **Implement:** `engineForTweak()` as a `const_cast` of `engineForTest()` with a NOLINT and the reason (the
  `loadState` precedent); `RenderSpec` gains `std::function<void(Krate::DSP::VoragoEngine&)> engineTweak;`;
  `SweepCapture` gains `bool tweakHeld = true;`; `renderPreset` applies the tweak after `host.loadState`, re-reads
  the three getters after block 0 and sets `tweakHeld = false` on any mismatch. With no tweak set nothing
  changes (the FR-002 identity: the Phase 14 sweep records must still load and reproduce).
- **Check:** `vorago_tests` builds with zero warnings; `Vorago_PresetPilot_PrimaryProbe` on Slow Bloom with no
  lever env reproduces its T015 line exactly. Log `artifacts/t066_tweak_identity.log`.

### T067 — `VORAGO_PILOT_LEVER` in the probe (plan §2.9)

- **File:** `plugins/vorago/tests/integration/preset_pilot_test.cpp`.
- **Implement:** parse `"childGain=0.7,ghostTapDb=18,ghostDensity=0.6,ecologyWetDb=12"` (any subset) into an
  `engineTweak` attached to every `RenderSpec` the probe builds — takes, twins, route arms, the 44.1 kHz render
  and the −6 dB re-read; unknown keys `FAIL`; `REQUIRE(c.tweakHeld)` on every capture; the verdict line prints
  `lever: <string>` when set and `lever: none` otherwise, so no log can be mistaken for a gate reading.
- **Check:** Slow Bloom with `VORAGO_PILOT_LEVER="childGain=0.35"` reproduces the no-lever line (0.35 is the
  shipped default); with `childGain=1.0` the `d` moves and `lever:` is printed. Log
  `artifacts/t067_lever_check.log`. Groups 6–13's ladders then read "a rung is a probe run with the lever
  env; the ruled value is then compiled in and re-read with no lever env before the RULING task".

## Group 4 — Before-record step (ii) on the reporting binary (sequential; one run at a time)

### T015 — Re-read the 15 primaries (FR-003 (ii), SC-001)

- The T002 runs on the T014 binary → `artifacts/before2_<cell>.log`. Each value must be within the
  reproduction tolerance of its T002 value; append the comparison to `before_tolerance.txt`.
- **Stop-and-surface:** any miss (the reporting change moved a gate figure).

### T016 — Roster secondaries, before (FR-001, FR-004, SC-006)

- Alone, appended in order to `artifacts/before2_secondaries.log`, each with its START line:
  Colony Pulse `VORAGO_PILOT_SECONDARY=1` (E7.hi, D13.2); Teeming
  `VORAGO_PILOT_SECONDARY=1 VORAGO_PILOT_CELLS=S7` (D13.1 and its S7 conjunct); Slow Bloom
  `VORAGO_PILOT_SECONDARY=1` (D14.1); Drifting Strata `VORAGO_PILOT_SECONDARY=1` (D14.2).

### T017 — The FR-030b set, before (FR-030b, SC-007b)

- **Derive** the set from `specs/vorago-phase14-presets-release/artifacts/sweep4_aggregate.log` (the
  `CoverageComplete` matrix from `:47`, legend "S verified secondary"; default-state line `:445-450`). Plan §7
  expects 22 host-preset secondaries (E6.hi; D4.1–D4.5, D4.7–D4.12; D5.1, D5.2; D6.1–D6.3; D7.2, D7.3; D10.1;
  D11; D12.1) and 8 default-state cells (D1.6, D1.7, D2, D5.3, D8.1, D9.2, D10.2, D12.2). Write the derived
  list, with each cell's host, as the header of `artifacts/before2_secondaries_030b.log`. If the count is
  not 22 + 8, the log wins and the difference is surfaced.
- **Run** each host once with `VORAGO_PILOT_SECONDARY=1` (cells grouped by host) into that log.
- **Run** `VORAGO_SWEEP_SHARD=42/43 vorago_tests.exe "Vorago_PresetSweep_AblationVerifiesClaims"` →
  `artifacts/before2_default_state.log`.
- Record **D7.1 as unverified-before** in the header: surfaced, not charged to a 13c lever.

---

## Group 5 — L1 bloom: failing tests first (sequential; `vorago_voice_test.cpp`)

Each case below **fails to compile** on the base tree (`kCloudRichnessFloor` and `kBloomChildGain` do not
exist) and, behaviourally, fails because children are silent below r ≈ 0.626 (T007's log).

### T018 — `VoragoVoice_CloudRichnessFloor` (FR-011 floor pin)

- **TEST_CASE** `("VoragoVoice_CloudRichnessFloor", "[systems][vorago]")`:
  1. A standalone `HarmonicCloud` prepared at 48 kHz, `setRichness(VoragoVoice::kCloudRichnessFloor)`, one
     control update: `REQUIRE(cloud.getActivePartialCount() == VoragoVoice::kMinCloudCapacity)` (14).
  2. A prepared voice, for r ∈ {0.0, 0.35, 0.6346, 0.7, 1.0}: after `setRichness(r)` and two control chunks,
     `REQUIRE(v->getRichness() == r)` exactly, and `REQUIRE(v->cloud().getActivePartialCount() >= 14u)`.
  3. For r on a 0.02 grid in [0, 1], inclusive: active count ≥ 14.
  4. `setRichness` with a NaN (bit pattern through `volatile`) and with +Inf leaves `getRichness()` unchanged.

### T019 — `VoragoVoice_CloudRichnessFloorNeutral` (FR-011, SC-008 (b) below the floor)

- **TEST_CASE** `[systems][vorago]`: bloom off (`setBloomDepth(0)`), mutation 0, r ∈ {0.0, 0.25, 0.40, 0.55}.
  After 2 chunks + 1 s of settling, for every slot `i < N_user` (N_user = `round(64^r)` clamped to [1, 64]):
  `|20·log10((a_v[i]/a_v[0]) / (a_ref[i]/a_ref[0]))| ≤ 0.5` dB, where `a_v` is
  `v->cloud().getPartialCurrentAmplitude(i)` and `a_ref` comes from a standalone `HarmonicCloud` at r_user with
  the same prepare and seed, no target. Every slot in `[N_user, 14)` reads below
  `BloomEngine::kSilentParentAmplitude` (1e-5).

### T020 — `VoragoVoice_BloomChildrenAudible` (FR-011, SC-008 (a)(b), per-push sentinel)

- **TEST_CASE** `("VoragoVoice_BloomChildrenAudible", "[systems][vorago]")`, plan §5's six steps:
  1. 48 kHz twins from `makeFastAttackVoice(1)`, mutation 0. On-voice: `makeBloomDeterministic(mutableBloom(*v))`,
     fade-in 1 s. Off-voice: bloom depth 0, wake 0.
  2. For each r ∈ {0.0, 0.35, 0.40, 0.70, 1.0}: `setRichness(r)` on both, render 2 chunks, `triggerBloom()` on
     the on-voice, render 1.6 s.
  3. **(a)** for each `i < BloomEngine::kMaxChildren` with `getChildSlotIndex(i) != BloomEngine::kMaxSlots` and
     `getChildAmplitude(i) > 1e-4f`: `REQUIRE(slot < v->cloud().getActivePartialCount())` and
     `REQUIRE(v->cloud().getPartialCurrentAmplitude(slot) > BloomEngine::kSilentParentAmplitude)`.
  4. **(b)** for each `i < min(N_user, 8)` that is not `getChildParentIndex` of a live child:
     `|20·log10((a_on[i]/a_on[0]) / (a_off[i]/a_off[0]))| ≤ 0.5` dB.
  5. Non-vacuity: at every r, `REQUIRE(passingFilterCount >= 1)`; print the count.
  6. Print the absolute per-parent drop (dB, on vs off) at `VoragoVoice::kBloomChildGain` (FR-040 record).
- Every amplitude read is checked with `detail::isFinite` (bit pattern).

### T021 — `VoragoVoice_CloudFloorEdge` (FR-011 edges, plan §2.1 one-chunk lag)

- **TEST_CASE** `[systems][vorago]`: default voice, 48 kHz, bloom on, sustained; r 0.70 → 0.40 → 0.70, each
  held 2 s. At each edge, `maxDeltaInWindow` over the 50 ms after the change ≤ 1.5 × the same statistic over
  the 50 ms before it. Reuse the statistic and its helper from `VoragoVoice_SpectralTargetEdge`
  (`:1655-1700`); do not copy it.

### T022 — Create the FR-034 list and surface `VoragoVoice_SpectralTargetIsNeutral` (FR-034)

- **File:** create `artifacts/fr034_surfaced.md`.
- **Entry:** `VoragoVoice_SpectralTargetIsNeutral` (`vorago_voice_test.cpp:1439`), richness set
  `{0, 0.25, 0.5, 1}` (`:1369`). Below the floor, the "plain" arm's `clearSpectralTarget()` leaves the cloud at
  the floor richness, not the user law. Rung: L1. Re-spec: assert plain-vs-forced identity at
  `{0.6346, 0.75, 1.0}`; the below-floor half moves to `VoragoVoice_CloudRichnessFloorNeutral` (T019).
  **Do not edit the test in this task.**

---

## Group 6 — L1 bloom: implementation, ladder, ruling (sequential)

### T023 — Always-on active-count decoupling and `kBloomChildGain` (FR-011, FR-019, plan §2.1)

- **File:** `dsp/include/krate/dsp/systems/vorago_voice.h` only.
- **ODR first:** `grep -rn "kCloudRichnessFloor\|partialCountFor\|kBloomChildGain" dsp/ plugins/ tools/` must
  return 0 hits outside this phase's new tests; append the output to `artifacts/odr_sweep.txt` (FR-038).
- **Implement** plan §2.1 exactly:
  - public `static constexpr float kCloudRichnessFloor = 0.6346f;` beside `:346-355`, documented
    `64^0.6346 = 14.002`;
  - `setRichness(float r)`: return on `detail::isNaN(r) || detail::isInf(r)`; `cloudRichness_ = std::clamp(r,
    0.0f, 1.0f)`; `cloud_.setRichness(std::max(cloudRichness_, kCloudRichnessFloor))`. `getRichness()` returns
    `cloudRichness_`. Update the doc comment above `:1141`;
  - private `static std::size_t partialCountFor(float r) noexcept`, the cloud's `harmonic_cloud.h:1462-1463`
    expression verbatim;
  - `updateSpectrumTarget()`: the four ▲ changes of plan §2.1 (`userCount`; parents `≥ userCount` zero; the
    `live == 0u && count < userCount` extension; `wantTarget = (live > 0u) || (userCount < active)`); `count`
    becomes non-const; the set/clear tail is unchanged;
  - `static constexpr float kBloomChildGain = 0.35f;` and `bloom_.setChildGain(kBloomChildGain);` in
    `prepare()` beside `setBloomDepth(0.60f)` (`:617`).
- **Verify:** build `dsp_systems_tests`, zero warnings; T018–T021 green; `…BloomCapacityTracksCloud`,
  `…ClearingPathsDropTheSpectralTarget`, `…SpectralTargetEdge`, `…SetterContract` green **unedited**.
  `…SpectralTargetIsNeutral` may be red only at its below-floor points.

### T024 — Apply the T022 re-spec and run the per-push suite (FR-034)

- **File:** `vorago_voice_test.cpp:1369`, `:1439`. Change only the richness set to `{0.6346f, 0.75f, 1.0f}`,
  with a comment citing FR-034 and `artifacts/fr034_surfaced.md`. No tolerance changes.
- **Verify:** per-push `dsp_systems_tests` 100 % green → `artifacts/l1_suite.log`. Any other red is entered in
  `fr034_surfaced.md` and surfaced **before** it is touched.

### T025 — L1 instrument and CPU re-read

- Alone: `VoragoVoice_BloomCountsProbe` → `artifacts/bloom_mechanism_l1.log` (every live child `slot < active
  = yes` at every r); `VoragoVoice_CloudFloorCpuProbe` → `artifacts/cpu_floor_l1.log`. Record the ns/block
  delta vs T008 at r 0.40 and 0.70; the 0.70 figure (path unchanged) must sit within run-to-run noise.

### T026 — L1 child-gain ladder (FR-010, FR-011, FR-024c)

- **Rungs**, one rebuild of `kBloomChildGain` each: **0.35** (floor only), **0.50**, **0.70**, **1.00** (the
  component clamp, `bloom_engine.h:561-566`). These values are this task list's proposal (Open questions).
- **Per rung:** build `dsp_systems_tests vorago_tests`; then alone: Slow Bloom (S6) → `l1_bloom_<v>_S6.log`;
  Bloom Colony (E1) → `l1_bloom_<v>_E1.log`; both again with `VORAGO_PILOT_MASTER_TRIM=-6` →
  `…_minus6.log`; `dsp_systems_tests.exe "VoragoVoice_BloomChildrenAudible" -s` → `l1_sentinel_<v>.log`
  (green; records the parent drop).
- The ladder stops at the first rung where S6 reads `d ≥ 4.0` with `levels: arms [y y y y] arm1@44.1k [y]` and
  the −6 dB re-read also `d ≥ 4.0` (plan §8). E1 is read here; its own lever is L2.
- **Stop-and-surface:** 1.00 does not clear S6 (plan §2.1 (b) options: `setChildrenPerEvent`,
  `setParentCount`, or an append-only higher-ceiling setter in `bloom_engine.h`, each only on a ruling).

### T027 — RULING: bloom lever (FR-010b, step 1)

- Table per rung: S6 d, E1 d and attribBase, arms, −6 dB d, parent drop (dB), CPU delta (T025). Record the
  ruling; rebuild at the ruled value; per-push `dsp_systems_tests` green.

---

## Group 7 — L2 routes (sequential; `vorago_ecosystem_lever_test.cpp`, then `vorago_voice.h`)

### T028 — `VoragoVoice_RouteLeverZeroAtZeroLane` (FR-012, SC-009, 13b FR-017 / FR-019)

- **File:** `dsp/tests/unit/systems/vorago_ecosystem_lever_test.cpp` (append; follow the
  `VoragoVoice_EcosystemLeverMapping` pattern at `:1305-1333`).
- **TEST_CASE** `[systems][vorago]`: lever voice at 48 kHz, note on.
  1. `Probe::injectEco(*voice, uniformLanes(0.0f))`, `Probe::advanceLifeOnly(*voice)`,
     `Probe::schedLanes(*voice, S)`: `voice->cloud().getMutation() == Probe::mutationBase(*voice)` and
     `voice->bloom().getDepth() == Probe::bloomDepthBase(*voice)` exactly, and
     `voice->getGhostRequest() == VoragoVoice::combineWake(0.0f, 0.0f, S[kKindGhost][0])` exactly.
  2. For raw ∈ {0.1, 0.5, 1.0}: mutation `== std::clamp(base + VoragoVoice::kPartialLaneGain * raw, 0, 1)`,
     bloom depth likewise, ghost `== combineWake(0.0f, std::min(1.0f, VoragoVoice::kGhostLaneGain * raw), S[kKindGhost][0])`.
- **Fails** to compile before T030.

### T029 — Surface `VoragoVoice_EcosystemLaneShapingFidelity` (FR-034)

- Append to `fr034_surfaced.md`: `vorago_ecosystem_lever_test.cpp:1266`, assertions at `:1324-1329`
  (mutation `== clamp(base + raw)`, ghost `== combineWake(0, raw, sched)`). It breaks if `kPartialLaneGain` or
  `kGhostLaneGain` is ruled ≠ 1. Rung: L2. Re-spec: the same identities with the gain applied, as in T028
  step 2. (This entry is not in plan §5's list; found while writing T028.)

### T030 — Add `kPartialLaneGain` and `kGhostLaneGain` at the no-change value 1.0 (FR-012, plan §2.2)

- **File:** `vorago_voice.h`. ODR-sweep both names first (append to `odr_sweep.txt`).
- **Implement:** `static constexpr float kPartialLaneGain = 1.0f;` and `kGhostLaneGain = 1.0f;` beside the 13b
  spans (`:1607-1624`). At `:2124-2126`: `mutationBase_ + kPartialLaneGain * partialEco` and
  `bloomDepthBase_ + kPartialLaneGain * partialEco` (clamps unchanged). At `:2128`:
  `combineWake(0.0f, std::min(1.0f, kGhostLaneGain * lanes.eco[kGhost][0]), lanes.sched[kGhost][0])`.
  `kLeverInputGain` and its `static_assert` (`:1665-1670`) are **not** edited.
- **Verify:** T028 green; `VoragoVoice_EcosystemLever*`, `…LaneShapingFidelity`, `VoragoVoice_WakeCombineRule`,
  `VoragoVoice_AgentReductionRule` green unedited; per-push systems green.

### T031 — L2 ladders, in order E1 → E3 → E4 → E5 (FR-012, FR-021, plan §2.2, §2.10)

- **E1 first, with no route change** (Bloom Colony on the T027 tree) → `l2_E1_base.log`. Ladder only if
  short: `kPartialLaneGain` 1.5, 2, 3.
- **E3** (Swarm Breath): `kNoiseLevelLeverSpanDb` 12 → 15, 18 (above 18 is inert: `writeNoiseLevel` clamps at
  +12 dB, `vorago_voice.h:1691`). A lower noise base is a level trim the override string may carry.
- **E4** (Feeding Loops): `kLoopGainLeverSpan` 0.18 → 0.24, 0.30; then `kCouplingLeverSpan` 0.30 → 0.38, 0.44
  (bounds `feedback_ecology.h:264-272`).
- **E5** (Haunted Colony): `kGhostLaneGain` 1 → 1.5, 2.
- **At every rung, alone:** the rung's own preset → `l2_<cell>_<rung>.log` (its `route arms:` line passes
  only with `d(R_k, R_k0) ≥ 4.0` **and** `≥ attribBase + 1.5`, arms green); Colony Pulse
  `VORAGO_PILOT_SECONDARY=1` and Teeming `VORAGO_PILOT_SECONDARY=1 VORAGO_PILOT_CELLS=S7` →
  `l2_secondaries_<rung>.log`; Slow Bloom (S6 re-read; R step).
- At any rung ≠ 1 on the two new gains, apply T029's re-spec before the per-push run.

### T032 — RULING: route levers (FR-010b, step 2)

- Table per cell: rung → d, attribBase, margin, arms; E7.hi, D13.1 (with S7), D13.2 beside each. Where several
  rungs clear the own cell, propose the one that moves the secondaries furthest (plan §2.10). Record the
  rulings; rebuild with every ruled value; per-push systems green.

---

## Group 8 — L3 ghost (sequential; `vorago_engine.h`, `vorago_engine_test.cpp`)

### T033 — Surface `VoragoEngine_GhostConfiguration` clause 1 (FR-034)

- Append to `fr034_surfaced.md`: `vorago_engine_test.cpp:2767` REQUIREs `atmos.getDensity() == 0.30f`. Rung:
  L3, only if a density rung is ruled. Re-spec: `== VoragoEngine::kGhostDensity`.

### T034 — Name the ghost density at its no-change value (FR-013, plan §2.3)

- **File:** `vorago_engine.h`. ODR-sweep `kGhostDensity`.
- **Implement:** `static constexpr float kGhostDensity = 0.30f;` beside `kGhostBurstPeak` (`:271`); `:375`
  becomes `atmos_.setDensity(kGhostDensity);`.
- **Verify:** `VoragoEngine_GhostConfiguration` green unedited; per-push systems green.

### T035 — L3 ladders (FR-013, FR-024c)

- **Make-up first:** `kGhostTapMakeupDb` 12 → 15, 18, 21, 24 with the matching `kGhostTapMakeupGain` literal
  (`10^(dB/20)`: 5.6234133, 7.9432823, 11.220185, 15.848932), the arithmetic in the comment.
- **Density only if the make-up is short:** `kGhostDensity` 0.45, 0.60, 1.0.
- **Per rung, alone:** Choir of Absence (S9) and Haunted Colony (E5) → `l3_ghost_<rung>_<cell>.log`, each
  also with `VORAGO_PILOT_MASTER_TRIM=-6` → `…_minus6.log`; R step on S6, E1, E3, E4;
  `dsp_systems_tests.exe "VoragoEngine_GhostLevelProbe" -s` → `l3_ghost_level_probe_<rung>.log` (it prints
  `getGhostTapMakeupDb()` and the gain; a mismatched pair voids the rung).
- Stop at the first rung clearing S9 (`d ≥ 4.0`, arms green, −6 dB ≥ 4.0) and E5 (FR-021, arms green).

### T036 — RULING: ghost lever (FR-010b, step 3)

- Table, ruling, rebuild. If a density rung is ruled, apply T033's re-spec, then per-push systems green.
  Record the FR-013 sizing figures (ghost alone vs drone, loudest second, seconds sounding) from the ruled
  rung's `GhostLevelProbe` log in `rulings.md`.

---

## Group 9 — L4 feedback ecology, S4 (sequential; `vorago_voice_test.cpp`, then `vorago_voice.h`)

### T037 — `VoragoVoice_EcologyWetMakeupInstalled` (FR-017)

- **File:** `vorago_voice_test.cpp` (append; ODR-sweep the TEST_CASE name and `kEcologyWetMakeupDb` first).
- **TEST_CASE** `[systems][vorago]`: after `prepare`, `REQUIRE(v->ecology().getWetGain() ==
  Approx(VoragoVoice::kEcologyWetMakeupDb).margin(1e-4))`; after `reset()`, the same (configuration survives,
  `feedback_ecology.h:878`). Fails to compile before T038.

### T038 — Install `kEcologyWetMakeupDb` at the no-change value 0 dB (FR-017, plan §2.4)

- **File:** `vorago_voice.h`: `static constexpr float kEcologyWetMakeupDb = 0.0f;` and
  `ecology_.setWetGain(kEcologyWetMakeupDb);` in `prepare()` beside `setEcologyMix` (`:604`).
- **Verify:** T037 green; per-push systems green.

### T039 — L4 ladder and RULING (FR-017, FR-024, FR-024c, FR-010b step 4)

- Rungs 6, 12, 18, 24 dB (component ceiling, `feedback_ecology.h:585`). Per rung, alone: Feedback Mire (S4) →
  `l4_ecology_<dB>.log` and `…_minus6.log`; R step on E4. A pass needs `d ≥ 4.0`, **arm 2 green** (no silent
  pass, E-3), all four arms green, −6 dB ≥ 4.0. `kLoopWakeBase` is **not** a candidate (13b Gate 1 rests on it).
- **Stop-and-surface:** 24 dB is short. RULING: table, ruling, rebuild, per-push green.

---

## Group 10 — L5 attack: failing tests first (sequential)

### T040 — Find and surface default-attack-sensitive tests (FR-034, plan §8)

- `grep -n` every `vorago_*_test.cpp` under `dsp/tests/unit/systems/` and `plugins/vorago/tests/` for cases
  that read level inside the first 20 s of a default-attack note; enter each in `fr034_surfaced.md`. At least:
  `VoragoVoice_SilenceClearsEcologyAudio` (`vorago_voice_test.cpp:877`, `REQUIRE(steadyDb > -50.0)` at
  `:909-923`, which falls under −50 dB at every L5 rung). Re-spec: shorten stage 0 for the drive through
  `setEnvelopeStageTimeMs(0, …)` (`vorago_voice.h:1068`), keeping the −50 dB control and the −80 dB claim.
  **Do not edit yet.**

### T041 — `VoragoVoice_AttackCurveReach` (FR-016, SC-004)

- **File:** `vorago_voice_test.cpp`. **TEST_CASE** `[systems][vorago]`: default envelope, 48 kHz, no cavern,
  n = `VoragoVoice::kAttackShapePower`, T = 20 s:
  1. the first time `getEnvelopeOutput() / velocity ≥ 0.426` lies at `t/T` within ±0.02 of `0.426^(1/n)`;
  2. `|Δ| < 1e-3` per sample across the stage-0 → stage-1 boundary;
  3. at each stage boundary 1–3 the gain equals its stage level ±1e-4;
  4. a note-on while Running at 0.85 does not re-enter stage 0 and stays continuous (`|Δ| < 1e-3`).
- Fails to compile before T044.

### T042 — `VoragoVoice_AttackTracksStageTime` (FR-016 tracking, plan §2.5)

- **File:** `vorago_voice_test.cpp`. **TEST_CASE** `[systems][vorago]`: the whole default voice (every section at
  its `prepare()` value; no engine, no cavern) at 8 kHz on the `kIdentitySampleRate8k` pattern (`:2506`),
  rendered to 180 s. RMS(Sus) over the sustain stage; the first 1 s window at RMS(Sus) − 6 dB must lie in
  `[15.0, 20.0]` s (`[0.75·T0, T0]`, T0 = 20 s). Print the reach and the wall clock; tag `[long]` only if
  it costs more than ~15 s.
- Fails on the base curve (reach about 6 s, `compliance.md:518`).

### T043 — `Vorago_PresetSweep_MeasuredAttackWindow` (FR-016 harness, plan §3.3)

- **File:** `plugins/vorago/tests/integration/preset_sweep_test.cpp` (append beside the `Vorago_PresetSupport_*`
  arithmetic cases), tags `[vorago][preset]`.
- **Asserts:** `measuredAttackWindowEndSeconds(3.0, 7.0, 60.0) == 12.0`; `(nullopt, 7.0, 60.0)` and
  `(3.0, nullopt, 60.0)` give `nullopt`; `(3.0, 56.0, 60.0)` gives `nullopt` (later than captureEnd − 5);
  `(55.0, 3.0, 60.0) == 60.0` (boundary inclusive). Fails to compile before T045.

---

## Group 11 — L5 attack: implementation, ladder, ruling (sequential)

### T044 — Stage-0 reshape in the voice (FR-016 option (b), plan §2.5)

- **File:** `vorago_voice.h`. ODR-sweep `kAttackStageCurve`, `kAttackShapePower`, `stageCurveFor`,
  `attackStartLevel_`, `shapeAttack`.
- **Implement:** `kAttackStageCurve = EnvCurve::Linear`; `kAttackShapePower = 6` (plan's proposal; rebuilt
  per rung at T046); `stageCurveFor(int)`; the three writers (`:1061`, `:1880`, `:1882`) use it; private
  `float attackStartLevel_ = 0.0f;` latched in `noteOn` before `:944` when `mse_.getState() ==
  MultiStageEnvState::Idle`, zeroed wherever `mse_.reset()` runs (`:1797`); `shapeAttack` in the Standard loop
  at `:2449` (identity when `span ≤ 1e-6f`; `u^n` by repeated multiplication, no per-sample `pow`). Rewrite
  the `kStageCurve` doc (`:304`) to state the stage-0 exception, citing FR-016. The Growth loop is untouched.
  `kVoiceSizeBound` (`:437`) stays unless the `static_assert` fails (then surface).
- **Verify:** T041 and T042 green; apply the T040 re-specs; per-push systems green.

### T045 — Measured-reach attack window in the harness and the probe (FR-016, the FR-002 exception, plan §3.3)

- **Files:** `plugins/vorago/tests/preset_test_support.h`, `preset_pilot_test.cpp`.
- **Implement:** `TakeRecord` gains `std::vector<float> attackCapL, attackCapR;`, filled by `renderTake`
  (`:2052-2056`) instead of computing `t.attack` there (`attackReachSeconds` still computed there);
  `measuredAttackWindowEndSeconds(reachP, reachRev, captureEnd)`; skip reason
  `kSkipReachOutsideCapture = "reach outside capture"`. The capture stays
  `[0, attackWindowEndSeconds(def, comp)]`. Both attack descriptors are `describeWithEnergyFloor` over the
  first `round(W_meas·sr)` samples. Apply at all four sites (sweep `:2577-2600`, `:2683-2708`; pilot
  `:446-470`, `:530-552`). Each prints `W_end registered %.1f s, measured %.1f s`.
- **Verify:** T043 green; `vorago_tests "~[.perf]~[long]"` green.

### T046 — L5 ladder, option (a), D8.2 (FR-010, FR-016, FR-030)

- **Rungs:** `kAttackShapePower` = 1, 2, 4, 5, 6, 7 (n = 1 and 2 are measured only; T042 is expected red on
  them). Per rung, alone: Sudden Chasm (D9.1) → `l5_attack_n<k>.log` (cite `d_att`, `d_Sus`, both `W_end`,
  the `P` and `P_rev` reach lines; tracking needs `P_rev` reach ∈ [15, 20] s); the T042 result.
- **Option (a)**, scratch build, never shipped: stage 0 on `kStageCurve` (reshape bypassed) and the stage-0
  default 20 s → 40 s in `kDefaultStageTimesMs[0]` and the `envelope_params.h:56-62` default. **Before**
  measuring, write the prediction into the log header: `P_rev` reach about 6 s → about 11 s, so `d_att`
  should rise. → `l5_attack_optionA.log`. Revert; `git diff` clean on both files.
- D8.2's host as compiled at the proposed rung → `l5_d82.log` (must still verify).
- **Stop-and-surface:** no rung puts `P_rev` reach ≥ 15 s, or none clears `d_att ≥ 4.0` and
  `d_att ≥ d_Sus + 1.5`.

### T047 — RULING: attack lever (FR-010b, step 5)

- Table (rung → reach, d_att, d_Sus, both W_end, D8.2) with option (a) beside it; the 0.75 tracking floor goes
  to the user with it (plan §10.5). Rebuild at the ruled n; R step on S6, E1, E3, E4, E5, S9, S4.

---

## Group 12 — L6 life modulators (sequential; `vorago_voice_test.cpp`, then `vorago_voice.h`)

### T048 — `VoragoVoice_LifeLaneDepthZero` (FR-017b)

- **TEST_CASE** `[systems][vorago]`: at breathing depth 0 and tidal depth 0, over 60 s,
  `getBreathingGravityLane() == 0.0f` and `getTidalFogDepth() == 0.0f` exactly at every chunk. At depth 1:
  `|getBreathingGravityLane()| ≤ VoragoVoice::kBreathGravityLaneGain`, `resonance().getGravity()` within
  [−1, 1], `getTidalFogDepth()` within [0, 1]. Fails to compile before T049.

### T049 — Lane gains at the no-change value 1.0 (FR-017b, plan §2.6)

- **File:** `vorago_voice.h`. ODR-sweep both names. `kBreathGravityLaneGain = 1.0f`, `kTidalFogLaneGain = 1.0f`;
  `publishLifeLanes()` per plan §2.6 (`tidalFogDepth_ = std::clamp(kTidalFogLaneGain * tide_.getCurrentValue(),
  0.0f, 1.0f)`).
- **Verify:** T048 green; `VoragoVoice_LifeModulatorLanes` (`:2910`) green unedited; per-push systems green.
- Append to `fr034_surfaced.md`: `VoragoVoice_LifeModulatorLanes` `:2971-2972` breaks for a breath gain ≠ 1.
  Re-spec: extremes = `kBreathGravityLaneGain · 0.30 ± 0.01`.

### T050 — L6 ladder and RULING (FR-017b, FR-023, FR-010b step 6)

- Rungs 1.5, 2, 3 for each gain. Per rung, alone: Slow Bloom `VORAGO_PILOT_SECONDARY=1` (D14.1) and Drifting
  Strata `VORAGO_PILOT_SECONDARY=1` (D14.2) → `l6_life_<lane>_<g>.log`; R step on S6 and M4. Pass:
  `verifiedAt(Secondary)` true (`d ≥ 1.5`, state ok, conjunct ok).
- **Stop-and-surface:** a ladder tops out; plan §2.6's swell fallback is measured only on a ruling.
- RULING; apply the `LifeModulatorLanes` re-spec if the breath gain ≠ 1; per-push green.

---

## Group 13 — L7 macros (sequential; `vorago_macro_matrix.h`; Life last)

**Every rung:** edit `kRows` with designated initializers and `f` literals; an appended row bumps `kNumRows`
and the count comment (`:273-276`); `VoragoMacroTarget::Count` stays 41; build (`static_assert`s
`:1215-1234` compile); `VoragoMacro_NeutralIsIdentity` (`:1419`) green; then probe the showcase preset alone
→ `l7_<macro>_<rung>.log`. Every Movement and Gravity rung also runs `VoragoMacro_NoZipper` and
`VoragoMacro_SweepAxes` alone (every assertion that passed at T004 still passes) before it is ruled. The
Fog→CloudSpectralTiltDb row stays at amount 0 (`:603-615`). A rung that only lands in a clamp is reported,
not ruled. After each macro's ruling, the R step re-reads every earlier ruled cell its rows can reach.

### T051 — M5 cause table (FR-015, SC-005)

- Stone Gravity at Gravity 1.0, all with `VORAGO_PILOT_TAKES=4`, alone → `artifacts/m5_cause.log`:
  A0 `104=1`; A1 `104=0.5,400=<norm for +1.0>` (read the norm from `resonance_params.h:106`; print it);
  A2 `104=1` on a scratch rebuild with the ResonanceGravity row's `amount = 0` (logged, then reverted);
  A3 `104=1,1500=0`; A4 `104=0.5,1500=0`. Table: arm 3 (late − Sus dB) and arm 1 per arm, per take, plus the
  44.1 kHz line.
- **Stop-and-surface:** no single ablation brings arm 3 into [−18, +12] dB (surface with the lever proposal).

### T052 — M2 Age, M3 Density, M4 Movement, M9 Fog, M12 Mass (FR-014)

- Plan §2.7 rungs, in order, each a rebuild, each macro its own lever-table row and its own RULING (Q5):
  - **M2** (Erosion): Age→CloudSpectralTiltDb −4 → −7; Age→BodyDamping +0.55 → +0.70; new
    Age→CloudMutation (base 0.15, +0.40).
  - **M3** (Crowded Dark): re-read first; Density→NoiseLevelDb +6 → +12; new Density→EcologyMix (0.15, +0.30).
  - **M4** (Drifting Strata): Movement→CavernDamperDepth +0.45 → +0.60; new Movement→TidalDepth (0.40, +0.30);
    new Movement→CloudMutation (0.15, +0.25). The wander rates are not touched (13b's zipper axis).
  - **M9** (Fogbound): Fog→CavernFog +0.55 → +0.70; new Fog→SmearDecoherence (its existing base, +0.40);
    Fog→SmearAmount +0.70 → +0.80.
  - **M12** (Monolith): Mass→SubToneLevelOffsetDb +3 → +6; new Mass→BodyDamping (0.25, −0.15);
    Mass→BodyResonance +0.28 → +0.29.
- A pass: `d ≥ 4.0`, arms green in the same printout.

### T053 — M5 Gravity lever (FR-014, FR-015, SC-005, E-6)

- Rungs, all with `VORAGO_PILOT_TAKES=4`: the T051-named row or lane change (for example the OctaveLock row's
  curve Linear → SCurve, or a smaller amount) **plus** new Gravity→CloudSpectralTiltDb (base −4, −3); then
  Gravity→ResonanceMix (0.45, +0.25). Pass: `d ≥ 4.0`; arm 3 within [−18, +12] dB **and** arm 1 green on all
  4 takes; `arm1@44.1k` PASS. NoZipper and SweepAxes per rung (Gravity is bipolar). One RULING, one row.

### T054 — M10 Life, last (FR-014, E-5, plan §2.10)

- Re-read Teeming first, on the tree with every earlier lever ruled. Then Life→BloomSpawnRateHz +0.0208 →
  +0.0458 (reaches `kMaxSpawnRateHz`); then new Life→BreathingDepth (0.30, +0.40). Every rung re-reads E7.hi
  and D13.2 (Colony Pulse, `VORAGO_PILOT_SECONDARY=1`), D13.1 with S7 (Teeming, `VORAGO_PILOT_CELLS=S7`), and
  13b Gate 1 at Life max (`VORAGO_PROBE_SEEDS=6 VORAGO_PROBE_KNOBS=- VORAGO_PROBE_SURFACE=109=1`, ratio ≥ 0.5).
- RULING. **Stop-and-surface:** E7.hi, D13.1 (or its S7 conjunct) or D13.2 still short with every named lever
  at its ladder top — surfaced with readings across all rungs, never UNMET.

---

## Group 14 — Boundedness guard on the ruled tree (sequential; `vorago_ecosystem_lever_test.cpp`)

### T055 — `VoragoEngine_CapabilityLeverBounded` (SC-013, E-12, E-13)

- **TEST_CASE** `[systems][vorago]` following `VoragoEngine_EcosystemLeverBounded` (`:1755`): six voices held;
  all 12 macros at 1, with Gravity run at **both** 0 and 1; bloom depth 1 and spawn rate
  `BloomEngine::kMaxSpawnRateHz`; ghost peak 1; eco lanes injected at 1 through `Probe`; 60 s per arm at 44.1,
  48 and 96 kHz. Inside `TestHelpers::AllocationScope` only plain locals accumulate (no REQUIRE or INFO
  inside). After it: `REQUIRE(allocations == 0u)`; `getAllocatedBytes()` equals its after-prepare value;
  every sample finite by `detail::isFinite`; `|out| ≤ 0.9661f`. Include `<allocation_detector.h>` only, never
  `<allocation_operator_overrides.h>`.
- Print the wall clock. **If it exceeds 4 minutes measured alone, stop and surface** (do not shorten, split or
  tag it `[long]`).
- This is a guard on the ruled tree, expected green; a red is a defect in a ruled lever.

---

## Group 15 — Final tree: gates, no regression, re-harvest, CPU (sequential; one run at a time)

### T056 — F1: every roster gate on the final binary (FR-020–FR-026, SC-002–SC-006, SC-020, SC-023)

- Build once; nothing else running. Each roster preset with `VORAGO_PILOT_TAKES=4` and its ruled override
  string (or none) → `final_<cell>.log`; then plus `VORAGO_PILOT_MASTER_TRIM=-6` → `final_minus6_<cell>.log`.
  T016's four secondary runs → `final_secondaries.log`.
- Each compliance row will cite the verdict line, the `take:` line, the `levels:` line and, for routes, the
  `route arms:` line; for D9.1, both `W_end`s and the reach lines.
- **Stop-and-surface:** any roster cell below its bar (FR-027).

### T057 — F1: no regression on verified cells (FR-030, FR-030b, SC-007, SC-007b)

- `VORAGO_PILOT_ITERATE=verified27` (no override) → `final_verified27.log`: `derived == 27` holds and every
  row is PASS with arms green.
- The FR-030b set (T017's list) on its hosts with `VORAGO_PILOT_SECONDARY=1` → `final_secondaries_030b.log`;
  `VORAGO_SWEEP_SHARD=42/43 … "Vorago_PresetSweep_AblationVerifiesClaims"` → `final_default_state.log`.
- **Stop-and-surface:** any de-verified cell; the lever that caused it is re-opened, not adopted as is.

### T058 — F2: Phase 10 bounds, 13b gates, default surface after (FR-005, FR-031, FR-032, SC-011, SC-012, SC-012b, SC-018)

- Alone: `VoragoMacro_SweepAxes -s` → `final_sweepaxes.log`; `VoragoMacro_NoZipper -s` → `final_nozipper.log`
  (every T004-passing assertion passes; zipper ≤ 1.5×).
- GATE1M default and Life max → `final_gate1_{default,lifemax}.log` (ratio ≥ 0.5). The 14-knob tables →
  `gate2_table_{default,lifemax}.log` (syncRate counts on the default surface; selfAffinity and syncRate count
  at Life max).
- `VORAGO_PROBE_KNOBS=-` at the default surface → `default_after_13b.log`; `Vorago_PresetPilot_Calibrate` →
  `default_after_p0.log` (P0 descriptor, M1 RMS, finite, peak ≤ 0.9661).

### T059 — F2: fingerprint re-harvest, once (FR-035, SC-016, E-14)

- Run the `VoragoEngine_GhostExtensionWiring` printer inside `dsp_systems_tests` twice →
  `reharvest_run1.log`, `reharvest_run2.log`; the literals must be byte-identical (record both md5s). Write
  them into `atmosphere_ghost_fixtures.h` with a new PROVENANCE block (tree hash, date, md5). Any other moved
  fingerprint follows the same rule. Verify run green → `reharvest_verify.log`.

### T060 — F2: CPU after (FR-033, SC-014)

- Machine idle and cool: `node tools/run-cpu-tests.js dsp_systems_tests` → `cpu_final_dsp.log`, then
  `node tools/run-cpu-tests.js vorago_tests` → `cpu_final_vorago.log`; `VoragoVoice_CloudFloorCpuProbe` →
  `cpu_floor_final.log`. Clause (i), clause (ii), P/D ≤ 1.05; the delta vs T006 in ns and %.
- A red is re-run alone after the machine idles before it is treated as a defect. Never relax a budget.

---

## Group 16 — Integration (sequential; last)

### T061 — CMake registration (the ONE CMake task; plan §6)

- This phase adds **no** TU, so no list changes. Verify instead that every TU it edited is registered:
  `dsp/tests/CMakeLists.txt` (`vorago_voice_test.cpp`, `vorago_engine_test.cpp`, `vorago_macro_test.cpp`,
  `vorago_perf_test.cpp`, `vorago_ghost_ext_test.cpp`, `vorago_ecosystem_lever_test.cpp`, all in
  `dsp_systems_tests`, `:324`) and `plugins/vorago/tests/CMakeLists.txt` (`preset_pilot_test.cpp`,
  `preset_sweep_test.cpp`).
- `dsp_systems_tests.exe --list-tests` and `vorago_tests.exe --list-tests` (plus `"[.probe]"` and `"[.perf]"`
  for hidden cases) must list every new TEST_CASE: `VoragoVoice_CloudFloorCpuProbe`,
  `VoragoVoice_CloudRichnessFloor`, `…CloudRichnessFloorNeutral`, `…BloomChildrenAudible`, `…CloudFloorEdge`,
  `…RouteLeverZeroAtZeroLane`, `…EcologyWetMakeupInstalled`, `…AttackCurveReach`, `…AttackTracksStageTime`,
  `…LifeLaneDepthZero`, `VoragoEngine_CapabilityLeverBounded`, `Vorago_PresetSweep_MeasuredAttackWindow` →
  `artifacts/test_registration.txt`.
- `git diff --name-only 64f57e1a -- '*CMakeLists.txt'` must be empty. A missing case is fixed in its TU.

### T062 — Full-suite run (FR-034, FR-036, FR-037, SC-015, SC-017, SC-019, SC-021, SC-022)

- Build every target, zero warnings → `final_build.log`.
- Alone, each to `final_suite_<exe>.log` with `"~[.perf]~[long]"`: `dsp_core_tests`, `dsp_primitives_tests`,
  `dsp_processors_tests`, `dsp_systems_tests`, `dsp_effects_tests`, `vorago_tests`, `seraphis_tests`,
  `shared_tests`. Then the `[long]` sets of `dsp_systems_tests` and `vorago_tests` (Phase 10 soaks,
  `soak_test.cpp`) → `final_long_<exe>.log`. 100 % pass; test-file edits only under `fr034_surfaced.md`.
  Cite `param_table_test`, `state_v2_test`, `state_v3_test` and `VoragoEngine_SlotSeedReproducibility` by name.
- `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Vorago.vst3"` →
  `final_pluginval.log`.
- `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`, then `-Target vorago` →
  `final_tidy_{dsp,vorago}.log`: 0 findings.
- `git diff --name-only 64f57e1a` (committed and working tree) → `git_diff_names.txt`: no `plugins/seraphis/**`,
  `harmonic_cloud.h`, `atmosphere_engine.h`, `continuous_body.h`, `aether_reverb.h`, `entropy_processor.h`, life
  modulator or `seraphis_*.h`; and no `bloom_engine.h`, `feedback_ecology.h` or `multi_stage_envelope.h` unless a
  recorded ruling allowed it.

### T063 — Portability check (FR-036, SC-021)

- `node tools/check-portability.js` → `final_portability.log`: clean. Then `wsl --shutdown`.
- **Stop-and-surface:** any finding. Fix it in source, rebuild, and re-run the affected T062 suite.

### T064 — Compliance, from the checked-in logs only (FR-040–FR-042, every SC)

- **File:** `specs/vorago-phase13c-capability-audibility/compliance.md`.
- The FR-040 lever table, one row per feature (bloom; routes; ghost; S4; attack; life modulators; M2, M3, M4,
  M5, M9, M10, M12): cells served; every mechanism in the change set; before; after; every candidate (value →
  reading → log line); the ruled value; the override string. The FR-041 default-render note (T005 vs T058).
  The FR-042 hand-off list (per showcase preset, its F1 override string; any descriptor-visible change to the
  E0 / P0 default surface). The SC-007 and SC-007b before/after tables. One row per FR and SC, each citing a
  log file and line read at this step. An unresolved stop-and-surface item is written as such, never as UNMET
  or as met.

---

## Open questions (carried to the user; none blocks Groups 0–5)

1. **Child-gain ladder values (T026).** The plan names no rungs for `kBloomChildGain`; this list proposes
   0.35, 0.50, 0.70, 1.00.
2. **`VoragoVoice_EcosystemLaneShapingFidelity` (T029)** asserts the un-gained Partial and Ghost identities
   (`vorago_ecosystem_lever_test.cpp:1324-1329`) and is missing from plan §5's FR-034 list. It is added here
   as a surfaced item, re-specced only if a gain ≠ 1 is ruled.
3. Plan §10 items 1, 3 and 5 (the Q2 reading of "always on", the `kPartialLaneGain` / `kGhostLaneGain`
   reading of FR-012, and the 0.75 attack-tracking floor) are still for the user to confirm before T023,
   T030 and T047 respectively.
