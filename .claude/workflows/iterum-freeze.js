// Iterum Freeze mode: research, design and rebuild of the Freeze delay mode's signal path (PatternFreezeMode).
//
// Why (user, 2026-10-29): "the default settings in freeze don't produce ANY audible effects, I only hear the input
// signal". Measured by the main loop the same day (hidden probe, defaults, 120 BPM, a tone that changes pitch every
// second): the wet signal is -10 dB re input and is the LIVE INPUT delayed by one slice length (Euclidean: r = 0.69
// at a 175 ms lag, active 59 % of the time), so at 50 % mix it reads as a faint comb colouring or a weak slapback.
//
// Facts established before this workflow was written (verify, never assume; they are the seeds):
//   - Freeze never freezes. RollingCaptureBuffer rolls continuously (spec 069 FR-004 wanted that), but nothing ever
//     takes a snapshot: onPatternTrigger cuts a fresh slice from the NEWEST sliceLengthMs of input at every hit
//     (extractSlice offset 0 = the last N samples) and plays it at once. The archived spec (specs/_archive_/069-
//     pattern-freeze/spec.md) described a Trigger button and a captured slice that is held; the implementation made
//     freeze "always active" (processor.cpp Freeze branch: setFreezeEnabled(true) every block, "no checkbox needed").
//   - Harmonic Drones and Noise Bursts play BACKWARDS: both read sample by sample through extractSlice(.., 1, offset)
//     with an increasing offset, and offset means "samples before the newest sample", so the read walks into the
//     past; Drones add a modulo wrap on top (pattern_freeze_mode.h processHarmonicDrones / processNoiseBursts).
//   - The PatternScheduler free-runs from plugin start: never phased to ctx.projectTimeMusic, and ctx.isPlaying is
//     ignored (the probe gives identical numbers stopped and playing); an invalid tempo falls back to the last one.
//   - Every Euclidean hit plays the same material (always the newest slice); nothing selects a different part of the
//     captured audio per step.
//   - The existing tests prove nothing about the effect: "PatternFreezeMode outputs audio when freeze enabled" only
//     checks RMS > 0, which a delayed input satisfies.
//   - Wet level: about -10 dB re input at defaults (1/sqrt(n) gain compensation, 10 ms attack, 50 % mix).
//
// Three stages, the user rules between them:
//   {stage: "design"}     capture the mode as it is (hidden signal-path probe in dsp_effects_tests + hidden Freeze
//                         panel screenshots) -> parallel research (code facts, prior art, a musician's critique,
//                         DSP/state/UI consequences) -> one design document -> challenge/revise -> open questions.
//                         Writes specs/iterum-freeze/ and the two hidden test cases only.
//   {stage: "reconcile"}  apply the user's rulings from specs/iterum-freeze/open-items.md to the design.
//   {stage: "build", notes}  preflight (never on main) -> failing tests -> DSP -> params/processor/controller/panel
//                         -> gate with a bounded fix loop -> code review + probe-based audio review (+ visual review
//                         if the panel changed) -> close-out. Nothing commits.
//
// Model per job (user, 2026-10-29: "the right model for the right job, don't assign Fable to every agent"):
//   haiku  - fixed procedures that only run commands and report: preflight, gates.
//   sonnet - bounded reading and writing against a checklist: code facts, prior art, consequences, the two hidden
//            cases, the first fix of a red gate, changelog, the close-out (haiku failed its output schema 5 times
//            on the tap-editor run).
//   opus   - independent judgment and C++ that must be right the first time: the critique, the challengers,
//            revisions, the test author, both implementers, the reviews.
//   fable  - exactly one call: the design synthesis.
//
// Resume replays agent() calls by a hash chained over every call in order: changing a prompt's text re-runs that
// call and everything after it. Add arguments that default to the old behaviour instead of editing a used prompt.
// Prompts that run before the implementers must not contain values that later stages change (the tap-editor run
// nearly replayed both implementers because a shot list grew).
export const meta = {
  name: 'iterum-freeze',
  description: 'Iterum Freeze mode: research, design and rebuild of the Freeze signal path (a real capture moment, forward playback for every pattern, bar-aligned triggering, audible defaults)',
  whenToUse: 'When fixing or redesigning the Iterum Freeze delay mode (PatternFreezeMode). Flow: (1) run args {stage: "design"}; (2) the user reviews specs/iterum-freeze/design.md and answers the open questions; (3) the main loop writes the rulings and every unresolved item to specs/iterum-freeze/open-items.md and runs {stage: "reconcile"}; (4) on a feature branch, run {stage: "build", notes: "..."}; (5) the user auditions Freeze. Nothing commits.',
  phases: [
    { title: 'Capture', detail: 'hidden signal-path probe + hidden Freeze panel screenshots, before-readings (sonnet)', model: 'sonnet' },
    { title: 'Research', detail: 'code facts, prior art, DSP/state/UI consequences (sonnet); a musician\'s critique (opus)', model: 'sonnet' },
    { title: 'Design', detail: 'one design document for the Freeze mode (fable, the only fable call)', model: 'fable' },
    { title: 'Challenge', detail: 'musical and implementability skeptics (opus), revision (opus)', model: 'opus' },
    { title: 'Reconcile', detail: 'apply the user rulings, resolve every open item (opus), one implementability check (opus)', model: 'opus' },
    { title: 'Preflight', detail: 'branch, artifacts and clean tree (haiku)', model: 'haiku' },
    { title: 'Tests', detail: 'failing Catch2 cases for every rule of the design (opus)', model: 'opus' },
    { title: 'Implement', detail: 'DSP first, then params/processor/controller/panel (opus)', model: 'opus' },
    { title: 'Verify', detail: 'build, sharded tests, pluginval, probe + shots (haiku), bounded fix loop (sonnet, then opus)', model: 'haiku' },
    { title: 'Review', detail: 'DSP/VST code review + probe-based audio review + panel visual review (opus), one fix round (sonnet)', model: 'opus' },
    { title: 'Close', detail: 'clang-tidy, lints, symbols, cleanup (sonnet), changelog (sonnet)', model: 'sonnet' },
  ],
}

// ---------------------------------------------------------------------------
// Args
// ---------------------------------------------------------------------------
let A = args
if (typeof A === 'string') {
  try { A = JSON.parse(A) } catch (e) { throw new Error(`args arrived as an unparseable string: ${A}`) }
}
A = A || {}
const stage = A.stage || 'design'
if (!['design', 'reconcile', 'build'].includes(stage)) throw new Error(`args.stage must be "design", "reconcile" or "build", got ${stage}`)
const NOTES = A.notes ? `\nUSER RULINGS (recorded in specs/iterum-freeze/design.md; the document wins on any conflict):\n${A.notes}\n` : ''

