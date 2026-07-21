export const meta = {
  name: 'gradus-audit',
  description: 'Full audit of the Gradus step-arpeggiator plugin: bugs, anti-patterns, duplication, wrong implementations — adversarially verified, synthesized into an implementation plan executable by a lesser model',
  whenToUse: 'When you want a comprehensive correctness/quality sweep of plugins/gradus (plus its shared arp engine in dsp/ and plugins/shared/) that ends in a concrete, ordered fix plan an Opus-class executor can follow without re-deriving context.',
  phases: [
    { title: 'Understand', detail: 'parallel readers map each subsystem', model: 'opus' },
    { title: 'Find', detail: 'one finder per bug dimension', model: 'opus' },
    { title: 'Verify', detail: 'two independent skeptics per finding', model: 'opus' },
    { title: 'Synthesize', detail: 'implementation plan + skill draft', model: 'opus' },
  ],
}

// ---------------------------------------------------------------------------
// Shared context handed to every agent so they judge against the right rules.
// ---------------------------------------------------------------------------
const CONTEXT = `
You are auditing the Gradus VST3/AU plugin — a standalone step-arpeggiator
instrument (AU 'aumu', MIDI in/out + stereo out) in the monorepo at
f:/projects/iterum. This is a READ-ONLY analysis task: DO NOT edit any files.
Read source and tests, reason about correctness.

ARCHITECTURE FACTS (trust these; verify details in code):
- Gradus was EXTRACTED from Ruinae's arp section. Param IDs 3000-3372
  (kArpBaseId=3000) are SHARED with Ruinae by design.
- The arp ENGINE is shared DSP: dsp/include/krate/dsp/processors/arpeggiator_core.h
  and midi_note_delay.h (Layer 2, header-only). Gradus plugin code is under
  plugins/gradus/src/{processor,controller,parameters,preset,ui,dsp}/.
- State SAVE prefix is unified in plugins/shared/src/parameters/arp_params_common.h
  (Krate::Shared::saveArpParamsShared); Gradus appends a plugin-specific tail
  (MIDI-delay / sequencer / Markov / speed-curve lanes). A cross-plugin
  byte-golden test guards shared-save identity.
- INTENTIONAL, NOT A BUG: loadArpParams stays Gradus-local because clamp ranges
  and version gates diverge from Ruinae (e.g. mode clamps 0-11 here vs 0-9
  there). Do not report the load-path split itself as duplication; DO report
  drift WITHIN it (e.g. save/load field-order mismatch, missing version gate).
- Processor holds arpEvents_[128] and combinedEvents_[512] (arp + delay echoes)
  fixed buffers, a built-in AuditionVoice, and prev-value caches so setters that
  reset engine state only fire on real changes.
- Tests: gradus_tests target, plugins/gradus/tests/. Existing coverage includes
  sequencer polymetric/rests/transport-gating, source-mode toggle/transpose,
  v2->v3 state migration, editor lifecycle, live-mode byte-identical, UI logic.

REAL-TIME AUDIO RULES (violations are bugs): no allocation/locks/exceptions/IO
on the audio thread (process(), setActive() during processing, anything the
engine calls per-block); parameters at the VST boundary are normalized [0,1]
and must be denormalized in processParameterChanges; NaN/Inf must not
propagate; processor and controller are separate components that must never
cross-include headers.

MIDI-INSTRUMENT-SPECIFIC HAZARDS to keep in mind: stuck notes (every note-on
must have a guaranteed note-off path across latch, source-mode toggles, arp
mode changes, transport stop, setActive(false), reset); event-buffer overflow;
sample-offset ordering within a block; double note-offs; hanging notes on
state reload.

Report format discipline: every finding must cite file and line(s) you
actually read. If you cannot point at concrete code, do not report it.
`

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
        required: ['title', 'severity', 'category', 'file', 'line', 'problem', 'why_wrong', 'consequence', 'suggested_fix'],
        properties: {
          title: { type: 'string', description: 'one-line summary' },
          severity: { type: 'string', enum: ['critical', 'high', 'medium', 'low'] },
          category: { type: 'string', description: 'kebab-case, e.g. rt-safety, stuck-notes, state-migration, duplication, ui-wiring, logic, test-gap, anti-pattern' },
          file: { type: 'string', description: 'repo-relative path' },
          line: { type: 'string', description: 'line number or range, e.g. "446" or "446-449"' },
          problem: { type: 'string', description: 'what the code does that is wrong' },
          why_wrong: { type: 'string', description: 'the correct behaviour and why this deviates' },
          consequence: { type: 'string', description: 'concrete user-visible or maintenance consequence' },
          suggested_fix: { type: 'string', description: 'concrete fix direction incl. which test would catch it' },
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
    refuted: { type: 'boolean', description: 'true if the finding is wrong, already handled elsewhere, or intentional per the stated architecture facts' },
    reasoning: { type: 'string', description: 'cite the exact code you read to reach this verdict' },
    severity_adjustment: { type: 'string', enum: ['none', 'upgrade', 'downgrade'], description: 'if not refuted, should severity change?' },
  },
}

