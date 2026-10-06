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

## T050 — FR-042 / SC-020 audition table (prepared 2026-10-06; the last two columns are the user's)

Installed by the `Vorago` build's POST_BUILD step to `%PROGRAMDATA%\Krate Audio\Vorago\` (42 presets, 7 categories; the
tree regenerated 2026-10-06 14:59 for sweep 5). Nothing automated substitutes for the audition: the sweep verifies that a
preset's named feature is audible, not that the preset sounds right or sits in its category. Fill "category fit" (yes / move
to <category>) and "reads as a variant of" (another preset's name, or blank); re-filing or re-authoring follows the notes,
then T048's affected shards and the aggregate re-run.

**Interim (2026-10-06 evening, in chat, audition under way, not complete):** the user reports the presets sound majestic and, so far,
all very different from one another; no re-filing or variant note yet. SC-020 stays pending until every row is filled.

| # | preset | category | primary | character note (the def's own line) | category fit | reads as a variant of |
|---|---|---|---|---|---|---|
| 1 | Wind Through Basalt | Textures | S1 Noise | Raw wind and hiss pouring through cracks in black stone over a thin, dark tone. |  |  |
| 2 | Resonant Shaft | Caverns | S2 Resonance | A deep vertical shaft whose walls sing back the played notes in slow, drifting peaks. |  |  |
| 3 | Smeared Horizon | Textures | S3 Smear | A drone dissolved into a wide, blurred haze where every partial bleeds into the next. |  |  |
| 4 | Feedback Mire | Machines | S4 Ecology | Choked feedback loops churning in a sump of rust, each filtered to its own grinding band. |  |  |
| 5 | Tectonic Floor | Abyss | S5 Sub | A fifth below the floor: slow plates of sub grind under a dark, close cloud. |  |  |
| 6 | Slow Bloom | Drones | S6 Bloom | A sparse drone that takes two minutes to open, budding new partials as it breathes. |  |  |
| 7 | Colony Pulse | Organisms | S7 Ecosystem | A restless colony of small voices, quickening and feeding on each other in the dark. |  |  |
| 8 | Cathedral Void | Caverns | S8 Cavern | A vast, slow nave around a wide cloud. Engage Freeze once the drone has bloomed to hold the space. |  |  |
| 9 | Choir of Absence | Ghosts | S9 Ghost | Voices that are not there: reversed fragments of the drone surfacing and sinking away. |  |  |
| 10 | Hull Resonance | Machines | S10 Body | The inside of a vast steel and stone hull, every plate ringing under the drone. |  |  |
| 11 | Lightless | Abyss | M1 Darkness | A bright cloud pressed down into lightless black, every overtone smothered as it sinks. |  |  |
| 12 | Erosion | Textures | M2 Age | Weathered stone crumbling to dust: crackle and grit wearing a ringing drone down to a dull husk. |  |  |
| 13 | Crowded Dark | Drones | M3 Density | A thin drone that fills to bursting, partials and embers packing the dark shoulder to shoulder. |  |  |
| 14 | Drifting Strata | Drones | M4 Movement | Layers of drone sliding over one another like slow geological strata under a rolling tide. |  |  |
| 15 | Stone Gravity | Abyss | M5 Gravity | Resonant peaks dragged down and locked to octaves, heavy as stone settling in the deep. |  |  |
| 16 | Entropic Hum | Machines | M6 Entropy | A clean machine hum slowly coming apart, its partials skewing and fraying into noise. |  |  |
| 17 | Pressure Front | Machines | M7 Pressure | A compressed wall of pressure: loops churning and the floor pulled tight into a dense, saturated hum. |  |  |
| 18 | Weighted Deep | Abyss | M8 Weight | Two octaves under the drone a sub swells up, pulling the whole cavern down with it. |  |  |
| 19 | Fogbound | Ghosts | M9 Fog | Static and rumble drifting through a thick fog, the drone heard only as a ghost of itself. |  |  |
| 20 | Teeming | Organisms | M10 Life | A dense colony teeming in the dark, many small lives waking slowly and feeding on the drone. |  |  |
| 21 | Endless Descent | Caverns | M11 Depth | A drone falling into a cavern that keeps opening beneath it, each echo farther down than the last. |  |  |
| 22 | Monolith | Drones | M12 Mass | A single vast mass of sound, body and sub fused into one unmoving block of stone. |  |  |
| 23 | Bloom Colony | Organisms | E1 PartialBloom | A sparse drone where a hidden colony swells new partials into bloom and lets them wither. |  |  |
| 24 | Singing Colony | Organisms | E2 ResonatorPeaks | Free-floating resonant peaks that a colony wakes into song, one voice rising as another fades. |  |  |
| 25 | Swarm Breath | Textures | E3 NoiseWake | Hiss and glassy air stirred by a swarm, flaring and settling as the colony breathes. |  |  |
| 26 | Feeding Loops | Machines | E4 FeedbackLoopWake | Feedback loops that wake and feed on one another, flaring into a grinding chorus and sinking back. |  |  |
| 27 | Haunted Colony | Ghosts | E5 GhostBursts | A dark room where a colony calls up ghosts of the drone in sudden, blurred bursts. |  |  |
| 28 | Growth Ring | Organisms | D8 Growth | One slow organic growth, a minute long, that keeps budding new partials as it breathes. |  |  |
| 29 | Glass Well | Caverns | D1 Glass | A ringing glass shaft: a bright, barely damped body singing into a pale cavern. |  |  |
| 30 | Strung Abyss | Drones | D1 Strings | Vast slack strings stretched across a chasm, humming a low, dark, sustained chord. |  |  |
| 31 | Iron Plate | Machines | D1 MetalPlate | A great iron plate struck by the drone, ringing with clanging, inharmonic overtones. |  |  |
| 32 | Chamber Drone | Drones | D1 Chamber | A warm, closed wooden chamber filled by one breathing, slowly blooming drone. |  |  |
| 33 | Ice Shelf | Textures | D1 Ice | A glittering shelf of ice, bright shards of tone cracking and shimmering in the cold. |  |  |
| 34 | Hull Ark | Drones | D1 WoodenHull | The creaking wooden hull of an ark rolling on a dark sea, its timbers groaning with the drone. |  |  |
| 35 | Column Hymn | Caverns | D1 CathedralColumn | Tall stone columns in a sunken cathedral, each one singing its own grave, sustained hymn. |  |  |
| 36 | Cavern Wall | Caverns | D1 CavernWall | Rough, wet rock walls close around a heavy drone, thudding back a dull, massive resonance. |  |  |
| 37 | Glass Sphere | Ghosts | D1 GlassSphere | A hollow sphere of glass drifting in fog, ringing faintly with the ghost of a far-off drone. |  |  |
| 38 | Sudden Chasm | Abyss | D9 FastAttack | The ground gives way at once: a dark, full drone that opens beneath you in seconds. |  |  |
| 39 | Dead Air | Ghosts | D3 Direct | A dead channel hissing in an empty room, tape hiss and static where a voice should be. |  |  |
| 40 | Abyssal Wind | Abyss | D3 FilteredWind | Wind howling up out of a bottomless pit over a deep sub and slowly wandering peaks. |  |  |
| 41 | Spore Drift | Organisms | D3 GranularDust | Clouds of spores sifting through the dark, their grains drifting backwards into humming loops. |  |  |
| 42 | Steam Vent | Machines | D3 MetallicHiss | Metallic steam hissing from rusted vents over a grinding deep sub and whistling loops. |  |  |

## Close records T051–T060 (main loop, 2026-10-06, during the T049 lane wait)

**T051 — `plugins/vorago/docs/index.html` (FR-062).** Created on the Seraphis page's structure (`plugins/seraphis/docs/index.html`
head / downloads / overview / features / presets / installation / footer; `assets/style.css` copied verbatim): what Vorago is,
the twelve macros by name, the ecosystem view and the two rule knobs (Ecosystem Sync, Self Affinity), the seven categories one
line each, the Freeze gesture, system requirements, `https://krateaudio.com/vorago/` and the `{{VERSION}}` placeholder that
`docs.yml` fills from `version.json` (C-11, no `docs.yml` edit). `docs/.gitkeep` removed. Verify: the page has 2 relative
`src`/`href` references (`assets/style.css`, `../`), both resolve (node walk, 0 missing).