// ---------------------------------------------------------------------------
// Fixed facts
// ---------------------------------------------------------------------------
const DIR = 'specs/iterum-freeze'
const DESIGN = `${DIR}/design.md`
const OPEN = `${DIR}/open-items.md`
const SHOTS_BEFORE = `${DIR}/shots/before`
const SHOTS_AFTER = `${DIR}/shots/after`
const PROBE_BEFORE = `${DIR}/probe/before.txt`
const PROBE_AFTER = `${DIR}/probe/after.txt`
const TMP = 'F:/tmp/iterum-freeze'
const UIDESC = 'plugins/iterum/resources/editor.uidesc'
const CMAKE = '"C:/Program Files/CMake/bin/cmake.exe"'
const PROBE_CASE = 'PROBE Freeze signal path at defaults'
const PROBE_FILE = 'dsp/tests/unit/effects/test_pattern_freeze_mode.cpp'
const SHOT_CASE = 'Iterum_FreezeScreenshots'
const SHOT_FILE = 'plugins/iterum/tests/unit/vst/freeze_screenshot_test.cpp'
const SHOT_ENV = 'ITERUM_FREEZE_SHOTS_DIR'
const PERF_FILTER = "'~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]'"
// The exact probe source the main loop measured with (a Catch2 case body; the capture agent appends it as the
// hidden case). Kept in the repo under specs/iterum-freeze/probe/ by the capture agent.
const PROBE_SRC = 'C:/Users/scab/AppData/Local/Temp/claude/f--projects-iterum/9215cbe4-8bb5-4081-9b59-a780e643522c/scratchpad/freeze-path-probe.cpp'

const CONTEXT = `
PROJECT: Iterum, a ten-mode VST3 delay (VSTGUI, Steinberg SDK, C++20, cross-platform: VSTGUI only, no native UI).
This workflow is about ONE mode: Freeze (DelayMode::Freeze = 9), the "Pattern Freeze" of spec 069.

WHERE THINGS ARE
- DSP (Layer 4): dsp/include/krate/dsp/effects/pattern_freeze_mode.h, class PatternFreezeMode: process(left, right,
  n, ctx) writes every input sample into a RollingCaptureBuffer (dsp/include/krate/dsp/primitives/
  rolling_capture_buffer.h: writeStereo, isReady(ms), getAvailableSamples, extractSlice(outL, outR, length,
  offsetSamples) where offset = samples BEFORE the newest sample and the slice is read forward from offset+length
  back), then, when freezeMixSmoother_ > 0 and the buffer is ready (and the tempo valid for Euclidean / NoiseBursts),
  runs PatternScheduler (dsp/include/krate/dsp/processors/pattern_scheduler.h: Euclidean steps of one note value at
  ctx.tempoBPM, trigger callback -> onPatternTrigger, which cuts a slice of sliceLengthMs_ from offset 0, or a random
  offset for GranularScatter, into a SlicePool slice and plays it through calculateEnvelope) and processActiveSlices
  (processEuclideanSlices, processGranularScatter with its own Poisson trigger, processHarmonicDrones, processNoiseBursts),
  a ~500 ms crossfade on pattern-type change (processCrossfade), and the dry/wet mix (dryWetSmoother_). Types and
  defaults: dsp/include/krate/dsp/core/pattern_freeze_types.h (PatternType, SliceMode, PitchInterval, NoiseColor,
  EnvelopeShape, PatternFreezeConstants: kDefaultSliceLengthMs 200, kDefaultEuclideanSteps 8 / Hits 3,
  kDefaultCaptureBufferSeconds 5, kMinReadyBufferMs, envelope 10 / 100 ms). EuclideanPattern in core/.
  The older FreezeMode (effects/freeze_mode.h, an infinite-sustain feedback loop) is a different class; check whether
  anything in Iterum still uses it before touching it.
- Plugin: plugins/iterum/src/parameters/freeze_params.h (FreezeParams atomics, registerFreezeParams, handle/format,
  save/load: the Freeze block sits inside the per-mode sequence of Processor::getState / setState, processor.cpp
  ~351: the stream has NO version field; new fields go at the very END, after saveMultiTapLengthParams, as the Tape
  sync and MultiTap Length tails did, with a controller-side sync for setComponentState and
  loadComponentStateWithNotify). IDs in plugins/iterum/src/plugin_ids.h, block 1000-1099: used 1012 (Mix), 1015-1017,
  1020-1023, 1030-1033, 1040-1043, 1050-1054, 1060-1062; 1070-1099 are free. processor.cpp "case DelayMode::Freeze"
  pushes every parameter each block and calls patternFreezeMode_.process; the BlockContext carries tempoBPM,
  isPlaying, transportPositionSamples, projectTimeMusic (+ projectTimeMusicValid) from the host.
- Panel: specs/iterum-ui-redesign/design-system.md (7.10 Freeze: core select cell, SLICE + ENVELOPE in column 1,
  four stacked variants EUCLIDEAN / GRANULAR SCATTER / HARMONIC DRONES / NOISE BURSTS at columns 2-3 under the
  Pattern selector, one visible, anchors 1020 / 1030 / 1040 / 1050 as direct children; 8.2) and
  specs/iterum-ui-redesign/panels/freeze.md; built by specs/iterum-ui-redesign/xslt/freeze.xsl (+ cells.xsl). uidesc
  edits of more than ~3 controls are done with XSLT (npx xslt3 -xsl:F.xsl -s:IN -o:OUT), never by regex. The
  controller shows the variant by kFreezePatternTypeId through ContainerVisibilityController (controller.cpp ~925).
  Any new parameter needs a control (every parameter keeps one) placed per the design system.
- Tests: dsp/tests/unit/effects/test_pattern_freeze_mode.cpp (23 cases, untagged, dsp_effects_tests; helpers
  pfCreateContext, pfGenerateSine, patternFreezeCalculateRMS), plugins/iterum/tests/unit/parameters/
  freeze_params_test.cpp, editor_panels_test.cpp (Freeze variant visibility), mode_crossfade_tests.cpp. plugin_tests
  compiles processor.cpp (since the tap-editor build) so processor-level cases are possible: heap-allocate the
  Processor (owned(new Processor)), initialize + setupProcessing + setActive(true), drive process() with
  HostProcessData + ProcessContext (see plugins/iterum/tests/unit/processor/multitap_tempo_output_test.cpp).
  approval_tests holds golden renders: a Freeze DSP change may legitimately change a Freeze golden; the design must
  say which, and a fixer may re-approve only those.
- Tests run sharded: node tools/run-gate.js --filter ${PERF_FILTER} --out DIR <exe> ... (last line GATE GREEN|RED;
  per-shard logs under --out). A single case by name is the only direct exe run. Timing cases are excluded on
  purpose. node tools/header-consumers.js --tag '[iterum]' <modified files> names the executables a shared header
  reaches (pattern_freeze_mode.h reaches dsp_effects_tests and plugin_tests / approval_tests).
- Build: ${CMAKE} --build build/windows-x64-release --config Release --target <targets> --parallel -- /nr:false
  (a post-build copy permission error into C:/Program Files/Common Files/VST3 is harmless; a warning is not).
- Offline audio: tools/krate-render feeds NO input to an effect (numInputs = 0), so it cannot render Freeze. The
  measurement instrument is the hidden probe case "${PROBE_CASE}" in ${PROBE_FILE} (dsp_effects_tests), which
  prints, per pattern type and transport state, the wet level re input, the active fraction and the best lag /
  correlation of the wet signal against the input. Extend it rather than inventing a second instrument.

FROZEN (user rulings 2026-10-28, still in force): parameter IDs, types and ranges of EXISTING parameters; every
parameter keeps a control; saved state stays loadable (append new fields at the end of the stream; say exactly what an
old preset loads as). Cross-platform VSTGUI only. Real-time rules: no allocation, lock, exception or I/O in process().
Release state: Iterum 1.0.0 is the open changelog entry (plugins/iterum/CHANGELOG.md top section); add bullets to it,
never bump the version.

WRITING FILES: write every file with the Write tool; heredocs are blocked by a hook. A command that exits non-zero has
failed: read its output. Before you return, confirm with ls that every file you claim to have written exists and is
non-empty; a file you could not write is a failure to report, never a success.
`

