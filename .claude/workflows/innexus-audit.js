export const meta = {
  name: 'innexus-audit',
  description: 'Full audit of the Innexus harmonic analysis / resynthesis instrument (plugin code + the KrateDSP it consumes): static bug hunt across 25 dimensions, a literature check of the analysis / resynthesis algorithms and the physical models, one capped measurement lane (lints, build, sharded gate, pluginval, audio probes, analysis-accuracy and resynthesis-quality probes, preset and parameter audibility probes, clang-tidy, portability), 3-lens adversarial verification, a completeness critic round, then a findings report + self-contained fix plan under specs/innexus-audit/',
  whenToUse: 'When you want a comprehensive correctness/quality sweep of plugins/innexus that ends in specs/innexus-audit/report.md (every finding, verdict, evidence, the algorithm and physical-model review and the analysis / resynthesis / audibility tables) and specs/innexus-audit/fix-plan.md (ordered tasks a coding agent executes without re-deriving context). args: { date: "YYYY-MM-DD" (required for the report header), skip?: { build?, probes?, analysis?, audibility?, research?, tidy?, portability? }, extra_focus?: "free text the finders must also hunt" }. Nothing commits; the tree is left clean.',
  phases: [
    { title: 'Inventory', detail: 'mechanical cross-reference tables (params, types, presets/versions)', model: 'haiku' },
    { title: 'Understand', detail: 'eleven subsystem readers map the plugin and the shared DSP it uses', model: 'sonnet' },
    { title: 'Measure', detail: 'ONE serial lane, capped: lints, build -m:2, gate --share 4, pluginval, audio probes, analysis / resynthesis probes, preset / parameter audibility probes, tidy -Jobs 4, portability', model: 'sonnet' },
    { title: 'Research', detail: 'analysis, resynthesis and physical-model claims checked against published DSP / acoustics literature (web), four agents at a time', model: 'opus' },
    { title: 'Find', detail: 'one finder per bug dimension + a measurement interpreter', model: 'opus' },
    { title: 'Verify', detail: 'three independent skeptics per finding (code: correctness, context, impact; literature: code, literature, context+impact)', model: 'opus' },
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
const OUT_DIR = 'specs/innexus-audit'
const LOG_DIR = 'f:/tmp/innexus-audit'
const CMAKE = '"/c/Program Files/CMake/bin/cmake.exe"'
const BUILD_DIR = 'build/windows-x64-release'
let laneDone = false // set when the measurement lane finishes; verification runs throttled until then

// ---------------------------------------------------------------------------
// Shared context handed to every agent so they judge against the right rules.
// ---------------------------------------------------------------------------
const CONTEXT = `
You are auditing the Innexus VST3/AU plugin — a HARMONIC ANALYSIS / RESYNTHESIS
INSTRUMENT (it analyses audio into per-frame harmonic + residual models and
resynthesises them additively, with an optional physical-modelling path; AU type
aumu; VST3 subcategory Instrument|Synth; MIDI event in, one stereo output, one
stereo AUX "Sidechain" audio input that is not active by default, no MIDI
output) in the monorepo at f:/projects/iterum. Unless your task says otherwise
this is READ-ONLY: do not edit files. Read source and tests, reason about
correctness, cite file:line.

ARCHITECTURE FACTS (trust these; verify details in code):
- Plugin code: plugins/innexus/src/{processor,controller,dsp,parameters,preset,
  update}/, plugin_ids.h, entry.cpp (about 15,400 lines). There is NO engine/
  directory (plugins/innexus/CLAUDE.md names one). Tests: plugins/innexus/tests/
  {unit/processor,unit/vst,unit/controller,integration}, target innexus_tests
  (~590 cases in 66 files). The cases carry NO common tag ([innexus] sits on
  about 36 of them; the rest are per area: [ui], [confidence], [evolution],
  ...); perf cases are hidden ([.perf]). Several integration cases are
  diagnostics that read WAV files from absolute paths such as C:/test/*.wav.
  Bundle target: Innexus.
- TWO INPUT SOURCES, ONE FRAME CONTRACT (Krate::DSP::HarmonicFrame with up to
  kMaxPartials = 96 Partial entries + Krate::DSP::ResidualFrame with
  kResidualBands = 16 bands; dsp/include/krate/dsp/processors/harmonic_types.h,
  residual_types.h), selected by the atomic inputSource_ (kInputSourceId 500):
  (a) SAMPLE mode: a WAV (dr_wav) chosen in the editor or dropped on it ->
  IMessage "LoadSampleFile" -> Processor::loadSample -> SampleAnalyzer
  (src/dsp/sample_analyzer.{h,cpp}: a background std::thread, analyzeOnThread
  ~165-646) -> an immutable SampleAnalysis (src/dsp/sample_analysis.h) published
  to the audio thread by an atomic pointer swap (currentAnalysis_;
  Processor::checkForNewAnalysis, processor.cpp ~2066, runs inside process());
  the superseded analysis goes to pendingDeletion_ and is freed in setActive or
  the destructor, never on the audio thread; each voice advances
  currentFrameIndex through the frames and loops a sustain region.
  (b) SIDECHAIN mode: the aux input downmixed to mono (sidechainBuffer_, a fixed
  8192-sample array) -> LiveAnalysisPipeline (src/dsp/live_analysis_pipeline.
  {h,cpp}) running ON THE AUDIO THREAD, one frame per STFT hop, in two latency
  modes (kLatencyModeId 501: Low Latency = short window only, High Precision =
  short + long window) and a Mono / Poly / Auto analysis mode that exists in
  the pipeline (kAnalysisModeId 502 is declared in plugin_ids.h; whether it is
  registered and consumed is something to CHECK). An ANALYSIS FEEDBACK loop
  (kAnalysisFeedbackId 710 / decay 711, sidechain mode only) mixes the previous
  block's output (feedbackBuffer_, also 8192 samples, through a tanh) back into
  the analysis input.
- ANALYSIS PIPELINE shared by both front-ends: PreProcessingPipeline
  (src/dsp/pre_processing_pipeline.h: DC removal -> 30 Hz high-pass ->
  transient suppression -> noise gate) -> YIN pitch (processors/
  yin_pitch_detector.h) -> dual Blackman-Harris STFT (src/dsp/
  dual_stft_config.h: short 1024 / hop 512, long 4096 / hop 2048) ->
  PartialTracker (processors/partial_tracker.h: peak picking, Hungarian
  matching, the HARMONIC SIEVE) -> HarmonicModelBuilder (systems/
  harmonic_model_builder.h: dual-timescale smoothing) -> ResidualAnalyzer /
  SpectralCoringEstimator (16 residual bands). Polyphonic material:
  MultiPitchDetector, MultiSourceSieve, SubharmonicValidator; the offline path
  has a TWO-PASS polyphony guard (an unstable F0 triggers a second pass with a
  fixed F0 and voiced = false, so the sieve is off and every peak survives; that
  pass sets per-partial bandwidth to 0). EnvelopeDetector (src/dsp/
  envelope_detector.h) derives a DetectedADSR from the sample's level contour.
- FRAME SELECTION per block (Processor::process, processor.cpp ~348-2031: ONE
  function of about 1,680 lines): memory recall (~578) and capture (~695)
  triggers -> manual freeze (~917) -> harmonic filter mask (~975) -> morph
  between the frozen and the live frame (~1020) -> confidence-gated auto-freeze
  with a SpectralDecayEnvelope for live frames (~1087) -> EvolutionEngine
  (~1165, walks the occupied memory slots) -> the per-sample loop (~1273):
  frame advancement in sample mode (~1328), HarmonicBlender (~1488, weights
  over the eight memory slots + the live frame), the two HarmonicModulators
  (~1571; targets Amplitude / Frequency / Pan; free rate or tempo sync) ->
  applyModulatorAmplitude -> applyHarmonicPhysics (src/dsp/harmonic_physics.h:
  Coupling -> Warmth -> the per-partial agent dynamics of Stability / Entropy)
  -> broadcastFrameToVoices (~2195: copies the frame into every voice and loads
  it into that voice's oscillator bank and residual synthesizer).
- PER-VOICE SIGNAL PATH (src/processor/innexus_voice.h + processor.cpp
  ~1637-1878): Krate::DSP::HarmonicOscillatorBank::processStereo (up to 96
  Gordon-Smith magic-circle sines; partial count 48 / 64 / 80 / 96 from
  kPartialCountId, a StringListParameter; inharmonicity amount, detune spread,
  stereo spread; phase state is NOT reset across frames = continuity) + an
  exciter chosen by kExciterTypeId (Residual = ResidualSynthesizer, Impact =
  ImpactExciter with a mallet-choke envelope, Bow = BowExciter driven by the
  ADSR and the resonator's energy, optional 2x oversampling) -> a resonator
  chosen by kResonanceTypeId (ModalResonatorBank or WaveguideString, an
  equal-power crossfade with energy gain matching on a switch) ->
  BodyResonance -> PhysicalModelMixer (out = harmonic + (1 - mix) x residual +
  mix x physical; kPhysModelMixId 800, default 0) -> per-voice freeze-recovery
  and anti-click crossfades, velocity, MPE note expression (tuning, volume,
  pan, brightness), the ADSR (kAdsrAmountId 724 blends it in; default 0) and
  the release envelope (kReleaseTimeId 200).
- MASTER (processor.cpp ~1879-2027): sum of voices x 1/sqrt(active voice
  count) -> source-switch crossfade -> manual-freeze recovery crossfade ->
  global Krate::DSP::SympatheticResonance fed with the mono sum -> master gain
  -> a per-sample finiteness guard + tanh SOFT LIMITER above a 0.85 knee
  (always on, no control) -> output and silence flags -> the feedback buffer ->
  sendDisplayData.
- VOICES AND MIDI (src/processor/processor_midi.cpp): kVoiceModeId 730 = Mono
  / 4 Voices / 8 Voices (kMaxVoices = 8, Krate::DSP::VoiceAllocator), noteId
  routing for MPE, pitch bend +/-12 semitones, note expression; handleNoteOn
  ~339, initVoiceForNoteOn ~127, handleNoteOff ~515. Events are dispatched
  through plugins/shared/src/midi/midi_event_dispatcher.h.
- PARAMETERS: about 88 registered (Controller::initialize, controller.cpp
  ~241-893); IDs in src/plugin_ids.h by section: 0-1 bypass / master gain,
  200-202 release / inharmonicity / partial count, 300-306 freeze / morph /
  harmonic filter / responsiveness / memory slot / capture / recall, 400-403
  harmonic and residual levels / brightness / transient emphasis, 500-502
  input source / latency mode / analysis mode, 601-649 stereo spread,
  evolution, two modulators, detune, the nine blend weights, 700-703 harmonic
  physics, 710-711 analysis feedback, 720-728 ADSR, 730 voice mode, 800-861
  physical modelling. The processor stores every parameter as an atomic float
  (processor.h), most of them NORMALIZED and denormalized at the point of use,
  some as plain values (releaseTimeMs_, resonanceDecay_): which is which
  differs per parameter. Processor::processParameterChanges is
  src/processor/processor_params.cpp; src/parameters/innexus_params.h holds
  mapping helpers.
- STATE (src/processor/processor_state.cpp: getState ~20-253, setState
  ~254-725): one int32 version followed by a fixed SEQUENCE of fields; today's
  writer writes version 2 and the reader accepts ONLY versions 1 and 2. Fields
  added after version 2 were APPENDED WITHOUT a version bump and are read with
  "if (streamer.readFloat(...))" so an older, shorter stream keeps defaults.
  The stream also carries the sample file path (setState calls loadSample,
  which starts the analysis thread), the residual frames of the loaded
  analysis, the eight memory slots (harmonic snapshots with per-slot ADSR
  data) and an instance-id trailer for the SharedDisplayBridge. RELEASES: tags
  innexus/v0.9.4 .. innexus/v1.0.8 exist on origin; v0.9.4 wrote version 9,
  v0.9.5 .. v0.10.0 wrote version 1, v1.0.0 and later write version 2, so what
  today's build does with a project saved by each release is something to
  CHECK, never to assume. Tests are still named test_state_v6 .. test_state_v10
  and specs/_architecture_/innexus-plugin.md still describes "State
  Persistence (Version 4 / 5 / 6)": the code is the truth, stale names are
  leads. The CONTROLLER parses the same stream a second time
  (controller/controller_state.cpp readComponentState ~71-572, "the one
  controller parser (R-200)"), and tools/innexus_preset_format.h writes it a
  third time.
- LOAD PATHS: host setState; the preset browser -> Controller::
  loadComponentStateWithNotify (controller/controller_presets.cpp ~112) ->
  IMessage "LoadPresetState" -> Processor::notify (src/processor/
  processor_messages.cpp ~195) -> the processor's state reader; a harmonic
  snapshot JSON file -> Controller::importSnapshotFromJson -> IMessage
  "HarmonicSnapshotImport" (src/dsp/harmonic_snapshot_json.h) -> a memory
  slot; a sample -> "LoadSampleFile" -> loadSample, answered with
  "SampleFileLoaded". Processor::notify runs on the host's message thread.
  Processor -> controller: a DisplayData block (controller/display_data.h)
  through a Steinberg DataExchangeHandler at about 30 Hz with a
  SharedDisplayBridge polling fallback (plugins/shared/src/display/
  shared_display_bridge.h), plus the IMessages "DetectedADSR" and
  "RecalledADSR", which are allocated and sent from inside process()
  (processor.cpp ~644 and ~2116).
- CONTROLLER AND EDITOR: controller/controller.cpp (~1475 lines: initialize,
  createCustomView ~937, createSubController ~1049, didOpen ~1071, willClose
  ~1144, the DataExchange receiver ~1202-1241, notify ~1242, note expression
  ~1399), controller_state.cpp, controller_presets.cpp, controller_adsr.cpp
  (setParamNormalized ~57), controller_view_sync.cpp (visibility of the
  impact / bow / modal / waveguide / feedback / latency containers,
  onDisplayTimerFired ~367), controller_param_display.cpp,
  modulator_sub_controller.{h,cpp}, sample_drop_target.h, adsr_expanded_
  overlay.h (plugin-local), views/ (HarmonicDisplayView,
  ConfidenceIndicatorView, MemorySlotStatusView, EvolutionPositionView,
  ModulatorActivityView). resources/editor.uidesc is ~1355 lines (templates
  Editor and modulator_panel; sub-controller ModulatorController used twice).
  The controller caches about twenty raw view pointers (controller.h ~236-348)
  and runs a display timer. Shared views come from plugins/shared/src/ui/
  (ArcKnob, SyncableArcKnob, BipolarSlider, FieldsetContainer, ToggleButton,
  ActionButton, OutlineButton, ADSRDisplay, preset browser, save dialog,
  update banner).
- PRESETS: 54 factory presets (.vstpreset) in the seven FIXED categories
  Brass and Winds / Drums and Perc / Found Sound / Keys / Pads and Drones /
  Strings / Voice under plugins/innexus/resources/presets/<Category>/,
  generated by tools/innexus_preset_generator.cpp + tools/
  innexus_preset_format.h (build target generate_innexus_presets). As
  generated, a factory preset is a PARAMETER SET: its sample path is empty and
  its eight memory slots are empty, so it makes no sound until the user
  supplies a sample or a sidechain signal, and features that need occupied
  memory slots (evolution, blend, recall) have nothing to work on (confirm in
  the generator before relying on it).
- KrateDSP consumed (dsp/include/krate/dsp/): processors/
  harmonic_oscillator_bank.h, harmonic_types.h, harmonic_frame_utils.h,
  harmonic_snapshot.h, residual_types.h, residual_analyzer.h,
  residual_synthesizer.h, spectral_coring_estimator.h, yin_pitch_detector.h,
  partial_tracker.h, multi_pitch_detector.h, multi_source_sieve.h,
  subharmonic_validator.h, modal_resonator_bank.h, waveguide_string.h,
  impact_exciter.h, bow_exciter.h, body_resonance.h, envelope_follower.h;
  systems/harmonic_model_builder.h, sympathetic_resonance.h,
  voice_allocator.h; primitives/stft.h, spectral_buffer.h, adsr_envelope.h,
  smoother.h, biquad.h; core/window_functions.h, pitch_utils.h, midi_utils.h,
  note_value.h, random.h, sigmoid.h, db_utils.h, math_constants.h,
  scoped_denormal_mode.h. Several are shared with other plugins (Membrum uses
  the modal bank, the waveguide string, both exciters, the sympathetic
  resonance and the voice allocator; grep the others).
- Reference docs: specs/INNEXUS-ROADMAP.md, specs/Innexus-initial-plan.md,
  specs/Innexus_harmonic_oscillator_dsp_plan.md, specs/Innexus-emergent-
  harmonics-roadmap.md, specs/Innexus-physical-modelling-roadmap.md, the
  ARCHIVED milestone specs specs/_archive_/115-innexus-m1-core-instrument ..
  124-adsr-envelope-detection and 127-modal-resonator-bank ..
  132-sympathetic-resonance (spec.md, plan.md, research.md; code comments cite
  their FR-xxx / SC-xxx and still write the old path specs/<nnn>-.../spec.md),
  specs/_architecture_/innexus-plugin.md, plugins/innexus/MANUAL.md, docs/
  (index.html, innexus_signal_flow.dot, polyphonic-f0-design-notes-2026-07.md),
  CHANGELOG.md, version.json, plugins/innexus/CLAUDE.md,
  .claude/skills/innexus/SKILL.md.
- PRIOR AUDITS (HISTORY: never overwrite them): (1) plugins/innexus/docs/
  audit-2026-07-implementation-plan.md (2026-07-17: 44 findings as work items
  WI-1 .. WI-22 plus the quick-sweep items QS-n / Cn; code comments cite
  WI-n, up to WI-24); (2) the Ruinae audit changed Innexus's preset-load path
  (code comments R-200 / R-201 and others refer to specs/ruinae-audit/
  report.md); (3) the Membrum audit (specs/membrum-audit/report.md and
  fix-plan.md, ids MB-nnn) reviewed and is CHANGING shared headers Innexus
  uses (modal_resonator_bank.h, impact_exciter.h, sympathetic_resonance.h, the
  waveguide string, the bow exciter): its fix phases land on this branch
  (git log --oneline --grep "audit phase"), and its section 5 holds literature
  verdicts on those models; (4) specs/disrumpo-audit/report.md mentions Innexus
  where a shared file is involved. A prior finding that is still present in
  today's code is a valid finding (say which prior id it repeats); one that
  was fixed is not. THIS audit's ids are INX-nnn.

REAL-TIME AUDIO RULES (violations are bugs): no allocation, lock, exception,
string/IO or unbounded loop on the audio thread (process(), everything a voice,
the live analysis pipeline, the frame pipeline or the master section calls per
block, setActive while processing); state written from another thread (the
IMessage handlers, setState, the analysis thread) must reach the audio thread
through an atomic or a transfer object, never by writing engine members
directly; parameters at the VST boundary are normalized [0,1]; NaN/Inf must
never reach the output; processor and controller never cross-include headers
(processor.h includes controller/display_data.h: judge whether that is a shared
data contract or a violation); the macOS leg compiles with -ffast-math
(classify finiteness only via Krate::DSP::detail::isFinite / isNaN / isInf,
never std::isnan or a bit-mask clone; no numeric_limits infinity() /
quiet_NaN() constants in fast-math code; clamp pow / sqrt / log arguments at
domain edges; produce values that must be exact at a neutral point by a branch
or clamp, not by arithmetic that cancels).

INTENDED BY DESIGN (not findings): the mono harmonic sieve runs only when the F0
is voiced, and the polyphonic / pass-2 code turns it off conditionally with a
neutral voiced = false F0 (never globally); pass 2 sets per-partial bandwidth to
0; harmonic physics and the modulators change amplitudes only; a superseded
SampleAnalysis is freed only in setActive or the destructor; kPartialCountId is
a StringListParameter; the preset categories are the fixed seven; the
oscillator bank keeps its phase across frames.

RESYNTHESIS-SPECIFIC HAZARDS: ANALYSIS: octave errors and a wrong F0 at the
edges of the pitch range (the YIN window against the lowest supported F0),
partials rejected by the sieve (low fundamentals, inharmonic sources) or
spurious ones kept, amplitude and frequency bias of the peak interpolation,
the long-window STFT analysed but not used, the two front-ends (offline, live)
disagreeing on the same signal or on the scale of a quantity (inputRms), a
sample whose rate differs from the host rate, very short / very long / silent /
clipped / multi-channel files, frame-to-frame jitter that becomes audible
roughness. NOISE: per-partial bandwidth (0 = sine, 1 = noise) inflated so that
every partial hisses (the historical bug), residual energy that does not match
the source's noise, a residual synthesizer that is re-prepared or reloaded per
sample. RESYNTHESIS: oscillator divergence (the magic-circle epsilon outside
its stability bound after detune / modulation / pitch bend / inharmonicity),
partials at or above Nyquist (high notes, 44.1 kHz, pitch bend up, 96
partials), clicks at a frame change, at freeze on / off, at a morph, a recall,
a source switch, a partial-count change, a voice steal and a retrigger, pitch /
level / envelope times that depend on the sample rate or the host block size.
FRAME PIPELINE: work redone per SAMPLE that belongs per frame or per block
(evolution, blend, frame broadcast, pan and detune tables: CPU spikes and
dropouts), memory slots written by the message thread while the audio thread
reads them, capture / recall triggers lost or fired twice, a modulator target
that is ignored. LIVE PATH: the whole analysis on the audio thread (cost per
hop, worst-case block), host blocks larger than the 8192-sample sidechain and
feedback buffers, the feedback loop running away or self-oscillating with no
input, latency that is not reported, a confidence gate that freezes or releases
at the wrong moment. PHYSICAL PATH: resonators that ring forever or blow up
(feedback velocity, bow pressure, stiffness, stretch at their extremes),
exciter / resonator switches while ringing, the sympathetic resonance making
sound with no notes. LEVEL: polyphony compensation, harmonic + residual +
physical balance, the always-on soft limiter shaping ordinary playing.
PARAMETERS AND PRESETS: parameters that do nothing, too little, or only in a
context the editor does not show; factory presets that sound alike, or whose
distinguishing features cannot act (empty memory slots, a zero mix). STATE: a
project saved by a released version rejected or misread, a tail field read into
the wrong parameter because one reader skipped a field (processor, controller
and generator each parse the stream), huge counts in a corrupt stream,
setState starting a thread. CROSS-THREAD: the analysis thread, the message
thread and the audio thread around currentAnalysis_, memorySlots_,
loadedFilePath_ and the display bridge; denormals in long tails.

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
    log_path: { type: 'string', description: 'absolute path of the captured log under f:/tmp/innexus-audit/' },
  },
}

const PROBE_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['ran', 'probe_source_path', 'tree_clean', 'summary', 'measurements', 'artifacts', 'log_path'],
  properties: {
    ran: { type: 'boolean' },
    probe_source_path: { type: 'string', description: 'where the probe TU was preserved (under specs/innexus-audit/probes/) or "-"' },
    tree_clean: { type: 'boolean', description: 'git status --porcelain plugins/innexus was empty after cleanup' },
    summary: { type: 'string' },
    measurements: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['name', 'setup', 'metric', 'value', 'threshold', 'verdict', 'note'],
        properties: {
          name: { type: 'string', description: 'short probe id, e.g. "matrix:voices=8:exciter=Bow:resonator=Waveguide", "analysis:S1-110Hz:offline" or "param:kMod1TargetId"' },
          setup: { type: 'string', description: 'source or test signal, preset, input mode, note, velocity, params, sample rate, block size' },
          metric: { type: 'string', description: 'what was measured (peak dBFS, tail time, F0 or partial frequency error in cents, partial amplitude error in dB, harmonic-to-noise ratio, click count, NaN count, onset sample offset, band distance dB, ...)' },
          value: { type: 'string', description: 'the number(s) as printed' },
          threshold: { type: 'string', description: 'the bar you judged against and where it comes from (spec SC, the known content of the test signal, the documented range, common practice)' },
          verdict: { type: 'string', enum: ['ok', 'suspicious', 'bad', 'error'] },
          note: { type: 'string', description: 'which code the result implicates, if any (file:line), or why it is fine' },
        },
      },
    },
    artifacts: { type: 'array', items: { type: 'string' }, description: 'repo-relative paths of the result files copied to specs/innexus-audit/probes/ (CSV tables, results text)' },
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
          category: { type: 'string', description: 'kebab-case: rt-safety, threading, dsp-numerics, analysis-pitch, analysis-partials, residual-noise, resynthesis, frame-pipeline, physical-path, voice-management, midi-timing, gain-staging, live-sidechain, param-plumbing, dead-parameter, preset-quality, lifecycle-state, ui-wiring, shared-dsp-contract, duplication, parallel-path, spec-conformance, portability, test-gap, anti-pattern, prior-audit-regression, measured-*' },
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

// One algorithmic or physical claim checked against the literature. A topic agent returns several.
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
          title: { type: 'string', description: 'one line naming the claim, e.g. "The pitch detector implements the cumulative mean normalized difference of YIN with an absolute threshold"' },
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
          implication: { type: 'string', description: 'the audible consequence if wrong or oversimplified, and which sources, notes or settings expose it' },
          severity: { type: 'string', enum: ['high', 'medium', 'low', 'none'], description: 'none when confirmed or when the deviation is inaudible' },
          suggested_fix: { type: 'string', description: 'the concrete change (values / formula / topology) and the test that would pin it, or "-"' },
          prior_verdict: { type: 'string', description: 'what a prior audit ruled on this claim (a WI-n item of the 2026-07 Innexus audit plan, or section 5 of the Membrum audit report for a shared header) and whether the code changed since, or "-"' },
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
          question: { type: 'string', description: 'the question to verify against the literature' },
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
          kind: { type: 'string', enum: ['uncovered-area', 'unverified-claim', 'suspicious-refutation', 'missing-measurement', 'lane-not-run', 'unresearched-claim'] },
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
const LANE_KEYS = ['lints', 'build', 'gate', 'pluginval', 'probes', 'analysis', 'audibility', 'tidy', 'portability']

// ---------------------------------------------------------------------------
// Measurement lane — ONE heavy job at a time, each capped. Started first and
// NOT awaited until the static findings are in, so it overlaps the reading
// phases. Every step is its own agent (fresh context, structured result).
// ---------------------------------------------------------------------------
async function laneStep(key, prompt, opts) {
  const r = await agent(`${CONTEXT}\n\nYou are ONE step of the Innexus measurement lane. Only this step runs on the machine right now, but readers are also using the box: stay inside the caps named below, never widen them. Capture every long command's output to a log file under ${LOG_DIR}/ on the FIRST run and read the log (never re-run a slow command to grep it). Create ${LOG_DIR} if missing. Do not edit repository files unless your step says so.\n\n${prompt}`, {
    label: `lane:${key}`,
    phase: 'Measure',
    ...opts,
  })
  LANE_STEPS.push({ key, result: r })
  return r
}

