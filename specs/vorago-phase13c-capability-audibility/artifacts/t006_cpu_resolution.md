# T006 — CPU baseline: resolution (main loop, 2026-10-01)

`cpu_base_dsp.log` (the workflow's T006 agent) reports `VoragoEngine_CpuBudget` clause (i) RED on the
unchanged base tree (production tree 64f57e1a, HEAD ae149a1c):

| run | engine ns/block | + Cavern | clause (i) ≤ 3.2e6 | clause (ii) | log |
|---|---|---|---|---|---|
| full dsp lane via `run-cpu-tests.js`, 12:05–17:32, runaway `node -e` at 95 % of a core for the whole lane | 3.32808e6 | 3.45258e6 | FAIL | PASS 123.5 % | `cpu_base_dsp.log:1-7` (marked INVALID at the top) |
| alone, pinned, 5 min after the kill, 13.1 % load | 3.48663e6 | 3.61113e6 | FAIL | PASS 129.4 % | `cpu_base_dsp.log:3419-3420` |
| alone, pinned, 16 min idle, 9.8 % load | 3.37378e6 | 3.49827e6 | FAIL | PASS 125.2 % | `cpu_base_dsp.log:3459-3460` |

`Vorago_ProcessorCpu` (`cpu_base_vorago.log`, after the kill): P 3.24586e6, D 3.37042e6, P/D 0.963 PASS.

## Alternating pinned A/B against the pre-Phase-14 binary (18:07–18:14, 60 s settles, 8–13 % load)

`pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe <exe> -ExeArgs VoragoEngine_CpuBudget`, one case at a
time, old / new / old / new. The old binary is `05d04f66` (last pushed, pre-Phase-14) from the worktree
`f:/tmp/p13b_ab`, built 2026-09-27 19:02 — the same binary 13b's T014 measured.

| arm | run | engine ns/block | + Cavern | clause (i) | log |
|---|---|---|---|---|---|
| `05d04f66` | 1 | 3.09490e6 | 3.21939e6 | FAIL | `cpu_base_ab_05d04f66_run1.log` |
| base (64f57e1a) | 1 | 3.23527e6 | 3.35977e6 | FAIL | `cpu_base_ab_base_run1.log` |
| `05d04f66` | 2 | 3.36264e6 | 3.48713e6 | FAIL | `cpu_base_ab_05d04f66_run2.log` |
| base (64f57e1a) | 2 | 3.27651e6 | 3.40101e6 | FAIL | `cpu_base_ab_base_run2.log` |

History of the SAME `05d04f66` binary: 2.78455e6 / 2.90905e6 on 2026-09-27 (13b
`before_cpu_ab_05d04f66.log`, 2 % load). Phase 14's T049 (2026-09-30 22:34, under the runaway process) read
arm D 2.920e6. Old and new overlap today (old 3.09–3.36e6, new 3.24–3.28e6) and both sit 15–20 % above their
late-September readings: the machine, not the code. Background at the time: a VS Code WSL session
(`wsl.exe` parented by `Code.exe`, relaunched after `wsl --shutdown`; `vmmemWSL` 0.9–2.3 GB), Docker,
Defender, webview — 8–13 % total load.

## Ruling B-2 (spec Clarifications "Build stage (2026-10-01)")

Clause (i) stays absolute; nothing relaxed. This A/B is the recorded before. T060 repeats the alternating
pinned A/B (base binary vs final binary) on an idle machine where the base binary passes clause (i); the
13c delta vs base is reported in ns and % either way. T002–T005 are deterministic renders (T003's repeat
spread 0.0000, `before_tolerance.txt`) and are not re-run for the runaway process.

The four other timing reds in the lane (CharacterProcessor Tape, continuous_body Strings, noise_organism (a),
feedback_ecology ref ceiling) were measured under the runaway process and are owned by T060's isolated
re-measure, never labelled pre-existing. The two `VoragoMacro_SweepAxes` reds are T004's
(`base_long_failures.txt`), not timings.
