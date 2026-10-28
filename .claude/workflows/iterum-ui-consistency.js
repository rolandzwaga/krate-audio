// Iterum UI consistency redesign: one shared design language across the ten delay-mode panels.
//
// Two stages, the user reviews between them (user ruling 2026-10-28):
//   {stage: "design"}  capture -> per-mode audit -> design system -> challenge/revise -> per-panel layout specs ->
//                      cross-panel consistency check. Writes specs/iterum-ui-redesign/ only; touches no source.
//   {stage: "build"}   preflight (never on main) -> global changes -> per-panel XSLT (parallel, dry-run) -> apply ->
//                      build/test/pluginval/screenshots gate with a bounded fix loop -> before/after visual review ->
//                      lints -> cleanup. Nothing commits.
//   Optional args.notes: the user's review rulings, passed to every build-stage author. Record them in
//   design-system.md first; the note is a pointer, the document is the authority.
//
// Model per job (user, 2026-10-28: "choose the right agent for the right job"):
//   haiku  - runs commands and reports (capture, apply, gates, lints, cleanup): no judgment, fixed procedure.
//   sonnet - bounded, checklist-driven work repeated per mode (audits, panel specs, panel XSLT, uidesc fixes).
//   fable  - the one creative synthesis: the design system across all ten modes, and its revisions.
//   opus   - independent judgment that must not be anchored on the author: the challengers, the cross-panel
//            consistency check, the before/after visual review, and C++ controller work (editor lifecycle pitfalls).
//
// Resume replays agent() calls by a hash chained over every call in order: changing a prompt's text re-runs that
// call and everything after it. Add arguments that default to the old behaviour instead of editing a used prompt.
export const meta = {
  name: 'iterum-ui-consistency',
  description: 'Iterum UI redesign for consistency across the ten delay modes: design stage (capture, audit, design system, challenge, per-panel specs, consistency check) then build stage (global changes, per-panel XSLT, gate, visual review)',
  whenToUse: 'When redesigning the Iterum editor so every delay mode shares one layout language. Flow: (1) run args {stage: "design"}; (2) the user reviews specs/iterum-ui-redesign/ (design-system.md, panels/*.md) and answers the open questions; (3) the main loop writes the rulings and every unresolved item to specs/iterum-ui-redesign/open-items.md and runs {stage: "reconcile"}; (4) on a feature branch, run {stage: "build", notes: "..."}; (5) the user auditions the editor. Nothing commits.',
  phases: [
    { title: 'Capture', detail: 'before-screenshots of all ten modes + uidesc inventory (haiku)', model: 'haiku' },
    { title: 'Audit', detail: 'one auditor per mode panel and one for the editor chrome (sonnet)', model: 'sonnet' },
    { title: 'Design', detail: 'one design system across all modes (fable)', model: 'fable' },
    { title: 'Challenge', detail: 'usability and implementability skeptics (opus), revision by the author (fable)', model: 'opus' },
    { title: 'Panel Specs', detail: 'exact layout spec per mode (sonnet)', model: 'sonnet' },
    { title: 'Consistency', detail: 'cross-panel consistency check (opus) + per-mode fixes (sonnet)', model: 'opus' },
    { title: 'Reconcile', detail: 'apply the user rulings and resolve every open item in the design and specs (fable)', model: 'fable' },
    { title: 'Preflight', detail: 'branch and artifact checks (haiku)', model: 'haiku' },
    { title: 'Global', detail: 'colors, fonts, editor template, shared templates, controller C++ (opus)', model: 'opus' },
    { title: 'Panels', detail: 'one XSLT per mode, dry-run on a scratch copy (sonnet)', model: 'sonnet' },
    { title: 'Apply', detail: 'apply the panel XSLTs + inventory check (haiku)', model: 'haiku' },
    { title: 'Verify', detail: 'build, sharded tests, pluginval, after-screenshots (haiku), bounded fix loop (sonnet, then opus)', model: 'haiku' },
    { title: 'Visual Review', detail: 'before/after review against the design system (opus), one fix round (sonnet)', model: 'opus' },
    { title: 'Lint', detail: 'editor baseline lint, clang-tidy when C++ changed (haiku)', model: 'haiku' },
    { title: 'Cleanup', detail: 'node tools/clean-test-output.js --apply (haiku)', model: 'haiku' },
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
const NOTES = A.notes ? `\nUSER REVIEW RULINGS (recorded in specs/iterum-ui-redesign/design-system.md; the document wins on any conflict):\n${A.notes}\n` : ''

// ---------------------------------------------------------------------------
// Fixed facts
// ---------------------------------------------------------------------------
const DIR = 'specs/iterum-ui-redesign'
const DESIGN = `${DIR}/design-system.md`
const UIDESC = 'plugins/iterum/resources/editor.uidesc'
const INV_BEFORE = `${DIR}/inventory-before.json`
const SHOTS_BEFORE = `${DIR}/shots/before`
const SHOTS_AFTER = `${DIR}/shots/after`
const CMAKE = '"C:/Program Files/CMake/bin/cmake.exe"'

const MODES = [
  { key: 'granular', name: 'Granular', template: 'GranularPanel' },
  { key: 'spectral', name: 'Spectral', template: 'SpectralPanel' },
  { key: 'shimmer', name: 'Shimmer', template: 'ShimmerPanel' },
  { key: 'tape', name: 'Tape', template: 'TapePanel' },
  { key: 'bbd', name: 'BBD', template: 'BBDPanel' },
  { key: 'digital', name: 'Digital', template: 'DigitalPanel' },
  { key: 'pingpong', name: 'PingPong', template: 'PingPongPanel' },
  { key: 'reverse', name: 'Reverse', template: 'ReversePanel' },
  { key: 'multitap', name: 'MultiTap', template: 'MultiTapPanel' },
  { key: 'freeze', name: 'Freeze', template: 'FreezePanel' },
]

const CONTEXT = `
PROJECT: Iterum, a ten-mode VST3 delay (VSTGUI, Steinberg SDK, C++20). Its editor was built by hand and is inconsistent
across modes; the goal is one consistent interface language for all ten delay modes.

WHERE THINGS ARE
- Editor: ${UIDESC}. Template "Editor" (900 x 600, min 900 x 600, max 1200 x 900): header (title + IconSegmentButton
  bound to control-tag "Mode"), a UIViewSwitchContainer (20,60, 860 x 400) that swaps one of ten templates
  (GranularPanel ... FreezePanel) on "Mode", and a footer (VersionLabel, Gain ArcKnob, CheckForUpdatesButton,
  SavePresetButton, PresetBrowserButton), then the UpdateBanner as the template's last view.
- Parameter IDs: plugins/iterum/src/plugin_ids.h, scheme k{Mode}{Parameter}Id, one block per mode. EVERY MODE HAS ITS
  OWN PARAMETER IDS: Digital's Mix is kDigitalMixId, Tape's is kTapeMixId. One control can never serve two modes, so
  anything "shared" across modes is a per-mode template following one layout, never one control.
- Controller: plugins/iterum/src/controller/ - controller.cpp (createCustomView, didOpen/willClose: the visibility
  controllers), visibility_controller.h (VisibilityController hides/shows controls by tag; ContainerVisibilityController
  finds a CONTAINER by locating one of its child controls by tag, so which container holds which tagged control is
  load-bearing), delay_time_sync_controller.* (sub-controller "<Mode>TimeSyncController" on the time knob group),
  custom_views.* (preset buttons, tap pattern buttons). plugins/iterum/src/ui/tap_pattern_editor.* (MultiTap custom view).
- Shared controls: plugins/shared/src/ui/ - ArcKnob, FieldsetContainer, IconSegmentButton, ToggleButton (registered
  in plugins/iterum/src/entry.cpp), plus the shared editor views (update_banner_view.h).
- Tools: node tools/gen-screenshots.js iterum --out DIR renders all ten modes headlessly (2x, editor-MODE.png, the
  editor at default parameter values); node tools/uidesc-inventory.js ${UIDESC} --out FILE.json writes every view with
  class, origin, size, template-absolute rect, control-tag, title, fieldset-title, parent, plus per-template metrics
  (sizes per class, fonts, colors) and geometry defects; add --check --baseline ${INV_BEFORE} to fail on overlap,
  out-of-bounds, lost bindings or changed tags. Sibling containers at the same origin are "stacked" (visibility
  variants, e.g. the Freeze pattern fieldsets) and are not defects.

KNOWN DEFECT (found 2026-10-28, confirmed in the real editor): every CSlider in the uidesc sets back-color /
value-color / frame-color, but VSTGUI's CSlider reads draw-back-color / draw-value-color / draw-frame-color, so every
slider draws VSTGUI's defaults (black track, white value). The redesign must fix this.

USER RULINGS (2026-10-28) - these bound every decision:
- Allowed: change layout inside panels; add shared header/footer regions; change the editor and panel size; change the
  visual style (colors, control types, section styling).
- Frozen: parameter IDs, types, ranges, defaults and saved state (plugin_ids.h, parameters/, processor). Control-tag
  names and numbers in the uidesc stay as they are. Every parameter that has a control today keeps one.
- Allowed: display labels may change, to the canonical names (Mix not Dry/Wet, Mod Depth, Mod Rate, Width ...; the
  table in plugins/iterum/CLAUDE.md) and to readable words instead of abbreviations like "PosJit".
- Cross-platform VSTGUI only, no native UI (root CLAUDE.md). The shared editor views stay: exactly one UpdateBanner
  (the Editor template's last view), one CheckForUpdatesButton, one VersionLabel.
- uidesc edits that touch more than ~3 controls are done with XSLT (npx xslt3 -xsl:FILE.xsl -s:IN -o:OUT), never by
  regex or string replacement.

WRITING FILES (2026-10-28: two auditors lost their reports to a failed heredoc and reported success): write every file
with the Write tool; heredocs are blocked by a hook. A command that exits non-zero has failed: read its output. Before
you return, confirm with ls that every file you claim to have written exists and is non-empty; a file you could not
write is a failure to report, never a success.
`

const MODE_LIST = MODES.map(m => `${m.name} (${m.template}, editor-${m.key}.png)`).join(', ')

const EXEC_RULES = `EXECUTION RULES:
- Run every command as a FOREGROUND Bash command with timeout 600000, output redirected to a log file under F:/tmp/iterum-ui/, then tail the log.
- NEVER use run_in_background or the Monitor tool; never sleep, poll or "wait". A foreground command returns when it is done.
- DISK GUARD: never run find /c, find /, du or any drive-wide walk; only tail logs, never cat a whole log.
- The moment you have the result, call StructuredOutput.`

// ---------------------------------------------------------------------------
// Schemas
// ---------------------------------------------------------------------------
const CAPTURE_SCHEMA = {
  type: 'object',
  properties: {
    ok: { type: 'boolean' },
    shots: { type: 'array', items: { type: 'string' } },
    inventoryDefects: { type: 'string', description: 'the inventory tool\'s last line, verbatim' },
    notes: { type: 'string' },
  },
  required: ['ok', 'shots', 'inventoryDefects'],
}
const AUDIT_SCHEMA = {
  type: 'object',
  properties: {
    scope: { type: 'string' },
    file: { type: 'string' },
    controls: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          controlTag: { type: 'string' },
          label: { type: 'string', description: 'the label shown today' },
          canonicalLabel: { type: 'string' },
          role: { type: 'string', description: 'time | feedback | mix | width | modulation | filter | character | mode-specific | output | navigation' },
          widget: { type: 'string', description: 'class and size today, e.g. CSlider 260x20' },
          group: { type: 'string', description: 'the fieldset or container it sits in' },
          conditional: { type: 'string', description: 'when it is hidden/shown and by which controller, or empty' },
        },
        required: ['controlTag', 'role', 'widget'],
      },
    },
    issues: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          severity: { type: 'string', enum: ['high', 'medium', 'low'] },
          kind: { type: 'string', description: 'layout | alignment | grouping | labeling | widget-choice | sizing | color | overlap | wasted-space | bug' },
          detail: { type: 'string' },
        },
        required: ['severity', 'kind', 'detail'],
      },
    },
    summary: { type: 'string' },
  },
  required: ['scope', 'file', 'controls', 'issues', 'summary'],
}
const DESIGN_SCHEMA = {
  type: 'object',
  properties: {
    file: { type: 'string' },
    summary: { type: 'string' },
    editorSize: { type: 'string' },
    codeChanges: { type: 'array', items: { type: 'string' }, description: 'C++ changes the design needs (controller, visibility, shared views)' },
    openQuestions: { type: 'array', items: { type: 'string' }, description: 'decisions that are the user\'s to make' },
  },
  required: ['file', 'summary', 'editorSize', 'codeChanges', 'openQuestions'],
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
          where: { type: 'string', description: 'section of the document, or the mode' },
          problem: { type: 'string' },
          fix: { type: 'string' },
        },
        required: ['severity', 'where', 'problem', 'fix'],
      },
    },
  },
  required: ['verdict', 'issues'],
}
const PANEL_SCHEMA = {
  type: 'object',
  properties: {
    mode: { type: 'string' },
    file: { type: 'string' },
    mapped: { type: 'integer', description: 'bound controls the spec places' },
    unmapped: { type: 'array', items: { type: 'string' }, description: 'control-tags of this template the spec does not place (must be empty)' },
    deviations: { type: 'array', items: { type: 'string' }, description: 'places the spec departs from the design system, with the reason' },
  },
  required: ['mode', 'file', 'mapped', 'unmapped', 'deviations'],
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
const IMPL_SCHEMA = {
  type: 'object',
  properties: {
    files: { type: 'array', items: { type: 'string' } },
    summary: { type: 'string' },
    problems: { type: 'array', items: { type: 'string' }, description: 'anything left undone or uncertain; empty when done' },
  },
  required: ['files', 'summary', 'problems'],
}
const GATE_SCHEMA = {
  type: 'object',
  properties: {
    green: { type: 'boolean' },
    build: { type: 'string', description: 'compile result: errors and warnings, or "clean"' },
    inventory: { type: 'string', description: 'uidesc-inventory --check last line, verbatim' },
    tests: { type: 'string', description: 'run-gate last line, verbatim' },
    pluginval: { type: 'string' },
    shots: { type: 'string' },
    failures: { type: 'array', items: { type: 'string' }, description: 'each failure with the log path or assertion text' },
  },
  required: ['green', 'build', 'inventory', 'tests', 'pluginval', 'shots', 'failures'],
}
const LINT_SCHEMA = {
  type: 'object',
  properties: {
    green: { type: 'boolean' },
    editorBaseline: { type: 'string' },
    clangTidy: { type: 'string', description: 'result, or "skipped: no C++ changed"' },
    cleanup: { type: 'string' },
  },
  required: ['green', 'editorBaseline', 'clangTidy'],
}

const blockingOrMajor = (r) => (r && r.issues ? r.issues.filter(i => i.severity === 'blocking' || i.severity === 'major') : [])
const fmtIssues = (list) => list.map((i, n) => `${n + 1}. [${i.severity}] ${i.where || i.mode || ''}: ${i.problem}\n   fix: ${i.fix}`).join('\n')

// Cross-panel consistency: check (opus), fix the flagged specs (sonnet), re-check once (opus). Returns what is left.
// labelPrefix keeps each stage's agent labels distinct ('' keeps the design stage's original labels).
async function runConsistency(phaseName, labelPrefix) {
  const consistencyPrompt = (round) => `${CONTEXT}
TASK: check the ten panel specs (${DIR}/panels/*.md) against ${DESIGN} and against EACH OTHER (round ${round}). The point
of this redesign is that the modes look like one product: the same kind of parameter takes the same slot, widget, size,
label and readout in every mode; sections appear in the same order at the same coordinates; spacing follows the grid.
Also check that every bound control-tag of each mode's template in ${INV_BEFORE} appears in that mode's spec.
Report each inconsistency against the mode whose spec must change ("where" = the mode key: ${MODES.map(m => m.key).join(', ')}).
blocking = a control lost or impossible geometry; major = a visible inconsistency between modes; minor = polish.
Do not edit any file.`
  let consistency = await agent(consistencyPrompt(1), { label: `${labelPrefix}consistency:r1`, phase: phaseName, model: 'opus', schema: ISSUES_SCHEMA })
  let left = blockingOrMajor(consistency)
  if (left.length) {
    const byMode = MODES.map(m => ({ m, issues: left.filter(i => String(i.where).toLowerCase().includes(m.key)) })).filter(x => x.issues.length)
    await parallel(byMode.map(({ m, issues }) => () => agent(`${CONTEXT}
TASK: fix the ${m.name} panel spec ${DIR}/panels/${m.key}.md for these cross-panel consistency findings, following
${DESIGN}. Edit only that file.
${fmtIssues(issues)}`, { label: `${labelPrefix}panel-fix:${m.key}`, phase: phaseName, model: 'sonnet', schema: PANEL_SCHEMA })))
    consistency = await agent(consistencyPrompt(2), { label: `${labelPrefix}consistency:r2`, phase: phaseName, model: 'opus', schema: ISSUES_SCHEMA })
    left = blockingOrMajor(consistency)
  }
  return left
}

// ===========================================================================
// STAGE: design
// ===========================================================================
if (stage === 'design') {
  phase('Capture')
  const capture = await agent(`${CONTEXT}
TASK: capture the current editor as the "before" reference. Run exactly these, in order:
1. mkdir -p F:/tmp/iterum-ui ${SHOTS_BEFORE}
2. node tools/gen-screenshots.js iterum --out ${SHOTS_BEFORE}   (builds plugin_tests, then renders; a post-build copy
   permission error into C:/Program Files/Common Files/VST3 is harmless, a compile error is not)
3. node tools/uidesc-inventory.js ${UIDESC} --out ${INV_BEFORE}
Report the ten PNG paths written, and the inventory's last line (UIDESC CLEAN/DEFECTS ...) verbatim. ok = all ten
PNGs exist and the inventory JSON was written (defects in the CURRENT uidesc are expected and are not a failure).
${EXEC_RULES}`, { label: 'capture', phase: 'Capture', model: 'haiku', effort: 'low', schema: CAPTURE_SCHEMA })
  if (!capture || !capture.ok) throw new Error(`capture failed: ${capture ? capture.notes : 'agent died'}`)

  phase('Audit')
  const auditTargets = [
    ...MODES.map(m => ({ scope: m.name, file: `${DIR}/audits/${m.key}.md`, what: `the ${m.name} mode panel: template ${m.template}, screenshot ${SHOTS_BEFORE}/editor-${m.key}.png` })),
    { scope: 'Editor chrome', file: `${DIR}/audits/chrome.md`, what: `the editor chrome: template "Editor" (header, mode strip, panel frame, footer) as it appears in every screenshot under ${SHOTS_BEFORE}/, plus the uidesc's <colors>, <fonts>, <gradients> and <bitmaps> and how consistently the panels use them (inventory metrics per template)` },
  ]
  const audits = (await parallel(auditTargets.map(t => () => agent(`${CONTEXT}
TASK: audit ${t.what}.
Read: the screenshot (Read tool shows images), the template in ${UIDESC}, its entry in ${INV_BEFORE} (views with
template-absolute rects, metrics, stacked pairs, defects), and the controller code that hides/shows or wires anything in
this scope (grep controller.cpp and visibility_controller.h for this mode's control-tags and template name).
Record every bound control: tag, label shown, the canonical label it should carry, its role, widget and size, group,
and any visibility condition. Then list the problems a user meets: misaligned rows, inconsistent widget choice for the
same kind of parameter, inconsistent sizes, cramped or empty areas, abbreviations, groups that split related controls,
overlaps, defects from the inventory, the CSlider color defect. Judge against the other modes too where it helps (the
other screenshots are in the same folder), but your report covers ${t.scope} only.
Write the audit to ${t.file} (markdown: a control table, then issues by severity, each issue with the view and its
coordinates). Do not edit anything else.`, { label: `audit:${t.scope}`, phase: 'Audit', model: 'sonnet', schema: AUDIT_SCHEMA }))))
    .filter(Boolean)
  if (audits.length < auditTargets.length) log(`audit: ${auditTargets.length - audits.length} of ${auditTargets.length} auditors returned nothing; the design proceeds without them`)

  const auditDigest = audits.map(a => `- ${a.scope} (${a.file}): ${a.controls.length} controls, ${a.issues.filter(i => i.severity === 'high').length} high / ${a.issues.length} issues. ${a.summary}`).join('\n')

  phase('Design')
  const designPrompt = `${CONTEXT}
TASK: design ONE interface language for Iterum that every delay mode follows, and write it to ${DESIGN}.
Inputs: the audits (${DIR}/audits/*.md, digest below), the before-screenshots (${SHOTS_BEFORE}/, ${MODE_LIST}),
${INV_BEFORE}, the uidesc, plugin_ids.h and the controller code named above.
${auditDigest}

The document must let ten independent implementers lay out ten panels that look like one product. It must contain:
1. Principles: what a musician should find in the same place in every mode, and why.
2. Editor geometry: editor size (min/max), regions (header, mode strip, any shared per-mode strip, mode panel, footer),
   the grid (columns, gutters, row height, margins), all in pixels.
3. Section taxonomy and order: the standard sections (e.g. Time, Feedback, Character, Modulation, Filter, Stereo,
   Output/Mix) with their fixed position, which sections are optional per mode, and where mode-specific sections go.
   Remember each mode has its own parameter IDs: a shared region is ten per-mode templates with one layout.
4. Control vocabulary: which widget each kind of parameter gets (continuous, bipolar, time with sync, enum of 2-4,
   enum of more, toggle, free/synced pair), with exact sizes, label placement, value-readout placement, and the
   attribute set to use (including the correct CSlider draw-*-color names if sliders stay).
5. Visual style: color tokens (names + rgba), fonts, section/fieldset styling, states. Light/dark is your call; say why.
6. Labels: the canonical label dictionary, and the rule for abbreviations (none, unless a width limit forces one).
7. Per-mode mapping table: for EVERY bound control-tag of EVERY mode (take them from the inventory), its section and
   slot. No control may be left out; parameters whose meaning is the same across modes take the same slot and label.
8. Conditional groups: how visibility-switched groups (Freeze pattern variants, free/synced time, MultiTap's pattern
   editor) fit the grid, and which container must keep which tagged child for the visibility controllers.
9. Implementation plan: template structure, the XSLT strategy (one stylesheet per panel + one global), and the C++
   changes needed (controller, visibility controllers, sub-controllers, shared views), each with the file it touches.
10. Open questions: decisions only the user can make. Keep them few; decide everything you can and say why.
Write the whole document; no placeholders.`
  let design = await agent(designPrompt, { label: 'design', phase: 'Design', model: 'fable', effort: 'high', schema: DESIGN_SCHEMA })
  if (!design) throw new Error('design agent died')

  phase('Challenge')
  const LENSES = [
    { key: 'usability', brief: 'USABILITY: does a musician switching modes find time, feedback, mix, modulation and filter in the same place, with the same widget and label? Are the mode-specific sections clear and the dense modes (Freeze, Granular, Tape) readable at the proposed size? Any control that becomes harder to reach, read or adjust than today?' },
    { key: 'implementability', brief: 'IMPLEMENTABILITY: can this be built in VSTGUI uidesc + the existing controller without breaking it? Check per-mode parameter IDs (no control shared across modes), UIViewSwitchContainer behaviour, visibility controllers that locate containers by child tag, sub-controllers on the time groups, the tap pattern editor, editor resizing (min/max size, autosize flags), the shared editor views, CSlider attribute names, and that the per-mode mapping table covers EVERY bound control-tag in the inventory.' },
  ]
  let challenge = []
  for (let round = 1; round <= 2; ++round) {
    const reviews = (await parallel(LENSES.map(l => () => agent(`${CONTEXT}
TASK: challenge the design system in ${DESIGN} through one lens. Default to skepticism: find what is wrong, missing or
inconsistent, and verify each claim against the uidesc, ${INV_BEFORE}, plugin_ids.h and the controller code rather than
trusting the document. ${l.brief}
blocking = it cannot be built as written or it loses a control; major = it will produce an inconsistent or worse UI;
minor = polish. verdict "pass" only when there is nothing blocking or major. Do not edit the document.`,
      { label: `challenge:${l.key}:r${round}`, phase: 'Challenge', model: 'opus', schema: ISSUES_SCHEMA })))).filter(Boolean)
    challenge = reviews.flatMap(r => blockingOrMajor(r))
    log(`challenge round ${round}: ${challenge.length} blocking/major issues`)
    if (!challenge.length) break
    if (round === 2) break   // issues left after the second revision go to the user as open questions
    design = await agent(`${CONTEXT}
TASK: revise ${DESIGN} to resolve these challenges. Fix the document itself (all sections they touch, including the
mapping table); where a challenge is wrong, keep the design and add one line under "Challenges considered" saying why.
${fmtIssues(challenge)}`, { label: `revise:r${round}`, phase: 'Challenge', model: 'fable', effort: 'high', schema: DESIGN_SCHEMA }) || design
  }

  phase('Panel Specs')
  const panels = (await parallel(MODES.map(m => () => agent(`${CONTEXT}
TASK: write the exact layout spec for the ${m.name} mode (today's template ${m.template}) to ${DIR}/panels/${m.key}.md,
following ${DESIGN} to the pixel.
Inputs: ${DESIGN} (sections 2-8 govern you), ${DIR}/audits/${m.key}.md, ${SHOTS_BEFORE}/editor-${m.key}.png, the
template in ${UIDESC} and its entry in ${INV_BEFORE}.
The spec lists every view the new template(s) for this mode contain, in uidesc nesting order: template name, class,
origin, size, control-tag, label/title, and every attribute that differs from the design system's defaults. Then a
mapping table: every bound control-tag in today's ${m.template} -> where it goes. "unmapped" must come back empty; if
the design system has no slot for a control, place it by the design's rules for mode-specific sections and list it
under deviations with the reason. Check your coordinates: no sibling overlap (except deliberately stacked visibility
variants), nothing outside its parent. Do not edit any file but ${DIR}/panels/${m.key}.md.`,
    { label: `panel:${m.key}`, phase: 'Panel Specs', model: 'sonnet', schema: PANEL_SCHEMA })))).filter(Boolean)
  const unmapped = panels.filter(p => p.unmapped.length)
  if (unmapped.length) log(`panel specs: unmapped controls in ${unmapped.map(p => `${p.mode} (${p.unmapped.join(', ')})`).join('; ')}`)

  phase('Consistency')
  const remaining = await runConsistency('Consistency', '')

  return {
    stage: 'design',
    dir: DIR,
    design: DESIGN,
    summary: design.summary,
    editorSize: design.editorSize,
    codeChanges: design.codeChanges,
    openQuestions: [
      ...design.openQuestions,
      ...challenge.map(i => `[challenge, unresolved after 2 revisions] ${i.where}: ${i.problem} (suggested: ${i.fix})`),
      ...remaining.map(i => `[consistency, unresolved] ${i.where}: ${i.problem} (suggested: ${i.fix})`),
    ],
    audits: audits.map(a => ({ scope: a.scope, file: a.file, issues: a.issues.length, high: a.issues.filter(i => i.severity === 'high').length })),
    panels: panels.map(p => ({ mode: p.mode, file: p.file, mapped: p.mapped, unmapped: p.unmapped, deviations: p.deviations })),
    shotsBefore: SHOTS_BEFORE,
    next: 'User reviews design-system.md and panels/*.md and answers the open questions; the main loop writes rulings + unresolved items to open-items.md and runs {stage: "reconcile"}; then, on a feature branch, {stage: "build"}.',
  }
}

// ===========================================================================
// STAGE: reconcile
// ===========================================================================
const RECONCILE_SCHEMA = {
  type: 'object',
  properties: {
    summary: { type: 'string' },
    resolved: { type: 'integer', description: 'items of open-items.md marked Resolved' },
    unresolved: { type: 'array', items: { type: 'string' }, description: 'items left open, each with the reason; empty when done' },
    files: { type: 'array', items: { type: 'string' } },
  },
  required: ['summary', 'resolved', 'unresolved', 'files'],
}
if (stage === 'reconcile') {
  phase('Reconcile')
  const rec = await agent(`${CONTEXT}
TASK: reconcile the design before the build. ${DIR}/open-items.md holds the user's rulings (2026-10-28) and every item the
design stage left unresolved. Resolve ALL of them in ${DESIGN} and in every affected ${DIR}/panels/*.md. You own the
design; the panel specs must end up saying exactly what the design says.
- Rulings: section 10 becomes "Rulings (2026-10-28)", each ruling with what it changed; apply rulings 5-7 to the
  design and the specs (they replace the open questions they answer).
- Readouts: ONE format standard for every mode (time, percent, pitch semitones/cents, frequency, dB, plain numbers),
  stated once in 6.4 and used verbatim by every spec. A 9.3 row for every formatter that does not meet it today,
  each with its test-first Catch2 case. Read the real formatters (plugins/iterum/src/parameters/*_params.h, the Gain
  case in controller.cpp) instead of trusting the document; they are the ground truth for "today".
- Every C++ row must be right against the source: read delay_time_sync_controller.cpp (initial tag chosen in verifyView
  before the view is attached), custom_views.h and VSTGUI's cgradient.h / cbuttons.cpp (CGradient::create ownership:
  wrap in owned()) before writing those rows.
- Verification (9.4): (a) the gate as the build stage runs it: each panel stylesheet is dry-run on a private copy and
  answers only for its OWN templates and control-tags; UIDESC CLEAN is required once, after every stylesheet is
  applied. (b) tools/uidesc-inventory.js already has the 9.5 changes (UpdateBanner exemption, lost-control check, with
  --self-test); the *NoteValue exemption is unnecessary because those tags have no view in the baseline; the untouched
  file reads UIDESC DEFECTS {"overlap":17}. Update 9.5 to say so. (c) A REQUIRED test-first row: a Catch2 case
  tagged [iterum][ui] that opens the editor headlessly with the shared view creators registered (include their headers
  as plugins/iterum/tests/unit/vst/editor_screenshot_test.cpp does), sets the UIViewSwitchContainer animation time to
  0, switches the Mode control through all ten modes, and for each mode asserts that every control-tag of that mode has
  a live control of the designed class, and that no plain CViewContainer stands where the uidesc names a registered
  class. Search from the template root (frame child 0): the preset browser overlay reuses tags 0-31.
- Where a panel spec and the design disagree, decide which is right, make both say it, and add the decision to 11.
In ${DIR}/open-items.md, append "Resolved: <section(s)>" under every item (or "Open: <reason>" if it truly needs the
user). Report the files you changed.`, { label: 'reconcile', phase: 'Reconcile', model: 'fable', effort: 'high', schema: RECONCILE_SCHEMA })
  if (!rec) throw new Error('reconcile agent died')
  phase('Consistency')
  const left = await runConsistency('Consistency', 'reconcile:')
  return {
    stage: 'reconcile',
    summary: rec.summary,
    resolved: rec.resolved,
    unresolved: [
      ...rec.unresolved,
      ...left.map(i => `[consistency, unresolved] ${i.where}: ${i.problem} (suggested: ${i.fix})`),
    ],
    files: rec.files,
    next: 'If unresolved is empty, the user can review the diff of design-system.md and run {stage: "build"} on the feature branch.',
  }
}

// ===========================================================================
// STAGE: build
// ===========================================================================
phase('Preflight')
const pre = await agent(`${CONTEXT}
TASK: preflight for the build stage. Check, and report each failed check as a problem:
1. git branch --show-current is NOT main (the build edits source; it runs on a feature branch).
2. ${DESIGN}, ${INV_BEFORE}, ten files ${DIR}/panels/{${MODES.map(m => m.key).join(',')}}.md and ten PNGs in ${SHOTS_BEFORE}/ exist.
3. git status --short -- ${UIDESC} plugins/iterum/src is empty (the build starts from the committed editor).
4. npx xslt3 -? runs (the XSLT processor is installed).
ok = every check passed. Change nothing.
${EXEC_RULES}`, { label: 'preflight', phase: 'Preflight', model: 'haiku', effort: 'low', schema: PREFLIGHT_SCHEMA })
if (!pre || !pre.ok) {
  return { stage: 'build', stopped: 'preflight', branch: pre ? pre.branch : null, problems: pre ? pre.problems : ['preflight agent died'] }
}

phase('Global')
const globalImpl = await agent(`${CONTEXT}${NOTES}
TASK: implement the GLOBAL part of ${DESIGN} (its sections 2, 4, 5, 8 and 9): colors, fonts, gradients, the Editor
template (size, header, mode strip, panel frame, footer, any shared per-mode region and its switch container), the
CSlider attribute fix applied tree-wide, and every C++ change section 9 lists (controller, visibility controllers,
sub-controllers). FIRST write the per-mode editor test that 9.4 requires (all ten modes opened headlessly, every
control-tag of each mode live, no placeholder CViewContainer); it runs in every gate after this. Each C++ row of 9.3
is test-first: its Catch2 case fails before the change and passes after. Do NOT rewrite the ten mode panel templates;
per-panel stylesheets do that next. If the design adds
new per-mode templates for a shared region, create them here, empty or with the shared layout only, and say so.
How:
- Write the uidesc change as ${DIR}/xslt/00-global.xsl (identity transform + the changed parts) and apply it:
  npx xslt3 -xsl:${DIR}/xslt/00-global.xsl -s:${UIDESC} -o:F:/tmp/iterum-ui/global.uidesc, inspect, then copy over ${UIDESC}.
- C++: follow the vst-guide rules (no cached control pointers across view switches; deactivate before destroy in
  willClose). Keep the Every-Plugin-Editor rule intact (one UpdateBanner last, CheckForUpdatesButton, VersionLabel).
- Build to prove it compiles with zero warnings: ${CMAKE} --build build/windows-x64-release --config Release --target plugin_tests Iterum
  (a post-build copy permission error into C:/Program Files/Common Files/VST3 is harmless).
- Run node tools/uidesc-inventory.js ${UIDESC} --check --baseline ${INV_BEFORE} and report its last line; panel-level
  defects are expected until the panels land, lost bindings and changed tags are not.
${EXEC_RULES}`, { label: 'global', phase: 'Global', model: 'opus', schema: IMPL_SCHEMA })
if (!globalImpl) throw new Error('global implementer died')

phase('Panels')
// Each author writes its own stylesheet and dry-runs it on a private copy, so ten run in parallel without touching
// editor.uidesc; Apply runs them in one pass afterwards.
const panelImpls = (await parallel(MODES.map(m => () => agent(`${CONTEXT}${NOTES}
TASK: write ${DIR}/xslt/${m.key}.xsl, an XSLT 3.0 stylesheet that rebuilds the ${m.name} mode's template(s) exactly as
${DIR}/panels/${m.key}.md specifies (identity transform for everything else). ${DESIGN} governs anything the spec leaves
open. The global stylesheet has already been applied to ${UIDESC}; build on it, do not undo it.
Dry-run on a private copy, never on ${UIDESC}:
  mkdir -p F:/tmp/iterum-ui/${m.key}
  npx xslt3 -xsl:${DIR}/xslt/${m.key}.xsl -s:${UIDESC} -o:F:/tmp/iterum-ui/${m.key}/editor.uidesc
  node tools/uidesc-inventory.js F:/tmp/iterum-ui/${m.key}/editor.uidesc --check --baseline ${INV_BEFORE}
Defects in THIS mode's template(s), or a lost binding of one of this mode's control-tags, are yours to fix; iterate
until there are none. Defects in other templates belong to other authors; ignore them. Edit nothing but your .xsl.
${EXEC_RULES}`, { label: `xslt:${m.key}`, phase: 'Panels', model: 'sonnet', schema: IMPL_SCHEMA })))).filter(Boolean)
if (panelImpls.length < MODES.length) log(`panels: ${MODES.length - panelImpls.length} stylesheet authors returned nothing; Apply will report the missing files`)

phase('Apply')
const apply = await agent(`${CONTEXT}
TASK: apply the ten panel stylesheets to ${UIDESC}, in this order, each onto the previous result:
${MODES.map(m => `  ${DIR}/xslt/${m.key}.xsl`).join('\n')}
For each: npx xslt3 -xsl:THAT.xsl -s:CURRENT -o:F:/tmp/iterum-ui/apply-N.uidesc (N = 1..10), starting from ${UIDESC}.
Then copy the last result over ${UIDESC} and run node tools/uidesc-inventory.js ${UIDESC} --check --baseline ${INV_BEFORE}.
Report a missing stylesheet or an xslt3 error as a problem and do not copy anything over ${UIDESC} in that case.
Report the inventory's last line in summary. Change nothing else.
${EXEC_RULES}`, { label: 'apply', phase: 'Apply', model: 'haiku', effort: 'low', schema: IMPL_SCHEMA })
// Stop only on a real failure: the inventory's last line is the verdict. "problems" also carries caveats (a run on
// 2026-10-28 stopped here on "nothing verified beyond the exit codes" after a clean apply).
if (!apply || !/UIDESC CLEAN/.test(apply.summary)) {
  return { stage: 'build', stopped: 'apply', global: globalImpl, panels: panelImpls, apply }
}

phase('Verify')
const PERF_FILTER = "'~[performance]~[perf]~[benchmark]~[!benchmark]~[long]'"
const gatePrompt = (attempt) => `${CONTEXT}
TASK: verification gate, attempt ${attempt}. Run, in order, and report each result verbatim:
1. Build: ${CMAKE} --build build/windows-x64-release --config Release --target plugin_tests Iterum
   A compile error or ANY warning is a red (a post-build copy permission error into Common Files/VST3 is harmless).
2. node tools/uidesc-inventory.js ${UIDESC} --check --baseline ${INV_BEFORE}   (last line must read UIDESC CLEAN)
3. node tools/run-gate.js --filter ${PERF_FILTER} --out F:/tmp/gate/iterum-ui-a${attempt} plugin_tests 2>&1 | tail -40
   (sharded across the machine; last line GATE GREEN / GATE RED; per-shard logs in the --out folder: read a failing
   shard's log there, never re-run to re-read). approval_tests holds processor golden renders, which an editor change
   cannot reach; do not run it. Timing cases are excluded on purpose; never remove the filter.
4. tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Iterum.vst3"
5. node tools/gen-screenshots.js iterum --no-build --out ${SHOTS_AFTER}   (ten PNGs)
green = build clean AND UIDESC CLEAN AND GATE GREEN AND pluginval passed AND ten shots written. Change nothing.
${EXEC_RULES}`
let gate = null
for (let attempt = 1; attempt <= 3; ++attempt) {
  gate = await agent(gatePrompt(attempt), { label: `gate:a${attempt}`, phase: 'Verify', model: 'haiku', effort: 'low', agentType: 'build-test-runner', schema: GATE_SCHEMA })
  if (gate && gate.green) break
  if (attempt === 3) break
  const failures = gate ? gate.failures.join('\n') : 'the gate agent died; re-run the gate commands yourself to see the state'
  await agent(`${CONTEXT}${NOTES}
TASK: the verification gate is red (attempt ${attempt}). Find the root cause of each failure and fix it in the code or
the uidesc, following ${DESIGN} and the panel specs; never weaken a test, never relax the inventory check, never
change a parameter ID. Read the logs the failures point to before changing anything. Build after your fix
(${CMAKE} --build build/windows-x64-release --config Release --target plugin_tests Iterum). Do not run the test suites;
the next gate does.
FAILURES:
${failures}
${EXEC_RULES}`, { label: `fix:a${attempt}`, phase: 'Verify', model: attempt === 1 ? 'sonnet' : 'opus', schema: IMPL_SCHEMA })
}
if (!gate || !gate.green) {
  return { stage: 'build', stopped: 'verify', gate, global: globalImpl, panels: panelImpls }
}

phase('Visual Review')
const reviewPrompt = (round) => `${CONTEXT}
TASK: visual review, round ${round}. Compare, mode by mode, ${SHOTS_BEFORE}/editor-MODE.png with ${SHOTS_AFTER}/editor-MODE.png
(${MODE_LIST}), against ${DESIGN} and ${DIR}/panels/*.md. You did not write any of this; judge what is on screen.
Check: the modes now look like one product (same sections in the same places, same widgets and labels for the same
kind of parameter, aligned to the grid); nothing clipped, overlapping, unreadable or truncated; no control missing
compared with the before-shot; the CSlider colors now render as designed; the header, mode strip and footer are
consistent. Note that shots show default parameter values, so hidden visibility variants are not on screen: read the
uidesc for those. Report each problem against its mode ("where" = mode key, or "chrome").
blocking = a control missing or unusable; major = a visible inconsistency or defect; minor = polish. Edit nothing.`
let review = await agent(reviewPrompt(1), { label: 'review:r1', phase: 'Visual Review', model: 'opus', schema: ISSUES_SCHEMA })
let reviewLeft = blockingOrMajor(review)
if (reviewLeft.length) {
  // One fixer for all findings: they all edit editor.uidesc, so parallel fixers would collide.
  await agent(`${CONTEXT}${NOTES}
TASK: fix these visual-review findings in ${UIDESC} (XSLT for anything touching more than ~3 controls; update the
matching ${DIR}/xslt/*.xsl so the stylesheets stay the record of the change). Follow ${DESIGN}. Build afterwards
(${CMAKE} --build build/windows-x64-release --config Release --target plugin_tests Iterum) and re-render:
node tools/gen-screenshots.js iterum --no-build --out ${SHOTS_AFTER}
FINDINGS:
${fmtIssues(reviewLeft)}
${EXEC_RULES}`, { label: 'review-fix', phase: 'Visual Review', model: 'sonnet', schema: IMPL_SCHEMA })
  gate = await agent(gatePrompt(4), { label: 'gate:post-review', phase: 'Visual Review', model: 'haiku', effort: 'low', agentType: 'build-test-runner', schema: GATE_SCHEMA })
  review = await agent(reviewPrompt(2), { label: 'review:r2', phase: 'Visual Review', model: 'opus', schema: ISSUES_SCHEMA })
  reviewLeft = blockingOrMajor(review)
}

phase('Lint')
const lint = await agent(`${CONTEXT}
TASK: lints. Run and report verbatim:
1. node tools/lint-plugin-editor-baseline.js   (the shared editor views rule)
2. If git status --short -- plugins/iterum/src plugins/shared/src lists a modified or new C++ file:
   pwsh -NoProfile -File ./tools/run-clang-tidy.ps1 -Target iterum -BuildDir build/windows-ninja
   Otherwise report "skipped: no C++ changed". Any clang-tidy warning in a changed file is a red.
green = the baseline lint passes and clang-tidy is clean or skipped. Change nothing.
${EXEC_RULES}`, { label: 'lint', phase: 'Lint', model: 'haiku', effort: 'low', schema: LINT_SCHEMA })

phase('Cleanup')
const cleanup = await agent(`TASK: run node tools/clean-test-output.js --apply from the repo root as a FOREGROUND Bash
command (timeout 600000) and return its last 10 lines verbatim. It must not touch ${DIR}/; if its dry listing (run it
without --apply first) includes anything under ${DIR}, do not apply and say so.`, { label: 'cleanup', phase: 'Cleanup', model: 'haiku', effort: 'low' })

return {
  stage: 'build',
  branch: pre.branch,
  green: Boolean(gate && gate.green && lint && lint.green && !reviewLeft.length),
  gate,
  lint,
  global: globalImpl,
  panels: panelImpls.map(p => ({ files: p.files, problems: p.problems })),
  reviewLeft,
  reviewMinor: review ? review.issues.filter(i => i.severity === 'minor') : [],
  shotsBefore: SHOTS_BEFORE,
  shotsAfter: SHOTS_AFTER,
  cleanup,
  next: 'User auditions the editor in a host and reviews shots/after; commit only on the user\'s word.',
}
