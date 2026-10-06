# Compliance: Vorago Phase 14 — Factory Presets & Release Readiness

**Status:** IN PROGRESS (gate G1 pending)

## FR-070 / SC-025 — audibility probe

### Run 1 — default surface (2026-09-27, 10:29:36 → 10:47:21, 17 min 45 s wall for 28 renders ≈ 38 s per 340 s render)

Command: `build/windows-x64-release/bin/Release/vorago_tests.exe "Vorago_EcosystemRuleProbe" > f:/tmp/p14/probe.log 2>&1`, run alone. Log: `artifacts/fr070_probe_default_surface.log` (exit=0, "All tests passed (892779 assertions in 1 test case)"). Result: **0 of 14 candidates audible** — every knob's best `d` (max 0.6311, grazeRate → 0) is below `t0` = 1.7128, the seed-twin distance, so at the default surface no rule knob moves the sound as much as reseeding does.

```
=== Vorago_EcosystemRuleProbe (FR-070) ===
renders: 28 (default + seed twin + 26 extremes), 340 s each, 48 kHz, block 512
default M1 stereo RMS: -26.10 dBFS
t0 = d(default, seed twin) = 1.7128   INAUDIBLE threshold 2*t0 = 3.4256

knob           range              default    extreme      d         d/t0      flag       E cells (P-4)
grazeRate      [0, 3]             0.75       0            0.6311    0.368     INAUDIBLE  .lo,.hi
                                             3            0.2338    0.137                
                                                          0.6311    0.368       (best)
leakRate       [0, 1]             0.06       0            0.4976    0.291     INAUDIBLE  .lo,.hi
                                             1            0.6071    0.354                
                                                          0.6071    0.354       (best)
kernelSigma    [0.01, 0.35]       0.03       0.01         0.3551    0.207     INAUDIBLE  .lo,.hi
                                             0.35         0.5761    0.336                
                                                          0.5761    0.336       (best)
moveRate       [0, 0.5]           0.2        0            0.0194    0.011     INAUDIBLE  .lo,.hi
                                             0.5          0.3265    0.191                
                                                          0.3265    0.191       (best)
freqDrift      [0, 0.0002]        4e-05      0            0.0556    0.032     INAUDIBLE  .lo,.hi
                                             0.0002       0.2975    0.174                
                                                          0.2975    0.174       (best)
forageRate     [0, 0.05]          0.01       0            0.2530    0.148     INAUDIBLE  .lo,.hi
                                             0.05         0.1409    0.082                
                                                          0.2530    0.148       (best)
syncRate       [0, 0.5]           0          0.5          0.2150    0.126     INAUDIBLE  .hi
                                                          0.2150    0.126       (best)
exchangeRate   [0, 3]             0.35       0            0.1511    0.088     INAUDIBLE  .lo,.hi
                                             3            0.1329    0.078                
                                                          0.1511    0.088       (best)
crossAffinity  [-2, 2]            0.45       -2           0.1231    0.072     INAUDIBLE  .lo,.hi
                                             2            0.0190    0.011                
                                                          0.1231    0.072       (best)
predation      [0, 1]             0.55       0            0.0581    0.034     INAUDIBLE  .lo,.hi
                                             1            0.1046    0.061                
                                                          0.1046    0.061       (best)
crowding       [0, 0.2]           0.05       0            0.0879    0.051     INAUDIBLE  .lo,.hi
                                             0.2          0.0545    0.032                
                                                          0.0879    0.051       (best)
selfAffinity   [-2, 2]            -1         -2           0.0066    0.004     INAUDIBLE  .lo,.hi
                                             2            0.0715    0.042                
                                                          0.0715    0.042       (best)
maxSpeed       [0.001, 0.05]      0.03       0.001        0.0684    0.040     INAUDIBLE  .lo,.hi
                                             0.05         0.0001    0.000                
                                                          0.0684    0.040       (best)
feedRate       [0, 1]             0          1            0.0074    0.004     INAUDIBLE  .hi
                                                          0.0074    0.004       (best)

audible knobs: 0 of 14
WARNING: fewer than 4 audible knobs - a 4-6 roster cannot be proposed from audible knobs alone (surface at G1)
=== STOP: gate G1 - ratify R with the user (FR-071) ===
```

### Run 2 — whole-ecosystem audibility at the default surface (10:50:21 → 10:52:14)

`VORAGO_PROBE_KNOBS=- VORAGO_PROBE_REF=900=0` (probe TU diagnostic options; same binary, same math). Log: `artifacts/fr070_probe_ecosystem_off_default.log`. Reference = Ecosystem Depth (ID 900) → 0 at block 0, i.e. the ecosystem lanes silenced. **d(default, ecosystem off) = 0.6312, d/t0 = 0.368** — the entire ecosystem contributes less than one reseed at the default surface, and it equals grazeRate → 0's 0.6311 from run 1 (that extreme starves the colony, which is the same as switching it off).

### Run 3 — full candidate table on the Life-at-maximum surface (10:52:15 → 11:10:52, 18 min 37 s)

`VORAGO_PROBE_SURFACE=109=1 VORAGO_PROBE_REF=900=0` (Life macro at 1.0 → ecosystem depth 1.0, event rate up; roadmap line 466). Log: `artifacts/fr070_probe_life_max.log`. **0 of 14 audible.** t0 = 2.1601; ecosystem off d = 2.1248 (d/t0 0.984); the two best extremes, grazeRate → 0 (2.1321) and leakRate → 1 (2.1242), sit at the ecosystem-off distance — they kill the colony — and every other extreme is below 0.52·t0.

```
=== Vorago_EcosystemRuleProbe (FR-070) ===
renders: 28 (default + seed twin + 26 extremes), 340 s each, 48 kHz, block 512
surface overrides: 109=1
default M1 stereo RMS: -25.84 dBFS
t0 = d(default, seed twin) = 2.1601   INAUDIBLE threshold 2*t0 = 4.3202
reference (900=0): d(default, reference) = 2.1248   d/t0 = 0.984   M1 RMS -25.70 dBFS

knob           range              default    extreme      d         d/t0      flag       E cells (P-4)
grazeRate      [0, 3]             0.75       0            2.1321    0.987     INAUDIBLE  .lo,.hi
                                             3            0.2625    0.122                
                                                          2.1321    0.987       (best)
leakRate       [0, 1]             0.06       0            0.8236    0.381     INAUDIBLE  .lo,.hi
                                             1            2.1242    0.983                
                                                          2.1242    0.983       (best)
kernelSigma    [0.01, 0.35]       0.03       0.01         0.4088    0.189     INAUDIBLE  .lo,.hi
                                             0.35         1.1194    0.518                
                                                          1.1194    0.518       (best)
moveRate       [0, 0.5]           0.2        0            0.0621    0.029     INAUDIBLE  .lo,.hi
                                             0.5          0.3705    0.172                
                                                          0.3705    0.172       (best)
freqDrift      [0, 0.0002]        4e-05      0            0.0398    0.018     INAUDIBLE  .lo,.hi
                                             0.0002       0.1989    0.092                
                                                          0.1989    0.092       (best)
syncRate       [0, 0.5]           0          0.5          0.1921    0.089     INAUDIBLE  .hi
                                                          0.1921    0.089       (best)
predation      [0, 1]             0.55       0            0.0653    0.030     INAUDIBLE  .lo,.hi
                                             1            0.1888    0.087                
                                                          0.1888    0.087       (best)
forageRate     [0, 0.05]          0.01       0            0.1480    0.068     INAUDIBLE  .lo,.hi
                                             0.05         0.0533    0.025                
                                                          0.1480    0.068       (best)
exchangeRate   [0, 3]             0.35       0            0.1174    0.054     INAUDIBLE  .lo,.hi
                                             3            0.1303    0.060                
                                                          0.1303    0.060       (best)
selfAffinity   [-2, 2]            -1         -2           0.0075    0.003     INAUDIBLE  .lo,.hi
                                             2            0.1283    0.059                
                                                          0.1283    0.059       (best)
crossAffinity  [-2, 2]            0.45       -2           0.0655    0.030     INAUDIBLE  .lo,.hi
                                             2            0.0748    0.035                
                                                          0.0748    0.035       (best)
crowding       [0, 0.2]           0.05       0            0.0602    0.028     INAUDIBLE  .lo,.hi
                                             0.2          0.0532    0.025                
                                                          0.0602    0.028       (best)
maxSpeed       [0.001, 0.05]      0.03       0.001        0.0340    0.016     INAUDIBLE  .lo,.hi
                                             0.05         0.0003    0.000                
                                                          0.0340    0.016       (best)
feedRate       [0, 1]             0          1            0.0017    0.001     INAUDIBLE  .hi
                                                          0.0017    0.001       (best)

audible knobs: 0 of 14
WARNING: fewer than 4 audible knobs - a 4-6 roster cannot be proposed from audible knobs alone (surface at G1)
=== STOP: gate G1 - ratify R with the user (FR-071) ===
```

**Reading (main loop, 2026-09-27):** by the spec's own C-7.2 metric the whole ecosystem is worth at most about one reseed on both probed surfaces (0.37·t0 default, 0.98·t0 Life max), so no rule knob can reach the 2·t0 audibility bar on either. The Clarification Q2 premise — that rule knobs add audible variety — is measured false at these surfaces. Surfaced at G1.

### Run 4 — colony owns the wakes (11:20:46 → 11:22:28)

`VORAGO_PROBE_WAKEBASE=0 VORAGO_PROBE_SURFACE=301=0 VORAGO_PROBE_KNOBS=- VORAGO_PROBE_REF=900=0`: the private peak and loop wake bases (0.50 / 0.50, `vorago_voice.h` prepare()) set to 0 through the probe friend and the noise wake base (`kNoiseWakeId` = 301, shipped 0.35) to 0, so `combineWake = max(0, eco, sched)`. Log: `artifacts/fr070_probe_wakebase0_default.log`. t0 = 2.1534; **ecosystem off d = 1.9267, d/t0 = 0.895**, M1 RMS −26.12 (on) vs −25.52 dBFS (off). Even when the colony owns every wake decision its whole contribution stays below one reseed.

## G1 rulings (2026-09-27)

