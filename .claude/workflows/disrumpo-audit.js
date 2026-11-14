export const meta = {
  name: 'disrumpo-audit',
  description: 'Full audit of the Disrumpo multi-band morphing distortion plugin (plugin code + the KrateDSP it consumes): static bug hunt across 14 dimensions, one capped measurement lane (build, sharded gate, pluginval, audio probes, clang-tidy, lints, portability), 3-lens adversarial verification, a completeness critic round, then a findings report + self-contained fix plan under specs/disrumpo-audit/',
  whenToUse: 'When you want a comprehensive correctness/quality sweep of plugins/disrumpo that ends in specs/disrumpo-audit/report.md (every finding, verdict, evidence) and specs/disrumpo-audit/fix-plan.md (ordered tasks a coding agent executes without re-deriving context). args: { date: "YYYY-MM-DD" (required for the report header), skip?: { build?, probes?, tidy?, portability? }, extra_focus?: "free text the finders must also hunt" }. Nothing commits; the tree is left clean.',
  phases: [
    { title: 'Inventory', detail: 'mechanical cross-reference tables (params, types, presets/versions)', model: 'haiku' },
    { title: 'Understand', detail: 'eight subsystem readers map the plugin and the shared DSP it uses', model: 'sonnet' },
    { title: 'Measure', detail: 'ONE serial lane, capped: lints, build -m:2, gate --share 4, pluginval, audio probes, tidy -Jobs 4, portability', model: 'sonnet' },
    { title: 'Find', detail: 'one finder per bug dimension + a measurement interpreter', model: 'opus' },
    { title: 'Verify', detail: 'three independent skeptics per finding (correctness, context, impact)', model: 'opus' },
    { title: 'Critique', detail: 'completeness critic: uncovered areas, suspicious refutations, follow-up finders', model: 'fable' },
    { title: 'Synthesize', detail: 'report.md + fix-plan.md written to specs/disrumpo-audit/', model: 'fable' },
  ],
}

// ---------------------------------------------------------------------------
// Args
// ---------------------------------------------------------------------------
const DATE = (args && args.date) || 'undated'
const SKIP = (args && args.skip) || {}
const EXTRA_FOCUS = (args && args.extra_focus) || ''
const OUT_DIR = 'specs/disrumpo-audit'
const LOG_DIR = 'f:/tmp/disrumpo-audit'
const CMAKE = '"/c/Program Files/CMake/bin/cmake.exe"'
const BUILD_DIR = 'build/windows-x64-release'
let laneDone = false // set when the measurement lane finishes; verification runs throttled until then

// ---------------------------------------------------------------------------
// Shared context handed to every agent so they judge against the right rules.
// ---------------------------------------------------------------------------
const CONTEXT = `
You are auditing the Disrumpo VST3/AU plugin — a multi-band (1-4 bands)
MORPHING DISTORTION effect (Fx | Distortion, stereo in/out) in the monorepo at
f:/projects/iterum. Unless your task says otherwise this is READ-ONLY: do not
edit files. Read source and tests, reason about correctness, cite file:line.

ARCHITECTURE FACTS (trust these; verify details in code):
- Plugin code: plugins/disrumpo/src/{processor,controller,dsp,preset,update}/,
  plugin_ids.h, entry.cpp. NO parameters/ dir. Tests: plugins/disrumpo/tests/
  ({unit,integration,dsp,approval}), target disrumpo_tests.
- Processor is split across four TUs of ONE class: processor.cpp (lifecycle,
  process(), bus, morph, solo), processor_state.cpp (getState/setState),
  processor_params.cpp (shape-slot map + processParameterChanges),
  processor_messaging.cpp (connect/disconnect, spectrum DataExchange +
  SharedDisplayBridge fallback, mod-offset messages).
- Signal path per block (processor.cpp Processor::process): parameter changes
  -> ModulationEngine (plugins' shared mod system, krate/dsp/systems/
  modulation_engine.h) applies offsets to input/output gain, mix, sweep
  freq/width/intensity, per-band gain/pan/morph XY/drive/mix/tone/bias
  -> sweep LFO + envelope follower modulate the sweep centre frequency
  -> SweepProcessor computes per-band intensity multipliers
  -> input gain -> CrossoverNetwork (cascaded Krate::DSP::CrossoverLR4,
  D'Appolito allpass phase compensation, 1-4 bands, SC-001 says +/-0.1 dB
  reconstruction) -> per band: BandProcessor (MorphEngine over 2-4 MorphNodes
  each holding one of 26 DistortionTypes via DistortionAdapter, intelligent
  oversampling 1x/2x/4x chosen from morph-weighted per-type profiles,
  gain/pan/mute/solo/mix/tone/bias) -> summation -> output gain -> global mix
  -> spectrum block to the editor.
- MorphEngine (plugins/disrumpo/src/dsp/morph_engine.{h,cpp}): inverse-
  distance weights from a 2-D cursor, three MorphModes; same-family nodes
  interpolate parameters through ONE processor, cross-family nodes run in
  parallel with an equal-power crossfade.
- DistortionAdapter (dsp/distortion_adapter.{h,cpp}) wraps 26 KrateDSP
  processors/primitives (saturation, tube, tape, fuzz, wavefolder,
  bit-crusher, temporal, feedback, aliasing, spectral, fractal, formant,
  granular, allpass saturator, sample-rate reducer, ring saturation, bitwise
  mangler, chaos waveshaper, stochastic shaper, ...). Those live in
  dsp/include/krate/dsp/{primitives,processors}/ and are SHARED with other
  plugins: a defect there is CROSS-PLUGIN IMPACT.
- Param IDs are BIT-PACKED 16-bit (plugin_ids.h): nibbles node|band|param.
  Global 0x0Fxx, Sweep 0x0Exx, band-level makeBandParamId -> 0xFbpp,
  node-level makeNodeParamId -> 0xNbpp. Never range-parse them.
- Controller: plugins/disrumpo/src/controller/controller.cpp is a ~2000-line
  monolith plus parameter_registration.cpp, sub_controllers.h,
  visibility_controllers.cpp, morph_link.cpp, animated_expand_controller.cpp,
  keyboard_shortcut_handler.cpp, views/ (morph pad, spectrum, band strips),
  resources/editor.uidesc (1370 lines).
- Presets: 120 factory .vstpreset files under plugins/disrumpo/resources/
  presets/<Category>/; a generator tool exists (disrumpo_preset_generator).
- Reference docs: specs/Disrumpo/*.md (specs-overview FR/SC lists,
  dsp-details param encoding + algorithms, plans-overview signal flow,
  custom-controls, vstgui-implementation). Code comments cite FR-xxx from
  these and from retired per-feature specs (002 band mgmt, 003 distortion
  integration, 005 morph, 007 sweep, 009 oversampling, 010 presets, 012
  progressive disclosure). Version: plugins/disrumpo/version.json (0.9.11),
  CHANGELOG.md beside it.

REAL-TIME AUDIO RULES (violations are bugs): no allocation, lock, exception,
string/IO or unbounded loop on the audio thread (process(), everything a band
or adapter calls per block, setActive while processing); parameters at the
VST boundary are normalized [0,1] and are denormalized in
processParameterChanges; NaN/Inf must never reach the output; processor and
controller never cross-include headers; the macOS leg compiles with
-ffast-math (classify finiteness only via Krate::DSP::detail::isFinite/isNaN,
never std::isnan or a bit-mask clone; no numeric_limits infinity()/quiet_NaN()
constants in fast-math code; clamp pow/sqrt/log arguments at domain edges).

DISTORTION-EFFECT-SPECIFIC HAZARDS: aliasing from under-oversampled shapers;
DC offset from asymmetric shaping (bias) with no DC blocker; level jumps when
the morph cursor crosses a node, when oversampling factor switches, when band
count changes, when a band is muted/soloed, when mix/drive are automated;
latency mismatch between bands with different oversampling factors (phase
smear at summation) and between dry and wet paths; crossover reconstruction
error; self-oscillation/noise with silent input (feedback distortion,
chaos/stochastic shapers); stateful shared processors used for both L and R;
denormals in long-tailed filters; smoother targets set from un-denormalized
values; divide-by-zero in inverse-distance weighting when the cursor sits on a
node.

Report format discipline: every finding must cite a file and line(s) you
actually read (or a measurement you actually ran). If you cannot point at
concrete code or a number, do not report it.
${EXTRA_FOCUS ? `\nEXTRA FOCUS REQUESTED BY THE USER: ${EXTRA_FOCUS}\n` : ''}`

// ---------------------------------------------------------------------------
// Schemas
// ---------------------------------------------------------------------------
const INVENTORY_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['summary', 'tables', 'mismatches'],
  properties: {
    summary: { type: 'string', description: 'what was cross-referenced and the counts on each side' },
    tables: { type: 'array', items: { type: 'string' }, description: 'markdown tables (one string each) of the cross-reference, compact' },
    mismatches: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['what', 'where', 'detail'],
        properties: {
          what: { type: 'string', description: 'one-line statement of the inconsistency' },
          where: { type: 'string', description: 'file:line on each side, e.g. "plugin_ids.h:88 vs parameter_registration.cpp:412"' },
          detail: { type: 'string', description: 'the two values/names that disagree, verbatim' },
        },
      },
    },
  },
}

