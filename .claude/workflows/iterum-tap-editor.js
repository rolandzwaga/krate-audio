// Iterum MultiTap tap editor: research, design and rebuild of the custom tap pattern editor (TapPatternEditor).
//
// Why (user, 2026-10-29): raising the tap count renders the new taps on top of each other, so nothing visibly
// changes; and the background grid does not reflect the selected timing (1/4, 1/8 ...), so the user cannot tell
// where in time a tap is being placed. The user also asked for an independent critical look: usability and visual
// improvements beyond those two, only where there are real ones.
//
// Facts established by the main loop before this workflow was written (verify, never assume; they are the seeds):
//   - setActiveTapCount places a new tap at (n+1)/(count+1) and snaps it to the Snap grid (default 1/4 = 4 columns),
//     so the 5th and 6th taps both snap to 0.75 and the 7th and 8th both to 1.0: identical bars, invisible.
//   - The x axis is 0..5000 ms (processor: ratio x maxDelayMs, multi_tap_delay.h applyCustomTimingPattern). The Snap
//     table treats the full width as one whole note, so the "1/4" line is a quarter note at no tempo. The background
//     lines are fixed quarters whatever Snap says; only the ruler ticks follow it. Dotted divisions are rounded
//     (1/8 dotted = 5 columns, true value 5.33). The controller has no tempo; Iterum has no processor->controller
//     message at all (Innexus processor_messages.cpp and Disrumpo processor_messaging.cpp are the precedents).
//   - Three different "default" tap positions exist (add-tap spread, double-click calculateDefaultTapTime, Reset's
//     linear spread). Shrinking then growing the tap count overwrites the taps the user had placed. Taps at ratio 0
//     and 1 draw half-clipped at the view edges. No readout of time or level while dragging; the ruler has no labels.
//     The level axis is linear gain (most useful levels crowd the top third). Colors and fonts are hard-coded, not the
//     design system's tokens. When Timing is not Custom the whole lower half of the panel is empty.
//
// Three stages, the user rules between them:
//   {stage: "design"}     capture the editor as it is (a hidden screenshot case for the Custom pattern) -> parallel
//                         research (code facts, prior art, a musician's critique, DSP/state consequences) -> one
//                         design document -> challenge/revise -> open questions for the user. Writes
//                         specs/iterum-tap-editor/ and one hidden test case only.
//   {stage: "reconcile"}  apply the user's rulings from specs/iterum-tap-editor/open-items.md to the design.
//   {stage: "build", notes}  preflight (never on main) -> failing tests -> DSP/processor/controller -> editor view
//                         (+ panel XSLT if the design changes the panel) -> gate with a bounded fix loop -> visual
//                         review + code review -> lints, changelog, symbols, cleanup. Nothing commits.
//
// Model per job (user, 2026-10-29: "the right model for the right job, don't assign Fable to every agent"):
//   haiku  - fixed procedures that only run commands and report: capture render, preflight, gates, lints, cleanup.
//   sonnet - bounded reading and writing against a checklist: code-facts research, prior art, DSP/state consequences,
//            the hidden screenshot case, the first fix of a red gate, the panel XSLT, changelog bullets.
//   opus   - independent judgment and C++ that must be right the first time: the musician's critique, the
//            challengers, design revisions, the test author, both implementers, the visual and code reviews.
//   fable  - exactly one call: the design synthesis (the creative step the rest hangs on).
//
// Resume replays agent() calls by a hash chained over every call in order: changing a prompt's text re-runs that
// call and everything after it. Add arguments that default to the old behaviour instead of editing a used prompt.
export const meta = {
  name: 'iterum-tap-editor',
  description: 'Iterum MultiTap tap editor: research, design and rebuild of the custom tap pattern editor (time grid that reflects the timing, visible new taps, usability pass)',
  whenToUse: 'When improving the MultiTap custom tap pattern editor (TapPatternEditor). Flow: (1) run args {stage: "design"}; (2) the user reviews specs/iterum-tap-editor/design.md and answers the open questions; (3) the main loop writes the rulings and every unresolved item to specs/iterum-tap-editor/open-items.md and runs {stage: "reconcile"}; (4) on a feature branch, run {stage: "build", notes: "..."}; (5) the user auditions the editor. Nothing commits.',
  phases: [
    { title: 'Capture', detail: 'hidden screenshot case for the Custom pattern editor (sonnet), before-shots (haiku)', model: 'sonnet' },
    { title: 'Research', detail: 'code facts, prior art, DSP/state consequences (sonnet); a musician\'s critique (opus)', model: 'sonnet' },
    { title: 'Design', detail: 'one design document for the tap editor (fable, the only fable call)', model: 'fable' },
    { title: 'Challenge', detail: 'usability and implementability skeptics (opus), revision (opus)', model: 'opus' },
    { title: 'Reconcile', detail: 'apply the user rulings, resolve every open item (opus), one implementability check (opus)', model: 'opus' },
    { title: 'Preflight', detail: 'branch, artifacts and clean tree (haiku)', model: 'haiku' },
    { title: 'Tests', detail: 'failing Catch2 cases for every rule of the design (opus)', model: 'opus' },
    { title: 'Implement', detail: 'DSP/processor/controller, then the editor view and panel (opus; XSLT sonnet)', model: 'opus' },
    { title: 'Verify', detail: 'build, sharded tests, pluginval, after-shots (haiku), bounded fix loop (sonnet, then opus)', model: 'haiku' },
    { title: 'Review', detail: 'visual review of the shots + DSP/VST code review (opus), one fix round (sonnet)', model: 'opus' },
    { title: 'Close', detail: 'clang-tidy and editor lint (haiku), changelog (sonnet), symbols + cleanup (haiku)', model: 'haiku' },
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
const NOTES = A.notes ? `\nUSER RULINGS (recorded in specs/iterum-tap-editor/design.md; the document wins on any conflict):\n${A.notes}\n` : ''

// ---------------------------------------------------------------------------
// Fixed facts
// ---------------------------------------------------------------------------
const DIR = 'specs/iterum-tap-editor'
const DESIGN = `${DIR}/design.md`
const OPEN = `${DIR}/open-items.md`
const SHOTS_BEFORE = `${DIR}/shots/before`
const SHOTS_AFTER = `${DIR}/shots/after`
const TMP = 'F:/tmp/iterum-tap'
const UIDESC = 'plugins/iterum/resources/editor.uidesc'
const CMAKE = '"C:/Program Files/CMake/bin/cmake.exe"'
const SHOT_CASE = 'Iterum_TapEditorScreenshots'
const SHOT_FILE = 'plugins/iterum/tests/unit/vst/tap_editor_screenshot_test.cpp'
const SHOT_ENV = 'ITERUM_TAP_SHOTS_DIR'
const PERF_FILTER = "'~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]'"

const CONTEXT = `
PROJECT: Iterum, a ten-mode VST3 delay (VSTGUI, Steinberg SDK, C++20, cross-platform: VSTGUI only, no native UI).
This workflow is about ONE custom view: the MultiTap mode's tap pattern editor, shown when Timing Pattern = Custom.

WHERE THINGS ARE
- The view: plugins/iterum/src/ui/tap_pattern_editor.{h,cpp} (CControl subclass: drawing, mouse, cursor, drag,
  Escape, double-click) and tap_pattern_editor_logic.h (pure functions: coordinate mapping, hit test, snap table
  SnapDivision/getSnapDivisions/snapToGrid, calculateDefaultTapTime, calculatePatternRatios for Copy). Unit tests:
  plugins/iterum/tests/unit/ui/tap_pattern_editor_test.cpp (25 cases, logic-level). Executable: plugin_tests.
- Controller wiring: plugins/iterum/src/controller/controller.cpp - createCustomView("TapPatternEditor"): sets tap
  count, snap, loads the 16 time + 16 level params, sets tag 920, installs the parameter callback (setNormalized +
  performEdit). setParamNormalized forwards kMultiTapSnapDivisionId and kMultiTapTapCountId to the view (skipped
  during bulk loads; syncAllViews re-syncs after). copyPatternToCustom (Copy button) and resetPatternToDefault
  (Reset button) in the same file; custom_views.{h,cpp} hold the two buttons. visibility_controller.h:
  ContainerVisibilityController finds the CUSTOM PATTERN fieldset by locating its tagged child (920).
- Parameters: plugins/iterum/src/parameters/multitap_params.h (atomics, registration, state save/load: the custom
  ratios are 16 floats + 16 levels appended after the older fields; Iterum's state stream has NO version field, new
  fields go at the END of the stream, see processor.cpp getState/setState and the Tape sync fields added 2026-10-29).
  IDs in plugins/iterum/src/plugin_ids.h: 900-913 MultiTap params, 920-923 UI tags (not params), 950-965 custom
  times, 966-981 custom levels. kMultiTapNoteValueId (911) and kMultiTapNoteModifierId (912) serve the mathematical
  patterns only; kMultiTapSnapDivisionId (913) is the editor's snap (0 = off, 1-21 = note values 1/64T .. 1/1D).
- DSP: dsp/include/krate/dsp/effects/multi_tap_delay.h - setCustomTimeRatio / applyCustomTimingPattern: time =
  clamp(maxDelayMs_ x ratio, kMinDelayMs, maxDelayMs_); the processor prepares it with maxDelayMs 5000. Tempo
  reaches the DSP through BlockContext (ctx.tempoBPM) in process(); the controller never sees it. Pattern/tap-count
  changes morph over Morph Time.
- Panel layout: specs/iterum-ui-redesign/design-system.md (the design system every panel follows; 4.10 custom views,
  7.9, 8.2 MultiTap) and specs/iterum-ui-redesign/panels/multitap.md (the MultiTap panel: CUSTOM PATTERN fieldset at
  12,288,876,180 with Snap label/menu and Reset on its title line and the editor at 12,28,852,140; the NOTE VALUE
  fieldset shows only for mathematical patterns, the CUSTOM PATTERN fieldset only for Custom, so the lower half of the
  panel is empty for every other pattern). The panel is built by specs/iterum-ui-redesign/xslt/multitap.xsl
  (+ cells.xsl); uidesc edits of more than ~3 controls are done with XSLT (npx xslt3 -xsl:F.xsl -s:IN -o:OUT),
  never by regex. Shared controls: plugins/shared/src/ui/ (ArcKnob, FieldsetContainer, ...).
- Screenshots: plugins/iterum/tests/unit/vst/editor_screenshot_test.cpp (hidden [.][screenshots] case: opens the
  editor headlessly with the shared view creators registered, root->attached(frame), setAnimationTime(0),
  triggerDeferedUpdates, writeShot) is the model for the tap-editor screenshot case. editor_panels_test.cpp
  ([iterum][ui]) asserts every control of every panel is live; its helpers show how to open the editor in a test.
- Tests run sharded: node tools/run-gate.js --filter ${PERF_FILTER} --out DIR <exe> ... (last line GATE GREEN|RED;
  per-shard logs under --out). A single case by name is the only direct exe run. Timing cases are excluded on
  purpose. node tools/header-consumers.js --tag '[iterum]' <modified files> names the executables a shared-header
  change reaches (multi_tap_delay.h reaches dsp_effects_tests and plugin_tests / approval_tests).
- Build: ${CMAKE} --build build/windows-x64-release --config Release --target <targets> --parallel -- /nr:false
  (a post-build copy permission error into C:/Program Files/Common Files/VST3 is harmless; a warning is not).

FROZEN (user rulings 2026-10-28, still in force): parameter IDs, types and ranges; every parameter keeps a control;
saved state stays loadable (append new fields at the end of the stream; a saved custom pattern must mean the same
thing after the change, or the design must say exactly what changes and why). Cross-platform VSTGUI only.
Release state: Iterum 1.0.0 is the open changelog entry (plugins/iterum/CHANGELOG.md top section); add bullets to it,
never bump the version.

WRITING FILES: write every file with the Write tool; heredocs are blocked by a hook. A command that exits non-zero has
failed: read its output. Before you return, confirm with ls that every file you claim to have written exists and is
non-empty; a file you could not write is a failure to report, never a success.
`

const EXEC_RULES = `EXECUTION RULES:
- Run every command as a FOREGROUND Bash command with timeout 600000, output redirected to a log file under ${TMP}/, then tail the log.
- NEVER use run_in_background or the Monitor tool; never sleep, poll or "wait". A foreground command returns when it is done.
- DISK GUARD: never run find /c, find /, du or any drive-wide walk; only tail logs, never cat a whole log.
- The moment you have the result, call StructuredOutput.`

// The hidden screenshot case renders these states; the visual review compares them before/after.
const SHOT_STATES = [
  { file: 'custom-4taps.png', what: 'Timing Pattern = Custom, Taps = 4, Snap = 1/4, custom times at their registered defaults' },
  { file: 'custom-8taps.png', what: 'then Taps raised 4 -> 5 -> 6 -> 7 -> 8 one step at a time through the Taps parameter (the way a user drags the slider), so the add-tap placement is what is on screen' },
  { file: 'custom-16taps.png', what: 'then Taps raised one step at a time to 16' },
  { file: 'custom-8taps-snap8.png', what: 'Taps = 8 (as reached above), Snap = 1/8' },
  { file: 'custom-8taps-snap-off.png', what: 'Taps = 8, Snap = Off' },
  { file: 'custom-8taps-levels.png', what: 'Taps = 8, Snap = 1/4, tap levels set through the level parameters to 1.0, 0.7, 0.5, 0.25, 0.1, 0.05, 0.00501 (-46 dB), 0.0 (how the level axis reads)' },
  { file: 'custom-8taps-free.png', what: 'Length = Free (the legacy 5 s axis with the tempo beat grid)' },
  { file: 'custom-8taps-2bars-60bpm.png', what: 'Length = 2/1 at 60 BPM after Reset: over-ceiling region, eight taps dividing the playable 5 s' },
  { file: 'custom-length8-snap4.png', what: 'Length = 1/8 with Snap = 1/4: the 1/16 fallback lines' },
  { file: 'preset-quarter-8taps.png', what: 'Timing Pattern = Quarter, 8 taps, Length 2/1 (read-only display; title line under the Copy row)' },
  { file: 'preset-quarter-16taps.png', what: 'Quarter, 16 taps, Length Free: taps 10-16 stacked at 5 s' },
  { file: 'preset-golden-8taps.png', what: 'Timing Pattern = Golden Ratio, Note Value 1/4, 8 taps (read-only display)' },
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
    notes: { type: 'string' },
  },
  required: ['ok', 'shots'],
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
          kind: { type: 'string', description: 'bug | usability | visual | consistency | dsp | state | prior-art' },
          detail: { type: 'string' },
          evidence: { type: 'string', description: 'file:line, screenshot, or source URL' },
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
    axisModel: { type: 'string', description: 'the recommended time-axis model in one sentence' },
    codeChanges: { type: 'array', items: { type: 'string' }, description: 'each C++ / uidesc / state change with the file it touches' },
    openQuestions: { type: 'array', items: { type: 'string' }, description: 'decisions that are the user\'s to make' },
  },
  required: ['file', 'summary', 'axisModel', 'codeChanges', 'openQuestions'],
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
          where: { type: 'string', description: 'section of the document, file:line, or screenshot' },
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
    rulesWithoutTest: { type: 'array', items: { type: 'string' }, description: 'rules of the design that no test can hold, with the reason; the visual review covers them' },
    problems: { type: 'array', items: { type: 'string' } },
  },
  required: ['files', 'cases', 'rulesWithoutTest', 'problems'],
}
const GATE_SCHEMA = {
  type: 'object',
  properties: {
    green: { type: 'boolean' },
    build: { type: 'string', description: 'compile result: errors and warnings, or "clean"' },
    tests: { type: 'string', description: 'run-gate last line, verbatim, per executable set' },
    pluginval: { type: 'string' },
    shots: { type: 'string' },
    failures: { type: 'array', items: { type: 'string' }, description: 'each failure with the log path or assertion text' },
  },
  required: ['green', 'build', 'tests', 'pluginval', 'shots', 'failures'],
}
const LINT_SCHEMA = {
  type: 'object',
  properties: {
    green: { type: 'boolean' },
    clangTidy: { type: 'string' },
    editorBaseline: { type: 'string' },
    symbols: { type: 'string' },
    cleanup: { type: 'string' },
    problems: { type: 'array', items: { type: 'string' } },
  },
  required: ['green', 'clangTidy', 'editorBaseline', 'symbols', 'cleanup', 'problems'],
}