- **E-route risk:** keep the E cells as primaries; FR-017 stop-and-surface catches an unverifiable route in Stage F (ruled with the first G1 question).
- **Roster R:** NOT ratified. The user ruled that the goal is an audible ecosystem and audible knobs, not knobs that measure inaudible. **Phase 14 is paused at G1.** A new Phase 13b (`vorago-phase13b-ecosystem-audibility`, roadmap entry added) gives the ecosystem direct sonic levers, gated on this same probe (ecosystem off vs on ≥ 2·t0 at the default surface and at Life max, then ≥ 4 knobs ≥ 2·t0 that are not colony kills). Phase 14 re-runs its specify stage afterwards; Q2's roster then comes from the 13b probe table.
- Kept in the tree from this pass: T001 (inert friend), T001b (`preset_test_support.h` part 0), T002/T003 (probe TU with its diagnostic options, registered), this record and its four logs.

## Gates on the paused tree (2026-09-27)

- Build: `vorago_tests`, `dsp_systems_tests`, `Vorago` — exit 0, `grep -ci ": warning"` = 0 (`f:/tmp/p14/probe_build5.log`, `build_dsp_plugin.log`).
- Per-push suite: `vorago_tests.exe "~[performance]~[perf]~[benchmark]~[!benchmark]~[long]"` → "All tests passed (3989771 assertions in 67 test cases)", exit 0 (`f:/tmp/p14/vorago_tests_perpush.log`).
- clang-tidy: `artifacts/clang_tidy_vorago_probe.log` — `-Target vorago`, 47 files, Errors 0, Warnings 0 (the first pass reported 21 in the probe TU: designated initialisers, a `_dupenv_s`/`free` pair replaced by `getenv_s`, nested conditionals, an uninitialised member, and the plan §4.3 `const_cast` marked NOLINT with its reason — all fixed, then re-run).
- Portability: `artifacts/portability_probe.log` — `check-portability: all clear -- 1 compiled.` (the probe TU), followed by `wsl --shutdown` (exit 0).
- The four probe runs above were made with the pre-tidy probe binary; the tidy pass changed no arithmetic (initialiser syntax, env reading, string branches), so their figures stand.

## Plan §12 rulings (second pass)

Ruled by the user 2026-09-29 (spec Clarifications "Plan stage (2026-09-29)" is the canonical record):

1. P2-2 take sets — acknowledged (`A_K` / `B_K` of K takes each, K ≤ 8).
2. P2-3 controls (a), (a′), (c) — same-seed single-take comparisons.
3. P2-4 SC-026a access path — **drop** the `VoragoEcosystemRosterProbe` friend; read through `Processor::engineForTest()`.
4. P2-1 order — E0 before stage B, acknowledged.
5. §6.11 skip rules — acknowledged (skip only where the verdict is exact; reason recorded).
6. SC-011 envelope clause — primaries by attack-window reversion, D8/D9 secondaries as state cells; acknowledged.

Task confirmations: T003 is the single mid-phase registration (FR-027a), T060 audits; R-6 lands at T002; the tasks
agent's added per-push cases stay; E0 tolerance 0.0015. Push-dependent tasks (T048, T057, T063) pending a push ruling.

## FR-017a E0 — default-surface t_K curve

Run 2026-09-29 (T008, plan P2-1), alone, nothing else executing, on the binary built 11:42:46 (newer than
`preset_pilot_test.cpp` 11:42:07):

```
build/windows-x64-release/bin/Release/vorago_tests.exe "Vorago_PresetPilot_DefaultSurfaceTakeCurve" > f:/tmp/p14/e0.log 2>&1
```

- Exit 0; "All tests passed (53 assertions in 1 test case)". Measured wall clock **249 s** (11:45:52 → 11:50:01),
  pool width 4 (plan §10 estimated ≈ 9 min).
- Log: `artifacts/e0_default_take_curve.log`.
- Assertions: 16 seeds + the serial seed-0 render all finite; peaks 0.1732…0.2291 (≤ 0.9661); M1 stereo RMS
  −27.07…−25.38 dBFS (≥ −60). Pool trust check (seed 0 M1, serial vs pooled): L metric 0.000e+00 sample 0.000e+00
  ok | R metric 0.000e+00 sample 0.000e+00 ok.

| K | t_K | 2·t_K | pass (2·t_K ≤ 4.0) |
|---|---|---|---|
| 1 | 5.9515 | 11.9031 | no |
| 2 | 3.2013 | 6.4026 | no |
| 4 | 0.7293 | 1.4587 | yes |
| 8 | 0.8423 | 1.6846 | yes |

Cross-check (log line): `t_1 = 5.9515  13b t0on = 5.9515  |diff| = 0.000044  tolerance = 0.0015` — within
tolerance; the harness reproduces 13b's figure.

Verdict (log line): `E0: 2*t_8 = 1.6846 -> PROCEED` — `2·t_8 = 1.6846 ≤ 4.0`, no FR-017a stop; proceed to stage B.
Note: `t_8` (0.8423) is slightly above `t_4` (0.7293); both are far under the bar, and the curve is not required to
be monotone.

## Stage B

T022 gate, run 2026-09-29 13:02 → 13:41. Logs in `f:/tmp/p14/`. **Verdict: RED.** The `-Target vorago` clang-tidy
gate reports 5 warnings (listed below). The other gates are green.

- **Build (zero warnings):** `cmake --build build/windows-x64-release --config Release --target dsp_systems_tests
  vorago_tests Vorago`. First an incremental pass (`stageB_build.log`, exit 0, 0 warnings). That pass recompiled only
  some TUs, so it cannot show zero warnings across all of them. I then deleted every `.obj` of the three targets
  (99 `dsp_systems_tests`, 61 `vorago_tests`, 7 `Vorago`) and rebuilt (`stageB_rebuild.log`, 13:03:35 → 13:09:02).
  Result: exit 0, 166 TUs listed, `grep -ci ": warning"` = 0, `grep -ci " error"` = 0. The POST_BUILD step installed
  Vorago and its factory presets normally.
- **Per-push `dsp_systems_tests`:** `dsp_systems_tests.exe "~[performance]~[perf]~[benchmark]~[!benchmark]~[long]"`
  → `All tests passed (6061314 assertions in 1414 test cases)`, exit 0, 13:10:45 → 13:37:16
  (`stageB_dsp_systems_perpush.log`).
- **Per-push `vorago_tests`:** same filter → `All tests passed (4011642 assertions in 84 test cases)`, exit 0,
  13:37:32 → 13:41:30 (`stageB_vorago_perpush.log`).
- **pluginval:** `tools/pluginval.exe --strictness-level 5 --validate
  "build/windows-x64-release/VST3/Release/Vorago.vst3"` → exit 0. Every section logged `Completed tests in …` with no
  failure line. It reported `Krate Audio: Vorago v0.2.0.1`, `Reported taillength: 95` and a 0-in / 2-out bus. The vst3
  validator section was skipped ("validator path hasn't been set") (`stageB_pluginval.log`).
- **clang-tidy:** I regenerated `build/windows-ninja/compile_commands.json` first (`cmake -S . -B build/windows-ninja`,
  13:05:48; `stageB_ninja_config.log`). The 06:51 database had no entries for the new phase-14 TUs; it now lists all
  six (`preset_{sweep,matrix,pilot,load_rt,cpu}_test.cpp`, `ecosystem_roster_test.cpp`).
  - `-Target dsp`: 372 files, `Errors: 0`, `Warnings: 0`, exit 0 (`stageB_tidy_dsp.log`). **Green.**
  - `-Target vorago`: 57 files, `Errors: 0`, **`Warnings: 5`**, exit 0 (`stageB_tidy_vorago.log`). **Red.**
    - `plugins/vorago/tests/integration/preset_pilot_test.cpp:198:22` and `:199:22` — `bugprone-misplaced-widening-cast`
      (`static_cast<std::size_t>(3 * K)`, from T007).
    - `plugins/vorago/tests/unit/preset/factory_preset_test.cpp:40:5` and `:49:5` — `readability-use-anyofallof`
      (loops in `allFiniteSamples` / `allWithin`, from T004/T006).
    - `plugins/vorago/tests/unit/preset/factory_preset_test.cpp:149:27` — `readability-container-contains`
      (`b.count(x) == 0u`, from T006).
- **Portability:** `node tools/check-portability.js` → `check-portability: all clear -- 28 compiled.`, exit 0
  (`stageB_portability.log`). The default changed-files run left out two untracked TUs,
  `unit/state_v3_test.cpp` and `unit/tail_samples_test.cpp`, so I checked them explicitly →
  `all clear -- 2 compiled.` (`stageB_portability_extra.log`). `wsl --shutdown` ran after each run, exit 0.

T022 is a no-code gate, so I did not apply the five tidy fixes here. Each is a mechanical edit in its owning test file:
`static_cast<std::size_t>(3) * static_cast<std::size_t>(K)`, `std::ranges::all_of`, and `!b.contains(x)` /
`a8.contains(x)`. After those edits, `-Target vorago` must be re-run to 0 before stage B closes.

**Closed by the main loop (2026-09-29 13:42-13:51).** The five findings were fixed in their owning files (`preset_pilot_test.cpp`: `3u * static_cast<std::size_t>(K)`; `factory_preset_test.cpp`: `std::ranges::all_of` in `allFiniteSamples` / `allWithin`, `!b.contains(x)`, `<algorithm>` included). `vorago_tests` rebuilt: exit 0, 0 warnings (`f:/tmp/p14/tidyfix2_build.log`). `run-clang-tidy.ps1 -Target vorago`: 57 files, **Errors 0, Warnings 0** (`artifacts/stageB_clang_tidy_vorago_fixed.log`). Per-push `vorago_tests`: `All tests passed (4011642 assertions in 84 test cases)` (`artifacts/stageB_vorago_tests_fixed.log`). **Stage B verdict: GREEN.**

## FR-017a pilot / G2

Run 2026-09-29 (T039), alone, with nothing else executing (no build, test, tidy or pluginval process was running at
launch). The binary was built 16:29:59, after `preset_pilot_test.cpp` (16:29:11):

```
VORAGO_SWEEP_THREADS=4 build/windows-x64-release/bin/Release/vorago_tests.exe "Vorago_PresetPilot_Calibrate" > f:/tmp/p14/pilot.log 2>&1
```

- Exit 0; `All tests passed (256 assertions in 1 test case)`. The assertions cover only the timeline cross-check and
  finite renders with peak ≤ 0.9661, so a pass here does not mean the gate passed.
- Wall clock: **2229 s (37.2 min)**, 16:34:15 → 17:11:24, pool width 4. Plan §10 estimated ≈ 20 min, so the real
  run was 1.9× longer. Use this figure when estimating T043 and later runs.
