# Disrumpo audit — fix plan

Date: 2026-11-05. Companion report: `specs/disrumpo-audit/report.md` (finding ids D-001…D-173; this plan does not repeat its prose). Probe baseline: `f:/tmp/disrumpo-audit/probe-results.txt` (551 PROBE lines) produced by `specs/disrumpo-audit/probes/audit_probes.cpp.txt`, `audit_followup_probes.cpp.txt`, `audit_band_arrival_probe.cpp.txt`.

## Scope

This plan turns every CONFIRMED and PLAUSIBLE finding of the Disrumpo audit into ordered, self-contained tasks for a coding agent with no memory of the audit. It covers the Disrumpo plugin (`plugins/disrumpo/src/{processor,controller,dsp}`, `resources/editor.uidesc`, `tests/`, presets, docs) and the shared KrateDSP / `plugins/shared` code it exercises (every such task is flagged CROSS-PLUGIN IMPACT with its consumers). Six phases: (1) critical/high correctness and audio defects, (2) medium defects, (3) measured defects not covered above, (4) duplication / spec-conformance / anti-pattern cleanups, (5) test-gap fills, (6) promotion of the audit probes into regression tests. Within a phase tasks are ordered so none depends on a later one; hard prerequisites are named. Nothing in this plan pushes; every task ends in one local commit.

## Mandatory per-task workflow (referenced as "Standard workflow" below)

Every task runs these six steps, in this order, and says so. Task-specific values (test file, TEST_CASE name, build targets, extra gate executables) are given in the task.

1. **Failing test first.** Create/extend the named test file with the named `TEST_CASE`. Build it, run it alone by name (`build/windows-x64-release/bin/Release/disrumpo_tests.exe "<TestName>"`) and confirm it is RED for the reason the finding gives. For PLAUSIBLE findings this run is the verification: if the test passes on current code, stop, mark the finding refuted in `report.md`, keep the test, commit the test only.
2. **Implement the fix** exactly as described (files, functions, before/after). Touch nothing else.
3. **Build, capped:** `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target disrumpo_tests -- -m:2`. Add `--target Disrumpo` (same command, second invocation) when any file under `plugins/disrumpo/src` or `plugins/shared/src` or `dsp/include` changed. Zero warnings; fix every warning before step 4.
4. **Gate, sharded, never serial:** `node tools/run-gate.js --share 4 --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' disrumpo_tests`. If any file under `dsp/include/` or `plugins/shared/` was MODIFIED (new files do not count), first run `node tools/header-consumers.js --tag '[disrumpo]'` and append every executable its last line names to the same `run-gate.js` command. A `GATE RED` is owned and fixed, never excused as pre-existing.
5. **pluginval + clang-tidy, one heavy job at a time:** when plugin sources changed: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Disrumpo.vst3"` (exit code is the verdict). Then `./tools/run-clang-tidy.ps1 -Target disrumpo -BuildDir build/windows-ninja -Jobs 4` (PowerShell; add `-Target dsp` / `-Target shared` when those trees changed). Fix every tidy warning.
6. **Commit:** `git add <files>`, write the message to a file, `git commit -F <message file>` with the attribution line `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`. Never `--amend`, never push.