**T052 — `plugins/vorago/CLAUDE.md` (FR-064 note).** Edited: ecosystem band row (900 / 901 / 902), "110 registered IDs, 108
persisted" (`tests/unit/param_table_expected.h:64` `kNumExpectedParams = 110`; 4 and 5 never written), VP 33
(`src/parameters/param_routes.h:283` `countRoute(Route::VP) == 33`), the state table with the v3 row (`src/plugin_ids.h:24`
`kCurrentStateVersion = 3`, `:52-54` `kStateV3Bytes = 436`), load rule `> 3`, binding 108 IDs (`resources/editor.uidesc`
108 distinct `control-tag`s; 901 / 902 at `:112-113`), page 6 r0 four knobs (`:391`, `:393`), the seven fixed categories
(`src/preset/vorago_preset_config.h:32-33`), the generator / `generate_vorago_presets` / tree test / determinism check, the
sweep lane and `[vorago-sweep]`, FR-061 cited once, the 1.0.0 controller-interface freeze under decision 1.

**T053 — `tools/check-preset-generator-determinism.js --plugin` (FR-024, SC-007).** Test first: before the edit
`--plugin vorago` failed with "unrecognized argument '--plugin'" (the parser rejects unknown flags, `parseArgs`). Implemented:
`GENERATORS = {seraphis, vorago}`, `DEFAULT_PLUGIN = 'seraphis'`, `defaultBinaries(plugin)` (the three resolution paths per
generator), the not-found message names the generator and its CMake target, temp prefix `${plugin}-presets-`, USAGE updated,
unknown name → "unknown plugin 'nope' (known: seraphis, vorago)". The no-flag path resolves the same three Seraphis binaries in
the same order. SC-007's two runs are recorded under T061 (the generator may not share the T049 timing lane).

**T054 — rosters (FR-063, SC-021).** `.claude/workflows/release-readiness.js` `PLUGIN_MAP` gained
`vorago: { testTarget: 'vorago_tests', bundle: 'Vorago.vst3' }`; `.claude/skills/release/SKILL.md` gained `vorago` in the
Inputs list and the target/bundle table. `node tools/lint-plugin-roster.js` → "OK — 8 plugins present in every roster
(disrumpo, gradus, innexus, iterum, membrum, ruinae, seraphis, vorago)", exit 0.

**T055 — `ci.yml` nightly filters (FR-066, SC-023).** `grep -n "FILTER=" .github/workflows/ci.yml`: `:369`, `:655`, `:1116`
now `FILTER='[long]~[vorago-sweep]'`; `:374`, `:660`, `:1121` unchanged (`~[performance]~[perf]~[benchmark]~[!benchmark]~[long]`).
`actionlint` is not on PATH (recorded, not run).

**T056 — `long-tests-nightly.yml` (FR-066, §5.10).** Jobs `vorago-sweep` (matrix `os: [windows-2022, macos-latest,
ubuntu-latest]` × `shard: [0..9]`, `fail-fast: false`, `timeout-minutes: 180`, `needs: check-activity`, gated on
`should_run == 'true'`; checkout, Linux apt packages, FetchContent cache, the leg's configure line minus the AU / ccache options,
`--target vorago_tests` only, `VORAGO_SWEEP_SHARD=<shard>/10 VORAGO_SWEEP_OUT=sweep-out vorago_tests "[vorago-sweep]~[vorago-aggregate]" -d yes`,
upload `vorago-sweep-<os>-<shard>`) and `vorago-sweep-aggregate` (`needs: [check-activity, vorago-sweep]`,
`if: ${{ !cancelled() && … should_run == 'true' }}`, per OS, download `vorago-sweep-<os>-*` merge-multiple into `sweep-in`,
`VORAGO_SWEEP_IN=sweep-in vorago_tests "[vorago-aggregate]" -d yes`). Verify: job names unique (`check-activity`, `long-tests`,
`vorago-sweep`, `vorago-sweep-aggregate`), both `timeout-minutes: 180`; `actionlint` and a YAML parser are not available on
this machine, so well-formedness rests on the structural greps until the first dispatch (T057).

**T057 — SC-013 / SC-023 on the runners.** Needs a push and a dispatch of `long-tests-nightly.yml`: **pending the user's
permission** (asked at the close); nothing estimated.

**T058 — 1.0.0 (FR-064).** `plugins/vorago/version.json` `"version": "1.0.0"`; `plugins/vorago/CHANGELOG.md` `## [1.0.0] -
2026-10-06` above `[0.2.0]` (the library, the two rule knobs, state v3, the tail report, the Freeze note, the 13b / 13c engine
retunes). `node tools/check-changelog-coverage.js vorago` → exit 0, 5 plugin commits + 2 shared-dsp commits surfaced and
reconciled against the 8 bullets (the two shared-dsp commits are Seraphis perf-gate test fixes, not Vorago-visible).

**T059 — install path, freeze load path, `dsp/` bound, roster evidence.**
- SC-032: `plugins/vorago/CMakeLists.txt:123` `krate_plugin_install_presets(${PLUGIN_NAME})`; `plugins/shared/src/platform/preset_paths.h:25-27`
  (macOS `/Library/Application Support/Krate Audio/{pluginName}`, Linux `/usr/share/krate-audio/{pluginName}`,
  `getFactoryPresetDirectory`); `plugins/vorago/installers/windows/setup.iss:66-68` `Source: "presets\*"; DestDir:
  "{commonappdata}\Krate Audio\Vorago"`; `plugins/vorago/installers/linux/README.txt:29-44` (user and system-wide copy).
  The `%PROGRAMDATA%` listing is taken after T061's `Vorago` build (POST_BUILD copies the tree) and recorded there.
