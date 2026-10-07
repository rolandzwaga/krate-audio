# Tasks: Vorago Phase 13d — Engine Ceilings

**Spec:** `specs/vorago-phase13d-engine-ceilings/spec.md` (Clarifications Q1–Q8 and OQ-1, 2026-10-07)
**Plan:** `plan.md` (same directory)
**Branch / base:** `feat/vorago-phase1-events-modulation` at `502e5243`
**Status:** TASKS. Dependency-ordered. No commit tasks; commits happen outside this workflow.


**Main-loop pre-rulings (2026-10-07, before the build stage; the conditional user rulings in the header stand):**
1. FR-034 self-listing: yes — `VoragoVoice_EcosystemLaneShapingFidelity` hard-codes gain 1 (`vorago_ecosystem_lever_test.cpp:1334-1337`), so any adopted change to `kPartialLaneGain` edits it; it is listed under FR-034 and the edit is the new compiled value, nothing else.
2. Re-aims inside the roadmap families: Age → CavernDarkness and Movement → BreathingIrregularity (and the Gravity → tilt / sub / darkness rungs) are "re-aimed or given a target the descriptor hears" in the roadmap's words; they stay in the ladders. A re-aim is adopted only under FR-010d like any rung.
3. T-M2 (meter on vs off bit-identical in one process) is measured, not assumed: if it fails on MSVC the task is a stop-and-surface item with the first differing sample, never a tolerance chosen by the main loop.
4. T015 plumbing: add the meter flag to `computeTakes` in `preset_test_support.h` (one RenderSpec field), not an empty-tweak detour through `computeLeverTakes`.
5. T019 `VORAGO_PILOT_LIST=1`: keep it; sweep the name and append it to `odr_sweep.txt` (plan §10).

## How to read this file

- **Groups run strictly in order.** Inside a group, a task marked **[P]** may run in parallel with the other [P]
  tasks of that group, because it creates **new files only** and no other task touches them. This phase creates
  **no new source file and no new TU** (plan §0 item 1, §7): every code task edits a shared, already-registered
  file (`vorago_voice.h`, `vorago_engine.h`, `vorago_macro_matrix.h`, the existing test TUs,
  `preset_test_support.h`, `preset_pilot_test.cpp`, `ecosystem_rule_probe_test.cpp`,
  `vorago_macro_retune_probe_test.cpp`, `tools/vorago_preset_defs.h`), and every measurement task must run alone
  (isolation rule below). **So no task in this list is [P]; every group is sequential.**
- Every task is self-contained: the files, the test or probe to run **first** (and what it shows before the
  change), the implementation intent, and the target that verifies it.
- Canonical loop for every code task: failing test → implement → **zero warnings** → tests pass.
- **Build** (Windows, always the full path):
  `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target <targets>`.
  A dsp-header change needs **both** `dsp_systems_tests vorago_tests` (the plugin links the headers, plan §7).
- **Run one suite directly** (never `ctest -R <exe>`):
  `build/windows-x64-release/bin/Release/<target>.exe "<TestName*>" 2>&1 | tail -5`
- **Per-push runs:** `dsp_systems_tests.exe "~[.perf]~[long]"` and `vorago_tests.exe "~[.probe]~[long]~[perf]~[.perf]"`.
- **Probe run** (one preset), from the repo root:
  `VORAGO_PILOT_PRESET="<Preset Name>" [env] build/windows-x64-release/bin/Release/vorago_tests.exe "Vorago_PresetPilot_PrimaryProbe" >> specs/vorago-phase13d-engine-ceilings/artifacts/<name>.log 2>&1`,
  after first writing the line `START <date time> <label> tree=<git short hash>[+dirty] <candidate>` into the log.
- **Isolation.** Probe runs, `[long]` runs and CPU runs go to a log in `artifacts/`, **one at a time, with nothing
  else running** (no build, no other suite, no clang-tidy). Never re-run a slow run to see its output again: read
  the log.
- **Ladder rungs.** A **seam rung** is a `VORAGO_PILOT_LEVER="<key>=<v>,…"` run on the instrumented binary (no
  rebuild). A **rebuild rung** edits one `constexpr` or `kRows` entry, rebuilds `dsp_systems_tests vorago_tests`,
  measures, logs, then is kept or reverted as FR-010d decides. Every rung writes up to four logs:
  - `artifacts/<cell>_r<n>_gate.log` — the cell's gate surface;
  - `_roster.log` — `VORAGO_PILOT_ITERATE=roster`, the FR-010c (a) set (earlier ruled roster cells);
  - `_readset.log` — `VORAGO_PILOT_ITERATE=liveFor:<lever-id>`, the FR-010c (b) set;
  - `_minus6.log` — the gate with `VORAGO_PILOT_MASTER_TRIM=-6` (FR-024c).
  A rung whose `_gate.log` misses the bar need not run the other three.
