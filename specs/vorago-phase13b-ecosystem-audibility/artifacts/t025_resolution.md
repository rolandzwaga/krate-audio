# T025 — resolution (main loop, 2026-09-28)

The workflow's T025 agent stopped on two per-push reds after the L1+L2 retune (its own logs:
`L2_gate1_default.log`, `L2_gate1_lifemax.log`).

## 1. `VoragoEngine_GhostExtensionWiring` clause (a) — FR-031(b), user ruling

Pins the pre-13b 60 s default render (`kBaseCommitVoragoFingerprint`, `atmosphere_ghost_fixtures.h`
§6.2). Worst metric relative error 0.0244 (bound 0.005), worst sample error 0.0283 (bound 0.006) on the
L2 tree — the intended FR-030 voicing change. **Ruled:** expected red through the tuning ladder; re-pinned
ONCE on the final tree at T028 with a refilled PROVENANCE block. Encoded in tasks.md T028 and the
Plan-decisions block.

## 2. `VoragoVoice_EcosystemLeverReset` path "reset()" — FR-031(a), defect, fixed

Symptom: after `reset()` every lever destination, offset and wake surface equalled the fresh twin
exactly, yet the 1 s depth-0 render differed (L metric 4.196e-03, sample 2.738e-05; R 3.552e-03 /
5.998e-05). Bisection with temporary test switches (removed again): the difference is identical with
the FR-023 injection removed, identical with the 200 dirty chunks removed, and ZERO when the dirty
period's `noteOn()` is removed — so it was never the levers. Cause: `noteOn()` writes per-note
configuration that the component resets preserve: `ContinuousBody::reset()` snaps its 20 ms
log-frequency note glide to the PRESERVED `noteHz_` (`refreshSmootherTargets()` +
`noteLog2Smoother_.snapToTarget()`), and `HarmonicCloud::reset()` keeps `fundamentalHz_` and
`setFundamentalHz()` early-outs an unchanged value. A voice reset after a note therefore took its next
`noteOn()` at the same pitch as "no change" — no glide, no FR-013 pitch-jump crossfade — while a
freshly prepared twin glides from 220 Hz and crossfades from the default. Latent since Phase 10 (its
reset contract was only exercised without a preceding note); exposed by this phase's SC-012 test.

Fix (`vorago_voice.h` `clearRunState()`, right after `installIdentityNeutral()`): restore the
prepare-time note defaults — `cloud_.setFundamentalHz(220.0f)` and
`bodies_[i].setNoteFrequencyHz(ContinuousBody::kDefaultNoteHz)` — BEFORE the component resets, which
then snap to them exactly as `prepare()` does. Scalar writes only, so the RT-safe steal and recovery
paths carry them too. The resonance network's `noteHz_` needs no restore: it holds no smoother on it and
`reset()` dirties the anchors, so both twins recompute the same anchors at the first control step.

Result: all four paths exact — `[LeverReset] reset() / silence()+resetForSteal() / resetForRecovery() /
prepare(96000)->prepare(48000)`: L and R metric 0.000e+00, sample 0.000e+00; "All tests passed (254
assertions in 1 test case)".

## Regression on the fixed L2 tree (per-push filter `~[performance]~[perf]~[benchmark]~[!benchmark]~[long]`)

- `dsp_systems_tests`: "test cases: 1412 | 1411 passed | 1 failed" — the one failure is item 1
  (`vorago_ghost_ext_test.cpp(458)`), the ruled expected red (`t025_regression_dsp_systems.log`).
- `vorago_tests`: "All tests passed (3989980 assertions in 67 test cases)" (`t025_regression_vorago.log`).
- Build of both targets: exit 0, 0 warnings.

Also fixed: the probe's "wake bases: shipped (0.50/0.50)" label was a fixed string that went stale at
L1 (the bases are now `kPeakWakeBase` / `kLoopWakeBase` = 0.45); it now names the constants instead of
quoting values (`ecosystem_rule_probe_test.cpp`).

## T028 — fingerprint re-pin on the final tree (2026-09-28, 10:33-10:40)

`kBaseCommitVoragoFingerprint` (`dsp/tests/unit/systems/atmosphere_ghost_fixtures.h` §6.2) re-measured ONCE on the shipped L5 tree by the consuming clause's own printer: `t028_ghost_fingerprint_before_repin.log` and `_run2.log` print byte-identical literals (md5 5c94cdb92eb2e09d060cfd7903146037; rms 0.096119390174576963, peak 0.30484500527381897, non-zero); pasted with a fourth-harvest PROVENANCE block (why, tree, recipe unchanged, machine, toolchain, date, reproducibility, non-vacuity); the third-harvest block is kept as superseded. After the paste: `t028_ghost_fingerprint_after_repin.log` — "All tests passed (27 assertions in 1 test case)"; build 0 warnings.