const DOC_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['markdown'],
  properties: { markdown: { type: 'string' } },
}

// ---------------------------------------------------------------------------
// Phase 1: Understand — parallel subsystem readers.
// Barrier justified: every finder receives the combined map, and the skill
// author needs all maps together.
// ---------------------------------------------------------------------------
const SUBSYSTEMS = [
  {
    key: 'engine',
    prompt: `Map the shared arp engine that Gradus drives:
dsp/include/krate/dsp/processors/arpeggiator_core.h and midi_note_delay.h
(read them fully), plus any headers they include from krate/dsp. Cover: note
input tracking, mode/octave sequencing, lane system (velocity/gate/pitch/
ratchet + Gradus-specific sequencer/Markov/speed-curve lanes), event emission
contract (ArpEvent), latch/retrigger, reset semantics, and what MidiNoteDelay
adds on top.`,
  },
  {
    key: 'processor',
    prompt: `Map the Gradus processor: plugins/gradus/src/processor/processor.cpp
and processor.h (read fully), plus plugins/gradus/src/dsp/audition_voice.h.
Cover: process() flow (transport handling, param changes, event input->engine->
event output, audition rendering), buffer sizes and overflow guards, state
get/set, setActive/setupProcessing, threading of the audition atomics.`,
  },
  {
    key: 'params-state',
    prompt: `Map parameters + persistence: plugins/gradus/src/plugin_ids.h,
plugins/gradus/src/parameters/ (all files), plugins/shared/src/parameters/
arp_params_common.h, and plugins/gradus/tests/unit/vst/state_v2_v3_migration_test.cpp.
Cover: the full param inventory (ranges, defaults, normalization), save format
(shared prefix + Gradus tail, field order, version number), load-path clamps
and version gates, and how migration v2->v3 works.`,
  },
  {
    key: 'controller-ui',
    prompt: `Map the controller + UI layer: plugins/gradus/src/controller/ (all
files) and plugins/gradus/src/ui/ (all headers), plus resources/editor.uidesc
(scan structure + control-tags, not every pixel). Cover: param registration,
view sync / verify-view machinery, sub-controllers, custom views (ring display,
piano roll, markov matrix editor, speed curve editor, pin flag strip, lane tab
bar), how UI state reaches the processor and back, editor lifecycle.`,
  },
  {
    key: 'ruinae-arp',
    prompt: `Map how RUINAE uses the same arp engine, as the divergence baseline:
find the arp section in plugins/ruinae/ (grep for arpeggiator_core, kArpBaseId,
saveArpParams, loadArpParams) and read the relevant processor/controller/param
files. Cover: which params Ruinae exposes vs Gradus, its save/load code, clamp
ranges, and any Gradus code that looks copy-pasted from Ruinae rather than
shared.`,
  },
]

phase('Understand')
log('Mapping 5 subsystems in parallel (Opus readers)...')
const maps = await parallel(
  SUBSYSTEMS.map(s => () =>
    agent(
      `${CONTEXT}\n\nYOUR SUBSYSTEM: ${s.prompt}\n\nReturn a dense, factual map. The "oddities" list is the most valuable output — every suspicious thing you notice becomes a lead for the bug-finder phase.`,
      { label: `map:${s.key}`, phase: 'Understand', schema: MAP_SCHEMA, model: 'opus' },
    ).then(m => ({ key: s.key, ...m })),
  ),
)
const goodMaps = maps.filter(Boolean)
if (goodMaps.length < SUBSYSTEMS.length) {
  log(`WARNING: only ${goodMaps.length}/${SUBSYSTEMS.length} subsystem maps completed — finders proceed with partial context`)
}

const MAP_DIGEST = goodMaps
  .map(m => `## ${m.key}\n${m.summary}\nKEY FILES:\n${m.key_files.join('\n')}\nINVARIANTS:\n${m.invariants.join('\n')}\nODDITIES (investigate these):\n${m.oddities.join('\n')}`)
  .join('\n\n')