- **FR-010d adoption (no user stop).** The build adopts the **smallest-change rung** (fewest mechanisms, then the
  smallest `|rung − compiled|` normalised by its ladder's span) that clears **all** of:
  - the cell's gate (the `d` bar; attributability `d ≥ attribBase + 1.5` for E1 / E4; for E4 also arms 2–3 `y` on
    all four take lines and `loopsAlive y`; for E6.hi / E7.hi the secondary `-> VERIFIED` lines);
  - every row of `_roster.log` and `_readset.log` verified (a sunk preset is recorded under FR-030 and does not
    block adoption, it joins the confirming pass's full re-author);
  - `levels: arms [y y y y] arm1@44.1k [y]`;
  - the `_minus6.log` bar.

  Each adoption is one line in `artifacts/rulings.md`: date, cell, lever, rung, gate `d`, read-set result, the
  `levels:` line, the −6 dB `d`, the FR-010d clauses it cleared, and the cited log lines.
- **USER RULING tasks** (only what FR-010d reserves, plan §11): E1's child-attachment change; a compiled E1 split;
  E4 W2; every FR-027 stop-and-surface; every FR-030 conflict whose re-author fails. Such a task stops, puts the
  ladder table in front of the user, and waits; nothing after it starts until the ruling is a line in
  `rulings.md`.
- **Re-open rule (FR-010b, Q8).** If a later lever's `_roster.log` shows an earlier ruled cell below its bar, that
  earlier lever is re-laddered **once** (re-open count in `rulings.md`). A second drop of the same cell is a
  stop-and-surface with both ladders' readings — never a third ladder (SC-027).
- **Stop-and-surface** means: record the measured figures; change no threshold, bar, window, K, take count,
  descriptor or test assertion; report to the user. A cell is never recorded UNMET and never met (FR-027).
- **Override scope (FR-025, FR-025b).** A gate override sets only the cell's own controls plus level trims, with
  two named exceptions (E4 `800=0.5`; E6.hi / E7.hi `109=<L>`) and companions to an adopted engine lever (E1
  `1300=…`, M10 `900=0.15`), each itemised in the lever table. The 36 sweep-5-verified primaries are read **as
  compiled**.
- **FR-034 surfaced list.** `artifacts/fr034_surfaced.md` (created at T001) holds one entry per pre-existing test
  that encodes old voicing as data: name, file:line, reason, rung, proposed edit. A test is entered **before** it is
  edited, never edited silently. Before the first rebuild rung of each cell, grep `dsp/tests` and
  `plugins/vorago/tests` for the literal of every constant or row amount the rung touches; a new hit is entered
  first.
- **ODR (FR-038).** Before writing any name not in plan §10, run `grep -rn "<Name>" dsp/ plugins/ tools/` and append
  the result to `artifacts/odr_sweep.txt`.

### Fixed facts every task relies on (read this session on `502e5243`)

| Fact | Where |
|---|---|
| `kPeakWakeBase = 0.45f`, `kLoopWakeBase = 0.45f` (public) | `dsp/include/krate/dsp/systems/vorago_voice.h:430-431` |
| `prepare(double, const VoragoVoiceConfig&)` `:525`; `reset()` `:799`; in prepare: `ecology_.setWetGain(kEcologyWetMakeupDb)` `:644`, ring coupling at `kRingCouplingBase` `:650-653`, `loopWakeBase_.fill(kLoopWakeBase)` `:654`, `setBloomDepth(0.60f)` `:657`, `bloom_.setChildGain(kBloomChildGain)` `:658`; no `setParentCount` / `setChildrenPerEvent` call | `vorago_voice.h` |
| `processStereoBlock(float* outL, float* outR, std::size_t n)` serves `carryL_` / `carryR_` through `carryRead_` / `carryAvail_` / `take`, calling `renderOneChunk()` when empty | `vorago_voice.h:902-929` |
| `setEcologyMix` `:1402`; `setEcosystemDepth` (clamp [0,1], non-finite rejected) `:1501-1506`; `setEcosystemSyncRate` `:1547`; `setEcosystemSelfAffinity` (diagonal) `:1556-1561`; `setEventRateScale` (clamp [0.1,10]) `:1566-1571`; `setBloomDepth` writes `bloomDepthBase_` `:1576-1590`; `setBloomSpawnRateHz` / `getBloomSpawnRateHz` `:1593-1594`; 13c seam forwards `:1600-1603` | `vorago_voice.h` |
| Accessors `cloud()` … `ecosystem()` | `vorago_voice.h:1637-1642` |
| `kNoiseLevelLeverSpanDb = 12` `:1679`, `kPeakLevelLeverSpanDb = 18` `:1680`, `kLoopGainLeverSpan = 0.18f` `:1681`, `kPartialLaneGain = 1.0f` `:1685`, `kGhostLaneGain = 1.0f` `:1686`, `kRingCouplingBase = 0.06f` `:1694`, `kCouplingLeverSpan = 0.30f` `:1695`, `kFreqWanderLeverSpanSemis = 3.0f` `:1701`, `kWanderLeverRateCompExponent = 0.75f` `:1725`, `wanderLeverRateComp` `:1726-1731`, `kLeverInputGain{1,2,2,2,1}` and the Partial/Ghost `static_assert` `:1742-1747`, `kMinRetunedWakeBase = 0.05f` `:1750`, the wake-base `static_assert` `:1754-1755` | `vorago_voice.h` |
| `slotCountForKind(std::size_t)` `:2018`; `struct IdentityLanes` `:2049` | `vorago_voice.h` |
| `applyIdentityLanes` `:2203`: loop wake `combineWake(loopWakeBase_[l], lanes.eco[kFeedback][l], lanes.sched[kFeedback][l])` `:2224-2228`; mutation write `:2232`; bloom-depth write `:2233`; `loopGainOffset_[l] = kLoopGainLeverSpan * x` `:2275`; coupling `clamp(kRingCouplingBase + kCouplingLeverSpan * x, 0, kMaxCouplingPerPair)` `:2279-2280` | `vorago_voice.h` |
| `publishIdentity()` builds a local `IdentityLanes`, gathers, then `applyIdentityLanes(lanes)` | `vorago_voice.h:2369-2380` |
| `renderOneChunk()` `:2547`; step 7 `ecology_.processBlock(excL_.data(), excR_.data(), excL_.data(), excR_.data(), n);` `:2610` | `vorago_voice.h` |
| Engine: `prepare` `:352`; `getAllocatedBytes()` `:557`; 13c seams `setBloomChildGain` `:1040`, `setEcologyWetMakeupDb` `:1047`, `setGhostDensity` (rejects non-finite itself) `:1056-1066` — fan-out over `kMaxVoices`, getters read slot 0; `processStereoBlock` `:1175`; `slice = std::min(n - done, kControlChunkSamples - phase)` `:1194`; voice loop `:1205-1226` (`processStereoBlock(vL_.data(), vR_.data(), slice)`, FR-072 `isFiniteBits` break `:1220-1223`, `busL_[s] += a; busR_[s] += b;` `:1224-1225`); `getVoice(std::size_t) const` `:1371` | `dsp/include/krate/dsp/systems/vorago_engine.h` |
| `kNumRows = 51` and the per-macro tally comment `:272-276`; `static_assert(kRows.size() == kNumRows)` `:1223-1224`; one base per target `:1233-1234`; `VoragoMacroTarget::Count == 41` `:1239-1240` | `dsp/include/krate/dsp/systems/vorago_macro_matrix.h` |
| `kDefaultParentCount = 4` `:211`, `kDefaultChildrenPerEvent = 2` `:213`, `kMaxSpawnRateHz = 0.05f` `:285`, `getSmoothedDepth()` `:688`, `getParentCount()` `:690`, `getChildrenPerEvent()` `:691`, `getLiveChildCount()` `:717`, event / child counters `getSpawnEventCount()` … `getFallbackChildCount()` `:729-745` | `dsp/include/krate/dsp/systems/bloom_engine.h` |
| `kMaxLoops = 6` `:193`; `processBlockTapped(inL, inR, outL, outR, loopTaps, n)` `:979`; `getNumLoops()` `:1343`; `getLoopGain(loop)` `:1409` | `dsp/include/krate/dsp/systems/feedback_ecology.h` |
| `kNumKinds = 5` | `dsp/include/krate/dsp/systems/ecosystem_engine.h:168` |
| Lever-test friend `VoragoEcosystemLeverProbe` `:64` — `advanceLifeOnly` `:69`, `injectEco` `:77`, `injectEcoAll` `:83`, `loopGainBase` `:91`, `loopGainOffset` `:97`, `leverInputGain` `:104`, `ringCouplingBase()` `:116`, `ringCouplingApplied` `:120`, `mutationBase` / `bloomDepthBase` `:133-134`, `partialLaneGain()` `:137`, `schedLanes` `:150`, `loopWakeBase` `:195`; `using Probe` `:223`; `uniformLanes(float)` `:245`; `kKindFeedback` / `kKindGhost` `:262-263`; `checkWakes` `:374-386`; `makeLeverVoice(double, std::uint32_t)` `:414`; `…LeverMapping` `:989`; `…LeverNeutral` `:1147`; `…LaneShapingFidelity` `:1276`; `…RouteLeverZeroAtZeroLane` `:1360` (setup `makeLeverVoice(48000.0, leverSeed())`, `noteOn(65.406f, 100.0f / 127.0f)`); `VoragoEngine_CapabilityLeverBounded` `:1974`; `VoragoEngine_GhostDensityRoute` `:2213` | `dsp/tests/unit/systems/vorago_ecosystem_lever_test.cpp` (2261 lines) |
| `makeNaNFloat()` `:112`; `isFiniteBits` `:147`; `makeNonFiniteFloat(std::uint32_t)` `:2135`; `kPosInfBits = 0x7F800000u` `:2143`; `VoragoEngine_SetterContract` `:3071` (fixture `VoragoEngineConfig cfg{}; cfg.atmosCaptureSeconds = 1.0f; auto engine = makeEngine(kSampleRate8k, cfg);`) | `dsp/tests/unit/systems/vorago_engine_test.cpp` (3328 lines) |
| `VoragoMacro_SweepAxes` `[long]` `:1211` (Movement CHECKs `:1318-1319`); `VoragoMacro_NeutralIsIdentity` `:1419`; `VoragoMacro_NoZipper` `[long]` `:1514` | `dsp/tests/unit/systems/vorago_macro_test.cpp` |
| Row emulation `matrix.setTargetBase(t, matrix.getTargetBase(t) + delta[i])` `:326`; `VoragoMacro_Phase12RetuneProbe` `:380`; `VoragoMacro_Phase12DriveProbe` `:497`; `spearmanRho` `:96` | `dsp/tests/unit/systems/vorago_macro_retune_probe_test.cpp` (548 lines) |
| `dsp_systems_tests` lists `vorago_voice_test.cpp` `:511`, `vorago_engine_test.cpp` `:513`, `vorago_macro_test.cpp` `:515`, `vorago_ghost_ext_test.cpp` `:538`, `vorago_macro_retune_probe_test.cpp` `:552`, `vorago_ecosystem_lever_test.cpp` `:560` | `dsp/tests/CMakeLists.txt` |
| `vorago_tests` lists `ecosystem_rule_probe_test.cpp` `:52`, `factory_preset_test.cpp` `:57`, `preset_sweep_test.cpp` `:58`, `preset_matrix_test.cpp` `:59`, `preset_pilot_test.cpp` `:60`, `preset_cpu_test.cpp` `:62`; those TUs are in the `-fno-fast-math` list `:146-156` | `plugins/vorago/tests/CMakeLists.txt` |
| `RenderSpec` (with `engineTweak`) `:284-299`; `SweepCapture` (`blockPowerL/R`, `tweakHeld`) `:301-310`; `detail::readTweakLevers` returns `std::array<float, 4>` `:321-324`; `renderPreset` `:333`; the tweak applied through `host.engineForTweak()` `:371-383`; block-0 re-read `:451-454`; `struct VerificationVector` `:1082-1084`; `kArmWindowSeconds` `:1768`; `kSilenceDb = -60.0` `:1770`; `kPowerFloor = 1e-24` `:1778`; `spanPowerDb` `:1808-1820`; `windowDb` `:1822`; `tenSecondWindows` `:1873`; `evaluateArms` `:2012` (hold windows `tenSecondWindows(tl.A, tl.H, sr)` `:2029`); `computeVerificationVector(def, comp, tl, selfDistance, pSus, threads, storedTake = nullptr)` `:2580-2583` | `plugins/vorago/tests/preset_test_support.h` (3368 lines) |
| `engineForTweak()` (non-const) | `plugins/vorago/tests/vorago_preset_host.h:178-184` |
| Pilot: `struct PilotLever` `:870`; `readPilotLever()` `:881-934` (four keys; unknown key `FAIL` `:917-918`); `computeLeverTakes` `:942-1003`; `runPrimaryProbe` `:1010`, its secondary-under-lever `FAIL` `:1015-1019`; `route arms:` `:1116`; verdict line `:1188`; `levels:` `:1196`; secondary block `:1222-1248` (`computeVerificationVector` call `:1234`); `Vorago_PresetPilot_Calibrate` `:577`; `Vorago_PresetPilot_PrimaryProbe` `:1347`; `VORAGO_PILOT_ITERATE` branch `REQUIRE(*iter == "verified27")` `:1350-1354`; `VORAGO_PILOT_OVERRIDE` `:1394`; `VORAGO_PILOT_MASTER_TRIM` `:1427` | `plugins/vorago/tests/integration/preset_pilot_test.cpp` (1503 lines) |
| Rule probe: anonymous namespace `:176`; `readEnv` `:211`; knob rows `syncRate` `:310`, `selfAffinity` `:321`; `struct RenderResult` `:327`; `renderOnce` `:360`; `colonyEngine` `:417`; `DESCRIPTOR` print `:534`; `Vorago_EcosystemRuleProbe` `:544`; options `VORAGO_PROBE_SURFACE` `:553`, `_KNOBS` `:555`, `_REF` `:558`, `_WAKEBASE` `:564`, `_GR` `:572`, `_SEEDS` `:583` | `plugins/vorago/tests/integration/ecosystem_rule_probe_test.cpp` (1016 lines) |
| Param ids `kMacroLifeId = 109` `:110`, `kEcologyMixId = 500` `:155`, `kEventsRateScaleId = 800` `:177`, `kEcosystemDepthId = 900` `:180`, `kBloomDepthId = 1300` `:220` | `plugins/vorago/src/plugin_ids.h` |
| MB route helpers | `plugins/vorago/src/parameters/param_routes.h` |
| Preset defs: E6.hi twin `{kEcosystemSyncRateId, 0.0}`, E7.hi twin `{kEcosystemSelfAffinityId, 0.25}` `:187-190`; `requiredPrimaryCells()` `:297`; Smeared Horizon `:437`; Slow Bloom `:532`; Colony Pulse `:562` (Life 0.45 `:574`); Hull Resonance `:654`; Erosion `:711`; Drifting Strata `:771`; Stone Gravity `:800`; Teeming `:944` (Life 0.70 `:952`, events 0.150515 `:953`, depth 1.0 `:954`); Bloom Colony `:1040` (bloom depth 0.50 `:1049`); Feeding Loops `:1139` (events 0.389076 `:1159`); Haunted Colony `:1172` | `tools/vorago_preset_defs.h` |
| `Vorago_FactoryPresets_TreeMatchesGenerator` | `plugins/vorago/tests/unit/preset/factory_preset_test.cpp:1807` |
| Preset regeneration target `generate_vorago_presets` | root `CMakeLists.txt:730-731` |
| Sweep-5 records: 43 files `record_*.txt` | `f:/tmp/p14/sweep5-out/` |
| Lane runners and gates: `tools/run-close-lanes.js`, `tools/run-cpu-tests.js`, `tools/check-portability.js` | `tools/` |

### Roster (spec "Cell roster")

| Cell | Gate surface | Verification | Bar | Sweep-5 "before" |
|---|---|---|---|---|
| E1 Partial → bloom | Bloom Colony (+ adopted `1300=…` companion) | route arms | d ≥ 4.0 and d ≥ attribBase + 1.5 | 1.1823, attribBase 1.1035 |
| E4 Feedback → loop wake | Feeding Loops at `800=0.5` (1.0× events) | route arms + loops alive | as E1; arms 2–3 on all four takes; `loopsAlive y` | 2.9182 (attrib 0.9851); 1× premise 3.71 |
| E6.hi | Colony Pulse at `109=<L>` | ExtReversion, secondary | `verifiedAt(Secondary)`: d ≥ 1.5, state, conjunct | 1.3700 |
| E7.hi | Colony Pulse at `109=<L>` | ExtReversion, secondary | as E6.hi | 1.3430 |
| M2 Age | Erosion | macro reset | d ≥ 4.0 | 3.2191 |
| M5 Gravity | Stone Gravity | macro reset (to 0.5) | d ≥ 4.0; arms 1 and 3 on four takes and at 44.1 kHz | 3.1864 |
| M4 Movement | Drifting Strata | macro reset | d ≥ 4.0 | 0.8745 |
| Movement rho | `VoragoMacro_SweepAxes` | Spearman rho | rho ≥ 0.9, endpoint ≥ +20 % | rho 0.8333 |
| M10 Life | Teeming (+ `900=0.15` companion if r3 is adopted) | macro reset | d ≥ 4.0 | 0.9943 |

---

## Group 0 — Starting tree (sequential)

### T001 — Confirm the starting tree, build it, create the evidence files

**DONE (main loop, 2026-10-07):** HEAD `5bda20c1` = `502e5243` + the docs-only commit (`artifacts/base_tree.txt`); no production file dirty; build exit 0, 0 warnings (`base_build_status.txt`); binaries copied to `f:/tmp/p13d/base-bin/`; `rulings.md`, `odr_sweep.txt`, `fr034_surfaced.md`, `lever_table.md` created.

- **Files:** create `specs/vorago-phase13d-engine-ceilings/artifacts/` with:
  - `rulings.md` (header line only);
  - `odr_sweep.txt` (plan §10's names, "0 hits, 502e5243, planning session");
  - `fr034_surfaced.md` (plan §6.4 entries 1–5 verbatim, each marked "applies only if adopted");
  - `lever_table.md` (FR-050 columns: feature, cells served, lever with every mechanism, before, every candidate →
    reading → log, ruled value, after, override string, re-open count; one row each for E1, E4, E6/E7, M2, M5,
    M4/rho, M10; plus a CPU section).
- **Steps:**
  1. `git rev-parse --short HEAD` prints `502e5243`; `git status --short` shows no change under `dsp/`, `plugins/` or
     `tools/` → `artifacts/base_tree.txt`.
  2. Build `dsp_systems_tests vorago_tests`, zero warnings → `artifacts/base_build_status.txt` (exit code, warning
     count).
  3. Copy `build/windows-x64-release/bin/Release/{vorago_tests,dsp_systems_tests}.exe` to `f:/tmp/p13d/base-bin/`
     (the FR-033 A/B base binary; outside the repo).
- **Stop-and-surface:** HEAD is not `502e5243`, a production file is dirty, or the build warns.

---

## Group 1 — Step B: before-record on the unmodified binary (sequential; one run at a time)

Every run uses the T001 binary. **No source edit happens before T008 is done.**

### T002 — Probe every roster cell as compiled, four takes (FR-003 (i), SC-001)

**DONE (main loop, 2026-10-07):** nine probes run concurrently (deterministic renders; the hours-off MUST): every roster cell within tolerance, all deltas 0.0000 except E7.hi 0.0001 (`artifacts/before_tolerance.txt`, `before_*.log`). The first Colony Pulse run used `VORAGO_PILOT_CELLS=S7`, an unknown label (labels are the full cell text) — `before_E67_badlabel.log`; re-run with `VORAGO_PILOT_SECONDARY=1` only. E4 at 1× (`800=0.5` alone): d 4.4059 / attrib 3.6649, arms green on all four takes — the "loops fall silent" premise does not reproduce with the rate change alone (see T028 note; `rulings.md`).

- **Run** alone, one at a time, `VORAGO_PILOT_TAKES=4`, no override:
  - Bloom Colony → `before_E1.log`; Erosion → `before_M2.log`; Drifting Strata → `before_M4.log`; Stone Gravity →
    `before_M5.log`; Teeming → `before_M10.log`; Feeding Loops → `before_E4_stored.log`;
  - Feeding Loops with `VORAGO_PILOT_OVERRIDE="800=0.5"` → `before_E4_1x.log`;
  - Colony Pulse with `VORAGO_PILOT_SECONDARY=1 VORAGO_PILOT_CELLS=S7` → `before_E67.log`.
- **Check** each against the Roster table with `|d_base − d_sweep5| ≤ max(0.01, 0.005·d_sweep5)` (also `attribBase`
  for E1 / E4). For E4 at 1× compare to the premise 3.71 and record arm 2 per take (premise: red on every take).
  Write the comparison (sweep 5, base, |Δ|, tolerance, ok, cited log line) → `artifacts/before_tolerance.txt`.
- **Stop-and-surface:** any value outside its tolerance. The tolerance is never widened.

### T003 — Repeat spread (FR-003 (i))

**DONE (main loop, 2026-10-07):** Bloom Colony repeat 1.1823 vs 1.1823, spread 0.0000 ≤ 0.01 (`before_E1_repeat.log:10`, `before_tolerance.txt`).

- **Run** Bloom Colony again, same env → `before_E1_repeat.log`. Append `|d_1 − d_2|` and
  `max(0.01, 0.005·1.1823) = 0.01` to `before_tolerance.txt`.
- **Stop-and-surface:** spread > 0.01.

### T004 — `[long]` macro baselines with every assertion listed (FR-003 (ii), FR-031, SC-001, SC-010)

**DONE (main loop, 2026-10-07):** `base_sweepaxes.log` 37 / 38, the one red the Movement row rho 0.8333 (`:86-91`); `base_nozipper.log` 24 / 24; `artifacts/base_long_failures.txt`. No stop.

- **Run** alone: `dsp_systems_tests.exe "VoragoMacro_SweepAxes" -s` → `base_sweepaxes.log`;
  `dsp_systems_tests.exe "VoragoMacro_NoZipper" -s` → `base_nozipper.log`.
- Record the Movement rho (expected 0.8333) and every **failing** assertion → `artifacts/base_long_failures.txt`.
  FR-031 binds the assertions that pass here, plus the Movement row.
- **Stop-and-surface:** Movement rho ≠ 0.8333 (4 dp), or any red besides the Movement row.

### T005 — 13b tables and the default-surface record (FR-003 (iii), FR-005, FR-032, SC-011, SC-019)

**DONE (main loop, 2026-10-07):** GATE1M PASS on both surfaces (default ratio 0.998, Life max 0.629: `gate1_base_default.log`, `gate1_base_lifemax.log`); Gate 2 tables with t0 2.2107 / 4.5842 (`gate2_table_default_base.log`, `gate2_table_lifemax_base.log`; counting knobs in `gate2_base_counting.txt` — syncRate counts on the default surface, reads INAUDIBLE .hi at Life max on this base, unlike the 13c 10-04 table); `default_before_13b.log` (M1 RMS −26.97 dBFS, t0 1.1895); `default_before_p0.log` (ruled K = NONE / G2 STOP, identical to Phase 14; K = 4 inherited). No stop.

- **Run** alone, in order:
  1. `VORAGO_PROBE_SEEDS=6 VORAGO_PROBE_KNOBS=- vorago_tests.exe "Vorago_EcosystemRuleProbe"` →
     `gate1_base_default.log`; the same plus `VORAGO_PROBE_SURFACE=109=1` → `gate1_base_lifemax.log`. Cite each
     `GATE1M … ratio=…` line.
  2. The 14-knob table (no `VORAGO_PROBE_KNOBS`) at the default surface → `gate2_table_default_base.log`; with
     `VORAGO_PROBE_SURFACE=109=1` → `gate2_table_lifemax_base.log`. Record the counting knobs per surface (d/t0 ≥
     0.5, no KILL, not OFF-LIKE) → `artifacts/gate2_base_counting.txt`.
  3. `VORAGO_PROBE_KNOBS=-`, default surface, single seed → `default_before_13b.log` (t0, `t0on`, M1 RMS);
     `vorago_tests.exe "Vorago_PresetPilot_Calibrate"` → `default_before_p0.log` (P0: stored-seed descriptor, M1
     RMS).
- **Stop-and-surface:** GATE1M < 0.5 on the base tree at either surface.

### T006 — CPU base (FR-033, SC-015)

**DONE (main loop, 2026-10-07):** both lanes alone, P-core pinned (mask 0xFFFF), 15 min idle before the first; base binary as compiled. `dsp_systems_tests` 12:27:58–17:44:54 (5 h 17 min; the first attempt was cut at the 2 h background-task ceiling 90 min in and re-launched detached): `VoragoEngine_CpuBudget` clause (i) 3.10765e6 ≤ 3.2e6 ns (97.114 % of the reference), clause (ii) 2.98315e6 ≤ 4.04172e6 ns (110.713 % of the baseline) — `cpu_base_dsp_systems_tests_full.log:3168-3176`. The lane itself is 89 / 97: seven non-Vorago perf budgets red plus the carried Movement rho (`cpu_base_dsp_reds.txt`, with the 13c precedent); the seven are re-run alone after a fresh idle (`cpu_base_rerun_alone.log`, result appended below). `vorago_tests` 17:49:54–18:50:29 all 6 cases green (`cpu_base_vorago.log`): `Vorago_ProcessorCpu` P/D 0.960344 (`cpu_base_vorago_tests_full.log:22`), PF/D 0.991988 (`:32`); `Vorago_PresetCpu` worst Entropic Hum 1.1056 ≤ 1.15, default surface 3024934 ns/block (`:635`). No red clause (i); nothing for the 13c B-2 A/B. Re-run alone of the seven (`cpu_base_rerun_alone.log`, 19:05–19:14): five green (Character tape, HarmonicCloud, ContinuousBody, Seraphis full-poly, NoiseOrganism (a) 88784 ns), `SpectralMorph_CpuBudget` red on a different clause than in the lane (+0.6 %, a flipping verdict = machine noise), `FeedbackEcology_CpuBudget` red at the same reading as the 13c base (nsRef 119532.2 × 1.5 > 160000) — the one stable non-Vorago red, surfaced as in 13c B-17.

- Idle machine: `node tools/run-cpu-tests.js dsp_systems_tests` → `cpu_base_dsp.log`; then
  `node tools/run-cpu-tests.js vorago_tests` → `cpu_base_vorago.log`. Cite `VoragoEngine_CpuBudget` clauses (i), (ii)
  in ns, `Vorago_ProcessorCpu` P/D and `Vorago_PresetCpu` worst ratio.
- A red clause (i) is recorded and judged later by the alternating pinned A/B (13c B-2); no action now.

### T007 — Freeze the sweep-5 records (FR-042, SC-021)

**DONE (main loop, 2026-10-07):** 43 records copied to `artifacts/sweep5-records/`, `sweep5-records.sha256` written and checked: 43 OK.

- Copy `f:/tmp/p14/sweep5-out/record_*.txt` (43 files) to `artifacts/sweep5-records/`; run
  `sha256sum artifacts/sweep5-records/record_*.txt > artifacts/sweep5-records.sha256`; then
  `sha256sum -c artifacts/sweep5-records.sha256` must report OK for all 43.
- **Stop-and-surface:** a file is missing (count ≠ 43).

### T008 — FR-034 literal grep, base (FR-034, SC-018)

**DONE (main loop, 2026-10-07):** `artifacts/fr034_grep_base.txt` + judgment: every constant hit reads by name through an accessor; no literal expectation; nothing joins `fr034_surfaced.md` beyond entry 1.

- For every constant a planned rung touches (`kPartialLaneGain`, `kLoopWakeBase`, `kLoopGainLeverSpan`,
  `kCouplingLeverSpan`, `kLeverInputGain`, `kPeakLevelLeverSpanDb`, `kNoiseLevelLeverSpanDb`, `kGhostLaneGain`,
  `kWanderLeverRateCompExponent`, `kNumRows`) and every row amount in plan §1.3's table, run
  `grep -rn "<name or literal>" dsp/tests plugins/vorago/tests` → `artifacts/fr034_grep_base.txt`.
- A hit that asserts the literal as an expectation (not read by name through a friend or `VoragoVoice::k…`) is
  added to `fr034_surfaced.md` now, with its reason.

---

## Group 2 — Step I: instruments and seams (sequential; every task edits a shared file)

With no tweak set and the meter off, nothing in this group changes a rendered sample. T022 proves it.

### T009 — Voice route seams (FR-012 (a), plan §4.1)

**DONE (build stage T009–T021 by the implement agents, verify runs and T022 in the main loop, 2026-10-07):** `VoragoVoice_CeilingLeverNeutral` green and the Lever tests unedited green (`instr_newcases.log`: All tests passed (31518 assertions in 16 test cases), exit 0); build 0 warnings (`/f/tmp/p13d/build_g2.log`).

- **Files:** `dsp/tests/unit/systems/vorago_ecosystem_lever_test.cpp` (test first), then `vorago_voice.h`.
- **Failing test first** — append `TEST_CASE("VoragoVoice_CeilingLeverNeutral", "[systems][vorago]")` (T-M3) after
  `VoragoVoice_RouteLeverZeroAtZeroLane` (`:1360`), with that case's setup (`makeLeverVoice(48000.0, leverSeed())`,
  `REQUIRE(voice->isPrepared())`, `noteOn(65.406f, 100.0f / 127.0f)`). Assertions:
  1. After prepare: `getPartialBloomLaneGain() == 1.0f`, `getPartialMutationLaneGain() == 1.0f`,
     `getLoopGainLeverSpan() == 0.18f`, `getCouplingLeverSpan() == 0.30f`,
     `getLoopWakeBase() == VoragoVoice::kLoopWakeBase`, `getBloomParentCount() == 4u`,
     `getBloomChildrenPerEvent() == 2u`.
  2. Lane 0 (`Probe::injectEco(*voice, uniformLanes(0.0f)); Probe::advanceLifeOnly(*voice);`):
     `cloud().getMutation() == Probe::mutationBase(*voice)`; `bloom().getDepth() == Probe::bloomDepthBase(*voice)`;
     for every `l < voice->ecology().getNumLoops()`: `Probe::loopGainOffset(*voice, l) == 0.0f` and
     `Probe::ringCouplingApplied(*voice, l) == Probe::ringCouplingBase()`.
  3. After `setPartialBloomLaneGain(2.5f)`, `setPartialMutationLaneGain(0.5f)`, `setLoopGainLeverSpan(0.26f)`,
     `setCouplingLeverSpan(0.40f)`, `setLoopWakeBase(0.15f)`, for `raw ∈ {0.1f, 0.5f, 1.0f}` (inject, advance,
     `Probe::schedLanes(*voice, S)`), with `x = std::clamp(Probe::leverInputGain(kKindFeedback) * raw, 0.0f, 1.0f)`:
     - `cloud().getMutation() == std::clamp(Probe::mutationBase(*voice) + 0.5f * raw, 0.0f, 1.0f)`;
     - `bloom().getDepth() == std::clamp(Probe::bloomDepthBase(*voice) + 2.5f * raw, 0.0f, 1.0f)`;
     - `Probe::loopGainOffset(*voice, l) == 0.26f * x`;
     - `Probe::ringCouplingApplied(*voice, l) == std::clamp(Probe::ringCouplingBase() + 0.40f * x, 0.0f, 0.5f)`;
     - `ecology().getLoopWakeAmount(l) == VoragoVoice::combineWake(0.15f, raw, S[kKindFeedback][l])`.
  4. `setLoopWakeBase(0.0f)` → `getLoopWakeBase() == 0.05f` (E-8); `setLoopWakeBase(2.0f)` → `1.0f`;
     `setPartialBloomLaneGain(9.0f)` → `8.0f`; NaN (`std::bit_cast<float>(0x7FC00000u)`) and +Inf
     (`std::bit_cast<float>(0x7F800000u)`) into each float seam leave the previous value.

  It does not compile before the change (the seams do not exist).
- **Implement** (plan §4.1):
  - members `partialBloomLaneGain_`, `partialMutationLaneGain_`, `loopGainLeverSpan_`, `couplingLeverSpan_`,
    initialised from the compiled constants and re-installed in `prepare()`;
  - `static constexpr std::size_t kBloomParentCount = BloomEngine::kDefaultParentCount;` and
    `kBloomChildrenPerEvent = BloomEngine::kDefaultChildrenPerEvent;`, installed with `bloom_.setParentCount` /
    `bloom_.setChildrenPerEvent` beside `:658`;
  - setters reject non-finite (`detail::isFinite`) and clamp: gains `[0, 8]`; `setLoopWakeBase`
    `[kMinRetunedWakeBase, 1]`, filling all `FeedbackEcology::kMaxLoops` entries of `loopWakeBase_`; loop span
    `[0, FeedbackEcology::kMaxLoopGain]`; coupling span `[0, FeedbackEcology::kMaxCouplingPerPair]`; the count seams
    forward to `bloom_`;
  - getters: the four members; `getLoopWakeBase()` reads slot 0; count getters forward to
    `bloom_.getParentCount()` / `getChildrenPerEvent()`;
  - `applyIdentityLanes` reads the four members at `:2232`, `:2233`, `:2275`, `:2279` instead of the constants;
  - `reset()` keeps them (configuration). No new include.
- **Verify:** build `dsp_systems_tests vorago_tests`, zero warnings; `dsp_systems_tests.exe
  "VoragoVoice_CeilingLeverNeutral"` green; `"VoragoVoice_*Lever*"` and `"VoragoVoice_EcosystemLaneShapingFidelity"`
  green with no test edit.

### T010 — Engine fan-out seams (FR-012 (a), plan §4.2)

**DONE (build stage T009–T021 by the implement agents, verify runs and T022 in the main loop, 2026-10-07):** `VoragoEngine_CeilingSeamContract` green, `VoragoEngine_SetterContract` green unedited (`instr_newcases.log`: All tests passed (31518 assertions in 16 test cases), exit 0).

- **Files:** `dsp/tests/unit/systems/vorago_engine_test.cpp` (test first), then `vorago_engine.h`.
- **Failing test first** — append `TEST_CASE("VoragoEngine_CeilingSeamContract", "[systems][vorago]")` (T-M1) after
  `VoragoEngine_SetterContract` (`:3071`), with its fixture. Assertions:
  1. After prepare, for every `i < VoragoEngine::kMaxVoices`: `engine->getVoice(i)` reads gain 1.0 / 1.0,
     `getLoopWakeBase() == VoragoVoice::kLoopWakeBase`, spans 0.18 / 0.30, counts 4 / 2; the seven engine getters
     read the same values.
  2. `setPartialBloomLaneGain(3.0f)`, `setPartialMutationLaneGain(0.5f)`, `setLoopWakeBase(0.30f)`,
     `setLoopGainLeverSpan(0.22f)`, `setCouplingLeverSpan(0.34f)`, `setBloomParentCount(2.0f)`,
     `setBloomChildrenPerEvent(3.0f)` read back on every `getVoice(i)`.
  3. `makeNaNFloat()`, `makeNonFiniteFloat(kPosInfBits)` and `makeNonFiniteFloat(0xFF800000u)` into each of the seven
     leave every voice's value unchanged.
  4. Clamps: `setPartialBloomLaneGain(9.0f)` → 8; `setLoopWakeBase(0.0f)` → `VoragoVoice::kMinRetunedWakeBase`;
     both count seams `-1.0f` → 1 and `1e12f` → 8 (parents) / 4 (children per event); `2.6f` → 3.

  It does not compile before the change.
- **Implement:** seven fan-outs over all `kMaxVoices` after `setGhostDensity` (`:1056-1066`), getters on slot 0 (as
  `float`). The two count seams take `float`: reject non-finite (`detail::isFinite`), clamp **in float** to
  `[1, BloomEngine::kMaxParents]` / `[1, BloomEngine::kMaxChildrenPerEvent]`, then
  `static_cast<std::size_t>(std::lround(v))` — clamp before round (plan §4.2).
- **Verify:** both targets, zero warnings; `"VoragoEngine_CeilingSeamContract"` and `"VoragoEngine_SetterContract"`
  green.

### T011 — Loop-bus meter, voice and engine (FR-004, FR-021b, plan §2.1)

**DONE (build stage T009–T021 by the implement agents, verify runs and T022 in the main loop, 2026-10-07):** `VoragoEngine_LoopBusMeterIsObservationOnly` green: meter on / off bit-identical on MSVC (`instr_newcases.log`: All tests passed (31518 assertions in 16 test cases), exit 0); pre-ruling 3 not triggered.

- **Files:** `vorago_engine_test.cpp` (test first), `vorago_voice.h`, `vorago_engine.h`.
- **Failing test first** — append `TEST_CASE("VoragoEngine_LoopBusMeterIsObservationOnly", "[systems][vorago]")`
  (T-M2): engines A and B, each `makeEngine(48000.0, cfg)` with the same `setSeed`, default voice params (ecology mix
  0.15 > 0, `vorago_voice.h:643`), one `noteOn(36, 100)` each, 20 s in 512-sample blocks; B calls
  `setLoopBusMeterEnabled(true)` after prepare. Assertions:
  1. every output sample of A equals B's (`REQUIRE(a == b)`; a same-process A/B identity, not a stored golden);
  2. `A.takeLoopBusSumSq() == 0.0` after every block; B's per-block value is finite by bit pattern
     (`std::bit_cast<std::uint64_t>` exponent check) and the 20 s total is `> 0.0`;
  3. every B block value `≤ 512.0 * 36.0 * 36.0` (each tap ≤ 1; at most `kMaxLoops × kMaxVoices` = 36 taps summed);
  4. `B.getAllocatedBytes()` after enabling equals its value after prepare;
  5. `B.isLoopBusMeterEnabled()` is true; after `B.prepare(48000.0, cfg)` it is false.

  It does not compile before the change.
- **Implement** (plan §2.1):
  - voice: `setLoopBusMeterEnabled` / `isLoopBusMeterEnabled`; fixed `loopTap_` (`kMaxLoops × kControlChunkSamples`)
    and `carryLoopBus_`; in `renderOneChunk()` step 7 (`:2610`), when on, call `processBlockTapped` with tap pointers
    (`nullptr` at or above `getNumLoops()`) and fill `carryLoopBus_[s] = Σ_l loopTap_[l][s]`; when off, the
    unchanged `processBlock`; a four-argument `processStereoBlock(outL, outR, loopBusOut, n)` serving
    `carryLoopBus_` with the same `carryRead_` / `take` arithmetic, the three-argument form forwarding `nullptr`;
    `prepare()` sets the flag false; `reset()` and the clearing path zero `carryLoopBus_`;
  - engine: fan-out setter, slot-0 getter, `vLoopBus_`, `loopBusAcc_`, `double loopBusSumSq_`; in the voice loop
    (`:1205-1226`), when on, call the four-argument overload and add `loopBusAcc_[s] += vLoopBus_[s]` inside the
    existing per-sample loop after the FR-072 check (so a poisoned slice is mirrored, not zeroed); after the voice
    loop add `Σ (double)loopBusAcc_[s]²`; `takeLoopBusSumSq()` returns and zeroes it.
- **Verify:** both targets, zero warnings; T-M2 green; the per-push systems run green.

### T012 — Per-kind eco-lane read-out (FR-015, plan §2.3 voice half)

**DONE (build stage T009–T021 by the implement agents, verify runs and T022 in the main loop, 2026-10-07):** `VoragoVoice_EcoLaneMeanReadout` green (`instr_newcases.log`: All tests passed (31518 assertions in 16 test cases), exit 0).

- **Files:** `vorago_ecosystem_lever_test.cpp` (test first), `vorago_voice.h`.
- **Failing test first** — append `TEST_CASE("VoragoVoice_EcoLaneMeanReadout", "[systems][vorago]")` (T-M4), setup as
  T009: for `x ∈ {0.0f, 0.5f, 1.0f}`, `Probe::injectEco(*voice, uniformLanes(x)); Probe::advanceLifeOnly(*voice);`
  then for every kind `k < 5` with at least one addressed slot,
  `REQUIRE(voice->getEcoLaneMean(static_cast<EcosystemEngine::Kind>(k)) == x)`; and
  `getEcoLaneMean(static_cast<EcosystemEngine::Kind>(5)) == 0.0f`. (0, 0.5 and 1 are exact under sum / count.)
- **Implement:** `std::array<float, EcosystemEngine::kNumKinds> ecoLaneMean_{}`, written in `publishIdentity()`
  after the gather (`:2369-2380`) as the mean of `lanes.eco[k][slot]` over `slotCountForKind(k)` slots (0 if none);
  `getEcoLaneMean(Kind)` returns 0 for an out-of-range kind.
- **Verify:** both targets, zero warnings; T-M4 green.

### T013 — Harness: eleven lever getters, secondaries under a lever (plan §4.3, §4.4)

**DONE (build stage T009–T021 by the implement agents, verify runs and T022 in the main loop, 2026-10-07):** failing check first on the base binary: `t013_before.log:` "VORAGO_PILOT_LEVER cannot combine with VORAGO_PILOT_SECONDARY=1" (`preset_pilot_test.cpp(1018)` FAILED). Per-push `vorago_tests` on the G2 binary: All tests passed (4059849 assertions in 119 test cases), exit 0 (`instr_perpush_vorago.log`).

- **File:** `plugins/vorago/tests/preset_test_support.h`.
- **Failing check first:** `VORAGO_PILOT_PRESET="Colony Pulse" VORAGO_PILOT_LEVER="childGain=1.5"
  VORAGO_PILOT_SECONDARY=1` → `t013_before.log` FAILs with "cannot combine" (`preset_pilot_test.cpp:1017`).
- **Implement:**
  - `detail::readTweakLevers` returns `std::array<float, 11>`: the existing four plus `getPartialBloomLaneGain`,
    `getPartialMutationLaneGain`, `getLoopWakeBase`, `getLoopGainLeverSpan`, `getCouplingLeverSpan`,
    `getBloomParentCount`, `getBloomChildrenPerEvent`; `leversAfterTweak` becomes `std::array<float, 11>`;
  - `VerificationVector` gains `bool tweakHeld = true;`;
  - `computeVerificationVector` gains the trailing parameter
    `const std::function<void(Krate::DSP::VoragoEngine&)>& engineTweak = {}`, copied into every `RenderSpec` it
    builds; each capture's `tweakHeld` is ANDed into `vec.tweakHeld`. With the default argument nothing changes.
- **Verify:** `vorago_tests` builds, zero warnings; the per-push `vorago_tests` run green.

### T014 — Pilot: seven new lever keys; secondaries under a lever (FR-012, plan §4.3, §4.4)

**DONE (build stage T009–T021 by the implement agents, verify runs and T022 in the main loop, 2026-10-07):** failing check first `t014_before.log`: "unknown VORAGO_PILOT_LEVER key partialBloomGain" (`:911`). `t014_identity.log`: Bloom Colony under all seven keys at their compiled values reads d 1.1823 / attrib 1.1035 = `before_E1.log` (delta 0.0000). `t014_secondary.log`: Colony Pulse `childGain=1.5` + `VORAGO_PILOT_SECONDARY=1` prints D13.2 0.1942, E6.hi 1.3700, E7.hi 1.3429, nothing FAILs.

- **File:** `plugins/vorago/tests/integration/preset_pilot_test.cpp`.
- **Failing check first:** `VORAGO_PILOT_PRESET="Bloom Colony" VORAGO_PILOT_LEVER="partialBloomGain=1"` →
  `t014_before.log` FAILs "unknown VORAGO_PILOT_LEVER key partialBloomGain" (`:917-918`).
- **Implement:** `readPilotLever()` accepts `partialBloomGain`, `partialMutationGain`, `parentCount`,
  `childrenPerEvent`, `loopWakeBase`, `loopGainSpan`, `couplingSpan` (each an `std::optional<float>` captured into
  the tweak, calling the matching engine seam); the unknown-key `FAIL` stays; update the doc comment (`:875-880`).
  In `runPrimaryProbe` delete the `FAIL` at `:1015-1019`, pass `lever.tweak` as the new last argument of
  `computeVerificationVector` (`:1234`), and `REQUIRE(vec.tweakHeld)`.
- **Verify:** build, zero warnings. Run alone:
  1. Bloom Colony with `VORAGO_PILOT_LEVER="partialBloomGain=1,partialMutationGain=1,parentCount=4,childrenPerEvent=2,loopWakeBase=0.45,loopGainSpan=0.18,couplingSpan=0.30"`
     → `t014_identity.log`: `primary d` equals `before_E1.log`'s within `max(0.01, 0.005·d)`;
  2. Colony Pulse with `VORAGO_PILOT_LEVER="childGain=1.5" VORAGO_PILOT_SECONDARY=1` → `t014_secondary.log`: the
     `secondary E6.hi` / `E7.hi` / `D13.2` lines print and nothing FAILs.

### T015 — Harness + pilot: loop-bus readout (FR-004, FR-021b, plan §2.1)

**DONE (build stage T009–T021 by the implement agents, verify runs and T022 in the main loop, 2026-10-07):** failing check first `t015_before.log`: no loopbus line (16 assertions, pass). `t015_check.log`: four `loopbus` lines (worst 10 s −62.56 / −62.06 / −58.85 / −54.07 dBFS, takes 0-1 below the −60 dBFS silence line) and d 2.9182 = `before_E4_stored.log`; `looplife` lines show no dormant loop and no gate below the 0.45 wake floor.

- **Files:** `preset_test_support.h`, then `preset_pilot_test.cpp`.
- **Failing check first:** Feeding Loops with `VORAGO_PILOT_LOOPBUS=1` prints no `loopbus` line → `t015_before.log`.
- **Implement:**
  - `RenderSpec` gains `bool loopBusMeter = false;`; `SweepCapture` gains `std::vector<double> loopBusPower;`
    (reserved like `blockPowerL`);
  - `renderPreset`, when set: `host.engineForTweak()->setLoopBusMeterEnabled(true)` after `loadState`; after every
    `host.process`, push `engine->takeLoopBusSumSq()`; set `tweakHeld = false` if `isLoopBusMeterEnabled()` reads
    false after block 0;
  - `[[nodiscard]] inline double loopBusWindowDb(const SweepCapture& cap, const SweepWindow& w)`:
    `10·log10(max(Σ loopBusPower[first, end) / (n_blocks · kRenderBlock), kPowerFloor))` (mono `spanPowerDb`);
  - pilot, `VORAGO_PILOT_LOOPBUS=1`: set `loopBusMeter` on every take render (stored take; all four under
    `VORAGO_PILOT_TAKES=4`) — on the no-lever path use `computeLeverTakes` with an empty tweak, so the meter can be
    set on its `RenderSpec`; twins and route arms do not carry it. Per take print
    `  loopbus take j seed s: worst10s %.2f dBFS [PASS|NO] windows n` (minimum `loopBusWindowDb` over
    `tenSecondWindows(tl.A, tl.H, sr)` against `kSilenceDb`), plus one per-loop line from voice 0 at the take's end
    (`getLoopWakeAmount`, `getLoopGate`, `isLoopDormant`, `getLoopGain`); the verdict line gains `loopsAlive y|n`
    (AND over printed takes; `-` with the meter off).
- **Verify:** build, zero warnings; Feeding Loops `VORAGO_PILOT_LOOPBUS=1 VORAGO_PILOT_TAKES=4` → `t015_check.log`
  prints four `loopbus` lines and a `primary d` equal to `before_E4_stored.log`'s within tolerance.

### T016 — Harness + pilot: block observer and bloom readout (plan §2.2)

**DONE (build stage T009–T021 by the implement agents, verify runs and T022 in the main loop, 2026-10-07):** failing check first `t016_before.log`: no bloom line. `t016_check.log`: two bloom lines — R_k spawn 1 discarded 0 spawned 0 refused 2 liveMean 5.984 depthMean 0.999; R_k0 spawn 0 refused 0 liveMean 4.984 depthMean 0.500 — and d 1.1823 unchanged.

- **Files:** `preset_test_support.h`, `preset_pilot_test.cpp`.
- **Failing check first:** Bloom Colony with `VORAGO_PILOT_BLOOM=1` prints no `bloom` line.
- **Implement:** `RenderSpec` gains `std::function<void(const Krate::DSP::VoragoEngine&, long long startSample)>
  blockObserver{};`, called after each `host.process` (empty: no change). Pilot `VORAGO_PILOT_BLOOM=1`: on R_k and
  R_k0 of a route primary, a lambda writing into a per-job struct (never Catch2; jobs run on worker threads) samples
  `getVoice(0).bloom()` over `[A, H]`; print per arm
  `  bloom <arm>: spawn %llu discarded %llu spawned %llu refused %llu liveMean %.3f depthMean %.3f` from the counters
  at `bloom_engine.h:729-745`, `getLiveChildCount()` and `getSmoothedDepth()`.
- **Verify:** build, zero warnings; `t016_check.log` on Bloom Colony prints two bloom lines and the unchanged verdict.

### T017 — Pilot: eco-lane ranking (FR-015, plan §2.3)

**DONE (build stage T009–T021 by the implement agents, verify runs and T022 in the main loop, 2026-10-07):** failing check first `t017_before.log`: no lanes line. `t017_check.log`: two rank lines — E6.hi Partial 0.1862 > Ghost 0.1504 > Feedback 0.1021 > Noise 0.0647 > Resonator 0.0566; E7.hi Feedback 0.0692 > Ghost 0.0657 > Noise 0.0530 > Partial 0.0484 > Resonator 0.0029. `VORAGO_PILOT_CELLS` is not needed: Colony Pulse claims both cells as secondaries.

- **File:** `preset_pilot_test.cpp`.
- **Failing check first:** Colony Pulse with `VORAGO_PILOT_LANES=1` prints no `lanes` line.
- **Implement:** `VORAGO_PILOT_LANES=1` renders, on the stored take, P and the ExtReversion twin of each named
  secondary (E6.hi: 901 → 0.0; E7.hi: 902 → 0.25, `tools/vorago_preset_defs.h:187-190`) with a `blockObserver`
  sampling `getVoice(0).getEcoLaneMean(k)` for all five kinds each block over `[A, H]` (vectors reserved before the
  render). Print `  lanes <label> <kind>: mean p10 p90 dabs` per render and kind, then
  `  lanes rank <cell>: <kind>=<score> …` ordered by `|Δmean| + |Δ(p90 − p10)|` between P and twin.
- **Verify:** build, zero warnings; `t017_check.log` on Colony Pulse prints two rank lines.

### T018 — Pilot: Life-row read-back (FR-019, plan §2.4)

**DONE (build stage T009–T021 by the implement agents, verify runs and T022 in the main loop, 2026-10-07):** failing check first `t018_before.log`: no readback line. `t018_check.log`: nine readback lines (Teeming: EcosystemDepth CLAMPED at Life 0.70 and 1.00, rowsum 1.105 / 1.150; EventRateScale 6.5 / 0.2 / 9.2 ok; BloomSpawnRateHz 0.0107 / 0.0005 / 0.0213 ok).

- **File:** `preset_pilot_test.cpp`.
- **Failing check first:** Teeming with `VORAGO_PILOT_READBACK=1` prints no `readback` line.
- **Implement:** read `plugins/vorago/src/parameters/param_routes.h` for the normalized→plain helper used for ids
  800, 900 and 1301 and quote it in a comment. With the option, render block 0 at Life ∈ {stored, 0.0, 1.0}
  (override `109`) and print `  readback life %.2f: <target> dest %.6f rowsum %.6f [CLAMPED|ok]` for
  `EcosystemDepth` (`getEcosystemDepth()`), `EventRateScale` (`getEventRateScale()`) and `BloomSpawnRateHz`
  (`getBloomSpawnRateHz()`) on voice 0; `rowsum` = stored plain base + Σ over `VoragoMacroMatrix::kRows` rows on
  that target of `amount * applyModCurve(curve, macro)`; `CLAMPED` iff `dest` equals the setter clamp and `rowsum`
  exceeds it.
- **Verify:** build, zero warnings; `t018_check.log` on Teeming prints nine readback lines.

### T019 — Pilot: read-set iterators `roster`, `liveFor:<id>`, `verified36` (FR-010c, FR-030, plan §4.5)

**DONE (build stage T009–T021 by the implement agents, verify runs and T022 in the main loop, 2026-10-07):** failing check first `t019_before.log`: `VORAGO_PILOT_ITERATE=roster` FAILs at `:1351` (verified27 REQUIRE). `t019_check.log`: `liveFor:e4` lists 42 live presets including Feeding Loops and Feedback Mire — every preset is live because Ecology Mix (500) defaults to 0.15 and no def zeroes it; `t019_check36.log`: verified36 = 36 cells, count REQUIRE passes. `VORAGO_PILOT_LIST` appended to `odr_sweep.txt`.

- **File:** `preset_pilot_test.cpp`.
- **Failing check first:** `VORAGO_PILOT_ITERATE=roster` → `t019_before.log` fails `REQUIRE(*iter == "verified27")`
  (`:1351`).
- **Implement:** widen the branch at `:1350-1354` to accept `verified27`, `verified36`, `roster` and `liveFor:<id>`;
  `verified27` / `verified36` keep the three prohibitions (no override, no trim, no lever).
  - `verified36`: `requiredPrimaryCells()` minus the six roster primaries, `REQUIRE(count == 36)`, as compiled.
  - `liveFor:<id>` (`e1route`, `e1attach`, `e4`, `e67`, `m2`, `m4`, `m5`, `m10`) with plan §4.5's predicates over
    stored values: `e1route` 900 > 0 and (1300 > 0 or the cloud sounds); `e1attach` 1300 > 0; `e4` 500 > 0; `e67`
    900 > 0; `m*` the macro away from neutral (Gravity neutral 0.5); undecidable → live. It runs every
    sweep-5-verified primary and secondary of each live preset as compiled under the current `VORAGO_PILOT_LEVER`;
    it REQUIREs no override and no trim. An unknown id FAILs.
  - `roster`: a TU-level table `kRosterGateOverrides` (cell → override string; initially E4 `800=0.5`, others empty)
    and a set `kRosterRuled` (initially empty); runs each ruled cell on its override with the current lever; E4 with
    four takes and the loop-bus meter; E6.hi / E7.hi via the secondary path with `S7`. It REQUIREs no
    `VORAGO_PILOT_OVERRIDE` in the environment.
  - `VORAGO_PILOT_LIST=1` prints the derived list without rendering.
  - Each row prints `<mode> <cell> <host> <role> overrides <s|-> d %.4f bar %.4f arms [....] -> verified|no`.
- **Verify:** build, zero warnings; `VORAGO_PILOT_ITERATE=liveFor:e4 VORAGO_PILOT_LIST=1` → `t019_check.log` lists
  Feeding Loops and Feedback Mire; `VORAGO_PILOT_ITERATE=verified36 VORAGO_PILOT_LIST=1` passes its count REQUIRE.

### T020 — Rule probe: `VORAGO_PROBE_LANES` (FR-015, plan §2.3)

**DONE (build stage T009–T021 by the implement agents, verify runs and T022 in the main loop, 2026-10-07):** failing check first `t020_before.log`: no LANES line (191318 assertions, pass). `t020_check.log`: five LANES lines (default + syncRate.hi + selfAffinity.lo/.hi at Life max) and its syncRate / selfAffinity rows (1.8915 0.413 INAUDIBLE .hi; 0.0163 0.004 INAUDIBLE .lo,.hi) equal `gate2_table_lifemax_base.log` exactly. The sampler lives in the table mode (the gate-1 mode prints no LANES line).

- **File:** `plugins/vorago/tests/integration/ecosystem_rule_probe_test.cpp`.
- **Failing check first:** `VORAGO_PROBE_KNOBS=syncRate VORAGO_PROBE_LANES=1` prints no `LANES` line.
- **Implement:** `struct LaneStats { double mean, p10, p90, dabs; };` in the anonymous namespace (`:176`);
  `RenderResult` (`:327`) gains `std::array<LaneStats, EcosystemEngine::kNumKinds> lanes{}`; `renderOnce` (`:360`),
  when `readEnv("VORAGO_PROBE_LANES") == "1"`, samples `colonyEngine.getVoice(0).getEcoLaneMean(k)` per block over
  the M1–M3 capture into vectors reserved before the loop. Print `LANES <label> <kind>=mean/p10/p90/dabs …` after the
  default render's `DESCRIPTOR` line and after each knob's `.lo` / `.hi` row. Option off: no sampling, no print.
- **Verify:** build, zero warnings; `VORAGO_PROBE_KNOBS=syncRate,selfAffinity VORAGO_PROBE_SURFACE=109=1
  VORAGO_PROBE_LANES=1` → `t020_check.log` prints LANES lines, and its GATE2 rows for the two knobs equal
  `gate2_table_lifemax_base.log`'s.

### T021 — Row-emulation probe `VoragoMacro_Phase13dRowProbe` (plan §2.5)

**DONE (build stage T009–T021 by the implement agents, verify runs and T022 in the main loop, 2026-10-07):** failing check first `t021_before.log`: "No test cases matched". `t021_check.log`: `VORAGO_ROWPROBE=` + `VORAGO_ROWPROBE_ROWS=Movement` reproduces `base_sweepaxes.log` — rho 0.8333, endpoint 0.2213, seeds 101 / 202 / 303 rho 0.9 / 0.6 / 1.0.

- **File:** `dsp/tests/unit/systems/vorago_macro_retune_probe_test.cpp` (append; registered at
  `dsp/tests/CMakeLists.txt:552`).
- **Failing check first:** `dsp_systems_tests.exe "VoragoMacro_Phase13dRowProbe"` matches no test case.
- **Implement:** `TEST_CASE("VoragoMacro_Phase13dRowProbe", "[.probe][vorago]")` reading
  `VORAGO_ROWPROBE="<macro>:<target>:<amount>:<curve>[;…]"` (enum names as in `vorago_macro_matrix.h`; an unknown
  name FAILs). Each entry is emulated as a delta through `setTargetBase(t, base + amount · applyModCurve(curve, x))`,
  as the Phase 12 case does at `:326`; a negative amount equal to a shipped row's cancels that member. Run the
  SweepAxes fixture (seeds 101 / 202 / 303, five points, 60 s) for every SweepAxes row touched and print rho,
  endpoint and the per-seed five-point series against the unchanged thresholds. No assertion.
- **Verify:** build, zero warnings; `VORAGO_ROWPROBE=` (empty) with the Movement row requested → `t021_check.log`
  reproduces `base_sweepaxes.log`'s Movement rho 0.8333.

### T022 — Step I: inertness, per-push green, premise instruments (FR-003, FR-004, FR-010)

**DONE (build stage T009–T021 by the implement agents, verify runs and T022 in the main loop, 2026-10-07):** step 1 `instr_perpush.log`: dsp_systems All tests passed (6069572 assertions in 1430 test cases), exit 0; vorago All tests passed (4059849 assertions in 119 test cases), exit 0 (both suites run concurrently, G2 binary). Step 2 inertness: Bloom Colony 1.1823 = 1.1823, Erosion 3.2191 = 3.2191 (`instr_inert_E1.log`, `instr_inert_M2.log`; tolerance max(0.01, 0.005·d), deltas 0.0000). Step 3 `ie1_bloom.log` (= t016_check): R_k spawns 1 and refuses 2, R_k0 none. Step 4 `ie4_loopbus_1x.log` (800=0.5): d 4.4059 / attrib 3.6649, loopbus −63.01 / −62.06 / −58.85 / −54.16 dBFS, loopsAlive n; `ie4_loopbus_stored.log` (= t015_check): d 2.9182, loopbus −62.56 / −62.06 / −58.85 / −54.07; in both, every loop at end reads dormant n and gate ≥ 0.45 — no stop. Step 5 `ie67_lanes_L{045,070,090,100}.log`: E6.hi 1.3700 / 1.4016 / 1.4016 / 1.3863, E7.hi 1.3429 / 1.3045 / 1.3045 / 1.2573; the P and twin lane stats and both rank lines are identical at every Life value (override confirmed applied, `[probe] override 109`): Life's only lane path, EcosystemDepth, is CLAMPED at 1.0 from the stored 0.45 upward (`ie67_readback.log` rowsum 1.0675 CLAMPED at 0.45), so raising Life moves events, not lanes. Ranks not flat — no stop. `ie67_lanes_lifemax.log` (gate-1 mode) GATE1M 0.629 PASS = base; `ie67_lanes_lifemax_table.log` / `ie67_lanes_default_table.log`: LANES default at Life max Partial 0.765 Resonator 0.288 Noise 0.541 Feedback 0.496 Ghost 0.753 vs default surface 0.650 / 0.245 / 0.460 / 0.422 / 0.640 (every lane up by ~1.17×). `VORAGO_PILOT_CELLS=S7` was not passed (bare labels are unknown; Colony Pulse claims both cells). Step 6 `im4_member_cancel.log`: cancel CloudDrift rho 0.7333 / endpoint 0.1804; ResonanceWander 0.7333 / 0.2107; NoiseWander 0.9333 / 0.1819; Breathing 0.9333 / 0.1369; CavernDamper 0.8333 / 0.2213 (identical to the shipped row — inaudible on the metric); `im4_member_cancel_bothwander.log` (plan §3.6 (1)) 0.7000 / 0.1298. No single cancel restores rho ≥ 0.9 with endpoint ≥ 0.2. `im4_member_override_<id>.log`: the primary M4 cell skips at Movement 0 ("M displacement"), so the read ran through `Vorago_PresetPilot_SwingProbe` (`im4_member_swing_{ref,204,402,302,1500,1104}.log`): d(P, Movement 0) 0.8745; with one member restored to its Movement-1 value: drift 204 → 1.6901, breathing 1500 → 1.1571, noise wander 302 → 1.0569, resonance wander 402 → 0.9076, damper 1104 → 0.8868 — the descriptor hears drift, breathing and noise wander. Step 7 `im10_readback.log` (= t018_check): EcosystemDepth CLAMPED only; EventRateScale and BloomSpawnRateHz ok — no stop.

- **Run** alone, in order, on the T021 binary:
  1. both per-push runs green → `instr_perpush.log`;
  2. inertness: Bloom Colony and Erosion as compiled, `VORAGO_PILOT_TAKES=4` → `instr_inert_E1.log`,
     `instr_inert_M2.log`; each `d` equals its `before_*.log` within `max(0.01, 0.005·d)`;
  3. I-E1: Bloom Colony `VORAGO_PILOT_TAKES=4 VORAGO_PILOT_BLOOM=1` → `ie1_bloom.log`;
  4. I-E4: Feeding Loops `VORAGO_PILOT_TAKES=4 VORAGO_PILOT_LOOPBUS=1` with `800=0.5` → `ie4_loopbus_1x.log`, and
     stored → `ie4_loopbus_stored.log` (FR-003 (i)'s loop-level before-record, plan §8);
  5. I-E67: Colony Pulse `VORAGO_PILOT_LANES=1 VORAGO_PILOT_SECONDARY=1 VORAGO_PILOT_CELLS=S7` with `109=0.45`,
     `0.70`, `0.90`, `1.00` → `ie67_lanes_L{045,070,090,100}.log`; rule probe `VORAGO_PROBE_SURFACE=109=1
     VORAGO_PROBE_LANES=1` → `ie67_lanes_lifemax.log`; Colony Pulse `VORAGO_PILOT_READBACK=1` → `ie67_readback.log`;
  6. I-M4: `VORAGO_ROWPROBE` with each of the five Movement members cancelled in turn, and once with
     `ResonanceWanderRate` cancelled → `im4_member_cancel.log`; Drifting Strata with Movement at 0 and one member's
     stored parameter at its Movement-1 value (ids 204, 402, 302, 1500, 1104) → `im4_member_override_<id>.log`;
  7. I-M10: Teeming `VORAGO_PILOT_READBACK=1` → `im10_readback.log`.
- **Stop-and-surface:** inertness out of tolerance (then no loop-level before-record exists); `ie4_*` shows
  `isLoopDormant` true or a zero gate on the identity path (plan §3.2's model of the loops is wrong); flat `ie67`
  rank lines (plan §3.3 reserve); `im10_readback.log` shows both `EventRateScale` and `BloomSpawnRateHz` CLAMPED.

---

## Group 3 — L1: E1 Partial → bloom (FR-013, FR-021; sequential)

Read-set ids: `e1route` (arms A and B), `e1attach` (arm C). Slow Bloom S6 is in every E1 read set.

### T023 — E1 premise reads (FR-010, FR-025b)

**DONE (main loop, 2026-10-07):** `e1_premise_mut0.log` d 1.1476 / attrib 1.0454 (the mutation half carries 0.058 of attribBase); `e1_companion_00.log` d 1.1035 = attribBase (the route is inert at 1300 = 0); `e1_companion_02.log` d 1.4852 / attrib 1.1035 — no FR-025b stop. `rulings.md`: E1 is SPAWN-LIMITED (R_k spawn 1, spawned 0, refused 2 in every arm; Bloom Colony stores Life 0 where BloomSpawnRateHz reads 0.010 Hz, `e1_readback.log`), so arm C ran before b4.

- **Run** alone on Bloom Colony, `VORAGO_PILOT_TAKES=4 VORAGO_PILOT_BLOOM=1`:
  1. `VORAGO_PILOT_LEVER="partialMutationGain=0"` → `e1_premise_mut0.log` (the mutation half's share of attribBase;
     diagnostic, never adopted);
  2. companion alone, no lever: `VORAGO_PILOT_OVERRIDE="1300=0.0"` → `e1_companion_00.log`; `"1300=0.2"` →
     `e1_companion_02.log`.
- From `ie1_bloom.log` record in `rulings.md` whether E1 is **spawn-limited** (few spawn events in both arms) or
  **level-limited** (live children in both arms at similar depth); this decides whether arm C runs before b4.
- **Stop-and-surface (FR-025b):** a companion alone gives `d ≥ 4.0` and `d ≥ attribBase + 1.5`.

### T024 — E1 arm A (single gain + companion), rungs a1–a6

**DONE (main loop, 2026-10-07):** a1–a6 (`e1_a<n>_gate.log`): d 1.7056 at 1300 = 0.2 and 1.7712 = attribBase at 1300 = 0.0 for gains 2.0, 3.0 and 4.0 alike — the bloom depth lane reads depthMean 1.000 from gain 2.0 up (saturated clamp), so higher gains change nothing; no rung clears (d < 4.0, d < attrib + 1.5); arms green on all. No `_readset` / `_minus6` runs (no clearing rung). Recorded in `lever_table.md`.

- **Run** each alone (seam rungs), `VORAGO_PILOT_TAKES=4 VORAGO_PILOT_BLOOM=1`:
  a1 `partialBloomGain=2.0,partialMutationGain=2.0` + `1300=0.2`; a2 the same + `1300=0.0`; a3 `3.0` / `3.0` +
  `0.2`; a4 `3.0` / `3.0` + `0.0`; a5 `4.0` / `4.0` + `0.2`; a6 `4.0` / `4.0` + `0.0` → `e1_a<n>_gate.log`. For each
  rung that clears the gate also `_readset.log` (`liveFor:e1route`, same lever, no override) and `_minus6.log`.
  (No `_roster.log`: E1 is first.)
- Record every rung in `lever_table.md`. A rung that sinks a verified cell (S6 included) is recorded
  "sunk: <preset> <cell> before/after" (FR-030) and stays adoptable.

### T025 — E1 arm B (split), rungs b1–b4 (always measured)

**DONE (main loop, 2026-10-07):** b1–b3 (`e1_b<n>_gate.log`) d 1.7056 / attrib 1.7712 at every gain; b4 on b1 + 1300 = 0.2 with partialMutationGain 0.5 / 0.25 / 0: 1.7056 / 1.7053 / 1.6579 (attrib 1.7712 / 1.7707 / 1.7272) — no rung clears.

- **Run** alone: b1 `partialBloomGain=2.0` + `1300=0.2`; b2 `3.0`; b3 `4.0`; b4 = the best of b1–b3 with the better of
  `1300=0.2` / `0.0`, plus `partialMutationGain=0.5`, then `0.25`, then `0` → `e1_b<n>_gate.log` (read set and −6 for
  clearing rungs).

### T026 — E1 attachment arm C (on the best gain rung)

**DONE (main loop, 2026-10-07):** Base a1 (the smallest gain; a1 = b1 in d). c1 parentCount 3 / 2 / 1: d 1.2845 / 6.8712 / 4.8289, attrib 1.4150 / 6.8706 / 4.9939 (parentCount moves the whole render — attribBase moves with d — and only parentCount = 1 lets a child spawn: spawned 1 refused 1); c2 childrenPerEvent 3 / 4: d 2.0373 / 0.6813, attrib 2.1082 / 0.7727 (refused 3 / 4). No rung has d ≥ attrib + 1.5. Order logged: arm C before b4 (spawn-limited).

- Base: arm A's best clearing rung, else arm B's best. If T023 recorded spawn-limited, run this task before b4 and
  log the order.
- **Run** alone: c1 + `parentCount=3`, `=2`, `=1`; c2 + `childrenPerEvent=3`, `=4` → `e1_c<n>_gate.log`; read set
  `liveFor:e1attach` and `_minus6.log` for clearing rungs.

### T027 — E1 adoption or ruling, then ship (FR-010d, FR-013, FR-034)

**DONE (main loop, 2026-10-07):** nothing clears → **STOP-AND-SURFACE (FR-027)** to the user with every rung's d, attribBase and arms (`lever_table.md` E1 row). Nothing shipped; `kRosterGateOverrides` / `kRosterRuled` untouched. USER RULING (AskUserQuestion, 2026-10-07): record E1 as engine-limited at this ladder and move on; no re-open spent (Q8 credit unused); E1 stays red in the roster as Phase 14 found; the ladder and every reading stay in lever_table.md.

- **Decide:**
  - an arm-A rung clears every FR-010d clause → adopt the smallest (arm B logged as the measured alternative);
  - arm C improves on it → **USER RULING** on the attachment (present the c1 / c2 table);
  - no arm-A rung clears, an arm-B rung does → **USER RULING** on the split (it edits
    `VoragoVoice_RouteLeverZeroAtZeroLane`, plan §11 item 2);
  - nothing clears → **stop-and-surface** (FR-027) with every rung's d, attribBase and arms.
- **Ship (rebuild):**
  - single gain: first apply FR-034 entry 1 (`vorago_ecosystem_lever_test.cpp:1334-1337`, `+ raw` →
    `+ Probe::partialLaneGain() * raw`, marked applied in `fr034_surfaced.md`), then set `kPartialLaneGain`
    (`vorago_voice.h:1685`) to the ruled g;
  - split (ruled only): `kPartialMutationLaneGain` / `kPartialBloomLaneGain`, the seams installed from them, and the
    ruled edit of `RouteLeverZeroAtZeroLane` and the `Probe` accessor (`:137`);
  - attachment (ruled only): `kBloomParentCount` / `kBloomChildrenPerEvent` values;
  - set `kRosterGateOverrides` E1 → `"1300=<ruled>"` and add E1 to `kRosterRuled` in the same edit as the
    `rulings.md` line.
- **Verify:** both targets, zero warnings; per-push runs green; Bloom Colony with
  `VORAGO_PILOT_OVERRIDE="1300=<ruled>"`, no lever → `e1_shipped_gate.log` reproduces the adopted rung's `d` within
  `max(0.01, 0.005·d)`.

---

## Group 4 — L2: E4 Feedback → loop wake at 1× events (FR-014, FR-021, FR-021b; sequential)

Every rung runs on Feeding Loops with `VORAGO_PILOT_OVERRIDE="800=0.5" VORAGO_PILOT_TAKES=4 VORAGO_PILOT_LOOPBUS=1`.
Gate: the route bar, arms 2 and 3 `y` on all four take lines, every `loopbus take j` `PASS`, `loopsAlive y`. Read
sets: `_roster.log` and `_readset.log` (`liveFor:e4`).

### T028 — E4 premise (FR-003, FR-014)

**DONE (main loop, 2026-10-07):** `ie4_loopbus_1x.log` d 4.4059 / attrib 3.6649 (= `before_E4_1x.log`, delta 0.0000; the 3.71 premise does not apply, see the note above); arm 2 lo per take −40.6 / −36.6 / −38.0 / −35.6 dB green; loop-bus worst 10 s −63.01 / −62.06 / −58.85 / −54.16 dBFS (takes 0–1 below −60 → loopsAlive n); per-loop end lines: no dormant loop, every gate ≥ 0.45. No stop.

**Main-loop note (2026-10-07, T002 before-record):** with `800=0.5` alone the premise does NOT reproduce — Feeding Loops reads
d 4.4059, attribBase 3.6649 (attributable iff d >= 5.1649), arms all green and arm 2 lo -40.6 / -36.6 / -38.0 / -35.6 dB on the
four takes (`artifacts/before_E4_1x.log:9-15`). The 3.71 / arm-2-red reading came from Phase 14 batch 12 FL_I, whose override
also zeroed breathing and bloom and set ecology mix 1.0. So at 1x the loops stay alive and E4 clears the d bar; the gap is
attributability (the route-independent change is 3.66 of the 4.41). The ladder therefore targets attributability — the route
term must add >= 1.5 over the reversion — with the loops-alive conjunct kept as a guard, not as the problem.

- From `ie4_loopbus_1x.log`: confirm `primary d` within `max(0.01, 0.005·3.71)` of 3.71; record arm 2 per take, each
  take's loop-bus worst 10 s and the per-loop line in `rulings.md`.
- **Stop-and-surface:** `isLoopDormant` true or a zero gate on any loop (the sleep edge firing from a path plan §3.2
  says cannot), or `d` out of tolerance.

### T029 — E4 W1 rungs r1–r3 (loop-wake base)

- **Run** alone: r1 `loopWakeBase=0.30`, r2 `0.15`, r3 `0.05` → `e4_r<n>_gate.log`; for clearing rungs
  `_roster.log`, `_readset.log`, `_minus6.log`.

### T030 — E4 companions r4–r5 (sustain)

- Base: the best of r1–r3 by gate `d` with `loopsAlive`. **Run** alone: r4 + `loopGainSpan=0.22`, `0.26`, `0.30`;
  r5 = r-best (with r4's best if it helped) + `couplingSpan=0.34`, `0.40`, `0.44` → `e4_r4{a,b,c}_*.log`,
  `e4_r5{a,b,c}_*.log`. Itemise each companion in `lever_table.md`.

### T031 — E4 adoption or ruling, then ship (FR-010d, FR-014)

- **Decide:** adopt the smallest-change clearing rung of r1–r5. If none clears → **USER RULING** on W2 (plan §11
  item 3) with the r1–r5 table. If authorised, run r6: rebuild rungs `kLoopWakeLaneGain ∈ {1.5, 2.0, 2.5}` in
  `combineWake(loopWakeBase_[l], std::min(1.0f, kLoopWakeLaneGain * eco), sched)` at the compiled base, four logs
  each, with the ruled `checkWakes` edit entered in `fr034_surfaced.md`. W2 declined or failing → **stop-and-surface**.
- **Ship (rebuild):** W1 → `kLoopWakeBase` (`vorago_voice.h:431`) to the ruled value (`kPeakWakeBase` unchanged; the
  `static_assert` at `:1754-1755` must compile); companions → `kLoopGainLeverSpan` (`:1681`) /
  `kCouplingLeverSpan` (`:1695`). Add E4 to `kRosterRuled`.
- **Verify:** both targets, zero warnings; `"VoragoVoice_EcosystemLeverMapping"`,
  `"VoragoVoice_EcosystemLaneShapingFidelity"`, `"VoragoVoice_EcosystemLeverNeutral"`, `"VoragoVoice_WakeCombineRule"`
  green unedited (unless W2 was ruled); Feeding Loops at `800=0.5`, no lever → `e4_shipped_gate.log` reproduces the
  adopted rung within tolerance with `loopsAlive y`.
- **Re-open check:** the adopted rung's `_roster.log` E1 row below its bar → re-open E1 once (Q8).

---

## Group 5 — L3: E6.hi / E7.hi (FR-015, FR-022; sequential)

Gate: Colony Pulse with `109=<L>`, `VORAGO_PILOT_SECONDARY=1 VORAGO_PILOT_CELLS=S7`: `secondary E6.hi … ->
VERIFIED`, `secondary E7.hi … -> VERIFIED`, S7 primary ≥ 4.0, `secondary D13.2 … -> VERIFIED`, levels green. Read
set `liveFor:e67`.

### T032 — Choose L and name the carrying kinds (FR-015, FR-010d)

- From `ie67_lanes_L*.log`: if an L ∈ {0.70, 0.90, 1.00} already verifies E6.hi, E7.hi, S7 and D13.2 with arms green,
  take the smallest such L; run its `_roster.log`, `_readset.log` and `_minus6.log` with no lever and adopt it if all
  clear (no engine lever; the named FR-025 exception, transcribed in the confirming pass).
- Otherwise take the best L by summed E6.hi + E7.hi `d`, and the top one or two kinds of the rank lines as the
  carrying kinds → `rulings.md`.

### T033 — E6 / E7 rebuild rungs on the carrying kinds

- Per carrying kind, three rebuild rungs in increasing distance (plan §3.3):
  - Resonator / Noise / Feedback: `kLeverInputGain[k]` 2 → 2.25 / 2.5 / 2.75 (Partial / Ghost `static_assert` stays);
  - Resonator: `kPeakLevelLeverSpanDb` 18 → 19 / 20 / 21;
  - Noise: `kNoiseLevelLeverSpanDb` 12 → 13 / 14 / 15;
  - Ghost: `kGhostLaneGain` 1 → 1.25 / 1.5 / 2.0 (S9 / E5 in the read set);
  - Partial: re-open E1 instead.
- Before the first rung, FR-034 grep the constant and enter any hit.
- Per rung: rebuild both targets; `e67_r<n>_gate.log`; rule probe `VORAGO_PROBE_SURFACE=109=1 VORAGO_PROBE_LANES=1
  VORAGO_PROBE_KNOBS=syncRate,selfAffinity` → `e67_r<n>_lifemax.log` (copy the two rows and their LANES lines into
  `rulings.md`); `_roster.log`, `_readset.log`, `_minus6.log`. Revert after each rung unless adopted.

### T034 — E6 / E7 adoption, ship (FR-010d)

- Adopt the smallest-change clearing rung; set `kRosterGateOverrides` E6.hi / E7.hi → `"109=<L>"` and add them to
  `kRosterRuled`. Nothing clears, or flat rank lines → **stop-and-surface** with the I-E67 table.
- **Verify:** both targets, zero warnings; per-push green; `e67_shipped_gate.log` reproduces the adopted rung.
  Re-open check on the E1 / E4 rows.

---

## Group 6 — L4: M2 Age (FR-016, FR-020; Erosion; sequential)

Every rung is a rebuild edit of `kRows` in `dsp/include/krate/dsp/systems/vorago_macro_matrix.h` (Age rows
`:342-359`), pre-screened with T021 on the Age SweepAxes row (HF > 4 kHz down ≥ 3 dB) and the folded `Decay`
clauses; a rung failing the pre-screen is not built. Read set `liveFor:m2`. 13c's rungs (tilt −7, damping 0.70, its
Age → mutation amount from `l7_M2_ladder_summary.txt`) are never repeated.

### T035 — M2 clamp headroom and pre-screen

- Log to `m2_headroom.txt` the unclamped sum of every row on each candidate target at the five SweepAxes Age points
  and at Erosion's stored macros, printing `CLAMPED` past the setter clamp (tilt ±12 dB/oct,
  `harmonic_cloud.h:194-195`; damping [0, 1], `continuous_body.h:146-147`; read the cavern decay clamp before r5).
- `VoragoMacro_Phase13dRowProbe` per r1–r5 magnitude → `m2_prescreen.log`.

### T036 — M2 rungs r1–r6

- r1 Age → BodyDamping amount 0.55 → 0.75 / 0.85 / 0.95; r2 Age → CloudSpectralTiltDb −4 → −8 / −9 / −10 (−8 first);
  r3 new row Age → CavernDarkness base 0.80, +0.05 / +0.10 / +0.20, Linear; r4 new row Age → CloudMutation base 0.15,
  +0.30 / +0.50 / +0.70 (one equal to 13c's amount becomes the midpoint to its neighbour); r5 Age →
  CavernDecaySeconds −14 → −16 / −18 / −20 (drop rungs past the clamp); r6 the two best singles combined.
- An added row joins the Age block and bumps `kNumRows` and its tally comment (`:272-276`) in the same edit; the
  `static_assert` at `:1223-1224` must compile.
- Per rung: rebuild; `m2_r<n>_gate.log` (Erosion), `_roster.log`, `_readset.log`, `_minus6.log`;
  `VoragoMacro_SweepAxes -s` → `m2_r<n>_sweepaxes.log` and `VoragoMacro_NoZipper -s` → `_nozipper.log` (every
  assertion passing in `base_sweepaxes.log` still passes). Revert unless adopted.

### T037 — M2 adoption, ship

- Adopt by FR-010d plus the SweepAxes / NoZipper clause; nothing clears → **stop-and-surface**.
- **Verify:** final edit built, zero warnings; `"VoragoMacro_NeutralIsIdentity"` green; per-push green. Re-open check
  on the E1, E4, E6.hi and E7.hi rows.

---

## Group 7 — L5: M5 Gravity (FR-017, FR-020; Stone Gravity; sequential)

Every rung adds one Gravity row (bipolar through `contributionOf`) and bumps `kNumRows` and the tally comment. Every
gate run uses `VORAGO_PILOT_TAKES=4`; a rung that turns arm 1 or arm 3 red on any take or at 44.1 kHz is not adopted.
Read set `liveFor:m5`.

### T038 — M5 headroom and pre-screen

- `m5_headroom.txt`: unclamped sums at Gravity 0 and 1 and at Stone Gravity's stored macros for CloudSpectralTiltDb
  (Darkness's tilt row shares it), SubToneLevelOffsetDb and CavernDarkness, with `CLAMPED` flags.
- `VoragoMacro_Phase13dRowProbe` per magnitude → `m5_prescreen.log` (the Gravity SweepAxes row must still pass).

### T039 — M5 rungs r1–r3

- r1 → CloudSpectralTiltDb base −4, amount −3 / −6 / −8; r3 → CavernDarkness base 0.80, +0.10 / +0.15 / +0.20; r2
  (last) → SubToneLevelOffsetDb base 0, +2 / +4 / +6. Per rung: rebuild; `m5_r<n>_gate.log`, `_roster.log`,
  `_readset.log`, `_minus6.log`, `_sweepaxes.log`, `_nozipper.log`.

### T040 — M5 adoption, ship

- Adopt by FR-010d; else **stop-and-surface**. Verify as T037; re-open check on every earlier ruled cell.

---

## Group 8 — L6: M4 Movement and the Movement rho (FR-018, FR-023; Drifting Strata; sequential)

An admissible rung reads Movement rho ≥ 0.9, endpoint ≥ +20 %, every `base_sweepaxes.log`-passing assertion still
passing, and NoZipper ≤ 1.5×. Read set `liveFor:m4`.

### T041 — M4 premise

- From `im4_member_cancel.log` name the inverting member (the cancel that restores rho ≥ 0.9); from
  `im4_member_override_*.log` name the member the descriptor hears (largest `d`). Record both in `rulings.md`.
- **Stop-and-surface:** no single cancel restores rho ≥ 0.9 and no member override moves `d` above 1.5.

### T042 — M4 rungs r1–r4

- r1: the inverting member's curve → each of the two other admissible curves (Linear / Exponential / SCurve; never
  Stepped);
- r2: `kWanderLeverRateCompExponent` (`vorago_voice.h:1725`) 0.75 → 0.80 / 0.85 / 0.90 (exempt from the pre-screen);
- r3: the inverting member's amount × 2/3, plus a new row Movement → BreathingIrregularity base 0.30, +0.20 / +0.35 /
  +0.50 (bump `kNumRows`; pre-compute the sum with Entropy's +0.60 row against the clamp);
- r4: r-best + the heard member widened by 1/3, 2/3, 3/3 of its headroom to its component clamp.
- Row rungs pass T021's pre-screen first. Per rung: rebuild; `m4_r<n>_sweepaxes.log`, `_nozipper.log`, `_gate.log`,
  `_roster.log`, `_readset.log`, `_minus6.log`.

### T043 — M4 / rho adoption, ship

- Adopt the smallest admissible rung clearing M4 ≥ 4.0 and FR-010d. A rung that fixes rho but not M4 is adopted for
  FR-023 only if it clears FR-010d's other clauses, and M4 then goes to **stop-and-surface** with every rung's (d,
  rho, endpoint, zipper). Verify as T037 plus `VoragoMacro_SweepAxes` green; re-open check.

---

## Group 9 — L7: M10 Life, last (FR-019, FR-020; Teeming; sequential)

Read set `liveFor:m10`, plus E1, E4, E6.hi, E7.hi, S7, D13.1 and D13.2 through `roster`.

### T044 — M10 premise and companion alone

- From `im10_readback.log` record each Life row's `dest` / `rowsum` / flag at Life 0, 0.7 and 1; a `CLAMPED` row is
  excluded except as the companion.
- **Run** Teeming with `VORAGO_PILOT_OVERRIDE="900=0.15"`, no lever → `m10_companion_alone.log`.
- **Stop-and-surface:** the companion alone gives `d ≥ 4.0` (FR-025b).

### T045 — M10 rungs r1–r4

- r1 Life → BloomSpawnRateHz amount 0.0208 → 0.029 / 0.037 / 0.0458; r2 Life → EventRateScale 9 → 10 / 11.5 / 13
  (each Life-1 sum checked against the clamp 10); r3 Life → EcosystemDepth 0.15 → 0.45 / 0.65 / 0.85 with `900=0.15`
  on every rung; r4 the two best combined. Per rung: rebuild; `m10_r<n>_gate.log`, `_roster.log`, `_readset.log`,
  `_minus6.log`, `_sweepaxes.log`, `_nozipper.log`.

### T046 — M10 adoption, ship, re-opens

- Adopt by FR-010d; if r3 (or an r4 containing it) is adopted, set `kRosterGateOverrides` M10 → `"900=0.15"`. Every
  earlier cell that drops in the adopted rung's `_roster.log` is re-laddered once (its ladder task re-run, logs
  suffixed `_reopen`); a second drop is **stop-and-surface** with both ladders. Nothing clears →
  **stop-and-surface**. Verify as T037.

---

## Group 10 — F1: final-tree gates (FR-026, FR-024c, FR-030, FR-030b; sequential, alone)

### T047 — Final binary and gate re-measure

- Build `dsp_systems_tests vorago_tests` once, zero warnings; every `final_*` log comes from this binary.
- **Run** alone each roster gate on its recorded surface with `VORAGO_PILOT_TAKES=4` (plus `VORAGO_PILOT_LOOPBUS=1`
  for E4 and `VORAGO_PILOT_SECONDARY=1 VORAGO_PILOT_CELLS=S7` for E6 / E7) → `final_E1.log`, `final_E4.log`,
  `final_E67.log`, `final_M2.log`, `final_M5.log`, `final_M4.log`, `final_M10.log`; then each with
  `VORAGO_PILOT_MASTER_TRIM=-6` → `final_minus6_<cell>.log`.
- **Pass:** SC-002 – SC-005 and SC-007, citing the verdict, `route arms:`, `levels:`, `loopbus` and `secondary` lines.
  A red cell is stop-and-surface.

### T048 — Verified primaries and secondaries kept (SC-008, SC-009)

- `VORAGO_PILOT_ITERATE=verified36` → `final_verified36.log` (count 36; each d ≥ bar, arms green; a sunk preset is
  marked and re-read after Group 12).
- `VORAGO_PILOT_SECONDARY=1` on each host of an FR-030b secondary → `final_secondaries.log`; build the before/after
  table against `artifacts/sweep5-records/`.

---

## Group 11 — F2: final-tree regression gates (sequential, alone)

### T049 — Phase 10 bounds, 13b gates, default render (FR-031, FR-032, FR-005, SC-010, SC-011, SC-019)

- `VoragoMacro_SweepAxes -s` → `final_sweepaxes.log` (the Movement CHECKs at `vorago_macro_test.cpp:1318-1319` pass,
  every `base_sweepaxes.log`-passing assertion passes, the case is green); `VoragoMacro_NoZipper -s` →
  `final_nozipper.log`.
- T005's runs on the final binary → `final_gate1_{default,lifemax}.log`,
  `final_gate2_table_{default,lifemax}.log` (with `VORAGO_PROBE_LANES=1`), `default_after_13b.log`,
  `default_after_p0.log` (finite, peak ≤ 0.9661).
- **Pass:** GATE1M ≥ 0.5 on both surfaces; every knob in `gate2_base_counting.txt` still counts.

### T050 — Fingerprint re-harvest (FR-035, SC-017)

- Run the `VoragoEngine_GhostExtensionWiring` printer twice → `final_ghost_harvest_{1,2}.log`; md5 of the two literal
  blocks identical; paste the literals with a new PROVENANCE block into
  `dsp/tests/unit/systems/vorago_ghost_ext_test.cpp` (entry 4 of `fr034_surfaced.md`); rebuild; the verify run green.
  Do the same for any other fingerprint a per-push red names.

### T051 — CPU lane alone (FR-033, SC-015)

- Idle machine: `node tools/run-cpu-tests.js dsp_systems_tests` → `cpu_final_dsp.log`, then `vorago_tests` →
  `cpu_final_vorago.log`. Clause (i) is judged by the alternating pinned A/B against `f:/tmp/p13d/base-bin/` →
  `cpu_ab_final.log`. Write the ns and % delta per clause into `lever_table.md`'s CPU section. No budget changes.

---

## Group 12 — Confirming pass: Phase 14 T048 re-run (FR-040 – FR-044; sequential)

### T052 — Affected set and full re-authors (FR-040, FR-041)

- Derive the affected set: every preset where an adopted lever's `liveFor` id is live, plus every preset whose
  claimed cell in T047 / T048 differs from its sweep-5 record by > 0.01 → `affected_set.txt` (cite a log line for
  each exclusion).
- **Full re-author** by `VORAGO_PILOT_OVERRIDE` batches for Bloom Colony, Feeding Loops (with `800=0.5`), Colony Pulse
  (with `109=<L>`), Erosion, Stone Gravity, Drifting Strata, Teeming (with `900=0.15` if adopted), Hull Resonance,
  Smeared Horizon, Haunted Colony and every sunk preset → `reauthor_<preset>.log`. Target: primary and secondaries
  verified with arms green; for the three subset presets, no SUBSET pair left.

### T053 — Re-measure the other affected presets (FR-040)

- Probe each other affected preset's claimed cells as compiled → `remeasure_<preset>.log`; re-author only on a drop
  > 0.01 or a lost verification, citing the line.

### T054 — Transcribe, regenerate, tree check (FR-040)

- **Failing check first:** after editing `tools/vorago_preset_defs.h` and before regenerating,
  `vorago_tests.exe "Vorago_FactoryPresets_TreeMatchesGenerator"` is red.
- Transcribe each adopted override into `tools/vorago_preset_defs.h` with a comment (`// 13d <cell>: <reason>,
  <log>`); build `generate_vorago_presets`; `Vorago_FactoryPresets_TreeMatchesGenerator` green. Empty each
  transcribed entry of `kRosterGateOverrides`; rebuild `vorago_tests`.

### T055 — Shards and aggregate (FR-042, SC-021, SC-022)

- `sha256sum -c artifacts/sweep5-records.sha256` passes first. Re-run every shard that contains an affected preset:
  `VORAGO_SWEEP_SHARD=i/N VORAGO_SWEEP_OUT=f:/tmp/p13d/sweep6-out vorago_tests.exe
  "Vorago_PresetSweep_AblationVerifiesClaims"` → `sweep6_shard_<i>.log`; copy the unaffected records from
  `artifacts/sweep5-records/` into the same directory; aggregate with `VORAGO_SWEEP_IN` → `sweep6_aggregate.log`.
- **Pass:** `verified primaries: 42` (or each remaining cell ruled); `SUBSET pairs: 0`; every shard reading within
  0.01 of its probe reading (else stop-and-surface).

### T056 — T063 re-read and preset CPU (FR-043, FR-044, SC-023)

- Re-read the nine red and five ruled T063 rows (spec FR-043) and the out-of-roster secondaries (D13.1, D13.2, D14.2,
  D11, D6.2, D6.3, D7.2) against `sweep6_aggregate.log`, citing lines → `t063_reread.md`; anything not verified is a
  user ruling in `rulings.md`.
- `node tools/run-cpu-tests.js vorago_tests` → `cpu_presets_final.log`; worst preset ≤ 1.15.

---

## Group 13 — Integration (sequential, last)

### T057 — CMake registration (single task)

- Plan §7: no CMake list changes. Confirm `git diff --stat 502e5243 -- '*CMakeLists.txt'` is empty and every edited TU
  sits at the registration lines in the facts table (`dsp/tests/CMakeLists.txt:511`, `:513`, `:515`, `:538`, `:552`,
  `:560`; `plugins/vorago/tests/CMakeLists.txt:52`, `:57`, `:60`) → `cmake_check.txt`. If any task added a file
  anyway, register it here in its layer's list (and the `-fno-fast-math` list for a plugin test TU), then rebuild.

### T058 — Full-suite run (FR-034, FR-036, FR-037, FR-039, SC-016, SC-018, SC-020, SC-024, SC-025)

- Build every target (`dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests
  vorago_tests seraphis_tests shared_tests Vorago`), zero warnings → `final_build.log`.
- `node tools/run-close-lanes.js` → `summary.txt`, 100 % including the `[long]` lanes; cite the lane lines for
  `VoragoEngine_SlotSeedReproducibility`, `Vorago_PresetSweep_RendersAreReproducible`,
  `VoragoEngine_CapabilityLeverBounded`, `param_table_test`, `state_v2_test`, `state_v3_test` and the named 13b / 13c
  tests (FR-032), with `git diff 502e5243 --` on their files showing no edit outside `fr034_surfaced.md`.
- `git diff --name-only 502e5243` → `git_diff_names.txt`: no `plugins/seraphis/**`, `harmonic_cloud.h`,
  `atmosphere_engine.h`, `continuous_body.h`, `aether_reverb.h`, `entropy_processor.h`, life-modulator or
  `seraphis_*.h` path.
- `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` and `-Target vorago` → 0 findings;
  `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Vorago.vst3"` passes.

### T059 — Portability check (FR-036, SC-024)

- `node tools/check-portability.js` → `portability.log` clean; then `wsl --shutdown`.
- A finding (brace-init narrowing, `std::isnan`, missing include) is fixed in its file, rebuilt, and that target's
  per-push lane re-run. Never waived.

### T060 — Compliance (FR-050 – FR-053, SC-026, SC-027)

- Write `specs/vorago-phase13d-engine-ceilings/compliance.md`: one row per FR / SC citing its `final_*` log line; the
  FR-050 lever table from `lever_table.md` (feature, cells, every mechanism, before, every rung → reading → log,
  ruled value, after, override string, re-open count); the FR-051 default-render note (T005 vs T049 figures and the
  named cause); the FR-052 CPU delta; `rulings.md` cited for every adoption and user ruling. No row is marked met
  without its log line; a stop-and-surface cell is recorded as its ruling, never UNMET.
