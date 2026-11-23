export const meta = {
  name: 'membrum-audit',
  description: 'Full audit of the Membrum physically-modelled drum synthesizer plugin (plugin code + the KrateDSP it consumes): static bug hunt across 23 dimensions, a literature check of the physical model, one capped measurement lane (lints, build, sharded gate, pluginval, audio probes, kit-quality probes, clang-tidy, portability), 3-lens adversarial verification, a completeness critic round, then a findings report + self-contained fix plan under specs/membrum-audit/',
  whenToUse: 'When you want a comprehensive correctness/quality sweep of plugins/membrum that ends in specs/membrum-audit/report.md (every finding, verdict, evidence, the physical-model review and the kit-quality tables) and specs/membrum-audit/fix-plan.md (ordered tasks a coding agent executes without re-deriving context). args: { date: "YYYY-MM-DD" (required for the report header), skip?: { build?, probes?, quality?, research?, tidy?, portability? }, extra_focus?: "free text the finders must also hunt" }. tools/membrum-fit is out of scope. Nothing commits; the tree is left clean.',
  phases: [
    { title: 'Inventory', detail: 'mechanical cross-reference tables (params, types, presets/versions)', model: 'haiku' },
    { title: 'Understand', detail: 'eleven subsystem readers map the plugin and the shared DSP it uses', model: 'sonnet' },
    { title: 'Measure', detail: 'ONE serial lane, capped: lints, build -m:2, gate --share 4, pluginval, audio probes, kit-quality probes, tidy -Jobs 4, portability', model: 'sonnet' },
    { title: 'Research', detail: 'physical-model claims checked against published acoustics / DSP literature (web), four agents at a time', model: 'opus' },
    { title: 'Find', detail: 'one finder per bug dimension + a measurement interpreter', model: 'opus' },
    { title: 'Verify', detail: 'three independent skeptics per finding (code: correctness, context, impact; physics: code, literature, context+impact)', model: 'opus' },
    { title: 'Critique', detail: 'completeness critic: uncovered areas, suspicious refutations, follow-up finders', model: 'fable' },
    { title: 'Synthesize', detail: 'report.md + fix-plan.md written as parallel part files (report sections, finding chunks, a planned task list in batches), then joined and id-checked', model: 'fable' },
  ],
}

// ---------------------------------------------------------------------------
// Args
// ---------------------------------------------------------------------------
const DATE = (args && args.date) || 'undated'
const SKIP = (args && args.skip) || {}
const EXTRA_FOCUS = (args && args.extra_focus) || ''
const OUT_DIR = 'specs/membrum-audit'
const LOG_DIR = 'f:/tmp/membrum-audit'
const CMAKE = '"/c/Program Files/CMake/bin/cmake.exe"'
const BUILD_DIR = 'build/windows-x64-release'
let laneDone = false // set when the measurement lane finishes; verification runs throttled until then

// ---------------------------------------------------------------------------
// Shared context handed to every agent so they judge against the right rules.
// ---------------------------------------------------------------------------
const CONTEXT = `
You are auditing the Membrum VST3/AU plugin — a 32-pad PHYSICALLY-MODELLED DRUM
SYNTHESIZER instrument (modal synthesis; AU type aumu; VST3 subcategory
Instrument|Drum; MIDI event in, one stereo main output plus up to 15 stereo aux
output buses, no audio input, no MIDI output) in the monorepo at
f:/projects/iterum. Unless your task says otherwise this is READ-ONLY: do not
edit files. Read source and tests, reason about correctness, cite file:line.

ARCHITECTURE FACTS (trust these; verify details in code):
- Plugin code: plugins/membrum/src/{processor,controller,dsp,voice_pool,state,
  preset,ui,update}/, plugin_ids.h, entry.cpp, version.h. There is NO
  parameters/ directory. Tests: plugins/membrum/tests/{unit,integration,
  approval,golden,perf}, target membrum_tests (~670 cases). The cases carry NO
  common [membrum] tag (tags are per area: [pad_config], [voice_pool],
  [multi-bus], [render], [macro_mapper], ...); perf cases are hidden
  ([.perf]...[benchmark]); tests/unit/diagnose/ holds diagnostic renders.
  Static libs: membrum_core (processor / controller / UI / voice-pool TUs),
  membrum_preset_io (state codec), membrum_float_guard.
- PAD MODEL: 32 pads = MIDI notes 36..67 (kFirstDrumNote..kLastDrumNote; other
  notes are ignored). Each pad owns a PadConfig (dsp/pad_config.h: every float
  is NORMALIZED [0,1] and is denormalized at the point of use), stored in
  VoicePool::padConfigs_. Per-pad parameter ID = kPadBaseId (1000) + pad *
  kPadParamStride (128) + PadParamOffset; offsets 0..65 are active
  (kPadActiveParamCountV12 = 66). Global IDs sit below 1000 (plugin_ids.h):
  100-104 Material / Size / Decay / Strike Position / Level, 200-244 exciter +
  body selectors, tone shaper, unnatural zone, material morph, 250-252 max
  polyphony [4,16] / voice stealing / choke group, 260 selected pad, 270-273
  cross-pad coupling, 280 UI mode (session scoped, NOT in processor state),
  282 output bus, 290-326 noise / click layer, damping, air loading, shell
  coupling, tension, pad enable, pitch-envelope knee, pan, wire coupling, 320
  master gain. Most global IDs are SELECTED-PAD PROXIES: a write is forwarded
  to the selected pad's per-pad ID (Controller::forwardGlobalToPad /
  syncGlobalProxyFromPad; the processor side is Processor::
  processParameterChanges, processor.cpp ~145-535).
- PER-VOICE SIGNAL PATH (dsp/drum_voice.h, ~1780 lines; the comment at the top
  of that file describes an OLDER design): at note-on the pad config is pushed
  into the voice (dsp/pad_config_apply.h applyPadConfigToVoice, VoicePool::
  applyPadConfigToSlot), the active body's mapper computes the modes
  (dsp/bodies/*_mapper.h) and the strike is level-normalised to kBodyHeadroom
  (0.5 = -6 dBFS strike peak). Per block, in chunks of kMaxBlockSize: the pitch
  envelope (tone_shaper.h + pitch_segment_envelope.h, three-point) and the
  Membrane-only tension modulation retune the body once per chunk; Material
  Morph refreshes the mapper once per chunk; ExciterBank (7 ExciterTypes:
  Impulse, Mallet, NoiseBurst, Friction, FMImpulse, Feedback, Clap) plus the
  always-on ClickLayer (half amplitude into the excitation, full amplitude
  into the output) -> BodyBank (6 BodyModelTypes: Membrane, Plate, Shell,
  String, Bell, NoiseBody; the modal bodies share one Krate::DSP::
  ModalResonatorBank, String is a Krate::DSP::WaveguideString) -> optional
  secondary shell bank (feedforward body -> shell, mixed in additively) ->
  parallel NoiseLayer (x standaloneGain x per-pad noiseLayerGain x the
  wire-coupling gain) -> UnnaturalZone (modeInject, nonlinearCoupling) ->
  ToneShaper (drive -> wavefolder -> DC blocker -> SVF with a filter envelope)
  -> amp envelope x level -> hardClip safety rail. THREE renderers exist:
  process() (single sample), processBlockFast (block-rate body through the
  SIMD bank) and processBlockSlow (per sample; used only for the Feedback
  exciter); they must stay behaviour-equivalent. A voice retires when the bank
  reports zero active modes and the amp envelope has finished, with a
  256-silent-block safety net.
- VOICE POOL (voice_pool/voice_pool.{h,cpp}): 16 main DrumVoice slots plus 16
  shadow "releasing" slots for the 5 ms fast-release crossfade (steal, choke),
  driven by Krate::DSP::VoiceAllocator; maxPolyphony 4..16; stealing policies
  Oldest / Quietest / Priority; 8 choke groups (0 = none); per-pad enable;
  per-pad equal-power pan applied in the pool mix (the voice is mono); per-pad
  output bus (a voice is written to the main bus and additionally to its aux
  bus when that bus is active); kVoicePoolMaxBlock = 8192.
- PROCESSOR (processor/processor.{h,cpp}, ONE TU, ~1270 lines). process():
  ScopedDenormalMode guard -> processParameterChanges -> processEvents
  (note-on / note-off for pitches 36..67; a velocity-0 note-on is a note-off)
  -> consumePendingAudition (UI pad clicks arrive as IMessage "AuditionPad" /
  "AuditionPadOff" packed into one atomic word) -> VoicePool::processBlock
  (main + aux) -> cross-pad COUPLING on the main bus when the engine is not
  bypassed (mono sum -> couplingDelay_ 0.5..2 ms -> Krate::DSP::
  SympatheticResonance -> one-pole energy limiter capped at -20 dBFS -> added
  to L and R) -> main-bus Krate::DSP::TruePeakLimiter at -1 dBTP -> master
  gain (-24..+12 dB, default -6 dB; main bus only: the aux buses are
  pre-limiter, pre-master sends) -> silence flags -> PadGlowPublisher ->
  MetersBlock (peaks, active voices, CPU per-mille, pad glow buckets) through
  a Steinberg DataExchangeHandler. CouplingMatrix (dsp/coupling_matrix.h) +
  PadCategory (dsp/pad_category.h: Kick / Snare / Tom / HatCymbal / Perc,
  derived from each pad's config) decide which pads excite which. MacroMapper
  (processor/macro_mapper.{h,cpp}) turns five per-pad macros (Tightness,
  Brightness, Body Size, Punch, Complexity) into deltas on underlying pad
  parameters. The processor reports no latency (no getLatencySamples).
- STATE: ONE codec, state/state_codec.{h,cpp} (namespace Membrum::State):
  writeKitBlob / readKitBlob (the processor state and kit presets share it)
  and writePadPresetBlob / readPadPresetBlob (one pad's sound).
  kCurrentStateVersion = kBlobVersion = kPadBlobVersion = 5, 59 sound slots
  per pad, and the reader accepts ONLY that version. Code comments say "the
  plugin has not shipped"; release tags membrum/v0.9.0 .. v0.12.5 exist, so
  whether a released build wrote another blob version is something to CHECK,
  never to assume. Several header comments still describe v3 / v4, 57 slots or
  a 64-wide pad stride: the constants are the truth, stale comments are leads.
  uiMode is session scoped: written into kit presets only (hasSession).
- LOAD PATHS: host setState -> readKitBlob -> Processor::applyKitSnapshot;
  kit-preset browser load -> Controller::kitPresetLoadProvider -> IMessage
  "LoadPresetState" (the blob) -> Processor::notify -> applyKitSnapshot; pad
  preset -> "LoadPadPreset" -> Processor::applyPadPresetSnapshot.
  Processor::notify runs on the host's message thread. Controller side:
  controller/controller_state.cpp (setComponentState, the kit / pad preset
  state and load providers) through controller/controller_state_codec.{h,cpp}
  (KitSnapshot <-> parameter values).
- CONTROLLER: controller/controller.cpp (~1760 lines: initialize registers
  every parameter, setParamNormalized with the proxy forwarding,
  createCustomView, verifyView, didOpen / willClose, the DataExchange
  receiver), controller_view_sync.cpp, controller_param_format.cpp,
  controller_state.cpp. resources/editor.uidesc is ~430 lines (templates
  EditorDefault, SelectedPadSimple, SelectedPadAdvanced; a
  UIViewSwitchContainer swaps the Acoustic / Extended panel). Plugin-local
  views live in src/ui/ (PadGridView, KitMetersView, PolyphonySlider,
  InlinePresetBrowserView, ADSRExpandedOverlayView, membrum_buttons.h); shared
  views come from plugins/shared/src/ui/ (ArcKnob, FieldsetContainer,
  XYMorphPad, ADSRDisplay, PitchEnvelopeDisplay, ToggleButton,
  OutlineActionButton, preset browser, save dialog, update banner). The
  controller caches about two dozen raw view pointers (controller.h ~188-405)
  and runs a 30 Hz poll timer.
- PRESETS: 20 factory KIT presets (.vstpreset), 5 in each of the four FIXED
  categories Acoustic / Electronic / Percussive / Unnatural, under
  plugins/membrum/resources/presets/Kit Presets/<Category>/, generated by
  tools/membrum_preset_generator.cpp (which re-declares the codec constants).
  The build's install step copies them to <ProgramData>/Krate Audio/Membrum/
  Kits/. Pad presets (one pad's sound) use Membrum/Pads with subcategories
  Kick, Snare, Tom, Hat, Cymbal, Perc, Tonal, FX. dsp/default_kit.h builds the
  default kit. Design constraints that are INTENDED: maxPolyphony stays in
  [4,16]; modeInject > 0 rings undamped; the Friction exciter sustains without
  a note-off. Goldens: plugins/membrum/tests/golden/*.bin
  (approval/test_phase1_regression.cpp).
- KrateDSP consumed (dsp/include/krate/dsp/): processors/
  modal_resonator_bank.h, waveguide_string.h, impact_exciter.h, bow_exciter.h,
  fm_operator.h, harmonic_oscillator_bank.h, harmonic_types.h,
  envelope_follower.h, true_peak_limiter.h; systems/sympathetic_resonance.h,
  voice_allocator.h; primitives/svf.h, noise_oscillator.h, adsr_envelope.h,
  dc_blocker.h, waveshaper.h, wavefolder.h, tanh_adaa.h, envelope_utils.h,
  delay_line.h; core/xorshift32.h, sigmoid.h, scoped_denormal_mode.h,
  math_constants.h, curve_table.h, pattern_freeze_types.h. Several are shared
  with other plugins (Innexus, Ruinae, Iterum).
- Reference docs: specs/135-membrum-drum-synth.md and specs/136..141-membrum-
  phase*/ (spec.md, plan.md, research.md, data-model.md, contracts/; code
  comments cite their FR-xxx / SC-xxx), plugins/membrum/MANUAL.md, docs/
  (index.html, membrum_signal_flow.dot), CHANGELOG.md, version.json (0.12.5),
  plugins/membrum/CLAUDE.md, .claude/skills/membrum/SKILL.md, and the working
  papers in plugins/membrum/ (CRASH-REDESIGN-PLAN.md, PRESET-RETUNE-PLAN.md,
  PLAN-wire-coupling-2026-07-13.md, INVESTIGATION-snare-body-2026-07-01.md,
  docs/tom-depth-implementation-plan.md, preset-retune/).
- PRIOR AUDITS (HISTORY: never overwrite them): (1) plugins/membrum/docs/
  correctness-audit.md (2026-06: 23 confirmed findings "Finding 1..23"; the
  file says all are resolved); (2) plugins/membrum/
  AUDIT-signal-path-2026-06-07.md with AUDIT-signal-path-2026-06-07.
  findings.json (33 verified code findings, ids H-n / M-n / L-n, later N-1,
  and 18 literature verdicts; the .md carries a REMEDIATION STATUS tracker
  with open items); (3) the Ruinae audit changed Membrum's preset-load path
  (code comments R-202 / R-203 refer to specs/ruinae-audit/report.md). A prior
  finding that is still present in today's code is a valid finding (say which
  prior id it repeats); one that was fixed is not. THIS audit's ids are MB-nnn.
- OUT OF SCOPE: tools/membrum-fit (the offline sample fitter), except where it
  duplicates the plugin's parameter mapping.

REAL-TIME AUDIO RULES (violations are bugs): no allocation, lock, exception,
string/IO or unbounded loop on the audio thread (process(), everything the
voice pool, a voice, the coupling engine or the limiter calls per block,
setActive while processing); state written from another thread (the IMessage
handlers, setState) must reach the audio thread through an atomic or a transfer
object, never by writing engine members directly; parameters at the VST boundary
are normalized [0,1]; NaN/Inf must never reach the output; processor and
controller never cross-include headers; the macOS leg compiles with -ffast-math
(classify finiteness only via Krate::DSP::detail::isFinite / isNaN / isInf,
never std::isnan or a bit-mask clone; no numeric_limits infinity() /
quiet_NaN() constants in fast-math code; clamp pow / sqrt / log arguments at
domain edges; produce values that must be exact at a neutral point by a branch
or clamp, not by arithmetic that cancels).

DRUM-SYNTH-SPECIFIC HAZARDS: infinite ring (modes that never decay, modeInject,
Friction / Feedback sustain, coupling loop gain, voices that never retire = a
CPU leak) and the opposite (a tail cut early by the retirement logic, the
safety net or fast release); clicks at note-on, at a retrigger of the same pad
(flams, rolls), at a steal, a choke, a kit switch, and at an exciter / body
switch while ringing; TIMING: note events applied at the block start instead of
their sampleOffset, output that changes with the host block size, jitter in
fast rolls; velocity response flattened by normalisation, the limiter or the
clip rail, and velocity -> timbre mapping; mode frequencies that disagree with
their tables, modes at or above Nyquist (small Size, 44.1 kHz), pitch / decay /
level that depend on the sample rate; modal-bank instability (pole radius >= 1,
a damping-law sign or unit error, a retune while ringing); block-rate (chunk)
refresh that stair-steps short pitch envelopes; divergence between the three
voice renderers; LEVEL: body vs noise vs click balance, one hit vs 16
coincident hits vs the -1 dBTP limiter, the limiter working on ordinary playing
(homogenised dynamics), the aux buses bypassing limiter and master gain;
COUPLING: runaway or self-noise with no notes, the energy limiter, silence
flags; parameters that do nothing, too little, or only in a context the editor
does not show (dead and weak parameters), macros that fight direct edits;
factory kits whose pads sound alike; STATE: a state or preset from another
version rejected wholesale, a kit load while voices ring, proxy and per-pad
parameters that disagree after a load; CROSS-THREAD: IMessage-driven state
changes racing process(); denormals in long tails; MULTI-BUS: null or inactive
aux buffers, unexpected bus arrangements.

Report format discipline: every finding must cite a file and line(s) you
actually read (or a measurement you actually ran, or a source you actually
opened). If you cannot point at concrete code, a number or a citation, do not
report it.
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
          where: { type: 'string', description: 'file:line on each side, e.g. "pad_config.h:175 vs state_codec.cpp:212"' },
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
          kind: { type: 'string', description: 'build-warning | build-error | test-red | pluginval | tidy | lint | portability' },
          file: { type: 'string', description: 'repo-relative path or "-"' },
          line: { type: 'string', description: 'line or range or "-"' },
          text: { type: 'string', description: 'the message / failed assertion / measurement verbatim, trimmed' },
        },
      },
    },
    log_path: { type: 'string', description: 'absolute path of the captured log under f:/tmp/membrum-audit/' },
  },
}

const PROBE_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['ran', 'probe_source_path', 'tree_clean', 'summary', 'measurements', 'artifacts', 'log_path'],
  properties: {
    ran: { type: 'boolean' },
    probe_source_path: { type: 'string', description: 'where the probe TU was preserved (under specs/membrum-audit/probes/) or "-"' },
    tree_clean: { type: 'boolean', description: 'git status --porcelain plugins/membrum was empty after cleanup' },
    summary: { type: 'string' },
    measurements: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['name', 'setup', 'metric', 'value', 'threshold', 'verdict', 'note'],
        properties: {
          name: { type: 'string', description: 'short probe id, e.g. "matrix:exciter=Mallet:body=Plate" or "param:offset=23:modeInject"' },
          setup: { type: 'string', description: 'kit / pad / note, velocity, params, sample rate, block size' },
          metric: { type: 'string', description: 'what was measured (peak dBFS, decay time, mode-frequency error in cents, click count, NaN count, onset sample offset, band distance dB, ...)' },
          value: { type: 'string', description: 'the number(s) as printed' },
          threshold: { type: 'string', description: 'the bar you judged against and where it comes from (spec SC, mode table, common practice)' },
          verdict: { type: 'string', enum: ['ok', 'suspicious', 'bad', 'error'] },
          note: { type: 'string', description: 'which code the result implicates, if any (file:line), or why it is fine' },
        },
      },
    },
    artifacts: { type: 'array', items: { type: 'string' }, description: 'repo-relative paths of the result files copied to specs/membrum-audit/probes/ (CSV tables, results text)' },
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
          category: { type: 'string', description: 'kebab-case: rt-safety, threading, dsp-numerics, exciter, body-model, voice-path, layers-shaper, voice-management, midi-timing, gain-staging, coupling, param-plumbing, dead-parameter, kit-quality, lifecycle-state, ui-wiring, shared-dsp-contract, duplication, parallel-path, spec-conformance, portability, test-gap, anti-pattern, prior-audit-regression, measured-*' },
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

// One physical claim checked against the literature. A topic agent returns several.
const RESEARCH_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['claims'],
  properties: {
    claims: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['title', 'file', 'line', 'claim_as_coded', 'verdict', 'evidence_quality', 'correct_form', 'sources', 'implication', 'severity', 'suggested_fix', 'prior_verdict'],
        properties: {
          title: { type: 'string', description: 'one line naming the physical claim, e.g. "Membrane mode ratios are the Bessel zeros of an ideal circular membrane"' },
          file: { type: 'string', description: 'repo-relative path where the claim lives' },
          line: { type: 'string', description: 'line number or range' },
          claim_as_coded: { type: 'string', description: 'the actual formula / constants / topology found IN THE CODE (read it, do not guess), with the values' },
          verdict: { type: 'string', enum: ['confirmed', 'contradicted', 'oversimplified', 'unsupported', 'nuanced'] },
          evidence_quality: { type: 'string', enum: ['strong', 'moderate', 'weak'], description: 'strong only when backed by a textbook or a peer-reviewed paper' },
          correct_form: { type: 'string', description: 'what the literature says it SHOULD be (values / formula), or "matches" when confirmed' },
          sources: {
            type: 'array',
            items: {
              type: 'object',
              additionalProperties: false,
              required: ['title', 'type', 'url', 'says'],
              properties: {
                title: { type: 'string', description: 'authors, title, year' },
                type: { type: 'string', enum: ['textbook', 'paper', 'thesis', 'docs', 'blog', 'forum'] },
                url: { type: 'string', description: 'the URL you opened, or "-" for a print-only textbook' },
                says: { type: 'string', description: 'the specific thing this source establishes (equation / table / number)' },
              },
            },
          },
          implication: { type: 'string', description: 'the audible consequence if wrong or oversimplified, and whether it makes kits sound generic or alike' },
          severity: { type: 'string', enum: ['high', 'medium', 'low', 'none'], description: 'none when confirmed or when the deviation is inaudible' },
          suggested_fix: { type: 'string', description: 'the concrete change (values / formula / topology) and the test that would pin it, or "-"' },
          prior_verdict: { type: 'string', description: 'what the 2026-06 signal-path audit ruled on this claim and whether the code changed since, or "-"' },
        },
      },
    },
  },
}

const CLAIM_LIST_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['claims'],
  properties: {
    claims: {
      type: 'array',
      maxItems: 8,
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['key', 'title', 'files', 'question'],
        properties: {
          key: { type: 'string', description: 'kebab-case label' },
          title: { type: 'string' },
          files: { type: 'string', description: 'where the claim lives (file:line)' },
          question: { type: 'string', description: 'the physical question to verify against the literature' },
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
    reasoning: { type: 'string', description: 'cite the exact code (file:line) or the source you read to reach this verdict' },
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
          kind: { type: 'string', enum: ['uncovered-area', 'unverified-claim', 'suspicious-refutation', 'missing-measurement', 'lane-not-run', 'unresearched-physics'] },
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
// Run fn over items, `size` at a time (keeps agent slots free for the measurement lane).
async function inChunks(items, size, fn) {
  const out = []
  for (let i = 0; i < items.length; i += size) {
    out.push(...(await parallel(items.slice(i, i + size).map(it => () => fn(it)))))
  }
  return out
}
const LANE_STEPS = []
const LANE_KEYS = ['lints', 'build', 'gate', 'pluginval', 'probes', 'quality', 'tidy', 'portability']

// ---------------------------------------------------------------------------
// Measurement lane — ONE heavy job at a time, each capped. Started first and
// NOT awaited until the static findings are in, so it overlaps the reading
// phases. Every step is its own agent (fresh context, structured result).
// ---------------------------------------------------------------------------
async function laneStep(key, prompt, opts) {
  const r = await agent(`${CONTEXT}\n\nYou are ONE step of the Membrum measurement lane. Only this step runs on the machine right now, but readers are also using the box: stay inside the caps named below, never widen them. Capture every long command's output to a log file under ${LOG_DIR}/ on the FIRST run and read the log (never re-run a slow command to grep it). Create ${LOG_DIR} if missing. Do not edit repository files unless your step says so.\n\n${prompt}`, {
    label: `lane:${key}`,
    phase: 'Measure',
    ...opts,
  })
  LANE_STEPS.push({ key, result: r })
  return r
}

