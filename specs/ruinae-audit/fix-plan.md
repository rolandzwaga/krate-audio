# Ruinae audit — fix plan

**Date:** 2026-11-16 · **Branch:** `audit/ruinae` · **Anchors valid at:** `f8ed81e1` · **Companion:** [report.md](report.md) (finding prose; this plan only references ids)

## Scope

This plan turns the 189 verified findings of the 2026-11-16 Ruinae audit (ids between R-001 and R-194) into 99 ordered tasks. It covers the plugin (`plugins/ruinae/`), the shared UI and parameter code it uses (`plugins/shared/`) and the KrateDSP headers it consumes (`dsp/include/krate/dsp/`). It is written for a coding agent that has no memory of the audit: every task names the files and symbols to change, the behaviour before and after, the failing test to write first and the acceptance check. Nothing here has been implemented. `specs/ruinae-audit/implementation-plan.md` and `findings.json` belong to the earlier audit (F001–F042) and must not be edited or used as a command reference.

## How to read an anchor

Line numbers were re-read at commit `f8ed81e1`. Earlier tasks move lines, so every anchor also quotes a symbol or a code fragment. If the line no longer matches, find the quoted symbol with Grep; never edit by line number alone. Paths are repo-relative. `ruinae/` abbreviates `plugins/ruinae/`, `dsp/` abbreviates `dsp/include/krate/dsp/`.

## Rulings needed before the affected task starts

These tasks change how existing presets or projects sound, or pick between two defensible designs. Each lists the default this plan assumes. Ask the user once, before starting the task; if no answer is available, stop at that task and continue with the next independent one.

| Task | Question | Default assumed here | Ruling (user, 2026-11-16) |
|------|----------|----------------------|---------------------------|
| T1.26 | LFO 1/2 Depth and Chaos Depth never reached the audio. Once wired, should states saved before the fix keep their old (full-depth) sound? | Yes: states with version < 9 load depth 1.0 for all three; the generator's authored depths are rewritten to 1.0 so factory sound is unchanged | **Default confirmed** |
| T1.27 | Velocity never reached the VCA. Add a "Velocity → Amp" amount; what do old states get? | New parameter, default 1.0 for new instances; states with version < 9 load 0.0 (old sound); factory presets regenerated at 1.0 | **Default confirmed** |
| T2.7 | Formant Morph becomes an offset from Vowel (position = clamp(vowel + morph, 0, 4)). Today the vowel is ignored, so 11 factory presets and every project with a non-A vowel would move. Keep their sound? | (added 2026-11-18; the T2.7 implementer stopped because the plan's PRESET-IMPACT line understated this) Keep the sound | **Keep current sound (user, 2026-11-18).** States with version < 9 load each Formant Vowel as A and keep Morph, which is exact because the vowel had no effect before. The generator re-authors every Formant preset to vowel = floor(today's position), morph = the fraction. Exceptions that take the new sound on purpose, as their comments say: Vowel Descent oscB (E/0, "pure E") and Vocal Bass oscA (O/0.6 = 3.6, "just past O toward U"). See the T2.7 ruling bullet. |
| T2.9 | Distortion types at drive 0 are silent or −24 dB. Change the drive law? | Only the drive-0 end is made neutral; the law above ~10 % drive is kept so presets do not shift | **Default confirmed** |
| T2.19 | Voice-route Filter Resonance scale 1 Q vs 10 Q for the global route | Raise to 10 Q for new routes; states with version < 9 divide stored amounts by 10 so they sound the same | **Default confirmed** |
| T2.20 | "Voice LFO" has no rate or shape control | Add two parameters (rate, shape) defaulting to 1 Hz sine, so the five presets that use it are unchanged | **Default confirmed** |
| T2.24 | Comb filter at max resonance has +22 dB of gain | Add level compensation `(1 − feedback)` blended in above feedback 0.5; PRESET-IMPACT listed in the task | **Default confirmed** |
| T2.32 | Should a free-rate arp run while the DAW transport is stopped (R-073)? Gradus Live mode does | Yes for free rate; tempo-synced stays transport-gated | **Default confirmed** |
| T3.6 | Tape-delay hiss at wear 0 (R-154) and spectral-delay hop quantisation (R-160) are documented designs | Leave the DSP; document both in MANUAL.md | **Split.** Tape hiss (R-154): **OVERRIDDEN, fix in the DSP**: silent at Wear 0 and Age 0, unchanged above (T3.6a). Spectral hop quantisation (R-160): first answered "fix", then, shown the trade-off (a fix needs 75 % overlap at 2× CPU, or relaxes the ±1 dB level pin to about ±4 dB), **document only, default confirmed** (T3.6b). |
| T4.8 | Truncated state: partial apply with `kResultTrue` (R-076) is pinned by two tests | Keep the contract; add the missing engine reset only | **OVERRIDDEN: reject truncated state** (return an error, leave the current state untouched). The two pinning tests (`preset_safety_test.cpp:178`, `state_migration_test.cpp:97`) are rewritten. Scope: only a cut inside the required prefix (before the voice routes) is rejected; tail packs stay tolerant because they were appended without version bumps. The T4.8 R-076 bullet below is rewritten accordingly. |

## Workflow every task follows (mandatory, in this order)

Each task below states steps 1 and 2 in full and names the build targets and gate executables for steps 3 and 4. Steps 3–6 are always run exactly as written here.

1. **Failing test first.** Create or extend the named test file, add the named `TEST_CASE` with the named tags, build, run that one case directly (`build/windows-x64-release/bin/Release/<exe>.exe "<TestName>"`) and confirm it FAILS on current code. For findings marked **PLAUSIBLE** ("verify first, then fix"): if the test passes on current code, stop; keep the test, record the finding as refuted in the commit message and in the completion checklist, and do not change production code.
2. **Implement the fix** in the named files.
3. **Build, capped:**
   `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests -- -m:2`
   Append `Ruinae` to the target list when a file under `plugins/ruinae/src/`, `plugins/ruinae/resources/` or `plugins/shared/src/` changed. Append `dsp_<layer>_tests` (and `dsp_ieee_tests` for NaN/Inf cases) when a `dsp/` file changed. Append `gradus_tests` for a GRADUS IMPACT task. Zero warnings; the post-build copy to `C:/Program Files/Common Files/VST3/` may fail and that is fine.
