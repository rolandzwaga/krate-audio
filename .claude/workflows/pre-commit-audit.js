export const meta = {
  name: 'pre-commit-audit',
  description: 'Pre-commit go/no-go: discover which plugins changed, then per plugin fan out build+test, clang-tidy, and a DSP/VST code review, and synthesize a single go/no-go verdict.',
  whenToUse: 'Before committing a substantive change that touches one or more plugins or the DSP library. Gives a build/tidy/review matrix and a blocking-issues list so you commit green, not hopeful. Spawns build+test agents (real builds) — opt in explicitly.',
  phases: [
    { title: 'Scope', detail: 'git diff -> which plugins / dsp changed' },
    { title: 'Audit', detail: 'per changed target: build+test, clang-tidy, code review (in parallel)' },
    { title: 'Verdict', detail: 'synthesize go/no-go + blocking issues' },
  ],
}

// ---------------------------------------------------------------------------
// Plugin -> canonical build/test/pluginval identifiers (see CLAUDE.md).
// ---------------------------------------------------------------------------
const PLUGIN_MAP = {
  iterum:   { testTarget: 'plugin_tests',   bundle: 'Iterum.vst3',   srcGlob: 'plugins/iterum/src' },
  disrumpo: { testTarget: 'disrumpo_tests', bundle: 'Disrumpo.vst3', srcGlob: 'plugins/disrumpo/src' },
  ruinae:   { testTarget: 'ruinae_tests',   bundle: 'Ruinae.vst3',   srcGlob: 'plugins/ruinae/src' },
  innexus:  { testTarget: 'innexus_tests',  bundle: 'Innexus.vst3',  srcGlob: 'plugins/innexus/src' },
  gradus:   { testTarget: 'gradus_tests',   bundle: 'Gradus.vst3',   srcGlob: 'plugins/gradus/src' },
  membrum:  { testTarget: 'membrum_tests',  bundle: 'Membrum.vst3',  srcGlob: 'plugins/membrum/src' },
}

const BUILD_DIR = 'build/windows-x64-release'

// ---------------------------------------------------------------------------
// Schemas (tool-executing agents return status objects, not read-only findings)
// ---------------------------------------------------------------------------
const CHANGED_SCHEMA = {
  type: 'object',
  properties: {
    plugins: { type: 'array', items: { type: 'string' }, description: 'plugin dir names changed under plugins/<name>/src' },
    dspChanged: { type: 'boolean', description: 'any file under dsp/ changed' },
    sharedChanged: { type: 'boolean', description: 'any file under plugins/shared/ changed' },
    files: { type: 'array', items: { type: 'string' } },
  },
  required: ['plugins', 'dspChanged', 'sharedChanged', 'files'],
  additionalProperties: false,
}

const BUILD_SCHEMA = {
  type: 'object',
  properties: {
    target: { type: 'string' },
    built: { type: 'boolean', description: 'compiled with zero errors' },
    warnings: { type: 'integer', description: 'compiler warning count (must be 0 to pass)' },
    testsPassed: { type: 'boolean' },
    testSummary: { type: 'string', description: 'the Catch2 summary line' },
    detail: { type: 'string', description: 'first errors/warnings if any' },
  },
  required: ['target', 'built', 'warnings', 'testsPassed', 'testSummary'],
  additionalProperties: false,
}

const TIDY_SCHEMA = {
  type: 'object',
  properties: {
    target: { type: 'string' },
    clean: { type: 'boolean' },
    warningCount: { type: 'integer' },
    topIssues: { type: 'array', items: { type: 'string' } },
  },
  required: ['target', 'clean', 'warningCount', 'topIssues'],
  additionalProperties: false,
}

const REVIEW_SCHEMA = {
  type: 'object',
  properties: {
    scope: { type: 'string' },
    blocking: { type: 'boolean', description: 'true if any CRITICAL/blocking finding' },
    findings: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          severity: { type: 'string', enum: ['critical', 'warning', 'suggestion'] },
          file: { type: 'string' },
          line: { type: 'integer' },
          summary: { type: 'string' },
        },
        required: ['severity', 'file', 'summary'],
        additionalProperties: false,
      },
    },
  },
  required: ['scope', 'blocking', 'findings'],
  additionalProperties: false,
}

// ---------------------------------------------------------------------------
// Phase 1 — Scope: discover changed plugins/dsp from the working tree.
// ---------------------------------------------------------------------------
phase('Scope')
const changed = await agent(
  `Determine what changed in this repo relative to origin/main (or the merge-base if origin/main is unavailable).
Run: git diff --name-only origin/main...HEAD ; and also git status --porcelain to include uncommitted work.
Classify: which of these plugin dirs have changes under plugins/<name>/src: ${Object.keys(PLUGIN_MAP).join(', ')}.
Set dspChanged if any dsp/ path changed, sharedChanged if any plugins/shared/ path changed.
Return the structured result. Do not edit anything.`,
  { label: 'scope:git-diff', phase: 'Scope', schema: CHANGED_SCHEMA, agentType: 'build-test-runner' }
)