// Rules shared by the two probe-author steps.
const PROBE_RULES = `Every TEST_CASE carries the tags "[.][audit-probe]" (hidden: never part of the per-push gate) and PRINTS one line per measurement to stdout in the form
  PROBE <name> | <setup> | <metric> = <value> | threshold <bar> | <ok|suspicious|bad>
using WARN() or std::printf, and asserts nothing except that the output is finite (so a probe never fails the build gate; the numbers are the result). Split the work over many TEST_CASEs so the shards balance; keep the whole probe set under ~10 minutes wall clock when sharded. Heap-allocate large render buffers (a huge local array overflows the stack and Catch2 reports "Unknown exception"). Measure levels, frequencies and sample counts only: NO wall-clock or CPU measurements (the box is shared).

HARNESS: model it on plugins/membrum/tests/unit/processor/test_kit_preset_load_render.cpp and unit/preset/test_factory_kit_presets.cpp (reading a factory .vstpreset from MEMBRUM_RESOURCES_DIR and loading its component state into a real Processor), unit/processor/membrum_processor_tests.cpp and test_morph_host_simulation.cpp (ProcessData, event lists with sample offsets, parameter changes), unit/voice_pool/test_multi_bus_output.cpp and voice_pool_test_helpers.h (aux buses, direct VoicePool access), unit/test_exciter_body_matrix.cpp, unit/bodies/body_test_helpers.h and unit/exciters/exciter_test_helpers.h (direct DrumVoice / body / exciter access), unit/dsp/test_render_perceptual.cpp (band-energy analysis of a rendered pad). Use the shared helpers in tests/test_helpers/: spectral_analysis.h, artifact_detection.h (clicks / discontinuities), signal_metrics.h, audio_features.h (extractAudioFeatures, linToDbfs), test_signals.h, vst_event_list.h, vst_param_changes.h, parameter_sweep.h. Read their headers for exact signatures before writing code.

Hard rules: Write tool for new files (no heredocs), one build at a time, never run tools/clean-test-output.js, never run the whole test executable as a single serial process, never commit, never touch the prior-audit files (plugins/membrum/docs/correctness-audit.md, plugins/membrum/AUDIT-signal-path-2026-06-07.*). Everything you add under plugins/ is TEMPORARY and must be removed at the end.`

function probeProcedure(tu, preserved, resultsFile) {
  return `PROCEDURE:
 a. Write the TU plugins/membrum/tests/audit/${tu}; append it to the membrum_tests source list in plugins/membrum/tests/CMakeLists.txt.
 b. Build (cap TWO nodes; this is the only build on the box): ${CMAKE} --build ${BUILD_DIR} --config Release --target membrum_tests -- -m:2  > ${LOG_DIR}/${tu}.build.log 2>&1 ; read the log; fix YOUR compile errors/warnings (never touch plugin code).
 c. Run sharded, a quarter of the machine, capture:  node tools/run-gate.js --share 4 --filter '[audit-probe]' membrum_tests > ${LOG_DIR}/${tu}.run.log 2>&1 ; the PROBE lines are in the per-shard logs under f:/tmp/gate/ — collect them all into ${LOG_DIR}/${resultsFile}.
 d. Preserve: copy the TU to ${OUT_DIR}/probes/${preserved} (create the dir; the .txt suffix keeps repo-wide .cpp globs, the portability hook and clang-tidy away from it) and copy ${LOG_DIR}/${resultsFile} plus any CSV you wrote to ${OUT_DIR}/probes/, so the fix plan can promote the useful probes into real regression tests and the report can link the tables.
 e. CLEAN UP, mandatory: git checkout -- plugins/membrum/tests/CMakeLists.txt ; delete plugins/membrum/tests/audit/ ; then run  git status --porcelain plugins/membrum  and report tree_clean = (output empty). If anything else shows up there, you broke the rule — revert it.
 f. Return the measurements with a verdict each and, for anything suspicious/bad, read the implicated code and name file:line in note. List every file you copied to ${OUT_DIR}/probes/ in artifacts.`
}