const EXEC_RULES = `EXECUTION RULES:
- Run every command as a FOREGROUND Bash command with timeout 600000, output redirected to a log file under ${TMP}/ (mkdir -p it first; a cleanup may have removed it), then tail the log.
- NEVER use run_in_background or the Monitor tool; never sleep, poll or "wait". A foreground command returns when it is done.
- DISK GUARD: never run find /c, find /, du or any drive-wide walk; only tail logs, never cat a whole log.
- The moment you have the result, call StructuredOutput with EVERY field of the schema (use an empty array or "none" rather than leaving a field out).`

const SHOT_STATES = [
  { file: 'freeze-euclidean.png', what: 'Mode = Freeze, Pattern Type = Euclidean, every other parameter at its registered default' },
  { file: 'freeze-granular.png', what: 'Pattern Type = Granular Scatter' },
  { file: 'freeze-drones.png', what: 'Pattern Type = Harmonic Drones' },
  { file: 'freeze-noise.png', what: 'Pattern Type = Noise Bursts' },
]
const SHOT_LIST = SHOT_STATES.map(s => `  ${s.file}: ${s.what}`).join('\n')

// ---------------------------------------------------------------------------
// Schemas
// ---------------------------------------------------------------------------
const IMPL_SCHEMA = {
  type: 'object',
  properties: {
    files: { type: 'array', items: { type: 'string' } },
    summary: { type: 'string' },
    problems: { type: 'array', items: { type: 'string' }, description: 'anything left undone or uncertain; empty when done' },
  },
  required: ['files', 'summary', 'problems'],
}
const CAPTURE_SCHEMA = {
  type: 'object',
  properties: {
    ok: { type: 'boolean' },
    shots: { type: 'array', items: { type: 'string' } },
    probeTable: { type: 'string', description: 'the probe output lines, verbatim' },
    notes: { type: 'string' },
  },
  required: ['ok', 'shots', 'probeTable', 'notes'],
}
const RESEARCH_SCHEMA = {
  type: 'object',
  properties: {
    file: { type: 'string' },
    summary: { type: 'string' },
    findings: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          severity: { type: 'string', enum: ['high', 'medium', 'low'] },
          kind: { type: 'string', description: 'bug | musical | usability | dsp | state | ui | prior-art' },
          detail: { type: 'string' },
          evidence: { type: 'string', description: 'file:line, probe line, screenshot, or source URL' },
        },
        required: ['severity', 'kind', 'detail', 'evidence'],
      },
    },
  },
  required: ['file', 'summary', 'findings'],
}
const DESIGN_SCHEMA = {
  type: 'object',
  properties: {
    file: { type: 'string' },
    summary: { type: 'string' },
    captureModel: { type: 'string', description: 'the recommended capture / trigger model in one sentence' },
    codeChanges: { type: 'array', items: { type: 'string' }, description: 'each DSP / plugin / uidesc / state change with the file it touches' },
    openQuestions: { type: 'array', items: { type: 'string' }, description: 'decisions that are the user\'s to make' },
  },
  required: ['file', 'summary', 'captureModel', 'codeChanges', 'openQuestions'],
}
const ISSUES_SCHEMA = {
  type: 'object',
  properties: {
    verdict: { type: 'string', enum: ['pass', 'revise'] },
    issues: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          severity: { type: 'string', enum: ['blocking', 'major', 'minor'] },
          where: { type: 'string', description: 'section of the document, file:line, probe line or screenshot' },
          problem: { type: 'string' },
          fix: { type: 'string' },
        },
        required: ['severity', 'where', 'problem', 'fix'],
      },
    },
  },
  required: ['verdict', 'issues'],
}
const RECONCILE_SCHEMA = {
  type: 'object',
  properties: {
    summary: { type: 'string' },
    resolved: { type: 'integer' },
    unresolved: { type: 'array', items: { type: 'string' }, description: 'items left open, each with the reason; empty when done' },
    files: { type: 'array', items: { type: 'string' } },
  },
  required: ['summary', 'resolved', 'unresolved', 'files'],
}
const PREFLIGHT_SCHEMA = {
  type: 'object',
  properties: {
    ok: { type: 'boolean' },
    branch: { type: 'string' },
    problems: { type: 'array', items: { type: 'string' } },
  },
  required: ['ok', 'branch', 'problems'],
}
const TESTS_SCHEMA = {
  type: 'object',
  properties: {
    files: { type: 'array', items: { type: 'string' } },
    cases: { type: 'array', items: { type: 'string' }, description: 'every new case name, each with "RED" (fails before the change) or "GREEN-ALREADY" (and why that is acceptable)' },
    rulesWithoutTest: { type: 'array', items: { type: 'string' }, description: 'rules of the design that no test can hold, with the reason; the reviews cover them' },
    problems: { type: 'array', items: { type: 'string' } },
  },
  required: ['files', 'cases', 'rulesWithoutTest', 'problems'],
}
const GATE_SCHEMA = {
  type: 'object',
  properties: {
    green: { type: 'boolean' },
    build: { type: 'string', description: 'compile result: errors and warnings, or "clean"' },
    tests: { type: 'string', description: 'run-gate last line, verbatim' },
    pluginval: { type: 'string' },
    probe: { type: 'string', description: 'the probe output lines, verbatim' },
    shots: { type: 'string' },
    failures: { type: 'array', items: { type: 'string' }, description: 'each failure with the log path or assertion text' },
  },
  required: ['green', 'build', 'tests', 'pluginval', 'probe', 'shots', 'failures'],
}
const LINT_SCHEMA = {
  type: 'object',
  properties: {
    green: { type: 'boolean' },
    clangTidy: { type: 'string' },
    lints: { type: 'string' },
    symbols: { type: 'string' },
    cleanup: { type: 'string' },
    problems: { type: 'array', items: { type: 'string' } },
  },
  required: ['green', 'clangTidy', 'lints', 'symbols', 'cleanup', 'problems'],
}

const blockingOrMajor = (r) => (r && r.issues ? r.issues.filter(i => i.severity === 'blocking' || i.severity === 'major') : [])
const fmtIssues = (list) => list.map((i, n) => `${n + 1}. [${i.severity}] ${i.where || ''}: ${i.problem}\n   fix: ${i.fix}`).join('\n')