// ---------------------------------------------------------------------------
// Phase 2: Find — one finder per bug dimension, all fed the combined map.
// ---------------------------------------------------------------------------
const DIMENSIONS = [
  {
    key: 'rt-safety',
    prompt: `Hunt REAL-TIME SAFETY violations on the Gradus audio thread: any
allocation, lock, exception path, string/IO, unbounded loop, or blocking call
reachable from process()/setActive(); atomics used with wrong ordering or
non-lock-free types; denormal/NaN hazards in the audition voice. Focus:
plugins/gradus/src/processor/, plugins/gradus/src/dsp/audition_voice.h,
dsp/include/krate/dsp/processors/arpeggiator_core.h, midi_note_delay.h.`,
  },
  {
    key: 'stuck-notes-midi',
    prompt: `Hunt MIDI-correctness bugs: stuck notes, missing/double note-offs,
event ordering. Trace EVERY path a note-on takes to its note-off: latch modes,
source-mode toggle (spec 142 panic), arp mode/note-value changes mid-hold,
transport stop/start, setActive(false), state reload while notes held, delay
echoes outliving their source note, overflow of arpEvents_[128] /
combinedEvents_[512] (what happens when the arp + echoes exceed capacity in
one block?), sample-offset ordering of emitted events.`,
  },
  {
    key: 'lifecycle-state',
    prompt: `Hunt VST3 LIFECYCLE and STATE bugs: getState/setState field-order or
type mismatches (write int, read float; write in different order than read),
missing version gates, defaults drift between Processor state, Controller
setComponentState, and ArpeggiatorParams defaults; migration v2->v3 edge cases;
normalized-vs-plain confusion at any boundary; controller receiving state
before/after editor exists; preset load path (controller_presets.cpp) vs host
setState path divergence.`,
  },
  {
    key: 'duplication-drift',
    prompt: `Hunt DUPLICATION and COPY-PASTE DRIFT between Gradus and Ruinae, and
within Gradus itself: logic that exists in both plugins but has silently
diverged where it should be shared (remember: the load path split is
INTENTIONAL — look for drift inside it, like field order or missed fields,
not the split itself); repeated blocks within controller_*.cpp or ui/ headers
that should be one helper; constants defined in multiple places with different
values (e.g. lane counts, step counts, note-value tables, dropdown mappings vs
engine enums).`,
  },
  {
    key: 'ui-wiring',
    prompt: `Hunt UI WIRING bugs: control-tags in resources/editor.uidesc that do
not match plugin_ids.h; params registered but absent from the uidesc or vice
versa; dropdown/StringListParameter entry counts that disagree with engine enum
ranges (mode 0-11, note values, waveforms); view-sync code
(controller_view_sync.cpp, controller_verify_view.cpp) that misses params or
updates the wrong view; editor lifecycle hazards (dangling view pointers after
close, IDependent not deregistered, forget() imbalance); ui/*.h logic headers
whose math disagrees with their tests or with the engine (ring geometry,
euclidean, markov row normalization, speed-curve evaluation, piano roll
playhead).`,
  },
  {
    key: 'engine-logic',
    prompt: `Hunt LOGIC bugs in the arp engine itself and the processor's use of
it: off-by-one in step advance/wrap, polymetric lane length interactions, rest
handling, ratchet subdivision timing, swing, gate-length-to-samples math,
tempo/sample-rate changes mid-pattern, note-value table correctness, Markov
transition sampling (row normalization, zero rows), speed-curve evaluation,
octave sequencing at range boundaries, retrigger semantics, the prev-value
caching in the processor (a setter that should ALSO fire when some other
coupled param changes but doesn't).`,
  },
  {
    key: 'portability-quality',
    prompt: `Hunt CROSS-PLATFORM and CODE-QUALITY problems: narrowing conversions
that MSVC accepts but Clang/GCC reject; std::isnan under -ffast-math; float
comparisons with ==; uninitialized members; signed/unsigned mixups in loop
bounds; anti-patterns (God functions, magic numbers duplicating named
constants, dead code, misleading names/comments that contradict behaviour);
naming-convention violations vs the project style. Focus on plugins/gradus/src/
but include the two engine headers.`,
  },
  {
    key: 'test-gaps',
    prompt: `Hunt TEST DEFECTS and COVERAGE GAPS in plugins/gradus/tests/: tests
that cannot fail (assert existence rather than behaviour), tests that
duplicate each other, tautological golden tests, missing coverage for the
riskiest behaviours (buffer overflow at 128/512 events, stuck-note paths,
state-reload-during-playback, sample-rate change, all 12 modes, latch x
retrigger matrix). For each gap, name the specific test that should exist and
what it would assert.`,
  },
  {
    key: 'gestalt',
    prompt: `You are the GESTALT reviewer: read plugins/gradus/src/processor/processor.cpp
and plugins/gradus/src/controller/controller.cpp END TO END, top to bottom,
and flag anything that just doesn't make sense — code whose comment says one
thing and body does another, branches that can never execute, work done twice,
values computed then ignored, ordering that looks accidental, TODOs that hide
bugs. Ignore the other finders' dimensions if needed; your value is noticing
what category-driven review misses.`,
  },
]