- SC-024: `git diff 339cd501..HEAD -- dsp/include/krate/dsp/effects/cavern_verb.h dsp/include/krate/dsp/effects/aether_reverb.h
  plugins/vorago/src/parameters/space_params.h` → 0 bytes. `git diff 339cd501..HEAD -- plugins/vorago/src/processor/processor.cpp`
  hunks: `@@ -13` / `@@ -28` (includes), `@@ -561,8 +563,8` (the state comment: v3, FR-072), `@@ -591` / `@@ -623` /
  `@@ -633` (`setState` v3 → FR-072), `@@ -657,9 +678,38` (`getState` v3 → FR-072 and `getTailSamples` at `:689` → FR-060),
  `@@ -859,6 +909,9` (`pushVoiceParams`: `ecosystemSyncRate` / `ecosystemSelfAffinity` → FR-071a). No hunk in
  `pushCavernParams` or at the `loadSpaceParams` call.
- FR-076: `git diff --stat 339cd501..HEAD -- dsp/include` lists seven headers, attributed by `git log --name-only`:
  Phase 14's own commit `64f57e1a` touched `vorago_engine.h` (+46/−2), `vorago_voice.h` (+33/−1) — the R-1 bound — **plus**
  `noise_generator.h` (+19) and `noise_organism.h` (+10), the ruled FR-077 / FR-077a changes (sweep rulings S-1 / S-6,
  2026-09-30: noise-bus make-up, `snapLevelSmoothers`); the 13c commit `91a40879` touched `bloom_engine.h`,
  `feedback_ecology.h`, `vorago_engine.h`, `vorago_macro_matrix.h`, `vorago_voice.h` under its own rulings B-1..B-20.
  So FR-076's "exactly two headers, added lines only" holds for the R-1 edit itself and is exceeded by the three ruled
  widenings (FR-077, FR-077a, Phase 13c), each recorded where it was ruled — stated here, not re-attributed.
- SC-025 / SC-026: `specs/vorago-phase13b-ecosystem-audibility/artifacts/final2_table_default.log:52` `syncRate [0, 0.5] … 2.1606
  0.534 0.871 counted .hi`, `:75` `selfAffinity [-2, 2] … 0.0257 0.006 1.386 INAUDIBLE .lo,.hi`; `final2_table_lifemax.log:59`
  `syncRate … 2.1446 0.528 0.965 counted .hi`, `:50` `selfAffinity … 0.0803 0.020 1.429 INAUDIBLE .lo,.hi`. The roster
  `R` = {syncRate, selfAffinity} was ratified 2026-09-29 (spec Clarifications; docs commit `7a5198ed` 2026-09-29) before the
  first commit adding 901 / 902 (`64f57e1a`, 2026-10-01).
- FR-061: Clarification Q8 (2026-09-29); `Vorago_Ghost_TriggersAddToDensityScheduler` (`tests/unit/ghost_triggers_additive_test.cpp:78`)
  runs in the per-push lane — its passing line is cited under T061.

**T060 — CMake registration audit (FR-027a).** `plugins/vorago/tests/CMakeLists.txt:52-62`: the probe TU plus exactly the ten
T003 TUs under the Phase 14 comment (`ecosystem_rule_probe_test`, `ecosystem_roster_test`, `state_v3_test`, `tail_samples_test`,
`ghost_triggers_additive_test`, `preset/factory_preset_test`, `preset_sweep_test`, `preset_matrix_test`, `preset_pilot_test`,
`preset_load_rt_test`, `preset_cpu_test`), every one on disk; `git ls-files plugins/vorago/tests` holds no unregistered `.cpp`
(the only diff against the registered set is `vstgui_test_stubs.cpp`, registered at `:79` outside the `unit/`/`integration/`
pattern); the fast-math list (`:147-157`) holds the probe + the eight T003 entries and not `preset_cpu_test.cpp` or
`ghost_triggers_additive_test.cpp` (`:159-162` says why); `${CMAKE_SOURCE_DIR}/tools` on the include path (`:100`); root
`CMakeLists.txt:672-688` `vorago_preset_generator` and `:730-731` `generate_vorago_presets`; `vorago_tests.exe --list-tests "[.perf]"` lists
`Vorago_ProcessorCpu`, `Vorago_SelectStrongestLinks_WorstCase`, `Vorago_PresetCpu`. Nothing was missing.

## T049 — preset CPU (FR-041, SC-017; main loop, 2026-10-06 19:22–20:17, alone, P-core pinned, after 15 min idle)

**Test first.** `Vorago_PresetCpu` (`tests/integration/preset_cpu_test.cpp`, `[vorago][.perf][performance]`) was written this
session into the T003 skeleton (no TEST_CASE before): per preset and the default surface, PresetHost at 48 kHz / 512, setState,
block 0 carries the four `kCpuNotes` and `kPolyphonyId → 0.6` (list index 3, four voices; `REQUIRE(enginePolyphony() == 4)` is
the non-vacuity check), untimed pre-roll to the patch's own `A + 5 s`, 16 trials × 100 blocks `steady_clock`, interleaved with a
default host that is re-created and re-pre-rolled whenever its next trial would leave `[A + 5, A + 65] s`; gate worst
`min-trial(preset) / min-trial(default) ≤ 1.15`; printed, not gated: the figure against `kReferenceNs` and the stored-polyphony
figure (measured separately only when the stored polyphony ≠ 4; no def stores one, so every row's stored figure is the forced
one). No fast-math exemption for the TU (`tests/CMakeLists.txt:159-162`). Built with 0 warnings (`f:/tmp/p14/build_t049.log`).

**Run.** `node tools/run-cpu-tests.js vorago_tests` (`artifacts/t049_cpu_lane.log`: strays 0, START 19:22:26 after the 19:06 build
and 15 min of idle, END 20:17:01, "1/1 suites passed"), full output `artifacts/t049_cpu_vorago_tests.log`: `:652` "All tests
passed (1655398 assertions in 6 test cases)" — the three hidden perf cases plus the three `[long]~[vorago-sweep]` cases the runner's
filter includes.

**Verdict** (`:639`): "worst preset: Ice Shelf ratio 1.1267 (gate <= 1.15) default surface 2661251 ns/block = 0.8316 x
kReferenceNs". 42 / 42 presets ≤ 1.15. Against the 3 200 000 ns reference the processor-level figures run 0.6866–1.1555×;
above 1.0: Choir of Absence 1.1555, Growth Ring 1.0065 (recorded only, FR-041 does not claim the 30 % ceiling for presets).