async function runMeasurementLane() {
  const lane = {}

  lane.lints = await laneStep('lints', `STEP: repo lints scoped to Membrum (cheap node scripts, run them one after another, each output to its own log):
  node tools/lint-odr.js
  node tools/lint-layers.js
  node tools/lint-apple-globals.js
  node tools/lint-finite-bitmask.js
  node tools/lint-nonfinite-symbols.js
  node tools/lint-ieee-flag-sets.js
  node tools/lint-musical-time-truncation.js
  node tools/lint-unordered-serialization.js
  node tools/lint-allocation-probes.js
  node tools/lint-allocation-operator-overrides.js
  node tools/lint-float-bit-goldens.js
  node tools/lint-platform-type-literals.js
  node tools/lint-simd-aligned-loadstore.js
  node tools/lint-arch-guarded-includes.js
  node tools/lint-plugin-editor-baseline.js
  node tools/lint-wallclock-tags.js
  node tools/check-au-plist.js membrum
  node tools/uidesc-inventory.js plugins/membrum/resources/editor.uidesc --check
Read each script's header comment first if its arguments are unclear (some take a path or --self-test). Report ONLY items that touch plugins/membrum/, a plugins/shared/ file Membrum includes, or a dsp/ header Membrum includes (grep -rhoE '#include <krate/dsp/[^>]+>' plugins/membrum/src gives that list); a tree-wide ratchet baseline hit elsewhere is not a Membrum item, but a Membrum file that sits IN a ratchet baseline is (name the baseline file). kind=lint.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })

  if (!SKIP.build) {
    lane.build = await laneStep('build', `STEP: build the Membrum test executable and the plugin bundle in ONE invocation, capped at two MSBuild nodes:
  ${CMAKE} --build ${BUILD_DIR} --config Release --target membrum_tests Membrum -- -m:2
The bundle target is "Membrum" (the built bundle is ${BUILD_DIR}/VST3/Release/Membrum.vst3); if MSBuild says the target does not exist, grep -rn MEMBRUM_NAME CMakeLists.txt cmake plugins/membrum/CMakeLists.txt for the real name. If ${BUILD_DIR} does not exist, configure first: ${CMAKE} --preset windows-x64-release. A post-build copy failure into "C:/Program Files/Common Files/VST3/" is NOT an error (the compile succeeded). Report every compiler WARNING (the project requires zero) as kind=build-warning with file:line, and any error as build-error. Record the wall clock.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })

    lane.gate = await laneStep('gate', `STEP: run the per-push Membrum suite SHARDED, taking a quarter of the machine:
  node tools/run-gate.js --share 4 --scope all --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' membrum_tests   # gate-all-ok: the audit measures the whole Membrum suite
(Keep the "# gate-all-ok: ..." comment: without it tools/hooks/guard-test-scope.js blocks a whole-suite run; without the scope flag the gate runs only cases touched by uncommitted changes.)
Run it ONCE, capture to a log, then read the log. Report the GATE GREEN / GATE RED line verbatim in summary. For every failed case report kind=test-red with the test file:line and the failed assertion text (per-shard logs are under f:/tmp/gate/). Also note skipped/hidden/[!mayfail] cases you see in the log, the slowest five cases from shard-times.tsv, and any file the run left modified or created inside the repository (git status --porcelain afterwards: the tests/unit/diagnose cases render files; report where they land, do not delete tracked files). Timing tests are excluded on purpose; do NOT run tools/run-cpu-tests.js.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })

    lane.pluginval = await laneStep('pluginval', `STEP: validate the bundle:
  tools/pluginval.exe --strictness-level 5 --validate "${BUILD_DIR}/VST3/Release/Membrum.vst3"
Capture to a log. The EXIT CODE is the verdict (there is no SUCCESS line): 0 = pass; 0xC0000005 (-1073741819) = crash. Report every FAIL/error block as kind=pluginval with the test name and message; on a crash report the last test that started.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })
  } else {
    log('Measure: build/gate/pluginval skipped by args.skip.build')
  }

  if (!SKIP.probes && !SKIP.build) {
    lane.probes = await agent(`${CONTEXT}\n\nYou are the AUDIO PROBE author of the Membrum measurement lane (hard defects). You MAY edit files, but ONLY these: create plugins/membrum/tests/audit/audit_probes.cpp, append it to the membrum_tests source list in plugins/membrum/tests/CMakeLists.txt, and create files under ${OUT_DIR}/probes/.

GOAL: measure what code reading cannot — hit real pads through the real Processor (and, where simpler, directly through VoicePool / DrumVoice) and print numbers.

${PROBE_RULES}

MEASURE AT LEAST (one TEST_CASE per group or sub-group, loop inside; 48 kHz unless stated):
 1. Exciter x body matrix (7 x 6 = 42 combinations, other parameters at their defaults): one hit at velocity 1.0 and one at 0.25 -> peak dBFS, RMS, DC offset, NaN/Inf count, decay time (until the 10 ms RMS envelope stays 60 dB under its maximum; ">10 s" if it never does), spectral centroid, and the level difference between the two velocities in dB (under 3 dB = flattened dynamics).
 2. Mode accuracy per modal body (Membrane, Plate, Shell, Bell; Impulse exciter, noise and click mix 0, tone shaper neutral, unnatural zone off): the first 6-8 spectral peaks vs the coded tables (dsp/membrane_modes.h, bodies/plate_modes.h, shell_modes.h, bell_modes.h) as error in cents per partial; Size at 0, 0.5 and 1 -> measured fundamental vs bodies/natural_fundamental.h and the 500 Hz .. 50 Hz range documented in plugin_ids.h; the String body's fundamental error in cents at the same three sizes.
 3. Sample-rate consistency: a kick-like, a snare-like and a cymbal-like pad of the default kit at 44.1k, 48k, 96k, 192k -> fundamental (cents), decay time (%), RMS (dB), spectral centroid (%); flag more than 5 cents, 10 %, 1 dB between rates and any NaN; at 44.1k with Size 0 (smallest) report whether partials at or above Nyquist are dropped or alias (energy at non-table frequencies).
 4. Timing: one note-on at sampleOffset 0, 1, 100 and blockSize-1 -> index of the first non-zero output sample minus the offset (expect 0; the processor reports no latency); the SAME event stream (8 hits on four pads, deterministic exciters) rendered with block sizes 32, 64, 512, 2048 and 16384 (above kVoicePoolMaxBlock) -> max sample difference between renders and the onset index of every hit per block size.
 5. Voice lifecycle: the same pad retriggered 20 times at 10 ms spacing (roll) -> click count, largest sample-to-sample jump, peak growth; polyphony 4, 8 and 16 with polyphony+4 different pads hit in quick succession, once per stealing policy -> click count and jump at each steal; three steals of the same slot inside 5 ms; an open hat choked by a closed hat in the same choke group -> residual level 5 ms and 20 ms after the choke and the jump at the choke; maxPolyphony reduced 16 -> 4 while 12 voices ring; a pad disabled while it rings.
 6. Tail and idle: every exciter type on a Membrane and on a Plate body, no note-off, render 30 s -> time until output falls under -100 dBFS and whether the pool's active voice count returns to 0 (Friction, Feedback and modeInject > 0 are EXPECTED to sustain: report them as ok with the reason and whether a note-off, a choke and a kit switch stop them); the same hit followed by a note-off after 10 ms vs none -> decay time ratio (how much note length changes the drum).
 7. Level: single hard hit per body type -> peak dBFS at the voice output vs the -6 dBFS strike budget (kBodyHeadroom); all 16 voices hit in the same block at velocity 1.0 -> main-bus peak before/after the bus limiter and after master gain (master at 0, 0.5 and 1 normalized -> measured gain in dB vs -24 / -6 / +12); a kick + snare + hat hit together at velocity 100/127 -> how many dB the limiter reduces (more than 1 dB on ordinary playing = it shapes the dynamics); velocity 0.1 .. 1.0 in ten steps on kick, snare, hat -> level curve (monotonic? total range in dB); pan 0, 0.5, 1 -> L and R level in dB.
 8. Multi-bus: a pad routed to aux bus 3 with that bus active and then inactive -> energy on main and on the aux bus, the aux level relative to the main level for the same hit, whether master gain and the limiter touch the aux bus, silence flags of every bus; all 16 buses active with one pad on each; a null channel buffer on an active aux bus -> no crash.
 9. Coupling: global coupling 0 vs 1 with snare buzz and tom resonance at 1 -> added energy in dB after a kick; 30 s of 16th-note kicks at 120 BPM with coupling at maximum -> RMS in the last second vs the first (runaway?) and peak; coupling at maximum with no note ever played -> output RMS over 5 s and the main-bus silence flag; coupling delay stepped 0.5 -> 2.0 ms while ringing -> click count; coupling toggled 0 -> 1 -> 0 -> 1 between hits -> burst of stale energy on re-enable?
10. Tone shaper and pitch envelope: filter cutoff swept 20 Hz -> 20 kHz over a 2 s sustained (Friction) note at resonance 0, 0.5, 1 for LP / HP / BP -> peak dBFS (runaway), NaN count, click count, repeated at 192 kHz; drive and fold at 0.25 / 0.75 / 1.0 -> DC after the DC blocker, level change vs neutral, energy above 15 kHz from a 100 Hz kick (aliasing indicator); pitch envelope 160 Hz -> 50 Hz in 50 ms on every body type -> measured pitch at 0, 25 and 100 ms vs the target (cents) and the pitch staircase step size at block sizes 64 and 1024; the three-point envelope with the knee enabled.
11. Unnatural zone and morph: mode stretch 0.5 / 1 / 2 -> measured ratio of partial 4 to partial 1 vs the unstretched ratio; decay skew -1 / 0 / +1 -> decay time of a high partial vs a low partial; modeInject 0.5 and 1 -> level after 10 s and whether the amplitude stays bounded; nonlinear coupling 1.0 -> peak, NaN; every unnatural amount at exactly 0 -> bit-identical to the bypassed render; material morph enabled (start 1, end 0, 500 ms) on every body -> spectral centroid at 0, 250, 500 ms.
12. Parameter automation mid-note: on a ringing tom, step every continuous per-pad parameter 0 -> 1 (one at a time) -> click count and NaN count per parameter; switch the exciter type and the body model while the voice rings and on the next hit; load a different kit (setState) while 8 voices ring -> click count, NaN, and level 1 s later (infinite ring?).
13. State: for each of the 20 factory kits: setState, then getState -> setState -> getState -> the two blobs byte-identical? then render pad 36 before and after the round trip -> max sample difference. Load path A (setState) vs path B (the "LoadPresetState" IMessage with the same blob, sent through Processor::notify as the controller does) -> getState bytes identical? Feed setState a truncated stream (cut at 25 %, 50 %, 99 %), garbage bytes, a blob whose version field is 4 and one whose version is 6, and a valid blob with exciterType 200, maxPolyphony 99 and a NaN in a sound slot -> return code, whether the previous kit survives intact (getState compared with the state before the attempt), finite output.
14. Proxy and macro plumbing: for every selected-pad proxy ID, writing the proxy with pad P selected vs writing pad P's per-pad ID directly -> max sample difference between the two renders (expect 0); each macro at 0.5 -> bit-identical to the untouched pad; a macro moved to 1 then back to 0.5 -> the pad's underlying parameters restored (getState compare).

${probeProcedure('audit_probes.cpp', 'audit_probes.cpp.txt', 'probe-results.txt')}`, { label: 'lane:probes', phase: 'Measure', schema: PROBE_SCHEMA, agentType: 'general-purpose', model: 'opus' })
    LANE_STEPS.push({ key: 'probes', result: lane.probes })
  } else {
    log('Measure: audio probes skipped')
  }

  if (!SKIP.quality && !SKIP.build) {
    lane.quality = await agent(`${CONTEXT}\n\nYou are the KIT-QUALITY PROBE author of the Membrum measurement lane. You MAY edit files, but ONLY these: create plugins/membrum/tests/audit/audit_quality_probes.cpp, append it to the membrum_tests source list in plugins/membrum/tests/CMakeLists.txt, and create files under ${OUT_DIR}/probes/.

GOAL: put numbers on two questions the user has asked about Membrum before: (1) how different do the pads of each factory kit, and the kits themselves, really sound; (2) which parameters audibly change the sound and which do nothing or next to nothing. The numbers are the result; another agent localises causes.

${PROBE_RULES}

FEATURES per rendered hit (define them once in the TU and state the definitions in the results file): peak dBFS; RMS dBFS over the first 500 ms; DC offset; NaN count; decay time (until the 10 ms RMS envelope stays 60 dB under its maximum, ">4 s" if never); spectral centroid in Hz over the first 200 ms; a 24-band log-spaced band-energy vector in dB from 40 Hz to 16 kHz, normalised to its loudest band. bandDist(a, b) = RMS difference of the two band vectors in dB.

A. KIT TABLE. For each of the 20 factory kits (MEMBRUM_RESOURCES_DIR "/presets/Kit Presets/<Category>/<name>.vstpreset") loaded into a real Processor: for every ENABLED pad (notes 36..67), from silence, a note-on at velocity 100/127 with NO note-off, render 4 s -> all features, plus the pad's exciter type, body model and PadCategory; then the same hit with a note-off after 100 ms -> decay time only. Flags per pad: silent (enabled but peak under -60 dBFS), limiter proximity (peak dBFS minus (kit master gain in dB - 1) within 0.5 dB: one hit already rides the bus limiter), dc (|DC| above -60 dBFS), nan, long ring (still within 60 dB of its peak at 4 s: say whether modeInject > 0, Friction or Feedback explains it). Write EVERY row to ${LOG_DIR}/kit-pads.csv. PROBE lines: one summary per kit (enabled pads, flagged pads by flag, level spread in dB between its loudest and quietest pad) and one per flagged pad.

B. SAMENESS. Near-duplicate pair = bandDist under 3 dB AND centroids within 10 % AND decay times within 20 %. (1) Within each kit: every pair of enabled pads -> the count of near-duplicate pairs and how many of them share a PadCategory; all pairs to ${LOG_DIR}/kit-pairs.csv; PROBE lines: the count per kit plus its ten closest pairs. (2) Across kits: for notes 36, 38, 42, 45 and 49 compare the same note in all 20 kits -> per note the mean pairwise bandDist, the standard deviation of centroid (in octaves) and of decay time (as a ratio); per kit its nearest other kit by mean bandDist over those five notes; flag kit pairs under 3 dB. (3) Velocity response: for notes 36, 38, 42 of every kit, velocity 40/127 vs 127/127 -> level difference in dB and centroid ratio; flag under 6 dB together with under 5 % centroid change (the pad does not respond to how hard it is hit).

C. PARAMETER AUDIBILITY. For every per-pad parameter offset 0..65 (dsp/pad_config.h PadParamOffset), the four coupling globals, master gain and the five macros: pick the CONTEXT in which the parameter is supposed to matter and write it in the setup (read the comments in pad_config.h, dsp/pad_config_apply.h and dsp/drum_voice.h: the FM ratio needs the FMImpulse exciter, friction pressure needs Friction, morph start / end / duration / curve need morph enabled, secondary size / material and coupling strength need the secondary bank enabled, the pitch-envelope mid point and second curve need the knee enabled and a non-zero envelope time, the filter-envelope parameters need a non-zero envelope amount and a cutoff below maximum, tension modulation needs the Membrane body, the noise-layer parameters need a noise mix above 0, the damping overrides leave their -1 sentinel, per-pad coupling amount needs global coupling above 0 and a second pad to excite). In that context render one hit with the parameter at normalized 0, 0.25, 0.5, 0.75 and 1 (discrete parameters: every step), everything else fixed, on three base pads of the default kit: a kick (note 36), a snare (38) and a hat or cymbal (42 or 49). Across the renders report: max level change (dB), max centroid change (%), max decay-time change (%), max bandDist (dB), and whether the output is bit-identical over the whole range. Verdict: bad = bit-identical on all three base pads in its own context (a dead parameter); suspicious = every metric under 1 dB / 5 % / 5 % / 1 dB on all three (inaudibly weak), or a jump above 12 dB between two adjacent steps; ok otherwise. Also record per parameter whether changing it while a long tom rings (no new note) alters the ringing voice (applied live vs only at the next note-on): that column is information for the report, not a verdict. Write every row to ${LOG_DIR}/param-audibility.csv. PROBE lines: one per suspicious or bad parameter and one summary line with the counts of ok / suspicious / bad.

RETURN as measurements: the per-kit summaries, the flagged pads, the sameness lines of B, and the suspicious / bad parameter lines plus the summary (not all ~640 pad rows: those live in the CSV files, which you list in artifacts).

${probeProcedure('audit_quality_probes.cpp', 'audit_quality_probes.cpp.txt', 'quality-results.txt')}
 g. Besides quality-results.txt, copy kit-pads.csv, kit-pairs.csv and param-audibility.csv from ${LOG_DIR}/ to ${OUT_DIR}/probes/.`, { label: 'lane:quality', phase: 'Measure', schema: PROBE_SCHEMA, agentType: 'general-purpose', model: 'opus' })
    LANE_STEPS.push({ key: 'quality', result: lane.quality })
  } else {
    log('Measure: kit-quality probes skipped')
  }

  if (!SKIP.tidy) {
    lane.tidy = await laneStep('tidy', `STEP: clang-tidy for the Membrum area, four jobs, Windows PowerShell script only (never the .sh on Windows):
  pwsh -NoProfile -File ./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja -Jobs 4 -Full > ${LOG_DIR}/tidy.log 2>&1   # tidy-full-ok: the audit measures every Membrum TU
(Keep the "# tidy-full-ok: ..." comment: without it tools/hooks/guard-tidy-scope.js blocks -Full; without -Full the script analyzes only uncommitted changes.)
If build/windows-ninja/compile_commands.json is missing, generate it first with the ninja preset (${CMAKE} --list-presets shows the name; see .claude/skills/clang-tidy-setup/SKILL.md). If a warning looks like a phantom (header not found, unknown flag), reconfigure that preset once and re-run only the one TU. Set a wall-clock ceiling: if the script has produced no new output for 20 minutes, find the stuck clang-tidy process (Get-CimInstance Win32_Process | where CommandLine -match clang-tidy), record the TU, kill it, and report that TU as kind=tidy "hung". Report warnings CLUSTERED by check name + file (one item per cluster, count in text, one example line), kind=tidy. The total count goes in summary.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })
  } else {
    log('Measure: clang-tidy skipped by args.skip.tidy')
  }

  if (!SKIP.portability) {
    lane.portability = await laneStep('portability', `STEP: the Linux/macOS compile proxy. tools/check-portability.js syntax-checks TUs with GCC in WSL; with no arguments it only takes files changed vs origin/main (nothing, on a clean tree), so pass EXPLICIT files: every .cpp under plugins/membrum/src and plugins/membrum/tests (build the list with git ls-files and pass it on one command line), output captured to ${LOG_DIR}/portability.log. Read its header comment first for any extra flags. It boots WSL; leave WSL running afterwards (never shut it down or restart it: the user runs WSL in other sessions). Report every error/warning it prints for plugins/membrum/ or an included dsp/ or plugins/shared/ header as kind=portability with file:line. Note in summary what the script does NOT cover (it is a compile proxy, not fast-math semantics).`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })
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
// Research — physical-model claims vs the literature. Web-bound, so it runs
// beside everything else, four agents at a time, and is awaited before the
// critic. Each topic agent reads the code for the ACTUAL numbers first.
// ---------------------------------------------------------------------------
const RESEARCH_TOPICS = [
  {
    key: 'membrane-modes',
    title: 'Circular membrane: mode ratios, strike-position weighting, air loading',
    files: 'plugins/membrum/src/dsp/membrane_modes.h, plugins/membrum/src/dsp/bodies/membrane_mapper.h (incl. the air-loading curve and mode scatter)',
    question: 'Do the coded mode-frequency ratios equal the Bessel-zero ratios of an ideal circular membrane (1.000, 1.594, 2.136, 2.296, 2.653, 2.918, ...) with the right (m,n) labels? Are the mode amplitudes weighted by the mode shape J_m(k_mn r) at the strike radius (centre strikes excite only m = 0 modes)? Is the air-loading correction (the tabulated depression of the low modes) consistent with the measured timpani / drumhead ratios in Rossing and in Fletcher & Rossing, and is it applied to the right modes?',
  },
  {
    key: 'plate-modes',
    title: 'Plate / cymbal mode ratios',
    files: 'plugins/membrum/src/dsp/bodies/plate_modes.h, plate_mapper.h (and tools/gen-plate-chladni.js if the table is generated)',
    question: 'Which plate model do the coded ratios implement (simply supported rectangular (m^2 + n^2), free-edge circular, Kirchhoff)? Is that model appropriate for the instruments the plugin uses it for (cymbals, gongs, metal plates), whose spectra are dense and inharmonic with free edges? What do Fletcher & Rossing and Bilbao give for free circular plates, and how far off are the coded ratios and the strike-position weighting?',
  },
  {
    key: 'shell-bell-modes',
    title: 'Shell (free-free beam) and bell partial series',
    files: 'plugins/membrum/src/dsp/bodies/shell_modes.h, shell_mapper.h, bell_modes.h, bell_mapper.h, and configureSecondaryBank in plugins/membrum/src/dsp/drum_voice.h (~1418)',
    question: 'Are the shell ratios the free-free Euler-Bernoulli beam eigenfrequency ratios (1, 2.757, 5.404, 8.933, ...), and is a beam the right model for a drum shell (cylindrical shell modes)? Do the bell ratios match a published bell partial series (hum 0.5, prime 1, tierce 1.2, quint 1.5, nominal 2, ...; Rossing, Lehr)? Is the secondary-shell tuning rule (shell fundamental as a fraction of the head fundamental) supported by measurements of real drums?',
  },
  {
    key: 'string-body',
    title: 'Waveguide string body',
    files: 'plugins/membrum/src/dsp/bodies/string_body.h, string_mapper.h, dsp/include/krate/dsp/processors/waveguide_string.h',
    question: 'Is the waveguide string a correct digital-waveguide / extended Karplus-Strong implementation (loss filter, stiffness dispersion allpass, fractional-delay tuning compensation, pick / strike position comb; Smith, Jaffe & Smith, Valimaki)? Is the tuning error introduced by the loop filters compensated? Are the brightness / decay mappings physically sensible for struck strings?',
  },
  {
    key: 'noise-body',
    title: 'NoiseBody (hi-hat / cymbal model)',
    files: 'plugins/membrum/src/dsp/bodies/noise_body.h, noise_body_mapper.h',
    question: 'How does the hybrid modal + noise body compare with published cymbal / hi-hat synthesis (dense inharmonic partial clusters, the six-square-oscillator 808 / 909 metal core, band-passed noise, nonlinear plate models by Bilbao, Chaigne, Touze)? Are the coded partial frequencies, noise band and decay laws in a realistic range for hats and cymbals?',
  },
  {
    key: 'damping-law',
    title: 'Per-mode damping law R = b1 + b3 f^2',
    files: 'dsp/include/krate/dsp/processors/modal_resonator_bank.h (DampingLaw and how a decay rate becomes a pole radius), the b1 / b3 derivations in plugins/membrum/src/dsp/bodies/*_mapper.h, the override ranges documented in plugins/membrum/src/dsp/pad_config.h',
    question: 'Is frequency-dependent damping of the form b1 + b3 f^2 the form used by Chaigne & Lambourg (plates), Chaigne & Askenfelt (strings) and Bilbao? Are the units consistent between the law, the mappers and the documented ranges (Hz vs rad/s, decay rate vs T60)? Do the coded b1 / b3 ranges produce T60 values that are plausible for drum heads, wood, metal and plastic (Aramaki et al. on material perception)?',
  },
  {
    key: 'strike-normalisation',
    title: 'Measured-strike level normalisation',
    files: 'plugins/membrum/src/dsp/drum_voice.h (kBodyHeadroom, measuredStrikeOutputGain, StrikeNormKey ~1717, the note-on code that applies it), dsp/include/krate/dsp/processors/modal_resonator_bank.h (getInputGainSum, output gain)',
    question: 'Is normalising every strike to a fixed peak physically and perceptually defensible? In real drums, level depends on size, material, strike position and strike force; which of those differences does the normalisation remove? What do reference modal implementations do at the output of a modal bank (STK Modal, Faust physmodels.lib, Modalys, Cook; Smith / CCRMA)? Could this stage make differently-configured pads sound alike in level?',
  },
  {
    key: 'tension-modulation',
    title: 'Nonlinear tension modulation / pitch glide',
    files: 'plugins/membrum/src/dsp/drum_voice.h (kTensionEnergyScale, kTensionPitchModMax, the energy follower and tensionPitchMod in processBlockFast ~703-760 and ~902-920)',
    question: 'Does the energy-driven pitch-up-then-relax model match Avanzini, Marogna & Bank, "Efficient synthesis of tension modulation in strings and membranes based on energy estimation" (JASA 2012)? Are the glide depth (about 1.5 semitones at full, capped near 3) and the 20 ms follower consistent with measured tom-tom and kick pitch glides (Dahl; Kirby & Sandler)? Is restricting it to the Membrane body justified?',
  },
  {
    key: 'strike-position',
    title: 'Strike-position spectral weighting on every body',
    files: 'the strike-position handling in plugins/membrum/src/dsp/bodies/*_mapper.h (membrane, plate, shell, bell, string, noise body)',
    question: 'Does each body weight its mode amplitudes by the mode shape at the strike point (sin(n pi x) for a 1-D structure, J_m for the membrane, the product of sines for a rectangular plate), so that a mode with a node at the strike point is suppressed? Is the mapping from the 0..1 knob to a physical position sensible, and is the centre-vs-edge timbre change reproduced on every body or only on some?',
  },
  {
    key: 'exciter-physics',
    title: 'Exciter models (mallet, impulse, friction, FM, feedback, clap, noise burst)',
    files: 'plugins/membrum/src/dsp/exciters/*.h, plugins/membrum/src/dsp/exciter_bank.h, dsp/include/krate/dsp/processors/impact_exciter.h, bow_exciter.h, fm_operator.h',
    question: 'Are the contact models physically grounded: a nonlinear (Hertzian / power-law) mallet force with contact time shortening as velocity and hardness rise (Chaigne & Doutaut 1997; Avanzini & Rocchesso), so harder hits are brighter; stick-slip friction (McIntyre, Schumacher & Woodhouse; Serafin) for the friction exciter? Is the velocity -> force -> spectrum mapping realistic? Does the clap model follow the multi-burst structure of real and 808 / 909 claps? Which exciters are admitted synthesis shortcuts with no physical claim?',
  },
  {
    key: 'click-noise-layers',
    title: 'Always-on click transient and parallel noise layer',
    files: 'plugins/membrum/src/dsp/click_layer.h, plugins/membrum/src/dsp/noise_layer.h, their mix in plugins/membrum/src/dsp/drum_voice.h',
    question: 'Is a 2-5 ms raised-cosine filtered-noise burst a realistic stand-in for stick / beater contact (published contact durations on drum heads and their dependence on the beater), and is routing half of it into the body excitation justified? Is a parallel filtered-noise layer with its own decay consistent with residual / stochastic modelling of percussion (Serra & Smith SMS, Cook PhISEM)? Are the calibrated levels (about -18 dBFS accents) in a realistic body-to-noise ratio for kicks, snares, hats?',
  },
  {
    key: 'coupling-models',
    title: 'Head-shell coupling, snare-wire coupling, cross-pad sympathetic resonance',
    files: 'plugins/membrum/src/dsp/drum_voice.h (the secondary bank mix ~802-846, computeWireGainMod and the wire-coupling comments), plugins/membrum/src/dsp/coupling_matrix.h, dsp/include/krate/dsp/systems/sympathetic_resonance.h, the coupling chain in plugins/membrum/src/processor/processor.cpp (~688-708)',
    question: 'Is a feedforward-only head -> shell path an acceptable simplification of the bidirectional head / air cavity / shell coupling of a real drum (Rossing; Bilbao; Torin & Newton), and what audible character does it lose? Does scaling the snare noise by the head modal-energy envelope capture the snare-wire mechanism described by Bilbao, "Time domain simulation and sound synthesis for the snare drum" (JASA 2012) (collisions, lift-off, the buzz dying with the head)? Is the cross-pad sympathetic-resonance model (a mono sum driving tuned resonators after a short delay) consistent with measured sympathetic snare buzz and tom ring in a real kit, including its delay range of 0.5-2 ms?',
  },
  {
    key: 'nonlinear-coupling',
    title: 'Unnatural-zone nonlinear coupling and mode injection',
    files: 'plugins/membrum/src/dsp/unnatural/nonlinear_coupling.h, mode_inject.h, the mode-stretch and decay-skew code (grep modeStretch / decaySkew under plugins/membrum/src/dsp)',
    question: 'The zone is non-physical by design; check only the physical phenomena its comments cite. If nonlinear coupling claims to imitate the energy cascade to high modes in cymbals and gongs (von Karman plate nonlinearity; Touze, Chaigne, Ducceschi), does the coded mechanism produce the cited behaviour (amplitude-dependent brightening, pitch glide in gongs)? Is mode stretch comparable to stiffness inharmonicity (f_n = n f0 sqrt(1 + B n^2))? Report claims the comments make that the code does not deliver.',
  },
  {
    key: 'pitch-envelope-808',
    title: 'Pitch envelope vs analogue drum machines',
    files: 'plugins/membrum/src/dsp/pitch_segment_envelope.h, the pitch-envelope part of plugins/membrum/src/dsp/tone_shaper.h, plugins/membrum/tests/unit/tone_shaper/test_pitch_envelope_808.cpp',
    question: 'How do the default and factory pitch sweeps (start / end frequency, time, curve) compare with the measured or circuit-derived pitch behaviour of the TR-808 and TR-909 kick (bridged-T resonator with a short pitch sweep; Werner, Abel & Smith, "A physically-informed, circuit-bendable, digital model of the Roland TR-808 bass drum circuit", DAFx 2014)? Is an exponential sweep the right shape, and is a block-rate (once per chunk) pitch update fine enough for sweeps of 5-50 ms?',
  },
  {
    key: 'bus-limiter',
    title: 'Zero-latency true-peak limiter on the main bus',
    files: 'dsp/include/krate/dsp/processors/true_peak_limiter.h and its use in plugins/membrum/src/processor/processor.cpp (~710-717, setupProcessing)',
    question: 'Can a limiter with no look-ahead hold a -1 dBTP ceiling on drum transients without audible distortion? What does ITU-R BS.1770 require for a true-peak measurement (4x oversampled peak), and does the implementation meet it? What do Giannoulis, Massberg & Reiss ("Digital dynamic range compressor design", JAES 2012) and the limiter literature say about attack time vs transient distortion for a brickwall limiter? What does this stage do to the first milliseconds of a kick or snare?',
  },
]

async function runResearch() {
  const priorNote = `PRIOR WORK: plugins/membrum/AUDIT-signal-path-2026-06-07.findings.json holds 18 literature verdicts in its "research" array and section 3 of AUDIT-signal-path-2026-06-07.md discusses them. Read the entries for your topic FIRST, then check whether the code changed since (the gain staging, the body normalisation and the tension model were reworked after that audit) and report in prior_verdict what was ruled then and whether it still applies.`
  const method = `METHOD: (1) Read the code and state precisely what it does, with the actual numbers: never guess a coefficient. (2) Use web search + fetch to find authoritative sources and OPEN them. WEIGHT SOURCE QUALITY: textbooks (Fletcher & Rossing, "The Physics of Musical Instruments"; Bilbao, "Numerical Sound Synthesis"; Chaigne & Kergomard, "Acoustics of Musical Instruments"; Smith's CCRMA online books) and peer-reviewed papers (JASA, JAES, DAFx, Acta Acustica) are ground truth; Faust / STK / vendor documentation is corroboration; blogs and forums can never be the sole basis for a contradicted verdict. (3) Return ONE claim per distinct physical statement (a topic usually yields 2-5): verdict confirmed / contradicted / oversimplified / unsupported (the code cites a source that does not say this, or no source exists) / nuanced (defensible, the literature differs in detail), with the correct form and the sources. (4) State the audible implication, a severity (none when confirmed or inaudible), and the concrete fix with the test that would pin the coded values to the published ones. A deliberate, documented simplification is still reported: say that it is deliberate and where that is written.`
  const curated = await inChunks(RESEARCH_TOPICS, 4, t =>
    agent(`${CONTEXT}\n\n=== PHYSICAL-MODEL REVIEW AGAINST THE LITERATURE ===\nTopic: ${t.title}\nFiles to read FIRST: ${t.files}\n\nQuestions: ${t.question}\n\n${priorNote}\n\n${method}`, {
      label: `research:${t.key}`,
      phase: 'Research',
      schema: RESEARCH_SCHEMA,
      model: 'opus',
    }).then(r => (r ? r.claims.map(c => ({ ...c, topic: t.key })) : null)),
  )
  const failed = RESEARCH_TOPICS.filter((_, i) => !curated[i]).map(t => t.key)
  if (failed.length) log(`WARNING: research topics failed: ${failed.join(', ')} — those physical claims are UNCHECKED`)

  const gaps = await agent(`${CONTEXT}\n\nThese physical topics are already being checked against the literature: ${RESEARCH_TOPICS.map(t => t.title).join('; ')}.\n\nScan the modelling code (plugins/membrum/src/dsp/bodies/*, exciters/*, unnatural/*, drum_voice.h, tone_shaper.h, noise_layer.h, click_layer.h, coupling_matrix.h, pad_category.h, and dsp/include/krate/dsp/processors/modal_resonator_bank.h, systems/sympathetic_resonance.h) and the manual (plugins/membrum/MANUAL.md) for ANY OTHER physical or acoustic claim, constant, formula or cited paper — in a comment, a constant name or the manual — that the list above does not cover and that is worth verifying. Return up to 8 of the highest-value ones, each with file:line and the question to answer.`, {
    label: 'research:gaps',
    phase: 'Research',
    schema: CLAIM_LIST_SCHEMA,
    model: 'sonnet',
  })
  const extraTopics = ((gaps && gaps.claims) || []).slice(0, 8)
  const extra = await inChunks(extraTopics, 4, c =>
    agent(`${CONTEXT}\n\n=== PHYSICAL-MODEL REVIEW AGAINST THE LITERATURE (gap fill) ===\nClaim: ${c.title}\nWhere: ${c.files}\nQuestion: ${c.question}\n\n${priorNote}\n\n${method}`, {
      label: `research:extra:${c.key}`,
      phase: 'Research',
      schema: RESEARCH_SCHEMA,
      model: 'opus',
    }).then(r => (r ? r.claims.map(x => ({ ...x, topic: `extra:${c.key}` })) : null)),
  )
  return { claims: [...curated, ...extra].filter(Boolean).flat(), failed, extra_topics: extraTopics.map(c => c.key) }
}

const researchPromise = SKIP.research
  ? Promise.resolve({ claims: [], failed: [], extra_topics: [], skipped: true })
  : runResearch().catch(e => ({ claims: [], failed: RESEARCH_TOPICS.map(t => t.key), extra_topics: [], error: String(e) }))
if (SKIP.research) log('Research: skipped by args.skip.research')
else log(`Research started: ${RESEARCH_TOPICS.length} physical-model topics, four at a time, beside the other phases`)

// ---------------------------------------------------------------------------
// Inventory (Haiku) + Understand (Sonnet) — concurrent, barrier before Find
// because every finder receives both digests.
// ---------------------------------------------------------------------------
const INVENTORIES = [
  {
    key: 'params',
    prompt: `Cross-reference the PARAMETER plumbing. Membrum has no parameter-pack directory: build one table row per PadParamOffset (plugins/membrum/src/dsp/pad_config.h, offsets 0..65) and one per global ID in plugins/membrum/src/plugin_ids.h. Sides: (1) plugin_ids.h — every global ID and, for each selected-pad PROXY, the per-pad offset it forwards to; (2) pad_config.h — the PadParamOffset entry, the PadConfig field and its member initializer; (3) Controller::initialize in plugins/membrum/src/controller/controller.cpp — the registration of the global / proxy parameter AND of the per-pad parameters (type, default, step count, units, flags, title); (4) Processor::processParameterChanges (plugins/membrum/src/processor/processor.cpp ~145-535) — which IDs are consumed and how proxies are forwarded — and VoicePool::setPadConfigField / setPadConfigSelector (plugins/membrum/src/voice_pool/voice_pool.cpp ~605-720): the switch branch per offset; (5) plugins/membrum/src/dsp/pad_config_apply.h applyPadConfigToVoice and VoicePool::applyPadConfigToSlot: is every PadConfig field pushed into the voice, and with which denormalization (range, law); (6) plugins/membrum/src/controller/controller_param_format.cpp: the displayed range / units per ID; (7) the state codec — the sound-slot index used by toPadSnapshot / applyPadSnapshot / toPadPresetSnapshot / applyPadPresetSnapshot in plugins/membrum/src/state/state_codec.cpp, by plugins/membrum/src/controller/controller_state_codec.cpp, and by tools/membrum_preset_generator.cpp; (8) plugins/membrum/src/processor/macro_mapper.cpp — which offsets each of the five macros drives — and the table built by Processor::buildRegisteredDefaultsTable vs the registered defaults of side 3; (9) plugins/membrum/src/dsp/default_kit.h — the fields its templates set; (10) resources/editor.uidesc control-tags — run  node tools/uidesc-inventory.js plugins/membrum/resources/editor.uidesc --out ${LOG_DIR}/uidesc-inventory.json  (create ${LOG_DIR} first) and read the JSON. Report mismatches: a PadConfig field with no parameter offset or no codec slot; an offset with no switch branch, not applied to the voice, not saved, saved but not loaded, or loaded into a different slot; a proxy with no per-pad target, or a per-pad offset with no proxy and no control (unreachable from the editor); a default that differs between the PadConfig initializer, the registration, the registered-defaults table, default_kit.h and the generator; a displayed range that differs from the voice's denormalization; a registered automatable parameter with no control in either pad template (SelectedPadSimple, SelectedPadAdvanced); a uidesc control-tag that is not a known ID; a range stated in a plugin_ids.h comment that differs from the code.`,
  },
  {
    key: 'types',
    prompt: `Cross-reference the ENUM <-> STRING <-> SWITCH tables. Enums and discrete sets: ExciterType (plugins/membrum/src/dsp/exciter_type.h, 7 members incl. Clap), BodyModelType (dsp/body_model_type.h, 6), the tone-shaper filter type (LP / HP / BP), the pitch-envelope and morph curve selectors, VoiceStealingPolicy (voice_pool/voice_stealing_policy.h), PadCategory (dsp/pad_category.h), VoiceSlotState (voice_pool/voice_meta.h), choke groups (0..8), output buses (kMaxOutputBuses = 16: Main, Aux 1..15), UI mode (ui/ui_mode.h), and the kit / pad preset subcategory lists (preset/membrum_preset_config.h). For each: the StringListParameter entries and step count in Controller::initialize (the global proxy AND the per-pad registration), the normalized <-> index conversion on every side (processor.cpp, voice_pool.cpp setPadConfigSelector, controller_state_codec.cpp, state_codec.cpp, tools/membrum_preset_generator.cpp: same rounding everywhere?), the load clamps, every switch / std::visit / variant dispatch over the enum in dsp/exciter_bank.h, dsp/body_bank.h, dsp/drum_voice.h, voice_pool.cpp and processor.cpp (missing case, fallthrough, a default that hides a new member), and the names in plugins/membrum/MANUAL.md, docs/index.html, docs/membrum_signal_flow.dot and in code comments (plugin_ids.h still says "6 choices" for the exciter). Report: enum members without a branch; list lengths that disagree with the enum count; names or orders that differ between enum, UI menu, manual and docs; a load clamp narrower or wider than the enum; two sides that round a normalized value to different indices.`,
  },
  {
    key: 'presets-versions',
    prompt: `Cross-reference PRESETS, VERSIONS and DOCS. (1) List the factory kits per category under "plugins/membrum/resources/presets/Kit Presets/" (20 files, 5 per category today) and compare with kitPresetConfig / padPresetConfig in plugins/membrum/src/preset/membrum_preset_config.h (and its comments, which name other categories), the kit list in plugins/membrum/MANUAL.md ("Factory Kits"), the kit definitions in tools/membrum_preset_generator.cpp (names, categories, count) and plugins/membrum/src/dsp/default_kit.h; are there factory PAD presets anywhere, and does the install rule in plugins/membrum/CMakeLists.txt (krate_plugin_install_presets) cover what the runtime scans? (2) Codec constants: kCurrentStateVersion (plugin_ids.h), kBlobVersion / kPadBlobVersion and the sound-slot count (state/state_codec.h), the same constants re-declared in tools/membrum_preset_generator.cpp, the layout comments in state_codec.h, processor.cpp (~855-870) and the generator header, the byte sizes they state, and the golden files plugins/membrum/tests/golden/*.bin with the tests that read them (which version do they hold?). (3) version.json vs src/version.h vs the top CHANGELOG.md entry vs docs/index.html vs installers/ metadata vs resources/au-info.plist.in and resources/auv3 vs src/update/membrum_update_config.h. (4) MANUAL.md, docs/index.html and CHANGELOG claims that name a number or feature (pads, voices, exciter and body types, aux buses, choke groups, macro names, parameter ranges, master gain range, coupling delay range) vs the constants in code. (5) plugins/membrum/CLAUDE.md and .claude/skills/membrum/SKILL.md statements vs the filesystem (the src skeleton, "no update/ dir", the signal path, "current active-development plugin"). Report every disagreement with both values verbatim.`,
  },
]

const SUBSYSTEMS = [
  {
    key: 'voice',
    prompt: `Map the VOICE: plugins/membrum/src/dsp/drum_voice.h read END TO END (by offsets; it is ~1780 lines), plus dsp/pad_config_apply.h and dsp/voice_common_params.h. Cover: prepare / noteOn (~170-440: everything it recomputes per hit) / noteOff / resetForKitSwitch; the THREE renderers process() (~497), processBlockFast (~682) and processBlockSlow (~933) stage by stage and where they differ; the chunking by kMaxBlockSize and what is refreshed once per chunk (pitch envelope, tension modulation, material morph, the energy follower, wire coupling); the strike normalisation (kBodyHeadroom, measuredStrikeOutputGain, the StrikeNormKey cache ~1717); the secondary shell bank (configureSecondaryBank ~1418, effectiveCoupling_); updateModalParameters / retuneFundamental (~1573-1700); the amp envelope's role, level_ and the hardClip rail; the retirement logic and the 256-silent-block safety net; every member that carries state from one hit to the next.`,
  },
  {
    key: 'exciters',
    prompt: `Map the EXCITERS: plugins/membrum/src/dsp/exciter_bank.h, exciter_type.h, every file under plugins/membrum/src/dsp/exciters/ (impulse, mallet, noise_burst, friction, fm_impulse, feedback, clap), plugins/membrum/src/dsp/click_layer.h, and the shared pieces they wrap: dsp/include/krate/dsp/processors/impact_exciter.h, bow_exciter.h, fm_operator.h, primitives/noise_oscillator.h, core/xorshift32.h. For EACH exciter: what it outputs, how velocity changes amplitude and spectrum, its per-pad parameter (FM ratio, feedback amount, burst duration, friction pressure), its duration and whether it ends by itself, its use of body feedback, its noise seed (per voice? per hit? fixed?), its trigger / reset rules. For the bank: the variant dispatch, the deferred type swap and when it takes effect. For the click layer: contact-time and brightness mapping, kStandaloneOutputGain, mix 0.`,
  },
  {
    key: 'bodies',
    prompt: `Map the BODIES: plugins/membrum/src/dsp/body_bank.h, body_model_type.h, membrane_modes.h, every file under plugins/membrum/src/dsp/bodies/ (the six *_body.h, the six *_mapper.h, plate_modes.h, shell_modes.h, bell_modes.h, natural_fundamental.h). For EACH body: its mode table (count, ratios, where the numbers come from), how Material, Size, Decay and Strike Position become frequencies, amplitudes and damping (b1, b3; the -1 override sentinel), which optional features it implements (air loading, mode scatter, mode stretch, decay skew, damping overrides, strike-position weighting) and which it silently ignores, what happens to modes at or above Nyquist, and its level before normalisation. For the bank: how the shared ModalResonatorBank is reused across body types, the String body's separate waveguide, getSharedBank / getLastOutput, processBlock argument order, a body switch while ringing.`,
  },
  {
    key: 'layers-shaper',
    prompt: `Map the LAYERS and the SHAPER: plugins/membrum/src/dsp/noise_layer.h, tone_shaper.h, pitch_segment_envelope.h, every file under plugins/membrum/src/dsp/unnatural/ (material_morph, mode_inject, nonlinear_coupling, unnatural_zone), and where mode stretch and decay skew are applied (grep modeStretch / decaySkew under plugins/membrum/src/dsp). Cover: the noise layer's filter, envelope, colour, standaloneGain, per-pad noiseLayerGain and the wire-coupling gain (computeWireGainMod in drum_voice.h); the tone shaper's order (drive -> wavefolder -> DC blocker -> SVF), its bypass conditions, cutoff / resonance / envelope-amount mapping, the filter ADSR, gain compensation of drive and fold; the pitch envelope (three points, knee, mid fraction, the two curves, time 0 = off, the 20..2000 Hz mapping) and who owns the body's fundamental while it runs; mode inject (what it adds, why it rings on, what stops it), nonlinear coupling (the nonlinearity, its gain, amount 0), material morph (enable, start / end, duration, curve, what it re-runs). Note which shared primitives each uses (svf.h, wavefolder.h, waveshaper.h, tanh_adaa.h, dc_blocker.h, adsr_envelope.h, envelope_follower.h) and with which settings.`,
  },
  {
    key: 'voice-pool',
    prompt: `Map the VOICE POOL: plugins/membrum/src/voice_pool/voice_pool.h and voice_pool.cpp read END TO END (~1070 lines), voice_meta.h, voice_stealing_policy.h, choke_group_table.h, and the shared dsp/include/krate/dsp/systems/voice_allocator.h (public API and its contract). Cover: noteOn (~128-265: pad lookup, enable check, choke processing, allocation, steal, applyPadConfigToSlot, coupling-engine notification) and noteOff (~283); the two processBlock overloads (~306 and ~443: per-voice render into scratch, fast-release ramp, pan, main and aux mixing, bus-active handling, block sizes above kVoicePoolMaxBlock); the main / shadow slot scheme and the VoiceSlotState machine; beginFastRelease / applyFastRelease (the 5 ms exponential, the 1e-6 floor, the re-entry rule); the three stealing policies; choke groups; setMaxPolyphony while voices sound; setPadConfigField / setPadConfigSelector (~605-720: which fields reach a sounding voice immediately and which wait for the next note-on); resetAllVoicesForKitSwitch; getActiveVoiceCount / isAnyVoiceActive; currentLevel bookkeeping.`,
  },
  {
    key: 'processor-master',
    prompt: `Map the PROCESSOR and MASTER section: plugins/membrum/src/processor/processor.h and processor.cpp read END TO END (~1270 lines), meters_block.h, macro_mapper.h / .cpp, plugins/membrum/src/dsp/coupling_matrix.h, pad_category.h, pad_glow_publisher.h, and the shared pieces: dsp/include/krate/dsp/systems/sympathetic_resonance.h, processors/true_peak_limiter.h, primitives/delay_line.h, core/scoped_denormal_mode.h (public API and contracts). Cover: initialize (buses declared, default kit), processParameterChanges (~145-535: globals, proxies, per-pad IDs, macros, what triggers recomputeCouplingMatrix; which point of each automation queue is used), processEvents (does it use sampleOffset?), consumePendingAudition, process() order, the aux-bus pointer handling and silence flags, the coupling chain (mono sum, delay read / write order, engine, applyEnergyLimiter in processor.h), the bus limiter, master gain, the pad-glow and MetersBlock publication, the CPU meter, getState / setState / applyKitSnapshot / applyPadPresetSnapshot (~871-1020: everything they flush and everything they leave), activateBus / setBusArrangements, setupProcessing / setActive (what is re-prepared, what is reset, what is not), connect / disconnect and the DataExchange lifetime, notify (the four message ids, which thread, what each writes).`,
  },
  {
    key: 'params-state-presets',
    prompt: `Map parameters, persistence and presets: plugins/membrum/src/plugin_ids.h, dsp/pad_config.h, dsp/default_kit.h, state/state_codec.h and state_codec.cpp (~580 lines, every function), controller/controller_state_codec.h / .cpp, controller/controller_state.cpp (setComponentState, kitPresetStateProvider, kitPresetLoadProvider, padPresetStateProvider, padPresetLoadProvider), preset/membrum_preset_config.h, preset/membrum_preset_container.h / .cpp, tools/membrum_preset_generator.cpp, tools/convert-memkit.js, and the shared preset manager the controller drives (plugins/shared/src/preset/: public API only). Cover: the kit blob and pad blob layouts as CODED (field order, types, counts), the version check and what a reader returns on a version mismatch or a short read, the PadConfig <-> PadSnapshot index mapping incl. the fields with no parameter ID (noiseLayerGain) and the -1 damping sentinels, the optional uiMode tail, endianness, the kit-preset save path (who builds the blob: the controller from parameter values, or the processor?) vs getState, the kit and pad preset load paths (the IMessage carrying the blob AND any per-parameter replay with performEdit), what setComponentState does on a version mismatch, how the generator builds the 20 kits, the preset directories and the install rule.`,
  },
  {
    key: 'controller-ui',
    prompt: `Map the controller + UI: plugins/membrum/src/controller/controller.h and controller.cpp (~1760 lines, by offsets: initialize ~274-787, setParamNormalized ~788, syncGlobalProxyFromPad / forwardGlobalToPad ~961-1002, createCustomView ~1017, verifyView ~1237, viewWillDelete ~1461, didOpen ~1493, willClose ~1643, the DataExchange receiver ~1694-1745, notify), controller_view_sync.cpp, controller_param_format.cpp, every file under plugins/membrum/src/ui/, resources/editor.uidesc (templates, custom views, control-tags), and the shared views it instantiates from plugins/shared/src/ui/ (public API only). Cover: editor lifecycle (createView, didOpen, willClose, the update banner + version label rule), EVERY raw view pointer the controller caches (controller.h ~188-405) and where each is set and nulled (willClose, viewWillDelete, the UIViewSwitchContainer swap between SelectedPadSimple and SelectedPadAdvanced), how verifyView identifies controls (tags, custom-view names, title text), the poll timer and the deferred view-refresh flags, the meters path (which thread delivers a block, who reads cachedMeters_), the pad grid (selection, glow, enabled mirror, audition messages), the proxy <-> per-pad parameter sync on a selected-pad change, the enable / disable logic for morph and pitch-envelope controls, the kit and pad browsers and save dialogs (callbacks capturing the controller, lifetime), bus-activation notification, parameter display formatting.`,
  },
  {
    key: 'shared-dsp-modal',
    prompt: `Map the SHARED PHYSICAL-MODELLING DSP Membrum wraps (dsp/include/krate/dsp/): processors/modal_resonator_bank.h (read END TO END: mode storage, the damping law and its units, setModes / updateModes, processBlock signature and argument order, damp(), flushSilentModes and getNumActiveModes, getModalEnergy, getInputGainSum, output gain, maximum mode count and block size, SIMD and alignment requirements, what happens to modes above Nyquist, NaN guards, allocations outside prepare), processors/waveguide_string.h, impact_exciter.h, bow_exciter.h, fm_operator.h, harmonic_oscillator_bank.h + harmonic_types.h (find which Membrum file uses them and for what), systems/sympathetic_resonance.h (resonator pool, how resonators are added and retired, isBypassed, per-sample process cost, stability). For EACH: the API contract (prepare / reset / process signatures, per-sample vs block, parameter ranges and units, stability limits, whether it can produce output from silence, latency) and then how Membrum calls it (prepared with the right rate and block size? reset when the body or exciter type changes and on note-on? ranges respected?). These headers are consumed by other plugins too (Innexus in particular): note the cross-plugin exposure of each suspected defect. Oddities = contract violations you suspect.`,
  },
  {
    key: 'shared-dsp-utility',
    prompt: `Map the SHARED UTILITY DSP Membrum relies on (dsp/include/krate/dsp/): primitives/svf.h, noise_oscillator.h, adsr_envelope.h, dc_blocker.h, waveshaper.h, wavefolder.h, tanh_adaa.h, envelope_utils.h, delay_line.h (readLinear vs write order, maximum delay vs the prepared size); processors/envelope_follower.h, true_peak_limiter.h (read END TO END: detection, oversampling, attack / release, look-ahead or not, latency, prepare's block-size argument, reset, non-finite input); systems/voice_allocator.h; core/xorshift32.h, sigmoid.h (hardClip, softClip), scoped_denormal_mode.h, math_constants.h, curve_table.h, pattern_freeze_types.h (find what Membrum takes from a pattern-freeze header). Same coverage: contracts, units, ranges and clamps, mono vs stereo state, latency, fast-math hazards, allocations outside prepare — and how the Membrum call sites (tone_shaper.h, noise_layer.h, click_layer.h, drum_voice.h, voice_pool.cpp, processor.cpp) honour them. Note the other plugins that include each header.`,
  },
  {
    key: 'specs-and-tests',
    prompt: `Map the SPEC CLAIMS and the TEST SUITE. Read specs/135-membrum-drum-synth.md and the spec.md of specs/136-membrum-phase1-scaffold, 137-membrum-phase2-exciters-bodies, 138-membrum-phase3-polyphony, 139-membrum-phase4-pads, 140-membrum-phase5-coupling and 141-membrum-phase6-ui (FR-xxx / SC-xxx: skim, list the testable ones), and plugins/membrum/MANUAL.md; list the testable claims (numbers, ranges, behaviours) as invariants with their source line. Then read plugins/membrum/tests/CMakeLists.txt (targets, per-file compile options) and skim every test file's TEST_CASE names and tags (grep is fine) to record what each file covers, which cases are hidden / [!mayfail] / [long] / [.perf], which assertions look tautological or too loose (Approx margins that swallow the effect, "is not silent" as the only check), which cases under tests/unit/diagnose/ run in the per-push gate and what they write to disk, which tests run on vstgui_test_stubs.cpp instead of the real views, how the golden .bin files are produced and compared, and which spec claims have no test. Also read the three PRIOR audits named in the architecture facts and record, per prior finding id, the regression test the fix named and whether a test with that name exists today. Oddities = claims the code probably violates, tests that cannot fail, prior findings whose regression test is missing.`,
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

const goodInv = (inventories || []).filter(i => i && i.mismatches)
const goodMaps = (maps || []).filter(m => m && m.oddities)
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
    prompt: `Hunt REAL-TIME SAFETY and THREADING violations. AUDIO THREAD: allocation (std::vector growth / resize / assign, std::function, std::string, new, make_unique, a variant emplace that constructs a heap-owning type), locks, exceptions, IO / logging, unbounded loops, blocking calls, reachable from Processor::process(), processParameterChanges, processEvents, consumePendingAudition, VoicePool::noteOn / noteOff / both processBlock overloads / beginFastRelease / processChokeGroups / setPadConfigField / applyPadConfigToSlot, DrumVoice::noteOn (it re-runs the body mapper and the strike normalisation on every hit: what does that cost, can it allocate, is the cost bounded?), processBlockFast / processBlockSlow, the ExciterBank / BodyBank type swaps, MacroMapper::apply, recomputeCouplingMatrix, the SympatheticResonance engine, TruePeakLimiter::processBlock, the DataExchangeHandler calls in process() (getCurrentOrNewBlock, sendCurrentBlock: do they allocate or lock in the SDK's IMessage fallback?) and std::chrono::steady_clock::now(). CROSS-THREAD: Processor::notify runs on the host's message thread. Trace "LoadPresetState" and "LoadPadPreset" into applyKitSnapshot / applyPadPresetSnapshot and list every piece of state they write (the voice pool and its voices, the pad configs, the coupling engine and matrix, the macro cache, plain members such as selectedPadIndex_, padCategories_, energyEnvelope_) while process() may be running: is there a transfer object, a flag checked by the audio thread, or nothing? Ask the same of setState (hosts call it off the audio thread while processing), getState reading the pad configs, setActive / setupProcessing while running, activateBus writing busActive_ (plain bools read in process), the pendingAudition_ word (two auditions between two blocks: is one lost?) and editorOpen_. Atomics with the wrong memory order or non-lock-free types; data shared with the UI thread without an atomic or a transfer object (PadGlowPublisher, MetersBlock, the controller's cachedMeters_). Focus: plugins/membrum/src/processor/*, src/voice_pool/*, src/dsp/drum_voice.h, src/dsp/pad_glow_publisher.h, and the shared headers they call.`,
  },
  {
    key: 'dsp-numerics',
    prompt: `Hunt NUMERICAL defects: NaN/Inf sources — modal coefficients at parameter extremes (Size 0 / 1, Decay 0 / 1, Material 0 / 1, mode stretch 0.5 / 2, scatter, air loading, the damping overrides and their -1 sentinel reaching a formula as a number), mode frequencies at or above Nyquist at 44.1 kHz (dropped, clamped, or left to alias or blow up?), pole radius >= 1, division by getInputGainSum or by a measured strike peak that can be 0 (strike normalisation of a silent excitation), log / pow / sqrt / acos at domain edges, std::pow(energyAlpha_, chunk), tension modulation with runaway energy, the 20..2000 Hz log mapping and a zero-time pitch envelope, SVF coefficient tan near Nyquist at maximum cutoff / resonance 1 / 192 kHz, drive and fold at 1.0, the WaveguideString at extreme pitch (a delay shorter than its minimum), FM ratio and feedback extremes, a Feedback-exciter loop gain >= 1, the division inside applyEnergyLimiter (processor.h), the master-gain pow. FINITENESS CHECKS: plugins/membrum/src/dsp/float_guard.{h,cpp} and every isnan / isfinite / bit-mask test under plugins/membrum (the repo rule: classify only through Krate::DSP::detail::isFinite / isNaN / isInf; a per-file -fno-fast-math does not protect inline header code; an "IEEE" flag set in CMake is -fno-fast-math -fno-finite-math-only -ffp-contract=off on one line — check plugins/membrum/CMakeLists.txt and plugins/membrum/tests/CMakeLists.txt ~250-270). The hardClip rail with a NaN input (does NaN pass to the bus limiter and latch its gain?), TruePeakLimiter with non-finite input. Denormals: ScopedDenormalMode is set in process(): is any DSP run outside it (a flush in setState or notify, the UI thread)? Float == comparisons used as gates ("amount != 0.0f", "gain != 1.0f", "unchanged"). Exactness by cancellation at neutral points (macro 0.5 = zero delta, pan 0.5 = unity, the modeStretch default 0.333333f meant to denormalize to exactly 1.0, the morph-duration default 0.095477f): do they land exactly, also under fast-math contraction? Smoothing coefficients derived before the sample rate is known; time constants hard-coded as per-sample coefficients (independent of the sample rate); uninitialised state read on the first block. Focus: plugins/membrum/src/dsp/**, src/processor/processor.{h,cpp}, src/voice_pool/voice_pool.cpp, and the KrateDSP headers Membrum includes.`,
  },
  {
    key: 'exciters',
    prompt: `Hunt EXCITER bugs, one exciter at a time (Impulse, Mallet, NoiseBurst, Friction, FMImpulse, Feedback, Clap) and the ClickLayer: velocity -> amplitude and velocity -> spectrum mapping (does a soft hit differ from a hard hit in anything but level?), trigger / retrigger state reset (a retrigger while the previous excitation still runs), durations and contact times that depend on the sample rate or the host block size, the deferred type swap in ExciterBank (a swap requested while the voice sounds; stale state of the previous type; the swap landing mid-excitation), body-feedback use (only Feedback should consume it; on the fast path it is the previous sample or the previous block's last sample), the per-pad secondary parameters (FM ratio, feedback amount, noise-burst duration, friction pressure: range, default, does each reach its exciter, does it do anything on the other six), how a Friction voice ends when no note-off ever comes (choke, steal, kit switch, safety net) and whether a note-off ends it cleanly, Feedback stability and level, Clap (burst count and spacing, sample-rate dependence, randomisation), noise determinism and seeding (the same noise on every hit or on every voice = a machine-gun effect; different noise on every run = non-reproducible renders: which is intended?), the click layer (2..5 ms contact mapping, brightness, half-into-excitation / full-into-output routing, kStandaloneOutputGain, mix 0 must be silent AND must not excite the body), exciter output level across types (it feeds the strike normalisation: which exciters defeat or confuse it?). Focus: plugins/membrum/src/dsp/exciter_bank.h, src/dsp/exciters/*.h, src/dsp/click_layer.h, dsp/include/krate/dsp/processors/impact_exciter.h, bow_exciter.h, fm_operator.h.`,
  },
  {
    key: 'bodies-modal',
    prompt: `Hunt BODY / MODAL bugs: per body the mode table and mapper (ratios vs their stated source; amplitude weighting by strike position; Size -> fundamental against the documented 500 Hz .. 50 Hz and bodies/natural_fundamental.h; Material -> brightness / damping; Decay -> b1; the damping law's units as the bank expects them vs what each mapper passes; the override parameters and the -1 sentinel), features implemented in some mappers and silently missing in others (air loading, mode scatter, mode stretch, decay skew, damping overrides, strike position), mode count vs the bank's capacity, modes above Nyquist; the measured-strike normalisation (measuredStrikeOutputGain and the StrikeNormKey cache: which parameters change the strike response but are missing from the key = a stale gain; what the measurement costs at note-on; what it does to level differences between sizes, materials, strike positions and velocities); retuneFundamental / updateModalParameters / the material refresh while the body rings (does a retune reset phase or amplitude, click, or re-derive amplitudes; live Size / Material edits on a ringing voice); the String body (tuning, damping and brightness mapping, pitch-envelope retune) and every feature that reads the SHARED MODAL BANK while the String body is active (retirement on getNumActiveModes, the modal-energy follower for tension and wire coupling, noteOff / fast-release damping through getSharedBank().damp()): do they still work for a String voice?; the NoiseBody; a body-type switch that leaves stale modes in the shared bank; the secondary shell bank (ratios, size mapping, the coupling clamp, the level of the additive mix, and retirement: the voice retires on the PRIMARY bank, so is a ringing shell cut off or does it keep an inaudible voice alive?); tension modulation (the Membrane-only gate, the scale, the clamp, the follower coefficient vs chunk length and sample rate); noteOff damping (kNoteOffDampScale = a per-sample radius scale: is the resulting decay time independent of the sample rate?) and fast-release damping. Focus: plugins/membrum/src/dsp/body_bank.h, src/dsp/bodies/*, src/dsp/membrane_modes.h, src/dsp/drum_voice.h (noteOn, ~1418-1700), dsp/include/krate/dsp/processors/modal_resonator_bank.h, waveguide_string.h.`,
  },
  {
    key: 'voice-signal-path',
    prompt: `Hunt VOICE ORCHESTRATION bugs in plugins/membrum/src/dsp/drum_voice.h: diff the THREE renderers process() (~497), processBlockFast (~682) and processBlockSlow (~933) stage by stage and report every divergence (a stage missing in one, a different order, a different gain, the click routing, the noise gain chain, wire coupling, tension modulation, the secondary bank, the energy follower, what is refreshed per chunk vs per sample); chunking by kMaxBlockSize with the pitch envelope and the morph advanced once per chunk (stair-stepping of short envelopes; the rendered audio depending on the host block size because chunk boundaries move; an envelope whose first value is applied a chunk late); the lastBody feedback handoff between chunks and blocks; note-on ordering (config applied, mapper, normalisation, envelopes triggered, exciter triggered: anything used before it is set?); a retrigger of a sounding voice (state carried over: bank phase and amplitude, energyEnv_, wireEnergyPeak_, silentBlockCount_, envelopes, lastBody: click or level jump?); the amp envelope (its fixed attack / release constants, what a note-off does to a one-shot drum, hosts and pad clicks that never send a note-off); the retirement logic (bank silent AND envelope finished; the safety net counts 256 BLOCKS, so its time depends on the host block size; a noise layer, click layer or resonant tone-shaper filter still sounding when the bank goes silent = a cut tail; a layer that settles at a non-zero value = an immortal voice); level_ and the hardClip rail (what reaches it in normal use); velocity handling end to end; scratch-buffer sizes vs the chunk size; the inactive early-out. Also check noteOff, resetForKitSwitch and fastReleaseDamp for the non-modal bodies.`,
  },
  {
    key: 'layers-shaper',
    prompt: `Hunt bugs in the LAYERS and the SHAPER: NoiseLayer (filter mode and the cutoff / resonance / decay / colour mappings, the standaloneGain calibration, mix 0 = exactly zero and no CPU, an envelope that holds in Sustain: is the output exactly zero there?, per-pad noiseLayerGain (a state-only field with no parameter: who can set it, is it reset by a pad preset, can the editor show it?), the wire-coupling gain (block-rate, taken from the previous chunk; the peak tracker's reset per note; behaviour when the body is a String or NoiseBody)), ToneShaper (order drive -> wavefolder -> DC blocker -> SVF; bypass equivalence when every control is neutral; filter-type switching with stale state; cutoff 20 Hz..20 kHz mapping and resonance -> Q; the filter envelope (ADSR mapping, bipolar amount, retrigger, what sustain means for a drum that gets no note-off); drive / fold gain compensation; the DC blocker's corner vs 30-50 Hz kick fundamentals; aliasing from drive and fold with no oversampling), the pitch envelope (three points, knee, mid fraction, both curves, time 0 = off, start / end mapping; the interaction with Size: which one sets the fundamental while the envelope runs, and what happens at its end; retrigger), UnnaturalZone (modeInject: amount 0 exactly free, bounded amplitude when ringing, stopped by choke / steal / kit switch / note-off?; nonlinearCoupling: stability, gain, amount 0 = identity), Material Morph (enable gating, start / end / duration / curve mapping, retrigger, the once-per-chunk refresh, morph on a String body), mode stretch and decay skew (applied on every body? at note-on only?). Focus: plugins/membrum/src/dsp/noise_layer.h, tone_shaper.h, pitch_segment_envelope.h, unnatural/*.h, their call sites in drum_voice.h, and the primitives they use.`,
  },
  {
    key: 'voice-management-midi',
    prompt: `Hunt VOICE MANAGEMENT and MIDI bugs. Events (processor.cpp processEvents ~559): is event.sampleOffset used at all, or does every note start at the block start (quantify: timing error up to one block, two hits in one block collapsing onto each other, flams and fast rolls)? The order of events inside a block (a note-off and a note-on of the same pad; two note-ons of the same pad), velocity 0, notes outside 36..67, other event types dropped silently (is that documented?). The audition path (a single atomic word: two auditions between blocks, an audition while the processor is not processing; its velocity scale). What a note-off does to a drum: it damps the body, so the same pad sounds different with 10 ms MIDI notes than with a pad click that never releases — intended per spec and manual? VoicePool: allocation through VoiceAllocator (pad -> slot, a retrigger of the same pad: reuse or new voice, does a pad choke itself), the stealing policies (Oldest / Quietest / Priority as defined in the spec vs the code; ties; how fresh the currentLevel metric is), fast release (5 ms exponential in a shadow slot; ONE fade per slot: what happens to a second and third steal of that slot inside 5 ms — hard cut, click, dropped note?), choke groups (lookup by note, group 0, hats), setMaxPolyphony reduced while voices sound, a pad disabled or re-routed to another bus while it sounds, resetAllVoicesForKitSwitch (click, voices left half-released), the VoiceSlotState machine and the meta bookkeeping (originatingNote, currentLevel, fastReleaseGain), getActiveVoiceCount / isAnyVoiceActive vs the shadow slots, pan gains, numSamples above kVoicePoolMaxBlock (chunked or overflow), numSamples == 0, null aux pointers and inactive buses in the multi-bus overload. Focus: plugins/membrum/src/processor/processor.cpp (~536-600), src/voice_pool/*, dsp/include/krate/dsp/systems/voice_allocator.h.`,
  },
  {
    key: 'gain-staging-master',
    prompt: `Hunt GAIN-STAGING and MASTER-SECTION bugs. Follow a unit-velocity hit numerically through every multiply and clamp: exciter output -> strike normalisation (kBodyHeadroom) -> secondary-shell mix -> noise (standaloneGain x noiseLayerGain x wire gain) -> click (kStandaloneOutputGain) -> unnatural zone -> tone shaper (drive make-up) -> envelope x level -> hardClip -> pan -> voice sum -> coupling return (energy limiter at -20 dBFS) -> TruePeakLimiter at -1 dBTP -> master gain (default -6 dB, up to +12 dB AFTER the limiter) -> meters. Find: double scaling; a stage that loses most of the level; a clip reached early; a compensation that cancels a parameter (the normalisation erasing the level difference a real drum has between sizes, materials, strike positions or velocities); N coincident voices vs the limiter; the limiter working on ordinary single hits (shaved transients, pumping, every kit settling at the same loudness); its stereo linking, attack and release on drum transients, and whether it really holds a true-peak ceiling with no latency; master gain placed after the limiter (the output can exceed 0 dBFS at +12 dB: intended?), master gain with no smoothing (zipper noise under automation), the "gain != 1.0f" gate; aux buses: pre-limiter and pre-master (documented? the same pad is louder or quieter on its aux bus than on main; the pad stays on main too, so a DAW mix doubles it unless the manual says how to avoid that); the pan law; silence flags (main is reported non-silent forever while coupling is enabled, even with no voice; aux flags computed by scanning samples; flags of inactive or null buses); peak meters and the CPU meter (steady_clock per block, EWMA); applyEnergyLimiter (processor.h: its attack / release coefficients are fixed per-sample numbers, not derived from the sample rate; verify what the update equation does with them and what a -20 dBFS cap means for audible coupling). Focus: plugins/membrum/src/dsp/drum_voice.h, src/voice_pool/voice_pool.cpp, src/processor/processor.{h,cpp}, dsp/include/krate/dsp/processors/true_peak_limiter.h.`,
  },
  {
    key: 'coupling',
    prompt: `Hunt CROSS-PAD COUPLING bugs: how the voice pool feeds the SympatheticResonance engine (setCouplingEngine; which note events add or retire resonators; the per-pad couplingAmount), the CouplingMatrix and PadCategory rules (the rule order in dsp/pad_category.h; a pad edit that flips its category mid-performance; the snare-buzz and tom-resonance knobs -> matrix gains), every trigger of recomputeCouplingMatrix (does a kit load, a pad-preset load, a macro move, an exciter / body change, a per-pad coupling-amount change reach it?), the "CouplingMatrixEdit" / "CouplingMatrixSnapshotRequest" messages that processor.h documents (does notify() handle them, does the controller send them, or is that a documented feature that does not exist?), isBypassed() (the whole loop is skipped at global coupling 0: do the delay line and the engine keep stale content that bursts out when coupling is raised again?), the delay (0.5..2 ms, read before write, prepared for 2 ms: the exact maximum at 192 kHz; a delay change mid-note), the loop: the coupling output is added to the main bus, whose mono sum is the engine's input one delay later — what is the loop gain, what bounds it, and can a held or rapidly repeated hit make it grow; the energy limiter (state, cap, sample-rate dependence); the mono sum taken AFTER pan and from the main bus only; coupling absent from the aux buses; resonator count and CPU with all 32 pads coupled; engine and delay reset on kit switch, setActive and sample-rate change; self-noise with no notes. Cite the engine's contract line AND Membrum's call site. Focus: plugins/membrum/src/processor/processor.{h,cpp}, src/voice_pool/voice_pool.cpp, src/dsp/coupling_matrix.h, src/dsp/pad_category.h, dsp/include/krate/dsp/systems/sympathetic_resonance.h.`,
  },
  {
    key: 'param-plumbing-macros',
    prompt: `Hunt PARAMETER-PLUMBING bugs, starting from the INVENTORY mismatches (confirm each in code before reporting). The proxy system: a selected-pad change -> syncGlobalProxyFromPad (does it cover EVERY proxy, including 290-326?); a proxy write -> forwardGlobalToPad on the controller AND proxy handling in the processor (applied twice? applied by only one side, so host automation of a proxy does nothing while the editor is closed?); host automation recorded on a proxy and replayed with another pad selected (it writes the wrong pad: are proxies flagged non-automatable?); kSelectedPadId itself as an automatable, state-saved parameter. The per-pad parameters: how many are registered (32 pads x 66 offsets), their flags, titles and units; float-as-bool thresholds; selector rounding (the same normalized value mapped to the same index on every side). processParameterChanges: only the last point of each queue used (no sample-accurate automation: acceptable for which parameters?), the ordering of proxy vs per-pad vs macro changes that arrive in the same block. setPadConfigField: which fields change a sounding voice and which wait for the next hit, and whether that matches what the editor suggests. MacroMapper: delta-from-registered-default arithmetic (macro 0.5 = no change); a macro move overwriting a direct edit of the same underlying parameter, or the reverse; BOTH the macro value and the underlying parameter being saved, so a load applies the delta twice; the macro cache and its resync triggers (kit load, pad-preset load, selected-pad change, setState); clamping; macros that drive offsets beyond the 64-entry registered-defaults table; the registered-defaults table drifting from Controller::initialize. Default drift between the PadConfig initializers, the registrations, the default kit and the generator. Denormalization formulas duplicated between the voice and the display code with different ranges. Focus: plugins/membrum/src/plugin_ids.h, src/dsp/pad_config.h, pad_config_apply.h, src/processor/processor.cpp (~59-535), macro_mapper.{h,cpp}, src/voice_pool/voice_pool.cpp (~605-790), src/controller/controller.cpp (~274-1002), controller_param_format.cpp.`,
  },
  {
    key: 'parameter-audibility',
    prompt: `You are the PARAMETER-AUDIBILITY reviewer (the static twin of the kit-quality probe). Enumerate every user-facing parameter (the 66 per-pad offsets, the globals, the five macros) and trace each from its PadConfig field through applyPadConfigToVoice into the DSP that uses it. Rate each STRONG / WEAK / NONE / CONTEXT-ONLY and report as findings: NONE = never read by the DSP, overwritten before use, cancelled by a later normalisation, or clamped to one value; WEAK = the effective range collapses (mapped into a narrow sub-range, multiplied by a near-zero constant, masked by a later stage such as the strike normalisation, the limiter or a fixed filter); CONTEXT-ONLY = it works only for one exciter, one body or behind an enable switch, AND the editor still shows it as a live control in the other contexts (cross-check the enable / disable logic in plugins/membrum/src/controller/controller_view_sync.cpp and the two pad templates in resources/editor.uidesc); parameters whose two halves of the range do the same thing; parameters applied only at the next note-on while the editor suggests a live control. Then the KIT level: read tools/membrum_preset_generator.cpp and plugins/membrum/src/dsp/default_kit.h and report structural reasons for kits and pads sounding alike, with counts: kits built from the same few templates with small offsets, parameters that never vary across all 20 kits (which ones stay at their default everywhere), pads 16-31 duplicating pads 0-15, categories that differ in name only. Give the code reason for every item; the probe supplies the numbers.`,
  },
  {
    key: 'lifecycle-state',
    prompt: `Hunt VST3 LIFECYCLE and STATE bugs. The codec (state/state_codec.cpp): write vs read order, type and count for the kit blob and the pad blob; the version check — the reader accepts ONLY version 5. (a) RELEASE CHECK: list the released tags from the remote (git ls-remote --tags origin "refs/tags/membrum/*": release state comes from origin's tags) and read which blob version each wrote (git show <tag>:plugins/membrum/src/plugin_ids.h and <tag>:plugins/membrum/src/state/state_codec.h; say so if a tag is not available locally, do not fetch). If any released build wrote another version, today's build rejects projects and kits saved with it: report it with the exact versions. (b) What the user gets on a rejected or short read: the return code of setState and of setComponentState, whether the previous kit stays or defaults appear, whether processor and controller end up showing different values, whether anything tells the user. (c) Tests and goldens that name versions the codec does not accept (tests/unit/processor/test_state_v6_migration.cpp, tests/golden/phase2_state_v2.bin). Parsing robustness: a short or truncated stream (is anything applied before the failure?), garbage enum values, NaN or out-of-range doubles in a sound slot (are they clamped before they reach the DSP?), maxPolyphony outside [4,16], choke group above 8, output bus above 15, a selected pad out of range, the optional uiMode tail, endianness, float64 on the wire vs float in PadConfig, the -1 damping sentinels surviving the round trip. applyKitSnapshot / applyPadPresetSnapshot: what they flush and what they leave (bus limiter, energy envelope, coupling delay), FR-052 (a kit load keeps the selected pad) and FR-061 (what a pad preset must not touch). setComponentState vs the processor's state (proxies resynced? the pad grid mirror?). setActive(true) re-preparing the pool (it kills ringing voices; it does not reset the coupling engine or delay); setupProcessing after a sample-rate or block-size change (is EVERYTHING re-prepared: limiter, coupling, pool, voices' scratch sizes); maxSamplesPerBlock above kVoicePoolMaxBlock; setBusArrangements (non-stereo, fewer or more buses), activateBus and busActive_ (its initializer sets only bus 0), the buses declared in initialize vs the AU configuration (resources/au-info.plist.in, resources/auv3, the audiounitconfig channel pairs); initialize / terminate balance and the DataExchange handler's lifetime across connect / disconnect / setActive; process() with numOutputs == 0, null channel buffers, 64-bit sample size, offline mode; tail and latency reporting. Focus: plugins/membrum/src/state/*, src/processor/processor.{h,cpp}, src/controller/controller_state.cpp, controller_state_codec.{h,cpp}, src/preset/*.`,
  },
  {
    key: 'ui-wiring',
    prompt: `Hunt UI WIRING bugs, starting from the INVENTORY mismatches (confirm each in code before reporting): control-tags vs plugin_ids.h; parameters registered but absent from both pad templates or the reverse; StringListParameter entry counts vs enum ranges. DANGLING VIEW POINTERS: every raw view pointer the controller caches (controller.h ~188-405) must be nulled in willClose AND in viewWillDelete AND when the UIViewSwitchContainer swaps SelectedPadSimple / SelectedPadAdvanced (list each pointer and the places that null it; a pointer set in verifyView for a view that lives inside the swapped template is the classic miss). verifyView handlers that identify controls by title text ("Knee", "Dur") or assume a sibling view exists. Timers and deferred work that can run after willClose (the poll timer, requestViewRefresh / applyViewRefresh flags, preset-browser callbacks capturing the controller). The meters path (which thread delivers DataExchange blocks; cachedMeters_ shared with the timer; the IMessage fallback; blocks arriving after the editor closed). PadGridView (selection <-> kSelectedPadId both ways, glow and enabled mirrors, audition messages on press and release, every mouse interaction it offers: note each so no fix removes one), KitMetersView, PolyphonySlider <-> kMaxPolyphonyId [4,16], XYMorphPad <-> morph start / end and its enabled state, ADSRDisplay + ADSRExpandedOverlayView <-> the filter-envelope parameters, PitchEnvelopeDisplay <-> the pitch-envelope parameters incl. the knee, the output-bus selector, its tooltip and the bus-activation path (notifyBusActivation: who calls it, given the processor cannot call the controller directly?), the kit and pad browsers, inline browsers and save dialogs (lifetime, failure flags, the status label), UI mode (session scope; the template swap), the greying logic for morph and pitch-envelope controls vs the DSP's real context, parameter display strings vs the DSP's real range, the update banner / version label rule (plugins/shared/src/ui/update_banner_view.h). Focus: plugins/membrum/src/controller/*, src/ui/*, resources/editor.uidesc, and the shared views under plugins/shared/src/ui/ that Membrum instantiates.`,
  },
  {
    key: 'shared-dsp-contracts',
    prompt: `Hunt CONTRACT VIOLATIONS between Membrum and the KrateDSP it wraps: a processor whose process() expects a block but is called per sample (or the reverse); prepare() never called, called with the wrong sample rate, or with a block size smaller than what is later passed (TruePeakLimiter::prepare(maxBlockSize) vs larger host blocks; ModalResonatorBank vs kMaxBlockSize chunks); reset() not called on a body / exciter type switch, on note-on, on kit switch or on setActive; ARGUMENT ORDER: drum_voice.h calls bodyBank_.processBlock(bodyScratch, excScratch, chunk) and secondaryBank_.processBlock(secExc, secOut, chunk) — two different orders at two call sites; check both against the callee signatures (BodyBank's wrapper vs the raw ModalResonatorBank); setters given normalized values where the primitive expects plain (or dB vs linear, Hz vs normalized, ms vs seconds, Hz vs rad/s in the damping law); ranges outside what the primitive clamps (resonance, feedback >= 1, mode counts, delay beyond the prepared maximum); setters that recompute expensive tables when called for every pad parameter change or on every hit; components that emit output from silence (sympathetic resonance, feedback, bow, self-oscillating filter) with no gate; VoiceAllocator API misuse (calling order, ignored return values, note ids); DelayLine read / write order and the fractional read at the maximum delay; ADSREnvelope used as a one-shot with no note-off; XorShift32 seeding; why a drum synth includes core/pattern_freeze_types.h (which symbol; a layering smell or a real dependency?) and harmonic_oscillator_bank.h. Cite BOTH sides (Membrum's call site AND the primitive's contract line). A defect in the shared header itself is CROSS-PLUGIN IMPACT: name the other plugins that include it (grep -rl the header under plugins/). Focus: plugins/membrum/src/dsp/**, src/voice_pool/voice_pool.cpp, src/processor/processor.cpp, and the dsp/include/krate/dsp headers Membrum includes.`,
  },
  {
    key: 'duplication-drift',
    prompt: `Hunt DUPLICATION and COPY-PASTE DRIFT: the same denormalization formula written in dsp/pad_config_apply.h or drum_voice.h, in controller/controller_param_format.cpp, in controller/controller_view_sync.cpp (the display updates), in processor/macro_mapper.cpp, in tools/membrum_preset_generator.cpp, in dsp/default_kit.h and in tests, with subtle differences (find the copy where one differs: a range, a law, a default); the codec constants and layout re-declared in the generator and restated in comments (state_codec.h, processor.cpp, plugin_ids.h); the six body mappers and the seven exciters as copy-paste families (the member that lacks a feature or carries a different constant); the proxy registration vs the per-pad registration of the same parameter in Controller::initialize (title, default, range, step count drift) and the proxy branches vs the per-pad branches in processParameterChanges; the four snapshot functions in state_codec.cpp and their index tables, and controller_state_codec.cpp beside them; constants defined twice with different values or as bare literals beside a named constant (the note range 36 / 67 in processor.cpp vs kFirstDrumNote / kLastDrumNote, kNumPads, voice counts, kMaxBlockSize vs kVoicePoolMaxBlock, the 64-entry defaults table vs 66 offsets); plugin-local re-implementations of things that exist in plugins/shared/ (InlinePresetBrowserView vs the shared preset browser, src/ui/adsr_expanded_overlay.h vs the one Ruinae has, membrum_buttons.h vs the shared buttons, PolyphonySlider); tools/membrum-fit duplicating the plugin's parameter mapping (report DRIFT between the two only; the tool itself is out of scope); names that could collide (ODR): Membrum types declared outside namespace Membrum or with the same name as a type in dsp/ or plugins/shared/. Focus: plugins/membrum/src/**, plugins/shared/src/**, tools/membrum_preset_generator.cpp.`,
  },
  {
    key: 'parallel-paths',
    prompt: `Hunt PARALLEL PATHS: two or more separate code routes that do the SAME job, where one should exist. This is not copy-paste drift (another finder covers that); the routes can look nothing alike. Method (use the Serena MCP tools when available, ToolSearch "serena": mcp__serena__find_referencing_symbols finds every caller of an entry point, including template and virtual call sites grep misses; fall back to Grep): first list every job the plugin performs at a boundary, then, for each job, list EVERY entry point that performs it (grep the callers, follow each to where the work ends), then report every job with more than one route. Jobs to enumerate at minimum: loading a full kit (host setState, the kit browser's "LoadPresetState" message, any per-parameter replay the controller does on a kit load, the default kit at initialize, setComponentState), loading one pad (the "LoadPadPreset" message, a per-parameter replay), saving a kit (getState vs kitPresetStateProvider: is the preset blob built from the controller's parameter values or from the processor's truth, and can they differ in the fields that have no parameter such as noiseLayerGain and the -1 sentinels?), applying one parameter value (per-pad ID, proxy ID, macro, kit load, pad preset), configuring a voice from a pad (applyPadConfigToVoice vs any other setter route into DrumVoice, at note-on vs live), rendering a voice (process, processBlockFast, processBlockSlow), triggering a note (MIDI event vs the audition message), ending sounding notes (note-off, choke, steal, kit switch, setActive, polyphony reduction, pad disable), resetting the engine, sending processor data to the controller (DataExchange, its IMessage fallback, output parameters). A route that applies a state or value WITHOUT going through the single canonical function is a finding even when today both routes give the same result: name what each route does that the other does not (flushes, resets, non-parameter data, clamps, version checks, thread), and the observable consequence. When an earlier fix "completed" a second route by adding the missing piece to it, report that as reinforcing the duplication. For each finding give the job, every route with file:line, the canonical route it should collapse into, and a test that proves the routes leave identical state (e.g. getState bytes after route A vs route B). Focus: plugins/membrum/src/** (processor.cpp, controller_state.cpp, controller_state_codec.cpp, state_codec.cpp, voice_pool.cpp, drum_voice.h), plugins/shared/src/preset/**.`,
  },
  {
    key: 'spec-conformance',
    prompt: `Hunt SPEC CONFORMANCE failures: take the testable claims from the specs-and-tests map (and read specs/135-membrum-drum-synth.md, the spec.md files of specs/136..141-membrum-phase*/ and plugins/membrum/MANUAL.md yourself for numbers: ranges, counts, voice limits, signal-flow order, stealing policies, choke behaviour, coupling behaviour, master gain, routing), then check the code honours each one; check that code comments citing an FR-xxx / SC-xxx or a research.md section implement what the comment says. COMMENTS THAT LIE are findings — group them: ONE finding per file listing its stale statements (examples to confirm: the signal-path comment at the top of drum_voice.h, "Phase 3.0: scaffolding only -- all methods are stubs" in voice_pool.h, the v3 / 57-slot text in state_codec.h, the "State v4" layout comment in processor.cpp, the per-pad ID range and "has not shipped" text in plugin_ids.h, the message names documented in processor.h). Also docs/index.html, docs/membrum_signal_flow.dot, CHANGELOG.md, plugins/membrum/CLAUDE.md and .claude/skills/membrum/SKILL.md claims against behaviour. Report the claim, its source line, and the code line that contradicts it.`,
  },
  {
    key: 'portability-quality',
    prompt: `Hunt CROSS-PLATFORM and CODE-QUALITY problems: narrowing in brace-init that MSVC accepts but Clang rejects, std::isnan / bit-mask finiteness under -ffast-math, numeric_limits infinity() / quiet_NaN() constants in DSP code, per-file IEEE flag sets missing -fno-finite-math-only or -ffp-contract=off, iteration over an unordered container that feeds serialization, names that collide with Apple globals / macros (MAC, Rect, Point, Component, Fixed, Boolean...), missing includes that MSVC forgives (<cstdint>, <cstring>, <algorithm>, <array>, <numbers>), signed / unsigned and narrowing mixups in the pad-index, note and bus arithmetic (int16 pitch -> uint8, int -> size_t), MetersBlock copied by memcpy across the component boundary (layout, padding, its 32-byte alignment rounding), uninitialised members, unused results, misleading names or comments, God functions (name the worst with line counts: processParameterChanges, Controller::initialize, DrumVoice::noteOn, verifyView), magic numbers duplicating named constants, dead code and dead parameters, test-only accessors in production classes, naming-convention violations (members without a trailing underscore, constants without k), header-only code with non-inline definitions, NOLINT markers that hide a real problem. Focus: plugins/membrum/src/** and plugins/membrum/tests/**.`,
  },
  {
    key: 'test-gaps',
    prompt: `Hunt TEST DEFECTS and COVERAGE GAPS in plugins/membrum/tests/: assertions that cannot fail (REQUIRE(true), checking only non-silence, Approx margins wider than the effect), goldens that are self-generated with no independent check (tests/golden/*.bin and approval/test_phase1_regression.cpp: how are they produced, are they compared bit-exactly, what would a wrong golden look like, will they go red on macOS fast-math?), the tests/unit/diagnose/ cases (un-hidden tags such as [diagnose_n1]: do they assert anything, what do they write to disk during a per-push gate?), hidden cases that hide known reds or do real work (the hidden kit-regeneration case in test_factory_kit_presets.cpp, [.snare-audition], the hidden part of test_kit_switch_infinite_ring.cpp), tests that pass on vstgui_test_stubs.cpp and would not on the real view, duplicated tests, wall-clock assertions without the [wallclock] tag, and MISSING coverage for the riskiest behaviours: note timing vs sampleOffset and block-size invariance; every exciter x body combination for NaN and runaway level at 192 kHz and at parameter extremes; preset or state load from the message thread while process() runs; state written by released versions; truncated and corrupt state; every factory kit RENDERED per pad (silence, clipping, DC, infinite ring) rather than only parsed; limiter engagement on ordinary playing; aux-bus level and silence flags; proxy vs per-pad equivalence; macro round trip; the third steal of a slot inside 5 ms; choke and kit-switch clicks; live polyphony reduction; equivalence of the three voice renderers; pitch and decay across sample rates; a coupling stability soak; the DataExchange IMessage fallback (on Linux a bare harness has no run loop: gate such checks, do not assert them); the editor lifecycle with the pad template swapped while open. For each gap name the test file to extend, the TEST_CASE name and tags, and the assertion.`,
  },
  {
    key: 'prior-audit-regressions',
    prompt: `You are the PRIOR-AUDIT REGRESSION checker. Read the three prior audits: (1) plugins/membrum/docs/correctness-audit.md (Findings 1..23), (2) plugins/membrum/AUDIT-signal-path-2026-06-07.md with AUDIT-signal-path-2026-06-07.findings.json (the "confirmed" array; the REMEDIATION STATUS tracker and its open items), (3) for the preset-load findings that touched Membrum, grep "Membrum" and "R-20" in specs/ruinae-audit/report.md and fix-plan.md. For EVERY prior finding: open today's code at the place the finding names (line numbers have moved: locate by function and symbol), decide whether the defect is (a) fixed and guarded by a test, (b) fixed with no regression test, (c) partially fixed (one site fixed, a sibling site with the same pattern still wrong), or (d) still present / reintroduced. Report a finding for every (c) and (d) (category prior-audit-regression, put the prior id in the title, e.g. "M-2 still open: ..." or "Finding 12 partially fixed: ..."), saying when the tracker already lists it as open, and a low-severity test-gap finding for each (b) that names the missing test. Do not report (a). Also look for the SAME defect patterns in code added after those audits (wire coupling, the Clap exciter, per-pad pan, the three-point pitch envelope, the update banner, noiseLayerGain).`,
  },
  {
    key: 'gestalt-processor',
    prompt: `You are the GESTALT reviewer for the processor side: read plugins/membrum/src/processor/processor.h and processor.cpp END TO END, top to bottom, then src/voice_pool/voice_pool.cpp and src/processor/macro_mapper.cpp, and flag anything that does not make sense — a comment that says one thing while the body does another, branches that cannot execute, work done twice per block or redone for all 32 pads when one changed, values computed then ignored, ordering that looks accidental (parameter changes vs events vs audition vs render; coupling vs limiter vs master gain), block-rate handling of things that need sample accuracy, early returns that skip cleanup or leave output buffers unwritten (numSamples == 0, no outputs, null buffers: are the outputs cleared, are silence flags set?), the main bus never being cleared before the pool writes it (does the pool overwrite or add?), TODO / FIXME hiding bugs, leftovers of earlier phases (dead members, "Phase N" comments describing code that no longer exists, test-only hooks). Your value is noticing what category-driven review misses.`,
  },
  {
    key: 'gestalt-voice',
    prompt: `You are the GESTALT reviewer for the voice: read plugins/membrum/src/dsp/drum_voice.h END TO END by offsets, top to bottom, then body_bank.h, exciter_bank.h, tone_shaper.h, noise_layer.h and click_layer.h, and flag anything that does not make sense — header comments that describe an older design, setters that store a value nobody reads, getters returning stale state, the three renderers drifting apart, members set in prepare() but not restored by reset / noteOn / resetForKitSwitch (or the reverse), constants whose comments contradict their values or each other (headroom, damp scales, tension scale, calibration gains), calibration constants tuned against a design that later changed, scratch buffers shared by two stages in the same chunk, sizes derived from one block-size constant and used with another, per-hit work that belongs in prepare, per-sample work that belongs per block, code that is only correct for the Membrane body. Your value is noticing what category-driven review misses.`,
  },
  {
    key: 'gestalt-controller',
    prompt: `You are the GESTALT reviewer for the UI side: read plugins/membrum/src/controller/controller.h and controller.cpp END TO END, then controller_state.cpp, controller_state_codec.cpp, controller_view_sync.cpp and controller_param_format.cpp, then the files under plugins/membrum/src/ui/, and flag anything that does not make sense — repeated registration boilerplate with one copy subtly different, string-list parameters whose entries disagree with the DSP enums, handlers that update a view but not the parameter (or the reverse), lifecycle code that assumes the editor exists, state kept in the controller that the processor never learns about (and is lost on reload) or the reverse, a second copy of the kit kept on the controller side that can disagree with the processor, dead handlers and members, misleading comments, a 1700-line file whose parts belong elsewhere. Your value is noticing what category-driven review misses.`,
  },
]

phase('Find')
log(`Fanning out ${DIMENSIONS.length} Opus finders...`)
const found = await parallel(
  DIMENSIONS.map(d => () =>
    agent(`${CONTEXT}\n\nINVENTORY CROSS-REFERENCES (mechanical mismatches; confirm in code before reporting):\n${INVENTORY_DIGEST}\n\nSUBSYSTEM MAPS from the understanding phase (leads in ODDITIES are gold):\n${MAP_DIGEST}\n\nYOUR DIMENSION: ${d.prompt}\n\nRead the actual code before reporting anything. Quality over quantity: a finding without a concrete file:line you personally read is worthless; one real bug beats ten maybes. Do not report style nits unless they hide a defect. Severity: critical = wrong/unsafe audio, stuck or infinite notes, crash or data loss in normal use; high = audible defect or host-visible misbehaviour in common use; medium = edge-case defect or maintenance trap; low = quality.`, {
      label: `find:${d.key}`,
      phase: 'Find',
      schema: FINDING_SCHEMA,
      model: 'opus',
    }).then(r => (r ? r.findings.map(f => ({ ...f, dimension: d.key, source: 'static' })) : null)),
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
  'CONTEXT: is it intentional, guarded elsewhere (a clamp upstream, a reset in setActive, a test that pins it), unreachable in a real host, or already documented as a design decision in the specs (specs/135-membrum-drum-synth.md, specs/136..141-membrum-phase*/), the manual, a prior audit ruling or a code comment? Intended by design: modeInject ringing on, Friction sustaining without a note-off, maxPolyphony limited to [4,16].',
  'IMPACT: assume the defect is real — does it produce the claimed consequence at the claimed severity? Quantify (dB, cents, samples, milliseconds, which hosts/settings trigger it); downgrade or upgrade accordingly, and refute if the consequence cannot occur.',
]
// Physics findings (from the Research phase) need a literature check, not only a code check.
const PHYSICS_LENSES = [
  'CODE: re-read the cited code. Does it really implement the formula, constants or topology the finding attributes to it (the "claim as coded")? Refute if the reviewer misread the code, read a table that is not the one in use, or the code changed.',
  'LITERATURE: independently check the cited sources with web search and fetch. Do they say what the finding claims, for THIS physical system (ideal vs air-loaded membrane, free vs supported plate, string vs bar)? Refute if the sources are misquoted, apply to a different system, are only blogs or forums, or if the "correct form" is itself disputed in the literature.',
  'CONTEXT AND IMPACT: is the deviation a deliberate, documented simplification (the Unnatural Zone is non-physical by design; plugins/membrum/docs/correctness-audit.md and section 3 of plugins/membrum/AUDIT-signal-path-2026-06-07.md rule several simplifications acceptable; the phase research.md files record design choices)? Assume it is real: would correcting it be audible, and at the claimed severity? Refute if it is intentional and documented, or inaudible.',
]
// Verification is throttled while the measurement lane still runs: a full-width
// fan-out (N findings x 3 lenses) fills every agent slot and the lane's next
// step (tidy, portability) queues behind it for an hour (disrumpo-audit run
// wf_b866487e, 2026-11-05). Chunks of 4 findings = 12 agents leave slots for the
// lane; full width once it is done. (laneDone is declared with the args at the
// top so the lane's .then never hits a TDZ.)
async function verifyAll(list, tag, lenses = LENSES, what = 'this defect in Membrum') {
  const out = []
  for (let i = 0; i < list.length; ) {
    const size = laneDone ? list.length : 4
    const chunk = list.slice(i, i + size)
    i += chunk.length
    out.push(...(await verifyChunk(chunk, tag, lenses, what)))
  }
  return out
}
async function verifyChunk(list, tag, lenses, what) {
  return parallel(
    list.map(f => () =>
      parallel(
        lenses.map(lens => () =>
          agent(`${CONTEXT}\n\nA reviewer claims ${what}. Your job is to REFUTE it if you can. Lens: ${lens}\n\nFINDING:\n${JSON.stringify(f, null, 2)}\n\nRead ${f.file} around line ${f.line} AND every code path the claim depends on (callers, the shared DSP header it uses, the test that covers it). Default to refuted=true if you cannot positively confirm the defect from source, from the quoted measurement or from a source you opened yourself.`, {
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
log(`Verifying ${deduped.length} static findings (3 Opus lenses each) while the measurement lane and the research finish...`)
const [verifiedStaticRaw, lane, researchRaw] = await parallel([
  () => verifyAll(deduped, 'static'),
  () => lanePromise,
  () => researchPromise,
])
const verifiedStatic = (verifiedStaticRaw || []).filter(Boolean)
const research = researchRaw || { claims: [], failed: RESEARCH_TOPICS.map(t => t.key), extra_topics: [], error: 'research promise returned nothing' }
if (research.error) log(`WARNING: research threw: ${research.error}`)

// ---------------------------------------------------------------------------
// Measurement interpretation — turn lane output into findings, then verify them.
// ---------------------------------------------------------------------------
const laneErr = lane && lane.error
if (laneErr) log(`WARNING: measurement lane threw: ${laneErr}`)
const laneSteps = LANE_STEPS.filter(s => s.result)
const laneDigest = laneSteps
  .map(s => `## lane:${s.key}\n${JSON.stringify(s.result, null, 1)}`)
  .join('\n\n')
const laneNotRun = LANE_KEYS.filter(k => !laneSteps.find(s => s.key === k))
if (laneNotRun.length) log(`Measurement steps not run: ${laneNotRun.join(', ')}`)

let measured = []
if (laneSteps.length) {
  phase('Find')
  const interp = await agent(`${CONTEXT}\n\nSUBSYSTEM MAPS:\n${MAP_DIGEST}\n\nMEASUREMENT LANE RESULTS (facts produced by running the code; the probe sources are ${OUT_DIR}/probes/audit_probes.cpp.txt and ${OUT_DIR}/probes/audit_quality_probes.cpp.txt if they ran, with their CSV tables beside them):\n${laneDigest}\n\nTurn these into FINDINGS. Rules: every test-red, pluginval failure, build warning, portability error, probe "bad" and probe "suspicious" measurement becomes a finding IF you can localise it — read the implicated code and cite file:line (for a probe, the DSP code responsible, plus the probe name in evidence). Before blaming the plugin for a probe result, read the probe's own code for that measurement: a harness mistake (wrong parameter ID, a context in which the parameter cannot matter, a note never released, a kit not loaded) is NOT a finding. KIT-QUALITY results: one finding per dead or weak parameter (category measured-dead-parameter: trace the parameter from its PadConfig field to the DSP and name the line where its effect is lost; dead = high, weak = medium or low); near-duplicate pads grouped as ONE finding per kit (category measured-kit-quality, list the pairs) and one finding per pair of kits that sound alike, each saying whether the cause is the kit DATA (tools/membrum_preset_generator.cpp: quote the values that are equal) or the ENGINE (a parameter that differs between the pads but has no audible effect: link the dead-parameter finding); pads that are silent, ride the limiter, carry DC or ring on without an intended reason are findings of their own; flat velocity response is a finding per kit. Expected sustain (modeInject above 0, Friction, Feedback) is not a finding. Clang-tidy clusters: report only clusters that indicate a real defect (bugprone-*, clang-analyzer-*, performance-* that touches the audio thread, cert-*), one finding per cluster, severity low unless the check proves a bug; style-only clusters are NOT findings (the report lists them from the lane summary). Lints: one finding per item. category must start with "measured-" (measured-test, measured-pluginval, measured-probe, measured-dead-parameter, measured-kit-quality, measured-tidy, measured-lint, measured-portability, measured-build). Evidence must quote the number or message verbatim.`, {
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
      const dup = verifiedStatic.find(g => isDup(g, f))
      if (dup) dup.corroborated_by_measurement = (dup.corroborated_by_measurement || []).concat(f.evidence)
      return !dup
    })
    log(`${fresh.length} measured findings -> ${measured.length} new (others corroborate static findings); static list was ${before}`)
  }
}
phase('Verify')
const verifiedMeasured = measured.length ? (await verifyAll(measured, 'measured')).filter(Boolean) : []

// ---------------------------------------------------------------------------
// Physics findings — every literature verdict that is not "confirmed" or
// "nuanced" becomes a finding and goes through the physics lenses.
// ---------------------------------------------------------------------------
const researchClaims = research.claims || []
const physicsRaw = researchClaims
  .filter(c => ['contradicted', 'oversimplified', 'unsupported'].includes(c.verdict) && c.severity !== 'none')
  .map(c => ({
    title: `Physics (${c.verdict}): ${c.title}`,
    severity: c.severity,
    category: `physics-${c.verdict}`,
    file: c.file,
    line: c.line,
    problem: c.claim_as_coded,
    why_wrong: `${c.correct_form} — SOURCES (evidence quality ${c.evidence_quality}): ${c.sources.map(s => `${s.title} [${s.type}] ${s.url}: ${s.says}`).join(' | ')}`,
    consequence: c.implication,
    suggested_fix: c.suggested_fix,
    evidence: `Prior audit ruling: ${c.prior_verdict}`,
    dimension: `research:${c.topic}`,
    source: 'research',
  }))
const physicsList = []
dedupInto(physicsList, physicsRaw, 'research')
const physicsNew = physicsList.filter(f => {
  const dup = verifiedStatic.find(g => isDup(g, f)) || verifiedMeasured.find(g => isDup(g, f))
  if (dup) dup.corroborated_by_research = (dup.corroborated_by_research || []).concat(`${f.title}: ${f.why_wrong}`)
  return !dup
})
log(`Research: ${researchClaims.length} physical claims checked, ${physicsRaw.length} not confirmed -> ${physicsNew.length} physics findings to verify`)
const verifiedPhysics = physicsNew.length
  ? (await verifyAll(physicsNew, 'research', PHYSICS_LENSES, 'that Membrum\'s physical model deviates from the published literature')).filter(Boolean)
  : []

// Finding ids are MB-nnn; the prior audits' ids (Finding n, H-n / M-n / L-n / N-1, R-2xx) stay distinct.
let verified = [...verifiedStatic, ...verifiedMeasured, ...verifiedPhysics]
verified.forEach((f, i) => { f.id = `MB-${String(i + 1).padStart(3, '0')}` })
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
const critique = await agent(`${CONTEXT}\n\nYou are the COMPLETENESS CRITIC of this audit. Below is everything the audit produced so far. Ask: what is missing? Which subsystem, hazard class or file got no finder attention (compare the dimension list and the maps against the file list under plugins/membrum/src — every exciter, body, mapper and view included — and the ~26 KrateDSP headers Membrum includes)? Which measurement did not run or did not cover a hazard the static finders flagged? Which physical claim in the code or the manual was never checked against the literature (kind unresearched-physics)? Which REFUTED finding was refuted for a weak reason (a skeptic citing intent without a code line, refuting on severity rather than existence, or a literature skeptic who opened no source)? Which CONFIRMED finding has verifier notes that actually disagree with each other?\n\nDIMENSIONS RUN: ${DIMENSIONS.map(d => d.key).join(', ')}${failedFinders.length ? ` (FAILED: ${failedFinders.join(', ')})` : ''}\nMEASUREMENT STEPS NOT RUN: ${laneNotRun.join(', ') || 'none'}\nRESEARCH TOPICS RUN: ${research.skipped ? 'none (skipped by args)' : RESEARCH_TOPICS.map(t => t.key).concat((research.extra_topics || []).map(k => `extra:${k}`)).join(', ')}${(research.failed || []).length ? ` (FAILED: ${research.failed.join(', ')})` : ''}\n\nINVENTORY:\n${INVENTORY_DIGEST}\n\nMAPS:\n${MAP_DIGEST}\n\nLANE SUMMARIES:\n${laneSteps.map(s => `- ${s.key}: ${s.result.summary}`).join('\n')}\n\nPHYSICAL CLAIMS CHECKED (verdict, title, file:line):\n${researchClaims.map(c => `- [${c.verdict}/${c.severity}] ${c.title} — ${c.file}:${c.line}`).join('\n')}\n\nALL FINDINGS WITH VERDICTS (id, verdict, severity, title, file:line, verifier notes):\n${verified.map(f => `${f.id} [${f.verdict}/${f.final_severity}] ${f.title} — ${f.file}:${f.line}\n  notes: ${(f.verifier_notes || []).map(n => n.slice(0, 300)).join(' || ')}`).join('\n')}\n\nSpot-check the code where you doubt a verdict. Return up to 6 follow-up finder prompts, each naming concrete files and one hazard class (a prompt may ask for a literature check of one named physical claim), and the IDs of refuted findings that deserve a tie-break.`, {
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
          agent(`${CONTEXT}\n\nINVENTORY:\n${INVENTORY_DIGEST}\n\nSUBSYSTEM MAPS:\n${MAP_DIGEST}\n\nALREADY REPORTED (do not repeat these; find what they missed):\n${verified.map(f => `- ${f.title} — ${f.file}:${f.line}`).join('\n')}\n\nYOUR FOLLOW-UP DIMENSION (from the completeness critic): ${d.prompt}\n\nRead the actual code before reporting anything; cite file:line you personally read.\n\nIF the critic asked for a LITERATURE check: read the code for the actual values, open authoritative sources with web search and fetch (textbooks and peer-reviewed papers are ground truth; blogs and forums never suffice alone), and put the citations (authors, title, year, URL, what the source establishes) in why_wrong; category physics-contradicted / physics-oversimplified / physics-unsupported.\n\nIF, AND ONLY IF, the critic asked for a MEASUREMENT you cannot make by reading: you may add ONE temporary hidden [.][audit-probe] TEST_CASE file under plugins/membrum/tests/audit/ (+ its line in plugins/membrum/tests/CMakeLists.txt), build with  ${CMAKE} --build ${BUILD_DIR} --config Release --target membrum_tests -- -m:2  (TWO MSBuild nodes, never more: another agent may be building too), run it via  node tools/run-gate.js --share 4 --filter '[audit-probe]' membrum_tests , log under ${LOG_DIR}/, copy the TU (with a .cpp.txt suffix) and its results to ${OUT_DIR}/probes/ (a new file name: never overwrite audit_probes.cpp.txt or audit_quality_probes.cpp.txt), then  git checkout -- plugins/membrum/tests/CMakeLists.txt , delete tests/audit/, and confirm  git status --porcelain plugins/membrum  is empty. Edit nothing else.`, {
            label: `find2:${d.key}`,
            phase: 'Find',
            schema: FINDING_SCHEMA,
            model: 'opus',
          }).then(r => (r ? r.findings.map(f => ({ ...f, dimension: `followup:${d.key}`, source: 'followup' })) : null)),
        ),
      ),
    () =>
      parallel(
        critique.rejudge_ids
          .map(id => verified.find(f => f.id === id && f.verdict === 'REFUTED'))
          .filter(Boolean)
          .map(f => () =>
            agent(`${CONTEXT}\n\nTIE-BREAK. Three skeptics refuted this finding; the completeness critic doubts the refutation. Read the code yourself (for a physics finding, also open the cited sources) and rule. Default to refuted=true only if the code or the literature positively shows the defect cannot occur.\n\nFINDING:\n${JSON.stringify(f, null, 2)}`, {
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
  const novelPhysics = novel.filter(f => String(f.category).startsWith('physics-'))
  const novelCode = novel.filter(f => !String(f.category).startsWith('physics-'))
  const verifiedFollow = [
    ...(novelCode.length ? await verifyAll(novelCode, 'followup') : []),
    ...(novelPhysics.length ? await verifyAll(novelPhysics, 'followup', PHYSICS_LENSES, 'that Membrum\'s physical model deviates from the published literature') : []),
  ].filter(Boolean)
  verifiedFollow.forEach((f, i) => { f.id = `MB-${String(verified.length + i + 1).padStart(3, '0')}` })
  verified = verified.concat(verifiedFollow)
  followupAdded = verifiedFollow.length
}

const surviving = verified.filter(f => f.verdict === 'CONFIRMED' || f.verdict === 'PLAUSIBLE')
const refuted = verified.filter(f => f.verdict === 'REFUTED')
const unverified = verified.filter(f => f.verdict === 'UNVERIFIED')
log(`Final: ${surviving.length} surviving (${tally(surviving).confirmed} CONFIRMED, ${tally(surviving).plausible} PLAUSIBLE), ${refuted.length} refuted, ${unverified.length} unverified`)

// ---------------------------------------------------------------------------
// Synthesize — Fable writes report.md and fix-plan.md to specs/membrum-audit/.
// ---------------------------------------------------------------------------
phase('Synthesize')
const sevOrder = { critical: 0, high: 1, medium: 2, low: 3 }
const verdictOrder = f => (f.verdict === 'CONFIRMED' ? 0 : 1)
const sortedSurviving = [...surviving].sort((a, b) => (sevOrder[a.final_severity] - sevOrder[b.final_severity]) || (verdictOrder(a) - verdictOrder(b)) || String(a.id).localeCompare(String(b.id)))
const laneForReport = laneSteps.map(s => ({ key: s.key, ...s.result }))
const probeBaseline = {
  probes: (laneSteps.find(s => s.key === 'probes') || {}).result || null,
  quality: (laneSteps.find(s => s.key === 'quality') || {}).result || null,
}

// The full finding data (~770k tokens with verifier notes) fits no single prompt, and no single agent can write it
// out, so both documents are written as numbered part files by parallel agents and then joined in file-name order.
const REPORT_PARTS = `${OUT_DIR}/report-parts`
const PLAN_PARTS = `${OUT_DIR}/fix-plan-parts`
const capNotes = (f, n) => ({ ...f, verifier_notes: (f.verifier_notes || []).map(s => (String(s).length > n ? `${String(s).slice(0, n)} [...]` : String(s))) })
const digestOf = f => ({ id: f.id, title: f.title, severity: f.final_severity, verdict: f.verdict, category: f.category, dimension: f.dimension, at: `${f.file}:${f.line}`, fix: String(f.suggested_fix).slice(0, 500) })
const sliceBy = (list, size) => { const out = []; for (let i = 0; i < list.length; i += size) out.push(list.slice(i, i + size)); return out }
const pad2 = n => String(n).padStart(2, '0')
const sevName = s => s.charAt(0).toUpperCase() + s.slice(1)
const sevCount = s => sortedSurviving.filter(f => f.final_severity === s).length
const PART_RULES = `Write ONLY the part file named below, with the Write tool (it creates the directory). It is one piece of a larger markdown document that is joined from parts in file-name order, so: no document title, no table of contents, no closing remarks, and use exactly the heading levels you are told. If the content is long, write the file in several steps (Write the first portion, then append the rest with Edit) rather than shortening it. Do not touch any other file.`
const STATS_LINE = `raw static findings ${rawStatic.length}, deduped ${deduped.length}, measured ${measured.length}, physics ${physicsNew.length}, follow-up ${followupAdded}, confirmed ${tally(surviving).confirmed}, plausible ${tally(surviving).plausible}, refuted ${refuted.length}, unverified ${unverified.length}, tie-breaks upheld ${rejudged}, finders failed: ${failedFinders.join(', ') || 'none'}, measurement steps not run: ${laneNotRun.join(', ') || 'none'}, research topics failed: ${(research.failed || []).join(', ') || 'none'}${research.skipped ? ' (research skipped by args)' : ''}`
const FINDING_FORMAT = `Each finding: heading "#### <id> — <title>", a line with severity / verdict / category / dimension(s) incl. also_flagged_by, corroborated_by_measurement and corroborated_by_research when present, then file:line (as a markdown link relative to the repo root), Problem, Why wrong, Consequence, Evidence, Suggested fix, and "Verifier notes" as a short bullet list (compress each note to its key sentence, keep the file:line citations and source citations). Do NOT drop or merge findings; do not soften verdicts; keep the order given.`

// ---- Report parts ----
const reportJobs = []
reportJobs.push({
  file: '00-summary.md', label: 'synthesize:report-summary', model: 'fable',
  prompt: `You are writing the OPENING of the Membrum AUDIT REPORT. Audience: the repo owner and a coding agent that will fix things; it must stand alone (no reference to "this session").\n\nDATE: ${DATE}. Plugin version: read plugins/membrum/version.json. HEAD: run  git rev-parse --short HEAD .\n\nCONTENT:\n1. "# Membrum audit — ${DATE}", then date, version, commit, and "## 1. Executive summary": one paragraph on what was audited and how (${DIMENSIONS.length} static dimensions + ${laneSteps.length} measurement steps + ${researchClaims.length} physical claims checked against the literature, 3-lens adversarial verification, critic round), the headline defects (the critical and high findings below, grouped by theme, with their ids), and one line on how this audit relates to the prior ones (docs/correctness-audit.md, AUDIT-signal-path-2026-06-07.md, the Ruinae audit's R-2xx items; ids here are MB-nnn). Then a short list of the report's sections: 2 Statistics, 3 Measurement lane, 4 Kit quality, 5 Physical-model review, 6 Findings, 7 Prior-audit status, 8 Inventory cross-reference, Appendices A-C. The fix plan is fix-plan.md next to this file.\n2. "## 2. Statistics": a table of: ${STATS_LINE}; plus a table of surviving findings by severity (critical ${sevCount('critical')}, high ${sevCount('high')}, medium ${sevCount('medium')}, low ${sevCount('low')}) and one by category (count them from the digest).\n\nDIGEST OF EVERY SURVIVING FINDING:\n${JSON.stringify(sortedSurviving.map(digestOf), null, 1)}`,
})
reportJobs.push({
  file: '10-measurement.md', label: 'synthesize:report-measurement', model: 'opus',
  prompt: `You are writing sections 3 and 4 of the Membrum AUDIT REPORT.\n\n"## 3. Measurement lane": one "### <step>" subsection per step with its summary verbatim, verdict line, log path, and a table of its items (tidy clusters as a table of check/count/files). Probe measurements as a table (name, setup, metric, value, threshold, verdict, note) — ALL of them, ok ones included, because they are the baseline for after the fixes. Probe sources: ${OUT_DIR}/probes/audit_probes.cpp.txt and audit_quality_probes.cpp.txt.\n\n"## 4. Kit quality" (from the quality step): a table of the 20 kits (enabled pads, flagged pads, near-duplicate pairs, nearest other kit and its distance, velocity response), the cross-kit spread per reference note, and the parameter-audibility table (every suspicious and bad parameter with its numbers, plus the ok count); link the CSV files under ${OUT_DIR}/probes/ (list that directory) for the full per-pad and per-parameter rows. State the definitions (features, distance, thresholds) the probe used; read ${OUT_DIR}/probes/audit_quality_probes.cpp.txt for them when the summary does not say.\n\nLANE:\n${JSON.stringify(laneForReport, null, 1)}`,
})
const physicsFindings = sortedSurviving.filter(f => String(f.dimension).startsWith('research:') || f.source === 'research').map(f => ({ id: f.id, title: f.title, at: `${f.file}:${f.line}` }))
const researchHalves = sliceBy(researchClaims, Math.max(1, Math.ceil(researchClaims.length / 2)))
researchHalves.forEach((claims, i) => {
  const from = i * researchHalves[0].length + 1
  reportJobs.push({
    file: `2${i}-physics.md`, label: `synthesize:report-physics-${i + 1}`, model: 'opus',
    prompt: `You are writing ${i === 0 ? 'the start of section 5' : 'a continuation of section 5'} of the Membrum AUDIT REPORT: the physical-model review.\n\n${i === 0 ? 'Start with "## 5. Physical-model review" and one sentence on method (each claim in the code checked against textbooks and peer-reviewed papers; confirmed ones listed so nobody re-checks them). Then ' : 'Start with '}"### 5.${i + 1} Claims ${from}-${from + claims.length - 1}": a table of EVERY claim below (title, file:line, verdict, evidence quality, correct form in one line, key source, severity, MB-id when it became a finding — match by title and file against the PHYSICS FINDINGS list, "-" otherwise), then "#### Sources for claims ${from}-${from + claims.length - 1}": per claim, its full source list (authors, title, year, type, URL, what it establishes).\n\nPHYSICS FINDINGS (id, title, location):\n${JSON.stringify(physicsFindings, null, 1)}\n\nCLAIMS:\n${JSON.stringify(claims, null, 1)}`,
  })
})
const findingChunks = sliceBy(sortedSurviving, 20)
findingChunks.forEach((chunk, i) => {
  const groupStarts = chunk.filter((f, j) => (j === 0 ? (i === 0 || findingChunks[i - 1][findingChunks[i - 1].length - 1].final_severity !== f.final_severity) : chunk[j - 1].final_severity !== f.final_severity))
  reportJobs.push({
    file: `3${pad2(i)}-findings.md`, label: `synthesize:report-findings-${i + 1}`, model: 'opus',
    prompt: `You are writing part ${i + 1} of ${findingChunks.length} of section 6 (Findings) of the Membrum AUDIT REPORT. Findings are grouped critical -> high -> medium -> low, CONFIRMED before PLAUSIBLE within a group; the input below is already in that order.\n\nHEADINGS: ${i === 0 ? 'start with "## 6. Findings". ' : ''}${groupStarts.length ? groupStarts.map(f => `immediately before ${f.id} write "### ${sevName(f.final_severity)} (${sevCount(f.final_severity)})"`).join('; ') : 'no group heading in this part (it continues the previous group)'}. Nothing else above the "####" finding headings.\n\n${FINDING_FORMAT}\n\nFINDINGS (${chunk.length}):\n${JSON.stringify(chunk.map(f => capNotes(f, 2500)), null, 1)}`,
  })
})
const priorFindings = sortedSurviving.filter(f => f.dimension === 'prior-audit-regressions' || (f.also_flagged_by || []).includes('prior-audit-regressions'))
reportJobs.push({
  file: '70-prior-audits.md', label: 'synthesize:report-prior', model: 'opus',
  prompt: `You are writing section 7 of the Membrum AUDIT REPORT: "## 7. Prior-audit status". A table of the prior finding ids (docs/correctness-audit.md, AUDIT-signal-path-2026-06-07.md, the Ruinae audit's R-2xx Membrum items) that this audit found still present, still open in the tracker, or partially fixed, with the MB-id that tracks each. Work from the findings below; open the prior documents to get the prior ids and titles right.\n\nFINDINGS FROM THE PRIOR-AUDIT-REGRESSIONS DIMENSION:\n${JSON.stringify(priorFindings.map(f => capNotes(f, 1500)), null, 1)}`,
})
reportJobs.push({
  file: '80-inventories.md', label: 'synthesize:report-inventories', model: 'sonnet',
  prompt: `You are writing section 8 of the Membrum AUDIT REPORT: "## 8. Inventory cross-reference". All the inventory cross-reference tables below (one "### " subsection each) and their full mismatch lists. Keep every row.\n\nINVENTORIES:\n${JSON.stringify(goodInv, null, 1)}`,
})
reportJobs.push({
  file: '90-appendices.md', label: 'synthesize:report-appendices', model: 'sonnet',
  prompt: `You are writing the appendices of the Membrum AUDIT REPORT.\n"## Appendix A — Refuted findings": a table of id, title, file:line, one-line refutation reason each (so nobody re-finds them; take the reason from the verifier notes).\n"## Appendix B — Unverified findings": a table, or "None." when empty.\n"## Appendix C — Critic gaps": the critic's gap list verbatim (kind, detail).\n\nREFUTED:\n${JSON.stringify(refuted.map(f => ({ id: f.id, title: f.title, file: f.file, line: f.line, verifier_notes: capNotes(f, 800).verifier_notes })), null, 1)}\n\nUNVERIFIED:\n${JSON.stringify(unverified.map(f => ({ id: f.id, title: f.title, file: f.file, line: f.line })), null, 1)}\n\nCRITIC GAPS:\n${JSON.stringify(critique ? critique.gaps : [], null, 1)}`,
})
const runPart = (dir, j) =>
  agent(`${CONTEXT}\n\n${PART_RULES}\nPART FILE: ${dir}/${j.file}\n\n${j.prompt}\n\nReturn the path and a summary.`, { label: j.label, phase: 'Synthesize', schema: DOC_SCHEMA, model: j.model })

// ---- Fix-plan skeleton (one planner over the digest) ----
const PLAN_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['phases', 'tasks'],
  properties: {
    phases: {
      type: 'array',
      items: { type: 'object', additionalProperties: false, required: ['n', 'name', 'goal'], properties: { n: { type: 'integer' }, name: { type: 'string' }, goal: { type: 'string', description: 'one sentence' } } },
    },
    tasks: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['task_id', 'phase', 'title', 'finding_ids', 'prerequisites', 'flags', 'estimate_min'],
        properties: {
          task_id: { type: 'string', description: 'T<phase>.<n>' },
          phase: { type: 'integer' },
          title: { type: 'string' },
          finding_ids: { type: 'array', items: { type: 'string' } },
          prerequisites: { type: 'array', items: { type: 'string' }, description: 'task ids that must land first' },
          flags: { type: 'array', items: { type: 'string', enum: ['CROSS-PLUGIN IMPACT', 'STATE-COMPAT', 'PRESET-IMPACT', 'PHYSICS', 'UI', 'THREADING', 'MACOS', 'VERIFY-FIRST'] } },
          estimate_min: { type: 'integer' },
        },
      },
    },
  },
}
const PLAN_RULES = `PLAN REQUIREMENTS:\n- Phases in this order: (1) critical/high correctness and audio defects (crashes, threading, wrong or unsafe audio, timing first), (2) medium defects, (3) measured defects not already covered (test reds, pluginval, portability, tidy clusters that prove bugs), (4) physical-model corrections (physics findings) — they change how every kit sounds, so they come BEFORE any kit data is touched, (5) dead and weak parameters, then kit data (near-duplicate pads and kits) through tools/membrum_preset_generator.cpp — never retune a kit against a parameter or a model that an earlier task still has to fix, (6) duplication / parallel-path / spec-conformance / anti-pattern cleanups, (7) test-gap fills, (8) promote the useful audit probes from ${OUT_DIR}/probes/audit_probes.cpp.txt and audit_quality_probes.cpp.txt into real regression tests (un-hidden tags, real assertions with the thresholds the report names, placed under plugins/membrum/tests/unit/ or integration/, registered in plugins/membrum/tests/CMakeLists.txt; a case that renders more than ~15 s of toolchain-independent audio gets the [long] tag, a wall-clock assertion gets [wallclock]). Low findings go to the phase their category belongs to, after the medium ones. Within a phase order so no task depends on a later one; name hard prerequisites. Several findings with one root cause or one file region share a task.\n- Every task follows this repo's mandatory workflow:\n  1. write the failing test (test file to create/extend, exact TEST_CASE name and tags, what it asserts and with which helper from tests/test_helpers/); for a defect in a shared DSP header the test goes in dsp/tests/unit/<layer>/ and its dsp_<layer>_tests target\n  2. implement the fix (exact files, functions, before/after)\n  3. build, capped: "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests -- -m:2   (add Membrum when plugin sources changed; add the dsp_<layer>_tests target when a dsp/ file changed)\n  4. run SHARDED, never serial: node tools/run-gate.js --share 4 --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' membrum_tests   — and when a dsp/include or plugins/shared file was MODIFIED, first run  node tools/header-consumers.js --tag '[membrum]'  (Membrum's cases carry no common tag, so membrum_tests itself is in its list: that is expected) and add the executables its last line names to the same run-gate command with ~[long] appended to the filter, building them first\n  5. pluginval (tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"; exit code is the verdict) when plugin sources changed, clang-tidy (./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja -Jobs 4) before the commit, ONE heavy job at a time\n  6. commit (git add, then git commit -F <message file>; never --amend; never push)\n- Flags: CROSS-PLUGIN IMPACT = touches dsp/include/krate/dsp/ or plugins/shared/ (name the other consumers via grep -rl of the header under plugins/; a shared root-cause fix is in scope and gates every consumer). STATE-COMPAT = changes the saved-state layout: move kCurrentStateVersion, kBlobVersion and kPadBlobVersion together (static_asserts pin them), update the constants in tools/membrum_preset_generator.cpp and regenerate the 20 factory kits, account for plugins/membrum/tests/golden/*.bin, and — if the report established that a released build wrote another blob version — add a read path for that version instead of rejecting it; never swap a parameter type at an existing ID, never renumber a per-pad offset or a global ID. PRESET-IMPACT = changes how a factory kit sounds: regenerate the kits with the generator (never hand-edit a .vstpreset), install through the build target (never hand-copy), keep the four kit categories (Acoustic, Electronic, Percussive, Unnatural) and the ring / range constraints (maxPolyphony in [4,16], modeInject above 0 rings on, Friction sustains), re-run the kit-quality probe and list the before/after numbers of every affected pad; a change the user would hear as a different kit needs the user's ruling before the commit. PHYSICS = new values carry their source (authors, title, year, equation or table) in a code comment AND in a test that checks the coded values against the published ones; also PRESET-IMPACT. UI = never remove an interaction to fix a bug; bulk editor.uidesc changes (more than about three controls) go through an XSLT stylesheet, not string replacement; after an editor change rerun  node tools/gen-screenshots.js membrum . THREADING = state applied from the message thread: a deterministic interleaving test plus a ThreadSanitizer run where the repo supports it; hand state to the audio thread through a transfer object or an atomic flag, never a lock. MACOS = fast-math, Apple globals, finiteness checks, IEEE flag sets: run tools/macos-fastmath-repro.sh, tools/lint-apple-globals.js, tools/lint-ieee-flag-sets.js, tools/check-portability.js before pushing. VERIFY-FIRST = a PLAUSIBLE finding (one skeptic refuted it, or a tie-break upheld it): the failing test doubles as the verification; if it passes on current code the task ends with the finding marked refuted and the test kept.`
const reportPartsP = parallel(reportJobs.map(j => () => runPart(REPORT_PARTS, j)))
const planSkeleton = await agent(`${CONTEXT}\n\nYou are the PLANNER of the Membrum audit FIX PLAN. It will be executed by a coding agent working task-by-task with NO memory of this audit. Detail writers will expand each of your tasks afterwards from the full finding data; your job is the structure, and two part files.\n\n1. Return phases and tasks (schema). Every finding id in the digest belongs to exactly one task. Task ids are T<phase>.<n> (T1.1, T1.2, ...), numbered in execution order.\n2. ${PART_RULES}\nPART FILE A: ${PLAN_PARTS}/00-head.md — "# Membrum audit fix plan — ${DATE}", a one-paragraph scope (companion report: ${OUT_DIR}/report.md), "## How to execute a task" (the six-step workflow below, ONCE, with the exact commands), "## Flags" (each flag's meaning and obligations below, ONCE), and "## Phases" (a table: phase, name, goal, task count, total estimate). Detail writers will refer to these sections instead of repeating them.\nPART FILE B: ${PLAN_PARTS}/99-checklist.md — "## Completion checklist" mirroring the repo's Completion Honesty rules: each fix verified against the original finding with file:line evidence and the test's actual output, no relaxed thresholds, no quietly removed features, probes re-run and compared with the measurement baseline.\n\n${PLAN_RULES}\n\nMEASUREMENT BASELINE (summaries; full tables in ${OUT_DIR}/probes/*.csv):\n${JSON.stringify({ probes: (probeBaseline.probes || {}).summary || null, quality: (probeBaseline.quality || {}).summary || null }, null, 1)}\n\nDIGEST OF EVERY VERIFIED FINDING (CONFIRMED = no skeptic refuted; PLAUSIBLE = one did, or a tie-break upheld — flag those VERIFY-FIRST):\n${JSON.stringify(sortedSurviving.map(digestOf), null, 1)}`, {
  label: 'synthesize:plan-skeleton',
  phase: 'Synthesize',
  schema: PLAN_SCHEMA,
  model: 'fable',
})
const survivingIds = new Set(sortedSurviving.map(f => f.id))
let planPhases = (planSkeleton && planSkeleton.phases) || []
let planTasks = ((planSkeleton && planSkeleton.tasks) || []).map(t => ({ ...t, finding_ids: t.finding_ids.filter(id => survivingIds.has(id)) })).filter(t => t.finding_ids.length)
const assignedIds = new Set(planTasks.flatMap(t => t.finding_ids))
const unassignedFindings = sortedSurviving.filter(f => !assignedIds.has(f.id))
if (!planSkeleton) log('WARNING: plan skeleton agent failed; every finding becomes its own task in phase 9')
if (unassignedFindings.length) {
  log(`WARNING: ${unassignedFindings.length} findings were not assigned by the planner; they become phase 9 tasks`)
  planPhases = planPhases.concat({ n: 9, name: 'Findings the planner left unassigned', goal: 'One task per finding, in severity order.' })
  planTasks = planTasks.concat(unassignedFindings.map((f, i) => ({ task_id: `T9.${i + 1}`, phase: 9, title: f.title, finding_ids: [f.id], prerequisites: [], flags: f.verdict === 'PLAUSIBLE' ? ['VERIFY-FIRST'] : [], estimate_min: 30 })))
}
const taskNum = t => parseInt(String(t.task_id).split('.')[1], 10) || 0
planTasks.sort((a, b) => a.phase - b.phase || taskNum(a) - taskNum(b))
const taskBatches = []
for (const t of planTasks) {
  const last = taskBatches[taskBatches.length - 1]
  if (last && last.reduce((n, x) => n + x.finding_ids.length, 0) + t.finding_ids.length <= 18) last.push(t)
  else taskBatches.push([t])
}
const findingById = new Map(sortedSurviving.map(f => [f.id, f]))
const firstTaskOfPhase = new Map()
for (const t of planTasks) if (!firstTaskOfPhase.has(t.phase)) firstTaskOfPhase.set(t.phase, t.task_id)
const planJobs = taskBatches.map((batch, i) => {
  const phaseHeads = batch.filter(t => firstTaskOfPhase.get(t.phase) === t.task_id).map(t => planPhases.find(p => p.n === t.phase) || { n: t.phase, name: `Phase ${t.phase}`, goal: '' })
  return {
    file: `2${pad2(i)}-tasks.md`, label: `synthesize:plan-tasks-${i + 1}`, model: 'opus',
    prompt: `You are writing part ${i + 1} of ${taskBatches.length} of the task list of the Membrum audit FIX PLAN. A coding agent with NO memory of this audit executes it task by task, so every task must be self-contained: never say "fix the issue" — spell out the exact edit intent, the file:line anchors (re-read the code to make sure every anchor is right), the before/after behaviour and the acceptance check. First read ${PLAN_PARTS}/00-head.md: it defines the standard six-step workflow and the flags; refer to them, do not repeat them. The companion report (${OUT_DIR}/report.md) carries the full finding prose; reference finding ids, do not duplicate it.\n\nHEADINGS: ${phaseHeads.length ? phaseHeads.map(p => `immediately before the first task of phase ${p.n} write "## Phase ${p.n} — ${p.name}" and the line "${p.goal}"`).join('; ') : 'no phase heading in this part (it continues the previous phase)'}. Each task: "### <task id> — <title>", then: Findings (ids with severity and verdict), Flags, Prerequisites, Estimate (minutes), "1. Failing test" (file, exact TEST_CASE name and tags, what it asserts, which helper from tests/test_helpers/), "2. Fix" (exact files and functions, before/after), "3. Build and gate" (only what differs from the standard workflow: extra targets, header-consumers executables for CROSS-PLUGIN tasks, kit regeneration for PRESET-IMPACT), "4. Acceptance" (the measurable check, with the baseline number where a probe measured one). Keep the tasks in the order given, with the given ids, titles and flags (add a flag only if the code proves it applies, and say why).\n\nTASKS:\n${JSON.stringify(batch, null, 1)}\n\nFINDINGS THESE TASKS COVER:\n${JSON.stringify(batch.flatMap(t => t.finding_ids).map(id => capNotes(findingById.get(id), 1500)), null, 1)}`,
  }
})
const planPartsR = await parallel(planJobs.map(j => () => runPart(PLAN_PARTS, j)))
const reportPartsR = await reportPartsP

// ---- Join the parts ----
const missingParts = (jobs, results) => jobs.filter((j, i) => !results[i]).map(j => j.file)
const findingsTable = ['## Findings to tasks', '', '| Finding | Title | Severity | Verdict | Task | Phase |', '|---|---|---|---|---|---|']
  .concat(sortedSurviving.map(f => {
    const t = planTasks.find(x => x.finding_ids.includes(f.id))
    return `| ${f.id} | ${String(f.title).replace(/\|/g, '\\|').replace(/\n/g, ' ')} | ${f.final_severity} | ${f.verdict} | ${t ? t.task_id : '-'} | ${t ? t.phase : '-'} |`
  }))
  .join('\n')
const JOIN_RULES = (dir, out, headingRe, ids) => `Join the part files: write a small Node.js script (Write tool) to f:/tmp/membrum-audit/join-parts.js that reads every .md file in ${dir} in file-name order, trims each, joins them separated by one blank line, writes the result to ${out}, and prints the part names it joined. Run it with node. Then write a second small Node.js script that reads ${out}, collects every heading matching the regular expression ${headingRe}, and reports any expected id that appears zero times or more than once; the expected ids are: ${ids.join(' ')}. If the check is clean, delete the directory ${dir} (rm -rf) and confirm with  git status --porcelain ${OUT_DIR}  that it is gone. If the check is not clean, keep ${dir} and report the missing and duplicated ids. Edit nothing else. Return the path and a summary with the part count, the id check result and the byte size of ${out}.`
const [report, plan] = await parallel([
  () => {
    const missing = missingParts(reportJobs, reportPartsR)
    return agent(`${CONTEXT}\n\nYou are ASSEMBLING the Membrum AUDIT REPORT from part files written by other agents.\n${missing.length ? `These parts failed to be written; FIRST create each one with the Write tool, containing only the line "> PART MISSING: <file name> — the writer agent failed; rerun the synthesis for it.": ${missing.map(f => `${REPORT_PARTS}/${f}`).join(', ')}\n` : ''}${JOIN_RULES(REPORT_PARTS, `${OUT_DIR}/report.md`, '^#### (MB-\\d+) ', sortedSurviving.map(f => f.id))}`, {
      label: 'synthesize:report-join',
      phase: 'Synthesize',
      schema: DOC_SCHEMA,
      model: 'sonnet',
    })
  },
  () => {
    const missing = missingParts(planJobs, planPartsR).concat(planSkeleton ? [] : ['00-head.md', '99-checklist.md'])
    return agent(`${CONTEXT}\n\nYou are ASSEMBLING the Membrum audit FIX PLAN from part files written by other agents.\nFIRST write the following text VERBATIM with the Write tool to ${PLAN_PARTS}/10-findings-to-tasks.md:\n\n${findingsTable}\n\n${missing.length ? `These parts failed to be written; create each one with the Write tool, containing only the line "> PART MISSING: <file name> — the writer agent failed; rerun the synthesis for it.": ${missing.map(f => `${PLAN_PARTS}/${f}`).join(', ')}\n` : ''}${JOIN_RULES(PLAN_PARTS, `${OUT_DIR}/fix-plan.md`, '^### (T\\d+\\.\\d+) ', planTasks.map(t => t.task_id))}`, {
      label: 'synthesize:plan-join',
      phase: 'Synthesize',
      schema: DOC_SCHEMA,
      model: 'sonnet',
    })
  },
])

return {
  stats: {
    inventories: goodInv.length,
    maps: goodMaps.length,
    finders_failed: failedFinders,
    lane_steps_run: laneSteps.map(s => s.key),
    lane_steps_not_run: laneNotRun,
    research_claims: researchClaims.length,
    research_not_confirmed: physicsRaw.length,
    research_topics_failed: research.failed || [],
    raw_static: rawStatic.length,
    deduped_static: deduped.length,
    measured_new: measured.length,
    physics_new: physicsNew.length,
    followup_added: followupAdded,
    tiebreaks_upheld: rejudged,
    ...tally(verified),
    probe_tree_clean: (probeBaseline.probes || {}).tree_clean,
    quality_tree_clean: (probeBaseline.quality || {}).tree_clean,
  },
  report: report,
  fix_plan: plan,
  plan_tasks: planTasks.length,
  parts_failed: missingParts(reportJobs, reportPartsR).concat(missingParts(planJobs, planPartsR)),
  findings: verified,
  lane: laneForReport,
  research: researchClaims,
  inventories: goodInv,
  critique: critique,
}