// Rules shared by the three probe-author steps.
const PROBE_RULES = `Every TEST_CASE carries the tags "[.][audit-probe]" (hidden: never part of the per-push gate) and PRINTS one line per measurement to stdout in the form
  PROBE <name> | <setup> | <metric> = <value> | threshold <bar> | <ok|suspicious|bad>
using WARN() or std::printf, and asserts nothing except that the output is finite (so a probe never fails the build gate; the numbers are the result). Split the work over many TEST_CASEs so the shards balance; keep the whole probe set under ~10 minutes wall clock when sharded. Heap-allocate large render buffers (a huge local array overflows the stack and Catch2 reports "Unknown exception"). Measure levels, frequencies and sample counts only: NO wall-clock or CPU measurements (the box is shared).

HARNESS: model it on plugins/innexus/tests/integration/innexus_processor_tests.cpp (makeTestAnalysis / makeFullTestAnalysis build a SampleAnalysis by hand and Processor::testInjectAnalysis hands it to a real Processor: the way to play a KNOWN harmonic model without the analyzer), integration/sample_load_e2e_tests.cpp (writes a sine or multi-harmonic WAV to a temp file and runs it through the real SampleAnalyzer and the processor's load path: the way to test the analyzer itself), integration/sidechain_integration_tests.cpp and test_sidechain_signal_end.cpp (ProcessData with the aux input bus, feeding a signal through LiveAnalysisPipeline), integration/test_polyphonic_mpe.cpp (voice modes, noteId, note expression), integration/harmonic_memory_tests.cpp and musical_control_tests.cpp (capture / recall / freeze / morph), integration/test_modulator_sync.cpp (tempo in the process context), unit/vst/controller_state_tests.cpp and test_state_v10.cpp (getState / setState through a MemoryStream; the controller's reader), unit/processor/sample_analyzer_tests.cpp and live_analysis_pipeline_tests.cpp (the two front-ends driven directly), unit/controller/editor_lifecycle_test.cpp (reading a factory .vstpreset from INNEXUS_RESOURCES_DIR). Use the shared helpers in tests/test_helpers/: spectral_analysis.h, artifact_detection.h (clicks / discontinuities), signal_metrics.h, audio_features.h, test_signals.h, vst_event_list.h, vst_param_changes.h, parameter_sweep.h. Read their headers for exact signatures before writing code. Synthesise every test signal in the TU (sums of sines with known frequencies, amplitudes and phases; seeded noise): never read a WAV from outside the repository (the existing diagnostics that open C:/test/*.wav are not a model).

Hard rules: Write tool for new files (no heredocs), one build at a time, never run tools/clean-test-output.js, never run the whole test executable as a single serial process, never commit, never touch the prior-audit file plugins/innexus/docs/audit-2026-07-implementation-plan.md. Temp files (generated WAVs) go under ${LOG_DIR}/, never into the repository. Everything you add under plugins/ is TEMPORARY and must be removed at the end.`

function probeProcedure(tu, preserved, resultsFile) {
  return `PROCEDURE:
 a. Write the TU plugins/innexus/tests/audit/${tu}; append it to the innexus_tests source list in plugins/innexus/tests/CMakeLists.txt.
 b. Build (cap TWO nodes; this is the only build on the box): ${CMAKE} --build ${BUILD_DIR} --config Release --target innexus_tests -- -m:2  > ${LOG_DIR}/${tu}.build.log 2>&1 ; read the log; fix YOUR compile errors/warnings (never touch plugin code).
 c. Run sharded, a quarter of the machine, capture:  node tools/run-gate.js --share 4 --filter '[audit-probe]' innexus_tests > ${LOG_DIR}/${tu}.run.log 2>&1 ; the PROBE lines are in the per-shard logs under f:/tmp/gate/ — collect them all into ${LOG_DIR}/${resultsFile}.
 d. Preserve: copy the TU to ${OUT_DIR}/probes/${preserved} (create the dir; the .txt suffix keeps repo-wide .cpp globs, the portability hook and clang-tidy away from it) and copy ${LOG_DIR}/${resultsFile} plus any CSV you wrote to ${OUT_DIR}/probes/, so the fix plan can promote the useful probes into real regression tests and the report can link the tables.
 e. CLEAN UP, mandatory: git checkout -- plugins/innexus/tests/CMakeLists.txt ; delete plugins/innexus/tests/audit/ ; then run  git status --porcelain plugins/innexus  and report tree_clean = (output empty). If anything else shows up there, you broke the rule — revert it.
 f. Return the measurements with a verdict each and, for anything suspicious/bad, read the implicated code and name file:line in note. List every file you copied to ${OUT_DIR}/probes/ in artifacts.`
}

