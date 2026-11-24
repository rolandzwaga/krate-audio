# Membrum audit fix plan — 2026-11-23

This plan turns the 259 verified findings of the 2026-11-23 Membrum audit (ids between MB-001 and MB-272: 6 critical, 48 high, 71 medium, 134 low; 210 CONFIRMED, 49 PLAUSIBLE) into 120 ordered tasks in 8 phases. It covers the plugin (`plugins/membrum/`), the KrateDSP headers it consumes (`dsp/include/krate/dsp/`), the shared UI code it uses (`plugins/shared/`), the kit generator (`tools/membrum_preset_generator.cpp`) and, for one task, the field map in `tools/membrum-fit/`. The companion report is [report.md](report.md) (`specs/membrum-audit/report.md`): it holds each finding's full prose under its `#### MB-nnn` heading (section 6), the measurement baseline (sections 3 and 4) and the physical-model review with its sources (section 5). The plan refers to findings by id and does not repeat them. It is written for a coding agent with no memory of the audit: every task names the test to write first, the files and functions to change, the behaviour before and after, and the acceptance check. Nothing in it has been implemented. File and line anchors were read at commit `4a14376d`; earlier tasks move lines, so every anchor also names a symbol: find the symbol with Grep and never edit by line number alone.

## How to execute a task

Tasks run in plan order (T1.1, T1.2, ...), one at a time: they share one build tree (`build/windows-x64-release`). Work on a branch created from `origin/main`, never on `main`. A task section states steps 1 and 2 in full and, for steps 3 to 5, only what differs from the text here. Steps 3 to 6 are always run exactly as written below.

1. **Write the failing test.** Create or extend the test file the task names, with the exact `TEST_CASE` name and tags it gives.
   - Membrum tests live under `plugins/membrum/tests/unit/<area>/` or `plugins/membrum/tests/integration/`. A new file is added to the `add_executable(membrum_tests` source list in `plugins/membrum/tests/CMakeLists.txt`. Membrum's cases carry no common tag; use the area tags the task gives. A case that renders more than about 15 s of toolchain-independent audio gets `[long]`; a case that asserts on its own wall clock gets `[wallclock]`; NaN / Inf guards, bounded-grid and state-format cases never get `[long]`.
   - For a defect in a shared DSP header the test goes in `dsp/tests/unit/<layer>/` and builds into `dsp_<layer>_tests` (NaN / Inf injection and bit-exactness cases go into `dsp_ieee_tests`).
   - Use the helpers in `tests/test_helpers/` and read the header for exact signatures first; never write a second copy of a helper that exists: `vst_event_list.h` and `vst_param_changes.h` (events with sample offsets, parameter queues), `artifact_detection.h` (clicks and discontinuities), `spectral_analysis.h`, `signal_metrics.h`, `audio_features.h`, `render_fingerprint.h`, `allocation_detector.h`, `parameter_sweep.h`, `editor_lifecycle_harness.h`, `shared_editor_views_probe.h`, `registered_defaults_probe.h`. Membrum's own fixtures are in `plugins/membrum/tests/unit/voice_pool/voice_pool_test_helpers.h`, `unit/bodies/body_test_helpers.h` and `unit/exciters/exciter_test_helpers.h`.
   - Build (step 3), then run that one case by name, the only direct executable run the repo allows:
     `build/windows-x64-release/bin/Release/membrum_tests.exe "<TEST_CASE name>"`
     Confirm it FAILS, and for the reason the task states. A test that passes on unfixed code proves nothing: correct the test, or, for a VERIFY-FIRST finding, follow that flag.
2. **Implement the fix** in the files and functions the task names. Change nothing else; report unrelated problems instead of fixing them.
3. **Build, capped to two nodes:**
   `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests -- -m:2`
   Add `Membrum` to the target list when a file under `plugins/membrum/src/`, `plugins/membrum/resources/` or `plugins/shared/src/` changed. Add `dsp_<layer>_tests` (and `dsp_ieee_tests` when the test lives there) when a file under `dsp/` changed. Zero compiler warnings: fix them before running anything. The post-build copy into the system VST3 folder may fail with a permission error; the compilation still succeeded.
4. **Run sharded, never serial:**
   `node tools/run-gate.js --share 4 --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' membrum_tests`
   When a file under `dsp/include/`, `plugins/shared/` or `tests/test_helpers/` was MODIFIED, first run
   `node tools/header-consumers.js --tag '[membrum]'`
   (Membrum's cases carry no common tag, so `membrum_tests` itself is in its list: that is expected). Build every executable its last line names and add them to the same command, with `~[long]` appended to the filter:
   `node tools/run-gate.js --share 4 --filter '~[performance]~[perf]~[benchmark]~[!benchmark]~[long]' membrum_tests <exe> [<exe> ...]`
   The last line must read `GATE GREEN`. The gate runs the cases the uncommitted change reaches, so it runs before the commit. Every red case is yours to fix; none is "pre-existing". Timing cases (`[.perf]`, `[benchmark]`) run only through `node tools/run-cpu-tests.js membrum_tests`, alone, with nothing else executing, and only when the task says so; a budget is never relaxed and a workload never shrunk.
5. **pluginval, then clang-tidy, one heavy job at a time.** When plugin sources changed:
   `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"`
   The exit code is the verdict (0 passes; `0xC0000005` is a crash). Skip it for test-only, docs-only and generator-only changes. Before the commit:
   `./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja -Jobs 4`
   It analyses what changed since HEAD. Add a `-Target dsp` or `-Target shared` run when those trees changed, and `-Target membrum-fit` for T6.2. Zero warnings. Never run the build, the gate, pluginval and clang-tidy at the same time.
6. **Commit.** `git add <the files of this task>`, write the message to a file with the Write tool, then `git commit -F <message file>`. One git operation per command. Never `--amend`, never push. The message names the task id, the finding ids, the test that went from red to green and, where the task measured something, the numbers before and after.

Rules that hold for every task:

- **Thresholds are final.** Never relax a threshold, weaken an assertion, hide or re-tag a test, or shrink a workload to get a green run. If a threshold cannot be met, stop and report the measured value.
- **Nothing is removed to make a problem go away.** No control, interaction, parameter, exciter, body or kit is deleted unless the task says so and the user has ruled on it.
- **When to stop.** If the failing test cannot be made to fail, if the fix needs a decision the task does not make, or if the gate is still red after two attempts: stop, report what was measured, and continue with the next task that does not list this one as a prerequisite.
- **Prerequisites are hard.** A task starts only when every task in its Prerequisites line has been committed, or closed as refuted under VERIFY-FIRST.
- **Measuring with the audit probes.** The audit's probe sources are kept as text under `specs/membrum-audit/probes/` (`audit_probes.cpp.txt`: 39 cases through the real Processor, VoicePool, DrumVoice and Controller; `audit_quality_probes.cpp.txt`: one case per factory kit, a cross-kit case, one per parameter and a choke-table check; follow-up probes for the filter glide, the limiter, note-off, retune and velocity centroid). To re-run one before phase 8 promotes it:
  1. Copy `specs/membrum-audit/probes/<probe>.cpp.txt` to `plugins/membrum/tests/audit/<probe>.cpp` and append it to the `membrum_tests` source list in `plugins/membrum/tests/CMakeLists.txt`. Both are temporary and are never committed. The quality probe writes its CSV parts to a directory named at the top of its source: check that path first.
  2. Build (step 3), then `node tools/run-gate.js --share 4 --filter '[audit-probe]' membrum_tests`. Probe cases are hidden (`[.][audit-probe]`), assert only finiteness and print `PROBE <name> | <setup> | <metric> = <value> | threshold <bar> | <verdict>` lines into the per-shard logs of the gate's output directory.
  3. Compare with the baseline next to the sources: `probe-results.csv`, `kit-pads.csv` (per pad: peak, rms500, DC, decay, decay with a note-off at 100 ms, centroid, limiter margin, 24 band levels), `kit-pairs.csv` (band distance and the near-duplicate flag per pad pair), `param-audibility.csv`, `filter-glide.csv`, `limiter-pads.csv`, `noteoff-probe.csv`, `retune-probe-results.txt`, `velocity-centroid-results.txt`. Report sections 3 and 4 summarise them and define the metrics.
  4. Delete `plugins/membrum/tests/audit/` and revert the CMake line. `git status --porcelain plugins/membrum` must then show only the task's own files.

## Flags

A task's Flags line lists the flags below. Each flag adds obligations to the six steps; none replaces a step.

- **CROSS-PLUGIN IMPACT**: the task edits a file under `dsp/include/krate/dsp/` or `plugins/shared/` (T1.4 edits a shared test helper under `tests/test_helpers/`, which every plugin's suite includes: the same obligations apply).
  - Before editing, run `grep -rl "<header file name>" plugins/ dsp/include tests/` and list every consumer in the commit message. The consumers a task names were read during the audit; the grep is authoritative.
  - A shared root-cause fix is in scope: fix the shared file for every plugin instead of working around it in Membrum. Where the task says another plugin's output must stay bit-identical, the new behaviour is opt-in (a new setter, overload or accessor) and the default path is untouched.
  - The failing test for a shared DSP header goes in `dsp/tests/unit/<layer>/` (step 1).
  - Step 4's `header-consumers.js` run is mandatory, and every executable it names is built and gated. A consumer's golden that moves is diffed and justified in the commit message, never regenerated blindly.
  - Step 5 adds the `-Target dsp` or `-Target shared` clang-tidy run.
- **STATE-COMPAT**: the task changes the saved-state layout or the meaning of a stored value.
  - The versions move together: `kCurrentStateVersion` (`plugins/membrum/src/plugin_ids.h:33`, pinned by the `static_assert` at `:229`), `kBlobVersion` and `kPadBlobVersion` (`plugins/membrum/src/state/state_codec.h:108-109`), and `kVersion` in `tools/membrum_preset_generator.cpp:41`.
  - The plan uses ONE new version. The first STATE-COMPAT task that changes the layout or a stored meaning (T1.22) sets all four constants to 6; later STATE-COMPAT tasks keep 6 and append to that layout. Before reusing 6, run `git ls-remote --tags origin "refs/tags/membrum/*"`: if a build that writes 6 has been released in the meantime, move to 7 and keep a read path for 6.
  - A version that a released build wrote gets a read path that maps it into the current layout; it is never rejected. Released builds wrote 1 (v0.9.0, v0.9.1), 2 (v0.10.1, v0.10.2, with a 64-wide per-pad parameter stride) and 5 (v0.12.0 to v0.12.5); see MB-088 and MB-119. So every task that moves the version keeps a read path for 5, with a test that loads a version-5 blob captured from the code before the change and checks the migrated values, and T2.8 adds the read paths for 1 and 2. Versions no release wrote (3, 4, anything newer than current) are still rejected, with the current state left untouched.
  - Both blob kinds and both sides are covered: `readKitBlob` (host `setState`, kit presets) and `readPadPresetBlob`; `Processor::setState` / `applyKitSnapshot` / `applyPadPresetSnapshot`; `Controller::setComponentState` and the kit and pad load providers in `controller/controller_state.cpp` through `controller_state_codec.cpp`.
  - Regenerate the 20 factory kits with the generator (see PRESET-IMPACT) and confirm that `getState(setState(kit))` is idempotent for all 20.
  - Account for `plugins/membrum/tests/golden/*.bin`: `phase1_default.bin` is an audio golden (re-baseline it deliberately when rendered audio moves, and say why in the commit message); `phase2_state_v2.bin` is a version-2 state blob with no reader today (T2.8 decides its use).
  - Never swap a parameter's type at an existing ID. Never renumber a per-pad offset (`kPadBaseId` 1000 + pad x `kPadParamStride` 128 + offset) or a global ID. A new per-pad field takes the next free offset, a new global parameter a free ID below 1000, and a new sound slot is appended after the last one.
- **PRESET-IMPACT**: the task changes how a factory kit or the default kit sounds, through the engine or through kit data.
  - Kit data changes only through `tools/membrum_preset_generator.cpp`. Regenerate with
    `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target generate_membrum_presets -- -m:2`
    which writes `plugins/membrum/resources/presets/Kit Presets/<Category>/`. Never hand-edit a `.vstpreset`. Install through the build target (building `Membrum` copies the kits to the installed kit folder); never hand-copy a preset or a single VST3 file.
  - Keep 20 kits, 5 in each of the four fixed categories (Acoustic, Electronic, Percussive, Unnatural); the directory and the XML metadata must both match.
  - Keep the ring and range constraints: `maxPolyphony` in [4,16], `modeInject` above 0 rings on, Friction sustains. The audit measured that today's engine does neither of the last two (Friction is a bow transient of about 41 ms and `modeInject` decays with Decay: MB-124, MB-184). T6.10 owns that contradiction and needs the user's ruling; until it lands, no task changes a kit or the engine to move toward either reading.
  - Measure before and after. Run the kit-quality probe (see "Measuring with the audit probes") on the code before the fix and again after it, and list for every affected pad its peak, rms500, decay, centroid and limiter margin from `kit-pads.csv`, plus any near-duplicate pair that appears or disappears in `kit-pairs.csv`. The table goes into the commit message.
  - A change the user would hear as a different kit needs the user's ruling before the commit: show the table and stop. An engine fix whose audible change is the finding's stated purpose (for example, the pitch sweep now happens) still shows the table, but does not stop: the table goes into the commit message and the user reviews a phase's tables at the phase end (ruling 2026-11-24, last row of the rulings table).
  - Never retune kit data against a parameter or a model that a later task still has to fix. Before T5.6 a task may regenerate the kits only to re-encode them (same sound, new encoding), and the probe must show unchanged numbers.
- **PHYSICS**: the task replaces or adds a physical law, a mode table or a constant.
  - Every new value carries its source (authors, title, year, equation or table number) in a code comment next to the value AND in a test that checks the coded values against the published ones. The test's expected numbers are typed from the source, not computed by the code under test.
  - The sources are in report section 5. Where the report could not open a source, say so in the comment instead of citing it as read.
  - A PHYSICS task that changes a coded value is also PRESET-IMPACT. A PHYSICS task without PRESET-IMPACT (T4.17, T4.22) corrects comments and citations only and must not change rendered audio, unless the user rules for the model option the task describes.
- **UI**: the task changes the editor, a view or how a control is bound.
  - Never remove an interaction to fix a bug.
  - A change to more than about three controls in `plugins/membrum/resources/editor.uidesc` goes through an XSLT stylesheet (an identity transform plus the change, checked in under `specs/membrum-audit/xslt/`, applied with `npx xslt3 -xsl:<stylesheet> -s:plugins/membrum/resources/editor.uidesc -o:<output file>`), not through string replacement or hand edits.
  - Use VSTGUI's cross-platform classes only. The editor keeps exactly one `UpdateBanner`, one `CheckForUpdatesButton` and one `VersionLabel` (`node tools/lint-plugin-editor-baseline.js`).
  - After an editor change run `node tools/uidesc-inventory.js plugins/membrum/resources/editor.uidesc --check` (with the allow file once T3.2 has landed) and `node tools/gen-screenshots.js membrum`, which refreshes `plugins/membrum/images/`.
- **THREADING**: state arrives from the host's message thread (`Processor::notify`, `setState`) or crosses between the audio thread and the UI.
  - Hand state to the audio thread through a transfer object (`Steinberg::Vst::RTTransferT`, as Ruinae does), an atomic flag or slot (as Disrumpo does) or a lock-free SPSC queue. Never a lock, never a direct write to engine members, and the audio thread never allocates, frees or blocks while taking the state.
  - Tests: a deterministic interleaving test (one test thread steps the message-thread call and `process()` in a fixed order: a message between two blocks, two messages between two blocks, a message while voices ring) plus a two-thread stress case.
  - ThreadSanitizer builds only under GCC or Clang (`-DENABLE_TSAN=ON`, Linux or WSL, its own build directory; the nightly `tsan-nightly.yml` picks up every test file that spawns `std::thread` through `tools/tsan-scope.js`, so `node tools/tsan-scope.js --check` must pass). MSVC has no TSan: on Windows run the stress case under the ASan build (root `CLAUDE.md`, "AddressSanitizer") and state in the commit message which sanitizer runs were done and which are left to CI. Never shut down or restart WSL.
- **MACOS**: the task touches finiteness checks, a value that must be exact at a neutral point or an endpoint, an IEEE flag set, or an Apple-reserved name. The macOS leg compiles with `-ffast-math -ffp-contract=fast`; a green Windows run proves nothing about it.
  - Classify finiteness only with `Krate::DSP::detail::isFinite` / `isNaN` / `isInf` (`core/db_utils.h`): no `std::isnan` / `std::isfinite`, no bit-mask clone. No `numeric_limits<T>::infinity()` or `quiet_NaN()` constant in fast-math code (in an IEEE test TU build a NaN from a bit pattern through a `volatile`). Produce an exact value by a branch or a clamp, not by arithmetic that cancels. Clamp the argument of `pow` / `sqrt` / `log` at its domain edge.
  - An IEEE test TU needs all three flags on one line: `-fno-fast-math;-fno-finite-math-only;-ffp-contract=off` (the block at `plugins/membrum/tests/CMakeLists.txt:261-266` lacks the middle one today: MB-014).
  - Before the task is committed, and again before any push, run `tools/macos-fastmath-repro.sh` (WSL clang; it takes TUs under `dsp/tests/unit`), `node tools/lint-apple-globals.js`, `node tools/lint-ieee-flag-sets.js`, `node tools/check-portability.js <changed .cpp files>`, `node tools/lint-finite-bitmask.js` and `node tools/lint-nonfinite-symbols.js`. For plugin test TUs the fast-math proxy is `tools/clangcl-fastmath-build.ps1 -Targets membrum_tests` (read its blind spot). Where no proxy covers the change, write "unverified on macOS until CI" in the commit message.
- **VERIFY-FIRST**: the task contains a PLAUSIBLE finding (one skeptic refuted it, or a tie-break upheld it). The failing test of step 1 doubles as the verification. If it passes on current code, the finding is refuted: keep the test, change no production code for that finding, and record "refuted" with the test's output in the commit message and in the completion checklist. In a task that mixes CONFIRMED and PLAUSIBLE findings the rule applies per finding: only the PLAUSIBLE finding's part ends, and the rest of the task continues. The "Findings to tasks" table shows which findings are PLAUSIBLE.

## Phases

| Phase | Name | Goal | Tasks | Estimate |
|---|---|---|---|---|
| 1 | Critical and high defects: threading, state loss, real-time safety, voice lifecycle, timing, parameter plumbing | Remove the preset-load race, the data loss on preset save, the audio-thread allocations and stalls, the voices that never retire and the block-size-dependent pitch, release and note timing, then make every selected-pad control, macro and damping knob edit and show the pad it belongs to. | 25 | 5220 min (87 h) |
| 2 | Medium and low defects | Fix the remaining wrong-audio, lifecycle, gain-staging, parameter-display and editor-wiring defects, medium severity first (T2.1-T2.20), then low (T2.21-T2.32). | 32 | 4350 min (72.5 h) |
| 3 | Measured defects not already covered | Clear what the measurement lane and the portability review found and no earlier task owns: test guards that are vacuous under macOS fast-math, the failing uidesc lint, pad classification, limiter hits on resonant pads and the inexact Mode Stretch neutral point. | 5 | 570 min (9.5 h) |
| 4 | Physical-model corrections | Bring coupling, the string, the mode tables, damping, the exciters, tension and the bus limiter in line with the cited literature, every new value carrying its source in the code and in a test, before any kit is retuned. | 26 | 5310 min (88.5 h) |
| 5 | Dead and weak parameters, then kit data | Make the remaining dead or weak controls audible (T5.1-T5.5), then retune the default kit and the factory kits through `tools/membrum_preset_generator.cpp` against the corrected engine (T5.6-T5.13). | 13 | 1860 min (31 h) |
| 6 | Duplication, parallel paths, conformance and documentation cleanups | Reduce the hand-written field maps and registrations to one table each, delete dead code, and make comments, manual, site and project docs describe the engine that ships, without changing one rendered sample or one state byte. | 10 | 1650 min (27.5 h) |
| 7 | Test-gap fills | Replace tests that cannot fail or that measure the wrong thing with tests that pin the real behaviour. | 4 | 540 min (9 h) |
| 8 | Audit probes promoted to regression tests | Turn the audit probe rig and its kit-quality, parameter-audibility, velocity and tail measurements into un-hidden regression tests that assert the thresholds the report names. | 5 | 1260 min (21 h) |
| | Total | | 120 | 20760 min (346 h) |

Estimates are working time for one agent, including the capped build, the sharded gate, pluginval and clang-tidy; they exclude waiting for a ruling.

How the phases are cut:

- Findings with one root cause or one file region share a task, so a task can hold findings of several severities. The task sits in the phase of its most severe finding, unless its fix is a physical-model change (phase 4) or kit data (phase 5). Phase 1 therefore also holds 14 medium and 13 low findings, and T1.10 (medium) runs before T1.11 because a converging retune would otherwise turn note-off damping into a no-op (MB-269).
- Six high findings are fixed in phase 4, not phase 1: MB-004, MB-167, MB-232 and MB-258 (Snare Buzz, Tom Resonance and the coupling model, T4.1) and MB-208 and MB-210 (the string loop, T4.2 and T4.3). Their fix is a model change, and T4.1 to T4.3 open the phase.
- Within phases 2, 4 and 5 the order is severity first, then dependency. In phase 5 the parameter tasks come before every kit task whatever their severity.
- **Kit-data gate.** No kit is retuned (T5.6 to T5.13) before every PRESET-IMPACT task of phases 1 to 4 and T5.1 to T5.5 has been committed, closed as refuted, or ruled out by the user. A kit retuned against an engine that still changes has to be retuned again.
- Phase 6 changes no behaviour. Its tasks pass the same tests before and after, and `getState` of all 20 factory kits is byte-identical before and after each of them.
- Phase 8 needs the engine and the kits to be final: its thresholds are the ones the report names, measured against the fixed code, and its tests replace the temporary probe procedure above.

Tasks that stop for the user's ruling before their commit (every PRESET-IMPACT task also stops when its before/after table shows an audible change):

| Task | Question | Default this plan assumes | Ruling (user) |
|---|---|---|---|
| T1.20 | Selected-pad proxies stop being automatable, so a host lane recorded on a proxy ID no longer plays. Accept? | Yes: per-pad IDs carry automation; no parameter type or ID changes. | Accept (2026-11-24). Implement the default; no stop before the commit. |
| T1.22 | A version-5 state can hold macro deltas already baked into its fields. How does it load once macros are offsets? | The sound is kept: the stored field values stay what plays. Factory macro values are reset to neutral. | Keep macro positions (2026-11-24): a version-5 state keeps its saved macro values and its base fields are worked back as `base = clamp01(field - delta(macro))`; where a base would fall outside [0, 1] the sound may differ slightly, and the task reports how many pads that hits. Factory kits keep their macro positions too, with the same sound: the generator stores `base = field - offset` (follow-up ruling, 2026-11-24). No stop before the commit. |
| T1.23 | Which encoding replaces the hidden damping sentinel: an override relative to the derived law, or an explicit Auto / Manual state? | None: ask. Either way every existing state and factory kit keeps its sound. | Option R, relative override, no toggle (2026-11-24). No stop after step 1. |
| T1.5 (follow-up) | Re-arming the Impulse / Mallet exciter on a type swap calls `ImpactExciter::prepare` on the audio thread (the only way to re-seed its RNG); allocation-free only because the comb delay keeps its size. Keep, or change the shared exciter? | Raised by the run, 2026-11-24. | Shared-code fix (2026-11-24): correctness over the plan's "stop if `dsp/include` changes". Add a real-time-safe `ImpactExciter::restart(voiceId)` (re-seed the RNG, `reset()`, no buffer sizing, no transcendental setup); Membrum's Impulse and Mallet `restart` call it instead of `prepare`. CROSS-PLUGIN: Innexus uses `ImpactExciter` (it does not call the new method); gate its consumers. Done in the main loop before T2.5. |
| T1.6 | Raised by the run, 2026-11-24: (Q1) case (f) note-on cost stays red at p99 109.8 us vs 66.7 us; the remainder is the MB-005 whole-voice `std::swap` in `beginFastRelease` on same-note retriggers, owned by T2.5. (Q2) "Membrum: Size 0.0 peak > 400 Hz, Size 1.0 peak < 100 Hz (FR-033)" goes red: pad 0's 32 Hz pitch envelope and the body bin are within about 1 dB, and the slot no longer advances the ImpactExciter RNG in a probe, so the jitter realisation and the tie flip. | - | (Q1) Run T2.5 first (2026-11-24): T2.5 is pulled ahead of T1.6 (its only prerequisite is T1.5); (f) is then re-run unchanged, bar unchanged. (Q2) Make the test measure the body (2026-11-24): in that case set pad 0's pitch-envelope time (`kPadTSPitchEnvTime`) to 0 in the same block; the 400 Hz / 100 Hz bars stay. No per-trigger RNG re-seed. |
| T2.8 | Read paths for released versions 1 and 2, or a documented break? | Read paths (STATE-COMPAT rule). Per-pad automation IDs of the 64-wide stride cannot be remapped by the plugin; the CHANGELOG says so. | |
| T2.9 | FR-114 says a note-off does nothing to a percussion voice; since Phase 8A.5 it gates and damps, which cuts 180 of 185 long pads to under half their decay. Which is wanted? | None: ask. | |
| T4.1 | Wire Snare Buzz, Tom Resonance and Coupling Amount into a receiver model, or remove them? | Wire them: nothing is removed. | |
| T4.5 | Mode Stretch 1.0 was left non-neutral on Plate, Shell, Bell and NoiseBody by a user-approved decision in the 2026-06 audit. Change it? | None: ask. | |
| T4.20, T4.24 | New model features from PLAUSIBLE findings (two-mass head and shell, plate and cymbal pitch glide). Build them? | None: ask after the verification test. | |
| T4.25 | Strike normalisation as a ceiling changes the level of every pad. Adopt it? | None: ask. Without a ruling the normalisation stays and only the comments change. | |
| T4.26 | One millisecond of limiter look-ahead adds reported latency and delays the aux buses to match. Accept? | None: ask. | |
| T5.3, T5.4, T5.5 | Each finding offers "implement" or "relabel / document". Which? | None: ask. | |
| T6.10 | Friction sustain and `modeInject` ring-on are documented constraints the engine does not implement. Correct the documents, or build the behaviour? | Correct the documents; building the behaviour is a feature request. | |
| T5.7 to T5.13 | Every kit retune. | Show the before/after table per kit and ask. | |
| every PRESET-IMPACT task | Stop at each before/after table, or collect them? | Stop at each. | Collect (2026-11-24): when the audible change is the finding's stated purpose, the task does not stop; its table goes into the commit message and the user reviews all of a phase's tables at the phase end. A change beyond the finding's purpose still stops the task. This replaces every "stop for the ruling" line in a task section that concerns a purposeful change. T5.7 to T5.13 still stop per kit. |

## Findings to tasks

One row per verified finding (259), in report order: critical, high, medium, low, CONFIRMED before PLAUSIBLE within a severity. Every finding belongs to exactly one task. A PLAUSIBLE finding is VERIFY-FIRST inside its task (see "Flags").

| Finding | Title | Severity | Verdict | Task | Phase |
|---|---|---|---|---|---|
| MB-001 | Kit/pad preset browser loads (and setState) rewrite the voice pool, allocator, pad configs and coupling engine on the message thread while process() runs. A probe reproduces the crash. | critical | CONFIRMED | T1.1 | 1 |
| MB-008 | Modal retunes (pitch envelope, tension, Material Morph) converge with a time constant of 2 ms x host block size, so the 808 pitch sweep does not happen at normal buffer sizes | critical | CONFIRMED | T1.11 | 1 |
| MB-026 | Pitch envelope, tension glide and Material Morph barely move the sounding modes on the block path: the bank's smoothing step runs once per block, and the result depends on host block size (H-3 fix not effective) | critical | CONFIRMED | T1.11 | 1 |
| MB-027 | Feedback-exciter voices never retire: the slow path never calls flushSilentModes, so getNumActiveModes() stays above 0 forever | critical | CONFIRMED | T1.7 | 1 |
| MB-087 | MB-S01: Kit- and pad-preset SAVE build the blob from controller parameters: every pad's noiseLayerGain is saved as 0 (loads as 0.1), and damping sentinels are saved as explicit 0.5 overrides | critical | CONFIRMED | T1.3 | 1 |
| MB-162 | Kit/pad preset SAVE from the editor drops noiseLayerGain and replaces the body-damping 'auto' sentinel, so a saved kit reloads with quieter noise and shorter decays (lead 1 confirmed) | critical | CONFIRMED | T1.3 | 1 |
| MB-002 | An exciter or body type swap at note-on allocates and frees heap memory on the audio thread. The default kit allocates on 30% of hits; FMImpulse runs FFT wavetable generation. | high | CONFIRMED | T1.5 | 1 |
| MB-003 | The strike-normalisation probe renders 30 ms of scalar modal bank on the audio thread for almost every hit. One noteOn costs up to 1.4-3.5 ms and blocks miss their deadline. | high | CONFIRMED | T1.6 | 1 |
| MB-004 | Snare Buzz and Tom Resonance do nothing: the CouplingMatrix they recompute is never read by the audio path. Per-pad Coupling Amount is only an on/off gate. | high | CONFIRMED | T4.1 | 4 |
| MB-009 | Feedback-exciter voices never retire: the slow path never calls flushSilentModes, so getNumActiveModes() never reaches 0 | high | CONFIRMED | T1.7 | 1 |
| MB-017 | Feedback-exciter pads drive the unnormalised modal bank into its soft-clip rail: the output is a square wave pinned at ±0.707×velocity for 1-3 s, on every body, at every velocity, whatever the Feedback Amount | high | CONFIRMED | T1.16 | 1 |
| MB-018 | Feedback-exciter voices never retire after note-off (the slow path never flushes silent modes): a permanent voice-slot and CPU leak | high | CONFIRMED | T1.7 | 1 |
| MB-019 | Friction exciter has an inverted velocity response: soft hits drive the body into the hardClip rail while a full-velocity hit sits at the -6 dBFS budget | high | CONFIRMED | T1.17 | 1 |
| MB-020 | Exciter type swap allocates and runs FFT wavetable generation on the audio thread (about 190 us per FMImpulse swap); the allocation tests deliberately avoid the case | high | CONFIRMED | T1.5 | 1 |
| MB-028 | With Material Morph enabled, the pitch envelope (and tension glide) is fully overwritten every chunk or sample | high | CONFIRMED | T1.12 | 1 |
| MB-029 | With both damping overrides set (every factory pad), the per-pad Decay knob, its global proxy and the decay macros do not change a modal body's ring time, and Material loses its damping role | high | CONFIRMED | T1.23 | 1 |
| MB-030 | Extra-focus lead (1), damping part confirmed: controller-side kit and pad preset saves turn every sentinel pad into an explicit 0.5/0.5 override, cutting the default kick and tom ring to a fraction | high | CONFIRMED | T1.3 | 1 |
| MB-038 | Voices with the Feedback exciter never retire: the slow path never flushes silent modes, so getNumActiveModes() never reaches 0 | high | CONFIRMED | T1.7 | 1 |
| MB-042 | Tone-shaper SVF glides its cutoff and filter type over 5 ms from the slot's previous note: the attack of a filtered pad depends on voice-slot history | high | CONFIRMED | T1.15 | 1 |
| MB-043 | noiseLayerGain has no parameter and is lost on every controller-side preset save: the default-kit snare wire drops by 36 dB | high | CONFIRMED | T1.3 | 1 |
| MB-049 | Quietest stealing drops simultaneous hits: a voice triggered earlier in the same block has currentLevel 0, so the next note-on steals it | high | CONFIRMED | T1.9 | 1 |
| MB-050 | Allocator slots leak when a voice finishes before its MIDI note-off; with Quietest, polyphony collapses to one voice | high | CONFIRMED | T1.9 | 1 |
| MB-051 | Note events ignore sampleOffset: every hit starts at the block start, so timing and flams depend on the host buffer size | high | CONFIRMED | T1.14 | 1 |
| MB-052 | Auto-release uses a per-host-block peak, so voice length depends on buffer size; at 1-16 sample blocks hits are killed at onset | high | CONFIRMED | T1.13 | 1 |
| MB-067 | setupProcessing re-prepares the coupling engine and leaves its amount at 0: Global Coupling goes silent after a sample-rate change, an offline bounce or a project reload | high | CONFIRMED | T1.19 | 1 |
| MB-071 | MB: Simple-view macro knobs and the Coupling Amount knob always edit pad 1, whichever pad is selected | high | CONFIRMED | T1.21 | 1 |
| MB-072 | MB: Host automation recorded on any selected-pad proxy replays onto whichever pad is selected at playback | high | CONFIRMED | T1.20 | 1 |
| MB-073 | MB: Macro deltas never reach the controller, so knobs show pre-macro values, the next direct edit wipes the delta, and controller-side saves drop it | high | CONFIRMED | T1.22 | 1 |
| MB-074 | MB: The body-damping B1/B3 'derive' sentinel has no parameter representation: the knobs show 50% for a value never applied, and the first touch shortens decay several-fold | high | CONFIRMED | T1.23 | 1 |
| MB-081 | Punch macro below neutral collapses an un-enveloped pad's pitch to 20 Hz; Punch and Brightness do not return to the original sound at neutral | high | CONFIRMED | T1.22 | 1 |
| MB-096 | PolyphonySlider has no control listener, so dragging it never changes Max Polyphony and it never follows the parameter | high | CONFIRMED | T1.24 | 1 |
| MB-097 | XYMorphPad Y-axis (Morph End) edits use performEdit only and never update the controller's parameter, so kit and pad preset saves lose them | high | CONFIRMED | T1.25 | 1 |
| MB-105 | VoiceAllocator misuse: voiceFinished() on a still-Active allocator slot is ignored, and with the Quietest policy the pool collapses to one voice | high | CONFIRMED | T1.9 | 1 |
| MB-106 | String body: the shared bank's stale active-mode count after a body swap means a String voice never retires; on a fresh voice it is cut after 256 host blocks while still ringing | high | CONFIRMED | T1.8 | 1 |
| MB-136 | Every per-component allocation assertion is vacuous: AllocationScope::getAllocationCount() is read while the scope is still open, so it always returns 0 | high | CONFIRMED | T1.4 | 1 |
| MB-137 | SC-007 zero-allocation tests hide the known exciter/body-swap allocation by forcing every pad to Impulse+Membrane | high | CONFIRMED | T1.5 | 1 |
| MB-139 | No test covers the preset SAVE paths against the processor: the noiseLayerGain and body-damping loss (extra focus lead 1) ships green | high | CONFIRMED | T1.3 | 1 |
| MB-141 | No test checks note timing against Event::sampleOffset or render invariance across host block sizes; the SC-009 pitch-glide test runs only the per-sample renderer no host uses | high | CONFIRMED | T1.14 | 1 |
| MB-151 | Signal-path finding 'pitch envelope refresh is block-rate coarse' (findings.json confirmed[17]) is still present and was dropped from the remediation tracker: measured 1600-cent steps at 1024-sample blocks | high | CONFIRMED | T1.11 | 1 |
| MB-152 | R-202/R-203 partially fixed: the browser load path was unified but the performEdit replay still re-applies the kit (2119 edits, 162 matrix rebuilds, hybrid kits on interleave, divergent clamping), and the save providers still drop noiseLayerGain and the damping sentinel | high | CONFIRMED | T1.2 | 1 |
| MB-159 | Pitch envelope, tension glide and Material Morph barely move the modal bank in the processBlock renderers: the bank only steps ~1% per call toward the new target | high | CONFIRMED | T1.11 | 1 |
| MB-160 | Retirement depends only on the shared modal bank's mode count, which the String body never updates: a String voice is either cut after 256 host blocks or, after a kit switch, never retired | high | CONFIRMED | T1.8 | 1 |
| MB-163 | Automatable selected-pad proxies: recorded automation rewrites whichever pad is selected at playback, and the host's per-pad and proxy values go stale | high | CONFIRMED | T1.20 | 1 |
| MB-166 | Preset SAVE turns the body-damping -1 sentinel into an explicit 0.5 override, shortening untouched pads 5-16x | high | CONFIRMED | T1.3 | 1 |
| MB-167 | Tom Resonance (global 272) is a dead parameter for the same reason: it only rebuilds the unused coupling matrix | high | CONFIRMED | T4.1 | 4 |
| MB-173 | Brightness macro round trip is lossy: 0.5 -> 1 -> 0.5 leaves the filter cutoff at 0.8 instead of 1.0 | high | CONFIRMED | T1.22 | 1 |
| MB-208 | Physics (contradicted): Linear interpolation inside the loop gives exact fractional delay and keeps the loop gain flat (the code comment's claim) | high | CONFIRMED | T4.2 | 4 |
| MB-210 | Physics (contradicted): The pitch envelope retunes the String waveguide (audit H-3 fix) | high | CONFIRMED | T4.3 | 4 |
| MB-232 | Physics (contradicted): Cross-pad 'sympathetic resonance' rings resonators tuned to the STRUCK voice's own partials, not the unstruck pads | high | CONFIRMED | T4.1 | 4 |
| MB-237 | Physics (contradicted): The configured pitch sweep (start -> end over Time) actually reaches the modal body on the paths a host uses | high | CONFIRMED | T1.11 | 1 |
| MB-258 | Physics (contradicted): Cross-pad coupling makes idle drums ring in sympathy ('kick makes snare wires buzz', Snare Buzz / Tom Resonance route energy through a category matrix into other pads' excitation) | high | CONFIRMED | T4.1 | 4 |
| MB-260 | FMImpulse strike normalisation is measured only at velocity 1.0, but the FM carrier frequency moves with velocity: on Bell bodies a vel-100 hit lands up to 3.7x (+11.3 dB) over the body budget and reaches the limiter (Glass Bell Garden 46, Latin Perc 46, LinnDrum CR-78 40) | high | CONFIRMED | T1.18 | 1 |
| MB-265 | Tone-shaper filter is nearly dead on any voice slot whose last filtered state was open: the SVF g-smoother takes 20-55 ms (not 5 ms) to come down, so a hat at LP 317 Hz plays 46 dB louder on a fresh slot than on a warm one | high | CONFIRMED | T1.15 | 1 |
| MB-273 | Shared SVF with smoothing off: setMode / setGain / setResonance after prepare() leave the mix targets stale, and snapToTarget() restores them, so NoiseBurst, Clap and Friction hits run through the Lowpass mix instead of their Bandpass (found by the T1.5 implementer, 2026-11-24; not in report.md) | high | CONFIRMED | T1.26 | 1 |
| MB-109 | Body-damping default is written four different ways; factory kits store explicit B1/B3 on 429 of 459 pads, so Decay does not change the modal ring | high | PLAUSIBLE | T1.23 | 1 |
| MB-005 | Each voice steal or choke does std::swap of two 270 KB DrumVoice objects: a 270 KB temporary on the audio-thread stack and about 0.8 MB of copying per swap | medium | CONFIRMED | T2.5 | 2 |
| MB-006 | AuditionPad and AuditionPadOff share one atomic word, so a pad press and release between two audio blocks overwrite each other | medium | CONFIRMED | T2.4 | 2 |
| MB-010 | No NaN barrier anywhere: a non-finite pad field makes a voice emit NaN forever and never retire, and it latches the coupling energy limiter | medium | CONFIRMED | T2.2 | 2 |
| MB-012 | noteOff (and fast-release) damping is a per-sample radius factor, so the release time halves every time the sample rate doubles | medium | CONFIRMED | T1.10 | 1 |
| MB-014 | Membrum's NaN/Inf test guards are bit-mask clones or std::isfinite in fast-math TUs, so they are vacuous on the macOS leg | medium | CONFIRMED | T3.1 | 3 |
| MB-021 | StrikeNormKey omits FM Ratio and Friction Pressure: after editing either, the cached body gain is stale by 3-7 dB until some other key field changes on that slot | medium | CONFIRMED | T1.6 | 1 |
| MB-024 | Noise-based exciters and the click layer change level and balance with the sample rate (Violet NoiseBurst -10 dB at 96 kHz; Feedback seed impulse is a fixed 2 samples) | medium | CONFIRMED | T2.13 | 2 |
| MB-031 | The 'physical' Mode Stretch (1.0) stretches Plate, Shell, Bell and NoiseBody partials (bell nominal/hum +23 cents, plate top mode x1.89) because the bank's stretch ceiling was widened 10x with no re-baselining | medium | CONFIRMED | T4.5 | 4 |
| MB-032 | Strike-normalisation cache survives a sample-rate change, so cache-hit hits are about 6.8 dB too loud (44.1 to 96 kHz) or too quiet (the reverse) | medium | CONFIRMED | T1.6 | 1 |
| MB-033 | Membrane tension-glide depth scales with sample rate squared: 0.34 semitones at 22.05 kHz, 1.3 at 44.1 kHz, the 3-semitone clamp at 96 kHz and above | medium | CONFIRMED | T2.12 | 2 |
| MB-034 | Note-off damping is a fixed per-sample radius scale, so the release time scales with 1/sampleRate and compounds on every auto-release block | medium | CONFIRMED | T1.10 | 1 |
| MB-035 | String body skips strike normalisation and injects a fixed velocity-1.0 noise burst into its loop, so it peaks near the 1.0 soft-clip ceiling against the 0.5 budget of every other body | medium | CONFIRMED | T2.11 | 2 |
| MB-036 | Strike-norm probe runs a 30 ms render on the audio thread on almost every note-on, because the per-slot cache thrashes when slots rotate across pads | medium | CONFIRMED | T1.6 | 1 |
| MB-039 | A String voice whose shared bank still counts modes (kit switch or body swap while ringing) never retires and feeds stale partials to coupling | medium | CONFIRMED | T1.8 | 1 |
| MB-041 | The single-sample renderer process() diverges from the production block renderers, yet the SC-009 pitch-envelope test and the Phase-1 golden run only process() | medium | CONFIRMED | T2.1 | 2 |
| MB-044 | Tone-shaper bypass ignores the filter type: HP/BP jumps by 20 dB between cutoff 0.999 and 1.0 | medium | CONFIRMED | T2.15 | 2 |
| MB-045 | modeInject output is unscaled against the body budget: amount 1 pins the voice on its hardClip rail | medium | CONFIRMED | T2.16 | 2 |
| MB-046 | Noise-layer level depends on colour by up to 28 dB: Violet through the forced lowpass is nearly silent | medium | CONFIRMED | T2.14 | 2 |
| MB-053 | Ghost voices: frozen shadow voices are swapped back into main slots by choke / Quietest / poly-shrink and render with meta Free (unreleasable, uncounted, flagged silent) | medium | CONFIRMED | T2.6 | 2 |
| MB-054 | Kit load / setState hard-wipes every ringing voice with no fade (step click) | medium | CONFIRMED | T2.3 | 2 |
| MB-059 | Master gain is unsmoothed: a per-block step under automation (zipper noise) | medium | CONFIRMED | T2.10 | 2 |
| MB-068 | Coupling resonators are tuned to the pitch-envelope START frequency, and a String pad registers the stale modes left on its voice slot by another pad | medium | CONFIRMED | T2.17 | 2 |
| MB-077 | MB: Filter-envelope A/D/R values are displayed on a linear scale while the voice decodes them cubically | medium | CONFIRMED | T2.18 | 2 |
| MB-082 | Editor dims the pitch envelope and hides the morph power toggle on non-Membrane bodies, although the DSP drives both on every body | medium | CONFIRMED | T2.19 | 2 |
| MB-085 | Context-only controls shown as live for every exciter and body: the 4 exciter-secondary knobs, Air, Tension, the shell knobs, Wire, and noise colour/res at mix 0 | medium | CONFIRMED | T2.19 | 2 |
| MB-089 | MB-S05: A zero-sample process() call (VST3 parameter flush) returns before processParameterChanges, so the flushed parameter changes are lost | medium | CONFIRMED | T1.14 | 1 |
| MB-098 | XYMorphPad cursor is set only once at view build and never follows pad selection, automation or preset loads | medium | CONFIRMED | T1.25 | 1 |
| MB-099 | Per-pad parameter changes for the selected pad never refresh the editor's proxy-bound knobs and displays | medium | CONFIRMED | T1.20 | 1 |
| MB-101 | PadGridView meta provider always returns null, so the category glyph and the CG/BUS indicators (FR-065) are never drawn | medium | CONFIRMED | T2.20 | 2 |
| MB-115 | MB-PP-03: setupProcessing re-prepares the coupling engine (amount smoother snapped to 0) and, unlike polyphony and stealing, never re-applies the stored Global Coupling | medium | CONFIRMED | T1.19 | 1 |
| MB-116 | MB-PP-04: The default kit installed at initialize is a third kit-load route that skips the choke-table sync, so the default hi-hats do not choke until some kit is loaded | medium | CONFIRMED | T2.7 | 2 |
| MB-119 | Released builds wrote state versions 1 and 2 and used a 64-wide pad stride; the code says 'has not shipped' and rejects them wholesale | medium | CONFIRMED | T2.8 | 2 |
| MB-120 | The SC-023 8-voice 'worst case' CPU benchmark measures a bypassed Tone Shaper and Unnatural Zone: its per-voice setup is overwritten at every note-on | medium | CONFIRMED | T7.1 | 7 |
| MB-131 | Global proxy parameters are a hand-written 60-case switch that has drifted from the per-pad path (no reclassify or coupling-matrix refresh) | medium | CONFIRMED | T1.20 | 1 |
| MB-132 | Membrum's NaN/Inf and denormal sentinel tests are vacuous on macOS: 30 TUs use the forbidden bit-mask clone and 2 use std::isfinite | medium | CONFIRMED | T3.1 | 3 |
| MB-138 | SC-007 coupling cap test asserts peak < 10.0 on the post-limiter bus, which can never exceed 0.89 | medium | CONFIRMED | T7.2 | 7 |
| MB-140 | Almost every processor-level test uses one output bus, so the multi-bus VoicePool::processBlock that real hosts run is unpinned (and has drifted) | medium | CONFIRMED | T8.1 | 8 |
| MB-142 | The phase-1 golden is a self-regenerated change detector over the unused per-sample renderer, and it silently skips when the file is missing | medium | CONFIRMED | T2.1 | 2 |
| MB-148 | No test chokes a slot whose shadow is still fading from a steal (non-forced beginFastRelease early return), and the 'third steal inside 5 ms' case is uncovered | medium | CONFIRMED | T2.6 | 2 |
| MB-161 | With Material Morph on, the per-chunk morph refresh overwrites the pitch-envelope and tension scaling (the last updateModes in the chunk wins) | medium | CONFIRMED | T1.12 | 1 |
| MB-168 | Per-pad Coupling Amount (offset 36) only gates on > 0; its magnitude has no audible effect | medium | CONFIRMED | T4.1 | 4 |
| MB-171 | Kick pitch and decay depend on sample rate: tension modulation uses raw modal state energy, which grows with the number of excitation samples | medium | CONFIRMED | T2.12 | 2 |
| MB-175 | String body is not level-normalised: it peaks at -0.02 dBFS while every modal body peaks near -6 dBFS | medium | CONFIRMED | T2.11 | 2 |
| MB-183 | PadCategory classifies only 2 of 517 factory pads as Snare because the Snare rule requires the NoiseBurst exciter | medium | CONFIRMED | T3.3 | 3 |
| MB-186 | Experimental FX kit: metal hats 42/44 are bit-identical and 46 is within 1 dB (only Decay differs, and Decay is dead under b1/b3 overrides) | medium | CONFIRMED | T5.7 | 5 |
| MB-204 | Physics (contradicted): Strike-position weighting cos(m*theta)*cos(n*pi*rho) is the free-plate mode shape at the strike point | medium | CONFIRMED | T4.9 | 4 |
| MB-206 | Physics (contradicted): Bell Strike Position = strike azimuth, weighted by \|cos(m*theta)\| | medium | CONFIRMED | T4.10 | 4 |
| MB-209 | Physics (oversimplified): High-frequency decay (brightness) mapping: a fixed S per Material, applied once per period | medium | CONFIRMED | T4.4 | 4 |
| MB-213 | Physics (contradicted): NoiseBody's 64-mode cloud is dense enough to fuse into a cymbal wash | medium | CONFIRMED | T4.11 | 4 |
| MB-222 | Physics (contradicted): A 20 ms one-pole follower on modal energy reproduces the 'upward jump at the strike that relaxes back' (Avanzini et al.) | medium | CONFIRMED | T4.14 | 4 |
| MB-225 | Physics (contradicted): Impact (Impulse / Mallet) pulse: velocity shortens contact time by (1-v)^0.2 and sets force amplitude as v^0.6 ('Hertzian scaling') | medium | CONFIRMED | T4.12 | 4 |
| MB-229 | Physics (oversimplified): The parallel noise layer follows SMS deterministic+stochastic / PhISEM stochastic modelling of percussion | medium | CONFIRMED | T4.13 | 4 |
| MB-233 | Physics (contradicted): CouplingMatrix category routing (Snare Buzz: Kick->Snare, Tom Resonance: Tom->Tom) decides which pads excite which | medium | CONFIRMED | T4.1 | 4 |
| MB-234 | Physics (contradicted): NonlinearCoupling imitates the von Karman energy cascade: amplitude-dependent brightening and delayed HF bloom | medium | CONFIRMED | T4.15 | 4 |
| MB-236 | Physics (contradicted): Decay Skew inverts the Chaigne-Lambourg decay-vs-frequency law (fundamental dies, shimmer sustains) | medium | CONFIRMED | T4.8 | 4 |
| MB-244 | Physics (oversimplified): Modes with n >= 2 and (m >= 6, 1) left unshifted because 'Rossing's published data doesn't quantify them' | medium | CONFIRMED | T4.6 | 4 |
| MB-252 | Physics (oversimplified): The R = b1 + b3*f^2 per-mode damping law is used as the drum-head (Membrane) loss law | medium | CONFIRMED | T4.7 | 4 |
| MB-264 | The FR-114 'note-off is a no-op' test passes while the voice is gated off and damped; no test checks that a short MIDI note still plays the full hit | medium | CONFIRMED | T2.9 | 2 |
| MB-266 | The quality lane's 'ok' verdicts for Filter Type / Cutoff / Resonance / Env (offsets 7-10, 17-20) measured the coefficient glide, not the parameter; the US5-2 unit test's 'filter still closed at t=1 ms' premise is false | medium | CONFIRMED | T8.3 | 8 |
| MB-267 | SC-004 / SC-005 stay green because the exciter test measures the bare exciter's full-band centroid and the processor test was relaxed to 'either direction'; the spec 136 compliance row still claims > 2x | medium | CONFIRMED | T8.4 | 8 |
| MB-269 | Any fix that makes modal retunes converge (snap or per-sample smoothing) turns note-off damp() into a no-op: measured -60 dB at 3.45 s instead of 0.052 s | medium | CONFIRMED | T1.10 | 1 |
| MB-062 | Default kit: Closed, Pedal and Open Hi-Hat render identically, and so do the Crash and Ride pads | medium | PLAUSIBLE | T5.8 | 5 |
| MB-088 | MB-S03: Released builds wrote blob versions 1 and 2 (and per-pad IDs with stride 64); today's reader accepts only 5, so their projects and kits load silently as the default kit and automation lands on other pads | medium | PLAUSIBLE | T2.8 | 2 |
| MB-107 | noteOff damp() is undone by the per-chunk updateModes() of the pitch envelope, morph or tension, so the release length depends on the host block size | medium | PLAUSIBLE | T1.10 | 1 |
| MB-153 | Finding 17 and M-9 decisions rested on a false 'pre-release' premise: v0.10.2 shipped kChokeGroupId as a RangeParameter and a 64-wide pad stride | medium | PLAUSIBLE | T2.8 | 2 |
| MB-169 | Global Coupling at full amount adds no measurable level: the -46..-26 dB send plus the -20 dBFS energy limiter make it nearly inaudible | medium | PLAUSIBLE | T5.2 | 5 |
| MB-178 | Soft hits are brighter than hard hits on 10 of 60 notes: the noise layer ignores velocity while the body scales by about v^1.6 | medium | PLAUSIBLE | T4.13 | 4 |
| MB-214 | Physics (contradicted): Mode Stretch at its documented 'physical' default (1.0) leaves the Chladni ratios unstretched | medium | PLAUSIBLE | T4.5 | 4 |
| MB-223 | Physics (oversimplified): Glide depth follows strike strength (energy ~ velocity^2) and the physical membrane parameters | medium | PLAUSIBLE | T4.14 | 4 |
| MB-230 | Physics (unsupported): The noise and click layers are calibrated as ~-18 dBFS accents ~12 dB under the -6 dBFS body, a realistic body-to-noise balance | medium | PLAUSIBLE | T2.14 | 2 |
| MB-231 | Physics (oversimplified): Head-to-shell coupling is feedforward only (body drives shell, shell is summed into the output, nothing returns to the head) | medium | PLAUSIBLE | T4.20 | 4 |
| MB-007 | After the DrumVoice swap, FMOperator's wavetable pointer refers to the other voice slot's storage (latent use of a dead object) | low | CONFIRMED | T2.5 | 2 |
| MB-011 | Membrane strike-position amplitudes are wrong above strikePos ~0.93: the 20-term Bessel series is truncated, not merely imprecise in float | low | CONFIRMED | T4.18 | 4 |
| MB-013 | Default filter cutoff relies on pow(1000, 1.0) landing exactly on 20000 Hz to bypass the tone-shaper SVF; the macOS fast-math rewrite gives 19999.998 | low | CONFIRMED | T2.15 | 2 |
| MB-015 | Coupling energy limiter uses hard-coded per-sample coefficients, so its release time depends on the sample rate | low | CONFIRMED | T2.26 | 2 |
| MB-016 | VoicePool clamps the sample rate to [22050, 192000] while the rest of the processor runs at the host rate | low | CONFIRMED | T2.28 | 2 |
| MB-022 | ImpactExciter stops running its SVF the instant the pulse ends, cutting the filter's response mid-swing: soft Mallet hits on small bodies inject a step of up to 50% of their peak | low | CONFIRMED | T2.22 | 2 |
| MB-025 | Noise seeding is inconsistent across exciters: Clap and the click layer replay identical noise on every hit of a slot (machine-gun rolls), Friction jitter is identical on all voices, and the strike probe changes the next hit's noise only on a cache miss | low | CONFIRMED | T2.23 | 2 |
| MB-037 | Membrane strike amplitudes are wrong near Strike Position 1.0: the 20-term float Bessel series diverges (mode 46 gives 0.533 vs the true 0.176) | low | CONFIRMED | T4.18 | 4 |
| MB-047 | Pitch-envelope LUT rebuild (up to ~4,000 powf) runs on the audio thread at every note-on, even with the envelope off | low | CONFIRMED | T2.24 | 2 |
| MB-048 | NoiseLayer at mix 0 still runs the noise oscillator, SVF and ADSR every sample | low | CONFIRMED | T2.24 | 2 |
| MB-055 | Audition uses one atomic word: a pad-down and pad-up between two blocks (or two pads) overwrite each other, and a stale request fires on resume | low | CONFIRMED | T2.4 | 2 |
| MB-056 | Pan and output bus are re-read live for ringing and fading voices: automation or re-routing jumps a tail between buses in one block step | low | CONFIRMED | T2.25 | 2 |
| MB-060 | Aux routing duplicates the pad on main and sends it pre-master and unlimited, while the manual implies a separate route | low | CONFIRMED | T6.8 | 6 |
| MB-061 | NonlinearCoupling adds about +5 to +7 dB without make-up, and ModeInject adds up to 2.72 peak x amount on top of the 0.5 body budget | low | CONFIRMED | T4.15 | 4 |
| MB-064 | Main-bus silence flag is never set while Global Coupling > 0, and the coupling loop and limiter run forever with no voices | low | CONFIRMED | T2.26 | 2 |
| MB-065 | Peak meter shows one audio block's peak per UI tick with no hold: drum transients and overs are mostly missed | low | CONFIRMED | T2.31 | 2 |
| MB-066 | Pan law: the centre is not bit-unity (0.99999994) as the comment claims, a hard pan is +3 dB into the limiter, and the coupling feed drops 3 dB for panned pads | low | CONFIRMED | T2.27 | 2 |
| MB-069 | Coupling-engine noteOff is audibly a no-op, and the multi-bus pool path omits it, so the owner and refCount bookkeeping drifts | low | CONFIRMED | T1.9 | 1 |
| MB-070 | processor.h documents CouplingMatrixEdit / CouplingMatrixSnapshotRequest messages that notify() does not handle | low | CONFIRMED | T6.7 | 6 |
| MB-078 | MB: Size readout assumes the Membrane formula for every body; pitch-envelope params show raw 0..1 values labelled Hz/ms | low | CONFIRMED | T2.18 | 2 |
| MB-079 | MB: Defaults drift between global proxies, per-pad registrations, PadConfig and the generator | low | CONFIRMED | T6.3 | 6 |
| MB-080 | MB: Discrete decodes in the processor round where the SDK's toPlain floors, so intermediate automation values disagree with what the host displays | low | CONFIRMED | T2.30 | 2 |
| MB-083 | With the pitch envelope on, Size no longer sets the drum's pitch (absolute Hz), and the envelope's defaults (start = end = 20 Hz) detune any pad on which the user only raises Time | low | CONFIRMED | T5.4 | 5 |
| MB-084 | Authored macro values in the factory kits are cosmetic: 281 non-neutral macro settings never reach the sound | low | CONFIRMED | T1.22 | 1 |
| MB-086 | Kit sameness: the default kit has 12 distinct sounds on 32 pads, and across all 20 factory kits whole parameter groups never vary | low | CONFIRMED | T5.8 | 5 |
| MB-090 | MB-S07: Fresh instance: the controller registers generic per-pad defaults, while the processor plays DefaultKit; until a host-driven setComponentState arrives, the UI, host and preset saves describe a different kit | low | CONFIRMED | T2.7 | 2 |
| MB-091 | MB-S08: A kit load hard-wipes ringing voices with no fade, and leaves the bus limiter's gain reduction and the master gain as a step | low | CONFIRMED | T2.3 | 2 |
| MB-092 | MB-S09: The sound-slot to field mapping is hand-written in five places and the FR-061 skip list twice; clamping differs per route | low | CONFIRMED | T6.1 | 6 |
| MB-093 | MB-S10: Tests, goldens and comments name blob versions the codec rejects, and the codec's 'has not shipped' premise is false | low | CONFIRMED | T2.8 | 2 |
| MB-094 | MB-S11: setActive(true) wipes every voice but keeps the coupling engine, coupling delay and energy envelope, whose resonators keep the dead voices as owners | low | CONFIRMED | T1.19 | 1 |
| MB-095 | MB-S12: The processor reports kNoTail although voices and coupling resonators ring for seconds after the last event | low | CONFIRMED | T2.29 | 2 |
| MB-100 | notifyBusActivation is never called in production, so the Output Bus tooltip always warns "Host must activate Aux N bus" and the FR-043 reset never runs | low | CONFIRMED | T2.31 | 2 |
| MB-102 | Kit meter and pad glow show one audio block's peak per 30 Hz tick (last block wins, no peak hold), so drum transients and clips are mostly missed | low | CONFIRMED | T2.31 | 2 |
| MB-103 | Inline kit and pad browsers cycle by preset name only and keep a stale name after a project load or pad switch | low | CONFIRMED | T2.32 | 2 |
| MB-104 | modeToggleButton_ is write-only and never nulled; the toggle visibility is re-applied with invalid() on every timer tick | low | CONFIRMED | T2.19 | 2 |
| MB-108 | NoiseColor, NoiseOscillator's own parameter type, lives in core/pattern_freeze_types.h, so five Membrum headers include an Iterum Pattern Freeze header | low | CONFIRMED | T6.5 | 6 |
| MB-111 | Global proxy registrations drift from their per-pad twins (defaults and titles); Ctrl-click reset writes a different value than the per-pad default | low | CONFIRMED | T6.3 | 6 |
| MB-112 | Dead RegisteredDefaultsTable re-declares per-pad defaults in a 64-wide array beside 66 offsets | low | CONFIRMED | T6.4 | 6 |
| MB-113 | Secondary shell bank copies the B1 override formula with its own 0.4 fallback instead of using the body's real damping law | low | CONFIRMED | T4.19 | 4 |
| MB-114 | ADSRExpandedOverlayView exists as three plugin-local copies (Membrum, Ruinae, Innexus) | low | CONFIRMED | T6.6 | 6 |
| MB-117 | MB-PP-06: Applying one pad field through its selected-pad proxy ID skips the reclassify and recompute that the per-pad ID route runs, and the editor uses the proxy route | low | CONFIRMED | T1.20 | 1 |
| MB-118 | MB-PP-10: The two VoicePool::processBlock overloads have drifted: only the mono one releases coupling resonators when a voice finishes naturally, and production runs the multi-bus one | low | CONFIRMED | T1.9 | 1 |
| MB-121 | MANUAL.md describes a different engine: signal-flow order, exciter list, shell coupling, tension, air loading, meter, morph curves and RT safety disagree with the code | low | CONFIRMED | T6.8 | 6 |
| MB-122 | docs/index.html (published landing page) repeats false engine claims: 6 exciters, 40-mode Noise Body, bidirectional shell loop, String stretch control, RT pre-allocation | low | CONFIRMED | T6.9 | 6 |
| MB-123 | membrum_signal_flow.dot draws a different signal path: no bus limiter, a shell feedback edge, aux-only routing, proxies fanning out to all pads, noise and click added at the SVF | low | CONFIRMED | T6.9 | 6 |
| MB-124 | plugins/membrum/CLAUDE.md and the membrum SKILL state ring constraints the code no longer has (Friction sustains, modeInject rings undamped), and tests are skipped on that premise | low | CONFIRMED | T6.10 | 6 |
| MB-125 | drum_voice.h comments misdescribe the signal path, pitch-envelope refresh rate, note-off damp factor and shell-coupling timing | low | CONFIRMED | T6.7 | 6 |
| MB-126 | voice_pool.h header still describes Phase 3.0 stubs, a no-op note-off, a broadcast helper and a reserved stereo scratch that no longer match the code | low | CONFIRMED | T6.7 | 6 |
| MB-127 | state_codec.h documents a v3 / 57-slot / 468-byte format and 'single source' save paths that the code contradicts | low | CONFIRMED | T6.7 | 6 |
| MB-128 | processor.cpp documents the state as 'v4' with 34 sound slots and no globals; processor.h documents IMessage handlers that were removed | low | CONFIRMED | T6.7 | 6 |
| MB-129 | plugin_ids.h parameter comments disagree with the registered parameters (choice counts, defaults, ranges, types, per-pad ID range) | low | CONFIRMED | T6.7 | 6 |
| MB-130 | Spec 135 and spec 141 still mark the Tier 2 coupling-matrix editor (FR-050..053, FR-030/034 overrides) as shipped; it was removed in 0.9.0 | low | CONFIRMED | T6.10 | 6 |
| MB-134 | Dead and test-only API in production classes, including a choke setter that writes a different state than production uses | low | CONFIRMED | T6.4 | 6 |
| MB-135 | Header comments state the opposite of what the code does (proxies, selected pad, blob size, filter-env law) | low | CONFIRMED | T6.7 | 6 |
| MB-143 | Per-push gate runs 11 diagnostic cases that assert nothing (printf/WARN only), including one named 'should decay' | low | CONFIRMED | T7.3 | 7 |
| MB-144 | Infinite-ring kit sweeps skip every Friction pad on a false premise and have no lower bound, so tails cut early pass | low | CONFIRMED | T8.5 | 8 |
| MB-145 | Factory kits are only parsed, never rendered for level, clipping, DC or distinctness; the count check is >= 3 for 20 kits, and a hidden case overwrites all 20 with one kit | low | CONFIRMED | T8.2 | 8 |
| MB-146 | The pad-snapshot inverse test checks 17 of 59 slots and none of the late slots (34-58) where the hand-written maps can drift | low | CONFIRMED | T6.1 | 6 |
| MB-149 | No editor test toggles UiMode (the Simple/Advanced template swap) while the editor is attached, the path whose stale pointers already crashed once | low | CONFIRMED | T7.4 | 7 |
| MB-150 | Coupling fuzz test exercises a hand-written limiter that has drifted from the processor's | low | CONFIRMED | T7.2 | 7 |
| MB-154 | H-1 'safety rail (runaway/NaN only)' is not a NaN rail: hardClip is std::clamp, the bus limiter relies on it, and nothing on the output path classifies finiteness | low | CONFIRMED | T2.2 | 2 |
| MB-155 | L-6 and L-10 still open and silently dropped from the tracker: noise-layer Q up to 5 on the 'no spectral peak' Lowpass path; NoiseBurst Duration proxy default 5 ms vs per-pad 8.5 ms | low | CONFIRMED | T2.14 | 2 |
| MB-156 | Findings 9 / 10 / 17 / 18 and L-1 partially fixed: the generator and several comment sites were left behind (generator FM Ratio default 2.5x, b3 units and scale, Friction default, choke parameter type, 1/sqrt(N) + softClip narrative) | low | CONFIRMED | T5.6 | 5 |
| MB-164 | Sound-slot to parameter mapping is hand-written in five places; the FR-061 skip list twice, plus a hard-coded slot 51 in the controller (lead 3) | low | CONFIRMED | T6.1 | 6 |
| MB-165 | Dead or stale controller code: an unused forwardGlobalToPad, a write-only modeToggleButton_, an unused ui_mode.h, and comments with wrong sizes and laws | low | CONFIRMED | T6.4 | 6 |
| MB-170 | Coupling Delay (global 273) is inaudible: it delays a coupling return that is itself about 60 dB down | low | CONFIRMED | T5.2 | 5 |
| MB-172 | Noise-layer level drops about 6 dB at 192 kHz: white noise through a fixed-cutoff lowpass is not bandwidth-normalised to the sample rate | low | CONFIRMED | T2.13 | 2 |
| MB-176 | FM Ratio steps change level by 12 dB because StrikeNormKey omits fmRatio, so the strike gain cached for the old ratio is reused | low | CONFIRMED | T1.6 | 1 |
| MB-177 | ModeInject level is not scaled to the body budget: the 8-harmonic series peaks at about 2.7 x amount against a 0.5 body | low | CONFIRMED | T2.16 | 2 |
| MB-179 | Mode Stretch below 1.0 does nothing on Membrane: compression is clamped away | low | CONFIRMED | T5.3 | 5 |
| MB-182 | Main-bus silence flag is never set while coupling is on, even with no notes and a -240 dBFS output | low | CONFIRMED | T2.26 | 2 |
| MB-184 | Documented sustain constraints do not match the engine: Friction stops after about 41 ms and modeInject decays (D2) | low | CONFIRMED | T6.10 | 6 |
| MB-185 | Tabla kit: 6 near-duplicate pad pairs; pads 53/66 are copies, and 36/42 and 38/59 differ mainly in the dead Decay knob | low | CONFIRMED | T5.11 | 5 |
| MB-187 | 808 Electronic Kit: closed hat 42 and pedal hat 44 are near-duplicates (1.60 dB) | low | CONFIRMED | T5.9 | 5 |
| MB-189 | Jazz Brushes: closed hat 42 and pedal hat 44 are near-duplicates (0.55 dB) | low | CONFIRMED | T5.9 | 5 |
| MB-190 | Vintage Wood: closed hat 42 and pedal hat 44 are near-duplicates (0.50 dB); only the dead Decay and noiseLayerDecay differ | low | CONFIRMED | T5.9 | 5 |
| MB-191 | LinnDrum CR-78: closed hat 42 and pedal hat 44 are near-duplicates (0.29 dB); only the dead Decay and noiseLayerDecay differ | low | CONFIRMED | T5.9 | 5 |
| MB-192 | Acoustic Studio Kit: crash 1 (49) and crash 2 (57) are near-duplicates (1.23 dB) | low | CONFIRMED | T5.13 | 5 |
| MB-193 | Latin Perc: four near-duplicate pairs among the clave, castanet and shaker pads (47/63, 47/48, 48/63, 52/53) | low | CONFIRMED | T5.12 | 5 |
| MB-194 | Hand Drums and Latin Perc share bit-identical pads: Latin 36 = Hand Drums 42, and notes 38 and 40 are identical in both kits | low | CONFIRMED | T5.12 | 5 |
| MB-195 | uidesc-inventory --check fails on Membrum's editor: 160 overlaps from FieldsetContainer frames drawn as siblings of their controls, with no allow list | low | CONFIRMED | T3.2 | 3 |
| MB-199 | Physics (contradicted): Air Loading is described as a low-mode frequency depression that makes the drum deeper | low | CONFIRMED | T4.6 | 4 |
| MB-200 | Physics (unsupported): Mode Scatter models natural head imperfection and reproduces the (0,1)/(1,1) mode splitting of a real snare head | low | CONFIRMED | T4.17 | 4 |
| MB-201 | Physics (contradicted): Membrane inharmonicity (Stretch) uses the stiff-string law sqrt(1 + B n^2) with n = sorted mode index + 1, on by default via material | low | CONFIRMED | T4.16 | 4 |
| MB-211 | Physics (oversimplified): Tuning compensation for the loop filters' phase delay (fundamental lands on the mapped f0) | low | CONFIRMED | T4.2 | 4 |
| MB-212 | Physics (unsupported): The waveguide string is a linear system (the loop's only nonlinearity is a safety soft clip) | low | CONFIRMED | T2.11 | 2 |
| MB-216 | Physics (contradicted): Documented b1/b3 override ranges and the 'neutral' 0.5 default disagree with the coded denormalisation | low | CONFIRMED | T1.23 | 1 |
| MB-219 | Physics (unsupported): The noteOn comments cite Faust / STK / Bilbao for a 1/sqrt(N) output norm, but the code now does a per-configuration measured-peak normalisation that none of those references perform | low | CONFIRMED | T6.7 | 6 |
| MB-221 | Physics (contradicted): The String body is exempt from the strike normalisation and peaks 6 dB above the budget, at full scale | low | CONFIRMED | T2.11 | 2 |
| MB-243 | Physics (unsupported): Glide magnitude: '~2 semitones at max, matches JASA 2021 (Kirby & Sandler) tom-tom measurements', with velocity^2 depth | low | CONFIRMED | T4.23 | 4 |
| MB-245 | Physics (unsupported): Manual and header comments describe air loading as a low-mode depression curve that 'makes it deeper' | low | CONFIRMED | T4.6 | 4 |
| MB-246 | Physics (contradicted): 'modeScatter 0.28 reproduces the (0,1)/(1,1) mode splitting of a real snare head'; the coded low-mode ratios against the measured snare | low | CONFIRMED | T4.17 | 4 |
| MB-256 | Physics (contradicted): Secondary shell bank damping: references a fake body b1 on sentinel pads, and its b3 direction inverts the 'Material' semantic | low | CONFIRMED | T4.19 | 4 |
| MB-259 | Physics (contradicted): Coupling Delay above 1.5 ms 'simulates larger physical rooms where sympathetic resonance has to travel further' | low | CONFIRMED | T5.2 | 5 |
| MB-263 | The 30 ms strike-probe window misses the peak on only 2 of 383 modal factory pads, by at most 1.9 dB (critic cause (b) is not behind the limiter hits) | low | CONFIRMED | T1.6 | 1 |
| MB-270 | The magic-circle bank changes a ringing mode's amplitude when it is retuned, depending on sample rate: a pitch envelope starting at 2 kHz loses up to 4.4 dB per upper mode at 44.1 kHz even with ideal per-sample retuning, and up to 6.4 dB with 64-sample snaps | low | CONFIRMED | T2.21 | 2 |
| MB-023 | Feedback Amount and Friction Pressure are still effectively dead at the voice output; the regression tests that closed prior-audit Findings 4/5 only require a 1e-4 mean difference | low | PLAUSIBLE | T5.1 | 5 |
| MB-075 | MB: Kit-preset browser load re-applies the whole kit through about 2119 performEdits and 162 coupling-matrix recomputes in one block (lead 2 quantified) | low | PLAUSIBLE | T1.2 | 1 |
| MB-076 | MB: Per-pad edits never reach a sounding voice; only pan and output bus change ringing notes | low | PLAUSIBLE | T5.5 | 5 |
| MB-110 | Browser preset load applies every field twice by two different code paths; about 2,119 edits and 162 matrix rebuilds per kit load | low | PLAUSIBLE | T1.2 | 1 |
| MB-133 | Mode Stretch 'Phase-1 bit-identity' relies on arithmetic that does not cancel: the default modeStretch decodes to 0.99999946 | low | PLAUSIBLE | T3.5 | 3 |
| MB-147 | Tests pin the wholesale rejection of state versions 1 and 2, which released builds wrote; no fixture from a released tag is loaded | low | PLAUSIBLE | T2.8 | 2 |
| MB-157 | Finding 12 pattern reintroduced by the Finding 6 fix: grid view mutations from setComponentState / setParamNormalized bypass requestViewRefresh (and Finding 12's deferral has no test) | low | PLAUSIBLE | T2.20 | 2 |
| MB-158 | Extra focus (2): a kit-browser load applies the kit twice; the replay costs about 163 coupling-matrix recomputes and about 2,100 host edits, and can overwrite a later load's damping sentinel | low | PLAUSIBLE | T1.2 | 1 |
| MB-180 | Bell 'natural fundamental' is the nominal partial, two octaves above the lowest mode, so the pitch envelope and ModeInject target the wrong partial | low | PLAUSIBLE | T4.10 | 4 |
| MB-188 | 909 Drum Machine: closed hat 42 and pedal hat 44 are near-duplicates (0.28 dB) | low | PLAUSIBLE | T5.9 | 5 |
| MB-197 | Physics (oversimplified): Strike-position weighting is \|J_m(j_mn r0/a)\|, the radial mode shape at the strike point, with r0/a = 0.9*strikePos | low | PLAUSIBLE | T4.18 | 4 |
| MB-198 | Physics (oversimplified): Air loading tunes the (m,1) modes to Rossing's timpani series 1 : 1.50 : 2.00 : 2.44 : 2.90 and leaves all other modes at pure Bessel | low | PLAUSIBLE | T4.6 | 4 |
| MB-203 | Physics (unsupported): Exponent P = 1.7 is 'the measured-cymbal Chladni exponent reported by Rossing & Peterson' | low | PLAUSIBLE | T4.9 | 4 |
| MB-205 | Physics (contradicted): Default ("physical") Mode Stretch still warps the shell and bell ratios off their published series | low | PLAUSIBLE | T4.5 | 4 |
| MB-207 | Physics (unsupported): Secondary shell fundamental = head f0 x (1 - 0.75*secondarySize), default 0.625x, cited as a 'Chromaphone rule of thumb' | low | PLAUSIBLE | T4.19 | 4 |
| MB-215 | Physics (oversimplified): A linear modal bank plus enveloped noise reproduces a struck cymbal's time evolution | low | PLAUSIBLE | T4.11 | 4 |
| MB-217 | Physics (oversimplified): Membrane per-mode damping is monotone in f^2, but drum-head damping is radiation-dominated and depends on mode order: (0,1) decays FASTEST | low | PLAUSIBLE | T4.7 | 4 |
| MB-218 | Physics (contradicted): Every modal strike is normalised to one fixed peak (-6 dBFS at velocity 1), whatever the body's size, material, damping or strike position | low | PLAUSIBLE | T4.25 | 4 |
| MB-220 | Physics (oversimplified): An equal strike PEAK is not an equal LOUDNESS: peak normalisation turns low-pitched bodies into the quietest pads | low | PLAUSIBLE | T4.25 | 4 |
| MB-224 | Physics (unsupported): The hard ceiling kTensionPitchModMax = 1.19 (~+3 st) reflects membrane saturation (Avanzini-Marogna-Bank) | low | PLAUSIBLE | T4.23 | 4 |
| MB-226 | Physics (contradicted): The click burst length (2-5 ms, shortened by velocity) represents the stick/beater contact time on a drum head | low | PLAUSIBLE | T4.22 | 4 |
| MB-235 | Physics (oversimplified): Gong / plate pitch glide is provided (left to the tension feature) | low | PLAUSIBLE | T4.24 | 4 |
| MB-239 | Physics (contradicted): The factory '808' and default kick pitch sweeps match TR-808 / TR-909 kick pitch behaviour | low | PLAUSIBLE | T5.10 | 5 |
| MB-240 | Physics (unsupported): SC-009 '808 kick' test pins the pitch envelope to 808 behaviour and covers the real render path | low | PLAUSIBLE | T1.11 | 1 |
| MB-241 | Physics (oversimplified): A zero-look-ahead, instantaneous-attack limiter is an acceptable brickwall stage for percussive material | low | PLAUSIBLE | T4.26 | 4 |
| MB-242 | Physics (oversimplified): The 80 ms one-pole release is a 'program-dependent recovery' | low | PLAUSIBLE | T4.26 | 4 |
| MB-247 | Physics (contradicted): (m,n) group assignments of the bell partials (nodal meridians per partial) | low | PLAUSIBLE | T4.10 | 4 |
| MB-248 | Physics (oversimplified): Contact time follows Hertzian scaling T ~ m^(2/5) k^(-2/5) v^(-1/5) | low | PLAUSIBLE | T4.12 | 4 |
| MB-249 | Physics (oversimplified): The struck body's size sets the mallet contact time (D4 piecewise cubic mass scaling) | low | PLAUSIBLE | T4.21 | 4 |
| MB-250 | Physics (contradicted): Mallet velocity mapping gives an 8 ms (soft) to 1 ms (hard) contact (header comment, spec FR-011 / spec 135) | low | PLAUSIBLE | T4.12 | 4 |
| MB-251 | Physics (contradicted): The noise component of the impact pulse is 'pinked' by a one-pole filter | low | PLAUSIBLE | T4.12 | 4 |
| MB-253 | Physics (unsupported): Shell/head damping comments cite 'Chaigne (1993) / Chaigne & Askenfelt 1993' as the source of the shell's b1 + b3*f^2 law | low | PLAUSIBLE | T4.19 | 4 |
| MB-255 | Physics (contradicted): The b3 ceiling (1e-3 s, 'widened empirically') and the default tom b3 (1.5e-4) damp drum-head partials far faster than measured | low | PLAUSIBLE | T4.7 | 4 |
| MB-257 | Physics (oversimplified): Wire buzz as a linear, peak-normalised follower of the batter-body modal energy (no contact threshold, no cavity delay) | low | PLAUSIBLE | T4.13 | 4 |
| MB-261 | FM Ratio's 12 dB adjacent-step jump is not the stale StrikeNormKey: the peak is normalised, but the sustained level is not, and the FM sidebands sit at fixed Hz, not the body's pitch, so they hit or miss the hat's long-ringing ~1.06 kHz mode | low | PLAUSIBLE | T1.18 | 1 |
| MB-262 | Resonant tone-shaper filter and modeInject add peak gain after the strike probe, so Experimental FX Kit FM pads sit at -1.7 to -3.4 dBFS and cross the limiter on some hits (critic cause (c), confirmed for these pads) | low | PLAUSIBLE | T3.4 | 3 |
| MB-268 | membrum-fit keeps an eighth hand-numbered offset-to-field map that stops at offset 58: the fitter cannot reach the enable, knee, mid-pitch, curve2, pan, wire-coupling or noise-gain fields | low | PLAUSIBLE | T6.2 | 6 |
| MB-272 | getModalEnergy() (the tension and wire-coupling driver) is not the smooth amplitude^2 its comment claims: for the magic circle, s^2 + c^2 ripples at twice the mode frequency, so a reading taken once per chunk is off by up to 31% | low | PLAUSIBLE | T4.14 | 4 |

## Phase 1 — Critical and high defects: threading, state loss, real-time safety, voice lifecycle, timing, parameter plumbing

Remove the preset-load race, the data loss on preset save, the audio-thread allocations and stalls, the voices that never retire and the block-size-dependent pitch, release and note timing, then make every selected-pad control, macro and damping knob edit and show the pad it belongs to.

### T1.1 — Kit and pad preset loads (and setState while active) reach the engine through a transfer object applied on the audio thread

**Findings:** MB-001 (critical, CONFIRMED).

**Flags:** THREADING.

**Prerequisites:** none.

**Estimate:** 300 minutes.

**Background (read before starting).** `Processor::notify` runs on the host's message thread. Its `"LoadPresetState"` branch (`processor.cpp:1222-1241`) parses the blob and calls `applyKitSnapshot(kit)` at `:1239`; its `"LoadPadPreset"` branch (`:1247-1267`) calls `applyPadPresetSnapshot` at `:1265`. `Processor::setState` (`:905-916`) calls `applyKitSnapshot` the same way. `applyKitSnapshot` (`:918-1003`) writes engine members that `process()` uses at the same time, with no hand-off: `voicePool_.resetAllVoicesForKitSwitch()` (`:927`, which resets 32 voices, the voice metas and the allocator, `voice_pool.cpp:268-281`), `voicePool_.setMaxPolyphony` (`:932`, `voice_pool.cpp:570-588`), every `padConfigMut(pad)` (`:937-942`), the choke table (`:945-946`), the plain `int selectedPadIndex_` (`:948`), `couplingEngine_.reset()`, `couplingDelay_.reset()` and the plain `float energyEnvelope_` (`:965-967`), `padCategories_` and `couplingMatrix_` (`:985-988`) and the MacroMapper cache (`:999-1002`). `applyPadPresetSnapshot` (`:1005-1018`) writes a pad config, its category, the matrix and the macro cache. `getState` (`:871-903`) reads `voicePool_.padConfig(pad)` and `selectedPadIndex_` on the host thread while `processParameterChanges` writes them on the audio thread. Only the audition messages use a hand-off today (`pendingAudition_`, `:1198`, `:1214`). The audit's two-thread probe (real `voice_pool.cpp`, a kit apply every 2 ms against `noteOn` + `VoicePool::processBlock(128)`) crashed 3 of 3 runs (`0xC0000409`, a first build `0xC0000005`); the same code with a mutex around both sides ran 21826 blocks and 192 kit loads with 0 non-finite samples. The regression arrived in `536d8af3` (R-202 / R-203); `7ac00404` handled only the audition messages.

**1. Failing test.** New file `plugins/membrum/tests/unit/processor/test_preset_load_handoff.cpp`, added to the `membrum_tests` source list. Drive the processor directly: `Membrum::Processor`, `initialize(nullptr)`, `setupProcessing` (48 kHz, 256-sample blocks), `setActive(true)`, one `ProcessData` per block with a stereo main bus (copy the block runner shape of `processKbBlock` in `plugins/membrum/tests/unit/preset/test_kit_browser_load.cpp:126-156`; use `Krate::Test::EventList` from `tests/test_helpers/vst_event_list.h` for notes and `Krate::Test::ParameterChanges` from `vst_param_changes.h` for parameters). Send messages the way `test_morph_host_simulation.cpp:138-147` does: `Steinberg::owned(new HostMessage())`, `setMessageID("LoadPresetState")`, `getAttributes()->setBinary("bytes", ...)` (and `setInt("pad", n)` for `"LoadPadPreset"`). Build kit blobs with `State::writeKitBlob` from `DefaultKit::apply` plus distinct edits (copy the `defaultKit()` / `makeKitPreset` approach of `test_kit_browser_load.cpp:179-221`; at least two kits that differ in a pad's exciter and body type, `noiseLayerGain`, `maxPolyphony` and `globalCoupling`). The reference state for a kit is a second processor loaded with `setState` and processed for one block.

- `TEST_CASE("Preset loads from the message thread reach the engine only inside process()", "[processor][state][threading]")` with these SECTIONs, each of which steps the message-thread call and `process()` in a fixed order on the test thread:
  - "one kit message between two blocks": after `notify(LoadPresetState kit B)` and before the next `process()`, `voicePoolForTest().padConfig(p)` for every pad still equals kit A field by field, and `getState` already returns the bytes of the kit-B reference (the host contract: `getState` after a load returns the loaded state). After one `process()` block, `getState` equals the kit-B reference byte for byte. Red today: the pad configs change inside `notify`.
  - "two messages between two blocks": `notify(kit B)`, `notify(kit C)`, one block: `getState` equals the kit-C reference. Then `notify(kit B)`, `notify(LoadPadPreset pad 2, a pad blob from State::writePadPresetBlob)`, one block: the state equals kit B with that pad preset applied to pad 2 (reference: a processor given `setState(kit B)` then the same `LoadPadPreset`, each followed by a block). The order kit-then-pad must hold; a pad preset that arrives before a kit in the same gap is replaced by the kit.
  - "message while voices ring": note-ons on notes 36, 38, 41, 42 at velocity 1.0, six blocks, then `notify(kit B)`: until the next `process()`, `voicePoolForTest()` reports the same number of active voices as before the message, and `couplingEngineForTest()` and `energyEnvelopeForTest()` are unchanged. The next block's output is finite. Red today: `resetAllVoicesForKitSwitch` runs inside `notify`.
  - "setState while active": the same deferral and `getState` contract for `setState` on an active processor.
  - "setState while inactive": before `setActive(true)`, `setState(kit B)` then `getState` returns kit B and `voicePoolForTest().padConfig(p)` already equals kit B (no audio thread can exist yet, so the load applies at once).
  - "kit load keeps the processor's selected pad" (FR-052): set `kSelectedPadId` through a parameter change in a block, then `notify(LoadPresetState)` with a blob whose `selectedPadIndex` differs; after the next block `selectedPadIndexForTest()` is the parameter's pad. A `setState` blob's `selectedPadIndex` is applied.
- `TEST_CASE("Kit loads on a message thread while process() runs leave the engine consistent", "[processor][state][threading][stress]")`: a `std::thread` sends `LoadPresetState` alternating kit A and kit B every 2 ms (at least 150 loads) while the test thread runs `process()` with a note-on every fourth block on notes cycling through 36..67. Assert every output sample finite (`Krate::DSP::detail::isFinite`), no crash, and after the thread joins and two more blocks, `getState` equals the reference of the last kit sent. Today this case crashes or corrupts the allocator (the probe above): a crash in step 1 is the expected red; the gate's crash dump lands in `f:/tmp/crashdumps`.
- The file spawns `std::thread`, so `node tools/tsan-scope.js --check` must pass after it is added (the nightly TSan job picks it up).

**2. Fix.** Model the hand-off on Disrumpo's atomic slot (`plugins/disrumpo/src/processor/processor.h:457-593`: `PendingState`, `pendingSlot_` with Idle / Writing / Ready / Applying, `active_`, `lifecycleMutex_`, `consumePendingState`). Ruinae's `RTTransferT` (`plugins/ruinae/src/processor/processor.cpp:291`) is not enough here on its own: a second `transferObject_ui` discards an unconsumed first one, and a kit followed by a pad preset must both apply, in order.

- `plugins/membrum/src/processor/processor.h`:
  - Add a preallocated `PendingLoad` member: `bool hasKit`, `State::KitSnapshot kit`, `bool keepSelectedPad`, `std::array<bool, kNumPads> hasPadPreset`, `std::array<State::PadPresetSnapshot, kNumPads> padPresets`. Include `state/state_codec.h` (the forward declaration at `:36` is no longer enough for a by-value member). Search for the struct name first (ODR rule).
  - Add `std::atomic<int> pendingSlot_` with the four slot states, `std::atomic<bool> active_`, and a `std::mutex lifecycleMutex_` taken only by `setActive`, `setupProcessing`, `setState` and `getState` (never by `process()`; it closes the race of a host that calls `setActive(true)` on one thread while `setState` runs on another, as Disrumpo's comment at `processor.h:560-566` explains).
  - Make `selectedPadIndex_` a `std::atomic<int>` (written by `processParameterChanges` at `processor.cpp:398`, read by `getState`).
  - Add a triple-buffered published copy of the pad configs for `getState`: three `std::array<PadConfig, kNumPads>` plus an atomic index word, single writer (the audio thread while active, the lifecycle calls while inactive), single reader (`getState`). The writer copies `voicePool_.padConfigsArray()` into its back buffer and publishes it at the end of every block in which a pad config changed (a `padsDirty_` flag set by every `setPadConfigField` / `setPadConfigSelector` / `setPadChokeGroup` / `macroMapper_.apply` call in `processParameterChanges` and by the pending apply). It is never a lock and never allocates.
- `plugins/membrum/src/processor/processor.cpp`:
  - `notify("LoadPresetState")`: keep the parse on the message thread (the `std::vector` copy at `:1232` is allowed there). Instead of `kit.selectedPadIndex = selectedPadIndex_; applyKitSnapshot(kit);` (`:1238-1239`), acquire the slot (CAS Idle→Writing or Ready→Writing; yield while Applying), set `hasKit = true`, copy the kit, set `keepSelectedPad = true`, clear every `hasPadPreset` (a kit replaces pad presets that have not been applied yet), store Ready.
  - `notify("LoadPadPreset")`: acquire the slot the same way, set `hasPadPreset[pad]` and copy the preset (a pending kit stays and applies first), store Ready.
  - `setState`: parse, then publish into the slot the same way with `keepSelectedPad = false`. Put the publish of all three callers into one helper that takes `lifecycleMutex_` and, when `!active_`, consumes the slot right there (no audio thread can exist, so the load applies at once, as today).
  - Add `consumePendingLoad()`: CAS Ready→Applying; if it succeeds, run `applyKitSnapshot` when `hasKit` (keeping the current `selectedPadIndex_` when `keepSelectedPad`), then `applyPadPresetSnapshot` for each `hasPadPreset` pad in pad order, set `padsDirty_`, clear the flags, store Idle. Call it at the top of `process()`, after the `ScopedDenormalMode` guard and the `numSamples == 0` early return and BEFORE `processParameterChanges` (`:620`), so the block's own parameter changes land on the new kit. Also call it at the start of `setActive(true)` and `setupProcessing` (processing is stopped there).
  - `applyKitSnapshot` / `applyPadPresetSnapshot` keep their bodies; they now run only on the audio thread or while inactive. Confirm by reading that everything they call is allocation-free and lock-free: `VoicePool::resetAllVoicesForKitSwitch`, `setMaxPolyphony` (its `allocator_.setVoiceCount` returns a span), `SympatheticResonance::reset`, `DelayLine::reset`, `MacroMapper::invalidateCache` / `syncCacheFromCfg`. The hard voice wipe without a fade is MB-054 (T2.3); do not change it here.
  - `getState`: take `lifecycleMutex_`; build the snapshot from the latest published pad copy and the atomics (as today); if the slot is Ready (a load not yet applied), take it (Ready→Writing), overlay the pending kit (all pads and globals) and then each pending pad preset (apply `State::applyPadPresetSnapshot` to a copy of that pad's config, then `State::toPadSnapshot`), and put the slot back to Ready.
  - Remove nothing else. The audition hand-off stays as it is.
- Existing tests: any case that calls `setState` or sends a load message to an ACTIVE processor and then reads engine members (`voicePoolForTest`, `couplingEngineForTest`, `macroMapperForTest`, `couplingMatrixForTest`) without a `process()` call now sees the old state. That is the new contract, not a weakened assertion: add one empty `process()` block after the load in such a case and list every case changed in the commit message. Do not change any expected value.

**3. Build and gate.** Standard workflow. Step 5 adds the THREADING obligations: run the stress case under the ASan build (root `CLAUDE.md`, "AddressSanitizer"; `node tools/run-gate.js --bin build-asan/bin/Debug --filter '[threading]' membrum_tests`), run `node tools/tsan-scope.js --check`, and state in the commit message that TSan is left to `tsan-nightly.yml` unless it was run under WSL clang in its own build directory.

**4. Acceptance.** All SECTIONs of the first case green; the stress case green in the gate and in the ASan run, with at least 150 loads per run and 0 non-finite samples (baseline: 3 of 3 crashes). `grep -n "applyKitSnapshot\|applyPadPresetSnapshot" plugins/membrum/src/processor/processor.cpp` shows calls only from `consumePendingLoad`. `test_kit_browser_load.cpp` "Browser load leaves the processor in the same state as a host load" stays green. pluginval at strictness 5 exits 0.

### T1.2 — A browser preset load has one processor apply: no performEdit replay of the kit, no hybrid kit, one coupling-matrix rebuild

**Findings:** MB-152 (high, CONFIRMED; its load half is fixed here, its save half by T1.3), MB-075 (low, PLAUSIBLE), MB-110 (low, PLAUSIBLE), MB-158 (low, PLAUSIBLE).

**Flags:** THREADING, VERIFY-FIRST (MB-075, MB-110 and MB-158 are PLAUSIBLE: the step-1 tests are their verification, finding by finding).

**Prerequisites:** T1.1.

**Estimate:** 180 minutes.

**Background.** `Controller::kitPresetLoadProvider` (`controller/controller_state.cpp:120-190`) sends `"LoadPresetState"` (`:150-159`) and then replays the whole kit through `setAndNotify` (`:163-172`: `setParamNormalized`, `beginEdit`, `performEdit`, `endEdit` per ID, values clamped to [0,1]) via `ControllerState::applySnapshot` (`controller_state_codec.cpp:367-400`: 7 globals, then `applyPadSnapshotToParams` for 32 pads, `:183-272`). `padPresetLoadProvider` (`controller_state.cpp:217-275`) does the same with `"LoadPadPreset"` and `applyPadPresetSnapshotToParams` (`:261-271`). Every replayed edit reaches `Processor::processParameterChanges` and writes the field a second time; per pad it calls `recomputeCouplingMatrix()` five times (`processor.cpp:187` exciter, `:198` body, `:206` pitch-env time, `:215` coupling amount, `:227` Complexity macro, the last even when the macro did not change), plus `:415` (Snare Buzz) and `:422` (Tom Resonance). Measured (`specs/membrum-audit/probes/probe-results.txt:21-26`): one kit load = 2119 performEdit calls (2112 per-pad, 7 global), 2119 beginEdit, 2119 endEdit and 162 matrix rebuilds in the next block; two quick loads with the first kit's edits arriving after the second kit's message leave a hybrid kit (about 70 field types differ, snare max diff 0.4774 against a pure second-kit load); a blob with decay 1.7 ends at 1.7 through the message and at 1.0 through the edits. The replay was chosen on purpose by `specs/ruinae-audit/fix-plan.md:924` (T1.36: "The controller mirror keeps `ControllerState::applySnapshot` ... with performEdit") to give the host undo, automation and the dirty flag; this task replaces that mechanism, for Membrum only, with the VST3 calls meant for a preset change inside the plugin (`restartComponent(kParamValuesChanged)` and `IComponentHandler2::setDirty`). Say so in the commit message.

**1. Failing test.** Extend `plugins/membrum/tests/unit/preset/test_kit_browser_load.cpp`.

- Extend `KbRecordingHandler` (`:47-75`) to count `beginEdit` and `endEdit` calls and to record every `restartComponent` flag word. Add `IComponentHandler2` to it (`setDirty` recorded; `requestOpenEditor`, `startGroupEdit`, `finishGroupEdit` return `kResultOk`) and answer its iid in `queryInterface`, so `EditController::setComponentHandler` picks it up (`vsteditcontroller.cpp:165-170`).
- Add a test-only counter to the processor: `recomputeCouplingMatrix()` (`processor.cpp:131-143`) increments a plain `std::uint32_t` member, read through `couplingMatrixRecomputeCountForTest()` next to the other test-only accessors in `processor.h:79-82`.
- `TEST_CASE("Kit browser load sends no parameter edits and rebuilds the coupling matrix once", "[presets][kit][state]")`: `KitBrowserPair pair; pair.browserLoad(makeKitPreset(false));` then CHECK `handler.edits.empty()`, begin and end counts 0, a recorded `restartComponent` flag word containing `kParamValuesChanged`, and `setDirty(true)` recorded. Read the recompute counter, run `pair.forwardEditsAndProcess()`, CHECK the counter rose by exactly 1 (the pending apply's own call at `applyKitSnapshot`, `processor.cpp:988`). CHECK the controller's `getParamNormalized` for pad 4's coupling amount and pad 0's macros equals the kit's values (the UI still mirrors the load). Red today: 2119 edits and 163 rebuilds (MB-075, MB-110, MB-158 verification).
- `TEST_CASE("A stale edit batch from an earlier kit load cannot leave a hybrid kit", "[presets][kit][state]")`, the probe's interleave: `browserLoad(kit A)`, move `handler.edits` aside as batch A, `browserLoad(kit B)` (a kit that differs from A in exciter and body types on at least 8 pads, as `Jazz Brushes` differs from `Acoustic Studio Kit`; build it from `defaultKit()` with edits), process one block with batch A as parameter changes and a note-on on note 38, then one block with whatever edits B produced. CHECK `getState` equals a host-loaded kit B reference byte for byte, and the note-38 render of the first block matches a pure kit-B render (max abs diff ≤ 1e-6; baseline 0.4774). Red today (MB-158 verification).
- `TEST_CASE("Browser load and host load agree on out-of-range sound slots", "[presets][kit][state]")`: a kit blob with pad 0 `sound[2]` (decay) = 1.7 and `sound[0]` (material) = -0.3. After a browser load plus `forwardEditsAndProcess()`, `getState` equals a host load of the same bytes (baseline: 1.700 vs 1.000 and -0.300 vs 0.000). Red today (MB-110 verification). This test pins agreement only; whether slots 0-33 should be clamped at all is MB-092 (T6.1).
- The pad twin in `plugins/membrum/tests/unit/preset/test_pad_preset.cpp`: `TEST_CASE("Pad browser load sends no parameter edits", "[presets][pad][state]")`, same handler checks around `padPresetLoadProvider`, and the processor state after one block equals the existing "Browser pad load leaves the processor in the same state as applyPadPresetSnapshot" reference (`test_pad_preset.cpp:674`).
- VERIFY-FIRST: if a case passes on unfixed code, the finding it verifies is refuted; record that and leave its production change out.

**2. Fix.**

- `controller/controller_state.cpp`, `kitPresetLoadProvider`: replace `setAndNotify` (`:163-170`) with the direct setter `setComponentState` uses (`:83-86`, `EditControllerEx1::setParamNormalized` only, no begin / perform / endEdit). After `applySnapshot`, the uiMode write, `pushKitEnabledToGrid` and `syncGlobalProxyFromPad`, call `if (auto* h = getComponentHandler()) h->restartComponent(kParamValuesChanged);` and `setDirty(true)` (`EditController::setDirty`, `vsteditcontroller.h:90-91`). Keep the `"LoadPresetState"` message as the single processor apply.
- `padPresetLoadProvider`: the same change (`:261-271`).
- Update the comments that describe the replay: `controller_state.cpp:161-162` and `:259-260`, `controller.h:130-133` and `:170-173`, and the `ParamSetter` comment in `controller_state_codec.h:31-38` (the preset load no longer performs edits). The "macros FIRST" ordering comment at `controller_state_codec.cpp:197-209` describes a processor replay that no longer happens; reword it to say the order only matters for a notifying setter, and leave the order itself unchanged.
- `processor/processor.cpp`, `processParameterChanges`: replace the seven `recomputeCouplingMatrix()` calls (`:187`, `:198`, `:206`, `:215`, `:227`, `:415`, `:422`) with a local `bool couplingMatrixDirty = true;` assignment and call `recomputeCouplingMatrix()` once after the parameter loop when it is set. The Complexity branch (`:224-228`) marks dirty only when the pad's macros were dirty before `macroMapper_.apply` ran (`MacroMapper::isDirty`, `processor/macro_mapper.h:47-49`; `apply` returns early on a clean pad, `macro_mapper.cpp:63-75`). The coupling matrix is not read by the audio path today (MB-004, T4.1); keep it correct anyway.
- Nothing changes in the processor's apply: after T1.1 it is the only writer of a loaded kit.

**3. Build and gate.** Standard workflow. THREADING: the interleave case is the deterministic interleaving test this task needs; T1.1's stress case covers the two-thread run and must stay green.

**4. Acceptance.** For one kit browser load: 0 performEdit, 0 beginEdit, 0 endEdit (baseline 2119 each), 1 `restartComponent(kParamValuesChanged)`, 1 coupling-matrix rebuild in the next block (baseline 162 + 1). Interleaved loads end byte-identical to a pure second-kit load (baseline: about 70 field types differ, snare max diff 0.4774). The decay-1.7 blob ends at 1.7 on both routes. `test_kit_browser_load.cpp` "Browser load leaves the processor in the same state as a host load" and "Browser kit load silences the previous kit's voices and coupling" stay green. pluginval at strictness 5 exits 0.

### T1.3 — Kit and pad preset SAVE writes the processor's own state: noiseLayerGain and the damping sentinel survive a save and reload

**Findings:** MB-087 (critical, CONFIRMED), MB-162 (critical, CONFIRMED), MB-030 (high, CONFIRMED), MB-043 (high, CONFIRMED), MB-166 (high, CONFIRMED), MB-139 (high, CONFIRMED). MB-152's save half (T1.2 holds the finding) is closed by this task's tests too.

**Flags:** none.

**Prerequisites:** none. It edits `controller_state.cpp` after T1.2; if T1.2 was stopped, place the same fills in the load providers as they stand.

**Estimate:** 240 minutes.

**Background.** Both editor save buttons build the blob from controller parameters: the PresetManagers are created with a null processor (`controller.cpp:735-761`), so `PresetManager::savePreset` uses the state providers. `Controller::kitPresetStateProvider` (`controller_state.cpp:105-118`) calls `ControllerState::buildSnapshot` (`controller_state_codec.cpp:329-365`), which calls `buildPadSnapshotFromParams` (`:116-181`) per pad; `padPresetStateProvider` (`controller_state.cpp:196-215`) calls `buildPadSnapshotFromParams` directly and copies all 59 slots (`:210`). Two sound fields have no lossless parameter:

- `sound[57]` = `noiseLayerGain` (range 0.1..16) has no ParamID. `kLateSlots` (`controller_state_codec.cpp:58-93`) jumps from snapIndex 56 to 58 (comment `:91`), so `sound[57]` keeps the value-initialised 0.0 of `PadSnapshot::sound` (`state_codec.h:60`), and `State::applyPadSnapshot` / `applyPadPresetSnapshot` clamp it to 0.1 on load (`state_codec.cpp:243`, `:326`). The default kit uses 6.2 on the snare (`dsp/default_kit.h:120`), 3.5 (`:262`) and 2.2 (`:336`); the PadConfig default is 1.0.
- `sound[42]` / `sound[43]` (`bodyDampingB1` / `B3`) use -1 as "derive the damping from Decay and Material" (`state_codec.cpp:121-125`, `:213-222`). The per-pad parameters are [0,1] with default 0.5 (`controller.cpp:227-228`); every controller load skips the sentinel (`controller_state_codec.cpp:263-268`, `:309-316`), so the parameter keeps 0.5 or a stale value from an earlier kit, and the save writes it as an explicit override (b1 = 0.2 + 0.5 x 49.8 = 25.1 s^-1, `membrane_mapper.h:71-78`).

Only the processor's `getState` keeps both (`State::toPadSnapshot`, `state_codec.cpp:124-125`, `:147`). Measured (`specs/membrum-audit/probes/probe-results.txt:408-427`): every kit-preset save and reload differs at slot 57 on 32 of 32 pads; on the default kit also slot 42 on 22 pads and slot 43 on 16. Default snare (note 38): noiseLayerGain 6.2 -> 0.1, energy above 3 kHz -7.84 -> -42.35 dB, peak -9.52 -> -13.89 dBFS. Default kick (36): B1/B3 -1 -> 0.5, decay60 0.97 -> 0.19 s. Tom (41): 1.54 -> 0.23 s. Closed hat (42): 0.32 -> 0.02 s. Acoustic Studio Kit snare: energy above 3 kHz -12.83 -> -48.37 dB; 808 snare -23.13 -> -42.35 dB. A pad-preset save loses the same fields (pad 38: 6.2 -> 0.1; pad 41: B1 -1 -> 0.5, decay 1.54 -> 0.23 s). No test catches it: every save-provider test loads the blob back into a controller (`test_kit_uimode_roundtrip.cpp:44-128`, `test_pad_preset.cpp:78-153`, `:243`), and `test_pad_preset.cpp:662` starts from a hand-built blob.

The fix keeps the controller as the save source (an IMessage round trip to the processor is asynchronous in some hosts) and gives it an exact mirror of the two parameterless fields. It does not change the sentinel's meaning or its UI: how the sentinel is shown and edited is T1.23 (MB-074, MB-109), which needs the user's ruling and must keep this mirror working. A fresh instance's controller still registers generic defaults while the processor plays the default kit (MB-090, T2.7); T2.7 must fill this mirror too.

**1. Failing test.**

- In `plugins/membrum/tests/unit/preset/test_kit_browser_load.cpp`: `TEST_CASE("Kit preset saved from the controller reloads to the processor's own state", "[presets][state][save]")`. For each of three kit blobs, as SECTIONs: the default kit (`kitBytes(defaultKit())`, `:172-188`), `makeKitPreset(false)` and `makeKitPreset(true)` (`:195-221`; they carry noiseLayerGain 2.5 / 0.75 on pad 1 and explicit B1/B3 versus the sentinel on pad 5):
  1. processor P1 (`makeKbProcessor`), `hostLoad(P1, kit)`, one block, `bytes0 = processorStateBytes(P1)`;
  2. a `Membrum::Controller` C, `initialize`, `setComponentState(bytes0)`;
  3. `IBStream* s = C.kitPresetStateProvider();` copy its bytes, `s->release()`;
  4. a fresh processor P2, `hostLoad(P2, those bytes)`, one block;
  5. `CHECK(processorStateBytes(P2) == bytes0)`, with `CAPTURE` of the first differing byte and of P2's `padConfig(p).noiseLayerGain`, `bodyDampingB1`, `bodyDampingB3` for pads 0, 1, 2, 5, 6.
  Red today: slot 57 on 32 pads; slots 42/43 on the sentinel pads.
- Same file: `TEST_CASE("A sentinel pad loaded after an explicit one saves as the sentinel", "[presets][state][save]")`. Common setup: controller `setComponentState(makeKitPreset(false))` (pad 5 explicit 0.4 / 0.3), then `kitPresetLoadProvider` with the bytes of `makeKitPreset(true)` in a `MemoryStream` (as `KitBrowserPair::browserLoad`, `:258-265`; pad 5 sentinel). SECTION "untouched": `kitPresetStateProvider()`, parse it with `State::readKitBlob`, CHECK `pads[5].sound[42] == -1.0`, `sound[43] == -1.0` and `pads[1].sound[57] == 0.75`. Red today (the controller keeps the stale 0.4 / 0.3, MB-139). SECTION "edited": the user edits pad 5's B1 (`C.setParamNormalized(padParamId(5, kPadBodyDampingB1), 0.3)`, the path VST3Editor and host automation take) and the global proxy B3 with pad 5 selected (`setParamNormalized(kSelectedPadId, 5.0 / 31.0)`, then `setParamNormalized(kBodyDampingB3Id, 0.2)`); the saved `sound[42]` is 0.3 and `sound[43]` is 0.2 (an edited pad saves its explicit override).
- In `plugins/membrum/tests/unit/preset/test_pad_preset.cpp`: `TEST_CASE("Pad preset saved from the controller reloads to the processor's pad config", "[presets][pad][save]")`, SECTIONs for default-kit pads 2 (note 38, noiseLayerGain 6.2) and 5 (note 41, B1 sentinel): P1 default kit, `bytes0 = getState`; controller `setComponentState(bytes0)`, select the pad, `padPresetStateProvider()`; a fresh P2 with the default kit receives `"LoadPadPreset"` with those bytes and `pad` (a `HostMessage` as in `test_morph_host_simulation.cpp:138-147`), one block; `CHECK(P2 getState == bytes0)`. Red today: slot 57 on both, slot 42 on pad 5.

**2. Fix.**

- `controller/controller_state_codec.h`: declare, in `Membrum::ControllerState`, `struct PadParamlessFields { double noiseLayerGain = 1.0; bool dampingB1Derived = true; bool dampingB3Derived = true; };` (grep the name first: ODR rule). The defaults are `PadConfig`'s (`noiseLayerGain` 1.0, B1/B3 -1). Add `PadParamlessFields paramlessFieldsFromSnapshot(const std::array<double, 59>& sound) noexcept` (slot 57, and `sound[42] < 0.0`, `sound[43] < 0.0`).
- `controller_state_codec.cpp`: give `buildPadSnapshotFromParams` a third parameter `const PadParamlessFields& paramless` and, after the `kLateSlots` loop (`:159-163`), write `snap.sound[57] = paramless.noiseLayerGain;` and `-1.0` into `sound[42]` / `sound[43]` when the matching `...Derived` flag is set. Give `buildSnapshot` a third parameter `const std::array<PadParamlessFields, kNumPads>&` and pass each pad's entry. Fix the stale comment at `:158` ("Sound[34..51]").
- `controller/controller.h`: add `std::array<ControllerState::PadParamlessFields, kNumPads> padParamless_{};` next to `selectedPadIndex_` (`:181`).
- Fill the mirror on every load path, from the same snapshot the path already parses:
  - `Controller::setComponentState` (`controller_state.cpp:64-99`): every pad from `kit.pads[p].sound`.
  - `kitPresetLoadProvider` (`:120-190`): every pad from `kit`.
  - `padPresetLoadProvider` (`:217-275`): the target pad from `preset.sound`.
- Clear a pad's `...Derived` flag when its damping parameter is edited: in `Controller::setParamNormalized` (`controller.cpp:788-917`), (a) when `tag` is a per-pad ID whose offset is `kPadBodyDampingB1` / `kPadBodyDampingB3`, clear that pad's flag (the per-pad branch near `:893-914`); (b) in the proxy loop (`:862-891`), when `mapping.padOffset` is one of the two, clear the selected pad's flag. Neither the load paths (they call `EditControllerEx1::setParamNormalized` directly) nor `syncGlobalProxyFromPad` pass through the override, so they cannot clear a flag by accident. Place (a) before the `suppressProxyForward_` early return at `:842`, so a per-pad write made while the flag is set still counts.
- `kitPresetStateProvider` and `padPresetStateProvider`: pass `padParamless_` (or `padParamless_[pad]`).
- Do not add a parameter, an ID or a state slot (no STATE-COMPAT change).

**3. Build and gate.** Standard workflow.

**4. Acceptance.** All three save tests green: a controller kit save reloads byte-identical to the processor's own state for the default kit and both test kits (baseline: slot 57 differs on 32 of 32 pads, slots 42/43 on 22 / 16 default-kit pads), and the pad save reloads byte-identical for pads 2 and 5. Byte identity implies the measured losses are gone: default snare energy above 3 kHz stays -7.84 dB (baseline after save -42.35), kick decay60 stays 0.97 s (baseline 0.19), tom 1.54 s (0.23), closed hat 0.32 s (0.02). `test_kit_uimode_roundtrip.cpp` and the existing `test_pad_preset.cpp` cases stay green. pluginval at strictness 5 exits 0.

### T1.4 — AllocationScope counts while its scope is open, so the per-component allocation matrix can fail

**Findings:** MB-136 (high, CONFIRMED).

**Flags:** CROSS-PLUGIN IMPACT (a shared test helper under `tests/test_helpers/`, included by about 130 test files across `dsp/tests` and every plugin's suite).

**Prerequisites:** none.

**Estimate:** 90 minutes.

**Background.** `TestHelpers::AllocationScope` (`tests/test_helpers/allocation_detector.h:111-131`) starts tracking in its constructor and stores the count in `count_` only in its destructor (`:117-119`); `getAllocationCount()` (`:121-123`) and `hadAllocations()` (`:125-127`) return `count_`, initialised to 0 (`:130`). An object cannot be read after its destructor ran, so every `scope.getAllocationCount()` in the repository reads 0 and every `CHECK(count == 0)` built on it cannot fail. `ThreadScopedAllocationScope` (`:149-184`) has the same shape; its comment (`:145-147`) documents the latch and tells callers to read `AllocationDetector::instance().getAllocationCount()` instead. Membrum alone has about 30 such sites in 25 files (`test_exciter_body_matrix.cpp:445-450`, `test_allocation_matrix.cpp:130-163`, `test_body_bank.cpp:121`, `test_exciter_bank.cpp:113`, the six body and seven exciter tests, `test_click_layer.cpp:56`, `test_noise_layer.cpp:69`, `test_tone_shaper.cpp:294/304/325`, `test_decay_skew.cpp:193`, `test_mode_stretch.cpp:187`, `test_mode_inject.cpp:186`, `test_material_morph.cpp:192`, `test_nonlinear_coupling.cpp:405`); `dsp/tests` and the Disrumpo, Profundum, Seraphis and Vorago suites use the same pattern (`grep -rn "\.getAllocationCount()" --include=*.cpp --include=*.h dsp/tests plugins tests` minus the `instance()` reads: 46 lines). Measured: `membrum_tests.exe "ExciterBodyMatrix144: all 168 combos audible finite bounded alloc-free"` passes (842 assertions) although its scope contains an exciter swap that allocates (MB-002, fixed by T1.5).

**1. Failing test.**

- `plugins/shared/tests/test_allocation_detector.cpp` (it owns `shared_tests`' operator overrides, `:46-47`): `TEST_CASE("TestHelpers_AllocationScope_CountsWhileOpen", "[test_helpers]")` with two SECTIONs. "AllocationScope": open a scope, call `TestHelpers::allocateObservably()` (`allocation_detector.h:190-195`; it is the probe `tools/lint-allocation-probes.js` requires), `REQUIRE(scope.getAllocationCount() >= 1)` and `REQUIRE(scope.hadAllocations())` while the scope is open. "ThreadScopedAllocationScope": the same on the opted-in thread. Both red today (0).
- `plugins/membrum/tests/unit/test_allocation_matrix.cpp` (it owns `membrum_tests`' overrides, `:34`): `TEST_CASE("AllocationScope counts inside an open scope in membrum_tests", "[allocation]")`, the same AllocationScope check, so the Membrum binary proves its own detector counts. Red today.

**2. Fix.**

- Before editing, run `grep -rl "allocation_detector.h" dsp/tests plugins tests` and keep the list for the commit message (the head's CROSS-PLUGIN grep names `dsp/include`; for this helper the consumers are in `dsp/tests`).
- `tests/test_helpers/allocation_detector.h`: `AllocationScope::getAllocationCount()` returns `AllocationDetector::instance().getAllocationCount()`; `hadAllocations()` returns `getAllocationCount() > 0`; remove `count_`; the destructor still calls `stopTracking()`. Make the same change to `ThreadScopedAllocationScope` (its destructor still restores the filter flag and the thread opt-in). Replace the comment at `:145-147` with one that says both scopes report the live count of the open scope, and that nested scopes share one counter (the inner constructor's `startTracking()` resets it). Update the matching comment in `test_allocation_detector.cpp:83-85`.
- Change no call site. Every site that was vacuous now measures; that is the point of the task.

**3. Build and gate.** The mandatory CROSS-PLUGIN step: `node tools/header-consumers.js --tag '[membrum]'`, build every executable its last line names plus `shared_tests` and `membrum_tests` in one capped build, then `node tools/run-gate.js --share 4 --filter '~[performance]~[perf]~[benchmark]~[!benchmark]~[long]' membrum_tests shared_tests <exe> [<exe> ...]`. A red case is an allocation the vacuous check hid. Classify every red before deciding anything, recording for each the case, the count and the call that allocates (break on `AllocationDetector::recordAllocation` in a debugger, or narrow the scope):

- A Membrum case whose allocation is the exciter or body swap at note-on (`ExciterBank::applyPendingSwap` / `BodyBank::applyPendingSwap` calling `prepare()`; the 168 matrix is the expected one) is MB-002's allocation and T1.5 removes it. Do not commit T1.4 alone with a red gate, do not weaken the case, and do not revert the helper. Continue straight into T1.5 on the same tree, and commit both tasks in ONE commit once T1.5's gate is GREEN; the message names both task ids and lists which cases went red at T1.4 and green at T1.5. This is the only exception to "one task, one commit" and to "prerequisites are committed first" in this plan.
- Any other red (another Membrum allocation, or any case in `dsp_*_tests`, `disrumpo_tests`, `profundum_tests`, `seraphis_tests`, `vorago_tests`, `shared_tests`) is a real allocation no task in this plan owns. Stop: report the list with the counts and the allocating call, leave the change uncommitted, and continue with the next task that does not depend on T1.4.

Step 5: `-Target shared` and `-Target membrum` clang-tidy runs. pluginval is skipped (test-only change).

**4. Acceptance.** Both positive controls green (baseline: 0 counted inside an open scope). Every consumer executable named by `header-consumers.js` is gated, and its reds are classified as above. With T1.5 landed in the same commit, the gate reads `GATE GREEN` and `ExciterBodyMatrix144: all 168 combos audible finite bounded alloc-free` measures real counts (reported in its `INFO` line) and passes.

### T1.5 — No heap allocation or wavetable generation when the exciter or body type changes at note-on

**Findings:** MB-002 (high, CONFIRMED), MB-020 (high, CONFIRMED), MB-137 (high, CONFIRMED).

**Flags:** none. The fix stays inside `plugins/membrum/src/dsp/`; if a change under `dsp/include/krate/dsp/` proves unavoidable, stop and report (it would make the task CROSS-PLUGIN).

**Prerequisites:** T1.4 (see T1.4 step 3: the two may be committed together).

**Estimate:** 300 minutes.

**Background.** A voice slot serves every pad, so a note on a pad whose exciter or body type differs from the slot's previous pad swaps types inside `DrumVoice::noteOn` on the audio thread: `pad_config_apply.h:32-33` sets the pending types, `bodyBank_.configureForNoteOn` (`dsp/drum_voice.h:244`) and `exciterBank_.trigger` (`:343`, and the strike probe at `:1338`) run `applyPendingSwap`. `ExciterBank::applyPendingSwap` (`dsp/exciter_bank.h:198-252`) default-emplaces a fresh alternative, which frees the old one's heap buffers, and calls its `prepare()` (`:235`); `BodyBank::applyPendingSwap` (`dsp/body_bank.h:177-213`) does the same (`:212`). Those prepares allocate: `ImpulseExciter` / `MalletExciter` -> `ImpactExciter::prepare` -> `combDelay_.prepare(sr, 0.055f)` (`impact_exciter.h:66`) -> `buffer_.resize` (`delay_line.h:316`); `FMImpulseExciter::prepare` (`exciters/fm_impulse_exciter.h:32-54`) -> two `FMOperator::prepare` (`fm_operator.h:103-117`, "NOT real-time safe"), each running `generateMipmappedFromHarmonics` with a local FFT and `std::vector` buffers; `StringBody::prepare` (`bodies/string_body.h:32-36`) -> `WaveguideString::prepare` (`waveguide_string.h:105-122`) -> two `DelayLine::prepare`. The comments that say otherwise are wrong: `exciter_bank.h:200` ("no heap"), `fm_impulse_exciter.h:17-19` ("only called from DrumVoice::prepare"), `drum_voice.h:1312-1317` ("the net allocation is unchanged"). Measured (audit rtprobe, global operator-new counter, 48 kHz): X->Impulse and X->Mallet 1 allocation / 16423 B inside `noteOn`; X->FMImpulse 4 / 32940 B with `noteOn` taking 548-795 us (189 us for the swap alone in a second probe); Membrane, Plate, Shell, Bell or NoiseBody -> String 2 / 32846 B; every other swap 0. A VoicePool with the default kit, maxPoly 8, random pads: 672 of 2250 note-ons allocated (30 %); `processBlock` 0. The tests avoid the case: `test_allocation_matrix.cpp:104-111` runs the swapping `noteOn` outside its scope on the premise that emplace never allocates, and every VoicePool allocation test forces Impulse + Membrane on all pads (`test_per_pad_dispatch.cpp:359-369`, whose comment blames an MSVC CRT allocation; `test_polyphony_allocation_matrix.cpp:73-75`; `test_coupling_fuzz.cpp:101-103`; `test_choke_allocation_free.cpp:48-49`; `test_voice_pool_allocation_free.cpp:44-45`).

**1. Failing test.** (With T1.4's helper fix in the tree, `AllocationScope` counts.)

- `plugins/membrum/tests/unit/test_allocation_matrix.cpp`: `TEST_CASE("AllocationMatrix: a noteOn that swaps the exciter or body type allocates nothing", "[allocation][rt-safety]")`. For every ordered exciter pair A != B (42 pairs) on a Membrane body, and every ordered body pair A != B (30 pairs) with the Impulse exciter: prepare a `DrumVoice` at 48 kHz, set type A, `noteOn`, 64 samples of `processBlock`, `noteOff`, drain 200 blocks of 64 (outside any scope; type A is now current), set type B, then inside an `AllocationScope` call `noteOn(kVelocity)` and one `processBlock` of 512; `CHECK(scope.getAllocationCount() == 0)` with `INFO` naming the pair. Red today on every pair ending in Impulse, Mallet, FMImpulse or String. Correct the warm-up comment at `:104-110` of the existing 36-combo case to say the swap is measured by this case.
- `plugins/membrum/tests/unit/voice_pool/test_per_pad_dispatch.cpp`: `TEST_CASE("SC-007: zero allocations with heterogeneous exciter/body pads", "[membrum][voice_pool][pad_config][rt-safety]")`, a copy of the 10 s fuzz of "SC-007: Zero allocations with 32 pads, stealing, and choke groups" (`:346-439`, `detector.startTracking()` / `stopTracking()`), with two SECTIONs for the pad types: "round robin" (pad p gets `ExciterType(p % 7)` and `BodyModelType(p % 6)` through `setPadConfigSelector`) and "default kit" (`DefaultKit::apply(pool.padConfigsArray())`, the probe's 30 % case). `REQUIRE(allocCount == 0)`. Red today. Replace the comment at `:360-366` of the uniform-type case with a pointer to this case (the uniform types stay; the MSVC explanation goes).
- `ExciterBodyMatrix144: all 168 combos audible finite bounded alloc-free` (`test_exciter_body_matrix.cpp:408`) is red after T1.4 and must turn green.
- Guard tests that must stay green through the change (they pass today and pin that a re-selected alternative behaves like a freshly prepared one):
  - `plugins/membrum/tests/unit/architecture/test_exciter_bank.cpp`: `TEST_CASE("ExciterBank: a re-selected exciter renders like a freshly prepared one", "[exciter_bank][rt-safety]")`. For each exciter X and one other type Y: bank 1 is prepared, set to X, triggered at velocity 0.8 and rendered for 4096 samples with `process(0.0f)`; bank 2 is prepared with the same voice id, renders X, then Y, then X again (trigger + 4096 samples each); bank 2's last render equals bank 1's (`max abs diff <= 1e-6`; both in one TU, so the values are expected identical). Set the secondary parameters (`setFMRatio`, `setFeedbackAmount`, `setFrictionPressure`, `setNoiseBurstContactMs`, `setMalletBodySizeHint`) to non-default values before the first trigger.
  - `test_body_bank.cpp`: the same for the six bodies through `configureForNoteOn` with a fixed `VoiceCommonParams` and `processBlock` of a fixed 64-sample impulse excitation.

**2. Fix.** Make every alternative allocation-free after `DrumVoice::prepare` by preparing all of them up front (the design `MANUAL.md:505` already describes: "ExciterBank and BodyBank pre-allocate every variant up front").

- `dsp/exciter_bank.h`: replace `std::variant<...> active_` with one member per alternative (a `std::tuple` of the seven, or seven named members) and keep `currentType_` as the selector. Dispatch through one private helper that switches on `currentType_` and calls the functor with the typed member, so `process`, `processBlock`, `withActive`, `reset`, `release`, `isActive` and `trigger` keep exactly one dispatch per call (the FR-001 / FR-002 contract in the comments at `:72-113`). `prepare(sampleRate, voiceId)` prepares all seven (setup thread). The setters (`setFMRatio`, `setFeedbackAmount`, `setFrictionPressure`, `setNoiseBurstContactMs`, `setMalletBodySizeHint`) keep caching their value and write it into their alternative.
- `applyPendingSwap` becomes: select `pendingType_` (map `kCount` to Impulse as today), then restore the selected alternative to its post-`prepare` state WITHOUT calling `prepare`, then replay the cached secondary parameters (as `:240-251` do now). Read each alternative's `prepare` and `reset` (`exciters/*.h`) and split what `prepare` does into the allocating part (buffer sizing, `FMOperator` wavetable generation: done once in `ExciterBank::prepare`) and the state part (RNG seeds such as `ImpactExciter`'s `rng_.seed(voiceId)` and `FeedbackExciter`'s `seedNoise_.setSeed(0xFEEDu ^ voiceId)`, filter and envelope state, the FM ratios and feedback that `FMImpulseExciter::prepare` sets at `:38-41`). Give an alternative whose `reset()` does not already restore the full post-prepare state a non-allocating `restart(voiceId)` that does. The guard test above is the arbiter.
- `dsp/body_bank.h`: the same for the six bodies. `applyPendingSwap` keeps `sharedBank_.reset()` (`:181`) and restores the selected body without `prepare`; for `StringBody` that is `string_.silence()` plus whatever non-allocating state `WaveguideString::prepare` and `prepareVoice` set (`waveguide_string.h:105-128`: smoother configuration, `controlAlpha_`, `perceptualAlpha_`, `rng_.seed(voiceId)`). Do not change how the String body or the shared bank reports active modes; that is T1.8 (MB-106, MB-160).
- `setStringFrequency` and `refreshStringMaterial` (`:126-144`) use the String member directly; keep them no-ops unless String is current.
- Keep the `static_assert` intent (no throwing construction) in a form that fits the new members, or remove it with the variant it guards.
- Correct the three wrong comments: `exciter_bank.h:200`, `fm_impulse_exciter.h:17-19` (prepare now runs only from `DrumVoice::prepare`; make that true), `drum_voice.h:1312-1317`.
- `DrumVoice` grows (every alternative is now resident). Print `sizeof(DrumVoice)`, `sizeof(ExciterBank)` and `sizeof(BodyBank)` before and after in a temporary `INFO` and put the numbers in the commit message: each steal and choke `std::swap`s two whole voices on the audio thread (MB-005, about 270 KB each today, fixed by T2.5), so the growth matters there. Also time `VoicePool::prepare` once before and after (it now generates 2 FM wavetables per voice for 32 voices) and report it.

**3. Build and gate.** Standard workflow. The audio goldens (`plugins/membrum/tests/golden/*.bin` through `approval/test_phase1_regression.cpp`) and every render test must stay green without re-baselining: a moved golden means a re-armed alternative does not match a fresh one; fix the re-arm, never the golden. If T1.4 is still uncommitted, this task's gate is also T1.4's gate (T1.4 step 3).

**4. Acceptance.** 0 allocations for all 42 exciter pairs and 30 body pairs inside `noteOn` (baseline: 1 allocation / 16423 B for X->Impulse and X->Mallet, 4 / 32940 B for X->FMImpulse, 2 / 32846 B for X->String). 0 allocations in both heterogeneous VoicePool fuzz SECTIONs (baseline: 672 of 2250 note-ons allocated with the default kit). `ExciterBodyMatrix144` green with real counts. Both guard tests green. Goldens unchanged. pluginval at strictness 5 exits 0.

### T1.6 — Strike normalisation leaves the note-on path: a per-pad cached gain with a complete cache key

**Findings:** MB-003 (high, CONFIRMED), MB-036 (medium, CONFIRMED), MB-021 (medium, CONFIRMED), MB-032 (medium, CONFIRMED), MB-176 (low, CONFIRMED), MB-263 (low, CONFIRMED). The verifiers showed that the 12.2 dB FM Ratio jump quoted in MB-176 does not come from this cache: the quality probe built a fresh Processor for every step. MB-261 / T1.18 own that number. The stale-key defect MB-176 names is the same one as in MB-021, and this task fixes it.

**Flags:** PRESET-IMPACT, THREADING.

**Prerequisites:** T2.5 (user ruling, 2026-11-24: case (f)'s remaining cost is the whole-voice swap T2.5 removes). In plan order, T1.5 (allocation-free exciter and body swaps) runs before this task. This task swaps exciter and body types on a probe voice, and in its fallback path it does so on the audio thread, so it depends on T1.5's fix. If T1.5 was closed without a fix, stop and report.

**Ruling (user, 2026-11-24):** (Q1) T2.5 runs before this task; case (f) is re-run unchanged with its bar unchanged. (Q2) The FR-033 case "Membrum: Size 0.0 peak > 400 Hz, Size 1.0 peak < 100 Hz (FR-033)" (`membrum_processor_tests.cpp:824`) sets pad 0's pitch-envelope time (`kPadTSPitchEnvTime`) to 0 in the same block, so its dominant frequency is the body's; the bars stay. No per-trigger RNG re-seed.

**Estimate:** 360 min.

**What the code does today** (anchors read at `4a14376d`):

- `DrumVoice::noteOn` (`plugins/membrum/src/dsp/drum_voice.h:170-440`) calls the cached `measuredStrikeOutputGain(bank, params_, body, exciter)` at :302-304 for every modal body that is not driven by the Feedback exciter.
  - On a key miss (:1407) that call runs `probeStrikePeakExcited` (:1322-1363). The probe renders `0.030f * sr` samples (:1329) through `bank.processSample`, together with the live exciter and click layer.
  - `bank.processSample` goes through `processSample(ex, 1.0f)` (`dsp/include/krate/dsp/processors/modal_resonator_bank.h:374-377`, `:421-425`). That path calls `smoothCoefficients()` and the scalar `processSampleCore` on every sample.
- The cache (`StrikeNormKey` / `strikeNormKey_` / `strikeNormGain_`, :1717-1729) belongs to each DrumVoice, not to each pad, so two things defeat it:
  - `VoiceAllocator::findIdleVoiceOldest` (`voice_allocator.h` ~567-581) rotates the pads across slots.
  - `VoicePool::beginFastRelease` swaps the main and shadow voices (`voice_pool.cpp:922`).
- The key is missing several fields:
  - FM Ratio, Friction Pressure and Feedback Amount. All three reach the exciter (`pad_config_apply.h:58-60` → `ExciterBank::setFMRatio` / `setFeedbackAmount` / `setFrictionPressure`, `exciter_bank.h:146-169`).
  - The sample rate. `DrumVoice::prepare` (:119-164) also never clears the cache.
- `exciterBank_.setMalletBodySizeHint(size_)` runs at :340, after the probe at :302. A Mallet probe therefore uses the previous note's contact hint.
- The secondary (shell) bank's gain is probed without any cache on every note-on of a pad with shell coupling: :394-406 → `probeStrikePeak` (:1273-1299), a 20 ms scalar per-sample render.
- The 30 ms window (:1327-1329) misses peaks that build up slowly: Orchestral 49 peaks at 47.8 ms and Orchestral 52 at 40.5 ms.
- The probe uses the playing slot's own noise generators, and their seeds depend on the slot: `0xB357u ^ voiceId` in `noise_burst_exciter.h:52`, and the same pattern in `clap_exciter.h:60` and `feedback_exciter.h:72`. The same pad therefore gets a slightly different gain on each slot.

**Target design.** Step 2 gives the details.

- **Per-pad table.** VoicePool owns a table of 32 entries holding each pad's body gain and shell gain.
  - The key is the pad's whole `PadConfig` plus the sample rate, compared with a defaulted `operator==`. A key that covers the whole config cannot miss a field again.
- **Probe voice.** The gains are measured on a dedicated probe DrumVoice with a fixed voiceId, never on a playing voice. A gain is then a pure function of (PadConfig, sample rate): it no longer depends on the slot or on what played before.
- **When the gains are measured.** At most once per pad for each config change, and never because of slot rotation:
  - On the setup thread, in `VoicePool::prepare`, for every pad.
  - On a low-priority worker thread that `Processor::setActive(true)` starts, whenever a pad's config changes.
  - On the audio thread, as a fallback, only when a note-on arrives for an edited pad before the worker has delivered its result. This keeps every hit deterministic.
- **Note-on.** `DrumVoice::noteOn` does not measure when the pool has supplied the gains. A DrumVoice used on its own (DrumVoice unit tests, `tools/membrum-fit` through `pad_config_apply.h`) keeps its per-voice cache, with the key completed.

**1. Failing test**

Create `plugins/membrum/tests/unit/voice_pool/test_strike_norm.cpp` and add it to the `add_executable(membrum_tests` list. Cases (a) to (f) use only today's API, so they compile on unfixed code and fail on their values:

- **(a)** `TEST_CASE("Strike norm: an FM Ratio edit re-normalises the next hit on the same voice", "[drum_voice][strike_norm]")`
  - Setup: one `DrumVoice`, `prepare(48000.0, 0u)`. Config: `PadConfig` defaults with `exciterType = ExciterType::FMImpulse`, `clickLayerMix = 0.0f`, `fmRatio = 0.133333f`.
  - Steps: `applyPadConfigToVoice`, `noteOn(1.0f)`, render 4800 samples in 256-sample blocks. Set `fmRatio = 1.0f`, apply it, call `noteOn(1.0f)` again and read `v.bodyBank().getSharedBank().getOutputGain()`.
  - Reference: a fresh DrumVoice prepared the same way, configured with `fmRatio = 1.0f` from the start, one `noteOn(1.0f)`.
  - Assert: `std::abs(20 * log10(gain / ref)) <= 0.5`.
  - SECTIONs: `bodyModel` Membrane, Plate and Bell.
  - Fails today by 7.1, 5.5 and 4.4 dB (MB-021, keyprobe).
- **(b)** `TEST_CASE("Strike norm: a Friction Pressure edit re-normalises the next hit on the same voice", "[drum_voice][strike_norm]")`
  - Same as (a), with `exciterType = Friction` and `frictionPressure` going from 0 to 1, on Membrane.
  - Fails today by 3.4 dB.
- **(c)** `TEST_CASE("Strike norm: the body gain follows a sample-rate change", "[voice_pool][strike_norm]")`
  - Setup: a `VoicePool` prepared at 44100 Hz with block 256. Set pad 0 (note 36) through `padConfigMut(0)` to Mallet / Membrane with size 0.5.
  - Steps:
    1. `noteOn(36, 1.0f)`, one `processBlock`, then render 3 s so the voice retires.
    2. `prepare(96000.0, 256)`, then `noteOn(36, 1.0f)`.
    3. Find the slot through `voiceMeta` and read `mainVoiceRef(slot).bodyBank().getSharedBank().getOutputGain()`.
  - Reference: a fresh VoicePool prepared at 96 kHz with the same pad config, hit once.
  - Assert: `Approx(ref).epsilon(0.01)`.
  - Fails today by a factor of about 2.18, i.e. 6.8 dB (MB-032).
- **(d)** `TEST_CASE("Strike norm: a pad whose strike peaks after 30 ms stays inside the body budget", "[voice_pool][strike_norm]")`
  - Kit: load `plugins/membrum/resources/presets/Kit Presets/Acoustic/Orchestral.vstpreset` with the `loadKit` pattern of `specs/membrum-audit/probes/audit_limiter_probe.cpp.txt:71-94`. It uses `MEMBRUM_RESOURCES_DIR`, which `plugins/membrum/tests/CMakeLists.txt` already defines. Turn each snapshot pad into a `PadConfig` the way that file does further down (`State::applyPadSnapshot`; read the call there).
  - Pads: note 49 (pad 13, Mallet / Plate) and note 52 (pad 16, Mallet / Bell).
  - Post-chain: neutralise it exactly as `neutralAll` does (:167-176), and set `pan = 0.5f` so both pan gains are 1.0.
  - Render: a VoicePool at 48 kHz, 256-sample blocks, velocity 1.0, 1 s.
  - Assert: peak of main L divided by the pad's `level` ≤ 0.5 × 1.06 = 0.53.
  - Baseline (`specs/membrum-audit/probes/limiter-pads.csv` rows 64 and 67): 0.621 for pad 49, which fails; 0.523 for pad 52.
- **(e)** `TEST_CASE("Strike norm: a pad's gain does not depend on which voice slot plays it", "[voice_pool][strike_norm]")`
  - Setup: VoicePool at 48 kHz, `setMaxPolyphony(8)`, the Oldest policy. Pad 2 (note 38) is NoiseBurst / Membrane.
  - Steps: hit note 38 eight times. Before each hit, render 2 s and check with `voiceMeta` that the previous voice has retired, so the allocator moves the note across slots.
  - Assert: at least 4 distinct slots were used, and every hit's `getOutputGain()` on its slot equals the first within `epsilon(1e-6)`.
  - Fails today: each slot probes a different noise realisation. If the gains are already equal, check the slot count before you go on.
- **(f)** `TEST_CASE("Strike norm: note-on cost on rotating pads stays under 10 % of a 64-sample block at 96 kHz", "[voice_pool][strike_norm][wallclock]")`
  - Setup: call `DefaultKit::apply(pool.padConfigsArray())` BEFORE `prepare(96000.0, 64)`, then `setMaxPolyphony(8)`.
  - Pattern, over 600 blocks: every second block, hit the next of notes 36, 38, 42, 45, 49, 51 in round-robin. Every eighth block, hit three of them together.
  - Timing: time each `noteOn` with `std::chrono::steady_clock`. The first 32 note-ons are an untimed warm-up.
  - Assert: the 99th percentile is ≤ 66.7 µs, which is 10 % of the 667 µs block.
  - Baseline (MB-003 probe at 96 kHz / 64): 519 of 694 note-ons took over 100 µs, the worst 1368 µs.
  - The case asserts on its own wall clock, so it carries `[wallclock]`.
  - After the fix it must read ≤ 60 % of its bar on the dev box (≤ 40 µs; root `CLAUDE.md`, "Wall-clock bars"). If it does not, cut the case's fixed cost. Never raise the bar.

Run each case by name (head, step 1). Cases (a) to (e) must fail on their values, and (f) on time.

**1b. Tests that pin the new protocol.** These cases call the API of step 2, so they cannot be red first. Write them in the same file as soon as that API exists.

- **(g)** `TEST_CASE("StrikeNormTable: unchanged pads are never measured at note-on", "[voice_pool][strike_norm]")`
  - After `prepare`, 64 hits over four pads in round-robin leave `pool.strikeNorm().inlineMeasureCount()` at 0. MB-036 asked for at most 4.
  - Then edit pad 0's `material` and hit pad 0 twice: the count rises by exactly 1.
- **(h)** THREADING, deterministic interleaving. `TEST_CASE("StrikeNormTable: edits, worker drains and note-ons in a fixed order give the same gains", "[voice_pool][strike_norm][threading]")`
  - No thread runs. The test calls `setManualConsumer(true)` and calls `drainOnce()` itself.
  - Reference gain for a config: the gain a second, freshly prepared VoicePool at the same rate measured in its `prepare`.
  - SECTIONs:
    - Edit pad 0 between two blocks, then `drainOnce()`, then hit: the gain equals the reference, `inlineMeasureCount()` is unchanged and `workerMeasureCount()` rose by 1.
    - Edit, then hit before any drain: the inline count rises by 1 and the gain equals the reference. Then `drainOnce()` and hit again: the gain is unchanged and there is no further inline measurement.
    - Two edits of pad 0 between two blocks, then one `drainOnce()`: `workerMeasureCount()` rises by 1 only, because the first request is stale and skipped. The next hit's gain equals the reference of the second config.
    - Edit pad 0 while four voices of pads 0 and 1 ring: each ringing slot's `getOutputGain()` is unchanged by the edit and by the drain. Only the next note-on uses the new gain.
  - Compare with `Approx(ref).epsilon(1e-6)`. One compiled function produces both values, but the root rule forbids claiming bit equality.
- **(i)** THREADING, stress. `TEST_CASE("StrikeNormTable: worker thread against the audio thread under random edits", "[voice_pool][strike_norm][threading]")`
  - Setup: VoicePool at 48 kHz / 64, then `strikeNorm().startWorker()`.
  - Loop: 3000 blocks, driven by a fixed-seed `std::mt19937`. Each block makes 0 to 2 random edits on pads 0 to 3, then 0 to 3 note-ons, then `processBlock`.
    - Fields to edit: size, material, fmRatio, frictionPressure, exciterType, bodyModel, and secondaryEnabled together with couplingStrength.
    - After each note-on, record the pad, a copy of its PadConfig and `mainVoiceRef(slot).lastStrikeNormGains()`.
  - Then `stopWorker()`, and measure the reference for every record on a fresh pool.
  - Assert: every record matches its reference within `epsilon(1e-6)`, and every output sample is finite (`Krate::DSP::detail::isFinite`).
  - Print `workerMeasureCount()` and `inlineMeasureCount()`. The inline count is the number of hits that arrived before the worker had delivered.
- **(j)** `TEST_CASE("StrikeNormTable: note-on with a changed pad is allocation-free", "[voice_pool][strike_norm][allocation]")`
  - Edit pad 0's exciter from Impulse to FMImpulse and its body from Membrane to Bell. Then call `noteOn` inside an `allocation_detector.h` scope, which runs the inline measurement on the probe voice.
  - Assert: the count is 0. Read it after the scope has closed, never while it is open (MB-136 / T1.4).

**2. Fix**

1. **ODR check.** `grep -rn "class StrikeNormTable\|struct StrikeNormGains" dsp/ plugins/` must find nothing before you add either type.
2. **`plugins/membrum/src/dsp/exciter_bank.h`.** Next to `getPendingNoiseBurstContactNorm()` (:127-130), add the const getters `getPendingFMRatioNorm()`, `getPendingFrictionNorm()` and `getPendingFeedbackNorm()`. They return `pendingFMRatioNorm_`, `pendingFrictionNorm_` and `pendingFeedbackNorm_` (:262-264).
3. **`plugins/membrum/src/dsp/pad_config.h`.** Add `bool operator==(const PadConfig&) const = default;` to `struct PadConfig` (:182). PadConfig has `std::uint8_t` members with padding between them, so never compare it with `memcmp`.
4. **`plugins/membrum/src/dsp/drum_voice.h`.**
   - **Value type.** In `namespace Membrum`, before `class DrumVoice`, add `struct StrikeNormGains { float body = 1.0f; float shell = 1.0f; };`.
   - **Complete the standalone cache key.**
     - In `StrikeNormKey` (:1717-1727), add `fmRatio`, `frictionPressure`, `feedbackAmount` and `sampleRate`.
     - Fill them in the cached `measuredStrikeOutputGain(bank, p, body, exciter)` (:1400-1406): the first three from the new getters, the last from `static_cast<float>(sampleRate_)`.
     - Add a public `void invalidateStrikeNormCache() noexcept { strikeNormKey_ = StrikeNormKey{}; }` and call it at the end of `prepare(double, std::uint32_t)` (:119-164).
   - **Mallet hint order.** In `noteOn`, move `exciterBank_.setMalletBodySizeHint(size_);` (:340, with its D4 comment) to just before the gain block that opens at :246, so every probe uses this pad's own hint.
   - **Probe render.**
     - In `probeStrikePeakExcited` (:1322-1363), call `bank.prepareBlockSmoothing()` once before the loop and use `bank.processSampleNoSmooth(exc)` instead of `bank.processSample(exc)`. The bank was just set up or reset, so its smoothed arrays already equal their targets; values change by float rounding only.
     - Replace the fixed 30 ms window with an adaptive one. Track `peak` and `lastMaxIndex` (the sample of the last new maximum). Stop when `i >= 0.030 * sr` and `i - lastMaxIndex >= 0.010 * sr`, or at the latest when `i >= 0.100 * sr`.
     - Make the same two changes in `probeStrikePeak` (:1273-1299), with a 20 ms minimum.
     - Rewrite the window comments at :1267-1272 and :1327-1328.
   - **One-shot supply path.**
     - Private members: `float suppliedBodyGain_ = 0.0f;`, `float suppliedShellGain_ = 0.0f;`, `bool gainsSupplied_ = false;`, `float usedBodyGain_ = 1.0f;`, `float usedShellGain_ = 1.0f;`.
     - Public: `void setSuppliedStrikeNormGains(float body, float shell) noexcept`, which sets the first three, and `[[nodiscard]] StrikeNormGains lastStrikeNormGains() const noexcept`, which returns `{usedBodyGain_, usedShellGain_}`.
   - **Use the supplied gains in `noteOn`.**
     - In the modal non-Feedback branch (:302-304): `const float g = gainsSupplied_ ? suppliedBodyGain_ : measuredStrikeOutputGain(bank, params_, bodyBank_.getCurrentType(), exciterBank_.getPendingType()); bank.setOutputGain(g); usedBodyGain_ = g;`.
     - In the Feedback branch (:272-276) and the String branch (:277-285), keep what they set today (`1.0f`, `kBodyHeadroom / bank.getInputGainSum()`) and also record it in `usedBodyGain_`.
     - At :402-403: `const float sg = gainsSupplied_ ? suppliedShellGain_ : measuredStrikeOutputGain(secondaryBank_); secondaryBank_.setOutputGain(sg); usedShellGain_ = sg;`.
     - Set `gainsSupplied_ = false;` at the end of `noteOn`.
     - Rewrite the comments at :298-301 and :397-401 to say where the gain is measured now.
   - **Keep the standalone path.** Keep `measuredStrikeOutputGain` and the per-voice cache. They remain the path for a DrumVoice used without the pool, and `StrikeNormTable::measure` runs exactly this code.
5. **New `plugins/membrum/src/voice_pool/strike_norm_table.h` / `.cpp`** (`namespace Membrum`).
   - **Build setup.**
     - Add the `.cpp` to `add_library(membrum_core STATIC` in `plugins/membrum/CMakeLists.txt`, next to `src/voice_pool/voice_pool.cpp` (:178).
     - The controller TU already uses `std::thread`. Confirm that `membrum_core` links the threads library on Linux and macOS, and add `Threads::Threads` if it does not.
   - **Storage.**
     - Allocated in the constructor, on the host thread:
       - Two probe voices, `std::unique_ptr<DrumVoice> inlineProbe_, workerProbe_` (about 220 KiB each, like VoicePool's voice arrays).
       - A fixed ring, `std::array<Request, 64>` with `std::atomic<std::uint32_t> head_, tail_`, where `struct Request { int pad; std::uint32_t ticket; PadConfig cfg; };`. It has one producer (the audio thread) and one consumer (the worker, or a test thread that calls `drainOnce`).
     - Owned by the audio thread (plain members, one per pad): `PadConfig keyCfg_[kNumPads]`, `std::uint32_t ticket_[kNumPads]`, `StrikeNormGains gains_[kNumPads]`, `bool valid_[kNumPads]`, `bool retry_[kNumPads]`, plus `double keyRate_`.
     - Shared atomics (one per pad, each with exactly one writer thread):
       - `std::atomic<std::uint32_t> latestTicket_[kNumPads]`: the audio thread writes it and the worker reads it.
       - `std::atomic<std::uint32_t> resultTicket_[kNumPads]`, `std::atomic<float> resultBody_[kNumPads]` and `std::atomic<float> resultShell_[kNumPads]`: the worker writes them and the audio thread reads them.
   - **`static StrikeNormGains measure(DrumVoice& probe, const PadConfig& cfg) noexcept`.**
     - Defined in the `.cpp`, so both threads run one compiled copy.
     - Body: `probe.resetForKitSwitch(); applyPadConfigToVoice(probe, cfg); probe.invalidateStrikeNormCache(); probe.noteOn(1.0f); const auto g = probe.lastStrikeNormGains(); probe.resetForKitSwitch(); return g;`.
     - Calling `resetForKitSwitch` before the note re-seeds the noise generators (`NoiseOscillator::reset` re-seeds from `seed_`, `noise_oscillator.h` ~240), so a config always measures the same value.
     - Keep velocity at 1.0. T1.18 owns velocity-dependent FMImpulse normalisation.
   - **`void prepare(double sampleRate, const std::array<PadConfig, kNumPads>& pads)`** (setup thread).
     1. Call `stopWorker()`.
     2. Prepare both probe voices at `sampleRate` with `kProbeVoiceId = 0u`.
     3. For each pad that is not yet valid, or whose config differs from `keyCfg_[p]`, or every pad when the rate changed: measure with `inlineProbe_`, store `keyCfg_`, increment `ticket_`, store `gains_`, set `valid_ = true`, and store the ticket in `latestTicket_` (release).
     4. Set `keyRate_ = sampleRate`.
   - **`void syncAll(const std::array<PadConfig, kNumPads>& pads) noexcept`** (audio thread): calls `syncPad(p, pads[p])` for every pad.
   - **private `void syncPad(int p, const PadConfig& cfg) noexcept`.**
     - If `!(cfg == keyCfg_[p])`:
       - Copy `cfg` into `keyCfg_[p]` and increment `ticket_[p]`.
       - `latestTicket_[p].store(ticket_[p], std::memory_order_release)` and set `valid_[p] = false`.
       - Push a Request while a consumer is attached. When the ring is full, set `retry_[p]`.
     - Else, if `retry_[p]` is set, try the push again.
     - If `!valid_[p]` and `resultTicket_[p].load(std::memory_order_acquire) == ticket_[p]`, adopt the result: relaxed loads of `resultBody_[p]` and `resultShell_[p]`, then `valid_[p] = true`.
   - **`StrikeNormGains gainsForNoteOn(int p, const PadConfig& cfg) noexcept`** (audio thread).
     - Call `syncPad(p, cfg)`.
     - If `valid_[p]` is still false: `gains_[p] = measure(*inlineProbe_, cfg)`, `valid_[p] = true`, `++inlineMeasureCount_`.
     - Return `gains_[p]`.
   - **`bool drainOnce() noexcept`** (consumer).
     - Pop every queued Request.
     - Skip a request when `latestTicket_[req.pad].load(std::memory_order_acquire) != req.ticket`: a newer edit has superseded it.
     - Otherwise:
       1. `measure(*workerProbe_, req.cfg)`.
       2. Store `resultBody_` and `resultShell_` (relaxed).
       3. Store `resultTicket_ = req.ticket` (release).
       4. `workerMeasureCount_.fetch_add(1)`.
     - Returns whether anything was popped.
   - **`void startWorker()` / `void stopWorker()`** (setup or host thread, both idempotent).
     - Start sets `running_` and the consumer flag, then launches a `std::thread`. The thread body is `const Krate::DSP::ScopedDenormalMode guard;` followed by `while (running_.load(std::memory_order_acquire)) { if (!drainOnce()) std::this_thread::sleep_for(std::chrono::milliseconds(1)); }`. The guard puts the worker in the same FTZ/DAZ state as `process()`, so both threads measure the same value.
     - Stop clears `running_`, joins the thread and clears the consumer flag.
     - The destructor calls `stopWorker()`.
   - **Test hooks.**
     - `void setManualConsumer(bool)`: queue requests without a thread. Only while the worker is stopped.
     - `std::uint32_t inlineMeasureCount() const noexcept`.
     - `std::uint32_t workerMeasureCount() const noexcept` (atomic).
6. **`plugins/membrum/src/voice_pool/voice_pool.{h,cpp}`.**
   - Add the member `StrikeNormTable strikeNorm_;` and the public accessor `StrikeNormTable& strikeNorm() noexcept`.
   - In `VoicePool::prepare` (`voice_pool.cpp:52-122`), after the voice loop, call `strikeNorm_.prepare(sampleRate_, padConfigs_);`.
   - In both `processBlock` overloads (:306 and :443), call `strikeNorm_.syncAll(padConfigs_);` right after the argument guards.
   - In `noteOn`, NoteOn case (:227-233), between `applyPadConfigToSlot(slot, padIndex);` and `VP_VOICES[slot].noteOn(clampedVel);`, add `const auto g = strikeNorm_.gainsForNoteOn(padIndex, padConfigs_[static_cast<std::size_t>(padIndex)]); VP_VOICES[slot].setSuppliedStrikeNormGains(g.body, g.shell);`.
7. **`plugins/membrum/src/processor/processor.cpp`.**
   - `Processor::setupProcessing` (:1067): call `voicePool_.strikeNorm().stopWorker();` first.
   - `Processor::setActive(true)` (:1095-1101): call `voicePool_.strikeNorm().startWorker();` after `voicePool_.prepare(...)`.
   - `Processor::setActive(false)` (:1117-1125): call `voicePool_.strikeNorm().stopWorker();`.

**Behaviour before:**

- A note-on re-probes on any slot whose last key differs, which is most hits in a mixed pattern.
- The probe is a 30 ms scalar render on the audio thread.
- FM Ratio, Friction Pressure and sample-rate changes leave a stale gain.
- The gain depends on the slot and on what played before.

**Behaviour after:**

- A note-on reads its pad's gain.
- The probe runs on the setup thread in `prepare`, and on the worker after an edit.
- It runs on the audio thread only for a hit that arrives before the worker has delivered, once per pad per edit.
- The gain is a pure function of (PadConfig, sample rate).

**3. Build and gate**

- **Targets:** `membrum_tests Membrum`.
- **THREADING:**
  - `node tools/tsan-scope.js --check` must pass. Case (i) starts a thread through `startWorker`; if the tool asks for the file to be listed, list it and say so in the commit message.
  - Run cases (h) and (i) in the ASan build: `node tools/run-gate.js --bin build-asan/bin/Debug --share 4 --filter '[strike_norm]' membrum_tests`. Run it as the only heavy job.
  - Leave TSan to `tsan-nightly.yml`, unless a Linux / WSL TSan build directory already exists. Never shut down WSL.
  - Name in the commit message which sanitizer runs were done.
- **Golden:** `plugins/membrum/tests/golden/phase1_default.bin` (`approval/test_phase1_regression.cpp`). The default kit's gains move, from the fixed probe seed, the adaptive window, the hint order and the no-smooth path. If the approval case goes red, measure the change (peak and RMS per pad), justify it in the commit message, and only then re-baseline deliberately.
- **PRESET-IMPACT:**
  - Run the kit-quality probe before and after (head, "Measuring with the audit probes").
  - Also run `audit_limiter_probe.cpp.txt` before and after. Compare `limiter-pads.csv` (neutral1Norm, neutral1PeakMs) and `limiter-mallet-hint-history.csv`.
  - Fixing the hint order alone moves 38 of 101 Mallet pads by more than 1 dB (`limiter-probe-summary.txt`: World Metal 67 by 17.65 dB, Orchestral 52 by 8.71 dB). Expect an audible before/after table, and stop for the user's ruling before the commit.

**4. Acceptance**

- (a) An FM Ratio edit lands within 0.5 dB of a fresh voice on Membrane, Plate and Bell. Baseline: 7.1, 5.5 and 4.4 dB off.
- (b) A Friction Pressure edit lands within 0.5 dB. Baseline: 3.4 dB off.
- (c) After 44.1 → 96 kHz the gain is within 1 % of a fresh 96 kHz pool. Baseline: a factor of 2.18 (6.8 dB).
- (d) Normalised peak ≤ 0.53 for Orchestral 49 (baseline 0.621) and Orchestral 52 (baseline 0.523).
- (e) The same pad has the same gain on every slot.
- (f) Note-on p99 ≤ 66.7 µs at 96 kHz / 64, and ≤ 40 µs on the dev box. Baseline: worst 1368 µs.
- (g) Unchanged pads are measured zero times at note-on, and an edit costs exactly one measurement.
- (h) and (i) are green, also in the ASan build.
- (j) counts zero allocations.
- The commit message reports the wall time of `VoicePool::prepare` at 192 kHz with the default kit. Prepare measures all 32 pads on the setup thread; if it takes over 500 ms, stop and report.
- The commit message carries the kit-quality before/after table (peak, rms500, decay, centroid and limiter margin for every affected pad), and the user has ruled on it.

### T1.7 — Feedback-exciter voices retire: the per-sample renderer flushes silent modes

**Findings:** MB-027 (critical, CONFIRMED), MB-009 (high, CONFIRMED), MB-018 (high, CONFIRMED), MB-038 (high, CONFIRMED).

**Flags:** none. The fix calls the public `ModalResonatorBank::flushSilentModes()` and does not edit the shared header. The slow path then flushes as the block path already does, which only zeroes mode states below the bank's silence threshold, so kits do not change audibly.

**Prerequisites:** none.

**Estimate:** 60 min.

**What the code does today:**

- `DrumVoice::processBlock` sends every Feedback-exciter voice to `processBlockSlow` (`drum_voice.h:610-624`, `useSlowPath = feedbackExciter` at :620).
- `processBlockSlow` (:933-1084) drives the banks only through two calls:
  - `sharedBank.prepareBlockSmoothing()` (:1002).
  - Per sample, `body.processSampleNoSmooth(sharedBank, exc)` (:1030) and `secondaryBank_.processSampleNoSmooth(...)` (:1041).
- Neither call flushes silent modes. `numActiveModes_` goes down only in `flushSilentModes()` (`modal_resonator_bank.h:461-474`), and only the two `processBlock` overloads call it (:446, :457).
- The retirement test (`drum_voice.h:644-669`) therefore never sees `bankSilent`:
  - `bankSilent = sharedBank.getNumActiveModes() == 0` (:645) gates both the normal retirement (:647) and the 256-block safety net (:660-665).
  - A Feedback voice holds its slot forever. In the tail probe, a Feedback voice on Membrane falls below -100 dBFS after 3.83 s and on Plate after 7.11 s; neither ever retires (`probe-results.csv` rows 522, 529, 534-536).

**1. Failing test**

Create `plugins/membrum/tests/unit/voice_pool/test_voice_retirement.cpp` and add it to the `membrum_tests` list. T1.8 extends the same file.

- **(a)** `TEST_CASE("Retirement: a Feedback-exciter voice retires once its body is silent", "[drum_voice][retirement]")`
  - Body SECTIONs: Membrane, Plate, Shell, Bell, NoiseBody.
  - Each SECTION: one `DrumVoice`, `prepare(48000.0, 0u)`, a `PadConfig` with defaults except `exciterType = ExciterType::Feedback` and that `bodyModel`, then `applyPadConfigToVoice` and `noteOn(1.0f)`.
  - Nested SECTIONs:
    - No note-off: render 20 s in 512-sample blocks.
    - `noteOff()` after 100 ms: render 10 s.
  - Assert: `!v.isActive()` at the end, and the output peak over the 100 ms before the block in which the voice retired is below -90 dBFS (3.16e-5). The second check proves that retirement does not cut audible sound.
  - Fails today: the voice is never retired. The probe shows 44 active modes on Membrane and 48 on Plate with an output of exactly 0.
- **(b)** `TEST_CASE("Retirement: sixteen Feedback hits leave no active voice in the pool", "[voice_pool][retirement]")`
  - Setup: VoicePool at 48 kHz / 512, `setMaxPolyphony(16)`, `TestHelpers::setAllPadsExciterType(pool, ExciterType::Feedback)` (`voice_pool_test_helpers.h`).
  - Steps: hit notes 36 to 51, one per block, then render 20 s.
  - Assert: `pool.getActiveVoiceCount() == 0` and `!pool.isAnyVoiceActive()`.

The pads use defaults with no pitch envelope, morph or tension. A retune recounts the modes (`computeModeCoefficients`, `modal_resonator_bank.h:760` and `:841`) on both renderers; T1.10 and T1.11 own that.

**2. Fix**

1. In `DrumVoice::processBlockSlow`, inside the body lambda, after the per-sample loop that ends at :1062 and before `bodyBank_.setLastOutput(lastBody);` (:1063), add `sharedBank.flushSilentModes();` and `if (secondaryActive) secondaryBank_.flushSilentModes();`.
   - This matches the block path: `bodyBank_.processBlock` → `ModalResonatorBank::processBlock` flushes once per chunk, and the secondary bank's `processBlock` (:818) flushes too.
   - For a String body the call runs on the unused shared bank and does nothing.
2. Rewrite the retirement comment at :631-637 so that it no longer says `flushSilentModes()` runs only "inside ModalResonatorBank::processBlock": both renderers now flush once per block. Make the same correction in the `isActive()` comment at :1088-1091.

**Behaviour before:** a Feedback voice stays active for good, keeps rendering per sample, and keeps the main-bus silence flag clear.

**Behaviour after:** the voice retires like any other voice: once the bank has flushed every mode and the envelope has finished, or through the safety net.

**3. Build and gate**

Standard workflow, with `Membrum` added. No CPU-lane run: the flush is one O(numModes) pass per block.

**4. Acceptance**

- (a) is green for all 5 bodies, with and without a note-off, and the output before retirement is below -90 dBFS.
- (b) is green.
- Report each body's measured retirement time in the commit message. Baseline: never retired. For reference, the output fell below -100 dBFS after 3.83 s on Membrane and 7.11 s on Plate.

### T1.8 — Voice retirement is body-aware: a String voice retires by its own level, silence is counted in samples, a body swap clears the bank's mode count

**Findings:** MB-106 (high, CONFIRMED), MB-160 (high, CONFIRMED), MB-039 (medium, CONFIRMED).

**Flags:** PRESET-IMPACT.

**Prerequisites:** none. In plan order this task follows T1.7 and extends its test file.

**Estimate:** 180 min.

**What the code does today:**

- `BodyBank::applyPendingSwap()` (`plugins/membrum/src/dsp/body_bank.h:177-213`, `sharedBank_.reset()` at :181) and `BodyBank::reset()` (:45-50) both call `ModalResonatorBank::reset()` (`modal_resonator_bank.h:222-236`).
  - `reset()` zeroes the oscillator states and snaps the smoothers, but leaves `active_[]` and `numActiveModes_` untouched.
  - `DrumVoice::resetForKitSwitch` (`drum_voice.h:467-491`) reaches `bodyBank_.reset()` at :470.
- `StringBody` never uses the shared bank (`bodies/string_body.h:38-41`, `:43-59`, `:82-89`), so nothing recounts or flushes the bank's modes on a String voice.
- Retirement uses `sharedBank.getNumActiveModes() == 0` for every body (`drum_voice.h:644-647`). The safety net counts `processBlock` calls: `++silentBlockCount_; if (silentBlockCount_ > 256)` (:660-665), with `int silentBlockCount_` at :1776.
- The results:
  - A String note on a fresh slot reads `bankSilent` from its first block and is cut after 257 host blocks. That is 0.343 s at 64-sample blocks, while the tail is still above -60 dB (its own -60 dB point is 0.639 s), and 0.789 s at 512.
  - A String note on a slot whose previous note was modal, or after a kit switch while a modal voice rang, never retires. The probe shows `bankActiveModes=44` after 20 s.
  - `getPartialInfo` (:1250-1258) passes the old body's mode frequencies to the coupling engine.

**1. Failing test**

Extend `plugins/membrum/tests/unit/voice_pool/test_voice_retirement.cpp`. All cases render at 48 kHz.

- **(a)** `TEST_CASE("Retirement: a String voice is not cut before its own decay, at any block size", "[drum_voice][retirement]")`
  - Config: `PadConfig` defaults with `bodyModel = String`, `exciterType = Impulse`, `decay = 1.0f`. A fresh `DrumVoice`, `noteOn(1.0f)`, no note-off, render 15 s.
  - Do the render twice, at block sizes 64 and 1024.
  - For each render, record:
    - The retirement time: the end of the block after which `isActive()` is false.
    - The -60 dB time: the last sample above 1e-3 × the render's peak.
  - Assert:
    - Both retirement times are ≥ their -60 dB time.
    - The two retirement times differ by at most one 1024-sample block (21.3 ms). Retirement is checked once per block, so finer agreement cannot be asked for.
    - The output peak in the 100 ms before retirement is below -90 dBFS.
  - Baseline: retired at 0.343 s at block 64 (257 × 64 samples), while still above -60 dB.
- **(b)** `TEST_CASE("Retirement: a String note after a Membrane note on the same voice retires", "[drum_voice][retirement]")`
  - Steps: Membrane defaults, `noteOn(1.0f)`, render 512 samples. Then apply the String config of (a) with `decay = 0.3f`, `noteOn(1.0f)`, and render 20 s in 512-sample blocks.
  - Assert: `!isActive()` and `v.bodyBank().getSharedBank().getNumActiveModes() == 0`.
  - Baseline: never retired, with 44 active modes.
- **(c)** `TEST_CASE("Retirement: a String note after a kit switch retires", "[drum_voice][retirement]")`
  - As (b), but between the two notes call `v.resetForKitSwitch()` while the Membrane note still rings.
  - Same assertions.
- **(d)** `TEST_CASE("Retirement: a String voice offers no stale modal partials to coupling", "[drum_voice][retirement]")`
  - After the String `noteOn` of (b), every entry of `v.getPartialInfo().frequencies` is 0. The coupling engine skips frequencies ≤ 0 (`sympathetic_resonance.h:186-189`).
  - A String voice on a fresh slot already reports zeros today. Deriving String partials from the waveguide pitch, as MB-039 suggests, would be a new coupling feature and is not part of this task.

**2. Fix**

1. **`body_bank.h`.**
   - Add a private helper: `void clearSharedModes() noexcept { static constexpr float kNone[1] = {0.0f}; sharedBank_.setModes(kNone, kNone, 0, 1.0f, 0.5f, 0.0f, 0.0f); }`.
     - With 0 partials, `computeModeCoefficients` (`modal_resonator_bank.h:755-851`) sets `numModes_` and `numActiveModes_` to 0, zeroes `inputGainSum_` (so `getInputGainSum()` returns 1.0) and deactivates every mode.
     - It needs no change to the shared header.
   - Call the helper right after `sharedBank_.reset()` in `applyPendingSwap()` (:181) and in `reset()` (:47).
   - Modal bodies call `setModes` again in their `configureForNoteOn`, so they are unaffected.
2. **`body_bank.h`, body-aware silence test.** Add `[[nodiscard]] bool isBodySilent() const noexcept`:
   - For `StringBody`: return `std::get<StringBody>(active_).string_.getControlEnergy() < kStringSilentEnergy`, using `std::get_if` as `setStringFrequency` (:126-130) does.
   - For every other body: return `sharedBank_.getNumActiveModes() == 0`.
   - `WaveguideString::getControlEnergy()` (`waveguide_string.h:236-239`) returns an exponentially smoothed energy of the string output. Read how it is computed (~:215-228) and set `kStringSilentEnergy` to the value that corresponds to -100 dBFS RMS: `1.0e-10f` if it is a mean square. Write the derivation in a comment.
3. **`drum_voice.h`, `DrumVoice::processBlock` retirement (:644-669).**
   - Replace `sharedBank.getNumActiveModes() == 0` with `bodyBank_.isBodySilent()`.
   - Count silence in samples:
     - Rename `int silentBlockCount_` (:1776) to `std::int64_t silentSamples_`.
     - Add `silentSamples_ += numSamples` per block.
     - Retire when `silentSamples_ > safetyNetSamples_`.
   - Add `std::int64_t safetyNetSamples_`, set in `prepare` to `static_cast<std::int64_t>(kSafetyNetSeconds * sampleRate)`, with `static constexpr double kSafetyNetSeconds = 65536.0 / 48000.0;`. That is 1.365 s, the old net's length at 48 kHz with 256-sample blocks, so that case keeps its behaviour.
   - Reset `silentSamples_` wherever `silentBlockCount_` is reset today: `noteOn` (:438), `resetForKitSwitch` (:489) and the retirement branch (:650).
   - Rewrite the comment at :663 (`≈1.4 s at 48 kHz / 256 blocks`) to state the net in seconds.
4. **What stays as it is.** MB-160 also asks that the noise layer, ModeInject and NoiseBody's noise bed count as "still sounding". This task does not take that on:
   - The comment at :639-643 explains why the noise and click layers cannot be gated on.
   - The ModeInject ring-on question waits for the user's ruling in T6.10 (head, PRESET-IMPACT).
   - The net is now 1.365 s of audio at every block size. Nothing retires sooner than today at 48 kHz with blocks of 256 samples or more, and smaller blocks no longer cut earlier.
   - If the acceptance render shows a noise or ModeInject tail cut by the net, report it in the commit message.

**Behaviour before:** a String voice is cut after 257 host blocks (a time that depends on the host block size) or is never retired. A modal body swap leaves the old mode count and partials in the bank.

**Behaviour after:** a String voice retires when its waveguide output has decayed below -100 dBFS (and its envelope has finished, or the 1.365 s net has run). The safety net no longer depends on the block size. A body swap or kit switch clears the bank's modes.

**3. Build and gate**

- Standard workflow, with `Membrum` added.
- PRESET-IMPACT: String pads in the factory kits, for example the pads written at `tools/membrum_preset_generator.cpp:1560, 2877, 3541, 5139, 6159`, now ring to their natural end instead of stopping at 257 blocks. Run the kit-quality probe before and after (48 kHz, block 256). List every String pad's decay, rms500 and peak in the commit message, and stop for the user's ruling if any of them changes audibly.

**4. Acceptance**

- (a): at block sizes 64 and 1024, retirement comes after the -60 dB time, within 21.3 ms of each other, below -90 dBFS. Baseline: 0.343 s at block 64, above -60 dB.
- (b) and (c): the voice retires within 20 s with 0 active modes. Baseline: never, with 44 modes.
- (d) passes.
- T1.7's cases stay green.
- The kit table is in the commit message.

### T1.9 — One VoicePool::processBlock; a finished voice frees its allocator slot and coupling resonators; Quietest never steals a voice triggered in the same block

**Findings:** MB-049 (high, CONFIRMED), MB-050 (high, CONFIRMED), MB-105 (high, CONFIRMED), MB-118 (low, CONFIRMED), MB-069 (low, CONFIRMED).

**Flags:** none.

**Prerequisites:** none. T1.6 runs first and adds `strikeNorm_.syncAll(padConfigs_)` at the top of both `processBlock` overloads. Keep that call in the merged body.

**Estimate:** 240 min.

**What the code does today** (all lines in `plugins/membrum/src/voice_pool/voice_pool.cpp` unless named):

- **Two drifted copies (MB-118, MB-069).** The mono `processBlock` (:306-437) and the multi-bus `processBlock` (:443-564) are near-copies.
  - When a voice finishes naturally, only the mono copy calls `couplingEngine_->noteOff(slot)` (:388-390). The multi-bus branch at :514-519 does not.
  - The processor uses the multi-bus copy whenever the host passes more than one output bus (`processor.cpp:633-666`), which is the normal case.
  - Stale owners and `refCounts_` then pull later merged resonator frequencies toward the dead voice's partial (`sympathetic_resonance.h:199-204`).
- **Allocator slot leak (MB-050, MB-105).**
  1. On natural finish, both copies call `allocator_.voiceFinished(slot)` (:395, :518) without first calling `allocator_.noteOff`.
  2. `VoiceAllocator::voiceFinished` ignores any slot that is not Releasing (`dsp/include/krate/dsp/systems/voice_allocator.h:288-293`). The auto-release at :380-384 and :508-512 only gates the voice, so the allocator slot stays Active while the MIDI note is held.
  3. The later MIDI note-off (`VoicePool::noteOff`, :283-300) skips the voice, whose meta is already Free, and moves the allocator slot to Releasing (`voice_allocator.h:258-280`). Nothing calls `voiceFinished` for it again.
  4. `poolFull` (:158-159) reads the inflated count. Under Quietest every new hit then kills the only sounding voice, and polyphony collapses to 1.
  5. Under Oldest and Priority, the leaked slots are stolen through `beginFastRelease` on idle voices.
- **Quietest steals the wrong voices (MB-049).**
  - In Quietest mode with a full pool, `noteOn` releases `selectQuietestActiveSlot()` (:848-872) before the allocator runs (:161-179).
  - A voice started earlier in the same block has `currentLevel = 0.0f` (:256) until it renders (:375, :486), so it is always the victim. In a three-note chord, the first two notes die at their first sample.
  - The pre-steal also runs for a same-note retrigger, which the allocator serves on the note's own slot (`voice_allocator.h:238-244`, `findVoicePlayingNote` :830-840). An unrelated ringing voice dies for nothing.
  - When `q < 0`, no allocation mode is set (:161-189).

**1. Failing test**

Create `plugins/membrum/tests/unit/voice_pool/test_voice_pool_allocator_lifecycle.cpp` and add it to the `membrum_tests` list. Reuse the `PoolFixture` and `findActiveSlotByNote` pattern of `test_voice_pool_stealing_policies.cpp:20-55`: declare local copies in an anonymous namespace, because that file's helpers are not in a header. All cases use 44.1 kHz and 128-sample blocks.

- **(a)** `TEST_CASE("VoicePool Quietest: three note-ons in one block all sound", "[voice_pool][stealing]")`
  - Setup: `setMaxPolyphony(4)`, Quietest. `TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 1.0f, 0.3f, 0.8f)` (decay 1.0, so voices ring for seconds).
  - Steps:
    1. Hit notes 49, 51, 57 and 59, one per block, then render 20 blocks.
    2. In one block, with no render in between, call `noteOn` for 36, 38 and 42.
    3. Render one block.
  - Assert: `findActiveSlotByNote` finds 36, 38 and 42, and `getActiveVoiceCount() == 4`.
  - Baseline (MB-049 probe S1): only 42 is left.
- **(b)** `TEST_CASE("VoicePool Quietest: retriggering a ringing pad in a full pool kills no other voice", "[voice_pool][stealing]")`
  - Setup: the four ringing voices of (a).
  - Steps: `noteOn(51, 1.0f)`, then render one block.
  - Assert:
    - `getActiveVoiceCount() == 4`, and 49, 57 and 59 are still Active.
    - No `releasingMeta(i)` is FastReleasing except note 51's own slot, whose retrigger fades the old voice through the shadow.
  - Baseline: the count drops to 3 (probe S2).
- **(c)** `TEST_CASE("VoicePool: a voice that finishes before its note-off frees its allocator slot", "[voice_pool][stealing]")`
  - Setup: `setMaxPolyphony(4)`, every pad set to decay 0.0 through `setAllPadsVoiceParams`.
  - Steps:
    1. Hit 36, 37, 38 and 39, one per block, with no note-off.
    2. Render until all four `voiceMeta` states are Free. REQUIRE that this happens within 10 s, so the case cannot pass vacuously.
    3. Send `noteOff` for 36 to 39.
    4. Hit 40, 41 and 42, rendering one block after each.
  - SECTION Quietest: assert `getActiveVoiceCount() == 3`. Baseline: 1 (probe S3: `alloc=4 metaActive=1`).
  - SECTIONs Quietest, Oldest and Priority: assert no `releasingMeta(i).state == VoiceSlotState::FastReleasing`. Today, under Oldest and Priority, the leaked slots are stolen and fade a silent voice.
- **(d)** `TEST_CASE("VoicePool multi-bus: a naturally finished coupled voice releases its coupling resonators", "[voice_pool][multi-bus][coupling]")`
  - Setup:
    - A `Krate::DSP::SympatheticResonance engine` prepared at 44.1 kHz, attached with `pool.setCouplingEngine(&engine)`.
    - Pads 0 and 1 (notes 36 and 37) are Membrane with `couplingAmount = 1.0f` and decay 0.1.
    - Render with the multi-bus overload: `numOutputBuses = 2`, both aux pointers valid, both buses active.
    - Never call `engine.process`, so the resonators stay active (reclaim needs the envelope to decay, `sympathetic_resonance.h:337-351`).
  - Steps:
    1. Hit 36. Read `fA = mainVoiceRef(slotA).getPartialInfo().frequencies[0]`.
    2. Render until 36's meta is Free.
    3. Set pad 1's `size` so that its partial 0 lies 0.05 to 0.25 Hz from `fA`. Find the size by search in the test and REQUIRE the distance, so the merge path is really exercised (`kMergeThresholdHz` 0.3, `sympathetic_resonance.h:49`).
    4. Hit 37 and REQUIRE that it landed on a different slot than 36. Read `fB` from its slot.
    5. Find the engine resonator within 0.3 Hz of `fA` (`getResonatorFrequency(i)` for `i < Krate::DSP::kMaxSympatheticResonators`).
  - Assert: its frequency equals `fB` within 1e-3 Hz. With the dead owner released, the refCount is 0 and the merge takes `fB` unweighted (`sympathetic_resonance.h:199-204`).
  - Baseline: (fA + fB) / 2, because the multi-bus branch never released 36.

**2. Fix**

1. **One renderer.**
   - Replace the mono overload's body (:306-437) with a forwarder: `processBlock(outL, outR, nullptr, nullptr, nullptr, 1, numSamples);`. With one bus, the aux branch (:495-505, :549-559) never runs.
   - Move the `kSilenceThreshold` rationale comment (:331-350) into the multi-bus body, replacing its back-reference at :464.
   - Keep the forwarder in `voice_pool.h:126`, because tests and the processor's single-bus path (`processor.cpp:636`) call it.
2. **Natural finish.** In the multi-bus natural-finish branch (:514-519), before the state change:
   - `if (couplingEngine_ != nullptr) couplingEngine_->noteOff(slot);`
   - `if (allocator_.getVoiceState(static_cast<std::size_t>(slot)) == Krate::DSP::VoiceState::Active) (void)allocator_.noteOff(meta_[slot].originatingNote);`
   - Then the existing `voiceFinished`.
   - A same-note retrigger reuses the note's slot (`voice_allocator.h:238-244`), so no other main slot holds the same note in the allocator. If `allocator_.noteOff` ever returned more than one event here, that would be a bug: add a debug assert on the returned span's size.
3. **Quietest pre-steal in `noteOn`** (:153-189).
   - Compute `const bool alreadyPlaying`: any `i < maxPolyphony_` with `allocator_.getVoiceState(i) != Krate::DSP::VoiceState::Idle && allocator_.getVoiceNote(i) == midiNote`.
   - Run the pre-steal only when `poolFull && !alreadyPlaying`.
   - In every branch where the pre-steal does not free a slot (`q < 0`, retrigger, pool not full), call `allocator_.setAllocationMode(AllocationMode::Oldest)`, so the allocator never steals with a stale mode. Priority keeps `HighestNote`.
   - Rewrite the comment at :153-157 to match.
4. **`selectQuietestActiveSlot`** (:848-872): skip slots with `meta_[slot].noteOnSampleCount == sampleCounter_`. Those voices were triggered in the current block and have not rendered, so they have no measured level. Extend the FR-122 / FR-128 comment to say so. If every candidate was triggered in this block, the function returns -1 and the Oldest steal from step 3 applies.
5. **Untouched:** `VoicePool::noteOff` (:283-300) and the auto-release (:508-512). With step 2, a finished slot always returns to Idle, and a later MIDI note-off finds no Active allocator slot for that note.

**Behaviour before:**

- The two renderers differ in coupling release.
- A held note longer than its pad's ring leaks an allocator slot.
- Under Quietest, chords and retriggers kill the wrong voices, and polyphony can collapse to one voice.

**Behaviour after:**

- There is one renderer.
- A finished voice frees its allocator slot and its coupling ownership.
- Quietest steals only voices that have rendered, and only when a new slot is really needed.

**3. Build and gate**

Standard workflow, with `Membrum` added. All existing `[voice_pool]`, `[stealing]`, `[multi-bus]` and coupling cases must stay green. `test_voice_pool_stealing_policies.cpp:142-195` renders between notes, so it does not depend on the same-block rule. If a case asserted the old victim, re-read the FR-122 text before changing the test, and report it.

**4. Acceptance**

- (a): 36, 38 and 42 all sound, 4 active voices. Baseline: only 42 left.
- (b): 4 active voices after the retrigger. Baseline: 3.
- (c): 3 active voices and no shadow fade under all three policies. Baseline: 1 active under Quietest.
- (d): the merged resonator frequency equals `fB` within 1e-3 Hz. Baseline: (fA + fB) / 2.
- Re-run the audit probe case `lifecycle:steal:poly=4:policy=Quietest` (`probe-results.csv` row 64, max steal jump 0.0151 against 0.0093 at non-steal hits) and report the new numbers in the commit message.

### T1.10 — Note-off and fast-release damping is a persistent, time-based release multiplier in the modal bank

**Findings:** MB-269 (medium, CONFIRMED), MB-107 (medium, PLAUSIBLE), MB-012 (medium, CONFIRMED), MB-034 (medium, CONFIRMED).

**Flags:** CROSS-PLUGIN IMPACT, PRESET-IMPACT, VERIFY-FIRST (MB-107 only), MACOS. MACOS is added because the fix puts a new `std::log` of the damp scale into a fast-math DSP header (`damp()` clamps its scale to `[0, 1]`, so `log(0)` is reachable and needs a domain clamp), and the new release term must leave every other consumer's decay rate exact when it is zero, which the MACOS rules say is produced by a branch, not by adding 0.

**Prerequisites:** none.

**Estimate:** 180 min.

Context. `ModalResonatorBank::damp(scale)` (`dsp/include/krate/dsp/processors/modal_resonator_bank.h:507-516`) multiplies `radiusTarget_[k]` and `radius_[k]` once and stores the scale nowhere. Every later `updateModes` (`:294-305` -> `computeModeCoefficients(..., false)`, which writes `radiusTarget_[k] = exp(-(b1 + b3 f_w^2)/fs)` at `:823-824`, `:838`) and `updateDampingLaw` (`:356-370`) rebuilds the target from the damping law alone, so the release is undone; the per-block smoother (`smoothCoefficients`, `:906-914`) pulls `radius_` back 1.1 % per call today, and once T1.11 snaps the coefficients the release would vanish within one chunk (MB-269). The scale is also a per-sample pole factor, so the added decay rate is `-ln(0.997) * fs`: the release halves each time the sample rate doubles (MB-012), and because `VoicePool::processBlock` calls `DrumVoice::noteOff()` again on every block whose peak is below `1e-3` (`plugins/membrum/src/voice_pool/voice_pool.cpp:380-384` and `:508-512`), `0.997` compounds once per quiet block (MB-034). Callers: `DrumVoice::noteOff()` (`plugins/membrum/src/dsp/drum_voice.h:442-452`, `damp(kNoteOffDampScale)` at `:451`, constant `0.997f` at `:64`) and `DrumVoice::fastReleaseDamp()` (`:104-107`, `kFastReleaseDampScale = 0.88f` at `:71`, called from `VoicePool::beginFastRelease`, `voice_pool.cpp:937`). No other plugin calls `damp()` (Grep `\.damp\(` over `plugins/` and `dsp/`: only those two lines).

**1. Failing test.**

- `dsp/tests/unit/processors/test_modal_resonator_bank.cpp` (builds into `dsp_processors_tests`), new case `TEST_CASE("ModalResonatorBank: damp() survives updateModes and snapCoefficients", "[modal_resonator_bank][release]")`. Build the bank exactly as experiment E3 of `specs/membrum-audit/probes/audit_retune_probe.cpp.txt` does (48-mode membrane set at 150 Hz, `b1 = 2 s^-1`, impulse excitation, `damp(0.997)` at 50 ms). Measure the time from the `damp` call until `getModalEnergy()` is 60 dB below its value at the call, in four SECTIONs at 44.1 kHz and 96 kHz: no retune; `updateModes` every 64 samples (today's path); `updateModes` + `snapCoefficients()` every 64 samples; `updateModes` every sample with `processSample` smoothing. REQUIRE each retuning variant within 10 % of the no-retune time at the same rate. It fails today on the snap and per-sample variants (E3 baseline: 0.052 s no retune against 3.454 s and 3.141 s at 44.1 kHz; 0.024 s against 3.449 s and 2.484 s at 96 kHz, `specs/membrum-audit/probes/retune-probe-results.txt:49-52, 60-63`).
- New file `plugins/membrum/tests/unit/dsp/test_note_off_release.cpp` (append to the `membrum_tests` source list). Copy `detPad()` and `hzToPitchNorm()` from `specs/membrum-audit/probes/audit_probes.cpp.txt` (`detPad` at `:365-375`, `hzToPitchNorm` at `:679`) as file-local helpers. Measure "release time" as the time from the note-off sample until a 5 ms running RMS of the output stays 60 dB below the render's peak; use `Krate::DSP::detail::isFinite` for any finiteness check (never `std::isfinite` or `allFinite` from `buffer_comparison.h`).
  - `TEST_CASE("Note-off release time does not depend on the sample rate", "[drum_voice][release]")`: `detPad(Membrane, Impulse)` with `decay = 1`, `material = 1`, velocity 1, `applyPadConfigToVoice` + `DrumVoice::noteOn`, `DrumVoice::processBlock` in 64-sample blocks, `noteOff()` at 100 ms, at 44.1, 96 and 192 kHz. REQUIRE the largest release time at most 1.10 x the smallest. Baseline (MB-012 probe): 38.9 / 19.0 / 10.0 ms. Red today.
  - `TEST_CASE("Repeated auto-release does not compound the note-off release", "[voice_pool][release]")` (MB-034): `VoicePool` at 48 kHz, `padConfigMut(0) = detPad(Membrane, Impulse)`, note 36 velocity 1, `noteOff(36)` at 100 ms, rendered at blocks 32 and 512; measure the time to 80 dB below the peak (deep enough that the pool's per-quiet-block `noteOff()` repeats). REQUIRE the two within 10 %. Red today (the repeat count per second is 48000/32 against 48000/512).
  - `TEST_CASE("Note-off release survives the pitch envelope at every host block size", "[voice_pool][release][pitch_env]")` (MB-269 at plugin level, and the VERIFY-FIRST test of MB-107): `VoicePool` at 48 kHz, `padConfigMut(0) = detPad(Membrane, Impulse)` with a pitch envelope 160 -> 80 Hz over 30 ms (`tsPitchEnvStart` / `tsPitchEnvEnd` through `hzToPitchNorm`; set `tsPitchEnvTime` for 30 ms after reading its denormalisation in `plugins/membrum/src/dsp/pad_config_apply.h`), `noteOff(36)` at 200 ms, blocks 8, 64 and 1024, and the same voice without the pitch envelope. REQUIRE: release time with the envelope within 10 % of the one without, at each block size; release times across the three block sizes within 10 %; `!pool.isAnyVoiceActive()` within 1 s after the note-off. MB-107's DrumVoice-only probe measured retirement at 6.591 / 5.632 / 3.713 / 0.512 s at blocks 8 / 16 / 32 / 64 against 0.361-0.479 s without the envelope; one verifier argued the pool's repeated `noteOff()` hides this. If this case is GREEN on the unfixed code, MB-107 is refuted under VERIFY-FIRST: keep the case, record its output, and continue the task for MB-269, MB-012 and MB-034, whose red cases are the ones above.
- The cases that call the new bank API (step 2) cannot compile before the fix; write them in step 2.

**2. Fix.**

- `dsp/include/krate/dsp/processors/modal_resonator_bank.h` (opt-in: a bank that never sees `damp()` or the new call renders exactly as before):
  - New member `float releaseRate_ = 0.0f;` (extra decay rate in s^-1, applied to every mode).
  - `computeModeCoefficients` (`:823`): after `float decayRate_k = b1 + b3 * f_w * f_w;` add `if (releaseRate_ > 0.0f) decayRate_k += releaseRate_;`. Same line after `:367` in `updateDampingLaw`. The branch keeps the zero-release arithmetic identical for Innexus and Seraphis (no `+ 0.0f` that fast-math may contract into an fma).
  - `setModes` (the `DampingLaw` overload, `:258-274`): set `releaseRate_ = 0.0f;` before `computeModeCoefficients` (a note-on ends any release).
  - `reset()` (`:222-236`): if `releaseRate_ != 0.0f`, set it to 0 and call `updateDampingLaw(storedDampingLaw_)` before the three `memcpy` snaps, so the snapped radii carry no release.
  - `damp(scale)` (`:507-516`): keep both multiplies and the clamp; add `releaseRate_ += -std::log(std::max(scale, 1.0e-6f)) * sampleRate_;` so the damping is persistent (MB-269). Update the doc comment `:501-506` to say the release now survives `updateModes` / `updateDampingLaw` until the next `setModes` or `reset`.
  - New `void raiseReleaseDecayRate(float ratePerSecond) noexcept`: `r = std::clamp(ratePerSecond, 0.0f, 1.0e6f)`; if `r <= releaseRate_` return (idempotent, never lowers within a note); else `f = std::exp(-(r - releaseRate_) / sampleRate_)`, multiply `radiusTarget_[k]` and `radius_[k]` of every active mode by `f`, `releaseRate_ = r`. The rate is in seconds, so the release time does not depend on the sample rate (MB-012). Add `[[nodiscard]] float getReleaseDecayRate() const noexcept`.
  - New `[[nodiscard]] float getLiveModeFrequency(int k) const noexcept`: the formula of `getModeFrequency` (`:526-533`) applied to the running `epsilon_[k]` instead of `epsilonTarget_[k]`. `getModeFrequency` reads the target, which is why the existing pitch tests cannot see MB-026; T1.11's tests need the running value, and adding it here edits and gates the shared header once.
  - New cases in `test_modal_resonator_bank.cpp`, tag `[modal_resonator_bank][release]`: (a) `raiseReleaseDecayRate(132.5f)` gives the same time to -60 dB (within 5 %) at 44.1, 96 and 192 kHz, with and without `updateModes` + `snapCoefficients()` every 64 samples; (b) a second call with a lower or equal rate changes nothing (bit-identical render); (c) `setModes` and `reset()` return `getReleaseDecayRate()` to 0 and the next note decays at the plain law; (d) after `updateModes` without a snap `getLiveModeFrequency(0) != getModeFrequency(0)`, after `snapCoefficients()` they are equal.
- `plugins/membrum/src/dsp/drum_voice.h`:
  - Replace `kNoteOffDampScale` and `kFastReleaseDampScale` (`:57-71`) with `kNoteOffReleaseRate = 132.5f` (s^-1, `-ln(0.997) x 44100`: the existing 44.1 kHz calibration, extra T60 = ln(1000)/132.5 = 52 ms) and `kFastReleaseRate = 5637.0f` (`-ln(0.88) x 44100`, extra T60 1.2 ms). Rewrite the two comments to state the rate, the T60 and that both hold at every sample rate.
  - `noteOff()` (`:451`): `bodyBank_.getSharedBank().raiseReleaseDecayRate(kNoteOffReleaseRate);`. Fix the stale "0.993 yields ~50 ms" comment (`:447-450`).
  - `fastReleaseDamp()` (`:106`): `raiseReleaseDecayRate(kFastReleaseRate)`.
  - Because the call is idempotent, the pool's repeated `noteOff()` no longer compounds (MB-034). Removing the repeat itself is T1.13; do not touch `voice_pool.cpp` here.
- `damp()` has no caller left in the repo after this change. It is a public KrateDSP API with its own semantics; leave it (now persistent) and mention the orphan in the commit message.

**3. Build and gate.**

- Targets: `membrum_tests Membrum dsp_processors_tests`.
- CROSS-PLUGIN: before editing, `grep -rl "modal_resonator_bank.h" plugins/ dsp/include tests/` and list the consumers in the commit message (read during the audit: Membrum's bodies and `drum_voice.h`, `plugins/innexus/src/processor/innexus_voice.h`, `dsp/include/krate/dsp/systems/continuous_body.h` used by Seraphis). Run `node tools/header-consumers.js --tag '[membrum]'`, build every executable its last line names (expect at least `innexus_tests`, `seraphis_tests`, `dsp_systems_tests`, `dsp_processors_tests`) and gate them with `membrum_tests` and `~[long]` appended. Innexus and Seraphis never call `damp()` or `raiseReleaseDecayRate()`, so `releaseRate_` stays 0 there: no golden of theirs may move. A moved golden is a defect in this fix, not a re-baseline.
- MACOS: run the flag's checks; `tools/macos-fastmath-repro.sh` on `processors/test_modal_resonator_bank.cpp` covers the new bank code.
- clang-tidy: add `-Target dsp`.
- PRESET-IMPACT: no kit data changes. Run the kit-quality probe before and after; the column that moves is "decay with a note-off at 100 ms" in `kit-pads.csv` (at 48 kHz the release changes from 48 ms to 52 ms extra T60, and pitch-envelope pads now release fully). Show the table.

**4. Acceptance.**

- The E3 case: every retuning variant reaches -60 dB within 10 % of the no-retune time (baseline 3.454 s and 3.141 s against 0.052 s at 44.1 kHz; 3.449 s and 2.484 s against 0.024 s at 96 kHz).
- Note-off release at 44.1 / 96 / 192 kHz within 10 % of each other (baseline 38.9 / 19.0 / 10.0 ms), and the 44.1 kHz value within 10 % of today's 38.9 ms (calibration kept).
- Pool release at blocks 32 and 512 within 10 % (MB-034).
- Pitch-envelope voice: release within 10 % of the same voice without the envelope at blocks 8, 64 and 1024, retirement within 1 s of the note-off (MB-269; MB-107 "fixed" or "refuted" with the step-1 output).
- `GATE GREEN` over `membrum_tests` and every consumer executable, no consumer golden moved, kit-probe table in the commit message.

### T1.11 — Pitch envelope, tension and Material Morph retune the modal bank on a fixed control grid, independent of the host block size

**Findings:** MB-008 (critical, CONFIRMED), MB-026 (critical, CONFIRMED), MB-151 (high, CONFIRMED), MB-159 (high, CONFIRMED), MB-237 (high, CONFIRMED), MB-240 (low, PLAUSIBLE).

**Flags:** PRESET-IMPACT, VERIFY-FIRST (MB-240 only).

**Prerequisites:** T1.10 (its `getLiveModeFrequency` is what these tests read, and without its persistent release the snaps below would cancel note-off damping, MB-269).

**Estimate:** 300 min.

Context. Every retune ends in `ModalResonatorBank::updateModes`, which moves only the coefficient targets: the pitch envelope through `retuneFundamental` -> `updateModalFundamentalOnBank` (`drum_voice.h:1587-1593`, `:1607-1626`, `updateModes` at `:1619`), tension through the `else if (tensionActive)` branch (`:747-754` fast, `:966-972` slow), Material Morph through `refreshBodyForMaterialOnBank` -> `pushCachedModes` (`:1638-1665`). The bank applies its per-sample 2 ms smoother (`smoothCoeff_` at `modal_resonator_bank.h:213`, `kSmoothingTimeMs = 2.0f` at `:647`) once per `processBlock` call (`:431-447`, `:906-914`), and `DrumVoice` calls it once per chunk of `min(host block, kMaxBlockSize = 2048)` (`drum_voice.h:589`, `:701`, `:799`; the slow path once per block, `:960-977`, `:1002`), so the audible coefficients reach about 1.1 % of the way per host block (MB-008, MB-026, MB-159). The fast path also reads the envelope once per chunk and discards the other chunk-1 values (`:734-746`, `:741-742`), a staircase whose step is the host block (MB-151). At note-on, `configureForNoteOn` (`:244`) snaps the bank to the natural pitch and the start-pitch seed at `:318-321` is an unsnapped `updateModes`, so the start frequency never sounds (MB-237). Nothing in `plugins/membrum/src` calls `snapCoefficients()` (`modal_resonator_bank.h:342-351`, whose banner `:307-326` describes this exact caller pattern; Seraphis `continuous_body.h:2860` uses it on a 64-sample grid). The comments at `drum_voice.h:584-588`, `:613-619`, `:703-707` and `:929-932` claim a 64-sample refresh that does not exist. The existing tests miss all of it: `test_pitch_envelope_808.cpp:119` renders with the per-sample `process()`, and `test_pitch_env_all_bodies.cpp:123-127` and `test_per_mode_damping.cpp:506, :564` read `getModeFrequency()`, which returns the target.

**1. Failing test.** New file `plugins/membrum/tests/unit/dsp/test_pitch_env_block_path.cpp` (append to `membrum_tests`). File-local helpers: `detPad()` and `hzToPitchNorm()` copied from `specs/membrum-audit/probes/audit_probes.cpp.txt`; a Goertzel peak search in 0.1 Hz steps over a given band, written the way `test_pitch_envelope_808.cpp:92-142` does it; `Krate::DSP::detail::isFinite` for finiteness. "Live mode-0" means `voice.getBodyBankForTest().getSharedBank().getLiveModeFrequency(0)` (T1.10).

- `TEST_CASE("Pitch envelope sweep sounds on the block path at every host block size (SC-009)", "[pitch_env][drum_voice][timing]")`:
  - Pad: the default-kit kick: `Membrum::DefaultKit::apply(pads)` and take `pads[0]` (note 36; 160 -> 50 Hz over 20 ms, `plugins/membrum/src/dsp/default_kit.h:54-61`), with `noiseLayerMix = 0`, `clickLayerMix = 0`, `tensionModAmt = 0`, velocity 100/127, as MB-237's probe set it.
  - Renders of 400 ms: `DrumVoice::process()` per sample, and `DrumVoice::processBlock` at host blocks 1, 8, 32, 64, 128, 256, 512, 1024 and 2048 at 44.1 kHz; blocks 64 and 2048 at 96 kHz.
  - (a) Settled pitch: Goertzel peak over 30-120 Hz in 150-350 ms within +/-3 % of 50 Hz for every render, and within 1 % of the `process()` render. Baseline (MB-237, 44.1 kHz): 50.1 Hz per sample; 50.9 / 55.0 / 62.2 / 69.0 / 73.7 / 76.8 / 78.4 Hz at blocks 32 / 64 / 128 / 256 / 512 / 1024 / 2048.
  - (b) Start pitch: live mode-0 read right after `noteOn()`, before any rendering, at least 0.85 x 160 Hz = 136 Hz. Baseline 80.0 Hz on every path.
  - (c) Trajectory: record live mode-0 after every block of the block-8 render. For the block-64 and block-1024 renders, after every block REQUIRE live mode-0 within 50 cents of the block-8 value at the same sample index. On the block-8 render, live mode-0 at 20 ms within +/-10 % of 50 Hz (SC-009). Baseline (probe `pitchenv:staircase`, 20 ms sweep): 233 cents at block 64, 2012 cents at block 1024; MB-151 (50 ms sweep, all modal bodies): 911 and 1599 cents.
  - (d) Identity: max |sample difference| between every block-size render and the block-8 render at the same rate at most 3e-5 (-90 dBFS, the bar of audit probe 04b). Together with (c) this pins the pitch at every sample, so it also holds at every 1 ms point of the block-1024 render.
- `TEST_CASE("Tension glide is independent of the host block size", "[tension][drum_voice][timing]")`: `detPad(Membrane, Impulse, 0.4f)`, `tensionModAmt = 1`, no pitch envelope, velocity 1, 44.1 kHz, blocks 32, 256, 1024, 2048 and `process()`. REQUIRE the Goertzel peak over 150-260 Hz in 50-100 ms within 1 % across all renders, and max |difference| against the block-32 render at most 3e-5. Baseline (MB-008): 208.6 / 201.0 / 199.7 / 199.5 Hz at 32 / 256 / 1024 / 2048 (4.5 % spread).
- `TEST_CASE("Material Morph refresh is independent of the host block size", "[morph][drum_voice][timing]")`: `detPad(Plate, Mallet)`, `morphEnabled = 1`, `morphStart = 0`, `morphEnd = 1`, `morphDuration` for about 200 ms, 48 kHz, blocks 32, 512 and 2048 against block 8: max |difference| at most 3e-5.
- `plugins/membrum/tests/unit/tone_shaper/test_pitch_envelope_808.cpp` (MB-240, VERIFY-FIRST): in `TEST_CASE("808Kick: PitchEnv Start=160 End=50 Time=20 ms gives ~50 Hz at t=20 ms (SC-009)", ...)` keep the existing `process()` path as one SECTION and add SECTIONs that render the same voice through `processBlock` at 64 and 512 samples with the same `[45, 55]` Hz assertion, plus a start assertion (live mode-0 after `noteOn()` at least 136 Hz). The case name and tags stay. If the new SECTIONs pass on unfixed code, MB-240's coverage claim is refuted: keep them and record the output.
- Run each new case by name and confirm it fails for the reason above (settled pitch far from 50 Hz, start at 80 Hz, block-size differences).

**2. Fix** (all in `plugins/membrum/src/dsp/drum_voice.h` unless named).

- Control grid. Add `static constexpr int kControlGridSamples = 32;` (0.73 ms at 44.1 kHz; the decimation Marogna, Avanzini & Bank use for tension modulation, DAFx-10, which MB-151 cites) and a member `std::int64_t voiceSampleCount_` (samples rendered since `noteOn`, reset there). Grid points are the samples where `voiceSampleCount_ % kControlGridSamples == 0`, so they sit at the same voice time whatever the host block size.
- `void controlStep(Krate::DSP::ModalResonatorBank& sharedBank) noexcept`, run at a grid point, in this order:
  1. Tension / wire energy follower (moved here from the end of the fast-path chunk, `:902-920`, and the end of the slow-path block, `:1069-1083`, both of which raise the per-sample coefficient to the chunk or block length and so depend on the host block): `e = sharedBank.getModalEnergy()`, `a = energyAlphaGrid_` (precompute `std::pow(energyAlpha_, kControlGridSamples)` in `prepare`), `energyEnv_ = a * energyEnv_ + (1 - a) * e`, then the `wireEnergyPeak_` / `wireGainMod_` update.
  2. Pitch: `basePitchHz = pitchEnvActive ? toneShaper_.processPitchEnvelope() : naturalFundamentalHz_`; `pitchHz = basePitchHz * tensionPitchMod` (tension only when `tensionActive`, as computed at `:722-728`).
  3. Morph: `m = unnaturalZone_.materialMorph.process()` when morph is enabled.
  4. Dirty gate: when `|pitchHz - lastPushedPitchHz_| > 1e-6f * pitchHz` or `m != lastPushedMorph_`, retune (morph refresh then pitch retune as today; T1.12 merges them into one push) and call `sharedBank.snapCoefficients()` after the last `updateModes`; store the pushed values. For String, `retuneFundamental` keeps calling `setStringFrequency` (the waveguide has its own smoother); no snap is needed there.
  The modulators advance exactly once per rendered sample: a grid point's read counts as that sample; every other rendered sample ticks `(void)toneShaper_.processPitchEnvelope()` and `(void)materialMorph.process()`. Remove the per-chunk tick loops (`:741-742`, `:765-766`) and the in-loop ticks of the slow path (`:1021-1024`).
- `processBlockFast` (`:682-925`): when `pitchEnvActive || tensionActive || morphActive || wireActive`, cut each chunk at the next grid point (`len = min(remaining, kMaxBlockSize, kControlGridSamples - voiceSampleCount_ % kControlGridSamples)`), run `controlStep` at the start of a sub-chunk that begins on a grid point, render the sub-chunk exactly as today, then `voiceSampleCount_ += len`. When none of them is active, keep today's chunking and only advance `voiceSampleCount_`, so a pad without modulation renders bit-identically to today.
- `processBlockSlow` (`:933-...`): the same grid loop around the two `withActive` visitors, with `sharedBank.prepareBlockSmoothing()` (and the secondary bank's) per sub-chunk.
- `process()` (`:497-519`): replace the per-sample `updateBodyFundamental` / `refreshBodyForMaterial` with the same rule (control step on a grid point, tick otherwise), so all three renderers follow one trajectory and `process()` gains the tension path it lacks.
- `noteOn()`: move the seed block (`:313-321`) to directly after `bodyBank_.configureForNoteOn(params_, initialPitchHz);` (`:244`) and before the output-gain block (`:246-311`), and call `bodyBank_.getSharedBank().snapCoefficients()` after `retuneFundamental(initialPitchHz)`: the states were just cleared by `setModes`, so the snap is click-free, and the strike-normalisation probe (`measuredStrikeOutputGain`, `:302`; its `bank.reset()` at `:1359` keeps the seeded targets) now measures the bank at the pitch the note starts at (MB-026). Add the seeded start frequency (`initialPitchHz` when the pitch envelope is active, else 0) to `StrikeNormKey` (`:1717-1728`) and to the key built at `:1400-1406`. Set `voiceSampleCount_ = 0` and the `lastPushed*` values to the seed.
- Nyquist guard. A snapped retune whose new target crosses the bank's Nyquist cull (`modal_resonator_bank.h:813-819`) zeroes a ringing mode's radius at once: experiment E5 measured an output step of |dy| 2.91 on a 400 -> 3000 Hz snap at 44.1 kHz (`retune-probe-results.txt:54`). Add `TEST_CASE("Rising pitch envelope across the Nyquist guard does not click", "[pitch_env][drum_voice]")`: `detPad(Membrane, Impulse, 0.0f)`, pitch envelope 400 -> 2000 Hz over 50 ms, 44.1 kHz, block 64; zero detections from `Krate::DSP::TestUtils::ClickDetector` (`tests/test_helpers/artifact_detection.h`, configured as the audit probes' click counter does, `audit_probes.cpp.txt:330-346`). If it is red after the snap, cap each scaled frequency in `updateModalFundamentalOnBank` just below the bank's guard frequency (read `kNyquistGuard` in `modal_resonator_bank.h`) for every mode whose previously pushed frequency was below the guard, so a ringing mode is never culled by a retune; modes that were already culled stay culled.
- Comments: rewrite `:584-588`, `:613-619`, `:703-707`, `:756-761` and `:929-932` to describe the 32-sample voice-time grid with a snap.
- Existing tests that read targets: `test_pitch_env_all_bodies.cpp:123-127` renders through `processBlock(256)` and reads `getLiveModeFrequency(0)` (MB-026 measured the sounding peak at 193 Hz on that test's 800 -> 100 Hz patch while it asserts a sweep); `test_per_mode_damping.cpp:506` and `:564` read `getLiveModeFrequency(0)`. `test_pitch_envelope_808.cpp`: fix the comment at `:179` (0.001 ms rounds to 0 samples in `pitch_segment_envelope.h:142-146`, so the envelope returns End at once; nothing clamps to 1 ms), and state in the file header that 160 / 50 Hz over 20 ms are SC-009's spec values (`specs/137-membrum-phase2-exciters-bodies/spec.md` SC-009), not measured TR-808 values (Werner, Abel & Smith, DAFx-14). Leave its bit-mask finiteness helpers (`:74-88`, `:193-195`) to T3.1, which owns MB-014 and MB-132.

**3. Build and gate.**

- Targets: `membrum_tests Membrum`.
- Timing lane, alone, after the gate: `node tools/run-cpu-tests.js membrum_tests`. The grid adds one `updateModes` + snap per 32 samples while a modulation runs. Budgets are never relaxed; the dirty gate is the lever. If a budget is still red, stop and report the measured value.
- `plugins/membrum/tests/golden/phase1_default.bin`: the default pad has no pitch envelope, tension or morph, so it must not move. If it moves, diff it and stop.
- PRESET-IMPACT: every pitch-envelope, tension and morph pad changes; the sweep becoming audible is the purpose. Run the kit-quality probe before and after, list every pad with a pitch envelope, tension or morph (peak, rms500, decay, centroid, limiter margin), show the table and stop for the ruling.

**4. Acceptance.**

- Settled kick pitch within +/-3 % of 50 Hz at every block size and both rates, and within 1 % of `process()` (baseline 50.9-78.4 Hz across blocks 32-2048).
- Start: live mode-0 at note-on at least 136 Hz (baseline 80.0 Hz).
- Trajectory within 50 cents of the block-8 reference at blocks 64 and 1024 (baseline 233 / 2012 cents for 20 ms; 911 / 1599 cents for 50 ms); SC-009 within +/-10 % at 20 ms on the block path.
- Max |difference| at most 3e-5 between block sizes for the kick, tension and morph renders; tension spread at most 1 % (baseline 4.5 %).
- No click on the rising sweep; the updated H-3, tension and SC-009 tests green on the live coefficient.
- MB-240: "fixed" or "refuted" with the SECTION output. Timing lane green; kit-probe table and the user's ruling recorded.

### T1.12 — Material Morph keeps the pitch-envelope and tension scaling: one push-modes path per control step

**Findings:** MB-028 (high, CONFIRMED), MB-161 (medium, CONFIRMED).

**Flags:** PRESET-IMPACT.

**Prerequisites:** T1.11.

**Estimate:** 120 min.

Context. In every renderer the pitch or tension retune runs first (`updateModalFundamentalOnBank`, `drum_voice.h:1607-1626`, which scales `cachedMapperResult_` by `pitchHz / naturalFundamentalHz_` and calls `updateModes`), and then the morph refresh (`refreshBodyForMaterialOnBank`, `:1638-1652`) overwrites `cachedMapperResult_` with `mapModalBody(t, p, 0.0f)` and calls `pushCachedModes` (`:1656-1665`) with the unscaled natural-pitch frequencies. The last `updateModes` wins, so on any pad with `morphEnabled` the pitch envelope and the Membrane tension glide are dead (fast path `:734-770`, slow path `:960-977`, `process()` `:506-519`; after T1.11 the same order inside `controlStep`). The mappers ignore their pitch argument (`membrane_mapper.h:89`, `plate_mapper.h:34`, `shell_mapper.h:39`, `bell_mapper.h:30`, `noise_body_mapper.h:90`). `morphActive` is `materialMorph.isEnabled()` (`:609`), so the overwrite lasts the whole note. The live-edit path `updateModalParameters` (`:1573-1581`, called by `setMaterial`, `setSize`, `setDecay`, ... at `:1098-1201` while the voice is active) also pushes unscaled modes, and after T1.11's dirty gate nothing re-applies the pitch until the pitch changes again.

**1. Failing test.** Extend `plugins/membrum/tests/unit/dsp/test_pitch_env_block_path.cpp` (T1.11's file, same helpers), `TEST_CASE("Material Morph keeps the pitch envelope and tension scaling", "[morph][pitch_env][drum_voice]")`, 48 kHz:

- SECTION "sweep under morph" (MB-028): `detPad(Membrane, Impulse)`, pitch envelope 400 -> 100 Hz over 100 ms, `morphEnabled = 1`, morph 0.2 -> 0.8 over 300 ms; render via `process()` and via `processBlock(256)`. REQUIRE live mode-0 at 100 ms within +/-10 % of 100 Hz on both, and more than 1 semitone away from the same render with the pitch envelope off. Today mode-0 sits at the natural pitch.
- SECTION "neutral morph changes nothing" (MB-161): pitch envelope 160 -> 50 Hz over 50 ms and `tensionModAmt = 1`, once with `morphEnabled = 0` and once with `morphEnabled = 1`, `morphStart = morphEnd = material` (0.5), duration 300 ms; blocks 1, 64 and 1024. REQUIRE max |difference| at most 3e-5 at each block size: a morph that does not move the material must not move the sound.
- SECTION "live edit keeps the pitch": the 160 -> 50 Hz / 50 ms voice without morph, `processBlock(64)`; at 200 ms (sweep over, so the dirty gate is idle) call `voice.setDecay(<the same value>)`; REQUIRE live mode-0 over the next 100 ms within 1 cent of its value before the call. Red after T1.11: the push is unscaled and nothing re-applies the ratio.

**2. Fix** (`plugins/membrum/src/dsp/drum_voice.h`):

- `cachedMapperResult_` always holds the UNSCALED mapper result for the current material (set at `noteOn` `:220-221`, by the morph refresh and by `updateModalParameters`).
- New member `float currentPitchRatio_ = 1.0f;` (`pitchHz / naturalFundamentalHz_`, including the tension factor), set by the note-on seed and by `controlStep`.
- One push function, `void pushModes(Krate::DSP::ModalResonatorBank& sharedBank) noexcept`: scale `cachedMapperResult_.frequencies` by `currentPitchRatio_` into the local array (as `:1613-1617` does), one `updateModes`, then `snapCoefficients()`. It replaces `pushCachedModes` and the push inside `updateModalFundamentalOnBank`.
- `refreshBodyForMaterialOnBank` re-maps `cachedMapperResult_` for the morph material and no longer pushes; String keeps `refreshStringMaterial`.
- `controlStep` (T1.11): if morph is active, re-map; compute `currentPitchRatio_`; if either changed past the dirty gate, `pushModes` once. Exactly one `updateModes` per control step.
- `updateModalParameters`: re-map, then `pushModes` with the stored `currentPitchRatio_`.
- Update the comment at `:1601-1606` (the scaling now lives in `pushModes`) and the morph comment at `:1628-1632`.

**3. Build and gate.**

- Targets: `membrum_tests Membrum`.
- PRESET-IMPACT: the factory pads that set both features change: Experimental FX Kit pad 4 (Plate, 230 -> 160 Hz glide, `tools/membrum_preset_generator.cpp:1437-1442`), Jazz Brushes pad 2 (`:1718`, `:1732`) and Modular West Coast pad 2 (`:3506-3512`, also `tensionModAmt 0.30`). Re-scan the generator for pads with `morphEnabled` and a pitch envelope or tension. Kit-quality probe before and after, table for those pads, stop for the ruling.

**4. Acceptance.**

- Live mode-0 at 100 ms within +/-10 % of 100 Hz under morph, on `process()` and `processBlock` (baseline: the natural pitch).
- Neutral morph renders within 3e-5 of morph-off at blocks 1, 64 and 1024.
- A same-value live edit moves live mode-0 by at most 1 cent.
- T1.11's cases stay green; kit-probe table and ruling recorded.

### T1.13 — Auto-release uses a time window with an onset grace period, so voice length does not depend on the buffer size

**Findings:** MB-052 (high, CONFIRMED).

**Flags:** PRESET-IMPACT.

**Prerequisites:** T1.9, T1.10.

**Estimate:** 150 min.

Context. After rendering each host block, `VoicePool::processBlock` calls `DrumVoice::noteOff()` when that block's absolute peak is below `kSilenceThreshold = 1e-3` and the slot is Active (`plugins/membrum/src/voice_pool/voice_pool.cpp:350`, `:367-384`; the multi-bus overload `:465`, `:478-512`). The window is one host block, so with small blocks the test fires at a zero crossing of a loud low tone, and on the first block of a hit, because the amp envelope restarts from 0 (`drum_voice.h:435-437`) with a 0.5 ms attack (`:159`). It repeats on every quiet block. T1.9 reworks these overloads: find the auto-release by Grep (`peak < kSilenceThreshold`), not by line number. A decision made by the pool can only act at a host-block boundary, so the detector moves into the voice and acts at a fixed voice time.

**1. Failing test.** New file `plugins/membrum/tests/unit/voice_pool/test_auto_release_block_size.cpp` (append to `membrum_tests`), `TEST_CASE("Auto-release does not depend on the host block size", "[voice_pool][release][timing]")`:

- `VoicePool` prepared at 48 kHz, `Membrum::DefaultKit::apply(pool.padConfigsArray())`, `noteOn(45, v)` for v = 0.25 and 0.8, no note-off, 3 s rendered at blocks 1, 16, 32, 64 and 512.
- Retirement time = the end of the first block after which `!pool.isAnyVoiceActive()`.
- REQUIRE for each block size against block 512: retirement within 50 ms (one hold window); RMS over 0-50 ms and over 300-600 ms within 1 dB.
- Baseline (MB-052, probe S5), v = 0.25: block 1 retires at 16 ms with 0-50 ms RMS -62.4 dBFS against -35.8 at block 512; block 16 at 396 ms (300-600 ms RMS -77.7 against -52.4); block 32 at 583 ms; block 512 at 928 ms. v = 0.8: block 1 at 15 ms, block 16 at 760 ms against 1397 ms at block 512.

**2. Fix.**

- `plugins/membrum/src/dsp/drum_voice.h`:
  - Constants `kAutoReleaseThreshold = 1.0e-3f` (the pool's -60 dBFS, measured on the same signal: the voice output after `level`, before pan), `kAutoReleaseGraceMs = 10.0f`, `kAutoReleaseHoldMs = 50.0f`; sample counts computed in `prepare` with rounding.
  - Members `int quietRunSamples_` (consecutive output samples below the threshold) and `bool releaseLatched_`; `noteOn` clears both.
  - Update `quietRunSamples_` per output sample in the post-chain loops of `processBlockFast` (`:876-900`), of `processBlockSlow` (`:1015-1062`), and in `process()`.
  - At every voice-time grid point (T1.11's `kControlGridSamples` and `voiceSampleCount_`): if `!releaseLatched_ && voiceSampleCount_ >= graceSamples && quietRunSamples_ >= holdSamples`, call `noteOff()`. The release therefore starts at the same voice sample whatever the host block size.
  - While the detector is armed (`active_ && !releaseLatched_`), the renderers cut at grid points even when no modulation runs: extend T1.11's "needs control" condition with `!releaseLatched_`. If T1.11 was stopped and is not committed, add the voice-time counter and the grid cut here as T1.11 step 2 describes (the cut and the counter only, not the retune).
  - `noteOff()` sets `releaseLatched_` and returns early when it is already set: a voice is released once, by MIDI or by the detector, whichever comes first.
  - Update the safety-net comment (`:652-659`), which describes the pool's auto-noteOff.
- `plugins/membrum/src/voice_pool/voice_pool.cpp`: delete the `peak < kSilenceThreshold` auto-release block(s); keep the per-block peak in `meta_[slot].currentLevel` (stealing still uses it). Rewrite the threshold comment (`:331-350`) to say the voice owns auto-release, and remove `kSilenceThreshold` where it becomes unused.

**3. Build and gate.**

- Targets: `membrum_tests Membrum`.
- `test_kit_switch_infinite_ring.cpp` must stay green: it is the guard against voices that never retire.
- Timing lane alone if T1.11's grid cut now runs for unmodulated pads: `node tools/run-cpu-tests.js membrum_tests`, no budget relaxed.
- PRESET-IMPACT: tails now continue for the 50 ms hold before the release starts, and small-block hosts stop cutting hits. Kit-quality probe before and after (decay, rms500 per pad), table, stop for the ruling.

**4. Acceptance.**

- Retirement at blocks 1, 16, 32 and 64 within 50 ms of block 512, for v = 0.25 and 0.8 (baseline spread 16-928 ms and 15-1397 ms).
- 0-50 ms RMS within 1 dB of block 512 (baseline -62.4 against -35.8 dBFS at block 1), 300-600 ms RMS within 1 dB (baseline -77.7 against -52.4 dBFS at block 16).
- The infinite-ring tests green; kit-probe table and ruling recorded.

### T1.14 — Note events start at their sampleOffset, a zero-sample process() call still applies parameter changes, and the render is invariant to the host block size

**Findings:** MB-051 (high, CONFIRMED), MB-089 (medium, CONFIRMED), MB-141 (high, CONFIRMED).

**Flags:** none.

**Prerequisites:** T1.8, T1.9, T1.11, T1.13.

**Estimate:** 300 min.

Context. `Processor::processEvents` (`plugins/membrum/src/processor/processor.cpp:559-599`) never reads `Event::sampleOffset` (in `src` it is read only for parameter queues, `:164-165`), and `process()` applies parameter changes, all events and the audition (`:620-622`) before a single `VoicePool::processBlock` call over the whole block (`:636` mono, `:663-666` multi-bus). Every note starts at sample 0 of its block, a same-pad flam inside one block collapses into a steal at sample 0, and a note-off in the same block as its note-on gates and damps the voice before its first sample (MB-051). `process()` returns at `data.numSamples == 0` (`:613-614`) before `processParameterChanges`, so a host's parameter flush (`extern/vst3sdk/pluginterfaces/vst/ivstaudioprocessor.h:188-189`) is lost, and so are events sent in it (MB-089). No test checks onsets against `sampleOffset` or renders the same stream at several block sizes (MB-141). The prerequisites make the voice itself block-size invariant (T1.8 retirement safety net, T1.9 pool bookkeeping, T1.11 control grid, T1.13 auto-release); this task removes the processor's contribution.

**1. Failing test.** Extend `plugins/membrum/tests/unit/processor/membrum_processor_tests.cpp` and reuse its `MembrumTestFixture` (`:64-156`; construct it with the block size and rate the case needs) and `Krate::Test::EventList` / `ParameterChanges` (`tests/test_helpers/vst_event_list.h`, `vst_param_changes.h`). Place an event at absolute sample `p` into the block that contains it, at `sampleOffset = p % blockSize`. Copy `detPad()` from `specs/membrum-audit/probes/audit_probes.cpp.txt` for a deterministic pad (noise and click off). All at 48 kHz.

- `TEST_CASE("Membrum: Note-on starts at its Event::sampleOffset", "[membrum][processor][timing]")`: block 512, `processor.voicePoolForTest().padConfigMut(0) = detPad()`, a note-on for 36 at offsets 0, 1, 100, 300 and 511 (fresh fixture each). REQUIRE the first sample with |x| > 1e-9 at index in `[offset, offset + 2]`. Baseline (audit probe 04a): first index minus offset = 1 / 0 / -99 / -510 for offsets 0 / 1 / 100 / 511.
- `TEST_CASE("Membrum: Two hits in one block start at their own offsets", "[membrum][processor][timing]")`: pads 36 and 38 set to `detPad()`; render A with note-ons 36 at 0 and 38 at 256, render B with only 36 at 0. REQUIRE the first index where |A - B| > 1e-9 in `[256, 258]`.
- `TEST_CASE("Membrum: A note-off in the same block as its note-on does not mute the hit", "[membrum][processor][timing]")`: note-on 36 at 0 and note-off 36 at 400 in one 512 block, against the same block without the note-off. REQUIRE samples 0-399 identical (max |difference| at most 1e-9).
- `TEST_CASE("Membrum: Render is invariant to the host block size", "[membrum][processor][timing]")`: default kit (as `initialize` builds it), notes 36 (default kick, pitch envelope) and 38 (default snare) unchanged, note 41 set to `BodyModelType::String` with Impulse and note 43 to `BodyModelType::NoiseBody` with Mallet through `padConfigMut`; the eight hits of audit probe 04b at samples 0, 3001, 7003, 12345, 20000, 26011, 33333, 40000 on notes 36, 38, 41, 43, 36, 38, 41, 43 (velocities 1.0, 0.8, 0.9, 0.7, 0.6, 1.0, 0.5, 0.9); 4 s rendered at blocks 32, 64, 256, 1024 and 4096 (fixture `maxSamplesPerBlock` = the block). REQUIRE for every pair of block sizes: RMS of the sample difference over the whole render at most -80 dBFS; the index of the last sample with |x| >= 3.16e-5 (-90 dBFS) within 32 samples. Baseline (probe 04b, max |difference| against block 32): -20.9 dBFS at 64, -9.8 at 512, -8.8 at 2048, -10.0 at 16384; every onset at the block start.
- `TEST_CASE("Membrum: A zero-sample process call applies flushed parameters and events", "[membrum][processor][state]")`: `data.numSamples = 0`, `data.numOutputs = 0`, `data.outputs = nullptr`, `inputParameterChanges` carrying `padParamId(3, kPadMaterial)` = 0.83 and `inputEvents` carrying a note-on for 36. REQUIRE `process()` returns `kResultOk`; `voicePoolForTest().padConfig(3).material` within 1e-6 of 0.83; `getState` into a `MemoryStream` and `setState` into a second Processor gives the same pad 3 material; the next normal 512-sample block has a peak above 1e-3 (the flushed note sounds). Baseline: the change and the note are dropped.

**2. Fix** (`plugins/membrum/src/processor/processor.cpp`, `Processor::process` and `processEvents`; declarations in `processor.h`):

- Order in `process()`: denormal guard; `processParameterChanges`; then, if `data.numSamples == 0`, apply every event at once (below) and `consumePendingAudition`, and return `kResultOk` without render, meters or CPU timing.
- Split the per-event body of `processEvents` (pitch range 36-67, velocity <= 0 as note-off) into `void applyNoteEvent(const Event& e)`.
- Render loop, allocation-free, iterating the `IEventList` in place: `pos = 0`; for each event in list order, `off = std::clamp(event.sampleOffset, pos, numSamples - 1)` (an out-of-order or out-of-range offset is applied at the current position, never rendered backwards); if `off > pos`, render `[pos, off)` and set `pos = off`; `applyNoteEvent(event)`. After the loop render `[pos, numSamples)`. `consumePendingAudition()` stays at sample 0, before the first segment.
- `renderVoices(int32 start, int32 len)`: the existing mono / multi-bus dispatch (`:633-666`) with `outL + start`, `outR + start` and, in a stack copy of the aux pointer arrays, every non-null aux pointer `+ start`. The aux zeroing (`:653-659`) runs once per block before the first segment. The aux silence flags, coupling, bus limiter, master gain, main silence flag, glow and meters run once over the whole block after the last segment, unchanged.
- With `numOutputs == 0` or `outputs == nullptr` and `numSamples > 0`, still apply every event.
- `VoicePool` needs no API change: each segment is a normal `processBlock` call (it zeroes its own range of the main bus and advances `sampleCounter_` by the segment length).
- If the invariance case is still red after this, find the source by rendering with global coupling 0 and comparing again. If the residual comes from the coupling engine or the bus limiter, stop and report the measured value; do not change those in this task.

**3. Build and gate.** Targets `membrum_tests Membrum`. Nothing else differs.

**4. Acceptance.**

- Onset within `[offset, offset + 2]` for offsets 0, 1, 100, 300 and 511 (baseline 1 / 0 / -99 / -510).
- The second hit of a block starts within 2 samples of its offset; a same-block note-off leaves the samples before it identical.
- Pairwise RMS difference at most -80 dBFS across blocks 32, 64, 256, 1024 and 4096, tails within 32 samples (baseline -20.9 to -8.8 dBFS max difference).
- The flushed pad 3 material reads 0.83 in the processor and after a state round trip; the flushed note sounds.

### T1.15 — The tone-shaper filter starts every note at its own cutoff and type, with no glide from the slot's previous note

**Findings:** MB-042 (high, CONFIRMED), MB-265 (high, CONFIRMED).

**Flags:** PRESET-IMPACT.

**Prerequisites:** none.

**Estimate:** 120 min.

Context. `ToneShaper::prepare` sets the SVF to 20 kHz while smoothing is still off and then enables 5 ms coefficient smoothing (`plugins/membrum/src/dsp/tone_shaper.h:79-83`). At note-on, `applyPadConfigToSlot` (`voice_pool.cpp:232`, `pad_config_apply.h:78-86`) calls `setFilterType` / `setFilterCutoff` / `setFilterResonance` (`tone_shaper.h:134-165`), which with smoothing on only move `gTarget_`, `kTarget_` and the mode-mix targets (`dsp/include/krate/dsp/primitives/svf.h:188-195`, `:204-211`). `ToneShaper::noteOn` then calls only `filter_.resetIntegrators()` (`tone_shaper.h:348`; `svf.h:295-298` clears `ic1eq_` / `ic2eq_` and keeps `g_`, `k_`, `m0..m2`), so the note starts from whatever the slot's previous filtered note left: 20 kHz on a fresh slot. Because `g = tan(pi fc / fs)` spans decades, closing from 20 kHz takes 20-55 ms, not 5 ms (`specs/membrum-audit/probes/svf-glide-times.txt`). `SVF::reset()` (`svf.h:277-287`) zeroes the integrators AND snaps the coefficients, and is reached today only from `DrumVoice::resetForKitSwitch` (`drum_voice.h:477`). The comment at `tone_shaper.h:321-331` rejected `snapToTarget()` only because it keeps the integrators; `reset()` does both.

**1. Failing test.**

- `plugins/membrum/tests/unit/tone_shaper/test_tone_shaper.cpp`, `TEST_CASE("ToneShaper: a note starts at its own filter cutoff and type", "[tone_shaper][filter]")`, 48 kHz, white noise from `generateWhiteNoise` (`tests/test_helpers/test_signals.h`):
  - SECTION "fresh": `prepare(48000)`, `setFilterType(Lowpass)`, `setFilterCutoff(300)`, `setFilterEnvAmount(0)`, `noteOn(1.0f)`, noise through `processSample`. REQUIRE output RMS over the first 240 samples (5 ms) at least 20 dB below the input RMS over the same samples. Today the filter is near 20 kHz there (fc still 16.7 kHz at 5 ms for a 20 Hz target at 44.1 kHz).
  - SECTION "after a highpass note": same instance, `setFilterType(Highpass)`, `setFilterCutoff(14000)`, `noteOn`, 100 ms of noise; then `setFilterType(Lowpass)`, `setFilterCutoff(300)`, `noteOn`, same noise. REQUIRE the 0-5 ms output RMS within 0.5 dB of the "fresh" SECTION's value.
- New file `plugins/membrum/tests/unit/processor/test_filter_slot_history.cpp` (append to `membrum_tests`), `TEST_CASE("Tone-shaper filter sounds the same on a fresh and a warm voice slot", "[processor][tone_shaper][filter]")`, a Processor at 48 kHz (setup as in `membrum_processor_tests.cpp`), following `specs/membrum-audit/probes/audit_filter_glide_probe.cpp.txt` scenario A (`:324-339`): `kMaxPolyphonyId` = 0.0 (4 voices) and `kVoiceStealingId` = 1.0 (Priority, so slot 0 is reused); for the target pad `padParamId(pad, kPadTSFilterType)` = 0.0 (lowpass), `kPadTSFilterCutoff` = 0.4 (317 Hz), `kPadTSFilterEnvAmount` = 0.5 (amount 0); hits at 0 and 1 s, note-offs at +100 ms. Per hit compute rms500 (0-500 ms) and hi50 (power at or above 2 kHz in the first 50 ms, Hann window, as the probe's `bandPowers` does at `:202-233`).
  - SECTION pad 42 (default-kit hat): REQUIRE |rms500(hit 1) - rms500(hit 2)| <= 1 dB and |hi50(hit 1) - hi50(hit 2)| <= 2 dB. Baseline (`filter-glide.csv`): rms500 -49.84 fresh against -95.92 dBFS warm (46 dB).
  - SECTION pad 38 (default-kit snare), the same REQUIREs. Baseline: rms500 -36.01 against -36.82, hi50 29.10 against -6.11 dB (35 dB).
  - SECTION "after another pad" (MB-042): an LP 300 Hz pad with the click layer on, hit on a fresh slot, hit again, and hit on a slot last used by a highpass pad; REQUIRE 0-5 ms peak and RMS of all three within 0.5 dB. Baseline: fresh 0.2863 / 0.16555, repeat 0.2118 / 0.13612, after HP 0.1527 / 0.06635 (peak / RMS).

**2. Fix.**

- `ToneShaper::noteOn` (`tone_shaper.h:319-363`): replace `filter_.resetIntegrators();` (`:348`) with `filter_.reset();`. It zeroes the integrators and snaps `g`, `k` and `m0..m2` to the targets `applyPadConfigToVoice` set just before (`voice_pool.cpp:232-233`). The forced first cutoff update (`cutoffUpdateCounter_ = kCutoffUpdateInterval`, `:351`) then starts from the base cutoff times `8^(amount x env)`, with the envelope restarted from 0, so no jump remains. In-note smoothing stays enabled for the filter envelope.
- Rewrite the comment `:321-347`: item (1) now says the SVF is reset (integrators cleared, coefficients snapped to this pad's cutoff, type and resonance), so the hit sounds the same whatever played on the slot before; drop the `snapToTarget()` remark.
- No change to `svf.h`.

**3. Build and gate.**

- Targets `membrum_tests Membrum`.
- PRESET-IMPACT: 85 of 459 enabled factory pads engage the filter (30 LP, 38 HP, 17 BP; 60 with an envelope amount; `specs/membrum-audit/probes/factory-filter-pads.csv`), among them every snare in Acoustic Studio, Rock, Vintage Wood, Orchestral, 808, 909, LinnDrum and Trap, the HP 800 Hz open hats and all 16 Ghost Bones pads. Their first hit on a fresh slot changes. Kit-quality probe before and after, table for those pads, stop for the ruling.

**4. Acceptance.**

- ToneShaper unit case: 0-5 ms output at least 20 dB below the input on a fresh instance; after a highpass note within 0.5 dB of fresh.
- Hat 42 at LP 317 Hz: fresh and warm rms500 within 1 dB (baseline 46 dB apart), hi50 within 2 dB. Snare 38: the same (baseline hi50 35 dB apart).
- LP 300 Hz pad: fresh, repeat and after-HP 0-5 ms peak and RMS within 0.5 dB (baseline 0.2863 / 0.2118 / 0.1527 peak).
- Record (no bar) the 0-500 ms RMS of an HP pad at normalised cutoff 0.95 (14159 Hz) and 0.999 (19862 Hz); the baseline reads -36.3 dBFS for both.
- Kit-probe table and ruling recorded.

### T1.16 — The Feedback exciter runs below the clip rail and Feedback Amount sets its sustain

- **Findings:** MB-017 (high, CONFIRMED).
- **Flags:** PRESET-IMPACT.
- **Prerequisites:** T1.6 (strike-normalisation probe and cache rework), T1.7 (Feedback voices retire).
- **Estimate:** 180 min.

Today a Feedback-exciter voice skips the strike normalisation. `DrumVoice::noteOn` (`plugins/membrum/src/dsp/drum_voice.h:269-276`, the `feedbackExciter` branch inside the output-stage block) sets `bank.setOutputGain(1.0f)` and `bank.setOutputSoftClipThreshold(0.707f)`. The raw modal sum runs far above 0.707, so `ModalResonatorBank::applyOutputStage` (`dsp/include/krate/dsp/processors/modal_resonator_bank.h:894-900`, `softClip(x / t) * t`, with `softClip` returning exactly ±1 beyond ±3 at `dsp/include/krate/dsp/core/dsp_utils.h:105-112`) pins the bank at ±0.707 for 1-3 s. The voice output is that square wave times the amp envelope (`processBlockSlow`, `drum_voice.h:1026-1061`, ending in `hardClip(shaped * env * level)` at :1061). The loop never leaves the clip, so `FeedbackExciter::setFeedbackAmount` (`plugins/membrum/src/dsp/exciters/feedback_exciter.h:100-103`, applied in `trigger` at :105-126) cannot change anything audible.

1. **Failing test.** New file `plugins/membrum/tests/unit/dsp/test_feedback_exciter_headroom.cpp`, added to the `membrum_tests` source list. Build the voice the way `plugins/membrum/tests/unit/dsp/test_body_headroom_budget.cpp:35-77` does (`VoicePool` at 48 kHz, block 64, `setMaxPolyphony(1)`, pad 0, every non-body layer off: noise mix 0, click mix 0, shell off, NLC 0, modeInject 0, drive and fold 0, pitch env time 0, tension 0, morph off), with `kPadExciterType = Feedback`, Material 0.3, Size 0.8, Decay 0.85, Level 1.0. Copy that file's `configureBodyOnly` idea into the new file's anonymous namespace with the exciter as a parameter; do not edit the existing file. Use `Krate::DSP::TestUtils::calculateCrestFactorDb` from `tests/test_helpers/signal_metrics.h` for the crest factor. Two cases, tags `[feedback_exciter][gain_staging]`:
   - `TEST_CASE("Feedback exciter: voice stays under the body budget and off the clip rail on every modal body", "[feedback_exciter][gain_staging]")`. For each modal body (Membrane, Plate, Shell, Bell, NoiseBody; String runs a waveguide and is T2.11's) and each velocity in {0.3, 0.8, 1.0}, render 1.5 s of one hit with Feedback Amount 0.5. Assert: (a) crest factor of the output over 0.2-1.0 s > 3 dB; (b) at velocity 1.0 the output peak ≤ `kBodyHeadroom` × level × 1.12 = 0.56; (c) peak is non-decreasing in velocity across {0.3, 0.8, 1.0}. Today (a) reads 0.0 dB (the 200 ms windows of the audit probe read RMS 0.565 = peak 0.5656 on Membrane at v 0.8) and (b) reads 0.707.
   - `TEST_CASE("Feedback exciter: Feedback Amount sets the sustain", "[feedback_exciter][gain_staging]")`. Membrane and Plate, velocity 0.8, Feedback Amount 0, 0.5 and 1.0. Assert: RMS over 0.2-1.2 s differs by more than 3 dB between Amount 0 and Amount 1; the time for the 50 ms RMS envelope to fall 40 dB below its maximum increases strictly from Amount 0 to 0.5 to 1.0; and at Amount 1.0 and velocity 1.0 the voice still retires (`VoicePool::getActiveVoiceCount() == 0`) within 5 s, which keeps T1.7's guarantee.
   Run both by name; both must fail on today's code for the clip reason (crest 0 dB, peak 0.707, Amount 0 vs 1 at 0.565 vs 0.565 RMS).
2. **Fix.**
   - `plugins/membrum/src/dsp/drum_voice.h`, `DrumVoice::noteOn`, the `feedbackExciter` branch (:269-276): normalise the Feedback path like every other modal path. Set the bank's output gain from the strike-normalisation path as T1.6 left it (today `measuredStrikeOutputGain(bank, params_, body, exciter)`, :1394-1412, through `probeStrikePeakExcited`, :1322-1363), and turn the bank's soft clip off (`setOutputSoftClipThreshold(0.0f)`) or keep it only as a safety net at a threshold the normal path never reaches: measure that a signal at `kBodyHeadroom` passes it with less than 0.1 dB of gain change, and say so in a comment. Rewrite the comment block at :264-268, which says the loop is "calibrated against the clipped output".
   - The probe must measure the Feedback strike open loop: in `probeStrikePeakExcited` (or its T1.6 replacement) pass `0.0f` instead of `lastBody` to `exciterBank_.process` when the exciter is Feedback (the comment at :1348-1349 says Feedback never reaches the probe; after this change it does). Driving the loop closed at unity bank gain is exactly the saturation this task removes. The open-loop strike does not depend on Feedback Amount (it multiplies the zero feedback input), so the cache key needs no new field; if T1.6's key already contains Feedback Amount, leave it.
   - `plugins/membrum/src/dsp/exciters/feedback_exciter.h`: once the bank runs below the rail, retune what Feedback Amount does so it sets the sustain audibly. The loop's tail is bounded today by `amplitudeEnv_`, which decays to -60 dB in `kDecayTauSec` = 0.8 s (:47, :76, applied at :175 and :184) whatever the amount, and the energy limiter threshold `kEnergyThreshold` = 0.35 (:45) was set for a body ringing at the clip. Make the sustain grow with `feedbackDrive_` (for example scale the `amplitudeEnv_` decay time or the loop gain `feedbackAmount_` with it) while keeping `feedbackAmount_ ≤ kMaxFeedback` and the SC-008 output bound (:177-182). Velocity must keep driving brightness (the cutoff law at :116; the SC-004 cases in `plugins/membrum/tests/unit/exciters/test_feedback_exciter.cpp` and `test_velocity_mapping.cpp` must stay green). Update the header comment (:6-20) to the new law.
   - Before: bank at gain 1 with a 0.707 soft clip, output a clipped square at 0.707 × v, Amount inaudible. After: bank normalised to `kBodyHeadroom` against its open-loop strike, no clipping on a normal hit, Amount lengthens the tail.
   - Do not touch the Friction or `modeInject` sustain behaviour, and do not give Feedback an unbounded sustain: T6.10 owns the documented sustain constraints.
3. **Build and gate.** Standard steps; add `Membrum` to the build targets. PRESET-IMPACT: 22 enabled factory pads use the Feedback exciter (Modular West Coast, Chaos Engine, Drone and Sustain, Experimental FX Kit; `specs/membrum-audit/probes/kit-pads.csv`). Run the kit-quality probe before and after the fix and put the per-pad table (peak, rms500, decay, centroid, limiter margin, plus any near-duplicate pair that appears or disappears in `kit-pairs.csv`) in the commit message. Kit data is not retuned here. The audible change is this task's purpose: show the table and stop for the user's ruling before the commit.
4. **Acceptance.** Both new cases green. Baselines: Membrane v 0.8 crest 0.0 dB, consecutive samples at -0.5656; peak 0.0707 / 0.2121 / 0.3535 / 0.4949 / 0.7070 at v 0.1 / 0.3 / 0.5 / 0.7 / 1.0 (exactly 0.707 × v); Amount 0 vs 1: 0.565 vs 0.565 RMS. Kit probe before the fix: Modular West Coast 50 and Experimental FX Kit 38, 51, 54, 57 and 60 peak at -7.00 dBFS, on the bus limiter (63 at -7.16). After the fix no Feedback pad sits on the limiter from a single velocity-100 hit. The existing `FeedbackExciter: peak <= 0 dBFS for extreme bodyFeedback (SC-008)` case and T1.7's retirement test stay green.

### T1.17 — Friction exciter output rises with velocity

- **Findings:** MB-019 (high, CONFIRMED).
- **Flags:** PRESET-IMPACT.
- **Prerequisites:** none.
- **Estimate:** 120 min.

`FrictionExciter::trigger` (`plugins/membrum/src/dsp/exciters/friction_exciter.h:72-97`) sets bow pressure 0.1 + 0.4v (+ 0.5 × Friction Pressure, :79-81), bow speed 0.2 + 0.6v (:82) and a bandpass at 400 × 20^v Hz (:86). `process` drives the bow with zero body feedback (`core_.process(0.0f)`, :124), so the differential velocity equals the bow velocity, clamped to v × speed in `Krate::DSP::BowExciter` (`dsp/include/krate/dsp/processors/bow_exciter.h:292-296`). The STK friction table there (:315-323: slope = 5 - 4p, refl = clamp(1 / (|dv × slope| + 0.75)^4, 0.01, 0.98), force = dv × refl) gives a smaller force for a larger dv once dv passes about 0.25 / slope. The exciter peak therefore falls 23.3 dB from v 0.1 to v 1.0. The voice normalises the body against a probe at velocity 1.0 (`drum_voice.h:1336-1338`), which is the weakest hit, so every softer hit is boosted past `kBodyHeadroom` into the voice `hardClip`.

1. **Failing test.** Two cases, tags `[friction_exciter][velocity]`:
   - In `plugins/membrum/tests/unit/exciters/test_friction_exciter.cpp`: `TEST_CASE("FrictionExciter: peak rises monotonically with velocity", "[friction_exciter][velocity]")`. Use the existing setup in that file (and `plugins/membrum/tests/unit/exciters/exciter_test_helpers.h`). At 44.1 kHz, Friction Pressure 0 and again at 0.5, trigger at v ∈ {0.1, 0.2, 0.3, 0.5, 0.7, 1.0} on a fresh exciter each time, run 60 ms with `process(0.0f)` and record the peak. Assert the peak is non-decreasing in v and that peak(1.0) / peak(0.25) > 2 (+6 dB).
   - New file `plugins/membrum/tests/unit/dsp/test_friction_velocity_response.cpp` (added to `membrum_tests`): `TEST_CASE("Friction pads: voice peak rises with velocity and stays under the body budget", "[friction_exciter][velocity][gain_staging]")`. Same `VoicePool` set-up as `test_body_headroom_budget.cpp:35-77` (48 kHz, block 64, one voice, every non-body layer off, click mix 0) with `kPadExciterType = Friction`, Level 1.0, on Membrane, Bell and Plate. Render 0.5 s at v ∈ {0.1, 0.2, 0.3, 0.5, 0.7, 1.0}. Assert: the voice peak is non-decreasing in v; every peak ≤ `kBodyHeadroom` × 1.12 = 0.56; and the spec 136 SC-005 bar holds, peak(127/127) / peak(30/127) > 6 dB on each body.
   Both fail today: exciter peaks 0.0187 / 0.0175 / 0.0119 / 0.0065 / 0.0035 / 0.0013 at v 0.1 ... 1.0; voice peak on Membrane 0.700 at v 0.1, 1.000 (the rail) at v 0.2-0.7, 0.500 at v 1.0.
2. **Fix.** In `plugins/membrum/src/dsp/exciters/friction_exciter.h`, `FrictionExciter::trigger`, change the velocity mapping so the bow force rises with velocity. The intended edit keeps the bow on the rising branch of the friction table: with zero feedback and refl clamped at 0.98, force ≈ 0.98 × dv up to dv* ≈ 0.25 / slope, so scale the bow speed so that `maxVelocity × speed` (`bow_exciter.h:153`, :292-293) stays at or below dv* for the configured pressure, and let velocity set the force linearly through that range. If the stick-slip roughness or the SC-004 brightness spread suffers, an equivalent alternative is a per-trigger output gain that makes the exciter peak proportional to velocity. Keep: the bandpass brightness law (:86), so the existing `FrictionExciter: velocity spectral centroid ratio >= 2.0 (SC-004)` case and `test_velocity_mapping.cpp` stay green; the non-monotonic first-50-ms envelope (`FrictionExciter: first-50-ms envelope is non-monotonic (US1-4)`); and the 41 ms auto-release (:43-44, :112-115), since T6.10 owns the Friction sustain question. Do not edit `bow_exciter.h` (shared with other plugins): the fix is the plugin-local mapping. Update the header comment's velocity mapping (:11-13). Before: the exciter is loudest at the softest hit. After: the exciter's peak rises with velocity, so the voice's strike normalisation at v = 1 calibrates the loudest hit, as it already does for the other exciters.
3. **Build and gate.** Standard steps; add `Membrum` to the build targets. PRESET-IMPACT: 38 enabled factory pads use Friction (`kit-pads.csv`). Run the kit-quality probe before and after and put the per-pad table in the commit message. The audible change is the finding's purpose: show the table and stop for the user's ruling before the commit. No kit data changes here.
4. **Acceptance.** Both cases green, with the measured peaks and the SC-005 ratio in the commit message. Baselines: exciter v 0.1 0.01869 (-34.6 dB) to v 1.0 0.00128 (-57.9 dB); voice Membrane v 0.2-0.7 at 1.000, v 1.0 0.500; Bell v 0.2-0.5 at 1.000, v 1.0 0.532; Plate v 0.2-0.5 at 1.000, v 1.0 0.500. Kit probe before the fix: Chaos Engine 51, Drone and Sustain 36 and 37, Experimental FX Kit 39, Ghost Bones 47 and Glass Bell Garden 61 peak at -7.0 dBFS (on the bus limiter); after the fix none of them does from one velocity-100 hit.

### T1.18 — FMImpulse strike level follows the note's own velocity, and its sidebands follow the body's pitch

- **Findings:** MB-260 (high, CONFIRMED), MB-261 (low, PLAUSIBLE).
- **Flags:** PRESET-IMPACT, VERIFY-FIRST (MB-261 only).
- **Prerequisites:** T1.6.
- **Estimate:** 180 min.

The strike-normalisation probe triggers the exciter at velocity 1.0 (`probeStrikePeakExcited`, `plugins/membrum/src/dsp/drum_voice.h:1336-1338`) and the cached gain is keyed without velocity (`StrikeNormKey`, :1717-1727; `measuredStrikeOutputGain`, :1394-1412). For FMImpulse, velocity moves the spectrum, not just the level: `baseFreq = 400 × 6.25^v` (`plugins/membrum/src/dsp/exciters/fm_impulse_exciter.h:83`), modulator at `baseFreq × (1 + 3 × fmRatioNorm)` (:80, :86), modulation index 0.5 + 2.5v (:90), amplitude v (:93). On sparse high-Q Bell bodies the gain measured with the 2500 Hz carrier is wrong for every other velocity (MB-260). MB-261 claims a second cause: the carrier sits at fixed Hz rather than tracking the body, so FM Ratio steps land sidebands on or off a long-ringing mode. This part works on the code as T1.6 left it; re-read T1.6's probe and cache before editing.

1. **Failing test.** New file `plugins/membrum/tests/unit/dsp/test_fm_strike_velocity.cpp`, added to `membrum_tests`. Load a factory kit's pad through the state codec: read the `.vstpreset` under `MEMBRUM_RESOURCES_DIR "/presets/Kit Presets/<Category>/<Kit>.vstpreset"`, extract the component-state chunk with `PresetFile`, `State::readKitBlob`, `State::applyPadSnapshot` into a `PadConfig`, then `applyPadConfigToVoice` and `noteOn` on a fresh heap-allocated `DrumVoice` (`prepare(48000, 0)`), rendered with `processBlock` in 256-sample blocks. `specs/membrum-audit/probes/audit_limiter_probe.cpp.txt` (`loadKit` :71-94, `hit` :127-163, `neutralAll` :167-176, `freshVoice` :181-186) shows the exact sequence: port those helpers into the test's anonymous namespace. Two cases:
   - `TEST_CASE("FMImpulse strike: voice peak follows velocity on bell pads", "[fm_impulse][gain_staging][velocity]")` (MB-260). Pads: Glass Bell Garden note 46, Latin Perc note 46, LinnDrum CR-78 note 40, each with the post-chain neutral (`neutralAll`). For each MIDI velocity k/127, k = 32, 36, ..., 124, 127, render 0.6 s with a fresh voice. Normalised peak = peak / (level × v). Assert: normalised peak ≤ 0.56 (`kBodyHeadroom` × 1.12) at every velocity, and the voice peak is non-decreasing in velocity within 1 dB (peak(k_{n+1}) ≥ peak(k_n) × 10^(-1/20)). Today GBG 46 reads 1.846 normalised at v 100/127 and its pre-limiter peak goes +2.26 dBFS at v 0.78 against -6.60 at v 1.0.
   - `TEST_CASE("FMImpulse: FM Ratio steps do not jump the ring level", "[fm_impulse][velocity]")` (MB-261, the VERIFY-FIRST test). Default kit (`DefaultKit::apply`) pad 6 (MIDI 42), exciter forced to FMImpulse, velocity 100/127, FM Ratio swept 0..1 in 1/32 steps, a fresh voice per step, full post-chain as configured. Measure the RMS over the first 500 ms (rms500). Assert every adjacent-step difference < 6 dB.
   Confirm the MB-260 case fails before any edit. For MB-261, the VERIFY-FIRST rule is applied after the MB-260 part of step 2 lands, not on today's code: today's normalisation is wrong at v 100/127 for MB-260's reason, so a red sweep on today's code cannot tell the two causes apart. Run the MB-261 case once on today's code (it is expected red) and once after the MB-260 fix. If it then passes, MB-261 is refuted: keep the test, skip the carrier change below, and record "refuted" with both runs' output in the commit message.
2. **Fix.**
   - MB-260, in `plugins/membrum/src/dsp/drum_voice.h`: probe the FMImpulse strike at the note's own velocity. Pass the note's velocity from `noteOn(float velocity)` into the strike-normalisation call (today :302-304) and on to the probe. For FMImpulse the probe triggers `exciterBank_.trigger(vNote)` instead of 1.0 (:1338) and the gain becomes `kBodyHeadroom × vNote / peak`. That keeps today's level law exactly where the spectrum does not move (body peak 0.5 × v before the amp envelope, as for a linear exciter) and leaves every other exciter on the velocity-1.0 probe. Key the cached gain on the MIDI-quantised velocity (`std::lround(v × 127)`), FMImpulse only (a fixed sentinel for the other exciters), so the key is exact for every MIDI note.
   - Cost: the probe must stay where T1.6 put it. If T1.6 computes gains off the audio thread per pad configuration, compute a 128-entry velocity table for each FMImpulse pad there. If T1.6 keeps a cache that can miss on the audio thread, velocity becomes part of that key, and T1.6's audio-thread cost test is extended with a case that plays 127 different velocities on one FMImpulse pad and must still pass. Report the measured cost in the commit message.
   - MB-261 (only if its test is still red after the MB-260 part): make the FM carrier track the body. Add `FMImpulseExciter::setReferenceFrequencyHz(float)` (forwarded through `ExciterBank` the way `setMalletBodySizeHint` is at `drum_voice.h:340`), called from `DrumVoice::noteOn` with `naturalFundamentalHz_` before `exciterBank_.trigger(velocity)` (:343). In `FMImpulseExciter::trigger` (:83) compute `baseFreq` as the reference frequency times a velocity multiplier, so the carrier-to-body ratio is independent of Size and velocity still raises brightness: the `FMImpulseExciter: velocity spectral centroid ratio >= 2.0 (SC-004)` and `velocity drives modulation index / high-order sidebands (L-3/L-4)` cases must stay green. Keep `fmRatioNorm`'s mapping (:80). Update the header comment (:6-15, :73-76). This changes the timbre of every FMImpulse pad: the PRESET-IMPACT ruling below covers it, and it must be shown separately from the MB-260 table.
3. **Build and gate.** Standard steps; add `Membrum` to the build targets. PRESET-IMPACT: 68 enabled factory pads use FMImpulse (`kit-pads.csv`). Run the kit-quality probe and the limiter probe (`audit_limiter_probe.cpp.txt`, whose CSVs are `limiter-pads.csv`, `limiter-velocity-sweep.csv`, `limiter-fm-ratio-sweep.csv`) before the fix, after the MB-260 part and, if it runs, after the MB-261 part. Put the per-pad tables in the commit message, show them and stop for the user's ruling before the commit.
4. **Acceptance.** MB-260 case green; the limiter probe's summary line `FMImpulse n 66 neutral787Norm ... >0.707(+3dB)` drops from 14 to 0. Baselines (`limiter-probe-summary.txt`, `limiter-velocity-sweep.csv`): normalised peak at v 100/127 of 1.846 (GBG 46), 1.483 (GBG 44), 1.369 (Latin Perc 46), 1.313 (LinnDrum 40), all exactly 0.500 at v 1.0; GBG 46 pre-limiter +2.24 dBFS; LinnDrum 40 jumps 7.5 dB between v 0.75 and 0.78. MB-261 case green after the fix, or refuted after the MB-260 part. Baseline (`limiter-fm-ratio-sweep.csv`, pad 42, v 0.787): rms500 -34.78 / -25.72 / -30.78 / -44.12 dB at FM Ratio 0.1875 / 0.21875 / 0.25 / 0.3125, largest adjacent jump 13.5 dB (0.84375 at -28.10 against 0.875 at -41.57); `param-audibility.csv` offset 32 reads `-47.1 -36.8 -49.0` with decay 0.33 / 1.02 / 0.52 s.

### T1.19 — Global Coupling survives setupProcessing and setActive: the amount is re-applied, and the engine, delay and energy envelope reset with the voices

- **Findings:** MB-067 (high, CONFIRMED), MB-115 (medium, CONFIRMED), MB-094 (low, CONFIRMED).
- **Flags:** CROSS-PLUGIN IMPACT.
- **Prerequisites:** none.
- **Estimate:** 120 min.

`Processor::setupProcessing` (`plugins/membrum/src/processor/processor.cpp:1067-1091`) calls `couplingEngine_.prepare(sampleRate_)` (:1081) and re-applies polyphony and stealing (:1076-1078), but never the stored Global Coupling. `SympatheticResonance::prepare` (`dsp/include/krate/dsp/systems/sympathetic_resonance.h:113-132`) snaps the amount smoother to 0 (:124-125) and leaves `couplingGain_` at its old value; only `setAmount` (:152-160) and `reset` (:135-140) write it. After a re-prepare with a non-zero amount, `isBypassed()` (:370-372) is false, so the per-sample coupling loop (processor.cpp:693-708) runs, while `process()` scales the input by a smoother parked at 0 (:319-322): coupling is silent, costs CPU and keeps the main-bus silence flag clear (processor.cpp:739-741). The only `setAmount` callers are processor.cpp:408 (`kGlobalCouplingId` in `processParameterChanges`) and :978 (`applyKitSnapshot`). Separately, `setActive(true)` (:1093-1116) re-prepares the voice pool and resets the bus limiter but not `couplingEngine_`, `couplingDelay_` or `energyEnvelope_`, so resonators and delay content from before the deactivation ring on with every voice gone (MB-094). `applyKitSnapshot` (:950-967) already shows the intended flush order: `couplingEngine_.reset(); couplingDelay_.reset(); energyEnvelope_ = 0.0f;` and only then `setAmount` (:978).

1. **Failing test.**
   - Shared header, in `dsp/tests/unit/systems/sympathetic_resonance_test.cpp` (built into `dsp_systems_tests`): `TEST_CASE("SympatheticResonance: prepare leaves a consistent bypassed state until setAmount is called again", "[systems][sympathetic]")`. `prepare(44100)`, `setAmount(0.5f)`, register a partial set with `noteOn`, process a few samples, then `prepare(96000)`. Assert `isBypassed()` is true right after the re-prepare (today false: smoother 0, `couplingGain_` non-zero), and that after `setAmount(0.5f)` plus a `noteOn` and an input burst the output is non-zero.
   - Membrum, three cases in `plugins/membrum/tests/unit/processor/test_coupling_phase9_verification.cpp`, using that file's `makeSetup`, `Krate::Test::EventListNoteIdEqPitch` (`tests/test_helpers/vst_event_list.h`) and `Krate::Test::ParameterChanges` (`tests/test_helpers/vst_param_changes.h`). Measure the coupling return alone the way T060 in the same file does (:87ff): two output buses, every pad routed to aux bus 1, so the main bus carries only coupling.
     - `TEST_CASE("Coupling: Global Coupling survives setActive(false) -> setupProcessing -> setActive(true)", "[coupling][phase9][fr006][lifecycle]")`: Global Coupling 1.0 at 44.1 kHz, then the host's rate-change sequence to 96 kHz, then notes 36 and 41 at velocity 1.0. Assert the main-bus RMS over 50 blocks is within 1 dB of a second Processor set up at 96 kHz directly with the same parameter and notes. Today it is exactly 0.
     - `TEST_CASE("Coupling: setState then setupProcessing keeps the loaded Global Coupling", "[coupling][phase9][lifecycle]")`: `setState` with a kit blob whose `globalCoupling` is 0.5 (build it with `State::writeKitBlob` from a `KitSnapshot`), then `setupProcessing`, `setActive(true)`, notes, the same comparison against a Processor that received the blob after `setupProcessing`. Today the first one is silent.
     - `TEST_CASE("Coupling: setActive(true) flushes coupling resonators, delay and energy envelope", "[coupling][phase9][lifecycle]")`: Global Coupling 1.0, notes, 20 blocks, then `setActive(false)` and `setActive(true)` without `setupProcessing`, then one block with no events and no parameter changes. Assert `couplingEngineForTest().getActiveResonatorCount() == 0`, `energyEnvelopeForTest() == 0.0f` and every main-bus sample `== 0.0f`. Today the resonator count is non-zero and the block carries the old ring.
2. **Fix.**
   - `dsp/include/krate/dsp/systems/sympathetic_resonance.h`, `SympatheticResonance::prepare`: set `couplingGain_ = 0.0f` together with `amountSmoother_.snapTo(0.0f)` (:124-125), so a prepared engine is bypassed until `setAmount` is called, as after `reset()`. Document on `prepare` that the amount must be set again after it. This keeps Innexus bit-identical: `plugins/innexus/src/processor/processor.cpp` calls `setAmount` (:902) at the start of every block before `process` (:1914), and nothing reads `isBypassed()` there between the two.
   - `plugins/membrum/src/processor/processor.cpp`: add one private helper (declared in `processor.h`), for example `void resetCouplingState() noexcept`, that runs `couplingEngine_.reset(); couplingDelay_.reset(); energyEnvelope_ = 0.0f; couplingEngine_.setAmount(globalCoupling_.load(std::memory_order_relaxed));` in that order. Call it in `setupProcessing` after `couplingEngine_.prepare` / `couplingDelay_.prepare` / `setCouplingEngine` (replacing the bare `energyEnvelope_ = 0.0f` at :1084), in `setActive(true)` after `voicePool_.prepare` (:1097), and in `applyKitSnapshot` in place of :965-967 (keep the store of `globalCoupling_` before the helper runs, so the helper applies the kit's value; :974-978 then lose their own `setAmount`). Before: coupling silent after any re-prepare, and stale coupling ring across activation. After: the stored amount is always live, and activation flushes all coupling state with the voices.
   - Do not touch the double voice-pool prepare across `setupProcessing` and `setActive` that MB-094 mentions; it is not part of this task.
3. **Build and gate.** CROSS-PLUGIN IMPACT. Before editing, `grep -rl "sympathetic_resonance.h" plugins/ dsp/include tests/`; at commit `4a14376d` the consumers are `plugins/membrum/src/processor/processor.h`, `plugins/membrum/src/voice_pool/voice_pool.h`, `plugins/membrum/src/dsp/drum_voice.h`, `plugins/innexus/src/processor/processor.h`, `dsp/tests/unit/systems/sympathetic_resonance_test.cpp`, `plugins/innexus/tests/unit/processor/sympathetic_resonance_integration_test.cpp` and three Membrum coupling tests (`dsp/include/krate/dsp/effects/aether_reverb.h` includes only `sympathetic_resonance_simd.h`). List them in the commit message. Build `membrum_tests Membrum dsp_systems_tests` plus every executable `node tools/header-consumers.js --tag '[membrum]'` names (it should include `innexus_tests`), and gate them in one `run-gate.js` command with `~[long]` appended. Clang-tidy adds `-Target dsp`, and `-Target innexus` if Innexus is in the consumer list. pluginval for Membrum; Innexus only if its build output changed.
4. **Acceptance.** The four new cases green; the existing `Coupling T061` case and every `[coupling]` case green; `innexus_tests` green with no golden moved (a moved Innexus golden is a defect of this change, not something to re-baseline). Baseline: the coupling-only main bus after a re-prepare reads exactly 0 (static reading, MB-067 / MB-115); after the fix it matches a directly prepared Processor within 1 dB.

### T1.20 — Selected-pad proxies are UI-only binding targets: a gesture edits the selected pad's per-pad ID, automation stays on its pad, and per-pad changes refresh the proxy-bound controls

- **Findings:** MB-072 (high, CONFIRMED), MB-163 (high, CONFIRMED), MB-131 (medium, CONFIRMED), MB-099 (medium, CONFIRMED), MB-117 (low, CONFIRMED).
- **Flags:** UI.
- **Prerequisites:** none.
- **Estimate:** 360 min.

Every editor control is bound to a global proxy ID (`plugins/membrum/resources/editor.uidesc:29-38` and the rest of the control-tag block), and every proxy is registered `ParameterInfo::kCanAutomate` (the proxy registrations inside `Controller::initialize`, `plugins/membrum/src/controller/controller.cpp:281-670`, for example Material to Level at :281-295 and the Exciter and Body lists at :299-321). VSTGUI's `VST3Editor` sends a control gesture as `setParamNormalized(proxyId)` then `performEdit(proxyId)` (`extern/vst3sdk/vstgui4/vstgui/plugin-bindings/vst3editor.cpp:192-236`), so the host records automation on the proxy ID. The processor applies each proxy to whichever pad `selectedPadIndex_` names when the value arrives (`plugins/membrum/src/processor/processor.cpp:261-522`, for example `case kMaterialId:` at :267-268), and `kSelectedPadId` (:395-401) is itself automatable (controller.cpp:500-503) and performEdited on every grid click (:1056-1063). The controller mirrors a proxy into the per-pad ID only through the base setter (:861-891), never `performEdit`, so the host never sees the per-pad change; a selection change updates the proxies the same way (`syncGlobalProxyFromPad`, :961-985). A host write to a per-pad ID of the selected pad never reaches the proxy-bound controls (:893-916; MB-099). The processor's proxy cases also skip the reclassify and `recomputeCouplingMatrix` that the per-pad route runs for exciter, body and pitch-env time (:178-207 against :282-297 and :334-336; MB-131, MB-117).

**Ruling (user, 2026-11-24): accept.** The proxies stop being automatable, a host lane recorded on a proxy ID no longer plays, the per-pad IDs carry automation, and no parameter type or ID changes. Implement that; no stop before the commit. The CHANGELOG entry says that host lanes recorded on a selected-pad proxy no longer play.

1. **Failing test.** New file `plugins/membrum/tests/unit/vst/test_proxy_automation.cpp`, added to `membrum_tests`. It needs a recording `IComponentHandler`: move `KbRecordingHandler` from `plugins/membrum/tests/unit/preset/test_kit_browser_load.cpp:47-75` into a new `plugins/membrum/tests/unit/controller/recording_component_handler.h` and use it from both files (do not write a second copy). Use `Krate::Test::ParameterChanges` from `tests/test_helpers/vst_param_changes.h` to replay edits into a `Membrum::Processor`. Cases, tags `[pad_params][proxy][automation]`:
   - `TEST_CASE("Selected-pad proxy: a knob gesture is recorded on the selected pad's per-pad ID", ...)` (MB-072, MB-163): controller with the handler installed, select pad 2 through `setParamNormalized(kSelectedPadId, 2/31.0)`, then do what `VST3Editor` does for a Decay knob: `beginEdit(kDecayId)`, `setParamNormalized(kDecayId, 0.9)`, `performEdit(kDecayId, 0.9)`, `endEdit(kDecayId)`. Assert the handler recorded exactly one edit, on `padParamId(2, kPadDecay)` with 0.9, and none on `kDecayId`. Then select pad 1 and replay the recorded edits into a processor whose selected pad is 1: pad 2's `decay` becomes 0.9 and pad 1's is unchanged.
   - `TEST_CASE("Selected-pad proxy: proxies and Selected Pad are not automatable", ...)`: for every row of the controller's proxy table and for `kSelectedPadId`, `(info.flags & kCanAutomate) == 0`; every per-pad parameter keeps `kCanAutomate`.
   - `TEST_CASE("Selected-pad proxy: the processor ignores a proxy ID", ...)`: send `kBodyModelId` and `kMaterialId` straight to a processor (selected pad 3); pad 3's config and `couplingMatrixForTest()` are unchanged.
   - `TEST_CASE("Selected-pad proxy: a Body menu gesture reclassifies the pad", ...)` (MB-131, MB-117): controller gesture on `kBodyModelId` (NoiseBody) and on `kToneShaperPitchEnvTimeId` with pad 3 selected; replay the recorded edits into a processor; assert `couplingMatrixForTest()` equals that of a second processor that received the per-pad IDs directly, and differs from the matrix before the edit when the category changes.
   - `TEST_CASE("Selected-pad proxy: a host write to the selected pad's per-pad ID refreshes the proxy", ...)` (MB-099): select pad 3, `setParamNormalized(padParamId(3, kPadMaterial), 0.9)`, assert `getParamNormalized(kMaterialId) == 0.9`; the same for `padParamId(3, kPadTSPitchEnvTime)` against `kToneShaperPitchEnvTimeId`, plus the pitch-envelope display refresh bit set in the controller's pending-refresh word; a write to pad 4's per-pad Material leaves `kMaterialId` alone. The refresh flags are private (`controller.h:331`): add a test-only accessor next to the existing `...ForTest()` members (`controller.h:156-165`).
   Every case fails today for its stated reason: the edit is recorded on `kDecayId`, the flags carry `kCanAutomate`, the processor applies proxies (and skips the reclassify, so the matrix differs from the per-pad reference), the proxy stays stale.
2. **Fix.**
   - `plugins/membrum/src/controller/controller.cpp` / `controller.h`: override `beginEdit`, `performEdit` and `endEdit` (virtual in `EditController`, `extern/vst3sdk/public.sdk/source/vst/vsteditcontroller.h:72-74`). When the tag is a `kProxyMappings` row (:84-155), forward to the base class with `padParamId(pad, row.padOffset)` instead of the proxy ID, where `pad` is the selected pad latched at `beginEdit` for that proxy (so a gesture never splits across pads; a `performEdit` without a `beginEdit` uses `selectedPadIndex_`). Other tags pass through. This covers the uidesc controls and the custom views that call `performEdit` with proxy tags (:1296, :1432, :1604).
   - Registration in `Controller::initialize`: register every proxy and `kSelectedPadId` without `kCanAutomate`, with `ParameterInfo::kIsHidden` (keep `kIsList` where it is set; never `kIsReadOnly`, the grid's `kSelectedPadId` performEdit must still reach the processor, which saves `selectedPadIndex` in the kit blob). Same IDs, same parameter classes, same ranges and step counts. The per-pad parameters (:675-723) keep `kCanAutomate`.
   - `Controller::setParamNormalized`, per-pad branch (:893-916): when `padIdx == selectedPadIndex_` and a table row has that offset, set the proxy with the derived `setParamNormalized(row.globalId, value)` under `suppressProxyForward_`, so the refresh hooks at :797-839 run for the proxy tag and nothing forwards back. Keep the grid notifications.
   - `plugins/membrum/src/processor/processor.cpp`, `processParameterChanges`: delete every case whose ID is a `kProxyMappings` row, from the comment at :261-266 to `kWireCouplingId` at :520-522 (this includes the choke-group proxy ending at :386 and `kOutputBusId` at :389-394). Keep the non-proxy cases in and around that range: `kMaxPolyphonyId`, `kVoiceStealingId`, `kSelectedPadId`, `kGlobalCouplingId`, `kSnareBuzzId`, `kTomResonanceId`, `kCouplingDelayId` and `kMasterGainId`. A proxy ID then falls to `default: break;`. The per-pad route (:173-235) becomes the only way a pad field changes, which removes the second hand-written table and its missing reclassify (MB-131, MB-117); the per-pad choke route already updates the choke table (`voice_pool.cpp:641-648`).
   - Tests that send proxy IDs to the processor encode the removed routing: `plugins/membrum/tests/unit/processor/membrum_processor_tests.cpp` (:659-660, :780-781, :815-816, :914-916, :963, :984-988, :1014-1018, and the case `Phase 8C: kAirLoadingId processor dispatch routes to selected pad` at :1230) and `test_morph_host_simulation.cpp` (:183-187, :255-257, :267-268). Rewrite each to send the selected pad's per-pad ID (what the controller now emits) with the same assertion, and replace the Phase 8C case by the "processor ignores a proxy ID" case above. List each rewritten case in the commit message. The gate finds any other.
   - Update the processor's proxy comments and the controller comment at :861 to say the proxies are UI-only binding targets.
3. **Build and gate.** Standard steps; add `Membrum` to the build targets. UI: `editor.uidesc` does not change. Run `node tools/lint-plugin-editor-baseline.js`, `node tools/uidesc-inventory.js plugins/membrum/resources/editor.uidesc --check` (with the allow file once T3.2 has landed) and `node tools/gen-screenshots.js membrum`; the screenshots must come out unchanged. pluginval at strictness 5 must pass with the hidden, non-automatable proxies. Stop before the commit for the ruling in the fix-plan head's table, with the list of proxy IDs whose flags changed.
4. **Acceptance.** The five new cases green, the rewritten cases green, `test_pad_parameters.cpp` (`[pad_params][proxy]`) green. A recorded proxy gesture lands on `padParamId(selectedPad, offset)` (today on the proxy ID); 0 of the proxy rows and `kSelectedPadId` carry `kCanAutomate` (today all of them do); a per-pad host write to the selected pad shows on its proxy (today it does not).

### T1.21 — The macro knobs and the Coupling Amount knob edit the selected pad

- **Findings:** MB-071 (high, CONFIRMED).
- **Flags:** UI.
- **Prerequisites:** T1.20.
- **Estimate:** 150 min.

The five macro knobs are bound to tags 1037-1041 (`plugins/membrum/resources/editor.uidesc:39-43`, used by the `ArcKnob`s at :137-145 in `SelectedPadSimple`) and the Coupling Amount knob to tag 1036 (:93, used at :241). With `kPadBaseId` 1000 and stride 128 those are pad 0's per-pad IDs, offsets 36-41 (`kPadCouplingAmount` = 36, `kPadMacroTightness` .. `kPadMacroComplexity` = 37-41 in `plugins/membrum/src/dsp/pad_config.h`). `kProxyMappings` (`plugins/membrum/src/controller/controller.cpp:84-155`) has no row for them, nothing retags them, and the processor applies them to pad 0 (`processor.cpp:208-229`). So with any other pad selected these knobs edit and show pad 1 (index 0). The uidesc comment at :92 ("pad 0 offset 36 = selected-pad proxy") and `controller_param_format.cpp:428-429` ("Selected-pad proxies are the pad-0 slot") describe a remap that does not exist, and `plugins/membrum/tests/unit/controller/test_param_reachability_in_editor.cpp` (header :11-13, rule at :303-310) counts pad-0 anchored tags as reachable for every pad, which encodes the bug.

1. **Failing test.**
   - In `plugins/membrum/tests/unit/vst/test_proxy_automation.cpp` (created by T1.20; reuse its recording handler header): `TEST_CASE("Selected-pad proxy: macro and Coupling Amount knobs edit the selected pad", "[pad_params][proxy][macro]")`. Select pad 5 on the controller; for each of the six knobs, perform the `VST3Editor` gesture on the knob's tag as the editor binds it, read from `editor.uidesc` by control-tag name (`MacroTightness`, `MacroBrightness`, `MacroBodySize`, `MacroPunch`, `MacroComplexity`, `CouplingAmount`; parse the `<control-tag>` block, so the test follows the uidesc). Assert each recorded edit is on `padParamId(5, offset)`. Replay into a processor with selected pad 5: pad 5's `macroTightness` (and the other five fields) becomes the gesture value, and pad 0's macro fields, `couplingAmount`, `material` and `decay` are unchanged. Today the gesture is recorded on 1037 and lands on pad 0.
   - Same file: `TEST_CASE("Selected-pad proxy: macro knobs show the selected pad's values", "[pad_params][proxy][macro]")`. Write pad 2's per-pad Punch to 0.2 with `setParamNormalized(padParamId(2, kPadMacroPunch), 0.2)`, select pad 2 and assert the value read through the `MacroPunch` control tag is 0.2; select pad 0 and assert it reads pad 0's 0.5.
   - `test_param_reachability_in_editor.cpp`, `TEST_CASE("editor.uidesc reaches every registered parameter (SC-002)", ...)`: delete the pad-0 anchored reachability rule (:303-310 and the header bullet :11-13), and add the assertion that no `<control-tag>` value lies in the per-pad range `[kPadBaseId, kPadBaseId + kNumPads × kPadParamStride)`. Today 1036-1041 do.
2. **Fix.**
   - `plugins/membrum/src/plugin_ids.h`: add six global IDs below 1000 at the next free values after `kWireCouplingId` (326). The proposed values are `kCouplingAmountId = 330`, `kMacroTightnessId = 331`, `kMacroBrightnessId = 332`, `kMacroBodySizeId = 333`, `kMacroPunchId = 334`, `kMacroComplexityId = 335`; confirm with Grep that 330-335 and the six names are unused, and add a `static_assert(... < kPadBaseId)` like :265-271. No existing ID changes.
   - Move `ProxyMapping` / `kProxyMappings` out of `controller.cpp` (:78-155) into a new header `plugins/membrum/src/controller/proxy_mappings.h` (namespace `Membrum`, `inline constexpr`; run the ODR grep for `ProxyMapping` first), add the six rows (`kCouplingAmountId` -> `kPadCouplingAmount`, `kMacroTightnessId` -> `kPadMacroTightness`, ...), and include it from `controller.cpp` and from `test_param_reachability_in_editor.cpp`, whose private copies `globalProxyMap()` (:155ff) and `proxiedOffsets()` (:235ff) are replaced by it. T1.20's `beginEdit` / `performEdit` / `endEdit` translation, the per-pad-to-proxy mirror and `syncGlobalProxyFromPad` then cover the new rows with no new code.
   - `Controller::initialize`: register the six as UI-only proxies the way T1.20 registers the others (not `kCanAutomate`, `kIsHidden`), each with the title and default of its per-pad twin (`kPadParamSpecs`, `controller.cpp:206-211` for the macros; the Coupling Amount row in the same table).
   - `plugins/membrum/src/controller/controller_param_format.cpp`, `getParamStringByValue`: format the six new IDs as their per-pad offsets are formatted (route a proxy tag through `formatByPadOffset(row.padOffset, ...)` using the table), and correct the comment at :428-429.
   - `plugins/membrum/resources/editor.uidesc`: change the six control-tag values (1036 -> 330, 1037 -> 331, ..., 1041 -> 335) and the comment at :92. That is six elements, so do it with an XSLT stylesheet `specs/membrum-audit/xslt/t1-21-macro-proxy-tags.xsl` (identity transform plus the six value rewrites), applied with `npx xslt3 -xsl:specs/membrum-audit/xslt/t1-21-macro-proxy-tags.xsl -s:plugins/membrum/resources/editor.uidesc -o:<output file>`, then replace the uidesc with the output. The knobs' names, positions and `control-tag` names stay the same.
   - The processor needs nothing: the controller now emits the per-pad IDs 1036-1041 of the selected pad, which `processParameterChanges` already routes (:208-229).
   - Before: the macro and Coupling Amount knobs edit and show pad 1 whatever is selected. After: they edit and show the selected pad, like the knobs beside them.
3. **Build and gate.** Standard steps; add `Membrum` to the build targets. UI: `node tools/lint-plugin-editor-baseline.js`, `node tools/uidesc-inventory.js plugins/membrum/resources/editor.uidesc --check` (with T3.2's allow file once it exists), and `node tools/gen-screenshots.js membrum`; the screenshots must not change apart from knob positions that now show the selected pad's macro values. pluginval at strictness 5.
4. **Acceptance.** The two new cases and the corrected reachability case green; `grep -n 'tag="10[0-9][0-9]"' plugins/membrum/resources/editor.uidesc` finds nothing (today it finds 1036-1041). Spec 141 FR-020 and US1 scenarios 3-4 hold: with pad 2 selected, moving Tightness changes pad 2 and leaves pad 1's macros where they were.

### T1.22 — Macros are offsets on a stored base value: lossless round trip, visible to the controller, kept by saves; saved and factory macro positions are kept and no kit changes

- **Findings:** MB-073 (high, CONFIRMED), MB-081 (high, CONFIRMED), MB-173 (high, CONFIRMED), MB-084 (low, CONFIRMED).
- **Flags:** STATE-COMPAT, PRESET-IMPACT.
- **Prerequisites:** T1.3 (controller-side save paths), T1.21 (macro knobs edit the selected pad).
- **Estimate:** 360 min.

`MacroMapper::apply` (`plugins/membrum/src/processor/macro_mapper.cpp:63-96`) runs only in the processor (`processor.cpp:217-229`) and bakes each macro move into the pad's fields incrementally: every applier adds `newDelta - oldDelta` and `clamp01`s the result (:127-219), while the cache stores the unclamped macro (:91-95). Consequences: a sweep that hits a clamp does not come back (Brightness 0.5 -> 1 -> 0.5 leaves `tsFilterCutoff` at 0.8, MB-173); the controller never learns the derived fields, so the next direct edit overwrites them absolutely (`voice_pool.cpp` `setPadConfigField`, e.g. `case kPadMaterial`) and a controller-side kit save (`kitPresetStateProvider`, `controller/controller_state.cpp:105-118`, through `buildPadSnapshotFromParams`, `controller_state_codec.cpp:117-181`) stores pre-macro fields next to non-neutral macros, which `applyKitSnapshot` then only syncs into the cache (`processor.cpp:990-1002`; MB-073). Punch below 0.5 on a pad without a pitch envelope (`tsPitchEnvStart` = `End` = `Time` = 0, the `PadConfig` default) turns the envelope on at 20 Hz (`macro_mapper.cpp:185-192`, decoded at `pad_config_apply.h:101-107`), retuning the whole body to a 20 Hz fundamental (MB-081). The generator writes non-neutral macros that never reach the sound, because a load only syncs the cache (`tools/membrum_preset_generator.cpp:1696-1698`, :1735-1737, :1842-1845 and about 120 more assignments; MB-084).

**Ruling (user, 2026-11-24): keep macro positions.** A version-5 state keeps its saved macro values, and its stored fields (which are what it plays today: baked by the processor, or never touched by the macros in a factory kit) are converted to base values with `base = clamp01(field - delta(macro))`, so `applyMacroOffsets(base)` plays the stored field again. Where a base would fall outside [0, 1] the clamp loses the difference and that pad sounds slightly different: count those pads and report them. The factory kits keep their macro positions and their sound the same way: the generator keeps its macro assignments and stores base values (follow-up ruling, 2026-11-24). This replaces the plan's earlier default (macros reset to neutral). No stop before the commit.

1. **Failing test.** First, before any code change, capture the version-5 fixture (STATE-COMPAT): add a temporary hidden case that drives a `Processor` (48 kHz, block 256) through `processParameterChanges` with pad 0 Tightness 0.8 and pad 2 Brightness 0.2 (per-pad IDs), writes `getState` to `plugins/membrum/tests/golden/state_v5_macros.bin`, renders notes 36 and 38 (1 s each, velocity 0.8) and prints their `Krate::DSP::TestUtils::fingerprintRender` values (`tests/test_helpers/render_fingerprint.h`). Run it once on the unfixed code, keep the `.bin` and paste the fingerprint numbers into the test below as constants, then delete the temporary case. Then, new file `plugins/membrum/tests/unit/processor/test_macro_offsets.cpp` (added to `membrum_tests`), tags `[macro_mapper][macro_offsets]` plus the extra tag named per case, through a real `Processor` with per-pad parameter IDs (`Krate::Test::ParameterChanges`):
   - `TEST_CASE("Macro offsets: a macro sweep back to neutral restores the pad", ...)` (MB-173, MB-081): for each macro, set its target fields on pad 0 to 0.0, then 0.1, 0.9 and 1.0 (four runs), sweep the macro 0.5 -> 1 -> 0 -> 0.5 in separate blocks, and assert every target field reads exactly its start value and a rendered hit matches the hit rendered before the sweep (`compareFingerprints`). Today Brightness reads `tsFilterCutoff` 1.0 / 1.0 / 0.8 (audit probe `macro:Brightness round trip`).
   - `TEST_CASE("Macro offsets: Punch below neutral leaves an un-enveloped pad at its pitch", "[macro_mapper][macro_offsets][pitch]")` (MB-081): default kit pad 2 (note 38, no pitch envelope), noise and click mix 0 on that pad, Punch 0.45, render 1 s at velocity 0.8; estimate the peak frequency over a power-of-two frame from 30 ms with `Krate::DSP::TestUtils::estimatePeakFrequencyHz` (`tests/test_helpers/low_frequency_metrics.h:245`) and compare with the same render at Punch 0.5: within one semitone (ratio within 2^(±1/12)). Today the body is retuned to a 20 Hz fundamental.
   - `TEST_CASE("Macro offsets: a direct edit after a macro move keeps the macro's contribution", ...)` (MB-073 a): Tightness 1.0 then Material + 0.01, against Material + 0.01 then Tightness 1.0, on two processors; the two renders match (`compareFingerprints`). Today the first order loses the +0.15 material delta.
   - `TEST_CASE("Macro offsets: a controller-side kit save reproduces the processor's sound", "[macro_mapper][macro_offsets][preset]")` (MB-073 b): a `Controller` and a `Processor` kept in sync by replaying the controller's recorded edits (the T1.20 handler); move Tightness to 0.8 on pad 0 through the `MacroTightness` gesture; save with `kitPresetStateProvider`; load the blob into a fresh processor with `setState`; assert both processors' `getState` bytes are equal and notes 36 and 38 render the same. Extend T1.3's save test helpers rather than writing new ones. Today the loaded processor plays the pre-macro fields.
   - `TEST_CASE("Macro offsets: a version-5 state loads with its macro positions and its sound kept", "[macro_mapper][macro_offsets][state]")` (STATE-COMPAT): `setState` with `state_v5_macros.bin` succeeds; pad 0 Tightness reads 0.8 and pad 2 Brightness reads 0.2 (every other macro 0.5); each stored sound field reads back as `clamp01(field - delta)` of the fixture's field; notes 36 and 38 match the captured fingerprints (the fixture's macros keep every base inside [0, 1]: check that when choosing it); `getState` writes version 6, and `setState(getState())` is byte-idempotent. Also: a version-5 pad-preset blob loads unchanged; blobs claiming versions 3, 4 and 7 are rejected and leave the processor state untouched. Today the version-6 and macro-position assertions fail.
   - In `plugins/membrum/tests/unit/preset/test_factory_kit_presets.cpp`: `TEST_CASE("Factory kit presets: stored macros reach the sound and every kit plays as before", "[membrum][preset][factory][kit_preset]")` (MB-084). Before any code change, add a temporary hidden case that writes every pad's sound fields and macros of all 20 kits, as loaded today, to `plugins/membrum/tests/golden/factory_kit_fields_v5.bin`; run it once, keep the `.bin`, delete the case. The new case asserts, for every pad of all 20 kits: the stored macros equal the fixture's macros (281 of the 3200 values are non-neutral: per kit 0, 46, 23, 27, 25, 3, 11, 10, 10, 15, 5, 6, 13, 9, 5, 15, 14, 25, 16, 3), and `applyMacroOffsets(stored pad)` equals the fixture's fields within 1e-6. Red today: `applyMacroOffsets` does not exist (the compile error is the expected red), and the stored macros do not reach the sound.
2. **Fix.**
   - New header `plugins/membrum/src/dsp/macro_offsets.h` (ODR grep for its names first): move `MacroCurves` (`processor/macro_mapper.h:101-122`) and the `linDelta` / `expDelta` curves (`macro_mapper.cpp:22-33`) there, and add `[[nodiscard]] inline PadConfig applyMacroOffsets(const PadConfig& base) noexcept`, which returns a copy whose target fields are `clamp01(base + sum of the macro deltas)` with today's curves and spans. A macro at exactly 0.5 contributes nothing by a branch, not by arithmetic that cancels (macOS fast-math rule). Punch's pitch-envelope deltas apply only when `base.tsPitchEnvTime > 0`, and scale an existing envelope instead of switching it: the effective time stays above 0 whenever the base time is above 0 (for example a multiplicative factor on the time instead of the additive span), so Punch never turns a pitch envelope on or off; its `noiseBurstDuration` delta stays. The stored `PadConfig` fields are the base values and are never modified by a macro. Add the inverse, `[[nodiscard]] inline PadConfig baseFromEffective(const PadConfig& effective, int* clampedFields = nullptr) noexcept`: for each target field `base = clamp01(effective - delta)` (for Punch's multiplicative time factor, divide), counting the fields where the clamp acted. A unit case checks `applyMacroOffsets(baseFromEffective(x)) == x` for random in-range configs whose bases stay inside [0, 1].
   - `plugins/membrum/src/voice_pool/voice_pool.cpp`, `VoicePool::applyPadConfigToSlot` (:769-786): pass `applyMacroOffsets(padConfigs_[pad])` (a stack copy) to `applyPadConfigToVoice`. `dsp/pad_config_apply.h` stays as it is (it is shared with `tools/membrum-fit`).
   - `plugins/membrum/src/processor/processor.cpp`: the macro branch (:217-229) only stores the field (and still recomputes the coupling matrix for Complexity). `recomputeCouplingMatrix` (:131-143) reads the effective `couplingAmount`, and every `classifyPad` call (:115-116, :185-205, :986-987, :1011) classifies the effective config. Delete the `MacroMapper` uses (:120 `prepare`, :223 `apply`, :990-1002 cache sync and its comment, :1014-1017), the member and accessor in `processor.h` (:85, :165), and `processor/macro_mapper.{h,cpp}` with their CMake entries once nothing uses them. If `buildRegisteredDefaultsTable` / `RegisteredDefaultsTable` are then unused, delete them too and say in the commit message that T6.4's MB-112 part is done.
   - Tests that pin the incremental mapper encode the defect: `plugins/membrum/tests/unit/processor/test_macro_mapper.cpp`, `test_macro_preset_load_regression.cpp`, `test_macro_automation_extreme.cpp` and the FR-082 cases of `test_state_v6_migration.cpp`. Rewrite each case to the offset model (stored fields untouched, effective value = base + delta, zero allocations on the audio thread kept), never by deleting an assertion without a replacement, and list each in the commit message.
   - State (STATE-COMPAT, the plan's one version change): run `git ls-remote --tags origin "refs/tags/membrum/*"`; if no released build writes 6, set `kCurrentStateVersion` (`plugins/membrum/src/plugin_ids.h:33`, with the `static_assert` at :229), `kBlobVersion` and `kPadBlobVersion` (`plugins/membrum/src/state/state_codec.h:108-109`) and `kVersion` in `tools/membrum_preset_generator.cpp:41` to 6. The layout does not change; the meaning does (pad sound fields are base values). `readKitBlob` (`state_codec.cpp:421`, check at :431) accepts 5 and 6, and for 5 keeps every pad's `macros` and replaces its sound fields with `baseFromEffective(fields)` (the stored fields are what a version-5 state plays; ruling 2026-11-24). Log nothing on the audio thread; the clamp count is reported by the tests and the generator, not at runtime. `readPadPresetBlob` (:549, check at :559) accepts 5 and 6 with no transform (pad presets carry no macros). Writers write 6. Because the migration sits in the codec, `Processor::setState`, the kit and pad load providers and `Controller::setComponentState` see the same values. Update the version comments in `state_codec.h` (:108-114, :132, :148). T2.8 adds the read paths for versions 1 and 2 on top of this.
   - Controller: no derived computation is needed. The controller's per-pad parameters already hold exactly what is stored (base fields and macro values), so `getState` and the controller-side saves describe the same state; the field knobs show the base and the macro knobs the offset. Keep `controller_state_codec.cpp` as T1.3 left it.
   - Generator (`tools/membrum_preset_generator.cpp`): keep the macro assignments. Where a pad's fields are final, before the kit is written, replace its sound fields with `baseFromEffective(fields)` (include `plugins/membrum/src/dsp/macro_offsets.h`), so every factory kit stores the designer's macro positions and base values that play exactly what the kit plays today. The generator prints, per kit, the number of pads whose clamp acted; any nonzero count stops the task for the user with the list (the fields the designer wrote then cannot be reproduced at that macro position). Set `kVersion` to 6.
   - Before: macros mutate the stored fields, lossy at the clamps, invisible to the controller, dropped by controller-side saves, ignored on load, and Punch can detune a pad to 20 Hz. After: fields are stored once as base values, macros are offsets applied at note-on, every save path reproduces the sound, and saved and factory macro positions are kept.
3. **Build and gate.** Standard steps; add `Membrum` and `generate_membrum_presets` to the build targets. STATE-COMPAT: regenerate the 20 kits with the generator, keep 5 kits in each of the four categories, and confirm `getState(setState(kit))` is idempotent for all 20. `plugins/membrum/tests/golden/phase1_default.bin` must not move (the default kit's macros are neutral); `phase2_state_v2.bin` is untouched. PRESET-IMPACT: this is a re-encode, not a retune. Run the kit-quality probe before and after: every pad's peak, rms500, decay, centroid and limiter margin in `kit-pads.csv` and every pair in `kit-pairs.csv` must be unchanged; any difference is a defect of this task. The ruling is recorded; no stop before the commit unless the generator reports a clamped pad.
4. **Acceptance.** All six new cases, the `baseFromEffective` round-trip case and the rewritten macro cases green. Brightness round trip reads 1.0 / 1.0 / 1.0 (baseline 1.0 / 1.0 / 0.8); Punch 0.45 on the default snare keeps its pitch within one semitone (baseline: retuned to a 20 Hz fundamental); the factory kits keep their 281 non-neutral macro values and the generator reports 0 clamped pads; a controller-side kit save reloads byte-identical to `getState`; the version-5 fixture keeps its macro positions and plays its captured fingerprints; the kit-quality probe numbers are identical before and after.

### T1.23 — Body damping B1/B3 has one representable encoding with no hidden sentinel, and Decay stays live under an override

**Findings:** MB-074 (high, CONFIRMED), MB-029 (high, CONFIRMED), MB-109 (high, PLAUSIBLE), MB-216 (low, CONFIRMED).

**Flags:** STATE-COMPAT, PRESET-IMPACT, UI, VERIFY-FIRST (MB-109 is PLAUSIBLE: a skeptic read the explicit 0.40 / 0.40 factory default as a documented design, citing `MANUAL.md:237` and `PRESET-RETUNE-PLAN.md:99-101`).

**Prerequisites:** T1.3, T1.22.

**Estimate:** 420 minutes.

**Ruling (user, 2026-11-24): Option R, the relative override, no toggle.** Do not stop after step 1; implement Option R below (Option M stays in the text only as the rejected alternative). Report the step-1 numbers in the commit message.

**What is wrong today (anchors read at `4a14376d`):**

- `PadConfig::bodyDampingB1` / `bodyDampingB3` default to the hidden sentinel `-1.0f` (`plugins/membrum/src/dsp/pad_config.h:282-283`; the same default in `dsp/voice_common_params.h:42-43` and `DrumVoice::bodyDampingB1_` / `bodyDampingB3_`, `dsp/drum_voice.h:1742-1743`).
- `dampingLawFromParams` (`dsp/bodies/membrane_mapper.h:64-80`) REPLACES the derived law (`b1 = 1/decayTime`, `b3 = (1 - brightness) * 4e-5`, `dsp/include/krate/dsp/processors/modal_resonator_bank.h:123,133-139`) with an absolute value whenever the field is `>= 0`: `b1 = 0.2 + n * 49.8`, `b3 = n * 1e-3`. Every modal mapper routes damping only through it (membrane `:177`, `plate_mapper.h:95`, `shell_mapper.h:93`, `bell_mapper.h:90`, `noise_body_mapper.h:149`), so with an override set, Decay, the damping half of Material and the Tightness / Body Size macro decay deltas do nothing to the modal ring.
- The controller registers both per-pad parameters (`controller/controller.cpp:227-228`) and both proxies `kBodyDampingB1Id` / `kBodyDampingB3Id` (`controller.cpp:620-625`) at 0.5, which the comment at `controller.cpp:221-226` wrongly calls "neutral". The controller replay skips sentinel slots (`controller/controller_state_codec.cpp:252-271` kit, `:313-316` pad preset), so after a load the knob shows 0.5 for a value the DSP never uses; the first touch (`processor.cpp:461-465` -> `voice_pool.cpp:678-679`) writes an absolute override.
- The default is written four ways: `-1` (PadConfig, voice), `0.5` (controller), `0.40` (`tools/membrum_preset_generator.cpp:131-136`, every factory pad) and `0.4` (`drum_voice.h:1472-1473`, the shell fallback inside `configureSecondaryBank`).
- The state codec keeps `-1` verbatim (`state/state_codec.cpp:210-222` in `applyPadSnapshot`, `:333-338` in `applyPadPresetSnapshot`).
- Stale comments (MB-216): `pad_config.h:274-279` (says `B3 = 1 -> 8e-5 s*rad^-2`; code is `1e-3 s`, Hz convention), `controller.cpp:221-226` (0.5 "neutral", "4e-5 ... equivalent to legacy"), `default_kit.h:241` (says T60 ~ 4.6 s; `b1 = 1.196` gives 5.78 s, 5.57 s at 752 Hz), `drum_voice.h:448` (says 0.993; `kNoteOffDampScale = 0.997f` at `:64`).
- The editor shows both knobs (`resources/editor.uidesc:244,246`, tags 300 / 301), formatted as a plain percent (`controller/controller_param_format.cpp:335-336`, `:413-414`).

Before starting, run `git log --oneline --grep "T1.3"` and `--grep "T1.22"` and read what those commits did to slots 42 / 43, the sentinel skip in `controller_state_codec.cpp` and the state version. T1.23 replaces any sentinel special case T1.3 added; it keeps the version T1.22 set (6, see STATE-COMPAT).

**1. Failing test**

New file `plugins/membrum/tests/unit/processor/test_body_damping_encoding.cpp` (add it to the `membrum_tests` source list). The three cases below hold for either encoding, so they are written before the ruling. Drive a real `Membrum::Processor` at 48 kHz, block 256 (copy the setup pattern of `unit/processor/test_kit_preset_load_render.cpp:78` `makeSetup`), send per-pad parameter values through `tests/test_helpers/vst_param_changes.h` (`ParameterChanges` / `ParamValueQueue`) and notes through `tests/test_helpers/vst_event_list.h` (`Krate::Test::EventList`). Measure decay60 (time from the hit peak to -60 dB below it, on the main-bus envelope in 10 ms windows) with a small file-local function; `tests/test_helpers/signal_metrics.h` and `audio_features.h` hold the envelope helpers, so read them first and use what is there. The noise layer may be seeded per voice, so compare decay60 and RMS, not raw samples.

- `TEST_CASE("Body damping: writing the controller's displayed B1/B3 value back to the processor leaves every pad's decay unchanged", "[body_damping][param_plumbing]")`. Default kit: `Controller::setComponentState` with the processor's `getState`, then for every enabled pad and for `kPadBodyDampingB1` and `kPadBodyDampingB3` send `controller.getParamNormalized(padParamId(pad, offset))` to the processor's per-pad ID, hit the pad at velocity 100 and render 2 s. Asserts decay60 within 5 % and RMS(1 s) within 0.1 dB of an untouched render of the same pad. Fails today: the audit probe (`specs/membrum-audit/probes/probe-results.csv`, rows `save-path:kit:default-kit:*`) measured kick 36 0.97 s -> 0.19 s, tom 41 1.54 s -> 0.23 s, closed hat 42 0.32 s -> 0.02 s when 0.5 is written over the sentinel.
- `TEST_CASE("Body damping: a 0.01 nudge of Body Damping b1 or b3 from the displayed value changes decay60 by less than 10 %", "[body_damping][param_plumbing]")`. Default-kit pads 36 and 41 (sentinel B1) and 38 (explicit 0.60 / 0.40, `default_kit.h:130-131`), each coefficient separately: displayed value + 0.01 (or - 0.01 at the top of the range). Asserts the decay60 ratio is in [0.90, 1.10]. Fails today on 36 and 41 by about 5x (MB-074: the 1 % nudge moves `b1` from about 3.3 to 25.6 s^-1).
- `TEST_CASE("Body damping: per-pad Decay changes the modal ring of factory-kit pads", "[body_damping][param_plumbing][preset]")`. Default-kit snare 38 plus one Membrane, one Plate or Shell and one Bell pad from one kit in each of the four categories (choose them from `specs/membrum-audit/probes/kit-pads.csv` and name them in the test). Load each kit into the processor from `plugins/membrum/resources/presets/Kit Presets/<Category>/<Kit>.vstpreset` with `Steinberg::Vst::PresetFile::restoreComponentState` (the same call `tests/test_helpers/editor_screenshot.h:182-192` makes for a controller, here on the `IComponent`). Render the pad at per-pad Decay 0.2 and 0.8. Asserts decay60(0.8) / decay60(0.2) >= 2. Fails today: `param-audibility.csv` row `offset=4:decay`, note 38: decay 0.16 s at all five steps, `bitIdentical = 1`.
- A second case with the same body, `TEST_CASE("Body damping: per-pad Decay changes the modal ring of every factory-kit modal pad", "[body_damping][param_plumbing][preset][long]")`, sweeps every enabled non-String pad of all 20 kits (the audit counted 459 enabled pads, 0 sentinels, 429 non-String pads with both overrides set; MB-109). It renders far more than 15 s of toolchain-independent audio, hence `[long]`.

Run each case by name (plan head step 1) and confirm it fails for the reason given. VERIFY-FIRST: the third and fourth cases are MB-109's verification. If they pass on current code, MB-109 is refuted: record it, and continue with MB-074 / MB-029 / MB-216.

**Ruling to ask for** (show the step-1 numbers with it). Both options remove the sentinel, give the knob a value that is always the one the DSP uses, and re-encode every stored value so every factory kit and every version-5 state keeps its sound:

- **Option R (relative override, no toggle).** The stored value scales the derived law instead of replacing it. `b1 = b1_derived * 2^(kB1 * (n - 0.5))`, `b3 = max(0, b3_derived + (n - 0.5) * kB3)`. `n = 0.5` is the derived law exactly; produce it by a branch (`n == 0.5f` returns the derived value), not by arithmetic (MACOS rule). Decay, Material and the macros stay live on every pad, so it meets the whole task title and closes MB-029 / MB-109 for the factory kits. Choose `kB1` / `kB3` so that (a) a 0.01 step from 0.5 changes decay60 by under 10 % (with `kB1 = 12`, ±6 octaves, a step is 2^0.12 = 1.087) and (b) every explicit value in the 20 factory kits and the default kit re-encodes without clipping. Check (b) by computing the re-encoded `n` for every pad (step 2); if no constant satisfies both, stop and report the clipped pads.
- **Option M (explicit Auto / Manual).** Two new per-pad float-as-bool parameters (B1 Auto, B3 Auto) and their proxies; Auto uses the derived law, Manual keeps today's absolute override. Decay stays live only in Auto. In Manual the editor must show that Decay does not move the ring (dim the Decay knob and say why in its tooltip). The factory kits re-encode as Manual, so MB-029 and MB-109 stay open for them until a phase-5 retune switches pads to Auto; record that in the completion checklist.

**2. Fix** (after the ruling; the common part first)

Common to both options:

- One definition. Put the encoding (constants and the encode / decode functions) next to `dampingLawFromParams` in `plugins/membrum/src/dsp/bodies/membrane_mapper.h`, with a doc comment that states the ranges in the units the code uses (b1 in s^-1, b3 in s with f in Hz, per `modal_resonator_bank.h:113-118`). `pad_config.h`, `controller.cpp`, `default_kit.h` and the generator refer to it instead of restating ranges. Add one helper, for example `encodeBodyDampingFromAbsolute(const VoiceCommonParams& params, BodyModelType body, float b1Abs, float b3Abs)`, that runs the pad's own body mapper with both fields at "derived" to get that pad's derived law (each mapper computes its own `decayTime` and `brightness`) and returns the stored values that reproduce `b1Abs` / `b3Abs`. Search for the helper's name first (ODR rule).
- No sentinel left: PadConfig, `VoiceCommonParams`, `DrumVoice` defaults, the controller's per-pad and proxy registration defaults and the generator's `Pad` defaults all hold the encoding's "derived" value (option R: 0.5; option M: Auto on). Delete the sentinel skip in `controller_state_codec.cpp:252-271` and `:309-316`; every slot is then written like any other.
- `DrumVoice::configureSecondaryBank` (`drum_voice.h:1470-1479`): replace the `(bodyDampingB1_ >= 0) ? bodyDampingB1_ : 0.4f` re-denormalisation with the body's effective law `b1` (the `MapperResult::damping` the voice applied at note-on). This changes the shell tail of a pad that has the secondary shell enabled and used the old sentinel (today it saw `b1 = 20.1`). The probe table of PRESET-IMPACT shows whether any default-kit pad moves; factory kits have no sentinel pads, so they cannot. Include that in the ruling message.
- `default_kit.h` keeps its authored absolute values (`:130-131`, `:174`, `:249-250`, `:323-324`) and converts them through the helper when the kit is built; correct `:241` to "T60 ~ 5.8 s (5.6 s at the 752 Hz fundamental)". Correct `drum_voice.h:448` to 0.997, `pad_config.h:274-281` and `controller.cpp:221-226` to the new encoding.
- `controller_param_format.cpp:335-336` and `:413-414`: display what the DSP uses (option R: "Auto" at exactly 0.5, otherwise the b1 factor as "x0.25 .. x4" style and the b3 offset; option M: the absolute b1 in s^-1 and b3 in s, plus the Auto state).
- STATE-COMPAT. Run `git ls-remote --tags origin "refs/tags/membrum/*"` first: if a release wrote version 6, move to 7 and keep a read path for 6. Otherwise keep 6. Version-5 read path (`readKitBlob` / `readPadPresetBlob` through `applyPadSnapshot` / `applyPadPresetSnapshot`, and the controller side through `controller_state_codec.cpp`): slot value `< 0` maps to "derived"; an explicit value maps through the helper. Because no release wrote 6, a version-6 blob written between T1.22 and this commit is test-only; say so in the commit message.
  - Option M also appends two sound slots (B1 Auto, B3 Auto) after the last slot, takes the next free per-pad offsets after the last `kPadActiveParamCountV*` in `pad_config.h:176` (offset 66 is taken if T1.3 or T1.22 added a field: read the enum) and two free global IDs below 1000 (`plugin_ids.h:145-189` shows 300-326 in use; check what T1.3 / T1.20 / T1.22 added), registered in `Controller::initialize`, the proxy table `controller.cpp:132-133`, `Processor::processParameterChanges` and `VoicePool::setPadConfigField`. Never renumber existing IDs.
- PRESET-IMPACT. The generator (`tools/membrum_preset_generator.cpp:131-136` and every per-pad damping value) keeps authoring absolute values and converts them through the same helper: add `plugins/membrum/src` and `dsp/include` to the `membrum_preset_generator` target's include directories (find its `add_executable` with Grep in the root `CMakeLists.txt`; the run target is at `:728`) so it calls the plugin's mapper instead of copying formulas. Update `kVersion` (`:41`) as STATE-COMPAT says. Regenerate the kits. This is a re-encode: the kit-quality probe must show the same peak, rms500, decay, centroid and limiter margin for every pad of all 20 kits (before T5.6 no kit may change sound).
- UI. Option R changes no uidesc. Option M adds two toggles next to `editor.uidesc:244,246` (two controls: a hand edit is allowed; more than three goes through XSLT) and dims Decay on Manual pads through the controller's existing view-refresh helpers (`controller_view_sync.cpp:117-124`).

Then add the encoding-specific tests to the same file: `TEST_CASE("Body damping: the encoding's derived value reproduces dampingLawFromLegacy exactly", "[body_damping]")` over the default PadConfig and three default-kit pads, and `TEST_CASE("Body damping: dampingLawFromParams pins the documented ranges", "[body_damping]")` with the expected numbers typed from the doc comment, not computed by the code under test (MB-216). Extend the STATE-COMPAT version-5 test: load a version-5 blob captured from the code before T1.22 (reuse the fixture T1.22 committed; `git show --stat` its commit to find it) holding sentinel pads and explicit pads, and assert the migrated values render each pad's decay60 within 2 % of the version-5 render.

**3. Build and gate**

- Add `Membrum` to the build (plugin sources and, for option M, `resources/editor.uidesc`).
- Build `membrum_preset_generator`, then regenerate with the `generate_membrum_presets` target (PRESET-IMPACT command), and confirm 20 kits, 5 per category, directory and XML metadata matching; confirm `getState(setState(kit))` is idempotent for all 20.
- Run the kit-quality probe before (on the commit before this task) and after, as the plan head's "Measuring with the audit probes" describes, and put the per-pad table in the commit message.
- `tests/golden/phase1_default.bin` renders a default PadConfig voice, whose damping was the sentinel; option R keeps it bit-identical (0.5 is the derived law by a branch). If it moves, the derived path is not exact: fix that, do not re-baseline.
- UI flag: `node tools/lint-plugin-editor-baseline.js`, `node tools/uidesc-inventory.js plugins/membrum/resources/editor.uidesc --check`, `node tools/gen-screenshots.js membrum`.

**4. Acceptance**

- The four step-1 cases are green; the `[long]` case is green over all 429 pads. Baselines: decay60 after writing the displayed value back, kick 36 0.97 s -> 0.19 s today, must stay 0.97 s within 5 %; snare 38 Decay sweep 0.16 s flat today, ratio >= 2 after (option M: ratio >= 2 only on Auto pads, and the commit message lists the Manual pads that stay inert).
- No `-1` sentinel remains: Grep for `bodyDampingB[13] *= *-1`, `< 0.0) ? -1.0f`, `isSentinelSlot` and `: 0.4f` in `plugins/membrum/src` and the generator finds nothing.
- Every factory kit and the default kit: kit-quality probe numbers unchanged (peak, rms500, decay, centroid, limiter margin per pad), and `kit-pairs.csv` near-duplicate pairs unchanged. Any pad that moves stops the task with the table.
- The version-5 migration test is green; versions 3, 4 and newer-than-current are still rejected with the state untouched.

### T1.24 — PolyphonySlider drives and follows Max Polyphony

**Findings:** MB-096 (high, CONFIRMED).

**Flags:** UI.

**Prerequisites:** none.

**Estimate:** 60 minutes.

**What is wrong today:** `Controller::createCustomView` builds `new UI::PolyphonySliderView(viewRect, kMaxPolyphonyId)` and returns it without a listener (`plugins/membrum/src/controller/controller.cpp:1156-1164`; the comment at `:1158-1159` claims VST3Editor routes it). The view's constructor passes a null listener (`plugins/membrum/src/ui/polyphony_slider.h:28-29`). The uidesc declares it as `class="CView"` (`resources/editor.uidesc:391`), so VSTGUI applies only the CView creator's attributes and never sets a listener; `VST3Editor::verifyView` attaches its ParameterChangeListener only when `control->getListener() == this`. So `applyMouse` (`polyphony_slider.h:139-157`) calls `valueChanged()` into nothing: no `setParamNormalized`, no `performEdit`, and the bar never follows automation, a kit load or `setComponentState`. The `createCustomView` signature leaves the editor argument unnamed (`controller.cpp:1017-1021`).

**1. Failing test**

New file `plugins/membrum/tests/unit/controller/test_polyphony_slider_binding.cpp`, `TEST_CASE("PolyphonySlider: a drag edits Max Polyphony and the slider follows the parameter", "[voice_management][ui]")`.

- Initialise a `Membrum::Controller`, give it a file-local recording `IComponentHandler` modelled on `KbRecordingHandler` (`plugins/membrum/tests/unit/preset/test_kit_browser_load.cpp:47`) that records beginEdit / performEdit / endEdit triples.
- Open the editor headless the way `unit/controller/editor_screenshot_test.cpp:98-116` does (`Krate::TestSupport::ensureVstguiInitialized()` from `tests/test_helpers/editor_lifecycle_harness.h`, `new VSTGUI::VST3Editor(controller, "EditorDefault", MEMBRUM_RESOURCES_DIR "/editor.uidesc")`, `open`), and find the view with `Shots::findFirst<Membrum::UI::PolyphonySliderView>(root)` (`tests/test_helpers/editor_screenshot.h:138`).
- Assert `slider->getListener() != nullptr`. Simulate `onMouseDown` then `onMouseUp` at `x = viewSize.left + 0.75 * width` (the view computes `where.x - rect.left` against its own `getViewSize()`). Expected value: 13 voices, normalized `(13 - 4) / 12 = 0.75`. Assert `controller.getParamNormalized(kMaxPolyphonyId) == Approx(0.75).margin(1e-6)` and that the handler recorded `performEdit(kMaxPolyphonyId, 0.75)` between a begin and an end.
- Then `controller.setParamNormalized(kMaxPolyphonyId, 2.0 / 12.0)` and assert `slider->getValueNormalized() == Approx(2.0 / 12.0)`. The VST3Editor's ParameterChangeListener delivers this through the parameter's IDependent update: read `ParameterChangeListener` in `extern/vst3sdk/vstgui4/vstgui/plugin-bindings/vst3editor.cpp` before writing the assertion so the test observes the update the way the editor receives it.
- Close the editor and `terminate` the controller. Fails today on the listener assertion (null) and on both value assertions.

**2. Fix**

`controller.cpp`, `Controller::createCustomView`: name the editor parameter (`VSTGUI::VST3Editor* editor`) and in the `PolyphonySlider` branch call `view->setListener(editor);` before returning, so `VST3Editor::verifyView` attaches its ParameterChangeListener (the same binding every uidesc-created control gets). Correct the comment at `:1158-1159` to say the listener is set here. No change to `polyphony_slider.h` or the uidesc; no interaction is removed.

**3. Build and gate**

Add `Membrum` to the build. UI flag: `node tools/lint-plugin-editor-baseline.js`, `node tools/uidesc-inventory.js plugins/membrum/resources/editor.uidesc --check`, `node tools/gen-screenshots.js membrum` (the screenshot should not change; if it does, find out why before committing).

**4. Acceptance**

The new case is green; the existing `[voice_management]` cases (`unit/controller/test_voice_management_ui.cpp`, including `"Kit Column exposes Max Polyphony slider bound to kMaxPolyphonyId (FR-060)"`) stay green. In pluginval, the editor opens and closes cleanly. Before: a drag left `kMaxPolyphonyId` at its registered default and recorded no performEdit; after: 0.75 and one recorded triple.

### T1.25 — XYMorphPad: Morph End edits reach the controller's parameter, and the cursor follows pad selection, automation and preset loads

**Findings:** MB-097 (high, CONFIRMED), MB-098 (medium, CONFIRMED).

**Flags:** CROSS-PLUGIN IMPACT, UI.

**Prerequisites:** T1.20.

**Estimate:** 150 minutes.

**What is wrong today:**

- Shared view `plugins/shared/src/ui/xy_morph_pad.h`: the X axis goes through `CControl::valueChanged`, so VST3Editor calls `setParamNormalized` + `performEdit`. The Y axis (the secondary parameter) only calls `controller_->performEdit(secondaryParamId_, ...)`: double-click `:257-269`, mouse-down `:305-309`, drag `:374-377`, wheel `:424-428`, Escape `:445-448`. The file never calls `setParamNormalized`; the header comment at `:11` documents "direct performEdit()". `EditController::performEdit` only forwards to the component handler, so the controller's own value never changes.
- Membrum wires the pad with `xyPad->setController(this); xyPad->setSecondaryParamId(kMorphEndId);` (`plugins/membrum/src/controller/controller.cpp:1360-1363`). Its proxy forwarding from `kMorphEndId` to the selected pad's `kPadMorphEnd` lives only in `Controller::setParamNormalized` (`controller.cpp:861-871`), which this path never reaches. So the kit- and pad-preset save providers (`controller/controller_state.cpp:111` `buildSnapshot`, `:200-202` `buildPadSnapshotFromParams`) store the previous Morph End, and `syncGlobalProxyFromPad` (`controller.cpp:961-985`) copies the stale per-pad value back. Membrum's other custom editors pair the two calls (PitchEnvelopeDisplay: `performEdit(tag, v); setParamNormalized(tag, v);` at `controller.cpp:1290-1297`).
- `setMorphPosition` is the only external setter of the cursor (`xy_morph_pad.h:117-121`), and Membrum calls it only in `verifyView` (`controller.cpp:1365-1368`). The `setParamNormalized` refresh hooks (`controller.cpp:793-839`) and `syncGlobalProxyFromPad`'s refresh set (`:982-984`) never touch `xyMorphPad_`, and `applyViewRefresh` (`controller/controller_view_sync.cpp:117-124`) has no morph-pad entry. So after a pad switch, automation or a preset load the cursor shows old values, and the wheel (`xy_morph_pad.h:413-417`) and Escape (`:440-448`) write those stale values plus a delta into the newly selected pad.
- The only other consumer is Ruinae (`plugins/ruinae/src/controller/controller_verify_view.cpp:565-576`, secondary `kMixerTiltId`): it has the same Y-axis defect. Its `setParamNormalized` hook (`plugins/ruinae/src/controller/controller_view_sync.cpp:462-471`) already moves the cursor on `kMixerPositionId` / `kMixerTiltId`, which is the pattern Membrum lacks.

**1. Failing tests**

- Shared, in `plugins/shared/tests/test_xy_morph_pad.cpp`: `TEST_CASE("XYMorphPad: a secondary-axis edit sets the controller's parameter value as well as performEdit", "[xy_morph_pad][controller]")`. A minimal `EditControllerEx1` subclass with two `RangeParameter`s (X tag and secondary tag) and a recording component handler; place a pad, `setController`, `setSecondaryParamId`, then drive a `MouseDownEvent` / `MouseMoveEvent` / `MouseUpEvent`, a `MouseWheelEvent` and a double-click. After each, assert `controller.getParamNormalized(secondaryTag) == Approx(pad.getMorphY()).margin(1e-6)` and that a performEdit with that value was recorded. Fails today: the controller value stays at the registered default.
- Membrum, new file `plugins/membrum/tests/unit/controller/test_xy_morph_pad_sync.cpp`. Open the editor headless and switch to the Advanced template the way `unit/controller/editor_screenshot_test.cpp:80-116` does (`setParamNormalized(kUiModeId, 1)`, `Shots::findFirst<VSTGUI::UIViewSwitchContainer>`); the pad sits in `SelectedPadAdvanced` (`editor.uidesc:201`). Find it with `Shots::findFirst<Krate::Plugins::XYMorphPad>(root)`.
  - `TEST_CASE("XYMorphPad: a vertical drag sets Morph End on the proxy, the selected pad and the saved pad preset", "[ui][morph]")`. Select pad 2 (`setParamNormalized(kSelectedPadId, 2.0 / 31.0)`), drag to a known Y, then assert `getParamNormalized(kMorphEndId)` and `getParamNormalized(padParamId(2, kPadMorphEnd))` both equal the pad's `getMorphY()`, and that the blob from `padPresetStateProvider()` read back with `State::readPadPresetBlob` has `sound[25]` (morphEnd) equal to it.
  - `TEST_CASE("XYMorphPad: the cursor follows pad selection, automation and preset loads", "[ui][morph]")`. Set pad A morph start / end 0.2 / 0.8 and pad B 0.9 / 0.1 through their per-pad IDs; select A and assert `getMorphX/Y` = 0.2 / 0.8; select B and assert 0.9 / 0.1; `setParamNormalized(kMorphStartId, 0.4)` and assert X = 0.4; `setComponentState` with a kit whose selected pad has other morph values and assert the cursor shows them.
  - `TEST_CASE("XYMorphPad: a wheel nudge after a pad switch edits from the new pad's values", "[ui][morph]")`. After selecting B, send one `MouseWheelEvent` and assert the new X is B's 0.9 plus the wheel delta, not A's.
  Fails today: every assertion on the controller values and the cursor.

**2. Fix**

- Shared (`plugins/shared/src/ui/xy_morph_pad.h`): add one private helper that sends a secondary value, for example `void editSecondary(double y)`, which calls `controller_->setParamNormalized(secondaryParamId_, y)` and then `controller_->performEdit(secondaryParamId_, y)`; use it at the five sites above (begin / end calls stay where they are). Update the comment at `:11`. Before the edit, run `grep -rl "xy_morph_pad.h" plugins/ dsp/include tests/` (today: Membrum, Ruinae, `plugins/shared/tests/test_xy_morph_pad.cpp`) and list the consumers in the commit message. In Ruinae the new `setParamNormalized(kMixerTiltId, y)` reaches its hook at `controller_view_sync.cpp:467-470`, which sets the cursor to the value it already has; read that hook and confirm it does not re-enter the pad's edit path. The Ruinae controller value now updates as well, which fixes the same defect there.
- Membrum (`controller.cpp`, `controller.h`, `controller_view_sync.cpp`): add a refresh flag for the morph pad next to `kRefreshPitchEnvDisplay` (`controller.h:327-331`; take the next free bit after what T1.20 added) and a helper `updateMorphPadPosition()` that, when `xyMorphPad_` is set and `!xyMorphPad_->isDragging()` (`xy_morph_pad.h:144`), calls `setMorphPosition(getParamNormalized(kMorphStartId), getParamNormalized(kMorphEndId))`. Request it in `setParamNormalized` for `kMorphStartId` and `kMorphEndId` (next to the other display hooks, before the `suppressProxyForward_` early return), add it to the set `syncGlobalProxyFromPad` requests (`controller.cpp:982-984`) and dispatch it in `applyViewRefresh`. If T1.20 introduced a per-pad-ID refresh table (MB-099), add `kPadMorphStart` / `kPadMorphEnd` of the selected pad to it instead of a second mechanism. Keep the `verifyView` initialisation at `:1365-1368`.

**3. Build and gate**

- Add `Membrum` to the build. CROSS-PLUGIN: run `node tools/header-consumers.js --tag '[membrum]'`; build and gate every executable it names (expect `ruinae_tests` and `shared_tests` besides `membrum_tests`) with `~[long]` appended to the filter. pluginval for Membrum and also for Ruinae (`build/windows-x64-release/VST3/Release/Ruinae.vst3`), whose editor code path changed. clang-tidy adds `-Target shared` and `-Target ruinae` (the plugin sources that include the changed header).
- UI flag: `node tools/lint-plugin-editor-baseline.js`, `node tools/uidesc-inventory.js plugins/membrum/resources/editor.uidesc --check`, `node tools/gen-screenshots.js membrum` and `node tools/gen-screenshots.js ruinae`.

**4. Acceptance**

All five new cases are green, and the existing `[xy_morph_pad]` and Ruinae editor cases stay green. Before: after a vertical drag `kMorphEndId`, the per-pad `kPadMorphEnd` and pad-preset slot 25 kept their old value and the cursor ignored pad switches; after: all three equal the dragged Y and the cursor shows the selected pad's values within one refresh.

### T1.26 — Shared SVF: with smoothing off, snapToTarget() restores a stale mode mix, so Membrum's Bandpass exciter filters run as Lowpass

**Findings:** MB-273 (high, CONFIRMED by code reading in the main loop, 2026-11-24). Not in `report.md`: the T1.5 implementer found it, and the user ruled on 2026-11-24 to add it as a task after the rest of phase 1. This section is its evidence.

**Flags:** CROSS-PLUGIN IMPACT, PRESET-IMPACT.

**Prerequisites:** T1.15 (the tone-shaper SVF fix, MB-265: the same class, and its tests read the SVF smoother).

**Estimate:** 300 minutes.

**What is wrong today** (anchors read at `6f35ea4d`):

- `dsp/include/krate/dsp/primitives/svf.h`:
  - `prepare()` (`:161-176`) sets the mix targets `m0Target_` / `m1Target_` / `m2Target_` to the mix of the mode current at that moment.
  - With smoothing off (the default), these calls update the live mix `m0_` / `m1_` / `m2_` but never the targets:
    - `setMode()` (`:188-195`) and `setGain()` (`:261-270`) call `updateMixCoefficients()` directly.
    - `setCutoff()` and `setResonance()` (`:204-229`) reach it through `updateCoefficients()` → `updateDerivedCoefficients()` (`:469-486`).
  - `snapToTarget()` (`:309-318`) copies the stale targets into the live mix and does not recompute it from `mode_`.
  - Result: after a mode change made after `prepare()` (or a Q or gain change in a mode whose mix depends on `k_` or `A_`: Bandpass `m1 = k`, Allpass, Peak, the shelves), `snapToTarget()` plays the old mode's mix. It stays that way until the next `setCutoff` / `setResonance` / `setGain` / `reset`.
  - `enableSmoothing(true)` after such a change ramps the mix toward the stale target as well.
  - `reset()` (`:277-287`) is not affected: `updateDerivedCoefficients()` recomputes the mix from `mode_`.
- Membrum, where this is audible on every hit. In each of these, `prepare` calls `setMode(Bandpass)` after `filter.prepare()`, and every trigger calls `setCutoff` and then `snapToTarget()`, so every hit runs through the Lowpass mix (m0 0, m1 0, m2 1):
  - `NoiseBurstExciter`: `exciters/noise_burst_exciter.h:53-61`, `:106-107`.
  - `ClapExciter`: `exciters/clap_exciter.h:62-65`, `:115`.
  - `FrictionExciter` body filter: `exciters/friction_exciter.h:47-50`, `:88`.
  - Not affected as far as the main loop read; confirm before relying on it:
    - `NoiseLayer` (`noise_layer.h:47`) and `ClickLayer` (`click_layer.h:49`) set Bandpass but never call `snapToTarget`.
    - `FeedbackExciter` uses Lowpass, which is the prepare-time default.
    - `ToneShaper` enables smoothing, so its targets are computed.
- Other plugins: `snapToTarget()` appears in about 40 headers under `dsp/include` and `plugins/` (not all of them on an SVF). Every SVF user that changes mode, Q or gain after `prepare()` with smoothing off and then calls `snapToTarget()`, or enables smoothing later, has the same defect.

**1. Failing tests**

- `dsp_primitives_tests`, in the existing SVF test file (`grep -rl "SVF" dsp/tests/unit/primitives`): `TEST_CASE("SVF: with smoothing off, snapToTarget keeps the mix of the current mode", "[primitives][svf]")`.
  - Reference: an SVF configured with the same mode, Q, gain and cutoff before `prepare()`.
  - Under test: an SVF prepared first in Lowpass, then configured, then `snapToTarget()`.
  - Render the same 2048-sample noise through both and assert max abs diff <= 1e-6.
  - SECTIONs:
    - a mode change for each of the eight modes;
    - a Q change in Bandpass and in Allpass;
    - a gain change in Peak, LowShelf and HighShelf;
    - `setMode(Bandpass)` with smoothing off, then `enableSmoothing(true)`, compared after 4096 samples.
  - Red today: every non-Lowpass SECTION, and the Q and gain SECTIONs.
- `membrum_tests`: `TEST_CASE("NoiseBurst, Clap and Friction exciters band-limit their burst around the filter centre", "[exciter][svf]")`.
  - Per exciter, render one hit at 48 kHz.
  - Measure the energy in a band an octave around the exciter's own cutoff against the energy below cutoff / 8.
  - Assert the difference is at least the response of a 2-pole Bandpass at the exciter's Q at those frequencies, minus 6 dB. Compute it from the SVF formula in the test and print it.
  - Red today: the Lowpass passes the low band at 0 dB.

**2. Fix**

- `svf.h` only:
  - With smoothing off, keep the mix targets equal to the live mix wherever the live mix is recomputed (the smoothing-off branches of `setMode`, `setGain`, `setCutoff` and `setResonance`, or once inside `updateMixCoefficients()` behind `!smoothingEnabled_`).
  - Smoothing-on behaviour stays byte-identical; the SVF tests for smoothing on must not move.
  - Do not change `snapToTarget()`'s contract; correct its doc comment if it is wrong.
- Before the edit, list every consumer:
  - `node tools/header-consumers.js --tag '[membrum]' dsp/include/krate/dsp/primitives/svf.h` (expect most executables).
  - `grep -rn "snapToTarget\|enableSmoothing" dsp/include plugins --include=*.h` restricted to SVF members.
  - For each SVF site that changes mode, Q or gain after `prepare()` with smoothing off, write in the commit message whether its sound changes.

**3. Build and gate**

- Build and gate every executable `header-consumers.js` names, with `~[long]~[.]` appended to the filter.
- PRESET-IMPACT, Membrum:
  - The kit-quality before/after table goes into `preset_table`.
  - The audible change (NoiseBurst, Clap and Friction pads become band-limited, as designed) is the finding's purpose, so it is collected under the standing ruling.
- Other plugins:
  - Any render, golden or approval test of another plugin that moves is a sound change outside Membrum. Stop with `ruling_needed` naming the plugin, the test and the measured difference.
  - Never re-baseline another plugin's golden without a ruling.
- pluginval for Membrum and for every plugin whose output changed. clang-tidy `-Target dsp` and `-Target membrum`.

**4. Acceptance**

- Both new cases are green.
- The SVF smoothing-on tests are unchanged.
- Before: NoiseBurst, Clap and Friction hits ran through the Lowpass mix. After: Bandpass, matching the reference SVF within 1e-6.
- The commit message lists every SVF consumer site and whether its sound changed.

## Phase 2 — Medium and low defects

Fix the remaining wrong-audio, lifecycle, gain-staging, parameter-display and editor-wiring defects, medium severity first (T2.1-T2.20), then low (T2.21-T2.32).

### T2.1 — One voice renderer: process() wraps processBlock, and the phase-1 golden renders through the pool and fails when its file is missing

**Findings:** MB-041 (medium, CONFIRMED), MB-142 (medium, CONFIRMED).

**Flags:** MACOS (added: `plugins/membrum/tests/approval/test_phase1_regression.cpp:40-45` classifies finiteness with the forbidden `(bits & 0x7F800000u)` clone, listed at `tools/lint-finite-bitmask-baseline.json:41`; this task replaces it).

**Prerequisites:** T1.11.

**Estimate:** 240 minutes.

**What is wrong today:**

- `DrumVoice::process()` (`plugins/membrum/src/dsp/drum_voice.h:497-567`) is a fourth renderer that the plugin never runs: `VoicePool` calls only `DrumVoice::processBlock` (`voice_pool/voice_pool.cpp:356, 410, 471, 528`), which dispatches to `processBlockFast` / `processBlockSlow` (`drum_voice.h:597-629`). `process()` has no secondary shell bank, no tension modulation, leaves `wireGainMod_` at 1 (its own comment, `:542-546`), refreshes pitch envelope and morph every sample (`:506-519`) and never clears `active_`, so a voice rendered through it never retires (retirement lives only in `processBlock`, `:644-669`).
- The spec acceptance tests run that renderer: SC-009 (`unit/tone_shaper/test_pitch_envelope_808.cpp:118-119`, `voice.process()`) and the phase-1 golden (`approval/test_phase1_regression.cpp:51-73`). Nine more test call sites use it (Grep `voice.process()` under `plugins/membrum/tests`; the hit in `membrum_processor_tests.cpp:1166` is a comment: `test_decay_skew.cpp`, `test_mode_stretch.cpp`, `test_mode_inject.cpp`, `test_nonlinear_coupling.cpp`, `test_coupling_no_homogenization.cpp`, `test_allocation_matrix.cpp`). `tools/membrum-fit` already uses `processBlock` (`render_voice.cpp:37`).
- The golden is a self-regenerated change detector (8 rewrites of `tests/golden/phase1_default.bin` in `git log`), compares raw samples by RMS, locates its file through working-directory-relative paths plus a hard-coded `F:/projects/iterum/...` (`:79-95`), and on a missing file calls `WARN` + `SUCCEED` and returns (`:160-168`), so a runner from another directory turns it green silently.

T1.11 made the block path's retune independent of the host block size. Check that its tests are in the tree (`git log --grep T1.11`) and note the tolerance its block-size case asserts: this task reuses it. If T1.11 already ported SC-009 to `processBlock` (MB-240 is in T1.11), do not port it again.

**1. Failing test**

New file `plugins/membrum/tests/unit/dsp/test_renderer_equivalence.cpp`.

- `TEST_CASE("DrumVoice: process() renders the same note as processBlock (shell, tension, wire coupling, pitch envelope)", "[drum_voice][renderer]")`. Two `DrumVoice`s prepared at 48 kHz with the same settings: Impulse + Membrane, `setSecondaryEnabled(1)`, `setTensionModAmt(0.8f)`, `setWireCoupling(1.0f)`, `setNoiseLayerGain(4.0f)`, a 160 -> 50 Hz pitch envelope over 20 ms (the `toneShaper()` setters used by `test_pitch_envelope_808.cpp:107-110`). Render 1 s through `process()` and through `processBlock(out, 64)`. Compare with `Krate::DSP::TestUtils::fingerprintRender` / `compareFingerprints` (`tests/test_helpers/render_fingerprint.h:73, 122`) at the shared tolerances. Fails today: `process()` skips the shell, tension and wire stages.
- `TEST_CASE("DrumVoice: a note rendered through process() retires", "[drum_voice][renderer]")`. Default voice, one hit, 20 s of `process()`; assert `isActive()` is false at the end (`drum_voice.h:1092`). Fails today: `process()` never clears `active_`.
- `TEST_CASE("DrumVoice: processBlock renders the same note at block sizes 1, 64 and 2048", "[drum_voice][renderer]")`. Same voice settings as the first case, fingerprints compared at T1.11's tolerance. This one should already pass after T1.11; it guards the equivalence the wrapper below relies on.

**2. Fix**

- `drum_voice.h`, `DrumVoice::process()`: replace the body with a call into the one renderer, for example `float s = 0.0f; processBlock(&s, 1); return s;`. Delete the per-sample-only code and its comment at `:542-546` (the shell, tension, wire and retirement stages now run on every path). Keep the function so the test call sites compile; their thresholds stay as they are. Every one of the eleven call sites now exercises the production path: if one of those cases goes red, that is a real divergence the old renderer hid, so stop and report it with its numbers (never relax its threshold).
- `approval/test_phase1_regression.cpp`:
  - Render through `Membrum::VoicePool` (prepare 44.1 kHz, max block 64; default `PadConfig` on pad 0, note 36, velocity 0.8) in 64-sample `processBlock(outL, outR, 64)` calls; store the left channel (the default pan 0.5 is unity on both sides).
  - Replace `isFiniteSample` with `Krate::DSP::detail::isFinite` (`dsp/include/krate/dsp/core/db_utils.h:119`) and remove this file's entry from `tools/lint-finite-bitmask-baseline.json` (the ratchet must shrink).
  - Locate the golden as `std::filesystem::path(MEMBRUM_RESOURCES_DIR) / ".." / "tests" / "golden" / "phase1_default.bin"` (`MEMBRUM_RESOURCES_DIR` is defined for `membrum_tests` at `plugins/membrum/tests/CMakeLists.txt:225-228`) and delete the relative and hard-coded candidates.
  - Replace the `WARN` / `SUCCEED` / `return` with `REQUIRE(readGolden(path, golden))`.
  - Compare `fingerprintRender(current)` against `fingerprintRender(golden)` with `compareFingerprints` at the shared tolerances, with `INFO(cmp.detail)`, instead of the raw-sample RMS.
- Re-baseline the golden deliberately, once, by running the hidden case by name (`build/windows-x64-release/bin/Release/membrum_tests.exe "Generate Phase 1 golden reference"`), and add a sidecar note `plugins/membrum/tests/golden/phase1_default.md` that records this re-baseline (date, task id, reason: rendered through the pool instead of the unused per-sample renderer) and asks every later re-baseline to append its own line. The commit message says why the golden moved.

**3. Build and gate**

Add `Membrum` to the build (`drum_voice.h` is under `src/`). MACOS: run `node tools/lint-finite-bitmask.js`, `node tools/lint-nonfinite-symbols.js`, `node tools/check-portability.js plugins/membrum/tests/approval/test_phase1_regression.cpp plugins/membrum/tests/unit/dsp/test_renderer_equivalence.cpp`, and `tools/clangcl-fastmath-build.ps1 -Targets membrum_tests` (the plugin-test proxy; read its blind spot). pluginval runs: `drum_voice.h` is plugin code.

**4. Acceptance**

- The three renderer cases and the golden case are green; the golden case fails when `phase1_default.bin` is renamed (check once locally, then restore it).
- Grep finds no per-sample render body in `DrumVoice::process()` and no `0x7F800000` in `test_phase1_regression.cpp`; the bitmask lint baseline lost one entry.
- The SC-009 case (`"808Kick: PitchEnv Start=160 End=50 Time=20 ms gives ~50 Hz at t=20 ms (SC-009)"`) is green on the block path. The audit probe measured the old split: `process()` tracked a 400 -> 80 Hz sweep down to 80 Hz while `processBlock` at 256 samples stayed at 87-92 Hz (before T1.11).

### T2.2 — State and preset loads sanitise non-finite and out-of-range values; a NaN cannot latch a voice, the coupling limiter or the main bus

**Findings:** MB-010 (medium, CONFIRMED), MB-154 (low, CONFIRMED).

**Flags:** MACOS.

**Prerequisites:** none.

**Estimate:** 180 minutes.

**What is wrong today:**

- The codec reads every double raw (`plugins/membrum/src/state/state_codec.cpp:33-36` `readT` -> `readDouble`; `readKitBlob` pad loop `:456-460`, `readPadPresetBlob` from `:549`). `applyPadSnapshot` copies sound slots 0-27 and 30-33 with a bare `static_cast<float>` (`:167-200`), `applyPadPresetSnapshot` does the same (`:276` onward). Slots 34-58 and the kit-level doubles use `std::clamp`, which returns a NaN unchanged; the damping test `b1Raw < 0.0` (`:216-221`) is false for NaN. No `isFinite` exists anywhere in `plugins/membrum/src` apart from `Membrum::DSP::isNonFinite` in `feedback_exciter.h:147`.
- Nothing downstream stops a NaN. The voice's safety rail `Krate::DSP::hardClip` is `std::clamp` (`dsp/include/krate/dsp/core/sigmoid.h:267-269`; used at `drum_voice.h:566, 885, 898, 1061`). `ModalResonatorBank::flushSilentModes` tests `energy < kSilenceThreshold`, false for NaN, so the voice never retires. The pool's `std::max(peak, a)` drops NaN, so the voice looks silent. `Processor::applyEnergyLimiter` (`processor/processor.h:183-196`): once `energyEnvelope_` is NaN, `energyEnvelope_ > kThreshold` is false forever, so the -20 dBFS coupling cap is off until `applyKitSnapshot` resets it (`processor.cpp:967`). `TruePeakLimiter` states it does no sanitising because the per-voice `hardClip` bounds every voice (`true_peak_limiter.h:189-192`), which is false for NaN; its gain itself stays finite (MB-154 verifier), but NaN passes its output clamp (`:256-257`).
- MB-154 also notes that the 2026-06 audit appendix (`plugins/membrum/AUDIT-signal-path-2026-06-07.md:389, 420-421`) claims two NaN barriers that do not exist. That file is a prior audit and is never overwritten; the commit message records the correction instead.

**1. Failing test**

New file `plugins/membrum/tests/unit/state/test_nonfinite_state_sanitise.cpp`. It injects NaN and Inf, so it is an IEEE TU: add it to `plugins/membrum/tests/CMakeLists.txt` with its own `set_source_files_properties(... PROPERTIES COMPILE_OPTIONS "-fno-fast-math;-fno-finite-math-only;-ffp-contract=off")` inside `if(NOT MSVC)`, next to the existing block at `:261-266` (do not fix that block's missing flag here; T3.1 owns MB-014). Build the NaN / Inf values from bit patterns read through a `volatile std::uint32_t` (or `std::uint64_t` for doubles) and `std::memcpy`; no `numeric_limits` constants. Classify with `Krate::DSP::detail::isFinite`.

- `TEST_CASE("State load: a NaN or Inf in any sound slot loads as a finite in-range value on both sides", "[state][nonfinite]")`. Start from the default kit (`Processor::getState` -> `State::readKitBlob`). For each sound slot 0..58 and each of NaN, +Inf, -Inf: poke pad 0's slot, `State::writeKitBlob`, `Processor::setState`, `getState`, `readKitBlob`; assert every slot of every pad is finite and in its documented range. Feed the same blob to `Controller::setComponentState` and assert every registered parameter is finite and in [0, 1]. Repeat the poke for the kit-level doubles (global coupling, snare buzz, tom resonance, coupling delay, master gain, macros) and for a pad preset through `readPadPresetBlob` / `Processor`'s `LoadPadPreset` path. Fails today: the probe `state:malformed-stored` measured "NON-FINITE stored and written back by getState".
- `TEST_CASE("State load: out-of-range sound slots 0-33 are clamped to [0, 1]", "[state]")`. A kit blob with pad 0 decay = 1.7 and material = -0.3 loads as 1.0 and 0.0 on the processor (probe `double-apply:out-of-range-slots`: `1.700/1.000 / -0.300/0.000` msg / edits today) and every slot set to 5.0 reads back as at most 1.0 (probe `fieldmap:applyPadSnapshot-clamp`: slots 0-27 and 30-33 kept above 1.0 today).
- `TEST_CASE("NaN barrier: a voice whose pad config is NaN outputs finite samples and retires", "[voice_pool][nonfinite]")`. Bypass the codec: set `processor.voicePoolForTest().padConfigMut(0).material` (then `size`, `decay`, `strikePosition`, `tsFilterCutoff`, one per section) to NaN, hit note 36 and render 20 s at 48 kHz, block 256. Assert every main-bus sample is finite and the pool has no active voice at the end. Fails today: probe `nanSlot()` measured "non-finite samples 881920 / 881920, active after 20.0 s: YES" for each of the first four.
- `TEST_CASE("NaN barrier: one NaN voice does not latch the coupling limiter or the main bus", "[processor][nonfinite]")`. Global coupling 1.0, a NaN voice on pad 0 as above, then a clean hit on pad 2 one second later: assert `energyEnvelopeForTest()` is finite and the pad-2 hit's RMS and peak are within 0.1 dB of the same hit on a fresh processor with the same kit.

**2. Fix**

- Codec, `state_codec.cpp`. In `readKitBlob` and `readPadPresetBlob`, after reading, replace every non-finite double with that slot's default (take the defaults from `toPadSnapshot(PadConfig{})` for the sound slots and from the kit defaults for the kit-level fields) using `Krate::DSP::detail::isFinite(double)` (`db_utils.h:126`). In `applyPadSnapshot` and `applyPadPresetSnapshot` clamp slots 0-27 and 30-33 to [0, 1] like slots 34+. Leave the damping slot handling to whatever T1.23 committed (no sentinel after it; if T1.23 has not landed, keep `< 0` meaning the sentinel). Sanitising in the readers covers both sides, because the controller's load path reads the same blob through `controller_state_codec.cpp`.
- Voice rail, `drum_voice.h`, `DrumVoice::processBlock` (`:597` onward, before the retirement block): sum the block's output samples into one float and test it with `Krate::DSP::detail::isFinite`; a NaN or Inf anywhere makes the sum non-finite. On a hit, zero the block, run the same state wipe `resetForKitSwitch()` does (`:467-491`) and leave the voice inactive. One check per block, no per-sample branch.
- Bus rail, `processor.cpp`, `Processor::process`, after the voice pool and the coupling loop and before `busLimiter_.processBlock` (`:693-716`): the same one-sum check on `outL` and `outR`. On a hit, zero both buffers for this block and reset `busLimiter_`, `couplingEngine_`, `couplingDelay_` and `energyEnvelope_`. Read `TruePeakLimiter::reset`, `SympatheticResonance::reset` and `DelayLine::reset` first and confirm none allocates or frees (audio thread). The aux buses are fed by the voices, which the voice rail already cleans.
- MACOS: only `detail::isFinite`; no `std::isnan` / `std::isfinite`, no bit mask, no `numeric_limits` NaN or infinity in plugin code. Do not use `Membrum::DSP::isNonFinite` for the new checks (the plan's rule names `detail::isFinite`).
- Leave the shared `sigmoid.h` and `true_peak_limiter.h` untouched (the limiter's comment is about its callers' guarantee, which now holds for Membrum; report the comment for phase 6 instead of editing a header three other plugins consume).

**3. Build and gate**

Add `Membrum` to the build. MACOS: `node tools/lint-ieee-flag-sets.js` (the new block carries all three flags), `node tools/lint-finite-bitmask.js`, `node tools/lint-nonfinite-symbols.js`, `node tools/check-portability.js` on the changed `.cpp` files, `tools/clangcl-fastmath-build.ps1 -Targets membrum_tests`. The `[nonfinite]` cases are cross-platform sentinels: never `[long]`. Confirm `getState` of all 20 factory kits is byte-identical before and after (they are in range, so the clamp must not move a byte).

**4. Acceptance**

- The four cases are green. Before: 881920 of 881920 samples non-finite and the voice still active after 20 s; NaN stored and written back by `getState`; decay 1.7 kept. After: zero non-finite samples, no active voice, every stored value finite and in range.
- Factory-kit `getState` bytes unchanged; the default-kit render (golden case) unchanged.
- Grep finds a `detail::isFinite` call in `state_codec.cpp`, `drum_voice.h` and `processor.cpp`, and no new `std::isnan` / `std::isfinite` in `plugins/membrum/src`.

### T2.3 — A kit load fades ringing voices instead of wiping them, and the bus limiter and master gain do not step

**Findings:** MB-054 (medium, CONFIRMED), MB-091 (low, CONFIRMED).

**Flags:** THREADING.

**Prerequisites:** T1.1.

**Estimate:** 150 minutes.

**What is wrong today:**

- `Processor::applyKitSnapshot` (`plugins/membrum/src/processor/processor.cpp:918-927`) starts with `voicePool_.resetAllVoicesForKitSwitch()`, a deliberate "panic" (comment `:920-926`, added to stop a tonal beep after a kit switch). `VoicePool::resetAllVoicesForKitSwitch` (`voice_pool/voice_pool.cpp:268-281`) calls `DrumVoice::resetForKitSwitch` (`dsp/drum_voice.h:467-491`: amp envelope, both banks, exciter, layers and tone shaper reset, `active_ = false`) on every main and shadow slot, clears `meta_` / `releasingMeta_` and resets the allocator. A ringing voice drops to zero in one sample. The pool's 5 ms fast release (`VoicePool::beginFastRelease`, `voice_pool.cpp:874-959`), which steal and choke use to stay under the -30 dB click bound (spec 138 SC-021 / SC-022), is not used.
- The same function hard-resets the coupling return (`couplingEngine_.reset()`, `couplingDelay_.reset()`, `energyEnvelope_ = 0`, `processor.cpp:965-967`), which also cuts a ringing coupling tail.
- `busLimiter_` is reset only in `setActive` (`processor.cpp:1104`), so gain reduction from the old kit's last hit ducks the new kit's first hits for the limiter's release. `masterGainNorm_` is stored as a step (`:980-982`) and applied per block with no ramp (`:726-736`).
- Both load paths reach this: `setState` (`:905-915`) and the `LoadPresetState` message (`notify`, from `:1218`). T1.1 moved the apply onto the audio thread through a pending-kit transfer; read what it committed (`git log --grep T1.1`) and put the fade in that audio-thread apply.

**1. Failing test**

New file `plugins/membrum/tests/unit/processor/test_kit_switch_click.cpp`. Processor at 48 kHz, block 256, default kit, eight hits (the audit probe's setup: rows `kitswitch:setState:8-ringing` and `kitswitch:LoadPresetState:8-ringing` in `specs/membrum-audit/probes/probe-results.csv`), switch to `Acoustic/Acoustic Studio Kit` 200 ms after the last hit. The kit arrives through the same calls a host makes: `setState` between two `process()` calls, and a `LoadPresetState` IMessage to `Processor::notify` between two `process()` calls. Use `tests/test_helpers/artifact_detection.h` (`ClickDetector`) for clicks.

- `TEST_CASE("Kit switch: ringing voices fade out instead of being cut", "[kit_switch][click]")`, both load paths as sections. Asserts (a) the largest sample-to-sample step in the 10 ms after the switch is at most -30 dB relative to the main-bus peak of the 10 ms before it, and (b) `ClickDetector` finds no click in that window. Fails today: probe jump 0.0290 at a pre-switch level of -13.70 dBFS (0.206), one click; the bound is 0.0065.
- `TEST_CASE("Kit switch: the coupling tail fades instead of being cut", "[kit_switch][click]")`. Same, with global coupling 1.0 in the old kit so the coupling return rings at the switch.
- `TEST_CASE("Kit switch: the first hit of the new kit is not ducked by the old kit's limiter state", "[kit_switch][limiter]")`. Before the switch, hit a pad that drives the bus limiter (one of the pads at the limiter ceiling in `specs/membrum-audit/probes/limiter-pads.csv`; name it in the test); switch; hit a pad of the new kit 10 ms after the fade ends. Assert its peak is within 0.1 dB of the same hit on a fresh processor with the new kit loaded.
- `TEST_CASE("Kit switch: a master gain change at a kit load does not step", "[kit_switch][click]")`. The new kit's master gain differs from the old (for example +12 dB against -6 dB; edit a `KitSnapshot` in the test and write it with `State::writeKitBlob`); with voices fading, the step bound of the first case holds.
- THREADING: `TEST_CASE("Kit switch: deterministic interleavings keep the fade", "[kit_switch][threading]")` with sections: one message between two blocks; two kit messages between two blocks (the second wins, one fade); a message while eight voices ring. And `TEST_CASE("Kit switch: message thread and audio thread run concurrently", "[kit_switch][threading]")`: one `std::thread` posts 200 `LoadPresetState` messages alternating two kits while the test thread calls `process()`; asserts every sample finite and no step above the bound. Fails today only on the step assertion (T1.1 already made it race-free).

**2. Fix**

- `VoicePool` (`voice_pool.h` / `voice_pool.cpp`): add `void fadeOutAllVoicesForKitSwitch() noexcept` (Grep the name first). For every main slot in state `Active`: `beginFastRelease(slot, true)` (the forced path re-snapshots even when the shadow is still fading), then `resetForKitSwitch()` on the voice now in the main slot (the old shadow contents; this keeps the "no residue from the old kit" property the panic was added for), and set `meta_[slot]` to free. Main slots that are not Active get `resetForKitSwitch()` as today. Shadow slots already fading keep fading. Reset the allocator and re-apply the voice count as `resetAllVoicesForKitSwitch` does (`:279-280`). `applyKitSnapshot` calls this instead of `resetAllVoicesForKitSwitch`; keep the old function only if other callers remain (Grep), and update its doc comment (`voice_pool.h:236-242`).
- Coupling (`processor.cpp`, `applyKitSnapshot` `:952-967` and the coupling loop `:693-708`): do not reset the engine at the switch. Start a 5 ms linear fade of the coupling return (a gain member applied to `coupling` in the loop); when it reaches zero, run `couplingEngine_.reset()`, `couplingDelay_.reset()`, `energyEnvelope_ = 0` and only then `couplingEngine_.setAmount(gcF)` for the new kit, keeping the order the comment at `:952-964` requires (reset before `setAmount`). The infinite-ring cases in `unit/processor/test_kit_switch_infinite_ring.cpp` must stay green.
- Bus limiter: call `busLimiter_.reset()` when the kit-switch fade has finished (all shadow slots the switch started are free and the coupling fade is done), not at the switch, so the fading tail is still limited and the new kit starts at unity gain. Confirm `TruePeakLimiter::reset` does not allocate (audio thread).
- Master gain (`process()`, `:722-736`): keep the gain applied in the previous block in an audio-thread member and ramp linearly to the new target across the block when it differs; this smooths a kit load and host automation alike. The default (-6 dB, unchanged) path stays exactly the same samples.

**3. Build and gate**

Add `Membrum` to the build. THREADING: the stress case spawns `std::thread`, so `node tools/tsan-scope.js --check` must pass; on Windows run the stress case under the ASan build (root `CLAUDE.md`, "AddressSanitizer") and state in the commit message which sanitizer runs were done and which are left to CI. The existing steal and choke click cases (`unit/voice_pool/test_steal_click_free.cpp`, `test_choke_click_free.cpp`) and `test_kit_switch_infinite_ring.cpp` are in the gate and must stay green.

**4. Acceptance**

- All six cases are green. Step at the switch: 0.0290 before (one click, pre-switch level -13.70 dBFS), at most 0.0065 and no click after.
- RMS 1.2-1.3 s after the switch stays at the probe's -240 dBFS (no ring left behind).
- `test_kit_switch_infinite_ring.cpp`, the steal and choke click tests and the default-kit golden are unchanged.

### T2.4 — Audition pad-down and pad-up go through a small SPSC queue instead of one atomic word

**Findings:** MB-006 (medium, CONFIRMED), MB-055 (low, CONFIRMED).

**Flags:** THREADING.

**Prerequisites:** none.

**Estimate:** 90 minutes.

**What is wrong today:** `Processor::notify` stores both `AuditionPad` (`plugins/membrum/src/processor/processor.cpp:1186-1200`) and `AuditionPadOff` (`:1202-1216`) into the one `std::atomic<std::uint32_t> pendingAudition_` (`processor/processor.h:121-126`) with a plain `store`. `consumePendingAudition` (`processor.cpp:540-556`) takes one word per block with `exchange(0)`, called once per `process()` (`:622`). The controller sends the down / up pair from `PadGridView` (`controller/controller.cpp:1070-1102`, `ui/pad_grid_view.cpp:287-305, 389-393`), so a click shorter than one host buffer (46 ms at 2048 / 44.1 kHz; a trackpad tap at any buffer) loses its note-on, an Off(A) + On(B) in one block loses A's note-off (a sustaining pad keeps ringing), and a word posted while the host is not processing fires on resume. No test sends more than one message (the only audition test is `unit/processor/test_morph_host_simulation.cpp:137-190`).

**1. Failing test**

New file `plugins/membrum/tests/unit/processor/test_audition_queue.cpp`. Send the messages exactly as `test_morph_host_simulation.cpp:137-147` does (`sendAuditionPadMessage`; add the matching `AuditionPadOff` sender, attribute `midi` only, `controller.cpp:1091-1102`). Processor at 48 kHz, block 256. Separate pads by pan so each channel carries one pad: `voicePoolForTest().setPadConfigField(pad, kPadPan, 0.0f)` for pad A (left only) and `1.0f` for pad B (right only).

- `TEST_CASE("Audition: a pad-down and pad-up between two blocks still sound the pad and release it", "[audition][threading]")`. Post On(36) then Off(36), then one `process()`. Assert the block's peak is above -60 dBFS and that the pad's decay60 equals that of a reference where On arrives in block 0 and Off in block 1 (within 5 %). Fails today: the block is silent (only the Off word is seen).
- `TEST_CASE("Audition: an Off for pad A and an On for pad B in one block are both applied", "[audition][threading]")`. On(A) in block 0, then Off(A) and On(B) before block 5. Assert B sounds on the right channel and A's left-channel decay60 matches a reference where Off(A) arrives alone in block 5 (within 5 %) and differs from a no-Off reference (the probe measured note-off shortening the kick from 0.97 s to 0.10 s, so the difference is large). Fails today: A's note-off is lost.
- `TEST_CASE("Audition: two pad-downs in one block start two voices", "[audition][threading]")`. On(36) and On(38) before one block; `voicePoolForTest().getActiveVoiceCount() == 2`.
- `TEST_CASE("Audition: a request posted while the host is not processing does not fire on resume", "[audition][threading]")`. `setProcessing(false)`, post On(36), `setProcessing(true)`, one block: silent.
- `TEST_CASE("Audition: message thread and audio thread run concurrently", "[audition][threading]")`. One `std::thread` posts 10 000 On / Off pairs over random pads 36..67 while the test thread runs `process()`; after the producer ends and 10 s of blocks, no voice is active that received an On without its Off, and every sample is finite.

**2. Fix**

- `processor.h`: replace `pendingAudition_` with a fixed-capacity single-producer single-consumer ring of 16 `std::uint32_t` words (same encoding as today: bits 0-6 pitch, 7-13 velocity, bit 14 valid), a `std::array` plus `std::atomic<std::uint32_t>` write and read indices, power-of-two wrap. Plugin-local; Grep for the type name first (ODR rule). The producer is `notify` on the message thread, the consumer is `process()`; release / acquire on the indices. A full ring drops the incoming event (never overwrite an unread one); 16 events per block is far above what a mouse produces. This is the existing UI-to-audio message channel made lossless, not a new queue or IMessage loop (the Membrum leaf rule about the `MetersBlock` bridge concerns audio-to-UI data).
- `notify` (`processor.cpp:1186-1216`): push the word instead of storing it.
- `consumePendingAudition` (`:540-556`): drain every queued word in order, applying each as today (velocity 0 -> `voicePool_.noteOff`, else `noteOn`).
- Stale requests: in `setProcessing(false)` (the Processor does not override it today; add the override and call `AudioEffect::setProcessing`) and `setActive(false)` (`processor.cpp:1093`) set an atomic "discard" flag; the first `process()` after it drains and drops every queued word before reading new ones. Clearing the ring from those calls directly would make them a second consumer.

**3. Build and gate**

Add `Membrum` to the build. THREADING: `node tools/tsan-scope.js --check`; run the concurrent case under the ASan build on Windows and state which sanitizer runs were done in the commit message. `unit/processor/test_morph_host_simulation.cpp` (the existing single-message audition) stays green.

**4. Acceptance**

All five cases are green. Before: a down + up in one block produced a silent block and a lost note-off kept pad A ringing to its natural decay; after: the pad sounds and releases, both voices start, nothing fires after a resume, and the stress case leaves no stuck voice. Grep finds no `pendingAudition_` left.

### T2.5 — Steal and choke swap voice indices, not 270 KB DrumVoice objects; FMOperator keeps a valid table pointer after a copy or move

**Findings:** MB-005 (medium, CONFIRMED), MB-007 (low, CONFIRMED).

**Flags:** CROSS-PLUGIN IMPACT.

**Prerequisites:** T1.5.

**Estimate:** 180 minutes.

**What is wrong today:**

- `VoicePool::beginFastRelease` snapshots the live voice into the shadow slot with `std::swap(VP_RVOICES[slot], VP_VOICES[slot])` (`plugins/membrum/src/voice_pool/voice_pool.cpp:922`; comment `:895-921`). `std::swap` move-constructs a whole `DrumVoice` temporary on the audio thread's stack and moves three objects. `sizeof(DrumVoice)` measured 270 688 bytes (the comment at `voice_pool.h:23-27` still says 224 096). Callers, all on the audio thread inside `noteOn`: the Quietest steal `:171`, the allocator steal `:221` (`force = true`), `:583`, and the choke loop in `processChokeGroups` `:1047` / `:1068` (up to 16 swaps per note-on). A 270 KB frame risks the 512 KB render-thread stacks of macOS AU hosts.
- Storage: two heap arrays `voicesPtr_` / `releasingVoicesPtr_` (`voice_pool.h:331, 339`), reached through the macros `VP_VOICES` / `VP_RVOICES` (`voice_pool.cpp:26-27`, 22 uses) and `mainVoiceRef` (`voice_pool.cpp:43-46`).
- `Krate::DSP::FMOperator` defaults its copy and move operations (`dsp/include/krate/dsp/processors/fm_operator.h:89-92`) although `prepare()` stores a pointer to its own member (`osc_.setWavetable(&sineTable_)`, `:113`; `WavetableOscillator::setWavetable` only stores the pointer, `primitives/wavetable_oscillator.h:95-97`, and reads through it every sample, `:123, 152-157, 250-253`). After the voice swap, an `FMImpulseExciter` (`plugins/membrum/src/dsp/exciters/fm_impulse_exciter.h:149-150`, two operators) in the shadow reads the wavetable through a pointer into the other slot's variant storage, whose object may have been destroyed and replaced (undefined behaviour; inaudible today, the audit probe measured max |diff| 0 against an unswapped twin).

Read what T1.5 committed first (`git log --grep T1.5`): it removed the exciter / body type-swap allocation and may have changed how `ExciterBank` holds its alternatives. Re-read the `std::swap` site and the exciter storage before editing.

**1. Failing tests**

- New file `plugins/membrum/tests/unit/voice_pool/test_fast_release_no_voice_move.cpp`, `TEST_CASE("Fast release moves no DrumVoice: a stolen voice keeps its address", "[voice_pool][rt_safety]")`. `VoicePool` prepared at 48 kHz, max polyphony 4, policy Oldest; four notes, record `&pool.mainVoiceRef(victimSlot)` of the oldest slot, play a fifth note (steal). Assert the shadow voice of that slot (add `[[nodiscard]] const DrumVoice& releasingVoiceRef(int slot) const noexcept` next to `mainVoiceRef`, `voice_pool.h:260`) has the recorded address. Add a section for a choke: pads 42 / 46 in choke group 1 (`setPadChokeGroup`), hit 46 then 42, and assert the same for the choked slot. Fails today: the swap moves the contents between two separate arrays, so the address differs.
- In `dsp/tests/unit/processors/fm_operator_test.cpp` (builds into `dsp_processors_tests`): `TEST_CASE("FMOperator: a moved or copied operator reads its own wavetable", "[fm_operator][lifetime]")`. Prepare an operator held in a `std::optional<FMOperator>` at 48 kHz, 440 Hz; move-construct `b` from it; `reset()` the optional and `emplace()` a fresh, unprepared operator in the same storage; render 512 samples from `b` and from a freshly prepared reference with the same settings; assert they are equal (max |diff| <= 1e-6). Sections for copy construction, move assignment and copy assignment. Fails today: `b` reads the unprepared table in the reused storage and outputs silence (the pre-`prepare` contract at `fm_operator_test.cpp:303`, "process() before prepare() returns 0.0").

**2. Fix**

- `VoicePool`: one heap array of `2 * kMaxVoices` `DrumVoice`s plus two index tables, `std::array<std::uint8_t, kMaxVoices> mainIndex_` (initialised to `i`) and `shadowIndex_` (`i + kMaxVoices`). Replace the macros with two private accessors, `mainVoice(slot)` and `shadowVoice(slot)`, and change the 22 uses and `mainVoiceRef`. In `beginFastRelease` replace `std::swap` of the voices with `std::swap(mainIndex_[slot], shadowIndex_[slot])` (O(1), no temporary). Everything after the swap stays as it is (`fastReleaseDamp` on the shadow voice, the meta copy). `resetAllVoicesForKitSwitch` (or T2.3's `fadeOutAllVoicesForKitSwitch`) needs no index reset: each index table stays a permutation. Update the stale size comments (`voice_pool.h:23-27, 323-330`) with the current `sizeof(DrumVoice)` (print it once in the scaffold test `unit/voice_pool/test_voice_pool_scaffold.cpp:27`, which records it) and keep the `kVoicePoolSizeLimit` static_assert (`voice_pool.h:391-395`) valid. The `is_move_assignable` static_assert at `voice_pool.cpp:919-921` can go with the swap; `DrumVoice` stays movable.
- `fm_operator.h` (shared): define the four copy / move operations by hand, copying every member and then calling `osc_.setWavetable(&sineTable_)` whenever the source's oscillator pointed at the source's own table (an unprepared source keeps the null pointer). Keep them `noexcept` where they are today. Before editing, run `grep -rl "fm_operator.h" plugins/ dsp/include tests/` (today: Membrum's `fm_impulse_exciter.h` and `dsp/include/krate/dsp/systems/fm_voice.h`, whose `FMVoice` holds `std::array<FMOperator, 4>`, `fm_voice.h:700`) and list them in the commit message. Other plugins' output must stay bit-identical: copying the wavetable and re-pointing changes no sample.
- Report, do not fix: `dsp/include/krate/dsp/processors/phase_distortion_oscillator.h:140` has the same self-pointer (`osc_.setWavetable(&cosineTable_)`); name it in the commit message as a follow-up.

**3. Build and gate**

- Add `Membrum` and `dsp_processors_tests` to the build. CROSS-PLUGIN: run `node tools/header-consumers.js --tag '[membrum]'`, build and gate every executable it names (expect `dsp_processors_tests` and `dsp_systems_tests` for `FMVoice`) with `~[long]` appended. clang-tidy adds `-Target dsp`.
- The steal, choke and fast-release suites (`unit/voice_pool/test_steal_click_free.cpp`, `test_choke_click_free.cpp`, `test_fast_release_*.cpp`, `test_choke_allocation_free.cpp`, `test_voice_pool_allocation_free.cpp`) are in the gate and must stay green unchanged.
- Timing: the per-steal cost is a CPU question; run `node tools/run-cpu-tests.js membrum_tests` once, alone, after the gate, and record the numbers of `tests/perf/test_polyphony_benchmark.cpp` and `test_polyphony_stress_16.cpp` before and after in the commit message (never relax a budget).

**4. Acceptance**

- Both new cases are green. Before: the stolen voice's address changed (a whole-voice move with a 270 688-byte stack temporary per swap); after: the same address, and Grep finds no `std::swap(VP_` or swap of a `DrumVoice` in `voice_pool.cpp`.
- The FMOperator case is green; the existing `fm_operator_test.cpp` and `fm_voice_test.cpp` cases and every consumer gate named by `header-consumers.js` stay green, and Membrum's default-kit golden is unchanged.

### T2.6 — No ghost voices: a frozen shadow never returns to a main slot, and a choke into a still-fading shadow mutes the voice

**Findings:** MB-053 (medium, CONFIRMED), MB-148 (medium, CONFIRMED).
**Flags:** none.
**Prerequisites:** T1.9 (allocator bookkeeping, Quietest and the two `processBlock` overloads), T2.5 (the shadow hand-over no longer `std::swap`s two 270 KB `DrumVoice` objects).
**Estimate:** 180 min.

Context. The pool keeps 16 main `DrumVoice` slots and 16 shadow slots for the 5 ms fast-release crossfade (`kFastReleaseSecs`, 5 ms to the 1e-6 floor). Two defects share one cause: the voice that `VoicePool::beginFastRelease` puts back into the main slot is not guaranteed to be silent, and three callers mark the slot Free without a NoteOn that overwrites it.

- **MB-053.** When a fade reaches the floor, `VoicePool::applyFastRelease` (`plugins/membrum/src/voice_pool/voice_pool.cpp:962-1002`, floor branch `:988-997`) only sets `releasingMeta_[slot].state = Free` (`:994`). The shadow `DrumVoice` keeps `active_ == true`: `fastReleaseDamp()` (`plugins/membrum/src/dsp/drum_voice.h:104-107`) damps the bank but never gates the amp envelope, and `active_` clears only in the voice's own render (`drum_voice.h:644-665`: bank silent and envelope finished, or more than 256 silent blocks). The next `beginFastRelease` on that slot swaps this frozen voice into the main slot (`voice_pool.cpp:922`). A Steal overwrites it with a NoteOn (`:203-226`), but the choke (`processChokeGroups`, `:1047` then `:1054-1057`), the Quietest pre-steal (`noteOn`, `:171` then `:177`) and the polyphony shrink (`setMaxPolyphony`, `:583-584`) do not. Both render loops gate on `VP_VOICES[slot].isActive()` alone (mono overload `:354`, multi-bus overload `:469`), so the ghost renders with meta Free: no auto-release (needs meta Active, `:380-381`, `:508-509`), unreachable by `VoicePool::noteOff` (`:293`), and invisible to `getActiveVoiceCount` / `isAnyVoiceActive` (`:792-826`), so `Processor::process` flags the main bus silent while it carries audio (`plugins/membrum/src/processor/processor.cpp:739-741`).
- **MB-148.** `beginFastRelease(slot)` without `force` returns early when the shadow is still fading (`voice_pool.cpp:892-893`), yet the choke, Quietest and shrink callers still mark the slot Free and idle the allocator slot. The choked voice keeps ringing unramped (open hat not cut by the closed hat), or is hard-overwritten by a later NoteOn on that idle slot (a click). No test covers a choke, Quietest pre-steal or shrink into a busy shadow, nor three same-slot steals inside 5 ms.

After T2.5 the hand-over at `voice_pool.cpp:922` may be an index or pointer exchange rather than `std::swap`; find it inside `VoicePool::beginFastRelease` and apply the steps below to whichever object ends up in the main role and the shadow role. After T1.9 the allocator calls around `:171-177` and `:1054-1057` may have moved; find them by the symbols named here.

**1. Failing test**

New file `plugins/membrum/tests/unit/voice_pool/test_shadow_ghost_voices.cpp` (add it to the `membrum_tests` list in `plugins/membrum/tests/CMakeLists.txt`). Use the public accessors `VoicePool::mainVoiceRef(slot).isActive()` (`voice_pool.h:260`), `voiceMeta(slot)` and `releasingMeta(slot)` (`:210`, `:214`); configure choke groups with `setPadChokeGroup` (`:174`), never the dead global `setChokeGroup`; fill the kit with `DefaultKit::apply(pool.padConfigsArray())` (`plugins/membrum/src/dsp/default_kit.h:398`) and then call `setPadChokeGroup(p, pool.padConfig(p).chokeGroup)` for every pad. Define one local helper `int countGhosts(VoicePool&)` = number of slots in `[0, kMaxVoices)` with `mainVoiceRef(s).isActive() && voiceMeta(s).state != VoiceSlotState::Active`.

- `TEST_CASE("VoicePool: a finished fast release never returns a sounding voice to a main slot", "[voice_pool][fast_release][ghost]")`. `prepare(48000.0, 128)`, `setMaxPolyphony(8)`, Oldest policy. 48 hits alternating note 46 and note 42 (choke group 1), one every 128 ms (48 blocks of 128 samples). After every block, record `countGhosts`. Assert that no hit is followed by a ghost (0 of 48) and that the maximum concurrent ghost count is 0. Baseline (MB-053, probe S7): 39 of 48 hits followed by at least one ghost, up to 7 concurrent.
- `TEST_CASE("VoicePool: shrinking polyphony then growing it back resurrects nothing", "[voice_pool][poly][ghost]")` with two SECTIONs:
  - "shadow free": 8 voices on 8 different pads (notes 36..43), render 50 ms, `setMaxPolyphony(4)`, render 1 s, `setMaxPolyphony(8)`, render 3 s.
  - "shadow busy": arrange the note-on order so a note-on steals a slot that `setMaxPolyphony(4)` will release (the allocator's `setVoiceCount` returns the released slots; read `VoicePool::setMaxPolyphony`, `voice_pool.cpp:570-588`), and call `setMaxPolyphony(4)` in the same block, right after that steal. `REQUIRE(pool.releasingMeta(slot).state == VoiceSlotState::FastReleasing)` for that slot before the shrink, so the SECTION cannot pass vacuously. Then render 1 s, grow to 8, render 3 s.
  In both SECTIONs assert, after every block: `countGhosts(pool) == 0`; if the block's output peak exceeds 1e-6 then `pool.isAnyVoiceActive()` is true; and after the grow, `mainVoiceRef(s).isActive()` is false for every slot `s` in `[4, 8)` that held a released voice. Baseline (MB-053, probe S6): after the grow a snare came back at peak 0.0927 for 2.7 s (shadow free) and 0.4697 for 3.1 s (shadow busy), both with `isAnyVoiceActive() == false`.

Extend `plugins/membrum/tests/unit/voice_pool/test_choke_click_free.cpp` (its fixtures `prepare` the pool at `:68-74` and `:169-170`; its click measurement and `blocksForMs` helper at `:46` are reused, not copied):

- `TEST_CASE("Choke into a slot whose shadow is still fading mutes the main voice within 6 ms", "[voice_pool][choke][m8]")`. `prepare(48000.0, 32)` (blocks of 0.67 ms so events land within 2 ms), `setMaxPolyphony(4)`, Oldest. Pads of notes 36..39 at `kPadLevel` 0 fill the four slots. Note 46 (pad 10, choke group 1, level 1, long decay) steals slot k; `REQUIRE(releasingMeta(k).state == FastReleasing)`. Within 2 ms trigger note 42 (pad 6, choke group 1, level 0). Assert: the output 6 ms after the choke is at or below -60 dB relative to the open hat's pre-choke peak, and no click above -30 dBFS anywhere in the window (the bound the file already uses). SECTIONs "Quietest pre-steal into a busy shadow" (policy Quietest, the pre-steal victim's shadow busy) and "polyphony shrink into a busy shadow" (`setMaxPolyphony` releases a slot whose shadow is busy), same assertions. Today the open hat rings on at its pre-choke level, or is cut by a hard overwrite.
- `TEST_CASE("Three same-slot steals within 5 ms are click-free", "[voice_pool][steal][m8]")`: three re-triggers of the same note within 5 ms (same-note retrigger steals the same slot), measured the way `test_fast_release_resteal_collision.cpp:144` measures its collision; click peak at or below -30 dBFS, and `countGhosts == 0` afterwards.

Confirm the ghost, resurrection and busy-shadow choke cases fail on current code; the three-steal case may already pass (it is a coverage gap, MB-148): keep it either way.

**2. Fix**

All in `plugins/membrum/src/voice_pool/voice_pool.cpp` (and the comment in `voice_pool.h`):

1. `VoicePool::applyFastRelease`, floor branch (`:988-997`): when the gain reaches the floor and the slot goes Free, also hard-reset the shadow voice with its existing `DrumVoice::resetForKitSwitch()` (`drum_voice.h:467-491`; allocation-free, sets `active_ = false`). Before: the shadow stays `active_ == true` with frozen state. After: a finished shadow is inactive, so the next hand-over can only bring an idle voice into the main slot.
2. `VoicePool::beginFastRelease` (`:874-960`): after the hand-over, if the voice now in the main slot still reports `isActive()` (the forced case: it is the old shadow, mid-fade), reset it with `resetForKitSwitch()`. Before: the old shadow lands in the main slot still sounding and relies on a following NoteOn. After: the main slot holds an inactive voice whatever the caller does next; a Steal's NoteOn then starts from a clean voice.
3. The three callers that mark the slot Free without a guaranteed NoteOn pass `force = true`: the main-voice branch of `processChokeGroups` (`:1047`), the Quietest pre-steal in `noteOn` (`:171`), and `setMaxPolyphony` (`:583`). Before: a busy shadow made `beginFastRelease` return early and the sounding main voice stayed in a Free slot. After: the sounding main voice is always snapshotted into the shadow and fades in 5 ms; only the older, already-attenuated shadow tail is truncated (the M-8 trade-off, already accepted for steals). The choke-completeness re-scan (`:1065-1069`) keeps the non-forced call: it targets slots whose shadow is already fading.
4. Rewrite the comments that state the old contract: `beginFastRelease`'s doc in `voice_pool.h:273-287` and the in-body comments at `voice_pool.cpp:879-893`, `:911-928` and `:953-959` (force is now used by every caller except the re-scan; the voice left in the main slot is always inactive), and the steal comment at `:214-220`.

Do not add a render-loop guard on `meta_` state: with steps 1 to 3 a Free slot cannot hold an active voice, and the new tests assert exactly that.

**3. Build and gate**

Standard workflow; Membrum-only. The allocation-free voice-pool cases (`test_choke_allocation_free.cpp`, `test_voice_pool_allocation_free.cpp`) are in the gate and must stay green: `resetForKitSwitch` now runs on the audio thread.

**4. Acceptance**

- S7 pattern: 0 of 48 hits followed by a ghost, 0 concurrent (baseline 39 of 48, up to 7).
- S6 pattern: no voice resurrects after the grow in either SECTION (baseline peaks 0.0927 and 0.4697); no block with output above 1e-6 while `isAnyVoiceActive()` is false.
- Choke, Quietest and shrink into a busy shadow: at or below -60 dB 6 ms after the choke, no click above -30 dBFS.
- Three same-slot steals inside 5 ms: click at or below -30 dBFS.
- Every existing steal, choke, fast-release and allocation-free case stays green.

### T2.7 — The default kit goes through the canonical kit apply on processor and controller: hi-hats choke on a fresh instance and the UI shows the kit that plays

**Findings:** MB-116 (medium, CONFIRMED), MB-090 (low, CONFIRMED).
**Flags:** none. (Not PRESET-IMPACT: no kit data or voice DSP changes. The fresh instance gains the choke behaviour a saved and reopened project already has, and the kit-quality probe renders single pads, which the choke does not reach.)
**Prerequisites:** none.
**Estimate:** 150 min.

Context. A full kit reaches the processor by three routes. `setState` and the browser's "LoadPresetState" go through `Processor::applyKitSnapshot` (`plugins/membrum/src/processor/processor.cpp:918-1003`), which also syncs the separate choke table (`:944-946`), reclassifies the pads, recomputes the coupling matrix and syncs the macro cache. `Processor::initialize` instead calls `DefaultKit::apply(voicePool_.padConfigsArray())` (`:109-120`), which writes `cfg.chokeGroup = 1` on pads 6, 8 and 10 (`plugins/membrum/src/dsp/default_kit.h:487-489`) but never fills `VoicePool::chokeGroups_` (zero-initialised, `plugins/membrum/src/voice_pool/choke_group_table.h:49`); the note-on choke reads only that table (`voice_pool.cpp:254`, `:1030-1032`). So on a fresh instance the closed hat does not cut the open hat (MB-116). On the controller side, `Controller::initialize` registers all 32 x 66 per-pad parameters from one generic table, `kPadParamSpecs` (`plugins/membrum/src/controller/controller.cpp:167-250`, loop `:672-725`), and never applies the default kit: the controller's values and every per-pad `defaultNormalizedValue` describe 32 identical generic pads (material 0.5, size 0.5, decay 0.3, exciter Impulse, body Membrane, choke 0) while the processor plays the GM kit (MB-090). Hosts normally copy `getState` into `Controller::setComponentState` (`plugins/membrum/src/controller/controller_state.cpp:64-99`), which hides the value mismatch, but the reset-to-default value stays generic in every host.

If T1.1 turned `applyKitSnapshot` into a hand-off that the audio thread consumes in `process()`, use the synchronous function that consumer calls: `initialize` runs before any audio thread exists, so a synchronous apply is correct there.

**1. Failing test**

- In `plugins/membrum/tests/unit/processor/test_default_kit.cpp` (next to "DefaultKit Hat choke group 1 - MIDI 42, 44, 46", `:169`, which checks only `cfg.chokeGroup`): `TEST_CASE("DefaultKit: hi-hats choke on a fresh instance (FR-032)", "[default_kit][choke]")`. A fresh `Membrum::Processor`: `initialize`, `setupProcessing` (48 kHz, block 32), `setActive(true)`, no `setState`. Send `padParamId(6, kPadLevel)` = 0 through `Krate::Test::ParameterChanges` (`tests/test_helpers/vst_param_changes.h`) so the closed hat is silent, note-on 46 at t = 0 and note-on 42 at t = 200 ms through `Krate::Test::EventList` (`tests/test_helpers/vst_event_list.h`). Assert that the RMS of the window 20 ms after the choke is at least 60 dB below the RMS of the 20 ms before it. Baseline (probe `choke:openhat46-by-42`, `specs/membrum-audit/probes/probe-results.txt:13-14`): fresh instance -23.79 dB at 20 ms (unchoked); after `setState(getState())` -175.36 dB.
- New file `plugins/membrum/tests/unit/controller/test_controller_default_kit.cpp` (add to CMake): `TEST_CASE("Controller: a fresh instance shows and defaults to the kit the processor plays", "[controller][default_kit]")`. A fresh `Membrum::Controller` (`initialize` only, no `setComponentState`) and a fresh `Membrum::Processor` (`initialize` only). Write `ControllerState::buildSnapshot(controller, 0)` (`controller_state_codec.h:84`) with `State::writeKitBlob` and compare it byte for byte with `processor.getState`: zero differing bytes. Then, for every pad and every active offset (the offsets `kPadParamSpecs` registers), `controller.getParameterObject(id)->getInfo().defaultNormalizedValue == controller.getParamNormalized(id)`. Today both fail: pad 0 material 0.5 vs kit 0.3, size 0.5 vs 0.8, choke 0 vs 1 on pads 6/8/10, and so on.

**2. Fix**

1. New header `plugins/membrum/src/state/default_kit_snapshot.h` (namespace `Membrum::State`; grep for `makeDefaultKitSnapshot` first, ODR rule), one function `[[nodiscard]] inline KitSnapshot makeDefaultKitSnapshot()`: fill a local `std::array<PadConfig, kNumPads>` with `DefaultKit::apply`, convert every pad with `State::toPadSnapshot`, and leave the globals at the `KitSnapshot` member defaults (`state_codec.h:69-92`). Before relying on those defaults, compare each with the processor's own initial value (`maxPolyphony_`, `voiceStealingPolicy_`, `globalCoupling_`, `snareBuzz_`, `tomResonance_`, `couplingDelayMs_`, `masterGainNorm_{0.5f}` at `processor.h:156`, `selectedPadIndex_`); where one differs, set that field explicitly to the processor's current initial value so a fresh instance sounds exactly as before, and name the difference in the commit message (default drift between registrations is T6.3's, not this task's). `state/` already serves both sides, so the processor/controller no-cross-include rule holds.
2. `Processor::initialize` (`processor.cpp:109-120`): call `macroMapper_.prepare(buildRegisteredDefaultsTable())` first, then `applyKitSnapshot(State::makeDefaultKitSnapshot())`, and delete the `DefaultKit::apply` call and the classify loop (the canonical apply does both, plus the choke sync and the macro-cache sync). Before that, read every call inside `applyKitSnapshot` and confirm it is safe before `setupProcessing` has prepared anything: `voicePool_.resetAllVoicesForKitSwitch()`, `voicePool_.setMaxPolyphony`, `couplingEngine_.reset()`, `couplingDelay_.reset()`. If one of them touches a buffer that only `prepare` allocates, split `applyKitSnapshot` into `installKit(kit)` (pad configs, choke sync, globals, classify, coupling matrix, macro cache) and the audio-state reset, have `applyKitSnapshot` call both, and call only `installKit` from `initialize`. Before: the default kit bypasses the choke sync. After: all three routes install a kit through the same code.
3. `Controller::initialize` (`controller.cpp:274`), after the per-pad registration loop (`:672-725`) and before the preset managers (`:727`): build `State::makeDefaultKitSnapshot()` and apply it through `Membrum::ControllerState::applySnapshot(kit, setter, {.applySelectedPad = true})`, the same call `setComponentState` makes (`controller_state.cpp:83-88`). The setter writes the value with `EditControllerEx1::setParamNormalized`, and for per-pad IDs only (`padIndexFromParamId(id) >= 0`) also sets `getParameterObject(id)->getInfo().defaultNormalizedValue` to the same value. Then call `syncGlobalProxyFromPad(0)` as `setComponentState` does (`:92`); skip the grid calls there (no view exists during `initialize`). Global proxies keep their registered defaults (their drift is T6.3). Before: generic values and generic defaults. After: values and reset-to-default values equal the kit the processor plays. No parameter type, ID or range changes.

**3. Build and gate**

Standard workflow. Add `Membrum` to the build (processor, controller and state sources change); pluginval is required.

**4. Acceptance**

- Fresh instance: the open hat 46 is at least 60 dB down 20 ms after note 42 (baseline -23.79 dB, unchoked).
- Fresh controller snapshot and fresh processor `getState` are byte-identical; every per-pad `defaultNormalizedValue` equals the default-kit value.
- `getState` of a fresh processor is byte-identical to what the code wrote before the change (capture the bytes from a run before the fix and compare once after it; do not commit the capture), so the default kit's sound and saved state did not move.
- The existing default-kit, choke and editor-lifecycle cases stay green.

### T2.8 — State written by released versions 1 and 2 loads through read paths instead of being rejected; tests, goldens and comments name the real versions

**Findings:** MB-119 (medium, CONFIRMED), MB-088 (medium, PLAUSIBLE), MB-153 (medium, PLAUSIBLE), MB-147 (low, PLAUSIBLE), MB-093 (low, CONFIRMED).
**Flags:** STATE-COMPAT, VERIFY-FIRST.
**Prerequisites:** T1.22 (it moves the format to version 6 and adds the read path for 5).
**Estimate:** 300 min.
**Ruling:** this task is in the ruling table. Implement the plan's default (read paths), then stop before the commit and show the user the results with the question "Read paths for released versions 1 and 2, or a documented break?". If the user rules for a documented break, keep only the comment, test-name and CHANGELOG parts and turn the fixture tests into explicit-rejection tests.

Context. Origin carries the release tags `membrum/v0.9.0`, `v0.9.1` (blob version 1), `v0.10.1`, `v0.10.2` (version 2) and `v0.12.0` to `v0.12.5` (version 5); `v0.11.0` (version 4) was never tagged. Re-check with `git ls-remote --tags origin "refs/tags/membrum/*"` before starting. Today `readKitBlob` and `readPadPresetBlob` accept one version only (`plugins/membrum/src/state/state_codec.cpp:428-432` and `:556-560`), so a project or user kit saved with 0.9.x or 0.10.x loads as the default kit with no message, and saving overwrites it. The code justifies this with "the plugin has not shipped" (`plugins/membrum/src/plugin_ids.h:26-27`, `plugins/membrum/src/state/state_codec.h:12-14`, `:103-106`, `:132-133`); the CHANGELOG calls the bumps "Pre-release, strict version check with no migration" (`plugins/membrum/CHANGELOG.md:189`, `:231`, `:329`). The PLAUSIBLE verdicts (MB-088, MB-153, MB-147) rest on whether that documented policy makes the rejection intended; the head's STATE-COMPAT rule decides it for this plan: a released version gets a read path.

The released layouts were read at the tags for this plan (`git show membrum/v0.9.0:plugins/membrum/src/state/state_codec.h`, same at `membrum/v0.10.2`); verify them again with the same commands:

| Version | Released in | Kit blob per pad `sound` slots | Pad-preset blob slots | Otherwise |
|---|---|---|---|---|
| 1 | v0.9.0, v0.9.1 | 52 (indices 0-51) | 51 (indices 0-50), 420 bytes | same field order and globals block as today |
| 2 | v0.10.1, v0.10.2 | 56 (adds 52-55: pitch-envelope knee, mid pitch, mid fraction, curve 2) | 56, 460 bytes | same |
| 5 | v0.12.0 - v0.12.5 | 59 (adds 56 pan, 57 noiseLayerGain, 58 wireCoupling) | 59, 484 bytes | read path added by T1.22 |

Byte order is unchanged (the IBStreamer rewrite in audit finding 13 kept little-endian on the wire). The per-pad parameter stride was 64 in versions 1 and 2 and is 128 now (`plugins/membrum/src/dsp/pad_config.h:25`), and `kChokeGroupId` (252) changed from `RangeParameter(0..8, 8 steps)` to a 9-entry `StringListParameter` with the same normalized mapping (`controller.cpp:484-496`). Host automation is stored by parameter ID, so the plugin cannot remap lanes recorded at the old stride; the CHANGELOG states that instead.

**1. Failing test**

Fixtures from the released builds (real data, not hand-made):

```
git show "membrum/v0.9.0:plugins/membrum/resources/presets/Kit Presets/Acoustic/Acoustic Studio Kit.vstpreset" > "plugins/membrum/tests/golden/kit_v1_v0.9.0_acoustic_studio.vstpreset"
git show "membrum/v0.10.2:plugins/membrum/resources/presets/Kit Presets/Acoustic/Acoustic Studio Kit.vstpreset" > "plugins/membrum/tests/golden/kit_v2_v0.10.2_acoustic_studio.vstpreset"
```

Extracting the component-state chunk from a `.vstpreset` is already written as `loadFactoryPreset` in `plugins/membrum/tests/unit/processor/test_kit_switch_infinite_ring.cpp:118-158` (anonymous namespace). Move its chunk-extraction part into a small shared test header `plugins/membrum/tests/unit/state/vstpreset_chunk.h` (`IPtr<MemoryStream> componentChunkFromFile(const std::filesystem::path&)`), make `loadFactoryPreset` call it, and use it in the new tests; do not write a second copy. No pad-preset file was ever released, so the version-1 and version-2 pad blobs are written in the test with `IBStreamer` (little-endian) from the table above: header, exciter, body, then 51 or 56 doubles.

In `plugins/membrum/tests/unit/state/test_state_codec.cpp`:

- Replace `"state_codec: readKitBlob rejects every version other than kBlobVersion"` (`:123-145`) with `TEST_CASE("state_codec: readKitBlob accepts the released versions 1, 2, 5 and the current one and rejects every other", "[state_codec][migration]")`: versions 0, 3, 4, current + 1 and 99 return `kResultFalse` and leave the destination snapshot untouched; 1, 2, 5 and current return `kResultOk` on a well-formed blob of that version. Name the release tags in a comment. Same change for the pad twin at `:257-273`.
- `TEST_CASE("state_codec: a kit saved by v0.9.0 (blob version 1) loads with every stored field preserved", "[state_codec][migration]")` and the same for v0.10.2 (version 2). Read the fixture's chunk with an independent byte parser in the test (header 12 bytes; per pad 4 + 4 + n x 8 + 1 + 1 bytes; then the selected pad, the four coupling globals, 32 coupling amounts, 160 macros, master gain, optional uiMode), `REQUIRE` the first int32 to be 1 (or 2) so the fixture proves what it is, then `readKitBlob` and compare: every stored slot, choke group, output bus, coupling amount, macro and global equals the raw value (after the clamps `applyPadSnapshot` applies), and every slot the old version did not have holds its legacy value from step 2.
- `TEST_CASE("state_codec: pad presets of blob versions 1 and 2 load", "[state_codec][migration]")` with the synthesised pad blobs.

Rename `plugins/membrum/tests/unit/processor/test_state_v6_migration.cpp` to `test_state_version.cpp` (update the CMake list; keep every case T1.22 added there) and add:

- `TEST_CASE("State: setState and setComponentState load a kit saved by v0.10.2", "[state][migration]")`: `Processor::setState` on the v2 fixture returns `kResultOk`; `getState` then writes the current version; `Controller::setComponentState` returns `kResultOk` and `getParamNormalized(padParamId(1, kPadMaterial))` equals the raw material of pad 1. Same SECTION for the v1 fixture.
- Update `"State: setState rejects every version other than kCurrentStateVersion"` (`:207-228`) to the accepted/rejected sets above; a rejected blob leaves the previous kit intact (compare `getState` bytes before and after).

VERIFY-FIRST: on current code the fixture loads return `kResultFalse`, so MB-088, MB-153 and MB-147 are confirmed by step 1. If a fixture load already returned `kResultOk`, the PLAUSIBLE part would be refuted: record it and change no reader code for it.

**2. Fix**

1. Check that no stored meaning changed between the tags before mapping slots one to one: read `git diff membrum/v0.9.0 membrum/v0.12.0 -- plugins/membrum/src/state plugins/membrum/src/dsp/pad_config.h plugins/membrum/src/dsp/pad_config_apply.h plugins/membrum/src/voice_pool/voice_pool.cpp` and the CHANGELOG entries from 0.9.1 to 0.12.0, and list every slot whose denormalisation or meaning changed. A changed slot gets a conversion in the migration that preserves the plain value the old build used. Engine changes that alter how an unchanged value sounds are not migrated; the stored values are kept and the CHANGELOG says the sound may differ. Put the list in the commit message.
2. `plugins/membrum/src/state/state_codec.cpp`, `readKitBlob` (`:421`) and `readPadPresetBlob` (`:549`): replace the single-version check with a per-version slot count (kit: 1 -> 52, 2 -> 56, 5 -> 59, current -> what T1.22 left; pad: 1 -> 51, 2 -> 56, 5 -> 59, current). Read that many doubles into `sound[0..n-1]`, fill the remaining slots from one table of legacy values, then run the version-5-to-current migration T1.22 added. The legacy values are those that make the new engine do what the old build did: pitch-envelope knee, mid pitch, mid fraction and curve 2 (slots 52-55) at the values that reproduce the old two-point envelope (read `PadConfig`'s defaults in `pad_config.h` and the Phase 10 change at the tag to confirm), pan 0.5 (slot 56, centre), noiseLayerGain at the gain the old engine applied (slot 57; read the noise-layer mix at `membrum/v0.10.2` to confirm it was an implicit 1.0), wireCoupling 0.0 (slot 58, "independent buzz (legacy)", `controller.cpp:666-670`). Versions 3, 4 and anything above current stay rejected, with the destination untouched (read into a local snapshot and copy only on success).
3. Leave the four version constants alone: this task adds read paths and changes no layout (STATE-COMPAT: one new version, set by T1.22).
4. Comments that state the false premise or the wrong format (MB-093, MB-119): `plugin_ids.h:26-27`; `state_codec.h:12-14` (current version, slot count, and "released builds wrote 1, 2 and 5; all three are read"), `:103-106`, `:120` and `:132-133`, `:145-153` (pad blob: the size the constants give after T1.22; 484 bytes with 59 slots at commit `4a14376d`); the "State v4 ... 34 x float64" layout block above `Processor::getState` (`processor.cpp:855-869`): replace it with a one-line pointer to `state_codec.h`. Other stale comments in these files belong to T6.7 (MB-127, MB-128); leave them.
5. Test text (MB-093): `test_state_version.cpp` header (`:8-11`) and `:99`; `plugins/membrum/tests/unit/processor/test_coupling_state.cpp:117-130` (comment "Pre-release reset pins this at 1" and the case name "kCurrentStateVersion is the pinned pre-release value" become the real version and "kCurrentStateVersion is the current format version"); `plugins/membrum/tests/unit/preset/test_pad_preset.cpp:6`, `:75`, `:78`, `:101` (460 bytes / 56 float64 become the size the constants give).
6. Delete `plugins/membrum/tests/golden/phase2_state_v2.bin`: it is a 268-byte Phase-2 development layout that no release wrote and no test reads, and it suggests coverage that does not exist. The real version-2 fixture replaces it. Leave its mentions in `specs/138-*` (history).
7. `plugins/membrum/CHANGELOG.md`: add bullets under the topmost unreleased section (create `## [Unreleased]` if the topmost section is a tagged release; never touch `version.json`): projects and kits saved by 0.9.x and 0.10.x load again; per-pad automation lanes recorded in those versions addressed the old 64-wide stride and now drive other parameters (pad p, offset o was 1000 + 64p + o and is now 1000 + 128p + o), which the plugin cannot remap; the Choke Group parameter's type changed from range to list in 0.12.0 with the same normalized mapping. Do not rewrite the old entries.

**3. Build and gate**

Standard workflow plus the STATE-COMPAT obligations: build `Membrum` too; regenerate the 20 factory kits with `generate_membrum_presets` and confirm they are byte-identical to the committed ones (this task changes no write path), and that `getState(setState(kit))` is idempotent for all 20; the version-5 read path from T1.22 keeps its test. Both sides and both blob kinds are covered by the tests above. pluginval is required.

**4. Acceptance**

- The v0.9.0 and v0.10.2 Acoustic Studio Kit fixtures load through `readKitBlob`, `Processor::setState` and `Controller::setComponentState` with every stored field equal to the raw bytes (baseline: `kResultFalse` on all three; probe `state:released-versions` "versions 1,2,4,6 rejected (rc 1), previous kit intact").
- Version-1 and version-2 pad blobs load through `readPadPresetBlob`.
- Versions 0, 3, 4, current + 1 and 99 are rejected and leave the current kit byte-identical.
- No comment, test name or test text in the files above says "not shipped", "pre-release reset", version 1, 3 or 4 as current, 57 slots, 460 or 468 bytes.
- The 20 factory kits regenerate byte-identically. The commit message lists the slot-meaning check from step 2.1 and the user's ruling.

### T2.9 — Note-off policy for percussion (FR-114): a short MIDI note plays the full hit, pinned by a render comparison

**Findings:** MB-264 (medium, CONFIRMED).
**Flags:** PRESET-IMPACT.
**Prerequisites:** T1.10 (note-off and fast-release damping independent of sample rate and of retune convergence), T1.13 (auto-release independent of the host block size).
**Estimate:** 180 min.
**Ruling:** this task is in the ruling table with no default. Write the test first, then stop and ask before step 2.

Context. FR-114 (`specs/138-membrum-phase3-polyphony/spec.md:161`, contract `contracts/voicepool_api.md:66`) says a MIDI note-off is a no-op on a percussion voice: allocator bookkeeping only, the amp envelope is not forced into release. Since Phase 8A.5, `VoicePool::noteOff` (`plugins/membrum/src/voice_pool/voice_pool.cpp:283-300`) calls `DrumVoice::noteOff` (`plugins/membrum/src/dsp/drum_voice.h:442-452`), which releases the exciter, sends the tone shaper's note-off, gates the amp envelope off (300 ms release) and damps the modal bank (`kNoteOffDampScale`, `drum_voice.h:64`). `voice_pool.h:115-118` still documents the no-op. The FR-114 test, `"VoicePool: note-off is a no-op for percussion voices"` (`plugins/membrum/tests/unit/voice_pool/test_voice_pool_allocate.cpp:61-79`), asserts only the voice count, which `noteOff` never changes, so it cannot fail; the next case (`:81-107`) needs an explicit note-off to retire the voice. Measured: a note-off at 10 ms cuts the default kick from 0.97 s to 0.10 s and tom 41 from 1.54 s to 0.06 s (`specs/membrum-audit/probes/probe-results.txt:536-538`); a note-off at 100 ms cuts 180 of the 185 factory pads that ring 0.5 s or more to under half their decay (median ratio 0.21; `kit-pads.csv` columns `decayS` and `decayNoteOff100msS`); per block size in `noteoff-probe.csv`. Phase 1 pins the opposite: FR-013 and FR-038 (`specs/136-membrum-phase1-scaffold/spec.md:127`, `:165`) and their tests `"Membrum: Note-off triggers natural decay, not abrupt cut (FR-013)"` and `"Membrum: ADSR defaults -- release phase active after note-off (FR-038)"` (`plugins/membrum/tests/unit/processor/membrum_processor_tests.cpp:230-261`, `:339-387`; the second requires output below -60 dBFS 500 ms after a note-off).

The question for the user, with the numbers above: "FR-114 says a note-off does nothing to a drum voice; since Phase 8A.5 it gates and damps, which cuts 180 of 185 long factory pads to under half their decay. FR-013 / FR-038 (Phase 1) say a note-off starts the release. Which is wanted?"
- (a) FR-114: a MIDI note-off releases only a sustaining exciter (Friction, Feedback) and leaves the amp envelope, tone shaper and body to decay naturally. A short note plays the full hit.
- (b) Keep the Phase 8A.5 behaviour: amend FR-114 to say a note-off gates and damps, and pin that behaviour with the render test instead.
- A per-pad "note-off" option is a new parameter: it would add STATE-COMPAT and UI to this task, so stop and report rather than build it here.

Record the ruling in the spec rows first (FR-114, and FR-013 / FR-038 for (a)), then in the code comments and tests.

**1. Failing test**

New file `plugins/membrum/tests/unit/processor/test_note_off_policy.cpp` (add to CMake), a processor-level render comparison built on `Krate::Test::EventList` (`tests/test_helpers/vst_event_list.h`, `addNoteOn` / `addNoteOff` with sample offsets) and the setup steps of `MembrumTestFixture` (`membrum_processor_tests.cpp:64`). Default kit, 48 kHz.

`TEST_CASE("Note-off policy (FR-114): a short MIDI note plays the full hit", "[processor][note_off][fr114]")`. For pads 36 (kick, Impulse), 41 (tom, Mallet) and 49 (crash), render 2 s three ways at block 256: A note-on alone; B note-on with a note-off one block later; C note-on with a note-off at 125 ms. SECTION "same block": note-on at offset 0 and note-off at offset 10 of the same block, at block sizes 256 and 1024.
- Ruling (a): B, C and "same block" equal A: the RMS of the difference is at least 120 dB below the RMS of A over the 2 s. Fails today (kick decay 0.97 s vs 0.10 s).
- Ruling (b): decay60 of B and C is at most the documented fraction of A that the amended FR-114 states, identical at block 256 and 1024 within 5 %, and A is unchanged. Write this variant only after the ruling.

Replace the vacuous case at `test_voice_pool_allocate.cpp:61-79` with a pool-level twin of the same comparison (same name, same tags): note-on alone vs note-on plus `pool.noteOff(36)` one block later, rendered 2 s; for (a) the outputs match within -120 dB. Update `:81-107` to the ruling: under (a) the voice retires through auto-release with no note-off (assert retirement within the time the T1.13 auto-release gives for that pad); under (b) keep the explicit note-off and say why in its comment.

**2. Fix (after the ruling)**

Ruling (a):
1. `plugins/membrum/src/dsp/drum_voice.h`: add `void releaseExciter() noexcept { exciterBank_.release(); }` next to `noteOff` (`:442`). `DrumVoice::noteOff` stays as it is: the auto-release paths in both `VoicePool::processBlock` overloads (`voice_pool.cpp:380-384`, `:508-512`, or wherever T1.13 moved them) keep calling it, because auto-release is the pool retiring a silent voice, not a MIDI note-off.
2. `VoicePool::noteOff` (`voice_pool.cpp:283-300`): for every Active slot playing `midiNote`, call `releaseExciter()` instead of `noteOff()`. Keep the `allocator_.noteOff(midiNote)` bookkeeping. Rewrite its comment (`:285-290`) to FR-114, and leave `voice_pool.h:115-118` true.
3. Check what `exciterBank_.release()` does for each exciter type (`plugins/membrum/src/dsp/exciter_bank.h:60` and the exciters it dispatches to). For Impulse, Mallet, NoiseBurst, FMImpulse and Clap it must not change the output after the strike, or the -120 dB comparison fails; if one of them does (for example a release that cuts a still-running pulse), report it and stop. For Friction and Feedback a note-off still stops the excitation, which is what "Friction sustains without a note-off" in `plugins/membrum/CLAUDE.md` means; T6.10 owns whether Friction sustains at all.
4. Amend the Phase 1 rows: FR-013 and FR-038 (`specs/136-membrum-phase1-scaffold/spec.md:127`, `:165`) get a note that FR-114 supersedes them for percussion voices since Phase 3; then rewrite the two Phase 1 tests (`membrum_processor_tests.cpp:230-261`, `:339-387`) to the ruled behaviour: a note-off leaves the hit unchanged. This changes a requirement by ruling; it is not a relaxed threshold.

Ruling (b): no engine change. Amend FR-114 and `contracts/voicepool_api.md:66` to the Phase 8A.5 behaviour, rewrite `voice_pool.h:115-118`, and make the render test pin it.

Under (a), cases that relied on the note-off damp to bound a tail (the infinite-ring kit sweeps that send paired note-on / note-off, `plugins/membrum/tests/unit/processor/test_kit_switch_infinite_ring.cpp`, `NoteOnOffEvents` at `:726`) may go red because pads now ring their full decay. Read each red case: if it asserts that a voice retires, the auto-release (T1.13) must retire it, so a red case there is a real defect to report; if it asserts a tail shorter than the pad's natural decay, it encoded the old behaviour and changes with the ruling. Never raise a time bound to get green; stop and report the measured tail instead.

**3. Build and gate**

Standard workflow; build `Membrum` too, pluginval required. PRESET-IMPACT: run the kit-quality probe before and after and tabulate, for every pad whose `decayNoteOff100msS` moves, `peakDbfs`, `rms500Dbfs`, `decayS`, `decayNoteOff100msS`, `centroidHz` and `limiterMarginDb`. No kit data changes, so no regeneration. The table goes to the user with the ruling question; the audible change (full tails on short notes) is the purpose under (a), and the user still sees the table.

**4. Acceptance**

- Ruling (a): for kick 36, tom 41 and crash 49, note-off after one block, after 125 ms and in the same block (block 256 and 1024) all match the note-on-only render within -120 dB (baseline: kick decay60 0.97 s vs 0.10 s, tom 1.54 s vs 0.06 s). Kit probe: `decayNoteOff100msS` equals `decayS` for every pad without a Friction or Feedback exciter (baseline: 180 of 185 long pads under half, median ratio 0.21).
- Ruling (b): the render test pins the amended FR-114 at both block sizes, and every FR-114 text says the same thing.
- No test named for FR-114 asserts bookkeeping only.

### T2.10 — Master gain is ramped across the block

**Findings:** MB-059 (medium, CONFIRMED).
**Flags:** none.
**Prerequisites:** none.
**Estimate:** 60 min.

Context. `Processor::processParameterChanges` stores the master gain's last normalized point in the atomic `masterGainNorm_` (`kMasterGainId` case, `plugins/membrum/src/processor/processor.cpp:525-529`; atomic at `processor.h:156`). `Processor::process` computes one gain per block (`norm -> dB = -24 + 36 * norm -> pow(10, dB / 20)`) and multiplies every sample of the main bus by it, after the bus limiter (`processor.cpp:719-737`). Nothing holds the previous gain, so automation or a knob drag steps the level once per block, and the step grows with the host buffer (a 1 s full-range sweep: 0.42 dB per block at 512 samples, 1.67 dB at 2048). `applyKitSnapshot` stores the kit's master gain the same way (`:980-982`). T2.3 (MB-091) handles the step at a kit load: if T2.3 already added a smoothed master gain, reuse its members and only add what this task needs; never add a second smoother.

**1. Failing test**

New file `plugins/membrum/tests/unit/processor/test_master_gain_ramp.cpp` (add to CMake). `TEST_CASE("Master gain ramps instead of stepping at the block boundary", "[processor][master_gain]")`. Processor at 44.1 kHz, block 512, default kit, kick 36 at velocity 0.2 (below the limiter, as probe `level:master-gain` used, `probe-results.txt:41-43`). Render A: master norm 0.0 for blocks 0-3, then norm 1.0 sent through `Krate::Test::ParameterChanges` (`tests/test_helpers/vst_param_changes.h`) at block 4, then 4 more blocks. Render B: identical, norm 0.0 throughout. Because the gain is applied after the limiter, `r[n] = A[n] / B[n]` (for `|B[n]| > 1e-5`) is exactly the applied gain curve. Assert: `max r[n+1] / r[n] <= 1.01` across the change; `r` reaches `10^(36/20) = 63.1` within 0.1 % no later than 10 ms after the change and stays there. SECTION "block-size independent": repeat A and B at block 128 with the change delivered at the same sample (2048); the two `r` curves agree within 1e-4 relative. Today `r` jumps by x63.1 (+36 dB) in one sample.

**2. Fix**

1. `processor.h`: next to `masterGainNorm_` add audio-thread members for the applied gain: the current linear gain, a per-sample ratio and a remaining-sample count.
2. `Processor::process` (`processor.cpp:719-737`): compute the target gain from `masterGainNorm_` as today. When it differs from the last target, start a ramp of fixed length `kMasterGainRampSeconds = 0.010` (samples = round(0.010 x sample rate)) from the current gain to the new target, geometric (equal dB per sample: ratio = (target / current)^(1 / samples)), so a full-range change gives a constant per-sample ratio (1.0095 at 44.1 kHz, inside the 1.01 bound) instead of a large first step. The ramp continues across block boundaries; a new target mid-ramp restarts from the current value. When the count reaches 0, set the gain to the target exactly (a branch, not accumulated multiplication). Keep the existing skip of the multiply only when the current gain and the target are both exactly 1.0f.
3. `Processor::setupProcessing` and `setActive(true)`: snap the current gain to the target (no ramp from a stale value after activation or a sample-rate change). The kit load (`applyKitSnapshot`) only stores the norm, so it now ramps too.

**3. Build and gate**

Standard workflow; build `Membrum` too, pluginval required.

**4. Acceptance**

- Full-range change 0 -> 1 at 44.1 kHz: largest per-sample gain ratio at most 1.01 (baseline: x63.1 in one sample); the target is reached within 10 ms and held.
- The gain curve is identical at block 128 and 512 (1e-4).
- Steady-state master gain still measures -24.00 / -6.00 / +12.00 dB at norm 0 / 0.5 / 1 within 0.1 dB (probe `level:master-gain`, `probe-results.txt:41-43`).

### T2.11 — The String body is level-normalised to the body budget and follows velocity

**Findings:** MB-035 (medium, CONFIRMED), MB-175 (medium, CONFIRMED), MB-221 (low, CONFIRMED), MB-212 (low, CONFIRMED).
**Flags:** CROSS-PLUGIN IMPACT, PRESET-IMPACT.
**Prerequisites:** T1.6 (the strike-normalisation probe and its cache as they must work on the audio thread: key fields, sample-rate invalidation, no audio-thread render where T1.6 moved it off).
**Estimate:** 240 min.

Context. Every modal body is trimmed so a canonical hard strike peaks at `kBodyHeadroom` = 0.5 (-6 dBFS, `plugins/membrum/src/dsp/drum_voice.h:73-84`) by `measuredStrikeOutputGain` (`drum_voice.h:286-306`, cached on `StrikeNormKey`, `:1394-1412`, `:1717-1728`). The String body is exempt: its branch (`:277-285`) sets a gain on the shared modal bank that, as its own comment says, never reaches the audio path. `StringBody` (`plugins/membrum/src/dsp/bodies/string_body.h`) calls `string_.noteOn(r.frequencyHz, 1.0f)` (`:58`) whatever the hit velocity and returns `string_.process(excitation)` unscaled (`:62-89`). Inside `Krate::DSP::WaveguideString` (`dsp/include/krate/dsp/processors/waveguide_string.h`), `noteOn` (`:289`) writes a noise burst normalised to RMS 1 times `velocity * excitationGain_` (`:400`, `:409`, burst write at the end of `noteOn`), with `excitationGain_ = (261.6 / f0)^0.275 / gTotal`, about 1.4 at 80 Hz; the junction `feedback + excitation` then goes through `softClip` (threshold `kSoftClipThreshold = 1.0`, `:46`, `:194-197`, `:588-593`) inside the loop. Measured: String body-only peaks at -0.02 dBFS while Membrane, Plate, Shell, Bell and NoiseBody read -5.94 to -6.67 (`probe-results.txt:34-39`); the in-loop clip removes about 5 dB at 80 Hz and engages on every hit (MB-212: 14.85 dB instead of 20 dB between velocity 1.0 and 0.1 at 80 Hz); velocity reaches the String only through the small exciter term. 30 factory pads use the String body (`kit-pads.csv`, body `String`; Chaos Engine 51 and Drone and Sustain 36 already sit at the -7.00 dBFS limiter ceiling).

CROSS-PLUGIN IMPACT: the clipping mechanism is in `waveguide_string.h`, which Innexus (`plugins/innexus/src/processor/innexus_voice.h`) and Seraphis (through `dsp/include/krate/dsp/systems/continuous_body.h`) also use. The fix below keeps that header unchanged: Membrum scales what it feeds into the loop, which the loop's linearity makes exact. Run the consumer grep anyway and list the consumers in the commit message with "header unchanged". If the implementation turns out to need a header change (for example an accessor), every CROSS-PLUGIN obligation applies and the change must be opt-in so Innexus and Seraphis render bit-identically.

**1. Failing test**

- In `plugins/membrum/tests/unit/dsp/test_body_headroom_budget.cpp` (today it covers Membrane and Bell, `:81`): `TEST_CASE("N-1: every body, String included, strikes at the -6 dBFS budget", "[body_headroom][string]")`. For each of the six `BodyModelType`s: a `DrumVoice` at 48 kHz, Impulse exciter, velocity 1.0, level 1.0, noise-layer and click-layer mix 0, configured through `applyPadConfigToVoice` + `noteOn` as audit probe 07a does (`specs/membrum-audit/probes/audit_probes.cpp.txt:1247-1261`). Assert the body-only peak is within 1 dB of -6.02 dBFS. Baseline: String -0.02 dBFS; the others -5.94 to -6.67, already inside.
- In `plugins/membrum/tests/unit/bodies/test_string_body.cpp`: `TEST_CASE("String body: velocity scales the string and the loop stays linear", "[string_body][velocity]")`. Two `StringBody` instances prepared identically (same sample rate and voice id, so the burst noise is identical), Size 1 (80 Hz, the worst case), driven by the same external excitation buffer; one configured at velocity 1.0, the other at velocity 0.01 with the excitation scaled by 0.01. The first 100 ms of the velocity-1 output must equal 100 x the velocity-0.01 output within -40 dB (difference RMS relative to the signal RMS): that holds only if velocity reaches the burst and the in-loop clip never engages. Add SECTIONs at Size 0.5 and 0. Fails today: the burst ignores velocity, and at velocity 1 the clip compresses about 5 dB.

**2. Fix**

1. Velocity into the string. `DrumVoice::noteOn(float velocity)` (`drum_voice.h:170`) calls `bodyBank_.configureForNoteOn(params_, initialPitchHz)` (`:244`); `BodyBank::configureForNoteOn` (`plugins/membrum/src/dsp/body_bank.h:58-66`) visits the active body. Hand the clamped note velocity to the String alternative only (a setter on `StringBody` reached through a `BodyBank` forwarding call, made before `configureForNoteOn`), and in `StringBody::configureForNoteOn` call `string_.noteOn(r.frequencyHz, velocity * kStringLoopHeadroom)`. Update the header comment at `string_body.h:13-15` ("Phase 2 uses a fixed velocity=1.0").
2. Loop headroom. Add `static constexpr float kStringLoopHeadroom` to `StringBody` and multiply the external excitation by it before `string_.process` in `processSample`, `processSampleNoSmooth` and `processBlock`. Choose the value so that a velocity-1 Impulse strike at Size 1 (80 Hz) keeps the junction below 1.0: start from MB-212's suggestion of 0.25 and confirm it with the linearity test at Size 1, which fails if the clip engages. The in-loop clip stays as a safety rail only.
3. Output normalisation. Add a `float outputGain_` to `StringBody`, multiplied into the value all three render functions return. Replace the no-op String branch in `drum_voice.h:277-285` with a measured strike gain for String: through the probe mechanism T1.6 left (find `measuredStrikeOutputGain` / `StrikeNormKey` or their successors), measure the String's peak for a canonical velocity-1 strike over the same window as the modal probe, and set `outputGain_ = kBodyHeadroom / peak`, with the same near-silent fallback as `measuredStrikeOutputGainExcited` (`:1380-1387`). The cache key for String holds `BodyModelType::String`, the exciter type, every input `Bodies::StringMapper::map` reads (`plugins/membrum/src/dsp/bodies/string_mapper.h`), the click-layer fields already in the key and the sample rate (as T1.6 keys it). The probe must leave the live string as `noteOn` leaves it: run it before the real `string_.noteOn`, or on a copy if T1.6's mechanism requires one; if the probe advances the waveguide's noise generator, say so in the commit message (MB-025 owns noise seeding). Keep `bank.setOutputSoftClipThreshold(0.0f)` for String as today.
4. Comments: `drum_voice.h:79-80` ("the per-voice hardClip rail stays disengaged") is now true for every body; rewrite the String-branch comment to describe the new normalisation.

**3. Build and gate**

Standard workflow; build `Membrum` too, pluginval required. CROSS-PLUGIN: run `grep -rl "waveguide_string.h" plugins/ dsp/include tests/` and list the consumers in the commit message; with the header unchanged, `node tools/header-consumers.js --tag '[membrum]'` names no executable beyond `membrum_tests`. If you changed the header: build and gate every executable it names (`dsp_processors_tests`, `dsp_systems_tests`, `innexus_tests`, `seraphis_tests`), add `-Target dsp` to clang-tidy, and prove Innexus and Seraphis renders unchanged. PRESET-IMPACT: kit-quality probe before and after; tabulate the 30 String pads (`peakDbfs`, `rms500Dbfs`, `decayS`, `centroidHz`, `limiterMarginDb`) and any near-duplicate pair that appears or disappears in `kit-pairs.csv`. String pads will move by up to about -6 dB in peak (the purpose of the fix); show the table to the user and stop for the ruling before the commit. No kit data is retuned here (kit-data gate).

**4. Acceptance**

- String body-only peak at velocity 1, level 1: within 1 dB of -6.02 dBFS (baseline -0.02 dBFS); the other five bodies stay where they are (-5.94 to -6.67).
- Velocity 1 vs 0.01 at Size 1, 0.5 and 0: the outputs scale by exactly 100 within -40 dB, so velocity reaches the string and the loop clip never engages (baseline at 80 Hz: 14.85 dB instead of 20 dB between velocity 1.0 and 0.1).
- The String peak at velocity 0.24 is at least 6 dB below velocity 1.0 before the amp envelope (MB-035).
- Chaos Engine 51 and Drone and Sustain 36 no longer read -7.00 dBFS at the limiter ceiling in the kit probe, or the table says why.

### T2.12 — Tension-modulation depth does not depend on the sample rate

**Findings:** MB-033 (medium, CONFIRMED), MB-171 (medium, CONFIRMED).
**Flags:** PRESET-IMPACT, added: at 48 kHz, the rate of every kit probe, every pad with `tensionModAmt > 0` glides about 16 % less after the fix (the energy scale changes by (44.1 / 48)^2 = 0.844); the default kick (0.8) and toms (0.25) and the Tabla bayan pads (0.78-0.85) are among them. At 44.1 kHz renders stay bit-identical.
**Prerequisites:** none. (T1.11 lands first in plan order and removes the block-rate pitch-envelope error that makes the tension-0 control vary with the rate; the processor-level check below relies on it.)
**Estimate:** 90 min.

Context. The Membrane tension glide is `pitchMod = min(1 + tensionAmtEffective_ * energyEnv_, kTensionPitchModMax)` (fast path `plugins/membrum/src/dsp/drum_voice.h:722-728`, slow path `:944-950`), with `tensionAmtEffective_ = tensionModAmt * kTensionEnergyScale` (`:420-421`; constant `3.3e-5f` at `:86-94`, calibrated on a "hard tom hit peaks ~2.4e3" reading at 44.1 kHz, the rate of the calibration test `"Phase 8E: Tom tension modulation drifts pitch vs velocity"`, `plugins/membrum/tests/unit/dsp/test_per_mode_damping.cpp:481`). `energyEnv_` follows `ModalResonatorBank::getModalEnergy()` (`:910-914`, slow path `:1072-1077`), the raw sum of `sin^2 + cos^2` over the mode states, taken before the output gain. The bank adds `gain * excitation` every sample with no `1/sr` factor and the exciters' pulses last a fixed time, so mode amplitudes grow with the sample rate and the energy with its square. Computed (MB-033): a hard tom glides 0.34 / 1.32 / 3.0 / 3.0 semitones at 22.05 / 44.1 / 96 / 192 kHz (the last two pinned at the 1.19 ceiling). Measured (MB-171, `probe-results.txt:428-429`): the default kick reads 73.43 / 73.51 / 74.23 / 74.56 Hz at 44.1 / 48 / 96 / 192 kHz (+24.4 cents); with tension 0 it reads 72.67 / 72.62 / 72.38 / 72.24 Hz, so tension's own share grows from about +18 to +55 cents. The 18 % decay change in MB-171 is not this mechanism: it persists with tension 0 (`:430-431`); this task only requires that tension's effect on decay is rate-independent.

**1. Failing test**

- In `plugins/membrum/tests/unit/dsp/test_per_mode_damping.cpp`, next to `:481`: `TEST_CASE("Phase 8E: tension glide depth does not depend on the sample rate", "[phase8e][drum_voice][tension][sample_rate]")`. Reuse the `:481` setup and its measurement (peak `getModeFrequency(0)` over the first 200 ms, against a velocity-0.02 reference at the same rate) at 22.05, 44.1, 96 and 192 kHz, with the block length scaled to the rate (256 samples at 44.1 kHz). Assert: at every rate the velocity-1 glide is within 20 % of the 44.1 kHz glide, and inside the existing 1.0 to 2.5 semitone band. Fails today (0.34 / 1.32 / 3.0 / 3.0 semitones).
- New processor-level case in `plugins/membrum/tests/unit/processor/membrum_processor_tests.cpp` beside the sample-rate cases (`:1040-1100`): `TEST_CASE("Membrum: default kick pitch does not depend on the sample rate", "[membrum][processor][sample_rate]")`. Default kit, kick 36 at velocity 1.0, block 512, at 44.1 / 48 / 96 / 192 kHz, tension as in the kit and again with the pad's tension at 0. Assert: the fundamental spread across rates with tension on is at most 5 cents (baseline 24.4 cents); the decay60 ratio (tension on / tension 0) agrees across the four rates within 10 %. If the tension-0 control itself spreads by more than 5 cents, T1.11's fix is not in effect: stop and report, do not change this task's thresholds.

**2. Fix**

`plugins/membrum/src/dsp/drum_voice.h`:
1. Add `static constexpr float kTensionCalibrationRate = 44100.0f;` next to `kTensionEnergyScale` (`:94`), with a comment: the scale was calibrated at 44.1 kHz, and modal energy scales with the square of the sample rate because the bank's per-sample injection is not rate-normalised.
2. In `prepare` (beside `energyAlpha_`, `:130-132`) compute `energyRateNorm_ = (kTensionCalibrationRate / sr)^2` as a new member. At 44.1 kHz the division is exactly 1.0, so 44.1 kHz renders stay bit-identical.
3. Multiply the energy reading by `energyRateNorm_` at both reads: `const float e = bodyBank_.getSharedBank().getModalEnergy() * energyRateNorm_;` (fast path `:912`, slow path `:1075`).
4. Wire coupling reads the same `energyEnv_` but divides it by its own running peak (`computeWireGainMod`, `:1501-1512`), so a constant factor cancels; its only absolute constant is the `1e-12` floor on the peak. Confirm the wire-coupling cases (`plugins/membrum/tests/unit/dsp/test_wire_coupling.cpp`) stay green.

The physical model of the glide (follower shape, energy ripple, ceiling) is T4.14 and T4.23; change nothing else here.

**3. Build and gate**

Standard workflow; build `Membrum` too, pluginval required. PRESET-IMPACT: kit-quality probe (48 kHz) before and after; tabulate every pad with `tensionModAmt > 0` (`peakDbfs`, `rms500Dbfs`, `decayS`, `centroidHz`, `limiterMarginDb`) and stop for the user's ruling if any change is audible.

**4. Acceptance**

- Tom glide at 22.05 / 44.1 / 96 / 192 kHz within 20 % of the 44.1 kHz value and inside 1.0 to 2.5 semitones (baseline 0.34 / 1.32 / 3.0 / 3.0).
- Default kick fundamental spread across 44.1 / 48 / 96 / 192 kHz at most 5 cents (baseline 24.4 cents, `probe-results.txt:445`); tension's effect on decay60 the same at every rate within 10 %.
- `"Phase 8E: Tom tension modulation drifts pitch vs velocity"` (44.1 kHz) unchanged and green; 44.1 kHz renders bit-identical to before.

### T2.13 — Noise-based exciters, the click layer and the noise layer keep their level across sample rates

**Findings:** MB-024 (medium, CONFIRMED), MB-172 (low, CONFIRMED).

**Flags:** none. PRESET-IMPACT was considered and not added. Every factor below is exactly 1 at 48 kHz, and the branch in step 2 enforces that. 48 kHz is the rate the kits were measured at (the kit-quality probe's `kSR = 48000.0`) and the rate the level constants were calibrated at (`gain_staging_balance_test.cpp`, `kSr = 48000.0`). So at 48 kHz every kit is bit-identical. At other rates the level change is the purpose of the findings. Step 4 confirms the 48 kHz kit numbers do not move.

**Prerequisites:** none.

**Estimate:** 120 min.

**Background (why levels move with the rate).** Each source below makes per-sample noise of fixed variance and filters it at a centre or cutoff fixed in Hz. A band fixed in Hz holds a fraction BW/(sr/2) of white-noise power, so white noise loses amplitude as sqrt(1/sr). Violet noise is `0.5 * (w[n] - w[n-1])` (`dsp/include/krate/dsp/primitives/noise_oscillator.h`, `processViolet`), with |H| = sin(pi f / sr), so it also loses sin(pi f / sr) on top. The Feedback exciter's seed impulse lasts a fixed 2 samples, so its area falls as 1/sr. The body is renormalised per rate by `DrumVoice::probeStrikePeakExcited` (`drum_voice.h:1322`). The click's direct output path and the noise layer are not renormalised.

**1. Failing test**

- New file `plugins/membrum/tests/unit/dsp/test_layer_level_sample_rate.cpp`. Add it to the `membrum_tests` source list in `plugins/membrum/tests/CMakeLists.txt`.
- `TEST_CASE("Sample-rate invariance: noise exciters and the click keep their energy across 44.1/96/192 kHz", "[dsp][exciter][click_layer][sample_rate]")`:
  - For `NoiseBurstExciter`, `ClapExciter` and `FeedbackExciter` (with `bodyFeedback = 0`), at velocity 1.0 and sample rates 44100, 96000 and 192000, use `membrum_exciter_tests::runBurst` from `plugins/membrum/tests/unit/exciters/exciter_test_helpers.h` to render 0.5 s.
  - Compute E = sum(x^2) / sr and the peak.
  - REQUIRE that E at each rate is within 0.5 dB of E at 48 kHz (`10*log10` ratio), and the same for the peak.
  - For `ClickLayer` (`mix 1`, `contactMs 0.5`, `brightness 0.5`, velocity 1), scale by `clickLayer.standaloneGain()` (added in step 2). For the failing run on unfixed code, use `ClickLayer::kStandaloneOutputGain`. REQUIRE its peak within 0.5 dB of the 48 kHz peak at each rate.
- `TEST_CASE("Sample-rate invariance: the noise layer keeps its level across 44.1/96/192 kHz for every colour", "[dsp][noise_layer][sample_rate]")`:
  - Use `NoiseLayer` with `setFilterMode(Krate::DSP::SVFMode::Lowpass)` (the DrumVoice path). Set `mix 1`, `resonance 0.3`, `decay 0.5`, colours {0.1, 0.4, 0.7, 0.95} (Brown, Pink, White, Violet) and cutoffs {0.5, 0.88}.
  - Take the mean over voiceIds 0..15 of the peak of `processBlock` output x `layer.standaloneGain()`.
  - REQUIRE each rate within 1 dB of 48 kHz.
- `TEST_CASE("Sample-rate invariance: default-kit crash RMS(1 s) at 48 vs 192 kHz", "[dsp][noise_layer][sample_rate]")`:
  - Fill a `std::array<PadConfig, kNumPads>` with `Membrum::DefaultKit::apply` (`plugins/membrum/src/dsp/default_kit.h:398`).
  - Prepare a `DrumVoice` at 48000 and at 192000, call `applyPadConfigToVoice(voice, pads[13])` (`dsp/pad_config_apply.h`; pad 13 = MIDI 49, the crash), `noteOn(1.0f)`, and render 1 s with `processBlock` in 512-sample blocks.
  - REQUIRE |RMS_48k - RMS_192k| <= 1 dB.
- Build, run each case by name, and confirm they fail:
  - NoiseBurst E falls about 10 dB at 96 kHz.
  - The click peak is about 3.4 dB low at 96 kHz.
  - The crash differs by about 4.5 dB.

**2. Fix**

- **New shared constant and helpers.** Create `plugins/membrum/src/dsp/level_calibration.h` (run the ODR grep for the names first) with:
  - `inline constexpr double kLevelCalibrationSampleRate = 48000.0;`
  - `[[nodiscard]] inline float whiteNoiseRateGain(double sr) noexcept`, returning exactly `1.0f` when `sr == kLevelCalibrationSampleRate` (a branch, never arithmetic: MACOS rule) and `static_cast<float>(std::sqrt(sr / kLevelCalibrationSampleRate))` otherwise.
  - `[[nodiscard]] inline float violetBandRateGain(double sr, float centreHz) noexcept`, returning `1.0f` at the calibration rate and otherwise `whiteNoiseRateGain(sr) * sin(pi * centreHz / 48000) / sin(pi * centreHz / sr)`. Clamp `centreHz` below `0.49 * sr` before the `sin`.
- **NoiseBurst** (`plugins/membrum/src/dsp/exciters/noise_burst_exciter.h`, `trigger()` :87-113). After the cutoff is computed at :105, multiply `amplitude_` (:110) by `violetBandRateGain(sampleRate_, cutoff)`.
- **Clap** (`plugins/membrum/src/dsp/exciters/clap_exciter.h`, `trigger()` :84-119). Multiply `amplitude_` (:108) by `whiteNoiseRateGain(sampleRate_)`.
- **Feedback** (`plugins/membrum/src/dsp/exciters/feedback_exciter.h`):
  - In `prepare()` (:49), cache `seedRateGain_ = whiteNoiseRateGain(sampleRate)`, and apply it to the seed-noise term at :161.
  - Replace `impulseSamplesRemaining_ = 2;` (:124) with a length in time:
    - `const double exact = 2.0 * sampleRate_ / kLevelCalibrationSampleRate;`
    - `impulseSamplesRemaining_ = std::max(1, (int)std::lround(exact));`
    - Store `impulseScale_ = (float)(exact / impulseSamplesRemaining_)` so the impulse keeps its area. This gives exactly 2 samples and a scale of 1 at 48 kHz.
  - At :163-167, add `amplitudeEnv_ * impulseScale_` instead of `amplitudeEnv_`.
- **ClickLayer** (`plugins/membrum/src/dsp/click_layer.h`):
  - Keep `kStandaloneOutputGain` (:157) as the 48 kHz calibration.
  - Add a member `standaloneGain_` set in `prepare()` (:41) to `kStandaloneOutputGain * whiteNoiseRateGain(sampleRate)`, and the accessor `[[nodiscard]] float standaloneGain() const noexcept`.
  - In `plugins/membrum/src/dsp/drum_voice.h`, replace the four uses of `ClickLayer::kStandaloneOutputGain` with `clickLayer_.standaloneGain()`. They are in `process()` :532, the block renderer :789, the slow renderer :1027 and `probeStrikePeakExcited` :1347; grep for `ClickLayer::kStandaloneOutputGain` to find them. T2.14 builds on this accessor.
- **NoiseLayer** (`plugins/membrum/src/dsp/noise_layer.h`):
  - Remove `[[maybe_unused]]` from `sampleRate_` (:324).
  - Store the decoded colour in a new member `colour_` in `configure()` (:81).
  - Make `computeStandaloneGain` (:270-276) a non-static member that multiplies the cutoff law by a per-colour rate factor `r = sr / 48000`. The analytic in-band amplitude law for a fixed-Hz lowpass gives:
    - White: `whiteNoiseRateGain`.
    - Violet: r^1.5, written as `r * sqrt(r)`, not `pow`.
    - Pink (Kellet filter, roughly 1/f in normalised frequency): 1.
    - Brown (leaky integrator with a fixed per-sample leak 0.99, about 1/f^2): r^-0.5.
  - Every factor returns exactly 1.0f at 48 kHz by the same branch.
  - `configureRaw()` (:100-115) uses the stored `colour_`.
  - The second test case decides. If Pink or Brown misses the 1 dB bound, replace that colour's exponent with the measured one, and write the measured exponent and how it was measured in a comment. T2.14 later replaces these factors with a band-energy calibration that must keep this test green.
- Do not change `noise_oscillator.h` or `svf.h`.

**3. Build and gate:** standard workflow. No shared file changes.

**4. Acceptance**

- All three new cases are green. The existing `gain_staging_balance_test.cpp` cases stay green unchanged, because they run at 48 kHz.
- Re-run the audit probe `srate:crash49` (`probe-results.csv:437`): the RMS(1 s) deviation from 48 kHz falls from the baseline 4.55 dB to <= 1 dB. `srate:kick36` (0.31 dB) and `srate:snare38` (0.62 dB) stay <= 1 dB.
- MB-024 probe baselines to beat (E = sum x^2 / sr at 44.1 / 96 / 192 kHz):
  - NoiseBurst 9.544e-05 / 9.718e-06 / 1.884e-06.
  - Clap 1.035e-04 / 4.158e-05 / 2.396e-05.
  - Feedback peak 0.8467 / 0.6416 / 0.3775.
  - After the fix, each is within 0.5 dB across rates.
- Run the kit-quality probe at 48 kHz before and after: every `kit-pads.csv` number is identical. Put the "unchanged at 48 kHz" statement in the commit message.

### T2.14 — Noise-layer and click level are calibrated per colour, cutoff and Q; prior-audit items L-6 and L-10 closed

**Findings:** MB-046 (medium, CONFIRMED), MB-230 (medium, PLAUSIBLE), MB-155 (low, CONFIRMED).

**Flags:** PRESET-IMPACT, VERIFY-FIRST.
- PRESET-IMPACT: violet-noise pads (default-kit hats at colour 0.95, the crash at 0.9) and bright or dark click pads change level by several dB.
- VERIFY-FIRST applies to MB-230 only. One verifier refuted its physics framing: the -18 dBFS target is a documented gain-staging choice (audit H-2), not a physical ratio. Its engineering claim, that the calibration holds only at one point, is the same mechanism as MB-046. The grid test below decides MB-230: if the grid passes on current code, MB-230 is refuted.

**Prerequisites:** T2.13. This task replaces T2.13's per-colour rate factors and must keep T2.13's tests green.

**Estimate:** 240 min.

**1. Failing test**

- Extend `plugins/membrum/tests/unit/dsp/gain_staging_balance_test.cpp`, using the file's `blockPeak` and `dbfs` helpers and `kSr = 48000`.
- `TEST_CASE("Gain staging: noise layer level is independent of colour, cutoff and resonance", "[membrum][dsp][gain_staging]")`:
  - Use `NoiseLayer` with `setFilterMode(SVFMode::Lowpass)`, `mix 1` and `decay 0.5`.
  - Grid: colour {0.1, 0.4, 0.7, 0.95} x cutoff {0.1, 0.3, 0.5, 0.8, 0.88} x resonance {0.0, 0.3, 1.0}.
  - Effective peak = mean over voiceIds 0..15 of `blockPeak(out) * layer.standaloneGain()`.
  - REQUIRE every grid point at -18 dBFS +/- 3 dB.
- `TEST_CASE("Gain staging: click layer level is independent of brightness", "[membrum][dsp][gain_staging]")`:
  - Use `ClickLayer` with `mix 1` and `contactMs 0.5`, brightness {0.1, 0.5, 0.9}, velocity 1.
  - Effective peak = mean over voiceIds 0..15 of `blockPeak * clickLayer.standaloneGain()`.
  - REQUIRE -18 dBFS +/- 3 dB.
- `TEST_CASE("Audit L-10: NoiseBurst Duration proxy default matches the per-pad default", "[membrum][controller][audit][defaults]")` in `plugins/membrum/tests/unit/controller/test_audit_ui_controller_batch.cpp`, following the pattern of "Audit #10" at :110-121: `getParameterObject(kExciterNoiseBurstDurationId)->getInfo().defaultNormalizedValue == Approx(0.5).margin(1e-12)`.
- Expected failures on current code: violet at cutoff 0.5 about -45 dBFS and brown at 0.88 about -31.7 dBFS (MB-230 layer_probe), and a proxy default of 0.230769. If the noise grid passes, record MB-230 as refuted (VERIFY-FIRST) and continue with the rest.

**2. Fix**

- **Noise-layer gain from the coloured band energy** (`plugins/membrum/src/dsp/noise_layer.h`):
  - In `prepare()` (:39), build a per-instance table `E[colour 4][cutoff norm 33][resonance norm 9]`, with cutoff and resonance each spanning 0..1 in equal steps. It holds the output energy of the layer's own noise source through its own lowpass at the current sample rate.
  - Fill each entry by summing over 2048 bins in (0, sr/2): S_colour(w) x |H_LP(w)|^2.
    - S_colour comes from the recurrences as coded in `noise_oscillator.h`: White 1; Violet |0.5(1 - e^{-jw})|^2; Brown from the leaky integrator in `processBrown` (leak `kLeak = 0.99f` at :319, and its output gain); Pink from the `PinkNoiseFilter` (Kellet) coefficients.
    - H_LP is the TPT SVF lowpass of `svf.h` (g = tan(pi fc / sr) as in `computeG` :489, k = 1/Q). fc = `denormCutoff(cutoffNorm)` and Q = `denormResonance(resNorm)`.
  - `E_ref` is the same sum for Pink, cutoff norm 0.5 and resonance norm 0.3 at 48 kHz. That is the current calibration point of the existing test at :38-63, so `kStandaloneOutputGain = 0.243f` (:321) keeps its meaning. Compute it once in `prepare()`.
  - `computeStandaloneGain` becomes `kStandaloneOutputGain * sqrt(E_ref / E_interp)`, where `E_interp` is bilinear in (cutoff norm, resonance norm) in log energy for the stored `colour_`. It is evaluated in `configure()` (:87), which runs at note-on on the audio thread: O(1), with no allocation, no `std::exp` loop and no table build there.
  - Replace the clamp `0.25..4` (:273-274) with a floor on `E_interp` (no divide by about 0) and a gain cap large enough for the grid. Violet at cutoff 0.1 needs about +57 dB analytically; write the chosen cap and its reason in the comment.
  - Because the energy is computed at the current sample rate against a fixed 48 kHz reference, this replaces T2.13's per-colour rate factor: delete that code.
  - `configureRaw()` (:100-115): convert its Hz and Q back to norms with the inverse of `denormCutoff` and `denormResonance`, and use the same lookup. NoiseBody does not read this gain, but it must stay meaningful.
  - Bloom (`configureBloom`) moves the cutoff after `configure()`. The gain stays the one for the base cutoff; say so in the `configureBloom` comment.
- **Click brightness compensation** (`plugins/membrum/src/dsp/click_layer.h`). In `configure()` (:67-76), set `standaloneGain_ = kStandaloneOutputGain * whiteNoiseRateGain(sampleRate_) * sqrt(denormBrightness(0.5f) / cutoffHz)`. A constant-Q bandpass passes white-noise power in proportion to its centre frequency, and the reference is brightness 0.5, the current calibration point at :66-90.
- **Comments.**
  - Correct the colour thresholds in `noise_layer.h:33` to 0 = Brown, < 0.25 Brown, < 0.55 Pink, < 0.80 White, else Violet.
  - Correct the "Lowpass has no spectral peak" rationale at `noise_layer.h:66-70` and `drum_voice.h:137-146`: the lowpass peaks above Q 0.707 (resonance norm above about 0.087), and the level is calibrated to stay constant across Q.
  - In `noise_layer.h:220-228`, `:312-321` and `click_layer.h:150-157`, state that the -18 dBFS target is a gain-staging design choice (audit H-2), not a physical body-to-noise ratio (MB-230).
- **L-6 resolution.** Do not cap the noise-layer Q. Two verifiers showed that resonant noise is used on purpose: the clap formant at Q 2.18 and the Tonal "wah" buzz. The defect was the false comment and the level change with Q, and both are fixed above.
- **L-10.**
  - In `plugins/membrum/src/controller/controller.cpp`, `kPhase2Specs`, change the `kExciterNoiseBurstDurationId` entry's `.defaultValue = 0.230769` (:336) to `0.5`, the per-pad default at :202, `pad_config.h:236` and `processor.cpp:77`.
  - Correct `plugin_ids.h:65` "default 5" to "default 8.5".
- **Tracker.** In `plugins/membrum/AUDIT-signal-path-2026-06-07.md`, append to the end of the existing L-6 line (:298) and L-10 line (:302) a status in the style the L-4 line uses ("✅ DONE (T2.14, membrum audit 2026-11-23): ..."). Append only; never reword the history.

**3. Build and gate:** standard workflow. Then follow the PRESET-IMPACT procedure: run the kit-quality probe before (on T2.13's commit) and after.

**4. Acceptance**

- The new noise grid, click grid and L-10 cases are green. The three existing gain-staging cases and T2.13's sample-rate cases stay green.
- Baselines to beat:
  - MB-046 probe test 9 (noise part, mix 1, cutoff 0.5): Brown -23.0, Pink -20.6, White -24.7, Violet -48.5 dBFS peak. After the fix, all are within 3 dB of each other.
  - MB-230 layer_probe at cutoff 0.88: brown -31.7, pink -24.6, white -20.5, violet -27.4 dBFS.
- Kit-quality probe table (peak, rms500, decay, centroid, limiter margin per affected pad, and any near-duplicate pair change in `kit-pairs.csv`). Expect the default-kit closed hat (pad 6, colour 0.95) noise part to rise from its baseline of -63.1 dBFS RMS over 500 ms, and the clap and cymbal pads to move.
- This is an audible kit change. Show the table and stop for the user's ruling before the commit. Do not compensate in kit data: the kits are retuned in T5.6 to T5.13.

### T2.15 — Tone-shaper bypass is decided on the normalised cutoff and applies to the lowpass type only

**Findings:** MB-044 (medium, CONFIRMED), MB-013 (low, CONFIRMED).

**Flags:** MACOS.

PRESET-IMPACT was checked and not added. Every Highpass or Bandpass pad in `tools/membrum_preset_generator.cpp` uses a cutoff of 0.72 or lower: `filtCyc` at :1517/:1537, `hpc[]` at :5898/:6121, `cut[]` at :5972, :6050, :6512 and :6567, and the literal pads at :747, :1258, :1587, :1611, :1710, :2707, :2822, :3703, :6266, :6299, :6396 and :6456. So no kit pad sits on the HP/BP bypass. On Windows and Linux the Lowpass at norm 1.0 keeps its bypass.

**Prerequisites:** none.

**Estimate:** 90 min.

**1. Failing test**

- In `plugins/membrum/tests/unit/tone_shaper/test_tone_shaper_bypass.cpp` (`kSampleRate = 44100`), add `TEST_CASE("ToneShaperBypass: only the Lowpass at the top of the cutoff knob is bypassed", "[tone_shaper][bypass]")`.
- **HP and BP section.**
  - For `ToneShaperFilterType::Highpass` and `Bandpass`, prepare a `ToneShaper`, set the type, and set the cutoff once to `20.0f * std::pow(1000.0f, 0.999f)` and once to `20000.0f`.
  - Process 8192 samples of the same seeded white noise (`Krate::DSP::XorShift32`) and compute the RMS of each run.
  - REQUIRE |RMS(0.999) - RMS(1.0)| <= 1 dB.
- **Lowpass section.** With `Lowpass` and cutoff `20000.0f`, the output is bit-identical to the input and `isBypassed()` is true.
- **Default-pad section (MB-013).**
  - Use a `DrumVoice` prepared at 44100, then `applyPadConfigToVoice(voice, PadConfig{})` from `dsp/pad_config_apply.h`.
  - REQUIRE `voice.toneShaper().getFilterCutoff() == 20000.0f` exactly and `voice.toneShaper().isBypassed()`.
- Expected results on current code:
  - The HP/BP section fails: probe test 8 measured -36.3 dBFS at norm 0.999 against -16.5 dBFS at norm 1.0, a 19.8 dB jump.
  - The default-pad section passes under MSVC, where pow lands on 20000. It fails only where fast-math rewrites `pow(1000, x)` as `exp2(x * log2 1000)`: 19999.998.
  - Before the fix, run `tools/clangcl-fastmath-build.ps1 -Targets membrum_tests` and that case by name. Record whether the proxy reproduces the failure. If it does not, the commit message says "MB-013 unverified on macOS until CI".

**2. Fix**

- `plugins/membrum/src/dsp/pad_config_apply.h:83-84` (`applyPadConfigToVoice`, Tone Shaper block). Decide the endpoint by a branch on the normalised value:

  ```cpp
  const float n = std::clamp(cfg.tsFilterCutoff, 0.0f, 1.0f);
  v.toneShaper().setFilterCutoff(n >= 1.0f ? 20000.0f : 20.0f * std::pow(1000.0f, n));
  ```

  The mapping is unchanged at every other value, so no preset migration is needed (see the file's header comment :14-16).
- `plugins/membrum/src/dsp/tone_shaper.h`, `processSample()` :427-428. Add `filterType_ == ToneShaperFilterType::Lowpass` to `filterBypass`. Do the same in `isBypassed()` :467-474. Highpass and Bandpass now always run the SVF; at 20 kHz the SVF's own clamp (`svf.h`) bounds the cutoff.
- Update the comment at :426 to say the bypass is the Lowpass-at-maximum identity of FR-045.

**3. Build and gate:** standard workflow. Then the MACOS obligations: `node tools/lint-finite-bitmask.js`, `node tools/lint-nonfinite-symbols.js`, `node tools/lint-ieee-flag-sets.js`, `node tools/check-portability.js plugins/membrum/tests/unit/tone_shaper/test_tone_shaper_bypass.cpp` (the only changed `.cpp`), and `tools/clangcl-fastmath-build.ps1 -Targets membrum_tests` with the new case.

**4. Acceptance**

- The new case is green.
- HP and BP at norm 0.999 vs 1.0 differ by at most 1 dB (baseline 19.8 dB).
- The Lowpass at 1.0 is bit-identical to bypass.
- A default pad is bypassed with a cutoff of exactly 20000 Hz.
- The `[tone_shaper]` suite and the Phase 1 golden (`approval/test_phase1_regression.cpp`) stay green with no re-baseline.

### T2.16 — ModeInject is scaled to the body budget

**Findings:** MB-045 (medium, CONFIRMED), MB-177 (low, CONFIRMED).

**Flags:** PRESET-IMPACT. The generator and the default kit set `modeInjectAmount` above 0 on many pads (65 assignments in `tools/membrum_preset_generator.cpp`, 1 in `dsp/default_kit.h`).

**Prerequisites:** none.

**Estimate:** 120 min.

**Background.**
- `ModeInject::prepare()` (`plugins/membrum/src/dsp/unnatural/mode_inject.h:54-67`) sets 8 partials at amplitude 1/k, which sum to 2.718.
- `trigger()` (:126) loads them with `skipNormalization = true`, so the bank's RMS normaliser never runs.
- `process()` (:137-145) returns `amount_ * env_ * bank_.process()`.
- `DrumVoice` adds the result after the body has been normalised to `kBodyHeadroom = 0.5` (`drum_voice.h:84`). This happens at :557 in `process()`, :893 in the block path and :1056 in the slow path. The sum then reaches `hardClip`.
- The comments at :47-53 and :124-125 ("normalized so the bank's internal RMS normalization produces a modest output level", "we pre-chose small per-partial amplitudes") are false.
- This task changes the level only. Whether `modeInject` should ring on is T6.10's question: do not touch the decay envelope (`setDecaySeconds`, `env_`).

**1. Failing test**

- In `plugins/membrum/tests/unit/unnatural/test_mode_inject.cpp`, add `TEST_CASE("UnnaturalZone ModeInject -- full amount stays inside the body budget", "[unnatural][modeinject]")`.
- Set up a `DrumVoice` at 44100 Hz, Membrane body, Impulse exciter, `setMaterial(0.5f)`, `setDecay(0.8f)`, `setLevel(0.8f)`, noise and click mix 0. These are the audit probe tests 5/10.
- Render 1 s at velocity 1 in 256-sample `processBlock` calls, once with `modeInject.setAmount(0.0f)` and once with `1.0f`.
- REQUIRE that the amount-1 render has zero samples with |x| >= 0.999, and that its peak is within 6 dB of the amount-0 peak.
- Add `TEST_CASE("UnnaturalZone ModeInject -- default-kit closed hat level steps stay under 6 dB", "[unnatural][modeinject]")`:
  - Take `DefaultKit::apply`, pad 6 (MIDI 42). For `modeInjectAmount` {0, 0.25, 0.5, 0.75, 1.0}, use `applyPadConfigToVoice`, then `noteOn(1.0f)` and render 500 ms at 48 kHz.
  - Compute the RMS in dB with `linToDbfs` from `tests/test_helpers/audio_features.h`.
  - REQUIRE each adjacent difference < 6 dB.
- Both fail on current code:
  - Probe baseline: peak 0.403 at amount 0 vs 1.000 at amount 1, with 804 samples on the rail.
  - `param-audibility.csv` row `offset=23:modeInjectAmount` for pad 42: rms500 -49.0 / -36.8 / -31.0 / -27.6 / -26.1 dBFS, a 12.2 dB first step.

**2. Fix**

- `plugins/membrum/src/dsp/unnatural/mode_inject.h`:
  - Add `static constexpr float kTargetPeak = 0.5f;` with a comment that it equals `DrumVoice::kBodyHeadroom`, the -6 dBFS strike-peak budget.
  - In `prepare()`, after the partial loop, compute `outputScale_ = kTargetPeak / sum(frame_.partials[k].amplitude)`. The worst-case in-phase peak of the series then equals the body budget at amount 1.
  - In `process()` (:142), use `amount_ * outputScale_ * env_ * bank_.process()`.
  - Rewrite the comments at :47-53 and :124-125 to describe this scaling.
- `plugins/membrum/src/dsp/drum_voice.h`, next to `kBodyHeadroom` (:84): `static_assert(ModeInject::kTargetPeak == kBodyHeadroom, "...");`. `mode_inject.h` cannot include `drum_voice.h`, because `drum_voice.h` includes it.
- Leave the amount law linear. A squared or perceptual curve would change what a stored `modeInjectAmount` means, which is STATE-COMPAT and needs a ruling. If the closed-hat step test still fails with the scaling alone, stop and report the five measured levels (plan rule "When to stop").

**3. Build and gate:** standard workflow, plus the PRESET-IMPACT before/after kit-quality probe.

**4. Acceptance**

- Both new cases are green. The existing `[modeinject]` cases stay green: 1/k roll-off, zero leak at amount 0, envelope decay, and silence on NoteOn-only.
- The probe `unnatural:modeInject=1.0` (`probe-results.csv:576`, peak 0.00 dBFS, on the rail) now peaks below -0.1 dBFS.
- The hat's first step falls below 6 dB (baseline 12.2 dB).
- Kit table: expect every pad with `modeInjectAmount > 0` to lose level and clipping. Show the table and stop for the ruling.

### T2.17 — Coupling resonators are tuned to the settled body frequencies (pitch-envelope end, String harmonics), not to start-of-sweep or stale modes

**Findings:** MB-068 (medium, CONFIRMED).

**Flags:** PRESET-IMPACT (added). Factory kits run cross-pad coupling: `k.opts.globalCoupling` is 0.18-0.65 in `tools/membrum_preset_generator.cpp` (:1661, :1674, :2283, :2516, :2911, :3735, :4243). Their kicks, toms and String pads therefore couple at different frequencies after this fix.

**Prerequisites:** T1.8. It makes the String voice's shared-bank mode count correct. Re-read `getPartialInfo` and `BodyBank::applyPendingSwap` after it lands, because the String branch below must not depend on that bank at all.

**Estimate:** 120 min.

**1. Failing test**

- New file `plugins/membrum/tests/unit/dsp/test_coupling_partials.cpp`; add it to `plugins/membrum/tests/CMakeLists.txt`.
- `TEST_CASE("Coupling partials: a pitch-envelope pad registers its settled modes", "[coupling][dsp]")`:
  - Set up a `DrumVoice` at 44100 Hz. Use a `PadConfig` with a Membrane body and Impulse exciter, `tsPitchEnvStart = 0.4515f` (160 Hz), `tsPitchEnvEnd = 0.19897f` (50 Hz) and `tsPitchEnvTime = 0.04f` (20 ms), applied with `applyPadConfigToVoice`.
  - Call `noteOn(1.0f)` and take `info = voice.getPartialInfo()` immediately, as `VoicePool` does (`voice_pool.cpp:246`).
  - Render 100 ms with `processBlock`. After the envelope has elapsed, the bank's targets are the settled frequencies: `getModeFrequency` reads `epsilonTarget_` (`modal_resonator_bank.h:526-533`), so this holds whatever the smoothing speed.
  - REQUIRE `info.frequencies[k]` within 1 % of `voice.bodyBank().getSharedBank().getModeFrequency(k)` for k = 0..3.
- `TEST_CASE("Coupling partials: a String pad registers its own harmonics, not the slot's previous modes", "[coupling][dsp]")`:
  - On one `DrumVoice`, play a Membrane pad whose f0 is 300 Hz (size `log10(500/300) = 0.2218`) for 50 ms.
  - Then apply a String `PadConfig` with size `log10(800/110) = 0.8617` (110 Hz; `natural_fundamental.h:41`) and call `noteOn`.
  - REQUIRE `getPartialInfo().frequencies[0]` within 1 % of 110 Hz and `[k]` within 1 % of 110 x (k+1).
  - Repeat on a freshly prepared voice (baseline: 0 Hz).
- Both fail on current code:
  - The pitch-envelope pad registers 160 Hz x the mode ratios, about 1.68 octaves above the settled pitch.
  - The String pad registers the previous pad's modes, or 0 on a fresh slot.

**2. Fix**

- `plugins/membrum/src/dsp/drum_voice.h`, `getPartialInfo()` :1250-1259. It stays `const`, `noexcept` and allocation-free.
  - Settled pitch: `settledHz = toneShaper_.isPitchEnvActive() ? toneShaper_.getPitchEnvEndHz() : naturalFundamentalHz_` (`tone_shaper.h:310`, :481).
  - **String** (`bodyBank_.getCurrentType() == BodyModelType::String`): `info.frequencies[k] = settledHz * (k + 1)`. For String, `naturalFundamentalHz_` equals the `StringMapper` f0 (800 x 0.1^size; `string_mapper.h:40`, `natural_fundamental.h:41`).
  - **Modal bodies**: `info.frequencies[k] = bank.getModeFrequency(k) * (settledHz / noteOnPitchHz_)`. `noteOnPitchHz_` is a new member set in `noteOn()` to the pitch the bank was configured at: `initialPitchHz` (:228) when the pitch envelope is active, else `naturalFundamentalHz_`.
    - Scaling the read-back frequency is exact because the bank's stretch and scatter warps multiply each mode's frequency (`computeModeCoefficients`, `modal_resonator_bank.h` around :805-812).
    - A mode the bank culled at the start frequency reads 0 and stays 0.
    - Document both facts, and that the function describes the note as struck and is valid from `noteOn()` on (its only caller is `voice_pool.cpp:246`).
- `plugins/membrum/src/voice_pool/voice_pool.cpp:237`: the comment "FR-041: velocity scales coupling excitation" is false, because `SympatheticPartialInfo` carries no velocity. Grep for where velocity actually scales coupling (the case "Coupling integration: velocity scaling produces proportionally less coupling" in `test_coupling_integration.cpp:738` exercises it) and point the comment there, or delete the claim.

**3. Build and gate:** standard workflow, plus the PRESET-IMPACT kit-quality probe before and after, with global coupling as each kit sets it.

**4. Acceptance**

- Both new cases are green. The coupling suite stays green, in particular "Phase 4: SC-008 frequency-selective coupling via modal coincidence" (`test_coupling_integration.cpp:518`) and the energy-limiter cases.
- The default-kit kick's lowest registered resonator now sits within 1 % of its settled mode 0 (baseline: at 160 Hz x ratio).
- Kit table for the coupled kits: show it and stop for the ruling if any pad's numbers move audibly.

### T2.18 — Parameter readouts use the DSP's laws: cubic filter-envelope times, Size per body model, pitch-envelope Hz and ms

**Findings:** MB-077 (medium, CONFIRMED), MB-078 (low, CONFIRMED).

**Flags:** none. Display only: audio and state are unchanged.

**Prerequisites:** none.

**Estimate:** 120 min.

**1. Failing test**

- New file `plugins/membrum/tests/unit/controller/test_param_readouts.cpp`; add it to the CMake list.
- Use a `Membrum::Controller` with `initialize(nullptr)`. Convert the `String128` to ASCII with `VST3::StringConvert::convert` (`public.sdk/source/vst/utility/stringconvert.h`).
- `TEST_CASE("Param readouts: filter-envelope times use the cubic law the voice decodes", "[controller][param_format]")`:
  - `getParamStringByValue(kToneShaperFilterEnvDecayId, 0.5)` == "250 ms" (baseline "1.00 s").
  - `kToneShaperFilterEnvAttackId` at 0.5 == "62.5 ms" (baseline "250 ms").
  - `kToneShaperFilterEnvReleaseId` at 0.1 == "2.00 ms" (baseline "200 ms").
  - The same three values on `padParamId(3, kPadTSFilterEnvAttack / Decay / Release)`.
- `TEST_CASE("Param readouts: Size shows the selected body's fundamental", "[controller][param_format]")`:
  - Set `kBodyModelId` to Shell (normalized 2/5) and format `kSizeId` at 0.5: "50% (474 Hz)" (baseline "50% (158 Hz)").
  - Set `padParamId(5, kPadBodyModel)` to Plate (1/5) and format `padParamId(5, kPadSize)` at 0.5: "50% (253 Hz)".
  - Membrane still reads "50% (158 Hz)".
- `TEST_CASE("Param readouts: pitch-envelope parameters show Hz and ms", "[controller][param_format]")`:
  - `kToneShaperPitchEnvStartId` at 0.4515 == "160 Hz".
  - `kToneShaperPitchEnvEndId` at 0.19897 == "50.0 Hz".
  - `kToneShaperPitchEnvTimeId` at 0.1 == "50.0 ms".
  - `kPitchEnvMidPitchId` at 1.0 == "2.00 kHz".
  - The per-pad offsets `kPadTSPitchEnvStart`, `kPadTSPitchEnvTime` and `kPadTSPitchEnvMidPitch` on pad 7 give the same strings.
  - Baseline: the SDK default prints the 0..1 plain value.

**2. Fix** (all in `plugins/membrum/src/controller/controller_param_format.cpp`)

- Factor the text branch of `formatLinearMs` (:112-127) into a helper `writeMs(float ms, String128 out)`. Add `formatCubicMs(norm, maxMs, out)`, which computes ms = n^3 x maxMs as `pad_config_apply.h:91-97` does, and writes it with `writeMs`.
- Use `formatCubicMs` for `kPadTSFilterEnvAttack` (500), `kPadTSFilterEnvDecay` and `kPadTSFilterEnvRelease` (2000) at :291-293, and for the proxies `kToneShaperFilterEnvAttackId`, `DecayId` and `ReleaseId` at :372-374.
- `formatSizeHz` (:224-237) takes a `BodyModelType` and computes the Hz with `Membrum::Bodies::naturalFundamentalHz(body, norm)` from `dsp/bodies/natural_fundamental.h:30`. This is the one formula the voice uses (`drum_voice.h:209`); do not write a second copy.
- Add one helper that decodes a body-model normalized value with the processor's clamp: `clamp(int(norm * BodyModelType::kCount), 0, kCount - 1)`, as `controller_view_sync.cpp:160-172` does today.
  - `kSizeId` (:360) reads `getParamNormalized(kBodyModelId)`.
  - The per-pad path (:428-436) reads `getParamNormalized(padParamId(padIndex, kPadBodyModel))` for the pad the tag belongs to.
  - `formatByPadOffset` (:268) gains a `BodyModelType` parameter for that purpose.
- Add cases:
  - `kToneShaperPitchEnvStartId`, `kToneShaperPitchEnvEndId` and `kPitchEnvMidPitchId`: `formatLogHz(v, 20, 2000)`.
  - `kToneShaperPitchEnvTimeId`: `formatLinearMs(v, 0, 500)`.
  - The same for `kPadTSPitchEnvStart`, `kPadTSPitchEnvEnd`, `kPadTSPitchEnvMidPitch` and `kPadTSPitchEnvTime` in `formatByPadOffset`. These match `pad_config_apply.h:101-107` and :114-116.
- Correct the header comment at :69-72 so it names `natural_fundamental.h` and `pad_config_apply.h` as the laws the readouts mirror. The per-pad stride is 128, not 64 (the comment at :66).

**3. Build and gate:** standard workflow (add `Membrum` to the build targets: a `src/` file changed).

**4. Acceptance**

- The three new cases are green.
- `getParamStringByValue(kToneShaperFilterEnvDecayId, 0.5)` returns "250 ms".
- A Shell pad at Size 0.5 reads 474 Hz.
- A 160 Hz pitch-envelope start reads "160 Hz".
- No other formatted parameter changes: every existing `[controller]` case stays green.

### T2.19 — The editor dims only what the DSP ignores: pitch envelope and morph stay live on every body, context-only controls dim with their exciter or body

**Findings:** MB-082 (medium, CONFIRMED), MB-085 (medium, CONFIRMED), MB-104 (low, CONFIRMED).

**Flags:** UI.

**Prerequisites:** none. T2.18 adds the body-model decode helper; if it has landed, reuse it, otherwise add it here.

**Estimate:** 180 min.

**Facts the fix rests on** (re-read each before editing):

- **Pitch envelope and morph run on every body.**
  - The pitch envelope drives every body (`drum_voice.h:604-607`; `retuneFundamental` :1587-1593 sends String to `setStringFrequency` and every other body to `updateModalFundamentalOnBank`).
  - Material Morph refreshes every body (`refreshBodyForMaterialOnBank` :1638-1652).
  - The controller still gates both on Membrane: `updatePitchEnvControlsEnabled` (`controller_view_sync.cpp:174-192`, alpha 0.35 and mouse off) and `updateMorphEnabledToggleVisibility` (:207-219, toggle hidden). Their comments cite DSP code that no longer exists (:154-158, :195-199).
- **Context-only controls.** Each acts in one context only:
  - FM (tag 202 `kExciterFMRatioId`) acts only on the FMImpulse exciter, FB (203) only on Feedback, Burst (204) only on NoiseBurst and Press (205) only on Friction (`exciter_bank.h:136-169`, replay :240-251).
  - Air (302) is read only by `membrane_mapper.h:101-107`.
  - Tens (308) is Membrane-only (`drum_voice.h:722-724`, :945-947).
  - Shell (304) acts only with 2nd (305) on, and 2Sz (306) / 2Mat (307) only with 2nd on AND Shell > 0 (`drum_voice.h:394`).
  - Wire (326) does nothing on String (`computeWireGainMod` :1506) and nothing at noise mix 0.
  - The parallel noise layer's Cutf / Res / Dec / Color (291-294) do nothing at noise mix (290) = 0.
- **Where they sit in the editor.** `editor.uidesc`:
  - Exciter knobs at :216-222.
  - Air, Shell, 2nd, 2Sz, 2Mat and Tens at :248-260; 2nd is an ArcKnob for a boolean parameter.
  - Noise knobs and Wire at :307-315.
  - The controller has no dimming logic for any of them.
- **MB-104.** `modeToggleButton_` (`controller.h:224`) is written once (`controller.cpp:1182`), never read and never nulled. The 33 ms poll timer calls `updateMorphEnabledToggleVisibility()` (`controller.cpp:1533`), which runs `setVisible` + `invalid()` on every tick.

**1. Failing test**

- Rewrite `plugins/membrum/tests/unit/controller/test_pitch_env_body_gate.cpp`, replacing its premise: its header comment states the false Membrane-only claim.
- `TEST_CASE("Context gate: pitch envelope and morph are live on every body", "[pitch_env_gate][context_gate]")`: for each of the six bodies (the file's `bodyNormFor`), REQUIRE that `isContextControlLiveForTest(tag)` is true for `kToneShaperPitchEnvStartId`, `kToneShaperPitchEnvEndId`, `kToneShaperPitchEnvTimeId` and `kMorphEnabledId`.
- `TEST_CASE("Context gate: exciter, body, shell and noise controls are live only where the DSP reads them", "[context_gate]")`:
  - For each of the 7 exciters (`kExciterTypeId` normalized idx/6), exactly one of 202-205 is live: the one matching the exciter. Impulse, Mallet and Clap have none.
  - For each body, 302 and 308 are live only on Membrane.
  - 304 is live only when `kSecondaryEnabledId` >= 0.5. 306 and 307 are live only when 305 >= 0.5 and `kCouplingStrengthId` > 0.
  - 291-294 are live only when `kNoiseLayerMixId` > 0. 326 is live only when noise mix > 0 and the body is not String.
- `TEST_CASE("Context gate: the open editor dims the dead knobs and shows the morph toggle on Plate", "[context_gate][editor]")`:
  - Open the editor headless the way `editor_screenshot_test.cpp:96-118` does: `Krate::TestSupport::ensureVstguiInitialized()`, `new VSTGUI::VST3Editor(controller, "EditorDefault", MEMBRUM_RESOURCES_DIR "/editor.uidesc")`, `attached(nullptr, nativePlatformType())`, `root->attached(cframe)`, `triggerDeferedUpdates()`, then switch to the Extended panel with `kUiModeId = 1` and animation time 0.
  - Set the exciter to Impulse and the body to Plate.
  - Find the controls by tag with `Shots::findView` / `findFirst` (`tests/test_helpers/editor_screenshot.h:91`, :138).
  - REQUIRE that the ArcKnob with tag 204 has alpha 0.35 and `getMouseEnabled() == false`.
  - Switch the exciter to NoiseBurst and REQUIRE alpha 1.0 and mouse enabled.
  - REQUIRE that the `PitchEnvelopeDisplay` has alpha 1.0 and that the `MorphEnabled` toggle is visible on Plate.
  - Close the editor (`removed()` / `forget()` as that test does).
- They fail on current code: `isContextControlLiveForTest` does not exist, and on Plate the pitch-envelope display is dimmed and the toggle hidden.

**2. Fix**

- **Controller decision.** Add `[[nodiscard]] bool isContextControlLiveForTest(Steinberg::Vst::ParamID tag) noexcept`. It implements exactly the table in the second test case, reads only `getParamNormalized` of the global proxies (`kExciterTypeId`, `kBodyModelId`, `kSecondaryEnabledId`, `kCouplingStrengthId`, `kNoiseLayerMixId`), and returns true for every tag not in the table. It is used by production code, following the `isMembraneBodySelectedForTest` pattern (`controller.h:165`). Decode the exciter the way the processor does: `clamp(int(norm * ExciterType::kCount), 0, kCount - 1)`.
- **Applying it.** Add `updateContextControlsEnabled()` in `controller_view_sync.cpp`.
  - If `activeEditor_` and its frame exist, walk the view tree. For every `VSTGUI::CControl` whose `getTag()` is a context-only tag, set alpha (1.0 live, 0.35 dead) and `setMouseEnabled(live)`, and `invalid()` only when either changed, the idempotence check `updatePitchEnvControlsEnabled` uses today (:180-184).
  - It caches no view pointers, so a template swap cannot leave a dangling one.
  - In `verifyView` (`controller.cpp:1237`), apply the same per-view rule to each context-only `CControl` as it is created, so a freshly built Extended panel is dimmed correctly.
- **Removing the Membrane gate.**
  - Delete `updatePitchEnvControlsEnabled`, `updateMorphEnabledToggleVisibility` and `isMembraneBodySelectedForTest` (`controller_view_sync.cpp:153-219`, declarations `controller.h:165`, :294, :302). The pitch-envelope views and the morph toggle stay at their editor defaults: enabled and visible.
  - Replace the flags `kRefreshPitchEnvControls` / `kRefreshMorphToggleVis` (`controller.h:328-329`) with one `kRefreshContextControls`, and update `applyViewRefresh` (`controller_view_sync.cpp:117-124`).
  - Update the callers at `controller.cpp:810`, :983-984, :1316, :1329, :1344, :1395, :1631 and :1634. Use Grep: they may have moved.
  - Request `kRefreshContextControls` from `setParamNormalized` whenever the tag is `kExciterTypeId`, `kBodyModelId`, `kSecondaryEnabledId`, `kCouplingStrengthId` or `kNoiseLayerMixId`. The pad-switch sync at :983 already requests a refresh.
  - The cached pointers `pitchEnvKneeView_` and `pitchEnvKneeLabel_` that only the deleted helper read become orphans of this change: remove them and their `viewWillDelete` / `willClose` lines.
- **MB-104.**
  - Delete `modeToggleButton_` (`controller.h:224`) and its write (`controller.cpp:1182`); the button's action lambda already captures `view`.
  - Delete the per-tick `updateMorphEnabledToggleVisibility()` call and its comment in the timer lambda (`controller.cpp:1526-1533`).
- **2nd as a toggle.** In `plugins/membrum/resources/editor.uidesc:254`, change the `SecondaryEnabled` ArcKnob to the shared `ToggleButton` class with the attribute style of the `MorphEnabled` toggle at :205, centred in the knob's cell at 22 x 22: origin "225, 351". This is one control, so a hand edit is within the UI flag's three-control limit. The parameter is already discrete (`controller.cpp:234`).

**3. Build and gate:** standard workflow (targets `membrum_tests Membrum`). Then the UI flag's checks: `node tools/lint-plugin-editor-baseline.js`, `node tools/uidesc-inventory.js plugins/membrum/resources/editor.uidesc --check` (with the allow file once T3.2 has landed), and `node tools/gen-screenshots.js membrum`, which refreshes `plugins/membrum/images/`. pluginval runs: plugin sources changed.

**4. Acceptance**

- The three new cases are green, and the editor lifecycle cases (`editor_lifecycle_test.cpp`) stay green.
- On a Plate, String, Shell, Bell or NoiseBody pad the pitch-envelope editor and the morph power toggle are live. The audit's kit scan counted 23 non-Membrane pads with an active pitch envelope and 54 with morph on, none of them editable before.
- For each exciter, exactly one of FM, FB, Burst and Press is live.
- The grep `modeToggleButton_` finds nothing.
- The screenshots are regenerated.

### T2.20 — PadGridView draws the category glyph and the CG/BUS indicators, and grid updates go through the deferred view refresh

**Findings:** MB-101 (medium, CONFIRMED), MB-157 (low, PLAUSIBLE).

**Flags:** UI, VERIFY-FIRST (MB-157 only: one verifier argued that the SDK's UI-thread contract makes the off-thread path unreachable on a compliant host).

**Prerequisites:** none.

**Estimate:** 150 min.

**Facts:**
- **MB-101.** `createCustomView` builds the only production grid with `/*metaProvider*/ [](int) -> const PadConfig* { return nullptr; }` (`controller.cpp:1049-1052`). So `PadGridView::padConfigFor` (`pad_grid_view.h:206-211`) is always null, and `draw()` skips both the category glyph (`pad_grid_view.cpp:522-533`) and the CG/BUS indicators (:539-562).
  - The requirement is spec 141 FR-011 (c)(d)(e) and acceptance scenario `spec.md:99` (pad 4 shows "CG2" / "BUS3"). The finding's "FR-065" label is wrong.
  - `classifyPad` (`dsp/pad_category.h:28`) reads `bodyModel`, `exciterType` and `tsPitchEnvTime`. The indicators read `chokeGroup` and `outputBus`.
- **MB-157.** These grid mutations bypass `requestViewRefresh` (`controller_view_sync.cpp:109-115`, the finding-12 deferral):
  - `controller_state.cpp:91` and :183 (`pushKitEnabledToGrid`, `controller.cpp:1480-1488`).
  - `controller_state.cpp:95-96` (`setSelectedPadIndex`).
  - `controller.cpp:857` (`setSelectedPadIndex`), :882 and :903 (`notifyMetaChanged`), and :913 (`setPadEnabled`).
  - Each calls `invalid()` from the calling thread (`pad_grid_view.cpp:158-188`).

**1. Failing test**

- **MB-101 case.** Add to `plugins/membrum/tests/unit/controller/test_audit_ui_controller_batch.cpp`, using its `makeGrid` helper (:41-55): `TEST_CASE("Pad grid shows the choke group, output bus and category of each pad", "[membrum][controller][pad_grid]")`.
  - Set `padParamId(4, kPadChokeGroup)` to 2/8 and `padParamId(4, kPadOutputBus)` to 3/15.
  - REQUIRE that `grid->padConfigFor(4)` is non-null, with `chokeGroup == 2` and `outputBus == 3`, that `UI::chokeGroupIndicatorText(...)` gives "CG2" and `outputBusIndicatorText(...)` gives "BUS3".
  - Set pad 4's `kPadBodyModel` and `kPadExciterType` to a kick configuration from `DefaultKit::apply` pad 0 and REQUIRE `UI::categoryGlyphForConfig(*grid->padConfigFor(4)) == "K"`.
  - It fails today: the pointer is null.
- **MB-157 case** (VERIFY-FIRST): `TEST_CASE("Pad grid mutations from a non-UI thread are deferred to the UI thread", "[membrum][controller][pad_grid][threading]")`.
  - Open the editor headless as in T2.19's third case. `didOpen` records the UI thread and `activeEditor_`. Find the `PadGridView` with `findFirst`.
  - From a `std::thread`, call `setParamNormalized(kSelectedPadId, 19.0 / 31.0)`, then join.
  - REQUIRE `grid->selectedPadIndex() == 0`, because nothing touched the view off-thread. Then call `drainPendingViewRefreshForTest()` (step 2) and REQUIRE 19.
  - If the first REQUIRE passes on current code, MB-157 is refuted: keep the test, change no production code for MB-157, and record "refuted" with the output.
  - The test spawns a thread, so `node tools/tsan-scope.js --check` must pass after it is added.

**2. Fix**

- **Meta cache.**
  - Add `std::array<PadConfig, kNumPads> gridMeta_` to `Controller`, with `refreshGridMeta()`. For every pad, it decodes the five fields from the controller's own per-pad parameters with the processor's decodes:
    - `exciterType` and `bodyModel` as `clamp(int(norm * kCount), 0, kCount - 1)`.
    - `chokeGroup` as `clamp(int(norm * 8 + 0.5), 0, 8)` and `outputBus` as `clamp(int(norm * 15 + 0.5), 0, 15)`, matching `voice_pool.cpp:641-655`.
    - `tsPitchEnvTime` as the normalized value.
  - Nothing crosses into the processor.
  - Pass `[this](int pad) -> const PadConfig* { return &gridMeta_[pad]; }` at `controller.cpp:1052`, and call `refreshGridMeta()` before constructing the view.
- **Deferral.**
  - Add `kRefreshPadGrid` to the refresh flags (`controller.h:327-331`). In `applyViewRefresh`, when it is set and `padGridView_` exists: call `refreshGridMeta()`, push `selectedPadIndex_` and the 32 enabled flags (`getParamNormalized(padParamId(p, kPadEnabled)) >= 0.5`), and invalidate the grid.
  - Replace every direct grid mutation listed under Facts with `requestViewRefresh(kRefreshPadGrid)`. Also request it when a pad's `kPadExciterType`, `kPadBodyModel` or `kPadTSPitchEnvTime` changes, because the glyph depends on them.
  - `pushKitEnabledToGrid` becomes unused: delete it.
- **Drain.**
  - Move the drain in the poll timer lambda (`controller.cpp:1518-1524`: `pendingViewRefresh_.exchange(0)` + `applyViewRefresh`) into a member `drainPendingViewRefresh()`, called from the lambda, and expose it to tests as `drainPendingViewRefreshForTest()`.
  - `requestViewRefresh` applies immediately when called on the UI thread, or when no editor is open (`activeEditor_ == nullptr`, so no timer would ever drain the queue; this keeps "Audit #6" at :61-108 green unchanged). Otherwise it queues.
- **Reporting.** Note in the commit message that the spec 141 compliance row for FR-011 (`spec.md:437`) was wrong; do not edit the spec.

**3. Build and gate:** standard workflow (targets `membrum_tests Membrum`), then `node tools/tsan-scope.js --check`. UI flag: `node tools/lint-plugin-editor-baseline.js`, the uidesc inventory check, and `node tools/gen-screenshots.js membrum`, because the grid now draws glyphs and indicators in the site images. pluginval runs.

**4. Acceptance**

- The MB-101 case is green: pad 4 reports CG2 / BUS3 and glyph "K".
- The MB-157 case is green, or MB-157 is recorded as refuted with the test output.
- "Audit #6" (both cases), "FR-066" and the output-routing UI cases stay green.
- The refreshed `pad-grid.png` shows category glyphs on every pad and CG / BUS text on the pads of the loaded kit that have them.

### T2.21 — A ringing mode keeps its amplitude when it is retuned: opt-in state-preserving retune in the modal bank

**Findings:** MB-270 (low, CONFIRMED).

**Flags:** CROSS-PLUGIN IMPACT, PRESET-IMPACT.

**Prerequisites:** T1.11, which makes modal retunes converge. Read what T1.11 committed in `updateModalFundamentalOnBank` and `pushCachedModes` (`drum_voice.h:1607-1626`, :1656-1665) before starting. This task assumes T1.11 retunes with `updateModes` followed by `snapCoefficients` on a fixed control grid. If it uses per-sample coefficient smoothing instead, the amplitude correction has to run inside that smoothing: stop and report.

**Estimate:** 180 min.

**Mechanism.**
- The bank's kernel is the Gordon-Smith magic circle: s' = R(s + eps c) + g x, c' = R(c - eps s') (`modal_resonator_bank.h:959-960`, SIMD `modal_resonator_bank_simd.cpp:98-103`), with eps = 2 sin(pi f / fs) (`modal_resonator_bank.h:827`).
- With R = 1 it conserves Q = s^2 + c^2 + eps s c, and its peak is A = sqrt(Q / (1 - eps^2 / 4)).
- `updateModes` (:294-305) and `snapCoefficients` (:342-351) change eps while (s, c) carry over, so A jumps by an amount that depends on the phase, and a slow sweep scales A as cos(pi f / fs)^(-1/2).
- Sources: M. Vicanek, "A New Recursive Quadrature Oscillator", 2015 (sections 4 and Appendix); J.O. Smith, *Physical Audio Signal Processing*, "Digital Sinusoid Generators". The invariant is derived in the finding, not stated in either source.

**1. Failing test**

- `dsp/tests/unit/processors/test_modal_resonator_bank.cpp` (builds into `dsp_processors_tests`): `TEST_CASE("ModalResonatorBank retuneModes keeps a ringing mode's amplitude", "[modal_resonator_bank][retune]")`.
  - Use one mode with R close to 1 (long decay), rung by one impulse.
  - At 64 different phases (advance 0..63 samples before the retune), at 44100 and 96000 Hz, retune 16000 Hz to 800 Hz with the new call.
  - Measure the peak of the next 4 periods against the decay-only expectation.
  - REQUIRE |error| <= 0.05 dB at every phase.
  - With `updateModes` + `snapCoefficients` instead, the same harness shows the baseline (`specs/membrum-audit/probes/retune-probe-results.txt`, E1: -6.22..+2.34 dB at 44.1 kHz). Assert this in a second SECTION, so the case documents that the default path is unchanged.
- `plugins/membrum/tests/unit/dsp/test_pitch_env_all_bodies.cpp`: `TEST_CASE("H-3: a high-start pitch envelope keeps the upper partials' level at every sample rate", "[dsp][pitch_env][retune]")`.
  - Use a Membrane pad with Impulse exciter, noise and click mix 0, pitch envelope 2000 to 100 Hz over 30 ms (`tsPitchEnvStart = 1.0`, `tsPitchEnvEnd = log100(5) = 0.3495`, `tsPitchEnvTime = 0.06`).
  - Render 500 ms through `applyPadConfigToVoice` + `processBlock` at 44100 and 96000.
  - Compare the levels in octave bands 100 Hz-16 kHz (`sumBinPower` / `toDb` from `tests/test_helpers/spectral_analysis.h`). REQUIRE every band within 0.5 dB between the two rates.
  - Baseline (probe E2, 64-sample snap): min -6.38 dB, median -1.48 dB at 44.1 kHz against -0.62..-1.04 dB at 96 kHz.

**2. Fix**

- `dsp/include/krate/dsp/processors/modal_resonator_bank.h`: add a public `retuneModes(const float* frequencies, const float* amplitudes, int numPartials, DampingLaw damping, float stretch, float scatter) noexcept`, next to `updateModes` (:294).
  1. For each mode k < `numModes_`, compute A_k^2 = (s^2 + c^2 + eps_k s c) / (1 - eps_k^2 / 4) from `sinState_`, `cosState_` and the running `epsilon_`.
  2. Call `computeModeCoefficients(..., false)`, as `updateModes` does.
  3. Call `snapCoefficients()`.
  4. For each mode, keep s, and replace c with the root of c'^2 + eps'_k s c' + s^2 - A_k^2 (1 - eps'_k^2 / 4) = 0 nearest the old c. The discriminant is (4 - eps'^2)(A^2 - s^2): clamp it at 0 before `sqrt` (the MACOS domain-edge rule).
  - A mode that became inactive keeps its state untouched.
  - The function is allocation-free and runs at control rate (every 64 samples, at most 96 modes).
  - Document the invariant and the two sources above.
- Leave `updateModes`, `snapCoefficients`, `smoothCoefficients` and `setModes` unchanged, so Innexus and Seraphis stay bit-identical. Innexus calls `prepare` / `reset` / `setModes` / `updateModes` (legacy overloads) / `processSample` (`innexus_voice.h`, `processor.cpp`, `processor_midi.cpp`). Seraphis `ContinuousBody` calls `updateModes(DampingLaw)` + `snapCoefficients` (`continuous_body.h`).
- `plugins/membrum/src/dsp/drum_voice.h`: in `updateModalFundamentalOnBank` (:1607-1626) and `pushCachedModes` (:1656-1665), replace the `updateModes` (+ `snapCoefficients` from T1.11) pair with `retuneModes`. At note-on the state is zero, so the correction is a no-op there.
- CROSS-PLUGIN obligations:
  - Before editing, run `grep -rl "modal_resonator_bank.h" plugins/ dsp/include tests/` and list every consumer in the commit message.
  - The failing test is the `dsp_processors_tests` case above.

**3. Build and gate**

- Build `dsp_processors_tests membrum_tests Membrum`.
- Run `node tools/header-consumers.js --tag '[membrum]'`, build every executable its last line names (expect `innexus_tests` and `seraphis_tests` among them), and gate them all with `~[long]` appended to the filter.
- Their goldens must not move: the default path is untouched. A moved Innexus or Seraphis golden is a defect in this change, never a re-baseline.
- clang-tidy adds `-Target dsp`.
- Then follow the PRESET-IMPACT kit-quality probe procedure.

**4. Acceptance**

- The bank case is green: |error| <= 0.05 dB at all 64 phases, at both rates (baseline -6.22..+2.34 dB).
- The Membrum case is green: every band within 0.5 dB between 44.1 and 96 kHz.
- The Innexus and Seraphis suites are green with no golden change.
- Kit table: expect changes only on pads with a high-start pitch envelope or Material Morph. Kick and tom sweeps moved at most 0.11 dB in the probe (E2 kick 200 to 40 Hz). Show the table and stop for the ruling if any pad moves audibly.

### T2.22 — ImpactExciter runs its filter out after the pulse instead of cutting it mid-swing

**Findings:** MB-022 (low, CONFIRMED).
**Flags:** CROSS-PLUGIN IMPACT; PRESET-IMPACT (added: the Mallet and Impulse exciters of every factory kit go through `ImpactExciter`, and `tools/membrum_preset_generator.cpp` builds small-body Mallet pads (crotale, glockenspiel, small bell), so the excitation of those pads changes; that change is the finding's purpose, but the before/after table is still owed).
**Prerequisites:** none.
**Estimate:** 90 min.

**1. Failing test.**
- `dsp/tests/unit/processors/impact_exciter_test.cpp` (builds into `dsp_processors_tests`), new `TEST_CASE("ImpactExciter rings its lowpass out after the pulse instead of truncating it", "[processors][impact_exciter]")`. At 44.1 kHz, `prepare(44100.0, 0)`, then `trigger(0.1f, 0.15f, 0.0024f, -0.47f, 0.0f, 0.0f)`. These are the arguments `MalletExciter::trigger` passes at velocity 0.1 with body-size hint 0.1: hardness 0.1 + 0.5 x 0.1, brightness -0.5 + 0.3 x 0.1, mass 0.3 x 0.2^3. Render 4096 samples. Find the peak and the index of the last sample with `|y| > 0`. Assert `|y[last]| / peak < 0.01`; today it is 0.500 (the audit probe) to 0.515 (a verifier's rerun). Also assert that `isActive()` stays true until the output has fallen below 1e-6, and that it is false by sample 4096.
- `plugins/membrum/tests/unit/exciters/test_mallet_contact_hint.cpp`, new `TEST_CASE("MalletExciter: a soft hit on a small body ends without a step", "[membrum][exciter][mallet]")`. Use `Membrum::MalletExciter` at 44.1 kHz: `prepare(44100.0, 0)`, `setBodySizeHint(0.1f)`, `trigger(0.1f)`, 4096 samples of `process(0.0f)`. Apply the same ratio assertion (< 0.01), so the Membrum call path is pinned as well. Use `isFiniteSample` from `exciter_test_helpers.h` for the finiteness check.
- Both cases fail today because `ImpactExciter::process` returns 0 on the sample after the pulse ends.

**2. Fix.** `dsp/include/krate/dsp/processors/impact_exciter.h`, class `ImpactExciter`:
- Add a member `bool ringing_ = false;` and a counter `int quietSamples_ = 0;`. Clear both in `reset()`. In `trigger()`, set `ringing_ = false;` and `quietSamples_ = 0;`.
- In `process()`, the early return (`if (!pulseActive_ && !bounceActive_) return 0.0f;`, today at :229-230) becomes `if (!pulseActive_ && !bounceActive_ && !ringing_) return 0.0f;`.
- When the pulse counter clears `pulseActive_` (today :256-258), or the bounce counter clears `bounceActive_` (:272-273), and neither stage is still active, set `ringing_ = true`.
- While `ringing_` is set, the sample's input is 0. It still runs through the attack-ramp block, the SVF (`svf_.process`), the comb block and the energy cap, exactly as today. After the energy cap, if `|output| < 1e-6f` increment `quietSamples_`, otherwise reset it to 0. When `quietSamples_` reaches `64 + combDelaySamples_` (so the comb's delayed copy has drained too), clear `ringing_` and return 0.
- `isActive()` (today :320) returns `pulseActive_ || bounceActive_ || ringing_`.
- The noise generator and the pinking state run only while the pulse is active, as today. The ring-out draws no RNG values, so the RNG stream of the next hit is unchanged.
- Fix the doc comment on `process()` to say that the filter rings out to -120 dBFS after the pulse.
- Do not touch `plugins/membrum/src/dsp/exciters/mallet_exciter.h` or `impulse_exciter.h`. They forward `isActive()`, so the voice sees the slightly longer active phase on its own.
- Innexus (`plugins/innexus/src/processor/processor.cpp`, the `ExciterType::Impact` case calling `impactExciter.process`) gets the same ring-out. This is a shared root-cause fix: no opt-in, Innexus's truncation click goes away too.
- The existing cases in `impact_exciter_test.cpp` ("isActive tracks pulse lifecycle", "processBlock produces non-zero then zero output", "pulse is zero after maximum duration at all mass settings") must stay unchanged and green. The ring-out ends within a few milliseconds of the pulse, well before their sample-1500 and sample-1800 bounds. If one of them goes red, report the measured tail length instead of moving its bound.

**3. Build and gate.**
- Before editing: `grep -rl "impact_exciter.h" plugins/ dsp/include tests/ dsp/tests`. At the audit this listed `plugins/innexus/src/processor/innexus_voice.h`, `plugins/innexus/tests/unit/processor/bow_exciter_switch_test.cpp`, `plugins/membrum/src/dsp/exciters/impulse_exciter.h`, `plugins/membrum/src/dsp/exciters/mallet_exciter.h` and `dsp/tests/unit/processors/impact_exciter_test.cpp`. List the grep's result in the commit message.
- Build `dsp_processors_tests`, `membrum_tests`, `Membrum` and every executable that `node tools/header-consumers.js --tag '[membrum]'` names (expect `innexus_tests`), then gate them in one `run-gate.js` command with `~[long]`.
- An Innexus golden that moves is diffed and justified (the tail of the Impact excitation no longer truncates), never regenerated blindly. Add the `-Target dsp` clang-tidy run.
- PRESET-IMPACT: run the kit-quality probe before and after, and put the table of every Mallet and Impulse pad whose numbers move in the commit message.

**4. Acceptance.**
- Both new cases pass. The last-sample-to-peak ratio for hint 0.1 / velocity 0.1 goes from 0.500 to below 0.01.
- `dsp_processors_tests`, `innexus_tests` and `membrum_tests` read `GATE GREEN`.
- The kit table shows the affected pads' peak, rms500, decay and centroid. A change the user would hear as a different kit stops for a ruling.

### T2.23 — One noise-seeding policy for every exciter and the click layer: consecutive hits differ, renders stay reproducible

**Findings:** MB-025 (low, CONFIRMED).
**Flags:** CROSS-PLUGIN IMPACT; PRESET-IMPACT (added: the new policy changes the noise sequence every Clap, NoiseBurst, Impulse, Mallet, Friction and Feedback pad, every click transient and every parallel noise layer plays on a given hit. The level and spectrum do not change, but the kit probe numbers can move within the noise, so the before/after table is owed).
**Prerequisites:** none.
**Estimate:** 120 min.

The policy this task implements: every noise source in a voice is seeded per hit from a seed that depends on the pad and on that pad's hit count. So two consecutive hits of one pad differ. Two fresh pools that receive the same events render bit-identically. A stolen or choked voice renders exactly what a pristine voice renders for the same pad and the same hit number, because the slot no longer enters the seed. That keeps the FR-124 bit-identity tests meaningful.

**1. Failing test.** New file `plugins/membrum/tests/unit/voice_pool/test_noise_seeding.cpp` (add it to `add_executable(membrum_tests` in `plugins/membrum/tests/CMakeLists.txt`), tags `[voice_pool][noise_seeding]`:
- `TEST_CASE("Noise seeding: two consecutive hits of one Clap pad differ", "[voice_pool][noise_seeding]")`.
  - Use one `DrumVoice`, `prepare(48000.0, 3)`. Apply a `PadConfig` with `exciterType = ExciterType::Clap`, `noiseLayerMix = 0.5f` and `clickLayerMix = 0.5f` through `applyPadConfigToVoice` (`dsp/pad_config_apply.h`).
  - Call `noteOn(0.8f)` and render 4096 samples into buffer A. Keep rendering until `!isActive()` (bounded to 10 s, `REQUIRE` the bound holds). Then call `noteOn(0.8f)` again and render 4096 samples into buffer B.
  - Assert that the maximum of `|A - B|` is > 1e-3. Today it is 0: `ClapExciter::trigger`, `ClickLayer::trigger` and `NoiseLayer::trigger` all reseed to the per-slot seed, and the probe measured "Clap hit1 vs hit2 maxdiff 0.000000".
- `TEST_CASE("Noise seeding: renders are reproducible across fresh pools", "[voice_pool][noise_seeding]")`.
  - Use two fresh `VoicePool`s prepared identically, with pad 0 set to Clap, pad 2 to NoiseBurst and pad 4 to Impulse via `setPadConfigSelector` / `setPadConfigField`.
  - Send the same sequence to both: notes 36, 38 and 40 at 0.8, then note 36 again 50 ms later. Use the event helpers in `tests/test_helpers/vst_event_list.h` only if driving a `Processor`; with a bare pool, call `noteOn` between blocks.
  - Render 1 s and assert the two outputs are bit-identical. This is a regression guard and passes before and after.
- `TEST_CASE("Noise seeding: a pad's hit is independent of which slot it lands on", "[voice_pool][noise_seeding]")`.
  - Pool A plays note 40 (Impulse), waits until `!isAnyVoiceActive()`, then plays note 36 (Clap). Pool B plays only note 36.
  - Assert that note 36's first 4096 samples match within 1e-6. Today the Clap seed is `0xC1A9 ^ voiceId`, so the result depends on the slot the allocator picks. Record whether it is red today. If it is green (same slot), the first case is the red that counts.
- `dsp/tests/unit/processors/bow_exciter_test.cpp`, `TEST_CASE("BowExciter setNoiseSeed gives distinct jitter streams and the default stream is unchanged", "[processors][bow_exciter]")`.
  - Two `BowExciter`s prepared identically, one with `setNoiseSeed(0x1234u)`, the other with no call. Run the same trigger and process sequence on both and assert their outputs differ.
  - The default path, with no setter call, stays exactly as it is: every existing `bow_exciter_test.cpp` case and Innexus's `bow_exciter_switch_test.cpp` must pass unchanged.
  - It fails to compile until the setter exists. That is the expected red.

**2. Fix.**
- New header `plugins/membrum/src/dsp/hit_seed.h` (first run `grep -rn "hitNoiseSeed" dsp/ plugins/` for the ODR check). It holds `constexpr std::uint32_t hitNoiseSeed(int padIndex, std::uint32_t hitCount) noexcept`:
  - `x = static_cast<std::uint32_t>(padIndex + 1) * 0x9E3779B9u + hitCount;`
  - then the 32-bit MurmurHash3 finaliser: `x ^= x >> 16; x *= 0x85EBCA6Bu; x ^= x >> 13; x *= 0xC2B2AE35u; x ^= x >> 16;`
  - return `x == 0 ? 1u : x`.
  - Every consumer XORs its own existing constant into it, so the streams stay distinct: `0xC1A9u` Clap, `0xB357u` NoiseBurst, `0xFEEDu` Feedback, `17u` click layer, `1u` noise layer, `0x1A7Cu` Impact and `0xB0E5u` Friction.
- `VoicePool` (`voice_pool.h` / `voice_pool.cpp`):
  - Add `std::array<std::uint32_t, kNumPads> padHitCount_{}`, zeroed in `VoicePool::prepare`.
  - In `VoicePool::noteOn`, in the `VoiceEvent::Type::NoteOn` case (today :227-258), just before `VP_VOICES[slot].noteOn(clampedVel)`, call `VP_VOICES[slot].setHitSeed(hitNoiseSeed(padIndex, padHitCount_[padIndex]++));`.
  - Do the same in any other path that calls `DrumVoice::noteOn`. Grep `\.noteOn(` in `voice_pool.cpp`.
- `DrumVoice` (`plugins/membrum/src/dsp/drum_voice.h`):
  - Add `void setHitSeed(std::uint32_t s) noexcept { pendingHitSeed_ = s; hasPendingHitSeed_ = true; }`.
  - In `noteOn` (starting :170), compute `const std::uint32_t seed = hasPendingHitSeed_ ? pendingHitSeed_ : hitNoiseSeed(kNumPads + static_cast<int>(voiceId_), ownHitCount_++);`, then clear `hasPendingHitSeed_`. A standalone voice in a test also varies per hit.
  - Pass the seed to `exciterBank_.setHitSeed(seed)`, `noiseLayer_.setHitSeed(seed)` and `clickLayer_.setHitSeed(seed)` before their `trigger` calls (today `exciterBank_.trigger` :343, `noiseLayer_.trigger` :372, `clickLayer_.trigger` :378).
- `ExciterBank` (`exciter_bank.h`):
  - `setHitSeed(std::uint32_t)` stores the value.
  - `trigger()` first runs `applyPendingSwap()` as today, then visits `active_` with `if constexpr (requires { e.setHitSeed(seed); }) e.setHitSeed(seed);`, then triggers. That way an exciter created by the swap also gets the seed.
- Exciters and layers. Each one gets `setHitSeed(std::uint32_t s)` storing `hitSeed_ = s ^ <its constant>`, and its `trigger()` reseeds from it:
  - `ClapExciter::trigger` (`clap_exciter.h:84-88`), `NoiseBurstExciter::trigger` (`noise_burst_exciter.h:87`, which today does not reseed), `FeedbackExciter`'s trigger for `seedNoise_`, `ClickLayer::trigger` (`click_layer.h:87-90`) and `NoiseLayer::trigger` (`noise_layer.h:158-170`): call `noise_.setSeed(hitSeed_);` followed by the existing `noise_.reset();`. `NoiseOscillator::setSeed` stores the seed, and `reset()` then reseeds to it and clears the colour filters.
  - `ImpulseExciter` and `MalletExciter` (`exciters/impulse_exciter.h`, `exciters/mallet_exciter.h:69-91`): call `core_.reseed(hitSeed_)` before `core_.trigger(...)`.
  - `FrictionExciter` (`exciters/friction_exciter.h:72-95`): call `core_.setNoiseSeed(hitSeed_)` before `core_.trigger(velocity)`.
- Shared headers, opt-in only, so Innexus stays bit-identical:
  - `dsp/include/krate/dsp/processors/impact_exciter.h` gets `void reseed(std::uint32_t seed) noexcept { rng_.seed(seed); }`.
  - `dsp/include/krate/dsp/processors/bow_exciter.h` gets `void setNoiseSeed(std::uint32_t seed) noexcept { noiseState_ = seed != 0 ? seed : 12345u; }`.
  - `reset()` keeps `noiseState_ = 12345` (`bow_exciter.h:137`), so nothing changes for a caller that never calls the setter.
- Rewrite the comments that this policy makes false: the strike-probe comment in `probeStrikePeakExcited` (`drum_voice.h:1318-1320`), the "re-seeds" comments at `clap_exciter.h:27-28` and `click_layer.h:79-86` (FR-124 there is the fast-release rule, not bit-identity), the `noise_layer.h:153-157` comment, and the `VoicePool::prepare` comment block at `voice_pool.cpp:88-105`.
  - The probe now leaves no residue, because the audible `trigger` reseeds every source.
  - If T1.6 already removed the probe from note-on, there is nothing to change there.
- Existing determinism tests keep their strength. `"ClapExciter: bit-identical output for same voiceId + velocity (FR-124)"` (`test_clap_exciter.cpp:96`), the click-layer test and the choke and steal bit-identity checks (`test_choke_click_free.cpp`, `test_steal_click_free.cpp`) give the compared instances the same hit seed (`setHitSeed`, or the same per-pad hit history in the reference pool). Never delete or loosen an equality.
- `phase1_default.bin` may move because the first hit's noise sequence changes. Re-baseline it deliberately and say why in the commit message.
- Out of scope: report, do not change, `unnatural/mode_inject.h:44` (`rng_.seed(voiceId)`) if the reviewer wants it under the same policy.

**3. Build and gate.**
- Run `grep -rl "impact_exciter.h\|bow_exciter.h" plugins/ dsp/include tests/ dsp/tests` and list the consumers. At the audit: Innexus `innexus_voice.h` and its `bow_exciter_switch_test.cpp`, Membrum's three exciter wrappers, and four `dsp/tests/unit/processors/*bow*` files plus `impact_exciter_test.cpp`.
- Build `dsp_processors_tests`, `membrum_tests`, `Membrum` and the executables `header-consumers.js` names (expect `innexus_tests`), and gate them together with `~[long]`.
- Innexus output must be bit-identical, because it never calls either new method. A moved Innexus golden is a defect in this task.
- PRESET-IMPACT: run the kit probe before and after; expect the numbers to move only within noise.

**4. Acceptance.**
- The Clap consecutive-hit difference goes from 0.000000 to > 1e-3.
- Two fresh pools are bit-identical.
- `BowExciter` with no setter call is unchanged.
- All gates read `GATE GREEN`.
- The kit table shows no pad whose peak, rms500 or decay moves by more than the noise. Stop for a ruling if one does.

### T2.24 — No wasted work at note-on or at mix 0: pitch-envelope tables rebuilt only on change, the noise layer skipped when silent

**Findings:** MB-047 (low, CONFIRMED), MB-048 (low, CONFIRMED).
**Flags:** none.
**Prerequisites:** none.
**Estimate:** 120 min.

**1. Failing test.**
- `plugins/membrum/tests/unit/tone_shaper/test_tone_shaper.cpp`, new `TEST_CASE("ToneShaper: pitch-envelope tables are rebuilt only when an input changes", "[tone_shaper][rt-safety]")`.
  - Use a `DrumVoice` prepared at 48 kHz and a `PadConfig` with `tsPitchEnvTime = 0.1f` and `tsPitchEnvKneeEnabled = 1.0f`. Call `applyPadConfigToVoice(voice, cfg)` and `voice.noteOn(1.0f)` once (warm-up), then read `voice.toneShaper().pitchEnvRebuildCountForTest()`.
  - Call `applyPadConfigToVoice` with the same config and `noteOn` again, and assert the count did not change. Today it rises by 8, one rebuild per setter at `pad_config_apply.h:101-120`.
  - Change one input (`tsPitchEnvEnd`), apply and trigger, and assert it rose by exactly 1.
  - With `tsPitchEnvTime = 0` and a changed input, assert it rose by 0, because a disabled envelope is not built.
  - It fails to compile until the accessor exists. Add the accessor first with the rebuild counting `++` inside `rebuildPitchEnv_`, confirm the 8, then fix.
- `plugins/membrum/tests/unit/dsp/test_noise_layer.cpp`, new `TEST_CASE("NoiseLayer: mix 0 does not advance the noise, filter or envelope", "[membrum][dsp][noise_layer]")`.
  - Configure a `NoiseLayer` with mix 0, `trigger(1.0f)`, then `processBlock(out, 64)`.
  - Take the next value of `layer.oscillator().process()` (accessor at `noise_layer.h:151`) and compare it with the first `process()` value of a fresh `NoiseOscillator` given the same colour and seed, reset the same way. Assert they are equal.
  - Today the layer advanced the oscillator 64 times, so they differ.
  - Also assert that `out` is all 0.0f.

**2. Fix.**
- `plugins/membrum/src/dsp/tone_shaper.h`:
  - Every pitch-envelope setter (`setPitchEnvStartHz`, `setPitchEnvEndHz`, `setPitchEnvTimeMs`, `setPitchEnvCurveAmount`, `setPitchEnvCurve2Amount`, `setPitchEnvKneeEnabled`, `setPitchEnvMidHz`, `setPitchEnvMidFraction`, today :228-283) computes its clamped value. If that equals the stored member, it returns. Otherwise it stores the value and sets `pitchEnvDirty_ = true`. It no longer calls `rebuildPitchEnv_()`.
  - Add `void ensurePitchEnvBuilt_() noexcept { if (pitchEnvDirty_ && pitchEnvTimeMs_ > 0.0f) { rebuildPitchEnv_(); pitchEnvDirty_ = false; } }`.
  - Call it first thing in `noteOn()` (today :319) and in `processPitchEnvelope()` (:380), and before any other read of `pitchEnv_` (Grep `pitchEnv_` in the file).
  - `prepare()` (:113-115) still builds once and clears the flag.
  - `rebuildPitchEnv_` increments `pitchEnvRebuildCount_`.
  - Add `[[nodiscard]] std::uint32_t pitchEnvRebuildCountForTest() const noexcept`.
  - Rewrite the comment at :537-540: the rebuild runs on the audio thread at note-on, but only when an input changed.
- `plugins/membrum/src/dsp/noise_layer.h`:
  - In `processBlock` (:196-207), the `mix_ <= 0.0f` branch becomes `std::fill(out, out + numSamples, 0.0f); return;`.
  - In `processSample` (:178-191), the same branch returns `0.0f` before `stepBloom` and before any `process()` call.
  - This is output-identical. `mix_` changes only in `configure()` / `configureRaw()`, called at note-on, and `trigger()` resets the noise, filter and envelope and re-arms bloom. `isActive()` (:218) is already gated on `mix_ > 0`.
- `plugins/membrum/src/dsp/drum_voice.h`, the parallel-noise block in the fast path (today :855-864):
  - When `noiseLayer_.mix() <= 0.0f`, skip both `noiseLayer_.processBlock` and the gain loop. `noiseScratch` must then be zero. Zero it once with `std::fill` in that branch; that costs one pass instead of three components.
  - The per-sample paths (:548, :1050) need no change, since `processSample` now returns 0 at once.

**3. Build and gate.**
- Standard workflow. Run the timing lane once at the end, alone: `node tools/run-cpu-tests.js membrum_tests`. Report the polyphony benchmark numbers before and after in the commit message.
- `phase1_default.bin` and every render test must be unchanged. These changes are output-identical; a moved golden is a defect.

**4. Acceptance.**
- An unchanged pad's note-on goes from 8 rebuilds (about 2,056 `powf` with the knee off, 4,112 with it on) to 0.
- The noise-layer case passes.
- Every existing `membrum_tests` case is green with no golden change.

### T2.25 — Pan and output bus are latched per note, so a ringing tail does not jump between buses

**Findings:** MB-056 (low, CONFIRMED).
**Flags:** none.
**Prerequisites:** none.
**Estimate:** 90 min.

**1. Failing test.** `plugins/membrum/tests/unit/voice_pool/test_multi_bus_output.cpp`, new `TEST_CASE("Multi-bus: a ringing voice keeps the output bus and pan it started with", "[multi-bus][voice_pool]")`.
- Setup: a `VoicePool` at 48 kHz, block 256, 4 output buses, all active. Set pad 0 (note 36) to `outputBus` 2 (`setPadConfigField(0, kPadOutputBus, 2.0f / 15.0f)`) and pan 0.5. Call `noteOn(36, 1.0f)` and render 4 blocks with the multi-bus `processBlock`.
- Then set pad 0's `kPadOutputBus` to `3.0f / 15.0f` and `kPadPan` to 0.0, and render 1 block. Assert:
  - aux 3 is all zeros;
  - aux 2's block is non-zero (max > 1e-4);
  - its first sample stays continuous with the previous block's last sample: the jump is no larger than the largest sample-to-sample step inside the previous block;
  - main `outL[i] == outR[i]` for every sample, so the centre pan is kept.
- Today the tail moves to aux 3, aux 2 drops to 0, and the main bus is hard left.
- Then render until `!isAnyVoiceActive()` (bounded, `REQUIRE`), call `noteOn(36, 1.0f)` again and render 1 block: aux 3 now receives the hit, and `outR` is 0 (hard left).
- Add the same pan assertions to `test_per_pad_pan.cpp` for the mono `processBlock`: `TEST_CASE("VoicePool per-pad pan: a ringing voice keeps its note-on pan", "[voice_pool][pan]")`.

**2. Fix.**
- `plugins/membrum/src/voice_pool/voice_meta.h`, struct `VoiceMeta`: add `float panGainL = 1.0f;`, `float panGainR = 1.0f;` and `std::uint8_t outputBus = 0;`. The `static_assert(sizeof(VoiceMeta) <= 64)` must still hold; it will, at about 32 bytes.
- `voice_pool.cpp`, `VoicePool::noteOn`, `NoteOn` case (today :250-257): latch `panGainsForNote(midiNote, meta_[slot].panGainL, meta_[slot].panGainR)` and `meta_[slot].outputBus = padConfigs_[padIndex].outputBus`.
- `VoicePool::beginFastRelease` (today :946-951): copy `panGainL`, `panGainR` and `outputBus` into `releasingMeta_[slot]` next to `originatingNote`.
- Both `processBlock` overloads, main and shadow loops (today :359-361, :418-420, :474-476, :532-534): use `meta_[slot].panGainL/R` and `releasingMeta_[slot].panGainL/R` instead of calling `panGainsForNote`.
- The aux lookups (today :489-494 and :543-548) use `meta_[slot].outputBus` and `releasingMeta_[slot].outputBus` instead of `padConfigs_[padIndex].outputBus`. Keep the existing `busActive` and null-pointer checks.
- Any other place that sets a slot to `Active` (Grep `VoiceSlotState::Active` in `voice_pool.cpp`) latches the same fields.
- `setPadConfigField` (`:649`, `:651-655`) stays as it is. A pan or bus edit now takes effect at the pad's next note-on, the same as the other 62 per-pad parameters (audit probe: "Mid-note automation changes only pan"). Update the M-9 comments at :417 and :531 to say the pan is latched at note-on.

**3. Build and gate.** Standard workflow; no extra targets.

**4. Acceptance.**
- The new cases pass.
- The existing `[multi-bus]` cases (including "bus deactivation mid-session routes to main only", which reads the live `busActive`, not the latched bus) and the per-pad pan cases are green.
- A mid-ring Output Bus change no longer starts or stops a tail on an aux bus.

### T2.26 — Coupling return path: energy-limiter times in milliseconds, and the main-bus silence flag set when nothing sounds

**Findings:** MB-015 (low, CONFIRMED), MB-064 (low, CONFIRMED), MB-182 (low, CONFIRMED).
**Flags:** none. The limiter's release changes only at rates other than 44.1 kHz, and only while the coupling return is above -20 dBFS. The audit measured that Global Coupling at 1.0 moves kit level by 0.0 dB, so the limiter does not engage on factory kits. If the step-4 gate or a probe shows a kit change, add PRESET-IMPACT and say why.
**Prerequisites:** none.
**Estimate:** 120 min.

**1. Failing test.** `plugins/membrum/tests/unit/processor/test_coupling_integration.cpp`. Use its `CouplingIntegrationFixture`, which takes a block size and a sample rate. Do not use `test_coupling_energy.cpp`: it is a per-file `-fno-fast-math` TU, and inline processor code must not be checked from there.
- `TEST_CASE("Coupling integration: energy-limiter release time is the same in milliseconds at 44.1 and 192 kHz", "[coupling][sample-rate]")`.
  - Add the test accessor `float applyEnergyLimiterForTest(float x) noexcept { return applyEnergyLimiter(x); }` to `Processor` (`processor.h`, next to `energyEnvelopeForTest`).
  - For each rate, use a fixture at that rate. Feed 1.0 for 64 samples, then 0.0, and count the samples until `energyEnvelopeForTest()` falls to `exp(-1)` of its peak. Convert the count to ms.
  - Assert that both rates read 226.7 ms within 5 %. Today 44.1 kHz reads 226.7 ms and 192 kHz reads 52 ms.
  - Also feed a 0.5-amplitude 100 Hz sine for 0.5 s at both rates and assert that every limiter output is <= 0.1 (the -20 dBFS cap).
- `TEST_CASE("Coupling integration: main bus is flagged silent once voices and coupling resonators have decayed", "[coupling][silence]")`.
  - Fixture at 48 kHz, block 256. Set `kGlobalCouplingId` to 1.0, play `addNoteOn(36, 1.0f)`, then render 10 s.
  - Assert that every block in the last 1 s has `fix.outputBus.silenceFlags == 3` and all-zero `outL` and `outR`. Today none of them does.
- `TEST_CASE("Coupling integration: idle processor with coupling on reports silence", "[coupling][silence]")`.
  - Coupling 1.0, no notes, 5 s.
  - Assert that at least 95 % of the blocks are flagged 3, and every block after the first 200 ms. The probe baseline is `coupling:idle-no-notes`: RMS -240 dBFS with 469 of 469 blocks unflagged.

**2. Fix.** `plugins/membrum/src/processor/processor.h` / `processor.cpp`:
- `applyEnergyLimiter` (today `processor.h:183-196`):
  - The attack becomes a peak hold (`if (absVal > energyEnvelope_) energyEnvelope_ = absVal;`). That is rate-independent and keeps the -20 dBFS cap exact, because the envelope is never below `|x|`.
  - The release uses a member `energyReleaseCoeff_`, computed in `Processor::setupProcessing` (today :1067-1091) as `std::exp(-1.0f / (kEnergyLimiterReleaseSec * static_cast<float>(sampleRate_)))`, with `constexpr float kEnergyLimiterReleaseSec = 0.2267f;`. That is today's 10,000-sample time constant at 44.1 kHz, so a 44.1 kHz session is unchanged except for the 0.1 % attack lag.
  - Keep any non-finite guard an earlier task added to this function.
  - `test_coupling_fuzz.cpp:59-63` keeps a mirror copy (`fuzzEnergyLimiter`). Make it call `applyEnergyLimiterForTest` instead, so the copy cannot drift.
- Silence and idle skip in `Processor::process`:
  - Compute `const bool voicesIdle = !voicePool_.isAnyVoiceActive();` and `const bool couplingIdle = couplingEngine_.getActiveResonatorCount() == 0;` (the accessor exists at `sympathetic_resonance.h:365`).
  - Keep a member `int couplingIdleSamples_`. Reset it to 0 whenever `!(voicesIdle && couplingIdle)`, otherwise add `data.numSamples`.
  - The per-sample coupling loop (today :693-708) runs only when `!couplingEngine_.isBypassed() && couplingIdleSamples_ < couplingIdleHoldSamples_`. Set `couplingIdleHoldSamples_` in `setupProcessing` to `(0.002 + 0.1) * sampleRate_`: the 2 ms maximum coupling delay plus a 100 ms hold.
  - When the skip starts, call `couplingDelay_.reset()` so the next hit starts from an empty delay line.
  - After the master gain block (today :725-737), replace the flag expression at :739-741. When `voicesIdle && couplingIdle`, scan the main block. If every `|outL|` and `|outR|` is below `1e-7f` (-140 dBFS), zero both buffers and set `silenceFlags = 3`; otherwise set 0. When voices or resonators are active, set 0.
  - `busLimiter_` keeps running every block (its oversampler state must flush), as today.
- Reset `couplingIdleSamples_` in `setupProcessing`, in `setActive(true)` and wherever the processor resets `energyEnvelope_` (Grep `energyEnvelope_ = 0`).
- The aux-bus flags (:668-685) stay as they are.

**3. Build and gate.** Standard workflow. pluginval is mandatory: silence flags are host-visible.

**4. Acceptance.**
- 192 kHz release goes from 52 ms to 226.7 ms within 5 %.
- Idle coupled blocks go from 469 of 469 unflagged to at least 95 % flagged.
- Every existing `[coupling]` case is green, including "SC-007 energy limiter caps below -20 dBFS".

### T2.27 — The pan law is exactly unity at centre and the coupling feed does not depend on pan

**Findings:** MB-066 (low, CONFIRMED).
**Flags:** MACOS; PRESET-IMPACT (added: `tools/membrum_preset_generator.cpp` pans many factory pads off centre (for example `pads[1].pan = 0.58`, `pads[3].pan = 0.44`) in kits whose `globalCoupling` is above 0, so the coupling drive of those pads changes by up to about -0.07 dB at those pans and up to 3 dB at a hard pan).
**Prerequisites:** none. T2.25 runs first in plan order, so the gains are latched in `VoiceMeta`.
**Estimate:** 60 min.

**1. Failing test.**
- First refactor `VoicePool::panGainsForNote` (`voice_pool.cpp:744-767`). Move its arithmetic unchanged into a new public `static void equalPowerPanGains(float panNorm, float& gainL, float& gainR) noexcept` on `VoicePool`, and have `panGainsForNote` call it.
- Then, in `plugins/membrum/tests/unit/voice_pool/test_per_pad_pan.cpp`, add `TEST_CASE("VoicePool per-pad pan: centre gains are exactly unity", "[voice_pool][pan]")`. It asserts `gainL == 1.0f && gainR == 1.0f` for `panNorm = 0.5f`, and `gainL * gainL + gainR * gainR` within 1e-6 of 2 at 0.0, 0.25, 0.75 and 1.0. Today the centre gives 0.99999994 on both channels.
- In `test_coupling_integration.cpp`, add `TEST_CASE("Coupling integration: coupling excitation does not depend on pad pan", "[coupling][pan]")`.
  - Use two fixtures at 48 kHz with `kGlobalCouplingId` 1.0. In one, set pad 0's pan to 0.5; in the other, to 0.0 (through `voicePoolForTest().setPadConfigField(0, kPadPan, ...)`).
  - Play note 36 at 1.0 and render 200 ms. Record `energyEnvelopeForTest()` after every block, and assert the two traces agree within 1e-6 relative.
  - Today the hard-panned pad drives coupling 3 dB lower: the feed is `(outL + outR) * 0.5` (`processor.cpp:700`), taken after the pan.

**2. Fix.**
- In `equalPowerPanGains`, add `if (panNorm == 0.5f) { gainL = 1.0f; gainR = 1.0f; return; }` before the trigonometry: a branch, not arithmetic (root CLAUDE.md fast-math rule). Keep the constant-power law with unity at centre and +3 dB at a hard pan; the comment and `test_per_pad_pan.cpp:135-137` state that law on purpose. Rewrite the comment at `voice_pool.cpp:759-762` and the header of `test_per_pad_pan.cpp:13-15`: centre is exactly unity because of the branch, and a hard pan puts +3 dB into one channel of the bus limiter.
- Coupling feed from the pre-pan voice sum:
  - Add `std::unique_ptr<float[]> couplingFeed_` to `VoicePool`, allocated in `VoicePool::prepare` next to `scratchL_` (the only allocation point).
  - Zero it at the start of both `processBlock` overloads. Add `scratch[i]` (pre-pan) for every main voice and for every shadow voice's `liveCount` samples, in the same loops that add to `outL` / `outR`.
  - Expose `[[nodiscard]] const float* couplingFeed() const noexcept`.
- In `Processor::process`, the coupling loop reads `const float mono = feed[s];` with `const float* feed = voicePool_.couplingFeed();` instead of `(outL[s] + outR[s]) * 0.5f`.
  - At centre pan this is bit-identical to today, because `(x + x) * 0.5f == x` exactly with unity gains.
  - Rename the existing case "Coupling integration: mono sum (L+R)/2 feeds delay then engine" (`test_coupling_integration.cpp:287`) and its comment to say "pre-pan voice sum". Its assertion stays.

**3. Build and gate.**
- MACOS: run `node tools/check-portability.js plugins/membrum/src/voice_pool/voice_pool.cpp plugins/membrum/src/processor/processor.cpp` and `tools/clangcl-fastmath-build.ps1 -Targets membrum_tests`. The exact-unity case is the one the macOS leg must keep. If the proxy cannot show it, write "unverified on macOS until CI" in the commit message.
- PRESET-IMPACT: run the kit probe before and after. Expect changes only in the coupling contribution of panned pads in coupled kits.

**4. Acceptance.**
- The centre gain is exactly 1.0f on both channels (was 0.99999994).
- The coupling envelope traces for pan 0.0 and 0.5 agree within 1e-6 relative (today they differ by 3 dB).
- The probe rows `level:pan=0.0` / `0.5` / `1.0` (L/R RMS -19.91 / -240, -22.92 / -22.92, -161.08 / -19.91 dBFS) are unchanged.

### T2.28 — VoicePool runs at the host sample rate instead of a private clamp

**Findings:** MB-016 (low, CONFIRMED).
**Flags:** none.
**Prerequisites:** none.
**Estimate:** 60 min.

**1. Failing test.** `plugins/membrum/tests/unit/processor/test_coupling_integration.cpp` is the TU with a rate-aware `Processor` fixture; put the case there or copy the fixture into a new `plugins/membrum/tests/unit/processor/test_sample_rate_range.cpp` (add it to the CMake list). The case is `TEST_CASE("Processor: pad pitch is the same at 384 kHz and 16 kHz as inside the clamp range", "[processor][sample-rate]")`.
- For each pair of rates (192 kHz vs 384 kHz, and 44.1 kHz vs 16 kHz), use a fixture with the default kit. Make pad 0 sample-rate invariant inside today's range, as the audit's `srate-kick:tension0+pitchEnvOff+noise/click0` probe did (0.1 cents across 44.1-192 kHz): `kPadTensionModAmt` 0, `kPadTSPitchEnvTime` 0, `kPadNoiseLayerMix` 0, `kPadClickLayerMix` 0, set through `voicePoolForTest().setPadConfigField`.
- Play note 36 at 1.0 and render 1 s. Find the fundamental with `findPeakFrequency` (`plugins/membrum/tests/unit/bodies/body_test_helpers.h`; include it, do not copy it) over 60-100 Hz with 2048 bins.
- Assert that the two rates agree within 5 cents. Today 384 kHz plays about 1200 cents low (voices prepared at 192 kHz) and 16 kHz about 560 cents high.

**2. Fix.**
- `plugins/membrum/src/voice_pool/voice_pool.cpp`, `VoicePool::prepare` (today :52-58): delete the `std::clamp(sampleRate, 22050.0, 192000.0)` line and its comment, so every voice, the fast-release coefficient and `sampleRate_` use the host rate the processor already uses for the coupling engine, delay and limiter (`processor.cpp:1081-1087`).
- Update the precondition note in `specs/138-membrum-phase3-polyphony/contracts/voicepool_api.md:19` ("implementation clamps if outside") to say that the pool runs at the host rate.
- If the 384 kHz or 16 kHz render produces a non-finite sample, a mode at or above Nyquist, or an out-of-bounds write (run that one case once under the ASan build, root `CLAUDE.md` "AddressSanitizer"), stop and report the component. Do not reintroduce a clamp.

**3. Build and gate.** Standard workflow, plus the one ASan run of the new case named above.

**4. Acceptance.**
- 384 kHz vs 192 kHz and 16 kHz vs 44.1 kHz agree within 5 cents (was about 1200 and 560 cents).
- Every `[sample-rate]` and body sample-rate sweep case stays green.

### T2.29 — The processor reports a tail

**Findings:** MB-095 (low, CONFIRMED).
**Flags:** none.
**Prerequisites:** none. T2.26 runs first in plan order and makes the main-bus silence flag reliable, which this task relies on.
**Estimate:** 30 min.

**1. Failing test.** New file `plugins/membrum/tests/unit/processor/test_processor_host_contract.cpp` (add it to the CMake list), `TEST_CASE("Processor reports an infinite tail and relies on silence flags", "[processor][host-contract]")`. Construct and initialize a `Membrum::Processor`, then assert `getTailSamples() == Steinberg::Vst::kInfiniteTail`. Today it returns `kNoTail`, the SDK default (`extern/vst3sdk/public.sdk/source/vst/vstaudioeffect.h:84`).

**2. Fix.** `plugins/membrum/src/processor/processor.h`, class `Processor`: add `Steinberg::uint32 PLUGIN_API getTailSamples() override { return Steinberg::Vst::kInfiniteTail; }`, as Iterum does (`plugins/iterum/src/processor/processor.h:91`). Add a comment saying that voices ring for up to about 35 s (decay clamp 5 s) and coupling resonators after them, and that idle detection is the per-block silence flag (`Processor::process`, made reliable by T2.26).

**3. Build and gate.** Standard workflow. pluginval is mandatory: the tail is host-visible.

**4. Acceptance.** The new case passes, and pluginval at strictness 5 exits 0.

### T2.30 — Stepped parameters decode with the SDK's convention in the processor

**Findings:** MB-080 (low, CONFIRMED).
**Flags:** none. Kit data stores choke groups, buses and polyphony as integers (`state_codec.cpp:102-103, 151-152, 247-248`), so no stored value changes. Only off-grid values, from interpolated automation or a generic host slider, decode differently.
**Prerequisites:** none.
**Estimate:** 60 min.

**1. Failing test.** New file `plugins/membrum/tests/unit/vst/test_stepped_param_decode.cpp` (add it to the CMake list), `TEST_CASE("Stepped parameters: processor and controller decode every normalized value to the index the host displays", "[vst][param-decode]")`.
- Initialize a `Membrum::Controller`. For each stepped ID, take the expected index from the controller's own parameter object: `ctl.getParameterObject(id)->toPlain(v)`, which goes through `Steinberg::FromNormalized` (`futils.h:96`). The IDs are:
  - per-pad Choke Group `padParamId(0, kPadChokeGroup)` (stepCount 8);
  - per-pad Output Bus `padParamId(0, kPadOutputBus)` (15);
  - `kMaxPolyphonyId` (RangeParameter 4..16, stepCount 12);
  - `kChokeGroupId` (StringList, 9 entries);
  - `kSelectedPadId` (0..31).
- Sweep `v` from 0 to 1 in steps of 1e-3:
  - Per-pad IDs: `VoicePool::setPadConfigField`, then read `padConfig(0).chokeGroup` / `.outputBus`.
  - `kMaxPolyphonyId`, `kChokeGroupId`, `kSelectedPadId`: a `Processor` with a parameter change built with `tests/test_helpers/vst_param_changes.h`, then read `voicePoolForTest().maxPolyphony()`, `voicePoolForTest().padConfig(selected).chokeGroup` and `selectedPadIndexForTest()`.
  - Controller side for `kSelectedPadId`: `ctl.setParamNormalized(kSelectedPadId, v)`, then read a new `selectedPadIndexForTest()` accessor on `Controller` (add it next to `cachedMetersForTest`).
- Assert equality at every step.
- Today it fails at, for example, Choke 0.07 (host "None", DSP CG1), Max Polyphony 0.54 (host 11, DSP 10) and Selected Pad 0.02 (host pad 1, processor pad 2).

**2. Fix.**
- Add one helper `[[nodiscard]] constexpr int stepFromNormalized(double norm, int numSteps) noexcept` to `plugins/membrum/src/plugin_ids.h`. Run `grep -rn "stepFromNormalized" dsp/ plugins/` first. Its body clamps `norm` to [0, 1] and returns `std::min(numSteps, static_cast<int>(norm * (numSteps + 1)))`, the body of `Steinberg::FromNormalized`; cite `futils.h:96` in a comment. Use it at:
  - `voice_pool.cpp`, `setPadConfigField` (today :641-656): `kPadChokeGroup` (`stepFromNormalized(v, 8)`) and `kPadOutputBus` (`stepFromNormalized(v, 15)`);
  - `processor.cpp`, `processParameterChanges`: `kMaxPolyphonyId` (today :241-249: `4 + stepFromNormalized(v, 12)`), `kChokeGroupId` (:380-387) and `kSelectedPadId` (:395-400);
  - `controller.cpp`, the `kSelectedPadId` branch of `setParamNormalized` (today :846-850). The controller and the processor must keep agreeing on the selected pad, or proxy writes go to different pads;
  - `controller_view_sync.cpp`, `updateOutputBusTooltip` (today :74-77, `lround(norm * 15)`).
- Leave `kVoiceStealingId` (:252, `int(v * 3)`) and the exciter and body selectors (:281-295): they already floor and match the SDK.
- Grep `lround\|+ 0.5f)` in `plugins/membrum/src` for any other decode of a stepped ID and convert it the same way. List what you converted in the commit message.

**3. Build and gate.** Standard workflow.

**4. Acceptance.**
- The sweep matches at all 1001 points for all six decodes.
- Grid values `k / n` decode to `k` as before: the existing state, preset and choke tests are green.

### T2.31 — MetersBlock keeps peaks between UI ticks and reports bus activation to the controller

**Findings:** MB-065 (low, CONFIRMED), MB-102 (low, CONFIRMED), MB-100 (low, CONFIRMED).
**Flags:** UI.
**Prerequisites:** none.
**Estimate:** 150 min.

**1. Failing test.** New file `plugins/membrum/tests/unit/controller/test_meters_bridge.cpp` (add it to the CMake list), tags `[controller][meters]`.
- `TEST_CASE("Meters: the peak of any block delivered between UI ticks reaches the meter", "[controller][meters]")`.
  - Initialize a `Controller`. Build three `MetersBlock`s with `peakL` / `peakR` = 0.9, 0.1 and 0.1 and pad-glow bucket 20 / 0 / 0 for pad 3. Wrap them in `Steinberg::Vst::DataExchangeBlock`s and deliver them in one `onDataExchangeBlocksReceived` call.
  - Then deliver one more block with peak 0.05 in a second call, before the timer runs.
  - Call the timer's meter step directly: factor the timer body's `updateMeterViews(...)` call (`controller.cpp`, the `pollTimer_` lambda, today :1511-1525) into `void Controller::consumeMetersForUiTick() noexcept`, and add a test accessor for the values it pushed, for example `lastPushedMetersForTest()`.
  - Assert that the pushed `peakL >= 0.9` and that pad 3's glow mirror reads bucket 20. Today the last block wins (`controller.cpp:1716-1724`), so it reads 0.05 and bucket 0.
- `TEST_CASE("Meters: the processor keeps the peak of blocks it could not send", "[processor][meters]")`.
  - Use a `Processor` whose DataExchange queue is full. Fill the 4-block queue by never letting the controller side drain it; if the SDK fallback cannot be held full in a test, add `Processor::metersPendingPeakForTest()`.
  - While the send fails, render the block that contains a note-36 hit at velocity 1.0, and record that block's true main-bus peak from the output buffer. Then render a quiet block that is sent. Assert that the sent block's `peakL` is at least the recorded peak. Today it carries only the quiet block's peak.
- `TEST_CASE("Meters: KitMetersView holds a peak and falls at 20 dB per second", "[ui][meters]")`. Call `setPeaks(0.95f, 0.95f)`, then `setPeaks(0.0f, 0.0f)` once. `peakLForTest()` must be within 1e-4 of `0.95 * 10^(-20 / 20 / 30)` (one 33 ms tick), and after 30 more calls it must be below 0.1.
- `TEST_CASE("Meters: bus activation reaches the controller through MetersBlock", "[controller][meters][output_routing]")`. Deliver a `MetersBlock` whose `busActiveMask` has bits 0 and 2 set. Assert `isBusActive(2)` and `!isBusActive(3)`. Today nothing in production calls `notifyBusActivation` (`controller.cpp:923` is reached only from tests), so `isBusActive(2)` is false.

**2. Fix.**
- `plugins/membrum/src/processor/meters_block.h`:
  - Append `std::uint16_t busActiveMask = 1;` after `padGlowBuckets`. Bit `b` means output bus `b` is active.
  - Change the `static_assert(sizeof(MetersBlock) == 44)` to the new size (48 with padding) and update the comment.
  - `Processor::connect`'s block size (`processor.cpp:1144-1150`) rounds up to 32 and stays 64; check the `static_assert` there still holds.
  - Update the contract comment at the top to name the new field.
- `Processor::process`, the MetersBlock section (today :788-850):
  - Keep members `float metersPeakAccL_`, `metersPeakAccR_` and `std::array<std::uint8_t, kNumPads> metersGlowAcc_`. Each block, fold that block's peaks (after master gain, as today) and the glow snapshot into them with `std::max`.
  - Write the accumulated values into the block. Reset the accumulators only when `getCurrentOrNewBlock()` returned a valid block and `sendCurrentBlock()` was called.
  - Fill `mb.busActiveMask` from `busActive_` (bit `b` set when `busActive_[b]`).
  - Reset the accumulators in `setActive`.
- `plugins/membrum/src/controller/controller.cpp`, `onDataExchangeBlocksReceived` (today :1710-1738):
  - Replace the last-block-wins memcpy loop with a fold into a member `MetersBlock pendingMeters_`. Take the max of `peakL` and `peakR` and of each `padGlowBuckets[p]`; take the last value of `activeVoices`, `cpuPermille` and `busActiveMask`.
  - Keep `cachedMeters_` as the last received block, for `cachedMetersForTest`.
  - Publish the folded glow buckets to `padGlowMirror_`.
  - If `busActiveMask` differs from the last applied mask, call a new `applyBusActivationMask(std::uint16_t mask)`. It sets `busActive_[b]` for every bit (bus 0 always true) and calls `updateOutputBusTooltip()`.
- `consumeMetersForUiTick()`, called from the poll timer instead of `updateMeterViews(cachedMeters_)`, pushes `pendingMeters_` through `updateMeterViews`, then zeroes its peaks and glow buckets.
- `plugins/membrum/src/ui/kit_meters_view.cpp`, `setPeaks` (today :29-34): hold and fall. `peakL_ = std::max(std::clamp(peakL, 0.0f, 1.0f), peakL_ * kFallPerTick)`, the same for R, with `constexpr float kFallPerTick = 0.92612f;` (20 dB/s at the 30 Hz tick, i.e. `10^(-1/30)`). The clip colour logic (> 0.9) stays.
- FR-043 decision, made here and to be written into the commit message and a comment above `notifyBusActivation`:
  - The production path (`applyBusActivationMask`) updates `busActive_` and the tooltip only. It does not run the FR-043 loop that rewrites pads' Output Bus to Main (`controller.cpp:934-949`).
  - The audible purpose of FR-043 is already met by the processor: every voice is always written to the main bus, and an inactive aux bus is skipped (`VoicePool::processBlock`, the `busActive[bus]` check).
  - That loop calls `EditControllerEx1::setParamNormalized` without `performEdit`, so it would split the controller's value from the processor's, and it would permanently rewrite a user's saved routing whenever a host toggles a bus.
  - `notifyBusActivation` keeps its current behaviour for the tests that call it.
  - If the reviewer wants the reset in production, that is a user ruling; do not wire it without one.
- Fix the header comment at `controller.h:107-111` ("provided for testing") to name the production source.

**3. Build and gate.**
- Add `Membrum` to the build.
- UI flag: run `node tools/uidesc-inventory.js plugins/membrum/resources/editor.uidesc --check` and `node tools/gen-screenshots.js membrum`. Expect no screenshot change: the meter is empty at rest.
- pluginval is mandatory: the DataExchange block size is host-visible.

**4. Acceptance.**
- The three-block delivery shows 0.9, not 0.05.
- A peak from a dropped block reaches the next sent block.
- The meter holds and falls at 20 dB/s.
- `isBusActive(2)` follows the processor's `activateBus`.
- The existing `[output_routing]` and bus-activation cases are green.

### T2.32 — Inline kit and pad browsers track the current preset by path and clear stale names

**Findings:** MB-103 (low, CONFIRMED).
**Flags:** UI.
**Prerequisites:** none.
**Estimate:** 90 min.

**1. Failing test.** New file `plugins/membrum/tests/unit/ui/test_inline_preset_browser.cpp` (add it to the CMake list).
- First refactor `InlinePresetBrowserView::cyclePreset` (`plugins/membrum/src/ui/preset_inline_browser_view.h:112-139`). Move today's index computation unchanged into a public `static int cycleIndex(const std::vector<Krate::Plugins::PresetInfo>& presets, const std::filesystem::path& currentPath, const std::string& currentName, int direction) noexcept`. For now it ignores `currentPath` and matches by name, as today.
- `TEST_CASE("Inline preset browser: Next from the second of two same-named presets reaches the next preset", "[ui][preset_browser]")`.
  - Build a list of three `PresetInfo`: name "X" path `user/X.vstpreset`, name "X" path `factory/Acoustic/X.vstpreset`, name "Y" path `factory/Acoustic/Y.vstpreset`.
  - Assert that `cycleIndex(list, "factory/Acoustic/X.vstpreset", "X", +1) == 2` and `cycleIndex(list, same, "X", -1) == 0`.
  - Today the first case returns 1, which is why Next is stuck on the duplicate.
- `TEST_CASE("Inline preset browser: a project load clears the kit name and a pad switch shows that pad's name", "[controller][preset_browser]")`.
  - Initialize a `Controller`. Add test accessors `lastKitPresetNameForTest()` and `padPresetNameForTest(int pad)`.
  - Drive a kit-browser load the way `tests/unit/preset/test_kit_browser_load.cpp` does, and assert the kit name is set. Call `setComponentState` with a stream from a `Processor::getState`, and assert the kit name is empty. Today it keeps the browser-loaded name (`controller_state.cpp:64-99` never touches it).
  - Drive a pad-preset load on pad 2 the way `tests/unit/preset/test_pad_preset.cpp` does, and assert `padPresetNameForTest(2)` is that preset's name and `padPresetNameForTest(5)` is empty. Today there is one global `lastPadPresetName_`.

**2. Fix.**
- `preset_inline_browser_view.h`:
  - Store `std::filesystem::path currentPath_` next to `currentName_`. Add `void setCurrentPreset(const std::string& name, const std::filesystem::path& path)`; `setCurrentPresetName(name)` keeps working and clears the path.
  - `cycleIndex` matches by `p.path == currentPath` when `currentPath` is not empty, and falls back to the name only when it is empty. When nothing matches, it starts at 0 / n-1 as today.
  - `cyclePreset` calls `cycleIndex(presets, currentPath_, currentName_, direction)`.
  - The rescan on every click (`manager_->scanPresets()`) stays: it picks up newly saved presets, and `PresetManager` (`plugins/shared/src/preset/preset_manager.h`) has no cached accessor. Changing that is shared code outside this finding's fix.
- `controller.h` / `controller.cpp`:
  - Replace `lastPadPresetName_` with `std::array<std::string, kNumPads> padPresetNames_` and `std::array<std::filesystem::path, kNumPads> padPresetPaths_`, and add `std::filesystem::path lastKitPresetPath_`.
  - The kit load provider (today :743-753) stores `info.name` and `info.path`. A kit load replaces every pad's sound, so it also clears all pad names and paths.
  - The pad load provider (today :764-776) stores into the slot of the pad being loaded, the selected pad.
  - `createCustomView` (today :1213 and :1226) passes name and path through `setCurrentPreset`; the pad browser shows the selected pad's entry.
- `controller_state.cpp`, `Controller::setComponentState` (today :64-99): clear the kit name and path and all pad names and paths.
- The `kSelectedPadId` branch of `Controller::setParamNormalized` (today :846) updates which pad entry the pad browser shows.
- Both of these can run off the UI thread (audit finding 12), so they must not touch the views directly. Add a `kRefreshPresetBrowserNames` bit to `ViewRefreshFlags` (`controller.h:325-331`) and call `requestViewRefresh(kRefreshPresetBrowserNames)`. `applyViewRefresh` (`controller_view_sync.cpp:117-123`) then pushes the current kit entry and the selected pad's entry into `kitInlineBrowser_` / `padInlineBrowser_` when they are non-null.

**3. Build and gate.**
- Add `Membrum` to the build.
- UI flag: run `node tools/uidesc-inventory.js plugins/membrum/resources/editor.uidesc --check` and `node tools/gen-screenshots.js membrum`. Expect no screenshot change, since the label is empty at rest.

**4. Acceptance.**
- Next from the second duplicate reaches "Y".
- A project load leaves the kit label empty.
- The pad label follows the selected pad.
- The existing `[preset]` cases (kit browser load, pad preset) are green.

## Phase 3 — Measured defects not already covered

Clear what the measurement lane and the portability review found and no earlier task owns: test guards that are vacuous under macOS fast-math, the failing uidesc lint, pad classification, limiter hits on resonant pads and the inexact Mode Stretch neutral point.

### T3.1 — Membrum's NaN/Inf and denormal test guards work under macOS fast-math

**Findings:** MB-014 (medium, CONFIRMED), MB-132 (medium, CONFIRMED).
**Flags:** MACOS.
**Prerequisites:** none.
**Estimate:** 180 min.

This is a test-only task: no plugin source changes, so pluginval is skipped.

**1. Failing test.** The red is the repo's own ratchet with Membrum's debt removed.
- Record `node tools/lint-finite-bitmask.js --list` and `node tools/lint-nonfinite-symbols.js --all`, filtered to `plugins/membrum/tests`. At the audit:
  - 30 bit-mask hits in 29 files (`tools/lint-finite-bitmask-baseline.json:41-69`: `approval/test_phase1_regression.cpp`, four `perf/*.cpp`, `unit/architecture/test_body_bank.cpp` and `test_exciter_bank.cpp`, `unit/processor/membrum_processor_tests.cpp` (2), `test_coupling_fuzz.cpp`, `test_default_kit.cpp`, `unit/test_exciter_body_matrix.cpp`, `unit/test_stability_guard.cpp`, `unit/tone_shaper/test_pitch_envelope_808.cpp`, five `unit/unnatural/*.cpp` and eleven `unit/voice_pool/*.cpp`);
  - 8 `std::isfinite` hits (`tools/lint-nonfinite-symbols-baseline.json:9-10`: `test_coupling_phase9_verification.cpp` 6, at :196-197, :285-286, :318-319; `test_secondary_shell_paths.cpp` 2, at :98-99).
- Delete those 31 Membrum entries from the two baseline files by hand. Both lints must now fail, naming exactly those files. That is the red.
- Negative control on the fast-math proxy:
  - Temporarily make one sentinel see a NaN. In `test_exciter_body_matrix.cpp`'s render loop, overwrite one output sample with a NaN built from the bit pattern `0x7FC00000u` through a `volatile std::uint32_t` and `std::memcpy`.
  - Build with `tools/clangcl-fastmath-build.ps1 -Targets membrum_tests` and run that case by name. Record whether it goes red before the fix (the clone survived the proxy) or stays green (folded). Read the script's blind spot first.
  - Never commit the injection.

**2. Fix.**
- In every file listed above, replace the body of the local helper (`isNaNOrInfBits`, `isFiniteSample`, `isNaNBits` / `isInfBits` and the like) with a call to `::Krate::DSP::detail::isFinite(x)` (or `isNaN` / `isInf` to keep the helper's meaning), and include `<krate/dsp/core/db_utils.h>`. Keep each helper's name and call sites.
  - `plugins/membrum/tests/unit/bodies/body_test_helpers.h:25-29` is the pattern already in the tree. Copy its two-line comment and delete the false comments that say the mask "stays correct under -ffast-math" (for example `test_exciter_body_matrix.cpp:41-43`).
- In `test_coupling_phase9_verification.cpp` and `test_secondary_shell_paths.cpp`, replace `std::isfinite(x)` with `::Krate::DSP::detail::isFinite(x)`; the double overload exists at `db_utils.h:126`.
- In `test_fast_release_denormals.cpp`, the denormal classifier (:38-48) reads its bits through `::Krate::DSP::detail::opaqueFloatBits(x)` instead of `std::memcpy`, and its finite helper (:35) uses `detail::isFinite`.
- Where a case builds a NaN or Inf to inject, keep building it from a bit pattern through a `volatile` (as `unit/exciters/test_feedback_exciter.cpp:177` explains). Never use `numeric_limits<float>::quiet_NaN()` / `infinity()`.
- `plugins/membrum/tests/CMakeLists.txt`:
  - Rewrite the comment at :250-252 (the bit-manipulation premise is false under Apple clang; the guards use `detail::isFinite`).
  - Change the flag list at :265 to `"-fno-fast-math;-fno-finite-math-only;-ffp-contract=off"`.
- Run `node tools/lint-finite-bitmask.js --update-baseline` and `node tools/lint-nonfinite-symbols.js --update-baseline`. The Membrum entries must be gone from both files, and no other plugin's count may change. If one does, revert that part of the regenerated file and report it.

**3. Build and gate.**
- Build `membrum_tests` only; no plugin source changed.
- The gate runs every case of the changed TUs. The step-4 filter excludes the `perf/*.cpp` benchmark cases, so run them once by name, alone, through `node tools/run-cpu-tests.js membrum_tests`.
- MACOS:
  - `node tools/lint-finite-bitmask.js`, `node tools/lint-nonfinite-symbols.js` and `node tools/lint-ieee-flag-sets.js` must pass;
  - `node tools/check-portability.js` on the changed `.cpp` files;
  - repeat the proxy negative control: the injected NaN must now turn the matrix case red under `clangcl-fastmath-build.ps1`, then remove the injection.
  - `tools/macos-fastmath-repro.sh` takes DSP TUs only and does not cover these files. Write "unverified on macOS until CI" in the commit message for the plugin TUs.

**4. Acceptance.**
- Zero Membrum entries in `lint-finite-bitmask-baseline.json` (was 29 files, 30 hits) and `lint-nonfinite-symbols-baseline.json` (was 2 files, 8 hits).
- Both lints and `lint-ieee-flag-sets.js` are green.
- The CMake IEEE block carries all three flags.
- `membrum_tests` reads `GATE GREEN`.
- The proxy negative control goes red with the injection and green without it.

### T3.2 — uidesc-inventory --check passes for Membrum's editor

**Findings:** MB-195 (low, CONFIRMED).

**Flags:** UI.

**Prerequisites:** none.

**Estimate:** 90 minutes.

**Context.** `node tools/uidesc-inventory.js plugins/membrum/resources/editor.uidesc --check` exits 1 with `UIDESC DEFECTS {"overlap":160}` (re-run at `4a14376d`: 23 in template `EditorDefault`, 31 in `SelectedPadSimple`, 106 in `SelectedPadAdvanced`). Every one of the 160 pairs a `FieldsetContainer` frame with a control drawn inside it: each fieldset is an empty, transparent, self-closing `FieldsetContainer` view and its controls are its siblings, not its children (for example `editor.uidesc:134-135`, `FieldsetContainer` "Macros" at 4,28 size 388x104, followed by the sibling labels and `ArcKnob`s at `:136-145`). A containment check run during planning found all 160 controls lie wholly inside their frame's rectangle, and no detail string occurs twice, so every allow entry has count 1. The tool has no built-in exemption for a fieldset frame (`tools/uidesc-inventory.js:105-118`); its mechanism for a deliberate overlap is `--allow <file>` (`applyAllowList`, `:179-195`: each entry needs `template`, `detail`, `reason`, optional `count`; an entry whose count no longer matches is an `allow-mismatch` defect). Disrumpo and Ruinae use it (`plugins/disrumpo/tests/uidesc_overlaps_allowed.json`, `plugins/ruinae/tests/uidesc_overlaps_allowed.json`) and register it as a CTest (`plugins/disrumpo/tests/CMakeLists.txt:271-278`, `add_test(NAME disrumpo_uidesc_inventory ...)`). Membrum has neither the file nor the CTest, so nothing gates its editor geometry. This task takes the allow-list route, the precedent of both other plugins; it does not move a single view, so the editor looks and behaves exactly as before.

**1. Failing test**

- File: `plugins/membrum/tests/CMakeLists.txt`. After `catch_discover_tests(membrum_tests REPORTER console)` (`:277`) and before the KRATE_TEST_PCH block, add the same registration Disrumpo has, with Membrum's paths:
  ```cmake
  # editor.uidesc geometry lint (Membrum audit T3.2, MB-195): no overlap outside the audited allow list, nothing out of bounds.
  find_program(KRATE_NODE_EXECUTABLE node)
  if(KRATE_NODE_EXECUTABLE)
      add_test(NAME membrum_uidesc_inventory
          COMMAND ${KRATE_NODE_EXECUTABLE} tools/uidesc-inventory.js plugins/membrum/resources/editor.uidesc --check
                  --allow plugins/membrum/tests/uidesc_overlaps_allowed.json
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  endif()
  ```
- Create `plugins/membrum/tests/uidesc_overlaps_allowed.json` with a `note` (as Disrumpo's file has: what the file is, that the detail strings are exact, that a count mismatch is a defect, and the audit reference MB-195) and an empty list: `"overlaps": []`.
- There is no Catch2 case for this; the test is the CTest `membrum_uidesc_inventory`. Build step 3 (the CMake change reconfigures the tree), then run it alone:
  `ctest --test-dir build/windows-x64-release -C Release -j 2 -R membrum_uidesc_inventory --output-on-failure`
  It must FAIL with `UIDESC DEFECTS {"overlap":160}`. If the number is not 160, the editor changed since this plan was written: count again by template and carry on with the new number.

**2. Fix**

- Fill `plugins/membrum/tests/uidesc_overlaps_allowed.json` with one entry per overlap line of the `--check` output: `{ "template": "<name in the brackets>", "detail": "<the text after the colon, exactly>", "count": 1, "reason": "Decorative FieldsetContainer frame drawn behind the controls of its group; the frame is transparent and not mouse-enabled (MB-195)." }`. Generate the entries with a throwaway Node script in the scratchpad directory (never committed) that parses the `--check` output with the regex `^overlap \[(\w+)\]: (.*?) at (-?\d+),(-?\d+),(\d+),(\d+) / (.*?) at (-?\d+),(-?\d+),(\d+),(\d+)$` and, for each line, asserts that one side's class is `FieldsetContainer` and that the other view's rectangle lies wholly inside the frame's. A line that fails either assertion is a real overlap: do not allow-list it, report it, and fix it in the uidesc (one view moved, by hand for up to three controls, otherwise through an XSLT stylesheet under `specs/membrum-audit/xslt/` as the UI flag says).
- Before writing "not mouse-enabled" in the reason, read the `FieldsetContainer` class (`plugins/shared/src/ui/`, grep `class FieldsetContainer`) and confirm the frame does not take mouse events; if it does, word the reason as "transparent frame drawn behind its controls" and report the mouse behaviour as a separate observation.
- Change no view in `editor.uidesc`.

**3. Build and gate**

- Build `membrum_tests` (step 3) so the CMake change reconfigures; no `Membrum` target is needed (no plugin source changed). Step 4's gate runs as written; it does not run CTest, so also run the `ctest ... -R membrum_uidesc_inventory` command above.
- UI flag: the editor is unchanged, so `node tools/gen-screenshots.js membrum` is not needed; run `node tools/lint-plugin-editor-baseline.js` (must pass).
- pluginval: skip (no plugin source changed). clang-tidy: run as written (no C++ changed, so it analyses nothing).

**4. Acceptance**

- `node tools/uidesc-inventory.js plugins/membrum/resources/editor.uidesc --check --allow plugins/membrum/tests/uidesc_overlaps_allowed.json` exits 0 and its output lists 160 allowed overlaps (baseline: exit 1, `{"overlap":160}`).
- `ctest ... -R membrum_uidesc_inventory` passes.
- Negative control, not committed: move one `ArcKnob` in a scratch copy of `editor.uidesc` by 1 px and run the tool on the copy with the allow file; it must exit 1 with an `allow-mismatch` and an `overlap` defect.

### T3.3 — PadCategory classifies snares and toms by what the pad is, not by its exciter

**Findings:** MB-183 (medium, CONFIRMED).

**Flags:** none. (The pad grid's K/S/T/H/P glyph comes from the same classifier, `categoryGlyphForConfig` in `plugins/membrum/src/ui/pad_grid_view.cpp:114-126`, so editor screenshots change; no view code changes, so the UI flag is not added, but step 3 below refreshes the screenshots.)

**Prerequisites:** none.

**Estimate:** 120 minutes.

**Context.** `classifyPad` (`plugins/membrum/src/dsp/pad_category.h:28-52`) applies, for a Membrane pad: Mallet exciter -> Tom (`:36-37`); any pitch envelope (`tsPitchEnvTime > 0`) -> Kick (`:39-40`); NoiseBurst exciter -> Snare (`:42-43`); otherwise Tom (`:45`). The redesigned snare uses the Impulse exciter with a boosted noise layer as its wires (`plugins/membrum/src/dsp/default_kit.h:87-141`, `case DrumTemplate::Snare`: `exciterType = Impulse` `:98`, `noiseLayerGain = 6.2f` `:120`, `wireCoupling = 0.45f` `:124`), so the default kit's snares (pads 2 and 4, notes 38 and 40) classify Tom, and any factory snare or hand drum with a pitch envelope classifies Kick. The kit-quality probe (`specs/membrum-audit/probes/kit-pads.csv`, column `padCategory`) counts 459 enabled factory pads as Kick 76, Snare 2, Tom 83, HatCymbal 75, Perc 223; the Membrane pads at notes 38 and 40 come out 9 Kick, 4 Tom, 1 Snare (Orchestral 38) plus Jazz Brushes 39 Snare; Hand Drums, Latin Perc and Tabla congas, bongos and timbales come out Kick. Consumers: the processor's category cache (`processor.cpp:114-116` at initialize, `:185-206` on exciter / body / pitch-env-time edits, `:987` and `:1011` on kit and pad loads) feeding `CouplingMatrix::recomputeFromTier1` (`coupling_matrix.h:42-77`: Kick -> Snare carries Snare Buzz, Tom -> Tom carries Tom Resonance), and the pad grid glyph. The matrix is not audible until T4.1, so this task changes no rendered sample; it is the prerequisite that gives T4.1 real Snare receivers and stops it from treating congas as kicks.

**1. Failing test**

- File `plugins/membrum/tests/unit/dsp/test_pad_category.cpp`, new case `TEST_CASE("classifyPad: default-kit roles (kick 36, snares 38/40, toms, hats)", "[pad_category]")`. Build the default kit with `DefaultKit::apply(pads)` on a `std::array<PadConfig, kNumPads>`, then REQUIRE: `classifyPad(pads[0]) == Kick`; `pads[2]` and `pads[4]` == `Snare`; pads 5, 7, 9, 11, 12, 14 == `Tom` (the list `test_coupling_integration.cpp:372` uses); every pad whose `bodyModel` is `NoiseBody` == `HatCymbal`. On current code pads 2 and 4 return `Tom`: that is the expected failure.
- File `plugins/membrum/tests/unit/preset/test_factory_kit_presets.cpp` (it already has `factoryPresetRoot()` at `:55`; reuse it and its parsing pattern, do not copy the loader from `test_kit_switch_infinite_ring.cpp`), new case `TEST_CASE("Factory kits: PadCategory matches each pad's role", "[preset][pad_category]")`. For every `.vstpreset` under the four category folders, read the kit blob with `Membrum::State::readKitBlob`, convert each pad with `Membrum::State::applyPadSnapshot(snapshot.pads[i], cfg)` (`state_codec.h:166`), skip disabled pads (`sound[51] < 0.5`, as `test_kit_switch_infinite_ring.cpp:457` does), and REQUIRE a missing preset folder to fail the case (never `WARN` and skip). Expected labels (pad index = note - 36):
  - Snare: the Membrane pad at note 38 in every Acoustic and Electronic kit that has one (Acoustic Studio Kit, Jazz Brushes, Orchestral, Rock Big Room, Vintage Wood, 808 Electronic Kit, 909 Drum Machine, LinnDrum CR-78, Trap Modern); Jazz Brushes 39 and 40; 808 Electronic Kit 40.
  - Kick: the Membrane pad at note 36 in every Acoustic and Electronic kit that has one.
  - Tom: Latin Perc 41 and 42 (`tools/membrum_preset_generator.cpp:4345` "Timbale Hi macho (5)", `:4365` "Timbale Lo hembra (6)": NoiseBurst Membrane, timbales have no snare wires), and every Membrane pad of Hand Drums, Latin Perc and Tabla unless its pitch envelope ends at or below the kick threshold chosen in step 2 (print each such exception with `INFO` and list it in the commit message).
  On current code this fails on every snare above except Orchestral 38 and Jazz Brushes 39, and on the hand-drum pads that classify Kick (for example `kit-pads.csv` "Percussive,Hand Drums,36,...,Impulse,Membrane,Kick").
- Keep the existing cases in `test_pad_category.cpp:60-134` and `test_coupling_integration.cpp:314-345` passing; if one asserts the old rule (for example a NoiseBurst Membrane pad with a pitch envelope classifying Kick, `test_pad_category.cpp:125-134`), change its expectation only where the new rule documents the role differently, and say which case changed and why in the commit message.

**2. Fix**

- `plugins/membrum/src/dsp/pad_category.h`, `classifyPad`. New Membrane rule chain, first match wins:
  1. Mallet exciter -> Tom (unchanged, `:36-37`).
  2. Kick: pitch envelope active (`tsPitchEnvTime > 0`) AND its end pitch at or below 90 Hz. The stored end is normalised as `Hz = 20 * 100^n` (`pad_config_apply.h:104-106`, `setPitchEnvEndHz`), so the test is `cfg.tsPitchEnvEnd <= kKickEndNorm` with `kKickEndNorm = log(90/20)/log(100) = 0.32660` written as a named constant with that derivation in its comment. Factory kicks end at 30-85 Hz (`tools/membrum_preset_generator.cpp` `tsPitchEnvEnd = toLogNorm(...)` on pad 0 of each kit: 50, 40, 30, 60, 46, 55, 85, 55); snares end at 110-160 Hz, toms and hand drums higher.
  3. Snare: wire evidence. Read, for every pad in the expected-label list of step 1, its `exciterType`, `noiseLayerMix`, `noiseLayerGain`, `noiseLayerCutoff`, `wireCoupling` and pitch-envelope end (print them from the new test with `INFO`, or read the generator), and choose the simplest rule over these fields that gives every expected label. The starting point is `noiseLayerGain > 1.0f || wireCoupling > 0.0f` (the redesigned snares: `pad_config.h:256-266` documents both fields as the snare wire's) OR the old recipe `exciterType == NoiseBurst` restricted by a field that separates the old NoiseBurst snares from the Latin Perc timbales. If no `PadConfig` field separates them, stop: an explicit per-pad category would be a new stored field (STATE-COMPAT) and needs the user's ruling; report the field table.
  4. Otherwise -> Tom.
  Non-Membrane rules (NoiseBody -> HatCymbal, else Perc) are unchanged. Update the enum comments (`:17-24`) and the header comment to state the new rules.
- `plugins/membrum/src/processor/processor.cpp`, `processParameterChanges`: the category is recomputed today only for `kPadExciterType`, `kPadBodyModel` and `kPadTSPitchEnvTime` (`:178-207`). Add the same recompute (`padCategories_[pad] = classifyPad(...)` then `recomputeCouplingMatrix()`) for every per-pad offset the new rule reads that has a parameter: `kPadTSPitchEnvEnd` (offset 14, `pad_config.h:54`), `kPadWireCoupling` (65, `:175`) and any other field the chosen snare rule uses. `noiseLayerGain` has no parameter offset (it is a stored sound slot); kit and pad loads already reclassify (`:987`, `:1011`).
- Do not change `CouplingMatrix`.

**3. Build and gate**

- Add `Membrum` to the build (plugin sources changed). Standard gate.
- The pad grid glyphs change: run `node tools/gen-screenshots.js membrum` and commit the refreshed `plugins/membrum/images/` with the task.

**4. Acceptance**

- Both new cases pass. Re-running the kit-quality probe (`audit_quality_probes.cpp.txt`, "Measuring with the audit probes") shows the note-38 Membrane pads of the nine Acoustic and Electronic kits as Snare (baseline: 1 of 9), no Hand Drums or Latin Perc Membrane pad as Kick unless listed as an exception, and every other column of `kit-pads.csv` unchanged (this task changes no audio; any changed peak, rms500, decay or centroid is a defect).

### T3.4 — A resonant tone-shaper filter stays inside the strike budget: Experimental FX pads stay under the limiter

**Findings:** MB-262 (low, PLAUSIBLE).

**Flags:** PRESET-IMPACT, VERIFY-FIRST.

**Prerequisites:** T1.15 (the tone-shaper SVF no longer glides from the slot's previous note), T2.16 (modeInject scaled to the body budget).

**Estimate:** 120 minutes.

**Context.** The strike normalisation (`plugins/membrum/src/dsp/drum_voice.h`, `probeStrikePeakExcited` `:1322-1363`, `measuredStrikeOutputGainExcited` `:1380-1387`, cached by `measuredStrikeOutputGain` `:1394-1412`) drives only exciter + half the click into the modal bank and scales the bank so its strike peak is `kBodyHeadroom = 0.5` (`:84`). Everything after the bank is outside that budget: modeInject, nonlinear coupling, then `toneShaper_.processSample` (`:559`, `:883`, `:896`, `:1059`). `ToneShaper::setFilterResonance` (`plugins/membrum/src/dsp/tone_shaper.h:158-165`) maps resonance [0,1] to Q in [0.707, 10] with no make-up, so res 0.55 gives Q 5.82. The limiter probe (`specs/membrum-audit/probes/limiter-pads.csv`, `limiter-probe-summary.txt`) measured the peak each stage adds (`dFilter`, `dModeInject`, dB) and the pre-limiter peak on a first hit on a fresh voice (`stalePreLimDb`) and on a correctly probed hit (`asisPreLimDb`): Experimental FX Kit 52 (FMImpulse/Shell, cutoff 0.48, res 0.55, panGain 1.280) dFilter 5.16, dModeInject 4.04, stale +0.20, asis -2.63 dBFS; EFX 58 4.56 / 2.15 / -0.45 / -1.68; EFX 40 dFilter 6.11, asis -3.14; EFX 55 5.32, -3.44; EFX 62 5.10 / 4.88, asis -2.86; 909 Drum Machine 38 dFilter 5.98. The limiter threshold is 0.8913 (-1 dBTP). `limiterAsis` is 0 for all 383 modal pads: the two crossings are first hits, where the skeptics attribute the difference to modeInject's random per-trigger phase (T2.16) and the slot-history filter glide (T1.15), not to the resonance itself. Hence VERIFY-FIRST: after T1.15 and T2.16 have landed, the test below decides whether a resonance make-up is needed at all.

**1. Failing test (doubles as the verification)**

- New file `plugins/membrum/tests/unit/voice_pool/test_resonant_filter_headroom.cpp` (add it to `add_executable(membrum_tests` in `plugins/membrum/tests/CMakeLists.txt`), `TEST_CASE("Resonant tone-shaper pads stay under the bus limiter threshold pre-limiter", "[voice_pool][gain_staging]")`.
- Load Experimental FX Kit and 909 Drum Machine from the factory folder (copy nothing: use the loader pattern of `plugins/membrum/tests/unit/preset/test_factory_kit_presets.cpp`; if two TUs now need it, move `factoryPresetRoot` and the blob read into one test header under `plugins/membrum/tests/unit/preset/` and include it from both), convert with `State::applyPadSnapshot` into a VoicePool's `padConfigsArray()`, prepare the pool at 48 kHz, block 256.
- Pads: EFX notes 40, 52, 55, 58, 62 and 909 note 38. For each pad and each velocity 100/127 and 127/127: (a) 10 hits, each on a freshly prepared pool (first hit on a fresh voice); (b) 10 hits 2 s apart on one pool. Render each hit for 300 ms through `VoicePool::processBlock` (the pool applies the pad's pan, so its output is the pre-limiter, pre-master bus signal). REQUIRE every hit's peak over L and R to be `< 0.8913f`. `INFO` the pad, velocity, hit index and peak in dBFS.
- Run it after T1.15 and T2.16 are committed.
  - If it PASSES: MB-262 is refuted for the limiter consequence. Keep the test, change no production code, record "refuted" with the per-pad worst peaks in the commit message and the completion checklist, and skip steps 2 to 4 except the gate.
  - If it FAILS: note the worst pad and peak, then add `TEST_CASE("Tone-shaper resonance adds no peak above the Butterworth response on the strike", "[tone_shaper][gain_staging]")` to `plugins/membrum/tests/unit/tone_shaper/test_tone_shaper.cpp`: a DrumVoice with the EFX 52 config rendered at resonance 0.55 and again at resonance 0.0, everything else equal, fresh voices; REQUIRE the peak ratio res 0.55 / res 0.0 to be at most +0.5 dB. Today it is about +5 dB (`dFilter` 5.16). Confirm it fails, then go to step 2.

**2. Fix (only if step 1 failed)**

- Decision this plan makes: Resonance changes TIMBRE, not peak LEVEL, on the strike, the same principle as the M-2 drive make-up (`tone_shaper.h:206-214`). The cutoff keeps its level effect (a darker filter is quieter), so the strike probe must not normalise the filter as a whole; it removes only the excess peak the resonance adds over a Butterworth response at the same cutoff.
- `ToneShaper` (`tone_shaper.h`): add a small value type, e.g. `ResonanceProbe`, created by a `const` member `makeResonanceProbe() const noexcept` that copies the configured `filter_` (`Krate::DSP::SVF`, `:528`) twice, one at the pad's Q and one set to `Krate::DSP::SVF::kButterworthQ`, plus a copy of `filterEnv_` (`:529`) reset and gated on, and runs the same control-rate cutoff schedule as `processSample` (`:447-457`: `filterCutoffHz_ * exp2(3 * filterEnvAmount_ * env)` every `kCutoffUpdateInterval` samples). Its `process(float x)` feeds both filters and tracks both absolute peaks; `excess()` returns `max(1, peakQ / peakButterworth)`. Before writing it, read `primitives/svf.h` and `primitives/adsr_envelope.h` and confirm both are copyable without heap memory (no `std::vector`, no `unique_ptr`); if either holds heap state, stop and report.
- Skip the probe (excess 1) when the filter is bypassed (`processSample`'s `filterBypass`, `:427-428`) or the resonance is 0.
- `DrumVoice::probeStrikePeakExcited` (`drum_voice.h:1322-1363`): create the probe before the loop, feed it each `lastBody` sample, and return the bank peak together with the excess (change the return to a small struct or add an out parameter). `measuredStrikeOutputGainExcited` then returns `kBodyHeadroom / (peak * excess)`.
- `StrikeNormKey` (built at `:1400-1406`): add the filter type, cutoff, resonance, env amount and the filter-env attack/decay/sustain so a change to any of them re-probes.
- The probe runs where it runs today (note-on, on the audio thread); it adds two SVF evaluations per probe sample over the existing 30 ms window and allocates nothing (check with `allocation_detector.h` in the test below).

**3. Build and gate**

- Add `Membrum` to the build. Standard gate. Add to the new tone-shaper case an allocation check around `noteOn` with the detector in `tests/test_helpers/allocation_detector.h` (read the header for its API; Membrum's tests already use it, e.g. `test_mode_stretch.cpp:160`); the count must be 0.
- PRESET-IMPACT: run the kit-quality probe and the limiter probe (`audit_limiter_probe.cpp.txt`) before and after; list, for every pad whose `tsFilterResonance > 0`, peak, rms500, decay, centroid and limiter margin before and after, and the near-duplicate pairs that appear or disappear. Show the table and stop for the user's ruling before the commit.

**4. Acceptance**

- Refuted path: the step-1 render test passes on the code after T1.15 and T2.16; the commit records the worst peak per pad (the baseline worst was +0.20 dBFS stale on EFX 52).
- Fix path: both step-1 cases pass; the limiter probe shows `dFilter` at most 0.5 dB on EFX 40, 52, 55, 58, 62 and 909 38 (baseline 4.56-6.11 dB) and no pad with `limiterStale` or `limiterAsis` set; the kit-quality table is in the commit message with the user's ruling.

### T3.5 — The Mode Stretch neutral point is exact by a branch, not by arithmetic that cancels

**Findings:** MB-133 (low, PLAUSIBLE).

**Flags:** MACOS, VERIFY-FIRST.

**Prerequisites:** none.

**Estimate:** 60 minutes.

**Context.** The per-pad default `modeStretch` is the normalised literal `0.333333f` (`plugins/membrum/src/dsp/pad_config.h:212`; also `controller.cpp:189`, `controller.cpp:354`, `processor.cpp:71`). `applyPadConfigToVoice` decodes it as `0.5f + clamp(n, 0, 1) * 1.5f` (`plugins/membrum/src/dsp/pad_config_apply.h:125-126`), which gives 0.99999946, not 1.0. `MembraneMapper` (`plugins/membrum/src/dsp/bodies/membrane_mapper.h:129-138`) subtracts `kUnityStretchNorm = (1.0f - 0.5f) / 1.5f` from `clamp((modeStretch - 0.5f) * (1.0f / 1.5f))`; that cancels exactly for an input of exactly 1.0f, so the residual comes from the decode: `stretchDelta = -3.5762786865234375e-7` (Math.fround), and the default Membrane stretch is `material * 0.3 - 3.6e-7`. The comments at `pad_config_apply.h:123-124` and `membrane_mapper.h:126-135` claim bit-identity at 1.0, which is false on the PadConfig path. The effect is inaudible; the defect is a neutral point produced by arithmetic instead of a branch (root `CLAUDE.md`, fast-math rules). One skeptic refuted it on the grounds that FR-055 defines identity within -120 dBFS; hence VERIFY-FIRST.

**1. Failing test (doubles as the verification)**

- File `plugins/membrum/tests/unit/unnatural/test_mode_stretch.cpp`, new `TEST_CASE("Mode Stretch default PadConfig decodes to exactly 1.0 and adds no Membrane stretch", "[unnatural][mode_stretch]")`:
  - Prepare a `DrumVoice` at 48 kHz, apply a default `PadConfig{}` with `applyPadConfigToVoice` (the function in `pad_config_apply.h`), and REQUIRE `voice.unnaturalZone().getModeStretch() == 1.0f`. Comparing with the constant is legitimate here: after the fix the value is produced by a branch, not computed.
  - Build two `VoiceCommonParams` from `makeDefaultParams()` (`plugins/membrum/tests/unit/bodies/body_test_helpers.h:167`) with `material = 0.5f`: one with `modeStretch` set to the voice's decoded value, one with `modeStretch = 1.0f`. REQUIRE `MembraneMapper::map(a, 0).stretch == MembraneMapper::map(b, 0).stretch` (same inline mapper in the same TU, identical inputs after the fix).
- On current code the first REQUIRE fails with 0.99999946. If it passes (for example because the compiler folded the decode), MB-133 is refuted: keep the test, change no production code, and record "refuted" with the printed value.

**2. Fix**

- `pad_config_apply.h:125-126`: decode through a branch. Add, next to the call, a named constant for the stored default (`kModeStretchUnityNorm = 1.0f / 3.0f`) and a tolerance of `1.0e-5f` in normalised units (0.0015 % of the range, about 1.5e-5 of stretch: below any host knob step), and set the stretch to exactly `1.0f` when `std::abs(n - kModeStretchUnityNorm) <= tolerance`, otherwise to `0.5f + n * 1.5f` as today. Write the reason in the comment: a neutral point must be exact by a branch (root `CLAUDE.md`, macOS fast-math rules; MB-133).
- Do not change the stored default literal `0.333333f` (that would change every saved state's meaning; the branch covers it).
- `membrane_mapper.h:123-138`: keep the arithmetic (it is exact for an exact 1.0f input) and correct the comment: identity holds because the decode produces an exact 1.0, and FR-055 requires identity within -120 dBFS (`specs/137-membrum-phase2-exciters-bodies/spec.md:240`). Correct `pad_config_apply.h:123-124` the same way.

**3. Build and gate**

- Add `Membrum` to the build. Standard gate.
- MACOS: run `node tools/lint-finite-bitmask.js`, `node tools/lint-nonfinite-symbols.js`, `node tools/lint-ieee-flag-sets.js`, `node tools/lint-apple-globals.js`, `node tools/check-portability.js plugins/membrum/tests/unit/unnatural/test_mode_stretch.cpp`, and `tools/clangcl-fastmath-build.ps1 -Targets membrum_tests` followed by the new case on that tree. No DSP TU changed, so `tools/macos-fastmath-repro.sh` has nothing to run; write "unverified on macOS until CI" in the commit message.

**4. Acceptance**

- The new case passes on the MSVC build and on the clang-cl `/fp:fast` proxy build. The default decoded stretch is exactly 1.0 (baseline 0.999999463558197) and the default Membrane stretch delta is 0 (baseline -3.5762786865234375e-7). No other test changes; rendered audio moves by less than -120 dBFS (the FR-055 identity cases in `test_mode_stretch.cpp:204-238` stay green).

## Phase 4 — Physical-model corrections

Bring coupling, the string, the mode tables, damping, the exciters, tension and the bus limiter in line with the cited literature, every new value carrying its source in the code and in a test, before any kit is retuned.

### T4.1 — Cross-pad coupling rings the unstruck pads: receiver resonators driven through the category matrix, so Snare Buzz, Tom Resonance and Coupling Amount are audible

**Findings:** MB-232 (high, CONFIRMED), MB-258 (high, CONFIRMED), MB-233 (medium, CONFIRMED), MB-004 (high, CONFIRMED), MB-167 (high, CONFIRMED), MB-168 (medium, CONFIRMED).

**Flags:** CROSS-PLUGIN IMPACT, PRESET-IMPACT, PHYSICS.

**Prerequisites:** T1.9 (allocator and coupling-engine bookkeeping), T1.19 (coupling lifecycle in `setupProcessing` / `setActive`), T2.17 (coupling resonators tuned to the settled pitch, no stale String modes), T3.3 (snares and toms classified by role). Read the commits of all four first: they move the code anchored below.

**Estimate:** 480 minutes.

**Context.** What the code does today (all at `4a14376d`):
- Only the STRUCK voice registers resonators, at its own first four bank modes: `VoicePool` note-on, `voice_pool.cpp:243-248` (`couplingEngine_->noteOn(slot, VP_VOICES[slot].getPartialInfo())`, gated only by `couplingAmount > 0.0f`), with `DrumVoice::getPartialInfo` (`drum_voice.h:1250-1259`). An idle pad has no resonator.
- `Processor::process` (`processor.cpp:688-708`) sends the main-bus mono sum through `couplingDelay_` into `couplingEngine_.process` (`Krate::DSP::SympatheticResonance`, `processor.h:139`), then `applyEnergyLimiter` (`processor.h:183`, threshold -20 dBFS), and adds the result to L and R. The engine's only gain is `setAmount(globalCoupling)` (`processor.cpp:408`, `:978`; `sympathetic_resonance.h:152-160`, -46..-26 dB). Membrum never calls `setDecay`, so Q is 316.2 (`sympathetic_resonance.h:128`) and the resonators ring T60 = 6.908 Q / (pi f): 11.6 s at 60 Hz, 6.95 s at 100 Hz, 0.35 s at 1 kHz.
- `kSnareBuzzId` / `kTomResonanceId` (`processor.cpp:411-424`) only store an atomic and call `recomputeCouplingMatrix` (`:131-143`), which fills `CouplingMatrix::effectiveGain_` (`coupling_matrix.h:42-77`: Kick -> Snare = snareBuzz x 0.05, Tom -> Tom = tomResonance x 0.05, each x amount[src] x amount[dst]). Nothing on the audio path reads it; the only reader is `couplingMatrixForTest` (`processor.h:81`). Per-pad Coupling Amount reaches audio only as the `> 0` gate.
- Measured (quality probe, `specs/membrum-audit/probes/param-audibility.csv`): Snare Buzz (271) and Tom Resonance (272) "bit-identical on 36, 38, 42"; Coupling Amount (offset 36) changes level, centroid, decay and band distance by under 0.1 dB / 1 %. Probe `coupling:kick-added-energy` (`probe-results.txt`): default kit, kick velocity 1, coupling 0 vs 1 with buzz 1 and tom 1: RMS change 0.00 dB, coupling-only component -60.98 dB relative to dry.
- The tests pass vacuously: SC-002 (`test_coupling_integration.cpp:240-285`) varies only Global Coupling; SC-008 (`:518-607`) strikes only pad 5, and the "receiver" pad 9 never sounds, so the energy it measures is pad 5's own partial; the Tom Resonance cases (`:347-405`) check matrix values only.

What the literature says (report section 5, sources for MB-232, MB-258, MB-233): sympathetic response in a kit is the forced response of an UNSTRUCK drum at its OWN modes, strongest where the source spectrum coincides with them, decaying with the receiver's own damping (Fletcher & Rossing, The Physics of Musical Instruments, 2nd ed., 1998, §18.10: the side drum's (01) mode has radiated most of its energy within about 0.5 s); a snare answers with wire buzz once its snare-side head passes a critical amplitude (F&R §18.13; Bilbao, Torin & Chatziioannou, "Numerical Modeling of Collisions in Musical Instruments", Acta Acustica 2012: wires launched by contact with the pressure-driven lower membrane; Rossing, "Acoustics of Drums", Physics Today 45(3), 1992: coupling through enclosed air and shell). Lehtonen et al. 2007, cited in `sympathetic_resonance.h:38-40` and `:148-149`, is a piano-string model and does not transfer.

Decision this plan makes (the default in the head's ruling table, T4.1: wire them, remove nothing): Membrum's coupling becomes a receiver model. Every enabled pad with `couplingAmount > 0` whose matrix column is non-zero owns persistent receiver resonators at its own first four natural mode frequencies, with its own damping; receiver j is driven by `globalCoupling x sum over sounding sources i of effectiveGain[i][j] x (pad i's voice output)`; a Snare receiver also drives a wire-noise burst through a critical-amplitude law. Membrum stops using `Krate::DSP::SympatheticResonance`.

**1. Failing tests**

New file `plugins/membrum/tests/unit/processor/test_coupling_receivers.cpp` (add to `add_executable(membrum_tests`), tags `[coupling][receiver]`. Use the `CouplingIntegrationFixture` of `test_coupling_integration.cpp` (move it into a header next to that file if both TUs need it; do not copy it), the default kit (after T3.3 pad 2 and pad 4 classify Snare, pads 5, 7, 9, 11, 12, 14 Tom), 48 kHz, block 256, every pad's Coupling Amount at 1.0 unless stated. Band energy: `makeOctaveBand` / `OctaveBand::process` from `tests/test_helpers/reverb_metrics.h` (4 kHz and 8 kHz octave bands make the 4-8 kHz wire band) and Goertzel magnitude at a mode frequency from `plugins/membrum/tests/unit/bodies/body_test_helpers.h` (`goertzelWindowMag`); T60 with `schroederT60` (`reverb_metrics.h:214`) over 10 ms hop energies. Windows exclude the first 30 ms after the strike (the source's own click).

1. `TEST_CASE("Snare Buzz drives the idle snare's wires from a kick hit", ...)`: Global Coupling 1, Tom Resonance 0, strike only note 36 at velocity 1, render 1 s. Wire-band (4-8 kHz) energy over 30 ms-1 s with Snare Buzz 1 must exceed Snare Buzz 0 by at least 20 dB (MB-232, MB-233), and the Goertzel energy at the idle snare's (pad 2) fundamental by at least 12 dB (MB-258). With Snare Buzz 0 the output must equal the Global Coupling 0 render within 1e-6 absolute (-120 dBFS). Today all three renders are bit-identical: the case fails on the 20 dB check.
2. `TEST_CASE("Tom Resonance rings an idle tom from a struck tom", ...)`: Snare Buzz 0; strike only pad 5 (note 41); energy at the idle pad 9's fundamental over 30 ms-1 s with Tom Resonance 1 at least 12 dB above Tom Resonance 0 (MB-258; MB-167 asked for 6 dB, the plan takes the stricter bar).
3. `TEST_CASE("SC-008: coupling peaks at the never-struck receiver's own mode", ...)`: rewrite of SC-008. Strike only pad 5. Tune the never-struck pad 9 twice through its Size: once so its fundamental coincides with a partial of pad 5 (read pad 5's bank frequencies with `getModeFrequency` after a note-on), once so it falls midway between two pad-5 partials. Energy at pad 9's fundamental must be at least 12 dB higher in the coincident case, and the spectral peak of the coupling component (render with coupling minus render without) must sit within 1 % of pad 9's fundamental in both cases (it moves with the receiver's Size). Today pad 9 contributes nothing.
4. `TEST_CASE("Coupling Amount scales send and receive monotonically", ...)`: pad 9's Coupling Amount in {0, 0.2, 0.5, 1.0} with Tom Resonance 1, strike pad 5: the coupling-component energy at pad 9's fundamental is 0 at amount 0 (within 1e-6) and strictly increasing over 0.2, 0.5, 1.0 (MB-168). Repeat with the SOURCE pad 5's amount varied. Today it is flat above 0.
5. `TEST_CASE("Receiver ring is bounded by the receiving pad's own decay", ...)`: from case 2's render, the T60 of the coupling component at pad 9's fundamental must be at most 1.5 x the T60 of pad 9 struck alone at velocity 1 (MB-232). Today the engine rings 6-12 s at tom pitches.
6. `TEST_CASE("Snare wires stay silent below the critical amplitude", ...)`: as case 1 with the kick at velocity 0.05: the wire-band energy rises by less than 3 dB from Snare Buzz 0 to 1 while the energy at the snare's fundamental still rises (the head answers tonally, the wires do not engage).
7. `TEST_CASE("Coupling with no notes is exact silence; 16 coincident hits stay bounded", ...)`: all coupling knobs at 1, no notes, 2 s: output exactly 0. Then 16 notes at once (36..51) at velocity 1: output finite and the coupling component's peak at most 0.1 (the energy limiter's -20 dBFS).
8. `TEST_CASE("Receiver coupling allocates nothing on the audio thread", ...)`: `tests/test_helpers/allocation_detector.h` around 50 blocks of `process()` with coupling active and notes ringing, and around a kit load through `Processor::notify` / `applyKitSnapshot`: zero allocations.

Existing tests that encode the old model are rewritten, not deleted: SC-002 (`test_coupling_integration.cpp:240-285`) keeps its two bars (coupling component audible, at least 40 dB below the kick peak) but strikes the kick with Snare Buzz 1 and measures the idle snare's contribution; SC-008 (`:518-607`) is replaced by case 3; the matrix-value cases (`:347-405`) stay (the matrix is still the routing); `:836-880` (amount 0 excluded) is re-expressed through the receivers; `test_coupling_phase9_verification.cpp` and `test_coupling_fuzz.cpp` are ported to the new engine with their bars unchanged. List every rewritten case in the commit message.

**2. Fix**

- New Membrum-local header `plugins/membrum/src/dsp/pad_receivers.h`, class `PadReceiverBank` (namespace `Membrum`; ODR check run at planning: no `PadReceiverBank` exists). It owns, per pad (32), up to `kReceiverModes = 4` second-order resonators with a 0 dB peak at resonance (bandpass), a per-pad active flag, a one-pole envelope follower and, for Snare-category pads, a wire-noise generator (`Krate::DSP::NoiseOscillator` or the XorShift32 in `core/xorshift32.h`) through an SVF bandpass at the pad's noise-layer cutoff, denormalised exactly as the pad's NoiseLayer does (read the noise-layer cutoff mapping in `drum_voice.h` / the NoiseLayer class and call the same function). Use `Krate::DSP::Biquad` (`primitives/biquad.h`, `FilterType::Bandpass`) unless reading it shows a constant-peak-gain bandpass is not available; then write the two-pole resonator in the header with its coefficient formula in a comment.
  - `configurePad(pad, cfg, category, sampleRate)`, called off the per-sample path: when `cfg.couplingAmount > 0`, the pad is enabled and its matrix column is non-zero, compute the pad's natural modes with its body mapper at its natural pitch (`DrumVoice::mapModalBody`, `drum_voice.h:1548-1566`, is a private static: move the switch into a free function in a body-mapping header that both `DrumVoice` and the receivers call, never a second copy of the switch), take the first four mode frequencies below 0.45 fs from `MapperResult::frequencies` (`bodies/membrane_mapper.h:23-41`), and set each resonator's Q from the pad's own per-mode T60: `MapperResult::damping` is the `ModalResonatorBank::DampingLaw` the bank uses (`modal_resonator_bank.h:111-119`, decay rate `b1 + b3 f^2` in s^-1, f in Hz), so `T60_k = ln(1000) / (b1 + b3 f_k^2)` and `Q_k = pi f_k T60_k / ln(1000)`. Read `computeModeCoefficients` in the bank before coding and use the same rate convention (amplitude versus energy) it uses. Cite in the comment that a receiver decays with its own damping (F&R §18.10). The String body (no modal bank) uses its waveguide f0 and harmonics 1..4 with the mapper's decay.
  - `process(const float* const* srcByPad, const std::uint32_t activeSrcMask, float globalGain, const float (&gain)[32][32], float* out, int n)`: for each receiver j with an active flag, drive_j[s] = globalGain x sum over set bits i of `gain[i][j] x srcByPad[i][s]`; resonate; add to `out`. Skip a receiver whose drive is zero for the block and whose resonator state has fallen below 1e-9 (idle skip, so no notes means no work and exact zeros).
  - Snare wires: envelope `e` of the receiver's summed output (attack 1 ms, release 20 ms); wire amplitude `kWireGain x max(0, e - kWireCriticalAmplitude)`, applied to the band-passed noise and added to the output. `kWireCriticalAmplitude` and `kWireGain` are design constants: write in the comment that F&R §18.13 establishes that snares need a critical snare-head amplitude but gives no value for this model, and that the values were chosen so cases 1 and 6 hold (state both measured margins in the comment).
  - Everything is sized at `prepare` (32 x 4 resonators are fixed arrays; no heap).
- `VoicePool` (`voice_pool.h` / `voice_pool.cpp`): give the pool per-pad source buffers, `std::unique_ptr<float[]>` of `kNumPads x maxBlock` allocated in `prepare` next to `scratchL_` (`voice_pool.h:349-354`), plus a per-block active-pad bit mask. While mixing each voice (main and releasing slots, both `processBlock` overloads), add the voice's mono, pre-pan, post-amp-envelope scratch signal into its pad's buffer (clear a pad's buffer the first time its bit is set in a block). Expose `const float* const* sourceBuffers()` and `activeSourceMask()`. Remove the `couplingEngine_` pointer, `setCouplingEngine` (`voice_pool.h:250-253`, `:373`), the note-on registration (`voice_pool.cpp:243-248`) and every `couplingEngine_->noteOff` call T1.9 left (grep `couplingEngine_` in `voice_pool.cpp`): these become orphans of this task.
- `Processor` (`processor.h`, `processor.cpp`): replace `couplingEngine_` (`processor.h:139`) with `PadReceiverBank receivers_`; keep `couplingDelay_`, `couplingDelayMs_` and `applyEnergyLimiter`. In `process()` (`:688-708`), when Global Coupling > 0: `receivers_.process(...)` into a mono scratch (allocated in `setupProcessing`), then per sample `couplingDelay_` (the 0.5-2 ms air path now delays the receivers' output, which keeps Coupling Delay meaningful), `applyEnergyLimiter`, and add to L and R with each receiver pad's equal-power pan (use the pool's pan law; `panGainsForNote`, `voice_pool.h:318`). Global Coupling sets `globalGain` with the existing -46..-26 dB map written out locally (it was `SympatheticResonance::setAmount`); smooth it over 5 ms as the engine did.
  - `recomputeCouplingMatrix` (`:131-143`) keeps filling the matrix and now also marks changed receiver pads dirty; `configurePad` runs for dirty pads once at the start of the next `process()` block, not per edit (the kit-browser load issues about 162 recomputes in one block, probe `double-apply:edits`).
  - Lifecycle: `setupProcessing` prepares `receivers_` and `couplingDelay_`; `setActive` and `applyKitSnapshot` reset them and mark every pad dirty (keep whatever T1.19 established for the delay and the smoother).
  - Remove `#include <krate/dsp/systems/sympathetic_resonance.h>` from `processor.h:23` and `voice_pool.h:57`, and `couplingEngineForTest` (`processor.h:80`); add `receiversForTest()`.
- Comments: rewrite the header of `coupling_matrix.h:3-11` and the coupling comments in `processor.h:7-8` and `processor.cpp:688-692` to describe the receiver model with the sources above. Do not edit `MANUAL.md` here (the phase 6 documentation task owns it).
- Shared headers: none is edited. `SympatheticResonance` keeps its other consumers (Innexus `plugins/innexus/src/processor/processor.h`; grep below for the full list) bit-identical.

**3. Build and gate**

- CROSS-PLUGIN: before editing, run `grep -rl "sympathetic_resonance.h" plugins/ dsp/include tests/` and list the consumers in the commit message (at planning: Innexus processor and test, Membrum processor, voice pool, drum voice and three coupling tests, `aether_reverb.h`). No shared file is modified, so `header-consumers.js` names nothing beyond `membrum_tests`; if the work does require a change to a shared header after all, the CROSS-PLUGIN flag's obligations apply in full.
- Add `Membrum` to the build. Standard gate. pluginval (plugin sources changed).
- Timing: the receivers run per sample on the audio thread. After the gate, run `node tools/run-cpu-tests.js membrum_tests` alone with nothing else executing; no CPU budget may go red, and no budget is relaxed.
- PRESET-IMPACT: factory kits set Snare Buzz 0.18 and Tom Resonance 0.22 among other values (`tools/membrum_preset_generator.cpp:1674-1676`; preset-retune notes 02, 03, 05, 09, 13, 15, 17, 18, 19 choose 0.30-0.78) and they become audible. Run the kit-quality probe before and after and list per affected pad peak, rms500, decay, centroid and limiter margin, plus near-duplicate pairs gained or lost; show the table and stop for the user's ruling before the commit. Do not change kit data (phase 5 retunes the kits).
- PHYSICS: every new constant carries its source or the statement that it is a design value (above). The test for coded-versus-published values is case 5 (receiver decays with its own damping, F&R §18.10) and case 6 (critical amplitude, F&R §18.13); no published numeric constant enters the code.

**4. Acceptance**

- Cases 1 to 8 pass; the rewritten SC-002, SC-008 and the ported phase-9 and fuzz cases pass with their original bars.
- Quality probe: Snare Buzz and Tom Resonance are `live=yes` in `param-audibility.csv` (baseline "bit-identical on 36, 38, 42"); Coupling Amount changes the coupling component by more than 1 dB between 0.2 and 1.0 (baseline under 0.1 dB); `coupling:kick-added-energy` shows a coupling-only component above -60.98 dB relative to dry and below -20 dB; `coupling:idle-no-notes` reads -240 dBFS (exact silence) as before; `coupling:30s-16th-kicks` stays under +1 dB drift (no runaway).
- The before/after kit table and the user's ruling are in the commit message.

### T4.2 — WaveguideString: allpass fractional delay and an exact tuning budget

**Findings:** MB-208 (high, CONFIRMED), MB-211 (low, CONFIRMED).

**Flags:** CROSS-PLUGIN IMPACT, PRESET-IMPACT, PHYSICS.

**Prerequisites:** none.

**Estimate:** 300 minutes.

**Context.** `Krate::DSP::WaveguideString` (`dsp/include/krate/dsp/processors/waveguide_string.h`) reads its loop with `bridgeSideDelay_.readLinear(bridgeDelayFloat_)` inside `process()` (`:191`). Linear interpolation is the one-zero FIR `(1-d) + d z^-1`: inside a feedback loop it adds a d-dependent high-frequency loss every round trip (gain `cos(w/2)` at d = 0.5). The comment at `:187-190` ("exact fractional delay at all frequencies") is false, and the header advertises a "Thiran fractional delay" (`:6`) and "allpass interpolation in feedback loop" (`:16`) the code does not use. `noteOn` (`:289-364`) sets the loop length with the empirical `D = period - 1 - 0.55*dLoss - 0.96*dDisp` (`:337`), which omits the in-loop DC blocker (`kDcBlockerCutoffHz = 3.5`, `:48`; run at `:201`) whose phase lead grows as 1/f^2; `retune()` (`:515-544`) already uses the exact budget `D = period - 1 - dLoss - dDisp - dDC` (`:532`), and its own comment (`:480-514`) documents noteOn's error. `DelayLine` already provides the first-order allpass read (`primitives/delay_line.h:152-167`, `makeAllpassTap` `:442-476` keeps the fractional part in [0.5, 1.5), `readAllpass(const AllpassTap&)` `:478-486`), documented "for feedback loops" (`:42-43`).
Measured (MB-208, 48 kHz, brightness 0 i.e. Membrum Material 1, T60 4 s, f0 790..826 Hz): at fractional delay 0.000 (f0 800) the 10th harmonic's T60 is 4.475 s; at fractional delays 0.11-0.90 it is 0.108-0.161 s; the fundamental's T60 is 2.54-3.49 s against 4 s set. Measured (MB-211, pick 0.3, fundamental versus `800*0.1^size`): Size 0 +1.2 / -2.0 / -5.3 cents for Material 1 / 0.5 / 0 at 48 kHz (-5.85 at 44.1 kHz); Size 0.5 +1.8..+3.8; Size 0.7 +4.4..+6.0; Size 1 +11.2..+11.9 cents at all three rates.
Consumers: Membrum (`plugins/membrum/src/dsp/bodies/string_body.h`, `drum_voice.h`), Innexus (`plugins/innexus/src/processor/innexus_voice.h:51`), and `dsp/include/krate/dsp/systems/continuous_body.h` (Seraphis, Vorago). The default path is pinned: Seraphis FR-084 / SC-014 require every pre-existing `WaveguideString` path to stay identical, enforced by `WaveguideString_RetuneIsInert` (`dsp/tests/unit/processors/waveguide_string_retune_test.cpp:330`, a RenderFingerprint of a `noteOn(220)` render). Decision this plan makes: the allpass read and the exact budget are OPT-IN (`setLoopInterpolation`), the default stays linear and bit-identical for Innexus, Seraphis and Vorago, and Membrum opts in. Whether the other plugins adopt it is a separate ruling: name it in the commit message as an open question for the user.

**1. Failing tests**

- First add the API as a stub so the tests compile and fail for the physical reason, not a compile error: in `WaveguideString` add `enum class LoopInterpolation : std::uint8_t { Linear, Allpass };` (nested in the class; ODR grep at planning found no other `LoopInterpolation`) and `void setLoopInterpolation(LoopInterpolation mode) noexcept` that only stores the mode.
- `dsp/tests/unit/processors/waveguide_string_retune_test.cpp` (it already has `estimateFundamentalHz` `:91`, `centsError` `:136` and the render helpers; reuse them, write no second estimator). New cases, tags `[waveguide_string][allpass_loop]`:
  1. `TEST_CASE("WaveguideString_AllpassLoop_FlatLoopGainAtEveryFraction")`: `setLoopInterpolation(Allpass)`, prepare 48 kHz, stiffness 0, brightness 0, decay 4 s, pick 0.3; for f0 in {790, 795, 800, 805, 810, 815, 820, 826}: `noteOn(f0, 1)`, render 6 s with zero excitation; filter with `makeOctaveBand(48000, 8000)` (`tests/test_helpers/reverb_metrics.h:189`) and with an octave band centred on f0, take energy per 10 ms hop and `schroederT60` (`:214`). REQUIRE the 8 kHz band T60 within +/-15 % of 4 s and the fundamental band within +/-10 % (MB-208's bars). Fails today at every non-integer loop length (baseline above).
  2. `TEST_CASE("WaveguideString_AllpassLoop_TuningWithin2Cents")`: Allpass mode; Size in {0, 0.3, 0.5, 0.7, 1} mapped to f0 = 800*0.1^size; brightness in {1, 0.5, 0}; fs in {44100, 48000, 96000}; pick 0.3, decay 4 s; measured fundamental within +/-2 cents of f0 (MB-211; the existing `test_string_body.cpp:44-80` 2 % tolerance, about 34 cents, cannot see this). Fails today up to +11.9 cents.
  3. `TEST_CASE("WaveguideString_AllpassLoop_RetuneGlideIsClickFree")`: Allpass mode, `noteOn(800)`, then `retune` every 64 samples on an exponential path to 100 Hz over 120 ms; `ClickDetector` from `tests/test_helpers/artifact_detection.h` (read its config struct first) finds no click, and the last 100 ms read 100 Hz +/-5 cents. (A coefficient step in the allpass leaves a transient; `makeAllpassTap` keeps |a| <= 1/3, so it decays within a few samples. This case pins that for T4.3.)
- `plugins/membrum/tests/unit/bodies/test_string_body.cpp`, new case `TEST_CASE("StringBody: Material 1 keeps its upper harmonics at every Size near 800 Hz", "[string_body]")`: StringBody through `body_test_helpers.h` (`runBodyImpulse`, `goertzelWindowMag`, `findPeakFrequency`), Material 1, Decay 0.5 (T60 = 4 s: `string_mapper.h:56-58`, `lerp(0.5,4,1) * exp(0) = 4`), Size = log10(800 / f0) for the eight f0 above, 48 kHz; the 10th harmonic's T60 (Goertzel per 10 ms hop at 10 x the measured f0, then `schroederT60`) within +/-15 % of 4 s. Fails today.
- Keep green and unedited: `WaveguideString_RetuneIsInert`, `WaveguideString_RetunePitchAccuracy`, every case in `waveguide_string_test.cpp`, `waveguide_string_dc_blocker_test.cpp`, `bow_waveguide_coupling_test.cpp`, and the Innexus waveguide tests. They prove the default path did not move.

**2. Fix**

- `waveguide_string.h`:
  - Factor the exact budget out of `retune()` (`:524-532`) into a private helper `computeExactBudget(f0, sr, S)` returning `period - 1 - dLoss - dDisp - dDC`, and call it from `retune()`.
  - Add a member `DelayLine::AllpassTap bridgeAllpassTap_{}` and a private `configureAllpassTap(float D, float f0, float sr)`: build the tap with `bridgeSideDelay_.makeAllpassTap(D)`, compute the read's phase delay at f0 as `tap.index0 + computeThiranPhaseDelay(tap.coeff, w0)` (`:665`; the tap realises `z^-index0 (a + z^-1)/(1 + a z^-1)`, `delay_line.h:445-446`), and if it differs from D, rebuild the tap once at `D - error` (one correction step; state the residual in a comment after measuring it in test 2).
  - `noteOn`: when the mode is Allpass, replace `:337` by `D = computeExactBudget(f0, sr, S)` and call `configureAllpassTap`; the Linear path keeps `:337` exactly as written.
  - `retune()`: when the mode is Allpass, also call `configureAllpassTap` (do not reset the delay line's allpass state: the loop keeps ringing).
  - `process()`: `const float feedback = (loopInterpolation_ == LoopInterpolation::Allpass) ? bridgeSideDelay_.readAllpass(bridgeAllpassTap_) : bridgeSideDelay_.readLinear(bridgeDelayFloat_);`.
  - `silence()` already resets the delay line, which clears its allpass state (`delay_line.h:325`, `:331`); confirm and leave it.
  - Correct the comments: `:4-7` and `:16` (linear read by default; first-order allpass when opted in); `:186-190` (linear interpolation is the one-zero FIR `(1-d) + d z^-1`, which damps high partials by up to `cos(w/2)` per round trip, J.O. Smith, Physical Audio Signal Processing, "Fractional Delay Filtering by Linear Interpolation" and "Delay-Line Interpolation"; it stays the default because Seraphis FR-084 / SC-014 pin the default render); `:327-337` (the empirical factors belong to the linear path); the doc of `setLoopInterpolation` cites the PASP "Extended Karplus-Strong Algorithm" tuning allpass and Jaffe & Smith 1983 (already cited at `delay_line.h:443`), and says the exact budget is the one `retune()` documents at `:480-514`.
- `plugins/membrum/src/dsp/bodies/string_body.h`, `StringBody::prepare` (`:32-36`): after `string_.prepare(sampleRate)`, call `string_.setLoopInterpolation(Krate::DSP::WaveguideString::LoopInterpolation::Allpass)`, with a comment citing MB-208 / MB-211.

**3. Build and gate**

- CROSS-PLUGIN: before editing, `grep -rl "waveguide_string.h" plugins/ dsp/include tests/` and list the consumers in the commit message. Step 4's `node tools/header-consumers.js --tag '[membrum]'` is mandatory; build and gate every executable it names (at least `dsp_processors_tests`, `dsp_systems_tests`, `innexus_tests`, `seraphis_tests`, `vorago_tests`) with `~[long]` appended. A consumer's golden or fingerprint that moves means the default path changed: that is a defect of this task, fix it; never re-baseline another plugin's golden here.
- Build `dsp_processors_tests` plus the consumers, `membrum_tests` and `Membrum`. pluginval for Membrum. clang-tidy `-Target dsp` and `-Target membrum`.
- PHYSICS: the published-value checks are the tuning case (the loop resonates where the summed phase delay equals the period: PASP EKS) and the flat-loop-gain case (an allpass has unity gain at every frequency: PASP "Delay-Line Interpolation"); no new numeric constant enters the code.
- PRESET-IMPACT: every String pad gets brighter tails (the interpolator loss disappears) and moves by up to 12 cents. Run the kit-quality probe before and after, list every String pad (body column `String` in `kit-pads.csv`, for example Drone and Sustain 38 and 40) with peak, rms500, decay, centroid and limiter margin, and the near-duplicate pairs gained or lost. Show the table and stop for the user's ruling.

**4. Acceptance**

- The four new cases pass; at f0 790-826 Hz the 10th harmonic's T60 is within 15 % of 4 s (baseline 0.108-0.161 s) and the fundamental's within 10 % (baseline 2.54-3.49 s); the fundamental is within 2 cents of `800*0.1^size` across the grid (baseline up to +11.9 cents).
- Every consumer suite named by `header-consumers.js` is GATE GREEN with no golden or fingerprint changed.

### T4.3 — The pitch envelope retunes the String waveguide through retune()

**Findings:** MB-210 (high, CONFIRMED; repeats the 2026-06 signal-path finding H-3 for the String body).

**Flags:** PRESET-IMPACT, PHYSICS.

**Prerequisites:** T1.11 (pitch-envelope refresh cadence on every renderer), T4.2 (allpass loop; its glide case pins click-free retunes).

**Estimate:** 120 minutes.

**Context.** `DrumVoice::retuneFundamental` (`plugins/membrum/src/dsp/drum_voice.h:1583-1593`) routes the String body to `BodyBank::setStringFrequency` (`plugins/membrum/src/dsp/body_bank.h:121-130`), which calls `WaveguideString::setFrequency` (`waveguide_string.h:134-140`). That only re-targets `frequencySmoother_`, which feeds nothing but the loss gain `rho` (`:175-183`; the header says so itself at `:548-551`); the loop length `bridgeDelayFloat_` changes only in `noteOn` (`:341`) and `retune()` (`:535`), and Membrum never calls `retune()`. Callers: the note-on seed (`drum_voice.h:318-321`), the per-sample path (`:506-510`, `updateBodyFundamental`), the block paths (`:745`, `:964`). Measured at 48 kHz: `noteOn(800)` then `setFrequency(100)` still peaks at 799.06 Hz with magnitude 1.3e-5 at 100 Hz, and the 800 Hz fundamental's T60 drops from 1.500 s to 0.241 s (set 2 s), because `rho` is computed for 100 passes per second while the loop runs at 800. `retune(100)` gives 100.00 Hz. The regression test `test_pitch_env_all_bodies.cpp:143-189` ("H-3: pitch envelope retunes the String waveguide body") compares zero-crossing rates early and late in the render, which fall anyway as the excitation's high frequencies decay, so it passes on the broken code. Sources (report section 5, MB-210): J.O. Smith, PASP, "Extended Karplus-Strong Algorithm" (pitch is the loop delay plus the tuning allpass; the damping filter sets decay, not pitch); J.O. Smith, CCRMA REALSIMPLE, "One-Zero String Damping Filter" (`rho = 0.001^(P T / t60)` is tied to the actual loop period P).

**1. Failing test**

- `plugins/membrum/tests/unit/dsp/test_pitch_env_all_bodies.cpp`: replace the case at `:143-189` by `TEST_CASE("H-3: pitch envelope moves the String waveguide's pitch and keeps its decay", ...)` with the tags the replaced case had. Setup: `DrumVoice` at 48 kHz, String body, Impulse exciter, Material 1, Decay 0.2219 (T60 2 s: `string_mapper.h:56-58`, `4 * exp(lerp(ln 0.3, ln 3, d)) = 2` gives d = (ln 0.5 - ln 0.3) / ln 10 = 0.2219), pitch envelope start 800 Hz, end 100 Hz, time 120 ms: `tsPitchEnvStart = log(800/20)/log(100) = 0.80103`, `tsPitchEnvEnd = log(100/20)/log(100) = 0.34949`, `tsPitchEnvTime = 120/500 = 0.24` (the encodings `default_kit.h:54-61` documents). Render 2 s twice: through `processBlock` at block 256 and through the per-sample `process()`.
  - REQUIRE the strongest peak (`findPeakFrequency`, `plugins/membrum/tests/unit/bodies/body_test_helpers.h:60`) in the window 300-400 ms to be 100 Hz +/-5 cents. Baseline: 799.06 Hz.
  - Render a flat reference: pitch envelope off, Size = log10(800/100) = 0.90309 (the String's natural f0 is `800 * 0.1^size`, `string_mapper.h:40`). REQUIRE the swept render's T60 at 100 Hz (Goertzel per 10 ms hop from 300 ms, `schroederT60` from `tests/test_helpers/reverb_metrics.h`) within +/-15 % of the flat render's. Baseline: the swept ring decays about 6x too fast.
  - REQUIRE no click in the sweep (`ClickDetector`, `tests/test_helpers/artifact_detection.h`).
  - REQUIRE the `processBlock` and `process()` renders' measured end pitch to agree within 1 cent (the renderers stay equivalent).

**2. Fix**

- `plugins/membrum/src/dsp/bodies/string_body.h`: add `void retune(float pitchHz) noexcept` that calls `string_.retune(pitchHz)` and then `string_.snapSmoothers()` (`waveguide_string.h:555-560`: the header recommends snapping for a caller that steps on its own control grid, and it keeps `rho` matched to the real loop period instead of gliding two transcendentals per sample). Rate-limit inside `StringBody`: keep `lastRetuneHz_` and skip a retune whose pitch moved by less than 0.1 cent (`|log2(pitchHz / lastRetuneHz_)| < 0.1 / 1200`); reset `lastRetuneHz_` in `configureForNoteOn`.
- `body_bank.h:121-130`: `setStringFrequency` calls `s->retune(pitchHz)` instead of `s->string_.setFrequency(pitchHz)`; update its comment (it retunes the loop length through `WaveguideString::retune`, MB-210).
- Note-on seed: when the pitch envelope is active, the string must start at the envelope's START pitch with its excitation (the pick-position comb, `waveguide_string.h:348-352`) laid out for that pitch, not be noteOn'd at the mapper's f0 and retuned a few lines later (`drum_voice.h:318-321`). Pass the initial pitch to `StringBody::configureForNoteOn` (`string_body.h:43-58`) so it calls `string_.noteOn(initialPitchHz, 1.0f)`; keep the mapper's f0 when the envelope is off.
- Cadence: after T1.11, re-read the three call sites. The per-sample `process()` path calls `retuneFundamental` every sample; with the 0.1-cent gate a fast sweep still retunes nearly every sample, so additionally limit the String retune in that path to once every 16 samples plus the sample on which the envelope reaches its end value, unless T1.11 already established a cadence for the modal path, in which case use the same one. Write the chosen cadence and its stair-step bound (cents per step on the test's 3600-cent, 120 ms sweep) in the comment.
- Correct the comment at `drum_voice.h:1583-1586` ("retunes via its frequency smoother").

**3. Build and gate**

- Add `Membrum` to the build. Standard gate. pluginval.
- Timing: `retune()` evaluates a few transcendentals; after the gate run `node tools/run-cpu-tests.js membrum_tests` alone with nothing else executing; no budget may go red.
- PRESET-IMPACT: every String pad with an active pitch envelope now glides and keeps its decay. Kit-quality probe before and after; table of those pads (peak, rms500, decay, centroid, limiter margin) and near-duplicate changes; stop for the user's ruling. The audible change is the finding's purpose, and the table is still shown.

**4. Acceptance**

- The rewritten case passes on both renderers: end pitch 100 Hz +/-5 cents (baseline 799.06 Hz), swept T60 within 15 % of the flat render (baseline about 6x short: 0.241 s against 1.500 s), no click.

### T4.4 — String high-frequency decay is computed per note from f0 and the sample rate

**Findings:** MB-209 (medium, CONFIRMED).

**Flags:** CROSS-PLUGIN IMPACT, PRESET-IMPACT, PHYSICS.

**Prerequisites:** T4.2 (with the linear-interpolation loss removed, the loss filter is the only high-frequency loss left in the loop).

**Estimate:** 180 minutes.

**Context.** `StringMapper::map` (`plugins/membrum/src/dsp/bodies/string_mapper.h:43-58`) sets `brightness = 1 - material` and `decayTime = lerp(0.5, 4, material) * exp(lerp(ln 0.3, ln 3, decay))`; `WaveguideString` turns brightness into `S = brightness * 0.5` (`waveguide_string.h:184`, `:319`) and applies the one-zero loss `rho * ((1-S) x + S x[n-1])` once per period (`:212-214`), with only `rho` designed in seconds (`computeRho`, `:596-601`). A fixed S is a fixed loss per normalised frequency applied f0 times a second, so the decay in seconds of a partial at a fixed frequency scales with f0 and with fs. Measured (f0 200 Hz, T60 4 s, partial nearest 4 kHz): Material 0 gives T60 0.445 / 0.491 / 1.443 s at 44.1 / 48 / 96 kHz; Material 0.5 gives 0.450 / 0.688 / 1.818 s; Material 1 gives 0.697 (44.1 kHz, fractional delay not 0) / 4.109 / 4.054 s. `refreshStringMaterial` (`plugins/membrum/src/dsp/body_bank.h:132-144`) re-applies the same mapping during Material Morph.
The one-zero cannot be designed to a seconds target at every rate: at f0 200 Hz, T60 4 s and a 1 s target at 4 kHz, the closed form `S(1-S) = (1 - (g/rho)^2) / (4 sin^2(pi fRef/fs))` needs S(1-S) = 0.1597 / 0.1884 / 0.7409 at 44.1 / 48 / 96 kHz, and 0.7409 exceeds the one-zero's ceiling of 0.25 (S = 0.5). Decision this plan makes: when opted in, the loss filter becomes the one-pole `H(z) = g (1 + a) / (1 + a z^-1)` designed per note from the T60 at DC and at a reference frequency (the one-pole loss filter design of Valimaki et al., named in MB-209 and report section 5; if the report could not open that paper, say so in the comment instead of citing it as read), which reaches the target at every rate. Sources for the law: J.O. Smith, PASP, "Loop Filter Identification" (per-partial gain `|H(w_k)| = 10^(-3 / (f0 T60(f_k)))`, applied once per period); J.O. Smith, REALSIMPLE, "One-Zero String Damping Filter"; Chaigne & Askenfelt, "Numerical simulations of piano strings I", JASA 95(2), 1994 (damping `sigma = b1 + b3 w^2` in physical frequency, independent of the sampling grid).

**1. Failing tests**

- Add the API first as stubs so the tests compile (they must then fail on the measured law): `WaveguideString::setHighFrequencyDecay(float t60AtRefSeconds, float refHz) noexcept` (stores, does nothing yet) and a public pure function `[[nodiscard]] static float designOnePoleLossCoeff(float f0, float sr, float t60, float t60AtRef, float refHz) noexcept` (returns 0 for now).
- New file `dsp/tests/unit/processors/waveguide_string_hf_decay_test.cpp`, added to `dsp_processors_tests` in `dsp/tests/CMakeLists.txt`, tags `[waveguide_string][hf_decay]`:
  1. `TEST_CASE("WaveguideString_HfDecay_OnePoleCoefficientMatchesTheLaw")`: REQUIRE `designOnePoleLossCoeff(200, fs, 4, 1, 4000)` == -0.127933 (fs 44100), -0.145052 (48000), -0.339942 (96000), each within 1e-4; and exactly `0.0f` when `t60AtRef >= t60` (a branch, not arithmetic). The expected numbers were computed by hand from the law below (`rho = 10^(-3/(f0 T60))`, `r = 10^(-3/(f0 T60ref)) / rho`, `A = 1 - r^2`, `B = 1 - r^2 cos(2 pi fRef / fs)`, `a = (-B + sqrt(B^2 - A^2)) / A`); type them, never compute them in the test with the code under test.
  2. `TEST_CASE("WaveguideString_HfDecay_SameSecondsAtEveryRate")`: Allpass loop (T4.2) plus `setHighFrequencyDecay(1.0, 4000)`, f0 200, T60 4 s, render 6 s at 44.1, 48 and 96 kHz; the T60 of the 4 kHz octave band (`makeOctaveBand`, `schroederT60` from `tests/test_helpers/reverb_metrics.h`) agrees within +/-15 % across the three rates, and the band around f0 is within +/-10 % of 4 s.
- `plugins/membrum/tests/unit/bodies/test_string_body.cpp`:
  3. `TEST_CASE("StringMapper: HF decay law reproduces the 48 kHz / 200 Hz anchor", "[string_body]")`: the mapper's new HF extra decay rate at 4 kHz, in dB per second, equals 60.2249 / 56.3360 / 44.7727 / 25.8362 / 0.0 for Material 0 / 0.25 / 0.5 / 0.75 / 1, within 1e-3 (typed values; derivation in step 2).
  4. `TEST_CASE("StringBody: HF decay is the same at 44.1, 48 and 96 kHz and across Size", "[string_body]")`: Material 0, Decay 0.5; T60 of the partial nearest 4 kHz (Goertzel per 10 ms hop with `goertzelWindowMag`, `body_test_helpers.h`, then `schroederT60`) at f0 200 Hz agrees within +/-15 % across the three rates and equals the mapper's target `60 / (60 / T60 + 60.2249)` within +/-15 %; at 48 kHz, Size 0 (800 Hz) and Size 0.602 (200 Hz) agree within +/-15 %. Baseline: 0.445 / 0.491 / 1.443 s across the rates.
- Keep the existing `waveguide_string_*` tests and `WaveguideString_RetuneIsInert` green and unedited (the default path does not move).

**2. Fix**

- `waveguide_string.h` (opt-in; default behaviour unchanged):
  - `setHighFrequencyDecay(t60Ref, refHz)` sets a flag, stores the target, clamps `refHz` to at most 0.45 fs; a matching `clearHighFrequencyDecay()` returns to the brightness path.
  - `designOnePoleLossCoeff` implements the law above with `r >= 1` (target at or above the DC T60) returning exactly 0 by a branch, and the `sqrt` argument clamped at 0. Comment: the derivation (`(1 + a)^2 / (1 + 2 a cos w + a^2) = r^2`, the root in (-1, 0]) and the sources.
  - When the flag is set, the loss stage in `process()` becomes `lossOutput = rho * (1 + a) * x - a * lossState_` with `lossState_` holding the previous OUTPUT; `a` is recomputed wherever `rho` is (the memo at `:175-182`, keyed additionally on the HF target), so a glide and a decay change keep it matched. `noteOn` and `retune` design `a` for the new f0, and the delay budget uses the one-pole's phase delay at f0 (`-atan2(a sin w0, 1 + a cos w0) / w0`, a positive delay for a < 0) in place of `dLoss`.
- `plugins/membrum/src/dsp/bodies/string_mapper.h`: replace the `brightness` output by `hfDecayTime` (keep `brightness` only if another caller reads it; grep first). The law, anchored so a 200 Hz String at 48 kHz decays at 4 kHz exactly as today's S = 0.5 (1 - material) does, and independent of f0 and fs elsewhere: extra HF decay rate `Dhf(m) = -2000 * log10(1 - 4 S (1 - S) sin^2(pi / 12))` dB per second with `S = 0.5 (1 - m)` (that is 200 periods per second times the one-zero's per-period loss at 4 kHz / 48 kHz), and `hfDecayTime = 60 / (60 / decayTime + Dhf(m))`. Comment: the anchor, the numbers of test 3, and Chaigne & Askenfelt's frequency-domain damping.
- `plugins/membrum/src/dsp/bodies/string_body.h`, `configureForNoteOn` (`:47-58`): call `string_.setHighFrequencyDecay(r.hfDecayTime, 4000.0f)` instead of `setBrightness`. `body_bank.h:136-144` `refreshStringMaterial`: the same call instead of `setBrightness`.
- Correct the comment block at `string_mapper.h:43-52`.

**3. Build and gate**

- CROSS-PLUGIN: `grep -rl "waveguide_string.h" plugins/ dsp/include tests/`, list the consumers in the commit message, run `header-consumers.js` and gate every executable it names (as in T4.2) with `~[long]`. A moved consumer golden is a defect of this task.
- Build `dsp_processors_tests`, the consumers, `membrum_tests`, `Membrum`. pluginval for Membrum. clang-tidy `-Target dsp` and `-Target membrum`.
- PRESET-IMPACT: String pads change brightness at every rate other than 48 kHz and at every f0 other than 200 Hz. Kit-quality probe before and after (it runs at 48 kHz; also report the 44.1 and 96 kHz HF T60 of one String pad per kit from test 4's method); table and the user's ruling before the commit.

**4. Acceptance**

- The four cases pass. The 4 kHz partial's T60 for Material 0 at f0 200 Hz agrees within 15 % across 44.1 / 48 / 96 kHz (baseline 0.445 / 0.491 / 1.443 s, a 3x spread) and matches the mapper target within 15 %; Size 0 and Size 0.602 agree within 15 %. Every consumer suite is GATE GREEN with no golden changed.

### T4.5 — Mode Stretch 1.0 is neutral on Plate, Shell, Bell and NoiseBody

**Findings:** MB-031 (medium, CONFIRMED), MB-214 (medium, PLAUSIBLE), MB-205 (low, PLAUSIBLE).

**Flags:** PRESET-IMPACT, PHYSICS, MACOS, VERIFY-FIRST.

**Prerequisites:** T3.5 (the default decodes to exactly 1.0).

**Estimate:** 180 minutes.

**Context.** The Plate, Shell, Bell and NoiseBody mappers set `stretch = clamp((modeStretch - 0.5) / 1.5, 0, 1)` with no unity baseline (`plugins/membrum/src/dsp/bodies/plate_mapper.h:58-62`, `shell_mapper.h:60-62`, `bell_mapper.h:55-57`, `noise_body_mapper.h:118-120`), so the default Mode Stretch 1.0, documented as "physical" / "no stretch" (`pad_config_apply.h:123`, `voice_common_params.h:23`; spec 137 FR-050), gives stretch 0.333. The shared bank then applies `B = stretch^2 * 0.01 = 1.111e-3` and `f_k *= sqrt(1 + B (k+1)^2)` by array index (`dsp/include/krate/dsp/processors/modal_resonator_bank.h:783`, `:806-807`); its comment at `:781-782` ("Membrum default-off get B==0") holds only for Membrane, which subtracts the unity baseline (`membrane_mapper.h:136-138`) but keeps `material * 0.3` of stretch. Computed and measured at the default: Bell nominal/hum 4.053 instead of 4 (+22.7 cents), prime/hum 2.0033, tierce/prime 1.2033; Shell mode 6 +33 cents, mode 32 +657 cents; Plate top mode (k = 47) x1.887; NoiseBody k = 31 x1.46 (+650 cents), k = 63 x2.356; audit probe `modes:body=Bell` (`probe-results.txt`) +0.9, +3.8, +8.6, +15.2, +23.7, +33.9, +45.9, +59.4 cents on partials 1-8. A plate's, bar's or bell's dispersion is already inside its eigenvalue series (Fletcher & Rossing 1998, ch. 2, ch. 3 §3.6, ch. 20 §20.2, ch. 21; Hibbert et al., "Partial Frequencies and Chladni's Law in Church Bells", Open Journal of Acoustics 4, 2014), so a stiff-string `sqrt(1 + B n^2)` on top has no physical basis at the neutral setting. The non-neutral default was accepted by the user in the 2026-06 audit (`plugins/membrum/AUDIT-signal-path-2026-06-07.md:64`, "Accepted consequence (user-approved)"), and the preset retune was built on it (`PRESET-RETUNE-PLAN.md:178`, `:199`); that is why MB-214 and MB-205 are PLAUSIBLE and why the head's ruling table lists T4.5 as "None: ask".

**Ruling needed before step 2.** Ask the user, with the step-1 numbers:
- (a) Make 1.0 neutral on Plate, Shell, Bell and NoiseBody, reversing the 2026-06 acceptance?
- (b) Keep Membrane's `material * 0.3` baseline stretch (top mode x1.232 at the default Material 0.5, x1.753 at Material 1), or remove it?
- (c) With (a), Mode Stretch below 1.0 has no effect on these four bodies (the bank clamps stretch at 0, `modal_resonator_bank.h:756`), where today 0.5..1.0 maps to stretch 0..0.333. Accept that, or give the lower half a compressing meaning (a separate model change, not in this task)?
If (a) is no: close MB-214 and MB-205 as "kept by ruling", keep the step-1 test with its expectation inverted into a pin of the accepted warp, correct the false comments only (`pad_config_apply.h:123`, `voice_common_params.h:23`, and `modal_resonator_bank.h:781-782`, which is a shared header: run `header-consumers.js` and gate what it names; audio must not move), and stop. The plan's default if (a) is yes: keep Membrane as it is (b), accept (c) and report it.

**1. Failing test (doubles as the verification)**

- File `plugins/membrum/tests/unit/unnatural/test_mode_stretch.cpp`, `TEST_CASE("Mode Stretch 1.0 leaves Plate, Shell, Bell and NoiseBody on their tables", "[unnatural][mode_stretch]")`. For each of the four bodies: a `DrumVoice` prepared at 96 kHz (fewer modes cut at Nyquist), default `PadConfig` (Mode Stretch decodes to exactly 1.0 after T3.5) with `modeScatter = 0` and `airLoading = 0`, Impulse exciter, note-on; read the bank's `getModeFrequency(k)` (`modal_resonator_bank.h:526`) for every installed mode below 0.45 fs and REQUIRE `f_k / f_0` within 0.1 % of the expected ratio:
  - Bell, expected ratios typed from the tuned-bell partial series relative to the nominal (F&R ch. 21; Hibbert et al. 2014): hum 0.25, prime 0.5, tierce 0.6, quint 0.75, nominal 1.0, octave nominal 2.0, so relative to the hum 1, 2, 2.4, 3, 4, 8 for the first, second, third, fourth, fifth and seventh entries of `kBellRatios` (`bell_modes.h:56-60`).
  - Shell, typed from the free-free bar eigenvalue series (roots of `cos(bL) cosh(bL) = 1`, F&R ch. 2): 1, 2.757, 5.404, 8.933, 13.344, 18.637 for k = 0..5; above k = 5 the table `kShellRatios` (`shell_modes.h:37-44`).
  - Plate and NoiseBody: the table `kPlateRatios` (`plate_modes.h`), whose agreement with the Chladni law is already pinned by `test_plate_body.cpp:59` ("PlateModes: kPlateRatios == free-plate Chladni ...").
  Copy the exact equation or table numbers for the F&R and Hibbert citations from report section 5 into the test comment; where the report could not open a source, say so.
- Add a render check (MB-205): 96 kHz, Shell and Bell at default Mode Stretch; `findPeakFrequency` (`body_test_helpers.h:60`) near each expected partial; shell modes 2-6 within 5 cents of `kShellRatios`, bell hum, prime, tierce and quint within 5 cents of 0.25, 0.5, 0.6, 0.75 x the measured nominal.
- On current code both fail (bell nominal/hum 4.053, +22.7 cents; shell mode 6 +33 cents). If they PASS, MB-214 and MB-205 are refuted and MB-031 with them: keep the tests, change no production code, record "refuted".
- `test_plate_body.cpp:135-137` sets `modeStretch = 0.5f` "to neutralise the modeStretch warp": after the fix, remove that override so the ratio case runs at the default (it must still pass; this strengthens it).

**2. Fix (after a yes to (a))**

- New header `plugins/membrum/src/dsp/bodies/mode_stretch.h` (ODR grep for the function name first) with `[[nodiscard]] inline float stretchAboveUnity(float modeStretch) noexcept`: `if (modeStretch <= 1.0f) return 0.0f;` then `return std::clamp((modeStretch - 1.0f) * (1.0f / 1.5f), 0.0f, 1.0f);`. Above 1.0 this equals the old `(m - 0.5) / 1.5 - 1/3`, so the top of the range (2.0 -> 0.667) matches Membrane's; at and below the neutral point the branch returns an exact 0 (MACOS rule: a neutral value by a branch, not by cancellation). Comment: MB-031, the sources, and the ruling.
- Use it in `plate_mapper.h:58-62`, `shell_mapper.h:60-62`, `bell_mapper.h:55-57` and `noise_body_mapper.h:118-120` (`out.modal.stretch`). Correct the comments in each mapper and in `pad_config_apply.h:123-124`. `MembraneMapper` changes only if the ruling on (b) says so.
- `modal_resonator_bank.h:781-782` becomes true and is not edited (no shared header changes in this path).

**3. Build and gate**

- Add `Membrum` to the build. Standard gate. pluginval.
- `plugins/membrum/tests/golden/phase1_default.bin` (`approval/test_phase1_regression.cpp`): if its render moves, re-baseline it deliberately and state why in the commit message.
- MACOS: run `node tools/lint-finite-bitmask.js`, `node tools/lint-nonfinite-symbols.js`, `node tools/lint-ieee-flag-sets.js`, `node tools/lint-apple-globals.js`, `node tools/check-portability.js` on the changed `.cpp` files, and `tools/clangcl-fastmath-build.ps1 -Targets membrum_tests` followed by the new cases on that tree; no DSP TU changed, so write "unverified on macOS until CI" in the commit message.
- PRESET-IMPACT: every Plate, Shell, Bell and NoiseBody pad with Mode Stretch at or below 1.0 changes timbre (the default kit's crash and clap at 1.4 change less). Kit-quality probe before and after; table per affected pad (peak, rms500, decay, centroid, limiter margin) and near-duplicate pairs gained or lost; the user sees it before the commit (the ruling in the box above covers the model, the table covers the kits).

**4. Acceptance**

- After a yes: the table-ratio and render cases pass; the audit probe `modes:body=Bell/Shell/Plate` reads under 5 cents against the table for partials 1-8 (baseline up to +59.4 cents); bell nominal/hum 4.000 (baseline 4.053); NoiseBody k = 31 ratio within 0.1 % of `kPlateRatios` (baseline x1.46).
- After a no: the pin test passes, the corrected comments describe the accepted warp, and rendered audio is unchanged (`getState` of the 20 factory kits and every render test unchanged).

### T4.6 — Air loading covers the full Fletcher & Rossing mode table, keeps each mode family monotonic, and is documented as what it does

**Findings:** MB-244 (medium, CONFIRMED), MB-198 (low, PLAUSIBLE), MB-199 (low, CONFIRMED), MB-245 (low, CONFIRMED).

**Flags:** PRESET-IMPACT, PHYSICS, VERIFY-FIRST (applies to MB-198 only: its own part is the (m,1) fold-back and the (0,n) raise the header comment promises; MB-244, MB-199 and MB-245 are CONFIRMED and are fixed whatever MB-198's test shows).

**Prerequisites:** none.

**Estimate:** 180 min.

What the code does today (re-read at `4a14376d`): `MembraneMapper::map` (`plugins/membrum/src/dsp/bodies/membrane_mapper.h:100-109`) computes `f_k = f0 * kMembraneRatios[k] * (1 + airLoading * (kAirLoadingTargetScale[k] - 1))`. `kAirLoadingTargetScale` (`plugins/membrum/src/dsp/membrane_modes.h:98-105`) differs from 1.0 only at indices 2, 4, 6, 9 ((2,1), (3,1), (4,1), (5,1): 1.1192 / 1.2012 / 1.2321 / 1.2668). The comment at `membrane_modes.h:88-91` justifies holding every n >= 2 mode and every (m >= 6, 1) mode at 1.0 because "Rossing's published data doesn't quantify them"; Fletcher & Rossing Tables 18.1 and 18.2 do. Relative to (1,1) at airLoading 1, (6,1) sits at 2.593 below (5,1) at 2.900 and (7,1) at 2.893; at airLoading 0.5 and the PadConfig default 0.6 (`pad_config.h:291`) (6,1) is still below (5,1). The prose at `membrane_modes.h:64-66` says air loading "raises the axisymmetric (0, n) modes", the table never does. The user-facing and parameter docs describe a different model: `MANUAL.md:241` ("low-mode frequency depression ... sub-bass"), `MANUAL.md:247` ("full Rossing curve"), `pad_config.h:121-125` (a `kAirLoadingCurve` that does not exist), `pad_config.h:286-289` ("depression ... deeper / less whistly"), `voice_common_params.h:45-48` (`f_k *= (1 - airLoading * curve[k])`, scatter "[0, 0.15]" while the bank uses `scatter * 0.10f` at `dsp/include/krate/dsp/processors/modal_resonator_bank.h:784`). `MANUAL.md:177` lists Air Loading under Body Size and `:179` lists Mode Scatter and Tension Modulation under Complexity; `MacroMapper::applyBodySize` (`plugins/membrum/src/processor/macro_mapper.cpp:159-177`) moves only size, modeStretch and decay, and `applyComplexity` (`:199-216`) only couplingAmount, nonlinearCoupling and modeInjectAmount.

**1. Failing test**

New file `plugins/membrum/tests/unit/bodies/test_membrane_air_loading.cpp`, added to the `add_executable(membrum_tests` list. The mapper is a pure function, so cases (a) and (b) need no helper from `tests/test_helpers/`; build a `Membrum::VoiceCommonParams` (material 0, size 0.5, decay 0.5, strikePos 0.3, modeStretch 1.0, decaySkew 0, modeScatter 0) and call `Membrum::Bodies::MembraneMapper::map(p, 0.0f)`. Build an (m,n) -> frequency lookup from `kMembraneBesselOrder` and `kMembraneBesselRadial` (`membrane_modes.h:40-60`); index map for reference: 0 (0,1), 1 (1,1), 2 (2,1), 3 (0,2), 4 (3,1), 5 (1,2), 6 (4,1), 7 (2,2), 8 (0,3), 9 (5,1), 10 (3,2), 11 (6,1), 12 (1,3), 13 (4,2), 14 (7,1).

- (a) `TEST_CASE("MembraneMapper: air loading keeps every (m,n) family monotonic", "[membrane_body][air_loading]")`. For each airLoading in {0, 0.25, 0.5, 0.75, 1}: for every (m,n) whose (m+1,n) is also in the 48-mode table, `CHECK(f(m+1,n) > f(m,n))`; for every (m,n) whose (m,n+1) is in the table, `CHECK(f(m,n+1) > f(m,n))`. INFO the pair and both frequencies. Expected red on unfixed code at airLoading 0.5, 0.75 and 1: (6,1) < (5,1) (at 1: 4.1316 vs 4.6207 in units of f0, ratio 0.894). This is MB-198's verification and MB-244's fold-back. If it passes on current code, MB-198 is refuted (record it) and MB-244's part still proceeds through case (b).
- (b) `TEST_CASE("MembraneMapper: airLoading 1 matches Fletcher & Rossing Tables 18.1 / 18.2", "[membrane_body][air_loading][physics]")`. At airLoading 1, ratios relative to f(1,1), expected values typed from the sources:
  - Rossing 1992 (Physics Today 45(3), after Rossing & Kvistad 1976): (2,1) 1.50, (3,1) 2.00, (4,1) 2.44, (5,1) 2.90, each `Approx(...).epsilon(0.005)`.
  - Fletcher & Rossing 1998, Table 18.2 (measured, with kettle, after Christian et al. 1984): (1,2) 2.00, (2,2) 2.86, (0,3) 2.71, each `epsilon(0.03)`.
  - Fletcher & Rossing 1998, Table 18.1 (calculated, with kettle): (3,2) 3.28, (6,1) 3.38, (1,3) 3.38, (4,2) 3.80, each `epsilon(0.03)`.
  - Anchors: f(0,1) and f(1,1) at airLoading 1 equal their airLoading-0 values (`epsilon(1e-6)`).
  Red today on (1,2) 1.831, (2,2) 2.197, (0,3) 2.258, (3,2) 2.547, (6,1) 2.593 and the rest.
- (c) `TEST_CASE("Air Loading documentation describes the coded ratio morph", "[membrane_body][air_loading][docs]")`. Read text files through the existing compile definition `MEMBRUM_RESOURCES_DIR` (`plugins/membrum/tests/CMakeLists.txt:225-227`): `MEMBRUM_RESOURCES_DIR "/../MANUAL.md"`, `MEMBRUM_RESOURCES_DIR "/../src/dsp/pad_config.h"`, `MEMBRUM_RESOURCES_DIR "/../src/dsp/voice_common_params.h"`, `MEMBRUM_RESOURCES_DIR "/../src/dsp/membrane_modes.h"`. `REQUIRE` each opens. `CHECK` that none of them contains `kAirLoadingCurve` or `1 - airLoading * curve`; that the MANUAL text between the heading `### Air Loading + Mode Scatter` and the next `###` contains neither `depression` nor `sub-bass`; that the MANUAL macro-table line starting `| **Body Size**` does not contain `Air Loading`; and that the line starting `| **Complexity**` contains neither `Mode Scatter` nor `Tension Modulation`. Red today on every check. A test that reads non-C++ files is a consumer of those files: it is new, so step 4's affected-scope run includes it.

**2. Fix**

- `plugins/membrum/src/dsp/membrane_modes.h`, `kAirLoadingTargetScale` (`:98-105`). Each entry is `scale_k = target(m,n) * 1.5933 / kMembraneRatios[k]`, where `target` is the ratio to (1,1) at airLoading 1 and 1.5933 is the (1,1) Bessel ratio:
  - Indices 0 (0,1) and 1 (1,1) stay 1.0 (the anchors keep Size -> f0 meaning what it means today).
  - Indices 2, 4, 6, 9 keep today's values (Rossing 1992's 1.50 / 2.00 / 2.44 / 2.90).
  - Tabulated by Fletcher & Rossing, measured value (Table 18.2) where one exists, else calculated (Table 18.1): index 5 (1,2) 2.00 -> 1.0923; index 7 (2,2) 2.86 -> 1.3019; index 8 (0,3) 2.71 -> 1.1999; index 10 (3,2) 3.28 -> 1.2875; index 11 (6,1) 3.38 -> 1.3034; index 12 (1,3) 3.38 -> 1.2730; index 13 (4,2) 3.80 -> 1.3159.
  - Every other mode takes the scale of the nearest tabulated mode in the same n family with a smaller m (so (m >= 7, 1) -> 1.3034, (m >= 5, 2) -> 1.3159, (m >= 2, 3) -> 1.2730); a mode whose n family has no tabulated member with smaller m takes the scale of the tabulated mode with the same m and the highest n below or nearest to it ((0,2) and (0,n >= 4) -> 1.1999 from (0,3); (1,n >= 4) -> 1.2730 from (1,3); (2,n >= 4) -> 1.3019 from (2,2)).
  - Generate the 48 values with a new Node script `tools/gen-membrane-air-loading.js` (same style as `tools/gen-plate-chladni.js`: reads nothing, prints the C++ array, committed). The script asserts the monotonicity of case (a) at airLoading 0 and 1 (each frequency is linear in airLoading, so the two endpoints prove every value in between) and exits non-zero on a violation. If the rule above produces a violation, stop and report the pair; do not hand-tweak a value. Note in the comment that (1,2) and (3,1) both land on 2.00: that is the measured near-degeneracy (344 Hz and 340 Hz in Table 18.2), not a defect.
  - Rewrite the comment at `:62-97`: the model is a ratio-space morph with (0,1) and (1,1) anchored; physically air loading lowers the low modes in absolute terms (Fletcher & Rossing §18.1, Table 18.1: (1,1) 228 -> 150 Hz), so anchoring the fundamental means the knob raises the overtones relative to it: brighter and more pitched, not deeper. Give the source of every value group next to it (Rossing 1992 Physics Today 45(3):40-47; Fletcher & Rossing 1998, Tables 18.1 and 18.2). Delete the "Rossing's published data doesn't quantify them" premise and the "raises the axisymmetric (0, n) modes" sentence (replace it with what the table now does: (0,2) and (0,3) rise per Table 18.2, (0,1) is anchored). The existing "Scientific American 1982" citation at `:71` stays only with the words "not opened by the audit" (report section 5 lists the sources that were opened).
- `plugins/membrum/src/dsp/pad_config.h:121-125` and `:285-291`: describe the morph as above (0 = pure Bessel ratios; 1 = the (m,1) family on Rossing's 1 : 1.5 : 2 : 2.44 : 2.9 series and the n >= 2 modes on Fletcher & Rossing Tables 18.1 / 18.2; fundamental anchored; brighter, not deeper). Fix the scatter line to "10 % sinusoidal dither" (`modal_resonator_bank.h:784`, `C = scatter * 0.10f`).
- `plugins/membrum/src/dsp/voice_common_params.h:45-48`: replace the formula with the one `membrane_mapper.h:107-108` uses and the scatter range with [0, 0.10].
- `plugins/membrum/MANUAL.md:239-248`: rewrite the Air Loading paragraph and the table row to the coded behaviour (same wording as the header comment, for a reader). `MANUAL.md:177`: Body Size row -> "Size, Mode Stretch, Decay" (`macro_mapper.cpp:159-177`). `MANUAL.md:179`: Complexity row -> "Coupling Amount, Nonlinear Coupling, Mode Inject" (`macro_mapper.cpp:199-216`). The Punch row (`:178`) is also wrong (the code moves pitch-envelope start and time and noiseBurstDuration, `macro_mapper.cpp:179-197`); it belongs to T6.8 (MB-121): name it in the commit message and leave it.
- No other file changes. Kit data is not touched.

**3. Build and gate**

Standard, with `Membrum` in the target list. PRESET-IMPACT: the change is engine-side, so no regeneration; run the kit-quality probe before and after. Affected pads are every Membrane pad with airLoading > 0: in the default kit the kick (0.7, `default_kit.h:76`), both snares (0.5, `:134`) and the toms (0.6, `:164`), and every factory Membrane pad. The existing Phase 8C cases (`plugins/membrum/tests/unit/dsp/test_per_mode_damping.cpp:333`, `:372`, `:408`, `:622`) must stay green unchanged.

**4. Acceptance**

- (a) green at all five airLoading values; record (6,1)/(5,1) at airLoading 1 (baseline 0.894, after > 1: expected 1.165 from the table).
- (b) green; record every ratio against its typed target.
- (c) green.
- `Phase 8C: airLoading=1 produces the Rossing timpani series` (`test_per_mode_damping.cpp:622`) green, unchanged.
- The parameter-audibility probe row `offset=52:airLoading` on note 36 (baseline in `specs/membrum-audit/probes/param-audibility.csv`: maxBandDistDb 0.51, maxCentroidChangePct 8.4, decay 1.03 s at every step) re-measured and listed with the kit table. If the table shows an audible change, stop for the user's ruling before the commit.

### T4.7 — Membrane damping gains a radiation term keyed by mode order, and the tom b3 comes down

**Findings:** MB-252 (medium, CONFIRMED), MB-217 (low, PLAUSIBLE), MB-255 (low, PLAUSIBLE).

**Flags:** CROSS-PLUGIN IMPACT, PRESET-IMPACT, PHYSICS, VERIFY-FIRST (MB-217 and MB-255; MB-252 is CONFIRMED and its part is built whatever their tests show).

**Prerequisites:** T1.23 (it replaces the hidden B1/B3 damping sentinel; this task writes the tom's b3 in T1.23's encoding and adds its term on top of the law T1.23 leaves).

**Estimate:** 300 min.

What the code does today: every modal mode decays at `decayRate_k = b1 + b3 * f_w^2` with one `{b1, b3}` pair per body (`dsp/include/krate/dsp/processors/modal_resonator_bank.h:821-824` in `computeModeCoefficients`, and again at `:365-368` in `updateDampingLaw`; `b1 >= 0.2`, `b3 >= 0` at `:766-767`). `MembraneMapper::map` builds the pair at `membrane_mapper.h:177` through `dampingLawFromParams` (`:64-80`); air loading only moves frequencies (`:100-109`); `kMembraneBesselOrder` is read only for the strike weight (`:115`). So T60 can only fall with frequency and the (0,1) mode always rings longest. Measured drums do the opposite on the (m,1) family: Fletcher & Rossing Table 18.4 (26-in timpani, with kettle, T = 3710 N/m, after Christian et al. 1984) gives (01) 0.4 s, (11) 2.3 s, (21) 3.7 s, (31) 4.6 s, (02) 0.3 s, (03) 0.5 s; §18.7 says radiation is a major part of membrane damping and the monopole (0,n) modes damp fastest. The radiation-only calculation (Christian et al. 1984 as reproduced in Packer 1993, Table 6.1, 64-cm drum, 5360 N/m) gives T60 (0,1) 0.06, (1,1) 0.8, (2,1) 1.8, (3,1) 4.6, (4,1) 10.5, (5,1) 16.2, (0,2) 0.2, (0,3) 0.1, (1,2) 0.2, (2,2) 0.8 s. Separately, the default Tom template sets `bodyDampingB3 = 0.15f` (`plugins/membrum/src/dsp/default_kit.h:166-174`, "Tom-depth Fix B"), b3 = 1.5e-4 s: computed T60 0.17 s at 500 Hz and 45 ms at 1 kHz against 1.11 s for the (0,1) mode of the size-0.5 tom, where Fletcher & Rossing Table 18.8 (Bork 1983, 32-cm tom-tom) measures (01) 0.8-1.05 s and 1.1-2.7 s in the 500 Hz third-octave band. Read the deferral note `plugins/membrum/preset-retune/kits/06-orchestralKit-fix-plan.md:187` (D8) first: it records this diagnosis, explains why an airLoading-keyed term was deferred (it shortens kick and tom fundamentals kit-wide), and this task is that deferred item; its warning is the reason the PRESET-IMPACT table below must show every Membrane pad.

**1. Failing test**

- (a) Shared bank, `dsp/tests/unit/processors/test_modal_resonator_bank.cpp` (already in `dsp_processors_tests`, `dsp/tests/CMakeLists.txt:307`): `TEST_CASE("ModalResonatorBank: per-mode extra decay rate adds to b1 + b3*f^2", "[modal_resonator_bank][damping]")`. prepare(48000); four modes at 200 / 400 / 600 / 800 Hz, amplitude 1, `DampingLaw{.b1 = 2.0f, .b3 = 1.0e-5f}`, stretch 0, scatter 0, `extraRate = {0, 5, 10, 20}` through the new `setModes` overload. `CHECK(getRadiusTargets()[k] == Approx(std::exp(-(2.0 + 1e-5 * f_k * f_k + extra_k) / 48000.0)).epsilon(1e-6))` with `f_k = getModeFrequency(k)`. Then `updateDampingLaw({.b1 = 3.0f, .b3 = 2.0e-5f})` and check the extra rates are still included; then `updateModes(..., nullptr)` and check the radii equal the law without them. Second case `TEST_CASE("ModalResonatorBank: setModes without per-mode rates is unchanged", "[modal_resonator_bank][damping]")`: 48 modes from a fixed table, the existing `setModes(..., DampingLaw, stretch, scatter)` overload and the new overload with `nullptr` produce identical `getRadiusTargets()` and `getEpsilonTargets()` (`==`, same code path, same TU). Red: the first case does not compile (no such overload). That is the expected red for a new API.
- (b) `plugins/membrum/tests/unit/dsp/test_per_mode_damping.cpp`: `TEST_CASE("Phase 8C: airLoading adds a mode-order radiation term (F&R Table 18.4)", "[phase8c][drum_voice][damping][radiation]")`. DrumVoice as in the existing Rossing case (`:626-643`) but `prepare(48000.0, 0u)`, `setMaterial(1.0f)` (legacy b3 = 0, so only b1 and the new term remain), size 0.5, decay 0.5, strike 0.3, Impulse, Membrane, `setModeScatter(0.0f)`, `noteOn(1.0f)`; read `v.getBodyBankForTest().getSharedBank().getRadiusTargets()` and convert each radius to `T60_k = 6.9078 / (-std::log(R_k) * 48000)`. `SECTION("airLoading 1")`: `CHECK(T60(1,1) > 2 * T60(0,1))`, `CHECK(T60(2,1) >= T60(1,1))`, `CHECK(T60(0,2) < T60(1,1))` (indices 0, 1, 2, 3). Red today: the coded law makes T60(0,1) the longest. This is MB-252's test and MB-217's verification (MB-217's claim is that the (0,1) mode cannot decay faster than (1,1)). `SECTION("airLoading 0 keeps today's law")`: every radius equals `exp(-(law.b1 + law.b3 * f_k^2) / 48000)` with `law = bank.getDampingLaw()` and `f_k = bank.getModeFrequency(k)`, `epsilon(1e-6)`; green today and must stay green.
- (c) Same file: `TEST_CASE("Default Low-Mid Tom keeps its 500 Hz band ringing (F&R Table 18.8)", "[default_kit][damping][tom]")`. Build the pad with the default kit's template function (`applyTemplate(cfg, DrumTemplate::Tom, 0.5f)`, `plugins/membrum/src/dsp/default_kit.h:40`; the Low-Mid Tom row is `:419`; check the namespace there), push it into a DrumVoice with `applyPadConfigToVoice` (`plugins/membrum/src/dsp/pad_config_apply.h:30`), prepare 48000, noteOn(1). From the bank's radius targets: `T60_f0` = T60 of mode 0; `T60_500` = the largest T60 among installed modes whose `getModeFrequency(k)` lies in [445.4, 561.2] Hz (the 500 Hz third-octave). `CHECK(T60_500 >= 0.5 * T60_f0)` and `CHECK(T60_f0 >= 0.53)` and `CHECK(T60_f0 <= 1.58)` (Table 18.8's (01) 0.8-1.05 s widened by a factor of 1.5 each way). Computed baseline on today's code: T60_f0 about 1.11 s, T60_500 about 0.19 s (the (3,1) mode at about 470 Hz; the finding's 0.17 s is the value at exactly 500 Hz), so the first check is red. This is MB-255's verification: if (c) passes on current code, MB-255 is refuted and the tom's b3 stays.

**2. Fix**

- Shared bank, `dsp/include/krate/dsp/processors/modal_resonator_bank.h` (opt-in; the default path stays byte-for-byte the same expression):
  - New state `alignas(32) float extraDecayRate_[kMaxModes]{}` next to the other SoA arrays (`sinState_` and friends, from `:658`).
  - New overloads `setModes(const float* frequencies, const float* amplitudes, int numPartials, DampingLaw damping, float stretch, float scatter, const float* extraDecayRate)` and the same for `updateModes`. The existing overloads (`:258-274`, `:294-305`) forward `nullptr`. `computeModeCoefficients` (`:743`) takes the pointer: when non-null it stores `std::max(extraDecayRate[k], 0.0f)` into `extraDecayRate_[k]` and adds it to `decayRate_k` at `:823`; when null it zeroes `extraDecayRate_` (so a later `updateDampingLaw` cannot reuse a stale array) and evaluates `b1 + b3 * f_w * f_w` exactly as today.
  - `updateDampingLaw` (`:356-370`) adds `extraDecayRate_[k]` to `decayRate_k`.
  - Update the doc comments at `:106-119` and `:238-305`: `R_k = exp(-(b1 + b3*f_k^2 + e_k)/sampleRate)`, `e_k` optional, s^-1, non-negative.
- `plugins/membrum/src/dsp/bodies/membrane_mapper.h`, `MapperResult` (`:23-41`): add `float extraDecayRate[kMaxModes]{}` and `bool hasExtraDecayRate = false`.
- `plugins/membrum/src/dsp/membrane_modes.h`: add the radiation table.
  - `kMembraneRadiationWeight[48]`, `g = T60(1,1) / T60(m,n)` from Packer 1993 Table 6.1 (Christian et al. 1984, radiation only): (0,1) 13.333, (1,1) 1.0, (2,1) 0.4444, (3,1) 0.1739, (4,1) 0.07619, (5,1) 0.04938, (0,2) 4.0, (0,3) 8.0, (1,2) 4.0, (2,2) 1.0. Untabulated modes: (m >= 6, 1) -> 0.04938 (the (5,1) value); (0, n >= 4) -> 8.0 (the (0,3) value); (1, n >= 3) -> 4.0 (the (1,2) value); (m >= 2, n >= 2) -> 1.0 (the (2,2) value). Comment each group with its source and say the extension rule is this plan's, not the source's.
  - `kMembraneRadiationRate11 = 0.243f` (s^-1), the one fitted constant: it is the value of R that makes T60(1,1)/T60(0,1) = 2.3 / 0.4 = 5.75 (Fletcher & Rossing Table 18.4) for a Membrane with material 1.0 (b3 = 0), decay 1.0, size 0.74 ((1,1) = 145 Hz, the table's drum) and airLoading 1: `R = b1 * (5.75 - 1) / (13.333 - 5.75)` with that config's legacy `b1 = 1 / decayTime = 0.388` (decayTime 2.578 s from `membrane_mapper.h:148-152`). Write the derivation next to the constant and say it is fitted, not published. The rate is not rescaled with Size or tension: say so (design choice).
- `MembraneMapper::map` (after `:177`): when `airLoading > 0`, `r.extraDecayRate[k] = airLoading * kMembraneRadiationRate11 * kMembraneRadiationWeight[k]` for every k and `r.hasExtraDecayRate = true`. At airLoading 0 nothing is set and the bank gets `nullptr` (bit-identical to today).
- Pass the array wherever a Membrane `MapperResult` reaches the bank, as `r.hasExtraDecayRate ? r.extraDecayRate : nullptr`: `plugins/membrum/src/dsp/bodies/membrane_body.h:37` (`setModes`), `plugins/membrum/src/dsp/drum_voice.h:1619` (`updateModalFundamentalOnBank`, the pitch-envelope and tension retune) and `:1658` (`pushCachedModes`, the Material Morph refresh). Without the last two the term vanishes at the first retune. Grep `setModes(\|updateModes(` under `plugins/membrum/src` once more for any site T1.6 or T1.11 added (for example a strike-normalisation probe that installs the mapper result into a scratch bank) and pass it there too. `StrikeNormKey` (`drum_voice.h:1717-1727`) already holds `airLoading`, so the cached strike gain is re-measured when the term changes.
- Tom b3, only if case (c) was red (MB-255 confirmed): in `default_kit.h` Tom template (`:166-174`) lower the b3 override from 0.15 (b3 = 1.5e-4 s) to 0.01 (b3 = 1.0e-5 s), written in the encoding T1.23 introduced. Rewrite the "Tom-depth Fix B" comment: the inharmonic mid cluster is now shortened by the radiation term on the (0,n) and (1, n >= 2) modes rather than by a large f^2 term on every mode; cite Table 18.8. The factory kits' toms are kit data and are not touched here; name them for T5.6 in the commit message.
- `membrane_mapper.h:56-63`: keep the 1e-3 s b3 ceiling (removing the range would remove a control) and say in the comment that above about 1e-5 s it is a perceptual range, not a measured drum-head value (measured: Table 18.8; the heaviest f^2 coefficient in Sterling et al. 2019, wood and plastic, is about 2.6e-6 s in this Hz convention). If T1.23 did not already correct `pad_config.h:276-279` ("8e-5 s * rad^-2", owned by MB-216 in T1.23), correct it here to "b3 = norm * 1.0e-3 s (Hz convention)".

**3. Build and gate**

- CROSS-PLUGIN: before editing, `grep -rl "modal_resonator_bank.h" plugins/ dsp/include tests/` (at `4a14376d` it lists, outside Membrum, `plugins/innexus/src/processor/innexus_voice.h`, `plugins/innexus/tests/unit/processor/test_physical_model.cpp` and `dsp/include/krate/dsp/systems/continuous_body.h`, which Seraphis and Vorago reach). Targets: `membrum_tests Membrum dsp_processors_tests`, then every executable `node tools/header-consumers.js --tag '[membrum]'` names (expect at least `dsp_systems_tests`, `innexus_tests`, `seraphis_tests` and `vorago_tests`; its last line is authoritative), all in one sharded run with `~[long]`. Other plugins must stay bit-identical: they never pass the new argument, and case (a)'s second test pins the unchanged path. clang-tidy adds `-Target dsp`.
- PRESET-IMPACT: kit-quality probe before and after. Every Membrane pad with airLoading > 0 changes decay: the default kick (airLoading 0.7) gains about 0.7 x 0.243 x 13.333 = 2.27 s^-1 on its (0,1) mode, roughly a third off its fundamental T60. List every Membrane pad of the default kit and the 20 factory kits (peak, rms500, decay, centroid, limiter margin) and every near-duplicate pair that appears or disappears. This change is audible by design: stop for the user's ruling with the table. Tell the user the D8 alternative explicitly: keying the term to a new per-pad "kettle" amount instead of airLoading would keep kick and tom fundamentals but needs a new parameter (STATE-COMPAT), which this task does not add.

**4. Acceptance**

- (a) both dsp cases green; every consumer suite green in the same `GATE GREEN` line.
- (b) green; record T60 of (0,1), (1,1), (2,1), (0,2) at airLoading 1 and their ratios. Calibration check, added to (b) as a third SECTION: at material 1, decay 1, size 0.74, airLoading 1, `T60(1,1) / T60(0,1) == Approx(5.75).epsilon(0.02)`. A fourth SECTION types the ten Packer weights and checks `kMembraneRadiationWeight` at the matching indices (`epsilon(1e-3)`).
- (c) green, or MB-255 recorded as refuted with the test output. Record T60_500 / T60_f0 (computed baseline about 0.17; expected after about 1.0) and T60_f0 (baseline about 1.11 s; expected about 1.48 s with b3 = 1e-5 and the airLoading-0.6 term).
- Every existing case in `test_per_mode_damping.cpp` green, including `Phase 8A.5: b3 sweep produces audible energy change` (`:681`) unchanged.
- The parameter-audibility row `offset=52:airLoading` on note 36 now shows a decay change across steps (baseline 1.03 s at every step); record it.

### T4.8 — Decay Skew scales per-mode decay instead of tilting amplitudes

**Findings:** MB-236 (medium, CONFIRMED).

**Flags:** CROSS-PLUGIN IMPACT, PRESET-IMPACT, PHYSICS.

**Prerequisites:** T4.7 (it adds the per-mode damping path to the shared bank and to `MapperResult`; this task extends both).

**Estimate:** 180 min.

What the code does today: every modal mapper turns `decaySkew` in [-1, +1] into (1) a global decay-time factor `skewBias = 1 - 0.3 * decaySkew` and (2) a per-mode AMPLITUDE tilt `clamp(ratio^(-decaySkew), 1/8, 8)` (`kDecaySkewMaxModeTilt`, `membrane_mapper.h:43-51`): `membrane_mapper.h:140-173`, `plate_mapper.h:68-91`, `shell_mapper.h:69-89`, `bell_mapper.h:64-86`, `noise_body_mapper.h:125-145`. The per-mode decay stays `b1 + b3 * f^2` with `b3 >= 0` (`modal_resonator_bank.h:767`, `:823`), so higher modes always die at least as fast as the fundamental at every skew. The contract requires the opposite at skew -1: `specs/137-membrum-phase2-exciters-bodies/contracts/unnatural_zone_contract.md:47-53` and `:165` ("fundamental decays fast, high modes sustain"; Impulse + Membrane, assert `t60(mode7) > t60(mode0)`), with `research.md:284-289` giving `final_decay_time_k = base_decay_time_k * (f_k / f_fundamental)^(-decaySkew)`. The existing test admits it checks something weaker (`plugins/membrum/tests/unit/unnatural/test_decay_skew.cpp:6-13`, `:127-130`: a late-window Goertzel magnitude ratio). The manual has the sign backwards: `MANUAL.md:347` says "positive = bright sustain, negative = dark sustain". Audit probe baseline (`specs/membrum-audit/probes/probe-results.txt:571-573`, Membrane Impulse decay 0.6): decay rate mode 0 / mode 7 in dB/s = -13.8 / -45.3 at skew -1, -17.2 / -48.7 at 0, -23.4 / -54.9 at +1: mode 7 decays about three times faster at every skew.

Why research.md's exponent alone is not enough (computed for this plan from the mapper and bank formulas, on the contract patch `test_decay_skew.cpp:52-66`: material 0.5, size 0.5, decay 0.8, Membrane, stretch 1.0, airLoading 0): with the global factor removed, b1 = 1.059 s^-1 and b3 = 2.0e-5 s, so mode 0 (158.1 Hz) decays at 1.559 s^-1 and mode 7 (557 Hz after stretch) at 7.273 s^-1. Dividing mode 7's rate by `(f_7/f_0)^1 = 3.50` leaves 2.078 s^-1, still faster than mode 0: US6-2 fails. The natural rate grows as f^2 once b3 dominates, so the multiplier must use the square: `(f_7/f_0)^2 = 12.25`, clamped to 8, gives 0.909 s^-1 and T60(mode 7) 7.6 s > T60(mode 0) 4.4 s.

**1. Failing test**

- (a) Shared bank, `dsp/tests/unit/processors/test_modal_resonator_bank.cpp`: `TEST_CASE("ModalResonatorBank: per-mode decay-rate scale multiplies the whole rate", "[modal_resonator_bank][damping]")`. prepare(48000); modes 200 / 400 / 600 / 800 Hz, `DampingLaw{.b1 = 2.0f, .b3 = 1.0e-5f}`, extra `{0, 5, 0, 5}`, scale `{1.0, 0.5, 2.0, 0.125}` through the new overload. `CHECK(getRadiusTargets()[k] == Approx(std::exp(-std::max(0.2, (2.0 + 1e-5 * f_k * f_k + e_k) * s_k) / 48000.0)).epsilon(1e-6))`. A second SECTION sets scale 0.01 on mode 0 and checks its rate is floored at 0.2 s^-1. A third checks `updateDampingLaw` keeps the scale. Red: no such overload (compile failure).
- (b) `plugins/membrum/tests/unit/unnatural/test_decay_skew.cpp`: `TEST_CASE("UnnaturalZone Decay Skew -- skew -1 inverts per-mode T60 (US6-2)", "[UnnaturalZone][DecaySkew]")`. Same patch as `renderWithSkew` (`:52-66`) at 44100 Hz, but instead of rendering read `voice.getBodyBankForTest().getSharedBank().getRadiusTargets()` after `noteOn(0.8f)` and convert to `T60_k = 6.9078 / (-std::log(R_k) * 44100)`. For skew in {-1, 0, +1}: record `q(skew) = T60_7 / T60_0`. `CHECK(q(-1) > 1.0)` (the contract: `t60(mode7) > t60(mode0)`), `CHECK(q(0) < 1.0)` (natural law), `CHECK(q(-1) > q(0))`, `CHECK(q(+1) < q(0))`. Red today on `q(-1) > 1.0` (and `q(-1) > q(0)`, because only the amplitudes move).
- (c) `plugins/membrum/tests/unit/unnatural/test_decay_skew_all_bodies.cpp`: the four cases `DecaySkew per-mode tilt -- Plate / Shell / Bell / NoiseBody` (`:61-152`) assert an amplitude tilt the fix removes. Replace each with `TEST_CASE("DecaySkew per-mode decay -- <Body>", "[UnnaturalZone][DecaySkew][<Body>]")` at the same strength, on the mapper result: with `T60 multiplier m_k = 1 / r.decayRateScale[k]`, the hi/lo multiplier ratio at skew -1 is `> 2.0` and at skew +1 is `< 0.6`, and Shell keeps its bound check `< 9.0` at skew -1 (the same bars and the same lo/hi indices the amplitude cases used: lo = 0 everywhere, hi = 8 / 7 / 15 / 8 for Plate / Shell / Bell / NoiseBody), and the amplitude arrays at skew -1, 0 and +1 are identical (no tilt left). Keep `DecaySkew per-mode tilt -- decaySkew==0 leaves amplitudes untilted` (`:154`) and add to it `CHECK_FALSE(r.hasDecayRateScale)`. Fix the stale comment at `:59` (`ratio[8]=12.5`; `kPlateRatios[8]` is 5.2701). Red today: `decayRateScale` does not exist.

**2. Fix**

- Shared bank, `dsp/include/krate/dsp/processors/modal_resonator_bank.h`: extend T4.7's opt-in overloads with a trailing `const float* decayRateScale` (nullable). New state `alignas(32) float decayRateScale_[kMaxModes]` filled with 1.0 when the pointer is null. When non-null, `decayRate_k = std::max((b1 + b3 * f_w * f_w + e_k) * s_k, kMinDecayRate)` where `kMinDecayRate = 1.0f / 5.0f` is the same floor `b1` gets at `:766` (name it once and use it in both places; the longest T60 stays 34.5 s, so voices keep retiring). When null, the expression is exactly T4.7's. `updateDampingLaw` applies the stored scale the same way. Update the doc comments.
- `MapperResult` (`membrane_mapper.h:23-41`): add `float decayRateScale[kMaxModes]{}` and `bool hasDecayRateScale = false`.
- Each modal mapper (`membrane_mapper.h:140-173`, `plate_mapper.h:64-91`, `shell_mapper.h:64-89`, `bell_mapper.h:59-86`, `noise_body_mapper.h:122-145`):
  - Delete `skewBias` from the decay time (at skew 0 it is a multiplication by 1.0f, so removing it keeps skew 0 bit-identical).
  - Delete the per-mode amplitude tilt loop.
  - When `decaySkew != 0`: for every installed mode, `s_k = clamp(pow(f_k / f_0, 2 * decaySkew), 1 / kDecaySkewMaxModeTilt, kDecaySkewMaxModeTilt)` with `f_k = r.frequencies[k]` and `f_0 = r.frequencies[0]` (the body's lowest mode: the hum on Bell), and set `hasDecayRateScale = true`. At skew 0 leave it false. Put the exponent in a named constant (`kDecaySkewRateExponent = 2.0f`) with the derivation above: research.md §9 writes exponent 1, which fails US6-2 on the contract's own patch because the natural rate grows as f^2. Clamp the `pow` base at its domain edge (`std::max(f_k / f_0, 1e-6f)`). Rewrite `kDecaySkewMaxModeTilt`'s comment (`membrane_mapper.h:43-50`): it now bounds the per-mode decay-rate multiplier.
  - `noise_body_mapper.h:164-176` (the wash decay from the lowest mode's T60) needs no change: mode 0's scale is 1.
- Pass `r.hasDecayRateScale ? r.decayRateScale : nullptr` at every site that passes T4.7's extra-rate array, and add it for the bodies that do not pass one (they pass `nullptr` for the extra rate): `membrane_body.h:37`, `plate_body.h:37`, `shell_body.h:33`, `bell_body.h:33`, `noise_body.h:64`, `drum_voice.h:1619`, `drum_voice.h:1658`, and any probe site T4.7 found. `StrikeNormKey` already holds `decaySkew` (`drum_voice.h:1721`).
- Docs: `membrane_mapper.h:140-159` comments; `test_decay_skew.cpp:6-13` header (now a T60 assertion, no "weaker assertion"); `MANUAL.md:347` -> "±1.0; tilts per-mode decay: positive = high modes die faster than the fundamental (darker tail); negative = the fundamental dies first and the high modes sustain". `MANUAL.md:175` (Tightness: "Decay Skew (faster high-mode falloff)") is now true: `kTightnessDecaySkewSpan = 1.00f` (`plugins/membrum/src/processor/macro_mapper.h:103`) raises skew with Tightness; leave it.

**3. Build and gate**

- CROSS-PLUGIN: the same consumer grep, `header-consumers.js` run, targets (`membrum_tests Membrum dsp_processors_tests` plus every named executable) and `-Target dsp` clang-tidy as T4.7. Other plugins pass no scale and stay bit-identical; T4.7's "unchanged" dsp case still pins it.
- `UnnaturalZone Decay Skew -- decaySkew==0 matches defaults-off path` (`test_decay_skew.cpp:138`) must stay green: it is the bit-identity check at skew 0.
- PRESET-IMPACT: kit-quality probe before and after. Affected pads: every modal pad whose stored decaySkew is not the neutral 0.5 normalized (for example the default crash, 0.55, `default_kit.h:232`), plus pads whose Tightness macro is off neutral once T1.22 lets macros reach the sound. Table and ruling as the head says.

**4. Acceptance**

- (a) green; consumer suites green.
- (b) green: record q(-1), q(0), q(+1) (computed for this plan: q(0) about 0.21, q(-1) about 1.7).
- (c) green on all four bodies, plus the untilted case.
- `UnnaturalZone Decay Skew -- inversion shifts energy toward high modes` (`test_decay_skew.cpp:93`) green, unchanged.
- The probe lines `unnatural:decaySkew=-1/0/1` re-run: at skew -1 mode 7's dB/s must now be smaller in magnitude than mode 0's (baseline -13.8 / -45.3). The parameter-audibility row `offset=22:decaySkew` (baseline note 36: rms500 -35.2 to -28.0 dB, centroid 306 to 567 Hz, decay 0.97 to 0.83 s across the five steps) re-measured and listed.

### T4.9 — Plate strike position weights each mode by its radial shape; the 1.7 exponent carries a real source or is called a design choice

**Findings:** MB-204 (medium, CONFIRMED), MB-203 (low, PLAUSIBLE).

**Flags:** PRESET-IMPACT, PHYSICS, VERIFY-FIRST (applies to MB-203 only; MB-204's part runs regardless).

**Prerequisites:** none.

**Estimate:** 240 min.

What the code does today: `computePlateAmplitude` (`plugins/membrum/src/dsp/bodies/plate_modes.h:117-129`) maps Strike Position to `rho = 0.40 + 0.50 * strikePos` and `theta = strikePos * pi/2` and returns `cos(m * theta) * cos(n * pi * rho)`. Consumers take `|.|` and floor it: `PlateMapper::map` at 0.05 (`plate_mapper.h:45-53`), `NoiseBodyMapper::map` at 0.03 (`noise_body_mapper.h:108-113`). The comment (`:103-116`) calls this the free-plate shape `R_mn(r) cos(m theta)` (Leissa). It is not: a free circular plate is axisymmetric, so a point strike excites the member of each degenerate (cos, sin) pair aligned with the strike and the weight is `|R_mn(r0)| / ||R_mn||`, independent of azimuth; `R_mn = J_m(lambda r) + C I_m(lambda r)` grows like r^m near the centre, while the coded n = 0 radial factor is 1 at every radius and the coded (0,1) nodal circle sits at rho 0.5 instead of 0.68. Consequences (recomputed by the verifiers): at strikePos 0.3 (the default and the default kit's value) (2,1) gets 0.09 and (3,1) the floor while (6,0) gets 0.95 and (7,0) 0.99; at 0.5 the (2,0) fundamental and (6,0) fall to the floor; at 1.0 every odd-m mode is floored. Separately, `plate_modes.h:26-28` and `tools/gen-plate-chladni.js:7-9` attribute P = 1.7 to "Rossing & Peterson"; the audit could not find the number in any source it opened (Wilbur & Rossing 1996, 5pMU8, gives the per-family law with no numeric exponent; the published range is 1.4-2.4 for cymbals, handbells and bells).

**1. Failing test**

In `plugins/membrum/tests/unit/bodies/test_plate_body.cpp`:

- (a) `TEST_CASE("PlateModes: strike weight is the free-plate radial shape R_mn(rho)", "[body][plate][modes][strike]")`, on `computePlateAmplitude` directly (the mapper floor cannot mask it):
  - For every index k with `kPlateIndices[k].n == 0`: `CHECK(std::abs(computePlateAmplitude(k, 1.0f)) > std::abs(computePlateAmplitude(k, 0.2f)))` (edge strikes are rich in (m,0) modes, centre strikes are not: Rossing 2001, Acoust. Sci. & Tech. 22(3) §6.1).
  - Index 1 ((0,1)): sweep strikePos over 201 points; the strikePos with the smallest `|weight|` maps (through the new `rho = 0.05 + 0.9 * strikePos`) to `rho == Approx(0.680).margin(0.03)`.
  - Index 0 ((2,0)): `CHECK(std::abs(computePlateAmplitude(0, s)) > 0.01f)` for every s in [0.2, 1] on a 81-point grid.
  Red today: (3,0) at strikePos 1 is `cos(3 pi/2) = 0`; the (0,1) minimum is at rho 0.5 (strikePos 0.2); (2,0) is 0 at strikePos 0.5.
- (b) `TEST_CASE("PlateModes: radial shape table holds the free-plate solution", "[body][plate][modes][table]")`: `kPlateFlatLambdaSq[1] == Approx(9.003).epsilon(0.005)` (the (0,1) free-edge eigenvalue at Poisson ratio 0.3; value from the audit's solve of Leissa's free-edge equations, report section 5; Leissa 1969 §2.1.3 itself was not opened by the audit, say so in the test comment); for every k, the 33 samples of `kPlateRadialShape[k]` change sign exactly `kPlateIndices[k].n` times on rho in (0, 1). Red: the arrays do not exist.
- The existing `PlateModes: every mode responds to Strike Position` (`:97-122`) keeps its assertion (`hi - lo > 0.01` for every mode); only its comment changes to the radial model. `PlateModes: kPlateRatios == free-plate Chladni (m+2n) of kPlateIndices` (`:59-85`) stays and proves the frequency table did not move.

MB-203 verification is not a code test: search for the primary source of P = 1.7 (Rossing & Peterson 1982, Percussive Notes 19(3), and Rossing 1982, Am. J. Phys. 50:271). If a page, table or figure gives 1.7 for cymbals, MB-203 is refuted: add that page and figure to the two comments and record it. If none is found, the finding stands and only comments change (below).

**2. Fix**

- `tools/gen-plate-chladni.js`: keep the ratio output byte-identical and add the radial shapes. For each emitted (m,n): solve the free circular plate (Poisson ratio 0.3) for `W(r) = J_m(lambda r) + C I_m(lambda r)` with the free-edge conditions at r = 1 (radial bending moment M_r = 0 and Kelvin-Kirchhoff effective shear V_r = 0), C taken from the moment condition. Root order: for m = 0 and m = 1 the j-th elastic root (1-based) is n = j (the rigid (0,0) and (1,0) modes are excluded, as in the table); for m >= 2 the j-th root is n = j - 1. Assert inside the script that the solved shape has exactly n sign changes on (0,1) and that (0,1) gives lambda^2 = 9.003 within 0.5 % and its nodal radius 0.680 within 0.005; exit non-zero otherwise. Normalise each shape to unit modal mass (`integral_0^1 W^2 r dr = 1`), sample it at `rho_i = i / 32`, i = 0..32, and emit `inline constexpr float kPlateRadialShape[kPlateMaxModeCount][33]` and `inline constexpr float kPlateFlatLambdaSq[kPlateMaxModeCount]` (Bessel J_m and I_m by series or recurrence inside the script; Node only, no Python).
- `plugins/membrum/src/dsp/bodies/plate_modes.h`: paste the generated arrays; replace `computePlateAmplitude` (`:117-129`) with `rho = 0.05f + 0.9f * std::clamp(strikePos, 0.0f, 1.0f)` (centre to near edge) and linear interpolation in `kPlateRadialShape[modeIdx]`; no azimuth. Rewrite the comment at `:103-116`: azimuth does not select modes on an axisymmetric plate; the shape is Leissa's free-plate radial function; the frequencies stay on the (m + 2n)^P Chladni law while the shapes come from the flat plate, a stated mismatch.
- Floors: `plate_mapper.h:51` 0.05 -> 0.01 and `noise_body_mapper.h:111` 0.03 -> 0.01. Reason in the comment: the physical shape only nulls a mode on a true nodal circle; 0.01 keeps every mode above the bank's -80 dB amplitude cull (`kAmplitudeThresholdLinear`, `modal_resonator_bank.h:646`). If an SC-002 ratio case (`PlateBody: first 8 partial ratios within +/-3%`, `test_plate_body.cpp:124`, or `NoiseBody: modal layer uses plate ratios within +/-3%`, `test_noise_body.cpp:90`) can no longer find a partial because its strike position now sits on that mode's nodal circle, move that case's strike position off the circle and say which in the commit message; never widen the ±3 %.
- MB-203 (when not refuted): `plate_modes.h:26-28`, `tools/gen-plate-chladni.js:7-9` and the comment at `test_plate_body.cpp:49-58` say "P = 1.7 is a design choice inside the published 1.4-2.4 range for cymbals, handbells and church bells (Chladni's law as summarised from Fletcher & Rossing 1998 p. 680 and Rossing & Fletcher pp. 73-74; the audit read the summary, not those pages); the earlier attribution to Rossing & Peterson could not be traced (Wilbur & Rossing 1996, ASA 5pMU8, gives the per-family law f = C_n (m+2n)^P_n with no numeric exponent)". No value changes. The per-family law is a possible later model change, not part of this task.

**3. Build and gate**

Standard, with `Membrum` in the target list. Run `node tools/gen-plate-chladni.js` and confirm its ratio output is unchanged (diff it against `kPlateRatios`). PRESET-IMPACT: every Plate and every NoiseBody pad changes timbre (NoiseBody reuses `computePlateAmplitude`): kit-quality probe before and after, table, ruling.

**4. Acceptance**

- (a) and (b) green; `every mode responds` and the Chladni table test green unchanged.
- Every SC-002 and strike-sweep case in `test_plate_body.cpp` and `test_noise_body.cpp` green (`PlateBody: Strike Position sweep changes first-5 mode weights`, `:315`, keeps its `>= 0.10` L1 bar).
- Record, in the commit message, the new weights of (2,1), (3,1), (6,0), (7,0) at rho 0.55 (strikePos 0.5556) next to the verifiers' physical values (0.98, 1.00, 0.12, 0.09 as relative weights) and the old coded ones (0.09, 0.05, 0.95, 0.99).
- Parameter-audibility rows `offset=5:strikePosition` re-measured (baseline note 42, the NoiseBody hat: maxBandDistDb 4.98, maxCentroidChangePct 8.2) and listed with the kit table.
- MB-203: the search result (source found or not) is in the commit message.

### T4.10 — Bell: partials carry their real (m, n) labels, Strike Position is a strike height, and the reference-pitch partial is documented

**Findings:** MB-206 (medium, CONFIRMED), MB-247 (low, PLAUSIBLE), MB-180 (low, PLAUSIBLE).

**Flags:** PRESET-IMPACT, PHYSICS, VERIFY-FIRST (applies per finding to MB-247 and MB-180; MB-206's part runs regardless).

**Prerequisites:** none.

**Estimate:** 240 min.

What the code does today: `computeBellAmplitude` (`plugins/membrum/src/dsp/bodies/bell_modes.h:89-102`) maps Strike Position to an azimuth `theta = strikePos * pi/2` and returns `|cos(m * theta)| / (1 + 0.12 n)`; `BellMapper::map` floors it at 0.05 (`bell_mapper.h:43-48`). The header (`bell_modes.h:22-40`) presents this as the physical correction. On an axisymmetric bell every m > 0 partial is a degenerate (cos, sin) pair, so the strike azimuth does not change how strongly a partial is excited (Hibberts, "Doublets or warble in bells": azimuth only matters when asymmetry splits the pair, and then it redistributes energy between the two members); what changes the balance is the strike HEIGHT relative to each partial's nodal circles. At strikePos 1/3 the tierce and the quint (m = 3) fall to the floor, 19.0 dB under the nominal; at strikePos 1 every odd-m partial is floored. The labels (`kBellIndices`, `:72-76`) are right for the principal five (hum (2,0), prime (2,1), tierce (3,1), quint (3,1), nominal (4,1)) but off by one to three meridians above: index 5 (1.5 x nominal) is coded (4,1) and is the superquint / twelfth (5,1); index 6 (2.0) coded (5,1) is the octave nominal (6,1); index 7 (2.6) coded (5,1) is (7,1); index 8 (3.2) coded (6,1) is (8,1); index 9 (4.0) coded (6,2) is (9,1). Separately, `naturalFundamentalHz` returns the NOMINAL for Bell (`plugins/membrum/src/dsp/bodies/natural_fundamental.h:12`, `:41-42`), so the pitch envelope (`drum_voice.h:209-211`, `retuneFundamental` via `:1613`) and ModeInject (`drum_voice.h:326`) target the nominal; the hum sits two octaves below (probe `modes:body=Bell`, `probe-results.txt:287/292/297`: mode 0 at -2399 cents from `naturalFundamentalHz`). That convention is specified (spec 137 FR-032, data-model.md:412) and is the bell-founding convention, but `MANUAL.md` never says it.

**1. Failing test**

New file `plugins/membrum/tests/unit/bodies/test_bell_modes.cpp`, added to the `add_executable(membrum_tests` list.

- (a) MB-247 verification: `TEST_CASE("BellModes: partials carry their published (m, n) labels", "[body][bell][modes][table]")`. Expected values typed from Hibberts, "Identifying bell partials" (nodal meridian lines / 2) and Fletcher & Rossing's labels as quoted on Wikipedia "Strike tone": for k = 0..9, `m == {2, 2, 3, 3, 4, 5, 6, 7, 8, 9}` and `n == {0, 1, 1, 1, 1, 1, 1, 1, 1, 1}`; `waistCircle` true for k = 1 (prime, (2,1#)) and k = 3 (quint, (3,1#)) and false for the others. Red today at k = 5..9 (m = 4, 5, 5, 6, 6; n = 2 at k = 9). If it passes, MB-247 is refuted.
- (b) MB-206: `TEST_CASE("BellModes: Strike Position is a strike height; no partial is deleted", "[body][bell][modes][strike]")`. Over 101 strikePos values, with the mapper floor applied (`std::max(computeBellAmplitude(k, s), 0.05f)`, the floor `bell_mapper.h:46` uses): `CHECK(20 * log10(a_tierce / a_nominal) >= -12.0)` (k = 2 vs k = 4). And for every partial with published nodal heights: `computeBellAmplitude(k, h_c) < 1e-3f` at each of its heights `h_c` (strikePos = height). Red today: -19.0 dB at strikePos 1/3; the height check fails because the function knows no heights.
- (c) MB-180: `TEST_CASE("Bell pitch reference is the nominal partial and the manual says so", "[body][bell][pitch][docs]")`. Part 1 pins the convention at the mapper (no stretch warp involved): for size in {0, 0.5, 1}, `BellMapper::map(p, 0.f).frequencies[4] == Approx(naturalFundamentalHz(BodyModelType::Bell, size)).epsilon(1e-6)` and `frequencies[0] == Approx(0.25 * that)`. Green today. Part 2 reads `MEMBRUM_RESOURCES_DIR "/../MANUAL.md"` and checks the Bell row of the Bodies table (`MANUAL.md:195`) or the Bell note right after the table contains `nominal`. Red today: `MANUAL.md` never mentions the nominal. Part 2 is MB-180's verification. If the user rules the reference should move (see below), Part 1 changes with it; without a ruling the reference stays the nominal.

**2. Fix**

- `bell_modes.h`, the struct at `:62-66` and the table at `:68-76`: extend `BellModeIndices` with `bool waistCircle`, `int numNodalCircles` and `float nodalHeight[2]` (fraction of the lip-to-shoulder distance; 0 = lip / soundbow, 1 = shoulder) and `bool labelled`. Entries (source: Hibberts, "Identifying bell partials", after Perrin, Charnley & DePont 1983, which the audit could not read because the PDF is a scan; say so):
  - k0 hum (2,0): no nodal circle.
  - k1 prime (2,1#): circle at 0.30, waist.
  - k2 tierce (3,1): 0.54.
  - k3 quint (3,1#): 0.19, waist.
  - k4 nominal (4,1): 0.54.
  - k5 superquint / twelfth (5,1): 0.54 and 1.00.
  - k6 octave nominal (6,1): 0.28 and 0.98.
  - k7 (7,1), k8 (8,1), k9 (9,1): labels from Hibberts' meridian counts (14, 16 lines) and the Strike tone table; no nodal heights in the sources the audit read: `numNodalCircles = 0`.
  - k10..k15: no published (m,n) census in the sources the audit read: `labelled = false`, `m = 0`, `n = 0`, no circles. Comment it; do not invent labels.
- `computeBellAmplitude` (`:78-102`): `h = std::clamp(strikePos, 0.0f, 1.0f)`; weight = product over the partial's listed circles of `|h - h_c| / std::max(h_c, 1 - h_c)`; 1.0 when it lists none. No azimuth. Drop the `1 / (1 + 0.12 n)` taper: its only input was the invented n, and with real labels it would be a constant for every rim partial. Say in the comment that the zeros at the published heights are sourced and the linear profile between them is a design choice.
- Rewrite the header comment (`:22-40`): degenerate pairs on an axisymmetric bell, so azimuth does not select partials (Hibberts, "Doublets or warble in bells"; Cha, Buschert & King 2010, arXiv:1004.0491, abstract only read by the audit); Strike Position is the strike height from lip (0) to shoulder (1). Delete the "honesty note" that the labels above the principal five are approximate, and state which entries are labelled and which are not.
- MB-180 (when Part 2 was red): document, do not move, the reference. `MANUAL.md` Bodies table (`:195`) or a note under it: "Bell's pitch reference is the nominal partial: Size, the pitch envelope's Hz values and Mode Inject's fundamental all name the nominal; the hum sounds two octaves below it and the prime, the perceived strike note, one octave below." Add the same sentence to `natural_fundamental.h:12` and `bell_mapper.h:34-37`, and the Strike Position height mapping for Bell to the manual's Bell note. Ask the user whether the reference should become the prime (0.5 x nominal; the hum, as MB-180 first suggested, would put the perceived pitch an octave above the target). Default without a ruling: keep the nominal.
- Update the comment of `BellBody: Strike Position sweep changes first-5 mode weights` (`test_bell_body.cpp:227-233`) from "meridional strike azimuth" to strike height; its assertion stays.

**3. Build and gate**

Standard, with `Membrum` in the target list. T4.8 has already rewritten the Bell case of `test_decay_skew_all_bodies.cpp` to per-mode decay; it reads no strike weights, so it must stay green unchanged. PRESET-IMPACT: every Bell pad changes (for example the Glass Bell Garden kit): kit-quality probe before and after, table, ruling. The Strike Position knob's meaning on Bell changes from azimuth to height, so the ruling question includes it.

**4. Acceptance**

- (a), (b) and (c) green (or MB-247 / MB-180 recorded as refuted with the test output).
- Tierce / nominal worst case over the knob range recorded (baseline -19.0 dB at strikePos 1/3; expected after: 0 dB, since both have their circle at 0.54 and share the weight).
- `test_bell_body.cpp` green, including `BellBody: first 5 Chladni ratios within +/-3%` (`:44`) and the strike sweep L1 `>= 0.10` (`:231`).
- The probe lines `modes:body=Bell` keep mode 0 at -2400 cents from the reference: that is the documented hum position, not a defect.

### T4.11 — NoiseBody keeps 64 modes inside the audible band at every sample rate

**Findings:** MB-213 (medium, CONFIRMED), MB-215 (low, PLAUSIBLE).

**Flags:** PRESET-IMPACT, PHYSICS, VERIFY-FIRST (applies to MB-215 only; MB-213's part runs regardless).

**Prerequisites:** none. T4.5 (Mode Stretch neutral point) and T4.9 (strike weights) run earlier in plan order and both change NoiseBody; measure this task's numbers on the code they left. If T4.5 changed the NoiseBody stretch at modeStretch 1.0, recompute the mode counts below with the new value before writing the test.

**Estimate:** 240 min.

What the code does today: `NoiseBodyMapper::map` (`plugins/membrum/src/dsp/bodies/noise_body_mapper.h:89-150`) installs `kNoiseBodyModeCount = 64` modes (`:73`) at `f0 * kPlateRatios[k]` with `f0 = 1500 * 0.1^size` (`:95`, `:105-106`); the bank stretches them (`B = stretch^2 * 0.01`, `f_w = f * sqrt(1 + B (k+1)^2)`), scatters them (`* (1 + 0.10 * scatter * sin(k * kScatterD))`) and then culls every mode at or above `0.49 * sampleRate` (`modal_resonator_bank.h:783-819`). The table runs to `kPlateRatios[63] = 39.36`, so most of it lands above Nyquist. Recomputed by the verifiers (and again for this plan): default-kit Hat (size 0.15, stretch norm 1/3, scatter 0.10) f0 1061.9 Hz keeps 26 of 64 modes at 44.1 kHz, median gap 817 Hz; Cymbal (size 0.3, stretch norm 0.6, scatter 0.35) f0 751.8 Hz keeps 26, median gap 687 Hz; at 96 kHz 42 and 39, all extra modes ultrasonic. The comment at `:58-63` says 64 modes halve a 600 Hz spacing that it calls a pitched chime (defect D6); the realised cloud is sparser than that. Because the culled set depends on the rate, so does the crash: audit probe `srate:crash49` (`specs/membrum-audit/probes/probe-results.txt:436-441`) measured RMS(1 s) -29.69 / -29.68 / -32.41 / -34.23 dB and 40 ms centroid 8280 / 8664 / 13256 / 20225 Hz at 44.1 / 48 / 96 / 192 kHz (max deviation 4.55 dB and 133.4 % against the 1 dB and 10 % bars). Real cymbals start in the tens of Hz: Fletcher & Rossing Table 20.1 (n = 0 fits, c1 7.7-20.6 Hz, p about 1.5-1.86) puts the (2,0) mode at about 25-60 Hz, and Fig. 20.2 shows the first resonances of a 38-cm cymbal in the low hundreds of Hz (164 and 244 Hz among them).

Computed for this plan with the bank's formulas and the real table (Node; ceiling 19 kHz; truncation at the first mode whose worst-case bound exceeds it, see below):

| f0 law | Hat (size 0.15) | Cymbal (size 0.3) | Clap (size 0.25) |
|---|---|---|---|
| 1500 * 0.1^size (today) | 1062 Hz: 23 in band, gap 817 Hz | 752 Hz: 23, gap 687 Hz | 22, gap 958 Hz |
| 300 * 0.1^size | 212 Hz: 63, gap 266 Hz | 150 Hz: 56, gap 322 Hz | 53, gap 317 Hz |
| 250 * 0.1^size | 177 Hz: 64, gap 255 Hz | 125 Hz: 62, gap 287 Hz | 58, gap 301 Hz |

("gap" is the median spacing between installed modes in 2-10 kHz. Today's row counts modes under the 19 kHz ceiling; the bank itself keeps 26 at 44.1 kHz because its cull sits at 0.49 x 44100 = 21.6 kHz.) The 250 law is the highest of these that meets both bars for the default Hat and Cymbal, and its size-1 end (25 Hz) is the lower end of Table 20.1's (2,0) range. At full Stretch (norm 1) and full Scatter a size-0 pad still fits only 32 modes: the invariant this task guarantees is that the installed set is the same at every rate, and the 64-mode count holds at the default-kit settings.

**1. Failing test**

In `plugins/membrum/tests/unit/bodies/test_noise_body.cpp`:

- (a) `TEST_CASE("NoiseBody: default Hat and Cymbal keep a dense in-band cloud at every sample rate", "[body][noise_body][density]")`. For the default kit's Hat and Cymbal templates (`applyTemplate(cfg, DrumTemplate::Hat)` / `DrumTemplate::Cymbal`, `plugins/membrum/src/dsp/default_kit.h:40`; check the namespace) pushed into a DrumVoice with `applyPadConfigToVoice` (`pad_config_apply.h:30`), at 44100, 48000, 96000 and 192000 Hz: `noteOn(1.0f)`, then read the shared bank. `CHECK(getNumActiveModes() >= 56)`; the count is identical at all four rates; the median spacing of the installed `getModeFrequency(k)` values inside [2000, 10000] Hz is `< 300` Hz. Red today: 26 modes at 44.1 kHz, 42 / 39 at 96 kHz, gaps 817 / 687 Hz.
- (b) `TEST_CASE("NoiseBodyMapper: no installed mode exceeds the audible ceiling", "[body][noise_body][density]")`: bank prepared at 192 kHz, a grid of size {0, 0.5, 1} x modeStretch {0.5, 1.0, 1.4, 2.0} x modeScatter {0, 0.5, 1}: every nonzero `getModeFrequency(k) <= 19000.0f`. This guards the mapper's copy of the bank's warp bound against drift. Red today (modes up to 0.49 x 192 kHz).
- (c) MB-215 verification, run on the code after the MB-213 fix (the low band changes with it): `TEST_CASE("NoiseBody crash: 3-5 kHz builds up and the low band hands energy upward (F&R 20.3)", "[body][noise_body][crash]")`. Default-kit Cymbal pad into a DrumVoice at 48 kHz, rendered 400 ms at velocity 1.0 and at 0.25. Band levels over 10 ms windows with the band-level helper in `tests/test_helpers/spectral_analysis.h` (read the header for the exact function; if it has no band-level function, use `signal_metrics.h` or the band helper in `unit/bodies/body_test_helpers.h`, never a new copy). Checks, typed from Fletcher & Rossing §20.3 (Rossing & Shepherd 1983): `L(3-5 kHz, window at 75 ms) - L(3-5 kHz, window at 5 ms) >= 6 dB`; `L(< 700 Hz, window at 0 ms) - L(< 700 Hz, window at 200 ms) >= 10 dB`; spectral centroid (first 50 ms) at velocity 1.0 over velocity 0.25 `> 1.2`.

**2. Fix**

- `noise_body_mapper.h:95`: `f0 = 250.0f * std::pow(0.1f, params.size)` (25-250 Hz). Comment: Fletcher & Rossing 1998, Table 20.1 and Fig. 20.2 (as above); 250 Hz at size 0 is this plan's choice, the highest round value that keeps the default Hat and Cymbal clouds at >= 56 modes with < 300 Hz spacing (the table above).
- `plugins/membrum/src/dsp/bodies/natural_fundamental.h:43` (NoiseBody) and its comment at `:13` move to the same law in lock-step (the helper mirrors the mappers for the pitch envelope, ModeInject and tension). If T2.18 added a per-body Size readout in the controller, change its NoiseBody formula too (grep `1500` in `plugins/membrum/src/controller/`).
- Band-limit before installing (`:97-106`): add `kNoiseBodyCeilingHz = 19000.0f` (below 0.45 x 44100 = 19845 Hz, so the installed set is the same at every rate from 44.1 kHz up; between 22.05 and 44.1 kHz (`VoicePool` clamps the rate to [22050, 192000]) the bank's own Nyquist cull still removes modes, and the comment says so). Walk k = 0.. up to the existing cap (`kNoiseBodyModeCount`, or `params.maxModes` for the Feedback slow path, `:102-103`) and stop at the first k whose worst-case bank frequency `f0 * kPlateRatios[k] * sqrt(1 + B (k+1)^2) * (1 + 0.10 * scatter)` exceeds the ceiling, with `B = stretchNorm^2 * 0.01` from the `stretchNorm` the mapper already computes (`:118-120`; move that computation above the loop). Install only k below that index (truncating the tail keeps every installed mode's index, so the bank's `n = k + 1` stretch is unchanged). `numPartials` becomes that count. Comment that the two constants mirror `modal_resonator_bank.h:783-784` and that test (b) pins them.
- Rewrite the comment at `:58-63` with the measured numbers (today 26 of 64 at 44.1 kHz; after, the table above).
- `noise_body_mapper.h:164-176` (wash decay from the lowest-mode T60) needs no edit: it reads the new f0.
- MB-215, only after case (c) was run: if (c) passes, MB-215 is refuted; keep the test. If it fails, the finding is confirmed, but the fix it needs (an energy-transfer term inside the body, Chaigne, Touze & Thomas 2005) reverses a documented non-goal (`plugins/membrum/CRASH-REDESIGN-PLAN.md:339-344`) and is a new model feature: stop that part for the user's ruling, as T4.20 and T4.24 do. Do not commit a red test: save case (c) as `specs/membrum-audit/probes/noisebody_time_evolution_probe.cpp.txt` with its measured numbers in a header comment, and commit that file instead. In either case correct the stale comment at `default_kit.h:241` ("lowest-mode T60 ~ 4.6 s"): with b1 = 1.196 s^-1, b3 = 8e-8 s and the new f0 of 125 Hz the lowest mode's T60 is 6.9078 / 1.197 = 5.8 s (today, at 752 Hz, it is 5.57 s); write the value the code gives after T1.23's encoding.
- Tests whose expected frequencies follow the old law change with it, without loosening a tolerance: `naturalFundamentalHz helper matches per-body Size->f0 formulas` (`plugins/membrum/tests/unit/dsp/test_natural_fundamental_hz.cpp:62`) and `NoiseBody: modal layer uses plate ratios within +/-3%` (`test_noise_body.cpp:90`). If the latter measured partials that now sit below its analysis window, keep its ±3 % and move the window.

**3. Build and gate**

Standard, with `Membrum` in the target list. PRESET-IMPACT: every NoiseBody pad changes (default kit: hats 42 / 44 / 46, crash and ride cymbals 49 / 51 / 52 / 53 / 55 / 57, clap 39; and every factory NoiseBody pad). The hats in particular gain partials between about 180 Hz and 1 kHz that they do not have today. Kit-quality probe before and after, the table, and the user's ruling before the commit. Also re-run the `srate:crash49` case of `audit_probes.cpp.txt` (see "Measuring with the audit probes") before and after.

**4. Acceptance**

- (a) green: record the counts per rate (baseline 26 / 26 at 44.1 kHz, 42 / 39 at 96 kHz; expected after 64 / 62 at every rate) and the median gaps (baseline 817 / 687 Hz; expected 255 / 287 Hz).
- (b) green.
- `srate:crash49`: max RMS(1 s) deviation against 48 kHz <= 1 dB (baseline 4.55 dB) and max centroid(40 ms) deviation <= 10 % (baseline 133.4 %). If the centroid still fails because of noise- or click-layer content above 20 kHz at 192 kHz (T2.13 owns the sample-rate dependence of those layers), record the measured value and that attribution; do not change the bar.
- Every other case in `test_noise_body.cpp` and `test_natural_fundamental_hz.cpp` green.
- (c): either green (MB-215 refuted) or saved as a probe with its numbers and the ruling question asked; the `default_kit.h:241` comment corrected either way.

### T4.12 — ImpactExciter follows Hertz contact-time and momentum laws, and its noise shaper is really pink

**Findings:** MB-225 (medium, CONFIRMED), MB-248 (low, PLAUSIBLE), MB-250 (low, PLAUSIBLE), MB-251 (low, PLAUSIBLE).

**Flags:** CROSS-PLUGIN IMPACT, PRESET-IMPACT, PHYSICS, VERIFY-FIRST.

**Prerequisites:** none.

**Estimate:** 300 minutes.

Context the executor needs. `Krate::DSP::ImpactExciter` (`dsp/include/krate/dsp/processors/impact_exciter.h`) drives Membrum's Impulse exciter (`plugins/membrum/src/dsp/exciters/impulse_exciter.h:35-40`, `ImpulseExciter::trigger`) and Mallet exciter (`plugins/membrum/src/dsp/exciters/mallet_exciter.h:69-91`, `MalletExciter::trigger`), and Innexus's Impact exciter (`plugins/innexus/src/processor/processor_midi.cpp:289`, member at `plugins/innexus/src/processor/innexus_voice.h:52`). In `ImpactExciter::trigger` (`impact_exciter.h:113-218`):

- `amplitude_ = std::pow(velocity, 0.6f)` (:129).
- `T = kTMin + (kTMax - kTMin) * std::pow(mass, 0.4f)` with 0.5 ms and 15 ms (:132-134), then `T *= std::pow(std::max(1.0f - velocity, 0.01f), 0.2f)` (:138-139) and a ±5 % jitter (:143). Hardness reaches only gamma, skew, the noise level and the SVF cutoff (:120-178), never `T`.
- `ImpactExciter::process` (:227-304) shapes the noise with `pink = white - 0.9f * pinkState_` (:246-248): a one-pole with its pole at z = -0.9, which is a high-pass tilt (+25.6 dB from DC to Nyquist), not pinking.

The voice multiplies by velocity a second time through the amp envelope (`plugins/membrum/src/dsp/drum_voice.h:163` `ampEnvelope_.setVelocityScaling(true)`, :436 `ampEnvelope_.setVelocity(velocity)`) and normalises the body to a strike probed at velocity 1.0 (`DrumVoice::probeStrikePeakExcited`, :1322-1362). Net result, measured: the delivered impulse peaks near v = 0.75 and v = 1.0 hits a low mode 4.2 dB less hard (MB-225).

Decisions this task makes, so the executor does not have to:

- **Opt-in model, Innexus untouched.** Innexus has its own archived spec (spec 128, which fixed the `(1 - v)^0.2` law as FR-020 and the noise recursion as FR-010) and no finding against it. The new laws therefore sit behind a new setter. The default `Legacy` model stays bit-identical, and only Membrum's two wrappers select `Hertz`. The misleading "pinking" comment is corrected for both models, which changes no output.
- **Contact time (Hertz).** `T = Tmass(mass) * kHertzAnchor * kRel^-0.4 * u^-0.2`:
  - `Tmass` is the existing mass term, kept with its 0.5 ms floor. The D4 small-metal contact calibration in `mallet_exciter.h:51-63` and the `[D4]` tests depend on it, and no finding measured a defect from the floor at the masses Membrum uses.
  - `u = 0.1 + 0.9 * velocity` maps MIDI velocity to impact speed (never 0).
  - `kRel = 2^(2 * (effectiveHardness - 0.5))` maps hardness to relative stiffness. This is a perceptual map with no published value, and the comment must say so.
  - The exponents -0.4 (stiffness) and -0.2 (speed) are Hertz's: K. L. Johnson, *Contact Mechanics*, CUP 1985, ch. 11; A. He and J. S. Wettlaufer, "Hertz beyond belief", Soft Matter 10, 2014, eq. (1a), T_e = 2.8683 (M^2 / (R V E*^2))^(1/5).
  - `kHertzAnchor = 0.4970f` keeps today's full-velocity Impulse contact. At v = 1 and effective hardness 0.9 the old factor was 0.01^0.2 = 0.3981, and kRel^-0.4 = 2^-0.32 = 0.8011, so 0.3981 / 0.8011 = 0.4970. Write that derivation in the comment.
  - Resulting nominal contact at mass 0.3: Impulse 7.1 / 5.0 / 3.77 ms and Mallet 7.9 / 5.6 / 4.2 ms at v = 0.1 / 0.5 / 1.0. The drumhead literature reports 3-7.5 ms (A. Wagner, KTH MSc thesis 2006, Fig. 4.7) and 5-8 ms at mezzoforte (S. Dahl, Acoust. Sci. & Tech. 32(5), 2011).
- **Momentum (Hertz).** The pulse area (the drive of the low modes) is proportional to velocity: impulse = m v (1 + e) (Johnson 1985, ch. 11). Peak force then rises by itself as the contact shortens.
- **Noise (Hertz).** The noise shaper becomes Kellet's pink filter, which already exists as `Krate::DSP::PinkNoiseFilter` (`dsp/include/krate/dsp/primitives/pink_noise_filter.h`, Layer 1, so the Layer 2 exciter may use it).
- **The FR-034 energy cap stays** in both models (`impact_exciter.h:77-80`, :296-301). It is inert for a single strike (MB-225 measured 0.177 of its threshold), but the retrigger-storm test `ImpactExciter 100 rapid triggers: peak never exceeds 4x single-strike (SC-010)` relies on it.
- **The amp envelope's velocity scaling is not changed here.** With area ∝ v and the envelope ∝ v, the low-mode level of an Impulse or Mallet pad goes as v^2. That is -24 dB at v = 0.25, against about -19 dB today (MB-225's "v^1.6 law" below the inversion). The before/after table shows it and the user rules on it (see step 4).

**1. Failing test**

Three cases in `dsp/tests/unit/processors/impact_exciter_test.cpp` (builds into `dsp_processors_tests`), and one in a new Membrum file. Step 1 may first add two things that change no output: the read-only accessor `[[nodiscard]] float getNominalContactTime() const noexcept` (seconds, `T` before the ±5 % jitter), and a nested `ImpactExciter::NoiseShaper` struct holding the existing recursion verbatim, which `process()` calls. Confirm the existing `[impact_exciter]` cases stay green after that refactor. Every Hertz case is written first WITHOUT the `setContactModel(ImpactContactModel::Hertz)` call, so it runs the current law and must fail. Step 2 adds the call.

- `TEST_CASE("ImpactExciter Hertz model: delivered impulse is proportional to velocity (MB-225)", "[processors][impact_exciter][hertz]")`
  - Use Impulse's map (hardness `0.3 + 0.5v`, brightness `0.15 + 0.25v`, mass 0.3, position 0, f0 0) for v = 0.1, 0.2, ..., 1.0 at 44.1 kHz.
  - Sum the output over 0.1 s. That is the net impulse: the SVF lowpass has unity DC gain and the comb is off. Average the sum over voiceIds 0..15 so the noise term averages out.
  - Assert `impulse(v) / v` stays within ±5 % of its mean over all ten velocities. Repeat with Mallet's map (hardness `0.1 + 0.5v`, brightness `-0.5 + 0.3v`, mass 0.3).
  - Baseline (MB-225 probe): Impulse net impulse 48.7 / 103.4 / 116.8 / 104.0 / 92.2 / 67.9 at v = 0.1 / 0.5 / 0.75 / 0.9 / 0.95 / 1.0, so `impulse / v` runs from 487 down to 68. Fails.
- `TEST_CASE("ImpactExciter Hertz model: contact time follows the Hertz speed and stiffness laws (MB-248)", "[processors][impact_exciter][hertz]")`
  - Read `getNominalContactTime()` after each trigger.
  - (i) For both maps, `T(1.0) / T(0.5)` lies in [0.6, 0.9].
  - (ii) T strictly decreases over v = 0.1..1.0 in steps of 0.05.
  - (iii) For every adjacent pair in [0.1, 1.0], `|ln T(v + 0.05) - ln T(v)| / 0.05 < 1.5`, so there is no top-end cliff.
  - (iv) At v = 0.5 and mass 0.3, T strictly decreases over hardness {0.1, 0.3, 0.5, 0.7, 0.9}.
  - (v) With `hardness = 0.8 - 0.1v` (effective hardness constant at 0.8), `T(0.25) / T(1.0) = 1.2523 ± 1 %`. That value is typed from the Hertz exponent -1/5 (He & Wettlaufer eq. 1a) as `(0.325 / 1.0)^-0.2`, with u = 0.1 + 0.9v.
  - Baseline: T(1)/T(0.5) = 0.457. The log-slope at v = 0.95 is 4.0. T does not depend on hardness. Fails (i), (iii) and (iv), which is the MB-248 verification.
- `TEST_CASE("ImpactExciter noise shaper: Hertz model noise is pink, not high-pass (MB-251)", "[processors][impact_exciter][hertz]")`
  - Drive the `NoiseShaper` of an exciter prepared at 48 kHz with 2^16 samples of `Krate::DSP::Xorshift32` white noise.
  - Average power spectra over 64 Hann frames of 1024 samples (use `sumBinPower` from `tests/test_helpers/spectral_analysis.h`; read its signature first).
  - Assert that the 16-20 kHz band level is at least 6 dB below the 1-2 kHz band level. An ideal -3 dB/octave slope gives about -10.8 dB over those 3.6 octaves.
  - Baseline: the legacy recursion has |H| = 0.527 at 1 kHz and 2.0 at 20 kHz, so the HF band is about 11 dB ABOVE the 1-2 kHz band. Fails, which is the MB-251 verification.
- New file `plugins/membrum/tests/unit/dsp/test_strike_velocity_law.cpp` (add it to the `membrum_tests` source list):
  - `TEST_CASE("Strike velocity law: Impulse and Mallet pads get louder up to full velocity (MB-225)", "[membrum][dsp][velocity]")`.
  - Fill a `std::array<PadConfig, kNumPads>` with `Membrum::DefaultKit::apply` (`dsp/default_kit.h:398`). For pad 0 (Kick, Impulse) and pad 5 (Low Floor Tom, Mallet), prepare a `DrumVoice` at 44.1 kHz with voiceId 0, call `applyPadConfigToVoice` (`dsp/pad_config_apply.h:30`) and `noteOn(v)`, then render 300 ms with `processBlock` in 256-sample blocks.
  - Measure the RMS, and the fundamental-band magnitude with `goertzelWindowMag` from `plugins/membrum/tests/unit/bodies/body_test_helpers.h` (include it by relative path; read its signature first) at `getBodyBankForTest().getSharedBank().getModeFrequency(0)`, read right after `noteOn`.
  - Assert both strictly increase over v ∈ {0.25, 0.5, 0.75, 0.85, 0.9, 0.95, 1.0}.
  - Baseline: kick RMS 0.0810 at v = 0.9 against 0.0728 at v = 1.0. Floor tom 0.1135 against 0.0950, and its peak 0.4995 at v = 0.95 against 0.4775 at v = 1.0. Fails.

MB-250 is a documentation finding: it says the comment and spec are wrong and the code is right. Its verification is a read, not a red test. If `mallet_exciter.h:10-29` (hardness `lerp(0.05, 0.45, v)`, mass `lerp(0.6, 0.3, v)`, "8 ms → 1 ms", brightness `lerp(-0.8, -0.1, v)`) still disagrees with `:81-90`, as it did at `4a14376d`, the finding stands.

Pin it with `TEST_CASE("MalletExciter: drum-size contact stays in the measured drumhead range (MB-250)", "[exciters][mallet][hertz]")` in `plugins/membrum/tests/unit/exciters/test_mallet_exciter.cpp`. With `setBodySizeHint(1.0f)`, `core_.getNominalContactTime()` for v ∈ {0.1, 0.5, 1.0} must lie in [3.0, 9.5] ms (Wagner 2006: 3-7.5 ms; Dahl 2011: 5-8 ms; 9.5 ms is today's v = 0.1 value). The pin passes before and after the fix.

**2. Fix**

Run `grep -rn "ImpactContactModel" dsp/ plugins/` first (ODR); it must find nothing.

- `dsp/include/krate/dsp/processors/impact_exciter.h`:
  - Add `enum class ImpactContactModel : std::uint8_t { Legacy, Hertz };` in `Krate::DSP`.
  - Add the member `contactModel_ = ImpactContactModel::Legacy`, `void setContactModel(ImpactContactModel m) noexcept`, and the `getNominalContactTime()` accessor and `NoiseShaper` struct from step 1.
  - In `ImpactExciter::trigger`, the Legacy branch keeps :128-139 and the micro-bounce block (:193-217) byte-for-byte. The Hertz branch:
    - Computes `T` by the law above, then applies the same `T` jitter draw (:143). Record the pre-jitter value for `getNominalContactTime()` in both models.
    - Keeps the RNG draw order of the legacy path (gamma jitter, T jitter, bounce delay, bounce amplitude) and sets `amplitude_` AFTER the micro-bounce block: `amplitude_ = velocity * areaRef_ / (T * meanShape * (1 + 0.5f * bounceFrac))`, where `bounceFrac` is 0 when `effectiveHardness <= 0.6f`, then `bounceAmplitude_ = amplitude_ * bounceFrac`.
    - `meanShape` is a 64-point midpoint sum, `(1/64) * Σ_{i=0..63} pow(sin(kPi * pow((i + 0.5f) / 64, 1 - skew_)), gamma_)`, with the jittered gamma. It is bounded, allocation-free work at note-on.
    - Compute `areaRef_` in `prepare()` from the same expression at v = 1, effective hardness 0.9, mass 0.3, no jitter and nominal bounce fraction 0.125 (the value of :202 at hardness 0.9). A full-velocity Impulse strike then keeps amplitude 1.0.
  - Add `PinkNoiseFilter pink_` to `NoiseShaper`:
    - Call `pink_.prepare(static_cast<float>(sampleRate))` in `ImpactExciter::prepare`. Reset the filter state where :160 resets `pinkState_` (the filter has `reset()`, `pink_noise_filter.h:134`).
    - Hertz output: `pink_.process(white) * kPinkTrim`. `kPinkTrim` makes the 1-2 kHz band level equal to the legacy shaper's: measure it once in the MB-251 test and write the measured value and the method next to the constant.
    - Legacy output: the old recursion. Correct its comment to "one-pole high-pass tilt (pole at z = -0.9), kept verbatim for spec-128 consumers; not a pinking filter (J. O. Smith, *Introduction to Digital Filters*, One-Pole section)".
  - Correct the `// FR-005 ... (Hertzian scaling)` comment at :131 to name which law each model uses.
  - Add `TEST_CASE("ImpactExciter Hertz model: 100 rapid triggers stay under 4x single-strike peak (SC-010)", "[processors][impact_exciter][hertz][energy]")`, the SC-010 case with `setContactModel(Hertz)`, as a pin.
- `plugins/membrum/src/dsp/exciters/impulse_exciter.h` and `mallet_exciter.h`:
  - In each `prepare()`, call `core_.setContactModel(Krate::DSP::ImpactContactModel::Hertz);` after `core_.prepare(...)`.
  - Rewrite `impulse_exciter.h:6-11` and `mallet_exciter.h:10-29` to the real mapping. Mallet: hardness 0.1..0.6, brightness -0.5..-0.2, mass `0.3 * min(size / 0.5, 1)^3` with no velocity term. Both: the Hertz contact-time and momentum laws with their sources, and nominal contact about 7.9 ms (v 0.1) to 4.2 ms (v 1) for Mallet and 7.1 to 3.77 ms for Impulse at size ≥ 0.5.
- `specs/135-membrum-drum-synth.md`:
  - Line 127: replace "~8ms (soft) to ~1ms (hard stick)" with "about 8 ms (soft) to about 4 ms (hard) on a drumhead (Wagner 2006: 3-7.5 ms; Dahl 2011: 5-8 ms at mezzoforte)".
  - Under the "Mallet-Membrane Interaction" table (around :644-648), add one sentence: the 1-3 ms "Wood stick tip" row comes from bar and hammer studies (its source line cites Chaigne & Doutaut and Boutillon), and stick-on-drumhead contact is 3-8 ms. Change no row.
- Do not touch `drum_voice.h`.

Expected collateral, never "fixed" by relaxing an assertion:

- The Phase-1 golden `plugins/membrum/tests/golden/phase1_default.bin` (`approval/test_phase1_regression.cpp`) moves, because the default patch uses Impulse. Regenerate it deliberately with `build/windows-x64-release/bin/Release/membrum_tests.exe "Generate Phase 1 golden reference"` and justify it in the commit message.
- `Phase 8E: Tom tension modulation drifts pitch vs velocity` (`tests/unit/dsp/test_per_mode_damping.cpp:481`) is calibrated against the legacy Mallet contact energy (`mallet_exciter.h:56-57` says so). If it leaves its 1.0-2.5 semitone window, re-derive `kTensionEnergyScale` (`drum_voice.h:94`) so a full-knob v = 1 Mallet tom glides 1.5 semitones again, and record the old and new values. T4.14 replaces that scale.
- The SC-004 centroid-ratio cases (`test_velocity_mapping.cpp:60`, `test_impulse_exciter.cpp:43`, `test_mallet_exciter.cpp:38`, the US1-2 case at `test_mallet_exciter.cpp:138`) and the `[D4]` cases in `test_mallet_contact_hint.cpp` must stay green unchanged. Soft hits now have a shorter contact (7.1 ms instead of 9.26 ms) and are slightly brighter. If an SC-004 ratio drops below 2.0, stop and report the measured ratio.

**3. Build and gate**

- CROSS-PLUGIN consumers of `impact_exciter.h` read at `4a14376d`: `plugins/innexus/src/processor/innexus_voice.h`, `plugins/innexus/tests/unit/processor/bow_exciter_switch_test.cpp`, `dsp/tests/unit/processors/impact_exciter_test.cpp`, `plugins/membrum/src/dsp/exciters/impulse_exciter.h`, `plugins/membrum/src/dsp/exciters/mallet_exciter.h`. The grep is authoritative.
- Build targets: `dsp_processors_tests membrum_tests Membrum`.
- Run `node tools/header-consumers.js --tag '[membrum]'` and add every executable it names (expect `innexus_tests`). Gate them with `~[long]`. Innexus must stay green without any golden moving, because the Legacy path is unchanged. Say so in the commit message.
- clang-tidy also runs `-Target dsp`.
- PRESET-IMPACT: no kit data changes, so no regeneration. Run the kit-quality probe before and after the fix (Impulse and Mallet are used in about 126 generator places). Also run the `audit_probes` velocity-curve probes (`level:velocity-curve:pad36/38/41/42`).

**4. Acceptance**

- The three new `[hertz]` cases in `dsp_processors_tests`, the SC-010 Hertz pin, the MB-250 Mallet pin and the MB-225 voice case pass.
- Impulse and Mallet `impulse / v` vary by at most ±5 % (baseline: 487 to 68).
- T(1)/T(0.5) lies in [0.6, 0.9] (baseline 0.457).
- The 16-20 kHz noise band sits at least 6 dB under the 1-2 kHz band (baseline about +11 dB over it).
- Default-kit kick and floor-tom RMS strictly increase up to v = 1.0. Baseline: -0.9 dB (kick) and -1.5 dB (tom) from v = 0.9 to 1.0.
- Every existing `[impact_exciter]` case and the Innexus suite are unchanged and green.
- Before the commit, show the user the PRESET-IMPACT table. Add the velocity-curve rows: today pad 36 runs -41.3 dBFS (v 0.1) to -8.0 dBFS (v 1.0), a 33.25 dB range, and pad 41 runs 33.18 dB. Ask: "Impulse and Mallet pads now follow an impulse ∝ velocity law, times the amp envelope's velocity scaling: about v^2 in low-mode level. Keep it, or remove the amp envelope's velocity scaling for these exciters?" Stop until the user rules.
- Record per finding whether it was confirmed by its failing test, or refuted (VERIFY-FIRST) with the test output.

### T4.13 — The noise layer and the snare-wire buzz follow strike velocity

**Findings:** MB-229 (medium, CONFIRMED), MB-178 (medium, PLAUSIBLE), MB-257 (low, PLAUSIBLE).

**Flags:** PRESET-IMPACT, PHYSICS, VERIFY-FIRST.

**Prerequisites:** T2.14. This task also runs after T4.12 in plan order, and its Impulse and Mallet strike law depends on T4.12's outcome (see step 2).

**Estimate:** 240 minutes.

Context:

- `NoiseLayer::trigger(float /*velocity*/)` (`plugins/membrum/src/dsp/noise_layer.h:158-170`) discards velocity, and `processSample` / `processBlock` (:178-215) return `mix_ * env * f`.
- DrumVoice scales the layer by `standaloneGain() * noiseLayerGain_ * wireGainMod_` on all three paths: `process()` `drum_voice.h:547-549`, `processBlockFast` :855-864, `processBlockSlow` :1050-1051.
- The only velocity factor the noise shares with the body is the amp envelope (:163, :436), so noise ∝ v while an Impulse or Mallet body runs at about v^1.6 or steeper. Measured on the default snare (voice probe, 48 kHz): the noise-to-body RMS (0-50 ms) is -5.8 / +1.3 / +6.1 dB at v = 1.0 / 0.5 / 0.3.
- The wire gain `DrumVoice::computeWireGainMod` (:1499-1515) divides the 20 ms follower `energyEnv_` by its own running peak `wireEnergyPeak_`. The comment says that makes the gain "velocity- and gain-staging-independent". So the buzz-to-head ratio carries no velocity cue (MB-257).
- `ClickLayer::trigger` (`click_layer.h:87-133`) already multiplies by velocity, so the noise layer is the odd one out.

**1. Failing test**

The voice-level cases go in `plugins/membrum/tests/unit/dsp/test_strike_velocity_law.cpp`. T4.12 created it; create it and add it to the `membrum_tests` source list if T4.12 closed without it.

- `TEST_CASE("Noise layer follows the body's velocity law (MB-229)", "[membrum][dsp][velocity][noise_layer]")`
  - Use `DefaultKit::apply` pads 0 (Kick), 2 (Snare), 5 (Low Floor Tom) and 6 (Closed Hat), at 48 kHz with voiceId 0 and `applyPadConfigToVoice`. Call `setWireCoupling(0.0f)` on the snare after applying the config; the wire law has its own case below.
  - For each pad, `REQUIRE` that `tsDriveAmount`, `tsFoldAmount` and `nonlinearCoupling` are 0 in its `PadConfig` (`pad_config.h:200-201`, :215). That makes the post chain linear, so the difference of two renders is the noise component. If a pad fails the `REQUIRE`, replace it with another default-kit pad of the same category that passes, and name it in the commit message.
  - For v ∈ {1.0, 0.5, 0.3}, render 50 ms twice in 256-sample blocks: as configured, and with `setNoiseLayerMix(0.0f)`. Noise = full minus noise-off; body = noise-off.
  - `R(v) = RMS(noise) / RMS(body)`. Assert `|20 log10(R(v) / R(1.0))| <= 2 dB` for v = 0.5 and v = 0.3.
  - Baseline, v = 1 to v = 0.3: kick -14.6 → -12.1 dB (+2.5), floor tom -16.4 → -13.4 (+3.0), closed hat -24.9 → -19.2 (+5.7). Fails.
- In `plugins/membrum/tests/unit/dsp/test_wire_coupling.cpp`, `TEST_CASE("Wire coupling: soft hits rattle proportionally less than hard hits (MB-257)", "[membrum][dsp][wire-coupling]")`
  - Use that file's `snareConfig()` and `applyConfig()` helpers and keep the snare's own `wireCoupling` 0.45 (`default_kit.h:124`).
  - Render v = 0.2 and v = 1.0 for 150 ms in 256-sample blocks.
  - `ratioDb(v)` = band energy 4-8 kHz minus band energy 100-400 Hz, in dB, over 0-150 ms (power spectrum via `sumBinPower`, `tests/test_helpers/spectral_analysis.h`).
  - Assert `ratioDb(0.2) <= ratioDb(1.0) - 3.0`.
  - Baseline: the noise grows relative to the head on soft hits (MB-229, broadband: the snare's noise-to-body ratio is 11.9 dB higher at v = 0.3 than at v = 1.0). The case is expected to fail, which is the MB-257 verification. A green run on current code means refuted: keep the test, drop the wire part of step 2.
- MB-178's claim, that the noise layer causes the inverted brightness, is verified with the audit probe, not a committed test (T8.4 promotes it later).
  - Copy `specs/membrum-audit/probes/audit_velocity_centroid_probes.cpp.txt` in per "Measuring with the audit probes" and compare its `V0 as-is` and `V2 lev+noiseMix0` rows with `velocity-centroid-results.txt` run 1b (level as-is, centroid ratio v127 / v40).
  - Noise-off lifts the ratio for 909 Drum Machine:38 (1.038 → 1.373), Orchestral:38 (1.002 → 1.136), Rock Big Room:38 (0.888 → 1.431) and Vintage Wood:38 (0.860 → 1.248). MB-178 stands for these four pads.
  - Noise-off does not lift it for Acoustic Studio Kit:36 (0.691 / 0.689) and :38 (0.792 / 0.783), Hand Drums:38 (0.959 / 0.952), Hand Drums:42 and Latin Perc:36 (0.528 / 0.517), Latin Perc:38, Drone and Sustain:36 (0.999 / 0.999) and Ghost Bones:38 (0.967 / 0.956). MB-178 is refuted for those pads: record them as "not the noise layer" in the commit message and the checklist. Acoustic Studio Kit:36 is driven by its pitch envelope: probe section (F) gives a ratio of 1.349 with the pitch envelope off and 0.584 with it on. Do not fix other causes here.

**2. Fix**

- **Strike law per exciter.** Add `[[nodiscard]] float strikeLevel(float velocity) const noexcept` to every exciter wrapper in `plugins/membrum/src/dsp/exciters/`, and an `ExciterBank::strikeLevel(float velocity) const noexcept` that visits the active exciter (`exciter_bank.h`, beside `trigger` :53-58). The value is the exciter's own level law:
  - `ImpulseExciter`, `MalletExciter`: `velocity` (pulse area ∝ v under T4.12's Hertz model). If T4.12 was refuted or ruled out, return `std::pow(velocity, 0.6f)`, the legacy `amplitude_` law (`impact_exciter.h:129`), and expect the ±2 dB check to need the user's ruling.
  - `NoiseBurstExciter`, `ClapExciter`: `0.2f + 0.8f * velocity` (`noise_burst_exciter.h:110`, `clap_exciter.h:108`).
  - `FMImpulseExciter`: `velocity` (`fm_impulse_exciter.h:93`).
  - `FrictionExciter`, `FeedbackExciter`: `1.0f`. These are sustained drives whose strike level has no closed form; this keeps today's noise law for them, and the comment says so.
- **Noise layer.** In `NoiseLayer::trigger` (`noise_layer.h:158`), name the parameter `levelGain` and store `levelGain_ = std::clamp(levelGain, 0.0f, 1.0f)`. `processSample` and `processBlock` return `mix_ * levelGain_ * env * f`.
  - `NoiseBody` keeps calling `trigger(1.0f)` (`bodies/noise_body.h:78`), so the NoiseBody output is bit-identical.
  - In `DrumVoice::noteOn`, call `noiseLayer_.trigger(exciterBank_.strikeLevel(velocity))` (`drum_voice.h:372`). `exciterBank_.trigger(velocity)` at :343 has already applied the deferred exciter swap.
  - Update the doc comment above `trigger` and the misleading "bandpass" wording at :155 and :173-176.
- **Reference energy (MB-257).**
  - In `DrumVoice::probeStrikePeakExcited` (:1322-1362), track `refEnergy = max(bank.getModalEnergy())`, read every 32 samples of the probe loop. Return it next to the peak (an out-parameter or a small struct).
  - Cache it as `strikeRefEnergy_` beside `strikeNormGain_` (:1728-1729), re-measured under the same `StrikeNormKey` (`measuredStrikeOutputGain`, :1394-1412).
  - The Feedback branch and the String branch of `noteOn` (:272-284) run no probe, so set `strikeRefEnergy_ = 0.0f` there, meaning "not measured".
- **Wire gain.** In `DrumVoice::computeWireGainMod` (:1506-1515), when `strikeRefEnergy_ > 0`, use `norm = std::clamp(e / strikeRefEnergy_, 0.0f, 1.0f)`. Here `e` is the instantaneous `getModalEnergy()` value read at the chunk end (`processBlockFast` :912, `processBlockSlow` :1075; pass it in), not `energyEnv_`: the 20 ms follower starts at 0 at note-on and would mute the onset. Otherwise keep today's running-peak ratio. Keep `track = sqrt(norm)` and `gain = 1 + wireCoupling_ * (track - 1)`.
  - Rewrite the comments at :1499-1505 and :1695-1703. The buzz now follows the head's absolute amplitude relative to a full-velocity strike of the same configuration, because the wire force exists only under contact and so depends on amplitude (S. Bilbao, A. Torin, V. Chatziioannou, "Numerical modeling of collisions in musical instruments", Acta Acustica united with Acustica 101(1), 2015; A. Torin and M. Newton, NESS 2014).
  - Say what is not modelled: no contact threshold and no 1-3 ms cavity delay. No source gives values for a generic snare; Torin, Hamilton and Bilbao, DAFx-14, Sec. 5.2, report snares hitting the head about 9 ms after the strike for one simulated drum.
  - T4.14 later replaces both `getModalEnergy()` reads with the ripple-free accessor.
- **Unchanged.** `process()` keeps `wireGainMod_ = 1` (documented convention, :542-546). Add no velocity-to-cutoff map: no source gives its depth, and the probe check above decides whether one is needed. If the four noise-driven pads still invert after this fix, stop and report their ratios.
- `Wire coupling: coupling=1 does not attenuate the onset` (`test_wire_coupling.cpp:239`) renders at v = 1.0, where `norm` is about 1. It must stay green unchanged; if it goes red, stop and report.

**3. Build and gate**

No shared file changes. PRESET-IMPACT: no kit data changes, so no regeneration. Run the kit-quality probe and the velocity-centroid probe before and after the fix. In the table, list every pad whose `noiseLayerMix` is at least 0.3 or whose `noiseLayerGain` is above 1 (snares and hats in every kit), plus the four MB-178 pads.

**4. Acceptance**

- The MB-229 case keeps R(v) within ±2 dB of R(1.0) on pads 0, 2, 5 and 6. Baseline spreads: +2.5 / +3.0 / +5.7 dB, and the snare with wires +11.9 dB.
- The MB-257 case shows `ratioDb(0.2)` at least 3 dB below `ratioDb(1.0)`. At wire coupling 0.45 with head amplitude ∝ v, the expected value is 20 log10(1 + 0.45 · (0.2 - 1)) = -3.9 dB.
- The velocity-centroid probe gives a V0 ratio of at least 1.0 for 909 Drum Machine:38, Orchestral:38, Rock Big Room:38 and Vintage Wood:38 (baselines 1.038 / 1.002 / 0.888 / 0.860).
- The other MB-178 pads are recorded as refuted with their V2 ratios.
- Every existing `[wire-coupling]` and `[noise_layer]` case passes unchanged.
- Show the table and stop for the ruling. On the default snare, a v = 0.3 hit today carries 11.9 dB more wire relative to the head than a v = 1.0 hit. After the fix it is expected to carry about 3.3 dB less: a change of roughly 15 dB in how soft snare notes sound.

### T4.14 — Tension modulation is driven by instantaneous, normalised modal energy

**Findings:** MB-222 (medium, CONFIRMED), MB-272 (low, PLAUSIBLE), MB-223 (medium, PLAUSIBLE).

**Flags:** CROSS-PLUGIN IMPACT, PRESET-IMPACT, PHYSICS, VERIFY-FIRST.

**Prerequisites:** T1.11, T2.12.

**Estimate:** 240 minutes.

Context, at `4a14376d`. T1.11 and T2.12 will have moved these lines, so find each symbol with Grep.

- `energyAlpha_ = exp(-1 / (0.02 * sr))` sets a 20 ms one-pole follower (`plugins/membrum/src/dsp/drum_voice.h:130-133`). `energyEnv_` is zeroed at note-on (:422).
- `processBlockFast` computes `tensionPitchMod = min(1 + tensionAmtEffective_ * energyEnv_, kTensionPitchModMax)` at the START of each chunk (:722-728). It updates the follower from `getModalEnergy()` only at the END of the chunk (:902-920). `processBlockSlow` does the same (:945-951, :1069-1083).
- The pitch therefore starts at nominal and rises to a peak 30-49 ms after the strike: default kick +2.31 semitones at 42 ms, floor tom +0.80 at 41 ms. Measured toms do the opposite: they start at the tension-modulated frequency and glide down (MB-222).
- The depth `tensionAmtEffective_ = knob * kTensionEnergyScale` (3.3e-5, :94, :420-421) multiplies raw pre-normalisation modal energy. So equally loud hits glide 0 to 3 semitones depending on exciter and size: Mallet +1.68, Impulse +1.64, FMImpulse +0.25, Clap +0.02, NoiseBurst 0.00. Kick and floor tom barely change with velocity: kick +2.31 / +2.05 at v 1 / 0.5 (MB-223).
- `ModalResonatorBank::getModalEnergy()` (`dsp/include/krate/dsp/processors/modal_resonator_bank.h:482-499`) sums `s^2 + c^2`. For the magic-circle update (:959-960, `s' = R(s + eps c) + in`, `c' = R(c - eps s')`) that sum ripples by ±sin(pi f / fs) at 2f. The invariant is `s^2 + c^2 + eps s c`. One read per chunk is off by up to 31 % (48 modes, f0 2 kHz, 44.1 kHz; `specs/membrum-audit/probes/retune-probe-results.txt`, E6) (MB-272).

Decisions this task makes:

- **Driver.** Use the normalised instantaneous modal amplitude²: `E_norm = getModalAmplitudeSq() / strikeRefEnergy_`.
  - `strikeRefEnergy_` is the maximum of `getModalAmplitudeSq()` during the full-velocity strike probe. T4.13 introduced it with `getModalEnergy()`; this task switches the probe read to the new accessor. If T4.13 was not committed, add `strikeRefEnergy_` here exactly as T4.13's step 2 describes.
  - A full-velocity strike therefore reaches `E_norm ≈ 1` whatever the exciter, size or sample rate.
  - Velocity enters physically: with T4.12's law, modal amplitude ∝ v, so `E_norm ∝ v^2`.
- **Law.** `pitchMod = min(sqrt(1 + kTensionRatioAtFullStrike * knob * E_norm), kTensionPitchModMax)`.
  - Sources: the quasistatic tension rise is linear in the membrane energy (Marogna, Avanzini and Bank, "Energy based synthesis of tension modulation in membranes", DAFx-10, eq. 20), and an ideal membrane's mode frequency goes as sqrt(T) (f_mn = j_mn / (2 pi a) · sqrt(T / sigma); Fletcher and Rossing, *The Physics of Musical Instruments*, 2nd ed., ch. 18).
  - `kTensionRatioAtFullStrike = 0.1892f = 2^(1.5/6) - 1`. A full-knob, full-velocity strike glides +1.5 semitones at the energy peak: the existing design target (`drum_voice.h:86-93` "~1-2 semitones") pinned by `Phase 8E: Tom tension modulation drifts pitch vs velocity` (1.0-2.5 semitones). No published value exists for a drum knob, and the comment says so.
  - Keep `kTensionPitchModMax = 1.19f` (:100).
  - Size and tension do not scale the depth. MAB eq. 20 predicts more glide for looser or larger heads at equal energy, but Membrum's Size knob carries no rest tension or radius. Write this simplification in the comment.
- **No lag.** Read `getModalAmplitudeSq()` every 32 samples, the update interval of MAB DAFx-10 Sec. 4.2. Recompute `tensionPitchMod` from that read, with no follower.
  - If T1.11 already processes the bank in sub-chunks of at most 32 samples for retunes, use its cadence. Otherwise split each `processBlockFast` / `processBlockSlow` chunk into 32-sample sub-chunks while `tensionActive || wireActive`. Push the retune through the same converging path T1.11 established for the pitch envelope.
  - Delete `energyAlpha_`, `energyEnv_` and the follower blocks when nothing else reads them. The wire gain (T4.13) reads the same 32-sample value. `wireEnergyPeak_` stays only for the "not measured" fallback.
- **Fallback.** Feedback exciter and String body (`strikeRefEnergy_ == 0`): tension keeps the T2.12 driver and the wire keeps its running-peak fallback. Feedback runs the slow path, which `Phase 8E: tension glide also fires on the slow (Feedback) path` (`test_per_mode_damping.cpp:536`) must keep covering.
- **Seraphis untouched.** `getModalEnergy()` stays unchanged, because Seraphis reads it (`dsp/include/krate/dsp/systems/continuous_body.h:4133-4156`, `dsp/tests/unit/systems/continuous_body_test.cpp:5828`, :5980). Only its comment is corrected.

**1. Failing test**

- In `dsp/tests/unit/processors/test_modal_resonator_bank.cpp` (`dsp_processors_tests`), `TEST_CASE("ModalResonatorBank: modal amplitude^2 accessor is ripple-free on a ringing mode (MB-272)", "[processors][modal_bank][energy]")`.
  - Prepare the bank at 44.1 kHz with one mode at 2000 Hz and amplitude 1. Use a damping law with b1 = 0.01 s^-1 and b3 = 0, then strike it once.
  - Read the accessor every sample over one period and divide each value by exp(-2 · b1 · t).
  - Assert that (max - min) / mean < 0.1 %.
  - Write it first against `getModalEnergy()`. Its ripple is ±sin(pi · 2000 / 44100) = ±14.2 %, so it fails, which is the MB-272 verification. After the fix the case reads `getModalAmplitudeSq()`. A second `CHECK` pins `getModalEnergy()` to its corrected documented bound, a ripple of at most sin(pi f / fs) + 0.1 %.
- In `plugins/membrum/tests/unit/dsp/test_per_mode_damping.cpp`, `TEST_CASE("Phase 8E: tension glide starts high and falls (MB-222)", "[phase8e][drum_voice][audio]")`.
  - Use the existing tom setup (material 0.4, size 0.5, decay 0.5, strike 0.3, noise and click mix 0, `setTensionModAmt(1.0f)`) with the IMPULSE exciter. Its contact is 3.77 ms at v = 1 both before and after T4.12, so the energy peak sits inside 5 ms. Use v = 1.0 at 44.1 kHz.
  - Render in 32-sample blocks and record `getBodyBankForTest().getSharedBank().getModeFrequency(0)` after each block.
  - Assert:
    - the maximum falls within the first 5 ms;
    - after 5 ms the frequency never rises by more than 0.02 semitones between reads;
    - f(2 ms) > f(30 ms) > f(100 ms) > f(300 ms).
  - Render the same voice in 1024-sample blocks too. Read the mode frequency once per block and compare it with the 32-block curve at the same sample positions: within 5 cents.
  - Baseline: +0.08 semitones at 1.5 ms, +2.24 at 30 ms, peak at 42 ms. Fails.
- In the same file, `TEST_CASE("Phase 8E: equally loud strikes glide equally, and glide scales with strike energy (MB-223)", "[phase8e][drum_voice][audio]")`.
  - (a) Use the same tom at knob 1 and v = 1 with each of Impulse, Mallet, NoiseBurst, FMImpulse and Clap. The glide is the peak `getModeFrequency(0)` over 200 ms against the same voice at knob 0, in semitones. Assert each is within ±30 % of Mallet's.
  - Friction and Feedback are sustained drives with no single strike peak to normalise against. For them, only assert `0 < glide <= 12 · log2(1.19)`.
  - (b) For `DefaultKit::apply` pads 0, 5 and 11 (Kick, Low Floor Tom, Low-Mid Tom; Membrane, with their own `tensionModAmt`), assert `glide(v = 0.5) / glide(v = 1.0)` lies in [0.15, 0.40]. Physically it is v^2 = 0.25.
  - Baselines: (a) Mallet +1.68, Impulse +1.64, FMImpulse +0.25, Clap +0.02 and NoiseBurst 0.00 semitones; (b) kick 2.05 / 2.31 = 0.89, floor tom 0.76 / 0.80 = 0.95, tom pad 11 0.08 / 0.36 = 0.22. Fails, which is the MB-223 verification. A green run on current code refutes MB-223 alone: keep the test, keep the raw-energy depth, and still fix MB-222 and MB-272.

**2. Fix**

- `dsp/include/krate/dsp/processors/modal_resonator_bank.h`:
  - Add `[[nodiscard]] float getModalAmplitudeSq() const noexcept`, returning `Σ_k (s_k^2 + c_k^2 + epsilon_[k] · s_k · c_k) / (1 - epsilon_[k]^2 / 4)` over `numModes_`. Use the running `epsilon_` (:661), not `epsilonTarget_`.
  - Cite the magic-circle invariant in its comment: M. Vicanek, "A New Recursive Quadrature Oscillator", 2015, appendix "Magic Circle Oscillator", https://www.vicanek.de/articles/QuadOsc.pdf. The report could not render that PDF's text, so say so, and add that the invariant follows from the update at :959-960 by direct expansion.
  - Rewrite the `getModalEnergy()` comment (:482-492): it ripples at 2f by ±sin(pi f / fs). It is unchanged for existing consumers. MAB's energy is a mode-order-weighted sum (DAFx-10 eq. 19), which neither accessor is.
  - Run `grep -rn "getModalAmplitudeSq" dsp/ plugins/` first; it must find nothing.
- `plugins/membrum/src/dsp/drum_voice.h`:
  - Implement the driver, law, cadence and fallback above in `processBlockFast` and `processBlockSlow`.
  - Replace `kTensionEnergyScale` (:86-94) by `kTensionRatioAtFullStrike`, with its comment and sources.
  - `tensionAmtEffective_` becomes `knob · kTensionRatioAtFullStrike` (:408-421). Rewrite the comment there: the "already carries velocity ~ v^2" claim is now true by construction, through `E_norm`.
  - Read the probe's reference value with `getModalAmplitudeSq()` in `probeStrikePeakExcited`.
  - Switch the wire gain's per-read value to the same accessor.
  - Rewrite the stale justification at :703-717 and :937-944: there is no follower, the pitch starts at the tension-modulated value and relaxes with the energy, and cite Kirby and Sandler, DAFx-20, Sec. 3.3.
- Update the comment in `test_per_mode_damping.cpp:484-488` ("tau ~20 ms ... peak between 20 and 80 ms"); its assertions stay.

**3. Build and gate**

- CROSS-PLUGIN: run `grep -rl "modal_resonator_bank.h" plugins/ dsp/include tests/` and list every consumer in the commit message (Innexus, Seraphis' `continuous_body.h` and others). The new accessor is additive and `getModalEnergy()` is byte-identical, so no other plugin's output may change.
- Build targets: `dsp_processors_tests membrum_tests Membrum` plus every executable `node tools/header-consumers.js --tag '[membrum]'` names, gated with `~[long]`. clang-tidy also runs `-Target dsp`.
- The 32-sample retune cadence adds per-sample cost on tension-enabled pads. After the gate, run `node tools/run-cpu-tests.js membrum_tests` alone with nothing else running. Membrum's CPU budgets must hold; a budget is never relaxed.
- PRESET-IMPACT: no kit data changes, so no regeneration. Run the kit-quality probe before and after. The table lists every pad with `tensionModAmt > 0` on a Membrane body. The generator gives toms `tensionMod 0` because "it fights the descending boom-glide" (`tools/membrum_preset_generator.cpp:1129-1130`, :1298): say in the table that this workaround is now obsolete, but do not retune kits here (kit-data gate).

**4. Acceptance**

- The MB-272 case passes: the accessor ripples by less than 0.1 % (baseline 14.2 % for `getModalEnergy()`).
- The MB-222 case passes: peak within 5 ms (baseline 42 ms); after it, monotonic descent; block 32 against block 1024 within 5 cents.
- The MB-223 case passes: five exciters within ±30 % of Mallet; v 0.5 / v 1 glide ratio in [0.15, 0.40] on pads 0, 5 and 11.
- `Phase 8E: Tom tension modulation drifts pitch vs velocity` (1.0-2.5 semitones), the Feedback slow-path case and T2.12's sample-rate invariance case stay green.
- The Seraphis and Innexus suites are unchanged.
- The CPU lane is green.
- Show the table and stop for the ruling. Every tension-enabled membrane pad now glides down from the strike instead of swooping up over 30-50 ms.

### T4.15 — NonlinearCoupling changes timbre, not level

**Findings:** MB-234 (medium, CONFIRMED), MB-061 (low, CONFIRMED). Only MB-061's NonlinearCoupling half belongs here; its ModeInject half is T2.16's.

**Flags:** PRESET-IMPACT, PHYSICS.

**Prerequisites:** T2.16.

**Estimate:** 180 minutes.

Context:

- `NonlinearCoupling::processSample` (`plugins/membrum/src/dsp/unnatural/nonlinear_coupling.h:99-127`) computes `drive = 1 + 6 * velocity_ * amount_ * env` from an RMS follower. That follower's attack is `120 - 90 * velocity` ms and its release 200 ms (`setVelocity`, :85-92).
- It returns `x + amount_ * (recipSqrt(x * drive) - x)`, where `recipSqrt(x) = x / sqrt(x^2 + 1)` (`dsp/include/krate/dsp/core/sigmoid.h:193`).
- For small x that is `x * (1 + amount * (drive - 1))`, an in-phase gain above 1, so the stage is mostly a level EXPANDER. 93-96 % of the added signal is linear gain.
- Measured at amount 1 and v 1: +5.2 dB (0-50 ms) and +4.3 dB (50-300 ms) on a Plate, +6.0 dB on a 5-mode membrane. The centroid moves only +1.7 %.
- The parameter-audibility probe (`specs/membrum-audit/probes/param-audibility.csv`, offset 24 rows) gives rms500 -31.0 → -26.6 dB on note 36 (+4.4 dB, while the centroid FALLS 402 → 358 Hz), -35.6 → -31.0 dB on note 38 (+4.6 dB), and +1.3 dB on note 42.
- The stage is applied to body + noise + click + modeInject: `process()` `drum_voice.h:557-558`, `processBlockFast` :893-895, `processBlockSlow` :1056-1058.
- `MANUAL.md:349` promises "energy-limited cross-mode coupling" producing "FM-like sidebands". Neither exists.

Decision: keep the documented character-knob design (`nonlinear_coupling.h:6-15`: envelope-controlled waveshaping, not the von Kármán / Poirot energy-transfer model) and remove its level side effect with an attenuate-only RMS make-up. The energy-conserving coupled filter bank (Poirot, Bilbao and Kronland-Martinet, DAFx 2023, eq. 15/19) is a feature request: list it in the commit message as not done.

**1. Failing test**

In `plugins/membrum/tests/unit/unnatural/test_nonlinear_coupling.cpp`, `TEST_CASE("UnnaturalZone NonlinearCoupling -- changes timbre, not level (MB-234, MB-061)", "[UnnaturalZone][NonlinearCoupling][level]")`.

- Set up a `DrumVoice` at 48 kHz with the setup of `renderVoice` in `test_coupling_no_homogenization.cpp:49-70` (Impulse, material 0.6, size 0.5, decay 0.8, strike 0.33, level 0.2) and set the amount through `voice.unnaturalZone().nonlinearCoupling.setAmount(...)`. Render 300 ms with `processBlock` in 256-sample blocks at v = 1.0.
- (a) Plate body, amount 0 against amount 1:
  - Total energy over 0-300 ms within ±0.5 dB.
  - The fraction of energy above 4 kHz in the 30-200 ms window at least 3 dB higher at amount 1. Compute band energies from power spectra (`sumBinPower`, `tests/test_helpers/spectral_analysis.h`, or `Krate::DSP::FFT`).
  - The 5 ms frame at which the amount-1 render's HF fraction peaks lies later than 10 ms (delayed bloom).
- (b) Membrane body, amount 0 against amount 1: `|20 log10(peak1 / peak0)| <= 1.5 dB` and energy over 0-300 ms within ±0.5 dB.
- Baseline: +5.2 / +4.3 dB on the Plate and +6.0 dB on the membrane (MB-234). Fails (a) and (b) on energy.

**2. Fix**

- In `NonlinearCoupling`, add two one-pole mean-square followers, `msIn_` and `msOut_`. Their coefficient `msAlpha_ = std::exp(-1.0f / (0.010f * sr))` (10 ms) is set in `prepare()`, and both are zeroed wherever `envFollower_` is reset (`reset()` :71-74, called from `drum_voice.h:334` and :479).
- In `processSample`, after `y = bodyOutput + amount_ * (shaped - bodyOutput)`:
  - `msIn_ = msAlpha_ * msIn_ + (1 - msAlpha_) * bodyOutput^2;`
  - `msOut_ = msAlpha_ * msOut_ + (1 - msAlpha_) * y^2;`
  - `const float g = (msOut_ > msIn_ && msOut_ > 1e-12f) ? std::sqrt(msIn_ / msOut_) : 1.0f;`
  - `return y * g;`
  - The gain only ever attenuates (g ≤ 1), so the stage can no longer add level. The ratio stays positive, so `sqrt` needs no clamp.
- The `amount_ == 0.0f` early return stays first, so the bypass stays bit-exact (FR-055).
- Write in the comment that the 10 ms constant is a design choice (shorter than the drive follower's 30-120 ms attack, so the make-up tracks the drive). No published value exists.
- If case (a)'s HF assertion still fails after the make-up, the knob is level-neutral but too weak. Then raise `kDriveScale` (:117), an uncalibrated character constant, to the smallest value that meets +3 dB while `NonlinearCoupling guard: engaging coupling never amplifies the voice peak`, `... velocity dynamics survive full coupling` and `... distinct bodies stay distinct at full coupling` (`test_coupling_no_homogenization.cpp:127`, :156, :169) still pass. If no value satisfies all four, stop and report the measured HF gain.
- Rewrite the header (:17-38) to describe the make-up and what the stage now guarantees: level within ±0.5 dB, timbre via odd harmonics, peaks still owned downstream.
- `plugins/membrum/MANUAL.md:349`: replace the row text with "Amplitude-dependent odd-harmonic brightening that builds over the first tens of milliseconds; changes timbre, not level".

**3. Build and gate**

- Run `grep -rl "MANUAL.md" plugins/membrum/tests tools/site` (tests that read non-C++ files are consumers; at `4a14376d`: `tools/site/tests/gen-site.test.js` via `tools/gen-site.js`) and run those suites, e.g. `node --test tools/site/tests/`.
- PRESET-IMPACT: no kit data changes, so no regeneration. Run the kit-quality and parameter-audibility probes before and after.
- The table lists every pad with `nonlinearCoupling > 0`: about 30 generator sites. Examples: `tools/membrum_preset_generator.cpp:574` (0.22), :778 (0.35), :1500 (0.45), and the "Chaos" cycle at :1534 (0.75-0.85). Also the default-kit crash, `default_kit.h:234` (0.35).

**4. Acceptance**

- The new case passes: Plate and Membrane energy within ±0.5 dB (baseline +4.3 to +6.0 dB), Plate HF fraction +3 dB or more and peaking after 10 ms.
- All existing `[NonlinearCoupling]` cases pass unchanged, including `amount==0 exact bypass`, `small amount is near-bypass in steady state (M-3)` and `amount drives sustained brightening (M-4)`.
- The parameter-audibility rows for offset 24 show `maxLevelChangeDb` of at most 1.0 on notes 36, 38 and 42 (baseline 4.42 / 4.62 / 1.60).
- Show the table and stop for the ruling. Pads with coupling get quieter by the amount the expander used to add (about +0.3 dB at 0.2, +0.9 dB at 0.35, +3 dB at 0.8, from MB-061's verification).

### T4.16 — Membrane stiffness uses the membrane's own wavenumber, not the stiff-string law on a sorted mode index

**Findings:** MB-201 (low, CONFIRMED).

**Flags:** PRESET-IMPACT, PHYSICS.

**Prerequisites:** T4.5.

**Estimate:** 150 minutes.

Context:

- `MembraneMapper::map` (`plugins/membrum/src/dsp/bodies/membrane_mapper.h:88-179`) sets `r.stretch = clamp(material * 0.3 + stretchDelta, 0, 1)` (:129-138), so the stretch is on by default through Material.
- The bank turns that into `B = stretch^2 * 0.01` and `f_w = f_k * sqrt(1 + B * (k + 1)^2)` with k the index into the 48 SORTED Bessel modes (`dsp/include/krate/dsp/processors/modal_resonator_bank.h:783`, :806-807).
- A 2-D membrane's sorted index grows as j_mn^2, so the warp behaves like sqrt(1 + B' j^4). A stiff membrane obeys ω^2 = c^2 k^2 + κ^2 k^4, so f_mn = f_ideal · sqrt(1 + β j_mn^2) (S. Bilbao, *Numerical Sound Synthesis*, Wiley 2009, ch. 11-12).
- Measured multipliers on mode 47: ×1.089 at material 0.3 (default kick), ×1.232 at 0.5 (default snare), ×1.753 at 1.0.
- The stretch reaches the bank at note-on (`bodies/membrane_body.h:36-42`), on retune (`drum_voice.h:1619-1625`) and on the Material Morph refresh (:1656-1664), all through `cachedMapperResult_.stretch`.

**1. Failing test**

In `plugins/membrum/tests/unit/bodies/test_membrane_body.cpp`, `TEST_CASE("MembraneMapper: stiffness follows the membrane wavenumber law (MB-201)", "[membrum][body][membrane][BodyModes]")`.

- Start from `makeDefaultParams()` (`body_test_helpers.h:167`) with airLoading 0, modeScatter 0, decaySkew 0 and modeStretch neutral (1.0, normalised 1/3, or whatever T4.5 made neutral). Call `Membrum::Bodies::MembraneMapper::map(params, 0.0f)` for material ∈ {0.0, 0.5, 1.0}.
- Assert `r.stretch == 0.0f` exactly.
- Assert `r.frequencies[0]` equals `500 * 0.1^size` within 1e-6 relative: stiffness keeps the Size-derived (0,1) pitch. Never compare bit-for-bit across separately compiled code (the macOS fast-math rule).
- For k ∈ {1, 9, 23, 46}, assert `r.frequencies[k] / (r.frequencies[0] * ratio_k)` equals `sqrt(1 + kMembraneStiffnessMax * material^2 * (ratio_k^2 - 1))` within 0.1 %.
  - Type `ratio_k = j_k / 2.4048` from the Bessel zeros in Abramowitz and Stegun, Table 9.5: j_01 2.4048 (index 0), j_11 3.8317 (index 1), j_51 8.7715 (index 9), j_43 14.3725 (index 23), j_16 19.6159 (index 46). Check the index-to-(m, n) mapping against `kMembraneBesselOrder` / `kMembraneBesselRadial` (`membrane_modes.h:40-60`) before typing.
  - Type `kMembraneStiffnessMax` as 0.0050 in the test.
- Baseline: `r.stretch` is 0.15 at material 0.5, and the mapper frequencies carry no stiffness: the bank adds ×1.232 on mode 47 instead. Fails.

**2. Fix**

- In `MembraneMapper::map`, multiply step (1)'s frequency (:102-109) by `std::sqrt(1.0f + Bm * (ratio * ratio - 1.0f))` with `ratio = kMembraneRatios[k]` and `Bm = kMembraneStiffnessMax * material^2 * stiffScale`.
  - This is the stiff-membrane dispersion normalised to (0,1): f = f_ideal · sqrt((1 + β r^2) / (1 + β)), with Bm = β / (1 + β).
  - The material^2 shape keeps today's B ∝ material^2.
  - `constexpr float kMembraneStiffnessMax = 0.0050f;` goes in `membrane_modes.h`. It is sized so material 1 raises mode 9 ((5,1)) by +3 %: sqrt(1 + 0.005 · 12.30) = 1.030. That makes it a fine-tuning effect, as T. D. Rossing, "Acoustics of drums", Physics Today 45(3), 1992 describes it. The report found no published numeric value for a drumhead's bending stiffness, and the comment says so.
- Set `r.stretch = std::clamp(std::max(stretchDelta, 0.0f), 0.0f, 1.0f)`. Mode Stretch above neutral stays an explicit unnatural extra through the bank's stiff-string law.
  - Below neutral, the knob used to act only by cancelling the material term. Keep its travel by `stiffScale = std::clamp(1.0f + std::min(stretchDelta, 0.0f) / kUnityStretchNorm, 0.0f, 1.0f)`, which lowers the membrane stiffness to 0 at the knob minimum.
  - If T4.5 changed `stretchDelta` or `kUnityStretchNorm`, apply the same split to what T4.5 left. Touch nothing else in that block.
- Rewrite the comments at :123-138, and at `membrane_modes.h` beside the new constant, with the law and both sources.
- Resulting top-mode (47) multipliers: about ×1.015 / ×1.040 / ×1.152 at material 0.3 / 0.5 / 1.0. Today: ×1.089 / ×1.232 / ×1.753.

**3. Build and gate**

- The Phase-1 golden `phase1_default.bin` moves if the default patch is a Membrane with material above 0. Regenerate it deliberately (`membrum_tests.exe "Generate Phase 1 golden reference"`) and justify it in the commit message.
- `MembraneBody: modal ratios within 2% of kMembraneRatios (SC-002)` and the US4-3 strike-position sweep must stay green unchanged.
- PRESET-IMPACT: no kit data changes, so no regeneration. Run the kit-quality probe and the `modes:body=Membrane` rows of `audit_probes` before and after. Baseline: size 0.5 measured partials 0.2 / 0.7 / 1.7 / 3.0 / 4.8 / 6.9 / 9.4 / 12.2 cents above the table for k0..k7.

**4. Acceptance**

- The new case passes.
- `r.stretch` is 0 at neutral Mode Stretch for every material (baseline 0.3 · material).
- The mode-46 multiplier at material 0.5 is 1.040 ± 0.1 % (baseline 1.224 for index 46 and 1.232 for index 47, from sqrt(1 + 2.25e-4 · n^2) with n = 47 and 48).
- The `modes:body=Membrane` probe shows (0,1) within 1 cent of the table at every size, and k1..k7 within 15 cents.
- Show the table and stop for the ruling. Upper membrane modes on every Membrane pad move down by up to 19 % (snare, mode 47).

### T4.17 — Mode Scatter is documented as an inharmonicity dither (or gains a real doublet model)

**Findings:** MB-200 (low, CONFIRMED), MB-246 (low, CONFIRMED).

**Flags:** PHYSICS.

**Prerequisites:** none.

**Estimate:** 60 minutes.

Context:

- Scatter is `f_w *= 1 + C * sin(k * kScatterD)`, with `C = scatter * 0.10` and `kScatterD = pi * (phi - 1)` (`dsp/include/krate/dsp/processors/modal_resonator_bank.h:655-656`, :784, :810).
- k = 0, which is the membrane's (0,1), never moves (sin 0 = 0). No mode is ever added, so no doublet exists.
- The default snare's scatter 0.28 moves (1,1) +2.6 % and (2,1) -1.9 %. Its first four ratios come out 1 : 1.636 : 2.22 : 2.27. The measured batter head is 1 : 1.25 : 1.80 : 1.99 (Rossing et al. 1992, via Skrodzka, Hojan and Proksza, Archives of Acoustics 31(3), 2006, Table 1; Skrodzka's own head 1 : 1.44 : 2.24 : 2.32). A real snare's mode pairs come from head-to-head air coupling: (0,1) at 182 / 330 Hz and (1,1) at 278 / 341 Hz (Fletcher and Rossing, *The Physics of Musical Instruments*, 2nd ed., Table 18.6).
- Real asymmetry splits each m ≥ 1 mode into a closely spaced doublet that beats (J. Woodhouse, Euphonics, "3.6 Tuned drums").

Per the PHYSICS flag, this task corrects comments only and changes no rendered sample, unless the user rules for option A below.

**1. Failing test**

This task changes no behaviour, so step 1 is a pin that passes before and after; there is no red step.

In `plugins/membrum/tests/unit/bodies/test_membrane_body.cpp`, `TEST_CASE("MembraneBody: Mode Scatter detunes single partials and never moves (0,1) (MB-200, MB-246)", "[membrum][body][membrane][BodyModes]")`:

- Configure a Membrane `DrumVoice` at 44.1 kHz (size 0.5, material 0, airLoading 0) at scatter 0 and at scatter 1. Read `getBodyBankForTest().getSharedBank().getModeFrequency(k)` and the active-mode count after `noteOn(1.0f)`.
- Assert:
  - `getModeFrequency(0)` is identical at both settings.
  - The active-mode count is identical.
  - `getModeFrequency(1)` at scatter 1 / at scatter 0 = 1 + 0.10 · sin(1.9416) = 1.0932 ± 0.1 %. The constants are typed from the code's documented law: C = scatter · 0.10, kScatterD = π(φ - 1) = 1.9416.
- Material 0 matters: T4.16 then leaves stiffness off, and before T4.16 it leaves the material stretch off.

**2. Fix**

Comments and documents only:

- `plugins/membrum/src/dsp/default_kit.h:132-133`: replace "scatter 0.28 reproduces the (0,1)/(1,1) mode splitting of a real snare head". New text: scatter detunes (1,1) +2.6 % and (2,1) -1.9 % as an inharmonicity colour. It splits no mode. A real snare's pairs come from head-to-head air coupling (Fletcher and Rossing Table 18.6), which Membrum does not model.
- Read `default_kit.h:74-77` and `:163-165` and change only a claim of physical splitting or imperfection, if present.
- `plugins/membrum/src/dsp/pad_config.h:121-125`: replace "natural imperfection". New text: an artistic inharmonicity dither of up to ±10 % per mode that leaves mode 0 fixed. Real asymmetry splits m ≥ 1 modes into doublets (Woodhouse, Euphonics 3.6), which this does not model.
- `pad_config.h:285-287`: replace "15 % sinusoidal dither" with 10 %.
- `plugins/membrum/src/dsp/voice_common_params.h:44-47`: replace "[0, 1] -> [0, 0.15]" with "[0, 1] → per-mode dither of up to ±10 % (C = scatter · 0.10 in ModalResonatorBank)".
- `plugins/membrum/MANUAL.md:243`: replace "natural imperfection" with "a deterministic detune of up to ±10 % per mode that leaves the lowest mode fixed: an inharmonicity colour, not a model of head imperfection".
- `plugins/membrum/preset-retune/archetypes/acoustic-snare-wire-buzz.md:20` and `plugins/membrum/preset-retune/recipes.json:3216`: the same false "reproduces the (0,1)/(1,1) mode splitting" claim. Correct the wording. No code reads `recipes.json`: a grep for it found only these working papers.
- Do not touch `modal_resonator_bank.h`: its scatter comment (:770-774) makes no physical claim.

Ask the user before the commit. Default: B.

- **A (model):** for each m ≥ 1 membrane mode, add a second bank entry at `f · (1 + δ_k)`, with `δ_k = scatter · 0.02 · d_k` from a fixed per-mode table and excitation weight `0.3 · scatter`, so each pair beats. The bank has room: `kMaxModes = 96` (:73), and 48 + 42 = 90 entries. MB-246's closed-drum ratio target belongs to the same question. Option A is PRESET-IMPACT and comes back as its own task.
- **B (this task):** comments only.

**3. Build and gate**

- The comment edits change no code path. `MANUAL.md` is read by `tools/gen-site.js`, so run the site tests (`node --test tools/site/tests/`).
- pluginval is skipped: these are docs and test changes.

**4. Acceptance**

- The pin case passes.
- `git diff --stat` shows only comments, Markdown, the `recipes.json` string and the test.
- `getState` of all 20 factory kits and the `phase1_default.bin` golden are unchanged.
- The user's A/B answer is recorded in the commit message.

### T4.18 — Membrane strike-position weights: Bessel J evaluated to convergence, with modal normalisation

**Findings:** MB-011 (low, CONFIRMED), MB-037 (low, CONFIRMED), MB-197 (low, PLAUSIBLE).

**Flags:** PRESET-IMPACT, PHYSICS, VERIFY-FIRST.

**Prerequisites:** none.

**Estimate:** 180 minutes.

Context:

- `evaluateBesselJ` (`plugins/membrum/src/dsp/membrane_modes.h:130-160`) sums the J_m power series in float with `kMaxTerms = 20` (:152-157).
- `MembraneMapper::map` calls it at `x = j_mn * 0.9 * strikePos` (`bodies/membrane_mapper.h:111-118`), up to 17.65 at strikePos 1.0. There the series terms reach about 8e5, and the 21-term truncation error is of order 1, so a double-precision 21-term sum is just as wrong.
- Measured against converged references (a 120-term double series and a 20000-point quadrature):
  - worst |error| is 0.0019 at strikePos 0.9, 0.019 at 0.93, 0.045 at 0.95 and 0.357 at 1.0;
  - mode 46 (m = 1) gives 0.533 against the true 0.176, so the wrong mode becomes the loudest by +7.1 dB.
- The header claim "accuracy under 1 %" (:123-127) is false at the top of the range. Nothing clamps strikePos below 1 (`drum_voice.h:1119-1124`).
- Separately, the weight is the raw mode shape `|J_m(j_mn r0/a)|`, with no modal-norm factor (MB-197). Projecting a point strike onto the orthogonal modes divides by N_mn = π a^2 ε_m J_{m+1}(j_mn)^2, with ε_0 = 1 and ε_m = 1/2 for m ≥ 1. The surface-rms weight is therefore ψ(r0) / sqrt(N_mn), which the code under-weights by 5.2 dB for (1,1), 6.7 dB for (2,1) and 3.7 dB for (0,2) (S. Bilbao, "Modal Synthesis", CCRMA 2006; Fletcher and Rossing, 2nd ed.).
- One verifier refuted MB-197 as a documented choice of observable (spec 135:616-632 states only a proportionality). That is why it is VERIFY-FIRST and lands as a separate commit after the user's ruling.

**1. Failing test**

In `plugins/membrum/tests/unit/bodies/test_membrane_body.cpp`:

- `TEST_CASE("Membrane modes: evaluateBesselJ converges over the whole strike range (MB-011, MB-037)", "[membrum][body][membrane][BodyModes]")`
  - (i) Typed anchors from Abramowitz and Stegun, Table 9.1, within 1e-5: J0(1) = 0.7651976866, J1(1) = 0.4400505857, J0(5) = -0.1775967713, J1(5) = -0.3275791376, J0(10) = -0.2459357645, J1(10) = 0.0434727462.
  - (ii) For all 48 k, `|evaluateBesselJ(m_k, j_k)| < 1e-4`; the zero table itself checks out.
  - (iii) For every k and strikePos ∈ {0.3, 0.75, 0.9, 0.95, 0.99, 1.0}, compare `evaluateBesselJ(m_k, j_k * 0.9f * s)` with an independent reference within 1e-3 absolute. The reference is computed in the test from the integral representation J_m(x) = (1/π) ∫_0^π cos(mτ - x sin τ) dτ (A&S 9.1.21) with a 20000-point midpoint rule in double.
  - (iv) Mode 46 (m = 1) at strikePos 1.0 gives `|J| = 0.176 ± 0.002`.
  - Baseline: (iii) fails at 0.95 (0.045), 0.99 (0.129) and 1.0 (0.357), and (iv) gives 0.533. Fails.
- `TEST_CASE("MembraneMapper: strike weights carry the modal norm (MB-197)", "[membrum][body][membrane][BodyModes]")`
  - Use `makeDefaultParams()` with strikePos 0.3, airLoading 0 and decaySkew 0, then call `MembraneMapper::map`.
  - For k = 1, 2, 3 ((1,1), (2,1), (0,2)), assert `(amp_k / |J_m(x_k)|) / (amp_0 / |J_0(x_0)|)` equals 1.8227 / 2.161 / 1.5255 within 1 %. The radial parts come from the now-converged `evaluateBesselJ`.
    - The factors are typed from A&S values at the zeros: J1(2.4048) = 0.5191, J2(3.8317) = 0.4028, J3(5.1356) = 0.3397, |J1(5.5201)| = 0.3403.
    - Each is sqrt(J1(j01)^2 / (ε_m J_{m+1}(j_mn)^2)), e.g. sqrt(0.2695 / (0.4028^2 / 2)) = 1.8227.
  - Also assert that at strikePos 0 every m ≥ 1 amplitude is below 1e-4 (a centre strike excites only axisymmetric modes).
  - Baseline: every factor is 1.000 (the code's (1,1)/(0,1) at strikePos 0.3 is 0.451 / 0.897). Fails, which is the MB-197 verification. A green run refutes MB-197 alone: keep the test and skip commit B.

**2. Fix**

Two commits, in this order.

- **Commit A (MB-011, MB-037).** Rewrite `evaluateBesselJ`:
  - Accumulate in `double`.
  - Loop `for (int k = 1; k <= kMaxTerms; ++k)` with `kMaxTerms = 80` as a hard bound (real-time safe). Break once `k > x / 2` and `|term| < 1e-12 * std::max(std::abs(result), 1e-3)`. Return `static_cast<float>(result)`.
  - The worst case, x ≤ 17.66, converges by k ≈ 40. The mapper runs at note-on and on the Material Morph refresh, about 48 × 40 multiply-adds per call.
  - Replace the header comment (:117-128) with the convergence criterion, the measured worst error from test (iii), and the A&S reference (eq. 9.1.10 for the series, 9.1.21 for the integral the test uses).
- **Commit B (MB-197, after the ruling).**
  - Add `constexpr std::array<float, 48> kMembraneModalNormInv` next to `kMembraneBesselZeros` (`membrane_modes.h:108-115`), with `value_k = sqrt(J1(j_01)^2 / (ε_m J_{m+1}(j_k)^2))` (1.0000, 1.8227, 2.161, 1.5255, ...).
  - Generate the 48 values once with a throwaway Node script in the scratchpad, never committed, using the integral representation in double. Paste them with 4 significant digits, and describe the method and source in the comment.
  - In `MembraneMapper::map` step (2), multiply: `r.amplitudes[k] = std::abs(evaluateBesselJ(m, jmn * r_over_a)) * kMembraneModalNormInv[k];`.
  - State the observable in the comment: surface-rms modal weight ψ(r0) / sqrt(N_mn), with no observation point and no radiation weighting.
  - The strike normalisation (`measuredStrikeOutputGain`, keyed on strikePos) re-levels the peak, so only the balance between modes changes.
  - If `MembraneBody: Strike Position sweep changes first-5 mode weights (US4-3 Bessel J_m)` (`test_membrane_body.cpp:244`) goes red, it pins the unnormalised model. Update its expectation to the normalised weights only after the ruling, with the same assertion strength.

**3. Build and gate**

- Commit A: run `grep -n "strikePosition" tools/membrum_preset_generator.cpp` and `plugins/membrum/src/dsp/default_kit.h`, and list every Membrane pad with strikePosition above 0.9. Only those can sound different, since the error is at most 0.0019 below 0.9. Run the kit-quality probe before and after for them. Also show the parameter-audibility rows for offset 5 (`strikePosition`, notes 36 / 38 / 42); baseline rms500 per step -28.8 ... -37.4 dB on note 36.
- Commit B changes every Membrane pad's mode balance: m ≥ 1 modes rise 3.7 to 12 dB against (0,1). Run the full kit-quality probe before and after. Regenerate `phase1_default.bin` deliberately if it moves, and justify it.
- No kit data changes in either commit, so no regeneration.

**4. Acceptance**

- Commit A:
  - The convergence case passes: worst error under 1e-3 at every strikePos (baseline 0.357 at 1.0).
  - Mode 46 at strikePos 1.0 is 0.176 (baseline 0.533), and the loudest mode at strikePos 1.0 is no longer mode 46.
  - Every pad with strikePosition ≤ 0.9 is unchanged in the kit probe within its run-to-run noise.
- Commit B:
  - The norm case passes: factors 1.8227 / 2.161 / 1.5255 within 1 % (baseline 1.000).
  - Show the before/after table and ask: "Membrane pads now weight the (m,1) modes 4-12 dB higher against (0,1), as a struck ideal membrane does. Adopt it?" Stop until the user rules.
  - If the user declines, close MB-197 as "ruled out by the user". Do not commit its test (it would stay red, and an assertion is never weakened). Record its measured factors in the checklist instead.
- Commit A stages only the convergence case and `membrane_modes.h`. The MB-197 case stays uncommitted until commit B.

### T4.19 — Secondary shell bank: damping from the body's realised law, Material means the same on head and shell, sourced fundamental rule and citations

**Findings:** MB-256 (low, CONFIRMED), MB-113 (low, CONFIRMED), MB-207 (low, PLAUSIBLE), MB-253 (low, PLAUSIBLE)

**Flags:** PRESET-IMPACT, PHYSICS, VERIFY-FIRST (MB-207 and MB-253 only; MB-256 and MB-113 are CONFIRMED and are fixed whatever the verification shows)

**Prerequisites:** T1.23 (it replaces the hidden `bodyDampingB1 = -1` "derive" sentinel; this task must read the body's damping from the law the mapper actually produced, whatever encoding T1.23 chose)

**Estimate:** 180 min

Code anchors (read at `4a14376d`, find each by its symbol): `DrumVoice::configureSecondaryBank()` in `plugins/membrum/src/dsp/drum_voice.h:1418-1484`, with the fundamental rule at `:1421-1426` (comment "Chromaphone's 'shell f0 ~0.6 f_head' rule of thumb" at `:1423`), the damping-law comment citing "Chaigne (1993)" at `:1450-1452`, the stale "[25, 80] s^-1" range at `:1467-1468`, the duplicated body-b1 formula with its 0.4 fallback at `:1471-1474` (a copy of `dampingLawFromParams`, `plugins/membrum/src/dsp/bodies/membrane_mapper.h:65-80`), and `b3 = mat * 1.0e-5f` at `:1480`. `cachedMapperResult_` (the law the bank received; `MapperResult::damping`, `membrane_mapper.h:35`) is assigned at `drum_voice.h:220-221`, before `configureSecondaryBank()` is called at `:396`, so it is available there. The accessor pattern to copy is `getBodyBankForTest()` at `drum_voice.h:1144`; `ModalResonatorBank::getDampingLaw()` exists at `dsp/include/krate/dsp/processors/modal_resonator_bank.h:539`. Comments to correct: `plugins/membrum/src/dsp/pad_config.h:111-116` (b3 cited to "Chaigne & Askenfelt 1993") and `:300-302` (the 0.6x mapping and "same semantic as primary").

1. **Failing test.** Extend `plugins/membrum/tests/unit/voice_pool/test_secondary_shell_paths.cpp` (already in the `membrum_tests` source list). Add a test-only accessor `const Krate::DSP::ModalResonatorBank& getSecondaryBankForTest() const noexcept { return secondaryBank_; }` next to `getBodyBankForTest()` (it is part of this task's diff). Every case below drives a `Membrum::DrumVoice` directly, as `tests/unit/dsp/test_per_mode_damping.cpp:489-509` does: `prepare(48000.0, 0u)`, `setBodyModel(BodyModelType::Membrane)`, `setExciterType(ExciterType::Impulse)`, `setNoiseLayerMix(0.0f)`, `setClickLayerMix(0.0f)`, the body damping left in its derived ("Auto" after T1.23) state, `setSecondaryEnabled(1.0f)`, `setCouplingStrength(0.5f)`, then `noteOn(1.0f)` and read `getSecondaryBankForTest().getDampingLaw()` and `getBodyBankForTest().getSharedBank().getDampingLaw()`.
   - `TEST_CASE("Secondary shell b1 follows the body's realised damping law (MB-256, MB-113)", "[membrum][voice_pool][coupling][secondary][MB256][MB113]")`. For material 0.5 / decay 0.3 / size 0.5 and secondaryMaterial 0, 0.5 and 1: `shell.b1 == Approx(std::max(25.0f, 2.5f * body.b1) + (1.0f - mat) * 15.0f).epsilon(1e-4)`. A second SECTION, the corner MB-113 measured (material 0, decay 0, size 0.5, secondaryMaterial 1): `shell.b1 >= 2.5f * body.b1`, i.e. shell T60 (6.908 / b1) at most 0.4 x the body's. Today both fail: the shell assumes a body b1 of 20.12 s^-1, while the body's real b1 is 1/decayTime (about 3.4 s^-1 at decay 0.3; about 21 s^-1 in the corner, where 2.5 x 21 = 53 > the shell's 50.3).
   - `TEST_CASE("Secondary shell b3 falls as Shell Material rises, like the head's (MB-256)", "[membrum][voice_pool][coupling][secondary][MB256][physics]")`. Assert `law(mat = 1).b3 < law(mat = 0).b3` and `law(mat).b3 == Approx((1 - mat) * DrumVoice::kShellMaxB3)` for mat 0, 0.4, 1. Add a SECTION that types the measured Rayleigh coefficients for steel and wood from the Sterling, Rewkowski, Klatzky and Lin (IEEE TVCG 2019) entry in report section 5 and asserts that the coded shell law orders them the same way (metal: lower b1 AND lower b3 than wood). The finding JSON quotes two different coefficient pairs from that paper; use the pair report section 5 prints, and if the report does not settle it, open the paper (Table 1) before typing the numbers. Today the b3 assertion fails (b3 = mat x 1e-5 rises).
   - `TEST_CASE("Secondary shell places a mode near Rossing's measured shell/head ratio at the default Shell Size (MB-207)", "[membrum][voice_pool][coupling][secondary][MB207][physics]")`. Size 0.4 (head f0 about 200 Hz, `natural_fundamental.h:38`), secondarySize 0.5: some shell mode frequency (`getSecondaryBankForTest().getModeFrequency(k)`, k < `getNumModes()`) lies within 10 % of 2.268 x the head f0 (`getBodyBankForTest().getSharedBank().getModeFrequency(0)` with airLoading 0). 2.268 = 415 Hz / 183 Hz, typed from Rossing, "Acoustics of drums", Physics Today 45(3) 1992: bare-shell (4,0) 415 Hz against the complete snare's (0,1) 183 Hz. Today it fails: the modes sit at 0.625 x {1, 2.757, 5.404, ...} = 0.625, 1.72, 3.38 x head. A second SECTION asserts that the anchored mode moves down monotonically as secondarySize goes 0 -> 0.5 -> 1.
   - MB-253 is a citation defect and has no executable test. Its verification is report section 5: the Chaigne and Askenfelt paper was opened (JASA 95(2), February 1994, piano strings). Record "verified by source" in the commit message.
   - Run each case by name and confirm it fails for the reason given. If the MB-207 case passes, MB-207's rule change is refuted: keep the test and change only its comment (step 2c).
2. **Fix.**
   a. *Damping (MB-256, MB-113).* In `configureSecondaryBank()`, replace `bodyN` / `bodyB1` (`:1471-1474`) with `const float bodyB1 = cachedMapperResult_.damping.b1;` (the law the primary bank received this note; for String the voice keeps the Membrane mapper result, `mapModalBody` at `drum_voice.h:1548-1565`, which is a defined value). Keep `b1 = std::max(25.0f, 2.5f * bodyB1) + (1.0f - mat) * 15.0f`. Replace `b3 = mat * 1.0e-5f` with `b3 = (1.0f - mat) * kShellMaxB3`, adding `static constexpr float kShellMaxB3 = 1.0e-5f;` next to `kBodyHeadroom`. Its comment says: the magnitude keeps today's maximum and is a design value with no published source; the direction (metal has a lower f^2 coefficient than wood) is from Sterling et al. 2019 (the table you used in the test) and Fletcher and Rossing, The Physics of Musical Instruments, 2nd ed., sec. 2.13. Correct the range comment at `:1467-1468`: b1 now lies in [25, 140] s^-1 (2.5 x 50 + 15), T60 0.05 to 0.28 s. Before: a fake body b1 of 20.1 s^-1 on derived pads and HF damping that rose toward "metal". After: the 2.5x ratio uses the real body law, and Shell Material brightens the shell as Material brightens the head.
   b. *Fundamental rule (MB-207, only if its test was red).* Replace `sizeRatio` (`:1424-1426`) with `f0 = std::max(naturalFundamentalHz_, 20.0f) * (kRossingShellHeadRatio / Bodies::kShellRatios[1]) * std::exp2(1.0f - 2.0f * std::clamp(secondarySize_, 0.0f, 1.0f))`, with `static constexpr float kRossingShellHeadRatio = 2.268f;` and a comment citing Rossing 1992 as typed in the test. If report section 5 says the Rossing PDF could not be opened, the comment says so instead of citing it as read. Result: secondarySize 0.5 puts beam mode 1 at 2.27x the head, secondarySize 0 puts it at 4.5x (near Rossing's mounted-drum (4,0) at 4.8x), secondarySize 1 puts mode 0 at 0.41x (near the ring-theory (2,0) estimate of 0.42x quoted in MB-207). Before: [0.25, 1.0] x head, never above the head. Correct `pad_config.h:300-301` and the "Secondary Size" row at `plugins/membrum/MANUAL.md:258`, and grep `plugins/membrum/preset-retune/00-param-dictionary.md` for the old 0.6x mapping (T5.6 tunes kits from that dictionary).
   c. *Citations (MB-253, MB-207).* Rewrite `drum_voice.h:1421-1423` to drop the "Chromaphone rule of thumb" (the AAS Chromaphone 3 manual states no such ratio; report section 5). Rewrite `drum_voice.h:1450-1452` and `pad_config.h:112-114` to cite "Chaigne & Askenfelt, JASA 95(2), 1994 (piano strings); Rayleigh-type b1 + b3 f^2 damping, the standard modal-synthesis convention", and to state that for solid shells and plates internal friction gives a loss rate linear in f (Fletcher and Rossing, Eq. 2.42), which this law does not model. Do not add a linear term to `ModalResonatorBank::DampingLaw`: that is a shared header and this task carries no CROSS-PLUGIN flag.
   d. Report, do not fix: `pad_config.h:297-298` still describes the removed 0.25 coupling ceiling and `MANUAL.md:256` says "up to 0.25 effective"; name them in the commit message as unrelated stale text.
3. **Build and gate.** Standard. No kit is regenerated: this is an engine change, and no kit data changes. `tests/golden/phase1_default.bin` must not move (no default-kit pad enables the shell: `pad_config.h:303-304` defaults, `default_kit.h:175-177` sets only size and material); if it moves, stop and find out why. `test_kit_switch_infinite_ring.cpp` (the shell decay stress cases) must stay green.
4. **Acceptance.** The four new cases are green; the MB-207 verdict is recorded (fixed or refuted). The kit-quality probe before and after, with the PRESET-IMPACT table for every factory pad whose shell is on (about 90 pads in `tools/membrum_preset_generator.cpp`, for example `:521-524`, `:594-597`, `:680-683`). On the parameter rows of `specs/membrum-audit/probes/param-audibility.csv`: `secondaryMaterial` (rows 173-175; today the pad-36 centroid FALLS 256 -> 232 Hz as material rises) must show a centroid that does not fall; `secondarySize` (rows 170-172) is recorded before and after. The audible change is the finding's purpose, and the moved shell pitches will be heard on every coupled factory pad: show the table and stop for the user's ruling before the commit.

### T4.20 — Head-to-shell coupling becomes a two-mass normal-mode pair

**Findings:** MB-231 (medium, PLAUSIBLE)

**Flags:** PRESET-IMPACT, PHYSICS, VERIFY-FIRST; STATE-COMPAT added when the user rules to build the model: a coupling frequency needs a control, and either control the ruling can choose changes saved state. A new per-pad parameter appends a sound slot and a pad offset; reusing Coupling Strength gives a stored value a new meaning. `pad_config.h:294-306` holds no field that could carry it today.

**Prerequisites:** T4.19

**Estimate:** 240 min

Code anchors: the feedforward shell stage in `DrumVoice::processBlockFast` (`drum_voice.h:802-846`: `secExc[i] = bodyScratch[i] * effectiveCoupling_` at `:816-817`, `bodyScratch[i] += secOut[i]` at `:842-843`) and in `processBlockSlow` (`:1033-1044`); the noteOn gate at `:391-406`; `effectiveCouplingStrength()` at `:1494-1497`. The comment at `:805-808` says the coupling "reads the previous-block's primary output", but `:817` reads the current chunk's `bodyScratch` (filled at `:799`). Nothing returns energy to the head, so the head's modes are identical with the shell on or off.

The ruling table in the plan head lists T4.20: run the verification test first, then ask whether to build the model. The stale comment at `:805-808` is corrected whatever the answer, because it is wrong today.

1. **Failing test.** New file `plugins/membrum/tests/unit/dsp/test_head_pair_coupling.cpp`, added to the `membrum_tests` source list. `TEST_CASE("Coupled Membrane shows the two-mass (01) doublet at f0 and sqrt(f0^2 + 2 fc^2) (MB-231)", "[membrum][drum_voice][coupling][MB231][physics]")`. Use a `DrumVoice` at 48 kHz, Membrane, Impulse, size 0.398 (natural f0 = 500 x 0.1^0.398 = 200 Hz, `natural_fundamental.h:38`), airLoading 0, modeScatter 0, decay 1, noise and click mix 0, velocity 1. Render 2 s and take a Hann-windowed FFT of the whole render (helpers in `tests/test_helpers/spectral_analysis.h`; read the header for the exact signatures). The coupling control at verification time is today's: secondaryEnabled 1, couplingStrength 0.5. The expected doublet comes from the mapping this task defines: fc = c x kHeadPairFcRatio x f0, with kHeadPairFcRatio = 67 / 44 = 1.523. That is Fletcher and Rossing, The Physics of Musical Instruments, 2nd ed., sec. 18.9: a bass drum with fc = 67 Hz has its (01) pair at 44 and 104 Hz. With c = 0.5, fc = 152.3 Hz and f2 = sqrt(200^2 + 2 x 152.3^2) = 293.6 Hz.
   - Assert a spectral peak within 1 % of 200 Hz and one within 1 % of 293.6 Hz, each at least 20 dB above the median magnitude of the 150-350 Hz band. Today the second peak is absent: the shell layer can never sit above the head (`drum_voice.h:1421-1426`, and after T4.19 its modes follow the beam ratios, not the doublet).
   - A second SECTION types the F&R table values and checks the coded mapping: `DrumVoice::headPairUpperHz(44.0f, 1.0f)` (a new static helper) is within 2 % of 104 Hz. Another assertion types the side-drum (01) pair (182 and 330 Hz, F&R Table 18.6) and records, as INFO only, how close the two-mass formula gets; that drum's lower mode is not at the uncoupled batter frequency, so it is not an equality.
   - A third SECTION checks the beating: c = 0.05 (fc = 15.2 Hz, f2 = 201.15 Hz). The 190-210 Hz band envelope (band-pass the render, then a 10 ms RMS) shows its first minimum after the attack within 10 % of 1 / (f2 - f1) = 0.87 s.
   - If the doublet case PASSES on current code, MB-231 is refuted: keep the test, change no production code except the comment at `:805-808`, and record "refuted".
   - If it fails: stop and ask the user (plan head ruling table): build the model, and with which control? (i) Reuse Coupling Strength: every saved state and factory pad with the shell on gains a doublet. (ii) A new per-pad "Head Coupling" parameter, default 0, so existing states keep their sound. This plan assumes (ii) when the user says "build it" without choosing. If the user declines, the test is not committed; record "not fixed: user ruling" and commit only the comment fix.
2. **Fix (only after a "build" ruling).**
   - Add the control. Under (ii) it gets the next free per-pad offset, an appended sound slot, a global proxy ID below 1000 and a registration in `Controller::initialize`. Follow STATE-COMPAT: the version moves with the other constants to 6 (set by T1.22), with a read path for version 5 that loads 0 into the new field. Under (i) the version-5 read path maps stored couplingStrength so old states keep their sound (head coupling 0 on load), which is a migrated meaning.
   - Model, built at noteOn: after `cachedMapperResult_` is computed (`drum_voice.h:220-221`) and before `bodyBank_.configureForNoteOn` pushes the modes, add a partner mode for the (0,1) mode and for each (1,1) mode the mapper emits (the axisymmetric and first non-axisymmetric pairs F&R measures). Each partner sits at f2 = sqrt(f^2 + 2 fc^2) (helper `headPairUpperHz`, fc = c x kHeadPairFcRatio x f). Both members of a pair take half the head mode's strike amplitude: that is the projection of a strike on one mass onto the equal-mass two-DOF normal modes (in-phase at f, anti-phase at f2), and it is what makes the head displacement beat. Partners use the head mode's damping law. The modes go into the existing `ModalResonatorBank` (respect `kMaxModes`: drop the highest-frequency head modes to make room, never the partners). The bank stays a sum of passive resonators, so the model is stable at any c (Torin, Hamilton and Bilbao, DAFx-14, report section 5) and costs nothing extra per sample.
   - Every path that rewrites the modes keeps the pairs: `updateModalFundamentalOnBank` (`:1607-1625`, which scales `cachedMapperResult_` frequencies), the Material Morph refresh and `updateModalParameters`. The simplest way is to insert the partners into `cachedMapperResult_` itself, so every consumer scales them along with the rest.
   - The strike normalisation key (`StrikeNormKey`, `:1717-1727`, as T1.6 left it) gains the new control.
   - The secondary shell layer of T4.19 stays as the shell: the measured doublets come from head-to-head air coupling, not from the shell. Correct the comment at `:805-808`. Every new value (kHeadPairFcRatio, the formula, the 1/2 split) carries its source in a comment next to it.
3. **Build and gate.** Standard, plus the STATE-COMPAT obligations (a version-5 blob captured before the change loads with head coupling 0 and renders as before; `getState(setState(kit))` is idempotent for all 20 kits) and the UI obligations if a new knob goes into `editor.uidesc`. That makes the UI flag apply as well: add it to the commit message.
4. **Acceptance.** The doublet, F&R-table and beating sections are green. With head coupling 0, `phase1_default.bin` and the kit-quality probe are unchanged. Under (i), the PRESET-IMPACT table for every factory pad with the shell on, and the user's ruling before the commit.

### T4.21 — The struck body's stiffness sets the mallet contact time

**Findings:** MB-249 (low, PLAUSIBLE)

**Flags:** CROSS-PLUGIN IMPACT, PRESET-IMPACT, PHYSICS, VERIFY-FIRST

**Prerequisites:** T4.12 (it reworks the ImpactExciter contact law, MB-225, MB-248, MB-250 and MB-251; this task applies the stiffness inside whatever law T4.12 left)

**Estimate:** 120 min

Code anchors: `MalletExciter::trigger` in `plugins/membrum/src/dsp/exciters/mallet_exciter.h:69-90` (the D4 mass `0.3f * t*t*t` with `t = min(bodySizeHint_ / 0.5f, 1)` at `:88-89`, passed to `core_.trigger(... mass ...)` at `:90`); `ImpulseExciter::trigger` at `impulse_exciter.h:35-39` (fixed mass 0.3); `ExciterBank::setMalletBodySizeHint` at `exciter_bank.h:174-179` (re-applied on a variant swap at `:250-251`); the single caller at `drum_voice.h:336-340`; the shared law `ImpactExciter::trigger` in `dsp/include/krate/dsp/processors/impact_exciter.h:113-139` (`T = kTMin + (kTMax - kTMin) * pow(mass, 0.4)`, `:132-134`). Consumers of `impact_exciter.h`: Membrum's two exciters, and Innexus (`plugins/innexus/src/processor/innexus_voice.h`, `plugins/innexus/tests/unit/processor/bow_exciter_switch_test.cpp`). Membrum has no static head-tension parameter (`plugin_ids.h` has only `kTensionModAmtId`, the energy glide), so the tension term MB-249 proposes is dropped: say so in the commit message. Ordering hazard: the hint is set at `:340`, after the strike-normalisation probe at `:302` runs the live exciter. `specs/membrum-audit/probes/limiter-probe-summary.txt:81` shows 38 of 101 Mallet pads probed with a stale hint. Check whether T1.6 already moved it; if not, the stiffness must be set before the probe.

1. **Failing test.**
   - Shared side, `dsp/tests/unit/processors/impact_exciter_test.cpp`: `TEST_CASE("ImpactExciter contact stiffness scales the Hertz term as k^-0.4 and defaults to the legacy pulse (MB-249)", "[impact_exciter][MB249]")`. Two exciters with the same seed: one never touched and one at `setContactStiffness(1.0f)` give the same pulse length in samples (an integer, same TU). With stiffness 4, the contact time above kTMin scales by 4^-0.4 = 0.574 (within 2 %). It fails to compile today: the setter does not exist. Treat that as red, and also run the Membrum cases below, which compile today.
   - Membrum side, extend `plugins/membrum/tests/unit/exciters/test_mallet_contact_hint.cpp`:
     - `TEST_CASE("Contact time shortens from centre to rim on a Membrane (MB-249)", "[exciters][mallet][D4][MB249][physics]")`. Mallet and Impulse at velocity 0.5, Membrane size 0.6: T(strikePos 1) / T(strikePos 0) lies in [0.43, 0.64], bounds typed from Wagner, KTH MSc thesis 2006, Fig. 4.7 (centre about 7 ms, rim 3 to 4.5 ms). Use the pulse-length helper this file already has. Today the ratio is 1.0: strike position never reaches the exciter.
     - `TEST_CASE("Hard small bodies get short contacts for Impulse and Mallet (MB-249)", "[exciters][mallet][impulse][D4][MB249]")`. Plate and Bell at size 0.3, velocity 0.5: FWHM below 2 ms for both exciters. Today Impulse keeps mass 0.3 (T = 8.23 ms at v 0.5) and Mallet at size 0.3 gives about 4.7 ms.
   - To test the voice wiring, add a static `DrumVoice::contactStiffnessFor(BodyModelType, float size, float strikePos)` and test it directly. It is monotonic in strikePos on Membrane, and it returns exactly 1.0 for Membrane at size >= 0.5 and strikePos 0.3.
   - If both Membrum cases pass on current code, the finding is refuted: keep the tests and change nothing.
2. **Fix.**
   - `impact_exciter.h`: add `void setContactStiffness(float kRel) noexcept` (clamped to [0.25, 16], default 1.0) and apply it in the Hertz term as `pow(mass / kRel, 0.4f)` inside the law T4.12 left (Hertz: T ~ (m/k)^(2/5), Avanzini and Rocchesso SMC 2004, report section 5). Stiffness 1.0 must take the existing arithmetic path, so Innexus stays bit-identical: branch on `kRel == 1.0f` rather than divide by 1.
   - `ExciterBank`: generalise the hint into `setContactStiffness(float)`, cached and re-applied on a variant swap like `pendingMalletSizeHint_`, and forwarded to both `ImpulseExciter` and `MalletExciter` (each forwards it to its `core_`). Keep the D4 size hint: it is the mass term, and test_mallet_contact_hint's existing cases pin it.
   - `DrumVoice::contactStiffnessFor` = bodyFactor(type) x posFactor(strikePos). posFactor is normalised to 1 at strikePos 0.3 and reaches (7/4)^2.5 = 4.05 relative to the centre at strikePos 1 (Wagner Fig. 4.7 through T ~ k^-0.4; Membrum's strikePos 1 is r/a = 0.9, `membrane_mapper.h:112`). bodyFactor is 1 for Membrane and String. For Plate, Shell and Bell it is the smallest value that meets the < 2 ms FWHM case. It is a design value with no published number in report section 5, and the comment says so. NoiseBody uses the Plate value.
   - Call `exciterBank_.setContactStiffness(contactStiffnessFor(...))` before the strike probe at `:302` (and keep `setMalletBodySizeHint` before it too).
   - Before: contact time depends on Size only, and only for Mallet. After: strike position and body hardness also shorten it, for both impact exciters. Membrane at size >= 0.5 and strikePos 0.3 is unchanged, which preserves the tension-glide calibration noted at `mallet_exciter.h:56-57`.
3. **Build and gate.** Add `dsp_processors_tests` to the build. Run `grep -rl "impact_exciter.h" plugins/ dsp/include tests/` and list the consumers in the commit message. Run `node tools/header-consumers.js --tag '[membrum]'` and gate every executable it names with `~[long]` appended (expect `innexus_tests`; if it is missing, find out why before trusting the list). Run clang-tidy with `-Target dsp` as well. The strike probe re-measures with the new contact, so pad levels stay normalised; the timbre moves.
4. **Acceptance.** The three new cases are green, and Innexus's suite is green with its goldens unmoved. The existing D4 cases stay green (`"Drum-sized hints keep the legacy soft mallet contact exactly"`). The kit-quality probe before and after gives the PRESET-IMPACT table for every Impulse and Mallet pad whose strikePos is not 0.3 or whose body is not Membrane, with the centroid change per pad. The MB-249 verdict is recorded. Show the table and stop for the user's ruling.

### T4.22 — Click burst length is documented as a perceptual tick, or re-ranged to measured contact times

**Findings:** MB-226 (low, PLAUSIBLE)

**Flags:** PHYSICS, VERIFY-FIRST

**Prerequisites:** none

**Estimate:** 60 min

Code anchors: `plugins/membrum/src/dsp/click_layer.h`. The header claims "2-5 ms shaped filtered-white-noise burst ... modeling the finite contact time" (`:9-12`, citing the Avanzini and Rocchesso impact model at `:7`). `denormContactMs` maps norm to 2 + 3 x norm ms (`:160-164`). `trigger()` sets `vScale = 1 - 0.3 v` and `burstMs = max(0.5, contact x vScale)` (`:95-96`), so the range that plays is 1.4 to 5 ms. The body's real contact model is `ImpactExciter` (0.5 to 15 ms, `impact_exciter.h:132-133`). This task carries PHYSICS without PRESET-IMPACT, so by the plan's rule it corrects documentation only and must not change rendered audio. Option (b) of the finding (re-range to about 3-10 ms with a beater-mass term) changes every pad's onset: do it only if the user rules for it, and then the task also becomes PRESET-IMPACT.

1. **Failing test.** Extend `plugins/membrum/tests/unit/dsp/test_click_layer.cpp`. Add `static constexpr float kBurstMinMs` and `kBurstMaxMs` to `ClickLayer`, set to the range the header documents today (2.0 and 5.0), and reference them from the header text.
   - `TEST_CASE("ClickLayer: realised burst length spans exactly the documented range and shortens with velocity (MB-226)", "[membrum][dsp][click_layer][MB226]")`. Measure the burst length in samples (reuse the measurement of `"ClickLayer: burst fits within configured contact time"`, `:60`) at contact norm 0 and 1 and velocity 0 and 1, at 48 kHz. Assert that the minimum over the grid equals `kBurstMinMs` within one sample, that the maximum equals `kBurstMaxMs` within one sample, and that burst(v = 1) < burst(v = 0.3) at norm 0 and at norm 1.
   - Red today: the measured minimum is 1.4 ms (norm 0, v 1) against the documented 2.0. If it is green, the finding is refuted.
2. **Fix (option (a), the default).** Set `kBurstMinMs = 1.4f`. Rewrite the header at `:6-16`. The click is a perceptual attack tick layered on the selected exciter (Cook's sines + noise + transients, `:6-8`), not a model of stick contact. It plays 1.4 to 5 ms, shortened up to 30 % by velocity, in the same direction as measured contact (Wagner 2006 sec. 4.2.1). Published stick-head contacts are 3-6 ms (Wagner 2006) and 5-8 ms for a mezzoforte centre stroke (Dahl, Acoustical Science and Technology 32(5), 2011). The contact itself is modelled by `ImpactExciter`. Change no arithmetic. Before: a header that claims 2-5 ms and a contact model. After: a header that states what plays and what it is. The rendered audio is bit-identical (the new constants feed only the test and the text).
3. **Build and gate.** Standard. The audit's render checks must not move: run the kit-quality probe once and confirm that `kit-pads.csv` is unchanged for every pad.
4. **Acceptance.** The new case is green with the measured minimum 1.4 ms (record the sample counts), and the MB-226 verdict is recorded. The kit-quality probe is identical before and after. If the user ruled for option (b), follow PRESET-IMPACT instead, including the before/after table.

### T4.23 — Tension glide magnitude and curve: square-root law, calibrated depth, truthful citations

**Findings:** MB-224 (low, PLAUSIBLE), MB-243 (low, CONFIRMED)

**Flags:** PRESET-IMPACT, PHYSICS, VERIFY-FIRST (MB-224 only)

**Prerequisites:** T4.14 (the modal-energy follower that drives the glide, MB-222, MB-223, MB-272), T4.21 (the contact time changes the injected energy)

**Estimate:** 150 min

Code anchors: `kTensionEnergyScale = 3.3e-5f` (`drum_voice.h:86-94`); `kTensionPitchModMax = 1.19f` with its comment "Real membranes saturate; ... (Avanzini-Marogna-Bank)" (`:96-100`); the duplicated law `std::min(1.0f + tensionAmtEffective_ * energyEnv_, kTensionPitchModMax)` in `processBlockFast` (`:725-728`) and `processBlockSlow` (`:948-951`); `tensionAmtEffective_ = clamp(knob) * kTensionEnergyScale`, with no velocity term (`:420-421`); the stale member comment `// depth * velocity^2` (`:1761`). Stale documentation: `pad_config.h:141-143` ("Kirby & Sandler 2021 JASA. Depth is scaled by velocity^2") and `:308-310` ("0 -> 0.15 effective max ... matches JASA 2021"); `default_kit.h:178-179` ("JASA 2021 Kirby & Sandler"); `MANUAL.md:263` ("Depth scales by velocity² at note-on ... ~2 semitones") and `:267`. T4.14 changes the follower, so re-read these lines first.

1. **Failing test.** Extend `plugins/membrum/tests/unit/dsp/test_per_mode_damping.cpp`.
   - `TEST_CASE("Phase 8E: tension glide has no flat plateau while modal energy falls (MB-224)", "[phase8e][drum_voice][audio][MB224]")`. Drive the voice as `"Phase 8E: Tom tension modulation drifts pitch vs velocity"` (`:481-528`) does (Mallet, Membrane, material 0.4, decay 0.5, strikePos 0.3, noise and click 0, knob 1, velocity 1), but in 32-sample blocks. Run sizes 0.2, 0.4, 0.6, 0.8 and 1.0 at 44.1 kHz, and size 0.5 at 96 kHz. Each block, record `getModeFrequency(0)` and `getModalEnergy()` of `getBodyBankForTest().getSharedBank()`. Assert that no run longer than 5 ms holds the mode-0 pitch constant within 0.01 semitone while the energy is falling block over block. Baseline: size 1.0 sits at exactly +3.01 st from 20 ms to 100 ms; at 96 kHz the generic tom sits there from 10 ms to 200 ms (the kTensionPitchModMax clamp). Re-measure on the T4.14 code before trusting those numbers. If the case is green there, MB-224 is refuted: keep the test, keep the 1.19 clamp, and still correct its comment (step 2c).
   - `TEST_CASE("Phase 8E: tension pitch ratio follows sqrt(1 + 2 k E) (MB-224)", "[phase8e][drum_voice][MB224][physics]")` on a new `static float DrumVoice::tensionPitchRatio(float kE) noexcept`. The expected values are typed, not computed: kE 0 -> 1.0, kE 0.09 -> 1.08628, kE 0.19 -> 1.17473, kE 2.0 -> 2.0 (the rail). The form comes from Marogna, Avanzini and Bank (DAFx-10), Eq. 7a with c^2 = T/sigma and Eq. 20 (Tqs linear in E), so the frequency ratio is sqrt(1 + Tqs/T0). It fails to compile today; that counts as red only together with the plateau case.
   - MB-243's comment defects are CONFIRMED and need no test. Its kit-side request (the default-kit tom should glide 0.8 to 2.0 semitones at v = 1, Rossing's 8-10 % "nearly a whole tone") is kit data. The default tom's 0.25 knob was lowered on purpose (`default_kit.h:180-182`), so that test is written by T5.6, which retunes the default kit. Record the hand-over in the checklist.
2. **Fix.**
   a. Add `tensionPitchRatio(kE) = std::min(std::sqrt(1.0f + 2.0f * std::max(kE, 0.0f)), kTensionPitchRail)`, clamping the sqrt argument at its domain edge (macOS rule). Use it in both renderers in place of the two `std::min(1 + ..., kTensionPitchModMax)` expressions (`:725-728`, `:948-951`), which removes the duplicated law. The small-signal slope equals today's (d/dE at 0 is k), so `kTensionEnergyScale` keeps its value and its knob-1 calibration test (`highGlideSemis` in [1.0, 2.5], `:526-527`) must stay green unchanged.
   b. Replace `kTensionPitchModMax = 1.19f` with `kTensionPitchRail = 2.0f` (+12 st). Its comment: a safety rail for runaway (Feedback) energy, set far above musical use. The source states no saturation (the MAB paper validates the linear Tqs(E) law up to 20 m/s strikes, report section 5). Measured tom glides reach about an octave at a hard hit (Kirby and Sandler, DAFx-20, Fig. 7: a 7x12" tom's chirp spans 100-200 Hz).
   c. Correct the documentation to the coded law: the depth is knob x 3.3e-5 x modal energy, velocity enters through the modal energy (about velocity^2), and the ratio is sqrt(1 + 2kE) under a +12 st safety rail. Cite Marogna, Avanzini and Bank (DAFx-10) for the law and Rossing (Physics Today 1992) plus Kirby and Sandler (DAFx-20) for measured magnitudes. Drop "matches JASA 2021" everywhere it appears (`pad_config.h:141-143`, `:308-310`, `default_kit.h:178-179`, `MANUAL.md:263` and `:267`), and fix the member comment at `drum_voice.h:1761`. If T4.14 already rewrote one of these, leave its text.
   d. Before: a linear law with a hard knee at +3.01 st that holds a flat detune plateau. After: a curved law with no plateau in musical use, and documentation that describes the code.
3. **Build and gate.** Standard.
4. **Acceptance.** The plateau case and the ratio case are green; the existing `"Tom tension modulation drifts pitch vs velocity"` (glide 1.0-2.5 st) and `"tension glide also fires on the slow (Feedback) path"` stay green, with the new glide values recorded. On `param-audibility.csv` rows 176-178 (`tensionModAmt`), record the centroid and level per step before and after. The kit-quality probe gives the PRESET-IMPACT table for every factory pad with `tensionModAmt` > 0 on a Membrane body: the Tabla, Orchestral, Trap Modern and Drone kits lean on it (`MANUAL.md:468`, `:478`, `:486`). Show the table and stop for the user's ruling.

### T4.24 — Plate and NoiseBody gain an energy-driven pitch glide with a signed direction

**Findings:** MB-235 (low, PLAUSIBLE)

**Flags:** PRESET-IMPACT, PHYSICS, VERIFY-FIRST; STATE-COMPAT added for part B only: a signed direction needs a stored per-pad value, and `pad_config.h` has none (the only glide field is `tensionModAmt`, `:311`).

**Prerequisites:** T4.23

**Estimate:** 150 min

Code anchors: the Membrane-only gate `tensionActive = tensionAmtEffective_ > 1e-6f && bodyBank_.getCurrentType() == BodyModelType::Membrane` in `processBlockFast` (`drum_voice.h:718-724`) and `processBlockSlow` (`:945-947`); the energy follower at `:910-920` and `:1073-1077`; the retune `updateModalFundamentalOnBank(..., naturalFundamentalHz_ * ratio)` at `:747-754` and `:966-972`. The generator builds gongs on Plate and Bell with `tensionModAmt = 0.0; // Bell: tensionMod is a no-op` (`tools/membrum_preset_generator.cpp:2746-2777`). The tension knob is always active in the editor (`editor.uidesc:260`; no body-dependent dimming in `controller_view_sync.cpp`), so no UI change is needed. The plan head's ruling table lists T4.24: run the verification test first, then ask whether to build.

1. **Failing test.** Extend `plugins/membrum/tests/unit/dsp/test_per_mode_damping.cpp`: `TEST_CASE("Phase 8E: Plate pitch glide follows strike energy (MB-235)", "[phase8e][drum_voice][audio][MB235][physics]")`. Use a `DrumVoice` at 44.1 kHz, Plate, Impulse, size 0.5, decay 0.8, noise and click 0, tension knob 1, rendered in 64-sample blocks for 600 ms. Read `getModeFrequency(0)` at 20 ms and at 500 ms. At velocity 1 they differ by at least 1 semitone, with the higher pitch at 20 ms (the hardening, downward glide of large flat gongs). At velocity 0.3 they differ by less than 0.2 semitone. The magnitudes come from Chaigne, Touzé and Thomas, Acoust. Sci. & Tech. 26(5) 2005, sec. 4.1: large opera gongs glide down "by as much as three semitones". Repeat the velocity-1 assertion as a SECTION for NoiseBody. Today both are red: the gate keeps the pitch constant on every non-Membrane body. If it is green, the finding is refuted.
   - Then stop and ask the user (ruling table): build part A, part B, or neither? If neither, the test is not committed; record "not fixed: user ruling".
2. **Fix.**
   - Part A (no state change): widen the gate at both sites to Membrane, Plate and NoiseBody. Extract one `static bool energyGlideBody(BodyModelType)` so the two renderers cannot diverge. Use `tensionPitchRatio` from T4.23, which gives the hardening direction: pitch is highest at the strike and falls as the energy decays. If the Plate's modal-energy scale differs from the Membrane's calibration, so that a knob-1, velocity-1 Plate glides far outside 1 to 3 semitones, add a per-body energy scale next to `kTensionEnergyScale`. Calibrate it to the Chaigne, Touzé and Thomas "up to three semitones" figure and say so in its comment. Bell and Shell stay excluded: the source places glides on thick gongs, and Membrum's gongs on Bell keep their current sound.
   - Part B (only if the user rules for it; STATE-COMPAT): add a per-pad "Glide Direction" field (next free offset, appended sound slot, global proxy, registration). Its values are hardening (down, the default, so every existing state loads unchanged) and softening, with pitch = 2 - ratio, i.e. starting low and rising toward the natural pitch. Small, strongly curved gongs glide up (same source). Follow every STATE-COMPAT obligation and the UI flag for the new control.
3. **Build and gate.** Standard; with part B, add the STATE-COMPAT steps.
4. **Acceptance.** The MB-235 case is green and the verdict is recorded. Run the kit-quality probe before and after, and list every factory pad on Plate or NoiseBody whose `tensionModAmt` is above 0 (those pads start to glide) with peak, rms500, decay and centroid. Show the table and stop for the user's ruling.

### T4.25 — Strike normalisation acts as a ceiling, so size, damping and strike position move the level

**Findings:** MB-218 (low, PLAUSIBLE), MB-220 (low, PLAUSIBLE)

**Flags:** PRESET-IMPACT, PHYSICS, VERIFY-FIRST

**Prerequisites:** T1.6 (the strike-normalisation probe and cache, MB-003, MB-021, MB-032, MB-036), T2.11 (String level normalisation)

**Estimate:** 240 min

Code anchors: `kBodyHeadroom = 0.5f` and its comment "Final loudness make-up is master gain + per-pad Level ... NOT this budget" (`drum_voice.h:73-84`); `bank.setOutputGain(measuredStrikeOutputGain(bank, params_, body, exciter))` at `:302`, used for every body that is neither Feedback nor String; `probeStrikePeakExcited` (`:1322-1363`), `measuredStrikeOutputGainExcited` returning `kBodyHeadroom / peak` (`:1380-1387`), the cached wrapper (`:1394-1412`) and `StrikeNormKey` (`:1717-1727`). T1.6 reworks all of these: re-read them and apply this task to the form T1.6 left. Default-kit compensation: "Fix D" at `default_kit.h:456-469` (`tomLevel` 0.95 to 0.67, +3.0 dB). Baseline: `specs/membrum-audit/probes/probe-results.txt:34-38`, DrumVoice at 48 kHz, Impulse, velocity 1, level 1, body-only peak: Membrane -5.96, Plate -5.94, Shell -6.67, Bell -5.98, NoiseBody -6.16 dBFS. Every modal body lands within 0.7 dB of -6.02. The plan head's ruling table lists T4.25: without a ruling, the normalisation stays and only the comments change.

1. **Failing test.** New file `plugins/membrum/tests/unit/dsp/test_strike_level_ceiling.cpp`, added to the source list. Each case uses a `DrumVoice` at 48 kHz, Impulse, velocity 1, level 1, noise and click mix 0, rendering 300 ms and taking the absolute peak.
   - `TEST_CASE("Strike position moves the body level: a rim strike is quieter than a centre strike (MB-218)", "[membrum][drum_voice][level][MB218]")`. Membrane at size 0.6: peak(strikePos 0.9) <= peak(strikePos 0.1) - 3 dB. Today they are equal within 0.7 dB.
   - `TEST_CASE("Size moves the body level: a bigger head is not quieter at equal Level (MB-218)", "[membrum][drum_voice][level][MB218]")`. Membrane: peak(size 0.8) >= peak(size 0.4).
   - `TEST_CASE("Default-kit toms get louder from high tom to floor tom (MB-220)", "[membrum][default_kit][level][MB220]")`. Render the six default-kit tom pads (`tomPad` indices 5, 7, 9, 11, 12, 14, `default_kit.h:460`) through `VoicePool` with the kit as shipped, velocity 1, 400 ms each. Measure `Krate::DSP::TestUtils::kWeightedLevelDb` (`tests/test_helpers/k_weighting.h:111`) over 0-400 ms. The levels must not increase from floor tom to high tom, and floor >= high + 2 LU. The source is the ISO 226:2003 equal-loudness contours (Suzuki and Takeshima, JASA 116(2), 2004, report section 5). A verifier measured the floor tom LOUDER than the high tom in rms500 on factory toms (report, MB-220 notes), so this case may pass today. If it does, MB-220 is refuted: keep the test.
   - Keep `test_body_headroom_budget.cpp` (`"N-1: modal body stays under the per-voice rail at level 1.0"`) as the guard that no configuration reaches the ±1 rail.
   - If the MB-218 cases pass on current code, MB-218 is refuted.
   - Then stop and ask the user (ruling table): adopt the ceiling? Without a "yes", do only step 2c.
2. **Fix (after a "yes").**
   a. Measure a reference peak per modal body type once, at `DrumVoice::prepare` (not the audio thread). Use the same probe as T1.6 at a reference configuration: size 0.5, strikePos 0.3, material 0.5, decay 0.3, derived damping, Impulse, velocity 1, click as the pad default. This gives `refPeak_[body]`. Recompute it whenever `prepare` sees a new sample rate.
   b. At noteOn, set `gain = kBodyHeadroom / refPeak_[body]`. Then cap it: `gain = min(gain, kBodyHeadroom * kStrikeCeiling / peak_thisConfig)` with `kStrikeCeiling = 1.6f` (a body peak of at most 0.8, -1.9 dBFS, still under the hardClip rail). Use the per-configuration probe that T1.6 kept. Size, strike position, material, damping and exciter then move the v = 1 peak as they do in a struck body: per-mode gains under one fixed output scale, as in van den Doel and Pai (ICAD 1996) and Fletcher and Rossing ch. 7 and 18 (report section 5). A hot configuration is still bounded. String stays as T2.11 left it.
   c. Comments (done whatever the ruling): rewrite `drum_voice.h:73-84`. Under no ruling it states that the per-configuration normalisation equalises the v = 1 peak across Size, Strike Position, Material, damping and exciter, so those parameters do not move the level, and names MB-218. Under a "yes" it describes the ceiling. The Fix D comment at `default_kit.h:456-457` is kit data and is updated by T5.6; do not touch `default_kit.h` here.
3. **Build and gate.** Standard. `tests/golden/phase1_default.bin` will move under a "yes": re-baseline it deliberately and say why in the commit message.
4. **Acceptance.** Re-run the probe lines `level:body=<Body>` (`probe-results.txt:34-38`) and record the five body peaks at the reference config: each within 1 dB of -6.02 dBFS, since the reference is what the old target was. The MB-218 and MB-220 verdicts are recorded. The kit-quality probe gives the PRESET-IMPACT table for all 20 kits (every modal pad's peak and rms500 can move), plus the limiter-hit count, which may rise as hot configurations climb toward the ceiling: name every pad that newly reaches the -7.00 dBFS bus ceiling. Show the table and stop for the user's ruling.

### T4.26 — Bus limiter: one millisecond of look-ahead with reported latency, and an honest release description

**Findings:** MB-241 (low, PLAUSIBLE), MB-242 (low, PLAUSIBLE)

**Flags:** CROSS-PLUGIN IMPACT, PRESET-IMPACT, PHYSICS, VERIFY-FIRST

**Prerequisites:** none

**Estimate:** 180 min

Code anchors: `dsp/include/krate/dsp/processors/true_peak_limiter.h`. The header says the instantaneous attack "for percussive material is acceptable" (`:17-18`); `kDefaultReleaseMs = 80.0f; ///< program-dependent recovery` (`:50`) is in fact one fixed coefficient (`setReleaseMs`, `:139-144`); the zero-look-ahead path sets `currentGain_ = required` on the same sample and runs a linear one-pole release (`:246-251`); the output is clamped to ±ceiling (`:256-257`). The opt-in look-ahead exists: `setLookaheadSamples` (`:124-131`, `kMaxLookaheadSamples = 1024` at `:125`), applied in `prepare` (`:76-80`), with its path at `:217-243`, tested in `dsp/tests/unit/processors/true_peak_limiter_test.cpp:295-383`. Membrum: `busLimiter_` (`processor.h:144`) gets only `prepare` and `setCeilingDb(-1.0f)` in `Processor::setupProcessing` (`processor.cpp:1087-1088`), runs at `:717` on the main bus after the coupling return (`:693-708`) and before master gain (`:725-737`). The aux buses are written by `voicePool_.processBlock` (`:663-666`) and never pass the limiter. The processor has no `getLatencySamples`. Other consumers of `true_peak_limiter.h`: Profundum, Seraphis and Vorago (engines under `dsp/include/krate/dsp/systems/`), their tests, and `tests/test_helpers/low_frequency_metrics.h`. The plan head's ruling table lists T4.26: latency must be accepted by the user.

1. **Failing test.** New file `plugins/membrum/tests/unit/processor/test_bus_limiter_lookahead.cpp`, added to the source list.
   - `TEST_CASE("Bus limiter does not hard-clip the attack of a dense downbeat (MB-241)", "[membrum][processor][limiter][MB241]")`. Use a Processor at 44.1 kHz, block 256, the default kit, master gain at 0 dB (norm 24/36 = 0.6667), global coupling 0. Play two renders: a downbeat of kick, snare, closed hat and crash at velocity 1 in one block, and the 16-pad downbeat (pads 36-51 at velocity 1, as probe `level:16-hits`, `probe-results.txt:31-33`, pre-limiter peak +9.82 dBFS). Assert two things:
     - (a) No output sample lies within 1e-5 of the -1 dBTP ceiling as a sample value (0.8913).
     - (b) The largest one-sample drop of the limiter gain is below 0.5 dB. The gain is out[n + L] / in[n] on samples with |in| > 1e-2, where L is the reported latency (0 today). The reference input comes from the same Processor configuration with every pad's Level divided by 4. The voice path is linear in Level (`voice_pool.cpp:617` stores the norm as the linear multiplier, and the per-voice rail is idle below 1), so that render, multiplied by 4, is the pre-limiter signal while the limiter stays idle. Assert the reference's own peak is below -13 dBFS.
     - Baseline from the finding (44.1 kHz, kick+snare+hat+crash): 2 samples at the ceiling, a 1.85 dB one-sample drop; kick+snare at +6 dB: 7 samples, 2.13 dB.
   - `TEST_CASE("Membrum reports the bus limiter look-ahead as latency and keeps the aux buses aligned (MB-241)", "[membrum][processor][limiter][multi-bus][MB241]")`. At 44.1, 48 and 96 kHz, `getLatencySamples()` equals `std::lround(1e-3 * sampleRate)`. A pad routed to aux bus 2 has its first non-zero sample on aux 2 at the same index as on the main bus. Today `getLatencySamples()` returns 0 (the base class).
   - `TEST_CASE("TruePeakLimiter release hold removes in-cycle gain ripple on a hot kick (MB-242)", "[true_peak_limiter][gain_staging][MB242]")` in `true_peak_limiter_test.cpp`. Take a decaying 50 Hz sine burst (period 20 ms, peak +8 dBFS, decay 0.3 s) through the limiter at ceiling -1 dB with 44 samples of look-ahead. Compute the residual y - x·g15, where g15 is the limiter gain low-passed at 15 Hz (Faust compressors.lib `limiter_lad`: fluctuations above about 15 Hz are heard as timbre). It must be at or below -40 dB relative to the output. The finding measured -27.6 dB on a +9 dB kick without look-ahead and -27.5 dB with 64 samples. The setter this case needs does not exist yet, so it is red at compile time. The first case is the behavioural red.
   - If (a) and (b) pass on current code, MB-241 is refuted; if the residual case meets -40 dB without a hold, MB-242's hold is refuted. Then stop and ask the user (ruling table): accept 1 ms of reported latency and aux delay? Without a "yes", do only the comment changes in step 2c.
2. **Fix.**
   a. (After a "yes".) In `Processor::setupProcessing`, call `busLimiter_.setLookaheadSamples(static_cast<std::size_t>(std::lround(1.0e-3 * sampleRate_)))` before `busLimiter_.prepare(...)`. Store the value in `std::atomic<int> reportedLatency_` and return it from a new `getLatencySamples()` override, mirroring Profundum (`plugins/profundum/src/processor/processor.h:91` and its `processor.cpp`). Allocate, in `setupProcessing`, one stereo delay line of that length per aux bus (`kMaxOutputBuses` = 16, `pad_config.h:28`) and delay each active aux bus by the same N samples after `voicePool_.processBlock` (`:663-666`), before its silence flag is computed (`:669-685`). The audio thread never allocates. Existing Membrum tests that check onset timing against `sampleOffset` (T1.14's) subtract the reported latency; they are never loosened.
   b. (MB-242, if its case was red.) Add an opt-in `setReleaseHoldMs(float)` to `TruePeakLimiter` (default 0, so Profundum, Seraphis and Vorago stay bit-identical; prove it with a case that renders the same input with and without an explicit `setReleaseHoldMs(0)` and compares sample counts and values in the same TU). The hold keeps the gain at its last minimum for the hold time before the release starts (Zmölnig, IEM 1999 limiter notes). Membrum sets 25 ms (about one kick period).
   c. Comments (whatever the ruling): `true_peak_limiter.h:17-18` states the trade-off without calling it acceptable. Zero look-ahead pins each new rising-edge maximum to the ceiling, which is clipper behaviour, and the literature treats instantaneous attack as the distortion case (Giannoulis, Massberg and Reiss, JAES 2012, sec. 1; Hämäläinen, DAFx-02). `:50` becomes `///< fixed one-pole release`.
3. **Build and gate.** Add `dsp_processors_tests` to the build. Run `grep -rl "true_peak_limiter.h" plugins/ dsp/include tests/` and list the consumers in the commit message. Run `node tools/header-consumers.js --tag '[membrum]'` and gate every executable its last line names with `~[long]` appended. Expect `profundum_tests`, `seraphis_tests` and `vorago_tests` (their engines and tests include the header, as does `tests/test_helpers/low_frequency_metrics.h`); if one is missing, find out why before trusting the list. Run clang-tidy with `-Target dsp` too. Run pluginval: it checks latency reporting.
4. **Acceptance.** The three cases are green with the numbers recorded (samples at the ceiling, largest drop in dB, residual in dB, against the baselines above), and both verdicts are recorded. The consumer suites are green with no golden moved. The kit-quality probe before and after shows no pad changes level on a single hit (the limiter is idle on ordinary playing: `probe-results.txt:40` measures a 0.00 dB reduction for kick+snare+hat at velocity 100). List the 21 pads that reached the ceiling in the baseline with their new peak. The user's ruling on latency is recorded.

## Phase 5 — Dead and weak parameters, then kit data

Make the remaining dead or weak controls audible (T5.1-T5.5), then retune the default kit and the factory kits through tools/membrum_preset_generator.cpp against the corrected engine (T5.6-T5.13).

### T5.1 — Feedback Amount and Friction Pressure are audible at the voice output, pinned by voice-level tests

**Findings:** MB-023 (low, PLAUSIBLE)

**Flags:** PRESET-IMPACT, VERIFY-FIRST

**Prerequisites:** T1.16 (the Feedback square-wave rail, MB-017), T1.17 (the inverted Friction velocity response, MB-019)

**Estimate:** 150 min

Code anchors: `FeedbackExciter::trigger` sets `drive = velocity + feedbackDrive_ * (1 - velocity)` and `feedbackAmount_ = drive * kMaxFeedback` (`plugins/membrum/src/dsp/exciters/feedback_exciter.h:108-112`, `kMaxFeedback = 0.85f` at `:44`). At velocity 1 the knob cannot change anything, by construction. `FrictionExciter` adds `pressureAmount_ * 0.5f` to the bow pressure (`friction_exciter.h:76-81`), which changes the BowExciter slope `5 - 4p` (`dsp/include/krate/dsp/processors/bow_exciter.h:315-323`). The bow never hears the body (`process(0.0f)`, `friction_exciter.h:124`). The existing tests accept a 1e-4 mean difference over 2048 samples (`tests/unit/voice_pool/test_dead_exciter_params.cpp:113`, `:125`). Verifier measurements (report, MB-023 notes): Feedback Amount is bit-identical at velocity 1 and moves the level by at most 0.02-0.08 dB below it. Friction Pressure moves the level by 2.1-5.3 dB at velocity 0.6, but only 0.3-1.6 dB at velocity 1. T1.16 and T1.17 change both exciters: re-read them first.

1. **Failing test.** Extend `test_dead_exciter_params.cpp`. Reuse `renderPad0`, but render 44100 samples (1 s) and compare features from `Krate::Test::extractAudioFeatures` (`tests/test_helpers/audio_features.h:37`).
   - `TEST_CASE("Dead-param: Feedback Amount is audible at the voice output at every velocity (MB-023)", "[membrum][voice_pool][dead_param][feedback][MB023]")`. At velocities 0.3, 0.6 and 1.0, Feedback Amount 0 vs 1 changes `rmsDbfs` by more than 1.5 dB OR `centroidHz` by more than 10 %. The criterion is from the finding.
   - `TEST_CASE("Dead-param: Friction Pressure is audible at the voice output at every velocity (MB-023)", "[membrum][voice_pool][dead_param][friction][MB023]")`. Same criterion, for Friction Pressure 0 vs 1.
   - Keep the old 1e-4 cases.
   - The verdict is per parameter. Feedback is expected red at velocity 1 (bit-identical) and below. Friction may already be green at 0.3 and 0.6; if it is green at all three velocities, the Friction half is refuted. Baseline from `param-audibility.csv`: `feedbackAmount` rows 101-103, max level change 0.00 / 0.02 / 0.00 dB and centroid 3.4 / 4.8 / 1.5 %; `frictionPressure` rows 107-109, 1.02 / 0.48 / 0.21 dB and 14.0 / 1.3 / 1.8 %.
2. **Fix.**
   - Feedback Amount (if red): in `FeedbackExciter::trigger`, make the knob set the loop gain at every velocity: `feedbackAmount_ = kMaxFeedback * (kFeedbackLoopMin + (1.0f - kFeedbackLoopMin) * feedbackDrive_)`, with kFeedbackLoopMin chosen so the knob spans an audible range (start at 0.3 and record the value that meets the test). Velocity keeps its two other roles, the exciter's own amplitude (`amplitudeEnv_ = velocity`, `:112`) and the lowpass opening (FR-016, `:113`), so velocity still shapes level and brightness. The bound `feedbackAmount_ <= kMaxFeedback` (the passivity condition in the header, `:16`) still holds. Before: the knob raises a drive floor that velocity 1 already reaches. After: the knob is the sustain and loop gain.
   - Friction Pressure (only if red): keep the bow open-loop, because closing it is the Friction-sustain question T6.10 owns (plan head, PRESET-IMPACT). Widen what pressure does to the bow at a fixed velocity instead. Map `pressureAmount_` to the full BowExciter pressure range rather than a +0.5 bias that saturates at velocity 1, so the slope `5 - 4p` and the force shape move at every velocity. The strike normalisation (after T1.6, keyed on pressure) levels the peak, so the test's centroid term is what this must move.
   - Correct the header comments that describe each mapping (`feedback_exciter.h:20`, `:93-99`; `friction_exciter.h:8-12`, `:65`).
3. **Build and gate.** Standard. The default value of Feedback Amount is 0 (`pad_config.h:235`), so every Feedback pad changes sound. In `tools/membrum_preset_generator.cpp`, 13 lines set `feedbackAmount` and 10 lines name `ExciterType::Feedback`; the kit-quality probe gives the exact pad list.
4. **Acceptance.** Both new cases are green, with the measured dB and % per velocity recorded, and the per-parameter verdict is recorded. In `param-audibility.csv` the `feedbackAmount` and `frictionPressure` rows read "ok" with level or centroid change above the case's criterion. The kit-quality probe gives the PRESET-IMPACT table for every Feedback and Friction pad. Show the table and stop for the user's ruling.

### T5.2 — Global Coupling and Coupling Delay have an audible range, and the manual says what the delay is

**Findings:** MB-169 (medium, PLAUSIBLE), MB-170 (low, CONFIRMED), MB-259 (low, CONFIRMED)

**Flags:** CROSS-PLUGIN IMPACT, PRESET-IMPACT, VERIFY-FIRST (MB-169 only)

**Prerequisites:** T4.1 (it wires Snare Buzz, Tom Resonance and Coupling Amount into a receiver model, so the coupling return rings at the unstruck pads' modes instead of the struck voice's own partials)

**Estimate:** 180 min

Code anchors: `SympatheticResonance::setAmount` maps [0,1] to -46..-26 dB (`dsp/include/krate/dsp/systems/sympathetic_resonance.h:146-160`, a range calibrated for piano per Lehtonen 2007, `:148-149`). Consumers of that header: Membrum (`drum_voice.h`, `processor.h`, `voice_pool.h`, three coupling tests), Innexus (`processor.h` and its integration test) and `dsp/include/krate/dsp/effects/aether_reverb.h`. In Membrum, `kCouplingDelayId` maps to 0.5 + 1.5 x norm ms (`processor.cpp:425-431`), the delay line is prepared for 2 ms (`:1082`), and the chain runs mono -> delay -> engine -> `applyEnergyLimiter` (-20 dBFS, `processor.h:181-196`) -> L/R (`processor.cpp:693-708`). Spec 140 bounds the result from both sides. SC-002 asks for coupling "at least -40 dBFS below the kick's peak", which `test_coupling_integration.cpp:279-284` pins as a ceiling (coupling RMS < kick peak x 0.01). SC-007 is the -20 dBFS limiter. SC-008 is a 12 dB coincident/gap ratio (`specs/140-membrum-phase5-coupling/spec.md:268-275`). Manual: `MANUAL.md:433` (the delay row), `:437` (the limiter "to prevent runaway feedback"; there is no feedback path: the delay input at `:700` is taken before the return is added at `:705-706`), `:529` (the "big room" tip). T4.1 rewires this chain: re-read all of it first.

1. **Failing test.** Extend `plugins/membrum/tests/unit/processor/test_coupling_integration.cpp` (use its `CouplingIntegrationFixture`).
   - `TEST_CASE("Coupling integration: Global Coupling at 1.0 rings a coupled tom pair audibly (MB-169)", "[coupling][MB169]")`. Use the default kit, global coupling 1.0 and Tom Resonance 1.0. Strike tom note 45 at velocity 1 and leave tom note 47 unstruck. Render 1 s with coupling on and off. In the coupling-on minus coupling-off difference, the energy in a ±3 % band around note 47's lowest mode must be at least -30 dB relative to the dry hit's total energy over the same window. The criterion is from the finding. Today the return is -60.98 dB relative to dry and sits on the struck voice's own partials (`probe-results.txt:18`). Re-measure after T4.1: if the case is already green there, MB-169 is refuted.
   - `TEST_CASE("Coupling integration: Coupling Delay sets the onset of the sympathetic return (MB-170, MB-259)", "[coupling][MB170]")`. With the same setup, take the coupling-only difference signal at delay norm 0 (0.5 ms) and norm 1 (2.0 ms). Its onset (the first sample above -60 dB relative to its own peak) moves by 1.5 ms x sample rate, within ±1 sample. This pins what the parameter does. The audibility of that shift depends on the return being audible, so the first case is the red one for MB-170.
   - `TEST_CASE("Coupling integration: SC-002 kick triggers audible snare buzz", "[coupling]")` (`:240-285`) and the SC-007 and SC-008 cases stay unchanged and green. They are the spec's bounds.
2. **Fix.**
   - `sympathetic_resonance.h`: add `void setAmountRangeDb(float minDb, float maxDb) noexcept`. Its default is (-46, -26), so Innexus and AetherReverb stay bit-identical; add a `dsp_systems_tests` case in `dsp/tests/unit/systems/sympathetic_resonance_test.cpp` that pins the default mapping (amount 0.5 -> -36 dB, typed). `setAmount` uses the stored range.
   - Membrum calls `couplingEngine_.setAmountRangeDb(...)` in `Processor::setupProcessing`. Start from the finding's -30..-6 dB and take the smallest maximum that turns the MB-169 case green while SC-002 (`< kick peak x 0.01`) and SC-007 stay green. Record the chosen range and the three measured values. If no range satisfies both MB-169 and SC-002, stop and report the numbers: that is a spec ruling (SC-002 against audibility), not a threshold to move.
   - Keep the -20 dBFS energy limiter (SC-007).
   - If Coupling Delay is still inaudible once coupling is audible, report it. Never remove or hide the parameter without the user's ruling (plan head rule).
   - Manual (MB-259):
     - `MANUAL.md:433`: "Airborne travel time between drums: 0.5-2 ms is 17-69 cm at 345 m/s. It shifts when the sympathetic ring starts and how it lines up with the direct sound. It does not add room size: use a reverb for that." (c = 345 m/s per J. O. Smith, PASP, report section 5.)
     - `:437`: the limiter bounds the coupling level; there is no feedback loop.
     - `:529`: delete the "big room" claim and replace it with the sentence above.
3. **Build and gate.** Add `dsp_systems_tests` to the build. Run `grep -rl "sympathetic_resonance.h" plugins/ dsp/include tests/` and list the consumers in the commit message. Run `node tools/header-consumers.js --tag '[membrum]'` and gate every executable its last line names with `~[long]` appended. Expect `innexus_tests` and the suites that reach `aether_reverb.h` (Seraphis and Vorago include it); if one of them is missing from the list, find out why before trusting the list. Run clang-tidy with `-Target dsp` too.
4. **Acceptance.** All three new cases are green and the SC-002, SC-007 and SC-008 cases are unchanged and green, with the band-energy ratio, the coupling-only RMS relative to dry and the onset shift recorded. Re-running probe `coupling:kick-added-energy` (baseline 0.00 dB RMS change, -60.98 dB coupling-only) and `coupling:30s-16th-kicks` (no runaway, below +1 dB) gives the new numbers. In `param-audibility.csv`, the `globalCoupling` rows (200-202, today 0.00-0.01 dB) and the `couplingDelay` rows (209-211, today "inaudibly weak on all 3 base pads") no longer read inaudible. Innexus and Seraphis suites are green with no golden moved. The kit-quality probe before and after gives the table for every factory kit whose global coupling is above 0. Show the table and stop for the user's ruling.

### T5.3 — Mode Stretch below 1.0 compresses the Membrane partials (or the knob range is relabelled)

**Findings:** MB-179 (low, CONFIRMED).
**Flags:** PRESET-IMPACT. If the user rules for the relabel option (2b), STATE-COMPAT also applies, because the stored normalized value then means a different stretch.
**Prerequisites:** T4.5, T4.16.
**Estimate:** 120 min.

**Ruling before step 2.** This task is in the ruling table ("implement or relabel"). Show the user both options below and the step-1 numbers, and stop until they choose. The plan recommends 2a: `specs/135-membrum-drum-synth.md:177` defines the lower half as "Below = clustered/gong-like", and FR-050 (`specs/137-membrum-phase2-exciters-bodies/spec.md:235`) gives the range as about 0.5 to 2.0.

Read T4.16's commit before starting. T4.16 (MB-201) moves the Membrane's stiffness law out of the shared bank into `MembraneMapper` and passes `stretch = 0` to the bank for the membrane. Build on that law; do not reintroduce the `material * 0.3 + stretchDelta` clamp. If T4.16 was closed without moving the law, the anchors below still hold at commit `4a14376d`:
- `plugins/membrum/src/dsp/bodies/membrane_mapper.h:129-138`, `MembraneMapper::map` step (4): `stretchFromStretchParam = clamp((modeStretch - 0.5) / 1.5, 0, 1)` and `r.stretch = std::clamp(params.material * 0.3f + stretchDelta, 0.0f, 1.0f)`. Below 1.0 this clamps to 0.
- `dsp/include/krate/dsp/processors/modal_resonator_bank.h:756` clamps stretch to [0,1], `:783` sets `B = stretch * stretch * 0.01f`, and `:807` computes `f_w = f_k * std::sqrt(1.0f + B * n_k * n_k)`. The bank can only raise partials.
- The denormalisation is in `plugins/membrum/src/dsp/pad_config_apply.h:123-126` (`0.5f + norm * 1.5f`); the default is `pad_config.h:212` (`modeStretch = 0.333333f`, which is 1.0).

**1. Failing test.** Extend `plugins/membrum/tests/unit/unnatural/test_mode_stretch.cpp` with
`TEST_CASE("UnnaturalZone Mode Stretch -- below 1.0 compresses Membrane partials", "[UnnaturalZone][ModeStretch]")`.
- Build a `Membrum::DrumVoice` with the file's existing `makeBaselineParams()` setup (Membrane body, material 0.5, scatter 0, decaySkew 0). Prepare at 48 kHz, set modeStretch through `unnaturalZone().setModeStretch(...)` (the setter `applyPadConfigToVoice` uses), call `noteOn`, and read `voice.bodyBank().getSharedBank().getModeFrequency(k)`.
- Compute `ratio3(s) = f(3) / f(0)` at s = 0.5, 0.75 and 1.0.
- Assert `ratio3(0.5) <= 0.97 * ratio3(1.0)` (compression of at least 3 %, the threshold MB-179 names), and `ratio3(0.5) < ratio3(0.75) < ratio3(1.0)` (monotone).
- Assert that at s = 1.0 every mode frequency equals the T4.16 reference law for the same material within 1e-4 relative. The neutral point must stay exact.
- Today it fails: the audit measured ratio 2.295 at 0.5 and 2.299 at 1.0, a 0.2 % change (`specs/membrum-audit/probes/probe-results.csv:579-581`).
- Under option 2b the test is written against the new range instead: the knob's lowest position must display "1.00", and every stored value at or below the old 1.0 point must migrate to exactly 1.0 (see 2b).

**2. Fix.**
- **2a (implement, recommended).** In `MembraneMapper::map`, after the T4.16 stiffness step, apply a compression for `params.modeStretch < 1.0f`. Use an exponent law on each mode's ratio to the (0,1) mode:
  - `ratio_k = r.frequencies[k] / r.frequencies[0]`
  - `e = 1 - kMaxCompression * (1 - modeStretch) / 0.5`
  - `r.frequencies[k] = r.frequencies[0] * std::pow(ratio_k, e)`
  - Start with `kMaxCompression = 0.2`, so e = 0.8 at s = 0.5. The (0,1) mode (k = 0) stays fixed. Tune the constant only until the step-1 threshold passes with margin, and keep it named, with a comment that cites FR-050 and spec 135 line 177.
  - Produce the neutral point with a branch: `if (params.modeStretch >= 1.0f)` skips the step entirely. Arithmetic that cancels to 1.0 would not be exact under macOS fast-math (MACOS rule; T3.5 fixed the same class of defect).
  - Keep `pow`'s argument positive with a clamp at a small floor.
  - Leave the upper half (s > 1) exactly as T4.16 left it.
  - The other modal mappers (`plate_mapper.h`, `shell_mapper.h`, `bell_mapper.h`, `noise_body_mapper.h`) have the same `clamp((m - 0.5) / 1.5, 0, 1)` dead lower half. This task does not change them: list them in the commit message as a follow-up for the user.
  - Correct `plugins/membrum/MANUAL.md:346`. It says "> 1.0 is harmonically tighter, < 1.0 is sparser", which inverts the design. Write "< 1.0 clusters (compresses) the overtones, gong-like; > 1.0 spreads them; 1.0 is physical", and say the compression applies to the Membrane body.
- **2b (relabel).** Make the parameter range 1.0..2.0 (norm 0 = 1.0, norm 1 = 2.0) in `pad_config_apply.h:123-126`, every mapper's `stretchFromStretchParam`, the per-pad registration `kPadModeStretch` (`controller.cpp`, default 0.0) and the global proxy. Follow STATE-COMPAT:
  - The version-5 read path maps a stored norm n to `s = 0.5 + 1.5n` and then to `max(s, 1.0) - 1.0`.
  - The generator writes the new norms.
  - Update MANUAL.md:346 to "1.0-2.0".

**3. Build and gate.**
- Add `Membrum` to the build targets.
- `test_mode_stretch.cpp` and the goldens under `plugins/membrum/tests/golden/` must stay green. A modeStretch of 1.0 must not move one sample.
- Option 2b: also regenerate the kits (PRESET-IMPACT) and run the STATE-COMPAT load tests.
- After the MANUAL.md edit, run `node --test "tools/site/tests/*.test.js"`.

**4. Acceptance.**
- Step 1 passes: `ratio3(0.5) / ratio3(1.0) <= 0.97`, against the 0.998 measured today.
- PRESET-IMPACT: the kit-quality probe before and after shows unchanged numbers on all 640 pad rows. Grep `tools/membrum_preset_generator.cpp` for `modeStretch` values below 0.3333 and confirm that none is on a Membrane pad (at `4a14376d` the only ones are Plate toms, `:3594`, and String pads, `:5981`). `plugins/membrum/src/dsp/default_kit.h` sets no Membrane modeStretch below 1.0 (`:231` and `:348` are NoiseBody pads at 0.6).
- If a row moves, stop and show the table.

### T5.4 — Pitch envelope and Size: Size still sets the pitch or the editor shows that it does not, and raising Time alone no longer drops a pad to 20 Hz

**Findings:** MB-083 (low, CONFIRMED).
**Flags:** UI. If the user rules for option 2A, STATE-COMPAT and PRESET-IMPACT also apply: stored Start, End and Mid values change meaning. CROSS-PLUGIN IMPACT applies as well, because the Hz read-out of `plugins/shared/src/ui/pitch_envelope_display.h` must change.
**Prerequisites:** T1.11.
**Estimate:** 150 min.

**Ruling before step 2.** This task is in the ruling table. Show the user options 2A and 2B and the step-1 numbers, then stop. The spec makes the absolute-Hz envelope a design (`specs/137-membrum-phase2-exciters-bodies/spec.md:274-275`; `plugins/membrum/MANUAL.md:327`), so 2B keeps the documented model. The 20 Hz defaults, however, depart from the spec under either option: the spec gives Start 160 Hz and End 50 Hz (`spec.md:274-275`, `data-model.md:783-784`). Both options fix the defaults.

The mechanism, at `4a14376d`:
- `DrumVoice::noteOn` sets `naturalFundamentalHz_ = Bodies::naturalFundamentalHz(..., size_)` (`plugins/membrum/src/dsp/drum_voice.h:209-210`) and caches the mapper result at pitch 0 (`:220-221`).
- `DrumVoice::updateModalFundamentalOnBank` scales the cached frequencies by `pitchHz / naturalFundamentalHz_` (`:1613-1617`). Size therefore cancels whenever the envelope is active.
- `ToneShaper::processPitchEnvelope` (`plugins/membrum/src/dsp/tone_shaper.h:380-390`) returns the natural f0 only while `pitchEnvTimeMs_ == 0`. `isPitchEnvActive` is `pitchEnvTimeMs_ > 0` (`:310-313`).
- Start and End denormalise to `20 * 100^n` Hz (`pad_config_apply.h:101-106`).
- The defaults are 0 = 20 Hz: `pad_config.h:202-203`, the per-pad rows `controller.cpp:181-182`, and the proxies `controller.cpp:347-348` (Start 0.070721, about 27.7 Hz; End 0.0). `plugin_ids.h:75-76` claims "default 160" and "default 50".

T1.11 changed how the retune reaches the bank; re-read `updateModalFundamentalOnBank` and its callers before you edit.

**1. Failing test.** Extend `plugins/membrum/tests/unit/voice_pool/test_per_pad_pitch_env.cpp` (use its existing VoicePool fixture and `voice_pool_test_helpers.h`) with two cases.
- `TEST_CASE("Per-pad pitch env: raising Time alone does not retune a default pad to 20 Hz", "[voice_pool][pitch_env]")`.
  - Take a `PadConfig{}` Membrane pad with only `tsPitchEnvTime = 0.2` (100 ms). Play note 36 and render 400 ms in 256-sample blocks.
  - Read the settled body fundamental as `getModeFrequency(0)` of the sounding voice's shared bank (through `DrumVoice::bodyBank()`).
  - Assert it is at least 40 Hz. Today it is 20 Hz.
  - Under 2A it must also equal the Size-derived natural f0 within 1 %. Under 2B it must equal the new default End (50 Hz) within 1 %.
- `TEST_CASE("Per-pad pitch env: Size reaches the settled pitch while the envelope is active", "[voice_pool][pitch_env]")`.
  - Use a pad with an active envelope (Time 0.2). Under 2A use Start +12 st and End 0 st; under 2B use Start 160 Hz and End 50 Hz.
  - Render it at Size 0.3 and at Size 0.8 and read the settled `getModeFrequency(0)`.
  - Under 2A, assert the two pitches differ by the Size law (`500 * 0.1^size` for Membrane, within 1 %). Today they are identical.
  - Under 2B this case is replaced by a controller case in `plugins/membrum/tests/unit/controller/test_pitch_env_body_gate.cpp`, written in the same style: `TEST_CASE("Size pitch indicator: active pitch envelope marks Size as not setting pitch", "[pitch_env_gate]")`. It sets `kToneShaperPitchEnvTimeId` to 0 and then to 0.2 and asserts a new `Controller::isSizePitchOverriddenForTest()` returns false and then true. Add a null-tolerant section like `:92-110`.

**2. Fix.**
- **Both options: defaults.**
  - Set the PitchEnv Start and End defaults to the spec's 160 Hz and 50 Hz: norm `log(160/20)/log(100) = 0.451545` and `log(50/20)/log(100) = 0.198970`.
  - Apply them in `PadConfig` (`pad_config.h:202-203`), the two per-pad rows (`controller.cpp:181-182`) and the two proxy rows (`:347-348`), so all four agree.
  - Make `plugin_ids.h:75-76` match.
  - Leave the other proxy-default drift to T6.3 (MB-079).
  - No stored state changes: every blob stores explicit values, and pads with Time 0 ignore Start and End.
- **2A (relative envelope).** Store Start, End and Mid as semitone offsets from the pad's Size-derived natural f0: norm 0..1 maps to -48..+48 st, and 0.5 is 0 st.
  - Add `ToneShaper::setPitchEnvStartSemitones` / `setPitchEnvEndSemitones` / `setPitchEnvMidSemitones`. Convert them to Hz inside `ToneShaper` when `setNaturalFundamentalHz` is called at note-on (`drum_voice.h:211`), before the `PitchSegmentEnvelope` LUTs are built. Keep the 20..2000 Hz clamp.
  - `pad_config_apply.h:101-120` calls the semitone setters.
  - The new defaults are Start 0.5 and End 0.5 (0 st), so Time alone does nothing until Start moves. This replaces the 160/50 defaults above.
  - STATE-COMPAT: the version-5 read path converts a stored absolute norm n to `st = 12 * log2(20 * 100^n / f0(body, size))`, clamped to ±48. Apply it to both blob kinds and to both the processor and controller paths, and add the version-5 migration test.
  - Re-express `default_kit.h:446-481` (the tom pass: End becomes 0 st and Start the glide percentage in st), the kick at `:59-60`, the generator's `toLogNorm(...)` pitch values, and the Punch macro's pitch-envelope delta (`processor/macro_mapper.cpp`, `tsPitchEnvStart` near `:185`) in semitones.
  - Change the display units in `controller.cpp` (`:829`, the Hz formatting of 216/217) and in `pitch_envelope_display.h` (CROSS-PLUGIN IMPACT, even though Membrum is its only consumer today: grep confirms it).
  - Update `MANUAL.md:327-332` ("Start (Hz)" becomes "Start (st from the drum's pitch)").
- **2B (absolute Hz, the editor shows it).** Keep the absolute model.
  - Add `Controller::updateSizePitchIndicator()` to `controller/controller_view_sync.cpp`, next to `updatePitchEnvControlsEnabled` (`:174-192`). Call it from the same refresh path (a new `kRefresh...` flag in `applyViewRefresh`, `:117-124`) whenever `kToneShaperPitchEnvTimeId` or the selected pad changes.
  - When the selected pad's PitchEnv Time is above 0, set both Size knobs (`editor.uidesc:151` and `:233`) to alpha 0.6. **Keep them mouse-enabled**: Size still sets decay and the strike gain, and an interaction is never removed. Change both "Size" labels (`:150`, `:234`) to "Size (decay)" with the tooltip "Pitch comes from the Pitch Envelope while its Time is above 0".
  - Cache the four views in `verifyView` by control tag and exact title "Size". Do not match the "Body Size" macro label at `:140`.
  - Add `isSizePitchOverriddenForTest()`, as `isMembraneBodySelectedForTest` does at `:160`.
  - Update `MANUAL.md:327` to say that Size does not set the pitch while the envelope runs.

**3. Build and gate.**
- Add `Membrum` to the build targets.
- UI flag: run `node tools/uidesc-inventory.js plugins/membrum/resources/editor.uidesc --check`, `node tools/lint-plugin-editor-baseline.js` and `node tools/gen-screenshots.js membrum`. After the MANUAL.md edit, run `node --test "tools/site/tests/*.test.js"`.
- Option 2A also needs: the PRESET-IMPACT kit regeneration and probe table, the STATE-COMPAT version-5 migration test, `node tools/header-consumers.js --tag '[membrum]'` for `pitch_envelope_display.h`, and `-Target shared` clang-tidy.

**4. Acceptance.**
- A Time-only edit on a default pad settles at 50 Hz (2B) or at the pad's natural f0 (2A), not 20 Hz.
- Under 2A, Size 0.3 against 0.8 moves the settled pitch by the Size law (about 3.2x for Membrane).
- Under 2B, the controller case passes and the screenshots show the Size labels unchanged in the default state (Time 0).
- Under 2A, the kit-quality probe shows every factory pad with an active envelope (146 pads, audit kit scan) at its old settled pitch within 1 cent, because the migration preserves pitch. The 7 pads whose End is more than an octave from their Size pitch are listed by name in the commit message.

### T5.5 — Continuous per-pad edits reach a sounding voice (or the manual and the UI say they apply per hit)

**Findings:** MB-076 (low, PLAUSIBLE).
**Flags:** VERIFY-FIRST.
**Prerequisites:** none.
**Estimate:** 150 min.

**Ruling before step 2.** This task is in the ruling table. The skeptics showed that latching at note-on is the phase specs' design (`specs/139-membrum-phase4-pads/spec.md:182`, `:353`, `:369`, `:372`; `research.md:89-93`). Only `MANUAL.md:227` (exciter type only) tells the user, and pan is the one per-pad field that acts live. Show the user the step-1 result and the two options, then stop.

The mechanism, at `4a14376d`:
- `VoicePool::setPadConfigField` (`plugins/membrum/src/voice_pool/voice_pool.cpp:605-699`) writes only `padConfigs_[pad]`. The exception is `chokeGroup`, which also updates the choke table, read only at note-on.
- A `PadConfig` reaches a voice only through `VoicePool::applyPadConfigToSlot` (`:769-786`), and that is called only from the note-on path (`:232`).
- Pan is read live by `panGainsForNote` (`:744-767`), and the output bus in `processBlock` (`:489`, `:543`).
- `DrumVoice` has live setters that are never called on a sounding voice: `setLevel` (`drum_voice.h:1126`), the `ToneShaper` setters `setFilterCutoff` / `setFilterResonance` / `setFilterEnvAmount` / `setDriveAmount` / `setFoldAmount` (`tone_shaper.h:151-217`), and `setNoiseLayerMix` (`drum_voice.h:1149`).

**1. Failing test (the verification).** New file `plugins/membrum/tests/unit/voice_pool/test_live_param_while_ringing.cpp`; add it to `membrum_tests`.
`TEST_CASE("Live per-pad edit: filter cutoff automation darkens a ringing voice", "[voice_pool][automation]")`.
- Drive a real `Membrum::Processor` at 48 kHz in 256-sample blocks. Use `Krate::Test::EventList` (`vst_event_list.h`) and `Krate::Test::ParameterChanges` (`vst_param_changes.h`).
- Play note 49 on the fresh default kit (crash, decay about 1 s).
- At 200 ms, send `padParamId(13, kPadTSFilterCutoff)` = 0.2 (from the default 1.0).
- Compute the spectral centroid of 300-600 ms with `Krate::Test::extractAudioFeatures` (`audio_features.h`) and compare it with the same render without the change.
- Assert the automated render's centroid is at least 30 % lower.
- Today the two renders are identical. The audit probe found that only pan (offset 64) changes a ringing voice: probe `automation:mid-note`, and `param-audibility.csv` "live-while-ringing".
- If the case passes on current code, MB-076 is refuted: keep the test, record "refuted" and stop.

**2. Fix.**
- **(a) implement.** Add `VoicePool::forwardLiveField(int padIndex, int offset)`, called at the end of `setPadConfigField` for these offsets only: `kPadLevel`, `kPadTSFilterCutoff`, `kPadTSFilterResonance`, `kPadTSFilterEnvAmount`, `kPadTSDriveAmount`, `kPadTSFoldAmount`, `kPadNoiseLayerMix`.
  - For every main slot whose `meta_[slot].originatingNote - kFirstDrumNote == padIndex` and whose voice is active, apply that one field with the same denormalisation `applyPadConfigToVoice` uses (`pad_config_apply.h:30-95`).
  - Move each conversion into a small inline helper in `pad_config_apply.h`. `applyPadConfigToVoice` and `forwardLiveField` then share one formula: never two copies.
  - Releasing (shadow) slots stay latched.
  - Check that each setter takes effect on an active voice:
    - `level_` is read per sample.
    - `noiseLayerParams_` must be confirmed: Grep for where `NoiseLayer` consumes it. If only at note-on, push the mix into the running `NoiseLayer` as well.
    - For the SVF cutoff, confirm the `ToneShaper` cutoff update path (`cutoffUpdateCounter_`) smooths a step.
  - Smooth `level` with a one-pole of about 5 ms in `DrumVoice` so a step cannot click.
  - Body, exciter, damping, pitch-envelope and morph fields stay latched at the strike. Document that split in `MANUAL.md` next to `:227`.
  - No allocation, no lock: `setPadConfigField` runs inside `processParameterChanges` on the audio thread.
- **(b) document.**
  - Add a "When edits take effect" section to `MANUAL.md` after `:227`. Per-pad parameters apply from the next hit; pan and output bus act on a ringing voice; master gain, global coupling and coupling delay act live.
  - Add one `CTextLabel` to each selected-pad template in `editor.uidesc` (two views, hand edit) reading "Edits apply from the next hit", with the same text as a tooltip.
  - Rewrite the step-1 case as the pinned behaviour, renamed `"Live per-pad edit: filter cutoff latches until the next hit"`. The centroid inside the ringing note is unchanged within 2 %, and a second hit after the change is at least 30 % darker. Under (b) MB-076's request is met by documentation, so this is not a weakened assertion. State it in the commit message.

**3. Build and gate.**
- Add `Membrum` to the build targets.
- Option (a): add a click check to the new file. Ramp `kPadLevel` 0.8 to 0.2 at 200 ms on the ringing crash and run `Krate::DSP::TestUtils::ClickDetector` (`artifact_detection.h`) over 150-300 ms: zero detections.
- Run `allocation_detector.h` around the `process()` call that carries the change.
- Option (b): run the UI-flag commands: `uidesc-inventory.js --check`, `lint-plugin-editor-baseline.js` and `gen-screenshots.js membrum`. Run `node --test "tools/site/tests/*.test.js"` after the MANUAL.md edit.

**4. Acceptance.**
- Option (a): the centroid drop of at least 30 % inside the same note (0 % today), zero clicks and zero allocations. The kit-quality probe is unchanged: no factory kit automates.
- Option (b): the pinned-latch case passes, the manual section and the label are present, and the screenshots are refreshed.

### T5.6 — Generator pad defaults come from PadConfig; the generator defaults and comment sites the prior audit left behind are fixed

**Findings:** MB-156 (low, CONFIRMED). It repeats prior-audit Findings 9, 10, 17 and 18 and L-1/H-1 (`plugins/membrum/docs/correctness-audit.md`, `AUDIT-signal-path-2026-06-07.md`), each fixed at some sites and not at others.
**Flags:** PRESET-IMPACT.
**Prerequisites:** T1.22, T1.23.
**Estimate:** 120 min.

This is the first kit-data task. The kit-data gate (head, "How the phases are cut") applies: every PRESET-IMPACT task of phases 1 to 4 and T5.1 to T5.5 is committed, refuted or ruled out before this task starts. This task changes no pad's sound. It re-encodes the kits and removes the generator's private defaults.

The defects, at `4a14376d`:
- `tools/membrum_preset_generator.cpp:105`: the `Pad` struct has `double fmRatio = 0.5` (2.5x). `PadConfig` (`plugins/membrum/src/dsp/pad_config.h:234`) and the registration (`controller.cpp:200`) use 0.133333 (1.4x).
- The `Pad` struct (`:78-160`) re-declares every default by hand. Besides fmRatio it differs from `PadConfig` in `tsPitchEnvCurve` (0.15 against 0.0, `:93`; 428 of 459 factory pads rely on 0.15) and `bodyDampingB1/B3` (0.40 against the sentinel, `:131-136`; T1.23 changed this encoding), and in `tsPitchEnvStart/End` if T5.4 moved the `PadConfig` defaults.
- Stale comments:
  - `pad_config.h:274-279`: b3 "1 -> 8e-5 s * rad^-2". The mapper computes `law.b3 = n * 1.0e-3f` (`membrane_mapper.h:77`).
  - `controller.cpp:220-226`: "b3 norm 0.5 -> 4e-5 s" (the real value is 5e-4), and "only kicks in once the host/user actively writes a value", which the controller-built preset save contradicts.
  - `plugin_ids.h:66`: Friction Pressure "default 0.3". It is 0.0.
  - `plugin_ids.h:104`: kChokeGroupId "RangeParameter stepped [0,8]". It is a StringListParameter, `controller.cpp:484-488`.
  - `drum_voice.h:247-262`: a 1/sqrt(N) pre-scale and a "Downstream `softClip(shaped * env * level)`". Neither exists: the output gain is `kBodyHeadroom` over the realised strike peak (`:272-293`), and the voice ends in `Krate::DSP::hardClip(shaped * env * level)`.
- T1.23 may already have rewritten the damping comments (`pad_config.h:274-283`, `controller.cpp:220-228`). Correct whatever text is there when you reach this task against the code as it stands then.

**1. Failing test.**
- First a pure move, with no behaviour change. Move the generator's `ExciterType` and `BodyModelType` enums, `FilterType` and `struct Pad` (`membrum_preset_generator.cpp:47-160`) into a new header, `tools/membrum_preset_generator_pad.h`, inside `namespace MembrumPresetGen`.
  - The generator includes it and adds `using namespace MembrumPresetGen;` after its includes.
  - ODR: Grep shows `struct Pad` exists only in the generator.
  - Rebuild `membrum_preset_generator` and run `generate_membrum_presets`. `git status` must show no `.vstpreset` change.
- Add `target_include_directories(membrum_tests PRIVATE ${CMAKE_SOURCE_DIR}/tools)` in `plugins/membrum/tests/CMakeLists.txt`. Create `plugins/membrum/tests/unit/preset/test_generator_pad_defaults.cpp`; add it to `membrum_tests`.
- `TEST_CASE("Generator Pad defaults equal PadConfig defaults except the documented authoring exceptions", "[preset][generator]")`.
  - Compare every field of `MembrumPresetGen::Pad{}` with the matching `Membrum::PadConfig{}` field (`Approx(...).margin(1e-6)`; enums by value).
  - Skip only the fields named in a `kAuthoringExceptions` list in the test, each with its reason: `tsPitchEnvCurve` (0.15, the historical exponential drop the factory pads are voiced with; the registration drift is T6.3's), and `bodyDampingB1` / `bodyDampingB3` only if T1.23 left the generator writing explicit values in place of `PadConfig`'s default.
  - Also assert that the list has exactly those entries, so a new exception needs an edit here.
  - Today it fails on `fmRatio` (0.5 against 0.133333).

**2. Fix.**
- Give `membrum_preset_generator` the plugin source include path (root `CMakeLists.txt:718-724`: `target_include_directories(membrum_preset_generator PRIVATE ${CMAKE_SOURCE_DIR}/plugins/membrum/src)`). `pad_config.h` needs only `exciter_type.h`, `body_model_type.h`, `<array>` and `<cstdint>`.
- In `membrum_preset_generator_pad.h`, include `dsp/pad_config.h`, define `inline constexpr Membrum::PadConfig kPadDefaults{}` (or `inline const` if `PadConfig` is not a literal type), and initialise every `Pad` member from it (`double fmRatio = kPadDefaults.fmRatio;` and so on). Only the exception fields keep a literal, each with a comment naming the reason.
- Keep every factory pad's sound. Each field whose default changes (`fmRatio`, plus `tsPitchEnvStart/End` if T5.4 moved them) is audible on some pads:
  - `fmRatio` acts only on FMImpulse pads (`:895`, `:1333`, ...).
  - The pitch-envelope endpoints act only on pads with `tsPitchEnvTime > 0`.
  - Find every pad where the field is audible and that does not assign it. Write the old literal on that pad explicitly (for example `pads[i].fmRatio = 0.5; // was the generator default (2.5x)`).
  - The audit's skeptics found that every FMImpulse pad sets fmRatio. Confirm it with Grep, and list in the commit message the pads you had to touch (expected: none for fmRatio).
- Fix the comment sites listed above so each states what the code does now.
- Regenerate the kits.

**3. Build and gate.**
- Build `membrum_preset_generator` and `membrum_tests`; add `Membrum` (the comment edits are in `plugins/membrum/src/`, and the build installs the kits).
- PRESET-IMPACT: run `generate_membrum_presets`, keep 20 kits with 5 per category, and check that `getState(setState(kit))` is idempotent for all 20.
- pluginval may be skipped only if `git diff --stat plugins/membrum/src` shows comment lines only. Otherwise run it.

**4. Acceptance.**
- The new case passes.
- The kit-quality probe before and after shows identical numbers on all 640 pad rows of `kit-pads.csv` and an unchanged `kit-pairs.csv`. Any moved row stops the task.
- `git diff` of the `.vstpreset` files is limited to the slots of fields that changed default on pads where they are inert.

### T5.7 — Experimental FX Kit: three distinct metal hats (42 / 44 / 46)

**Findings:** MB-186 (medium, CONFIRMED).
**Flags:** PRESET-IMPACT.
**Prerequisites:** T1.23, T5.6.
**Estimate:** 90 min.

The data, at `4a14376d`, in `experimentalKit()` (`tools/membrum_preset_generator.cpp:1380`):
- Pad 6 (note 42) is Bell/NoiseBurst, decay 0.08, with `bodyDampingB3 = 0.0` (`:1451-1461`). It inherits the struct default `bodyDampingB1 = 0.40`, so both overrides are set and Decay does nothing to the modal ring.
- `pads[8] = pads[6]; pads[8].decay = 0.04;` (`:1463-1464`).
- `pads[10] = pads[6]; pads[10].decay = 0.60; pads[10].noiseLayerDecay = 0.55;` (`:1466-1467`).

T1.23 changed how an override and Decay interact. Re-read the generator and `dampingLawFromParams` (`membrane_mapper.h:64-80`) as T1.23 left them, and use the encoding T1.23 chose for every damping value below.

**This task creates the shared kit-render test helper** that T5.8 to T5.13 use. If an earlier task already created one (Grep `plugins/membrum/tests` for `bandDist`), reuse it instead. Create `plugins/membrum/tests/unit/preset/kit_sameness_helpers.h` (namespace `Membrum::KitTest`) by porting from `specs/membrum-audit/probes/audit_quality_probes.cpp.txt`:
- the rig and renderer (`:65-218`: a real `Membrum::Processor` at 48 kHz, block 256, 16 output buses, note events through `vst_event_list.h`);
- the feature extractor (`:223-392`: `Feat` with peak dBFS, rms500 dBFS, decay to -60 dB in 10 ms frames, the 200 ms centroid and the 24 log bands from 40 Hz to 16 kHz normalised to the loudest; `bandDist`; `ratioHiLo`);
- the factory-kit loader (`:456-510`: read `MEMBRUM_RESOURCES_DIR/presets/Kit Presets/<cat>/<name>.vstpreset`, then `setState`).

Expose:
- `Feat renderFactoryPad(category, kitName, note, seconds)`
- `Feat renderDefaultKitPad(note, seconds)`: a fresh `Processor`, no `setState`.
- `bool isNearDuplicate(const Feat&, const Feat&)`, with the audit's rule as named constants: bandDist < 3 dB AND centroid ratio <= 1.10 AND decay ratio <= 1.20.

Velocity is 100/127. Drop the probe's CSV and `PROBE` printing; print the three numbers with `INFO`. Phase 8 reuses this header when it promotes the probes.

**1. Failing test.** New file `plugins/membrum/tests/unit/preset/test_factory_kit_sameness.cpp`; add it to `membrum_tests`.
`TEST_CASE("Factory kit sameness: Experimental FX Kit metal hats 42/44/46 are distinct", "[preset][kit_sameness]")`.
- Render notes 42, 44 and 46 for 2 s each.
- Assert that no pair among the three is a near-duplicate.
- Assert `decayS(46) >= 3 * decayS(42)`.
- Today: 42/44 = 0.00 dB / 1.000 / 1.000; 42/46 and 44/46 = 1.02 / 1.046 / 1.000; all three decay 0.29 s, peak -18.73 dBFS, centroid 5428 / 5428 / 5678 Hz (`kit-pairs.csv`, `kit-pads.csv:552-556`).

**2. Fix.** In `experimentalKit()` only:
- Give the three hats distinct damping and noise tails. MB-186 suggests b1 norms of about 0.75 (closed), 0.85 (pedal) and 0.2 (open) in the old override encoding; translate them to T1.23's encoding.
- Distinct `noiseLayerDecay` values: closed 0.12, pedal 0.08, open 0.60 as a start.
- Make the pedal darker and quieter: `noiseLayerCutoff` 0.80 against 0.90, `level` 0.62.
- Keep Bell/NoiseBurst, choke group 1, the morph settings and the pans. Write each value on its own pad; do not copy and then patch one field.
- Update the comment at `:1450`.

**3. Build and gate.**
- PRESET-IMPACT: regenerate the kits and build `Membrum` to install them.
- Run the kit-quality probe before and after, and put the table in the commit message: peak, rms500, decay, centroid and limiter margin for notes 42, 44 and 46, plus every pair of the kit that enters or leaves `kit-pairs.csv`'s near-duplicate flag.
- Show the table to the user and stop for the ruling before the commit.

**4. Acceptance.**
- The new case passes.
- The kit's near-duplicate count goes from 3 (of 496) to 0.
- No other pad's row moves.
- The three hats stay within the bus limiter margin (no new `limiterMarginDb >= 0` row).

### T5.8 — Default kit: distinct hi-hats, crash versus ride, and per-GM-pad recipes instead of 12 sounds on 32 pads

**Findings:** MB-062 (medium, PLAUSIBLE), MB-086 (low, CONFIRMED).
**Flags:** PRESET-IMPACT, VERIFY-FIRST.
**Prerequisites:** T5.6.
**Estimate:** 240 min.

At `4a14376d`, `DefaultKit::apply` (`plugins/membrum/src/dsp/default_kit.h:398-490`) gives 32 pads 7 templates through `kSpecs` (`:407-440`). The only per-pad passes are the tom pass (`:446-481`) and the hat choke group (`:487-489`). The result is 12 distinct sounds:
- the 3 hats are identical (`:414/:416/:418`);
- the 7 cymbals are identical (`:421-431`: crash, ride, china, ride bell, splash, crash 2, ride 2);
- the 12 Perc pads are identical (side stick, tambourine, cowbell, vibraslap, bongos, congas, timbales, agogo);
- the 2 snares are identical.

`Processor::initialize` calls it (`processor.cpp:111`), so this is every new instance's sound. The skeptic on MB-062 pointed out that FR-030/FR-031 (`specs/139-membrum-phase4-pads/spec.md:254-264`) define one parameter set per template. Departing from that is the user's call: this task stops for the ruling. MB-086's factory-kit half (never-varied parameter groups across the 459 factory pads) is not reworked here. Its suggested factory-kit test ("no two enabled pads in a kit are identical") is added by T5.11, the task that removes the last identical pair (Tabla 53/66). A red case would block this task's `GATE GREEN`.

**1. Failing test (the verification for MB-062).** New file `plugins/membrum/tests/unit/processor/test_default_kit_distinct.cpp`; add it to `membrum_tests`. Use `kit_sameness_helpers.h` (T5.7). Three cases:
- `TEST_CASE("DefaultKit: every pad has its own PadConfig", "[default_kit]")`.
  - Apply `DefaultKit::apply` and compare the pads pairwise with `State::toPadSnapshot`, field by field. Ignore `chokeGroup`, `outputBus` and `pan` (sound index 56): none of them changes the mono voice.
  - Put the comparison in a helper, `bool sameVoice(const State::PadSnapshot&, const State::PadSnapshot&)`, in `kit_sameness_helpers.h`; T5.11 reuses it.
  - Assert 32 distinct configs. Today there are 12.
- `TEST_CASE("DefaultKit render: open hat rings longer than closed, ride differs from crash", "[default_kit][kit_sameness]")`, using `renderDefaultKitPad`:
  - the RMS over 2 s of note 46 is at least 6 dB above note 42;
  - `decayS(46) >= 3 * decayS(42)`;
  - 42 and 44 are not near-duplicates;
  - 49 and 51 differ by at least 20 % in centroid or in decay.
  - Today 42 and 46 both give peak 0.1534 and RMS 0.0035, and 49 and 51 both give peak 0.2766 and RMS 0.0232 (`tests/unit/processor/test_output_level_measure.cpp:158-179`, hidden `[.measure][level]`).
- `TEST_CASE("DefaultKit render: no two pads of one PadCategory are near-duplicates", "[default_kit][kit_sameness]")`.
  - Render all 32 notes for 2 s (4 s for 49, 51, 52, 53, 55, 57 and 59).
  - Assert zero same-category near-duplicates.

VERIFY-FIRST applies to MB-062 only. If the render case passes on current code, MB-062 is refuted and its part of the fix ends. MB-086 is CONFIRMED, and its config case continues either way.

**2. Fix.**
- In `default_kit.h`, keep `applyTemplate` and the archetype values that `test_default_kit.cpp` pins: pads 0, 1, 2, 6 and 13 and the tom template (`:78-297` of that test).
- Add a per-GM-pad recipe pass after the tom pass, written like it: a `constexpr` table indexed by pad, setting only the fields that differ from the template.
- Starting values, to be tuned against step 1 only:
  - **40 Electric Snare:** size 0.35, decay 0.10, `noiseLayerCutoff` 0.88, `noiseLayerColor` 0.85, `clickLayerBrightness` 0.85.
  - **44 Pedal Hat:** level 0.65, `noiseLayerCutoff` 0.78, `noiseLayerDecay` 0.08, `clickLayerBrightness` 0.6, a higher b1 than 42.
  - **46 Open Hat:** decay 0.5, `noiseLayerDecay` 0.65, a lower b1 than 42.
  - **51 / 59 Ride 1 / 2:** Impulse exciter (stick ping), size 0.22 / 0.26, `nonlinearCoupling` 0.10 (little bloom), `noiseLayerGain` 1.5, a longer low-mode ring than the crash.
  - **52 China:** size 0.38, modeStretch 0.75, `noiseLayerColor` 0.70.
  - **53 Ride Bell:** Bell body, Impulse, size 0.25, material 0.9.
  - **55 Splash:** size 0.18, decay 0.45, `noiseLayerDecay` 0.70.
  - **57 Crash 2:** size 0.40, `noiseLayerCutoff` 0.76.
  - **54 Tambourine:** NoiseBurst/NoiseBody, size 0.12, material 0.9, `noiseLayerMix` 0.8, `noiseLayerCutoff` 0.85.
  - **56 Cowbell:** Bell, Impulse, size 0.35, material 0.8, decay 0.25.
  - **67 Agogo:** Bell, size 0.28.
  - **58 Vibraslap:** NoiseBody, `noiseLayerDecay` 0.6, `nonlinearCoupling` 0.3.
  - **60 / 61 Bongos:** Membrane, Impulse, size 0.30 / 0.36, strikePosition 0.6.
  - **62 / 63 / 64 Congas:** Membrane, size 0.45 / 0.42 / 0.52, decay 0.12 (mute) / 0.40 / 0.45.
  - **65 / 66 Timbales:** Membrane, size 0.33 / 0.40, material 0.75, `clickLayerMix` 0.8.
- Express damping in T1.23's encoding.
- Keep the hats in choke group 1 and every other pad in group 0 (`test_default_kit.cpp:169-190`).
- Update the header comment (`:6-9`) and the GM table comment (`:362-397`) to say templates plus per-pad recipes.
- Do not edit `specs/139-.../spec.md`. If the user rules for the change, list FR-031 as superseded in the commit message.

**3. Build and gate.**
- Add `Membrum` to the build targets. The factory kits are not touched; the default kit is engine code.
- The existing tests that use `DefaultKit` must stay green: `test_default_kit.cpp`, `test_crash_acceptance.cpp`, `test_tom_body_depth.cpp`, `test_snare_body_balance.cpp`, `test_coupling_integration.cpp`, `test_wire_coupling.cpp` and `test_per_mode_damping.cpp`.
- `plugins/membrum/tests/golden/phase1_default.bin` must not move (pad 0 is unchanged). If it moves, stop.
- PRESET-IMPACT measurement: the factory-kit probe does not cover the default kit. Give the before/after table from step 1's printed values (peak, rms500, decay and centroid for every changed pad) plus the hidden `[.measure][level]` case, then stop for the user's ruling.
- Pad categories change for several pads (`pad_category.h`), and so do coupling roles. Rerun `test_coupling_integration.cpp` and say in the commit message which pads changed category.

**4. Acceptance.**
- 32 distinct default-kit configs (12 today).
- Note 46 RMS over 2 s at least 6 dB above note 42 (0 dB today), with 3x the decay.
- 49 against 51: 20 % or more in centroid or decay (identical today).
- Zero same-category near-duplicates in the default kit.

### T5.9 — Closed and pedal hi-hat differ in the 808, 909, Jazz Brushes, Vintage Wood and LinnDrum CR-78 kits

**Findings:** MB-187 (low, CONFIRMED), MB-188 (low, PLAUSIBLE), MB-189 (low, CONFIRMED), MB-190 (low, CONFIRMED), MB-191 (low, CONFIRMED).
**Flags:** PRESET-IMPACT, VERIFY-FIRST (MB-188 only).
**Prerequisites:** T5.6.
**Estimate:** 180 min.

In every kit the pedal hat is `pads[8] = pads[6]` plus one or two small decay or damping edits. Under the explicit b1/b3 overrides those edits barely move the sound (Decay before T1.23; read T1.23's commit for what Decay does now). The kit builders, at `4a14376d`, are all in `tools/membrum_preset_generator.cpp`:
- 808, `electronicKit()`: `:1231-1253`. The pedal changes material, size, decay, level, `noiseLayerDecay`, b1 and skew by small steps.
- 909, `nineOhNineKit()`: `:3006-3022`. Decay, `noiseLayerDecay` and b1.
- Jazz Brushes, `jazzBrushesKit()`: `:1774-1789`. Decay, `noiseLayerDecay` and b1.
- Vintage Wood, `vintageWoodKit()`: `:2371-2385`. Decay and `noiseLayerDecay` only.
- LinnDrum CR-78, `linnDrumKit()`: `:3222-3234`. Decay and `noiseLayerDecay` only.

MB-188 is PLAUSIBLE. A real TR-909 has no separate pedal voice. The kit design records make the 909 pedal a decay-only variant on purpose: `plugins/membrum/preset-retune/kits/07-nineOhNineKit.md:70` and the 909 hat archetype file's "Variants" section.

**1. Failing test.** Extend `test_factory_kit_sameness.cpp` (T5.7) with one case per kit:
`TEST_CASE("Factory kit sameness: <kit name> pedal hat 44 differs from closed hat 42", "[preset][kit_sameness]")`.
- Render notes 42, 44 and 46 for 2 s each.
- Assert `bandDist(42, 44) >= 3.0` dB (the threshold the findings name).
- Assert the pedal is not longer than the closed hat (`decayS(44) <= decayS(42) * 1.05`) and not louder (`peakDb(44) <= peakDb(42) + 0.5`).
- Assert 44 and 46 are not near-duplicates.
- Baselines for 42 against 44 (bandDist / centroid ratio / decay ratio): 808 1.60 / 1.006 / 1.167; 909 0.28 / 1.002 / 1.167; Jazz Brushes 0.55 / 1.001 / 1.143; Vintage Wood 0.50 / 1.002 / 1.000; LinnDrum CR-78 0.29 / 1.002 / 1.000 (`quality-results.txt:74, :85, :184, :272, :206`).
- The 909 case is the verification of MB-188. If it passes on current code, MB-188 is refuted.

**2. Fix.** For each kit, give the pedal hat its own "chick", a darker and shorter closed-foot sound, per the pedal-hat archetype `plugins/membrum/preset-retune/archetypes/pedal-hi-hat-acoustic-foot-closed-chick.md`:
- `noiseLayerCutoff` about 0.12 below the closed hat;
- `noiseLayerColor` about 0.15 darker;
- `clickLayerBrightness` lower, where the closed hat has a click;
- level 0.05 to 0.08 lower;
- a clearly higher b1 in T1.23's encoding;
- size 0.02 to 0.04 larger (more low-mode weight).

Write the pedal's values explicitly after the copy and update each kit's hat comment.

The 909 part follows the per-finding VERIFY-FIRST rule. Show the user the 909 numbers. If they rule that the 909 pedal stays a decay-only variant (the authentic voice), drop the 909 case's `bandDist` assertion before the commit, keep its other assertions, and record "MB-188 ruled out by the user". That is the ruling closing a PLAUSIBLE finding, not a relaxed threshold.

**3. Build and gate.**
- PRESET-IMPACT: regenerate, build `Membrum`, then the kit-quality probe before and after.
- The commit-message table covers notes 42, 44 and 46 of all five kits (peak, rms500, decay, centroid, limiter margin) and the near-duplicate flags entering or leaving `kit-pairs.csv`.
- Stop for the user's ruling.

**4. Acceptance.**
- `bandDist(42, 44) >= 3` dB in all five kits (four if MB-188 is ruled out).
- Each kit's near-duplicate count falls by one: 808 1 to 0, 909 1 to 0, Jazz Brushes 1 to 0, Vintage Wood 1 to 0, LinnDrum 1 to 0.
- No new near-duplicate pair anywhere in the five kits.

### T5.10 — Kick pitch sweeps of the 808, 909 and Trap kits follow their references and differ from each other

**Findings:** MB-239 (low, PLAUSIBLE).
**Flags:** PRESET-IMPACT, PHYSICS, VERIFY-FIRST.
**Prerequisites:** T1.11, T5.6.
**Estimate:** 150 min.

At `4a14376d`, in `tools/membrum_preset_generator.cpp`:
- 808 kick, pad 0 of `electronicKit()` (`:1126-1143`): Impulse/Membrane, material 0.15, size 0.90, `toLogNorm(200)` to `toLogNorm(40)` over `tsPitchEnvTime` 0.06 (30 ms), curve 0.15, `tensionModAmt` 0.30, single segment (knee off).
- 909 kick, `nineOhNineKit()` (`:2926-2939`): 180 to 55 Hz over 20 ms.
- Trap "Sub 808", `trapModernKit()` (`:3747-3757`): 250 to 35 Hz over 30 ms, `tensionModAmt` 0.85, modeInject 0.20.
- The envelope holds End after Time (`pitch_segment_envelope.h:101-106`, `tone_shaper.h:380-390`); `pad_config_apply.h:101-120` maps the norms.

The default-kit kick (`default_kit.h:54-61`, 160 to 50 Hz over 20 ms) is a 909-style sweep, which MB-239 does not contradict. It stays as it is, because `test_default_kit.cpp:78` pins it.

The reference is Werner, Abel and Smith, "A Physically-Informed, Circuit-Bendable, Digital Model of the Roland TR-808 Bass Drum Circuit", DAFx-14, pp. 159-166 (report section 5):
- bridged-T centre frequency at rest about 49.5 Hz (Roland's tuning chart: 56 Hz);
- section 8.1: an attack rise of more than an octave lasting about 6 ms, under one period, heard as punch rather than pitch;
- section 8.2 and Fig. 11: a "pitch sigh" from about 56 Hz to about 49.5 Hz over the first 300 ms.

For the 909, Reid ("Synth Secrets: Practical Bass Drum Synthesis", Sound On Sound) supports a large downward sweep, but no peer-reviewed Hz/ms measurement was found. Its numbers stay unsupported, not wrong.

Before ruling, the user should see that the current 808 voicing is a documented producer-style choice: `plugins/membrum/preset-retune/archetypes/808-kick-roland-tr-808-bass-drum.md:8-21` cites the same data and picks a 200 to 40 Hz "boom". MB-239 is PLAUSIBLE for that reason.

**1. Failing test (the verification).** New file `plugins/membrum/tests/unit/preset/test_factory_kick_pitch.cpp`; add it to `membrum_tests`. Use `kit_sameness_helpers.h` (T5.7) for loading and rendering, at 512-sample blocks, as MB-239 asks.
- `TEST_CASE("Factory 808 kick follows the TR-808 bass-drum pitch reference", "[preset][kick_pitch]")`.
  - Low-pass the mono render (second-order, 150 Hz) and measure the instantaneous frequency from the period between successive positive-going zero crossings.
  - Assert the frequency at 50 ms is in [52, 59] Hz and the mean over 250-300 ms is in [48.5, 51.5] Hz.
  - Type the bounds from the source in the test with a citation comment (PHYSICS): they bracket Werner et al.'s 56 Hz and 49.5 Hz.
  - Today the 808 kick rests at 40 Hz, so the second assertion fails. Before T1.11 every kick sounded near 80 Hz at host block sizes (MB-008).
- `TEST_CASE("Factory kicks: 808, 909 and Trap are distinct", "[preset][kick_pitch]")`.
  - Measure the settled pitch (mean over 250-300 ms, same method) of note 36 in the three kits and require pairwise differences of at least 1 semitone.
  - Require no pair to be a near-duplicate by `isNearDuplicate`.
  - Today the configured rests are 40, 55 and 35 Hz (2.3 semitones between Trap and 808). The measured centroids are 275.6, 499.2 and 388.2 Hz (`kit-pads.csv:162, :194, :290`). Record whether this case is already green: if it is, that half of MB-239 is refuted.

**2. Fix.** Only after the user's ruling: either circuit-accurate 808, or keep the producer-style 808 and close the reference half as ruled out.
- **808 kick (`electronicKit()` pad 0):**
  - Enable the knee: `tsPitchEnvKneeEnabled = 1.0`.
  - Start `toLogNorm(110)` (more than an octave above rest).
  - Mid `toLogNorm(56)` (Roland chart) at `tsPitchEnvMidFraction = 0.02` of a total `tsPitchEnvTime = 0.6` (300 ms), so the mid point falls at 6 ms. Curve 1 fast: keep 0.15.
  - End `toLogNorm(49.5)` (Werner et al.'s rest frequency), with `tsPitchEnvCurve2` at an exponential setting (about 0.15).
  - Lower `tensionModAmt` to 0.05 or less so tension does not add its own glide.
  - Raise `airLoading` only if the fundamental stops dominating. Werner et al. model the sustain as one damped pseudo-sinusoid: check that the 2nd partial's band (`Feat::bands`) stays at least 12 dB under the fundamental's.
  - Put a comment next to each number citing the paper, section and figure.
- **909 and Trap:** keep their large sweeps. Change them only if the distinctness case fails, by moving the End pitch or the sweep time (for example 909 End 55 to 60 Hz). Comment that no measurement supports exact 909 numbers.
- Update the 808 archetype file only if the user asks; it is a design record.

**3. Build and gate.**
- PRESET-IMPACT: regenerate the kits, build `Membrum`, then the kit-quality probe before and after. The table covers note 36 of the 808, 909 and Trap kits.
- `test_tom_body_depth.cpp` and the SC-009 kick test (MB-240, rewritten by T1.11) must stay green.
- Stop for the ruling.

**4. Acceptance.**
- The 808 kick reads [52, 59] Hz at 50 ms and [48.5, 51.5] Hz over 250-300 ms (it rests at 40 Hz today).
- The three kicks' settled pitches differ pairwise by at least 1 semitone, and no pair is a near-duplicate.
- The coded 49.5, 56 and 6 ms values carry their citation in the generator and in the test.

### T5.11 — Tabla kit: six near-duplicate pad pairs separated

**Findings:** MB-185 (low, CONFIRMED).
**Flags:** PRESET-IMPACT.
**Prerequisites:** T5.6.
**Estimate:** 120 min.

At `4a14376d`, in `tablaKit()` (`tools/membrum_preset_generator.cpp:4672`):
- pad 30 (note 66) is `pads[30] = pads[17]; pads[30].pan = 0.50;` (`:5079-5080`): a bit-identical voice;
- pad 6 (note 42) is pad 0 with decay and `tensionModAmt` 0.78 to 0.85 (`:4775-4777`);
- pad 23 (note 59) is pad 2 with small decay, strike, tension and skew changes (`:5007-5010`);
- pad 7 (note 43) is pad 3 with edits (`:4780-4783`), and pad 28 (note 64) is pad 12 with edits (`:5063-5069`).

Every one of these keeps explicit b1/b3 (`:4700`, `:4730`). With the envelope active, the absolute pitch envelope sets the pitch, not Size (pad 2: 420 to 380 Hz; tarang pad 17: 360 to about 338 Hz). Read T1.23's and T5.4's commits for what Decay and the pitch envelope do now.

The near-duplicate pairs (bandDist / centroid ratio / decay ratio), from `kit-pairs.csv`:
- 53/66: 0.00 / 1.000 / 1.000
- 36/42: 0.00 / 1.027 / 1.000
- 38/59: 0.25 / 1.004 / 1.115
- 38/53: 0.94 / 1.043 / 1.093
- 38/66: 0.94 / 1.043 / 1.093
- 43/64: 1.21 / 1.056 / 1.000
- 44/48 (different categories): 2.81 / 1.086 / 1.170

That is 7 flagged pairs (`quality-results.txt:249`).

**1. Failing test.** Extend `test_factory_kit_sameness.cpp`:
- `TEST_CASE("Factory kit sameness: Tabla has no near-duplicate pads", "[preset][kit_sameness]")` renders every enabled pad of "Tabla" (Percussive) for 4 s and asserts zero near-duplicate pairs, whatever their category. Baseline: 7.
- `TEST_CASE("Factory kits: no two enabled pads of a kit share one voice", "[preset][kit_sameness]")` is MB-086's suggested generator test. For each of the 20 kits, `State::readKitBlob` the component state and assert that no two enabled pads are equal under `sameVoice` (T5.8's helper: every field except choke group, output bus and pan). Today it fails only on Tabla 53/66.
- If rendering all 32 pads makes the first case slower than about 15 s, compare only the 13 pads of the seven pairs plus their nearest neighbours. Do not tag it `[long]`.

**2. Fix.** In `tablaKit()` only:
- Pad 30: give it its own tarang pitch. Set the start and end Hz to continue the 300 / 360 / 430 row upward (for example 500 to 470 Hz) and size 0.32. Write the fields explicitly instead of copying pad 17. Its comment says "optional spare". If the user prefers it disabled, remove 30 from `k.crafted` instead; that needs the ruling.
- Pad 6 ("Ge" extreme gliss): separate it from pad 0 through pitch, not Decay. Start `toLogNorm(220)`, End 60 Hz and Time 0.30, so the glide is wider and longer, with strikePosition 0.35.
- Pad 23 ("Dhin"): separate it from pads 2 and 17 by its End pitch (for example 340 Hz), `secondaryEnabled` ring (`couplingStrength` 0.45) and noise mix.
- Pads 7 and 28: separate them by size or End pitch and click brightness.
- Pad 44 against 48: move one of them by at least 2 semitones in End pitch.
- Update the kit comment's pad count (`:4676`).

**3. Build and gate.**
- PRESET-IMPACT: regenerate, build `Membrum`, then the kit-quality probe before and after.
- The table covers every changed pad and every near-duplicate pair that disappears or appears.
- Stop for the ruling.

**4. Acceptance.**
- The Tabla near-duplicate count goes from 7 (of 496) to 0.
- No two enabled pads in any factory kit share a voice.
- No other kit's rows move.

### T5.12 — Latin Perc and Hand Drums: clave, castanet and shaker pads separated; congas and bongos no longer bit-identical across the two kits

**Findings:** MB-193 (low, CONFIRMED), MB-194 (low, CONFIRMED).
**Flags:** PRESET-IMPACT.
**Prerequisites:** T5.6.
**Estimate:** 150 min.

At `4a14376d`, in `latinPercKit()` (`tools/membrum_preset_generator.cpp:4250`):
- **Clave Hi, pad 11 (note 47), `:4411-4424`:** Impulse/Shell, `clickLayerMix` 0.92, `clickLayerContactMs` 0.06.
- **Clave Lo, pad 12:** `pads[12] = pads[11]`, with only size 0.16, decay 0.16 and pan changed (`:4426-4429`).
- **Castanets, pad 27 (note 63), `:4648-4661`:** the same click and the same b3 0.70 / b1 0.40.
- The shell f0 is `1500 * 0.1^size` (`shell_mapper.h:44`): 1191, 1038 and 1306 Hz. The identical full-level click dominates all three.
- **Cabasa (16 / note 52, `:4464-4476`) and Hand Shaker (17 / note 53, `:4478-4490`):** both NoiseBurst/NoiseBody, `noiseLayerMix` 0.85, cutoff 0.73 / 0.70, colour 0.75 / 0.72.
- **Cross-kit:** Latin pads 0-4 (notes 36-40, `:4261-4343`) copy `handDrumsKit()` (`:3959`) pads 6, 8, 2, 0 and 4 (notes 42, 44, 38, 36, 40; `:3970-4115`) field for field. The only exception is Latin pad 2's pan.
- The Hand Drums header (`:3963-3965`) says cross-kit duplicates were removed. The Latin header (`:4254`) promises 28 distinct voices.

**1. Failing test.** Extend `test_factory_kit_sameness.cpp`:
- `TEST_CASE("Factory kit sameness: Latin Perc has no near-duplicate pads", "[preset][kit_sameness]")`: zero near-duplicate pairs in "Latin Perc". Baseline: 4 pairs (`quality-results.txt:194-199`):
  - 47/63: 0.64 / 1.044 / 1.000
  - 47/48: 0.96 / 1.007 / 1.000
  - 48/63: 1.41 / 1.037 / 1.000
  - 52/53: 2.11 / 1.029 / 1.111
- `TEST_CASE("Factory kit sameness: no Latin Perc pad duplicates a Hand Drums pad", "[preset][kit_sameness]")`.
  - Render the enabled pads of both kits.
  - For every Latin pad against every Hand Drums pad with the same exciter and body, assert `bandDist >= 3.0` dB (MB-194's threshold, applied to every note pairing, not only the same note).
  - Today Latin 36 against Hand Drums 42 gives identical rows: centroid 1258.0 Hz, `kit-pads.csv:360` against `:386`. Note 38 is 741.5 Hz in both kits and note 40 is about 1732 Hz in both.

**2. Fix.** In `latinPercKit()`:
- **Claves and castanets:**
  - Lower `clickLayerMix` to about 0.45 so the shell body carries the pitch.
  - Spread the sizes: Clave Hi 0.06, Clave Lo 0.20, Castanets 0.12, with the castanets on a brighter material (0.90) and a shorter contact.
  - Write pad 12's fields explicitly instead of copying pad 11.
- **Cabasa and shaker:**
  - Cabasa brighter and shorter: cutoff 0.85, colour 0.85, `noiseLayerDecay` 0.07, burst 0.15.
  - Shaker darker and longer: cutoff 0.60, colour 0.60, `noiseLayerDecay` 0.18, burst 0.45.
- **Congas and bongos (Latin pads 0-4):**
  - Re-voice them to Latin tuning, higher and tighter than the Hand Drums set: sizes 0.04 to 0.06 smaller, End pitch 2 to 3 semitones higher, strikePosition 0.45 (slap-edge).
  - Write each one explicitly. Do not change the Hand Drums kit.
- Fix the header comments (`:3963-3965`, `:4254-4259`) so they state what the data now does.

**3. Build and gate.**
- PRESET-IMPACT: regenerate, build `Membrum`, then the kit-quality probe before and after.
- The table covers Latin notes 36-40, 47, 48, 52, 53 and 63 and the near-duplicate flags that change.
- Stop for the ruling.

**4. Acceptance.**
- The Latin Perc near-duplicate count goes from 4 (of 378) to 0.
- Every Latin pad is at least 3 dB bandDist from every Hand Drums pad that shares its exciter and body (0.00 dB today for five pairs).
- The Hand Drums rows are unchanged.

### T5.13 — Acoustic Studio Kit: crash 1 and crash 2 differ

**Findings:** MB-192 (low, CONFIRMED).
**Flags:** PRESET-IMPACT.
**Prerequisites:** T5.6.
**Estimate:** 60 min.

At `4a14376d`, in `acousticKit()` (`tools/membrum_preset_generator.cpp:501`):
- Crash 1 is pad 13 (note 49, `:769-793`) and crash 2 is pad 21 (note 57, `:917-942`). Both are NoiseBurst/NoiseBody with the same `bodyDampingB1` 0.060 and `bodyDampingB3` 0.00008 (`:781-782`, `:931-932`), modeStretch, scatter, nonlinear coupling, level, `noiseLayerGain` and click.
- They differ only slightly: material 0.93 / 0.90, size 0.35 / 0.40, noise cutoff 0.85 / 0.80, colour 0.79 / 0.70, noise decay 0.95 / 0.90, and pan.
- The comment at `:924` says crash 2 is the "darker/larger sister".
- Measured: bandDist 1.23 dB, centroid ratio 1.043, decay ratio 1.031 (`kit-pairs.csv:281`). Centroid 7525.6 against 7216.6 Hz, decay 0.96 against 0.99 s, peak -16.35 against -15.78 dBFS (`kit-pads.csv:15`, `:23`).

**1. Failing test.** Extend `test_factory_kit_sameness.cpp` with
`TEST_CASE("Factory kit sameness: Acoustic Studio Kit crash 2 is a darker, longer crash than crash 1", "[preset][kit_sameness]")`.
- Render notes 49 and 57 for 4 s.
- Assert `bandDist >= 3.0` dB, `centroid(57) <= 0.90 * centroid(49)` and `decayS(57) >= 1.2 * decayS(49)`. These are the comment's stated intent made measurable.
- Today: 1.23 dB, a centroid ratio of 0.96 and a decay ratio of 1.03.

**2. Fix.** In `acousticKit()` pad 21 only:
- Size 0.50 (MB-192 suggests 0.30 against 0.50; crash 1 stays at 0.35).
- About half crash 1's low-mode damping: b1 0.060 becomes 0.030 in the old override encoding. Translate it to T1.23's encoding.
- `noiseLayerCutoff` 0.74, `noiseLayerColor` 0.62.
- `pads[21].strikePosition = 0.32;` appears twice (`:922` and `:930`). Remove the second only because you are editing this block; it changes nothing.
- Keep the crash design rules from `CRASH-REDESIGN-PLAN.md` (no modeInject, bloom on, f^2 roll-off).

**3. Build and gate.**
- PRESET-IMPACT: regenerate, build `Membrum`, then the kit-quality probe before and after (notes 49 and 57).
- `test_snare_body_balance.cpp:279` ("Factory Acoustic Studio Kit snare...") and `test_crash_acceptance.cpp` must stay green.
- Stop for the ruling.

**4. Acceptance.**
- 49 against 57: bandDist at least 3 dB (1.23 today), crash 2's centroid at least 10 % lower, and its decay at least 1.2x.
- The kit's near-duplicate count goes from 1 (of 378) to 0.
- No other row of the kit moves.

## Phase 6 — Duplication, parallel paths, conformance and documentation cleanups

Reduce the hand-written field maps and registrations to one table each, delete dead code, and make comments, manual, site and project docs describe the engine that ships, without changing one rendered sample or one state byte.

### T6.1 — One sound-slot descriptor table drives the codec, the controller codec, setPadConfigField and the generator; every slot round-trips through every route

- **Findings:** MB-092 (low, CONFIRMED), MB-164 (filed medium, final low, CONFIRMED), MB-146 (filed medium, final low, CONFIRMED)
- **Flags:** none
- **Prerequisites:** none
- **Estimate:** 360 min

**Step 1 in phase 6.** T6.1 to T6.6 are refactors, and a refactor has no behavioural red. Each of these tasks therefore names two kinds of case:
- A **characterization case** must be green on the code before the edit and must stay green after it.
- A **red case** fails on the unfixed code. Often it fails because the shared table or header it includes does not exist yet; that compile error is the expected red. Where a finding names a wrong value (T6.2, T6.3), the red case asserts that value.

This task adds a factory-kit state pin, `"Factory kits: processor state bytes are pinned"`. Every later phase-6 task keeps it green: that is how "not one state byte" is checked.

**Read this first.** Earlier tasks have edited every file this task touches:
- T1.3 changed how `noiseLayerGain` (sound slot 57) and the body-damping sentinel are saved, and may have given `noiseLayerGain` a parameter offset or a row in the controller codec.
- T1.22 reworked the macros and moved the state version to 6.
- T1.23 replaced the damping sentinel encoding.
- T2.8 added read paths for released versions 1 and 2.

The anchors below were read at commit `4a14376d`; find each one by its symbol. The numbers 59 slots, 66 offsets (`kPadActiveParamCountV12`) and version 5 are the values at that commit. The table this task builds must reproduce **the layout the code has when the task starts**, row for row. It never adds, removes or reorders a slot.

The audit's field-map probe is the baseline (`specs/membrum-audit/probes/probe-results.csv`):
- `fieldmap:offset->slot->cfg` maps 56 offsets one to one; only offsets 59 (enabled) and 64 (pan) are skipped by the pad-preset route (FR-061).
- `fieldmap:applyPadSnapshot-clamp` shows that slots 0-27 and 30-33 keep a value of 5.0 unclamped, while every later slot is clamped.

**Clamping.** The table gives each row the clamp that `State::applyPadSnapshot` applies today, and every route applies that row's rule:
- **None:** slots 0-27 and 30-33.
- **[0, 1]:** slots 34-41, 44-56 and 58.
- **Sentinel:** slots 42-43. A value below 0 stays -1; any other value is clamped to [0, 1]. Use T1.23's rule if T1.23 changed this.
- **[0.1, 16]:** slot 57.
- **uint8 choke / bus:**
  - On blob load, a choke group above 8 or a bus above 15 becomes 0.
  - On a parameter write, the value is `round(v x 8)` clamped to 0..8, or `round(v x 15)` clamped to 0..15.

Widening the "None" rows to [0, 1] would change how an out-of-range blob loads, and so would change a state byte. Do not do it here. Name it in the commit message as a follow-up for the user, with the probe value above.

**1. Failing test**

Create two new files and add both to the `add_executable(membrum_tests` list in `plugins/membrum/tests/CMakeLists.txt`.

**Red case.** In `plugins/membrum/tests/unit/state/test_sound_slot_table.cpp`:
`TEST_CASE("Sound slot table: every slot and pad offset appears exactly once", "[state_codec][slot_map]")`

It includes `dsp/pad_field_table.h` and asserts:
- `Membrum::kSoundSlotCount` equals `std::tuple_size_v<decltype(State::PadSnapshot::sound)>`.
- Every sound slot `0..kSoundSlotCount-1` has exactly one row in `kPadFields`.
- Every pad offset from 2 to the active-offset count minus 1 has exactly one row. Offsets 0 and 1 are the selectors, which stay in `setPadConfigSelector`.
- Every row with float storage has a non-null member pointer.
- The FR-061 flag `padPresetSkips` is set on exactly these rows: slots 28, 29, 51 and 56, `couplingAmount`, and the five macros. Type this list into the test from the comments at `state_codec.cpp:281-283` and `:350-351` and at `controller_state_codec.cpp:305-307` and `:321-322`, not from the table.

It fails today because the header does not exist. That compile error is the expected red.

**Characterization case.** In the same file:
`TEST_CASE("Sound slot table: every slot round-trips through every route", "[state_codec][slot_map]")`

It uses only APIs that exist today, so it compiles and runs on unfixed code. Its oracle is a list typed into the test from the index comment in `state_codec.h` (`PadSnapshot::sound`, lines 44-59): each entry is `{slot, pad offset or none, accessor lambda PadConfig& -> float&}`. For each entry `k`, take `v = (k + 1) / 64.0f`, which is exact in float and in double, and check every route:

1. `State::toPadSnapshot`: `sound[slot] == double(v)` exactly.
2. `State::applyPadSnapshot` onto a PadConfig whose fields hold other values: the field equals `v`.
3. `State::applyPadPresetSnapshot`: the field equals `v`, except on FR-061 rows, where the field keeps its prior value.
4. `VoicePool::setPadConfigField(3, offset, v)`, then `padConfig(3)`: the field equals `v`. For choke, write `5 / 8.0f` and expect `chokeGroup == 5`. For bus, write `11 / 15.0f` and expect `outputBus == 11`. The fixture is in `unit/voice_pool/voice_pool_test_helpers.h`.
5. Controller, with `Membrum::Controller` after `initialize(nullptr)` (as in `unit/vst/membrum_vst_tests.cpp:76-77`):
   - `setParamNormalized(padParamId(3, offset), v)`, then `ControllerState::buildPadSnapshotFromParams(ctrl, 3).sound[slot] == v` (margin 1e-12).
   - `ControllerState::applyPadSnapshotToParams` with a capturing `ParamSetter` records `(padParamId(3, offset), v)`.

Extra checks:
- Slots 42 and 43 at -1 stay -1 through routes 1-3. Route 5 must not call the setter for those two IDs (`controller_state_codec.cpp:263-271`).
- Slot 57 at 6.2f survives routes 1-3.
- Rows with no pad offset skip routes 4 and 5. If T1.3 gave `noiseLayerGain` an offset, it is no longer one of those rows.
- Pin today's clamp: `sound[k] = 5.0` through `applyPadSnapshot` keeps 5.0f on the "None" rows and clamps every other row by its rule.

This case must be **green on the code before any production edit**. Run it and record the result. If a check fails, a route has drifted: stop and report it, because phase 6 changes no behaviour.

**State pin (characterization).** In `plugins/membrum/tests/unit/preset/test_factory_kit_state_pin.cpp`:
`TEST_CASE("Factory kits: processor state bytes are pinned", "[preset][factory][state_pin]")`

For each of the 20 kits under `plugins/membrum/resources/presets/Kit Presets/`:
- Extract the component-state chunk. Copy the parsing steps of `unit/preset/test_factory_kit_presets.cpp:94-126` (`readFile`, `factoryPresetRoot`, `PresetFile::getEntry(kComponentState)`).
- Run `Processor::initialize`, `setState(chunk)`, `getState`. If `setState` now hands the kit to the audio thread (T1.1), run one `process()` block between `setState` and `getState`, as the T1.1 tests do.
- Require the byte length and the FNV-1a 64 digest of the `getState` bytes to equal a table typed into the test (relative kit path, length, digest). This digests stored values, not float arithmetic, which `dsp/CLAUDE.md` allows.
- `REQUIRE` that the preset root is found. Do not `WARN` and return.

Produce the table from the code before any production edit:
1. Add a temporary `WARN` that prints each row.
2. Run the case once and paste the rows into the test.
3. Remove the `WARN`.

**2. Fix**

- **`plugins/membrum/src/dsp/sound_slots.h` (new).** Depends only on `<cstddef>` and `<cstdint>`. In `namespace Membrum`:
  - `enum SoundSlot : std::uint8_t`, with one named constant per slot, named after its field: `kSlotMaterial = 0` through `kSlotWireCoupling = 58`, following the `state_codec.h` index comment.
  - `inline constexpr std::size_t kSoundSlotCount`.

  Before creating it, run `grep -rn "SoundSlot\|PadFieldDesc\|kPadFields" dsp/ plugins/` (ODR).
- **`plugins/membrum/src/dsp/pad_field_table.h` (new).** Includes `pad_config.h` and `sound_slots.h`.
  - `struct PadFieldDesc { int padOffset; int soundSlot; PadFieldStorage storage; float PadConfig::* member; PadFieldClamp clamp; float lo; float hi; bool padPresetSkips; }`.
  - `padOffset` uses -1 for "no parameter" and `soundSlot` uses -1 for "not in `sound[]`".
  - `PadFieldStorage` is one of `Sound`, `ChokeMirror`, `BusMirror`, `CouplingAmount` or `Macro`; a Macro row also carries its macro index.
  - `inline constexpr std::array kPadFields`, ordered by sound slot, followed by the `couplingAmount` and macro rows.
  - Two constexpr lookups, `padFieldForOffset(int)` and `padFieldForSlot(int)`, each returning a row pointer or nullptr.
  - `static_assert`s that each slot and each active offset from 2 up appears exactly once.
  - Change `std::array<double, 59>` in `PadSnapshot` and `PadPresetSnapshot` (`state_codec.h:60`, `:100`) to `kSoundSlotCount`.
- **`state/state_codec.cpp`.** `toPadSnapshot` (`:64-160`), `applyPadSnapshot` (`:162-255`) and `applyPadPresetSnapshot` (`:276-358`) become loops over `kPadFields` that apply each row's clamp and FR-061 flag.
  - The selectors stay explicit.
  - The uint8 `chokeGroup` and `outputBus` stay authoritative on load (`:247-248`).
  - Version read paths for older layouts (T2.8 and the STATE-COMPAT migrations) keep their own old-layout maps, but name current-layout slots with `kSlot...` constants.
- **`controller/controller_state_codec.cpp`.**
  - Delete `kContiguousOffsets`, `kExciterSecOffsets`, `LateSlot` / `kLateSlots` and `kMacroOffsets` (`:28-99`), and the hand-coded `sound[28..29]` and `sound[30 + j]` blocks (`:143-156`, `:223-239`).
  - `buildPadSnapshotFromParams`, `applyPadSnapshotToParams` and `applyPadPresetSnapshotToParams` loop over `kPadFields`.
  - The sentinel skip (`:265-268`, `:313-316`) and the FR-061 skip (`:311`) come from the row flags.
  - Keep the write order `applyPadSnapshotToParams` has when the task starts. At `4a14376d` that is the macro rows first (`:197-214`), then every other row, because a macro write must not overwrite the preset's saved `noiseBurstDuration` or `couplingAmount`.
- **`voice_pool/voice_pool.cpp`, `VoicePool::setPadConfigField` (`:605-699`).** Replace the switch with `padFieldForOffset(offset)` and a member write through the row clamp.
  - The choke row keeps its `chokeGroups_.setEntry` side effect and the rounding at `:641-648`. The bus row keeps its rounding (`:651-656`).
  - An unknown offset is still a no-op.
  - It runs on the audio thread: a constexpr lookup and a member-pointer write, with no allocation.
  - For a host value in [0, 1], the clamp changes nothing.
- **`controller/controller.cpp`, `Controller::pushKitEnabledToGrid` (`:1480-1491`).** Replace `sound[51]` with `sound[kSlotEnabled]`.
- **`tools/membrum_preset_generator.cpp`.**
  - `writePadToBuffer` (`:243-299`) fills a `std::array<double, kSoundSlotCount>` by named slot (`sound[kSlotMaterial] = p.material;` and so on).
  - A `std::array<bool, kSoundSlotCount>` records each write. The generator exits with an error naming the slot if any slot is written twice or never.
  - Include `dsp/sound_slots.h` and delete `kSoundSlotsPerPad` (`:42`).
  - Add `target_include_directories(membrum_preset_generator PRIVATE ${CMAKE_SOURCE_DIR}/plugins/membrum/src)` after `add_executable(membrum_preset_generator` in the root `CMakeLists.txt` (`:718-724`).
  - The generator's global `kNumPads` and its unscoped `ExciterType` / `BodyModelType` do not collide with `namespace Membrum`.
  - Correct the "57 x float64" comment at `:247`, which this edit touches.
- **Tests that hard-code slot indices** switch to the `kSlot...` names: `unit/processor/test_kit_switch_infinite_ring.cpp:457` and `:1772-1805`, `unit/preset/test_kit_browser_load.cpp:207-211`, `unit/preset/test_pad_preset.cpp:535-556` and `:658-671`.

**3. Build and gate**

- Targets: `membrum_tests Membrum membrum_preset_generator membrum_fit_tests`.
  - `membrum_fit_core` links `membrum_preset_io`, and `tools/membrum-fit/src/preset_io/kit_preset_writer.cpp` calls `State::toPadSnapshot`.
  - The gate adds `membrum_fit_tests` to the command.
- Regenerate the kits:
  `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target generate_membrum_presets -- -m:2`
  Then `git status --porcelain "plugins/membrum/resources/presets"` must print nothing. The kits are re-encoded byte for byte.
- No file under `dsp/`, `plugins/shared/` or `tests/test_helpers/` changes, so no `header-consumers.js` run is needed.

**4. Acceptance**

- The red case is green. The characterization case and the state pin are green before the edit and after it; put both run lines in the commit message.
- The regenerated kits show no git diff.
- `Grep` for `sound\[[0-9]` in `plugins/membrum/src`, `plugins/membrum/tests` and `tools/membrum_preset_generator.cpp` finds only the old-version migration maps.
- `Grep` for `kLateSlots` finds nothing.
- `plugins/membrum/tests/golden/phase1_default.bin` (`approval/test_phase1_regression.cpp`) stays green unregenerated.
- If you re-run the field-map probe, it still reads `56 / off59 ... padRT=n, off64 ... padRT=n`.

### T6.2 — membrum-fit addresses pad fields through the same descriptor table

- **Findings:** MB-268 (low, PLAUSIBLE)
- **Flags:** VERIFY-FIRST
- **Prerequisites:** T6.1
- **Estimate:** 120 min

`tools/membrum-fit` is out of the audit's scope, except where it duplicates the plugin's parameter mapping. `fieldPtr` duplicates it.

**What the code does now.** `fieldPtr` (`tools/membrum-fit/src/refinement/bobyqa_refine.cpp:12-79`) is a switch on integer literals for offsets 2-29 and 32-58. Its `default:` returns nullptr.
- `padConfigToVector` (`:82-91`) turns a nullptr into 0.0f.
- `vectorToPadConfig` (`:93-101`) skips the write.
- So offsets 59-65 (enabled, knee, mid pitch, mid fraction, curve 2, pan, wire coupling) read as 0 and are never written.

**Why VERIFY-FIRST.** The verifiers found that the 57 cases match the `PadParamOffset` enum today, and that every caller passes indices inside 2..58 (`main.cpp:281-282` and `:296`, `tests/unit/refinement/test_bobyqa.cpp:32` and `:104-105`). The PLAUSIBLE part is the consequence. The red test below decides it. Leaving `noiseLayerGain`, the knee and pan out of the optimiser is a documented design choice (`main.cpp:273` and `:283-299`). This task does not change which fields the fitter optimises, so every fit result stays identical.

**1. Failing test**

New file `tools/membrum-fit/tests/unit/refinement/test_field_map.cpp`. Add it to the `add_executable(membrum_fit_tests` list in `tools/membrum-fit/tests/CMakeLists.txt`, next to `unit/refinement/test_bobyqa.cpp` (`:21`).

`TEST_CASE("membrum-fit field map addresses the same PadConfig member as the plugin table for every float offset", "[membrum_fit][field_map]")`

It includes `dsp/pad_field_table.h` from T6.1, which is visible through `membrum_dsp`'s include directory. For every `kPadFields` row that has a pad offset and float storage (sound slots, `couplingAmount`, the macros):
- Set the member of a default `PadConfig` to a distinct in-range value, `0.25f + row index / 256.0f`.
- Require `MembrumFit::padConfigToVector(cfg, {offset})[0]` to equal that value.
- Call `vectorToPadConfig({0.61f}, {offset}, cfg2)` and require `cfg2.*member == 0.61f`.

On today's code the offsets 59-65 return 0.0f and are not written: the case is red. If it is green on the code you start from, the finding is refuted. Keep the test, change no production code, and write "MB-268 refuted" with the case's output into the commit message.

**2. Fix**

- **`bobyqa_refine.cpp`, `fieldPtr`.** Replace the switch with `Membrum::padFieldForOffset(idx)`. Return `&(c.*row->member)` for a row with float storage, and nullptr otherwise (selectors, choke and bus, unknown offsets).
  - In `padConfigToVector` and `vectorToPadConfig`, add an `assert` on a nullptr pointer.
  - Keep the [0, 1] clamp in `vectorToPadConfig` (`:98`).
  - Delete the hand-written layout comment (`:13-17`).
- **`bobyqa_refine.h:23`.** Change the comment "subset of [2..58]" to "PadParamOffset values with a float PadConfig member (dsp/pad_field_table.h)".
- **Named offsets instead of number literals.** Replace the literals with `Membrum::kPadMaterial`, `kPadSize` and the other named offsets; the numbers stay the same:
  - `main.cpp:281-282` (`{2, 3, 4, 8, 9, 15}`)
  - `main.cpp:296` (`{50, 51, 52, 53, 54, 56, 57, 58}`); note that 57 is the offset `kPadSecondaryMaterial`, not sound slot 57.
  - `tests/unit/refinement/test_bobyqa.cpp:32` and `:104-105`
  - `tests/unit/refinement/test_global_crs.cpp:28`
- **Leave the optimiser set alone.** Do not add `noiseLayerGain`, or any offset above 58, to an optimisable set. `noiseLayerGain` is not normalised, and `vectorToPadConfig` clamps to [0, 1].
- **`tools/membrum-fit/AGENT_GUIDE.md:311`.** Point the line "`bobyqa_refine.cpp::fieldPtr` — optimisable parameter index map" at the table.

**3. Build and gate**

No plugin file changes.
- Build `membrum_fit membrum_fit_tests` instead of `membrum_tests`.
- Gate: `node tools/run-gate.js --share 4 --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' membrum_fit_tests`
- No pluginval: no plugin source changed.
- clang-tidy: `-Target membrum-fit`.

**4. Acceptance**

- The new case goes from red to green, with offsets 59-65 returning their member values.
- `"BOBYQA: 14D Phase-8 refinement never increases loss"` stays green, and the whole gate is green.
- `Grep` for `case [0-9]` in `bobyqa_refine.cpp` finds nothing.
- The optimisable index values in `main.cpp` are unchanged. Confirm by printing `rctx.optimisable` once before the edit and once after; delete the print.

### T6.3 — Per-pad and proxy parameter registrations come from one table: defaults, titles and step counts agree

- **Findings:** MB-079 (low, CONFIRMED), MB-111 (low, CONFIRMED)
- **Flags:** none
- **Prerequisites:** T1.20
- **Estimate:** 180 min

**Read this first.** T1.20 made the selected-pad proxies non-automatable, and it may have moved the proxy-to-offset map (`kProxyMappings`, `controller.cpp:84-155`) into a header the processor shares. This task extends whatever map T1.20 left. It keeps T1.20's parameter flags row by row, and it changes no parameter type, ID, title, unit or step count that a host sees, with two exceptions:
- the three defaults listed under "Decisions";
- the internal Exciter Type `stepCount` in the spec table, which no host sees.

**What drifts today.** Each proxied field is registered twice by hand: the global proxy (inline at `controller.cpp:281-322`, `kPhase2Specs` at `:325-382`, and inline again at `:384-461`, `:477-497`, `:561-591` and `:593-670`) and the per-pad spec (`kPadParamSpecs`, `:157-256`). Values at `4a14376d`:
- Proxy `kExciterNoiseBurstDurationId` defaults to 0.230769 (5 ms, `:336`). The per-pad default and `PadConfig::noiseBurstDuration` are 0.5 (8.5 ms, `:202`, `pad_config.h:236`). This repeats the prior L-10.
- Proxy `kToneShaperPitchEnvStartId` defaults to 0.070721 (about 27.7 Hz, `:347`); the per-pad default is 0.0 (`:181`).
- PitchEnv Curve: the per-pad default (`:184`) and the proxy default (`:433`) are 0.5 (linear). `PadConfig::tsPitchEnvCurve` is 0.0f (`pad_config.h:205`), which gives curveAmount -1 (`pad_config_apply.h:110-111`).
- `kPadParamSpecs` gives `stepCount = 5` for the 7-entry Exciter Type list (`:168`). The StringList branch (`:685-709`) never reads it.

VST3Editor copies a parameter's default into the bound control, and the default-value click resets to it (`vst3editor.cpp:287`, `ccontrol.cpp:33-52`). So Ctrl-click on the NoiseBurst Duration knob (`editor.uidesc:220`, tag 204) writes 5 ms into the selected pad. A host reset of the per-pad parameter writes 8.5 ms.

**Decisions.**
- **Defaults.** Every registered default, per pad and proxy, is the normalised encoding of the `PadConfig{}` field: the value a fresh pad plays.
  - Float fields use the member value.
  - Choke group g encodes as g/8, and bus b as b/15.
  - A list selector encodes as index/(entries - 1).
  - For the body-damping rows, use T1.23's encoding of the default. If T1.23 left a -1 sentinel with no parameter representation, exclude those two rows, with a comment naming T1.23 and MB-074.

  Three defaults change: proxy NoiseBurst Duration 0.230769 to 0.5; proxy PitchEnv Start 0.070721 to 0.0; per-pad and proxy PitchEnv Curve 0.5 to 0.0. The processor's PadConfig is untouched, so no rendered sample and no state byte changes. Only the value a reset writes changes. If T2.14 (MB-155) already aligned NoiseBurst Duration, that row is already green.
- **Titles.** The host-visible proxy titles stay. `kDecayId` "Resonance" and `kStrikePositionId` "Strike Point" are pinned on purpose by `unit/vst/membrum_vst_tests.cpp:116-120`. The table carries a title override for these.
- **Types.** Each ID keeps its type. Proxies that are StringLists stay StringLists: exciter, body, filter type, morph curve, knee, choke and bus. Per-pad discrete offsets stay stepped RangeParameters. `controller.cpp:567-576` explains why the types must not be unified.
- **Generator.** The generator's own Pad defaults (`fmRatio = 0.5` at `tools/membrum_preset_generator.cpp:105`, `tsPitchEnvCurve = 0.15` at `:93`) are kit data. They are not touched here. MB-156 / T5.6 owns the first of them.

**1. Failing test**

New file `plugins/membrum/tests/unit/controller/test_param_registration_defaults.cpp`, added to the `membrum_tests` list.

`TEST_CASE("Registered defaults: proxy == per-pad == PadConfig{} for every proxied field", "[controller][params][defaults]")`

It uses `Membrum::Controller` after `initialize(nullptr)` and reads each value with `getParameterObject(id)->getInfo()`, as `unit/controller/test_audit_ui_controller_batch.cpp:113-118` does.

First, explicit checks; each is red today:
- `kExciterNoiseBurstDurationId` default is 0.5.
- `kToneShaperPitchEnvStartId` default is 0.0.
- `kToneShaperPitchEnvCurveId` default and `padParamId(p, kPadTSPitchEnvCurve)` default are 0.0 for all 32 pads.

Then two loops:
- For every `kPadFields` row (T6.1) with a pad offset, and for all 32 pads: the registered default equals the encoding of `PadConfig{}`, with `Approx().margin(1e-6)`. The margin covers `0.333333f` against `0.333333`.
- For every row of the proxy table exported by the fix (see below): the proxy default equals the per-pad default, and the step counts are equal. For a list, the step count is entries - 1.

The loops compile only after the fix exports the table. That compile error is part of the red.

**2. Fix**

All in `plugins/membrum/src/controller/controller.cpp`, in the anonymous namespace, unless the map moved under T1.20.
- **Per-pad spec table.** Keep one row per offset: name, kind (continuous, stepped, list), step count or list strings, and unit. Delete the typed `defaultValue` column; the default is computed from `PadConfig{}` through `kPadFields`, so it is never typed twice.
  - For the Exciter Type and Body Model rows, the list strings live in the row. The registration loop (`:672-725`) builds the StringList from them, with step count entries - 1.
  - That fixes `stepCount = 5` for the 7-entry exciter list. The 6-entry body list already had the right count.
- **Proxy table.** Extend the proxy table (`kProxyMappings`, or T1.20's shared map) to carry, for each proxy ID: its pad offset, a title override, a unit override (Level "dB", the Hz / ms units of `kPhase2Specs` and the inline Phase 7 registrations), the kind with list strings (choke "None" / "CG1".."CG8", bus "Main" / "Aux 1".."Aux 15", filter type, morph curve, knee "Off" / "On", exciter, body), and T1.20's flags.
  - `initialize()` registers every proxy from this table. Default and step count always come from the per-pad row.
  - Move the table to `plugins/membrum/src/controller/proxy_param_table.h`. The test includes it from there, and it stays controller-side; the processor must not include controller headers.
- **Delete** `kPhase2Specs` and every inline proxy registration listed under "What drifts today". Keep the non-proxy globals hand-written: Max Polyphony, Voice Stealing, Selected Pad, the four coupling parameters, Master Gain and UI Mode.
- **Keep the registration order of the IDs.** Register the proxy rows in today's order. Split the loop where today's code interleaves non-proxy globals: the first run ends before Max Polyphony (`:463`), Choke Group follows Voice Stealing (`:477-497`), and the rest starts at Output Bus (`:561`).
- **`setParamNormalized` (`:861-891`) and `syncGlobalProxyFromPad` (`:961-985`)** iterate the same table.

**3. Build and gate**

Standard workflow.

Before and after the edit, dump the whole parameter list:
1. Add a temporary hidden case tagged `[.][param_dump]`. For each parameter index, it prints `id | title | units | stepCount | default (%.6f) | flags` to `f:/tmp/membrum-phase6/params-before.txt`, and on the second run to `params-after.txt`.
2. `diff` the two files.
3. Delete the case before the commit.

**4. Acceptance**

- The red case is green.
- The dump diff contains exactly 35 changed lines (34 if T2.14 already aligned NoiseBurst Duration), all of them defaults:
  - `kExciterNoiseBurstDurationId`, `kToneShaperPitchEnvStartId` and `kToneShaperPitchEnvCurveId`;
  - the 32 per-pad PitchEnv Curve IDs.

  Nothing else differs: no order, title, unit, step count or flag. Paste the diff into the commit message.
- `"Membrum Controller parameter metadata matches contract"` stays green. So does the parameter-count check in `membrum_vst_tests.cpp:98` (2181 at `4a14376d`, or the count when the task starts).
- `"Audit #10: Friction Pressure proxy default is 0.0 (matches per-pad)"` stays green.
- The state pin (T6.1) stays green.

### T6.4 — Dead and test-only code removed from the processor, the voice pool and the controller

- **Findings:** MB-134 (low, CONFIRMED), MB-165 (low, CONFIRMED), MB-112 (low, CONFIRMED)
- **Flags:** none
- **Prerequisites:** none
- **Estimate:** 120 min

**Overlaps.** Other tasks own parts of these findings:
- **T2.19 (MB-104)** owns the write-only `modeToggleButton_`. If T2.19 already removed it, or gave it a reader and a reset to nullptr, leave it. Otherwise delete it here.
- **T6.7 (MB-070, MB-125 to MB-129, MB-135)** owns the stale `notify` comment at `processor.h:68-70` and the header comments of `drum_voice.h`, `voice_pool.h`, `state_codec.h` and `plugin_ids.h`. Touch those only where this task deletes the code a comment describes.
- **T6.3** already fixed the `stepCount` of the Exciter Type spec.
- **T1.22** reworked `MacroMapper`. If T1.22 removed `RegisteredDefaultsTable`, or gave `defaults_` a reader, then:
  - Do only what is left.
  - If a reader exists, keep the table, but size it with the active-offset count (`kPadActiveParamCountV12`, or the current count) instead of 64. Then add the missing `tsPitchEnvCurve` entry by reading it from T6.1's `kPadFields` instead of typing it.
- **T1.20** may now call `Controller::forwardGlobalToPad`. If it does, keep it.

**1. Failing test**

Deleting dead code changes no behaviour, so there is no behavioural red. The red is the compile error of every caller that remains once the API is gone. The tests are ported first, then the API is deleted.

New characterization case, added to `plugins/membrum/tests/unit/voice_pool/test_choke_group.cpp`:
`TEST_CASE("Choke groups set through setPadConfigField reach both PadConfig and the choke table", "[voice_pool][choke_group]")`

This is the coverage the `setChokeGroup` tests skipped (MB-134 "why wrong"). For pads 6 and 10 it does the following:
1. `setPadConfigField(p, kPadChokeGroup, 1 / 8.0f)`
2. Require `padConfig(p).chokeGroup == 1`.
3. Note-on pad 6, render 50 ms, note-on pad 10.
4. Require that pad 6's slot enters fast release: its `voiceMeta` / `releasingMeta` show FastReleasing.

Use the helpers in `voice_pool_test_helpers.h`. The case is green before and after the deletion.

**2. Fix**

**Voice pool**
- Delete `VoicePool::setChokeGroup` (`voice_pool.h:151-153`, `voice_pool.cpp:595-599`). It writes only the choke table, for all 32 pads, never `cfg.chokeGroup`.
  - Port its 9 test calls to the production route, applied to all 32 pads so the meaning is unchanged: `for (int p = 0; p < Membrum::kNumPads; ++p) pool.setPadConfigField(p, Membrum::kPadChokeGroup, g / 8.0f);`
  - The callers: `unit/voice_pool/test_choke_group.cpp:55`, `test_choke_click_free.cpp:74`, `test_choke_allocation_free.cpp:50`, `test_per_pad_dispatch.cpp:426`, `test_polyphony_allocation_matrix.cpp:132`, `test_voice_pool_allocation_free.cpp:77`, and `unit/processor/test_coupling_fuzz.cpp:109`, `test_coupling_phase9_verification.cpp:372` and `:459`.
  - In the allocation-free tests, the loop runs inside the measured region exactly where the old call did. `setPadConfigField` does not allocate.
- Delete `getChokeGroupAssignments` and `loadChokeGroupAssignments` (`voice_pool.h:220-228`, `voice_pool.cpp:832-842`).
  - Port the five `loadChokeGroupAssignments(raw)` calls in `test_choke_group.cpp` (`:146`, `:182`, `:276`, `:344`, `:396`) to `for (p) pool.setPadChokeGroup(p, raw[p]);`. Like `ChokeGroupTable::loadFromRaw`, `setPadChokeGroup` maps a group above 8 to 0 (`voice_pool.cpp:735-742`, `choke_group_table.h:84-92`).
  - Update the comments at `test_choke_group.cpp:131-133` and `:319`.
- `ChokeGroupTable::setGlobal`, `loadFromRaw` and `raw()` (`voice_pool/choke_group_table.h:29`, `:39-46`, `:58-66`, `:84-92`) lose their last callers. `Grep` `plugins/membrum`, and delete each one that has no caller left.
- Delete `scratchR_` (`voice_pool.h:354`) and its allocation (`voice_pool.cpp:76`). Rewrite the comment at `voice_pool.h:348-352` to describe the one mono scratch buffer.
- **Keep** `forEachMainVoice` and `mainVoiceRef`. Six test files use them to inspect voices, and deleting them would need a replacement accessor. T6.7 corrects their "broadcast" comment (MB-126).

**Processor**
- Delete `editorOpen_` and its accessors `editorOpenForTest` / `setEditorOpenForTest` (`processor.h:87-88`, `:168`). No test calls them.
- Delete `RegisteredDefaultsTable` (`processor/macro_mapper.h:23-27`).
- Delete `MacroMapper::defaults_` (`macro_mapper.h:84`).
- `MacroMapper::prepare(const RegisteredDefaultsTable&)` becomes `prepare()` (`macro_mapper.h:37`, `macro_mapper.cpp:37-42`). It keeps `invalidateCache(); prepared_ = true;`.
- Delete `Processor::buildRegisteredDefaultsTable` (`processor.cpp:57-86`, declaration at `processor.h:176-179`). The call at `processor.cpp:120` becomes `macroMapper_.prepare()`.
- Remove the sentences about `defaults_` from the cache comment at `macro_mapper.h:65-74`, and fix the "delta-from-registered-default" wording in the header banner (`:9-10`).
- Port the test copies to `prepare()` and delete their local default tables: `unit/processor/test_macro_mapper.cpp:26-41`, `test_macro_preset_load_regression.cpp:66-79`, `test_state_v6_migration.cpp:266-268`.

**Controller**
- Delete `Controller::forwardGlobalToPad` (`controller.h:178-179`, `controller.cpp:987-1001`). It has no caller, and `setParamNormalized` repeats its loop at `:861-891`.
- Use `ui/ui_mode.h` in production instead of hard-coded thresholds:
  - `controller.cpp:1172` and `:1174-1175` use `UI::uiModeFromNormalized` / `UI::uiModeToNormalized`.
  - `controller_state.cpp:78` uses `UI::uiModeToNormalized(UI::kDefaultUiMode)`.
  - `controller_state.cpp:113` and `:179-180` use `uiModeFromNormalized` and `uiModeToNormalized`.

  The threshold is the same (`n < 0.5` means Simple), so behaviour does not change.
- Correct the stale controller comments MB-165 cites:
  - `controller.h:128`: "9036-byte kit preset blob (v4 without selectedPadIndex)". The blob is `State::writeKitBlob` at the current version and includes `selectedPadIndex`. State no byte count.
  - `controller.h:168`: "284-byte pad preset blob". It is `State::writePadPresetBlob`.
  - `controller.h:266-270`: "asymmetric linear ranges". The ranges are decoded cubically; T2.18 fixed the display law.
  - `controller_param_format.cpp:62`: "ten ArcKnobs".
  - `controller_param_format.cpp:66`: "64-param stride". The stride is `kPadParamStride`, 128.
  - `controller_param_format.cpp:429`: "Selected-pad proxies are the pad-0 slot". Proxies forward to the selected pad (`Controller::setParamNormalized`, `syncGlobalProxyFromPad`).
- The three split TUs `controller_view_sync.cpp`, `controller_state.cpp` and `controller_param_format.cpp`:
  - Replace the banner "Phase 4 per-pad parameter registration + proxy logic" (`:2`) with a one-line description of what each file holds.
  - Trim each file's include list (`:5-40`, a copy of `controller.cpp`'s) to the headers that file uses. Remove one include at a time and build. The duplicate `../ui/membrum_buttons.h` at `:37` goes first.

**3. Build and gate**

Standard workflow.

**4. Acceptance**

- `Grep` over `plugins/membrum` finds nothing for each of: `setChokeGroup(`, `ChokeGroupAssignments`, `scratchR_`, `editorOpen`, `RegisteredDefaultsTable`, `defaults_`, `forwardGlobalToPad`.
- `ui/ui_mode.h` has at least one includer under `plugins/membrum/src`.
- The number of `TEST_CASE`s in the edited test files is the same as before plus the one new case. Tests were ported, not deleted; count with `Grep` `output_mode: count` before and after.
- The full gate is green, and the state pin (T6.1) is green.

### T6.5 — NoiseColor moves to a neutral core header

- **Findings:** MB-108 (low, CONFIRMED)
- **Flags:** CROSS-PLUGIN IMPACT
- **Prerequisites:** none
- **Estimate:** 60 min

**What the code does now.** `NoiseColor` is the parameter type of `NoiseOscillator::setColor` (`primitives/noise_oscillator.h:106`). It lives in `dsp/include/krate/dsp/core/pattern_freeze_types.h:118-154`, next to Iterum's Pattern Freeze types: `PatternType`, `SliceMode`, `PitchInterval`, `EnvelopeShape`, `CaptureLength`, `DryMode`, `PatternFreezeConstants`. That block holds the enum (`:124-133`), `kNoiseColorCount` (`:135`), `kDefaultNoiseColor` (`:136`) and `getNoiseColorName` (`:141-154`).

Five Membrum headers include the Pattern Freeze header only for this enum:
- `src/dsp/noise_layer.h:15`
- `src/dsp/click_layer.h:19`
- `src/dsp/bodies/noise_body.h:27`
- `src/dsp/exciters/noise_burst_exciter.h:31`
- `src/dsp/exciters/feedback_exciter.h:29`

`noise_oscillator.h:18`, `processors/ring_modulator.h:21` and `systems/noise_organism.h:91` include it as well.

The move changes no generated code.

**1. Failing test**

New file `dsp/tests/unit/core/noise_color_test.cpp`, added to the `dsp_core_tests` source list in `dsp/tests/CMakeLists.txt` (the `unit/core/...` block near `:29-33`).

`TEST_CASE("NoiseColor lives in core/noise_color.h with eight colours", "[core][noise_color]")`

It includes only `<krate/dsp/core/noise_color.h>` and checks:
- `static_assert(Krate::DSP::kNoiseColorCount == 8)`
- The underlying values run from White = 0 to RadioStatic = 7.
- `kDefaultNoiseColor == NoiseColor::Pink`.
- `getNoiseColorName` returns "White" through "Radio" for the eight colours, and "Unknown" for value 8.

The red is a compile error: the header is missing.

**2. Fix**

1. Before editing, run the CROSS-PLUGIN grep: `grep -rl "pattern_freeze_types.h" plugins/ dsp/include tests/`. At `4a14376d` it found the eight sites named above, plus the files that keep using Pattern Freeze types: `effects/pattern_freeze_mode.h`, `processors/pattern_scheduler.h`, `plugins/iterum/src/parameters/freeze_params.h` and `pattern_freeze_params.h`. List them all in the commit message.
2. Run the ODR check: `grep -rn "enum class NoiseColor\b" dsp/ plugins/`. It must find only the one declaration.
3. Create `dsp/include/krate/dsp/core/noise_color.h`, Layer 0, depending only on `<cstddef>` and `<cstdint>`. Move the four declarations into it unchanged, in `namespace Krate::DSP`, under a Layer-0 banner in the style of the other core headers. Drop "for Noise Bursts pattern" from the comment.
4. `pattern_freeze_types.h` includes `noise_color.h` in place of the moved block, so Pattern Freeze code and `dsp/tests/unit/core/pattern_freeze_types_test.cpp` compile unchanged.
5. `noise_oscillator.h:18` includes `noise_color.h`. Update the dependency comment at `:56`.
6. `ring_modulator.h:21` and `noise_organism.h:91` switch to `noise_color.h`, but only after a `Grep` of each file for the seven Pattern Freeze names above finds none. This is the same root cause, so it is in scope under the flag.
7. The five Membrum headers include `<krate/dsp/core/noise_color.h>`.
8. Add the header to `dsp/lint_all_headers.cpp` next to `:33`.
9. Update the comment reference at `dsp/tests/unit/primitives/noise_oscillator_test.cpp:703`.
10. Update the dependency line at `specs/_architecture_/layer-1-primitives.md:1967`, and the Layer-0 reference doc wherever it lists `pattern_freeze_types.h`.

**3. Build and gate**

- Targets: `dsp_core_tests` and `Membrum`, plus every executable named on the last line of `node tools/header-consumers.js --tag '[membrum]'`. That run is mandatory under the flag, because `pattern_freeze_types.h`, `noise_oscillator.h`, `ring_modulator.h` and `noise_organism.h` were modified. Iterum's consumers are among them.
- Gate all of them in one `run-gate.js` command, with `~[long]` appended to the filter.
- Also run `node tools/lint-layers.js`.
- clang-tidy: `-Target dsp` as well as `-Target membrum`.

**4. Acceptance**

- The new case is green.
- `Grep` for `pattern_freeze_types.h` in `plugins/membrum` finds nothing, and `noise_oscillator.h` no longer includes it.
- Every consumer executable is gated green, and no golden moved.
- The state pin (T6.1) is green.
- `node tools/lint-layers.js` exits 0.

### T6.6 — ADSRExpandedOverlayView has one shared implementation for Membrum, Ruinae and Innexus

- **Findings:** MB-114 (low, CONFIRMED)
- **Flags:** CROSS-PLUGIN IMPACT, UI
- **Prerequisites:** none
- **Estimate:** 180 min

**What the code does now.** Three near-identical classes wrap the shared `Krate::Plugins::ADSRDisplay`:
- **`Membrum::UI::ADSRExpandedOverlayView`** (`plugins/membrum/src/ui/adsr_expanded_overlay.h:29`, 153 lines):
  - Derives from `CViewContainer` only.
  - Closes through an `OutlineActionButton` callback, 65 px wide (`:130-134`).
  - Has `setTitle` and `setColors`. The title defaults to "Filter Envelope", and the title label is 240 px wide.
- **`Ruinae::ADSRExpandedOverlayView`** (`plugins/ruinae/src/controller/adsr_expanded_overlay.h:27`, 178 lines):
  - Also derives from `IControlListener`.
  - Its close button is an `OutlineBrowserButton` with tag 9999, 55 px wide (`:153-158`), handled in `valueChanged` (`:77-81`).
  - Has `setTitle` and `setColors`. The title defaults to "ADSR Envelope", and the label is 200 px wide.
- **`Innexus::ADSRExpandedOverlayView`** (`plugins/innexus/src/controller/adsr_expanded_overlay.h:26`, 160 lines):
  - Same structure as Ruinae.
  - No `setTitle` and no `setColors`.

All three share the backdrop drawing, the click-outside-to-close behaviour (`onMouseDown`) and the panel geometry. The reason Membrum's header gives for keeping a local copy (`:12-13`, "use Membrum's OutlineActionButton") no longer holds: `OutlineActionButton` is the shared `plugins/shared/src/ui/outline_button.h:215`.

**Decisions.**
- **Class and namespace.** One class, `Krate::Plugins::ADSRExpandedOverlayView`, in `plugins/shared/src/ui/adsr_expanded_overlay.h`.
- **Constructor.** `ADSRExpandedOverlayView(const VSTGUI::CRect& frameSize, const char* initialTitle = "ADSR Envelope")`.
- **API.** `open`, `close`, `isOpen`, `getDisplay`, `setCloseCallback`, `setTitle` and `setColors`.
- **Close control.** An `OutlineActionButton` callback, Membrum's 65 px geometry. It has no control tag and no `IControlListener`.
- **Title label.** 240 px wide.

Both ways of closing stay in all three plugins: the Close button and a click on the backdrop. No interaction is removed. The look of the Close button in Ruinae and Innexus changes from `OutlineBrowserButton` to `OutlineActionButton`. Record it with the screenshots under step 4.

**1. Failing test**

New file `plugins/shared/tests/test_adsr_expanded_overlay.cpp`, added to the `shared_tests` source list in `plugins/shared/tests/CMakeLists.txt`, next to `test_adsr_display.cpp` (`:25`).

`TEST_CASE("ADSRExpandedOverlayView: starts hidden, opens, closes by its button and by a backdrop click", "[shared][ui][adsr_overlay]")`

Construct `ADSRExpandedOverlayView overlay(CRect(0, 0, 1000, 700), "Filter Envelope")` on the stack or with `makeOwned`, and set a close callback that counts its calls. Then check:
1. `!isOpen()` after construction, and `getDisplay() != nullptr`.
2. `open()` makes `isOpen()` true.
3. A click at `CPoint(5, 5)`, outside the panel, closes it: `onMouseDown(p, CButtonState(kLButton))`. The callback count is 1.
4. `open()`, then find the child `OutlineActionButton` (iterate the children with `dynamic_cast`). `onMouseDown` at the centre of its view rect closes the overlay, and the count is 2. `OutlineButton::onMouseDown` fires `onClick` on mouse down (`outline_button.h:193-201`).
5. A click inside the panel but away from the button and the display leaves it open.

The red is a compile error: the header is missing.

**2. Fix**

1. Before editing, run `grep -rl "adsr_expanded_overlay.h" plugins/` and list the consumers in the commit message. At `4a14376d` they were:
   - Membrum: `controller.cpp:18`, `controller_param_format.cpp:18`, `controller_state.cpp:18`, `controller_view_sync.cpp:18`.
   - Ruinae: `controller.cpp:30`, `controller_presets.cpp:11`, `controller_view_sync.cpp:26`.
   - Innexus: `controller.cpp:20`, `controller_adsr.cpp:20`, `controller_state.cpp:20`, `controller_view_sync.cpp:20`, and `plugins/innexus/CMakeLists.txt:52`.

   Run the ODR check `grep -rn "class ADSRExpandedOverlayView" plugins/`. After the fix, only the shared declaration may remain.
2. Create the shared header from Membrum's copy, with the constructor argument above. Add it to the shared source list in `plugins/shared/CMakeLists.txt`, next to `src/ui/adsr_display.h` (`:65`).
3. Delete the three plugin copies, and remove `src/controller/adsr_expanded_overlay.h` from `plugins/innexus/CMakeLists.txt:52`.
4. Port the includes and the type names:
   - **Membrum.** `controller.cpp:1592` becomes `new Krate::Plugins::ADSRExpandedOverlayView(frameSize, "Filter Envelope")`. The forward declaration at `controller.h:38` moves out of `namespace Membrum::UI`. The member at `controller.h:277` changes type.
   - **Ruinae.** `controller.cpp:523`. The forward declaration is at `controller.h:41`. The `setCloseCallback` call at `:539` and the `setTitle` / `setColors` calls at `:909-932` stay.
   - **Innexus.** `controller.cpp:1119-1121`. The forward declaration is at `controller.h:51`.
   - The shared header includes `ui/adsr_display.h` and `ui/outline_button.h`, so a plugin TU no longer needs Membrum's `membrum_buttons.h` for this class.
5. Correct Membrum's header comment in the shared file: it is shared now.

**3. Build and gate**

- Build `shared_tests membrum_tests ruinae_tests innexus_tests Membrum Ruinae Innexus`, plus every executable named by the mandatory `node tools/header-consumers.js --tag '[membrum]'`.
- Gate them in one `run-gate.js` command, with `~[long]` appended to the filter.
- Each plugin's editor lifecycle test opens and closes its editor with the overlay attached. Those cases must be green.
- pluginval for all three bundles, one at a time:
  - `build/windows-x64-release/VST3/Release/Membrum.vst3`
  - `build/windows-x64-release/VST3/Release/Ruinae.vst3`
  - `build/windows-x64-release/VST3/Release/Innexus.vst3`
- clang-tidy: `-Target shared`, `-Target membrum`, `-Target ruinae` and `-Target innexus`.
- UI flag:
  - No `editor.uidesc` changes, so no XSLT.
  - Run `node tools/lint-plugin-editor-baseline.js`.
  - Run `node tools/gen-screenshots.js membrum`, then `ruinae`, then `innexus`. No screenshot case opens the overlay, so no image should change. If one does, find out why before committing.

**4. Acceptance**

- The new case is green.
- `grep -rn "class ADSRExpandedOverlayView" plugins/` finds exactly one declaration, in `plugins/shared/src/ui/`.
- `Grep` finds no `kCloseTag` in Ruinae or Innexus.
- All gated executables are green, and all three pluginval runs exit 0.
- Before and after, a hidden one-off case per plugin opens the overlay in a headless editor render (`tests/test_helpers/editor_screenshot.h`) and saves a PNG under `f:/tmp/membrum-phase6/overlay/`. Attach the before and after PNGs to the commit message as paths, to show the changed Close button in Ruinae and Innexus. Delete the cases before the commit.
- The Membrum state pin (T6.1) is green.

### T6.7 — Code comments match the code: drum_voice.h, voice_pool.h, state_codec.h, processor.{h,cpp}, plugin_ids.h

**Findings:** MB-125 (low, CONFIRMED), MB-126 (low, CONFIRMED), MB-127 (low, CONFIRMED), MB-128 (low, CONFIRMED), MB-129 (low, CONFIRMED), MB-070 (low, CONFIRMED), MB-135 (low, CONFIRMED), MB-219 (low, CONFIRMED).

**Flags:** none.

**Prerequisites:** none. In plan order this task runs after phases 1 to 5 and after T6.1 to T6.6, and those tasks rewrite much of the code these comments describe: T1.11 (pitch-envelope refresh), T1.8 (retirement safety net), T1.3 (controller save path), T1.20 (proxy routing), T1.22 and T2.8 (state version and read paths), T2.9 (note-off), T4.1 (coupling matrix), T4.25 (strike normalisation), T6.3 (registered defaults), T6.4 (dead API: `scratchR_`, `setChokeGroup`, `forEachMainVoice`, `editorOpen_`, the `controller.h` comments of MB-165). **Write every comment from the code as it is when this task runs.** The "today" text below is the text at `4a14376d`; use it to find the comment, not as the answer. Where an earlier task already corrected or deleted a comment, record that and move on.

**Estimate:** 150 min.

**Scope rule.** This task changes comments and adds `static_assert`s. It changes no executable line, so no rendered sample and no state byte moves (the phase 6 rule). Deleting dead members is T6.4's job; if T6.4 kept one, its comment says what it is (test-only, unused) instead.

**1. Failing test**

No Catch2 case can fail on a comment. The red check is a fixed-string search over the stale phrases, run before any edit:

```
git grep -n -F -e "ExciterBank -> BodyBank -> ToneShaper -> UnnaturalZone::NonlinearCoupling" -e "runs BEFORE the body each sample" -e "~14 refresh points" -e "~14 update points" -e "0.993 yields" -e "one sample late" -e "previous-block's primary output" -e "1.4 s at 48 kHz / 256 blocks" -e "Only Membrane supports mid-note" -e "1/sqrt(N)" -e "field-standard transparent path" -e "softClip(shaped * env * level)" -e "scaffolding only" -e "Phase 3.0 stubs" -e "bookkeeping only (FR-114)" -e "Phase 3 single-pad template" -e "broadcasting parameter changes" -e "stub is a no-op" -e "Phase 3.1 is a" -e "reserved for a future stereo" -e "exp(-1 / (0.005 * sampleRate))" -e "intentionally included in the 32-slot budget" -e "Current version: v3" -e "has not shipped" -e "carries 57 slots" -e "44 float64 sound params" -e "57 x float64" -e "468 bytes" -e "0=Acoustic, 1=Extended" -e "Reuse the first 51 sound slots" -e "State v4" -e "version = 4" -e "34 x float64" -e "are no-ops in the processor" -e "CouplingMatrixEdit" -e "for state only, not used in audio" -e "284-byte" -e "asymmetric linear ranges" -e "StringListParameter, 6 choices" -e "StringListParameter: Exp/Lin" -e "RangeParameter stepped [0,8]" -e "{Acoustic, Extended}" -e "Pad 0: 1000-1063" -e "Offsets 0-35 are active" -- plugins/membrum/src
```

Record the output. At `4a14376d` every phrase hits at least once (the `6 choices` phrase also hits `kBodyModelId`, which is correct: 6 bodies; only the `kExciterTypeId` line is wrong). Phrases an earlier task already removed are listed as such in the commit message. After the fix the command prints only the `kBodyModelId` line. The per-item defaults in `plugin_ids.h` (step 2, `plugin_ids.h` row) are checked by hand against `Controller::initialize`, because a default value is not a fixed phrase.

Add two compile-time guards next to the comments they back (they compile green; they stop the next drift):

- `plugins/membrum/src/state/state_codec.h`, after the pad preset layout comment above `writePadPresetBlob`: `static_assert(3 * sizeof(Steinberg::int32) + std::tuple_size_v<decltype(PadPresetSnapshot::sound)> * sizeof(double) == <bytes>, "pad preset blob size stated above");` where `<bytes>` is 484 for 59 sound slots (4 + 4 + 4 + 59 x 8). If a STATE-COMPAT task appended slots, compute it from the slot count; the comment, the assert and the three tests that pin the size (`tests/unit/preset/test_pad_preset.cpp` `kPadPresetBytes`, `tests/unit/state/test_state_codec.cpp` ~240-243, `tests/unit/processor/test_coupling_state.cpp` ~248-252) must agree.
- `plugins/membrum/src/plugin_ids.h`, after the per-pad layout comment (the block that says `Pad 0: 1000-1063`): `static_assert(kPadBaseId + (kNumPads - 1) * kPadParamStride + kPadActiveParamCountV12 - 1 == 5031, "last active per-pad ID stated above");` (pad 31 = 4968 to 5031 with stride 128 and 66 active offsets, `dsp/pad_config.h:25` and `:176`). If a later task added per-pad offsets or renamed the count constant, use the current constant and number.

**2. Fix**

Edit each comment to state what the code next to it does. Before / after, by file and symbol:

`plugins/membrum/src/dsp/drum_voice.h` (MB-125, MB-219):

| Symbol / anchor at `4a14376d` | Today | Write instead |
|---|---|---|
| File header, :3-19 | `ExciterBank -> BodyBank -> ToneShaper -> UnnaturalZone::NonlinearCoupling -> amp envelope -> level`; pitch envelope "runs BEFORE the body each sample" | The chain `processBlockFast` runs (:774-900): exciter + click layer (`ClickLayer::kStandaloneOutputGain`, half amplitude into the excitation, full amplitude into the output sum) -> `BodyBank` -> optional secondary shell bank (feedforward, mixed in additively) -> + noise layer (`standaloneGain() x noiseLayerGain_ x wireGainMod_`) -> `ModeInject` -> `NonlinearCoupling` -> `ToneShaper` (drive -> wavefolder -> DC blocker -> SVF with filter envelope) -> x amp envelope x level -> `Krate::DSP::hardClip`. NonlinearCoupling runs BEFORE the ToneShaper. Name the three renderers and that `processBlockSlow` serves only the Feedback exciter. State the pitch-envelope refresh as the code does it (next row). |
| `processBlock` path-selection comment, :613-619; `processBlockFast` chunk comment, :703-707 | "a 64-sample block refresh gives ~14 update points"; "~14 refresh points across a 64-sample audio block" | If T1.11 landed a fixed control grid: "the pitch envelope, tension and morph retune the bank every N samples (N = the grid constant), independent of the host block size", naming the constant and T1.11's test. If T1.11 stopped: "once per chunk = once per host block up to `kMaxBlockSize` (2048); a 20 ms sweep gets 1 to 2 refresh points at 512 samples per block". Never the 64-sample claim unless the code sub-chunks to 64. |
| `noteOff()`, :447-450 | "0.993 yields ~50 ms effective release" | The constant is `kNoteOffDampScale` = 0.997 (:64; the doc at :57-63 already says 0.997). If T2.9's ruling changed what a note-off does, describe that instead. |
| Secondary bank block in `processBlockFast`, :802-809 | "applied one sample late ... reads the previous-block's primary output" | Delete that sentence: the shell is driven by the same chunk's `bodyScratch` (:816-817). Keep the feedforward / no-return explanation at :814-841. |
| Safety net in `processBlock`, :663 | `// ≈1.4 s at 48 kHz / 256 blocks` | `silentBlockCount_` counts `processBlock` calls, so the net is 256 HOST blocks: 0.34 s at 64 samples / 48 kHz, 5.5 s at 1024. If T1.8 made it time-based, state the time and the constant. |
| Doc comment above `probeStrikePeak`, :1262-1266 | "Only Membrane supports mid-note parameter updates in Phase 2 ..." (orphaned: it sits above the probe, not above any update function) | Delete it. `updateModalParameters` (:1573) updates every modal body. |
| `noteOn` output-stage comments, :230-239 and :246-268 (MB-219) | "field-standard transparent path -- 1/N summation normalisation only" citing Faust / STK / Bilbao; "pre-scale the bank output by 1/sqrt(N)"; "Downstream `softClip(shaped * env * level)`" | What the block at :269-306 does: Feedback keeps output gain 1.0 and the 0.707 soft clip; String sets `kBodyHeadroom / getInputGainSum()` (the bank is not in its audio path); every other body gets `measuredStrikeOutputGain` = `kBodyHeadroom` / measured peak of a canonical strike, cached on the mapper inputs. Say plainly that this per-configuration measured-peak normalisation is a project design decision recorded as N-1 in `plugins/membrum/AUDIT-signal-path-2026-06-07.md`, and that the cited references do NOT do it: STK `Modal.cpp` keeps `masterGain_ = 1.0` with per-mode `setGain`, Faust `physmodels.lib` mode banks end in a fixed `:> /(nModes)`, and J.O. Smith's PASP "Modal Expansion" fits per-mode gains with no global renormalisation (report section 5 lists the URLs). The final voice bound is `Krate::DSP::hardClip` (:566, :885, :898, :1061), not a soft clip. If T4.25's ruling changed the normalisation, describe the shipped code. |
| `probeStrikePeakExcited` comment, :1301-1316 | First paragraph: "drive the bank with a local copy of the configured exciter ... Local instances leave the live exciter/click untouched"; the next paragraph says it reuses the LIVE exciter and click | One consistent statement: the live exciter and click layer are re-triggered for a 30 ms canonical strike at velocity 1.0 (half the click layer's output into the excitation), then re-triggered again for the note. |

`plugins/membrum/src/voice_pool/voice_pool.h` and `voice_pool.cpp` (MB-126, MB-135):

| Anchor at `4a14376d` | Today | Write instead |
|---|---|---|
| `voice_pool.h:39-40`; `:231` | "Phase 3.0: scaffolding only -- all methods are stubs"; "Query helpers (Phase 3.0 stubs)" | Delete the first; the second becomes "Query helpers". |
| `noteOff` doc, `voice_pool.h:115-117` | "Percussion note-off: bookkeeping only (FR-114) ..." | What `VoicePool::noteOff` (`voice_pool.cpp` ~283-300) does: calls `DrumVoice::noteOff` on every Active slot playing that note, which gates the amp envelope and damps the bank by `kNoteOffDampScale`. Or what T2.9's ruling made it. |
| `setChokeGroup` doc, `:151-153` | "Phase 3 single-pad template" | If T6.4 kept it: "test-only; writes the same group into all 32 table entries; production uses `setPadChokeGroup` / the per-pad `kPadChokeGroup` offset". If deleted, nothing. |
| `forEachMainVoice` block comment, `:182-191` | "broadcasting parameter changes to every main slot" | If still present: "test-only; a write made through it is overwritten at the next `noteOn` by `applyPadConfigToSlot`" (T7.1 relies on this being stated). If deleted, nothing. |
| `processChokeGroups` doc, `voice_pool.h:303-305`; `voice_pool.cpp:148-150` | "Phase 3.1 stub is a no-op"; "Phase 3.1 is a no-op stub so the code path compiles" | It is implemented (`voice_pool.cpp` ~1004): fast-releases every Active or releasing voice in the new note's choke group. |
| `scratchR_`, `:349-354` | "reserved for a future stereo DrumVoice" | If T6.4 kept it: "allocated in `prepare`, never read". If deleted, nothing. |
| `fastReleaseK_`, `voice_pool.h:358`; `applyFastRelease` comment, `voice_pool.cpp` ~970-972 | header: `exp(-1 / (tau * sampleRate))`; cpp: "The 5 ms time constant ... exp(-1 / (0.005 * sampleRate))" | The code (`voice_pool.cpp` ~85) computes `exp(-kFastReleaseLnFloor / (kFastReleaseSecs * sr))`: 5 ms is the total time to the 1e-6 floor, tau = 5 ms / ln(1e6) = 362 us. Two verifiers judged the header line defensible (tau is the time constant); write the explicit form in both places so neither can be misread. |
| Size-budget comment above `kVoicePoolSizeLimit`, `:376-389` | "the two `unique_ptr<std::array<DrumVoice, 16>>` heap blocks are intentionally included in the 32-slot budget" | `sizeof(VoicePool)` holds only the two `unique_ptr`s, so the `32 * sizeof(DrumVoice)` term never constrains anything and the assert cannot catch a heap-owning member. Say what it does bound (the inline members: meta arrays, allocator, choke table, `padConfigs_`). Leave the expression unchanged. |

`plugins/membrum/src/state/state_codec.h` and `state_codec.cpp` (MB-127, MB-135):

| Anchor at `4a14376d` | Today | Write instead |
|---|---|---|
| File header, :6-14 | "Current version: v3 (M-9). The plugin has not shipped ... carries 57 slots"; "Both Processor::getState/setState and the Controller ... route through the same write/read helpers" | The version is `kBlobVersion` / `kPadBlobVersion` (5 at `4a14376d`, 6 after T1.22); releases shipped (v0.9.0 to v0.12.5) and the reader accepts the versions T2.8 and T1.22 gave read paths (5, 1, 2), rejecting 3, 4 and anything newer. Slot count from `PadSnapshot::sound`. On the controller: say where the controller's save snapshot comes from as the code now does it (after T1.3: the processor's blob or the controller mirror; before T1.3 it was `buildPadSnapshotFromParams`, `controller_state_codec.cpp` ~117-181, which cannot represent slot 57 or the -1 damping sentinel). |
| `PadSnapshot` doc, :35-38 | "44 float64 sound params cover offsets 2-35, 42-49 and 50-51" | Point at the index map at :44-59, which is right; the count is the array size. |
| `KitSnapshot::uiMode`, :85 | "uiMode: 0=Acoustic, 1=Extended" | The registered labels: "Simple" / "Advanced" (`controller.cpp` ~556-557), with Acoustic / Extended as the internal names if the code still uses them. |
| Kit layout block, :120; pad layout block, :151-152 | "[57 x float64 sound]"; "Total: 468 bytes" | The real slot count and byte total (484 at 59 slots), backed by the `static_assert` of step 1. |
| `toPadPresetSnapshot`, `state_codec.cpp` ~262-266 | "Reuse the first 51 sound slots ... index 51 is intentionally excluded" | Delete; the comment at ~268-271 and the `std::copy_n` at ~272 copy all slots, and the load side ignores 28-29, 51 and 56. |

`plugins/membrum/src/processor/processor.h`, `processor.cpp`, `controller/controller.h`, `controller/controller.cpp` (MB-128, MB-070, MB-135):

| Anchor at `4a14376d` | Today | Write instead |
|---|---|---|
| `processor.cpp` file header, :4-7 | "Global proxy IDs (100-252) are no-ops in the processor (controller-only proxies)" | What `processParameterChanges` does with global proxies after T1.20 (at `4a14376d` it forwards about 60 of them to `selectedPadIndex_`, :267-522). |
| Comment block above `Processor::getState`, `processor.cpp` ~854-869 | "State v4 ... version = 4 ... [34 x float64] sound params" | One line: the layout is `State::writeKitBlob`, documented in `state/state_codec.h`. |
| `notify` doc, `processor.h:68-71` | handles "CouplingMatrixEdit" / "CouplingMatrixSnapshotRequest", replies "CouplingMatrixSnapshot" | The message IDs `Processor::notify` compares against now (at `4a14376d`: "AuditionPad", "AuditionPadOff", "LoadPresetState", "LoadPadPreset", `processor.cpp` ~1186-1247), and anything else goes to `AudioEffect::notify`. If T6.4 already fixed this doc (MB-134 names it), verify and leave. |
| `recomputeCouplingMatrix` doc, `processor.h:106-110` | "baked into effectiveGain (FR-014, FR-023)" | True only if T4.1 wired `effectiveGain` into the audio path; then name the consumer. If not, say the matrix is computed and not read by the audio path. |
| `selectedPadIndex_`, `processor.h:135` | "(for state only, not used in audio)" | It decides which pad every global-proxy write reaches (if T1.20 kept that routing) and is saved in the kit blob. Grep its uses: at `4a14376d` the audio thread writes it (:398) while the message thread reads it in `notify("LoadPresetState")` (:1238) and writes it via `applyKitSnapshot` (:948), a race of the MB-001 class that T1.1 owns. If any message-thread access remains after phase 1, do not patch it here (it is a behaviour change outside phase 6): write the comment to name the remaining access and report it as an open item of T1.1. |
| `controller.h:168`; `controller.h:266-270` and `controller.cpp` ~1417 | "Produces a 284-byte pad preset blob"; "asymmetric linear ranges (attack x500 ms, decay/release x2000 ms, see processor.cpp:72-75)" | 484 bytes (or the step 1 number); the filter envelope decodes cubically, `norm^3 x 500 / 2000 / 2000 ms` (`pad_config_apply.h` ~91-97). These lines are also MB-165 (T6.4): if T6.4 fixed them, verify and leave. |

`plugins/membrum/src/plugin_ids.h` (MB-129). Read each registration in `Controller::initialize` (`controller.cpp`) and the per-pad spec table, and write the comment from them (after T6.3 the defaults may have moved):

| Line at `4a14376d` | Today | Truth at `4a14376d` (re-check) |
|---|---|---|
| :61 `kExciterTypeId` | "StringListParameter, 6 choices" | 7 choices, Clap last (`controller.cpp` ~298-308). |
| :65 `kExciterNoiseBurstDurationId` | "(2..15, default 5)" | Per-pad default 0.5 norm = 8.5 ms; give the registered range and default. |
| :66 `kExciterFrictionPressureId` | "default 0.3" | 0.0 (`controller.cpp` ~341, `pad_config.h` ~237). |
| :75-76 PitchEnv Start / End | "default 160" / "default 50" | Proxy defaults 0.070721 / 0.0 norm (`controller.cpp` ~347-348); per-pad 0.0 = 20 Hz. Write Hz values from the registration. |
| :78 `kToneShaperPitchEnvCurveId` | "StringListParameter: Exp/Lin" | Continuous `RangeParameter` 0..1, default 0.5 = linear (`controller.cpp` ~430-434; the file's own :173-175 says so). |
| :104 `kChokeGroupId` | "RangeParameter stepped [0,8]" | `StringListParameter` None, CG1..CG8 (`controller.cpp` ~484-489). |
| :114-117 coupling knobs | "RangeParameter [0.0, 1.0]" | 0..100 % (`controller.cpp` ~511-527); Coupling Delay 0.5..2 ms. |
| :123 `kUiModeId` | "{Acoustic, Extended}" | "Simple", "Advanced" (`controller.cpp` ~556-557). |
| :208-210 per-pad layout | "Pad 0: 1000-1063 ... Pad 31: 2984-3047. Offsets 0-35 are active; 36-63 reserved" | Stride 128: pad 0 = 1000-1127, pad 31 = 4968-5095; offsets 0-65 active (`kPadActiveParamCountV12` = 66), 66-127 reserved. Add one sentence: released v0.9.x / v0.10.x used a 64-wide stride, so their per-pad automation IDs differ (MB-119; T2.8 records it in the CHANGELOG). |
| :229-230 `static_assert` message | "Pre-release codec is pinned at state version 5" | After T1.22 the constant and the message say 6 and the word "pre-release" goes (releases exist). If T1.22 already changed it, leave it. |

**3. Build and gate**

Build targets `Membrum` and `membrum_tests` (headers under `plugins/membrum/src/` changed). The gate runs the cases these headers reach (`--scope affected` is the default); every modified header here is Membrum-local, so no other executable is involved. Skip pluginval when the non-comment diff (step 4) shows only the two `static_assert`s: pluginval cannot see a comment. Run clang-tidy as usual.

**4. Acceptance**

- The step 1 `git grep` prints only the `kBodyModelId` line (it printed every phrase before; the commit message lists both runs, with the phrases an earlier task had already removed).
- `git diff -U0 -- plugins/membrum/src | grep -E '^[+-]' | grep -vE '^[+-]{3}|^[+-]\s*(//|$)'` lists only the two `static_assert` lines (and continuation lines of their messages). Anything else is out of scope for this task: revert it.
- Zero warnings; `GATE GREEN`. No rendered sample and no state byte can move, since no executable line changed.
- Every row above was checked against the code at execution time; rows another task had already fixed are named in the commit message with that task's id.

### T6.8 — MANUAL.md describes the engine that ships, including what an aux bus carries

**Findings:** MB-121 (low, CONFIRMED), MB-060 (low, CONFIRMED).

**Flags:** none.

**Prerequisites:** none. In plan order it runs after phases 1 to 5, which change much of what the manual describes (T4.1 the coupling model, the phase 4 mode tables and air loading, T5.3 to T5.5 the weak parameters by ruling, T1.23 the damping encoding, the phase 1 exciter-swap allocation fix). Write the manual from the code as it is when this task runs; the item list below says what was wrong at `4a14376d`.

**Estimate:** 240 min.

**1. Failing test**

New file `plugins/membrum/tests/unit/docs/test_docs_match_engine.cpp`, added to the `membrum_tests` source list in `plugins/membrum/tests/CMakeLists.txt`. Pattern: `plugins/disrumpo/tests/unit/manual_coverage_test.cpp` (it reads its manual through the resources-dir compile definition and takes names from the controller, never from a hand-written list). Membrum's tests already get `MEMBRUM_RESOURCES_DIR` (`plugins/membrum/tests/CMakeLists.txt` ~225-228); the manual is `MEMBRUM_RESOURCES_DIR "/../MANUAL.md"`. Read the file with `std::ifstream` in binary mode; compare with spaces removed where a registered name is compact (`NoiseBurst`, `FMImpulse`).

`TEST_CASE("Membrum docs: MANUAL.md states the exciters, bodies, mode counts, curves and meter the engine has", "[docs][manual]")` asserts:

1. A `Membrum::Controller` is initialised (`initialize(nullptr)`); for every step of the `kExciterTypeId` and `kBodyModelId` string lists (`getParamStringByValue`), `**<name>**` appears in MANUAL.md, spaces ignored. Clap is missing today: RED.
2. The manual contains `<ExciterType::kCount x BodyModelType::kCount> base voice` (42 at `4a14376d`) and not `36 base`.
3. The line that starts with `| **Noise Body** |` contains `std::to_string(Membrum::Bodies::NoiseBodyMapper::kNoiseBodyModeCount)` (64; today "40 + broadband": RED). Same for the Shell and Bell rows with `Membrum::Bodies::ShellMapper::kModeCount` and `Membrum::Bodies::kBellModeCount` (these two match today and guard the next drift).
4. The line that contains `**Morph Curve**` names every string of the `kMorphCurveId` list (Linear, Exponential) and contains neither `logarithmic` nor `S-curve`, case-insensitive (RED today).
5. The line that contains `**Meter**` does not contain `RMS` (`processor/meters_block.h` carries peaks only; RED today).
6. The Pad Browser sentence names every entry of `Membrum::padPresetConfig().subcategoryNames` (`preset/membrum_preset_config.h` ~38-41: Hat and Tonal are missing today: RED).
7. The manual mentions the main-bus limiter: it contains `dBTP` (RED today: the manual's only "limiter" is the coupling energy limiter).

Build, run it by name, confirm it fails on items 1, 3, 4, 5, 6 and 7.

**2. Fix**

Edit `plugins/membrum/MANUAL.md` only (prose, tables, the ASCII diagram). Check each item against the code at execution time; anchors are the manual's lines at `4a14376d` (530 lines).

| # | Manual today | Write instead (code at `4a14376d`; re-check) |
|---|---|---|
| 1 | :5 and :39-40 six exciters, "36 base voice fabrics"; the exciter table :218-225 lists six | Seven exciters including Clap (`dsp/exciter_type.h` `kCount = 7`), 42 combinations; add a Clap row to the table with its real secondary control (read `exciters/clap_exciter.h` and the per-pad specs). |
| 2 | Diagram :54-59 puts "Noise Layer + Click Layer (added in parallel)" below the Tone Shaper | Body + noise + click are summed BEFORE ModeInject, NonlinearCoupling and the whole Tone Shaper (`drum_voice.h` ~882-896): the filter and drive shape the noise and click too. The click also enters the excitation at half amplitude. |
| 3 | :56 "Pitch envelope updates body fundamental per-sample" | The refresh as T1.11 left it (a fixed control grid, or once per host block if T1.11 stopped); same wording as T6.7's `drum_voice.h` header. |
| 4 | Diagram omits the main-bus limiter | Insert "Main-bus true-peak limiter, -1 dBTP (`busLimiter_`, `processor.cpp` ~717)" between the coupling return and the master gain, plus a row in the master section. Add the look-ahead and latency only if T4.26 was ruled in. |
| 5 | :193-196 Shell "Cylindrical-shell modes", Bell "Chladni patterns", Noise Body "40 + broadband" | Shell = free-free bar partial series (`bodies/shell_mapper.h:10-16` says it is not a cylindrical shell); Bell = Hibberts church-bell partials, extended by his Chladni fit (`bodies/bell_modes.h:13-20`), not a Chladni-pattern plate; Noise Body = `kNoiseBodyModeCount` (64) modes plus broadband noise. Use what the phase 4 mode-table tasks left. |
| 6 | :221 Mallet's secondary control "Burst Duration" | `MalletExciter` reads no duration; `noiseBurstDuration` feeds only NoiseBurst. Give Mallet its real controls, or what T5.3 to T5.5 ruled. |
| 7 | :252-257 shell bank "exchanges energy bidirectionally", "clamped to a 0.25 effective maximum" | Feedforward body -> shell, shell output mixed in additively, no return path (`drum_voice.h` ~814-843); Coupling Strength maps over the full 0..1 range (~1494-1497, audit L-7). |
| 8 | :263 "Depth scales by velocity² at note-on" | The velocity² term was removed: depth = knob x `kTensionEnergyScale` (`drum_voice.h` ~408-421); the glide follows the modal energy, which already grows with the strike. |
| 9 | :241 air loading "low-mode frequency depression (5% at the fundamental ...)" | The table holds the fundamental and raises the (m,1) modes by 12 to 27 % (`bodies/membrane_modes.h` ~94-105), or what the phase 4 air-loading task left; never "sub-bass". |
| 10 | :362 Morph Curve "Linear, exponential, logarithmic, S-curve" | Linear and Exponential (`controller.cpp` ~400-404). |
| 11 | :450 Meter "Peak + RMS" | Peak only (`MetersBlock` peakL / peakR), plus active voices and CPU if the editor shows them. |
| 12 | :393 Pad Browser "Kick, Snare, HiHat, Tom, Cymbal, Perc, FX" | Kick, Snare, Tom, Hat, Cymbal, Perc, Tonal, FX (`padPresetConfig()`). |
| 13 | :349 Nonlinear Coupling "Energy-limited cross-mode coupling ... FM-like sidebands" | An envelope-driven reciprocal-square-root waveshaper with no limiter (`unnatural/nonlinear_coupling.h` ~6-38). |
| 14 | :315 Drive "Alias-safe waveshaper" | ReciprocalSqrt waveshaper without ADAA (`tone_shaper.h` ~96-102), unless a later task added anti-aliasing. |
| 15 | :426-437 coupling "mixed into other pads' excitation paths"; :431-432 Snare Buzz / Tom Resonance described as audible | What T4.1 built (receiver model, or removal by ruling). If T4.1 stopped, say they currently have no audible effect and that the coupling return is a SympatheticResonance signal added to main L/R; describe the energy limiter as it is after T2.26 and T7.2 only if T7.2 has landed. |
| 16 | :505 "Zero allocations on the audio thread. ExciterBank and BodyBank pre-allocate every variant up front" | True only if the phase 1 exciter-swap task removed the swap-time `prepare` allocation (`exciter_bank.h` ~206-235 -> `DelayLine::resize`); check with grep and write what holds. |
| 17 | :237 "Legacy Decay and Material still work ... when the Phase 8A knobs are at their default position" | After T1.23: the encoding the user ruled. At `4a14376d` the registered default 0.5 is an explicit override, while the engine default is the -1 sentinel (`controller.cpp` ~227-228 vs `pad_config.h` ~282-283), so the sentence is false. |
| 18 (MB-060) | Diagram :67-72 forks "0 = main, 1-15 = aux" into either Main or Aux; :374 Output Bus "1-15 = aux stereo buses for separate DAW processing"; :527 "without affecting the rest of the kit" | An aux bus is a SEND, not a route (spec 139 FR-041 / FR-042, `specs/139-membrum-phase4-pads/spec.md` ~272-273): the pad always stays in the main mix and is additionally written to its aux bus (`voice_pool.cpp` ~483-504). The aux copy is post-pan, pre-coupling, pre-limiter and pre-master: at the default master gain (-6 dB) it is 6 dB above that pad's contribution to main, a hard-panned voice can reach +3 dBFS on one aux channel (pan law sqrt2 x hardClip 1.0, `panGainsForNote`), and coincident hits on one aux bus are not limited. Redraw the diagram so every voice reaches the main sum and the selected aux bus additionally. In the :527 tip, say how to get an aux-only pad: mute the main output in the DAW and rebuild the mix from aux buses, or use the aux for a parallel effect. Re-check whether T4.26 delays the aux buses. |

Keep the manual's heading structure and the screenshot references (`plugins/membrum/images/`); `tools/gen-site.js` renders the manual with pandoc and links those images.

**3. Build and gate**

Docs plus one test TU: build `membrum_tests` only. Run the new case by name, then the standard gate. Tests that read non-C++ files are consumers too: run `git grep -l -e "MANUAL.md" -- plugins/membrum/tests tools/site/tests` and include every hit in the run (`editor_screenshot_test.cpp` writes the manual's images and does not read the text; `tools/site/tests/gen-site.test.js` runs through `node --test "tools/site/tests/*.test.js"`). Render the site into a scratch directory: `node tools/gen-site.js --out f:/tmp/membrum-site` (exit 0 = written and lint-clean); without pandoc add `--no-manuals` and write "manual HTML not rendered locally" in the commit message. Then `node tools/lint-site-seo.js`. No pluginval (docs only).

**4. Acceptance**

- `Membrum docs: MANUAL.md states the exciters, bodies, mode counts, curves and meter the engine has` went RED (failing items recorded) and is GREEN.
- `git grep -n -F -e "36 base" -e "bidirectionally" -e "0.25 effective" -e "velocity²" -e "depression" -e "S-curve" -e "Peak + RMS" -e "HiHat" -e "Energy-limited" -e "Alias-safe" -e "pre-allocate every variant" -- plugins/membrum/MANUAL.md` prints nothing, unless the code now makes one of those words true (say which, and why, in the commit message).
- Each of the 18 rows is listed in the commit message with the code line it was checked against at execution time.
- `node tools/gen-site.js --out ...` exits 0, `node tools/lint-site-seo.js` exits 0, `GATE GREEN`.

### T6.9 — The landing page and the signal-flow diagram match the engine

**Findings:** MB-122 (low, CONFIRMED), MB-123 (low, CONFIRMED).

**Flags:** none.

**Prerequisites:** none. It runs after T6.8 in plan order and uses the same facts; reuse the wording T6.8 settled. Both files are published on krateaudio.com by `tools/gen-site.js` (the landing template, and `/membrum/signal-flow.html` rendered from the `.dot`, which MANUAL.md links).

**Estimate:** 150 min.

**1. Failing test**

Extend `plugins/membrum/tests/unit/docs/test_docs_match_engine.cpp` (created by T6.8) with two cases that read `MEMBRUM_RESOURCES_DIR "/../docs/index.html"` and `"/../docs/membrum_signal_flow.dot"`:

`TEST_CASE("Membrum docs: the landing page states the exciter count, names and mode counts the engine has", "[docs][site]")`:
- every `kExciterTypeId` string (spaces ignored) appears in index.html; `<ExciterType::kCount> exciter` appears (in the meta description, the og description and the features list) and `6 exciter` / `six excitation` do not (RED: :7, :10, :66, :74, :100);
- the Noise Body list item contains `kNoiseBodyModeCount` and the page contains neither `40-mode` nor `40 modes` (RED: :75, :120);
- the page contains none of `bidirectional`, `Single-sample`, `cylindrical`, `pre-allocate every variant` (case-insensitive), unless the code now makes one true (RED: :78, :102, :117, :93).

`TEST_CASE("Membrum docs: the signal-flow diagram draws the limiter, Clap, the send routing and the feedforward shell", "[docs][site]")`:
- a node label contains `dBTP` (RED: no limiter node);
- `Clap` appears (RED: :121-135);
- none of `shell\nfeedback` (the edge label at :231, as written in the file), `Bidirectional`, `fan out`, `velocity^2`, `per-sample\nfundamental`, `RMS`, `label="bus = 0"`, `per-pad excitation` appears (RED: :231, :173, :61, :175, :239, :316, :338, :270, :292).

Build, run both by name, confirm both fail.

**2. Fix**

`plugins/membrum/docs/index.html` (MB-122), anchors at `4a14376d`; keep `{{HEAD_COMMON}}`, `{{HERO}}`, `{{OTHER_PLUGINS}}` and the markup structure:

| Line | Today | Write instead (re-check against the code) |
|---|---|---|
| :7, :10 meta and og description | "32 pads, 6 exciters, 6 body models, ..." | 7 exciters. Keep the description 50 to 160 characters (`tools/lint-site-seo.js`). |
| :66, :74, :100 | six exciters, no Clap | Seven, Clap listed, with a one-line description from `exciters/clap_exciter.h`. |
| :75, :120 | Noise Body "40 modes" / "40-mode hybrid" | `kNoiseBodyModeCount` (64) modes plus broadband noise. |
| :117 | "Shell -- 32 cylindrical-shell modes" | Free-free bar partials (`bodies/shell_mapper.h:10-16`). |
| :119 | "Bell -- 16 Chladni-pattern bell modes with metallic damping (b3 = 0)" | Hibberts bell partials; brightness 0.7 + 0.3 x material (`bodies/bell_mapper.h` ~53), so b3 is non-zero below material 1. |
| :118 | "String -- Waveguide string body with mode-stretch control" | `StringMapper` ignores modeStretch: drop the claim, or state what T4.2 / T4.3 left. |
| :78 | "bidirectional energy exchange. Stability-clamped two-bank feedback loop" | Feedforward body -> shell, mixed in additively (same wording as T6.8 row 7). |
| :77, :115 | air loading "depresses the lowest modes for realistic kick/tom sub-bass" | Same as T6.8 row 9. |
| :89 | "toms ring the snare wires, kick excites the floor tom" | What T4.1 built; at `4a14376d` the matrix knows only Kick->Snare and Tom->Tom and is not read by the audio path. |
| :93 | "Zero allocations ... pre-allocate every variant up front" | Same as T6.8 row 16. |
| :102 | Impulse "Single-sample velocity-scaled impulse" | A short Hertzian contact pulse of 0.5 to 15 ms (`exciters/impulse_exciter.h` ~6-39 wraps `ImpactExciter`). |
| :103 | Mallet "velocity-controlled contact time" | Contact time comes from mallet mass and the size hint (`exciters/mallet_exciter.h` ~51-58). |

`plugins/membrum/docs/membrum_signal_flow.dot` (MB-123). Redraw from `Processor::process` (`processor.cpp` ~620-742) and `DrumVoice::processBlockFast` (`drum_voice.h` ~774-900):

| Line | Today | Write instead |
|---|---|---|
| :61 | "Global proxies fan out to all 32 pads" | Global proxies write the selected pad only, or what T1.20 left. |
| :121-135 | six exciters | Add an `ex_clap` node and its edge from `exciter_switch`. |
| :173, :231 | "Bidirectional energy exchange ... Stability-clamped < 0.25"; edge `secondary -> body_switch` labelled shell feedback | Feedforward label, coupling 0..1; delete the feedback edge; draw body -> secondary -> additive mix into the body output. |
| :175 | "Depth scaled by velocity^2" | Knob x `kTensionEnergyScale`, follows modal energy. |
| :239 | "per-sample fundamental" | The refresh T1.11 left. |
| :244-245 | noise and click added at the SVF | Sum body + noise + click before ModeInject; edges into the node that feeds ModeInject (or NLC, then the tone shaper chain). Click also into the excitation at half amplitude. |
| :270-271 | `bus = 0 -> main`, `bus > 0 -> aux` | Every voice -> main accumulator; additionally -> aux accumulator when bus > 0 and active (send). |
| :292, :299-300 | "Mono sum -> matrix -> per-pad excitation" | The coupling path T4.1 built (at `4a14376d`: mono sum -> delay -> SympatheticResonance -> energy limiter -> added to main L/R; the matrix is not in it). |
| :322-323 | `main_accum -> master_gain` | `main_accum -> coupling return -> bus_limiter (-1 dBTP TruePeakLimiter) -> master_gain`; aux accumulator bypasses all three. |
| :316, :338 | "Peak / RMS publisher" | Peak publisher (plus active voices, CPU, pad glow). |
| :149, :151, :152 | Shell "32 cylindrical modes", Bell "16 Chladni modes ... b3=0", Noise Body "40 modes" | As in the index.html rows. |

**3. Build and gate**

Docs plus the test TU: build `membrum_tests` only; run the two cases by name, then the standard gate. Then `node tools/gen-site.js --out f:/tmp/membrum-site` (renders the `.dot` with @viz-js/viz, so a syntax error exits non-zero), `node tools/lint-site-seo.js`, and `node --test "tools/site/tests/*.test.js"`. Open `f:/tmp/membrum-site/membrum/signal-flow.html` in a browser once and check that every edge renders. No pluginval, no screenshots (the editor did not change).

**4. Acceptance**

- Both cases went RED (failing assertions recorded) and are GREEN.
- The meta description length (characters) is recorded and lies within 50 to 160; `lint-site-seo.js` exits 0; `gen-site.js` exits 0; the site tests pass.
- Each row of both tables is listed in the commit message with the code line it was checked against.

### T6.10 — plugins/membrum/CLAUDE.md, the membrum skill and specs 135 / 140 / 141 state what the code does

**Findings:** MB-124 (low, CONFIRMED), MB-184 (low, CONFIRMED), MB-130 (low, CONFIRMED).

**Flags:** none.

**Prerequisites:** none.

**Estimate:** 90 min.

**Ruling needed before the commit** (the plan's ruling table): the documents call two behaviours load-bearing that the engine does not have. Friction is a transient bow: A 1 ms, D 40 ms, S 0, R 5 ms and a forced gate-off at `0.041 * sr` (`dsp/exciters/friction_exciter.h` ~38-44, ~112-115; header ~6-9: "Sustained bowing is out of scope"). ModeInject decays: a one-pole T60 envelope (`dsp/unnatural/mode_inject.h` ~84-93, ~142-143, ~148-153) set per note to `0.3 + 4.7 x decay^2` s (`drum_voice.h` ~331), default 1.5 s. The probe measured both silent (-240 dBFS) after 2 s (report section 3, MB-184). Ask: correct the documents, or build a sustaining bow and an undamped inject as a feature? The plan's default is to correct the documents; building the behaviour is a separate feature request and not part of this plan. Prepare the change below, show the diff and the sweep result, and commit only after the answer.

**1. Failing test**

The documents cannot fail a test, but the premise reached one: `plugins/membrum/tests/unit/processor/test_kit_switch_infinite_ring.cpp`, `runPerPadSustainTest` (~440), skips every Friction pad (~460-466: "Friction sustains WITHOUT note-off by design ... continue;"), and the sweep comment (~551-552) repeats it. So no factory Friction pad is ever checked for "silent by 8 s" and "geometric decay" (~498-503).

- Delete the Friction `continue` and its comment; rewrite the sweep comment at ~551-552 to say every exciter is swept. The cases keep their names and tags: `Per-pad single hit decays to silence on <kit>` and `Per-pad single hit decays to silence on every <Category> kit (NoteOn-only)` (`[membrum][processor][infinite_ring][regression][factory][per_pad]`, three of the sweeps also `[all_kits][long]`).
- This widens coverage; it is not expected to go RED. The baseline (`specs/membrum-audit/probes/kit-pads.csv`): 38 enabled Friction pads in 11 kits (Cajon and Frames, Chaos Engine, Drone and Sustain, Experimental FX Kit, Ghost Bones, Glass Bell Garden, Latin Perc, LinnDrum CR-78, Modular West Coast, Tabla, World Metal), decay 0.25 to 2.10 s, except Modular West Coast note 37 and Experimental FX Kit note 39, which measured `>4` s (the probe window).
- If any Friction pad fails, the failure is a real long ring (body or coupling) that the skip was hiding, which is MB-124's stated consequence. Do not put the skip back: stop, report the kit, note and window peaks, and continue with the next task. Phase 6 changes no audio, so a fix belongs to a new task after the user has heard it.
- The red check for the documents is a search, run before any edit:

  ```
  git grep -n -F -e "rings undamped" -e "rings UNDAMPED" -e "Friction sustains" -e "sustains WITHOUT" -e "design-sustain" -e "current active-development plugin" -e 'no `update/` dir' -- plugins/membrum/CLAUDE.md .claude/skills/membrum/SKILL.md plugins/membrum/tests tools/membrum_preset_generator.cpp
  ```

  At `4a14376d` it hits CLAUDE.md :4, :8, :23-24; SKILL.md :9, :26-27; the test ~460 and ~551; the generator ~2549, ~2629, ~2751. After the fix it prints nothing.

**2. Fix**

- `plugins/membrum/CLAUDE.md`: line 4 says "This is the current active-development plugin": the root `CLAUDE.md` names Seraphis; delete the line. Lines 7-8 say "no `parameters/` dir, no `update/` dir": `src/update/membrum_update_config.h` exists (the root rule requires it); list `update/` in the skeleton and keep "no `parameters/` dir". Lines 23-25 (ring constraints): `maxPolyphony` in [4,16] stays; replace the other two clauses with what the code does: "Friction is a ~41 ms transient bow (A 1 ms, D 40 ms, forced gate-off at 41 ms); any sustain is the body ringing out. `modeInject > 0` decays with the drum: T60 = 0.3 + 4.7 x decay^2 s. A sustaining drone needs a long-decay body (String, Bell), not Friction or ModeInject. The infinite-ring sweeps cover every exciter." (If the user ruled to build the behaviour instead, leave these clauses and record that the behaviour is a pending feature.)
- `.claude/skills/membrum/SKILL.md`: line 9 ("the current active-development plugin") and lines 26-28: the same corrections.
- `tools/membrum_preset_generator.cpp` ~2549, ~2629, ~2751: comments only ("inject rings UNDAMPED (no envelope)", "rings undamped"). Say that inject decays with Decay; keep the values. A comment-only generator change produces byte-identical kits (step 3 proves it).
- `.claude/workflows/membrum-audit.js` ~155 (the audit brief repeats the constraint as INTENDED): correct it the same way if the file is tracked when this task runs (it was untracked at `4a14376d`); otherwise leave it.
- Do not rewrite history: `plugins/membrum/preset-retune/**`, `docs/tom-depth-implementation-plan.md`, `CRASH-REDESIGN-PLAN.md` and the two prior audits stay as they are.
- Specs (MB-130): mark, do not delete. Each note reads "Removed in 0.9.0: the 32x32 matrix UI, the Tier 2 per-pair override layer and Solo (see `plugins/membrum/CHANGELOG.md`, [0.9.0], Removed)".
  - `specs/135-membrum-drum-synth.md`: line 3 status; the Phase 5 and Phase 6 rows at :16-17 (append the note to the scope cells that name "Tier 2 per-pair overrides" and "Tier 2 coupling matrix editor"); the Tier 2 section at :96-100; the decision at :790. Add a Version History row dated the commit day: "Tier 2 coupling matrix editor and overrides marked removed (0.9.0)".
  - `specs/140-membrum-phase5-coupling/spec.md`: FR-034 at :223 (the override clause); the compliance rows FR-030 (:353, cites `overrideGain_` / `hasOverride_`, which no longer exist) and FR-034 (:357). Change their status to "MET (v0.5.0); override part REMOVED in 0.9.0" and point FR-030 at `coupling_matrix.h` as it is now. If T4.1 rewired the matrix, cite T4.1's test as the evidence of what is MET today.
  - `specs/141-membrum-phase6-ui/spec.md`: User Story 6 (:136-149), FR-050 to FR-053 (:256-259), risk R6 (:420) and the compliance rows (:458-461, which cite `src/ui/coupling_matrix_view.cpp` and `src/dsp/matrix_activity_publisher.h`, both gone): status "REMOVED in 0.9.0".

**3. Build and gate**

Test-only, docs and generator comments: build `membrum_tests` and `generate_membrum_presets`. Run the infinite-ring cases explicitly so the `[long]` sweeps run too: `node tools/run-gate.js --share 4 --filter '[infinite_ring]' membrum_tests`, then the standard gate. Regenerate the kits with the generator command of the PRESET-IMPACT flag and confirm `git status --porcelain "plugins/membrum/resources/presets"` prints nothing (the generator change is comment-only). No pluginval, no kit-quality probe.

**4. Acceptance**

- The user's ruling is recorded in the commit message.
- The `[infinite_ring]` run is `GATE GREEN` with the Friction skip gone; the commit message gives the case count before and after and names the two `>4` s pads with their measured window peaks at 8 s (below `kSilenceThreshold` in that file).
- The step 1 `git grep` printed its hits before and prints nothing after.
- The kits are byte-identical after regeneration; `GATE GREEN` for the standard gate.

## Phase 7 — Test-gap fills

Replace tests that cannot fail or that measure the wrong thing with tests that pin the real behaviour.

### T7.1 — The SC-023 polyphony benchmark measures the real worst case

**Findings:** MB-120 (medium, CONFIRMED).

**Flags:** none.

**Prerequisites:** none. T6.4 (earlier in plan order) may have deleted `VoicePool::forEachMainVoice` or `mainVoiceRef` and migrated their test callers; if it already moved the benchmark and the stress test to pad-config writes, this task reduces to the guard test of step 1 and the timing run, and the commit message says so.

**Estimate:** 120 min.

**What is wrong.** `configurePoolWorstCase` (`plugins/membrum/tests/perf/test_polyphony_benchmark.cpp:99-168`) writes the Tone Shaper (drive 0.5, fold 0.3, LP 3 kHz, filter envelope, pitch envelope 160 to 50 Hz over 20 ms) and the Unnatural Zone (stretch 1.2, skew 0.3, inject 0.3, nonlinear coupling 0.3, morph 0.2 to 0.8 over 300 ms) straight into each `DrumVoice` through `pool.forEachMainVoice`, then triggers with `pool.noteOn` (:205, and on every retrigger at :245). `VoicePool::noteOn` calls `applyPadConfigToSlot` (`voice_pool.cpp` ~232), and `applyPadConfigToVoice` (`dsp/pad_config_apply.h` ~76-143) rewrites every tone-shaper and Unnatural field from the pad config, whose defaults are neutral (`pad_config.h` ~196-218). So the "TS on / UN on" cell runs with `ToneShaper::isBypassed()` true and the Unnatural Zone inert. `configureStressPool` (`tests/perf/test_polyphony_stress_16.cpp:50-92`, triggered at :116 and in the loop) has the same defect for SC-024.

**1. Failing test**

- Move `configurePoolWorstCase` unchanged (still using `forEachMainVoice`) into `plugins/membrum/tests/unit/voice_pool/voice_pool_test_helpers.h` as `inline void setAllPadsWorstCasePatch(VoicePool& pool, ExciterType ex, BodyModelType body, bool toneShaperOn, bool unnaturalOn)` in `Membrum::TestHelpers` (grep the name first: ODR). Call it from the benchmark and, with `(Feedback, NoiseBody, true, true)`, from the stress test in place of `configureStressPool`.
- New file `plugins/membrum/tests/unit/voice_pool/test_bench_patch_config.cpp`, added to the `membrum_tests` source list: `TEST_CASE("Bench patch: the worst-case Tone Shaper and Unnatural Zone reach the voices", "[voice_pool][bench_config]")`. It is not a timing case and runs in every gate.
  - Four pools at 44.1 kHz, block 128, polyphony 8, Feedback + NoiseBody, configured with (TS, UN) = (off, off), (on, off), (off, on), (on, on). Each plays the benchmark's chord (notes 36 to 43, velocity 0.8) and renders 0.5 s; keep the main L output.
  - Assert `rmsDiffDb(on/off variant, off/off) > -20.0` for each of the three variants, with `rmsDiffDb = 20 log10(rms(a - b) / rms(b))` (`tests/test_helpers/signal_metrics.h` if it has an RMS helper; read the header first). Today the four renders are equal because every variant is reset to the pad defaults at note-on: RED.
  - If `mainVoiceRef` still exists, also check slots 0 to 7 of the (on, on) pool after the first block: `isActive()`, `!toneShaper().isBypassed()`, `toneShaper().isPitchEnvActive()`, `unnaturalZone().modeInject.getAmount() == Approx(0.3f)`, `nonlinearCoupling.getAmount() == Approx(0.3f)`, `materialMorph.isEnabled()`.
- Build, run by name, record the measured differences (expected: no difference at all).

**2. Fix**

Rewrite the body of `setAllPadsWorstCasePatch` to write every value through `pool.setPadConfigField(pad, offset, normalised)` for all 32 pads, so `applyPadConfigToSlot` carries it into each voice at note-on. Material, size, decay, strike position and level stay on `setAllPadsVoiceParams` (0.5, 0.5, 0.7, 0.3, 0.8); exciter and body stay on the selector helpers. The normalised values follow the decode in `applyPadConfigToVoice`:

| Offset (`dsp/pad_config.h`) | Benchmark value | Normalised value (decode) |
|---|---|---|
| `kPadTSFilterType` (7) | Lowpass | 0.0 (`int(norm x 3)`, Lowpass = 0) |
| `kPadTSFilterCutoff` (8) | 3000 Hz | 0.725364 (`20 x 1000^n`) |
| `kPadTSFilterResonance` (9) | 0.5 | 0.5 |
| `kPadTSFilterEnvAmount` (10) | 0.6 | 0.8 (`2n - 1`) |
| `kPadTSFilterEnvAttack` (17) | 1 ms | 0.125992 (`n^3 x 500`) |
| `kPadTSFilterEnvDecay` (18) | 150 ms | 0.421716 (`n^3 x 2000`) |
| `kPadTSFilterEnvSustain` (19) | 0.2 | 0.2 |
| `kPadTSFilterEnvRelease` (20) | 200 ms | 0.464159 (`n^3 x 2000`) |
| `kPadTSDriveAmount` (11) | 0.5 | 0.5 |
| `kPadTSFoldAmount` (12) | 0.3 | 0.3 |
| `kPadTSPitchEnvStart` (13) | 160 Hz | 0.451545 (`20 x 100^n`) |
| `kPadTSPitchEnvEnd` (14) | 50 Hz | 0.198970 |
| `kPadTSPitchEnvTime` (15) | 20 ms | 0.04 (`n x 500`) |
| `kPadTSPitchEnvCurve` (16) | Exponential (old 2-step list) | 1.0 (curve amount +1; the curve now is continuous) |
| `kPadModeStretch` (21) | 1.2 | 0.466667 (`0.5 + 1.5n`) |
| `kPadDecaySkew` (22) | 0.3 | 0.65 (`2n - 1`) |
| `kPadModeInjectAmount` (23) | 0.3 | 0.3 |
| `kPadNonlinearCoupling` (24) | 0.3 | 0.3 |
| `kPadMorphEnabled` (25) | on | 1.0 |
| `kPadMorphStart` / `kPadMorphEnd` (26 / 27) | 0.2 / 0.8 | 0.2 / 0.8 |
| `kPadMorphDuration` (28) | 300 ms | 0.145729 (`10 + 1990n`) |

The "off" branches write the `PadConfig` member defaults from `pad_config.h` (~196-218), read from a default-constructed `PadConfig`, not typed numbers. Re-read the decode in `pad_config_apply.h` before typing the table: an earlier task may have changed a law (the cubic filter-envelope decode, the morph duration range).

Then delete the old `configurePoolWorstCase` and `configureStressPool`. If `forEachMainVoice` now has no caller left (`git grep -n forEachMainVoice -- plugins/membrum`), delete it and its comment from `voice_pool.h`: this task's change made it unused. `test_dc_offset_trace.cpp`, `test_kit_preset_load_render.cpp` and `test_per_pad_pitch_env.cpp` also call it at `4a14376d`; do not change them, but if one of them writes through it before a `noteOn` (the same overwrite), report it.

**3. Build and gate**

Test-only change (plus the `voice_pool.h` deletion if it applies: then `Membrum` is built too). Run the new case by name and the standard gate. The timing lane runs twice, each alone with nothing else executing: once BEFORE step 2 (it measures the bypassed configuration: the "before" number) and once after: `node tools/run-cpu-tests.js membrum_tests` (it runs the hidden `[.perf]` cases the change reaches: `Phase35 Bench: 8-voice worst case under 12% CPU` and `Phase35 Stress: 16 voices 10 s zero xruns`). Pluginval only if `voice_pool.h` changed.

**4. Acceptance**

- `Bench patch: the worst-case Tone Shaper and Unnatural Zone reach the voices` went RED (three differences recorded, expected none) and is GREEN with each difference above -20 dB.
- The benchmark's printed line `[phase35-bench8] Feedback + NoiseBody (ts=on un=on) poly=8 -> x% CPU` is recorded before and after. The audit has no CPU baseline for this cell (no earlier `membrum_phase3_benchmark.csv`). The bar is the spec's: at most 18 % for this waived cell (SC-023), zero NaN or Inf; the stress case reports zero xruns (SC-024). If the real worst case misses a bar, stop and report the measured number: never relax a budget or shrink the workload.
- `GATE GREEN`.

### T7.2 — Coupling tests measure the processor's own coupling path

**Findings:** MB-138 (medium, CONFIRMED), MB-150 (low, CONFIRMED).

**Flags:** MACOS, added because this task replaces finiteness checks that the macOS leg defeats: `std::isfinite` in `test_coupling_energy.cpp:188` and the bit-mask clone `isFiniteSample` in `test_coupling_fuzz.cpp:52-57` (unless T3.1 already replaced it).

**Prerequisites:** T2.26 (sample-rate-based energy-limiter coefficients and the silence flag), T4.1 (the coupling model). Write every test against the coupling path as those two tasks left it.

**Estimate:** 180 min.

**What is wrong.** The case `Coupling energy: SC-007 energy limiter caps below -20 dBFS` (`tests/unit/processor/test_coupling_energy.cpp:148-190`) reads the final main bus, after the -1 dBTP `TruePeakLimiter` (`processor.cpp` ~717, final `std::clamp` to 0.891) and the master gain, and asserts `peak < 10.0f`: it cannot fail, and removing `applyEnergyLimiter` (`processor.h` ~181-196) passes it. The SC-009 fuzz (`tests/unit/processor/test_coupling_fuzz.cpp:80-234`) drives `VoicePool` plus a hand-written `fuzzEnergyLimiter` (:62-76: attack 0.01, release 0.001 per sample) instead of the processor's (per-sample step 0.999 on attack and 1e-4 on release), and skips the bus limiter, master gain, aux buses, parameter and event handling.

**1. Failing test**

a. **One limiter, not two.** Unless T2.26 already did it, move the limiter out of `Processor` into `plugins/membrum/src/processor/coupling_energy_limiter.h`, `class Membrum::CouplingEnergyLimiter` (run `grep -r "class CouplingEnergyLimiter" dsp/ plugins/` first) with `prepare(double sampleRate)` (T2.26's coefficients), `reset()`, `float process(float sample) noexcept` and `float envelope() const noexcept`. `Processor` owns one; `applyEnergyLimiter` and `energyEnvelope_` go, and `energyEnvelopeForTest()` returns `couplingLimiter_.envelope()`. The move must not change a sample: build, run `node tools/run-gate.js --share 4 --filter '[coupling]' membrum_tests` GREEN before going on.

b. **The limiter's own bound.** In `test_coupling_energy.cpp`: `TEST_CASE("Coupling energy limiter: output never exceeds -20 dBFS", "[coupling][energy_limiter]")`. At 44.1, 48, 96 and 192 kHz, on a freshly prepared limiter: (1) a full-scale step 0 -> 1.0 held 100 ms, (2) a 1 kHz sine at 0 dBFS for 1 s, (3) after `reset()`, a 1 kHz sine at -40 dBFS for 1 s. Assert, for (1) and (2), `max |out| <= std::nextafter(0.1f, 1.0f)` (0.1, allowing one float ulp for the division's rounding); for (3), `out == in` for every sample (below threshold the limiter is transparent); every output finite via `Krate::DSP::detail::isFinite` (`<krate/dsp/core/db_utils.h>`). Expected RED: the gain is `kThreshold / envelope` with the envelope one step behind `|x|`, so the first sample of the step comes out at 1.0 x 0.1 / 0.999 = 0.1001 at today's coefficients, about 13 000 ulp above 0.1 (more with a slower attack after T2.26); a step from 0 to just above 0.1 passes through unreduced. Record the measured peak per rate.

c. **SC-007 at the processor.** Replace the case at :148-190 with `TEST_CASE("Coupling energy: SC-007 coupling contribution stays below -20 dBFS", "[coupling]")`. Add a test seam on `Processor`: `float couplingReturnPeakForTest() const noexcept`, the largest `|sample|` the energy limiter emitted during the last `process()` call (reset to 0 at the top of `process()`, updated with `std::max` in the coupling loop, next to the other test accessors at `processor.h` ~79-82). Place the seam at the limiter's output wherever T4.1 left it (added to main L/R, or into receiver pads). Drive: Global Coupling 1.0, Snare Buzz 1.0, Tom Resonance 1.0, every pad's Coupling Amount 1.0 (per-pad ID `kPadBaseId + pad x kPadParamStride + kPadCouplingAmount`), all 32 notes at velocity 1.0, 44.1 kHz, block 512, 5 s (431 blocks). Assert `max over blocks of couplingReturnPeakForTest() <= std::nextafter(0.1f, 1.0f)` and `> 0.0f` (the coupling path ran: non-vacuity), and every main-bus sample finite via `Krate::DSP::detail::isFinite`. This case is expected GREEN on real notes (the probe measured the coupling component of a kick at -60.98 dB relative to the dry hit, `coupling:kick-added-energy`); its teeth come from a mutation: temporarily make `CouplingEnergyLimiter::process` return `sample * 1000.0f`, confirm RED, revert.

d. **The fuzz drives the processor.** Rewrite `test_coupling_fuzz.cpp` as `TEST_CASE("Coupling fuzz: SC-009 Processor::process allocates nothing under random MIDI load", "[coupling][rt-safety][fuzz]")`; delete `fuzzEnergyLimiter`, `isFiniteSample` and the "mirrors processor.cpp lines ..." comments (:13-15, :59-61, :208).
   - Setup, outside the tracking scope: a `Membrum::Processor` with `initialize(nullptr)`, the 15 aux output buses activated (`activateBus(kAudio, kOutput, b, true)`, b = 1..15), `setupProcessing` at 44.1 kHz / 512, `setActive(true)`; `ProcessData` with 16 `AudioBusBuffers` of 2 channels each. One prebuilt `Krate::Test::ParameterChanges` (`tests/test_helpers/vst_param_changes.h`) sets Global Coupling, Snare Buzz and Tom Resonance to 1.0, every pad's Coupling Amount to 1.0, and routes pads 16 to 31 to aux buses 1 to 15 through their per-pad Output Bus parameter (read the normalisation in `Processor::processParameterChanges` before choosing the value). Warm up as today: three rounds of all 32 pads on, 16 blocks, all off, 16 blocks.
   - The schedule: the existing xorshift with seed `0xC0FFEE`, 0 to 4 events per block, one in three a note-off, velocity 0.1 to 1.0, plus a random `sampleOffset` in [0, 511]. Precompute one `Krate::Test::EventListNoteIdEqPitch` per block (`tests/test_helpers/vst_event_list.h`) before tracking starts: an `EventList` grows a vector, which must not happen inside the scope.
   - Tracking: `TestHelpers::AllocationDetector::instance().startTracking()`, then for each block set `data.inputEvents` and call `processor.process(data)`, recording into one bool whether any sample of any bus is non-finite (`Krate::DSP::detail::isFinite`; no CHECK inside the loop), then `stopTracking()`.
   - Assert zero allocations, more than 100 note-ons, no non-finite sample. Non-vacuity of the detector: in its own start / stop pair, call `TestHelpers::allocateObservably()` and `REQUIRE` a count of at least 1 (`tests/test_helpers/allocation_detector.h` ~186-195; gated by `tools/lint-allocation-probes.js`).
   - Teeth, by mutation: temporarily add `static float* volatile sink = nullptr; sink = new float[8]; delete[] sink;` inside the coupling branch of `Processor::process`; confirm RED; revert.

**2. Fix**

`CouplingEnergyLimiter::process`: compute the gain from the larger of the envelope and the current sample, so the bound holds on the sample that rises above it:

```
const float level = std::max(envelope_, std::abs(sample));
return level > kThreshold ? sample * (kThreshold / level) : sample;
```

(after the envelope update, which stays as T2.26 left it). Below the threshold nothing changes, so a sample moves only where the limiter engages, which the probe never saw in ordinary playing. Re-run case b at all four rates.

**3. Build and gate**

Build `Membrum` and `membrum_tests` (processor sources changed). Run the three new cases by name (and each mutation once, reverted), then `node tools/run-gate.js --share 4 --filter '[coupling]' membrum_tests` and the standard gate. Pluginval: yes. MACOS: `tools/clangcl-fastmath-build.ps1 -Targets membrum_tests` (read its blind spot), `node tools/lint-finite-bitmask.js`, `node tools/lint-nonfinite-symbols.js`, `node tools/lint-ieee-flag-sets.js` (`test_coupling_energy.cpp` is in the IEEE block at `plugins/membrum/tests/CMakeLists.txt` ~260-266; MB-014 adds the missing `-fno-finite-math-only`: check it is there), `node tools/lint-allocation-probes.js`, `node tools/check-portability.js plugins/membrum/tests/unit/processor/test_coupling_energy.cpp plugins/membrum/tests/unit/processor/test_coupling_fuzz.cpp plugins/membrum/src/processor/processor.cpp`. Whatever no proxy covers is "unverified on macOS until CI" in the commit message.

Re-run the audit probe's coupling cases (see "Measuring with the audit probes"; filter `[audit-probe]`, lines `PROBE coupling:...`) before step 2 and after it. Their numbers must be identical; if one moved, the limiter engaged in that case: list it with both values.

**4. Acceptance**

- `Coupling energy limiter: output never exceeds -20 dBFS`: RED value recorded per rate (about 0.1001 for the step at today's coefficients), GREEN with every peak at most `nextafter(0.1f, 1.0f)` and the -40 dBFS sine passed through unchanged.
- `Coupling energy: SC-007 coupling contribution stays below -20 dBFS`: the measured maximum coupling peak in dBFS is recorded (baseline: the coupling component of a kick at -60.98 dB relative to the dry hit); RED under the x1000 mutation.
- `Coupling fuzz: SC-009 Processor::process allocates nothing under random MIDI load`: 0 allocations, the positive control counted at least 1, more than 100 note-ons; RED under the allocation mutation.
- No copy of the limiter remains in a test (`git grep -n "fuzzEnergyLimiter\|kAttack  = 0.01f" -- plugins/membrum/tests` prints nothing); the coupling probe lines are unchanged before and after step 2.
- `GATE GREEN`, pluginval exit 0, zero warnings, zero clang-tidy warnings.

### T7.3 — Diagnostic cases in the per-push gate assert something or are hidden

**Findings:** MB-143 (low; reported as medium; CONFIRMED)
**Flags:** none
**Prerequisites:** none
**Estimate:** 120 min

Eleven cases compiled into `membrum_tests` print traces and assert nothing, yet carry no hidden tag, so `run-gate.js` and CI's per-push shard filter (`tools/ci-test-shard.js`, `~[performance]~[perf]~[benchmark]~[!benchmark]~[long]`) run them on every push. Two of them name a behaviour they can check, so they are converted into assertions. The other nine are investigation traces whose behaviour is already asserted by sibling cases, so they are hidden with a leading-dot tag, as `test_snare_audition.cpp:203` (`[.snare-audition]`) and `test_snare_body_diagnose.cpp:249` (`[.snare-diag]`) already do. No case is deleted.

| Case (file:line at `4a14376d`) | Today | After |
|---|---|---|
| `"Diagnostic: dump installed Jazz Brushes snare params"` (`plugins/membrum/tests/unit/processor/test_kit_switch_infinite_ring.cpp:1616`) | WARN only; the REQUIRE at `:1622` checks the blob decode | hidden: `[diag]` becomes `[.diag]` |
| `"Diagnostic: minimal Membrane body at low b1 should decay"` (`:1764`) | renders 1.2 s, WARN loop at `:1814-1823`, no CHECK | **converted** (see step 1) |
| `"Diagnostic: HHOpen frequency profile"` (`:1829`) | WARN only | hidden `[.diag]` |
| `"Diagnostic: single Jazz Brushes tom decay trace"` (`:1879`) | WARN only | hidden `[.diag]` |
| `"Diagnostic: hat hit dominant-frequency capture"` (`:1915`) | WARN only | hidden `[.diag]` |
| `"Diagnostic: alternating two pads on Jazz Brushes"` (`:1949`) | WARN only | hidden `[.diag]` |
| `"Diagnostic: rapid self-hit on Jazz Brushes tom decay trace"` (`:1984`) | WARN only | hidden `[.diag]` |
| `"N-1 spike: modal body resonant peak vs Sum\|a_k\| bound"` (`plugins/membrum/tests/unit/diagnose/test_body_resonant_peak.cpp:132`, tag `[diagnose_n1]`) | printf + `SUCCEED` (`:178`) | hidden: `[.diagnose_n1]` |
| `"N-1 tension: glide in semitones vs velocity"` (`:185`, `[diagnose_n1_tension]`) | printf + `SUCCEED` (`:215`) | hidden: `[.diagnose_n1_tension]` |
| `"N-1 spike: F_strike across material/size/strikePos"` (`:224`, `[diagnose_n1_axes]`) | printf + `SUCCEED` (`:318`) | hidden: `[.diagnose_n1_axes]` |
| `"Orchestral timpani kick: bisect distortion source"` (`plugins/membrum/tests/unit/diagnose/test_orchestral_distortion.cpp:56`, `[diagnose_orch]`) | printf only; prints `"CLIPPING"` at `:119` and passes | **converted** (see step 1) |

Why the three N-1 spikes are hidden and not converted: they print the raw bank over-run factor `F = peak_raw / Sum|a_k|` that motivated the measured-strike normalisation now in `DrumVoice` (`plugins/membrum/src/dsp/drum_voice.h:1365-1387`, `kBodyHeadroom / probeStrikePeak`). A bound on the raw factor says nothing about the shipped gain staging, which `plugins/membrum/tests/unit/dsp/test_body_headroom_budget.cpp:81` (`"N-1: modal body stays under the per-voice rail at level 1.0"`) already asserts. Their local `constexpr float kBodyHeadroom = 0.25f; // matches drum_voice.h` (`test_body_resonant_peak.cpp:41`) is stale: `drum_voice.h:84` has `0.5f` and the member is public. Replace the local constant with `Membrum::DrumVoice::kBodyHeadroom` so the hidden spike prints the real budget; this changes printed text only.

#### 1. Failing test

There is no production defect, so "failing" means: each converted case must be able to fail. Prove it with a temporary local mutation that is reverted before the commit.

- **`test_kit_switch_infinite_ring.cpp:1764`.** Rename the case to `"Minimal Membrane body at low b1 decays to silence"` with tags `"[membrum][processor][infinite_ring][regression]"` (drop `[diag]`; it stays per-push). Keep the kit setup at `:1767-1808` unchanged. Collect the twelve 100 ms window peaks into `float winPeak[12]` instead of WARNing them (keep the values in an `INFO` line). Assert, as the finding gives:
  `CHECK(winPeak[11] < 1.0e-3f);` and `CHECK(winPeak[11] * 10.0f < winPeak[0]);`
  The helpers are the file's own `Fixture::renderBlock` / `blockPeak` (`:246-264`); no shared helper is needed.
  Teeth: temporarily set `p.sound[2] = 1.0;` (decay) and set `p.sound[42]` (bodyDampingB1) to the lowest damping the codec accepts (`0.0` at `4a14376d`; use T1.23's encoding if it changed the meaning of that slot), build, run the case by name, see it fail on the first CHECK, revert.
- **`test_orchestral_distortion.cpp:56`.** Rename to `"Orchestral timpani kick: no bisect scenario clips at the voice-pool output"` with tags `"[membrum][voice_pool][gain_staging]"`. Keep the seven scenarios (`:71-79`). In the loop, replace the `printf` at `:115-119` with `INFO` of the same values and assert per scenario:
  `CHECK(samplesNearClip == 0);` and `CHECK(peak < 0.99f);`. Also count non-finite samples with `Krate::DSP::detail::isFinite` (`<krate/dsp/core/db_utils.h>`) and `CHECK(nonFinite == 0)`.
  Teeth: temporarily call `configureTimpaniKick(pool, 1.0f)` instead of `0.10f` and multiply each `outL[i]` by 20 before measuring; the CHECKs must fail; revert.
- Run both converted cases by name on the unmutated code. They must PASS. If either fails without a mutation, that is a real defect (a minimal body that does not decay in 1.2 s, or a timpani that clips at level 0.10): stop and report the measured window peaks or clip count; do not loosen the assertion.

#### 2. Fix

- `test_kit_switch_infinite_ring.cpp`: in the tag strings at `:1617`, `:1830`, `:1880`, `:1916`, `:1950`, `:1985` replace `[diag]` with `[.diag]`. Apply the conversion at `:1764-1824` described in step 1. In the `"Recipe: JB → Tabla → JB never produces tonal output on any pad"` case delete the dead `checkPhase` lambda and its `(void)` cast (`:1691-1705`); nothing else in that case changes.
- `test_body_resonant_peak.cpp`: tags at `:132`, `:185`, `:224` become `"[.diagnose_n1]"`, `"[.diagnose_n1_tension]"`, `"[.diagnose_n1_axes]"`. Replace the local `kBodyHeadroom` at `:41` with `Membrum::DrumVoice::kBodyHeadroom` (the file already includes `dsp/drum_voice.h`). Update the file header comment (`:1-18`) with one sentence: hidden design spike; the shipped budget is asserted in `test_body_headroom_budget.cpp`.
- `test_orchestral_distortion.cpp`: the conversion in step 1, and update the header comment (`:1-4`): it is now a regression guard, not a diagnostic.

#### 3. Build and gate

Test-only change: target `membrum_tests` only; no pluginval. After the gate, list the non-hidden cases and confirm no trace-only case is left:
`build/windows-x64-release/bin/Release/membrum_tests.exe --list-tests "~[.]"`
The output must contain neither `Diagnostic:` nor `N-1 spike` nor `N-1 tension`, and must contain `Minimal Membrane body at low b1 decays to silence` and `Orchestral timpani kick: no bisect scenario clips at the voice-pool output`.

#### 4. Acceptance

- The two converted cases pass on the unmutated code and failed under their mutations (state both in the commit message).
- `--list-tests "~[.]"` lists 9 fewer cases than before the change (the 9 hidden ones; the 2 converted cases stay, under their new names), and `--list-tests "[diag],[diagnose_n1],[diagnose_n1_tension],[diagnose_n1_axes]"` still lists the 9 hidden cases (they remain runnable by tag).
- `GATE GREEN`.

### T7.4 — The editor survives Simple/Advanced toggles while it is open

**Findings:** MB-149 (low; reported as medium; CONFIRMED)
**Flags:** none (test-only; see the note on `modeToggleButton_`)
**Prerequisites:** none
**Estimate:** 120 min

No gated test swaps the selected-pad template while an editor frame exists. `plugins/membrum/tests/unit/controller/editor_lifecycle_test.cpp:14-26` only opens and closes `EditorDefault` three times through `Krate::TestSupport::exerciseEditorLifecycle` (`tests/test_helpers/editor_lifecycle_harness.h:102-133`, which writes no parameter). `plugins/membrum/tests/integration/test_editor_asan_lifecycle.cpp` toggles `kUiModeId` with no editor attached (its header, `:13-16`). The only live swap is in the hidden `"Membrum_EditorScreenshots"` case (`plugins/membrum/tests/unit/controller/editor_screenshot_test.cpp:82-88`, `:92`, tag `[.][screenshots][site]`). Meanwhile `Controller::viewWillDelete` (`plugins/membrum/src/controller/controller.cpp:1461-1478`) must null 10 cached raw pointers on every swap, and its own comment (`:1451-1459`) records that quick toggling once crashed on a stale pointer.

Note: `modeToggleButton_` (`controller.h:224`, assigned at `controller.cpp:1182`) is written but never read, so it cannot dangle into a dereference; its removal belongs to T2.19 (MB-104) and T6.4 (MB-165). This task does not touch it. The verifiers also showed that the button's own action (`controller.cpp:1173-1181`) is safe because the template swap is deferred until `UpdateHandler::triggerDeferedUpdates`; the test pins that ordering.

#### 1. Failing test

Add to `plugins/membrum/tests/unit/controller/editor_lifecycle_test.cpp`:
`TEST_CASE("Membrum editor survives Simple/Advanced toggles while open", "[membrum][controller][ui][lifecycle]")`

Includes: `"plugin_ids.h"`, `"dsp/pad_config.h"` (for `Membrum::padParamId`, `pad_config.h:331`), `"ui/arc_knob.h"`, `"ui/fieldset_container.h"`, `"ui/toggle_button.h"`, `"ui/outline_button.h"` (the shared creators register through `inline const` objects, so including them in a second TU is safe; this makes the tree match the plugin's, as `editor_screenshot_test.cpp:15-19` explains), `<editor_screenshot.h>` (for `Krate::TestSupport::Screenshots::findFirst` and `initPlatformForShots`), `<base/source/updatehandler.h>`, `<vstgui/uidescription/uiviewswitchcontainer.h>`.

Body (follow `editor_screenshot_test.cpp:96-118` for the open sequence):
1. `REQUIRE(Shots::initPlatformForShots());` create the controller with `Steinberg::owned(new Membrum::Controller())`, `initialize(nullptr)`, `Krate::TestSupport::ensureVstguiInitialized()`, build `new VSTGUI::VST3Editor(controller, "EditorDefault", <MEMBRUM_RESOURCES_DIR>/editor.uidesc)`, `attached(nullptr, Krate::TestSupport::nativePlatformType())`, attach the root view to the frame if it is not attached, `triggerDeferedUpdates()`, and find the `UIViewSwitchContainer` with `findFirst`.
2. `SECTION("synchronous swaps")`: `panel->setAnimationTime(0)`. Twenty iterations `i = 0..19`:
   - even `i` (a host or automation write): `controller->setParamNormalized(Membrum::kUiModeId, controller->getParamNormalized(Membrum::kUiModeId) >= 0.5 ? 0.0 : 1.0)`; odd `i` (the user's click): find the mode button in the current template, `findFirst<Krate::Plugins::OutlineActionButton>(panel, [](auto* b){ return b->getViewSize() == VSTGUI::CRect(312, 4, 392, 24); })` (`editor.uidesc:131` / `:179`, origin 312,4 size 80x20), `REQUIRE` it is not null, and click it: `VSTGUI::CPoint where(352, 14); btn->onMouseDown(where, VSTGUI::CButtonState(VSTGUI::kLButton));` (`OutlineButton::onMouseDown` is public, `plugins/shared/src/ui/outline_button.h:193-201`).
   - `Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();` then `REQUIRE(panel->getCurrentViewIndex() == (controller->getParamNormalized(Membrum::kUiModeId) >= 0.5 ? 1 : 0));`
   - Push into the cached views: for every parameter the controller registered (`getParameterCount()` / `getParameterInfo(k, info)`), except `kUiModeId`, call `controller->setParamNormalized(info.id, (i % 2) ? 0.25 : 0.75)`; this reaches every selected-pad proxy and every per-pad ID, including the morph, pitch-envelope and filter-envelope views listed in `viewWillDelete`. Then `triggerDeferedUpdates()` again.
3. `SECTION("swaps left mid-fade at close")`: keep the uidesc's 120 ms fade (do not call `setAnimationTime`), toggle `kUiModeId` 20 times with the same parameter pushes and `triggerDeferedUpdates()` after each, and run no timer, so fades are still pending when the editor closes.
4. Both sections end with `view->removed(); view->release();` then `controller->terminate()`. The pass condition is that nothing crashes; the ASan and valgrind lanes give it teeth.

No new shared helper: the open sequence is ten lines and has one other user (the hidden screenshot case); do not add it to `tests/test_helpers/` (that would make this a CROSS-PLUGIN task).

Teeth (required, under ASan, because a use-after-free does not reliably crash a Release build): temporarily delete the line `if (view == xyMorphPad_) xyMorphPad_ = nullptr;` (`controller.cpp:1470`). Build the ASan tree (root `CLAUDE.md`, "AddressSanitizer": `"C:/Program Files/CMake/bin/cmake.exe" -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON`, then `--build build-asan --config Debug --target membrum_tests -- -m:2`) and run the one case by name from `build-asan/bin/Debug/membrum_tests.exe`. ASan must report a heap-use-after-free; if it does not, find which cached pointer the parameter pushes do dereference after a swap (read `controller_view_sync.cpp`) and mutate that line instead. Restore the line, rebuild ASan, and confirm the case passes there too.

#### 2. Fix

None expected. If the new case crashes on unmutated code, that is a real stale-pointer defect: the fix is to null the offending pointer in `Controller::viewWillDelete` and register the listener where the view is cached (`createCustomView` / `verifyView`), with the case name in the commit message. If the offender is `modeToggleButton_`, coordinate with T2.19 instead of duplicating its change.

#### 3. Build and gate

Test-only when step 2 finds nothing: no pluginval. The ASan build and run of step 1 are extra; run them before or after the gate, never at the same time. If step 2 changed `controller.cpp`, add `Membrum` to the target list and run pluginval.

#### 4. Acceptance

- The case passes in Release and under `build-asan` (Debug); under the step-1 mutation ASan reported a heap-use-after-free naming `controller.cpp` (quote the first frame in the commit message).
- `GATE GREEN`.

## Phase 8 — Audit probes promoted to regression tests

Turn the audit probe rig and its kit-quality, parameter-audibility, velocity and tail measurements into un-hidden regression tests that assert the thresholds the report names.

### T8.1 — The audit probe rig becomes a shared Membrum test header; processor fixtures default to 16 output buses; mono and multi-bus renders agree

**Findings:** MB-140 (medium; reported as high; CONFIRMED)
**Flags:** none (test-only; the header lives in `plugins/membrum/tests/`, not in the shared `tests/test_helpers/`, so no other plugin consumes it)
**Prerequisites:** T1.9
**Estimate:** 240 min

`Processor::process` takes the mono `VoicePool::processBlock` overload when the host passes one output bus and the multi-bus overload otherwise (`plugins/membrum/src/processor/processor.cpp:630-666`). The plugin declares `kMaxOutputBuses` = 16 stereo buses (`plugins/membrum/src/dsp/pad_config.h:28`; `processor.cpp:97-106`), and a VST3 host passes every declared bus, so production runs the multi-bus overload. Of the processor-level fixtures, all but two set `data.numOutputs = 1` (grep `numOutputs\s*=` under `plugins/membrum/tests`: 29 of 31 sites; the exceptions are `test_kit_switch_infinite_ring.cpp:1545` and `test_coupling_phase9_verification.cpp:121`). At `4a14376d` the two overloads had drifted: the mono one called `couplingEngine_->noteOff(slot)` for a naturally finished voice (`plugins/membrum/src/voice_pool/voice_pool.cpp:386-396`), the multi-bus one did not (`:514-519`). T1.9 fixes that drift (MB-069, MB-118); this task makes the suite exercise the overload hosts run and pins the two paths to each other so they cannot drift again. The verifiers showed the drift itself was inaudible (resonators are reclaimed by envelope, `sympathetic_resonance.h:337-352`), which is why the pin is on output equality, not on resonator counts.

#### 1. Failing test

Add to `plugins/membrum/tests/unit/processor/test_coupling_integration.cpp`:
`TEST_CASE("Mono and multi-bus render paths are equivalent on the main bus", "[coupling][processor][multi-bus]")`

- Two processors, each at 48 kHz, block 256, default kit: one through a `ProcessorRig` (step 2) with `numBuses = 1`, one with `numBuses = Membrum::kMaxOutputBuses`, all aux buses left inactive (the processor's default, `processor.h:133`).
- Before the notes, send `kGlobalCouplingId` = 1.0 so the coupling engine runs. Then the same event list into both: note-ons 36 (offset 0), 38 (offset 77), 42 (offset 130) in block 0; 41 at block 10 offset 5; 36 again at block 400 (after its first voice has retired naturally, so the finished-voice branch runs); render 3 s.
- `CHECK` that the maximum `|L_mono - L_multi|` and `|R_mono - R_multi|` over the whole render is `<= 1.0e-6f` (-120 dBFS, the finding's bar; not bit-equality, because the two loops are separately compiled and may contract differently under fast-math).
- After the render, `CHECK(voicePoolForTest().getActiveVoiceCount() == 0)` and every `voiceMeta(slot).state == VoiceSlotState::Free` in both, and `couplingEngineForTest().getActiveResonatorCount()` is equal in both.
- Teeth: T1.9 has already removed the drift, so the case passes on the tree it starts from. Prove it can fail: temporarily replace the pan gains in the multi-bus branch with `1.0f` (or, if T1.9 merged the overloads into one body, make the multi-bus caller skip one voice slot), build, run the case by name, see it fail on the first `CHECK`, revert.

#### 2. Fix

1. **Create `plugins/membrum/tests/membrum_processor_rig.h`** (the test target already has `plugins/membrum/tests` on its include path, `plugins/membrum/tests/CMakeLists.txt:217-223`), namespace `Membrum::TestRig`. ODR check before writing: `grep -rn "namespace TestRig\|struct OutputBuses\|struct ProcessorRig\|struct HitFeatures" plugins/ dsp/ tests/` must return nothing (it returned nothing at `4a14376d`). Port from `specs/membrum-audit/probes/audit_quality_probes.cpp.txt`, keeping the probe's arithmetic exactly so the report's baselines stay comparable:
   - `struct OutputBuses`: owns the aux channel storage for `numBuses - 1` stereo buses of `maxBlock` samples and the `AudioBusBuffers` array; constructor `OutputBuses(float* mainL, float* mainR, int maxBlock, int numBuses = kMaxOutputBuses)` uses the caller's main buffers for bus 0; `void attach(Steinberg::Vst::ProcessData&)` sets `numOutputs` and `outputs`; `void clear(int numSamples)` zeroes every bus. This is what the existing fixtures switch to (step 2.2).
   - `struct ProcessorRig` from the probe's `Rig` (`:76-131`): constructor `ProcessorRig(double sampleRate = 48000.0, int blockSize = 256, int numBuses = kMaxOutputBuses, bool activateAux = false)`, owning a `Membrum::Processor`, an `OutputBuses`, a `Krate::Test::EventList` (`tests/test_helpers/vst_event_list.h`) and a `Krate::Test::ParameterChanges` (`vst_param_changes.h`).
   - `RigEvent` with `onAt / offAt / paramAt` (the probe's `Ev`, `mkOn`, `mkOff`, `mkParam`, `:133-146`), `struct RigRender` and `RigRender render(ProcessorRig&, std::vector<RigEvent>, int numSamples, bool earlyStop = true)` (`:148-218`, using the rig's block size instead of the constant 256).
   - `struct HitFeatures` and `HitFeatures analyseHit(const std::vector<float>& L, const std::vector<float>& R, double sampleRate, bool useL, int nonFinite)` (`Feat` / `analyse`, `:223-374`: peak, rms500, dc, decay to -60 dB in 10 ms RMS frames, 200 ms centroid with Hann 4096 / hop 1024, 24 bands 40 Hz-16 kHz), `double bandDistanceDb(const HitFeatures&, const HitFeatures&)` (`:376-385`). Finiteness only through `Krate::DSP::detail::isFinite`; the FFT is `Krate::DSP::FFT` as in the probe. The probe's `kSR` becomes the sample-rate argument.
   - The factory-kit table and loaders: `kFactoryKits` (the 20 `{category, name}` pairs, `:445-454`), `FactoryKit loadFactoryKit(int index)` (`:464-491`, path from `MEMBRUM_RESOURCES_DIR`), `void loadKitInto(ProcessorRig&, const FactoryKit&)` (`:493-502`: `setActive(false)`, `setState`, `setActive(true)`), and `RigRender hitPad(ProcessorRig&, const FactoryKit&, int note, float velocity, int numSamples, int noteOffAt)` (`:504-510`).
   - Leave out everything that prints `PROBE` lines or writes CSV parts.
2. **Move the processor-level fixtures to the real topology.** In each file below, replace the single `AudioBusBuffers` + `data.numOutputs = 1` with an `OutputBuses` member constructed on the fixture's existing `outL` / `outR` and `attach(data)`; clear it where the fixture cleared `outL` / `outR`. Measurements keep reading `outL` / `outR` (the main bus), so no assertion changes. Files and lines (`4a14376d`): `unit/processor/membrum_processor_tests.cpp:93`, `test_coupling_energy.cpp:90`, `test_coupling_integration.cpp:101`, `test_coupling_phase9_verification.cpp:239`, `test_coupling_state.cpp:77`, `test_dc_offset_trace.cpp:416, 575, 907, 1164, 1356`, `test_kit_preset_load_render.cpp:109`, `test_kit_switch_infinite_ring.cpp:228, 1179`, `test_macro_automation_extreme.cpp:105`, `test_macro_preset_load_regression.cpp:327`, `test_morph_host_simulation.cpp:122, 200`, `test_output_level_measure.cpp:93`, `test_snare_body_balance.cpp:117`, `test_tom_body_depth.cpp:115`; `unit/preset/test_kit_browser_load.cpp:141`, `test_pad_preset.cpp:475`; `unit/dsp/test_clap_acceptance.cpp:92`, `test_crash_acceptance.cpp:93`, `test_render_perceptual.cpp:80`; `integration/test_editor_asan_lifecycle.cpp:171`. Re-grep first; the list must match what the grep shows. Leave the two hidden diagnose fixtures (`unit/diagnose/test_snare_audition.cpp:105`, `test_snare_body_diagnose.cpp:116`) and the new equivalence case's deliberate `numBuses = 1` rig as they are.
3. Add a comment at the top of `membrum_processor_rig.h` stating the rule: processor-level fixtures use `kMaxOutputBuses` buses because hosts pass every declared bus; a mono rig is used only to compare against it.

#### 3. Build and gate

Test-only: target `membrum_tests`; no pluginval. No `header-consumers.js` run is needed (no file under `tests/test_helpers/`, `plugins/shared/` or `dsp/` changes). Every converted file is in `membrum_tests`, so the standard gate covers them; a case that goes red only because it now runs the multi-bus path is a real multi-bus defect: report it and stop (do not switch that fixture back to one bus).

#### 4. Acceptance

- The equivalence case passes, and failed under the step-1 mutation.
- `grep -rn "numOutputs\s*=\s*1" plugins/membrum/tests` returns only the two hidden diagnose fixtures.
- `GATE GREEN`, with the same case count as before plus one.

### T8.2 — Kit-quality probes become the factory-kit render test: level, clipping, DC, finiteness, kit count and near-duplicate pads

**Findings:** MB-145 (low; reported as medium; CONFIRMED)
**Flags:** none (test-only; it changes no kit and no engine code)
**Prerequisites:** T5.13, T8.1
**Estimate:** 300 min

`plugins/membrum/tests/unit/preset/test_factory_kit_presets.cpp` only parses each `.vstpreset` and round-trips it byte-exactly (`:75-151`), returns with a WARN when the preset root is missing (`:79-85`), and checks `CHECK(count >= 3)` under a stale "All three factory kits" comment (`:149-150`) although there are 20 kits, 5 in each fixed category. Its hidden `[.regen_factory_presets]` case (`:159-188`) writes one `DefaultKit::apply` blob into every `.vstpreset` under the tree, so one command turns all 20 kits into the default kit and the round-trip test stays green. The infinite-ring sweeps (`test_kit_switch_infinite_ring.cpp:440-629`) render the kits but check only decay. Nothing checks a pad's level, its margin to the bus limiter, DC, a silent pad, the MetaInfo category against the folder, near-duplicate pads or kit distinctness: those are exactly the audit's kit-quality probe (`specs/membrum-audit/probes/audit_quality_probes.cpp.txt:531-807`, report section 4). Its thresholds become assertions here.

Baselines at `4a14376d` (report section 4.2, `specs/membrum-audit/probes/kit-pads.csv`, `kit-pairs.csv`): 459 enabled pads, none silent, none non-finite, none with |dc| > 1e-3; **21 pads** in 10 kits already reach the bus ceiling on one velocity-100 hit (peak -7.00 to -7.16 dBFS, margin -0.00 to -0.16 dB: Chaos 51; Drone 36, 37, 50, 54; Experimental FX 38, 39, 51, 54, 57, 60, 63; Ghost Bones 47, 51; Glass Bell 46, 57, 61; Latin Perc 46; LinnDrum 40; Modular 50; World Metal 55); **20 near-duplicate pairs** in 9 kits (19 within one PadCategory, plus Tabla 44/48 Tom vs Kick); nearest kits 6.70 dB apart (Rock Big Room / Vintage Wood, 808 / 909); per-note cross-kit mean bandDist 16.53-23.24 dB. Phase 3 and phase 5 (T5.7-T5.13) own the limiter pads and the duplicates; this task only pins the result.

#### 1. Failing test

1. **Kit inventory** (per-push), in `test_factory_kit_presets.cpp`:
   - Replace the cwd walk `factoryPresetRoot()` (`:55-71`) with `std::filesystem::path(MEMBRUM_RESOURCES_DIR) / "presets" / "Kit Presets"` and `REQUIRE(std::filesystem::is_directory(root))` in place of the WARN-and-return at `:79-85`.
   - Replace `CHECK(count >= 3)` (`:149-150`) with `CHECK(count == 20)`, and count kits per sub-folder: for each name in `Membrum::kitPresetConfig().subcategoryNames` (`plugins/membrum/src/preset/membrum_preset_config.h:20-26`) `CHECK` exactly 5 files, and `CHECK` that no `.vstpreset` sits in any other folder.
   - In the same loop, read the MetaInfo chunk (`pf.getEntry(kMetaInfo)`) and `CHECK` it contains `MusicalCategory" value="<parent folder name>"` (the writer emits that attribute, `plugins/membrum/src/preset/membrum_preset_container.cpp:38`).
   - Keep every component-state chunk; after the loop `CHECK` that no two are byte-identical (20 distinct kits).
   - Delete the hidden `[.regen_factory_presets]` case (`:153-188`) and its now-unused includes. Kits are generated only by `tools/membrum_preset_generator.cpp` (target `generate_membrum_presets`); the comment that replaces the case says so.
   Red proof: temporarily copy one Acoustic kit into the Electronic folder (6 files there, 21 in total) and run the case by name: the count and per-folder `CHECK`s fail; delete the copy.
2. **Pad render** (new file `plugins/membrum/tests/integration/test_factory_kit_render.cpp`, added to the `membrum_tests` source list), using `Membrum::TestRig` from T8.1 (`ProcessorRig`, `loadFactoryKit`, `loadKitInto`, `hitPad`, `analyseHit`, `bandDistanceDb`). Harness exactly as the probe and report section 4.1 define it: 48 kHz, block 256, 16 buses, velocity 100/127, every enabled pad hit from silence (`loadKitInto` before each hit resets the bus limiter), render 4 s with the early stop.
   - One case per kit, generated by a macro as the probe did (`:1239-1241`): `TEST_CASE("Factory kit renders clean: kit " #N, "[membrum][processor][factory][render][long]")` for N = 0..19, plus `TEST_CASE("Default kit renders clean", "[membrum][processor][factory][render][long]")` on a fresh `ProcessorRig` (`DefaultKit::apply` in `initialize`, no `setState`). Each renders up to 32 pads x 4 s, more than 15 s of toolchain-independent audio, hence `[long]`.
   - Per enabled pad, with `masterDb = -24 + 36 * kit.masterGainNorm` and `ceilingDb = masterDb - 1` (probe `:512`, `:540-541`): `CHECK(f.nonFinite == 0)`; `CHECK(f.peakDb >= -60.0)` (not silent); `CHECK(f.peakDb - ceilingDb < -0.5)` (one hit does not reach the -1 dBTP bus limiter); `CHECK(std::abs(f.dc) <= 1.0e-3)`. Every `CHECK` carries an `INFO` with kit, note, exciter, body and the measured value.
   - Per kit, over every pair of sounding enabled pads (probe `:649-680`): a pair is a near-duplicate when `bandDistanceDb < 3.0` AND centroid ratio `<= 1.10` AND decay ratio `<= 1.20`; `CHECK` no pair is. A pair the user ruled intended in T5.7-T5.13 (for example a closed and pedal hat kept alike on purpose) goes into a `constexpr` allow-list in this file with the kit, the two notes and the ruling's commit hash in a comment; no other entry is allowed, and the list is empty when no ruling kept a pair.
   - A per-push sentinel the `[long]` tag must not hide (finiteness guards never get `[long]`): `TEST_CASE("Factory kits: every enabled pad renders finite", "[membrum][processor][factory][render]")` renders every enabled pad of all 20 kits for 0.5 s and `CHECK`s `nonFinite == 0` only.
3. **Kit distinctness** (same new file, `[long]`): `TEST_CASE("Factory kits are distinct from each other", "[membrum][processor][factory][render][long]")` ports `runCrossKit` (probe `:717-807`): notes 36/38/42/45/49 of all 20 kits; `CHECK` each note's mean pairwise bandDist `>= 3.0` dB and each kit's nearest other kit `>= 3.0` dB.

The render cases are new, so their red proof is a mutation: temporarily scale the pad level of one kit's note 38 to 0 in `loadKitInto`'s blob (via `PadSnapshot`) and see the silent-pad `CHECK` fail; then temporarily copy pad 42's snapshot over pad 44 of one kit and see the near-duplicate `CHECK` fail; revert both.

#### 2. Fix

No production change. If a render `CHECK` fails on the tree this task starts from (every earlier phase committed), the threshold is not moved: stop and report the failing pads with their `peakDb`, margin, dc and the pair metrics, and the task that should have cleared them (phase 3 for limiter margins, T5.7-T5.13 for near-duplicates).

#### 3. Build and gate

Test-only: target `membrum_tests`; no pluginval. The gate filter of the standard workflow does not exclude `[long]`, so the gate runs the render cases. Read their wall clock in `shard-times.tsv`; a single case above 60 s goes into the commit message so the nightly long lane can be sized.

#### 4. Acceptance

- The inventory case: 20 kits, 5 per category, MetaInfo matches folder, 20 distinct component states; it failed under the step-1 copy.
- The 21 render cases pass: 0 silent, 0 non-finite, 0 with |dc| > 1e-3, 0 pads within 0.5 dB of the bus ceiling (baseline 21), 0 near-duplicate pairs outside the ruled allow-list (baseline 20); the distinctness case passes with the nearest kit pair `>= 3` dB (baseline 6.70 dB). Quote the new nearest-pair value and the lowest limiter margin in the commit message.
- `GATE GREEN`.

### T8.3 — The parameter-audibility probe becomes a regression test scored on a warm hit; the US5-2 filter test asserts real attenuation

**Findings:** MB-266 (medium; CONFIRMED)
**Flags:** none (test-only)
**Prerequisites:** T1.15, T8.1
**Estimate:** 240 min

The audit's parameter sweep built a fresh Processor for every step and scored the first hit (`renderParam`, `specs/membrum-audit/probes/audit_quality_probes.cpp.txt:1070-1085`; one note-on at `:1080`; called once per step at `:1117-1119`). On a never-used slot the tone-shaper SVF starts at g(20 kHz) and glides down with a 5 ms one-pole on g (`plugins/membrum/src/dsp/tone_shaper.h:79-83` prepares at 20 kHz; `noteOn` only resets the integrators, `dsp/include/krate/dsp/primitives/svf.h:289-298`), so the filter has not closed before the transient has passed: fc is 19 168 Hz at 1 ms and 7 160 Hz at 10 ms at 48 kHz for a 20 Hz target (`specs/membrum-audit/probes/svf-glide-times.txt`). The verdict rule then let one pad carry a parameter ("suspicious" only when all three base pads are weak, `:1173-1174`). Result: Filter Cutoff, Env Amount and the filter-envelope ADSR were labelled "ok" while dead in the measured context (`specs/membrum-audit/probes/param-audibility.csv`: hat LP 20 Hz to 20 kHz 0.89 dB, row 28; snare 0.66 dB, row 27; Env Amount -1..+1 0.07-0.15 dB, rows 32-34; Env Attack / Decay / Sustain / Release on the hat at most 0.07 dB, rows 55, 58, 61, 64), whereas on a warm slot the same hat at LP 317 Hz with Env Amount -1 / 0 / +1 spans 25 dB (-98.26 / -95.92 / -72.89 dB warm vs -49.85 / -49.84 / -49.77 dB fresh, `filter-glide.csv`). T1.15 (MB-042, MB-265) makes the first hit independent of the slot's history; this task pins that and turns the sweep into a regression test. The unit test `"ToneShaper: Filter envelope sweep modulates LP cutoff (US5-2)"` (`plugins/membrum/tests/unit/tone_shaper/test_tone_shaper.cpp:228-273`) has the same blind spot: its comment at `:253` claims the filter is "still closed" at 1 ms while the code glides from 20 kHz, and it asserts only relative magnitudes (`:271-272`).

#### 1. Failing test

1. **US5-2 rewrite**, `test_tone_shaper.cpp:228-273` (keep the name and tags `"[membrum][tone_shaper][filter_env]"`):
   - Keep the setup (`:234-246`). If T1.15 added an entry point that the voice calls to snap the filter at note-on (a snap inside `ToneShaper::noteOn`, or a separate call before it), the test must use the same entry point the voice uses; read T1.15's change first.
   - Add the absolute check the finding names: `const double in_start = goertzelMag(input.data(), lenWin, kSampleRate, 3000.0);` and `CHECK(20.0 * std::log10(mag_start / in_start) <= -20.0);` (the 3 kHz content in the 0-3 ms window at least 20 dB below the input's). Keep both relative `CHECK`s.
   - Replace the false comment at `:253` with what is now true: the cutoff starts at the 200 Hz base at note-on and the 5 ms envelope attack opens it.
   - Red proof: temporarily undo T1.15's snap (restore the glide from 20 kHz), build, run the case by name: the new `CHECK` fails (near 0 dB attenuation with the cutoff near 19.5 kHz); restore.
2. **Fresh slot equals warm slot** (per-push), new file `plugins/membrum/tests/integration/test_param_audibility.cpp` (add it to the `membrum_tests` source list):
   `TEST_CASE("Tone-shaper filter: a fresh slot's first hit matches a warm slot within 1 dB", "[membrum][processor][tone_shaper]")`
   - Default kit, hat note 42, context LP (`kPadTSFilterType` 0.0), cutoff 0.4 (about 317 Hz), env decay 0.5, sustain 0 (the probe's offset-10 context, `contextFor`, `:867-870`); steps Env Amount 0.0 / 0.5 / 1.0 and, separately with env amount 0.5, cutoff 0.0 / 0.25 / 0.5.
   - Fresh: a new `Membrum::TestRig::ProcessorRig`, the context and value sent as parameter changes in block 0 with the note-on, score that hit (the probe's `renderParam`). Warm: see "Warm hit" below.
   - `CHECK(std::abs(fresh.rms500Db - warm.rms500Db) <= 1.0)` per step (the finding's bar). Baseline before T1.15: up to about 46 dB apart (MB-265). Red proof: the same temporary revert of T1.15.
3. **The sweep as a regression test** (same new file, `[long]`):
   - Port `kOffsetNames`, `Ctx`, `contextFor`, `couplingPartner`, `stepsFor`, `keyName` (`audit_quality_probes.cpp.txt:812-1068`) into the file's anonymous namespace. Re-check every `kPad*` constant name against `plugins/membrum/src/dsp/pad_config.h`: the per-pad offsets swept are 0 to the highest `kPadActiveParamCountV*` constant minus one (66 at `4a14376d`, `pad_config.h:176`; STATE-COMPAT tasks append offsets after it), plus globals 270, 271, 272, 273 and 320. A new offset gets a context only if it needs one, chosen like the probe's (the parameter's enabling switch on).
   - **Warm hit.** One `ProcessorRig` per step: block 0 sends the context, the value, and `kMaxPolyphonyId` = 0.0 (4 voices, `processor.cpp:242-248`); blocks 0-3 each carry one note-on of the base note at velocity 100/127, so all 4 slots have played this configuration; render until `!voicePoolForTest().isAnyVoiceActive()` plus 16 quiet blocks (cap 10 s; `REQUIRE` the cap is not reached), then send the scored note-on (plus the context's second note and note-off at their offsets relative to it) and render 3 s. This is the finding's "poly 4 and five hits" variant, made deterministic: every slot the allocator can pick is warm.
   - One case per parameter, by macro as in the probe (`:1283-1291`): `TEST_CASE("Parameter audibility on a warm hit: " #N, "[membrum][processor][param_audibility][long]")`.
   - Metrics per base pad (36 / 38 / 42), exactly as report section 4.1 "Parameter sweep" and the probe's `runParam` (`:1095-1172`) compute them: level = max - min rms500 (floor -100 dB), centroid % = max/min - 1, decay % = max/min - 1 over non-silent steps, max pairwise `bandDistanceDb`, bit-identity across all steps, max adjacent-step rms500 jump (continuous parameters only).
   - Assertions, the report's rules now enforced: (a) `CHECK_FALSE` bit-identical on all three base pads (the "bad" rule); (b) `CHECK_FALSE` weak on all three, weak = level < 1 dB AND centroid < 5 % AND decay < 5 % AND bandDist < 1 dB (the "suspicious" rule); (c) for continuous parameters `CHECK(maxAdjacentJump <= 12.0)`; (d) MB-266's rule for the tone-shaper offsets `kPadTSFilterType`, `kPadTSFilterCutoff`, `kPadTSFilterResonance`, `kPadTSFilterEnvAmount`, `kPadTSFilterEnvAttack`, `kPadTSFilterEnvDecay`, `kPadTSFilterEnvSustain` (offsets 7-10, 17-19): not weak on **each** of the three base pads, so one pad can no longer carry a dead filter. For rule (d) "weak" leaves out the decay metric (level < 1 dB AND centroid < 5 % AND bandDist < 1 dB): on short pads it moves by one 10 ms frame and reads as 25 % or more (report section 4.4: "The 25 % on pad 42 is 0.04 -> 0.05 s, one 10 ms decay frame"), which would let a dead filter pass. `kPadTSFilterEnvRelease` (offset 20) gets rule (d) only if T2.9's ruling kept note-off gating the filter envelope; if T2.9 made note-off a no-op, the release segment cannot be reached by design and offset 20 gets rules (a)-(c) only, with T2.9's commit hash in a comment.
   - By-design readings, as in the probe: `kPadOutputBus` checks aux routing instead (rig with `activateAux = true`; the aux energy lands on exactly the selected bus at every step, probe `:1123-1134`; baseline 48 of 48); `kPadPan` is scored on L (`useL`). A parameter the user ruled "relabel / document" in T5.1-T5.5 instead of making it audible goes into a `constexpr` exemption list with the ruling's commit hash, exempt from rule (b) only; the list is otherwise empty.
   - Red proof for the sweep: temporarily make `Processor::processParameterChanges` skip the per-pad `kPadTSFilterCutoff` offset and run that one case by name: rule (a) or (d) fails; revert.

#### 2. Fix

No production change. If a sweep case fails on the tree this task starts from, report the parameter, its per-pad metrics and the task that owns it (T4.1 for globals 271 / 272 and offset 36, T5.1-T5.5 for the dead and weak parameters, T1.6 / T1.18 for the FM Ratio jump, T1.15 for the filter). For a parameter no task owns (for example the 12.12 dB Mode Inject jump, offset 23 in report section 4.4), stop and report the measured value. Never widen a rule.

#### 3. Build and gate

Test-only: target `membrum_tests`; no pluginval. The gate runs the `[long]` sweep cases too; record the slowest case's wall clock from `shard-times.tsv` in the commit message.

#### 4. Acceptance

- US5-2: the 3 kHz content in the 0-3 ms window is at least 20 dB below the input's (quote the measured value); it was near 0 dB under the revert.
- Fresh vs warm: every step within 1 dB (quote the largest difference; baseline about 46 dB).
- The sweep: no parameter bit-identical on all three base pads (baseline: 271, 272), none weak on all three outside the ruled list (baseline: 36, 273), no continuous adjacent jump above 12 dB (baseline: 23 at 12.12 dB, 32 at 12.22 dB), and every tone-shaper offset 7-10 and 17-19 audible on each base pad under rule (d) (baseline from `param-audibility.csv`, fresh hits: offset 10 weak on 36, 38 and 42; 17 and 18 weak on 38 and 42; 19 weak on 38; 20 weak on all three).
- `GATE GREEN`.

### T8.4 — Velocity probes become processor-level SC-004 / SC-005 tests with a directional timbre assertion

**Findings:** MB-267 (medium; CONFIRMED)
**Flags:** none (tests plus two spec compliance rows; no production code)
**Prerequisites:** T4.13, T8.1
**Estimate:** 240 min

Every velocity test is green while factory pads get darker when hit harder. `plugins/membrum/tests/unit/exciters/test_velocity_mapping.cpp:29-86` measures a bare `ExciterBank` (no body, click, noise layer, envelope or tone shaper) for 441 samples and takes the full-band centroid, which the velocity-opened exciter filter dominates; its list (`:66-73`) has 6 types and leaves out `ExciterType::Clap`. That matches the letter of spec 137 FR-092 / SC-004 (`specs/137-membrum-phase2-exciters-bodies/spec.md:295`, `:327`), but not FR-016 (`:192`: "velocity 127 produces measurably higher spectral centroid than velocity 30"), which is about the instrument. At processor level, `"Membrum: Velocity 30 and velocity 127 produce distinct spectra (SC-005)"` (`plugins/membrum/tests/unit/processor/membrum_processor_tests.cpp:533-570`) was relaxed to `REQUIRE((ratio < 0.9f || ratio > 1.1f))` (`:569`), so an inversion passes, while spec 136 still reads `SC-005 | ✅ MET | ... > 2x centroid (threshold > 2.0f)` (`specs/136-membrum-phase1-scaffold/spec.md:297`; the criterion is `:196`). Measured (`specs/membrum-audit/probes/velocity-centroid-results.txt`, probe 1b, warm, ratio centroid(vel 127) / centroid(vel 30)): Hand Drums 42 0.528, Acoustic Studio 36 0.691, Acoustic Studio 38 0.792, Vintage Wood 38 0.860, Rock Big Room 38 0.888, Hand Drums 38 0.959, Ghost Bones 38 0.967, Drone and Sustain 36 0.999, Orchestral 38 1.002, 909 38 1.038; the quality lane (vel 40 vs 127) found 12 of 60 factory notes below 1 (report section 4.2). T4.13 changes the noise-layer velocity response (MB-178); this task asserts the result.

#### 1. Failing test

1. **Processor-level SC-005 per exciter and body** — new file `plugins/membrum/tests/integration/test_velocity_timbre.cpp` (add to the `membrum_tests` source list), using `Membrum::TestRig` from T8.1:
   `TEST_CASE("Velocity timbre (SC-005): a vel 127 hit is at least 2x brighter than vel 30 through the processor", "[membrum][processor][velocity][sc005]")`
   - Build each configuration as a kit: `std::array<Membrum::PadConfig, Membrum::kNumPads>` from `Membrum::DefaultKit::apply`, edit pad 0 (note 36) by field name, convert with `Membrum::State::toPadSnapshot`, `writeKitBlob`, `setState` (this reaches fields with no parameter, such as `PadConfig::noiseLayerGain`, `pad_config.h:261`). Seven configurations, the finding's list: Impulse / Membrane at size 0.2 and at 0.85; Mallet / Membrane with the pitch envelope on (time 0.3, start 0.7, end 0.3, normalized as in the probe's `contextFor`); NoiseBurst / Membrane snare with `noiseLayerMix` 0.65 and `noiseLayerGain` 6.2; Impulse / Bell; Friction / String; Clap / NoiseBody. Every other field keeps the default pad 0 value.
   - Topology as the finding requires: `ProcessorRig` with 16 buses, master at its default -6 dB, 48 kHz, block 256. For each velocity (30/127, then 127/127) a fresh rig, one note-on, render 300 ms; T1.15 made the first hit equal to a warm one and T8.3 pins it, so no priming is needed.
   - Centroid = `HitFeatures::centroidHz` (the first 200 ms window of `analyseHit`). `REQUIRE(lo.centroidHz > 0)`, then two separate checks so a failure says which: `CHECK(ratio > 1.0)` (direction) and `CHECK(ratio >= 2.0)` (the SC-005 value, spec 136 `:196`; spec 137 SC-004 `:327`). Also `CHECK(hi.peakDb - (-7.0) < -0.5)`: the vel-127 hit must stay out of the -1 dBTP bus limiter so the limiter cannot shape the timbre; if a configuration reaches it, lower that pad's `level` field in the test's configuration (state the value in a comment), never the thresholds.
2. **Factory pads do not get darker when hit harder** — same file, one case per category so each renders under 15 s of audio and stays per-push:
   `TEST_CASE("Velocity timbre: no <Category> factory pad gets darker when hit harder", "[membrum][processor][velocity][factory]")` for Acoustic, Electronic, Percussive, Unnatural. For notes 36 / 38 / 42 of every kit in the category (the quality probe's B3, `audit_quality_probes.cpp.txt:690-710`), vel 40/127 vs 127/127, fresh rig per hit via `loadKitInto`, 300 ms: `CHECK(centroid(127) / centroid(40) >= 1.0)` with an `INFO` naming kit, note and both centroids. Baseline: 12 of 60 below 1.0 (report section 4.2: Acoustic Studio 36 and 38, Orchestral 38, Rock 38, Vintage Wood 38, 909 38, Hand Drums 38 and 42, Latin Perc 36 and 38, Drone 36, Ghost Bones 38).
3. **Exciter level** — `test_velocity_mapping.cpp`: add `Membrum::ExciterType::Clap` to the type list (`:66-73`) and rename the case to `"Velocity mapping: all 7 exciters have centroid ratio >= 2.0 (SC-004)"` (tags unchanged). This stays the excitation-signal check FR-092 asks for.
4. **The relaxed processor case** — `membrum_processor_tests.cpp:533-570`: replace `REQUIRE((ratio < 0.9f || ratio > 1.1f));` (`:569`) with the directional `REQUIRE(ratio > 1.0f);` and replace the comment at `:536-552` with two sentences: the 46 ms window of this case checks direction only; the SC-005 2x criterion is asserted by the new case in `integration/test_velocity_timbre.cpp`.

Run the new cases by name on the tree this task starts from. Red proof for a case that passes: temporarily make `ExciterBank::trigger(float velocity)` (`plugins/membrum/src/dsp/exciter_bank.h:53`) use `0.5f` in place of `velocity` (this flattens the exciter's velocity response) and confirm that the `ratio >= 2.0` `CHECK`s of the configuration cases fail; revert.

#### 2. Fix

No production change in this task (T4.13 owns the noise-layer velocity fix). Documentation that the tests now decide:
- `specs/136-membrum-phase1-scaffold/spec.md:297`: rewrite the SC-005 row's evidence to name the new processor-level case and the measured lowest ratio over the seven configurations, with ✅ MET only if `CHECK(ratio >= 2.0)` passes for all seven.
- `specs/137-membrum-phase2-exciters-bodies/spec.md:541` (FR-016) and `:599` (SC-004): keep the citation of `test_velocity_mapping.cpp` for the excitation-level SC-004 / FR-092 reading, correct the line references to the renamed case, and add the processor-level case as the evidence for FR-016's instrument-level clause.

If `ratio >= 2.0` or the factory direction check fails after T4.13: do not lower 2.0 and do not drop a configuration or a pad. Stop and report the table (configuration or kit / note, centroid lo, centroid hi, ratio). Whether to mark SC-005 ❌ NOT MET in spec 136 `:297` and spec 137 `:599` until the engine meets it (the finding's alternative), or to open an engine task, is the user's decision.

#### 3. Build and gate

Test-only plus spec text: target `membrum_tests`; no pluginval. Standard gate.

#### 4. Acceptance

- The seven processor configurations each have ratio `> 1.0` and `>= 2.0` (quote all seven ratios; the audit measured no configuration at processor level; the bare Impulse exciter read 5.1x full band but 1.26x below 2.5 kHz, MB-267 evidence).
- All 60 factory notes have centroid(127) / centroid(40) `>= 1.0` (baseline 12 of 60 below; lowest 0.249, Drone and Sustain 36, a fresh-slot reading that probe 3 (E) showed as 0.999 warm).
- The exciter case passes for all 7 types including Clap; `membrum_processor_tests.cpp` no longer accepts an inversion; the two spec rows cite tests that enforce what they claim.
- `GATE GREEN`.

### T8.5 — Tail probes become lower-bound tests: no factory pad is cut before its natural decay, Friction pads included, with note-on-only sweeps

**Findings:** MB-144 (low; reported as medium; CONFIRMED)
**Flags:** none (test-only)
**Prerequisites:** T2.9, T6.10, T8.1
**Estimate:** 240 min

The infinite-ring sweeps bound tails only from above and skip a whole class of pads. `runPerPadSustainTest` (`plugins/membrum/tests/unit/processor/test_kit_switch_infinite_ring.cpp:440-505`) skips every pad whose exciter is Friction (`:460-466`, "Friction sustains WITHOUT note-off by design"; repeated for the all-kits sweep at `:551-552`), but `FrictionExciter` gates itself off about 41 ms after the trigger (`plugins/membrum/src/dsp/exciters/friction_exciter.h:44` `autoReleaseSamples_ = 0.041 * sr`, `:111-114` `gate(false)`), so any long ring on a Friction pad comes from the body or the layers, which is what the sweep exists to catch; the generator gives Friction to many factory pads (bowls, drones, guiros: `tools/membrum_preset_generator.cpp`, for example `:1559`, `:3424`, `:5013`, `:6323`). T6.10 corrects the documented constraint (MB-124, MB-184). The sweep also asserts only silence by 8 s and `s10 * 100 < s1` (`:498-503`); nothing asserts that a tail is not cut early. Two mechanisms can cut one: the per-block auto note-off when a voice's block peak falls below 1e-3 (`plugins/membrum/src/voice_pool/voice_pool.cpp:380-384`, mirrored in the multi-bus path `:508-512`) and the 256-silent-block force-retire (`plugins/membrum/src/dsp/drum_voice.h:660-665`), whose length depends on the host block size. Baseline (`specs/membrum-audit/probes/kit-pads.csv`, block 256): **27 of the 30 enabled String pads end at 1.37-1.38 s**, which is 256 blocks x 256 samples / 48 kHz = 1.365 s plus one 10 ms frame; T1.8 (MB-160) fixes that retirement, and this task makes sure it cannot come back.

#### 1. Failing test

1. **Remove the Friction exemption** in `test_kit_switch_infinite_ring.cpp`:
   - With T6.10's default ruling (the documents are corrected: Friction is a ~41 ms transient), delete the `if (... exciterType == Membrum::ExciterType::Friction) continue;` block and its comment (`:460-466`), and the sentence "Friction pads are skipped inside the sweep (design-sustain without note-off)." at `:551-552`.
   - If the user ruled in T6.10 to build a real Friction sustain instead, the exemption shrinks to the pads whose `PadConfig` actually sustains under the new engine (the predicate T6.10 introduced), with a comment citing T6.10's commit; all other Friction pads are swept.
   - The sweeps stay note-on-only (no note-off is sent), as their names say.
   - Run the six dedicated per-kit cases (`:509-543`) and the four category sweeps (`:605-629`) by name. If a Friction pad now fails the 8 s silence check or the geometric-decay check, that is a real infinite ring: stop and report the kit, the note and its `s1..s10` window peaks (the `INFO` at `:491-496`); never restore the exemption.
2. **Lower bound on every tail**, new file `plugins/membrum/tests/integration/test_factory_tails.cpp` (add to the `membrum_tests` source list), using `Membrum::TestRig` from T8.1. One case per category, each rendering hundreds of seconds of toolchain-independent audio, hence `[long]`:
   `TEST_CASE("Factory pads are not cut before their natural decay (<Category>)", "[membrum][processor][factory][tail][long]")` for Acoustic, Electronic, Percussive, Unnatural.
   - For every kit in the category and every enabled pad: two `ProcessorRig`s at 48 kHz with 16 buses, block size 64 and block size 1024; `loadKitInto`, one note-on at velocity 100/127, no note-off, render until no voice is active plus 16 quiet blocks, capped at 12 s (`REQUIRE` the cap is not reached: a pad still sounding at 12 s is an infinite-ring failure for the sweeps above).
   - Per render, from the mono signal in 10 ms RMS frames (`analyseHit`'s frames): `decay` = the probe's -60 dB-below-maximum time; and a self-referenced natural decay: fit a straight line in dB to the frames after the loudest frame that lie between 6 dB and 20 dB below it, and extrapolate the time at which that line reaches -60 dB (`tNatural`).
   - Assertions: (a) every pad, block-size invariance, `CHECK(std::abs(d64 - d1024) <= std::max(0.10 * std::max(d64, d1024), 0.010))` (the finding's 10 %, floored at one 10 ms frame because the decay is measured in 10 ms frames); (b) pads whose longer decay is 0.5 s or more (the report's "long" pads, section 4.2: 185 at baseline), not cut early, `CHECK(decay >= 0.9 * tNatural)` at both block sizes; for these `REQUIRE` at least three frames in the fit window (a long pad that drops from -6 dB to below -20 dB in under 30 ms is itself a cut). Shorter pads (hats, clicks) have one or two frames in that window, so only (a) applies to them. Rule (b) is one-sided on purpose: a two-slope tail (a fast noise or click layer over a slow body) measures longer than its early slope predicts and passes; a tail that ends faster than its own decay rate, which is what a retirement cut produces, fails. This self-referenced bound replaces the finding's "T60 implied by decay / material": the bank exposes its damping law (`ModalResonatorBank::getDampingLaw`, `getModeFrequency`) but not each mode's level relative to the hit's peak, so an analytic lower bound would need a factor this plan cannot source.
   - Every `CHECK` carries an `INFO` with kit, note, exciter, body, `d64`, `d1024` and `tNatural`.
   - Red proof: temporarily change the force-retire limit at `drum_voice.h:663` from `> 256` to `> 4` (or, if T1.8 replaced that counter, shorten the retirement T1.8 introduced the same way), build, run the Unnatural case by name: String and NoiseBody pads must fail (b) or (a); revert.
3. **Note-off, decided by T2.9's ruling.** If T2.9 ruled that a note-off leaves a percussion voice's tail alone (FR-114), add to the same file, per category and `[long]`: `TEST_CASE("A short MIDI note does not cut a factory pad's tail (<Category>)", "[membrum][processor][factory][tail][long]")`, block 256, one render without note-off and one with a note-off 100 ms after the note-on: `CHECK(decayWithOff >= 0.9 * decayWithout)`. Baseline: 180 of the 185 pads that decay for 0.5 s or more were cut below half (median ratio 0.21; Acoustic Studio tom 41 1.37 s to 0.17 s; Orchestral 54 > 4 s to 0.14 s; report section 4.2). If T2.9 ruled that note-off gates and damps, add nothing here (T2.9's own test pins that behaviour) and say so in the commit message.

#### 2. Fix

No production change. A failure on the tree this task starts from is a defect owned by an earlier task (T1.8 String retirement, T1.7 Feedback retirement, T2.9 note-off): stop and report the pads and numbers; do not loosen a bound, shorten a render or skip a pad.

#### 3. Build and gate

Test-only: target `membrum_tests`; no pluginval. The `[long]` cases run in the local gate; record each category case's wall clock from `shard-times.tsv` in the commit message.

#### 4. Acceptance

- No Friction exemption is left in `test_kit_switch_infinite_ring.cpp` (or only T6.10's sustaining-pad predicate), and all infinite-ring cases pass with Friction pads swept.
- Every enabled factory pad: `d64` and `d1024` within 10 % (or one frame); every pad with a decay of 0.5 s or more: `decay >= 0.9 * tNatural` at both block sizes. Quote the worst ratio for each; the audit baseline had 27 String pads ending at 1.37-1.38 s at block 256 (which at block 64 would have been 0.34 s).
- Under T2.9's no-op ruling, every pad's decay with a 100 ms note-off is at least 0.9 of its decay without one (baseline: 180 of 185 below 0.5).
- `GATE GREEN`.

## Completion checklist

The plan is complete only when every line below has been checked against the code and a fresh run. Do not fill a line from memory or from an earlier run. An open line with its reason is worth more than a tick nobody verified.

**For every task, at its commit (120 times):**

- [ ] The failing test was seen RED on the unfixed code. Recorded: the `TEST_CASE` name and the failing assertion's actual output.
- [ ] The same test is GREEN after the fix. Recorded: the `GATE GREEN` line of the run (cases, assertions), copied, not paraphrased.
- [ ] Each finding id of the task was re-read in `report.md` and compared with the fixed code. Recorded: the fix as file:line, and the finding's own symptom re-measured with the numbers the finding gives.
- [ ] Every acceptance value of the task was measured and is written down as a number next to its threshold. "Passes" without a number does not count.
- [ ] No threshold was relaxed, no assertion weakened, no test hidden or re-tagged, no workload shrunk, no budget raised. Where a threshold could not be met, the task stopped and reported the measured value.
- [ ] No feature, control, interaction, parameter, exciter, body or kit was removed to make the problem go away. Anything removed was named in the task and ruled on by the user.
- [ ] The build log has zero warnings; pluginval exited 0 when plugin sources changed; clang-tidy reported 0 warnings on the changed files.
- [ ] Every flag on the task was honoured (next block), and every question in the "Tasks that stop for the user's ruling" table that applies was asked and its answer recorded in the task section.
- [ ] The commit holds only this task's files; no temporary probe TU, no build output, nothing amended, nothing pushed.

**For every flag a task carries:**

- [ ] CROSS-PLUGIN IMPACT: the `grep -rl` consumer list is in the commit message; every executable `node tools/header-consumers.js --tag '[membrum]'` named was built and is in the `GATE GREEN` line; each consumer golden that moved was diffed and justified; where the task promised bit-identity for Innexus, Seraphis, Ruinae or Iterum, a test shows it.
- [ ] STATE-COMPAT: `kCurrentStateVersion`, `kBlobVersion`, `kPadBlobVersion` and the generator's `kVersion` carry the same number; a version-5 blob captured before the change loads and its migrated values are asserted; versions 1 and 2 load after T2.8; an unknown version is rejected with the current state untouched; no parameter changed type at an existing ID and no per-pad offset or global ID was renumbered (compared with `plugin_ids.h` and `dsp/pad_config.h` at the release tag `membrum/v0.12.5`); `getState(setState(kit))` is idempotent for all 20 factory kits.
- [ ] PRESET-IMPACT: the kits were regenerated by `generate_membrum_presets`, never edited by hand, and a second generator run changes no file; 20 kits, 5 per category, in Acoustic, Electronic, Percussive and Unnatural; `maxPolyphony` in [4,16] on every kit; the kit-quality probe ran before and after and the commit message lists every affected pad's numbers; the user's ruling is recorded where a kit audibly changed; `tests/golden/phase1_default.bin` was re-baselined only with a stated reason.
- [ ] PHYSICS: each new value has its source (authors, title, year, equation or table) in a comment next to it, and a test compares the coded values with numbers typed from that source; no citation claims a source the report could not open.
- [ ] UI: no interaction was removed; a bulk `editor.uidesc` change has its XSLT stylesheet under `specs/membrum-audit/xslt/`; `node tools/lint-plugin-editor-baseline.js` and the uidesc inventory check pass; `node tools/gen-screenshots.js membrum` was rerun and the new images are in the commit.
- [ ] THREADING: no lock, allocation or direct engine write on the hand-over; the deterministic interleaving test and the two-thread stress case are green; the commit message says which sanitizer runs were done (ThreadSanitizer under GCC or Clang, or the ASan build on Windows) and which are left to CI.
- [ ] MACOS: `tools/macos-fastmath-repro.sh` (for DSP TUs) or `tools/clangcl-fastmath-build.ps1` (for plugin test TUs), `node tools/lint-apple-globals.js`, `node tools/lint-ieee-flag-sets.js`, `node tools/check-portability.js`, `node tools/lint-finite-bitmask.js` and `node tools/lint-nonfinite-symbols.js` were run and their last lines recorded; anything no proxy covers is marked "unverified on macOS until CI".
- [ ] VERIFY-FIRST: each PLAUSIBLE finding has a verdict, "fixed" or "refuted", with the test output that decided it; a refuted finding changed no production code and its test was kept.

**For the whole plan, after the last task:**

- [ ] The "Findings to tasks" table has an outcome for all 259 findings: fixed (commit, file:line, test name, measured value), refuted under VERIFY-FIRST (test name, output), or not fixed (the user's ruling or the blocker, stated). No finding is silently dropped and none is marked fixed because a neighbouring finding in the same task was.
- [ ] The per-push suites of everything the branch changed are green in one sharded run: `node tools/run-gate.js --share 4 --base main --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' membrum_tests <every executable header-consumers.js named during the plan>` ends `GATE GREEN`. The line is recorded.
- [ ] The timing lane ran once, alone, after the last engine change: `node tools/run-cpu-tests.js membrum_tests`. No budget was relaxed; T7.1's benchmark measures a voice whose tone shaper is not bypassed.
- [ ] pluginval at strictness 5 exits 0 on the final `Membrum.vst3`.
- [ ] The audit probes were re-run on the final code (or their promoted phase-8 tests were) and compared with the baseline in `specs/membrum-audit/probes/` and report sections 3 and 4. Each line below is recorded as baseline value, new value, verdict:
  - [ ] Kit and pad preset save then reload (T1.3). Baseline: `noiseLayerGain` (slot 57) lost on 32 of 32 pads; on the default kit the B1 sentinel became 0.5 on 22 pads and B3 on 16; default snare 38 energy above 3 kHz -7.84 to -42.35 dB; kick decay 0.97 to 0.19 s; tom 41 1.54 to 0.23 s. Required: `getState` byte-identical for the default kit and all 20 factory kits, and the same for a pad preset.
  - [ ] Kit browser load (T1.2). Baseline: 2119 begin / perform / endEdit triples and 162 coupling-matrix rebuilds in the next block; out-of-range slots load differently by route (decay 1.7 against 1.0); a hybrid kit for one block (snare max difference 0.477). Required: the bounds T1.2 states, both routes equal, no hybrid kit.
  - [ ] Note timing (T1.14). Baseline: every onset at the block start; output differs by up to -8.8 dBFS between block sizes. Required: onset within 2 samples of `sampleOffset`; pairwise RMS difference at or below -80 dBFS across block sizes 32, 64, 256, 1024 and 4096.
  - [ ] Pitch envelope (T1.11). Baseline: a 20 ms 160 to 50 Hz sweep is wrong by 233 cents at block 64 and by 2012 cents at block 1024. Required: within 50 cents of the block-8 reference at every 1 ms point, at block 64 and block 1024.
  - [ ] Voice retirement (T1.7, T1.8). Baseline: a Feedback-exciter voice never retires; a String voice is cut after 256 host blocks or never retires after a body swap. Required: both retire, and no tail is cut before its natural decay.
  - [ ] Tone-shaper filter (T1.15). Baseline: a hat at LP 317 Hz plays 46 dB louder on a fresh voice slot than on a warm one. Required: fresh and warm within 1 dB.
  - [ ] Macro round trip (T1.22). Baseline: Brightness 0.5 to 1 to 0.5 leaves the cutoff at 0.8 instead of 1.0. Required: every macro restores every target field exactly.
  - [ ] Hi-hat choke on a fresh instance (T2.7). Baseline: the choke table reads 0, 0, 0 for notes 42, 44 and 46. Required: the open hat is choked without a prior state load.
  - [ ] Released states (T2.8). Baseline: versions 1 and 2 are rejected. Required: both load with the migrated values asserted.
  - [ ] Non-finite and out-of-range state values (T2.2). Baseline: NaN and Inf in slots 0 to 33 are stored and written back by `getState`; slots 0-27 and 30-33 are not clamped. Required: sanitised on load, finite output, the voice retires.
  - [ ] Note-off (T2.9). Baseline: a note-off at 100 ms cuts 180 of 185 long pads to under half their decay (median ratio 0.21; kick 0.97 to 0.10 s, tom 1.54 to 0.06 s). Required: what the user ruled, pinned by the render comparison.
  - [ ] String level (T2.11). Baseline: peak -0.02 dBFS against about -6 dBFS for every modal body. Required: within 1 dB of -6.02 dBFS.
  - [ ] Sample-rate invariance (T2.12, T2.13). Baseline: the processor-level kick pitch and decay depend on the sample rate; the noise layer drops about 6 dB at 192 kHz. Required: fundamental spread at most 5 cents and decay spread at most 10 % across 44.1, 48, 96 and 192 kHz; noise level within 1 dB.
  - [ ] Silence flag (T2.26). Baseline: coupling at maximum clears the main-bus silence flag on every block with no notes. Required: the flag is set once nothing sounds.
  - [ ] Limiter hits. Baseline: 21 pads in 10 kits reach the limiter ceiling (-7.00 dBFS) on a single velocity-100 hit. Required: the count after T1.16, T1.17, T1.18, T2.16 and T3.4, with each remaining pad named and explained.
  - [ ] Parameter audibility (T4.1, T5.1 to T5.5, T8.3). Baseline: 65 of 71 parameters ok, 4 suspicious, 2 bad; Snare Buzz and Tom Resonance bit-identical, Coupling Amount only a gate, Coupling Delay and Global Coupling inaudible, 12.1 to 12.2 dB jumps between adjacent `modeInject` and `fmRatio` steps. Required: no bit-identical or inaudible parameter in its own context; adjacent-step jumps below 6 dB.
  - [ ] Pad classification (T3.3). Baseline: 2 of 517 enabled factory pads classify as Snare. Required: every factory note-38 snare classifies as Snare, and congas and bongos as Tom.
  - [ ] Velocity and timbre (T4.13, T8.4). Baseline: 10 of 60 reference notes get darker when hit harder (centroid ratio 0.25 to 0.9). Required: ratio at least 1.0 on notes 36, 38 and 42 of every kit.
  - [ ] Mode Stretch (T4.5). Baseline: neutral 1.0 still stretches Plate, Shell and Bell partials by up to +59 cents at partial 8. Required: table ratios within the task's tolerance, or the user's ruling to keep it.
  - [ ] Kit sameness (T5.7 to T5.13, T8.2). Baseline: 19 near-duplicate pairs in 9 kits, among them bit-identical pads (Tabla 53/66 and 36/42, Experimental FX 42/44) and identical notes 36 and 38 in Hand Drums and Latin Perc. Required: 0 near-duplicate pairs, or each remaining pair ruled on by the user.
- [ ] What the baseline measured as OK is still OK: velocity curves; the -1 dBTP limiter and the master gain (-24, -6 and +12 dB exact); the pan law; multi-bus (aux is a pre-master send, a null aux buffer does not crash); no coupling runaway; proxy equals per-pad (0 of 62 differ); `getState` idempotent for all 20 factory kits; malformed blobs rejected; no mode at or above Nyquist; no NaN anywhere; no factory pad silent or with a DC offset; no two kits closer than the baseline's 6.7 dB.
- [ ] Lints and checks, each run and its last line recorded: every `tools/lint-*.js` the audit lane ran exits 0; `node tools/check-au-plist.js membrum`; `node tools/uidesc-inventory.js plugins/membrum/resources/editor.uidesc --check` with the allow file from T3.2; `node tools/lint-site-seo.js` after T6.9; the Membrum entries in `tools/lint-finite-bitmask-baseline.json` and `tools/lint-nonfinite-symbols-baseline.json` are gone after T3.1.
- [ ] The macOS proxies were run on the final tree (see the MACOS flag); what they cannot see is listed as unverified until CI, per operating system. "Zero warnings" is claimed only for the operating systems whose full build log was read.
- [ ] Documentation matches the final code: `MANUAL.md`, `docs/index.html`, `docs/membrum_signal_flow.dot`, `plugins/membrum/CLAUDE.md`, `.claude/skills/membrum/SKILL.md`, the header comments T6.7 lists, and the compliance rows of specs 135 to 141 that this plan made true or false. `plugins/membrum/CHANGELOG.md` lists every user-audible change under the unreleased version: each PRESET-IMPACT change, the state version and what older states load as, the automation change of T1.20. No version bump unless the user asks.
- [ ] The prior audits were not rewritten: `plugins/membrum/docs/correctness-audit.md` and `plugins/membrum/AUDIT-signal-path-2026-06-07.md` are unchanged except for status notes the user asked for.
- [ ] The tree is clean: `plugins/membrum/tests/audit/` does not exist, `git status --porcelain` shows nothing unintended, and nothing was pushed.
- [ ] Self-check, answered in writing: Is any threshold looser than the plan or the report states? Is any line above ticked without a number or a log line behind it? Was any feature removed without a ruling? Is any finding reported fixed that was only partly fixed? Each "yes" is listed as a gap, not hidden.