const gatePrompt = (attempt) => `${CONTEXT}
TASK: verification gate, attempt ${attempt}. Run, in order, and report each result verbatim:
0. node tools/header-consumers.js --tag '[iterum]' 2>&1 | tail -3
   Its last line names the test executables outside Iterum that include a MODIFIED shared header (design.md 7.3);
   "none" adds nothing. EXES = dsp_effects_tests plugin_tests approval_tests plus every executable it names.${attempt >= 2 ? `
   Where it writes "<exe> (or dsp_ieee_tests if listed there)", add dsp_ieee_tests to EXES as well: some consumer
   TUs (e.g. dsp/tests/unit/systems/profundum_engine_test.cpp) are built into dsp_ieee_tests, not into the layer exe.` : ''}
1. Build: ${CMAKE} --build build/windows-x64-release --config Release --target <EXES> Iterum --parallel -- /nr:false
   A compile error or ANY warning is a red.
2. node tools/run-gate.js --filter ${PERF_FILTER} --out ${TMP}/gate-a${attempt} <EXES> 2>&1 | tail -40
   (last line GATE GREEN / GATE RED; read a failing shard's log under --out, never re-run to re-read). Never remove
   the filter.
3. tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Iterum.vst3"
4. Probe: build/windows-x64-release/bin/Release/dsp_effects_tests.exe "PROBE Freeze signal path at defaults" > ${TMP}/probe-a${attempt}.txt 2>&1
   then copy that file to ${PROBE_AFTER} and put its table lines in "probe" verbatim.
5. Screenshots: mkdir -p ${SHOTS_AFTER}; ${SHOT_ENV}=${SHOTS_AFTER} build/windows-x64-release/bin/Release/plugin_tests.exe "Iterum_FreezeScreenshots"
   (the one hidden case by name; at least 4 PNGs expected, more if the design added states; list the files).
6. If the Profundum evidence files changed (git status --short -- specs/profundum-*), restore them with
   git checkout -- <those paths>; a test rewrote them, it is not a code change.
green = build clean AND GATE GREEN AND pluginval passed AND the probe case passed AND the shot case passed. Change nothing else.
${EXEC_RULES}`