- Log: `artifacts/pilot_calibrate.log`.
- Timeline cross-check (log line): `A = 155.000  M1 = [160.000, 220.000]  M2 = [220.000, 280.000]  M3 = [280.000,
  340.000]  H = 340.000  (E0: 155 / 340)`. This matches T007's constants.

### t_K curves (16 seed indices each; pass means 2·t_K ≤ 4.0)

| Preset | t_1 | t_2 | t_4 | t_8 |
|---|---|---|---|---|
| P0 default surface | 5.9515 no | 3.2013 no | 0.7293 yes | 0.8423 yes |
| P1 Locked Choir | 2.1376 no | 1.0624 yes | 0.6494 yes | 0.2152 yes |
| P2 Clotting Colony | 1.2254 yes | 1.2278 yes | 0.7310 yes | 0.7372 yes |
| P3 Tectonic Floor | 0.7428 yes | 1.3691 yes | 0.3344 yes | 0.7175 yes |
| P4 Cathedral Void | 1.9224 yes | 0.8153 yes | 0.6223 yes | 0.3963 yes |
| P5 Growth Ring | 3.1493 no | 2.0829 no | 0.5176 yes | 0.9437 yes |
| P6 Glass Well | 5.4401 no | 2.4883 no | 1.5595 yes | 1.3427 yes |

P0's row matches E0 exactly (5.9515 / 3.2013 / 0.7293 / 0.8423). **Ruled K = 4** (log line `ruled K = 4`). K = 4 is
the smallest K that passes for every preset, and it is ≤ 8.

### d(P3, P3′) (Q6)

`d(P3, P3') over A_4 = 4.8057 -> STOP (Q6)`. P3′ is Tectonic Floor with 600 moved +0.125 normalized and 700 moved
+0.05. The distance **4.8057 ≥ 4.0**, so the near-variant control **fails**.

### Pilot primary scores (K = 4)

| Preset | Primary | s(P) | primary d | bar max(4.0, 2·s) | verdict |
|---|---|---|---|---|---|
| P0 default surface | none | 1.9207 | — | — | — |
| P1 Locked Choir | E6.hi | 4.2818 | 0.1476 | 8.5636 | **FAIL (Q5, FR-017)** |
| P2 Clotting Colony | E7.hi | 3.4558 | 0.0721 | 6.9117 | **FAIL (Q5, FR-017)** |
| P3 Tectonic Floor | S5 | 4.9473 | 6.9068 | 9.8946 | **FAIL (C-2.2)** |
| P4 Cathedral Void | S8 | 1.8587 | 9.4813 | 4.0000 | PASS |
| P5 Growth Ring | D8.2 | 1.4590 | 4.4348 | 4.0000 | **FAIL (C-2.2)**: attack window d_att 4.4348 < d_Sus + 1.5 = 9.9347 |
| P6 Glass Well | D1.1 | 4.3529 | 3.5381 | 8.7058 | **FAIL (C-2.2)**; the S10 conjunct passed (d 10.4043 ≥ 8.7058) |

P5 attack detail: P reaches RMS(Sus) − 6 dB at 20 s, P_rev at 5 s.

### Verdict

Log line: `G2: STOP (d(P3, P3') >= 4.0 (Q6); P1 Locked Choir primary E6.hi ... below its bar (Q5, FR-017); P2
Clotting Colony primary E7.hi ... below its bar (Q5, FR-017); P3 Tectonic Floor primary S5 subharmonic below its bar
(C-2.2 ...); P5 Growth Ring primary D8.2 envelope Growth below its bar (C-2.2 ...); P6 Glass Well primary D1.1
material Glass below its bar (C-2.2 ...))`.

**G2 = STOP (FR-017).** Two of the failures are user-ruled stops under T039: the Q6 near-variant distance, and P1/P2
under Q5. Nothing was re-authored or re-run, and no bar, floor or K was changed. The C-2.2 re-author route for P3, P5
and P6 was not taken either, because the Q5 and Q6 stops need the user's ruling first.

Observations for that ruling (measured, not remedies):
- P1 and P2 score primary d = 0.148 and 0.072 against bars of 8.56 and 6.91, roughly 50–100× short. Reverting the
  colony knob changes the take-mean descriptor by less than the preset's own seed spread. This is consistent with the
  Phase 13b finding that the ecosystem is a wake-only lane.
- Most bars are set by s(P) rather than by the 4.0 floor: P1, P2, P3 and P6 have s(P) between 3.46 and 4.95. These
  s(P) values are 3–15× larger than the same presets' t_4 values.
- The Q6 failure (4.81 at a +6 dB sub move plus a 0.05 smear move) means the distance metric counts a small near-variant
  as a new sound. The C(N, 2) distinctness floor and the controls rest on that same metric.

### Main-loop probes before the G2 ruling (2026-09-29 17:10-17:23)