const MAP_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['summary', 'key_files', 'invariants', 'oddities'],
  properties: {
    summary: { type: 'string', description: 'dense prose map of how this subsystem works (data flow, responsibilities, threading)' },
    key_files: { type: 'array', items: { type: 'string' }, description: 'repo-relative paths with 3-8 word role each, "path — role"' },
    invariants: { type: 'array', items: { type: 'string' }, description: 'rules this subsystem must uphold (with file:line evidence)' },
    oddities: { type: 'array', items: { type: 'string' }, description: 'things that look suspicious/confusing but were not fully investigated — leads for the finder phase, each with file:line' },
  },
}

const LANE_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['ran', 'ok', 'summary', 'items', 'log_path'],
  properties: {
    ran: { type: 'boolean', description: 'false if the step could not run at all (tool missing, build dir absent)' },
    ok: { type: 'boolean', description: 'true if the step found nothing wrong' },
    summary: { type: 'string', description: 'one paragraph: what ran, wall clock, headline result (quote the verdict line verbatim)' },
    items: {
      type: 'array',
      description: 'every defect/red/warning the step produced, one entry each (cluster clang-tidy by check+file)',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['kind', 'file', 'line', 'text'],
        properties: {
          kind: { type: 'string', description: 'build-warning | build-error | test-red | pluginval | tidy | lint | portability | probe-bad | probe-suspicious | probe-error' },
          file: { type: 'string', description: 'repo-relative path or "-"' },
          line: { type: 'string', description: 'line or range or "-"' },
          text: { type: 'string', description: 'the message / failed assertion / measurement verbatim, trimmed' },
        },
      },
    },
    log_path: { type: 'string', description: 'absolute path of the captured log under f:/tmp/disrumpo-audit/' },
  },
}

const PROBE_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['ran', 'probe_source_path', 'tree_clean', 'summary', 'measurements', 'log_path'],
  properties: {
    ran: { type: 'boolean' },
    probe_source_path: { type: 'string', description: 'where the probe TU was preserved (specs/disrumpo-audit/probes/audit_probes.cpp) or "-"' },
    tree_clean: { type: 'boolean', description: 'git status --porcelain plugins/disrumpo was empty after cleanup' },
    summary: { type: 'string' },
    measurements: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['name', 'setup', 'metric', 'value', 'threshold', 'verdict', 'note'],
        properties: {
          name: { type: 'string', description: 'short probe id, e.g. "alias:Tube:drive=1.0"' },
          setup: { type: 'string', description: 'input signal, params, sample rate, block size, bands' },
          metric: { type: 'string', description: 'what was measured (aliasing dB, DC offset, peak delta dB, click count, NaN count, reconstruction error dB...)' },
          value: { type: 'string', description: 'the number(s) as printed' },
          threshold: { type: 'string', description: 'the bar you judged against and where it comes from (spec SC, common practice)' },
          verdict: { type: 'string', enum: ['ok', 'suspicious', 'bad', 'error'] },
          note: { type: 'string', description: 'which code the result implicates, if any (file:line), or why it is fine' },
        },
      },
    },
    log_path: { type: 'string' },
  },
}

const FINDING_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['findings'],
  properties: {
    findings: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['title', 'severity', 'category', 'file', 'line', 'problem', 'why_wrong', 'consequence', 'suggested_fix', 'evidence'],
        properties: {
          title: { type: 'string', description: 'one-line summary' },
          severity: { type: 'string', enum: ['critical', 'high', 'medium', 'low'] },
          category: { type: 'string', description: 'kebab-case: rt-safety, dsp-numerics, signal-path, morph, sweep, oversampling, lifecycle-state, ui-wiring, shared-dsp-contract, duplication, spec-conformance, portability, test-gap, anti-pattern, measured' },
          file: { type: 'string', description: 'repo-relative path' },
          line: { type: 'string', description: 'line number or range, e.g. "446" or "446-449"' },
          problem: { type: 'string', description: 'what the code does that is wrong' },
          why_wrong: { type: 'string', description: 'the correct behaviour and why this deviates (cite DSP reasoning / spec FR / API contract)' },
          consequence: { type: 'string', description: 'concrete audible, user-visible or maintenance consequence' },
          suggested_fix: { type: 'string', description: 'concrete fix direction incl. which test would catch it' },
          evidence: { type: 'string', description: 'the code lines you read (quoted, short) and/or the measurement numbers' },
        },
      },
    },
  },
}

const VERDICT_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['refuted', 'reasoning', 'severity_adjustment'],
  properties: {
    refuted: { type: 'boolean', description: 'true if the finding is wrong, already handled elsewhere, unreachable, or intentional per the stated architecture facts' },
    reasoning: { type: 'string', description: 'cite the exact code (file:line) you read to reach this verdict' },
    severity_adjustment: { type: 'string', enum: ['none', 'upgrade', 'downgrade'], description: 'if not refuted, should severity change?' },
  },
}

const CRITIQUE_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['gaps', 'followup_finders', 'rejudge_ids'],
  properties: {
    gaps: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['kind', 'detail'],
        properties: {
          kind: { type: 'string', enum: ['uncovered-area', 'unverified-claim', 'suspicious-refutation', 'missing-measurement', 'lane-not-run'] },
          detail: { type: 'string' },
        },
      },
    },
    followup_finders: {
      type: 'array',
      maxItems: 6,
      description: 'targeted finder prompts for areas the first round missed; each names files and the specific hazard to hunt',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['key', 'prompt'],
        properties: { key: { type: 'string', description: 'kebab-case label' }, prompt: { type: 'string' } },
      },
    },
    rejudge_ids: { type: 'array', items: { type: 'string' }, description: 'IDs of REFUTED findings whose refutation looks wrong and deserves a tie-break re-judgement' },
  },
}

const DOC_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['written_path', 'summary'],
  properties: {
    written_path: { type: 'string', description: 'repo-relative path of the file you wrote' },
    summary: { type: 'string', description: '3-5 lines: what the document contains, counts' },
  },
}

// ---------------------------------------------------------------------------
// Helpers (plain code)
// ---------------------------------------------------------------------------
const firstLine = l => parseInt(String(l).split(/[-,]/)[0], 10) || 0
const norm = s => String(s).toLowerCase().replace(/[^a-z0-9]+/g, ' ').trim()
function isDup(a, b) {
  if (a.file !== b.file) return false
  if (Math.abs(firstLine(a.line) - firstLine(b.line)) <= 3) return true
  return norm(a.title) === norm(b.title)
}
function dedupInto(list, incoming, tag) {
  let added = 0
  for (const f of incoming) {
    const dup = list.find(g => isDup(g, f))
    if (dup) {
      dup.also_flagged_by = (dup.also_flagged_by || []).concat(f.dimension || tag)
    } else {
      list.push(f)
      added++
    }
  }
  return added
}
const LANE_STEPS = []

// ---------------------------------------------------------------------------
// Measurement lane — ONE heavy job at a time, each capped. Started first and
// NOT awaited until the static findings are in, so it overlaps the reading
// phases. Every step is its own agent (fresh context, structured result).
// ---------------------------------------------------------------------------
async function laneStep(key, prompt, opts) {
  const r = await agent(`${CONTEXT}\n\nYou are ONE step of the Disrumpo measurement lane. Only this step runs on the machine right now, but readers are also using the box: stay inside the caps named below, never widen them. Capture every long command's output to a log file under ${LOG_DIR}/ on the FIRST run and read the log (never re-run a slow command to grep it). Create ${LOG_DIR} if missing. Do not edit repository files unless your step says so.\n\n${prompt}`, {
    label: `lane:${key}`,
    phase: 'Measure',
    ...opts,
  })
  LANE_STEPS.push({ key, result: r })
  return r
}