// ===========================================================================
// STAGE: design
// ===========================================================================
if (stage === 'design') {
  phase('Capture')
  const capture = await agent(`${CONTEXT}
TASK: capture the Freeze mode as it is, in three parts.
A. The signal-path probe. Append to ${PROBE_FILE} (it is CRLF: write the whole file back with CRLF line endings, or
   append with a small node script that converts the snippet) a hidden Catch2 case named exactly "${PROBE_CASE}" with
   tags "[.][probe][freeze-path]". Its body is in ${PROBE_SRC} (read it; it needs #include <cstdio>): for each of the
   four pattern types and both transport states it runs 6 s of a tone that doubles in frequency every second through
   PatternFreezeMode at the plugin defaults (Euclidean 8/3, 200 ms slices, mix 50 %, 120 BPM) and prints wet level re
   input, peak, the active fraction of 10 ms windows and the best lag / correlation against the input. Also copy the
   source to ${DIR}/probe/probe-case.cpp.txt for the record (the .txt keeps the portability hook from compiling a fragment). Build dsp_effects_tests (zero warnings) and run the case by
   name: build/windows-x64-release/bin/Release/dsp_effects_tests.exe "PROBE Freeze signal path at defaults" > ${PROBE_BEFORE} 2>&1 (mkdir -p
   ${DIR}/probe first). The eight table lines go into probeTable verbatim.
B. The panel. Add ONE hidden case "${SHOT_CASE}" (tags "[.][screenshots][freeze]") in a new file ${SHOT_FILE},
   modelled on plugins/iterum/tests/unit/vst/tap_editor_screenshot_test.cpp (same headless setup, same env-var
   pattern with ${SHOT_ENV}, default <temp>/iterum-freeze-shots): switch Mode to Freeze and write these PNGs:
${SHOT_LIST}
   Pattern Type is index i of 4 -> normalized i/3. Register the file in plugins/iterum/tests/CMakeLists.txt next to
   tap_editor_screenshot_test.cpp. Build plugin_tests (zero warnings) and render: mkdir -p ${SHOTS_BEFORE};
   ${SHOT_ENV}=${SHOTS_BEFORE} build/windows-x64-release/bin/Release/plugin_tests.exe "Iterum_FreezeScreenshots".
   Look at every PNG with the Read tool and confirm the right variant fieldset is visible in each.
C. Report. ok = the probe ran and printed eight lines, and four PNGs exist showing four different variants.
Edit nothing but the two test files and the CMake list.
${EXEC_RULES}`, { label: 'capture', phase: 'Capture', model: 'sonnet', schema: CAPTURE_SCHEMA })
  if (!capture || !capture.ok) throw new Error(`capture failed: ${capture ? capture.notes : 'agent died'}`)

  phase('Research')
  const RESEARCH = [
    {
      key: 'code', model: 'sonnet', file: `${DIR}/research/code-facts.md`,
      brief: `CODE FACTS. Read pattern_freeze_mode.h end to end, pattern_scheduler.h, rolling_capture_buffer.h, slice_pool.h,
euclidean_pattern.h, pattern_freeze_types.h, freeze_params.h, the processor's Freeze branch and BlockContext, and the
archived spec specs/_archive_/069-pattern-freeze/spec.md (its user stories, FRs, clarifications: what was promised).
Write what the mode does today, precisely, with file:line: where each pattern type reads its audio from and in which
direction (confirm or refute the reversed reads in Drones and Noise Bursts: trace extractSlice's offset semantics
against the increasing offsets), what "freeze" holds (nothing?), when slices trigger (scheduler phase vs the host
position, isPlaying, tempo fallback), the envelope and gain compensation (why -10 dB), the crossfade on pattern
change, Slice Mode Variable (is it implemented?), every parameter the panel shows and whether it has an audible
effect at defaults, the state block and where a new field could go, and the probe's before table (${PROBE_BEFORE})
explained line by line from the code. Confirm or refute each seed fact in the header comment of
.claude/workflows/iterum-freeze.js and say which. Every finding carries its evidence.`,
    },
    {
      key: 'prior-art', model: 'sonnet', file: `${DIR}/research/prior-art.md`,
      brief: `PRIOR ART. How do shipping products do "freeze" and rhythmic re-sampling? Study at least: Ableton Beat Repeat
(interval, offset, grid, gate/mix/insert, decay), Ableton Echo / Looper freeze, iZotope Stutter Edit 2, Native
Instruments Replika XT's Freeze and Molekular, Valhalla Freeze / Supermassive, Eventide Blackhole freeze, Output
Portal, Glitchmachines Fracture / Polygon, Sugar Bytes Effectrix / Looperator, Cableguys ShaperBox Time, Soundtoys
Little AlterBoy / Crystallizer freeze, Strymon Timeline / BigSky freeze and hold footswitches, and any granular
freeze (Mutable Clouds / Beads, Arturia Efx FRAGMENTS). Use WebSearch/WebFetch (load them with ToolSearch first).
For each: what triggers the capture (button, momentary, auto per bar, MIDI, input gate), what is held (a loop of N
beats, a grain window, a spectral frame), how playback relates to the host grid (bar-aligned, free), what happens
when the transport stops, how dry and wet relate (mix, insert, gate, ducking), defaults out of the box (is the
effect audible with no interaction?), and the parameters that users reach for first. End with a table of the
conventions that recur (what users expect of "freeze" and of a beat-repeat) and the product-specific extras. Cite URLs.`,
    },
    {
      key: 'critique', model: 'opus', file: `${DIR}/research/critique.md`,
      brief: `A MUSICIAN'S CRITIQUE. You are a producer who selected Freeze expecting the delay to freeze or stutter the
audio. You cannot listen; you have the probe table (${PROBE_BEFORE}: wet level, active fraction, lag / correlation
per pattern type), the four panel shots (${SHOTS_BEFORE}/*.png, the Read tool shows images), the archived spec's
promises, and the code for what a number cannot show. Write what gets in the way, ranked by how much it hurts: what
you hear at defaults and why it reads as "only the input"; what each pattern type actually does versus what its name
and controls promise; what you cannot do (hold a moment, retrigger, lock to the bar, hear it with the transport
stopped); which of the 24 controls change nothing audible; what the panel implies that the DSP does not deliver; and
what is fine. Form your own list first; the seed facts in the header of .claude/workflows/iterum-freeze.js are there
to check against afterwards, not to copy. Only real problems: say when something works.`,
    },
    {
      key: 'consequences', model: 'sonnet', file: `${DIR}/research/consequences.md`,
      brief: `DSP, STATE AND UI CONSEQUENCES. The design will choose a capture model. Work out, against the code, what each
candidate costs: (A) a Freeze trigger parameter (latched toggle; momentary; or both) that snapshots the rolling
buffer into a held loop; (B) automatic re-capture every N bars / beats phased to ctx.projectTimeMusic (works with no
interaction); (C) an input gate (capture when the input exceeds a threshold, hold while quiet); (D) combinations
(an Auto / Latch / Momentary mode parameter plus a Capture Length). For each: the DSP change (snapshot vs holding
the write pointer; memory: the buffer is 5 s stereo already; slice selection per step; forward playback for Drones
and Noise Bursts and a real pitch shift for Drones, e.g. what Shimmer uses in dsp/include/krate/dsp/processors/
pitch_shift_processor.h, or crossfaded loop reads), the scheduler change (phase to the bar from projectTimeMusic,
behaviour when isPlaying is false or projectTimeMusicValid is false, tempo fallback), the plugin change (new IDs
from 1070, controls on the panel: where in panels/freeze.md's geometry, the XSLT), the state change (append-only at
the end of the stream after the MultiTap Length tail, controller-side sync in setComponentState and
loadComponentStateWithNotify, what an old preset loads as, factory presets in plugins/iterum/resources or
tools/preset_generator.cpp that set Freeze), the approval goldens that cover Freeze (grep plugins/iterum/tests/approval
and its reference files), and the test harness (can a dsp_effects_tests case inject projectTimeMusic / isPlaying?
yes, BlockContext has them; can a processor case drive ProcessContext? yes, see multitap_tempo_output_test.cpp).
Also the mix question: 50 % mix vs 100 % wet vs a gate / insert mode, in terms of the existing Mix parameter's
meaning for saved presets. Give each option a cost in files touched and a risk list; recommend nothing, the designer
decides.`,
    },
  ]
  const research = (await parallel(RESEARCH.map(r => () => agent(`${CONTEXT}
TASK: ${r.brief}
Write the report to ${r.file} (markdown; findings by severity, each with its evidence). Do not edit any other file.`,
    { label: `research:${r.key}`, phase: 'Research', model: r.model, schema: RESEARCH_SCHEMA })))).filter(Boolean)
  if (research.length < RESEARCH.length) log(`research: ${RESEARCH.length - research.length} of ${RESEARCH.length} researchers returned nothing; the design proceeds without them`)
  const researchDigest = research.map(r => `- ${r.file}: ${r.findings.filter(f => f.severity === 'high').length} high / ${r.findings.length} findings. ${r.summary}`).join('\n')

  phase('Design')
  const designPrompt = `${CONTEXT}
TASK: design the Freeze mode as it should be, and write it to ${DESIGN}.
Inputs: the four research reports (${DIR}/research/*.md, digest below), the probe table (${PROBE_BEFORE}), the panel
shots (${SHOTS_BEFORE}/), the archived spec, the DSP, the plugin code and the panel documents named above.
${researchDigest}
Probe before:
${capture.probeTable}

The user's complaint is the floor: a fresh Freeze instance at its defaults must produce an effect a producer hears at
once and recognises as a freeze or a stutter, with the transport running and ideally stopped too. Beyond that, adopt
what the critique and the prior art show users expect of "freeze", and nothing speculative: every addition must remove
a concrete problem named in the research.

The document must let two implementers (DSP, then plugin) build it without asking anything. Contents:
1. Problems addressed: each one, with its research evidence, and the ones deliberately left alone (and why).
2. Capture model: what is held, when it is captured (trigger modes, automatic re-capture, phase to the bar, input
   gate), what happens on transport stop / start / tempo loss, how the four pattern types read the held audio (all
   forward; which slice per step; Drones' pitch method; Noise Bursts' slice + noise), the crossfade on re-capture and
   on pattern change, the exact level rule (wet re input at defaults), and the Mix semantics.
3. Parameters: every new parameter (name, ID from 1070, type, range, default, string list, what it does, where its
   control sits in panels/freeze.md's geometry and the XSLT needed), every existing parameter whose meaning changes
   (and why that is allowed: IDs, types and ranges stay), and the registered defaults of a fresh instance.
4. Scheduler and transport: the phase rule against projectTimeMusic, isPlaying and projectTimeMusicValid, the fallback.
5. State and compatibility: every new field, appended at the end of the stream after the MultiTap Length tail; what
   every saved Freeze preset from before loads as; the factory presets; which approval goldens change and why.
6. Expected measurements: the probe table the build must produce (wet level, active fraction, correlation / lag
   semantics for a HELD loop: the wet must NOT track the live input once captured; say the numbers), plus any new
   probe lines (held-content test: change the input pitch after capture and show the wet keeps the old pitch).
7. Implementation plan: ordered rows, each with the file, the function, and the Catch2 case that proves it (DSP cases
   in test_pattern_freeze_mode.cpp; parameter / state cases in freeze_params_test.cpp; processor cases with
   ProcessContext; the panel case in editor_panels_test.cpp; the hidden ${SHOT_CASE} and "${PROBE_CASE}" for the
   reviews). Real-time rules for the DSP; the controller-side state sync; the XSLT.
8. Open questions: decisions only the user can make (musical semantics, defaults, compatibility trade-offs). Decide
   everything else and say why. Keep them few and make each a concrete A/B.
Write the whole document; no placeholders.`
  let design = await agent(designPrompt, { label: 'design', phase: 'Design', model: 'fable', effort: 'high', schema: DESIGN_SCHEMA })
  if (!design) throw new Error('design agent died')

  phase('Challenge')
  const LENSES = [
    { key: 'musical', brief: 'MUSICAL AND USABILITY: as a producer, does a fresh Freeze instance now do something you hear and recognise at once? Does "freeze" hold what you expect, retrigger where you expect, lock to the bar, and survive the transport stopping? Does each pattern type sound like its name? Is anything worse than today? Are the new controls where the design system puts them, named canonically, and few enough?' },
    { key: 'implementability', brief: 'IMPLEMENTABILITY: can this be built as written? Check the capture model against rolling_capture_buffer.h and slice_pool.h (memory, real-time safety, offset semantics), the scheduler phase against BlockContext (projectTimeMusic validity, block boundaries), the pitch method for Drones against the real pitch shifter API or a crossfaded loop, the state plan against processor.cpp getState / setState and the controller sync precedents (tape_params.h syncTapeSyncParamsToController, multitap_params.h syncMultiTapLengthParamsToController), the approval goldens, the panel geometry against panels/freeze.md and the XSLT, and that every rule in section 7 has a test that can really hold it (a case that only checks RMS > 0 holds nothing).' },
  ]
  let challenge = []
  for (let round = 1; round <= 2; ++round) {
    const reviews = (await parallel(LENSES.map(l => () => agent(`${CONTEXT}
TASK: challenge ${DESIGN} through one lens. Default to skepticism: find what is wrong, missing, inconsistent or
unbuildable, and verify each claim against the code and the research (${DIR}/research/*.md) rather than trusting the
document. ${l.brief}
blocking = cannot be built as written, loses saved presets, or breaks a frozen rule; major = a worse or inconsistent
mode; minor = polish. verdict "pass" only when nothing is blocking or major. Do not edit the document.`,
      { label: `challenge:${l.key}:r${round}`, phase: 'Challenge', model: 'opus', schema: ISSUES_SCHEMA })))).filter(Boolean)
    challenge = reviews.flatMap(r => blockingOrMajor(r))
    log(`challenge round ${round}: ${challenge.length} blocking/major issues`)
    if (!challenge.length) break
    if (round === 2) break
    design = await agent(`${CONTEXT}
TASK: revise ${DESIGN} to resolve these challenges. Fix the document itself, every section they touch; where a
challenge is wrong, keep the design and add one line under "Challenges considered" saying why.
${fmtIssues(challenge)}`, { label: `revise:r${round}`, phase: 'Challenge', model: 'opus', schema: DESIGN_SCHEMA }) || design
  }

  return {
    stage: 'design',
    dir: DIR,
    design: DESIGN,
    summary: design.summary,
    captureModel: design.captureModel,
    codeChanges: design.codeChanges,
    openQuestions: [
      ...design.openQuestions,
      ...challenge.map(i => `[challenge, unresolved after 2 revisions] ${i.where}: ${i.problem} (suggested: ${i.fix})`),
    ],
    research: research.map(r => ({ file: r.file, findings: r.findings.length, high: r.findings.filter(f => f.severity === 'high').length })),
    probeBefore: PROBE_BEFORE,
    shotsBefore: SHOTS_BEFORE,
    next: 'User reviews design.md and answers the open questions; the main loop writes rulings + unresolved items to open-items.md and runs {stage: "reconcile"}; then, on a feature branch, {stage: "build"}.',
  }
}