The colony-knob primaries (E6.hi 0.148, E7.hi 0.072 on the pilot's one-minute Sus window) were 15-30× under Phase 13b's readings for the same knobs (2.16 / 2.64), so the 13b probe was re-run on the pilot surfaces before ruling (`artifacts/g2_*_probe.log`, all four renders per run, 340 s, M1-M3 mean descriptor):

| surface | measurement | d | note |
|---|---|---|---|
| P1 Locked Choir | ecosystem on vs true-off | 1.1971 | default surface 5.6272, Life max 5.8810 (13b) — the colony-forward authoring made the colony LESS audible |
| P1 Locked Choir | 901 = 1.0 vs reference 901 = 0 (the E6.hi reversion twin) | **0.8486** | pilot (M1 only) 0.148; F = 4.0; bar 8.56 |
| P2 Clotting Colony | ecosystem on vs true-off | 2.1653 | |
| P2 Clotting Colony | 902 = 1.0 vs reference 902 = 0.25 (the E7.hi reversion twin) | **0.1459** | pilot (M1 only) 0.072 |
| Life max + 901 = 1.0 (control) | vs reference 901 = 0 | **2.1446** | reproduces 13b's `final2_table_lifemax.log` syncRate .hi 2.1446 exactly, through the PARAMETER path — the harness and the roster wiring are sound |

Also found: the 13b probe's own knob rows for syncRate / selfAffinity now read 0 because it drives the engine through the friend seam and the processor's per-block roster push (Phase 14 T017) overwrites that; the `VORAGO_PROBE_REF=id=value` parameter path is the valid one and was used above. Conclusion: the E6/E7 primaries are not a harness defect — on any measured surface the counted knob moves the descriptor by 0.15-2.14, always under F = 4.0 (two reseeds), and the colony-forward surfaces reduce the whole colony's contribution to 1.2-2.2.

## Gate G2 rulings (2026-09-29; canonical record in spec.md Clarifications "Gate G2 rulings")

1. E6.hi / E7.hi are secondaries; N = 38 (the two colony-knob showcase rows withdrawn; `requiredPrimaryCells()` = 38).
2. Near-variant: the pilot re-measures a nearer pair (sub +2 dB, smear +0.02) against the floor d < F; a second failure returns to the user.
3. Twin bars: primary F = 4.0, secondary 1.5, the `2·s(P)` term recorded (`twoS`, `selfDistance`) but not gated; the pairwise distinctness floor unchanged.
4. Proceed: the main loop re-runs the pilot (P0 + Tectonic Floor, Cathedral Void, Growth Ring, Glass Well), re-authors any pilot still under its bar, and continues.

## G2 re-author loop (main loop, 2026-09-29 17:49-18:35)

Instrument: `Vorago_PresetPilot_PrimaryProbe` (`[.probe]`, `VORAGO_PILOT_PRESET=<name>`), added to the pilot TU: the stored-seed take plus the primary twin (and S conjunct), scored exactly as the calibrate case scores them, about 90 s per candidate. Bars per the G2 rulings (primary F = 4.0, secondary 1.5).

| preset | version | change | reading | log |
|---|---|---|---|---|
| Tectonic Floor (S5) | as authored | — | pilot run 2: d 6.9068 ≥ 4.0 PASS; near-variant (+2 dB / +0.02) d 1.6445 < 4.0, floor holds | `pilot_calibrate_run2.log` |
| Cathedral Void (S8) | as authored | — | pilot run 2: d 9.4813 PASS | `pilot_calibrate_run2.log` |
| Growth Ring (D8.2) | v1 | Life 0.5, bloom 0.85, mutation 0.35 | d_att 4.4348 vs d_Sus 8.4347 + 1.5 FAIL | `pilot_calibrate_run2.log` |
| | v2 | every trajectory-sensitive section quieted (Life 0.2, depth 0.15, bloom 0.3, mutation 0.1, ghost 0.1) | d_att 5.5424 vs d_Sus 6.9823 + 1.5 FAIL | `g2_probe_Growth_Ring_v2.log` |
| | **v3** | stage times 1 + 20 + 19 + 20 s = the 60 s growth duration, so both renders' Sus windows coincide (plan 6.1 puts each at its own attack span; before, [65, 125] vs [160, 220] — d_Sus measured that offset, not the envelope) | **d_att 8.1910, d_Sus 0.1817 → PASS** (P reaches RMS(Sus) − 6 dB at 30 s, P_rev at 4 s) | `g2_probe_Growth_Ring_v3.log` |
| Glass Well (D1.1) | v1 | blend 0.2, 25 s dark cavern, ghost 0.4 | d 3.5381 FAIL | `pilot_calibrate_run2.log` |
| | v2 | body A alone, damping 0.05, richer cloud, 8 s paler cavern | d 3.5286 FAIL | `g2_probe_Glass_Well_v2.log` |
| | v3 | cavern a quarter wet, body mix explicit, resonance 0.98 | d 3.9293 FAIL | `g2_probe_Glass_Well_v3.log` |
| | **v4** | cavern a tenth wet, no ghost, noise −40 dB, cloud richness 0.8 | **d 4.4031 → PASS**; S10 conjunct 12.51 | `g2_probe_Glass_Well_v4.log` |

Pilot run 3 (the G2 gate on the v3 / v4 tree): `pilot_calibrate_run3.log`.

### Pilot run 3 — gate G2 (2026-09-29 18:38 → 19:03, `artifacts/pilot_calibrate_run3.log`)

Alone, `VORAGO_SWEEP_THREADS=4`, binary built 18:33 after the v4 defs; wall clock 1469.8 s (24.5 min) for P0 + four pilots × 16 takes, the near-variant's 4 takes and 5 twin renders. **Ruled K = 4** (unchanged). `d(P3, P3') over A_4 = 1.6445 -> ok` (floor F = 4.0). Primaries at the ruled bar F = 4.0: Tectonic Floor S5 **6.9068 PASS**; Cathedral Void S8 **9.4813 PASS**; Growth Ring D8.2 **d_att 8.1910 PASS** (d_Sus 0.1817, attributable iff ≥ 1.6817); Glass Well D1.1 **4.4031 PASS** (S10 conjunct ok). Log line: **`G2: PROCEED with K = 4`**. s(P) recorded: 4.9473 / 1.8587 / 2.4950 / 1.9599. Per-push `vorago_tests` on the same binary (`artifacts/g2_vorago_tests_perpush_v4.log`): 116 of 117, the one red being `Vorago_FactoryPresets_LibraryShape` (`factory_preset_test.cpp:570`, allowed red until T047 per R-7(ii)).

## T043 ruling (2026-09-29)

Measured default-state set = 9 cells (`artifacts/default_state_vector.log`); D3.1–D3.4, D4.6, D6.1 fell out (S1 d 0.0110, S4 d 0.0009 on the default surface: the noise bed is inaudible there). Derived required primaries = 42. **User ruling: accept N = 42** — four noise-model showcase rows (D3.1 Direct, D3.2 FilteredWind, D3.3 GranularDust, D3.4 MetallicHiss), each with an audible noise organism; D4.6 / D6.1 as secondaries of presets whose noise is audible. Encoded: `kRecordedDefaultStateCells` (9 cells), RequiredPrimaries / LibraryShape counts 42, spec Clarifications "T043 ruling", plan §7 / §10, tasks Stage F.

## T048 — first full local sweep (main loop, 2026-09-29 21:36 → 2026-09-30 00:21) and its diagnosis

Run: four shards `VORAGO_SWEEP_SHARD=i/4 VORAGO_SWEEP_THREADS=1 vorago_tests.exe "[vorago-sweep]~[vorago-aggregate]"` in parallel (logs `f:/tmp/p14/shard_{0..3}.log`, 2 h 21 min – 2 h 38 min each), then `[vorago-aggregate]` (`aggregate.log`, 8 min). Per shard: 4 of 5 cases pass (shard 2: 2 of 5). Aggregate (N = 42, K = 4): **17 of 42 primaries verified** (S4, S5, S8, S10, M1, M5–M8, M11, M12, E2, D1.1, D1.3, D1.4, D1.9, D8.2); 56 claimed-but-failed cells; 42 cells with no factory verifier; pairs 861: min d 0.33, median 8.30, max 20.57; t_max 1.4913; **313 pairs below the C-7.3 floor** (97 of them under 4.0); 55 SUBSET pairs; boundedness: Stone Gravity 10 s RMS −5.84 dBFS against the −6 bar (limiter ceiling) and two LongRender arm failures on it.

Diagnosis (main-loop probes, `artifacts/sweep_diag_*.log`, `VoragoVoice_NoiseBusProbe` in `vorago_voice_test.cpp`):

1. **The noise organism is inaudible by construction.** Voice-level probe: noise at +12 dB with wake base 1.0 vs noise at −96 dB — output RMS −19.79 vs −19.69 dBFS, difference signal **−52.7 dBFS**; plugin-level probe (13b probe, `300=1.0,301=1.0` vs `300=0`): descriptor unchanged (band 0: 7.3758 vs 7.3810), d 0.195. Cause: the organism calibrates every model to a Direct-slot reference of −50.6 dBFS (`noise_organism.h` kModelTrimDb), so at its maximum the bed sits ≈ 33 dB under the drone. Consequence: S1, D3.1–D3.4, D4.1–D4.12, E3 (5 required primaries, 16 cells) cannot verify on any preset; every noise-carrying preset (Wind Through Basalt +6 dB / wake 0.7: S1 d 0.24) confirms it. Not a Phase 14 authoring gap — a Phase 10 voicing fact.
2. **The cross-preset floor's `2·max(s(P), s(Q))` term** (kept at G2-3, which ruled twins only) sets floors up to 13.7 (Monolith) on evolving presets; 216 of the 313 failing pairs would pass a floor of 4.0. Same logic as G2-3: D(P) vs D(Q) are K-take, three-minute means whose noise is the take term (t_max 1.49), not either preset's own evolution.
3. **The twin window (Sus = M1, 60 s) under-measures slow capabilities.** The same twin scored on the M1–M3 mean reads 2–5× larger (E6.hi 0.15 → 0.85; Life's full travel on the default surface 1.68 over three minutes). Macros M2/M3/M4/M9/M10 (2.4 / 3.7 / 0.9 / 2.1 / 0.35), the bloom/ghost routes and D14 (breathing/tidal) all sit in that band.
4. **Authoring gaps** (no ruling): Slow Bloom's S6 twin d = 0 exactly — spawn 0.01 Hz puts no bloom inside the window (bloom depth 1.0 → 0 on the default surface reads 1.52); Stone Gravity over the limiter ceiling; 97 pairs under 4.0 (e.g. Erosion vs Crowded Dark 0.33, Column Hymn vs Glass Sphere 1.18, Crowded Dark vs Teeming 1.40); D1 materials Strings 2.0, Ice 3.1, WoodenHull 0.44, CavernWall 1.2, GlassSphere 2.8 (the Glass pilot needed the cavern at a tenth wet to reach 4.4); S9 ghost 0.34; D9.1 1.3; E1 0.21, E5 1.5, E4 2.05 (routes).

Surfaced for rulings (FR-017): the noise bus, the cross-preset floor term, the twin window.

## Sweep rulings (2026-09-30; canonical record in spec.md Clarifications "Sweep rulings")

S-1 noise-bus make-up gain +30 dB in `VoragoVoice` (FR-077 / SC-033; probe with the gain: noise +12 dB → output RMS −22.75 dBFS, difference −25.05 dBFS against the −19.69 dBFS drone; before: difference −52.7). S-2 pair floor = max(F, 2·t_max), s(P) recorded. S-3 twins on M1…M3 (`twinSusDescriptor`, twin renders to the end of M3; the attack twin captures its three sustain minutes). Re-authored: Stone Gravity (sub +6 → 0 dB), Slow Bloom (spawn 0.01 → 0.03 Hz). Sweep 2 follows on the amended tree.

## Noise make-up: regression on the amended voice (2026-09-30 07:30-09:00)

Per-push after the +30 dB make-up (`artifacts/sweep2_vorago_perpush.log`: vorago green, 117 of 117 incl. LibraryShape at 42; `artifacts/sweep2_dsp_systems_perpush.log`: 1412 of 1414). The two reds are Phase 10 / 13b render sentinels:

1. `VoragoEngine_GhostExtensionWiring` clause (a): 13b's fifth-harvest fingerprint moved (checkpoint[30] 0.155 vs 0.231, relative error 0.086 vs 0.005) — the faint default bed re-routes the colony's trajectory. Per FR-077: a SIXTH harvest inside `dsp_systems_tests` on the final voice tree, once the make-up gain is final.
2. `VoragoEngine_SlotSeedReproducibility` (a)/(b): worst metric 2.65e-4 vs 2.5e-4. Diagnosed with hidden probes (`VoragoVoice_ResetReproProbe`, `VoragoVoice_NoiseOrganismResetProbe`, `VoragoEngine_ResetReproProbe`): the two renders diverge at sample 2049 (8 kHz engine) in every reset variant (retire / immediate / tail) and with the smear or ghost at 0; fresh engines are bit-exact; with the make-up at 0 dB the same divergence is 1.15e-6 (inside the bound), at +30 dB 4.07e-5 = 31.6× — it lives entirely in the noise path and predates the change; the 2048-sample state that reset leaves different from prepare was not located (engine reset clears limiter, saturators, smear, atmosphere; the organism's reset re-seeds and snaps its ramps). **Ruling (2026-09-30):** wait for sweep 2, then pick the make-up gain: if the noise presets clear their bars with margin at +30 dB, drop to +24 dB (divergence back under the bound, bed ≈ 11 dB under the drone); otherwise return with the numbers.

## Sweep 2 (2026-09-30 08:05–12:56) — results, rulings S-4…S-6, the reset defect

**Run.** `artifacts/sweep2_wall.txt`: shards 08:05:28 → 12:43:26 (shard 0 16 678 s, 1 14 919 s, 2 14 568 s,
3 14 297 s of `LongRender`), aggregate → 12:56:31 (`SoundSpaceDistinct` 785 s). `artifacts/sweep2_aggregate.log`
line 665: "Required primaries from the measured set: 42; from the recorded constant: 42; verified
primaries: 14" (lines 683–686 list them: S1, S5, S8, S10, M1, M6, M7, M11, E2, D1.1, D1.3, D1.4, D1.9,
D8.2); "Pairs 861: min d 0.9146 (Monolith vs Dead Air) median d 8.0392 max d 20.7176 … pairs below floor:
99" (t_max 1.4323, K 4). Shard 2 (`sweep2_shard_2.log` lines 43–60, 127): Stone Gravity take 0 arm 3
late-sustain +14.32 dB, take 3 arm 1 hi −5.94 dB, 44.1 kHz hi −5.97 dB — still at the limiter (peak 0.966)
after the sub went +6 → 0 dB. Per-primary d for the 28 failing primaries: the PRI rows of the four shard logs
(e.g. S6 Slow Bloom 0.0000, E1 Bloom Colony 0.0571, S9 Choir of Absence 0.2229, D1.8 Hull Ark 0.2065,
M10 Teeming 0.4279, E5 0.4159, E3 0.6497 — the inert-looking cells the re-author loop probes first; and
S3 3.98, S2 3.64, M5 3.46, D1.5 3.14 just under the bar). Every D3.x row: "state-only kind … pri NO".

**Noise-organism probes (before ruling S-4 / S-5).** `artifacts/sweep2_noise_probe_baseline.log`: the pilot
probe reproduces the sweep exactly (Dead Air S1 conjunct 0.1168, Wind Through Basalt 5.3512; ~2 min each).
`artifacts/sweep2_noise_probe_variants.log` (ten `VORAGO_PILOT_OVERRIDE` runs, 13:06–13:27): Dead Air
300=1.0,301=1.0 → 1.0589; 300=0.944444,301=1.0 → 0.4552; +313=0.333333 → 3.0992; WTB 313=0,323=0 →
5.3024; WTB 300=0.888889 → 4.0936; WTB 300=0.944444,301=1.0 → 6.3892; WTB 300=1.0,301=1.0 → 6.3995;
Spore Drift 300=0.944444,301=1.0 → 2.9763; Steam Vent → 5.6713; Abyssal Wind → 4.5917. Rulings S-4 (D3
primary = state ∧ S1 d ≥ F) and S-5 (+30 dB stays) recorded in spec.md Clarifications "Sweep 2 rulings".
Harness: `preset_test_support.h` pass 3 copies the S1 conjunct's render terms into D3.x, `verifiedAt`
(Capability overload) admits D3.x at Primary; `preset_pilot_test.cpp` `applyD3PrimaryRule`;
`factory_preset_test.cpp` "verifiedAt at Primary on StateWithS" and `preset_sweep_test.cpp`'s default-surface
vector case updated to the rule.

**Reset defect (ruling S-6, root cause found and fixed).** Evidence chain, each a hidden probe run from the
main loop:
1. `artifacts/reset_probes_run2.log` — `VoragoEngine_ResetReproProbe` (both arms after a reset, 8 kHz):
   default 4.065e-5, smear 0 4.295e-5, ghost 0 4.065e-5, first differing sample 2049 in every variant;
   `VoragoVoice_NoiseOrganismResetProbe` path 3 (organism alone, both arms after reset): max diff 0.
2. `artifacts/reset_engine_bisect_lanes.log` — macro-matrix bases pushed through `VoragoMacroMatrix::apply`:
   noise level −96 dB 1.521e-6, noise wake base 0 1.306e-5, ecosystem depth 0 3.376e-5, smear 0 4.295e-5,
   ghost 0 4.065e-5, bloom depth 0 4.065e-5, noise −96 + eco 0 1.172e-6 → the noise bus carries it.
3. `artifacts/reset_engine_bisect_downstream.log` — ecology mix 0 5.337e-5, ecology loop gain 0 4.060e-5,
   resonance mix 0 6.214e-5, **body mix 0 4.066e-3**, cloud richness 0 3.563e-5 → the divergent signal is
   the excitation itself (the bodies were attenuating it).
4. `artifacts/reset_voice_shape_models.log` — `VoragoVoice_SentinelShapeProbe` (one voice, the sentinel's
   two arms, body mix 0, noise +12 dB, wake 1.0): all Direct 0.000e+00, all FilteredWind 0.000e+00, all
   GranularDust 0.000e+00, **all MetallicHiss 5.404e-3** (peak 3.40, first differing sample 0); wander 0 /
   breathing 0 / eco 0 do not remove it (3.130e-3); comb echoes and every per-slot observable (gain, wake,
   level, cutoff, RMS) identical in both arms at 0.5 / 1.0 / 1.5 / 2.0 s
   (`artifacts/reset_voice_shape_series.log`).
5. `artifacts/reset_organism_paths.log` — the organism alone on MetallicHiss, both arms after reset: 0 at
   48 kHz (path 5), 0 under per-step level + wake modulation (path 6), 0 at 8 / 16 / 44.1 kHz (path 7).
6. Code: `noise_generator.h` keeps `levelSmoothers_[kNumNoiseTypes]` (5 ms one-poles) that `reset()` does
   not touch; `noise_organism.h` `settleSourceLevelSmoothers()` (prepare only) settles the types active at
   prepare; `applySlotConfiguration` enables the new type (Blue for MetallicHiss) with a target the smoother
   reaches only by rendering. The sentinel's prime arm never renders before its capture.
7. Failing test first: `NoiseOrganism_TypeSwitchedAfterPrepareReplaysAfterReset` — before the fix
   `maxDiff := 0.0121f`, `firstDiff := 0`, `peak := 1.2817f` (two CHECKs failed). Fix:
   `NoiseGenerator::snapLevelSmoothers()` + the call in `NoiseOrganism::applySlotConfiguration`. After:
   `artifacts/reset_fix_targeted.log` — the regression case, `VoragoEngine_SlotSeedReproducibility` and
   `VoragoEngine_ResetReproProbe` "All tests passed (15 assertions in 3 test cases)", every probe variant
   max|diff| 0.000e+00 (first differing sample 16000 = none).

**Sixth ghost-fingerprint harvest (SC-033, 2026-09-30 14:24).** On the final gain (+30 dB) and the FR-077a fix, `VoragoEngine_GhostExtensionWiring` clause (a) read worst metric relative error 0.0863958 (bound 0.005; checkpoint[30] 0.155 vs 0.231) against the fifth-harvest constant (`artifacts/ghost_fingerprint_harvest6_run1.log` line 43). Re-harvested inside `dsp_systems_tests` by the clause's own printer, run twice (`_run1.log`, `_run2.log`; literals byte-identical, md5 f1e53f803ff886a231cdc88ebed02698), pasted into `atmosphere_ghost_fixtures.h` with a SIXTH PROVENANCE block (the fifth kept, marked superseded). Verifying rebuild + re-run: "All tests passed (27 assertions in 1 test case)" (`/f/tmp/p14/harvest6_verify_run.log`). Regression on the fix before the harvest: `artifacts/resetfix_dsp_processors_systems.log` — dsp_processors 3311/3311, dsp_systems 1414/1415 (the one red = this fingerprint); `artifacts/d3rule_vorago_perpush.log` — vorago per-push 118/118 on the D3 rule + fix.

**Re-author loop instruments (2026-09-30 14:26, `artifacts/bloom_ghost_probes.log`).** `VoragoVoice_BloomCountsProbe` (8 kHz, 340 s, spawn 0.03 Hz): blooms DO spawn (9 events / 6 spawned / 14 rejected / 6 live at depth 1.0; 6 / 6 / 7 / 6 at base depth 0.0, because the ecosystem's partial lane holds the effective depth at 0.627 with eco depth 0.85) at every richness (0.35: 4 active partials, capacity 14; 0.9: 42) — so Slow Bloom's S6 d = 0.0000 is not "no bloom"; the pilot probe on Slow Bloom itself is next. `VoragoEngine_GhostLevelProbe` (8 kHz, 340 s, default surface, ghost peak level 0 / 0.5 / 1.0 through the macro matrix): the ghost tap alone (difference signal) reads −44.7 / −38.6 dBFS RMS over 220–340 s against a −19.3 dBFS drone; at 1.0 its loudest 1 s window is −33.0 dBFS (14 dB under the drone) and it sounds in 62 of 120 one-second windows (5 of 120 at 0.5) — the ghost is a faint, half-time burst by construction (`atmos_.setLevel(ghostPeak_ * ghostRequest)`), consistent with Choir of Absence's S9 0.22 and Haunted Colony's E5 0.42.

## Re-author loop after sweep 2 (2026-09-30 14:12 →) — probe batches, `artifacts/reauthor_probe_batch*.log`

Every figure is `Vorago_PresetPilot_PrimaryProbe` on the stored-seed take with `VORAGO_PILOT_OVERRIDE`
(ID=normalized), bar F = 4.0, the S1 conjunct at 1.5 where the rule needs it; the D3 rule of S-4 applies.
Adopted values are in `tools/vorago_preset_defs.h` with a "sweep 2 probe" note on the line.

**Batch 1 (`reauthor_probe_batch1.log`, D3 rule end-to-end).** Steam Vent 300=0.944444 (+6 dB), 301=1.0 →
S1 5.6739 **PASS**; Abyssal Wind same → 4.5922 **PASS**; Spore Drift 300=1.0 (+12 dB), 301=0.75 → 2.7993,
301=1.0 → 3.2422 (the dust drives the limiter: louder reads lower); Dead Air +12 dB / wake 1.0 with slots 2–3
TapeHiss / Pink → 3.4516, wake 0.6 → 2.5497.

**Batch 2 (`reauthor_probe_batch2.log`).** Spore Drift 300=0.888889 (0 dB), 301=0.75 → 4.3746 **PASS**
(−6 dB 2.9702; −3 dB / wake 1.0 4.2719); Dead Air +12 dB / wake 1.0 with White / Pink / TapeHiss / Brown →
4.1026 PASS (+6 dB 2.8950); Slow Bloom as authored → 0.0000, with 900=0.0 → 0.0000; Choir of Absence
0.2229; Teeming 0.4286; Drifting Strata 2.1514 (the three baselines equal their sweep-2 rows).

**Batch 3 (`reauthor_probe_batch3.log`).** Smeared Horizon 1105=0.2 → 4.3010 **PASS** (702=1.0 too: 4.2369);
Resonant Shaft 1105=0.2 → 4.7602 **PASS** (400=0.15 adds nothing: 4.7602); Stone Gravity 600=0.375 → 3.5242,
600=0.25 + 200=0.4 → 3.1923; Ice Shelf 1002=0.99, 1001=0.01, 201=1.0 → 4.3676 **PASS**; Glass Sphere the same
levers → 3.9445; Crowded Dark 200=0.2 → 1.8576 (worse than 2.77); Weighted Deep 1003=1.0 → 2.7782 (no
change); Dead Air +12 dB / wake 1.0 / loud types + 200=0.2, 1003=0.3 → **8.8078 PASS** (adopted over the
4.10 row: a thin tone under the dead channel).

**Adopted so far (defs):** Steam Vent +6 dB / wake 1.0; Abyssal Wind +6 dB / wake 1.0; Spore Drift 0 dB;
Dead Air +12 dB / wake 1.0, types White / Pink / TapeHiss / Brown (secondaries D4.1 / D4.2 / D4.3 / D4.6 —
Velvet, VinylCrackle and Blue are near-silent as a bed; D4.4 stays verified on Erosion, D4.10–D4.12 on
Fogbound), richness 0.20, body mix 0.30; Smeared Horizon space mix 0.20; Resonant Shaft space mix 0.20
(new line); Ice Shelf resonance 0.99 / damping 0.01 / tilt +8 dB.

**Instrument findings.** The bloom needs SOUNDING parents: `VoragoVoice_BloomCountsProbe` shows children
spawn at every richness, but a child is scaled by its parent's amplitude and the parent region is
[0, reserveBase = capacity − 6) with capacity floored at 14 — at richness 0.35–0.40 only 4 partials sound, so
most buds attach to silent parents (Slow Bloom and Bloom Colony both chose "sparse, so the buds stand out";
batch 4 measures richness 0.7 / 0.9). The ghost tap is faint by construction (`VoragoEngine_GhostLevelProbe`,
above): −33 dBFS at its loudest second against a −19 dBFS drone at peak level 1.0.

**Batch 4 (`reauthor_probe_batch4.log`, 15:04–15:37).** Slow Bloom 200=0.7 → 1.7414 (from 0.0000), 200=0.9 →
0.4708 (non-monotone: the bloom's six children scale with their parents and the descriptor); Glass Sphere
1002=0.995, 1001=0.01, 201=1.0, 1105=0.05 → 3.9712; Stone Gravity 1105=0.2 + 600=0.375 → 3.0192, + 1003=0.3 →
3.0527; Weighted Deep 1105=0.2 → 3.5562 (from 2.78); Crowded Dark 200=0.6 → 2.1494, 1105=0.2 → 3.4623;
Erosion 1105=0.2 → 0.9929 (from 2.45: Age works through the cavern); Drifting Strata 1105=0.2 → 2.3794;
Monolith 1105=0.2 → 3.2511 (from 2.09); Fogbound 1105=0.9 → 1.7019 (from 1.41).

**Batch 5 (`reauthor_probe_batch5.log`, 15:05–15:36).** Teeming 109=1.0 + 900=0.0 → 0.5896, 900=0.2 → 0.6252
(the Life macro's whole travel moves this preset by < 0.7); Colony Pulse 301=0.2 → 2.6117, + 901=1.0, 902=1.0
→ 3.8908 (the noise wake base at 0.8 was masking the ecosystem's wake lane); Hull Ark 1002=0.95, 1001=0.10 →
0.3821; Cavern Wall 1002=0.95, 1001=0.05 → 1.2459; Strung Abyss 1002=0.99, 1001=0.02 → 2.1003; Feedback Mire
1105=0.2 → 1.7579, + 501=1.0 → 1.7563; Sudden Chasm as authored: W_end 160.0 s, d_att 1.2451, d_Sus 3.3268,
P reaches RMS(Sus) − 6 dB at 4 s, P_rev at **6 s** — the registered envelope (stage times 20 / 30 / 45 / 60 s,
levels 1.0 / 0.8 / 0.92 / 0.85) is at full level after 20 s, so the C-6 "attack span" of 155 s is a
stage-time sum, not an audible attack, and the [0, 160 s] attack window compares 150 s of near-identical
sustain; Glass Sphere + 200=0.9 → 2.6870.

**Reading at this point.** Authoring reaches or nearly reaches the bar for S2, S3, D1.5, the four D3 cells,
S7 (3.89), D1.11 (3.97), M8 (3.56), M3 (3.46), M12 (3.25) — these continue. The bloom (S6 1.74 at best, E1),
the ghost (S9 0.22, E5), the Life macro (M10 0.63 at best), the attack window as defined (D9.1 1.25 against
an attributability floor of 4.83), and the wood / cavern-wall / string materials against StoneChamber (D1.8
0.38, D1.10 1.25, D1.2 2.10 even at resonance 0.99) do not respond to any preset lever — the feature's own
audible range sits under F. Surfaced for a ruling after the route arms are measured (batch 6).

**Rulings after batch 6 (2026-09-30 ~16:30; canonical record spec.md Clarifications "Re-author loop rulings").** S-7 ghost make-up: measured before ruling (below). S-8: E1 0.0568 (route arm, attribBase 0.0568), E3 0.6496 (attribBase 0.0760), E4 1.2203 (0.4488), E5 0.4160 (0.3296) - `reauthor_probe_batch6.log`; M10 0.6252, S6 1.7414 - batches 5 / 4; recorded UNMET at these ceilings, presets keep their claims. S-9: `audibleAttackSeconds` in `preset_test_support.h`, used by `revertedAttackSpanSeconds` / `attackWindowEndSeconds`; Sudden Chasm and Growth Ring re-probed on it (below). S-10: batch 7 on the nine remaining, then sweep 3.

**S-7 measurement, engine level (`artifacts/ghost_tap_makeup_probe.log`, 16:33–16:34).** `VoragoEngine_GhostLevelProbe` with `setGhostTapMakeupDb` on the default surface, ghost peak level 1.0, sustain 220–340 s at 8 kHz (the three lines after the 0 / 0.5 / 1.0 references are +6 / +12 / +18 dB): mix RMS −18.91 / −18.11 / −16.31 dBFS; the ghost alone −32.46 / −26.27 / −20.27 dBFS; its loudest second −26.89 / **−20.75** / −15.02 dBFS against the −19.19 dBFS drone; seconds above −40 dBFS 111 / 120 / 120 of 120. Candidate **+12 dB**: the ghost's loudest second sits at the drone's level (the S-1 sizing rule, "within 6 dB of the drone") and it sounds in every second; +18 dB puts the ghost above the drone and lifts the mix by 3 dB. Set as `kGhostTapMakeupDb` / `kGhostTapMakeupGain` for the pilot-probe measurement of S9 (Choir of Absence) and E5 (Haunted Colony); ruled on those numbers below.

**Batch 6 (`reauthor_probe_batch6.log`, 15:38–16:23; route arms through the probe's new plan 6.9 arms).**
E1 Bloom Colony d(R_k, R_k0) 0.0568, attribBase 0.0568; E3 Swarm Breath 0.6496 / 0.0760; E4 Feeding Loops
1.2203 / 0.4488; E5 Haunted Colony 0.4160 / 0.3296 (the four route primaries as authored — ruling S-8, E5 under
S-7). Colony Pulse 301=0.1 + 901=1.0 + 902=1.0 → 3.8983; + 109=1.0 (wake 0.2) → 3.0948; Glass Sphere 1002=0.995,
1001=0.005, 201=1.0, 1105=0.0 → 3.9756; Weighted Deep 1105=0.2 + 200=0.8 → **4.8480 PASS**; Crowded Dark 1105=0.2
+ 200=0.5 → 3.1426; Monolith 1105=0.1 → 3.4589; Stone Gravity 1105=0.1, 401=1.0, 600=0.375 → 2.9780; Feedback
Mire 501=1.0, 1105=0.2, 200=0.3 → 1.7926; Cavern Wall 1002=0.99, 1001=0.01, 201=1.0 → **10.0563 PASS**; Strung
Abyss 1002=0.99, 1001=0.02, 201=1.0, 1105=0.05 → **15.8845 PASS**; Hull Ark 1002=0.99, 1001=0.01, 201=1.0 → 2.1773.

**Batch 7 (`reauthor_probe_batch7.log`, 16:30–16:58; ruling S-10's round).** Glass Sphere + 203=0.0 → 1.5154
(the 0.02 inharmonicity carries the sphere), + 200=0.5 → **4.0349 PASS**; Hull Ark + 1105=0.05, 206=0.5 → 3.4242,
+ 200=0.8 → **6.7306 PASS**; Crowded Dark 1105=0.2 + 201=1.0 → **4.1669 PASS**; Monolith 1105=0.1 + 201=1.0 →
2.6082; Stone Gravity 1105=0.1, 201=1.0, 600=0.375 → 3.1563; Erosion 201=1.0 → 2.4958; Drifting Strata 1105=0.2 +
201=1.0 → 3.9656; Feedback Mire 1105=0.2, 201=1.0, 200=0.3 → 1.3184; Fogbound 1105=0.9 + 201=1.0 → 0.9405; Colony
Pulse 301=0.1, 901=1.0, 902=1.0, 201=1.0 → **12.0247 PASS** (adopted with E6.hi / E7.hi added to its secondaries).
Adopted from batches 6–7: Cavern Wall, Strung Abyss, Weighted Deep, Glass Sphere, Hull Ark, Crowded Dark, Colony
Pulse — the recurring recipe is a cloud tilt of +8 dB (the descriptor reads the upper spectrum the twin moves),
a nearly undamped body for the materials, and the cavern mix at or under 0.2.

**Batch 8 (`reauthor_probe_batch8.log`, 17:01–17:23; vorago_tests rebuilt with `kGhostTapMakeupDb` = 12 and the S-9 window).** S-7 at the preset level: Choir of Absence S9 0.2229 → **1.8349**; Haunted Colony E5 route arm 0.4160 → **1.1920** (attribBase 0.3296) — the +12 dB tap clears the secondary bar for S9, not F. S-9: Sudden Chasm W_end 25.0 s, d_att 2.7661 (from 1.2451), d_Sus 4.3038, attributability floor 5.8038 → still FAIL (P reaches RMS(Sus) − 6 dB at 4 s, P_rev at 6 s; the two Sus windows now start 11 s apart, which is what d_Sus measures); Growth Ring W_end 65.0 s, d_att 8.1714, d_Sus 2.4636 → **PASS** on the audible-attack rule (P reaches level at 30 s, P_rev at 4 s). Colony Pulse as re-authored 13.3188 PASS. Candidates: Drifting Strata + 1502=1.0 → 3.0004 (3.97 without stays the best); Monolith 1105=0.2 + 201=0.0 → 3.0795; Stone Gravity 1105=0.2, 600=0.375, 201=0.0 → 2.8372; Erosion 1105=0.6 → 1.7981; Feedback Mire 1105=0.0, 500=1.0, 501=1.0 → **8.4463 PASS** (adopted). Under F after the S-10 round, recorded at their best reading with the lever adopted: M4 Drifting Strata 3.97 (space 0.2, tilt +8), M12 Monolith 3.46 (space 0.1), M5 Stone Gravity 3.52 (sub −6 dB), M2 Erosion 2.45 (as authored), M9 Fogbound 1.70 (space 0.9), D9.1 Sudden Chasm 2.77.

**S-7 ruled (17:25): +12 dB ships** (FR-077b / SC-033b); S9 1.83 and E5 1.19 recorded UNMET under S-8. Presets regenerated (42, `generate_vorago_presets`, 17:24). Final-tree chain launched 17:26: dsp_systems + vorago_tests rebuilt; dsp_systems per-push; `VoragoEngine_GhostExtensionWiring` twice for the SEVENTH harvest; vorago per-push; then sweep 3.

**Final-tree per-push (17:27–17:45, `artifacts/final3_dsp_systems_perpush.log`, `final3_vorago_perpush.log`).** dsp_systems 1414 of 1415 — the one red is `VoragoEngine_GhostExtensionWiring` clause (a) against the sixth-harvest constant (worst metric 0.129103, checkpoint[12] 0.259 vs 0.190: the +12 dB tap in the default render), the expected re-pin; vorago "All tests passed (4024759 assertions in 118 test cases)" on the re-authored 42 with E6.hi / E7.hi claimed by Colony Pulse. **Seventh ghost-fingerprint harvest** (`artifacts/ghost_fingerprint_harvest7_run{1,2}.log`, literals byte-identical, md5 4805f945856d17ad52f15b0a41d75f4e) pasted into `atmosphere_ghost_fixtures.h` with a SEVENTH PROVENANCE block (sixth marked superseded); verifying rebuild + re-run below. Sweep 3 launched 17:46 on this binary (`/f/tmp/p14/sweep3_*`, four shards, `sweep3_wall.txt`).
Seventh harvest verified: rebuild + `VoragoEngine_GhostExtensionWiring` "All tests passed (27 assertions in 1 test case)" (`artifacts/ghost_fingerprint_harvest7_verify.log`, 17:48). Vorago plugin rebuilt on the final tree (0 warnings) and pluginval strictness 5 completed (`artifacts/final3_pluginval.log`, 17:49).
Regression on the final tree, remaining suites (`artifacts/final3_other_suites_summary.log`): dsp_effects 495 / 495, seraphis 109 / 109, shared 460 / 460 (dsp_processors 3311 / 3311 on the FR-077a fix, `artifacts/resetfix_dsp_processors_systems.log`). Portability (`artifacts/final3_portability.log`): the first run caught one g++-only error in `NoiseOrganism_TypeSwitchedAfterPrepareReplaysAfterReset` (a constexpr local odr-used by reference inside a lambda), fixed with a by-value copy; second run 30 / 30 OK, `wsl --shutdown` after each run. clang-tidy dsp 22 / vorago 88 warnings on the first pass (all in test TUs: explicit pointee resets in the hidden probes; designated initializers, braced returns, string appends, one sign comparison, two implicit widenings, one nested max, one move in the Phase 14 preset tests) — all fixed; dsp_systems rebuilt and the three sentinels re-run green (`artifacts/final3_sentinels_after_tidy_fixes.log`: 41 assertions in 3 cases); second tidy pass recorded below.
Second clang-tidy pass (`artifacts/final3_tidy_dsp.log`, `final3_tidy_vorago.log`, 18:05–18:18): dsp 372 files, **0 errors / 0 warnings**; vorago 57 files, **0 errors / 0 warnings**. The vorago_tests MSVC rebuild on the tidy-fixed TUs and its per-push re-run follow sweep 3 (the sweep holds the binary).

## Sweep 3 (2026-09-30 17:46–22:18, the final-tree confirming run of ruling S-10) — `artifacts/sweep3_*.log`

**Run.** `sweep3_wall.txt`: shards 17:46:15 → 22:08:46 (shard 0 exit 10, 1 exit 15, 2 exit 11, 3 exit 8), aggregate →
22:18:03. `sweep3_aggregate.log`: "Required primaries from the measured set: 42; from the recorded constant: 42;
verified primaries: **28**" (sweep 2: 14; sweep 1: 17); "Pairs 861: min d 1.4617 (Colony Pulse vs Drifting Strata)
median d 8.5773 max d 30.6176 … pairs below floor: **46**" (sweep 2: 99), t_max 1.7013, K 4.

**Primaries under F, with the sweep-3 reading (the S-8 record):** S6 Slow Bloom 0.0000 (the sweep ran on richness
0.35; the 0.7 lever that read 1.74 is adopted after the sweep — see "Repairs"), S9 Choir of Absence 1.8349 (S-7),
M2 Erosion 2.4545, M3 Crowded Dark 3.9226 (probe 4.17 before the ghost make-up), M4 Drifting Strata 3.0007,
M5 Stone Gravity 3.5289, M9 Fogbound 2.2748, M10 Teeming 0.3794, M12 Monolith 3.3670, E1 Bloom Colony 0.0568,
E3 Swarm Breath 0.6496, E4 Feeding Loops 1.2203, E5 Haunted Colony 1.1920 (S-7), D9.1 Sudden Chasm 2.7661 (S-9
window). Coverage: E7.hi has no verified secondary (Colony Pulse claims it at 902 = 1.0; its ExtReversion twin
reads under 1.5 — recorded with the ecosystem-limited cells), D4.7 / D4.8 / D4.9 had no verifier (Swarm Breath's
S1 conjunct 0.65 < 1.5 — repaired below), and the UNMET primaries above are their cells' only claimants
(D9.1, E1, M2, M3, M4, M5, M9, M10, M12).

**Two level-arm defects the probe could not see** (the probe printed d only): Feedback Mire rendered SILENCE
(`sweep3_shard_3.log`: peak 0.0004, hi −84 dB, lo −103 dB [NO] on all four takes) — ecology mix 1.0 removed the
dry path and the loops are wake-gated, so the probe's 8.45 was "P silent vs a loud twin"; Resonant Shaft sat at
the limiter (`sweep3_shard_1.log`: peak 0.9661, hi −3.90…−4.81 dB [NO] on all four takes) once the cavern dropped
to 0.2; Stone Gravity still at the limiter at sub −6 dB (`sweep3_shard_2.log`: take 0 late-sustain +15.67 dB,
take 3 hi −5.92 dB; 44.1 kHz hi −5.97). The pilot probe now prints the stored take's four arms so a candidate
is checked for level as well as distance.

**Repairs after sweep 3 (defs):** Feedback Mire back to ecology mix 0.95 / loop gain 0.95, cavern 0.2 (best honest
S4 1.76); Slow Bloom richness 0.70 (1.74); Blue / Violet / Grey moved onto presets whose S1 verifies — Wind
Through Basalt slot 3 Direct + Blue (D4.7; its D3.2 claim goes, Abyssal Wind holds D3.2), Fogbound slot 3 Direct +
Violet (D4.8), Dead Air slot 3 Grey (D4.9; Brown stays verified on Erosion); Resonant Shaft and Stone Gravity
level candidates measured with the arm printout below.

**Pause (2026-09-30 22:30).** The user ruled on the sweep-3 picture: no feature ships inaudible, no preset ships
generic; "record UNMET at the ceiling" (S-8, and the S-7 / S-9 residues) is withdrawn as a release outcome and
kept only as the measurement record. Phase 13c (capability audibility) is inserted before this phase resumes
at T048; its premise is this record's numbers (roadmap entry 2026-09-30). Sweep 4 on the sweep-3 repairs runs
overnight as the record of the current tree.

**T049 — the pinned CPU lane after sweep 3 (`artifacts/t049_cpu_arm.log`, `node tools/run-cpu-tests.js vorago_tests`, 22:34–22:40 after 15 idle minutes).** 1 654 454 of 1 654 455 assertions; every CPU clause passed (SC-014 arm P 2.935e6 / arm D 2.920e6 ns per 512-sample block at poly 4, P/D 1.005 against 1.05). The one red is not a budget: `Vorago_ParameterStepsAreContinuous` control (b) (Phase 12 SC-011, [long]) read max(test) 0.00226 vs max(ref) 0.00154, ratio 1.47 against the 1.5 bound, and reported itself broken — the +30 dB noise bed (FR-077) raised the default surface's own sample-to-sample deltas until the control's 1/64-range master-gain snaps no longer stood out. Fix (continuity_test.cpp `renderSweep(..., control)`): control (b) snaps master gain between 0.25 and 1.0 at every step — a discontinuity no bed hides — with the criterion and bound unchanged; re-run recorded below. Every measured ID's clause-3 verdict was unaffected: the same log's table reads 110 PASS rows and "failing IDs (hand to T048): none".
Continuity re-run on the recalibrated control (`artifacts/continuity_control_b_recalibrated.log`, 22:43–22:47): control (a) injected 0.00335 > bound 0.00231 (OK); control (b) max(test) 0.125581 vs max(ref) 0.00143, ratio 87.56 → fails clause 3 (OK); "All tests passed (1215119 assertions in 1 test case)".

**Batch 9 (`reauthor_probe_batch9.log`, 22:43–22:59, arms printed).** Feedback Mire as repaired: peak 0.41, arms all yes, S4 1.7072; Slow Bloom richness 0.7: arms yes, S6 1.4453; Wind Through Basalt with slot 3 Direct + Blue: arms yes, S1 5.2031 PASS; Dead Air with slot 3 Grey: arms yes, D3.1 8.6421 PASS; Fogbound with slot 3 Violet: arms yes, M9 1.8229. Resonant Shaft 401=0.8, 200=0.4 → peak 0.9661, hi −5.11 [NO], late-sus +8.27, d 3.8228; 401=0.85, 200=0.45, 1105=0.3 → hi −5.22 [NO], d 4.0491 — the level, not the timbre, is the problem (a master-gain trim is measured next). Stone Gravity 600=0.25, 401=0.7 → hi −6.42 yes but late-sustain +15.40 [NO] (the sound grows 15 dB across the sustain); + 200=0.4 → +15.49 [NO]; the growth, not the sub, is the defect (resonance mix 0.5 and a master trim measured next).

**Batch 10 (`reauthor_probe_batch10.log`, 23:00–23:06).** Resonant Shaft 401=0.85, 200=0.45, 1105=0.3 with master gain 0=0.25 → peak 0.9661 (momentary), hi −9.20 dB yes, lo −25.96 yes, late-sus +8.94 yes, tail yes, S2 **4.1859 PASS** (adopted; 0=0.3 → hi −7.69, 4.1686). Stone Gravity 600=0.25, 401=0.5 → hi −7.21 yes but late-sustain **+14.29 dB [NO]**; with master gain 0.25 → peak 0.61, hi −13.14, late-sustain **+14.38 [NO]**: the sound grows 14 dB across its sustain whatever the level or the resonance mix — a behaviour of the Gravity extreme (octave lock, spectral gravity +0.6), not an authoring level. Left as authored (sub −6 dB) for sweep 4 as a KNOWN level-arm red, handed to Phase 13c with M5's 3.5 reading.

## Sweep 4 (2026-09-30 23:11 → 2026-10-01 03:25) — the record of the tree at the pause, `artifacts/sweep4_*.log`

**Run.** Presets regenerated 23:06 (42), vorago_tests rebuilt (0 warnings), per-push 118 / 118 (`sweep4_vorago_perpush.log`);
shards 23:11:35 → 03:16:20, aggregate → 03:25:40. "verified primaries: **27**" (sweep 3's 28 included Feedback
Mire's silent-render pass; honest now at 1.7072); "pairs below floor: **51**" (min d 1.4617 Colony Pulse vs
Drifting Strata; median 8.63; max 30.62; t_max 1.7013, K 4).

**Primaries under F (the Phase 13c input, with the sweep-4 reading):** S4 Feedback Mire 1.7072, S6 Slow Bloom
1.4453, S9 Choir of Absence 1.8349, M2 Erosion 2.4545, M3 Crowded Dark 3.9226, M4 Drifting Strata 3.0007, M5 Stone
Gravity 3.5289, M9 Fogbound 1.8229, M10 Teeming 0.3794, M12 Monolith 3.3670, E1 Bloom Colony 0.0568, E3 Swarm
Breath 0.6496, E4 Feeding Loops 1.2203, E5 Haunted Colony 1.1920, D9.1 Sudden Chasm 2.7661. The 27 verified: S1,
S2, S3, S5, S7, S8, S10, M1, M6, M7, M11, E2, D1.1–D1.5, D1.8–D1.11, D3.1–D3.4, D8.2.

**Coverage.** D4.7 Blue (Wind Through Basalt), D4.8 Violet (Fogbound), D4.9 Grey (Dead Air) now have verifiers;
E7.hi still has none (Colony Pulse's ExtReversion twin under 1.5 — an ecosystem-limited cell for 13c); the
UNMET primaries' cells have no other claimant (D9.1, E1, M2, M3, M4, M5, M9, M10, M12), as do D13.x / D14.x
(claimed only as secondaries on presets whose conjunct fails).

**Level arms.** Stone Gravity as known (late-sustain +15.7 dB on take 0, hi −5.92 on take 3, 44.1 kHz hi −5.97);
Resonant Shaft take 1 (seed 4) hi −5.59 dB [NO] — the master-gain trim measured on the stored seed (hi −9.2)
leaves one of the four seeds at the limiter; a −2 dB deeper trim (master gain 0.2) is the resume-time fix.
Feedback Mire now sounds (peak 0.41). Every other preset's four arms green on all four takes.

**Reading.** Every remaining failure is a cell whose feature's audible range sits under F (the 13c list above) or
a direct consequence of one (the 51 pairs are all among those presets, plus the Gravity extreme's self-growth),
except the Resonant Shaft seed-4 trim. The phase pauses here; Phase 13c takes the table.

## G2 re-run on the sweep-5 defs (main loop, 2026-10-06 15:00–15:38)

`VORAGO_SWEEP_THREADS=4 vorago_tests.exe "Vorago_PresetPilot_Calibrate"` on the sweep-5 binary (built 14:59, eight sweep
shards and the preset lane sharing the box — a render case, not a timing case), `artifacts/sweep5_pilot_calibrate.log`:

| preset | K = 1 | K = 2 | K = 4 | K = 8 | log line |
|---|---|---|---|---|---|
| P0 default surface | 1.1895 yes | 1.5869 yes | 1.1334 yes | 1.5285 yes | :15 |
| P1 Tectonic Floor | 1.0118 yes | 1.4569 yes | 0.4789 yes | 0.4080 yes | :16 |
| P2 Cathedral Void | 1.9119 yes | 0.7445 yes | 0.8057 yes | 0.4413 yes | :17 |
| P3 Growth Ring | 1.2664 yes | 2.6160 no | 0.5831 yes | 0.3032 yes | :18 |
| P4 Glass Well | 3.0451 no | 2.3836 no | 3.8071 no | 2.4264 no | :19 |

`ruled K = NONE` (:21); `G2: STOP (no K <= 8 gives 2*t_K <= 4.0 for every P0-P6 (FR-017a))` (:24); wall 2274.3 s (:23).
Identical to the 13c reading (`specs/vorago-phase13c-capability-audibility/artifacts/default_after_p0.log:15-24`).

**Premise probes before the ruling** (`Vorago_PresetPilot_PrimaryProbe`, `VORAGO_PILOT_TAKES=4`, `gw_premise_summary.txt`):

| Glass Well | primary d (4-take mean) | per-take d (seeds 12–15) | arms |
|---|---|---|---|
| stored | 6.2513 PASS (`gw_premise_GW_stored.log:9`) | 6.2513 / 10.2000 / 3.4914 / 5.0450 (:15-18) | all green |
| tidal 0 (`1502=0`) | 6.2549 PASS | 6.2549 / 10.2016 / 3.4831 / 5.0434 | all green |
| breathing 0 (`1500=0`) | 5.7456 PASS | 5.7456 / 9.1078 / 3.3099 / 5.3005 | all green |
| both 0 | 5.7405 PASS | 5.7405 / 9.1068 / 3.3039 / 5.2984 | all green |

The life lanes move the per-take d by ≤ 0.6; the seed-to-seed spread (3.5 → 10.2) is the cloud / glass body itself.

**Ruled (user, 2026-10-06; spec Clarifications "Gate G2 re-run ruling"):** K = 4 stays; the STOP is recorded as a
surfaced FR-017a exception for Glass Well; no re-author, F and K unchanged. Sweep 5 at K = 4 is the confirming run.

## Sweep 5 (2026-10-06 14:59 → 18:57, the confirming run after the sweep-5 re-author loop) — `artifacts/sweep5_*.log`

**Run.** `tools/vorago_preset_defs.h` re-authored for eight rows from the `VORAGO_PILOT_OVERRIDE` probe loop (tasks T048
main-loop note 2026-10-06; `reauthor5_*.log`, `reauthor5_batch{1..12}_summary.txt`, `reauthor5_confirm{1..3}_summary.txt`);
presets regenerated 14:59 (`wrote 42 presets`, 8 files changed); `vorago_tests` + `generate_vorago_presets` built with 0
warnings (`f:/tmp/p14/build_sweep5.log`); `[preset]~[long]` "All tests passed (18135 assertions in 41 test cases)" and
`Vorago_FactoryPresets_TreeMatchesGenerator` "All tests passed (246 assertions in 1 test case)"
(`sweep5_vorago_preset_lane.log`). Eight shards `VORAGO_SWEEP_SHARD=i/8`, `VORAGO_SWEEP_THREADS=1` 14:59:49 → 18:42:43,
aggregate → 18:57:51 (`sweep5_wall.txt`). The calibration (`sweep5_pilot_calibrate.log`), the preset lane and the Glass
Well premise probes shared the box for the first 72 min — all render cases, none timing.

**Headline** (`sweep5_aggregate.log:363`): "Required primaries from the measured set: 42; from the recorded constant: 42;
verified primaries: **36**" (13c final sweep 30, sweep 4 27). Failure set (every shard `FAILED:` message, numbers stripped)
is a strict subset of the 13c final sweep's: 35 → 16 lines, 19 cleared, none added.

| preset (cell) | sweep 4 (`f:/tmp/p14/sweep-out` records) | 13c final | **sweep 5** | re-author (defs comment "sweep 5 re-author") |
|---|---|---|---|---|
| Lightless M1 | 4.6574 | 1.783 | **4.4888** | tilt 1.0, richness 1.0, spread 0.85, cavern darkness 0, smear tilt 1.0, ghost / breathing / bloom 0 |
| Resonant Shaft S2 | 3.4732 | 2.685 | **7.2936** | mix 1.0, richness 0.80, bloom depth / spawn 0, breathing 0, master 0.20 |
| Singing Colony E2 | 5.9804 | 3.791 | **4.2721** (attrib 1.28) | mix 1.0, wander 0, bloom / ghost 0, richness 0.80, body 0.35, master 0.40 |
| Swarm Breath E3 | 0.0536 | 1.165 | **4.2219** (attrib 0.98) | bed +6 dB, wake 0, bloom / breathing 0, richness 0.10, 2× events, decay 0.30, space mix 0.10 |
| Haunted Colony E5 | 1.5036 | 3.909 | **6.4119** (attrib 0.88) | cavern darkness 0.50, tilt 0.40, fog 0.30 |
| Fogbound S1 conjunct (M9 host) | 0.0818 (M9 2.1456) | 1.440 | **1.573** (M9 5.0678) | bed +6 dB, ghost 0 — D4.8 / D4.10–12 re-verified |
| Erosion S1 conjunct / D3.3 | 0.0296 (D3.3 not rendered) | 1.440 | **5.6676** | bed +6 dB, ghost 0 — D4.4–6 re-verified; M2 primary 3.2191 stays under F |
| Spore Drift D3.3 | not rendered (S1 0.165) | 1.813 | **4.1033** | breathing / bloom 0, richness 0.30, ecology mix 0.50 |

**Still under F — the engine-limited six (13d hand-over, no settings candidate reached 4.0):** E1 Bloom Colony 1.1823
(2s 2.21, attrib 1.10), E4 Feeding Loops 2.9182 (2× events read 8.48 but silenced the loops on three takes, arms 2–3 red;
1× 3.71 with arm 2 red on every take), M2 Erosion 3.2191, M4 Drifting Strata 0.8745, M5 Stone Gravity 3.1864, M10 Teeming
0.9943 (shard messages, `sweep5_shard_*.log`). Cells with no factory verifier (`sweep5_aggregate.log:186-256`): those six
plus D13.1 / D13.2 / D14.2 / E6.hi / E7.hi (the ruling B-20 secondaries, unchanged).

**Secondaries still red** (10, all in the 13c set): Colony Pulse D13.2 0.1942 / E6.hi 1.3700 / E7.hi 1.3430; Drifting Strata
D14.2 0.3131; Teeming D13.1 0.8560; Spore Drift D11 / D6.2 and Steam Vent D6.3 / D7.2 (state-only cells whose conjunct is a
primary under F or a skipped twin, as in sweeps 4 and 13c).

**Level arms.** Choir of Absence take 3 (seed 13) peak 0.966051, hi −5.5755 dB [NO] — ruling B-18 (trim measured and
declined in 13c). Every other preset's four arms green on all four takes (`CHECK( t.armPass[0] )` appears once across the
eight shard logs).

**Distinctness and subsets (recorded, not gated — ruling 2026-09-30):** `sweep5_aggregate.log:4065-4066` "Pairs 861: min d
0.9547 (Wind Through Basalt vs Swarm Breath, floor 5.7290) median d 8.6876 max d 29.6164", "t_max 2.8645 K 4 … pairs below
floor: 159" (13c 149: Swarm Breath gained 14 below-floor partners once its cavern went to a tenth; Resonant Shaft and Lightless
lost theirs). `:2612` "SUBSET pairs: 51" (13c 50; new: Smeared Horizon vs Fogbound, Fogbound's unclaimed S3 now 1.584).

**G2.** Recorded above ("G2 re-run on the sweep-5 defs"): K = 4 stays, Glass Well's STOP surfaced; Glass Well's own D1.1 reads
6.2513 at K = 4 (`record_28`).

**Reading.** Every change the probe loop predicted landed within 0.01 of its probe reading in the sweep (the probe and the
sweep render the same takes); nothing verified before is unverified now. The six remaining primaries are engine ceilings,
not preset settings — the 13d hand-over. T048 closes here; T049 (preset CPU, alone, pinned) is next.