if (!changed || (changed.plugins.length === 0 && !changed.dspChanged && !changed.sharedChanged)) {
  log('No plugin/dsp/shared source changes detected — nothing to audit.')
  return { verdict: 'go', reason: 'no source changes', changed: changed || null }
}

// Targets to build+test: each changed plugin, plus dsp layers / shared if touched.
const auditTargets = []
for (const p of changed.plugins) if (PLUGIN_MAP[p]) auditTargets.push({ kind: 'plugin', name: p, ...PLUGIN_MAP[p] })
if (changed.dspChanged) {
  for (const layer of ['core', 'primitives', 'processors', 'systems', 'effects']) {
    auditTargets.push({ kind: 'dsp', name: `dsp_${layer}`, testTarget: `dsp_${layer}_tests` })
  }
}
if (changed.sharedChanged) auditTargets.push({ kind: 'shared', name: 'shared', testTarget: 'shared_tests' })

log(`Auditing: ${auditTargets.map((t) => t.name).join(', ')}`)

// ---------------------------------------------------------------------------
// Phase 2 — Audit: for each target run build+test, clang-tidy, and (plugins/dsp
// only) a code review, all concurrently. Pipeline so a slow build doesn't block
// another target's review.
// ---------------------------------------------------------------------------
phase('Audit')
const CMAKE = '"/c/Program Files/CMake/bin/cmake.exe"'

const auditResults = await parallel(
  auditTargets.map((t) => async () => {
    const [build, tidy, review] = await parallel([
      () =>
        agent(
          `Build and test the "${t.testTarget}" target for this monorepo.
Build: ${CMAKE} --build ${BUILD_DIR} --config Release --target ${t.testTarget}
Report compiler errors AND warnings (the project requires ZERO warnings).
Then run the test executable and report the Catch2 summary line (do NOT grep; use tail).
Return the structured status.`,
          { label: `build:${t.name}`, phase: 'Audit', schema: BUILD_SCHEMA, agentType: 'build-test-runner' }
        ),
      () =>
        agent(
          `Run clang-tidy for the "${t.name}" area using ./tools/run-clang-tidy.ps1 against build/windows-ninja
(capture output to a log file on the FIRST run, then read the log — do not re-run to grep).
Report whether it is clean and the total warning count with the top issues.`,
          { label: `tidy:${t.name}`, phase: 'Audit', schema: TIDY_SCHEMA, agentType: 'build-test-runner' }
        ),
      () =>
        t.kind === 'shared'
          ? Promise.resolve({ scope: t.name, blocking: false, findings: [] })
          : agent(
              `Code-review the CHANGED lines in ${t.srcGlob || 'dsp/'} for this branch (git diff origin/main...HEAD).
Apply the project's DSP/VST review criteria: real-time safety (no alloc/lock/throw/IO on the audio thread),
layer-dependency direction, ODR, NaN/denormal safety, VST3 processor/controller separation, and parameter
normalization. This is READ-ONLY — do not edit. Flag only real defects in the diff; set blocking=true if any
is CRITICAL.`,
              { label: `review:${t.name}`, phase: 'Audit', schema: REVIEW_SCHEMA, agentType: 'general-purpose' }
            ),
    ])
    return { target: t.name, kind: t.kind, build, tidy, review }
  })
)

// ---------------------------------------------------------------------------
// Phase 3 — Verdict.
// ---------------------------------------------------------------------------
phase('Verdict')
const problems = []
for (const r of auditResults.filter(Boolean)) {
  if (!r.build || !r.build.built) problems.push(`${r.target}: build FAILED`)
  else if (r.build.warnings > 0) problems.push(`${r.target}: ${r.build.warnings} compiler warning(s)`)
  if (r.build && !r.build.testsPassed) problems.push(`${r.target}: tests FAILED (${r.build.testSummary || '?'})`)
  if (r.tidy && !r.tidy.clean) problems.push(`${r.target}: clang-tidy ${r.tidy.warningCount} warning(s)`)
  if (r.review && r.review.blocking) {
    const crit = (r.review.findings || []).filter((f) => f.severity === 'critical')
    problems.push(`${r.target}: ${crit.length} blocking review finding(s)`) // detail in results
  }
}

const verdict = problems.length === 0 ? 'go' : 'no-go'
log(`Verdict: ${verdict.toUpperCase()}${problems.length ? ' — ' + problems.length + ' blocker(s)' : ''}`)
return { verdict, problems, changed, results: auditResults.filter(Boolean) }