phase('Find')
log(`Fanning out ${DIMENSIONS.length} finders (Opus)...`)
// Barrier justified: dedup below needs ALL findings at once.
const found = await parallel(
  DIMENSIONS.map(d => () =>
    agent(
      `${CONTEXT}\n\nSUBSYSTEM MAPS from the understanding phase (leads in ODDITIES are gold):\n${MAP_DIGEST}\n\nYOUR DIMENSION: ${d.prompt}\n\nRead the actual code before reporting anything. Quality over quantity: a finding without a concrete file:line you personally read is worthless. Also do not report the intentional load-path split as a bug.`,
      { label: `find:${d.key}`, phase: 'Find', schema: FINDING_SCHEMA, model: 'opus' },
    ).then(r => r.findings.map(f => ({ ...f, dimension: d.key }))),
  ),
)
const failedFinders = DIMENSIONS.filter((_, i) => !found[i]).map(d => d.key)
if (failedFinders.length) log(`WARNING: finders failed/skipped: ${failedFinders.join(', ')} — their dimension is UNCOVERED`)

// Dedup by file + overlapping line + similar title (plain code, needs all findings).
const all = found.filter(Boolean).flat()
const deduped = []
for (const f of all) {
  const dup = deduped.find(
    g => g.file === f.file && g.line.split('-')[0] === f.line.split('-')[0],
  )
  if (dup) {
    dup.also_flagged_by = (dup.also_flagged_by || []).concat(f.dimension)
  } else {
    deduped.push(f)
  }
}
log(`${all.length} raw findings -> ${deduped.length} after dedup`)

// ---------------------------------------------------------------------------
// Phase 3: Verify — two independent skeptics per finding.
// Kill if both refute; CONFIRMED if both uphold; PLAUSIBLE if split.
// ---------------------------------------------------------------------------
phase('Verify')
const LENSES = ['correctness (is the claimed behaviour actually what the code does — re-derive it from source)', 'context (is it intentional, guarded elsewhere, unreachable, or already covered by a test/invariant)']
const verified = await parallel(
  deduped.map(f => () =>
    parallel(
      LENSES.map(lens => () =>
        agent(
          `${CONTEXT}\n\nA reviewer claims this bug in Gradus. Your job is to REFUTE it if you can. Lens: ${lens}.\n\nFINDING:\n${JSON.stringify(f, null, 2)}\n\nRead ${f.file} around line ${f.line} AND every code path the claim depends on. Default to refuted=true if you cannot positively confirm the defect from source.`,
          { label: `verify:${f.file.split('/').pop()}:${f.line}`, phase: 'Verify', schema: VERDICT_SCHEMA, model: 'opus' },
        ),
      ),
    ).then(votes => {
      const v = votes.filter(Boolean)
      const refutes = v.filter(x => x.refuted).length
      const verdict = refutes === 0 && v.length >= 2 ? 'CONFIRMED' : refutes >= v.length ? 'REFUTED' : 'PLAUSIBLE'
      return { ...f, verdict, verifier_notes: v.map(x => x.reasoning), severity_votes: v.map(x => x.severity_adjustment) }
    }),
  ),
)
const surviving = verified.filter(Boolean).filter(f => f.verdict !== 'REFUTED')
const confirmed = surviving.filter(f => f.verdict === 'CONFIRMED')
log(`${surviving.length} findings survive (${confirmed.length} CONFIRMED, ${surviving.length - confirmed.length} PLAUSIBLE); ${verified.filter(Boolean).length - surviving.length} refuted`)