async function runMeasurementLane() {
  const lane = {}

  lane.lints = await laneStep('lints', `STEP: repo lints scoped to Disrumpo (cheap node scripts, run them one after another, each output to its own log):
  node tools/lint-odr.js
  node tools/lint-layers.js
  node tools/lint-apple-globals.js
  node tools/lint-finite-bitmask.js
  node tools/lint-nonfinite-symbols.js
  node tools/lint-plugin-editor-baseline.js
  node tools/lint-wallclock-tags.js
  node tools/uidesc-inventory.js plugins/disrumpo/resources/editor.uidesc --check
Read each script's header comment first if its arguments are unclear (some take a path or --self-test). Report ONLY items that touch plugins/disrumpo/ or a dsp/ header Disrumpo includes (grep -rhoE '#include <krate/dsp/[^>]+>' plugins/disrumpo/src gives that list); a tree-wide ratchet baseline hit elsewhere is not a Disrumpo item. kind=lint.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })

  if (!SKIP.build) {
    lane.build = await laneStep('build', `STEP: build the Disrumpo test executable and the plugin bundle in ONE invocation, capped at two MSBuild nodes:
  ${CMAKE} --build ${BUILD_DIR} --config Release --target disrumpo_tests Disrumpo -- -m:2
The bundle target is "Disrumpo" (the built bundle is ${BUILD_DIR}/VST3/Release/Disrumpo.vst3); if MSBuild says the target does not exist, grep -rn DISRUMPO_NAME cmake plugins/disrumpo/CMakeLists.txt for the real name. If ${BUILD_DIR} does not exist, configure first: ${CMAKE} --preset windows-x64-release. A post-build copy failure into "C:/Program Files/Common Files/VST3/" is NOT an error (the compile succeeded). Report every compiler WARNING (the project requires zero) as kind=build-warning with file:line, and any error as build-error. Record the wall clock.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })

    lane.gate = await laneStep('gate', `STEP: run the per-push Disrumpo suite SHARDED, taking a quarter of the machine:
  node tools/run-gate.js --share 4 --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' disrumpo_tests
Run it ONCE, capture to a log, then read the log. Report the GATE GREEN / GATE RED line verbatim in summary. For every failed case report kind=test-red with the test file:line and the failed assertion text (per-shard logs are under f:/tmp/gate/). Also note skipped/hidden/[!mayfail] cases you see in the log. Timing tests are excluded on purpose; do NOT run tools/run-cpu-tests.js.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })

    lane.pluginval = await laneStep('pluginval', `STEP: validate the bundle:
  tools/pluginval.exe --strictness-level 5 --validate "${BUILD_DIR}/VST3/Release/Disrumpo.vst3"
Capture to a log. The EXIT CODE is the verdict (there is no SUCCESS line): 0 = pass; 0xC0000005 (-1073741819) = crash. Report every FAIL/error block as kind=pluginval with the test name and message; on a crash report the last test that started.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })
  } else {
    log('Measure: build/gate/pluginval skipped by args.skip.build')
  }

  if (!SKIP.probes && !SKIP.build) {
    lane.probes = await agent(`${CONTEXT}\n\nYou are the AUDIO PROBE author of the Disrumpo measurement lane. You MAY edit files, but ONLY these: create plugins/disrumpo/tests/audit/audit_probes.cpp, append it to the disrumpo_tests source list in plugins/disrumpo/tests/CMakeLists.txt, and create ${OUT_DIR}/probes/. Everything you add to plugins/ is TEMPORARY and must be removed at the end.

GOAL: measure what code reading cannot — render real audio through the real Processor (and, where simpler, directly through BandProcessor / MorphEngine / CrossoverNetwork / DistortionAdapter) and print numbers. Model your harness on plugins/disrumpo/tests/integration/processor_audio_output_test.cpp (Processor setup, ProcessData, vst_param_changes.h for parameter changes) and plugins/disrumpo/tests/integration/preset/factory_preset_validation_test.cpp (loading a .vstpreset component state). Use the shared helpers in tests/test_helpers/: spectral_analysis.h (measureAliasing / compareAliasing), artifact_detection.h (clicks/discontinuities), signal_metrics.h, audio_features.h (extractAudioFeatures, linToDbfs), test_signals.h (generateSine/Sweep/Silence/Step/WhiteNoise). Read their headers for exact signatures before writing code.

Every TEST_CASE carries the tags "[.][audit-probe]" (hidden: never part of the per-push gate) and PRINTS one line per measurement to stdout in the form
  PROBE <name> | <setup> | <metric> = <value> | threshold <bar> | <ok|suspicious|bad>
using WARN() or std::printf, and asserts nothing except that the output is finite (so a probe never fails the build gate; the numbers are the result). Keep the whole probe set under ~8 minutes wall clock when sharded.

MEASURE AT LEAST (one TEST_CASE per group, loop inside):
 1. Per DistortionType (all 26, via a single-node band or DistortionAdapter directly): 1 kHz sine at -6 dBFS, 48 kHz, drive at 0.25/0.75/1.0 normalized -> aliasing level (energy at non-harmonic bins vs harmonic), DC offset (mean), NaN/Inf count, output peak dBFS; the same with bias at its extremes if the type exposes bias; and with SILENT input -> output RMS (self-noise / self-oscillation).
 2. Crossover reconstruction: band count 1,2,3,4 with all bands clean (mix 0 or an identity/neutral type), white noise or a 20 Hz-20 kHz sweep -> magnitude deviation of output vs input in dB across the band (SC-001 says +/-0.1 dB); also the group-delay/phase sanity (impulse through 4 bands: energy spread).
 3. Oversampling/latency alignment: two bands whose chosen oversampling factors differ (pick types whose profiles force 1x vs 4x) with neutral shaping -> impulse response: are the per-band arrivals aligned (report sample offset between band peaks) and does the dry path align with the wet path at mix 50%?
 4. Morph continuity: a 2-node and a 4-node cross-family band; move the morph cursor linearly across the pad over 2 s while a sine plays -> click/discontinuity count and the largest sample-to-sample jump, and the output level trajectory (max dB deviation); also park the cursor EXACTLY on a node position (inverse-distance weight singularity).
 5. Parameter automation: step drive 0->1, mix 0->1, band gain -24->+24 dB, band mute toggle, solo toggle, band count 2->4->1 mid-stream, oversample max 1x->8x mid-stream -> click count and NaN count per step.
 6. Sweep system: sweep enabled, intensity 100%, frequency automated 100 Hz -> 10 kHz over 2 s, LFO on at 5 Hz, env follower on -> output continuity + per-band intensity sanity (no band ever exceeds expected multiplier; no NaN).
 7. State: for every factory preset under plugins/disrumpo/resources/presets/: load via setState, render 0.5 s of a -12 dBFS sine, record peak/RMS/DC/NaN; then getState -> setState -> render again -> max sample difference (should be 0 or ~1e-6). Flag presets that are silent, clip above 0 dBFS, or differ after round trip.
 8. Sample-rate consistency: one representative chain at 44.1k, 48k, 96k, 192k -> RMS and centroid, flag deviations > 1 dB or > 15 % centroid between rates (fixed-ms expectations) and any NaN at 192k.

PROCEDURE:
 a. Write the TU; add it to CMakeLists.
 b. Build (cap TWO nodes; this is the only build on the box): ${CMAKE} --build ${BUILD_DIR} --config Release --target disrumpo_tests -- -m:2  > ${LOG_DIR}/probe-build.log 2>&1 ; read the log; fix YOUR compile errors/warnings (never touch plugin code).
 c. Run sharded, a quarter of the machine, capture:  node tools/run-gate.js --share 4 --filter '[audit-probe]' disrumpo_tests > ${LOG_DIR}/probe-run.log 2>&1 ; the PROBE lines are in the per-shard logs under f:/tmp/gate/ — collect them all into ${LOG_DIR}/probe-results.txt.
 d. Preserve: copy the TU to ${OUT_DIR}/probes/audit_probes.cpp (create the dir) so the fix plan can promote the useful probes into real regression tests.
 e. CLEAN UP, mandatory: git checkout -- plugins/disrumpo/tests/CMakeLists.txt ; delete plugins/disrumpo/tests/audit/ ; then run  git status --porcelain plugins/disrumpo  and report tree_clean = (output empty). If anything else shows up there, you broke the rule — revert it.
 f. Return every PROBE line as a measurement with a verdict and, for anything suspicious/bad, read the implicated code and name file:line in note.

Hard rules: Write tool for new files (no heredocs), one build at a time, never run tools/clean-test-output.js, never run tests as a single serial exe, never commit.`, { label: 'lane:probes', phase: 'Measure', schema: PROBE_SCHEMA, agentType: 'general-purpose', model: 'opus' })
    LANE_STEPS.push({ key: 'probes', result: lane.probes })
  } else {
    log('Measure: audio probes skipped')
  }

  if (!SKIP.tidy) {
    lane.tidy = await laneStep('tidy', `STEP: clang-tidy for the Disrumpo area, four jobs, Windows PowerShell script only (never the .sh on Windows):
  pwsh -NoProfile -File ./tools/run-clang-tidy.ps1 -Target disrumpo -BuildDir build/windows-ninja -Jobs 4 > ${LOG_DIR}/tidy.log 2>&1
If build/windows-ninja/compile_commands.json is missing, generate it first with the ninja preset (${CMAKE} --list-presets shows the name; see .claude/skills/clang-tidy-setup/SKILL.md). If a warning looks like a phantom (header not found, unknown flag), reconfigure that preset once and re-run only the one TU. Set a wall-clock ceiling: if the script has produced no new output for 20 minutes, find the stuck clang-tidy process (Get-CimInstance Win32_Process | where CommandLine -match clang-tidy), record the TU, kill it, and report that TU as kind=tidy "hung". Report warnings CLUSTERED by check name + file (one item per cluster, count in text, one example line), kind=tidy. The total count goes in summary.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })
  } else {
    log('Measure: clang-tidy skipped by args.skip.tidy')
  }

  if (!SKIP.portability) {
    lane.portability = await laneStep('portability', `STEP: the Linux/macOS compile proxy. tools/check-portability.js syntax-checks TUs with GCC in WSL; with no arguments it only takes files changed vs origin/main (nothing, on a clean tree), so pass EXPLICIT files: every .cpp under plugins/disrumpo/src and plugins/disrumpo/tests (build the list with find/git ls-files and pass it on one command line), output captured to ${LOG_DIR}/portability.log. Read its header comment first for any extra flags. It boots WSL; when it is done run  wsl --shutdown . Report every error/warning it prints for plugins/disrumpo/ or an included dsp/ header as kind=portability with file:line. Note in summary what the script does NOT cover (it is a compile proxy, not fast-math semantics).`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })
  } else {
    log('Measure: portability skipped by args.skip.portability')
  }

  return lane
}

phase('Measure')
log('Measurement lane started (serial, capped) — it overlaps the reading phases')
const lanePromise = runMeasurementLane()
  .catch(e => ({ error: String(e) }))
  .then(r => { laneDone = true; return r })

// ---------------------------------------------------------------------------
// Inventory (Haiku) + Understand (Sonnet) — concurrent, barrier before Find
// because every finder receives both digests.
// ---------------------------------------------------------------------------
const INVENTORIES = [
  {
    key: 'params',
    prompt: `Cross-reference the PARAMETER plumbing. Sides: (1) plugin_ids.h enums + make*ParamId helpers (list every concrete ID that can exist: globals, sweep incl. custom-curve points, per-band for 4 bands, per-node for 4 bands x 4 nodes); (2) plugins/disrumpo/src/controller/parameter_registration.cpp registrations (ID, default, step count/type, units); (3) plugins/disrumpo/src/processor/processor_params.cpp processParameterChanges handling (which IDs are consumed, denormalization formula) and the atomics' constructor defaults in processor.h; (4) plugins/disrumpo/src/processor/processor_state.cpp getState vs setState (order, type, count written vs read, version gates) and the controller's setComponentState in controller.cpp; (5) resources/editor.uidesc control-tags — run  node tools/uidesc-inventory.js plugins/disrumpo/resources/editor.uidesc --out ${LOG_DIR}/uidesc-inventory.json  (create ${LOG_DIR} first) and read the JSON. Report mismatches: registered but never consumed; consumed but never registered; saved but not loaded or loaded in a different order/type; default differs between registration, processor atomic init and state default; a uidesc control-tag that is not a known ID; a registered automatable parameter with no control (readouts don't count); a denormalization range that differs from the registration's displayed range.`,
  },
  {
    key: 'types',
    prompt: `Cross-reference the DISTORTION TYPE tables. Sides: DistortionType / DistortionCategory / DistortionFamily / MorphMode enums in plugins/disrumpo/src/dsp/distortion_types.h (and any name/description/oversampling-profile/parameter-range tables there); every switch/if over DistortionType in distortion_adapter.cpp/.h, morph_engine.cpp, band_processor.h, oversampling_utils.h, processor_params.cpp (shape-slot map), controller.cpp/menu population (unit/menu_items_test.cpp shows the expected strings), docs/index.html and specs/Disrumpo/specs-overview.md (the "26 distortion types" list). Report: enum members without an adapter branch or with a fallthrough; table lengths that disagree with the enum count; names/orders that differ between enum, UI menu, docs and spec; per-type parameter ranges that differ between adapter and spec; oversampling profiles missing for a type.`,
  },
  {
    key: 'presets-versions',
    prompt: `Cross-reference PRESETS, VERSIONS and DOCS. (1) Count factory presets per category under plugins/disrumpo/resources/presets/ (expected 120 per the test comment in tests/integration/preset/factory_preset_validation_test.cpp) and compare with the categories the preset browser code and the generator source expect (grep -ri "disrumpo_preset_generator" plugins/disrumpo tools CMakeLists.txt to find it). (2) version.json vs version.h vs CHANGELOG.md top entry vs docs/index.html vs installers/ metadata vs the update config under src/update/. (3) docs/index.html and CHANGELOG claims that name a number or feature (band count, type count, oversampling max, modulation targets) vs the constants in code. (4) plugins/disrumpo/CLAUDE.md statements vs the filesystem (file names, line counts within 20 %). Report every disagreement with both values verbatim.`,
  },
]

const SUBSYSTEMS = [
  {
    key: 'signal-path',
    prompt: `Map the block signal path: plugins/disrumpo/src/processor/processor.cpp (read process(), setupProcessing, setActive, setBusArrangements fully), dsp/crossover_network.h, dsp/band_processor.h (read fully), dsp/band_state.h. Cover: gain staging, where mix/drive/tone/bias/gain/pan/mute/solo are applied, per-band oversampling (how the factor is chosen per block, how latency is or is not compensated between bands and vs the dry path, what setLatencySamples reports), summation, spectrum tap, denormal handling, buffer ownership and sizes, what happens when band count changes while processing.`,
  },
  {
    key: 'morph-engine',
    prompt: `Map the morph system: dsp/morph_engine.{h,cpp}, dsp/morph_node.h, dsp/distortion_adapter.{h,cpp}, dsp/distortion_types.h, dsp/oversampling_utils.h, plus plugins/disrumpo/src/processor/processor_params.cpp's shape-slot map. Cover: weight computation per MorphMode (formula, singularities), same-family vs cross-family decision, parameter interpolation of continuous AND discrete parameters, equal-power crossfade, node enable/disable, how a node's type change re-prepares its adapter on the audio thread, oversampling factor derivation and switching.`,
  },
  {
    key: 'sweep-modulation',
    prompt: `Map the sweep + modulation system: dsp/sweep_processor.h, sweep_lfo.h, sweep_envelope.h, sweep_morph_link.h, sweep_types.h, custom_curve.h, the sweep block in processor.cpp process(), and how the shared ModulationEngine (dsp/include/krate/dsp/systems/modulation_engine.h, read its public API) is driven from processor_params.cpp / processor_messaging.cpp (mod-offset messages). Cover: intensity math (Gaussian/linear falloff, width in octaves), LFO sync/note-value, envelope follower, custom curve evaluation and point ordering, normalization round trips (normalizeSweepFrequency/denormalize), what is per-sample vs per-block, the sweep position buffer to the UI.`,
  },
  {
    key: 'params-state-messaging',
    prompt: `Map parameters, persistence and messaging: plugins/disrumpo/src/plugin_ids.h, processor/processor_params.cpp, processor/processor_state.cpp, processor/processor_messaging.cpp, processor.h atomics, plugins/disrumpo/src/preset/ (all files), controller.cpp setComponentState/getState paths, and the DataExchange + SharedDisplayBridge (plugins/shared/src/display/shared_display_bridge.h) spectrum pipeline. Cover: the save format (version, order, counts), load clamps and version gates, preset load vs host setState divergence, MIDI learn/CC mapping persistence, connect/disconnect lifecycle, block sizes/alignment (a macOS aligned_alloc rounding fix landed in 0.9.10 — check it is still right).`,
  },
  {
    key: 'controller-ui',
    prompt: `Map the controller + UI: plugins/disrumpo/src/controller/ (controller.cpp read end to end by offsets, parameter_registration.cpp, sub_controllers.h, visibility_controllers.cpp, morph_link.cpp, animated_expand_controller.cpp, keyboard_shortcut_handler.cpp, custom_buttons.h, format_helpers.h, views/*), resources/editor.uidesc structure (templates, custom views, control-tags; scan, not every pixel). Cover: param registration (types, string lists, step counts), editor lifecycle (createView, didOpen, willClose, update banner + version label per the repo rule), IDependent/forget balance, visibility logic for band count / expand-collapse / mod panel, morph pad <-> params, spectrum display data path, MIDI learn UI, keyboard shortcuts, accessibility.`,
  },
  {
    key: 'shared-dsp-distortion',
    prompt: `Map the SHARED distortion DSP Disrumpo wraps (dsp/include/krate/dsp/): processors/saturation_processor.h, tube_stage.h, tape_saturator.h, fuzz_processor.h, wavefolder_processor.h, temporal_distortion.h, feedback_distortion.h, aliasing_effect.h, spectral_distortion.h, fractal_distortion.h, formant_distortion.h, granular_distortion.h, allpass_saturator.h; primitives/bit_crusher.h, sample_rate_reducer.h, ring_saturation.h, bitwise_mangler.h, chaos_waveshaper.h, stochastic_shaper.h, waveshaper.h, dc_blocker.h. For EACH: the API contract (prepare/reset/process signatures, mono vs stereo state, per-sample vs block, parameter ranges and units, whether it blocks DC itself, whether it can self-oscillate or produce output from silence, internal oversampling, NaN guards, allocations outside prepare, latency). Then note how distortion_adapter.cpp calls it (one instance per channel? ranges respected? reset on type switch?). Oddities = contract violations you suspect.`,
  },
  {
    key: 'shared-dsp-infra',
    prompt: `Map the SHARED infrastructure DSP Disrumpo relies on (dsp/include/krate/dsp/): primitives/oversampler.h (factors, latency, filter design, factor switching), processors/crossover_filter.h (CrossoverLR4: phase, allpass, coefficient update cost), primitives/biquad.h, one_pole.h, smoother.h (time constants, snap behaviour), lfo.h, processors/envelope_follower.h, primitives/spectrum_fifo.h + fft.h + core/window_functions.h (the spectrum tap), primitives/sweep_position_buffer.h, core/crossfade_utils.h, core/db_utils.h (dbToGain, isFinite), core/fast_math.h, core/note_value.h, core/modulation_types.h, systems/modulation_engine.h. Same coverage as above: contracts, thread expectations, latency, NaN/denormal behaviour, fast-math hazards, and how Disrumpo's call sites honour them.`,
  },
  {
    key: 'specs-and-tests',
    prompt: `Map the SPEC CLAIMS and the TEST SUITE. Read specs/Disrumpo/specs-overview.md and dsp-details.md (FR-xxx / SC-xxx), skim plans-overview.md and custom-controls.md; list the testable claims (numbers, ranges, behaviours) as invariants with their source line. Then read plugins/disrumpo/tests/CMakeLists.txt and skim every test file's TEST_CASE names and tags (grep is fine) to record what each file covers, which cases are hidden/[.]/[!mayfail]/[long], which assertions look tautological or too loose (Approx margins that swallow the effect), and which spec claims have no test. Oddities = claims the code probably violates and tests that cannot fail.`,
  },
]

phase('Inventory')
log(`Inventory (${INVENTORIES.length} Haiku cross-refs) and Understand (${SUBSYSTEMS.length} Sonnet readers) running together...`)
const [inventories, maps] = await parallel([
  () =>
    parallel(
      INVENTORIES.map(i => () =>
        agent(`${CONTEXT}\n\nYOUR INVENTORY TASK (mechanical, exhaustive, no judgement about severity — just list what disagrees): ${i.prompt}\n\nUse grep/sed/node for the heavy lifting; read the code where a table lives. Every mismatch needs both sides verbatim with file:line.`, {
          label: `inv:${i.key}`,
          phase: 'Inventory',
          schema: INVENTORY_SCHEMA,
          model: 'haiku',
        }).then(r => ({ key: i.key, ...r })),
      ),
    ),
  () =>
    parallel(
      SUBSYSTEMS.map(s => () =>
        agent(`${CONTEXT}\n\nYOUR SUBSYSTEM: ${s.prompt}\n\nReturn a dense, factual map. The "oddities" list is the most valuable output — every suspicious thing you notice becomes a lead for the bug-finder phase. Do not report the same thing twice.`, {
          label: `map:${s.key}`,
          phase: 'Understand',
          schema: MAP_SCHEMA,
          model: 'sonnet',
        }).then(m => ({ key: s.key, ...m })),
      ),
    ),
])

const goodInv = (inventories || []).filter(Boolean)
const goodMaps = (maps || []).filter(Boolean)
if (goodInv.length < INVENTORIES.length) log(`WARNING: ${INVENTORIES.length - goodInv.length} inventory agent(s) failed — their cross-refs are missing`)
if (goodMaps.length < SUBSYSTEMS.length) log(`WARNING: only ${goodMaps.length}/${SUBSYSTEMS.length} subsystem maps completed — finders proceed with partial context`)

const INVENTORY_DIGEST = goodInv
  .map(i => `## inventory:${i.key}\n${i.summary}\nMISMATCHES (${i.mismatches.length}):\n${i.mismatches.map(m => `- ${m.what} [${m.where}] ${m.detail}`).join('\n')}`)
  .join('\n\n')
const MAP_DIGEST = goodMaps
  .map(m => `## ${m.key}\n${m.summary}\nKEY FILES:\n${m.key_files.join('\n')}\nINVARIANTS:\n${m.invariants.join('\n')}\nODDITIES (investigate these):\n${m.oddities.join('\n')}`)
  .join('\n\n')

// ---------------------------------------------------------------------------
// Find — one Opus finder per dimension, fed both digests.
// ---------------------------------------------------------------------------
const DIMENSIONS = [
  {
    key: 'rt-safety',
    prompt: `Hunt REAL-TIME SAFETY violations on the audio thread: allocation (std::vector growth, std::function, std::string, new, make_unique, map insert), locks, exceptions (at(), stoi, throw paths in SDK calls), IO/logging, unbounded loops, blocking calls, reachable from Processor::process(), BandProcessor::process, MorphEngine::process, DistortionAdapter, SweepProcessor, the DataExchange/SharedDisplayBridge send path, and setActive/setupProcessing while running; atomics with wrong memory order or non-lock-free types; re-preparing an adapter or oversampler on a type/factor change inside process(). Focus: plugins/disrumpo/src/processor/*.cpp, src/dsp/*.h|cpp, and the shared headers they call (dsp/include/krate/dsp/...).`,
  },
  {
    key: 'dsp-numerics',
    prompt: `Hunt NUMERICAL defects: NaN/Inf sources (0/0 in weight normalisation, log/pow/sqrt/acos at domain edges, tan at pi/2 in filter coefficient updates near Nyquist at 192 kHz, division by a smoothed value that can be 0), denormals in long-tailed filters/feedback paths, std::isnan / bit-mask finiteness clones / numeric_limits infinity() in fast-math code, float == comparisons, exactness-by-cancellation at neutral points (mix 50 %, pan centre, morph cursor on a node), smoother time constants derived before sampleRate is set, uninitialised state read on the first block, dbToGain of -inf, clamps missing at the VST boundary. Focus: src/dsp/*.h|cpp, processor.cpp, processor_params.cpp, and the KrateDSP headers Disrumpo includes.`,
  },
  {
    key: 'signal-path',
    prompt: `Hunt SIGNAL-PATH LOGIC bugs: crossover reconstruction (band edges, D'Appolito allpass count per band, coefficient update when a crossover frequency moves, 1-band bypass), gain staging and pan law, mix law (linear vs equal power, dry path alignment), mute/solo semantics (solo of a muted band, several solos), band count change while processing (stale state in bands that reappear, silent summation of disabled bands), LATENCY: per-band oversampling factors that differ -> per-band latency differs -> phase smear at summation; dry vs wet latency; what setLatencySamples/getLatencySamples reports to the host vs reality; oversampler factor switching mid-block; stereo handling (one stateful instance shared by L and R is a bug). Focus: processor.cpp process(), band_processor.h, crossover_network.h, oversampler.h, crossover_filter.h.`,
  },
  {
    key: 'morph-oversampling',
    prompt: `Hunt MORPH + OVERSAMPLING bugs: inverse-distance weights (cursor exactly on a node -> 1/0; all nodes at the same position; disabled nodes still weighted), weight normalisation and the three MorphModes, same-family detection vs families with incompatible parameter sets, interpolation of DISCRETE parameters (bit depth, sample-rate divisor, stages) producing in-between values, equal-power crossfade sum, re-prepare on node type change (click, allocation, stale filter state), morph-weighted oversampling factor rounding (roundUpToPowerOf2Factor caps at 4 while the global parameter offers 8x — reconcile), factor changes causing clicks or latency jumps, cost explosion when 4 cross-family nodes each run at 4x. Focus: morph_engine.{h,cpp}, morph_node.h, distortion_adapter.{h,cpp}, oversampling_utils.h, band_processor.h, processor_params.cpp shape-slot map.`,
  },
  {
    key: 'sweep-modulation',
    prompt: `Hunt SWEEP + MODULATION bugs: Gaussian/linear falloff math (width in octaves vs Hz, intensity > 1 handling: processor multiplies intensity by 2), per-band centre frequencies (kBandCenterFreqs vs the actual crossover-derived band centres — are they the same thing?), LFO note-value/tempo sync without a valid ProcessContext, envelope follower attack/release in ms vs samples, custom curve evaluation with unsorted or duplicate X points and the fixed P0X=0 rule, sweep->morph link modes, modulation offsets applied twice (ModulationEngine offset AND a legacy path), normalise/denormalise round trips that drift, modulation of parameters the UI shows but the DSP ignores (Tone/Bias on types without tone/bias). Focus: sweep_*.h, custom_curve.h, processor.cpp sweep block, processor_messaging.cpp, modulation_engine.h.`,
  },
  {
    key: 'lifecycle-state',
    prompt: `Hunt VST3 LIFECYCLE and STATE bugs: getState/setState order/type/count mismatches, missing or wrong version gates, bands/nodes saved for 4 but loaded for bandCount only (or vice versa), defaults drift between registration, processor atomics and state defaults, setComponentState vs setState divergence, preset load path (src/preset/, controller) vs host path, MIDI-learn mapping persistence, DataExchange connect/disconnect ordering, SharedDisplayBridge registry lifetime (registered in initialize, unregistered in terminate? what if two instances?), setActive(false) not resetting state, setupProcessing with a changed sample rate not re-preparing every component (sweep smoothers, LFO, envelope, oversamplers, crossovers, adapters), setBusArrangements rejecting mono. Focus: processor_state.cpp, processor_messaging.cpp, processor.cpp, controller.cpp, src/preset/*, shared_display_bridge.h.`,
  },
  {
    key: 'ui-wiring',
    prompt: `Hunt UI WIRING bugs, starting from the INVENTORY mismatches (confirm each in code before reporting): control-tags vs plugin_ids.h, params registered but absent from the uidesc or vice versa, StringListParameter entry counts vs enum ranges (26 types, 3 morph modes, 6 LFO waveforms, oversample 4 steps), visibility controllers that miss a band-count/expand state, sub-controller lifetimes (dangling view pointers after willClose, IDependent not removed, forget() imbalance, UIViewSwitchContainer templates), morph pad <-> morph XY param two-way sync (feedback loops, normalised vs plain), spectrum display reading a buffer after disconnect, keyboard shortcuts firing while a text edit has focus, MIDI learn target ID encoding through a normalised parameter, the update banner / version label rule (plugins/shared/src/ui/update_banner_view.h). Focus: controller/*.cpp|h, controller/views/*, resources/editor.uidesc.`,
  },
  {
    key: 'shared-dsp-contracts',
    prompt: `Hunt CONTRACT VIOLATIONS between Disrumpo and the KrateDSP it wraps: a processor whose process() expects a block but is called per sample (or the reverse), prepare() never called or called with maxBlockSize smaller than the oversampled block, reset() not called on type switch (stale feedback/granular buffers), ONE instance used for both channels, parameter setters given plain values where the primitive expects normalised (or dB vs linear, Hz vs normalised), ranges outside what the primitive clamps (bit depth 0, divisor 0, feedback >= 1), latency of primitives with internal lookahead/oversampling never summed into the reported latency, DC blockers assumed but absent for asymmetric shapers, processors that emit output from silence (feedback, chaos, stochastic, granular) with no gate. Cite BOTH sides (adapter call site AND primitive contract line). Focus: distortion_adapter.{h,cpp}, band_processor.h, and the dsp/include/krate/dsp headers Disrumpo includes.`,
  },
  {
    key: 'duplication-drift',
    prompt: `Hunt DUPLICATION and COPY-PASTE DRIFT: constants defined twice with different values or names (e.g. sweep frequency limits in processor.cpp vs sweep_processor.h, band centre tables, gain ranges), the same denormalisation formula written in processor_params.cpp, parameter_registration.cpp, controller.cpp and tests with subtle differences, repeated blocks across the four processor TUs and inside controller.cpp that should be one helper, logic that exists in plugins/shared/ (preset browser, MIDI CC manager, update checker, display bridge) but was re-implemented locally in Disrumpo, enum<->string tables maintained in two places, tests that duplicate each other. Focus: plugins/disrumpo/src/** and plugins/shared/src/** for the shared originals.`,
  },
  {
    key: 'spec-conformance',
    prompt: `Hunt SPEC CONFORMANCE failures: take the testable claims from the specs-and-tests map (and read specs/Disrumpo/specs-overview.md + dsp-details.md yourself for FR/SC numbers: ranges, dB tolerances, counts, oversampling policy, latency, CPU), then check the code honours each one; also check every code comment that cites an FR-xxx/SC-xxx actually implements what it cites (comments that lie are findings), and docs/index.html / CHANGELOG claims against behaviour. Report the claim, the source line, and the code line that contradicts it.`,
  },
  {
    key: 'portability-quality',
    prompt: `Hunt CROSS-PLATFORM and CODE-QUALITY problems: narrowing in brace-init that MSVC accepts but Clang rejects, std::isnan / bit-mask finiteness under -ffast-math, numeric_limits infinity()/quiet_NaN() constants in DSP code, names that collide with Apple globals/macros (MAC, Rect, Point, Component, Fixed, Boolean...), signed/unsigned mixups in loop bounds, uninitialised members, float == comparisons, unused results, misleading names/comments that contradict behaviour, God functions, magic numbers duplicating named constants, dead code, naming-convention violations (members without trailing underscore, constants without k). Focus: plugins/disrumpo/src/** and plugins/disrumpo/tests/**.`,
  },
  {
    key: 'test-gaps',
    prompt: `Hunt TEST DEFECTS and COVERAGE GAPS in plugins/disrumpo/tests/: assertions that cannot fail (REQUIRE(true), checking only non-silence, Approx margins wider than the effect), approval/golden tests with no reference or a self-generated one, tests that pass on a stubbed path (vstgui_test_stubs.cpp) and would not on the real view, duplicated tests, hidden/[!mayfail]/commented-out cases hiding known reds, and MISSING coverage for the riskiest behaviours: band-count change mid-stream, oversampling factor switch click, per-band latency alignment, morph cursor on a node, all 26 types x drive extremes for NaN/DC/aliasing, 192 kHz, setState with truncated/corrupt data, every factory preset rendered (not just loaded), mono bus rejection, MIDI learn persistence. For each gap name the test file, TEST_CASE name and the assertion.`,
  },
  {
    key: 'gestalt-processor',
    prompt: `You are the GESTALT reviewer for the audio side: read plugins/disrumpo/src/processor/processor.cpp, processor_params.cpp, processor_state.cpp, processor_messaging.cpp and src/dsp/band_processor.h END TO END, top to bottom, and flag anything that does not make sense — a comment that says one thing while the body does another, branches that cannot execute, work done twice per block, values computed then ignored, ordering that looks accidental (e.g. sweep intensity applied before the block's parameter changes), TODO/FIXME hiding bugs, copy-paste leftovers from Iterum (Disrumpo was cloned from it: look for Iterum names, IDs, delay-specific code). Your value is noticing what category-driven review misses.`,
  },
  {
    key: 'gestalt-controller',
    prompt: `You are the GESTALT reviewer for the UI side: read plugins/disrumpo/src/controller/controller.cpp END TO END by offsets (it is ~2000 lines), then parameter_registration.cpp and sub_controllers.h, and flag anything that does not make sense — repeated boilerplate with one copy subtly different, string-list parameters whose entries disagree with the DSP enums, handlers that update a view but not the parameter (or vice versa), lifecycle code that assumes the editor exists, Iterum leftovers, dead handlers, misleading comments. Your value is noticing what category-driven review misses.`,
  },
]

phase('Find')
log(`Fanning out ${DIMENSIONS.length} Opus finders...`)
const found = await parallel(
  DIMENSIONS.map(d => () =>
    agent(`${CONTEXT}\n\nINVENTORY CROSS-REFERENCES (mechanical mismatches; confirm in code before reporting):\n${INVENTORY_DIGEST}\n\nSUBSYSTEM MAPS from the understanding phase (leads in ODDITIES are gold):\n${MAP_DIGEST}\n\nYOUR DIMENSION: ${d.prompt}\n\nRead the actual code before reporting anything. Quality over quantity: a finding without a concrete file:line you personally read is worthless; one real bug beats ten maybes. Do not report style nits unless they hide a defect. Severity: critical = wrong/unsafe audio, crash or data loss in normal use; high = audible defect or host-visible misbehaviour in common use; medium = edge-case defect or maintenance trap; low = quality.`, {
      label: `find:${d.key}`,
      phase: 'Find',
      schema: FINDING_SCHEMA,
      model: 'opus',
    }).then(r => r.findings.map(f => ({ ...f, dimension: d.key, source: 'static' }))),
  ),
)
const failedFinders = DIMENSIONS.filter((_, i) => !found[i]).map(d => d.key)
if (failedFinders.length) log(`WARNING: finders failed/skipped: ${failedFinders.join(', ')} — their dimension is UNCOVERED`)

const deduped = []
const rawStatic = found.filter(Boolean).flat()
dedupInto(deduped, rawStatic, 'static')
log(`${rawStatic.length} raw static findings -> ${deduped.length} after dedup`)

// ---------------------------------------------------------------------------
// Verify — three lenses per finding; a finding's verdict is decided by votes.
// Static findings are verified NOW while the measurement lane finishes.
// ---------------------------------------------------------------------------
const LENSES = [
  'CORRECTNESS: re-derive the claimed behaviour from source — is the code really doing what the finding says? Trace the actual call path and values.',
  'CONTEXT: is it intentional, guarded elsewhere (a clamp upstream, a reset in setActive, a test that pins it), unreachable in a real host, or already documented as a design decision in specs/Disrumpo or a code comment?',
  'IMPACT: assume the defect is real — does it produce the claimed consequence at the claimed severity? Quantify (dB, samples, which hosts/settings trigger it); downgrade or upgrade accordingly, and refute if the consequence cannot occur.',
]
// Verification is throttled while the measurement lane still runs: a full-width
// fan-out (15 findings x 3 lenses) fills every agent slot and the lane's next
// step (tidy, portability) queues behind it for an hour (run wf_b866487e, 2026-11-05).
// Chunks of 4 findings = 12 agents leave slots for the lane; full width once it is done.
// (laneDone is declared with the args at the top so the lane's .then never hits a TDZ.)
async function verifyAll(list, tag) {
  const out = []
  for (let i = 0; i < list.length; ) {
    const size = laneDone ? list.length : 4
    const chunk = list.slice(i, i + size)
    i += chunk.length
    out.push(...(await verifyChunk(chunk, tag)))
  }
  return out
}
async function verifyChunk(list, tag) {
  return parallel(
    list.map(f => () =>
      parallel(
        LENSES.map(lens => () =>
          agent(`${CONTEXT}\n\nA reviewer claims this defect in Disrumpo. Your job is to REFUTE it if you can. Lens: ${lens}\n\nFINDING:\n${JSON.stringify(f, null, 2)}\n\nRead ${f.file} around line ${f.line} AND every code path the claim depends on (callers, the shared DSP header it uses, the test that covers it). Default to refuted=true if you cannot positively confirm the defect from source or from the quoted measurement.`, {
            label: `verify:${f.file.split('/').pop()}:${f.line}`,
            phase: 'Verify',
            schema: VERDICT_SCHEMA,
            model: 'opus',
          }),
        ),
      ).then(votes => {
        const v = votes.filter(Boolean)
        const refutes = v.filter(x => x.refuted).length
        const verdict = v.length === 0 ? 'UNVERIFIED' : refutes === 0 ? 'CONFIRMED' : refutes === 1 ? 'PLAUSIBLE' : 'REFUTED'
        const ups = v.filter(x => x.severity_adjustment === 'upgrade').length
        const downs = v.filter(x => x.severity_adjustment === 'downgrade').length
        const order = ['low', 'medium', 'high', 'critical']
        let sev = f.severity
        if (ups >= 2) sev = order[Math.min(3, order.indexOf(sev) + 1)]
        else if (downs >= 2) sev = order[Math.max(0, order.indexOf(sev) - 1)]
        return { ...f, verdict, final_severity: sev, verifier_notes: v.map(x => x.reasoning), verify_round: tag }
      }),
    ),
  )
}

phase('Verify')
log(`Verifying ${deduped.length} static findings (3 Opus lenses each) while the measurement lane finishes...`)
const [verifiedStatic, lane] = await parallel([
  () => verifyAll(deduped, 'static'),
  () => lanePromise,
])

// ---------------------------------------------------------------------------
// Measurement interpretation — turn lane output into findings, then verify them.
// ---------------------------------------------------------------------------
const laneErr = lane && lane.error
if (laneErr) log(`WARNING: measurement lane threw: ${laneErr}`)
const laneSteps = LANE_STEPS.filter(s => s.result)
const laneDigest = laneSteps
  .map(s => `## lane:${s.key}\n${JSON.stringify(s.result, null, 1)}`)
  .join('\n\n')
const laneNotRun = ['lints', 'build', 'gate', 'pluginval', 'probes', 'tidy', 'portability'].filter(k => !laneSteps.find(s => s.key === k))
if (laneNotRun.length) log(`Measurement steps not run: ${laneNotRun.join(', ')}`)

let measured = []
if (laneSteps.length) {
  phase('Find')
  const interp = await agent(`${CONTEXT}\n\nSUBSYSTEM MAPS:\n${MAP_DIGEST}\n\nMEASUREMENT LANE RESULTS (facts produced by running the code; the probe source is at ${OUT_DIR}/probes/audit_probes.cpp if it ran):\n${laneDigest}\n\nTurn these into FINDINGS. Rules: every test-red, pluginval failure, build warning, portability error, probe "bad" and probe "suspicious" measurement becomes a finding IF you can localise it — read the implicated code and cite file:line (for a probe, the DSP code responsible, plus the probe name in evidence). Clang-tidy clusters: report only clusters that indicate a real defect (bugprone-*, clang-analyzer-*, performance-* that touches the audio thread, cert-*), one finding per cluster, severity low unless the check proves a bug; style-only clusters are NOT findings (the report will list them separately from the lane summary). Lints: one finding per item. category must start with "measured-" (measured-test, measured-pluginval, measured-probe, measured-tidy, measured-lint, measured-portability, measured-build). Evidence must quote the number or message verbatim.`, {
    label: 'find:measured',
    phase: 'Find',
    schema: FINDING_SCHEMA,
    model: 'opus',
  })
  if (interp) {
    const before = deduped.length
    const fresh = interp.findings.map(f => ({ ...f, dimension: 'measured', source: 'measured' }))
    const list = []
    dedupInto(list, fresh, 'measured')
    // drop any measured finding that duplicates an already-verified static one, but record the corroboration
    measured = list.filter(f => {
      const dup = verifiedStatic.filter(Boolean).find(g => isDup(g, f))
      if (dup) dup.corroborated_by_measurement = (dup.corroborated_by_measurement || []).concat(f.evidence)
      return !dup
    })
    log(`${fresh.length} measured findings -> ${measured.length} new (others corroborate static findings); static list was ${before}`)
  }
}
phase('Verify')
const verifiedMeasured = measured.length ? await verifyAll(measured, 'measured') : []

let verified = [...verifiedStatic.filter(Boolean), ...verifiedMeasured.filter(Boolean)]
verified.forEach((f, i) => { f.id = `D-${String(i + 1).padStart(3, '0')}` })
const tally = list => ({
  confirmed: list.filter(f => f.verdict === 'CONFIRMED').length,
  plausible: list.filter(f => f.verdict === 'PLAUSIBLE').length,
  refuted: list.filter(f => f.verdict === 'REFUTED').length,
  unverified: list.filter(f => f.verdict === 'UNVERIFIED').length,
})
log(`Round 1 verdicts: ${JSON.stringify(tally(verified))}`)

// ---------------------------------------------------------------------------
// Critique — Fable asks what is missing, then one bounded follow-up round.
// ---------------------------------------------------------------------------
phase('Critique')
const critique = await agent(`${CONTEXT}\n\nYou are the COMPLETENESS CRITIC of this audit. Below is everything the audit produced so far. Ask: what is missing? Which subsystem, hazard class or file got no finder attention (compare the dimension list and the maps against the file list under plugins/disrumpo/src and the 37 KrateDSP headers Disrumpo includes)? Which measurement did not run or did not cover a hazard the static finders flagged? Which REFUTED finding was refuted for a weak reason (a skeptic citing intent without a code line, or refuting on severity rather than existence)? Which CONFIRMED finding has verifier notes that actually disagree with each other?\n\nDIMENSIONS RUN: ${DIMENSIONS.map(d => d.key).join(', ')}${failedFinders.length ? ` (FAILED: ${failedFinders.join(', ')})` : ''}\nMEASUREMENT STEPS NOT RUN: ${laneNotRun.join(', ') || 'none'}\n\nINVENTORY:\n${INVENTORY_DIGEST}\n\nMAPS:\n${MAP_DIGEST}\n\nLANE SUMMARIES:\n${laneSteps.map(s => `- ${s.key}: ${s.result.summary}`).join('\n')}\n\nALL FINDINGS WITH VERDICTS (id, verdict, severity, title, file:line, verifier notes):\n${verified.map(f => `${f.id} [${f.verdict}/${f.final_severity}] ${f.title} — ${f.file}:${f.line}\n  notes: ${(f.verifier_notes || []).map(n => n.slice(0, 300)).join(' || ')}`).join('\n')}\n\nSpot-check the code where you doubt a verdict. Return up to 6 follow-up finder prompts, each naming concrete files and one hazard class, and the IDs of refuted findings that deserve a tie-break.`, {
  label: 'critique:completeness',
  phase: 'Critique',
  schema: CRITIQUE_SCHEMA,
  model: 'fable',
})

let followupAdded = 0
let rejudged = 0
if (critique) {
  log(`Critic: ${critique.gaps.length} gaps, ${critique.followup_finders.length} follow-up finders, ${critique.rejudge_ids.length} re-judgements`)
  const [followFound, rejudgeVotes] = await parallel([
    () =>
      parallel(
        critique.followup_finders.slice(0, 6).map(d => () =>
          agent(`${CONTEXT}\n\nINVENTORY:\n${INVENTORY_DIGEST}\n\nSUBSYSTEM MAPS:\n${MAP_DIGEST}\n\nALREADY REPORTED (do not repeat these; find what they missed):\n${verified.map(f => `- ${f.title} — ${f.file}:${f.line}`).join('\n')}\n\nYOUR FOLLOW-UP DIMENSION (from the completeness critic): ${d.prompt}\n\nRead the actual code before reporting anything; cite file:line you personally read.\n\nIF, AND ONLY IF, the critic asked for a MEASUREMENT you cannot make by reading: you may add ONE temporary hidden [.][audit-probe] TEST_CASE file under plugins/disrumpo/tests/audit/ (+ its line in plugins/disrumpo/tests/CMakeLists.txt), build with  ${CMAKE} --build ${BUILD_DIR} --config Release --target disrumpo_tests -- -m:2  (TWO MSBuild nodes, never more: another agent may be building too), run it by name or via  node tools/run-gate.js --share 4 --filter '[audit-probe]' disrumpo_tests , log under ${LOG_DIR}/, copy the TU and its results to ${OUT_DIR}/probes/, then  git checkout -- plugins/disrumpo/tests/CMakeLists.txt , delete tests/audit/, and confirm  git status --porcelain plugins/disrumpo  is empty. Edit nothing else. (Run wf_b866487e: two follow-up finders did this unbriefed and one rebuilt build/ from scratch after another session deleted it.)`, {
            label: `find2:${d.key}`,
            phase: 'Find',
            schema: FINDING_SCHEMA,
            model: 'opus',
          }).then(r => r.findings.map(f => ({ ...f, dimension: `followup:${d.key}`, source: 'followup' }))),
        ),
      ),
    () =>
      parallel(
        critique.rejudge_ids
          .map(id => verified.find(f => f.id === id && f.verdict === 'REFUTED'))
          .filter(Boolean)
          .map(f => () =>
            agent(`${CONTEXT}\n\nTIE-BREAK. Three skeptics refuted this finding; the completeness critic doubts the refutation. Read the code yourself and rule. Default to refuted=true only if the code positively shows the defect cannot occur.\n\nFINDING:\n${JSON.stringify(f, null, 2)}`, {
              label: `rejudge:${f.id}`,
              phase: 'Verify',
              schema: VERDICT_SCHEMA,
              model: 'fable',
            }).then(v => ({ id: f.id, v })),
          ),
      ),
  ])
  for (const r of (rejudgeVotes || []).filter(Boolean)) {
    const f = verified.find(x => x.id === r.id)
    if (f && r.v && !r.v.refuted) {
      f.verdict = 'PLAUSIBLE'
      f.verifier_notes = (f.verifier_notes || []).concat(`TIE-BREAK (Fable) upheld: ${r.v.reasoning}`)
      rejudged++
    }
  }
  const freshFollow = []
  const rawFollow = (followFound || []).filter(Boolean).flat()
  dedupInto(freshFollow, rawFollow, 'followup')
  const novel = freshFollow.filter(f => !verified.find(g => isDup(g, f)))
  log(`Follow-up: ${rawFollow.length} raw -> ${novel.length} novel; verifying...`)
  const verifiedFollow = (await verifyAll(novel, 'followup')).filter(Boolean)
  verifiedFollow.forEach((f, i) => { f.id = `D-${String(verified.length + i + 1).padStart(3, '0')}` })
  verified = verified.concat(verifiedFollow)
  followupAdded = verifiedFollow.length
}

const surviving = verified.filter(f => f.verdict === 'CONFIRMED' || f.verdict === 'PLAUSIBLE')
const refuted = verified.filter(f => f.verdict === 'REFUTED')
const unverified = verified.filter(f => f.verdict === 'UNVERIFIED')
log(`Final: ${surviving.length} surviving (${tally(surviving).confirmed} CONFIRMED, ${tally(surviving).plausible} PLAUSIBLE), ${refuted.length} refuted, ${unverified.length} unverified`)

// ---------------------------------------------------------------------------
// Synthesize — Fable writes report.md and fix-plan.md to specs/disrumpo-audit/.
// ---------------------------------------------------------------------------
phase('Synthesize')
const sevOrder = { critical: 0, high: 1, medium: 2, low: 3 }
const sortedSurviving = [...surviving].sort((a, b) => (sevOrder[a.final_severity] - sevOrder[b.final_severity]) || (a.verdict === 'CONFIRMED' ? -1 : 1))
const laneForReport = laneSteps.map(s => ({ key: s.key, ...s.result }))

const [report, plan] = await parallel([
  () =>
    agent(`${CONTEXT}\n\nYou are writing the AUDIT REPORT. Write it with the Write tool to ${OUT_DIR}/report.md (create the directory). Audience: the repo owner and a coding agent that will fix things; it must stand alone (no reference to "this session").\n\nDATE: ${DATE}. Plugin version: read plugins/disrumpo/version.json. HEAD: run  git rev-parse --short HEAD .\n\nCONTENT, in this order:\n1. Title, date, version, commit, one-paragraph executive summary (what was audited, how: ${DIMENSIONS.length} static dimensions + ${laneSteps.length} measurement steps, 3-lens adversarial verification, critic round; the headline defects).\n2. Statistics table: raw static findings ${rawStatic.length}, deduped ${deduped.length}, measured ${measured.length}, follow-up ${followupAdded}, confirmed ${tally(surviving).confirmed}, plausible ${tally(surviving).plausible}, refuted ${refuted.length}, unverified ${unverified.length}, tie-breaks upheld ${rejudged}, finders failed: ${failedFinders.join(', ') || 'none'}, measurement steps not run: ${laneNotRun.join(', ') || 'none'}.\n3. Measurement lane: one subsection per step with its summary verbatim, verdict line, log path, and a table of its items (tidy clusters as a table of check/count/files). Probe measurements as a table (name, setup, metric, value, threshold, verdict, note) — ALL of them, ok ones included, because they are the baseline for after the fixes. Probe source: ${OUT_DIR}/probes/audit_probes.cpp.\n4. Findings, grouped critical -> high -> medium -> low, CONFIRMED before PLAUSIBLE within a group. Each finding: heading "### <id> — <title>", a line with severity / verdict / category / dimension(s) incl. also_flagged_by and corroborated_by_measurement, then file:line (as a markdown link relative to the repo root), Problem, Why wrong, Consequence, Evidence, Suggested fix, and "Verifier notes" as a short bullet list (compress each note to its key sentence, keep the file:line citations). Do NOT drop or merge findings; do not soften verdicts.\n5. Inventory cross-reference tables (all of them) and the full mismatch lists.\n6. Appendix A — Refuted findings: id, title, file:line, one-line refutation reason each (so nobody re-finds them). Appendix B — Unverified findings (if any). Appendix C — Critic gaps (verbatim list).\n\nINPUT DATA:\nSURVIVING FINDINGS:\n${JSON.stringify(sortedSurviving, null, 1)}\n\nREFUTED:\n${JSON.stringify(refuted.map(f => ({ id: f.id, title: f.title, file: f.file, line: f.line, verifier_notes: f.verifier_notes })), null, 1)}\n\nUNVERIFIED:\n${JSON.stringify(unverified.map(f => ({ id: f.id, title: f.title, file: f.file, line: f.line })), null, 1)}\n\nLANE:\n${JSON.stringify(laneForReport, null, 1)}\n\nINVENTORIES:\n${JSON.stringify(goodInv, null, 1)}\n\nCRITIC GAPS:\n${JSON.stringify(critique ? critique.gaps : [], null, 1)}\n\nAfter writing, re-open the file and check every finding id from the input appears exactly once. Return the path and a summary.`, {
      label: 'synthesize:report',
      phase: 'Synthesize',
      schema: DOC_SCHEMA,
      model: 'fable',
    }),
  () =>
    agent(`${CONTEXT}\n\nYou are writing the FIX PLAN. Write it with the Write tool to ${OUT_DIR}/fix-plan.md (create the directory). It will be executed by a coding agent working task-by-task with NO memory of this audit, so it must be fully self-contained: never say "fix the issue" — spell out the exact edit intent, the file:line anchors (re-read the code to make sure the anchors are right), the before/after behaviour and the acceptance check. The companion report is ${OUT_DIR}/report.md (being written in parallel; reference finding ids, do not duplicate its prose).\n\nVERIFIED FINDINGS (CONFIRMED = no skeptic refuted; PLAUSIBLE = one did, or a tie-break upheld — mark these "verify first, then fix": the failing test doubles as the verification, and if the test passes on current code the task ends with the finding marked refuted and the test kept):\n${JSON.stringify(sortedSurviving, null, 1)}\n\nMEASUREMENT BASELINE (probe results to beat / keep):\n${JSON.stringify(laneSteps.find(s => s.key === 'probes') ? laneSteps.find(s => s.key === 'probes').result : null, null, 1)}\n\nPLAN REQUIREMENTS:\n- Markdown. Title, date ${DATE}, one-paragraph scope, then a findings-to-tasks table (finding id, title, final severity, verdict, task id, phase).\n- Phases in this order: (1) critical/high correctness and audio defects, (2) medium defects, (3) measured defects not already covered (test reds, pluginval, portability, tidy clusters that prove bugs), (4) duplication / spec-conformance / anti-pattern cleanups, (5) test-gap fills, (6) promote the useful audit probes from ${OUT_DIR}/probes/audit_probes.cpp into real regression tests (un-hidden tags, real assertions with the thresholds the report names, placed under plugins/disrumpo/tests/dsp/ or integration/, registered in plugins/disrumpo/tests/CMakeLists.txt). Within a phase order so no task depends on a later one; name hard prerequisites.\n- Every task follows this repo's mandatory workflow and says so explicitly:\n  1. write the failing test (test file to create/extend, exact TEST_CASE name and tags, what it asserts and with which helper from tests/test_helpers/)\n  2. implement the fix (exact files, functions, before/after)\n  3. build, capped: "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target disrumpo_tests -- -m:2   (add Disrumpo when plugin sources changed)\n  4. run SHARDED, never serial: node tools/run-gate.js --share 4 --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' disrumpo_tests   — and when a dsp/include or plugins/shared file was MODIFIED, first run  node tools/header-consumers.js --tag '[disrumpo]'  and add the executables its last line names to the same run-gate command\n  5. pluginval (tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Disrumpo.vst3"; exit code is the verdict) when plugin sources changed, clang-tidy (./tools/run-clang-tidy.ps1 -Target disrumpo -BuildDir build/windows-ninja -Jobs 4) before the commit, ONE heavy job at a time\n  6. commit (git add, then git commit -F <message file>; never --amend; never push)\n- Flag every task that touches dsp/include/krate/dsp/ or plugins/shared/ as CROSS-PLUGIN IMPACT and name the other consumers (grep -rl the header under plugins/).\n- Flag every task that changes saved-state layout as STATE-COMPAT: it must add a version gate and a migration test, never swap a parameter type at an existing ID.\n- Flag macOS-sensitive tasks (fast-math, Apple globals) with the local proxies to run before pushing: tools/macos-fastmath-repro.sh, tools/lint-apple-globals.js, tools/check-portability.js.\n- Give each task a time estimate in minutes for a coding agent.\n- End with a completion checklist mirroring the repo's Completion Honesty rules: each fix verified against the original finding with file:line evidence and the test's actual output, no relaxed thresholds, no quietly removed features, probes re-run and compared with the baseline above.\n\nRe-read the code for every anchor before writing it. Return the path and a summary.`, {
      label: 'synthesize:fix-plan',
      phase: 'Synthesize',
      schema: DOC_SCHEMA,
      model: 'fable',
    }),
])

return {
  stats: {
    inventories: goodInv.length,
    maps: goodMaps.length,
    finders_failed: failedFinders,
    lane_steps_run: laneSteps.map(s => s.key),
    lane_steps_not_run: laneNotRun,
    raw_static: rawStatic.length,
    deduped_static: deduped.length,
    measured_new: measured.length,
    followup_added: followupAdded,
    tiebreaks_upheld: rejudged,
    ...tally(verified),
    probe_tree_clean: (laneSteps.find(s => s.key === 'probes') || { result: {} }).result.tree_clean,
  },
  report: report,
  fix_plan: plan,
  findings: verified,
  lane: laneForReport,
  inventories: goodInv,
  critique: critique,
}
