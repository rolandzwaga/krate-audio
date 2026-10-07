# FR-034 pre-surfaced list — tests that encode old voicing as data (plan §6.4). Each entry applies only if adopted.

1. `VoragoVoice_EcosystemLaneShapingFidelity` (`vorago_ecosystem_lever_test.cpp:1334-1337`). Trigger: `kPartialLaneGain ≠ 1`. Reason: the expectation hard-codes gain 1 (`+ raw`). Edit: the expectation reads `Probe::partialLaneGain() * raw`. — applies only if adopted.
2. `checkWakes` (`:374-386`) — `VoragoVoice_EcosystemLeverMapping` and `LaneShapingFidelity`. Trigger: W2 only. FR-032 names `EcosystemLever*` unedited, so this is a user ruling (plan §11), not a self-listing. — applies only if adopted.
3. `VoragoVoice_RouteLeverZeroAtZeroLane` (`:1386-1389`) and the `Probe::partialLaneGain()` accessor (`:137`). Trigger: a compiled split. SC-013 says unedited, so this is a user ruling (plan §11). — applies only if adopted.
4. `VoragoEngine_GhostExtensionWiring` clause (a): a fingerprint re-harvest (FR-035), not an edit of an assertion. — applies only if adopted.
5. `VoragoMacro_SweepAxes` comment block (`vorago_macro_test.cpp:1306-1317`) for the Gravity / Pressure history: untouched; listed so no rung's rewrite of nearby text is mistaken for a threshold change. — applies only if adopted.

Base grep of every constant and row amount a planned rung touches: `fr034_grep_base.txt` (T008).
