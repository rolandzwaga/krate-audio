export const meta = {
  name: 'release-readiness',
  description: 'Per-plugin release gate: build -> tests -> pluginval (strictness 5) -> version.json/CHANGELOG sync, producing a green/red readiness matrix.',
  whenToUse: 'Before tagging/shipping one or more plugins. Pass the plugin list (and optionally target versions) via args, e.g. args: {plugins:["membrum"],versions:{membrum:"0.12.0"}}. With no args it checks every plugin. Runs real builds + pluginval — opt in explicitly.',
  phases: [
    { title: 'Check', detail: 'per plugin: build -> tests -> pluginval -> version/changelog sync' },
    { title: 'Matrix', detail: 'assemble the green/red readiness matrix' },
  ],
}

// ---------------------------------------------------------------------------
// Plugin -> canonical identifiers (see CLAUDE.md).
// ---------------------------------------------------------------------------
const PLUGIN_MAP = {
  iterum:   { testTarget: 'plugin_tests',   bundle: 'Iterum.vst3' },
  disrumpo: { testTarget: 'disrumpo_tests', bundle: 'Disrumpo.vst3' },
  ruinae:   { testTarget: 'ruinae_tests',   bundle: 'Ruinae.vst3' },
  innexus:  { testTarget: 'innexus_tests',  bundle: 'Innexus.vst3' },
  gradus:   { testTarget: 'gradus_tests',   bundle: 'Gradus.vst3' },
  membrum:  { testTarget: 'membrum_tests',  bundle: 'Membrum.vst3' },
}

const BUILD_DIR = 'build/windows-x64-release'
const CMAKE = '"/c/Program Files/CMake/bin/cmake.exe"'

// Which plugins + which expected versions (args optional).
const requested =
  args && Array.isArray(args.plugins) && args.plugins.length
    ? args.plugins.filter((p) => PLUGIN_MAP[p])
    : Object.keys(PLUGIN_MAP)
const wantVersions = (args && args.versions) || {}

const RESULT_SCHEMA = {
  type: 'object',
  properties: {
    plugin: { type: 'string' },
    version: { type: 'string', description: 'version read from plugins/<p>/version.json' },
    built: { type: 'boolean' },
    warnings: { type: 'integer' },
    testsPassed: { type: 'boolean' },
    testSummary: { type: 'string' },
    pluginvalPassed: { type: 'boolean' },
    pluginvalDetail: { type: 'string' },
    changelogHasVersion: { type: 'boolean', description: 'plugins/<p>/CHANGELOG.md has a "## [version]" section' },
    versionMatches: { type: 'boolean', description: 'version.json matches the requested version (true if none requested)' },
    notes: { type: 'string' },
  },
  required: ['plugin', 'version', 'built', 'warnings', 'testsPassed', 'pluginvalPassed', 'changelogHasVersion', 'versionMatches'],
  additionalProperties: false,
}

// ---------------------------------------------------------------------------
// Phase 1 — Check each plugin end to end (one agent per plugin, in parallel).
// ---------------------------------------------------------------------------
phase('Check')
log(`Checking release readiness for: ${requested.join(', ')}`)

const results = await parallel(
  requested.map((p) => {
    const m = PLUGIN_MAP[p]
    const want = wantVersions[p]
    return () =>
      agent(
        `Assess release readiness for the ${p} plugin. Do ALL of the following and report the structured status; do NOT edit any files.

1. Read plugins/${p}/version.json and report its version. ${want ? `The requested release version is ${want} — set versionMatches accordingly.` : 'No specific version requested — set versionMatches true.'}
2. Build: ${CMAKE} --build ${BUILD_DIR} --config Release --target ${m.testTarget}
   Report compiler errors and the WARNING count (zero required).
3. Run the ${m.testTarget} executable; report the Catch2 summary line (use tail, do not grep repeatedly).
4. Build the plugin bundle, then run pluginval strictness 5:
   tools/pluginval.exe --strictness-level 5 --validate "${BUILD_DIR}/VST3/Release/${m.bundle}"
   Report pass/fail and the key line on failure. (If the post-build copy to Program Files failed, the bundle in the build dir is still valid — use that path.)
5. Check plugins/${p}/CHANGELOG.md for a "## [<version>]" section matching version.json's version; set changelogHasVersion.

Capture slow command output to a log file on the first run and read the log; do not re-run to grep.`,
        { label: `release:${p}`, phase: 'Check', schema: RESULT_SCHEMA, agentType: 'build-test-runner' }
      )
  })
)

// ---------------------------------------------------------------------------
// Phase 2 — Matrix.
// ---------------------------------------------------------------------------
phase('Matrix')
const rows = results.filter(Boolean).map((r) => {
  const gates = {
    build: r.built && r.warnings === 0,
    tests: r.testsPassed,
    pluginval: r.pluginvalPassed,
    changelog: r.changelogHasVersion,
    version: r.versionMatches,
  }
  const ready = Object.values(gates).every(Boolean)
  return { plugin: r.plugin, version: r.version, ready, gates, notes: r.notes || '' }
})

const notReady = rows.filter((r) => !r.ready)
for (const r of rows) {
  const g = r.gates
  log(
    `${r.ready ? 'READY ' : 'BLOCK '} ${r.plugin} ${r.version}  ` +
      `build:${g.build ? 'ok' : 'X'} tests:${g.tests ? 'ok' : 'X'} ` +
      `pluginval:${g.pluginval ? 'ok' : 'X'} changelog:${g.changelog ? 'ok' : 'X'} version:${g.version ? 'ok' : 'X'}`
  )
}

return {
  ready: notReady.length === 0,
  readyCount: rows.length - notReady.length,
  total: rows.length,
  blocked: notReady.map((r) => r.plugin),
  matrix: rows,
}