const blockingOrMajor = (r) => (r && r.issues ? r.issues.filter(i => i.severity === 'blocking' || i.severity === 'major') : [])
const fmtIssues = (list) => list.map((i, n) => `${n + 1}. [${i.severity}] ${i.where || ''}: ${i.problem}\n   fix: ${i.fix}`).join('\n')

// The gate, shared by the build stage's loop and the post-review check. `targets` and `exes` widen when the DSP
// header changed (the implementer reports it).
const gatePrompt = (attempt, dspChanged) => `${CONTEXT}
TASK: verification gate, attempt ${attempt}. Run, in order, and report each result verbatim:
1. Build: ${CMAKE} --build build/windows-x64-release --config Release --target plugin_tests approval_tests Iterum${dspChanged ? ' dsp_effects_tests' : ''} --parallel -- /nr:false
   A compile error or ANY warning is a red.
2. node tools/run-gate.js --filter ${PERF_FILTER} --out ${TMP}/gate-a${attempt} plugin_tests approval_tests${dspChanged ? ' dsp_effects_tests' : ''} 2>&1 | tail -40
   (last line GATE GREEN / GATE RED; read a failing shard's log under --out, never re-run to re-read). approval_tests
   holds the processor's golden renders: a processor or DSP change reaches them, so they run. Never remove the filter.
3. tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Iterum.vst3"
4. Screenshots: mkdir -p ${SHOTS_AFTER}; ${SHOT_ENV}=${SHOTS_AFTER} build/windows-x64-release/bin/Release/plugin_tests.exe "Iterum_TapEditorScreenshots"
   (the one hidden case by name; at least ${SHOT_STATES.length} PNGs expected: the design adds preset-pattern shots
   to the case, so more is right, fewer is a red; list the files written)
5. If the dsp_systems Profundum evidence files changed (git status --short -- specs/profundum-*), restore them with
   git checkout -- <those paths>; a test rewrote them, it is not a code change.
green = build clean AND every GATE GREEN AND pluginval passed AND the case passed with at least ${SHOT_STATES.length} shots written. Change nothing else.
${EXEC_RULES}`