| preset | preset ns/block | default ns/block (interleaved) | ratio | vs kReferenceNs | stored polyphony | log line |
|---|---|---|---|---|---|---|
| Ice Shelf | 2998452 | 2661251 | 1.1267 ok | 0.9370 | 4 (= forced) | :629 |
| Choir of Absence | 3697443 | 3330009 | 1.1103 ok | 1.1555 | 4 (= forced) | :605 |
| Strung Abyss | 3011717 | 2828917 | 1.0646 ok | 0.9412 | 4 (= forced) | :626 |
| Entropic Hum | 2942463 | 2772787 | 1.0612 ok | 0.9195 | 4 (= forced) | :612 |
| Crowded Dark | 2933603 | 2771877 | 1.0583 ok | 0.9168 | 4 (= forced) | :609 |
| Pressure Front | 2913292 | 2773949 | 1.0502 ok | 0.9104 | 4 (= forced) | :613 |
| Cathedral Void | 2851587 | 2758583 | 1.0337 ok | 0.8911 | 4 (= forced) | :604 |
| Erosion | 2838603 | 2749728 | 1.0323 ok | 0.8871 | 4 (= forced) | :608 |
| Colony Pulse | 2948612 | 2899321 | 1.0170 ok | 0.9214 | 4 (= forced) | :603 |
| Growth Ring | 3220915 | 3174127 | 1.0147 ok | 1.0065 | 4 (= forced) | :624 |
| Smeared Horizon | 2818804 | 2877402 | 0.9796 ok | 0.8809 | 4 (= forced) | :599 |
| Steam Vent | 2745524 | 2859572 | 0.9601 ok | 0.8580 | 4 (= forced) | :638 |
| Teeming | 2747099 | 2899649 | 0.9474 ok | 0.8585 | 4 (= forced) | :616 |
| Slow Bloom | 2922429 | 3217305 | 0.9083 ok | 0.9133 | 4 (= forced) | :602 |
| Drifting Strata | 2564506 | 2832035 | 0.9055 ok | 0.8014 | 4 (= forced) | :610 |
| Resonant Shaft | 2452489 | 2751758 | 0.8912 ok | 0.7664 | 4 (= forced) | :598 |
| Hull Ark | 2382802 | 2687251 | 0.8867 ok | 0.7446 | 4 (= forced) | :630 |
| Singing Colony | 2476238 | 2797505 | 0.8852 ok | 0.7738 | 4 (= forced) | :620 |
| Bloom Colony | 2385475 | 2725232 | 0.8753 ok | 0.7455 | 4 (= forced) | :619 |
| Feedback Mire | 2353197 | 2693546 | 0.8736 ok | 0.7354 | 4 (= forced) | :600 |
| Cavern Wall | 2375574 | 2726744 | 0.8712 ok | 0.7424 | 4 (= forced) | :632 |
| Wind Through Basalt | 2515791 | 2892755 | 0.8697 ok | 0.7862 | 4 (= forced) | :597 |
| Tectonic Floor | 2424096 | 2792259 | 0.8681 ok | 0.7575 | 4 (= forced) | :601 |
| Spore Drift | 2318420 | 2702042 | 0.8580 ok | 0.7245 | 4 (= forced) | :637 |
| Glass Sphere | 2394404 | 2793952 | 0.8570 ok | 0.7483 | 4 (= forced) | :633 |
| Fogbound | 2410490 | 2826062 | 0.8530 ok | 0.7533 | 4 (= forced) | :615 |
| Column Hymn | 2213638 | 2599629 | 0.8515 ok | 0.6918 | 4 (= forced) | :631 |
| Feeding Loops | 2422469 | 2881415 | 0.8407 ok | 0.7570 | 4 (= forced) | :622 |
| Haunted Colony | 2367953 | 2823600 | 0.8386 ok | 0.7400 | 4 (= forced) | :623 |
| Iron Plate | 2287062 | 2727114 | 0.8386 ok | 0.7147 | 4 (= forced) | :627 |
| Glass Well | 2283570 | 2732316 | 0.8358 ok | 0.7136 | 4 (= forced) | :625 |
| Dead Air | 2275052 | 2732242 | 0.8327 ok | 0.7110 | 4 (= forced) | :635 |
| Monolith | 2292442 | 2762254 | 0.8299 ok | 0.7164 | 4 (= forced) | :618 |
| Stone Gravity | 2280861 | 2750312 | 0.8293 ok | 0.7128 | 4 (= forced) | :611 |
| Hull Resonance | 2405543 | 2925080 | 0.8224 ok | 0.7517 | 4 (= forced) | :606 |
| Abyssal Wind | 2197089 | 2676341 | 0.8209 ok | 0.6866 | 4 (= forced) | :636 |
| Weighted Deep | 2282461 | 2813171 | 0.8113 ok | 0.7133 | 4 (= forced) | :614 |
| Chamber Drone | 2246883 | 2794025 | 0.8042 ok | 0.7022 | 4 (= forced) | :628 |
| Swarm Breath | 2338336 | 2939145 | 0.7956 ok | 0.7307 | 4 (= forced) | :621 |
| Lightless | 2369784 | 3003654 | 0.7890 ok | 0.7406 | 4 (= forced) | :607 |
| Sudden Chasm | 2268358 | 2917457 | 0.7775 ok | 0.7089 | 4 (= forced) | :634 |
| Endless Descent | 2370929 | 3179001 | 0.7458 ok | 0.7409 | 4 (= forced) | :617 |

Same lane, `Vorago_ProcessorCpu`: `:16` "SC-014 arm P best ns/block (512 @ 48 kHz, poly 4): 3.35773e+06", `:19` "SC-014 arm D best ns/block (512 @ 48 kHz, poly 4): 3.36161e+06" (P/D 0.9988 ≤ 1.05), `:36` "SC-012 PF/D ratio (gate <= 1.05): 1.01294".

## T061 / T062 — full suites, lanes in parallel (main loop, 2026-10-06 20:18 →), portability

