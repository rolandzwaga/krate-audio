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