// ===========================================================================
// STAGE: reconcile
// ===========================================================================
if (stage === 'reconcile') {
  phase('Reconcile')
  const rec = await agent(`${CONTEXT}
TASK: reconcile the design before the build. ${OPEN} holds the user's rulings and every item the design stage left
unresolved. Resolve ALL of them in ${DESIGN}: section 8 becomes "Rulings", each ruling with what it changed; apply
each ruling to every section it touches (capture model, parameters, scheduler, state, measurements, implementation
rows and their tests). Read the real code for every claim you restate; the research reports are a start, the source
is the truth. In ${OPEN}, append "Resolved: <section(s)>" under every item (or "Open: <reason>" if it truly needs the
user). Report the files you changed.`, { label: 'reconcile', phase: 'Reconcile', model: 'opus', schema: RECONCILE_SCHEMA })
  if (!rec) throw new Error('reconcile agent died')
  const check = await agent(`${CONTEXT}
TASK: implementability check of the reconciled ${DESIGN}. Verify, against the code, that every implementation row in
section 7 names a real file and function, that every new parameter has an ID, a control placement and a state field
with its controller-side sync, that the state plan is append-only and says what an old preset loads as, that the
expected probe numbers in section 6 follow from the capture model, and that no ruling in ${OPEN} is contradicted by
the document. blocking = unbuildable or contradicts a ruling; major = a row an implementer could not follow without
guessing. Do not edit any file.`, { label: 'reconcile:check', phase: 'Reconcile', model: 'opus', schema: ISSUES_SCHEMA })
  const left = blockingOrMajor(check)
  return {
    stage: 'reconcile',
    summary: rec.summary,
    resolved: rec.resolved,
    unresolved: [
      ...rec.unresolved,
      ...left.map(i => `[check, unresolved] ${i.where}: ${i.problem} (suggested: ${i.fix})`),
    ],
    files: rec.files,
    next: 'If unresolved is empty, the user reviews the diff of design.md and runs {stage: "build"} on the feature branch.',
  }
}

// ===========================================================================
// STAGE: build
// ===========================================================================
phase('Preflight')
const pre = await agent(`${CONTEXT}
TASK: preflight for the build stage. Check, and report each failed check as a problem:
1. git branch --show-current is NOT main.
2. ${DESIGN} exists, has no "[TBD]" or "placeholder" text, and ${OPEN} (if it exists) has no line starting with "Open:".
3. ${PROBE_BEFORE} exists with eight table lines, ${SHOT_FILE} exists and ${SHOTS_BEFORE}/ holds 4 PNGs.
4. git status --short -- ${UIDESC} plugins/iterum/src dsp/include is empty (the build starts from the committed code).
5. npx xslt3 -? runs.
ok = every check passed. Change nothing.
${EXEC_RULES}`, { label: 'preflight', phase: 'Preflight', model: 'haiku', effort: 'low', schema: PREFLIGHT_SCHEMA })
if (!pre || !pre.ok) {
  return { stage: 'build', stopped: 'preflight', branch: pre ? pre.branch : null, problems: pre ? pre.problems : ['preflight agent died'] }
}

phase('Tests')
const tests = await agent(`${CONTEXT}${NOTES}
TASK: write the FAILING tests first, for every implementation row of ${DESIGN} section 7 that names a Catch2 case:
DSP cases in ${PROBE_FILE} beside the existing ones (held content does not track the live input after capture: change
the input pitch and show the wet keeps the old one; every pattern type plays forward: a rising chirp comes out
rising; step k plays slice k; the scheduler phases to projectTimeMusic; transport-stop behaviour; the level rule),
parameter / state cases in plugins/iterum/tests/unit/parameters/freeze_params_test.cpp (new fields round-trip at the
end of the stream, an old stream loads as the design says, controller sync), processor cases with ProcessContext
(modelled on multitap_tempo_output_test.cpp, heap-allocated Processor), and the panel case in editor_panels_test.cpp
for any new control. Follow the existing style of each file; nothing hidden except the two existing hidden cases.
A case that references functions that do not exist yet may be written against the signatures ${DESIGN} specifies;
then it fails to COMPILE, which counts as RED: say so per case. Build only the executables you touched when they can
compile, and run each new case BY NAME to confirm it is red (a case name containing a comma must be escaped as
"Name\\, more"). Do not implement anything. Rules no test can hold go to rulesWithoutTest with the reason.
${EXEC_RULES}`, { label: 'tests', phase: 'Tests', model: 'opus', schema: TESTS_SCHEMA })
if (!tests) throw new Error('test author died')

phase('Implement')
const dspImpl = await agent(`${CONTEXT}${NOTES}
TASK: implement the DSP side of ${DESIGN} (sections 2, 4, 6 and the matching rows of 7) in
dsp/include/krate/dsp/effects/pattern_freeze_mode.h and the primitives / processors it needs (capture snapshot or
hold, per-step slice selection, forward playback for every pattern, Drones' pitch method, scheduler phase to the
bar, transport and tempo rules, level rule, crossfades). Declare the public API the plugin side will call exactly as
section 7 specifies. The tests written in the previous step are the contract: make the DSP cases pass without
weakening any; the plugin cases may stay red for the next implementer. Real-time rules apply (no allocation in
process(): size everything in prepare()). Build dsp_effects_tests with zero warnings; run each case you turned green
BY NAME, then run the hidden probe "${PROBE_CASE}" and compare its table with section 6's expected numbers: a held
loop must stop tracking the live input. Do not run whole suites; the gate does. Report in summary which probe lines
match the design and which do not yet.
${EXEC_RULES}`, { label: 'impl:dsp', phase: 'Implement', model: 'opus', schema: IMPL_SCHEMA })
if (!dspImpl) throw new Error('dsp implementer died')