Clean build first: `dsp_systems_tests`, `vorago_preset_generator`, `Vorago` (`f:/tmp/p14/build_t061.log`, exit 0, 0 warnings;
`vorago_tests` built 19:06 with the T049 TU, `build_t049.log`, 0 warnings). `node tools/run-close-lanes.js --skip-sweep --out
f:/tmp/p14/close-lanes` ran the eight per-push suites and both `[long]` lanes concurrently (the MUST from the 13c close; the
sweep lane is T048's sweep 5):

- per-push `vorago_tests`: `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"
- per-push `dsp_systems_tests` in the concurrent lane: `close-lanes/suite_dsp_systems_tests.log:1514` "test cases: 1426 | 1425 passed | 1 failed" — the one red is `SeraphisEngine_VoiceStealIsClickless` (`seraphis_engine_test.cpp:4265`, `close-lanes/suite_dsp_systems_tests.log:403` "REQUIRE( bestWorstBlockUs <= kBlockBudgetUs )" 22336.0 vs 10666.7 µs): a **wall-clock** block budget inside an untagged per-push case, inflated by the 16-way lane load (13c ran the suite alone: `specs/vorago-phase13c-capability-audibility/artifacts/final_suite_dsp_systems_tests.log` 1426 / 1426). Re-run alone, idle: `t061_suite_dsp_systems_rerun.log:1480` "All tests passed (6061626 assertions in 1426 test cases)". Finding, not fixed here: that clause measures the machine and belongs behind `[perf]` (Seraphis area, surfaced to the user).
- `[long]~[vorago-sweep]` `vorago_tests` (SeedTableSpread, ParameterStepsAreContinuous, RandomSurfaceSoak): `close-lanes/long_vorago_nonsweep.log:524` "All tests passed (1654356 assertions in 3 test cases)"; the same three
  also ran inside the T049 lane (`t049_cpu_vorago_tests.log:652`)
- `[long]` `dsp_systems_tests`: `close-lanes/long_dsp_systems_tests.log:1227` "test cases: 42 | 41 passed | 1 failed" — the one red is `vorago_macro_test.cpp(1318)` Movement per-band total-variation rho 0.8333 ≥ 0.9 (`:1140`), the base tree's own red recorded by 13c FR-031 (`specs/vorago-phase13c-capability-audibility/compliance.md:122`; every base-passing assertion passes). It is carried, not cleared: surfaced in the verdict below.
- the other six per-push suites: see `f:/tmp/p14/close-lanes/summary.txt` (copied to `artifacts/t061_close_lanes_summary.txt`)
- `node tools/check-seraphis-green.js`: `t061_check_seraphis_green.log:11` "check-seraphis-green: in scope"
- determinism: `t061_determinism_vorago.log:5` "check-preset-generator-determinism: OK — 42 file(s); 0 differing between two fresh runs, 0 changed by a third run over an existing tree."; no-flag Seraphis `t061_determinism_seraphis.log:5`; regenerate
  over the committed tree → "wrote 42 presets", `git status` 0 changed (`t061_determinism_tree.log`)
- pluginval strictness 5: `t061_pluginval.log:117` "exit=0 END 2026-10-06 20:18:56" (117 lines, every test block "Completed")
- clang-tidy: vorago `t061_tidy_vorago.log:34` "[OK]   Errors: 0", `:35` "[WARN]   Warnings: 1" (one finding in the new `preset_cpu_test.cpp:237`, a signed/unsigned comparison — fixed with an `int` constant, rebuilt, re-run: `t061_tidy_vorago_rerun.log:12` "[OK]   Warnings: 0"); dsp `t061_tidy_dsp.log:11` "[OK]   Errors: 0", `:12` "[OK]   Warnings: 0"
- preset descriptions (user, during the audition): every def gained a "Hold the note" sentence (the Info Comment only; no
  parameter changed, so sweep 5 stands); generator + `vorago_tests` rebuilt 0 warnings, tree regenerated (42 files), `[preset]~[long]`
  re-run `t061_vorago_preset_lane_final.log:55` "All tests passed (246 assertions in 1 test case)", `Vorago_FactoryPresets_TreeMatchesGenerator` `:55`; clang-tidy vorago after the TU fix
  `t061_tidy_vorago_rerun.log` 0 / 0
- T062 portability: `t062_portability.log:11` "check-portability: all clear -- 1 compiled."; `wsl --shutdown` rc 0

## T063 — FR / SC compliance table and release-gate verdict (2026-10-06)

Legend: ✅ pass (verified now, cite) · ❌ red, surfaced (a measured shortfall with its ruling or hand-over) · ☑ recorded by ruling
(a requirement the user re-scoped to "recorded, not gated") · ⏳ pending (needs the push / the user). Counts: 77 ✅, 9 ❌,
5 ☑, 7 ⏳.

| Requirement | Verdict | Evidence |
|---|---|---|
| FR-001 | ✅ pass | `src/preset/vorago_preset_config.h:32-33` the seven C-1 names in order; `Vorago_FactoryPresets_CategoriesMatchConfig` (`plugins/vorago/tests/unit/preset/factory_preset_test.cpp:253`): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| FR-002 | ✅ pass | same case; `%PROGRAMDATA%\Krate Audio\Vorago\` holds Abyss Caverns Drones Ghosts Machines Organisms Textures, 42 `.vstpreset` (listed 2026-10-06 20:18 after the T061 `Vorago` build). |
| FR-003 | ✅ pass | `Vorago_FactoryPresets_ContainerAndInfo` (`plugins/vorago/tests/unit/preset/factory_preset_test.cpp:1581`), `Vorago_FactoryPresets_InfoMatchesSavePreset` (`:1651`), `Vorago_PresetDefs_InfoXmlBytes` (`:591`): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| FR-004 | ✅ pass | `Vorago_FactoryPresets_LibraryShape` (`plugins/vorago/tests/unit/preset/factory_preset_test.cpp:567`, ≥ 3 per category, N = 42): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)"; `sweep5_aggregate.log:363` "Required primaries from the measured set: 42; from the recorded constant: 42; verified primaries: 36". |
| FR-005 | ✅ pass | `Vorago_FactoryPresets_BrowserScan` (`plugins/vorago/tests/unit/preset/factory_preset_test.cpp:1725`, count == N, 0 non-factory) and `Vorago_PresetDefs_ClaimsWellFormed` (`:500`): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| FR-006 | ✅ pass | `Vorago_FactoryPresets_StreamShape` (`plugins/vorago/tests/unit/preset/factory_preset_test.cpp:1751`: version == `kCurrentStateVersion` = 3, length == `kStateV3Bytes` = 436, `src/plugin_ids.h:24,52-54`): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)"; generator "Wrote 436 state bytes" per preset (`f:/tmp/p14/build_sweep5.log`). |
| FR-007 | ✅ pass | `Vorago_FactoryPresets_StreamShape` (polyphony ≤ 4): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)"; every T049 row "stored poly 4" (`t049_cpu_vorago_tests.log:597-638`). |
| FR-008 | ✅ pass | `Vorago_FactoryPresets_StreamShape` (`A ≤ 180 s`, `Rel ≤ 60 s`): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| FR-009 | ✅ pass | `Vorago_FactoryPresets_StreamShape` (bit-pattern finiteness; TU in the fast-math list `tests/CMakeLists.txt:152`): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| FR-010 | ✅ pass | `Vorago_PresetDefs_CellSpecsMatchSpec` (`plugins/vorago/tests/unit/preset/factory_preset_test.cpp:368`, `Capability::Count == 79`): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)"; every sweep record "cells 79" (`f:/tmp/p14/sweep5-out/record_*.txt`). |
| FR-011 | ✅ pass | `Vorago_PresetDefs_ClaimsWellFormed` (`plugins/vorago/tests/unit/preset/factory_preset_test.cpp:500`): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| FR-011b | ✅ pass | D10.1 is claimed only as Cathedral Void's secondary (`tools/vorago_preset_defs.h`, S8 row); `Vorago_PresetDefs_ClaimsWellFormed`: per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)"; matrix `:163` "D10.1 freeze holds a field .......S.................................. . (1 factory verifier)". |
| FR-011a | ❌ red (surfaced) | `Vorago_PresetMatrix_NoShowcaseSubset`: `sweep5_aggregate.log:2612` "SUBSET pairs: 51" (13c final 50; every pair names Smeared Horizon, whose single S3 claim is verified by 51 others). Recorded since sweep 1; no ruling relaxes it — open. |
| FR-012 | ✅ pass | `Vorago_PresetSweep_AblationVerifiesClaims` computes all 79 cells per preset (`integration/preset_sweep_test.cpp:1150`); records `f:/tmp/p14/sweep5-out/record_*.txt` "cells 79" + 79 cell lines each. |
| FR-013 | ❌ red (surfaced) | `Vorago_PresetMatrix_CoverageComplete`: `sweep5_aggregate.log:363` "Required primaries from the measured set: 42; from the recorded constant: 42; verified primaries: 36"; `:317` "E1 partial -> bloom is no preset's verified primary", `:326` "E4 feedback -> loop wake is no preset's verified primary"; 11 cells without verifier (`:186-256`). The six engine-limited primaries are handed to a 13d pass (compliance "Sweep 5"). |
| FR-014 | ✅ pass | `Vorago_PresetMatrix_ParameterSpaceDistinct` (`plugins/vorago/tests/unit/preset/factory_preset_test.cpp:1856`): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| FR-015 | ☑ recorded by ruling | `Vorago_PresetSweep_SoundSpaceDistinct`: `sweep5_aggregate.log:4065` "Pairs 861: min d 0.9547 (Wind Through Basalt vs Swarm Breath, floor 5.7290)  median d 8.6876  max d 29.6164", `:4066` "t_max 2.8645  K 4  effective floor max(F, 2 t_max) = 5.7290 (s(P) recorded, not gated - ruling 2026-09-30)  pairs below floor: 159" — the floor is recorded, not gated (sweep ruling 2026-09-30, spec Clarifications "Sweep rulings"). |
| FR-016 | ✅ pass | plan §7 derivation table (N = 40 → 38 → 42 by rulings); `Vorago_PresetDefs_RequiredPrimaries` (`plugins/vorago/tests/unit/preset/factory_preset_test.cpp:537`, 42): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| FR-017 | ✅ pass | every stop surfaced and ruled: spec Clarifications "Gate G2 rulings", "T043 ruling", "Sweep rulings", "Sweep 2 rulings", "Re-author loop rulings", "Gate G2 re-run ruling"; 13c rulings B-1..B-20 (`specs/vorago-phase13c-capability-audibility/artifacts/rulings.md`). |
| FR-017a | ☑ recorded by ruling | compliance "FR-017a pilot / G2" (run 3 PROCEED, K = 4) and "G2 re-run on the sweep-5 defs" (`sweep5_pilot_calibrate.log:19,21,24`, STOP on Glass Well, ruled K = 4 stays; `Vorago_PresetSupport_RuledTakes` `integration/preset_sweep_test.cpp:959`: per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)"). |
| FR-070 | ✅ pass | compliance T059: `specs/vorago-phase13b-ecosystem-audibility/artifacts/final2_table_default.log:52,75`, `final2_table_lifemax.log:50,59`. |
| FR-071 | ✅ pass | spec Clarifications Q1 (2026-09-29) ratified `R` = {syncRate, selfAffinity}; docs commit `7a5198ed` (2026-09-29) precedes `64f57e1a` (2026-10-01), the first commit with IDs 901 / 902. |
| FR-071a | ✅ pass | `dsp/include/krate/dsp/systems/vorago_voice.h:1547` `setEcosystemSyncRate → ecosystem_.setSyncRate`, `:1556` self affinity; `Vorago_EcosystemRosterReachesEngine` (`unit/ecosystem_roster_test.cpp:126`): `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)". |
| FR-072 | ✅ pass | `src/plugin_ids.h:181-182` (901, 902), `:24` `kCurrentStateVersion = 3`, `:52-54` `kStateV3Bytes = 436`; `src/processor/processor.cpp:636,663,681` v3 load / save; `Vorago_StateRoundTripV3`, `Vorago_State_V2LoadsWithRosterDefaults`, `Vorago_State_V3TruncatedKeepsPrefix`, `Vorago_State_V3NonFiniteKnobRejected`, `Vorago_State_V4Rejected`, `Vorago_ControllerState_V3AndV2` (`unit/state_v3_test.cpp:148-318`): `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)". |
| FR-073 | ✅ pass | `resources/editor.uidesc:112-113` control-tags, `:391` / `:393` ArcKnobs on page 6; `Vorago_Ecosystem_PageBindsRosterIds` (`unit/controller/editor_layout_test.cpp:1219`): `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)". |
| FR-074 | ✅ pass | the FR-072 / FR-073 cases plus `Vorago_EcosystemParamsContract` (`unit/params/ecosystem_params_test.cpp:117`), `Vorago_VoiceParams_FieldCount` (`unit/ecosystem_roster_test.cpp:173`): `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)". |
| FR-077 | ✅ pass | `vorago_voice.h:1711-1712` `kNoiseBusMakeupDb = 30`, `kNoiseBusMakeupGain = 31.6227766`; compliance "Noise make-up: regression on the amended voice" (the probe readings); sweep 5 S1 primaries verified (Wind Through Basalt et al., `sweep5_aggregate.log:381`). |
| FR-077a | ✅ pass | `noise_generator.h:350` `snapLevelSmoothers()`, `noise_organism.h:1955` called from `applySlotConfiguration`; compliance "Sweep 2 … the reset defect"; `Vorago_PresetSweep_RendersAreReproducible` green in all 8 shards (8 shards, failures only in AblationVerifiesClaims / LongRender). |
| FR-077b | ✅ pass | `vorago_engine.h:1018` `kGhostTapMakeupDb = 21.0f` (13c ruling B-6 moved S-7's 12 dB to 21); S9 Choir of Absence 4.3383 at K = 4 (`f:/tmp/p14/sweep5-out/record_8.txt`, compliance "Sweep 5"). |
| FR-075 | ❌ red (surfaced) | E6.hi / E7.hi are cells (`Vorago_PresetDefs_CellSpecsMatchSpec`) claimed by Colony Pulse, but `sweep5_aggregate.log:341` "E6.hi ecosystem sync rate high is no preset's verified secondary claim", `:348` "E7.hi ecosystem self-affinity high is no preset's verified secondary claim" (1.3700 / 1.3430 < 1.5; 13c ruling B-20 surfaced them). |
| FR-076 | ☑ recorded by ruling | compliance T059: the R-1 edit is `vorago_engine.h` (+ fields at `:188,191`, `kFieldCount = 33` at `:194`, `:899`) and `vorago_voice.h` forwarders; the three ruled widenings (FR-077, FR-077a in `64f57e1a`; 13c in `91a40879`) exceed the literal bound and are each recorded where ruled. |
| FR-018 | ✅ pass | root `CMakeLists.txt:679` `add_executable(vorago_preset_generator tools/vorago_preset_generator.cpp …)`; built with 0 warnings (`f:/tmp/p14/build_sweep5.log`, `build_t061.log`). |
| FR-019 | ✅ pass | `tools/vorago_preset_generator.cpp:131-134` `argv[1]` → output base; run output "wrote 42 presets" (`t061_determinism_tree.log`). |
| FR-020 | ✅ pass | root `CMakeLists.txt:730-731` `generate_vorago_presets`; regenerating over the committed tree changes 0 files (`t061_determinism_tree.log` last line "0"). |
| FR-021 | ✅ pass | `Vorago_PresetHost_DriveContract` (`plugins/vorago/tests/unit/preset/factory_preset_test.cpp:89`), `Vorago_PresetHost_BuildPresetComponentState` (`:1122`): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| FR-022 | ✅ pass | `tools/vorago_preset_defs.h` namespace `Vorago::PresetDefs`, data only (`VoragoPresetDef` aggregates); `Vorago_PresetDefs_ClaimsWellFormed`: per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| FR-023 | ✅ pass | `t061_determinism_vorago.log:5` "check-preset-generator-determinism: OK — 42 file(s); 0 differing between two fresh runs, 0 changed by a third run over an existing tree.". |
| FR-024 | ✅ pass | compliance T053; `t061_determinism_seraphis.log:5` "check-preset-generator-determinism: OK — 42 file(s); 0 differing between two fresh runs, 0 changed by a third run over an existing tree." (no-flag path), `t061_determinism_vorago.log:5` (`--plugin vorago`). |
| FR-025 | ⏳ pending | needs the release runner (Linux/GCC/Ninja): `check-portability.js` compiles headers, not the generator target — recorded pending the push (T057). |
| FR-026 | ✅ pass | `plugins/vorago/CMakeLists.txt:123` `krate_plugin_install_presets`; listing after the T061 build: 7 directories, 42 files under `%PROGRAMDATA%\Krate Audio\Vorago\`. |
| FR-027 | ✅ pass | `installers/windows/setup.iss:66-68` and `installers/linux/README.txt:29-44` read and accurate (compliance T059). |
| FR-027a | ✅ pass | compliance T060 (eleven Phase 14 TUs registered, fast-math list exact, nothing missing). |
| FR-028 | ✅ pass | `Vorago_FactoryPresets_ContainerAndInfo` (`plugins/vorago/tests/unit/preset/factory_preset_test.cpp:1581`): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| FR-029 | ✅ pass | `Vorago_FactoryPresets_RoundTrip` (`plugins/vorago/tests/unit/preset/factory_preset_test.cpp:1708`): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| FR-030 | ✅ pass | `Vorago_FactoryPresets_BrowserScan` (`plugins/vorago/tests/unit/preset/factory_preset_test.cpp:1725`), `Vorago_FactoryPresets_CategoriesMatchConfig` (`:253`): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| FR-031 | ✅ pass | `Vorago_PresetSupport_DecodeDefaultSurface` (`plugins/vorago/tests/unit/preset/factory_preset_test.cpp:659`), `Vorago_PresetSupport_TimelineDefault` (`:692`), `Vorago_PresetDefs_DStatePredicates` (`:998`): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| FR-032 | ✅ pass | `Vorago_FactoryPresets_TreeMatchesGenerator` (`plugins/vorago/tests/unit/preset/factory_preset_test.cpp:1807`): `sweep5_vorago_preset_lane.log:55` "All tests passed (246 assertions in 1 test case)". |
| FR-033 | ❌ red (surfaced) | `Vorago_PresetSweep_LongRender` over 8 shards (shard 0 `:630` "test cases: 5 | 3 passed | 2 failed"; shard 1 `:624` "test cases: 5 | 4 passed | 1 failed"; shard 2 `:579` "All tests passed (181 assertions in 5 test cases)"; shard 3 `:538` "test cases: 5 | 4 passed | 1 failed"; shard 4 `:495` "All tests passed (173 assertions in 5 test cases)"; shard 5 `:533` "test cases: 5 | 4 passed | 1 failed"; shard 6 `:554` "test cases: 5 | 4 passed | 1 failed"; shard 7 `:496` "All tests passed (180 assertions in 5 test cases)"): the one arm red is `sweep5_shard_0.log:36` "Choir of Absence take 3 (seed 13): peak 0.966051, hi -5.57553, lo -8.61327," — ruling B-18 (trim measured and declined). |
| FR-033a | ✅ pass | `Vorago_PresetSweep_SustainAtAllRates` ran in every shard (5 cases per shard); every shard's failures are in AblationVerifiesClaims / LongRender only (shard logs' `FAILED:` lines). |
| FR-034 | ✅ pass | `Vorago_PresetSweep_ShortBounded` (`integration/preset_sweep_test.cpp:78`): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| FR-035 | ✅ pass | as FR-014. |
| FR-036 | ☑ recorded by ruling | as FR-015 (`sweep5_aggregate.log:4065-4066`, t_max 2.8645, K 4). |
| FR-037 | ❌ red (surfaced) | `Vorago_PresetSweep_AblationVerifiesClaims`: 16 red lines across the shards (6 primaries, 10 secondaries; compliance "Sweep 5"); 13c final 35 → 16, none added (`sweep5_record_diff_13c.txt`). |
| FR-038 | ✅ pass | `Vorago_PresetSweep_RendersAreReproducible` (`integration/preset_sweep_test.cpp:1215`, tagged `[vorago-sweep]`) ran in every shard; no shard has a failure outside AblationVerifiesClaims / LongRender. |
| FR-039 | ✅ pass | `Vorago_FactoryPresets_SequentialLoadNoAlloc` (`integration/preset_load_rt_test.cpp:105`): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| FR-040 | ✅ pass | `Vorago_FactoryPresets_ConcurrentLoadIsRtSafe` (`integration/preset_load_rt_test.cpp:170`): per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| FR-041 | ✅ pass | `t049_cpu_vorago_tests.log:639` "worst preset: Ice Shelf  ratio 1.1267 (gate <= 1.15)  default surface 2661251 ns/block = 0.8316 x kReferenceNs (3200000 ns, recorded only)"; `:652` "All tests passed (1655398 assertions in 6 test cases)" (compliance "T049 — preset CPU"). |
| FR-042 | ⏳ pending | the user's audition (T050 table in this file) — nothing automated substitutes. |
| FR-060 | ✅ pass | `src/processor/processor.cpp:689` `getTailSamples()`; `Vorago_Processor_GetTailSamplesMatchesState` (`unit/tail_samples_test.cpp:103`): `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)". |
| FR-061 | ✅ pass | `Vorago_Ghost_TriggersAddToDensityScheduler` (`unit/ghost_triggers_additive_test.cpp:78`): `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; Clarification Q8 (2026-09-29); cited in `plugins/vorago/CLAUDE.md` (T052). |
| FR-062 | ✅ pass | `plugins/vorago/docs/index.html` + `assets/style.css` (compliance T051; 2 relative refs, 0 missing). |
| FR-063 | ✅ pass | `.claude/workflows/release-readiness.js` `PLUGIN_MAP.vorago`; `.claude/skills/release/SKILL.md` list + table; `lint-plugin-roster` OK (compliance T054). |
| FR-064 | ✅ pass | `plugins/vorago/version.json` "1.0.0"; `CHANGELOG.md` "## [1.0.0] - 2026-10-06"; `check-changelog-coverage.js vorago` exit 0; freeze recorded in `plugins/vorago/CLAUDE.md` decision 1 (compliance T058, T052). |
| FR-065 | ⏳ pending | the parts ran here: build 0 warnings (`f:/tmp/p14/build_t061.log`), `vorago_tests` `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)", pluginval `t061_pluginval.log:117` "exit=0 END 2026-10-06 20:18:56" (strictness 5), version / CHANGELOG sync (FR-064), `t062_portability.log:11` "check-portability: all clear -- 1 compiled.", clang-tidy vorago `t061_tidy_vorago.log:34` "[OK]   Errors: 0" / `:35` "[WARN]   Warnings: 1" → after the fix `t061_tidy_vorago_rerun.log:12` "[OK]   Warnings: 0", dsp `t061_tidy_dsp.log:11` "[OK]   Errors: 0" / `:12` "[OK]   Warnings: 0"; the packaged `release-readiness` workflow itself and the AU validation need the push — pending (T057 / T063). |
| FR-066 | ✅ pass | `.github/workflows/long-tests-nightly.yml` jobs `vorago-sweep` (3 OS × 10 shards) and `vorago-sweep-aggregate`, `timeout-minutes: 180`; `ci.yml:369,655,1116` `[long]~[vorago-sweep]` (compliance T055 / T056); the first green run needs the push (SC-023). |
| SC-001 | ✅ pass | `Vorago_FactoryPresets_CategoriesMatchConfig`: per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| SC-002 | ✅ pass | `Vorago_FactoryPresets_ContainerAndInfo`: per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| SC-003 | ✅ pass | `Vorago_FactoryPresets_RoundTrip`: per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| SC-004 | ✅ pass | `Vorago_FactoryPresets_BrowserScan`: per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| SC-005 | ✅ pass | `Vorago_FactoryPresets_StreamShape`: per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| SC-006 | ✅ pass | `Vorago_FactoryPresets_TreeMatchesGenerator`: `sweep5_vorago_preset_lane.log:55` (MSVC leg; GCC / AppleClang legs need the push). |
| SC-007 | ✅ pass | `t061_determinism_vorago.log:5` "check-preset-generator-determinism: OK — 42 file(s); 0 differing between two fresh runs, 0 changed by a third run over an existing tree.". |
| SC-008 | ❌ red (surfaced) | `sweep5_aggregate.log:363` "Required primaries from the measured set: 42; from the recorded constant: 42; verified primaries: 36" — 36 of 42; E1 / E4 no verified primary (`:317`, `:326`); E6.hi / E7.hi no verified secondary (`:341`, `:348`); `SUBSET pairs: 51` (`:2612`). |
| SC-009 | ✅ pass | `Vorago_PresetMatrix_ParameterSpaceDistinct`: per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| SC-010 | ☑ recorded by ruling | `sweep5_aggregate.log:4065` "Pairs 861: min d 0.9547 (Wind Through Basalt vs Swarm Breath, floor 5.7290)  median d 8.6876  max d 29.6164"; `:4066` "t_max 2.8645  K 4  effective floor max(F, 2 t_max) = 5.7290 (s(P) recorded, not gated - ruling 2026-09-30)  pairs below floor: 159" (recorded, not gated — ruling 2026-09-30). |
| SC-011 | ❌ red (surfaced) | 16 claims under their bars (6 primaries < 4.0, 10 secondaries < 1.5; compliance "Sweep 5"); every other claimed cell verified (shard logs). |
| SC-012 | ❌ red (surfaced) | `Vorago_PresetSweep_LongRender`: 41 presets all arms green on all four takes; `sweep5_shard_0.log:36` "Choir of Absence take 3 (seed 13): peak 0.966051, hi -5.57553, lo -8.61327," (ruling B-18). |
| SC-013 | ⏳ pending | runner wall clocks — needs the push and a dispatch (T057). |
| SC-014 | ✅ pass | `Vorago_PresetSweep_ShortBounded`: per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| SC-015 | ✅ pass | `Vorago_PresetSweep_RendersAreReproducible` green in all 8 shards (FR-038). |
| SC-016 | ✅ pass | `Vorago_FactoryPresets_SequentialLoadNoAlloc`, `Vorago_FactoryPresets_ConcurrentLoadIsRtSafe`: per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| SC-017 | ✅ pass | `t049_cpu_vorago_tests.log:639` "worst preset: Ice Shelf  ratio 1.1267 (gate <= 1.15)  default surface 2661251 ns/block = 0.8316 x kReferenceNs (3200000 ns, recorded only)". |
| SC-018 | ✅ pass | matrix rows `:165` "D11 ghost reverse >= 0.5 ........S...............................X. . (1 factory verifier)"; `:166` "D12.1 ghost event triggers o ........S................................. . (1 factory verifier)" (verified by ≥ 1 preset each). |
| SC-019 | ⏳ pending | `version.json` 1.0.0 and the `[1.0.0]` entry are in; the release-readiness row and auval need the release commit pushed (T063) — pending. |
| SC-020 | ⏳ pending | T050 audition table prepared; the user's notes are outstanding. |
| SC-021 | ✅ pass | `grep -n vorago .claude/workflows/release-readiness.js .claude/skills/release/SKILL.md` — both entries (compliance T054); `lint-plugin-roster` 8 plugins. |
| SC-022 | ✅ pass | `Vorago_PresetSweep_SustainAtAllRates` green in all 8 shards (FR-033a). |
| SC-023 | ⏳ pending | the file side is in (FR-066; both jobs ≤ 180 min; `ci.yml` filters); one green nightly run needs the push. |
| SC-024 | ✅ pass | `Vorago_PresetSweep_FreezeGesture` green in all 8 shards; matrix `:163` "D10.1 freeze holds a field .......S.................................. . (1 factory verifier)"; `git diff 339cd501..HEAD` on the cavern / aether / space-params headers is empty and `processor.cpp` has no freeze-path hunk (compliance T059). |
| SC-025 | ✅ pass | compliance T059 (both 13b tables' rows for syncRate and selfAffinity). |
| SC-026 | ✅ pass | compliance T059 (ratified 2026-09-29, first ID commit 2026-10-01). |
| SC-027 | ✅ pass | `src/plugin_ids.h:181-182,24,52-54`; `Vorago_VoiceParams_FieldCount` (`kFieldCount == 33`, `vorago_engine.h:194`), the `unit/state_v3_test.cpp` cases: `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)". |
| SC-028 | ❌ red (surfaced) | UI side ✅ (`Vorago_Ecosystem_PageBindsRosterIds`: `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"); coverage side ❌ E6.hi / E7.hi not verified (`sweep5_aggregate.log:341,348`, ruling B-20). |
| SC-033 | ✅ pass | `vorago_voice.h:1711-1712` and compliance "Noise make-up: regression on the amended voice" (probe readings); S1-primary presets verified in sweep 5 (`sweep5_aggregate.log:381`). |
| SC-029 | ✅ pass | `sweep5_aggregate.log:363` "Required primaries from the measured set: 42; from the recorded constant: 42; verified primaries: 36" (N = 42, required = recorded). |
| SC-030 | ✅ pass | `Vorago_Processor_GetTailSamplesMatchesState` (`unit/tail_samples_test.cpp:103`): `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)". |
| SC-031 | ✅ pass | `Vorago_FactoryPresets_LibraryShape`: per-push lane `close-lanes/suite_vorago_tests.log:250` "All tests passed (4059849 assertions in 119 test cases)"; `[preset]~[long]` lane `sweep5_vorago_preset_lane.log:32` "All tests passed (18135 assertions in 41 test cases)"; after the T061 tidy fix `t061_vorago_preset_lane_rerun.log:55` "All tests passed (246 assertions in 1 test case)". |
| SC-032 | ✅ pass | compliance T059 cites + the 7-directory / 42-file listing. |

**Verdict: NOT RELEASE-GREEN.** The library ships 42 playable, level-safe presets (SC-012 all arms green except Choir of
Absence's seed-13 take, ruling B-18) with 36 of 42 named features verified audible; the six engine-limited primaries (E1, E4,
M2, M4, M5, M10), the four unverified roster / E cells and the 51 subset pairs stay red by measurement and are surfaced for the
13d engine pass the user proposed. Pending the user: the T050 audition (SC-020) and permission to push (T057 wall clocks, SC-013
/ SC-019 auval / SC-023 nightly, FR-025 Linux generator build). One carried red from 13c: the Movement rho assertion in the
`dsp_systems_tests` `[long]` lane.