// ---------------------------------------------------------------------------
// Phase 4: Synthesize — implementation plan (for an Opus-class executor) and
// a Gradus domain-skill draft. Independent inputs -> run in parallel.
// ---------------------------------------------------------------------------
phase('Synthesize')
const [plan, skill] = await parallel([
  () =>
    agent(
      `${CONTEXT}\n\nYou are writing a DETAILED IMPLEMENTATION PLAN to fix the verified findings below. The plan will be executed by a LESS CAPABLE model working task-by-task with no memory of this audit, so it must be fully self-contained: never say "fix the issue" — spell out the exact edit intent, the file:line anchors, and the acceptance check.\n\nVERIFIED FINDINGS (CONFIRMED = both skeptics upheld; PLAUSIBLE = one did — mark these "verify first, then fix"):\n${JSON.stringify(surviving, null, 2)}\n\nPLAN REQUIREMENTS:\n- Markdown document. Title, one-paragraph scope summary, then a findings-to-phases mapping table (ID, title, severity, verdict, phase).\n- Order phases by: (1) critical/high correctness bugs, (2) medium bugs, (3) duplication/anti-pattern cleanups, (4) test-gap fills. Within a phase, order so no task depends on a later one; note any task that MUST land before another.\n- Each task follows this repo's mandatory workflow and says so explicitly:\n  1. write failing test (name the test file to create/extend, the exact TEST_CASE name, and what it asserts — for PLAUSIBLE findings this doubles as the verify step: if the test passes on current code, mark the finding refuted, skip the fix, keep the test)\n  2. implement fix (exact files, functions, before/after behaviour)\n  3. build: "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target gradus_tests\n  4. run: build/windows-x64-release/bin/Release/gradus_tests.exe 2>&1 | tail -5 (plus dsp_processors_tests if the shared engine changed, plus ruinae_tests + the cross-plugin byte-golden test if arp_params_common.h or shared save format is touched)\n  5. pluginval + clang-tidy gate before commit\n- Flag every task that touches dsp/include/krate/dsp/processors/ or plugins/shared/ as CROSS-PLUGIN IMPACT: Ruinae consumes the same code; its tests must pass too and saved-state compatibility must not break.\n- End with a completion checklist mirroring the repo's Completion Honesty rules (verify each fix against the original finding with file:line evidence, no relaxed thresholds).\nReturn the full markdown.`,
      { label: 'synthesize:plan', phase: 'Synthesize', schema: DOC_SCHEMA, model: 'opus' },
    ),
  () =>
    agent(
      `You are drafting a Claude Code domain skill for the Gradus plugin in the repo at f:/projects/iterum, matching the EXACT format of the existing sibling skills. READ FIRST: .claude/skills/innexus/SKILL.md and .claude/skills/membrum/SKILL.md (format/tone/frontmatter templates), plugins/gradus/CLAUDE.md, and spot-check any file you cite. Then use these subsystem maps:\n${MAP_DIGEST}\n\nWrite SKILL.md content with:\n- YAML frontmatter: name: gradus; a description in the same style ("Domain knowledge for the Gradus step-arpeggiator plugin — ... Use when working on Gradus, arpeggiators, ... especially outside plugins/gradus/ where the leaf CLAUDE.md won't auto-load."); allowed-tools: Read, Glob, Grep\n- Sections mirroring the siblings: an intro paragraph, "Signal path" (here: MIDI/event flow — note input -> ArpeggiatorCore -> MidiNoteDelay echoes -> event output + AuditionVoice), "Load-bearing constraints" (shared param IDs 3000-3372 with Ruinae, unified save prefix + INTENTIONALLY local load path with divergent clamps, byte-golden guard, fixed event buffer capacities, prev-value setter caching), and "Deep tooling" (links to plugins/gradus/CLAUDE.md, the engine headers, arp_params_common.h, and .claude/workflows/gradus-audit.js for full audits).\n- Every constraint must be TRUE per the code you read — no invention. Keep it as tight as the siblings (~60 lines).\nReturn the full markdown.`,
      { label: 'synthesize:skill', phase: 'Synthesize', schema: DOC_SCHEMA, model: 'opus' },
    ),
])

return {
  stats: {
    maps: goodMaps.length,
    finders_failed: failedFinders,
    raw_findings: all.length,
    deduped: deduped.length,
    confirmed: confirmed.length,
    plausible: surviving.length - confirmed.length,
    refuted: verified.filter(Boolean).length - surviving.length,
  },
  findings: surviving,
  plan_markdown: plan ? plan.markdown : null,
  skill_markdown: skill ? skill.markdown : null,
}