async function runMeasurementLane() {
  const lane = {}

  lane.lints = await laneStep('lints', `STEP: repo lints scoped to Innexus (cheap node scripts, run them one after another, each output to its own log):
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
  node tools/check-au-plist.js innexus
  node tools/uidesc-inventory.js plugins/innexus/resources/editor.uidesc --check
Read each script's header comment first if its arguments are unclear (some take a path, a plugin name or --self-test). Report ONLY items that touch plugins/innexus/, tools/innexus_preset_generator.cpp or tools/innexus_preset_format.h, a plugins/shared/ file Innexus includes, or a dsp/ header Innexus includes (grep -rhoE '#include <krate/dsp/[^>]+>' plugins/innexus/src gives that list); a tree-wide ratchet baseline hit elsewhere is not an Innexus item, but an Innexus file that sits IN a ratchet baseline is (name the baseline file). kind=lint.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })

  if (!SKIP.build) {
    lane.build = await laneStep('build', `STEP: build the Innexus test executable and the plugin bundle in ONE invocation, capped at two MSBuild nodes:
  ${CMAKE} --build ${BUILD_DIR} --config Release --target innexus_tests Innexus -- -m:2
The bundle target is "Innexus" (the built bundle is ${BUILD_DIR}/VST3/Release/Innexus.vst3); if MSBuild says the target does not exist, grep -rn PLUGIN_NAME plugins/innexus/CMakeLists.txt for the real name. If ${BUILD_DIR} does not exist, configure first: ${CMAKE} --preset windows-x64-release. A post-build copy failure into "C:/Program Files/Common Files/VST3/" is NOT an error (the compile succeeded). Report every compiler WARNING (the project requires zero) as kind=build-warning with file:line, and any error as build-error. Record the wall clock.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })

    lane.gate = await laneStep('gate', `STEP: run the per-push Innexus suite SHARDED, taking a quarter of the machine:
  node tools/run-gate.js --share 4 --scope all --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' innexus_tests   # gate-all-ok: the audit measures the whole Innexus suite
(Keep the "# gate-all-ok: ..." comment: without it tools/hooks/guard-test-scope.js blocks a whole-suite run; without the scope flag the gate runs only cases touched by uncommitted changes.)
Run it ONCE, capture to a log, then read the log. Report the GATE GREEN / GATE RED line verbatim in summary. For every failed case report kind=test-red with the test file:line and the failed assertion text (per-shard logs are under f:/tmp/gate/). Also note skipped/hidden/[!mayfail] cases you see in the log, every case that SKIPs or passes vacuously because a file under C:/test/ is missing (the WAV diagnostics in tests/integration/: name each case and the path it wants), the slowest five cases from shard-times.tsv, and any file the run left modified or created inside the repository (git status --porcelain afterwards: report where rendered or temporary WAV files land, do not delete tracked files). Timing tests are excluded on purpose; do NOT run tools/run-cpu-tests.js.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })

    lane.pluginval = await laneStep('pluginval', `STEP: validate the bundle:
  tools/pluginval.exe --strictness-level 5 --validate "${BUILD_DIR}/VST3/Release/Innexus.vst3"
Capture to a log. The EXIT CODE is the verdict (there is no SUCCESS line): 0 = pass; 0xC0000005 (-1073741819) = crash. Report every FAIL/error block as kind=pluginval with the test name and message; on a crash report the last test that started.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })
  } else {
    log('Measure: build/gate/pluginval skipped by args.skip.build')
  }

  if (!SKIP.probes && !SKIP.build) {
    lane.probes = await agent(`${CONTEXT}\n\nYou are the AUDIO PROBE author of the Innexus measurement lane (hard defects). You MAY edit files, but ONLY these: create plugins/innexus/tests/audit/audit_probes.cpp, append it to the innexus_tests source list in plugins/innexus/tests/CMakeLists.txt, and create files under ${OUT_DIR}/probes/.

GOAL: measure what code reading cannot — play real notes through the real Processor (sample mode with an injected or analysed source; sidechain mode with a synthesised input signal) and print numbers. Analysis accuracy, resynthesis fidelity and preset / parameter audibility are measured by two OTHER probe steps: this one hunts crashes, NaN, clicks, runaway, timing, level and state defects.

${PROBE_RULES}

REFERENCE SOURCE unless a group says otherwise: an injected SampleAnalysis of a 110 Hz tone with 32 harmonics at 1/n amplitudes, stable over 2 s, with a low flat residual; note A2 (MIDI 45) at velocity 100/127; 48 kHz; block 512.

MEASURE AT LEAST (one TEST_CASE per group or sub-group, loop inside):
 1. Configuration matrix: voice mode (Mono, 4, 8) x exciter (Residual, Impact, Bow) x resonator (Modal, Waveguide) x Physical Model Mix (0, 0.5, 1), other parameters at their defaults: one note held 1 s then released -> peak dBFS, RMS, DC offset, NaN/Inf count, click count, tail time after the note-off (until the 10 ms RMS envelope stays 60 dB under its maximum; ">10 s" if it never does).
 2. Tuning: the reference source on MIDI notes 24, 36, 48, 60, 72, 84, 96, 108 -> measured fundamental vs equal temperament in cents, and partials 2, 4, 8, 16 vs n x f0 in cents with Inharmonicity at 0 and at 1; pitch bend at -1, 0 and +1 (expect -12 / 0 / +12 semitones); note-expression tuning; Detune Spread at 0.5 and 1 -> spread of the partials in cents. High notes at 44.1 kHz with Partial Count 96: are partials at or above Nyquist dropped, or is there energy at aliased frequencies (name the strongest alias)?
 3. Sample-rate consistency: the same injected analysis rendered at 44.1k, 48k, 96k, 192k -> fundamental (cents), RMS (dB), spectral centroid (%), measured release time for Release at 100 ms and 2000 ms, measured ADSR attack and decay with ADSR Amount at 1; flag more than 5 cents, 1 dB, 10 %. Then a 44.1 kHz WAV analysed by the real SampleAnalyzer and played with the host at 96 kHz -> pitch error in cents and the playback speed of the frame sequence (does the timbre evolve at the same rate as at 44.1 kHz?).
 4. Timing: one note-on at sampleOffset 0, 1, 100 and blockSize-1 -> index of the first non-zero output sample minus the offset (the processor reports no latency); the SAME event stream (8 notes, sample mode) rendered with block sizes 32, 64, 512, 2048 and 16384 -> max sample difference between renders and the onset index of every note per block size. Sidechain mode with a block of 16384 (above the 8192-sample sidechain and feedback buffers): no crash, and how many input samples reach the analysis?
 5. Voice lifecycle: the same note retriggered 20 times at 20 ms spacing in Mono -> click count, largest sample-to-sample jump; in 4 and 8 voices, voices+4 different notes in quick succession -> click count and jump at each steal; three steals inside 5 ms; Voice Mode changed 8 -> Mono while 6 voices ring and Mono -> 8 while one rings; a note-off during the attack; Release at 20 ms and 5000 ms -> measured time to -60 dB; sustain: a 10 s note on a 2 s analysis -> level and click count across the sustain loop.
 6. Tail and idle: after the last note-off, time until the output is exactly silent and the silence flags are set, per exciter x resonator; Sympathetic Amount at 1 with no note ever played -> output RMS over 5 s; Bow held 30 s -> level drift (dB between the first and the last second); Analysis Feedback at 1, decay 0, in sidechain mode with a 1 s burst followed by silent input -> RMS in each of the next 10 s (self-oscillation?); a state with denormal-range levels -> any denormal output.
 7. Level: one note at the defaults -> peak dBFS; 8 notes at velocity 127 in 8-voice mode -> peak before and after the soft limiter (how far above the 0.85 knee, and the share of samples above it); Harmonic Level and Residual Level at 2.0 with Physical Model Mix 0.5 -> peak and limiter share; a single 1 kHz-fundamental note at ordinary settings -> THD added by the limiter; Master Gain at 0, 0.5, 1 normalized -> measured gain in dB vs the displayed value; velocity 0.1 .. 1.0 in ten steps -> level curve (monotonic? range in dB); Stereo Spread 0 and 1 -> L / R level, inter-channel correlation and the level of the mono sum (mono compatibility); note-expression pan and volume.
 8. Sidechain mode: a 220 Hz sawtooth on the aux bus with a held note -> output fundamental vs the MIDI pitch (cents) and the delay from input onset to the first output frame per Latency Mode (compare with the 15-25 ms / 50-100 ms documented in src/dsp/dual_stft_config.h); the lowest input F0 that is still tracked per mode (sweep 30 .. 120 Hz; compare with the documented ~80-100 Hz / ~40 Hz); input silence after 1 s of signal -> what the output does over the next 3 s (confidence-gated freeze, spectral decay); Input Source switched Sample <-> Sidechain while a note rings -> click count; the aux bus absent (numInputs = 0), mono, inactive, and with null channel buffers -> no crash, output finite.
 9. Musical control: Freeze on and off while a note rings -> click count and largest jump; Morph swept 0 -> 1 over 1 s against a frozen frame -> click count; each Harmonic Filter type -> measured odd / even / low / high partial energy vs the type's promise; Responsiveness 0, 0.5, 1 in sidechain mode -> time for the output spectrum to follow a step change of the input timbre; memory Capture then Recall in each of the 8 slots -> recalled frame equal to the captured one (max partial amplitude difference), Recall with no note and no analysis loaded; Capture and Recall written in the same block; Partial Count stepped 48 -> 96 -> 48 while a note rings -> click count.
10. Modulators, evolution, blend: Modulator 1 with Target = Amplitude, then Frequency, then Pan, depth 1, rate 2 Hz -> measured amplitude-modulation depth, frequency deviation in cents and L / R movement FOR EACH TARGET (a target that also moves the other two quantities is the result to report); Rate Sync on at 120 BPM for three note values -> measured rate vs expected; Range Start / End limiting the affected partials; Evolution enabled with 3 occupied slots at Speed 1 Hz -> measured cycle time per mode, and with NO occupied slot -> bit-identical to evolution off?; Blend enabled with two slot weights -> output partial amplitudes vs the weighted mix; all blend weights 0.
11. Physical path: Resonance Decay at 0.01, 0.5, 5 s -> measured T60 per resonator vs the displayed value; Brightness 0 / 1 -> decay ratio of a high vs a low mode; Stretch and Scatter 0 / 1 -> ratio of mode 4 to mode 1; Waveguide tuning error in cents on notes 36 .. 96 with Stiffness 0 and 1; the Modal <-> Waveguide switch while ringing -> click count and level step; Impact Hardness / Mass / Position / Brightness extremes -> peak, NaN; Bow Pressure x Speed at their extremes -> peak, NaN, pitch error, and Bow Oversampling on vs off -> level and spectral difference; Body Mix 0 vs 1 with Size / Material extremes -> peak, NaN; Sympathetic Amount 1 with decay 1 under a 30 s sequence of notes -> RMS in the last second vs the first (runaway?).
12. Parameter automation mid-note: on a held note, step every continuous parameter 0 -> 1 (one at a time, in the context where it acts) -> click count and NaN count per parameter; switch Exciter Type, Resonance Type, Voice Mode, Input Source, Latency Mode and Partial Count while the note rings.
13. State: getState -> setState -> getState byte-identical at the defaults, with a loaded sample analysis, with occupied memory slots, and for each of the 54 factory presets (INNEXUS_RESOURCES_DIR "/presets/<Category>/<name>.vstpreset": load its component state); render the reference note before and after the round trip -> max sample difference. Feed setState: a truncated stream (cut at 25 %, 50 %, 99 %), garbage bytes, version 9 (what innexus/v0.9.4 wrote), version 3 and version 0, a path length of 2^31-1 and of 4095 with a short stream, a residual frame count of 10^9, a memory slot with numPartials 10^6 or negative, NaN and out-of-range floats, a sample path that does not exist -> return code, whether the previous state survives intact (getState compared with the state before the attempt), finite output, and whether an analysis thread was started. The byte size of the state with a 10 s sample loaded.
14. Reader parity: for the default state, three factory presets and a state with every parameter off its default, feed the SAME bytes to Processor::setState and to the controller's reader (Controller::setComponentState) -> compare every parameter's normalized value on the controller with the processor's value (through the processor's getters): any parameter that differs means the two readers disagree on the stream layout. Load path A (setState) vs path B (the "LoadPresetState" IMessage with the same bytes through Processor::notify) -> getState bytes identical? A malformed and an oversized JSON through "HarmonicSnapshotImport" -> no crash, slot unchanged.
15. Sample loading: WAV files generated into ${LOG_DIR}/ — mono and stereo, 8 / 16 / 24-bit and float, 22.05 / 44.1 / 96 kHz, 5 ms long, 60 s long, all-zero, full-scale clipped, a 0-byte file, a text file renamed .wav -> does the analysis complete, the frame count, finite output on a note; a second loadSample issued while the first analysis still runs, 20 times in a row while process() is called in a loop -> no crash, the last file wins.

${probeProcedure('audit_probes.cpp', 'audit_probes.cpp.txt', 'probe-results.txt')}`, { label: 'lane:probes', phase: 'Measure', schema: PROBE_SCHEMA, agentType: 'general-purpose', model: 'opus' })
    LANE_STEPS.push({ key: 'probes', result: lane.probes })
  } else {
    log('Measure: audio probes skipped')
  }

  if (!SKIP.analysis && !SKIP.build) {
    lane.analysis = await agent(`${CONTEXT}\n\nYou are the ANALYSIS-ACCURACY and RESYNTHESIS-QUALITY PROBE author of the Innexus measurement lane. You MAY edit files, but ONLY these: create plugins/innexus/tests/audit/audit_analysis_probes.cpp, append it to the innexus_tests source list in plugins/innexus/tests/CMakeLists.txt, and create files under ${OUT_DIR}/probes/.

GOAL: put numbers on two questions: (A) how accurately do the two analysis front-ends recover signals whose content is KNOWN; (B) how close is the resynthesised sound to its source. The numbers are the result; another agent localises causes.

${PROBE_RULES}

TEST SIGNALS (synthesise them in the TU, 3 s each unless stated, peak -6 dBFS; state every definition in the results file):
 S1 harmonic tones: 32 harmonics (or as many as fit under 0.45 x the sample rate) with amplitudes 1/n, at F0 = 40, 55, 82.41, 110, 220, 440, 880, 1760 and 3520 Hz.
 S2 formant tone: F0 = 146.83 Hz, harmonic amplitudes shaped by three resonances at 700, 1200 and 2600 Hz (a vowel-like spectrum).
 S3 stiff string: partial n at n x F0 x sqrt(1 + B n^2), F0 = 110 Hz, B = 0, 1e-4, 1e-3; exponential decay, T60 = 2 s.
 S4 tone plus noise: S1 at 220 Hz with white noise at SNR 40, 20 and 10 dB.
 S5 vibrato: S1 at 220 Hz with +/-50 cents at 5 Hz; glide: 220 -> 440 Hz over 1 s.
 S6 envelope: S1 at 220 Hz under a known ADSR (attack 50 ms, decay 200 ms, sustain 0.5, release 400 ms, held 1 s).
 S7 traps: a missing fundamental (harmonics 2..10 of 110 Hz), a dominant second harmonic (+12 dB), a single sine at 1 kHz.
 S8 polyphony: a dyad (220 + 277.18 Hz) and a triad (220 + 277.18 + 329.63 Hz), each note an S1 tone.
 S9 non-pitched: silence, a DC offset of 0.5, white noise, one click.

A. ANALYSIS ACCURACY. Run every signal through (1) the offline SampleAnalyzer (write a WAV to ${LOG_DIR}/ and use the real load path, as sample_load_e2e_tests.cpp does) at file rates 44.1 and 48 kHz (S1 also at 96 kHz), and (2) LiveAnalysisPipeline in Low Latency and in High Precision mode (push the samples in 512-sample blocks; Mono analysis mode, and Poly / Auto for S8). Take the frames of the steady part (skip the first 200 ms) and report per signal and front-end: F0 error in cents (median and worst frame), the share of frames that are voiced, the confidence; the number of partials found vs the number expected under Nyquist, missing ones (which harmonic numbers) and spurious ones; per partial 1..16 the frequency error in cents and the amplitude error in dB against the known value (median over frames); frame-to-frame jitter of a stable tone (standard deviation of amplitude in dB and of frequency in cents, partials 1, 4, 16); the per-partial bandwidth values (S1 should be near 0; S4 is the interesting case); for S3 the measured inharmonic deviation vs the true one and the highest partial the sieve keeps; for S4 the residual energy vs the true noise energy in dB; for S5 the RMS tracking error in cents and the lag in ms; for S6 the DetectedADSR vs the known segments (error in ms and in sustain level); for S7 the octave the F0 lands in; for S8 the F0s found; for S9 voiced must be false and the partial list empty (anything else is a result). OFFLINE vs LIVE on the same signal: the difference of the two partial sets (amplitude in dB, frequency in cents) and of scalar frame quantities both carry (the input RMS in particular: same scale?). Verdicts: bad = F0 error above 50 cents or an octave error on a clean harmonic tone inside the documented pitch range, a missing partial among the first 8 of S1, amplitude error above 3 dB or frequency error above 10 cents on the first 8 partials, voiced = true on S9, bandwidth above 0.2 on a clean tone; suspicious = half those bars, or the two front-ends differing by more than 3 dB / 10 cents; ok otherwise. Signals outside the documented range (40 Hz in Low Latency mode) are reported as information with the documented limit quoted. Write EVERY row to ${LOG_DIR}/analysis-accuracy.csv. PROBE lines: one summary per signal x front-end and one per suspicious or bad row.

B. RESYNTHESIS QUALITY. For S1 (110, 220, 440, 1760 Hz), S2, S3 (B = 1e-4), S4 (20 dB) and S6: analyse offline, then play the result through a real Processor on the MIDI note nearest the source F0 (read how a note's pitch relates to the analysed F0 in processor_midi.cpp first), held for the source's length, at default parameters, and compare the output with the source. Features, measured over the steady part: per-harmonic amplitude error in dB for harmonics 1..16 after aligning overall level; log-spectral distance over 24 log-spaced bands from 40 Hz to 16 kHz (RMS dB); spectral centroid ratio; overall level difference in dB; HARMONIC-TO-NOISE RATIO of the output vs the source (energy at the harmonic bins against the energy between them): a drop of more than 6 dB against the source means the resynthesis adds noise; the inharmonicity of the output partials for S3; the correlation of the two amplitude envelopes for S3 and S6. Repeat with: Residual Level 0 (the harmonic path alone), Partial Count 48 vs 96, Inharmonicity 0 vs 1, the same source through SIDECHAIN mode (the source on the aux bus, a held note, High Precision), and one octave up and down (which features follow the transposition: formants shift with the pitch or stay?). Verdicts: bad = log-spectral distance above 6 dB, a harmonic among the first 8 off by more than 6 dB, HNR drop above 12 dB, pitch error above 10 cents; suspicious = half those bars; ok otherwise. Write every row to ${LOG_DIR}/resynthesis-quality.csv. PROBE lines: one per source x condition.

RETURN as measurements: the summary lines of A and B plus every suspicious / bad row (not all rows: those live in the CSV files, which you list in artifacts).

${probeProcedure('audit_analysis_probes.cpp', 'audit_analysis_probes.cpp.txt', 'analysis-results.txt')}
 g. Besides analysis-results.txt, copy analysis-accuracy.csv and resynthesis-quality.csv from ${LOG_DIR}/ to ${OUT_DIR}/probes/.`, { label: 'lane:analysis', phase: 'Measure', schema: PROBE_SCHEMA, agentType: 'general-purpose', model: 'opus' })
    LANE_STEPS.push({ key: 'analysis', result: lane.analysis })
  } else {
    log('Measure: analysis-accuracy / resynthesis-quality probes skipped')
  }

  if (!SKIP.audibility && !SKIP.build) {
    lane.audibility = await agent(`${CONTEXT}\n\nYou are the PRESET and PARAMETER AUDIBILITY PROBE author of the Innexus measurement lane. You MAY edit files, but ONLY these: create plugins/innexus/tests/audit/audit_audibility_probes.cpp, append it to the innexus_tests source list in plugins/innexus/tests/CMakeLists.txt, and create files under ${OUT_DIR}/probes/.

GOAL: put numbers on two questions: (1) how different do the factory presets really sound, and does each one's distinguishing feature act at all; (2) which parameters audibly change the sound and which do nothing or next to nothing. The numbers are the result; another agent localises causes.

${PROBE_RULES}

REFERENCE SOURCES (a factory preset carries no sample, so every preset is rendered on the same three sources; build each as an injected SampleAnalysis, and as the matching audio signal for presets whose Input Source is Sidechain): R1 bright harmonic (110 Hz, 48 harmonics at 1/n, low residual), R2 vowel-like (146.83 Hz, three formants, moderate residual, slow amplitude movement over the frames), R3 percussive / inharmonic (stiff-string partials with B = 1e-3, exponential decay over 1.5 s, strong residual at the onset).

FEATURES per render (define them once in the TU and state the definitions in the results file): peak dBFS; RMS dBFS over the held part; DC offset; NaN count; the share of samples above the soft limiter's 0.85 knee; tail time after the note-off (until the 10 ms RMS envelope stays 60 dB under its maximum, ">8 s" if never); spectral centroid in Hz; a 24-band log-spaced band-energy vector in dB from 40 Hz to 16 kHz, normalised to its loudest band; the modulation depth of the level envelope (dB) and its dominant rate (Hz) over the held part; inter-channel correlation. bandDist(a, b) = RMS difference of the two band vectors in dB.

A. PRESET TABLE. For each of the 54 factory presets (INNEXUS_RESOURCES_DIR "/presets/<Category>/<name>.vstpreset") loaded into a real Processor through setState, on each reference source: from silence, note C3 (MIDI 48) at velocity 100/127 held 3 s then released, render 8 s more -> all features, plus the preset's Input Source, Voice Mode, Exciter Type, Resonance Type, Physical Model Mix and which of evolution / blend / modulators / harmonic physics / ADSR it enables. Flags per preset: silent (peak under -60 dBFS on all three sources), limiter (more than 1 % of samples above the knee), dc (|DC| above -60 dBFS), nan, long tail (still within 60 dB of its peak 8 s after the note-off: name the cause), INERT FEATURE: for every feature the preset enables or sets off its default, render the preset again with that one feature returned to its default and compare — bit-identical or under 0.5 dB bandDist with under 1 % centroid change means the preset's setting cannot be heard (typical causes to check: evolution or blend enabled while every memory slot is empty, physical-model parameters set while Physical Model Mix is 0, modulator settings while the modulator is disabled or its depth is 0, ADSR settings while ADSR Amount is 0, sidechain-only settings in a sample-mode preset). Write EVERY row to ${LOG_DIR}/preset-table.csv. PROBE lines: one summary per preset (flags, inert features) and one per category (presets, flagged, level spread in dB).

B. SAMENESS. Near-duplicate pair = bandDist under 3 dB AND centroids within 10 % AND tail times within 20 % AND level-modulation depth within 3 dB, on ALL three sources. (1) Every pair of presets -> the near-duplicate pairs, marked same-category or cross-category; all pairs to ${LOG_DIR}/preset-pairs.csv; PROBE lines: the count per category plus the twenty closest pairs overall. (2) Distance of every preset from the INIT state (the processor's defaults on the same source): presets under 3 dB bandDist with matching envelope are indistinguishable from doing nothing. (3) Per category: mean pairwise bandDist inside the category vs towards the other categories (do the categories differ in sound or only in name?). (4) Velocity response: velocity 40/127 vs 127/127 per preset on R1 -> level difference in dB and centroid ratio; flag under 6 dB together with under 5 % centroid change.

C. PARAMETER AUDIBILITY. For every registered parameter (read Controller::initialize for the list, about 88; skip Bypass): pick the CONTEXT in which the parameter is supposed to matter and write it in the setup (read plugin_ids.h, processor_params.cpp and the use sites: the impact parameters need Exciter = Impact, the bow parameters Exciter = Bow, the waveguide parameters Resonance = Waveguide, all physical parameters need Physical Model Mix above 0, Body Size / Material need Body Mix above 0, Sympathetic Decay needs Sympathetic Amount above 0, the modulator parameters need the modulator enabled with depth above 0 and the right target, Rate vs Note Value depend on Rate Sync, the evolution parameters need Evolution enabled AND at least two occupied memory slots, the blend weights need Blend enabled and occupied slots, Morph needs Freeze, Responsiveness / Latency Mode / Analysis Feedback and its decay need sidechain mode with input, the ADSR parameters need ADSR Amount above 0, Residual Brightness / Transient Emphasis need Residual Level above 0 and a source with residual energy, Memory Slot / Capture / Recall act as triggers: measure the recalled sound). In that context render one note with the parameter at normalized 0, 0.25, 0.5, 0.75 and 1 (discrete parameters: every step), everything else fixed, on R1 and R3. Across the renders report: max level change (dB), max centroid change (%), max tail-time change (%), max bandDist (dB), max change of the level-modulation depth and rate, of the stereo correlation and of the fundamental (cents), and whether the output is bit-identical over the whole range. Verdict: bad = bit-identical on both sources in its own context (a dead parameter); suspicious = every metric under 1 dB / 5 % / 5 % / 1 dB / 5 cents on both (inaudibly weak), or a jump above 12 dB between two adjacent steps; ok otherwise. Also record per parameter whether changing it while a note is held (no new note) alters the sounding voice (applied live vs only at the next note-on): that column is information for the report, not a verdict. Write every row to ${LOG_DIR}/param-audibility.csv. PROBE lines: one per suspicious or bad parameter and one summary line with the counts of ok / suspicious / bad.

RETURN as measurements: the per-preset summaries, the flagged presets, the sameness lines of B, and the suspicious / bad parameter lines plus the summary (not all rows: those live in the CSV files, which you list in artifacts).

${probeProcedure('audit_audibility_probes.cpp', 'audit_audibility_probes.cpp.txt', 'audibility-results.txt')}
 g. Besides audibility-results.txt, copy preset-table.csv, preset-pairs.csv and param-audibility.csv from ${LOG_DIR}/ to ${OUT_DIR}/probes/.`, { label: 'lane:audibility', phase: 'Measure', schema: PROBE_SCHEMA, agentType: 'general-purpose', model: 'opus' })
    LANE_STEPS.push({ key: 'audibility', result: lane.audibility })
  } else {
    log('Measure: preset / parameter audibility probes skipped')
  }

  if (!SKIP.tidy) {
    lane.tidy = await laneStep('tidy', `STEP: clang-tidy for the Innexus area, four jobs, Windows PowerShell script only (never the .sh on Windows):
  pwsh -NoProfile -File ./tools/run-clang-tidy.ps1 -Target innexus -BuildDir build/windows-ninja -Jobs 4 -Full > ${LOG_DIR}/tidy.log 2>&1   # tidy-full-ok: the audit measures every Innexus TU
(Keep the "# tidy-full-ok: ..." comment: without it tools/hooks/guard-tidy-scope.js blocks -Full; without -Full the script analyzes only uncommitted changes.)
If build/windows-ninja/compile_commands.json is missing, generate it first with the ninja preset (${CMAKE} --list-presets shows the name; see .claude/skills/clang-tidy-setup/SKILL.md). If a warning looks like a phantom (header not found, unknown flag), reconfigure that preset once and re-run only the one TU. Set a wall-clock ceiling: if the script has produced no new output for 20 minutes, find the stuck clang-tidy process (Get-CimInstance Win32_Process | where CommandLine -match clang-tidy), record the TU, kill it, and report that TU as kind=tidy "hung". Report warnings CLUSTERED by check name + file (one item per cluster, count in text, one example line), kind=tidy. The total count goes in summary. Also count the NOLINT markers under plugins/innexus/src by check name (grep): a marker whose reason says clang-tidy "cannot resolve includes" points at a broken tidy configuration, not at clean code.`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })
  } else {
    log('Measure: clang-tidy skipped by args.skip.tidy')
  }

  if (!SKIP.portability) {
    lane.portability = await laneStep('portability', `STEP: the Linux/macOS compile proxy. tools/check-portability.js syntax-checks TUs with GCC in WSL; with no arguments it only takes files changed vs origin/main (nothing, on a clean tree), so pass EXPLICIT files: every .cpp under plugins/innexus/src and plugins/innexus/tests (build the list with git ls-files and pass it on one command line), output captured to ${LOG_DIR}/portability.log. Read its header comment first for any extra flags. It boots WSL; leave WSL running afterwards (never shut it down or restart it: the user runs WSL in other sessions). Report every error/warning it prints for plugins/innexus/ or an included dsp/ or plugins/shared/ header as kind=portability with file:line. Note in summary what the script does NOT cover (it is a compile proxy, not fast-math semantics).`, { schema: LANE_SCHEMA, agentType: 'build-test-runner', model: 'sonnet' })
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
// Research — analysis / resynthesis algorithms and the physical models vs the
// literature. Web-bound, so it runs beside everything else, four agents at a
// time, and is awaited before the critic. Each topic agent reads the code for
// the ACTUAL numbers first.
// ---------------------------------------------------------------------------
const RESEARCH_TOPICS = [
  {
    key: 'yin-pitch',
    title: 'YIN fundamental-frequency estimation',
    files: 'dsp/include/krate/dsp/processors/yin_pitch_detector.h, its configuration in plugins/innexus/src/dsp/sample_analyzer.cpp and live_analysis_pipeline.cpp (window sizes, thresholds, pitch range), plugins/innexus/src/dsp/dual_stft_config.h (kHighPrecisionYinWindowSize and the minimum-F0 comments)',
    question: 'Does the implementation follow de Cheveigne & Kawahara, "YIN, a fundamental frequency estimator for speech and music" (JASA 2002): the difference function, the cumulative mean normalized difference, the absolute threshold with the first-dip rule, parabolic interpolation of the lag, the best-local-estimate step? Which steps are omitted or changed (an FFT-based autocorrelation instead of the difference function is a known variant: is it equivalent as coded)? Is the confidence / voiced decision derived from the aperiodicity as the paper defines it? Given each configured window size and the maximum lag searched, what is the lowest F0 that can be detected at 44.1 and 48 kHz, and does it match the documented floors (about 40 Hz offline and in High Precision mode, 80-100 Hz in Low Latency mode)? Would pYIN (Mauch & Dixon 2014) change the octave-error behaviour the code works around?',
  },
  {
    key: 'multi-pitch',
    title: 'Multi-pitch detection, subharmonic validation and the polyphony guard',
    files: 'dsp/include/krate/dsp/processors/multi_pitch_detector.h, multi_source_sieve.h, subharmonic_validator.h, the two-pass polyphony guard in plugins/innexus/src/dsp/sample_analyzer.cpp, runPolyphonicAnalysis in plugins/innexus/src/dsp/live_analysis_pipeline.cpp (~517-605), plugins/innexus/docs/polyphonic-f0-design-notes-2026-07.md',
    question: 'The code cites Klapuri for iterative multi-F0 estimation (harmonic summation, then cancellation of the detected sound with a spectral-envelope model; Klapuri, "Multiple fundamental frequency estimation by summing harmonic amplitudes", ISMIR 2006, and the 2003 IEEE TSAP paper) and Hermes for subharmonic summation (JASA 1988). Do the coded salience function, the harmonic weights, the cancellation step (a flat 0.85 x amplitude subtraction was noted by the prior audit) and the stopping rule match those sources? Is assigning each partial to exactly one source (the multi-source sieve) defensible when harmonics of two notes coincide (a fifth, an octave)? Is "F0 instability" a sound criterion for deciding that a sample is polyphonic?',
  },
  {
    key: 'peak-estimation',
    title: 'Spectral peak estimation: window, interpolation, dual resolution',
    files: 'the peak detection and interpolation in dsp/include/krate/dsp/processors/partial_tracker.h, dsp/include/krate/dsp/core/window_functions.h (the Blackman-Harris coefficients), dsp/include/krate/dsp/primitives/stft.h, plugins/innexus/src/dsp/dual_stft_config.h, how the short and the long spectrum are combined in sample_analyzer.cpp and live_analysis_pipeline.cpp',
    question: 'Which Blackman-Harris window is coded (the 4-term -92 dB window of Harris 1978? exact coefficients) and is its main-lobe width (8 bins) respected when two partials are closer than that (low F0 on the 1024-point window)? Is the quadratic (QIFFT) peak interpolation done on LOG magnitude as Smith & Serra (PARSHL, 1987) and Abe & Smith ("Design criteria for simple sinusoidal parameter estimation based on quadratic interpolation of FFT magnitude peaks", 2004) require, and what are the published bias bounds for this window with and without zero padding vs what the code can achieve? Is the amplitude corrected for the window\'s coherent gain? Does the code really use the long window for the low partials and the short one for the upper partials as the dual-resolution design claims (compare multiresolution sinusoidal modelling: Levine, Verma & Smith 1998), or is one spectrum ignored?',
  },
  {
    key: 'partial-tracking',
    title: 'Partial tracking, the harmonic sieve and inharmonicity',
    files: 'dsp/include/krate/dsp/processors/partial_tracker.h (matching cost, birth / death, the sieve and its tolerance), harmonic_types.h (harmonicIndex, relativeFrequency, inharmonicDeviation, bandwidth, stability, age), the inharmonicity handling in harmonic_oscillator_bank.h',
    question: 'How does the Hungarian (Kuhn-Munkres) assignment with its coded cost function compare with the greedy frame-to-frame matching of McAulay & Quatieri ("Speech analysis/synthesis based on a sinusoidal representation", IEEE TASSP 1986) and of Serra & Smith (SMS, 1990), incl. track birth, death and "sleeping" tracks? Is the harmonic-sieve tolerance (its width as a function of the harmonic number n and of the frequency resolution) consistent with the literature on harmonic sieves (Duifhuis, Willems & Sluyter 1982) AND with real inharmonicity: stiff strings follow f_n = n f0 sqrt(1 + B n^2) (Fletcher 1964; Fletcher & Rossing), so the deviation grows roughly as n^3 while the prior audit noted a tolerance growing as sqrt(n) with no absolute floor? Which instruments (piano bass, bells, bars) lose partials under the coded tolerance?',
  },
  {
    key: 'harmonic-model',
    title: 'Harmonic model building: smoothing, normalisation, descriptors',
    files: 'dsp/include/krate/dsp/systems/harmonic_model_builder.h, dsp/include/krate/dsp/processors/harmonic_frame_utils.h, the frame fields in harmonic_types.h (globalAmplitude, spectralCentroid, brightness, noisiness, f0Confidence), plugins/innexus/src/dsp/spectral_decay_envelope.h',
    question: 'Is the dual-timescale smoothing of partial amplitudes and frequencies a recognised technique (compare the amplitude / frequency interpolation and track smoothing in SMS and in Loris), and what does it do to attack transients and to vibrato depth (how many dB / cents are lost at the coded time constants)? Are the spectral descriptors computed by their standard definitions (spectral centroid; noisiness / harmonic-to-noise as in Peeters, "A large set of audio features for sound description", 2004)? Is normalising partial amplitudes by a global amplitude and re-applying it at synthesis lossless as coded?',
  },
  {
    key: 'residual-model',
    title: 'Residual (stochastic) model and bandwidth-enhanced partials',
    files: 'dsp/include/krate/dsp/processors/residual_analyzer.h, spectral_coring_estimator.h, residual_synthesizer.h, residual_types.h (16 bands, transient flag), the per-partial bandwidth path in harmonic_oscillator_bank.h, the residual handling in plugins/innexus/src/dsp/sample_analyzer.cpp and live_analysis_pipeline.cpp',
    question: 'Serra & Smith (SMS, "Spectral modeling synthesis", CMJ 1990) obtain the residual by subtracting the resynthesised deterministic part and model it as filtered noise with a line-segment spectral envelope. Which residual estimate does the code use (time-domain subtraction, spectral subtraction, "spectral coring" between the harmonic peaks) and is the estimate unbiased for a tone in white noise? Are 16 bands with the coded edges enough resolution compared with ERB / Bark-band envelopes used in the literature (Goodwin 1996; Levine & Smith 1998)? How are transients handled compared with transient modelling synthesis (Verma & Meng 2000)? For the per-partial bandwidth: Fitz & Haken ("Bandwidth enhanced sinusoidal modeling in Lemur", ICMC 1995; Loris) define the noise-modulated oscillator so that total energy is preserved as bandwidth rises; does the coded modulator preserve energy (the prior audit said it was not variance-normalised), and is the bandwidth estimate itself as Loris defines it?',
  },
  {
    key: 'oscillator-bank',
    title: 'Magic-circle oscillator bank and frame-to-frame interpolation',
    files: 'dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h (read END TO END: the recurrence, epsilon, amplitude and frequency smoothing, loadFrame, the phase seeding, the Nyquist handling, detune / pan / inharmonicity), plugins/innexus/src/processor/processor.cpp broadcastFrameToVoices (~2195)',
    question: 'Is the oscillator the "magic circle" / modified coupled form (Gordon & Smith, "A sine generator algorithm for VLSI applications", ICMC 1985; Smith, "Physical Audio Signal Processing", Digital Sinusoid Generators) with epsilon = 2 sin(pi f / fs), and what do those sources state about its amplitude stability, its quadrature error and its behaviour when epsilon changes every sample (frequency modulation, detune, pitch bend)? Is there a stability bound the code must clamp to, and does it? McAulay & Quatieri interpolate phase with a cubic polynomial between frames to keep partials phase-coherent; the code keeps the oscillator phase and smooths frequency and amplitude instead: what is lost (waveform shape, transient sharpness, phase relations between partials) and is that the standard trade-off of oscillator-bank additive synthesis? How should a partial that crosses Nyquist be faded (the coded rule vs common practice)?',
  },
  {
    key: 'morph-freeze-evolution',
    title: 'Timbre morphing, blending and spectral freeze',
    files: 'the morph code in plugins/innexus/src/processor/processor.cpp (~1020-1086) with dsp/include/krate/dsp/processors/harmonic_frame_utils.h (lerp of frames), plugins/innexus/src/dsp/harmonic_blender.h, evolution_engine.h, the confidence-gated freeze (~1087-1164) and spectral_decay_envelope.h, the harmonic filter mask (~975)',
    question: 'Published timbre morphing interpolates matched partials in LOG frequency and (often) in dB or with perceptual warping, and treats unmatched partials with birth / death ramps (Tellman, Haken & Holloway, "Timbre morphing of sounds with unequal numbers of features", JAES 1995; Fitz & Haken on Loris morphing; Caetano & Rodet 2013). Does the code interpolate linearly in Hz and in linear amplitude, by array index or by harmonic number, and what artefacts does the difference cause (a dip in loudness at the midpoint, beating, partials that jump)? Is blending N sources by weighted linear amplitude sums defensible? For the freeze: is holding the last frame while confidence is low, with an exponential spectral decay, comparable to published "spectral freeze" / sustain techniques, and are the coded thresholds (confidence 0.3, hysteresis 0.05, recovery 7 ms) in a sensible range?',
  },
  {
    key: 'envelope-detection',
    title: 'ADSR detection from the sample',
    files: 'plugins/innexus/src/dsp/envelope_detector.h, its call site in plugins/innexus/src/dsp/sample_analyzer.cpp, the "DetectedADSR" message in plugins/innexus/src/processor/processor.cpp (~2116)',
    question: 'How do the coded attack / decay / sustain / release estimates compare with published temporal descriptors: log-attack time and the fixed-threshold (10 % - 90 %) vs adaptive "weakest effort" method of Peeters (2004), the energy-envelope segmentation of Jensen (1999), Lerch ("An introduction to audio content analysis")? Is an RMS contour at the coded frame rate fine enough for attacks of a few milliseconds? Are the steady-state thresholds applied to a normalised contour (the prior audit found raw RMS)? What should the detector return for sounds with no sustain (percussive) or no decay (organ-like)?',
  },
  {
    key: 'modal-resonator',
    title: 'Modal resonator bank driven by analysed partials',
    files: 'dsp/include/krate/dsp/processors/modal_resonator_bank.h (the resonator topology, the damping law and its units, stretch, scatter, getFeedbackVelocity / getControlEnergy / getPerceptualEnergy), how Innexus sets it up in plugins/innexus/src/processor/processor_midi.cpp (initVoiceForNoteOn) and processor.cpp (kResonanceDecay / Brightness / Stretch / Scatter)',
    question: 'Is the frequency-dependent damping (the CHANGELOG says Chaigne-Lambourg) of the form R = b1 + b3 f^2 used by Chaigne & Lambourg (2001) and Chaigne & Askenfelt (1994), with consistent units between the law and the Decay / Brightness parameters (the Membrum audit, specs/membrum-audit/report.md section 5, already ruled on this header: read its verdict and what was changed since)? Is tuning a modal bank to the ANALYSED partial frequencies and amplitudes of an arbitrary source a recognised technique (modal synthesis from analysis: Adrien 1991; van den Doel & Pai; Bilbao), and what does it assume about the source that a sustained or noisy source violates? Is the Stretch mapping comparable to stiffness inharmonicity?',
  },
  {
    key: 'waveguide-string',
    title: 'Waveguide string resonance',
    files: 'dsp/include/krate/dsp/processors/waveguide_string.h, its setup in plugins/innexus/src/processor/processor_midi.cpp and the per-voice use in processor.cpp (~1650-1776), the Stiffness and Pick Position parameters',
    question: 'Is it a correct digital waveguide / extended Karplus-Strong string (Karplus & Strong 1983; Jaffe & Smith 1983; Smith, "Physical Audio Signal Processing"; Valimaki et al., "Discrete-time modelling of musical instruments", 2006): loss filter, dispersion allpass cascade for stiffness (Van Duyne & Smith; Abel, Valimaki & Smith 2010; Rauhala & Valimaki 2006), fractional-delay tuning (Thiran allpass or Lagrange) with the phase delay of the loop filters compensated, pick-position comb? What tuning error remains across the keyboard and with stiffness (the Membrum audit, specs/membrum-audit/report.md section 5, reviewed this header: read its verdict first)? Is a soft clipper inside the loop consistent with any published passive-nonlinearity string model, and does it alter pitch or decay?',
  },
  {
    key: 'exciters',
    title: 'Impact (mallet) and bow exciters',
    files: 'dsp/include/krate/dsp/processors/impact_exciter.h, bow_exciter.h, their parameters in plugins/innexus/src/plugin_ids.h (806-809, 820-823), the mallet-choke envelope in plugins/innexus/src/processor/innexus_voice.h and processor.cpp, the bow branch in processor.cpp (~1660-1680: ADSR as bow force, resonator energy fed back)',
    question: 'Impact: is the contact a nonlinear power-law force (Hertz; Hunt & Crossley 1975; Chaigne & Doutaut 1997; Avanzini & Rocchesso 2001) whose contact time shortens as velocity and hardness rise, so harder hits are brighter, and does Mass act as in those models? Bow: is the friction characteristic one of the published forms (McIntyre, Schumacher & Woodhouse 1983; Smith\'s bow table in STK; Serafin 2004; the Bristol thermal model), are the Schelleng (1973) minimum / maximum bow-force limits reflected in how Pressure, Speed and Position interact, and is 2x oversampling a recognised remedy for the friction nonlinearity\'s aliasing? Is choking a ringing resonator on a re-strike physically motivated? (The Membrum audit, specs/membrum-audit/report.md section 5, reviewed both exciter headers: read its verdicts first and report what changed.)',
  },
  {
    key: 'body-resonance',
    title: 'Body resonance: modal bank plus FDN',
    files: 'dsp/include/krate/dsp/processors/body_resonance.h (the three reference body presets and their mode tables, the crossover, the 4-line Hadamard FDN with delays 11 / 17 / 23 / 31 samples, the Size and Material mappings)',
    question: 'Are the coded mode frequencies, Q values and gains of the small / medium / large bodies in the range of measured instrument bodies (violin: A0 near 275 Hz, B1- / B1+ near 450-550 Hz; guitar: air mode near 100 Hz, top-plate mode near 200 Hz; Fletcher & Rossing; Jansson, "Acoustics for violin and guitar makers")? Is a short FDN (Jot & Chaigne 1991; Schlecht & Habets on FDN design: unitary feedback, mutually prime delays, decay per line) an accepted model for the dense high-frequency response of a body, and are delays of 11-31 samples long enough to act as anything but a comb / EQ? Compare with commuted synthesis and measured body impulse responses (Smith; Karjalainen, Valimaki & Tolonen 1998).',
  },
  {
    key: 'sympathetic-resonance',
    title: 'Sympathetic resonance',
    files: 'dsp/include/krate/dsp/systems/sympathetic_resonance.h and its use in plugins/innexus/src/processor/processor.cpp (~1911: fed with the mono sum of all voices, added to both channels) and processor_midi.cpp (how resonators are added per note)',
    question: 'How do published models produce sympathetic vibration (coupled strings through a bridge admittance: Weinreich 1977; Smith on coupled strings; Bank & Sujbert; Lehtonen, Penttinen, Rauhala & Valimaki, "Analysis and modeling of piano sustain-pedal effects", JASA 2007; Le Carrou et al. on the concert harp), and is driving a pool of resonators tuned to the played notes\' partials with the instrument\'s OWN output a comparable signal flow, or does it mostly re-amplify the partials that are already sounding? What Amount and Decay ranges do the sources imply? (The Membrum audit, specs/membrum-audit/report.md section 5, reviewed this header for cross-pad coupling: read its verdict first.)',
  },
  {
    key: 'harmonic-physics',
    title: 'Harmonic physics: warmth, coupling, stability, entropy',
    files: 'plugins/innexus/src/dsp/harmonic_physics.h, its call in plugins/innexus/src/processor/processor.cpp applyHarmonicPhysics (~2170), specs/_archive_/122-harmonic-physics/spec.md and research.md, specs/Innexus-emergent-harmonics-roadmap.md',
    question: 'This block is a creative model, not a physical one; check only what its comments, its spec and the manual CLAIM. Does "coupling" conserve energy as the comments say (nearest-neighbour energy sharing: is the sum of squared amplitudes preserved by the coded update)? Does "warmth" (a tanh on partial amplitudes) behave like the saturation it is described as, or does it only compress the spectrum\'s dynamic range? Do "stability" (inertia) and "entropy" (decay) match the first-order dynamics the spec describes, independent of the frame rate (a per-frame coefficient behaves differently at hop 512 and hop 2048, and at other sample rates)? Report claims the text makes that the code does not deliver.',
  },
  {
    key: 'level-and-limiter',
    title: 'Polyphony compensation and the always-on soft limiter',
    files: 'plugins/innexus/src/processor/processor.cpp (~1879-1970: the 1/sqrt(N) compensation, master gain, the finiteness guard, the tanh limiter above the 0.85 knee), the level handling in harmonic_oscillator_bank.h and the Harmonic / Residual level parameters',
    question: 'Is scaling the voice sum by 1/sqrt(active voices) the right law (it assumes uncorrelated voices; unison or octave notes of the SAME analysed source are highly correlated), and what do instruments in the literature and in practice do (fixed headroom, no compensation)? Note that the factor changes abruptly when a voice starts or ends: what is the audible effect on the notes already sounding? For the limiter: what distortion and aliasing does a memoryless tanh above a knee add to a sum of sines, compared with a look-ahead limiter or an oversampled / ADAA soft clipper (Parker, Zavalishin & Le Bihan, "Reducing the aliasing of nonlinear waveshaping using continuous-time convolution", DAFx 2016)? Is an uncontrollable output nonlinearity acceptable in an instrument whose purpose is faithful resynthesis?',
  },
]

async function runResearch() {
  const priorNote = `PRIOR WORK: (1) plugins/innexus/docs/audit-2026-07-implementation-plan.md: its work items WI-5, WI-8 and WI-11 .. WI-15 are analysis / synthesis-theory claims (epsilon stability bound, Loris bandwidth normalisation, inputRms scales, sieve tolerance and its growth law, QIFFT on linear magnitude, the YIN window vs the F0 floor), each with a verdict; code comments citing WI-n show what was done about them. (2) plugins/innexus/docs/polyphonic-f0-design-notes-2026-07.md records the design choices of the polyphony guard. (3) specs/membrum-audit/report.md section 5 ("Physical-model review") holds literature verdicts on the SHARED physical-modelling headers (modal_resonator_bank.h, waveguide_string.h, impact_exciter.h, bow_exciter.h, sympathetic_resonance.h) and specs/membrum-audit/fix-plan.md says what is being changed; git log --oneline --grep "audit phase" -- dsp/ shows what already landed. Read the entries for your topic FIRST, then check whether the code changed since and report in prior_verdict what was ruled then and whether it still applies.`
  const method = `METHOD: (1) Read the code and state precisely what it does, with the actual numbers: never guess a coefficient. (2) Use web search + fetch to find authoritative sources and OPEN them. WEIGHT SOURCE QUALITY: textbooks (Smith's CCRMA online books "Spectral Audio Signal Processing" and "Physical Audio Signal Processing"; Zolzer, "DAFX"; Roads, "The Computer Music Tutorial"; Fletcher & Rossing, "The Physics of Musical Instruments"; Bilbao, "Numerical Sound Synthesis") and peer-reviewed papers (JASA, JAES, IEEE TASLP, CMJ, DAFx, ICMC) are ground truth; Loris / SMS-tools / STK / Faust / vendor documentation is corroboration; blogs and forums can never be the sole basis for a contradicted verdict. (3) Return ONE claim per distinct statement (a topic usually yields 2-5): verdict confirmed / contradicted / oversimplified / unsupported (the code cites a source that does not say this, or no source exists) / nuanced (defensible, the literature differs in detail), with the correct form and the sources. (4) State the audible implication, a severity (none when confirmed or inaudible), and the concrete fix with the test that would pin the coded values to the published ones. A deliberate, documented simplification is still reported: say that it is deliberate and where that is written.`
  const curated = await inChunks(RESEARCH_TOPICS, 4, t =>
    agent(`${CONTEXT}\n\n=== ALGORITHM AND PHYSICAL-MODEL REVIEW AGAINST THE LITERATURE ===\nTopic: ${t.title}\nFiles to read FIRST: ${t.files}\n\nQuestions: ${t.question}\n\n${priorNote}\n\n${method}`, {
      label: `research:${t.key}`,
      phase: 'Research',
      schema: RESEARCH_SCHEMA,
      model: 'opus',
    }).then(r => (r ? r.claims.map(c => ({ ...c, topic: t.key })) : null)),
  )
  const failed = RESEARCH_TOPICS.filter((_, i) => !curated[i]).map(t => t.key)
  if (failed.length) log(`WARNING: research topics failed: ${failed.join(', ')} — those claims are UNCHECKED`)

  const gaps = await agent(`${CONTEXT}\n\nThese topics are already being checked against the literature: ${RESEARCH_TOPICS.map(t => t.title).join('; ')}.\n\nScan the analysis and modelling code (plugins/innexus/src/dsp/*, plugins/innexus/src/processor/processor.cpp and innexus_voice.h, and the KrateDSP headers Innexus includes: grep -rhoE '#include <krate/dsp/[^>]+>' plugins/innexus/src) and the manual (plugins/innexus/MANUAL.md) for ANY OTHER signal-processing, psychoacoustic or physical claim, constant, formula or cited paper — in a comment, a constant name or the manual — that the list above does not cover and that is worth verifying (examples of the kind: the pre-processing chain's high-pass and transient suppression, the noise gate, window overlap / COLA of the STFT, the stereo-spread and detune laws, tempo-sync note values, the MPE mappings). Return up to 8 of the highest-value ones, each with file:line and the question to answer.`, {
    label: 'research:gaps',
    phase: 'Research',
    schema: CLAIM_LIST_SCHEMA,
    model: 'sonnet',
  })
  const extraTopics = ((gaps && gaps.claims) || []).slice(0, 8)
  const extra = await inChunks(extraTopics, 4, c =>
    agent(`${CONTEXT}\n\n=== ALGORITHM AND PHYSICAL-MODEL REVIEW AGAINST THE LITERATURE (gap fill) ===\nClaim: ${c.title}\nWhere: ${c.files}\nQuestion: ${c.question}\n\n${priorNote}\n\n${method}`, {
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
else log(`Research started: ${RESEARCH_TOPICS.length} topics, four at a time, beside the other phases`)

// ---------------------------------------------------------------------------
// Inventory (Haiku) + Understand (Sonnet) — concurrent, barrier before Find
// because every finder receives both digests.
// ---------------------------------------------------------------------------
const INVENTORIES = [
  {
    key: 'params',
    prompt: `Cross-reference the PARAMETER plumbing. Build one table row per ID in plugins/innexus/src/plugin_ids.h (the ParameterIds enum; include IDs that are declared but may be unused, such as kAnalysisModeId 502, and the reserved ID 600). Sides: (1) plugin_ids.h — the ID and the range / default its comment states; (2) Controller::initialize in plugins/innexus/src/controller/controller.cpp (~241-893) — the registration (parameter class, min / max / default, step count, units, flags, title, string-list entries); (3) plugins/innexus/src/parameters/innexus_params.h and note_value_ui.h — the mapping helpers and what they say the plain range is; (4) the processor's atomic in plugins/innexus/src/processor/processor.h (its initializer = the default, and whether it holds a NORMALIZED or a PLAIN value); (5) Processor::processParameterChanges in plugins/innexus/src/processor/processor_params.cpp — the case branch, the conversion applied before the store, which point of the automation queue is used; (6) the USE SITE: where the atomic is read in processor.cpp / processor_midi.cpp / processor_state.cpp and the denormalization applied there (range, law); (7) getState and setState in plugins/innexus/src/processor/processor_state.cpp — the position in the stream, the type written, the clamp on read; (8) Controller::readComponentState in plugins/innexus/src/controller/controller_state.cpp — the position at which the same field is read and the conversion to a normalized value; (9) tools/innexus_preset_format.h (InnexusPresetState::serialize) — the position, the type and the default the generator writes; (10) plugins/innexus/src/controller/controller_param_display.cpp getParamStringByValue — the displayed range / units; (11) resources/editor.uidesc control-tags — run  node tools/uidesc-inventory.js plugins/innexus/resources/editor.uidesc --out ${LOG_DIR}/uidesc-inventory.json  (create ${LOG_DIR} first) and read the JSON. Report mismatches: an ID declared but never registered, registered but never consumed by the processor, consumed but never read by any DSP code, not saved, saved but not loaded, or read at a DIFFERENT stream position by the processor, the controller and the generator (number the fields on all three sides: one skipped or extra read shifts every later field); a default that differs between the plugin_ids.h comment, the registration, the processor's initializer, the setState fallback and the generator (the Master Gain default is one to check); a conversion on one side that is not the inverse of the other (stored plain, read back as normalized); a displayed range that differs from the DSP's denormalization; a registered automatable parameter with no control in the editor, or a control-tag that is not a known ID; a clamp on read that is narrower or wider than the registered range.`,
  },
  {
    key: 'types',
    prompt: `Cross-reference the ENUM <-> STRING <-> SWITCH tables. Enums and discrete sets: InputSource, LatencyMode, HarmonicFilterType (5), ExciterType (3) in plugins/innexus/src/plugin_ids.h; EvolutionMode (plugins/innexus/src/dsp/evolution_engine.h), ModulatorWaveform (5) and ModulatorTarget (3) (dsp/harmonic_modulator.h); Krate::DSP::AnalysisMode (Mono / Poly / Auto: find its definition under dsp/include/krate/dsp/) vs kAnalysisModeId; the resonance type (Modal / Waveguide: two options stored as a normalized float, an int activeResonanceType_ in innexus_voice.h), the voice mode (Mono / 4 / 8), the partial count (48 / 64 / 80 / 96), the memory slot (8), the tempo-sync note values (21 entries; core/note_value.h and src/parameters/note_value_ui.h), the modulator range start / end (1..96), Krate::DSP::ADSRStage as it crosses to the UI in DisplayData, the preset category list (plugins/innexus/src/preset/innexus_preset_config.h: seven). For each: the StringListParameter entries and step count in Controller::initialize, the normalized <-> index conversion on EVERY side (processor_params.cpp, the use site in processor.cpp / processor_midi.cpp, processor.h getters such as getHarmonicFilterTypeFromParam and getActivePartialCount, processor_state.cpp, controller_state.cpp, controller_view_sync.cpp visibility logic, tools/innexus_preset_generator.cpp constants such as kExciterImpact = 0.5f: same multiplier and rounding everywhere? a two-option list denormalized with x 2, or a three-option list with x 1, is the classic defect), the load clamps, every switch over the enum (missing case, fallthrough, a default that hides a new member: the exciter switch in processor.cpp ~1660 uses default for Residual), and the names in plugins/innexus/MANUAL.md, docs/index.html, docs/innexus_signal_flow.dot and code comments (plugin_ids.h says of Bow "Reserved for Phase 4"). Report: enum members without a branch; list lengths that disagree with the enum count; names or orders that differ between enum, UI menu, manual and docs; a load clamp narrower or wider than the enum; two sides that round a normalized value to different indices; an int state field (input source, latency mode, filter type, selected slot) written as one type and read as another.`,
  },
  {
    key: 'presets-versions',
    prompt: `Cross-reference PRESETS, VERSIONS and DOCS. (1) List the factory presets per category under plugins/innexus/resources/presets/ (54 files in seven folders today) and compare with the category list in plugins/innexus/src/preset/innexus_preset_config.h, the preset definitions in tools/innexus_preset_generator.cpp (names, categories, count: does createAllPresets produce exactly the files on disk, no orphan file and no missing one?), the preset list in plugins/innexus/MANUAL.md and docs/index.html, and the install rule in plugins/innexus/CMakeLists.txt (krate_plugin_install_presets) vs the directories the runtime scans. Per preset, from the generator source: its Input Source, whether it sets a sample path or any memory slot, and which features it turns on. (2) State format: the version written by getState and accepted by setState (plugins/innexus/src/processor/processor_state.cpp), by Controller::readComponentState (controller_state.cpp), by loadComponentStateWithNotify (controller_presets.cpp) and kStateVersion in tools/innexus_preset_format.h; the JSON snapshot version in plugins/innexus/src/dsp/harmonic_snapshot_json.h; the field ORDER on the writer vs each reader (list the fields in order for getState, setState, readComponentState and the generator's serialize, side by side, and mark every position where they differ); the version each test file name claims (tests/unit/vst/test_state_v6.cpp .. test_state_v10.cpp) vs what those tests actually write; the versions named in specs/_architecture_/innexus-plugin.md and specs/_architecture_/plugin-state-persistence.md. (3) version.json vs the generated version header vs the top CHANGELOG.md entry vs docs/index.html vs installers/ metadata vs resources/au-info.plist.in and resources/auv3 vs src/update/innexus_update_config.h vs the newest tag (git ls-remote --tags origin "refs/tags/innexus/*"). (4) MANUAL.md, docs/index.html, README.md and CHANGELOG claims that name a number or feature (partial counts, voice counts, memory slots, residual bands, window sizes, latency figures, pitch range, parameter ranges, modulator waveforms and targets, exciter and resonator types, preset count and categories) vs the constants in code. (5) plugins/innexus/CLAUDE.md, .claude/skills/innexus/SKILL.md and specs/_architecture_/innexus-plugin.md statements vs the filesystem and the code (the src skeleton incl. an engine/ directory, test directories, the signal path, parameter ID sections, bus configuration). (6) Code comments that cite a spec path (specs/<nnn>-.../spec.md): does the path exist, or has the spec moved to specs/_archive_/? Report every disagreement with both values verbatim.`,
  },
]

const SUBSYSTEMS = [
  {
    key: 'analysis-offline',
    prompt: `Map the OFFLINE ANALYSIS front-end: plugins/innexus/src/dsp/sample_analyzer.h and sample_analyzer.cpp read END TO END (~650 lines), sample_analysis.h, envelope_detector.h, dual_stft_config.h, pre_processing_pipeline.h, and how the processor drives them (Processor::loadSample ~2158, checkForNewAnalysis ~2066-2157, cleanupPendingDeletion, the "LoadSampleFile" handler in processor_messages.cpp, the loadSample call inside setState). Cover: the WAV decode (dr_wav: channel downmix, bit depths, the file's sample rate vs the host rate, length limits, a failed decode), startAnalysis / cancel / joinThread / takeResult / isComplete (which thread calls each, what the atomics guard, what happens when a second file arrives while the first is analysed, at plugin destruction, in terminate), analyzeOnThread stage by stage (pre-processing, YIN and its window, the short and the long STFT and what each is actually used for, the partial tracker, the model builder, the residual analysis, the two-pass polyphony guard and its trigger criterion, the envelope detector, the sustain-loop region), every quantity stored in SampleAnalysis (frames, residual frames, hop size, FFT size, sample rate, detected ADSR, file path) and its units, the publication to the audio thread (the atomic exchange, pendingDeletion_ holding ONE pointer: what if two analyses are superseded before setActive runs?), and the messages sent on completion.`,
  },
  {
    key: 'analysis-live',
    prompt: `Map the LIVE (sidechain) front-end: plugins/innexus/src/dsp/live_analysis_pipeline.h and live_analysis_pipeline.cpp read END TO END (~605 lines), pre_processing_pipeline.h, dual_stft_config.h, and the processor side in plugins/innexus/src/processor/processor.cpp: the source-switch detection (~379), the sidechain downmix into the fixed 8192-sample sidechainBuffer_ (~400), the analysis-feedback mix (~430-470) and the feedback capture at the end of process (~1972-2015), the hand-over of live frames (currentLiveFrame_, currentLiveResidualFrame_, currentLivePolyFrame_), the responsiveness forwarding (~373), the confidence-gated freeze with the spectral decay (~1087-1164). Cover: prepare / reset / setLatencyMode (what is reallocated, on which thread, while audio runs?), pushSamples (buffering, how many hops one host block can trigger, the work done per hop: this is the audio-thread cost), runAnalysis and its mono and polyphonic branches, crossfadeFrames (mono <-> poly, by index or by frequency), hasNewFrame / consumeFrame, the residual path and setResidualEnabled, what happens with a block larger than 8192 samples, a mono or missing sidechain bus, a sample rate change, silence, the latency each mode adds and whether anything reports it to the host, and every place where the live path computes a quantity differently from the offline analyzer (list them: RMS scale, window use, smoothing).`,
  },
  {
    key: 'shared-dsp-analysis',
    prompt: `Map the SHARED ANALYSIS DSP Innexus wraps (dsp/include/krate/dsp/): processors/yin_pitch_detector.h (read END TO END), partial_tracker.h (END TO END, ~1100 lines: peak detection and interpolation, the Hungarian matcher, the harmonic sieve and its tolerance, track birth / death, the fixed capacities), systems/harmonic_model_builder.h (END TO END), processors/multi_pitch_detector.h, multi_source_sieve.h, subharmonic_validator.h, residual_analyzer.h, spectral_coring_estimator.h, primitives/stft.h and spectral_buffer.h (public API, hop / overlap handling, allocation), core/window_functions.h. For EACH: the API contract (prepare / reset / process signatures, the sample rate and sizes it is prepared with, input units and scaling, fixed capacities and what happens beyond them, allocations outside prepare, thread assumptions, NaN guards, fast-math hazards) and then how the two Innexus front-ends (plugins/innexus/src/dsp/sample_analyzer.cpp, live_analysis_pipeline.cpp) call it — the same way in both? prepared with the right rate (the WAV's rate offline, the host rate live)? every output consumed? These headers may be consumed by other plugins too (grep -rl each header name under plugins/): note the cross-plugin exposure of each suspected defect. Oddities = contract violations you suspect, quantities whose scale differs between callers, results computed and then ignored.`,
  },
  {
    key: 'shared-dsp-resynthesis',
    prompt: `Map the SHARED RESYNTHESIS DSP (dsp/include/krate/dsp/processors/): harmonic_oscillator_bank.h read END TO END (~1390 lines: the magic-circle recurrence and epsilon, loadFrame and what it recomputes, amplitude and frequency smoothing, phase seeding and continuity, the target pitch and inharmonicity amount, partial count, detune spread and stereo spread with their per-partial pan tables, pitch bend / tuning, the fade-out tail, the Nyquist rule, the per-partial bandwidth noise, process vs processStereo, SIMD and alignment, allocations), residual_synthesizer.h (END TO END: prepare and its allocations, loadFrame, the band envelope, brightness, transient emphasis, the feedback-velocity argument of process, the loadFrame call counters used by tests), harmonic_types.h, residual_types.h, harmonic_frame_utils.h (lerp, filter mask application), harmonic_snapshot.h (HarmonicSnapshot, MemorySlot, capture / recall conversion). For EACH: the API contract, the units of every setter, what is safe to call per sample vs per frame vs per block and what it costs, and then how Innexus calls it (plugins/innexus/src/processor/processor.cpp broadcastFrameToVoices ~2195, the per-voice loop ~1637-1878, processor_midi.cpp initVoiceForNoteOn ~127): prepared with the host rate? setters called per sample that recompute tables? frames loaded more often than they change? Note the other plugins that include each header (Membrum includes harmonic_oscillator_bank.h and harmonic_types.h).`,
  },
  {
    key: 'frame-pipeline',
    prompt: `Map the FRAME PIPELINE between analysis and the voices: the frame-selection part of Processor::process in plugins/innexus/src/processor/processor.cpp read by offsets (~557-1272 before the sample loop: source decision, memory recall ~578 and capture ~695, the early-return path ~785-836, manual freeze ~917-974, the harmonic filter mask ~975, harmonic physics smoothers ~1001, morph ~1020-1086, the live-frame freeze ~1087-1164, evolution ~1165-1272; inside the sample loop ~1328-1636: frame advancement and the sustain loop in sample mode, the evolution / blend rebuild and its counter evoBlendRebuildCounter_, the modulators), applyModulatorAmplitude ~2182, applyHarmonicPhysics ~2170, broadcastFrameToVoices ~2195-2269, and the plugin-local engines plugins/innexus/src/dsp/evolution_engine.h, harmonic_blender.h, harmonic_modulator.h, harmonic_physics.h, spectral_decay_envelope.h. Cover: which frame each stage reads and writes (voice_.morphedFrame, manualFrozenFrame_, currentLiveFrame_, lastGoodFrame, the memory slots), the ORDER of stages and which combinations are mutually exclusive (blend overrides evolution; freeze vs live; recall vs capture in one block), what runs per block, per frame and per SAMPLE and what each costs (frame copies of 96 partials x 8 voices, residual loads), the trigger idiom for capture / recall (previousCaptureTrigger_), how the memory slots are written (audio thread on capture, message thread on JSON import and setState) and read, each engine's state and its reset rules (note-on, freeze, source switch, setActive), the modulator's rate source (free vs tempo sync from tempoBPM_) and its three targets, the smoothers and their time constants. The voice_ alias (voice 0) is used for "the" frame while eight voices exist: note every place where voice 0 stands for global state.`,
  },
  {
    key: 'voice-physical',
    prompt: `Map the VOICE and the PHYSICAL-MODELLING path: plugins/innexus/src/processor/innexus_voice.h, plugins/innexus/src/processor/processor_midi.cpp read END TO END (~617 lines: processEvents, onNoteOn / onNoteOff / onPitchBend / onNoteExpression, findVoiceByNoteId, getActiveVoiceCount, initVoiceForNoteOn ~127-338 with everything it configures per note, handleNoteOn ~339-514 incl. the mono path, the allocator path, steals and the anti-click crossfade, handleNoteOff ~515, handlePitchBend ~577, updateReleaseDecayCoeff ~605), the per-voice part of the sample loop in processor.cpp (~1637-1878: oscillator bank, exciter switch, mallet choke, resonator and its crossfade with energy gain matching, body resonance, PhysicalModelMixer, the recovery and anti-click crossfades, velocity, expression volume / pan / brightness, the ADSR gain with ADSR Amount, the release envelope, isFinished and voice retirement), the hoisted per-block setup (~881-906 and ~1284-1327: material parameters, resonance type change, bow and body parameters), plugins/innexus/src/dsp/physical_model_mixer.h. Cover: what a note-on resets and what it deliberately keeps, how a voice's pitch is derived from the MIDI note, the analysed F0, pitch bend and note-expression tuning, mono legato / retrigger rules, how noteId = -1 is handled, the voice-mode change while voices sound, the ADSR's two roles (amplitude envelope with ADSR Amount, bow force for the Bow exciter: is adsr.process() called twice per sample in that case?), the release path when ADSR Amount is 0, the sympathetic-resonance note bookkeeping, every member that carries state from one note to the next, and where the sample offset of an event is used or ignored.`,
  },
  {
    key: 'shared-dsp-physical',
    prompt: `Map the SHARED PHYSICAL-MODELLING and UTILITY DSP Innexus relies on (dsp/include/krate/dsp/): processors/modal_resonator_bank.h (read END TO END: mode storage, the damping law and its units, setModes and the per-sample processSample(excitation, decayScale) path Innexus uses, getFeedbackVelocity / getControlEnergy / getPerceptualEnergy, maximum mode count, modes above Nyquist, NaN guards), processors/waveguide_string.h (prepare / prepareVoice / silence / process, tuning, stiffness, pick position, the soft clipper, energy getters), impact_exciter.h, bow_exciter.h (setEnvelopeValue, setResonatorEnergy, oversampling), body_resonance.h, systems/sympathetic_resonance.h (resonator pool, add / retire, per-sample cost, stability, output from silence), systems/voice_allocator.h (public API and contract), primitives/adsr_envelope.h (curve tables, retrigger modes, the gate vs setter ordering the processor comments on ~497), smoother.h (OnePoleSmoother: configure, snapTo, advanceSamples), biquad.h, processors/envelope_follower.h, core/pitch_utils.h, midi_utils.h, note_value.h, random.h, sigmoid.h, db_utils.h (detail::isFinite), scoped_denormal_mode.h. For EACH: the API contract (signatures, per-sample vs block, parameter ranges and units, stability limits, whether it can produce output from silence, latency, allocations outside prepare) and then how Innexus calls it (prepared with the right rate? reset on note-on, on a type switch, on setActive? ranges respected?). Several of these headers are being changed by the Membrum audit fixes on this branch (git log --oneline --grep "audit phase" -- dsp/): for each such change, say whether Innexus's use of the header is affected. Note the other plugins that include each header. Oddities = contract violations you suspect.`,
  },
  {
    key: 'processor-master',
    prompt: `Map the PROCESSOR shell and the MASTER section: plugins/innexus/src/processor/processor.h (members, the test accessors, the voicesPtr_ / voices_ / voice_ aliasing), processor.cpp outside the frame pipeline (constructor and destructor, initialize ~68 with the buses it declares, terminate ~105, setActive ~131-257: everything it prepares, resets and frees, setupProcessing ~258-347, the top of process ~348-556: denormal guard, parameter changes, tempo, checkForNewAnalysis, ADSR parameter update for all voices, events, the output-bus handling, the early-return path and what it leaves in the output buffers, the master part ~1879-2027: polyphony compensation, crossfades, sympathetic resonance, master gain, finiteness guard and soft limiter, mono vs stereo output, silence flags, the feedback buffer, the ADSR display atomics), setBusArrangements ~2032, canProcessSampleSize ~2054, and plugins/innexus/src/processor/processor_messages.cpp END TO END (connect / disconnect and the DataExchangeHandler lifetime, sendDisplayData ~76-194 with its throttle, the DataExchange block vs the SharedDisplayBridge fallback, notify ~195-291: the three message ids, which thread runs it and what each writes), plus plugins/shared/src/display/shared_display_bridge.h and controller/display_data.h. Cover: process() order and where parameter changes, events and audio interleave (is anything sample-accurate?), every buffer with a fixed size and what guards it against a larger host block, what setActive(false -> true) keeps and loses, what happens with numOutputs == 0 or null buffers or 64-bit processing, the bypass parameter's effect, the IMessages allocated inside process(), the instance-id scheme of the display bridge (state trailer, register / unregister, two instances with the same stored id after a project duplicate).`,
  },
  {
    key: 'params-state-presets',
    prompt: `Map parameters, persistence and presets: plugins/innexus/src/plugin_ids.h, parameters/innexus_params.h, parameters/note_value_ui.h, processor/processor_params.cpp (END TO END), processor/processor_state.cpp (END TO END: list every field getState writes IN ORDER with its type, and every field setState reads in order with its clamp and what it does on a short read), controller/controller_state.cpp (readComponentState ~71-572: the same list for the controller; importSnapshotFromJson ~573), controller/controller_presets.cpp (createComponentStateStream, loadComponentStateWithNotify, editParamWithNotify, the preset-manager providers), dsp/harmonic_snapshot_json.h (the JSON reader / writer and its limits), preset/innexus_preset_config.h, tools/innexus_preset_format.h and tools/innexus_preset_generator.cpp (the serializer and the 54 definitions), and the shared preset manager the controller drives (plugins/shared/src/preset/: public API only). Cover: the state layout as CODED (version, the M1..M6 parameter groups, the sample path, the residual frames and their size, the eight memory slots with their snapshots and per-slot ADSR, the tail fields appended after version 2 in the order they were added, the instance-id trailer), what "version" gates today (only the timbralBlend skip?), what a reader does on a version it does not know, on a short stream at each point, on huge counts, the side effects of setState beyond storing values (loadSample and its thread, memory slots written while the audio thread may read them, evolution waypoints, the display bridge re-registration), which thread each load path runs on, the preset-save path (who builds the bytes: the processor's getState or the controller from parameter values? does a saved preset embed the sample path, the residual frames and the memory slots?), the preset-load path (the IMessage carrying the bytes AND the per-parameter replay with performEdit: both?), and how the generator's defaults relate to the plugin's.`,
  },
  {
    key: 'controller-ui',
    prompt: `Map the controller + UI: plugins/innexus/src/controller/controller.h and controller.cpp (~1475 lines, by offsets: initialize ~241-893, createPresetManager ~894, terminate ~913, createView ~925, createCustomView ~937, createSubController ~1049, didOpen ~1071, willClose ~1144, queueOpened / queueClosed / onDataExchangeBlocksReceived ~1202-1241, notify ~1242-1389, the note-expression interfaces ~1399-1475), controller_adsr.cpp (setParamNormalized ~57, wireAdsrDisplay, the expanded overlay), controller_view_sync.cpp (sample file selection, the visibility updates for impact / bow / resonator / feedback / latency containers, onDisplayTimerFired ~367), controller_param_display.cpp, controller_presets.cpp, modulator_sub_controller.{h,cpp}, sample_drop_target.h, adsr_expanded_overlay.h, display_data.h, every file under plugins/innexus/src/controller/views/, resources/editor.uidesc (templates Editor and modulator_panel, custom views, control-tags), and the shared views it instantiates from plugins/shared/src/ui/ (public API only). Cover: editor lifecycle (createView, didOpen, willClose, the update banner + version label rule), EVERY raw view pointer the controller caches (controller.h ~236-348) and where each is set and nulled, the display timer (who starts and stops it, what it reads: the DataExchange copy or the SharedDisplayBridge buffer, and from which thread each is written), the sub-controller instances (lifetime, the views they hold, tag remapping for modulator 1 vs 2, the rate knob that switches between free rate and note value), the visibility logic vs the DSP's real contexts, the sample load button / file selector / drag-and-drop path and the filename label, the "SampleFileLoaded" / "DetectedADSR" / "RecalledADSR" handlers (what they write, performEdit or setParamNormalized), the ADSR display wiring, the preset browser and save dialog (callbacks capturing the controller, lifetime), the JSON snapshot import entry point, parameter display formatting, the note-expression and physical-UI mappings.`,
  },
  {
    key: 'specs-and-tests',
    prompt: `Map the SPEC CLAIMS and the TEST SUITE. Read specs/INNEXUS-ROADMAP.md, the spec.md of the archived milestone specs under specs/_archive_/ (115-innexus-m1-core-instrument, 116-residual-noise-model, 117-live-sidechain-mode, 118-musical-control-layer, 119-harmonic-memory, 120-creative-extensions, 121-plugin-ui, 122-harmonic-physics, 123-analysis-feedback-loop, 124-adsr-envelope-detection, 127-modal-resonator-bank, 128-impact-exciter, 129-waveguide-string-resonance, 130-bow-model-exciter, 131-body-resonance, 132-sympathetic-resonance: FR-xxx / SC-xxx, skim, list the testable ones), specs/_architecture_/innexus-plugin.md and plugins/innexus/MANUAL.md; list the testable claims (numbers, ranges, latencies, pitch range, behaviours) as invariants with their source line. Then read plugins/innexus/tests/CMakeLists.txt (targets, per-file compile options, the resource-dir define) and skim every test file's TEST_CASE names and tags (grep is fine) to record what each file covers, which cases are hidden / [!mayfail] / [.perf], which read a WAV from an absolute path outside the repository (C:/test/...: what do they do when the file is missing — skip, pass, fail?), which are diagnostics that print instead of asserting, which assertions look tautological or too loose (Approx margins that swallow the effect, "is not silent" as the only check), which tests run on vstgui_test_stubs.cpp instead of the real views, which test names promise something the body does not check (test_state_v6 .. v10), and which spec claims have no test. Also read the prior audit plugins/innexus/docs/audit-2026-07-implementation-plan.md and record, per work item (WI-1 .. WI-22, QS-n), the regression test the item named and whether a test with that name exists today. Oddities = claims the code probably violates, tests that cannot fail, prior items whose regression test is missing.`,
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
    prompt: `Hunt REAL-TIME SAFETY and THREADING violations. AUDIO THREAD: allocation (std::vector growth / resize / assign, std::function, std::string, new, make_unique, a frame container that owns heap memory), locks, exceptions, IO / logging (KRATE_BRIDGE_LOG), unbounded loops, blocking calls, reachable from Processor::process(), processParameterChanges, processEvents and the note handlers, checkForNewAnalysis (it calls allocateMessage / sendMessage for "DetectedADSR" at processor.cpp ~2116, and the recall path does the same for "RecalledADSR" at ~644: do these allocate or lock in the SDK, and on which thread does the host deliver them?), broadcastFrameToVoices, ResidualSynthesizer::prepare or loadFrame called from process (the prior audit's WI-3), HarmonicOscillatorBank::loadFrame, LiveAnalysisPipeline::pushSamples / runAnalysis and everything under them (STFT, YIN, the partial tracker's matcher, the model builder, multi-pitch: any allocation, any data-dependent unbounded loop?) and setLatencyMode when the parameter changes while audio runs (does it re-prepare, i.e. allocate, on the audio thread?), the DataExchangeHandler calls in sendDisplayData (getCurrentOrNewBlock, sendCurrentBlock: the SDK's IMessage fallback) and the SharedDisplayBridge write. THREE THREADS: (1) the ANALYSIS thread (SampleAnalyzer::analyzeOnThread): what it shares with the others (the result pointer, the complete / cancel flags and their memory orders), startAnalysis while a previous thread still runs (join on the caller's thread: which thread is the caller — the message thread in notify, the host thread in setState, ever the audio thread?), the destructor and terminate with a running analysis, takeResult racing completion. (2) The MESSAGE thread: Processor::notify handles "LoadPresetState" (the whole state reader), "HarmonicSnapshotImport" (writes a memory slot) and "LoadSampleFile" while process() may be running; setState and getState are called by hosts off the audio thread while processing. List every piece of NON-atomic state those paths write or read (memorySlots_, loadedFilePath_ (a std::string read by getState and written by loadSample), manualFrozenFrame_, the evolution engine's waypoints, filterMask_, previous*Trigger_, instanceId_, residual frames) and say for each whether the audio thread can observe a half-written value: is there a transfer object, a flag the audio thread checks, or nothing? (3) The AUDIO thread's pointer swap: currentAnalysis_ exchange and pendingDeletion_ — a single slot: if two new analyses arrive before setActive frees the first superseded one, is one leaked or deleted on the audio thread? testInjectAnalysis deletes the old pointer directly: test-only, or reachable in production? Atomics with the wrong memory order or non-lock-free types; data shared with the UI thread without an atomic or a transfer object (DisplayData, sharedDisplay_, the controller's copy). Focus: plugins/innexus/src/processor/*, src/dsp/sample_analyzer.*, src/dsp/live_analysis_pipeline.*, plugins/shared/src/display/shared_display_bridge.h, and the shared headers they call.`,
  },
  {
    key: 'dsp-numerics',
    prompt: `Hunt NUMERICAL defects: NaN/Inf sources — the magic-circle oscillator when its epsilon leaves the stability bound (detune spread x modulator frequency target x pitch bend +12 x inharmonicity x note-expression tuning multiplied together on a high partial of a high note: is the product clamped once at the end or per factor?), partials at or above Nyquist (dropped, faded, or left to alias?), division by a global amplitude, an F0, an RMS, a frame count, a hop size or an energy that can be 0 (silent sample, zero-length file, unvoiced frame, eNew in the resonator crossfade's gain match, crossfadeTotalSamples, sourceCrossfadeLengthSamples_, freeze recovery lengths set to 0 at a low sample rate or before setupProcessing), log / pow / sqrt / acos / exp at domain edges (log of a zero magnitude in peak interpolation or dB conversion, sqrt of a negative energy difference, the log-mapped parameters at 0), YIN on silence or DC (0 / 0 in the normalised difference), parabolic interpolation with a flat or zero neighbourhood, the Hungarian cost with non-finite entries, least-squares fits over a long sample (float cancellation, WI-19), the modal bank and the waveguide at parameter extremes (decay 0.01 s and 5 s, stretch 1, stiffness 1, a note above the waveguide's shortest delay), bow pressure / speed extremes, the body FDN's feedback gain at Size / Material extremes, sympathetic resonance loop gain, the analysis-feedback loop (tanh x 2 x amount: can its loop gain exceed 1 with decay 0?). FINITENESS CHECKS: the per-sample sanitize in the master section (processor.cpp ~1923): does it only hide a NaN that has already poisoned a recursive state (an oscillator, a resonator, a smoother, the sympathetic bank, the feedback buffer) so the voice stays silent or broken until a reset? Every isnan / isfinite / bit-mask test under plugins/innexus and in the consumed headers (the repo rule: classify only through Krate::DSP::detail::isFinite / isNaN / isInf; a per-file -fno-fast-math does not protect inline header code; an "IEEE" flag set in CMake is -fno-fast-math -fno-finite-math-only -ffp-contract=off on one line — check plugins/innexus/CMakeLists.txt and plugins/innexus/tests/CMakeLists.txt). numeric_limits infinity() / quiet_NaN() used as a search seed in analysis code. Denormals: ScopedDenormalMode is set in process(): the analysis THREAD runs outside it (does it set its own FP mode? denormal-slow analysis is a load-time cost, not a glitch) and so does any DSP run from setState or notify. Float == comparisons used as gates (amount != 0.0f, mix > 0, "sampleL != 0.0f" for the silence flag: an inaudible denormal-free residue keeps the bus "not silent" forever?). Exactness by cancellation at neutral points (Physical Model Mix 0 documented as bit-exact, harmonic physics at 0 documented as bit-exact bypass, morph position 0 / 1, brightness 0.5 as neutral, expression brightness 0.5 giving x1 on both levels): do they land exactly, also under fast-math contraction? Smoothing coefficients derived before the sample rate is known; time constants hard-coded as per-sample or per-frame coefficients (independent of sample rate or hop size); uninitialised state read on the first block; the mono output path summing L + R before the limiter (6 dB hotter than stereo?). Focus: plugins/innexus/src/dsp/**, src/processor/**, and the KrateDSP headers Innexus includes.`,
  },
  {
    key: 'analysis-pitch',
    prompt: `Hunt defects in PRE-PROCESSING and PITCH DETECTION, the front of both analysis paths: plugins/innexus/src/dsp/pre_processing_pipeline.h (DC removal, the 30 Hz high-pass, transient suppression, the noise gate: coefficients per sample rate? does transient suppression eat the attacks the instrument is supposed to reproduce? does the gate chop decays, and does a gated frame reach the tracker as silence or as "no frame"?), dsp/include/krate/dsp/processors/yin_pitch_detector.h (window length vs the lowest F0 at each sample rate: the offline window and the documented 40 Hz floor, WI-15; the search range's upper bound; the threshold and the first-dip rule; interpolation at the range edges; the confidence value and how "voiced" is decided; behaviour on silence, DC, noise, a pure sine; the cost per call), how each front-end configures and calls it (plugins/innexus/src/dsp/sample_analyzer.cpp, live_analysis_pipeline.cpp: window size per latency mode, hop, the SAMPLE RATE passed — the WAV's rate offline, not the host's — and what happens to the F0 when they differ), octave-error handling (subharmonic_validator.h: what it corrects, when it makes things worse: a real low note with a strong second harmonic), the two-pass polyphony guard (the F0-instability criterion and its thresholds: does vibrato, a glide or a decaying note trip it and switch a clean monophonic sample to the sieve-less pass? the fixed F0 chosen for pass 2: median of what?), multi_pitch_detector.h and multi_source_sieve.h (iteration limits, harmonics shared by two sources, the comment vs code mismatch the prior audit noted, the number of sources vs kMaxVoices), and the F0 smoothing / hysteresis downstream. Then the CONSEQUENCE chain: an F0 that is wrong by an octave or jitters by a few cents: what do the sieve, relativeFrequency, the voice pitch and the display do with it? For each defect name the input that triggers it (an F0, a sample rate, a signal type). Focus: the files above plus dsp/include/krate/dsp/systems/harmonic_model_builder.h where it consumes the F0.`,
  },
  {
    key: 'analysis-partials',
    prompt: `Hunt defects in SPECTRAL ANALYSIS and PARTIAL TRACKING: the STFT setup in both front-ends (window type and size, hop, the scaling of magnitudes: is the window's coherent gain compensated so a full-scale sine reads amplitude 1? the same scaling offline and live?), the DUAL window (plugins/innexus/src/dsp/dual_stft_config.h says the long window serves partials 1-4 and the short one the upper partials; the prior audit's WI-1 found the long spectrum computed and never consumed: what is true today in sample_analyzer.cpp and in live_analysis_pipeline.cpp, per latency mode? are the two spectra time-aligned when both are used — their centres differ by (4096 - 1024) / 2 samples?), dsp/include/krate/dsp/processors/partial_tracker.h read carefully: peak picking (threshold, minimum distance vs the Blackman-Harris main lobe, the maximum number of peaks and what is dropped when exceeded), the peak interpolation (linear vs log magnitude, WI-14; edge bins), the Hungarian assignment (cost function, the gating distance, fixed-size matrices and what happens with more peaks than slots, worst-case iterations on the audio thread), track birth / death / age / stability, the HARMONIC SIEVE (tolerance as a function of n and of the bin width, WI-12 / WI-13; low fundamentals whose harmonics are closer than the short window resolves; which partial wins when two peaks fall in one sieve slot; what the sieve does with voiced = false), harmonicIndex / relativeFrequency / inharmonicDeviation bookkeeping (consistent with each other? what the oscillator bank later assumes), the per-partial bandwidth estimate (where it is computed, its range, the "inflated bandwidth = hiss on every partial" history), then dsp/include/krate/dsp/systems/harmonic_model_builder.h (the dual-timescale smoothing: time constants per frame, independent of hop size and sample rate?; amplitude normalisation by globalAmplitude and its inverse at synthesis; partial ordering and gaps: is partial k always harmonic k+1, and what if harmonic 3 is missing; responsiveness mapping). PARITY: list every way the offline and the live path differ in what they feed to and take from the tracker and the builder, and whether a difference changes the frame contract (inputRms scale, WI-11). Sample-rate dependence: window sizes are fixed in samples, so resolution, latency and the lowest F0 scale with the rate (44.1 vs 96 vs 192 kHz): is anything adapted? Focus: plugins/innexus/src/dsp/sample_analyzer.cpp, live_analysis_pipeline.cpp, and the shared analysis headers.`,
  },
  {
    key: 'residual-noise',
    prompt: `Hunt defects in the RESIDUAL / NOISE model, end to end: analysis (dsp/include/krate/dsp/processors/residual_analyzer.h and spectral_coring_estimator.h: how the residual is separated from the harmonics, the 16 band edges vs the sample rate, the energy scale of a band value, the transient flag, what a pure harmonic tone leaves as "residual" (leakage of the window's sidelobes and of unsieved partials counted as noise), what pure noise leaves), storage (ResidualFrame in residual_types.h; the residual frames embedded in the plugin state by getState and their size for a long sample; the "residualFrameCount = 0" generator path), synthesis (residual_synthesizer.h: prepare and its allocations, loadFrame cost and how often the processor calls it — per frame, per block or per SAMPLE (the loadFrame counters in processor.h exist because of this), band interpolation, overlap, level calibration against the harmonic part so that Residual Level 1.0 reproduces the source's noise level, the Brightness tilt and the Transient Emphasis, the feedbackVelocity argument), the live path (setResidualEnabled toggled by residualLevel_ > 0 each block: a parameter at exactly 0 disables analysis — what does the first frame after re-enabling contain?), the role of the residual as the EXCITATION of the physical-model path (exciter type Residual feeds the resonator with the residual synthesizer's output: level, and what happens when no residual exists — hasResidual false gives silence into the resonator, so Physical Model Mix 1 with a harmonic-only source is silent?), and per-partial BANDWIDTH in harmonic_oscillator_bank.h (the noise modulation: energy preserving? WI-8; the noise source's seeding per voice and per partial: correlated across partials or voices?; the cost; who writes bandwidth, and every code path that could inflate it). Report where the noise in the output can exceed the noise in the source, and where real noise is lost. Focus: the files above plus the residual handling in plugins/innexus/src/dsp/sample_analyzer.cpp, live_analysis_pipeline.cpp and plugins/innexus/src/processor/processor.cpp (~907-916, ~2195-2269, ~1680).`,
  },
  {
    key: 'resynthesis-oscillator',
    prompt: `Hunt defects in the ADDITIVE RESYNTHESIS: dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h read END TO END, and its driving code in plugins/innexus/src/processor/processor.cpp (broadcastFrameToVoices ~2195-2269, the per-block setup ~844-880, the per-voice loop ~1650) and processor_midi.cpp (initVoiceForNoteOn). Check: the recurrence and its amplitude drift over minutes (is the amplitude renormalised?), epsilon computation and the stability clamp (WI-5: where exactly is the clamp, and is it applied AFTER detune, modulator frequency offsets, pitch bend and tuning are multiplied in?), frequency smoothing vs amplitude smoothing time constants and what they do to a fast attack and to a frame change (zipper noise vs smear), what loadFrame does when the partial COUNT or ORDER changes between frames (a partial index that now holds a different harmonic: a frequency jump with a phase discontinuity or a click?), when a partial's amplitude goes to 0 and returns, when the frame is the first after note-on (phase seeding: from the analysed phase, random, or zero? all partials starting in phase gives a click and a pulse-like onset), phase continuity across freeze / morph / recall / source switch, the target pitch: how the played note, the analysed F0, relativeFrequency and inharmonicDeviation combine, and what Inharmonicity Amount 0 / 1 means for an inharmonic source; a source whose F0 is 0 or unvoiced (which pitch plays?); Partial Count 48 / 64 / 80 / 96 (does lowering it cut partials instantly = click? does raising it restart them in phase?); partials above Nyquist per note and under pitch bend (checked once at note-on or continuously?), anti-aliasing fade width; Detune Spread and Stereo Spread laws (per-partial tables recomputed how often, WI-20 / WI-21; equal-power pan? mono compatibility of spread = 1; odd / even partial allocation), processStereo's fade-out tail scan (WI-22), the note-off path (does the bank keep running at full cost after the voice's gain reaches zero?), output level vs the number of partials and vs globalAmplitude (does a source with 96 partials play louder than one with 8 at the same analysed level?), SIMD paths vs the scalar path (same result?). Sample-rate independence of every time constant. Focus: the files above; cite the shared header and the Innexus call site for each defect.`,
  },
  {
    key: 'frame-pipeline',
    prompt: `Hunt defects in the FRAME PIPELINE (freeze, morph, harmonic filter, memory, evolution, blend, modulators, harmonic physics) in plugins/innexus/src/processor/processor.cpp (~557-1636) and plugins/innexus/src/dsp/{evolution_engine,harmonic_blender,harmonic_modulator,harmonic_physics,spectral_decay_envelope}.h. ORDER AND EXCLUSIVITY: which stage wins when several are on (recall + capture in one block; freeze + evolution; blend + evolution; freeze in sample mode vs sidechain mode; the early-return path at ~785 that duplicates part of the freeze logic: do the two copies agree?); stages that read a frame an earlier stage has not written yet this block; state that a stage keeps when it is switched off and replays when switched on again. PER-SAMPLE WORK: evolution and blend inside the sample loop (WI-6: the rebuild counter evoBlendRebuildCounter_ — what rate does it give, is the rate independent of block size and sample rate, and does every rebuild still copy 96 partials to 8 voices and reload the residual?), the modulators' per-sample pan / frequency tables. MORPH: interpolation by array index vs harmonic number between frames with different partial sets, linear amplitude and Hz, the smoother advanced "by block size" (~1046: the same result for block 32 and block 4096?), morph position exactly 0 and 1. FREEZE: the captured frame (pre- or post-filter, pre- or post-physics), the recovery crossfade using one "old level" scalar for a stereo signal, auto-freeze thresholds and hysteresis chatter around confidence 0.3, lastGoodFrame per voice vs the global live frame, the spectral decay's rate per frame vs per second. HARMONIC FILTER: the mask per type vs the manual's description, "Low / High Harmonics" slope vs the partial count, applied to amplitudes only? MEMORY: slot index from a normalized float (rounding), capture of an empty / silent frame, recall of an empty slot, the per-slot ADSR that travels with a slot ("RecalledADSR"), slots written by JSON import or setState on another thread while evolution reads them. EVOLUTION: waypoints not refreshed after a slot changes, modes Cycle / PingPong / RandomWalk (RNG seeding, bounds), speed in Hz vs the rate the engine is advanced at, zero or one occupied slot. BLEND: weights summing to 0 or above 1 (normalised? level jump), the QS-2 harmonicIndex issue. MODULATORS: Target Amplitude / Frequency / Pan — is the selected target the ONLY thing modulated (WI-10)? range start > end, depth 0 bit-exact, waveform phase reset on note-on or free-running, Random S&H seeding, tempo sync from tempoBPM_ when the host gives no tempo, two modulators on the same partials (order, multiplication vs addition). HARMONIC PHYSICS: coupling energy conservation, warmth's level change, the agents' time constants per frame (hop- and rate-dependent), state reset rules, all four at 0 bit-exact. Focus: the files above.`,
  },
  {
    key: 'physical-path',
    prompt: `Hunt defects in the PHYSICAL-MODELLING path as Innexus wires it: the per-voice loop in plugins/innexus/src/processor/processor.cpp (~1650-1776), the per-block setup (~881-906, ~1284-1327), plugins/innexus/src/processor/processor_midi.cpp (initVoiceForNoteOn: how modes, the waveguide, the exciters and the body are configured from the analysed frame and the note), innexus_voice.h, plugins/innexus/src/dsp/physical_model_mixer.h. Check: the modal bank's modes taken from the analysed partials (how many, which amplitudes, refreshed when the frame changes or only at note-on? retuned on pitch bend?); Resonance Decay / Brightness / Stretch / Scatter mappings and when a change reaches a sounding voice; the resonator SWITCH (kResonanceTypeId: the two-option denormalization, WI-7; the crossfade runs BOTH resonators — is the incoming one silent because it was never excited, so the "crossfade" is a fade-out? the energy gain match dividing by eNew near 0 and clamped to 4x; the outgoing resonator reset at the end while it may still be audible), the exciter switch (state of the exciter that was not running; Bow -> Impact while the string rings), the Impact exciter's trigger at note-on with velocity, the mallet choke (chokeMaxScale_, the 10 ms envelope: applied only to the modal bank; passed as decayScale every sample), the Bow branch calling v.adsr.process() and the amplitude stage calling it again in the same sample (~1671 and ~1821: the envelope advances twice as fast with the Bow exciter?), bow force when ADSR Amount is 0, setResonatorEnergy units for the two resonators, Bow Oversampling toggled while sounding, feedbackVelocity fed from the resonator into the exciter (a one-sample feedback loop: gain, stability), the residual exciter with no residual available (silence into the resonator), body resonance on the physical signal only (bypassed exactly at Body Mix 0?), PhysicalModelMixer: at mix 0 the resonators and the body are still processed every sample (cost) and at mix 1 the residual is removed while the HARMONIC oscillators still play at full level (is "physical" then an addition rather than a replacement, as the manual describes it?), level of the physical path vs the additive path at equal settings, mono physical signal added equally to L and R under Stereo Spread, the per-voice expression brightness scaling the two levels, the sympathetic resonance: what adds and removes resonators (note-on / note-off bookkeeping, voice steal, all-notes-off), its input (post-compensation mono sum incl. its own previous output?), output with no notes. Voice retirement: isFinished looks only at the release envelope / ADSR — a voice whose resonator still rings is cut, or a voice with a ringing waveguide never ends? Focus: the files above and the contracts of the shared physical headers they call.`,
  },
  {
    key: 'voice-management-midi',
    prompt: `Hunt VOICE MANAGEMENT and MIDI defects: plugins/innexus/src/processor/processor_midi.cpp (END TO END) with plugins/shared/src/midi/midi_event_dispatcher.h and dsp/include/krate/dsp/systems/voice_allocator.h. TIMING: are note events applied at their sampleOffset or all at the block start (processEvents is called once before the sample loop: every note quantised to the block, so timing jitters with the host block size)? parameter changes: last point of the queue only? the same notes rendered at block 32 vs 2048 — identical? ALLOCATION: Mono mode (retrigger vs legato, the last-note priority on release, a note-off for a note that is no longer the sounding one), 4 / 8 voices through the VoiceAllocator (API misuse, ignored return values, the steal path and the 20 ms anti-click crossfade that stores ONE "old level" scalar: what does it fade from when the stolen voice was stereo and mid-waveform?), the same pitch pressed twice, a note-off matched by pitch vs by noteId (handleNoteOff ignores noteId: two MPE notes of the same pitch on different channels), noteId = -1, velocity 0 note-on, notes outside 0..127, Voice Mode changed while notes are held (orphaned voices? stuck notes?), all-notes-off / all-sound-off / sustain pedal (CC 64) / reset on transport stop: handled at all?, the voice count used for the 1/sqrt(N) compensation (getActiveVoiceCount: releasing voices included?). PITCH: note -> frequency (A4 = 440, the analysed F0 as the reference: a sample analysed at 220 Hz played on A3 plays 1:1?), pitch bend range 12 semitones applied to sounding and to new voices, per-note expression tuning added to it, the bend's smoothing (a stepped bend zippers), the update of the resonators' tuning under bend. ENVELOPES: Release time 20..5000 ms -> releaseDecayCoeff (sample-rate dependent? recomputed when the parameter moves during a release?), ADSR Amount blending two envelope systems (inRelease vs inAdsrRelease: which one ends the voice, can both run, can neither end it = a stuck voice?), ADSR Time Scale and the curve parameters applied before or after gate (the comment at processor.cpp ~497), the detected ADSR overwriting the user's ADSR parameters on sample load ("DetectedADSR" -> the controller -> parameter writes: a surprise edit of a saved project?), the sustain loop region (loop length 0, a loop over a decaying region, loop-point clicks), frame advancement speed vs the file's hop size and sample rate vs the host rate, a note longer than the sample with no loop. NOTE EXPRESSION: the declared types vs the handled ones (controller.cpp ~1399-1475 vs onNoteExpression), value ranges and their mapping, expression reset on note reuse. Focus: the files above plus the envelope part of processor.cpp (~1777-1878).`,
  },
  {
    key: 'gain-staging-master',
    prompt: `Hunt GAIN STAGING and MASTER-section defects in plugins/innexus/src/processor/processor.cpp (~862-880 levels, ~1637-1650, ~1879-2027). Follow one note's level from the analysed amplitude to the output: globalAmplitude and partial amplitudes -> the oscillator bank's output level -> Harmonic Level (plain 0..2) and Residual Level (0..2; the default 0.3: why not 1.0 if the residual is calibrated to the source?) -> expression brightness -> the physical mix -> velocity (the curve: linear in amplitude?) -> expression volume -> ADSR / release -> the voice sum -> 1/sqrt(active voices) (the factor JUMPS at every note-on and note-off: the notes already sounding change level audibly; releasing voices counted?) -> the source-switch and freeze-recovery crossfades (they fade from ONE stored scalar "old level" applied to both channels, i.e. from a DC value, not from the old signal: what does that sound like?) -> sympathetic resonance added -> Master Gain (the mapping of the normalized value to gain: masterGain_ is used directly as a linear factor 0..1? its default 1.0 in processor.h vs 0.8 in tools/innexus_preset_format.h vs the registered default; what the editor displays) -> the finiteness guard -> the tanh soft limiter above 0.85 (transparent below the knee as the comment says? how often ordinary playing exceeds it: one note at defaults, chords, Harmonic Level 2; the hard ceiling at 1.0 = 0 dBFS with no headroom control; distortion of a clean resynthesis) -> the mono output branch (softLimit(L + R): +6 dB against stereo) -> silence flags (set from "any sample != 0": a decaying tail keeps the flag clear until it underflows; with the limiter and smoothers, does the output ever become exactly 0 after the last voice ends?). Then: Bypass (kBypassId: what does it do — nothing, mute, pass the sidechain? hosts expect a click-free bypass of an instrument to output silence), the sidechain input's level into the analysis (any normalisation? a quiet input yields quiet resynthesis or the gate closes), the analysis-feedback path's level (taken post-limiter, post-master: Master Gain changes the feedback amount), the level difference between sample mode and sidechain mode for the same source, between Mono and 8-voice mode for one note, between sample rates. Output level of factory presets relative to each other is covered by the audibility probe; report here the code reasons for level problems. Focus: plugins/innexus/src/processor/processor.cpp, processor_params.cpp, processor_midi.cpp.`,
  },
  {
    key: 'live-sidechain-feedback',
    prompt: `Hunt defects specific to SIDECHAIN (live) operation: plugins/innexus/src/dsp/live_analysis_pipeline.{h,cpp} and the processor code around it (processor.cpp ~379-486, ~557-577, ~1087-1164, ~1972-2015). BUSES: the aux input declared in initialize (stereo, kAux, not default active), setBusArrangements (~2032: which arrangements are accepted; mono sidechain; no sidechain), activateBus, the AU configuration (resources/au-info.plist.in, resources/auv3/audiounitconfig.h.in channel pairs) vs the VST3 buses; a host that does not connect the bus (numInputs 0, null buffers, the silence flag set on the input: is it honoured?). BUFFERS: sidechainBuffer_ and feedbackBuffer_ are 8192 samples; std::min caps the copy, so with a larger host block the tail of the block is not analysed and the feedback of sample s is the PREVIOUS block's sample s (a delay of one block, so the loop's timbre depends on the host block size): quantify. COST: the analysis runs inside process(); how many hops can one block trigger (block 4096 at hop 512 = 8 analyses in one callback), what one hop costs in each latency mode and in Poly mode, is the work spread or bursty (a dropout risk at small buffers every N blocks)? LATENCY: what each mode adds, that nothing reports it (no getLatencySamples), whether the manual's figures match the window sizes at 44.1 / 96 kHz. MODE CHANGES: Latency Mode or Input Source switched while audio runs (re-prepare on the audio thread? stale buffered samples? the crossfade from one scalar level), the first frames after a switch (zeros analysed as silence -> the gate -> a freeze?). CONFIDENCE GATE: which frames count as "good" (lastGoodFrame), the hold and the spectral decay when the input stops, recovery when it returns (click? the 7 ms crossfade), behaviour on noisy or polyphonic input where confidence hovers at the threshold, on input below the noise gate. FEEDBACK LOOP (kAnalysisFeedbackId / decay): the signal flow (output -> x decay -> tanh(x 2 x amount) x 0.5 -> mixed (1 - amount) with the input), its stability with no input, whether "decay" is a per-block gain that depends on the block size (exp(-decay x count / sampleRate) applied once to the whole block: a level step at block boundaries, and a different loop decay at different buffer sizes), freeze disabling the feedback mix, the stale feedback buffer after a source switch or setActive, feedback active in sample mode? RESPONSIVENESS forwarding and what it changes. The live residual. Poly / Auto analysis mode: reachable by the user at all (kAnalysisModeId)? If not, dead code on the audio path. Focus: the files above plus plugins/innexus/src/dsp/pre_processing_pipeline.h.`,
  },
  {
    key: 'param-plumbing',
    prompt: `Hunt PARAMETER PLUMBING bugs, starting from the INVENTORY mismatches (confirm each in code before reporting). For every parameter: registration (controller.cpp ~241-893) -> processParameterChanges (plugins/innexus/src/processor/processor_params.cpp) -> the atomic -> the use site -> state save / load -> the controller's reader -> the display string. Look for: the NORMALIZED vs PLAIN split (some atomics hold plain values, e.g. releaseTimeMs_ and resonanceDecay_, most hold normalized ones: a conversion applied twice or not at all on one of the paths: parameter change, setState, readComponentState, the generator, a test accessor); log-mapped parameters (release, resonance decay, ADSR times) whose forward and inverse mappings disagree, so a save / load round trip moves the value; discrete parameters denormalized with the wrong step count (the two-option x 2 defect, WI-7, in any sibling); a parameter consumed at block rate with no smoothing that zippers (which ones have a smoother in processor.h and which do not: Master Gain, Physical Model Mix, Body Mix, Sympathetic Amount, the ADSR Amount smoother per voice); smoothers not snapped on setState or setActive (a slow glide from the default after loading a project); the momentary triggers Capture / Recall (edge detection across blocks, a trigger that goes 1 -> 0 -> 1 inside one block, a host that automates them, their value saved in state and firing on load?); Freeze as a parameter that also latches internal state (loading a project saved with Freeze on: what is frozen — an empty frame?); the modulator Rate parameter vs the Note Value parameter sharing one knob (SyncableArcKnob, modulator_sub_controller.cpp) and what the processor uses when Rate Sync flips; ranges 1..96 for modulator range start / end vs the active partial count; IDs declared but not registered or not consumed (kAnalysisModeId 502, reserved 600); automation of parameters that trigger heavy work (Latency Mode, Input Source, Partial Count, Voice Mode, Resonance Type); parameters flagged automatable that should not be (Memory Slot?) or list parameters missing kIsList; the controller's setParamNormalized override (controller_adsr.cpp ~57) and what it does for non-ADSR parameters; "DetectedADSR" / "RecalledADSR" writing ADSR parameters from the processor side (performEdit with begin / end? an undo step? a feedback loop with the host's automation?). Focus: plugins/innexus/src/processor/processor_params.cpp, processor.h, src/parameters/*, src/controller/controller.cpp, controller_adsr.cpp, controller_param_display.cpp.`,
  },
  {
    key: 'parameter-audibility',
    prompt: `You are the PARAMETER-AUDIBILITY reviewer (the static twin of the audibility probe). Enumerate every user-facing parameter (about 88) and trace each from its atomic to the DSP that uses it. Rate each STRONG / WEAK / NONE / CONTEXT-ONLY and report as findings: NONE = never read by the DSP, overwritten before use, cancelled by a later stage, or clamped to one value (check in particular: the modulator Target selector, WI-10; Analysis Mode; Transient Emphasis and Residual Brightness in each exciter mode; Responsiveness in sample mode; Bow Oversampling; Waveguide Pick Position vs Impact Position vs Bow Position — three position parameters: does each reach its DSP?); WEAK = the effective range collapses (mapped into a narrow sub-range, multiplied by a near-zero constant, masked by a later stage such as the 1/sqrt(N) compensation, the soft limiter or a smoother with a very long time constant); CONTEXT-ONLY = it works only in one input source, one exciter, one resonator or behind an enable switch or a non-zero mix, AND the editor still shows it as a live control in the other contexts (cross-check the visibility logic in plugins/innexus/src/controller/controller_view_sync.cpp and resources/editor.uidesc: which containers are hidden for which selector value, and which context-only controls stay visible and enabled); parameters whose two halves of the range do the same thing; parameters applied only at the next note-on while the editor suggests a live control (the physical-model material parameters, voice mode). Then the PRESET level: read tools/innexus_preset_generator.cpp and tools/innexus_preset_format.h and report structural reasons for presets sounding alike or not as named, with counts: a factory preset carries no sample and no memory-slot content, so how many presets enable Evolution or Blend (which need occupied slots) or set Morph (which needs a frozen frame) and therefore ship a feature that cannot act; how many set physical-model parameters with Physical Model Mix at 0, modulator settings with the modulator off, ADSR settings with ADSR Amount 0; how many presets choose Input Source = Sidechain (silent on a MIDI track without a routed input); parameters that never vary across all 54 presets (which stay at their default everywhere); presets that differ from another one in name and category only; whether the category names (Brass and Winds, Keys, Voice ...) can mean anything when the timbre comes from the user's sample. Give the code reason for every item; the probe supplies the numbers.`,
  },
  {
    key: 'lifecycle-state',
    prompt: `Hunt VST3 LIFECYCLE and STATE bugs. THE THREE PARSERS: the state is written by Processor::getState and read by Processor::setState (plugins/innexus/src/processor/processor_state.cpp), by Controller::readComponentState (plugins/innexus/src/controller/controller_state.cpp ~71-572) and written again by tools/innexus_preset_format.h. Walk all four field by field IN ORDER and report every position where they disagree in order, type, count or conditional (the prior audit's WI-4 was exactly this: the controller skipped two fields, which shifted every later read and broke the trailer). The format gates almost nothing on the version: fields added after version 2 are read with "if (readFloat)" so a short stream keeps defaults — but that only works for fields at the END; check for any field that was inserted in the MIDDLE without a version bump (compare the writer at each release tag: (a) RELEASE CHECK: list the released tags from the remote (git ls-remote --tags origin "refs/tags/innexus/*": release state comes from origin's tags; say so if a tag is not available locally, do not fetch) and read which version each wrote and its field order (git show <tag>:plugins/innexus/src/processor/processor_state.cpp). Known so far: v0.9.4 wrote version 9, v0.9.5 .. v0.10.0 wrote 1, v1.0.0+ write 2, and today's readers accept only 1 and 2. For EACH release, state what today's build does with its state: rejected (which return code; what the user sees: defaults, silence, a controller that disagrees with the processor?), accepted and correct, or accepted and MISREAD (fields shifted). (b) What the user gets on a rejected or short read: the return code of setState and of setComponentState, whether the previous state stays or half of the new one is applied (the reader stores into atomics as it goes: a stream that fails midway leaves a mix), whether anything tells the user. (c) Tests whose names claim versions the code does not write (tests/unit/vst/test_state_v6.cpp .. test_state_v10.cpp): what do they pin? Parsing robustness: the path length (pathLen < 4096 guard; a negative length), a residual frame count / FFT size / hop size of garbage (allocation size, hop 0 -> division by zero in frame advancement), numPartials per memory slot beyond 96 or negative, NaN or out-of-range floats (clamped before they reach the DSP? std::clamp with NaN returns NaN), int fields out of enum range, the trailer marker "KRAT" appearing by chance, endianness. SIDE EFFECTS OF setState: loadSample starts the analysis thread from the host's thread (a project with a missing file: what then? a relative or foreign-OS path? the path is stored absolute, so a project moved to another machine loses its sound unless the embedded data suffices: what exactly is embedded — residual frames but not harmonic frames?), the old analysis kept or dropped, memory slots written while process() may run, evolution waypoints refreshed?, smoothers snapped?, voices reset?, the display bridge re-registration with a stored instance id (two plugin instances loading the same state share an id). getState: reads loadedFilePath_ and the current analysis from the host thread while the audio thread may swap it; the size of the state with a long sample (residual frames x 16 bands: megabytes per instance in every project save?). Preset semantics: a user preset saved from the browser embeds what (path, residual frames, memory slots)? Loading a factory preset: does it clear the loaded sample (empty path -> loadedFilePath_.clear()) and the memory slots, i.e. silence the instrument until the user reloads a sample? LIFECYCLE: setActive(true) (~131-257): what is re-prepared, what is reset, what is freed; setupProcessing after a sample-rate or block-size change (EVERYTHING re-prepared: voices, live pipeline, smoothers, resonators, crossfade lengths, the display throttle?); maxSamplesPerBlock above 8192; setBusArrangements; initialize / terminate balance, the analysis thread and the display bridge registration across terminate and the destructor, the DataExchange handler's lifetime across connect / disconnect / setActive; process() with numOutputs == 0, null channel buffers, 64-bit sample size (canProcessSampleSize), offline mode; tail and latency reporting (none: a tail of several seconds exists with long release and resonators). Focus: plugins/innexus/src/processor/processor_state.cpp, processor.cpp, processor_messages.cpp, src/controller/controller_state.cpp, controller_presets.cpp, tools/innexus_preset_format.h.`,
  },
  {
    key: 'ui-wiring',
    prompt: `Hunt UI WIRING bugs, starting from the INVENTORY mismatches (confirm each in code before reporting): control-tags vs plugin_ids.h; parameters registered but absent from the editor or the reverse; StringListParameter entry counts vs enum ranges; COptionMenu / CSegmentButton entry counts vs the parameter's steps. DANGLING VIEW POINTERS: every raw view pointer the controller caches (controller.h ~236-348: the five display views, the sample filename label, the load container, the drop overlay, the preset browser and save dialog, the update banner, the ADSR display and its expanded overlay, the six visibility containers, activeEditor_) must be nulled in willClose (list each pointer and the place that nulls it; a pointer set in createCustomView or verifyView and never cleared is the classic miss); the ModulatorSubController instances (two, created per template instantiation: who owns them, do they outlive the editor, do they hold view pointers, do they unregister as parameter dependents?). Timers and deferred work that can run after willClose (the display timer, preset-browser and file-selector callbacks capturing the controller, the update checker's polling). THE DISPLAY PATH: onDataExchangeBlocksReceived (which thread; the copy into the controller's buffer; blocks arriving after the editor closed) vs the SharedDisplayBridge fallback (the controller holds a raw SharedDisplay* into the PROCESSOR's memory: lifetime when the processor is destroyed first, when processor and controller live in different processes, the frame counter as the only synchronisation: a torn read of the DisplayData struct?), what onDisplayTimerFired pushes to each view. VIEWS: HarmonicDisplayView (partial count shown vs active partial count, scaling), ConfidenceIndicatorView (per-voice cache, the note-name / pitch readout), MemorySlotStatusView (occupied flags after capture, JSON import, state load), EvolutionPositionView, ModulatorActivityView. SAMPLE LOADING UI: the load button and CNewFileSelector (cross-platform; lifetime of the selector and its callback), drag-and-drop (sample_drop_target.h: accepted extensions, multiple files, a non-file drag, the overlay's lifetime, a drop while the editor closes), the filename label after state load / preset load / a failed load ("SampleFileLoaded" handler), the path travelling as an IMessage string (UTF-8 / UTF-16, long paths). VISIBILITY LOGIC (controller_view_sync.cpp): impact / bow knob containers vs Exciter Type, modal / waveguide containers vs Resonance Type, the feedback and latency containers vs Input Source — each driven by which notification (setParamNormalized override, a parameter dependent, the timer), correct after state load and on editor open, tags looked up by walking child views. ADSR: ADSRDisplay wiring both ways, the expanded overlay (plugin-local copy: lifetime, the Membrum audit's MB-114 notes three copies exist), the playback dot from DisplayData, the "DetectedADSR" / "RecalledADSR" handlers moving nine parameters (edit notification to the host?). The preset browser and save dialog (lifetime, failure paths, the seven categories), the JSON snapshot import entry (is it reachable from the editor at all?), parameter display strings vs the DSP's real range (controller_param_display.cpp), note-expression and physical-UI mapping declarations, the update banner / version label rule (plugins/shared/src/ui/update_banner_view.h). Never propose removing an interaction: list each one a fix must keep. Focus: plugins/innexus/src/controller/**, resources/editor.uidesc, and the shared views under plugins/shared/src/ui/ that Innexus instantiates.`,
  },
  {
    key: 'shared-dsp-contracts',
    prompt: `Hunt CONTRACT VIOLATIONS between Innexus and the KrateDSP it wraps: a processor whose process() expects a block but is called per sample (or the reverse); prepare() never called, called with the wrong sample rate (the analysis components prepared with the host rate but fed a file at another rate, or the reverse; the voices prepared in setActive vs setupProcessing), or called on the audio thread; reset() not called on a type switch, on note-on, on a source switch or on setActive; InnexusVoice::prepare does not prepare residualSynth or impactExciter (innexus_voice.h ~150-170: who prepares them, and when?); InnexusVoice::reset calls waveguideString.silence() but modalResonator.reset(): equivalent contracts?; setters given normalized values where the primitive expects plain (or dB vs linear, Hz vs normalized, ms vs seconds, seconds vs a decay coefficient); ranges outside what the primitive clamps (bow pressure, stiffness, modal mode count vs kMaxPartials, sympathetic resonator pool size vs 8 voices x partials); setters that recompute expensive tables called every sample or for every voice on every block (setStereoSpread / setDetuneSpread, WI-20; applyPanOffsets, WI-21; setModes; body setParameters; ADSR curve setters that rebuild tables each block ~497-536); components that emit output from silence with no gate (sympathetic resonance, bow, waveguide feedback, the body FDN); VoiceAllocator API misuse (calling order, ignored return values, note ids, the voice count change); OnePoleSmoother::advanceSamples vs per-sample process used inconsistently on one smoother; ADSREnvelope gate / setter ordering and RetriggerMode set in two places (initialize and prepare); STFT / SpectralBuffer sizes vs what is passed; the partial tracker's fixed capacities vs the peaks it is handed; the YIN detector's window vs the buffer it is given; the modal bank's processSample(excitation, decayScale) contract for decayScale; getFeedbackVelocity read BEFORE the resonator processes the current sample (one sample late by design?); ResidualSynthesizer::process(feedbackVelocity) — what does the residual synthesizer do with a velocity?; harmonic_frame_utils lerp on frames with different partial counts; MemorySlot / HarmonicSnapshot conversion losing fields (bandwidth, phase, the residual). Cite BOTH sides (Innexus's call site AND the primitive's contract line). A defect in the shared header itself is CROSS-PLUGIN IMPACT: name the other plugins that include it (grep -rl the header under plugins/), and check whether a Membrum-audit fix on this branch (git log --oneline --grep "audit phase" -- dsp/) changed a contract Innexus relies on without updating Innexus. Focus: plugins/innexus/src/processor/**, src/dsp/**, and the dsp/include/krate/dsp headers Innexus includes.`,
  },
  {
    key: 'duplication-drift',
    prompt: `Hunt DUPLICATION and COPY-PASTE DRIFT: the state layout written four times (getState, setState, Controller::readComponentState, tools/innexus_preset_format.h) and restated in comments and in specs/_architecture_/ (find the copy where one differs: an order, a type, a clamp, a default); the same normalized <-> plain mapping written in src/parameters/innexus_params.h, processor_params.cpp, the use sites in processor.cpp, processor.h getters (getActivePartialCount, getHarmonicFilterTypeFromParam, getSelectedSlotIndex), controller_state.cpp (releaseTimeToNormalized and friends), controller_param_display.cpp, tools/innexus_preset_generator.cpp (normalizeEvoSpeed, normalizeModRate, normalizeResDecay) and in tests, with subtle differences (a range, a law, a rounding); the two analysis front-ends (sample_analyzer.cpp vs live_analysis_pipeline.cpp) as a copy-paste pair: constants, thresholds, the order of stages, the scale of quantities — every place one was fixed and the other was not; the freeze logic duplicated in the early-return path (processor.cpp ~785) and the main path (~917); Modulator 1 vs Modulator 2 handling (parameter IDs are interleaved oddly: 617 / 618 for mod 1 sit after 625; any branch that handles one and not the other, or uses mod 1's value for mod 2); the per-voice code written once for voice_ (voice 0) and once for the loop over voices_; the eight blend slot weights and nine smoothers; sourceCrossfade / manualFreezeRecovery / per-voice freezeRecovery / antiClick as four copies of one crossfade idiom (which copies differ, and which share the "fade from a scalar" defect); constants defined twice with different values or as bare literals beside a named constant (8192 buffer sizes, 96 / kMaxPartials, 8 slots / kMaxVoices, the 0.85 knee, 12 semitones, confidence thresholds duplicated between processor.h and the live pipeline or the views); plugin-local re-implementations of things that exist in plugins/shared/ or in another plugin (src/controller/adsr_expanded_overlay.h vs Membrum's and Ruinae's copies, MB-114; src/parameters/note_value_ui.h, a forwarding shim to plugins/shared/src/ui/note_value_ui.h: anything left in it that differs?; the sample drop target; the display views vs shared display components); the processor including controller/display_data.h (one struct shared by both sides: where should it live?); names that could collide (ODR): Innexus types declared outside namespace Innexus, or with the same name as a type in dsp/ or plugins/shared/ or another plugin (ExciterType exists in Membrum too; ADSRExpandedOverlayView; DetectedADSR; SharedDisplay declared in both processor.h and controller.h). Focus: plugins/innexus/src/**, plugins/shared/src/**, tools/innexus_preset_generator.cpp, tools/innexus_preset_format.h.`,
  },
  {
    key: 'parallel-paths',
    prompt: `Hunt PARALLEL PATHS: two or more separate code routes that do the SAME job, where one should exist. This is not copy-paste drift (another finder covers that); the routes can look nothing alike. Method (use the Serena MCP tools when available, ToolSearch "serena": mcp__serena__find_referencing_symbols finds every caller of an entry point, including template and virtual call sites grep misses; fall back to Grep): first list every job the plugin performs at a boundary, then, for each job, list EVERY entry point that performs it (grep the callers, follow each to where the work ends), then report every job with more than one route. Jobs to enumerate at minimum: loading a full state (host setState, the preset browser's "LoadPresetState" message, the per-parameter replay the controller does on a preset load (editParamWithNotify), setComponentState, the defaults at initialize), saving a state / preset (getState vs createComponentStateStream: is a preset built from the controller's parameter values or from the processor's truth, and can they differ in the data that has no parameter: sample path, residual frames, memory slots, the frozen frame?), loading a sample (the load button, drag-and-drop, the path inside setState, the path inside a preset, the test injection), analysing audio (SampleAnalyzer vs LiveAnalysisPipeline: one algorithm, two implementations), filling a memory slot (Capture, JSON import, state load, recall-then-capture), obtaining "the current frame" (sample playback, live, frozen, morphed, recalled, evolved, blended: how many places choose it, and do the per-voice path and the voice-0 alias agree?), applying one parameter value (host automation, state load, preset replay, the DetectedADSR / RecalledADSR messages), starting a note (mono path vs allocator path in handleNoteOn; initVoiceForNoteOn vs inline setup), ending sounding notes (note-off, steal, voice-mode change, setActive, a source switch, a new sample arriving while notes sound), enveloping a voice (the legacy release envelope vs the ADSR), rendering the physical path (modal vs waveguide vs crossfade branch), sending processor data to the controller (DataExchange, its IMessage fallback, the SharedDisplayBridge, the IMessages from process, output parameters), resetting the engine. A route that applies a state or value WITHOUT going through the single canonical function is a finding even when today both routes give the same result: name what each route does that the other does not (clamps, resets, smoother snaps, non-parameter data, version checks, thread), and the observable consequence. When an earlier fix "completed" a second route by adding the missing piece to it (the R-200 single controller parser is an example of a merge; look for the opposite), report that as reinforcing the duplication. For each finding give the job, every route with file:line, the canonical route it should collapse into, and a test that proves the routes leave identical state (e.g. getState bytes after route A vs route B). Focus: plugins/innexus/src/** (processor_state.cpp, processor_messages.cpp, controller_state.cpp, controller_presets.cpp, processor.cpp, processor_midi.cpp, the two analysis front-ends), plugins/shared/src/preset/**.`,
  },
  {
    key: 'spec-conformance',
    prompt: `Hunt SPEC CONFORMANCE failures: take the testable claims from the specs-and-tests map (and read specs/INNEXUS-ROADMAP.md, the spec.md files of the archived milestone specs specs/_archive_/115..124 and 127..132, specs/_architecture_/innexus-plugin.md and plugins/innexus/MANUAL.md yourself for numbers: pitch range, latency per mode, partial counts, voice counts, memory slots, parameter ranges, signal-flow order, freeze / morph / filter behaviour, evolution modes, modulator behaviour, physical-model behaviour), then check the code honours each one; check that code comments citing an FR-xxx / SC-xxx or a research.md section implement what the comment says. COMMENTS THAT LIE are findings — group them: ONE finding per file listing its stale statements (examples to confirm: "Phase 1: Single-voice struct" and the Phase / milestone markers in innexus_voice.h and processor.h, "Reserved for Phase 4" on ExciterType::Bow, the dual-window description in dual_stft_config.h vs what is used, "Actual STFT instances are owned by SampleAnalyzer (Phase 10)", the ID-range table at the top of plugin_ids.h vs the IDs below it (700-799 "Output", 400-499 residual model holding kHarmonicLevelId), the GUID-generator boilerplate, state-version comments, spec paths that moved to specs/_archive_/, PhysicalModelMixer's "bit-exact current behavior" and its harmonicSignal argument that the caller passes as 0). Also docs/index.html, docs/innexus_signal_flow.dot (walk the diagram against process()), CHANGELOG.md, README.md, plugins/innexus/CLAUDE.md, .claude/skills/innexus/SKILL.md and specs/_architecture_/innexus-plugin.md claims against behaviour (the skill names a "tanh soft-limiter", a 1/sqrt(activeCount) sum and specific window sizes; the architecture doc names state versions 4 / 5 / 6). Report the claim, its source line, and the code line that contradicts it.`,
  },
  {
    key: 'portability-quality',
    prompt: `Hunt CROSS-PLATFORM and CODE-QUALITY problems: narrowing in brace-init that MSVC accepts but Clang rejects, std::isnan / bit-mask finiteness under -ffast-math, numeric_limits infinity() / quiet_NaN() constants in DSP or analysis code, per-file IEEE flag sets missing -fno-finite-math-only or -ffp-contract=off, iteration over an unordered container that feeds serialization or JSON output, names that collide with Apple globals / macros (MAC, Rect, Point, Component, Fixed, Boolean...), missing includes that MSVC forgives (<cstdint>, <cstring>, <algorithm>, <array>, <numbers>, <thread>, <string>), std::thread use and joining on all three platforms (the analysis thread's stack size for large local arrays; a thread started from a plugin in a sandboxed AUv3), FILE PATHS: the sample path as std::string (UTF-8 on Windows? std::filesystem::path construction from a narrow string, non-ASCII file names, dr_wav's wide-char open on Windows), paths saved absolute in the state (portability of a project between machines and OSes), tests that open C:/test/*.wav; the JSON reader / writer in src/dsp/harmonic_snapshot_json.h (locale-dependent float parsing with strtod / stof: a decimal comma locale breaks import; number formatting; recursion or size limits); signed / unsigned and narrowing mixups (int32 sample counts vs size_t, int frame indices vs size_t, int16 pitch, static_cast<int>(std::round(norm * N)) patterns), DisplayData copied by memcpy across the component boundary (layout, padding, alignment, a struct with non-trivial members?) and through the SharedDisplayBridge between two modules, KRATE_BRIDGE_LOG in release builds, <windows.h> / <unistd.h> includes in tests, uninitialised members, unused results, misleading names or comments, God functions (name the worst with line counts: Processor::process at about 1,680 lines, Controller::initialize, setState, readComponentState, analyzeOnThread, handleNoteOn), the reference-member aliases voices_ and voice_ bound to *voicesPtr_ (copy / move semantics, lifetime), about 85 std::atomic<float> members read one by one each block, magic numbers duplicating named constants, dead code and dead parameters, test-only accessors in production classes (processor.h has about a hundred getters and several test* methods: which of them weaken encapsulation or are reachable in production?), naming-convention violations (InnexusVoice mixes trailing-underscore and plain member names), header-only code with non-inline definitions, NOLINT markers that hide a real problem (several say clang-tidy "can't resolve includes": a broken tidy setup means the file is not really analysed). Focus: plugins/innexus/src/** and plugins/innexus/tests/**.`,
  },
  {
    key: 'test-gaps',
    prompt: `Hunt TEST DEFECTS and COVERAGE GAPS in plugins/innexus/tests/: assertions that cannot fail (REQUIRE(true), checking only non-silence, Approx margins wider than the effect), DIAGNOSTIC cases that print numbers and assert little (test_output_noise.cpp "Noise isolation / Noise tracing", test_real_sample_noise.cpp, test_sidechain_wav_quality.cpp: what do they pin, and do they run in the per-push gate?), cases that read WAV files from absolute paths outside the repository (C:/test/...: when the file is missing do they SKIP, silently pass, or fail? on CI and on every other machine they are vacuous), tests named for state versions the code does not write (unit/vst/test_state_v6.cpp .. test_state_v10.cpp), hidden cases that hide known reds or do real work, tests that pass on vstgui_test_stubs.cpp and would not on the real view, duplicated tests, wall-clock assertions without the [wallclock] tag, perf cases ([.perf], qs9perf, qs13perf) and whether tools/run-cpu-tests.js picks them up, tests that depend on thread timing (sleep / poll for the analysis thread: flaky under load?), temp WAV files written into the working directory and not removed, the lack of a common [innexus] tag (about 36 of ~590 cases carry it, so tag-scoped gates miss most of the suite). No determinism gate covers tools/innexus_preset_generator.cpp (tools/check-preset-generator-determinism.js supports other plugins only) and no test renders a factory preset. MISSING coverage for the riskiest behaviours: analysis accuracy against signals with KNOWN content (F0, partial frequencies and amplitudes, residual level) for both front-ends and their parity; the lowest and highest supported F0 per latency mode; a file whose sample rate differs from the host's; octave-error traps; resynthesis fidelity (spectrum and harmonic-to-noise ratio of the output vs the source); note timing vs sampleOffset and block-size invariance; host blocks above 8192 samples in sidechain mode; every exciter x resonator x voice mode for NaN and runaway at 192 kHz and at parameter extremes; state written by each RELEASED version (v0.9.4's version 9 included) loaded today; truncated and corrupt state incl. huge counts; processor / controller / generator reader parity on the same bytes; preset or state load and JSON import from the message thread while process() runs; a new sample arriving while notes sound and while another analysis runs; two superseded analyses before setActive; every factory preset RENDERED on a reference source (silence, clipping, features that cannot act) rather than only loaded; the soft limiter on ordinary playing; modulator target isolation; evolution / blend rebuild rate vs block size; the resonator switch crossfade; ADSR with the Bow exciter (envelope speed); voice-mode change with held notes; MPE note-off by noteId; the DataExchange IMessage fallback and the SharedDisplayBridge with the processor destroyed first (on Linux a bare harness has no run loop: gate such checks, do not assert them); the editor lifecycle with the sample-drop overlay and the ADSR overlay open. For each gap name the test file to extend, the TEST_CASE name and tags, and the assertion.`,
  },
  {
    key: 'prior-audit-regressions',
    prompt: `You are the PRIOR-AUDIT REGRESSION checker. Read the prior audits: (1) plugins/innexus/docs/audit-2026-07-implementation-plan.md (work items WI-1 .. WI-22 in groups A-D, the quick-sweep items QS-n / Cn in group E; code comments mention items up to WI-24: find where WI-23 / WI-24 are defined); (2) for the findings of other audits that touch Innexus or a file Innexus uses: grep -n -i "innexus" specs/ruinae-audit/report.md specs/ruinae-audit/fix-plan.md (R-nnn ids; the R-200 / R-201 preset-load items), specs/membrum-audit/report.md specs/membrum-audit/fix-plan.md (MB-nnn ids; shared headers: modal_resonator_bank.h, waveguide_string.h, impact_exciter.h, bow_exciter.h, sympathetic_resonance.h, voice_allocator.h, harmonic_oscillator_bank.h, adsr_envelope.h, and MB-114 on the ADSR overlay copies), specs/disrumpo-audit/report.md and follow-ups.md. For EVERY prior item: open today's code at the place the item names (line numbers have moved: locate by function and symbol), decide whether the defect is (a) fixed and guarded by a test, (b) fixed with no regression test, (c) partially fixed (one site fixed, a sibling site with the same pattern still wrong — e.g. fixed in the offline analyzer but not in the live pipeline, fixed for modulator 1 but not 2, fixed in the processor's reader but not the controller's or the generator), or (d) still present / reintroduced. The nine PLAUSIBLE items (WI-11 .. WI-19, "verify first") need a ruling each: was the verification done, what did it conclude, where is that recorded? Report a finding for every (c) and (d) (category prior-audit-regression, put the prior id in the title, e.g. "WI-13 still open: ..." or "WI-4 partially fixed: ..."), and a low-severity test-gap finding for each (b) that names the missing test. Do not report (a). For a shared-header defect that another audit found and fixed FOR ITS OWN plugin: check that the fix did not change behaviour Innexus depends on (run git log --oneline --grep "audit phase" -- dsp/ plugins/shared/ and read each commit's diff of the headers Innexus includes). Also look for the SAME defect patterns in Innexus code added after the 2026-07 audit (git log --since 2026-07-17 --oneline -- plugins/innexus/src).`,
  },
  {
    key: 'gestalt-processor',
    prompt: `You are the GESTALT reviewer for the processor side: read plugins/innexus/src/processor/processor.cpp END TO END, top to bottom (Processor::process alone is about 1,680 lines: read ALL of it by offsets, do not sample it), then processor_midi.cpp, processor_params.cpp and processor_messages.cpp, and flag anything that does not make sense — a comment that says one thing while the body does another, branches that cannot execute, the same atomic loaded and denormalized several times per block with different formulas, work done per sample that depends on nothing sample-varying, work redone for all eight voices when one is active, values computed then ignored, ordering that looks accidental (parameter changes vs events vs analysis hand-over vs frame selection vs render; recall before capture; the early-return path at ~785 and everything it skips: smoothers not advanced, crossfades not counted down, the feedback buffer and display data not updated, output buffers left unwritten or uncleared, silence flags), block-rate handling of things that need sample accuracy, the voice_ alias (voice 0) used as if it were global state in poly mode (ADSR display atomics, getters, freeze state, morphedFrame), state that survives setActive or a source switch by accident, counters that never reset or overflow in long sessions (displayFrameCounter_, frame indices), TODO / FIXME hiding bugs, leftovers of earlier milestones (dead members, "M4 / M5 / M6 / Spec A / Spec B / Phase N" comments describing code that no longer exists, the reserved timbral-blend remnants, test-only hooks). Your value is noticing what category-driven review misses.`,
  },
  {
    key: 'gestalt-analysis',
    prompt: `You are the GESTALT reviewer for the analysis side: read plugins/innexus/src/dsp/sample_analyzer.cpp and live_analysis_pipeline.cpp END TO END, side by side, then pre_processing_pipeline.h, envelope_detector.h, spectral_decay_envelope.h, harmonic_snapshot_json.h, and the shared headers they lean on most (dsp/include/krate/dsp/processors/partial_tracker.h, yin_pitch_detector.h, systems/harmonic_model_builder.h), and flag anything that does not make sense — header comments that describe an older pipeline (the numbered stage list at the top of sample_analyzer.cpp vs the code), a stage whose output nobody reads, the long-window spectrum's real role, thresholds and magic constants with no stated origin or with comments that contradict their values, constants that are right at 44.1 kHz only (sizes in samples, Hz-per-bin assumptions, the "10.77 Hz" style comments), units that change between stages (linear vs dB, peak vs RMS, bins vs Hz, frames vs seconds), buffers sized for one configuration and used with another, the two front-ends drifting apart, per-frame state that the offline path resets and the live path does not (or the reverse), a "pass 2" that silently discards pass 1's better data, results cached across files, early returns that leave a half-filled SampleAnalysis, error paths that publish nothing and leave the UI waiting, progress / completion reporting, memory use for a long file (frames x 96 partials + residual: megabytes?) and analysis time, cancellation points inside the long loops. Your value is noticing what category-driven review misses.`,
  },
  {
    key: 'gestalt-controller',
    prompt: `You are the GESTALT reviewer for the UI side: read plugins/innexus/src/controller/controller.h and controller.cpp END TO END, then controller_state.cpp, controller_presets.cpp, controller_view_sync.cpp, controller_adsr.cpp, controller_param_display.cpp, modulator_sub_controller.cpp, sample_drop_target.h, then the files under plugins/innexus/src/controller/views/, and flag anything that does not make sense — repeated registration boilerplate with one copy subtly different (the two modulators, the eight blend weights, the nine ADSR parameters), string-list parameters whose entries disagree with the DSP enums, handlers that update a view but not the parameter (or the reverse), lifecycle code that assumes the editor exists, state kept in the controller that the processor never learns about (and is lost on reload) or the reverse (the loaded file name, memory-slot occupancy, the detected ADSR), a 570-line state reader that mirrors the processor's by hand, visibility code that walks the view tree by tag, display code that reads processor memory through a raw pointer, dead handlers and members, misleading comments, files whose parts belong elsewhere. Your value is noticing what category-driven review misses.`,
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
  'CONTEXT: is it intentional, guarded elsewhere (a clamp upstream, a reset in setActive, a test that pins it), unreachable in a real host, or already documented as a design decision in the specs (specs/INNEXUS-ROADMAP.md, the archived milestone specs specs/_archive_/115..124 and 127..132, specs/_architecture_/innexus-plugin.md), the manual, a prior audit ruling (plugins/innexus/docs/audit-2026-07-implementation-plan.md), plugins/innexus/docs/polyphonic-f0-design-notes-2026-07.md or a code comment? Intended by design: the harmonic sieve running only for a voiced F0 and being switched off conditionally for polyphonic material; pass 2 writing bandwidth 0; oscillator phase kept across frames; kPartialCountId as a StringListParameter; the seven fixed preset categories; a superseded analysis freed only in setActive or the destructor.',
  'IMPACT: assume the defect is real — does it produce the claimed consequence at the claimed severity? Quantify (dB, cents, samples, milliseconds, which hosts/settings/sources trigger it); downgrade or upgrade accordingly, and refute if the consequence cannot occur.',
]
// Literature findings (from the Research phase) need a literature check, not only a code check.
const PHYSICS_LENSES = [
  'CODE: re-read the cited code. Does it really implement the formula, constants or topology the finding attributes to it (the "claim as coded")? Refute if the reviewer misread the code, read a path that is not the one in use (the offline analyzer vs the live pipeline, a latency mode, a disabled branch), or the code changed.',
  'LITERATURE: independently check the cited sources with web search and fetch. Do they say what the finding claims, for THIS algorithm or physical system (the YIN variant in use, this window, a harmonic vs an inharmonic source, a string vs a bar)? Refute if the sources are misquoted, apply to a different method or system, are only blogs or forums, or if the "correct form" is itself disputed in the literature.',
  'CONTEXT AND IMPACT: is the deviation a deliberate, documented simplification (harmonic physics is a creative model by design; plugins/innexus/docs/audit-2026-07-implementation-plan.md, plugins/innexus/docs/polyphonic-f0-design-notes-2026-07.md, the archived specs\' research.md files and section 5 of specs/membrum-audit/report.md record design choices and rulings)? Assume it is real: would correcting it be audible, and at the claimed severity? Refute if it is intentional and documented, or inaudible.',
]
// Verification is throttled while the measurement lane still runs: a full-width
// fan-out (N findings x 3 lenses) fills every agent slot and the lane's next
// step (tidy, portability) queues behind it for an hour (disrumpo-audit run
// wf_b866487e, 2026-11-05). Chunks of 4 findings = 12 agents leave slots for the
// lane; full width once it is done. (laneDone is declared with the args at the
// top so the lane's .then never hits a TDZ.)
async function verifyAll(list, tag, lenses = LENSES, what = 'this defect in Innexus') {
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
  const interp = await agent(`${CONTEXT}\n\nSUBSYSTEM MAPS:\n${MAP_DIGEST}\n\nMEASUREMENT LANE RESULTS (facts produced by running the code; the probe sources are ${OUT_DIR}/probes/audit_probes.cpp.txt, audit_analysis_probes.cpp.txt and audit_audibility_probes.cpp.txt if they ran, with their CSV tables beside them):\n${laneDigest}\n\nTurn these into FINDINGS. Rules: every test-red, pluginval failure, build warning, portability error, probe "bad" and probe "suspicious" measurement becomes a finding IF you can localise it — read the implicated code and cite file:line (for a probe, the DSP code responsible, plus the probe name in evidence). Before blaming the plugin for a probe result, read the probe's own code for that measurement: a harness mistake (wrong parameter ID, a context in which the parameter cannot matter, a test signal outside the documented range, an analysis that had not finished, a preset not loaded) is NOT a finding. ANALYSIS-ACCURACY results: one finding per distinct failure (category measured-analysis: an F0 or octave error, missing or spurious partials, amplitude or frequency bias, inflated bandwidth, a wrong residual level, a wrong DetectedADSR, voiced = true on a non-pitched signal), naming the stage that loses the information (pre-processing, YIN, the STFT scaling, peak interpolation, the sieve, the model builder, the residual analyzer) with file:line, and saying whether it affects the offline analyzer, the live pipeline or both; a disagreement between the two front-ends on the same signal is a finding of its own. RESYNTHESIS-QUALITY results: one finding per source of error (category measured-resynthesis: harmonic amplitude error, added noise = a harmonic-to-noise drop, a spectral or level difference, a pitch error, an envelope mismatch), saying whether the ANALYSIS already lost it (link the analysis finding) or the SYNTHESIS adds it (oscillator bank, residual synthesizer, levels, the limiter). AUDIBILITY results: one finding per dead or weak parameter (category measured-dead-parameter: trace the parameter from its atomic to the DSP and name the line where its effect is lost; dead = high, weak = medium or low); inert preset features grouped as ONE finding per cause (category measured-preset: for example "N presets enable Evolution with no occupied memory slot"; list the presets), each saying whether the cause is the preset DATA (tools/innexus_preset_generator.cpp: quote the values) or the ENGINE (the feature cannot work without data a preset does not carry); near-duplicate presets grouped as one finding per category and one per cross-category cluster; presets that are silent, ride the limiter, carry DC or never end are findings of their own; flat velocity response is one finding listing the presets. Clang-tidy clusters: report only clusters that indicate a real defect (bugprone-*, clang-analyzer-*, performance-* that touches the audio thread, cert-*), one finding per cluster, severity low unless the check proves a bug; style-only clusters are NOT findings (the report lists them from the lane summary). Lints: one finding per item. category must start with "measured-" (measured-test, measured-pluginval, measured-probe, measured-analysis, measured-resynthesis, measured-dead-parameter, measured-preset, measured-tidy, measured-lint, measured-portability, measured-build). Evidence must quote the number or message verbatim.`, {
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
// Literature findings — every literature verdict that is not "confirmed" or
// "nuanced" becomes a finding and goes through the literature lenses.
// ---------------------------------------------------------------------------
const researchClaims = research.claims || []
const physicsRaw = researchClaims
  .filter(c => ['contradicted', 'oversimplified', 'unsupported'].includes(c.verdict) && c.severity !== 'none')
  .map(c => ({
    title: `Literature (${c.verdict}): ${c.title}`,
    severity: c.severity,
    category: `literature-${c.verdict}`,
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
log(`Research: ${researchClaims.length} claims checked against the literature, ${physicsRaw.length} not confirmed -> ${physicsNew.length} literature findings to verify`)
const verifiedPhysics = physicsNew.length
  ? (await verifyAll(physicsNew, 'research', PHYSICS_LENSES, 'that an Innexus algorithm or physical model deviates from the published literature')).filter(Boolean)
  : []

// Finding ids are INX-nnn; the prior audits' ids (WI-n, QS-n / Cn, R-nnn, MB-nnn) stay distinct.
let verified = [...verifiedStatic, ...verifiedMeasured, ...verifiedPhysics]
verified.forEach((f, i) => { f.id = `INX-${String(i + 1).padStart(3, '0')}` })
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
const critique = await agent(`${CONTEXT}\n\nYou are the COMPLETENESS CRITIC of this audit. Below is everything the audit produced so far. Ask: what is missing? Which subsystem, hazard class or file got no finder attention (compare the dimension list and the maps against the file list under plugins/innexus/src — every file under src/dsp and src/controller/views included — and the ~36 KrateDSP headers Innexus includes)? Which measurement did not run or did not cover a hazard the static finders flagged? Which algorithmic or physical claim in the code or the manual was never checked against the literature (kind unresearched-claim)? Which REFUTED finding was refuted for a weak reason (a skeptic citing intent without a code line, refuting on severity rather than existence, or a literature skeptic who opened no source)? Which CONFIRMED finding has verifier notes that actually disagree with each other?\n\nDIMENSIONS RUN: ${DIMENSIONS.map(d => d.key).join(', ')}${failedFinders.length ? ` (FAILED: ${failedFinders.join(', ')})` : ''}\nMEASUREMENT STEPS NOT RUN: ${laneNotRun.join(', ') || 'none'}\nRESEARCH TOPICS RUN: ${research.skipped ? 'none (skipped by args)' : RESEARCH_TOPICS.map(t => t.key).concat((research.extra_topics || []).map(k => `extra:${k}`)).join(', ')}${(research.failed || []).length ? ` (FAILED: ${research.failed.join(', ')})` : ''}\n\nINVENTORY:\n${INVENTORY_DIGEST}\n\nMAPS:\n${MAP_DIGEST}\n\nLANE SUMMARIES:\n${laneSteps.map(s => `- ${s.key}: ${s.result.summary}`).join('\n')}\n\nCLAIMS CHECKED AGAINST THE LITERATURE (verdict, title, file:line):\n${researchClaims.map(c => `- [${c.verdict}/${c.severity}] ${c.title} — ${c.file}:${c.line}`).join('\n')}\n\nALL FINDINGS WITH VERDICTS (id, verdict, severity, title, file:line, verifier notes):\n${verified.map(f => `${f.id} [${f.verdict}/${f.final_severity}] ${f.title} — ${f.file}:${f.line}\n  notes: ${(f.verifier_notes || []).map(n => n.slice(0, 300)).join(' || ')}`).join('\n')}\n\nSpot-check the code where you doubt a verdict. Return up to 6 follow-up finder prompts, each naming concrete files and one hazard class (a prompt may ask for a literature check of one named claim), and the IDs of refuted findings that deserve a tie-break.`, {
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
          agent(`${CONTEXT}\n\nINVENTORY:\n${INVENTORY_DIGEST}\n\nSUBSYSTEM MAPS:\n${MAP_DIGEST}\n\nALREADY REPORTED (do not repeat these; find what they missed):\n${verified.map(f => `- ${f.title} — ${f.file}:${f.line}`).join('\n')}\n\nYOUR FOLLOW-UP DIMENSION (from the completeness critic): ${d.prompt}\n\nRead the actual code before reporting anything; cite file:line you personally read.\n\nIF the critic asked for a LITERATURE check: read the code for the actual values, open authoritative sources with web search and fetch (textbooks and peer-reviewed papers are ground truth; blogs and forums never suffice alone), and put the citations (authors, title, year, URL, what the source establishes) in why_wrong; category literature-contradicted / literature-oversimplified / literature-unsupported.\n\nIF, AND ONLY IF, the critic asked for a MEASUREMENT you cannot make by reading: you may add ONE temporary hidden [.][audit-probe] TEST_CASE file under plugins/innexus/tests/audit/ (+ its line in plugins/innexus/tests/CMakeLists.txt), build with  ${CMAKE} --build ${BUILD_DIR} --config Release --target innexus_tests -- -m:2  (TWO MSBuild nodes, never more: another agent may be building too), run it via  node tools/run-gate.js --share 4 --filter '[audit-probe]' innexus_tests , log under ${LOG_DIR}/, copy the TU (with a .cpp.txt suffix) and its results to ${OUT_DIR}/probes/ (a new file name: never overwrite audit_probes.cpp.txt, audit_analysis_probes.cpp.txt or audit_audibility_probes.cpp.txt), then  git checkout -- plugins/innexus/tests/CMakeLists.txt , delete tests/audit/, and confirm  git status --porcelain plugins/innexus  is empty. Edit nothing else.`, {
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
            agent(`${CONTEXT}\n\nTIE-BREAK. Three skeptics refuted this finding; the completeness critic doubts the refutation. Read the code yourself (for a literature finding, also open the cited sources) and rule. Default to refuted=true only if the code or the literature positively shows the defect cannot occur.\n\nFINDING:\n${JSON.stringify(f, null, 2)}`, {
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
  const novelPhysics = novel.filter(f => String(f.category).startsWith('literature-'))
  const novelCode = novel.filter(f => !String(f.category).startsWith('literature-'))
  const verifiedFollow = [
    ...(novelCode.length ? await verifyAll(novelCode, 'followup') : []),
    ...(novelPhysics.length ? await verifyAll(novelPhysics, 'followup', PHYSICS_LENSES, 'that an Innexus algorithm or physical model deviates from the published literature') : []),
  ].filter(Boolean)
  verifiedFollow.forEach((f, i) => { f.id = `INX-${String(verified.length + i + 1).padStart(3, '0')}` })
  verified = verified.concat(verifiedFollow)
  followupAdded = verifiedFollow.length
}

const surviving = verified.filter(f => f.verdict === 'CONFIRMED' || f.verdict === 'PLAUSIBLE')
const refuted = verified.filter(f => f.verdict === 'REFUTED')
const unverified = verified.filter(f => f.verdict === 'UNVERIFIED')
log(`Final: ${surviving.length} surviving (${tally(surviving).confirmed} CONFIRMED, ${tally(surviving).plausible} PLAUSIBLE), ${refuted.length} refuted, ${unverified.length} unverified`)

// ---------------------------------------------------------------------------
// Synthesize — Fable writes report.md and fix-plan.md to specs/innexus-audit/.
// ---------------------------------------------------------------------------
phase('Synthesize')
const sevOrder = { critical: 0, high: 1, medium: 2, low: 3 }
const verdictOrder = f => (f.verdict === 'CONFIRMED' ? 0 : 1)
const sortedSurviving = [...surviving].sort((a, b) => (sevOrder[a.final_severity] - sevOrder[b.final_severity]) || (verdictOrder(a) - verdictOrder(b)) || String(a.id).localeCompare(String(b.id)))
const laneForReport = laneSteps.map(s => ({ key: s.key, ...s.result }))
const probeBaseline = {
  probes: (laneSteps.find(s => s.key === 'probes') || {}).result || null,
  analysis: (laneSteps.find(s => s.key === 'analysis') || {}).result || null,
  audibility: (laneSteps.find(s => s.key === 'audibility') || {}).result || null,
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
const STATS_LINE = `raw static findings ${rawStatic.length}, deduped ${deduped.length}, measured ${measured.length}, literature ${physicsNew.length}, follow-up ${followupAdded}, confirmed ${tally(surviving).confirmed}, plausible ${tally(surviving).plausible}, refuted ${refuted.length}, unverified ${unverified.length}, tie-breaks upheld ${rejudged}, finders failed: ${failedFinders.join(', ') || 'none'}, measurement steps not run: ${laneNotRun.join(', ') || 'none'}, research topics failed: ${(research.failed || []).join(', ') || 'none'}${research.skipped ? ' (research skipped by args)' : ''}`
const FINDING_FORMAT = `Each finding: heading "#### <id> — <title>", a line with severity / verdict / category / dimension(s) incl. also_flagged_by, corroborated_by_measurement and corroborated_by_research when present, then file:line (as a markdown link relative to the repo root), Problem, Why wrong, Consequence, Evidence, Suggested fix, and "Verifier notes" as a short bullet list (compress each note to its key sentence, keep the file:line citations and source citations). Do NOT drop or merge findings; do not soften verdicts; keep the order given.`

// ---- Report parts ----
const reportJobs = []
reportJobs.push({
  file: '00-summary.md', label: 'synthesize:report-summary', model: 'fable',
  prompt: `You are writing the OPENING of the Innexus AUDIT REPORT. Audience: the repo owner and a coding agent that will fix things; it must stand alone (no reference to "this session").\n\nDATE: ${DATE}. Plugin version: read plugins/innexus/version.json. HEAD: run  git rev-parse --short HEAD .\n\nCONTENT:\n1. "# Innexus audit — ${DATE}", then date, version, commit, and "## 1. Executive summary": one paragraph on what was audited and how (${DIMENSIONS.length} static dimensions + ${laneSteps.length} measurement steps + ${researchClaims.length} algorithmic and physical claims checked against the literature, 3-lens adversarial verification, critic round), the headline defects (the critical and high findings below, grouped by theme, with their ids), and one line on how this audit relates to the prior ones (plugins/innexus/docs/audit-2026-07-implementation-plan.md with its WI-n / QS-n items, and the Ruinae, Membrum and Disrumpo audit items that touch Innexus or a shared file; ids here are INX-nnn). Then a short list of the report's sections: 2 Statistics, 3 Measurement lane, 4 Analysis, resynthesis and audibility, 5 Algorithm and physical-model review, 6 Findings, 7 Prior-audit status, 8 Inventory cross-reference, Appendices A-C. The fix plan is fix-plan.md next to this file.\n2. "## 2. Statistics": a table of: ${STATS_LINE}; plus a table of surviving findings by severity (critical ${sevCount('critical')}, high ${sevCount('high')}, medium ${sevCount('medium')}, low ${sevCount('low')}) and one by category (count them from the digest).\n\nDIGEST OF EVERY SURVIVING FINDING:\n${JSON.stringify(sortedSurviving.map(digestOf), null, 1)}`,
})
reportJobs.push({
  file: '10-measurement.md', label: 'synthesize:report-measurement', model: 'opus',
  prompt: `You are writing sections 3 and 4 of the Innexus AUDIT REPORT.\n\n"## 3. Measurement lane": one "### <step>" subsection per step with its summary verbatim, verdict line, log path, and a table of its items (tidy clusters as a table of check/count/files). Probe measurements as a table (name, setup, metric, value, threshold, verdict, note) — ALL of them, ok ones included, because they are the baseline for after the fixes. Probe sources: ${OUT_DIR}/probes/audit_probes.cpp.txt, audit_analysis_probes.cpp.txt and audit_audibility_probes.cpp.txt.\n\n"## 4. Analysis, resynthesis and audibility" with three subsections. "### 4.1 Analysis accuracy" (from the analysis step): a table per test signal and front-end (offline, live low latency, live high precision) with F0 error, voiced share, partials found / expected, the worst amplitude and frequency error among the first 8 partials, bandwidth, residual level error and verdict; then the offline-vs-live differences, the envelope-detection rows and the polyphony rows. "### 4.2 Resynthesis quality" (same step): a table per source and condition with log-spectral distance, worst harmonic error, harmonic-to-noise change, level and pitch difference and verdict. "### 4.3 Presets and parameters" (from the audibility step): a table of the 54 presets (category, input source, flags, inert features, nearest other preset and its distance, distance from the init state, velocity response), the per-category spread, and the parameter-audibility table (every suspicious and bad parameter with its numbers, plus the ok count). Link the CSV files under ${OUT_DIR}/probes/ (list that directory) for the full rows. State the definitions (signals, features, distances, thresholds) the probes used; read the probe sources under ${OUT_DIR}/probes/ for them when a summary does not say. A step that did not run gets its subsection with the line "Not run." and the reason.\n\nLANE:\n${JSON.stringify(laneForReport, null, 1)}`,
})
const physicsFindings = sortedSurviving.filter(f => String(f.dimension).startsWith('research:') || f.source === 'research').map(f => ({ id: f.id, title: f.title, at: `${f.file}:${f.line}` }))
const researchHalves = sliceBy(researchClaims, Math.max(1, Math.ceil(researchClaims.length / 2)))
researchHalves.forEach((claims, i) => {
  const from = i * researchHalves[0].length + 1
  reportJobs.push({
    file: `2${i}-physics.md`, label: `synthesize:report-physics-${i + 1}`, model: 'opus',
    prompt: `You are writing ${i === 0 ? 'the start of section 5' : 'a continuation of section 5'} of the Innexus AUDIT REPORT: the algorithm and physical-model review.\n\n${i === 0 ? 'Start with "## 5. Algorithm and physical-model review" and one sentence on method (each analysis, resynthesis or physical-modelling claim in the code checked against textbooks and peer-reviewed papers; confirmed ones listed so nobody re-checks them). Then ' : 'Start with '}"### 5.${i + 1} Claims ${from}-${from + claims.length - 1}": a table of EVERY claim below (title, file:line, verdict, evidence quality, correct form in one line, key source, severity, INX-id when it became a finding — match by title and file against the LITERATURE FINDINGS list, "-" otherwise), then "#### Sources for claims ${from}-${from + claims.length - 1}": per claim, its full source list (authors, title, year, type, URL, what it establishes).\n\nLITERATURE FINDINGS (id, title, location):\n${JSON.stringify(physicsFindings, null, 1)}\n\nCLAIMS:\n${JSON.stringify(claims, null, 1)}`,
  })
})
const findingChunks = sliceBy(sortedSurviving, 20)
findingChunks.forEach((chunk, i) => {
  const groupStarts = chunk.filter((f, j) => (j === 0 ? (i === 0 || findingChunks[i - 1][findingChunks[i - 1].length - 1].final_severity !== f.final_severity) : chunk[j - 1].final_severity !== f.final_severity))
  reportJobs.push({
    file: `3${pad2(i)}-findings.md`, label: `synthesize:report-findings-${i + 1}`, model: 'opus',
    prompt: `You are writing part ${i + 1} of ${findingChunks.length} of section 6 (Findings) of the Innexus AUDIT REPORT. Findings are grouped critical -> high -> medium -> low, CONFIRMED before PLAUSIBLE within a group; the input below is already in that order.\n\nHEADINGS: ${i === 0 ? 'start with "## 6. Findings". ' : ''}${groupStarts.length ? groupStarts.map(f => `immediately before ${f.id} write "### ${sevName(f.final_severity)} (${sevCount(f.final_severity)})"`).join('; ') : 'no group heading in this part (it continues the previous group)'}. Nothing else above the "####" finding headings.\n\n${FINDING_FORMAT}\n\nFINDINGS (${chunk.length}):\n${JSON.stringify(chunk.map(f => capNotes(f, 2500)), null, 1)}`,
  })
})
const priorFindings = sortedSurviving.filter(f => f.dimension === 'prior-audit-regressions' || (f.also_flagged_by || []).includes('prior-audit-regressions'))
reportJobs.push({
  file: '70-prior-audits.md', label: 'synthesize:report-prior', model: 'opus',
  prompt: `You are writing section 7 of the Innexus AUDIT REPORT: "## 7. Prior-audit status". A table of the prior finding ids (the WI-n and QS-n / Cn items of plugins/innexus/docs/audit-2026-07-implementation-plan.md, and the Ruinae R-nnn, Membrum MB-nnn and Disrumpo items that touch Innexus or a file it uses) that this audit found still present, never verified (the plan's verify-first items), or partially fixed, with the INX-id that tracks each. Work from the findings below; open the prior documents to get the prior ids and titles right.\n\nFINDINGS FROM THE PRIOR-AUDIT-REGRESSIONS DIMENSION:\n${JSON.stringify(priorFindings.map(f => capNotes(f, 1500)), null, 1)}`,
})
reportJobs.push({
  file: '80-inventories.md', label: 'synthesize:report-inventories', model: 'sonnet',
  prompt: `You are writing section 8 of the Innexus AUDIT REPORT: "## 8. Inventory cross-reference". All the inventory cross-reference tables below (one "### " subsection each) and their full mismatch lists. Keep every row.\n\nINVENTORIES:\n${JSON.stringify(goodInv, null, 1)}`,
})
reportJobs.push({
  file: '90-appendices.md', label: 'synthesize:report-appendices', model: 'sonnet',
  prompt: `You are writing the appendices of the Innexus AUDIT REPORT.\n"## Appendix A — Refuted findings": a table of id, title, file:line, one-line refutation reason each (so nobody re-finds them; take the reason from the verifier notes).\n"## Appendix B — Unverified findings": a table, or "None." when empty.\n"## Appendix C — Critic gaps": the critic's gap list verbatim (kind, detail).\n\nREFUTED:\n${JSON.stringify(refuted.map(f => ({ id: f.id, title: f.title, file: f.file, line: f.line, verifier_notes: capNotes(f, 800).verifier_notes })), null, 1)}\n\nUNVERIFIED:\n${JSON.stringify(unverified.map(f => ({ id: f.id, title: f.title, file: f.file, line: f.line })), null, 1)}\n\nCRITIC GAPS:\n${JSON.stringify(critique ? critique.gaps : [], null, 1)}`,
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
          flags: { type: 'array', items: { type: 'string', enum: ['CROSS-PLUGIN IMPACT', 'STATE-COMPAT', 'PRESET-IMPACT', 'LITERATURE', 'UI', 'THREADING', 'MACOS', 'VERIFY-FIRST'] } },
          estimate_min: { type: 'integer' },
        },
      },
    },
  },
}
const PLAN_RULES = `PLAN REQUIREMENTS:\n- Phases in this order: (1) critical/high correctness and audio defects (crashes, threading, wrong or unsafe audio, states of released versions that no longer load, timing first), (2) medium defects, (3) measured defects not already covered (test reds, pluginval, portability, tidy clusters that prove bugs), (4) analysis, resynthesis and physical-model corrections (the literature findings and the measured analysis / resynthesis errors) — they change how every source sounds, so they come BEFORE any preset data is touched; inside the phase fix the ANALYSIS before the synthesis that consumes it, (5) dead and weak parameters, then preset data (inert features, near-duplicate presets) through tools/innexus_preset_generator.cpp — never retune a preset against a parameter or a model that an earlier task still has to fix, (6) duplication / parallel-path / spec-conformance / anti-pattern cleanups, (7) test-gap fills, (8) promote the useful audit probes from ${OUT_DIR}/probes/audit_probes.cpp.txt, audit_analysis_probes.cpp.txt and audit_audibility_probes.cpp.txt into real regression tests (un-hidden tags, real assertions with the thresholds the report names, synthesised signals only: no file from outside the repository, placed under plugins/innexus/tests/unit/ or integration/, registered in plugins/innexus/tests/CMakeLists.txt; a case that renders more than ~15 s of toolchain-independent audio gets the [long] tag, a wall-clock assertion gets [wallclock]). Low findings go to the phase their category belongs to, after the medium ones. Within a phase order so no task depends on a later one; name hard prerequisites. Several findings with one root cause or one file region share a task.\n- Every task follows this repo's mandatory workflow:\n  1. write the failing test (test file to create/extend, exact TEST_CASE name and tags, what it asserts and with which helper from tests/test_helpers/); for a defect in a shared DSP header the test goes in dsp/tests/unit/<layer>/ and its dsp_<layer>_tests target\n  2. implement the fix (exact files, functions, before/after)\n  3. build, capped: "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target innexus_tests -- -m:2   (add Innexus when plugin sources changed; add the dsp_<layer>_tests target when a dsp/ file changed)\n  4. run SHARDED, never serial: node tools/run-gate.js --share 4 --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' innexus_tests   — and when a dsp/include or plugins/shared file was MODIFIED, first run  node tools/header-consumers.js --tag '[innexus]'  (only a minority of Innexus's cases carry that tag, so innexus_tests itself is in its list: that is expected) and add the executables its last line names to the same run-gate command with ~[long] appended to the filter, building them first\n  5. pluginval (tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"; exit code is the verdict) when plugin sources changed, clang-tidy (./tools/run-clang-tidy.ps1 -Target innexus -BuildDir build/windows-ninja -Jobs 4) before the commit, ONE heavy job at a time\n  6. commit (git add, then git commit -F <message file>; never --amend; never push)\n- Flags: CROSS-PLUGIN IMPACT = touches dsp/include/krate/dsp/ or plugins/shared/ (name the other consumers via grep -rl of the header under plugins/; a shared root-cause fix is in scope and gates every consumer). STATE-COMPAT = changes the saved-state layout. The format is ONE int32 version plus a fixed field sequence that FOUR pieces of code must agree on — Processor::getState, Processor::setState, Controller::readComponentState and tools/innexus_preset_format.h: change all four in the same task and add or extend the reader-parity test that feeds the same bytes to the processor and to the controller. Append new fields at the END only (an older, shorter stream must keep loading with defaults), or bump the version and gate the read on it; regenerate the 54 factory presets (build target generate_innexus_presets) when the layout or a default changes. States written by RELEASED builds must keep loading: the report lists which version each release wrote; where it established that a released build's state is rejected or misread, add a read path for that version instead of rejecting it, with a test that loads bytes in that release's layout. Never swap a parameter type at an existing ID (kPartialCountId stays a StringListParameter), never renumber an ID. PRESET-IMPACT = changes how a factory preset sounds or what it contains: regenerate the presets with the generator (never hand-edit a .vstpreset), install through the build target (never hand-copy), keep the seven categories (Brass and Winds, Drums and Perc, Found Sound, Keys, Pads and Drones, Strings, Voice), re-run the audibility probe and list the before/after numbers of every affected preset; a change the user would hear as a different preset, and any decision about what a factory preset should carry (a sample, memory-slot content), needs the user's ruling before the commit. LITERATURE = new values or formulas carry their source (authors, title, year, equation or table) in a code comment AND in a test that checks the coded behaviour against the published one or against a signal with known content; when the change alters the analysis or the sound, re-run the analysis probe and list the before/after numbers; it is also PRESET-IMPACT when presets sound different afterwards. Two constraints hold through every such change: the mono harmonic sieve is never disabled globally, and nothing may inflate per-partial bandwidth. UI = never remove an interaction to fix a bug; bulk editor.uidesc changes (more than about three controls) go through an XSLT stylesheet, not string replacement; after an editor change rerun  node tools/gen-screenshots.js innexus . THREADING = state applied from the message thread or handed over by the analysis thread: a deterministic interleaving test plus a ThreadSanitizer run where the repo supports it; hand state to the audio thread through a transfer object or an atomic flag, never a lock, and never free on the audio thread. MACOS = fast-math, Apple globals, finiteness checks, IEEE flag sets: run tools/macos-fastmath-repro.sh, tools/lint-apple-globals.js, tools/lint-ieee-flag-sets.js, tools/check-portability.js before pushing. VERIFY-FIRST = a PLAUSIBLE finding (one skeptic refuted it, or a tie-break upheld it): the failing test doubles as the verification; if it passes on current code the task ends with the finding marked refuted and the test kept.`
const reportPartsP = parallel(reportJobs.map(j => () => runPart(REPORT_PARTS, j)))
const planSkeleton = await agent(`${CONTEXT}\n\nYou are the PLANNER of the Innexus audit FIX PLAN. It will be executed by a coding agent working task-by-task with NO memory of this audit. Detail writers will expand each of your tasks afterwards from the full finding data; your job is the structure, and two part files.\n\n1. Return phases and tasks (schema). Every finding id in the digest belongs to exactly one task. Task ids are T<phase>.<n> (T1.1, T1.2, ...), numbered in execution order.\n2. ${PART_RULES}\nPART FILE A: ${PLAN_PARTS}/00-head.md — "# Innexus audit fix plan — ${DATE}", a one-paragraph scope (companion report: ${OUT_DIR}/report.md), "## How to execute a task" (the six-step workflow below, ONCE, with the exact commands), "## Flags" (each flag's meaning and obligations below, ONCE), and "## Phases" (a table: phase, name, goal, task count, total estimate). Detail writers will refer to these sections instead of repeating them.\nPART FILE B: ${PLAN_PARTS}/99-checklist.md — "## Completion checklist" mirroring the repo's Completion Honesty rules: each fix verified against the original finding with file:line evidence and the test's actual output, no relaxed thresholds, no quietly removed features, probes re-run and compared with the measurement baseline.\n\n${PLAN_RULES}\n\nMEASUREMENT BASELINE (summaries; full tables in ${OUT_DIR}/probes/*.csv):\n${JSON.stringify({ probes: (probeBaseline.probes || {}).summary || null, analysis: (probeBaseline.analysis || {}).summary || null, audibility: (probeBaseline.audibility || {}).summary || null }, null, 1)}\n\nDIGEST OF EVERY VERIFIED FINDING (CONFIRMED = no skeptic refuted; PLAUSIBLE = one did, or a tie-break upheld — flag those VERIFY-FIRST):\n${JSON.stringify(sortedSurviving.map(digestOf), null, 1)}`, {
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
    prompt: `You are writing part ${i + 1} of ${taskBatches.length} of the task list of the Innexus audit FIX PLAN. A coding agent with NO memory of this audit executes it task by task, so every task must be self-contained: never say "fix the issue" — spell out the exact edit intent, the file:line anchors (re-read the code to make sure every anchor is right), the before/after behaviour and the acceptance check. First read ${PLAN_PARTS}/00-head.md: it defines the standard six-step workflow and the flags; refer to them, do not repeat them. The companion report (${OUT_DIR}/report.md) carries the full finding prose; reference finding ids, do not duplicate it.\n\nHEADINGS: ${phaseHeads.length ? phaseHeads.map(p => `immediately before the first task of phase ${p.n} write "## Phase ${p.n} — ${p.name}" and the line "${p.goal}"`).join('; ') : 'no phase heading in this part (it continues the previous phase)'}. Each task: "### <task id> — <title>", then: Findings (ids with severity and verdict), Flags, Prerequisites, Estimate (minutes), "1. Failing test" (file, exact TEST_CASE name and tags, what it asserts, which helper from tests/test_helpers/), "2. Fix" (exact files and functions, before/after), "3. Build and gate" (only what differs from the standard workflow: extra targets, header-consumers executables for CROSS-PLUGIN tasks, preset regeneration for PRESET-IMPACT), "4. Acceptance" (the measurable check, with the baseline number where a probe measured one). Keep the tasks in the order given, with the given ids, titles and flags (add a flag only if the code proves it applies, and say why).\n\nTASKS:\n${JSON.stringify(batch, null, 1)}\n\nFINDINGS THESE TASKS COVER:\n${JSON.stringify(batch.flatMap(t => t.finding_ids).map(id => capNotes(findingById.get(id), 1500)), null, 1)}`,
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
const JOIN_RULES = (dir, out, headingRe, ids) => `Join the part files: write a small Node.js script (Write tool) to f:/tmp/innexus-audit/join-parts.js that reads every .md file in ${dir} in file-name order, trims each, joins them separated by one blank line, writes the result to ${out}, and prints the part names it joined. Run it with node. Then write a second small Node.js script that reads ${out}, collects every heading matching the regular expression ${headingRe}, and reports any expected id that appears zero times or more than once; the expected ids are: ${ids.join(' ')}. If the check is clean, delete the directory ${dir} (rm -rf) and confirm with  git status --porcelain ${OUT_DIR}  that it is gone. If the check is not clean, keep ${dir} and report the missing and duplicated ids. Edit nothing else. Return the path and a summary with the part count, the id check result and the byte size of ${out}.`
const [report, plan] = await parallel([
  () => {
    const missing = missingParts(reportJobs, reportPartsR)
    return agent(`${CONTEXT}\n\nYou are ASSEMBLING the Innexus AUDIT REPORT from part files written by other agents.\n${missing.length ? `These parts failed to be written; FIRST create each one with the Write tool, containing only the line "> PART MISSING: <file name> — the writer agent failed; rerun the synthesis for it.": ${missing.map(f => `${REPORT_PARTS}/${f}`).join(', ')}\n` : ''}${JOIN_RULES(REPORT_PARTS, `${OUT_DIR}/report.md`, '^#### (INX-\\d+) ', sortedSurviving.map(f => f.id))}`, {
      label: 'synthesize:report-join',
      phase: 'Synthesize',
      schema: DOC_SCHEMA,
      model: 'sonnet',
    })
  },
  () => {
    const missing = missingParts(planJobs, planPartsR).concat(planSkeleton ? [] : ['00-head.md', '99-checklist.md'])
    return agent(`${CONTEXT}\n\nYou are ASSEMBLING the Innexus audit FIX PLAN from part files written by other agents.\nFIRST write the following text VERBATIM with the Write tool to ${PLAN_PARTS}/10-findings-to-tasks.md:\n\n${findingsTable}\n\n${missing.length ? `These parts failed to be written; create each one with the Write tool, containing only the line "> PART MISSING: <file name> — the writer agent failed; rerun the synthesis for it.": ${missing.map(f => `${PLAN_PARTS}/${f}`).join(', ')}\n` : ''}${JOIN_RULES(PLAN_PARTS, `${OUT_DIR}/fix-plan.md`, '^### (T\\d+\\.\\d+) ', planTasks.map(t => t.task_id))}`, {
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
    analysis_tree_clean: (probeBaseline.analysis || {}).tree_clean,
    audibility_tree_clean: (probeBaseline.audibility || {}).tree_clean,
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
