# Disrumpo Audit Report

**Date:** 2026-11-05
**Plugin:** Disrumpo (Multiband Morphing Distortion), version **0.9.11** (`plugins/disrumpo/version.json`)
**Commit:** `3abeef52` (branch `audit/disrumpo`, created from `main`)
**Repository root:** `f:/projects/iterum`; all paths below are relative to it.

## Executive summary

This report is a full correctness audit of the Disrumpo VST3/AU plugin: the processor split across `processor.cpp` / `processor_state.cpp` / `processor_params.cpp` / `processor_messaging.cpp`, the band/morph/adapter DSP under `plugins/disrumpo/src/dsp/`, the shared KrateDSP primitives and processors it wraps, the controller monolith and its sub-controllers and views, `editor.uidesc`, the 121 factory presets, the test suites, and the specs and docs. The audit ran 14 static dimensions (rt-safety, dsp-numerics, signal-path, morph+oversampling, sweep+modulation, lifecycle-state, ui-wiring, shared-dsp-contracts, duplication, spec-conformance, portability-quality, test-gaps, gestalt-processor, gestalt-controller) and a 7-step measurement lane (lints, build, sharded gate, pluginval, 21 hidden audio probes producing 551 measurements, clang-tidy, portability). Every finding was then checked by three independent verifiers (correctness, context, impact lenses); a critic round added six follow-up finder passes that produced 19 more findings, verified the same way. Refuted findings are listed in Appendix A so they are not re-found.

The headline defects: (1) **the plugin never oversamples**: `Processor::process` calls the per-sample `BandProcessor::process`, which skips the oversamplers, the band bypass and the factor crossfade, so every shaper aliases at 1x and the Oversample Max and Band Bypass controls do nothing (D-017, D-027, D-095, D-116); (2) **one mono MorphEngine serves both channels and is prepared at 8x the host rate** (D-016, D-009), so the stereo image collapses (R leaks to within 1 dB of L), the 4 kHz tone filter acts at about 1 kHz, morph smoothing runs 4x slow and time-based types run at the wrong rate; (3) **a same-family morph with a Spectral node dominant truncates the FFT size and re-prepares the STFT on every sample**, allocating on the audio thread and silencing the Spectral wet path (D-001, D-002, D-007, D-106); (4) **state restore is broken in several independent ways**: `setupProcessing` wipes the morph nodes and crossovers restored by `setState`, `setActive(true)` wipes every mod routing and macro, `getState` writes literal 0 for morph mode and smoothing, sweep width/intensity are not restored, host-restored shape slots are never mapped to DSP params, and the controller-side preset writer corrupts routing sources and active-node counts (D-046, D-047, D-048, D-049, D-050, D-052, D-053, D-085, D-117); 120 of 121 factory presets render differently depending on the host's call order; (5) **the sweep system is largely inert**: its LFO and envelope follower advance once per block, bands outside the sweep window are muted rather than left clean, band centres are hard-coded, intensity above 100 % is clamped away, and the sweep-to-morph link exists only inside the editor (D-035, D-036, D-037, D-019, D-039, D-137); (6) **MIDI learn cannot work** because the processor declares no event input bus and the two halves of the feature watch different parameter IDs (D-096, D-136). The measurement lane also found that neutral settings sit 3 dB below unity (pan law), that a 50 % global mix carves 16.6 dB notches at every crossover (no dry-path phase compensation), that cross-family morphs dip up to 19.5 dB and click, and that Stochastic emits -10 dBFS of noise from digital silence.

## Statistics

| Metric | Count |
|---|---|
| Raw static findings | 180 |
| Deduplicated static findings | 137 |
| Measured findings (from the probe/lint/build lanes) | 17 |
| Follow-up findings (critic round) | 19 |
| **Confirmed** | **154** |
| **Plausible** (one refute vote) | **10** |
| **Refuted** (two or more refute votes) | **9** |
| Unverified | 0 |
| Tie-breaks upheld | 0 |
| Finders failed | none |
| Measurement steps not run | none |

Confirmed + plausible findings by final severity: critical 7, high 42, medium 68, low 47 (164 surviving findings, D-001 … D-173 minus the 9 refuted).

## Measurement lane

Each step below reproduces the lane runner's summary verbatim, its verdict, its log path, and its items.

### Step 1: lints

**Verdict:** RED (one lint red: `uidesc-inventory.js --check`; seven lints green).
**Log:** `f:/tmp/disrumpo-audit/uidesc-inventory.log` (one log per lint under `f:/tmp/disrumpo-audit/`).

> Ran 8 lints in a row, each logged under f:/tmp/disrumpo-audit/. Seven exited 0 with no Disrumpo item: lint-odr (958 definitions scanned, no cross-file name collisions), lint-layers, lint-apple-globals, lint-finite-bitmask, lint-nonfinite-symbols, lint-plugin-editor-baseline and lint-wallclock-tags. `uidesc-inventory.js plugins/disrumpo/resources/editor.uidesc --check` exited 1 with the verdict line `UIDESC DEFECTS {"overlap":20,"out-of-bounds":84}`. All 104 defects are in Disrumpo's editor.uidesc. They fall into two groups: 84 out-of-bounds views and 20 overlaps.

| Kind | File | Line | Item |
|---|---|---|---|
| lint | plugins/disrumpo/resources/editor.uidesc | - | out-of-bounds [editor]: 84 views, ModRoute12..ModRoute32 (Source/Dest/Amount/Curve, 4 each). Parent container is 500x198, but the rows sit at y=198..558 (first: ModRoute12Source at 10,198 size 80,16 in parent 500,198; last: ModRoute32Curve at 320,558 size 65,16). They lie outside the parent's bounds (likely clipped or unreachable unless the container scrolls). **Ruled a lint false positive: the rows are inside a CScrollView with container-size 488x576 (D-149).** |
| lint | plugins/disrumpo/resources/editor.uidesc | - | overlap [editor]: 4x 'CViewContainer at 6,6,684,284 / CViewContainer at 0,0,684,284'; 4x Band2Container 240,0,235,340 / CViewContainer 0,0,690,290; 4x Band3Container 480,0,235,340 / CViewContainer 0,0,690,290. **Intentional shadow/border/overlay structure (D-150 refuted).** |
| lint | plugins/disrumpo/resources/editor.uidesc | - | overlap [editor]: CView SpectrumDisplay 20,60,960,120 / CView SweepIndicator 20,60,960,120; SpectrumDisplay and SweepIndicator both / CSegmentButton SpectrumMode 860,62,115,20. **Intentional overlay (D-151 refuted).** |
| lint | plugins/disrumpo/resources/editor.uidesc | - | overlap [BandShapeTab]: CSlider Band.DisplayedType 0,0,1,1 / UIViewSwitchContainer 0,0,215,200; [BandExpandedStrip]: CSegmentButton Band.ActiveNodes 70,182,120,20 / ArcKnob Band.MorphSmoothing 75,200,30,25; NodeEditorBorder 0,0,280,230 / CViewContainer 0,0,280,20, / CSlider Band.DisplayedType 0,22,1,1, / UIViewSwitchContainer 32,22,215,200. **ActiveNodes/MorphSmoothing 2 px overlap is real (D-152); the 1x1 switch slider and NodeEditorBorder overlaps are intentional (D-153 refuted).** |

### Step 2: build

**Verdict:** RED (compiles and links, but 4 warnings against the project's zero-warning rule).
**Log:** `f:/tmp/disrumpo-audit/build.log`

> Built disrumpo_tests and Disrumpo with "cmake --build build/windows-x64-release --config Release --target disrumpo_tests Disrumpo -- -m:2". It exited 0 after 54 s. The bundle is at build/windows-x64-release/VST3/Release/Disrumpo.vst3. The install and preset copy steps succeeded. There were no errors. The log has 4 warnings, all the same C4127 in the Highway third-party header (math-inl.h line 1026) while compiling KrateDSP. They are probably four copies of one warning from a single header (4 log lines), so it is the same 1026,24 location each time. The project requires zero warnings, so this is flagged.

| Kind | File | Line | Item |
|---|---|---|---|
| build-warning | build/windows-x64-release/_deps/highway-src/hwy/contrib/math/math-inl.h | 1026,24 | warning C4127: conditional expression is constant [KrateDSP.vcxproj] (appears 4 times; third-party Highway header included from KrateDSP). See D-154. |

### Step 3: gate (sharded per-push test suite)

**Verdict:** GREEN.
**Log:** `f:/tmp/disrumpo-audit/gate.log` (per-shard logs under `f:\tmp\gate\20261105-201625`).

> Ran the sharded per-push Disrumpo suite once. Command: node tools/run-gate.js --share 4 --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' disrumpo_tests. Wall clock was 7.1 s with 7 workers. Verdict line: "GATE GREEN disrumpo_tests cases=510 assertions=595549 wall=7.1s workers=7 logs=f:\tmp\gate\20261105-201625". There were 0 failed cases across 36 shards, and the slowest shard took 6.4 s. The [wallclock] second pass ran 0 cases. I did not inspect the per-shard logs for skipped, hidden or [!mayfail] cases, because the gate summary showed no failures. I did not run run-cpu-tests.js.

No items. Note for readers: the suite is green while most of the critical and high findings below exist, which is itself the subject of the test-gap findings (D-114 … D-131).

### Step 4: pluginval

**Verdict:** GREEN (exit code 0 at strictness 5).
**Log:** `f:/tmp/disrumpo-audit/pluginval.log`

> Ran pluginval at strictness level 5 on build/windows-x64-release/VST3/Release/Disrumpo.vst3. Exit code 0, which is a pass. It took about 6 s wall clock. I read only the tail of the log and grepped it for fail/error. That grep printed nothing, and the tail showed the bus tests completing normally. pluginval skipped the vst3 validator because no validator path was set. That is an environment setting, not a plugin failure.

No items.

### Step 5: audio probes

**Verdict:** RED (GATE GREEN for the probe TU itself; 56 bad and 42 suspicious per-type lines, 57 bad preset lines, plus the bad lines listed below).
**Log:** `f:/tmp/disrumpo-audit/probe-results.txt` (551 PROBE lines; raw shard logs in `f:/tmp/gate/20261105-203701`).
**Probe source:** `specs/disrumpo-audit/probes/audit_probes.cpp.txt` (hidden `[.][audit-probe]` TEST_CASEs; the tree was reverted after the run and `git status --porcelain plugins/disrumpo` is empty).

> I wrote 21 hidden [.][audit-probe] TEST_CASEs and built disrumpo_tests with -m:2: zero compile warnings, and the only warnings in the log are CMake configure warnings from the highway/pffft deps. I ran them through run-gate.js --share 4, sharded into 36 shards on 7 workers: GATE GREEN, wall 12.5 s, so the 8-minute budget is no concern. The final run printed 551 PROBE lines to f:/tmp/disrumpo-audit/probe-results.txt (raw shard logs in f:/tmp/gate/20261105-203701). Two procedure notes: the filter needed the ~[performance]~[perf]~[benchmark]~[!benchmark] exclusions because run-gate.js refuses a filter without them, and the crossover probes needed a 0.3 s pre-roll because the CrossoverLR4 frequency smoothers ramp after prepare(). The TU is saved at specs/disrumpo-audit/probes/audit_probes.cpp.txt. CMakeLists.txt is reverted, the audit/ folder is deleted, and git status --porcelain plugins/disrumpo is empty. The built exe in build/ still contains the probe cases until the next rebuild.
>
> Main measured defects:
> (1) The Processor never oversamples. processor.cpp:487/494 calls BandProcessor::process() per sample, which skips the oversamplers. Aliasing at the 1x and 8x OS limits is identical (-43.3 dB). BandProcessor::processBlock does oversample (18.4 dB less aliasing), but nothing calls it from the Processor.
> (2) One mono MorphEngine is shared by L and R (band_processor.h:487-488) and is prepared at 8x the sample rate (band_processor.h:111,124). Effects: R leaks to within 0-1 dB of L for every stateful type tested; the 4 kHz tone filter acts at about 1 kHz; morph smoothing takes 37 ms instead of 10 ms; time-based types are off (adapter-vs-processor centroid ratio 0.08 for SergeFold, 0.28 for FeedbackDist, 0.36 for RingSat).
> (3) setupProcessing wipes state. It resets node types and crossover frequencies (processor.cpp:101,114-120). 120 of 121 presets change when setState runs before setupProcessing; 114 change level by more than 1 dB, and Sharp_Focus drops by 187 dB.
> (4) Band bypass does nothing on the Processor path. Only processBlock checks bypassed_ (band_processor.h:521).
> (5) Neutral settings give -3.01 dB, not 0 dB. This is the centre-pan law (band_processor.h:502-510), and it also leaves a -16.6 dB notch at global mix 50 %.
> (6) The sweep is broken in four ways. The LFO advances once per block (processor.cpp:350): 0.24 octave range and 0 cycles where about 4 octaves and 10 cycles were expected. The envelope follower reads one sample per block (processor.cpp:342): 320 ms to 63 % against a 10 ms attack. Bands away from the sweep centre are muted (-125 dB). Intensities above 100 % are clamped away (band_processor.h:197).
> (7) Morph clicks: the 4-node cross-family sweep has a jump ratio of 9.9 with 54 clicks, and level dips 13-19.5 dB below the start/end level line. Changing the band count clicks (jump ratio 3.8-3.9).
> (8) Silent-input noise and DC: Stochastic outputs -10 dBFS from silence; FeedbackDist and Quantize still sit at -48 dBFS 1 s after the input stops; Tube has DC of -0.04 to -0.067.
> (9) Presets: 35 lose audio in a getState/setState round trip. Two fresh loads match exactly, so this is state loss: the files are v10, saves are v12, and repeated saves are not stable (first drift at byte 92, the sweep LFO rate field). 11 presets peak above 0 dBFS from a -12 dBFS input, 39 have |DC| above 0.01, and Rectified_Drums is below -60 dBFS.
>
> What passed: crossover reconstruction after settling (SC-001, max deviation 0.004 dB); the cursor parked exactly on a node (weights finite and sum to 1); no NaN or Inf anywhere, including at 192 kHz; gain, mute, solo, drive and mix steps are click-free.
>
> The measurements list carries every non-ok line, either as its own entry or grouped by class (all 70 bad and suspicious preset lines are in the grouped preset entries, and every preset-hostorder line is in one entry); most ok lines are counted in summary entries. probe-results.txt has every line.

Two caveats that the follow-up round attached to this lane: the "35 presets lose audio" wording is not supported by a level measurement of the reloaded render (D-161), and the per-preset peak/RMS/DC baseline was measured on the host-restore (`setState`) path, which never maps shape slots to DistortionParams, so it describes that defect rather than the presets as authored (D-160). Re-measure both after the fixes.

#### Probe measurements (baseline for the post-fix re-run)

| Name | Setup | Metric | Value | Threshold | Verdict | Note |
|---|---|---|---|---|---|---|
| os-engaged:processor:HardClip | Processor 1 band HardClip drive 1.0, 1031.25 Hz -6 dBFS, 48k, OS max 1x vs 8x | nonharm_dB@1x,nonharm_dB@8x,reduction_dB,band_factor@1x,band_factor@8x | -43.3,-43.3,0.0,1,4 → **Phase 6 (2026-11-09):** non-harmonic 26.85 dB at 1x, 0.90 dB at 4x (power, last 4 x 8192 samples; fundamental 63.05 / 63.04 dB): reduction 25.95 dB, factors 1 / 4. Asserted (>= 12 dB) in the extended `integration/processor_audio_output_test.cpp` "Processor HardClip aliasing drops with Oversample Max 4x vs 1x" (T1.07 asserts the aliased-bin reduction, now 25.98 dB; T6.05 adds the non-harmonic one). | >= 12 dB aliasing reduction when band reports factor 4 (spec 009) | bad | The band reports factor 4, but the Processor calls BandProcessor::process() per sample (processor.cpp:487,494), which never touches oversampler2x_/4x_ (band_processor.h:473-512). Only processBlock (band_processor.h:519) oversamples, and the Processor never calls it. |
| os-engaged:bandprocessor-block:HardClip | BandProcessor::processBlock direct, HardClip drive 10, tone 8k, 1031.25 Hz -6 dBFS, max 1x vs 8x | nonharm_dB@1x,nonharm_dB@max8,reduction_dB,factor@1x,factor@max8 | -48.8,-67.2,18.4,1,4 | >= 12 dB reduction | ok | The Oversampler works when the block path is used. This is the control for the Processor probe above. |
| os-latency:bandprocessor-block | BandProcessor::processBlock HardClip drive 0.1 (linear), tone 8k, impulse 0.5, max 1x vs 8x | factor@1x,factor@max8,peak_idx@1x,peak_idx@max8,gd1k_smp@1x,gd1k_smp@max8,relative_gd_smp | 1,4,1,8,3.04,7.42,4.38 → **Phase 6 (2026-11-09):** Processor path: impulse peak 4 / 7 samples at 2x / 4x = getLatencySamples 4 / 7 (existing "getLatencySamples equals the measured impulse delay at every Oversample Max", T2.10, +/-1). Group delay at 1 kHz reads 3.01 / 6.48 samples: 0.99 / 0.52 below the report, outside the probe's 0.5-sample bar; that assertion is held back for a ruling (follow-up 81). Investigated 2026-11-09: the report is the oversampler's impulse-peak index (2x peak 0.5923 at index 4 vs 0.5442 at 3), not a group delay; 2x group delay 3.00 from DC to 2 kHz, 4x 6.47; a 1x + 2x band crossover sum dips -0.44 dB at a 5 kHz crossover. Options in follow-up 81. **Ruling applied 2026-11-09 (fractional alignment to the group delay, reported latency unchanged):** group delay at 1 kHz 4.005 / 7.008 against 4 / 7, asserted GREEN (bar 0.1). **Minimax ruling (2026-11-09) applied:** each band arrives at L + its minimax offset (48 kHz: 2x -0.759 without a 4x band, -0.409 with, 4x -0.506), measured 3.246 / 6.502 at 1 kHz, asserted to +/- 0.1; flatness table in follow-up 81. | abs(relative group delay) <= 0.5 smp (getLatency() returns 0) | bad | The 'ZeroLatency' IIR 4x path adds 4.4 samples of group delay at 1 kHz (peak at sample 8 vs 1), while band_processor.h:376-379 reports 0. If processBlock were wired in, bands at different factors would be misaligned and dry/wet would comb. (Filed as D-147; refuted as unreachable today, see Appendix A.) |
| band-latency:processor:HardClip-vs-Bitcrush | Processor 2 bands, b0 HardClip (4x profile) b1 Bitcrush (1x), drive 0.1, tone 8k, 0.3 s pre-roll, impulse 0.5, other band muted, vs drive 0 | b0_factor,b1_factor,b0_extra_delay_smp,b1_extra_delay_smp,relative_smp | 4,1,4,0,4 → **Phase 6 (2026-11-09):** relative arrival 1 sample (HardClip band 8, Bitcrush band 7, getLatencySamples 7): still RED against the bar 0, held back (follow-up 81). Investigated 2026-11-09: as group delays the bands are 7.31 vs 7.00 samples apart: the 4x oversampler 6.47 (half-sample rounding of the integer compensation) plus the HardClip node's own 8 kHz Tone, 0.84, which Bitcrush skips by design. Still held. The 1x-passthrough vs 4x-HardClip pair of "Bands at different oversampling factors arrive on the same sample" (T2.10) reads 0 and stays asserted. **Ruling applied 2026-11-09:** rewritten as group-delay arrival of oversampler-only bands (1x / 2x / 4x pairs at 44.1 / 48 / 96 kHz): -0.52 to -0.99 samples before, within 0.010 sample after, asserted GREEN; the node Tone stays part of the type (not aligned). | relative 0 samples | bad | Oversampling is never used in the Processor, so this is not OS latency. The lane attributed it to the tone filter (applied for HardClip, skipped for Bitcrush at distortion_adapter.cpp:225-231); the verifiers refuted that attribution (D-146, Appendix A) and point at the oversampler path the probe exercised. The inter-band misalignment itself remains measured and unexplained on the live path. |
| band-latency:processor:HardClip-vs-SoftClip | as above, b1 SoftClip (tone applied) | b0_factor,b1_factor,b0_extra_delay_smp,b1_extra_delay_smp,relative_smp | 4,2,4,1,3 → **Phase 6 (2026-11-09):** relative arrival 1 sample (8 / 7): still RED, held back (follow-up 81). Investigated 2026-11-09: as group delays 7.31 vs 6.56-6.71 samples: +0.46 from the 4x-vs-2x rounding of the oversamplers' fractional delays, the rest the tone filter clocked at 4x vs 2x. **Ruling applied 2026-11-09:** 2x vs 4x oversampler-only bands +0.48 samples before, within 0.010 after (GREEN); tone filters not aligned by ruling. | relative 0 samples | bad | Both bands use the tone filter yet still differ by 3 samples. The cause was not isolated (HardClip vs SoftClip processRaw paths, distortion_adapter.cpp:261-300). |
| stereo-leak (SoftClip,Tape,Tube,FeedbackDist,AllpassResonant,Temporal,Formant) | 1 band, drive 0.5, L = 1031 Hz -6 dBFS, R = digital silence, centre pan, 48k | R_out_rms_dBFS,L_out_rms_dBFS per type | SoftClip -13.5/-12.9; Tape -13.7/-13.7; Tube -15.2/-14.6; FeedbackDist -24.0/-24.0; AllpassResonant -17.7/-17.7; Temporal -18.0/-17.4; Formant -7.1/-7.1 → **Phase 6 (2026-11-09):** R -240 dBFS (exact zero) for all 7 types; L SoftClip -1.1, Tape -7.0, Tube -3.0, FeedbackDist -35.9, AllpassResonant -0.7, Temporal -4.6, Formant -1.3 dBFS. Asserted (R < -90 dBFS, L > -40 dBFS) for all 7 in `integration/processor_stereo_test.cpp` "Processor keeps L and R independent through a morph band" (T1.05 covered Tape, FeedbackDist and Formant; T6.03 folded those sections into one loop over the probe's 7 types). | R <= -120 dBFS | bad | One mono MorphEngine (and its adapters) processes L then R each sample (band_processor.h:487-488; the legacy path at 493-494 does the same with distortion_). Filter, envelope and delay state is shared, so L bleeds fully into R and every filter steps twice per sample. (D-016) |
| tone-response (1k/2k/4k/8k) | 1 band SoftClip drive 0.01, mix 1, tone 4000 Hz, level relative to 100 Hz; Processor vs DistortionAdapter prepared at 48k | proc_rel_dB,adapter_rel_dB,proc_implied_fc_Hz,adapter_implied_fc_Hz | 1k: -2.98,-0.25,1007,4070 / 2k: -7.00,-0.94,999,4064 / 4k: -12.46,-2.91,981,4095 / 8k: -18.93,-6.59,910,4238 → **Phase 6 (2026-11-09):** tone 4 kHz, re 250 Hz: processor -0.24 / -0.93 / -2.90 / -6.58 dB = adapter@48k -0.24 / -0.93 / -2.90 / -6.58 dB at 1x; -0.24 / -0.95 / -2.97 / -6.87 dB at 2x (largest gap 0.29 dB, the 2x oversampler near 8 kHz). Asserted (within 1 dB) in the extended `integration/processor_audio_output_test.cpp` "Processor tone filter cutoff matches the Tone parameter" (T6.02), next to its T1.06 one-pole checks. | proc within 1 dB of adapter@48k | bad | The tone filter's cutoff is 4x too low (1 kHz for 4 kHz): it is prepared at sampleRate*8 (band_processor.h:111-112,124-125) but run at 1x and stepped twice per sample (L then R, band_processor.h:487-488). setCommonParams -> toneFilter_.setCutoff (distortion_adapter.cpp:168-171). (D-009) |
| adapter-vs-processor (26 types) | drive 7.5, mix 1, tone 4000, same DistortionParams; adapter at 48k mono vs Processor 1-band L, 1031.25 Hz -6 dBFS | rms_diff_dB(proc-adapter),centroid_ratio | 19 bad, 6 suspicious, 1 ok. SergeFold -11.9 dB/0.077; FeedbackDist +9.9/0.282; RingSaturation +5.9/0.359; SineFold and TriangleFold -11.4/0.494; FullRectify -16.7/0.988; AsymFuzz -12.5/0.998; Stochastic -21.4/2.77; most others about -6 dB/0.8-1.0; Bitcrush and BitwiseMangler exactly -3.01/1.000 → **Phase 6 (2026-11-09):** all 26 within 0.00 dB, centroid ratio 0.997-1.000, against an adapter prepared at the band's clocked rate (1x / 2x / 4x) and read below 18 kHz: `integration/processor_rate_parity_test.cpp` "Processor renders every type like a DistortionAdapter at the band's clocked rate" (T6.02, GREEN). Before the drive-route fix (`fix(disrumpo): a Drive change re-routes Temporal and Stochastic`) Temporal read +6.22 dB / 1.255 and Stochastic +4.17 dB / 1.363. RingSaturation is measured with its Fixed carrier: the default Track carrier's level depends on its lock phase (2.3 dB at 4x). An adapter at the host rate (the probe's reference) reads centroid 0.71 (RingSaturation) to 0.87 for oversampled types at the default Oversample Max: the aliasing the oversampler removes, not a rate error. | abs(rms diff) <= 1 dB and centroid ratio 0.85-1.15 | bad | -3.01 dB is the pan law (band_processor.h:502-510); about -3 dB more comes from the mis-rated tone filter. The large centroid shifts (Serge, FeedbackDist, RingSat, folds) are time-based state running at the wrong rate (prepare(sampleRate*8), band_processor.h:111,124) and interleaved L/R. The processors involved (WavefolderProcessor, FeedbackDistortion, RingSaturation) are shared KrateDSP code, but the misuse is Disrumpo-side. |
| type:SergeFold (drive 0.25/0.75/1.0) | 1 band node0 SergeFold, 1031.25 Hz -6 dBFS, 48k, mix 1, bias 0 | nonharm_dB,dc,nonfinite,peak_dBFS | -14.2,-3.7e-08,0,-47.18 / 17.3,2.2e-08,0,-47.52 / 24.6,-5.2e-09,0,-35.93 → **Phase 6 (2026-11-09):** non-harmonic -121.9 / -121.7 / -112.9 dB, |DC| <= 1e-6, peak -0.74 / -1.76 / -5.07 dBFS (drive 2.5 / 7.5 / 10), asserted GREEN in `tests/dsp/type_render_matrix_test.cpp` "Type render matrix: SergeFold" (T6.01). It reads identical to SineFold at defaults (follow-up 80). | <= -50 dB (4x profile), peak informative | bad | Nearly silent output (-36 to -47 dBFS peak from -6 dBFS) dominated by non-harmonic energy. The adapter at 48k gives a 6424 Hz centroid; the Processor gives 497 Hz. Path: distortion_adapter.cpp:410-420 (WavefolderProcessor fed 1-sample blocks), interleaved L/R, prepared at 8x. (D-145, D-158, D-159) |
| type:SineFold/TriangleFold drive=1.00 | 1 band, 1031.25 Hz -6 dBFS, 48k, drive 10 | nonharm_dB,dc,nonfinite,peak_dBFS | SineFold -3.1,-3.4e-07,0,-31.04; TriangleFold -3.1,0,0,-31.04 (at 0.25/0.75: -46.0/-33.9, suspicious) → **Phase 6 (2026-11-09):** SineFold -121.9 / -121.7 / -112.9 dB, peak -0.74 / -1.76 / -5.07 dBFS, asserted GREEN ("Type render matrix: SineFold"). TriangleFold -65.6 / -51.2 / -43.6 dB: RED at drive 10 (bar -50; 1x reads -16.3), case held back for a ruling (follow-up 79). Investigated 2026-11-09: aliasing of the fold corners, periodic to -337 dB, drive 10 at 4x / 8x / 16x (ideal decimation) -43.4 / -58.1 / -68.4 dB; ADAA1 on the bare fold at 4x -43.7 -> -65.5 dB. **User ruling 2026-11-09 applied (ADAA1 on the fold, WavefolderProcessor opt-in):** -89.7 / -79.5 / -72.9 dB, asserted GREEN ("Type render matrix: TriangleFold"); A/B against the plain fold: level within 0.014 dB, H1..H9 of a 99.6 Hz tone within 0.004 dB. | <= -50 dB (4x profile) | bad | Aliased energy is as large as the harmonics because the 4x-profile folders run at 1x in the Processor (processor.cpp:487,494). The peak is also -31 dBFS, about 25 dB quieter than the input. |
| type:Tape (drive 0.25/0.75/1.0) | 1 band Tape, 1031.25 Hz -6 dBFS, 48k | nonharm_dB,dc,nonfinite,peak_dBFS | -23.6,3.7e-05,0,-10.32 / -25.2,1.1e-04,0,-10.25 / -24.5,1.6e-04,0,-10.25 → **Phase 6 (2026-11-09):** -26.7 / -26.2 / -27.1 dB: still RED (bar -50). Not flutter (flutter 0 reads -26.0, its 5.5 Hz sidebands sit inside the harmonic lobes); the default Hysteresis tape model; the Simple model reads -87.5 dB. Case "Type render matrix: Tape" held back for a ruling (follow-up 79). Investigated 2026-11-09: it is aliasing after all (the model changes with the rate, so 1x vs 2x did not isolate it): the Hysteresis output sits on its +/-Sag magnetisation clamp 48-65 % of each cycle (a hard-clipped square-ish wave, periodic to -340 dB), bare saturator 2x / 4x / 8x / 16x -19.6 / -25.6 / -35.9 / -42.3 dB; at Sag 1 the model diverges (silent at 1x, noise at 2x: follow-up 82). Still held. | <= -50 dB (2x profile) | bad | The non-harmonic energy is probably flutter sidebands (flutter default 0.2, distortion_types.h:91), not just aliasing. The flutter phase runs inside the shared 8x-prepared adapter, so its rate is wrong too. |
| type:Fractal drive 0.25/0.75/1.0 | 1 band Fractal, 1031.25 Hz -6 dBFS, 48k | nonharm_dB,dc,nonfinite,peak_dBFS | -35.2,-1.4e-06,0,-10.55 / -27.9,-3.6e-07,0,-10.48 / -24.2,-2.2e-06,0,-10.35 → **Phase 6 (2026-11-09):** -40.9 / -32.4 / -32.2 dB at 2x (1x -24.6): still RED (bar -50). Case held back for a ruling (follow-up 79; 4x would add to follow-up 1's red Fractal CPU case). Investigated 2026-11-09: aliasing of the Harmonic mode, drive 10 at 2x / 4x / 8x / 16x (ideal decimation) -30.5 / -35.8 / -48.4 / -55.5 dB; 4x passes no drive. Still held. | <= -50 dB (2x profile) | bad | Iterated shaping at 1x (no oversampling on the Processor path, processor.cpp:494). |
| type: aliasing suspicious set | 1 band, 1031.25 Hz -6 dBFS, 48k | nonharm_dB | HardClip 0.75/1.0 -49.2/-43.3; SoftClip 0.75/1.0 -43.0/-43.4; AllpassResonant 0.75/1.0 -46.0/-41.1; FullRectify -42.6 (all drives); HalfRectify -31.4/-41.0/-42.9; RingSaturation -33.7/-34.3/-34.7; FeedbackDist 0.25 -45.8; Granular 0.25 -19.7 → **Phase 6 (2026-11-09):** HardClip -79.7 / -69.2 / -65.7, SoftClip -63.5 / -53.5 / -50.2, AllpassResonant -82.7 / -73.7 / -70.7, FullRectify -72.6, FeedbackDist -114.7 / -100.5 / -90.1 dB: asserted GREEN (T6.01). HalfRectify -42.3 / -51.9 / -54.1 (RED at drive 2.5; -42.3 / -51.7 / -54.1 after the 2026-11-09 normalisation: aliasing of the threshold step, 6 dB per doubling, ADAA1 on the bare shaper at 4x -36.4 -> -60.8 dB; follow-up 79; **user ruling 2026-11-09 applied, ADAA1 on the rectifier:** -72.4 / -81.7 / -84.2 dB, asserted GREEN ("Type render matrix: HalfRectify"); A/B: level within 0.002 dB, H1..H9 of a 99.6 Hz tone within 0.011 dB) and RingSaturation -33.6 / -29.4 / -30.2 (ring modulation is inharmonic by design: a Fixed carrier reads -0.7 dB, depth 0 reads -129.6 dB; user ruling 2026-11-09 exempts it from this bar, "Type render matrix: RingSaturation" committed GREEN on every other bar). Granular is noise by design (not asserted, -6.5 to -9.1 dB). | <= -50 dB for types with OS profile >= 2 | suspicious | All of these would be helped by the oversampling that the Processor never engages (processor.cpp:487,494). |
| type: peak above 0 dBFS (digital types) | 1 band, 1031.25 Hz -6 dBFS, drive 0.75/1.0 | peak_dBFS | Aliasing +8.55/+11.10; SampleReduce +8.49/+10.96; BitwiseMangler +2.48/+4.98; Granular +1.4..+4.6; Chaos +1.7/+3.6; Spectral +0.46/+2.96 → **Phase 6 (2026-11-09):** Aliasing -6.00 / -5.99 / -5.99, SampleReduce -6.01 / -6.02 / -6.01, BitwiseMangler -11.98 / -12.01 / -12.01, Granular +1.90 / -7.97 / -9.82, Chaos -7.09 / -10.19 / -10.67, Spectral -11.02 dBFS: asserted GREEN (<= +6 dBFS). New reds above +6 dBFS: Fuzz +2.01 / +7.50 / +9.44, AsymmetricFuzz +1.57 / +7.03 / +8.96, FullRectify -4.54 / +5.00 / +7.50, HalfRectify -1.45 / +7.80 / +10.28 dBFS (the audit read them 3 dB lower through the centre-pan loss and the mis-rated tone filter). User ruling 2026-11-09: normalised (commit "fix(disrumpo): Fuzz, AsymmetricFuzz and the rectifiers stop following drive above 5", 5 / drive above drive 5): Fuzz +2.01 / +3.97 / +3.42, AsymmetricFuzz +1.57 / +3.51 / +2.94, FullRectify -4.54 / +1.48 / +1.48, HalfRectify -1.45 / +4.28 / +4.26 dBFS; the Fuzz, AsymmetricFuzz and FullRectify cases are asserted GREEN; HalfRectify's case stays held back for its non-harmonic bar (follow-up 79). | <= +6 dBFS | suspicious | Drive multiplies the signal before non-saturating digital shapers (distortion_adapter.cpp:211) with no output normalisation, so peaks reach about +11 dBFS. (D-144) |
| type:Tube DC | 1 band Tube, bias 0, 1031.25 Hz -6 dBFS | dc | drive 0.25: -0.00566; 0.75: -0.033; 1.0: -0.041 → **Phase 6 (2026-11-09):** 9.8e-07 / -2.1e-06 / 7.0e-07 (DC over 1.02 s), asserted GREEN ("Type render matrix: Tube"). | abs(dc) < 1e-3 | bad | Tube is not in the DC-block list (distortion_adapter.cpp:588-591), and the TubeStage output carries DC. (D-075) |
| bias:Tube:bias=-1 / +1 | Tube drive 0.75, bias -1 and +1 | dc,nonfinite,peak_dBFS | -0.0673,0,-7.15 / 0.00686,0,-4.38 → **Phase 6 (2026-11-09):** 8.5e-07 / 7.0e-07, asserted GREEN. | abs(dc) < 1e-3 (ok), < 1e-2 (suspicious) | bad | Asymmetric bias with no DC blocker (distortion_adapter.cpp:588-591, Tube missing). |
| bias: suspicious DC and no-effect types | drive 0.75, bias -1 vs +1, all 26 types | dc | Aliasing 0.00458 (both); Granular +0.00358/-0.00488; SampleReduce -0.00116/+0.00779; SergeFold/SineFold -/+0.00394; 17 types give the same DC and peak at bias -1 and +1 → **Phase 6 (2026-11-09):** every DC-blocked type < 1e-3 at bias -1 / +1 (asserted) except Stochastic -0.00253 (held back, follow-up 79); not DC-blocked, so not asserted: SampleReduce -0.000203 / +0.00194, Aliasing 2.3e-06, Granular +/-3.5e-05. | abs(dc) < 1e-3 | suspicious | Bias reaches only some types (routeParamsToProcessor, distortion_adapter.cpp:594+). Where it does reach a folder (SergeFold input + bias, distortion_adapter.cpp:413) there is no DC blocker. |
| selfnoise:Stochastic | fresh activate, silent input 1 s, drive 1.0, RMS of last 0.5 s | rms_dBFS,nonfinite | -10.4,0 → **Phase 6 (2026-11-09):** -240 dBFS (exact zero), asserted GREEN ("Type render matrix: Stochastic", committed 2026-11-09 with its DC bar exempt by user ruling: |DC| 0.00253 at drive 7.5 / 10 and bias -1 / +1 over 1.02 s, slow drift below the 10 Hz blocker; the self-noise bar is also covered at the adapter in "Adapter self-noise is below -90 dBFS for every type"). | <= -90 dBFS | bad | Produces -10 dBFS of noise from digital silence (distortion_adapter.cpp:573-575 -> StochasticShaper, shared KrateDSP). The tail 1-1.5 s after input stops is -5.7 dBFS. (D-142) |
| selfnoise:Quantize | silent input, drive 1.0 | rms_dBFS | -49.0 → **Phase 6 (2026-11-09):** -40.5 dBFS (tail -40.7), the dither on silence of follow-up 39. User ruling 2026-11-09: intended behaviour; "Type render matrix: Quantize" is committed with Quantize exempt from the two -90 dBFS bars on silence (self-noise, tail) and GREEN on every other bar. | <= -90 dBFS | bad | The quantiser grid maps 0 to a non-zero level (distortion_adapter.cpp:478-490, index = (input+1)/step). Silence becomes a constant offset; the tail is -48.8 dBFS. (D-143, D-010) |
| selfnoise: AsymmetricFuzz/BitwiseMangler/Tape | silent input, drive 1.0 | rms_dBFS | -86.2 / -89.2 / -89.2 → **Phase 6 (2026-11-09):** -240 dBFS each. | <= -90 dBFS | suspicious | Low-level output from silence. (Filed as D-148 and refuted: it is the DC blocker's post-activate settle, and the tail probe for the same types reads -134 to -157 dBFS.) |
| tail:FeedbackDist | drive 1.0 sine 0.68 s then silence, RMS 1.0-1.5 s after stop | tail_rms_dBFS | -47.9 → **Phase 6 (2026-11-09):** -97.6 dBFS 1.0-1.5 s after the input stops, -240 dBFS at 4.5-5 s: asserted GREEN (ringing types: below -60 dBFS within 5 s; AllpassResonant -240 dBFS). | <= -90 dBFS | bad | The feedback loop keeps ringing at -48 dBFS more than 1 s after the input stops. The feedback delay runs inside the 8x-prepared, L/R-shared adapter (distortion_adapter.cpp:542; band_processor.h:111,487-488). (D-078, D-108) |
| per-type ok summary | 26 types x (3 drives, 2 bias, silence, tail, adapter) | count | Of 208 lines: 110 ok, 42 suspicious, 56 bad. Nonfinite = 0 in every per-type render; tail <= -90 dBFS for 23/26 types; self-noise <= -90 dBFS for 21/26 → **Phase 6 (2026-11-09):** T6.01 asserts every bar for 16 types (SoftClip, HardClip, Tube, SineFold, SergeFold, Bitcrush, SampleReduce, Aliasing, BitwiseMangler, Temporal, FeedbackDist, AllpassResonant, Chaos, Formant, Granular, Spectral): GREEN, non-finite 0 everywhere. 10 type cases held back with their reds (follow-up 79, patch `f:/tmp/disrumpo-fix/p6a/t601-held-types.patch`). After the 2026-11-09 rulings: 22 types asserted GREEN (adds RingSaturation, Stochastic and Quantize with one ruled exemption each, and Fuzz, AsymmetricFuzz, FullRectify after the drive normalisation); 4 still held back RED on the non-harmonic bar: Tape, TriangleFold (drive 10), HalfRectify (drive 2.5), Fractal (investigation in follow-up 79). | - | ok | No NaN or Inf from any type at drive 0.25-1.0 or bias +/-1. |
| crossover:network:bands=2/3/4 | CrossoverNetwork direct, default log crossovers (632 / 200,2000 / 112,632,3557 Hz), 0.3 s pre-roll, unit impulse, sum of bands | max_mag_dev_dB | 0.000 / 0.002 / -0.004 → **Phase 6 (2026-11-09):** below 0.0001 dB for 2 / 3 / 4 bands (impulse, 160 log frequencies 20 Hz - 20 kHz), asserted (+/-0.1 dB) in `unit/crossover_network_test.cpp` "CrossoverNetwork impulse response sums flat at the default crossovers, from the first sample" (T6.04). Also covered at 300 Hz - 4.8 kHz by "CrossoverNetwork pink noise FFT flat response" (2/3/4 bands) and for `MultibandCrossover` at 30 Hz - 15 kHz, 44.1-192 kHz. | +/-0.1 dB (SC-001) | ok | SC-001 is met once the crossover frequency smoothers have settled. |
| crossover:network:startup-ramp:bands=4 | CrossoverNetwork direct, impulse at the first sample after prepare() | max_mag_dev_dB,at_Hz | 12.346,80 → **Phase 6 (2026-11-09):** below 0.0001 dB from the first sample after prepare() (prepare snaps since T1.03 / crossover dedup), asserted (<= 0.5 dB) in the same T6.04 case. | info | suspicious | prepare() snaps CrossoverLR4 to its default and then setCrossoverFrequency ramps (crossover_network.h:79-94 -> initializeLogarithmicDistribution 253-269; crossover_filter.h:169-221). For about 5 ms after prepare/reset the bands do not reconstruct. (D-024) |
| crossover:processor:bands=1..4 | Processor, all node drives 0, default crossovers, 0.3 s pre-roll, unit impulse, 160 log freqs 20-20k | ripple_dB(max-min) | 0.000 / 0.000 / 0.007 / 0.007 → **Phase 6 (2026-11-09):** |dev| re input <= 1.0e-06 dB for 1-4 bands at 44.1 / 48 / 96 / 192 kHz (mix 100 %, 480 log frequencies 30 Hz - 18 kHz), asserted (+/-0.1 dB, so ripple <= 0.2 dB) in the extended `integration/processor_audio_output_test.cpp` "Global mix 50 % and 100 % with neutral bands reproduce the input within the crossover error" (T6.04). | ripple <= 0.2 dB | ok | Full-path reconstruction is flat. |
| unity:processor:bands=1..4 | Processor, all drives 0, centre pan, gain 0 dB, mix 100 % | abs_level_dB | -3.010 for every band count → **Phase 6 (2026-11-09):** 0.000 dB at every band count: 1 kHz sine L/R -1e-15 / -6e-08 / -1.2e-07 / -1e-08 dB (existing "Neutral settings are unity gain", T1.08, CHECK |level| < 0.1 dB) and across 30 Hz - 18 kHz within 1.0e-06 dB (the T6.04 mix-100 % rows above). | 0 dB +/- 0.1 | bad | The equal-power pan law gives cos(pi/4) = -3.01 dB per channel at centre (band_processor.h:502-510; also 770-779). With the distortion bypassed the plugin is still not unity. (D-021) |
| drywet-mix50:bands=2/4 | Processor, all drives 0, global mix 0.5, 0.3 s pre-roll, unit impulse | deepest_notch_dB,at_Hz | -16.66 @ 114 Hz / -16.61 @ 2000 Hz → **Phase 6 (2026-11-09):** deepest -1.5e-07 dB (2 bands) / -8.4e-07 dB (4 bands): existing "Global mix 50 % does not notch the crossover frequencies" (T1.09, |dev| <= 0.5 dB re 1 kHz) and "Global mix 50 % ... reproduce the input" (|dev| <= 0.1 dB re input, 1-4 bands, 44.1-192 kHz) assert more than the plan's >= -1 dB; cited, not duplicated. | >= -1 dB | bad | The dry path is raw input (processor.cpp:502), while the wet path carries the LR4 allpass phase plus the -3 dB pan law, so parallel mixing combs. Dry has no phase compensation. (D-018) |
| morph-sweep:4node-crossfamily-planar | 1 band, 4 nodes planar (SoftClip, SineFold, FullRectify, RingSat) drive 3, 220 Hz -12 dBFS, cursor (0,0)->(1,1) over 2 s, block-rate automation | jump_ratio,clicks_sweep,clicks_static,nonfinite,start_dB,end_dB,max_level_dev_dB,min_dip_dB | 9.90,54,0,0,-6.62,-17.82,-19.51,-19.51 → **Phase 6 (2026-11-09):** jump ratio 1.00, non-finite 0: asserted GREEN in `tests/dsp/morph_continuity_test.cpp` "Morph sweep through the Processor: 4 cross-family nodes on the planar diagonal" (T6.06). Held back RED for a ruling (patch `f:/tmp/disrumpo-fix/p6b/t606-held-bars.patch`): clicks 321 (bar 0) and max level deviation +2.77 dB (bar 1 dB; start -3.40, end -17.83 dB). The level is follow-up 12's >= 3-node blend law (+1.33 / +1.31 dB with the band held at 1x / 2x) plus the 2x -> 4x factor-switch dip at 0.555 s (-1.54 dB, follow-up 6); the clicks need RingSaturation in the mix at 4x (0 with Oversample Max 1x or 2x, with RingSat's Harmonic carrier, or with RingSat replaced; parked at the same positions the mix reads 0 after its first 0.25 s): follow-up 84. | jump_ratio <= 1.3, abs(level dev) <= 3 dB | bad | The largest jump (0.229 at t=1.28 s) happens while the weights move smoothly (0.409/0.220/0.220/0.150), so it is not a weight step. Likely causes: (a) adapters skipped below kWeightThreshold (morph_engine.cpp:745) keep stale state and click on re-entry; (b) four cross-family outputs summed linearly, not with equal power (morph_engine.cpp:756), which explains the 19.5 dB dip; (c) shared L/R state. (D-139, D-008, D-097) |
| morph-sweep:2node-crossfamily(SoftClip-SineFold) | 1 band 2 nodes Linear1D, cursor x 0->1 over 2 s | jump_ratio,clicks_sweep,clicks_static,nonfinite,start_dB,end_dB,max_level_dev_dB | 1.83,1,0,0,-6.62,-14.66,-12.95 → **Phase 6 (2026-11-09):** jump ratio 1.01, clicks 0, non-finite 0: asserted GREEN in "Morph sweep through the Processor: 2 cross-family nodes" (T6.06). Max level deviation -1.77 dB (start -3.40, end -2.02 dB): held back RED (bar 1 dB, follow-up 6): it is the 10 ms output dip (D-034) of the band's 2x -> 4x factor switch at 0.576 s; with the band held at 1x or 2x the sweep reads -0.32 dB. The engine-level sweep (`unit/morph_transition_test.cpp` "Cross-family sweep is level-flat and click-free") asserts the 1 dB bar without the band. | jump_ratio <= 1.3, abs(level dev) <= 3 dB | bad | The level falls 13 dB below the start/end line mid-path: a linear weighted sum of decorrelated outputs (morph_engine.cpp:756). The spec says equal-power crossfade. |
| morph-sweep:2node-samefamily(SoftClip-Tube) | 1 band 2 nodes, cursor x 0->1 over 2 s | jump_ratio,clicks,max_level_dev_dB | 1.00,0,2.16 → **Phase 6 (2026-11-09):** jump ratio 1.00, non-finite 0: asserted GREEN in "Morph sweep through the Processor: 2 same-family nodes" (T6.06). It also found a defect the plan's bar does not cover: the band switched 2x -> 4x -> 2x 12 times during the sweep (two 2x types; the float weight sum rounded the factor up), max level deviation -4.72 dB. Fixed in "fix(disrumpo): a morph between types of one factor keeps that factor": 0 switches, +0.81 dB. | jump_ratio <= 1.3 | ok | The same-family interpolation path (morph_engine.cpp:691-718) is continuous. |
| morph-onnode (exact/near/close node1, exact node0, centre) | 4 nodes planar, cursor parked at (1,0), (0.9995,0.0005), (0.998,0.002), (0,0), (0.5,0.5) | weights, weight_sum, nonfinite, level delta vs exact | on/near node: w=1.0000 for that node, sum 1.00000, nonfinite 0, level delta 0.000 dB; centre: 0.25 x4 → **Phase 6 (2026-11-09):** the same (w = 1 on the node, sum 1.000000, non-finite 0) and level within 0.000 dB of that node alone at (1, 0), (0.9995, 0.0005), (0.998, 0.002) and (0, 0); asserted (sum within 1e-3, level within 0.5 dB) in "Cursor on or next to a node through the Processor plays that node" (T6.06). The follow-up probe's stale-transition-gain cases (D-155: Planar2D -> Linear1D switch on node A, baseline -23.52 dB; fresh Linear1D 4 nodes at x = 0, baseline -3.01 dB) read 0.00 dB / 0.00 dB against SoftClip alone and are asserted (0.5 dB) by `unit/morph_transition_test.cpp` "Cursor parked on a node after a mode switch plays that node at unity" (T1.13): cited, not duplicated. | weights finite, sum 1, near-node within 0.5 dB | ok | The IDW on-node branch (morph_engine.cpp:217-222, band_processor.h:596-600) handles the singularity. The stale-transition-gain case needs a cursor jump or mode switch, which the follow-up probe (D-155) measured separately. |
| morph-smoothing:default | 1 band 2 nodes, default smoothing (10 ms to 99 %), x step 0->1, block 32, 48k, read MorphEngine::getSmoothedX | time_to_99pct_ms | 37.33 → **Phase 6 (2026-11-09):** 99 % between 7 ms (X 0.9698) and 10 ms (X 0.9933), i.e. 9.2 ms, at 1x and through processBlock at 2x: asserted by `unit/morph_band_integration_test.cpp` "BandProcessor morph smoothing reaches 99 % in the configured time" (T1.05 / T1.06, bounds 7 and 10 ms inside the plan's 10 ms +/- 30 %): cited, not duplicated. A fresh Processor now defaults to 0 ms smoothing (follow-up 3), so the probe's "default" read would no longer measure 10 ms. | 10 ms +/- 30 % | bad | The smoother is configured at 8x the rate (band_processor.h:124 -> morph_engine.cpp:50-51) and advanced twice per sample (L+R, band_processor.h:487-488): 10*8/2 = 40 ms expected, 37 measured. Every smoothing-time setting is 4x slow. (D-138, D-099) |
| automation:bandcount2-4-1 (step1, step2) | default nodes, band count 2 -> 4 at 0.49 s -> 1 at 0.99 s, 220 Hz -12 dBFS | jump_ratio,clicks_step,clicks_pre,nonfinite | 3.78,1,0,0 / 3.90,1,0,0 | jump_ratio <= 1.3 | bad | setBandCount changes crossover topology at once (crossover_network.h:111-121, processor_params.cpp:339-346). Bands appear or disappear in the per-sample loop (processor.cpp:483) with no crossfade. (D-140, D-023) |
| automation:type-switch | 1 band SoftClip drive 3 -> SineFold node-type step | jump_ratio,clicks | 1.37,0 | jump_ratio <= 1.3 | suspicious | Type changes swap the adapter type at once (processor_params.cpp:840-855 -> morph_engine.cpp:706). (D-141) |
| automation: ok steps | drive 0->1, global mix 0->1, node mix 0->1, band gain -24->+24 dB, mute band0 on/off, solo band1 on/off, OS max 1x->8x | jump_ratio,clicks,nonfinite | all jump_ratio 0.92-1.00, clicks 0, nonfinite 0 | jump_ratio <= 1.3 | ok | Smoothed gain, pan and mute work. The OS step is click-free only because oversampling is never active on the Processor path. |
| bypass:all-bands | 4 bands SoftClip drive 10, all band bypass on vs off, 220 Hz -12 dBFS | max_abs_diff(on vs off),max_abs_diff(on vs dry) | 0,0.6292 | bypass must change output (FR-012) | bad | Bypass is honoured only in BandProcessor::processBlock (band_processor.h:521). Processor::process calls process() (processor.cpp:494), which ignores bypassed_, so the band bypass parameter has no audible effect. (D-095, D-020) |
| sweep:lfo-rate | sweep on, base 1 kHz, LFO sine 5 Hz depth 1, env off, 2 s, block 512; modulated-freq output param per block | mod_range_oct,upward_crossings_in_2s,blocks | 0.244,0,188 | about 4 oct range and about 10 cycles | bad | sweepLFO_.process() is called once per block (processor.cpp:350), advancing one sample per 512, so the LFO runs 512x slow and is effectively static. (D-035) |
| sweep:env-attack | sweep on base 200 Hz, env on, sens 1, attack ~10 ms, 0.5 s silence then 1 kHz -3 dBFS | final_shift_oct,time_to_63pct_ms | 0.911,320.0 | ~10 ms (block resolution 10.7 ms) | bad | The envelope follower is fed only inputL[0]/inputR[0] once per block (processor.cpp:340-343): decimated by the block size, it is 32x slow and misses transients. (D-036) |
| sweep:off-centre-band-level | 4 bands default, 11 kHz sine -12 dBFS, sweep off vs on (centre 100 Hz, 100 %) | level_off_dB,level_on_dB,delta_dB | -40.60,-165.36,-124.76 | delta > -6 dB | bad | The sweep intensity multiplies the band input before distortion (band_processor.h:481). Bands far from the sweep get intensity about 0 and are muted rather than left dry. Band centres are hard-coded at 100/600/3000/12000 Hz (processor.cpp:422), whatever the crossovers are. Also explains the near-silent Sweep presets. (D-019, D-037, D-038) |
| sweep:band-intensity:intensity=200% | SweepProcessor direct, width 1.5 oct, centre 100->10k Hz | max_band_multiplier | 2.0000 | <= intensity; BandProcessor clamps to [0,1] | suspicious | setIntensity accepts 0-2 (processor_params.cpp:402-408), but BandProcessor::setSweepIntensity clamps to 1 (band_processor.h:197), so 100-200 % is inert. |
| sweep:band-intensity:intensity=100% | SweepProcessor direct | max_band_multiplier,nonfinite | 1.0000,0 | <= 1 | ok | No NaN; bounded. |
| sweep:full-automation | 4 bands SoftClip drive 3, 4-sine input, sweep 100 %, freq 100->10k over 2 s, LFO 5 Hz, env on | nonfinite,max_jump_ratio_vs_off,rms_on_dB,rms_off_dB,mod_range_oct | 0,0.48,-20.98,-11.99,7.29 | nonfinite 0, jump ratio <= 1.3 | ok | Continuous and finite. The 9 dB level loss with the sweep on is the off-centre muting above. |
| samplerate:44.1k/48k/96k | 4 bands Tube/SoftClip/Fuzz/Tape drive 4, 1 kHz -12 dBFS, last 0.5 s, ref 48k | rms_delta_dB,centroid_ratio | 44.1k 0.01/1.006; 96k 0.52/0.915 | abs(rms) <= 1 dB, centroid within 15 % | ok | - |
| samplerate:192k | same chain at 192 kHz | rms_dBFS,centroid_Hz,rms_delta_dB,centroid_ratio,nonfinite | -16.36,1066,2.50,0.824,0 | abs(rms) <= 1 dB, centroid within 15 % | suspicious | No NaN at 192k. The level and brightness drift: band processing is prepared at 1.536 MHz (band_processor.h:111,124), so filter and smoother clamps likely engage. |
| state:survives-setupProcessing | 4-band chain set via params (Tube/SoftClip/Fuzz/Tape, xover0 moved), render, then setActive(false)+setupProcessing+setActive(true), render | max_abs_diff,rms_delta_dB,band0_node0_type_after | 0.1088,-2.24,SoftClip | max diff <= 1e-5 | bad | setupProcessing overwrites every band's morph cache with SoftClip defaults and pushes them (processor.cpp:114-124). crossoverL_.prepare re-runs the log distribution (processor.cpp:101 -> crossover_network.h:79-94). Any sample-rate or block-size change by the host loses the patch. (D-046) |
| preset-hostorder (121 presets) | setState BEFORE setupProcessing+setActive (normal host restore order) vs setState on an active processor, 440 Hz -12 dBFS, 44.1k | maxdiff,rms_delta_dB | 120/121 differ (only Init_1_Band matches); 114 change level by more than 1 dB; worst: Sweep/Sharp_Focus -186.98 dB (silenced), Step_Scanner -34.28, High_Sweep -33.86, Formant_Shift -28.15, Rectified_Drums +43.43, All_Types_Morph +17.03 | maxdiff <= 1e-5 | bad | Same cause as state:survives-setupProcessing (processor.cpp:101,114-124). Presets and projects restored before setupProcessing load wrong. (D-046) |
| preset round-trip loss (35 presets) | load file into fresh proc A' (determinism control) vs getState(A)->fresh setState, 440 Hz -12 dBFS 0.5 s | determinism_maxdiff, roundtrip_maxdiff | determinism 0 for all 121; roundtrip > 1e-5 for 35, e.g. Pitch_Tracker 1.25, Three_Node_Blend 0.522, Sharp_Focus 0.248, Chaos_Morph_4 0.226, Spectral_Scatter 0.19, Smooth_Crossfade 0.176, Chaos_Engine 0.173, Chaotic_Sweep 0.163 | <= 1e-5 | bad | This is real state loss, not randomness. The files are v10 (18338 B) and getState writes v12 (18360 B). A v12 save reloaded and saved again is not byte-stable for 27 of 35, with first drift at offset 92 (sweep LFO rate, written via log/exp, processor_state.cpp:99-104) in 12 presets, and offsets 136/149 in others. Check the v10->v12 migration in processor_state.cpp (routing dest remap 773-787 and the v11 rungler block 728) against what getState writes. (D-042, D-048, D-049; see D-161 for the limits of this measurement.) |
| preset levels: peak above 0 dBFS (11 presets) | 440 Hz -12 dBFS input, 44.1k | peak_dBFS | Spectral_Morph +4.84, Digital_Organic +4.41, Granular_Crush +3.94, Ring_Mod_Lead +3.09, Fractal_Noise +3.02, Chaos_Lead +2.26, Smooth_Crossfade +2.10, Chua_Circuit +1.24, Screaming_Lead +0.59, Formant_Pad +0.54, Stochastic_Burst +0.29 | <= 0 dBFS (> +3 bad) | bad | Unnormalised digital and drive gain (distortion_adapter.cpp:211). Factory presets clip from a -12 dBFS input. (D-144) |
| preset levels: DC (39 presets with abs(dc) > 0.01) | 440 Hz -12 dBFS input | abs(dc) | worst: Fractal_Noise 0.205, Stochastic_Burst 0.156, Chaos_Lead 0.15, Broken_Radio 0.139, Chua_Circuit 0.124, Coupled_Chaos 0.124, Spectral_Scatter 0.114, Spectral_Morph 0.11, Radial_Blend 0.109 | abs(dc) < 1e-3 (< 1e-2 suspicious) | bad | No output-stage DC blocker. The adapter blocks DC for only 4 types (distortion_adapter.cpp:588-591). (D-075, D-142, D-010) |
| preset levels: silent / near-silent | 440 Hz -12 dBFS input | rms_dBFS | Drums/Rectified_Drums -62.22 (bad, < -60); Sweep/Sharp_Focus -53.02; Synced_Pulse -46.67; High_Sweep -48.41; Low_Rumble -43.49 | rms > -60 dBFS | bad | Sweep presets are near-silent because bands away from the sweep centre are muted (band_processor.h:481, processor.cpp:422-435). Rectified_Drums is below the silence bar (root cause D-156: setState never maps shape slots, so HalfRectify compares against the default 0.7 threshold). |
| preset summary | 121 factory presets, 44.1k | verdict counts (preset: lines) | 38 ok, 26 suspicious, 57 bad; nonfinite 0 in all; setState ok for all | - | suspicious | Every preset loads and renders finite audio. The failures are DC, peaks, round-trip loss and near-silence (entries above). |

### Step 6: clang-tidy

**Verdict:** GREEN.
**Log:** `f:/tmp/disrumpo-audit/tidy.log`

> Ran run-clang-tidy.ps1 -Target disrumpo -BuildDir build/windows-ninja -Jobs 4 using the existing compile_commands.json. It analyzed 19 source files in about 2m53s, with no hang. Result line: "Clang-Tidy Analysis Complete  Files analyzed: 19  [OK] Errors: 0  [OK] Warnings: 0". Total warnings: 0.

| Check | Count | Files |
|---|---|---|
| (none) | 0 | 19 files analyzed, 0 warnings, 0 errors |

### Step 7: portability

**Verdict:** GREEN (GCC `-fsyntax-only` proxy only; read the blind spots).
**Log:** `f:/tmp/disrumpo-audit/portability.log`

> Ran tools/check-portability.js with an explicit list of 82 git-tracked .cpp files under plugins/disrumpo/src and plugins/disrumpo/tests. It took 1m54s. The final line was "check-portability: all clear -- 82 compiled." and the exit code was 0. The log contains no warning or error lines. I ran `wsl --shutdown` afterwards. The script is a GCC -fsyntax-only compile proxy run in WSL. It does NOT cover Apple clang or MSVC-specific behaviour, -ffast-math semantics (macOS finiteness folding, fma contraction, pow/sqrt domain edges, infinity() constants), arm64 behaviour, runtime platform gates, linking or ODR, or any runtime behaviour. A pass proves only that the sources parse under GCC. Glob listed only git-tracked files, so untracked .cpp files would be missed.

No items. The macOS fast-math leg is covered by static findings D-113 and D-166 instead.


## Findings

Findings are grouped critical -> high -> medium -> low by **final** severity (the severity after the three-lens verification; where it differs from the severity the finder reported, both are shown). Within a group, CONFIRMED findings come before PLAUSIBLE ones. A finding is CONFIRMED when no verifier refuted it and PLAUSIBLE when exactly one did; the dissenting note is kept in the verifier notes. "Corroborated by measurement" quotes the probe line(s) that back the static finding. Every file reference is relative to the repository root.

### Critical

### D-106 — Same-family morph through a Spectral node re-prepares the STFT (heap allocations + state wipe) every sample

**Severity:** critical | **Verdict:** CONFIRMED | **Category:** rt-safety | **Dimension:** portability-quality | **Also flagged by:** D-001, D-002, D-007 cover the same mechanism from the rt-safety, morph-oversampling and dsp-numerics dimensions | **Corroborated by measurement:** no (no allocation counter was run; see Critic gap on missing RT measurements)

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.cpp:893-896](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L893-L896)

**Problem.** MorphEngine::processSameFamily calls blendedAdapter_.setParams(interpolateParams()) on every sample (morph_engine.cpp:706-710). interpolateParams() builds fftSize by summing truncated integer terms: result.fftSize starts at 0 (morph_engine.cpp:497) and adds static_cast<int>(w * p.fftSize) per node (:606). For two Spectral nodes at 2048 with weights 0.3/0.7 that gives 614 + 1433 = 2047. routeParamsToProcessor then compares p.fftSize (2047) with spectral_.getFftSize(), which is always a power of two because SpectralDistortion::prepare rounds up (spectral_distortion.h:150-154). The values never match, so spectral_.prepare() runs on every sample.

**Why wrong.** SpectralDistortion::prepare is not real-time safe. It calls stft_.prepare and overlapAdd_.prepare, which run fft_.prepare: pffft_new_setup plus three makeAlignedBuffer allocations (fft.h:157-166). It also assigns Window::generate(), which returns a new std::vector (stft.h:72, :244), and it resets all STFT state. The comment 'allocates, but only on user param change' is false, because this runs from the per-sample morph path. CLAUDE.md forbids allocation on the audio thread. An STFT that is reset every sample never completes a frame.

**Consequence.** Two or more Experimental-family nodes (for example Spectral+Spectral, or Spectral dominant over Chaos/Granular) with the cursor off a node cause roughly 10 heap allocations per sample per channel. The same engine is called for L and R. Expect CPU overload and dropouts. The Spectral wet path stays silent or garbage because the STFT is wiped before it fills a frame. Clean only when the cursor sits exactly on a node (weights one-hot).

**Evidence.** morph_engine.cpp:497 `result.fftSize = 0;` :606 `result.fftSize += static_cast<int>(w * static_cast<float>(p.fftSize));` :706-710 `blendedAdapter_.setType(...); auto bp = interpolateParams(); ... blendedAdapter_.setParams(bp);` (in process(), per sample). distortion_adapter.cpp:893-896 `// Slot 1: FFT size — re-prepare if changed (allocates, but only on user param change) if (static_cast<std::size_t>(p.fftSize) != spectral_.getFftSize()) { spectral_.prepare(sampleRate_, ...p.fftSize); }`. spectral_distortion.h:150-154 rounds to pow2; fft.h:157 `setup_.reset(pffft_new_setup(...))`; stft.h:72 `window_ = Window::generate(...)`.

**Suggested fix.** Do not interpolate fftSize. Take discrete/structural fields such as fftSize, magnitudeBits and stages from the dominant node, the same way the other discrete fields are handled. Then move the FFT-size change out of setParams: pre-prepare at kMaxFFTSize in prepare() and switch the active size without allocating, or defer the change to a non-audio thread. Test: a hidden [.]-tagged probe builds MorphEngine with two Spectral nodes (fftSize 2048), cursor X=0.3, runs 8192 samples under the allocation-counting operator new hook (tools/lint-allocation-operator-overrides pattern), and asserts zero allocations and non-silent output after 2*fftSize samples.

**Verifier notes:**
- Correctness: every step holds; isSameFamily() (morph_engine.cpp:385-397) compares only families and distortion_types.h:337-344 puts Chaos/Formant/Granular/Spectral/Fractal/Stochastic all in Experimental, so any off-node cursor with a Spectral dominant re-prepares per sample via distortion_adapter.cpp:173-175 -> :888-896; stft.h:72/:78/:81/:244/:266/:269 and fft.h:157/:164-166 allocate; the fftSize default is 2048 (distortion_adapter.h:179) and the slot map gives 512*2^k (processor_params.cpp:244).
- Context: nothing upstream guards the path (band_processor.h:752-754 calls the engine per sample for L and R); the "Parallel blend" comment at distortion_types.h:337 has no code behind it; no test covers a Spectral node under morph (only distortion_adapter_test.cpp:2069-2074 touches fftSize); the same truncated-int pattern hits stages, magnitudeBits and iterations (:576, :607, :614).
- Impact: at 48 kHz this is about 2 million heap operations per second plus pffft twiddle recomputation on the audio thread, and stft.h:84-85 resets writeIndex_/samplesAvailable_ so no frame ever fills (silent Spectral wet); cross-family morphing passes each node's own power-of-two size (morph_engine.cpp:734-736) and does not trigger it; critical stands.

### D-047 — setActive(true) and setupProcessing wipe every modulation routing and macro (ModulationEngine::reset clears routings_/macros_)

**Severity:** critical | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** lifecycle-state | **Also flagged by:** gestalt-processor | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor.cpp:164](plugins/disrumpo/src/processor/processor.cpp#L164)

**Problem.** Processor::setActive(true) calls modulationEngine_.reset(). setupProcessing calls modulationEngine_.prepare(), which also ends in reset() (modulation_engine.h:123). The shared ModulationEngine::reset() does more than reset sources: it sets every routing to ModRouting{} and every macro to MacroConfig{} (modulation_engine.h:152-160). The routings and macros held by the processor live only inside the engine. Nothing re-applies them after the reset.

**Why wrong.** setActive(true) and prepare are signal-state resets, not parameter resets. VST3 hosts activate after setState, and they re-activate on transport changes, sample-rate or offline-render changes, and plugin off/on. Routing amount, source and destination are user parameters. They must survive activation, the same way gains survive it (setupProcessing re-pushes bandStates_).

**Consequence.** In both host orders (setState before setupProcessing, or setState after setupProcessing but before setActive), all 32 mod routings and 4 macros are silently dropped. The mod matrix UI still shows them, but nothing modulates. The next host save writes empty routings, which makes the loss permanent. The same happens on a running instance whenever the host toggles activation. This is a SHARED-DSP-CONTRACT issue: ruinae_engine.h:197 also calls globalModEngine_.reset(), so check whether Ruinae re-applies its routings every block.

**Evidence.** processor.cpp:163-164 `// Reset modulation engine\n modulationEngine_.reset();`; processor.cpp:139 `modulationEngine_.prepare(sampleRate_, setup.maxSamplesPerBlock);`; modulation_engine.h:123 `reset();` (end of prepare); modulation_engine.h:152-160 `// Reset routings for (auto& r : routings_) { r = ModRouting{}; } // Reset macros for (auto& m : macros_) { m = MacroConfig{}; }`.

**Suggested fix.** Split ModulationEngine::reset() into resetSources() (LFO phase, envelopes, smoothers, offsets) and a separate clearRoutings(). Use only the source reset from setActive and prepare. Alternatively, Disrumpo can keep a routing/macro cache and re-apply it after the reset. Test: set routings through processParameterChanges or setState -> setActive(false) -> setActive(true) -> getState must still contain the routings. Also assert that an LFO->InputGain routing still modulates the output after re-activation. Run the consumers of modulation_engine.h (node tools/header-consumers.js).

**Verifier notes:**
- Correctness: processor.h:247 holds a plain ModulationEngine with no cache; the only writers are processor_params.cpp:551-572/765-817 and processor_state.cpp:751-798, none of which runs in setActive, setupProcessing or process(); getState serializes straight from the engine (processor_state.cpp:288-308), so the next save makes the loss permanent; serialization_round_trip_test.cpp:59 never calls setActive after setState.
- Context: six test files call setActive and none checks that routings survive it; the engine's own doc says "Reset all sources and routing state", so clearing routings is intended engine behaviour, and Disrumpo calling it as a signal-state reset on activation is the bug.
- Impact: the usual VST3 load order (initialize -> setupProcessing -> setState -> setActive(true)) wipes the restored routings on every project reload; Ruinae re-pushes every route from parameters each block (ruinae processor_params.cpp:494 -> ruinae_engine.h:466), so the visible defect is Disrumpo-specific, but the shared reset() contract is the trap a fix should address.

### D-046 — setupProcessing throws away the state that setState restored: morph nodes, node count, crossovers, morph mode and smoothing

**Severity:** critical | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** lifecycle-state | **Corroborated by measurement:** yes (probes `state:survives-setupProcessing` and `preset-hostorder`)

**Location:** [plugins/disrumpo/src/processor/processor.cpp:100-128](plugins/disrumpo/src/processor/processor.cpp#L100-L128)

**Problem.** Every setupProcessing call does three destructive things. (1) It rebuilds bandMorphCache_[i].nodes[0..3] as four default SoftClip nodes and sets activeNodeCount = kDefaultActiveNodes (1) (lines 112-126). (2) It calls crossoverL_/R_.prepare(), which runs initializeLogarithmicDistribution() and overwrites the stored crossover frequencies (crossover_network.h:79-94, 253-269). (3) It calls BandProcessor::prepare(), which does morphEngine_ = std::make_unique<MorphEngine>() (band_processor.h:123), so the engine falls back to mode_ Linear1D and smoothing 10 ms (morph_engine.h:220-221). There is a second path to the same loss. When setState runs before the first setupProcessing, morphEngine_ is still null, so setMorphNodes, setMorphMode, setMorphSmoothingTime and setMorphPosition are silent no-ops (band_processor.h:247-287, all guarded by `if (morphEngine_)`).

**Why wrong.** VST3 hosts normally restore a project in the order initialize -> setState -> setupProcessing -> setActive(true). They also call setActive(false) -> setupProcessing -> setActive(true) whenever the sample rate or block size changes. setupProcessing should only prepare DSP for the new rate; it must not reset user parameters. The processor already keeps an authoritative cache (bandMorphCache_, crossover Hz). It should re-push that cache after prepare instead of overwriting it.

**Consequence.** When a project reopens, every band's node types, drive, mix, tone, bias, shape slots and active-node count reset to 1x SoftClip. Crossovers jump to 112/632/3556 Hz, and morph mode and smoothing reset. The controller still shows the restored values (setComponentState), so the UI and the audio disagree. The next host save writes the defaults, which makes the data loss permanent. Changing the sample rate in the middle of a session wipes the same state from a running instance. Not covered by any test: every test calls setupProcessing before setState (serialization_round_trip_test.cpp:59, factory_preset_validation_test.cpp:109).

**Evidence.** processor.cpp:114-122 `cache.nodes[0] = MorphNode(0, 0.0f, 0.0f, DistortionType::SoftClip); ... cache.activeNodeCount = kDefaultActiveNodes;`; processor.cpp:101 `crossoverL_.prepare(sampleRate_, numBands);` -> crossover_network.h:90 `initializeLogarithmicDistribution();`; band_processor.h:123 `morphEngine_ = std::make_unique<MorphEngine>();`; band_processor.h:260-263 `void setMorphMode(MorphMode mode) noexcept { if (morphEngine_) { morphEngine_->setMode(mode); } }`; morph_node.h:135 `kDefaultActiveNodes = 1`.

**Corroborated by measurement.** probe state:survives-setupProcessing: "max_abs_diff 0.1088, rms_delta_dB -2.24, band0_node0_type_after SoftClip". probe preset-hostorder: "120/121 differ (only Init_1_Band matches); 114 change level by more than 1 dB; worst: Sweep/Sharp_Focus -186.98 dB (silenced), Step_Scanner -34.28, High_Sweep -33.86, Formant_Shift -28.15, Rectified_Drums +43.43, All_Types_Morph +17.03". Code: processor.cpp:101 `crossoverL_.prepare(sampleRate_, numBands);`, :113 `cache.nodes[0] = MorphNode(0, 0.0f, 0.0f, DistortionType::SoftClip);`, :126 `bandProcessors_[i].setMorphNodes(cache.nodes, cache.activeNodeCount);`

**Suggested fix.** In setupProcessing, stop re-initialising bandMorphCache_. After prepare, push the existing cache to the band processors: setMorphNodes, setMorphPosition, and a cached morphMode and smoothing (store these in BandMorphCache). Save the crossover Hz before crossover prepare and restore it afterwards, or give CrossoverNetwork::prepare a keep-frequencies variant. Make BandProcessor::prepare keep or re-apply mode and smoothing. Test: processor setState(stream) -> setupProcessing(44.1k) -> setActive(true) -> getState must equal the input stream byte for byte. Repeat with setupProcessing(48k) on an active instance.

**Verifier notes:**
- Correctness: processor.cpp:112-122 unconditionally overwrites the cache that setState fills at processor_state.cpp:824-897 (only morphX/Y survive, :127); crossover prepare always calls initializeLogarithmicDistribution (crossover_network.h:253-269) and getState reads the overwritten Hz back (processor_state.cpp:76); band_processor.h:123 recreates the engine so mode/smoothing set at processor_state.cpp:827-831 are lost, and processor.h:179 crossoverFrequencies_ is never read.
- Context: no first-time/prepared_ flag guards the init; the comment at processor.cpp:111 ("Initialize morph cache with defaults") shows a first-time intent that the code does not limit; every harness calls setupProcessing before setState (serialization_round_trip_test.cpp:59, factory_preset_validation_test.cpp:109); setActive (processor.cpp:144-178) and process() never re-push the cache.
- Impact: in the SDK-documented order every band falls back to one SoftClip node at drive 1 on project reload; in every host a sample-rate, block-size or offline-render setupProcessing wipes the running instance; the loss persists through getState (processor_state.cpp:314, 344, 76); critical.

### D-016 — Left and right channels share one MorphEngine (and all its stateful adapters) per band

**Severity:** critical | **Verdict:** CONFIRMED | **Category:** signal-path | **Dimension:** signal-path | **Also flagged by:** morph-oversampling, shared-dsp-contracts | **Corroborated by measurement:** yes (probe `stereo-leak`)

**Location:** [plugins/disrumpo/src/dsp/band_processor.h:485-488](plugins/disrumpo/src/dsp/band_processor.h#L485-L488)

**Problem.** BandProcessor::process calls `left = morphEngine_->process(left); right = morphEngine_->process(right);` on the single per-band MorphEngine. Every stateful element inside it (morph X/Y OnePoleSmoothers, the per-node and blended DistortionAdapters with their OnePoleLP tone filter, 10 Hz DCBlocker, tape HF/body/wavefold-smooth filters, FeedbackDistortion delay line, SpectralDistortion STFT, GranularDistortion buffer, Temporal envelope follower, chaos/stochastic state, RNGs) is clocked with an interleaved L,R,L,R stream. The same pattern exists in the legacy distortion_ path (:492-494) and in processOversampledBlock (:753-757).

**Why wrong.** All Krate::DSP processors used here are mono, one instance per channel (adapter contract: 'stereo requires one adapter per channel'). A one-pole filter fed interleaved channels computes y_R[n] = (1-a)*x_R[n] + a*y_L[n], i.e. the R output is mostly the filtered L history; filters run at twice their intended clock; delay lines and STFT frames mix both channels (an interleaved stereo stream spectrally equals the mid signal plus the side signal mirrored around Nyquist). MorphEngine::process (morph_engine.cpp:668-669) also advances smootherX_/smootherY_ and recomputes weights on every call, so the cursor smoother advances twice per sample and R sees different weights than L.

**Consequence.** Wet-path stereo image collapses and smears: with the default tone filter coefficient a = exp(-2*pi*4000/352800) = 0.931, about 93 % of each R output sample is carried over from the previous L state, and the reverse holds for L. Hard-panned or wide material comes out near-mono with inter-channel bleed. Spectral/Granular/Feedback/Allpass types process a garbled interleaved signal, which produces Nyquist-mirrored artifacts and wrong delay times. A signal on L only leaks into R.

**Evidence.** band_processor.h:487 `left = morphEngine_->process(left);` :488 `right = morphEngine_->process(right);`; band_processor.h:808 single `std::unique_ptr<MorphEngine> morphEngine_;`; morph_engine.cpp:668-672 `smootherX_.process(); smootherY_.process(); calculateMorphWeights(...)` per call; distortion_adapter.cpp:583 `return toneFilter_.process(wet);` (single OnePoleLP member, distortion_adapter.h:358); one_pole.h:114 `state_ = (1.0f - coefficient_) * input + coefficient_ * state_;`

**Corroborated by measurement.** probe stereo-leak, 1 band, drive 0.5, L = 1031 Hz -6 dBFS, R = silence. R/L rms dBFS: "SoftClip -13.5/-12.9; Tape -13.7/-13.7; Tube -15.2/-14.6; FeedbackDist -24.0/-24.0; AllpassResonant -17.7/-17.7; Temporal -18.0/-17.4; Formant -7.1/-7.1" (threshold R <= -120 dBFS). Code: band_processor.h:487 `left = morphEngine_->process(left);` and :488 `right = morphEngine_->process(right);`

**Suggested fix.** Hold two MorphEngines per band (morphEngineL_/morphEngineR_, both heap-allocated in prepare), or make MorphEngine stereo with per-channel adapter instances and one shared cursor smoother advanced once per sample. Do the same for distortion_. Regression test (processor level, disrumpo_tests): 1 band, SoftClip drive 1, mix 1, L = 1 kHz sine, R = silence. Assert R output RMS < -90 dBFS; it fails today. Add a second case: same input on both channels with R polarity-inverted; assert the output channels are polarity-inverted copies within 1e-5.

**Verifier notes:**
- Correctness: processor.cpp:473-474 splits L/R through separate crossovers ("FR-001b: independent L/R") and then :487/:494 call one bandProcessors_[b].process(bandL, bandR); the adapter has exactly one toneFilter_ (distortion_adapter.h:358) and dcBlocker_ (:361) applied at distortion_adapter.cpp:219/:583; the 0.931 figure assumes 8x prepare, but bleed exists on every path.
- Context: processor.cpp:125 enables morph on every band so this is the live path; the per-channel crossover shows the intent is independent channels; specs/Disrumpo has no note accepting a shared mono processor; no test pins channel independence (greps for bleed/crosstalk/"left only" return nothing).
- Impact: with R silent each R output sample is about 0.931*y_L(prev), so crosstalk is roughly -0.6 dB rather than -90 dB, the wet path is effectively mono in every host at default settings, and Feedback/Spectral/Granular/Temporal garble the interleaved stream; critical.

### D-007 — Same-family morph truncates fftSize to a non-power-of-2, so SpectralDistortion is re-prepared (pffft setup + vector alloc + state wipe) on every sample and its output is silent

**Severity:** critical | **Verdict:** CONFIRMED | **Category:** dsp-numerics | **Dimension:** dsp-numerics | **Also flagged by:** same mechanism as D-001, D-002, D-106 | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/morph_engine.cpp:606](plugins/disrumpo/src/dsp/morph_engine.cpp#L606) (with distortion_adapter.cpp:894-896, morph_engine.cpp:706-710)

**Problem.** interpolateParams starts result.fftSize at 0 and accumulates `static_cast<int>(w * fftSize)` per node, so every node's share is floored. For two nodes at fftSize 2048, w=0.6/0.4 gives 1228+819 = 2047, and so do w=0.7 and 0.9 (computed with float32 rounding). In processSameFamily the blended adapter is set to the dominant type and setParams(bp) runs on EVERY sample. When Spectral is dominant, routeParamsToProcessor compares 2047 with spectral_.getFftSize(). That returns 2048, because SpectralDistortion::prepare rounds up to a power of 2 (spectral_distortion.h:150-154). The two never match, so spectral_.prepare() runs on every call.

**Why wrong.** A weighted mean of identical power-of-2 sizes must stay that size, and a discrete FFT size should come from the dominant node (as the other discrete fields do), not from a floored sum. prepare() is documented 'NOT real-time safe (allocates memory)': STFT::prepare calls Window::generate (returns a new std::vector, stft.h:72) and fft_.prepare calls pffft_new_setup (fft.h:157). It also resets samplesAvailable_ = 0 (stft.h:85), so canAnalyze() never becomes true and process() returns 0.0f.

**Consequence.** Any same-family morph in which a Spectral node is dominant (Spectral with Chaos, Formant, Granular, Fractal or Stochastic, or two Spectral nodes, all Experimental family), with the cursor anywhere except a few exact weights, does a heap allocation plus a pffft setup on every sample, twice per sample (L and R share the engine), for every band. The result is CPU spikes, dropouts, a hard RT violation, and a wet Spectral path that outputs silence instead of the effect.

**Evidence.** morph_engine.cpp:498 `result.fftSize = 0;` and :606 `result.fftSize += static_cast<int>(w * static_cast<float>(p.fftSize));`. morph_engine.cpp:706-710 setType(dominant) + interpolateParams + setParams per sample. distortion_adapter.cpp:894-896 `if (static_cast<std::size_t>(p.fftSize) != spectral_.getFftSize()) { spectral_.prepare(sampleRate_, static_cast<std::size_t>(p.fftSize)); }`. spectral_distortion.h:150-154 rounds up to pow2. stft.h:72 `window_ = Window::generate(...)` and :85 `samplesAvailable_ = 0`. fft.h:157 `setup_.reset(pffft_new_setup(...))`. Computed with fround: w=0.6 -> 2047, 0.7 -> 2047, 0.9 -> 2047, 0.5 -> 2048, 0.75 -> 2048.

**Suggested fix.** Take fftSize (and magnitudeBits) from the dominant node, like spectralMode and the other discrete fields. If it must be interpolated, round to the nearest power of 2. Also compare the rounded value in the adapter, or cache the requested size so that an equal pow2 does not re-prepare, and never call prepare() from setParams on the audio thread. Test: a MorphEngine with 2 Spectral nodes at fftSize 2048, cursor at w=0.6, process 8192 samples; assert non-zero output RMS and that getFftSize() is stable. An allocation-counting hook around process() would catch the RT side.

**Verifier notes:**
- Correctness: normalized IDW weights (morph_engine.cpp:196-208, :228-240) are almost never dyadic and are exactly 1.0 only on a node (:188-191, :220-223); prepare recomputes about 1025 binFrequencies per call (spectral_distortion.h:172-182) and resets samplesAvailable_ (stft.h:85) so canAnalyze() (spectral_distortion.h:230) is never true.
- Context: the adapter comment at distortion_adapter.cpp:893 ("only on user param change") assumes the size comes straight from processor_params.cpp:244, an assumption the morph path breaks; no test has a Spectral node inside a MorphEngine (multi_node_distortion_test.cpp:564 only lists the type name).
- Impact: a heap allocation plus an O(N) window/pffft setup per sample per band, twice per sample, in a normal user configuration, and SpectralDistortion::process returns 0.0f (spectral_distortion.h:243) so the effect is lost; critical.

### D-002 — Selecting Spectral, moving its FFT slot, or the first Spectral sample allocates on the audio thread

**Severity:** critical (reported: high; raised by all three verifiers) | **Verdict:** CONFIRMED | **Category:** rt-safety | **Dimension:** rt-safety | **Also flagged by:** shared-dsp-contracts | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.cpp:84](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L84) and [:893-896](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L893-L896)

**Problem.** DistortionAdapter::prepare prepares spectral_ with the BLOCK size as its FFT size: `spectral_.prepare(sampleRate, maxBlockSize)` (:84). BandProcessor passes maxBlockSize_*8 = 4096 (band_processor.h:111-112, 124-125). DistortionParams::fftSize defaults to 2048 (distortion_adapter.h:179), and the slot map yields 512..4096 (processor_params.cpp:244). These paths therefore reach spectral_.prepare (pffft setup, aligned mallocs, Window::generate vectors): (a) a kNodeType change to Spectral, which runs processParameterChanges -> setMorphNodes (processor_params.cpp:899) -> MorphEngine::setNodes -> adapters_[i].setParams (morph_engine.cpp:136-139) inside Processor::process; (b) any edit or automation of the Spectral FFT slot; (c) the first processSameFamily sample on blendedAdapter_ once Spectral is dominant, even with a single active node (morph_engine.cpp:706-710). The code comment admits 'allocates, but only on user param change'. User param changes arrive on the audio thread.

**Why wrong.** The project rule (CLAUDE.md 'Real-Time Audio Thread Safety', constitution Principle II, and the processor.cpp:183 banner) forbids allocation in process(). processParameterChanges runs at processor.cpp:191-192 inside process(). spectral_distortion.h:144 marks prepare as not RT-safe. prepare also calls the non-noexcept Window::generate from a noexcept function, so a failed allocation terminates the host.

**Consequence.** Choosing the Spectral type, loading a preset that contains it, or automating its FFT size causes malloc/free plus pffft setup on the audio thread. That means dropouts in low-latency sessions and priority inversion on the allocator lock. Each first selection allocates twice: once for the node adapter and once for the blended adapter. Across 4 bands x 5 adapters this repeats on every preset change.

**Evidence.** distortion_adapter.cpp:84 `spectral_.prepare(sampleRate, static_cast<size_t>(maxBlockSize));`. distortion_adapter.h:179 `int fftSize = 2048;`. band_processor.h:124-125 `morphEngine_->prepare(sampleRate * kMaxOversampleFactor, static_cast<int>(maxBlockSize_ * kMaxOversampleFactor));` with maxBlockSize_ = 512. processor_params.cpp:899 `bandProcessors_[band].setMorphNodes(cache.nodes, cache.activeNodeCount);`. morph_engine.cpp:136-139 setType/setParams for all 4 adapters. processor.cpp:191-192 processParameterChanges is called inside process().

**Suggested fix.** Prepare every SpectralDistortion at kMaxFFTSize (4096 is the plugin maximum) in DistortionAdapter::prepare and give it an RT-safe size switch over preallocated buffers, or pre-create one instance per supported size (512/1024/2048/4096) and select by index. Remove the prepare call from routeParamsToProcessor. Test: an allocation-counting test (operator new hook) that sends a kNodeType=Spectral change and then FFT-slot changes through Processor::process with a ParameterChanges mock, and asserts zero allocations.

**Verifier notes:**
- Correctness: claims (a) and (b) hold through processor_params.cpp:899 -> morph_engine.cpp:136-139 -> distortion_adapter.cpp:173-175 -> :893-896; claim (c) is worse than stated because interpolateParams (morph_engine.cpp:606) truncates fftSize so the blended adapter re-prepares on every sample while morphing (upgrade).
- Context: the prepare size is maxBlockSize*8 (band_processor.h:47) which gives 4096 at a 512 block and 8192 clamped (spectral_distortion.h:148) above, so the first Spectral setParams always mismatches the 2048 default; processCrossFamily (:736) also re-routes whenever a mod offset is non-zero; no RT-safe size switch, preallocation or deferral exists.
- Impact: Spectral shares DistortionFamily::Experimental (distortion_types.h:337-341) so the per-sample re-prepare happens beside any other Experimental node, giving hundreds of thousands of malloc/free calls per second and broken Spectral output; this is sustained, not first-sample, allocation (upgrade to critical).

### D-001 — Spectral node in a same-family morph re-prepares the FFT (malloc/free) twice per sample and keeps the wet path silent

**Severity:** critical | **Verdict:** CONFIRMED | **Category:** rt-safety | **Dimension:** rt-safety | **Also flagged by:** morph-oversampling, shared-dsp-contracts | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/morph_engine.cpp:497](plugins/disrumpo/src/dsp/morph_engine.cpp#L497), [:606](plugins/disrumpo/src/dsp/morph_engine.cpp#L606), [:706-718](plugins/disrumpo/src/dsp/morph_engine.cpp#L706-L718) (with distortion_adapter.cpp:893-896)

**Problem.** processSameFamily rebuilds the blended params every sample and calls blendedAdapter_.setParams(bp). interpolateParams starts fftSize at 0 (:497) and adds the truncated per-node terms `result.fftSize += static_cast<int>(w * p.fftSize)` (:606). With Spectral as the dominant node and any second Experimental-family node (Chaos, Formant, Granular, Fractal, Stochastic, or a second Spectral), the weights are rarely dyadic, so the sum lands one below the power of two. Example: 2 nodes at (0,0) and (1,0), cursor at (0.3,0), w = 0.845 / 0.155, gives 1730 + 317 = 2047. routeParamsToProcessor then sees `p.fftSize != spectral_.getFftSize()` (2047 != 2048) and calls spectral_.prepare(sampleRate_, 2047) (adapter.cpp:894-896). prepare rounds that back to 2048 (spectral_distortion.h:150-154), so the mismatch comes back on the next call. Because BandProcessor::process feeds L and R through the same MorphEngine (band_processor.h:487-488), this happens twice per sample per band.

**Why wrong.** spectral_distortion.h:144 says prepare is 'NOT real-time safe (allocates memory)'. It calls stft_.prepare, which runs fft_.prepare: pffft_new_setup plus three pffft aligned mallocs (fft.h:157-166). It then calls Window::generate, which returns a new std::vector (stft.h:72, window_functions.h:282), and does the same for overlapAdd_. prepare also clears writeIndex_/samplesAvailable_ (stft.h:84-85), so the STFT never collects fftSize samples and SpectralDistortion::process returns 0.0f every call (spectral_distortion.h:243). Window::generate is not noexcept but is called from a noexcept prepare, so a bad_alloc on the audio thread ends in std::terminate. Integer fields must not be re-derived per sample in a way that differs from the processor's own canonicalised value.

**Consequence.** Normal use: morphing Spectral with any other Experimental type. Each band does about 88,200 malloc/free cycles per second at 44.1 kHz (more at higher rates), with pffft setup creation every time. The result is CPU spikes, xruns, and allocator contention with other plugins. The Spectral wet path outputs silence for the whole morph, so only the dry share (1-mix) and the other node's contribution are heard. Under memory pressure the host process aborts.

**Evidence.** morph_engine.cpp:497 `result.fftSize = 0;` :606 `result.fftSize += static_cast<int>(w * static_cast<float>(p.fftSize));` :706-710 `blendedAdapter_.setType(nodes_[dominantNode].type); auto bp = interpolateParams(); ... blendedAdapter_.setParams(bp);`. distortion_adapter.cpp:893-896 `// Slot 1: FFT size — re-prepare if changed (allocates, but only on user param change) if (static_cast<std::size_t>(p.fftSize) != spectral_.getFftSize()) { spectral_.prepare(sampleRate_, ...p.fftSize); }`. spectral_distortion.h:150-157 round to pow2 then `fftSize_ = fftSize;`, :162 `stft_.prepare(...)`, :172-185 resize. fft.h:157 `setup_.reset(pffft_new_setup(...))`, :164-166 makeAlignedBuffer x3. stft.h:72 `window_ = Window::generate(window, fftSize, kaiserBeta);`, :84-85 index reset. band_processor.h:487-488 two process() calls on the same engine.

**Suggested fix.** Treat fftSize (and the other int fields: magnitudeBits, iterations, stages) as discrete and take it from the dominant node, like the other discrete fields in interpolateParams. Also, never call spectral_.prepare from routeParamsToProcessor: prepare SpectralDistortion once at kMaxFFTSize in DistortionAdapter::prepare and add an RT-safe setFftSize(n <= prepared max) that only changes the active size and window index into preallocated buffers, or swap between preallocated instances. Test: a hidden [audit-probe] or unit case that builds a MorphEngine with nodes {Spectral, Chaos}, puts the cursor at (0.3,0) and processes 8192 samples. Assert that the output RMS is above 0 after fftSize samples. Count allocations with a global operator new hook (pattern used in other plugin RT tests) and assert zero during process().

**Verifier notes:**
- Correctness: the other node keeps the default fftSize 2048 (distortion_adapter.h:179) because processor_params.cpp:244 writes fftSize only for Spectral; the snap to weight 1.0 happens only within kOnNodeThreshold 0.001 (morph_engine.cpp:23, :220-223), so almost every off-node position truncates; getFftSize returns the stored pow2 (spectral_distortion.h:497-499) so the mismatch repeats.
- Context: distortion_types.h:337 says "Parallel blend with output crossfade" but isSameFamily (morph_engine.cpp:385-397) has no Experimental exception; morphEnabled_ is forced true for every band (processor.cpp:125); no test uses Spectral inside a MorphEngine.
- Impact: per band about 2 x fs pffft setups, four or more aligned mallocs/frees, two window vectors and an O(N) bin-frequency loop (spectral_distortion.h:179-183) per sample, inside a noexcept function where bad_alloc terminates; the Spectral contribution is silent for the whole morph; critical.


### High

### D-156 — Rectified_Drums root cause: after a host state restore its HalfRectify band compares the signal against the default threshold 0.7, so the 440 Hz band outputs exactly 0

**Severity:** high | **Verdict:** CONFIRMED | **Category:** measured | **Dimension:** followup:rectify-fold-level-rootcause | **Also flagged by:** followup:rectify-fold-level-rootcause | **Corroborated by measurement:** yes (preset probe row for Rectified_Drums, -50.76 dBFS peak / -62.22 dBFS RMS; host-order row rms_delta +43.43 dB)

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.cpp:441-442](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L441-L442)

**Problem.** The preset sets band 1 (150-4500 Hz, which carries the 440 Hz test tone) to HalfRectify with slots {threshold 0.3, smooth 0.15, dcBlock 1} and drive 2.0 (tools/disrumpo_preset_generator.cpp:2630-2631, shapeHalfRect at :621-624). The host setState path (processor_state.cpp:852-889) restores the type, drive, mix, tone, bias, folds and bitDepth plus the raw shapeSlots, but never runs mapShapeSlotsToParams. typeParams_.threshold therefore keeps the DistortionParams default of 0.7 (distortion_adapter.h:85) and smoothness keeps 0.0. HalfRectify then computes `(input >= thresh) ? input : 0.0f` on the driven sample. The driven peak is 0.251 x 2.0 = 0.502, below 0.7, so every sample of band 1 is 0. What is left in the -50.8 dBFS render is band 0's LR4 skirt at 440 Hz: (440/150)^4 = 74, about -37.5 dB, then FullRectify and its DC transient. The 8x-rated smooth filter and the 1.25 Hz DC blocker (the brief's other suspects) are not the cause: smooth is 0 on this path and is skipped (cpp:443), and the DC blocker only removes DC.

**Why wrong.** The preset is meant to render the half-rectified mid band at about -12 dBFS. The parameter path (processor_params.cpp:852, 892) and the preset-browser path both re-derive threshold = slots[0] = 0.3 (processor_params.cpp:122-126). The host restore path is the one a DAW uses on project reload, and on it the stored threshold is ignored and a default above the signal level wins.

**Consequence.** Reloading a project that uses Rectified_Drums, or any preset or session with a HalfRectify node whose driven peak is under 0.7, silences that band. The preset drops from about -12 dBFS to -50.8 dBFS peak / -62.2 dBFS RMS (probe-results.txt:301). The host-order probe (probe-results.txt:145) shows a 43 dB difference against the SoftClip nodes that setupProcessing reinstates. This is the audible worst case of the already-reported 'setState never re-derives type-specific DistortionParams' defect (D-050); for HalfRectify it is a hard mute, not a timbre change.

**Evidence.** distortion_adapter.cpp:441-442 `const float thresh = typeParams_.threshold; float result = (input >= thresh) ? input : 0.0f;`. distortion_adapter.h:85 `float threshold = 0.7f;`. processor_state.cpp:859-861 restore only bias/folds/bitDepth, and :871 writes shapeSlots with no mapShapeSlotsToParams. Probe path: audit_probes.cpp.txt:1502-1504 (fresh Rig, setStateBytes, render), so the probe measured the host path. Node.js replica (scratchpad rect_sim.js; LR4 150/4500 Hz, FullRectify band 0 at drive 1.5, HalfRectify band 1 at drive 2, filters designed at 8x and double-clocked, -3 dB centre pan): threshold 0.7 / smooth 0 gives peak -62.86 dBFS, RMS -67.49. The preset's intended threshold 0.3 / smooth 0.15 gives peak -12.04 dBFS, RMS -18.61. The remaining ~12 dB gap to the measured -50.76 matches the preset's Transient->bandDrive(0) route at amount 0.4 (generator :2634; drive offset x10 takes 1.5 to 5.5, +11.3 dB), which the replica leaves out.

**Suggested fix.** In Processor::setState, after the slot loop for each node (processor_state.cpp:874), call the same mapShapeSlotsToParams(mn.type, mn.shapeSlots, mn.params) that processParameterChanges uses. Expose it from processor_params.cpp, for example as a private static member, instead of the file-static. Re-apply the stream's bias, folds and bitDepth only where they are not slot-derived. Test: load Drums/Rectified_Drums.vstpreset through Processor::setState on an active processor, render 440 Hz at -12 dBFS, and assert RMS > -30 dBFS (it fails today at -62.2). Also assert that the band-1 node's getNodeParams(0).threshold equals 0.3 within 1e-6.

**Verifier notes:**
- Correctness: mapShapeSlotsToParams exists only at processor_params.cpp:39 with callers :852 and :892 inside processParameterChanges; nothing in dsp/ reads shapeSlots; MorphEngine blends p.threshold (morph_engine.cpp:429, :538) from nodes still holding the 0.7 default; drive is applied before the threshold compare (distortion_adapter.cpp:211).
- Context: the generator sets every band-1 node to HalfRectify at drive 2.0 (setBandType :511-515) and routes modulate bandDrive(0)/(2) only (:2634-2635); the probe's preset lane loads bytes through Processor::setState (audit_probes.cpp.txt:1502-1504), so it measured the host path; no test checks threshold surviving setState.
- Impact: f:/tmp/disrumpo-audit/probe-results.txt:301 reads peak -50.76 / RMS -62.22 dBFS and :145 reads rms_delta 43.43 dB; a shipped factory preset's band going near-silent on project reload justifies high; the "intended -12 dBFS" level depends on the replica and on the cursor, which does not change the defect.

### D-155 — Measured: the stale transition gain on an on-node cursor is reachable through a MorphMode switch (-23.5 dB) and through Linear1D with 3-4 nodes (-3.0 dB); the cursor-jump path is not reachable

**Severity:** high | **Verdict:** CONFIRMED | **Category:** measured | **Dimension:** followup:missing-rt-probes | **Corroborated by measurement:** yes (follow-up probe 'stale transition gain on node')

**Location:** [plugins/disrumpo/src/dsp/morph_engine.cpp:107-109](plugins/disrumpo/src/dsp/morph_engine.cpp#L107-L109), [:186-192](plugins/disrumpo/src/dsp/morph_engine.cpp#L186-L192), [:218-224](plugins/disrumpo/src/dsp/morph_engine.cpp#L218-L224), [:267-270](plugins/disrumpo/src/dsp/morph_engine.cpp#L267-L270), [:748-756](plugins/disrumpo/src/dsp/morph_engine.cpp#L748-L756)

**Problem.** When any node is within 0.001 of the cursor, calculateMorphWeights returns early (morph_engine.cpp:186-192 for Linear1D, 218-224 for Planar2D). That skips the transitionGains_ update at :267-270. processCrossFamily then multiplies the one-hot node by whatever gain was left over (:748-756). setMode (:107-109) only assigns mode_ and recomputes nothing. Two paths leave a stale gain below 1. (a) Switch from Planar2D to Linear1D while x is 0: the node at x=0 becomes one-hot but keeps its small Planar2D gain. (b) Linear1D with 3-4 nodes: C and D share posX with A and B, so on the way to x=0 the weights of A and C are both about 0.5 and the gain is 0.707. At <0.001 A becomes one-hot and the 0.707 freezes.

**Why wrong.** With the cursor on a node, the output must equal that node alone (FR-014, 100 % weight). The weights report 1.0/0/0/0, but the audio is attenuated by a gain left over from a previous position or mode.

**Consequence.** A Planar2D-to-Linear1D mode change with the cursor on the left edge drops the band by up to 26.5 dB against the node alone, and the level stays there until the cursor moves more than 0.001. Every 3-4-node Linear1D band parked at either end of the axis sits a constant 3.01 dB low. The prior hypothesis (silence after a smoothing-0 cursor jump, D-012/D-120) is NOT reproduced: the morph smoother's 0.1 ms floor (smoother.h:58, 82) walks the cursor through intermediate positions that refresh the gains, giving 0.00 dB delta in all 10 jump cases.

**Evidence.** Probe 'stale transition gain on node' (48 kHz, 220 Hz -12 dBFS, drive 3). Reference: SoftClip alone = -6.62 dB. All jump cases at smoothing 0 and 10 ms, Linear1D and Planar2D (0.99->0, 0.5->0, 1.0->0, 0.99->0.01) read -6.62 dB, delta 0.00. onnode:planar-to-linear1d-switch: y=0.90 gives -33.15 dB after the switch against -9.63 dB for fresh Linear1D (delta -23.52; -26.5 dB against SoftClip alone), weights 1/0/0/0. y=0.70 gives delta -2.71. y=0.50 gives delta -14.68. Fresh Linear1D 4-node at x=0 = -9.63 dB, which is -3.01 dB against SoftClip alone with weights 1/0/0/0 (0.707 gain). Code: morph_engine.cpp:188-192 'weights_.fill(0.0f); weights_[i] = 1.0f; return;' and :748 'const float gain = transitionGains_[i];'.

**Suggested fix.** Update transitionGains_ on the on-node branch too: set the one-hot node's gain to 1 and the others to 0, or fall through to the common threshold/gain block instead of returning. Recompute weights and gains in setMode and setNodes. Regression test: 4-node cross-family band, cursor (0,0.9) Planar2D, render, switch mode to Linear1D, render. The level must be within 0.5 dB of node A alone. A fresh Linear1D 4-node band at x=0 must also be within 0.5 dB of node A alone.

**Verifier notes:**
- Correctness: one detail is wrong, setMode (morph_engine.cpp:107-113) does call calculateMorphWeights at :112, but that recompute takes the same early return so the gain still goes stale; the processor's fixed corners (processor.cpp:114-120) put A and C at posX 0, so Linear1D gives both about 0.5 and calculateTransitionGain (:352-380) returns 0.707 which freezes at x<0.001; at (0,0.9) A's weight is about 0.012, above kWeightThreshold (morph_node.h:139), giving sin(0.03*pi/2) = 0.047 (-26.5 dB), matching the probe.
- Context: transitionGains_ is written only at reset() (:87) and :268-270; the mode change is reachable through processor_params.cpp:967 and processor_state.cpp:827 via band_processor.h:260-262; only cross-family bands are affected (processSameFamily never reads transitionGains_); no test pins on-node transition gains.
- Impact: the default 4-corner layout parks every 3-4-node Linear1D band 3.01 dB low at the axis ends and a mode switch can drop a band 26 dB until the cursor moves; both break the on-node 100 % requirement; high stands.

### D-142 — Stochastic type outputs -10 dBFS of noise from digital silence: jitter offset is added to the input before the shaper and nothing blocks DC

**Severity:** high | **Verdict:** CONFIRMED | **Category:** measured-probe | **Dimension:** measured | **Corroborated by measurement:** yes (probe `selfnoise:Stochastic`)

**Location:** [dsp/include/krate/dsp/primitives/stochastic_shaper.h:563-583](dsp/include/krate/dsp/primitives/stochastic_shaper.h#L563-L583)

**Problem.** `const float jitterOffset = jitterAmount_ * smoothedJitter * kMaxJitterOffset + driftOffset;` and then `float out = waveshaper_.process(x + jitterOffset);`. With x = 0 the output is waveshaper(offset), a wandering noise/DC signal. The adapter adds no DC blocker or gate for Stochastic (distortion_adapter.cpp:586-591, processRaw :573-575).

**Why wrong.** A distortion effect must be silent for silent input. The jitter should modulate the shaper's transfer function around the signal, or be subtracted (out - shaper(offset)), so that 0 maps to 0.

**Consequence.** With Stochastic selected the plugin emits -10 dBFS of noise when the input stops (-5.7 dBFS in the tail). Stochastic_Burst also shows DC 0.156. StochasticShaper is a shared KrateDSP primitive, but grep finds Disrumpo as its only consumer.

**Evidence.** probe selfnoise:Stochastic: "rms_dBFS -10.4, nonfinite 0" (threshold <= -90 dBFS), "The tail 1-1.5 s after input stops is -5.7 dBFS". Code: stochastic_shaper.h:563 `const float jitterOffset = jitterAmount_ * smoothedJitter * kMaxJitterOffset + driftOffset;`, :583 `float out = waveshaper_.process(x + jitterOffset);`

**Suggested fix.** Subtract shaper(jitterOffset) from the output (keeping silence at 0), or add a DC blocker and silence gate in the adapter for Stochastic. Test: the selfnoise:Stochastic probe must read <= -90 dBFS. Add a dsp_primitives test that StochasticShaper outputs 0 for input 0.

**Verifier notes:**
- Correctness: kMaxJitterOffset is 0.5 (:166) and kDriftMaxOffset 0.3 (:171), Disrumpo's default jitterAmount is 0.2 (distortion_adapter.h:200), updateDCBlockerState (distortion_adapter.cpp:586-592) excludes Stochastic and the only gate is drive < 0.0001 (:191); probe-results.txt:421/:461/:329 confirm -10.4 dBFS, -5.7 dBFS tail and Stochastic_Burst |dc| 0.156; the archived spec FR-022 defines output = waveshaper(input + jitterOffset), so the formula is deliberate but the result is still a defect.
- Context: the primitive's header says "No internal DC blocking: Compose with DCBlocker" (:22, :135, :157), so the fault is the Disrumpo adapter's composition; dsp-details.md:543-548 describes D25 as a randomized transfer function for "analog warmth", not a noise source; no silent-input test exists for Stochastic; a 10 Hz DCBlocker alone would not remove the 10 Hz wander, so subtracting shaper(offset) or gating is the effective fix.
- Impact (dissent on severity, not existence): the jitter is a sample-and-hold at 10 Hz default smoothed over about 80 ms (:666-678, :685), so the output is mostly sub-10 Hz wander that still eats headroom and moves meters; -10 dBFS is the maximum-drive case and drive 1x gives about -20 dBFS peak; this verifier recommended medium, the other two kept high; high retained (one of three).

### D-139 — Cross-family morph dips 13-19.5 dB and clicks: outputs are summed linearly with double attenuation, and skipped nodes re-enter with stale state

**Severity:** high | **Verdict:** CONFIRMED | **Category:** measured-probe | **Dimension:** measured | **Corroborated by measurement:** yes (probes `morph-sweep:4node-crossfamily-planar` and `morph-sweep:2node-crossfamily`)

**Location:** [plugins/disrumpo/src/dsp/morph_engine.cpp:741-757](plugins/disrumpo/src/dsp/morph_engine.cpp#L741-L757)

**Problem.** processCrossFamily computes `output += nodeOutput * weight * gain` with normalized weights times transitionGains_. Below 0.6 weight a node is attenuated by both w and sin(w/0.4*pi/2). Nodes with w < kWeightThreshold are `continue`d (:745) and never processed, so their filters, delays and RNG go stale and are fed live audio again when the weight rises.

**Why wrong.** FR-007/FR-008 and SC-002 require an equal-power crossfade, with level within 1 dB across a cross-family morph and no artifacts (SC-003). A linear sum of decorrelated outputs dips, and the extra transition gain removes still more level.

**Consequence.** Sweeping the cursor across cross-family nodes drops the level by up to 19.5 dB, with 54 clicks on a 4-node sweep (jump ratio 9.9).

**Evidence.** probe morph-sweep:4node-crossfamily-planar: "jump_ratio 9.90, clicks_sweep 54, clicks_static 0, start_dB -6.62, end_dB -17.82, max_level_dev_dB -19.51". morph-sweep:2node-crossfamily(SoftClip-SineFold): "1.83,1,0,0,-6.62,-14.66,-12.95" (threshold jump_ratio <= 1.3, |level dev| <= 3 dB). Code: morph_engine.cpp:745 `if (weight < kWeightThreshold) continue;`, :756 `output += nodeOutput * weight * gain;`

**Suggested fix.** Use sqrt(w) (equal-power) gains normalised so that the sum of squares is 1, drop the double transition gain, and keep processing low-weight nodes (or crossfade them in from a primed state). Test: the morph-sweep probes must show |level dev| <= 1 dB (SC-002) and jump_ratio <= 1.3.

**Verifier notes:**
- Correctness: at the 2-node midpoint each node is 0.5 x 0.707 = 0.354 (-3 dB correlated, about -6 dB decorrelated, matching start_dB -6.62); a worse defect the finding missed is that calculateTransitionGain (:352-379) is discontinuous at w = 0.4 (just below 0.4 the gain is sin(pi/2) = 1, at 0.4 the zone branch gives sin(0) = 0), which explains the clicks and deep dips; stale-state re-entry at gain about 0.004 is the weaker part.
- Context: the spec (dsp-details.md:1150-1198) gives gainA = 1 below 0.4 then a complementary cos/sin crossfade plus a 7 ms TransitionFader (:1201-1229), neither of which the code implements; morph_interpolation_test.cpp:305 asserts only ratio < 3.0 (about 9.5 dB) and AP-003 (morph_artifact_test.cpp:383) uses the same-family setup, so no test can catch this.
- Impact: the step at w = 0.4 lands mid-block (weights recomputed per sample from the smoothed cursor, :668-672) and any dragged or automated cross-family morph triggers it; morphing is the plugin's headline feature and FR-007/FR-008/SC-002/SC-003 are violated; high.

### D-137 — Sweep-to-morph link runs only while the editor is open, follows only the Sweep Frequency parameter (not LFO/envelope), and writes automation without begin/endEdit

**Severity:** high | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** gestalt-controller | **Also flagged by:** D-039 (sweep-modulation) covers the DSP side of the same gap | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/controller.cpp:1454-1456](plugins/disrumpo/src/controller/controller.cpp#L1454-L1456)

**Problem.** The only working implementation of the per-band Morph X/Y Link feature (FR-034, the 'Morph X Link'/'Morph Y Link' COptionMenus at editor.uidesc:1353-1355) is MorphSweepLinkController. That is an IDependent created in didOpen and destroyed in willClose (controller.cpp:1535-1538). It observes kSweepFrequency, maps it through applyMorphLinkMode, and calls setParamNormalized + performEdit on kBandMorphX/Y (visibility_controllers.cpp:197-209). The processor ignores kBandMorphXLink/YLink (processor_params.cpp:971-972, no-op), and SweepProcessor::getMorphPosition is never called. With the editor closed, nothing links morph to sweep. Even with the editor open, the link follows only the base parameter: the sweep LFO, sweep envelope and mod-matrix offsets move the audible sweep inside the processor and never change the kSweepFrequency parameter, so the morph cursor does not follow them.

**Why wrong.** A sound-shaping link that the user sets in the editor must behave the same whether the editor is open or not, and the same during offline render. A UI-lifetime object cannot carry DSP behaviour. The controller also issues performEdit without beginEdit/endEdit from inside IDependent::update, which fires during host automation playback of Sweep Frequency. That writes Morph X/Y automation (in Touch/Latch modes it records it) and competes with existing Morph X/Y automation lanes. VST3 requires performEdit to be bracketed by beginEdit/endEdit.

**Consequence.** A preset that links Morph X to the sweep sounds different depending on whether the editor window is open. With the editor closed, during an offline bounce, and on a second instance with no editor, the morph stays put. A sweep driven by the sweep LFO or envelope never morphs, which is the usual way a sweep is animated. When the editor is open during automation playback, the host may record spurious Morph X/Y automation. The loop also walks bands 4-7, which do not exist (visibility_controllers.cpp:156).

**Evidence.** controller.cpp:1454-1456 `if (sweepFreqParam) morphSweepLinkController_ = new MorphSweepLinkController(this, sweepFreqParam);` (didOpen only). controller.cpp:1535-1538 deactivate + null in willClose. visibility_controllers.cpp:155-157 `float sweepNorm = sweepFreqParam_->getNormalized(); for (int band = 0; band < 8; ++band)`. visibility_controllers.cpp:198-201 `controller_->setParamNormalized(...kBandMorphX, newX); controller_->performEdit(...kBandMorphX, newX);` with no beginEdit/endEdit. processor_params.cpp:971-972 `case BandParamType::kBandMorphXLink: case BandParamType::kBandMorphYLink:` fall into the no-op group. editor.uidesc:1353,1355 are the user-facing link menus.

**Suggested fix.** Move the link into the processor. In Processor::process, after the sweep's modulated frequency is computed, apply the link curve per band from bandStates' X/Y link mode to the morph position passed to setMorphPosition, as an offset or a replacement. Persist the link modes in state. Remove MorphSweepLinkController and the controller-side performEdits. Test: a processor-level test (no controller) that sets kBandMorphXLink=SweepFreq, sweeps kSweepFrequency from 0 to 1, renders, and asserts that the band's morph weights change. A second test enables the sweep LFO with the base frequency fixed and asserts that the morph position moves.

**Verifier notes:**
- Correctness: morphSweepLinkController_ exists only at controller.h:341 and the two didOpen/willClose sites; SweepProcessor::getMorphPosition (sweep_processor.h:297) has callers only in tests/dsp/sweep_processor_tests.cpp:334-425; the global kSweepMorphLink is stored (processor_params.cpp:410-413, processor_state.cpp:95) but never read; other edit sites bracket correctly (controller.cpp:1180-1181, morph_pad.cpp:641-647) so visibility_controllers.cpp:198-208 is the odd one out; the band 0-7 loop is harmless because getParameterObject returns null and :168 returns early.
- Context: specs-overview.md:287-291 (FR-SWEEP-003) and roadmap.md:460-472 describe sweep-morph linking as audio behaviour ("Sweep with morph linking functional"), not UI; the only tests (tests/unit/morph_link_test.cpp) cover the pure mapping function; a host renders offline with the editor closed, so the failure is reachable.
- Impact: band link modes are not persisted anywhere (Controller::getState at controller.cpp:658-677 writes only version, two unused doubles and MIDI CC mappings), so the editor-open/closed difference is within a session only and a Morph X Link resets to None on reload; a headline feature is effectively non-functional in the DSP and can pollute automation; high is defensible.

### D-136 — MIDI Learn / MIDI CC mapping cannot work: no event input bus, the learn flag never reaches the processor, and the processor watches a different param ID

**Severity:** high | **Verdict:** CONFIRMED | **Category:** ui-wiring | **Dimension:** gestalt-controller | **Also flagged by:** D-096 (spec-conformance), D-058, D-094 cover the same feature | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/controller.cpp:1611-1662](plugins/disrumpo/src/controller/controller.cpp#L1611-L1662)

**Problem.** Every control's right-click menu offers 'MIDI Learn'. Its action calls `midiCCManager_->startLearn(paramId)` and `setParamNormalized(kGlobalMidiLearnActive, 1.0)`, which only changes controller-side state: no performEdit, so the processor never sees it. The processor's CC scan is keyed on a different parameter, kSweepMidiLearnActive (0x0E50, processor_params.cpp:527-528). It only looks at kLegacyMIDICCOutEvent in data.inputEvents (processor.cpp:392-397). The processor never declares an event input: initialize only calls addAudioInput/addAudioOutput (processor.cpp:65), so hosts never deliver MIDI and inputEvents is null. The detected-CC output parameter kSweepDetectedCCOutputId is never read by the controller (no match in src/controller), and nothing calls a learn-completion API on MidiCCManager. getMidiControllerAssignment (controller.cpp:707-726) also needs an event bus for hosts to query it. Its fallback reads the controller's `assignedMidiCC_` (controller.h:435), which is never assigned.

**Why wrong.** VST3 hosts route MIDI CC to a plugin, and query IMidiMapping for the event bus index, only when the component exposes an event input bus. A learn mode also needs a completion path: either the processor reports the CC and the controller maps it, or the controller implements IMidiLearn::onLiveMIDIControllerInput. Here the UI flag (0x0F07) and the processor flag (0x0E50) are different parameters, the processor flag is never written by any UI, and the processor's report (0x0F81) has no listener. Each half was built against a different design (spec 012 global learn vs FR-028 sweep-only learn) and the two were never connected.

**Consequence.** The user right-clicks a knob and chooses MIDI Learn, moves a controller, and nothing happens. Learn mode stays armed in MidiCCManager until Escape. Mappings loaded through Controller::setState (MidiCCManager blob) are never exercised because no CC ever reaches the plugin. 'Clear MIDI Learn' and 'Save Mapping with Preset' operate on mappings that cannot exist. A user-visible feature (FR-028/029/031) is dead in every host.

**Evidence.** controller.cpp:1623-1627 `midiCCManager_->startLearn(paramId); setParamNormalized(makeGlobalParamId(GlobalParamType::kGlobalMidiLearnActive), 1.0);`. processor.cpp:65 `addAudioInput(...)` is the only bus besides the output (grep addEventInput in src/processor: none). processor.cpp:392 `if (midiLearnActive_ && data.inputEvents)`, :397 `kLegacyMIDICCOutEvent`, :402-405 write kSweepDetectedCCOutputId. processor_params.cpp:527-528 `case SweepParamType::kSweepMidiLearnActive: midiLearnActive_ = (value >= 0.5);`. A grep for kSweepDetectedCCOutputId/IMidiLearn/onLiveMIDIControllerInput in src/controller finds no consumer. controller.cpp:720 `if (assignedMidiCC_ < 128 && ...)`. controller.h:435 `int assignedMidiCC_ = 128;` is never assigned.

**Suggested fix.** Pick one design. (a) Add `addEventInput(STR16("MIDI In"), 16)` to Processor::initialize. Implement IMidiLearn::onLiveMIDIControllerInput in the controller to complete midiCCManager_ learning, call restartComponent(kMidiCCAssignmentChanged), and keep IMidiMapping as the host-side routing. (b) Remove the context-menu entry and the dead params (kGlobalMidiLearnActive/Target, kSweepMidiLearnActive/CCNumber, kSweepDetectedCCOutputId, assignedMidiCC_ in both processor and controller). Test: a controller unit test that runs startLearn, then a simulated onLiveMIDIControllerInput(cc 7), then asserts that getMidiControllerAssignment(0, 0, 7, id) returns the learned param. A processor test that asserts getBusCount(kEvent, kInput) == 1.

**Verifier notes:**
- Correctness: every link checks out; kSweepDetectedCCOutputId is only registered read-only (parameter_registration.cpp:307); MidiCCManager::processCCMessage (shared midi_cc_manager.h:101) is never called from plugins/disrumpo/src; the controller implements only IMidiMapping (controller.h:62, 222), not IMidiLearn; the Escape path (controller.cpp:1408-1412) is also controller-local.
- Context: the processor scan filters on kLegacyMIDICCOutEvent, an output-event type that hosts never deliver as input, so it would not fire even with a bus; tests/integration/midi_learn_integration_test.cpp drives MidiCCManager alone (lines 27-47) and never touches plugin wiring; the comments cite FR-028/029 as implemented, nothing says the feature is meant to be inert.
- Impact: the MIDI Learn, Clear and Save-with-Preset menu items do nothing in any host and stored mapping blobs are never applied; no audio corruption; high for a dead user-visible feature.

### D-135 — Shape-slot 2 stepCount 8 makes VSTGUI wipe the custom menu-items: Tube Stage, AsymFuzz Trans, Serge Model, Bitcrush Mode and SampleReduce Mode menus show "0".."8"

**Severity:** high | **Verdict:** CONFIRMED | **Category:** ui-wiring | **Dimension:** gestalt-controller | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/parameter_registration.cpp:817-824](plugins/disrumpo/src/controller/parameter_registration.cpp#L817-L824)

**Problem.** Every band's 'Displayed Shape 2' proxy (and every node's real Shape 2) is registered as RangeParameter(0,1) with stepCount 8 (`int32_t stepCount = (s == 2) ? 8 : 0;`). BandSubController::verifyView adds the uidesc `menu-items` entries to each COptionMenu and then delegates to VST3Editor::verifyView. That creates a ParameterChangeListener. For a parameter with stepCount>0, its updateControlValue calls `optMenu->removeAllEntry()` and appends stepCount+1 entries from getParamStringByValue(i/8). formatShapeSlot only formats slot 2 for Temporal and RingSaturation, so for the other types the call falls through to RangeParameter::toString, which prints the integer step 0..8. Five slot-2 menus are affected: TypeParams_Tube 'Stage' (12AX7,EL84,6L6,300B), AsymFuzz 'Trans' (Germanium,Silicon), SergeFold 'Model' (Simple,Serge,Buchla,Lockhart), Bitcrush 'Mode' (Truncate,Dither) and SampleReduce 'Mode' (ZOH,Linear). Each loses its names and shows nine numeric entries.

**Why wrong.** The code assumes the slot params have stepCount 0, so VSTGUI's auto-population is skipped. The comment at sub_controllers.h:191-194 says so, and so does the menu_items_test.cpp header ('Shape slot parameters are RangeParameter(0,1, stepCount=0)'). For slot 2 that assumption is false. With stepCount 8 the SDK RangeParameter also treats plain values as step indices 0..8 (vstparameters.cpp:200-205), so the menu's index range and the DSP's ranges no longer match. Tube satStage is round(slot*3) (processor_params.cpp:58), so entries 0..8 select stage 0,0,1,1,2,2,2,3,3. SergeFold foldModel uses the same mapping. AsymFuzz transistor is round(slot), so entries 0-3 select Germanium and 4-8 select Silicon. The same root cause has a second effect: the HalfRectify 'DC Block' ToggleButton on slot 2 (uidesc:996) gets min/max 0..8. With the slot default of 0.5, toPlain gives 4, so the toggle draws OFF while the DSP sees dcBlock = (0.5 >= 0.5) = true.

**Consequence.** On five distortion types the user sees a menu of '0'..'8' instead of tube, transistor, fold model, crush mode or reducer mode names. They cannot tell which tube or model they picked, and several entries select the same DSP value. The HalfRectify DC-block toggle shows the opposite of what the DSP is doing until it is clicked. Hosts also show 'B1 N1 Shape 2' as a nine-step integer for every type.

**Evidence.** parameter_registration.cpp:820 `int32_t stepCount = (s == 2) ? 8 : 0;` (proxy) and :957 (node params). sub_controllers.h:191-194: 'Shape slot parameters are RangeParameter(stepCount=0), so VSTGUI's auto-populate ... is skipped ... Our entries survive.' extern/vst3sdk/vstgui4/vstgui/plugin-bindings/vst3editor.cpp:273-276 (isStepCount when stepCount>0) and :325-330 (`optMenu->removeAllEntry (); for (i = 0; i <= stepCount; i++) optMenu->addEntry (getParamStringByIndex (i)...)`). vstparameters.cpp:160-166 (RangeParameter::toString prints the int step when stepCount>1). format_helpers.h:48-141 (slot 2 handled only for Temporal/RingSat). editor.uidesc:900, 935, 974, 996, 1005, 1016 (slot-2 COptionMenus and ToggleButton). processor_params.cpp:58, 83, 110, 126, 133, 141.

**Suggested fix.** Do not quantize slot 2 globally. Register every shape slot with stepCount 0, and quantize the 9-curve Temporal/RingSat selector in formatShapeSlot and in the processor (both already round v*8). Alternatively, give the menus a per-type list parameter, which is not possible with one shared slot. Add a headless-editor test that opens TypeParams_Tube (and SergeFold, AsymFuzz) for band 0 and asserts that the COptionMenu has 4 (or 2) entries with the uidesc names, and that choosing entry 1 sets the proxy normalized value to 1/3 (or 1.0). Update the false comment in sub_controllers.h and in menu_items_test.cpp.

**Verifier notes:**
- Correctness: the proxy and node params both carry stepCount 8 (parameter_registration.cpp:820, :957); sub_controllers.h:151-160 maps Band.NodeShape2 to kBandDisplayedShape2 and the five menus (uidesc :900/:935/:974/:1005/:1016) plus the ToggleButton (:996) sit on that tag; VST3Editor::verifyView (vst3editor.cpp:1042-1063) builds a ParameterChangeListener whose constructor fires changed() (:112-114) and updateControlValue removes all entries and adds nine (:325-330); Controller::getParamStringByValue (controller.cpp:826-841) -> formatShapeSlot returns false for the five types so RangeParameter::toString prints the step (vstparameters.cpp:158-166).
- Context: no controller code re-adds entries (removeAllEntry/menu-items appear only at sub_controllers.h:200); git history shows menu-items arrived in f2cf88e1 and stepCount 8 was added later in 70c96aa9 for the Temporal waveshape selector without considering the other slot-2 menus, so this is a regression not a design decision; no test opens a TypeParams template.
- Impact: it reproduces on every host whenever one of the five types is shown; audio stays correct and every DSP value remains reachable, so it is UI correctness only, but five user-facing selectors are unreadable and the DC toggle is drawn inverted; high is defensible.

### D-117 — Controller state paths (preset writer, setComponentState, Controller get/setState) have zero tests, while hand-copied 'mirror' helpers test the correct formula

**Severity:** high | **Verdict:** CONFIRMED | **Category:** test-gap | **Dimension:** test-gaps | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/tests/integration/state_persistence_test.cpp:35-200](plugins/disrumpo/tests/integration/state_persistence_test.cpp#L35-L200), [:259-290](plugins/disrumpo/tests/integration/state_persistence_test.cpp#L259-L290), [:345-417](plugins/disrumpo/tests/integration/state_persistence_test.cpp#L345-L417)

**Problem.** No test calls Controller::createComponentStateStream, parseComponentState, setComponentState, loadComponentStateWithNotify or Controller::getState/setState (grep of tests finds none). state_persistence_test instead re-implements the formulas: controllerNormalizeSource uses /(kModSourceCount-1) (:276-278), and 'Full routing slot binary round-trip' writes and reads its own byte buffer. 'SC-011: All state persists together' copies local floats into other locals and asserts they are equal; it never touches the Controller.

**Why wrong.** The real preset writer has different formulas from the mirrors. It writes the routing source as getInt8FromList(id, 12), i.e. round(norm*12), while the load side uses 13 (controller.cpp:1699-1700, 1815). It writes active nodes as round(norm*2)+2 for a 4-entry list (controller.cpp:1830-1831). Window size and expand state are not persisted at all. The tests can only fail if the helper formulas disagree with each other, never if the shipped code is wrong.

**Consequence.** A user preset saved from the browser loads routings from sources 7-13 one source lower and changes 1-node bands to 2 nodes and 2-node bands to 3. SC-011 is reported as covered while it is not.

**Evidence.** controller.cpp:1815 `streamer.writeInt8(getInt8FromList(makeRoutingParamId(r, 0), 12));`; controller.cpp:1831 `int activeNodes = static_cast<int>(std::round(activeNodesNorm * 2.0f)) + 2;`; state_persistence_test.cpp:276-278 `static double controllerNormalizeSource(int8_t source) { return static_cast<double>(source) / (Krate::DSP::kModSourceCount - 1); }`; :123-130 `restoredExpandStates[i] = serializedExpandStates[...]; float restoredModPanel = serializedModPanel;`

**Suggested fix.** Add TEST_CASE "Controller preset stream round-trips through processor". Initialize a Controller, set non-default params (routing source 13 on slot 0, active nodes 1 on band 0, morph mode 2, smoothing 100 ms), call createComponentStateStream, feed the stream to Processor::setState then getState, and REQUIRE source == 13, activeNodes == 1 and morph mode == 2. Add a Controller::getState/setState round-trip for the MIDI CC blob. Delete or rewrite the tautological SC-011 sections.

**Verifier notes:**
- Correctness: the preset save path really goes through controller.cpp:116-124 (createComponentStateStream / loadComponentStateWithNotify); the 14-entry source list (parameter_registration.cpp:562-577) means round(12k/13) = k-1 for k >= 7; the 4-entry node list (parameter_registration.cpp:699-706, loader (count-1)/3 at controller.cpp:525-526) means round(norm*2)+2 maps 1->2, 2->3, 3->3, 4->4; the mirror helper divides by kModSourceCount-1 = 17 (modulation_types.h:63), which matches neither side.
- Context: only editor_lifecycle_test.cpp and data_exchange_spectrum_test.cpp construct a Controller and neither covers state; preset_integration_test.cpp passes its own stateProvider lambda (:219); the SC-011 case (:34-140) copies locals into locals.
- Impact: preset_manager.cpp:309/361/417/449 use the state provider whenever processor_ is null, so every browser save runs controller.cpp:1687 onward; kDefaultActiveNodes is 1 (morph_node.h:135), so every default band in a user preset reloads with 2 nodes and morph switches on; silent corruption of user presets justifies high.

### D-116 — All oversampling, bypass (SC-011) and alias-suppression tests run BandProcessor::processBlock, which the plugin never calls

**Severity:** high | **Verdict:** CONFIRMED | **Category:** test-gap | **Dimension:** test-gaps | **Corroborated by measurement:** no (but see D-027/D-095 probes)

**Location:** [plugins/disrumpo/tests/dsp/oversampling_single_type_tests.cpp:171-209](plugins/disrumpo/tests/dsp/oversampling_single_type_tests.cpp#L171-L209), [:267-289](plugins/disrumpo/tests/dsp/oversampling_single_type_tests.cpp#L267-L289)

**Problem.** The seven dsp/oversampling_*.cpp files and the SC-011 'bypassed band output is bit-identical' case drive bp.processBlock(...), usually on the legacy setDistortionType path. Processor::process only calls the per-sample BandProcessor::process (processor.cpp:487, 494). That function never reads bypassed_ and never oversamples (band_processor.h:473-512). The bypass check and the oversampling path exist only in processBlock (band_processor.h:519-523).

**Why wrong.** The tests prove FR-012 bit-transparent bypass, SC-006 alias suppression and the FR-017 8 ms factor crossfade on a code path the shipped processor never runs. They stay green while the plugin runs every shaper at 1x and treats kBandBypass as a no-op. The adapters are also prepared at sampleRate*8 (band_processor.h:111,124) while they run at 1x, and no test catches that either.

**Consequence.** Users get unmitigated aliasing on the 4x-profile types (HardClip, folds, fuzz, rectifiers), a band bypass that does nothing, and an Oversample Max control that does nothing, while every spec-009 test reports green.

**Evidence.** processor.cpp:494 `bandProcessors_[b].process(bandL, bandR);` (the only band entry; grep finds no processBlock in src/processor); band_processor.h:519-523 `void processBlock(...) { if (bypassed_) { return; }`; band_processor.h:473-512 process() never references bypassed_; oversampling_single_type_tests.cpp:203 `bp.processBlock(left.data(), right.data(), kNumSamples);`

**Suggested fix.** Add processor-level cases in processor_audio_output_test.cpp. (1) "Processor HardClip 5 kHz aliasing drops with Oversample Max 4x vs 1x": set the node type, drive and kOversampleMaxId through IParameterChanges, render, FFT, and REQUIRE an alias-bin reduction > 6 dB. (2) "Processor band bypass is bit-transparent": 1 band, kBandBypass = 1, REQUIRE output == input sample-for-sample at mix 1. Both should fail today.

**Verifier notes:**
- Correctness: the only processBlock call in processor.cpp is sweepProcessor_.processBlock (:370); bypass and oversampling live only in processBlock -> processBlockInner -> processWithFactor (band_processor.h:519-523, :548, :627, :653, :709-742); shouldBandContribute (processor.cpp:563-575) checks only mute and solo; kBandBypass and kOversampleMaxId appear in tests only as ID-encoding/keyboard checks.
- Context: processor_params.cpp:360 and :929 do forward the parameters into fields the per-sample path ignores; the only comment is processor.cpp:444 "FR-001a: sample-by-sample processing" and specs/ defines no FR-001a that drops oversampling or bypass.
- Impact: at 44.1 kHz a 5 kHz input into HardClip/folds/fuzz folds its 5th harmonic and up back into the audible band at full 1x level, which is the SC-006 margin (>= 48 dB) the tests credit to oversampling; a dead bypass and a dead Oversample Max control ship with the suite green; high.

### D-115 — No test follows the host call order; setActive(true) wipes restored mod routings and setupProcessing wipes restored morph nodes

**Severity:** high | **Verdict:** CONFIRMED | **Category:** test-gap | **Dimension:** test-gaps | **Corroborated by measurement:** no (the defects it hides are measured under D-046)

**Location:** [plugins/disrumpo/tests/integration/preset/factory_preset_validation_test.cpp:98-111](plugins/disrumpo/tests/integration/preset/factory_preset_validation_test.cpp#L98-L111), [:171-249](plugins/disrumpo/tests/integration/preset/factory_preset_validation_test.cpp#L171-L249)

**Problem.** Every state test builds its processor as initialize -> setupProcessing -> setState -> getState (factory_preset_validation_test.cpp:98-111, serialization_round_trip_test.cpp:48-62). None calls setActive(true) after setState, and none calls setState before setupProcessing. That is the order hosts normally use: initialize, setState, setupProcessing, setActive, process.

**Why wrong.** Processor::setActive(true) calls modulationEngine_.reset() (processor.cpp:164). That reset sets every routing to ModRouting{} and clears the macros (modulation_engine.h:152-157). Processor::setupProcessing rebuilds bandMorphCache_[i].nodes as four SoftClip nodes with activeNodeCount = kDefaultActiveNodes and pushes them to the band processors (processor.cpp:111-127). getState reads routings from modulationEngine_ (processor_state.cpp:298-307) and nodes from bandMorphCache_. A setState -> setActive(true) -> getState test would therefore come back with empty routings, and a setState -> setupProcessing test with SoftClip nodes. The suite never runs either sequence.

**Consequence.** In normal use, loading a project restores routings and macros that are then erased when the host activates the plugin. A host that calls setupProcessing after setState also loses the node types. No test can catch this.

**Evidence.** processor.cpp:164 `modulationEngine_.reset();` in setActive(true); modulation_engine.h:152-155 `// Reset routings  for (auto& r : routings_) { r = ModRouting{}; }`; processor.cpp:114-121 `cache.nodes[0] = MorphNode(0, 0.0f, 0.0f, DistortionType::SoftClip); ... cache.activeNodeCount = kDefaultActiveNodes;`; factory_preset_validation_test.cpp:98-111 createTestProcessor calls setupProcessing only, with no setActive.

**Suggested fix.** Add TEST_CASE "State survives host activation order" in serialization_round_trip_test.cpp. Run initialize -> setState(non-default v12 stream) -> setupProcessing -> setActive(true) -> process 1 block -> getState, then REQUIRE that the routing slots, macros, node types and activeNodeCount match the input. Also render one block with an LFO->band-gain routing and REQUIRE that the output level varies. Run the factory-preset round-trip with the same order.

**Verifier notes:**
- Correctness: routings and macros are stored only inside modulationEngine_ (writers at processor_state.cpp:751-760/:798 and processor_params.cpp:551-572/:765-817, no shadow copy), getState reads them back from the engine (:289, :299); a grep for files calling both setActive and setState in plugins/disrumpo/tests returns none.
- Context: setupProcessing also calls modulationEngine_.prepare() (processor.cpp:139), and prepare() itself calls reset() (modulation_engine.h:123), so setupProcessing after setState wipes routings as well as nodes; the only comment is "Activating: reset processing state" (processor.cpp:148).
- Impact: project load in every conforming host, every setActive toggle (bypass, transport restart, offline render) and every sample-rate or buffer change hit these paths; setActive(true) appears only in four audio tests that never call setState and the six setState tests never call setActive; high.

### D-114 — Morph-mode / smoothing round-trip tests compare getState to getState, hiding that the processor saves literal 0 for both

**Severity:** high | **Verdict:** CONFIRMED | **Category:** test-gap | **Dimension:** test-gaps | **Corroborated by measurement:** no (the defect it hides is D-048)

**Location:** [plugins/disrumpo/tests/unit/preset/serialization_round_trip_test.cpp:226-228](plugins/disrumpo/tests/unit/preset/serialization_round_trip_test.cpp#L226-L228), [:463-497](plugins/disrumpo/tests/unit/preset/serialization_round_trip_test.cpp#L463-L497), [:875-899](plugins/disrumpo/tests/unit/preset/serialization_round_trip_test.cpp#L875-L899)

**Problem.** writeNonDefaultV6Preset writes morphMode = b % 3 per band (line 226). The 'MorphMode values round-trip' section and 'v6 full parameter set' then do setState(input) -> getState(stream1) -> setState(stream1) -> getState(stream2) and compare only stream1 with stream2 (compareV6PresetStreams). Nothing compares stream1 with the input stream.

**Why wrong.** A round-trip test has to compare the output with what went in. Processor::getState writes writeInt8(0) for morphMode and writeFloat(0.0f) for morphSmoothing (processor_state.cpp:319, 323). So stream1 and stream2 both carry 0 and the test passes, even though the input's morph modes 1 and 2 were lost. The section is named for exactly the property that is broken.

**Consequence.** A host project save loses every band's Morph Mode (Planar/Radial) and Morph Smoothing, and the suite stays green. The same blind spot covers any other field that getState rewrites to a constant or a modulated snapshot (sweep width/intensity/frequency, saved from the modulated sweepProcessor_ values).

**Evidence.** serialization_round_trip_test.cpp:226 `s.writeInt8(static_cast<Steinberg::int8>(b % 3)); // morphMode (0,1,2 cycling)`; :896-898 `REQUIRE(compareV6PresetStreams(*stream1, *stream2));`; processor_state.cpp:319 `if (!streamer.writeInt8(static_cast<Steinberg::int8>(0)))  // morphMode`, :323 `if (!streamer.writeFloat(0.0f))  // morphSmoothing (ms)`

**Suggested fix.** In 'Serialization round-trip: enum types preserved' / 'MorphMode values round-trip', parse stream1 and REQUIRE each band's morphMode == b % 3 and morphSmoothing == the written value (make the input non-zero first, e.g. 50 ms). Do the same for sweep width/intensity, and compare against the input with a tolerance rather than stream1 vs stream2. The test goes red until processor_state.cpp:319/323 writes the real values.

**Verifier notes:**
- Correctness: setState reads and applies morphMode and smoothing (processor_state.cpp:819-831) but getState writes constants (:319, :323) and no getMorphMode/getMorphSmoothing exists in src/processor; both test cases only REQUIRE compareV6PresetStreams(stream1, stream2) (:495, :898); one sub-claim is overstated because the input writes morphSmoothing = 0.0f (:228), so that test could never catch a lost smoothing value anyway.
- Context: Controller::getState (controller.cpp:658-677) writes only version, two unused doubles and MIDI mappings, setComponentState pushes the zeros into the parameters (controller.cpp:507-529), and only the controller's own preset writer (controller.cpp:1829-1834) keeps the values; no comment or spec says the zeros are deliberate (git log -L shows them arriving unchanged in the file split 7fb9c9881).
- Impact: a DAW project reload resets every band from Planar/Radial to 1D Linear, which changes the morph weight computation (morph_engine.cpp:180 vs :212/:244), and resets smoothing to 0 ms in every host; the test section named for the property passes while it is broken; high.

### D-097 — Cross-family morph dips about -3 dB at the midpoint (SC-002 '+/-1 dB'); the SC-002 test is relaxed to 3 dB and only tests same-family nodes

**Severity:** high | **Verdict:** CONFIRMED | **Category:** morph | **Dimension:** spec-conformance | **Also flagged by:** D-008, D-139 cover the same gain law | **Corroborated by measurement:** yes (morph-sweep probes, see D-139)

**Location:** [plugins/disrumpo/src/dsp/morph_engine.cpp:754-757](plugins/disrumpo/src/dsp/morph_engine.cpp#L754-L757), [:352-379](plugins/disrumpo/src/dsp/morph_engine.cpp#L352-L379)

**Problem.** processCrossFamily sums nodeOutput * weight * transitionGain. The weights are already normalized to sum to 1, and calculateTransitionGain additionally attenuates every node with w < 0.6 (sin of the zone position). At a 2-node midpoint (w=0.5/0.5), gain = sin(pi/4) = 0.707, so the output is 2*0.5*0.707 = 0.707x the correlated level. At the 4-node centre (w=0.25 each), gain = 0.831, giving -1.6 dB.

**Why wrong.** SC-002 (cited in tests/approval/morph_artifact_test.cpp:377) requires a cross-family morph to stay within 1 dB of single-type output. FR-MORPH-003 requires artifact-free transitions. A weighted sum with normalized weights is already level-preserving for correlated outputs, and multiplying again by a sub-unity 'transition gain' double-attenuates.

**Consequence.** Sweeping the cursor between, for example, a SoftClip node and a SineFold node at moderate drive produces an audible ~3 dB hole in the middle of the pad, worse than the 1 dB target. Test AP-003 cannot catch it: it builds createSameFamilySetup() (morph_artifact_test.cpp:383), which takes processSameFamily, and asserts diffMid < 3.0 dB (:466) with a comment admitting the relaxation.

**Evidence.** morph_engine.cpp:751 `const float gain = transitionGains_[i];` :757 `output += nodeOutput * weight * gain;`; :372-378 zone gain = equalPowerGains((w-0.4)/0.2) fade-in, so w=0.5 gives 0.7071; morph_artifact_test.cpp:383 `auto nodes = createSameFamilySetup();` :462-467 'Relaxed to 3dB ... REQUIRE(diffMid < 3.0f)'.

**Suggested fix.** For correlated-output nodes use output = sum(w_i * y_i) with no extra gain. If equal-power behaviour is wanted, normalize the gains so that sum(w_i*g_i) = 1 (or use sqrt weights with a sum-of-squares normalization). Fix AP-003: use two cross-family nodes (e.g. SoftClip and SineFold at drive 0.5, so both are near-linear) and assert |mid - avg| < 1.0 dB per SC-002.

**Verifier notes:**
- Correctness: both numbers re-derive (0.707 at w = 0.5, 0.831 at w = 0.25); the gain also jumps at w = 0.4 (just below 0.4 it tends to 1, at 0.4 it is sin(0) = 0), so two nodes at 0.399/0.601 sum to about 1.0 while 0.4/0.6 sums to 0.6, a -4.4 dB step in one sample; AP-003 asserts diffMid < 3.0f at :460 (the finding says :466).
- Context: dsp-details.md:1164-1198 specifies gainA = cos, gainB = sin of the zone position applied directly "to maintain perceived loudness", not multiplied by the weight, and the comment at :754 ("Equal-power weighted sum") does not describe what the code does; a cross-family SC-002 test does exist (morph_interpolation_test.cpp:258-306) but asserts a linear ratio < 3.0 (about 9.5 dB), skips when minLevel <= 0.001 and never samples near w = 0.4, so the coverage gap stands; this verifier recommended medium.
- Impact: any host and any mixed-family node set hits it once the cursor leaves a node, and the engine's default node set (SoftClip, Tube, Fuzz, SineFold, :41-44) is already mixed-family; the discontinuity at w = 0.4 is an audible click and timbre jump; high (two of three).

### D-096 — MIDI CC sweep control and MIDI Learn cannot work: the processor declares no event input bus (FR-SWEEP-004, FR-028/029, docs and CHANGELOG claims)

**Severity:** high | **Verdict:** CONFIRMED | **Category:** spec-conformance | **Dimension:** spec-conformance | **Also flagged by:** gestalt-processor; D-136 is the controller-side view | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor.cpp:62-64](plugins/disrumpo/src/processor/processor.cpp#L62-L64), [:390-413](plugins/disrumpo/src/processor/processor.cpp#L390-L413)

**Problem.** Processor::initialize adds only stereo audio in/out (no addEventInput anywhere in plugins/disrumpo/src). The MIDI-learn scan in process() reads data.inputEvents, which a host only supplies for an event bus, and it matches kLegacyMIDICCOutEvent, an output-event type. Controller::getMidiControllerAssignment (controller.cpp:707-726) implements IMidiMapping for bus 0, but there is no event bus 0. Ruinae, for comparison, calls addEventInput (ruinae processor.cpp:61). The processor-side assignedMidiCC_ is write-only, and the controller's assignedMidiCC_ is never assigned (only declared at controller.h:435).

**Why wrong.** FR-SWEEP-004 lists MIDI CC as a sweep driver. docs/index.html:123 claims 'Full parameter automation and MIDI CC learn support', and CHANGELOG.md:124 claims 'MIDI CC learn for parameter automation'. VST3 IMidiMapping maps CCs arriving on the plugin's event input bus, so a plugin without one receives no CCs.

**Consequence.** The right-click MIDI Learn menu starts a learn that can never complete from incoming MIDI, and learned or saved mappings never move a parameter in hosts that route MIDI only to plugins with an event input. The documented feature is non-functional.

**Evidence.** processor.cpp:62-64 `addAudioInput(...kStereo); addAudioOutput(...kStereo);` (no event bus); grep 'addEventInput' in plugins/disrumpo/src finds nothing; processor.cpp:396 `if (e.type == Steinberg::Vst::Event::kLegacyMIDICCOutEvent)`; controller.cpp:712 `if (busIndex != 0) return kResultFalse;`; controller.cpp:720 `if (assignedMidiCC_ < 128 && ...)` with assignedMidiCC_ never written in controller.

**Suggested fix.** Add `addEventInput(STR16("Event Input"), 1)` in initialize, and handle kLegacyMIDICCOutEvent removal: a host maps CCs to params via IMidiMapping, so learning should be completed on the controller side. Drop or wire the write-only assignedMidiCC_. Add a test that the processor's bus info reports one event input bus, and a controller test that a learned CC returns the mapped ID from getMidiControllerAssignment(0, ...).

**Verifier notes:**
- Correctness: every link holds; kSweepDetectedCCOutputId (plugin_ids.h:168, registered parameter_registration.cpp:307) has no controller consumer; the processor writes assignedMidiCC_ at processor.cpp:409 and processor_params.cpp:532 and nothing on the audio path reads it; MidiCCManager::processCCMessage is called only from tests.
- Context: gradus, innexus, membrum, profundum, ruinae, seraphis and vorago all call addEventInput and Disrumpo does not; the controller's learn arms a different flag (kGlobalMidiLearnActive, controller.cpp:1626) than the processor watches (kSweepMidiLearnActive, processor_params.cpp:527-533); specs-overview.md:293-297 (FR-SWEEP-004), dsp-details.md:1803 section 12 and CHANGELOG.md:124 all claim the feature.
- Impact (dissent on severity): no audio, state or real-time harm follows, host-level MIDI learn still works as a workaround, so this verifier recommended medium; the other two kept high for a documented, user-visible feature that is dead everywhere; high retained.


### D-095 — Band Bypass button and Space shortcut do nothing: BandProcessor::process ignores bypassed_ (NFR 'bit-transparent bypass', FR-012)

**Severity:** high | **Verdict:** CONFIRMED | **Category:** spec-conformance | **Dimension:** spec-conformance | **Also flagged by:** D-020 (signal-path) | **Corroborated by measurement:** yes (probe `bypass:all-bands`)

**Location:** [plugins/disrumpo/src/dsp/band_processor.h:473-512](plugins/disrumpo/src/dsp/band_processor.h#L473-L512), [:519-523](plugins/disrumpo/src/dsp/band_processor.h#L519-L523)

**Problem.** kBandBypass is handled by bandProcessors_[band].setBypassed() (processor_params.cpp:927-930). bypassed_ is only read in processBlock (band_processor.h:521, comment 'FR-012: Bypass optimization - bit-transparent pass-through'), which the processor never calls. process() never reads bypassed_, and Processor::shouldBandContribute (processor.cpp:563-576) checks only mute and solo. The plugin also registers no kIsBypass parameter.

**Why wrong.** specs-overview.md 'Audio Quality' NFR requires bit-transparent bypass, and FR-BAND-004 lists 'Solo, Bypass, Mute toggles'. A bypassed band should pass its crossover band signal undistorted.

**Consequence.** The per-band Bypass toggles in the editor (editor.uidesc:1264, 1350, control-tag Band.Bypass) and the Space keyboard shortcut (keyboard_shortcut_handler.cpp:78,129-133) change a parameter with no audible effect. The band stays fully distorted, and users cannot A/B a band. No processor-level bypass test exists; grep for kBandBypass in tests finds only encoding and keyboard tests.

**Evidence.** band_processor.h:521 `if (bypassed_) { return; // Leave input buffers unchanged }` (processBlock only); band_processor.h:473-512 process() has no bypassed_ reference; processor_params.cpp:927-930 `bandStates_[band].bypass = value >= 0.5; bandProcessors_[band].setBypassed(...)`; processor.cpp:563-576 shouldBandContribute reads only mute/solo.

**Corroborated by measurement.** probe bypass:all-bands: "max_abs_diff(on vs off),max_abs_diff(on vs dry) = 0,0.6292" (threshold "bypass must change output (FR-012)"). Code: band_processor.h:521 `if (bypassed_) {` exists only in processBlock; process() at :473-512 has no bypass check.

**Suggested fix.** In BandProcessor::process, when bypassed_ is set, skip sweep and morph and apply only gain/pan/mute (or nothing, per spec intent). Smooth the switch to avoid a click. Add a Processor test: 2 bands, HardClip at drive 10 on band 0, bypass band 0, feed a sine. Assert the output equals the crossover-reconstructed input within 1e-4 (before the fix, THD stays high).

**Verifier notes:**
- Correctness: setBypassed (band_processor.h:335-336) only stores the flag; Processor::process calls beginBlockModulation, the per-sample process() (processor.cpp:487/494) and endBlockModulation and never processBlock; bandStates_[].bypass is read only by state save/load (processor_state.cpp:70, 437); the control is wired via sub_controllers.h:89-90 and registered automatable (parameter_registration.cpp:670-672).
- Context: band_state.h:36 says "Bypass flag (FR-024, for future distortion)" but band_processor.h:333, specs-overview.md:174/:379 and roadmap.md:947 all describe bypass as shipped; setBypassed appears in tests only on the processBlock path (oversampling_*_tests.cpp), so they pass while the plugin path is broken.
- Impact: bypass changes the output by 0 dB in every host and setting, so a visible, automatable per-band control does nothing; STATE-COMPAT note for the fix: bypass is saved in state (processor_state.cpp:70, 423-437), so any saved project with bypass=1 changes sound once bypass starts working; high.

### D-086 — Crossover Hz<->normalized log10 mapping is copied in four places, and two controller paths call the linear RangeParameter toNormalized/toPlain instead

**Severity:** high | **Verdict:** CONFIRMED | **Category:** duplication | **Dimension:** duplication-drift | **Also flagged by:** D-051 (lifecycle-state) covers the preset-browser load consequence | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/controller.cpp:208-210](plugins/disrumpo/src/controller/controller.cpp#L208-L210), [:948-950](plugins/disrumpo/src/controller/controller.cpp#L948-L950)

**Problem.** The log10 crossover mapping (logMin = log10(kMinCrossoverHz) ...) is written separately in processor_params.cpp:988-991, visibility_controllers.cpp:643-647 (CrossoverDragBridge), crossover_network.h:259-260 and parameter_registration.cpp:145-149. No shared helper exists, so two controller sites fall back to the RangeParameter's linear toNormalized/toPlain. parseComponentState sends `param->toNormalized(freq)`, which is linear. createCustomView seeds the spectrum dividers with `crossoverParam->toPlain(crossoverParam->getNormalized())`, which is also linear.

**Why wrong.** The processor decodes crossover params as pow(10, logMin + v*(logMax-logMin)). Every writer must therefore use the same log encoding. The drag bridge does; the preset-load path and the divider seeding do not.

**Consequence.** Example: a preset stores crossover 1000 Hz. loadComponentStateWithNotify performEdits the linear norm (1000-20)/19980 = 0.049, and the processor decodes that as 10^(1.301+0.049*3) = ~28 Hz. Preset-browser loads put the crossovers in the wrong place. On editor open, the dividers are drawn at the linear interpretation of the stored normalized value, not where the audio splits.

**Evidence.** controller.cpp:210 `setter(param->getInfo().id, param->toNormalized(freq));`; :950 `float freq = static_cast<float>(crossoverParam->toPlain(crossoverParam->getNormalized()));`; visibility_controllers.cpp:643-647 `const float logMin = std::log10(kMinCrossoverHz); ... double normalized = (logFreq - logMin)/(logMax - logMin)`; processor_params.cpp:988-989 same log10 formula; parameter_registration.cpp:152-158 `new RangeParameter(..., kMinCrossoverHz, kMaxCrossoverHz, defaultFreq, ...)` (linear).

**Suggested fix.** Add one pair of inline helpers, crossoverHzToNorm and crossoverNormToHz, in band_state.h (DSP-side, so both processor and controller can include it). Replace all four copies with them and use them at controller.cpp:210 and :950. Test: parseComponentState of a stream with crossover 1000 Hz must produce norm = (3-1.301)/3 (about 0.566), and processParameterChanges with that norm must set crossoverL_.getCrossoverFrequency(0) to 1000 Hz +/-1 Hz.

**Verifier notes:**
- Correctness: the parameter is a linear RangeParameter(20, 20000) (parameter_registration.cpp:151-160), the processor decodes log10 (processor_params.cpp:984-993), and the preset-browser path controller.cpp:123 -> loadComponentStateWithNotify (1876-1891) -> editParamWithNotify (1868-1873) -> performEdit sends the linear 0.049 for 1000 Hz, decoded as about 28 Hz; the registered default is also linear-normalized, so a host "reset to default" on crossover 1 (about 112 Hz) decodes to about 20.6 Hz.
- Context: createComponentStateStream also uses the linear toPlain (controller.cpp:1695-1697, :1729), so saving after a drag stores the wrong Hz; the only test, crossover_interaction_test.cpp:283-309, reimplements its own log2 mapping and never exercises the controller's toNormalized; right after a plain host setComponentState the divider seeding round-trips consistently, so :950 is wrong only after a log-norm writer (drag or automation).
- Impact: a 1 kHz crossover reloads at about 28 Hz through the in-plugin browser (a 5-octave move), a browser-saved preset of a drag-set 1 kHz crossover stores about 11.3 kHz, and dividers draw at about 11.3 kHz after a drag to 1 kHz on reopen; tens of dB of band-balance change; high.

### D-085 — Stream layout v12 has three hand-written codecs with literal denominators; the preset writer has already drifted in three places and no test covers it

**Severity:** high | **Verdict:** CONFIRMED | **Category:** duplication | **Dimension:** duplication-drift | **Also flagged by:** D-052, D-053, D-048, D-117 are the individual drifts | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/controller.cpp:1699-1702](plugins/disrumpo/src/controller/controller.cpp#L1699-L1702), [:1748-1750](plugins/disrumpo/src/controller/controller.cpp#L1748-L1750), [:1815](plugins/disrumpo/src/controller/controller.cpp#L1815), [:1829-1834](plugins/disrumpo/src/controller/controller.cpp#L1829-L1834)

**Problem.** Processor::getState/setState (processor_state.cpp), Controller::parseComponentState (controller.cpp:150-617) and Controller::createComponentStateStream (controller.cpp:1687-1866) each re-encode the same positional stream by hand. No field table or schema is shared between them. The preset writer decodes list params through a lambda `getInt8FromList(id, maxVal)` that takes a magic denominator at every call site (5, 12, 25, 3, 2, kMorphLinkModeCount-1), when it could use the constants the other two codecs use.

**Why wrong.** Every encoder and decoder of one append-only format has to agree field by field. Because each copy carries its own literals, they have already drifted. (1) Routing source is written with denominator 12 (`getInt8FromList(makeRoutingParamId(r, 0), 12)`), but processor_params.cpp:555 and the parse path decode with kUIModSourceCount-1 = 13. (2) activeNodes is written as `round(norm*2)+2`, but the list has 4 entries (1..4, norm k/3). (3) The session writer stores morphMode and morphSmoothing as literals 0 and 0.0f (processor_state.cpp:319, :323), while the preset writer stores the real values (:1829, :1834). (4) The preset writer always writes the 2-point curve (0,0),(1,1) (:1748-1750), while getState writes the real breakpoints.

**Consequence.** User presets and host sessions of the same patch differ. A user preset reload moves routing sources 7..13 one slot down and turns 1-node bands into 2 and 2-node bands into 3. A host session reload drops morph mode and smoothing. grep finds createComponentStateStream only in controller.cpp/.h and in no test, so nothing catches the next drift.

**Evidence.** controller.cpp:1815 `streamer.writeInt8(getInt8FromList(makeRoutingParamId(r, 0), 12));`; :1830-1831 `int activeNodes = static_cast<int>(std::round(activeNodesNorm * 2.0f)) + 2;`; :1748-1750 `writeInt32(2); writeFloat(0.0f); writeFloat(0.0f); writeFloat(1.0f); writeFloat(1.0f);`; processor_state.cpp:319 `writeInt8(static_cast<Steinberg::int8>(0))  // morphMode`, :323 `writeFloat(0.0f)  // morphSmoothing (ms)`; grep -rln createComponentStateStream tests -> no hits.

**Suggested fix.** Fix the three drifts with the shared constants (kUIModSourceCount-1; activeNodes = round(norm*3)+1; write the cache's morphMode/smoothing in getState). Then add a round-trip test: set a non-default value for every parameter on the controller, run createComponentStateStream -> Processor::setState -> Processor::getState -> Controller::parseComponentState, and assert that every normalized value comes back within a list-step tolerance. Longer term, drive all three codecs from one constexpr field descriptor table.

**Verifier notes:**
- Correctness: all four drifts verified; round(k/13*12) gives k-1 for k = 7..13 and processor_state.cpp:768-770 reads the int8 raw; the 4-entry node list (parameter_registration.cpp:699-707) decodes count-1 = idx*3 (processor_params.cpp:949-952) while the writer maps 1->2, 2->3; the controller holds the real curve as kSweepCustomCurve* params (controller.cpp:1148-1177) but the writer still hard-codes two points; the literal denominators sit at lines 1740, 1754, 1770, 1774, 1777, 1811, 1815, 1829, 1838.
- Context: createComponentStateStream is the PresetManager state provider (controller.cpp:116-118), so every user preset save goes through it; factory presets come from the separate generator, so the defects hit user-saved presets and host sessions; no test references createComponentStateStream.
- Impact: Macro 3 and above reload one source lower, a band left at the default "1" saves as 2 nodes (an audible morph-in of node B), and every host session reload resets mode to 1D Linear with 0 ms smoothing; silent data change on default and common settings; high.

### D-074 — GranularDistortion derives a grain's state slot from its position in a rebuilt active list, so grain states are cross-wired and grains are skipped or double-processed when one retires (shared with Ruinae)

**Severity:** high | **Verdict:** CONFIRMED | **Category:** shared-dsp-contract | **Dimension:** shared-dsp-contracts | **Corroborated by measurement:** no | **CROSS-PLUGIN:** Ruinae (ruinae_voice.h includes GranularDistortion)

**Location:** [dsp/include/krate/dsp/processors/granular_distortion.h:394-396](dsp/include/krate/dsp/processors/granular_distortion.h#L394-L396), [:556-567](dsp/include/krate/dsp/processors/granular_distortion.h#L556-L567), [:616-628](dsp/include/krate/dsp/processors/granular_distortion.h#L616-L628)

**Problem.** triggerGrain and processGrain each find grainIndex by scanning grainPool_.activeGrains() for the grain's position, and index grainStates_/waveshapers_ with it. activeGrains() rebuilds activeList_ in place on every call (grain_pool.h:107-115). The process loop iterates a span over that same array while processGrain rebuilds it.

**Why wrong.** An index into per-grain state must be stable for the grain's lifetime: its slot in GrainPool::grains_, i.e. grain - grains_.data(). The active-list position shifts whenever a grain with a lower pool slot retires. Because processGrain rebuilds activeList_ while the outer range-for is still walking it, the iteration skips one grain and visits the last one twice once a grain is released mid-loop.

**Consequence.** After the first grain retires, every later grain reads another grain's startBufferPos, grainSizeSamples and waveshaper drive/type. The read position jumps mid-grain (clicks), and a newly triggered grain overwrites a live grain's state. In the sample where a grain ends, one grain is skipped (its envelope stalls) and the last grain is summed twice (level spike). This affects Disrumpo's Granular type and Ruinae (ruinae_voice.h includes GranularDistortion): CROSS-PLUGIN IMPACT.

**Evidence.** granular_distortion.h:559-566 `for (const Grain* g : grainPool_.activeGrains()) { if (g == grain) { grainIndex = idx; ...` ; :619-626 same in processGrain; :394-396 `for (Grain* grain : grainPool_.activeGrains()) { wet += processGrain(grain); }`; grain_pool.h:109-114 rebuilds activeList_ each call; :666-668 releaseGrain inside processGrain

**Suggested fix.** Expose a stable slot index from GrainPool (indexOf(grain) = grain - grains_.data()). Use it in triggerGrain/processGrain and stop calling activeGrains() inside processGrain. Iterate over a snapshot copy, or release grains after the loop. Test (dsp_processors_tests): with grain size 10 ms and density 8, record per-grain read positions; each grain's readPos must advance by exactly 1 per sample over its lifetime, and the count of processGrain calls per sample must equal the number of active grains.

**Verifier notes:**
- Correctness: activeGrains() (grain_pool.h:107-116) rewrites activeList_ in pool-slot order and leaves stale tail pointers; acquireGrain (grain_pool.h:62-68) reuses the lowest inactive slot so a new grain moves live grains up a position; with 4 active grains [g0..g3] and g0 released, g2 is skipped and g3 summed twice; the bypass loop at :371-373 and the dead helper getGrainIndex (:490-500) share the flaw.
- Context: Disrumpo maps density to [1,8] (distortion_adapter.cpp:879) and grain size 5-100 ms, so 4+ overlapping grains is a normal operating point; the comments at :488-500 ("calculate the index by processing order") and :552 ("awkward") describe a mistaken workaround, not a design decision; `% kMaxGrains` only bounds the index.
- Impact: the read position jumps by hundreds to thousands of samples mid-grain (a waveform discontinuity scaled by the grain envelope) and drive/type can change mid-grain; the one-sample double-sum spike is deterministic; bounded output, no NaN, so clicks/glitches rather than safety; high for a user-selectable type that clicks on sustained input.

### D-063 — After a node type change the restored shape slots reach only the proxies, never the node's real kNodeShape params, so the controller holds the old type's slot values

**Severity:** high | **Verdict:** CONFIRMED | **Category:** ui-wiring | **Dimension:** ui-wiring | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/visibility_controllers.cpp:318-322](plugins/disrumpo/src/controller/visibility_controllers.cpp#L318-L322), [:441-454](plugins/disrumpo/src/controller/visibility_controllers.cpp#L441-L454)

**Problem.** copyDisplayedTypeToSelectedNode runs inside update() with isUpdating_ = true. It edits the node type and then writes the new type's shadow slots to the ten proxy params with editParam. Each proxy change synchronously re-enters update(), because FObject::changed -> triggerUpdates is synchronous, and update() returns at the isUpdating_ guard, so copyProxyToSelectedNode never runs. The selected node's real kNodeShape0..9 params keep the previous type's values.

**Why wrong.** The proxy params are the UI view of the selected node. Every proxy edit must be mirrored into the real node param, which is what the controller serializes and what the host sees. The re-entrancy guard swallows the mirror for exactly the writes that most need it.

**Consequence.** Example: switch a node from Tube (slot0 = 0.8) to Fuzz. The UI shows Fuzz's restored or default slots (0.5); the processor uses its own shadow (0.5), which processor_params.cpp:844-852 loads independently; the controller's kNodeShape0 still says 0.8. A user preset saved now (controller.cpp:1851-1855) stores Tube's slot values under type Fuzz, and recalling it sounds different from what was saved. Host automation lanes also show stale values.

**Evidence.** visibility_controllers.cpp:318-320 `if (isUpdating_) { return; }` ; :445-453 `shapeShadowPtr_->load(newTypeIdx, newSlots); ... editParam(makeBandParamId(band_, kProxyIndexToBandParam[4 + s]), newSlots[s]);` while isUpdating_ is still true (cleared at :337) ; fobject.cpp:196-201 changed() -> triggerUpdates (synchronous)

**Suggested fix.** In copyDisplayedTypeToSelectedNode, also editParam the real node shape params (makeNodeParamId(band, node, kNodeShape0+s)) with newSlots, or temporarily clear isUpdating_ only for the mirror. Test: with the editor's NodeSelectionController active, change DisplayedType, then assert that each node kNodeShape param equals the proxy value.

**Verifier notes:**
- Correctness: editParam (:350-355) -> setParamNormalized -> Parameter::setNormalized (vstparameters.cpp:62-78) -> changed() -> gUpdateHandler->triggerUpdates (updatehandler.cpp:437-440), the synchronous path, so update() re-enters and returns at :318; the controller is a dependent of every proxy (:253-258); the processor treats the proxies as UI-only (processor_params.cpp:971-979) and loads its own shadow on kNodeType (:838-850).
- Context: the controller has no setParamNormalized/performEdit override, so no other path mirrors proxy to node; no test references NodeSelectionController or DisplayedType; a side observation: controller.cpp:1460 passes shapeShadowStorage_[b].data(), node 0's storage only (filed as D-064).
- Impact: host project save goes through processor getState (processor_state.cpp:344), which holds the right slots, so DAW recall is unaffected; the in-plugin preset save (controller.cpp:1851-1855) and reselecting the node (copySelectedNodeToProxies :372-389) carry the stale values; "pick a type, save a preset" is the main preset workflow; high.

### D-062 — Morph pad never follows its parameters, and Morph Y is sent with performEdit only, so the controller's Morph Y value goes stale and preset saves lose it

**Severity:** high | **Verdict:** CONFIRMED | **Category:** ui-wiring | **Dimension:** ui-wiring | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/views/morph_pad.cpp:646-649](plugins/disrumpo/src/controller/views/morph_pad.cpp#L646-L649), [:687-689](plugins/disrumpo/src/controller/views/morph_pad.cpp#L687-L689), [:746-748](plugins/disrumpo/src/controller/views/morph_pad.cpp#L746-L748), [:820-823](plugins/disrumpo/src/controller/views/morph_pad.cpp#L820-L823)

**Problem.** The MorphPad is bound to MorphX through VST3Editor (setListener(editor), tag = MorphX), but it draws the cursor from its private morphX_/morphY_ and does not override setValue. Host automation, preset load, the sweep morph link and undo therefore never move the cursor. MorphY is written with controller_->beginEdit/performEdit/endEdit and never with setParamNormalized, so the controller-side MorphY parameter keeps its old value.

**Why wrong.** EditController::performEdit only notifies the host; it does not change the controller's own Parameter value, and hosts do not echo the value back. Every other UI write path here (editParam, CrossoverDragBridge, VST3Editor's ParameterChangeListener::performEdit) calls setParamNormalized first. A bound control must also reflect parameter changes.

**Consequence.** (1) A user preset saved after a Y drag stores the stale Y: createComponentStateStream reads getParamNorm(kBandMorphY) at controller.cpp:1828, so the morph position is lost on save and recall. (2) Reopening the editor puts the cursor at the stale Y (controller.cpp:1016-1018). (3) MorphSweepLinkController reads the stale currentY (visibility_controllers.cpp:192). (4) Automated or preset-loaded morph positions are not displayed.

**Evidence.** morph_pad.cpp:687-689 `controller_->beginEdit(morphYParamId_); controller_->performEdit(morphYParamId_, static_cast<double>(morphY_));` (no setParamNormalized) ; morph_pad.cpp:478 and :568 draw from morphX_/morphY_ ; morph_pad.h overrides draw and the mouse handlers but not setValue ; controller.cpp:1004-1005 `morphPad->setTag(morphXParamId); morphPad->setListener(editor);`

**Suggested fix.** Call controller_->setParamNormalized(morphYParamId_, y) before performEdit (or use editParam). Make the pad an IDependent on its MorphX/MorphY parameters, or override setValue for X plus a dependent for Y, so that setMorphPosition follows parameter changes. Tests: drag the pad, then assert getParamNormalized(MorphY) equals the drag Y. Then setParamNormalized(MorphX/Y) and assert the pad's getMorphX/Y follow.

**Verifier notes:**
- Correctness: all four Y write sites (:646-649, :687-689, :746-748, :820-823) call begin/perform/endEdit only; controller_ is a plain EditControllerEx1* (morph_pad.cpp:71) and controller.h overrides neither performEdit nor setParamNormalized; the processor's only output parameters are sweep frequency and detected CC (processor.cpp:379-406); other write paths set the value first (visibility_controllers.cpp:198-208, controller.cpp:1180-1181/1871-1872, dynamic_node_selector.cpp:192-193, vst3editor.cpp:231-232).
- Context: the pad's only IDependent subscription is ActiveNodes (morph_pad.cpp:80-84, 143-145) and it never reads getValue(); the Band.MorphY CSlider (editor.uidesc:1272) does not move on a pad Y drag; morph_pad_test.cpp checks only the pad's local getters; host project save goes through processor getState, so only the in-plugin preset save, the display, the MorphY slider and the link controller are affected.
- Impact: Y spans the full node layout so a stale Y can recall a completely different node blend; silent loss of preset data on the plugin's central control plus a cursor that ignores automation; high.

### D-061 — compactNodeSelectors_ dangles after a Main/Shape tab switch; willClose then calls deactivate() on the freed view

**Severity:** high | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** ui-wiring | **Also flagged by:** portability-quality | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/controller.cpp:1078-1081](plugins/disrumpo/src/controller/controller.cpp#L1078-L1081), [:1546](plugins/disrumpo/src/controller/controller.cpp#L1546), [:1451](plugins/disrumpo/src/controller/controller.cpp#L1451)

**Problem.** createCustomView stores a raw pointer to the auto-hide DynamicNodeSelector in compactNodeSelectors_[band]. That selector lives in BandNShapeTab, which is a page of a UIViewSwitchContainer (animation-time 0), so switching back to Main destroys it. The pointer is never cleared, and willClose later calls dns->deactivate() on it, which runs isActive_.exchange on freed memory.

**Why wrong.** Views created inside a UIViewSwitchContainer page are destroyed on every page switch: removeAll() and addView of the new template, with no caching. A controller that caches such a view must clear the pointer when the view dies, through IViewListener::viewWillDelete or the view's destructor, or else must not cache it.

**Consequence.** Open the editor, click Shape on any band, click Main, then close the editor: willClose touches freed memory, a crash or heap corruption in normal use. The high-contrast loop at :1451 has the same hazard if the pages ever exist when it runs.

**Evidence.** controller.cpp:1079-1081 `if (autoHideStr && *autoHideStr == "true") { ... compactNodeSelectors_[bandIndex] = nodeSelector;` ; :1546 `for (auto& dns : compactNodeSelectors_) { if (dns) { dns->deactivate(); dns = nullptr; } }` ; editor.uidesc:1293 the auto-hide selector is in template BandShapeTab ; :543 `UIViewSwitchContainer ... animation-time="0" template-names="Band1MainTab,Band1ShapeTab"`

**Suggested fix.** Do not cache the compact selector. Its destructor already calls deactivate(). Alternatively have DynamicNodeSelector notify the controller from its destructor so the slot is nulled. Test under ASan: an editor lifecycle test that toggles BandTabView 0->1->0 and then closes the editor.

**Verifier notes:**
- Correctness: BandSubController::createView (sub_controllers.h:212-225) forwards to Controller::createView, which stores the raw pointer with no remember() and no view listener; UIViewSwitchContainer::setCurrentViewIndex (uiviewswitchcontainer.cpp:40-143) takes the removeAll()/addView branch at animationTime 0, deleting the old page, and createViewForIndex (:205-211) caches nothing; ~DynamicNodeSelector (dynamic_node_selector.cpp:48-56) never tells the controller; willClose's deactivate() runs isActive_.exchange (dynamic_node_selector.cpp:65) on freed memory.
- Context: compactNodeSelectors_ is touched only at controller.h:364, controller.cpp:1081, :1451 and :1546; the switch is driven by a user-facing Main/Shape CSegmentButton (uidesc :541 etc.); the only TabView test is audio-only (processor_audio_output_test.cpp:267); the :1451 loop runs in didOpen when the pointers are fresh or were nulled, so it is low-risk today.
- Impact: VST3Editor::close calls willClose before teardown (vst3editor.cpp:1377-1378), so the sequence open -> Shape -> Main -> close dereferences freed heap; if reused, the exchange writes a zero byte into a live object or calls removeDependent through a garbage pointer; ASan flags it every time, release builds see intermittent corruption or a crash on close; high.

### D-053 — User-preset writer scales the routing source by 12 instead of 13, so sources 7..13 are saved one slot lower

**Severity:** high | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** lifecycle-state | **Also flagged by:** ui-wiring, portability-quality | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/controller.cpp:1815](plugins/disrumpo/src/controller/controller.cpp#L1815)

**Problem.** The routing source param is a 14-entry list (kUIModSourceCount = 14, plugin_ids.h:546). The processor and the parser both use a denominator of 13 (processor_params.cpp:555; controller.cpp:468-469). The preset writer instead uses getInt8FromList(makeRoutingParamId(r,0), 12), i.e. round(k/13*12).

**Why wrong.** The encoder has to use the same (count-1) = 13 scale as the decoder.

**Consequence.** Indices 0..6 survive. Index 7 (Macro3) saves as 6 (Macro2), and indices 8..13 (Macro4, Chaos, Rungler, S&H, Pitch, Transient) each save one lower. Every user preset that routes one of those sources reloads modulated by the wrong source.

**Evidence.** controller.cpp:1815 `streamer.writeInt8(getInt8FromList(makeRoutingParamId(r, 0), 12));`; getInt8FromList = `std::round(param->getNormalized() * maxVal)` (:1700); parse :468-469 `/ static_cast<double>(kUIModSourceCount - 1)`.

**Suggested fix.** Use kUIModSourceCount - 1. Better, share one encode/decode helper between parse and write. Test: for every source index, set routing 0's source, call createComponentStateStream, parse the stream, and compare.

**Verifier notes:**
- Correctness: parameter_registration.cpp:563-579 builds a 14-string list (None..Transient), so normalized = k/13 and round(k*12/13) drops k = 7..13 by one while k = 6 survives (5.54 -> 6); processor_state.cpp:767-770 casts the int8 straight to ModSource, whose enum order (modulation_types.h:36-50) matches the UI list.
- Context: controller.cpp:116-124 wires createComponentStateStream as the PresetManager state provider and preset_manager.cpp:309/:361/:417/:449 use it whenever processor_ is null, so every editor-browser save goes through this writer; the other list writers in the same function use count-1 correctly (:1737, :1743), so 12 is an outlier; no test in plugins/disrumpo/tests uses makeRoutingParamId(r, 0).
- Impact: Transient reloads as Pitch, Chaos as Macro4 and so on, with the routing still active, so a different source modulates the destination silently; host session save (processor_state.cpp:300, raw source) and factory presets are unaffected; high.

### D-052 — User-preset writer encodes the active node count as round(norm*2)+2, so saved presets change 1->2 and 2->3 nodes

**Severity:** high | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** lifecycle-state | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/controller.cpp:1830-1832](plugins/disrumpo/src/controller/controller.cpp#L1830-L1832)

**Problem.** createComponentStateStream converts the kBandActiveNodes norm with round(norm*2)+2. The parameter is a 4-entry list (1..4, norm = (count-1)/3), as the parser and the processor assume (controller.cpp:526; processor_params.cpp:951-952).

**Why wrong.** The preset writer must invert the parser: count = round(norm*3)+1, which matches what processor getState writes (cache.activeNodeCount, processor_state.cpp:321).

**Consequence.** Every user preset saved from the browser changes node counts on reload. 1 node (the default) becomes 2, 2 nodes become 3 (norm 1/3 -> round(0.67)+2), 3 nodes become 3 (norm 2/3 -> round(1.33)+2), and 4 nodes stay 4. A single-node patch reloads as a two-node morph between A and B, which sounds different.

**Evidence.** controller.cpp:1830-1831 `float activeNodesNorm = getParamNorm(makeBandParamId(band, BandParamType::kBandActiveNodes)); int activeNodes = static_cast<int>(std::round(activeNodesNorm * 2.0f)) + 2;` vs controller.cpp:526 `setter(..., static_cast<double>(count - 1) / 3.0);`

**Suggested fix.** Write static_cast<int>(std::round(activeNodesNorm * 3.0f)) + 1. Test: for counts 1..4, set the param, call createComponentStateStream, run parseComponentState on a fresh controller, and assert the count matches.

**Verifier notes:**
- Correctness: the parameter is a 4-entry StringListParameter "1".."4" with default norm 0.0 (parameter_registration.cpp:699-706); the processor decodes idx = int(value*3+0.5), count = idx+1 (processor_params.cpp:951-952) and the parser stores (count-1)/3 (controller.cpp:525-526); kMinActiveNodes and kDefaultActiveNodes are both 1 (morph_node.h:132, 135); the writer maps 1->2, 2->3, 3->3, 4->4.
- Context: the stale comment at plugin_ids.h:185 ("[2, 3, 4]") and band_state.h:43 ("2-4") are leftovers from an older 3-entry scheme; the register/parse/processor paths all moved to 1..4 and only this writer did not; serialization_round_trip_test.cpp:796 and :1267 write kDefaultActiveNodes by hand and never call createComponentStateStream.
- Impact: the default 1-node patch reloads as a two-node A/B morph through the plugin's browser and loadComponentStateWithNotify (controller.cpp:1876-1888) pushes the wrong count to the processor too; audio changes wherever node B differs from A; host project save (processor getState) is correct; high.

### D-051 — Preset-browser load turns factory-preset crossover Hz into a linear normalized value that the processor decodes on a log scale, collapsing all crossovers below about 160 Hz

**Severity:** high | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** lifecycle-state | **Also flagged by:** ui-wiring; D-086 is the duplication view | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/controller.cpp:203-213](plugins/disrumpo/src/controller/controller.cpp#L203-L213)

**Problem.** parseComponentState converts the stream's crossover Hz with param->toNormalized(freq). The parameter is a linear RangeParameter 20..20000 (parameter_registration.cpp:151-160). loadComponentStateWithNotify passes that value to the processor through performEdit (controller.cpp:1868-1873, 1887-1889). The processor decodes crossover values as log10 (processor_params.cpp:988-991). Processor getState and the factory presets store real Hz (processor_state.cpp:75-77).

**Why wrong.** The load path must use the same normalization the processor decodes, which is log10. Session restore gives the processor Hz directly and is correct, so the two load paths disagree.

**Consequence.** Loading a factory preset (or any processor-written stream) through the plugin's own preset browser sends 200/1500/6000 Hz as norms 0.0090/0.0741/0.2993. The processor decodes these to about 21/33/158 Hz, so nearly the whole spectrum lands in the top band and the preset sounds nothing like itself. The divider UI, which is log-mapped, also shows the wrong positions after a host session restore. No test covers browser loads of crossover values (grep Crossover in tests/integration/preset gives one comment).

**Evidence.** controller.cpp:208-210 `auto* param = getParameterObject(makeCrossoverParamId(...)); if (param) setter(param->getInfo().id, param->toNormalized(freq));`; parameter_registration.cpp:151 `new Steinberg::Vst::RangeParameter(... kMinCrossoverHz, kMaxCrossoverHz, defaultFreq ...)`; processor_params.cpp:990-991 `logFreq = logMin + value*(logMax-logMin); freqHz = std::pow(10.0f, logFreq);`. Computed: 200 Hz -> norm 0.00901 -> 21.3 Hz; 1500 -> 0.0741 -> 33.4 Hz; 6000 -> 0.2993 -> 158 Hz.

**Suggested fix.** Give the crossover parameter (and sweep frequency and the LFO rates) a log-mapped Parameter subclass whose toPlain/toNormalized match the processor. Or, in parseComponentState, normalise with (log10(f)-log10(20))/3. Test: load a factory preset through loadComponentStateWithNotify into a controller/processor pair and assert that the processor's crossover frequencies equal the preset's Hz within 1 %.

**Verifier notes:**
- Correctness: the PresetManager is built with processor=nullptr (controller.cpp:113-114), so preset_manager.cpp:231/:264 use the load provider, which is loadComponentStateWithNotify (controller.cpp:120-123); editParamWithNotify (:1868-1873) does setParamNormalized then performEdit; the factory generator writes raw Hz (tools/disrumpo_preset_generator.cpp:283-285, 855-930); the arithmetic (200 Hz -> 21.3 Hz) checks out; CrossoverDragBridge (visibility_controllers.cpp:643-653) normalizes on a log scale, so the controller disagrees with itself.
- Context: nothing on the path converts, editParamWithNotify only clamps to [0,1]; no spec or comment marks the mapping as intentional; only crossover_interaction_test.cpp and parameter_encoding_test.cpp reference makeCrossoverParamId and neither covers a browser load; createComponentStateStream also uses the linear toPlain (:1695-1696, :1729).
- Impact: every multi-band factory preset loaded through the plugin's own browser gets its crossovers squeezed below about 160 Hz; crossover_network.h:243-248 clamps only to 20 Hz, so the collapse stands; the controller stores the linear norm while the divider view maps log (visibility_controllers.cpp:643-646); high.

### D-050 — Host-path setState never re-derives the type-specific DistortionParams from the restored shape slots

**Severity:** high | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** lifecycle-state | **Also flagged by:** D-156 is the audible worst case; D-160 explains the probe baseline | **Corroborated by measurement:** yes (preset probe Rectified_Drums -62.22 dBFS)

**Location:** [plugins/disrumpo/src/processor/processor_state.cpp:852-895](plugins/disrumpo/src/processor/processor_state.cpp#L852-L895)

**Problem.** setState restores each node's type, drive, mix, tone, bias, folds, bitDepth, its 10 shapeSlots and the shadow. It then pushes the nodes with setMorphNodes. It never calls mapShapeSlotsToParams, a file-static function in processor_params.cpp:39 that is called only from processParameterChanges (lines 852, 892). Every other DistortionParams field (threshold, curve, sag, satStage, tape model, feedback, delayMs, vowel, fftSize, chaos and granular settings, ...) keeps whatever mn.params held before: defaults on a fresh instance, or the PREVIOUS patch's values on a live instance.

**Why wrong.** setState has to produce the same DSP state as replaying the parameters, which is the path loadComponentStateWithNotify takes. The shape slots are the persisted source of truth for those fields.

**Consequence.** Loading a host-side preset (DAW preset menu, project undo, copy/paste of plugin settings) onto a running instance restores the node types with the previous patch's per-type parameters, or with defaults. The patch sounds wrong until the user touches a shape knob of each node, which then makes the sound jump. On a fresh project restore this is hidden by the setupProcessing reset finding (D-046), but it applies as soon as that is fixed.

**Evidence.** grep mapShapeSlotsToParams: only processor_params.cpp:39 (definition), :852, :892; processor_state.cpp:871 `bandMorphCache_[b].nodes[...].shapeSlots[s] = slotValue;` followed by :894 `bandProcessors_[b].setMorphNodes(...)` with no slot-to-param mapping.

**Corroborated by measurement.** probe preset levels: silent/near-silent: "Drums/Rectified_Drums -62.22 (bad, < -60)". Generator: tools/disrumpo_preset_generator.cpp:2630-2631 `setBandType(p.state.bandMorph[1], DistortionType::HalfRectify, 2.0f, 4500.0f); shapeHalfRect(..., 0.3f, 0.15f, true);`. The processor_params.cpp:124 slot map is `p.threshold = slots[0];`, but grep finds mapShapeSlotsToParams only at processor_params.cpp:852 and :892, never in processor_state.cpp. distortion_adapter.cpp:441 `float result = (input >= thresh) ? input : 0.0f;`

**Suggested fix.** Move mapShapeSlotsToParams into a shared header or a Processor member. In setState, call it for each node after the slots are read: reset mn.params to DistortionParams{} first, re-apply bias/folds/bitDepth, then map the slots. Test: load stream A (Tube with non-default slots) into an active processor and render; load stream B (Tape) and then stream A again; the render must match the first render within a tolerance.

**Verifier notes:**
- Correctness: processor_state.cpp:852-862 restores only type/common/bias/folds/bitDepth, :865-874 writes raw shapeSlots and :878-888 the shadow, :892-898 pushes the nodes and returns at :919; nothing in dsp/ reads shapeSlots (morph_node.h:43 is only the default initializer); the type-specific fields (curve/knee, threshold, sag/satStage, tape fields, feedback/delayMs, chaos, per processor_params.cpp:41-229) therefore keep stale or default values.
- Context: setActive (processor.cpp:144-178) only resets DSP state; Controller::setComponentState uses setParamNormalized, not performEdit (controller.cpp:147-148, :619), so the host path never replays the slots; only the plugin's own browser (loadComponentStateWithNotify -> editParamWithNotify -> performEdit at :1872) does; no test pins setState-to-params equivalence (multi_node_distortion_test.cpp:673 and distortion_adapter_test.cpp:888-904 cover other paths).
- Impact: the editor shows the restored slot values while the audio uses stale or default per-type settings (feedback amount, tape model, tube stage, vowel), re-syncing only when a knob moves; almost every patch beyond default SoftClip is affected on every host's restore path; high.

### D-049 — setState does not restore the sweep width/intensity base atomics, so loaded width and intensity are overwritten on the next block

**Severity:** high | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** lifecycle-state | **Also flagged by:** D-041 (sweep-modulation) reports the same defect | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor_state.cpp:485-488](plugins/disrumpo/src/processor/processor_state.cpp#L485-L488)

**Problem.** setState calls only sweepProcessor_.setWidth() and setIntensity(). process() overwrites both every block from baseSweepWidthNorm_ and baseSweepIntensityNorm_ (processor.cpp:276-290). Those atomics are written only by processParameterChanges (processor_params.cpp:396, 404), and their defaults are 0.286 and 0.25 (processor.h:227, 230). By contrast, baseSweepFrequency_ is updated in setState (line 482).

**Why wrong.** The base value that process() reads is the parameter's source of truth. setState must write it, as it already does for the frequency.

**Consequence.** After a project reload, the sweep width runs at 0.5 + 0.286*3.5 = 1.5 oct and the intensity at 0.5 (50 % on the DSP's 0-200 % scale), whatever was saved. The controller shows the saved values. The next getState saves the defaults, which makes the loss permanent.

**Evidence.** processor_state.cpp:485-488 `if (streamer.readFloat(sweepWidthNorm)) sweepProcessor_.setWidth(0.5f + sweepWidthNorm * 3.5f); if (streamer.readFloat(sweepIntensityNorm)) sweepProcessor_.setIntensity(sweepIntensityNorm * 2.0f);` vs processor.cpp:277-282 `const float baseWidthNorm = baseSweepWidthNorm_.load(...); ... sweepProcessor_.setWidth(kMinWidth + modWidthNorm * (kMaxWidth - kMinWidth));`; grep: baseSweepWidthNorm_ written only at processor_params.cpp:396.

**Suggested fix.** In setState, store sweepWidthNorm into baseSweepWidthNorm_ and sweepIntensityNorm into baseSweepIntensityNorm_ alongside the setter calls. Test: build a v12 stream with width 3 oct and intensity 150 %, call setState, process one block, then assert sweepProcessor_.getWidth()==3 and getIntensity()==1.5 and that getState returns the same normalized values.

**Verifier notes:**
- Correctness: grep finds the two atomics written only at processor_params.cpp:396/:404 and loaded at processor.cpp:277/:286; the block at processor.cpp:276-290 runs unconditionally and calls setWidth/setIntensity every block, so the first block after setState replaces the restored values; getState (processor_state.cpp:89, :91) serializes from sweepProcessor_, so a later save persists the defaults.
- Context: no setActive reset or reload of the atomics exists, setComponentState only updates the controller, and VST3 hosts do not re-send parameters after setState; state_persistence_test.cpp checks sweep width/intensity only as MIDI-mapping/macro targets, and sweep_processor_tests.cpp tests the SweepProcessor alone; no setState -> process -> getState test exists.
- Impact: a saved 3 oct / 150 % sweep plays at 1.5 oct / 50 % after reload while the knobs show the saved values, and the next save writes the defaults; the same path runs for host-loaded .vstpreset files; trigger needs Sweep enabled with non-default values; high.

### D-048 — Processor getState writes literal 0 for per-band Morph Mode and Morph Smoothing

**Severity:** high | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** lifecycle-state | **Also flagged by:** D-114 (test gap), D-099, D-132 | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor_state.cpp:319-324](plugins/disrumpo/src/processor/processor_state.cpp#L319-L324)

**Problem.** getState writes `writeInt8(0) // morphMode` and `writeFloat(0.0f) // morphSmoothing (ms)` for every band. The real values set through kBandMorphMode and kBandMorphSmoothing (processor_params.cpp:958-969) are never written. setState then applies them: setMorphMode(0) and setMorphSmoothingTime(0) (lines 827-831). Controller::parseComponentState pushes 0 to the UI params (controller.cpp:522-529).

**Why wrong.** The host session format must round-trip every persistent parameter. The preset writer (controller.cpp:1829, 1834) writes the real mode and smoothing into the same layout, so the two writers disagree on identical fields.

**Consequence.** After any project save and reload, every band reverts to 1D Linear morph and 0 ms morph smoothing, which is lower than the engine's 10 ms default. Users who chose 2D Planar or Radial lose it, and morph moves click because smoothing is off. The UI shows the reverted values, so the loss is not obvious.

**Evidence.** processor_state.cpp:319 `if (!streamer.writeInt8(static_cast<Steinberg::int8>(0)))  // morphMode`; :323 `if (!streamer.writeFloat(0.0f))  // morphSmoothing (ms)`; :827-831 `bandProcessors_[b].setMorphMode(...morphMode...); ... bandProcessors_[b].setMorphSmoothingTime(morphSmoothing);`; controller.cpp:1829,1834 preset writer writes `getInt8FromList(...kBandMorphMode, 2)` and `smoothingNorm * 500.0f`.

**Suggested fix.** Cache morphMode and morphSmoothingMs in BandMorphCache. Write them in processParameterChanges and setState, and serialise the cached values in getState. Test: set kBandMorphMode = 2D Radial and smoothing = 200 ms on band 1, call getState, call setState on a fresh processor, then assert the engine's mode and smoothing and the bytes at the morphMode and smoothing offsets.

**Verifier notes:**
- Correctness: nothing in the processor stores these values (processor_params.cpp:958-969 sends them straight to bandProcessors_ and BandMorphCache has no field for them); setState applies the zeros (processor_state.cpp:813-831) and the controller pushes them to kBandMorphMode/kBandMorphSmoothing (controller.cpp:513-529); Controller::getState/setState (controller.cpp:658-701) save only a version, two unused doubles and MIDI mappings.
- Context: both params are registered automatable and user-facing (parameter_registration.cpp:687-693 "1D Linear/2D Planar/2D Radial", :717-720 0-500 ms) and appear in editor.uidesc (:1326, :1330); the controller's preset writer (controller.cpp:1829, 1833-1834) writes the real values, so the zeros are a defect not a format choice; one correction: the registered smoothing default is 0 ms (parameter_registration.cpp:719), so losing smoothing matters only to users who raised it.
- Impact: the mode changes the weight computation (morph_engine.cpp:179-247, Linear1D X-only vs Planar vs Radial) so a 2D band sounds different after every DAW session reload in every host; smoothing loss causes zipper/clicks only when the cursor moves; high.

### D-041 — Host session restore loses Sweep Width and Intensity (setState does not update the base atomics)

**Severity:** high | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** sweep-modulation | **Also flagged by:** D-049 (lifecycle-state) reports the same defect | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor_state.cpp:480-486](plugins/disrumpo/src/processor/processor_state.cpp#L480-L486)

**Problem.** setState applies the stream's width and intensity only to sweepProcessor_ (setWidth/setIntensity). It never stores them in baseSweepWidthNorm_ / baseSweepIntensityNorm_. Processor::process overwrites sweepProcessor_ width and intensity every block from those atomics (processor.cpp:276-290). The frequency is handled correctly (baseSweepFrequency_.store at :482).

**Why wrong.** State restore must leave the processor producing the saved patch. The per-block path treats the base atomics as the source of truth, so setState has to write them, as it does for frequency.

**Consequence.** After reopening a project, Sweep Width snaps back to 1.5 oct (0.286 norm) and Intensity to 50 % (0.25 norm) on the first processed block, whatever was saved, while the controller shows the saved values. The patch sounds different until the user touches those knobs. Preset-browser loads are unaffected because they resend every parameter.

**Evidence.** processor_state.cpp:483-486 `if (streamer.readFloat(sweepWidthNorm)) sweepProcessor_.setWidth(0.5f + sweepWidthNorm * 3.5f); if (streamer.readFloat(sweepIntensityNorm)) sweepProcessor_.setIntensity(sweepIntensityNorm * 2.0f);` vs :480-482 `baseSweepFrequency_.store(freqHz, ...)`. processor.cpp:277-289 `const float baseWidthNorm = baseSweepWidthNorm_.load(...); ... sweepProcessor_.setWidth(kMinWidth + modWidthNorm * (kMaxWidth - kMinWidth)); ... const float baseIntNorm = baseSweepIntensityNorm_.load(...); ... sweepProcessor_.setIntensity(modIntNorm * 2.0f);`.

**Suggested fix.** In setState, store sweepWidthNorm and sweepIntensityNorm in baseSweepWidthNorm_ and baseSweepIntensityNorm_ when read. Test: set width norm 0.9 and intensity norm 0.8 via params, run one block, getState. Create a new processor, setState, run one block, and assert sweepProcessor_ width == 0.5+0.9*3.5 and intensity == 1.6 (or compare sweep band intensities).

**Verifier notes:**
- Correctness: only processParameterChanges (processor_params.cpp:396, :404) writes the two atomics, which start at 0.286f and 0.25f (processor.h:227, :230); processor.cpp:277-289 rewrites width and intensity every block with no condition, so the first block after restore resets them; the frequency path (:482) shows the omission is an oversight.
- Context: the block has no gate on a modulation routing being active (it follows the I/O checks at :200-216); getState serializes from sweepProcessor_.getWidth()/getIntensity() (processor_state.cpp:89, :91), so a save after any processed block writes the defaults into the project file for good; a round-trip test with no process() call between setState and getState would pass and hide the bug.
- Impact: the same path runs when a host loads a .vstpreset through its own browser; width/intensity are audible only with Sweep enabled, where they change how many bands the sweep drives and how hard; silent loss on re-save supports high.


### D-039 — Sweep-morph link is not in the DSP: works only with the editor open and ignores sweep LFO/envelope/mod motion

**Severity:** high | **Verdict:** CONFIRMED | **Category:** sweep | **Dimension:** sweep-modulation | **Also flagged by:** ui-wiring, spec-conformance; D-137 is the controller-side view | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/visibility_controllers.cpp:148-210](plugins/disrumpo/src/controller/visibility_controllers.cpp#L148-L210)

**Problem.** The processor never applies a sweep-morph link. SweepProcessor::getMorphPosition (sweep_processor.h:297) has no caller. The global kSweepMorphLink only sets a field, kSweepCustomCurveP* are no-ops, and kBandMorphXLink/YLink are explicitly 'UI-only ... no processor action' (processor_params.cpp:971-978). The only working link is the controller's MorphSweepLinkController. It is created in Controller::didOpen (controller.cpp:1454-1456), observes only the BASE kSweepFrequency parameter, and writes kBandMorphX/Y with setParamNormalized + performEdit from IDependent::update.

**Why wrong.** FR-SWEEP-003 / spec item 'Morph position can be linked to sweep frequency' describes a DSP behaviour, and a processor must work without its controller (CLAUDE.md VST3 architecture). Sweep automation sources include the internal LFO and the envelope follower (FR-SWEEP-004). Their motion exists only in the processor (modulatedFreq), so a UI-thread link on the base parameter cannot follow them. A controller performEdit issued in response to host-driven parameter changes, with no beginEdit, also creates automation feedback.

**Consequence.** With the editor closed (offline bounce, render in place, most sessions) the morph link does nothing, so renders differ from what was heard with the GUI open. With the editor open, the sweep LFO, envelope follower and mod-matrix sweep routes never move the morph cursor, only the base frequency knob does. During host playback of sweep-frequency automation the controller emits MorphX/Y edits, which hosts in write/touch/latch mode record as new automation. The global 'Sweep Morph Link' and custom curve parameters have no audible effect at all.

**Evidence.** visibility_controllers.cpp:154-158 `float sweepNorm = static_cast<float>(sweepFreqParam_->getNormalized()); for (int band = 0; band < 8; ++band) updateBandMorphFromSweep(...)`; :199-208 `controller_->setParamNormalized(...kBandMorphX, newX); controller_->performEdit(...kBandMorphX, newX);` (no beginEdit). controller.cpp:1354 `void Controller::didOpen`, :1454-1456 `morphSweepLinkController_ = new MorphSweepLinkController(this, sweepFreqParam);`. processor_params.cpp:971-978 `case BandParamType::kBandMorphXLink: case BandParamType::kBandMorphYLink: ... // UI-only params (sweep-morph link ...): no processor action`. grep: getMorphPosition() is referenced only at sweep_processor.h:297.

**Suggested fix.** Implement the link in Processor::process. After the sweep smoother runs, compute per-band link positions from normalizeSweepFrequency(smoothed modulatedFreq) using each band's X/Y link mode, which the processor must then consume. Apply them as the morph-position base before the mod-engine offsets. Remove the controller-side performEdit path. Test (processor level, no controller): band 0 X link = Sweep Freq, sweep LFO on. Render and assert that the band's morph position (via a test accessor, or an audible morph between two contrasting nodes) follows the LFO.

**Verifier notes:**
- Correctness: kSweepMorphLink only calls sweepProcessor_.setMorphLinkMode (processor_params.cpp:410-414); getMorphPosition is called only from tests/dsp/sweep_processor_tests.cpp:334-425; the custom-curve point params do `(void)pointCount` and `break` (processor_params.cpp:495-521) and customCurve_ changes only in setState (processor_state.cpp:552-569); the controller link observes only the kSweepFrequency Parameter object (visibility_controllers.cpp:120-130, 154-158).
- Context: specs-overview.md:287-297 (FR-SWEEP-003/004), roadmap.md M5 and plans-overview IT-006 describe the link as a DSP milestone; the controller-only link has no processor-level test; the missing beginEdit/endEdit is a VST3 protocol violation that fires on host automation of Sweep Frequency.
- Impact: the per-band link menus (editor.uidesc:1353/1355) and the global SweepMorphLink tag (:49) are user-reachable, so live playback with the editor open differs from an offline render, LFO/envelope-driven sweeps never morph, and the host can record automation it never asked for; high.

### D-037 — Sweep band intensities use hard-coded 100/600/3000/12000 Hz centres, not the real bands (1-band mode goes silent)

**Severity:** high | **Verdict:** CONFIRMED | **Category:** sweep | **Dimension:** sweep-modulation | **Also flagged by:** duplication-drift, spec-conformance; D-019/D-038 cover the input-multiply side | **Corroborated by measurement:** yes (probe `sweep:off-centre-band-level`)

**Location:** [plugins/disrumpo/src/processor/processor.cpp:421-435](plugins/disrumpo/src/processor/processor.cpp#L421-L435)

**Problem.** Per-band sweep intensity is computed against the fixed `kBandCenterFreqs = {100, 600, 3000, 12000}`, taking the first numBands entries. The actual crossover frequencies and the band count are ignored.

**Why wrong.** FR-SWEEP-001 says sweep focus moves through the spectrum relative to the bands that exist. A band's distance from the sweep centre must come from that band's own frequency range: for example the geometric centre of its crossover edges, with the full 20 Hz-20 kHz range in 1-band mode. A fixed 4-band table is only correct for one specific crossover layout.

**Consequence.** In 1-band mode the full-range band is evaluated at 100 Hz. With the default sweep (1 kHz, width 1.5 oct, intensity 0.5) that gives 0.5*exp(-0.5*(3.32/0.75)^2) = 2.8e-5 (-91 dB). Since the sweep multiplies the band input, turning Sweep on mutes the wet output. In 2-band mode (crossover at 632 Hz) the upper band, 632 Hz-20 kHz, is evaluated at 600 Hz, below its own range. Moving crossovers in the UI has no effect on where the sweep 'hits'. Sweeping to 5 kHz in a 4-band layout with crossovers at 200/1500/6000 Hz still favours the band labelled '3000 Hz'.

**Evidence.** processor.cpp:422-430 `static constexpr std::array<float, kMaxBands> kBandCenterFreqs = { 100.0f, 600.0f, 3000.0f, 12000.0f }; ... sweepProcessor_.calculateAllBandIntensities(kBandCenterFreqs.data(), numBands, sweepIntensities.data());`. Defaults: processor.h:224/227/230 baseSweepFrequency_ 1000, width norm 0.286 (1.5 oct), intensity norm 0.25 (0.5). crossover_network.h:253-265 log distribution gives a 632 Hz crossover for 2 bands.

**Suggested fix.** Derive each band's centre from crossoverL_ frequencies: sqrt(f_lo*f_hi) with edges 20 Hz and 20 kHz, and use the full range for 1 band. Pass that array to calculateAllBandIntensities. Test: 1 band, sweep enabled at default settings, sine at 1 kHz. Assert the output RMS is within 6 dB of the sweep-disabled level (today -91 dB). Also: 4 bands with moved crossovers, sweep centred on band 2's geometric centre, and assert band 2 gets the maximum intensity.

**Verifier notes:**
- Correctness: sweep_processor.h:263-285 evaluates the Gaussian (sweep_morph_link.h:143-165, sigma = width/2) against the table entries as given; with the defaults (processor.h:224/227/230, processor_params.cpp:397/405) a 1-band distance of 3.32 oct gives about 2.8e-5, and in Sharp mode exactly 0; band_processor.h:196-198/478-481 apply it as a gain on the band input, so the wet path drops to about -91 dB.
- Context: the comment at :422 says "log-spaced for 4-band Bark scale", which assumes the 4-band layout; nothing in specs/Disrumpo documents a fixed table; sweep_band_intensity_test.cpp uses its own 8-entry centre array and calls SweepProcessor directly, so the processor wiring is untested; Sweep is opt-in, which limits exposure.
- Impact: enabling Sweep at defaults in 1-band mode mutes the wet output (dry 0 at mix 1, processor.cpp:502), and in every other layout the sweep targets the wrong bands and moving crossovers changes nothing; high.

### D-036 — Sweep envelope follower is fed one sample per block (inputL[0] only)

**Severity:** high | **Verdict:** CONFIRMED | **Category:** sweep | **Dimension:** sweep-modulation | **Also flagged by:** spec-conformance | **Corroborated by measurement:** yes (probe `sweep:env-attack`)

**Location:** [plugins/disrumpo/src/processor/processor.cpp:339-343](plugins/disrumpo/src/processor/processor.cpp#L339-L343)

**Problem.** The sweep envelope follower is updated once per block with the first sample of the block: `float inputMono = (inputL[0] + inputR[0]) * 0.5f; (void)sweepEnvelope_.processSample(inputMono);`. The rest of the block is never seen.

**Why wrong.** SweepEnvelope wraps Krate::DSP::EnvelopeFollower (RMS mode). Its attack/release coefficients are per-sample one-poles prepared for sampleRate (sweep_envelope.h prepare/setAttackTime). FR-026/027 attack 1-100 ms and release 10-500 ms assume one update per sample. Sampling one value per block also decimates the audio with no anti-aliasing: a 1 kHz tone sampled every 512 samples gives a pseudo-random value.

**Consequence.** Attack and release are stretched by the block size: 10 ms attack becomes about 5 s at 512 samples. The detected level is a sparse, aliased snapshot of sample 0, not the signal's RMS. So the envelope-driven sweep (FR-SWEEP-004 'Envelope follower') responds sluggishly and erratically, and its response depends on the host buffer size.

**Evidence.** processor.cpp:340-343 `if (sweepEnvelope_.isEnabled()) { float inputMono = (inputL[0] + inputR[0]) * 0.5f; (void)sweepEnvelope_.processSample(inputMono); }`. sweep_envelope.h processSample: `envelopeLevel_ = envelope_.processSample(input);` (one follower tick per call); prepare sets DetectionMode::RMS and setAttackTime(attackMs_).

**Corroborated by measurement.** probe sweep:env-attack: "final_shift_oct 0.911, time_to_63pct_ms 320.0" (threshold "~10 ms (block resolution 10.7 ms)"). Code: processor.cpp:341 `float inputMono = (inputL[0] + inputR[0]) * 0.5f;`

**Suggested fix.** Loop sweepEnvelope_.processSample over all numSamples of (L+R)/2 and use the final level, as ModulationEngine does for its env follower (processEnvFollowerSample per sample, modulation_engine.h:224). Test: feed a 0.5-amplitude burst after silence with attack 10 ms. Assert that the reported modulated sweep frequency rises by at least 90 % of its final octave shift within 50 ms, at both 64- and 1024-sample blocks.

**Verifier notes:**
- Correctness: no other per-sample call to sweepEnvelope_.processSample exists (other uses are prepare at processor.cpp:136 with the audio rate, reset :161, setters processor_params.cpp:468-489, getModulatedFrequency :359); SweepEnvelope::processSample (sweep_envelope.h:187-194) ticks EnvelopeFollower once, whose coefficient is exp(-2pi/(timeMs*0.001*sampleRate)) (envelope_follower.h:359-365), so a 10 ms attack takes about 441 blocks (5.1 s at 512).
- Context: nothing documents block-rate behaviour (no comment in sweep_envelope.h or processor.cpp, no spec text), the FR-026/027 ranges in sweep_envelope.h:29-39 assume per-sample times; sweep_envelope_tests.cpp drives the class sample by sample and never exercises the processor call site; the envelope is opt-in (enabled_ false by default).
- Impact: 10 ms attack becomes about 0.64 s at 64, 5.1 s at 512 and 10.2 s at 1024-sample blocks; a 100 ms release becomes about 51 s at 512; tones whose period divides the block length give a fixed-phase snapshot (possibly 0 at a zero crossing); the feature is effectively broken and host-dependent; high.

### D-035 — Sweep LFO runs once per block, so its rate is divided by the host block size

**Severity:** high | **Verdict:** CONFIRMED | **Category:** sweep | **Dimension:** sweep-modulation | **Corroborated by measurement:** yes (probe `sweep:lfo-rate`)

**Location:** [plugins/disrumpo/src/processor/processor.cpp:349-355](plugins/disrumpo/src/processor/processor.cpp#L349-L355)

**Problem.** Processor::process calls sweepLFO_.process() once per audio block. SweepLFO::process() (sweep_lfo.h) returns lfo_.process()*depth_. Krate::DSP::LFO::process advances phaseAcc_ by one per-sample increment (calculatePhaseIncrement(freq, sampleRate), lfo.h:499-502) on each call. Over a whole block the LFO phase therefore moves one sample's worth.

**Why wrong.** FR-SWEEP-004 / FR-024 set the internal sweep LFO to 0.01-20 Hz. That requires one phase increment per sample, or numSamples increments per block. The shared ModulationEngine does this correctly: it ticks lfo1_/lfo2_ once per sample in its per-block loop (modulation_engine.h:216-221). The sweep LFO does not.

**Consequence.** The real sweep LFO rate is the set rate divided by the block size. At 512 samples / 44.1 kHz a 1 Hz sweep LFO has a period of about 512 s, and 20 Hz becomes about 0.04 Hz. In practice the sweep LFO is frozen. Its speed also changes with the host buffer size, so the same project sounds different at 128 and at 1024 samples. S&H and Random waveforms step once every ~blockSize*sr/rate samples.

**Evidence.** processor.cpp:349-354 `if (sweepLFO_.isEnabled()) { float lfoValue = sweepLFO_.process(); ... modulatedFreq = std::pow(2.0f, log2Freq); }` (no loop over numSamples). sweep_lfo.h process(): `return lfo_.process() * depth_;`. lfo.h:499-502 `phaseAcc_.increment = calculatePhaseIncrement(freq, sampleRate_)`. Compare modulation_engine.h:216-221 `for (size_t i = 0; i < safeSamples; ++i) { ... lfo1LastValue_ = lfo1_.process();`. tests/dsp/sweep_lfo_tests.cpp:150-190 tests only the wrapper called per sample; no processor-level sweep LFO test exists (grep for kSweepLFO in tests/ finds only dsp/sweep_lfo_tests.cpp).

**Corroborated by measurement.** probe sweep:lfo-rate (5 Hz, depth 1, block 512): "mod_range_oct 0.244, upward_crossings_in_2s 0, blocks 188" (expected about 4 oct and about 10 cycles). Code: processor.cpp:350 `float lfoValue = sweepLFO_.process();` inside the per-block section.

**Suggested fix.** Advance the sweep LFO numSamples times per block and use the last value. Or add an LFO advance(n) / per-sample sweep evaluation. Test (processor level, new [sweep] case): enable sweep and the sweep LFO at 1 Hz with depth 1. Render 2 s at block sizes 64 and 1024. Read kSweepModulatedFrequencyOutputId per block and assert about 2 periods (4 zero crossings of log2(f/base)) at both block sizes. Today the 1024-sample render shows none.

**Verifier notes:**
- Correctness: SweepLFO::process (sweep_lfo.h:201-206) calls lfo_.process() once, which advances phaseAcc_ once (lfo.h:159) with a per-sample increment (lfo.h:501); sweepLFO_.prepare(sampleRate_) at processor.cpp:135 passes the audio rate, so nothing compensates; S&H and SmoothRandom change value only on phase wrap (lfo.h:162-169) and step at the same divided rate.
- Context: every setRate site (processor_params.cpp:435, processor_state.cpp:509) passes plain Hz; the comment at :335 says the sweep is processed "for the entire block"; the only test (tests/dsp/sweep_lfo_tests.cpp) drives the wrapper per sample; the envelope follower at :340-343 has the same once-per-block pattern (D-036).
- Impact: the 20 Hz maximum (sweep_lfo.h:34) gives about a 25.6 s period at 512, the 1 Hz default about 512 s, and the 0.01 Hz minimum about 14 h; tempo sync goes through the same lfo_ (sweep_lfo.h:132-154) and is wrong by the same factor; a user-facing modulation source is effectively frozen; high.

### D-028 — Same-family morph is a hard switch of the dominant node's type through ONE processor; Hybrid/Experimental are not blended in parallel as the spec requires

**Severity:** high | **Verdict:** CONFIRMED | **Category:** morph | **Dimension:** morph-oversampling | **Also flagged by:** spec-conformance | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/morph_engine.cpp:691-719](plugins/disrumpo/src/dsp/morph_engine.cpp#L691-L719), [:675-682](plugins/disrumpo/src/dsp/morph_engine.cpp#L675-L682)

**Problem.** processSameFamily picks the highest-weight node, calls blendedAdapter_.setType(dominant.type) every sample and runs a single processor with interpolated params. When the dominant node changes (2-node case: cursor crosses the 50% point), the transfer function switches instantly, e.g. SoftClip tanh-knee to HardClip linear-gain-plus-clip, Tube to Fuzz, or SineFold to SergeFold model. The processor that becomes active inside blendedAdapter_ has not run since its last use, so its smoothers, DC blocker, hysteresis, feedback and grain state are stale. This applies to all 7 families, including Hybrid (RingSat+AllpassResonant) and Experimental (Chaos..Stochastic).

**Why wrong.** FR-MORPH-004/006 specify 'transfer function interpolation' for Saturation and 'Parallel blend with output crossfade' for Hybrid and Experimental (specs-overview.md:265-271; distortion_types.h:279-280 repeats it). Swapping the processor at a weight threshold is neither: it is a discontinuity. FR-MORPH-003 also requires 'artifact-free transitions at all speeds'.

**Consequence.** Moving the morph cursor between two same-family nodes of different types produces a click and a timbre and level jump at the midpoint. For Hybrid/Experimental, unrelated processors (Chaos vs Spectral, RingSat vs Allpass) share interpolated fields and swap at 50%, which also triggers the Spectral re-prepare storm described separately (D-001).

**Evidence.** morph_engine.cpp:696-706 `for (...) if (weights_[i] > maxWeight) dominantNode = i; ... blendedAdapter_.setType(nodes_[dominantNode].type);`; :678-681 cross-family path only when !allSameFamily_; distortion_adapter.cpp:142-162 setType does no reset; distortion_types.h:279-280 `Hybrid ... Parallel blend with output crossfade` / `Experimental ... Parallel blend with output crossfade`; distortion_types.h:300-306 SoftClip..AsymmetricFuzz all Saturation.

**Suggested fix.** Use the single-processor interpolation path only when all active nodes have the SAME DistortionType. Different types, even in the same family, should go through the parallel per-node adapters with a continuous gain law. For Saturation, true transfer-function interpolation means running both shapers and blending. Test: a 2-node same-family morph (SoftClip to HardClip, and Chaos to Formant) swept slowly through 0.5 on a sine, asserting max sample-to-sample derivative and per-window level change stay under a click bound.

**Verifier notes:**
- Correctness: isSameFamily (morph_engine.cpp:385-397) compares families only; processSameFamily (:696-706) picks the strict-max node and calls setType every sample; DistortionAdapter::setType (distortion_adapter.cpp:142-162) only sets currentType_, the DC-blocker state and latency flags, so the newly active processor resumes from stale state; the approval test (morph_artifact_test.cpp:457-459) relaxes the SC-002 bound to 3 dB "for the same-family case", accommodating the discontinuity.
- Context: specs-overview.md:270-271 (FR-MORPH-006) and distortion_types.h:279-280/:332/:337 say Hybrid and Experimental use a parallel blend, which the code does not do; dsp-details.md:1244-1251 does say same-family uses one instance with interpolated parameters but also "transfer function coefficients interpolated directly", not a dominant-type swap; AP-003 only sweeps SoftClip -> Tube with loose bounds (up to 5 click detections, derivative < 0.5).
- Impact: SoftClip (distortion_adapter.cpp:261-284, never reaches 1.0) switching to HardClip (:287+, flat clamp) steps the output by a few tenths at full scale in one sample at mix 1; factory presets ship same-family A/B morphs (generator :914 Tube->Fuzz, :1034 Tape->Tube, :1062/:1240 SoftClip->HardClip, :1157 Tape->Fuzz) and periodic morph modulation repeats the click; high.

### D-027 — Intelligent oversampling never runs: the processor calls only per-sample BandProcessor::process, and processBlock would gate itself to 1x anyway

**Severity:** high | **Verdict:** CONFIRMED | **Category:** oversampling | **Dimension:** morph-oversampling | **Also flagged by:** D-017 (signal-path), D-116 (test gap), gestalt-processor | **Corroborated by measurement:** yes (probes `os-engaged:*` and the per-type aliasing set)

**Location:** [plugins/disrumpo/src/processor/processor.cpp:487](plugins/disrumpo/src/processor/processor.cpp#L487), [:494](plugins/disrumpo/src/processor/processor.cpp#L494)

**Problem.** Processor::process runs its own per-sample loop and calls bandProcessors_[b].process(L,R), which processes at the host rate. BandProcessor::processBlock, processWithFactor, the 2x/4x/8x oversamplers and the 8 ms factor crossfade are never invoked from src. Even if processBlock were wired in, processWithFactor takes the 1x branch whenever distortion_.getCommonParams().drive < 0.0001. distortion_ (the legacy adapter) is set to drive 0 in prepare and the processor never configures it, because morph is always enabled.

**Why wrong.** FR-OS-001..003 (spec 009) require per-type 2x/4x oversampling of the shapers, selected from morph weights and limited by the global Oversample Max parameter. getRecommendedOversample asks for 4x for HardClip, Fuzz, AsymFuzz, the folds, rectifiers, RingSat and AllpassResonant (distortion_types.h:170-180). All of them run at 1x, so their harmonics alias freely. kOversampleMaxId is consumed (processor_params.cpp:349-361) but has no audible effect. recalculateOversampleFactor still runs every block via setMorphPosition, and can leave crossfadeActive_ true forever because nothing advances the crossfade.

**Consequence.** Audible inharmonic aliasing from every hard or folding shaper at normal drive (e.g. HardClip or SineFold on a 5 kHz tone at 44.1 kHz), a dead user-facing Oversample control, and wasted per-block factor computation. The tests that exercise oversampling call BandProcessor::processBlock directly, so they pass while the plugin never uses that path.

**Evidence.** processor.cpp:487 `bandProcessors_[b].process(discardL, discardR);` :494 `bandProcessors_[b].process(bandL, bandR);` (grep: no processBlock call in src/processor); band_processor.h:629-635 `const float drive = distortion_.getCommonParams().drive; if (bypassDistortion || factor == 1) { ... process(left[i], right[i]); }`; band_processor.h:115-119 `defaultParams.drive = 0.0f; distortion_.setCommonParams(defaultParams);`; processor.cpp:125 setMorphEnabled(true).

**Corroborated by measurement.** probe os-engaged:processor:HardClip: nonharm_dB@1x,@8x,reduction = "-43.3,-43.3,0.0", band_factor@8x 4. The control os-engaged:bandprocessor-block:HardClip: "-48.8,-67.2,18.4". type:SineFold/TriangleFold drive=1.00 nonharm -3.1 dB; type:Tape -23.6/-25.2/-24.5; type:Fractal -35.2/-27.9/-24.2; suspicious set HardClip -49.2/-43.3, SoftClip -43.0/-43.4, AllpassResonant -46.0/-41.1, FullRectify -42.6, HalfRectify -31.4/-41.0/-42.9, RingSaturation -33.7/-34.3/-34.7, Granular 0.25 -19.7 (threshold <= -50 dB). Code: processor.cpp:494 `bandProcessors_[b].process(bandL, bandR);`; the oversampling lives in processBlock (band_processor.h:519+).

**Suggested fix.** Restructure Processor::process to split the crossover into per-band block buffers, call bandProcessors_[b].processBlock(bandL, bandR, n) (chunked to maxBlockSize), then sum. Replace the distortion_.drive gate in processWithFactor with a morph-aware check, or remove it when morphEnabled_. Report latency (0 for IIR) and keep the crossfade working. Test: a processor-level [.][audit-probe] render of HardClip at drive 8 on a 5 kHz sine at 44.1 kHz, asserting alias energy below the harmonics with Oversample Max 4x is at least 20 dB lower than with 1x.

**Verifier notes:**
- Correctness: the only processBlock in processor.cpp is sweepProcessor_.processBlock (:370); the oversamplers are reachable only via processBlock -> processBlockInner (:548) -> processWithFactor (:627) -> processWithOversampling2x/4x/8x (:709+); the drive gate at :629-636 reads distortion_, set to 0 in prepare (:114-119) and never configured because setDistortionCommonParams has no caller in src/processor; startCrossfade sets crossfadeActive_ (:706) and only processBlockWithCrossfade clears it (:679), so the latch claim holds.
- Context: "FR-001a: sample-by-sample processing" appears only as a comment at processor.cpp:444 and nowhere in specs/Disrumpo; CHANGELOG.md:121 advertises "Intelligent oversampling for alias-free distortion"; the MorphEngine is prepared at sampleRate*8 (band_processor.h:124) but run at the host rate, so its rate-dependent internals are also mis-tuned (D-009).
- Impact: HardClip (distortion_adapter.cpp:287-306) is a naive clip with no ADAA, so a 5 kHz sine at 44.1 kHz folds its 5th harmonic to 19.1 kHz at about -14 dB and its 7th to 9.1 kHz at about -17 dB, both inharmonic, at default settings in every host up to 96 kHz; the Oversample Max control is dead; high.

### D-022 — Crossover frequencies are silently dropped when set while the network has fewer bands (state load and parameter ordering)

**Severity:** high | **Verdict:** CONFIRMED | **Category:** signal-path | **Dimension:** signal-path | **Also flagged by:** spec-conformance; D-054 and D-124 cover the same ordering | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/crossover_network.h:127-130](plugins/disrumpo/src/dsp/crossover_network.h#L127-L130), [:274-297](plugins/disrumpo/src/dsp/crossover_network.h#L274-L297)

**Problem.** setCrossoverFrequency returns early when `index >= numBands_ - 1`, using the CURRENT band count, and does not even store the value in crossoverFrequencies_. setState applies the stream's crossovers (processor_state.cpp:452-457) BEFORE `setBandCount(bandCount)` (:462-463). processParameterChanges handles kBandCountId (processor_params.cpp:339-346) and crossover IDs (:984-994) in whatever queue order the host delivers within one block. Then setBandCount from <=1 band reinitialises a log distribution (:280-284), and from k>1 inserts log midpoints (redistributeIncreasing), overwriting the intended values.

**Why wrong.** Loading a 4-band state or preset into an instance currently at 1-2 bands must restore all three crossover frequencies. Stored frequencies should be accepted for every index < kMaxBands-1 and kept for later use (FR-011a 'preserve existing positions'). The order of parameter queues inside a block is host-defined, so processing must not depend on it.

**Consequence.** Session restore or preset browse from a low-band-count state gives wrong band splits (e.g. 112/632/3556 log defaults or log midpoints instead of the saved values). The controller still displays the saved Hz, so UI and audio disagree until the user drags each crossover.

**Evidence.** crossover_network.h:128 `if (index < 0 || index >= numBands_ - 1) { return; }` (before :133 store); processor_state.cpp:455-456 setCrossoverFrequency then :462-463 setBandCount; processor_params.cpp:344-345 and :992-993; crossover_network.h:280-283 `if (oldBandCount <= 1) { initializeLogarithmicDistribution(); ...}`

**Suggested fix.** In setCrossoverFrequency, always store crossoverFrequencies_[index] (bounds-check against kMaxBands-1) and retarget crossovers_[index] and the allpasses regardless of numBands_. In redistributeCrossovers, preserve already-stored valid values instead of regenerating them. In setState, call setBandCount before the crossover loop. Test: prepare at 2 bands, then setState with a v12 stream at 4 bands and crossovers 150/900/5000 Hz. Assert getCrossoverFrequency(0..2) == 150/900/5000 within 0.1 Hz. Repeat with the parameter path delivering crossover queues before the band-count queue in one block.

**Verifier notes:**
- Correctness: only the atomic bandCount_ is updated early (processor_state.cpp:413); the networks' numBands_ changes at :462, so indices above the live count are dropped and redistributeIncreasing (:301-358) fills them with log midpoints, or initializeLogarithmicDistribution overwrites everything from 1 band; setupProcessing's prepare() (processor.cpp:100-102 -> crossover_network.h:89-90) also discards restored crossovers, a separate mechanism with the same symptom.
- Context: a fresh instance starts at 4 bands (kDefaultBands, crossover_network.h:48, band_state.h:62), so a first load works; setActive only calls reset(); crossover_network_test.cpp covers 2->4 preservation at the current count (:375-391) and only -1/10 as invalid indices (:393-399); no test loads state into a lower-band instance; FR-011a (crossover_network.h:109, :272) contradicts the behaviour.
- Impact (dissent on severity): the drop needs setState into an instance currently at fewer bands (host-side .vstpreset load, A/B compare, undo after lowering the band count); the parameter-ordering half is mostly theoretical because the browser path performEdits band count first and its ID is lower; this verifier recommended medium, the other two kept high; high retained.

### D-020 — Band Bypass is a no-op: the live per-sample path never reads bypassed_

**Severity:** high | **Verdict:** CONFIRMED | **Category:** signal-path | **Dimension:** signal-path | **Also flagged by:** D-095 (spec-conformance) | **Corroborated by measurement:** yes (probe `bypass:all-bands`, see D-095)

**Location:** [plugins/disrumpo/src/dsp/band_processor.h:335-337](plugins/disrumpo/src/dsp/band_processor.h#L335-L337), [:473-512](plugins/disrumpo/src/dsp/band_processor.h#L473-L512), [:519-523](plugins/disrumpo/src/dsp/band_processor.h#L519-L523)

**Problem.** setBypassed stores bypassed_ (called from processor_params.cpp:929), but the flag is read only in processBlock (:521), which the plugin never calls. process(float&, float&), the function Processor::process uses, applies sweep, morph and gain/pan/mute unconditionally. setState restores bs.bypass into bandStates_ without even calling setBypassed (processor_state.cpp:437-442).

**Why wrong.** FR-012 / FR-BAND-004: 'When bypassed, band output is bit-identical to input (no processing)'. The editor exposes a per-band Bypass toggle (editor.uidesc:1264, control-tag Band.Bypass), and the keyboard handler's Space toggles bypass, so the user sees a control that does nothing.

**Consequence.** Pressing Bypass on a band leaves it fully distorted, so A/B comparison per band is impossible and automation of bypass has no effect.

**Evidence.** band_processor.h:473-512 process() has no `bypassed_` reference; :521 `if (bypassed_) { return; }` only in processBlock; processor_params.cpp:927-929 `bandProcessors_[band].setBypassed(bandStates_[band].bypass);`; editor.uidesc:1264 Band.Bypass ToggleButton.

**Suggested fix.** In process(): `if (bypassed_) return;` before the sweep multiply. Ideally crossfade into bypass over ~10 ms with a smoother to avoid a click; at minimum keep the crossover band signal unmodified. Also call setBypassed in setState and setupProcessing. Test: processor-level, 1 band, HardClip drive 10, set kBandBypass = 1. Assert the output equals the input sample for sample at mix 1 (allowing for the crossover passthrough at 1 band).

**Verifier notes:**
- Correctness: the only read of bypassed_ is processBlock (band_processor.h:521); the per-sample path (:473-512) runs sweep (:481-482), morph/distortion (:485-496) and gain/pan/mute (:510-511) every time; shouldBandContribute (processor.cpp:563-576) checks only mute and solo; the control is wired (editor.uidesc:1264, :1350 via sub_controllers.h:89-90) and registered automatable (parameter_registration.cpp:636-672).
- Context: band_state.h:36 says "for future distortion" but band_processor.h:333 and the specs (specs-overview.md:174, :379, roadmap.md:947) treat bypass as shipped; setBypassed appears in tests only on the processBlock path (oversampling_single_type_tests.cpp:186/212, oversampling_performance_tests.cpp:226); setState (processor_state.cpp:423-437) never calls setBypassed.
- Impact: any band with drive above 0.0001 or morph on stays fully distorted when bypassed in every host, and host automation has no effect; a primary per-band control does nothing; high.

### D-019 — Sweep multiplies band INPUT level, silencing bands outside the sweep window; intensity > 100 % is clamped away

**Severity:** high | **Verdict:** CONFIRMED | **Category:** sweep | **Dimension:** signal-path | **Also flagged by:** sweep-modulation, gestalt-processor; D-038 (spec-conformance) is PLAUSIBLE on the same mechanism | **Corroborated by measurement:** yes (probe `sweep:band-intensity:intensity=200%` and `sweep:off-centre-band-level`)

**Location:** [plugins/disrumpo/src/dsp/band_processor.h:196-199](plugins/disrumpo/src/dsp/band_processor.h#L196-L199), [:480-482](plugins/disrumpo/src/dsp/band_processor.h#L480-L482)

**Problem.** setSweepIntensity clamps to [0,1], and process() does `left *= sweep; right *= sweep;` before the morph engine. Nothing undoes this level change after distortion. processor.cpp:426-435 feeds the Gaussian/linear falloff `intensityParam * falloff` (sweep_morph_link.h:150-205), evaluated against hard-coded band centres {100, 600, 3000, 12000} Hz (processor.cpp:422-424) regardless of real crossover frequencies or band count.

**Why wrong.** FR-SWEEP-001 defines sweep as 'focused distortion INTENSITY moving through the spectrum', and FR-SWEEP-002 gives intensity 0-200 %. Bands far from the sweep centre should get less distortion, not disappear. Scaling the input by intensity changes the band's output level by the same factor (for low-drive types) or simply mutes it. Values above 1.0 (101-200 %) can never take effect because of the clamp.

**Consequence.** With the default width (1.5 oct, sigma 0.75) and centre 1 kHz, the 12 kHz band gets exp(-0.5*(3.585/0.75)^2) = 1.1e-5 (-99 dB) and the 100 Hz band 5.5e-5. Enabling sweep on a 4-band patch removes the lows and highs and drops the mids by 10-25 dB, and the sweep sounds like a moving band-pass filter. The upper half of the intensity range is dead. Band 0 of a 2-band split is evaluated at 100 Hz even when the crossover sits at 5 kHz.

**Evidence.** band_processor.h:197 `targetSweep_ = std::clamp(intensity, 0.0f, 1.0f);`; :481-482 `left *= sweep; right *= sweep;`; processor.cpp:422-434 kBandCenterFreqs and setSweepIntensity; sweep_morph_link.h `return intensityParam * falloff;` (Gaussian and linear); specs-overview.md:275-284.

**Corroborated by measurement.** probe sweep:band-intensity:intensity=200%: "max_band_multiplier 2.0000" with note "BandProcessor::setSweepIntensity clamps to 1 (band_processor.h:197), so 100-200 % is inert". Code: band_processor.h:197 `targetSweep_ = std::clamp(intensity, 0.0f, 1.0f);`

**Suggested fix.** Apply the sweep intensity to the drive (or to per-band wet/dry mix), not to the band signal level. For example, pass a sweep drive multiplier into MorphEngine alongside the drive mod offset, keep the band level untouched, and allow 0..2. Derive band centres from the actual crossover edges (geometric mean of neighbouring edges, with 20 Hz / 20 kHz for the outer bands). Test: sweep enabled, all nodes at drive 0 (passthrough), 4 bands, pink noise in. Assert the output spectrum is within 1 dB of the input in every band. Today the outer bands are ~-90 dB.

**Verifier notes:**
- Correctness: setIntensity accepts 0..kMaxIntensity = 2.0 (sweep_processor.h:55, 139-140) and processor_params.cpp:405-406 passes norm*2, so values up to 2.0 really arrive and the clamp at band_processor.h:196-198 discards them; the multiply happens on both the 1x path (:478-482) and the oversampled path (:744-749), and applyOutputStage (:502-511) only applies gain/pan/mute; kDefaultIntensity = 0.5 (sweep_processor.h:58) means even the centre band loses 6 dB with sweep on.
- Context: the retired spec (007-sweep-system/spec.md:194-195, :236, :378) says intensity 0 % means "no additional distortion applied beyond base band settings" and 200 % "doubled intensity at center", and BandProcessor should scale "distortion intensity"; plans-overview.md:78 draws "Sweep Intensity Multiply" at band input but does not call for muting; the tests (sweep_band_intensity_test.cpp:190-215, morph_band_integration_test.cpp:157-168) pin the attenuation without covering values above 1 or real crossovers.
- Impact: at defaults the per-band multipliers are 600 Hz about -10 dB, 3 kHz about -25 dB, 100 Hz about -92 dB and 12 kHz about -105 dB, so a headline feature acts as a moving band-pass level cut and the upper half of the intensity range does nothing, in every host; high.

### D-018 — Dry path is not phase-compensated: global mix < 100 % notches every crossover frequency

**Severity:** high | **Verdict:** CONFIRMED | **Category:** signal-path | **Dimension:** signal-path | **Also flagged by:** spec-conformance | **Corroborated by measurement:** yes (probe `drywet-mix50`)

**Location:** [plugins/disrumpo/src/processor/processor.cpp:502-503](plugins/disrumpo/src/processor/processor.cpp#L502-L503)

**Problem.** Output = `inputL[n] * dryMix + sumL * outputGainLinear * wetMix`. The wet sum of an N-band LR4 cascade with D'Appolito compensation equals AP(f0)*AP(f1)*...*AP(fN-2)*x, a chain of 2nd-order allpasses (LP_LR4 + HP_LR4 = (s^2 - sqrt2 s + 1)/(s^2 + sqrt2 s + 1)) with -180 degrees at each crossover. The dry signal is the raw input with no matching allpass.

**Why wrong.** Mixing a signal with an allpass-rotated copy of itself is a phaser: at each crossover fc, H = (1-m) - m*g with g the wet linear gain. With the default center-pan coefficient g = 0.707, H reaches a full null at m = 0.586 and is -15 dB below the passband at m = 0.5. With g = 1 (unity wet), m = 0.5 nulls completely. A parallel dry/wet mix must time- and phase-align dry to wet. The usual fix runs dry through the same allpass chain, or takes dry from the crossover sum.

**Consequence.** Any global mix setting other than 0 or 100 % carves deep notches at every crossover frequency (1-3 notches depending on band count) even when distortion is light. Automating mix sweeps a phaser-like colour. This is the main use of a parallel distortion mix.

**Evidence.** processor.cpp:502 `outputL[n] = inputL[n] * dryMix + sumL * outputGainLinear * wetMix;`; crossover_network.h:211-227 cascade + allpasses_ only on the band outputs; :55 kAllpassQ = 0.7071 (2nd-order allpass); band_processor.h:502-511 center pan coefficient cos(pi/4).

**Corroborated by measurement.** probe drywet-mix50:bands=2/4: "deepest_notch_dB -16.66 @ 114 Hz / -16.61 @ 2000 Hz" (threshold ">= -1 dB"). Code: processor.cpp:502 `outputL[n] = inputL[n] * dryMix + sumL * outputGainLinear * wetMix;`

**Suggested fix.** Add per-channel 'dry allpass' compensation to CrossoverNetwork, e.g. `float processDryCompensation(float x)` applying AP(f0)..AP(fN-2) with coefficients kept in sync with the crossover frequencies and band count, and use its output as the dry term. Alternatively, define dry as the sum of the unprocessed bands. Test: 4 bands at default crossovers, every node drive 0 (adapter passthrough), band pan 0, mix 0.5. Feed sines at each crossover frequency and at 1 kHz between crossovers; assert |out|/|in| is within 0.5 dB of the 1 kHz value. Today it is ~15 dB down.

**Verifier notes:**
- Correctness: the dry term is the raw host input, not even multiplied by inputGainLinear (applied only to inL at :469); crossover_filter.h:314-322 makes true LR4 with no polarity inversion and the D'Appolito allpasses (crossover_network.h:222-227, Q 0.7071 at :55) sit only on bands 0..N-2, so the summed bands are AP(f0)...AP(fN-2); at m = 0.5 with g = 0.707 the crossover output is 0.146x against 0.854x in the passband, about -15 dB, matching the finding; 1-band mode (:195-197) is a pure passthrough so the defect needs 2+ bands (default 4).
- Context: no spec note on dry-path phase/allpass alignment exists in specs/Disrumpo (Global Mix is defined only as 0-100 %, default 100 %); global_controls_test.cpp checks only the param ID; mix is user- and host-automatable and modulatable (processor.cpp:264 ModDest::kGlobalMix); any oversampling latency would add comb filtering on top.
- Impact: the default is 100 % and every factory preset uses globalMix = 1.0 (disrumpo_preset_generator.cpp:238), so default sound is unaffected, but anyone turning the parallel mix below 100 % gets a 15 dB+ (up to infinite) notch at every crossover in every host, and mix automation becomes a phaser sweep; high.

### D-017 — Intelligent oversampling, Oversample-Max parameter and the 8 ms factor crossfade never run: all shapers alias at 1x

**Severity:** high | **Verdict:** CONFIRMED | **Category:** oversampling | **Dimension:** signal-path | **Also flagged by:** signal-path, spec-conformance, gestalt-processor; same defect as D-027 from the morph-oversampling dimension | **Corroborated by measurement:** yes (see D-027)

**Location:** [plugins/disrumpo/src/processor/processor.cpp:481-499](plugins/disrumpo/src/processor/processor.cpp#L481-L499)

**Problem.** Processor::process runs a per-sample loop that calls BandProcessor::process(l, r). BandProcessor::processBlock, the only entry to processWithFactor, oversampler2x_/4x_/8xInner_, processBlockWithCrossfade and the bypass check, has no caller in plugins/disrumpo/src (grep: only its own definition). recalculateOversampleFactor still runs on every setMorphPosition, once per block per band (band_processor.h:270-278), and setMaxOversampleFactor is applied from kOversampleMaxId (processor_params.cpp:349), but both only update unused state.

**Why wrong.** FR-OS-001..003 / spec 009 require each band to run its shapers at the morph-weighted recommended factor (4x for HardClip, Fuzz, folds and rectifiers; 2x for most others) under a user global limit. Without that, the harmonics these nonlinearities generate above Nyquist fold back. Even if processBlock were called, processWithFactor (band_processor.h:629-632) takes the 1x branch whenever distortion_.getCommonParams().drive < 1e-4. The legacy distortion_ drive is 0 by default (:116) and is never configured while morph is enabled, so the oversampled branch would still never run.

**Consequence.** Aliasing at full strength for every distortion type at any drive. Oversample Max is a visible, automatable parameter with no audible effect. The oversampling tests (tests/dsp/oversampling_*.cpp) pass because they call processBlock directly, a path the plugin never takes.

**Evidence.** processor.cpp:487 `bandProcessors_[b].process(discardL, discardR);` and :494 `bandProcessors_[b].process(bandL, bandR);`; grep `processBlock(` in src/processor finds only `sweepProcessor_.processBlock` (processor.cpp:370); band_processor.h:519 processBlock definition; :629-632 `const bool bypassDistortion = (drive < 0.0001f); if (bypassDistortion || factor == 1)`; :116 `defaultParams.drive = 0.0f;`

**Suggested fix.** Restructure Processor::process to split the block into per-band buffers (crossover per sample into bandBufL/R[b][n]), then call bandProcessors_[b].processBlock(bufL, bufR, n) per band and sum. Base the drive gate in processWithFactor on the morph engine when morph is enabled. Fix the shared-engine and 8x-prepare findings in the same change. Test at processor level: 1 band, HardClip drive 8, 15 kHz sine at 44.1 kHz. Measure the alias bins (non-harmonic) and assert they are >= 20 dB lower with Oversample Max 4x than with 1x. Today the two runs are identical.

**Verifier notes:**
- Correctness: a grep for processBlock(/processWithFactor across plugins/disrumpo/src finds BandProcessor::processBlock only as its definition (band_processor.h:519) and processWithFactor only called from inside band_processor.h (:556, :659, :663, :687); the per-sample process() (:473-512) calls morphEngine_->process (:487-488) or distortion_.process (:493-494) with no oversampler; MorphEngine::process and DistortionAdapter::process add no internal oversampling; setDistortionCommonParams is never called from src/processor, so the drive gate would bypass oversampling even if processBlock were wired.
- Context: no spec note disables oversampling; "FR-001a" (processor.cpp:444) is not defined in specs/Disrumpo; CHANGELOG.md:121 claims the feature; the tests that cover oversampling call processBlock directly and do not pin the plugin's real path.
- Impact: the shapers do not band-limit themselves (HardClip at distortion_adapter.cpp:287-300 is a naive clip, no ADAA anywhere in the adapter), and the only post-shaper filter is a one-pole tone LP that cannot remove folded aliases; the adapters are also prepared at sampleRate*8 but run at 1x so filter cutoffs are about 8x too low (D-009); core quality defect of a distortion plugin, every preset, every host; high.

### D-010 — Quantize (D14) turns silence into a DC offset, up to +1.0 full-scale at Levels = 0, with no DC blocker

**Severity:** high | **Verdict:** CONFIRMED | **Category:** dsp-numerics | **Dimension:** dsp-numerics | **Also flagged by:** shared-dsp-contracts; D-143 is the measured view | **Corroborated by measurement:** yes (probe `selfnoise:Quantize` -49.0 dBFS, see D-143)

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.cpp:478-518](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L478-L518) (DC flag at 586-592)

**Problem.** Quantize maps x to index=(x+1)/step with step=2/(N-1). For x=0 that is (N-1)/2. When N-1 is odd or non-integer (any continuous Levels value except a few), round() moves the index to a level that does not decode back to 0. At Levels=0, N=2, index 0.5 rounds to 1, and the output is exactly +1.0. quantOffset adds a further index shift that is not undone symmetrically. updateDCBlockerState excludes Quantize, and the tone filter is skipped for it.

**Why wrong.** A quantizer used as a distortion should be mid-tread (0 maps to 0), or its DC must be blocked. The adapter already DC-blocks the other asymmetric types. A silent input must not produce a DC or full-scale output.

**Consequence.** A Quantize node with Levels at 0 outputs a constant +1.0 (0 dBFS DC times mix and band gain) on silence, and a ±1 square wave on noise-floor input. Other knob positions leave DC of up to 0.23 (Levels 0.01), 0.11 (0.1), or 0.38 with Offset 0.5. The DC reaches the output, upsets downstream dynamics and speakers, and shows up as a band level jump when the node is morphed in.

**Evidence.** distortion_adapter.cpp:482-486 `numLevels = 2 + quantLevels*62; stepSize = 2/(numLevels-1); index = (input + 1.0f) / stepSize;`, :505 `index = std::round(index) - typeParams_.quantOffset;`, :510 `quantized = index * stepSize - 1.0f`. :586-591 needsDCBlock_ is set only for AsymmetricFuzz, FullRectify, HalfRectify and FeedbackDist. :225-229 tone is skipped for Quantize. processor_params.cpp:147 p.quantLevels = slots[0]. Computed (float32) silence output: lv 0 -> 1.0000, lv 0.01 -> 0.2346, lv 0.1 -> 0.1111; with offset 0.5, lv 0.01 -> -0.3827. Tests (distortion_adapter_test.cpp:2545, 2576) never feed silence.

**Suggested fix.** Use a mid-tread grid (round x/step with step=2/(N-1) centred on 0, or force odd N), or add Quantize (and offset>0) to needsDCBlock_. Test: an adapter of type Quantize with quantLevels in {0, 0.01, 0.1, 0.5} and offset in {0, 0.5}, fed 4096 zeros; assert |mean output| < 1e-4.

**Verifier notes:**
- Correctness: at Levels = 0, N = 2, step = 2, index = 0.5, std::round(0.5) = 1 and the clamp keeps 1, so the output is exactly +1.0; any |x| < 1 gives a ±1 square; Levels 0.01 gives about 0.2346 DC; the default quantLevels = 0.5 (distortion_adapter.h:116) gives N = 33 and index exactly 16, so the default patch is clean and the fault needs a non-default knob position.
- Context: nothing downstream removes the DC (needsDCBlock_ excludes Quantize at :586-591, :701 is rectify-only, tone skipped at :225-229, no other DC blocker or silence gate in plugins/disrumpo/src); the only acknowledgement is a test comment at distortion_adapter_test.cpp:2566 ("may round either way") that skips zero rather than pinning a decision; processor_params.cpp:147 reaches Levels = 0 from a real host and the morph blends quantLevels continuously (morph_engine.cpp:560).
- Impact: exact-zero input survives the crossover and drive*0 is 0 (:211), so a band with a Quantize node at minimum Levels outputs a constant DC of mix x band gain (up to 0 dBFS) on a silent transport, a speaker and downstream-dynamics hazard that also appears as a level step when the node morphs in; high.

### D-009 — Adapter tone filter, DC blocker and morph smoothers use coefficients for 8x the sample rate but run at 1x (tone 4 kHz acts as about 500 Hz, max 8 kHz as about 1 kHz)

**Severity:** high | **Verdict:** CONFIRMED | **Category:** dsp-numerics | **Dimension:** dsp-numerics | **Also flagged by:** signal-path, morph-oversampling, shared-dsp-contracts, spec-conformance | **Corroborated by measurement:** yes (probes `tone-response` and `adapter-vs-processor`)

**Location:** [plugins/disrumpo/src/dsp/band_processor.h:111-112](plugins/disrumpo/src/dsp/band_processor.h#L111-L112), [:124-125](plugins/disrumpo/src/dsp/band_processor.h#L124-L125) (with distortion_adapter.cpp:27-31, morph_engine.cpp:51-52, processor.cpp:494)

**Problem.** BandProcessor::prepare prepares morphEngine_ (and distortion_) at sampleRate * kMaxOversampleFactor (8). Every DistortionAdapter then calls toneFilter_.prepare(8*fs), dcBlocker_.prepare(8*fs, 10 Hz), and the tape-HF, body and wavefold-smooth filters at 8*fs, and MorphEngine configures its X/Y smoothers at 8*fs. The processor only ever calls the per-sample BandProcessor::process (1x), so these coefficients run at the host rate.

**Why wrong.** OnePoleLP's coefficient is exp(-2*pi*fc/fs) (one_pole.h:138). With fs=8*fs_host and the filter clocked at fs_host, the real cutoff is fc/8. All Hz and ms quantities must be derived for the rate at which the filter is actually clocked.

**Consequence.** At 44.1 kHz the default tone of 4000 Hz becomes a ~500 Hz one-pole LP on every non-digital wet path, and the top of the range (8000 Hz) is ~1000 Hz. Because L and R are interleaved through the same engine, the per-channel figure is roughly 2x that for correlated signals, still far below the displayed value. The 10 Hz DC blocker becomes ~1.25 Hz (slow DC settling after bias changes on AsymmetricFuzz and the rectifiers). The morph smoothing time is 8x longer than set (halved again by the double L/R call), and tape flutter runs at 5.5/8 Hz. Disrumpo sounds heavily muffled and the Tone knob label is wrong by about 3 octaves.

**Evidence.** band_processor.h:111 `distortion_.prepare(sampleRate * kMaxOversampleFactor, ...)`, :124 `morphEngine_->prepare(sampleRate * kMaxOversampleFactor, ...)`, band_processor.h:47 kMaxOversampleFactor = 8. distortion_adapter.cpp:27 `toneFilter_.prepare(sampleRate);` :31 `dcBlocker_.prepare(sampleRate, 10.0f);`. one_pole.h:138 `coefficient_ = std::exp(-kTwoPi * cutoffHz_ / sampleRate_)`. band_processor.h:487-488 per-sample morphEngine_->process(left/right), called from processor.cpp:494. Computed: a = exp(-2*pi*4000/352800) gives an effective 500.0 Hz at 44.1 kHz; 8000 Hz gives 1000.0 Hz.

**Corroborated by measurement.** probe tone-response, tone 4000 Hz: proc_rel_dB vs adapter_rel_dB "1k: -2.98,-0.25 | 2k: -7.00,-0.94 | 4k: -12.46,-2.91 | 8k: -18.93,-6.59", implied fc "1007/999/981/910 Hz" vs adapter "4070/4064/4095/4238". probe adapter-vs-processor (26 types): "19 bad, 6 suspicious, 1 ok. SergeFold -11.9 dB/0.077; FeedbackDist +9.9/0.282; RingSaturation +5.9/0.359; SineFold and TriangleFold -11.4/0.494". Code: band_processor.h:111 `distortion_.prepare(sampleRate * kMaxOversampleFactor,`, :124 `morphEngine_->prepare(sampleRate * kMaxOversampleFactor,`

**Suggested fix.** Prepare the morph engine and adapters at the rate they are actually clocked: the host rate while the per-sample 1x path is live, or wire processBlock and re-prepare per factor. Do not prepare at 8x and run at 1x. Test: a processor-level render of a 1-band SoftClip node, drive 1, tone 8000 Hz, with a 4 kHz sine at -30 dBFS (linear region); assert output within 1 dB of input level. Today it reads about -12 dB.

**Verifier notes:**
- Correctness: morph_engine.cpp:51-52 configures the smoothers at the passed rate and :57-60 prepares every adapter with it; distortion_adapter.cpp:27/:31/:46/:51/:55 prepare toneFilter_, dcBlocker_, tapeHfFilter_, bodyFilter_ and wavefoldSmoothFilter_ at 8x; :218-219 applies the DC blocker and :225-231 applyTone (toneFilter_.process at :583) on every non-digital type at the host rate; the comment at :224 even says the tone filter is "prepared at oversampled rate".
- Context: no code rescales cutoffs by the factor; no document in specs/Disrumpo describes running 8x-prepared state at 1x; the :224 comment exempts only the digital types as a symptom workaround; even the unused processBlock path would be off for 2x and 4x because it prepares at 8x too.
- Impact: effective cutoff fc/8 (500 Hz for the 4 kHz default, 1 kHz for the 8 kHz max), or fc/4 for correlated L = R because the shared filter runs at 2*fs; a 4 kHz linear-region sine at tone 8000 loses about 7-12 dB; the DC blocker (about 1.25-2.5 Hz) and morph smoothing (4-8x longer) are off by the same factor; every non-digital wet path, every host, every rate, default settings; high.

### D-008 — Cross-family transition gain is discontinuous at weight 0.4 (jumps from 1.0 to 0.0), giving a -4.4 dB step mid-morph

**Severity:** high | **Verdict:** CONFIRMED | **Category:** dsp-numerics | **Dimension:** dsp-numerics | **Also flagged by:** morph-oversampling; D-097 and D-139 report the same gain law | **Corroborated by measurement:** yes (morph-sweep probes, see D-139)

**Location:** [plugins/disrumpo/src/dsp/morph_engine.cpp:352-379](plugins/disrumpo/src/dsp/morph_engine.cpp#L352-L379) (used at 269, 748-756)

**Problem.** calculateTransitionGain returns sin((w/0.4)*pi/2) for w<0.4, which reaches 1.0 as w approaches 0.4 from below. At w>=0.4 it switches to sin(((w-0.4)/0.2)*pi/2), which is 0.0 at w=0.4. The gain therefore drops from ~1 to 0 at w=0.4 and then ramps back to 1 at 0.6. processCrossFamily outputs sum(nodeOut * w * gain).

**Why wrong.** A transition-zone gain has to be continuous and monotonic in the weight, or the morph clicks (FR-008, SC-002 within 1 dB, SC-003 artifact-free). The two branches do not meet at 0.4. Even away from the step, w*gain is not equal-power: at 0.5/0.5 the sum is 0.707.

**Consequence.** With 2 cross-family nodes of equal level, summed contributions are: wB=0.3999 -> 1.000 (0 dB), wB=0.4 -> 0.600 (-4.44 dB, instantaneous), 0.45 -> -3.35 dB, 0.5 -> -3.01 dB. The same step appears symmetrically near the other node. In Linear1D with nodes at x=0 and x=1, wB=0.4 sits at x~0.4495, so dragging, automating or LFO-modulating the morph cursor across it produces an audible level step and click on every pass. Any preset that morphs between families (for example SoftClip to Bitcrush) is affected.

**Evidence.** morph_engine.cpp:364-370 `if (weight < kTransitionZoneLow) { const float normalizedPos = weight / kTransitionZoneLow; ... equalPowerGains(normalizedPos, fadeOut, fadeIn); return fadeIn; }`; :374-378 `zonePos = (weight - 0.4)/(0.6-0.4); equalPowerGains(zonePos,...); return fadeIn;`. crossfade_utils.h:51-52 fadeIn = sin(position*pi/2). morph_engine.cpp:756 `output += nodeOutput * weight * gain;`. Computed: sum(w*g) = 1.0000 at wB=0.3999, 0.6000 at wB=0.4000, 0.7071 at 0.5. Linear1D wB=0.4 at x=0.4495. The only FR-008 test (morph_interpolation_test.cpp:313-343) asserts only that the weights sum to 1.

**Suggested fix.** Make the gain a single continuous function of w, for example a plain weight-normalized blend (gain=1, contributions w_i), or an equal-power law sqrt(w_i) normalized so that sum(g_i^2)=1. The <0.4 branch must end where the zone branch starts. Test: sweep the cursor in 1e-4 steps across x in [0.4,0.6] with 2 cross-family nodes fed a constant-level sine, and assert the per-step output RMS change is < 0.1 dB and the total level stays within 1 dB (SC-002).

**Verifier notes:**
- Correctness: kTransitionZoneLow/High are 0.4/0.6 (morph_engine.cpp:30/:33); the below-zone branch (:364-371) tends to sin(pi/2) = 1 and the zone branch (:374-378) starts at sin(0) = 0; weights are recomputed per sample from the smoothed cursor (:672) so the step lands inside one sample; Linear1D inverse-square weights (:195-207) put wB = 0.4 at x = 0.4495; the node is skipped outright when its gain < kWeightThreshold (:749).
- Context: dsp-details.md:1156-1198 specifies a continuous law (A=1 below the zone, cos/sin inside it, B only above) and :1203 asks for a 5-10 ms fade-in on zone entry, neither of which the code has; the comment at :365 ("linear ramp from 0 to entry level") does not match the sine that ends at 1.0; transitionGains_ has no smoother (written only at :87 and :269); no test pins the gain law.
- Impact: every pass of a dragged, automated or modulated cursor across x = 0.4495 or 0.5505 in a cross-family band gives a -4.4 dB step and click, plus a -3 dB dip at the midpoint (and about -0.55 dB at wB = 0.1), breaking SC-002/SC-003; high.

### D-003 — Processor::setState mutates live DSP objects from the host thread with no handoff (data race, possible use-after-free)

**Severity:** high | **Verdict:** CONFIRMED | **Category:** rt-safety | **Dimension:** rt-safety | **Also flagged by:** lifecycle-state | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor_state.cpp:440-463](plugins/disrumpo/src/processor/processor_state.cpp#L440-L463), [:827-831](plugins/disrumpo/src/processor/processor_state.cpp#L827-L831), [:892-898](plugins/disrumpo/src/processor/processor_state.cpp#L892-L898), [:798](plugins/disrumpo/src/processor/processor_state.cpp#L798)

**Problem.** setState runs on the host/UI thread and writes directly into objects the audio thread is using inside process(). It calls bandProcessors_[b].setGainDb/setPan/setMute (:440-442) and crossoverL_/R_.setCrossoverFrequency + setBandCount (:455-463), which reconfigure biquads and change numBands_ while crossoverL_.process runs (processor.cpp:473-474). It calls setMorphMode / setMorphSmoothingTime (:827-831), which reconfigure the smoothers. It calls setMorphNodes (:894), which overwrites MorphEngine::nodes_ (about 4 x 500 B structs) while interpolateParams reads them per sample, and calls setType/setParams on the adapters. For a Spectral node that reaches spectral_.prepare, which frees and replaces the pffft setup and buffers the audio thread may be inside. It calls modulationEngine_.setRouting (:798) while evaluateRoutings iterates, and it mutates customCurve_ with remove/add/sort (:552-569). bandMorphCache_ is also written by processParameterChanges on the audio thread. None of this is guarded by an atomic flag, a lock-free queue or a double buffer.

**Why wrong.** VST3 hosts may call IComponent::setState while the component is active and processing: host preset load, undo, and project-level preset switches. The plugin must hand state to the audio thread (lock-free FIFO / pending-state swap applied at the top of process()) or otherwise make it safe. Only the three global atomics (:400-402) and bandCount_ are atomic. Every other write is a plain non-atomic race, which is undefined behaviour in C++.

**Consequence.** Loading a project-level preset while the transport runs can (a) tear MorphNode params mid-sample and give garbage types or parameters for a block, (b) make the crossover cascade run with half-updated band count and coefficients, causing clicks and a broken sum, and (c) for a Spectral node, crash the host with a use-after-free in pffft when fft_.prepare frees setup_ under the audio thread. Intermittent and hard to reproduce, which is typical for host-crash reports.

**Evidence.** processor_state.cpp:440-442 `bandProcessors_[b].setGainDb(bs.gainDb); ... setMute(bs.mute);` :455-456 `crossoverL_.setCrossoverFrequency(c, freq);` :462-463 `crossoverL_.setBandCount(bandCount);` :827-831 setMorphMode / setMorphSmoothingTime :894-897 `bandProcessors_[b].setMorphNodes(...); setMorphPosition(...)` :552-569 customCurve_ remove/add/setBreakpoint. Compare processor.cpp:473-494, where the same objects are used per sample. morph_engine.cpp:131-140 setNodes overwrites nodes_ and calls setParams, which can reach distortion_adapter.cpp:895 spectral_.prepare (fft.h:157 setup_.reset).

**Suggested fix.** Parse the stream into a ProcessorStateSnapshot on the calling thread and publish it via an atomic pointer / SPSC slot. Apply it at the start of process(). This also fixes the setupProcessing-after-setState ordering problem. Never call prepare-class methods from setState. Test: an integration test that runs process() on one thread in a loop while another thread repeatedly calls setState with a v12 stream containing Spectral nodes and 4 bands. Run it under ASan/TSan (tests already support ASan) and assert no crash or NaN output.

**Verifier notes:**
- Correctness: the SDK annotates setState as "[UI-thread & (Initialized | Connected | Setup Done | Activated | Processing)]" (ivstcomponent.h:198-200), so a host may call it while process() runs; a grep of src/processor for mutex, try_lock, pending, atomic_flag finds only the "NEVER use locks" comments (processor.h:13, processor.cpp:184); MorphEngine::setNodes (morph_engine.cpp:131-140) copies nodes_ wholesale and calls setType/setParams, and the Spectral case (distortion_adapter.cpp:894-895) calls spectral_.prepare, which allocates.
- Context: processor.h:139-277 lists only std::atomic members for synchronisation; sibling plugins hand state over explicitly (Profundum seqlock restore at processor.cpp:1231-1267, Seraphis forcePushAllPending_), so the project treats this as a real hazard; no Disrumpo test runs setState concurrently with process.
- Impact: DistortionAdapter::prepare prepares spectral_ at the host block size (distortion_adapter.cpp:84), setState never maps slots so a loaded Spectral node carries fftSize 2048, and with any block size other than 2048 the first setState adding a Spectral node runs a reallocating prepare on the host thread while setType has already published currentType_ = Spectral to the audio thread; a microseconds-wide but real use-after-free, plus torn biquad coefficients and half-copied MorphNode params; high.


### Medium

### D-170 — Expanded band panel is not tied to Band Count: it stays on screen for a band that was just removed, and an expand request made while the band is hidden is lost

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** ui-wiring | **Dimension:** followup:ui-wiring-leftovers | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/animated_expand_controller.cpp:76-81](plugins/disrumpo/src/controller/animated_expand_controller.cpp#L76-L81)

**Problem.** AnimatedExpandController observes only the band's Expanded parameter. While the parent band is hidden, update() returns early on an expand request and stores nothing. Nothing calls it again when Band Count later reveals the band (didOpen wires the band-count ContainerVisibilityControllers only to tags 9000+b). The expanded containers (tags 9100-9103) are also siblings of the band containers in editor.uidesc, not children. When Band Count drops below b while band b is expanded, ContainerVisibilityController hides Band{b}Container but leaves the expanded strip visible.

**Why wrong.** The expanded detail panel belongs to its band: the code comment cites FR-004 ('guard against expanding a hidden band'). Visibility should be a function of (Expanded && band visible) and should be re-evaluated when either input changes. Here it is evaluated on Expanded edges only.

**Consequence.** (a) Reducing Band Count from 4 to 2 with band 3 expanded leaves band 3's 690x290 expanded strip (morph pad, node selector, type params) covering the band columns, while it edits a band that no longer processes audio. (b) If a preset or host automation sets Expanded=1 for a hidden band and the band count is then raised, the band appears with its chevron 'on' and no panel. Toggling twice is the only recovery. Note: the cancel-callback concern from the map is not a defect (VSTGUI nulls the done notification on cancel unless notifyOnCancel).

**Evidence.** animated_expand_controller.cpp:79-81 'if (shouldExpand && !isParentBandVisible()) { return; }'; :37-42 addDependent only on watchedParam_ (Expanded); controller.cpp:1359-1366 band-count ContainerVisibilityController for containerTag 9000+b only; controller.cpp:1373-1381 AnimatedExpandController(&activeEditor_, expandedParam, 9100+b, 290, 250, 9000+b, b, 690); editor.uidesc:575-609 expanded containers (Band1..4ExpandedContainer markers) declared as siblings after Band4Container (565-574).

**Suggested fix.** Give AnimatedExpandController a second dependency on the Band Count parameter, or have the band ContainerVisibilityController notify it. In update(), compute shouldShow = expanded && bandVisible and apply it, collapsing the strip when the band hides and expanding it when a band appears with Expanded=1. Test (editor lifecycle harness): open the editor, set Band Count=4 and Band3Expanded=1, set Band Count=2, and assert the 9102 container is not visible. Set Band Count=4 again and assert it is visible with the expanded height.

**Verifier notes:**
- Correctness: the constructor (:37-42) subscribes only to the Expanded param, :79-81 drops the request without storing state, ContainerVisibilityController::update (visibility_controllers.h:175-195) only toggles the container holding the 9000+b control, and the expanded containers (editor.uidesc:577-609, 690x290) are siblings in the same 960x350 parent (:534), so hiding Band3Container does not hide 9102.
- Context: no other controller code ties kBandExpanded to Band Count; at editor open the band controllers are built before the expand controllers (controller.cpp:1359-1366 then 1373-1383) so a saved Expanded=1 on a hidden band hits the early return; expand_collapse_test.cpp:257-276 checks a local boolean, never the real controller.
- Impact (dissent on severity): Band N Expanded is kNoFlags (parameter_registration.cpp:709-711) so host automation cannot set it, recovery is one click on the strip's own collapse chevron (editor.uidesc:1314), audio is unaffected; this verifier recommended low; medium retained (one of three).

### D-169 — Base-Parameter mod registrations carry ms/Hz/%/deg units but no range: host and automation text shows the raw normalized value

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** ui-wiring | **Dimension:** followup:ui-wiring-leftovers | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/parameter_registration.cpp:378-379](plugins/disrumpo/src/controller/parameter_registration.cpp#L378-L379), [:414-415](plugins/disrumpo/src/controller/parameter_registration.cpp#L414-L415), [:435-440](plugins/disrumpo/src/controller/parameter_registration.cpp#L435-L440), [:452-455](plugins/disrumpo/src/controller/parameter_registration.cpp#L452-L455), [:482-485](plugins/disrumpo/src/controller/parameter_registration.cpp#L482-L485), [:488-495](plugins/disrumpo/src/controller/parameter_registration.cpp#L488-L495), [:500-503](plugins/disrumpo/src/controller/parameter_registration.cpp#L500-L503), [:506-507](plugins/disrumpo/src/controller/parameter_registration.cpp#L506-L507), [:601-603](plugins/disrumpo/src/controller/parameter_registration.cpp#L601-L603)

**Problem.** LFO 1/2 Phase (deg), Env Attack/Release (ms), Env Sensitivity (%), Random Rate (Hz), Random Smoothness (%), S&H Rate (Hz), S&H Slew (ms), Pitch Min/Max (Hz), Pitch Tracking (ms), Transient Attack/Decay (ms) and Rungler Rate (Hz) all use the plain Parameter overload (title, units, stepCount, normalizedDefault, flags, id). Its toPlain is identity, so the displayed value is the normalized 0..1 number followed by the unit. The processor denormalizes these to real ranges: phase x360, attack 1+99v ms, release 10+490v ms, random/S&H/rungler 0.1+49.9v Hz, slew 500v ms, pitch min 20+480v, pitch max 200+4800v, tracking 10+290v, transient attack 0.5+9.5v, decay 20+180v. Controller::getParamStringByValue (controller.cpp:747-893) formats only node drive/mix/shape slots, band gain/displayed drive/mix/shape/pan and the global mix/gain, so every mod ID falls through to the base formatter. Route Amount is a RangeParameter -1..1 labelled '%', so +50 % reads '0.50 %'.

**Why wrong.** VST3 hosts show ParameterInfo.units next to Parameter::toString(normalized). The pair must read in the unit the DSP uses, through a RangeParameter with the real min/max or a getParamStringByValue override. Here the unit label claims a physical value that the number does not express.

**Consequence.** In any host automation lane, generic editor or parameter readout, Env Attack's default 10 ms shows as '0.091 ms', Pitch Max 2000 Hz shows as '0.375 Hz', LFO Phase 180 deg shows as '0.5 deg', and a +100 % route amount shows as '1.00 %'. Users cannot type real values: getParamValueByString uses the identity mapping, so entering '50' for 50 ms clamps to normalized 1.0, which is 100 ms.

**Evidence.** parameter_registration.cpp:435-436 addParameter(STR16("Env Attack"), STR16("ms"), 0, 0.091, ...); :488-491 Pitch Min/Max 'Hz' defaults 0.125/0.375; :601-603 RangeParameter("Route Amount", ..., STR16("%"), -1.0, 1.0, 0.0, ...); processor_params.cpp:591 value*360; :643 1+v*99 ms; :648 10+v*490; :664 0.1+v*49.9 Hz; :704 v*500 ms; :711 20+v*480; :716 200+v*4800; :724 10+v*290; :734 0.5+v*9.5; :739 20+v*180; :746 0.1+v*49.9; controller.cpp:747-893 has no mod-ID branch and returns EditControllerEx1::getParamStringByValue.

**Suggested fix.** Register these as RangeParameter with the processor's min/max for the linear maps (attack 1..100, release 10..500, rates 0.1..50, slew 0..500, pitch 20..500 / 200..5000, tracking 10..300, transient 0.5..10 / 20..200, phase 0..360). Display Route Amount as a percentage (RangeParameter -100..100 '%' with the processor mapping unchanged, or a formatter). Take the ranges from one shared constant used by processor_params.cpp and processor_state.cpp. Test: for each mod ID, assert that getParamStringByValue(id, n) parses back to the processor's denormalized value for n in {0, 0.5, 1}.

**Verifier notes:**
- Correctness: all listed registrations use the plain overload; the mod IDs (makeModParamId, 0x0Dxx, plugin_ids.h:529) fail isNodeParamId (plugin_ids.h:323), isBandParamId (:244) and isGlobalParamId (:83), so controller.cpp:747-893 falls through at :893 and getParamValueByString (:896-901) uses the identity map; Env Sensitivity and Random Smoothness pass v as 0..1 (processor_params.cpp:653, :669) but are labelled '%', so '0.5 %' means 50 %; the SDK prints 4 digits ('0.0910'), which does not change the defect.
- Context: no formatter, RangeParameter or spec note covers these; a related extra is LFO Rate, registered as a linear RangeParameter 0.01..20 while the processor maps exp(-4.6052 + v*7.6009), so its displayed Hz also disagrees with the DSP (D-091 inventory).
- Impact (dissent on severity): display and text-entry only, audio and state round-trips are correct, the plugin's own CSliders bypass these strings; this verifier recommended low; medium retained (one of three).

### D-163 — RingSaturation at Mod Depth 0 skips its soft limiter and DC blocker: up to +8 dB level jump between depth 0 and depth 0+

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** dsp-numerics | **Dimension:** followup:shared-dsp-numerics-leftovers | **Corroborated by measurement:** no | **CROSS-PLUGIN:** shared primitive

**Location:** [dsp/include/krate/dsp/primitives/ring_saturation.h:262-282](dsp/include/krate/dsp/primitives/ring_saturation.h#L262-L282)

**Problem.** process() returns the input unchanged when depth_ == 0 (lines 270-272). For any depth > 0 the signal goes through the stages, then softLimit(x) = 2*tanh(x/2) (line 418) and dcBlocker_ (lines 279-280). As depth approaches 0, processStage approaches input + 0*ringModTerm (line 337), so the depth-0+ output is 2*tanh(x/2) of the input while depth 0 gives x raw. Disrumpo maps the RingSat Mod Depth knob (slot 0) directly to depth (processor_params.cpp:168, distortion_adapter.cpp:776), and the adapter feeds input*drive with drive up to 10 (distortion_adapter.cpp:211). Lead 2 (NaN passthrough) is refuted: lines 262-264 return before touching any state, so it is stateless. The NaN then hits the adapter tone OnePoleLP, which zeroes it.

**Why wrong.** A 'depth 0 = no ring mod' bypass must be continuous with small depths. Here the limiter and DC blocker, which belong to the processor's output stage rather than to the modulation, switch in and out on an exact-equality test. The output is bounded to +/-2 for depth > 0 and unbounded at depth 0. Sliding Mod Depth fully down, or morphing to a node with modDepth 0 (morph_engine.cpp:577 interpolates modDepth linearly), crosses that edge.

**Consequence.** With the input driven to x = 5 (drive 10, -6 dBFS input), depth 0.001 gives 2*tanh(2.5) = 1.97 and depth 0 gives 5.0: an +8.1 dB jump and a click as the knob reaches its end stop. At x = 1.25 (drive 2.5) the jump is +1.0 dB. The DC blocker's state also freezes while depth is 0 and resumes with stale state.

**Evidence.** ring_saturation.h:270-272 `if (depth_ == 0.0f) { return input; }`. :279-280 `signal = softLimit(signal); signal = dcBlocker_.process(signal);`. :337 `float output = input + ringModTerm * depth_;`. :417-418 `return kSoftLimitScale * Sigmoid::tanh(x * 0.5f);` with kSoftLimitScale = 2.0f (:84). processor_params.cpp:168 `p.modDepth = slots[0];`. Computed: 2*tanh(2.5) = 1.973 vs 5 (-8.07 dB); 2*tanh(0.625) = 1.109 vs 1.25 (-1.04 dB).

**Suggested fix.** Remove the depth_ == 0 early return, or keep it only for the ring-mod term and always apply softLimit and dcBlocker_ so depth 0 means 'saturate and limit without ring mod'. Alternatively, crossfade the bypass over the depth range [0, 0.01]. Add a dsp_primitives test: feed a sine at amplitude 2.5 with depth 0 and depth 1e-4, and require the RMS difference to be within 0.1 dB.

**Verifier notes:**
- Correctness: the exact-zero bypass at :270-272 sits after the NaN/Inf checks and before the stage loop, softLimit and dcBlocker_; processStage (:336-337) tends to the input as depth tends to 0 so the depth-0+ output tends to 2*tanh(x/2); Disrumpo's slot maps 1:1 (processor_params.cpp:168 -> distortion_adapter.cpp:776, clamped [0,1] at :183) and drive is clamped to [0,10] (band_processor.h:445, morph_engine.cpp:713); the DC blocker's state does freeze at :280.
- Context: the depth-0 bypass is intentional and tested (ring_saturation_test.cpp:302-307, :1602, :1637, SC-002), but those tests cover only ±1 inputs and never continuity; no spec note accepts the limiter switching with the depth; dsp-details.md:460-463 describes D16 only as self-modulation.
- Impact: the jump is about 0.1 dB at x = 0.5, 1.0 dB at x = 1.25 and 8 dB at x = 5 (drive 10 makes the depth-0 path a plain unlimited 10x gain); default modDepth is 0.5 (distortion_adapter.h:136) so the default sound is unaffected; the parameter is set per block with no smoothing so crossing the edge is a step; medium.

### D-159 — Wavefold level collapse (-23 to -47 dBFS) fully accounted for: default Smooth LP on an FM-index-24 spectrum, filters rated for 8x and clocked twice per sample, and centre-pan -3 dB; level drops as drive rises

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** measured | **Dimension:** followup:rectify-fold-level-rootcause | **Corroborated by measurement:** yes (type:SergeFold/SineFold/TriangleFold rows and adapter-vs-processor rows 18/19/25)

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.cpp:656-690](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L656-L690)

**Problem.** A Node.js replica of the adapter chain reproduces all nine probe measurements within 0.9 dB. The chain: drive x input -> fold -> WavefolderProcessor DC blocker -> 50/50 shape blend -> wavefoldSmoothFilter_ at 20000*(500/20000)^0.5 = 3162 Hz (the Smooth slot defaults to 0.5, so the LP is on by default) -> tone LP 4 kHz -> centre pan. In the plugin both one-poles carry coefficients for 8x the rate (band_processor.h:111,124). They are also clocked twice per output sample because L and R share one MorphEngine, so effective cutoffs are fc/4: 790 Hz and 1 kHz. Default folds are 6.5, so the sine fold sin(6.5 x 3.75 sin wt) has a modulation index of 24.4 rad and a Carson bandwidth of about 26 kHz. Its energy sits above Nyquist (aliased) and far above the effective LPs, which remove most of it. More drive pushes energy higher, so the output gets quieter: probe -23.2, -26.6, -31.0 dBFS for drive 2.5, 7.5, 10.

**Why wrong.** Drive should raise or saturate the level, not lower it monotonically. 'Smooth' is an optional tone control, but here it defaults to a 3.16 kHz low-pass (and about 790 Hz effective in the plugin). The fold spectrum at default Folds x Drive does not fit under Nyquist at 1x, so the type is mostly alias energy before the filters remove it.

**Consequence.** The three wavefolders sit 20-45 dB below the other types at default settings and lose level as the user turns Drive up. This explains D-145. It also explains the -11.4 to -11.9 dB adapter-vs-processor gaps (probe lines 18, 19, 25), which the replica predicts as -11.4 dB (Sine/Triangle) and -12.4 dB (Serge). Breakdown at SineFold drive 7.5: shape blend -5.4 dB, default smooth plus tone LP at the correct rate -4.1 dB peak (-7.9 dB RMS), 8x mis-rating with double clocking -13.8 dB, pan -3 dB. Only the mis-rating and the double clocking are already-reported defects; the default Smooth of 0.5, the 0.5 shape blend and the folds-x-drive index are not.

**Evidence.** distortion_adapter.cpp:664, 673, 689 `wavefoldSmoothFilter_.setCutoff(20000.0f * std::pow(500.0f / 20000.0f, p.smoothness));` with p.smoothness = slots[4] or slots[5], default 0.5 (processor_params.cpp:94, 103, 113). OnePoleLP coefficient a = exp(-2*pi*fc/fs) with fs taken at prepare (one_pole.h:136-139). Probe settings: audit_probes.cpp.txt:433-441 (only type, mix, bias and drive set; slots stay at 0.5). Replica fold_sim2.js (double-clocked, then -3 dB pan) vs probe peak dBFS: Sine 2.5/7.5/10 = -23.31/-26.54/-31.88 vs -23.23/-26.64/-31.04; Serge = -46.81/-47.38/-36.00 vs -47.18/-47.52/-35.93. Same chain at the adapter's own correct 48 kHz rate: Sine -7.82/-9.71/-15.68 dBFS peak, Serge -31.83/-33.25/-25.87.

**Suggested fix.** Besides the reported 8x-rate and shared-L/R fixes: default the Smooth slot to 0 for SineFold, TriangleFold, SergeFold, FullRectify and HalfRectify; default Folds to 1-2 (slot near 0-0.1) so the default index fits under Nyquist; and consider normalising the drive into the fold (fold gain on a unit-peak signal) instead of multiplying drive x folds. Promote the replica as a processor-level test: SineFold, all defaults, drive 2.5/7.5/10, 1031.25 Hz at -6 dBFS, 48 kHz; peak must stay within 6 dB across the three drives and above -12 dBFS.

**Verifier notes:**
- Correctness: morph_node.h:43 sets every slot to 0.5; processor_params.cpp:90-113 maps that to folds 6.5, shape 0.5 and smoothness 0.5; the smooth filter runs on every wavefold sample with no bypass at smoothness 0 (distortion_adapter.cpp:389, :406, :422) and the 50/50 blend is active (:384-386, :400-404, :418-420); probe-results.txt:521-526 and :18/:19/:25 match the quoted numbers; SergeFold is not monotonic (drive 10 is louder than 7.5), only Sine/Triangle fall with drive.
- Context: the 0.5 defaults are off-spec: ui-mockups.md:354-358, 382-386, 410-415 give SineFold Folds 2 / Shape 0.0 / Smooth 0.3, TriangleFold Folds 2 / Smooth 0.3, SergeFold Folds 3 / Smooth 0.3, Rectify Smooth 0.2; the ShapeShadowStorage constructor overrides only BitwiseMangler (morph_node.h:102-114).
- Impact: re-running the replica gives Sine -20.31/-23.54/-28.88 dBFS before pan with the mis-rating, matching the probe within 0.9 dB; at the correct rate with smoothing and tone off the Sine fold is flat at about -6 dBFS, so the defaults alone cost about 4-9 dB of drive-dependent loss and leave the folds 5-10 dB below SoftClip; the quoted "-7.82/-9.71/-15.68" correct-rate figures do not reproduce from the script (it gives -6.41/-7.72/-10.87 before pan); the largest share (about 13.8 dB) belongs to D-009/D-016; medium.

### D-158 — SergeFold defaults to the Buchla259 model, and the triangle and Buchla folders' output peak is 1/folds regardless of input, so SergeFold peaks at -22 dBFS even with every filter off

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** signal-path | **Dimension:** followup:rectify-fold-level-rootcause | **Corroborated by measurement:** yes (type:SergeFold rows; audit_band_arrival_results.txt:29-36)

**Location:** [plugins/disrumpo/src/processor/processor_params.cpp:106-114](plugins/disrumpo/src/processor/processor_params.cpp#L106-L114)

**Problem.** Two level problems stack. (1) The model slot defaults to 0.5, and `static_cast<int>(slots[2]*3.0f + 0.5f)` = 2 indexes models[2] = Buchla259 (distortion_adapter.cpp:678-685), so a fresh 'SergeFold' node is a Buchla 259 folder. (2) The triangle and Buchla folders scale their threshold by 1/foldAmount and never apply makeup gain. Wavefolder::applyTriangle uses threshold = 1/foldAmount, so |out| <= 1/folds. applyBuchla259 sums triangleFold(x, T_i/fold) weighted by G = {1,.8,.6,.4,.2} and divides by sum(G) = 3, so |out| <= 1.4/(3*fold). At the default folds of 6.5 the triangle branch peaks at 0.154 (-16.3 dBFS) and Buchla at 0.072 (-22.9 dBFS), whatever the input level. Raising Folds to 12 lowers them further, to -21.6 and -28.2 dBFS. The adapter forwards p.folds unchanged (cpp:658, 669, 686-688), and the SineFold/SergeFold 'shape' crossfade mixes in the same triangle branch (cpp:418-421).

**Why wrong.** A fold-count or 'Folds' control should change the harmonic density. It should not act as a 16-28 dB attenuator, and a wavefolder fed a full-scale signal should produce close to full-scale output. The Sine model already does: sin() peaks at 1 for any gain. The type is named and documented as a Serge folder (specs-overview.md D09; adapter comment cpp:677 '0=Serge'), but the default slot lands on the third entry.

**Consequence.** The default SergeFold node renders -47.5 dBFS peak from a -6 dBFS input in the plugin (probe-results.txt:522). Even at the correct sample rate with smooth and tone off, the replica gives -21.7 dBFS. The Folds knob works backwards: more folds means quieter. Morphing Serge against any saturation type gives a 20+ dB level hole.

**Evidence.** processor_params.cpp:108-110 `p.folds = 1.0f + slots[0] * 11.0f; ... p.foldModel = static_cast<int>(slots[2] * 3.0f + 0.5f);`. distortion_adapter.cpp:678-685 `models[] = {Serge, Simple, Buchla259, Lockhart}; int mi = std::clamp(p.foldModel, 0, 3);`. primitives/wavefolder.h:257-259 `const float threshold = 1.0f / foldAmount_; return WavefoldMath::triangleFold(x, threshold);`. wavefolder_processor.h:430-446 (scaledThreshold = thresholds[i]/foldAmount; output /= gainSum) and :456-463 (thresholds 0.2..1.0, gains 1.0..0.2). Replica (fold_sim.js, 48 kHz, correct-rate filters): SergeFold(Buchla) with smooth=0, tone off peaks at -21.68/-21.74/-21.23 dBFS for drive 2.5/7.5/10. Triangle-only (shape 0) peaks at -16.98/-17.18/-16.53 dBFS. Sine-only peaks at -0.04/-0.21/-0.98 dBFS. With the plugin's 8x-rated filters, double clocking and -3 dB pan the replica gives SergeFold -46.8/-47.4/-36.0 dBFS vs the probe's -47.18/-47.52/-35.93.

**Suggested fix.** In the adapter, not the shared primitive (its 1/fold contract is documented and other KrateDSP consumers may rely on it), multiply the Simple and Buchla259 outputs, and the wavefoldTriangle_ branch, by foldAmount: Buchla by 3*fold/1.4, or by a measured RMS-matching factor. Change the SergeFold model default to index 0 (Serge): either a per-type slot-2 default of 0.0 in ShapeShadowStorage, or reorder the table. Keep the menu labels consistent with the inventory finding that labels 0 and 1 are swapped (D-168). Test: for SergeFold at every model and SineFold/TriangleFold at shape 0, a 1 kHz -6 dBFS sine at drive 5 must give peak within 6 dB of the SoftClip type's peak at folds 2 and at folds 12.

**Verifier notes:**
- Correctness: ShapeShadowStorage sets every slot to 0.5 (morph_node.h:102-106), so foldModel = int(2.0) = 2 = Buchla259 via distortion_adapter.cpp:678-685; the uidesc menu (editor.uidesc:974 "Simple,Serge,Buchla,Lockhart") also lands on Buchla at index 2 but labels 0 and 1 the reverse of the adapter table; applyBuchla259 (wavefolder_processor.h:430-446) is capped at 1.4/(3*6.5) = 0.072 (-22.9 dBFS) and the triangle model at 1/6.5 (-16.3 dBFS); DistortionAdapter::process (:189-234) applies no makeup gain; probe-results.txt:522 was not on disk for one verifier, but audit_band_arrival_results.txt:29-36 shows SergeFold bands at -37.3/-39.5 dBFS against -31.4/-37.8 for SoftClip.
- Context: the spec default for the D09 model is Lockhart (ui-mockups.md:412; dsp-details.md:117, tasks-overview.md:82 map SergeFold to Lockhart), so Buchla is a deviation; no test checks model level (distortion_adapter_test.cpp:358-412 only checks that models differ); WavefolderProcessor clamps foldAmount at 10 (wavefolder_processor.h:125, :261; wavefolder.h:150), so "Folds 12" acts as 10 (-20/-26.6 dBFS, not -21.6/-28.2).
- Impact: the default node is 16-23 dB quieter than its input before any filtering and the Folds knob lowers level further; the -47.5 dBFS probe figure also includes D-009/D-016/D-021; audible level and usability defect on one of 26 types at default settings; medium.

### D-157 — SineFold and TriangleFold produce the same output at default settings: both compute 0.5*sineFold + 0.5*triangleFold of the same signal

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** signal-path | **Dimension:** followup:rectify-fold-level-rootcause | **Corroborated by measurement:** yes (type:SineFold vs type:TriangleFold rows identical to 0.02 dB)

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.cpp:376-408](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L376-L408)

**Problem.** SineFold runs WavefolderProcessor in the Serge (sine) model and crossfades toward wavefoldTriangle_ (triangle) by `shape`. TriangleFold runs the Simple (triangle) model and crossfades toward wavefoldTriangle_ switched to Sine by `angle`. Both crossfade controls come from slot 2, and slot 2 defaults to 0.5 (processor_params.cpp:92, 101; morph_node.h:43-44). Folds is 1+0.5*11 = 6.5 for both and symmetry is 0 for both. So both types compute result = 0.5*sin(6.5x) + 0.5*tri(x, 1/6.5), then the same smooth and tone filters. The only differences are WavefolderProcessor's internal 10 Hz DC blocker and smoothers, which are inaudible here.

**Why wrong.** D07 and D08 are offered as two distinct algorithms (the spec family table lists Sine Fold and Triangle Fold separately; the TypeParams templates give each its own panel). The crossfade parameter should default to 0, the pure algorithm, as DistortionParams already does (`float shape = 0.0f;`, distortion_adapter.h:100). The generic 0.5 slot default turns both into the same 50/50 blend.

**Consequence.** Picking Triangle Fold instead of Sine Fold, or morphing between them, makes no audible difference until the user finds the Shape/Angle knob. Every probe row for the two types is identical to 0.02 dB, which would also mislead any listening test or preset design built on 'Sine vs Triangle'.

**Evidence.** Code: cpp:384-386 `if (typeParams_.shape > 0.001f) { float triFolded = wavefoldTriangle_.process(biased); result = result * (1.0f - typeParams_.shape) + triFolded * typeParams_.shape; }` vs cpp:400-404 (Sine via wavefoldTriangle_.setType(Sine), weight `angle`). routeParams sets SineFold to Serge (sine) at :657 and TriangleFold to Simple (triangle) at :668. Probe: type:SineFold peaks -23.23/-26.64/-31.04 vs type:TriangleFold -23.25/-26.65/-31.04 (probe-results.txt:524-526 vs 542-544). adapter-vs-processor -11.39 dB / centroid 2246->1110 Hz (line 19) vs -11.38 dB / 2246->1110 Hz (line 25). Replica (scratchpad fold_sim.js): identical peak/RMS to 0.01 dB at all drives.

**Suggested fix.** Give slot 2 a per-type default of 0.0 for SineFold and TriangleFold in ShapeShadowStorage, as the constructor already special-cases BitwiseMangler at morph_node.h:111-113. Apply the same default in the controller's shapeShadowStorage_ and the uidesc/registration default for Shape2. Test: render a fresh SineFold node and a fresh TriangleFold node (defaults, drive 5, 1 kHz -6 dBFS) and assert their spectral centroids differ by more than 20 % or their RMS difference exceeds 1 dB. Today both read identical.

**Verifier notes:**
- Correctness: wavefolder_processor.h:391-392 maps Serge to WavefoldType::Sine and :388-389 maps Simple to Triangle; both delegate to the same Wavefolder primitive with the same foldAmount (wavefolder.h:251-266); MorphNode::shapeSlots and ShapeShadowStorage default to 0.5 (morph_node.h:43-44, :102-106) and processor_params.cpp:840-853 loads them on a type change; the only remaining difference is the 10 Hz DC blocker on the processor's half of the blend.
- Context: processor_params.cpp:88-104 maps slot 2 to shape (SineFold) and angle (TriangleFold); the code comments at :383/:399 call slot 2 a crossfade "between sine fold (0) and triangle fold (1)" so the 0.5 default is the generic slot default, not a per-type choice; DistortionParams' own shape default is 0; the controller mirrors the same ShapeShadowStorage (controller.h:355); no test checks silent-start differences between the two types.
- Impact: two of the 26 types sound the same when first picked and morphing between them does nothing until Shape/Angle leaves 0.5; presets that set slot 2 explicitly are unaffected; medium.

### D-143 — Quantize maps silence to a non-zero level (-49 dBFS from silent input)

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** measured-probe | **Dimension:** measured | **Also flagged by:** D-010 is the static analysis of the same code | **Corroborated by measurement:** yes (probe `selfnoise:Quantize`)

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.cpp:482-503](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L482-L503)

**Problem.** `index = (input + 1)/stepSize; index += quantOffset; index = round(index) - quantOffset;`. With an even or fractional level count, or a non-integer offset, input 0 rounds to a level that is not 0. Quantize also skips the DC blocker and the tone filter.

**Why wrong.** The quantiser should be mid-tread around 0 (or have its DC removed) so that silence stays silent.

**Consequence.** A constant offset or idle tone at -49 dBFS whenever Quantize is active and the input is silent. It adds to the DC found in presets.

**Evidence.** probe selfnoise:Quantize: "rms_dBFS -49.0" (threshold <= -90 dBFS), "the tail is -48.8 dBFS". Code: distortion_adapter.cpp:487 `float index = (input + 1.0f) / stepSize;`, :501 `index = std::round(index) - typeParams_.quantOffset;`

**Suggested fix.** Quantize on a grid centred at 0 (round(x/step)*step), apply the offset as a symmetric shift, and add Quantize to the DC-block list. Test: the selfnoise:Quantize probe must read <= -90 dBFS.

**Verifier notes:**
- Correctness: a fresh node has quantLevels 0.5 (N = 33, index 16), dither 0.5 and quantOffset 0.5 (morph_node.h:43, processor_params.cpp:145-151), so the index sits at 16.5 plus TPDF dither in [-0.5, 0.5] and rounds to 16 or 17, giving ±1/32 (-30 dBFS) on every silent sample before the smoothing lowpass (:513), consistent with -49 dBFS; needsDCBlock_ excludes Quantize (:586-592) and tone is skipped (:225-231).
- Context: the shared BitCrusher primitive (bit_crusher.h:92-101) records this exact [0,1] round-trip pattern as a fixed bug ("OLD BUG: 0.0 -> 0.00002 offset; NEW: symmetric quantization"), while Quantize still uses it; the comment at :488 only says the offset shifts the grid; no test pins silent-input behaviour; skipping the tone filter for Quantize (:222-229) is a deliberate choice and not the cause.
- Impact: with the default dither 0.5 the output is zero-mean hiss at about -49 dBFS rather than DC; with dither 0 and a non-integer offset it is constant +step/2 DC (about -30 dBFS at N = 33); a -49 dBFS noise floor from the default parameters whenever Quantize is active is audible in quiet passages; medium.

### D-140 — Band-count changes click: crossover topology switches instantly with no crossfade

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** measured-probe | **Dimension:** measured | **Also flagged by:** D-023 is the static analysis of the same transition | **Corroborated by measurement:** yes (probe `automation:bandcount2-4-1`)

**Location:** [plugins/disrumpo/src/processor/processor_params.cpp:339-346](plugins/disrumpo/src/processor/processor_params.cpp#L339-L346)

**Problem.** kBandCountId stores bandCount_ and calls crossoverL_/R_.setBandCount on the audio thread. CrossoverNetwork::setBandCount (crossover_network.h:111-122) changes numBands_ and redistributes or reconfigures allpasses at once. The sample loop (processor.cpp:483) then starts or stops summing bands, and newly active bands carry stale filter and adapter state.

**Why wrong.** Discrete structural parameter changes on the audio path need a short crossfade (old vs new topology) or a ramp to avoid discontinuities.

**Consequence.** Audible clicks when the band count is automated or changed during playback.

**Evidence.** probe automation:bandcount2-4-1 (step1, step2): "jump_ratio,clicks_step = 3.78,1 / 3.90,1" (threshold jump_ratio <= 1.3). Code: processor_params.cpp:344 `crossoverL_.setBandCount(clamped);`

**Suggested fix.** Crossfade the output over about 10 ms when the band count changes (render old and new topology, or fade the bands in and out through their mute smoothers), and reset newly enabled bands' state. Test: the automation:bandcount probe jump ratio must be <= 1.3.

**Verifier notes:**
- Correctness: setBandCount sets numBands_ at once and calls redistributeCrossovers with no reset (crossover_network.h:111-122, 274-297); process() (:185-228) runs a different number of LR4 splits and allpass stages from the next sample and passes input straight through at 1 band (:195-197); bands above the old count resume from stale state because processor.cpp:481 only processes b < numBands and the mute/solo fade is gated by shouldBandContribute (:563), not band count.
- Context: the retired spec (002-band-management/spec.md:260 FR-010, :337 SC-002, :230) requires band-count changes to be click-free; IT-003 (band_management_integration_test.cpp:137-172) feeds DC and settles 4000 samples on each side, so it never looks at the transition; Band Count is kCanAutomate (parameter_registration.cpp:65-77).
- Impact: probe-results.txt:27-28 show ratios 3.78/3.90 with 1 click each and equal pre/post levels (-18.23/-18.24 dB), so this is a pure discontinuity of roughly 0.02 (about -34 dBFS) on a 220 Hz sine, audible as a tick on clean material; mute, solo, drive, mix and OS steps in the same run were clean; medium.

### D-138 — Morph cursor smoothing is about 4x slower than set (10 ms reads 37 ms)

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** measured-probe | **Dimension:** measured | **Also flagged by:** D-099 (spec-conformance) | **Corroborated by measurement:** yes (probe `morph-smoothing:default`)

**Location:** [plugins/disrumpo/src/dsp/morph_engine.cpp:50-51](plugins/disrumpo/src/dsp/morph_engine.cpp#L50-L51)

**Problem.** smootherX_/Y_.configure(smoothingTimeMs_, sampleRate) receives the 8x sample rate (band_processor.h:124). process() advances the smoothers once per call, and it is called twice per host sample (L then R, band_processor.h:487-488). The net effect is 8/2 = 4x the configured time. setSmoothingTime at :127 uses the same rate.

**Why wrong.** FR-MORPH-003 says the smoothing time is in ms of real time (0-500 ms). The smoother must be configured at the rate it is advanced at and advanced once per sample.

**Consequence.** Morph motion lags 4x behind the setting. The 500 ms maximum becomes about 2 s, and automation and modulation of morph X/Y feel sluggish.

**Evidence.** probe morph-smoothing:default: "time_to_99pct_ms 37.33" (threshold "10 ms +/- 30 %"). Code: morph_engine.cpp:51 `smootherX_.configure(smoothingTimeMs_, static_cast<float>(sampleRate));`

**Suggested fix.** Configure the smoothers at the actual processing rate and advance them once per frame, not once per channel. Promote the morph-smoothing:default probe: time to 99 % = 10 ms +/- 30 %.

**Verifier notes:**
- Correctness: band_processor.h:123-124 prepares the engine at sampleRate*8 (kMaxOversampleFactor at :47); morph_engine.cpp:51-52 and setSmoothingTime (:127-128) configure OnePoleSmoothers (morph_engine.h:228-229, default 10 ms at :221) at that rate; smoother.h:156-158 defines configure() as time-to-99 %; process() (:662-669) advances both smoothers per call, and both the 1x path (band_processor.h:487-488) and the oversampled path (:753-754) call it once per channel.
- Context: the effective time is set_ms * 8 / (2 * factor): 4x slow at 1x, 2x slow at 2x, correct at 4x, 2x too fast at 8x, so the smoothing time also changes with the selected oversampling factor; the only related comment ("prepare at 8x to support all oversampling factors", band_processor.h:109-110) explains the adapter rate, not smoothing; the morph tests check clicks, not timing.
- Impact: the probe's 37.33 ms matches the 1x-path prediction of about 40 ms; 500 ms becomes about 2 s on the live 1x path; a user-facing timing parameter misbehaves with no corruption; medium.

### D-133 — Spectrum feed is spliced from disjoint block fragments: the 30 Hz throttle sends only the triggering block's samples

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** ui-wiring | **Dimension:** gestalt-processor | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor_messaging.cpp:98-120](plugins/disrumpo/src/processor/processor_messaging.cpp#L98-L120), [:139-143](plugins/disrumpo/src/processor/processor_messaging.cpp#L139-L143)

**Problem.** sendSpectrumBlock adds numSamples to an accumulator and returns until about 30 ms have accumulated. It then pushes only the CURRENT block's first min(numSamples, 2048) samples into sharedInputFIFO_/sharedOutputFIFO_ and the DataExchange block. The samples of the skipped blocks are never sent. The controller appends each received block to localInputFIFO_ (controller.cpp:1937), and SpectrumAnalyzer FFTs readLatest(2048) of that FIFO (spectrum_analyzer.h:107-108).

**Why wrong.** An FFT window has to cover contiguous audio. The throttle should limit how often messages are sent while still delivering contiguous audio, for example by accumulating the last 2048 samples in a ring and sending that.

**Consequence.** At 44.1 kHz with 128-sample blocks, one 128-sample fragment is sent every ~32 ms. The 2048-point analysis window then holds 16 fragments taken from ~0.5 s of audio, with 15 phase discontinuities. A pure sine is displayed as a broad smeared skirt, and the input-vs-output comparison shows distortion that is not there. The display is only correct when the host block is ≥ 2048 samples, which is rare.

**Evidence.** processor_messaging.cpp:103-107 `spectrumSendAccumulatorSamples_ += numSamples; if (... < spectrumSendIntervalSamples_) return; ... %=`; :109-113 `const auto count = std::min(numSamples, kSpectrumBlockMaxSamples); sharedInputFIFO_.push(spectrumBlockBuffer_.inputSamples, count);`; processor.cpp:222-226 fills inputSamples from the current block only; spectrum_analyzer.h:108 `fifo->readLatest(windowedSamples_.data(), config_.fftSize)`.

**Suggested fix.** Keep a 2048-sample mono ring for input and output that is written every block. When the throttle fires, copy the latest 2048 contiguous samples into the block and the Tier-3 FIFOs, or push every block's samples to the Tier-3 FIFOs unthrottled, since they are lock-free. Test: drive the processor with a 1 kHz sine at 64-sample blocks, collect the DataExchange or shared FIFO, and assert the sample sequence is continuous (no phase jumps) and that the analyzer's bins beyond 2 octaves from 1 kHz are below -60 dB.

**Verifier notes:**
- Correctness: processor.cpp:221-227 builds inputSamples from the current block only; processor_messaging.cpp:103-105 returns early until sampleRate*0.03 samples have accumulated (:101) and the only push sites are :113 and :119; controller.cpp:1937-1938 appends fragments to localInputFIFO_/localOutputFIFO_ (SpectrumFIFO<8192>) and SpectrumAnalyzer::process (spectrum_analyzer.h:101-119) Hann-windows readLatest(2048).
- Context: the throttle came from commit 7309ee32 (CHANGELOG:56, "Throttled spectrum data sends to ~30Hz") to fix queue overflow and did not address contiguity; the lock-free Tier-3 FIFO is throttled the same way for no queue reason; no test checks stream continuity.
- Impact: with 128-sample blocks at 44.1 kHz the 2048-sample window is 16 non-contiguous fragments with 15 phase seams (4 fragments at 512), so a pure sine shows a wide skirt; input and output are spliced identically so the comparison is wrong in the same way for both and the real harmonics get masked by splatter; display-only, no audio effect; medium.

### D-132 — setupProcessing recreates the MorphEngine; per-band morph mode and smoothing are never re-applied, and setState before setupProcessing writes into a null engine

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** gestalt-processor | **Also flagged by:** D-046 covers the node-cache reset; D-048 the getState zeros | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/band_processor.h:123-125](plugins/disrumpo/src/dsp/band_processor.h#L123-L125), [:260-287](plugins/disrumpo/src/dsp/band_processor.h#L260-L287)

**Problem.** BandProcessor::prepare() does `morphEngine_ = std::make_unique<MorphEngine>()`, which gives mode_ = Linear1D and smoothingTimeMs_ = 10 (morph_engine.h:220-221). Processor::setupProcessing re-pushes nodes and position but not mode or smoothing (processor.cpp:125-127). The processor does not store either value: kBandMorphMode and kBandMorphSmoothing go straight to the BandProcessor (processor_params.cpp:958-969) and are not kept in BandMorphCache. A further ordering hazard: before the first setupProcessing morphEngine_ is null, so setState's setMorphMode, setMorphSmoothingTime, setMorphNodes and setMorphPosition (processor_state.cpp:827-831, 893-897) are silent no-ops behind `if (morphEngine_)`.

**Why wrong.** Configuration set by parameters or state must survive setupProcessing. The processor needs to keep the authoritative copy and re-apply it after re-preparing DSP objects. The registered smoothing default is 0 ms (parameter_registration.cpp:717-719), but the engine runs at 10 ms.

**Consequence.** A sample-rate or block-size change, or any host that calls setupProcessing before activation, silently resets every band's Morph Mode to 1D Linear and smoothing to 10 ms while the UI still shows 2D Planar/Radial. With 4 nodes, 1D aliases nodes C and D onto A and B, so the sound changes. In the common host order (initialize, setState, setupProcessing), the state's morph configuration never reaches the engine. This compounds the separately known node-cache reset in setupProcessing (D-046).

**Evidence.** band_processor.h:123 `morphEngine_ = std::make_unique<MorphEngine>();`; band_processor.h:260-264 `void setMorphMode(MorphMode mode) noexcept { if (morphEngine_) { morphEngine_->setMode(mode); } }`; morph_engine.h:220-221 `MorphMode mode_ = MorphMode::Linear1D; float smoothingTimeMs_ = 10.0f;`; processor.cpp:125-127 re-push nodes and position only; processor.h:165-172 BandMorphCache has no mode or smoothing fields.

**Suggested fix.** Add morphMode and morphSmoothingMs to BandMorphCache. Set them in the param handlers and setState, write them in getState (this also fixes the literal 0 writes at processor_state.cpp:319/323), and re-apply them in setupProcessing after prepare. Test: setState with mode Planar2D and smoothing 50 ms, then setupProcessing twice (44.1 then 96 kHz), and assert that getMorphEngine()->getMode() and the smoothing time are preserved.

**Verifier notes:**
- Correctness: band_processor.h:121-125 recreates the engine with the morph_engine.h:220-221 defaults; processor.cpp:105-128 re-pushes gain, pan, mute, setMorphEnabled, setMorphNodes and setMorphPosition only; BandMorphCache (processor.h:165-172) holds nodes, activeNodeCount, morphX/Y and shapeShadow only; Processor::initialize does not prepare the bands (grep: prepare only at processor.cpp:106), so the null-engine path is real; MorphEngine::prepare (morph_engine.cpp:47-67) never restores a mode.
- Context: the only callers of setMorphMode/setMorphSmoothingTime are processor_params.cpp:958-969 and processor_state.cpp:827-831 and neither stores the value; getState already writes literal 0 for both (processor_state.cpp:319, 323), so state never carried them; the registered smoothing default is 0 ms (parameter_registration.cpp:718-719) while the engine runs at 10 ms from the first prepare.
- Impact: in the default 4-corner layout Linear1D makes A/C and B/D equidistant for any Y, so after a sample-rate/block-size change or an offline-bounce setupProcessing (Cubase, Reaper) a 2D band renders with Y ignored while the UI still shows 2D; the state-ordering half adds little beyond D-046/D-048; medium.

### D-126 — Sweep LFO, envelope and modulation 'audio path' tests run the components directly; the processor's once-per-block calls are never tested

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** test-gap | **Dimension:** test-gaps | **Corroborated by measurement:** no (the hidden defects are D-035/D-036, measured)

**Location:** [plugins/disrumpo/tests/integration/modulation_audio_path_test.cpp:32-77](plugins/disrumpo/tests/integration/modulation_audio_path_test.cpp#L32-L77)

**Problem.** sweep_lfo_tests.cpp:150-190 checks rate by calling SweepLFO::process() once per sample, with zero-crossing margins of +/-1 (Approx(2).margin(1), i.e. 50%). Processor::process calls sweepLFO_.process() once per block and feeds the envelope follower one sample per block (processor.cpp:340-351). modulation_audio_path_test, despite its name and FR-093 tag, only drives a standalone Krate::DSP::ModulationEngine and getModulatedValue. It never constructs a Processor or BandProcessor and never measures audio.

**Why wrong.** The suite verifies each part, not how the processor clocks them. At 512-sample blocks the sweep LFO advances at 1/512 of its nominal rate, and the envelope follower sees 1 of every 512 samples. A per-sample unit test cannot see this.

**Consequence.** A 1 Hz sweep LFO actually cycles about every 8.5 minutes at 512-sample blocks, its rate changes with host buffer size, and the envelope sweep barely follows the input. All green.

**Evidence.** processor.cpp:340-342 `float inputMono = (inputL[0] + inputR[0]) * 0.5f; (void)sweepEnvelope_.processSample(inputMono);`; :349-350 `if (sweepLFO_.isEnabled()) { float lfoValue = sweepLFO_.process();` (once per block); sweep_lfo_tests.cpp:170 `REQUIRE(zeroCrossings == Approx(2).margin(1));`; modulation_audio_path_test.cpp:33-34 `ModulationEngine engine; engine.prepare(...)` with no Processor.

**Suggested fix.** Add TEST_CASE "Processor sweep LFO rate is independent of block size" in sweep_band_intensity_test.cpp. Enable sweep and sweep LFO at 2 Hz, render 2 s through Processor at block sizes 64 and 512, and read kSweepModulatedFrequencyOutputId from outputParameterChanges. REQUIRE 4 +/- 0.25 cycles at both sizes. Add a processor-level routing test: an LFO1->band0 gain routing set through params, with output block RMS varying by > 6 dB over 1 s.

**Verifier notes:**
- Correctness: processor.cpp:340-343 feeds inputL[0]/inputR[0] once per process() and :349-350 calls sweepLFO_.process() once; SweepLFO::process (sweep_lfo.h:201-206) and Krate::DSP::LFO::process (lfo.h:113-114, phase step per call at :159) assume one call per sample; SweepEnvelope::processSample (sweep_envelope.h:187-193) likewise; nothing scales by numSamples.
- Context: grep over plugins/disrumpo/tests for SweepLFO/kSweepLFO*/kSweepEnv*/kSweepModulatedFrequencyOutputId matches only tests/dsp/sweep_lfo_tests.cpp and sweep_envelope_tests.cpp, both per-sample component tests; modulation_audio_path_test.cpp:13-16 includes band_processor.h but every case builds a standalone ModulationEngine; the processor comment at :333-337 does not justify one tick per block.
- Impact: the gap hides a defect that makes the sweep LFO near-frozen (about 512 s per cycle at 1 Hz/512) and the envelope 32-512x slow at typical buffers; the ModulationEngine itself receives numSamples and is probably clocked right, so that part is a smaller gap; medium as a test-gap entry (the runtime bugs are D-035/D-036).

### D-125 — Latency tests cannot fail (static return 0) and carry [performance], so per-push lanes skip them; real wet-path latency is never measured

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** test-gap | **Dimension:** test-gaps | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/tests/dsp/oversampling_performance_tests.cpp:32-60](plugins/disrumpo/tests/dsp/oversampling_performance_tests.cpp#L32-L60), [:66-125](plugins/disrumpo/tests/dsp/oversampling_performance_tests.cpp#L66-L125), [:250-276](plugins/disrumpo/tests/dsp/oversampling_performance_tests.cpp#L250-L276)

**Problem.** 'getLatency returns 0 (IIR mode)' (SC-012), 'uses ZeroLatency oversampling mode' (FR-018), 'latency does not change when factors change' and 'end-to-end latency does not exceed 10ms' (SC-004) all assert BandProcessor::getLatency(), a `static ... return 0;` (band_processor.h:376-379). They carry the [performance] tag, which CI and run-gate.js exclude, although none of them is a timing test. No test measures the wet path's actual delay: Spectral outputs zero for fftSize samples, FeedbackDistortion returns only the delayed path, and Granular reads from the past. No test compares an impulse's peak position across bands or against the dry path.

**Why wrong.** A latency test has to measure an impulse response. Asserting a constant only re-states the claim. Tagging correctness cases [performance] also moves them into the isolated CPU lane, so they do not run per push.

**Consequence.** Unreported and uncompensated latency (Spectral fftSize at the 8x-prepared rate, 1-100 ms feedback delay) comb-filters against the dry path at mix < 100% and smears phase between bands. The latency claims stay green.

**Evidence.** band_processor.h:376-379 `[[nodiscard]] static int getLatency() noexcept { return 0; }`; oversampling_performance_tests.cpp:33 tags `"[oversampling][performance][SC-012]"`, :67 `[FR-018]`, :105 `[latency-stability]`, :254 `[SC-004]`

**Suggested fix.** Remove [performance] from the four correctness cases. Add TEST_CASE "Per-band wet latency matches reported latency" in processor_audio_output_test.cpp. For each of SoftClip, Spectral, FeedbackDist and Granular at mix 1: send an impulse through Processor::process, find the peak index of the output, and REQUIRE it equals the processor's reported latency (once getLatencySamples exists) and is equal across bands.

**Verifier notes:**
- Correctness: every assertion in the four cases calls the static getLatency (lines :40, :43, :46, :53, :58, :76, :112-123, :265/:272); the tags are as quoted; tools/ci-test-shard.js:112, tools/run-gate.js:52 and tools/run-close-lanes.js:42 exclude [performance] per push and only run-cpu-tests.js (:56) selects it; distortion_adapter.cpp:150/155/179/181 sets blockLatency_ for Spectral and Granular but getProcessingLatency() (:248) has no caller in src/ and its only test (distortion_adapter_test.cpp:127-149) checks == 0 or > 0.
- Context: spectral_distortion.h:490-493 latency() returns fftSize_ and :306 notes the warmup; a grep for impulse/peak-index/latency in plugins/disrumpo/tests finds only envelope and spectrum-peak tests; specs-overview.md:66/:374 claim "< 10ms" and "0 samples (1x) to 256 samples (8x)", which the SC-004 test proves only by dividing the static 0; the [performance] tag looks like a file-wide convention but still hides correctness cases.
- Impact: Spectral at fftSize 2048 is about 46 ms at 44.1 kHz with no PDC; the oversampling itself is ZeroLatency IIR (band_processor.h:99-107), so FR-018 holds for the oversamplers; the latency defect itself is D-098/D-081/D-026; medium as a test gap.

### D-124 — Crossover tests pin the missing spacing rule, measure band-count changes only after settling on DC, and never load state into an instance with fewer bands

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** test-gap | **Dimension:** test-gaps | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/tests/unit/crossover_network_test.cpp:402-417](plugins/disrumpo/tests/unit/crossover_network_test.cpp#L402-L417)

**Problem.** 'CrossoverNetwork minimum spacing constraint' sets crossovers at 1000 and 1200 Hz (0.26 octave) and REQUIREs that 1200 is kept, so the test asserts the violation of FR-BAND-002 (>= 0.5 octave, kMinCrossoverSpacingOctaves in band_state.h:69). IT-003 'Band count change maintains output stability' (band_management_integration_test.cpp:137-176) uses DC input and measures only after 4000 settle samples, so the switch transient is never observed. DC sums to 1 for any LR4 topology, so the assertion cannot fail. No test calls Processor::setState on an instance whose current band count is lower than the stream's. In that case setCrossoverFrequency silently drops index >= numBands_-1 (crossover_network.h:127-129) because processor_state.cpp:446-458 sets crossovers before setBandCount (:461-462).

**Why wrong.** These are the riskiest crossover behaviours: ordering, spacing, a mid-stream band-count change and a load-order dependency. The tests either assert the wrong behaviour or measure where the defect cannot appear. Every serialization test starts from a fresh 4-band processor (kDefaultBands = 4), so the drop never triggers.

**Consequence.** Loading a 4-band project into an instance set to 2 bands replaces the saved crossovers 2-3 with log midpoints. Band-count automation clicks. Crossover automation can cross adjacent frequencies and break reconstruction. All of this is unobserved.

**Evidence.** crossover_network_test.cpp:415-416 `network.setCrossoverFrequency(1, 1200.0f); REQUIRE_THAT(network.getCrossoverFrequency(1), WithinAbs(1200.0f, 0.01f));`; crossover_network.h:127-129 `if (index < 0 || index >= numBands_ - 1) { return; }`; processor_state.cpp:452-462 setCrossoverFrequency loop, then `crossoverL_.setBandCount(bandCount);`

**Suggested fix.** (1) Change the spacing test to REQUIRE f1 >= f0*2^0.5, or record a spec ruling that spacing is not enforced. (2) Add TEST_CASE "Band count 4->3 switch is click-free" with a 1 kHz sine through Processor: send kBandCountId mid-render and REQUIRE max |y[n]-y[n-1]| near the switch < 2x the steady-state maximum. (3) Add TEST_CASE "setState restores crossovers when current band count is lower": set band count 2 via params, setState with a 4-band stream at 200/1500/6000 Hz, then getState and REQUIRE all three frequencies within 1 Hz.

**Verifier notes:**
- Correctness: the spacing test's own comment (:412-414) says spacing is not enforced, which pins behaviour contradicting kMinCrossoverSpacingOctaves = 0.5f (band_state.h:69); IT-003 (band_management_integration_test.cpp:137-172) settles 4000 samples before and after and only checks |sum-1| < 0.1; processor_state.cpp:450-459 sets crossovers before setBandCount at :462-463 and crossover_network.h:127-129 drops indices above the live count, redistributeIncreasing (:301-358) filling them with log midpoints.
- Context (partial dissent): the DSP-level spacing test is a documented design decision because the 0.5-octave rule is enforced in the UI divider drag (crossover_interaction_test.cpp:155-176, T095), so sub-claim (1) is refuted on context; the other two parts stand, no test uses kBandCountId or setState into a lower-band instance (every setState test uses the default 4-band processor).
- Impact: the drop does not hit a fresh instance (default 4 bands); it hits a host .vstpreset load, A/B compare or state undo into a running instance at 1-3 bands, moving band edges by up to an octave while the UI shows the saved Hz and getState (processor_state.cpp:76) persists the wrong values; medium.

### D-123 — Keyboard, global-control and visibility tests are tautologies, and two of them pin an obsolete 8-band /7 mapping

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** test-gap | **Dimension:** test-gaps | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/tests/unit/global_controls_test.cpp:25-146](plugins/disrumpo/tests/unit/global_controls_test.cpp#L25-L146)

**Problem.** global_controls_test and visibility_test define lambdas such as `index / 7.0f` and `round(n*7)+1` and assert the lambdas' own arithmetic (e.g. 'Normalized 1.0 gives band count 8'). keyboard_shortcut_test.cpp:20-146 increments local ints (`focusedBandIndex++`) and asserts `REQUIRE(fineStep == 0.01f)` after assigning 0.01f, without ever constructing KeyboardShortcutHandler. band_strip_test.cpp:140-162 asserts a local string array against itself, never against getTypeName or the menu, and that array's order 14-25 differs from the enum.

**Why wrong.** The real Band Count is a 4-entry list with normalization k/3, and visibility uses threshold (b-0.5)/3 (controller.cpp:171, 1362). The tests encode the old 8-band design and would stay green whatever the controller does. A test that never calls production code verifies nothing.

**Consequence.** Real defects in this area have no coverage: KeyboardShortcutHandler::setActiveBandCount has no caller, the frame hook swallows arrows and Space typed into the preset-name field, and visibility thresholds could be wrong. The stale tests also tell maintainers the plugin has 8 bands.

**Evidence.** global_controls_test.cpp:68-71 `int index = static_cast<int>(std::round(normalized * 7.0f)); return index + 1;` and :82-84 `REQUIRE(normalizedToBandCount(1.0f) == 8);`; controller.cpp:1362 `float threshold = (static_cast<float>(b) - 0.5f) / 3.0f;`; keyboard_shortcut_test.cpp:91-94 `float fineStep = 0.01f; REQUIRE(fineStep == 0.01f);`

**Suggested fix.** Replace them with tests that call production objects. Construct KeyboardShortcutHandler with a stub frame, send a Tab VstKeyCode and REQUIRE the band focus advance; send an arrow key while a CTextEdit is focused and REQUIRE handled == false. For visibility, instantiate ContainerVisibilityController with the real Band Count StringListParameter from registerAllParameters and REQUIRE band b visible iff count > b for counts 1-4. For band_strip_test, compare against getTypeName(static_cast<DistortionType>(i)) and the registered NodeType list strings.

**Verifier notes:**
- Correctness: global_controls_test.cpp:25-58, :68-88 and :118-193 assert only their own /7.0f lambdas and the only production code they touch is makeGlobalParamId/isGlobalParamId (:94-108, :199-229); visibility_test.cpp uses the same /7.0f lambdas (:36, :83, :157, :208, :278) and includes only plugin_ids.h; keyboard_shortcut_test.cpp includes only plugin_ids.h (:12), never builds a KeyboardShortcutHandler and pins stepCount 7 at :133-141; band_strip_test.cpp:29-56 defines a local D-number-ordered array and :130-165 asserts it against itself.
- Context: production uses a 4-band mapping (parameter_registration.cpp:67-75, controller.cpp:171 `(count-1)/3`, :1362 `(b-0.5)/3`, :1716 `*3`); the band_strip array puts Allpass Resonant at 25 while the enum (distortion_types.h:32-63) has it at 19; nothing marks the tests as intentional.
- Impact: KeyboardShortcutHandler::setActiveBandCount has only its declaration and definition (keyboard_shortcut_handler.cpp:58, .h:41), so activeBandCount_ is fixed at construction (cpp:20) and bounds Tab cycling (cpp:107-118), a live defect with no test; no runtime harm from the tests themselves, but false coverage plus stale 8-band documentation; medium.

### D-122 — Disrumpo lacks the registered-defaults probe and any registration-vs-processor mapping test

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** test-gap | **Dimension:** test-gaps | **Also flagged by:** D-072, D-101 are the defects it would catch | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/tests/unit/controller/editor_lifecycle_test.cpp:1-50](plugins/disrumpo/tests/unit/controller/editor_lifecycle_test.cpp#L1-L50)

**Problem.** Ruinae, Seraphis, Vorago and Profundum each have a lifecycle case calling Krate::TestSupport::parametersStartingOffTheirDefault (tests/test_helpers/registered_defaults_probe.h), which 'each plugin's controller lifecycle test lists ... and requires none'. Disrumpo's editor_lifecycle_test has no such case. No Disrumpo test touches a registered Parameter object: grep for getParameterObject/registerAllParameters finds only display_format_test. Nothing compares Parameter::toPlain with the processor's denormalization.

**Why wrong.** Band Count and Oversample Max are StringListParameters whose start value comes from setNormalized(1.0) / setNormalized(2.0/3.0) while ParameterInfo.defaultNormalizedValue stays 0 (parameter_registration.cpp:76, 90). The probe flags exactly that. The 40 registration/processor mismatches in the params inventory (linear RangeParameter vs log2/log10/exp processor maps for sweep frequency, crossovers, LFO rates; 6-string Sweep Morph Link vs round(v*7)) all exist because no test pairs the two sides.

**Consequence.** Host 'reset to default' puts Band Count at 1 and Oversample at 1x. Host-displayed values disagree with the audio, e.g. sweep frequency default 1000 Hz is heard as ~28 Hz. None of this can go red.

**Evidence.** parameter_registration.cpp:76 `bandCountParam->setNormalized(1.0);  // Default to index 3`; :90 `oversampleParam->setNormalized(2.0 / 3.0);`; grep shows parametersStartingOffTheirDefault used in profundum/ruinae/seraphis/vorago editor_lifecycle_test.cpp and absent from plugins/disrumpo/tests

**Suggested fix.** Add TEST_CASE "Disrumpo parameters start on their registered default" calling parametersStartingOffTheirDefault(controller) with REQUIRE(mismatches.empty()). Add TEST_CASE "Registered plain value matches processor denormalization": for each continuous param, take norm n in {0, 0.25, 0.5, 1}, send it through processParameterChanges, read the processor value via a getter, and REQUIRE it is within 1% of controller param->toPlain(n). Start with sweep frequency, crossovers, LFO rates and morph link.

**Verifier notes:**
- Correctness: the probe is used by Gradus (editor_lifecycle_test.cpp:61), Vorago (:300), Iterum (editor_open_close_test.cpp:61), Profundum (:455), Seraphis (:498) and Ruinae (:57) and by nothing under plugins/disrumpo/tests; Disrumpo's lifecycle test has only the UAF cycle and the shared-views count; the only Disrumpo test reading a Parameter is display_format_test.cpp, which simulates toPlain by hand (:56).
- Context: the StringListParameter constructor sets info.defaultNormalizedValue = 0 (vstparameters.cpp:235) and no Disrumpo source writes defaultNormalizedValue, so the probe (registered_defaults_probe.h:33-35) would flag Band Count and Oversample Max; its header (:12) says every plugin's lifecycle test should run it.
- Impact: Sweep Frequency is a linear RangeParameter 20-20000 default 1000 (parameter_registration.cpp:183-189) whose default norm 0.049 the processor maps on log2 (processor_params.cpp:378-388) to about 28 Hz, and Controller::getParamStringByValue (controller.cpp:747-893) has no sweep branch so the host shows linear Hz (norm 0.5 displays 10010 Hz, sounds about 632 Hz); real, user-audible default and display defects hidden by the gap; medium.


### D-120 — Cursor-on-node tests check weights only; nothing covers the skipped transition-gain update in cross-family output, and SC-002 was relaxed to 3 dB, same-family only

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** test-gap | **Dimension:** test-gaps | **Also flagged by:** D-012 (the defect), D-155 (the follow-up measurement) | **Corroborated by measurement:** yes (D-155 measured the reachable paths)

**Location:** [plugins/disrumpo/tests/unit/morph_weight_computation_test.cpp:58-83](plugins/disrumpo/tests/unit/morph_weight_computation_test.cpp#L58-L83)

**Problem.** 'cursor at node position gives 100% weight' asserts only getWeights(). In calculateMorphWeights the on-node branch fills weights and returns before transitionGains_ is recomputed (morph_engine.cpp:188-192, 220-224, vs 266-269). processCrossFamily multiplies by the stale transitionGains_[i] (morph_engine.cpp:747-756). No test renders cross-family audio with the cursor parked exactly on a node after arriving from a distant position. morph_artifact_test 'Output level consistency during morph' (SC-002, spec says 1 dB) asserts diffMid < 3.0 dB on same-family nodes (morph_artifact_test.cpp:455-460).

**Why wrong.** The early return is exactly where a divide-by-zero guard can create a level or silence bug. With 0 ms smoothing and a jump from node B to node A, node A's gain keeps the value computed when its weight was below 0.001, which is 0. The band then goes silent while sitting on A. SC-002 needs a cross-family 1 dB assertion. The cross-family path sums w*gain with normalized weights, which gives about -3 dB at the 50/50 point.

**Consequence.** Users hear silence or a level dip when the morph cursor snaps onto a node, and a mid-morph level dip in cross-family blends, with no red test.

**Evidence.** morph_engine.cpp:188-192 `if (distances[i] < kOnNodeThreshold) { weights_.fill(0.0f); weights_[i] = 1.0f; return; }`; :266-269 the transitionGains_ loop sits after the return; :756 `output += nodeOutput * weight * gain;`; morph_artifact_test.cpp:459-460 `float diffMid = std::abs(levelMid_dB - avgRef_dB); REQUIRE(diffMid < 3.0f);`

**Suggested fix.** Add TEST_CASE "Cross-family cursor jump onto node keeps unity node level" in morph_transition_test.cpp. Nodes SoftClip + Bitcrush, smoothing 0, position 1.0 for 2000 samples, then 0.0. REQUIRE that the output RMS equals a single SoftClip adapter's RMS within 0.5 dB. Restore SC-002 to 1 dB with a cross-family node pair.

**Verifier notes:**
- Correctness: the stale gain mechanism is real (early returns at :188-192/:220-224 skip :267-270; :749 skips gain < 0.001), but the smoothing-0 cursor-jump scenario cannot happen: OnePoleSmoother clamps the time to 0.1 ms (smoother.h:82) and snaps only within 1e-4 (:197), so the last off-node update has A's weight about 0.99999 and gain 1; the same stale gain IS reachable through setNodes (:131-151) after a node-count change with the cursor parked on a corner (processor_params.cpp:899), and D-155 later measured the mode-switch path.
- Context: this verifier reads the smoother as accepting 0 ms (:125-129) and notes the registered smoothing default is 0 ms (parameter_registration.cpp:717-719, processor_state.cpp:323), so a pad click onto a corner would be an instant jump; the cross-family on-node renders in multi_node_distortion_test.cpp (1197-1222, 1284-1313) start from fresh engines with gains at 1 and assert only RMS > 0.01; AP-003 (approval/morph_artifact_test.cpp:377-460) uses createSameFamilySetup and asserts < 3.0f.
- Impact: a cross-family test does exist (morph_interpolation_test.cpp:258-306) with smoothing 0 and positions 0, 0.5, 1.0, where B's gain is the stale 0.707 (-3 dB), but it asserts a linear ratio < 3.0 (about 9.5 dB) and skips when minLevel <= 0.001, so it has no teeth; the gap is real under the default 0 ms smoothing; medium.

### D-119 — No test feeds different L and R signals through a morph-enabled band, so the shared L/R MorphEngine is untestable

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** test-gap | **Dimension:** test-gaps | **Also flagged by:** D-016 is the defect | **Corroborated by measurement:** yes (probe `stereo-leak`, see D-016)

**Location:** [plugins/disrumpo/tests/integration/band_management_integration_test.cpp:241-308](plugins/disrumpo/tests/integration/band_management_integration_test.cpp#L241-L308)

**Problem.** BandProcessor::process runs left and then right through ONE MorphEngine (band_processor.h:487-488). Every morph test feeds identical L/R values (e.g. morph_band_integration_test.cpp:103-104, 202-203; morph_artifact_test feeds input[i] to both and reads only left). The processor fixture uses identical sines on both channels. IT-006 'Full stereo signal path' does use 1 kHz / 500 Hz, but on BandProcessors that never get setMorphNodes. Those have morph disabled and legacy drive 0 (band_processor.h:116), so they pass the signal through untouched.

**Why wrong.** Interleaving L and R through one stateful engine causes crosstalk and halves filter time constants. With identical channels the interleaved state looks like a mono run, so the defect cannot show. A stereo test must use decorrelated channels with the live morph path enabled.

**Consequence.** Cross-channel bleed and a doubled cursor-smoother speed on every stereo source go undetected.

**Evidence.** band_processor.h:487-488 `left = morphEngine_->process(left); right = morphEngine_->process(right);`; band_management_integration_test.cpp:261-266 BandProcessors created with prepare() only and no setMorphNodes; :272-273 L 1000 Hz / R 500 Hz; band_processor.h:116 `defaultParams.drive = 0.0f;  // Bypass distortion by default`

**Suggested fix.** Add TEST_CASE "Morph band keeps L and R independent" in morph_band_integration_test.cpp. setMorphNodes with Tube + FeedbackDist, L = 440 Hz sine, R = silence; REQUIRE R RMS < -90 dBFS after settling, and REQUIRE that L matches a mono run of the same engine type within 1e-4. The same check at processor level: L sine, R zeros, REQUIRE output R ~ 0 at mix 1.

**Verifier notes:**
- Correctness: a single morphEngine_ (band_processor.h:808) and a single distortion_ (:803) serve both channels at :487-488, :493-494 and :753-754; MorphEngine::process (morph_engine.cpp:662-683) advances the cursor smoothers on every call; the processor calls setMorphNodes on every band (processor.cpp:126, processor_params.cpp:899/954, processor_state.cpp:894), so the shared engine is the live path.
- Context: IT-006's BandProcessors get only prepare() (:255-258), so with drive 0 (band_processor.h:116) :492 skips distortion; every setMorphNodes-calling test feeds identical channels (oversampling_crossfade_tests.cpp:67-68/114-115/123-124/202-203, oversampling_integration_tests.cpp:62-64/114-115, oversampling_limit_tests.cpp:208-209, morph_band_integration_test.cpp:266/283/310-311, processor_audio_output_test.cpp:158); no test asserts channel independence.
- Impact: the DC blocker and tone filter run through applyTone for most types (distortion_adapter.cpp:189-225, :583), so with R silent the R output carries about the same level as L; IT-006 would pass even with bleed because it only checks RMS > 0.1 and an L/R ratio about 1 (:303-308); medium as a test gap (the defect is D-016).

### D-118 — Factory-preset tests never render a preset and never compare the load against the preset file; 119 of 121 presets go through the v10 migration unchecked

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** test-gap | **Dimension:** test-gaps | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/tests/integration/preset/factory_preset_validation_test.cpp:147-169](plugins/disrumpo/tests/integration/preset/factory_preset_validation_test.cpp#L147-L169), [:171-249](plugins/disrumpo/tests/integration/preset/factory_preset_validation_test.cpp#L171-L249), [:251-269](plugins/disrumpo/tests/integration/preset/factory_preset_validation_test.cpp#L251-L269)

**Problem.** 'Factory presets load without error' only checks setState() == kResultOk. setState returns kResultOk for almost any stream after the globals, including a truncated band count (processor_state.cpp:408-411). The round-trip case compares processor output with processor output and allows up to 16 differing bytes. No preset is ever processed. I measured the 'Comp' chunk versions of the shipped presets: {v6: 1 (Init/Init_5_Bands), v8: 1 (Experimental/Formant_Shift), v10: 119}. So every preset loads through the v<=11 routing-destination migration (processor_state.cpp:775-790) and the v<=7 8-band path, and nothing checks the migrated values. The category test pins Init 5 / Experimental 16, which counts the two orphan files the generator does not emit. tools/disrumpo_preset_generator.cpp has 'Formant Shift' only under Morph.

**Why wrong.** SC-004 'all factory presets pass validation' should mean that each preset loads the intended parameters and produces finite, non-silent, bounded audio. The current test only shows that the reader does not crash.

**Consequence.** A preset that renders NaN, silence, runaway feedback or DC, or whose routings migrate to the wrong band slot, ships green. An 'Init 5 Bands' preset that can only produce 4 bands is protected by the count test.

**Evidence.** factory_preset_validation_test.cpp:165 `CHECK(result == Steinberg::kResultOk);` (the only assertion); :242 `CHECK(differingBytes <= 16);`; :256-259 expected counts `{"Init", 5} ... {"Experimental", 16}`; measured by a node script over the Comp chunks: `{"6":1,"8":1,"10":119}`, with v6 = Init_5_Bands.vstpreset and v8 = Experimental/Formant_Shift.vstpreset; generator grep: only `p.name = "Formant Shift"; p.category = "Morph";` (line 1674).

**Suggested fix.** Add TEST_CASE "Every factory preset renders finite bounded audio" (tag [long] if > 15 s). For each preset: setState, setActive(true), 2 s of pink noise at 44.1 kHz; REQUIRE that every sample is finite (use Krate::DSP::detail::isFinite), peak < +12 dBFS, |DC| < 0.01 and RMS > -60 dBFS. Add a migration case that reads one v10 preset's raw routing dests and REQUIRE that getState shows the remapped kBandBase + band*8 + param. Decide on the two orphan presets and fix the expected counts.

**Verifier notes:**
- Correctness: the load test's only assertion is `CHECK(result == kResultOk)` (:166) and no process() call exists in the file; setState returns kResultOk when the band count cannot be read (processor_state.cpp:407-411); the round-trip compares getState with getState and allows 16 differing bytes (:242); the version measurement reproduces as {"6":1,"8":1,"10":119}; the generator defines "Formant Shift" only as Morph (tools/disrumpo_preset_generator.cpp:1674) and Init only up to 4 bands (:829-844).
- Context: kPresetVersion = 12 (plugin_ids.h:690), so all 121 presets (not just 119) take the version <= 11 routing remap at processor_state.cpp:779-787; no test covers that migration (grep for v11/v12/kOldParamsPerBand finds nothing); other preset TUs use dummy presets; the header comments at :4/:8 still say 120 while :144 checks 121.
- Impact: Init_5_Bands loads with bandCount clamped to 4 (:411), so a preset named "5 Bands" gives 4 and the count test treats it as expected; no actual broken preset was shown, so the stated consequence is a risk rather than an observed defect; medium.

### D-109 — Crossover divider drag calls std::clamp with lo > hi when neighbouring crossovers are under one octave apart

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** portability | **Dimension:** portability-quality | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/views/spectrum_display.cpp:376-396](plugins/disrumpo/src/controller/views/spectrum_display.cpp#L376-L396)

**Problem.** leftBound is the left neighbour times 2^0.5 and rightBound is the right neighbour times 2^-0.5. When the two neighbours of the dragged divider are less than one octave apart, or a right neighbour is below 28 Hz for divider 0, leftBound > rightBound and std::clamp(newFreq, leftBound, rightBound) is called with inverted bounds. The view's crossover values are seeded from the controller parameters (controller.cpp:946-951, via linear toPlain). Those values can be arbitrary from automation or a preset, because neither the processor nor the parameters enforce ordering or spacing.

**Why wrong.** std::clamp has the precondition !(hi < lo). Violating it is undefined behaviour. MSVC debug STL and libstdc++ with _GLIBCXX_ASSERTIONS / libc++ hardened modes assert on it. In release builds every implementation returns lo when v < lo, so the divider jumps past its right neighbour.

**Consequence.** Debug builds assert during a mouse drag. In release, dragging produces crossover frequencies out of order (crossover[i] > crossover[i+1]). These are sent to the processor through CrossoverDragBridge, and the cascaded LR4 network then no longer reconstructs flat (SC-001).

**Evidence.** spectrum_display.cpp:382-386 `leftBound = crossoverFreqs_[draggingDivider_ - 1] * std::pow(2.0f, kMinOctaveSpacing);` :388-392 `rightBound = crossoverFreqs_[draggingDivider_ + 1] * std::pow(2.0f, -kMinOctaveSpacing);` :395 `newFreq = std::clamp(newFreq, leftBound, rightBound);`. spectrum_display.h:195 `kMinOctaveSpacing = 0.5f`. controller.cpp:946-951 seeds crossoverFreqs_ from params.

**Suggested fix.** If leftBound > rightBound, either reject the move or collapse both bounds to the midpoint, then clamp. Also observe the crossover parameters so the view never starts from an unordered state. Test: SpectrumDisplay unit test with setCrossoverFrequency(0,500), (1,600), (2,700); drag divider 1; assert the result is ordered and no std::clamp precondition is violated.

**Verifier notes:**
- Correctness: :383-393 build the bounds with no lo <= hi check and :396 calls std::clamp; SpectrumDisplay::setCrossoverFrequency (:69-74) only clamps each value to [20, 20000]; the seed at controller.cpp:947-952 uses the linear RangeParameter toPlain while CrossoverDragBridge (visibility_controllers.cpp:643-652) writes log-scaled norms, so after a drag to 100 Hz and an editor reopen divider 0 is seeded at about 4675 Hz, above divider 2 at 3557 Hz, and dragging divider 1 inverts the bounds; one detail is wrong, MSVC's release clamp tests hi < v first so it does not always return lo.
- Context: nothing orders the view's values (the processor only clamps per value at processor_params.cpp:984-994 and crossover_network.h:127-134); normalized values of 0.5 or above (any crossover at or above about 632 Hz) land in 10010-20000 Hz in the view, a span under one octave, so a 700/2000/6000 Hz preset gives inverted bounds on divider 1; crossover_interaction_test.cpp:70-86 reimplements the bound logic and never drives onMouseMoved.
- Impact (dissent on severity): the SC-001 claim is wrong because the LR4 cascade with D'Appolito allpasses sums to AP0*AP1*AP2 regardless of ordering (crossover_network.h:209-227), so magnitude stays flat; the user gets a divider that jumps past its neighbour and a nearly empty middle band, a UI/UX bug; the larger defect is the linear-vs-log seeding (D-086); this verifier recommended low; medium retained (one of three).

### D-108 — Processor reports kNoTail although several types ring after input stops

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** portability-quality | **Corroborated by measurement:** partly (probe `tail:FeedbackDist` -47.9 dBFS 1 s after input stops)

**Location:** [plugins/disrumpo/src/processor/processor.h:130-292](plugins/disrumpo/src/processor/processor.h#L130-L292) (no getTailSamples override)

**Problem.** Processor does not override getTailSamples, so it inherits AudioEffect's `return kNoTail;` (vstaudioeffect.h:84). Several shipped types sustain output after the input goes silent: FeedbackDistortion (delay line with feedback up to 1.5), AllpassSaturator KarplusStrong/feedback topologies (feedback up to 0.999), the Granular circular buffer, Spectral overlap-add, and the Stochastic/Chaos shapers.

**Why wrong.** IAudioProcessor::getTailSamples tells the host how long output continues after silent input. Hosts use it to stop calling process() on silent tracks (silence suspend/auto-bypass) and to size offline render tails.

**Consequence.** Feedback/allpass/granular tails are truncated in exports and when the host suspends silent tracks. When processing resumes, stale buffer contents (granular grains, feedback delay, spectral OLA) play back as a burst. Behaviour depends on the host and its suspend settings.

**Evidence.** grep for getTailSamples / canProcessSampleSize in plugins/disrumpo/src: no hits. extern/vst3sdk/public.sdk/source/vst/vstaudioeffect.h:84 `uint32 PLUGIN_API getTailSamples () SMTG_OVERRIDE { return kNoTail; }`.

**Suggested fix.** Override getTailSamples to return a bound derived from the active types: the maximum feedback decay plus the delay, the granular buffer length and the FFT size, or kInfiniteTail while any feedback-capable node is active. Test: a processor-level case that calls getTailSamples() after loading a FeedbackDist preset and asserts a value > 0.

**Verifier notes:**
- Correctness: Processor derives from AudioEffect (processor.h:43) and no getTailSamples/kNoTail/kInfiniteTail appears under plugins/disrumpo; the adapter holds FeedbackDistortion (distortion_adapter.h:378, feedback set at distortion_adapter.cpp:813, kMaxFeedback = 1.5 at feedback_distortion.h:98, "indefinite" sustain noted at :40), GranularDistortion (:383) and AllpassSaturator (:387, setFeedback at :826); the processor never checks input silence flags (processor.cpp:196 only handles numSamples == 0).
- Context: no comment keeps the SDK default on purpose (unlike Seraphis processor.cpp:1776-1781); Iterum returns kInfiniteTail (processor.h:90) and Vorago (processor.cpp:700) and Profundum (processor.cpp:574) compute estimates with tests, so the project treats tail reporting as required; FeedbackDist feedback goes to 1.5 (processor_params.cpp:177-180) and allpassFeedback up to 0.99 with decay up to 10 s (:284-285).
- Impact: only presets using FeedbackDist, AllpassSaturator, Granular or Spectral are affected, and the "burst on resume" claim is overstated (a suspended host just stops calling process, so leftover content plays out as the rest of the cut tail); truncated multi-second tails in hosts with silence suspend (Cubase/Nuendo) justify medium.

### D-101 — FR-SWEEP-002 / Appendix A sweep and band-count parameters disagree with the spec at the host boundary (range, scale, defaults)

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** spec-conformance | **Dimension:** spec-conformance | **Also flagged by:** D-072, D-088, D-122 | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/parameter_registration.cpp:64-76](plugins/disrumpo/src/controller/parameter_registration.cpp#L64-L76), [:182-211](plugins/disrumpo/src/controller/parameter_registration.cpp#L182-L211)

**Problem.** Sweep Frequency is a linear RangeParameter 20-20000 Hz with default 1000 (comment says 'log scale'), while the processor decodes the normalized value on a log2 scale (processor_params.cpp:381-385). Sweep Intensity is registered 0-100 % with default 50, while the DSP maps norm*2 (0-200 %, processor_params.cpp:404-406). Sweep Width defaults to 1.0 oct, while Appendix A and the processor atomics (processor.h, 0.286 for 1.5 oct) say 1.5. Band Count calls setNormalized(1.0), but ParameterInfo.defaultNormalizedValue stays 0, so the host default is 1 band (Appendix A: 4).

**Why wrong.** FR-SWEEP-002 specifies Intensity 0-200 %. Appendix A specifies Sweep Freq default 1 kHz, Width default 1.5 oct, Intensity default 50 % and Band Count default 4. The host-facing normalized-to-plain mapping must equal the processor's denormalization, or host readouts and reset-to-default are wrong.

**Consequence.** Host 'reset to default' on Sweep Frequency sends 0.049, which the processor plays as ~28 Hz, not 1 kHz. Host automation lanes and readouts show linear Hz for a log control. Sweep Intensity displays 100 % where the DSP applies 200 %, and its 50 % default is DSP intensity 1.0 while the engine starts at 0.5. Band Count reset gives 1 band.

**Evidence.** parameter_registration.cpp:182 comment 'log scale' vs :183-188 `new RangeParameter(... 20.0, 20000.0, 1000.0 ...)`; :197-199 `0.5, 4.0, 1.0`; :203-210 `0.0, 100.0, 50.0` unit %; :75 `bandCountParam->setNormalized(1.0)`; processor_params.cpp:384 `kSweepLog2Min + value * kSweepLog2Range`; :405 `intensity = value * 2.0f`; specs-overview.md Appendix A rows Sweep Freq 1 kHz, Sweep Width default 1.5 oct, Sweep Intensity 0%-200% default 50%, Band Count default 4.

**Suggested fix.** Register these as log-mapped parameters (a Parameter subclass whose toPlain/toNormalized use the same log2 / log10 formula the processor uses), register Intensity as 0-200 %, set Width's default to 1.5 oct, and construct list params with the intended default index so defaultNormalizedValue is right. Add a test that iterates the sweep, crossover and band-count params and asserts processor-decoded plain(param->getInfo().defaultNormalizedValue) equals the Appendix A default.

**Verifier notes:**
- Correctness: the RangeParameter ctor sets defaultNormalizedValue = toNormalized(1000) = 0.049 (vstparameters.cpp:151) which the processor decodes as 2^(4.3219 + 0.049*9.9658) = about 28 Hz; the controller's own state fallback (controller.cpp:219, 0.566) and the processor atomic (processor.h:224) assume log; the intensity default norm 0.5 yields 1.0 while baseSweepIntensityNorm_ starts at 0.25 (processor.h:230); width default 1.0 oct (norm 0.143) vs atomic 0.286 (processor.h:227); the StringListParameter ctor sets defaultNormalizedValue = 0 (vstparameters.cpp:235) and setNormalized(1.0) changes only the value.
- Context: the controller's SweepIndicator decodes log2 too (controller.cpp:1281-1286), so only the host-facing toPlain/toString is linear; no test checks defaultNormalizedValue or the sweep registration mapping; specs-overview.md:399/:407-409 confirm the Appendix A values.
- Impact: generic-UI readouts are wrong in every host (processor at 1 kHz, norm 0.566, displays about 11.3 kHz), reset-to-default sends the sweep to about 28 Hz and collapses the plugin to 1 band in hosts that use defaultNormalizedValue (Cubase, Reaper, Bitwig); the frequency error is heard only with Sweep on, the band-count reset in every configuration; medium.

### D-100 — 1D Linear mode cannot address C and D (FR-MORPH-002 'A<->B<->C<->D'): node positions are fixed at the corners, so C aliases A and D aliases B

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** morph | **Dimension:** spec-conformance | **Also flagged by:** D-029 (morph-oversampling) | **Corroborated by measurement:** partly (D-155 measured the constant -3 dB at the Linear1D axis ends)

**Location:** [plugins/disrumpo/src/processor/processor.cpp:114-121](plugins/disrumpo/src/processor/processor.cpp#L114-L121)

**Problem.** Node positions are only set in setupProcessing: A(0,0), B(1,0), C(0,1), D(1,1). No processor path ever changes posX/posY (grep 'posX =' in processor/ and dsp/ finds nothing), and node positions are not in the state stream (processor_state.cpp:834-861 reads only type/common/bias/folds/bitDepth). Linear1D (morph_engine.cpp:180-210) uses posX only, so C shares x=0 with A and D shares x=1 with B. The on-node early return at :188-192 always picks the first index.

**Why wrong.** FR-MORPH-002 defines 1D Linear as 'Single axis A<->B<->C<->D', and the MorphMode doc says 'Single axis A-B-C-D interpolation using morphX only' (distortion_types.h:378). With 3-4 nodes the axis should visit each node in turn.

**Consequence.** In 1D mode with 3 or 4 nodes, C and D are never reached distinctly: at x=0 only A sounds, at x=1 only B, and in between A/C and B/D are always mixed in equal pairs. Dragging nodes on the MorphPad (morph_pad.cpp keeps its own copy) changes nothing audible in any mode.

**Evidence.** processor.cpp:114 `MorphNode(0, 0.0f, 0.0f, ...)`, :116 `MorphNode(1, 1.0f, 0.0f, ...)`, :118 `MorphNode(2, 0.0f, 1.0f, ...)`, :120 `MorphNode(3, 1.0f, 1.0f, ...)`; morph_engine.cpp:185 `distances[i] = calculate1DDistance(posX, nodes_[i].posX);` :188-192 on-node early return picks first i; distortion_types.h:378 Linear1D doc.

**Suggested fix.** For Linear1D, place active nodes at posX = i/(N-1) along the axis (or define a 1D order independent of the XY corners), or transmit node positions from the editor as parameters and persist them. Add a test: 3 nodes (SoftClip, SineFold, Bitcrush) in Linear1D at X=0.5. Assert that weight[1] (B) is about 1 and weight[2] (C) is distinct from weight[0].

**Verifier notes:**
- Correctness: posX/posY writes exist only in the default constructors (processor.cpp:114-120, morph_engine.cpp:41-44, band_state.h:49-52) and in the editor's own copy (morph_pad.cpp:169-172, 206-207), which never reaches the processor; morph_pad.cpp:764 calls listener_->onNodePositionChanged but no controller implements it; Linear1D uses nodes_[i].posX only (:185) and the early return at :188-192 returns at the first match.
- Context: kBandMorphMode (processor_params.cpp:964-968) and kBandActiveNodes (plugin_ids.h:185, 2-4) are host-exposed so 1D with 3-4 nodes is reachable; custom-controls.md:1013-1016, specs-overview.md:233, dsp-details.md:151 and distortion_types.h:378 all describe a sequential axis; morph_mode_test.cpp:43-50 builds its own 3-node line (0, 0.5, 1), so the processor's corner layout in 1D is untested; the pad only pins the cursor Y to 0.5 in 1D (morph_pad.cpp:804-806).
- Impact: 1D Linear is the default mode (parameter_registration.cpp:688-690, band_state.h:42, morph_engine.h:220), so anyone raising Active Nodes to 3 or 4 without changing mode cannot isolate two of their four configured types; no NaN, click or safety issue; medium.

### D-099 — Morph smoothing (FR-MORPH-003, 0-500 ms) runs about 4x slower than set, and host session save drops smoothing and morph mode

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** morph | **Dimension:** spec-conformance | **Also flagged by:** D-138 (measured), D-048 (state) | **Corroborated by measurement:** yes (probe `morph-smoothing:default`, 37.33 ms)

**Location:** [plugins/disrumpo/src/dsp/morph_engine.cpp:122-126](plugins/disrumpo/src/dsp/morph_engine.cpp#L122-L126), [:666-669](plugins/disrumpo/src/dsp/morph_engine.cpp#L666-L669)

**Problem.** MorphEngine::setSmoothingTime configures smootherX_/Y_ with sampleRate_, which is 8x the host rate (prepared from band_processor.h:124). process() advances both smoothers on every call, and BandProcessor calls it twice per host sample (L then R, band_processor.h:487-488). The net time constant is 8/2 = 4x the setting. In addition, Processor::getState writes a literal 0 for morphMode and 0.0f for morphSmoothing (processor_state.cpp:319-323), which setState then applies (:825-831).

**Why wrong.** FR-MORPH-003 specifies 'Configurable smoothing time: 0ms to 500ms'. The configured time should be the real-time glide, and the value must survive a project reload.

**Consequence.** A 500 ms smoothing setting glides for about 2 s, and the default 10 ms for about 40 ms. After any host project reload, every band's Morph Mode returns to 1D Linear and Smoothing to 0 ms, while the controller still shows the saved values, so the UI and the audio disagree.

**Evidence.** morph_engine.cpp:123-125 `smootherX_.configure(smoothingTimeMs_, static_cast<float>(sampleRate_));`; :667-668 `smootherX_.process(); smootherY_.process();` per call; band_processor.h:124 prepare at `sampleRate * kMaxOversampleFactor`, :487-488 two process() calls per sample; processor_state.cpp:319 `writeInt8(0)  // morphMode`, :323 `writeFloat(0.0f)  // morphSmoothing`.

**Suggested fix.** Configure the position smoothers at the rate at which process() advances them, and advance them once per stereo frame (split the smoother tick from per-channel processing, or add a stereo process). Write cache.morphMode / smoothing into getState from stored per-band values. Add tests: (1) BandProcessor with smoothing 100 ms, step X 0 to 1, and assert getSmoothedX() is at least 0.99 after 100 ms of host samples; (2) Processor state round-trip with mode Planar2D and smoothing 250 ms preserved.

**Verifier notes:**
- Correctness: band_processor.h:124 prepares at 8x (kMaxOversampleFactor at :47), morph_engine.cpp:48-52/:125-129 configure the smoothers at that rate, process() advances them per call (:667-669), and the processor always takes the direct 1x path (processWithFactor bypasses oversampling when distortion_.drive < 0.0001, band_processor.h:629-635, drive set to 0 at :115-119 and never changed), so the smoothers tick 2x per host sample against a coefficient built for 8, exactly 4x slow; the engine-level tests (morph_transition_test.cpp:30-31) prepare the engine at the host rate and cannot catch it.
- Context: on the oversampled paths the factor is 8/(2*OS) (2x slow at 2x, correct at 4x, 2x fast at 8x), so the glide time would also change when the factor switches; processor_state.cpp:319/:323 write literal zeros and setState applies them (:827-831); the controller's preset writer (controller.cpp:1829-1834) keeps the real values.
- Impact: one correction, Controller::setComponentState parses the same stream and sets kBandMorphMode/kBandMorphSmoothing from the zeros (controller.cpp:513-529), so after a reload the UI also shows Linear/0 ms and the loss is silent rather than a UI/audio mismatch; user-audible timing violation plus state loss on every project reload; medium.

### D-098 — Latency NFR (0-256 samples, < 10 ms) is violated and unreported: Spectral adds 512-4096 samples, Feedback/Granular add a delay, and no getLatencySamples exists

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** spec-conformance | **Dimension:** spec-conformance | **Also flagged by:** D-081 (shared-dsp-contracts), D-026 (signal-path) report the same gap | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.cpp:148-156](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L148-L156), [:172-182](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L172-L182), [:565-567](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L565-L567), [:893-896](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L893-L896)

**Problem.** The Spectral type runs SpectralDistortion per sample, and its latency() equals fftSize (spectral_distortion.h:490-494). processor_params.cpp:244 maps the FFT size to 512-4096. The adapter records blockLatency_ (fftSize, grain samples) but getProcessingLatency (:248) has no caller. Processor has no getLatencySamples override (grep in processor.h/.cpp is empty), and BandProcessor::getLatency is a hard-coded 0 (band_processor.h:376-379).

**Why wrong.** specs-overview.md NFR 'Latency: 0 samples (1x) to 256 samples (8x)' and success criterion 'Latency < 10ms at highest quality'. Any intrinsic latency must be reported to the host for PDC and aligned between bands and against the dry path.

**Consequence.** One Spectral node at the default fftSize 2048 delays that band by 2048 samples (46 ms at 44.1 kHz; 4096 gives 93 ms), with no host compensation. The band arrives late against the other bands (smeared transients at summation) and against the dry path (comb filtering whenever Global Mix or node Mix is below 100%). The track also drifts out of sync with the rest of the session.

**Evidence.** distortion_adapter.cpp:150 `blockLatency_ = typeParams_.fftSize;`; :248-250 getProcessingLatency (no callers; grep 'getProcessingLatency' in src finds only the definition and declaration); :567 `return spectral_.process(input);`; spectral_distortion.h:492-494 `latency() ... return fftSize_;`; band_processor.h:376-379 `static int getLatency() noexcept { return 0; }`; processor_params.cpp:244 `p.fftSize = 512 * (1 << ...); // 512-4096`.

**Suggested fix.** Either constrain Spectral to a latency within spec, or report the worst-case latency through getLatencySamples and restartComponent(kLatencyChanged) when it changes. Delay-compensate the other bands and the dry path to the max band latency. Add a test: one band Spectral at mix 100%, impulse in. Assert that the measured output peak offset equals Processor::getLatencySamples() (it reads 0 today against an actual ~2048-sample delay).

**Verifier notes:**
- Correctness: SpectralDistortion::process (spectral_distortion.h:217-243) pushes one sample into the STFT, analyses once a full window is available and returns 0.0f until the overlap-add has output, so the wet output lags by about fftSize; grep for getLatencySamples/kLatencyChanged/setLatency in plugins/disrumpo finds nothing in src/processor; getProcessingLatency is called only from distortion_adapter_test.cpp:133-149; the adapter's mix at :235 blends the undelayed dry with the delayed wet.
- Context: the retired spec 003 (git b6fe8faa7, spec.md:985) says block-based latency is "queryable via getProcessingLatency()" and "reported to the host", so reporting was intended and never wired; specs-overview.md:66/:374 contradict a 2048-sample delay; the Feedback and Granular parts are weaker because their delay is arguably the effect (feedback_distortion.h:338-340 is commented as deliberate).
- Impact: Spectral is an opt-in Experimental type and the default path has no latency, but when selected it adds 12-93 ms (46 ms default) with no PDC, misaligned against the other bands and combing against dry below 100 % mix; the Spectral case alone sustains medium.

### D-089 — Morph inverse-distance weights are implemented three times with different epsilons, on-node handling and mode support

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** duplication | **Dimension:** duplication-drift | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/band_processor.h:576-614](plugins/disrumpo/src/dsp/band_processor.h#L576-L614)

**Problem.** MorphEngine::calculateMorphWeights (morph_engine.cpp:170-270) handles the three MorphModes, uses eps 1e-6 and a one-hot on-node snap, then zeroes and renormalises weights below the 0.001 threshold. BandProcessor::computeRawMorphWeights (band_processor.h:576-614) is a second copy: always 2-D Euclidean whatever the mode, eps 1e-8, no threshold step. It drives the oversampling factor. MorphPad::recalculateWeights (morph_pad.cpp:833-868) is a third copy: always 2-D, std::pow(distance, 2), and a cursor on a node gets raw weight 1000 instead of one-hot. It drives node brightness (morph_pad.cpp:522) and connection alpha (:491).

**Why wrong.** The weights the engine applies are the ground truth. The oversampling selector and the UI display both claim to reflect them, so they should call the same function with the same mode.

**Consequence.** In Linear1D or Radial2D modes, the morph pad's node brightness shows Planar2D weights that differ from what is heard (Linear1D uses X only, Radial uses angle). With the cursor on node A and B at distance 1, the pad shows A at 0.999 and B at 0.001 rather than 1/0. If oversampling is ever wired in, the factor selection would use the wrong weights in non-planar modes.

**Evidence.** morph_engine.cpp:20 `constexpr float kDistanceEpsilon = 1e-6f;` and the :180-242 mode switch; band_processor.h:584-585 `kOnNodeThreshold = 0.001f; kDistanceEpsilon = 1e-8f;` with `const float dist = std::sqrt(dx*dx + dy*dy)` (2-D only); morph_pad.cpp:848-852 `if (distance < kMinDistance) { rawWeights[i] = 1000.0f; } else { rawWeights[i] = 1.0f / std::pow(distance, kDistanceExponent); }`

**Suggested fix.** Expose a pure static function, e.g. computeMorphWeights(mode, nodes, count, x, y) -> array, from morph_engine.h (or morph_node.h) that holds the mode switch, epsilon, on-node snap and threshold. Call it from MorphEngine, BandProcessor and MorphPad (the pad also needs the band's MorphMode). Test: for each mode and a grid of cursor positions, all three call sites return identical weights.

**Verifier notes:**
- Correctness: the three implementations exist as described; BandProcessor::setMorphMode (:260-264) forwards the mode to the engine and keeps no copy, so computeRawMorphWeights cannot know it; recalculateOversampleFactor (:393-411) calls it today (from setMorphPosition :277, setMorphEnabled :324, setMaxOversampleFactor :366), so the "if oversampling is ever wired in" hedge understates it; the pad stores morphMode_ (:192-195) but recalculateWeights never reads it and pins the cursor to Y = 0.5 in Linear1D (:672-674).
- Context: the archived 009 plan (plan.md:256-261) records a separate computeRawMorphWeights as a deliberate deviation to bypass the audio-rate smoother, but says nothing about ignoring MorphMode, the epsilon or the threshold; no spec documents the pad's planar-only display; no test compares the three call sites.
- Impact: in Linear1D or Radial2D the oversampling factor is selected today from Planar2D weights that differ from what the engine applies (e.g. Radial2D at (0.6, 0.5) gives one node almost all audible weight while the selector sees roughly even weights), so a band can run one power-of-2 factor low (more aliasing) or high (CPU); the pad's on-node 0.999/0.001 display error is visually negligible; medium.

### D-088 — Sweep frequency and width denormalisation is copied in about 8 places; the only odd one out is the host-facing registration, and the 0.286 width default is a copy-paste formula slip

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** duplication | **Dimension:** duplication-drift | **Also flagged by:** D-101 | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor_params.cpp:381-397](plugins/disrumpo/src/processor/processor_params.cpp#L381-L397)

**Problem.** The log2 20-20k sweep mapping is re-declared as local constants in processor_params.cpp:381-384, controller.cpp:1280-1285, sweep_lfo.h:232-233, sweep_envelope.h:221-222 and sweep_morph_link.h:38-41. It also appears as natural-log (morph_link.cpp:80-98) and kLogRatio (sweep_indicator.h:155, spectrum_display.h:192) variants. The processor itself inlines the LFO octave-shift math at processor.cpp:347-354 instead of calling SweepLFO::getModulatedFrequency, which duplicates it. The width range 0.5..4 is written as local kMinWidth/kMaxWidth at processor.cpp:280-282, processor_params.cpp:394-397, controller.cpp:1287-1290 and visibility_controllers.cpp:548-551 and :587-590, and as literals at processor_state.cpp:89 and :486. That is despite kMinSweepWidth/kMaxSweepWidth existing at sweep_processor.h:46-49. The one place that does not follow the log mapping is the RangeParameter registration (linear 20..20000 Hz).

**Why wrong.** A parameter's normalized<->plain mapping must be defined once and shared by registration, processor, state and UI. The atomic default baseSweepWidthNorm_{0.286f} (processor.h:227) equals 1.0/3.5, the width formula with the -0.5 offset dropped. The intended 1.0 oct is (1.0-0.5)/3.5 = 0.143, so 0.286 gives 1.5 oct. That is the kind of slip a single helper prevents.

**Consequence.** The host shows linear Hz for a log-decoded parameter: default 1000 Hz becomes norm 0.049, which the processor reads as ~28 Hz. A fresh instance runs at 1.5 oct width while the registration says 1.0 oct. Seven copies have to be kept in sync by hand for any future range change.

**Evidence.** processor_params.cpp:381-384 `constexpr float kSweepLog2Min = 4.321928f; ... pow(2, log2Freq)`; controller.cpp:1280-1285 identical local copy; sweep_lfo.h:232-233 and sweep_envelope.h:221-222 `constexpr float kLog2Min = 4.321928f;`; processor_state.cpp:89 `(sweepProcessor_.getWidth() - 0.5f) / 3.5f`; sweep_processor.h:46-49 `kMinSweepWidth = 0.5f; kMaxSweepWidth = 4.0f`; parameter_registration.cpp:183-188 `new RangeParameter(STR16("Sweep Frequency"), ..., 20.0, 20000.0, 1000.0, 0, kCanAutomate)`.

**Suggested fix.** Use sweep_morph_link.h's normalizeSweepFrequency/denormalizeSweepFrequency and new width helpers built on sweep_processor.h's kMinSweepWidth/kMaxSweepWidth everywhere. Register Sweep Frequency as a Parameter subclass whose toPlain/toNormalized call those helpers, so host text matches the DSP. Set the default norm with the helper. Test: for every sweep param, registration toPlain(defaultNormalized) must equal the processor's decoded value for the same norm.

**Verifier notes:**
- Correctness: the frequency mismatch is real (linear RangeParameter at parameter_registration.cpp:182-191, log2 decode at processor_params.cpp:381-388, no sweep branch in getParamStringByValue so the default norm 0.049 decodes to about 28 Hz and the processor's 0.566 displays as about 11.3 kHz); the duplication sites exist as listed; the width-default sub-claim has its cause backwards: sweep_processor.h:52 sets kDefaultSweepWidth = 1.5f and specs-overview.md:408 lists 1.5 oct, so 0.286 = (1.5-0.5)/3.5 is correct and the registration default of 1.0 oct (parameter_registration.cpp:198) is the slip.
- Context: no test pins the registration mapping (parameter_encoding_test.cpp checks only IDs); the shared normalizeSweepFrequency/denormalizeSweepFrequency helpers already exist and are used at processor.cpp:270-273 and processor_state.cpp:481; sweep is off by default (processor_state.cpp:471).
- Impact: reset-to-default sends the sweep centre to about 28 Hz and the host text is wrong across the whole range; a fresh instance runs 1.5 oct while the UI shows 1.0 oct until a state round-trip or parameter change syncs them; medium.

### D-087 — MorphLinkMode curves have two implementations with opposite meanings for the same enum and labels

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** duplication | **Dimension:** duplication-drift | **Also flagged by:** D-107, D-040 report the same pair from other dimensions | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/morph_link.cpp:27-68](plugins/disrumpo/src/controller/morph_link.cpp#L27-L68)

**Problem.** controller/morph_link.cpp applyMorphLinkMode and dsp/sweep_morph_link.h applyMorphLinkCurve both switch on Disrumpo::MorphLinkMode but implement different curves. EaseIn is sqrt(x) in the controller and x^2 in the DSP. EaseOut is x^2 in the controller and 1-(1-x)^2 in the DSP. HoldRise breaks at 0.5 in the controller and 0.6 in the DSP. Stepped has 5 levels (0, .25, .5, .75, 1) in the controller and 4 levels (0, .333, .667, 1) in the DSP. None returns the manual position in the controller and 0.5 in the DSP. Each copy has its own test file pinning its own version (tests/unit/morph_link_test.cpp vs tests/dsp/sweep_morph_link_tests.cpp).

**Why wrong.** One enum with one set of user-facing names ('Ease In', 'Ease Out', ...) and one doc comment (plugin_ids.h:644-647, which describes the controller's 5-step Stepped) must map to one curve. Today the controller copy drives the band Morph X/Y Link (MorphSweepLinkController, visibility_controllers.cpp:194-195). The DSP copy is reached only through SweepProcessor::getMorphPosition, which has no caller.

**Consequence.** 'Ease In' means the opposite curve depending on which path reads it. Wiring the sweep-side morph link (kSweepMorphLink) to getMorphPosition, the obvious fix for that dead feature, would make the same label behave differently from the band X/Y links. The two divergent tests guarantee neither copy is ever reconciled.

**Evidence.** morph_link.cpp:44-48 `case MorphLinkMode::EaseIn: return std::sqrt(sweepNorm);`, :53 `EaseOut: return sweepNorm * sweepNorm;`, :56-59 HoldRise `< 0.5f`, :65 `std::floor(sweepNorm * 5.0f) / 4.0f`; sweep_morph_link.h:99-100 `EaseIn: return x * x;`, :103-104 `EaseOut: 1 - (1-x)^2`, :109-112 HoldRise `x < 0.6f`, :118 `std::min(std::floor(x * 4.0f), 3.0f) / 3.0f`; grep: getMorphPosition has no caller outside sweep_processor.h.

**Suggested fix.** Keep one implementation, the DSP-side header that the controller already includes (controller.cpp:28, visibility_controllers.cpp:9), with the 'manual position' semantics as a parameter. Delete controller/morph_link.cpp's curve switch, and merge the two test files into one table-driven test over every MorphLinkMode.

**Verifier notes:**
- Correctness: every quoted line checks out (morph_link.cpp:34/:47/:52/:56-59/:65/:67-68 vs sweep_morph_link.h:88/:100/:104/:109-112/:118/:120-124); the only production caller of applyMorphLinkMode is visibility_controllers.cpp:194-195, applyMorphLinkCurve is reached only via SweepProcessor::getMorphPosition (sweep_processor.h:297-308) which no src code calls; both copies have their own pinning tests.
- Context: the enum comments (plugin_ids.h:644-647) describe the controller version while dsp-details.md:1264/:1268 describe the DSP version, so the docs disagree with each other; in the controller kSweepMorphLink only drives custom-curve visibility (controller.cpp:1492-1495).
- Impact (dissent on severity): only one curve set reaches users today, so the "opposite meaning" is latent until the sweep-side link is wired; this verifier recommended low; medium retained (one of three).

### D-081 — Wet-path latency of Spectral (fftSize), Granular and FeedbackDist is never reported or compensated

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** shared-dsp-contract | **Dimension:** shared-dsp-contracts | **Also flagged by:** D-098, D-026 | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.cpp:248-250](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L248-L250)

**Problem.** The adapter computes blockLatency_ for Spectral (fftSize) and Granular (grain samples), but getProcessingLatency() has no caller anywhere in plugins/disrumpo/src. BandProcessor::getLatency is a static that returns 0, and the processor never calls setLatencySamples. SpectralDistortion outputs 0 until the first frame and then lags by fftSize (spectral_distortion.h:225-246). FeedbackDistortion returns only the delayed feedback path, never x (feedback_distortion.h:338-340), so its wet signal is delayed by delayMs.

**Why wrong.** Primitives with internal buffering must have their latency summed into the reported plugin latency, or have the dry path and other bands delayed to match. The adapter's own API exists for exactly that purpose and is unused.

**Consequence.** A Spectral band lags by 2048-4096 samples at the effective rate, so a band with Spectral sums misaligned with the other bands (smeared transients). At mix < 100 % both the adapter dry/wet blend and the global dry/wet blend comb-filter. Hosts get no PDC.

**Evidence.** distortion_adapter.cpp:248 getProcessingLatency (grep: only definition and declaration); band_processor.h:376-379 `static int getLatency() noexcept { return 0; }`; spectral_distortion.h:240-246 returns 0 until OLA has samples; feedback_distortion.h:340 `return processed;`

**Suggested fix.** Decide the latency policy with the user: fixed max-latency PDC with per-band and dry delay lines, or accept the latency and document it. Then wire getProcessingLatency through MorphEngine/BandProcessor to setLatencySamples. Test: an impulse through a band with a Spectral node at mix 0.5 must give one peak (after compensation), not two separated by fftSize.

**Verifier notes:**
- Correctness: getProcessingLatency's only users are unit tests (distortion_adapter_test.cpp:127-151); grep for getLatencySamples/setLatencySamples/getTailSamples finds nothing in plugins/disrumpo/src; the Spectral latency is real (spectral_distortion.h:217-243, latency() = fftSize at :490-493, default 2048 at distortion_adapter.h:179) and nothing delays dry or the sibling bands (adapter blend at :234-235); specs-overview.md:66 says "< 10ms".
- Context: distortion_adapter.h:242-245 says callers should "Query via getProcessingLatency()" and nothing does; specs-overview.md:374 requires 0-256 samples, so the gap goes against the spec; FeedbackDistortion's delayed wet is the intended comb-resonator (feedback_distortion.h:254-258, :332-334) and Granular's grain buffering is the effect, so those parts overreach.
- Impact: Spectral's recommended factor is 1x (distortion_types.h:188-190), so the delay is the full fftSize at base rate (11.6-93 ms at 44.1 kHz, 46 ms default), which at that length is a flam or echo rather than fine comb filtering; no PDC in any host; the Spectral case alone carries medium.

### D-080 — setType switches the active processor with no reset, so stateful processors replay stale audio when a node type returns

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** shared-dsp-contract | **Dimension:** shared-dsp-contracts | **Also flagged by:** D-032, D-028 | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.cpp:142-162](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L142-L162)

**Problem.** DistortionAdapter::setType only stores currentType_ and updates DC/latency flags. Processors that were inactive keep whatever they held when last active, because processRaw feeds only the current type's processor. That includes the FeedbackDistortion delay line, the Granular 32768-sample buffer and grain pool, the Spectral STFT/OLA buffers, AllpassSaturator Karplus-Strong/matrix states, and the chaos attractor. MorphEngine::setNodes calls setType on every node-param change, and processSameFamily calls it on blendedAdapter_ every sample with the dominant type.

**Why wrong.** These primitives assume a continuous input stream between reset() calls. When the caller stops feeding one and later resumes, it must reset() it, or crossfade from a primed state, to avoid emitting stale content.

**Consequence.** Switching a node (or the same-family dominant node) back to Feedback, Granular or Spectral bursts out audio captured seconds or minutes earlier: a comb ringing tail, grains read from the old buffer, an old spectral frame. This is audible as ghost audio or clicks at type changes and morph-dominance changes.

**Evidence.** distortion_adapter.cpp:142-162 setType (no reset); :542-567 only the active type's processor is processed; morph_engine.cpp:705-706 blendedAdapter_.setType(nodes_[dominantNode].type) per sample

**Suggested fix.** In setType, call reset() on the newly selected type's processor when the type actually changes. A short output crossfade would avoid the reset click. Test: run FeedbackDist on a loud sine, switch to SoftClip and back, process 1 s of silence, switch back, process silence; output must be < -90 dBFS.

**Verifier notes:**
- Correctness: setType (:142-162) assigns currentType_, calls updateDCBlockerState and sets the block-latency fields only; processRaw (:542-575) feeds only the current type's processor; DistortionAdapter::reset (:98-136), MorphEngine::reset (morph_engine.cpp:72-94) and BandProcessor::reset (band_processor.h:136-153) run only from setActive/prepare (processor.cpp:149-164); processor_params.cpp:840-853 and morph_engine.cpp:131-140 never reset on a type change.
- Context: distortion_types.h:328-330 puts Temporal and FeedbackDist in Dynamic and :338-344 puts Chaos/Formant/Granular/Spectral/Fractal/Stochastic in Experimental, so processSameFamily (morph_engine.cpp:696-706) swaps blendedAdapter_ between stateful processors whenever dominance changes; FeedbackDistortion's delay line (feedback_distortion.h:209-215, :261, :330) is cleared only in reset(); no spec note on resetting at type change; the adapter tests call reset() right after setType.
- Impact: up to about 100 ms of old comb ringing (held up by the limiter if feedback > 1), about 100 ms of grains from old audio, or up to one 8192-sample FFT frame (about 186 ms at 44.1 kHz) of an old spectral frame, arriving as a click plus ghost audio at full wet level on the dominance switch; a related case is cross-family nodes skipped below kWeightThreshold (:745), which freeze and resume stale but fade back in with their weight; medium.

### D-079 — GranularDistortion's internal drive (default 5.0) is never set, so the adapter's drive is multiplied by 5 inside each grain

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** shared-dsp-contract | **Dimension:** shared-dsp-contracts | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.cpp:877-886](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L877-L886)

**Problem.** The adapter pre-scales the Granular input by commonParams_.drive (line 211) and never calls granular_.setDrive(), so baseDrive_ keeps its default 5.0 (clamped [1,20]). Each grain's waveshaper then applies drive 5 times the variation on top. The adapter explicitly fixes the same double-drive problem for Fractal (setDrive(1.0), :943-947) but not for Granular.

**Why wrong.** GranularDistortion's drive is part of its contract (setDrive, granular_distortion.h:239-241). The adapter must either neutralise it (setDrive(1)) or route the node drive into it instead of pre-scaling.

**Consequence.** Granular is driven 5x harder than the Drive knob shows (drive 2 acts as 10, drive 10 as 50, i.e. tanh hard-clip). The lower half of the Drive range is unusable and every Granular setting is heavily clipped.

**Evidence.** granular_distortion.h:713 `float baseDrive_ = 5.0f;`; :239-241 setDrive; distortion_adapter.cpp:878-885 (no setDrive); :211 `wet = input * commonParams_.drive;`; :943-947 Fractal setDrive(1.0f) with the double-drive comment

**Suggested fix.** Call granular_.setDrive(1.0f) in the Granular routing, matching Fractal. Test: Granular with drive 1, density 1, and a -20 dBFS sine; peak output < -12 dBFS (unsaturated). Today it is near 0 dBFS.

**Verifier notes:**
- Correctness: distortion_adapter.cpp:206-212 pre-scales every type except Temporal and Stochastic; the Granular routing (:877-886) never calls setDrive (grep confirms, only setDriveVariation at :881); granular_distortion.h:713 baseDrive_ = 5.0f, prepare/reset snap driveSmoother_ to it (:142, :172), triggerGrain sets ws.setDrive(grainDrive) (:567-576) and processGrain applies it (:648); kMinDrive = 1.0 (:93) so setDrive(1.0f) is a valid neutral value.
- Context: no spec, comment or test documents the double drive (specs/Disrumpo mentions only per-grain drive variation, dsp-details.md:510, ui-mockups.md:859; distortion_adapter_test.cpp checks only latency and category for Granular); the adapter fixes the same issue for Fractal (:943-947) and Stochastic (:951-956).
- Impact: the node drive range is 0-10 (processor_params.cpp:858, clamped at band_processor.h:445 and morph_engine.cpp:713), so the effective tanh input gain is 0-50; at knob 1 a -20 dBFS sine gives tanh(0.5) = 0.46, about -6.7 dBFS and already compressing (not "near 0 dBFS"); "lower half unusable" is backwards, it is the upper ~80 % that saturates alike; STATE-COMPAT note: fixing it changes every preset that uses Granular; medium.

### D-078 — Feedback type: the 'Curve' knob is never routed to FeedbackDistortion, the 'Lim' off toggle still limits at 0 dB, and 'Thr' is inverted

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** shared-dsp-contract | **Dimension:** shared-dsp-contracts | **Corroborated by measurement:** yes (probe `tail:FeedbackDist` -47.9 dBFS)

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.cpp:812-820](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L812-L820)

**Problem.** processor_params maps slot 2 to p.fbCurve, but the FeedbackDist routing never calls feedbackDist_.setSaturationCurve (or setDrive), so the knob does nothing. The limiter-off branch passes 0.0 dB, the primitive's maximum threshold, which still soft-limits to threshold+3 dB. The threshold mapping limThreshold*-24 means turning 'Thr' up LOWERS the threshold.

**Why wrong.** FeedbackDistortion exposes setSaturationCurve (feedback_distortion.h:400) and its threshold range is [-24, 0] dB with no 'off' value (:111-112). The adapter must route every UI control and implement 'off' itself, for example by skipping the limiter or using a very high threshold if the primitive gains a disable flag.

**Consequence.** The UI shows a 'Curve' knob that changes a stored value with no audible effect. 'Lim' off has almost no effect at normal levels. Users raising 'Thr' to loosen the limiter get maximum limiting at -24 dB instead.

**Evidence.** processor_params.cpp:182 `p.fbCurve = slots[2];`; distortion_adapter.cpp:813-819 (no setSaturationCurve); :815 `setLimiterThreshold(p.limiter ? (p.limThreshold * -24.0f) : 0.0f)`; feedback_distortion.h:111-112 kMin/MaxThresholdDb -24/0; :297 limiter active whenever smoothedThreshold > 0; editor.uidesc:1075-1076 'Curve' knob, :1083-1086 'Lim'/'Thr'

**Corroborated by measurement.** probe tail:FeedbackDist: "tail_rms_dBFS -47.9" (threshold <= -90 dBFS). Code: distortion_adapter.cpp:819 `feedbackDist_.setDecay(p.fbDecay);` with distortion_adapter.h:146 `float fbDecay = 0.0f; ///< Feedback decay amount [0, 1] (0=off)`

**Suggested fix.** Route fbCurve to setSaturationCurve (map it like the other 9-type curves) and invert the threshold map (-24 + limThreshold*24). Add a limiter-enable to FeedbackDistortion (a shared header: check consumers). Tests: changing fbCurve from 0 to 1 must change the output RMS by > 1 dB; with limiter on, a higher Thr must give a higher peak output.

**Verifier notes:**
- Correctness: fbCurve appears only in its declaration (distortion_adapter.h:141), the mapping (processor_params.cpp:182) and morph interpolation (morph_engine.cpp:470, :579); every feedbackDist_ use is at distortion_adapter.cpp:73/:114/:544/:813-819 and none calls setSaturationCurve (feedback_distortion.h:400) or setDrive (:414); the limiter branch at feedback_distortion.h:283 runs whenever absLevel > smoothedThreshold && smoothedThreshold > 0, and "off" passes 0 dB = linear 1.0, so it is identical to on with Thr = 0; :815 computes p.limThreshold * -24 so knob 0 is 0 dB and knob 1 is -24 dB, with the default 0.8 (distortion_adapter.h:145, uidesc:1086) giving -19.2 dB.
- Context: the spec (ui-mockups.md:708) defines Curve as a saturation selector for the feedback loop and (:713) gives Limit Thresh a -12 to 0 dB range with a -3 dB default, so the mapping disagrees with the design in direction, range and default; no test mentions fbCurve, limThreshold or limiter.
- Impact: two of the type's controls are dead or inverted for every preset and host using Feedback; fixing (1) and (3) is local to the adapter and only a real limiter-off switch touches the shared header; medium.


### D-077 — Tube multi-stage cascade calls one TubeStage instance up to 4 times per sample, sharing its DC blocker and smoothers between stages

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** shared-dsp-contract | **Dimension:** shared-dsp-contracts | **Corroborated by measurement:** partly (one verifier ran a standalone probe, see notes)

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.cpp:309-318](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L309-L318)

**Problem.** For satStage > 0 the adapter loops `tube_.process(singleSampleBuffer_, 1)` 2-4 times on the SAME TubeStage. Each call advances inputGain/outputGain/saturation smoothers (tube_stage.h:280-282) and runs the single dcBlocker_ (:301), whose one-sample memory then alternates between the stage signals within one sample.

**Why wrong.** TubeStage::process(buffer, n) treats successive calls as successive time samples of ONE signal. A cascade of S stages needs S instances (or S DC-blocker/smoother states). With a shared instance the DC blocker no longer implements its documented 10 Hz HP per stage, and the smoothing time is divided by S.

**Consequence.** Multi-stage tube settings get a corrupted DC-block/HP response: the high-pass sees an interleaved stage sequence, which leaves stage-to-stage differences and residual DC. Parameter ramps finish S times faster, so zipper noise appears on sag automation. The tonal result of 2-4 stages is not a real cascade.

**Evidence.** distortion_adapter.cpp:313-316 `for (int s = 0; s < stages; ++s) { tube_.process(singleSampleBuffer_, 1); }`; tube_stage.h:278-307 per-call smoother advance + dcBlocker_.process(wet)

**Suggested fix.** Hold std::array<TubeStage,4> in the adapter (prepare and route params to all four) and run stage s through tube_[s]. Test: Tube with satStage 3 and bias 0.5, fed a 100 Hz sine; output mean < 1e-3 after 1 s, and the result must match four independent TubeStage instances within 1e-4.

**Verifier notes:**
- Correctness: :312-317 calls the single `tube_` (distortion_adapter.h:371) stages = clamp(satStage+1,1,4) times per sample, satStage coming from processor_params.cpp:58 (0..3); tube_stage.h:271-308 advances all three OnePoleSmoothers (:280-282) and the single DCBlocker (:301, :334) per call, whose x1_/y1_ state (dc_blocker.h:138-151) sees a sequence repeating every S calls, so only the mean over all phases is removed; the zipper claim is overstated because the adapter routes only setBias and setSaturationAmount (:614-615) and a 5 ms/S ramp is still a ramp.
- Context: the adapter's own dcBlocker_ is enabled only for AsymmetricFuzz/FullRectify/HalfRectify/FeedbackDist (:586-592), so Tube output with bias gets no further DC block; no comment, spec (grep for cascade/multi-stage/satStage) or test references the stage count.
- Impact: a standalone probe (clang++ -O2, 44.1 kHz, sag 1.0, 1 s settle) comparing the shared instance with S independent TubeStages gave output means of -0.052/-0.075/-0.094 (bias 0) and +0.035/+0.069/+0.099 (bias 0.5) for 2/3/4 stages at drive 4 on a 100 Hz sine (up to about -20 dBFS DC, against 0.0000 for the true cascade), difference RMS -14/-5.7/-0.8 dB at 100 Hz, level off by up to 0.74 dB; needs a non-default stage selection; medium.

### D-076 — FuzzProcessor's tone is never set by the adapter, so Fuzz/AsymFuzz always run through a hidden ~1.8 kHz biquad lowpass on top of the adapter tone filter

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** shared-dsp-contract | **Dimension:** shared-dsp-contracts | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.cpp:633-651](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L633-L651)

**Problem.** The Fuzz and AsymmetricFuzz routing sets bias, fuzz amount, octave and body but never calls fuzz_.setTone(). FuzzProcessor defaults tone_ = 0.5, which maps to 400*20^0.5 ≈ 1789 Hz, and always applies that Q=0.707 lowpass to the saturated signal. The adapter then applies its own one-pole tone filter.

**Why wrong.** The primitive's tone control is part of its documented API (setTone, fuzz_processor.h:92, 182). If the plugin does not expose it, the adapter must set it to fully open (tone = 1, 8 kHz) or map it from the node's Tone. Otherwise the user's Tone knob cannot open the sound.

**Consequence.** Fuzz types have a fixed dark 12 dB/oct rolloff the user cannot remove. Combined with the 8x prepare-rate mismatch the corner sits near 224 Hz at 44.1 kHz, making Fuzz nearly muffled.

**Evidence.** distortion_adapter.cpp:637-650 (no setTone); fuzz_processor.h:116 kDefaultTone = 0.5f; :134,137 kToneMinHz 400 / kToneMaxHz 8000; :398-399 toneFilter_.configure(Lowpass, 400*20^tone, 0.707); :554 saturated = toneFilter_.process(saturated)

**Suggested fix.** Call fuzz_.setTone() in both cases, mapped from commonParams_.toneHz or fixed at 1.0. Test: Fuzz with sustain 0.5 and node tone 8000 Hz, fed a 4 kHz sine; output at 4 kHz must be within 6 dB of the 500 Hz response.

**Verifier notes:**
- Correctness: the Fuzz/AsymFuzz routing (:633-651) calls only setBias, setFuzz, setOctaveUp and bodyFilter_; a grep for fuzz_ finds no setTone anywhere (uses at :37, :109, :347-364, :637-647); FuzzProcessor tone_ = kDefaultTone = 0.5 (fuzz_processor.h:116, :233), updateToneFilter (:396-400) configures a Q 0.707 lowpass at 400*20^tone, applied with no bypass at :554; the adapter's own tone filter comes after and is clamped to 200-8000 Hz (:170).
- Context: no comment marks it intentional (the comments at :634-636/:643-645 only explain the FuzzType handling), nothing in specs/Disrumpo mentions fuzz tone, and distortion_adapter_test.cpp:210-241 checks only that output differs from input.
- Impact: the finding's 224 Hz figure assumes 1x; at the Fuzz types' recommended 4x profile (distortion_types.h:171-172) with the 8x prepare the hidden corner sits at about 894 Hz, and at the nominal rate at 1789 Hz, so a 4 kHz harmonic is cut by roughly 7-13 dB and the Tone knob can only darken further; two of 26 types (Fuzz is node C's default type, band_state.h:51) always carry a 12 dB/oct rolloff the user cannot remove; medium.

### D-075 — Stochastic (and Temporal with Diode/Tube curves) need an external DC blocker per their contract; the adapter gives none, and Stochastic emits noise and DC from silence

**Severity:** medium (reported: high; lowered by two of three verifiers) | **Verdict:** CONFIRMED | **Category:** shared-dsp-contract | **Dimension:** shared-dsp-contracts | **Also flagged by:** D-142 (measured Stochastic self-noise) | **Corroborated by measurement:** yes (probes `type:Tube DC` and `bias:Tube`)

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.cpp:586-592](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L586-L592)

**Problem.** updateDCBlockerState enables the adapter DC blocker only for AsymmetricFuzz, FullRectify, HalfRectify and FeedbackDist. StochasticShaper adds jitterOffset (up to ±0.5*amount plus drift) to the INPUT before the waveshaper (`waveshaper_.process(x + jitterOffset)`) and has no DC blocking. TemporalDistortion includes no DC blocker, yet the adapter lets it select WaveshapeType Diode (7) and Tube (8) via dynamicCurve (:761-764).

**Why wrong.** stochastic_shaper.h:135 states 'No internal DC blocking: Compose with DCBlocker for asymmetric types'. sigmoid diode() is asymmetric (forward bound 1, reverse bound -2, sigmoid.h:358-366), so Temporal with Diode produces a DC offset. The adapter is the composition point and must add the blocker.

**Consequence.** With silent input, a Stochastic node outputs waveshaper(drive*jitterOffset): a wandering noise/DC floor up to about tanh(10*0.5) ≈ full scale at high drive and jitter, with no gate. Temporal with the Diode/Tube curve, and Stochastic with drift, put a DC offset on the band output that sums into the master.

**Evidence.** distortion_adapter.cpp:588-591 needsDCBlock_ list excludes Stochastic/Temporal; stochastic_shaper.h:583 `float out = waveshaper_.process(x + jitterOffset);`; :135 contract note; temporal_distortion.h:26-30 includes (no dc_blocker); distortion_adapter.cpp:761-764 curve index 0..8 to WaveshapeType

**Corroborated by measurement.** probe type:Tube DC: "drive 0.25: -0.00566; 0.75: -0.033; 1.0: -0.041". bias:Tube:bias=-1 / +1: "-0.0673,0,-7.15 / 0.00686,0,-4.38" (threshold |dc| < 1e-3). Code: distortion_adapter.cpp:588 `needsDCBlock_ = (currentType_ == DistortionType::AsymmetricFuzz || ... FeedbackDist);`, with Tube absent.

**Suggested fix.** Add Stochastic and Temporal (at least for curve indices 7 and 8) to updateDCBlockerState. Decide with the user whether Stochastic should gate its jitter on input level, the way FeedbackDistortion gates its feedback. Test: an adapter of type Stochastic (jitter 1, drift 1, drive 5) fed 2 s of silence; output RMS < -90 dBFS and |mean| < 1e-4. Today it is loud.

**Verifier notes:**
- Correctness: needsDCBlock_ is set only for the four listed types (:586-592, plus :701 for the rectifiers); Stochastic gets the unscaled input (:207-212) and stochastic_.setDrive(drive) (:956); stochastic_shaper.h:530-592 builds jitterOffset from bipolar nextFloat values (:562-565) and shapes x + jitterOffset (:583), so at drive 5 / jitter 1 / drift 1 silence gives up to tanh(5*0.8) ≈ 1; the only gate is drive < 0.0001 (:191); Temporal's curve map (:761-764) does reach the asymmetric Diode (7) and Tube (8) (waveshaper.h:58-59, sigmoid.h:333-366) with no DC blocker.
- Context (dissent on severity): the adapter clamps the Stochastic curve to 0..5 (:952), all symmetric, so the "asymmetric types" contract in stochastic_shaper.h:135 does not apply to Stochastic; its jitter/drift are zero-mean random walks (sub-10 Hz wander at the default 10 Hz rate, 80 ms smoothing), so a 10 Hz blocker would not remove the 10-100 Hz part at high rates; the Temporal DC needs a non-default curve (default index 4 is symmetric); recommended medium.
- Impact (dissent on severity): computed means of tube() for a sine through Temporal's drive range (:754) reach about +0.31 (-10 dBFS) at drive 3 with a full-scale input and about 0.01 at -20 dBFS, Diode about 0.03-0.05; the DC follows the envelope so transients thump; the Stochastic floor is partly by design (FR-022) and needs extreme jitter/drive for full scale; recommended medium; final medium.

### D-071 — Spectrum dividers ignore crossover parameter changes, and band-count changes redistribute them in the view only

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** ui-wiring | **Dimension:** ui-wiring | **Also flagged by:** duplication-drift | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/views/spectrum_display.cpp:50-66](plugins/disrumpo/src/controller/views/spectrum_display.cpp#L50-L66)

**Problem.** The dividers are read from the parameters once, in createCustomView. No dependent observes the crossover parameters afterwards. BandCountDisplayController calls setNumBands, which overwrites crossoverFreqs_ with an even log spread inside the view without touching the parameters. The processor applies its own redistribution in CrossoverNetwork::setBandCount, and the controller parameters keep their old values.

**Why wrong.** The display must reflect the parameter and processor state. A view-local redistribution creates a third crossover state that neither the parameters nor the processor share.

**Consequence.** After automation, a preset load or a band-count change with the editor open, the dividers show frequencies that are neither the processor's nor the parameters'. The next drag of one divider writes one value while the others stay inconsistent, and the next preset save writes the stale parameters.

**Evidence.** spectrum_display.cpp:53-63 setNumBands rewrites crossoverFreqs_ ; visibility_controllers.cpp:57-59 BandCountDisplayController -> setNumBands ; grep for controller callers of setCrossoverFrequency: only createCustomView (controller.cpp:951)

**Suggested fix.** Add an IDependent on the 3 crossover params that calls setCrossoverFrequency with the same mapping as the processor. Remove the redistribution from setNumBands, or push the redistributed values to the params with begin/perform/endEdit. Test: setParamNormalized on a crossover and assert the display moves; change the band count and assert display == params.

**Verifier notes:**
- Correctness: setNumBands (spectrum_display.cpp:51-67) replaces crossoverFreqs_ with an even log2 spread; the view's setCrossoverFrequency (:69-74) is called only from createCustomView (controller.cpp:947-953); makeCrossoverParamId appears in the controller tree only at parameter_registration.cpp:153, controller.cpp:208/:948/:1729 and visibility_controllers.cpp:641 (the drag-bridge write path), so no IDependent watches the three crossover params; the processor's setBandCount (crossover_network.h:111-122, 274-339) keeps existing crossovers and inserts log midpoints, a different result from the view's even spread.
- Context: there is also an ordering problem at creation: setNumBands runs first (controller.cpp:944) and setCrossoverFrequency drops any index >= numBands_-1 (spectrum_display.cpp:70), so at 1-2 bands the higher params never reach the view; no comment or spec marks the view redistribution as intentional (the FR-009 log-spread comment is on the processor and overridden there by FR-011a/b); no test syncs view and params.
- Impact: divider positions, Hz labels (spectrum_display.cpp:438-503), band-region colouring (:316, :421-422) and the sweep intensity overlay (SweepVisualizationController reads display->getCrossoverFrequency, visibility_controllers.cpp:597-603) are all wrong after automation, preset load or a band-count change; the params remain the saved truth; medium.

### D-070 — The spectrum Tier-3 fallback is disabled for good after the first editor close

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** ui-wiring | **Also flagged by:** D-057 covers the same willClose clearing | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/controller.cpp:1581](plugins/disrumpo/src/controller/controller.cpp#L1581), [:645-646](plugins/disrumpo/src/controller/controller.cpp#L645-L646), [:1476](plugins/disrumpo/src/controller/controller.cpp#L1476)

**Problem.** willClose sets sharedDisplay_ = nullptr. The pointer is assigned only in setComponentState, from the instance-id trailer. The didOpen timer requires sharedDisplay_ != nullptr before activating the SharedDisplayBridge fallback.

**Why wrong.** The bridge lookup refers to processor-owned FIFOs that outlive the editor. Clearing it on editor close discards a lookup that only a host state restore can redo.

**Consequence.** On hosts where DataExchange does not deliver (the reason Tier 3 exists), the spectrum analyzer works the first time the editor opens and stays blank after every reopen in the same session.

**Evidence.** controller.cpp:1581 `sharedDisplay_ = nullptr;` (willClose) ; :645-646 only assignment, in setComponentState ; :1476 `if (!dataExchangeActive_ && sharedDisplay_ && !spectrumDataAvailable_)`

**Suggested fix.** Keep sharedDisplay_ across editor sessions (clear it in terminate), or redo the lookup by instanceId_ in didOpen. Test: lifecycle test with DataExchange disabled: open, close, reopen, tick the timer more than 10 times, and assert that the spectrum display has FIFOs.

**Verifier notes:**
- Correctness: the only assignment is setComponentState (controller.cpp:637-651) via SharedDisplayBridge::lookupInstance(instanceId_); willClose nulls it (:1581) along with dataExchangeActive_/fallbackTickCounter_ (:1582-1583); didOpen (:1354-1356) does not redo the lookup; the timer gate (:1476) therefore fails on every reopen; instanceId_ survives willClose so a didOpen lookup would be cheap.
- Context: the bridge entry is processor-owned and lives from initialize (processor.cpp:69) to terminate (:81), re-registered on setState (processor_state.cpp:911-913), so the editor lifecycle gives no reason to drop the pointer; Innexus has the same pattern (controller.cpp:1183, controller_state.cpp:536), suggesting copy-paste; data_exchange_spectrum_test.cpp has no open/close/reopen case.
- Impact (dissent on severity): only the spectrum display is affected, only on hosts where neither DataExchange nor the IMessage fallback delivers (mainstream hosts re-set dataExchangeActive_ at :1940-1945 on every open), and any setComponentState recovers it; this verifier recommended low; medium retained (one of three).

### D-069 — Most sweep controls and the custom-curve editor are absent from the editor

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** ui-wiring | **Dimension:** ui-wiring | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/resources/editor.uidesc:616-634](plugins/disrumpo/resources/editor.uidesc#L616-L634)

**Problem.** No uidesc control binds to SweepIntensity, SweepFalloff, SweepMorphLink, SweepLFOWaveform, SweepLFOSync, SweepLFONoteValue, SweepEnvSensitivity or Band.NodeBias (0 control-tag uses each). The CustomCurveEditor custom view (controller.cpp:1123-1246) and its 9200 container are never instantiated, so customCurveVisController_ (controller.cpp:1492-1495) watches nothing.

**Why wrong.** The processor consumes all of these, and the spec defines intensity 0-200%, falloff, LFO shape/sync and the custom curve as user features. A UI that cannot reach them leaves them at their mismatched defaults (intensity default 50% = 1.0 vs DSP 0.5; falloff Hard vs DSP Smooth).

**Consequence.** Users cannot set sweep intensity, falloff, LFO waveform or tempo sync, or envelope sensitivity, node bias, or a custom morph-link curve from the plugin UI; only the host's generic parameter list reaches them. The custom-curve code in the controller is dead code.

**Evidence.** grep -c 'control-tag="SweepIntensity"' etc. = 0 for each listed tag ; grep -c CustomCurveEditor editor.uidesc = 0 ; sweep row controls at editor.uidesc:616-634 cover only Enable/Freq/Width/LFO enable,rate,depth/Env enable,attack,release

**Suggested fix.** Add the missing controls to the sweep row and the band panel via XSLT, and add the CustomCurveEditor view with its marker, or remove the dead curve plumbing. Test: a uidesc inventory test asserting every automatable sweep param has a control.

**Verifier notes:**
- Correctness: the sweep row (editor.uidesc:614-638) binds only SweepEnable/Frequency/Width/LFOEnable/LFORate/LFODepth/EnvEnable/EnvAttack/EnvRelease plus ModPanelVisible; SweepIntensity, SweepMorphLink, SweepFalloff, SweepLFOWaveform, SweepLFOSync, SweepLFONoteValue and SweepEnvSensitivity appear only as control-tag declarations (:48-60); Band.NodeBias is declared at :281 and remapped in sub_controllers.h:145 but no view uses it (the "Bias" labels at :895/:904 sit on Band.NodeShape0); CustomCurveEditor and 9200 occur 0 times; the processor consumes all of them (processor_params.cpp:402-487, :868).
- Context: the parameters are registered automatable (parameter_registration.cpp:207, 218, 233, 252, 269, 298) and saved/loaded (controller.cpp:232-278, 1735-1747); specs-overview.md:279-291 (FR-SWEEP-002/003) define them as user parameters and tasks-overview.md:204-211 (T8.4) lists an Intensity knob and a Link selector as UI controls; the custom curve editor is a documented v1.0 deferral (roadmap.md:103), so that sub-claim is dead code rather than a wiring defect; ui-mockups.md:67/:71/:77 show Bias knobs and a Link selector.
- Impact: eight automatable parameters are unreachable from the plugin's own editor in every host and only the generic parameter list or automation reaches them; one side remark is wrong, intensity is registered 0-100 default 50 (parameter_registration.cpp:207-210), not 0-200; medium.

### D-067 — Node param automation and edits from outside the editor never refresh the proxy knobs, so the UI shows stale values and jumps on the next touch

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** ui-wiring | **Dimension:** ui-wiring | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/visibility_controllers.cpp:226-262](plugins/disrumpo/src/controller/visibility_controllers.cpp#L226-L262), [:331-335](plugins/disrumpo/src/controller/visibility_controllers.cpp#L331-L335)

**Problem.** NodeSelectionController subscribes to SelectedNode, the 4 node Type params, DisplayedType and the proxies, but not to the selected node's real Drive/Mix/Tone/Bias/Shape0-9 params. All node params are registered kCanAutomate. When the host automates one, update() is never called and the bound proxy keeps its old value.

**Why wrong.** The UI is bound only to the proxies (sub_controllers.h:136-160), so the proxies must mirror the real node params in both directions. Only the full-state parse path re-syncs them (controller.cpp:595-613).

**Consequence.** Drive/Tone/shape automation of the selected node is inaudible in the UI, because the knobs do not move. When the user then grabs the knob, copyProxyToSelectedNode sends the stale proxy value plus the drag delta, an audible jump.

**Evidence.** visibility_controllers.cpp:231-258 dependents registered: selectedNodeParam_, nodeTypeParams_[0..3], displayedTypeParam_, proxyParams_ only ; :331-335 the only proxy direction from a real param is via a type/selection change

**Suggested fix.** Also addDependent on the selected node's ten-plus-four real params, re-subscribing on node change, and call copySelectedNodeToProxies when one of them changes. Test: with the controller active, setParamNormalized on the selected node's Drive and assert that DisplayedDrive follows.

**Verifier notes:**
- Correctness: the constructor (:226-258) subscribes to exactly four groups and never to the real node Drive/Mix/Tone/Bias/Shape params; update() (:310-338) refreshes the proxies only on a SelectedNode change (:328-330, copySelectedNodeToProxies) and the else branch (:333-335) handles only type changes; Controller does not override setParamNormalized (its overrides are initialize, terminate, setComponentState, getState, setState, getMidiControllerAssignment, getParamStringByValue, getParamValueByString, notify), so host automation only notifies Parameter dependents.
- Context: the real node params carry kCanAutomate (parameter_registration.cpp:912-960) while the proxies are kNoFlags (:804-822), so hosts automate only the real ones; kProxyIndexToNodeParam is used only at controller.cpp:609 (full-state parse) and visibility_controllers.cpp:380/:400; no test touches NodeSelectionController or DisplayedDrive.
- Impact: copyProxyToSelectedNode (:392-410) writes the stale proxy value plus the drag delta through beginEdit/performEdit/endEdit, a jump of up to Drive's full 0-10 or Tone's 200-8000 Hz range that a host in touch/latch mode also records; limited to the selected node's 14 params, which is the main editing surface; medium.

### D-066 — Sweep Morph Link is registered with 6 list entries but the processor and serializers use 8 modes

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** ui-wiring | **Dimension:** ui-wiring | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/parameter_registration.cpp:212-224](plugins/disrumpo/src/controller/parameter_registration.cpp#L212-L224)

**Problem.** The StringListParameter has 6 strings (None, Linear, Inverse, Ease In, Ease Out, Ease In-Out; stepCount 5). The processor decodes round(v*7) over the 8-entry MorphLinkMode, and parseComponentState and createComponentStateStream also use kMorphLinkModeCount-1 = 7.

**Why wrong.** The list must match the enum, with names in the enum's order (None, SweepFreq, InverseSweep, EaseIn, EaseOut, HoldRise, Stepped, Custom).

**Consequence.** The host's 'Inverse' selects EaseIn, 'Ease In' selects EaseOut, 'Ease Out' selects Stepped and 'Ease In-Out' selects Custom. InverseSweep and HoldRise cannot be selected. A preset value written as mode/7 is shown as a different list entry. The editor has no control for it (0 uidesc uses), and the custom-curve visibility threshold 0.93 (controller.cpp:1495) targets the wrong entry.

**Evidence.** parameter_registration.cpp:218-223 six appendString calls ; processor_params.cpp:412 `value * static_cast<float>(kMorphLinkModeCount - 1) + 0.5f` ; plugin_ids.h:640-649 8 modes ; controller.cpp:237 and :1737 use kMorphLinkModeCount - 1

**Suggested fix.** Register all 8 enum names in enum order, or reduce the enum. Test: for each list index i, assert that the processor decodes MorphLinkMode(i) and that the parse/write round trip is identity.

**Verifier notes:**
- Correctness: the block is at parameter_registration.cpp:216-228 (six appendString calls, stepCount 5); plugin_ids.h:640-653 defines 8 modes; processor_params.cpp:412 decodes round(v*7), so list index i (v = i/5) maps 0->None, 1->SweepFreq, 2->EaseIn, 3->EaseOut, 4->Stepped, 5->Custom, and InverseSweep/HoldRise are unreachable; controller.cpp:237 and :1737 (getInt8FromList with maxVal 7) and processor_state.cpp:95/:491-493 all assume 8; a stored mode 2 becomes 2/7 = 0.286, shown as entry 1 "Linear"; the 0.93 threshold sub-claim is overstated because norm 1.0 does decode to Custom, so only the label is wrong.
- Context: no comment or spec explains the 6-entry list; the only tests are parameter_encoding_test.cpp:59/69 (ID encoding) and morph_link_test (curve math); no uidesc control maps the parameter.
- Impact (dissent on severity): the selected mode never reaches the audio because getMorphPosition (sweep_processor.h:297-309) has no caller (D-039), the editor has no control for it, and the 9200 container does not exist in editor.uidesc, so the visible effect is a mislabelled dead parameter in a host's generic editor; this verifier recommended low; medium retained (one of three).

### D-065 — DynamicNodeSelector is never bound to SelectedNode and maps the value inconsistently: with 2 nodes and B selected it highlights A, and the two selectors of a band do not follow each other

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** ui-wiring | **Dimension:** ui-wiring | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/views/dynamic_node_selector.cpp:76-100](plugins/disrumpo/src/controller/views/dynamic_node_selector.cpp#L76-L100), [:187-195](plugins/disrumpo/src/controller/views/dynamic_node_selector.cpp#L187-L195)

**Problem.** The uidesc declares the selector as class="CView", so UIDescription applies only the CView creator attributes (applyCustomViewAttributeValues(result, "CView", ...)). control-tag and listener are never applied, and VST3Editor never binds it. The view observes only ActiveNodes. createCustomView initializes it with setValueNormalized(paramNorm), where paramNorm = k/3 over 4 entries, but CSegmentButton decodes the index as min(N-1, floor(v*N)) over the current N segments.

**Why wrong.** A selector of a list parameter must decode index = round(norm*3) and must follow external changes to that parameter. Each band has two instances (expanded strip, editor.uidesc:1337; Shape-tab compact, :1293) plus host automation, preset loads and clampSelectedNode, all of which can change SelectedNode.

**Consequence.** In a 2-node band with node B selected, every editor open or Shape-tab visit highlights A (floor(0.333*2) = 0) while the knobs show B. Clicking a node in the expanded strip leaves the Shape-tab selector on the old node, and preset recall or automation of Selected Node is not shown.

**Evidence.** controller.cpp:1072-1076 `nodeSelector->setValueNormalized(normalized);` ; vstgui csegmentbutton.cpp:529-530 `static_cast<uint32_t> (value * (segments.size ()))` ; dynamic_node_selector.cpp:90 `if (changedParam != activeNodesParam_) return;` ; uidescription.cpp:684-687 applies custom-view attributes for the declared class only ; editor.uidesc:1293 `class="CView" custom-view-name="DynamicNodeSelector"`

**Suggested fix.** Make the view an IDependent on the SelectedNode parameter and set setSelectedSegment(round(norm*3)) clamped to N-1 in update() and at construction. Alternatively declare class="CSegmentButton" and drop the custom segment logic. Test: with ActiveNodes = 2 and SelectedNode = 1/3, construct the selector and assert getSelectedSegment() == 1; change SelectedNode and assert both selectors follow.

**Verifier notes:**
- Correctness: editor.uidesc:1293/:1337 declare class="CView", so uidescription.cpp:682-687 applies only CView attributes and controller.cpp:1068-1086 calls setTag but never setListener (the only setListener calls are spectrumDisplay :964 and morphPad :1005); VST3Editor::verifyView (vst3editor.cpp:1047) binds only when getListener() == this; the view subscribes only to ActiveNodes (dynamic_node_selector.cpp:31-35, early return at :90); CSegmentButton decodes min(N-1, uint32(v*N)) (csegmentbutton.cpp:133-136, 524-530), so N = 2 with B selected (0.333) truncates to 0.
- Context: dynamicNodeSelectors_/compactNodeSelectors_ are used only for high-contrast and deactivate (controller.cpp:1450-1451, 1545-1546); NodeEditorBorder does follow SelectedNode (node_editor_border.cpp:23-31, 74-81), so border colour and selector highlight disagree; the 3- and 4-node cases happen to decode correctly; no test mentions DynamicNodeSelector or getSelectedSegment.
- Impact: in the 2-node/B-selected state clicking A is a no-op (csegmentbutton.cpp:325-327 finds newIndex equal to the displayed index, so :187 sends no edit), so the user must click B then A; MorphPad node clicks (morph_pad.cpp:620-624), automation and preset loads never update either selector; UI-only; medium.

### D-064 — NodeSelectionController hands every node's shadow storage to node A, so per-type slot memory of nodes B-D is never kept and is saved as defaults

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** ui-wiring | **Dimension:** ui-wiring | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/controller.cpp:1458-1461](plugins/disrumpo/src/controller/controller.cpp#L1458-L1461)

**Problem.** shapeShadowStorage_ is std::array<std::array<ShapeShadowStorage, 4>, kMaxBands>, but each band's controller receives shapeShadowStorage_[b].data(), a pointer to node 0's storage. copyDisplayedTypeToSelectedNode saves and loads through that pointer without indexing by the selected node (visibility_controllers.cpp:430-447).

**Why wrong.** The shadow has to be per node, just as the processor keeps cache.shapeShadow[node] (processor_params.cpp:845) and the serializer writes shapeShadowStorage_[b][n] for each n (controller.cpp:1857).

**Consequence.** Changing the type of node B, C or D overwrites node A's per-type memory and restores node A's slots into B's UI. Nodes B-D's shadows stay at their loaded or default values and are written that way into user presets. Even if the type-change mirror were fixed, the UI and DSP would disagree after the first type change on a non-A node.

**Evidence.** controller.cpp:1459-1460 `new NodeSelectionController(this, static_cast<uint8_t>(b), shapeShadowStorage_[b].data());` ; controller.h:355 `std::array<std::array<ShapeShadowStorage, kMaxMorphNodesCtrl>, kMaxBands> shapeShadowStorage_;` ; visibility_controllers.cpp:438 `shapeShadowPtr_->save(oldTypeIdx, currentSlots);` with no node index

**Suggested fix.** Pass the whole per-band array (or the band index) to NodeSelectionController and index it with getSelectedNode() in save() and load(). Test: select node B, set slots on Tube, switch B to Fuzz and back to Tube, then assert B's slots are restored and node A's shadow is unchanged.

**Verifier notes:**
- Correctness: controller.h:355 declares a per-band, per-node array and controller.cpp:1459-1460 passes .data(), a pointer to element [b][0]; the constructor stores it as a plain ShapeShadowStorage* (visibility_controllers.cpp:216-224, .h:457); copyDisplayedTypeToSelectedNode (:413-455) uses selectedNode only for the type param ID (:441) and calls save/load with no node offset (:438, :447); the processor keeps a per-node shadow (processor_params.cpp:845-850, 888) and the serializer writes [b][n] (controller.cpp:1857-1861).
- Context: the pointer is used only at visibility_controllers.cpp:224/:430/:438/:445/:447; nodes 1-3's controller shadows are written only by setComponentState (controller.cpp:586); the preset state provider is createComponentStateStream (controller.cpp:116-118); no test references ShapeShadowStorage beyond a comment (multi_node_distortion_test.cpp:241).
- Impact: the active slot values are saved separately (controller.cpp:1851-1855), so a reloaded preset sounds right until the user switches that node's type again; ShapeShadowStorage defaults every slot to 0.5 (morph_node.h:102-113), so the visible error needs a history of edits on the same type; UI/state data-integrity defect that loses user per-type memory in saved presets, no audio artefact; medium.

### D-058 — MIDI-learn mappings: learning never completes, and 'Save Mapping with Preset' moves the mapping into storage that is never persisted

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** lifecycle-state | **Also flagged by:** ui-wiring; D-096/D-136 cover the missing event bus | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/controller.cpp:1623-1656](plugins/disrumpo/src/controller/controller.cpp#L1623-L1656), [:658-701](plugins/disrumpo/src/controller/controller.cpp#L658-L701)

**Problem.** (1) The 'MIDI Learn' item calls midiCCManager_->startLearn and setParamNormalized(kGlobalMidiLearnActive). setParamNormalized never reaches the processor, and the processor has no case for that ID anyway (processor_params.cpp:326-363). Nothing in Disrumpo calls MidiCCManager::processCCMessage. kSweepDetectedCCOutputId is produced by the processor (processor.cpp:400-406) but nothing on the controller side consumes it. So a learn can never capture a CC. (2) 'Save Mapping with Preset' calls removeGlobalMapping and then addPresetMapping (moving the mapping into presetMappings_). Controller::getState serialises only serializeGlobalMappings (controller.cpp:665). serializePresetMappings has no caller outside tests, and createComponentStateStream does not include it either. (3) Changing mappings never calls restartComponent(kMidiCCAssignmentChanged), so hosts keep their cached IMidiMapping table.

**Why wrong.** A persistence option has to persist somewhere, and the learn flow needs a CC-capture path. VST3 also requires restartComponent(kMidiCCAssignmentChanged) after the IMidiMapping table changes.

**Consequence.** Users cannot learn a CC from the context menu. Any mapping restored from controller state and then switched to 'Save Mapping with Preset' disappears on the next project save and reload.

**Evidence.** controller.cpp:1625-1626 `midiCCManager_->startLearn(paramId); setParamNormalized(makeGlobalParamId(GlobalParamType::kGlobalMidiLearnActive), 1.0);`; :1649-1650 `midiCCManager_->removeGlobalMapping(existingCC); midiCCManager_->addPresetMapping(existingCC, paramId, mapping.is14Bit);`; :665 `auto midiData = midiCCManager_->serializeGlobalMappings();`; grep processCCMessage|restartComponent in plugins/disrumpo/src: no hits; serializePresetMappings: only definition in shared/midi_cc_manager.cpp:322.

**Suggested fix.** Serialise presetMappings_ (in the controller state blob, or appended to the preset stream). Wire a capture path that completes learn, for example by reading kSweepDetectedCCOutputId in setParamNormalized, or through IMidiMapping-routed proxy params. Call componentHandler->restartComponent(kMidiCCAssignmentChanged) after any mapping change and after Controller::setState. Test: add a mapping, toggle it to per-preset, run getState -> setState on a fresh controller, and assert getMidiControllerAssignment still resolves.

**Verifier notes:**
- Correctness: MidiCCManager::processCCMessage (shared midi_cc_manager.cpp:127-146) is the only learn-completion API and no plugins/disrumpo/src file calls it; kGlobalMidiLearnActive appears only at plugin_ids.h:69, parameter_registration.cpp:120 and controller.cpp:1412/1626; the processor scan is keyed on kSweepMidiLearnActive (processor_params.cpp:527-528) and no setParamNormalized override reads kSweepDetectedCCOutputId; getState/setState (controller.cpp:664-671, 690-697) handle only global mappings; no restartComponent/kMidiCCAssignmentChanged in src.
- Context: dsp-details.md section 12 (about 1807-1855) specifies per-preset mappings and the right-click MIDI Learn workflow as features, so the gaps are not known limitations; the Disrumpo tests cover only the global-mapping round-trip (state_persistence_test.cpp:112-137, midi_learn_integration_test.cpp:104-108).
- Impact: part (1) fully breaks the feature; parts (2) and (3) are latent because a mapping can only exist after a learn that never completes (getCCForParam at :1633 never succeeds, so the Clear and Save-with-Preset items never appear); no audio or crash impact; medium, with (2) and (3) to be fixed together with (1).

### D-057 — Closing the editor permanently disables the mod-slider overlay and the Tier-3 spectrum fallback until the host re-activates or reloads state

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** lifecycle-state | **Also flagged by:** ui-wiring, portability-quality, gestalt-controller; D-070 and D-045 overlap | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/controller.cpp:1517-1518](plugins/disrumpo/src/controller/controller.cpp#L1517-L1518), [:1581](plugins/disrumpo/src/controller/controller.cpp#L1581)

**Problem.** willClose sets cachedModOffsets_ = nullptr and sharedDisplay_ = nullptr. cachedModOffsets_ is set only when a 'ModOffsets' message arrives, and the processor sends it only from setActive(true) (processor.cpp:170). notify() also discards messages while editorClosing_ is true (controller.cpp:1962-1963). sharedDisplay_ is set only in setComponentState (controller.cpp:644-646). didOpen restores neither of them (controller.cpp:1354-1512).

**Why wrong.** State that the editor needs has to be re-acquired on each didOpen, or kept for the life of the controller. Here it is cleared on every close but only obtained from one-time lifecycle events.

**Consequence.** After the user closes and reopens the editor in the same session, the modulation-amount overlay on the band sliders stops updating (the timer condition `if (cachedModOffsets_ && activeEditor_)` at :1470 is false). On hosts that rely on the SharedDisplayBridge fallback, the spectrum analyser stays blank after reopening (the condition at :1476 needs sharedDisplay_).

**Evidence.** controller.cpp:1518 `cachedModOffsets_ = nullptr;`; :1581 `sharedDisplay_ = nullptr;`; :1962-1963 `if (editorClosing_.load(...)) return EditControllerEx1::notify(message);`; processor.cpp:170 `sendModOffsetsMessage();` (only call site, inside setActive(true)); :1470 `if (cachedModOffsets_ && activeEditor_)`.

**Suggested fix.** Keep the pointers across close and reopen and null them only in terminate or disconnect, or have didOpen request them again (for example, a 'RequestModOffsets' message the processor answers). Test: a controller test that delivers ModOffsets, then runs didOpen -> willClose -> didOpen and asserts that cachedModOffsets_ is non-null and the timer pushes offsets.

**Verifier notes:**
- Correctness: cachedModOffsets_ is written only in notify() on a "ModOffsets" message (controller.cpp:1966-1973), which the processor builds in sendModOffsetsMessage (processor_messaging.cpp:149-165) and sends only from setActive(true) (processor.cpp:170); willClose nulls it (:1518) and leaves editorClosing_ true until the next didOpen, so a message arriving while closed is dropped at :1962-1963; didOpen never requests it, so the :1470 guard stays false and updateModSliders never runs after a reopen; sharedDisplay_ is written only at :645-646.
- Context: nulling the pointer is not needed for safety because the mod-offsets array belongs to the processor's modulationEngine_ and outlives the editor (the comment at :1515-1516 cites timer races, but the timer is stopped at :1519); no other Krate controller re-acquires either pointer; Innexus has the same sharedDisplay_ pattern.
- Impact: the modulation overlay disappears in every host after the first editor close (a very common action), with no audio effect since the engine still applies the offsets; the spectrum part is narrow (only fallback hosts, D-070); medium.

### D-056 — setState adopts the stored instance ID, so duplicated instances collide in the SharedDisplayBridge registry and a controller can hold a dangling pointer

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** lifecycle-state | **Corroborated by measurement:** no | **CROSS-PLUGIN:** Innexus uses the same pattern (plugins/innexus/src/processor/processor_state.cpp:708-717)

**Location:** [plugins/disrumpo/src/processor/processor_state.cpp:903-917](plugins/disrumpo/src/processor/processor_state.cpp#L903-L917)

**Problem.** When the trailer is present, setState unregisters the processor's own random ID, sets instanceId_ to the stored ID and registers &sharedDisplay_ under it. registerInstance is a plain map assignment that overwrites any existing entry (shared_display_bridge.cpp:15-19), and unregisterInstance erases by ID (:21-25). Two live processors can end up with the same stored ID, for example after duplicating a track, copying plugin settings, or a project saved after that. In that case the second overwrites the first's entry, and terminate() of either erases the entry for both. The controller looks up the processor by the stored ID in setComponentState and keeps the raw pointer (controller.cpp:644-646).

**Why wrong.** The registry key has to be unique per live processor. A restored ID is only usable when no other live instance holds it. Otherwise the processor should keep its fresh ID, and the controller should learn it through the connection, not from the shared stream.

**Consequence.** On hosts that need the Tier-3 fallback, instance A's editor can show instance B's spectrum. If B is then deleted, A's controller holds sharedDisplay_ pointing into B's freed processor. The 33 ms timer passes those FIFOs to the SpectrumDisplay (controller.cpp:1476-1486), a use-after-free. Innexus uses the same adopt-ID pattern (plugins/innexus/src/processor/processor_state.cpp), so this is CROSS-PLUGIN.

**Evidence.** processor_state.cpp:911-914 `SharedDisplayBridge::instance().unregisterInstance(instanceId_); instanceId_ = static_cast<uint64_t>(storedId); SharedDisplayBridge::instance().registerInstance(instanceId_, &sharedDisplay_);`; shared_display_bridge.cpp:18 `registry_[id] = data;`; processor.cpp:81 terminate `unregisterInstance(instanceId_);`; controller.cpp:645 `sharedDisplay_ = static_cast<SharedDisplay*>(...lookupInstance(instanceId_));`.

**Suggested fix.** Do not adopt the stored ID when lookupInstance(storedId) returns another live entry. Better, stop deriving the controller's lookup key from the component state and send the live ID to the controller with an IMessage after connect. Make unregisterInstance(id, data) erase only when the entry's pointer matches. Test: two processors setState the same stream, then terminate one; lookupInstance must still return the survivor's display and never the terminated one's.

**Verifier notes:**
- Correctness: processor_state.cpp:907-915 adopts the stored ID; getState writes the live ID at :356, so duplicated tracks or copied states give two live processors one key; shared_display_bridge.cpp:18 overwrites and :24 erases by key with no owner check; terminate (processor.cpp:81) erases by the adopted ID; controller.cpp:641-646 caches the raw lookup pointer, cleared only at willClose (:1581).
- Context: the factory .vstpreset files carry no "KRAT" trailer (0 of 121), so only duplicated tracks, copied settings and host-saved user presets carry an ID; the header (shared_display_bridge.h:29) asks for a unique random uint64 and the adopt step breaks that; no Disrumpo test touches SharedDisplayBridge or instanceId; the wrong-pointer case needs another instance's processor setState to run between this instance's processor setState and its controller setComponentState (a batched project load or a controller-only state push).
- Impact (dissent on severity): the pointer is used only by the Tier-3 path (:1476), which engages only when no DataExchange block arrives within about 330 ms of editor open; with per-instance sequential loads each controller gets its own display and a later delete only loses the fallback; the use-after-free needs a shared ID plus an interleaved load order plus Tier 3; this verifier recommended low; medium retained (one of three).

### D-055 — Inserting ModSource::Rungler renumbered S&H/Pitch/Transient without a Disrumpo state migration

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** lifecycle-state | **Also flagged by:** gestalt-processor | **Corroborated by measurement:** no | **STATE-COMPAT**

**Location:** [plugins/disrumpo/src/processor/processor_state.cpp:765-770](plugins/disrumpo/src/processor/processor_state.cpp#L765-L770)

**Problem.** setState casts the stored source byte straight to Krate::DSP::ModSource. Commit 05a3ddb7 (2026-02-16) inserted Rungler = 10 and renumbered SampleHold 10->11, PitchFollower 11->12 and Transient 12->13 (modulation_types.h:47-50). Disrumpo stayed at kPresetVersion 9 until 2026-03-19 (342d04fd / f536a153). Released v0.9.0 (2026-02-02) and v0.9.1 (2026-02-03) wrote v9 streams with SampleHold=10, PitchFollower=11, Transient=12 (verified in git show disrumpo/v0.9.1). The controller parser (controller.cpp:466-469) also has no remap.

**Why wrong.** Changing an enum that is persisted needs a version gate and a migration. Ruinae bumped its state version in the same commit; Disrumpo did not, so v9 streams are now ambiguous.

**Consequence.** Projects saved with Disrumpo 0.9.0/0.9.1 that route S&H, Pitch Follower or Transient now load as Rungler, S&H and Pitch Follower. The modulation behaves differently, and nothing tells the user.

**Evidence.** processor_state.cpp:769-770 `routing.source = static_cast<Krate::DSP::ModSource>(std::clamp(static_cast<int>(source), 0, ...kModSourceCount - 1));`; git show disrumpo/v0.9.1:dsp/.../modulation_types.h:47-49 `SampleHold = 10, PitchFollower = 11, Transient = 12`; v0.9.1 plugin_ids.h:622 `kPresetVersion = 9`; current modulation_types.h:47-50 `Rungler = 10, SampleHold = 11, PitchFollower = 12, Transient = 13`.

**Suggested fix.** Decide how to tell the streams apart: no v9 streams from Feb 16 to Mar 19 can be distinguished by version, so use release history or the presence of trailer fields. At minimum, remap 10/11/12 -> 11/12/13 for version <= 8, and record the v9 ambiguity in plugin_ids.h version history. Test: a v9 stream fixture captured from v0.9.1 with an S&H routing must load as ModSource::SampleHold. STATE-COMPAT.

**Verifier notes:**
- Correctness: processor_state.cpp:765-770 casts the byte with only a clamp and the only routing migration is the v <= 11 dest remap at :779; `git log -S'Rungler = 10'` shows the insertion in 05a3ddb7; disrumpo/v0.9.1 (on origin) has SampleHold=10/PitchFollower=11/Transient=12, kPresetVersion = 9 (plugin_ids.h:622), writes the raw enum byte (processor.cpp:1011) and offered S&H/Pitch/Transient at indices 10-12 in its Route Source list; disrumpo/v0.9.4 (2026-03-15) still had kPresetVersion 9 with the new numbering, so v9 streams exist under both numberings.
- Context: 05a3ddb7's message says it added a "ModSource enum migration for v<13 presets" for Ruinae only; the plugin_ids.h version history (v9 -> v12) and CHANGELOG.md mention only the destination remap; grep of plugins/disrumpo/tests for SampleHold/Rungler finds only an unrelated LFO waveform test.
- Impact: a v0.9.0/0.9.1 project with an S&H routing loads as Rungler (a different chaotic shape), Pitch as S&H, Transient as Pitch Follower, with no warning; scope is user projects from the two early pre-1.0 releases using those three sources; medium.

**Fix-plan ruling (T2.15): v9 is read with the new numbering.**
- The v9 streams cannot be told apart. The getState of disrumpo/v0.9.1 and disrumpo/v0.9.4 / v0.9.6 differs only in loop syntax (diffed with `git show`), so the byte layout is identical. Neither writes a Rungler block or a trailer.
- v0.9.4, v0.9.5 and v0.9.6 wrote v9 with the new numbering; only v0.9.0 and v0.9.1 used the old one. setState and the controller parser therefore read v9 as new numbering.
- Streams of v8 and older are all older than 05a3ddb7: the last v8 writer is 044e5fff, 2026-02-01. Their sources 10, 11 and 12 move up by one (`migrateModSourceIndex`, state_codec.h), in the processor and in the controller.
- A v0.9.0/0.9.1 project that routes S&H, Pitch or Transient still loads one source down. This is the accepted cost of the ruling, recorded in the plugin_ids.h version history.
- Measured with the hand-written fixtures `tests/fixtures/disrumpo_state_v{8,9}_pre_rungler.bin`, routings 0-3 = stored sources 10, 11, 12, 9:
  - v8 before the fix: 10, 11, 12, 9 in the processor and the controller.
  - v8 after the fix: 11, 12, 13, 9 (SampleHold, PitchFollower, Transient, Chaos).
  - v9 after the fix: 10, 11, 12, 9, unchanged.
- v10+ streams are untouched. All 121 factory presets render and re-save bit-identically before and after the fix, and so does the v12 fixture. The presets are 119 at v10 plus Init_5_Bands v6 and Formant_Shift v8, which route no source at or above 10.

### D-054 — setState applies crossover frequencies before setBandCount, so the live band count drops or overwrites the saved values

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** lifecycle-state | **Also flagged by:** D-022 (signal-path), D-124 (test gap) report the same ordering | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor_state.cpp:450-463](plugins/disrumpo/src/processor/processor_state.cpp#L450-L463)

**Problem.** setCrossoverFrequency(c, f) is called for each saved crossover before crossoverL_/R_.setBandCount(bandCount). CrossoverNetwork::setCrossoverFrequency returns early when index >= numBands_-1, using the network's CURRENT band count (crossover_network.h:127-130). setBandCount then redistributes: from 1 band it runs a fresh log distribution (crossover_network.h:280-284), and when increasing it inserts log midpoints (:287-290).

**Why wrong.** The band count has to be applied first, or the frequencies stored unconditionally, so the saved values are applied whatever the instance's state was before.

**Consequence.** If a running instance is at 1 band and the user loads a host preset or undoes to a 4-band state, all three saved crossovers are discarded and replaced by 112/632/3556 Hz. From 2 bands, only crossover 1 survives and the other two are made up. The controller shows the saved Hz.

**Evidence.** processor_state.cpp:455-456 `crossoverL_.setCrossoverFrequency(c, freq); crossoverR_.setCrossoverFrequency(c, freq);` then :462 `crossoverL_.setBandCount(bandCount);`; crossover_network.h:128 `if (index < 0 || index >= numBands_ - 1) { return; }`.

**Suggested fix.** Call setBandCount(bandCount) before the crossover loop. Better, have setCrossoverFrequency always store crossoverFrequencies_[index] and apply it when the band becomes active. Test: on a prepared processor, set kBandCountId to 1 band, setState a 4-band stream with custom crossovers, then assert getCrossoverFrequency(0..2).

**Verifier notes:**
- Correctness: only the atomic bandCount_ is updated at :413 while the networks' numBands_ still holds the live value during the loop at :450-459; the guard at crossover_network.h:127-130 returns before writing crossoverFrequencies_[index]; from 1 band initializeLogarithmicDistribution (:253-268) gives about 112/632/3556 Hz at 4 bands and from 2-3 bands redistributeIncreasing (:301-365) makes up the rest; getState (:76) reads the made-up values back, so the loss persists into the next save.
- Context: nothing else re-applies the saved Hz (processor.h:179 crossoverFrequencies_ is unused, setupProcessing's prepare() resets to the log split, the param path at processor_params.cpp:992-993 runs only on host parameter changes, which hosts do not echo after setState); kDefaultBands = 4 masks it on a fresh instance; no processor-level setState round-trip checks crossovers (getCrossoverFrequency appears only in crossover_network_test.cpp).
- Impact: a host preset load or undo from a 1-3 band setting to a 3-4 band state moves the split points by large amounts (150 -> 112 Hz, 1500 -> 632 Hz) so each band's type acts on a different part of the spectrum while the UI shows the saved Hz; a separate point for the report: setupProcessing's prepare() also overwrites restored crossovers when setState precedes it (D-046); medium.

### D-045 — Mod-offset display uses a raw processor pointer sent once over IMessage

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** sweep-modulation | **Also flagged by:** D-057 covers the lost-on-close consequence | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor_messaging.cpp:149-165](plugins/disrumpo/src/processor/processor_messaging.cpp#L149-L165)

**Problem.** sendModOffsetsMessage puts the address of modulationEngine_'s modOffsets_ array into an IMessage as int64 'ptr'. It is sent only from setActive(true) (processor.cpp:170). Controller::notify casts it back and updateModSliders dereferences cachedModOffsets_[ms->getModDestId()] from the UI timer, with no lifetime handshake, no bounds check against kMaxModDestinations, and no atomics.

**Why wrong.** VST3 processor and controller may live in different processes or sandboxes (the IMessage contract carries data, not addresses), and either component can be terminated before the other's editor closes. The controller cannot know whether the pointer is still valid. If setActive(true) runs before connect() the message is never received, and notify drops it while editorClosing_ is set.

**Consequence.** In hosts that isolate the processor (out-of-process or bridged plug-ins, some sandbox modes) the UI thread dereferences a foreign address and the host crashes. If the processor is destroyed before the controller's timer stops, it is a use-after-free. In hosts that activate before connecting, the modulation rings on the sliders never show.

**Evidence.** processor_messaging.cpp:159-162 `// Send pointer to modulation offset array (safe: both components in-process) attrs->setInt("ptr", static_cast<Steinberg::int64>(reinterpret_cast<intptr_t>(modulationEngine_.getModOffsetsArray())));`. controller.cpp:1966-1975 `cachedModOffsets_ = reinterpret_cast<const float*>(static_cast<intptr_t>(ptr));`; controller.cpp:1980-1984 `float offset = cachedModOffsets_[ms->getModDestId()];`. modulation_engine.h getModOffsetsArray comment 'Audio thread writes, UI thread reads'.

**Suggested fix.** Send offset values, not addresses. Copy the 38 used destinations into the existing DataExchange block (or a small IMessage at about 30 Hz) and keep a controller-side copy. Test: an editor/controller test where notify receives a 'ModOffsets' message without a valid processor. The controller must not dereference it, and the values must arrive through the data path.

**Verifier notes:**
- Correctness: the message carries the raw address of modOffsets_ (std::array<float,128>, modulation_engine.h:858) and is sent only at processor.cpp:170; notify casts it back with no validation (controller.cpp:1966-1974) and the 33 ms timer reads plain non-atomic floats (:1470-1472, :1983) that the audio thread writes (modulation_engine.h:803); no lifetime handshake exists (Processor::terminate does not notify, only Controller::terminate :134 and willClose :1518 clear it); the "no bounds check" sub-claim has no impact because every modDestId comes from ModDest::bandParam (sub_controllers.h:173-187), well below 128.
- Context: the comment at :159 ("safe: both components in-process") shows the in-process assumption is deliberate and no mainstream host splits the components; no test covers ModOffsets; after willClose nulls the pointer, nothing re-sends it, so the overlay dies after any close and reopen (D-057), which is the consequence that occurs in every host.
- Impact (dissent on severity): the cross-process crash needs a host that splits address spaces and the use-after-free needs the Processor freed while the editor timer runs, which hosts avoid by closing the editor first; the real consequence is the dead display after reopen; this verifier recommended low; medium retained (one of three).


### D-044 — setState custom-curve rebuild loops forever on an interior breakpoint near x=0 or x=1

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** sweep-modulation | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor_state.cpp:552-555](plugins/disrumpo/src/processor/processor_state.cpp#L552-L555)

**Problem.** setState clears the curve with `while (customCurve_.getBreakpointCount() > 2) { customCurve_.removeBreakpoint(1); }`. CustomCurve::removeBreakpoint returns false without removing anything when breakpoints_[index].x <= 0.001 or >= 0.999 (the 'endpoint' heuristic). addBreakpoint (used by the same load at :564-567) accepts any clamped x, including 0.0, 1.0 and 0.9995. So a curve that has gone through one load with such an interior point keeps it at index 1.

**Why wrong.** Loading state must terminate and must fall back to defaults on bad data (FR-021). The loop's progress depends on removeBreakpoint succeeding, which the heuristic does not guarantee. Endpoints should be identified by index (0 and count-1), not by an x threshold.

**Consequence.** Load a stream whose custom curve has an interior point at x in [0, 0.001] or [0.999, 1], e.g. count 3 with (0,0),(1.0,0.5),(1,1). The curve becomes {0, 1, 1}. getState writes it back, and the next setState on any instance spins forever in the while loop on the host's thread, hanging the DAW on project/preset load. No factory preset contains such a curve (the generator writes 2 points), so today only corrupted or crafted states trigger it, but the CustomCurveEditor would make it reachable once wired.

**Evidence.** processor_state.cpp:552-555 `pointCount = std::clamp(pointCount, 2, 8); // Clear and rebuild custom curve while (customCurve_.getBreakpointCount() > 2) { customCurve_.removeBreakpoint(1); }` and :564-567 `customCurve_.addBreakpoint(px, py);`. custom_curve.h:150-152 `// Don't allow removing endpoints if (breakpoints_[index].x <= 0.001f || breakpoints_[index].x >= 0.999f) { return false; }`; addBreakpoint :121-127 clamps x to [0,1] and appends with no endpoint check.

**Suggested fix.** Rebuild the curve with customCurve_.reset() and then add points, or make removeBreakpoint treat only indices 0 and count-1 as endpoints. Also reject non-finite x/y with detail::isFinite. Test: setState a v12 stream with a 3-point curve whose middle x = 1.0, getState, then setState again. Assert it returns within the test (today it hangs) and that the curve has at most 3 points with x sorted and in [0,1].

**Verifier notes:**
- Correctness: the loop at :552-554 never checks removeBreakpoint's return; custom_curve.h:154-157 returns false whenever x <= 0.001 or >= 0.999; addBreakpoint (:119-136) only clamps and sortBreakpoints (:225-231) forces x to 0/1 only at indices 0 and count-1; a fresh instance (count 2) loads the bad stream without hanging, so the hang is on that instance's second setState, not "any instance"; a NaN x does not trigger it because NaN fails both comparisons.
- Context: pointCount is clamped to 2..8 (:550) but x is never validated; customCurve_ is changed only by setState (the curve params at processor_params.cpp:495-522 are no-ops), so no automation or controller edit can create the interior point; no test pins the behaviour.
- Impact (dissent on severity): the bad state can come only from a hand-crafted or corrupted .vstpreset or project chunk, since real Disrumpo never writes such a curve and the editor wiring does not exist yet; pluginval does not fuzz bytes; this verifier recommended low; medium retained (one of three).

### D-043 — Sweep LFO tempo sync ignores host tempo (setTempo never called, fixed 120 BPM)

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** sweep | **Dimension:** sweep-modulation | **Also flagged by:** gestalt-processor | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor.cpp:236-248](plugins/disrumpo/src/processor/processor.cpp#L236-L248)

**Problem.** process() reads processContext->tempo only into the ModulationEngine BlockContext. SweepLFO::setTempo (sweep_lfo.h) is never called anywhere in plugins/disrumpo/src/processor, so the synced sweep LFO uses Krate::DSP::LFO's default bpm_ = 120.

**Why wrong.** Tempo sync (kSweepLFOSync + kSweepLFONoteValue, FR-024) must follow the host transport tempo. The mod-engine LFOs get setTempo(ctx.tempoBPM) every block (modulation_engine.h:190-191), and the sweep LFO should too.

**Consequence.** At any tempo other than 120 BPM a synced sweep LFO (for example 1/4) drifts against the beat. Combined with the per-block advance (D-035), its period is also block-size dependent.

**Evidence.** grep 'setTempo' in plugins/disrumpo/src/processor/*.cpp: no hits. sweep_lfo.h `void setTempo(float bpm) noexcept { lfo_.setTempo(bpm); }` has no caller. lfo.h:546 `float bpm_ = 120.0f;`, lfo.h:488-493 tempoSyncFrequency_ = (bpm_/60)/beatsPerNote. processor.cpp:241-244 tempo goes only to modCtx.tempoBPM.

**Suggested fix.** Call sweepLFO_.setTempo(processContext->tempo) each block when kTempoValid is set. Test: 90 BPM context, sweep LFO synced to 1/4. Assert a period of 0.667 s (after the per-block fix) by tracking kSweepModulatedFrequencyOutputId.

**Verifier notes:**
- Correctness: processor.cpp:240-244 copies tempo only into modCtx.tempoBPM; every sweepLFO_ call site (processor.cpp:135/160/349-350, processor_params.cpp:426-460, processor_state.cpp:99-527) is a setter, getter, prepare, reset or process, never setTempo; SweepLFO::setTempo (sweep_lfo.h:142-144) forwards to lfo_.setTempo and has no production caller; lfo.h:546 bpm_ = 120 and :488-493 compute (bpm_/60)/beatsPerNote.
- Context: the only test (tests/dsp/sweep_lfo_tests.cpp:53) sets setTempo(120) by hand, exactly the default, so it cannot catch the missing wiring; specs-overview.md:114 and roadmap.md:483 list tempo sync as a feature with no fixed-tempo note.
- Impact: the rate is off by 120/hostBPM (33 % fast at 90 BPM, 14 % slow at 140), correct only at 120; today the per-block advance bug (D-035) hides it, and once that is fixed this becomes the main sync error; one-line fix on a user-facing feature; medium.

### D-042 — getState saves the modulated sweep frequency/width/intensity, not the user's base values

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** sweep-modulation | **Also flagged by:** lifecycle-state; D-049/D-041 cover the restore side | **Corroborated by measurement:** yes (probe `preset round-trip loss`)

**Location:** [plugins/disrumpo/src/processor/processor_state.cpp:86-91](plugins/disrumpo/src/processor/processor_state.cpp#L86-L91)

**Problem.** getState writes normalizeSweepFrequency(sweepProcessor_.getTargetFrequency()), (getWidth()-0.5)/3.5 and getIntensity()/2. Each block, process() overwrites those fields with the modulated values: base plus mod-engine offset, plus sweep LFO and envelope octave shifts (processor.cpp:268-290, 346-368).

**Why wrong.** Persisted state must hold the parameter base values (baseSweepFrequency_, baseSweepWidthNorm_, baseSweepIntensityNorm_). Modulation is transient and is re-applied on top after load.

**Consequence.** Saving a project while the sweep LFO, envelope follower or a mod route is active stores an instantaneous modulated snapshot as the base. On reload the sweep centre or width differs from what the user set, and repeated save/load cycles drift it further. Through setComponentState the controller also shows the snapshot as the knob value.

**Evidence.** processor_state.cpp:86-91 `streamer.writeFloat(normalizeSweepFrequency(sweepProcessor_.getTargetFrequency())) ... writeFloat((sweepProcessor_.getWidth() - 0.5f) / 3.5f) ... writeFloat(sweepProcessor_.getIntensity() / 2.0f)`. processor.cpp:368 `sweepProcessor_.setCenterFrequency(modulatedFreq);` with modulatedFreq including the LFO (:349-355) and envelope (:358-360). sweep_processor.h setCenterFrequency sets targetFreqHz_, which getTargetFrequency returns.

**Corroborated by measurement.** probe preset round-trip loss (35 presets): "determinism 0 for all 121; roundtrip > 1e-5 for 35, e.g. Pitch_Tracker 1.25, Three_Node_Blend 0.522, Sharp_Focus 0.248, Chaos_Morph_4 0.226 ... first drift at offset 92 (sweep LFO rate ...) in 12 presets, and offsets 136/149 in others". The layout puts offset 92 at the sweep LFO rate and 136/149 at the LFO1/LFO2 rates. Code: :319 `streamer.writeInt8(static_cast<Steinberg::int8>(0)))  // morphMode`, :322 `streamer.writeFloat(0.0f))  // morphSmoothing (ms)`, :103 `constexpr float kMinRateLog = -4.6052f;  // ln(0.01)`

**Suggested fix.** Write normalizeSweepFrequency(baseSweepFrequency_), baseSweepWidthNorm_ and baseSweepIntensityNorm_. Test: set base freq norm 0.5, enable the sweep LFO at depth 1, and process blocks until the LFO is away from 0 (with the per-block LFO fixed). getState and assert the stored freq norm == 0.5 within 1e-4.

**Verifier notes:**
- Correctness: getState reads sweepProcessor_'s getters (processor_state.cpp:87/:89/:91 -> sweep_processor.h:183-185, :193-195, :198-200), which process() rewrites every block from the modulated values (processor.cpp:268-274, :277-289, :346-368, setCenterFrequency writes targetFreqHz_ at sweep_processor.h:124); the base values exist (processor.h:224-230) but getState never reads them; setState stores the snapshot into baseSweepFrequency_ (:482) so it becomes the new base and drifts on each cycle.
- Context: no comment or spec says the snapshot is intentional; the host calls getState at any time including while the LFO runs; a related restore bug (setState never stores baseSweepWidthNorm_/baseSweepIntensityNorm_, D-049) masks the width/intensity part of this finding, and the fix should pair with restoring those atomics.
- Impact: with the sweep LFO at depth 1 the saved centre can be off by up to ±2 octaves (1 kHz saved anywhere from 250 Hz to 4 kHz), the envelope adds up to +2 octaves during loud input, and every save/load compounds it; only when sweep modulation is active at save time; medium.

### D-033 — Same-family interpolation blends DistortionParams fields whose meaning differs per type (Fuzz bias unipolar 1=centered vs Tube/SoftClip bipolar 0)

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** morph | **Dimension:** morph-oversampling | **Also flagged by:** D-082 covers the kNodeBias side | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/morph_engine.cpp:425](plugins/disrumpo/src/dsp/morph_engine.cpp#L425), [:534](plugins/disrumpo/src/dsp/morph_engine.cpp#L534), [:709](plugins/disrumpo/src/dsp/morph_engine.cpp#L709)

**Problem.** interpolateParams accumulates result.bias from every node in the family, but the field has type-specific semantics. Tube/Tape map it bipolar [-1,1] with 0 neutral (processor_params.cpp:56,63). Fuzz/AsymFuzz store it unipolar, where 1.0 is centered and 0 is maximum offset (processor_params.cpp:73,81; fuzz_processor.h:492 `biasOffset = (1 - bias)*0.8`). SoftClip/HardClip never set it and keep the default 0.0. The bias mod offset is then added and clamped to [-1,1] for every type (morph_engine.cpp:709).

**Why wrong.** Interpolating one parameter between two types is only meaningful when it means the same thing on both. A SoftClip node (bias 0) and a centered Fuzz node (bias 1.0) at wFuzz = 0.6 give the dominant Fuzz bias 0.6, an offset of 0.32. The morph injects a gated, sputtery bias that neither endpoint has. Tube to Fuzz at 0.5/0.5 with the Tube neutral (0) and the Fuzz centered (1) gives the same distortion.

**Consequence.** Saturation-family morphs that include Fuzz/AsymFuzz change Fuzz character as the cursor moves, even when both nodes are at their neutral bias. A bias mod offset can push Fuzz bias negative, outside its domain.

**Evidence.** morph_engine.cpp:425 `result.bias = 0.0f;` :534 `result.bias += w * p.bias;` :709 `bp.bias = std::clamp(bp.bias + biasModOffset_ * 2.0f, -1.0f, 1.0f);`; processor_params.cpp:56 `p.bias = slots[0] * 2.0f - 1.0f;` :73 `p.bias = slots[0];  // Fuzz bias is unipolar [0,1]`; distortion_adapter.h:81 `float bias = 0.0f; ///< Asymmetry [-1, 1]`; fuzz_processor.h:492 `const float biasOffset = (1.0f - biasValue) * 0.8f;`.

**Suggested fix.** Normalize per-type fields to a common semantic before interpolation (e.g. store fuzz bias as bipolar and convert in routeParams), or interpolate only between nodes of the same type (see the same-family hard-switch finding, D-028). Test: SoftClip+Fuzz same-family morph at wFuzz 0.6 with the Fuzz slot0 at 1.0 (centered); assert blendedAdapter_.getParams().bias maps to fuzz offset 0.

**Verifier notes:**
- Correctness: distortion_types.h:300-306 puts SoftClip, HardClip, Tube, Tape, Fuzz and AsymFuzz in one family, so processSameFamily runs (morph_engine.cpp:678-679); interpolateParams zeroes bias (:425) and sums w*p.bias (:534) with no per-type conversion, and the blended adapter is set to the dominant type (:706); distortion_adapter.cpp:637/:646 pass p.bias straight to fuzz_.setBias, which clamps to [0,1] (fuzz_processor.h:365) and applies biasOffset = (1-bias)*0.8 (:492); the example (SoftClip 0 + Fuzz 1.0 at 0.6 gives offset 0.32) checks out; the node-level kNodeBias (processor_params.cpp:868-870) also maps bipolar for Fuzz.
- Context: dsp-details.md:1244-1251 says same-family morphs use one instance with interpolated parameters but nothing covers fields whose meaning differs per type; SoftClip/HardClip never write bias so it keeps the default or a stale value from a previous type.
- Impact: the "outside its domain" sub-claim is overstated because FuzzProcessor::setBias clamps to [0,1], so a negative value just means maximum offset; the real effect is a pre-clip DC shift (e.g. 0.32 on a unity-gain signal at the default sustain) producing even harmonics and gating that neither endpoint has, bounded and DC-blocked, so timbral; medium.

### D-032 — Type changes, node activation and family-path switches reuse processor state without a reset or crossfade; picking Spectral on a node allocates on the audio thread

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** morph | **Dimension:** morph-oversampling | **Also flagged by:** D-002 (allocation), D-080 (stale state), D-028 (hard switch) | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/morph_engine.cpp:131-152](plugins/disrumpo/src/dsp/morph_engine.cpp#L131-L152), [:675-682](plugins/disrumpo/src/dsp/morph_engine.cpp#L675-L682), [:743-757](plugins/disrumpo/src/dsp/morph_engine.cpp#L743-L757)

**Problem.** setNodes runs from processParameterChanges on the audio thread for every node parameter change. It calls setType/setParams on all 4 adapters, and DistortionAdapter::setType only sets currentType_ (no reset). The newly selected processor inside the adapter resumes with whatever delay-line, grain, hysteresis or filter content it had from its last use. The cross-family loop also skips nodes with weight below 0.001, so their adapters freeze and later restart on live audio with stale state. When a node type change flips allSameFamily_, output switches between blendedAdapter_ and adapters_ within one sample, with no crossfade. Every adapter's spectral_ is prepared with fftSize = maxBlockSize*8 = 4096, so the first setParams of a node set to Spectral (default fftSize 2048) calls spectral_.prepare(), which allocates inside process().

**Why wrong.** State-holding processors need a reset or a short fade when they are (re)activated or re-assigned. Switching between two independent processing paths needs a crossfade. SpectralDistortion::prepare is documented not real-time safe.

**Consequence.** Clicks and bursts of old audio (FeedbackDist comb, Granular buffer, AllpassResonant ringing) when a node's type changes, when the active node count increases, or when a node's weight rises from 0. A level jump when a type change crosses a family boundary. Heap allocation on the audio thread when the user picks Spectral or moves its FFT-size slot.

**Evidence.** distortion_adapter.cpp:142-162 setType sets currentType_/DC flag/latency only; morph_engine.cpp:136-140 setType/setParams on all adapters in setNodes; :745 `if (weight < kWeightThreshold) continue;` (adapter not run); :678-681 path switch; processor_params.cpp:899 setMorphNodes on every node param; band_processor.h:124-125 prepare(..., maxBlockSize_*8) -> distortion_adapter.cpp:84 `spectral_.prepare(sampleRate, maxBlockSize)` then :894-895 re-prepare on fftSize mismatch.

**Suggested fix.** Reset an adapter when its type changes (setType compares and calls reset of the new processor) and when a node goes from inactive to active. Crossfade over ~5 ms when allSameFamily_ flips. Pre-prepare spectral_ for all four FFT sizes (512-4096) at prepare time and switch between them without allocating. Test: process a FeedbackDist node, switch it to SoftClip and back. Assert no output above the input peak in the first 10 ms, and run an allocation counter around processParameterChanges when a node is set to Spectral.

**Verifier notes:**
- Correctness: processor_params.cpp:839-899 (and :954 for kBandActiveNodes) call setMorphNodes inside processParameterChanges, reaching morph_engine.cpp:136-140 setType/setParams/setCommonParams on all four adapters; setType (distortion_adapter.cpp:142-162) never resets and reset() (:98-136) is reached only from MorphEngine::reset (:72-94); skipped nodes (:745, :749) freeze; :675-682 switches paths per sample with no crossfade; the prepare chain (band_processor.h:111-125 -> morph_engine.cpp:57-60 -> distortion_adapter.cpp:84, spectral_distortion.h:145) gives fftSize 4096 at a 512 block against the 2048 default, and :893-896 re-prepares, which is documented not RT-safe (spectral_distortion.h:144) and calls pffft_new_setup (fft.h:157).
- Context (upgrade proposed): processSameFamily calls setParams every sample with a truncated fftSize (morph_engine.cpp:606, :706-710), so with two Spectral nodes at 0.6/0.4 the re-prepare repeats on every sample (this is D-001/D-007); nothing in specs/Disrumpo covers a crossfade for type or family switches; no test pins the behaviour.
- Impact: every trigger is a discrete user action (type change, FFT slot, node count) and the allocation per action happens once, so medium fits for this finding while the per-sample variant is filed separately; the low-weight re-entry part is weaker because a node re-enters at a gain of about -60 dB; the shared L/R engine (band_processor.h:487-488) is a distinct, more severe defect (D-016).

### D-031 — Cross-family path leaves the last modulated drive/mix/tone/bias stuck in the node adapters when the mod offsets return to exactly 0

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** morph | **Dimension:** morph-oversampling | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/morph_engine.cpp:726-738](plugins/disrumpo/src/dsp/morph_engine.cpp#L726-L738)

**Problem.** processCrossFamily re-applies node params plus modulation offsets to adapters_[i] only while some offset is non-zero. When every offset becomes exactly 0.0 (routing removed, source set to None, amount 0, macro at 0), the block is skipped and nothing restores nodes_[i].commonParams/params. The adapters keep the last modulated values until the next setNodes, which happens only on a node or band parameter change.

**Why wrong.** With no modulation, the audio must follow the base node parameters. The same-family path recomputes from base every sample and behaves correctly, so the two paths disagree under identical modulation.

**Consequence.** Remove a drive routing while the LFO is at its peak and a cross-family band stays at drive +X*10 (up to +10) indefinitely. The sound then differs from the knob values and from a preset reload.

**Evidence.** morph_engine.cpp:726-727 `if (driveModOffset_ != 0.0f || mixModOffset_ != 0.0f || toneModOffset_ != 0.0f || biasModOffset_ != 0.0f) {` :733 `adapters_[i].setCommonParams(cp);` :736 `adapters_[i].setParams(p);` (no else branch); processor.cpp:321-330 offsets pushed every block from modulationEngine_.getModulationOffset.

**Suggested fix.** Track whether offsets were active on the previous sample or block. On the transition to all-zero, re-apply base params once (setCommonParams then setParams per active node). Better, compute the effective params per block in setDriveMixModOffset/setToneBiasModOffset instead of per sample. Test: cross-family band with drive offset 0.5 for one block, then 0. Assert adapters_[0].getCommonParams().drive equals the node's base drive and the output RMS equals the never-modulated render.

**Verifier notes:**
- Correctness: the only writes to adapters_[i] are prepare() (:64-66), setNodes() (:137-139) and the guarded block at :726-738 with no else branch; setNodes is reached only through BandProcessor::setMorphNodes (band_processor.h:247-249) from processor.cpp:126, processor_params.cpp:899/954 and processor_state.cpp:894, none per block; the offsets are pushed every block (processor.cpp:321-330 -> band_processor.h:296-316 -> morph_engine.cpp:115-123) and become exactly 0.0f when a routing is inactive (modulation_engine.h:746-748, :753, :803).
- Context: the non-morph path saves and restores base params around each block (band_processor.h:438-463) and the same-family path recomputes from base every sample (:706-716), so only the cross-family path lacks the restore; no test calls setDriveMixModOffset or setToneBiasModOffset; no spec or comment describes the latch as intended.
- Impact: frozen values up to ±10 drive (offset*10, clamped), ±7800 Hz tone, ±1 mix and ±2 bias (clamped ±1) persist until a node/band parameter change or a state reload; a 0.5 drive offset left in place is +5 drive units; mixed-family node sets are common (defaults SoftClip, Tube, Fuzz, SineFold); medium.

### D-030 — Integer morph parameters (stages, iterations, fftSize, magnitudeBits) are interpolated by summing truncated terms, so they land below both endpoints

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** morph | **Dimension:** morph-oversampling | **Also flagged by:** D-011 (dsp-numerics) reports the same arithmetic | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/morph_engine.cpp:467](plugins/disrumpo/src/dsp/morph_engine.cpp#L467), [:505](plugins/disrumpo/src/dsp/morph_engine.cpp#L505), [:576](plugins/disrumpo/src/dsp/morph_engine.cpp#L576), [:614](plugins/disrumpo/src/dsp/morph_engine.cpp#L614)

**Problem.** interpolateParams zeroes the int fields and does `result.stages += static_cast<int>(w * p.stages)` (same for iterations, magnitudeBits, fftSize), truncating each node's term. Two Fractal nodes with iterations 4 at 0.6/0.4 give int(2.4)+int(1.6) = 3. Two RingSaturation nodes with stages 2 at 0.3/0.7 give int(0.6)+int(1.4) = 1. Two stages-1 nodes at 0.5/0.5 give 0, which setStages then clamps to 1.

**Why wrong.** Interpolating between two nodes with identical discrete settings must return that setting. A weighted sum of floors is biased downward by up to N-1 units for N nodes, and it moves as the cursor moves. Discrete structural parameters should come from the dominant node, or be rounded from a float-accumulated value.

**Consequence.** Fractal depth, ring-sat stages and feedback filter stages change audibly as the cursor moves between nodes that have the same value, and they step up and down at fractional weights. That is a timbre change with zipper/click edges. The same mechanism causes the critical per-sample Spectral re-prepare (D-001/D-007).

**Evidence.** morph_engine.cpp:467 `result.stages = 0;` :505 `result.iterations = 0;` :576 `result.stages += static_cast<int>(w * static_cast<float>(p.stages));` :614 `result.iterations += static_cast<int>(w * static_cast<float>(p.iterations));`; distortion_adapter.cpp:777 `ringSaturation_.setStages(std::clamp(p.stages, 1, 4));` :818 feedbackDist_.setStages(p.stages); :929 fractal_.setIterations(p.iterations); processor_params.cpp:169,185,257 stage/iteration mapping.

**Suggested fix.** Accumulate in float and std::lround at the end, or take the value from the dominant node like the other discrete fields. Test: two same-type nodes with identical stages/iterations, sweep weights 0..1, assert the interpolated params equal the node value at every step.

**Verifier notes:**
- Correctness: processSameFamily (:691) calls interpolateParams at :708 and blendedAdapter_.setParams at :710 every sample, and setParams routes immediately (distortion_adapter.cpp:173-175); the int fields are zeroed (:467, :497, :498, :505) and summed as truncated terms (:576, :606-607, :614) for nodes at or above kWeightThreshold (:529); the values land at RingSaturation setStages (clamp 1-4, :777), Feedback setStages (:818, no clamp) and Fractal setIterations (:929); node values are 1-4 stages and 1-8 iterations (processor_params.cpp:169, :185, :257).
- Context: downstream only clamps to range (ring_saturation.h:187, feedback_distortion.h:486, fractal_distortion.h:267), hiding 1->0 but not 4->3; the only pinning test (morph_interpolation_test.cpp:405+) uses iterations 4 at exactly 50/50 where the truncation does not show; the comment at :421-422 says discrete fields are kept from the dominant node, so these fields were wrongly listed as continuous.
- Impact: for two nodes at 4/4 the sum reads 4 only at dyadic weights (0.25, 0.5, 0.75) and 3 almost everywhere else, so the depth drops by one on leaving a node and by up to N-1 with more nodes; FeedbackDist reconfigures all filters on each stage change (feedback_distortion.h:487-489); magnitudeBits has no effect because the adapter uses p.bitDepth for Spectral (:922); medium.

### D-029 — Linear1D mode: nodes C and D share posX with A and B, so they never get distinct weights, and the on-node snap at the axis ends drops C/D instantly

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** morph | **Dimension:** morph-oversampling | **Also flagged by:** D-100 (spec-conformance), D-155 (measured the -3 dB consequence) | **Corroborated by measurement:** partly (D-155)

**Location:** [plugins/disrumpo/src/dsp/morph_engine.cpp:180-193](plugins/disrumpo/src/dsp/morph_engine.cpp#L180-L193)

**Problem.** Linear1D weights use only |posX - node.posX|. The processor fixes node positions at the corners: A(0,0), B(1,0), C(0,1), D(1,1). Nothing in src/processor ever writes posX/posY. With 3-4 active nodes, C always gets exactly A's weight and D exactly B's. When the cursor reaches x < 0.001, the loop returns at i=0 with A=1 and C=0. At x=0.0011, A and C are each ~0.5.

**Why wrong.** FR-MORPH-002 defines 1D Linear as a single axis A<->B<->C<->D. The current code makes C/D unreachable as independent stops and creates a discontinuity at both ends of the axis. Weight should be continuous in cursor position: the on-node shortcut assumes node positions are unique, and here they are not.

**Consequence.** In Linear1D with 3 or 4 nodes, moving the slider fully left (or right) cuts node C (or D) out in one sample. That is a level and timbre step and a click in cross-family mode. The nodes the user configured at C/D cannot be isolated at all.

**Evidence.** morph_engine.cpp:185 `distances[i] = calculate1DDistance(posX, nodes_[i].posX);` :188-191 `if (distances[i] < kOnNodeThreshold) { weights_.fill(0.0f); weights_[i] = 1.0f; return; }`; processor.cpp:114-121 nodes at (0,0),(1,0),(0,1),(1,1); grep posX|posY in src/processor: no matches.

**Suggested fix.** For Linear1D, place active nodes at evenly spaced 1-D positions (i/(N-1)) and interpolate between adjacent nodes piecewise-linearly, or derive 1-D positions explicitly instead of reusing the 2-D corner posX. Make the on-node test pick a unique node. Test: Linear1D with 4 cross-family nodes, sweep x from 0.01 to 0, assert no weight jump > 0.05 between adjacent cursor steps, and assert each of A..D reaches weight 1 at some x.

**Verifier notes:**
- Correctness: the cache nodes are fixed corners (processor.cpp:114-120, band_state.h:49-52) and reach the engine unchanged (processor_params.cpp:949-955 -> band_processor.h:247-249 -> morph_engine.cpp:131-133); weights are recomputed per sample (:672) with 1D distance (:185, :274-275) and the on-node return (:188) hits A before C; at x = 0.0011 A and C are each about 0.5 and below 0.001 A = 1, C = 0, a discontinuity the smoother only times.
- Context: kBandActiveNodes (processor_params.cpp:949-953) allows 2-4 and Linear1D is the BandState default (band_state.h:42); specs-overview.md:230-233, dsp-details.md:151 and distortion_types.h:378 all describe a sequential axis; morph_mode_test.cpp:115-133 uses its own 0/0.5/1.0 node layout, so the processor's corner layout with 3-4 nodes is untested; the morph pad also keeps the corner layout in Linear1D (morph_pad.cpp:167-172, :457-468).
- Impact: with 4 nodes at x just above 0.001 the cross-family output is 0.354*(A+C) (calculateTransitionGain(0.5) = 0.707) and one sample later 1.0*A, a step of about +3 dB (correlated) to +6 dB (uncorrelated) plus C vanishing; in same-family mode the interpolated params jump from mean(A,C) to A; the step needs C or D to differ from A or B; medium.

### D-026 — No latency or tail reporting: Spectral/Feedback/Granular wet delays misalign bands and dry; tails can be cut

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** signal-path | **Dimension:** signal-path | **Also flagged by:** D-098, D-081 (latency), D-108 (tail) | **Corroborated by measurement:** partly (probe `tail:FeedbackDist` -47.9 dBFS)

**Location:** [plugins/disrumpo/src/processor/processor.cpp:180-520](plugins/disrumpo/src/processor/processor.cpp#L180-L520)

**Problem.** The processor never calls setLatencySamples and does not override getLatencySamples or getTailSamples (grep in src finds none). DistortionAdapter computes blockLatency_ for Spectral (fftSize) and Granular (grain length) (distortion_adapter.cpp:150-181), but getProcessingLatency has no caller. FeedbackDistortion's wet output is only the delayed feedback path (feedback_distortion.h:261, returns `processed` at :334), so its wet is a 1-100 ms delayed copy. BandProcessor::getLatency is a hard-coded 0 (band_processor.h:376-379).

**Why wrong.** When one band carries a block-latency type (Spectral fftSize, 2048 calls by default) and others do not, the bands are summed out of time alignment, and the dry path at mix < 100 % is not delayed to match. This is phase smear and combing at band edges and against dry. Types with tails (FeedbackDist, AllpassResonant Karplus/feedback topologies) report kNoTail (SDK default), so hosts that suspend processing on silent input truncate them.

**Consequence.** Smeared transients and comb filtering when a Spectral/Granular/Feedback node is active in one band or the global mix is below 100 %. Host PDC never compensates the plugin, and ringing tails are cut off when input stops in hosts that use silence detection.

**Evidence.** grep `getLatencySamples|setLatencySamples|getProcessingLatency|getTailSamples` in plugins/disrumpo/src: only distortion_adapter.cpp:248 definition; distortion_adapter.cpp:179 `blockLatency_ = params.fftSize;`; band_processor.h:376-379 `static int getLatency() { return 0; }`; feedback_distortion.h:261 `float delayed = delayLine_.readLinear(delaySamples);` ... :334 `return processed;`

**Suggested fix.** Decide a fixed worst-case latency (e.g. max Spectral fftSize at base rate), report it via setLatencySamples, and delay every band and the dry path to that common latency. Alternatively, keep Spectral out of the per-band sum by aligning per-band delays to the maximum active latency (with a latency-change restartComponent). Override getTailSamples to return kInfiniteTail or a bound when feedback types are active. Test: 2 bands, band 1 Spectral, band 0 drive-0 passthrough, impulse in. Assert both bands' impulse peaks land on the same output sample and that getLatencySamples() equals that offset.

**Verifier notes:**
- Correctness: grep finds only the getProcessingLatency definition (distortion_adapter.cpp:248-249) and its unit tests (distortion_adapter_test.cpp:133-149); the processor never sets latency and inherits kNoTail; Spectral's process (spectral_distortion.h:217-243) returns 0 until the OLA has output and latency() = fftSize (:490-492), default 2048 (distortion_adapter.h:179, 512-4096 range), about 46 ms at 44.1 kHz against the "< 10ms" target (specs-overview.md:66); the adapter blend (:236-237) and the global dry path are undelayed.
- Context: specs-overview.md:374 requires 0-256 samples; no design note sanctions the gap; FeedbackDistortion's delayed-only wet is the intended comb resonator (feedback_distortion.h:254-258, :332-334, getLatency() constexpr 0 at :527-529) and Granular's lookback is a user-set effect, so those parts overreach; the tail part holds (kNoTail with Feedback/Allpass ringing and the STFT's pending output).
- Impact: Spectral's recommended factor is 1x (distortion_types.h:188-190), so a Spectral band sums 46 ms (default) late and at mix below 100 % produces a flam/echo rather than fine combing; no PDC in any host; the tail truncation depends on the host's silence-suspend behaviour; medium.

### D-025 — Input gain, output gain and global mix are block-stepped with no smoothing; output gain does not touch the dry path

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** signal-path | **Dimension:** signal-path | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor.cpp:449-457](plugins/disrumpo/src/processor/processor.cpp#L449-L457), [:469-470](plugins/disrumpo/src/processor/processor.cpp#L469-L470), [:502-503](plugins/disrumpo/src/processor/processor.cpp#L502-L503)

**Problem.** inputGainLinear, outputGainLinear, wetMix and dryMix are computed once per block from the last parameter point (processor_params.cpp:316) plus the block-rate mod offset, then held constant across the sample loop. Output gain multiplies only the wet sum, `inputL[n] * dryMix + sumL * outputGainLinear * wetMix`, and input gain likewise reaches only the wet path.

**Why wrong.** Gain and mix changes on the audio path must be smoothed (per-sample ramp or OnePoleSmoother) to avoid zipper noise. An 'Output Gain' control (specs-overview.md:321,363) is conventionally post-mix. Here it does nothing at mix 0 and changes the dry/wet balance at intermediate mixes.

**Consequence.** Audible zipper or step noise when automating or modulating input gain, output gain or mix, worse with large host blocks (one step per block of up to +/-48 dB range). Output gain cannot trim the overall output level.

**Evidence.** processor.cpp:453-457 constants per block; :469 `inputL[n] * inputGainLinear` only into the crossover; :502 `outputL[n] = inputL[n] * dryMix + sumL * outputGainLinear * wetMix;`

**Suggested fix.** Add OnePoleSmoothers (10-20 ms) for input gain, output gain and wet mix, advanced per sample. Apply output gain to (dry*dryMix + wet*wetMix). If 'input gain only drives the distortion' is intended, document it. Test: automate output gain 0 -> 1 normalized in one block on a 1 kHz sine. Assert max |out[n]-out[n-1]| < 1.5x the sine's slope bound. Also check that at mix 0 output gain +6 dB raises the output by 6 dB.

**Verifier notes:**
- Correctness: processor_params.cpp:312-337 keeps only the last point of each queue and stores the raw normalized value; processor.cpp:260-265 reads the atomics once per block through getModulatedValue, :449-457 converts them to block constants, and the loop at :467-504 uses them unchanged; no Smoother exists for these three parameters in src/processor; :469-470 applies input gain only to the crossover input and :502-503 applies output gain only to the wet sum.
- Context: the spec's signal flow (plans-overview.md:44-70) puts Output Gain AFTER Global Mix and plans-overview.md:157 asks for "Artifact-free smoothing", so the wet-only placement is a spec deviation; input gain feeding only the wet path is consistent with the diagram's dry tap; no test covers kOutputGainId or kGlobalMixId (grep of tests finds nothing).
- Impact: a host ramping output gain at 512-sample blocks gives a staircase of discontinuities over a 48 dB range and a mod source on gain or mix steps once per block, audible as zipper on sustained tones, worst for mix and output gain which act directly on the output; at mix 0 the output gain does nothing; no NaN, crash or state loss; medium.

### D-023 — Band-count change mid-stream has no crossfade, re-indexes crossovers and wakes stale filter/band state

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** signal-path | **Dimension:** signal-path | **Also flagged by:** D-140 (measured) | **Corroborated by measurement:** yes (probe `automation:bandcount2-4-1`, see D-140)

**Location:** [plugins/disrumpo/src/dsp/crossover_network.h:111-122](plugins/disrumpo/src/dsp/crossover_network.h#L111-L122), [:301-365](plugins/disrumpo/src/dsp/crossover_network.h#L301-L365)

**Problem.** setBandCount runs on the audio thread from processParameterChanges with no reset or fade. Decreasing (4->3) changes the topology: band 2 jumps from LP(f2) to the full HP remainder, band 1 loses its allpass, and crossover 2 stops being clocked. Increasing re-sorts old and new frequencies (redistributeIncreasing :358-364), so an inserted crossover below an existing one shifts existing frequencies to a different crossovers_[i] instance. That instance then glides (5 ms smoother) from a different frequency, with filter state built at another split. Re-activated crossovers and BandProcessors (b >= numBands are never processed, processor.cpp:481) resume with state frozen from whenever they were last used. The bands' distortion settings also move to a different frequency range when indices shift.

**Why wrong.** A topology switch on a running audio path needs either a short crossfade between the old and new band sums or a state reset plus fade-in. Otherwise the summed output has a step: the reconstruction changes from one allpass chain to another, and stale biquad z1/z2 values inject a transient.

**Consequence.** Clicks or thumps when the user (or automation or a preset) changes band count during playback. After an increase the per-band distortion is applied to a different frequency region than the user had dialled in.

**Evidence.** crossover_network.h:117-121 setBandCount -> redistributeCrossovers (no reset); :358-364 `std::sort(allFreqs...)` then `crossovers_[i].setCrossoverFrequency(...)`; processor_params.cpp:339-346 called inside the param loop; processor.cpp:481 `for (int b = 0; b < numBands; ++b)`.

**Suggested fix.** On a band-count change, run a ~10 ms equal-gain crossfade between the old-topology sum and the new one. Simpler: ramp the wet output to 0, switch, reset the crossover and newly activated band states, and ramp back. Keep crossover indices stable by inserting new crossovers without re-sorting existing ones into different instances, or copy filter state along with the frequency. Test: 4 bands, 1 kHz sine, change band count 4->3->4 mid-block. Assert max sample-to-sample delta stays below 3x the steady-state value.

**Verifier notes:**
- Correctness: processor_params.cpp:339-346 calls setBandCount inside the param loop and crossover_network.h:111-122/:274-297 never call reset() or fade; process() (:211-227) switches loop bounds on the next sample, so 4->3 drops crossovers_[2] and the AP(f2) stages; redistributeIncreasing (:301-365) sorts and writes crossovers_[i] by index, moving an existing split to another instance with state built at another frequency (crossover_network_test.cpp:191-192/:384 even say "may have moved to a different index after sorting"); processor.cpp:481/:293/:433/:463/:507 touch only b < numBands so returning bands resume from frozen state.
- Context: Band Count is kCanAutomate | kIsList (parameter_registration.cpp:65-77); no fade exists anywhere in the processor (grep for fade/prevBandCount/bandCountChanged is empty) and BandProcessor's only crossfade is for oversampling-factor changes; IT-003 checks only settled DC sums; no spec accepts clicks on a band-count change.
- Impact: with the 4-band log defaults (about 112/632/3557 Hz) removing AP(3557 Hz) at Q 0.707 gives about a 47 deg phase jump at 1 kHz, an instantaneous step of roughly 0.8x the signal amplitude; the band-settings move after an increase follows FR-011a ("preserve existing crossover frequencies") and is a design choice, not a defect; band count is rarely automated but preset loads take the same path; medium.

### D-014 — Drive gate at 1e-4 is a hard discontinuity: drive 0 is a unity passthrough, drive 0.0001 is about -80 dB

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** dsp-numerics | **Dimension:** dsp-numerics | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.cpp:191-195](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L191-L195), [:209-235](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L209-L235)

**Problem.** process() returns the raw input when drive < 0.0001 (bypassing processRaw, tone and mix). Just above the gate, wet = input*drive (≈1e-4 x input for linear-region shapers such as SoftClip, HardClip with ceiling==threshold, Tube), and with mix=1 the output is wet only. The gain as a function of drive therefore jumps from 1.0 at drive<1e-4 to ~1e-4 at drive=1e-4, and climbs back to ~1 at drive 1.

**Why wrong.** A continuous parameter needs a continuous transfer near its neutral point. Drive is a modulation destination (ModDest kBandDrive, offset*10 clamped at 0 in morph_engine.cpp:714 and 728), so it is routinely driven across 0 by LFOs. A bypass at exactly 0 has to crossfade, or 'drive 0' has to mean what the curve gives at 0+.

**Consequence.** An LFO or automation on node drive whose excursion touches 0 produces a full-scale jump between the dry signal and near-silence each time it crosses 1e-4 (a click per LFO cycle). In same-family morphs interpolating a drive-0 node with a driven node, the band level collapses abruptly as the interpolated drive leaves 0.

**Evidence.** distortion_adapter.cpp:191-195 `if (commonParams_.drive < 0.0001f) { ... return input; }`. :212 `wet = input * commonParams_.drive;`. :234-235 `mix = clamp(...); return dry * (1.0f - mix) + wet * mix;`. SoftClip :271-272 returns input unchanged below its threshold (linear). morph_engine.cpp:714 `cp.drive = std::clamp(cp.drive + driveModOffset_ * 10.0f, 0.0f, 10.0f);`.

**Suggested fix.** Make drive=0 a smoothed bypass crossfade (e.g. blend toward dry over a few ms as drive approaches 0), or remove the gate and let mix and drive behave continuously. Test: an adapter of type SoftClip, mix 1, 1 kHz sine at -20 dBFS, drive stepped 0 -> 1e-4 -> 1e-3; assert the output-level change per step is bounded and no sample-to-sample jump exceeds 0.1.

**Verifier notes:**
- Correctness: the gate (:191-195) skips drive, processRaw, DC blocker, tone and mix; :211 sets wet = input*drive for every type except Temporal and Stochastic (:207-209); SoftClip (:271-272) is linear below its threshold (>= 0.1) so wet ≈ 1e-4*input just above the gate; processor_params.cpp:858 maps node drive linearly (value*10) so normalized 0 gives drive 0 and processor.cpp:113 sets mix default 1.0; the same gate exists at band_processor.h:492 and :630, and every modulation path clamps drive at 0 (morph_engine.cpp:713/:730, band_processor.h:445).
- Context: the bypass at drive 0 is deliberate (band_processor.h:114-116 "drive=0 is passthrough per spec", distortion_adapter.h:238-240, dsp-details.md:2019, test UT-DI-009 at distortion_adapter_test.cpp:1469-1493), but none of these documents say anything about continuity at 0+, and no test checks the step from 0 to 1e-4.
- Impact: turning a node's Drive knob down fades the band toward silence then snaps it back to full dry level at the last step (gain ≈ drive for sub-knee signals at mix 1); an LFO with negative depth on a low base drive holds drive at 0 for part of every cycle and switches the band between near-silence and dry at block boundaries (6-26 dB steps, up to about 0.1 in one sample on a -20 dBFS sine); Temporal and Stochastic only switch tone; medium.

### D-013 — A single NaN/Inf input sample permanently latches EnvelopeFollower state: mod-engine env routes then output NaN forever, and the sweep centre collapses to 0 Hz

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** dsp-numerics | **Dimension:** dsp-numerics | **Corroborated by measurement:** no (no NaN was injected at processor level; see Critic gaps) | **CROSS-PLUGIN:** EnvelopeFollower and ModulationEngine are shared

**Location:** [dsp/include/krate/dsp/processors/envelope_follower.h:164-188](dsp/include/krate/dsp/processors/envelope_follower.h#L164-L188), [:296-326](dsp/include/krate/dsp/processors/envelope_follower.h#L296-L326) (consumers: modulation_engine.h:736, 668, 306-310; processor.cpp:340-365, 502)

**Problem.** EnvelopeFollower::processSample has no finiteness guard when the sidechain is off. NaN fails both comparisons and is written into envelope_/squaredEnvelope_. +Inf in Amplitude mode computes inf + c*(env - inf) = inf - inf = NaN. flushDenormal(NaN) keeps NaN, so the state is NaN from then on. The ModulationEngine env source passes it through std::clamp (which returns NaN) into modOffsets_, and getModulatedValue returns clamp(base+NaN) = NaN. In Disrumpo, sweepEnvelope_.processSample(inputL[0]+inputR[0]) feeds getModulatedFrequency. std::clamp(NaN,20,20000) returns NaN, SweepProcessor smoother setTarget(NaN) snaps to 0, and log2(0) = -inf goes into the Gaussian distance. The processor has no output NaN guard.

**Why wrong.** NaN/Inf must never reach the output, and a transient bad sample must not permanently corrupt state. Shared processors classify finiteness through detail::isFinite/isNaN (Biquad and OnePoleLP already reset on non-finite input). log2(0) under the macOS -ffast-math leg is undefined.

**Consequence.** With an Envelope Follower routed to any global destination (e.g. input/output gain), one NaN or Inf sample from an upstream plugin makes inputGainLinear/outputGainLinear NaN, and Disrumpo outputs NaN on every sample until setActive resets the engine. With the sweep envelope enabled, every band's sweep intensity goes to 0 (silent wet) for the rest of the session. CROSS-PLUGIN: EnvelopeFollower and ModulationEngine are shared.

**Evidence.** envelope_follower.h:296-306 processAmplitude `if (rectified > envelope_) ... else envelope_ = rectified + releaseCoeff_*(envelope_-rectified)` with no guard. :312-325 processRMS likewise. :184 flushDenormal(envelope_). db_utils.h:264-265 flushDenormal is a range compare (NaN passes). modulation_engine.h:736 `envFollower_.processSample(envInput)`, :759 `std::clamp(sourceValue,-1,1)`, :309 `std::clamp(baseNormalized + offset, 0, 1)`. processor.cpp:341-342 sweep env fed inputL[0]; :365 `std::clamp(modulatedFreq, 20, 20000)`; sweep_processor.h:124 clamp then setTarget; smoother.h:169-172 NaN snaps current_ to 0; sweep_morph_link.h:164 `std::log2(sweepCenterHz)`.

**Suggested fix.** In EnvelopeFollower::processSample, replace non-finite input with 0 (or reset) via detail::isFinite. Sanitize source values in ModulationEngine::evaluateRoutings. Optionally clamp the final output with a finite check in Disrumpo::process. Test (dsp_ieee_tests, NaN bits via volatile): feed one NaN then 1000 zeros, assert getCurrentValue() is finite. Add a processor-level case: env routed to output gain, one NaN sample in the input block, assert all later blocks are finite.

**Verifier notes:**
- Correctness: envelope_follower.h:163 says "Does NOT validate input" and the sidechain is off by default (:398); a NaN takes the release branch and stays NaN, flushDenormal (db_utils.h:264-265) passes it, RMS mode does the same; modulation_engine.h:225 runs the follower every sample and :668/:759/:309 pass NaN through std::clamp into modOffsets_ (:788-803); the example destination is wrong, though: dbToGain (db_utils.h:290-295) returns 0 for NaN, so input/output gain goes silent (wet path) rather than NaN; Global Mix (processor.cpp:264, :456-457, :502) does produce NaN output forever.
- Context: processor.cpp checks only null buffers and feeds raw input to modulationEngine_.process (:246) and sweepEnvelope_.processSample((inputL[0]+inputR[0])*0.5f) (:341-342); there is no sanitizer and no output guard at :502-503; SweepEnvelope latches the same way (sweep_envelope.h:87-94, :192) and the clamp at processor.cpp:365 and setCenterFrequency (sweep_processor.h:124) pass NaN to Smoother::setTarget, which sets current_ = 0 (smoother.h:169-172); no Disrumpo test feeds NaN or Inf into the processor.
- Impact: the sweep centre goes to 0 Hz, calculateAllBandIntensities (sweep_processor.h:272-277) hits log2(x/0) (0 intensity under IEEE, undefined under fast-math) and BandProcessor multiplies the band input by 0 (band_processor.h:196-198, 478-482), so the wet path is silent for the session; the sweep follower is fed only inputL[0], so it needs the NaN on sample 0; the trigger is a non-finite host input plus an env routing or the sweep envelope; medium.

### D-012 — Cursor-on-node early return skips the transitionGains_ update, so a stale gain of 0 can silence a band in cross-family mode

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** dsp-numerics | **Dimension:** dsp-numerics | **Also flagged by:** D-120 (test gap), D-155 (measured) | **Corroborated by measurement:** yes (D-155 measured the mode-switch and 3-4-node Linear1D paths; the cursor-jump path was not reproduced)

**Location:** [plugins/disrumpo/src/dsp/morph_engine.cpp:188-192](plugins/disrumpo/src/dsp/morph_engine.cpp#L188-L192), [:220-224](plugins/disrumpo/src/dsp/morph_engine.cpp#L220-L224) (gains updated only at 268-270; consumed 748-749)

**Problem.** When the cursor is within 0.001 of an active node, calculateMorphWeights sets one-hot weights and returns before the loop that recomputes transitionGains_. processCrossFamily then multiplies by the stale transitionGains_[i] and skips the node entirely if it is < 0.001. The gains only cover indices < activeNodeCount_, so a node's gain keeps whatever it had when it was last active.

**Why wrong.** The one-hot weight path must produce gain 1 for the on-node node. Its weight is 1.0, and calculateTransitionGain(1.0) is 1.0. The early return makes the gain depend on history instead of on the current weights.

**Consequence.** Reproduction: 3 active nodes, cursor parked on B (C's weight is thresholded, so its gain is 0). Drop to 2 nodes, move the cursor to C's corner (0,1) (not an active node, so gains[2] is not touched). Raise to 3 nodes, where C has a different family. setNodes recomputes the weights, the early return gives C weight 1 and stale gain 0, and the band outputs silence until the cursor moves more than 0.001 away. Preset or automation sequences that change the active-node count can hit the same state.

**Evidence.** morph_engine.cpp:188-192 `if (distances[i] < kOnNodeThreshold) { weights_.fill(0.0f); weights_[i] = 1.0f; return; }` (and :220-224 for Planar2D). :268-270 is the only place transitionGains_[i] is recomputed. :748-749 `const float gain = transitionGains_[i]; if (gain < kWeightThreshold) continue;`. setNodes (:131-152) and every process() call (:668-672) go through the same early return.

**Suggested fix.** In the on-node branch set transitionGains_ = {0..}, transitionGains_[i] = 1 (or fall through to the common gain loop instead of returning). Test: the reproduction sequence above with SoftClip, SoftClip, Bitcrush nodes; assert non-zero band output RMS.

**Verifier notes:**
- Correctness: the Linear1D and Planar2D on-node branches return from calculateMorphWeights before :267-270 (Radial2D's centre return at :298-304 is different and still gets gains); transitionGains_ is set only at :268-270, reset() :87 and the header initialiser (morph_engine.h:239); process() (:662-683) repeats the early return every sample so the stale value persists; the reproduction is reachable because node positions are fixed corners (processor.cpp:114-116), kBandActiveNodes (processor_params.cpp:949-956) calls setNodes, and the smoother snaps exactly to its target (smoother.h:197-200); smooth cursor travel alone does not trigger it, and setMode (:107-113) is a second trigger.
- Context: nothing guards it (reset() runs only on reset/setActive, setNodes :151 and process :668-672 go through the same early return); processSameFamily never reads transitionGains_, so only cross-family bands are affected and the processor's default all-SoftClip nodes are not; no test pins on-node transition gains.
- Impact: this verifier reads the smoother as accepting 0 ms and notes the smoothing default is 0 ms (parameter_registration.cpp:718-719, processor_state.cpp:323), so a discrete jump onto a corner node (stepped automation, a clamped mod route, a typed value or a preset change where setMorphNodes at processor_state.cpp:894-897 runs before setMorphPosition) inherits the previous gain: 0.707 (-3 dB) from 0.5, 0.047 (-26.5 dB) from 0.9, or 0 (silence) from x >= 0.969; D-155 later found the smoother's 0.1 ms floor refreshes the gain on a cursor jump, leaving the mode-switch and node-count paths; medium.

### D-011 — Integer morph params (stages, iterations) are floored per node, so two nodes with equal values morph to a smaller value

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** dsp-numerics | **Dimension:** dsp-numerics | **Also flagged by:** D-030 (morph-oversampling) reports the same arithmetic | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/morph_engine.cpp:576](plugins/disrumpo/src/dsp/morph_engine.cpp#L576), [:614](plugins/disrumpo/src/dsp/morph_engine.cpp#L614) (zeroed at 466, 505)

**Problem.** result.stages and result.iterations start at 0 and accumulate `static_cast<int>(w * value)`, flooring each node's share. Two FeedbackDist or RingSaturation nodes with stages=1 at any non-one-hot weights give 0+0 = 0. Two Fractal nodes with iterations=4 at w=0.6/0.4, 0.7/0.3 or 0.9/0.1 give 3, but 4 at 0.5/0.5 or 0.75/0.25.

**Why wrong.** Interpolating an integer must round, or take the value from the dominant node. A morph between identical settings must be a no-op, and here it changes the processing topology.

**Consequence.** Moving the cursor between two Fractal nodes that both say 4 iterations toggles the iteration count 4 -> 3 -> 4 -> 3 as the weights pass exact multiples, a discrete timbre and level jump (click) in the middle of a smooth morph. stages=0 is silently clamped back to 1 inside the processors, which hides the bug for 1-stage presets but not for 2-4 stages (for example 4/4 at 0.6/0.4 gives 3).

**Evidence.** morph_engine.cpp:466 `result.stages = 0;`, :505 `result.iterations = 0;`, :576 `result.stages += static_cast<int>(w * static_cast<float>(p.stages));`, :614 `result.iterations += static_cast<int>(w * static_cast<float>(p.iterations));`. distortion_adapter.cpp:927 fractal_.setIterations(p.iterations). Computed: iterations 4/4 gives 3 at w=0.6, 0.7 and 0.9, and 4 at 0.5 and 0.75. Stages 1/1 gives 0 at every split weight.

**Suggested fix.** Accumulate the integer fields as float and std::lround at the end (and clamp to range), or copy them from the dominant node like the other discrete fields. Test: same-family interpolateParams with two nodes of equal stages/iterations at w in {0.1..0.9}; assert the result equals the node value.

**Verifier notes:**
- Correctness: interpolateParams (:403-419) copies the dominant node's params, zeroes stages (:467) and iterations (:505), and sums truncated terms (:576, :614; fftSize/magnitudeBits at :606-607 too); the path runs per sample (processSameFamily :706-716 -> blendedAdapter_.setParams), the processors only clamp (feedback_distortion.h:486, fractal_distortion.h:267, ring_saturation.h:187, adapter :777), and the node values are 1-4 stages and 1-8 iterations (processor_params.cpp:169, :185, :257).
- Context: morph_interpolation_test.cpp:406-507 ("identical nodes produce clean output") checks only position 0.5 where 0.5*4 = 2 is exact; no spec or comment documents flooring as intended; the same flooring on fftSize (:589) is what drives the Spectral re-prepare (D-001/D-007).
- Impact: the "toggles 4->3->4->3" wording is overstated; weights from continuous IDW math (:228-240, :262-265) are almost never exact multiples of 0.25, so the result sits at N-1 for most cursor positions and steps only when the cursor lands on or leaves a node (weight exactly 1); Fractal 4/4 renders with 3 iterations and FeedbackDist 2/2 drops to 1 stage (reconfiguring its filters), a persistent topology and level change plus a step at the node boundary; medium.

### D-005 — Default same-family path re-routes every adapter parameter twice per sample per band

**Severity:** medium | **Verdict:** CONFIRMED | **Category:** rt-safety | **Dimension:** rt-safety | **Corroborated by measurement:** no (no CPU measurement was taken; see Critic gaps)

**Location:** [plugins/disrumpo/src/dsp/morph_engine.cpp:662-718](plugins/disrumpo/src/dsp/morph_engine.cpp#L662-L718), [:726-738](plugins/disrumpo/src/dsp/morph_engine.cpp#L726-L738)

**Problem.** MorphEngine::process runs once for L and once for R per sample (band_processor.h:487-488). Each call recomputes the IDW weights and isSameFamily. Any band whose nodes share a family takes processSameFamily, and that includes the default 1-node band. That path then runs setType + interpolateParams (about 120 fields, a 500 B struct by value) + setParams -> routeParamsToProcessor + setCommonParams -> toneFilter_.setCutoff, all per call. routeParamsToProcessor holds per-type coefficient work: Formant does two std::pow plus setVowel/setVowelBlend/filter updates (distortion_adapter.cpp:863-873), and AllpassResonant, FeedbackDist, Granular and Fractal have their own setter chains. In cross-family mode, any non-zero mod offset makes the same per-sample setParams loop run over all active adapters (:726-738). The work is bounded but scales as bands x 2 channels x samples x full parameter routing, all at host rate.

**Why wrong.** Control-rate configuration (type selection, parameter routing, filter coefficient design) belongs at block rate or on change, not inside the per-sample inner loop. Per-sample std::pow and coefficient recomputation is the kind of avoidable audio-thread cost that the dsp-architecture performance budget (NFR CPU <15 % at 4 bands) does not allow for. It also makes setter side effects (crossfade restarts, state resets) run every sample, which is how the Spectral re-prepare above turned into a per-sample allocation.

**Consequence.** CPU use roughly doubles to several times what is needed with 4 bands of Formant/Allpass/Granular nodes, which raises xrun risk at small buffers and high sample rates. Any future setter that is not idempotent becomes a per-sample bug.

**Evidence.** morph_engine.cpp:668-675 smoother.process + calculateMorphWeights + isSameFamily per call. :706 `blendedAdapter_.setType(...)` :708-710 `auto bp = interpolateParams(); ... blendedAdapter_.setParams(bp);` :716 `blendedAdapter_.setCommonParams(cp);`. distortion_adapter.cpp:173-175 setParams -> routeParamsToProcessor, :168-170 setCommonParams -> toneFilter_.setCutoff, :868-870 `std::pow(4.0f, ...)` x2. band_processor.h:487-488 `left = morphEngine_->process(left); right = morphEngine_->process(right);`

**Suggested fix.** Compute weights, family and the interpolated params once per block, or when the smoothed cursor has moved more than a small epsilon, and only call setParams/setCommonParams when the result changes. Give L and R their own adapter or engine instances instead of advancing one engine twice. Measure with the existing performance tests (run alone via tools/run-cpu-tests.js) before and after.

**Verifier notes:**
- Correctness: processor.cpp:125 enables morph with kDefaultActiveNodes = 1 (morph_node.h:135), and isSameFamily (:385-388) returns true for activeNodeCount_ <= 1, so the default band always takes processSameFamily; that path runs the dominant-node search, setType (distortion_adapter.cpp:142-161 incl. updateDCBlockerState), interpolateParams by value, setParams (always routeParamsToProcessor, :173-183) and setCommonParams (always toneFilter_.setCutoff, :168-171) on every call with no change check; Formant does two std::pow (:868, :870), the folders and Tape call std::pow in setCutoff (:664-725, :650); the Spectral re-prepare (:909-911) fires only when the size changes.
- Context: the comment at :671 says "Recalculate weights if position changed" but no such check exists; specs/Disrumpo (dsp-details.md:2157) asks only for per-sample automation resolution, not per-sample re-routing; the CPU tests (distortion_adapter_test.cpp:1740/2128/2186, morph_weight_computation_test.cpp:205) time adapters or weights directly, never MorphEngine with Formant/Allpass nodes; this verifier recommended low because the cost is unmeasured and no hard RT rule is broken.
- Impact: on the oversampled paths processOversampledBlock (band_processor.h:753-754) would call process per oversampled sample (2x/4x/8x the host rate); the CPU-budget tests default morphEnabled_ to false (band_processor.h:809), so none of them exercise this path; the setters read as idempotent, so the cost is efficiency only; medium retained (two of three).

### Medium, PLAUSIBLE (one refute vote)

### D-021 — Center pan applies -3 dB to every band: neutral settings are not unity gain

**Severity:** medium | **Verdict:** PLAUSIBLE (one of three verifiers refuted on context: the pan law is the retired spec's FR-022; see Critic gaps for the counter-argument that specs-overview.md has no FR-022) | **Category:** signal-path | **Dimension:** signal-path | **Also flagged by:** gestalt-processor | **Corroborated by measurement:** yes (probe `unity:processor:bands=1..4`)

**Location:** [plugins/disrumpo/src/dsp/band_processor.h:499-511](plugins/disrumpo/src/dsp/band_processor.h#L499-L511)

**Problem.** The band output stage uses a mono-source equal-power pan law on a stereo band signal: leftCoeff = cos(pan*pi/4 + pi/4), rightCoeff = sin(...). At pan 0 both coefficients are 0.707, so every band, and therefore the whole wet sum, is 3 dB below the input at 0 dB band gain. Tests pin this (band_processing_test.cpp:35-56, 130-142).

**Why wrong.** For a stereo signal, a band 'pan' is a balance control. It should be unity on both channels at centre, attenuating only the opposite side when panned (e.g. L = min(1, 1 - pan), R = min(1, 1 + pan), or an equal-power balance normalised so centre = 1). A band at 0 dB gain and centre pan is expected to pass at 0 dB (FR-BAND-004 gain range is referenced to unity).

**Consequence.** Engaging the plugin with light distortion at 100 % wet drops the level by 3 dB. The global mix control is not level-neutral (dry 0 dB against wet -3 dB), which also biases the dry/wet comb finding (D-018). Mono material loses 3 dB in the wet path, and panning a band fully to one side raises that side by +3 dB relative to centre.

**Evidence.** band_processor.h:502-504 `panAngle = pan * kPi / 4.0f + kPi / 4.0f; leftCoeff = std::cos(panAngle); rightCoeff = std::sin(panAngle);`; :510-511 multiply; tests/unit/band_processing_test.cpp:56 'At 0dB gain (unity) with center pan, output = input * 1.0 * 0.707'.

**Corroborated by measurement.** probe unity:processor:bands=1..4: "abs_level_dB -3.010 for every band count" (threshold "0 dB +/- 0.1"). Code: band_processor.h:502 `const float panAngle = pan * kPi / 4.0f + kPi / 4.0f;` then `left = left * gain * leftCoeff * muteMultiplier;`

**Suggested fix.** Switch to a stereo balance law that is unity at centre and apply the same law in applyOutputStage (:770-779). Update band_processing_test.cpp expectations, and add a processor-level test: all nodes at drive 0 passthrough, pan 0, gain 0 dB, mix 1, 1 band. Assert output equals input within 0.01 dB.

**Verifier notes:**
- Correctness (confirmed): :502-504 gives 0.7071 at pan 0 (default targetPan_ = 0 at :89), applied to both channels at :510-511 and in applyOutputStage (:766-772); processor.cpp:494/:497-498/:502-503 add no sqrt2 makeup anywhere in plugins/disrumpo/src; the retired spec 002 (git show 07985ac6, spec.md:283) mandates this law as FR-022 but the same spec says output should equal input at unity (:164, :188, :303), so the root is a spec conflict.
- Context (REFUTE): the -3 dB at centre is a recorded design decision: 002-band-management spec.md:283 FR-022 defines exactly this formula and marks it MET (:471), research.md:118-131 states "leftGain = cos(PI/4) = 0.707" at centre for constant power, plan.md:154 keeps it inline, and band_processing_test.cpp:35/48-51/56/68-70/129-144 pin the 0.707 centre coefficient and L = 1.0 at full left; changing to a balance law would change the sound of 120 factory presets and saved projects; FR-BAND-004 says nothing about the pan law.
- Impact (confirmed): with default settings every host and sample rate gets a constant -3.01 dB at 100 % wet, the global mix is not level-neutral (processor.cpp:502), and full pan gives +3.01 dB on one side while discarding the other channel's band content; the Seraphis phase10 spec (specs/seraphis-phase10-effects/spec.md:686, plan.md:261) calls exactly this uncompensated law a defect; STATE-COMPAT/preset-level concern for the fix; medium.

### D-038 — Sweep 'intensity' scales band input level, so out-of-focus bands are muted instead of passing clean

**Severity:** medium | **Verdict:** PLAUSIBLE (one of three verifiers refuted on impact: the retired 007 spec says outer bands "are attenuated") | **Category:** spec-conformance | **Dimension:** sweep-modulation | **Also flagged by:** D-019 (signal-path, CONFIRMED) and D-037 cover the same code | **Corroborated by measurement:** yes (probes `sweep:off-centre-band-level`, `sweep:full-automation`, near-silent Sweep presets)

**Location:** [plugins/disrumpo/src/dsp/band_processor.h:478-482](plugins/disrumpo/src/dsp/band_processor.h#L478-L482)

**Problem.** The per-band sweep value multiplies the band's signal before distortion (`left *= sweep; right *= sweep;`). The wet output is only the sum of the bands (processor.cpp:502-503), so a band with intensity near 0 is removed from the output entirely, not left undistorted.

**Why wrong.** FR-SWEEP-001 defines the sweep as 'Focused distortion intensity moving through frequency spectrum': the distortion amount should be focused, with the rest of the spectrum still passing. plans-overview.md does place a 'Sweep Intensity Multiply' at band input. But since there is no compensating make-up or dry path per band, the effect is a sweeping band-pass filter on the whole signal, not a sweeping distortion focus.

**Consequence.** Turning Sweep on in the default 4-band patch (centre 1 kHz, width 1.5 oct, intensity 0.5) gives band gains of 100 Hz: -91 dB, 600 Hz: -10 dB, 3 kHz: -25 dB and 12 kHz: -105 dB. Bass and top vanish from the wet signal at mix 100 %, and the result reads as a narrow band-pass at -10 dB, not 'distortion moving through the spectrum'.

**Evidence.** band_processor.h:478-482 `const float sweep = sweepSmoother_.process(); // Step 1: Apply sweep intensity BEFORE distortion/morph (per plan.md) left *= sweep; right *= sweep;`. specs/Disrumpo/specs-overview.md:275-277 'FR-SWEEP-001 ... Focused distortion intensity moving through frequency spectrum'. Gains computed with sweep_morph_link.h calculateGaussianIntensity (sigma=width/2) at the kBandCenterFreqs.

**Corroborated by measurement.** probe sweep:off-centre-band-level (11 kHz sine, centre 100 Hz): "level_off_dB -40.60, level_on_dB -165.36, delta_dB -124.76" (threshold "delta > -6 dB"). sweep:full-automation: "rms_on_dB -20.98, rms_off_dB -11.99". preset near-silent: "Sweep/Sharp_Focus -53.02; Synced_Pulse -46.67; High_Sweep -48.41; Low_Rumble -43.49". Code: band_processor.h:481 `left *= sweep;`; processor.cpp:422 `kBandCenterFreqs = { 100.0f, 600.0f, 3000.0f, 12000.0f }`

**Suggested fix.** Decide the semantics with the spec owner. Either apply the intensity to drive or wet-mix per band (out-of-focus bands pass dry), or apply make-up gain after the morph stage. Test: sweep enabled, broadband pink noise in, mix 100 %. Assert that the output level in a band far from the sweep centre stays within 3 dB of its sweep-disabled level while its THD drops.

**Verifier notes:**
- Correctness (confirmed): processor.cpp:426-441 computes Gaussian intensities from the fixed centres (:422-424), setSweepIntensity clamps to [0,1] (band_processor.h:196-198), and the multiply at :478-482 has no per-band dry path; a normalized intensity of 0.5 maps to 1.0 (processor.cpp:289, processor_state.cpp:488), so the band gains are about 6 dB higher than the finding states (100 Hz about -85 dB, 600 Hz -4 dB, 3 kHz -19 dB, 12 kHz -99 dB), with the same conclusion.
- Context (confirmed): the retired 007 spec says the multiplier exists "so each band receives the appropriate amount of distortion" (spec.md:126, :378) and tasks.md:307 says "multiply distortion gain or intensity per band"; only the plans-overview.md:78 box supports input scaling; the tests (sweep_band_intensity_test.cpp:190-206, morph_band_integration_test.cpp:164-168) pin the attenuation without a design ruling; the [0,1] clamp also makes FR-010's 200 % unreachable.
- Impact (REFUTE): the retired 007 spec's acceptance text (spec.md:32) says "bands within the sweep region receive increased distortion intensity while outer bands are attenuated", FR-010 calls for multiplicative scaling with centre = intensity, plans-overview.md:75-81 puts the multiply at band input, and the tests pin it; sweep is off by default and no crash, NaN or click follows, so wanting out-of-focus bands to pass dry is a product-design change for the spec owner, not a conformance defect; the hard-coded band centres are a separate issue (D-037).

### D-145 — SergeFold (and the Sine/Triangle folds) are nearly silent in the plugin (-36 to -47 dBFS peak from -6 dBFS input)

**Severity:** medium (reported: high; lowered by two verifiers) | **Verdict:** PLAUSIBLE (one of three verifiers refuted the mechanism: the adapter at 48 kHz is itself nearly silent, so the Disrumpo wiring is not the cause) | **Category:** measured-probe | **Dimension:** measured | **Also flagged by:** measured; root-caused by D-158 and D-159 | **Corroborated by measurement:** yes (probe `type:SergeFold`, `type:SineFold/TriangleFold`)

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.cpp:410-420](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L410-L420)

**Problem.** SergeFold feeds the shared WavefolderProcessor one sample at a time inside the adapter, which is prepared at 8x and processes L and R interleaved (band_processor.h:111,124,487-488). The same adapter at 48 kHz mono gives normal output. The loss therefore comes from the Disrumpo wiring: the rate mismatch and shared state on wavefolder_'s smoothers and DC blocker, plus the tone LP at about 1 kHz removing the fold harmonics. The exact split between those causes was not isolated.

**Why wrong.** A distortion type at full drive should produce output near the input level, as the adapter does when prepared and clocked correctly.

**Consequence.** Three wavefolder types are nearly unusable, and their energy is dominated by non-harmonic content.

**Evidence.** probe type:SergeFold (drive 0.25/0.75/1.0): "-14.2,-3.7e-08,0,-47.18 | 17.3,2.2e-08,0,-47.52 | 24.6,-5.2e-09,0,-35.93" (nonharm_dB,dc,nonfinite,peak_dBFS). "The adapter at 48k gives a 6424 Hz centroid; the Processor gives 497 Hz." type:SineFold/TriangleFold drive=1.00 peak "-31.04" dBFS. Code: distortion_adapter.cpp:413 `float biased = input + typeParams_.bias;`, :415 `wavefolder_.process(singleSampleBuffer_, 1);`

**Suggested fix.** Fix the 8x prepare and the L/R sharing, then re-measure. Test: type:SergeFold peak within 6 dB of the adapter@48k and centroid ratio 0.85-1.15.

**Verifier notes:**
- Correctness (REFUTE on mechanism): the probe's own row 18 (adapter-vs-processor:SergeFold rms_diff -11.92 dB) against a processor peak of -47.52 dBFS puts the adapter alone at or below about -35.6 dBFS RMS, so "normal output at 48k" is contradicted; non-fold types show about -6 dB everywhere (SoftClip -6.14, Tube -5.99, Spectral -6.03, rows 14/20/26) so only about 5-6 dB is fold-specific; the probe feeds identical L and R so interleaving a memoryless fold plus LTI one-poles cannot cost 30-40 dB; the suggested test bar compares against an adapter that is itself nearly silent.
- Context (confirmed, with downgrade): the real cause is the default fold parameters: ShapeShadowStorage sets every slot to 0.5 (morph_node.h:102-106), processor_params.cpp:106-113 maps that to folds 6.5, smoothness 0.5 and foldModel 2 = Buchla259 (distortion_adapter.cpp:678-685), the smoothing LP sits at about 3.2 kHz (:664/:673/:689) and the applyBuchla259 output is capped at 1.4/(3*fold) (wavefolder_processor.h:411-448); no spec or test says these levels are intended.
- Impact (confirmed, with downgrade): processor rows 521-526/542-544 are real (SergeFold -47.18/-47.52/-35.93, Sine/Triangle -23.2/-26.6/-31.0 dBFS peak against -6 dBFS input), so a fresh node of these types is 17-41 dB quieter than its input; fixing D-009/D-016 recovers only about 6-12 dB for SergeFold (most of its loss is the Buchla model cap, D-158) and most of the gap for Sine/Triangle; lowering Folds or choosing another model restores level, so "unusable" overstates; medium.

### D-168 — Serge Fold 'Model' menu labels are swapped for entries 0/1, and the default slot value runs Buchla259 instead of Serge

**Severity:** medium | **Verdict:** PLAUSIBLE (one of three verifiers refuted on impact: the labels are never displayed while stepCount is 8, and the spec's D09 backend is Buchla259/Lockhart, not Serge) | **Category:** ui-wiring | **Dimension:** followup:ui-wiring-leftovers | **Also flagged by:** D-158 (level consequence of the Buchla default), D-135 (menu wipe) | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/resources/editor.uidesc:974](plugins/disrumpo/resources/editor.uidesc#L974)

**Problem.** The SergeFold Model COptionMenu lists 'Simple,Serge,Buchla,Lockhart'. The processor decodes slot 2 as foldModel = int(slot*3+0.5), so the value maps to the menu index. The adapter then indexes models[] = {Serge, Simple, Buchla259, Lockhart}, which is not in WavefolderModel enum order (Simple=0, Serge=1, Buchla259=2, Lockhart=3). Menu entry 'Simple' selects the Serge model and entry 'Serge' selects Simple. The default shape slot is also 0.5: ShapeShadowStorage sets every slot to 0.5 and the node registration default is 0.5. 0.5 decodes to int(2.0)=2, which is Buchla259, so a newly chosen 'Serge Fold' node does not run the Serge model at all.

**Why wrong.** A list control's label must name the DSP state it selects. The adapter comment ('0=Serge, 1=Simple') shows a deliberate reorder that the uidesc was never updated to match. The type is called Serge Fold and its default should be the Serge model, but the 0.5 default lands on index 2.

**Consequence.** Today the already-reported slot-2 stepCount-8 defect (D-135) hides this, because VSTGUI replaces the menu items with '0'..'8'. Once that is fixed, picking 'Serge' gives the plain Simple triangle fold and picking 'Simple' gives Serge. Every default Serge Fold node, including factory presets that never set slot 2, runs Buchla259. Host text has no Serge-specific formatter (format_helpers.h has no SergeFold case), so nothing reveals the mismatch.

**Evidence.** editor.uidesc:974 menu-items="Simple,Serge,Buchla,Lockhart" (control-tag Band.NodeShape2); processor_params.cpp:110 p.foldModel = static_cast<int>(slots[2] * 3.0f + 0.5f); distortion_adapter.cpp:677-685 '// 0=Serge, 1=Simple, 2=Buchla259, 3=Lockhart' models[] = {Serge, Simple, Buchla259, Lockhart}; wavefolder_processor.h:56-60 Simple=0, Serge=1, Buchla259=2, Lockhart=3; morph_node.h:102-106 all shadow slots default 0.5f; parameter_registration.cpp:957-961 shape slot default 0.5, stepCount 8 for s==2.

**Suggested fix.** Make one table authoritative. Either reorder the adapter's models[] to the enum order and the uidesc labels to match, or change the uidesc to 'Serge,Simple,Buchla,Lockhart'. Give the SergeFold slot 2 a ShapeShadowStorage default that selects Serge. Fix this together with the slot-2 stepCount (stepCount 3, or a StringList proxy). Test: drive DistortionAdapter SergeFold through mapShapeSlotsToParams with slot2 = k/3 for k=0..3, assert getModel() equals the model named by uidesc menu entry k, and assert the default slot decodes to WavefolderModel::Serge. Add a controller test that reads the uidesc menu-items for TypeParams_SergeFold.

**Verifier notes:**
- Correctness (confirmed): editor.uidesc:974 lists "Simple,Serge,Buchla,Lockhart", processor_params.cpp:110 decodes int(slot*3+0.5), distortion_adapter.cpp:677-685 indexes {Serge, Simple, Buchla259, Lockhart} against the enum order Simple=0, Serge=1 (wavefolder_processor.h:56-60), so entries 0 and 1 are swapped; the 0.5 default (morph_node.h:43-44, :102-106, parameter_registration.cpp:961) decodes to 2 = Buchla259 and :410-416 runs that model; no test checks labels or the default decode.
- Context (confirmed): the ShapeShadowStorage constructor overrides only BitwiseMangler (:108-113); the spec's D09 default is Lockhart (ui-mockups.md:412; dsp-details.md:117; tasks-overview.md:82), so Buchla contradicts the documented intent and nothing records the reorder as deliberate.
- Impact (REFUTE): the menu is bound to a stepCount-8 RangeParameter (parameter_registration.cpp:820-823, :957), so VST3Editor rebuilds it from getParamStringByValue (vst3editor.cpp:325-330) and the uidesc labels are never shown; under either proposed fix (stepCount 3 or a StringList proxy) the menu shows the parameter's own strings, so the label swap stays latent; specs-overview.md:89 maps D09 Serge Fold to Wavefolder::Lockhart / WavefolderProcessor::Buchla259 and D07 Sine Fold to WavefolderProcessor::Serge, so the Buchla default is consistent with the spec backend; a valid model runs in every case; at most a stale-string clean-up inside D-135.


### Low

### D-173 — Bitwise Mangler 'Bits' slider is permanently hidden dead markup bound to a shape slot nothing reads

**Severity:** low | **Verdict:** CONFIRMED | **Category:** duplication | **Dimension:** followup:ui-wiring-leftovers | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/resources/editor.uidesc:1107-1110](plugins/disrumpo/resources/editor.uidesc#L1107-L1110)

**Problem.** TypeParams_Bitwise still declares a 'bits' group with a CSlider bound to Band.NodeShape3. BitwiseOpController always hides that group (showBits = false, 'BitRotate/BitShuffle removed'). mapShapeSlotsToParams reads only slots 0-2 for BitwiseMangler, and DistortionParams::bitwiseBits is documented 'reserved, unused'. The slot stays host-automatable and persisted (node Shape3) with no audible effect for this type.

**Why wrong.** Removed features should take their view, visibility branch and reserved field with them. Keeping a hidden control bound to a live parameter invites a future 'fix' that unhides it without a consumer.

**Consequence.** No user-visible defect today: the group is never shown. It is maintenance clutter and an automatable no-op for Bitwise nodes, and the inventory's '4 Bitwise controls vs 3 consumed' reading is explained by this hidden group.

**Evidence.** editor.uidesc:1107-1110 <view class="CViewContainer" bitwise-group="bits" ...><view class="CSlider" control-tag="Band.NodeShape3" .../>; sub_controllers.h:338-339 '// Bits control is no longer used (BitRotate/BitShuffle removed)' bool showBits = false; processor_params.cpp:197-202 BitwiseMangler reads slots[0..2] only; distortion_adapter.h:121 bitwiseBits '(reserved, unused)'; morph_node.h:113 typeSlots[bmIdx][3] = 0.5f; // (reserved).

**Suggested fix.** Delete the bitwise-group="bits" container from TypeParams_Bitwise (via XSLT, per project rule), remove bitsContainer_ handling from BitwiseOpController and the reserved bitwiseBits field, and drop the 'reserved' shadow default in morph_node.h:113.

**Verifier notes:**
- Correctness: every claim matches (uidesc:1107-1110, sub_controllers.h:305-306/:338-344, processor_params.cpp:197-202, distortion_adapter.h:121 with no reader anywhere, morph_node.h:113); the hidden container is load-bearing as written because sub_controllers.h:313 sets the initial Pattern visibility only when bitsContainer_ is non-null, so the fix must also edit line 313.
- Context: nothing documents the leftover as intentional; the only test hit (multi_node_distortion_test.cpp:733) is a stale comment about BitRotate; the Shape3 parameter itself is live for other types (Chaos "Coup" at uidesc:1121), so only the hidden Bitwise view is dead.
- Impact: 0 dB audible effect in every host; cosmetic/maintenance; low.

### D-172 — DataExchange queue is opened only in setActive(true): a connect() or reconnect that arrives while active leaves the spectrum queue closed

**Severity:** low | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** followup:ui-wiring-leftovers | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor_messaging.cpp:36-72](plugins/disrumpo/src/processor/processor_messaging.cpp#L36-L72)

**Problem.** connect() creates a new DataExchangeHandler and calls onConnect only. onActivate(processSetup), which opens the queue, is called only from setActive(true) (processor.cpp:167-168). disconnect() calls onDisconnect (closeQueue) and destroys the handler. If the host connects after activation, or disconnects and reconnects while active, the new handler is never activated. The SDK's getCurrentOrNewBlock/lockBlock then has no open queue and sendSpectrumBlock returns at the invalid-block check on every block. sendModOffsetsMessage likewise runs only in setActive(true), so a late-connected controller never receives the mod-offsets pointer.

**Why wrong.** The handler needs onActivate whenever it exists while the processor is active. The SDK sample opens it in setActive because it assumes connect precedes activation, but the VST3 spec does not guarantee that order for reconnects.

**Consequence.** In hosts that (re)connect components while processing, the spectrum analyser silently falls back to the Tier-3 SharedDisplayBridge path, or shows nothing if that lookup fails, and the mod-slider overlays stay dead until the next deactivate/activate.

**Evidence.** processor_messaging.cpp:61-63 dataExchange_ = make_unique<DataExchangeHandler>(...); dataExchange_->onConnect(other, getHostContext()); (no onActivate); processor.cpp:166-170 'if (dataExchange_) dataExchange_->onActivate(processSetup); sendModOffsetsMessage();' inside setActive(true) only; vst3sdk dataexchange.cpp:337-351 onActivate opens the queue, :373-378 getCurrentOrNewBlock -> impl->lockBlock(); processor_messaging.cpp:127-130 returns on InvalidDataExchangeBlockID.

**Suggested fix.** In connect(), if the processor is already active (track an isActive_ flag set in setActive), call dataExchange_->onActivate(processSetup) and sendModOffsetsMessage() immediately. Test: initialize, setupProcessing, setActive(true), then connect a mock connection point, run process(), and assert a DataExchange block or fallback message was sent.

**Verifier notes:**
- Correctness: connect() (:61-63) calls only onConnect, which in the SDK stores pointers and opens no queue; the queue opens only in onActivate (dataexchange.cpp:337-351), called only from setActive(true) (processor.cpp:166-168); disconnect() (:78-82) closes and resets; Impl::lockBlock returns an invalid block when the queue is closed (dataexchange.cpp:245-249), so sendSpectrumBlock exits at :127-130; sendModOffsetsMessage is called only at processor.cpp:170.
- Context: no isActive flag or re-activation in connect(); the controller's cachedModOffsets_ (controller.cpp:1966-1971) has no other source; the standard host sequence (initialize, connect, setupProcessing, setActive) never triggers it, so a late connect or reconnect while active is required.
- Impact: the spectrum keeps working through the always-fed Tier-3 FIFOs (processor_messaging.cpp:112-121, bridge lookup at controller.cpp:637-646), so only the mod overlays are lost; UI-only, host-order dependent; low.

### D-171 — Temporal mode: shape-slot default selects Derivative ('Transient') instead of EnvelopeFollow, and the adapter comment and spec list only 3 modes

**Severity:** low | **Verdict:** CONFIRMED | **Category:** spec-conformance | **Dimension:** followup:ui-wiring-leftovers | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor_params.cpp:156](plugins/disrumpo/src/processor/processor_params.cpp#L156)

**Problem.** The UI menu 'Follow,Inverse,Transient,Hyst' maps index k to TemporalMode k (EnvelopeFollow, InverseEnvelope, Derivative, Hysteresis), and processor_params decodes int(slot0*3+0.5). That wiring is consistent. The default slot0 is 0.5 (ShapeShadowStorage and the node registration default), which decodes to int(2.0)=2. A newly chosen Temporal node therefore runs Derivative, not the EnvelopeFollow mode implied by DistortionParams::dynamicMode = 0. The adapter header documents '0=Envelope, 1=Inverse, 2=Derivative' and specs-overview.md:219 lists 'Mode (Envelope/Inverse/Derivative)'. Both omit the reachable Hysteresis mode, and the Hysteresis-only Depth/Hold group is conditional on it.

**Why wrong.** The neutral or documented default of a mode selector should be its first (classic) mode, and both documentation sites should list all four reachable modes. 0.5 is a continuous-knob centre that lands on an arbitrary enum member when reused for a list slot.

**Consequence.** Temporal nodes start in the transient-only mode: sustained material is barely distorted, which reads as 'Temporal does nothing' until the user finds the menu. Docs and spec under-describe the shipped feature.

**Evidence.** processor_params.cpp:156 p.dynamicMode = static_cast<int>(slots[0] * 3.0f + 0.5f); morph_node.h:102-113 all slots 0.5 except the BitwiseMangler override; editor.uidesc:1034 menu-items="Follow,Inverse,Transient,Hyst"; temporal_distortion.h:51-66 EnvelopeFollow=0 ... Hysteresis=3; distortion_adapter.h:127 'Mode: 0=Envelope, 1=Inverse, 2=Derivative'; specs-overview.md:219 'Mode (Envelope/Inverse/Derivative)'; distortion_adapter.cpp:745-746 clamps 0..3 and calls setMode.

**Suggested fix.** Give list-type slots per-type defaults in ShapeShadowStorage (Temporal slot0 = 0.0, as is already done for BitwiseMangler slot0). Update distortion_adapter.h:127 and specs-overview.md:219 to the four modes with their UI names. Test: a default-constructed ShapeShadowStorage loaded for Temporal and passed through mapShapeSlotsToParams yields dynamicMode == 0.

**Verifier notes:**
- Correctness: processor_params.cpp:156 decodes int(0.5*3+0.5) = 2 = Derivative (temporal_distortion.h:51-66); ShapeShadowStorage (morph_node.h:102-114) and MorphNode::shapeSlots (:43-44) default every slot to 0.5 with only the BitwiseMangler override, and parameter_registration.cpp:958-961 registers 0.5; both type-change paths load those slots (processor_params.cpp:844-852, visibility_controllers.cpp:445-452); distortion_adapter.h:127 and specs-overview.md:219 omit Hysteresis while editor.uidesc:1034 offers it.
- Context: ui-mockups.md:634 lists the Temporal Mode default as "EnvelopeFollow", so Derivative contradicts the spec; dsp-details.md:451-456 lists four modes while specs-overview.md:219 lists three, so the docs disagree; the only dynamicMode test (distortion_adapter_test.cpp:2025-2032) sets mode 1 explicitly.
- Impact (dissent): the consequence is overstated: Derivative drive is baseDrive_ * (1 + driveModulation_ * |d env| * k) (temporal_distortion.h:472-477), so a steady signal still distorts at its set drive and transients add a boost; the menu shows "Transient" for 0.5, so the UI matches the DSP; this verifier rated it cosmetic/doc; low retained.

### D-166 — FastMath::fastTanh returns the numeric_limits quiet_NaN() constant in a header compiled with -ffast-math on macOS

**Severity:** low | **Verdict:** CONFIRMED | **Category:** portability | **Dimension:** followup:shared-dsp-numerics-leftovers | **Corroborated by measurement:** no | **CROSS-PLUGIN:** every fastTanh caller

**Location:** [dsp/include/krate/dsp/core/fast_math.h:66-68](dsp/include/krate/dsp/core/fast_math.h#L66-L68)

**Problem.** For NaN input, fastTanh returns std::numeric_limits<float>::quiet_NaN(). The finiteness test itself is correct (detail::isNaN). Producing a NaN constant in fast-math code is what the project rule forbids: it is undefined behaviour under -ffinite-math-only, and clang warns with -Wnan-infinity-disabled. The site is a known debt ratcheted at tools/lint-nonfinite-symbols-baseline.json:3 ('fast_math.h': 1). The header is included by 13 dsp/include headers and called by distortion_adapter.cpp:282 (SoftClip).

**Why wrong.** The documented contract 'NaN input returns NaN' (fast_math.h:55) can be met without the constant: returning x propagates the incoming NaN. On the macOS leg the compiler may assume a fast-math function never returns NaN and fold away the caller's downstream detail::isNaN checks, or treat the branch as unreachable.

**Consequence.** The NaN path can behave differently on macOS than on Windows and Linux, and it keeps one entry in the non-finite-symbol lint baseline that could be removed. No audible effect on Windows or Linux. CROSS-PLUGIN (every fastTanh caller).

**Evidence.** fast_math.h:66-68 `if (detail::isNaN(x)) { return std::numeric_limits<float>::quiet_NaN(); }`. lint-nonfinite-symbols-baseline.json:3 `"dsp/include/krate/dsp/core/fast_math.h": 1`. distortion_adapter.cpp:282 `headroom * Krate::DSP::FastMath::fastTanh(excess * curveDrive)`. `grep -rln fast_math.h dsp/include | wc -l` gives 13.

**Suggested fix.** Replace `return std::numeric_limits<float>::quiet_NaN();` with `return x;`, keeping constexpr. Remove the fast_math.h entry from tools/lint-nonfinite-symbols-baseline.json so the ratchet goes down. The existing fastTanh NaN test in dsp_ieee_tests still passes because x is NaN.

**Verifier notes:**
- Correctness: fast_math.h:65-69 is as quoted and the baseline entry exists; `return x;` keeps the documented contract (fast_math.h:56) and the test at dsp/tests/unit/core/fast_math_test.cpp:99-101 still passes; the claim that downstream detail::isNaN checks would be folded is wrong because db_utils.h:100-133 reads bits through opaqueFloatBits, which survives -ffast-math; the same pattern also exists at db_utils.h:169 and :221 under that file's own baseline.
- Context: the baseline is a ratchet over pre-existing hits (CLAUDE.md), not an exemption; no comment explains the constant; Disrumpo reaches it at distortion_adapter.cpp:282.
- Impact (dissent): the NaN branch runs only when NaN is already in the signal path and the output is NaN either way (SoftClip returns sgn*(threshold + headroom*NaN) with both forms), so no observable behavioural difference across platforms; a lint-baseline/rule-hygiene item and a possible clang diagnostic on the macOS leg; this verifier rated it below low; low retained.

### D-165 — LFO random generator is not MINSTD: the uint32 multiply wraps before the modulus, giving a 79,493-step cycle instead of 2^31-2, reseeded to 12345 on every reset

**Severity:** low | **Verdict:** CONFIRMED | **Category:** dsp-numerics | **Dimension:** followup:shared-dsp-numerics-leftovers | **Corroborated by measurement:** yes (Brent cycle detection reproduced by two verifiers) | **CROSS-PLUGIN:** Ruinae, Profundum SmoothRandom/S&H LFOs

**Location:** [dsp/include/krate/dsp/primitives/lfo.h:475-480](dsp/include/krate/dsp/primitives/lfo.h#L475-L480), [:98](dsp/include/krate/dsp/primitives/lfo.h#L98), [:560](dsp/include/krate/dsp/primitives/lfo.h#L560)

**Problem.** nextRandomValue computes randomState_ * 48271u % 2147483647u in uint32. The product wraps mod 2^32 before the % 2^31-1, so this is not the MINSTD generator the comment names. Measured from the fixed seed 12345: a 68,319-step tail, then a 79,493-step cycle. State 0 is a fixed point with one reachable 31-bit preimage (1622613393, which maps to 0). It is not on the 12345 orbit, so the generator does not stick at 0 from the default seed. reset() reseeds every LFO to 12345 (line 98), so every LFO instance draws the same S&H/SmoothRandom sequence.

**Why wrong.** MINSTD needs a 64-bit intermediate (uint64_t(x)*48271 % 2147483647) for its full period and its statistics. The code's comment claims MINSTD parameters, and its behaviour has neither property.

**Consequence.** Audible impact is small: one draw per LFO cycle at 20 Hz repeats only after about 2 h (tail plus cycle). Disrumpo's LFO1 and LFO2, and any two LFOs in Ruinae/Profundum (morph_state.h:265 and profundum_morph_modulator.h:133 expose SmoothRandom), produce identical random step sequences that restart on every setActive. Two 'Random' modulators at the same rate move in lockstep. CROSS-PLUGIN.

**Evidence.** lfo.h:477 `randomState_ = randomState_ * 48271u % 2147483647u;`, :98 `randomState_ = 12345u;` in reset, :560 `uint32_t randomState_ = 12345u;`. Measured by scratchpad lcg.js (Brent cycle detection, Math.imul semantics): 'cycle length lambda=79493', 'tail mu=68319', 'preimage of target 2147483647: x=1622613393 ... step(x)=0'. Mean of the first 1e6 outputs is 0.00151.

**Suggested fix.** Use a 64-bit intermediate: randomState_ = static_cast<uint32_t>((uint64_t{randomState_} * 48271u) % 2147483647u). Guard a 0 seed. Let callers seed per instance (e.g. setSeed), and have the ModulationEngine give LFO1 and LFO2 different seeds. Add a dsp_primitives test: two LFOs on SampleHold with different seeds produce different first values, and the generator reproduces the known MINSTD sequence from seed 1 (48271, 182605794, ...).

**Verifier notes:**
- Correctness: lfo.h:476 says "same as MINSTD" and :477 multiplies in uint32_t (randomState_ at :560), so it wraps mod 2^32 before the modulus; reset() hard-sets 12345u (:98) and the class has no setSeed (grep finds only :97-98, :477, :479, :560); an independent Brent run with Math.imul semantics gives lambda 79493, mu 68319, matching the finding.
- Context: the comment contradicts the code, so this is not a documented choice; modulation_engine.h:810-811/128-129 reset both lfo1_ and lfo2_ to the same seed, so they share one sequence; no spec says lockstep is intended.
- Impact: the output stays in [-1, 1] and 0 is not on the 12345 orbit, so there is no NaN; the orbit of 147,812 draws is inaudible as a period; the visible effect is correlated Random/S&H LFOs across instances that repeat after every setActive; low.

### D-164 — Crossover3Way/Crossover4Way allpass compensation uses Q = 0.5 where LR4 needs Q = 1/sqrt(2): about -0.13 dB sum error at one-octave spacing, which breaks their own SC-004/SC-005 0.1 dB spec

**Severity:** low | **Verdict:** CONFIRMED | **Category:** shared-dsp-contract | **Dimension:** followup:shared-dsp-numerics-leftovers | **Corroborated by measurement:** yes (digital-domain re-evaluation by one verifier) | **CROSS-PLUGIN:** shared header; FractalDistortion's Crossover4Way runs uncompensated

**Location:** [dsp/include/krate/dsp/processors/crossover_filter.h:423](dsp/include/krate/dsp/processors/crossover_filter.h#L423), [:643](dsp/include/krate/dsp/processors/crossover_filter.h#L643), [:592-595](dsp/include/krate/dsp/processors/crossover_filter.h#L592-L595), [:835-844](dsp/include/krate/dsp/processors/crossover_filter.h#L835-L844)

**Problem.** Both classes configure their D'Appolito compensation allpasses with kAllpassQ = 0.5f and the comment 'matches LR4 phase'. For an LR4 crossover, LP + HP = (s^2 - sqrt2*s + 1)/(s^2 + sqrt2*s + 1), a second-order allpass with Q = 0.7071. Disrumpo's own crossover_network.h uses 0.7071 correctly. With Q = 0.5 the compensated low band is out of phase with the mid+high path near the lower crossover. Crossover4Way's sub band passes through two mismatched allpasses (lines 806-808), which adds to the error.

**Why wrong.** Phase compensation only works when the compensating allpass matches the phase of the higher crossover's LP+HP sum exactly. The SC-004/SC-005 tests (crossover_filter_test.cpp:1738-1827) pass only because they space the crossovers a decade apart (300/3000 Hz), where the mismatch is -0.018 dB.

**Consequence.** No production code currently enables compensation (grep: setAllpassCompensation is called only from crossover_filter_test.cpp). FractalDistortion's Crossover4Way (fractal_distortion.h:683, 965-967) runs uncompensated. The defect is latent, but the shared API ships with a wrong constant. The next consumer that turns compensation on with one-octave crossovers gets a -0.13 dB dip, more than the 0.1 dB the class promises. CROSS-PLUGIN.

**Evidence.** crossover_filter.h:423 and :643 `static constexpr float kAllpassQ = 0.5f;  ///< Q for 2nd-order allpass (matches LR4 phase)`. :594 `lowBandAllpass_.configure(FilterType::Allpass, freq, kAllpassQ, 0.0f, srFloat);`. Analog model (scratchpad apq.js) of the Crossover3Way sum LP1*AP(f2,Q) + HP1*LR4sum(f2), worst-case deviation: f1=300/f2=3000: Q0.5 -0.018 dB; 300/1200: -0.082 dB; 300/600: -0.129 dB at 292 Hz; 1000/2000: -0.129 dB; 80/300: -0.089 dB. Q=0.7071 gives 0.000 dB in every case.

**Suggested fix.** Set kAllpassQ = 0.70710678f in both classes and fix the comment. Add flat-sum test sections with one-octave spacing (e.g. 300/600 Hz and 80/160/320 Hz), which fail at Q = 0.5 (-0.129 dB) and pass at 0.7071.

**Verifier notes:**
- Correctness: both constants are 0.5f (:423, :643), used at :594 and :839-843; CrossoverLR4 cascades two Butterworth stages (:360-365, kButterworthQ 0.7071 at biquad.h:56) with no inversion (:315-322), so LP+HP is a Q = 1/sqrt2 allpass; the RBJ allpass at Q 0.5 (biquad.h:725-729) is ((s-1)/(s+1))^2, which does not match; the topology (:554-566 3-way, :793-814 4-way) is otherwise correct; the analog model reproduces (-0.018/-0.082/-0.129/-0.129/-0.089 dB at Q 0.5, 0.000 at 0.7071).
- Context: Disrumpo's own crossover_network.h:55 uses 0.7071f and documents why; the class doc at :405-408/:628 promises 0.1 dB; the SC-004/SC-005 tests use only 300/3000 and 80/300/3000 Hz; no production consumer calls setAllpassCompensation(true).
- Impact: a double-precision digital evaluation at 44.1 kHz gives 3-way 300/600 -0.129 dB, 1000/2000 -0.129 dB, 4-way 80/160/320 -0.149 dB, 100/200/400 -0.149 dB at Q 0.5 and 0.0000 dB at 0.7071; latent in a shared API; low.

### D-162 — DCBlocker latches NaN/Inf permanently, and the adapter puts it straight after shapers that pass NaN through: one bad sample silences the band's wet path for good (in other plugins the output stays NaN)

**Severity:** low (reported: medium; lowered by two verifiers) | **Verdict:** CONFIRMED | **Category:** dsp-numerics | **Dimension:** followup:shared-dsp-numerics-leftovers | **Corroborated by measurement:** no | **CROSS-PLUGIN:** DCBlocker inside TubeStage, FuzzProcessor, WavefolderProcessor, TapeSaturator

**Location:** [dsp/include/krate/dsp/primitives/dc_blocker.h:194-211](dsp/include/krate/dsp/primitives/dc_blocker.h#L194-L211)

**Problem.** DCBlocker::process computes y = x - x1_ + R_*y1_, then stores x1_ = x and y1_ = flushDenormal(y), with no finiteness check. flushDenormal (db_utils.h:263-265) returns NaN unchanged, because both of its comparisons are false for NaN. After one NaN input, x1_ and y1_ are NaN and every later output is NaN until reset(). One +/-Inf input sets x1_ = Inf, and the next sample computes x - Inf + R*Inf = NaN, so Inf latches too. In Disrumpo the adapter's own dcBlocker_ runs on the wet signal for AsymmetricFuzz, FullRectify, HalfRectify and FeedbackDist (distortion_adapter.cpp:218-220, 586-592). FullRectify passes NaN straight in: std::abs(NaN) at cpp:431, and the smoothing filter is skipped when smoothness <= 0.001 (cpp:432). HalfRectify passes +Inf through (cpp:442). The shared TubeStage, FuzzProcessor, WavefolderProcessor and TapeSaturator all have zero isNaN/isFinite checks and an internal DCBlocker (tube_stage.h:301, fuzz_processor.h:551, wavefolder_processor.h:373, tape_saturator.h:376), so they latch the same way.

**Why wrong.** A filter state must not keep a non-finite value forever. The project rule says NaN/Inf must never reach the output, and the sibling primitives OnePoleLP (one_pole.h:107-111) and Biquad reset and return 0 on non-finite input. FR-016 ('NaN propagates') is a per-sample contract. The DC blocker turns it into a permanent latch, and its tests only check the first output (dc_blocker_test.cpp:543-581), never recovery.

**Consequence.** Disrumpo in 1-band mode passes input straight to the bands with no biquad in between (crossover_network.h:194-198). One NaN or Inf sample from the host then poisons the node's DC blocker. The adapter's tone OnePoleLP sees NaN on every following sample, resets and returns 0 (one_pole.h:107-111). The node's wet path stays at digital silence until setActive or prepare. At mix 100 % the band is muted for the rest of the session, and that sample's dry NaN also reaches the output. Every other plugin that composes TubeStage, Fuzz, Wavefolder or Tape (amp_channel.h, fuzz_pedal.h, tape_machine.h, distortion_rack.h, bass_character_stage.h, seraphis_engine.h, vorago_engine.h, ruinae_voice.h) has no trailing OnePoleLP, so for them the output stays NaN permanently. CROSS-PLUGIN IMPACT.

**Evidence.** dc_blocker.h:202-208 `float y = x - x1_ + R_ * y1_; x1_ = x; y1_ = detail::flushDenormal(y); return y;`. db_utils.h:263-265 `return (x > -kDenormalThreshold && x < kDenormalThreshold) ? 0.0f : x;` (NaN is returned unchanged). distortion_adapter.cpp:431 `float result = std::abs(input);`, :442 `(input >= thresh) ? input : 0.0f`, :218-220 `if (needsDCBlock_) wet = dcBlocker_.process(wet);`. one_pole.h:108-111 resets and returns 0. crossover_network.h:195-197 `if (numBands_ == 1) { bands[0] = input; return; }`. grep -c isNaN|isFinite|isInf gives 0 for tube_stage.h, fuzz_processor.h, wavefolder_processor.h and tape_saturator.h. Lead 3 is refuted: FormantDistortion has no guard (formant_distortion.h:169-195), but FormantFilter's Biquads return 0 on NaN (formant_filter.h:534-537), so its DCBlocker sees finite input. Its envelope NaN is unused because envelopeFollowAmount_ defaults to 0 (:465) and the adapter never sets it (distortion_adapter.cpp:862-874), so no latch is reachable there.

**Suggested fix.** In DCBlocker::process, after computing y, test with detail::isFinite(y) (never std::isnan). If it fails, reset x1_ and y1_ to 0 and return 0, matching OnePoleLP and Biquad, and update the FR-016 doc. Add a test to dc_blocker_test.cpp (in dsp_ieee_tests, since it injects NaN): feed 10 normal samples, one NaN, then 100 normal samples, and assert the last output is finite. Do the same for +Inf. Add a Disrumpo test: 1 band, node FullRectify, mix 1, feed one NaN sample then a sine, and assert non-zero finite output after 100 samples.

**Verifier notes:**
- Correctness: the latch is real (dc_blocker.h:202-208, flushDenormal passes NaN at db_utils.h:264-266; one NaN leaves x1_/y1_ NaN, one Inf latches via x - Inf + R*Inf); the adapter wiring (distortion_adapter.cpp:218-220, :586-592, FullRectify :431-434) and the OnePoleLP reset-to-0 (one_pole.h:108-111) are as described; plugins/disrumpo/src has no isNaN/isFinite/isInf guard at all; the finding overstates reachability because at 2x/4x/8x the IIR oversampler's BiquadCascade (oversampler.h:374-375) resets and returns 0 on non-finite input (biquad.h:352-356), so a host NaN reaches the adapter only on the 1x direct path (factor == 1 at band_processor.h:632 or Oversample Max 1x).
- Context: the FR-016 note (dc_blocker.h:192) and test (dc_blocker_test.cpp:543-557) pin only the first output, not permanent latching; the DC-blocked types all recommend 2x or 4x (distortion_types.h:170-200), so 1x needs the user's max-oversample choice; the trigger is a non-finite host sample that already reaches the output through the dry term (:235).
- Impact (two verifiers lowered to low): in Disrumpo the latch needs 1 band, an effective 1x factor and a non-finite host input; with FullRectify/HalfRectify smoothness > 0.001 the smoothing filter probably breaks the chain; the cross-plugin "output stays NaN" claim traces no consumer's input guards; a real latent primitive defect worth hardening; low.

### D-161 — No evidence that any preset falls below -60 dBFS after the round trip: the probe never measures the reloaded render's level, and bounds put 22 of 35 at -38.9 dBFS RMS or higher

**Severity:** low | **Verdict:** CONFIRMED | **Category:** test-gap | **Dimension:** followup:preset-roundtrip-field-decode | **Corroborated by measurement:** yes (bounds derived from probe-results.txt rows)

**Location:** [specs/disrumpo-audit/probes/audit_probes.cpp.txt:1520-1545](specs/disrumpo-audit/probes/audit_probes.cpp.txt#L1520-L1545)

**Problem.** The preset probe reports peak, RMS and DC only for render 'a' (load(file) on an active processor), and only maxAbsDiff for the reloaded render 'b'. 'Lose audio' cannot be read off these numbers. From the triangle inequality, rms_b >= rms_a - maxdiff and peak_b >= maxdiff - peak_a. Applied to the 35 flagged presets, 22 have rms_b at -38.87 dBFS or higher (lowest Random_Scan; most are above -30). Pitch_Tracker (peak_b >= +0.62 dBFS) and Sharp_Focus (peak_b >= -13.07 dBFS, against peak_a -31.74) get louder after reload, not quieter. The remaining 11 (Wide_Sweep, Step_Scanner, Synced_Pulse, Chaos_Morph_4, Chaotic_Sweep, Evolving_Texture, Three_Node_Blend, Chaos_Engine, Serge_Madness, All_Types_Morph, Spectral_Scatter) are unbounded either way.

**Why wrong.** A claim that audio is lost has to come from a level measurement of the reloaded render. The only preset under -60 dBFS RMS in the whole run is Drums/Rectified_Drums (-62.22 dBFS), and that is already the level of a fresh file load. Its round trip is clean (rt 9.3e-10), so its quietness is a load or DSP issue (rectifier types), not a serialisation issue. None of the three identified mechanisms (baked sweep centre, mode forced to Linear1D, smoothing forced to 0) mutes the signal. They change the sweep position, the morph weights and the glide.

**Consequence.** The report should not carry 'presets lose audio after save/reload' as a severity driver. The real consequence is that the sound changes, by up to 1.25 full-scale and sometimes louder. Any residual silence risk is limited to the 11 unbounded presets and stays unmeasured.

**Evidence.** audit_probes.cpp.txt:1522-1533 compute pk/rms/dc from aL/aR only and `rt = max(maxAbsDiff(dL,bL), maxAbsDiff(dR,bR))`. Bounds were computed with awk from probe-results.txt rows: e.g. Random_Scan rms_a -22.07, rt 0.0674 -> rms_b >= -38.87 dBFS; Radial_Blend >= -5.18; Four_Corners >= -9.91; Pitch_Tracker pk_a -15.09, rt 1.25 -> peak_b >= 0.62 dBFS; Sharp_Focus pk_a -31.74, rt 0.248 -> peak_b >= -13.07 dBFS. Rectified_Drums row probe-results.txt:301 `-50.76,-62.22,0.000366,0,0,9.31e-10`.

**Suggested fix.** Extend the probe (and the promoted regression test) to emit peak, RMS and DC for render b as well as the rms delta b-a in dB. Assert rms_b > -60 dBFS and |rms_b - rms_a| < 0.1 dB in addition to maxdiff <= 1e-5. Separately, treat Rectified_Drums' -62 dBFS fresh-load level as its own item (D-156).

**Verifier notes:**
- Correctness: audit_probes.cpp.txt:1526-1528 compute pk/rms/dc from aL/aR over the window from 0.1 s, render b appears only in rt (:1530), and the level checks at :1535-1537 use render a; the RMS lower bounds are rigorous (windowed RMS of b-d is at most the whole-buffer maxdiff); the peak bounds are not strict because rt covers the whole buffer while pk is windowed from 0.1 s.
- Context: audit_followup_probes.cpp.txt also has no reloaded-render level measurement; the quoted rows match probe-results.txt:301/:309/:387/:389 (the file lives in f:/tmp/disrumpo-audit, not under specs); a related row (:233) shows Sharp_Focus going silent under host-order restore (path c), which is a different path and does not change the scope.
- Impact: 35 presets have rt > 1e-5 with det = 0, 22 have a finite RMS lower bound (lowest Random_Scan -38.87 dBFS), Synced_Pulse also has a peak bound (-58 dBFS) that the finding lists as unbounded; this is a gap in audit evidence and the regression test, not a product defect; low.

### D-160 — The probe's 121-preset baseline table measures the host-restore defect, not the presets: every type-specific field is at its DistortionParams default in those renders

**Severity:** low | **Verdict:** CONFIRMED | **Category:** measured | **Dimension:** followup:rectify-fold-level-rootcause | **Corroborated by measurement:** yes (preset-hostorder row for Rectified_Drums, +43.43 dB)

**Location:** [specs/disrumpo-audit/probes/audit_probes.cpp.txt:1502-1504](specs/disrumpo-audit/probes/audit_probes.cpp.txt#L1502-L1504)

**Problem.** The preset probe loads each .vstpreset with Processor::setState into a fresh active Rig and renders. That path never calls mapShapeSlotsToParams (processor_state.cpp:852-889), so the threshold, curve, sag, feedback, folds and similar fields stay at the defaults left by setupProcessing's SoftClip nodes. The peak/RMS/DC numbers per preset (probe-results.txt preset:* rows, e.g. :301) describe presets with their shape settings discarded.

**Why wrong.** A baseline used to judge preset levels (rms > -60, peak <= 0 dBFS) should reflect what the preset sounds like on the path that maps its slots, or should be labelled as the host-restore path. Rectified_Drums shows the gap: -50.8 dBFS measured vs about -12 dBFS intended.

**Consequence.** The report's preset level tables and any 'preset X is too quiet/loud' findings derived from them can be wrong in either direction. A fix-plan task that promotes these numbers to goldens would pin the bug.

**Evidence.** audit_probes.cpp.txt:1502-1504 `Rig r(sr); okA = setStateBytes(*r.p, bytes); r.render(in, in, aL, aR);` and :1541 'setState on active proc'. processor_state.cpp:871 writes shapeSlots only. distortion_adapter.h:85 threshold default 0.7. Rectified_Drums replica: -62.9 dBFS (defaults) vs -12.0 dBFS (mapped slots).

**Suggested fix.** When promoting the probe, render each preset twice: once via setState (host path, expected to match after the setState mapping fix) and once by replaying every parameter through processParameterChanges (preset-browser path). Assert the two agree within 1e-4 and gate levels on the replayed render.

**Verifier notes:**
- Correctness: the Rig constructor (audit_probes.cpp.txt:302-306) runs initialize, setupProcessing and setActive(true), then setStateBytes (:1464-1469) and render with no parameter replay; processor_state.cpp:852-889 writes type/common/bias/folds/bitDepth and raw shapeSlots and never calls mapShapeSlotsToParams (callers only at processor_params.cpp:852 and :892); "every type-specific field" overstates because bias, folds and bitDepth are restored (:858-861).
- Context: the probe row is labelled "setState on active proc" (:1541), which names the path but not the unmapped slots; the plugin's own browser takes loadComponentStateWithNotify -> performEdit (controller.cpp:123/1876/1872) and does map the slots, so the two load paths diverge; probe-results.txt:145 shows a 43.43 dB rms delta for Rectified_Drums between setState orderings.
- Impact: the baseline rows describe the host-restore defect (D-050) rather than the presets as authored and must not be promoted to goldens; the 'bad' flag on Rectified_Drums remains a valid host-path symptom; the -12 dBFS "intended" figure was not reproduced by the verifiers; low.

### D-154 — Build warning C4127 from the third-party Highway header while compiling KrateDSP

**Severity:** low | **Verdict:** CONFIRMED | **Category:** measured-build | **Dimension:** measured | **Corroborated by measurement:** yes (build lane, 4 warnings)

**Location:** [build/windows-x64-release/_deps/highway-src/hwy/contrib/math/math-inl.h:1026](build/windows-x64-release/_deps/highway-src/hwy/contrib/math/math-inl.h#L1026)

**Problem.** KrateDSP's compile of spectral_simd code includes Highway's math-inl.h, which raises C4127 (conditional expression is constant) 4 times.

**Why wrong.** The project requires zero warnings. Third-party headers should be included as SYSTEM includes, or the warning should be suppressed around the include.

**Consequence.** Warning noise in every KrateDSP build, which hides new warnings. All plugins are affected, since KrateDSP is shared.

**Evidence.** build lane: "warning C4127: conditional expression is constant [KrateDSP.vcxproj] (appears 4 times; third-party Highway header included from KrateDSP)" at math-inl.h "1026,24".

**Suggested fix.** Mark the Highway include directory SYSTEM (or /external:I with /external:W0 on MSVC), or wrap the include in `#pragma warning(push/disable:4127)`. Check: a clean build log has 0 warnings.

**Verifier notes:**
- Correctness: f:/tmp/disrumpo-audit/build.log lines 25, 54, 83 and 112 each read "math-inl.h(1026,24): warning C4127" for KrateDSP.vcxproj and are the log's only warnings; math-inl.h:1026 is `if (kAllowSubnormals == true) {`, a template bool tested with a plain if; spectral_simd.cpp:15-17 includes hwy/foreach_target.h (which re-includes the TU per target ISA, explaining the count), hwy/highway.h and hwy/contrib/math/math-inl.h.
- Context: root CMakeLists.txt:336-345 fetches Highway without SYSTEM, dsp/CMakeLists.txt:35 links `hwy hwy_contrib` plainly and :46-51 compiles at /W4 with only /wd4100 disabled; the repo's smtg_mark_includes_as_system helper (CMakeLists.txt:244-262) covers only the VST3 SDK and VSTGUI; no accepted-warning note exists.
- Impact: build-log noise only, no runtime effect, no /WX so no build fails; every KrateDSP consumer sees it; low.

### D-152 — uidesc overlap in BandExpandedStrip: the ActiveNodes segment button overlaps the MorphSmoothing knob by 2 px

**Severity:** low | **Verdict:** CONFIRMED | **Category:** measured-lint | **Dimension:** measured | **Corroborated by measurement:** yes (uidesc-inventory lint)

**Location:** [plugins/disrumpo/resources/editor.uidesc:1328-1330](plugins/disrumpo/resources/editor.uidesc#L1328-L1330)

**Problem.** CSegmentButton Band.ActiveNodes spans y 182..202 and ArcKnob Band.MorphSmoothing starts at y=200 (x 75..105 inside 70..190), so the two overlap by 2 px.

**Why wrong.** Interactive controls should not overlap: the later view wins the clicks in the shared strip.

**Consequence.** The top 2 px of the smoothing knob are covered by the segment button, which can steal clicks there.

**Evidence.** lint: "[BandExpandedStrip]: CSegmentButton Band.ActiveNodes 70,182,120,20 / ArcKnob Band.MorphSmoothing 75,200,30,25". editor.uidesc:1328 `origin="70, 182" size="120, 20" control-tag="Band.ActiveNodes"`, :1330 `origin="75, 200" size="30, 25" control-tag="Band.MorphSmoothing"`.

**Suggested fix.** Move MorphSmoothing to y >= 202, or shrink ActiveNodes to height 18 (via the XSLT uidesc workflow).

**Verifier notes:**
- Correctness: both views sit in the same 200x230 "Morph" container (editor.uidesc:1322), the button covers x 70..190, y 182..202 and the knob x 75..105, y 200..225, a 30x2 px overlap; the template is instantiated for all four bands (uidesc:1359-1369 via controller.cpp:1347-1348) and sub_controllers.h:98-102 never repositions views.
- Context: no comment, spec or lint baseline marks the overlap as intended.
- Impact: the direction in the consequence is backwards: VSTGUI hit-tests the later child first, so the knob (declared later at :1330) wins and the bottom 2 px of the segment button's "1"/"2" segments are lost, not the knob's top; ArcKnob::onMouseDown (plugins/shared/src/ui/arc_knob.h:131-150) accepts any click in its rectangle; a 2 px strip in the expanded view, no audio effect; low.

### D-149 — uidesc-inventory reports 84 out-of-bounds ModRoute12-32 views, but they sit inside a CScrollView with container-size 488x576 (lint false positive)

**Severity:** low | **Verdict:** CONFIRMED | **Category:** measured-lint | **Dimension:** measured | **Corroborated by measurement:** yes (lint output)

**Location:** [plugins/disrumpo/resources/editor.uidesc:744](plugins/disrumpo/resources/editor.uidesc#L744), [:789-872](plugins/disrumpo/resources/editor.uidesc#L789-L872) (defect is in tools/uidesc-inventory.js:88-93)

**Problem.** The rows at y=198..558 are children of `CScrollView origin="0, 32" size="500, 198" container-size="488, 576"`. They are inside the scrollable container (the last row ends at 574 < 576), so they are reachable. The lint measures them against the view size and not the container-size.

**Why wrong.** Inside a CScrollView, the bounds check must use container-size. Reporting reachable views as defects makes the uidesc check permanently red and hides real defects.

**Consequence.** `uidesc-inventory.js --check` exits 1 on Disrumpo, so it cannot gate regressions.

**Evidence.** lint uidesc-inventory: "UIDESC DEFECTS {\"overlap\":20,\"out-of-bounds\":84}"; "out-of-bounds [editor]: 84 views, ModRoute12..ModRoute32 ... Parent container is 500x198, but the rows sit at y=198..558". editor.uidesc:744 `<view class="CScrollView" origin="0, 32" size="500, 198" container-size="488, 576" ...>`

**Suggested fix.** Teach tools/uidesc-inventory.js to use CScrollView container-size as the child bounds, and add a self-test case for it. No uidesc change is needed.

**Verifier notes:**
- Correctness: the 32 rows at :745-872 are 18 pt apart, rows 12-32 (:789-872) are 21 rows x 4 controls = 84 views, the last ends at 574 <= 576 and the widest at 385 <= 488, so every child fits the scroll container; tools/uidesc-inventory.js:88-93 checks children against parentSize and recurses with the view's `size`, never reading container-size; the self-test (:174-232) has no CScrollView case.
- Context: VSTGUI's CScrollView places children in its internal scroll container sized to container-size, so the rows are reachable by scrolling; nothing documents the omission as deliberate.
- Impact: the bug is in the tool, not the uidesc; uidesc-inventory.js is not a CI gate (referenced only by the Iterum workflows and this audit workflow), and the same run reports 20 overlaps, so the fix alone would not turn --check green; tooling hygiene; low.

### D-144 — Factory presets and digital types peak above 0 dBFS (up to +11 dBFS): drive multiplies into non-saturating shapers with no output normalisation

**Severity:** low (reported: medium; lowered by all three verifiers) | **Verdict:** CONFIRMED | **Category:** measured-probe | **Dimension:** measured | **Corroborated by measurement:** yes (probes `type: peak above 0 dBFS` and `preset levels: peak above 0 dBFS`)

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.cpp:205-212](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L205-L212)

**Problem.** `wet = input * commonParams_.drive;` (drive 0-10) feeds Aliasing, SampleReduce, BitwiseMangler, Granular, Chaos and Spectral, which do not bound their output. Nothing compensates gain or limits after the adapter.

**Why wrong.** Drive should push the signal into a shaper, not act as a +20 dB linear gain on transparent digital types. Factory presets should not clip from a -12 dBFS input.

**Consequence.** 11 factory presets exceed 0 dBFS from a -12 dBFS input (Spectral_Morph +4.84 dBFS), and digital types reach +11 dBFS, clipping the host bus.

**Evidence.** probe preset levels: peak above 0 dBFS (11 presets): "Spectral_Morph +4.84, Digital_Organic +4.41, Granular_Crush +3.94, Ring_Mod_Lead +3.09, Fractal_Noise +3.02, ...". type: peak above 0 dBFS: "Aliasing +8.55/+11.10; SampleReduce +8.49/+10.96; BitwiseMangler +2.48/+4.98; Granular +1.4..+4.6; Chaos +1.7/+3.6; Spectral +0.46/+2.96". Code: distortion_adapter.cpp:211 `wet = input * commonParams_.drive;`

**Suggested fix.** Apply drive-dependent output compensation for the non-saturating types, or bound their output, and re-level the affected presets. Test: all presets peak <= 0 dBFS at -12 dBFS input, and each type peaks <= +6 dBFS at drive 1.0.

**Verifier notes:**
- Correctness: :206-212 multiplies by drive for every type except Temporal and Stochastic; Aliasing (:520-522), SampleReduce (:474-476) and BitwiseMangler (:524-526) do not change level; after processRaw only the optional DC blocker, tone (skipped for these types) and mix follow, and processor.cpp has no limiter; probe rows match (Aliasing -0.94/+8.55/+11.10 dBFS at drive 2.5/7.5/10 on a -6 dBFS sine, close to plain linear gain); Bitcrush (-3.01) and Quantize (about -5 dBFS) do bound their output, so the digital types are inconsistent.
- Context: no spec note on auto-gain or level matching; no test pins preset peaks; the generator sets outputGain = 0.5 (0 dB) for every preset (disrumpo_preset_generator.cpp:237) and Spectral Morph (:1644-1671) uses drive 2.0-2.5 on Spectral/Granular/Fractal nodes.
- Impact (all three lowered): the per-type peaks come from drive 7.5-10 on a -6 dBFS input, the maximum of the specified range behaving as linear gain on amplitude-transparent types; float hosts do not clip internally, so "clipping the host bus" overstates; several over-level presets carry large DC (Chua_Circuit, Chaos_Lead, Fractal_Noise, Spectral_Morph |dc| 0.11-0.205) pointing at a separate cause, and Stochastic_Burst is exempt from the drive multiply; the user-facing part is 11 factory presets about 15-17 dB louder than bypass, a preset levelling problem; low.

### D-141 — Node type change swaps the adapter type instantly (small level jump)

**Severity:** low | **Verdict:** CONFIRMED | **Category:** measured-probe | **Dimension:** measured | **Also flagged by:** D-028, D-080 cover the same switch | **Corroborated by measurement:** yes (probe `automation:type-switch`)

**Location:** [plugins/disrumpo/src/processor/processor_params.cpp:840-853](plugins/disrumpo/src/processor/processor_params.cpp#L840-L853)

**Problem.** The kNodeType handler changes mn.type and re-maps the slots, and setMorphNodes then makes MorphEngine/DistortionAdapter::setType switch processRaw at once, with no state reset or crossfade.

**Why wrong.** Changing the transfer function on the audio path should be crossfaded to avoid a discontinuity.

**Consequence.** A slight jump (ratio 1.37) when the type is automated or switched during playback.

**Evidence.** probe automation:type-switch (SoftClip drive 3 -> SineFold): "jump_ratio 1.37, clicks 0" (threshold <= 1.3). Code: processor_params.cpp:847 `mn.type = newType;` then :852 `mapShapeSlotsToParams(mn.type, mn.shapeSlots, mn.params);`

**Suggested fix.** Crossfade old and new type over a few ms in the adapter or MorphEngine. Test: the automation:type-switch jump ratio must be <= 1.3.

**Verifier notes:**
- Correctness: processor_params.cpp:840-853 sets mn.type (:848), reloads the shadow slots (:850), re-maps (:852) and :899 calls setMorphNodes; MorphEngine::setNodes (morph_engine.cpp:131-140) calls adapters_[i].setType directly and DistortionAdapter::setType (distortion_adapter.cpp:142-162) only sets currentType_ and flags; the only crossfade in BandProcessor (band_processor.h:651-706, 8 ms) fires on an oversampling-factor change, not a type change; the probe (audit_probes.cpp.txt:1199-1201) steps to SineFold (its label says HardClip).
- Context: dsp-details.md:2122 section 16.1 says "Discrete (type, mode) | 5 ms crossfade | Prevent clicks", so the instant swap falls short of the documented spec; no test pins it.
- Impact: jump_ratio (audit_probes.cpp.txt:943-955) is the largest sample-to-sample step in the 50 ms after the change divided by the larger steady-state step before/after, so 1.37 means the switch itself makes a step bigger than either waveform's own, in the "suspicious" band with 0 clicks detected; pre -6.62 / post -14.66 dB is the two types' built-in level difference that a crossfade would not remove; type is a discrete parameter rarely automated; low.

### D-134 — Dead processor state: maxOversampleFactor_ is written and never read; crossoverFrequencies_ atomics are unused; band default limit 8x vs processor default 4x

**Severity:** low | **Verdict:** CONFIRMED | **Category:** anti-pattern | **Dimension:** gestalt-processor | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor.h:145](plugins/disrumpo/src/processor/processor.h#L145), [:179](plugins/disrumpo/src/processor/processor.h#L179)

**Problem.** maxOversampleFactor_ (default 4) is stored in processor_params.cpp:357 and never read anywhere. It is not saved and not pushed to the bands at setup, so each BandProcessor keeps its own default maxOversampleFactor_ = kMaxOversampleFactor = 8 (band_processor.h:836) until the parameter arrives. The crossoverFrequencies_ array of atomics ('Crossover frequency targets (normalized, for smoothing)') is never written or read. The processor.h comments also still say 'band count (1-8)' and 'bandIndex (0-7)'.

**Why wrong.** State that is written but never read, plus two defaults for one setting, is a maintenance trap. When oversampling is wired in, the FR-OS default of 4x will not hold until the host sends the parameter.

**Consequence.** No audible effect today, because the oversampling path is dead. When it is revived, bands default to an 8x limit instead of the specified 4x, and the 'smoothing' atomics invite someone to build on non-functional plumbing.

**Evidence.** grep over src/processor: `maxOversampleFactor_` appears only at processor.h:145 and processor_params.cpp:357 (store); `crossoverFrequencies_` appears only at processor.h:179; band_processor.h:836 `int maxOversampleFactor_ = kMaxOversampleFactor;` (=8, :47); processor.h:152 `/// @brief Current band count (1-8)`.

**Suggested fix.** Remove crossoverFrequencies_. Either remove maxOversampleFactor_ or make it authoritative: push it to the bands in setupProcessing and persist it, which ties into the known Oversample-Max-not-saved finding. Fix the stale 1-8 comments. A test that a fresh processor's bands report a 4x limit would catch the default mismatch.

**Verifier notes:**
- Correctness: processor.h:145 `std::atomic<int> maxOversampleFactor_{4}` is written only at processor_params.cpp:357 (the same case loops bp.setMaxOversampleFactor at :358-361, so the atomic is redundant); processor.h:179 crossoverFrequencies_ has no other reference in the processor (the hits in crossover_network.h:133-428 are a different member); setMaxOversampleFactor is called only from processor_params.cpp:360, never in setupProcessing or setActive, so the bands keep 8 (band_processor.h:836, :47) against the 4x default (processor.h:144, parameter_registration.cpp:90); processor.h:152 still says "(1-8)" while kMaxBands = 4 (band_state.h:61).
- Context: processor_state.cpp never mentions kOversampleMaxId, so the value is also not saved; no test pins the band default.
- Impact: Processor::process never calls processBlock (processor.cpp:487/494), so the 8x-vs-4x mismatch is latent and nothing the plugin plays depends on it; maintenance debt; low.

### D-131 — Factory preset test finds presets via a hard-coded F:/ path although DISRUMPO_RESOURCES_DIR is defined

**Severity:** low | **Verdict:** CONFIRMED | **Category:** portability | **Dimension:** test-gaps | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/tests/integration/preset/factory_preset_validation_test.cpp:115-134](plugins/disrumpo/tests/integration/preset/factory_preset_validation_test.cpp#L115-L134)

**Problem.** getPresetsDir tries four cwd-relative candidates and then falls back to the absolute path "F:/projects/iterum/plugins/disrumpo/resources/presets". tests/CMakeLists.txt:173 already defines DISRUMPO_RESOURCES_DIR, and editor_lifecycle_test uses it.

**Why wrong.** The result depends on the working directory and the developer's drive layout. On a CI runner or another checkout where no candidate matches, the preset cases depend on cwd luck.

**Consequence.** Factory-preset validation becomes fragile across machines and can fail spuriously, which encourages someone to skip it.

**Evidence.** factory_preset_validation_test.cpp:130 `fs::path absolute = "F:/projects/iterum/plugins/disrumpo/resources/presets";`; CMakeLists.txt:173 `DISRUMPO_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources"`

**Suggested fix.** Return fs::path(DISRUMPO_RESOURCES_DIR) / "presets" and delete the candidate list and the absolute fallback.

**Verifier notes:**
- Correctness: getPresetsDir (:115-134) checks four cwd-relative paths (:117-122), then the absolute F:/ path (:130), then a non-existent default (:133); three cases REQUIRE_FALSE(presetFiles.empty()) (:139-142, :148-150, :172-174); tests/CMakeLists.txt:171-174 defines DISRUMPO_RESOURCES_DIR and editor_lifecycle_test.cpp:22/:39 already use it.
- Context: catch_discover_tests (CMakeLists.txt:209) sets no WORKING_DIRECTORY, so ctest runs each case from build/<preset>/plugins/disrumpo/tests, where the deepest candidate resolves to a non-existent build path; tools/run-gate.js:108 and ci-test-shard.js:217 spawn from process.cwd() (the repo root), so the gates and CI pass because the first candidate matches.
- Impact: the documented `ctest --test-dir ... -j 30` run fails all three preset cases on any checkout not at F:/projects/iterum (Linux, macOS, a second worktree); test-only, shipped audio unaffected; low.

### D-130 — 'Bitcrush actually modifies the signal' never compares with the unprocessed or default output; CPU budgets are 4-5x the target and measure a non-plugin configuration

**Severity:** low | **Verdict:** CONFIRMED | **Category:** test-gap | **Dimension:** test-gaps | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/tests/integration/bitcrush_noise_repro_test.cpp:229-321](plugins/disrumpo/tests/integration/bitcrush_noise_repro_test.cpp#L229-L321)

**Problem.** The case captures the default SoftClip output (rmsDefault/acDefault, only WARNed) and the 1-bit Bitcrush output, then REQUIREs only rms > 0.001 and not-white-noise. Its INFO says 'If autocorrelation is same as default, bitcrush is not taking effect', but no assertion checks that. PT-DI-001/002 in distortion_adapter_test.cpp allow 10%/20% against 2%/5% targets (:1802, :2174). PT-DI-002 times a legacy single-SoftClip BandProcessor fed one mono band to both channels, not the processor's 4-band x 4-node morph path, where routeParamsToProcessor runs per sample.

**Why wrong.** A test named 'modifies the signal' has to assert a difference from the input. CPU budgets per CLAUDE.md must never be relaxed, and they should measure the shipped worst case.

**Consequence.** A Bitcrush that passes audio through unchanged would pass. CPU regressions in the per-sample same-family setParams path are invisible.

**Evidence.** bitcrush_noise_repro_test.cpp:317-320 `INFO("If autocorrelation is same as default, bitcrush is not taking effect"); REQUIRE(rmsBitcrush > 0.001f); REQUIRE_FALSE(looksLikeWhiteNoise(...));`; distortion_adapter_test.cpp:1802 `REQUIRE(cpuPercent < 20.0);  // Allow up to 20% for CI variability` vs comment 'Should be under 5% CPU'

**Suggested fix.** REQUIRE that the RMS of (bitcrush output - default output) > 0.05, or that the count of distinct output levels is <= 4 for 1 bit. Restore the 2%/5% bars as [performance]-lane REQUIREs, and add a Processor::process benchmark with 4 bands x 4 nodes at the center in same-family Experimental and cross-family modes.

**Verifier notes:**
- Correctness: bitcrush_noise_repro_test.cpp:253-258 computes rmsDefault/acDefault and only WARNs them, and the REQUIREs at :318-320 are rms > 0.001 and not-white-noise, so a SoftClip sine at drive 5 passes both; distortion_adapter_test.cpp:1801-1803 asserts < 20% against a "Should be under 5% CPU" comment, :2175 < 10% against 2%, and :2248 (PT-DI-003) < 40% against 10%; PT-DI-002 (:1749-1789) drives four legacy SoftClip BandProcessors with identical L/R, not Processor::process.
- Context: the case is tagged [diag] (:230) as a bug-repro diagnostic; Bitcrush output difference is asserted elsewhere (multi_node_distortion_test.cpp:1027-1072 RMS difference > 0.05; distortion_adapter_test.cpp:840/891/1013/1060/1158), so a pass-through Bitcrush DSP would be caught there and the gap is processor-level only; CLAUDE.md says never relax a budget and the [performance] cases run alone, so CI variability is no reason for the margin.
- Impact: no shipped defect is shown; the per-sample routeParamsToProcessor path (morph_engine.cpp:706-710/733-736 -> distortion_adapter.cpp:173-175) has no benchmark; low.

### D-129 — No test injects NaN/Inf input or runs at 192 kHz, a mono bus, or numSamples 0 at processor level

**Severity:** low | **Verdict:** CONFIRMED | **Category:** test-gap | **Dimension:** test-gaps | **Corroborated by measurement:** partly (the probe lane did run 192 kHz with finite input and found no NaN)

**Location:** [plugins/disrumpo/tests/integration/processor_audio_output_test.cpp:75-137](plugins/disrumpo/tests/integration/processor_audio_output_test.cpp#L75-L137)

**Problem.** grep for quiet_NaN, infinity() or NaN bit patterns in plugins/disrumpo/tests finds nothing. The 'NaN' checks in distortion_adapter_test only check that finite input gives finite output. 192000/176400 appear only in crossover_network_test. No test calls setBusArrangements with a mono bus or process() with numSamples = 0, numChannels = 1 or null buffers. The ProcessorFixture hard-codes 44.1 kHz, 2 channels and 512 samples.

**Why wrong.** Several wrapped processors do not absorb non-finite input. FormantDistortion::process has no guard, RingSaturation returns NaN input unchanged, and EnvelopeFollower latches NaN. Process() has no output sanitisation. The 8x adapter prepare makes 192 kHz a 1.536 MHz adapter rate. These are the cross-platform sentinels CLAUDE.md says must stay per-push.

**Consequence.** A single NaN from a host or a modulation source can permanently poison a band's IIR state, and high-rate behaviour is unknown, with no failing test.

**Evidence.** grep `quiet_NaN|infinity()|0x7FC00000|nanf` over plugins/disrumpo/tests returns no hits; grep `192000|176400` matches only unit/crossover_network_test.cpp; processor_audio_output_test.cpp:84-88 setup.sampleRate = kSampleRate, maxSamplesPerBlock = kBlockSize, two channels only

**Suggested fix.** Add TEST_CASE "Processor output stays finite after NaN/Inf input for every type": per type, inject one NaN sample (bit pattern via volatile, per the fast-math memory note) and one +Inf sample, then 4096 finite samples, and REQUIRE detail::isFinite on every output after 1024 samples. Add sample-rate sections 44.1/96/192 kHz to 'Processor produces audio output with default parameters', and a setBusArrangements(mono) REQUIRE == kResultFalse case.

**Verifier notes:**
- Correctness: no non-finite injection exists (every NaN hit checks finite-in/finite-out with std::isnan, e.g. distortion_adapter_test.cpp:2302, morph_interpolation_test.cpp:570/674/749/834, multi_node_distortion_test.cpp:692-694 which only WARNs); 192000 appears only in unit/crossover_network_test.cpp:265/:589; no test calls setBusArrangements or uses kSpeakerMono or numSamples = 0; the fixture (processor_audio_output_test.cpp:84-88, :111-125) hard-codes 44.1 kHz, 2 channels, kBlockSize.
- Context: no CI workflow runs pluginval on Disrumpo and pluginval never injects NaN; the only guards in plugins/disrumpo/src are processor.cpp:196 (numSamples == 0) and processor_messaging.cpp:95; no spec excludes these cases.
- Impact: the band path is mostly protected because host input reaches the adapters through CrossoverLR4 Biquads that reset on non-finite input (biquad.h:353-356), so "permanently poisons a band's IIR state" is overstated; the real latent latch is the sweep envelope (processor.cpp:340-342 -> sweep_envelope.h:192 -> EnvelopeFollower, "Does NOT validate input" at envelope_follower.h:163) and the ModulationEngine follower (modulation_engine.h:812), D-013; process() does not sanitise output (:502-503, NaN*0 = NaN); the mono-bus and numSamples cases are low-value (processor.cpp:527-533 rejects non-stereo, :196 handles 0); low.

### D-128 — DataExchange delivery test is [!mayfail] on every platform, including Windows where the pump works

**Severity:** low | **Verdict:** CONFIRMED | **Category:** test-gap | **Dimension:** test-gaps | **Corroborated by measurement:** yes (two verifiers ran the case on Windows: all 10 assertions pass)

**Location:** [plugins/disrumpo/tests/integration/data_exchange_spectrum_test.cpp:200-202](plugins/disrumpo/tests/integration/data_exchange_spectrum_test.cpp#L200-L202)

**Problem.** The only end-to-end spectrum delivery case is tagged [!mayfail] unconditionally, and the comment justifies it with the missing macOS CFRunLoop pump. The project rule (CLAUDE.md 'Linux test harnesses have no run loop') is to gate such non-vacuity checks by platform, not to make them non-failing everywhere.

**Why wrong.** [!mayfail] turns every failed REQUIRE into a pass. A Windows regression in the processor->controller spectrum pipeline (block sizing, the aligned_alloc rounding, editorClosing_ guard) can no longer go red anywhere.

**Consequence.** A spectrum display that silently goes dead would not be caught by CI.

**Evidence.** data_exchange_spectrum_test.cpp:200-202 `// CFRunLoop pump, so spectrum data is never delivered. [!mayfail]  TEST_CASE("DataExchange delivers audio samples to controller local FIFOs", "[disrumpo][data-exchange][integration][!mayfail]")`

**Suggested fix.** Drop [!mayfail]. Wrap the delivery REQUIREs in `#if defined(_WIN32)` (or a platform check where a timer exists), and on the other platforms assert only that sendSpectrumBlock wrote the Tier-3 shared FIFO.

**Verifier notes:**
- Correctness: the tag at :199-202 has no platform guard; pumpMessages (:46-64) pumps via PeekMessageW/DispatchMessageW only under _WIN32 and sleeps elsewhere; the case is the only one that asserts isSpectrumDataAvailable/getLocalInputFIFO (grep finds no other); running it by name on Windows gives "All tests passed (10 assertions in 1 test case)" with totalWritten 1536 per FIFO, so the tag only hides failures there.
- Context: the covered paths (processor_messaging.cpp:42-59, :90-147 block sizing/throttle/lockBlock; controller.cpp:1924-1954 editorClosing_ guard, size check, FIFO push) can regress with no red; Innexus has the same pattern (test_data_exchange_pipeline.cpp:213, 234); the comment's reasoning covers macOS only, and Linux has no pump either.
- Impact: real hosts have the Tier-3 fallback (processor_messaging.cpp:112-121, controller.cpp:1476-1478), so some regressions would degrade rather than kill the display, but a regression after dataExchangeActive_ is set, or a failed bridge lookup, would go unnoticed; test gap; low.

### D-127 — Assertions that cannot fail: REQUIRE(true) smoothing cases, an ignored truncated-stream result, and a 'real-time safety' case with no allocation tracking that never reaches the allocating path

**Severity:** low | **Verdict:** CONFIRMED | **Category:** test-gap | **Dimension:** test-gaps | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/tests/unit/morph_band_integration_test.cpp:271](plugins/disrumpo/tests/unit/morph_band_integration_test.cpp#L271), [:288](plugins/disrumpo/tests/unit/morph_band_integration_test.cpp#L288)

**Problem.** 'BandProcessor morph smoothing time configurable' has both sections end in REQUIRE(true), with no timing or level check. serialization_round_trip_test.cpp:660-689 'truncated stream loads partial data' does `(void)result;` and checks only that getState still works, so nothing is verified as loaded. distortion_adapter_test.cpp:1907-1965 'UT-DI-008: Real-time safety' only calls setType and process, so it never reaches setParams, where spectral_.prepare re-allocates whenever p.fftSize != getFftSize() (distortion_adapter.cpp:894-896). That mismatch always holds at first use: prepare passes maxBlockSize*8 as the FFT size (:84). The case also does not use tests/test_helpers/allocation_detector.h, which other plugins use, and ends in CHECK(true).

**Why wrong.** Each of these cases is named for a property it does not check. An allocation detector exists in the repo, so the real-time claim can be asserted, and the allocating path should be exercised.

**Consequence.** A real-time violation, allocation inside processParameterChanges -> setMorphNodes -> setParams on the audio thread, and any smoothing regression pass silently.

**Evidence.** morph_band_integration_test.cpp:270-271 `// No crash or artifacts expected  REQUIRE(true);`; serialization_round_trip_test.cpp:684 `(void)result;`; distortion_adapter_test.cpp:1937 `CHECK(true);  // If we got here, no crashes occurred`; distortion_adapter.cpp:894-896 `if (static_cast<std::size_t>(p.fftSize) != spectral_.getFftSize()) { spectral_.prepare(sampleRate_, static_cast<std::size_t>(p.fftSize)); }`

**Suggested fix.** Replace REQUIRE(true) with a measured check: the cursor-weight midpoint is crossed in the expected window for 5 ms / 200 ms, scaled by host rate. In UT-DI-008, wrap setParams + process for all 26 types (Spectral with fftSize 1024/2048/4096) in the allocation detector and REQUIRE zero allocations. It fails today on the Spectral re-prepare. In the truncated test, truncate after the band count and REQUIRE that inputGain is applied and the bands keep their defaults.

**Verifier notes:**
- Correctness: both smoothing SECTIONs end in REQUIRE(true) (:271, :288) with no timing check; the truncated-stream case is at tests/unit/preset/serialization_round_trip_test.cpp:660-685 (the finding's path omits preset/), discards the result at :680 and asserts only getState == kResultOk at :684; UT-DI-008 (:1907-1965) never calls setParams, ends in CHECK(true) at :1937 and says in its own comment (:1961-1964) that no allocation tracking is done; tests/test_helpers/allocation_detector.h exists and no Disrumpo test uses it.
- Context: the allocating path is real (distortion_adapter.cpp:894-896 -> spectral_distortion.h:160-183 resize/re-prepare) and reached from the audio thread via morph_engine.cpp:710/:736, processor_params.cpp:899/:954 and band_processor.h:231/:450; one detail is wrong, distortion_adapter.cpp:84 passes maxBlockSize itself and the *8 comes from band_processor.h:111-112/124-125; MorphEngine::prepare (morph_engine.cpp:63-66) pre-prepares only nodes already Spectral, so a later type switch still re-prepares on the audio thread.
- Impact: coverage gap hiding the one-shot allocation on a Spectral type change (D-002) and the per-sample variant (D-001); the approval tests in morph_artifact_test.cpp partly cover smoothing artifacts; low.


### D-113 — std::isnan/std::isinf NaN guard in a fast-math test TU (folded away on macOS)

**Severity:** low | **Verdict:** CONFIRMED | **Category:** portability | **Dimension:** portability-quality | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/tests/integration/multi_node_distortion_test.cpp:693-696](plugins/disrumpo/tests/integration/multi_node_distortion_test.cpp#L693-L696)

**Problem.** The cross-family BitwiseMangler test sanitises its output with `if (std::isnan(output[i]) || std::isinf(output[i])) { WARN(...); output[i] = 0.0f; }`. This TU is not in the -fno-fast-math list (tests/CMakeLists.txt:181-203), so on the macOS leg Apple clang folds the checks to false and the NaN diagnostic and sanitisation never run.

**Why wrong.** CLAUDE.md requires finiteness checks to use Krate::DSP::detail::isFinite/isNaN in fast-math code, or the TU to be listed among the IEEE sets. The other morph tests that use std::isnan are in the IEEE list. This one was missed.

**Consequence.** On macOS a NaN regression in this path gives no 'NaN/Inf at sample' diagnostic and fails later on an unrelated-looking RMS assertion. It is a diagnostic gap, not a false green.

**Evidence.** multi_node_distortion_test.cpp:693 `if (std::isnan(output[i]) || std::isinf(output[i])) {`; tests/CMakeLists.txt:181-203 IEEE list does not contain integration/multi_node_distortion_test.cpp; tools/lint-nonfinite-symbols.js reports clean only because it guards src, not this test usage.

**Suggested fix.** Replace the check with Krate::DSP::detail::isNaN/isInf, or add the file to the IEEE set_source_files_properties block.

**Verifier notes:**
- Correctness: lines 693-696 are as quoted and are the file's only finiteness check; the TU is compiled into disrumpo_tests (tests/CMakeLists.txt:129) and is absent from the IEEE block at :181-205; the Xcode fast-math comes from extern/vst3sdk/cmake/modules/SMTG_PlatformToolset.cmake:42-46 at directory scope, so on macOS the check is dead code and a NaN reaches the RMS REQUIRE at :710-712 without the per-sample diagnostic.
- Context: the evidence line is wrong on one point: tools/lint-nonfinite-symbols.js does scan plugins/ including tests (SCAN_DIRS, line 113) and records this file as known debt in tools/lint-nonfinite-symbols-baseline.json:21; a baseline is a ratchet, not an exemption, so the defect stands.
- Impact (dissent): if folded, the test gets stricter on macOS, not looser: `rms > 0.01f` on a NaN evaluates false and the REQUIRE fails, so no false green is possible and the only loss is the WARN text; this verifier rated it below low; low retained.

### D-112 — Comments that contradict the code at load-bearing spots

**Severity:** low | **Verdict:** CONFIRMED | **Category:** anti-pattern | **Dimension:** portability-quality | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/band_processor.h:465-466](plugins/disrumpo/src/dsp/band_processor.h#L465-L466)

**Problem.** Several comments describe behaviour the code does not have. (a) BandProcessor::process is documented as 'Applies sweep, distortion/morph with oversampling', but it runs MorphEngine at the host rate with no oversampler (band_processor.h:473-512), and it is the only path Processor::process uses. (b) plugin_ids.h:156 says output params 'use standalone IDs outside the encoding scheme', but 0x0F80/0x0F81 fall inside the global 0x0Fxx range, where isGlobalParamId() returns true. (c) isNodeParamId's doc says 'band nibble = 0-7', but the code accepts 0-3 (plugin_ids.h:318 vs :334). (d) MorphEngine documents setMorphPosition/setMode/setNodes as 'Thread-safe parameter updates' (morph_engine.h:40); they are plain non-atomic member writes. (e) processor_params.cpp:456 says the note index is '[0,15]', but it uses kNoteValueCount-1 = 29. (f) controller.h:299 calls the duplicated SharedDisplay struct a 'Forward declaration'. It is a second definition with a layout identical to processor.h:268, punned through void* with no static_assert.

**Why wrong.** These comments sit on the exact assumptions a fixer relies on (oversampling, thread safety, ID classification, bridge layout). Each one points a reader toward the wrong fix.

**Consequence.** Maintenance traps. For example, a developer trusting (d) could call setNodes from the UI thread. A change to one SharedDisplay struct without the other silently mis-reads FIFO pointers in the Tier-3 fallback.

**Evidence.** band_processor.h:466 `/// Applies sweep, distortion/morph with oversampling.` vs :486-489 `left = morphEngine_->process(left); right = morphEngine_->process(right);`. plugin_ids.h:156 'Output parameters use standalone IDs outside the encoding scheme' vs :164 `kSweepModulatedFrequencyOutputId = 0x0F80` and :83-84 isGlobalParamId `(paramId & 0xFF00) == 0x0F00`. morph_engine.h:40 'setMorphPosition/setMode/setNodes: Thread-safe parameter updates' vs morph_engine.h:219-259 non-atomic members. processor_params.cpp:456-458. controller.h:299-304 '/// Forward declaration of processor's shared display struct' followed by a full struct definition.

**Suggested fix.** Correct the comments. Move SharedDisplay into one neutral plugin-local header that both sides include, or at minimum add static_asserts on sizeof and offsetof in both TUs.

**Verifier notes:**
- Correctness: all six sub-claims hold against source (band_processor.h:465-466 vs :473-512 and processor.cpp:487/:494; plugin_ids.h:156 vs :164/:168/:83-84; :318 vs :334-335; morph_engine.h:40 vs :219-249; processor_params.cpp:456 vs :458 with kNoteValueCount = 30 at plugin_ids.h:704; controller.h:299-304 vs processor.h:268-272, no static_assert).
- Context: nothing documents the comments as intentional; grep for static_assert/offsetof in processor/ and controller/ finds nothing, so the two SharedDisplay layouts are unguarded.
- Impact: comments only, no runtime effect today because the two struct layouts match; the duplicated SharedDisplay without a static_assert is the one latent risk; low.

### D-110 — Dead members bloat every DistortionAdapter (24 per plugin instance) and get memset on every prepare/reset

**Severity:** low | **Verdict:** CONFIRMED | **Category:** duplication | **Dimension:** portability-quality | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/distortion_adapter.h:370](plugins/disrumpo/src/dsp/distortion_adapter.h#L370), [:396-399](plugins/disrumpo/src/dsp/distortion_adapter.h#L396-L399), [:439-446](plugins/disrumpo/src/dsp/distortion_adapter.h#L439-L446)

**Problem.** saturation_ (SaturationProcessor) is prepared and reset but never processed. tubeShaper_ and tapeShaper_ are configured in prepare but never used in processRaw, even though the comment says 'These are used instead of the block-based processors'. inputRingBuffer_ and outputRingBuffer_ (2 x 4096 floats), ringWritePos_/ringReadPos_ and isBlockBased_ are written but never read. blockLatency_ is read only by getProcessingLatency(), which has no callers. Processor::crossoverFrequencies_ (processor.h:179) is likewise declared and never used.

**Why wrong.** Comments claim behaviour that does not exist, and dead state costs memory and per-reset work. The project already hit stack-overflow problems with this object (stack_footprint_test.cpp, MorphEngine 936 KB).

**Consequence.** About 32 KB of dead ring buffer per adapter. With 6 adapters per band (1 legacy + 4 nodes + 1 blended) x 4 bands, that is about 768 KB per plugin instance, plus 32 KB of fill() per adapter on every prepare() and setActive reset. Readers are misled into thinking block-based latency handling exists.

**Evidence.** grep in plugins/disrumpo/src: saturation_ appears only at distortion_adapter.cpp:34 (prepare) and :106 (reset); tubeShaper_/tapeShaper_ only at :40-43; inputRingBuffer_/outputRingBuffer_ only at :89-92, :132-135 (fill/zero); isBlockBased_ written at :149,:153,:158 and never read; getProcessingLatency defined at :248 with no caller. processor.h:179 `std::array<std::atomic<float>, kMaxBands - 1> crossoverFrequencies_{};` has no use in processor/*.cpp.

**Suggested fix.** Delete saturation_, tubeShaper_, tapeShaper_, the ring buffers and their positions, isBlockBased_, and Processor::crossoverFrequencies_. Either wire getProcessingLatency into latency reporting or remove it. stack_footprint_test then covers the size reduction.

**Verifier notes:**
- Correctness: every grep result holds (saturation_ at cpp:34/:106 only, live paths use tube_/tape_ at :315/:335; tubeShaper_/tapeShaper_ only at :40-43; ring buffers 2 x 16 KB filled at :89-90/:132-133; isBlockBased_ write-only at :149/:153/:158); one inaccuracy: getProcessingLatency does have callers in tests/unit/distortion_adapter_test.cpp:133-149, so "no callers" should read "no production callers"; the 24-adapter count (band_processor.h:803, morph_engine.h:242/:245, processor.h:176) and 768 KB arithmetic check out.
- Context: the header comments at h:242-245 and :391-393 describe block-based latency handling that does not exist; nothing in specs or comments makes the dead state intentional.
- Impact: the stack-overflow angle is weak because the Processor is heap-allocated and MorphEngine sits behind a unique_ptr (band_processor.h:807); setType/setParams never call reset, so nothing runs per block; memory footprint and misleading comments only; low.

### D-107 — Two contradictory implementations of the MorphLinkMode curves (controller vs DSP)

**Severity:** low (reported: medium; lowered by all three verifiers) | **Verdict:** CONFIRMED | **Category:** duplication | **Dimension:** portability-quality | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/sweep_morph_link.h:86-124](plugins/disrumpo/src/dsp/sweep_morph_link.h#L86-L124)

**Problem.** The same enum MorphLinkMode (plugin_ids.h:640-650) is evaluated by two functions with different math. The live one is controller/morph_link.cpp applyMorphLinkMode, called by MorphSweepLinkController (visibility_controllers.cpp:194-195). The DSP one is sweep_morph_link.h applyMorphLinkCurve, reached only via SweepProcessor::getMorphPosition (sweep_processor.h:308), which nothing calls. They differ as follows. EaseIn: sqrt(x) in the controller vs x^2 in the DSP (opposite curvature). EaseOut: x^2 vs 1-(1-x)^2. HoldRise: threshold 0.5 vs 0.6. Stepped: 5 levels (0,.25,.5,.75,1) vs 4 levels (0,.333,.667,1). None: manual position vs 0.5. The DSP version and its tests (dsp/sweep_morph_link_tests.cpp) pin behaviour that does not ship. plugin_ids.h:647 documents the controller's 5-step version.

**Why wrong.** One enum should have one transfer function. The DSP copy is dead but tested, so it reads as authoritative. Anyone who wires sweep->morph linking into the processor (the intended FR) will silently invert EaseIn/EaseOut relative to what users hear today.

**Consequence.** Maintenance trap. The spec, docs and tests disagree about the curve shapes. A future processor-side link changes the sound of every preset that uses EaseIn, EaseOut, HoldRise or Stepped.

**Suggested fix.** Keep one implementation in a shared header that both controller and DSP include (both are plugin-local and not processor/controller headers), and delete the other. Add a test that asserts the shared function's values at x = 0.25/0.55/0.75 for each mode.

**Evidence.** controller/morph_link.cpp:44-47 `case MorphLinkMode::EaseIn: return std::sqrt(sweepNorm);` :49-52 EaseOut `return sweepNorm * sweepNorm;` :54-59 HoldRise `if (sweepNorm < 0.5f) return 0.0f;` :61-65 Stepped `std::min(std::floor(sweepNorm * 5.0f) / 4.0f, 1.0f)`. dsp/sweep_morph_link.h:98-100 EaseIn `return x * x;` :102-104 EaseOut `1.0f - (1.0f - x) * (1.0f - x)` :106-112 HoldRise `if (x < 0.6f)` :114-118 Stepped `std::min(std::floor(x * 4.0f), 3.0f) / 3.0f`; :86-88 None `return 0.5f;`.

**Verifier notes:**
- Correctness: both functions switch on plugin_ids.h:640-650 with the formulas as quoted (morph_link.cpp:34/:47/:52/:56-59/:65; sweep_morph_link.h:86-88/:98-100/:102-104/:109-112/:118); applyMorphLinkCurve's only production caller is sweep_processor.h:308 getMorphPosition, which no src file calls (tests/dsp/sweep_processor_tests.cpp:334-425 only); the processor only stores the mode (processor_params.cpp:410-413, processor_state.cpp:95/491-493).
- Context: specs/Disrumpo/dsp-details.md:1261-1322 gives the DSP formulas while the shipping controller cites the retired specs/006-morph-ui plan (morph_link.cpp:6), so the reference doc, the enum comment and two test files (tests/dsp/sweep_morph_link_tests.cpp:100-194, tests/unit/morph_link_test.cpp) disagree; nothing documents the split as intentional.
- Impact (all three lowered): users hear only the controller version and the DSP copy never runs, so there is no audible defect today; maintenance and spec-consistency trap; low.

### D-105 — Stale spec and code comments: 1-8 bands and default 2 morph nodes vs the shipped 4 bands and 1 node; FR-019 processor-cap tests are vacuous

**Severity:** low | **Verdict:** CONFIRMED | **Category:** spec-conformance | **Dimension:** spec-conformance | **Also flagged by:** D-121 covers the same FR-019 tests | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/morph_node.h:135](plugins/disrumpo/src/dsp/morph_node.h#L135), [:141-148](plugins/disrumpo/src/dsp/morph_node.h#L141-L148)

**Problem.** FR-BAND-001 / Appendix A say 1-8 bands and FR-MORPH-001 says 'Default: 2 nodes'. The code ships kMaxBands=4 (band_state.h:61) and kDefaultActiveNodes=1, a deliberate change (CHANGELOG.md:86), yet comments still say 'Current band count (1-8)' (processor.h:150) and '(1-8)' (crossover_network.h:110). FR-019's dynamic-threshold constants kMaxWeightThreshold/kWeightThresholdIncrement/kMaxGlobalProcessors are defined but no code uses them. The FR-019 tests (unit/morph_processor_cap_test.cpp:122-145) only assert activeCount <= 16 for one engine (at most 4), so they cannot fail.

**Why wrong.** The spec, the comments and the tests should describe the shipped limits. A test named for a requirement should exercise that requirement.

**Consequence.** This is a maintenance trap: readers and future agents size buffers or tests for 8 bands, and the FR-019 'cap' looks tested but is enforced nowhere (it holds only because 4 bands x 4 nodes = 16).

**Evidence.** morph_node.h:135 `kDefaultActiveNodes = 1;` :141-148 FR-019 constants; band_state.h:61 `kMaxBands = 4`; processor.h:150 `/// @brief Current band count (1-8)`; crossover_network.h:110 `@param numBands New number of bands (1-8)`; morph_processor_cap_test.cpp:144 `REQUIRE(activeCount <= Disrumpo::kMaxGlobalProcessors);` for one 4-node engine; specs-overview.md FR-BAND-001 'Range: 1 to 8 bands', FR-MORPH-001 'Default: 2 nodes (A and B)'.

**Suggested fix.** Update specs-overview FR-BAND-001/Appendix A to 1-4 bands and FR-MORPH-001 to a default of 1 node, and fix the '(1-8)' comments. Either delete the unused FR-019 constants and tests or implement the cap with a test that would fail without it.

**Verifier notes:**
- Correctness: morph_node.h:135/:142/:145/:148, band_state.h:61 and the test at morph_processor_cap_test.cpp:122-145 (one 4-node engine, `activeCount <= 16`, comment "For now, test the single-engine threshold behavior" at :129) are as claimed; the three FR-019 constants appear only in the test file (:144, :164-166); the stale "(1-8)" comment is at processor.h:152 (the finding said 150), plus crossover_network.h:78/:110, plugin_ids.h:383 and spectrum_display.h:61.
- Context: specs-overview.md:145/:158 say 1-8 bands and :228 says "Default: 2 nodes"; dsp-details.md:177 still shows `kMaxBands = 8`; CHANGELOG.md:86 (0.9.4) records the 1-node default as deliberate but nobody updated the spec.
- Impact: 4 x 4 = 16 meets the cap by construction, so nothing is audible or at runtime risk; doc and test maintenance; low.

### D-104 — Docs and CHANGELOG claim features the code does not deliver (intelligent oversampling, MIDI CC learn, 120 presets)

**Severity:** low | **Verdict:** CONFIRMED | **Category:** spec-conformance | **Dimension:** spec-conformance | **Also flagged by:** D-017/D-027 (oversampling), D-096/D-136 (MIDI) carry the underlying defects | **Corroborated by measurement:** yes (probe `os-engaged:processor:HardClip` 0.0 dB reduction)

**Location:** [plugins/disrumpo/docs/index.html:118](plugins/disrumpo/docs/index.html#L118), [:121](plugins/disrumpo/docs/index.html#L121), [:123](plugins/disrumpo/docs/index.html#L123)

**Problem.** The public docs advertise automatic 2x/4x oversampling and MIDI CC learn, neither of which works in the plugin (see the oversampling and event-bus findings), and '120 factory presets' while 121 .vstpreset files ship (factory_preset_validation_test.cpp expects 121) and the generator emits 119. CHANGELOG.md:121 and :124 (0.9.0) make the same oversampling and MIDI-learn claims.

**Why wrong.** User-facing documentation must describe the shipped behaviour. The preset count should match the files and the generator that produces them.

**Consequence.** Users rely on oversampling to avoid aliasing and on MIDI learn for performance, and get neither. The preset count claim disagrees with the product, and regenerating presets from the tool would drop two shipped presets.

**Evidence.** docs/index.html:118 'Intelligent oversampling — automatically selects 2x or 4x oversampling per algorithm'; :121 '120 factory presets across 11 categories'; :123 'Full parameter automation and MIDI CC learn support'; CHANGELOG.md:121 'Intelligent oversampling for alias-free distortion', :124 'MIDI CC learn for parameter automation'; inventory count: 121 files on disk, generator 119 PresetDef blocks.

**Suggested fix.** After fixing or ruling on oversampling and MIDI, update docs/index.html:118 and :123. Reconcile the preset count (disk 121, generator 119, docs 120) by adding the two missing presets to tools/disrumpo_preset_generator.cpp and stating 121, or removing the extras.

**Verifier notes:**
- Correctness: oversampling never runs because processor.cpp:487/:494 call the per-sample BandProcessor::process (band_processor.h:473-494), never processBlock (:519-556, :709-743); MIDI learn cannot work because processor.cpp:65-66 adds no event input bus, so hosts never route MIDI or query IMidiMapping (controller.cpp:707-726), and the learn scan at processor.cpp:392-412 matches only kLegacyMIDICCOutEvent; 121 files on disk, factory_preset_validation_test.cpp:144 asserts 121 while its comments at :4/:8 say 120, and the generator has 119 PresetDef blocks.
- Context: the two files on disk with no generator entry are Experimental/Formant_Shift and Init/Init_5_Bands (an Init preset for 5 bands though the maximum is 4); the learn state is also split across kGlobalMidiLearnActive (controller.cpp:1626) and kSweepMidiLearnActive (processor_params.cpp:527).
- Impact: documentation only; the real consequences belong to the separate oversampling and event-bus findings; low.

### D-103 — Global metering (spec 3.7: input/output stereo peak+RMS, per-band indicators) is not implemented

**Severity:** low | **Verdict:** CONFIRMED | **Category:** spec-conformance | **Dimension:** spec-conformance | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/resources/editor.uidesc:1-1370](plugins/disrumpo/resources/editor.uidesc#L1-L1370)

**Problem.** No meter view, meter parameter or level message exists. grep -i 'meter|peak|rms' over src/controller and editor.uidesc finds nothing outside parameter names, and the processor sends only spectrum mono blocks.

**Why wrong.** specs-overview.md 3.7 Global Controls: 'Metering: Input/Output stereo peak+RMS, per-band indicators'.

**Consequence.** Users cannot see input or output level or per-band activity while gain-staging a distortion that can add +24 dB of band gain and drive 10. This is a missing specified feature.

**Evidence.** specs-overview.md 3.7 'Metering: Input/Output stereo peak+RMS, per-band indicators'; Bash grep -rn -i 'meter|peak\b|rms' over src/controller/*.cpp, src/controller/*.h and resources/editor.uidesc (excluding 'parameter') returned no output.

**Suggested fix.** Either implement the meters (processor computes peak/RMS per block and sends them through the existing DataExchange block, and the editor adds meter views) or strike the line from the spec. If implemented, add a controller test that a meter tag exists and receives non-zero values from a test block.

**Verifier notes:**
- Correctness: specs-overview.md:366 requires metering; editor.uidesc has no CVuMeter and no meter custom view (13 view classes, 10 custom-view-names, none a meter); the only processor-to-controller paths are SpectrumBlock (spectrum_block.h:31-40, no level fields), sendSpectrumBlock (processor_messaging.cpp:90-135) and the ModOffsets message (:154); nothing computes peak or RMS.
- Context: roadmap.md:569-572 (T13.7-T13.10, P0/P1) and tasks-overview.md:307-310 (T13.3) are still unchecked, and the roadmap's Out-of-Scope table (:97-105) does not list metering; CHANGELOG and docs/index.html never mention it shipped or dropped.
- Impact: missing UI feature, no audio defect; the per-band coloured spectrum display partly covers the "per-band indicators" item; low.

### D-102 — FR-DIST-003 / Appendix A type-parameter ranges drift from the spec, and Bitcrush's 1-4 bit range (bottom 20 % of the knob) is a dead zone

**Severity:** low | **Verdict:** CONFIRMED | **Category:** spec-conformance | **Dimension:** spec-conformance | **Also flagged by:** D-083, D-090 (Bitcrush dead zone) | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor_params.cpp:90](plugins/disrumpo/src/processor/processor_params.cpp#L90), [:131](plugins/disrumpo/src/processor/processor_params.cpp#L131), [:159-160](plugins/disrumpo/src/processor/processor_params.cpp#L159-L160), [:194](plugins/disrumpo/src/processor/processor_params.cpp#L194), [:207](plugins/disrumpo/src/processor/processor_params.cpp#L207), [:219](plugins/disrumpo/src/processor/processor_params.cpp#L219)

**Problem.** mapShapeSlotsToParams maps slots to ranges wider than the spec: folds 1-12 (spec 1-8; the wavefolder clamps at 10), Temporal attack 1-500 ms (spec 1-100), release 10-5000 ms (spec 10-500), Aliasing freq shift +/-5000 Hz (spec +/-1000), Chaos attractor speed 0.01-100 (spec 0.1-10), Formant shift +/-24 st (spec +/-12). Bitcrush maps bits to 1-16 per spec, but the shared BitCrusher clamps to [4,16] (bit_crusher.h:50,152).

**Why wrong.** FR-DIST-003 and Appendix A define these ranges. The adapter header comments (distortion_adapter.h) repeat the spec ranges, so code, header and spec disagree. For Bitcrush, the spec's 1-16 range is displayed but bits below 4 are unreachable.

**Consequence.** Knob travel does not match the documented ranges (most knob travel spent outside the musical range on Temporal/Chaos). The lowest ~20 % of the Bitcrush Bits knob (1-4 bits) sounds identical to 4 bits.

**Evidence.** processor_params.cpp:90 `p.folds = 1.0f + slots[0] * 11.0f; // [1,12]`; :131 `p.bitDepth = 1.0f + slots[0] * 15.0f; // [1,16]`; :159 `p.attackMs = 1.0f + slots[3] * 499.0f;` :160 `p.releaseMs = 10.0f + slots[4] * 4990.0f;` :194 `* 5000.0f`; :207 `0.01f + slots[1] * 99.99f`; :219 `* 24.0f`; bit_crusher.h:50 `kMinBitDepth = 4.0f`, :152 `bitDepth_ = std::clamp(bits, kMinBitDepth, kMaxBitDepth)`; specs-overview.md FR-DIST-003 lists Folds (1-8), Attack (1-100ms), Release (10-500ms), Attractor Speed (0.1-10.0), Formant Shift (-12 to +12), Bit Depth (1.0-16.0).

**Suggested fix.** Decide per parameter whether spec or code is authoritative and make spec, header comments and mapping agree. For Bitcrush, either lower BitCrusher::kMinBitDepth (shared primitive: CROSS-PLUGIN, check consumers) or map the slot to [4,16]. Add a mapping test that asserts each slot's 0 and 1 endpoints equal the Appendix A min and max.

**Verifier notes:**
- Correctness: the Bitcrush dead zone is real (processor_params.cpp:131 -> distortion_adapter.cpp:709/:467 -> bit_crusher.h:152 clamps to kMinBitDepth 4 at :50; format_helpers.h:124 still displays 1-16 bit), and the range drifts match specs-overview.md:217-221 and :422/:428/:430 and the header comments at distortion_adapter.h:99/:109/:125/:126/:149/:156/:162; the finding understates one dead zone: wavefolder.h:150 clamps folds at 10, so the top ~18 % of the Folds knob is dead too.
- Context: the wider mappings match the shared processors' own limits (temporal_distortion.h:130-135, aliasing_effect.h:88-89, formant_distortion.h:85-86, chaos_waveshaper.h:121-122), so most of the drift is a code-side decision the docs never caught up with; distortion_adapter_test.cpp:1032 sets bitDepth = 1 as if reachable.
- Impact: no crash, NaN or state break; UI readout dishonesty and dead knob travel on Bitcrush Bits and Folds; low.

### D-094 — A legacy sweep-only MIDI-learn system runs alongside the shared MidiCCManager; the controller half is dead and the processor half writes state nothing reads

**Severity:** low | **Verdict:** CONFIRMED | **Category:** duplication | **Dimension:** duplication-drift | **Also flagged by:** D-096, D-136 (MIDI learn dead end to end) | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/controller.cpp:714-723](plugins/disrumpo/src/controller/controller.cpp#L714-L723)

**Problem.** Controller::getMidiControllerAssignment first asks the shared Krate::Plugins::MidiCCManager, then falls back to a plugin-local `assignedMidiCC_` member (controller.h:435, initialised to 128) mapped to kSweepFrequency. Nothing in the controller ever assigns that member (grep shows only its declaration and its read at :720). On the processor side, a parallel implementation exists: processor.h:240 assignedMidiCC_, the CC capture at processor.cpp:409, and the kSweepMidiLearnActive/kSweepMidiCCNumber handlers at processor_params.cpp:527-534. It is a write-only re-implementation of what MidiCCManager does.

**Why wrong.** MIDI learn already lives in plugins/shared/src/midi/midi_cc_manager.*, which the controller uses (controller.cpp:34). A second, partial, unpersisted copy cannot complete a mapping. Its two automatable parameters are exposed to hosts but change nothing audible.

**Consequence.** Hosts list 'Sweep MIDI Learn' and 'Sweep MIDI CC' as automatable parameters that do nothing. The dead fallback branch in getMidiControllerAssignment, and the processor's per-block CC scan when learn is active, are maintenance noise that hides which system is authoritative.

**Evidence.** controller.cpp:714-723 `if (midiCCManager_) { ... } if (assignedMidiCC_ < 128 && midiControllerNumber == assignedMidiCC_) { id = makeSweepParamId(SweepParamType::kSweepFrequency); return kResultTrue; }`; grep assignedMidiCC_ in controller: only controller.h:435 `int assignedMidiCC_ = 128;` and controller.cpp:720; processor.cpp:409 `assignedMidiCC_ = cc;`, processor_params.cpp:532-533 `assignedMidiCC_ = static_cast<int>(value * 128.0 + 0.5);` with no reader.

**Suggested fix.** Remove the controller fallback branch and the controller assignedMidiCC_ member. Retire the processor-side assignedMidiCC_/midiLearnActive_ handling. Keep the two parameter IDs registered but mark them hidden/non-automatable for host compatibility (never reuse the IDs). Test: getMidiControllerAssignment returns kResultFalse for every CC unless MidiCCManager has a mapping.

**Verifier notes:**
- Correctness: the controller member is declared at controller.h:435 and read only at controller.cpp:720, so the fallback can never fire; the processor's assignedMidiCC_ is write-only (processor.h:240, processor.cpp:409, processor_params.cpp:532-533); kSweepDetectedCCOutputId is written at processor.cpp:403 and consumed nowhere; neither field is persisted; the two parameters are registered kCanAutomate at parameter_registration.cpp:345-352 with no uidesc control (tags 3664/3665 absent).
- Context: the authoritative system is MidiCCManager plus kGlobalMidiLearnActive/Target (controller.cpp:1412, :1626, :1642); all MIDI tests exercise only MidiCCManager; no spec note keeps the legacy path.
- Impact: the per-block scan cost is close to zero because processor.cpp:65 adds no event input and the loop filters on kLegacyMIDICCOutEvent; host-list clutter and maintenance noise, no audio effect; low.

### D-093 — Distortion type names maintained in three production tables plus a fourth, differently ordered test copy that asserts against itself

**Severity:** low | **Verdict:** CONFIRMED | **Category:** duplication | **Dimension:** duplication-drift | **Also flagged by:** types inventory M2 | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/parameter_registration.cpp:774-799](plugins/disrumpo/src/controller/parameter_registration.cpp#L774-L799), [:847-856](plugins/disrumpo/src/controller/parameter_registration.cpp#L847-L856)

**Problem.** The 26 type names exist as getTypeName (distortion_types.h:213-243), as 26 appendString calls for the Displayed Type proxy (parameter_registration.cpp:774-799) and as kDistortionTypeNames for the node Type params (:847-856). tests/unit/band_strip_test.cpp:44-55 keeps a fourth local array, kCanonicalDistortionTypes, in D-number order (Temporal at 14, Aliasing at 17, Allpass Resonant at 25). Its REQUIREs (:161-163) check that local array, not any shipped table.

**Why wrong.** The menu index is decoded as a DistortionType (processor_params.cpp:840 round(v*25)), so every list must stay in enum order. One generator (loop over DistortionType calling getTypeName) removes the risk. The test is tautological and pins an order the product does not use.

**Consequence.** Adding or reordering a type requires editing three production tables in step. The test gives false assurance, since it cannot fail on a menu or enum mismatch.

**Evidence.** distortion_types.h:216 `case DistortionType::SoftClip: return "Soft Clip";` ...; parameter_registration.cpp:774 `displayedTypeParam->appendString(STR16("Soft Clip"));` ... :799; :847-856 `static const Steinberg::Vst::TChar* kDistortionTypeNames[] = { STR16("Soft Clip"), ...`; band_strip_test.cpp:161-163 `REQUIRE(kCanonicalDistortionTypes[25] == "Allpass Resonant");` vs enum AllpassResonant = 19.

**Suggested fix.** Fill both StringListParameters by looping i over 0..COUNT-1 with getTypeName(static_cast<DistortionType>(i)). Rewrite band_strip_test to compare the registered Displayed Type and Node Type param strings against getTypeName per enum value.

**Verifier notes:**
- Correctness: three production copies (distortion_types.h:214-243, parameter_registration.cpp:774-799, :847-856 used at :907) are all in enum order today; the test array actually spans band_strip_test.cpp:29-56 (the finding cites only the differing entries :44-55) and its REQUIREs at :131/:141-163 index only that local array; the comments "Index 25 (Allpass Resonant)" at :217/:242 are wrong for the product (index 25 is Stochastic, Allpass Resonant is 19).
- Context: nothing generates the tables from getTypeName (used only in tests and its own definition), and no test reads the registered StringListParameter strings; the test header comment (:9) claims coverage it does not have.
- Impact: no wrong label today; the risk is future drift that would show the wrong label for the type that plays while the test still passes; low.

### D-092 — Note-value dropdown table re-typed three times instead of using the shared note_value_ui.h, with a local kNoteValueCount copy of the DSP constant

**Severity:** low | **Verdict:** CONFIRMED | **Category:** duplication | **Dimension:** duplication-drift | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/parameter_registration.cpp:274-279](plugins/disrumpo/src/controller/parameter_registration.cpp#L274-L279), [:385-390](plugins/disrumpo/src/controller/parameter_registration.cpp#L385-L390), [:421-426](plugins/disrumpo/src/controller/parameter_registration.cpp#L421-L426)

**Problem.** Sweep LFO Note, LFO 1 Note Value and LFO 2 Note Value each append a hand-typed 30-string initializer list. plugins/shared/src/ui/note_value_ui.h already provides Krate::Plugins::Parameters::kNoteValueDropdownStrings, keyed to Krate::DSP::kNoteValueDropdownMapping. plugin_ids.h:704 also redeclares `constexpr int kNoteValueCount = 30;` rather than using Krate::DSP::kNoteValueDropdownCount (note_value.h:124). That constant is what processor_params.cpp:458/597/629 and the state codecs decode with. These lists also never set the shared default index (kNoteValueDefaultIndex = 10, 1/8), so the lists default to index 0 (1/64T).

**Why wrong.** The list order is a contract with Krate::DSP::getNoteValueFromDropdown. Iterum and other plugins consume the shared table, so a change to the shared dropdown would silently desync Disrumpo's three copies and its local count.

**Consequence.** Today's strings match. A future edit to the shared dropdown (add or reorder entries) would make Disrumpo's labels name the wrong subdivisions, and its decode use a stale count, with no compile error.

**Evidence.** parameter_registration.cpp:274-279 `for (const auto* n : {"1/64T","1/64","1/64D", ... ,"4/1D"}) lfoNoteParam->appendString(Steinberg::String(n));` (repeated at :385 and :421); plugin_ids.h:703-704 `/// Number of entries in the standard note value dropdown (from DSP layer). constexpr int kNoteValueCount = 30;`; shared/src/ui/note_value_ui.h:21 `inline const Steinberg::Vst::TChar* const kNoteValueDropdownStrings[] = {`.

**Suggested fix.** Build the three lists from kNoteValueDropdownStrings and set the default with kNoteValueDefaultIndex. Replace plugin_ids.h kNoteValueCount with `using Krate::DSP::kNoteValueDropdownCount` or a static_assert tying the two together. A compile-time static_assert(kNoteValueCount == Krate::DSP::kNoteValueDropdownCount) catches drift.

**Verifier notes:**
- Correctness: the three hand-typed lists (:274-279, :385-390, :421-426), the local constant (plugin_ids.h:703-704) and the shared table (note_value_ui.h:21, "Order MUST match kNoteValueDropdownMapping") are as described; the local count drives processor_params.cpp:458/:597/:629, processor_state.cpp:524/:604/:640 and controller.cpp:262/:314/:342/:1743/:1757/:1764 while processor_state.cpp:119-122/:177-180/:209-212 use the DSP constant, so encode and decode already read their count from different places; no static_assert ties them.
- Context: Iterum, Gradus and Innexus use kNoteValueDropdownStrings (gradus arpeggiator_params.h:11, innexus controller.cpp:8, iterum bbd_params.h:12), Disrumpo does not; none of the three StringListParameters sets a default, so the controller default is index 0 (1/64T) against kNoteValueDefaultIndex 10 and the SweepLFO's own Quarter default (sweep_lfo.h:252).
- Impact: strings and count match the DSP table today, so labels and audio are right; a latent drift hazard plus a small UI default mismatch (a host "reset to default" jumps to 1/64T); low.

### D-091 — Mod and sweep denormalisations are written twice (params TU and state TU) as literals, including nine hand-rounded copies of ln(0.01)/ln(20)

**Severity:** low | **Verdict:** CONFIRMED | **Category:** duplication | **Dimension:** duplication-drift | **Also flagged by:** portability-quality | **Corroborated by measurement:** partly (preset-rt-bytes probe: first unstable byte at offset 92, the sweep LFO rate field)

**Location:** [plugins/disrumpo/src/processor/processor_state.cpp:103-105](plugins/disrumpo/src/processor/processor_state.cpp#L103-L105), [:163-164](plugins/disrumpo/src/processor/processor_state.cpp#L163-L164), [:195-196](plugins/disrumpo/src/processor/processor_state.cpp#L195-L196), [:226-278](plugins/disrumpo/src/processor/processor_state.cpp#L226-L278), [:506-508](plugins/disrumpo/src/processor/processor_state.cpp#L506-L508), [:583-584](plugins/disrumpo/src/processor/processor_state.cpp#L583-L584), [:619-620](plugins/disrumpo/src/processor/processor_state.cpp#L619-L620), [:655-731](plugins/disrumpo/src/processor/processor_state.cpp#L655-L731)

**Problem.** Every mod-source and sweep range is encoded once in processParameterChanges and again, as an inverse, in getState/setState, each with literal constants. Examples: env attack 1+v*99 (params :643, state :226 and :655), release 10+v*490, random/S&H/rungler rate 0.1+v*49.9, LFO phase v*360. The LFO-rate log mapping appears as `constexpr float kMinRateLog = -4.6052f; kMaxRateLog = 2.9957f;` nine times: processor_params.cpp:431-432, 579-580, 611-612 and processor_state.cpp:103-104, 163-164, 195-196, 506-507, 583-584, 619-620.

**Why wrong.** A range belongs in one place. The four-decimal constants are not ln(0.01) = -4.605170 and ln(20) = 2.995732, so the copies agree with each other only because they were all typed identically, not because they derive from the LFO's real range. The registration side (parameter_registration.cpp:364-365, 400-401) uses linear RangeParameters for the same params, which is the drift this layout invites.

**Consequence.** A future range change made in only one of the two TUs silently corrupts session round-trips. This is the same failure class as the preset-writer drift. Today the copies are consistent; the cost is maintenance and the masked linear-vs-log registration mismatch.

**Evidence.** processor_params.cpp:431-433 `constexpr float kMinRateLog = -4.6052f;  // ln(0.01) ... exp(kMinRateLog + value*(kMaxRateLog-kMinRateLog))`; processor_state.cpp:105 `(std::log(sweepLFO_.getRate()) - kMinRateLog) / (kMaxRateLog - kMinRateLog)`; params :643 `float ms = 1.0f + value * 99.0f;` vs state :226 `(getEnvFollowerAttack() - 1.0f) / 99.0f` and :655 `1.0f + envAttackNorm * 99.0f`; grep shows the -4.6052/2.9957 literals only in Disrumpo.

**Suggested fix.** Add one header (e.g. dsp/param_ranges.h in the plugin) with inline toNorm/fromNorm helpers per range, using std::log(0.01f)/std::log(20.0f) computed once. Call them from processor_params.cpp, processor_state.cpp and the registration. Test: for each mod param, fromNorm(toNorm(x)) == x at the endpoints and midpoint, and processParameterChanges(v) followed by getState writes v back within 1e-5.

**Verifier notes:**
- Correctness: grep for `4\.6052|2\.9957` finds exactly the nine block-local constexpr pairs cited; the linear ranges are written in both directions (attack :643 vs state :226/:655, release :648 vs :228/:658, rates :664/:699/:746 vs :236/:254/:278/:670/:697/:731, phase :591/:623 vs :170/:202/:594/:630); parameter_registration.cpp:363-365/:399-401 register LFO 1/2 Rate as linear RangeParameter(0.01, 20) while the processor maps logarithmically (at normalized 0.5 the host would display about 10 Hz while the engine runs about 0.45 Hz).
- Context: the sweep envelope already uses named constants on both sides (processor_params.cpp:473-482, processor_state.cpp:134-138), so the mod-param literals are an inconsistency rather than a design choice; no test runs param -> getState -> setState for these IDs.
- Impact: the copies agree today and rounding ln(0.01) to -4.6052 moves the 0.01 Hz endpoint by about 3e-5 relative; the linear-vs-log registration mismatch is a separate, more visible defect carried by the params inventory; low.

### D-090 — Shape-slot display formatter re-implements the processor/adapter slot mappings and has already drifted (RingSat Hz in Harmonic mode, Bitcrush shows 1-3 bits)

**Severity:** low | **Verdict:** CONFIRMED | **Category:** duplication | **Dimension:** duplication-drift | **Also flagged by:** D-083, D-102 (Bitcrush dead zone) | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/format_helpers.h:86-135](plugins/disrumpo/src/controller/format_helpers.h#L86-L135)

**Problem.** formatShapeSlot hand-copies the denormalisations from mapShapeSlotsToParams (processor_params.cpp) and routeParamsToProcessor (distortion_adapter.cpp). Each copy carries its own literals, and the formatter has diverged from the DSP. RingSat slot 6 always prints `20*250^v Hz`, but the adapter treats it as a harmonic ratio 1+round(v*15) when the freq mode (slot 5) is Harmonic (distortion_adapter.cpp:803-806). Bitcrush slot 0 prints 1.0..16.0 bit, but BitCrusher clamps to kMinBitDepth = 4 (bit_crusher.h:50,152), so 1-3.9 bit are shown but never applied. The 9-entry waveshape name table is also duplicated inside the same function (lines 52-55 and 94-97).

**Why wrong.** The text a host or slider shows must be produced by the same mapping the DSP applies. Today the two mappings live in separate TUs with no shared definition.

**Consequence.** In RingSat Harmonic mode, host automation lanes and the value readout show e.g. '354 Hz' while the carrier runs at ratio 8. The bottom 20 % of the Bitcrush Bits knob reads 1-3.9 bit and sounds identical to 4 bit. Any future range edit on one side silently desyncs the readout again.

**Evidence.** format_helpers.h:111-118 `if (slot == 6) { float freqHz = 20.0f * std::pow(250.0, v); ... " Hz" }`; :123-126 `floatToString128(1.0 + v * 15.0, 1, string); ... " bit"`; distortion_adapter.cpp:803-806 `else if (freqMode == 1) { float ratio = 1.0f + std::round(p.rsCarrierFreq * 15.0f); ringSaturation_.setHarmonicRatio(ratio); }`; bit_crusher.h:50 `kMinBitDepth = 4.0f`.

**Suggested fix.** Move the per-type slot ranges into one constexpr descriptor table, e.g. in distortion_types.h, giving min/max/curve/unit per (type, slot). Have mapShapeSlotsToParams and formatShapeSlot both read it. The RingSat slot-6 formatter needs the slot-5 value: format from the node's shape slots, not the type alone. Test: for each type and slot, parse the formatter's numeric text and compare it with the denormalized DistortionParams field.

**Verifier notes:**
- Correctness: the formatter gets only (distType, slot, v) from controller.cpp:775 and :838 and never sees slot 5, while the adapter applies Hz only for freqMode 0 and a ratio for freqMode 1 (distortion_adapter.cpp:794-807), and sub_controllers.h:491-497 keeps the knob visible in Harmonic mode; the Bitcrush readout (format_helpers.h:124) matches processor_params.cpp:131 and both disagree with the primitive's clamp (bit_crusher.h:50/:152 via distortion_adapter.cpp:709/:467), so for Bitcrush the mismatch is mapping-vs-primitive rather than formatter drift; the duplicated waveshape table at :52-55 and :94-97 is real.
- Context: the processor's own comment calls slot 6 "Hz/Ratio" (processor_params.cpp:167), so the dual meaning is intended but unhandled by the formatter; no test pins formatter output against the DSP mapping.
- Impact: wrong text in automation lanes and readouts plus a dead 20 % knob range; no audio, crash or state risk; low.

### D-084 — AllpassSaturator::process reconfigures 9 biquads/stages (trig) every sample regardless of the active topology

**Severity:** low | **Verdict:** CONFIRMED | **Category:** shared-dsp-contract | **Dimension:** shared-dsp-contracts | **Corroborated by measurement:** no | **CROSS-PLUGIN:** shared header

**Location:** [dsp/include/krate/dsp/processors/allpass_saturator.h:733-752](dsp/include/krate/dsp/processors/allpass_saturator.h#L733-L752)

**Problem.** Every sample, process() calls singleStage_.setFrequency, configures all 4 chain allpass biquads (sin/cos), and sets 4 matrix stages' frequency/drive, even when the smoothed frequency is constant and only one topology is in use. Disrumpo calls it per sample per channel (via the shared instance) per node.

**Why wrong.** Coefficient recomputation should happen only when the smoothed frequency changed, and only for the active topology.

**Consequence.** About 9 coefficient designs (each with trig) per sample per AllpassResonant node, so 4 bands x 4 nodes in cross-family mode costs a large CPU share for no audible benefit.

**Evidence.** allpass_saturator.h:734-735 singleStage_.setFrequency/setDrive; :738-742 chainAllpasses_[i].configure(...) loop; :749-752 matrixStages_ loop, all before the topology switch at :756

**Suggested fix.** Cache the last smoothed frequency and drive, and reconfigure only the active topology when they change. Test: add a CPU-budget case in the [performance] lane, plus a bit-identity case showing output is unchanged.

**Verifier notes:**
- Correctness: process() (:715-778) advances the smoothers (:728-730) and then runs 9 Biquad::configure calls per sample (:734 via :469-472, :738-742, :749-752) before the topology switch at :756; Biquad::configure (biquad.h:330-337) calls BiquadCoefficients::calculate with std::sin/std::cos at :677-678, so 18 trig calls per sample; Disrumpo reaches it at distortion_adapter.cpp:546-548.
- Context: no CPU or performance test exists in dsp/tests/unit/processors/allpass_saturator_tests.cpp, and no comment says per-sample reconfiguration is intended; the setters at :822-843 only update smoother targets.
- Impact: CPU only (one verifier's rough estimate 2-3 us per 22.7 us sample period per adapter at 44.1 kHz, unmeasured); the output is unchanged and nothing allocates; "4 bands x 4 nodes" overstates because four AllpassResonant nodes are same-family and collapse into one blendedAdapter_ (morph_engine.cpp:718), so cross-family allows at most 3 per band; low.

### D-083 — Bitcrush maps Bits to [1,16] but BitCrusher clamps to [4,16]: the bottom 20 % of the knob is a dead zone

**Severity:** low | **Verdict:** CONFIRMED | **Category:** shared-dsp-contract | **Dimension:** shared-dsp-contracts | **Also flagged by:** D-090, D-102 | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor_params.cpp:131](plugins/disrumpo/src/processor/processor_params.cpp#L131)

**Problem.** Slot 0 maps to bitDepth = 1 + slot*15, and the jitter path clamps to [1,16] (distortion_adapter.cpp:463). BitCrusher::setBitDepth clamps to [kMinBitDepth 4, 16].

**Why wrong.** The plugin mapping must match the primitive's accepted range, or the primitive must support 1-3 bits.

**Consequence.** Bits 1-4 sound identical, so the first 20 % of the knob and of jitter excursions below 4 bits do nothing. setBitDepth also runs std::pow per sample (distortion_adapter.cpp:467).

**Evidence.** processor_params.cpp:131 `p.bitDepth = 1.0f + slots[0] * 15.0f;`; bit_crusher.h:50 kMinBitDepth = 4.0f; :152 clamp(bits, kMinBitDepth, kMaxBitDepth); distortion_adapter.cpp:463 clamp(…, 1.0f, 16.0f)

**Suggested fix.** Map slot 0 to [4,16] (and clamp jitter to 4), or lower BitCrusher's minimum (a shared header: check consumers). Test: Bitcrush outputs at slot 0.0 and 0.15 must differ.

**Verifier notes:**
- Correctness: processor_params.cpp:131 -> distortion_adapter.cpp:709 and the per-sample jitter path :454-467 -> BitCrusher::setBitDepth (bit_crusher.h:151-154) clamps to kMinBitDepth 4 (:50), so slot values 0-0.2 all give 4 bits and jitter never goes below 4; distortion_adapter_test.cpp:921 even comments "Slot0 = 0.0 -> bitDepth = 4" while its helper at :907 maps 0.0 to 1.0; setBitDepth calls updateQuantizationLevels with std::pow every sample.
- Context: specs-overview.md:218, ui-mockups.md:474/:486 and dsp-details.md:213 all promise 1-16; the tests at distortion_adapter_test.cpp:794/:870 and morph_band_integration_test.cpp:370 use 2.0f without checking 1-3-bit behaviour; nothing marks the clamp as intended.
- Impact: lost resolution, one-sided jitter near the bottom, no NaN or click; the node-level kNodeBitDepth mapping (processor_params.cpp:878, 4-24) hits the same primitive's 16-bit ceiling, so its top 40 % is dead too; low.

### D-082 — Bipolar node Bias and the bias mod offset are written into the field FuzzProcessor treats as unipolar, so the negative half pins Fuzz at its most-gated bias

**Severity:** low | **Verdict:** CONFIRMED | **Category:** shared-dsp-contract | **Dimension:** shared-dsp-contracts | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor_params.cpp:868-871](plugins/disrumpo/src/processor/processor_params.cpp#L868-L871)

**Problem.** kNodeBias is denormalized to [-1,1] into mn.params.bias. Fuzz/AsymFuzz slot 0 writes a unipolar [0,1] value into the same field (:73, :81). MorphEngine adds biasModOffset_*2 and clamps to [-1,1] (morph_engine.cpp:708, :735). FuzzProcessor::setBias clamps to [0,1], where 0 means the maximum 0.8 offset.

**Why wrong.** The FuzzProcessor contract is a unipolar bias (fuzz_processor.h:364-365, 1 = centred). A bipolar source must be remapped per type before it reaches setBias, as Tube/Tape already do in the opposite direction.

**Consequence.** For a Fuzz node, the bottom half of the Bias automation range, and any negative bias modulation, all produce the same most-gated sputter. A bias mod route centred on the base value is lopsided.

**Evidence.** processor_params.cpp:870 `mn.params.bias = static_cast<float>(value) * 2.0f - 1.0f;`; :73 `p.bias = slots[0];  // Fuzz bias is unipolar [0,1]`; fuzz_processor.h:365 `bias_ = std::clamp(bias, 0.0f, 1.0f);`; morph_engine.cpp:708 bias + biasModOffset_*2 clamp [-1,1]

**Suggested fix.** Remap the bias per destination type in the adapter (Fuzz: (b+1)/2), or keep separate fields per type. Test: a Fuzz node with kNodeBias norm 0.0 vs 0.25 must give different output RMS.

**Verifier notes:**
- Correctness: processor_params.cpp:868-870 writes value*2-1 with no per-type remap, :73/:81 write the unipolar Fuzz slot into the same field (Tube/Tape remap to bipolar at :56/:63), morph_engine.cpp:709/:735 and band_processor.h:449 add biasModOffset_*2 and clamp [-1,1], distortion_adapter.cpp:637/:646 pass p.bias straight to fuzz_.setBias, which clamps [0,1] (fuzz_processor.h:364-365) with offset (1-bias)*0.8 at :490-492; so kNodeBias normalized 0-0.5, including its registered default, collapses to the most-gated Fuzz setting.
- Context: kNodeBias is registered automatable (parameter_registration.cpp:930-934) but no editor.uidesc view binds Band.NodeBias, so it is reached through host automation and generic editors; the Fuzz panel drives bias through Band.NodeShape0; processor_state.cpp:334/:859 round-trips the field raw, so save/load does not trigger it.
- Impact (one verifier): the "lopsided mod route" claim is wrong because clamping around a centred base is symmetric; the real effect is double sensitivity (offset +/-0.25 sweeps the whole Fuzz range); reachable only through a parameter with no UI knob; low.

### D-073 — Host text for node shape slots is formatted with the band's displayed (selected) node type, not the owning node's type

**Severity:** low | **Verdict:** CONFIRMED | **Category:** ui-wiring | **Dimension:** ui-wiring | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/controller.cpp:765-777](plugins/disrumpo/src/controller/controller.cpp#L765-L777)

**Problem.** getParamStringByValue for any node kNodeShape0..9 looks up the band's kBandDisplayedType, which is the selected node's type, and formats the slot with it. It should look up the type of the node encoded in the parameter ID.

**Why wrong.** The shape-slot meaning depends on the node's own type (mapShapeSlotsToParams uses mn.type); a parameter's string must not depend on which node happens to be selected in the UI.

**Consequence.** Host automation lanes and generic editors show wrong units and labels for nodes B-D whenever their type differs from the selected node's, for example a Tube node's slot shown as a Bitcrush depth.

**Evidence.** controller.cpp:768-773 `auto displayedTypeId = makeBandParamId(band, BandParamType::kBandDisplayedType); ... int typeIdx = static_cast<int>(dtParam->getNormalized() * 25.0 + 0.5);`

**Suggested fix.** Read makeNodeParamId(band, extractNode(id), kNodeType) instead of DisplayedType. Test: give node B a different type from the selected node A, and assert that getParamStringByValue for B's slot uses B's formatter.

**Verifier notes:**
- Correctness: controller.cpp:765-777 extracts only the band (:768) and reads kBandDisplayedType (:769-773), a proxy that "mirrors selected node's type" (plugin_ids.h:193) synced from the selected node's kNodeType (controller.cpp:595-606, visibility_controllers.cpp:369); the processor maps slots with the owning node's type (processor_params.cpp:852/:892).
- Context: every node shape slot is registered host-visible and automatable as "B<b> N<n> Shape <s>" (parameter_registration.cpp:948-963), so hosts call getParamStringByValue for unselected nodes; the band-proxy branch at :826-842 correctly uses DisplayedType, and copying it into the node branch is the cause; no test pins node shape-slot strings.
- Impact: display-only; audio, state and normalized values are correct and the plugin's own editor edits through the proxies; low.

### D-072 — StringListParameter defaults set with setNormalized(), so the host 'default' for Band Count and Oversample Max is 1 band / 1x

**Severity:** low | **Verdict:** CONFIRMED | **Category:** ui-wiring | **Dimension:** ui-wiring | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/parameter_registration.cpp:76](plugins/disrumpo/src/controller/parameter_registration.cpp#L76), [:90](plugins/disrumpo/src/controller/parameter_registration.cpp#L90)

**Problem.** bandCountParam->setNormalized(1.0) and oversampleParam->setNormalized(2/3) change only the current value. StringListParameter's constructor fixes info.defaultNormalizedValue = 0 (vstparameters.cpp:235), and VST3Editor passes that 0 to the control as its default value (vst3editor.cpp:270, 287).

**Why wrong.** The intended defaults (4 bands, 4x) must be in ParameterInfo.defaultNormalizedValue for host reset, for control default-click and for hosts that initialise from getParameterInfo.

**Consequence.** A host 'reset to default' or a control's default gesture collapses the plugin to 1 band and 1x oversampling.

**Evidence.** parameter_registration.cpp:76 `bandCountParam->setNormalized(1.0);` ; :90 `oversampleParam->setNormalized(2.0 / 3.0);` ; vstparameters.cpp:235 `info.defaultNormalizedValue = 0;`

**Suggested fix.** Set getInfo().defaultNormalizedValue as well (as other plugins do), or use a small subclass. Test: assert that getParameterInfo(BandCount).defaultNormalizedValue == 1.0.

**Verifier notes:**
- Correctness: parameter_registration.cpp:65-77 and :80-91 use the StringListParameter constructor that hard-codes `info.defaultNormalizedValue = 0` (extern/vst3sdk/public.sdk/source/vst/vstparameters.cpp:225-239, line 235) and then call only setNormalized, which never touches the default; vst3editor.cpp:270/:287 pass getInfo().defaultNormalizedValue to c->setDefaultValue; grep of plugins/disrumpo for defaultNormalizedValue finds nothing.
- Context: Iterum (controller.cpp:200), Innexus (controller.cpp:471/:532) and Profundum (global_params.h:149/:156, morph_mod_params.h:270) set getInfo().defaultNormalizedValue explicitly, so Disrumpo is the outlier; Spectrum Mode at :103 has the same pattern but its intended default is 0; global_controls_test.cpp:94-107/:218-228 check only the IDs.
- Impact: limited to host reset-to-default, control default gestures and hosts that seed from ParameterInfo; a reset collapses to 1 band (audible) and 1x; normal load and audio are unaffected; low.

### D-059 — Controller::setState allocates a vector sized by an unvalidated int32 read from the stream

**Severity:** low | **Verdict:** CONFIRMED | **Category:** lifecycle-state | **Dimension:** lifecycle-state | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/controller.cpp:690-697](plugins/disrumpo/src/controller/controller.cpp#L690-L697)

**Problem.** midiDataSize is read from the stream, and the code then calls std::vector<uint8_t>(midiDataSize) with no upper bound before reading.

**Why wrong.** Corrupted or hostile controller state must not be able to trigger a multi-GB allocation, or a std::bad_alloc thrown across the VST3 ABI boundary (exceptions must not leave plugin entry points).

**Consequence.** A corrupt controller-state chunk with midiDataSize near 2^31 throws bad_alloc out of setState and can crash the host while it loads the project.

**Evidence.** controller.cpp:690-692 `Steinberg::int32 midiDataSize = 0; if (streamer.readInt32(midiDataSize) && midiDataSize > 0) { std::vector<uint8_t> midiData(static_cast<size_t>(midiDataSize));`

**Suggested fix.** Clamp midiDataSize to a sane maximum, e.g. 4 + 128*6 bytes for 128 CC mappings, and reject the chunk otherwise. Test: a controller setState with midiDataSize = 0x7FFFFFFF returns without throwing.

**Verifier notes:**
- Correctness: Controller::setState (controller.cpp:679-701) checks only `version >= 2` and `midiDataSize > 0` before `std::vector<uint8_t> midiData(midiDataSize)` at :692; no try/catch exists in any plugin controller*.cpp and the SDK adds none, so a bad_alloc would leave the PLUGIN_API entry point; the matching writer at :664-671 only ever writes serializeGlobalMappings().size().
- Context: the trigger needs a corrupt or hand-crafted controller chunk, which normal saves cannot produce; a smaller issue on the same path: `state->read` at :693 passes nullptr for numBytesRead, so a short read is not detected and a partly zero-filled buffer still reaches deserializeGlobalMappings.
- Impact: on a 64-bit host the 2 GiB allocate-and-zero usually succeeds (a commit spike) and bad_alloc appears only when memory is tight or on 32-bit; UI thread at load, cheap clamp; local to Disrumpo; low.

### D-024 — Allpass compensation jumps instantly while the LR4 crossover glides over 5 ms: reconstruction error on every crossover move

**Severity:** low (reported: medium; lowered by all three verifiers) | **Verdict:** CONFIRMED | **Category:** signal-path | **Dimension:** signal-path | **Corroborated by measurement:** yes (probe `crossover:network:startup-ramp:bands=4`)

**Location:** [plugins/disrumpo/src/dsp/crossover_network.h:79-94](plugins/disrumpo/src/dsp/crossover_network.h#L79-L94), [:132-150](plugins/disrumpo/src/dsp/crossover_network.h#L132-L150)

**Problem.** setCrossoverFrequency reconfigures the D'Appolito Biquad allpasses straight to the target frequency (:142-148), but CrossoverLR4::setCrossoverFrequency only sets a OnePoleSmoother target (crossover_filter.h:218-221), and the LR4 coefficients follow over the 5 ms default. kDefaultSmoothingMs = 10 in CrossoverNetwork (:49) is never applied. In prepare(), crossover.prepare() snaps each LR4 to its previously stored frequency (default 1000 Hz), and initializeLogarithmicDistribution only sets new targets, so after every setupProcessing the LR4s glide from 1 kHz while the allpasses already sit at the final frequencies.

**Why wrong.** Flat reconstruction (SC-001, +/-0.1 dB) needs the allpass in each lower band to match the LR4 at that crossover at every instant. During the glide, the low band's AP(f_target) and the higher split's LR4(f_current) differ, so the sum is not allpass.

**Consequence.** Short magnitude ripples or zipper colour while a crossover divider is dragged or automated, since each block's new target restarts a mismatched 5 ms window. There is also a brief non-flat transient at every (re)start.

**Evidence.** crossover_network.h:134 `crossovers_[index].setCrossoverFrequency(clamped);` then :142-148 `allpasses_[band][apIdx].configure(... clamped ...)`; crossover_filter.h:221 `frequencySmoother_.setTarget(clamped);`, :104 kDefaultSmoothingMs = 5.0f, :179 snapTo(stored freq) in prepare; crossover_network.h:49 kDefaultSmoothingMs unused.

**Corroborated by measurement.** probe crossover:network:startup-ramp:bands=4: "max_mag_dev_dB,at_Hz = 12.346,80". Settled reconstruction is fine: crossover:network:bands=2/3/4 "0.000 / 0.002 / -0.004". Code: crossover_network.h:84 `crossover.prepare(sampleRate);`, :266 `crossovers_[i].setCrossoverFrequency(crossoverFrequencies_[i]);`

**Suggested fix.** Smooth the allpass frequency with the same smoother and timing as its LR4 (update allpass coefficients per sample from crossovers_[i]'s current smoothed frequency, at the same 0.1 Hz hysteresis), or disable the LR4 smoother and smooth the frequency once in the network for both. In prepare, snap the LR4 smoothers to the distribution frequencies. Test: 3 bands, white noise, sweep crossover 1 from 500 Hz to 2 kHz in one block. Assert the summed output's STFT magnitude stays within 0.5 dB of the input in every 256-sample frame.

**Verifier notes:**
- Correctness: the mechanism holds (crossover_network.h:127-151 snaps the allpasses while :134 only sets an LR4 smoother target, crossover_filter.h:218-222/:289-312 with 5 ms at :104 and 0.1 Hz hysteresis at :106; :49 kDefaultSmoothingMs is never referenced; prepare (:79-94) snaps to the stored 1000 Hz at crossover_filter.h:178-179 and initializeLogarithmicDistribution (:253-269) only sets targets while updateAllpassCoefficients (:380-397) snaps); both the automation path (processor_params.cpp:984-994) and the state load (processor_state.cpp:455-456) go through this setter; every crossover_network_test.cpp case settles before measuring.
- Context: specs/Disrumpo has no mention of crossover smoothing or allpass glide; only crossover index >= 1 has a matching allpass (:138 `index > 0`), so the mismatch exists only with 3-4 bands when divider 2 or 3 moves.
- Impact (all three lowered): under smooth automation the lag is rate x tau (about 0.001 dB at 1 octave/s); a typical step gives about -0.04 dB decaying within 5 ms; only a worst-case instant jump (1 kHz with f1 jumping 1.5 to 6 kHz) gives about -3 dB near 1 kHz for 5 ms; the start-up transient falls on stream start; a brief, small, local transient rather than a sustained SC-001 violation; low.

### D-015 — LFO symmetry path rounds phase to 1.0f and reads wavetable[2048] out of bounds (shared lfo.h; Ruinae reaches it, Disrumpo does not)

**Severity:** low | **Verdict:** CONFIRMED | **Category:** dsp-numerics | **Dimension:** dsp-numerics | **Corroborated by measurement:** no | **CROSS-PLUGIN:** Ruinae (setLFO*Symmetry callers)

**Location:** [dsp/include/krate/dsp/primitives/lfo.h:122-126](dsp/include/krate/dsp/primitives/lfo.h#L122-L126), [:435-445](dsp/include/krate/dsp/primitives/lfo.h#L435-L445), [:523-528](dsp/include/krate/dsp/primitives/lfo.h#L523-L528)

**Problem.** When symmetry_ != 0.5, process() casts the double effectivePhase to float before applySymmetry. Any phase in [1-2^-25, 1) rounds to 1.0f, and applySymmetry(1.0f, s) returns 0.5+0.5*((1-s)/(1-s)) = 1.0. readWavetable then computes index0 = size_t(1.0*2048) = 2048 and reads table[2048] on a std::array<float,2048> (index1 is masked, index0 is not).

**Why wrong.** The lookup phase must stay in [0,1). The float cast at the wrap edge is exactly the IEEE boundary case the project rules require clamping (exactness by rounding). std::array::operator[] past the end is undefined behaviour.

**Consequence.** Rare out-of-bounds reads (the next table's first value, or past the 4-table set for the last waveform) produce a one-sample LFO glitch, or UB under sanitizers. CROSS-PLUGIN: Ruinae calls setLFO*Symmetry (ruinae_engine.h, processor_params.cpp). Disrumpo never sets symmetry, so it keeps 0.5 and skips the branch.

**Evidence.** lfo.h:124-126 `lookupPhase = static_cast<double>(applySymmetry(static_cast<float>(effectivePhase), symmetry_));`. :440 `size_t index0 = static_cast<size_t>(scaledPhase);` :441 only index1 is masked. :38 kTableSize = 2048; :374 std::array<std::array<float,kTableSize>,4>. :527 `return 0.5f + 0.5f * ((phase - symmetry) / (1.0f - symmetry));`. grep shows setSymmetry is called from plugins/ruinae/src/engine/ruinae_engine.h and not from any Disrumpo processor code.

**Suggested fix.** Keep applySymmetry in double, or clamp lookupPhase to std::nextafter(1.0, 0.0) (or mask index0 with kTableMask) in readWavetable. Test: an LFO with symmetry 0.3, phase offset chosen so the effective phase is 1 - 1e-8; assert index0 < kTableSize (an ASan build would flag the read).

**Verifier notes:**
- Correctness: lfo.h:118 wraps in double, :122-126 narrows to float when symmetry_ != 0.5 (setSymmetry clamps to [0.001,0.999] at :300-301), applySymmetry (:523-528) returns 1.0f for phase 1.0f (and even for 1-2^-24 with s = 0.3 via round-to-even), and readWavetable (:435-446) leaves index0 unmasked at :440-441 on std::array<float,2048> (:38, :374); the window is about 3e-8 per sample, so at 48 kHz the read happens roughly every 5-10 minutes for Triangle/Square with symmetry != 0.5.
- Context: Ruinae reaches it (processor_params.cpp:443/:460 -> ruinae_engine.h:494/:505 -> modulation_engine.h:418/:436), Disrumpo never sets LFO symmetry (its only symmetry calls are wavefolder ones at distortion_adapter.cpp:659-687); lfo_test.cpp:1434-1501 tests only the setter, clamp, 0.5 identity and triangle skew; wrapPhase of a tiny negative double can also return exactly 1.0 even at symmetry 0.5, so masking index0 is the better fix.
- Impact: Sine reads Triangle[0] = 0 and Sawtooth reads Square[0] = +1 (inaudible), Triangle reads Sawtooth[0] = -1 (a one-sample full-depth spike), Square reads past the whole table set (UB; MSVC Debug checked operator[] aborts); Ruinae only; low.

### Low, PLAUSIBLE (one refute vote)

### D-006 — Each node-parameter change re-pushes all 4 nodes and re-routes 4 adapters, so a preset load does about 1000 full routings inside one process() call

**Severity:** low | **Verdict:** PLAUSIBLE (one refute on consequence) | **Category:** rt-safety | **Dimension:** rt-safety | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/processor/processor_params.cpp:830-900](plugins/disrumpo/src/processor/processor_params.cpp#L830-L900)

**Problem.** Every node param (type, drive, mix, tone, bias, folds, bitDepth, each of the 10 shape slots) ends with bandProcessors_[band].setMorphNodes(cache.nodes, count) (:899). That copies the 4-node array twice (MorphEngine::nodes_ and BandProcessor::morphNodes_), calls setType+setParams+setCommonParams on all 4 adapters, recomputes the weights, and runs recalculateOversampleFactor/computeRawMorphWeights. A preset load or session restore through the controller (editParamWithNotify for all 272 node IDs) delivers every one of these in a single processParameterChanges call: 272 x 4 = 1088 routeParamsToProcessor calls, plus any Spectral re-prepares, before the first sample of that block.

**Why wrong.** The processing cost of a block should not grow with the number of queued parameter changes times the adapter count. Changes should be coalesced, for example with a per-band dirty flag that is acted on once after the parameter loop.

**Consequence.** A CPU spike and likely dropout in the block where a preset loads, worst with 4 bands. Combined with the Spectral prepare path it multiplies allocations.

**Evidence.** processor_params.cpp:898-899 `// Push updated nodes to BandProcessor  bandProcessors_[band].setMorphNodes(cache.nodes, cache.activeNodeCount);` runs after every node-param switch. band_processor.h:247-255 setMorphNodes -> morphEngine_->setNodes + morphNodes_ copy + recalculateOversampleFactor. morph_engine.cpp:131-151 per-adapter setType/setParams/setCommonParams for all 4 plus calculateMorphWeights.

**Suggested fix.** Set a per-band dirty bit in the node-param branch and call setMorphNodes once per dirty band after the parameter loop in processParameterChanges. Test: count setNodes calls (test hook or counter) for a ParameterChanges batch containing all node IDs of one band, and assert 1.

**Verifier notes:**
- Correctness: every node-param case in processor_params.cpp:830-899 falls through to setMorphNodes at :899 with no coalescing; band_processor.h:247-255 and morph_engine.cpp:131-151 do the work described; the preset-browser path (controller.cpp:120-124 -> loadComponentStateWithNotify :1876-1896 -> editParamWithNotify :1868-1874) emits 17 x 4 x 4 = 272 node IDs, so 1088 routings is accurate; "multiplies allocations" is wrong because distortion_adapter.cpp:894-896 re-prepares only when fftSize differs.
- Context: the setState path is already coalesced (processor_state.cpp:894 once per band) and so is setupActive (processor.cpp:126); only the parameter-change path lacks it; no test pins the call count; each per-change recalculateOversampleFactor can also start transient oversampling crossfades mid-load.
- Impact (REFUTE): normal processing already does more of this work every block, because processSameFamily (morph_engine.cpp:706-716) re-routes blendedAdapter_ per sample (4096 routings per 512-sample block at 4 bands and 1x), so 1088 once per preset change (about 0.1-0.5 ms) cannot cause a dropout; host session restore goes through setState and is unaffected; the real CPU issue is the per-sample re-routing (D-005), not this one.

### D-034 — 8x oversampling is unreachable even in the dormant code: the factor rounding caps at 4 while the global limit and an 8x cascade are offered; the crossfade double-clocks the shared engine

**Severity:** low | **Verdict:** PLAUSIBLE (one refute: 8x cap is spec-pinned; crossfade path dormant) | **Category:** oversampling | **Dimension:** morph-oversampling | **Also flagged by:** D-017, D-027 (oversampling not wired) | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/dsp/oversampling_utils.h:35-43](plugins/disrumpo/src/dsp/oversampling_utils.h#L35-L43)

**Problem.** roundUpToPowerOf2Factor returns at most 4, and getRecommendedOversample's maximum is 4. calculateMorphOversampleFactor and getSingleTypeOversampleFactor can therefore never yield 8, even with Oversample Max = 8x. oversampler8xInner_ is still prepared (band_processor.h:105-107) and processWithOversampling8x exists. The crossfade path (band_processor.h:653-690) runs the old and new factor through the SAME morphEngine_ and applyOutputStage smoothers, so engine state and gain/pan/mute smoothers advance twice per sample during an 8 ms transition.

**Why wrong.** FR-OS-001 offers 1/2/4/8x with the global limit acting as a cap. A limit of 8 that can never be reached is dead UI and dead memory. A crossfade between two paths must run independent state per path, or the comparison is between corrupted signals.

**Consequence.** No audible effect today, because oversampling is not wired (see the high finding). When it is wired, 8x will silently never engage and every factor change will corrupt filter and feedback state and the gain smoothers for 8 ms.

**Evidence.** oversampling_utils.h:36-42 `if (weightedAverage <= 1.0f) return 1; if (<= 2.0f) return 2; return 4;`; distortion_types.h:170-204 recommended factors are 1/2/4 only; band_processor.h:105 oversampler8xInner_.prepare; :659-663 processWithFactor called twice on the same morphEngine_; :715-716/:765-767 applyOutputStage advances gain/pan/mute smoothers per call.

**Suggested fix.** Either drop 8x from the parameter and spec, or allow a per-type 8x profile and extend the rounding. For the crossfade, keep a second engine/adapter set (or fade at the output of two independently stated paths) and advance the output-stage smoothers once per host sample. Test: oversampling_utils test asserting the factor is 8 when the limit is 8 and a node recommends 8; a crossfade test asserting the gain smoother reaches its target in the configured time during a factor change.

**Verifier notes:**
- Correctness: both halves hold in source: roundUpToPowerOf2Factor (:35-43) returns at most 4 and getRecommendedOversample (distortion_types.h:167-203) never returns 8, while "8x" is appended at parameter_registration.cpp:89, mapped at processor_params.cpp:352 and prepared at band_processor.h:105-107 with the factor==8 branch (:641-642, :729-742) unreachable; processBlockWithCrossfade (:653-690) calls processWithFactor twice (:659, :663) on the same morphEngine_/distortion_ and the same gain/pan/mute smoothers (:762-767, :478/:510).
- Context (REFUTE): the 8x cap is intentional and test-pinned: oversampling_utils.h:24-34 says the rounding maps to 1/2/4 "per spec FR-004", specs-overview.md:340-359 (FR-OS-003) lists only 1x/2x/4x profiles and FR-OS-002 (:338) defines the limit as a ceiling, and oversampling_utils_tests.cpp:54-55 and :84-86 ("limit of 8 does not force higher factors") assert exactly this; the crossfade path is unreachable because processor.cpp:487/:494 never call processBlock.
- Impact: none in the shipped plugin (the finding says so); the crossfade double-clocking is a latent defect in dormant code (about 353 samples at 44.1 kHz during which smoothers settle twice as fast and stateful types see interleaved input) that matters only when oversampling is wired in; one verifier would downgrade to informational; low retained.

### D-040 — Two sweep-morph link curve implementations disagree; the live one swaps Ease In/Out against the spec

**Severity:** low (reported: medium; lowered by two verifiers) | **Verdict:** PLAUSIBLE (one refute: two features with two specs) | **Category:** duplication | **Dimension:** sweep-modulation | **Also flagged by:** gestalt-controller; D-107 covers the same duplication | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/morph_link.cpp:44-65](plugins/disrumpo/src/controller/morph_link.cpp#L44-L65)

**Problem.** The link curves exist twice with different formulas. The live controller version (morph_link.cpp applyMorphLinkMode) uses EaseIn = sqrt(x), EaseOut = x^2, HoldRise holding until 0.5, and Stepped = floor(5x)/4 (5 levels). The DSP version (sweep_morph_link.h applyMorphLinkCurve, unit-tested but never called in the plugin) uses EaseIn = x^2, EaseOut = 1-(1-x)^2, HoldRise holding until 0.6, and Stepped = min(floor(4x),3)/3 (4 levels).

**Why wrong.** specs/Disrumpo/dsp-details.md §8.1/8.2 defines EaseIn y = x^2, EaseOut y = 1-(1-x)^2, HoldRise flat until 60 % and Stepped floor(x*4)/3. The DSP copy matches that, but the copy that runs inverts Ease In/Out and changes HoldRise and Stepped. The tests (tests/dsp/sweep_morph_link_tests.cpp) cover only the dead DSP copy, and plugin_ids.h:647 documents a third variant (0, .25, .5, .75, 1).

**Consequence.** Users choosing 'Ease In' get the spec's Ease Out shape (fast start) and the reverse. Hold-Rise starts rising at a different sweep position, and Stepped has 5 steps instead of 4. Any move of the link into the processor (see the link finding) will change the sound of existing sessions unless one formula is chosen.

**Evidence.** morph_link.cpp:44-65 `case MorphLinkMode::EaseIn: return std::sqrt(sweepNorm); case MorphLinkMode::EaseOut: return sweepNorm * sweepNorm; case MorphLinkMode::HoldRise: if (sweepNorm < 0.5f) return 0.0f; return (sweepNorm - 0.5f) * 2.0f; case MorphLinkMode::Stepped: return std::min(std::floor(sweepNorm * 5.0f) / 4.0f, 1.0f);` vs sweep_morph_link.h `case MorphLinkMode::EaseIn: return x * x; case MorphLinkMode::EaseOut: return 1.0f - (1.0f - x) * (1.0f - x); case MorphLinkMode::HoldRise: if (x < 0.6f) ... case MorphLinkMode::Stepped: return std::min(std::floor(x * 4.0f), 3.0f) / 3.0f;`. dsp-details.md:1264-1268 table.

**Suggested fix.** Delete morph_link.cpp's curve switch and call the shared applyMorphLinkCurve from one place, ideally the processor. Fix the plugin_ids.h:647 comment. Test: a table test that pins each MorphLinkMode at x = 0.25/0.5/0.75 against the dsp-details.md formulas and is run through the code path that drives audio.

**Verifier notes:**
- Correctness: the live path is visibility_controllers.cpp:162-209 -> applyMorphLinkMode (morph_link.cpp:44-65) with sqrt/x^2/0.5/5 levels, and the DSP copy (sweep_morph_link.h:81-118) with x^2/1-(1-x)^2/0.6/4 levels matches dsp-details.md:1264-1268 and :1300-1317 and is reached only through the uncalled getMorphPosition (sweep_processor.h:297-308); two secondary claims are wrong: tests/unit/morph_link_test.cpp:27-235 pins the controller formulas (so both copies are tested, each against its own), and plugin_ids.h:644-647 describes the live controller curve, not a third variant.
- Context (REFUTE): the two functions belong to two separately specified features that share one enum: the per-band X/Y link (006-morph-ui FR-034b-e, specs/_archive2_/006-morph-ui/spec.md:265-268, plan.md:364-367 `std::pow(sweepNorm, 0.5f)`, specs/_architecture_/plugin-architecture.md:913-920) is what morph_link.cpp implements and matches, while dsp-details.md section 8 and 007-sweep-system/spec.md:250-252/:425-427 specify the global kSweepMorphLink curve that sweep_morph_link.h implements; so users get the curve the per-band spec defines.
- Impact (two lowered): at x = 0.25 the live EaseIn gives 0.5 where the reference doc gives 0.0625, HoldRise rises at 50 % instead of 60 % and Stepped has 5 levels instead of 4, but all live curves are monotone and bounded with the same endpoints, so no click, NaN or level jump; a usability/spec-conformance difference; low.

**Fix-plan verification (T1.12):** not refuted. The x = 0.25 / 0.55 / 0.75 table against dsp-details.md passes for `applyMorphLinkCurve` but failed 11 of 21 points for the live controller `applyMorphLinkMode` (Ease In 0.5 vs 0.0625 at 0.25, Hold-Rise 0.1 vs 0 at 0.55, Stepped 0.25 vs 0.333 at 0.25); T1.12 deletes the controller copy and the processor applies `applyMorphLinkCurve`.

### D-068 — Global keyboard hook swallows arrow, Tab, Escape and (after one Tab) Space keys for every focused view, which breaks text entry on Linux and acts on hidden controls behind the preset browser

**Severity:** low (reported: medium; lowered by two verifiers) | **Verdict:** PLAUSIBLE (one refute on the Linux text-entry mechanism) | **Category:** ui-wiring | **Dimension:** ui-wiring | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/src/controller/keyboard_shortcut_handler.cpp:33-56](plugins/disrumpo/src/controller/keyboard_shortcut_handler.cpp#L33-L56), [:76-96](plugins/disrumpo/src/controller/keyboard_shortcut_handler.cpp#L76-L96)

**Problem.** The frame keyboard hook marks every arrow key as consumed (handleArrowKey always returns true), every Tab, Escape whenever a callback exists (always, controller.cpp:1408-1415), and Space whenever focusedBandIndex_ >= 0. focusedBandIndex_ is a private counter, not the real focus view. setActiveBandCount has no caller, so Tab cycles over the band count at open time.

**Why wrong.** CFrame dispatches to keyboard hooks before the focus view (cframe.cpp:544-552). On Linux, CTextEdit uses VSTGUI's GenericTextEdit (x11frame.cpp:775-778), which takes its keys through that same dispatch. The preset browser's hook, registered later and therefore called first, consumes only Escape and Enter.

**Consequence.** On Linux the arrow keys and Tab do nothing in the preset search field and the Save-preset name field. After one Tab, typing a space in a preset name toggles a band's bypass instead (and bypass is itself a processor no-op). With the browser open, arrow keys change a hidden focused control behind the overlay.

**Evidence.** keyboard_shortcut_handler.cpp:94-95 `adjustFocusedParameter(stepFraction); return true;` ; :73 Tab always true ; :77 Space keyed on focusedBandIndex_ ; grep setActiveBandCount callers: none ; vstgui cframe.cpp:544-546 hooks first

**Suggested fix.** Return early (not consumed) when the focus view is a CTextEdit or lies inside an open modal overlay; consume arrows only when adjustFocusedParameter actually edited; derive the band from the focus view; call setActiveBandCount from BandCountDisplayController. Test: with a CTextEdit focused, assert that onKeyboardEvent leaves event.consumed false for Left, Space and Tab.

**Verifier notes:**
- Correctness (REFUTE on mechanism): hooks run newest first (cframe.cpp:1581-1589 forEachReverse) and the Linux STBTextEditView registers its own frame hook in attached() (generictextedit.cpp:484-489) when the CTextEdit takes focus, i.e. after Disrumpo's hook from didOpen (controller.cpp:1417), so the text edit runs first, inserts Space (generictextedit.cpp:332-359) and consumes cursor-moving arrows; "arrow keys do nothing in the preset search field" and "a space toggles bypass" do not follow; setActiveBandCount (:58-60) really has no caller; the hidden-control scenario is not established.
- Context: handleArrowKey (:84-96) returns true even when adjustFocusedParameter (:146-161) edits nothing, handleTab (:70-74) always returns true, handleSpace (:76-82) uses only the private counter, handleEscape (:98-104) returns true whenever the callback exists (set whenever midiCCManager_ exists, controller.cpp:1408-1415); PresetBrowserView::onKeyboardEvent (preset_browser_view.cpp:195-230) consumes only Escape and Return; one verifier traced ctextedit.cpp:300-304 re-dispatching through frame->platformOnEvent and held the Linux claim, which the other two dispute.
- Impact (two lowered): what remains is that arrows are swallowed whenever no text field consumes them, which blocks arrow navigation in the preset browser list, and a control behind the overlay that holds focus (cviewcontainer.cpp:1076-1079) can be edited by arrows; minor UX, no audio effect; low.

### D-121 — FR-019 / SC-009 processor-cap tests are vacuous: no global cap or dynamic threshold exists in the code

**Severity:** low (reported: medium; lowered by all three verifiers) | **Verdict:** PLAUSIBLE (one refute on consequence) | **Category:** test-gap | **Dimension:** test-gaps | **Also flagged by:** D-105 | **Corroborated by measurement:** no

**Location:** [plugins/disrumpo/tests/unit/morph_processor_cap_test.cpp:122-168](plugins/disrumpo/tests/unit/morph_processor_cap_test.cpp#L122-L168)

**Problem.** 'dynamic threshold raises when exceeding limit' builds ONE engine, counts active weights (at most 4) and REQUIREs activeCount <= kMaxGlobalProcessors (16). That is always true. 'threshold never exceeds 0.25' checks that center weights of 0.25 are >= 0.24. kMaxGlobalProcessors and kMaxWeightThreshold are only defined (morph_node.h:142, 148) and nothing in src reads them.

**Why wrong.** The tests carry FR-019/SC-009 tags while no threshold-raising algorithm exists. They cannot fail for any implementation, including none.

**Consequence.** FR-019's 16-processor global cap is reported as covered while it is unimplemented. Four bands of 4-node cross-family morphs run 16 adapters, plus the blended adapters, with no limit.

**Evidence.** morph_processor_cap_test.cpp:143-144 `int activeCount = countActiveProcessors(weights, 4); REQUIRE(activeCount <= Disrumpo::kMaxGlobalProcessors);`; grep of src for kMaxGlobalProcessors|kMaxWeightThreshold: only morph_node.h:142 and :148

**Suggested fix.** Delete the vacuous cases or replace them with a Processor-level test. 4 bands x 4 cross-family nodes at the center; count adapters processed per sample through a test hook; REQUIRE <= 16 and REQUIRE that the effective threshold rose above 0.001. It stays red until FR-019 is implemented or formally dropped from the spec.

**Verifier notes:**
- Correctness: the cases at :122-145 and :147-168 cannot fail (countActiveProcessors at :58-67 returns at most 4; the second case checks 0.25 >= 0.24); kMaxWeightThreshold/kWeightThresholdIncrement/kMaxGlobalProcessors (morph_node.h:142/:145/:148) are never read in src, only kWeightThreshold 0.001 (morph_engine.cpp:250-263, :361, :529, :647, :745); the retired spec specs/_archive2_/005-morph-system/spec.md:198/:228 requires the algorithm and its compliance table still marks FR-019/SC-009 MET (:317, :326) citing this file.
- Context (REFUTE on consequence): the cap holds by construction: kMaxBands = 4 (band_state.h:61), kMaxMorphNodes = 4 (morph_node.h:86), one MorphEngine per band (band_processor.h:808), and processSameFamily (one blendedAdapter_, morph_engine.cpp:718) and processCrossFamily (at most activeNodeCount_ adapters, :743-757) are alternatives, so "16 plus the blended adapters, with no limit" is false and the threshold-raise branch can never trigger; the suggested test would encode wrong behaviour (FR-019 does not call for a raise at exactly 16).
- Impact (all three lowered): FR-019 was written for 5 bands (spec.md:150/:228, test comment :127 "5 bands * 4 nodes = 20"); misleading tags, a false MET entry and three dead constants; a static_assert(kMaxBands * kMaxMorphNodes <= kMaxGlobalProcessors) would document why it holds; low.

### D-167 — WaveshapeType::Diode is documented UNBOUNDED but is bounded to (-2, 1); the overflow-to-Inf lead does not hold, and the -Inf input case returns NaN despite the 'Infinity handled gracefully' contract

**Severity:** low | **Verdict:** PLAUSIBLE (one refute: documented and test-pinned) | **Category:** spec-conformance | **Dimension:** followup:shared-dsp-numerics-leftovers | **Corroborated by measurement:** no | **CROSS-PLUGIN:** shared header

**Location:** [dsp/include/krate/dsp/primitives/waveshaper.h:44](dsp/include/krate/dsp/primitives/waveshaper.h#L44), [:58](dsp/include/krate/dsp/primitives/waveshaper.h#L58), [:203-204](dsp/include/krate/dsp/primitives/waveshaper.h#L203-L204), [:259-260](dsp/include/krate/dsp/primitives/waveshaper.h#L259-L260)

**Problem.** waveshaper.h:44 and :58 say Diode 'can exceed [-1,1]', UNBOUNDED. Asymmetric::diode (sigmoid.h:358-366) is 1 - exp(-1.5x) for x >= 0, bounded below 1, and x/(1 - 0.5x) for x < 0, which tends to -2. So the output lies in (-2, 1) for every finite input and cannot overflow to Inf. With drive up to 10, the Granular 5x and feedback loops, nothing escalates: AllpassSaturator and FeedbackDistortion soft-clip the feedback to +/-2 (allpass_saturator.h:351-352, 498, 811, 845). For x = -Inf the reverse branch evaluates -Inf/Inf = NaN, which contradicts process()'s '@note Infinity inputs are handled gracefully' (waveshaper.h:204). waveshaper_test.cpp:577-581 accepts that as 'indeterminate'.

**Why wrong.** The doc should state the real bound so consumers size their headroom correctly, and the Inf contract should hold for every type. Disrumpo reaches Diode via Temporal curve index 7 (distortion_adapter.cpp:761-764), which has no DC blocker or limiter, so the negative half peaks at -2.0 (+6 dBFS). That is a bounded, documentable figure, not 'unbounded'.

**Consequence.** Documentation drift only, plus one edge case: -Inf into Diode gives NaN, and NaN then latches downstream DC blockers (see the DCBlocker finding). No overflow path exists. Recorded so the lead is closed with evidence.

**Evidence.** sigmoid.h:359-365 `if (x >= 0.0f) return 1.0f - std::exp(-x * 1.5f); else return x / (1.0f - 0.5f * x);`. waveshaper.h:58 `Diode = 7, ... (UNBOUNDED - can exceed [-1,1])`. :204 `@note Infinity inputs are handled gracefully`. allpass_saturator.h:351-352 `return Sigmoid::tanh(x * 0.5f) * 2.0f;` used at :498, :811, :845. distortion_adapter.cpp:761-764 maps curve to WaveshapeType index 0..8 for Temporal.

**Suggested fix.** Change the doc to 'Diode: bounded to (-2, 1), asymmetric: +6 dB headroom needed on the negative half'. In Asymmetric::diode, return -2.0f when x is -Inf, tested via detail::isInf, never std::isinf. Tighten waveshaper_test.cpp:577 to REQUIRE(output == -2.0f) in dsp_ieee_tests.

**Verifier notes:**
- Correctness: sigmoid.h:358-366 bounds every finite input to (-2, 1) (even -FLT_MAX gives about -2), so waveshaper.h:44/:58 "UNBOUNDED" is imprecise; for -Inf, process() (:205-216) -> applyShape (:259-260) -> diode gives -Inf/+Inf = NaN against the :204 note; waveshaper_test.cpp:577-584 accepts Inf, NaN or finite; Disrumpo reaches Diode through Temporal (:761-764), AllpassSaturator (:833-840), RingSaturation (:780-783), Spectral (:898-900) and Fractal (:934-936).
- Context (REFUTE): "UNBOUNDED" is defined where it is used (waveshaper.h:42-45: "can exceed [-1, 1]; users responsible for post-shaping limiting") and pinned by waveshaper_test.cpp:660-671 (REQUIRE(output < -1.0f)); the -Inf NaN is a deliberate, test-pinned per-type allowance (:577-584 "indeterminate", Tube's +/-Inf sections :512-517/:568-575 accept NaN too); the finding's own consequence line says it closes a dead lead.
- Impact: one wrong word in the doc; the -Inf case adds nothing because a host -Inf already breaks the CrossoverLR4 biquads and envelope follower before Diode (no finiteness guard in processor.cpp, band_processor.h or distortion_adapter.cpp), and under the macOS fast-math leg Inf arithmetic is undefined anyway; one verifier rated it below low; low retained.


## Inventory cross-reference

Three read-only inventories cross-referenced the parameter plumbing, the distortion type tables and the preset/version/docs claims. Every table and every mismatch is reproduced here in full; the counts are: params 4 tables and 40 mismatches, types 3 tables and 18 mismatches, presets-versions 3 tables and 11 mismatches.

### Inventory: Parameter plumbing (plugin_ids.h / registration / processor / state / preset writer / uidesc)

**Summary.** Parameter plumbing cross-reference for Disrumpo (read-only). Sides: plugin_ids.h enums and make*ParamId helpers; parameter_registration.cpp (controller registrations); processor_params.cpp processParameterChanges plus processor.h atomics; processor_state.cpp getState/setState; controller.cpp parseComponentState, setComponentState, and createComponentStateStream (the preset writer); editor.uidesc via tools/uidesc-inventory.js (JSON at f:/tmp/disrumpo-audit/uidesc-inventory.json, 453 controlTags, 42 templates). Registered IDs: 621 (globals 9, crossover 3, sweep 35 + 2 read-only outputs, mod 54 + routing 128 = 182, band 4x30 = 120, node 4x4x17 = 272). Every registered ID is consumed by processParameterChanges except the no-op and discarded ones listed below; no consumed ID is unregistered. The 35 sweep-group, 54 mod-group and 16 band-level uidesc tags all resolve to registered IDs. The mismatches cluster into: (a) linear RangeParameter registrations where the processor denormalizes logarithmically (sweep frequency, sweep LFO rate, mod LFO rates, crossover); (b) defaults that differ across registration, atomics, state fallbacks and DSP defaults; (c) preset-writer vs session-writer disagreements (morph mode/smoothing, active nodes, routing source, custom curve); (d) params that are registered and automatable but never persisted (oversample max, band morph links, sweep MIDI); (e) UI-edited custom-curve points that the processor never consumes; (f) uidesc tags for bands 5-8 and crossovers 4-7 that point at unregistered IDs.

#### params table 1

| Group | Registered (parameter_registration.cpp) | Consumed (processor_params.cpp) | Saved (processor_state.cpp getState / controller.cpp createComponentStateStream) | uidesc direct tags |
|---|---|---|---|---|
| Global 0x00-0x08 | 9 | 0x00-0x04 yes; 0x05-0x08 no (UI-only, kNoFlags) | 0x00-0x03 (in:gain, out:gain, mix, bandCount); oversample 0x04 not saved | 0x00-0x06 (SpectrumMode 0x05, ModPanelVisible 0x06); 0x07, 0x08 none |
| Crossover 0x0F10-0x0F12 | 3 | yes (log10 interpretation) | yes (Hz) | Crossover1-3 yes; Crossover4-7 (0x0F13-0x0F16) unregistered |
| Sweep 0x0E00-0x0E23 | 6 + 6 + 4 | yes | yes | all 16 present |
| Sweep custom curve 0x0E30-0x0E40 | 17 | point count discarded; points no-op | processor writes real breakpoints; preset writer writes fixed 2-point curve | none (CustomCurveEditor view performs the edits) |
| Sweep MIDI 0x0E50-0x0E51 | 2 (automatable) | yes (assignedMidiCC_) | not saved | none |
| Mod 0x0D00-0x0D6F | 54 | yes | yes | all 54 present |
| Routing 0x0D80-0x0DFF | 128 | yes | yes | all 128 present |
| Band 0xF0xx-0xF3xx (16 base + 14 displayed proxies) x4 | 120 | Gain/Pan/Solo/Bypass/Mute/MorphX/Y/Mode/Smoothing/ActiveNodes consumed; MorphXLink, MorphYLink, SelectedNode, Expanded, DisplayedType, TabView no-op; proxies unconsumed | Gain/Pan/Solo/Bypass/Mute/MorphX/Y/Mode(0 literal in session writer)/ActiveNodes/Smoothing(0 literal in session writer); MorphXLink/YLink/SelectedNode not saved | 64 direct (bands 1-4); bands 5-8 (52) unregistered |
| Node 0xNbpp (4 bands x 4 nodes x 17) | 272 | yes | yes (type, drive, mix, tone, bias, folds, bitdepth, shape0-9, shadow) | 49 direct (B1N1-4, B2N1, B3N1, B4N1); Band.Node* proxies cover Drive/Mix/Tone/Bias/Shape0-9 of the selected node; Folds and BitDepth for B2N2-B4N4 have no control; B5-8 (28) unregistered |

#### params table 2

| Parameter | Registration (file:line) | Processor / DSP (file:line) | Effect |
|---|---|---|---|
| Sweep Frequency | parameter_registration.cpp:187 RangeParameter 20..20000 linear, default 1000 | processor_params.cpp:384-385 log2 map (sweep_morph_link.h:52-67) | default normalized 0.049 is read as ~28 Hz |
| Sweep LFO Rate | parameter_registration.cpp:248 RangeParameter 0.01..20 linear, default 1.0 | processor_params.cpp:430-434 exp (log) map | default 1 Hz is read as ~0.015 Hz |
| LFO 1 Rate | parameter_registration.cpp:365 0.01..20 linear, default 1.0 | processor_params.cpp:581-582 exp | same class |
| LFO 2 Rate | parameter_registration.cpp:401 0.01..20 linear, default 0.5 | processor_params.cpp:613 exp | same class |
| Crossover 1-3 | parameter_registration.cpp:150-160 RangeParameter 20..20000 linear | processor_params.cpp:988-991 log10 map; controller.cpp:210 loads via toNormalized (linear) | displayed Hz and actual crossover disagree |
| Sweep Intensity | parameter_registration.cpp:209 0..100 %, default 50 | processor_params.cpp:405 x2.0 (0..200 %) | UI reads 0-100 %, DSP runs 0-200 % |
| Sweep Morph Link | parameter_registration.cpp:217-228, 6 labels | processor_params.cpp:412 round(v*7) over 8 modes; controller.cpp:237 writes /7 | InverseSweep and HoldRise unreachable; labels shifted |
| Node Bit Depth | parameter_registration.cpp:945 4..24 bit | processor_params.cpp:878 writes 4..24; bit_crusher.h:51 kMaxBitDepth 16 (clamped at :152); distortion_adapter.h:109 doc [1,16] | 17..24 display, effective max 16 |
| Node Folds | parameter_registration.cpp:939 1..12 | processor_params.cpp:874 1+round(v*11); wavefolder.h:149-150 clamp [0,10]; distortion_adapter.h:99 doc [1,8] | 11-12 display, effective max 10 |
| Env Attack / Release / Sensitivity | parameter_registration.cpp:435, 437, 439 plain Parameter with units ms/ms/% | processor_params.cpp:643, 648 (1..100, 10..500 ms) | host shows 0..1 with unit label |
| Random Rate / Smoothness | parameter_registration.cpp:452, 454 Hz / % plain | processor_params.cpp:664 (0.1..50 Hz) | same class |
| LFO 1 Phase | parameter_registration.cpp:378 plain, deg | processor_params.cpp:591 x360 | same class |
| S&H Rate / Slew | parameter_registration.cpp:482, 484 plain Hz / ms | processor_params.cpp:699, 704 | same class |
| Pitch Min / Max / Tracking | parameter_registration.cpp:488, 490, 494 plain | processor_params.cpp:711, 716, 724 | same class |
| Transient Attack / Decay | parameter_registration.cpp:500, 502 plain ms | processor_params.cpp:734, 739 | same class |
| Rungler Rate | parameter_registration.cpp:506 plain Hz | processor_params.cpp:746 | same class |
| Route Amount | parameter_registration.cpp:602-603 -1..1 with unit %% | processor_params.cpp:563 v*2-1 | unit label wrong for a -1..1 value |
| Shape slot 2 (all types) | parameter_registration.cpp:820, 957 stepCount 8 | processor_params.cpp:58 Tube stage round(v*3) (4 levels), other types continuous | host steps 9 positions where DSP has 4 or continuous |

#### params table 3

| Default | Registration | Atomic / DSP init | State fallback (setState / controller parse) | Mismatch |
|---|---|---|---|---|
| Sweep Width | parameter_registration.cpp:198 1.0 oct (norm 0.143) | processor.h:227 baseSweepWidthNorm_ 0.286 (1.5 oct) | processor_state.cpp:473, controller.cpp:220 0.286 | yes |
| Sweep Intensity | parameter_registration.cpp:209 50 % (norm 0.5 -> 1.0) | processor.h:230 0.25 (-> 0.5); sweep_processor.h:58 kDefaultIntensity 0.5 | processor_state.cpp:474 0.25 | yes |
| Sweep Falloff | parameter_registration.cpp:237-238 index 0 Hard | sweep_processor.h:355 Smooth | processor_state.cpp:475, controller.cpp:222 = 1 (Smooth) | yes |
| Crossover 1-3 | parameter_registration.cpp:147-149 112.2 / 632.5 / 3556 Hz | crossover_network.h:428-430 200 / 1500 / 6000 Hz | processor_state.cpp:451, controller.cpp:205 1000 Hz | yes (3 values) |
| Node Folds | parameter_registration.cpp:939 2.0 | distortion_adapter.h:99 1.0 | processor_state.cpp:841, controller.cpp:540 1.0 | yes |
| LFO 1 Retrigger | parameter_registration.cpp:395-396 0.0 (off) | lfo.h:557 retriggerEnabled_ true (no override in modulation_engine.h) | processor_state.cpp:612, controller.cpp:319 = 1 | yes |
| LFO 1 / LFO 2 Rate (fallback) | parameter_registration.cpp:365 1.0 Hz; :401 0.5 Hz | n/a | processor_state.cpp:581, :617 norm 0.5 -> ~0.45 Hz | yes |
| Oversample Max | parameter_registration.cpp:90 2/3 (4x) | processor.h:145 4 | none (not saved) | reload always 4x |
| Band Count | parameter_registration.cpp:76 norm 1.0 (4) | processor.h:153 kDefaultBands 4 | processor_state.cpp:407, controller.cpp:168 4 | no |
| Band Active Nodes | parameter_registration.cpp:706 norm 0 (1) | morph_node.h:135 kDefaultActiveNodes 1 | processor_state.cpp (kDefaultActiveNodes) | no |
| Node Drive/Mix/Tone/Bias/BitDepth | 1.0 / 100 % / 4000 / 0 / 16 | distortion_adapter.h:56-58, :109 same | processor_state.cpp:839-842 same | no |
| Shape slots | parameter_registration.cpp:823 0.5 | morph_node.h:43 0.5 | processor_state.cpp fallback n/a | no |

#### params table 4

| Preset writer (createComponentStateStream) vs session writer (processor getState) | Preset writer | Session writer | Consequence |
|---|---|---|---|
| Band morph mode | controller.cpp:1829 writes real mode (/2 norm) | processor_state.cpp:319 writes literal 0 | session restore resets mode to 1D Linear |
| Band morph smoothing | controller.cpp:1834 writes smoothingNorm*500 | processor_state.cpp:323 writes literal 0.0f | session restore resets smoothing to 0 |
| Band active nodes | controller.cpp:1831 round(norm*2)+2 (saves 1 as 2, 2 as 3) | processor_state.cpp:321 writes activeNodeCount (1..4) | 1-node and 2-node bands change count |
| Routing source | controller.cpp:1815 getInt8FromList(...,12): round(idx/13*12) | processor_state.cpp:769-770 reads raw enum; processor_params.cpp:555 uses 13 | sources 7..13 saved off by one (Macro 3 -> Macro 2, Transient -> Pitch) |
| Custom curve | controller.cpp:1748-1750 fixed 2 points (0,0),(1,1) | processor_state.cpp:144-152 writes real breakpoints | preset save drops the curve |
| Oversample / Sweep MIDI / Band morph X,Y link / SelectedNode | not written | not written (no assignedMidiCC_, no oversample, no link fields) | registered automatable values reset on load |

#### params mismatches (40)

1. **Sweep Frequency registered as linear RangeParameter but processed on a log2 curve**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:187 (RangeParameter 20.0, 20000.0, 1000.0) vs plugins/disrumpo/src/processor/processor_params.cpp:384-385 (log2Freq = kSweepLog2Min + value * kSweepLog2Range; pow(2, log2Freq))
   - Detail: Host normalized default for 1000 Hz is 0.049 on the linear registration; the processor reads 0.049 as ~28 Hz. Controller sweep indicator (controller.cpp:1283-1285) uses the log map, so the readout and the indicator disagree. Also sweep_morph_link.h:52-67 defines the log map.
2. **Sweep LFO Rate registered linear, processed exponentially**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:247-248 (0.01, 20.0, 1.0) vs plugins/disrumpo/src/processor/processor_params.cpp:430-434 (exp(kMinRateLog + value*(kMaxRateLog-kMinRateLog)))
   - Detail: Default 1.0 Hz is stored as normalized 0.0495 by the host; processor computes exp(-4.2292) = ~0.0146 Hz. processor_state.cpp:497 and controller.cpp (sweep LFO fallback) use the log normalization, so save/load is consistent but the host-facing value is not.
3. **LFO 1 Rate registered linear, processed exponentially**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:364-365 vs plugins/disrumpo/src/processor/processor_params.cpp:581-582
   - Detail: Same class as Sweep LFO Rate. Default 1.0 Hz maps to ~0.015 Hz in the engine when the host sends the default normalized value.
4. **LFO 2 Rate registered linear, processed exponentially**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:400-401 (0.01, 20.0, 0.5) vs plugins/disrumpo/src/processor/processor_params.cpp:612-614
   - Detail: Default 0.5 Hz is read as ~0.0146 Hz...
5. **Crossover frequency registered linear, processed log10; load path normalizes linearly**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:150-160 (RangeParameter kMinCrossoverHz..kMaxCrossoverHz) vs plugins/disrumpo/src/processor/processor_params.cpp:988-991 (logFreq = logMin + value*(logMax-logMin); pow(10, logFreq)); controller.cpp:210 setter(param->getInfo().id, param->toNormalized(freq))
   - Detail: UI edits and preset loads (loadComponentStateWithNotify -> editParamWithNotify) send a linear normalized value that the processor interprets on a log scale, so the audible crossover differs from the displayed Hz. Session restore (processor_state.cpp:451-455) sets Hz directly and is correct, so the two load paths disagree.
6. **Crossover default frequencies: three different sets**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:147-149 (log-spaced: 112.2, 632.5, 3556 Hz) vs plugins/disrumpo/src/dsp/crossover_network.h:428-430 (200.0f, 1500.0f, 6000.0f) vs plugins/disrumpo/src/processor/processor_state.cpp:451 and plugins/disrumpo/src/controller/controller.cpp:205 (1000.0f fallback)
   - Detail: Processor starts with 200/1500/6000 Hz (no setCrossoverFrequency in processor.cpp setup), the host default is 112/632/3556 Hz, and a truncated stream falls back to 1000 Hz for all three.
7. **Sweep Intensity displayed 0-100 % but DSP runs 0-200 %**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:206-209 (0.0, 100.0, 50.0, unit %) vs plugins/disrumpo/src/processor/processor_params.cpp:405 (intensity = value * 2.0f, comment 0-200%) and processor.cpp:289 (modIntNorm * 2.0f)
   - Detail: Default 50 % (norm 0.5) produces intensity 1.0 while the atomic default (processor.h:230) and sweep_processor.h:58 kDefaultIntensity = 0.5 correspond to 0.25 normalized.
8. **Sweep Morph Link: six UI labels, eight processor modes, wrong mapping**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:217-228 (None, Linear, Inverse, Ease In, Ease Out, Ease In-Out) vs plugins/disrumpo/src/processor/processor_params.cpp:412 (round(value*(kMorphLinkModeCount-1)), 8 modes) and plugins/disrumpo/src/controller/controller.cpp:237 (writes /(kMorphLinkModeCount-1))
   - Detail: Six host steps map to modes {0,1,3,4,6,7}: 'Inverse' -> EaseIn, 'Ease In' -> EaseOut, 'Ease Out' -> Stepped, 'Ease In-Out' -> Custom. InverseSweep (2) and HoldRise (5) cannot be selected.
9. **Node Bit Depth registered 4..24, DSP clamps at 16**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:945 (4.0, 24.0, 16.0, 20, unit bit) vs plugins/disrumpo/src/processor/processor_params.cpp:878 (mn.params.bitDepth = 4 + round(v*20)) vs dsp/include/krate/dsp/primitives/bit_crusher.h:51,152 (kMaxBitDepth = 16, clamp) and plugins/disrumpo/src/dsp/distortion_adapter.h:109 (doc [1, 16])
   - Detail: Values 17..24 are displayed and saved but clamp to 16 in the bit crusher.
10. **Node Folds registered 1..12, wavefolder clamps at 10, adapter documents 1..8**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:939 (1.0, 12.0, 2.0, 11) vs plugins/disrumpo/src/processor/processor_params.cpp:874 (1 + round(v*11)) vs dsp/include/krate/dsp/primitives/wavefolder.h:146-151 (FR-006a clamp [0.0, 10.0]) vs plugins/disrumpo/src/dsp/distortion_adapter.h:99 (doc [1, 8])
   - Detail: Folds 11-12 are displayed but effectively clamp to 10 in the wavefolder.
11. **Plain Parameter registrations carry unit strings but have no range; host shows normalized 0..1**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:378 (LFO 1 Phase deg), :435 (Env Attack ms), :437 (Env Release ms), :439 (Env Sensitivity %), :452 (Random Rate Hz), :454 (Random Smoothness %), :482 (S&H Rate Hz), :484 (S&H Slew ms), :488 (Pitch Min Hz), :490 (Pitch Max Hz), :494 (Pitch Tracking ms), :500 (Transient Attack ms), :502 (Transient Decay ms), :506 (Rungler Rate Hz) vs processor_params.cpp:591, 643, 648, 664, 699, 704, 711, 716, 724, 734, 739, 746
   - Detail: These use the base Parameter overload (title, units, stepCount, normalizedDefault, flags, id), so toPlain equals the normalized value; the processor denormalizes to 1..100 ms, 0.1..50 Hz, x360 deg, etc. The readout does not match the unit label. Ranges are not in the registration at all.
12. **Route Amount: -1..1 range with unit %**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:602-603 (RangeParameter -1.0, 1.0, unit %) vs plugins/disrumpo/src/processor/processor_params.cpp:563 (amount = value*2 - 1)
   - Detail: Displayed as a percentage of a bipolar -1..1 value; a 0.5 amount reads as 0.5 %.
13. **Shape slot 2 stepCount 8 applied to every distortion type**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:820 and :957 (stepCount = (s == 2) ? 8 : 0) vs plugins/disrumpo/src/processor/processor_params.cpp:58 (Tube stage round(slot*3), 4 levels) and other types using slot 2 continuously (e.g. :65 Tape speed)
   - Detail: Host steps nine positions where the DSP uses four or a continuous value.
14. **Band Active Nodes documented as [2,3,4] but registered and consumed as 1..4**
   - Where: plugins/disrumpo/src/plugin_ids.h:185 (kBandActiveNodes ... [2, 3, 4]) vs plugins/disrumpo/src/controller/parameter_registration.cpp:696-706 (appendString 1..4, setNormalized(0.0)) and plugins/disrumpo/src/processor/processor_params.cpp:952 with plugins/disrumpo/src/dsp/morph_node.h:132 (kMinActiveNodes = 1)
   - Detail: Documentation only; the behavior is 1..4.
15. **Sweep Width default: registration 1.0 oct, atomic and fallbacks 1.5 oct**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:198 (0.5, 4.0, 1.0) vs plugins/disrumpo/src/processor/processor.h:227 (baseSweepWidthNorm_{0.286f}) vs plugins/disrumpo/src/processor/processor_state.cpp:473 and plugins/disrumpo/src/controller/controller.cpp:220 (0.286f)
   - Detail: 0.286 is 1.0/3.5 while the save formula (processor_state.cpp:89) is (width-0.5)/3.5 = 0.143 for 1.0 oct. The fallback and atomic default are 1.5 oct, not the registered 1.0.
16. **Sweep Intensity default: registration 50 % (intensity 1.0), atomic and fallback 0.25 (intensity 0.5)**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:209 vs plugins/disrumpo/src/processor/processor.h:230 (baseSweepIntensityNorm_{0.25f}) and plugins/disrumpo/src/processor/processor_state.cpp:474 (0.25f) and plugins/disrumpo/src/dsp/sweep_processor.h:58 (kDefaultIntensity = 0.5f)
   - Detail: Host default normalized 0.5 yields intensity 1.0 while the DSP starts at 0.5.
17. **Sweep Falloff default: registration Hard, DSP and fallback Smooth**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:232-238 (no setNormalized, index 0 Hard) vs plugins/disrumpo/src/dsp/sweep_processor.h:355 (falloffMode_ = SweepFalloff::Smooth) vs plugins/disrumpo/src/processor/processor_state.cpp:475 and plugins/disrumpo/src/controller/controller.cpp:222 (sweepFalloff = 1)
   - Detail: The engine runs Smooth until the host sends the parameter; the UI shows Hard.
18. **Node Folds default: registration 2.0, DSP and fallback 1.0**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:939 (1.0, 12.0, 2.0, 11) vs plugins/disrumpo/src/dsp/distortion_adapter.h:99 (folds = 1.0f) vs plugins/disrumpo/src/processor/processor_state.cpp:841 and plugins/disrumpo/src/controller/controller.cpp:540 (float folds = 1.0f)
   - Detail: A fresh node starts at 1 fold in the DSP while the UI shows 2.
19. **LFO 1 Retrigger default: registration off, LFO primitive and fallbacks on**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:395-396 (default 0.0) vs dsp/include/krate/dsp/primitives/lfo.h:557 (retriggerEnabled_ = true) and plugins/disrumpo/src/processor/processor_state.cpp:612 and plugins/disrumpo/src/controller/controller.cpp:319 (Steinberg::int8 lfo1Retrigger = 1)
   - Detail: modulation_engine.h does not override the LFO default, so the engine retriggers until the host sends the parameter.
20. **Mod LFO fallbacks in setState differ from registered defaults**
   - Where: plugins/disrumpo/src/processor/processor_state.cpp:581 (lfo1RateNorm = 0.5f) and :617 (lfo2RateNorm = 0.5f) vs parameter_registration.cpp:365 (LFO 1 default 1.0 Hz) and :401 (LFO 2 default 0.5 Hz)
   - Detail: A stream truncated before the LFO rate loads 0.45 Hz for both LFOs (log normalization of 0.5).
21. **Oversample Max is automatable and consumed but never saved or loaded**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:77-90 (kCanAutomate) and plugins/disrumpo/src/processor/processor_params.cpp:349-361 (consumed into maxOversampleFactor_) vs plugins/disrumpo/src/processor/processor_state.cpp getState (lines 47-355; no write) and setState (no read) vs plugins/disrumpo/src/controller/controller.cpp:160-615 (parseComponentState, no kGlobalOversample setter) and :1687-1866 (createComponentStateStream, no write)
   - Detail: After save and reload the oversample limit returns to the 4x default (processor.h:145). grep for oversample in processor_state.cpp and controller.cpp returns no hits.
22. **Band morph mode and morph smoothing are written as literals by the session writer**
   - Where: plugins/disrumpo/src/processor/processor_state.cpp:319 (writeInt8(0) // morphMode) and :323 (writeFloat(0.0f) // morphSmoothing) vs plugins/disrumpo/src/processor/processor_state.cpp:819-831 (setState reads them) vs plugins/disrumpo/src/controller/controller.cpp:1829 and :1834 (preset writer writes real values) and :513-529 (controller parse)
   - Detail: Host session restore loses morph mode and smoothing; the preset path keeps them. The two writers disagree on the same stream layout.
23. **Band active-node count: preset writer maps 1 to 2 and 2 to 3**
   - Where: plugins/disrumpo/src/controller/controller.cpp:1831 (activeNodes = round(activeNodesNorm * 2.0f) + 2) vs plugins/disrumpo/src/processor/processor_state.cpp:321 (writes cache.activeNodeCount, 1..4) and plugins/disrumpo/src/controller/parameter_registration.cpp:696-706 (norm steps 1/3, values 1..4)
   - Detail: Saved presets change band node counts on reload (1 becomes 2, 2 becomes 3).
24. **Routing source in preset writer uses the wrong denominator**
   - Where: plugins/disrumpo/src/controller/controller.cpp:1815 (getInt8FromList(makeRoutingParamId(r, 0), 12)) vs plugins/disrumpo/src/processor/processor_params.cpp:555 (value * (kUIModSourceCount - 1), 13) and plugins/disrumpo/src/plugin_ids.h:546 (kUIModSourceCount = 14)
   - Detail: round(idx/13*12) maps source 7 to 6, 8 to 7, ... 13 to 12. Preset routings for sources 7..13 load one source lower.
25. **Custom curve: preset writer hard-codes a 2-point curve; UI edits never reach the processor**
   - Where: plugins/disrumpo/src/controller/controller.cpp:1748-1750 (writeInt32(2); (0,0); (1,1)) vs plugins/disrumpo/src/processor/processor_state.cpp:144-152 (writes breakpoints) and :550-570 (setState reads them); plugins/disrumpo/src/processor/processor_params.cpp:495-520 (point count clamped and discarded, point params fall through to the comment at :520 with no handler)
   - Detail: Preset save drops the user curve. The CustomCurveEditor (controller.cpp:1140-1230) writes kSweepCustomCurveP* via performEdit, but the processor ignores them and the processor's customCurve_ changes only through setState. The 17 curve IDs (0x0E30-0x0E40) are registered (parameter_registration.cpp:311-345) and automatable but have no audible effect in-session.
26. **Shape-derived DistortionParams are not re-derived on host session restore**
   - Where: plugins/disrumpo/src/processor/processor_state.cpp:871 (shapeSlots[s] = slotValue in setState; no mapShapeSlotsToParams call) vs plugins/disrumpo/src/processor/processor_params.cpp:852 and :892 (mapShapeSlotsToParams called only in processParameterChanges)
   - Detail: After host restore the type-specific fields (e.g. curve, knee, threshold, satStage) keep their defaults until a shape knob moves. Preset load re-sends every parameter through editParamWithNotify (controller.cpp:1868-1900), so presets are unaffected.
27. **Node Bias, Folds and BitDepth are written by two sources in the same DistortionParams fields**
   - Where: plugins/disrumpo/src/processor/processor_params.cpp:870 (bias), :874 (folds), :878 (bitDepth) vs :56, :63, :73, :81, :93, :102, :111, :172 (bias from shape slots), :90, :99, :108 (folds from shape slots), :131 (bitDepth from Bitcrush slot 0)
   - Detail: Both IDs are registered automatable (parameter_registration.cpp:915-945 and :863-881 for shapes); the last writer wins and the UI shows two controls for one field.
28. **Sweep MIDI Learn and MIDI CC: registered automatable, not saved, no control**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:345-352 (kCanAutomate) vs plugins/disrumpo/src/processor/processor_params.cpp:527-534 (midiLearnActive_, assignedMidiCC_) vs plugins/disrumpo/src/processor/processor_state.cpp (no write of assignedMidiCC_ or midiLearnActive_) and tools: no uidesc tag for 0x0E50 or 0x0E51 and no controller binding (grep of kSweepMidiCCNumber / kSweepMidiLearnActive in controller returns only registration)
   - Detail: A host can automate these IDs but no UI sets them, and the value is not persisted. The MIDI Learn flow uses the global 0x0F07 instead (controller.cpp:1412, :1626).
29. **Band Morph X/Y Link and Selected Node: automatable, not saved**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:723-760 and 0x0D (kCanAutomate StringLists) vs plugins/disrumpo/src/processor/processor_params.cpp:971-974 (no-op in processor) vs plugins/disrumpo/src/controller/visibility_controllers.cpp:163-172 (only consumer, controller side) and plugins/disrumpo/src/processor/processor_state.cpp morph block (no link fields) and controller.cpp:513-529 (no link setter)
   - Detail: Link modes reset to None on session restore and preset load. controller.cpp:598 reads the selected node but nothing restores it.
30. **MIDI Learn Target (0x0F08) registered but never read or written**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:128-135 vs plugins/disrumpo/src/plugin_ids.h:70 (kGlobalMidiLearnTarget = 0x08)
   - Detail: kNoFlags, so not automatable; no consumer or writer exists anywhere in src.
31. **uidesc tags Crossover4-7 point at unregistered IDs**
   - Where: plugins/disrumpo/resources/editor.uidesc:64 (Crossover4 tag=3859, 0x0F13) ... Crossover7 tag=3862 (0x0F16) vs plugins/disrumpo/src/plugin_ids.h crossover range 0x0F10-0x0F12 and parameter_registration.cpp:145 (loop i < kMaxBands - 1)
   - Detail: 4 literal tags with no registered parameter (uidesc-inventory.json controlTags).
32. **uidesc tags Band5-Band8 point at unregistered band IDs**
   - Where: plugins/disrumpo/resources/editor.uidesc:120 (Band5Gain tag=62464, 0xF400) ... Band8MorphYLink (0xF70C) vs plugins/disrumpo/src/controller/parameter_registration.cpp band loop (kMaxBands = 4, band_state.h:61) and plugin_ids.h isBandParamId
   - Detail: 52 literal tags for bands 5-8 with no registration; the controller only creates 4 bands.
33. **uidesc node tags for bands 5-8 are outside the node ID range**
   - Where: plugins/disrumpo/resources/editor.uidesc:200 (B5N1Type tag=1024, 0x0400) ... B8N1BitDepth (0x0706) vs plugins/disrumpo/src/plugin_ids.h isNodeParamId (bandNibble <= 3)
   - Detail: 28 literal tags that resolve to no parameter; they read as node parameters of bands 5-8 only if a future band count raises kMaxBands.
34. **Node Folds and BitDepth for nodes 2-4 of bands 2-4 have no control**
   - Where: plugins/disrumpo/src/controller/parameter_registration.cpp:939 and :945 (registered per node) vs editor.uidesc (direct node tags only for B1N1-B1N4, B2N1, B3N1, B4N1) vs plugins/disrumpo/src/controller/sub_controllers.h remap list (Band.NodeDrive/Mix/Tone/Bias and shape slots only; no NodeFolds or NodeBitDepth)
   - Detail: 18 registered automatable IDs (B2N2-B4N4 x Folds, BitDepth) have no UI control. Host automation still reaches them.
35. **Node-level IDs reached only through selected-node proxies**
   - Where: plugins/disrumpo/src/controller/sub_controllers.h:124-150 (Band.Node* remapped to kBandDisplayed* proxies) and plugins/disrumpo/resources/editor.uidesc (node tags only for the rows listed above)
   - Detail: Drive, Mix, Tone, Bias and Shape0-9 of nodes that are not selected have no direct control. Design-level, recorded so the coverage reading is complete.
36. **uidesc Band.* placeholder tags are template-scoped, not literal IDs**
   - Where: plugins/disrumpo/resources/editor.uidesc (Band.DisplayedType 0xC350 ... Band.NodeShape9 0xC36E, 31 tags) vs plugins/disrumpo/src/controller/sub_controllers.h:77-150
   - Detail: Resolved per band by BandSubController::getTagForName. Not a mismatch; listed so the unclassified tag count (116) is explained: 84 literal unregistered (see above), 31 template placeholders, 1 ModPanelContainer.
37. **Container view tags fall inside the node ID range numerically**
   - Where: plugins/disrumpo/resources/editor.uidesc Band1Container..Band8ExpandedContainer (0x2328-0x2393) vs plugins/disrumpo/src/plugin_ids.h isNodeParamId
   - Detail: Their values decode as node 2 / band 3 params. No registered ID falls there (node params use param bytes 0x00-0x19), so no collision today; a hazard for any future classifier.
38. **Preset-writer and session-writer disagree on the custom curve and morph fields**
   - Where: plugins/disrumpo/src/controller/controller.cpp:1748-1834 vs plugins/disrumpo/src/processor/processor_state.cpp:144-152, 319-323
   - Detail: Same stream format, different content. Summarized in the state table above.
39. **Doc drift: Band Count range, version history, note-table size**
   - Where: plugins/disrumpo/src/plugin_ids.h:383 (kBandCountId = 0x0F03, comment Band count (1-8)) vs parameter_registration.cpp:67-76 (1..4); plugins/disrumpo/src/plugin_ids.h version history (no v10 or v11 entry) vs processor_state.cpp rungler gate (version >= 11); plugin_ids.h migration comment 'v10, 21 entries' vs dsp/include/krate/dsp/core/note_value.h:124 (kNoteValueDropdownCount = 30)
   - Detail: Comments only, no code path affected; v10 and v11 note-index encoding is not documented.
40. **Doc drift in processor_state.cpp comments**
   - Where: plugins/disrumpo/src/processor/processor_state.cpp:62 ('Per-band state for all 8 bands') and :73 ('Crossover frequencies (7 floats)') vs kMaxBands = 4 loops at :63 and :76-80 (3 floats written)
   - Detail: Comments only.

### Inventory: Distortion type tables (enum / switches / adapter / menus / uidesc / tests / docs)

**Summary.** Cross-referenced the 26-entry DistortionType enum (distortion_types.h:32-74) against its switch tables (getCategory, getFamily, getRecommendedOversample, getTypeName), the DSP adapter (distortion_adapter.cpp setType 143-160, processRaw 258-580, routeParamsToProcessor 600-955), morph_engine.cpp, oversampling_utils.h, processor_params.cpp mapShapeSlotsToParams (39-285), the controller menus (parameter_registration.cpp 771-799 and 847-857), editor.uidesc (TypeParams templates 879-1240, template-names 1290 and 1340, menu-items), tests (band_strip_test.cpp, oversampling_single_type_tests.cpp, menu_items_test.cpp), docs/index.html and specs (specs-overview.md, dsp-details.md, roadmap.md, custom-controls.md, ui-mockups.md). Counts: enum 26 (+COUNT sentinel); every switch covers all 26 members with no fallthrough gaps; getCategory 6/3/2/5/1/3/6, getFamily 6/3/2/5/2/2/6, oversample 10x4 + 10x2 + 6x1, getTypeName 26 strings, menu 26 strings, kDistortionTypeNames 26, uidesc TypeParams 26 templates and both template-names lists 26 entries, all in enum order. Mismatches below are the disagreements found.

#### types table 1

| Table | Side | Count | Order matches enum? |
|---|---|---|---|
| DistortionType enum | distortion_types.h:32-74 | 26 + COUNT | canonical |
| getCategory | distortion_types.h:101-153 | 26 | yes (Feedback=Hybrid) |
| getFamily | distortion_types.h:297-349 | 26 | yes (Feedback=Dynamic) |
| getRecommendedOversample | distortion_types.h:167-205 | 10x4, 10x2, 6x1 = 26 | yes |
| getTypeName | distortion_types.h:216-241 | 26 | yes |
| processRaw switch | distortion_adapter.cpp:261-573 | 26 | yes, default returns input |
| routeParamsToProcessor switch | distortion_adapter.cpp:605-951 | 26 | yes, default break |
| displayed-type menu | parameter_registration.cpp:774-799 | 26 | yes |
| node-type menu | parameter_registration.cpp:847-857 | 26 | yes |
| uidesc template-names | editor.uidesc:1290 and 1340 | 26 | yes (index 14=Aliasing,15=Bitwise,16=Temporal,...,25=Stochastic) |
| uidesc titles | editor.uidesc:880-1229 | 26 | yes |
| docs/index.html type list | docs/index.html:135-185 | 26 (6/3/2/5/1/3/6 per category) | category order, Feedback under Hybrid |
| band_strip_test canonical | band_strip_test.cpp:29-56 | 26 | NO, see M2 |
| oversampling test table | oversampling_single_type_tests.cpp:41-72 | 10/10/6 | matches code |
| Roadmap App. B OS factors | roadmap.md:1116-1141 | 26 | all 26 match code |
| menu_items_test.cpp | tests/unit/menu_items_test.cpp | 0 type names | tests only parseMenuItems, no type table |

#### types table 2

| Type | Shape slots UI (editor.uidesc Band.NodeShapeN) | Slots consumed (processor_params.cpp) | Result |
|---|---|---|---|
| SoftClip | 2 | 0-1 | ok |
| HardClip | 2 | 0-1 | ok |
| Tube | 3 | 0-2 | ok |
| Tape | 6 | 0-5 | ok |
| Fuzz | 4 | 0-3 | ok |
| AsymFuzz | 5 | 0-4 | ok |
| SineFold | 5 | 0-4 | ok |
| TriFold | 5 | 0-4 | ok |
| SergeFold | 6 | 0-5 | ok |
| FullRectify | 2 | 0-1 | ok |
| HalfRectify | 3 | 0-2 | ok |
| Bitcrush | 4 | 0-3 | ok |
| SampleReduce | 4 | 0-3 | ok |
| Quantize | 4 | 0-3 | ok |
| Aliasing | 2 | 0-1 | ok |
| Bitwise | 4 (0-3) | 0-2 | slot 3 'Bits' dead, see M7 |
| Temporal | 7 (0-5,7) | 0-5,7 | ok |
| RingSat | 7 | 0-6 | ok |
| Feedback | 9 | 0-8 | ok |
| AllpassRes | 7 (0-5,7) | 0-5,7 | ok |
| Chaos | 7 | 0-6 | ok |
| Formant | 8 | 0-7 | ok |
| Granular | 7 | 0-6 | ok |
| Spectral | 8 | 0-7 | ok |
| Fractal | 8 | 0-7 | ok |
| Stochastic | 8 | 0-7 | ok |

#### types table 3

| Type | Oversample factor | Spec rows (specs-overview.md:340-358, dsp-details.md:565-586) |
|---|---|---|
| SoftClip | 2 | listed |
| HardClip | 4 | listed (with Fuzz) |
| Tube | 2 | MISSING from both spec tables |
| Tape | 2 | MISSING from both spec tables |
| Fuzz | 4 | listed |
| AsymmetricFuzz | 4 | MISSING from both spec tables (roadmap.md:1121 says 4x) |
| Wavefolders (3) | 4 | listed |
| Rectifiers (2) | 4 | listed |
| Bitcrush/SampleReduce/Quantize | 1 | listed as Digital D12-D14 |
| Temporal 2, RingSat 4, Feedback 2, Aliasing 1, Bitwise 1, Chaos 2, Formant 2, Granular 2, Spectral 1, Fractal 2, Stochastic 2, Allpass 4 | match code | listed |
| Family morph table FR-MORPH-004 (specs-overview.md:242-250) | Digital = D12-D14 only; no Dynamic/Hybrid/Experimental rows | conflicts with FR-MORPH-006 and getFamily |

#### types mismatches (18)

1. **M1 Feedback (D17) is Dynamic in getFamily but Hybrid in getCategory, docs and spec category tables**
   - Where: plugins/disrumpo/src/dsp/distortion_types.h:328-330 (case FeedbackDist -> DistortionFamily::Dynamic) vs distortion_types.h:136-138 (case FeedbackDist -> DistortionCategory::Hybrid); plugins/disrumpo/docs/index.html:178 (<strong>Feedback</strong> under Hybrid); specs/Disrumpo/specs-overview.md:198 (D17 Feedback | Hybrid)
   - Detail: getFamily comment at distortion_types.h:278 says 'Dynamic (D15, D17)' and specs-overview.md:269 (FR-MORPH-006) says 'Dynamic | D15, D17', while the category tables say Hybrid D16-D17, D26. Docs say 'Types within the same family can be morphed smoothly' and list Feedback under Hybrid, so the docs use category, the morph engine uses family.
2. **Spec morph family table (FR-MORPH-004) disagrees with getFamily**
   - Where: specs/Disrumpo/specs-overview.md:242-250 (FR-MORPH-004: Digital | D12-D14 ; only 4 rows) vs plugins/disrumpo/src/dsp/distortion_types.h:320-325 (Digital includes Aliasing and BitwiseMangler)
   - Detail: FR-MORPH-004 lists Digital as D12-D14 and omits Dynamic, Hybrid and Experimental. getFamily maps D18 and D19 to Digital and D15-D26 to Dynamic/Hybrid/Experimental. FR-MORPH-006 (specs-overview.md ~ line 262-270) matches getFamily, so the two spec tables disagree with each other.
3. **band_strip_test canonical type-name array order differs from enum order for indices 14-25**
   - Where: plugins/disrumpo/tests/unit/band_strip_test.cpp:44-55 (index 14 Temporal, 15 Ring Saturation, 16 Feedback, 17 Aliasing, 18 Bitwise Mangler, 19 Chaos, 20 Formant, 21 Granular, 22 Spectral, 23 Fractal, 24 Stochastic, 25 Allpass Resonant) vs distortion_types.h:58-71 (Aliasing=14, BitwiseMangler=15, Temporal=16, RingSaturation=17, FeedbackDist=18, AllpassResonant=19, Chaos=20, Formant=21, Granular=22, Spectral=23, Fractal=24, Stochastic=25)
   - Detail: The test comment says the array is 'from dsp-details.md (Appendix B)' which is D-number order, not enum or menu order. Its assertions band_strip_test.cpp:161-163 (index 23 Fractal, 24 Stochastic, 25 Allpass) do not match the enum where Fractal=24, Stochastic=25, Allpass=19. The menu (parameter_registration.cpp:774-799) follows the enum order, so the test pins a different ordering from what ships.
4. **Serge Fold model dropdown label order does not match the model table it indexes**
   - Where: plugins/disrumpo/resources/editor.uidesc:974 (menu-items="Simple,Serge,Buchla,Lockhart") vs plugins/disrumpo/src/dsp/distortion_adapter.cpp:676-684 (models[] = {Serge, Simple, Buchla259, Lockhart}, comment line 677 '0=Serge, 1=Simple')
   - Detail: Menu index 0 displays 'Simple' but selects WavefolderModel::Serge; index 1 displays 'Serge' but selects Simple. The processor maps the slot to foldModel 0-3 (processor_params.cpp:107-112), so the label and the behaviour are swapped for two entries. Spec ui-mockups.md:412 lists only 'Lockhart, Buchla259' for this control, and specs-overview.md:89 names D09 as Lockhart/Buchla259.
5. **Temporal mode list: UI has 4 modes with different names, adapter comment and spec list 3 modes, and enum has 4**
   - Where: plugins/disrumpo/resources/editor.uidesc:1034 (menu-items="Follow,Inverse,Transient,Hyst") vs dsp/include/krate/dsp/processors/temporal_distortion.h:51-70 (EnvelopeFollow=0, InverseEnvelope=1, Derivative=2, Hysteresis=3) vs plugins/disrumpo/src/dsp/distortion_adapter.h:127 (comment '0=Envelope, 1=Inverse, 2=Derivative') vs specs/Disrumpo/specs-overview.md:219 ('Mode (Envelope/Inverse/Derivative)') vs specs/Disrumpo/dsp-details.md:451-456 (4 modes, including Hysteresis)
   - Detail: processor_params.cpp:156 maps slot0 to 0-3 and distortion_adapter.cpp clamps 0..3, so Hysteresis (3) is reachable from the UI, but the adapter comment and specs-overview.md:219 list only 3 modes. UI label 'Transient' is the DSP's 'Derivative'; 'Follow' is 'EnvelopeFollow'.
6. **Temporal Attack range: mapping [1,500] ms vs spec and adapter [1,100] ms**
   - Where: plugins/disrumpo/src/processor/processor_params.cpp:159 (p.attackMs = 1.0f + slots[3] * 499.0f; comment [1,500]) vs specs/Disrumpo/specs-overview.md:219 (Attack (1-100ms)) and specs/Disrumpo/dsp-details.md:219 (attackMs [1, 100] ms) and plugins/disrumpo/src/dsp/distortion_adapter.h:125 (attackMs [1, 100] ms)
   - Detail: The adapter forwards p.attackMs unchanged into temporal_.setAttackTime (distortion_adapter.cpp ~line 752), so the spec's 1-100 ms range is exceeded by 5x at the top end.
7. **Temporal Release range: mapping [10,5000] ms vs spec and adapter [10,500] ms**
   - Where: plugins/disrumpo/src/processor/processor_params.cpp:160 (p.releaseMs = 10.0f + slots[4] * 4990.0f; comment [10,5000]) vs specs/Disrumpo/dsp-details.md:220 (releaseMs [10, 500] ms), specs/Disrumpo/specs-overview.md:219 (Release (10-500ms)), plugins/disrumpo/src/dsp/distortion_adapter.h:126 (releaseMs [10, 500] ms)
   - Detail: The spec and adapter header say 10-500 ms; the processor delivers up to 5000 ms, and it is routed unchanged to temporal_.setReleaseTime (distortion_adapter.cpp ~line 753).
8. **Chaos attractor speed: mapping [0.01,100] vs spec and adapter [0.1,10]**
   - Where: plugins/disrumpo/src/processor/processor_params.cpp:207 (p.attractorSpeed = 0.01f + slots[1] * 99.99f; comment [0.01,100]) vs specs/Disrumpo/dsp-details.md:236 (attractorSpeed [0.1, 10]), specs/Disrumpo/specs-overview.md:221 (Attractor Speed (0.1-10.0)), plugins/disrumpo/src/dsp/distortion_adapter.h:156 ([0.1, 10])
   - Detail: The processor's [0.01,100] range is 10x wider at the low end and 10x wider at the high end than the spec, and chaos_.setAttractorSpeed(p.attractorSpeed) at distortion_adapter.cpp ~line 853 takes it unclamped.
9. **Formant shift: mapping [-24,+24] semitones vs spec and adapter [-12,+12]**
   - Where: plugins/disrumpo/src/processor/processor_params.cpp:219 (p.formantShift = (slots[1] * 2.0f - 1.0f) * 24.0f; comment [-24,24]) vs specs/Disrumpo/dsp-details.md:238 (formantShift [-12, +12] semitones), specs/Disrumpo/specs-overview.md:221 (Formant Shift (-12 to +12 semitones)), plugins/disrumpo/src/dsp/distortion_adapter.h:162 ([-12, 12] semitones)
   - Detail: The value is passed unchanged to formant_.setFormantShift (distortion_adapter.cpp ~line 863), which takes semitones (dsp/include/krate/dsp/processors/formant_distortion.h:238), so the shift reaches 2 octaves in each direction, not the spec's 1.
10. **Aliasing frequency shift: mapping [-5000,+5000] Hz vs spec and adapter [-1000,+1000] Hz**
   - Where: plugins/disrumpo/src/processor/processor_params.cpp:194 (p.freqShift = (slots[1] * 2.0f - 1.0f) * 5000.0f; comment [-5000,5000]) vs specs/Disrumpo/dsp-details.md:229 (freqShift [-1000, 1000] Hz) and plugins/disrumpo/src/dsp/distortion_adapter.h:149 ([-1000, 1000] Hz)
   - Detail: Routed unchanged into aliasing_.setFrequencyShift (distortion_adapter.cpp ~line 729-730), so the effective shift is 5x the documented range. The processor's downsample range [2,32] (line 193) also has no spec row, since the Aliasing spec only defines freqShift.
11. **Wavefold Folds range: mapping [1,12] vs spec and adapter [1,8]**
   - Where: plugins/disrumpo/src/processor/processor_params.cpp:90, 99, 108 (p.folds = 1.0f + slots[0] * 11.0f; comments [0,1] -> [1,12]) vs specs/Disrumpo/specs-overview.md:217 (Folds (1-8)), specs/Disrumpo/dsp-details.md:208 (folds [1, 8]), plugins/disrumpo/src/dsp/distortion_adapter.h:99 (Fold count [1, 8])
   - Detail: Applies to SineFold, TriangleFold and SergeFold. The wavefolder receives setFoldAmount(p.folds) (distortion_adapter.cpp:~632-640, 657-661, 668-672, 679-683), so fold counts 9-12 are reachable and outside the spec.
12. **Saturation Bias: Fuzz and AsymFuzz store unipolar [0,1] in the field the spec and adapter define as [-1,1]**
   - Where: plugins/disrumpo/src/processor/processor_params.cpp:73 and 81 (p.bias = slots[0]; // Fuzz bias is unipolar [0,1]) vs specs/Disrumpo/specs-overview.md:216 (Saturation: Bias (-1.0 to +1.0)), specs/Disrumpo/dsp-details.md:204 (bias 'Asymmetry bias [-1, 1]'), plugins/disrumpo/src/dsp/distortion_adapter.h:81 ('Asymmetry [-1, 1]')
   - Detail: Tube and Tape convert to bipolar (processor_params.cpp:~50, ~63 as slots*2-1), but Fuzz and AsymFuzz do not. The same DistortionParams.bias field therefore means different ranges per type, and fuzz_.setBias(p.bias) (distortion_adapter.cpp ~line 636 and 645) gets [0,1].
13. **Fold Symmetry: mapping [-1,1] vs spec and adapter [0,1]**
   - Where: plugins/disrumpo/src/processor/processor_params.cpp:91, 100, 109 (p.symmetry = slots[1] * 2.0f - 1.0f) vs specs/Disrumpo/dsp-details.md:210 (symmetry [0, 1]), specs/Disrumpo/specs-overview.md:217 (Symmetry (0.0-1.0)), plugins/disrumpo/src/dsp/distortion_adapter.h:101 ([0, 1])
   - Detail: The default in the header is 0.5 (the neutral point for [0,1]) but the processor writes bipolar values, so the neutral point is 0.0 after mapping. wavefolder_.setSymmetry(p.symmetry) receives the bipolar value.
14. **Fractal feedback range: mapping [0,0.5] vs adapter comment [0,1]**
   - Where: plugins/disrumpo/src/processor/processor_params.cpp:261 (p.fractalFB = slots[5] * 0.5f; comment [0,1] -> [0,0.5]) vs plugins/disrumpo/src/dsp/distortion_adapter.h:194 (Fractal feedback [0, 1])
   - Detail: Only the adapter header comment disagrees with the mapping. No spec row covers fractal feedback, so the header is the only spec-side reference.
15. **Bitwise Mangler 'Bits' slider has no DSP consumer**
   - Where: plugins/disrumpo/resources/editor.uidesc:1111-1113 (control-tag="Band.NodeShape3" in TypeParams_Bitwise, CSlider 'Bits') vs plugins/disrumpo/src/processor/processor_params.cpp:197-202 (reads only slots 0-2) and plugins/disrumpo/src/dsp/distortion_adapter.h:121 (bitwiseBits 'reserved, unused')
   - Detail: The UI shows 4 Bitwise shape controls but the mapping consumes 3, so the Bits slider changes a stored value that nothing reads. Tests and specs do not mention it.
16. **KrateDSP component names for SoftClip and HardClip do not match the code**
   - Where: specs/Disrumpo/roadmap.md:1116-1117 (D01 SaturationProcessor::Tape; D02 SaturationProcessor::Digital) and specs/Disrumpo/specs-overview.md:81 (D01 SaturationProcessor::Tape, Waveshaper::Tanh) vs plugins/disrumpo/src/dsp/distortion_adapter.cpp:261-285 (SoftClip: inline tanh knee) and 287-307 (HardClip branch), with saturation_ prepared/reset at distortion_adapter.cpp:34 and 106 but never processed (distortion_adapter.h:370)
   - Detail: The adapter's saturation_ member is dead (prepared, reset, never called). The spec table names it as the implementation of D01 and D02. The roadmap also names FuzzProcessor and others, which do match.
17. **Node colour legend is by Category in the spec, but code keys colour by Family**
   - Where: specs/Disrumpo/custom-controls.md:963-964 (Dynamic (D15) yellow; Hybrid (D16-D17, D26) red) vs plugins/disrumpo/src/controller/views/morph_pad.cpp:48-56 (kCategoryColors keyed by DistortionFamily; Dynamic=FFCB6B, Hybrid=FF5370) and distortion_types.h:328-330
   - Detail: Under the code's family mapping, Feedback (D17) is Dynamic and gets yellow, while the spec table gives D17 red (Hybrid). The hex values match, so the disagreement is only the D17 key. getCategoryColor has no production caller in plugins/disrumpo/src (only the definition in morph_pad.h:246 and tests/integration/node_editor_test.cpp:234-235).
18. **Spec text says 24 of 26 distortion types have dedicated implementations**
   - Where: specs/Disrumpo/specs-overview.md:126 ('24 of 26 distortion types have dedicated implementations') vs specs/Disrumpo/roadmap.md:77 ('24 of 26 distortion types exist in KrateDSP library')
   - Detail: Both count KrateDSP coverage, not the 26 in the code. Included only for completeness. The adapter covers all 26 in processRaw (distortion_adapter.cpp:261-573) and the enum has all 26. This is a stale-count note, not a code defect.

### Inventory: Presets, versions and docs claims

**Summary.** Cross-referenced Disrumpo presets (filesystem, test expectations, generator, preset browser config, CHANGELOG), versions (version.json, version.h, win32resource.rc, CHANGELOG, update config, installers), docs/index.html numeric and feature claims vs code constants, and plugins/disrumpo/CLAUDE.md vs the filesystem. Presets: 121 .vstpreset files on disk; generator emits 119; test expectation map sums to 121 but its comment says 120; docs claim 120 in three places. Versions: version.json, version.h, win32resource.rc and the CHANGELOG top entry all agree on 0.9.11. Docs: 26 types and 7 families agree with code; the oversampling description and the Feedback family placement disagree. CLAUDE.md: all listed directories and files exist; controller.cpp is 1991 lines vs the claimed ~2004 (within 20 percent).

#### presets-versions table 1

| Category | On disk (plugins/disrumpo/resources/presets) | Generator PresetDef blocks (tools/disrumpo_preset_generator.cpp) | Test expected (tests/integration/preset/factory_preset_validation_test.cpp:256-259) |
|---|---|---|---|
| Init | 5 | 4 (comment at :826 'INIT (4 presets)') | 5 |
| Sweep | 15 | 15 | 15 |
| Morph | 15 | 15 | 15 |
| Bass | 10 | 10 | 10 |
| Leads | 10 | 10 | 10 |
| Pads | 10 | 10 | 10 |
| Drums | 10 | 10 | 10 |
| Experimental | 16 | 15 (comment at :2662 'EXPERIMENTAL (15 presets)') | 16 |
| Chaos | 10 | 10 | 10 |
| Dynamic | 10 | 10 | 10 |
| Lo-Fi | 10 | 10 | 10 |
| TOTAL | 121 | 119 | 121 (test header comment says 120 at :4 and :8) |

#### presets-versions table 2

Version matrix: plugins/disrumpo/version.json:2 version = 0.9.11 | src/version.h:13-22 SUB 9, RELEASE 11, BUILD 1, VERSION_STR 0.9.11 | resources/win32resource.rc:36 and :41 = 0.9.11.1 | CHANGELOG.md:9 '## [0.9.11] - 2026-11-02' | src/update/disrumpo_update_config.h:11 currentVersion = VERSION_STR (no literal) | installers/windows/setup.iss:13 default 0.0.0, overridden by release workflow /DVersion | docs/index.html uses {{VERSION}} placeholders only (no literal version).

#### presets-versions table 3

Docs vs code claims: docs/index.html:113 and :130 say 26 algorithms in 7 families (code: COUNT = 26 at src/dsp/distortion_types.h:~82, kDistortionFamilyCount = 7 at :284, consistent). docs/index.html:118 says intelligent oversampling 'automatically selects 2x or 4x' (code: getRecommendedOversample returns 1 for Bitcrush, SampleReduce, Quantize, Aliasing, BitwiseMangler, Spectral at distortion_types.h:189; oversampling_utils.h:52 documents 1, 2, or 4). docs/index.html:121, :7, :14, :28, :36 say 120 factory presets across 11 categories (disk has 121; generator 119). docs/index.html:11 categories count 11 consistent with 11 subdirectories.

#### presets-versions mismatches (11)

1. **Factory preset total on disk does not match the test's comment**
   - Where: plugins/disrumpo/resources/presets/* (121 files) vs plugins/disrumpo/tests/integration/preset/factory_preset_validation_test.cpp:4 and :8
   - Detail: disk: 121 .vstpreset; test comment: 'All 120 factory presets' / 'SC-004: All 120 factory presets pass validation'
2. **Test's own expected category counts sum to 121, not 120**
   - Where: factory_preset_validation_test.cpp:256-259 vs factory_preset_validation_test.cpp:4
   - Detail: expected map {Init 5, Sweep 15, Morph 15, Bass 10, Leads 10, Pads 10, Drums 10, Experimental 16, Chaos 10, Dynamic 10, Lo-Fi 10} = 121; comment says 120
3. **Generator Init count disagrees with disk and test**
   - Where: tools/disrumpo_preset_generator.cpp:826 ('INIT (4 presets)', 4 blocks at :829-844) vs factory_preset_validation_test.cpp:257 ({"Init", 5}) and disk Init dir
   - Detail: generator 4 Init presets (Init 1/2/3/4 Bands); disk 5; test expects 5
4. **Generator Experimental count disagrees with disk and test**
   - Where: tools/disrumpo_preset_generator.cpp:2662 ('EXPERIMENTAL (15 presets)') vs factory_preset_validation_test.cpp:259 ({"Experimental", 16}) and disk Experimental dir
   - Detail: generator 15; disk 16; test expects 16
5. **Generator total does not match shipped factory total**
   - Where: tools/disrumpo_preset_generator.cpp (119 PresetDef blocks) vs plugins/disrumpo/resources/presets (121 files)
   - Detail: generator emits 119 presets; repo ships 121; the two extra on disk are one Init and one Experimental
6. **docs claim 120 factory presets**
   - Where: plugins/disrumpo/docs/index.html:7, :14, :28, :36, :121 vs disk total
   - Detail: docs: '120 factory presets across 11 categories'; disk: 121
7. **CHANGELOG historical preset count references**
   - Where: plugins/disrumpo/CHANGELOG.md:54 (0.9.7 entry) and :123 (0.9.0 entry) vs disk total
   - Detail: CHANGELOG:54 'regenerated all 119 factory presets'; CHANGELOG:123 '120 factory presets across 11 categories'; disk: 121 (historical entries, but the 0.9.x claims never reconcile with the current 121)
8. **Docs oversampling claim disagrees with code values**
   - Where: plugins/disrumpo/docs/index.html:118 vs plugins/disrumpo/src/dsp/distortion_types.h:189 and src/dsp/oversampling_utils.h:52
   - Detail: docs: 'automatically selects 2x or 4x oversampling per algorithm'; code: getRecommendedOversample returns 1 for Bitcrush/SampleReduce/Quantize/Aliasing/BitwiseMangler/Spectral, 2 for 10 types, 4 for 10 types; oversampling_utils.h:52 says 1, 2, or 4
9. **Feedback distortion listed under Hybrid in docs but family is Dynamic in code**
   - Where: plugins/disrumpo/docs/index.html:174 (<h3>Hybrid</h3>) and :178 (Feedback bullet) vs plugins/disrumpo/src/dsp/distortion_types.h:327-330 (getFamily: FeedbackDist -> DistortionFamily::Dynamic)
   - Detail: docs group Feedback in Hybrid (7 families list); code family is Dynamic ('Dynamic (D15, D17)'), while category is Hybrid (distortion_types.h:137-139, D16-D17 Hybrid category)
10. **Feedback category vs family mapping is internally inconsistent in code**
   - Where: plugins/disrumpo/src/dsp/distortion_types.h:90 (DistortionCategory::Hybrid comment 'D16-D17, D26') vs distortion_types.h:278 and :327-330 (family Dynamic 'D15, D17')
   - Detail: same type D17 is category Hybrid and family Dynamic
11. **Docs heading 'Hybrid' vs family table mismatch is the only docs-to-code family disagreement; other families match**
   - Where: plugins/disrumpo/docs/index.html:132-190 vs distortion_types.h:273-281
   - Detail: Saturation/Wavefold/Rectify/Digital/Experimental/Hybrid groupings match code; Dynamic heading lists only Temporal (docs:168-172) while code Dynamic family also has FeedbackDist


## Appendix A — Refuted findings

Nine findings received two or more refute votes in the three-lens verification and are closed. They are listed so nobody re-finds them; the one-line reason is the verifiers' consensus.

| Id | Title | Location | Refutation reason |
|---|---|---|---|
| D-004 | disconnect() destroys dataExchange_ that the audio thread dereferences in sendSpectrumBlock with no synchronisation | [plugins/disrumpo/src/processor/processor_messaging.cpp:74-84](plugins/disrumpo/src/processor/processor_messaging.cpp#L74-L84), [:124-146](plugins/disrumpo/src/processor/processor_messaging.cpp#L124-L146) | The VST3 lifecycle tears down setProcessing(false) -> setActive(false) -> disconnect -> terminate, and setActive(false) already calls onDeactivate (processor.cpp:173-174); the SDK's own onDisconnect frees the queue with no synchronisation (dataexchange.cpp:328-334, hostcheckerprocessor.cpp:877-882) and every Krate plugin uses the same onDisconnect+reset pattern, so the race needs a non-conforming host and is not a Disrumpo defect. |
| D-060 | Mode sub-controller destructors call unregisterControlListener on an already-destroyed child control (use-after-free on every type switch and on editor close) | [plugins/disrumpo/src/controller/sub_controllers.h:283-286](plugins/disrumpo/src/controller/sub_controllers.h#L283-L286), [:368-371](plugins/disrumpo/src/controller/sub_controllers.h#L368-L371), [:444-447](plugins/disrumpo/src/controller/sub_controllers.h#L444-L447) | Under VST3Editor (controller.cpp:730, observer at vst3editor.cpp:1308) the attached-removal path runs CViewContainer::removed -> CFrame::onViewRemoved -> VST3Editor::onViewRemoved (vst3editor.cpp:776-781), which deletes the sub-controller while the template root still owns its child controls; CView::beforeDelete then finds no controller attribute, so both the type-switch (uiviewswitchcontainer.cpp:126-138, cviewcontainer.cpp:565-574) and editor-close (vst3editor.cpp:1407-1408) paths unregister on live memory. |
| D-111 | Unused <windows.h> include in the controller TU (Windows-only macro pollution) | [plugins/disrumpo/src/controller/controller.cpp:57-59](plugins/disrumpo/src/controller/controller.cpp#L57-L59) | The include at :58-60 is redundant but changes nothing: controller.cpp:36 includes display/display_bridge_log.h, which already includes <windows.h> under _WIN32 with WIN32_LEAN_AND_MEAN and NOMINMAX (display_bridge_log.h:18-27, permitted guarded debug logging), and vstguibase.h:74-79 defines NOMINMAX too, so the preprocessed TU is identical with the block deleted; a tidy-up nit, not a portability defect. |
| D-146 | Bands misalign at summation by 3-4 samples depending on type: the mis-rated tone LP is applied to some types and skipped for digital ones | [plugins/disrumpo/src/dsp/distortion_adapter.cpp:225-232](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L225-L232) | The misalignment exists but the mechanism is wrong: a one-pole LP's impulse response peaks at n = 0 and cannot move the probe's peak index, HardClip and SoftClip both apply the tone filter yet differ by 3 samples, and the delays (4/1/0 samples) follow the oversampling factor (4x/2x/1x) of the probe's processBlock path, i.e. the ZeroLatency IIR oversampler group delay (band_processor.h:99-107), which the plugin's live per-sample path never runs. See Appendix C for the critic's objection. |
| D-147 | BandProcessor::getLatency reports 0 while the 'ZeroLatency' IIR 4x oversampling path adds 4.4 samples of group delay | [plugins/disrumpo/src/dsp/band_processor.h:374-379](plugins/disrumpo/src/dsp/band_processor.h#L374-L379) | Unreachable and intentional: Processor::process never calls processBlock (processor.cpp:487/:494 use the per-sample path), so no oversampler runs in production; FR-018/FR-019 of specs/_archive2_/009-intelligent-oversampling/spec.md:151-152 require 0 reported latency for IIR mode and tests/dsp/oversampling_performance_tests.cpp:32-58 pins it; a minimum-phase IIR has frequency-dependent group delay that no integer setLatencySamples could compensate. A design note for the task that wires oversampling in (D-017/D-027). |
| D-148 | AsymmetricFuzz, BitwiseMangler and Tape emit low-level output from digital silence (-86 to -89 dBFS) | [plugins/disrumpo/src/dsp/distortion_adapter.cpp:320-341](plugins/disrumpo/src/dsp/distortion_adapter.cpp#L320-L341) | The selfnoise probe measures 0.5-1.0 s after a fresh activate (audit_probes.cpp.txt:500-511) and reads the DC blocker settling a constant offset (all three types map 0 to a constant that then decays through a 10 Hz DCBlocker running slow at the 8x-prepared rate); the same run's tail probe on the same types reads -134.6 / -157.5 / -157.3 dBFS (probe-results.txt:442/444/462), so there is no persistent noise floor; the finding admitted the cause was not isolated. |
| D-150 | uidesc overlap: expanded band strip shadow and border containers overlap each other and the band column containers | [plugins/disrumpo/resources/editor.uidesc:577-606](plugins/disrumpo/resources/editor.uidesc#L577-L606), [:545](plugins/disrumpo/resources/editor.uidesc#L545), [:555](plugins/disrumpo/resources/editor.uidesc#L555) | Intentional drop-shadow + border + content layering documented at editor.uidesc:575-576 and toggled by AnimatedExpandController (animated_expand_controller.cpp:167/214/233/240); the expanded strips are visible="false" overlays meant to cover the band columns; the finding itself says "No user-visible defect found". Lint noise, not a plugin defect. |
| D-151 | uidesc overlap: SweepIndicator stacked on SpectrumDisplay, and SpectrumMode segment button over both | [plugins/disrumpo/resources/editor.uidesc:531-533](plugins/disrumpo/resources/editor.uidesc#L531-L533) | Intentional overlay: SweepIndicator calls setTransparency(true) (sweep_indicator.cpp:19) and overrides no mouse handler, so clicks pass to SpectrumDisplay (spectrum_display.h:146-149, hitTestDivider at spectrum_display.cpp:589-592 tests x only); the 20 px segment button leaves 98 px of every divider reachable; the finding says "No functional defect found". Lint allowlist item. |
| D-153 | uidesc overlap: hidden 1x1 DisplayedType switch slider and NodeEditorBorder overlap their sibling containers (intentional pattern) | [plugins/disrumpo/resources/editor.uidesc:1334](plugins/disrumpo/resources/editor.uidesc#L1334) | NodeEditorBorder is the first child (lowest z-order), calls setTransparency(true) (node_editor_border.cpp:36) and overrides no mouse handler, so it cannot steal clicks; the 1x1 transparent CSlider is the standard template-switch-control for the UIViewSwitchContainer and touches none of its controls; the finding itself calls the pattern intentional. Lint noise. |

Two of these refutations (D-146, D-148) are on mechanism rather than on the existence of the measured effect; Appendix C records the critic's objection to D-146 and the measurement that would settle it.

## Appendix B — Unverified findings

None. Every finding received three verifier votes.


## Appendix C — Critic gaps (verbatim)

The critic round recorded 15 gaps: areas no finder covered, measurements not taken, claims not verified, and refutations it considered suspicious. They are reproduced verbatim.

1. **uncovered-area** — Denormals/FTZ: the brief lists 'denormals in long-tailed filters' as a hazard, but no finding and no probe touches it. grep for _MM_SET_FLUSH_ZERO_MODE/FTZ/ScopedNoDenormals over plugins/disrumpo/src returns nothing (and nothing in any plugins/*/src/processor/*.cpp either), so Processor::process (processor.cpp:180-520) relies on the host for FTZ/DAZ. With the 10 Hz DC blockers running at an effective 1.25 Hz (D-009) and AllpassSaturator/FeedbackDist tails, decays into the denormal range are long. dsp-numerics produced nothing here and no probe timed a decaying tail.

2. **uncovered-area** — Rectified_Drums renders at -62 dBFS RMS (probe-results.txt:301) but no finding root-causes it. HalfRectify uses an ABSOLUTE amplitude threshold (distortion_adapter.cpp:441-442 `(input >= thresh) ? input : 0`), so a -12 dBFS signal below the threshold is silenced, and the rectify smooth filter cutoff `20000*pow(500/20000, smoothness)` (distortion_adapter.cpp:664-689) is computed for the 8x rate and runs at 1x, i.e. 8x lower in real Hz. The preset (tools/disrumpo_preset_generator.cpp:2623-2636) uses HalfRectify thresh 0.3 / smooth 0.15 on the band that holds 440 Hz. Same hazard class as D-145 (SergeFold nearly silent), whose cause the verifiers also left open.

3. **uncovered-area** — Inventory mismatches that reach the user but were never elevated to findings: Serge Fold model menu order (editor.uidesc:974 'Simple,Serge,Buchla,Lockhart' vs distortion_adapter.cpp:676-684 models[]={Serge,Simple,...}: labels swapped for two entries; D-135 hides the menu today, but fixing D-135 exposes the swap); Temporal mode labels 'Follow,Inverse,Transient,Hyst' vs enum names and adapter/spec listing only 3 modes (uidesc:1034, temporal_distortion.h:51-70, distortion_adapter.h:127); Bitwise 'Bits' slider bound to Band.NodeShape3 with no consumer (uidesc:1111-1113 vs processor_params.cpp:197-202, distortion_adapter.h:121 'reserved, unused').

4. **uncovered-area** — Fourteen mod-system parameters are registered with the base Parameter overload carrying unit strings ('ms','Hz','%','deg') but no range (parameter_registration.cpp:378, 435-439, 452-454, 482-506), so hosts and the generic slider text show the raw normalized 0..1 with a unit suffix while the processor denormalizes to 1..100 ms, 0.1..50 Hz, x360 deg (processor_params.cpp:591, 643-746). D-101 covers only sweep/band-count; Route Amount shows -1..1 as '%' (parameter_registration.cpp:602-603). No finding.

5. **uncovered-area** — Shared-DSP leads from the maps that no finder turned into findings (cross-plugin): Crossover3Way/4Way kAllpassQ = 0.5 with comment 'matches LR4 phase' (crossover_filter.h:423, 643; LR4 sum needs Q 0.7071, as Disrumpo's own network uses) with fractal_distortion.h as a consumer; RingSaturation returns NaN input unchanged (ring_saturation.h ~257); FormantDistortion::process has no finiteness guard (formant_distortion.h:169-195); Waveshaper Diode is documented UNBOUNDED (waveshaper.h:44, :58) and is reachable through curve index 7 in Temporal/RingSat/Spectral/Fractal/AllpassResonant (distortion_adapter.cpp:761-765, 780-784, 833-837, 899, 934) with drive up to 10 and feedback loops; lfo.h:477 `randomState_ * 48271u % 2147483647u` multiplies in uint32 (overflows before the modulus, can reach state 0 and stick) and is used by Profundum's morph modulator S&H/SmoothRandom; fastTanh returns numeric_limits::quiet_NaN() at fast_math.h:68 (ratcheted in tools/lint-nonfinite-symbols-baseline.json:3) and is included by 18 shared headers plus distortion_adapter.cpp.

6. **missing-measurement** — Oversampler Economy/ZeroLatency IIR attenuation: the shared-infra map computes about -8 dB at 0.5 fs and -14 dB at 0.55 fs for the 8th-order Butterworth at 0.45 fs (oversampler.h:495-519) against the header's '48 dB/oct' claim. Nothing measured it. It matters for the fix plan: the fix for D-017/D-027 is to wire processBlock in, and that would ship this filter as the only anti-alias protection.

7. **unverified-claim** — The probes lane summary says '35 presets lose audio in a getState/setState round trip'. The preset probe lines only record roundtrip_maxdiff (e.g. Pitch_Tracker 1.25, Chaos_Morph_4 0.226, Chaotic_Sweep 0.163, Pitch_Drive 0.0842; probe-results.txt:286-309); no post-round-trip RMS was measured, and the divergent presets are the ones driven by chaos/pitch sources, where a 1e-5 drift in a saved parameter (D-091's hand-rounded log constants; the preset-rt-bytes lines put the first unstable byte at offsets 92, 136 and 149) diverges a chaotic trajectory without any state being lost. No finding decodes those offsets to fields or shows that audio is actually lost.

8. **missing-measurement** — D-012/D-120 (stale transitionGains_ when the cursor is exactly on a node): the morph-onnode probe (probe-results.txt morph-onnode:* lines, all 'ok') only PARKED the cursor at exact/near node positions and measured a static level. The failing scenario in the finding (cross-family, smoothing 0, jump from node B to exactly node A so the previous w<=0.001 gain of 0 is kept) was never rendered. The three D-120 verifiers disagree on reachability (first says the scenario cannot happen, the other two say it is reachable with defaults). A measurement would settle it.

9. **missing-measurement** — No allocation counter and no CPU number back the critical RT findings. D-001/D-002/D-007/D-106 (Spectral re-prepare mallocs per sample) and D-005 (per-sample routeParamsToProcessor) were confirmed by reading only; D-127 notes no allocation tracking exists in the suite, run-cpu-tests.js was not run, and the spec NFR 'CPU < 15 % at 4 bands' is unmeasured for the default same-family path.

10. **missing-measurement** — No NaN/Inf was ever injected at the processor input. The 'no NaN or Inf anywhere' probe verdict used finite input only. D-013 (EnvelopeFollower latch, sweep centre collapses to 0 Hz) and the Biquad state-wipe click (biquad.h:354) are therefore unmeasured consequences, and D-129 records that the suite has no injection test either.

11. **suspicious-refutation** — D-146 was refuted although all three verifiers agree that 'the bands do arrive at different times'. The refutation is on mechanism (tone LP cannot move an impulse peak; the probe's drive-0 reference and processBlock path point at the oversampler), not on existence. The probe ran BandProcessor::processBlock, which the plugin never calls, so the live per-sample path (processor.cpp:467-504) with mixed types per band was never measured for inter-band alignment. Needs a tie-break plus a live-path measurement.

12. **suspicious-refutation** — D-021 (centre pan -3 dB, neutral settings not unity): the single refute vote calls the pan law 'a recorded design decision (FR-022)'. specs/Disrumpo/specs-overview.md has no FR-022 and no pan-law text (grep finds only 'Pan: -100% L to +100% R' at :173 and :402); the only FR-022 reference is the code comment in band_processor.h:499. The probe measured -3.01 dB at defaults and a -16.6 dB notch at mix 50 %. The finding stays PLAUSIBLE but the refute vote cites intent without a spec line.

13. **unverified-claim** — Verifier notes that disagree on CONFIRMED findings and need a synthesizer ruling: D-026, D-081 and D-098 (two of three verifiers call the Feedback and Granular parts overstated; only the Spectral latency is solid); D-066 (one verifier wants low: no audible effect because the link is not in the DSP at all, D-039); D-045 (one verifier says the crash consequence is theoretical and the bounds-check sub-claim does not hold); D-109 (one verifier rules out the SC-001 consequence); D-143 (only part of the DC wording holds). Also an interplay note for the fix plan: the measured 37 ms smoothing (D-138/D-099) is 8x/2 because L and R advance the shared smoothers twice per sample (D-016); fixing D-016 alone makes smoothing 8x slower until D-009 is fixed too.

14. **uncovered-area** — DataExchange opened only when connect() precedes setActive(true): processor_messaging.cpp:61-63 creates the handler in connect() but onActivate is called only from setActive(true) (processor.cpp:167). A host that connects after activation never gets spectrum data. Two D-004 verifiers noted this while refuting D-004; it did not become a finding.

15. **uncovered-area** — Files with no finder citation at all: plugins/disrumpo/src/update/*, entry.cpp, controller/animated_expand_controller.cpp (expand request silently dropped when the band is hidden and never re-applied when band count rises, lines 76-81; I checked the cancel-callback concern from the map and it is NOT a defect: VSTGUI Animator::removeAnimation nulls the notification unless notifyOnCancel is set, animator.cpp:308-309), controller/views/sweep_indicator.cpp, node_editor_border.cpp, dsp/sweep_envelope.h (envelopeLevel_ never cleared on disable; RMS > 1 unclamped before the octave shift).


