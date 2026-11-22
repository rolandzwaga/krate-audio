export const meta = {
  name: 'ruinae-audit',
  description: 'Full audit of the Ruinae chaos/spectral hybrid synthesizer plugin (plugin code + the KrateDSP it consumes): static bug hunt across 18 dimensions, one capped measurement lane (build, sharded gate, pluginval, audio probes, clang-tidy, lints, portability), 3-lens adversarial verification, a completeness critic round, then a findings report + self-contained fix plan under specs/ruinae-audit/',
  whenToUse: 'When you want a comprehensive correctness/quality sweep of plugins/ruinae that ends in specs/ruinae-audit/report.md (every finding, verdict, evidence) and specs/ruinae-audit/fix-plan.md (ordered tasks a coding agent executes without re-deriving context). args: { date: "YYYY-MM-DD" (required for the report header), skip?: { build?, probes?, tidy?, portability? }, extra_focus?: "free text the finders must also hunt" }. Nothing commits; the tree is left clean.',
  phases: [
    { title: 'Inventory', detail: 'mechanical cross-reference tables (params, types, presets/versions)', model: 'haiku' },
    { title: 'Understand', detail: 'ten subsystem readers map the plugin and the shared DSP it uses', model: 'sonnet' },
    { title: 'Measure', detail: 'ONE serial lane, capped: lints, build -m:2, gate --share 4, pluginval, audio probes, tidy -Jobs 4, portability', model: 'sonnet' },
    { title: 'Find', detail: 'one finder per bug dimension + a measurement interpreter', model: 'opus' },
    { title: 'Verify', detail: 'three independent skeptics per finding (correctness, context, impact)', model: 'opus' },
    { title: 'Critique', detail: 'completeness critic: uncovered areas, suspicious refutations, follow-up finders', model: 'fable' },
    { title: 'Synthesize', detail: 'report.md + fix-plan.md written to specs/ruinae-audit/', model: 'fable' },
  ],
}

// ---------------------------------------------------------------------------
// Args
// ---------------------------------------------------------------------------
const DATE = (args && args.date) || 'undated'
const SKIP = (args && args.skip) || {}
const EXTRA_FOCUS = (args && args.extra_focus) || ''
const OUT_DIR = 'specs/ruinae-audit'
const LOG_DIR = 'f:/tmp/ruinae-audit'
const CMAKE = '"/c/Program Files/CMake/bin/cmake.exe"'
const BUILD_DIR = 'build/windows-x64-release'
let laneDone = false // set when the measurement lane finishes; verification runs throttled until then

// ---------------------------------------------------------------------------
// Shared context handed to every agent so they judge against the right rules.
// ---------------------------------------------------------------------------
const CONTEXT = `
You are auditing the Ruinae VST3/AU plugin — a 16-voice CHAOS/SPECTRAL HYBRID
SYNTHESIZER instrument (AU type aumu; event in, stereo out, an optional stereo
AUX sidechain input, a MIDI event output for the arpeggiator) in the monorepo at
f:/projects/iterum. Unless your task says otherwise this is READ-ONLY: do not
edit files. Read source and tests, reason about correctness, cite file:line.

ARCHITECTURE FACTS (trust these; verify details in code):
- Plugin code: plugins/ruinae/src/{processor,controller,engine,parameters,
  preset,update}/, plugin_ids.h, ruinae_types.h, entry.cpp. Tests:
  plugins/ruinae/tests/{unit,integration,fixtures}, target ruinae_tests. The
  test cases carry NO common [ruinae] tag (tags are per area: [arp], [params],
  [integration], [modmatrix], [ruinae-engine], [state], [controller], ...).
- The ENGINE is three header-only files under plugins/ruinae/src/engine/ that
  declare their classes in namespace Krate::DSP (the shared library's
  namespace): ruinae_voice.h (~1780 lines), ruinae_engine.h (~2215),
  ruinae_effects_chain.h (~1520).
- Voice signal flow (RuinaeVoice): OSC A + OSC B (two SelectableOscillator,
  10 pre-allocated OscTypes each: PolyBLEP, Wavetable, PhaseDistortion, Sync,
  Additive, Chaos, Particle, Formant, SpectralFreeze, Noise) -> Mixer
  (CrossfadeMix, or SpectralMorph through a 1024-point SpectralMorphFilter)
  -> Filter (13 RuinaeFilterTypes: 8 SVF modes, Ladder, Formant, Comb,
  EnvelopeFilter, SelfOscillating) -> Distortion (7 RuinaeDistortionTypes:
  Clean, ChaosWaveshaper, SpectralDistortion, GranularDistortion, Wavefolder,
  TapeSaturator, RingModulator) -> DC blocker -> TranceGate -> VCA. Three
  ADSR envelopes (amp, filter, mod; optional Bezier curves), a per-voice LFO
  and a VoiceModRouter. Everything is pre-allocated in prepare(); type
  switches claim to be allocation-free.
- Engine signal flow (RuinaeEngine): noteOn/Off -> VoiceAllocator (poly) or
  MonoHandler (mono, legato, portamento) -> 16 RuinaeVoice -> stereo pan +
  sum -> stereo width -> global filter (2x SVF) -> RuinaeEffectsChain ->
  master gain * 1/sqrt(N) -> soft limit -> NaN/Inf flush. NoteProcessor does
  pitch-bend smoothing and velocity curves. The global ModulationEngine
  (krate/dsp/systems/modulation_engine.h: LFO 1/2, Chaos, Rungler, Macros,
  Env Follower, Sample & Hold, Random, Pitch Follower, Transient, External0
  fed with the arp pitch) drives RuinaeModDest targets (global filter, master
  volume, effect mix, all-voice forwards, arp rate/gate/...); an 8-slot mod
  matrix (source, dest, amount + curve, smooth, scale, bypass) configures it.
- Effects chain (RuinaeEffectsChain): voice sum -> modulation slot (None /
  Phaser / Flanger / Chorus, crossfaded) -> Delay (5 RuinaeDelayTypes:
  Digital, Tape, PingPong, Granular, Spectral; crossfade switching; per-delay
  latency compensation to a constant worst case) -> Harmonizer (4 voices) ->
  Reverb (two types: Dattorro plate reverb.h and fdn_reverb.h, crossfaded).
- Processor is ONE class across five TUs: processor.cpp (lifecycle, process(),
  buses, arp block, UI playback atomics), processor_params.cpp (~1140 lines:
  processParameterChanges + the apply*Params fan-out into the engine),
  processor_state.cpp (getState/setState, applyPresetSnapshot),
  processor_midi.cpp (processEvents, the timed-event queue that splits the
  block into segments at each note event, MPE note expression, pitch bend),
  processor_messaging.cpp (notify, skip events, voice mod route state).
  Preset loads from the UI thread reach the audio thread through a lock-free
  Steinberg::Vst::RTTransferT<PresetSnapshot> drained at the top of process().
- ARPEGGIATOR: Krate::DSP::ArpeggiatorCore (krate/dsp/processors/
  arpeggiator_core.h + primitives/arp_lane.h) runs inside process() after
  processEvents() and before the engine render; its events are routed to the
  engine and/or the MIDI output bus depending on the arp operating mode. The
  core and the arp parameter IDs (3000 and up; read plugin_ids.h for the upper
  bound) are SHARED with the Gradus plugin. The shared SAVE prefix lives in
  plugins/shared/src/parameters/arp_params_common.h (saveArpParamsShared);
  loadArpParams is deliberately NOT shared (clamps and version gates differ).
- Parameters: plugin_ids.h allocates section bases in hundreds (Global 0,
  Osc A 100, Osc B 200, Mixer 300, Filter 400, Distortion 500, Trance Gate
  600, Amp/Filter/Mod Env 700/800/900, LFO 1/2 1000/1100, Chaos 1200, Mod
  Matrix 1300, Global Filter 1400, FX enables 1500, Delay 1600, Reverb 1700,
  Mono 1800, Phaser 1900, Flanger 1910, Chorus 1920, Macros 2000, Rungler
  2100, Settings 2200, Env Follower 2300, S&H 2400, Random 2500, Pitch
  Follower 2600, Transient 2700, Harmonizer 2800, Arp 3000+). Each section
  has a pack header in src/parameters/<section>_params.h holding the atomics
  struct plus handle<X>ParamChange, register<X>Params, format<X>Param,
  save<X>Params, load<X>Params and the controller-side load helper;
  dropdown_mappings.h holds the enum <-> string-list tables.
- State: kCurrentStateVersion = 8 (plugin_ids.h). getState writes the version
  then the packs in a fixed order with later additions appended behind
  version gates (reverb type v5, flanger v6, chorus v7, ...).
- Controller: plugins/ruinae/src/controller/controller.cpp (~950 lines) split
  into controller_view_sync.cpp, controller_verify_view.cpp,
  controller_mod_matrix.cpp, controller_adsr.cpp, controller_arp.cpp,
  controller_settings.cpp, controller_param_display.cpp,
  controller_presets.cpp. resources/editor.uidesc is ~4440 lines. Most custom
  views are SHARED and live in plugins/shared/src/ui/ (mod_matrix_grid,
  mod_ring_indicator, mod_heatmap, adsr_display, xy_morph_pad,
  step_pattern_editor, arp_*_lane, arp_lane_container, lfo_waveform_display,
  the mod-source displays, preset browser, save dialog, update banner).
- Presets: 168 factory .vstpreset files in 15 categories under
  plugins/ruinae/resources/presets/<Category>/, generated by
  tools/ruinae_preset_generator.cpp (+ tools/ruinae_preset_format.h). Golden
  MIDI fixtures for the arp presets: plugins/ruinae/tests/fixtures/.
- Reference docs: specs/Ruinae.md, specs/ruinae-roadmap.md,
  specs/arpeggiator-roadmap.md, specs/ruinae-preset-redesign/,
  plugins/ruinae/MANUAL.md, plugins/ruinae/docs/ (index.html,
  ruinae_signal_flow.dot). Code comments cite FR-xxx / SC-xxx from retired
  per-feature specs (041 voice architecture, 043 effects section, 044 engine
  composition, 049 mod matrix, 068 osc type params, 078 arp modulation, 079
  layout framework, 081 interaction polish, 125 dual reverb) whose folders no
  longer exist. Version: plugins/ruinae/version.json (0.12.5), CHANGELOG.md
  beside it.
- PRIOR AUDIT: ${OUT_DIR}/findings.json (42 findings, ids F001-F0xx) and
  ${OUT_DIR}/implementation-plan.md come from an earlier audit whose
  remediation was merged (PR #265). Those files are HISTORY: never overwrite
  them. A prior finding that is still present in today's code is a valid
  finding (say which F-id it repeats); one that was fixed is not.

REAL-TIME AUDIO RULES (violations are bugs): no allocation, lock, exception,
string/IO or unbounded loop on the audio thread (process(), everything the
engine, a voice, the effects chain or the arp calls per block, setActive while
processing); parameters at the VST boundary are normalized [0,1] and are
denormalized in processParameterChanges; NaN/Inf must never reach the output;
processor and controller never cross-include headers; the macOS leg compiles
with -ffast-math (classify finiteness only via Krate::DSP::detail::isFinite/
isNaN, never std::isnan or a bit-mask clone; no numeric_limits infinity()/
quiet_NaN() constants in fast-math code; clamp pow/sqrt/log arguments at domain
edges; convert host musical time to samples with std::llround, never a
truncating cast).

SYNTHESIZER-SPECIFIC HAZARDS: stuck notes (a note-on with no matching
note-off through the allocator, the mono handler, the arp, transport stop,
arp disable, preset switch, polyphony reduction); clicks at note-on, note-off,
voice steal, legato transitions, oscillator/filter/distortion type switches
and FX type crossfades; aliasing from non-band-limited oscillators, sync, FM
and waveshapers at high notes; filter instability at high resonance, near
Nyquist and at 192 kHz; self-oscillation or noise with no note held (chaos,
feedback delays, self-oscillating filter, reverb freeze); voices that never go
idle (CPU leak) or go idle before their tail ends; sample-accuracy of note
events (offset within the block) and output that changes with host block size;
pitch errors (tuning reference, pitch bend range, portamento, MPE per-note
tuning, detune); modulation applied twice, left stale when the source stops,
or scaled in the wrong unit; envelope time errors across sample rates;
tempo-sync drift and musical-time truncation in the arp, LFOs, trance gate and
delays; level explosions with 16 voices, unison or resonance (gain
compensation, soft limiter); latency reported to the host vs the real path;
state that loads into a different sound than was saved; denormals in release
tails and feedback loops.

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
          where: { type: 'string', description: 'file:line on each side, e.g. "plugin_ids.h:652 vs reverb_params.h:74"' },
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
    log_path: { type: 'string', description: 'absolute path of the captured log under f:/tmp/ruinae-audit/' },
  },
}

const PROBE_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['ran', 'probe_source_path', 'tree_clean', 'summary', 'measurements', 'log_path'],
  properties: {
    ran: { type: 'boolean' },
    probe_source_path: { type: 'string', description: 'where the probe TU was preserved (specs/ruinae-audit/probes/audit_probes.cpp) or "-"' },
    tree_clean: { type: 'boolean', description: 'git status --porcelain plugins/ruinae was empty after cleanup' },
    summary: { type: 'string' },
    measurements: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['name', 'setup', 'metric', 'value', 'threshold', 'verdict', 'note'],
        properties: {
          name: { type: 'string', description: 'short probe id, e.g. "alias:osc=Sync:note=C7"' },
          setup: { type: 'string', description: 'notes played, params, sample rate, block size, voice mode' },
          metric: { type: 'string', description: 'what was measured (aliasing dB, DC offset, peak dBFS, click count, NaN count, pitch error cents, onset sample offset, stuck voices...)' },
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
          category: { type: 'string', description: 'kebab-case: rt-safety, dsp-numerics, voice-path, voice-management, midi-timing, modulation, effects-chain, arpeggiator, lifecycle-state, ui-wiring, shared-dsp-contract, duplication, spec-conformance, portability, test-gap, anti-pattern, prior-audit-regression, measured' },
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
  const r = await agent(`${CONTEXT}\n\nYou are ONE step of the Ruinae measurement lane. Only this step runs on the machine right now, but readers are also using the box: stay inside the caps named below, never widen them. Capture every long command's output to a log file under ${LOG_DIR}/ on the FIRST run and read the log (never re-run a slow command to grep it). Create ${LOG_DIR} if missing. Do not edit repository files unless your step says so.\n\n${prompt}`, {
    label: `lane:${key}`,
    phase: 'Measure',
    ...opts,
  })
  LANE_STEPS.push({ key, result: r })
  return r
}

