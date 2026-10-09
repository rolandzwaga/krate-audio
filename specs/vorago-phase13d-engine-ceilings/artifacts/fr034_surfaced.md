# FR-034 pre-surfaced list — tests that encode old voicing as data (plan §6.4). Each entry applies only if adopted.

1. `VoragoVoice_EcosystemLaneShapingFidelity` (`vorago_ecosystem_lever_test.cpp:1334-1337`). Trigger: `kPartialLaneGain ≠ 1`. Reason: the expectation hard-codes gain 1 (`+ raw`). Edit: the expectation reads `Probe::partialLaneGain() * raw`. — applies only if adopted.
2. `checkWakes` (`:374-386`) — `VoragoVoice_EcosystemLeverMapping` and `LaneShapingFidelity`. Trigger: W2 only. FR-032 names `EcosystemLever*` unedited, so this is a user ruling (plan §11), not a self-listing. — **APPLIED 2026-10-08** (W2 adopted, T031): the loop clause reads `std::min(1.0f, VoragoVoice::kLoopWakeLaneGain * eco)`.
3. `VoragoVoice_RouteLeverZeroAtZeroLane` (`:1386-1389`) and the `Probe::partialLaneGain()` accessor (`:137`). Trigger: a compiled split. SC-013 says unedited, so this is a user ruling (plan §11). — applies only if adopted.
4. `VoragoEngine_GhostExtensionWiring` clause (a): a fingerprint re-harvest (FR-035), not an edit of an assertion. — applies only if adopted.
5. `VoragoMacro_SweepAxes` comment block (`vorago_macro_test.cpp:1306-1317`) for the Gravity / Pressure history: untouched; listed so no rung's rewrite of nearby text is mistaken for a threshold change. — applies only if adopted.
6. `VoragoVoice_WakeCombineRule` loop clause (`vorago_voice_test.cpp:2654-2656`). Trigger: W2 only (found by the r6 contracts run, `e4_r6_g25_contracts.log`). Edit: the expectation combines `std::min(1.0f, VoragoVoice::kLoopWakeLaneGain * p.eco)` for the loop lane. — **APPLIED 2026-10-08** (W2 adopted, T031).
7. `VoragoVoice_CeilingLeverNeutral` §3 loop clause (`vorago_ecosystem_lever_test.cpp:1455`, a 13d Group 2 contract written after this list). Trigger: W2 only (found by the shipped per-push lane, `e4_shipped_perpush_dsp_systems.log`: `0.25f == 0.15f` at raw 0.1). Edit: the expectation combines `std::min(1.0f, VoragoVoice::kLoopWakeLaneGain * raw)` for the loop lane. — **APPLIED 2026-10-08** (W2 adopted, T031).

Base grep of every constant and row amount a planned rung touches: `fr034_grep_base.txt` (T008).