Flags used below: **CROSS-PLUGIN IMPACT** (edits `dsp/include/krate/dsp/` or `plugins/shared/`; consumers named), **STATE-COMPAT** (changes saved-state layout: must bump `kPresetVersion` in `plugins/disrumpo/src/plugin_ids.h:690`, gate every read on the version, add a migration test with a fixture stream of the previous version; never change a parameter's type at an existing ID), **macOS-SENSITIVE** (fast-math / Apple globals: before pushing run `tools/macos-fastmath-repro.sh`, `node tools/lint-apple-globals.js`, `node tools/check-portability.js`, then `wsl --shutdown`).

Test helpers available in `tests/test_helpers/` (include with `#include "test_helpers/<name>"`; disrumpo_tests already has `${CMAKE_SOURCE_DIR}/tests` on its include path): `vst_param_changes.h` (`ParameterChanges` / `ParamValueQueue` mocks for `IParameterChanges`), `allocation_detector.h` (`AllocationScope` RAII counter over the global operator new hooks), `spectral_analysis.h` (`measureAliasing`, `getHarmonicBins`, `getAliasedBins`, `toDb`), `signal_metrics.h` (`calculateTHD`, `calculateSNR`), `artifact_detection.h` (`ClickDetector`), `test_signals.h` (`generateSine`, `generateImpulse`, `generateWhiteNoise`), `registered_defaults_probe.h` (`parametersStartingOffTheirDefault`), `editor_lifecycle_harness.h` (`exerciseEditorLifecycle`), `shared_editor_views_probe.h`. The probe TU `specs/disrumpo-audit/probes/audit_probes.cpp.txt:296-372` has a `Rig` struct (Processor + setupProcessing + setActive + `render()` block loop + `apply(PList)` parameter pusher) that Phase 6 moves into `plugins/disrumpo/tests/test_support/disrumpo_rig.h`; Phase 1 tests that need a processor render should create that header first (task T1.00).

## Findings → tasks

| Finding | Title (short) | Final severity | Verdict | Task | Phase |
|---|---|---|---|---|---|
| D-001 | Spectral same-family morph re-prepares FFT per sample | critical | CONFIRMED | T1.01 | 1 |
| D-002 | Selecting Spectral / FFT slot allocates on audio thread | critical | CONFIRMED | T1.01 | 1 |
| D-003 | setState mutates live DSP from host thread | high | CONFIRMED | T1.24 | 1 |
| D-005 | Same-family path re-routes params twice per sample | medium | CONFIRMED | T2.20 | 2 |
| D-006 | Node-param change re-pushes all 4 nodes | low | PLAUSIBLE | T4.19 | 4 |
| D-007 | Truncated fftSize → per-sample prepare, silent Spectral | critical | CONFIRMED | T1.01 | 1 |
| D-008 | Transition gain discontinuous at weight 0.4 | high | CONFIRMED | T1.13 | 1 |
| D-009 | Adapter filters prepared at 8x, run at 1x | high | CONFIRMED | T1.06 | 1 |
| D-010 | Quantize maps silence to DC | high | CONFIRMED | T2.07 | 2 |
| D-011 | Integer params floored per node (stages/iterations) | medium | CONFIRMED | T1.01 | 1 |
| D-012 | Stale transition gain can silence band | medium | CONFIRMED | T1.13 | 1 |
| D-013 | NaN latches EnvelopeFollower / sweep centre to 0 Hz | medium | CONFIRMED | T2.17 | 2 |
| D-014 | Drive gate at 1e-4 is a hard discontinuity | medium | CONFIRMED | T2.18 | 2 |
| D-015 | LFO symmetry path reads wavetable[2048] | low | CONFIRMED | T4.17 | 4 |
| D-016 | L and R share one MorphEngine per band | critical | CONFIRMED | T1.05 | 1 |
| D-017 | Oversampling / Oversample Max / crossfade never run | high | CONFIRMED | T1.07 | 1 |
| D-018 | Dry path not phase-compensated (mix notches) | high | CONFIRMED | T1.09 | 1 |
| D-019 | Sweep multiplies band input; intensity >100 % clamped | high | CONFIRMED | T1.11 | 1 |
| D-020 | Band Bypass is a no-op | high | CONFIRMED | T1.07 | 1 |
| D-021 | Centre pan = −3 dB | medium | PLAUSIBLE | T1.08 | 1 |
| D-022 | Crossover Hz dropped when set with fewer bands | high | CONFIRMED | T1.03 | 1 |
| D-023 | Band-count change: no crossfade, re-indexing, stale state | medium | CONFIRMED | T2.08 | 2 |
| D-024 | Allpass jumps while LR4 glides | low | CONFIRMED | T4.18 | 4 |
| D-025 | Global gain/mix block-stepped; output gain wet-only | medium | CONFIRMED | T2.09 | 2 |
| D-026 | No latency/tail reporting | medium | CONFIRMED | T2.10 | 2 |
| D-027 | Intelligent oversampling never runs | high | CONFIRMED | T1.07 | 1 |
| D-028 | Same-family morph is a hard switch of dominant type | high | CONFIRMED | T1.14 | 1 |
| D-029 | Linear1D: C/D share posX with A/B | medium | CONFIRMED | T2.11 | 2 |
| D-030 | Integer morph params summed truncated | medium | CONFIRMED | T1.01 | 1 |
| D-031 | Cross-family adapters keep stale modulated params | medium | CONFIRMED | T2.12 | 2 |
| D-032 | Type change / family switch without reset; Spectral alloc | medium | CONFIRMED | T2.13 | 2 |
| D-033 | Bias semantics differ per type in same-family blend | medium | CONFIRMED | T2.14 | 2 |
| D-034 | 8x unreachable; crossfade double-clocks engine | low | PLAUSIBLE | T1.06 | 1 |
| D-035 | Sweep LFO advances once per block | high | CONFIRMED | T1.10 | 1 |
| D-036 | Sweep envelope fed one sample per block | high | CONFIRMED | T1.10 | 1 |
| D-037 | Sweep band centres hard-coded 100/600/3000/12000 | high | CONFIRMED | T1.11 | 1 |
| D-038 | Sweep intensity mutes out-of-focus bands | medium | PLAUSIBLE | T1.11 | 1 |
| D-039 | Sweep-morph link only in controller | high | CONFIRMED | T1.12 | 1 |
| D-040 | Two link-curve implementations; live one swaps Ease In/Out | low | PLAUSIBLE | T1.12 | 1 |
| D-041 | Host restore loses sweep width/intensity | high | CONFIRMED | T1.04 | 1 |
| D-042 | getState saves modulated sweep values | medium | CONFIRMED | T1.04 | 1 |
| D-043 | Sweep LFO tempo sync ignores host tempo | medium | CONFIRMED | T1.10 | 1 |
| D-044 | setState custom-curve rebuild can loop forever | low | PLAUSIBLE | T4.20 | 4 |
| D-045 | Mod-offset display uses raw pointer over IMessage | low | CONFIRMED | T2.19 | 2 |
| D-046 | setupProcessing wipes restored state | critical | CONFIRMED | T1.03 | 1 |
| D-047 | setActive/prepare wipe mod routings and macros | critical | CONFIRMED | T1.02 | 1 |
| D-048 | getState writes literal 0 for morph mode/smoothing | high | CONFIRMED | T1.03 | 1 |
| D-049 | setState does not restore sweep base atomics | high | CONFIRMED | T1.04 | 1 |
| D-050 | Host setState never re-derives slot params | high | CONFIRMED | T1.15 | 1 |
| D-051 | Preset-browser load collapses crossovers (linear vs log) | high | CONFIRMED | T1.17 | 1 |
| D-052 | Preset writer: active nodes round(norm*2)+2 | high | CONFIRMED | T1.17 | 1 |
| D-053 | Preset writer: routing source ×12 instead of ×13 | high | CONFIRMED | T1.17 | 1 |
| D-054 | setState applies crossovers before setBandCount | medium | CONFIRMED | T1.03 | 1 |
| D-055 | ModSource::Rungler insertion with no migration | medium | CONFIRMED | T2.15 | 2 |
| D-056 | setState adopts stored instance ID (registry collision) | low | CONFIRMED | T2.19 | 2 |
| D-057 | Editor close disables mod overlay / Tier-3 for good | medium | CONFIRMED | T2.19 | 2 |
| D-058 | MIDI learn never completes; preset mappings unpersisted | medium | CONFIRMED | T1.18 | 1 |
| D-059 | Controller::setState allocates unbounded vector | low | CONFIRMED | T4.21 | 4 |
| D-061 | compactNodeSelectors_ dangles after tab switch | high | CONFIRMED | T1.21 | 1 |
| D-062 | Morph pad never follows params; Y sent without setParamNormalized | high | CONFIRMED | T1.20 | 1 |
| D-063 | Restored shape slots reach proxies, not node params | high | CONFIRMED | T1.20 | 1 |
| D-064 | Shadow storage pointer always node A | medium | CONFIRMED | T1.20 | 1 |
| D-065 | DynamicNodeSelector not bound to SelectedNode | medium | CONFIRMED | T1.20 | 1 |
| D-066 | Sweep Morph Link registered with 6 of 8 entries | low | CONFIRMED | T1.12 | 1 |
| D-067 | Proxy knobs do not follow node-param automation | medium | CONFIRMED | T1.20 | 1 |
| D-068 | Global keyboard hook swallows keys | low | PLAUSIBLE | T2.21 | 2 |
| D-069 | Sweep controls / custom-curve editor absent from editor | medium | CONFIRMED | T2.22 | 2 |
| D-070 | Tier-3 fallback disabled after first editor close | low | CONFIRMED | T2.19 | 2 |
| D-071 | Spectrum dividers ignore crossover param changes | medium | CONFIRMED | T2.23 | 2 |
| D-072 | StringListParameter defaults via setNormalized | low | CONFIRMED | T2.24 | 2 |
| D-073 | Shape-slot host text uses displayed type, not owning node | low | CONFIRMED | T4.22 | 4 |
| D-074 | Granular grain state cross-wired (shared) | high | CONFIRMED | T1.23 | 1 |
| D-075 | Stochastic/Temporal/Tube no DC blocker | medium | CONFIRMED | T1.22 | 1 |
| D-076 | FuzzProcessor tone never set (hidden 1.8 kHz LP) | medium | CONFIRMED | T2.01 | 2 |
| D-077 | Tube cascade reuses one TubeStage | medium | CONFIRMED | T2.02 | 2 |
| D-078 | Feedback Curve unrouted, Lim off limits, Thr inverted | medium | CONFIRMED | T2.03 | 2 |
| D-079 | Granular internal drive 5.0 never set | medium | CONFIRMED | T2.04 | 2 |
| D-080 | setType swaps processors with stale state | medium | CONFIRMED | T2.13 | 2 |
| D-081 | Spectral/Granular/Feedback latency unreported | medium | CONFIRMED | T2.10 | 2 |
| D-082 | Bipolar node bias into unipolar Fuzz field | low | CONFIRMED | T2.14 | 2 |
| D-083 | Bitcrush bits 1-4 dead zone | low | CONFIRMED | T2.05 | 2 |
| D-084 | AllpassSaturator reconfigures 9 biquads per sample | low | CONFIRMED | T4.16 | 4 |
| D-085 | Three hand-written v12 codecs drifted | high | CONFIRMED | T1.17 | 1 |
| D-086 | Crossover log mapping copied 4×; two linear call sites | high | CONFIRMED | T1.17 | 1 |
| D-087 | MorphLinkMode curves implemented twice | low | CONFIRMED | T1.12 | 1 |
| D-088 | Sweep freq/width denormalisation copied ~8× | medium | CONFIRMED | T4.01 | 4 |
| D-089 | Morph IDW weights implemented 3× | medium | CONFIRMED | T4.02 | 4 |
| D-090 | formatShapeSlot drifted from DSP mappings | low | CONFIRMED | T4.03 | 4 |
| D-091 | Mod/sweep denormalisations duplicated params/state TU | low | CONFIRMED | T4.01 (done in T1.17) | 4 |
| D-092 | Note-value table re-typed 3× | low | CONFIRMED | T4.04 | 4 |
| D-093 | Distortion type names in 3 tables + self-asserting test | low | CONFIRMED | T4.05 | 4 |
| D-094 | Legacy sweep-only MIDI learn system | low | CONFIRMED | T1.18 | 1 |
| D-095 | Band Bypass does nothing (FR-012) | high | CONFIRMED | T1.07 | 1 |
| D-096 | MIDI CC sweep / learn: no event bus | high | CONFIRMED | T1.18 | 1 |
| D-097 | Cross-family −3 dB midpoint; SC-002 test relaxed | high | CONFIRMED | T1.13 | 1 |
| D-098 | Latency NFR violated and unreported | medium | CONFIRMED | T2.10 | 2 |
| D-099 | Morph smoothing 4× slow; state drops mode/smoothing | medium | CONFIRMED | T1.05 / T1.03 | 1 |
| D-100 | 1D Linear cannot address C and D | medium | CONFIRMED | T2.11 | 2 |
| D-101 | Sweep/band-count registration vs spec | medium | CONFIRMED | T2.24 | 2 |
| D-102 | Type-parameter ranges drift; Bitcrush dead zone | low | CONFIRMED | T2.05 / T4.06 | 2 / 4 |
| D-103 | Global metering not implemented | low | CONFIRMED | T4.07 | 4 |
| D-104 | Docs/CHANGELOG claim undelivered features | low | CONFIRMED | T4.08 | 4 |
| D-105 | Stale 1-8 bands / 2-node comments; vacuous FR-019 tests | low | CONFIRMED | T4.09 | 4 |
| D-106 | Spectral node re-prepares STFT every sample | critical | CONFIRMED | T1.01 | 1 |
| D-107 | Two MorphLinkMode curve implementations (dup) | low | CONFIRMED | T1.12 | 1 |
| D-108 | Processor reports kNoTail | medium | CONFIRMED | T2.10 | 2 |
| D-109 | std::clamp with lo > hi in divider drag | low | PLAUSIBLE | T2.23 | 2 |
| D-110 | Dead adapter members (32 KB ring buffers ×24) | low | CONFIRMED | T4.10 | 4 |
| D-112 | Comments contradicting code | low | CONFIRMED | T4.11 | 4 |
| D-113 | std::isnan in fast-math test TU | low | CONFIRMED | T4.12 | 4 |
| D-114 | Round-trip tests compare getState to getState | high | CONFIRMED | T1.03 | 1 |
| D-115 | No test follows host call order | high | CONFIRMED | T1.03 | 1 |
| D-116 | Oversampling/bypass tests run processBlock only | high | CONFIRMED | T1.07 | 1 |
| D-117 | Controller state paths have zero tests | high | CONFIRMED | T1.17 | 1 |
| D-118 | Factory-preset tests never render presets | medium | CONFIRMED | T5.01 | 5 |
| D-119 | No test with different L/R through morph band | medium | CONFIRMED | T1.05 | 1 |
| D-120 | On-node tests check weights only; SC-002 relaxed | medium | CONFIRMED | T1.13 | 1 |
| D-121 | FR-019 cap tests vacuous | low | PLAUSIBLE | T4.09 | 4 |
| D-122 | No registered-defaults probe / mapping test | medium | CONFIRMED | T5.02 | 5 |
| D-123 | Keyboard/global/visibility tests tautological | medium | CONFIRMED | T5.03 | 5 |
| D-124 | Crossover tests pin missing spacing rule etc. | medium | CONFIRMED | T5.04 | 5 |
| D-125 | Latency tests cannot fail, tagged [performance] | medium | CONFIRMED | T5.05 | 5 |
| D-126 | Sweep LFO/env tests bypass processor clocking | medium | CONFIRMED | T1.10 | 1 |
| D-127 | REQUIRE(true) cases; RT-safety test without detector | low | CONFIRMED | T5.06 | 5 |
| D-128 | DataExchange test [!mayfail] everywhere | low | CONFIRMED | T5.07 | 5 |
| D-129 | No NaN/Inf, 192 kHz, mono, numSamples 0 processor tests | low | CONFIRMED | T5.08 | 5 |
| D-130 | Bitcrush test never compares; CPU budgets relaxed | low | CONFIRMED | T5.09 | 5 |
| D-131 | Hard-coded F:/ preset path in test | low | CONFIRMED | T5.10 | 5 |
| D-132 | setupProcessing recreates MorphEngine; mode lost | medium | CONFIRMED | T1.03 | 1 |
| D-133 | Spectrum feed spliced from disjoint fragments | medium | CONFIRMED | T2.25 | 2 |
| D-134 | Dead maxOversampleFactor_ / crossoverFrequencies_ | low | CONFIRMED | T4.10 | 4 |
| D-135 | Slot-2 stepCount 8 wipes menus | high | CONFIRMED | T1.19 | 1 |
| D-136 | MIDI Learn cannot work (bus, flag, output param) | high | CONFIRMED | T1.18 | 1 |
| D-137 | Sweep-to-morph link editor-only, no begin/endEdit | high | CONFIRMED | T1.12 | 1 |
| D-138 | Morph smoothing 4× slower than set | medium | CONFIRMED | T1.05 / T1.06 | 1 |
| D-139 | Cross-family dips 13-19.5 dB and clicks | high | CONFIRMED | T1.13 | 1 |
| D-140 | Band-count changes click | medium | CONFIRMED | T2.08 | 2 |
| D-141 | Node type change swaps instantly | low | CONFIRMED | T2.13 | 2 |
| D-142 | Stochastic outputs −10 dBFS from silence | high | CONFIRMED | T1.22 | 1 |
| D-143 | Quantize silence → −49 dBFS | medium | CONFIRMED | T2.07 | 2 |
| D-144 | Presets/digital types peak above 0 dBFS | low | CONFIRMED | T3.01 | 3 |
| D-145 | SergeFold/Sine/Triangle nearly silent | medium | PLAUSIBLE | T2.06 | 2 |
| D-149 | uidesc-inventory CScrollView false positive | low | CONFIRMED | T3.02 | 3 |
| D-152 | uidesc overlap ActiveNodes / MorphSmoothing | low | CONFIRMED | T3.03 | 3 |
| D-154 | C4127 from Highway header in KrateDSP build | low | CONFIRMED | T3.04 | 3 |
| D-155 | Stale transition gain via mode switch / Linear1D 3-4 nodes | high | CONFIRMED | T1.13 | 1 |
| D-156 | Rectified_Drums muted after host restore | high | CONFIRMED | T1.15 | 1 |
| D-157 | SineFold and TriangleFold identical at defaults | medium | CONFIRMED | T2.06 | 2 |
| D-158 | SergeFold default Buchla259; 1/folds output cap | medium | CONFIRMED | T2.06 | 2 |
| D-159 | Wavefold level collapse: Smooth default, 8x, pan | medium | CONFIRMED | T2.06 | 2 |
| D-160 | Preset baseline measured host-restore defect | low | CONFIRMED | T6.08 | 6 |
| D-161 | Round-trip probe never measures reloaded level | low | CONFIRMED | T6.08 | 6 |
| D-162 | DCBlocker latches NaN/Inf (shared) | low | CONFIRMED | T2.17 | 2 |
| D-163 | RingSaturation depth-0 bypass discontinuity | medium | CONFIRMED | T2.16 | 2 |
| D-164 | Crossover3Way/4Way allpass Q 0.5 | low | CONFIRMED | T4.13 | 4 |
| D-165 | LFO RNG not MINSTD; same seed every reset | low | CONFIRMED | T4.17 | 4 |
| D-166 | fastTanh returns quiet_NaN() constant | low | CONFIRMED | T4.14 | 4 |
| D-167 | Diode documented UNBOUNDED; -Inf → NaN | low | PLAUSIBLE | T4.15 | 4 |
| D-168 | Serge Fold model labels swapped; default Buchla | medium | PLAUSIBLE | T1.19 | 1 |
| D-169 | Mod params show normalized value with unit | medium | CONFIRMED | T2.24 | 2 |
| D-170 | Expanded panel not tied to Band Count | medium | CONFIRMED | T2.26 | 2 |
| D-171 | Temporal default = Derivative; docs list 3 modes | low | CONFIRMED | T4.06 | 4 |
| D-172 | DataExchange queue opened only in setActive | low | CONFIRMED | T2.19 | 2 |
| D-173 | Bitwise 'Bits' slider dead markup | low | CONFIRMED | T4.10 | 4 |

## Phase 1 — critical / high correctness and audio defects

Order inside the phase: T1.00 → T1.01 → T1.02 → T1.03 → T1.04 → T1.05 → T1.07 → T1.06 → T1.08 → T1.09 → T1.10 → T1.11 → T1.13 → T1.14 → T1.12 → T1.15 → T1.17 → T1.19 → T1.20 → T1.21 → T1.22 → T1.23 → T1.18 → T1.24. Prerequisites are stated per task.

### T1.00 — Test support: processor render rig (prerequisite for every processor-level test)  — 45 min

Findings: none (infrastructure for T1.01+). No plugin source changes, so no pluginval.

1. Create `plugins/disrumpo/tests/test_support/disrumpo_rig.h` (header-only, namespace `Disrumpo::TestSupport`) by moving the `Rig` struct from `specs/disrumpo-audit/probes/audit_probes.cpp.txt:296-372` (constructor `Rig(double sampleRate = 48000.0, int32 maxBlock = 512, bool doSetup = true)`, `setup()`, `deactivate()`, `reactivate()`, `processBlock(...)`, `apply(const PList&)`, `render(inL, inR, outL, outR, block, hook, outHook)`) together with its `PList`/`PChanges` aliases built on `tests/test_helpers/vst_param_changes.h` (`Krate::TestSupport::ParameterChanges`, `ParamValueQueue`) and the helpers `toDb`, `rmsOf`, `peakOf`, `meanOf` (probe lines ~93-110). Also move `setStateBytes(Processor&, const std::vector<uint8_t>&)` (probe :1464) and add `getStateBytes(Processor&) -> std::vector<uint8_t>` (MemoryStream round trip). Add `#include "processor/processor.h"`. Nothing in `src` is edited.
2. Add a smoke `TEST_CASE("DisrumpoRig renders a block and reports level", "[disrumpo][test-support]")` in a new `plugins/disrumpo/tests/unit/test_support_smoke_test.cpp`: Rig at 48 kHz, 1 kHz sine at −12 dBFS, 0.5 s, REQUIRE output RMS finite and > −60 dBFS. Register both the new .cpp in `plugins/disrumpo/tests/CMakeLists.txt` (after `unit/stack_footprint_test.cpp`) and add `integration/` is unaffected. Add the new TU to the IEEE `set_source_files_properties` list in that CMakeLists (line ~181 block).
3. Standard workflow steps 3-4 (target `disrumpo_tests` only), step 6.

### T1.01 — Spectral fftSize / integer fields: no per-sample re-prepare, no audio-thread allocation  — 150 min

Findings: D-001, D-002, D-007, D-106 (critical), D-011, D-030 (medium, same code). Prereq: T1.00.

1. **Failing tests** (extend `plugins/disrumpo/tests/unit/morph_interpolation_test.cpp`):
   - `TEST_CASE("MorphEngine same-family Spectral morph allocates nothing and is not silent", "[disrumpo][morph][rt-safety][D-001]")`: `MorphEngine e; e.prepare(48000.0, 512);` two nodes both `DistortionType::Spectral` with `params.fftSize = 2048`, `setNodes(nodes, 2)`, `setMorphPosition(0.3f, 0.0f)`, `setSmoothingTime(0)`; process 4096 samples of a 1 kHz sine at 0.5 amplitude once to settle, then wrap a second 8192-sample pass in `Krate::TestSupport::AllocationScope scope;` (`tests/test_helpers/allocation_detector.h`) and `REQUIRE(scope.allocationCount() == 0)`; `REQUIRE(rms(last 4096 samples) > 0.001f)`. Today: thousands of allocations and RMS 0.
   - `TEST_CASE("MorphEngine interpolateParams keeps integer fields at the shared node value", "[disrumpo][morph][D-011][D-030]")`: two Fractal nodes `iterations = 4`, weights set via `setMorphPosition(0.6f, 0)` (Linear1D, nodes at x=0 and x=1), then `REQUIRE(e.getBlendedParams().iterations == 4)` — add a `const DistortionParams& getBlendedParams() const` accessor to `MorphEngine` that returns the last `interpolateParams()` result (store it in a member `lastBlendedParams_` set in `processSameFamily`). Repeat for `stages = 2` on two RingSaturation nodes and `fftSize = 2048` on two Spectral nodes at weights 0.3/0.7. Today: 3, 1, 2047.
2. **Fix**
   - `plugins/disrumpo/src/dsp/morph_engine.cpp` `interpolateParams()`: delete the zeroing of `result.stages` (:467), `result.fftSize` and `result.magnitudeBits` (:497-498), `result.iterations` (:505) and the four accumulations at :576, :606, :607, :614. These integer/structural fields now stay at the dominant node's value copied at :419 (same treatment as `spectralMode`, `fractalMode`, etc.).
   - `plugins/disrumpo/src/dsp/distortion_adapter.cpp` Spectral case :893-896: delete the `spectral_.prepare(...)` call. Replace with `spectral_.setActiveFftSize(static_cast<std::size_t>(p.fftSize))` (new, RT-safe). In `DistortionAdapter::prepare` (:84) change `spectral_.prepare(sampleRate, static_cast<size_t>(maxBlockSize))` to `spectral_.prepare(sampleRate, kSpectralMaxFftSize)` with `static constexpr std::size_t kSpectralMaxFftSize = 4096;` (the plugin maximum from `processor_params.cpp:244`), then `spectral_.setActiveFftSize(static_cast<std::size_t>(typeParams_.fftSize))`.
   - **CROSS-PLUGIN IMPACT** (`dsp/include/krate/dsp/processors/spectral_distortion.h`; consumers: `dsp/include/krate/dsp/effects/spectral_delay.h`, `plugins/disrumpo`, `plugins/ruinae`): add `void setActiveFftSize(std::size_t n) noexcept` that is allocation-free: it rounds `n` to a power of two, clamps to `[kMinFFTSize, allocatedFftSize_]`, and if different from `fftSize_` sets `fftSize_/hopSize_/numBins_`, calls `stft_.setActiveSize(n, n/2)` and `overlapAdd_.setActiveSize(n, n/2)` (add these to `dsp/include/krate/dsp/primitives/stft.h` STFT/OverlapAdd: they must only re-index into the buffers allocated for the maximum size and regenerate the window into the pre-sized `window_` vector in place with `Window::generateInto(window_.data(), n, ...)` — add that in-place overload to `dsp/include/krate/dsp/core/window_functions.h` next to `generate`; no `resize`, no new vector), recompute `binFrequencies_[0..numBins_)` in place, and reset frame state. Keep `prepare()` as the allocating path with `allocatedFftSize_ = fftSize`. Add `dsp/tests/unit/processors/spectral_distortion_test.cpp` case `"SpectralDistortion::setActiveFftSize does not allocate and produces output"` under `AllocationScope`.
   - Remove the stale comment "allocates, but only on user param change".
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`, and `dsp_processors_tests` (`--target dsp_processors_tests`); gate `disrumpo_tests dsp_processors_tests` plus whatever `node tools/header-consumers.js --tag '[disrumpo]'` names (expect `ruinae_tests`, `dsp_effects_tests`); pluginval; tidy `-Target disrumpo` and `-Target dsp`. **macOS-SENSITIVE** (shared header edit): run the three proxies. Commit.

### T1.02 — Activation and prepare must not wipe modulation routings/macros  — 60 min

Findings: D-047 (critical). Prereq: T1.00.

1. **Failing test** (new `plugins/disrumpo/tests/integration/processor_lifecycle_state_test.cpp`, register in tests/CMakeLists.txt after `integration/processor_audio_output_test.cpp`): `TEST_CASE("Mod routings and macros survive setActive(false)/setActive(true)", "[disrumpo][lifecycle][D-047]")`: Rig; `apply({{makeRoutingParamId(0,0), 1.0/13.0 /*LFO 1*/}, {makeRoutingParamId(0,1), (double)ModDest::kInputGain/(ModDest::kTotalDestinations-1)}, {makeRoutingParamId(0,2), 1.0}})`, plus macro 0 value 0.8; `getStateBytes` → A; `r.reactivate()`; `getStateBytes` → B; `REQUIRE(A == B)`. Also REQUIRE the routing bytes in B are non-zero (decode offset of routing 0 source byte from the v12 layout written in `processor_state.cpp:299-307`). Second section: render 0.5 s with LFO1→InputGain active before and after reactivate and REQUIRE the block-RMS variance after reactivation is > 1 dB (modulation still moving).
2. **Fix** — **CROSS-PLUGIN IMPACT** (`dsp/include/krate/dsp/systems/modulation_engine.h`; consumers: `plugins/disrumpo`, `plugins/ruinae` (`ruinae_engine.h:197`), `dsp/include/krate/dsp/systems/ecosystem_engine.h`, `seraphis_macro_matrix.h`): split `reset()` (:127-161). New `void resetSources() noexcept` = lines 128-150 (source resets, offsets, smoothers, `wasPlaying_`). `reset()` becomes `resetSources()` + the routing/macro clears (:152-160), so Ruinae's semantics are unchanged. `prepare()` (:123) calls `resetSources()` instead of `reset()`. `plugins/disrumpo/src/processor/processor.cpp:164` → `modulationEngine_.resetSources();`.
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`, `dsp_systems_tests`; gate `disrumpo_tests dsp_systems_tests` + header-consumers output (expect `ruinae_tests`, `seraphis_tests`); pluginval; tidy disrumpo + dsp; macOS proxies (shared header). Commit.

### T1.03 — setupProcessing / setState must preserve the patch (nodes, crossovers, mode, smoothing, order)  — 180 min

Findings: D-046 (critical), D-048, D-114, D-115, D-022, D-054, D-132, D-099 (state half). Prereq: T1.00. **STATE-COMPAT: no layout change** (the morphMode/morphSmoothing bytes already exist at `processor_state.cpp:319/323`; this task only stops writing literal zeros), so no version bump — but add the migration-style fixture test below.

1. **Failing tests** (`processor_lifecycle_state_test.cpp` from T1.02):
   - `TEST_CASE("State restored before setupProcessing survives it byte for byte", "[disrumpo][lifecycle][D-046][D-115]")`: `Rig r(48000.0, 512, /*doSetup*/false)`; load `plugins/disrumpo/resources/presets/Morph/All_Types_Morph.vstpreset` component chunk (reuse the chunk reader from `factory_preset_validation_test.cpp:98-135`, replacing its F:/ path with `DISRUMPO_RESOURCES_DIR "/presets"`); `setStateBytes`; `r.setup()`; `getStateBytes` → B; REQUIRE B equals the original component bytes except the KRAT trailer (compare `B.size()-12` bytes). Today: node types collapse to SoftClip, crossovers to 112/632/3556.
   - `TEST_CASE("Sample-rate change keeps nodes, crossovers, morph mode and smoothing", "[disrumpo][lifecycle][D-046][D-132]")`: active Rig; apply band-0 node-0 type Tube (`makeNodeParamId(0,0,kNodeType)` = 2/25), `kBandMorphMode` = 1.0 (Radial), `kBandMorphSmoothing` = 0.4, crossover 0 = 0.5 norm; `r.deactivate(); r.sr = 96000; r.setup();` then REQUIRE `getStateBytes` equals the pre-change bytes (sampleRate is not in the stream) and REQUIRE a 440 Hz render differs from a fresh-default render by > 6 dB RMS (Tube vs SoftClip).
   - In `plugins/disrumpo/tests/unit/preset/serialization_round_trip_test.cpp` `"MorphMode values round-trip"` section (:875-899): parse `stream1` and REQUIRE each band's morphMode byte == `b % 3` and, after changing `writeNonDefaultV6Preset` line 228 to write `50.0f`, the smoothing float == 50.0f.
   - `TEST_CASE("setState with 4 bands into a 2-band instance restores all crossovers", "[disrumpo][lifecycle][D-022][D-054]")`: Rig; `apply({{kBandCountId, 1.0/3.0}})` (2 bands); setState a stream from a 4-band Rig with crossovers set to norms 0.4/0.6/0.8; REQUIRE `getStateBytes` crossover floats equal the saved ones within 0.1 Hz.
2. **Fix**
   - `plugins/disrumpo/src/processor/processor.h:165-172` `BandMorphCache`: add `MorphMode morphMode = MorphMode::Linear1D;` and `float morphSmoothingMs = 0.0f;`.
   - `processor_params.cpp:958-968`: write `bandMorphCache_[band].morphSmoothingMs = timeMs;` and `bandMorphCache_[band].morphMode = mode;` before the setter calls.
   - `processor_state.cpp:319` write `static_cast<int8>(cache.morphMode)`; `:323` write `cache.morphSmoothingMs`. In setState :827-831 store both into the cache before calling the setters.
   - `processor.cpp:105-128` setupProcessing: delete the default-node block (:111-122). After `bandProcessors_[i].prepare(sampleRate_)` push the cache: `setMorphEnabled(true)`, `setMorphNodes(cache.nodes, cache.activeNodeCount)`, `setMorphPosition(cache.morphX, cache.morphY)`, `setMorphMode(cache.morphMode)`, `setMorphSmoothingTime(cache.morphSmoothingMs)`. Move the one-time default initialisation of `bandMorphCache_` (SoftClip nodes at the four corners with `kDefaultCommon`) into the `Processor` constructor (`processor.cpp` ctor near line 40) so a fresh instance still starts from defaults.
   - Crossovers: in `plugins/disrumpo/src/dsp/crossover_network.h` `prepare()` (:79-94) replace `initializeLogarithmicDistribution();` with `if (!frequenciesInitialized_) { initializeLogarithmicDistribution(); frequenciesInitialized_ = true; } else { for (int i = 0; i < kMaxBands - 1; ++i) crossovers_[i].setCrossoverFrequency(crossoverFrequencies_[i]); }` and make the LR4s snap: call `crossovers_[i].reset()`-equivalent snap so the smoother does not glide from 1 kHz (CrossoverLR4::prepare already snaps to its stored frequency, so set each crossover's frequency BEFORE calling `crossover.prepare(sampleRate)` in the loop at :84-86). Add `bool frequenciesInitialized_ = false;`.
   - `setCrossoverFrequency` (:127-130): change the guard to `if (index < 0 || index >= kMaxBands - 1) return;` so the value is always stored in `crossoverFrequencies_[index]` and pushed to `crossovers_[index]`; keep the allpass update loop. In `redistributeCrossovers` (:274-297) the `oldBandCount <= 1` branch must only call `initializeLogarithmicDistribution()` when `!frequenciesInitialized_`; otherwise keep the stored values and call `updateAllpassCoefficients()`. `redistributeIncreasing` must keep stored values for indices that already hold a non-default value (track with a `std::array<bool, kMaxBands-1> frequencyWasSet_` set by `setCrossoverFrequency`).
   - `processor_state.cpp`: move the `setBandCount(bandCount)` calls (:462-463) ABOVE the crossover loop (:450-459). Also store restored bypass into the band processor (`bandProcessors_[b].setBypassed(bs.bypass)` after :442) so T1.07 works on restore.
   - `BandProcessor::prepare` (`band_processor.h:123-125`): after creating the engine, re-apply `morphEngine_->setMode(cachedMorphMode_)` and `setSmoothingTime(cachedSmoothingMs_)`; cache both in `setMorphMode`/`setMorphSmoothingTime` (:260-287).
3. Update `unit/crossover_network_test.cpp:178-220` expectations if the preserved-value behaviour changes them (it should not: FR-011a asks for preservation). Standard workflow: build `disrumpo_tests`, `Disrumpo`; gate `disrumpo_tests`; pluginval; tidy disrumpo. Commit.

### T1.04 — Sweep width/intensity base atomics restored by setState; getState writes base values  — 45 min

Findings: D-049, D-041 (high), D-042 (medium). Prereq: T1.00.

1. **Failing test** (`processor_lifecycle_state_test.cpp`): `TEST_CASE("Sweep width/intensity/frequency survive setState + one block and are saved unmodulated", "[disrumpo][lifecycle][D-049][D-042]")`: Rig; apply `kSweepEnable 1, kSweepWidth 0.9, kSweepIntensity 0.8, kSweepFrequency 0.5, kSweepLFOEnable 1, kSweepLFODepth 1`; render 0.2 s; `getStateBytes` → A; decode the three sweep floats (offsets per `processor_state.cpp:85-91`): REQUIRE freq == 0.5, width == 0.9, intensity == 0.8 within 1e-5 (today freq is the LFO-modulated snapshot). Fresh Rig; `setStateBytes(A)`; render one block; `getStateBytes` → B; REQUIRE B's sweep floats equal A's (today width 0.286, intensity 0.25).
2. **Fix** `processor_state.cpp:485-488`: add `baseSweepWidthNorm_.store(sweepWidthNorm, ...)` and `baseSweepIntensityNorm_.store(sweepIntensityNorm, ...)`. `processor_state.cpp:87-91`: write `normalizeSweepFrequency(baseSweepFrequency_.load())`, `baseSweepWidthNorm_.load()`, `baseSweepIntensityNorm_.load()` instead of the `sweepProcessor_` getters.
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`; gate; pluginval; tidy. Commit.

### T1.05 — One MorphEngine per channel (stereo independence)  — 180 min

Findings: D-016 (critical), D-119, half of D-138/D-099 (double-advanced smoothers). Prereq: T1.03.

1. **Failing tests** (`plugins/disrumpo/tests/integration/processor_stereo_test.cpp`, new, register in CMakeLists + IEEE list): `TEST_CASE("Processor keeps L and R independent through a morph band", "[disrumpo][stereo][D-016]")`: Rig 48 kHz, 1 band (`kBandCountId 0.0`), node-0 type Tape (`3/25`), drive 0.5 norm, mix 1; L = 1031 Hz sine at −6 dBFS, R = zeros, 1 s; REQUIRE RMS(outR over last 0.5 s) < −90 dBFS (today −13.7 dBFS). Second section: L = sine, R = −sine; REQUIRE max|outL + outR| < 1e-4. Repeat the first section for FeedbackDist (18/25) and Formant (21/25). Also `unit/morph_band_integration_test.cpp` new `TEST_CASE("BandProcessor morph smoothing reaches 99 % in the configured time", "[disrumpo][morph][D-138]")`: BandProcessor prepared at 48 kHz, smoothing 10 ms, step X 0→1, process stereo pairs; REQUIRE `getMorphEngine()->getSmoothedX() >= 0.99f` after 480 samples and `< 0.99f` after 240 (replace the `REQUIRE(true)` at :271/:288 with these).
2. **Fix** (`plugins/disrumpo/src/dsp/band_processor.h`): replace `std::unique_ptr<MorphEngine> morphEngine_` (:808) with `std::unique_ptr<MorphEngine> morphEngineL_, morphEngineR_;` and `DistortionAdapter distortion_` (:803) with `distortionL_, distortionR_`. Every setter that forwarded to `morphEngine_` (`setMorphNodes` :247, `setMorphMode` :260, `setMorphPosition` :270, `setMorphSmoothingTime` :283, the mod-offset setters :296-316, `setDistortionType` :208, `setDistortionCommonParams` :220, `reset` :136-153, `prepare` :111-125, `beginBlockModulation`/`endBlockModulation` :438-463) applies to both. `process()` :485-494 and `processOversampledBlock()` :752-757: L through the L instances, R through the R instances. Keep `getMorphEngine()` returning the L engine (tests use it). `recalculateOversampleFactor` reads the L engine. **Cursor smoother:** both engines receive identical `setMorphPosition`, so they stay in step; the per-channel single advance restores the configured smoothing time (remaining 8× factor is fixed in T1.06). Memory: each MorphEngine holds 5 adapters; doubling is acceptable only after T4.10 removes the dead 32 KB ring buffers — do T4.10's adapter-member deletion inside this task (delete `inputRingBuffer_`, `outputRingBuffer_`, `ringWritePos_`, `ringReadPos_`, `isBlockBased_`, `saturation_`, `tubeShaper_`, `tapeShaper_` from `distortion_adapter.h:370,396-399,439-446` and their uses at `distortion_adapter.cpp:34,40-43,89-92,106,132-135,149-158`), then re-run `unit/stack_footprint_test.cpp`.
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`; gate; pluginval; tidy. Commit.

### T1.07 — Wire the block path: oversampling, Oversample Max, band bypass actually run  — 240 min

Findings: D-027, D-017, D-116, D-095, D-020 (high). Prereq: T1.05.

1. **Failing tests** (`processor_audio_output_test.cpp`):
   - `TEST_CASE("Processor HardClip aliasing drops with Oversample Max 4x vs 1x", "[disrumpo][oversampling][D-027]")`: Rig 48 kHz, 1 band, node-0 HardClip (1/25), drive 1.0 norm, tone 1.0 norm; 1031.25 Hz sine at −6 dBFS, 1 s; run once with `kOversampleMaxId 0.0` (1x) and once with `2.0/3.0` (4x); use `Krate::TestSupport::measureAliasing`-style bins from `tests/test_helpers/spectral_analysis.h` (`getHarmonicBins`, `getAliasedBins`, `sumBinPower`, `toDb`) on the last 32768 samples; REQUIRE nonharmonic power at 4x is ≥ 12 dB below 1x. Today 0 dB (both −43.3).
   - `TEST_CASE("Band bypass passes the crossover band unchanged", "[disrumpo][bypass][D-095]")`: 4 bands, all nodes HardClip drive 1.0, `kBandBypass` = 1 on every band, mix 1; render 220 Hz at −12 dBFS; REQUIRE max|out − render with all node drives 0| < 1e-4 (today 0.63 difference).
2. **Fix**
   - `plugins/disrumpo/src/processor/processor.cpp:467-504`: replace the per-sample band loop. Per block: run the crossover per sample into pre-allocated per-band block buffers `std::array<std::array<float, kMaxBlock>, kMaxBands> bandBufL_, bandBufR_` (members sized at `setupProcessing` from `setup.maxSamplesPerBlock`, processing in chunks of `BandProcessor::kMaxBlockSize` = 2048 when the host block is larger); then for each band `bandProcessors_[b].processBlock(bandBufL_[b].data(), bandBufR_[b].data(), n)`; then a per-sample sum + output stage identical to :500-503. Muted/non-contributing bands (`shouldBandContribute`, :563-576) are still processed, their buffers just not summed.
   - `band_processor.h` `processBlock` (:519-523): bypass stays a bit-transparent early return. `processWithFactor` (:627-636): the drive gate must be morph-aware: `const bool bypassDistortion = morphEnabled_ ? false : (distortionL_.getCommonParams().drive < 0.0001f);`.
   - `processBlockWithCrossfade` (:653-690) runs the same engines twice (D-034). Replace the factor crossfade with a 5 ms equal-gain output dip: on factor change set `pendingFactor_`, ramp a `factorSwitchGain_` 1→0 over 5 ms (`kFactorSwitchMs = 5`), switch `currentOversampleFactor_` at the bottom, call `morphEngineL_/R_->resetState()` (new: reset adapters' filter state only, keep params), ramp back 1. Delete `processBlockWithCrossfade`, `crossfadeActive_`, `crossfadeOldFactor_`, `crossfadeProgress_` and `kCrossfadeDurationMs` (:51). Update `dsp/oversampling_crossfade_tests.cpp` to assert the dip (no sample jump > 3× steady state during the switch) instead of the old crossfade API.
   - `setMaxOversampleFactor` default: `maxOversampleFactor_` in `band_processor.h:836` initialises to 4 (`kDefaultOversampleFactor` renamed meaning) so the band default equals the processor default (D-134 half).
3. Extend `dsp/oversampling_single_type_tests.cpp` cases 171-209 and 247-289 so they go through `Processor::process` via the Rig (keep the BandProcessor versions as unit coverage). Standard workflow: build `disrumpo_tests`, `Disrumpo`; gate; pluginval; tidy. Commit.

### T1.06 — Run every adapter at the rate it is clocked (end the 8× prepare mismatch)  — 240 min

Findings: D-009 (high), D-034, remaining part of D-138/D-099 (smoothing time), D-159's 8×/double-clock share. Prereq: T1.07.

1. **Failing tests** (`processor_audio_output_test.cpp`): `TEST_CASE("Processor tone filter cutoff matches the Tone parameter", "[disrumpo][tone][D-009]")`: Rig 48 kHz, 1 band, node SoftClip drive 0.01 norm (linear region), mix 1, `kNodeTone` = 1.0 (8000 Hz); feed 4 kHz sine at −30 dBFS; REQUIRE output level within 1 dB of input level (one-pole at 8 kHz attenuates 4 kHz by about −1 dB; today about −12 dB). Second section with tone 4000 Hz and a 1 kHz tone: REQUIRE within 0.5 dB of −0.26 dB. `unit/morph_band_integration_test.cpp` smoothing case from T1.05 must now pass at exactly 10 ms ± 30 % (REQUIRE ≥ 0.99 after 480 samples and < 0.99 after 336 samples).
2. **Fix design (decided here, do not re-litigate):**
   - Cap the factor at 4. `band_processor.h:47` `kMaxOversampleFactor = 4`; delete `oversampler8xInner_` (:105-107, member :832), `processWithOversampling8x` (:729-742) and the `factor == 8` branch (:641-642). `processor_params.cpp:349-361`: the "8x" list entry stays registered for host compatibility but maps to 4 (`kOversampleFactors[] = {1, 2, 4, 4}`); `parameter_registration.cpp:89` keeps the string so IDs/indices do not move (STATE-COMPAT: the normalized value 1.0 keeps meaning "max", now 4x — note this in CHANGELOG under T4.08).
   - Split adapter preparation into allocation and rate. `DistortionAdapter`: `prepare(double maxRate, int maxBlock)` keeps allocating (called once from `BandProcessor::prepare` with `sampleRate * 4`), and new `void setProcessingRate(double rate) noexcept` (allocation-free) sets `sampleRate_ = rate` and re-prepares coefficient-only members: `toneFilter_.prepare(rate)`+`setCutoff`, `dcBlocker_.prepare(rate, 10.0f)`, `tapeHfFilter_`, `bodyFilter_`, `wavefoldSmoothFilter_` (all `OnePoleLP`/`DCBlocker`, whose `prepare` only recomputes coefficients — verify by reading `one_pole.h:135-140` and `dc_blocker.h`), then calls `setProcessingRate(rate)` on each KrateDSP processor member. For each member decide by reading its `prepare()`: if it allocates (vector resize / FFT setup — `SpectralDistortion`, `GranularDistortion`, `FormantDistortion` must be checked), leave it prepared at the max rate and pin that type's `getRecommendedOversample` (`distortion_types.h:167-203`) to 1 so it is only ever clocked at the host rate — then its allocation rate is the host rate: prepare those members with `sampleRate` not `sampleRate*4`. If it does not allocate, call its `prepare(rate, ...)` from `setProcessingRate`. Add a `dsp_processors_tests` case per processor whose `prepare` is called from the RT path: `"<Processor>::prepare re-run under AllocationScope allocates nothing"`.
   - `MorphEngine`: add `setProcessingRate(double rate)` that re-configures `smootherX_/Y_` (`morph_engine.cpp:51-52` pattern) and forwards to all 5 adapters. `BandProcessor::processWithFactor` calls `morphEngineL_/R_->setProcessingRate(sampleRate_ * factor)` and `distortionL_/R_.setProcessingRate(...)` only when the factor changed (track `preparedFactor_`), inside the gain dip from T1.07.
   - `BandProcessor::prepare` (:111,:124): `prepare(sampleRate * 4, maxBlockSize_ * 4)` for allocation, then `setProcessingRate(sampleRate * currentOversampleFactor_)`.
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`, `dsp_processors_tests`; gate `disrumpo_tests dsp_processors_tests` + header-consumers output if any `dsp/include` file changed (e.g. a `setProcessingRate` added to a shared processor → **CROSS-PLUGIN IMPACT**, name consumers from the grep table in T1.01/T1.22); pluginval; tidy disrumpo + dsp; macOS proxies if shared headers changed. Re-run the promoted tone/adapter-vs-processor probes (T6.02) and compare with baseline (`tone-response`, `adapter-vs-processor`: target proc within 1 dB and centroid ratio 0.85-1.15 for all 26 types). Commit.

### T1.08 — Centre pan must be unity (verify first, then fix)  — 60 min

Findings: D-021 (medium, PLAUSIBLE — one skeptic cited spec FR-022; the measured −3.01 dB at neutral settings against FR-BAND-004's unity reference wins, but the test is the verification). Prereq: T1.07.

1. **Verification/failing test** (`processor_audio_output_test.cpp`): `TEST_CASE("Neutral settings are unity gain", "[disrumpo][unity][D-021]")`: Rig, 1..4 bands, all node drives 0 (adapter passthrough), pan 0, gain 0 dB, mix 1; 1 kHz −12 dBFS; REQUIRE |level_out − level_in| < 0.1 dB for every band count. If it passes on current code the finding is refuted (it will not: probe `unity:processor` reads −3.010 dB).
2. **Fix** `band_processor.h:499-511` and `applyOutputStage` (:762-780): replace the mono equal-power law with a stereo balance law that is unity at centre: `leftCoeff = std::min(1.0f, 1.0f - pan)`, `rightCoeff = std::min(1.0f, 1.0f + pan)` (pan ∈ [−1,1]). Update `unit/band_processing_test.cpp:55-70` ("0dB gain is unity" expects 1.0 now), :129-144 (centre = 1.0), :112-127/:147-162 (full pan: near side 1.0, far side 0.0), :164-190 (replace constant-power assertion with "centre is unity, extremes are 1/0"), and `integration/band_management_integration_test.cpp:197,233`. **STATE-COMPAT note:** no layout change, but every preset gets +3 dB — fold this into the preset re-levelling in T3.01, do not add a makeup elsewhere.
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`; gate; pluginval; tidy. Commit.

### T1.09 — Phase-compensate the dry path  — 90 min

Findings: D-018 (high). Prereq: T1.08.

1. **Failing test** (`processor_audio_output_test.cpp`): `TEST_CASE("Global mix 50 % does not notch the crossover frequencies", "[disrumpo][mix][D-018]")`: Rig 48 kHz, 4 bands default crossovers, all drives 0, pan 0, `kGlobalMixId` = 0.5; 0.3 s pre-roll, then unit impulse; take a 65536-point FFT of the output (`spectral_analysis.h` helpers) and REQUIRE the magnitude over 30 Hz-18 kHz stays within ±0.5 dB of the 1 kHz value (today −16.6 dB at 114 Hz and 2 kHz). Repeat with 2 bands.
2. **Fix** `plugins/disrumpo/src/dsp/crossover_network.h`: add `float processDryCompensation(float x) noexcept` that runs `x` through `numBands_ - 1` second-order allpasses `dryAllpasses_[kMaxBands-1]` (`Krate::DSP::Biquad`, `FilterType::Allpass`, `kAllpassQ`, configured at the crossover frequencies in `setCrossoverFrequency` and `updateAllpassCoefficients`, reset in `reset()`), returning `x` unchanged when `numBands_ == 1`. `processor.cpp:502-503`: `outputL[n] = crossoverL_.processDryCompensation(inputL[n]) * dryMix + sumL * outputGainLinear * wetMix;` (same for R).
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`; gate; pluginval; tidy. Commit.

### T1.10 — Sweep LFO and envelope follower run per sample; LFO follows host tempo  — 90 min

Findings: D-035, D-036 (high), D-043, D-126 (medium). Prereq: T1.00.

1. **Failing tests** (new `plugins/disrumpo/tests/integration/processor_sweep_test.cpp`, register + IEEE list):
   - `TEST_CASE("Sweep LFO rate is independent of block size", "[disrumpo][sweep][D-035]")`: Rig; `kSweepEnable 1, kSweepLFOEnable 1, kSweepLFODepth 1, kSweepLFORate` = norm for 2 Hz (`(ln 2 − ln 0.01)/(ln 20 − ln 0.01)` = 0.6951), waveform sine; render 2 s at block 64 and at block 1024, collecting `kSweepModulatedFrequencyOutputId` from `outputParameterChanges` per block (Rig's `outHook`); REQUIRE 4 ± 0.5 upward zero crossings of `log2(f/1000)` at both block sizes (today 0).
   - `TEST_CASE("Sweep envelope follower attack is block-size independent", "[disrumpo][sweep][D-036]")`: `kSweepEnvEnable 1, kSweepEnvSensitivity 1, kSweepEnvAttack 0.0909` (10 ms); 0.5 s silence then 1 kHz at −3 dBFS; REQUIRE the modulated-frequency output reaches 63 % of its final octave shift within 40 ms at block 512 (today 320 ms).
   - `TEST_CASE("Sweep LFO tempo sync follows ProcessContext tempo", "[disrumpo][sweep][D-043]")`: `kSweepLFOSync 1`, note value 1/4 (index 13 → `13/29`), ProcessContext tempo 90 BPM with `kTempoValid`; REQUIRE period 0.667 s ± 5 % (needs Rig `processBlock` to accept a `ProcessContext*` — add an optional member `ctx` to the Rig).
2. **Fix** `processor.cpp:339-355`: inside a per-sample loop over `data.numSamples`, feed `sweepEnvelope_.processSample((inputL[n]+inputR[n])*0.5f)` and advance `sweepLFO_.process()` every sample, keeping the LAST values for the frequency computation (`lfoValue` = last sample's output). Before the loop, when `data.processContext && (state & kTempoValid)`, call `sweepLFO_.setTempo(static_cast<float>(data.processContext->tempo))`.
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`; gate; pluginval; tidy. Commit.

### T1.11 — Sweep targets real band centres and scales drive, not level  — 120 min

Findings: D-037 (high), D-019 (high), D-038 (medium, PLAUSIBLE: one skeptic called the attenuation spec-mandated; the FR-SWEEP-001 wording "focused distortion intensity" and the near-silent factory Sweep presets decide it — the failing test is the verification). Prereq: T1.10, T1.06.

1. **Failing tests** (`processor_sweep_test.cpp`):
   - `TEST_CASE("Sweep leaves out-of-focus bands passing clean", "[disrumpo][sweep][D-019][D-038]")`: 4 bands, nodes SoftClip drive 0.3 norm; 11 kHz sine −12 dBFS; sweep on, centre 100 Hz, intensity 100 %; REQUIRE level(on) ≥ level(off) − 1 dB (today −125 dB). Second section: sweep centred on band 2's geometric centre with intensity 200 %; REQUIRE THD of band 2 (`calculateTHD`, `signal_metrics.h`) with sweep on > THD off (drive doubled), and level within 3 dB.
   - `TEST_CASE("Sweep follows the actual crossover layout", "[disrumpo][sweep][D-037]")`: 1 band, sweep on at defaults, 1 kHz −12 dBFS: REQUIRE level within 1 dB of sweep-off (today −91 dB). 4 bands with crossovers moved to 200/1500/6000 Hz, sweep centred at sqrt(1500*6000) ≈ 3000 Hz: REQUIRE band 2's drive multiplier (expose `BandProcessor::getSweepDriveMultiplier()`) is the maximum of the four.
2. **Fix** — semantics: the sweep intensity is a per-band DRIVE multiplier in [0, 2]. `band_processor.h:196-198`: rename to `setSweepIntensity(float m)` clamping to `[0, 2]`, store in `targetSweep_`, and REMOVE the `left *= sweep` input multiply (:481-482, :747-749). Pass the smoothed multiplier into the engines each sample: `morphEngineL_/R_->setSweepDriveMultiplier(sweep)` (new; `MorphEngine::processSameFamily` multiplies `cp.drive` by it after the mod offset at :713, and `processCrossFamily` does the same at :730, clamped to [0,10]; the non-morph `distortionL_/R_` path scales `drive` the same way in `beginBlockModulation`). `processor.cpp:421-424`: delete `kBandCenterFreqs`; compute centres per block from `crossoverL_.getCrossoverFrequency(i)`: band b centre = `sqrt(lo*hi)` with lo = 20 Hz or crossover(b−1), hi = 20 kHz or crossover(b); for 1 band use the sweep centre itself (multiplier = intensity). `processor.cpp:437-439` (sweep off) passes 1.0. Update `integration/sweep_band_intensity_test.cpp:190-216` and `unit/morph_band_integration_test.cpp:146-176` to assert drive scaling (THD rises) instead of level attenuation.
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`; gate; pluginval; tidy. Commit.

### T1.13 — Cross-family blend: continuous equal-power law, gains refreshed on-node  — 150 min

Findings: D-139, D-097, D-008, D-155 (high), D-012, D-120 (medium). Prereq: T1.05.

1. **Failing tests** (`unit/morph_transition_test.cpp`):
   - `TEST_CASE("Cross-family sweep is level-flat and click-free", "[disrumpo][morph][SC-002][D-139]")`: MorphEngine at 48 kHz, nodes SoftClip and SineFold (drive 0.3 each via commonParams), Linear1D, 220 Hz −12 dBFS, cursor x 0→1 over 2 s at block rate; per 10 ms window REQUIRE |RMS − straight line between start and end dB| ≤ 1 dB (today −12.95 dB dip) and `ClickDetector` (`artifact_detection.h`) count == 0 (today 1); repeat with 4 cross-family nodes Planar2D (0,0)→(1,1) (today 54 clicks, −19.5 dB).
   - `TEST_CASE("Transition gain is continuous in weight", "[disrumpo][morph][D-008]")`: expose `MorphEngine::transitionGainForWeight(float)` as a public static; REQUIRE |g(0.3999) − g(0.4001)| < 0.01 and g monotone non-decreasing on a 1e-4 grid.
   - `TEST_CASE("Cursor parked on a node after a mode switch plays that node at unity", "[disrumpo][morph][D-155][D-012]")`: 4 cross-family nodes, Planar2D, cursor (0, 0.9), process 2000 samples, `setMode(Linear1D)`, process 2000; REQUIRE level within 0.5 dB of a single SoftClip adapter alone (today −23.5 dB). Section: fresh Linear1D 4-node engine at x=0 → within 0.5 dB (today −3.01 dB).
   - `approval/morph_artifact_test.cpp:383-460`: change the setup to two cross-family nodes (SoftClip + SineFold, drive 0.5) and restore `REQUIRE(diffMid < 1.0f)`. `unit/morph_interpolation_test.cpp:258-306`: replace `ratio < 3.0f` with a dB check `< 1.0 dB` and remove the `minLevel <= 0.001` skip.
2. **Fix** `morph_engine.cpp`: (a) delete `calculateTransitionGain` and `transitionGains_` (:352-379, :267-270, :748-749, header :239). (b) `processCrossFamily` (:743-757): `output += nodeOutput * crossGain_[i]` where `crossGain_[i] = sqrt(weights_[i])` normalised so `Σ crossGain_[i]² = 1` (the normalised weights already sum to 1, so `sqrt(w_i)` satisfies this; compute once per sample in `calculateMorphWeights`). (c) Nodes below `kWeightThreshold` keep running but contribute 0 — remove the `continue` at :745 so filter/delay state does not go stale (D-139 part); keep the skip only when `activeNodeCount_` excludes the node. (d) On-node early returns (:188-192, :220-224): instead of `return`, `break` out of the switch so the threshold/renormalise/gain block runs and yields one-hot weights with gain 1 for that node, 0 for the rest. (e) `setMode` (:107-113) and `setNodes` (:131-152) already recalculate; nothing else needed once (d) is in.
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`; gate; pluginval; tidy. Commit.

### T1.14 — Same-family interpolation only for identical types; different types blend in parallel  — 120 min

Findings: D-028 (high). Prereq: T1.13.

1. **Failing test** (`unit/morph_transition_test.cpp`): `TEST_CASE("Same-family different-type morph has no step at the midpoint", "[disrumpo][morph][D-028]")`: nodes SoftClip + HardClip (Saturation family), drive 4 each, 220 Hz −6 dBFS, slow cursor 0.4→0.6 over 1 s; REQUIRE `ClickDetector` count 0 and max |y[n]−y[n−1]| within the sweep ≤ 2× the steady-state max at either end. Repeat Chaos + Formant (Experimental). Today: step at 0.5.
2. **Fix** `morph_engine.cpp:385-397` `isSameFamily()` → rename to `isSameType()`: return true only when every active node has the same `DistortionType`. `process()` (:675-682) routes identical-type sets to `processSameFamily` (single processor, interpolated params — still correct) and everything else to the parallel path from T1.13. Rename `allSameFamily_` → `allSameType_`. Update `unit/morph_interpolation_test.cpp:349-404` family-detection tests to the new predicate and add a case that SoftClip+Tube is NOT the single-processor path. Remove the now-wrong "Parallel blend with output crossfade" vs "Transfer function interpolation" comments in `distortion_types.h:297-347` to match (all cross-type morphs are parallel).
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`; gate; pluginval; tidy. Commit.

### T1.12 — Sweep→morph link lives in the processor, one curve table, modes persisted  — 180 min

Findings: D-137, D-039 (high), D-040 (PLAUSIBLE), D-087, D-107, D-066 (low). Prereq: T1.10. **STATE-COMPAT** (new per-band bytes) and **macOS-SENSITIVE: no** (no shared headers).

1. **Failing tests** (`processor_sweep_test.cpp`):
   - `TEST_CASE("Band morph X link follows the processor sweep without a controller", "[disrumpo][sweep-link][D-137]")`: Rig; 1 band, 2 cross-family nodes (SoftClip, Bitcrush) so weights are audible; `kBandMorphXLink` = SweepFreq (`1/7`), `kSweepEnable 1`; sweep frequency param 0.0 then 1.0 with 0.5 s renders each; REQUIRE the band's morph X read through a new test accessor `Processor::debugMorphPositionX(band)` (or via the output spectra differing by > 6 dB RMS between the two renders). Section: base frequency fixed, `kSweepLFOEnable 1` depth 1 at 2 Hz: REQUIRE the band's morph X moves (variance > 0.01 over 2 s). Today: no movement without a controller.
   - `TEST_CASE("Morph link modes round-trip through processor state", "[disrumpo][state][STATE-COMPAT][D-137]")`: set `kBandMorphXLink` = Stepped, `kBandMorphYLink` = HoldRise on band 1; getState → setState into a fresh Rig → getState; REQUIRE equality and a v12 fixture stream (byte array captured from the current getState before this change, checked into `tests/fixtures/disrumpo_state_v12.bin`) loads with both link modes == None.
   - `TEST_CASE("MorphLinkMode curves have one definition matching dsp-details.md", "[disrumpo][sweep-link][D-040]")` (`dsp/sweep_morph_link_tests.cpp`): table test at x ∈ {0.25, 0.55, 0.75} for every mode against the spec formulas (EaseIn x², EaseOut 1−(1−x)², HoldRise flat to 0.6, Stepped floor(4x)/3) and REQUIRE `applyMorphLinkMode` (controller) no longer exists (compile-time: the test includes only `dsp/sweep_morph_link.h`).
2. **Fix**
   - Processor: add `MorphLinkMode xLink, yLink` to `BandState` (`plugins/disrumpo/src/dsp/band_state.h`) ; `processor_params.cpp:971-972` decode `kBandMorphXLink/YLink` as `round(value * 7)` into `bandStates_[band]`. In `processor.cpp` after the modulated frequency is known (:365), compute `sweepNorm = normalizeSweepFrequency(sweepProcessor_.getCurrentFrequency())` (add that getter returning `frequencySmoother_.getCurrentValue()`), and for each band whose link ≠ None set the morph position base to `applyMorphLinkCurve(mode, sweepNorm)` (for `None` keep the cached `morphX/Y`; for `Custom` use `customCurve_.evaluate`) BEFORE the mod-engine offsets at :305-316. The global `kSweepMorphLink` becomes a plain alias that applies to every band whose per-band link is None (documented in `plugin_ids.h`).
   - State: bump `kPresetVersion` to 13 (`plugin_ids.h:690`, add a history line); in getState after the morphSmoothing float (:323) write two int8 link modes per band; in setState read them only `if (version >= 13)`; in `Controller::parseComponentState` (:505-529) read and `setter(kBandMorphXLink/YLink, mode/7.0)` under the same gate; in `createComponentStateStream` (:1829-1834) write them with `getInt8FromList(..., 7)`.
   - Controller: delete `MorphSweepLinkController` (`visibility_controllers.cpp:120-210`, header, `controller.cpp:1454-1456`, `:1535-1537`, member `controller.h:341`) and `controller/morph_link.cpp` (+ its CMake entries in `plugins/disrumpo/CMakeLists.txt` and `tests/CMakeLists.txt`; move the still-valid cases of `unit/morph_link_test.cpp` onto `applyMorphLinkCurve` and delete the rest). Fix the registration list `parameter_registration.cpp:216-228` to the 8 enum names in enum order ("None, Sweep Freq, Inverse Sweep, Ease In, Ease Out, Hold-Rise, Stepped, Custom"), and `plugin_ids.h:647` comment to the 4-level Stepped. Update `custom_curve` wiring note: the custom-curve editor view remains unregistered (D-069, T2.22).
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`; gate; pluginval; tidy. Commit.

### T1.15 — setState re-derives type-specific params from the restored shape slots  — 60 min

Findings: D-050, D-156 (high). Prereq: T1.03.

1. **Failing test** (`processor_lifecycle_state_test.cpp`): `TEST_CASE("Host setState restores HalfRectify threshold from shape slots (Rectified_Drums)", "[disrumpo][state][D-050][D-156]")`: active Rig 44.1 kHz; `setStateBytes(Drums/Rectified_Drums.vstpreset component chunk)`; render 440 Hz −12 dBFS 1 s; REQUIRE RMS(last 0.5 s) > −30 dBFS (today −62.2). Section: `getBlendedParams`-style accessor on the band: REQUIRE band 1 node 0 `params.threshold == 0.3 ± 1e-6`. Second case: load stream A (Tube, slot0 0.9) into an active Rig, render; load stream B (Tape); load A again, render; REQUIRE the two A renders match within 1e-5 (today the second carries Tape's leftovers).
2. **Fix**: make `mapShapeSlotsToParams` a static member `Processor::mapShapeSlotsToParams` declared in `processor.h` (move the body from `processor_params.cpp:39-260`, keep it file-visible). In `processor_state.cpp` after the slot loop (:865-874) call `mn.params = DistortionParams{}; mn.params.bias = bias; mn.params.folds = folds; mn.params.bitDepth = bitDepth; mapShapeSlotsToParams(mn.type, mn.shapeSlots, mn.params);` for `version >= 9` (for v < 9 keep the legacy fields only).
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`; gate; pluginval; tidy. Commit.

### T1.17 — One state codec: shared encode/decode helpers, preset writer drift fixed  — 180 min

Findings: D-085, D-117, D-053, D-052, D-086, D-051 (high). Prereq: T1.12 (so the writer covers the v13 fields once).

1. **Failing tests** (new `plugins/disrumpo/tests/unit/controller/controller_state_codec_test.cpp`, register + link already includes controller sources):
   - `TEST_CASE("Controller preset stream round-trips through the processor", "[disrumpo][state][D-117][D-085]")`: `Controller c; c.initialize(nullptr)`; set routing-0 source = Transient (`13/13`), band-0 active nodes = 1 (`0.0`), morph mode = 2 (`1.0`), smoothing 100 ms (`0.2`), crossover 0 = 1000 Hz (log norm 0.566); `auto* s = c.createComponentStateStream()`; Rig `setStateBytes`; `getStateBytes`; `Controller c2; c2.setComponentState(bytes)`; REQUIRE `c2` normalized values equal `c`'s within one list step (source 13, nodes 1, mode 2, 100 ms, crossover 1000 Hz ± 1 Hz). Today: source 12, nodes 2, crossover 28 Hz.
   - `TEST_CASE("Crossover normalized<->Hz mapping is log10 everywhere", "[disrumpo][state][D-086][D-051]")`: `parseComponentState` of a stream with crossover 1000 Hz → param norm 0.566 ± 1e-3; `processParameterChanges` with that norm → `crossoverL_.getCrossoverFrequency(0)` == 1000 ± 1 Hz; `Controller::createCustomView("SpectrumDisplay")` divider seeded at 1000 Hz (use `SpectrumDisplay::getCrossoverFrequency(0)`).
   - `TEST_CASE("Controller getState/setState round-trips the MIDI CC blob", "[disrumpo][state][D-117]")`.
2. **Fix**
   - New header `plugins/disrumpo/src/state_codec.h` (plugin-local, included by processor and controller — it holds only constexpr/inline helpers, not processor or controller types): `crossoverHzToNorm/NormToHz` (log10 20..20000), `sweepFreqNormToHz/HzToNorm` (log2), `lfoRateNormToHz/HzToNorm` (`std::log(0.01f)`, `std::log(20.0f)` computed, not the 4-digit literals), `listIndexToNorm(index, count)`, `normToListIndex(norm, count)`, the mod-source count `kUIModSourceCount`, `kMorphLinkModeCount`, `kNoteValueCount` (alias `Krate::DSP::kNoteValueDropdownCount`), and the width/intensity/env/rate linear ranges from `processor_params.cpp:394-397, 643-746`.
   - Replace the literals at `controller.cpp:1815` (`12` → `kUIModSourceCount - 1`), `:1830-1831` (`round(norm*3)+1`), `:1748-1750` (write the controller's custom-curve params instead of the constant 2-point curve: count from `kSweepCustomCurvePointCount`, points from P0X..P7Y), `:1696/:1729` and `:950` (`crossoverNormToHz` instead of linear `toPlain`), `:210` (`crossoverHzToNorm` instead of `toNormalized`), and the three log-rate copies in `processor_params.cpp:431-432, 579-580, 611-612` and six in `processor_state.cpp:103-104,163-164,195-196,506-507,583-584,619-620` (D-091 is thereby done too; mark it in the table row for T4.01 as "done in T1.17").
   - Register the crossover and sweep-frequency parameters as a log-mapped `Parameter` subclass (`plugins/disrumpo/src/controller/log_range_parameter.h`: `toPlain/toNormalized/toString/fromString` call the codec helpers) so host text matches the DSP (covers D-101's frequency half). Same IDs, same `RangeParameter`-compatible flags — **STATE-COMPAT ok** (normalized value meaning is unchanged for the processor; only the controller's plain mapping changes).
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`; gate; pluginval; tidy. Commit.

### T1.19 — Shape-slot 2: stepCount 0 everywhere; Serge model labels match the adapter  — 60 min

Findings: D-135 (high), D-168 (PLAUSIBLE: the label swap is latent until D-135 is fixed, which this task does — the headless test is the verification). Prereq: none.

1. **Failing test** (`unit/controller/editor_lifecycle_test.cpp` or new `unit/controller/menu_population_test.cpp`): `TEST_CASE("Type-specific COptionMenus keep their uidesc entries", "[disrumpo][ui][D-135]")`: open the editor through `Krate::TestSupport::exerciseEditorLifecycle`-style harness (`editor_lifecycle_harness.h`), set band 0 DisplayedType to Tube, locate the COptionMenu bound to `Band.NodeShape2` in `TypeParams_Tube` (walk the frame with `uidesc_reachability.h` helpers or by control tag) and REQUIRE 4 entries titled "12AX7","EL84","6L6","300B"; select entry 1 and REQUIRE the proxy normalized == 1/3 ± 1e-6. Repeat for SergeFold expecting "Serge","Simple","Buchla","Lockhart" (enum order of `distortion_adapter.cpp:678-685` `models[]`) and REQUIRE a default-constructed SergeFold node decodes `foldModel == 0` (Serge). Today: 9 entries "0".."8".
2. **Fix**: `parameter_registration.cpp:820` and `:957` → `int32_t stepCount = 0;`. `format_helpers.h` Temporal/RingSat slot-2 formatting already quantises with `round(v*8)` (:52-55, :94-97), so no change there; `processor_params.cpp:156/:173` already quantise. `editor.uidesc:974` menu-items → `"Serge,Simple,Buchla,Lockhart"` (via the project's XSLT uidesc workflow, never hand-edit). `morph_node.h:102-113` `ShapeShadowStorage` ctor: `typeSlots[SergeFold][2] = 0.0f` (model Serge) and `[SergeFold][4] = 0.0f` (shape pure); also set `[SineFold][2] = 0` and `[TriangleFold][2] = 0` (D-157, charged to T2.06 but one line here). Fix the false comments at `sub_controllers.h:191-194` and `tests/unit/menu_items_test.cpp:8`.
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`; gate; pluginval; tidy. Commit.

### T1.20 — Proxy/node/pad wiring: mirrors both ways, per-node shadow, pad and selector follow parameters  — 240 min

Findings: D-063, D-062 (high), D-064, D-065, D-067 (medium). Prereq: T1.19.

1. **Failing tests** (new `unit/controller/node_proxy_wiring_test.cpp`; Controller initialised, `NodeSelectionController` constructed as in `controller.cpp:1458-1461`):
   - `"Type change writes the restored slots into the node's real shape params"` (D-063): band 0 node 0 Tube, `kNodeShape0` = 0.8; set DisplayedType to Fuzz; REQUIRE `getParamNormalized(makeNodeParamId(0,0,kNodeShape0)) == proxy value` (the Fuzz shadow 0.5), not 0.8.
   - `"Shadow storage is indexed by the selected node"` (D-064): select node B, set Tube slot0 0.9, switch B to Fuzz and back; REQUIRE B's slot0 == 0.9 and node A's shadow for Tube unchanged (0.5).
   - `"Automating a node param moves its proxy"` (D-067): `setParamNormalized(makeNodeParamId(0,0,kNodeDrive), 0.7)`; REQUIRE `kBandDisplayedDrive` == 0.7.
   - `unit/morph_pad_test.cpp`: `"MorphPad Y drag updates the controller parameter"` (D-062): simulate the drag path (`onMouseDown`/`onMouseMoved` with a frame-less controller stub that records `setParamNormalized`), REQUIRE `getParamNormalized(kBandMorphY)` == drag Y; `"MorphPad follows setParamNormalized on X and Y"`: REQUIRE `getMorphX/Y()` follow.
   - `"DynamicNodeSelector highlights the selected node with 2 nodes"` (D-065): ActiveNodes = 2, SelectedNode = 1/3; construct via `createCustomView("DynamicNodeSelector")`; REQUIRE `getSelectedSegment() == 1`; change SelectedNode → REQUIRE it follows.
2. **Fix**
   - `visibility_controllers.cpp:445-453` `copyDisplayedTypeToSelectedNode`: for each slot also `editParam(makeNodeParamId(band_, selectedNode, kNodeShape0+s), newSlots[s])`.
   - Pass the per-band array: `NodeSelectionController(controller, band, std::array<ShapeShadowStorage,4>* shadows)`; `save/load` through `(*shadows)[selectedNode]` (:438, :447); `controller.cpp:1459-1460` passes `&shapeShadowStorage_[b]`.
   - Subscribe to the selected node's 14 real params: in the constructor add dependents for `makeNodeParamId(band, n, kProxyIndexToNodeParam[i])` for all 4 nodes (remove/re-add not needed if all 56 are subscribed; filter in `update()` by `extractNode == getSelectedNode()`), and in `update()` route a real-param change to a new `copySelectedNodeParamToProxy(changedParam)` (guarded by `isUpdating_`). `deactivate()` removes them.
   - `morph_pad.cpp:646-649, 687-689, 746-748, 820-823`: call `controller_->setParamNormalized(morphYParamId_, y)` before each `performEdit`. Make `MorphPad` an `IDependent` on both MorphX and MorphY `Parameter`s (pattern: `morph_pad.cpp:79-90` for ActiveNodes) and in `update()` call `setMorphPosition(x, y)` + `invalid()` when either changes (skip while a drag is in progress).
   - `dynamic_node_selector.cpp`: subscribe to the SelectedNode parameter as well (ctor :28-36 pattern), decode `round(norm*3)` clamped to `N−1` into `setSelectedSegment` at construction and in `update()`; remove the `setValueNormalized(paramNorm)` seed at `controller.cpp:1072-1076`.
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`; gate; pluginval; tidy. Commit.

### T1.21 — Do not cache the auto-hide DynamicNodeSelector (use-after-free on close)  — 45 min

Findings: D-061 (high). Prereq: none.

1. **Failing test** (`unit/controller/editor_lifecycle_test.cpp`): `TEST_CASE("Main/Shape tab switch then close does not touch freed selectors", "[disrumpo][lifecycle][D-061]")`: open editor via the harness, set `kBandTabView` (band 0) to 1.0 then 0.0 (UIViewSwitchContainer page swap), close the editor. Under ASan this is a hard fail today; without ASan assert through a test hook: make `Controller::compactNodeSelectorsForTest()` return the array and REQUIRE every slot is null after the page swap.
2. **Fix** `controller.cpp:1078-1081`: stop storing the pointer (delete `compactNodeSelectors_[bandIndex] = nodeSelector;`, the member `controller.h:364`, the loop at `:1451` and `:1546`). `DynamicNodeSelector::~DynamicNodeSelector` (`dynamic_node_selector.cpp:48-56`) already deactivates. Apply the same review to `dynamicNodeSelectors_`: they live in `BandExpandedStrip`, which is not inside a switch container, so they may stay cached, but add the same destructor-nulling guard via an `IViewListener::viewWillDelete` registered in `createCustomView` that nulls the slot.
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`; gate; pluginval; tidy. Optional: run the editor lifecycle test once in the ASan build (`cmake -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON`, Debug) when time allows. Commit.

### T1.22 — Silent input stays silent: Stochastic, Quantize-independent DC blocking for Tube/Temporal  — 90 min

Findings: D-142 (high), D-075 (medium). Prereq: T1.06. **CROSS-PLUGIN IMPACT** (`dsp/include/krate/dsp/primitives/stochastic_shaper.h`; consumer: `plugins/disrumpo` only — confirmed by grep).

1. **Failing tests** (`dsp/tests/unit/primitives/stochastic_shaper_test.cpp`): `TEST_CASE("StochasticShaper outputs silence for silent input", "[primitives][stochastic][D-142]")`: jitter 1, drift 1, drive 5, 2 s of zeros → REQUIRE max |out| < 1e-6. `unit/distortion_adapter_test.cpp`: `TEST_CASE("Adapter self-noise is below -90 dBFS for every type", "[disrumpo][selfnoise][D-142][D-075]")`: for each of the 26 types, drive 1.0, 1 s silence → REQUIRE RMS(last 0.5 s) ≤ −90 dBFS (today Stochastic −10.4, Quantize −49 — Quantize is fixed in T2.07; EXPECT it red there and leave Quantize out of this loop until T2.07 lands, then add it). `TEST_CASE("Tube and Temporal(Diode/Tube curve) output has no DC", "[disrumpo][dc][D-075]")`: Tube drive 1.0 bias ±1 and Temporal curve index 8 (slot2 = 1.0) on a 100 Hz sine → REQUIRE |mean| < 1e-3 (today Tube −0.067).
2. **Fix**: `stochastic_shaper.h:584` → `float out = waveshaper_.process(x + jitterOffset) - waveshaper_.process(jitterOffset);` (keeps 0→0 and the FR-022 modulation around the signal; update the FR-022 doc comment). `distortion_adapter.cpp:586-592` `updateDCBlockerState`: add `Tube`, `Stochastic`, and `Temporal` when `typeParams_.dynamicCurve * 8 + 0.5` rounds to 7 or 8 (recompute in `setParams` too, since the curve can change after `setType`).
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`, `dsp_primitives_tests`; gate `disrumpo_tests dsp_primitives_tests` + header-consumers output; pluginval; tidy disrumpo + dsp; macOS proxies. Commit.

### T1.23 — GranularDistortion: stable per-grain slot index, no in-loop rebuild  — 90 min

Findings: D-074 (high). **CROSS-PLUGIN IMPACT** (`dsp/include/krate/dsp/processors/granular_distortion.h`, `dsp/include/krate/dsp/primitives/grain_pool.h`; consumers: `plugins/disrumpo`, `plugins/ruinae` (`ruinae_voice.h`), `dsp/include/krate/dsp/processors/grain_processor.h`, `systems/atmosphere_engine.h`, `granular_engine.h`, `granular_filter.h`, `noise_organism.h`). Prereq: none.

1. **Failing test** (`dsp/tests/unit/processors/granular_distortion_test.cpp`): `TEST_CASE("Each grain reads its own state for its whole life", "[processors][granular][D-074]")`: grain size 10 ms, density 8, 1 kHz input; instrument with a test hook `debugGrainReadPositions()` (per active grain: `startBufferPos`, current read pos); over 2000 samples REQUIRE each grain's read position advances by exactly 1 per sample while active, and REQUIRE `processGrain` call count per sample == active grain count (expose `debugLastProcessedGrainCount()`). Today: jumps after the first retirement; one skipped, one double-processed on the retirement sample.
2. **Fix**: `grain_pool.h`: add `[[nodiscard]] size_t indexOf(const Grain* g) const noexcept { return static_cast<size_t>(g - grains_.data()); }`. `granular_distortion.h:549-564` and `:619-630`: `grainIndex = grainPool_.indexOf(grain)`; delete both scan loops and the `% kMaxGrains`. `:395-397` and the bypass loop `:371-373`: copy the span into a local `std::array<Grain*, GrainPool::kMaxGrains> snapshot` (count from the span) before iterating, so `releaseGrain` inside `processGrain` cannot shift the live list. Delete the dead `getGrainIndex` helper (:488-500).
3. Standard workflow: build `dsp_processors_tests`, `disrumpo_tests`, `Disrumpo`; gate `dsp_processors_tests disrumpo_tests` + header-consumers output (expect `ruinae_tests`, `dsp_systems_tests`); pluginval; tidy dsp + disrumpo; macOS proxies. Commit.

### T1.18 — MIDI learn: one design, wired end to end  — 240 min

Findings: D-136, D-096 (high), D-058 (medium), D-094 (low). Prereq: T1.17 (controller state tests exist). Decision: design (a) — keep the feature; the docs and CHANGELOG advertise it (D-104).

1. **Failing tests**:
   - `processor_lifecycle_state_test.cpp`: `TEST_CASE("Processor declares one event input bus", "[disrumpo][midi][D-096]")`: `REQUIRE(p->getBusCount(kEvent, kInput) == 1)`.
   - `unit/controller/midi_learn_wiring_test.cpp` (new): `TEST_CASE("Context-menu MIDI Learn completes from a live CC and persists", "[disrumpo][midi][D-136][D-058]")`: Controller; `startLearnForTest(paramId)` (expose the menu action's body as `Controller::startMidiLearn(ParamID)`); call `onLiveMIDIControllerInput(0, 0, 7)`; REQUIRE `getMidiControllerAssignment(0, 0, 7, id)` returns kResultTrue with `id == paramId`; toggle to per-preset via `Controller::toggleMidiMappingScope(paramId)`; `getState`→`setState` into a fresh controller → REQUIRE the assignment still resolves. REQUIRE a recorded `restartComponent(kMidiCCAssignmentChanged)` call on a stub component handler.
2. **Fix**
   - `processor.cpp:63-66` `initialize`: `addEventInput(STR16("MIDI In"), 16);`. Delete the processor-side learn: `processor.cpp:392-414`, `processor_params.cpp:527-534`, members `midiLearnActive_`/`assignedMidiCC_` (`processor.h:237-240`); `kSweepMidiLearnActive`, `kSweepMidiCCNumber`, `kSweepDetectedCCOutputId` stay registered (`parameter_registration.cpp:307, 345-352`) with `kIsHidden` added and no processor action, so IDs are never reused (**STATE-COMPAT ok**: these are never serialised).
   - Controller: inherit `Steinberg::Vst::IMidiLearn`, add it to `DEF_INTERFACE`, implement `onLiveMIDIControllerInput(busIndex, channel, cc)`: if `midiCCManager_->isLearning()` call `midiCCManager_->processCCMessage(cc, 0)` (the shared manager's completion API), then `setParamNormalized(kGlobalMidiLearnActive, 0)` and `componentHandler->restartComponent(kMidiCCAssignmentChanged)`. Call `restartComponent(kMidiCCAssignmentChanged)` also after `removeMappingsForParam`, the scope toggle (`controller.cpp:1646-1654`) and at the end of `Controller::setState`. `getMidiControllerAssignment` (:707-726): delete the `assignedMidiCC_` fallback and the member (`controller.h:435`). Persist per-preset mappings: `Controller::getState` (:658-677) writes a second blob `serializePresetMappings()` after the global one (version 3 of the controller state; `setState` reads it when `version >= 3`).
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`; gate; pluginval (bus change — check it stays green); tidy. Commit.

### T1.24 — setState hands state to the audio thread instead of mutating live DSP  — 150 min

Findings: D-003 (high). Prereq: T1.03, T1.15 (the restore logic is final).

1. **Failing test** (`processor_lifecycle_state_test.cpp`): `TEST_CASE("setState during processing does not touch live DSP objects", "[disrumpo][rt-safety][D-003]")`: add a test-only atomic counter `MorphEngine::debugSetNodesCalls` and `CrossoverNetwork::debugSetBandCountCalls`; call `setStateBytes` on an active Rig from the test thread while no `process()` runs and REQUIRE both counters are still 0 right after `setState` returns and become > 0 after the next `processBlock`. (A true two-thread TSan run is Phase 5, T5.08.)
2. **Fix** `processor_state.cpp` setState: parse the stream into a `PendingState` struct (all fields setState currently applies: globals, band states, crossovers, band count, sweep, LFO/env, custom curve points, mod engine macros/routings, morph caches) stored in `pendingState_` plus `std::atomic<bool> pendingStateReady_` (publish with release). `processor.cpp` top of `process()` (before :191 `processParameterChanges`): if `pendingStateReady_.exchange(false, acquire)` call `applyPendingState()`, which performs the existing apply code (the setter calls now live there). `setupProcessing`/`setActive` keep applying the caches directly (they run with processing stopped). Guarantee `applyPendingState()` allocates nothing (run under `AllocationScope` in the test).
3. Standard workflow: build `disrumpo_tests`, `Disrumpo`; gate; pluginval; tidy. Commit.

## Phase 2 — medium defects

Order: T2.01 → T2.02 → T2.03 → T2.04 → T2.05 → T2.06 → T2.07 → T2.08 → T2.09 → T2.10 → T2.11 → T2.12 → T2.13 → T2.14 → T2.15 → T2.16 → T2.17 → T2.18 → T2.19 → T2.20 → T2.21 → T2.22 → T2.23 → T2.24 → T2.25 → T2.26. All Phase 1 tasks are prerequisites unless stated otherwise. Each task: Standard workflow with build targets `disrumpo_tests` + `Disrumpo` (plus `dsp_*_tests` where named), gate `disrumpo_tests` (+ header-consumers output when a shared header changed), pluginval, tidy, commit.

### T2.01 — Fuzz tone opened  — 30 min (D-076)
Test (`unit/distortion_adapter_test.cpp`): `"Fuzz passes 4 kHz within 6 dB of 500 Hz at node tone 8000 Hz"` — adapter prepared at 48 kHz, Fuzz, sustain 0.5, `toneHz` 8000; two sines; REQUIRE level difference ≤ 6 dB (today ≈ −13 dB from the hidden 1789 Hz biquad). Fix `distortion_adapter.cpp:637-650`: in both Fuzz and AsymmetricFuzz cases call `fuzz_.setTone(1.0f)` (fully open, 8 kHz) so the adapter's own tone filter is the only tone control.

### T2.02 — Tube cascade uses one TubeStage per stage  — 60 min (D-077)
Test: `"Tube with 3 stages and bias 0.5 has no DC and matches four independent stages"` — adapter Tube `satStage` 3, bias 0.5, 100 Hz sine; REQUIRE |mean(last 1 s)| < 1e-3 and output matches a reference built from four separately prepared `Krate::DSP::TubeStage` instances within 1e-4 (today −20 dBFS DC). Fix: `distortion_adapter.h:371` `Krate::DSP::TubeStage tube_` → `std::array<Krate::DSP::TubeStage, 4> tube_`; prepare/reset/route all four (`distortion_adapter.cpp:35, 107, 614-615`); `:313-316` loop runs stage `s` through `tube_[s]`.

### T2.03 — Feedback type: Curve routed, limiter off really off, Thr direction  — 45 min (D-078)
Tests: `"FeedbackDist fbCurve changes output"` (slot2 0 vs 1 → RMS differs > 1 dB), `"FeedbackDist higher Thr gives higher peak"`, `"FeedbackDist limiter off does not limit"` (feedback 1.2 → peak > threshold+3 dB). Fix `distortion_adapter.cpp:812-819`: `feedbackDist_.setSaturationCurve(static_cast<WaveshapeType>(clamp(int(p.fbCurve*8+0.5),0,8)))`; threshold `-24.0f + p.limThreshold * 24.0f`; limiter off → `feedbackDist_.setLimiterEnabled(false)` — **CROSS-PLUGIN IMPACT** add `setLimiterEnabled(bool)` to `dsp/include/krate/dsp/processors/feedback_distortion.h` (consumer: Disrumpo only) that skips the block at :283-298 when false; `dsp_processors_tests` case for it. Build/gate dsp_processors_tests too; macOS proxies.

### T2.04 — Granular adapter neutralises internal drive  — 20 min (D-079)
Test: `"Granular at drive 1 on -20 dBFS sine peaks below -12 dBFS"`. Fix `distortion_adapter.cpp:877-886`: add `granular_.setDrive(1.0f);` (mirrors the Fractal fix at :943-947).

### T2.05 — Bitcrush bits mapping matches the primitive  — 20 min (D-083, D-102 Bitcrush half)
Test: `"Bitcrush slot 0.0 and 0.15 give different output"`. Fix `processor_params.cpp:131` → `p.bitDepth = 4.0f + slots[0] * 12.0f;` and the jitter clamp `distortion_adapter.cpp:463` → `[4.0f, 16.0f]`; `format_helpers.h:124` → `4.0 + v * 12.0`; update `distortion_adapter_test.cpp:921` comment and `distortion_adapter.h:109` doc.

### T2.06 — Wavefolder level and defaults (verify first)  — 120 min (D-145 PLAUSIBLE, D-157, D-158, D-159)
Verification test (`processor_audio_output_test.cpp`): `"Wavefold types at default settings peak within 6 dB of SoftClip"` — Rig after T1.06/T1.08; SineFold, TriangleFold, SergeFold defaults, drive 0.25/0.75/1.0, 1031.25 Hz −6 dBFS; REQUIRE peak within 6 dB of the SoftClip render at the same drive and peak does not fall as drive rises. If it passes after T1.06/T1.08 alone, mark D-145 refuted (its wiring cause was fixed upstream) and keep the test; the defaults work below is still required by D-157/D-158/D-159. Second test: `"SineFold and TriangleFold differ at defaults"` (RMS diff > 1 dB or centroid ratio outside 0.85-1.15). Fix: `morph_node.h:102-113` defaults — SineFold/TriangleFold/SergeFold slot `smooth` (index 4 / 4 / 5) = 0.0, folds slot 0 = 0.09 (folds 2), shape/angle slot 2 = 0.0 (T1.19 did SergeFold model/shape); FullRectify/HalfRectify smooth slot = 0.0. In `distortion_adapter.cpp` SergeFold/TriangleFold paths (:395-424): multiply the Simple/Buchla259 `wavefolder_` output and the `wavefoldTriangle_` branch by `p.folds` (makeup for the primitive's 1/folds cap, Buchla by `3*folds/1.4`). Update `ui-mockups`-derived expectations in `distortion_adapter_test.cpp:275-560` where they pin the old defaults.

### T2.07 — Quantize is mid-tread; silence stays silent  — 30 min (D-010, D-143)
Test (`distortion_adapter_test.cpp`): `"Quantize outputs 0 for 4096 zeros at levels {0,0.01,0.1,0.5} × offset {0,0.5}"` → REQUIRE |mean| < 1e-4 and max|out| < 1e-4 (dither 0). Fix `distortion_adapter.cpp:478-510`: quantise on a grid centred at 0: `index = std::round(input/step + quantOffset) - quantOffset; quantized = index*step;` with `numLevels` forced odd (`2*round(quantLevels*31)+1`, step `2/(N−1)`), clamp to ±1; then add `Quantize` to `needsDCBlock_` (:586-592) as a belt-and-braces measure. Then add Quantize to the self-noise loop from T1.22.

### T2.08 — Band-count change is click-free  — 90 min (D-023, D-140)
Test (`processor_audio_output_test.cpp`): `"Band count 2->4->1 during playback has jump ratio <= 1.3"` — 220 Hz −12 dBFS, steps at 0.49 s and 0.99 s; reuse the `continuityAround` helper (move it from the probe :943-955 into `disrumpo_rig.h`); REQUIRE jump ratio ≤ 1.3 and `ClickDetector` 0 (today 3.78/3.90). Fix `processor_params.cpp:339-346`: do not call `setBandCount` immediately; set `pendingBandCount_` and in `process()` run a 10 ms output fade (`bandCountFadeGain_`, same ramp helper as T1.07's factor dip) → at the bottom call `crossoverL_/R_.setBandCount`, `crossoverL_/R_.reset()`-equivalent for newly active crossovers, `bandProcessors_[b].reset()` for bands that become active → fade up. `crossover_network.h:301-365` `redistributeIncreasing`: insert new frequencies into the free indices without re-sorting existing ones into different `crossovers_[i]` instances (keep index stability).

### T2.09 — Smooth global gain/mix; output gain post-mix  — 45 min (D-025)
Test: `"Output gain step of 48 dB in one block produces no sample jump above the sine slope bound"` and `"Output gain +6 dB raises output 6 dB at mix 0"`. Fix `processor.cpp:449-457, 502-503`: three `Krate::DSP::OnePoleSmoother`s (15 ms, configured in setupProcessing, snapped on setActive) for input gain, output gain and wet mix, advanced per sample in the loop; `out = (dry*dryMix + sum*wetMix) * outputGain`. Update `specs/Disrumpo/plans-overview.md` if its diagram disagrees (it already shows Output Gain after Mix).

### T2.10 — Latency and tail reporting  — 120 min (D-026, D-081, D-098, D-108)
Tests (`processor_lifecycle_state_test.cpp`): `"getLatencySamples equals the measured impulse delay with a Spectral node"` (1 band Spectral fftSize 2048 mix 1: measured peak offset == `getLatencySamples()` ± 1), `"getLatencySamples is 0 with SoftClip"`, `"Band with Spectral is time-aligned with a SoftClip band"` (2 bands, impulse peaks on the same sample), `"getTailSamples > 0 when FeedbackDist is active"`. Fix: `Processor::getLatencySamples()` override returns `maxActiveLatency_` = max over active bands of `BandProcessor::getLatency()` (make it non-static: max over the two engines' `blendedAdapter_.getProcessingLatency()` / node adapters, using `DistortionAdapter::blockLatency_`); add per-band and dry delay lines (`Krate::DSP::DelayLine` from `dsp/include/krate/dsp/primitives/delay_line.h`, sized for 4096 at setup) that delay every band and the dry path to `maxActiveLatency_`; call `componentHandler->restartComponent(kLatencyChanged)` from `processParameterChanges` is not allowed on the audio thread — instead set an atomic flag the controller polls in its 33 ms timer and have the controller call `restartComponent`. `getTailSamples()` returns `kInfiniteTail` while any band has FeedbackDist/AllpassResonant (feedback > 0) or Granular/Spectral active, else 0. Replace the four vacuous `oversampling_performance_tests.cpp:32-125, 253-276` cases with the measured ones above and drop their `[performance]` tag (D-125 is then done; T5.05 only verifies).

### T2.11 — Linear1D visits A, B, C, D in turn  — 60 min (D-029, D-100)
Test (`unit/morph_mode_test.cpp`): `"Linear1D with 4 corner nodes sweeps A->B->C->D with no weight jump"` — Processor default node positions; sweep x in 1e-3 steps; REQUIRE each node reaches weight 1 at some x, and max |Δweight| per step < 0.05 (today C never reaches 1, jump at x≈0.001). Fix `morph_engine.cpp:180-209`: in Linear1D use `posXLinear(i) = i / (activeNodeCount_-1)` instead of `nodes_[i].posX` (keep `posX/posY` for the 2-D modes); document in `distortion_types.h:378`.

### T2.12 — Cross-family adapters return to base params when offsets return to 0  — 30 min (D-031)
Test (`unit/morph_transition_test.cpp`): drive offset 0.5 for one block, then 0; REQUIRE `adapters_[0].getCommonParams().drive == node base drive` (expose `getNodeAdapterForTest(i)`) and the render equals a never-modulated render within 1e-5. Fix `morph_engine.cpp:726-738`: track `offsetsWereActive_`; when all four offsets are 0 and the flag is set, re-apply `nodes_[i].commonParams/params` to each active adapter once and clear the flag.

### T2.13 — Type change / processor switch resets state and fades  — 90 min (D-032, D-080, D-141)
Tests (`distortion_adapter_test.cpp`): `"Switching FeedbackDist -> SoftClip -> FeedbackDist does not replay old audio"` (loud sine, switch, 1 s silence, switch back, 100 ms silence → REQUIRE max|out| < 1e-4); `processor_audio_output_test.cpp`: `"Node type switch SoftClip->SineFold jump ratio <= 1.3"`. Fix `distortion_adapter.cpp:142-162` `setType`: when the type actually changes, call the new processor's `reset()` (add a `resetProcessorFor(type)` switch) and start a 5 ms output fade (`typeFadeGain_` ramp 0→1 applied in `process()`); the allocation part of D-032 is already removed by T1.01 (Spectral never prepares in setParams).

### T2.14 — Per-type bias semantics normalised before blend  — 45 min (D-033, D-082)
Tests (`unit/morph_interpolation_test.cpp`): `"SoftClip+Fuzz same-family blend keeps a centred Fuzz centred"` (Fuzz node slot0 = 1.0, SoftClip bias 0; weights 0.4/0.6 → the Fuzz adapter's effective bias offset is 0: expose `FuzzProcessor::debugBiasOffset()` or compare the output with a Fuzz-alone render within 1e-4). `"kNodeBias 0.0 and 0.25 give different Fuzz output"` (D-082). Fix: store bias in `DistortionParams` as bipolar everywhere — `processor_params.cpp:73, :81` → `p.bias = slots[0] * 2.0f - 1.0f;` and convert in the adapter `distortion_adapter.cpp:637, :646` → `fuzz_.setBias((p.bias + 1.0f) * 0.5f)`. The processor's own default for Fuzz slot0 (0.5 → bias 0 → fuzz 0.5 = sputtery) must keep today's sound: set `ShapeShadowStorage` Fuzz/AsymFuzz slot0 default to 0.5 is already the case; the processor-side meaning changes so flag **STATE-COMPAT**: a v13 stream carries the raw slot (unchanged) so no migration is needed; nothing else serialises bias semantics.

### T2.15 — Rungler renumbering migration for v0.9.0/0.9.1 streams  — 60 min (D-055) — **STATE-COMPAT**
Test (`serialization_round_trip_test.cpp`): fixture `tests/fixtures/disrumpo_state_v9_pre_rungler.bin` (build it from a v0.9.1 checkout once: `git show disrumpo/v0.9.1:...` or hand-craft a v9 stream whose routing-0 source byte is 10 with a marker that v9-pre streams lack the v11 rungler block) and REQUIRE it loads routing 0 as `ModSource::SampleHold`. Fix `processor_state.cpp:767-770`: `if (version <= 8 && source >= 10) source += 1;` (v9 is ambiguous — document the ambiguity in `plugin_ids.h:686-690`'s history and treat v9 as new numbering, since v0.9.4/0.9.5 also wrote v9 with the new enum; record this ruling in report.md).

### T2.16 — RingSaturation depth-0 bypass continuity  — 30 min (D-163) — **CROSS-PLUGIN IMPACT** (`dsp/include/krate/dsp/primitives/ring_saturation.h`; consumer: Disrumpo only)
Test (`dsp/tests/unit/primitives/ring_saturation_test.cpp`): `"depth 0 and depth 1e-4 differ by less than 0.1 dB at input amplitude 2.5"`. Fix `ring_saturation.h:270-272`: remove the early return; instead skip only the stage loop when `depth_ == 0.0f` and still apply `softLimit` + `dcBlocker_`. Update the SC-002 bypass test (`ring_saturation_test.cpp:302-307`) to expect `softLimit(x)` rather than `x` for |x| > 1 (document the contract change in the header). Build/gate `dsp_primitives_tests disrumpo_tests`; macOS proxies.

### T2.17 — Non-finite input must not latch (EnvelopeFollower, DCBlocker, output)  — 90 min (D-013, D-162) — **CROSS-PLUGIN IMPACT** (`envelope_follower.h` consumers: disrumpo, innexus, membrum + 20 dsp headers; `dc_blocker.h` consumers: disrumpo, membrum, ruinae + 30 dsp headers)
Tests in `dsp/tests/unit/.../*_ieee_test.cpp` TUs that belong to `dsp_ieee_tests` (NaN bit pattern through a `volatile` uint32, per `reference_fastmath_nan_in_tests`): `"EnvelopeFollower recovers after one NaN / one +Inf sample"` (feed NaN then 1000 zeros → REQUIRE `getCurrentValue()` finite and < 1e-3), `"DCBlocker recovers after one NaN / +Inf sample"`. Processor test (`processor_lifecycle_state_test.cpp`, IEEE-flagged TU): `"Processor output stays finite after one NaN sample with env->mix routing and sweep env on"` → every later block finite (`Krate::DSP::detail::isFinite`) and sweep centre back above 20 Hz within 1 s. Fix: `envelope_follower.h:164-188` and `dc_blocker.h:194-211`: at entry `if (!detail::isFinite(input)) { reset(); return 0.0f; }` (pattern of `one_pole.h:107-111`); `modulation_engine.h:759` leave as is (NaN can no longer arrive). Build/gate `dsp_processors_tests dsp_primitives_tests dsp_ieee_tests disrumpo_tests` + header-consumers output; tidy dsp + disrumpo; **macOS-SENSITIVE** proxies mandatory.

### T2.18 — Drive gate is a crossfade, not a step  — 30 min (D-014)
Test (`distortion_adapter_test.cpp`): `"Drive 0 -> 1e-4 -> 1e-3 produces bounded level steps"` (SoftClip, mix 1, 1 kHz −20 dBFS; per-step level change < 3 dB; no sample jump > 0.1). Fix `distortion_adapter.cpp:191-195`: replace the hard gate with a blend `wetWeight = clamp(drive / 0.01f, 0, 1)`; output `dry*(1−mix·wetWeight) + wet·mix·wetWeight` with the wet path computed from `max(drive, 0.01f)`. Keep `band_processor.h:629-632`'s factor-1 shortcut only for `drive == 0` exactly.

### T2.19 — Controller/processor plumbing that dies on editor close or lifecycle order  — 120 min (D-057, D-070, D-045, D-056, D-172)
Tests (`unit/controller/editor_lifecycle_test.cpp`): `"Mod overlay and Tier-3 spectrum survive close/reopen"` (deliver ModOffsets once, didOpen→willClose→didOpen, tick the timer > 10 times → REQUIRE mod sliders receive offsets and `spectrumDisplay_` has FIFOs); `"Connect after activation opens the DataExchange queue"` (D-172: initialize, setupProcessing, setActive(true), then connect; process one block → REQUIRE a block or Tier-3 FIFO write happened); `"Two processors restoring the same stream keep distinct bridge registrations"` (D-056). Fix: (a) D-045 — replace the pointer message: processor copies the first `ModDest::kTotalDestinations` offsets into the existing `SpectrumBlock` (add `float modOffsets[64]` to `spectrum_block.h`, keep the 32-byte-rounded size) each send; controller keeps `std::array<float,64> cachedModOffsets_` and `updateModSliders` reads it; delete `sendModOffsetsMessage`/`"ModOffsets"` handling (`processor_messaging.cpp:149-165`, `controller.cpp:1966-1973`). (b) D-057/D-070 — `willClose` (`controller.cpp:1518, :1581`) stops nulling `sharedDisplay_`; it is re-looked-up in `didOpen` from `instanceId_` (`SharedDisplayBridge::lookupInstance`) and nulled only in `terminate`. (c) D-172 — `connect()` (`processor_messaging.cpp:36-72`): if `isActive_` (new flag set in setActive) call `dataExchange_->onActivate(processSetup)` immediately. (d) D-056 — `processor_state.cpp:903-917`: adopt the stored ID only if `lookupInstance(storedId) == nullptr`; `SharedDisplayBridge::unregisterInstance(id, data)` erases only when the stored pointer matches (**CROSS-PLUGIN IMPACT** `plugins/shared/src/...shared_display_bridge.*`; consumer: Innexus too — run header-consumers; tidy `-Target shared`).

### T2.20 — Same-family routing at control rate  — 60 min (D-005)
Test: CPU case in the `[performance]` lane (`dsp/oversampling_performance_tests.cpp`): 4 bands × 4 Formant nodes at the centre, 48 kHz, 10 s → REQUIRE < 5 % CPU; plus a bit-identity case that the render is unchanged by the optimisation (compare against the pre-change render captured in the same TU, tolerance 1e-6). Fix `morph_engine.cpp:691-718`: compute `interpolateParams()`/`interpolateCommonParams()` and call `setType/setParams/setCommonParams` only when the weights changed by more than 1e-6 since the last call or a `paramsDirty_` flag is set by `setNodes`/offset setters; cache `lastWeights_`.

### T2.21 — Keyboard hook does not swallow text-field keys (verify first)  — 45 min (D-068 PLAUSIBLE)
Verification test (`unit/keyboard_shortcut_test.cpp`, real `KeyboardShortcutHandler` with a stub `CFrame` holding a focused `CTextEdit`): send Left, Space, Tab → REQUIRE `event.consumed == false` for all three when a `CTextEdit` has focus. If green on current code, mark refuted and keep the test. Fix `keyboard_shortcut_handler.cpp:33-56`: return early (not consumed) when `frame_->getFocusView()` is a `CTextEdit` or lies inside an open modal overlay (`PresetBrowserView`/`SavePresetDialogView`); `handleArrowKey` returns the result of `adjustFocusedParameter` (true only when it edited); call `setActiveBandCount` from `BandCountDisplayController::update` (controller keeps a pointer).

### T2.22 — Missing sweep controls in the editor  — 90 min (D-069)
Test (`unit/controller/uidesc_inventory_test.cpp`, new): parse `editor.uidesc` and REQUIRE every automatable Sweep* tag (`SweepIntensity, SweepFalloff, SweepMorphLink, SweepLFOWaveform, SweepLFOSync, SweepLFONoteValue, SweepEnvSensitivity`) and `Band.NodeBias` is used by at least one `control-tag=`. Fix via the XSLT uidesc workflow: add an ArcKnob for Intensity (0-200 %, after Width at x≈390), a CSegmentButton Falloff, COptionMenu Morph Link (8 entries), COptionMenu LFO waveform, ToggleButton Sync + COptionMenu Note, ArcKnob Env Sensitivity to the sweep row (`editor.uidesc:614-634`, widen the row), and a Bias ArcKnob bound to `Band.NodeBias` in `BandExpandedStrip`. Delete the dead `CustomCurveEditor` creation (`controller.cpp:1123-1246`) and `customCurveVisController_` (:1492-1495) — the curve editor is out of scope per `specs/Disrumpo/roadmap.md:103`; keep `Custom` mode reachable only via state.

### T2.23 — Spectrum dividers follow the parameters; drag bounds never invert  — 60 min (D-071, D-109 PLAUSIBLE)
Tests (`unit/spectrum_display_test.cpp`): `"Divider drag with neighbours under one octave apart keeps order"` (set 500/600/700 Hz, drag divider 1 → REQUIRE ordered, no assert); controller test: `"setParamNormalized on a crossover moves the spectrum divider"`. Fix `spectrum_display.cpp:378-396`: if `leftBound > rightBound` collapse both to their geometric mean before `std::clamp`; `setNumBands` (:51-66) stops rewriting `crossoverFreqs_` (the processor redistributes; the controller pushes the param values); add a `CrossoverParamObserver` IDependent on the 3 crossover params that calls `setCrossoverFrequency(i, crossoverNormToHz(norm))` (codec helper from T1.17).

### T2.24 — Registration defaults and host text  — 60 min (D-072, D-101, D-169)
Tests: `unit/controller/editor_lifecycle_test.cpp` add `TEST_CASE("Disrumpo parameters start on their registered default", ...)` using `parametersStartingOffTheirDefault` (`registered_defaults_probe.h`) → REQUIRE empty (covers D-122's first half); `"Registered plain value matches processor denormalization"` for Env Attack/Release, LFO rates, Pitch Min/Max, Sweep Width/Intensity at norm {0, 0.5, 1} (codec helpers). Fix `parameter_registration.cpp`: set `getInfo().defaultNormalizedValue` after `setNormalized` for Band Count (:76 → 1.0) and Oversample Max (:90 → 2/3); Sweep Width default 1.5 oct (:198), Sweep Intensity range 0-200 % default 50 (:205-210); convert the plain `addParameter(title, units, ...)` mod params (:378-379, 414-415, 435-440, 452-455, 482-485, 488-495, 500-503, 506-507) to `RangeParameter`s with the processor ranges, Route Amount to −100..100 %, LFO rates to the log subclass from T1.17.

### T2.25 — Spectrum feed is contiguous  — 45 min (D-133)
Test (`integration/data_exchange_spectrum_test.cpp`): `"Spectrum FIFO receives a contiguous 1 kHz sine at 64-sample blocks"` → REQUIRE no phase jump between consecutive pushed samples (max |Δ| < 2π·1000/48000·1.1 in phase terms, or FFT bins > 2 octaves away < −60 dB). Fix `processor_messaging.cpp:90-143`: keep a 2048-sample mono ring for input and output written every block; when the throttle fires copy the latest 2048 contiguous samples into the block and the Tier-3 FIFOs (Tier-3 pushes every block unthrottled since the FIFO is lock-free).

### T2.26 — Expanded band panel tied to Band Count  — 30 min (D-170)
Test (`integration/expand_collapse_test.cpp`, with the editor harness): Band Count 4, Band3Expanded 1, Band Count 2 → REQUIRE container 9102 hidden; Band Count 4 → visible. Fix `animated_expand_controller.cpp:36-43, 65-100`: add a dependency on the Band Count parameter; `update()` computes `shouldShow = expanded && bandVisible` for either input and applies it.

## Phase 3 — measured defects not covered above (build, lint, preset levels)

### T3.01 — Preset re-levelling after the unity/pan/sweep fixes  — 120 min (D-144; absorbs the +3 dB from T1.08 and the sweep semantics from T1.11)
Prereq: all Phase 1 and T2.06/T2.07. Test (`integration/preset/factory_preset_validation_test.cpp`): `TEST_CASE("Every factory preset renders finite, bounded, non-silent audio", "[disrumpo][preset][long][D-144]")` — for each of the 121 presets: Rig 44.1 kHz, `setStateBytes`, 2 s pink noise at −12 dBFS → REQUIRE every sample finite, peak ≤ 0 dBFS, |DC| < 0.01, RMS > −60 dBFS. Fix: in `tools/disrumpo_preset_generator.cpp` lower `outputGain` (currently `0.5f` = 0 dB for every preset, line 237) per preset to meet the bar, regenerate the 119 generated presets, and decide the two orphan files (`Init/Init_5_Bands.vstpreset` — impossible at kMaxBands 4, delete it and the `{"Init", 5}` expectation at `factory_preset_validation_test.cpp:256`; `Experimental/Formant_Shift.vstpreset` — add it to the generator under Experimental so the count is 120 and docs stay true). Digital types: add output normalisation in the adapter for `Aliasing`, `SampleReduce`, `BitwiseMangler`, `Granular`, `Chaos`, `Spectral` — divide by `max(1, drive)` after `processRaw` so drive stops acting as linear gain (`distortion_adapter.cpp:211-215`). Standard workflow (plugin sources changed → pluginval; regenerate presets with the generator target, not by hand).

### T3.02 — uidesc-inventory lint: honour CScrollView container-size  — 30 min (D-149)
No plugin change. `tools/uidesc-inventory.js:88-93`: when the parent view is `CScrollView`, use its `container-size` as the child bounds. Add a self-test case (a CScrollView with a child at y > size but < container-size reports 0 out-of-bounds). Verify `node tools/uidesc-inventory.js --check plugins/disrumpo/resources/editor.uidesc` reports 0 out-of-bounds (the 20 overlaps are T3.03). Commit.

### T3.03 — uidesc overlaps  — 30 min (D-152 and the other 19 overlaps the lint reports)
Via the XSLT uidesc workflow: `editor.uidesc:1330` MorphSmoothing ArcKnob origin `75, 200` → `75, 204` (ActiveNodes spans y 182..202), then fix the remaining overlaps the lint lists until `uidesc-inventory.js --check` reports `overlap: 0`. Add that command to the Disrumpo test CMake as a `add_test` (NAME disrumpo_uidesc_inventory) so it gates. Build `Disrumpo`, pluginval, commit.

### T3.04 — Highway C4127 in KrateDSP build  — 20 min (D-154) — **CROSS-PLUGIN IMPACT** (build system only)
In `dsp/CMakeLists.txt` after `target_link_libraries(KrateDSP PRIVATE hwy hwy_contrib)` (line 35) add `target_include_directories(KrateDSP SYSTEM PRIVATE $<TARGET_PROPERTY:hwy,INTERFACE_INCLUDE_DIRECTORIES>)` (same approach as root `CMakeLists.txt:244-262` `smtg_mark_includes_as_system`). Check: a rebuild of `KrateDSP` logs 0 warnings (`grep -c "warning C" build log`). Commit.

## Phase 4 — duplication, spec-conformance and anti-pattern cleanups

Each task: Standard workflow; targets as named; no pluginval when only tests/docs/comments change.

### T4.01 — One codec for sweep/mod ranges (remaining copies)  — 45 min (D-088, D-091 — bulk done in T1.17)
Replace the remaining local `kMinWidth/kMaxWidth` (`processor.cpp:280-281`, `controller.cpp:1287-1290`, `visibility_controllers.cpp:548-551, 587-590`) and the env/rate literals in `processor_state.cpp:226-278, 655-731` and `processor_params.cpp:643-746` with `state_codec.h` helpers. Test: `controller_state_codec_test.cpp` round-trips every mod param at norm {0, 0.5, 1} through `processParameterChanges` → `getState` within 1e-5.

### T4.02 — One IDW weight function  — 60 min (D-089)
Add `static std::array<float,4> MorphEngine::computeWeights(MorphMode, const nodes&, int count, float x, float y)` holding the mode switch, epsilon, on-node snap and threshold; call it from `calculateMorphWeights`, `BandProcessor::computeRawMorphWeights` (`band_processor.h:576-615`, which must receive the mode — cache it from `setMorphMode`) and `MorphPad::recalculateWeights` (`morph_pad.cpp:833-868`; the pad gets the mode from `setMorphMode`). Test: for each mode × 25 cursor positions the three call sites return identical arrays.

### T4.03 — Shape-slot formatter reads the same descriptor table as the mapping  — 90 min (D-090; D-073 done in T4.22)
Create `plugins/disrumpo/src/dsp/shape_slot_descriptors.h`: `constexpr` table `(type, slot) → {min, max, curve, unit, labelsOrNull}`; `mapShapeSlotsToParams` and `formatShapeSlot` both read it. RingSat slot 6 formats as Hz or ratio depending on the node's slot 5 (formatter signature gains the full slot array). Test: for every type/slot parse the formatter text back and compare with the mapped `DistortionParams` field within 1 %.

### T4.04 — Note-value lists from the shared table  — 20 min (D-092)
`parameter_registration.cpp:274-279, 385-390, 421-426`: fill from `Krate::Plugins::Parameters::kNoteValueDropdownStrings` (`plugins/shared/src/ui/note_value_ui.h`) and set the default to `kNoteValueDefaultIndex`; `plugin_ids.h:704` → `using Krate::DSP::kNoteValueDropdownCount` plus `static_assert`.

### T4.05 — Distortion type names from `getTypeName`  — 20 min (D-093)
`parameter_registration.cpp:774-799` and `:847-856`: loop `i` over `kDistortionTypeCount` with `getTypeName(static_cast<DistortionType>(i))`. Rewrite `unit/band_strip_test.cpp:130-165` to compare the registered strings against `getTypeName` per enum value (delete the local D-number array).

### T4.06 — Type-parameter ranges: spec and code agree; Temporal default  — 45 min (D-102 remainder, D-171)
Decide per parameter (ruling goes in `specs/Disrumpo/specs-overview.md` FR-DIST-003/Appendix A first, then code): code ranges win for Temporal attack/release, Aliasing shift, Chaos speed, Formant shift (they match the DSP limits); spec wins for Folds (code maps 1-12 but the folder clamps at 10 → map 1-10). Update `distortion_adapter.h` field comments. `ShapeShadowStorage` Temporal slot0 default 0.0 (EnvelopeFollow); `distortion_adapter.h:127` and `specs-overview.md:219` list all four modes. Test: mapping endpoints equal the Appendix A min/max for every slot.

### T4.07 — Metering: implement or strike  — 30 min decision + 180 min if implemented (D-103)
Ruling for the user: either implement input/output peak+RMS meters (processor computes per block into the SpectrumBlock from T2.19; editor adds two `CVuMeter`s) or strike the line from `specs-overview.md:366` and `tasks-overview.md:307`. Default if no ruling: strike, with a CHANGELOG "Known limitation".

### T4.08 — Docs and CHANGELOG tell the truth  — 30 min (D-104)
After Phase 1 lands: `plugins/disrumpo/docs/index.html:118-123` keep "Intelligent oversampling (2x/4x)" and "MIDI CC learn" only because T1.07/T1.18 made them true; preset count to the regenerated number (T3.01); add an "Unreleased" CHANGELOG entry listing the audible changes (unity pan, sweep semantics, oversampling live, 8x→4x, state fixes) — version bump itself is out of scope (user rules version bumps).

### T4.09 — Stale limits and vacuous FR-019 tests  — 30 min (D-105, D-121)
Fix comments `processor.h:152`, `crossover_network.h:78, 110`, `plugin_ids.h:383`, `spectrum_display.h:61` (1-4 bands), `plugin_ids.h:185` ([1-4]); specs-overview FR-BAND-001/Appendix A → 1-4 bands, FR-MORPH-001 default 1 node. Delete `kMaxWeightThreshold`, `kWeightThresholdIncrement`, `kMaxGlobalProcessors` (`morph_node.h:141-148`) and the two vacuous cases `morph_processor_cap_test.cpp:122-168`; add `static_assert(kMaxBands * kMaxMorphNodes <= 16)` where the cap was documented.

### T4.10 — Dead state removed  — 30 min (D-110 remainder, D-134, D-173)
Delete `Processor::maxOversampleFactor_` (`processor.h:145`, store at `processor_params.cpp:357`) and `crossoverFrequencies_` (`processor.h:179`); via XSLT delete the `bitwise-group="bits"` container (`editor.uidesc:1107-1110`), `bitsContainer_` handling in `sub_controllers.h:300-345` (and the `&& bitsContainer_` condition at :313), `bitwiseBits` field (`distortion_adapter.h:121`) and `morph_node.h:113`. Re-run `unit/stack_footprint_test.cpp`.

### T4.11 — Comments that contradict code  — 20 min (D-112)
`band_processor.h:466` (process() has no oversampling — after T1.07 it still does not; say so), `plugin_ids.h:156` (output IDs are inside the global range), `plugin_ids.h:318` (band nibble 0-3), `morph_engine.h:40` (setters are NOT thread-safe; call from the audio thread or via T1.24's pending state), `processor_params.cpp:456` ([0,29]), `controller.h:299` (second definition, add `static_assert(sizeof(SharedDisplay) == ...)` in both TUs or move the struct to a plugin-local header both include).

### T4.12 — IEEE classification in test TU  — 10 min (D-113)
`integration/multi_node_distortion_test.cpp:693` → `Krate::DSP::detail::isFinite`; remove the baseline entry for that file from `tools/lint-nonfinite-symbols-baseline.json` so the ratchet goes down. **macOS-SENSITIVE** proxies.

### T4.13 — Crossover3Way/4Way allpass Q  — 20 min (D-164) — **CROSS-PLUGIN IMPACT** (`crossover_filter.h`; consumers: `fractal_distortion.h`, `bass_stereo_field.h`, Disrumpo)
`crossover_filter.h:423, :643` → `kAllpassQ = 0.70710678f`, fix comment. Test (`dsp/tests/unit/processors/crossover_filter_test.cpp`): flat-sum sections at 300/600 Hz and 80/160/320 Hz with compensation on, REQUIRE deviation < 0.1 dB (today −0.129/−0.149 dB). Build/gate `dsp_processors_tests` + header-consumers; macOS proxies.

### T4.14 — fastTanh NaN path without the constant  — 10 min (D-166) — **CROSS-PLUGIN IMPACT** (`fast_math.h`; 13 dsp headers + Disrumpo)
`fast_math.h:67-68` → `return x;`; remove the `fast_math.h` entry from `tools/lint-nonfinite-symbols-baseline.json`. Existing `dsp_ieee_tests` NaN case still passes. Build/gate `dsp_core_tests dsp_ieee_tests` + header-consumers; macOS proxies.

### T4.15 — Diode doc and −Inf (verify first)  — 15 min (D-167 PLAUSIBLE)
Verification: `dsp_ieee_tests` case "Diode returns −2 for −Inf"; today NaN. Fix `sigmoid.h:363-365`: `if (detail::isInf(x)) return -2.0f;` on the negative branch; `waveshaper.h:44, :58` doc → "bounded to (−2, 1)". Consumers of `waveshaper.h`: disrumpo, membrum, ruinae + 13 dsp headers; macOS proxies.

### T4.16 — AllpassSaturator reconfigures only on change  — 45 min (D-084) — **CROSS-PLUGIN IMPACT** (`allpass_saturator.h`; Disrumpo only)
`allpass_saturator.h:733-752`: cache last smoothed freq/drive; reconfigure only the active topology's filters and only when either changed by > 1e-6. Test: bit-identity against the current output (capture in-TU) plus a `[performance]` CPU case.

### T4.17 — LFO RNG and symmetry edge  — 30 min (D-165, D-015) — **CROSS-PLUGIN IMPACT** (`lfo.h`; disrumpo, profundum, ruinae, shared + 20 dsp headers)
`lfo.h:477` → `randomState_ = static_cast<uint32_t>((uint64_t{randomState_} * 48271u) % 2147483647u);` guard seed 0; add `setSeed(uint32_t)`; `modulation_engine.h` prepare seeds `lfo2_` with a different value. `lfo.h:440` mask `index0 & kTableMask` (and keep `applySymmetry` in double at :124-126). Tests in `dsp/tests/unit/primitives/lfo_test.cpp`: MINSTD sequence from seed 1 (48271, 182605794, …); two LFOs with different seeds differ; symmetry 0.3 with phase 1−1e−8 → index < kTableSize. Build/gate `dsp_primitives_tests` + header-consumers (expect ruinae_tests, profundum_tests, shared_tests); macOS proxies.

### T4.18 — Allpass glides with its LR4  — 45 min (D-024)
`crossover_network.h:132-149`: smooth the allpass frequency with the same 5 ms `OnePoleSmoother` per index and reconfigure coefficients per sample from the smoothed value (hysteresis 0.1 Hz like `crossover_filter.h:106`); apply `kDefaultSmoothingMs` or delete it (:49). Test (`unit/crossover_network_test.cpp`): 3 bands, crossover 1 stepped 500→2000 Hz in one block; STFT magnitude of the summed output within 0.5 dB of input in every 256-sample frame.

### T4.19 — Coalesce node re-pushes (verify first)  — 30 min (D-006 PLAUSIBLE)
Verification: count `MorphEngine::setNodes` calls (test counter from T1.24) for a `ParameterChanges` batch holding all 17 node params of one band → REQUIRE 1 after the fix (today 17). Fix `processor_params.cpp:830-899`: set `bandDirty_[band]` and push once per dirty band after the queue loop.

### T4.20 — Custom-curve rebuild terminates (verify first)  — 20 min (D-044 PLAUSIBLE)
Verification test (`serialization_round_trip_test.cpp`): v13 stream with a 3-point curve whose middle x = 1.0, setState, getState, setState again → REQUIRE it returns (use a 5 s watchdog thread that fails the test). Fix `processor_state.cpp:552-555`: `customCurve_.reset()` then add points; `custom_curve.h:150-152` identify endpoints by index 0 and `count_-1`, not by x; reject non-finite x/y with `detail::isFinite`.

### T4.21 — Bound the controller MIDI blob  — 10 min (D-059)
`controller.cpp:690-692`: `if (midiDataSize > 0 && midiDataSize <= 4 + 128 * 8)`; test: `setState` with `midiDataSize = 0x7FFFFFFF` returns kResultOk without allocating.

### T4.22 — Shape-slot host text uses the owning node's type  — 15 min (D-073)
`controller.cpp:765-777`: read `makeNodeParamId(band, extractNodeIndex(id), kNodeType)` instead of `kBandDisplayedType`. Test in `controller_state_codec_test.cpp`: node B Tube, node A Bitcrush selected → B's Shape 0 string formats as Tube.

## Phase 5 — test-gap fills

Tests only (no plugin source), so steps 3-4 build `disrumpo_tests` and gate it; no pluginval; tidy still runs. Each case must be RED against a deliberately broken build or against the pre-fix commit before it is accepted as coverage (Completion Honesty: a test that cannot fail is not coverage).

### T5.01 — Factory presets rendered and migrated  — 60 min (D-118)
Beyond T3.01's render case: `"v10 factory preset routing destinations migrate to the 8-per-band layout"` — read one v10 preset's raw routing dest bytes (`processor_state.cpp:775-790` layout) and REQUIRE `getState` after load shows `kBandBase + band*8 + param`. Fix the category expectations after T3.01's orphan ruling.

### T5.02 — Registration vs processor mapping  — 45 min (D-122)
`"Registered plain value matches processor denormalization"` extended to every continuous Disrumpo parameter: for norm ∈ {0, 0.25, 0.5, 1} compare controller `param->toPlain(n)` with the processor's value read back through getters (`sweepProcessor_.getWidth()`, `crossoverL_.getCrossoverFrequency`, node drive via `getBlendedParams`), REQUIRE within 1 %.

### T5.03 — Replace tautologies with production calls  — 90 min (D-123)
`unit/global_controls_test.cpp:25-193`, `unit/visibility_test.cpp`, `unit/keyboard_shortcut_test.cpp:20-146`: delete the local lambdas/ints; construct `ContainerVisibilityController` with the real Band Count `StringListParameter` from `registerAllParameters` and REQUIRE band b visible iff count > b for counts 1-4; construct `KeyboardShortcutHandler` with a stub frame, send Tab and REQUIRE the focused band advances; `band_strip_test.cpp` as in T4.05.

### T5.04 — Crossover behaviour tests that can fail  — 45 min (D-124)
`unit/crossover_network_test.cpp:402-417`: either REQUIRE `f1 >= f0 * 2^0.5` after a spacing rule is added to `setCrossoverFrequency` (ruling: enforce 0.5 octave in the DSP too, since the UI already does) or record the ruling in the spec; `integration/band_management_integration_test.cpp:137-172` IT-003 → measure the transient (reuse T2.08's case); add `"setState restores crossovers when current band count is lower"` (done in T1.03 — only verify it exists).

### T5.05 — Latency tests measure  — 15 min (D-125)
Verify T2.10 replaced `oversampling_performance_tests.cpp:32-125, 253-276` with measured cases without `[performance]`; the only `[performance]` cases left in that TU are the CPU benchmarks (:130-250).

### T5.06 — Assertions that cannot fail  — 45 min (D-127)
`unit/morph_band_integration_test.cpp:271, :288` (done in T1.05 — verify); `serialization_round_trip_test.cpp:660-689`: truncate after the band count and REQUIRE inputGain applied and bands at defaults; `distortion_adapter_test.cpp:1907-1965` UT-DI-008: wrap `setType` + `setParams` + 4096 `process` for all 26 types (Spectral at fftSize 1024/2048/4096) in `AllocationScope` and REQUIRE 0 allocations (after T1.01 this is GREEN; confirm it is RED on the pre-T1.01 commit by `git stash`/worktree before accepting).

### T5.07 — DataExchange delivery test gated by platform  — 15 min (D-128)
`integration/data_exchange_spectrum_test.cpp:200-202`: drop `[!mayfail]`; wrap the delivery REQUIREs in `#if defined(_WIN32)`; on other platforms assert only that `sendSpectrumBlock` wrote the Tier-3 FIFO.

### T5.08 — NaN/Inf, 192 kHz, mono bus, numSamples 0, concurrent setState  — 90 min (D-129, D-003 follow-up)
`processor_lifecycle_state_test.cpp` (IEEE TU): per type inject one NaN and one +Inf sample (bit pattern via `volatile uint32_t`), then 4096 finite samples → REQUIRE every output after 1024 samples finite (`detail::isFinite`); sample-rate sections 44.1/96/192 kHz on the default render; `setBusArrangements(mono)` → kResultFalse; `process` with `numSamples == 0` and `numChannels == 1` returns without touching buffers. Add a two-thread case tagged `[long]`: one thread loops `processBlock`, another calls `setStateBytes` 200 times → REQUIRE finite output and no crash (run once under the ASan build when available).

### T5.09 — Bitcrush changes the signal; CPU budgets restored  — 30 min (D-130)
`integration/bitcrush_noise_repro_test.cpp:229-321`: REQUIRE RMS(bitcrush − default) > 0.05 and ≤ 4 distinct output levels for 1 bit. `distortion_adapter_test.cpp:1802, :2174, :2248`: restore the 5 % / 2 % / 10 % bars as REQUIREs in the `[performance]` lane (run alone via `node tools/run-cpu-tests.js disrumpo_tests`; never relax them — if red, optimise). Add a Processor-level benchmark (4 bands × 4 nodes, same-family Experimental and cross-family) with a 15 % bar per CLAUDE.md NFR.

### T5.10 — Portable preset path  — 10 min (D-131)
`factory_preset_validation_test.cpp:115-134` → `return fs::path(DISRUMPO_RESOURCES_DIR) / "presets";` (T1.03 already uses the macro; delete the candidate list and the F:/ fallback).

## Phase 6 — promote the audit probes into regression tests

Source: `specs/disrumpo-audit/probes/audit_probes.cpp.txt` (21 `[.][audit-probe]` cases, :548-1589), `audit_followup_probes.cpp.txt` (:414, :516, :651, :766), `audit_band_arrival_probe.cpp.txt` (:330). Each promoted case gets a visible tag set `[disrumpo][audit][<area>]` (no `[.]`), real REQUIREs with the thresholds below (the probe's "ok" bars), lives under `plugins/disrumpo/tests/dsp/` or `tests/integration/`, is registered in `plugins/disrumpo/tests/CMakeLists.txt` and the IEEE `set_source_files_properties` list, and reuses `disrumpo_rig.h` (T1.00) plus the probe helpers moved into `plugins/disrumpo/tests/test_support/audit_metrics.h` (`continuityAround`, `nonharmonicDb`, `magAt`, `countClicks`, `centroidHz`). Cases over ~15 s carry `[long]`; CPU-time cases none. Keep the probe files in `specs/disrumpo-audit/probes/` untouched as the audit record. Every promoted assertion must be re-run and compared with the baseline table in this plan's source material; record the new value next to the old one in `report.md`'s probe baseline table.

### T6.01 — Per-type render matrix  — 90 min (`audit probe: per-type A-D` → `tests/dsp/type_render_matrix_test.cpp`)
For all 26 types × drive {0.25, 0.75, 1.0}, 1031.25 Hz −6 dBFS, 48 kHz, 1 band: REQUIRE nonfinite == 0; nonharmonic ≤ −50 dB for types whose `getRecommendedOversample` ≥ 2 (baseline: HardClip −43.3, SineFold −3.1, Tape −23.6, Fractal −24.2 → all must pass after T1.07/T1.06); peak ≤ +6 dBFS (baseline Aliasing +11.1 → T3.01); |DC| < 1e-3 for bias 0 (baseline Tube −0.041 → T1.22); bias ±1 → |DC| < 1e-3 for the DC-blocked types; self-noise ≤ −90 dBFS (baseline Stochastic −10.4, Quantize −49.0); tail ≤ −90 dBFS 1 s after input stops for all but FeedbackDist/AllpassResonant with feedback > 0 (document those as intended tails, REQUIRE they decay below −60 dBFS within 5 s).

### T6.02 — Tone response and adapter-vs-processor parity  — 45 min (`tone filter effective cutoff`, `adapter-vs-processor` → `tests/integration/processor_rate_parity_test.cpp`)
REQUIRE processor level at 1k/2k/4k/8k within 1 dB of the adapter prepared at 48 kHz (baseline −2.98/−7.00/−12.46/−18.93 vs −0.25/−0.94/−2.91/−6.59); for all 26 types REQUIRE |RMS diff| ≤ 1 dB and centroid ratio 0.85-1.15 (baseline 19 bad; SergeFold 0.077, FeedbackDist 0.282).

### T6.03 — Stereo independence  — 20 min (`stereo independence` → folded into `processor_stereo_test.cpp` from T1.05; verify the 7 types listed in the baseline are all covered: SoftClip, Tape, Tube, FeedbackDist, AllpassResonant, Temporal, Formant, bar R ≤ −120 dBFS → use −90 dBFS as the REQUIRE floor to stay above float noise).

### T6.04 — Crossover reconstruction, unity, dry/wet  — 30 min (`crossover reconstruction` → `tests/dsp/crossover_reconstruction_test.cpp`)
Network direct 2/3/4 bands after 0.3 s pre-roll: max deviation ≤ 0.1 dB (baseline 0.000/0.002/−0.004, keep green); startup-ramp case: deviation ≤ 0.5 dB at the first sample after prepare (baseline 12.3 dB → T1.03 snap fix); processor 1-4 bands ripple ≤ 0.2 dB (baseline 0.007); unity 0 dB ± 0.1 (baseline −3.010 → T1.08); mix 50 % deepest notch ≥ −1 dB (baseline −16.66 → T1.09).

### T6.05 — Oversampling engaged and latency  — 30 min (`oversampling and latency`, `per-band arrival` → `tests/integration/processor_oversampling_test.cpp`)
REQUIRE ≥ 12 dB nonharmonic reduction 1x→4x for HardClip (baseline 0.0 → T1.07); per-band relative arrival 0 samples HardClip vs Bitcrush and HardClip vs SoftClip (baseline 4 and 3 → T2.10 alignment); `getLatencySamples()` equals the measured group delay (baseline reports 0 against 4.4 samples).

### T6.06 — Morph continuity, on-node, smoothing, stale gain  — 45 min (`morph continuity`, `morph on-node and smoothing`, followup `stale transition gain on node` → `tests/dsp/morph_continuity_test.cpp`)
4-node cross-family planar sweep: jump ratio ≤ 1.3, clicks 0, |level dev| ≤ 1 dB (baseline 9.90/54/−19.51); 2-node SoftClip-SineFold: same bars (baseline 1.83/1/−12.95); same-family SoftClip-Tube stays ≤ 1.3 (baseline 1.00); on-node weights sum 1, finite, within 0.5 dB of the node alone (baseline ok); smoothing time to 99 % = 10 ms ± 30 % (baseline 37.33 ms); planar→Linear1D switch on node A within 0.5 dB of A alone (baseline −23.52 dB); fresh Linear1D 4 nodes at x=0 within 0.5 dB (baseline −3.01 dB).

### T6.07 — Automation steps and sweep system  — 45 min (`automation steps`, `sweep system` → `processor_sweep_test.cpp` from T1.10/T1.11 + `tests/integration/processor_automation_test.cpp`)
Band count 2→4→1 jump ratio ≤ 1.3 (baseline 3.78/3.90 → T2.08); type switch ≤ 1.3 (baseline 1.37 → T2.13); drive/mix/gain/mute/solo/OS-max steps stay ≤ 1.3 (baseline ok); bypass changes the output (baseline 0 → T1.07); LFO 5 Hz over 2 s: ~4 oct range, ~10 cycles (baseline 0.244 oct, 0 cycles); env attack 63 % within 40 ms (baseline 320 ms); off-centre band level delta > −6 dB (baseline −124.76); intensity 200 % reaches multiplier 2.0 at the band (baseline clamped); full-automation render finite, jump ratio ≤ 1.3, RMS(on) within 3 dB of RMS(off) (baseline −9 dB loss).

### T6.08 — Presets: load, host order, round trip, levels (with the reloaded render measured)  — 60 min (`presets 0/8..7/8` → extend `factory_preset_validation_test.cpp`, tag `[long]`) (D-160, D-161)
For every preset: setState on an active processor and setState BEFORE setupProcessing render within 1e-5 of each other (baseline 120/121 differ → T1.03); getState→setState round trip max diff ≤ 1e-5 (baseline 35 presets up to 1.25 → T1.04/T1.03/T1.12) AND, per D-161, measure peak/RMS/DC of the reloaded render b and REQUIRE `rms_b > −60 dBFS`, `|rms_b − rms_a| < 0.1 dB`; per D-160 render each preset twice — via setState and via replaying every parameter through `processParameterChanges` (controller `parseComponentState` with a setter that records `(id, value)` pairs) — and REQUIRE the two renders agree within 1e-4 (today the setState path skips slot mapping → T1.15); level bars from T3.01 (peak ≤ 0 dBFS, |DC| < 0.01, RMS > −60).

### T6.09 — Sample-rate consistency, allocations, NaN injection, denormals  — 45 min (`sample-rate consistency`, followup `audio-thread allocations`, `NaN and Inf input injection`, `denormals in silence tail` → `processor_lifecycle_state_test.cpp` / `tests/integration/processor_rt_safety_test.cpp`)
44.1k/96k/192k vs 48k reference: |RMS delta| ≤ 1 dB and centroid within 15 % (baseline 192 kHz 2.50 dB / 0.824 → T1.06); zero allocations in `process()` across a node-type change to Spectral and across an FFT-slot change (baseline: allocations); NaN/Inf injection per type finite within 1024 samples; denormal tail case stays in the CPU lane (`[performance]`, run with `tools/run-cpu-tests.js`).

## Completion checklist (Completion Honesty — fill only from evidence produced in this run)

For every task before it is marked done:

- [ ] The failing test was run alone by name BEFORE the fix and its RED output is pasted into the task's commit message (file, `TEST_CASE` name, the failing assertion with its measured value). For PLAUSIBLE findings (D-021, D-038, D-040, D-068, D-109, D-145, D-168, D-006, D-044, D-034, D-167, D-121) the RED/GREEN result of that first run decides CONFIRMED vs REFUTED and is written into `report.md`'s verdict column; a refuted finding keeps its test and gets no fix.
- [ ] After the fix the same test is GREEN and the gate line reads `GATE GREEN` from `node tools/run-gate.js --share 4 --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' disrumpo_tests [<header-consumers executables>]`; the exact command and the `GATE` line are in the commit message. Timing-budget cases of untouched code that went red under concurrent load are recorded as load, not re-run as evidence.
- [ ] Every finding row in the table above is closed with `file:line` evidence of the changed code (re-read after the edit; the plan's line numbers shift as tasks land — cite the post-edit lines) and the test's actual output, not "implemented" or "passes".
- [ ] No threshold in a promoted probe or an existing test was relaxed: the bars are exactly those listed in Phase 6 (≤ 1.3 jump ratio, ≤ 1 dB level deviation, ≤ −50 dB nonharmonic for OS ≥ 2 types, ≤ −90 dBFS self-noise, ±0.1 dB reconstruction, 0 dB ± 0.1 unity, ≥ −1 dB mix-50 notch, 10 ms ± 30 % smoothing, ≤ 1e-5 round-trip, ≤ 1e-4 host-order/preset parity). CPU budgets (5 % / 2 % / 10 % / 15 %) were restored, never widened, and run alone via `tools/run-cpu-tests.js`.
- [ ] No feature was quietly removed: the only deletions are the ones this plan names (8x oversampling path → 4x alias with the "8x" string kept; processor-side sweep MIDI learn → replaced by IMidiLearn; MorphSweepLinkController and `controller/morph_link.cpp` → replaced by the processor link; CustomCurveEditor creation → out of scope per roadmap, state still carries the curve; dead adapter members; `Init_5_Bands` preset → ruled impossible at 4 bands). Each is listed in the CHANGELOG "Unreleased" entry (T4.08) and in `report.md`.
- [ ] STATE-COMPAT tasks (T1.12 v13, T2.14, T2.15, T1.06's 8x alias, T1.17's log parameters) each have a fixture-stream migration test of the previous version checked into `plugins/disrumpo/tests/fixtures/` and GREEN; no parameter type changed at an existing ID (grep the registration diff for `new Steinberg::Vst::` on an existing ID).
- [ ] CROSS-PLUGIN tasks (T1.01, T1.02, T1.22, T1.23, T2.03, T2.16, T2.17, T2.19, T3.04, T4.13, T4.14, T4.15, T4.16, T4.17) ran `node tools/header-consumers.js --tag '[disrumpo]'` after the edit and the named executables were in the gate command; the macOS proxies (`tools/macos-fastmath-repro.sh`, `node tools/lint-apple-globals.js`, `node tools/check-portability.js`, then `wsl --shutdown`) ran green before any push is proposed.
- [ ] pluginval (`tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Disrumpo.vst3"`) exit code 0 after every plugin-source task, and `./tools/run-clang-tidy.ps1 -Target disrumpo -BuildDir build/windows-ninja -Jobs 4` (plus `-Target dsp` / `-Target shared` where touched) reports 0 warnings before each commit; one heavy job at a time (`-m:2`, `--share 4`, `-Jobs 4`).
- [ ] All 21 original probes plus the 5 follow-up probes were re-run as their promoted tests (Phase 6) and every measured value is written next to its baseline value from the MEASUREMENT BASELINE table (`f:/tmp/disrumpo-audit/probe-results.txt`) in `report.md`; any value still outside its bar is an open finding, not a footnote.
- [ ] `git status --porcelain` shows no stray files; `node tools/clean-test-output.js --apply` was run at the end (and never while a run was writing to `f:/tmp`); nothing was pushed.