async function runMeasurementLane() {
  const lane = {}

  lane.lints = await laneStep('lints', `STEP: repo lints scoped to Ruinae (cheap node scripts, run them one after another, each output to its own log):
  node tools/lint-odr.js
  node tools/lint-layers.js
  node tools/lint-apple-globals.js
  node tools/lint-finite-bitmask.js
  node tools/lint-nonfinite-symbols.js
  node tools/lint-musical-time-truncation.js
  node tools/lint-unordered-serialization.js
  node tools/lint-allocation-probes.js
  node tools/lint-midi-timing-goldens.js
  node tools/lint-float-bit-goldens.js
  node tools/lint-plugin-editor-baseline.js
  node tools/lint-wallclock-tags.js
  node tools/uidesc-inventory.js plugins/ruinae/resources/editor.uidesc --check
Read each script's header comment first if its arguments are unclear (some take a path or --self-test). Report ONLY items that touch plugins/ruinae/, a plugins/shared/ file Ruinae includes, or a dsp/ header Ruinae includes (grep -rhoE '#include <krate/dsp/[^>]+>' plugins/ruinae/src gives that list; add the arpeggiator core's own includes); a tree-wide ratchet baseline hit elsewhere is not a Ruinae item. The engine headers under plugins/ruinae/src/engine/ declare classes in namespace Krate::DSP: pay attention to anything lint-odr.js says about them. kind=lint.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })

  if (!SKIP.build) {
    lane.build = await laneStep('build', `STEP: build the Ruinae test executable and the plugin bundle in ONE invocation, capped at two MSBuild nodes:
  ${CMAKE} --build ${BUILD_DIR} --config Release --target ruinae_tests Ruinae -- -m:2
The bundle target is "Ruinae" (the built bundle is ${BUILD_DIR}/VST3/Release/Ruinae.vst3); if MSBuild says the target does not exist, grep -rn RUINAE_NAME CMakeLists.txt cmake plugins/ruinae/CMakeLists.txt for the real name. If ${BUILD_DIR} does not exist, configure first: ${CMAKE} --preset windows-x64-release. A post-build copy failure into "C:/Program Files/Common Files/VST3/" is NOT an error (the compile succeeded). Report every compiler WARNING (the project requires zero) as kind=build-warning with file:line, and any error as build-error. Record the wall clock.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })

    lane.gate = await laneStep('gate', `STEP: run the per-push Ruinae suite SHARDED, taking a quarter of the machine:
  node tools/run-gate.js --share 4 --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' ruinae_tests
Run it ONCE, capture to a log, then read the log. Report the GATE GREEN / GATE RED line verbatim in summary. For every failed case report kind=test-red with the test file:line and the failed assertion text (per-shard logs are under f:/tmp/gate/). Also note skipped/hidden/[!mayfail] cases you see in the log, and the slowest five cases from shard-times.tsv. Timing tests are excluded on purpose; do NOT run tools/run-cpu-tests.js.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })

    lane.pluginval = await laneStep('pluginval', `STEP: validate the bundle:
  tools/pluginval.exe --strictness-level 5 --validate "${BUILD_DIR}/VST3/Release/Ruinae.vst3"
Capture to a log. The EXIT CODE is the verdict (there is no SUCCESS line): 0 = pass; 0xC0000005 (-1073741819) = crash. Report every FAIL/error block as kind=pluginval with the test name and message; on a crash report the last test that started.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })
  } else {
    log('Measure: build/gate/pluginval skipped by args.skip.build')
  }

  if (!SKIP.probes && !SKIP.build) {
    lane.probes = await agent(`${CONTEXT}\n\nYou are the AUDIO PROBE author of the Ruinae measurement lane. You MAY edit files, but ONLY these: create plugins/ruinae/tests/audit/audit_probes.cpp, append it to the ruinae_tests source list in plugins/ruinae/tests/CMakeLists.txt, and create ${OUT_DIR}/probes/. Everything you add to plugins/ is TEMPORARY and must be removed at the end.

GOAL: measure what code reading cannot — play real notes through the real Processor (and, where simpler, directly through RuinaeEngine / RuinaeVoice / RuinaeEffectsChain) and print numbers. Model your harness on plugins/ruinae/tests/unit/factory_preset_audio_test.cpp (loading a .vstpreset component state into a real Processor, sending a held note, rendering), plugins/ruinae/tests/integration/processor_audio_test.cpp and midi_events_test.cpp (ProcessData, event lists with sample offsets, parameter changes), preset_switch_safety_test.cpp (the preset snapshot path), unit/ruinae_engine_test.cpp, ruinae_voice_test.cpp and ruinae_effects_chain_test.cpp (direct engine access), and unit/processor/arp_integration_test.cpp (arp with a ProcessContext carrying tempo and transport). Use the shared helpers in tests/test_helpers/: spectral_analysis.h (measureAliasing / compareAliasing), artifact_detection.h (clicks/discontinuities), signal_metrics.h, audio_features.h (extractAudioFeatures, linToDbfs), test_signals.h. Read their headers for exact signatures before writing code.

Every TEST_CASE carries the tags "[.][audit-probe]" (hidden: never part of the per-push gate) and PRINTS one line per measurement to stdout in the form
  PROBE <name> | <setup> | <metric> = <value> | threshold <bar> | <ok|suspicious|bad>
using WARN() or std::printf, and asserts nothing except that the output is finite (so a probe never fails the build gate; the numbers are the result). Split the work over many TEST_CASEs so the shards balance; keep the whole probe set under ~10 minutes wall clock when sharded. Heap-allocate large render buffers (a huge local array overflows the stack and Catch2 reports "Unknown exception").

MEASURE AT LEAST (one TEST_CASE per group or sub-group, loop inside):
 1. Per OscType (all 10, OSC A alone, filter open, distortion Clean, FX off): notes A1, A4, C7 at 48 kHz -> fundamental frequency error in cents vs equal temperament at the tuning reference, aliasing level (energy at non-harmonic bins vs harmonic; not meaningful for Noise/Chaos/Particle: record spectral centroid instead), DC offset, NaN/Inf count, peak dBFS.
 2. Per RuinaeFilterType (all 13): a harmonically rich oscillator, cutoff swept 20 Hz -> 20 kHz over 2 s at resonance 0, 0.5 and maximum -> peak dBFS (runaway), NaN count, click count; the same at 192 kHz and with the cutoff parked at its maximum.
 3. Per RuinaeDistortionType (all 7): drive at 0.25 / 0.75 / 1.0 normalized -> DC offset after the DC blocker, NaN count, peak dBFS, level change vs Clean in dB.
 4. Silence: no note ever played, every FX enabled (each delay type, both reverbs, each modulation type, harmonizer), then the arp enabled with no held notes -> output RMS over 2 s (anything above -120 dBFS is self-noise) and NaN count.
 5. Voice lifecycle: amp attack 0 ms and release at minimum, note-on then note-off -> click count and largest sample-to-sample jump at both edges; play polyphony+4 overlapping notes for polyphony 1, 4, 8, 16 (voice stealing) -> click count; mono mode with legato and portamento on, overlapping notes -> pitch trajectory sanity (monotonic glide, final pitch error in cents) and click count; the same note retriggered 20 times fast.
 6. Tail and idle: play a chord, release, render 30 s -> time until the output falls below -100 dBFS and whether engine voices report inactive afterwards (active voice count must return to 0); repeat with delay feedback at maximum and reverb freeze off.
 7. Level: 1 voice vs a 16-note chord at full velocity, saw oscillators, resonance high -> peak dBFS before/after the soft limiter setting toggles; flag anything above 0 dBFS with the soft limiter on.
 8. Sample accuracy and block-size invariance: one note-on at sampleOffset 0, 1, 100 and blockSize-1 -> index of the first non-zero output sample minus the expected index (account for reported latency); render the SAME event stream with block sizes 32, 64, 512, 2048 (deterministic patch: no chaos, random, S&H or noise sources) -> max sample difference between renders.
 9. Parameter automation mid-note: step filter cutoff 0->1, resonance 0->1, master gain, osc type, filter type, distortion type, mix mode Crossfade<->SpectralMorph, voice mode poly<->mono, polyphony 16->1, delay type (all 5 in sequence), reverb type, modulation type (None/Phaser/Flanger/Chorus), each FX enable toggle -> click count and NaN count per step.
10. Effects timing: latency reported by the processor vs measured impulse arrival through the effects chain for each delay type; tempo-synced delay at 120 BPM and a 1/4 note value -> measured echo spacing in samples vs 0.5 s; reverb and delay tails decay monotonically.
11. Modulation: LFO 1 routed to filter cutoff through a mod-matrix slot at full amount -> measured cutoff swing is non-zero and periodic at the LFO rate; then set the amount back to 0 mid-note -> the modulation stops (no stale offset); every mod source routed once to a global and to a per-voice destination -> NaN count; pitch bend +/- full range -> pitch offset in cents vs the configured bend range; an MPE note-expression tuning event on one of two held notes -> only that note moves.
12. Arpeggiator: arp on, a held 3-note chord at 120 BPM with transport playing, each arp mode -> count of note-ons vs note-offs on the MIDI output bus (must balance after release), step-onset timing error in samples vs the tempo grid across 8 bars (drift), then: transport stop, arp disabled mid-note, all keys released with latch off -> every sounding note gets its note-off and the engine's active voice count returns to 0; a backward ppq jump (host loop) -> no stuck note.
13. State: for every factory preset under plugins/ruinae/resources/presets/: load via setState, hold C3 for 2 s then release, record peak / RMS / DC / NaN; then getState -> setState -> render again -> max sample difference (expect ~0 for deterministic patches; for patches with chaos/random/noise sources compare RMS within 1 dB instead and say so in the setup). Flag presets that are silent, clip above 0 dBFS, or differ after the round trip. Also feed setState a truncated stream (cut at 25 %, 50 %, 99 %) and garbage bytes -> no crash, finite output.
14. Sample-rate consistency: one representative patch at 44.1k, 48k, 96k, 192k -> fundamental frequency, RMS, measured amp attack time and release time in ms, flag deviations > 1 dB, > 5 cents or > 10 % envelope time between rates and any NaN at 192k.

PROCEDURE:
 a. Write the TU; add it to CMakeLists.
 b. Build (cap TWO nodes; this is the only build on the box): ${CMAKE} --build ${BUILD_DIR} --config Release --target ruinae_tests -- -m:2  > ${LOG_DIR}/probe-build.log 2>&1 ; read the log; fix YOUR compile errors/warnings (never touch plugin code).
 c. Run sharded, a quarter of the machine, capture:  node tools/run-gate.js --share 4 --filter '[audit-probe]' ruinae_tests > ${LOG_DIR}/probe-run.log 2>&1 ; the PROBE lines are in the per-shard logs under f:/tmp/gate/ — collect them all into ${LOG_DIR}/probe-results.txt.
 d. Preserve: copy the TU to ${OUT_DIR}/probes/audit_probes.cpp (create the dir) so the fix plan can promote the useful probes into real regression tests.
 e. CLEAN UP, mandatory: git checkout -- plugins/ruinae/tests/CMakeLists.txt ; delete plugins/ruinae/tests/audit/ ; then run  git status --porcelain plugins/ruinae  and report tree_clean = (output empty). If anything else shows up there, you broke the rule — revert it.
 f. Return every PROBE line as a measurement with a verdict and, for anything suspicious/bad, read the implicated code and name file:line in note.

Hard rules: Write tool for new files (no heredocs), one build at a time, never run tools/clean-test-output.js, never run the whole test executable as a single serial process, never commit, never touch ${OUT_DIR}/findings.json or ${OUT_DIR}/implementation-plan.md.`, { label: 'lane:probes', phase: 'Measure', schema: PROBE_SCHEMA, agentType: 'general-purpose', model: 'opus' })
    LANE_STEPS.push({ key: 'probes', result: lane.probes })
  } else {
    log('Measure: audio probes skipped')
  }

  if (!SKIP.tidy) {
    lane.tidy = await laneStep('tidy', `STEP: clang-tidy for the Ruinae area, four jobs, Windows PowerShell script only (never the .sh on Windows):
  pwsh -NoProfile -File ./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja -Jobs 4 > ${LOG_DIR}/tidy.log 2>&1
If build/windows-ninja/compile_commands.json is missing, generate it first with the ninja preset (${CMAKE} --list-presets shows the name; see .claude/skills/clang-tidy-setup/SKILL.md). If a warning looks like a phantom (header not found, unknown flag), reconfigure that preset once and re-run only the one TU. Set a wall-clock ceiling: if the script has produced no new output for 20 minutes, find the stuck clang-tidy process (Get-CimInstance Win32_Process | where CommandLine -match clang-tidy), record the TU, kill it, and report that TU as kind=tidy "hung". Report warnings CLUSTERED by check name + file (one item per cluster, count in text, one example line), kind=tidy. The total count goes in summary.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })
  } else {
    log('Measure: clang-tidy skipped by args.skip.tidy')
  }

  if (!SKIP.portability) {
    lane.portability = await laneStep('portability', `STEP: the Linux/macOS compile proxy. tools/check-portability.js syntax-checks TUs with GCC in WSL; with no arguments it only takes files changed vs origin/main (nothing, on a clean tree), so pass EXPLICIT files: every .cpp under plugins/ruinae/src and plugins/ruinae/tests (build the list with git ls-files and pass it on one command line), output captured to ${LOG_DIR}/portability.log. Read its header comment first for any extra flags. It boots WSL; leave WSL running afterwards (never shut it down or restart it: the user runs WSL in other sessions). Report every error/warning it prints for plugins/ruinae/ or an included dsp/ or plugins/shared/ header as kind=portability with file:line. Note in summary what the script does NOT cover (it is a compile proxy, not fast-math semantics).`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })
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
    prompt: `Cross-reference the PARAMETER plumbing, section by section (one table row per parameter pack under plugins/ruinae/src/parameters/). Sides: (1) plugin_ids.h — every concrete ID in the section's range; (2) the pack's register<X>Params (ID, default, step count/type, units, flags); (3) the pack's handle<X>ParamChange (which IDs are consumed, the denormalization formula) and the atomics struct's member initializers; (4) the pack's format<X>Param (displayed range/units); (5) save<X>Params vs load<X>Params (order, type, count written vs read, clamps) plus the controller-side load helper for the same pack, and the call order in plugins/ruinae/src/processor/processor_state.cpp getState vs setState vs Controller::setComponentState in controller.cpp (version gates on each side); (6) the apply*Params function in processor_params.cpp that pushes the atomics into the engine (is every atomic forwarded?); (7) resources/editor.uidesc control-tags — run  node tools/uidesc-inventory.js plugins/ruinae/resources/editor.uidesc --out ${LOG_DIR}/uidesc-inventory.json  (create ${LOG_DIR} first) and read the JSON. Report mismatches: registered but never consumed; consumed but never registered; consumed but never applied to the engine; saved but not loaded or loaded in a different order/type; processor load vs controller load disagree; default differs between registration, atomic initializer and the load fallback; a uidesc control-tag that is not a known ID; a registered automatable parameter with no control (output/readout parameters don't count); a denormalization range that differs from the displayed range; OSC A vs OSC B, LFO 1 vs LFO 2 and the three envelope packs that should mirror each other but do not.`,
  },
  {
    key: 'types',
    prompt: `Cross-reference the ENUM <-> STRING <-> SWITCH tables. Enums: OscType (dsp/include/krate/dsp/systems/oscillator_types.h), MixMode / RuinaeFilterType / RuinaeDistortionType / RuinaeDelayType (plugins/ruinae/src/ruinae_types.h), ModulationType (engine/ruinae_effects_chain.h), the reverb type (int: plate vs FDN), VoiceMode (systems/poly_synth_engine.h), AllocationMode / StealMode (systems/voice_allocator.h), ModSource (core/modulation_types.h), VoiceModSource / VoiceModDest (systems/voice_mod_types.h), RuinaeModDest (engine/ruinae_engine.h), the arp mode / octave mode / latch / retrigger / condition / modifier enums (processors/arpeggiator_core.h and plugins/ruinae/src/parameters/arpeggiator_params.h), LFO waveforms, note values (core/note_value.h, parameters/note_value_ui.h). For each: the string list in plugins/ruinae/src/parameters/dropdown_mappings.h (count, order, names), the StringListParameter step count in the pack's register function, the clamp in the pack's load function, every switch over the enum in engine/*.h and processor_params.cpp (missing case, fallthrough, default that hides a new member), the mod-matrix source/destination tables in controller_mod_matrix.cpp, controller/mod_ring_dest_ids.h and plugins/shared/src/ui/mod_matrix_types.h, and the names in plugins/ruinae/MANUAL.md and docs/index.html. Report: enum members without a switch branch; table lengths that disagree with the enum count; names/orders that differ between enum, UI menu, manual and docs; a load clamp narrower or wider than the enum.`,
  },
  {
    key: 'presets-versions',
    prompt: `Cross-reference PRESETS, VERSIONS and DOCS. (1) Count factory presets per category under plugins/ruinae/resources/presets/ (168 files in 15 categories today) and compare with the categories in plugins/ruinae/src/preset/ruinae_preset_config.h, the preset browser tab list in controller_presets.cpp, the generator source tools/ruinae_preset_generator.cpp (+ tools/ruinae_preset_format.h: does its field order and state version match processor_state.cpp getState and kCurrentStateVersion?) and the golden MIDI fixtures in plugins/ruinae/tests/fixtures/ (a fixture without a preset, an arp preset without a fixture). (2) version.json vs src/version.h vs CHANGELOG.md top entry vs docs/index.html vs installers/ metadata vs resources/au-info.plist and resources/auv3 vs the update config under src/update/. (3) MANUAL.md, README.md, docs/index.html and CHANGELOG claims that name a number or feature (voice count, oscillator types, filter types, delay types, mod-matrix slots, arp lanes/steps, harmonizer voices) vs the constants in code. (4) plugins/ruinae/CLAUDE.md statements vs the filesystem (file names, line counts within 20 %, the arp ID range). Report every disagreement with both values verbatim.`,
  },
]

const SUBSYSTEMS = [
  {
    key: 'voice',
    prompt: `Map the VOICE: plugins/ruinae/src/engine/ruinae_voice.h read END TO END (by offsets; it is ~1780 lines), plus dsp/include/krate/dsp/systems/selectable_oscillator.h and oscillator_types.h. Cover: prepare/reset/noteOn/noteOff/processBlock order of operations, how the two oscillators are pitched (tune, detune, portamento glide, pitch bend, per-voice modulation), the CrossfadeMix vs SpectralMorph mixer (latency of the FFT path, what happens on a mode switch mid-note), the filter dispatch over 13 types (per-sample vs block, state reset on type switch, key tracking, envelope amount units), the distortion dispatch over 7 types (which are block-based, their latency, reset on switch), DC blocker, TranceGate, VCA, the three envelopes (Bezier mode), the per-voice LFO, VoiceModRouter offsets (units, clamps), SharedOscResources, and when the voice reports itself inactive.`,
  },
  {
    key: 'engine-voice-management',
    prompt: `Map the ENGINE and NOTE HANDLING: plugins/ruinae/src/engine/ruinae_engine.h read END TO END (by offsets; ~2215 lines), plugins/ruinae/src/processor/processor_midi.cpp, and the shared pieces it drives: dsp/include/krate/dsp/systems/voice_allocator.h, processors/mono_handler.h, processors/note_processor.h, systems/poly_synth_engine.h (VoiceMode only). Cover: noteOn/noteOff paths with and without noteId (MPE), poly vs mono dispatch, legato and portamento, allocation and steal modes, unison if any, setPolyphony and setMode while notes sound, processBlock -> processBlockPoly / processBlockMono, the ModSubBlock mechanism, panning and stereo spread/width, global filter, gain compensation 1/sqrt(N), soft limiter, NaN flush, getLatencySamples, and in processor_midi.cpp the timed-event queue (capacity, ordering, overflow behaviour), renderEngineSegments (how the block is split at event offsets), note expression and pitch bend handling.`,
  },
  {
    key: 'modulation',
    prompt: `Map the MODULATION system: dsp/include/krate/dsp/systems/modulation_engine.h (public API, sources, routing, per-block vs per-sample), systems/voice_mod_router.h and voice_mod_types.h, core/modulation_types.h; in the plugin: the RuinaeModDest handling and global-offset application in engine/ruinae_engine.h, applyModSourceParams / applyModRoutingParams / applyAuxModSourceParams in processor/processor_params.cpp, parameters/mod_matrix_params.h, macro_params.h, lfo_params.h + lfo1/lfo2_params.h, chaos_mod_params.h, rungler_params.h, env_follower_params.h, sample_hold_params.h, random_params.h, pitch_follower_params.h, transient_params.h, the sidechain extraction in processor.cpp process(), and Processor::sendVoiceModRouteState in processor_messaging.cpp. Cover: every source and destination with its unit and scaling, which destinations are global vs forwarded to all voices vs per voice, curve/smooth/scale/bypass per slot, what happens to an offset when a slot is cleared or a source stops, tempo sync of LFOs, the arp-pitch External0 feed and the arp destinations.`,
  },
  {
    key: 'effects-chain',
    prompt: `Map the EFFECTS CHAIN: plugins/ruinae/src/engine/ruinae_effects_chain.h read END TO END (by offsets; ~1520 lines) and applyDelayParams / applyReverbParams / applyModulationEffectParams / applyHarmonizerParams / applyGlobalFilterAndFxEnableParams in processor/processor_params.cpp, with parameters/delay_params.h, reverb_params.h, phaser_params.h, flanger_params.h, chorus_params.h, modulation_effect_params.h, harmonizer_params.h, fx_enable_params.h. Cover: processBlock -> processChunk chunking, the modulation slot crossfade (None/Phaser/Flanger/Chorus), the delay slot (5 types, crossfade length, what is processed during a crossfade, per-type latency compensation delay lines, constant latency reporting), the harmonizer (enable fade, latency, pitch-shift mode), the reverb slot (two types, crossfade, freeze, pre-delay), enable/disable fades, tempo sync (where BlockContext comes from), buffer ownership and sizes vs maxBlockSize, reset().`,
  },
  {
    key: 'arpeggiator',
    prompt: `Map the ARPEGGIATOR integration: the arp block in plugins/ruinae/src/processor/processor.cpp process() (read it fully), applyArpParams in processor_params.cpp, parameters/arpeggiator_params.h (~1730 lines: read by offsets), plugins/shared/src/parameters/arp_params_common.h, and the shared core dsp/include/krate/dsp/processors/arpeggiator_core.h + primitives/arp_lane.h (public API and the processBlock contract: event buffer capacity, what happens when it overflows, how note-offs are guaranteed). Cover: operating modes (off / MIDI / mod / both) and the MIDI-out switch, how note events reach the arp vs the engine in processEvents, transport handling (the hostSupportsTransport heuristic, loop detection by backward ppq, syncToMusicalPosition, the musical-time to samples conversion), setEnabled(false) cleanup, lanes (velocity, gate, pitch, modifier, ratchet, condition, chord, inversion, ...), per-lane swing and length jitter, playhead output parameters and their encoding, skip events to the controller, the arp-as-mod-source path and arp modulation destinations, and everything that differs from Gradus's use of the same core (plugins/gradus/src/processor/ — skim).`,
  },
  {
    key: 'params-state-messaging',
    prompt: `Map parameters, persistence and messaging: plugins/ruinae/src/plugin_ids.h, processor/processor_params.cpp (processParameterChanges dispatch by ID range and applyParamsToEngine: what is applied every block vs on change), processor/processor_state.cpp (getState, setState incl. every version gate and the truncated-stream behaviour, applyPresetSnapshot), processor.h (PresetSnapshot, RTTransferT stateTransfer_, atomics), processor/processor_messaging.cpp (notify message ids, pointer-carrying messages, sendSkipEvent, sendVoiceModRouteState), src/preset/ruinae_preset_config.h, Controller::setComponentState and the controller's getState/setState, controller_presets.cpp (preset load path vs host setState path), tools/ruinae_preset_format.h. Cover: the save format (version, order, counts), load clamps, migration of old versions (tests/unit/state_migration_test.cpp and preset_format_compat_test.cpp show the supported ones), which UI state is persisted in the controller, output parameters (playheads, sidechain-active), and the lifetime of anything shared by pointer between processor and controller.`,
  },
  {
    key: 'controller-ui',
    prompt: `Map the controller + UI: plugins/ruinae/src/controller/ (controller.h, controller.cpp, controller_view_sync.cpp, controller_verify_view.cpp, controller_mod_matrix.cpp, controller_adsr.cpp, controller_arp.cpp, controller_settings.cpp, controller_param_display.cpp, controller_presets.cpp, adsr_expanded_overlay.h, mod_ring_dest_ids.h — read each by offsets), resources/editor.uidesc structure (templates, custom views, control-tags; scan, not every pixel), and the shared views it instantiates from plugins/shared/src/ui/ (public API only). Cover: editor lifecycle (createView, didOpen, willClose, the update banner + version label rule), every raw view pointer the controller caches and where each is nulled (willClose, tab change, mod-source view-mode switch, UIViewSwitchContainer template swaps), IDependent add/remove balance, timers, the mod-matrix grid <-> parameters sync, mod ring indicators, ADSR display + expanded overlay, morph pad, arp lane editors and playhead polling, trance-gate step editor, preset browser and save dialog, settings drawer, parameter display formatting.`,
  },
  {
    key: 'shared-dsp-synth',
    prompt: `Map the SHARED DSP the VOICE wraps (dsp/include/krate/dsp/): systems/selectable_oscillator.h and each oscillator it selects (find them from its includes), primitives/adsr_envelope.h, svf.h, ladder_filter.h, comb_filter.h, dc_blocker.h, lfo.h, chaos_waveshaper.h, wavefolder.h; processors/formant_filter.h, envelope_filter.h, self_oscillating_filter.h, spectral_morph_filter.h, spectral_distortion.h, granular_distortion.h, ring_modulator.h, tape_saturator.h, trance_gate.h; core/pitch_utils.h, db_utils.h, sigmoid.h. For EACH: the API contract (prepare/reset/process signatures, per-sample vs block, parameter ranges and units, band-limiting strategy, stability limits, whether it can produce output from silence, internal latency, NaN guards, allocations outside prepare). Then note how ruinae_voice.h calls it (prepared with the right block size? reset on type switch and on noteOn? ranges respected? latency ignored?). Oddities = contract violations you suspect.`,
  },
  {
    key: 'shared-dsp-fx-infra',
    prompt: `Map the SHARED DSP the ENGINE and EFFECTS CHAIN rely on (dsp/include/krate/dsp/): effects/digital_delay.h, tape_delay.h, ping_pong_delay.h, granular_delay.h, spectral_delay.h, reverb.h, fdn_reverb.h; processors/phaser.h, flanger.h, chorus.h; systems/harmonizer_engine.h; primitives/delay_line.h, smoother.h (time constants, snap behaviour); core/block_context.h, note_value.h, transport_sync.h, crossfade_utils.h, scoped_denormal_mode.h, math_constants.h. Same coverage: contracts, mono vs stereo state, latency, maximum delay vs prepared buffer, feedback limits, tempo-sync inputs, NaN/denormal behaviour, fast-math hazards, allocations outside prepare — and how ruinae_effects_chain.h / ruinae_engine.h honour them. These headers are consumed by other plugins too (Iterum uses the delays): note cross-plugin exposure for each suspected defect.`,
  },
  {
    key: 'specs-and-tests',
    prompt: `Map the SPEC CLAIMS and the TEST SUITE. Read specs/Ruinae.md, skim specs/ruinae-roadmap.md and specs/arpeggiator-roadmap.md and plugins/ruinae/MANUAL.md; list the testable claims (numbers, ranges, behaviours) as invariants with their source line. Then read plugins/ruinae/tests/CMakeLists.txt and skim every test file's TEST_CASE names and tags (grep is fine) to record what each file covers, which cases are hidden/[.]/[!mayfail]/[long], which assertions look tautological or too loose (Approx margins that swallow the effect, "is not silent" as the only check), which tests run on vstgui_test_stubs.cpp instead of the real views, and which spec claims have no test. Also read ${OUT_DIR}/findings.json and the task list of ${OUT_DIR}/implementation-plan.md (the PRIOR audit) and record, per prior finding id, the regression test the plan asked for and whether a test with that name exists today. Oddities = claims the code probably violates, tests that cannot fail, prior findings whose regression test is missing.`,
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
    prompt: `Hunt REAL-TIME SAFETY violations on the audio thread: allocation (std::vector growth/resize/assign, std::function, std::string, new, make_unique, map insert, a variant emplace that constructs a heap-owning type), locks, exceptions (at(), stoi, throw paths in SDK calls), IO/logging, unbounded loops, blocking calls, reachable from Processor::process(), processEvents, the timed-event queue, applyParamsToEngine and every apply*Params, applyPresetSnapshot, RuinaeEngine::processBlock, RuinaeVoice::processBlock and its type-switch setters (the headers CLAIM zero allocation on type switch: prove or disprove per type), RuinaeEffectsChain::processBlock and its type/enable setters, ArpeggiatorCore::processBlock, IMessage allocation/sending from process() (sendSkipEvent, sendVoiceModRouteState, sendOneTimeHandshakes), and setActive/setupProcessing while running; atomics with wrong memory order or non-lock-free types; data shared with the UI thread without an atomic or a transfer object. Focus: plugins/ruinae/src/processor/*.cpp, src/engine/*.h, and the shared headers they call (dsp/include/krate/dsp/...).`,
  },
  {
    key: 'dsp-numerics',
    prompt: `Hunt NUMERICAL defects: NaN/Inf sources (0/0 in gain compensation or pan normalisation, log/pow/sqrt/acos at domain edges, tan at pi/2 in SVF/ladder coefficient updates when modulation pushes cutoff past Nyquist or at 192 kHz, pitch math with extreme tune/bend/modulation sums, division by a smoothed value that can be 0, envelope times of 0 ms), filter blow-up at maximum resonance with audio-rate modulation, denormals in release tails / feedback delays / reverbs / the self-oscillating filter, std::isnan / bit-mask finiteness clones / numeric_limits infinity() in fast-math code (does the engine's final NaN/Inf flush survive -ffast-math?), float == comparisons, exactness-by-cancellation at neutral points (mix 50 %, pan centre, morph position 0/1, zero modulation), smoother time constants derived before sampleRate is set, uninitialised state read on the first block, dbToGain of -inf, clamps missing at the VST boundary, the soft limiter's behaviour with non-finite input. Focus: src/engine/*.h, processor.cpp, processor_params.cpp, and the KrateDSP headers Ruinae includes.`,
  },
  {
    key: 'voice-signal-path',
    prompt: `Hunt VOICE SIGNAL-PATH bugs in ruinae_voice.h and the DSP it calls: oscillator pitch computation (tune + detune + glide + bend + modulation: units, order, clamping), phase reset / retrigger rules per OscType, OSC A vs OSC B asymmetries that are not intended, the SpectralMorph mixer (FFT latency vs the Crossfade path, clicks and stale spectra on a mode switch, morph position mapping, spectral tilt), filter dispatch (a type switch that leaves stale state or does not reset the incoming filter, key tracking and envelope amount in the wrong unit, cutoff modulation applied per block where per sample is needed, envelope filter / self-oscillating filter special cases), distortion dispatch (block-based types processing a partially filled block, latency of spectral/granular distortion not accounted, drive/character mapping, mix law), DC blocker placement and cutoff, TranceGate (step timing, tempo sync, attack/release shaping, depth 0 must be transparent), VCA and velocity, envelope retrigger/legato behaviour, the voice-inactive decision (cut tails or immortal voices). Focus: plugins/ruinae/src/engine/ruinae_voice.h, systems/selectable_oscillator.h, the primitives/processors it includes.`,
  },
  {
    key: 'voice-management-midi',
    prompt: `Hunt VOICE MANAGEMENT and MIDI bugs: note-on/note-off pairing through VoiceAllocator and MonoHandler (same pitch pressed twice, note-off for a stolen voice, noteId vs pitch matching for MPE, note-off arriving in the same block as its note-on, velocity 0 note-on), voice stealing (the stolen voice's fade, the same slot allocated twice, steal modes), setPolyphony reduction while notes sound, poly<->mono switch with held notes, legato and portamento (stale glide on voice reuse, glide from the wrong start pitch, mono priority modes), sustain/all-notes-off handling if any, pitch bend smoothing and range, note expression routing to the right voice, the timed-event queue in processor_midi.cpp (capacity and what is dropped on overflow, ordering of events with equal offsets, offsets >= numSamples or negative, events when numSamples == 0), renderEngineSegments (off-by-one at segment edges, sidechain pointer advance, a segment of 0 samples), sample accuracy vs the arp's events, behaviour when setActive(false) or a preset snapshot arrives with voices sounding. Focus: plugins/ruinae/src/processor/processor_midi.cpp, src/engine/ruinae_engine.h, systems/voice_allocator.h, processors/mono_handler.h, processors/note_processor.h.`,
  },
  {
    key: 'modulation',
    prompt: `Hunt MODULATION bugs: a destination offset that stays stale when its slot is cleared, bypassed or its source stops (look for "!= 0.0f" guards around forwarding), modulation applied twice (global offset AND per-voice forward for the same target), wrong unit or scale per destination (normalized vs Hz vs semitones vs dB), bipolar vs unipolar source handling, curve/smooth/scale per slot, slot index or source/destination index off-by-one between the UI tables, the parameter packs and the engine enums (RuinaeModDest starts at 64; VoiceModDest is separate), per-block modulation of targets that need sub-block resolution (ModSubBlock), LFO tempo sync and phase reset on note/transport, Rungler / S&H / Random determinism and seeding, Env Follower / Pitch Follower / Transient fed from the sidechain vs self-analysis (switching between them, the sidechain bus being inactive or mono or having null channel pointers), macros, the arp-pitch External0 source, modulation of arp destinations, sendVoiceModRouteState consistency with what the audio thread uses. Focus: systems/modulation_engine.h, systems/voice_mod_router.h, engine/ruinae_engine.h (global offsets), processor_params.cpp (applyModSourceParams, applyModRoutingParams, applyAuxModSourceParams), parameters/mod_matrix_params.h and the mod-source packs, processor.cpp sidechain block.`,
  },
  {
    key: 'effects-chain',
    prompt: `Hunt EFFECTS-CHAIN bugs in ruinae_effects_chain.h and the effects it hosts: crossfades (a type switch requested during a running crossfade, the completion block processed through both the outgoing and the incoming effect or through one of them twice, the outgoing effect not reset so its tail returns on the next switch), enable/disable fades and what keeps running while disabled (tails, CPU), LATENCY: per-delay-type compensation lines sized for the worst case, the latency the processor reports to the host vs the real path with spectral delay / harmonizer / spectral distortion / the SpectralMorph mixer active, latency changing at run time without a restartComponent; chunking in processBlock/processChunk when numSamples exceeds the prepared maxBlockSize; delay time and LFO tempo sync without a valid tempo, note-value mapping, sync toggles mid-note; feedback >= 1 and runaway, tape delay head/splice options, ping-pong width, granular and spectral delay parameter ranges; reverb freeze, pre-delay bounds, plate vs FDN parameter mapping parity and level match; harmonizer voices (interval/scale/key mapping, per-voice delay and detune, dry/wet levels in dB vs linear, pitch-shift mode switch); the "effect mix" modulation destination; stereo correctness (one stateful mono instance used for both channels). Focus: plugins/ruinae/src/engine/ruinae_effects_chain.h, processor_params.cpp apply*Params for FX, dsp/include/krate/dsp/effects/*.h, processors/{phaser,flanger,chorus}.h, systems/harmonizer_engine.h.`,
  },
  {
    key: 'arpeggiator',
    prompt: `Hunt ARPEGGIATOR bugs in Ruinae's integration and in the shared core as Ruinae drives it: stuck notes (arp disabled or operating mode changed mid-note, transport stop, host loop / backward ppq jump, latch release, polyphony change, preset switch, setActive(false)), the fixed-size event buffer (arpEvents_ capacity vs worst-case ratchets x chord x lanes; what is dropped), note events delivered to the engine at the wrong sample offset or all at offset 0, events routed to the MIDI output bus but not the engine (or both when only one is selected), the hostSupportsTransport heuristic (a host that sets kPlaying once then never again; offline render), syncToMusicalPosition every block vs drift, the musical-time -> samples conversion (truncating casts; lint-musical-time-truncation territory), tempo changes mid-block, free-rate vs tempo-sync, swing and per-lane swing, per-lane length jitter, lane lengths vs the step-count dropdown, playhead output parameter encoding (stepIndex / 32 and the 1.0 sentinel), skip-event messages allocated on the audio thread, the arp-as-mod-source value held after the arp stops, state: loadArpParams clamps and version gates vs what saveArpParamsShared writes (mode clamps 0-9 here), parameter IDs that Gradus defines but Ruinae does not handle. Cite the core's contract line AND Ruinae's call site. Focus: plugins/ruinae/src/processor/processor.cpp (arp block), processor_params.cpp applyArpParams, parameters/arpeggiator_params.h, plugins/shared/src/parameters/arp_params_common.h, dsp/include/krate/dsp/processors/arpeggiator_core.h, primitives/arp_lane.h.`,
  },
  {
    key: 'lifecycle-state',
    prompt: `Hunt VST3 LIFECYCLE and STATE bugs: getState/setState order/type/count mismatches per pack, missing or wrong version gates (a field appended without bumping kCurrentStateVersion, a gate that reads a field older versions never wrote), setState's early "return kResultTrue" on a truncated stream leaving a half-applied state, a newer-than-known version handled as garbage, defaults drift between registration, atomic initializers and load fallbacks, processor load vs Controller::setComponentState divergence (the controller reading the same stream with different gates), the PresetSnapshot / RTTransferT path (fields missing from the snapshot, a snapshot applied while notes sound, two loads in quick succession, snapshot vs host setState ordering), the preset browser load path vs the host path, mod-matrix voice routes saved as int8 (range, sign), UI-only state persistence, tools/ruinae_preset_format.h and the generator drifting from getState, setActive(false)/(true) not resetting voices and effects, setupProcessing with a changed sample rate or block size not re-preparing every component, setBusArrangements (sidechain mono/stereo/absent, output not stereo), initialize/terminate balance, the arp and timed-event queue state across setActive. Focus: processor_state.cpp, processor.cpp, processor.h, processor_messaging.cpp, controller.cpp, controller_presets.cpp, src/preset/*, the save/load functions in src/parameters/*.h.`,
  },
  {
    key: 'ui-wiring',
    prompt: `Hunt UI WIRING bugs, starting from the INVENTORY mismatches (confirm each in code before reporting): control-tags vs plugin_ids.h, params registered but absent from the uidesc or vice versa, StringListParameter entry counts vs enum ranges, DANGLING VIEW POINTERS: every raw pointer to a view the controller caches must be nulled in willClose AND when its tab / UIViewSwitchContainer template / mod-source view mode is switched away (the prior audit found three of these: check that the fixes hold and that views added since follow the rule), IDependent not removed, forget() imbalance, timers that fire after willClose, verifyView handlers that assume a sibling view exists, the mod-matrix grid <-> the 56 mod-matrix parameters (two-way sync loops, normalised vs plain, slot detail params), mod ring indicators reading destination IDs that moved (mod_ring_dest_ids.h), ADSR display and expanded overlay <-> Bezier params, morph pad <-> mixer position / spectral tilt and its modulated-position animation, arp lane editors <-> lane step params and the playhead polling timer, trance-gate step editor, parameter display strings vs the DSP's real range, preset browser / save dialog lifetime, pointer-carrying IMessages used after the processor is gone, the update banner / version label rule (plugins/shared/src/ui/update_banner_view.h). Focus: controller/*.cpp|h, resources/editor.uidesc, and the shared views under plugins/shared/src/ui/ that Ruinae instantiates.`,
  },
  {
    key: 'shared-dsp-contracts',
    prompt: `Hunt CONTRACT VIOLATIONS between Ruinae and the KrateDSP it wraps: a processor whose process() expects a block but is called per sample (or the reverse), prepare() never called or called with a maxBlockSize smaller than what processBlock later passes, reset() not called on type switch or note-on (stale filter/granular/spectral buffers), ONE stateful instance used for both channels, parameter setters given normalised values where the primitive expects plain (or dB vs linear, Hz vs normalised, ms vs seconds), ranges outside what the primitive clamps (resonance, feedback >= 1, stage counts, voice counts), latency of primitives with internal FFT/lookahead never summed into the reported latency, setters that allocate or recompute expensive tables when called every block from apply*Params, components that emit output from silence (chaos, self-oscillating filter, feedback delays, reverb freeze) with no gate, VoiceAllocator / MonoHandler / NoteProcessor API misuse (calling order, return values ignored), ArpeggiatorCore and ModulationEngine preconditions. Cite BOTH sides (Ruinae call site AND the primitive's contract line). A defect in the shared header itself is CROSS-PLUGIN IMPACT: name the other plugins that include it. Focus: plugins/ruinae/src/engine/*.h, processor_params.cpp, and the dsp/include/krate/dsp headers Ruinae includes.`,
  },
  {
    key: 'duplication-drift',
    prompt: `Hunt DUPLICATION and COPY-PASTE DRIFT: OSC A vs OSC B (osc_a_params.h / osc_b_params.h over osc_params.h), LFO 1 vs LFO 2, the three envelope packs over env_params.h, phaser / flanger / chorus packs, the five delay-type parameter groups — find the copy where one mirror differs (an ID offset, a range, a default, a missing field in save/load); the same denormalisation formula written in a pack's handle function, its format function, the controller-side loader, tools/ruinae_preset_generator.cpp and tests with subtle differences; constants defined twice with different values (max voices, max steps, ranges); enum<->string tables maintained in two places (dropdown_mappings.h vs shared UI vs the manual); arp code that Gradus and Ruinae each keep a copy of when plugins/shared has (or should have) the single original (compare plugins/gradus/src/parameters/ with plugins/ruinae/src/parameters/arpeggiator_params.h: report DRIFT, not the deliberate differences named in the architecture facts); logic that exists in plugins/shared/ (preset browser, update checker, MIDI helpers) but was re-implemented locally; repeated blocks across the controller_*.cpp files and the apply*Params functions that should be one helper; plugin-local classes declared in namespace Krate::DSP whose names could collide with dsp/ (ODR). Focus: plugins/ruinae/src/**, plugins/shared/src/**, plugins/gradus/src/parameters/**.`,
  },
  {
    key: 'parallel-paths',
    prompt: `Hunt PARALLEL PATHS: two or more separate code routes that do the SAME job, where one should exist. This is not copy-paste drift (another finder covers that); the routes can look nothing alike. Method (use the Serena MCP tools when available, ToolSearch "serena": mcp__serena__find_referencing_symbols finds every caller of an entry point, including template and virtual call sites grep misses; fall back to Grep): first list every job the plugin performs at a boundary, then, for each job, list EVERY entry point that performs it (grep the callers, follow each to where the work ends), then report every job with more than one route. Jobs to enumerate at minimum: loading a full state (host setState, preset browser load, factory/default preset, undo/compare, any IMessage that carries state), loading a partial state (section/arp-only presets), saving state (getState vs any preset-save path), applying one parameter value (processParameterChanges vs any IMessage or direct setter), resetting the engine (setActive, setState, transport stop, preset load, panic), ending sounding notes (note-off, all-notes-off, reset, bypass, preset switch, arp disable), sending processor data to the controller, and versioning/migrating old state. A route that applies a state or value WITHOUT going through the single canonical function (e.g. the controller replaying parameters one by one while the processor never runs setState, or a message that sets engine fields the state parser also sets) is a finding even when today both routes give the same result: name what each route does that the other does not (resets, flushes, non-parameter data, version gates, clamps), and the observable consequence. When an earlier finding or fix plan "completes" a second route by adding the missing piece to it, report that as reinforcing the duplication. For each finding give the job, every route with file:line, the canonical route it should collapse into, and a test that proves the routes leave identical state (e.g. getState bytes after route A vs route B). Focus: plugins/ruinae/src/** (processor_state.cpp, processor_messaging.cpp, controller.cpp, controller_presets.cpp, controller_state*.cpp), plugins/shared/src/preset/**, plugins/shared/src/parameters/arp_params_common.h.`,
  },
  {
    key: 'spec-conformance',
    prompt: `Hunt SPEC CONFORMANCE failures: take the testable claims from the specs-and-tests map (and read specs/Ruinae.md, the relevant parts of specs/ruinae-roadmap.md and specs/arpeggiator-roadmap.md, and plugins/ruinae/MANUAL.md yourself for numbers: ranges, counts, voice limits, latency, signal-flow order, modulation sources/destinations, arp behaviours), then check the code honours each one; also check that code comments citing an FR-xxx / SC-xxx actually implement what the comment says (the per-feature spec folders are gone, so judge the comment against the code and against specs/Ruinae.md; comments that lie are findings — e.g. the effects-chain header still describes "Voice Sum -> Phaser -> Delay -> Harmonizer -> Reverb" and a single Dattorro reverb), docs/index.html, docs/ruinae_signal_flow.dot, README.md and CHANGELOG claims against behaviour. Report the claim, its source line, and the code line that contradicts it.`,
  },
  {
    key: 'portability-quality',
    prompt: `Hunt CROSS-PLATFORM and CODE-QUALITY problems: narrowing in brace-init that MSVC accepts but Clang rejects, std::isnan / bit-mask finiteness under -ffast-math, numeric_limits infinity()/quiet_NaN() constants in DSP code, truncating casts of musical time, iteration over an unordered container that feeds serialization, names that collide with Apple globals/macros (MAC, Rect, Point, Component, Fixed, Boolean...), missing includes that MSVC forgives (<cstdint>, <cstring>, <algorithm>, <array>), signed/unsigned mixups in loop bounds, uninitialised members, float == comparisons, unused results, misleading names/comments that contradict behaviour, God functions (the 2000-line engine header: name the worst), magic numbers duplicating named constants, dead code and dead parameters, naming-convention violations (members without trailing underscore, constants without k), header-only engine code with non-inline definitions. Focus: plugins/ruinae/src/** and plugins/ruinae/tests/**.`,
  },
  {
    key: 'test-gaps',
    prompt: `Hunt TEST DEFECTS and COVERAGE GAPS in plugins/ruinae/tests/: assertions that cannot fail (REQUIRE(true), checking only non-silence, Approx margins wider than the effect), golden fixtures that are self-generated with no independent check (tests/fixtures/*_golden_midi.txt: how are they produced and what would a wrong golden look like?), bit-exact float comparisons that will go red on macOS, tests that pass on a stubbed path (vstgui_test_stubs.cpp) and would not on the real view, duplicated tests, hidden/[!mayfail]/commented-out cases hiding known reds, wall-clock assertions without the [wallclock] tag, and MISSING coverage for the riskiest behaviours: stuck notes after arp disable / transport stop / preset switch, voice stealing clicks, poly<->mono switch with held notes, block-size invariance and sample-accurate note offsets, every oscillator / filter / distortion type for NaN and runaway level at 192 kHz, type switches mid-note, FX type crossfades, reported latency vs real latency, setState with truncated/corrupt data, every state version migration (1 through 8), every factory preset rendered (audibility exists: what about clipping, DC, round trip?), sidechain bus absent/mono, MPE note expression, pitch bend range, tuning reference. For each gap name the test file to extend, the TEST_CASE name and the assertion.`,
  },
  {
    key: 'prior-audit-regressions',
    prompt: `You are the PRIOR-AUDIT REGRESSION checker. Read ${OUT_DIR}/findings.json (the earlier audit's verified findings) and, for each one, the matching task in ${OUT_DIR}/implementation-plan.md (grep the finding id). For EVERY prior finding: open today's code at the place the finding names (line numbers have moved: locate by function and symbol), decide whether the defect is (a) fixed and guarded by a test, (b) fixed with no regression test, (c) partially fixed (one site fixed, a sibling site with the same pattern still wrong), or (d) still present / reintroduced. Report a finding for every (c) and (d) (category prior-audit-regression, put the prior id in the title, e.g. "F007 still present: ..."), and a low-severity test-gap finding for each (b) that names the missing test. Do not report (a). Also look for the SAME defect patterns in code added after that audit (new views with cached pointers, new mod destinations with stale-offset guards, new crossfades).`,
  },
  {
    key: 'gestalt-processor',
    prompt: `You are the GESTALT reviewer for the processor: read plugins/ruinae/src/processor/processor.h, processor.cpp, processor_params.cpp, processor_midi.cpp, processor_state.cpp and processor_messaging.cpp END TO END, top to bottom, and flag anything that does not make sense — a comment that says one thing while the body does another, branches that cannot execute, work done twice per block (applyParamsToEngine pushes EVERY parameter into the engine EVERY block: which setters are expensive or reset state when called with an unchanged value?), values computed then ignored, ordering that looks accidental (parameter changes vs preset snapshot vs events vs arp vs render), block-rate handling of sample-accurate automation, early returns that skip cleanup or leave output buffers uncleared (the "no audio to process" and "no valid output" paths, silence flags), TODO/FIXME hiding bugs, copy-paste leftovers from Iterum (Ruinae was scaffolded from it: look for delay-plugin names, IDs or assumptions). Your value is noticing what category-driven review misses.`,
  },
  {
    key: 'gestalt-engine',
    prompt: `You are the GESTALT reviewer for the engine: read plugins/ruinae/src/engine/ruinae_engine.h, then ruinae_voice.h, then ruinae_effects_chain.h END TO END by offsets, top to bottom, and flag anything that does not make sense — header comments that describe an older design (signal-flow order, effect list, feature lists), setters that store a value nobody reads, getters returning stale state, two code paths for the same job that have drifted (processBlockPoly vs processBlockMono; processDelayTypeRaw vs the crossfade path; the per-type reverb/modulation dispatchers), members initialised in prepare() but not restored by reset() (or the reverse), loops over all 16 voices where only active ones should count, scratch buffers reused by two stages in the same block, sizes derived from maxBlockSize that a later chunk exceeds, constants that look tuned by accident. Your value is noticing what category-driven review misses.`,
  },
  {
    key: 'gestalt-controller',
    prompt: `You are the GESTALT reviewer for the UI side: read plugins/ruinae/src/controller/controller.h and controller.cpp END TO END, then controller_view_sync.cpp, controller_verify_view.cpp, controller_mod_matrix.cpp, controller_presets.cpp, controller_arp.cpp, controller_adsr.cpp, controller_settings.cpp and controller_param_display.cpp by offsets, and flag anything that does not make sense — repeated boilerplate with one copy subtly different, string-list parameters whose entries disagree with the DSP enums, handlers that update a view but not the parameter (or vice versa), lifecycle code that assumes the editor exists, a method living in the wrong controller_*.cpp, state kept in the controller that the processor never learns about (and is lost on reload), Iterum leftovers, dead handlers, misleading comments. Your value is noticing what category-driven review misses.`,
  },
]

phase('Find')
log(`Fanning out ${DIMENSIONS.length} Opus finders...`)
const found = await parallel(
  DIMENSIONS.map(d => () =>
    agent(`${CONTEXT}\n\nINVENTORY CROSS-REFERENCES (mechanical mismatches; confirm in code before reporting):\n${INVENTORY_DIGEST}\n\nSUBSYSTEM MAPS from the understanding phase (leads in ODDITIES are gold):\n${MAP_DIGEST}\n\nYOUR DIMENSION: ${d.prompt}\n\nRead the actual code before reporting anything. Quality over quantity: a finding without a concrete file:line you personally read is worthless; one real bug beats ten maybes. Do not report style nits unless they hide a defect. Severity: critical = wrong/unsafe audio, stuck notes, crash or data loss in normal use; high = audible defect or host-visible misbehaviour in common use; medium = edge-case defect or maintenance trap; low = quality.`, {
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
  'CONTEXT: is it intentional, guarded elsewhere (a clamp upstream, a reset in setActive, a test that pins it), unreachable in a real host, or already documented as a design decision in specs/Ruinae.md, the roadmaps, the manual or a code comment?',
  'IMPACT: assume the defect is real — does it produce the claimed consequence at the claimed severity? Quantify (dB, cents, samples, which hosts/settings trigger it); downgrade or upgrade accordingly, and refute if the consequence cannot occur.',
]
// Verification is throttled while the measurement lane still runs: a full-width
// fan-out (N findings x 3 lenses) fills every agent slot and the lane's next
// step (tidy, portability) queues behind it for an hour (disrumpo-audit run
// wf_b866487e, 2026-11-05). Chunks of 4 findings = 12 agents leave slots for the
// lane; full width once it is done. (laneDone is declared with the args at the
// top so the lane's .then never hits a TDZ.)
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
          agent(`${CONTEXT}\n\nA reviewer claims this defect in Ruinae. Your job is to REFUTE it if you can. Lens: ${lens}\n\nFINDING:\n${JSON.stringify(f, null, 2)}\n\nRead ${f.file} around line ${f.line} AND every code path the claim depends on (callers, the shared DSP header it uses, the test that covers it). Default to refuted=true if you cannot positively confirm the defect from source or from the quoted measurement.`, {
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
  const interp = await agent(`${CONTEXT}\n\nSUBSYSTEM MAPS:\n${MAP_DIGEST}\n\nMEASUREMENT LANE RESULTS (facts produced by running the code; the probe source is at ${OUT_DIR}/probes/audit_probes.cpp if it ran):\n${laneDigest}\n\nTurn these into FINDINGS. Rules: every test-red, pluginval failure, build warning, portability error, probe "bad" and probe "suspicious" measurement becomes a finding IF you can localise it — read the implicated code and cite file:line (for a probe, the DSP code responsible, plus the probe name in evidence). Before blaming the plugin for a probe result, read the probe's own code for that measurement: a harness mistake (wrong parameter ID, note never released, latency not subtracted) is NOT a finding. Clang-tidy clusters: report only clusters that indicate a real defect (bugprone-*, clang-analyzer-*, performance-* that touches the audio thread, cert-*), one finding per cluster, severity low unless the check proves a bug; style-only clusters are NOT findings (the report will list them separately from the lane summary). Lints: one finding per item. category must start with "measured-" (measured-test, measured-pluginval, measured-probe, measured-tidy, measured-lint, measured-portability, measured-build). Evidence must quote the number or message verbatim.`, {
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

// Finding ids are R-nnn; the prior audit's ids (F0nn in findings.json) stay distinct.
let verified = [...verifiedStatic.filter(Boolean), ...verifiedMeasured.filter(Boolean)]
verified.forEach((f, i) => { f.id = `R-${String(i + 1).padStart(3, '0')}` })
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
const critique = await agent(`${CONTEXT}\n\nYou are the COMPLETENESS CRITIC of this audit. Below is everything the audit produced so far. Ask: what is missing? Which subsystem, hazard class or file got no finder attention (compare the dimension list and the maps against the file list under plugins/ruinae/src — every parameter pack under src/parameters/ included — and the ~50 KrateDSP headers Ruinae includes)? Which measurement did not run or did not cover a hazard the static finders flagged? Which REFUTED finding was refuted for a weak reason (a skeptic citing intent without a code line, or refuting on severity rather than existence)? Which CONFIRMED finding has verifier notes that actually disagree with each other?\n\nDIMENSIONS RUN: ${DIMENSIONS.map(d => d.key).join(', ')}${failedFinders.length ? ` (FAILED: ${failedFinders.join(', ')})` : ''}\nMEASUREMENT STEPS NOT RUN: ${laneNotRun.join(', ') || 'none'}\n\nINVENTORY:\n${INVENTORY_DIGEST}\n\nMAPS:\n${MAP_DIGEST}\n\nLANE SUMMARIES:\n${laneSteps.map(s => `- ${s.key}: ${s.result.summary}`).join('\n')}\n\nALL FINDINGS WITH VERDICTS (id, verdict, severity, title, file:line, verifier notes):\n${verified.map(f => `${f.id} [${f.verdict}/${f.final_severity}] ${f.title} — ${f.file}:${f.line}\n  notes: ${(f.verifier_notes || []).map(n => n.slice(0, 300)).join(' || ')}`).join('\n')}\n\nSpot-check the code where you doubt a verdict. Return up to 6 follow-up finder prompts, each naming concrete files and one hazard class, and the IDs of refuted findings that deserve a tie-break.`, {
  label: 'critique:completeness',
  phase: 'Critique',
  schema: CRITIQUE_SCHEMA,
  model: 'fable',
})

let followupAdded = 0
let rejudged = 0
if (critique) {
  log(`Critic: ${critique.gaps.length} gaps, ${critique.followup_finders.length} follow-up finders, ${critique.rejudge_ids.length} re-judgements`)
  if (critique.followup_finders.length > 6) log(`Critic returned ${critique.followup_finders.length} follow-up finders; only the first 6 run`)
  const [followFound, rejudgeVotes] = await parallel([
    () =>
      parallel(
        critique.followup_finders.slice(0, 6).map(d => () =>
          agent(`${CONTEXT}\n\nINVENTORY:\n${INVENTORY_DIGEST}\n\nSUBSYSTEM MAPS:\n${MAP_DIGEST}\n\nALREADY REPORTED (do not repeat these; find what they missed):\n${verified.map(f => `- ${f.title} — ${f.file}:${f.line}`).join('\n')}\n\nYOUR FOLLOW-UP DIMENSION (from the completeness critic): ${d.prompt}\n\nRead the actual code before reporting anything; cite file:line you personally read.\n\nIF, AND ONLY IF, the critic asked for a MEASUREMENT you cannot make by reading: you may add ONE temporary hidden [.][audit-probe] TEST_CASE file under plugins/ruinae/tests/audit/ (+ its line in plugins/ruinae/tests/CMakeLists.txt), build with  ${CMAKE} --build ${BUILD_DIR} --config Release --target ruinae_tests -- -m:2  (TWO MSBuild nodes, never more: another agent may be building too), run it via  node tools/run-gate.js --share 4 --filter '[audit-probe]' ruinae_tests , log under ${LOG_DIR}/, copy the TU and its results to ${OUT_DIR}/probes/ (a new file name: never overwrite audit_probes.cpp), then  git checkout -- plugins/ruinae/tests/CMakeLists.txt , delete tests/audit/, and confirm  git status --porcelain plugins/ruinae  is empty. Edit nothing else.`, {
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
  verifiedFollow.forEach((f, i) => { f.id = `R-${String(verified.length + i + 1).padStart(3, '0')}` })
  verified = verified.concat(verifiedFollow)
  followupAdded = verifiedFollow.length
}

const surviving = verified.filter(f => f.verdict === 'CONFIRMED' || f.verdict === 'PLAUSIBLE')
const refuted = verified.filter(f => f.verdict === 'REFUTED')
const unverified = verified.filter(f => f.verdict === 'UNVERIFIED')
log(`Final: ${surviving.length} surviving (${tally(surviving).confirmed} CONFIRMED, ${tally(surviving).plausible} PLAUSIBLE), ${refuted.length} refuted, ${unverified.length} unverified`)

// ---------------------------------------------------------------------------
// Synthesize — Fable writes report.md and fix-plan.md to specs/ruinae-audit/.
// ---------------------------------------------------------------------------
phase('Synthesize')
const sevOrder = { critical: 0, high: 1, medium: 2, low: 3 }
const sortedSurviving = [...surviving].sort((a, b) => (sevOrder[a.final_severity] - sevOrder[b.final_severity]) || (a.verdict === 'CONFIRMED' ? -1 : 1))
const laneForReport = laneSteps.map(s => ({ key: s.key, ...s.result }))

const [report, plan] = await parallel([
  () =>
    agent(`${CONTEXT}\n\nYou are writing the AUDIT REPORT. Write it with the Write tool to ${OUT_DIR}/report.md (the directory exists; leave findings.json and implementation-plan.md from the prior audit untouched). Audience: the repo owner and a coding agent that will fix things; it must stand alone (no reference to "this session").\n\nDATE: ${DATE}. Plugin version: read plugins/ruinae/version.json. HEAD: run  git rev-parse --short HEAD .\n\nCONTENT, in this order:\n1. Title, date, version, commit, one-paragraph executive summary (what was audited, how: ${DIMENSIONS.length} static dimensions + ${laneSteps.length} measurement steps, 3-lens adversarial verification, critic round; the headline defects), and one line saying how this audit relates to the prior one (findings.json / implementation-plan.md, remediated in PR #265; ids here are R-nnn).\n2. Statistics table: raw static findings ${rawStatic.length}, deduped ${deduped.length}, measured ${measured.length}, follow-up ${followupAdded}, confirmed ${tally(surviving).confirmed}, plausible ${tally(surviving).plausible}, refuted ${refuted.length}, unverified ${unverified.length}, tie-breaks upheld ${rejudged}, finders failed: ${failedFinders.join(', ') || 'none'}, measurement steps not run: ${laneNotRun.join(', ') || 'none'}.\n3. Measurement lane: one subsection per step with its summary verbatim, verdict line, log path, and a table of its items (tidy clusters as a table of check/count/files). Probe measurements as a table (name, setup, metric, value, threshold, verdict, note) — ALL of them, ok ones included, because they are the baseline for after the fixes; the per-preset state probe may go in its own table. Probe source: ${OUT_DIR}/probes/audit_probes.cpp.\n4. Findings, grouped critical -> high -> medium -> low, CONFIRMED before PLAUSIBLE within a group. Each finding: heading "### <id> — <title>", a line with severity / verdict / category / dimension(s) incl. also_flagged_by and corroborated_by_measurement, then file:line (as a markdown link relative to the repo root), Problem, Why wrong, Consequence, Evidence, Suggested fix, and "Verifier notes" as a short bullet list (compress each note to its key sentence, keep the file:line citations). Do NOT drop or merge findings; do not soften verdicts.\n5. Prior-audit status: a table of the prior finding ids that this audit found still present or partially fixed (from the prior-audit-regressions dimension), with the R-id that tracks each.\n6. Inventory cross-reference tables (all of them) and the full mismatch lists.\n7. Appendix A — Refuted findings: id, title, file:line, one-line refutation reason each (so nobody re-finds them). Appendix B — Unverified findings (if any). Appendix C — Critic gaps (verbatim list).\n\nINPUT DATA:\nSURVIVING FINDINGS:\n${JSON.stringify(sortedSurviving, null, 1)}\n\nREFUTED:\n${JSON.stringify(refuted.map(f => ({ id: f.id, title: f.title, file: f.file, line: f.line, verifier_notes: f.verifier_notes })), null, 1)}\n\nUNVERIFIED:\n${JSON.stringify(unverified.map(f => ({ id: f.id, title: f.title, file: f.file, line: f.line })), null, 1)}\n\nLANE:\n${JSON.stringify(laneForReport, null, 1)}\n\nINVENTORIES:\n${JSON.stringify(goodInv, null, 1)}\n\nCRITIC GAPS:\n${JSON.stringify(critique ? critique.gaps : [], null, 1)}\n\nAfter writing, re-open the file and check every finding id from the input appears exactly once. Return the path and a summary.`, {
      label: 'synthesize:report',
      phase: 'Synthesize',
      schema: DOC_SCHEMA,
      model: 'fable',
    }),
  () =>
    agent(`${CONTEXT}\n\nYou are writing the FIX PLAN. Write it with the Write tool to ${OUT_DIR}/fix-plan.md (the directory exists; never touch implementation-plan.md, the PRIOR audit's plan — its command blocks are outdated, do not copy them). It will be executed by a coding agent working task-by-task with NO memory of this audit, so it must be fully self-contained: never say "fix the issue" — spell out the exact edit intent, the file:line anchors (re-read the code to make sure the anchors are right), the before/after behaviour and the acceptance check. The companion report is ${OUT_DIR}/report.md (being written in parallel; reference finding ids, do not duplicate its prose).\n\nVERIFIED FINDINGS (CONFIRMED = no skeptic refuted; PLAUSIBLE = one did, or a tie-break upheld — mark these "verify first, then fix": the failing test doubles as the verification, and if the test passes on current code the task ends with the finding marked refuted and the test kept):\n${JSON.stringify(sortedSurviving, null, 1)}\n\nMEASUREMENT BASELINE (probe results to beat / keep):\n${JSON.stringify(laneSteps.find(s => s.key === 'probes') ? laneSteps.find(s => s.key === 'probes').result : null, null, 1)}\n\nPLAN REQUIREMENTS:\n- Markdown. Title, date ${DATE}, one-paragraph scope, then a findings-to-tasks table (finding id, title, final severity, verdict, task id, phase).\n- Phases in this order: (1) critical/high correctness and audio defects (stuck notes, crashes, wrong audio first), (2) medium defects, (3) measured defects not already covered (test reds, pluginval, portability, tidy clusters that prove bugs), (4) duplication / spec-conformance / anti-pattern cleanups, (5) test-gap fills, (6) promote the useful audit probes from ${OUT_DIR}/probes/audit_probes.cpp into real regression tests (un-hidden tags, real assertions with the thresholds the report names, placed under plugins/ruinae/tests/unit/ or integration/, registered in plugins/ruinae/tests/CMakeLists.txt; a case that renders more than ~15 s of toolchain-independent audio gets the [long] tag, a wall-clock assertion gets [wallclock]). Within a phase order so no task depends on a later one; name hard prerequisites.\n- Every task follows this repo's mandatory workflow and says so explicitly:\n  1. write the failing test (test file to create/extend, exact TEST_CASE name and tags, what it asserts and with which helper from tests/test_helpers/); for a defect in a shared DSP header the test goes in dsp/tests/unit/<layer>/ and its dsp_<layer>_tests target\n  2. implement the fix (exact files, functions, before/after)\n  3. build, capped: "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests -- -m:2   (add Ruinae when plugin sources changed; add the dsp_<layer>_tests target when a dsp/ file changed)\n  4. run SHARDED, never serial: node tools/run-gate.js --share 4 --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' ruinae_tests   — and when a dsp/include or plugins/shared file was MODIFIED, first run  node tools/header-consumers.js --tag '[ruinae]'  (Ruinae's cases carry no common tag, so ruinae_tests itself is in its list: that is expected) and add the executables its last line names to the same run-gate command, building them first\n  5. pluginval (tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"; exit code is the verdict) when plugin sources changed, clang-tidy (./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja -Jobs 4) before the commit, ONE heavy job at a time\n  6. commit (git add, then git commit -F <message file>; never --amend; never push)\n- Flag every task that touches dsp/include/krate/dsp/ or plugins/shared/ as CROSS-PLUGIN IMPACT and name the other consumers (grep -rl the header under plugins/). A task that touches the arpeggiator core, arp_lane.h or arp_params_common.h is GRADUS IMPACT: it must also build gradus_tests and run it in the same gate, and must keep the cross-plugin byte-golden (tests/test_helpers/arp_shared_prefix_golden.h) green.\n- Flag every task that changes saved-state layout as STATE-COMPAT: it must bump kCurrentStateVersion, add a version gate on BOTH the processor and the controller load path, update tools/ruinae_preset_format.h + the generator if the factory presets are affected, add a migration test beside tests/unit/state_migration_test.cpp, and never swap a parameter type at an existing ID or renumber an arp ID (they are shared with Gradus).\n- Flag every task whose fix changes how a factory preset sounds or the MIDI an arp preset emits as PRESET-IMPACT: name the fixtures under plugins/ruinae/tests/fixtures/ that must be regenerated and how to justify the new golden.\n- Flag macOS-sensitive tasks (fast-math, Apple globals, musical-time rounding) with the local proxies to run before pushing: tools/macos-fastmath-repro.sh, tools/lint-apple-globals.js, tools/lint-musical-time-truncation.js, tools/check-portability.js.\n- Give each task a time estimate in minutes for a coding agent.\n- End with a completion checklist mirroring the repo's Completion Honesty rules: each fix verified against the original finding with file:line evidence and the test's actual output, no relaxed thresholds, no quietly removed features, probes re-run and compared with the baseline above.\n\nRe-read the code for every anchor before writing it. Return the path and a summary.`, {
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
