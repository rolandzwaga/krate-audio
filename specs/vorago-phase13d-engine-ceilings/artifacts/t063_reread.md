# Phase 13d T056 — Phase 14 T063 re-read (FR-043), 2026-10-08

Against the sweep-6 aggregate with the Hull Resonance record at Weight 0.90 (`sweep6b_aggregate.log`; shards `sweep6_shard_<i>.log`, `sweep6b_shard_9.log`), tree 579eefa3 + the Hull 0.90 transcription. Phase 14 rows quoted from `specs/vorago-phase14-presets-release/compliance.md`.

| Phase 14 row | Phase 14 verdict | Sweep-6 reading (log line) | Verdict now |
|---|---|---|---|
| FR-011a showcase subsets | red: SUBSET pairs 51 | `sweep6b_aggregate.log`:2199 "SUBSET pairs: 0" (Hull Resonance witnesses M8 Weight / S10, :781-805; Smeared Horizon D14.1; Haunted Colony D12.1) | ✅ green |
| FR-013 coverage | red: verified primaries 36, E1 / E4 no verifier | `sweep6b_aggregate.log`:316 "verified primaries: 39"; :276 "E1 partial -> bloom is no preset's verified primary" (ruled engine-limited); E4 now Feeding Loops (:334 list); M4, M5 unverified (ruled) | ❌ recorded by ruling (3 cells: E1, M4, M5) |
| FR-033 long render | red: Choir of Absence take 3 arm 1 | `sweep6_shard_8.log`:13 "take 3 seed 13: ... arm1 hi -5.57 dB [NO]" (sweep 5: -5.58; 13c ruling B-18); every other preset's four takes green across the 42 shard logs | ❌ unchanged, ruling B-18 |
| FR-037 ablation verifies claims | red: 16 red lines (6 primaries, 10 secondaries) | shard FAILED lines: primaries E1 (`sweep6_shard_22.log`), M4 (`_13`), M5 (`_14`); secondaries D13.1 Teeming (`_19`), D14.2 Drifting Strata (`_13`), D6.2 / D11 Spore Drift (`_40`, D11 now verified by the probe but state-false in the shard), D6.3 / D7.2 Steam Vent (`_41`), D13.2 / E6.hi / E7.hi Colony Pulse (`_6`) — 3 primaries (was 6), 9 secondaries | ❌ reduced; the rest ruled |
| FR-075 E6.hi / E7.hi claimed | red | `sweep6b_aggregate.log`:294, :301 "no preset's verified secondary claim" (1.3827 / 1.3034, `final_E67.log`) | ❌ unchanged, ruled engine-limited (T034) |
| SC-008 | red: 36 of 42, subsets 51 | `sweep6b_aggregate.log`:316 "verified primaries: 39"; :2199 "SUBSET pairs: 0" | ❌ 39 of 42 (ruled cells), subsets ✅ |
| SC-011 | red: 16 claims under their bars | 12 claims under their bars (above) | ❌ reduced |
| SC-012 | red: Choir of Absence take 3 | as FR-033 | ❌ unchanged, ruling B-18 |
| SC-028 | red: E6.hi / E7.hi | as FR-075 | ❌ unchanged, ruled |
| FR-015 sound-space floor | recorded by ruling (159 below floor) | `sweep6b_aggregate.log`:3668 "Pairs 861: min d 0.9499 (Wind Through Basalt vs Swarm Breath, floor 5.7356)  median d 8.6728  max d 29.6196"; :3669 "t_max 2.8678  K 4 ... pairs below floor: 161" (sweep 5: 0.9547 / 8.6876 / 29.6164, t_max 2.8645, 159) | ☑ recorded, not gated |
| FR-017a ruled takes / G2 | recorded by ruling | `default_after_p0.log` "G2: STOP" as at T005 (Glass Well), K = 4 stays; seed twins: Pressure Front t_K 2.1593 (:2319) and Weighted Deep 2.8678 (:2327) over 2.0 as at sweep 5 (2.1656 / 2.8645); Monolith sub twin 4.0778 (:2308) as at sweep 5 (4.0582); Hull Resonance at Weight 1.0 read t_K 4.9080 / sub 4.8355 (`sweep6_aggregate.log`:2327) and was moved to 0.90, after which it is off the list | ☑ recorded, not gated |
| FR-036 | recorded by ruling | as FR-015 | ☑ |
| FR-076 engine field bound | recorded by ruling | no engine field added in 13d (`git_diff_names.txt`: no `vorago_engine.h` field change; `kLoopWakeLaneGain` is a voice constant) | ☑ |
| SC-010 | recorded by ruling | as FR-015 | ☑ |

Out-of-roster secondaries (FR-043): D13.1 Teeming conj NO d 1.3750 (`sweep6_shard_19.log`:105, X at sweep 5); D13.2 Colony Pulse 0.1886 (`final_E67.log`; X); D14.2 Drifting Strata 0.3096 (`sweep6_shard_13.log`; 0 verifiers at sweep 5); D11 Spore Drift state false in the shard (X at sweep 5); D6.2 Spore Drift, D6.3 / D7.2 Steam Vent (X at sweep 5). None verified at sweep 5, none newly lost; each stays a Phase 14 ruling.