const pluginImpl = await agent(`${CONTEXT}${NOTES}
TASK: implement the PLUGIN side of ${DESIGN} (sections 3, 5 and the matching rows of 7): new parameters in
plugin_ids.h and freeze_params.h (registration, handling, formatting, save / load at the END of the stream after
saveMultiTapLengthParams, the controller-side sync wrappers and their call sites in setComponentState and
loadComponentStateWithNotify), the processor's Freeze branch (push the new values, pass the transport fields), the
controller (any visibility rule for the new controls), and the panel: write ${DIR}/xslt/freeze-controls.xsl (XSLT
3.0, identity transform + the changed views) and apply it (npx xslt3 -xsl:... -s:${UIDESC} -o:${TMP}/editor.uidesc,
check node tools/uidesc-inventory.js ${TMP}/editor.uidesc --check --baseline specs/iterum-ui-redesign/inventory-before.json
for no new defect in FreezePanel and no lost binding, then copy over ${UIDESC}); update panels/freeze.md to match.
The DSP implementer has already done the DSP side (its report below); build on it, do not undo it. The tests written
earlier are the contract: make every remaining red case pass without weakening any. Build plugin_tests,
approval_tests and Iterum with zero warnings; run each case you turned green BY NAME; render the hidden
${SHOT_CASE} to ${TMP}/shots and LOOK at the PNGs (Read tool): the new controls must sit where the design says. Do
not run whole suites; the gate does.
PREVIOUS IMPLEMENTER: ${dspImpl.summary}
Files: ${dspImpl.files.join(', ')}
${dspImpl.problems.length ? `Open problems it reported: ${dspImpl.problems.join('; ')}` : ''}
${EXEC_RULES}`, { label: 'impl:plugin', phase: 'Implement', model: 'opus', schema: IMPL_SCHEMA })
if (!pluginImpl) throw new Error('plugin implementer died')

// Rulings given after the implementers reported (args.rulings_fix names them, e.g. "R11-R14"). Each is recorded in
// ${DESIGN} section 8.1 and in the rows it names; this agent applies them before the first gate.
let rulingsImpl = null
if (A.rulings_fix) {
  rulingsImpl = await agent(`${CONTEXT}${NOTES}
TASK: apply the user's rulings ${A.rulings_fix} of ${DESIGN} section 8.1 (read each ruling row and every row it names in
sections 1, 2.5.4, 3.1, 6.2, 7.1 and 7.3; the document wins). The two implementers before you finished; build on their
work and do not undo it. Concretely:
- R11: in the D4 (1) case and the probe, "held, not live" is asserted by the held-note Goertzel check; r is printed,
  not asserted.
- R12: the drone pitch cases D8 (1) and (7) find each voice's peak by a Goertzel search within +/- 10 cents in 0.5-cent
  steps and assert it within 3 cents of its interval, its level within the row's dB bar.
- R13: the step bars of D8 (5), D8 (8) and D11 become "at most 2 x the largest step of the ideal output" as the rows
  define it. Then row D18: the click inside the SHARED PitchShiftProcessor (PitchSync, the read tap clamped when the
  detected grain shrinks after a tone change). First write the D18 case in the shifter's own test file and run it by
  name to see it RED; read the shifter code and trace the jump to confirm the cause before changing anything; fix it
  so the read stays continuous across a grain change and nothing changes while the grain is steady; run D18 by name
  GREEN. Shimmer uses this shifter: run node tools/header-consumers.js --tag '[iterum]' and, through run-gate.js with
  the repo's filter plus a tag or name filter, the shifter's and Shimmer's existing cases; none may change verdict.
- R14: kNoiseLevelDb becomes -10.5 dB; D9 (3) reads the constant; D9 (1) keeps its bar.
Never widen a bar the rulings do not name and never weaken another case. Build every test executable you touched and
Iterum with zero warnings. Run by name: D4 (1), D8 (1) (5) (7) (8), D11, D9 (1) (3), D18; then the hidden probe
"${PROBE_CASE}" and copy its output to ${PROBE_AFTER}. Do not run whole suites; the gate does.
DSP IMPLEMENTER: ${dspImpl.summary}
PLUGIN IMPLEMENTER: ${pluginImpl.summary}
${EXEC_RULES}`, { label: 'impl:rulings', phase: 'Implement', model: 'opus', schema: IMPL_SCHEMA })
  if (!rulingsImpl) throw new Error('rulings implementer died')
}

phase('Verify')
let gate = null
for (let attempt = 1; attempt <= 3; ++attempt) {
  gate = await agent(gatePrompt(attempt), { label: `gate:a${attempt}`, phase: 'Verify', model: attempt === 1 ? 'haiku' : 'sonnet', effort: 'low', agentType: 'build-test-runner', schema: GATE_SCHEMA })
  if (gate && gate.green) break
  if (attempt === 3) break
  const failures = gate ? gate.failures.join('\n') : 'the gate agent died; re-run the gate commands yourself to see the state'
  await agent(`${CONTEXT}${NOTES}
TASK: the verification gate is red (attempt ${attempt}). Find the root cause of each failure and fix it in the code,
following ${DESIGN}; never weaken a test, never change an existing parameter ID, never relax a golden. An
approval_tests red is either a real regression (fix the code) or a designed output change that ${DESIGN} section 5
names explicitly (then re-approve that one golden and say which rule justifies it). Read the logs the failures point
to before changing anything. Build after your fix; do not run the suites, the next gate does.
FAILURES:
${failures}
${EXEC_RULES}`, { label: `fix:a${attempt}`, phase: 'Verify', model: attempt === 1 ? 'sonnet' : 'opus', schema: IMPL_SCHEMA })
}
if (!gate || !gate.green) {
  return { stage: 'build', stopped: 'verify', gate, tests, dspImpl, pluginImpl }
}