4. **Run sharded, never serial:**
   `node tools/run-gate.js --share 4 --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' ruinae_tests`
   When a `dsp/include` or `plugins/shared` file was MODIFIED: first run `node tools/header-consumers.js --tag '[ruinae]'` (Ruinae's cases have no common tag, so `ruinae_tests` appearing in its list is expected), build every executable its last line names, and add them to the same `run-gate.js` command. The last line must read `GATE GREEN`. A single case by name is the only direct `.exe` run.
5. **pluginval** when plugin sources changed: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"` (the exit code is the verdict; `0xC0000005` is a crash). Then **clang-tidy** before the commit: `./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja -Jobs 4` (add `-Target dsp` or `-Target shared` runs when those trees changed). ONE heavy job at a time: never run the build, the gate, pluginval and clang-tidy concurrently.
6. **Commit:** `git add <files>`, write the message to a file with the Write tool, `git commit -F <message file>`. Never `--amend`, never push, one git operation per command. End the message with `Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>`.

### Flags used in the tasks

- **CROSS-PLUGIN IMPACT** — the task edits `dsp/include/krate/dsp/` or `plugins/shared/`. Before editing, run `grep -rl "<header name>" plugins/ dsp/include` and list every consumer in the commit message. The consumers named in each task were read from the audit; the grep is authoritative.
- **GRADUS IMPACT** — the task edits `dsp/…/processors/arpeggiator_core.{h,cpp}`, `primitives/arp_lane.h` or `plugins/shared/src/parameters/arp_params_common.h`. Build `gradus_tests`, run it in the same gate, and keep the byte golden in `tests/test_helpers/arp_shared_prefix_golden.h` green. Gradus goldens (`plugins/gradus/tests/…golden_midi*.txt`) that change must be diffed and justified exactly like Ruinae's.
- **STATE-COMPAT** — the saved-state meaning or layout changes. The branch uses ONE new version: the first STATE-COMPAT task to run sets `kCurrentStateVersion = 9` (`ruinae/src/plugin_ids.h:20`) and `kStateVersion = 9` (`tools/ruinae_preset_format.h:48`); later STATE-COMPAT tasks reuse 9. Every such task adds its gate on BOTH load paths (`Processor::setState` and `applyPresetSnapshot` in `ruinae/src/processor/processor_state.cpp`, and `Controller::loadStateCore` in `ruinae/src/controller/controller_presets.cpp:389`), updates `tools/ruinae_preset_format.h` and `tools/ruinae_preset_generator.cpp` when factory presets carry the field, and adds a migration case beside `ruinae/tests/unit/state_migration_test.cpp` that loads a version-8 blob and checks the migrated values. Never change a parameter's type at an existing ID. Never renumber an arp ID (3000–3445 are shared with Gradus). New fields are appended at the end of the stream, after the arp pack.
- **PRESET-IMPACT** — a factory preset's audio or an arp preset's MIDI changes. The task names the fixtures under `ruinae/tests/fixtures/` to regenerate (`RUINAE_REGEN_GOLDENS=1` with the SC-004b case; see T0.1 for the procedure) and how to justify each diff.
- **macOS-SENSITIVE** — run before pushing: `tools/macos-fastmath-repro.sh` (DSP TUs), `node tools/lint-apple-globals.js`, `node tools/lint-musical-time-truncation.js`, `node tools/check-portability.js`. Classify finiteness only with `Krate::DSP::detail::isFinite / isNaN / isInf` (`core/db_utils.h`); no `std::isnan`, no `numeric_limits::infinity()/quiet_NaN()` constants in fast-math code (in IEEE test TUs build NaN from a bit pattern through a `volatile`), `std::llround` for musical time.

### Shared test conventions

- Processor-level tests reuse the fixtures already in the suite: `ArpIntegrationFixture` (`ruinae/tests/unit/processor/arp_integration_test.cpp:188-291`), the processor rig in `ruinae/tests/integration/processor_audio_test.cpp`, `drain_preset_transfer.h`, and the helpers under `tests/test_helpers/`: `vst_event_list.h`, `vst_param_changes.h`, `artifact_detection.h` (click detection), `spectral_analysis.h` and `signal_metrics.h` (pitch, RMS), `midi_golden_compare.h`, `editor_lifecycle_harness.h`, `registered_defaults_probe.h`, `terminate_with_editor_open_probe.h`, `render_fingerprint.h`. Read the helper header for exact function names before use; do not write a second copy of a helper that exists.
- The audit probes (`specs/ruinae-audit/probes/audit_probes.cpp.txt`, `view_switch_lifecycle_probe.cpp.txt`) already contain a working rig, pitch estimator, click metric (`clickAt`, "jump ratio") and block-invariance renderer. Phase 1–5 tests may copy the specific helper they need into the new test file; Phase 6 moves the shared rig into `ruinae/tests/audit_rig.h` and de-duplicates.
- New test files must be added to `ruinae/tests/CMakeLists.txt` (the `add_executable(ruinae_tests` list at line 10; files with NaN/Inf injection also go in the IEEE `set_source_files_properties` block at line 203).
- Thresholds in this plan are final. Do not relax one to get a green run; if a threshold cannot be met, stop and report.

## Findings → tasks

Verdict: C = CONFIRMED, P = PLAUSIBLE (verify first). Severity is the final severity after verification.

| Finding | Title (short) | Severity | Verdict | Task | Phase |
|---|---|---|---|---|---|
| R-193 | ModSource switch leaves ten rate/note-value group pointers dangling (ASan) | critical | C | T1.3 | 1 |
| R-147 | Huge OSC PhaseMod from state hangs the audio thread (`wrapPhase`) | critical | C | T1.1 | 1 |
| R-134 | Reverb type crossfade re-armed every block | critical | C | T1.4 | 1 |
| R-114 | Arp disable test is vacuous and hides a stuck note | critical | C | T1.6 | 1 |
| R-080 | DistortionViewMode switch leaves spectral pointers dangling | critical | C | T1.3 | 1 |
| R-079 | FX-tab group pointers not nulled on tab switch | critical | C | T1.3 | 1 |
| R-064 | Arp → Off never drains note-offs | critical | C | T1.6 | 1 |
| R-054 | Delay and reverb silent after any engine reset | critical | C | T1.5 | 1 |
| R-029 | Arp mode change with held notes leaves stuck voices | critical | C | T1.6 | 1 |
| R-028 | Arp chord slide glides one voice N times | critical | C | T1.8 | 1 |
| R-027 | Poly→Mono promotes a released note | critical | C | T1.9 | 1 |
| R-175 | Harmonizer collapses the mix to mono | high | C | T1.19 | 1 |
| R-148 | SpectralFreeze oscillator out of tune | high | C | T1.16 | 1 |
| R-142 | Dice button trigger never reaches the processor | high | C | T1.21 | 1 |
| R-141 | Mono mode ignores pitch bend and tuning reference | high | C | T1.11 | 1 |
| R-140 | Free-run trance-gate rate overwritten with 4 Hz | high | C | T1.13 | 1 |
| R-135 | OSC Phase Mod / Freq Mod act on one sample per block | high | C | T1.14 | 1 |
| R-131 | Pointer nulling hits the INCOMING template | high | C | T1.3 | 1 |
| R-129 | F002 partial: group pointers (same as R-193) | high | C | T1.3 | 1 |
| R-124 | Browser preset load never sends voice routes | high | C | T1.22 | 1 |
| R-121 | Mono portamento time overwritten by arp slide time | high | C | T1.12 | 1 |
| R-119 | Poly/Mono and polyphony tests assert nothing | high | C | T1.9 | 1 |
| R-118 | No test for SpectralFreeze after reset | high | C | T1.15 | 1 |
| R-116 | Latency tests pin a 6144-sample late echo | high | C | T1.18 | 1 |
| R-115 | Test asserts the stuck note keeps sounding | high | C | T1.6 | 1 |
| R-113 | Slide steps leave stuck MIDI-out notes; goldens pin them | high | C | T1.8 | 1 |
| R-091 | Per-sample cutoff re-arms SelfOsc kick | high | C | T1.17 | 1 |
| R-082 | Removing a global route leaves curve/smooth/scale/bypass | high | C | T1.23 | 1 |
| R-070 | Arp reset without note-offs strands MIDI-out notes | high | C | T1.7 | 1 |
| R-069 | Skip events sent with `sendMessage` from `process()` | high | C | T1.32 | 1 |
| R-068 | Retrigger Beat resets every block without ppq | high | C | T1.29 | 1 |
| R-067 | Single-note gate > 100 % cut by stale NoteOff | high | C | T1.30 | 1 |
| R-066 | Per-block sync destroys swing | high | C | T1.31 | 1 |
| R-065 | Key releases while arp Off not forwarded to the core | high | C | T1.6 | 1 |
| R-057 | Spectral delay feedback crushed by unscaled `tanh` | high | C | T1.20 | 1 |
| R-056 | Reverb crossfade never completes (same as R-134) | high | C | T1.4 | 1 |
| R-055 | Non-spectral echoes 139 ms late | high | C | T1.18 | 1 |
| R-043 | Mod-matrix curve labels do not match the DSP enum | high | C | T1.25 | 1 |
| R-041 | LFO 1/2 Depth never applied | high | C | T1.26 | 1 |
| R-040 | Browser loads keep old voice routes (same as R-124) | high | C | T1.22 | 1 |
| R-039 | Voice dest "Spectral Tilt" cast to OscALevel | high | C | T1.24 | 1 |
| R-038 | Trance-gate rate 4 Hz (same as R-140) | high | C | T1.13 | 1 |
| R-031 | Portamento dead (same as R-121) | high | C | T1.12 | 1 |
| R-030 | No IMidiMapping: pitch bend, aftertouch, CC lost | high | C | T1.28 | 1 |
| R-018 | Velocity never reaches the VCA | high | C | T1.27 | 1 |
| R-017 | SpectralMorph delays signal vs VCA and replays old tail | high | C | T1.33 | 1 |
| R-016 | SpectralFreeze silent after reset | high | C | T1.15 | 1 |
| R-011 | Mono ignores bend/tuning (same as R-141) | high | C | T1.11 | 1 |
| R-010 | Pitch-bend smoother advanced once per block | high | C | T1.10 | 1 |
| R-008 | SelfOsc filter wiped every 64-sample sub-block | high | C | T1.17 | 1 |
| R-003 | Unclamped enum indices from state: OOB write/read | high | C | T1.2 | 1 |
| R-001 | 144 envelope curve tables rebuilt every block | high | C | T1.34 | 1 |
| R-194 | F002/F003 regression tests use the reverse order | medium | C | T1.3 | 1 |
| R-190 | `getTailSamples` not overridden | medium | C | T2.26 | 2 |
| R-189 | Process-context requirements = 0 | medium | C | T2.26 | 2 |
| R-183 | Chaos Depth never applied | medium | C | T1.26 | 1 |
| R-182 | Mixer Spectral Shift never reaches the engine | medium | C | T2.5 | 2 |
| R-181 | Mono Priority labels swapped | medium | C | T2.4 | 2 |
| R-177 | Pre-reverb limiter tied to harmonizer enable | medium | C | T1.19 | 1 |
| R-176 | Pitch Shift Mode change hard-resets shifters | medium | C | T2.30 | 2 |
| R-169 | Out-of-range OSC type latched, OOB in `prepare()` | medium | C | T1.2 | 1 |
| R-168 | Mono noteId never follows the sounding note | medium | C | T2.21 | 2 |
| R-152 | Filter type change clicks | medium | C | T2.24 | 2 |
| R-150 | Delays and Hall reverb depend on host buffer size | medium | C | T2.31 | 2 |
| R-146 | SC-004b goldens stale after the llround fix (gate red) | medium | C | T0.1 | 3 (run first) |
| R-143 | Route add/remove desyncs detail params (same as R-082) | medium | C | T1.23 | 1 |
| R-136 | Arp MIDI-out stranded on deactivate (same as R-070) | medium | C | T1.7 | 1 |
| R-130 | DistortionViewMode (same as R-080) | medium | C | T1.3 | 1 |
| R-123 | State-migration tests cover no real old layout | medium | C | T5.2 | 5 |
| R-120 | Pitch tests check only non-zero output | medium | C | T5.9 | 5 |
| R-117 | Preset-switch-safety assertions cannot fail | medium | C | T5.1 | 5 |
| R-094 | Delay time 5000 ms sent to types that clamp at 2000 | medium | C | T2.13 | 2 |
| R-093 | Voice LFO is an unconfigured 1 Hz sine | medium | C | T2.20 | 2 |
| R-092 | PM/FM one-shot API (same as R-135) | medium | C | T1.14 | 1 |
| R-088 | Arp Accent Velocity / Slide Time / Voicing have no control | medium | C | T2.29 | 2 |
| R-087 | Nine factory presets hidden from the browsers | medium | C | T2.29 | 2 |
| R-086 | UI-only view-switch params automatable; null callbacks | medium | C | T2.27 | 2 |
| R-085 | Only two of eight arp lanes get a playhead | medium | C | T2.28 | 2 |
| R-084 | Flanger/chorus never reach their formatters | medium | C | T2.3 | 2 |
| R-083 | Grid detail edits call `performEdit` only | medium | C | T1.23 | 1 |
| R-081 | Curve labels (same as R-043) | medium | C | T1.25 | 1 |
| R-071 | First arp note after idle waits out a frozen counter | medium | C | T2.32 | 2 |
| R-061 | Reverb type switch while frozen goes silent | medium | C | T2.10 | 2 |
| R-060 | Chorus multi-voice modulation wraps in amplitude | medium | C | T2.11 | 2 |
| R-059 | Mod-slot crossfade re-targeted mid-fade | medium | C | T2.10 | 2 |
| R-058 | Spectral FFT Size changes without re-prepare | medium | C | T2.12 | 2 |
| R-050 | Sidechain ignores bus activation | medium | C | T2.14 | 2 |
| R-049 | Arp Rate modulation in sync mode leaves the grid | medium | C | T2.15 | 2 |
| R-048 | LFO rate clamps at 20 Hz of a 50 Hz range | medium | C | T2.16 | 2 |
| R-047 | Master volume / global filter mod steps every 64 samples | medium | C | T2.17 | 2 |
| R-046 | Route source/dest change keeps old amount | medium | C | T2.18 | 2 |
| R-045 | Voice-route Curve and Smooth never applied | medium | C | T2.19 | 2 |
| R-044 | Voice-route resonance scale 1 Q vs 10 Q | medium | C | T2.19 | 2 |
| R-042 | Aftertouch and mod wheel never fed (same as R-030) | medium | C | T1.28 | 1 |
| R-035 | noteId note-off falls back to pitch | medium | C | T2.21 | 2 |
| R-033 | Lowering polyphony freezes excess voices | medium | C | T1.9 | 1 |
| R-032 | Mono→Poly loses the held note's note-off | medium | C | T1.9 | 1 |
| R-024 | Spectral distortion mix blends undelayed dry | medium | C | T2.8 | 2 |
| R-023 | Per-voice Morph ignored in SpectralMorph | medium | C | T2.6 | 2 |
| R-022 | Formant Vowel overridden by Morph | medium | C | T2.7 | 2 |
| R-021 | PM/FM (same as R-135) | medium | C | T1.14 | 1 |
| R-020 | SelfOsc oscillation dies under a fast sweep | medium | C | T1.17 | 1 |
| R-019 | Distortion types silent / −24 dB / +8.5 dB at drive 0 | medium | C | T2.9 | 2 |
| R-013 | Stepped curve returns 4/3 at full scale | medium | C | T1.25 | 1 |
| R-012 | Mono voice panned by slot-0 spread | medium | C | T2.23 | 2 |
| R-009 | ChaosWaveshaper Lorenz resets 36×/s at default speed | medium | C | T2.25 | 2 |
| R-002 | Skip events on audio thread (same as R-069) | medium | C | T1.32 | 1 |
| R-072 | Arp input ignores note sample offsets | medium | P | T2.32 | 2 |
| R-073 | MIDI-mode silent with transport stopped | medium | P | T2.32 | 2 |
| R-191 | AUv2 component version never stamped | low | C | T3.4 | 3 |
| R-188 | Global filter registered defaults off | low | C | T2.34 | 2 |
| R-187 | Spectral FFT Size registered default 512 vs 1024 | low | C | T2.12 | 2 |
| R-186 | Env Attack / Self-Osc Release defaults off | low | C | T2.34 | 2 |
| R-185 | Ladder Slope registered default 6 dB vs 24 dB | low | C | T2.34 | 2 |
| R-184 | Chaos Rate range vs DSP clamp, defaults disagree | low | C | T2.34 | 2 |
| R-180 | Harmonizer Voice Delay jumps, stale delay line | low | C | T2.30 | 2 |
| R-179 | Muted voices glide pitch to unison | low | C | T2.30 | 2 |
| R-178 | Muted/removed voices cut in one sample | low | C | T2.30 | 2 |
| R-174 | Trance-gate NaN floats from state | low | C | T2.1 | 2 |
| R-173 | Arp NaN floats / unclamped lane speeds | low | C | T2.1 | 2 |
| R-172 | Envelope Bezier NaN control points | low | C | T2.1 | 2 |
| R-171 | Reverb NaN poisons the FDN tank | low | C | T2.1 | 2 |
| R-170 | LFO/Macro NaN reaches the mod engine | low | C | T2.1 | 2 |
| R-167 | Velocity rounding to 0 stamps the wrong voice | low | C | T2.21 | 2 |
| R-166 | Tab_Sound: MonoPortaMode clipped | low | C | T3.3 | 3 |
| R-165 | Tab_Seq: ArpMode overlaps Octaves | low | C | T3.3 | 3 |
| R-164 | Tab_Fx: harmonizer knobs at y = −2 | low | C | T3.3 | 3 |
| R-163 | `uidesc-inventory --check` fails | low | C | T3.3 | 3 |
| R-162 | Release 0 ms cuts in ~3 samples | low | C | T3.2 | 3 |
| R-161 | Spectral delay RNG seeded from `this` | low | C | T3.1 | 3 |
| R-159 | Formant oscillator period jitter | low | C | T2.35 | 2 |
| R-158 | Additive oscillator −42 dBc floor | low | C | T2.35 | 2 |
| R-156 | Comb filter +22 dB at max resonance | low | C | T2.24 | 2 |
| R-153 | Switching to Spectral discards echoes during pre-warm | low | C | T1.18 | 1 |
| R-151 | Mod grid not invariant below 64-sample buffers | low | C | T2.31 | 2 |
| R-149 | Formant oscillator clamps at 2000 Hz | low | C | T2.35 | 2 |
| R-144 | Lane playheads polled only while host plays | low | C | T2.28 | 2 |
| R-139 | Stale processor header, dead members | low | C | T4.3 | 4 |
| R-137 | Tempo dropout: two tempos in one block | low | C | T2.26 | 2 |
| R-128 | Chord/Inversion lane tests copy the controller rule | low | C | T5.6 | 5 |
| R-127 | Arp CPU test tags, `[!mayfail]`, `/20` normalisation | low | C | T5.5 | 5 |
| R-126 | Sidechain path never tested | low | C | T5.4 | 5 |
| R-125 | Factory-preset audio test never releases the note | low | C | T5.3 | 5 |
| R-122 | Empty golden for Arp_Tilt_Cascade | low | C | T5.7 | 5 |
| R-112 | `dropdown_mappings.h` includes the whole engine | low | C | T4.3 | 4 |
| R-111 | Stale gain-compensation and index-range comments | low | C | T4.2 | 4 |
| R-110 | Dead processor members | low | C | T4.3 | 4 |
| R-109 | Voice-route notify clamps to the wrong constants | low | C | T1.24 | 1 |
| R-108 | God functions, triplicated view-sync rules | low | C | T4.5 | 4 |
| R-107 | NaN/Inf-safety tests cannot fail | low | C | T5.8 | 5 |
| R-105 | `process()` never writes `silenceFlags` | low | C | T2.26 | 2 |
| R-104 | Degree sign built from UTF-8 bytes | low | C | T2.3 | 2 |
| R-103 | Stale signal-flow documentation | low | C | T4.2 | 4 |
| R-102 | Trance-gate docs contradict the code | low | C | T4.2 | 4 |
| R-101 | Gain Compensation control does nothing; docs promise it | low | C | T4.1 | 4 |
| R-099 | Dead tables in `dropdown_mappings.h` | low | C | T4.4 | 4 |
| R-098 | Arp handle/register/format duplicated with Gradus | low | C | T4.4 | 4 |
| R-097 | S&H / Random rate reuse the LFO map, dead 27 % | low | C | T2.34 | 2 |
| R-095 | Mod-matrix indices cast unclamped (same as R-003) | low | C | T1.2 | 1 |
| R-090 | Gain Compensation toggle (same as R-101) | low | C | T4.1 | 4 |
| R-089 | ADSR displays poll processor atomics after terminate | low | C | T2.27 | 2 |
| R-075 | Pre-v6/v7 controller keeps old flanger/chorus | low | C | T2.2 | 2 |
| R-074 | Older states keep previous arp trailing fields | low | C | T2.2 | 2 |
| R-063 | Harmonizer disable during fade-in jumps alpha | low | C | T2.10 | 2 |
| R-052 | Arp Pitch source stale after leaving Mod mode | low | C | T2.31 | 2 |
| R-051 | Self-analysis reads a scrambled previous-output buffer | low | C | T2.31 | 2 |
| R-037 | Mono return-to-held-note ignores retrigger | low | C | T2.22 | 2 |
| R-036 | Every note expression splits a render segment | low | C | T2.21 | 2 |
| R-034 | Voice Steal Mode is a no-op | low | C | T2.22 | 2 |
| R-026 | Idle voices replay frozen filter state | low | C | T1.33 | 1 |
| R-025 | Per-voice Spectral Tilt overwritten before use | low | C | T1.24 | 1 |
| R-015 | TranceGate step length truncated | low | C | T3.5 | 3 |
| R-014 | SVF Drive cliff at 0 dB | low | C | T2.24 | 2 |
| R-007 | Voice routes published field by field | low | C | T4.7 | 4 |
| R-005 | Raw atomic addresses handed to the controller | low | C | T4.6 | 4 |
| R-004 | Engine reset clears megabytes in one callback | low | C | T2.33 | 2 |
| R-006 | Every timed event costs a full engine render call | low | P | T4.8 | 4 |
| R-053 | Scale ×2/×4 saturate | low | P | T2.19 | 2 |
| R-076 | Truncated state applied partially | low | P | T4.8 | 4 |
| R-096 | Processor/controller loaders drifted on old versions | low | P | T2.2 | 2 |
| R-100 | Global dest 6 mapped to the Depth knob | low | P | T4.4 | 4 |
| R-106 | Tempo-derived step lengths truncated; lint blind | low | P | T3.5 | 3 |
| R-132 | Dead v1.5 arp IDs and atomics | low | P | T4.4 | 4 |
| R-133 | No test for the SVF 12→24 dB switch | low | P | T5.9 | 5 |
| R-138 | Parameter automation is block-rate | low | P | T4.8 | 4 |
| R-145 | Lane paste across types; modifier encoding | low | P | T4.8 | 4 |
| R-154 | Tape delay hiss with no input | low | P | T3.6a | 3 |
| R-160 | Spectral delay synced spacing drifts | low | P | T3.6b | 3 |
| R-192 | Mono sidechain arrangement rejected | low | P | T2.26 | 2 |

Total estimate: about 10 770 agent-minutes (180 h): T0.1 90, Phase 1 4 125, Phase 2 4 005, Phase 3 450, Phase 4 1 110, Phase 5 540, Phase 6 450. (Was 10 530 before the 2026-11-16 rulings rewrote T3.6 and T4.8: +90 and +150.)

---

## Phase 0 — hard prerequisite (belongs to Phase 3 by category, runs first)

### T0.1 — Re-record the stale SC-004b arp goldens so the gate is green (R-146)

**Why first:** the gate filter in step 4 does not exclude `[long]`, and `SC-004b: Ruinae factory arp presets produce byte-identical MIDI after kNumLanes 9->10` (`ruinae/tests/unit/ruinae_byte_identical_post_lane10_test.cpp:519-533`) fails 30 `REQUIRE(comparison.ok)` at line 437 on HEAD. Every later gate would be red.
**Flags:** PRESET-IMPACT (30 of 46 files `ruinae/tests/fixtures/ruinae_factory_*_golden_midi.txt`). **Estimate:** 90 min. **Prerequisites:** none.

1. **Failing test:** already exists and fails. Run it once, by name, and save the output: `build/windows-x64-release/bin/Release/ruinae_tests.exe "SC-004b: Ruinae factory arp presets produce byte-identical MIDI after kNumLanes 9->10" > f:/tmp/ruinae-audit/sc004b-before.log`.
2. **Fix (fixtures only, no production code):**
   - Write `tools/diff-midi-golden.js` (Node, not Python): given a captured dump and a golden, print every event that exists on one side only, and classify each removed golden event pair as "doubled step" when a `noteOn`/`noteOff` (or `noteOn`/`noteOn`) pair sits one sample apart (the signature from commit `8be7406d`: 165375/165376), or "spurious early step" when an onset sits exactly one step length before a grid onset (e.g. 170888 = 176400 − 5512).
   - Regenerate into a scratch directory, not over the fixtures: run the case with `RUINAE_REGEN_GOLDENS=1` after temporarily pointing the regen path (test lines 202-217, 417-422) at `f:/tmp/ruinae-audit/regen/`; revert that temporary edit before committing.
   - Run the diff tool for all 30 presets. Accept a preset only when every delta is classified. Presets with larger deltas (Arp_Chaos_Matrix 1056 vs 994, Ratchet_Fury +4, Sync_Ratchet +4, Swing_Vox +4) must be explained event by event (a ratchet no longer cut short by a spurious step produces the extra events). If any delta stays unexplained, stop and report; do not re-record that preset.
   - Copy the accepted files over `ruinae/tests/fixtures/`. Put the per-preset classification table in the commit message.
   - **Before:** 30 failing assertions. **After:** SC-004b passes; the five slide goldens are still unbalanced (that is R-113, fixed and re-recorded in T1.8).
3. **Build:** `ruinae_tests`. 4. **Gate:** `ruinae_tests`; expect `GATE GREEN`. 5. pluginval not needed (test-only); clang-tidy not needed (no C++ change kept). 6. Commit.

**Acceptance:** `GATE GREEN` with the standard filter; the commit message lists, per preset, the count of removed doubled steps and spurious steps.

---
## Phase 1 — critical and high: crashes, hangs, stuck notes, wrong audio

Order inside the phase: state-load safety (T1.1–T1.2), editor crashes (T1.3), effects that break (T1.4–T1.5), stuck notes (T1.6–T1.9), pitch (T1.10–T1.12), then the remaining high-severity audio and wiring defects. Each task's line "Steps 3–6" names what to build and gate; the commands are the ones in the workflow section.

### T1.1 — Clamp oscillator floats on state load and make `wrapPhase` O(1) (R-147)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/core/phase_utils.h` is included by every oscillator; all plugins), macOS-SENSITIVE (do not rely on `std::floor` of a non-finite). **Estimate:** 75 min. **Prerequisites:** T0.1.

1. **Failing tests:**
   - `dsp/tests/unit/core/phase_utils_test.cpp` (extend; create if missing and register in `dsp/tests/CMakeLists.txt`), target `dsp_core_tests`: `TEST_CASE("wrapPhase returns in bounded time for huge finite input", "[core][phase]")`. Assert `wrapPhase(1.6e29)`, `wrapPhase(-1.6e29)`, `wrapPhase(1e10)` each return a value in `[0, 1)`, and that `wrapPhase(2.25) == Approx(0.25)`, `wrapPhase(-0.25) == Approx(0.75)`. On current code the first call never returns: run this case by name with a 10 s external timeout (`timeout 10 <exe> "<name>"`) and treat the timeout as the expected failure.
   - `ruinae/tests/unit/state_migration_test.cpp`: `TEST_CASE("setState clamps out-of-range oscillator floats", "[state][robustness]")`. Build a valid v8 blob with `getState`, overwrite OSC B PhaseMod (the float written by `saveOscParams` for `phaseMod`; locate its offset by saving twice with two different param-212 values and diffing) with `1e30f`, `setState`, drain with `drain_preset_transfer.h`, hold C3, process 10 blocks. Assert the call returns (the test itself hanging is the failure), `getState` now holds a PhaseMod in `[-1, 1]`, and every output sample is finite and `|x| <= 1`.
2. **Fix:**
   - `dsp/core/phase_utils.h:74-82` `wrapPhase`: replace the two `while` loops with `phase -= std::floor(phase);` followed by `if (!(phase >= 0.0 && phase < 1.0)) phase = 0.0;` (the second line maps NaN, Inf and the `1.0 - ulp` rounding case to 0). Keep the signature.
   - `ruinae/src/parameters/osc_params.h` `loadOscParams` (around 731-802): clamp every field to the range `handleOscParamChange` produces (same file, 219-266): `phaseMod` and `freqMod` to `[-1, 1]` (lines 751-754), and each remaining float/int to its handler range. Use one local helper `finiteClamp(v, lo, hi, def)` that returns `def` when `!detail::isFinite(v)`; T2.1 reuses it, so put it in a new header `ruinae/src/parameters/state_load_helpers.h`.
   - **Before:** a state with PhaseMod 1e30 hangs the audio thread forever. **After:** the value loads as 1.0 and `wrapPhase` costs O(1) for any input.
3–6. **Build:** `ruinae_tests Ruinae dsp_core_tests` plus the executables `header-consumers.js` names. **Gate:** those executables. pluginval: yes. clang-tidy: `ruinae` and `dsp`. Run `tools/macos-fastmath-repro.sh`. Commit.

**Acceptance:** both new cases pass; probe `state-garbage:2` (`AuditProbe state truncated and garbage`, file `f:/tmp/ruinae-audit/garbage2.bin`) returns within one block.

### T1.2 — Clamp every enum and index loaded from state; make the DSP reject invalid indices (R-003, R-169, R-095)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/systems/modulation_engine.h`: Disrumpo, Vorago, Seraphis, Profundum, Ruinae; `dsp/systems/selectable_oscillator.h`, `dsp/processors/chaos_oscillator.h`, `dsp/processors/formant_oscillator.h`: confirm with grep). **Estimate:** 120 min. **Prerequisites:** T1.1 (helper header).

1. **Failing tests:**
   - `dsp/tests/unit/systems/selectable_oscillator_test.cpp`, `dsp_systems_tests`: `TEST_CASE("SelectableOscillator ignores an out-of-range type", "[systems][oscillator]")`: `prepare`, `setType(static_cast<OscType>(12))`, assert `getActiveType()` is unchanged, call `prepare` again, `processBlock` 512 samples, assert finite.
   - `dsp/tests/unit/systems/modulation_engine_test.cpp`: `TEST_CASE("ModulationEngine drops a routing with an invalid source", "[systems][modulation]")`: `setRouting(0, {source = static_cast<ModSource>(200), active = true, amount = 1})`, `process` one block, assert the routing reads back inactive and every `getModulationOffset` is 0.
   - `ruinae/tests/unit/state_migration_test.cpp`: `TEST_CASE("setState clamps out-of-range enum indices", "[state][robustness]")`: v8 blob with mod slot 0 source = 200 and dest = −5, OSC A type = 200, chaosAttractor = 99, formantVowel = 99, voice route 0 source = 200 / destination = 200; `setState`, drain, process 10 blocks with a held note, then call `setupProcessing` at 96 kHz and process again. Assert no crash, finite output, and that `getState` round-trips in-range values. This case must also be run once under the ASan build (see T1.3 step 1 for the ASan build command); the unfixed code reports an object overflow in `updateActiveSourceFlags`.
2. **Fix:**
   - `ruinae/src/parameters/mod_matrix_params.h:322-325` `loadModMatrixParams`: clamp `source` to `[0, kModSourceCount-1]`, `dest` to `[0, kModDestCount-1]` (constants from `dropdown_mappings.h:180,183`, the same ones the handler uses at 76-83); clamp curve to 0–3, scale to 0–4, bypass to 0/1.
   - `osc_params.h` `loadOscParams`: `type` to `[0, kOscTypeCount-1]` (731-732), `chaosAttractor` (779-780) and `formantVowel` (801-802) to their handler ranges, and every other int field likewise.
   - `ruinae/src/processor/processor_state.cpp:190-211` voice-route load: clamp `source` to `[0, VoiceModSource::NumSources-1]`, `destination` to `[0, kNumVoiceDestinations-1]` (the UI index space; T1.24 adds the translation), curve 0–3, scale 0–4. Also clamp `reverbType` (175-177) to 0–1 and `modType` (227) to 0–3.
   - `ruinae/src/processor/processor_params.cpp:483-486`: clamp `srcInt` and `dstInt` again at the cast site (defence in depth).
   - `dsp/systems/selectable_oscillator.h:132-147` `setType`: move the bounds check `idx >= kNumOscTypes || !slots_[idx]` above `activeType_ = type;` (line 138) and return without changing any state when it fails. Line 105 in `prepare`: if `activeType_` is out of range, fall back to `OscType::PolyBLEP`.
   - `dsp/systems/modulation_engine.h:355-374` `setRouting`: if `static_cast<size_t>(routing.source) >= kModSourceCount`, store the routing with `source = ModSource::None` and `active = false`. Line 666-671 `updateActiveSourceFlags`: skip any `r.source` index `>= kModSourceCount`.
   - `ChaosOscillator::setAttractor` (`chaos_oscillator.h:294-298`) and `FormantOscillator::setVowel` (`formant_oscillator.h:292-296`): clamp the enum to the table size before indexing.
   - **Before:** OOB write into `sourceActive_` every block, OOB table reads, garbage-pointer virtual call after a re-prepare. **After:** every index is in range at load, at the cast and inside the DSP.
3–6. **Build:** `ruinae_tests Ruinae dsp_systems_tests dsp_processors_tests` plus consumers. **Gate:** those. pluginval: yes. clang-tidy: `ruinae`, `dsp`. Commit.

**Acceptance:** three new cases pass; the ASan run of the Ruinae case is clean.

### T1.3 — Controller cached view pointers follow view lifetime (R-193, R-129, R-080, R-130, R-079, R-131, R-194)

**Flags:** none (plugin controller only). **Estimate:** 240 min. **Prerequisites:** T0.1.

**Root cause in one paragraph.** `verifyView` (`ruinae/src/controller/controller_verify_view.cpp`) caches raw `CView*` members. They are cleared by hand in `willClose` (`controller.cpp:664`), `onTabChanged` (`controller_settings.cpp:14-81`), `resetModSourceViewPointers` (`controller.cpp:650-662`) and two tag hooks in `setParamNormalized` (`controller_view_sync.cpp:64-84`). Three destruction paths are missed (the ten LFO/Chaos/S&H/Random rate and note-value groups on a ModSourceViewMode switch; `spectralCurveDropdown_`/`spectralBitsGroup_` on a DistortionViewMode switch; eight FX-tab groups on a MainTab switch). Worse, the hooks run AFTER `EditControllerEx1::setParamNormalized` (line 57), inside which the switch container has already built the incoming template and `verifyView` has cached its pointers, so on a host-driven change the hooks null the NEW views.

1. **Failing tests** (move `specs/ruinae-audit/probes/view_switch_lifecycle_probe.cpp.txt` into `ruinae/tests/unit/controller/view_switch_lifecycle_test.cpp`, register it in `ruinae/tests/CMakeLists.txt` next to line 101, un-hide the tags, keep its `DeleteTracker`/pointer-inventory helper):
   - `TEST_CASE("ModSource template destroyed, then Sync automation does not touch freed views", "[controller][lifecycle]")` with one SECTION per source (LFO1, LFO2, Chaos, S&H, Random): open the editor with `editor_lifecycle_harness.h`, show Tab_Mod, drive `kModSourceViewModeTag` through the bound control (control `setValueNormalized` + `valueChanged`, the real order), then `setParamNormalized(k<X>SyncId, 1.0)`. Assert every cached pointer of the destroyed template is null (expose `Controller::cachedViewPointerInventoryForTest()` returning name/pointer pairs for ALL cached view members), and every cached pointer of the visible template is non-null and present in the live frame tree.
   - `TEST_CASE("ModSource LFO1 template destroyed, then setComponentState", "[controller][lifecycle]")`: same switch, then `setComponentState` with a default processor state; assert no pointer is "non-null and not live".
   - `TEST_CASE("DistortionViewMode switch clears the spectral pointers", "[controller][lifecycle]")`: Tab_Sound, distortion type Spectral, `kDistortionViewModeTag` 1 → 0, assert `spectralCurveDropdown_` and `spectralBitsGroup_` null; then `setParamNormalized(kDistortionSpectralModeId, 1.0)`.
   - `TEST_CASE("Leaving the FX tab clears every FX-tab group pointer", "[controller][lifecycle]")`: show Tab_Fx, switch MainTab to 0 through the control, assert the eight pointers (`noModulationGroup_`, `phaserControlsGroup_`, `flangerControlsGroup_`, `chorusControlsGroup_`, `flangerRateGroup_`, `flangerNoteValueGroup_`, `chorusRateGroup_`, `chorusNoteValueGroup_`) null; then `setComponentState`.
   - `TEST_CASE("Host-driven MainTab change keeps the incoming tab's pointers", "[controller][lifecycle]")`: with the editor attached and the control NOT editing, `controller.setParamNormalized(kMainTabTag, <SEQ>)`; assert `stepPatternEditor_` and all eight lane pointers non-null, then that a `setParamNormalized` on a lane step parameter reaches the lane view.
   - Run these under ASan as well. One-time ASan build: `"C:/Program Files/CMake/bin/cmake.exe" -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON`, then `--build build-asan --config Debug --target ruinae_tests -- -m:2`, then `node tools/run-gate.js --share 4 --filter '[lifecycle]' build-asan/bin/Debug/ruinae_tests.exe` (pass the path form `run-gate.js` accepts; check its usage text). Baseline: 6 heap-use-after-free reports (`specs/ruinae-audit/probes/view_switch_lifecycle_results.txt`).
   - Rewrite the two existing cases in `ruinae/tests/unit/controller/mod_source_view_teardown_test.cpp` (131-167 and 169-197): drive the switch through the control in the real order, replace `modSourceViewPointerCount() <= 3` with the live/freed inventory check, and delete `notifyTabChangedForTest` use.
2. **Fix:**
   - Add `ruinae/src/controller/cached_view_registry.h`: a small class implementing `VSTGUI::IViewListener` (via `ViewListenerAdapter`) with `void track(VSTGUI::CView* view, VSTGUI::CView** slot)`: stores the pair, calls `view->registerViewListener(this)`; `viewWillDelete(CView* v)`: for every tracked pair whose view is `v`, set `*slot = nullptr`, unregister, erase; `void clear()`: unregister from all and null all slots (used by `willClose` and `terminate`). Fixed-capacity `std::array` (128 entries) so nothing allocates after construction.
   - In `controller_verify_view.cpp`, every assignment of a cached view member (`lfo1RateGroup_ = container;` at 872-921, `spectralCurveDropdown_ = view;` at 645-646, `spectralBitsGroup_ = container;` at 1036-1037, the FX groups at 942-981, the lanes at 141-143 / 361, `stepPatternEditor_` at 83-85, `xyMorphPad_` at 574, the displays at 707 onward, the grid in `controller_mod_matrix.cpp:51`, ring indicators, ADSR displays, harmonizer rows, delay/phaser groups, mono group, etc.) goes through one helper `cacheView(member, view)` that assigns and calls `viewRegistry_.track(view, reinterpret-free typed slot)`. For typed members (`ArpLaneEditor*` etc.) give `track` a template overload that stores a small lambda-free "null this slot" function pointer + `void*`.
   - Remove the manual nulling: `controller_view_sync.cpp:64-84` keeps only what is not pointer clearing (if `onTabChanged` does non-pointer work such as closing the settings drawer, keep that part and call it; delete lines 68-84's pointer resets); `controller_settings.cpp:14-81` loses every `= nullptr` / `.fill(nullptr)` line; `resetModSourceViewPointers` and `modSourceViewPointerCount` are deleted (their test uses are replaced above); `willClose` (`controller.cpp:664-739`) calls `viewRegistry_.clear()` instead of the hand list, keeping the timer resets at 738-739.
   - **Before:** UAF on mod-source/distortion-view/tab switches followed by automation or a preset load; after any host-driven tab change the new tab's displays stop updating. **After:** a cached pointer is non-null exactly while its view is alive, on every path.
3–6. **Build:** `ruinae_tests Ruinae`. **Gate:** `ruinae_tests`, then the ASan `[lifecycle]` run. pluginval: yes. clang-tidy: `ruinae`. Commit.

**Acceptance:** all five cases pass in Release; the ASan run reports 0 errors (baseline 6); the inventory check never finds a pointer that is non-null and not live.

### T1.4 — Reverb type crossfade starts once and can reverse (R-134, R-056)

**Flags:** none. **Estimate:** 60 min. **Prerequisites:** T0.1.

1. **Failing test:** `ruinae/tests/unit/processor/reverb_type_test.cpp`: `TEST_CASE("Reverb type switch completes at small block sizes", "[processor][reverb][fx]")`, SECTIONs for block 64, 256, 512 at 44.1 kHz: enable reverb, hold a 220 Hz note, send `kReverbTypeId` = Hall once, process 100 ms. Assert (a) the chain reports active type 1 and not crossfading (add `RuinaeEngine::isReverbCrossfadingForTest()` / `activeReverbTypeForTest()` forwarding to the chain if no accessor exists); (b) over the last second of a 3 s render the output matches a reference processor that loaded reverb type 1 via `setState` within relative RMS 0.01 (baseline 0.4083 at block 512). Second case `TEST_CASE("Reverb type toggle back mid-fade keeps the active tail", "[processor][reverb][fx]")`: build a Plate tail, request Hall, one block later request Plate, render 0.5 s; assert the tail RMS is at least 0.5× a no-toggle reference and `artifact_detection.h` finds no click.
2. **Fix:** `ruinae/src/engine/ruinae_effects_chain.h:689-701` `setReverbType`: clamp `type` to 0–1; add `if (reverbCrossfading_ && type == incomingReverbType_) return;` (mirrors `setDelayType` line 399); add `if (reverbCrossfading_ && type == activeReverbType_) { std::swap(activeReverbType_, incomingReverbType_); reverbCrossfadeAlpha_ = 1.0f - reverbCrossfadeAlpha_; return; }` so a return to the active type reverses the fade instead of crossfading an instance with itself. Check `processReverbSlot` (1118-1155) and `completeReverbCrossfade` (1158-1171) still reset only the reverb that ends up inactive after the swap.
   - **Before:** at any buffer under 1323 samples the fade restarts every block: block-rate buzz, both reverbs run forever. **After:** one 30 ms fade per request.
3–6. **Build:** `ruinae_tests Ruinae`. **Gate:** `ruinae_tests`. pluginval: yes. clang-tidy: `ruinae`. Commit.

**Acceptance:** both cases pass at all three block sizes.

### T1.5 — Delay and reverb survive an engine reset (R-054)

**Flags:** none. **Estimate:** 60 min. **Prerequisites:** T0.1.

1. **Failing tests:** `ruinae/tests/unit/ruinae_effects_chain_test.cpp`: `TEST_CASE("RuinaeEffectsChain: enabled delay and reverb still process after reset", "[ruinae-fx][reset]")`: `setDelayEnabled(true)`, `setReverbEnabled(true)`, process, `reset()`, `setDelayEnabled(true)`, `setReverbEnabled(true)` (what the processor does every block), feed an impulse with delay mix 1; assert wet peak at the echo time > 0.1 (baseline: 0.928 before reset, 0.000000 after) and reverb RMS > 0.001. `ruinae/tests/integration/processor_audio_test.cpp`: `TEST_CASE("Delay and reverb survive setActive cycle and a delay-on preset load", "[integration][fx][lifecycle]")`: enable both, process, `setActive(false)`, `setActive(true)`, play a short note, assert echo energy after the dry note ends; second SECTION: `setState` of a delay-on state over a delay-on processor, same assertion.
2. **Fix:** `ruinae_effects_chain.h:243-247` in `reset()`: replace the unconditional `Off` with `delayFadeState_ = delayEnabled_ ? SlotFadeState::On : SlotFadeState::Off; delayFadeAlpha_ = delayEnabled_ ? 1.0f : 0.0f;` and the same for reverb. (Do not clear `delayEnabled_`: the harmonizer path at 233 clears its flag because it needs a fade-in prime; delay and reverb do not.)
   - **Before:** after `setActive(true)` or a host preset load with the effect already on, the slot is skipped forever (`:840`, `:1077`). **After:** the fade state is rebuilt from the enable flags.
3–6. **Build:** `ruinae_tests Ruinae`. **Gate:** `ruinae_tests`. pluginval: yes. clang-tidy: `ruinae`. Commit.

**Acceptance:** both cases pass; fix the existing reset test (`ruinae_effects_chain_test.cpp:1439-1443`) so it no longer disables the effects right after `reset()`.

### T1.6 — Arp operating-mode changes never strand a note (R-064, R-029, R-114, R-115, R-065)

**Flags:** PRESET-IMPACT: none expected (fixtures do not change mode mid-run); confirm SC-004b stays green. **Estimate:** 210 min. **Prerequisites:** T0.1.

**Design.** Three rules, all in the processor (the arp core is not edited):
(a) *A note-off goes wherever its note-on went.* Add `std::bitset<128> engineDirectKeys_` to `Processor` (`ruinae/src/processor/processor.h`). `onNoteOn` (both overloads, `processor_midi.cpp:158-177`, `201-217`) sets the bit when it queues the direct engine NoteOn (the `!arpDispatchesNotes` branch at 172-176). `onNoteOff` (180-194, 220-235): ALWAYS call `arpCore_.noteOff(midiPitch)` (the core ignores pitches it does not hold); queue the engine NoteOff when `engineDirectKeys_[pitch]` is set (then clear it) regardless of the current mode.
(b) *The processor keeps a ledger of what the arp turned on.* Add `std::array<uint8_t,128> arpEngineOpen_{}` and `arpMidiOutOpen_{}` (open-note counts). Increment where `ArpNoteOn` is queued (`processor.cpp:342-346`) and where the MIDI-out NoteOn is added (347-358); decrement where `ArpNoteOff` is queued (362-365) and where the MIDI-out NoteOff is added (366-377). A NoteOff for a pitch whose count is 0 is dropped on that sink.
(c) *Leaving a dispatching mode flushes the ledger.* Track `prevArpOpMode_`. In `process()` at the arp block (`processor.cpp:289-293`): when the mode changed this block and the previous mode dispatched notes (MIDI or MIDI+Mod) and the new one does not (Off or Mod), emit at offset 0 one `ArpNoteOff` per open pitch in `arpEngineOpen_` and one MIDI-out NoteOff per open pitch in `arpMidiOutOpen_`, then zero both ledgers. When the new mode is Off, also call `arpCore_.processBlock(arpCtx, arpEvents_)` once and discard its events (this drains `needsDisableNoteOff_` so the core's own state is clean), and clear the core's held notes by calling `arpCore_.noteOff(p)` for every pitch whose key is no longer physically down. To know that, add `std::bitset<128> physicalKeys_` maintained in `onNoteOn`/`onNoteOff`. Fix the false comment at 294-296.
Add one helper `flushArpLedger(Steinberg::Vst::IEventList* out, int32 offset)`; T1.7 and T1.8 reuse it and the ledgers.

1. **Failing tests** (`ruinae/tests/unit/processor/arp_integration_test.cpp`, tags `[arp][integration][stuck-notes]`; capture `outputEvents` with MIDI Out on and count engine voices with a test accessor `Processor::engineActiveVoiceCountForTest()`):
   - Rewrite `ArpIntegration_DisableWhilePlaying_NoStuckNotes` (668-721): set `kArpGateLengthId` to 1.0 (200 %), octave range 2, disable two blocks after a step fires. REQUIRE per-pitch NoteOn count == NoteOff count on the output bus, engine voice count 0 within release time + 10 blocks, and 10 consecutive silent blocks.
   - Rewrite `ArpIntegration_EnableWithExistingHeldNote_NoStuckNotes` (803-870): after the key release REQUIRE silence within release time + 10 blocks (delete `CHECK(audioAfterNoteOff)` and the "expected behavior" comments).
   - New `ArpIntegration_MidiToMod_ReleasesArpVoices`: MIDI mode, amp sustain 1.0, hold a chord, switch to Mod mid-gate, release keys; REQUIRE silence 1 s later and a balanced output bus.
   - New `ArpIntegration_ReleaseWhileOff_NoPhantomPattern`: MIDI mode, hold 60/64/67, set Off, release all, set MIDI, process 2 s; REQUIRE zero NoteOns on the output bus and silence.
   - New `ArpIntegration_LatchedDisable_NoStuckNotes`: latch Hold, keys already released, switch Off mid-gate; same balance and silence checks.
2. **Fix:** as in the design. **Before:** probe `arp:disable-mid-note` = 1 unmatched MIDI-out note-on / 1 engine voice after 1.5 s. **After:** 0 / 0.
3–6. **Build:** `ruinae_tests Ruinae`. **Gate:** `ruinae_tests`. pluginval: yes. clang-tidy: `ruinae`. Commit.

**Acceptance:** five cases pass; probe `AuditProbe arp stop disable release loop` reads 0 / 0 for the disable case.

### T1.7 — Arp reset paths send note-offs for notes already on the MIDI output (R-070, R-136)

**Flags:** none (uses existing core API `requestPanicNoteOff` `arpeggiator_core.h:675-678` and `prepareRetainingPanicNoteOff` `:261-286`; no core edit). **Estimate:** 90 min. **Prerequisites:** T1.6 (ledger).

1. **Failing test:** `arp_integration_test.cpp`: `TEST_CASE("ArpMidiOut_ResetPaths_BalanceNoteOffs", "[arp][integration][stuck-notes]")`, MIDI mode, MIDI Out on, run until a NoteOn is on the output bus mid-gate, then three SECTIONs: (a) `setState` of another arp preset, process one block; (b) `setActive(false)`, `setupProcessing`, `setActive(true)`, process one block; (c) `setActive(false)`, `setActive(true)`, process one block. REQUIRE the output bus of that next block contains a NoteOff for every open pitch.
2. **Fix:** model on `plugins/gradus/src/processor/processor.cpp:104-149`.
   - `processor.cpp:203-219` `setActive`: on `state == false` set `pendingDeactivateFlush_ = true` (new member). On `state == true`: keep `engine_.reset()`; replace `arpCore_.reset()` (207) with: if `pendingDeactivateFlush_` leave the MIDI-out ledger intact so the first `process()` flushes it, else reset as today. Always zero `arpEngineOpen_` (the engine was reset).
   - First `process()` after activation: if `pendingDeactivateFlush_`, call `flushArpLedger(data.outputEvents, 0)` for the MIDI-out ledger only, then `arpCore_.reset()`, clear the flag.
   - `processor_state.cpp:363-365` `applyPresetSnapshot` runs on the audio thread but has no `ProcessData`: set `pendingPresetFlush_ = true` there and keep both resets; in `process()` right after the `accessTransferObject_rt` drain (`processor.cpp:235-237`) flush the MIDI-out ledger when the flag is set. Zero `arpEngineOpen_` in the snapshot path.
   - `setupProcessing` (`processor.cpp:185`) keeps `arpCore_.prepare`; the ledger is processor state and survives it.
3–6. **Build:** `ruinae_tests Ruinae`. **Gate:** `ruinae_tests`. pluginval: yes. clang-tidy: `ruinae`. Commit.

**Acceptance:** all three SECTIONs find the balancing NoteOffs; fix the stale comment at `arp_integration_test.cpp:727`.

### T1.8 — Slide steps: every old note ends, every new chord note sounds (R-113, R-028)

**Flags:** PRESET-IMPACT (regenerate `ruinae_factory_{Acid_Gate,Acid_Line_303,Arp_Filter_Sweep,Arp_Self_Modulator,Ratio_Stutter}_golden_midi.txt`; justification = the diff tool from T0.1 shows only added NoteOffs, each at the onset of a legato NoteOn, plus the end-of-run balance; any other delta is a stop). **Estimate:** 180 min. **Prerequisites:** T1.6 (ledger), T0.1 (diff tool).

1. **Failing tests:**
   - `arp_integration_test.cpp`: `TEST_CASE("ArpMidiOut_SlideStep_EveryNoteOnHasNoteOff", "[arp][integration][stuck-notes]")`: modifier lane {Active, Active|Slide}, MIDI Out on, 4 bars, release keys, 1 more bar; REQUIRE per-pitch on == off and the last event per pitch is a NoteOff.
   - `ruinae/tests/unit/ruinae_engine_test.cpp`: `TEST_CASE("RuinaeEngine: legato chord glides N voices", "[ruinae-engine][legato]")`: poly, `noteOn(60)`, `noteOn(64)`, `noteOn(67)`, render; then `noteOn(62, v, true)`, `noteOn(65, v, true)`, `noteOn(69, v, true)` with no `processBlock` between; render; REQUIRE three gated voices whose allocator notes are {62, 65, 69}; then `noteOff` of 62/65/69 and render 3 s; REQUIRE silence (RMS < 1e-4) and active voice count 0.
   - `arp_integration_test.cpp`: `TEST_CASE("ArpIntegration_ChordSlide_NoStuckVoices", "[arp][integration][stuck-notes]")`: Poly, chord lane 3 notes with a changing root, Slide on step 2, run two steps, release keys (latch off), render 3 s; REQUIRE silence.
   - In `ruinae_byte_identical_post_lane10_test.cpp` `verifyPreset` (362-438): add an invariant that runs before the golden compare AND in regen mode: for every pitch, NoteOn count == NoteOff count, and after the 45 s key release the last event is a NoteOff. It fails today for the five presets.
2. **Fix:**
   - Engine, `ruinae/src/engine/ruinae_engine.h:1760-1780` `dispatchPolyLegatoNoteOn`: add `uint32_t legatoSerial_` (incremented at the top of `processBlock`, line 783) and `std::array<uint32_t, kMaxPolyphony> legatoStamp_{}`. Choose the most recent voice that is allocator-`Active` AND has `legatoStamp_[i] != legatoSerial_`; glide it, `setVoiceNote`, stamp it. If no candidate exists, call `dispatchPolyNoteOn(note, velocity)` (a fresh voice).
   - Processor, arp event loop (`processor.cpp:334-378`): detect a slide batch = consecutive `NoteOn` events with `legato == true` and the same `sampleOffset`. Process the whole batch (queue `ArpNoteOn` with legato; add MIDI-out NoteOns), then for every pitch open in `arpEngineOpen_` that is not in the batch queue an `ArpNoteOff` at the same offset, and for every pitch open in `arpMidiOutOpen_` not in the batch add a MIDI-out NoteOff at the same offset (after the NoteOns, so the receiver sees an overlap). Rule (b) of T1.6 drops the core's later duplicate NoteOffs.
   - Regenerate the five goldens with the T0.1 procedure and diff tool.
   - **Before:** old chord voices drone, only the last new note sounds, MIDI-out pitches hang (e.g. pitch 83 from 9.9 s in Arp_Filter_Sweep). **After:** N-to-N glide, old notes end at the slide onset.
3–6. **Build:** `ruinae_tests Ruinae`. **Gate:** `ruinae_tests`. pluginval: yes. clang-tidy: `ruinae`. Commit (message includes the five golden diffs' summary).

**Acceptance:** all cases pass; a scan of all 46 fixtures finds 0 unbalanced files (baseline 5).

### T1.9 — Voice-mode switches and polyphony changes never strand or freeze a voice (R-027, R-032, R-033, R-119)

**Flags:** CROSS-PLUGIN IMPACT only if `VoiceAllocator` gets a new method (`dsp/systems/voice_allocator.h`: grep consumers; additive). **Estimate:** 180 min. **Prerequisites:** T0.1.

1. **Failing tests** (`ruinae/tests/unit/ruinae_engine_test.cpp`, tags `[ruinae-engine][voice-mode][stuck-notes]`; replace the assertion-free cases at 218-232 and 523-555):
   - `RuinaeEngine_PolyToMono_ReleasedNotesNotPromoted`: `noteOn(60)`, `noteOn(64)`, `noteOff(64)`, `noteOff(60)`, `setMode(Mono)`, render 2 s; REQUIRE RMS < 1e-4 after the release time. SECTION 2: hold 60, play and release 64, switch, release 60; REQUIRE silence.
   - `RuinaeEngine_MonoToPoly_HeldKeyReleases`: Mono, `noteOn(60)`, render, `setMode(Poly)`, `noteOff(60)`, render 2 s; REQUIRE output RMS < 1e-5 (do not use `getActiveVoiceCount`, which reads the reset allocator).
   - `RuinaeEngine_PolyphonyReduction_ExcessVoicesReleaseSmoothly`: 8 held notes, `setPolyphony(2)`, render; REQUIRE no click at the change (`artifact_detection.h`, jump ratio ≤ 1.5) and silence after release of all keys + release time; then `setPolyphony(8)` with no new notes and render 100 ms; REQUIRE RMS < 1e-5.
2. **Fix (`ruinae_engine.h`):**
   - `switchPolyToMono` (1833-1889): select survivors only among slots with `allocator_.getVoiceState(i) == VoiceState::Active` (key down). Push every such slot's note into `monoHandler_` in timestamp order (oldest first) so held keys survive. If none is Active: do not call `voices_[0].noteOn`, do not push anything; let every voice finish its release. Non-surviving voices: call `noteOff()` and keep rendering them (next bullet).
   - Render every active voice regardless of mode and polyphony: in `processBlockPoly` change the three loops bounded by `polyphonyCount_` (1940, 2006, 2032) to `kMaxPolyphony` for rendering and `voiceFinished`, keeping allocation limited by the allocator's voice count; in `processBlockMono` (2043-2131) also render voices 1..15 that are still active (releasing tails) with their own pan, without the mono frequency update.
   - `switchMonoToPoly` (1891-1897): if `voices_[0]` is gated and `monoVoiceNote_ >= 0`, register it with the allocator so a later `noteOff(note)` releases it. If `VoiceAllocator` has no adopt call, add `void adoptVoice(size_t index, uint8_t note, uint8_t velocity)` (sets slot Active with a fresh timestamp); otherwise call `voices_[0].noteOff()`.
   - `setPolyphony` (359-375): unchanged except that the excess voices now keep rendering until idle.
3–6. **Build:** `ruinae_tests Ruinae` (+ `dsp_systems_tests` and consumers if `voice_allocator.h` changed). **Gate:** matching. pluginval: yes. clang-tidy: `ruinae` (+ `dsp`). Commit.

**Acceptance:** all cases pass; `AuditProbe voice stealing` and `AuditProbe automation osc dist mix voice` (poly↔mono, polyphony 16→1) stay ≤ 1.5 jump ratio.

### T1.10 — Pitch-bend smoother advances by samples, not by calls (R-010)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/processors/note_processor.h`; consumers per grep: Ruinae engine and any `PolySynthEngine` user). **Estimate:** 75 min. **Prerequisites:** T0.1.

1. **Failing tests:** `dsp/tests/unit/processors/note_processor_test.cpp`, `dsp_processors_tests`: `TEST_CASE("NoteProcessor pitch bend settles in the configured time for any block size", "[processors][note-processor]")`: prepare 48 kHz, `setPitchBend(1)`, call `processPitchBend(n)` in chunks of 32, 512 and 2048 until 25 ms of samples elapsed; REQUIRE the bend ratio is within 1 cent of the full bend in all three. `ruinae/tests/integration/midi_events_test.cpp`: `TEST_CASE("Pitch bend settles within 25 ms at every block size", "[integration][pitch-bend][block_size]")`: sine A4, full-up bend, range 2 st, block 64 / 512 / 2048; measure pitch (helper from `spectral_analysis.h` or the probe's estimator) 50 ms after the event; REQUIRE within 3 cents of +200 at all three (baseline: 90 % reached after 148 / 1166 / 4665 ms).
2. **Fix:** `note_processor.h:99-108`: add `float processPitchBend(size_t numSamples) noexcept` that calls `bendSmoother_.advanceSamples(numSamples)` (`smoother.h:241-252`) and refreshes `currentBendRatio_`; keep the zero-argument overload as `processPitchBend(1)` and correct its comment. `ruinae_engine.h:841-842`: remove the per-call step; inside the 64-sample sub-block loop (846-912) call `noteProcessor_.processPitchBend(len)` at the start of each sub-block, before the voices read `getFrequency`.
3–6. **Build:** `ruinae_tests Ruinae dsp_processors_tests` + consumers. **Gate:** matching. pluginval: yes. clang-tidy: `ruinae`, `dsp`. Commit.

**Acceptance:** both cases pass; probes `pitch-bend-settle` ≤ 25 ms and `pitch-bend:range2/12` within 3 cents.

### T1.11 — Mono mode applies pitch bend and the tuning reference (R-141, R-011)

**Flags:** CROSS-PLUGIN IMPACT only if `MonoHandler` is edited (preferred fix does not). **Estimate:** 75 min. **Prerequisites:** T1.10.

1. **Failing test:** `ruinae_engine_test.cpp`: `TEST_CASE("RuinaeEngine: mono mode applies pitch bend and tuning reference", "[ruinae-engine][mono][pitch]")`: Mono, `setTuningReference(432)`, `setPitchBendRange(2)`, `setPitchBend(1)`, `noteOn(69)`, render 200 ms; REQUIRE measured pitch within 3 cents of `432 * 2^(2/12)`; SECTION poly gives the same value.
2. **Fix:** `ruinae_engine.h` `processBlockMono` (2108-2117) and `dispatchMonoNoteOff` (1822-1826): multiply the frequency returned by `monoHandler_.processPortamento()` by `noteProcessor_.getFrequency(69) / 440.0f` (this ratio carries both the A4 reference and the smoothed bend) before `voices_[0].setFrequency`. Add a small private `float monoPitchScale() const noexcept` so both sites share it.
3–6. **Build:** `ruinae_tests Ruinae`. **Gate:** `ruinae_tests`. pluginval: yes. clang-tidy: `ruinae`. Commit.

**Acceptance:** case passes; `AuditProbe mono legato portamento` final pitch still within 5 cents.

### T1.12 — Mono Portamento Time controls the mono glide; arp Slide Time only governs slide steps (R-121, R-031)

**Flags:** PRESET-IMPACT (audio): mono presets that relied on the accidental 60 ms glide now use their stored portamento (default 0). List in the commit every generator preset with `voiceMode` mono and `portamentoTimeMs == 0`; no fixture changes (MIDI output is unaffected). **Estimate:** 90 min. **Prerequisites:** T0.1.

1. **Failing test:** `ruinae/tests/integration/param_flow_test.cpp`: `TEST_CASE("Mono portamento time controls the glide", "[integration][params][mono]")`: Mono, legato, arp Off, SECTION A portamento 500 ms: notes 60 → 72, REQUIRE pitch 100 ms after the second note is below 90 % of the way to the target; SECTION B portamento 0: REQUIRE pitch within 5 cents of 72 after 2 ms. Delete the three `REQUIRE(true)` cases at 112-128, 130-157, 370-382 or give them these assertions.
2. **Fix:** `ruinae_engine.h:1588-1595`: split `setPortamentoTime(ms)` into `setMonoPortamentoTime(ms)` (writes `monoHandler_` only) and `setSlideTime(ms)` (writes each voice's `setPortamentoTime`, used by poly legato glides). `processor_params.cpp:780` calls `setMonoPortamentoTime`; `:1032` calls `setSlideTime`. For an arp slide step in Mono mode the glide must use the slide time: in `dispatchMonoNoteOn` when the note-on is legato-from-arp (the `legato` argument of `noteOn`, `ruinae_engine.h:226-238`) temporarily apply the slide time to `monoHandler_` for that note and restore the mono portamento afterwards, the same way `userLegato_` is restored at 236-238.
3–6. **Build:** `ruinae_tests Ruinae`. **Gate:** `ruinae_tests`. pluginval: yes. clang-tidy: `ruinae`. Commit.

**Acceptance:** both SECTIONs pass; `ArpIntegration_SlidePassesLegatoToEngine` still passes.

### T1.13 — Free-run trance-gate Rate reaches the voices (R-140, R-038)

**Flags:** none. **Estimate:** 45 min. **Prerequisites:** T0.1.

1. **Failing test:** `ruinae/tests/integration/trance_gate_param_flow_test.cpp`: `TEST_CASE("TranceGate free-run rate sets the step period", "[integration][trance-gate]")`: tempo sync off, 2 steps (1, 0), depth 1, held note; SECTION 0.5 Hz and SECTION 20 Hz; measure the gate period from the output envelope (rising-edge spacing); REQUIRE within 5 % of `2 / rate` seconds. Replace the energy-comparison case at 256-288.
2. **Fix:** `ruinae_engine.h:1317-1319` `setTranceGateParams`: add `baseTranceGateRateHz_ = std::clamp(params.rateHz, 0.1f, 100.0f);` guarded by `detail::isFinite`. No processor change.
3–6. **Build:** `ruinae_tests Ruinae`. **Gate:** `ruinae_tests`. pluginval: yes. clang-tidy: `ruinae`. Commit.

**Acceptance:** both SECTIONs pass.

### T1.14 — OSC Phase Mod and Freq Mod are static offsets, applied every sample (R-135, R-092, R-021)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/systems/oscillator_adapters.h`; grep consumers), PRESET-IMPACT (audio): about 20 generator presets set non-zero `phaseMod`/`freqMod` (e.g. `tools/ruinae_preset_generator.cpp:744, 2623, 4923, 6085`); they now get the PM/FM they describe. **Estimate:** 90 min. **Prerequisites:** T1.1.

1. **Failing test:** `ruinae/tests/unit/vst/voice_osc_param_test.cpp`: `TEST_CASE("OSC phase modulation is block-size independent and continuous", "[ruinae-voice][osc-params]")`: PolyBLEP sine, `PhaseModulation = 1.0`; render 4096 samples in blocks of 64 and of 512; REQUIRE identical output (max diff ≤ 1e-6), no block-periodic outlier in the second difference (`artifact_detection.h`), and that the output differs from PhaseMod 0 over the whole buffer (it is the sine shifted by 1 rad). Repeat for Wavetable. Freq Mod SECTION: `FrequencyModulation = 1.0` shifts the measured frequency by +1 Hz.
2. **Fix:** `oscillator_adapters.h`: for the PolyBLEP and Wavetable adapters store `staticPm_` / `staticFm_` in `setParam` (203-208, 215-220) instead of calling the one-shot setters, and in `processBlock` (374-385) for those two types loop per sample: `osc_.setPhaseModulation(staticPm_); osc_.setFrequencyModulation(staticFm_); out[i] = osc_.process();` (skip the loop and call `osc_.processBlock` when both are 0). The one-shot DSP API stays unchanged.
3–6. **Build:** `ruinae_tests Ruinae dsp_systems_tests` + consumers. **Gate:** matching. pluginval: yes. clang-tidy: `ruinae`, `dsp`. Commit.

**Acceptance:** case passes for both types.

### T1.15 — SpectralFreeze oscillator survives a reset (R-118, R-016)

**Flags:** CROSS-PLUGIN IMPACT (`oscillator_adapters.h`). **Estimate:** 60 min. **Prerequisites:** T0.1.

1. **Failing tests:** `ruinae/tests/unit/ruinae_voice_test.cpp`: `TEST_CASE("RuinaeVoice: SpectralFreeze survives reset", "[ruinae-voice][spectral-freeze]")`: `setOscAType(SpectralFreeze)`, `noteOn`, render, `reset()`, `noteOn`, render; REQUIRE RMS > 0.1 (baseline 0.69147 → 0.00000). `processor_audio_test.cpp`: `TEST_CASE("SpectralFreeze patch survives setActive cycle and preset reload", "[integration][lifecycle]")`: OSC A type 8, process, `setActive(false)`/`setupProcessing`/`setActive(true)`, note, REQUIRE audible; SECTION: `setState` of the same state twice, note, REQUIRE audible.
2. **Fix:** `oscillator_adapters.h:153-155`: in the `SpectralFreezeOscillator` specialisation of `reset()`, call `osc_.reset()` then `initSpectralFreeze()` (429-441; it uses a fixed pre-allocated buffer, so it does not allocate; confirm by reading it, and if it uses a local `std::vector`, move the buffer to a member sized in `prepare`). Re-apply the current pitch (`applySpectralFreezePitch`, 449-460).
3–6. **Build:** `ruinae_tests Ruinae dsp_systems_tests` + consumers. **Gate:** matching. pluginval: yes. clang-tidy: `ruinae`, `dsp`. Commit.

**Acceptance:** both cases pass; wrap the test's reset+render in `allocation_detector.h` to prove the reset path does not allocate.

### T1.16 — SpectralFreeze oscillator plays in tune (R-148)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/processors/spectral_freeze_oscillator.h`, `oscillator_adapters.h`), PRESET-IMPACT (audio: "Spectral Wash" `tools/ruinae_preset_generator.cpp:6902-6913`, "Frozen Spectral" `:8604-8607`; they move into tune). **Estimate:** 240 min. **Prerequisites:** T1.15.

1. **Failing test:** `dsp/tests/unit/systems/selectable_oscillator_test.cpp`: `TEST_CASE("SpectralFreeze adapter tracks pitch within 5 cents A2..C7", "[systems][oscillator][pitch]")` at 44.1 and 48 kHz for MIDI 45, 57, 60, 69, 81, 96: REQUIRE |error| ≤ 5 cents and worst non-harmonic component ≤ −40 dBc (baseline −110…+122 cents, +3.3 dBc). Tighten the existing `epsilon(0.06)` cases at 957-966, 981, 999 to 0.003.
2. **Fix:** make the adapter synthesise its reference so that every partial sits on a bin centre and shift with true-frequency phase advance:
   - In `initSpectralFreeze` (`oscillator_adapters.h:426-441`) freeze a sine at exactly `k0 * sampleRate / fftSize` for an integer `k0` (bin 9 at 1024), with a Hann analysis window applied in `freeze()` (`spectral_freeze_oscillator.h:233-253`) so leakage is confined.
   - In `SpectralFreezeOscillator` add an optional "harmonic resynthesis" path used by the adapter: instead of bin remapping with fixed bin-centre phase increments (136-138, 451-469, 607-610), keep per-peak true frequencies (here one peak at `k0`), and on synthesis place each shifted peak at fractional bin `k0 * ratio` using the window main-lobe kernel, advancing its phase by `2π · f_true · ratio · hop / sr`. Keep the existing bin-remap path as the default for other consumers.
   - `applySpectralFreezePitch` (449-460) computes the ratio from the exact reference frequency, not 440.
3–6. **Build:** `ruinae_tests Ruinae dsp_systems_tests dsp_processors_tests` + consumers. **Gate:** matching. pluginval: yes. clang-tidy: `ruinae`, `dsp`. macOS proxies. Commit.

**Acceptance:** the pitch case passes at both rates; probes `osc:SpectralFreeze:*` within 5 cents.

### T1.17 — Self-oscillating filter keeps ringing under cutoff modulation (R-091, R-008, R-020)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/processors/self_oscillating_filter.h`; grep consumers), PRESET-IMPACT (audio: presets with filter type 12, `tools/ruinae_preset_generator.cpp:5269-5271`, `:8059-8066`). **Estimate:** 150 min. **Prerequisites:** T0.1.

Note from verification: disabling the kick alone does NOT fix R-020; the ladder loses oscillation near the top of the cutoff range and nothing re-excites it. A watchdog is required.

1. **Failing tests:** `dsp/tests/unit/processors/self_oscillating_filter_test.cpp`: `TEST_CASE("SelfOscillatingFilter keeps oscillating through a fast cutoff sweep", "[processors][self-osc]")`: resonance 0.95 and 1.0, zero input, per-sample cutoff `1000 * 2^(48*env/12)` with a 0.1 ms attack / 400 ms decay envelope; REQUIRE RMS of [0.5 s, 1 s) > 0.1 (baseline 0.000) and max per-sample |Δoutput| < 0.2 during the sweep. `ruinae/tests/unit/ruinae_engine_test.cpp`: `TEST_CASE("RuinaeEngine: self-osc filter with key tracking is not re-kicked per sub-block", "[ruinae-engine][filter]")`: SelfOscillating, Q 20, key track 1, note C5, osc levels 0, render 1 s at block 64 and 512; REQUIRE RMS within 1 dB of a voice-only render with the same effective cutoff and no spectral line at `sampleRate/64` above −40 dB re the tone (baseline −14.8 dB level loss, 689 Hz buzz).
2. **Fix:**
   - `self_oscillating_filter.h`: add `void setCutoffModulated(float hz) noexcept` that updates the ladder cutoff (through the existing ramp/clamp) WITHOUT touching `needsKick_`; keep `setFrequency` (430-441) for note pitch. Add an energy watchdog in `process()` (near 273-303): track a one-pole envelope of the ladder output; when `resonance_ > 0.9` and the envelope stays below 1e-4 for 64 consecutive samples, inject a single impulse of 1e-2 WITHOUT `filter_.reset()`. Keep the kick (with reset) only for `noteOn`, `prepare`, `reset` and the resonance threshold crossing (454-456).
   - `ruinae/src/engine/ruinae_voice.h:1408-1409` (`setActiveFilterCutoff`, SelfOscillating case): call `setCutoffModulated(hz)`. Line 279-283 (`noteOn`) keeps `filterSelfOsc_->noteOn`.
   - `ruinae_voice.h:766-770` `setFilterCutoff`: store `filterCutoffHz_` only; do not call `setActiveFilterCutoff` (the per-sample loop at 519-527 applies the effective cutoff). Check the block-rate early-out path (369-372) does not need the immediate write.
3–6. **Build:** `ruinae_tests Ruinae dsp_processors_tests` + consumers. **Gate:** matching. pluginval: yes. clang-tidy: `ruinae`, `dsp`. Commit.

**Acceptance:** both cases pass; the existing static-cutoff test `ruinae_voice_test.cpp:2500-2544` still passes.

### T1.18 — Delay echoes land at the set time; no hidden wet-path latency (R-055, R-116, R-153)

**Flags:** PRESET-IMPACT (audio: every preset with Digital/Tape/PingPong/Granular delay now echoes 6144 samples earlier, i.e. on the grid; no MIDI fixture changes). **Estimate:** 150 min. **Prerequisites:** T1.5.

1. **Failing tests** (`ruinae/tests/unit/ruinae_effects_chain_test.cpp`, tags `[ruinae-fx][delay][timing]`): `RuinaeEffectsChain_Echo_ArrivesAtSetDelayTime`, SECTION per type (Digital, Tape, PingPong, Granular, Spectral) at 44.1 and 96 kHz: impulse (for Spectral a 25 ms 880 Hz burst), mix 1, feedback 0, time 250 ms; REQUIRE first-echo onset within ±1 ms of 250 ms for the four time-domain types and within ±1 hop (512 samples at FFT 1024) for Spectral (baseline 389.5 ms; Spectral 289.5 ms). `RuinaeEffectsChain_SwitchToSpectral_KeepsEchoes`: Digital active, switch to Spectral at t = 0, 20 ms blip at 0.1 s, fb 0, mix 1; REQUIRE an echo is present (baseline exact zero). Rewrite the cases at 1088-1168: delete `REQUIRE(latency == 6144)` (1097) and the `latency + 50 ms` expectation (1160-1167); the FR-027 constant-latency case becomes "reported latency is 0 for every delay type". `ruinae_engine_test.cpp:701-709`: assert `getLatencySamples() == 0`. New processor case in `processor_audio_test.cpp`: `Processor_ReportedLatency_MatchesMeasuredDryPath`: impulse-like note, REQUIRE dry onset offset == `getLatencySamples()` == 0.
2. **Fix (`ruinae_effects_chain.h`):**
   - Remove the wet-only compensation: `targetLatencySamples_` (143-160, 1417) becomes 0 and the comp delay lines, `applyCompensation` (1209-1222), `applyCompensationSingle` (1226-1234) and `warmBothCompDelays` (1240-1249) and their call sites (869-871, 915-920, 947-948, 955-961) are deleted. `getLatencySamples()` (807-808) returns 0. `preWarmTotalSamples()` (468-471) becomes `max(time, 20 ms)` only.
   - Spectral: in `setDelayTime` (449-455) pass `max(minDelay, ms − 1000·fftSize/sampleRate)` to `spectralDelay_.setBaseDelayMs` so its echo also lands near the set time (its intrinsic latency is `fftSize`, `spectral_delay.h:604-606`).
   - Pre-warm (842-891): with no comp line there is nothing left to drop; verify with the new switch test that the outgoing delay's wet output stays audible until the crossfade ends.
   - Update the comment block at 833-838 and the class doc.
3–6. **Build:** `ruinae_tests Ruinae`. **Gate:** `ruinae_tests`. pluginval: yes. clang-tidy: `ruinae`. Commit.

**Acceptance:** all cases pass; probes `fx-latency:*:wet-first-echo` ≤ 48 samples (baseline 6159) and `fx-spectral-delay:fb0` shows an echo.

### T1.19 — Harmonizer keeps the stereo dry signal; the pre-reverb limiter acts on the harmony only (R-175, R-177)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/systems/harmonizer_engine.h`, additive method; consumers per grep), PRESET-IMPACT (audio: presets with `harmonizerEnabled = 1`, e.g. generator lines 2047, 2276, 2586, 2721, 3073; they regain stereo width and lose the dry squash). **Estimate:** 120 min. **Prerequisites:** T0.1.

1. **Failing tests** (`ruinae_effects_chain_test.cpp`, tags `[ruinae-fx][harmonizer]`): `Harmonizer_DryPathStaysStereo`: enable, Wet −60 dB, Dry 0 dB, wait out the fade; SECTION left-only sine: REQUIRE R RMS < 0.01 × L RMS and L within 0.5 dB of the input (baseline R == L == 0.5 × input); SECTION anti-phase L/−L: REQUIRE output RMS within 0.5 dB of the input (baseline silence). `Harmonizer_DoesNotLimitTheDryMix`: 1.5-amplitude sine, harmonizer On with Wet −60 dB vs Off; REQUIRE RMS difference < 0.1 dB; then disable and REQUIRE no sample-to-sample step above the steady-state bound at the end of the fade-out.
2. **Fix:**
   - `harmonizer_engine.h`: add `void processWetOnly(const float* input, float* outputL, float* outputR, size_t n) noexcept`, identical to `process` (213-563) except the final blend (557-563) writes `wetGain * voiceCountScale * voices` with no dry term; factor the shared body so the code is not duplicated. Expose `float currentDryGain()` (the smoothed dry gain) or return it per block.
   - `ruinae_effects_chain.h` harmonizer slot (998-1056): always copy the stereo input to `harmonizerDryL_/R_`; call `processWetOnly` into scratch wet buffers; apply `Sigmoid::softLimit` to the wet buffers only; output = `dryGain * harmonizerDry + wet`, crossfaded against the untouched input by `harmonizerFadeAlpha_` during FadingIn/FadingOut. Delete the full-bus limiter at 1062-1072.
3–6. **Build:** `ruinae_tests Ruinae dsp_systems_tests` + consumers. **Gate:** matching. pluginval: yes. clang-tidy: `ruinae`, `dsp`. Commit.

**Acceptance:** all SECTIONs pass; the existing harmonizer level test (`ruinae_effects_chain_test.cpp:1520-1554`) still passes with a mono input.

### T1.20 — Spectral delay feedback limiter works in the signal's own scale (R-057)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/effects/spectral_delay.h`: Iterum spectral mode, Ruinae), PRESET-IMPACT (audio: every Spectral-delay preset in Ruinae AND Iterum gets real feedback; Iterum approval goldens (`approval_tests`) may change: build and run `plugin_tests approval_tests` in the gate and re-approve only spectral-delay diffs). **Estimate:** 90 min. **Prerequisites:** T0.1.

1. **Failing test:** `dsp/tests/unit/effects/spectral_delay_test.cpp`, `dsp_effects_tests`: `TEST_CASE("SpectralDelay feedback decays by the feedback amount per echo", "[effects][spectral-delay]")`: 0.5-amplitude 100 ms 440 Hz burst, delay 300 ms, feedback 0.9, mix 1; REQUIRE echo2/echo1 and echo3/echo2 RMS ratios in [−2 dB, 0 dB] (baseline −33.3 dB). Second case: feedback 1.2 for 10 s stays bounded (peak < 4× input).
2. **Fix:** `spectral_delay.h:928-948`: compute `const float magScale = 0.25f * static_cast<float>(fftSize_);` (Hann, 50 % overlap) once per frame and use `feedbackMag = magScale * std::tanh(delayedMag * binFeedback / magScale)` for L and R (932-933).
3–6. **Build:** `ruinae_tests Ruinae dsp_effects_tests plugin_tests approval_tests` + consumers. **Gate:** matching. pluginval: Ruinae and Iterum. clang-tidy: `dsp`. Commit.

**Acceptance:** both cases pass; Iterum approvals re-approved only where the diff is spectral-delay feedback.

### T1.21 — Dice button reaches the processor (R-142)

**Flags:** none. **Estimate:** 45 min. **Prerequisites:** T0.1.

1. **Failing test:** `arp_integration_test.cpp`: `TEST_CASE("ArpDice_TriggerSurvivesOnOffPairInOneBlock", "[arp][params]")`: feed one `IParamValueQueue` for `kArpDiceTriggerId` with points (0, 1.0) and (0, 0.0) using `vst_param_changes.h`; run `process()`; REQUIRE the dice variation changed (the note sequence over the next bar differs from a no-dice baseline run with the same seed, or expose `arpCore_` dice generation counter for the test).
2. **Fix:** `ruinae/src/controller/controller.cpp:793-799`: remove `performEdit(kArpDiceTriggerId, 0.0)`; keep `setParamNormalized(kArpDiceTriggerId, 0.0)` so the controller's own parameter resets (Gradus sends only the 1.0). Additionally, in `processParameterChanges` (`processor_params.cpp:141-148`) for `kArpDiceTriggerId` only, scan every point of the queue and pass 1.0 to the handler if any point is ≥ 0.5.
3–6. **Build:** `ruinae_tests Ruinae`. **Gate:** `ruinae_tests`. pluginval: yes. clang-tidy: `ruinae`. Commit.

**Acceptance:** case passes; `bottom_bar_test.cpp:23-101` updated to the new single-edit contract.

### T1.22 — Browser preset loads send the voice routes to the processor (R-124, R-040)

**Flags:** none. **Estimate:** 90 min. **Prerequisites:** T1.2.

1. **Failing test:** `ruinae/tests/unit/controller/preset_browser_test.cpp`: `TEST_CASE("Controller browser load sends voice routes to the processor", "[controller][presets][modmatrix]")`: connect a `Controller` and a `Processor` through a minimal `IConnectionPoint` shim (see how `voice_route_rt_safety_test.cpp` builds messages); load state A (one active voice route) through `loadComponentStateWithNotify`, then state B (no routes); after each, REQUIRE the processor's `getState` voice-route bytes equal the loaded state's.
2. **Fix:** `ruinae/src/controller/controller_presets.cpp:364-387` `loadComponentStateWithNotify`: after `loadStateCore` succeeds and `!arpOnly`, send all 16 `cachedVoiceRoutes_` to the processor. Extract the message-building code of the grid callback (`controller_mod_matrix.cpp:126-144`) into `void Controller::sendVoiceRouteToProcessor(int slot, const VoiceModRoute&)` and call it for every slot (inactive slots send `active = 0`, which the processor stores as cleared). Suppress the per-message `VoiceModRouteState` echo from re-entering the grid during the loop, or accept it since it carries the same data.
3–6. **Build:** `ruinae_tests Ruinae`. **Gate:** `ruinae_tests`. pluginval: yes. clang-tidy: `ruinae`. Commit.

**Acceptance:** case passes for A then B.

### T1.23 — Global mod-route add/remove/detail edits keep all seven slot parameters in sync (R-082, R-143, R-083)

**Flags:** CROSS-PLUGIN IMPACT only if `plugins/shared/src/ui/mod_matrix_grid.h` is edited (preferred fix is controller-only). **Estimate:** 120 min. **Prerequisites:** T1.3.

1. **Failing tests** (`ruinae/tests/integration/mod_matrix_grid_test.cpp`, tags `[modmatrix][controller]`; wire a real `ModMatrixGrid` to a real `Controller` via `wireModMatrixGrid`): `ModMatrix_RemoveRoute_ShiftsAllSevenParams`: routes 0 (LFO1→Cutoff) and 1 (LFO2→Reso, bypass on, scale ×4, curve 1, smooth 50); `removeRoute(0)`; REQUIRE `getParamNormalized` of slot 0 bypass/scale/curve/smooth equal route 1's values and slot 1 source == 0, bypass == 0, scale == default. `ModMatrix_AddRoute_ResetsDetailParams`: leave bypass = 1 on an emptied slot, `addRoute`, REQUIRE bypass param 0. `ModMatrix_DetailEdit_UpdatesControllerParam`: `fireBypassToggleForSlot(0)`, REQUIRE `getParamNormalized(modSlotBypassId(0)) == 1.0` and `syncModMatrixGrid()` keeps bypass true. `ModMatrix_AmountDrag_OneGesture`: record begin/perform/end calls through a recording component handler during a simulated amount drag; REQUIRE exactly one begin and one end for the amount ID and no edits of source/dest IDs.
2. **Fix (`ruinae/src/controller/controller_mod_matrix.cpp`):**
   - `:55-61` parameter callback: call `setParamNormalized(paramId, v)` before `performEdit` (keep `suppressModMatrixSync_`).
   - `:268-322` `pushAllGlobalRouteParams`: for every slot push all seven IDs (source, dest, amount, curve/3, smooth/100, scale index/4, bypass), each with `setParamNormalized` + begin/perform/end; the inactive branch (307-319) also calls `setParamNormalized` and writes defaults for the four detail params.
   - `:80-120` route-changed callback: push only fields whose normalized value differs from the controller's current parameter value, and never open a gesture for an ID that already has one open (track `openGestureId_` set by the grid's begin/end edit callbacks).
3–6. **Build:** `ruinae_tests Ruinae`. **Gate:** `ruinae_tests`. pluginval: yes. clang-tidy: `ruinae`. Commit.

**Acceptance:** four cases pass.

### T1.24 — Voice-matrix destinations use one index space; Spectral Tilt reaches the tilt (R-039, R-109, R-025)

**Flags:** PRESET-IMPACT (audio: generator preset at `tools/ruinae_preset_generator.cpp:2139` routes Env3 → `kVDstSpecTilt`; it now blooms tilt instead of touching OSC A level). No state change: stored UI index 7 keeps meaning "Spectral Tilt". **Estimate:** 120 min. **Prerequisites:** T1.2.

1. **Failing tests:** `ruinae/tests/integration/voice_mod_all_targets_test.cpp`: `TEST_CASE("Voice route UI destination 7 modulates spectral tilt, not OSC A level", "[integration][modmatrix][voice]")`: send `VoiceModRouteUpdate{source = Velocity, destination = 7, amount = 1}` through `notify`; SpectralMorph mix mode; REQUIRE the spectral centroid differs from the unrouted render (helper in `spectral_analysis.h`); in CrossfadeMix with OSC A level 0.5, REQUIRE output RMS equals the unrouted render (OSC A level untouched). `TEST_CASE("Per-voice spectral tilt route is audible in poly mode", "[ruinae-engine][modmatrix]")` in `ruinae_engine_test.cpp`: poly, voice route Env3 → `VoiceModDest::SpectralTilt` amount +1; REQUIRE spectrum slope differs from unrouted. `voice_route_rt_safety_test.cpp`: `TEST_CASE("VoiceModRouteUpdate clamps to the voice enums", "[vst][modmatrix]")`: source 9, destination 14 → echoed state holds source ≤ 7, destination ≤ 7.
2. **Fix:**
   - `ruinae/src/processor/processor_params.cpp:516-535`: add `static constexpr VoiceModDest kUiVoiceDestToDsp[kNumVoiceDestinations]` = {FilterCutoff, FilterResonance, MorphPosition, DistortionDrive, TranceGateDepth, OscAPitch, OscBPitch, SpectralTilt} with a `static_assert` on the size, and use it at line 521 instead of the raw cast.
   - `ruinae/src/processor/processor_messaging.cpp:41-45`: clamp source to `VoiceModSource::NumSources − 1` and destination to `kNumVoiceDestinations − 1`.
   - `ruinae/src/engine/ruinae_voice.h`: move the SpectralTilt application from after the block (542-551) to before `spectralMorph_->processBlock` (425), using the previous block's router offset exactly as the OscLevel code at 408-415 does, and keep `mixTilt_` (base) separate from the applied value so the engine's per-sub-block `setMixTilt(base)` (`ruinae_engine.h:1980-1987`, `ruinae_voice.h:734-737`) no longer discards the offset: `setMixTilt` stores the base only; the voice applies `base + offset` once per block.
3–6. **Build:** `ruinae_tests Ruinae`. **Gate:** `ruinae_tests`. pluginval: yes. clang-tidy: `ruinae`. Commit.

**Acceptance:** three cases pass.

### T1.25 — Mod-matrix curve labels match the DSP; Stepped stays within [0, 1] (R-043, R-081, R-013)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/core/modulation_curves.h`: Disrumpo, Vorago, Seraphis, Profundum, Ruinae; `plugins/shared/src/ui/mod_matrix_types.h`: grep consumers), PRESET-IMPACT (audio: routes with curve index 3 and a source that sits at exactly ±1 lose a 33 % overshoot, e.g. generator `:9552`; other plugins' goldens may move: run their suites). No state change: indices keep their DSP meaning; only labels change. **Estimate:** 90 min. **Prerequisites:** T0.1.

1. **Failing tests:** `dsp/tests/unit/core/modulation_curves_test.cpp`: change the pinned value at 65-69 (and 163) to `REQUIRE(applyModCurve(ModCurve::Stepped, 1.0f) == Approx(1.0f))` and add `TEST_CASE("applyModCurve maps [0,1] into [0,1] for every curve", "[core][modulation]")` sweeping 1001 points. `ruinae/tests/unit/mod_matrix_params_test.cpp`: `TEST_CASE("Mod curve list entries name the DSP ModCurve at the same index", "[modmatrix][params]")`: REQUIRE `kModCurveStrings[2]` == "S-Curve", `[3]` == "Stepped", and the same for `Krate::Plugins::kCurveTypeNames`.
2. **Fix:** `modulation_curves.h:51-52`: `return std::min(std::floor(x * 4.0f), 3.0f) / 3.0f;`. `ruinae/src/parameters/mod_matrix_params.h:43-47` and comment `:26`: labels {Linear, Exponential, S-Curve, Stepped}. `plugins/shared/src/ui/mod_matrix_types.h:113-118` (and comment `:80`): same four labels. Update `plugins/ruinae/README.md:83` and `CHANGELOG.md` (add an Unreleased bullet; do not edit the historic line 539).
3–6. **Build:** `ruinae_tests Ruinae dsp_core_tests` + every consumer executable named by `header-consumers.js` (expect disrumpo, vorago, seraphis, profundum, shared tests). **Gate:** matching. pluginval: yes. clang-tidy: `ruinae`, `dsp`, `shared`. Commit.

**Acceptance:** both suites pass; a Macro at 1.0 routed with Stepped at amount 0.5 gives an offset of exactly 0.5.

### T1.26 — LFO 1/2 Depth and Chaos Depth scale the modulation (R-041, R-183)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/systems/modulation_engine.h`, additive setters), STATE-COMPAT (semantic: version 9; states < 9 load depth 1.0), PRESET-IMPACT (per the ruling: generator depths rewritten to 1.0 so audio is unchanged). **Ruling required** (see table). **Estimate:** 150 min. **Prerequisites:** T1.2.

1. **Failing tests:** `ruinae/tests/integration/mod_source_pipeline_test.cpp`: `TEST_CASE("LFO depth scales the routed modulation", "[integration][modmatrix][lfo]")`: LFO1 → Global Filter Cutoff amount 1; depth 0 → the destination offset (read with the engine's `getGlobalModOffset` test hook used elsewhere in this file) is 0 on every block; depth 0.5 → peak offset 0.5 ± 0.02; depth 1 → 1.0. Same case for LFO2 and for Chaos (`kChaosModDepthId`). `state_migration_test.cpp`: `TEST_CASE("State v8 loads LFO and Chaos depth as 1.0", "[state][migration]")`: v8 blob with LFO2 depth 0.6 and chaos depth 0 → after `setState` both read 1.0 in `getState` AND in the controller after `setComponentState`.
2. **Fix:**
   - `modulation_engine.h`: add `setLFO1Depth(float)`, `setLFO2Depth(float)`, `setChaosDepth(float)` (clamped [0,1], non-finite ignored, default 1.0); apply after the unipolar mapping at 275-285 (`lfoNCurrentOutput_ *= depth`) and to the chaos value returned at 697-698.
   - `ruinae_engine.h` near 486-518: forwards `setGlobalLFO1Depth/2Depth`, `setChaosDepth`. `processor_params.cpp:426-472`: forward `lfo1Params_.depth`, `lfo2Params_.depth`, `chaosModParams_.depth`.
   - Defaults: `chaos_mod_params.h:19` atomic `depth{1.0f}` and the registered default at 53-54 → 1.0; uidesc knob default-values `editor.uidesc:1976, 2071` (LFO, currently 0.5) and `:2167` (Chaos) → 1.0 (apply uidesc edits with the project's XSLT workflow).
   - Version 9 gate on both load paths: when `version < 9`, after the packs load, force the three depths to 1.0 (processor atomics; controller `synthSetter(kLFO1DepthId, 1.0)` etc.).
   - `tools/ruinae_preset_generator.cpp`: set every authored `lfo*.depth` and chaos depth to 1.0 (list the changed presets in the commit), write version 9.
3–6. **Build:** `ruinae_tests Ruinae dsp_systems_tests` + consumers; rebuild the preset generator target and regenerate presets. **Gate:** matching. pluginval: yes. clang-tidy: `ruinae`, `dsp`. Commit.

**Acceptance:** both cases pass; `factory_preset_audio_test` levels unchanged within its existing bounds.

### T1.27 — Note velocity reaches the VCA (R-018)

**Flags:** STATE-COMPAT (new parameter appended at the end of the stream; version 9), PRESET-IMPACT (audio: all factory presets become velocity-sensitive; arp Velocity/Accent lanes become audible; no MIDI fixture change). **Ruling required.** **Estimate:** 180 min. **Prerequisites:** T1.26 (version 9 exists).

1. **Failing tests:** `ruinae_voice_test.cpp`: `TEST_CASE("RuinaeVoice: velocity scales the VCA by the amp-velocity amount", "[ruinae-voice][velocity]")`: amount 1: steady-state RMS at velocity 32/127 vs 127/127 differs by 20·log10(32/127) ± 0.5 dB; amount 0: equal within 0.1 dB. `state_migration_test.cpp`: `TEST_CASE("State v8 loads amp velocity amount 0", "[state][migration]")`. `controller_params_test.cpp`: the new parameter is registered, automatable, default 1.0.
2. **Fix:**
   - `ruinae/src/plugin_ids.h`: add `kAmpEnvVelocityAmountId` at the next free ID in the Amp Env block (700s; read the block and pick the first unused value; do not reuse any ID). `ruinae/src/parameters/env_params.h` (amp pack) or `global_params.h`: atomic `ampVelocityAmount{1.0f}`, handler, registration ("Velocity > Amp", %, default 1.0), formatter, and save/load helpers that append the field AFTER the arp pack on both sides (`processor_state.cpp` getState/setState/applyPresetSnapshot and `controller_presets.cpp::loadStateCore`), gated `version >= 9`, with `else` branches setting 0.0.
   - `ruinae_voice.h`: add `setAmpVelocityAmount(float)`; at 621-623 `ampLevel = output[i] * expressionVolume_ * (1.0f + ampVelocityAmount_ * (velocity_ − 1.0f))`. Engine forward + `processor_params.cpp`.
   - `tools/ruinae_preset_format.h` + generator: write the field (1.0). Add a knob in the Amp envelope section of `editor.uidesc` (XSLT workflow) and a MANUAL.md line.
3–6. **Build:** `ruinae_tests Ruinae` + generator. **Gate:** `ruinae_tests`. pluginval: yes. clang-tidy: `ruinae`. Commit.

**Acceptance:** all cases pass; `preset_format_compat_test` updated and green.

### T1.28 — MIDI pitch bend, channel aftertouch and mod wheel arrive through IMidiMapping (R-030, R-042)

**Flags:** none (new hidden, non-persisted parameters; not part of the state). **Estimate:** 150 min. **Prerequisites:** T1.10, T1.11.

1. **Failing tests:** `ruinae/tests/unit/controller_params_test.cpp`: `TEST_CASE("Controller maps pitch bend, aftertouch and mod wheel", "[controller][midi]")`: query `IMidiMapping` through `FUnknownPtr`; REQUIRE `getMidiControllerAssignment(0, 0, kPitchBend, id) == kResultOk` (and `kAfterTouch`, `kCtrlModWheel`) with ids that are registered parameters. `ruinae/tests/integration/midi_events_test.cpp`: `TEST_CASE("Pitch-bend parameter moves the pitch by the bend range", "[integration][pitch-bend]")`: parameter change 1.0 on the bend ID with a held A4, range 2 → pitch within 3 cents of +200. `TEST_CASE("Aftertouch parameter drives the voice Aftertouch source", "[integration][modmatrix]")`: voice route Aftertouch → FilterCutoff, parameter 1.0 → spectral centroid rises vs 0.0.
2. **Fix:**
   - `plugin_ids.h`: add `kMidiPitchBendId`, `kMidiAftertouchId`, `kMidiModWheelId` in a free block OUTSIDE `kArpBaseId..kArpEndId` (3000–3445) and not 3500 (`kSidechainActiveId`); e.g. 3510–3512 after confirming they are unused.
   - Controller (`controller.h:72-74`, `220-223`): implement `Steinberg::Vst::IMidiMapping` (model: `plugins/profundum/src/controller/controller.cpp:918-939`); register the three parameters as `kCanAutomate | kIsHidden`, pitch bend default 0.5.
   - Processor: `processParameterChanges` (`processor_params.cpp:125-246`) for these three IDs iterates ALL queue points and queues a `TimedEvent` per point at its `sampleOffset` (`PitchBend` with `2v − 1`; add `Aftertouch` and `ModWheel` kinds handled in `applyTimedEvent`, `processor_midi.cpp:90-112`, calling `engine_.setAftertouch(v)` / `engine_.setModWheel(v)`). They are not saved.
   - Keep the legacy `kLegacyMIDICCOutEvent` path (`processor_midi.cpp:245-248`) for hosts that send it.
3–6. **Build:** `ruinae_tests Ruinae`. **Gate:** `ruinae_tests`. pluginval: yes. clang-tidy: `ruinae`. Commit.

**Acceptance:** three cases pass; `state_roundtrip_test` shows the state size unchanged by this task.

### T1.29 — Retrigger Beat works without host musical time (R-068)

**Flags:** macOS-SENSITIVE (musical time: use `std::llround`). **Estimate:** 60 min. **Prerequisites:** T0.1.

1. **Failing test:** `arp_integration_test.cpp`: `TEST_CASE("ArpIntegration_RetriggerBeat_WithoutPpq_StillWalksThePattern", "[arp][integration]")`: context with `kPlaying | kTempoValid` and no `kProjectTimeMusicValid`, Up over 60/64/67, 1/16, Retrigger Beat, 2 s; REQUIRE more than one distinct pitch on the output bus and the sequence 60 64 67 repeating (baseline 60 60 60 …). SECTION: null `processContext`.
2. **Fix:** `ruinae/src/processor/processor.cpp:269-275` and 304-310: when `projectTimeMusicValid` is false, set `arpCtx.transportPositionSamples` from a processor-owned free-running counter `arpFreeRunSamples_` (int64, reset to 0 in `setActive(true)` and when the arp starts running, advanced by `numSamples` each block the arp runs). No core edit.
3–6. **Build:** `ruinae_tests Ruinae`. **Gate:** `ruinae_tests`. pluginval: yes. clang-tidy: `ruinae`. Run `node tools/lint-musical-time-truncation.js`. Commit.

**Acceptance:** both SECTIONs pass.

### T1.30 — Single-note arp steps with gate > 100 % are not cut by the previous step's NoteOff (R-067)

**Flags:** GRADUS IMPACT, CROSS-PLUGIN IMPACT (`dsp/processors/arpeggiator_core.cpp`), PRESET-IMPACT (Ruinae and Gradus goldens for presets with gate > 100 % and repeating pitches: regenerate with the T0.1 procedure; justification = only NoteOff timestamps of repeated pitches move from "old onset + gate" to "new onset"). **Estimate:** 120 min. **Prerequisites:** T1.8.

1. **Failing test:** `dsp/tests/unit/processors/arpeggiator_core_core_test.cpp`, `dsp_processors_tests`: `TEST_CASE("GMF-005 single note: repeated pitch with gate > 100% is released at the next onset", "[processors][arp][gate]")`: one held note, 1/8 at 120 BPM, gate 150 %; REQUIRE each NoteOn is preceded at the same sample offset by the NoteOff of the previous instance (no NoteOff falls mid-step) and every NoteOn-to-NoteOff duration equals one step except the last (baseline: OFF at onset + 5512).
2. **Fix:** `arpeggiator_core.cpp:1230-1257` single-note branch: before emitting the NoteOn, if `result.notes[0]` is in `currentArpNotes_` or has a pending off, cancel its pending off (`cancelPendingNoteOffs` for that pitch only; write a per-pitch variant of the helper at `arpeggiator_core.h:1499-1513`), emit its NoteOff at `humanizedSampleOffset`, and remove it from `currentArpNotes_`, mirroring the chord path at 1173-1178.
3–6. **Build:** `ruinae_tests Ruinae dsp_processors_tests gradus_tests` + consumers. **Gate:** matching; `arp_shared_prefix_golden` stays green. pluginval: Ruinae and Gradus. clang-tidy: `dsp`, `ruinae`, `gradus`. Commit.

**Acceptance:** case passes; all regenerated goldens balanced (T1.8 invariant).

### T1.31 — Per-block musical sync preserves swing (R-066)

**Flags:** GRADUS IMPACT, CROSS-PLUGIN IMPACT, macOS-SENSITIVE (`std::llround` everywhere; run `tools/macos-fastmath-repro.sh`), PRESET-IMPACT (every swung arp golden in Ruinae and Gradus, e.g. `ruinae_factory_Tape_Shuffle_golden_midi.txt`; justification = onsets alternate long/short within 2 samples and the 128–142-sample double hits disappear). **Estimate:** 180 min. **Prerequisites:** T1.30.

1. **Failing test:** `dsp/tests/unit/processors/arpeggiator_core_dawloop_test.cpp`: `TEST_CASE("syncToMusicalPosition keeps swing timing at every block size", "[processors][arp][swing]")`: 1/16 at 120 BPM, swing 50 %, ppq-driven blocks of 64 / 512 / 4096 with sync every block, 4 bars; REQUIRE NoteOn intervals alternate `base*(1+swing)` / `base*(1−swing)` within 2 samples and the onset list is identical across block sizes within 2 samples (baseline intervals 5060, 6101, 5044, 409, …).
2. **Fix:** `arpeggiator_core.h:1029-1052` `syncToMusicalPosition`: compute the position modulo a TWO-step period: `calculateMusicalStepPosition(projectTimeMusic, noteValue_, noteModifier_, 2)` (or derive pair phase from the step index parity); let `pairFrac` be the fraction within the pair; with `longDur = base*(1+swing)` and `shortDur = 2*base − longDur`, if `pairFrac*2*base < longDur` the arp is in the long half: set `swingStepCounter_` parity even, `currentStepDuration_ = longDur`, `sampleCounter_ = llround(pairFrac*2*base)`; else odd parity, `currentStepDuration_ = shortDur`, `sampleCounter_ = llround(pairFrac*2*base) − longDur`. With swing 0 this reduces to today's behaviour (assert with the existing 370-418 test).
3–6. **Build:** `ruinae_tests Ruinae dsp_processors_tests gradus_tests` + consumers. **Gate:** matching. pluginval: both. clang-tidy: `dsp`. macOS proxies. Commit (with golden diff summaries).

**Acceptance:** case passes; swung goldens regenerated and justified.

### T1.32 — Arp skip highlights leave the audio thread and carry each lane's own step (R-069, R-002)

**Flags:** GRADUS IMPACT (additive getter in `arpeggiator_core.h`). **Estimate:** 180 min. **Prerequisites:** T1.3.

1. **Failing tests:** `ruinae/tests/unit/vst/skip_event_test.cpp`: `TEST_CASE("process() never calls sendMessage for skip events", "[vst][arp][rt-safety]")`: connect a recording `IConnectionPoint` peer that counts `notify` calls and whether the processor is inside `process()` (set a flag around the call in the test); drive a Euclidean pattern that emits `kSkip`, editor-open flag on; REQUIRE zero `notify` calls from inside `process()`. `TEST_CASE("Skip highlight uses each lane's own step and covers all eight lanes", "[vst][arp]")`: velocity lane length 4, gate lane length 3; after a skip, REQUIRE the published gate-lane skip step equals the gate lane's step (≠ velocity step) and that lanes 6 and 7 publish too.
2. **Fix:**
   - Core: in `fireStep` (`arpeggiator_core.cpp` around 486-488 where `velStep`, `gateStep`, `ratchetStep` are captured) store all eight lanes' pre-advance step indices in `std::array<uint8_t, 8> lastFiredLaneSteps_`; add `uint8_t lastFiredLaneStep(size_t lane) const noexcept`.
   - `plugin_ids.h`: eight hidden read-only output parameters `kArpSkipLane0Id … kArpSkipLane7Id` (free block outside the arp range, e.g. 3520–3527; not saved). Value encoding: `(step + 1 + 33 * (seq & 3)) / 132.0`, where `seq` increments per skip so a repeated step still changes the value; 0 means "none".
   - Processor `processor.cpp:379-386`: replace `sendSkipEvent` with writes to `outputParameterChanges` for the eight IDs (same mechanism as the playheads at 393-415). Delete `sendSkipEvent` (`processor_messaging.cpp:113-130`), `skipMessages_` (`processor.h:255`) and their pre-allocation (`processor.cpp:67-80`).
   - Controller: decode in the 30 fps poll timer (`controller.cpp:478-557`) and call `setSkippedStep` on the lane when the value changed; delete the `ArpSkipEvent` branch of `notify` (414-428) and `handleArpSkipEvent`'s message path (`controller_arp.cpp:19-41`).
3–6. **Build:** `ruinae_tests Ruinae dsp_processors_tests gradus_tests` + consumers. **Gate:** matching. pluginval: yes. clang-tidy: `ruinae`, `dsp`. Commit.

**Acceptance:** both cases pass; `voice_route_rt_safety_test.cpp:448-475` (skip pre-allocation) is removed with the code it tested.

### T1.33 — SpectralMorph voices: VCA aligned with the delayed signal, no replay of the previous note; idle voices start clean (R-017, R-026)

**Flags:** none (voice only). **Estimate:** 150 min. **Prerequisites:** T0.1.

1. **Failing tests** (`ruinae_voice_test.cpp`, tags `[ruinae-voice][spectral-morph]` / `[ruinae-voice][idle]`): `RuinaeVoice_SpectralMorph_NoStaleTailAtNextNote`: SpectralMorph mode, note 1 full level, release to idle, set both OSC levels 0, note 2; REQUIRE RMS of note 2's first 1024 samples < 1e-3 (baseline 0.697). `RuinaeVoice_SpectralMorph_AttackShapesTheOnset`: 1 ms attack; REQUIRE the 10–90 % rise of the output envelope lasts 1 ms ± 0.5 ms and the note's last 23 ms of oscillator content is heard (the envelope gate is delayed, so release starts 1024 samples after `noteOff`). `RuinaeVoice_IdleVoiceStartsClean`: CrossfadeMix, Comb filter Q 30, note 1, release to idle, OSC levels 0, note 2; REQUIRE RMS < 1e-4 (baseline 0.0148 with SVF).
2. **Fix (`ruinae_voice.h`):**
   - `noteOn` (227-288): capture `const bool wasIdle = !ampEnv_.isActive();` before gating; when `wasIdle`, call `resetActiveFilter()` (1313-1343), `resetActiveDistortion()`, `dcBlocker_.reset()` and `spectralMorph_->reset()`; keep the no-reset path (256-271) for a retrigger of a sounding voice.
   - SpectralMorph alignment: add a pre-allocated 1024-sample gain delay line (sized in `prepare`, 108-187, from `spectralMorph_->getLatencySamples()`); in SpectralMorph mode delay the VCA gain written at 539 and read at 622 by that latency, and keep the voice active until the delayed gain has reached zero (extend the idle test at 369-372: idle only when the envelope is idle AND the gain delay line has drained).
   - `setFilterType` (748-763) and `setDistortionType` (925-929): reset the incoming filter/distortion before it becomes active (T2.24 adds the crossfade).
3–6. **Build:** `ruinae_tests Ruinae`. **Gate:** `ruinae_tests`. pluginval: yes. clang-tidy: `ruinae`. Commit.

**Acceptance:** three cases pass; fast-retrigger probe (`AuditProbe fast retrigger`) stays ≤ 1.05 jump ratio.

### T1.34 — Envelope curve tables are rebuilt only when their inputs change (R-001)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/primitives/adsr_envelope.h`: Membrum and every envelope user; grep). **Estimate:** 90 min. **Prerequisites:** T0.1.

1. **Failing tests:** `dsp/tests/unit/primitives/adsr_envelope_test.cpp`, `dsp_primitives_tests`: `TEST_CASE("ADSREnvelope does not regenerate an unchanged curve table", "[primitives][adsr]")`: expose `uint32_t tableGenerationCountForTest()`; `setAttackCurve(0.5f)` twice → counter +1; `setAttackBezierCurve(a,b,c,d)` twice → +1; changing one control point → +1. `ruinae/tests/unit/arp_performance_test.cpp` (or a new `ruinae/tests/unit/params_apply_performance_test.cpp`): `TEST_CASE("applyParamsToEngine costs under 5% of a 64-sample block with Bezier envelopes", "[performance][wallclock]")`: time 10 000 `process()` calls of 64 samples with Bezier enabled and no parameter change, subtract a baseline with silent voices; REQUIRE mean parameter-apply cost < 66 µs at 48 kHz and that the measured value is ≤ 60 % of the bar on the dev box. Runs only in the CPU lane (`node tools/run-cpu-tests.js ruinae_tests`), not in the gate.
2. **Fix:** `adsr_envelope.h:160-203`: in each of the six curve setters compare the new amount (or the four control points; store them per segment) with the stored values and return early when equal and the table is already valid. `:115-136` `setAttack/setDecay/setRelease`: return early when the time is unchanged. No change to the engine fan-out.
3–6. **Build:** `ruinae_tests Ruinae dsp_primitives_tests` + consumers. **Gate:** matching (the `[performance]` case is excluded by the filter); then the CPU lane alone. pluginval: yes. clang-tidy: `dsp`, `ruinae`. Commit.

**Acceptance:** generation-counter case passes; CPU-lane case passes (baseline 270–650 µs per block).

---
## Phase 2 — medium defects (and low defects that share their fix)

Format is the same as Phase 1. Unless a task says otherwise: prerequisites = T0.1; build `ruinae_tests Ruinae`; gate `ruinae_tests`; pluginval yes; clang-tidy `ruinae`; then commit. A task flagged CROSS-PLUGIN adds the DSP test target and the consumers named by `header-consumers.js` to the build and the gate, and clang-tidy `dsp`.

### T2.1 — Non-finite floats in a state never reach the DSP (R-170, R-171, R-172, R-173, R-174)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/systems/modulation_engine.h`, `dsp/effects/fdn_reverb.h`, `dsp/primitives/lfo.h`, `dsp/primitives/adsr_envelope.h`, `dsp/processors/trance_gate.h`), GRADUS IMPACT (`arpeggiator_core.h` setters), macOS-SENSITIVE (finiteness only via `detail::isFinite`; NaN-injection cases go in `dsp_ieee_tests` and in the IEEE block of `ruinae/tests/CMakeLists.txt:203`). **Estimate:** 180 min. **Prerequisites:** T1.1 (`state_load_helpers.h`), T1.2.

1. **Failing tests:** `dsp_ieee_tests` (new file `dsp/tests/unit/ieee/nonfinite_setters_test.cpp`): `TEST_CASE("DSP setters ignore non-finite input", "[ieee][robustness]")` with SECTIONs: `ModulationEngine::setLFO1Rate(NaN)` + route → `getModulationOffset` finite; `setMacroValue(0, NaN)`; `LFO::setSymmetry(NaN)`; `FDNReverb::setParams{roomSize = NaN}` then valid params → output finite and non-zero; `ADSREnvelope::setAttackBezierCurve(0.3, NaN, …)` → table finite; `TranceGate::setParams{depth = NaN, attackMs = NaN}` → `process` finite; `ArpeggiatorCore::setFreeRate(NaN)` keeps the previous rate; `setLaneSpeed(0, NaN)` keeps the lane advancing. `ruinae/tests/unit/state_migration_test.cpp`: `TEST_CASE("setState sanitises non-finite floats", "[state][robustness]")`: v8 blob with NaN in LFO1 rate (routed to Global Cutoff, global filter on), reverb size (type Hall), filter-env Bezier cp1y (Bezier on), trance-gate depth, arp free rate and one lane speed; `setState`, play; REQUIRE non-zero finite output, and still non-zero after a second `setState` with a clean blob (no stuck-silent engine).
2. **Fix:**
   - Loaders: use `finiteClamp(v, lo, hi, default)` for every float in `lfo_params.h:253,278`, `macro_params.h:75-78`, `reverb_params.h:176-184`, `env_params.h:285-301` (control points to [0,1], `bezierEnabled` to 0/1), `trance_gate_params.h:308-315`, `arpeggiator_params.h:1285,1288,1306-1307,1317-1318,1351,1399,1402,1407` and the lane speeds at 1444-1459 (snap to the nearest `kLaneSpeedValues` entry, as the controller does at 1717-1725). Ranges = the ones each `handle*ParamChange` uses.
   - DSP defence: `modulation_engine.h:761-821` `evaluateRoutings`: map a non-finite source value, contribution or summed offset to 0 (as `voice_mod_router.h:189-195` does); `setMacroValue` (466-470), `LFO::setFrequency` (`lfo.h:213-218`), `setSymmetry` (297-299), `FDNReverb::setParamsInternal` (`fdn_reverb.h:553-608`), `ADSREnvelope::set*BezierCurve` (`adsr_envelope.h:182-203`), `TranceGate::setParams` (`trance_gate.h:153-171`), `ArpeggiatorCore::setFreeRate`/`setGateLength`/`setLaneSpeed` (`arpeggiator_core.h:438-445, 700-703`): return without changing state when an argument is non-finite. Engine Bezier setters (`ruinae_engine.h:1431-1441, 1492-1502, 1553-1562`): add the same guard the scalar setters have at 1416-1419.
3–6. **Build:** add `dsp_ieee_tests dsp_systems_tests dsp_effects_tests dsp_primitives_tests dsp_processors_tests gradus_tests`. Run `tools/macos-fastmath-repro.sh`.

**Acceptance:** all SECTIONs pass under IEEE flags.

### T2.2 — Old or truncated states load the same values on every path (R-075, R-096, R-074)

**Flags:** none (no layout change; controller gains `else` branches). **Estimate:** 120 min. **Prerequisites:** T1.22, T1.26.

1. **Failing test:** `ruinae/tests/integration/controller_state_test.cpp`: `TEST_CASE("Legacy state loads identical values via setState and via the browser path", "[state][migration][controller]")`: build genuine v5 and v7 blobs (strip the gated packs the way `flanger_params_test.cpp:214-298` does, AND strip the bytes the older format never wrote); pre-load a state with non-default flanger, chorus, arp lane speeds 2×, chord lane data; then load the legacy blob (a) with `Processor::setState` + `Controller::setComponentState`, (b) with `loadComponentStateWithNotify` into a connected pair. REQUIRE for both: flanger/chorus parameters equal their registration defaults, arp lane speeds == 1.0, chord/inversion lanes at defaults, and processor `getState` bytes identical between (a) and (b).
2. **Fix:** `controller_presets.cpp:477-485`: add `else` branches calling `synthSetter` with the registered default of every flanger (v < 6) and chorus (v < 7) parameter; take the defaults from one shared constant table used by `processor_state.cpp:231-259` too (replace the literals there). `:473`: a missing modulation-type byte defaults to Phaser like `processor_state.cpp:227`. Trance gate: `trance_gate_params.h:418-428` controller loader mirrors the processor resets at 349-364. Arp: `arpeggiator_params.h` `loadArpParams` (1250-1462) and `loadArpParamsToController` (1677-1728): at entry reset every field behind a version gate (midiOut v2, chord/inversion/voicing v4, lane speeds v8) and every field after an EOF early-return to its registered default before reading.

**Acceptance:** case passes for v5 and v7.

### T2.3 — Flanger/chorus value text and the degree sign (R-084, R-104)

**Flags:** macOS-SENSITIVE (char signedness). **Estimate:** 45 min.

1. **Failing test:** `ruinae/tests/unit/controller_display_test.cpp`: `TEST_CASE("Flanger and chorus parameters use their own formatters", "[controller][display]")`: `getParamStringByValue(kFlangerRateId, 0.5)` ends in "Hz"; `kChorusFeedbackId` at 0.5 reads "0%" (or "+0%"); `kPhaserStereoSpreadId` at 0.25 equals `u"90°"` exactly (compare char16 code units: 0x39 0x30 0x00B0); the parameter info units of the three Spread parameters equal `u"°"`.
2. **Fix:** `ruinae/src/controller/controller_param_display.cpp:88-93`: test the flanger and chorus ranges before the phaser range. `phaser_params.h:109,158-159` and `modulation_effect_params.h:178,227`: units `STR16("°")`; build the value as ASCII digits with `fromAscii` and then append the single code unit `u'°'` to the `String128`.

**Acceptance:** case passes.

### T2.4 — Mono Priority labels follow the enum (R-181)

**Estimate:** 30 min.

1. **Failing test:** `controller_params_test.cpp`: `TEST_CASE("Mono Priority list entries name the MonoMode at the same index", "[controller][params]")`: entry 1 contains "Low", entry 2 contains "High". `processor_audio_test.cpp`: `TEST_CASE("Mono priority index 1 plays the lower note", "[integration][mono]")`: `kMonoPriorityId` = 0.5, hold C3 and C4, measured pitch = C3.
2. **Fix:** `ruinae/src/parameters/mono_mode_params.h:48-51` → `{Last, Low, High}`; comments at `:17` and `plugin_ids.h:669`.

### T2.5 — Mixer Spectral Shift reaches the spectral morph filter (R-182)

**Flags:** STATE-COMPAT (semantic; version 9: older states stored 0.0 for a knob shown at 50 %), PRESET-IMPACT: none if the gate is applied (generator writes 0.5). **Estimate:** 90 min. **Prerequisites:** T1.26.

1. **Failing test:** `ruinae_engine_test.cpp`: `TEST_CASE("Mixer spectral shift moves the spectrum in SpectralMorph mode", "[ruinae-engine][mixer]")`: shift 0.25 vs 0.75 → spectral centroids differ; 0.5 equals no shift. `state_migration_test.cpp`: `TEST_CASE("State v8 loads spectral shift as 0.5", "[state][migration]")`.
2. **Fix:** `RuinaeVoice::setMixShift(float normalized)` → `spectralMorph_->setSpectralShift((n*2−1)*24)` (`spectral_morph_filter.h:429`); engine forward next to `setMixTilt` (`ruinae_engine.h:1067-1080`); `processor_params.cpp:305-309` forwards `mixerParams_.shift`; `mixer_params.h:25` atomic `shift{0.5f}`, formatter in semitones; version-9 gate on both load paths forces 0.5 for `version < 9`; generator writes 0.5.

### T2.6 — Per-voice Morph Position works in SpectralMorph mode (R-023)

**Estimate:** 45 min. **Prerequisites:** T1.24.

1. **Failing test:** `ruinae_voice_test.cpp`: `TEST_CASE("RuinaeVoice: per-voice morph route is audible in SpectralMorph mode", "[ruinae-voice][spectral-morph][modmatrix]")`: route Env3 → MorphPosition +1, mix position 0, A and B different waveforms; REQUIRE output differs from the unrouted render after the latency.
2. **Fix:** `ruinae_voice.h:397-429`: before `spectralMorph_->processBlock`, call `spectralMorph_->setMorphAmount(clamp(mixPosition_ + previous-block MorphPosition offset, 0, 1))`, next to the OscLevel code at 408-415.

### T2.7 — Formant Vowel is not overridden by Morph (R-022)

**Flags:** CROSS-PLUGIN IMPACT (`oscillator_adapters.h`; consumers: `ruinae_voice.h`, `selectable_oscillator.h`, `selectable_oscillator_test.cpp`), STATE-COMPAT, PRESET-IMPACT (factory presets re-authored to keep their sound). **Estimate:** 120 min.

**Ruling (user, 2026-11-18): keep the current sound.** In addition to steps 1–2 below:
- **STATE-COMPAT:** on both load paths (Processor `setState` / `applyPresetSnapshot` and Controller `loadStateCore`), a state with version < 9 loads each Formant Vowel (OSC A and OSC B) as A (0) and keeps its Morph. `kCurrentStateVersion` is already 9, so no bump is needed. Add a migration case to `state_migration_test.cpp`: a version-8 blob with vowel O and morph 2.2 loads as vowel A and morph 2.2 on both the processor and the controller.
- **Generator** (`tools/ruinae_preset_generator.cpp`): rewrite every Formant oscillator (`type = 7`) so vowel + morph equals the position it plays today (that position is today's morph): vowel = floor(position), morph = position − vowel. Vowel-A presets are already correct. Exceptions that take the new sound, matching their comments: Vowel Descent oscB stays E/0 (plays pure E), and Vocal Bass oscA stays O/0.6 (plays 3.6). Regenerate the factory `.vstpreset` files. Add a test that loads each re-authored factory preset and checks that vowel + morph equals the expected position (the list of 15 Formant presets is in the T2.7 blocker notes: Vowel Descent, Vox Oracle, Lydian Aurora, Choir, Vox Automata, Formant Sea, Crystal Choir, Vox Machina, Vocal Bass, Vowel Ghost, Vowel Cloud, Glossolalia, Arp Morph Sequence, Macro Performer, Swing Vox).
- CHANGELOG/MANUAL: the Vowel knob now works and Morph moves away from it; old projects sound the same.

1. **Failing test:** `engine_osc_param_test.cpp`: `TEST_CASE("Formant vowel survives the per-block parameter fan-out", "[vst][osc-params]")`: through the PROCESSOR (parameter changes, not direct engine setters): vowel O, morph default; REQUIRE the render differs from vowel A and the strongest formant peak sits within 15 % of vowel O's F1.
2. **Fix:** adapter (`oscillator_adapters.h:325-334`): store vowel and morph; apply `setMorphPosition(vowelIndex + morph)` clamped to [0, 4] (morph is an offset from the selected vowel), in one place, only when either changed.

### T2.8 — Spectral distortion mix blends time-aligned dry (R-024)

**Estimate:** 60 min.

1. **Failing test:** `ruinae_voice_test.cpp`: `TEST_CASE("RuinaeVoice: spectral distortion at mix 0.5 has no comb", "[ruinae-voice][distortion]")`: white-noise-like input (Noise osc), Spectral distortion drive 0, mix 0.5; REQUIRE the long-term magnitude response relative to mix 0 is flat within ±1 dB between 200 Hz and 8 kHz (baseline: notches every 86 Hz).
2. **Fix:** `ruinae_voice.h:598-608`: for `SpectralDistortion`, delay the dry copy by `distSpectral_->latency()` through a delay line pre-allocated in `prepare` before the blend.

### T2.9 — Distortion types are usable at drive 0 (R-019)

**Flags:** PRESET-IMPACT (audio; per the ruling only the low end of the drive law changes). **Ruling required.** **Estimate:** 90 min.

1. **Failing test:** `ruinae_voice_test.cpp`: `TEST_CASE("RuinaeVoice: every distortion type passes signal at drive 0", "[ruinae-voice][distortion]")`: 220 Hz sine, mix 1, for each `RuinaeDistortionType`: steady-state RMS within ±6 dB of Clean at drive 0 (baseline Wavefolder and RingMod 0.0000, Tape −24 dB, Granular +8.5 dB), and continuity: RMS at drive 0.0 vs 0.001 within 0.5 dB.
2. **Fix:** `ruinae_voice.h:1533-1555` (and the duplicate mapping at 1489-1507): Wavefolder `foldAmount = 1 + drive*9`; RingMod: amplitude 1, drive crossfades dry → ring (`out = in*(1−drive) + ring*drive`); Tape: `−24 + drive*48` becomes `drive*24` dB for drive < 0.5 blended into the old law above (document the piecewise curve); Granular: divide by the measured overlap gain so drive 0 is unity. Keep each type's behaviour at drive ≥ 0.5 within 1 dB of today (assert in the test).

### T2.10 — Effects-chain crossfades can be re-targeted (R-061, R-059, R-063)

**Estimate:** 120 min. **Prerequisites:** T1.4, T1.19.

1. **Failing tests** (`ruinae_effects_chain_test.cpp`): `ModSlot_RetargetMidFade_NoClick` `[ruinae-fx][modulation]`: Phaser active, `startModCrossfade(Chorus)`, one 512 block later `startModCrossfade(Phaser)`; REQUIRE max per-sample step < 2× the dry sine's step (baseline 13×); SECTION A→B→C. `ReverbFreeze_TypeSwitch_KeepsFrozenTail` `[ruinae-fx][reverb]`: build a tail, freeze, switch type, REQUIRE RMS after 0.5 s ≥ 0.5× the frozen RMS (baseline 0.0); fix the vacuous `if (rmsAfterCrossfade > 1e-6f)` at `reverb_type_test.cpp:624`. `Harmonizer_ToggleDuringFade_NoStep` `[ruinae-fx][harmonizer]`: enable, disable 2 ms later; max step ≤ 1.5× steady.
2. **Fix (`ruinae_effects_chain.h`):** `startModCrossfade` (343-354): if crossfading and `incoming == activeModType_`, swap active/incoming and set `alpha = 1 − alpha`; if a third type, complete the running fade first (as `setDelayType` 410-413), then start. `setReverbType` (689-701): while `freeze` is on, store `pendingReverbType_` and apply it when freeze is released (document in MANUAL.md). `setHarmonizerEnabled` (738-754): on disable keep the current alpha; on enable during FadingOut switch to FadingIn without touching alpha; reset the harmonizer when a fade-out reaches Off (1050-1054).

### T2.11 — Chorus voices are offset in LFO phase, not amplitude (R-060)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/processors/chorus.h`), PRESET-IMPACT (audio: Chorus presets lose a click, depth unchanged). **Estimate:** 90 min.

1. **Failing test:** `dsp/tests/unit/processors/chorus_test.cpp`: `TEST_CASE("Chorus multi-voice delay modulation is continuous", "[processors][chorus]")`: 440 Hz 0.3 sine, rate 1 Hz, depth 0.5, mix 1, voices 2 and 3; REQUIRE max per-sample step ≤ 1.5× the voices = 1 result (baseline 0.2235 vs 0.0194).
2. **Fix:** `chorus.h:308-320, 338-349`: compute each voice's modulation from the LFO PHASE plus `v/voices` (evaluate the waveform at the offset phase: add an LFO method that returns the value at `phase + offset`, or keep one LFO per voice with a phase offset, pre-allocated for the max voice count).

### T2.12 — Spectral delay FFT Size takes effect safely; registered default matches (R-058, R-187)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/effects/spectral_delay.h`: Iterum). **Estimate:** 240 min. **Prerequisites:** T1.18, T1.20.

1. **Failing tests:** `dsp/tests/unit/effects/spectral_delay_test.cpp`: `TEST_CASE("SpectralDelay FFT size change is applied consistently", "[effects][spectral-delay]")`: prepare at 1024, request 4096, process; REQUIRE `getLatencySamples() == 4096`, a 1 kHz tone at a fractional-frame delay shows no smearing vs a delay prepared at 4096 (RMS within 1 dB, spectral peak width equal), and `allocation_detector.h` records no allocation in `process`. `controller_params_test.cpp`: registered default of `kDelaySpectralFFTSizeId` denormalises to index 1.
2. **Fix:** `spectral_delay.h`: `prepare` allocates STFT/OLA/spectra/bin-delay storage for all four sizes (512–4096) once; `setFFTSize` (470-477) stores a pending size; `process` applies a pending size at the next block start by switching the active set and resetting it (no allocation), updating `hopSize_`, frame rate and `getLatencySamples`. `ruinae_effects_chain.h:613-617`: unchanged call; route the switch through the existing delay crossfade when Spectral is active to avoid a click. `delay_params.h:467-469`: `createDropdownParameterWithDefault(..., 1, …)`.

### T2.13 — Delay time is honoured or visibly limited per type (R-094)

**Flags:** CROSS-PLUGIN IMPACT if a DSP max is raised (`tape_delay.h:178`, `granular_engine.h:113-114`, `spectral_delay.h:93`). **Estimate:** 90 min.

1. **Failing test:** `ruinae_effects_chain_test.cpp`: `TEST_CASE("Delay time 3000 ms is honoured by every delay type", "[ruinae-fx][delay][timing]")`: per type, impulse, time 3000 ms, REQUIRE first echo at 3000 ms ± 2 ms (± 1 hop for Spectral).
2. **Fix:** raise the three DSP maxima to 5000 ms where the buffers are sized from the prepare argument (Tape: `kMaxDelayMs`; Granular: `kMaxDelaySeconds` 2 → 5 and the `setPosition` clamp; Spectral: `kMaxDelayMs`). Check memory growth (granular buffer ×2.5) and the T2.33 reset cost. If a type cannot be raised, clamp in `setDelayTime` (`ruinae_effects_chain.h:449-456`) per type and make the display formatter show the clamped value; then the test expects the clamp.

### T2.14 — Sidechain is used only when its bus is active (R-050)

**Estimate:** 60 min.

1. **Failing test:** `mod_source_pipeline_test.cpp`: `TEST_CASE("Inactive sidechain bus with buffers does not replace self-analysis", "[integration][sidechain]")`: aux bus not activated, `numInputs = 1` with valid zero buffers, Transient → Master Volume route, note-on; REQUIRE the Transient offset > 0 after the note and `kSidechainActiveId` output == 0. SECTION: after `activateBus(kAudio, kInput, 0, true)` the sidechain is used.
2. **Fix:** `processor.cpp:471-489`: gate on the bus state (`getAudioInput(0)->isActive()` read into an atomic in an `activateBus` override) and on `!(scBus.silenceFlags & 1)` being optional (do not treat silence as inactive).

### T2.15 — Arp Rate modulation stays on the tempo grid (R-049)

**Flags:** GRADUS IMPACT (additive `setRateScale`), macOS-SENSITIVE. **Estimate:** 120 min. **Prerequisites:** T1.31.

1. **Failing test:** `arp_mod_integration_test.cpp`: `TEST_CASE("ArpRate modulation in sync mode scales the step and keeps grid lock", "[arp][modmatrix]")`: 1/1 at 60 BPM, ArpRate offset 0.001 → step duration within 0.1 % of 4 s (baseline 2 s); 1/16 with offset +1 → step = base/1.5 within 2 samples and onsets stay phase-locked to ppq after 8 bars.
2. **Fix:** `arpeggiator_core.h`: add `setRateScale(float)` (default 1, clamp [0.25, 4], non-finite ignored) applied inside `calculateStepDuration` (1086-1112) and honoured by `syncToMusicalPosition`. `processor_params.cpp:885-908`: in sync mode call `setRateScale(1 + 0.5*offset)` instead of `setTempoSync(false)` + `setFreeRate`.

### T2.16 — Global LFO rate reaches the displayed 50 Hz (R-048)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/primitives/lfo.h`, additive). **Estimate:** 60 min.

1. **Failing test:** `mod_source_pipeline_test.cpp`: `TEST_CASE("LFO1 rate at normalized 1.0 runs at 50 Hz", "[integration][lfo]")`: measure the LFO period from the routed offset sampled per 64-sample sub-block at 96 kHz; REQUIRE 50 Hz ± 2 %. SECTION sync 1/64 at 120 BPM → 32 Hz.
2. **Fix:** `lfo.h:35, 213-218, 517`: add `setMaxFrequency(float hz)` (default 20, existing consumers unchanged) used by both clamps; `ModulationEngine::prepare` in Ruinae's use sets 50 through a new `setLfoMaxFrequency` on the engine (`ruinae_engine.h` prepare, ~148).

### T2.17 — Master-volume and global-filter modulation are ramped inside a sub-block (R-047)

**Estimate:** 90 min.

1. **Failing test:** `ruinae_engine_test.cpp`: `TEST_CASE("RuinaeEngine: global gain modulation has no sub-block zipper", "[ruinae-engine][modulation]")`: LFO1 10 Hz → Master Volume amount 0.5 on a sustained 1 kHz sine at 44.1 kHz; REQUIRE no spectral line within ±20 Hz of 1000 ± 689 Hz above −60 dB re the fundamental (baseline ≈ −25 to −43 dB).
2. **Fix:** `ruinae_engine.h:885-894, 965-973`: keep `prevMasterGain_`; ramp linearly from it to `sub.masterGain` across the sub-block. `:151-158`: `globalFilterL_/R_.enableSmoothing(true)` (or the SVF's equivalent; read `svf.h:750` and the setter name).

### T2.18 — A route whose source or destination changed starts from its own amount (R-046)

**Flags:** CROSS-PLUGIN IMPACT (`modulation_engine.h`). **Estimate:** 45 min.

1. **Failing test:** `dsp/tests/unit/systems/modulation_engine_test.cpp`: `TEST_CASE("setRouting snaps the amount when source or destination changes", "[systems][modulation]")`: slot 0 LFO→dest A amount 1, process; `setRouting` Macro→dest B amount 0.1; the first block's dest-B offset ≤ 0.1.
2. **Fix:** `modulation_engine.h:355-374`: when `source`, `destParamId` or `curve` differ from the stored routing, `amountSmoothers_[i].snapTo(amount)` and reset `signalSmoothers_[i]` to 0.

### T2.19 — Voice-route Curve, Smooth and scales behave as shown (R-045, R-044, R-053)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/systems/voice_mod_types.h`, `voice_mod_router.h`), STATE-COMPAT (semantic, version 9, resonance amounts; per ruling), PRESET-IMPACT (audio: "Velocity Canvas" and bass presets whose voice routes carry Exp curves or smoothing start using them). **Ruling required.** R-053 is PLAUSIBLE: verify first. **Estimate:** 180 min. **Prerequisites:** T1.24, T1.26.

1. **Failing tests:** `voice_mod_filter_test.cpp`: `TEST_CASE("Voice route curve and smoothing are applied", "[integration][modmatrix][voice]")`: Velocity → FilterCutoff, curve Exponential, velocity 0.5 → cutoff offset 0.25·48 st ± 0.5 (baseline 24 st); smooth 50 ms on a Gate source → 10–90 % rise ≈ 50 ms ± 20 %. `TEST_CASE("Voice and global resonance routes have the same full scale", "[integration][modmatrix]")`: amount 1 → ±10 Q on both. R-053 verify-first: `TEST_CASE("Scale x4 keeps amount resolution", "[modmatrix]")`: ×4 at amount 0.5 vs 1.0 give different offsets; if this already passes, mark R-053 refuted and stop that part.
2. **Fix:** `voice_mod_types.h:73-77`: add `ModCurve curve` and `float smoothMs` to `VoiceModRoute`; `voice_mod_router.h:172-187`: apply `applyBipolarModulation(curve, source, amount)` and a per-route one-pole smoother (pre-allocated array of 16). `processor_params.cpp:516-535`: forward `r.curve`, `r.smoothMs`; line 506 scale 10.0; R-053: apply the ×2/×4 multiplier to the destination's full scale (`setVoiceModRouteScale` per route) instead of to the clamped amount, and the same in `ruinae_engine.h:469` for global routes (scale the contribution after the amount clamp; the per-destination ±1 clamp in `modulation_engine.h:811-816` still bounds the sum). Version gate: `version < 9` voice routes to FilterResonance have their amount divided by 10.

### T2.20 — Voice LFO has rate and shape (R-093)

**Flags:** STATE-COMPAT (two appended parameters, version 9). **Ruling required.** **Estimate:** 120 min. **Prerequisites:** T1.27 (append position).

1. **Failing test:** `ruinae_voice_test.cpp`: `TEST_CASE("RuinaeVoice: voice LFO rate is configurable", "[ruinae-voice][lfo]")`: rate 5 Hz, route Voice LFO → FilterCutoff; cutoff modulation period 200 ms ± 2 %. Migration case: v8 → 1 Hz sine.
2. **Fix:** IDs in a free block (read `plugin_ids.h`; e.g. the per-voice section), atomics, registration, handler, formatter; engine `setVoiceLfoRate/Shape` → `voice.getVoiceLFO().setFrequency/setWaveform`; append to the state after T1.27's field; generator writes defaults; two controls on the MOD tab voice section (XSLT).

### T2.21 — noteId bookkeeping addresses the note that is sounding (R-035, R-168, R-167, R-036)

**Estimate:** 150 min. **Prerequisites:** T1.9.

1. **Failing tests** (`ruinae/tests/integration/mpe_test.cpp`, tags `[integration][mpe][noteid]`): `NoteId_SupersededNoteOff_DoesNotCutTheNewNote`: `noteOn(60, id1)`, `noteOn(60, id2)`, `noteOff(60, id1)`, render 500 ms → still gated (RMS ≥ 50 % of sustain). `NoteId_MonoLegato_ExpressionFollowsTheSoundingNote`: mono + legato, on(48,id1), on(52,id2), tuning +1 st on id2, off(52,id2) → pitch = C3 within 1 cent, and a tuning expression on id1 changes the pitch. `NoteId_TinyVelocity_IsANoteOn`: velocity 0.003 with id2 while 60/id1 is held; `noteOff(64,id2)` → the 60 voice is not in release; after `noteOff(60,id1)` and the release time the allocator's active count is 0. `NoteExpression_IgnoredTypes_DoNotSplitSegments`: 500 pan (typeId 1) expressions in one buffer → exactly one engine `processBlock` call (add a test counter).
2. **Fix:** `processor_midi.cpp:160, 203`: `midiVelocity = clamp(lround(velocity*127), 1, 127)` for velocity > 0. `ruinae_engine.h:272-293`: `dispatchPolyNoteOn`/`dispatchMonoNoteOn` return the voice index that received the NoteOn (or −1); stamp only that index. `:296-313`: when `noteId >= 0` and no voice carries it, return (no pitch fallback); when found, release the allocator slot by voice index (`allocator_.noteOff(allocator_.getVoiceNote(i))` or a new `releaseVoice(i)`). Mono: keep a 128-entry `monoNoteIds_` side table; on a return-to-held-note (`dispatchMonoNoteOff` 1815-1827) set voice 0's noteId to the winner's id and reset expression to neutral. `processor_midi.cpp:238-242`: queue only typeIds 0, 2, 5.

### T2.22 — Steal mode and mono retrigger do what they say (R-034, R-037)

**Estimate:** 120 min. **Prerequisites:** T1.9.

1. **Failing tests** (`ruinae_engine_test.cpp`): `RuinaeEngine_StealModes_Differ` `[ruinae-engine][steal]`: polyphony 1, note A held, note B; Hard: output dips below 5 % of A's level within 3 ms before B's attack; Soft: no dip, and the outputs of the two modes differ. `RuinaeEngine_MonoReturnRetriggersWhenLegatoOff` `[ruinae-engine][mono]`: legato off, sustain 0, decay 50 ms; hold 60, play and release 64 → the amp envelope re-attacks (RMS rises again).
2. **Fix:** `ruinae_engine.h:1748-1752`: `Steal` → start a 2 ms linear fade on the victim (new per-voice `stealFade_` applied in the mix loop), then `reset()` + the queued NoteOn at fade end (store the pending note); `NoteOff` (Soft) keeps today's retrigger-from-current-level. `:1815-1827`: if `monoEvent.retrigger`, `voices_[0].noteOn(freq, mappedVelocity)`; drop the stray `processPortamento()` call. Correct MANUAL.md:1060.

### T2.23 — The mono voice stays centred (R-012)

**Estimate:** 30 min.

1. **Failing test:** `ruinae_engine_test.cpp`: `TEST_CASE("RuinaeEngine: mono voice is centred at any stereo spread", "[ruinae-engine][mono][pan]")`: Mono, spread 1, one note → |L RMS − R RMS| < 0.01 dB.
2. **Fix:** `ruinae_engine.h:2104-2106`: use pan 0.5 for voice 0 in `processBlockMono`.

### T2.24 — Filter stage: type switches crossfade, SVF drive is continuous, comb level is compensated (R-152, R-014, R-156)

**Flags:** PRESET-IMPACT (audio: Comb presets at high resonance get quieter; SVF presets with drive > 0 on hot signals shift slightly). **Ruling required for the comb part.** **Estimate:** 180 min. **Prerequisites:** T1.33.

1. **Failing tests** (`ruinae_voice_test.cpp`, `[ruinae-voice][filter]`): `FilterTypeSwitch_NoClick`: saw A3 held, step the type at a block boundary for every ordered pair of {SVF LP, Ladder, Comb, SelfOscillating, Formant}; jump ratio ≤ 1.5 (baseline 3.55); replace the assertion-free AS-4.4 case at 543-585. `SvfDrive_ContinuousAtZero`: SVF LP Q 20, drive 0 vs 0.05 dB → RMS within 0.5 dB. `CombResonance_LevelBounded`: comb res 1.0 sweep, limiter off → peak ≤ +6 dBFS (baseline +22).
2. **Fix:** `ruinae_voice.h:748-763`: keep `prevFilterType_` and a 10 ms crossfade counter; during the fade process both filters (incoming reset first) and blend. `:1462-1463`: `mix = min(1, driveDb / 3)`; `out = out*(1−mix) + tanh(out*g)*mix` (the suggested `tanh(g x)/g` does not remove the cliff). `:1667-1671` / `:1470-1471`: multiply the comb output by `1 − max(0, feedback − 0.5)·k` with `k` chosen so res 1.0 peaks ≤ +6 dBFS (measure; document the curve).

### T2.25 — ChaosWaveshaper Lorenz stays chaotic at every sample rate (R-009)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/primitives/chaos_waveshaper.h`), PRESET-IMPACT (audio: Chaos Waveshaper presets, generator `:3646, :8865, :7977`). **Estimate:** 90 min.

1. **Failing test:** `dsp/tests/unit/primitives/chaos_waveshaper_test.cpp`: `TEST_CASE("ChaosWaveshaper Lorenz does not fall into a reset loop", "[primitives][chaos]")`: speed 1 and 10, 44.1 / 48 / 96 kHz, DC 0.5 input, 2 s; REQUIRE no exact periodicity below 1 s (autocorrelation peak < 0.99 for lags < 1 s; baseline exact period 1216 samples) and output-range statistics within 20 % across rates.
2. **Fix:** `chaos_waveshaper.h:677-691, 744-761`: sub-step the integration so each Euler step has `dt ≤ 0.002` (loop `ceil(dt/0.002)` times, bounded by a small constant max), and compare against `safeBound_ * 10` as `chaos_mod_source.h:380-382` does. Same sub-stepping for Rössler and Chua.

### T2.26 — Host contract: context requirements, tail, silence flags, tempo, bus arrangement (R-189, R-190, R-105, R-137, R-192)

**Flags:** R-192 is PLAUSIBLE (verify first: the test below documents the current rejection; apply the fix only if the user wants mono sidechain accepted; default = accept, matching Innexus). **Estimate:** 120 min. **Prerequisites:** T1.18.

1. **Failing tests:** new `ruinae/tests/unit/processor_host_contract_test.cpp`, tags `[processor][host-contract]`: `Processor_DeclaresProcessContextRequirements` (query `IProcessContextRequirements`; tempo, time signature, project-time-music and transport-state bits set); `Processor_TailSamples` (`kInfiniteTail` with reverb Freeze on or delay enabled with feedback ≥ 1.0; otherwise ≥ amp release in samples); `Processor_WritesSilenceFlags` (preset `silenceFlags = 3`, play a note → 0 after `process`; no note and FX idle → 3); `Processor_TempoDropout_UsesLastValidTempo` (alternate blocks with and without `kTempoValid` at 90 BPM → trance-gate step length stays the 90 BPM value). `processor_bus_test.cpp`: `Accepts mono sidechain + stereo output`, `Rejects mono output`; fix the header comment (line 5).
2. **Fix:** `processor.cpp:48-65` `initialize`: `processContextRequirements.needTempo().needTimeSignature().needProjectTimeMusic().needTransportState();`. `processor.h:140-150` + `.cpp`: `getTailSamples()` override. `processor.cpp:436-509`: write `data.outputs[0].silenceFlags` on every return path (0 after rendering unless the block is all zeros and no voice/FX tail is active). `:254-263`: seed `blockCtx.tempoBPM` from `tempoBPM_`. `:584-600`: accept `kMono` or `kStereo` for the aux input and forward to `AudioEffect::setBusArrangements`.

### T2.27 — UI-only switch parameters are hidden; view callbacks cannot dereference null; ADSR displays drop processor pointers at terminate (R-086, R-089)

**Estimate:** 90 min. **Prerequisites:** T1.3.

1. **Failing tests:** `controller_params_test.cpp`: `TEST_CASE("UI-only view parameters are hidden and not automatable", "[controller][params]")` for `kMainTabTag`, `kModSourceViewModeTag`, `kFilterViewModeTag`, `kDistortionViewModeTag`. `view_switch_lifecycle_test.cpp`: after a host-driven tab change, invoke each lane transform callback and the voice route-removed callback → no crash (ASan clean). `editor_lifecycle_test.cpp`: `TEST_CASE("terminate with editor open revokes ADSR playback pointers", "[controller][lifecycle]")` using `terminate_with_editor_open_probe.h`: after `terminate()` each ADSR display's playback pointers are null and its timer stopped.
2. **Fix:** `controller.cpp:166-192` and `distortion_params.h:240-245` (+ the filter view mode registration): flags `kIsHidden` without `kCanAutomate`. `controller_verify_view.cpp:480-566`: the four transform lambdas capture the lane pointer (as `wireBarLaneTransform` 453-473 does). `controller_mod_matrix.cpp:165`: null-check `modMatrixGrid_`. `controller.cpp:251-269` `terminate`: call `setPlaybackStatePointers(nullptr, nullptr, nullptr)` on the three ADSR displays; `plugins/shared/src/ui/adsr_display.h:371-385`: a null set stops `playbackTimer_` (CROSS-PLUGIN IMPACT: shared UI; add `shared_tests`).

### T2.28 — All eight arp lanes show a playhead whenever the arp runs (R-085, R-144)

**Estimate:** 75 min. **Prerequisites:** T1.32.

1. **Failing tests:** `arp_integration_test.cpp`: `TEST_CASE("ArpPlayhead_AllEightLanesWritten", "[arp][integration][ui]")`: lanes of different lengths; REQUIRE `outputParameterChanges` contains all eight playhead IDs with steps that differ where lengths differ; arp Off → sentinel 1.0 on all eight. `arp_controller_test.cpp`: `TEST_CASE("Lane playheads follow the parameter, not the host transport", "[controller][arp]")`: transport flag false, playhead parameter set → lane shows it; parameter at the sentinel → `setPlayheadStep(-1)` called.
2. **Fix:** `processor.cpp:393-434`: write all eight IDs from `arpCore_.<lane>Lane().currentStep()` (mirror `plugins/gradus/src/processor/processor.cpp:358-372`). `controller.cpp:501-556`: poll unconditionally; decode the sentinel as −1 and clear the lane.

### T2.29 — Missing arp controls and hidden preset categories (R-088, R-087)

**Estimate:** 90 min.

1. **Failing tests:** `preset_browser_test.cpp`: `TEST_CASE("Every factory preset folder maps to a browser tab", "[controller][presets]")`: enumerate `ruinae/resources/presets/*/`; every folder name is in `makeRuinaePresetConfig().subcategoryNames` and in one of the two tab-label lists. A uidesc check (extend the existing uidesc reachability test using `uidesc_reachability.h`): `kArpAccentVelocityId`, `kArpSlideTimeId`, `kArpVoicingModeId` have a control-tag and a control.
2. **Fix:** `ruinae/src/preset/ruinae_preset_config.h:22-45`: add "Arp Modulation" (subcategory + arp tab labels) and "Modulation" (subcategory + main tab labels). `editor.uidesc` (XSLT workflow): control-tags `ArpAccentVelocity` 3180, `ArpSlideTime` 3181, `ArpVoicing` 3370 and three controls in the Tab_Seq arp section.

### T2.30 — Harmonizer transitions are ramped (R-176, R-178, R-179, R-180)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/systems/harmonizer_engine.h`, `pitch_shift_processor.h`). **Estimate:** 210 min. **Prerequisites:** T1.19.

1. **Failing tests** (`dsp/tests/unit/systems/harmonizer_engine_test.cpp`, `[systems][harmonizer][click]`; measure the step ACROSS the change, not only after it): `ModeSwitch_NoClick_NoStaleAudio` (sustained sine, +7, switch each mode pair mid-render: max |y[n]−y[n−1]| within 1.5× steady state including the boundary sample; after PV→Simple→PV the first PV output is uncorrelated with a distinct earlier input segment); `VoiceMute_And_CountChange_AreRamped` (level 0 → −60 dB and voices 4 → 3, 2 → 1: step bound holds, remaining voices' level changes over ≥ 5 ms); `Unmute_StartsAtTheInterval` (mute 0.5 s, unmute: instantaneous frequency within 10 cents of +7 st in the first 20 ms); `VoiceDelay_Sweep_NoZipper` (0 → 50 ms over one block: step bound holds; 0 → 20 ms never plays audio older than 20 ms). Fix the vacuous case at 4775-4839.
2. **Fix (`harmonizer_engine.h`):** `setPitchShiftMode` (616-623): fade wet out over 10 ms, switch + reset shifters, `sharedStft_`, `sharedAnalysisSpectrum_`, `sharedPitchDetector_`, snap each `PitchShiftProcessor` semitone smoother to target, re-arm the per-voice fade-in. Muting (243-246, 383-386, 466-469): base "muted" on the smoothed level (< 1e-4), not on `linearGain`. `setNumVoices` (576-598): removed voices fade out before being dropped. `voiceCountScale` (547-555): smooth over 10 ms. `advanceMutedVoiceSmoothers` (789-797): advance toward the voice's real pitch target. `setVoiceDelay` (681-687): per-voice delay-time smoother; always write the delay line.

### T2.31 — Output does not depend on host buffer size (R-150, R-151, R-051, R-052)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/effects/digital_delay.h`, `tape_delay.h`, `dsp/systems/feedback_network.h`). **Estimate:** 240 min. **Prerequisites:** T1.4, T1.10, T1.11, T1.18.

1. **Failing test:** promote `AuditProbe block size invariance` and `AuditProbe2 block invariance isolation` (`audit_probes.cpp:1263, 2316`) into `ruinae/tests/integration/block_size_invariance_test.cpp`: `TEST_CASE("Render is block-size invariant", "[integration][block_size]")`, SECTIONs: dry engine with bends; LFO1 → global + all-voice cutoff; mono legato portamento; each delay type steady state (type set BEFORE the first block via `setState`, so the type-switch transient is not what is measured); Plate; Hall (via state); Env Follower → Global Cutoff with no sidechain; block sizes 32 / 100 / 441 / 2048 vs 512; REQUIRE max |diff| ≤ 1e-5 for the sample-domain paths and RMS diff ≤ −60 dB for Spectral/Granular. Baselines: 0.136 (mod, bs32), 1.87 (mono), 0.59–0.89 (Digital), 0.15–0.51 (Hall).
2. **Fix:** (a) mod grid `ruinae_engine.h:846-912`: evaluate `globalModEngine_` only when a 64-sample cell completes; partial pieces render with the held offsets (advance sources by the accumulated cell length). (b) mono portamento chunk (2108-2117): count the 32-sample chunk grid across calls like `modGridPhase_`. (c) `digital_delay.h:614-641`: process the `FeedbackNetwork` per sample or in fixed 16-sample chunks counted across calls, feeding the per-chunk delay time; same for `tape_delay.h:515-536` feedback; limiter stereo-linked per sample (717-718). (d) `fdn_reverb.h:385-413`: keep the 16-sample grid phase across calls. (e) `previousOutputL_/R_` (833-852, 997-1000): replace with a ring buffer written per sample and read at a fixed delay of `kModSubBlock`. (f) R-052: `processor.cpp:466-469`: write 0 to External0 when `!isArpModSource`; clear `lastArpPitch_` on arp reset.

### T2.32 — Arp input timing (R-072, R-073, R-071)

**Flags:** GRADUS IMPACT for the R-071 core change; R-072 and R-073 are PLAUSIBLE: verify first, and R-073 needs the ruling. PRESET-IMPACT: goldens must not change (the fixture sequences start with transport playing and block-aligned keys; if a golden moves, stop). **Estimate:** 180 min. **Prerequisites:** T1.6, T1.31.

1. **Failing tests:** `dsp/tests/unit/processors/arpeggiator_core_core_test.cpp`: `TEST_CASE("First arp step after an idle gap fires in the block of the key press", "[processors][arp]")`: free rate 2 Hz, release mid-step, idle, press → NoteOn within that block (baseline 6690 samples late). `arp_integration_test.cpp`: `ArpInput_TapInsideOneBlock_StillPlays` (2048 block, on at 100, off at 600 → one arp NoteOn at offset ≥ 100) — R-072; `ArpFreeRate_RunsWithTransportStopped` (kPlaying once then cleared, free rate 8 Hz, held key → NoteOns) — R-073.
2. **Fix:** R-071 `arpeggiator_core.cpp:108-121`: when the held buffer goes empty → non-empty and tempo sync is not driving the counter, set `sampleCounter_ = currentStepDuration_`. R-072 `processor.cpp:282-330`: split `arpCore_.processBlock` at key-event offsets (apply `arpCore_.noteOn/Off` between sub-calls, offset the returned events), reusing the timed-event queue order. R-073 `processor.cpp:298-310`: `arpCtx.isPlaying = true` whenever the arp is in free-rate mode.

### T2.33 — Engine reset cost is bounded per block (R-004)

**Flags:** CROSS-PLUGIN IMPACT if `DelayLine` gains an incremental clear. **Estimate:** 150 min. **Prerequisites:** T1.5, T1.18.

1. **Failing test:** `params_apply_performance_test.cpp` (CPU lane): `TEST_CASE("Preset load and delay disable stay inside the block budget at 192 kHz / 64", "[performance][wallclock]")`: time the `process()` call that drains a preset snapshot and the one where the delay fade-out completes; REQUIRE each ≤ 60 % of 333 µs on the dev box (baseline 479 µs for three delays alone).
2. **Fix:** `ruinae_effects_chain.h:205-264, 975-993, 1098-1099`: stop clearing inactive delay types eagerly; mark them `needsClear` and clear a bounded slice per block (e.g. 16 384 samples) while the slot is muted, finishing before the type can become audible (`setDelayType` waits for the incoming type's clear, extending pre-warm if needed). On preset load only the active type is cleared in that block.

### T2.34 — Registered defaults and ranges agree with the processor (R-184, R-185, R-186, R-188, R-097)

**Estimate:** 120 min.

1. **Failing test:** `controller_params_test.cpp`: `TEST_CASE("Every registered default denormalises to the processor default", "[controller][params][defaults]")` using `tests/test_helpers/registered_defaults_probe.h`: for every registered, non-hidden parameter feed `defaultNormalizedValue` through the processor's `processParameterChanges` into a fresh processor and compare `getState` bytes with an untouched processor's. Expected failures today: Ladder Slope, Env Attack, Self-Osc Release, global filter cutoff/reso, Chaos Rate, S&H/Random rate, FFT size (fixed in T2.12). Plus `TEST_CASE("S&H and Random rate map [0,1] onto 0.1-50 Hz monotonically", "[params]")` and the same for Chaos speed over 0.05–20.
2. **Fix:** `filter_params.h:225-228` → `createDropdownParameterWithDefault(..., 3, …)`; `:257-258` default 0.5407; `:272-273` default 0.7384; `global_filter_params.h:54-56` defaults 0.5663 / 0.0203; `chaos_mod_params.h:28-29, 47-48, 69-72`: own log map over [0.05, 20], unit "x", default `logMapToNormalized(1, 0.05, 20)`; `sample_hold_params.h:51-55, 80-82, 107-109, 162` and `random_params.h:37, 64-66, 93, 146`: own log map over [0.1, 50] with matching inverse and default. Update the `editor.uidesc` default-values (1145, 1302, 1338, 2134, 2297, 2343, 2533, 2539) through XSLT. States store plain units, so no STATE-COMPAT; automation lanes of these four rate parameters change their normalized meaning (note in CHANGELOG).

### T2.35 — Oscillator quality: Formant range and jitter, Additive floor (R-149, R-159, R-158)

**Flags:** CROSS-PLUGIN IMPACT (`dsp/processors/formant_oscillator.h`, `additive_oscillator.h`), PRESET-IMPACT (audio: Formant and Additive presets). **Estimate:** 240 min.

1. **Failing test:** `selectable_oscillator_test.cpp`: `TEST_CASE("Formant and Additive oscillators: pitch range and inharmonic floor", "[systems][oscillator][quality]")`: Formant MIDI 93–108 within 5 cents (baseline C7 −78.7); Formant worst non-harmonic ≤ −40 dBc at A2–A5 (baseline +3.0 / −20.7); Additive worst non-harmonic ≤ −60 dBc at A1/A4/C7 (baseline −42).
2. **Fix:** `formant_oscillator.h:195`: raise `kMaxFundamental` to 8000 and the grain pool so overlaps fit (or fade the grain length with pitch); `:420-422, 507-532`: start each grain with the fractional offset `phaseAfterWrap / increment` applied to envelope and carrier (expose the remainder from `PhaseAccumulator::advance`, `phase_utils.h:160-167`, additively). `additive_oscillator.h:527-547`: place each partial with the Hann main-lobe kernel at its fractional bin (3 bins) instead of a single rounded bin.

---

## Phase 3 — measured defects not already covered

### T3.1 — Spectral delay renders deterministically (R-161)

**Flags:** CROSS-PLUGIN IMPACT (`spectral_delay.h`; Iterum). **Estimate:** 45 min.

1. **Failing test:** `factory_preset_audio_test.cpp`: `TEST_CASE("Spectral_Wash renders identically from two fresh processors", "[presets][determinism]")`: max diff ≤ 1e-7 (baseline 0.1451).
2. **Fix:** `spectral_delay.h:235-238`: seed from a constant (mixed with the channel index only); `reset()` (294-299) re-seeds before drawing phases.

### T3.2 — Minimum declick at note-off (R-162)

**Estimate:** 45 min.

1. **Failing test:** `ruinae_voice_test.cpp`: `TEST_CASE("RuinaeVoice: release 0 does not click", "[ruinae-voice][click]")`: sine A4, release 0 → jump ratio ≤ 1.5 (baseline 2.80).
2. **Fix:** `ruinae_voice.h:156-160` / the engine's `setAmpRelease` (`ruinae_engine.h:1399-1402`): clamp the amp-envelope release to ≥ 2 ms (amp envelope only; filter and mod envelopes keep 0.1 ms).

### T3.3 — `uidesc-inventory --check` passes for Ruinae (R-163, R-164, R-165, R-166)

**Estimate:** 90 min.

1. **Failing test:** add a ctest like Disrumpo's (`plugins/disrumpo/tests/CMakeLists.txt`, `uidesc_overlaps_allowed.json`): `node tools/uidesc-inventory.js --check plugins/ruinae/resources/editor.uidesc --allow plugins/ruinae/tests/uidesc_overlaps_allowed.json`; baseline 38 overlaps / 26 out-of-bounds.
2. **Fix (XSLT workflow on `editor.uidesc`):** MonoGroup (4016-4018) width ≥ 137 (R-166); MixerShift label below the knob (3834-3839); ArpMode (3494) width 64 or move "Octaves" (3499) to x ≥ 220 (R-165); Euclidean label (3438) inside its row; harmonizer voice knobs (3062-3073 and voices 2–4) to y ≥ 0 (R-164); Cutoff/Reso labels (2535, 2541) to y ≤ 34; Rate/Time labels to x ≥ 0. Allow-list only deliberate stacks (ring indicators over knobs, stacked mod-type groups, ADSR expand buttons, the off-screen settings drawer), each with a reason string.

### T3.4 — AUv2 component carries the plugin version (R-191)

**Flags:** macOS-only effect; cannot be verified on Windows — mark "unverified until CI" in the commit. **Estimate:** 60 min.

1. **Failing test:** extend `tools/check-bundle.js` (or a new Node check wired into CI) to assert the generated AU Info.plist's `CFBundleShortVersionString` equals `plugins/ruinae/version.json` and AudioComponents `version` equals `kAUcomponentVersion`.
2. **Fix:** rename `plugins/ruinae/resources/au-info.plist` → `au-info.plist.in` with `@PROJECT_VERSION@` and `@AU_COMPONENT_VERSION@`; generate it in `krate_plugin_configure_generated_files` (`cmake/KratePlugin.cmake:88-114`) and pass the generated file at `:246-251`. This touches the shared CMake: apply to Ruinae only unless the user asks for all plugins; say so in the commit.

### T3.5 — Tempo-derived step lengths are rounded; the lint sees them (R-106, R-015)

**Flags:** GRADUS IMPACT, CROSS-PLUGIN IMPACT (`arpeggiator_core.h`, `trance_gate.h`), macOS-SENSITIVE (run all four proxies), PRESET-IMPACT (goldens may move by ≤ 1 sample per step: the golden compare already tolerates 8 samples; structure must be identical). R-106 is PLAUSIBLE: the failing test is the verification. **Estimate:** 90 min. **Prerequisites:** T1.31.

1. **Failing test:** `dsp_ieee_tests` + `dsp_processors_tests`: `TEST_CASE("Step durations round to the nearest sample", "[processors][arp][trance-gate][timing]")`: TranceGate at 50 BPM, dotted half, 44.1 kHz → 79380 (baseline 79379; note the audit's 140 BPM 1/16 example is exact and does not fail); arp swing pair at 36 %: long + short == 2·base. Lint self-test: `tools/lint-musical-time-truncation.js --self-test` gains a case with `static_cast<size_t>(secondsPerBeat * beats * sampleRate)`.
2. **Fix:** `arpeggiator_core.h:1093-1096, 1105-1110`: `std::llround`; short half = `2*base − long`. `trance_gate.h:456, 459`: `std::llround`. `tools/lint-musical-time-truncation.js:31` `TIME_RE`: also match `secondsPerBeat|beatsPer\w*|tempo\w*|bpm`; fix or mark every new hit tree-wide (ratchet baseline if needed).

### T3.6a — Tape hiss is silent at Wear 0 and Age 0 (R-154)

**Ruling (user, 2026-11-16): fix in the DSP.** Both findings are PLAUSIBLE: the old behaviour was documented design (`tape_delay.h:686`, `character_processor.h:137-139` "Real tape hiss is present even during silence", `plugins/iterum/CHANGELOG.md:422`, `specs/_archive_/024-tape-delay/spec.md:96`), so this task changes a documented behaviour on purpose; the user overruled it.
**Interpretation this plan takes (state it in the commit message):** the hiss is gated off only when `wear_ == 0 && age_ == 0`. Any Wear above 0 or any Age above 0 keeps today's curve exactly (`-80 + wear*40 + age*10` dB), so every Tape preset with Wear or Age above 0 sounds the same. The alternative reading, "gate on Wear alone, so Age no longer adds hiss at Wear 0", also changes Age-only presets; take it only if the user says so.
**Flags:** CROSS-PLUGIN IMPACT (`dsp/effects/tape_delay.h` and `dsp/systems/character_processor.h`; consumers are Iterum's Tape mode and Ruinae's Tape delay; a grep of `plugins/iterum/tests` for `TapeDelay|tape_delay` finds no output test, but run `node tools/header-consumers.js` on both headers to confirm the consumer list). PRESET-IMPACT (audio: Tape presets with Wear 0 and Age 0 lose the −80 dB floor, measured −98.2 dBFS RMS in Ruinae; presets with Wear or Age above 0 are unchanged). No state change. **Estimate:** 90 min.

1. **Tests first** (`dsp/tests/unit/effects/tape_delay_test.cpp`, tags `[tape-delay][hiss]`; build `dsp_effects_tests`; run each by name and see it FAIL on current code).
   - `TEST_CASE("TapeDelay is silent at Wear 0 and Age 0 with no input")`: construct, `prepare(48000, 512, 2000)`, `setMotorSpeed(50)`, `setSaturation(0)`, `setMix(1)`, `setFeedback(0)`, head 0 enabled at 0 dB. **Do not call `setWear` or `setAge`**: the default state (both 0) is the case, and it catches a `prepare()` that re-enables the hiss without calling `updateCharacter()`. Process 2 s of digital silence; REQUIRE RMS ≤ 1e-6 (−120 dBFS) after the first 0.5 s. Also run it after `setWear(0.5f); setWear(0.0f);` to cover the toggle path.
   - `TEST_CASE("TapeDelay keeps today's hiss once Wear or Age is above 0")`: first run the case on the UNCHANGED code and record RMS dB for (wear 0.01, age 0), (wear 0, age 0.01), (wear 0.5, age 0), (wear 1, age 1); paste the four numbers into the test as the baseline; REQUIRE each within ±1 dB of the baseline. This is the "no preset moved" pin.
   - `TEST_CASE("TapeDelay hiss switch-on and switch-off do not click")`: sweep Wear 0 → 0.05 → 0 across 40 blocks on silence; REQUIRE the largest sample-to-sample difference ≤ 3e-4 (−70 dBFS). Disabling cuts the noise branch at once, but the level there is at most about 1e-4.
   - Ruinae: `ruinae/tests/unit/ruinae_effects_chain_test.cpp`, `TEST_CASE("Tape delay at Wear 0 Age 0 is silent with no input", "[effects-chain][tape]")`: Tape delay enabled, no notes, 2 s, RMS ≤ −120 dBFS (probe baseline `silence:delay:Tape` −98.2 / peak −85.6 dBFS, all FX −96.7 / −83.0).
2. **Fix.**
   - `character_processor.h` next to `setTapeHissLevel` (:364): add `void setTapeHissEnabled(bool enabled) noexcept { tapeHiss_.setNoiseEnabled(NoiseType::TapeHiss, enabled); }`. `prepare()` (:135) keeps enabling it; the tape delay overrides that below.
   - `tape_delay.h` `updateCharacter()` (:675-707): after the two `setTapeHissLevel` calls add `character_.setTapeHissEnabled(wear_ > 0.0f || age_ > 0.0f);`. Update the comment at :686 to "Hiss level: off at Wear 0 and Age 0; −80 dB to −40 dB with Wear; +0 to +10 dB with Age".
   - Confirm `TapeDelay::prepare()` and `reset()` end with `updateCharacter()` (grep shows calls at :290, :317, :339, :361 only from setters): if not, add the call, otherwise the first test fails with the hiss still on. Read around the call before editing.
3. **Docs:** `plugins/ruinae/MANUAL.md` delay section: "Tape adds hiss only when Wear or Age is above 0; at Wear 0 and Age 0 it is silent with no input". Add a bullet to the unreleased CHANGELOG section of Ruinae and of Iterum if one exists; never re-bump a version, and do not edit the old entry at `plugins/iterum/CHANGELOG.md:422`.
4. **Build, run:** `dsp_effects_tests ruinae_tests`, plus `Ruinae` and the Iterum `plugin_tests approval_tests` targets (shared header; consumers per `header-consumers.js`). Gate the three suites sharded. pluginval for Ruinae and Iterum.
5. **Acceptance:** the four new tests pass; the baseline pin is unchanged; zero warnings; existing "Wear=1 produces audible hiss with silence input" (`tape_delay_params_diagnostic_test.cpp:138`) still passes; the existing "Disable hiss" comments at `tape_delay_test.cpp:673-675, 1227, 1312` are now literally true.

### T3.6b — Spectral delay echo timing (R-160)

**Ruling (user, 2026-11-16, second answer, after being shown the trade-off): document only; no DSP change.** The first answer ("fix both") was given before the code was read. Reading `spectral_delay.h` showed that the echo envelope can only land on whole STFT frames: `readDelayedSpectrum` (:699-731) takes magnitude and phase from the nearer frame (`newerIsNearer = frac <= 0.5f`, :728-730) on purpose, because blending magnitudes dimmed note-onset echoes by up to 3.5 dB, and `SpectralDelay passes the delayed signal at unity between frames` (`spectral_delay_test.cpp:2715`) pins the echo level within ±1 dB. The realised delay is `round(T)` frames (T = delay × sr / hop; ties round down), so each echo is off by up to ±0.5 hop, ±256 samples (5.3 ms) at 48 kHz, 1024 FFT, hop 512: at 1/4 @ 120 BPM T = 46.875, realised 47, +64 samples per repeat, which the probe measured as 24074 / 24164 / 24141 including onset-detector bias. The two fixes were rejected: 75 % overlap (hop = fft/4) halves the error at 2× spectral-delay CPU in Iterum, Ruinae and Seraphis and changes Seraphis's hard-coded 512 hop (`seraphis/src/processor/processor.h:172, :199`); magnitude blending makes the timing exact but loosens the level pin to about ±4 dB.
**Flags:** none (documentation and a characterisation test; no DSP, state or preset change). **Estimate:** 30 min.

1. **Test** (`dsp/tests/unit/effects/spectral_delay_test.cpp`, tags `[spectral-delay][sync]`, build `dsp_effects_tests`; this passes on current code, it is a characterisation pin and needs no failing run): `TEST_CASE("SpectralDelay synced echo spacing is within one hop of the grid")`: 48 kHz and 44.1 kHz, 1024 FFT, feedback 0.5, synced to 1/4, 1/8, 1/16 at 120 BPM and 1/4 at 90 BPM, mix 1; feed one click, find the first three echo onsets with the probe's detector (`burstOnsets`, threshold 0.05, `specs/ruinae-audit/probes/audit_probes.cpp.txt:1486-1492`), REQUIRE each spacing within ±hop (512 samples) of the ideal grid and the first echo within ±hop/2 + 128 samples of one grid step (the 128 is the detector's measured bias; probe baseline 24074 / 24164 / 24141 at 1/4 @ 120). The bound is the design limit, not a target to tighten.
2. **Docs:** `plugins/ruinae/MANUAL.md` delay section: "Spectral delay echo timing is quantised to the FFT hop (about 10.7 ms at 48 kHz). A tempo-synced echo can sit up to about 5 ms off the grid." Same sentence in `plugins/iterum/` docs if a Spectral delay section exists (grep `MANUAL.md` there). No change to `spectral_delay.h`: its comments at :688-698 and :724-727 already state the design.
3. **Phase 6:** T6.4's `fx-sync 1/4 @ 120` (Spectral ±1 hop) is exactly this bound; promote the probe there and do not duplicate the test.

---
## Phase 4 — duplication, spec conformance, anti-patterns

Same defaults as Phase 2 (build `ruinae_tests Ruinae`, gate `ruinae_tests`, pluginval when plugin sources changed, clang-tidy `ruinae`, commit). Docs-only commits skip pluginval and clang-tidy but still run the gate once if any source comment changed.

### T4.1 — Gain Compensation: remove the dead control, correct the docs (R-101, R-090)

**Estimate:** 45 min.

1. **Failing test:** `controller_params_test.cpp`: `TEST_CASE("Gain Compensation is a hidden legacy parameter", "[controller][params]")`: flags of `kSettingsGainCompensationId` contain `kIsHidden` and not `kCanAutomate`; the uidesc reachability helper finds no control bound to tag 2205.
2. **Fix:** `settings_params.h:88-89` flags; remove the label and toggle at `editor.uidesc:4421-4434` (XSLT); keep the ID, atomic and state field (state layout unchanged). Fix `MANUAL.md:47, 1061`, `README.md:24, 95`, `docs/ruinae_signal_flow.dot:259`, and the engine comments at `ruinae_engine.h:12, 17, 357, 969`. Rename the misleading test at `ruinae_engine_integration_test.cpp:696-726`.

### T4.2 — Documentation and comments match the code (R-103, R-102, R-111)

**Estimate:** 60 min. **Prerequisites:** T1.18, T1.25, T2.10 (they change the facts being documented).

1. **Test:** add `tools/lint-ruinae-doc-counts.js` (Node) that reads `RuinaeFilterType::NumTypes`, `kNumGlobalSources`, the preset file count and compares them with the numbers in MANUAL.md / docs/index.html; wire it as a ctest. It fails today (6 vs 13 filter types, 10 vs 14 sources, 65+ vs 168 presets).
2. **Fix:** `ruinae_effects_chain.h:5-16, 52-53, 62-71` (modulation slot with 4 types, two reverbs, no compensated latency); `docs/ruinae_signal_flow.dot:313, 316, 374, 380`; `docs/index.html:126, 139, 172`; `MANUAL.md:111, 281, 469, 658, 908, 914`; `mod_matrix_params.h:23-24` and `controller_mod_matrix.cpp:21, 91, 215, 282` range comments (use `kModSourceCount` / `kModDestCount`).

### T4.3 — Dead processor members and the engine include in parameter packs (R-110, R-139, R-112)

**Estimate:** 75 min.

1. **Test:** compile-only guard: new `ruinae/tests/unit/dropdown_mappings_include_test.cpp` that includes only `parameters/dropdown_mappings.h` and `static_assert`s `!defined(RUINAE_ENGINE_H_INCLUDED)` (use the engine header's real include guard name); it fails to compile today.
2. **Fix:** move `RuinaeModDest` and its `static_assert` (`ruinae_engine.h:67-92`) into `ruinae/src/ruinae_types.h`; `dropdown_mappings.h:20` drops the engine include; `modDestFromIndex` (207-211) clamps its index. Delete `currentBlockSamples_`, `wasTransportPlaying_`, `mixBufferL_/R_`, `envDisplayMessageSent_`, `morphPadModMessageSent_` (`processor.h:324, 326, 349-350, 386, 397`) and their resize/fill (`processor.cpp:178-179, 208-209`); fix the header comment (`processor.h:16-19`); in `process()` clamp `numSamples` handling: if `data.numSamples > maxBlockSize_`, render in chunks of `maxBlockSize_`.

### T4.4 — Dead tables, dead arp IDs, duplicated arp parameter code, wrong ring table entry (R-099, R-098, R-132, R-100)

**Flags:** GRADUS IMPACT and CROSS-PLUGIN IMPACT for the shared-header part (`plugins/shared/src/parameters/arp_params_common.h`). R-100 and R-132 are PLAUSIBLE (latent, no user-visible effect): apply as cleanup. **Estimate:** 180 min. **Prerequisites:** T2.1, T2.2.

1. **Failing tests:** `arpeggiator_params_test.cpp`: `TEST_CASE("Note-value name table matches the dropdown count", "[arp][params]")` (static_assert in code + runtime check); `TEST_CASE("Every dropdown's step count equals its string table size", "[params]")` for LFO shape, phaser stages/waveform, delay mod waveform, harmonizer key; a cross-plugin case in `tests/` beside `arp_shared_prefix_golden.h`: for every shared arp ID except the documented exceptions (mode count, ratchet shuffle), `handleArpParamChange` produces identical atomics in Ruinae and Gradus.
2. **Fix:** register LFO/phaser/delay/harmonizer dropdowns from the `dropdown_mappings.h` tables (114-123, 389-421, 440-444) and delete the unused helpers (`numStepsToIndex`, `fftSizeToIndex`, `phaserStagesToIndex`, `harmonizerIntervalToIndex`); `RuinaeEffectsChain::setDelaySpectralFFTSize` uses `fftSizeFromIndex`. Move `kLaneSpeedValues`/count/default and the shared-ID handle/format helpers into `arp_params_common.h` (templated on the params struct like `saveArpParamsShared`); keep the deliberate per-plugin differences as explicit arguments. `arpeggiator_params.h:930-945`: use `Parameters::kNoteValueDropdownStrings`. Delete the unregistered v1.5 atomics (`arpeggiator_params.h:154-185`) and mark IDs 3388–3398, 3402–3445 "reserved for Gradus parity" in `plugin_ids.h:1202-1245` (keep `kArpEndId` and the numbers). `controller_mod_matrix.cpp:31`: `kTranceGateRateId`; delete the stale comment at `:21`. Update `plugins/ruinae/CLAUDE.md` arp ID range.

### T4.5 — One implementation of each view-sync rule (R-108)

**Estimate:** 120 min. **Prerequisites:** T1.3, T2.27.

1. **Failing test:** `controller_state_test.cpp`: `TEST_CASE("Live edit and state load give the same view state", "[controller][view-sync]")`: set OSC A waveform to Pulse via `setParamNormalized` and via `setComponentState`; the PW knob alpha is identical; same for the condition lane value and chaos display speed.
2. **Fix:** extract `applyOscPwDimming(osc, normalized)`, `conditionIndexFromNormalized`, `displayBeatsPerSecond()` and per-section wiring helpers; call them from `controller_view_sync.cpp:479-484, 889-894, 194, 644, 770, 794, 809` and `controller_verify_view.cpp:439, 635-643, 742, 794, 821`; replace `4.0`, `17`, `120.0f` with `kOscWaveformCount − 1`, `ArpConditionLane::kConditionCount − 1`, `LfoWaveformDisplay::kDefaultBpm`.

### T4.6 — Processor→controller display data travels as values, not addresses (R-005)

**Flags:** macOS note: the DataExchange fallback needs a Timer; Linux test harnesses have no run loop (gate non-vacuity checks on Linux). **Estimate:** 240 min. **Prerequisites:** T1.32, T2.28.

1. **Failing test:** `voice_route_rt_safety_test.cpp`: `TEST_CASE("No message carries a raw pointer", "[vst][lifecycle]")`: the three handshake messages (`processor.cpp:101-157`) no longer exist; `editor_lifecycle_test.cpp`: destroy the Processor while a connected Controller's editor timer ticks (ASan build) → no error.
2. **Fix:** publish trance-gate step, transport flag, env outputs/stages, voice-active flag and morph X/Y as hidden read-only output parameters (the pattern used for playheads) written once per block; the controller's poll timer and the ADSR displays read parameter values. Delete `sendOneTimeHandshakes`, the pointer members and `setPlaybackStatePointers` call sites.

### T4.7 — Voice routes are published atomically (R-007)

**Estimate:** 90 min. **Prerequisites:** T1.22, T2.19.

1. **Failing test:** `voice_route_rt_safety_test.cpp`: `TEST_CASE("Voice route table is never read torn", "[vst][modmatrix][threads]")`: one thread alternates slot 0 between two fully different routes while another calls the reader; every route read equals one of the two.
2. **Fix:** `processor.h:83-118`: publish all 16 routes as one snapshot through a second `RTTransferT<std::array<VoiceModRoute,16>>` (or a sequence counter per table that the reader retries on, bounded to 3 tries); `processor_messaging.cpp:36-65, 86` and `processor_state.cpp:190-211` write the snapshot; `processor_params.cpp:516-535` reads it.

### T4.8 — Verify-first bundle: event segments, block-rate automation, lane paste, truncated state (R-006, R-138, R-145, R-076)

All four are PLAUSIBLE. For R-006, R-138 and R-145 write each test and change code only where the test fails AND the stated default ruling says to. **R-076 is not verify-first: the user ruled (2026-11-16) that a truncated state is rejected, so it is a deliberate behaviour change with its own steps below.** **Flags (R-076):** no state-format change, no version bump. PRESET-IMPACT none: every factory preset is a complete stream (verify, step 6). **Estimate:** 150 min for R-006, R-138, R-145 plus 150 min for R-076 = 300 min.

- **R-006:** `midi_events_test.cpp`: `TEST_CASE("Dense pitch-bend stream does not multiply engine calls", "[integration][performance-structure]")`: 512 bend points in one 512 block → engine `processBlock` calls ≤ note events + 1 + (512/64). Fix if it fails: `processor_midi.cpp:120-148` splits segments only at note events; bend/expression events are applied at the next 64-sample sub-block boundary.
- **R-138:** document block-rate automation in MANUAL.md; the Dice case is covered by T1.21. No code change.
- **R-145:** `arp_controller_test.cpp`: `TEST_CASE("Modifier lane writes flags/255", "[controller][arp]")`: after a UI edit of flags 0x03 the processor's stored step equals 0x03 (baseline 0x33). Fix: `plugins/shared/src/ui/arp_modifier_lane.h:239, 433` use `/255.0f` (CROSS-PLUGIN IMPACT: Gradus uses the lane; confirm its decode at the same time). Cross-type paste stays as specified by `plugins/shared/tests/test_copy_paste.cpp`; fix only the comment at `arp_lane.h:73`.
- **R-076 (ruled: reject truncated state; nothing is applied):**
  - **Scope, read before coding.** "Truncated" means a stream that ends inside the *required prefix*: the version int32, then the 17 packs `loadGlobalParams … loadReverbParams`, the reverb-type int32 (version ≥ 5), and `loadMonoModeParams` (`processor_state.cpp:155-183`), i.e. everything before the voice routes. Streams that end later are NOT rejected. The tail packs (voice routes, FX flags, phaser, flanger, chorus, LFO extras, macro, rungler, settings, mod sources, harmonizer, arp) are loaded with ignored return values on purpose: packs were appended without a version bump (the comment at `processor_state.cpp:224-227` says "very old presets with no byte"), so a short tail cannot be told from a legitimate older stream. Do not widen the check into the tail. The existing empty-stream, null-stream and bad-version early returns (`processor_state.cpp:123, 137, 150, 152`) keep returning `kResultTrue`; they apply nothing and are not part of this ruling.
  - **Design: validate first, then apply (atomic, no rollback).** Add `plugins/ruinae/src/parameters/state_prefix_validator.h` (header only; layer: Ruinae parameters, included by both processor and controller, so there is ONE definition of "truncated"): `bool Ruinae::isRequiredStatePrefixComplete(const char* bytes, size_t size, Steinberg::int32 version)`. It builds a heap-allocated `struct PrefixScratch` (`std::make_unique`, since the packs are large; Catch2 reports a stack overflow as "Unknown exception") holding one default-constructed instance of each of the 18 pack types (`GlobalParams, OscAParams, OscBParams, MixerParams, RuinaeFilterParams, RuinaeDistortionParams, RuinaeTranceGateParams, AmpEnvParams, FilterEnvParams, ModEnvParams, LFO1Params, LFO2Params, ChaosModParams, ModMatrixParams, GlobalFilterParams, RuinaeDelayParams, RuinaeReverbParams, MonoModeParams`; those are the `processor.h:274-301` types), runs the same `load*Params(scratch, streamer)` calls in the same order over a `MemoryStream` on the bytes, requires every call to return true, and for version ≥ 5 requires the reverb-type `readInt32` to succeed (today its failure is ignored, `processor_state.cpp:175`). The `load*Params` functions already return `bool`; do not change them.
  - **Processor.** `processor_state.cpp` `setState`, after the version check at :151-152 and before the first `load*` call at :155: `if (!isRequiredStatePrefixComplete(snapshot->bytes.data(), snapshot->bytes.size(), version)) return Steinberg::kResultFalse;`. Because nothing was written yet, no atomic, voice route, engine or arp state changes, and the `transferObject_ui` at :289 is never reached. The 18 `if (!load…) return kResultTrue;` lines at :155-183 stay as a second line of defence (they can no longer fire for a stream that passed the validator); change their `kResultTrue` to `kResultFalse` so a future divergence cannot report success.
  - **Controller (same definition, no partial UI).** `controller.cpp` `setComponentState` (:276-304) and `controller_presets.cpp` `loadComponentStateWithNotify` (:364-387) read the whole stream into a `std::vector<char>` first (an `IBStream` may not seek), call `isRequiredStatePrefixComplete`, and on failure return `kResultFalse` / `false` before `bulkParamLoad_ = true`, before any `setParamNormalized` / `editParamWithNotify`, and before `syncAllViews()`. On success run the existing `loadStateCore` over a `MemoryStream` on the buffer. `loadStateCore` itself is unchanged (its `return false` at :444, :466, :468, :473, :501 stay: they cover the tail). Without this, the processor would reject the stream while the controller applied half of it, and the UI would show values the audio does not use.
  - **Tests first** (build `ruinae_tests`, run each by name, see it FAIL on current code, which returns `kResultTrue`):
    - Rewrite `state_migration_test.cpp:97` `"Truncated v1 stream loads partial defaults"` into `"Truncated v1 stream is rejected and leaves the state unchanged"`: make processor P, load a valid stream whose masterGain is 1.5 (take P's default `getState` bytes and patch the float after the version int32), save `before = getState(P)`; `setState(P, truncated)` with the existing 4-field stream; REQUIRE `kResultFalse`; REQUIRE `getState(P)` byte-identical to `before`. Remove the "Should return kResultTrue" comment at :112.
    - Rewrite `preset_safety_test.cpp:178` `"Truncated snapshot loads partial state"` the same way (20-byte cut, REQUIRE `kResultFalse`, state byte-identical; keep "processor remains functional": `getState` still `kResultTrue`, and a following valid `setState` still succeeds). The file lives at `plugins/ruinae/tests/unit/preset_safety_test.cpp`.
    - New `state_migration_test.cpp` `TEST_CASE("Every cut inside the required prefix is rejected", "[state][robustness]")`: take a full default stream; for every length `n` from 0 to the stream size, call `isRequiredStatePrefixComplete(bytes, n, kCurrentStateVersion)`; REQUIRE the result is monotone (false up to some `prefixEnd`, true from there), `4 < prefixEnd < size`, and for `n` in `[4, prefixEnd)` that `setState` of the first `n` bytes returns `kResultFalse` with the state byte-identical; for `n == prefixEnd` and for `n == prefixEnd + 1` (cut inside the voice routes) REQUIRE `kResultTrue`, which pins the tail contract. Use a stride of 1 only inside the 64 bytes around the Filter pack and around `prefixEnd`, stride 97 elsewhere, to keep the case under 1 s.
    - Controller (`plugins/ruinae/tests/unit/controller/preset_browser_test.cpp`, which already exposes `loadComponentStateWithNotify`): `"Truncated state changes no controller parameter"`: set a parameter to a non-default normalized value, call `setComponentState` and `loadComponentStateWithNotify` with a 20-byte stream; REQUIRE `kResultFalse` / `false` and the parameter value unchanged, and `bulkParamLoad_` not left set (assert through a following normal edit that still reaches the handler).
  - **Verify (step 6).** Every factory preset must still load: the existing preset sweeps (`factory_preset_audio_test.cpp:364, 406`, `ruinae_byte_identical_post_lane10_test.cpp:471`, which assert `kResultTrue` from `setComponentState`) pass unchanged; add one assertion in a processor-level sweep that `Processor::setState` returns `kResultTrue` for all 168 presets' component states (reuse `loadVstPresetComponentState` from `abyssal_level_test.cpp:52`, copy the helper rather than include the file). Also run pluginval (state tests use `getState`/`setState` round trips).
  - **Behaviour change to record (commit message, MANUAL.md "Troubleshooting" if present, CHANGELOG unreleased bullet):** a damaged or hand-edited `.vstpreset`/project chunk that ends inside the first part now fails to load and leaves the current sound, instead of loading a half-old, half-new patch that the next save would persist. Hosts may show their own "preset failed to load" message.

---

## Phase 5 — test-gap fills

Test-only tasks: build `ruinae_tests`, gate `ruinae_tests`, no pluginval, clang-tidy `ruinae`, commit. Each new assertion must be shown to fail when the behaviour it guards is broken: temporarily break the code, see red, revert, and say so in the commit message.

### T5.1 — Preset-switch safety tests assert behaviour (R-117)

**Estimate:** 60 min. **Prerequisites:** T1.6–T1.8.
`ruinae/tests/integration/preset_switch_safety_test.cpp:161-394`: arp→non-arp switch: after the switch and key release REQUIRE silence within release + 10 blocks and a balanced output bus (MIDI Out on); oscillator-type switch: click detector on the switch block (jump ratio ≤ 3, the preset reset is a hard cut by design, so assert the bound that holds and document it); voice routes: configure a route through `notify`, save, load into a second processor, REQUIRE the bytes match. Replace `std::isnan` in `hasNanOrInf` (124-129) with `detail::isFinite`. Fix the comments at 189, 286, 415 (3 = Sync, 4 = Additive).

### T5.2 — State migration covers real old layouts and prior-state contamination (R-123)

**Estimate:** 90 min. **Prerequisites:** T2.1, T2.2.
`state_migration_test.cpp:97-183`: build genuine v4/v5/v6/v7 blobs (helper that writes the packs the old version wrote, in order; do not patch the version of a v8 blob); `StateMigration_V{4,5,6,7}_LoadsExpectedValues` compares every parameter against expected values; `StateLoad_PartialOverNonDefault_DoesNotInheritPreviousPreset`; `StateLoad_OutOfRangeEnums_AreClamped` (also NaN floats). Add `f:/tmp/ruinae-audit/garbage2.bin` as `ruinae/tests/fixtures/state_garbage_phasemod.bin` and load it.

### T5.3 — Factory-preset audio test releases the note and checks level, DC and tails (R-125)

**Flags:** the extended render is more than 15 s of toolchain-independent audio → tag `[long]`. **Estimate:** 75 min. **Prerequisites:** T1.5, T1.18.
`factory_preset_audio_test.cpp:114-316`: note-off at 3 s, render 10 s more; per preset CHECK: last-second RMS < −80 dBFS (allow-list presets with feedback ≥ 1.0 or freeze, each with a reason); peak < 0.99 and < 1 % of samples above 0.85; |mean| < 0.01 over the sustain window; non-finite count 0 via `detail::isFinite`; assert `sustainedRms`.

### T5.4 — Sidechain path is exercised (R-126)

**Estimate:** 60 min. **Prerequisites:** T2.14, T2.26.
`mod_source_pipeline_test.cpp`: `Processor_Sidechain_StereoDrivesEnvFollower` (1 kHz burst on input bus 0, EnvFollower → Global Cutoff, centroid rises during the burst), `Processor_Sidechain_MonoBusDuplicated`, `Processor_Sidechain_SegmentOffsetsAligned` (note events split the block; the follower trace matches an unsplit render), `Processor_SidechainActiveParam_WrittenOnFirstBlockAndOnChange`. Tags `[integration][sidechain]`.

### T5.5 — Arp performance tests are tagged and can fail (R-127)

**Estimate:** 45 min. **Prerequisites:** T1.34.
`arp_performance_test.cpp:164-312`: tags `[arp][performance][wallclock]` (and `[long]` on the stress case if it exceeds 15 s); remove `[!mayfail]` from both CPU cases and from SC-003 (`ruinae_voice_test.cpp:1550-1604`), restore its bar to 8 %; do not raise any bar — if SC-003 fails at 8 % in the isolated CPU lane, stop and report. Normalise note values with `kNoteValueDropdownCount − 1` (221, 271; `state_roundtrip_test.cpp:696`). The stress test asserts a balanced output bus and no event-cap saturation. Extend `tools/lint-wallclock-tags.js` to follow values derived from a clock.

### T5.6 — Chord/Inversion lane disable is tested through the Controller (R-128)

**Estimate:** 45 min. **Prerequisites:** T1.3.
Replace `chord_lane_disabled_test.cpp:114-216, 254-289` with `Controller_VoiceModeMono_DisablesChordAndInversionLanes` (`editor_lifecycle_harness.h`: open the editor, SEQ tab, `setParamNormalized(kVoiceModeId, 1.0)`, the lane views found in the frame report `isDisabled()`), repeated after switching tabs away and back and after `setComponentState`.

### T5.7 — No empty arp golden (R-122)

**Flags:** PRESET-IMPACT (fixture `ruinae_factory_Arp_Tilt_Cascade_golden_midi.txt`). **Estimate:** 45 min. **Prerequisites:** T1.8.
`ruinae_byte_identical_post_lane10_test.cpp:362-482`: REQUIRE every golden has more than 0 events; for Mod-only presets (`kArpOperatingModeId` = Mod) force MIDI+Mod in the harness for capture only, and regenerate that golden.

### T5.8 — NaN/Inf safety tests can fail (R-107)

**Flags:** macOS-SENSITIVE (IEEE TUs; NaN from a bit pattern through `volatile`). **Estimate:** 60 min.
`ruinae_engine_test.cpp:401-409, 440-444, 726-730, 947-951, 963-966, 978-981, 1021-1025, 1250-1292` and `ruinae_voice_test.cpp:1987-2012`: inside the SAME section inject, then `noteOn`, render, and REQUIRE non-silent finite output plus an unchanged getter where one exists (add `getMasterGain()`-style accessors if missing); for the voice compare the cutoff-modulated render before and after the NaN aftertouch within tolerance. Prove each assertion fails with the guard removed.

### T5.9 — Pitch assertions for MPE tuning and the SVF slope switch (R-120, R-133)

**Estimate:** 60 min. **Prerequisites:** T1.10, T1.11, T1.28.
`mpe_test.cpp:251-266, 357-391`: `MPE_TuningExpression_ShiftsPitchByOctave` (523.25 Hz ± 1 %), bend + tuning combine to the summed offset. `ruinae_engine_test.cpp:936-952`: assert the bent pitch. R-133 (PLAUSIBLE, verify first): `ruinae_voice_test.cpp`: `SVF slope 12→24 dB switch matches a voice that started at 24 dB` after 5 ms smoothing; if it passes on current code keep it as a guard and close R-133 as "no defect".

---

## Phase 6 — promote the audit probes into regression tests

Source: `specs/ruinae-audit/probes/audit_probes.cpp.txt` (82 hidden `[.][audit-probe]` cases, results in `f:/tmp/ruinae-audit/probe-results.txt`). Promote the ones below; a promoted case loses the `[.]` tag, replaces its `PROBE` print with `REQUIRE`/`CHECK` at the threshold the report names, and lives under `ruinae/tests/`. Register every new file in `ruinae/tests/CMakeLists.txt`. Workflow per task: build `ruinae_tests`, gate `ruinae_tests`, clang-tidy `ruinae`, commit. Cases that render more than ~15 s of toolchain-independent audio get `[long]`; wall-clock assertions get `[wallclock]`. Leave the probe file in `specs/ruinae-audit/probes/` unchanged (it is the audit's evidence).

### T6.1 — Shared rig (60 min)
Move the probe `Rig`, note/param helpers, `pitchCents`, `aliasAnalysis`, `clickAt`, `burstOnsets` and the block-invariance renderer into `ruinae/tests/audit_rig.h`; replace the copies made in Phases 1–5 with includes. No behaviour change; the gate must stay green.

### T6.2 — Lifecycle and timing (75 min) → `ruinae/tests/integration/voice_lifecycle_regression_test.cpp`, tags `[integration][regression]`
`sample accuracy` (first non-zero sample − expected identical for offsets 0/1/100/511, dry and with FX); `voice stealing` (steal jump ratio ≤ 1.5, 0 click frames, voices → 0); `fast retrigger` (≤ 1.5); `mono legato portamento` (monotonic glide, final pitch within 5 cents); `voice edges` (attack 0 ≤ 1.5; release 0 ≤ 1.5 after T3.2); `sample rate consistency` (pitch ≤ 5 cents, RMS ≤ 1 dB, envelope times ≤ 10 % across 44.1–192 kHz); `MPE tuned / untouched note` (≤ 3 cents).

### T6.3 — Oscillators, filters, distortion (90 min) → `ruinae/tests/unit/sound_source_regression_test.cpp`, tags `[regression][osc]`, `[regression][filter]`, `[regression][distortion]`
Per oscillator type: pitch ≤ 5 cents at A1/A4/C7 (Sync measured at its slave pitch; Noise/Chaos/Particle excluded from pitch), DC ≤ 0.001, 0 non-finite, worst non-harmonic ≤ −50 dBc for PolyBLEP (≤ −48 at C7 is the measured value: keep ≤ −45 and say why), Wavetable, PD; Formant/Additive/SpectralFreeze thresholds from T2.35 and T1.16. Per filter type at 48 and 192 kHz: 0 non-finite, no zero run > 16 samples, parked-at-20 kHz peak ≤ +6 dBFS (Comb after T2.24). Per distortion type: DC within ±0.002, 0 non-finite, drive-0 level within ±6 dB of Clean (after T2.9). Use a sine, not a triangle, for the filter click metric (the probe's HP/BP/Notch click counts were a triangle-corner artifact).

### T6.4 — Effects (90 min) → `ruinae/tests/integration/fx_regression_test.cpp`, tags `[integration][regression][fx]`
`silence fx` (no note: RMS ≤ −120 dBFS for every FX combination, Tape at Wear 0 and Age 0 included after T3.6a; no exception); `tail dry / reverb` (voices → 0 within 0.2 s, reverb tail < −100 dBFS within 30 s → `[long]`); `tail delay max feedback` (characterisation: bounded ≤ 0 dBFS with the limiter on); `fx latency and echo timing` (≤ 48 samples for time-domain types, ±1 hop Spectral, dry shift 0); `fx-sync 1/4 @ 120` (Digital/PingPong exact, Spectral ±1 hop); `automation fx` and `automation osc dist mix voice` and `automation filter and gain` (jump ratio ≤ 1.5 for every step, after T2.24; exclude the master-gain-to-0 zero run).

### T6.5 — Arp, modulation, presets, state (90 min)
→ `ruinae/tests/unit/processor/arp_regression_test.cpp` `[arp][regression]`: all 10 modes balanced (128/128/0, Chord 384/384/0), grid error ≤ 1 sample, drift 0 over 8 bars; transport stop, latch-off release, backward ppq jump, no process context, disable mid-note: 0 unmatched / 0 voices.
→ `ruinae/tests/integration/mod_regression_test.cpp` `[modmatrix][regression]`: LFO1 → cutoff swing ±1.000, 2.00 Hz, stale |offset| ≤ 1e-4 after amount 0; every source: 0 non-finite, peak ≤ 0 dBFS.
→ extend `factory_preset_audio_test.cpp` `[presets][regression][long]`: 168 presets load, audible, ≤ 0 dBFS, round-trip max diff ≤ 1e-6 (Spectral_Wash deterministic after T3.1); `getState` after load is byte-identical to the loaded component state for every preset (Polymeter_Pulse differs today: find the field that is normalised on load, fix the generator or the loader, and name it in the commit).
→ `state_migration_test.cpp` `[state][regression]`: truncated (25/50/99 %) and garbage states: no hang, no crash, finite output.

### T6.6 — Final probe comparison (45 min)
Re-add the untouched probe file to a scratch build (do not commit it under `plugins/ruinae/tests/`), run it sharded (`node tools/run-gate.js --share 4 --filter '[audit-probe]' ruinae_tests`), and diff the new `PROBE` lines against `f:/tmp/ruinae-audit/probe-results.txt`. Baseline: 1827 lines = 1658 ok / 54 suspicious / 115 bad. Record the new counts and list every line that is still not ok with the task that owns it or the ruling that accepts it. Run `node tools/clean-test-output.js --apply` only after nothing else is writing to `f:/tmp`.

---

## Completion checklist

Fill this in with evidence gathered at that moment, not from memory. One row per task; a row without a file:line and a pasted test output line is not done.

- [ ] **Per finding (189 rows in the findings table):** the fix is verified against the original finding. Cite the changed file:line, the test name, and the test's actual output line (assertion count or measured value). For PLAUSIBLE findings state "fixed" or "refuted: test passed on unmodified code" with the output.
- [ ] **Rulings:** each ruling in the table at the top has a recorded answer (who, when) or the task is listed as blocked. No ruling was assumed silently.
- [ ] **No relaxed thresholds:** every threshold in the committed tests equals the one in this plan. List any that differ and why; "it failed otherwise" is not a reason.
- [ ] **No removed features:** the only user-visible removals are the Gain Compensation control (T4.1) and the pointer handshake internals (T4.6). Anything else removed is listed and justified.
- [ ] **No `[!mayfail]`, no vacuous assertions** (`REQUIRE(true)`, checks after the engine NaN flush only) added; the ones named in R-107, R-117, R-121, R-127 are gone (`grep -rn "REQUIRE(true)\|!mayfail" plugins/ruinae/tests` output pasted).
- [ ] **Gate:** final `node tools/run-gate.js --share 4 --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' ruinae_tests gradus_tests shared_tests` plus every DSP executable touched prints `GATE GREEN`; paste the last line and the case counts.
- [ ] **CPU lane, alone:** `node tools/run-cpu-tests.js ruinae_tests` green, with the measured values for T1.34 and T2.33 against their bars (≤ 60 % of the bar).
- [ ] **ASan:** `[lifecycle]` cases report 0 errors (baseline 6 heap-use-after-free).
- [ ] **pluginval:** exit code 0 at strictness 5 for Ruinae (and Gradus / Iterum for the tasks that touched their DSP); paste the exit codes.
- [ ] **clang-tidy:** 0 warnings for `ruinae`, `dsp`, `shared`, `gradus`.
- [ ] **Goldens:** every regenerated fixture is listed with the task that changed it and the diff classification; all 46 arp goldens are balanced and non-empty.
- [ ] **State:** `kCurrentStateVersion` and the generator's `kStateVersion` are both 9; a v8 blob of each factory category loads to the same audio as before for the fields under a ruling (pasted RMS deltas); no parameter changed type at an existing ID; no arp ID renumbered; `arp_shared_prefix_golden` green.
- [ ] **Cross-plugin:** for every CROSS-PLUGIN task the consumer list from `grep -rl` and the consumers' gate result are in the commit message.
- [ ] **macOS proxies:** `tools/macos-fastmath-repro.sh`, `node tools/lint-apple-globals.js`, `node tools/lint-musical-time-truncation.js`, `node tools/check-portability.js` all clean; macOS itself is "unverified until CI" and is reported that way.
- [ ] **Probes:** T6.6 table pasted: new ok / suspicious / bad counts against 1658 / 54 / 115, each remaining non-ok line explained.
- [ ] **Lints:** `node tools/lint-serial-test-runs.js`, `tools/lint-wallclock-tags.js`, `tools/uidesc-inventory.js --check` (Ruinae), `tools/lint-plugin-editor-baseline.js` clean.
- [ ] **Version and changelog:** not bumped by this plan. Add bullets under the unreleased entry of `plugins/ruinae/CHANGELOG.md` for every user-visible change (PRESET-IMPACT and STATE-COMPAT tasks in particular); `version.json` changes only when the user asks for a release.
- [ ] **Nothing pushed, nothing amended;** `git status` clean; `specs/ruinae-audit/findings.json` and `implementation-plan.md` untouched (`git diff --stat f8ed81e1 -- specs/ruinae-audit/findings.json specs/ruinae-audit/implementation-plan.md` empty).