// ===========================================================================
// STAGE: design
// ===========================================================================
if (stage === 'design') {
  phase('Capture')
  const shotCase = await agent(`${CONTEXT}
TASK: add ONE hidden Catch2 case "${SHOT_CASE}" (tags "[.][screenshots][tap-editor]") in a new file ${SHOT_FILE},
modelled on editor_screenshot_test.cpp (copy its headless-editor setup; share helpers only by including a header if
one exists, otherwise duplicate the few static helpers in the new file). The case opens the editor, switches Mode to
MultiTap, sets Timing Pattern to Custom (normalized 1.0) through controller->setParamNormalized + the control's
valueChanged, as the screenshot test does for Mode, and writes these PNGs of the MultiTap panel (crop: the template
root, as editor_screenshot_test does) to the directory named by the env var ${SHOT_ENV} (default <temp>/iterum-tap-shots):
${SHOT_LIST}
Parameter changes go through the controller (setParamNormalized on the parameter object + performEdit is what the
UI does; use the same path the controls use so the view's setActiveTapCount / setSnapDivision hooks fire), followed
by Steinberg::UpdateHandler::instance()->triggerDeferedUpdates(). Tap count is 2-16 mapped linearly on 0-1; Snap
index i is i/21; the custom time/level ids are 950-965 and 966-981.
Register the file in plugins/iterum/tests/CMakeLists.txt next to editor_screenshot_test.cpp. Then build plugin_tests
(${CMAKE} --build build/windows-x64-release --config Release --target plugin_tests --parallel -- /nr:false; zero
warnings) and render: mkdir -p ${SHOTS_BEFORE} ${TMP}; ${SHOT_ENV}=${SHOTS_BEFORE} build/windows-x64-release/bin/Release/plugin_tests.exe "Iterum_TapEditorScreenshots".
Look at every PNG with the Read tool and confirm the CUSTOM PATTERN fieldset and the tap editor are visible in each;
a shot of a hidden editor is a failure to fix, not to report. Edit nothing but the new test file and the CMake list.
${EXEC_RULES}`, { label: 'shot-case', phase: 'Capture', model: 'sonnet', schema: CAPTURE_SCHEMA })
  if (!shotCase || !shotCase.ok) throw new Error(`capture failed: ${shotCase ? shotCase.notes : 'agent died'}`)

  phase('Research')
  const RESEARCH = [
    {
      key: 'code', model: 'sonnet', file: `${DIR}/research/code-facts.md`,
      brief: `CODE FACTS. Read the view, the logic header, its tests, the controller wiring, multitap_params.h, the processor's
MultiTap block and multi_tap_delay.h's custom pattern path. Write what the editor does today, precisely, with
file:line: what the x axis means in ms and how the Snap table relates to it; where new taps go on a tap-count change
(trace Taps 4 -> 8 at Snap 1/4 and list each new tap's ratio); what happens to taps on a decrease then an increase;
every "default position" rule (add, double-click, Reset) and whether they agree; the edit gestures (beginEdit /
performEdit / endEdit) for every path that writes a parameter, including setActiveTapCount and copyPatternToCustom;
drawing (fonts, colors, clipping of taps at ratio 0 and 1, the tap number label at low levels, overlap rendering);
the level axis mapping (linear gain -> dB in the DSP) and the kMinDelayMs clamp; how a saved custom pattern is
stored and what a tempo change does to it; the dotted/triplet snap column counts against the true note lengths.
Each finding carries its evidence. Confirm or refute each seed fact in this workflow's header comment (read
.claude/workflows/iterum-tap-editor.js) and say which.`,
    },
    {
      key: 'prior-art', model: 'sonnet', file: `${DIR}/research/prior-art.md`,
      brief: `PRIOR ART. How do shipping multi-tap delays present a tap editor? Study at least Logic Pro's Delay Designer
(the reference tap display), Waves SuperTap, Native Instruments Replika XT / Molekular, Melda MMultiBandDelay or
MultiTap, FabFilter Timeless 3, Ableton Live's Echo or Beat Repeat where relevant, and any other tap-grid editor you
find (UVI, Arturia, u-he Colour Copy, Soundtoys EchoBoy's rhythm modes). Use WebSearch/WebFetch (load them with
ToolSearch first). For each: time-axis model (fixed ms span, bars at tempo, note grid, zoomable), how the grid and
ruler are labelled, how taps are added/removed/selected (click empty space, count control, context menu), what a
tap shows (level, pan, filter lanes), readouts while dragging, snapping and fine-adjust (modifier keys), keyboard,
and how the display behaves when the pattern is a preset rather than custom. End with a table of the conventions
that recur across products (those are what users expect) and the ones that are product-specific. Cite URLs.`,
    },
    {
      key: 'critique', model: 'opus', file: `${DIR}/research/critique.md`,
      brief: `A MUSICIAN'S CRITIQUE. You are a producer using this editor for the first time. Look at every before-shot
(${SHOTS_BEFORE}/*.png, Read tool shows images; the list of what each shows is below) and read the view code for the
behaviours a still image cannot show (drag, double-click, Shift, Escape, cursor, hover). Then write what gets in the
way, ranked by how much it hurts: what you cannot tell from the display (where a tap is in time, how loud, which tap
is which, what the grid means), what you cannot do (place a tap where you want, remove one, fine-adjust, see a
value), what misleads (grid vs Snap, "1/4" that is not a quarter note, clipped taps at the edges), what wastes the
space (the empty lower half of the panel when the pattern is not Custom; could the display serve every pattern?),
and what is inconsistent with the rest of the editor (specs/iterum-ui-redesign/design-system.md: colors, fonts,
readouts). Form your own list first; the seed facts in the header of .claude/workflows/iterum-tap-editor.js are
there to check against afterwards, not to copy. Only real problems: say when something is fine.
Shots:
${SHOT_LIST}`,
    },
    {
      key: 'dsp-state', model: 'sonnet', file: `${DIR}/research/dsp-and-state.md`,
      brief: `DSP AND STATE CONSEQUENCES. The design will need to choose a time-axis model for the custom pattern. Work out,
against the code, what each candidate costs: (A) keep ratio x 5000 ms and draw the grid where the current tempo's
note lengths fall (needs the tempo in the controller); (B) make the custom pattern tempo-synced: ratio x N bars (or
x the Snap/Note Value length) at the host tempo, converted in the processor from ctx.tempoBPM (saved patterns change
meaning at other tempos: say exactly how, and whether the existing 16 ratios can be re-interpreted or need a new
appended field, e.g. a "custom span" or a time-mode flag); (C) a span parameter (ms or bars) that the user sets.
For each: the processor/DSP change (multi_tap_delay.h applyCustomTimingPattern, the Morph on changes, kMinDelayMs,
the 5000 ms ceiling at slow tempos), the state change (append-only; what an old preset loads as), and how the tempo
reaches the controller: Iterum has no processor->controller message today; read Innexus
(plugins/innexus/src/processor/processor_messages.cpp, controller_view_sync.cpp) and Disrumpo
(plugins/disrumpo/src/processor/processor_messaging.cpp) for the IMessage pattern this repo uses, note the
real-time rules (allocate the message outside the audio callback or use the SDK's allowed path; rate-limit; the
controller must survive a missing message = no tempo yet, and the Linux test harness has no run loop), and whether
the processor already tracks ctx.tempoBPM changes it could send. Also: the Copy button's calculatePatternRatios
(does the copied pattern keep its musical meaning under each model?), and whether the headless tests can inject a
tempo. Give each option a cost in files touched and a risk list; recommend nothing, the designer decides.`,
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
TASK: design the MultiTap tap editor as it should be, and write it to ${DESIGN}.
Inputs: the four research reports (${DIR}/research/*.md, digest below), the before-shots (${SHOTS_BEFORE}/, list
below), the view and logic code, the controller wiring, multitap_params.h, multi_tap_delay.h, and
specs/iterum-ui-redesign/design-system.md + panels/multitap.md (the panel the editor lives in).
${researchDigest}
Shots:
${SHOT_LIST}

The user's two complaints are the floor, not the ceiling: (1) raising the tap count must produce visibly new taps in
sensible places; (2) the grid must show the selected timing (1/4, 1/8 ...) so the user sees where in time a tap is
placed. Beyond them, adopt what the critique and the prior art show users expect, and nothing speculative: every
addition must remove a concrete problem named in the research.

The document must let two implementers (DSP/controller, then the view) build it without asking anything. Contents:
1. Problems addressed: each one, with its research evidence, and the ones deliberately left alone (and why).
2. Time axis: the model (from dsp-and-state.md's options, or a better one), what x = 0 and x = 1 mean, the grid and
   ruler (which lines, labels such as "1/4", "1 bar", ms when free), what the Snap menu now means, how the tempo
   reaches the view and what the view shows before the first tempo arrives, and the exact DSP formula.
3. Tap lifecycle: where a new tap goes (free grid line nearest the ideal spread, never on another tap; say the
   algorithm), what a decrease then increase does (remember or not; say which and why), the ONE default-position
   rule shared by add, double-click and Reset, and the edit gestures (beginEdit/performEdit/endEdit) for every path.
4. Interaction: select, drag time and level, modifier keys, keyboard nudge if adopted, readout while dragging
   (what, where, format per design-system 6.4), hover, cursor, double-click, Escape, right-click (today ignored).
5. Visual: the level axis mapping and its labels, tap bar geometry, selected/hovered states, overlap rendering
   (two taps on one line must stay distinguishable), edge clipping, colors and fonts as design-system tokens, the
   ruler, the empty-panel question (does the display also show preset patterns read-only? decide).
6. Panel change, if any: exact geometry against panels/multitap.md and the XSLT needed; else "none".
7. State and compatibility: every new or re-interpreted field, appended at the end of the stream; what every saved
   pattern from before loads as; what a preset saved after loads as before (if it must still load).
8. Implementation plan: ordered rows, each with the file, the function, and the Catch2 case that proves it (logic
   cases in tap_pattern_editor_test.cpp, state cases in the parameters tests, processor cases, a headless editor
   case for the view's hooks, and the hidden ${SHOT_CASE} for the visual review). Real-time rules for the processor
   side; no cached control pointers across mode switches on the controller side.
9. Open questions: decisions only the user can make (taste, musical semantics, compatibility trade-offs). Decide
   everything else and say why. Keep them few and make each a concrete A/B.
Write the whole document; no placeholders.`
  let design = await agent(designPrompt, { label: 'design', phase: 'Design', model: 'fable', effort: 'high', schema: DESIGN_SCHEMA })
  if (!design) throw new Error('design agent died')

  phase('Challenge')
  const LENSES = [
    { key: 'usability', brief: 'USABILITY: as a producer, does this editor now tell you where every tap is in time and level, make adding, placing and removing taps obvious, and behave the way the prior art has taught users? Is anything harder than today? Does the grid stop lying? Is every readout and color consistent with specs/iterum-ui-redesign/design-system.md?' },
    { key: 'implementability', brief: 'IMPLEMENTABILITY: can this be built as written? Check the DSP formula against multi_tap_delay.h (clamps, morph, 5000 ms ceiling), the state plan against processor.cpp getState/setState (append-only, no version field) and an actual old preset, the tempo path against VST3 real-time rules and the Innexus/Disrumpo precedents, the controller plan against the vst-guide pitfalls (view lifecycle across mode switches, bulk loads, syncAllViews), the panel geometry against panels/multitap.md, and that every rule in section 8 has a test that can really hold it.' },
  ]
  let challenge = []
  for (let round = 1; round <= 2; ++round) {
    const reviews = (await parallel(LENSES.map(l => () => agent(`${CONTEXT}
TASK: challenge ${DESIGN} through one lens. Default to skepticism: find what is wrong, missing, inconsistent or
unbuildable, and verify each claim against the code and the research (${DIR}/research/*.md) rather than trusting the
document. ${l.brief}
blocking = cannot be built as written, loses saved patterns, or breaks a frozen rule; major = a worse or inconsistent
editor; minor = polish. verdict "pass" only when nothing is blocking or major. Do not edit the document.`,
      { label: `challenge:${l.key}:r${round}`, phase: 'Challenge', model: 'opus', schema: ISSUES_SCHEMA })))).filter(Boolean)
    challenge = reviews.flatMap(r => blockingOrMajor(r))
    log(`challenge round ${round}: ${challenge.length} blocking/major issues`)
    if (!challenge.length) break
    if (round === 2) break   // what is left after two revisions goes to the user as open questions
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
    axisModel: design.axisModel,
    codeChanges: design.codeChanges,
    openQuestions: [
      ...design.openQuestions,
      ...challenge.map(i => `[challenge, unresolved after 2 revisions] ${i.where}: ${i.problem} (suggested: ${i.fix})`),
    ],
    research: research.map(r => ({ file: r.file, findings: r.findings.length, high: r.findings.filter(f => f.severity === 'high').length })),
    shotsBefore: SHOTS_BEFORE,
    shotCase: SHOT_FILE,
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
unresolved. Resolve ALL of them in ${DESIGN}: section 9 becomes "Rulings", each ruling with what it changed; apply
each ruling to every section it touches (time axis, lifecycle, interaction, visual, state, implementation rows and
their tests). Read the real code for every claim you restate (the view, the controller, multitap_params.h,
multi_tap_delay.h, processor.cpp getState/setState); the research reports are a start, the source is the truth.
In ${OPEN}, append "Resolved: <section(s)>" under every item (or "Open: <reason>" if it truly needs the user).
Report the files you changed.`, { label: 'reconcile', phase: 'Reconcile', model: 'opus', schema: RECONCILE_SCHEMA })
  if (!rec) throw new Error('reconcile agent died')
  const check = await agent(`${CONTEXT}
TASK: implementability check of the reconciled ${DESIGN}. Verify, against the code, that every implementation row in
section 8 names a real file and function, that the state plan is append-only and says what an old preset loads as,
that the DSP formula is consistent with multi_tap_delay.h's clamps, and that no ruling in ${OPEN} is contradicted by
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
3. ${SHOT_FILE} exists and ${SHOTS_BEFORE}/ holds 6 PNGs.
4. git status --short -- ${UIDESC} plugins/iterum/src dsp/include is empty (the build starts from the committed code).
5. npx xslt3 -? runs.
ok = every check passed. Change nothing.
${EXEC_RULES}`, { label: 'preflight', phase: 'Preflight', model: 'haiku', effort: 'low', schema: PREFLIGHT_SCHEMA })
if (!pre || !pre.ok) {
  return { stage: 'build', stopped: 'preflight', branch: pre ? pre.branch : null, problems: pre ? pre.problems : ['preflight agent died'] }
}

phase('Tests')
const tests = await agent(`${CONTEXT}${NOTES}
TASK: write the FAILING tests first, for every implementation row of ${DESIGN} section 8 that names a Catch2 case:
logic cases in plugins/iterum/tests/unit/ui/tap_pattern_editor_test.cpp (new-tap placement never on an occupied grid
line, the one default rule, the snap table against true note lengths, the axis mapping), state cases beside the
existing multitap parameter tests (old stream loads, new fields round-trip, appended at the end), processor/DSP cases
where the formula changed (dsp/tests/unit/effects/ for multi_tap_delay.h, [iterum]-tagged processor cases for
processor.cpp), and a headless editor case in plugins/iterum/tests/unit/vst/ (modelled on editor_panels_test.cpp)
for the view's hooks (tap count change through the controller, the tempo arriving, the readout). Follow the existing
style of each file; a new DSP case tagged like its neighbours; nothing hidden except ${SHOT_CASE}.
A case that references functions that do not exist yet may be written against the signatures ${DESIGN} specifies;
then it fails to COMPILE, which counts as RED: say so per case. Build only the executables you touched
(${CMAKE} --build ... --target plugin_tests [dsp_effects_tests] --parallel -- /nr:false) when they can compile, and
run each new case BY NAME to confirm it is red (a case name containing a comma must be escaped as "Name\\, more").
Do not implement anything. Rules of the design that no test can hold go to rulesWithoutTest with the reason.
${EXEC_RULES}`, { label: 'tests', phase: 'Tests', model: 'opus', schema: TESTS_SCHEMA })
if (!tests) throw new Error('test author died')

phase('Implement')
const dspImpl = await agent(`${CONTEXT}${NOTES}
TASK: implement the DSP / processor / parameters / controller side of ${DESIGN} (its sections 2, 3, 7 and the matching
rows of 8): the time-axis formula in multi_tap_delay.h, the state fields (appended at the END of the stream, both
save and load, and the controller-side sync of the same stream), the tempo path from processor to controller if the
design has one (real-time safe: no allocation on the audio thread beyond what the SDK's message path allows, rate
limited, the controller tolerant of no message yet), the controller's new view hooks (the API the view will expose:
declare it in tap_pattern_editor.h exactly as section 8 specifies, with a minimal body if the view implementer owns
the behaviour), and the edit-gesture fixes in controller.cpp. The tests written in the previous step are the
contract: make the DSP, state, processor and controller cases pass; the view cases may stay red for the next
implementer. Build plugin_tests, approval_tests, Iterum and dsp_effects_tests with zero warnings; run each case you
turned green BY NAME. Do not run whole suites; the gate does. Report in summary whether dsp/include changed
("DSP CHANGED" or "DSP UNCHANGED", verbatim).
${EXEC_RULES}`, { label: 'impl:dsp-controller', phase: 'Implement', model: 'opus', schema: IMPL_SCHEMA })
if (!dspImpl) throw new Error('dsp/controller implementer died')
const dspChanged = /DSP CHANGED/.test(dspImpl.summary) || dspImpl.files.some(f => /^dsp\/include/.test(f))

const viewImpl = await agent(`${CONTEXT}${NOTES}
TASK: implement the VIEW side of ${DESIGN} (sections 2-5 as they concern tap_pattern_editor.{h,cpp} and
tap_pattern_editor_logic.h, and the matching rows of 8): the grid and ruler that reflect the timing, the new-tap
placement, the one default rule, the readout while dragging, selection/hover/overlap rendering, the level axis,
colors and fonts as design-system tokens, and any interaction the design adds. The previous implementer has already
done the DSP, state and controller side and declared the view API (read its report below); build on it, do not undo
it. The tests written earlier are the contract: make every remaining red case pass without weakening any.
If section 6 of the design changes the panel geometry, write ${DIR}/xslt/multitap-editor.xsl (XSLT 3.0, identity
transform + the changed views) and apply it: npx xslt3 -xsl:${DIR}/xslt/multitap-editor.xsl -s:${UIDESC} -o:${TMP}/editor.uidesc,
check node tools/uidesc-inventory.js ${TMP}/editor.uidesc --check --baseline specs/iterum-ui-redesign/inventory-before.json
(no new defect in MultiTapPanel, no lost binding), then copy it over ${UIDESC}; otherwise leave the uidesc alone.
Build plugin_tests and Iterum with zero warnings; run each case you turned green BY NAME; render the hidden
${SHOT_CASE} to ${TMP}/shots and LOOK at the PNGs (Read tool) before you return: a grid that does not match the design,
stacked taps, clipped labels or wrong colors are yours to fix now. Do not run whole suites; the gate does.
PREVIOUS IMPLEMENTER: ${dspImpl.summary}
Files: ${dspImpl.files.join(', ')}
${dspImpl.problems.length ? `Open problems it reported: ${dspImpl.problems.join('; ')}` : ''}
${EXEC_RULES}`, { label: 'impl:view', phase: 'Implement', model: 'opus', schema: IMPL_SCHEMA })
if (!viewImpl) throw new Error('view implementer died')

phase('Verify')
let gate = null
for (let attempt = 1; attempt <= 3; ++attempt) {
  gate = await agent(gatePrompt(attempt, dspChanged), { label: `gate:a${attempt}`, phase: 'Verify', model: 'haiku', effort: 'low', agentType: 'build-test-runner', schema: GATE_SCHEMA })
  if (gate && gate.green) break
  if (attempt === 3) break
  const failures = gate ? gate.failures.join('\n') : 'the gate agent died; re-run the gate commands yourself to see the state'
  await agent(`${CONTEXT}${NOTES}
TASK: the verification gate is red (attempt ${attempt}). Find the root cause of each failure and fix it in the code,
following ${DESIGN}; never weaken a test, never change a parameter ID, never relax a golden. An approval_tests red
after a processor change is either a real regression (fix the code) or a designed output change that the design
names explicitly (then re-approve that one golden and say which rule of the design justifies it). Read the logs the
failures point to before changing anything. Build after your fix; do not run the suites, the next gate does.
FAILURES:
${failures}
${EXEC_RULES}`, { label: `fix:a${attempt}`, phase: 'Verify', model: attempt === 1 ? 'sonnet' : 'opus', schema: IMPL_SCHEMA })
}
if (!gate || !gate.green) {
  return { stage: 'build', stopped: 'verify', gate, tests, dspImpl, viewImpl }
}

phase('Review')
const visualPrompt = (round) => `${CONTEXT}
TASK: visual review, round ${round}. Compare each ${SHOTS_BEFORE}/FILE with ${SHOTS_AFTER}/FILE (Read tool shows images):
${SHOT_LIST}
The first six have a before-shot; the rest were added by the design (Length, preset-pattern and fallback states) and
are judged against the design alone. Judge them
against ${DESIGN} sections 2-6 and specs/iterum-ui-redesign/design-system.md. You wrote none of this; judge what is on
screen: new taps visible and on free grid lines after every count step; the grid and ruler show the designed timing
and labels; tap numbers readable at every level; nothing clipped at the edges; two taps on one line distinguishable;
colors and fonts are the design system's; the readout (if the shot state shows one) formatted per 6.4; the panel
geometry unchanged unless section 6 changed it. blocking = a tap or control unusable or missing; major = the design
not met on screen; minor = polish. Edit nothing.`
const codeReviewPrompt = `${CONTEXT}
TASK: code review of the change set (git diff -- plugins/iterum/src dsp/include plugins/iterum/tests dsp/tests
${UIDESC}) against the repo's review criteria (.claude/skills/code-review/SKILL.md: real-time safety, thread safety,
numerical stability, layer dependencies, parameter handling, cross-platform). Focus: the processor->controller tempo
path (no audio-thread allocation or lock, bounded rate, nothing cached across view switches, the view null-safe
before didOpen and after willClose, bulk loads still skipped then re-synced), the state stream (append-only, old
streams load, both processor and controller readers agree byte for byte), the DSP formula's clamps and the morph,
the edit gestures (beginEdit/performEdit/endEdit paired on every path), fast-math hygiene (no std::isnan, no
infinity() seeds), and that no test was weakened. blocking = a real-time or state bug; major = a wrong behaviour or a
weakened test; minor = style. Edit nothing.`
let [visual, codeReview] = await parallel([
  () => agent(visualPrompt(1), { label: 'review:visual:r1', phase: 'Review', model: 'opus', schema: ISSUES_SCHEMA }),
  () => agent(codeReviewPrompt, { label: 'review:code', phase: 'Review', model: 'opus', schema: ISSUES_SCHEMA }),
])
let reviewLeft = [...blockingOrMajor(visual), ...blockingOrMajor(codeReview)]
if (reviewLeft.length) {
  await agent(`${CONTEXT}${NOTES}
TASK: fix these review findings in the code (XSLT for a uidesc change of more than ~3 controls, updating
${DIR}/xslt/multitap-editor.xsl so the stylesheet stays the record). Follow ${DESIGN}. Build plugin_tests, Iterum
(and dsp_effects_tests if you touched dsp/include) with zero warnings; do not run the suites, the gate does.
FINDINGS:
${fmtIssues(reviewLeft)}
${EXEC_RULES}`, { label: 'review-fix', phase: 'Review', model: 'sonnet', schema: IMPL_SCHEMA })
  gate = await agent(gatePrompt(4, dspChanged), { label: 'gate:post-review', phase: 'Review', model: 'haiku', effort: 'low', agentType: 'build-test-runner', schema: GATE_SCHEMA })
  visual = await agent(visualPrompt(2), { label: 'review:visual:r2', phase: 'Review', model: 'opus', schema: ISSUES_SCHEMA })
  reviewLeft = blockingOrMajor(visual)
}

phase('Close')
const changelog = await agent(`${CONTEXT}
TASK: add the changelog bullets for this change to the OPEN entry of plugins/iterum/CHANGELOG.md (the top "## [1.0.0]"
section; never bump the version, never add a new section). Read ${DESIGN} section 1 (problems addressed) and the
diff (git diff --stat; git diff -- plugins/iterum/src/ui plugins/iterum/src/parameters/multitap_params.h) and write
what a user notices, in the style of the entry's existing bullets (bold lead phrase, then the behaviour, then the
compatibility note if saved patterns changed meaning): under "### Fixed" for the stacked taps and the grid that did
not match the timing, under "### Changed" or "### Added" for the rest. Also append the design's one-line summary to
the Ruinae changelog ONLY if dsp/include/krate/dsp/effects/multi_tap_delay.h changed AND Ruinae includes it
(grep plugins/ruinae/src for multi_tap_delay); otherwise do not touch Ruinae. Edit nothing else.`,
  { label: 'changelog', phase: 'Close', model: 'sonnet', schema: IMPL_SCHEMA })

const lint = await agent(`${CONTEXT}
TASK: close-out checks. Run and report verbatim:
1. clang-tidy, the repo's script (never a header on its own: that reports every pre-existing line of the header):
   pwsh -NoProfile -File ./tools/run-clang-tidy.ps1 -Target iterum -BuildDir build/windows-ninja
   (its last lines read "Errors: N" / "Warnings: N"; both must be 0), plus, for every modified or new file under
   plugins/iterum/tests, dsp/tests and the test TU of a modified dsp/include header:
   "/c/Program Files/LLVM/bin/clang-tidy.exe" -p build/windows-ninja --quiet FILE 2>&1 | grep -v "bugprone-chained-comparison\\|cppcoreguidelines-avoid-do-while" | grep "warning:\\|error:"
   (the two filtered checks are accepted repo-wide; anything else printed in a changed line is a red; if
   build/windows-ninja has no compile_commands.json, say so as a problem instead of skipping silently).
2. node tools/lint-plugin-editor-baseline.js
3. node tools/gen-symbols.js   (the commit hook checks specs/_architecture_/symbols.json; report "regenerated")
4. node tools/clean-test-output.js (dry run first; if its listing includes anything under ${DIR}/ do not apply and
   say so), then node tools/clean-test-output.js --apply; report its last 5 lines.
5. git status --short -- specs/profundum-*  must be empty; if not, git checkout -- those paths (test evidence rewrite).
green = clang-tidy printed nothing AND the editor baseline lint passed. Change nothing else.
Copy every clang-tidy warning line into clangTidy BEFORE step 4 runs (the cleanup removes the log folder). Your
StructuredOutput must carry all six fields: green, clangTidy, editorBaseline, symbols, cleanup, problems (empty
array when none); a run on 2026-10-29 failed five times by leaving fields out.
${EXEC_RULES}`, { label: 'close', phase: 'Close', model: 'sonnet', schema: LINT_SCHEMA })

return {
  stage: 'build',
  branch: pre.branch,
  green: Boolean(gate && gate.green && lint && lint.green && !reviewLeft.length),
  gate,
  lint,
  tests: { cases: tests.cases, rulesWithoutTest: tests.rulesWithoutTest, problems: tests.problems },
  dspImpl: { files: dspImpl.files, summary: dspImpl.summary, problems: dspImpl.problems },
  viewImpl: { files: viewImpl.files, summary: viewImpl.summary, problems: viewImpl.problems },
  codeReviewMinor: codeReview ? codeReview.issues.filter(i => i.severity === 'minor') : [],
  reviewLeft,
  visualMinor: visual ? visual.issues.filter(i => i.severity === 'minor') : [],
  changelog: changelog ? changelog.summary : null,
  shotsBefore: SHOTS_BEFORE,
  shotsAfter: SHOTS_AFTER,
  next: 'User auditions the MultiTap editor in a host and reviews shots/after; commit only on the user\'s word.',
}