phase('Review')
const audioPrompt = (round) => `${CONTEXT}
TASK: audio review, round ${round}, by measurement. Compare ${PROBE_BEFORE} with ${PROBE_AFTER} against ${DESIGN}
section 6: at defaults the wet must be a HELD loop (its correlation with the live input must fall once the input
changes pitch, where before it was 0.69 at a 175 ms lag), each pattern type forward and at the designed level, the
transport-stopped lines as designed. You may extend the probe (edit ${PROBE_FILE}, rebuild dsp_effects_tests, re-run
the case by name) to measure what section 6 asks for and the table does not yet show, e.g. a pitch-change test, but
change no production code. Report each gap as an issue. blocking = the default still reads as the input, or a pattern
plays backwards; major = a designed number missed by more than its tolerance; minor = polish. Edit nothing but the probe.`
const codeReviewPrompt = `${CONTEXT}
TASK: code review of the change set (git diff -- dsp/include dsp/tests plugins/iterum/src plugins/iterum/tests ${UIDESC})
against the repo's review criteria (.claude/skills/code-review/SKILL.md: real-time safety, thread safety, numerical
stability, layer dependencies, parameter handling, cross-platform, fast-math hygiene: no std::isnan, no infinity()
seeds). Focus: no allocation or lock in process() (the snapshot must be a pre-sized copy or a held write pointer),
the scheduler's phase arithmetic at block boundaries and on tempo change, the crossfades (no clicks on re-capture),
the state stream (append-only, old streams load, processor and controller readers agree byte for byte), the
controller sync call sites, and that no test was weakened. blocking = a real-time or state bug; major = a wrong
behaviour or a weakened test; minor = style. Edit nothing.`
const visualPrompt = (round) => `${CONTEXT}
TASK: panel review, round ${round}. Compare ${SHOTS_BEFORE}/FILE with ${SHOTS_AFTER}/FILE (Read tool shows images):
${SHOT_LIST}
plus any shot the build added (no before counterpart: judge it against the design). Check against ${DESIGN} section 3
and specs/iterum-ui-redesign/design-system.md: every new control present, in its designed cell, canonical label,
readout formatted per 6.4, nothing clipped or overlapping, the four variants still one-visible-at-a-time, the
unchanged parts pixel-identical to before. If the design changed no control, say "panel unchanged" with verdict
pass. blocking = a control missing or unusable; major = the design not met on screen; minor = polish. Edit nothing.`
let [audio, codeReview, visual] = await parallel([
  () => agent(audioPrompt(1), { label: 'review:audio:r1', phase: 'Review', model: 'opus', schema: ISSUES_SCHEMA }),
  () => agent(codeReviewPrompt, { label: 'review:code', phase: 'Review', model: 'opus', schema: ISSUES_SCHEMA }),
  () => agent(visualPrompt(1), { label: 'review:visual:r1', phase: 'Review', model: 'opus', schema: ISSUES_SCHEMA }),
])
let reviewLeft = [...blockingOrMajor(audio), ...blockingOrMajor(codeReview), ...blockingOrMajor(visual)]
if (reviewLeft.length) {
  await agent(`${CONTEXT}${NOTES}
TASK: fix these review findings in the code (XSLT for a uidesc change of more than ~3 controls, updating
${DIR}/xslt/freeze-controls.xsl so the stylesheet stays the record). Follow ${DESIGN}. Build dsp_effects_tests,
plugin_tests, approval_tests and Iterum with zero warnings; do not run the suites, the gate does.
FINDINGS:
${fmtIssues(reviewLeft)}
${EXEC_RULES}`, { label: 'review-fix', phase: 'Review', model: 'sonnet', schema: IMPL_SCHEMA })
  gate = await agent(gatePrompt(4), { label: 'gate:post-review', phase: 'Review', model: 'sonnet', effort: 'low', agentType: 'build-test-runner', schema: GATE_SCHEMA })
  const second = await parallel([
    () => agent(audioPrompt(2), { label: 'review:audio:r2', phase: 'Review', model: 'opus', schema: ISSUES_SCHEMA }),
    () => agent(visualPrompt(2), { label: 'review:visual:r2', phase: 'Review', model: 'opus', schema: ISSUES_SCHEMA }),
  ])
  audio = second[0]
  visual = second[1]
  reviewLeft = [...blockingOrMajor(audio), ...blockingOrMajor(visual)]
}

phase('Close')
const changelog = await agent(`${CONTEXT}
TASK: add the changelog bullets for this change to the OPEN entry of plugins/iterum/CHANGELOG.md (the top "## [1.0.0]"
section; never bump the version, never add a new section). Read ${DESIGN} section 1 (problems addressed) and the
diff (git diff --stat; git diff -- plugins/iterum/src/parameters/freeze_params.h dsp/include/krate/dsp/effects/pattern_freeze_mode.h)
and write what a user notices, in the style of the entry's existing bullets (bold lead phrase, then the behaviour,
then the compatibility note for saved presets): under "### Fixed" for "Freeze produced no audible effect at its
defaults" and the backwards playback, under "### Changed" or "### Added" for the capture model and any new control.
Check whether any other plugin includes pattern_freeze_mode.h (grep plugins/*/src); if one does, add one bullet to
its open changelog entry too, else touch nothing else.`, { label: 'changelog', phase: 'Close', model: 'sonnet', schema: IMPL_SCHEMA })

const lint = await agent(`${CONTEXT}
TASK: close-out checks. Run and report verbatim:
1. clang-tidy, the repo's script (never a header on its own: that reports every pre-existing line of the header):
   pwsh -NoProfile -File ./tools/run-clang-tidy.ps1 -Target iterum -BuildDir build/windows-ninja
   (its last lines read "Errors: N" / "Warnings: N"; both must be 0), plus, for every modified or new file under
   plugins/iterum/tests and dsp/tests, and for the test TU of each modified dsp/include header with
   "-header-filter=.*<headername>":
   "/c/Program Files/LLVM/bin/clang-tidy.exe" -p build/windows-ninja --quiet FILE 2>&1 | grep -v "bugprone-chained-comparison\\|cppcoreguidelines-avoid-do-while" | grep "warning:\\|error:"
   (the two filtered checks are accepted repo-wide; anything else printed in a CHANGED line is a red: compare against
   git diff -U0; pre-existing lines of a header are reported as a note, not a red). Copy every warning line into
   clangTidy BEFORE step 4 runs (the cleanup removes the log folder).
2. node tools/lint-plugin-editor-baseline.js; node tools/lint-serial-test-runs.js; node tools/lint-wallclock-tags.js
3. node tools/gen-symbols.js   (the commit hook checks specs/_architecture_/symbols.json; report "regenerated")
4. node tools/clean-test-output.js (dry run first; if its listing includes anything under ${DIR}/ do not apply and
   say so), then node tools/clean-test-output.js --apply; report its last 5 lines.
5. git status --short -- specs/profundum-*  must be empty; if not, git checkout -- those paths (test evidence rewrite).
green = no clang-tidy warning in a changed line AND the three lints pass. Change nothing else. Your StructuredOutput
must carry all six fields: green, clangTidy, lints, symbols, cleanup, problems (empty array when none).
${EXEC_RULES}`, { label: 'close', phase: 'Close', model: 'sonnet', schema: LINT_SCHEMA })

return {
  stage: 'build',
  branch: pre.branch,
  green: Boolean(gate && gate.green && lint && lint.green && !reviewLeft.length),
  gate,
  lint,
  tests: { cases: tests.cases, rulesWithoutTest: tests.rulesWithoutTest, problems: tests.problems },
  dspImpl: { files: dspImpl.files, summary: dspImpl.summary, problems: dspImpl.problems },
  pluginImpl: { files: pluginImpl.files, summary: pluginImpl.summary, problems: pluginImpl.problems },
  codeReviewMinor: codeReview ? codeReview.issues.filter(i => i.severity === 'minor') : [],
  reviewLeft,
  audioMinor: audio ? audio.issues.filter(i => i.severity === 'minor') : [],
  visualMinor: visual ? visual.issues.filter(i => i.severity === 'minor') : [],
  changelog: changelog ? changelog.summary : null,
  probeBefore: PROBE_BEFORE,
  probeAfter: PROBE_AFTER,
  shotsAfter: SHOTS_AFTER,
  next: 'User auditions Freeze in a host; commit only on the user\'s word.',
}
