// EDITING THIS SCRIPT WHILE A RUN CAN STILL BE RESUMED (rule inherited from profundum-phase.js, 2026-10-13):
// Resume (resumeFromRunId) replays agent() calls by a hash CHAINED over every call in spawn
// order. Changing the TEXT of any prompt that an earlier call in the run already used — a gate's
// rules, a fixer's rules, a label, CONTEXT, APP_DIR — re-runs that call AND every call after it
// (implementers included; they rewrite code). Skipping a call breaks the chain the same way.
// So, for a phase whose build is in flight or may be resumed:
//   1. never change a prompt's text; add an ARGUMENT that defaults to the old behaviour;
//   2. after every resume, read the newest agent-*.meta.json label once; if it is not the call
//      you meant to resume from, TaskStop immediately;
//   3. a task named in args.retry_tasks needs its rewritten tasks.md section ON DISK first;
//   4. a user ruling is recorded in the spec's FR/SC ROW FIRST, then Clarifications, then tasks.
// Pure comments and new, unused arguments are safe to add at any time.
//
// Cloned from profundum-phase.js (2026-10-29). Differences, all because Krate Manager is a
// Tauri 2 desktop app (Rust + TypeScript) and not a KrateDSP / VST3 component:
//   - two extra stages before the per-phase stages: "roadmap" drafts specs/Krate-manager-roadmap.md
//     from the research (there was no roadmap), "roadmap-encode" writes the user's answers into it;
//   - gates run the app's own toolchain (cargo fmt / clippy / test, tsc, vitest) instead of Catch2
//     executables; plugin-side changes (release pipeline, CMake packaging, tools/*.js) get their own
//     gate lane, and C++ test runs keep the run-gate.js + header-consumers.js rules;
//   - macOS and Linux behaviour is proven by the manager's CI workflow, which only the user can
//     trigger (a push), so those rows close as PENDING-CI, never as a local pass;
//   - the legacy replay arguments of profundum-phase.js (legacy_gates, serial_gates,
//     sharded_v1_gates) are left out: this workflow has no earlier runs to replay.
export const meta = {
  name: 'krate-manager-phase',
  description: 'Speckit-style pipeline for Krate Manager, the cross-platform Tauri 2 plugin install/update manager: roadmap (draft→challenge→question scan), roadmap-encode, then per roadmap phase specify (spec→challenge→clarify-scan), plan (encode grill answers→plan→challenge→tasks), build (implement→gate→comply). Adversarial agents check every artifact; the user is grilled between stages. Nothing commits.',
  whenToUse: 'When building Krate Manager (the plugin install/update/uninstall manager researched in specs/krate-manager/). Flow: (1) run args {stage: "roadmap"} — drafts specs/Krate-manager-roadmap.md, returns questions; (2) the MAIN LOOP grills the user (grill-me skill); (3) run {stage: "roadmap-encode", clarifications: {...}}; (4) user reviews the roadmap; then per phase N: (5) {phase: N, stage: "specify"} → grill → (6) {phase: N, stage: "plan", clarifications: {...}} → user review → (7) {phase: N, stage: "build"}. Artifacts land in specs/krate-manager-phase<N>/. Nothing commits, nothing pushes.',
  phases: [
    { title: 'Roadmap', detail: 'specs/Krate-manager-roadmap.md from the research report, probes and repo reality', model: 'opus' },
    { title: 'Challenge Roadmap', detail: 'report fidelity, phase sequencing, repo reality skeptics + revision', model: 'opus' },
    { title: 'Roadmap Questions', detail: 'decisions the roadmap must not take alone, for the user grill', model: 'opus' },
    { title: 'Encode Roadmap Answers', detail: 'write the grill answers into the roadmap body, gate on unresolved', model: 'opus' },
    { title: 'Specify', detail: 'spec.md with FR/SC from roadmap phase', model: 'opus' },
    { title: 'Challenge Spec', detail: 'parallel skeptics + bounded revision loop', model: 'opus' },
    { title: 'Clarify Scan', detail: 'speckit.clarify-style ambiguity questionnaire for the user grill', model: 'opus' },
    { title: 'Encode Clarifications', detail: 'integrate grill answers into spec, gate on unresolved', model: 'opus' },
    { title: 'Plan', detail: 'plan.md, crate/API choices verified against real docs and the repo', model: 'opus' },
    { title: 'Challenge Plan', detail: 'safety/security, cross-platform, reuse-reality skeptics + revision', model: 'opus' },
    { title: 'Tasks', detail: 'dependency-ordered tasks.md with parallel groups', model: 'opus' },
    { title: 'Dispatch', detail: 'parse tasks.md into executable groups', model: 'sonnet' },
    { title: 'Implement', detail: 'one agent per task, TDD, disjoint files', model: 'opus' },
    { title: 'Build+Test', detail: 'app gate + pipeline gate (sonnet reporter), bounded fix loop (opus fixer)', model: 'opus' },
    { title: 'Comply', detail: 'adversarial FR/SC verification, real output only', model: 'opus' },
    { title: 'Report', detail: 'compliance.md + honest gap list (sonnet); roadmap marked done if COMPLETE (haiku)', model: 'sonnet' },
    { title: 'Cleanup', detail: 'node tools/clean-test-output.js --apply (haiku)', model: 'haiku' },
  ],
}

// ---------------------------------------------------------------------------
// Args
// ---------------------------------------------------------------------------
// Tolerate args arriving as a JSON string (some launch paths stringify).
let A = args
if (typeof A === 'string') {
  try { A = JSON.parse(A) } catch (e) { throw new Error(`args arrived as unparseable string: ${A}`) }
}
A = A || {}
const STAGES = ['roadmap', 'roadmap-encode', 'specify', 'plan', 'build']
const stage = A.stage || 'roadmap'
if (!STAGES.includes(stage)) throw new Error(`args.stage must be one of ${STAGES.join(', ')}, got: ${JSON.stringify(stage)}`)
const phaseNum = A.phase
const PHASE_STAGE = stage === 'specify' || stage === 'plan' || stage === 'build'
if (PHASE_STAGE && !(Number.isInteger(phaseNum) && phaseNum >= 1 && phaseNum <= 30)) {
  throw new Error(`stage "${stage}" needs args.phase (an integer roadmap phase number), got: ${JSON.stringify(phaseNum)}. Usage: {phase: 1, stage: "specify"|"plan"|"build", clarifications?: {...}}`)
}
// args.unblock: task ids whose cached status "blocked" has been RULED ON by the user (the decision is recorded in
// spec/plan/tasks). On a resume those results are treated as done so the stage continues past the stop.
const unblocked = Array.isArray(A.unblock) ? A.unblock.map(String) : []
// args.retry_tasks: task ids whose tasks.md section was REWRITTEN after a user ruling. The task prompt gains a
// marker so the cached (blocked) result is bypassed and the task re-executes against the new section.
const retryTasks = Array.isArray(A.retry_tasks) ? A.retry_tasks.map(String) : []
// args.retry_epochs / args.gate_epochs: {"Group 3": 5} re-measures ONE group's gate on a resume after a main-loop
// fix (gate_epochs: the gate only; retry_epochs: also the retry_tasks implementers of that group).
const groupKey = (name) => String(name).split(/\s+[—-]\s+/)[0]
const retryEpoch = (name) => (A.retry_epochs && A.retry_epochs[groupKey(name)]) || 1
const gateEpoch = (name) => (A.gate_epochs && A.gate_epochs[groupKey(name)]) || retryEpoch(name)

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------
const ROADMAP = 'specs/Krate-manager-roadmap.md'
const RESEARCH_REPORT = 'specs/krate-manager/reports/Krate Audio plugin manager design.md'
const RESEARCH_NOTES = 'specs/krate-manager/research_notes/Krate Audio plugin manager design/'
const PROBE_RESULTS = 'specs/krate-manager/probes/results.md'
// Proposed home of the app inside the monorepo. The roadmap stage asks the user to confirm it (question
// "app location"); if the answer differs, change this constant BEFORE the first phase's specify run.
const APP_DIR = 'apps/krate-manager'
// Top-level specs/<slug>/spec.md, the layout tools/gen-specs-index.js discovers.
const SLUG = PHASE_STAGE ? `krate-manager-phase${phaseNum}` : null
const DIR = SLUG ? `specs/${SLUG}` : null
const SPEC = DIR && `${DIR}/spec.md`
const PLAN = DIR && `${DIR}/plan.md`
const TASKS = DIR && `${DIR}/tasks.md`
const COMPLIANCE = DIR && `${DIR}/compliance.md`

// ---------------------------------------------------------------------------
// Shared context handed to every authoring / judging agent.
// ---------------------------------------------------------------------------
const CONTEXT = `
You are working on KRATE MANAGER, a new cross-platform (Windows, macOS, Linux) desktop application in the Krate Audio
monorepo at f:/projects/iterum. It shows which Krate Audio plugins are installed, whether updates exist, and downloads,
installs, updates and uninstalls them, and updates itself. The plugins are free and open source: there are no accounts,
no license keys, and NO code-signing certificate will EVER be bought (Apple or Windows). Treat any design that needs one
as a defect.
${PHASE_STAGE ? `The authoritative roadmap is ${ROADMAP} — READ IT FIRST (the whole file for context, then focus on Phase ${phaseNum}, spec slug "${SLUG}"). Phase artifacts live in ${DIR}/ (spec.md, plan.md, tasks.md).` : `There is no roadmap yet; this stage writes ${ROADMAP}.`}

THE RESEARCH (read what your role needs; cite it by file and section):
- ${RESEARCH_REPORT}: the design recommendation (Tauri 2; the manager places files itself; feed v2; CI changes;
  consequences of never signing). Its "open questions" table is partly answered by the probes below.
- ${RESEARCH_NOTES}: nine research notes with sources (framework comparison, Tauri feasibility, prior art, per-OS
  install mechanics, unsigned distribution, self-update, feed and CI, Windows per-user VST3 DAW support, Tauri plist
  quarantine and macOS runners).
- ${PROBE_RESULTS}: MEASURED answers (2026-10-29), which override any research claim they contradict:
  Windows: the per-user VST3 folder cannot be the default (Ableton Live 12, FL Studio, Renoise ignore it), so the
  default is C:\\Program Files\\Common Files\\VST3 behind one UAC prompt; files the manager downloads itself carry no
  Mark-of-the-Web. Signing: tauri signer keys ARE minisign keys (one key can sign manager updates and plugin archives).
  macOS (Sequoia 15.7.9 and Tahoe 26.6.2): non-browser downloads carry no quarantine; "sudo installer -pkg" installs a
  quarantined unsigned pkg with an unquarantined payload; the shipped plug-ins are already ad-hoc signed; ditto keeps
  quarantine from a quarantined zip; the Tauri Info.plist has no LSFileQuarantineEnabled; a quarantined ad-hoc app is
  blocked at launch, the unquarantined copy launches. NOT tested: the "Open Anyway" UI, and an end-to-end updater
  self-replace.

REPO FACTS THE MANAGER BUILDS ON (verify each against the file before relying on it; quote file:line):
- Feed: https://rolandzwaga.github.io/krate-audio/versions.json (schema_version 1: plugins.<Name>.{version,
  download_url, release_notes}). Nine shipped plugins parse it with plugins/shared/src/update/update_checker.cpp, which
  needs only plugins.<Name>.version and ignores unknown keys: every feed change MUST stay additive so those keep working.
- Release pipeline: .github/workflows/release.yml (one plugin per manual run, tag "<plugin>/vX.Y.Z", assets
  <Name>-<ver>-Windows-x64.exe (Inno Setup, plugins/<p>/installers/windows/setup.iss), <Name>-<ver>-macOS.pkg
  (pkgbuild, unsigned), <Name>-<ver>-Linux-x64.tar.gz; it also generates versions.json and deploys GitHub Pages).
- Each plugin's version.json is the single version source (cmake/KratePlugin.cmake krate_plugin_read_version); the
  optional preset_subdir field (Membrum: "Kits") decides the preset folder. moduleinfo.json in every bundle carries the
  version.json version since commit 294a26e7 (gated by tools/check-bundle.js); bundles released before a plugin's next
  release still say "0.8.0" at top level, so detection must fall back to the class Version.
- Install locations today: Windows C:\\Program Files\\Common Files\\VST3\\Krate Audio\\<Plugin>\\<Plugin>.vst3, presets
  C:\\ProgramData\\Krate Audio\\<Plugin>\\; macOS /Library/Audio/Plug-Ins/{VST3,Components}, presets
  /Library/Application Support/Krate Audio/<Plugin>/; Linux: the user extracts the tarball to ~/.vst3 or /usr/lib/vst3.
- Helper scripts and tooling in this repo are Node.js, never Python. The app itself is Rust (src-tauri) + TypeScript.
- The proposed app directory is ${APP_DIR}/ (confirmed or changed by the roadmap grill).

CROSS-CUTTING CONSTRAINTS (violations are defects at every stage):
- NEVER TOUCH THE REAL SYSTEM: this dev box has the user's real plugins installed. Every filesystem root (plug-in
  folders, preset folders, the manager's state dir), the feed URL and the elevation mechanism is INJECTED; tests use
  temp dirs and a local fixture HTTP server. No test, gate or agent writes to a real VST3/Components/preset folder,
  ProgramData, the registry, or runs the elevated helper or an installer for real. No test calls github.com or
  api.github.com; an opt-in live-network test is tagged/ignored by default and never runs in a gate.
- Security: every downloaded artifact is verified (sha256 + minisign signature) BEFORE it is extracted or executed;
  archive extraction rejects absolute paths, "..", and symlinks escaping the target (zip-slip); the elevated helper
  accepts only a fixed, validated operation set (no shell strings from the UI); the webview gets the narrowest Tauri
  capability set and no remote content. No private key is ever generated into, or committed to, the repo: tests make a
  throwaway key in a temp dir; the real key is a CI secret the user creates.
- Unsigned reality: no step may require a certificate; macOS builds are ad-hoc signed ("signingIdentity": "-") in CI;
  the manager never adds quarantine to what it writes; first-launch instructions are a deliverable, not an afterthought.
- Cross-platform: every OS-specific path goes through one abstraction with a Windows, macOS and Linux implementation,
  each with tests. MSVC/Windows-green proves nothing for the other two: macOS and Linux are proven by the manager's CI
  workflow on hosted runners, which only the user can trigger (a push). A row that needs macOS or Linux evidence closes
  as PENDING-CI with the exact job and assertion that will prove it, never as a local pass.
- Zero warnings: cargo clippy --all-targets -- -D warnings, cargo fmt --check, tsc --noEmit with strict, the linter the
  scaffold phase chooses. Never #[allow] or // @ts-ignore a warning away without a one-line reason the reviewer accepts.
- Tests run in parallel, never one serial process: cargo test (threads per test binary) and vitest (parallel files) as
  configured; never --test-threads=1 or --no-file-parallelism except for a documented timing case. C++ suites, if a
  phase touches plugin code, go through node tools/run-gate.js and node tools/header-consumers.js like every phase.
- The plugins' in-editor update checker and the existing per-plugin installers keep working throughout: the manager is
  additive until a roadmap phase explicitly retires something with the user's ruling.
- Naming: Rust per rustfmt/clippy conventions; TypeScript per the scaffold's lint config; Node tools follow tools/*.js.

DISCIPLINE: every claim about existing code must cite a file:line you actually read this session. Every claim about a
crate, npm package or Tauri API must cite its current docs page or source (version and URL). Never invent an API.
`

// Slim context for MECHANICAL roles (reporters, transcribers, gates): no roadmap or research read.
const CONTEXT_LITE = `
You are working in the Krate Audio monorepo at f:/projects/iterum, on KRATE MANAGER (a Tauri 2 desktop app in ${APP_DIR}/),
${PHASE_STAGE ? `roadmap Phase ${phaseNum} (spec slug "${SLUG}"). Phase artifacts: ${DIR}/ (spec.md, plan.md, tasks.md).` : `roadmap file ${ROADMAP}.`}
Do NOT read ${ROADMAP} or the research unless this prompt names them — everything you need is in this prompt and the
files it names. Never write to real plug-in folders, ProgramData or the registry; never run an installer or the elevated
helper for real; never call github.com.

DISCIPLINE: report only what you actually observed; quote real output verbatim, never paraphrase or invent.
`

// ---------------------------------------------------------------------------
// Model policy (same tiers as profundum-phase.js):
// - AUTHOR (opus): authoring, judgment, implementation, fixing, verification.
// - MECH (sonnet): transcription / reporting / gate roles, whose errors are loud. Never effort:'low' on the reporter.
// - TRIVIAL (haiku): mark-roadmap and cleanup.
// ---------------------------------------------------------------------------
const AUTHOR = 'opus'
const MECH = 'sonnet'
const TRIVIAL = 'haiku'
const run = (prompt, opts) => agent(prompt, { model: AUTHOR, ...opts })

// ---------------------------------------------------------------------------
// Schemas
// ---------------------------------------------------------------------------
const DOC_STATUS = {
  type: 'object',
  additionalProperties: false,
  required: ['path', 'summary', 'open_questions'],
  properties: {
    path: { type: 'string', description: 'repo-relative path of the file written' },
    summary: { type: 'string', description: 'dense summary of the document content' },
    open_questions: { type: 'array', items: { type: 'string' }, description: 'decisions deliberately left to the user (empty if none)' },
  },
}

const QUESTIONS_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['questions'],
  properties: {
    questions: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['id', 'area', 'question', 'why_it_matters', 'options', 'recommendation'],
        properties: {
          id: { type: 'string', description: 'Q1, Q2, ... — stable ids the grill answers key on' },
          area: { type: 'string', description: 'taxonomy bucket: functional-scope | behaviour-contract | data-and-state | non-functional | integration | edge-cases | terminology | security | distribution' },
          question: { type: 'string', description: 'one precise question; different answers must lead to materially different work' },
          why_it_matters: { type: 'string', description: 'which phase / FR / SC is ambiguous without the answer, and what could be built wrong' },
          options: { type: 'array', items: { type: 'string' }, description: '2-4 concrete candidate answers with their consequences, one line each' },
          recommendation: { type: 'string', description: 'the option you would pick and why (one sentence)' },
        },
      },
    },
  },
}

const ISSUES_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['issues'],
  properties: {
    issues: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['severity', 'section', 'problem', 'suggested_resolution'],
        properties: {
          severity: { type: 'string', enum: ['blocker', 'major', 'minor'] },
          section: { type: 'string', description: 'heading / phase / FR id / SC id the issue is in' },
          problem: { type: 'string', description: 'what is wrong, with evidence (file:line for code claims, URL for crate/API claims)' },
          suggested_resolution: { type: 'string', description: 'concrete edit that would resolve it' },
        },
      },
    },
  },
}

const RECHECK_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['resolved', 'unresolved'],
  properties: {
    resolved: { type: 'boolean', description: 'true if every blocker and major issue is now addressed in the document' },
    unresolved: { type: 'array', items: { type: 'string' }, description: 'issues still not addressed, quoting the document' },
  },
}

const TASKGROUPS_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['cmake_targets', 'groups'],
  properties: {
    cmake_targets: { type: 'array', items: { type: 'string' }, description: 'cmake targets this phase builds because it touches plugin / CMake code (e.g. Vorago, check-bundle consumers). EMPTY for a phase that touches only the app and Node tooling.' },
    groups: {
      type: 'array',
      description: 'executed strictly in order; tasks within a group run in parallel only if parallel=true',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['name', 'parallel', 'tasks'],
        properties: {
          name: { type: 'string' },
          parallel: { type: 'boolean', description: 'true ONLY if all tasks touch fully disjoint NEW files (no shared Cargo.toml, package.json, lib.rs/mod.rs, tauri.conf.json, capability or CMake edits)' },
          tasks: {
            type: 'array',
            items: {
              type: 'object',
              additionalProperties: false,
              required: ['id', 'title', 'instructions', 'files'],
              properties: {
                id: { type: 'string', description: 'task id from tasks.md, e.g. T003' },
                title: { type: 'string' },
                instructions: { type: 'string', description: 'ONE-to-THREE sentence orientation summary ONLY (goal + the test that proves it). Do NOT copy the task text — the executor reads its full section from tasks.md itself.' },
                files: { type: 'array', items: { type: 'string' }, description: 'every file this task creates or edits' },
              },
            },
          },
        },
      },
    },
  },
}

const IMPL_RESULT = {
  type: 'object',
  additionalProperties: false,
  required: ['task_id', 'status', 'files_written', 'notes'],
  properties: {
    task_id: { type: 'string' },
    status: { type: 'string', enum: ['done', 'blocked'] },
    files_written: { type: 'array', items: { type: 'string' } },
    notes: { type: 'string', description: 'deviations from the task instructions, or why blocked' },
  },
}

const BUILD_RESULT = {
  // 'failures' left OPTIONAL on purpose (profundum-phase.js 2026-08-04): requiring it made a green reporter flail.
  type: 'object',
  additionalProperties: false,
  required: ['build_ok', 'tests_ok', 'summary'],
  properties: {
    build_ok: { type: 'boolean', description: 'every build / compile / lint / format / type-check step of every lane is clean (zero warnings)' },
    tests_ok: { type: 'boolean' },
    summary: { type: 'string', description: 'the actual summary line(s) verbatim, one per lane step (cargo test result line, vitest summary, clippy/tsc outcome, GATE GREEN|RED)' },
    failures: { type: 'string', description: 'errors / warnings / failing test names + assertion output, verbatim excerpts; omit or empty if green' },
  },
}

const COMPLY_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['items'],
  properties: {
    items: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['id', 'verdict', 'evidence'],
        properties: {
          id: { type: 'string', description: 'FR-xxx, SC-xxx or CC-xxx' },
          verdict: { type: 'string', enum: ['pass', 'fail', 'partial', 'pending-ci'] },
          evidence: { type: 'string', description: 'file:line for FRs; verbatim test/measurement output with real numbers for SCs. "Implemented" without specifics is INVALID. pending-ci ONLY for macOS/Linux-only evidence: name the CI job and the assertion that will prove it, and the Windows-side evidence that exists.' },
        },
      },
    },
  },
}

const DOC_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['markdown'],
  properties: { markdown: { type: 'string' } },
}

// ---------------------------------------------------------------------------
// Helper: challenge → revise → recheck loop for an authored document.
// Bounded at 2 revision rounds; unresolved issues are surfaced, never hidden.
// ---------------------------------------------------------------------------
async function challengeAndRevise(docPath, docKind, skeptics, phaseTitle) {
  let round = 0
  let unresolvedTail = []
  while (round < 2) {
    round++
    const issueSets = await parallel(
      skeptics.map(s => () =>
        run(
          `${CONTEXT}\n\nYou are an adversarial reviewer of ${docPath} (${docKind}). Read it fully, plus every file it cites. YOUR LENS: ${s.lens}\n\nReport only defects you can substantiate. An empty issues list is a valid result — do not pad.`,
          { label: `challenge:${s.key}:r${round}`, phase: phaseTitle, schema: ISSUES_SCHEMA, model: s.model || AUTHOR },
        ).then(r => r.issues.map(i => ({ ...i, lens: s.key }))),
      ),
    )
    const issues = issueSets.filter(Boolean).flat()
    const serious = issues.filter(i => i.severity !== 'minor')
    log(`${docKind} challenge round ${round}: ${issues.length} issues (${serious.length} blocker/major)`)
    if (issues.length === 0) return { rounds: round, unresolved: [] }

    await run(
      `${CONTEXT}\n\nRevise ${docPath} to resolve the review issues below. Apply blockers and majors fully; apply minors where they improve the document; if you REJECT an issue, add a short "Review notes" entry at the bottom of the document stating why. Do not weaken requirements to dodge an issue — resolving by relaxing a threshold is forbidden unless the issue proves the threshold wrong against the ${docKind === 'roadmap' ? 'research and probe results' : 'roadmap'}. Edit the file in place.\n\nISSUES:\n${JSON.stringify(issues, null, 2)}`,
      { label: `revise:${docKind}:r${round}`, phase: phaseTitle },
    )

    const recheck = await run(
      `${CONTEXT}\n\nVerify that ${docPath} now addresses every blocker/major issue below (resolved in-text or explicitly rejected in "Review notes" with sound reasoning). Quote the document for each.\n\nISSUES:\n${JSON.stringify(serious, null, 2)}`,
      { label: `recheck:${docKind}:r${round}`, phase: phaseTitle, schema: RECHECK_SCHEMA, model: MECH },
    )
    if (!recheck || recheck.resolved) return { rounds: round, unresolved: [] }
    unresolvedTail = recheck.unresolved
    log(`${docKind} recheck: ${unresolvedTail.length} unresolved — ${round < 2 ? 'one more round' : 'giving up, surfacing to user'}`)
  }
  return { rounds: 2, unresolved: unresolvedTail }
}

// ===========================================================================
// STAGE: ROADMAP — draft → challenge → question scan.
// Ends by handing a questionnaire back to the main loop, which grills the user and re-enters at
// stage "roadmap-encode" with the answers.
// ===========================================================================
if (stage === 'roadmap') {
  phase('Roadmap')
  log(`Drafting ${ROADMAP} from the research and probe results...`)
  const roadmapStatus = await run(
    `${CONTEXT}\n\nWrite the Krate Manager roadmap to ${ROADMAP}. Before writing: read ${RESEARCH_REPORT} fully, ${PROBE_RESULTS} fully, and every research note you rely on; open every repo file the REPO FACTS list names and confirm it (quote file:line). Read specs/Profundum-roadmap.md for the STRUCTURE this repo's roadmaps use (not its content).\n\nROADMAP STRUCTURE (mirror specs/Profundum-roadmap.md):\n- Positioning: what Krate Manager is and is not (v1 scope: detect installed plugins and versions, install, update, uninstall, self-update; no accounts, no license keys, never signed). Name what stays as it is (the per-plugin installers, the in-editor update checker).\n- Architecture Overview: the processes (webview UI, Rust core, the minimal elevated helper), the per-OS install strategy the probes support, the feed and artifact model, the self-update path, where state lives. A short ASCII diagram.\n- Reuse Inventory: every existing repo piece the manager builds on or changes (feed generator in release.yml, update_checker.cpp parser, version.json + preset_subdir, check-bundle.js, the installer scripts), each with its file and what changes. Mark what is NEW.\n- Repository layout: the app at ${APP_DIR}/ (crate layout, frontend, the elevated helper), the new GitHub workflows, any new tools/*.js. Say this is a proposal the user confirms.\n- Phases, split into Part A (release pipeline and feed: changes on the plugin side, Node/YAML/CMake, testable on Windows) and Part B (the app). Every phase is independently buildable and testable on Windows, has a crisp exit criterion, lists its components, the measurable outcomes later specs will turn into SCs, and what it explicitly does NOT do. An early Part B phase is the scaffold plus the manager's own CI workflow on all three OSes, so every later phase has a cross-OS proof lane. Safety (injected roots, verify-before-extract, zip-slip, minimal helper) is designed into the phases, not bolted on at the end. Let the research decide the phase boundaries; do not invent features outside v1 scope.\n- Dependency Graph (ASCII).\n- Cross-Cutting Constraints (apply to every spec): restate the CONSTRAINTS above in roadmap form.\n- Open Questions: ONLY decisions the research could not settle and the user must take (for example the macOS install route: extract into ~/Library vs run the pkg through installer, the app location, how the manager versions itself, the minimum OS versions, how "Update all" and DAW-is-running are handled). Each with options and a recommendation.\nDo not write specs, plans or code. Return the doc status.`,
    { label: 'roadmap', phase: 'Roadmap', schema: DOC_STATUS },
  )
  if (!roadmapStatus) throw new Error('Roadmap agent failed — nothing to review')

  phase('Challenge Roadmap')
  const roadmapReview = await challengeAndRevise(ROADMAP, 'roadmap', [
    { key: 'fidelity', lens: `RESEARCH FIDELITY: compare the roadmap against ${RESEARCH_REPORT} and ${PROBE_RESULTS}. Flag recommendations it dropped without saying why, claims the probes contradict (the probes win), scope beyond v1 (detect, install, update, uninstall, self-update), anything that needs a code-signing certificate, and any reliance on api.github.com or /releases/latest in a monorepo.` },
    { key: 'sequencing', lens: 'PHASE SEQUENCING + TESTABILITY: every phase must be buildable and testable on this Windows box on its own, with an exit criterion a test can check; macOS/Linux proof must have a CI lane from an early phase; no phase may depend on a later one; safety work (injected filesystem roots, verify-before-extract, zip-slip, the minimal elevated helper, never touching the real system in tests) must land with the code it protects. Flag phases too large for one spec (more than ~25 tasks) or too thin to stand alone.' },
    { key: 'reality', model: MECH, lens: 'REPO REALITY: open every repo file the roadmap cites (release.yml, update_checker.cpp, KratePlugin.cmake, check-bundle.js, version.json files, installer scripts) and verify each claim and path. Flag wrong paths, invented fields, and any feed change that would break the schema_version 1 parser in the shipped plugins.' },
  ], 'Challenge Roadmap')

  phase('Roadmap Questions')
  const rq = await run(
    `${CONTEXT}\n\nYou are the QUESTION scanner for ${ROADMAP}. Read it fully, plus ${PROBE_RESULTS}. List the decisions the roadmap must not take on its own: where two reasonable engineers would build materially different products, or where only the user can decide (distribution, defaults users see, what happens to the existing per-plugin installers, the app location ${APP_DIR}/, how the manager versions and releases itself, minimum OS versions, the macOS install route, DAW-running and "Update all" behaviour, preset handling on uninstall).\n\nRULES:\n- Max 10 questions, ranked by impact. Only questions whose answer changes the phases or their outcomes; none the research, the probes or repo conventions already answer.\n- Each gets 2-4 concrete options with one-line consequences, plus your recommendation.\n- Also make sure the roadmap's "Open Questions" section lists the same questions (id, question, options), so the encode stage can gate on it.\n- If nothing is open, return zero questions and leave the file as it is.`,
    { label: 'roadmap-questions', phase: 'Roadmap Questions', schema: QUESTIONS_SCHEMA },
  )
  const questions = rq ? rq.questions : []
  log(`Roadmap questions: ${questions.length} for the user grill`)

  return {
    stage: 'roadmap',
    artifacts: { roadmap: ROADMAP },
    roadmap_summary: roadmapStatus.summary,
    open_questions: roadmapStatus.open_questions || [],
    unresolved_review_issues: roadmapReview.unresolved,
    questions,
    next: questions.length
      ? `MAIN LOOP: grill the user (grill-me skill) through these ${questions.length} questions, then run {stage: "roadmap-encode", clarifications: {Q1: "<answer>", ...}}. If the app-location answer differs from ${APP_DIR}, change APP_DIR in this script first.`
      : `No open questions. The user reviews ${ROADMAP}, then run {phase: 1, stage: "specify"}.`,
  }
}

// ===========================================================================
// STAGE: ROADMAP-ENCODE — write the grill answers into the roadmap body, gate.
// ===========================================================================
if (stage === 'roadmap-encode') {
  const clarifications = A.clarifications || null
  if (!clarifications) throw new Error('stage "roadmap-encode" needs args.clarifications ({Q1: "<answer>", ...})')
  phase('Encode Roadmap Answers')
  await run(
    `${CONTEXT_LITE}\n\nThe user was interviewed about the open questions in ${ROADMAP}. Encode their answers:\n1. Add/extend a "## Decisions" section with a dated log: one line per question, the question and the user's decision.\n2. UPDATE every phase, architecture paragraph, layout entry and constraint the answer affects, so the roadmap body itself carries the decision (a reader must never need the log). Record each decision where later specs will read it: the affected phase's components or outcomes.\n3. End each log line with the sections it changed, in brackets.\n4. Remove every answered question from "Open Questions"; leave unanswered ones.\n5. Do not weaken any outcome unless the answer explicitly decides that.\n\nANSWERS (id → decision):\n${JSON.stringify(clarifications, null, 2)}`,
    { label: 'encode-roadmap', phase: 'Encode Roadmap Answers', model: MECH },
  )
  const gate = await run(
    `${CONTEXT_LITE}\n\nRead ${ROADMAP}. resolved=true only if ALL pass:\n1. "Open Questions" holds no question the "## Decisions" log answers.\n2. No phase, architecture paragraph or constraint contradicts a "## Decisions" line.\n3. Every decision is carried in the body (the sections its log line cites exist and say it). Name each failure in unresolved.`,
    { label: 'roadmap-gate', phase: 'Encode Roadmap Answers', schema: RECHECK_SCHEMA, model: MECH },
  )
  return {
    stage: 'roadmap-encode',
    artifacts: { roadmap: ROADMAP },
    status: gate && !gate.resolved ? 'BLOCKED_UNRESOLVED' : 'ENCODED',
    unresolved: gate ? gate.unresolved : ['roadmap gate agent failed'],
    next: gate && !gate.resolved
      ? 'Roadmap still has open or contradicted decisions. Grill the user on these, then re-run stage "roadmap-encode".'
      : `The user reviews ${ROADMAP}, then run {phase: 1, stage: "specify"}.`,
  }
}

// ===========================================================================
// STAGE: SPECIFY — specify → challenge spec → clarify scan.
// ===========================================================================
if (stage === 'specify') {
  phase('Specify')
  log(`Authoring ${SPEC} from roadmap phase ${phaseNum}...`)
  const specStatus = await run(
    `${CONTEXT}\n\nWrite the feature specification for roadmap Phase ${phaseNum} to ${SPEC} (create ${DIR}/ if needed). Before writing: read ${ROADMAP} fully (its "## Decisions" log is binding); read every repo file the phase changes or builds on (quote real names and lines); for every crate, npm package or Tauri plugin the roadmap names for this phase, confirm the current version and the API it offers from its docs (cite URL + version).\n\nSPEC FORMAT (mirror the repo's speckit spec style):\n- Title, one-paragraph overview, explicit scope + non-goals (what later phases own).\n- Numbered functional requirements FR-001... — each testable, each traceable to a roadmap statement or decision. Cover the phase's components completely; do not invent extra features.\n- Numbered success criteria SC-001... — each MEASURABLE with the metric, threshold and how it is measured (test name sketch; cargo test / vitest / node test / CI job). Per-OS behaviour gets an SC per OS; a macOS/Linux SC names the CI job that proves it.\n- Safety section: which roots are injected, what is verified before extraction or execution, what the elevated helper may do, what the tests must never touch.\n- Edge cases (interrupted download, disk full, file locked by a running DAW on Windows, partial install, downgrade attempt, corrupt feed, offline, feed schema_version 1 vs 2, bundles that still say 0.8.0).\n- "Existing repo pieces" table: file → what this phase reuses or changes (verified, with lines).\n- "New components" table: module / crate / file → purpose.\n- Open questions ONLY where the roadmap explicitly defers a decision to this spec.\n\nReturn the doc status.`,
    { label: 'specify', phase: 'Specify', schema: DOC_STATUS },
  )
  if (!specStatus) throw new Error('Specify agent failed — nothing to review')

  phase('Challenge Spec')
  const specReview = await challengeAndRevise(SPEC, 'spec', [
    { key: 'fidelity', lens: `ROADMAP FIDELITY + DECISION COVERAGE: compare every FR/SC against ${ROADMAP} Phase ${phaseNum} and its "## Decisions" log. Flag roadmap outcomes missing from the spec, spec content the roadmap never asked for (scope creep), and any silently weakened threshold. If the spec carries a "## Clarifications" log, every behavioural clause of every decision must be expressed by at least one FR/SC in the body — a log-only clause is a BLOCKER.` },
    { key: 'testability', lens: 'TESTABILITY + SAFETY: every FR must be verifiable by a concrete test; every SC must state metric + threshold + measurement method. Flag vague criteria, untestable FRs, per-OS behaviour with no per-OS SC, macOS/Linux SCs with no CI job named, and any test sketch that would touch the real system (real plug-in folders, ProgramData, registry, github.com, a real elevation prompt) instead of injected roots and fixtures. Flag harness-reality gaps: a fixture that fakes a state the product cannot be in.' },
    { key: 'reality', model: MECH, lens: 'CODE + DEPENDENCY REALITY: open every repo file the spec cites and verify the claimed lines and behaviour; open the docs of every crate / npm package / Tauri plugin it names and verify the version and API exist as described. Flag invented APIs, wrong paths, and feed changes that would break the schema_version 1 parser in plugins/shared/src/update/update_checker.cpp.' },
  ], 'Challenge Spec')

  phase('Clarify Scan')
  const clar = await run(
    `${CONTEXT}\n\nYou are the CLARIFICATION scanner for ${SPEC} (speckit.clarify equivalent). Read the spec fully, the roadmap phase and its decisions, and any code the spec cites. Find where the spec is UNDERSPECIFIED or AMBIGUOUS — where two reasonable engineers would build different things. Sweep: functional scope and behaviour contracts (what the user sees and can do), data and state (manifest format, where state lives, what survives an uninstall), non-functional (timeouts, retry policy, sizes), integration (what later phases will consume: module boundaries, command names, feed fields), edge cases, security (what is verified when, what the helper may do), distribution, terminology.\n\nRULES:\n- Max 8 questions, ranked by impact. Only questions whose answer CHANGES the implementation — none the roadmap, its decisions or repo conventions already answer.\n- Each gets 2-4 concrete options with one-line consequences, plus your recommendation.\n- Also APPEND a "## Open Clarifications" section to ${SPEC} listing the same questions verbatim (id, question, options).\n- If the spec is genuinely unambiguous, return zero questions and do not touch the file.`,
    { label: 'clarify-scan', phase: 'Clarify Scan', schema: QUESTIONS_SCHEMA },
  )
  const questions = clar ? clar.questions : []
  log(`Clarify scan: ${questions.length} question(s) for the user grill`)

  return {
    stage: 'specify',
    phase: phaseNum,
    slug: SLUG,
    artifacts: { spec: SPEC },
    spec_summary: specStatus.summary,
    open_questions: specStatus.open_questions || [],
    unresolved_review_issues: specReview.unresolved,
    clarification_questions: questions,
    next: questions.length
      ? `MAIN LOOP: grill the user (grill-me skill) through these ${questions.length} questions, then run {phase: ${phaseNum}, stage: "plan", clarifications: {Q1: "<answer>", ...}}.`
      : `No ambiguities found. Run {phase: ${phaseNum}, stage: "plan"}.`,
  }
}

// ===========================================================================
// STAGE: PLAN — encode clarifications → plan → challenge plan → tasks
// ===========================================================================
if (stage === 'plan') {
  const clarifications = A.clarifications || null

  phase('Encode Clarifications')
  if (clarifications) {
    await run(
      `${CONTEXT_LITE}\n\nThe user was interviewed about the open clarifications in ${SPEC}. Encode their answers (speckit.clarify style):\n1. Add/extend a "## Clarifications" section with a dated session log: one line per question, the question and the user's decision.\n2. UPDATE every FR/SC/section the answer affects so the spec body itself reflects the decision.\n3. EVERY BEHAVIOURAL CLAUSE of every decision MUST be carried by at least one FR or SC — mint new FR/SC entries where none exists; downstream stages consume ONLY the FR/SC lists. End each log line with the enforcing ids in brackets, e.g. "[FR-017, SC-024]".\n4. DELETE the "## Open Clarifications" section entirely — every question below is now decided.\n5. Do not weaken any threshold unless the answer explicitly decides that.\n\nANSWERS (id → decision):\n${JSON.stringify(clarifications, null, 2)}`,
      { label: 'encode-clarifications', phase: 'Encode Clarifications', model: MECH },
    )
  }
  const gate = await run(
    `${CONTEXT_LITE}\n\nRead ${SPEC}. Three checks, resolved=true only if ALL pass:\n1. No "## Open Clarifications" section with unresolved questions remains.\n2. No FR/SC contradicts the "## Clarifications" decisions log (if present).\n3. COVERAGE: every behavioural clause of every "## Clarifications" decision is carried by at least one FR or SC in the spec BODY (verify each cited id exists AND expresses that clause; name every failure in unresolved).`,
    { label: 'clarify-gate', phase: 'Encode Clarifications', schema: RECHECK_SCHEMA, model: MECH },
  )
  if (gate && !gate.resolved) {
    return {
      stage: 'plan', phase: phaseNum, slug: SLUG,
      status: 'BLOCKED_UNRESOLVED_CLARIFICATIONS',
      unresolved: gate.unresolved,
      next: 'Spec still has open questions. Grill the user on these, then re-run stage "plan" with args.clarifications covering them.',
    }
  }

  phase('Plan')
  log(`Authoring ${PLAN}...`)
  const planStatus = await run(
    `${CONTEXT}\n\nWrite the technical implementation plan for ${SPEC} to ${PLAN}. Read the (now reviewed) spec first, then every repo file and dependency doc it relies on.\n\nPLAN FORMAT:\n- Module-by-module design: file path, responsibility, public API sketch (real Rust / TypeScript signatures; Tauri commands and their payload types; Node tool CLI), the OS abstraction and its three implementations, state layout and where it is stored, error model.\n- Dependencies: every crate / npm package / Tauri plugin with its exact version and the docs URL you verified it against; prefer first-party Tauri plugins and the Rust standard library; justify each third-party crate.\n- Safety design: injected roots, the verify-before-extract order, the zip-slip guard, the elevated helper's operation set and how the UI reaches it, the Tauri capability set.\n- Test plan: for each FR/SC, the test file, test name and assertion strategy (temp dirs, local fixture server, throwaway minisign key, injected OS layer); which CI job proves each macOS/Linux SC.\n- Build integration: workspace layout, scripts in package.json, which CI workflow files change, which gate lanes run (app lane and/or pipeline lane).\n- Risks + mitigations (WebKitGTK quirks on Linux, Windows file locks while a DAW runs, admin prompts, offline behaviour).\n\nReturn the doc status.`,
    { label: 'plan', phase: 'Plan', schema: DOC_STATUS },
  )
  if (!planStatus) throw new Error('Plan agent failed')

  phase('Challenge Plan')
  const planReview = await challengeAndRevise(PLAN, 'plan', [
    { key: 'spec-coverage', lens: `SPEC COVERAGE: map every FR/SC in ${SPEC} to a design element + test in the plan. Flag anything unaddressed, any test that could not detect its FR failing, and any plan element with no FR behind it.` },
    { key: 'safety-security', lens: 'SAFETY + SECURITY: audit the design for any path where a test or the app could write outside an injected root, extract or run an artifact before its sha256 and signature are checked, follow a symlink or ".." out of the target, accept a downgrade, pass a UI-supplied string to a shell or to the elevated helper, or widen the Tauri capability set beyond need. Also flag any step that needs a code-signing certificate.' },
    { key: 'xplat-reality', model: MECH, lens: 'CROSS-PLATFORM + DEPENDENCY REALITY: open the docs of every crate / package / plugin the plan names and verify each API it uses exists in that version on Windows, macOS and Linux as used. Flag APIs that are Windows-only or desktop-OS-specific without a counterpart in the plan, and per-OS code paths with no test or CI lane.' },
  ], 'Challenge Plan')

  phase('Tasks')
  log(`Authoring ${TASKS}...`)
  const tasksStatus = await run(
    `${CONTEXT}\n\nWrite the dependency-ordered task list for ${PLAN} to ${TASKS}. Read spec and plan first.\n\nTASK FORMAT:\n- Tasks T001... grouped into ordered GROUPS. Within a group, tasks marked [P] are parallel-safe: they touch fully disjoint NEW files only. Any task editing a SHARED file (Cargo.toml, package.json, lib.rs / mod.rs, tauri.conf.json, a capability file, a CI workflow, CMake) goes in its own sequential group.\n- Each task is fully self-contained (executor has no other context): exact files to create/edit, the failing test to write FIRST (file, test name, exact assertions), then the implementation intent, then which gate lane verifies it (app lane: cargo fmt --check, cargo clippy --all-targets -- -D warnings, cargo test, tsc --noEmit, vitest run; pipeline lane: node scripts' tests / --self-test, YAML parse, and run-gate.js for any C++ target).\n- Follow the canonical order: failing test → implement → zero warnings → tests pass.\n- Every test uses injected roots / temp dirs / the local fixture server / a throwaway key; a task whose test would touch the real system is a defect in the task.\n- Last group: integration — the per-OS CI workflow additions for this phase's SCs (written, NOT pushed), the repo lints (node tools/lint-serial-test-runs.js, node tools/check-portability.js where C++ changed, node tools/lint-plugin-editor-baseline.js where an editor changed), and a "PENDING-CI" list naming each macOS/Linux SC and the CI job that proves it.\n- LANE-RUNNING TASKS ARE MAIN-LOOP TASKS: a task whose work is a measurement longer than ~5 minutes (a full tauri build --bundles, a WSL Linux build, a C++ suite lane, clang-tidy) writes the lane script under ${DIR}/evidence/ and returns done with notes "deferred to the main loop at close: run <script>".\n- Pushing, triggering CI, and commits are NEVER tasks — the user does those outside this workflow.\n\nReturn the doc status.`,
    { label: 'tasks', phase: 'Tasks', schema: DOC_STATUS },
  )

  return {
    stage: 'plan', phase: phaseNum, slug: SLUG,
    artifacts: { spec: SPEC, plan: PLAN, tasks: TASKS },
    clarifications_encoded: clarifications ? Object.keys(clarifications).length : 0,
    plan_summary: planStatus.summary,
    tasks_summary: tasksStatus ? tasksStatus.summary : 'TASKS AGENT FAILED — rerun needed',
    open_questions: [...(planStatus.open_questions || []), ...((tasksStatus && tasksStatus.open_questions) || [])],
    unresolved_review_issues: { plan: planReview.unresolved },
    next: `Review ${DIR}/ (spec now includes the clarification decisions), then run {phase: ${phaseNum}, stage: "build"}`,
  }
}

// ===========================================================================
// STAGE: BUILD — dispatch → implement → gate → comply → report
// ===========================================================================
phase('Dispatch')
const dispatch = await run(
  `${CONTEXT_LITE}\n\nRead ${TASKS} (skim ${SPEC}/${PLAN} only if the task list is ambiguous). Convert tasks.md into executable task groups, preserving its group order and [P] parallel markers. For each task emit ONLY: id, title, the file list, and a 1-3 sentence orientation summary — do NOT copy the task body. Mark a group parallel=true ONLY if every task in it touches disjoint new files. Also list the cmake targets this phase builds (EMPTY when it touches only ${APP_DIR}/, Node tools and workflow YAML).`,
  { label: 'dispatch', phase: 'Dispatch', schema: TASKGROUPS_SCHEMA, model: MECH },
)
if (!dispatch || !dispatch.groups || dispatch.groups.length === 0) throw new Error(`Dispatcher produced no task groups — check ${TASKS} exists (run stages "specify" then "plan" first)`)
log(`${dispatch.groups.length} groups, ${dispatch.groups.reduce((n, g) => n + g.tasks.length, 0)} tasks, cmake targets: ${dispatch.cmake_targets.join(', ') || 'none'}`)

// The C++ suites' timing exclusions, CI's own string, reused when a phase touches plugin code.
const PERF_FILTER = "'~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]'"
const gateOut = (name, attempt) => `F:/tmp/gate/${SLUG}-${groupKey(name).replace(/[^A-Za-z0-9]+/g, '_')}-a${attempt}`

// Lane selection from the files a group touches (decided in code, not by the reporter).
const norm = (f) => String(f).replace(/\\/g, '/')
const groupFiles = (group) => group.tasks.flatMap(t => (t.files || []).map(norm))
const touchesApp = (files) => files.some(p => p.startsWith(`${APP_DIR}/`))
const touchesPipeline = (files) => files.some(p => p.startsWith('.github/') || p.startsWith('tools/') || p.startsWith('cmake/') || p.startsWith('plugins/') || p.startsWith('dsp/') || p === 'CMakeLists.txt')

const APP_LANE = `
APP LANE (only if ${APP_DIR}/ exists; if a LATER group creates it, report "not yet created (later group)" and skip):
  cd ${APP_DIR}
  dependency install exactly as the scaffold documents it (npm ci when package-lock.json exists)
  npx tsc --noEmit
  the frontend lint command package.json defines (skip only if the scaffold has none yet)
  npx vitest run
  cargo fmt --manifest-path src-tauri/Cargo.toml --all -- --check
  cargo clippy --manifest-path src-tauri/Cargo.toml --all-targets -- -D warnings
  cargo test --manifest-path src-tauri/Cargo.toml --all-targets
If the plan names a Cargo workspace or extra crates (e.g. the elevated helper), run fmt / clippy / test over the whole
workspace instead (--workspace). Each command's output to a log file under F:/tmp/gate/, tail it. ANY warning is a red.
Never pass --test-threads=1 or --no-file-parallelism. Never run "tauri build" with bundling in a gate (a main-loop lane).`

const PIPELINE_LANE = (targets, out) => `
PIPELINE LANE (this group touches release pipeline / tooling / plugin-side files):
  for every changed or new tools/*.js: node --check <file>, then its tests (node --test <its test file>, or --self-test if it has one)
  for every changed or new .github/workflows/*.yml: npx --yes yaml@2.6.1 valid < <file>
  node tools/lint-serial-test-runs.js
${targets.length ? `  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ${targets.join(' ')}   (log to a file, tail it; any warning is a red)
  node tools/check-bundle.js build/windows-x64-release/VST3/Release/<each built plugin>.vst3
  node tools/header-consumers.js --tag '[krate-manager]'   (no C++ test carries that tag, so it lists EVERY consumer of a
  modified shared file); then, if its last line names executables, run exactly those ONCE, sharded:
  node tools/run-gate.js --filter ${PERF_FILTER} --out ${out} <those exes> 2>&1 | tail -40   (last line GATE GREEN|RED)
  Never run a test executable as one serial process; never two run-gate commands at once.` : '  (no cmake targets in this phase: no C++ build)'}`

const EXEC_RULES = `EXECUTION RULES (violating them fails the gate):
- Every command is a FOREGROUND Bash command with timeout 600000, output redirected to a log file under F:/tmp/gate/, then tail that log.
- DISK GUARD: NEVER run find /c, find /, du or any disk-wide walk; only ever tail a log, never cat it whole.
- NEVER use run_in_background. NEVER use the Monitor tool. NEVER sleep, echo, poll, or "hold" while waiting.
- NEVER write outside the repo and F:/tmp; never run an installer, the elevated helper, or anything that writes to real plug-in folders, ProgramData or the registry.
- The moment every lane's last result line is in hand, call StructuredOutput.`

const implResults = []
let buildLog = null

for (const group of dispatch.groups) {
  phase('Implement')
  log(`Group "${group.name}": ${group.tasks.length} task(s), ${group.parallel ? 'parallel' : 'sequential'}`)

  const runTask = (t) => () =>
    run(
      `${CONTEXT}\n\nExecute this implementation task EXACTLY. FIRST open ${TASKS} and read the full section for task ${t.id} — that section is your AUTHORITATIVE instruction (the summary below is orientation only). Artifacts for reference: ${SPEC}, ${PLAN}. Write the failing test FIRST, then implement. Do NOT run the gate (a dedicated build agent does that after your group) — you MAY run a quick compile check of your own crate or file (cargo check, tsc --noEmit) to catch obvious errors. Do not touch any file outside your task's file list. Never commit, never push, never touch the real system (see the constraints). If the task section says STOP AND SURFACE (or its outcome is a decision only the user can take), return status "blocked" with the decision needed in notes.\n\nTASK ${t.id}: ${t.title}\nFILES: ${t.files.join(', ')}\n\nORIENTATION: ${t.instructions}${retryTasks.includes(t.id) ? `\n\nRETRY (epoch ${retryEpoch(group.name)}): this task's section in ${TASKS} was REWRITTEN after a user ruling (see the spec's Clarifications). Its own file list SUPERSEDES the FILES line above; the title and orientation above may be stale.` : ''}`,
      { label: `impl:${t.id}`, phase: 'Implement', schema: IMPL_RESULT },
    )

  if (group.parallel) {
    const rs = await parallel(group.tasks.map(runTask))
    implResults.push(...rs.filter(Boolean))
  } else {
    for (const t of group.tasks) {
      const r = await runTask(t)()
      if (r) implResults.push(r)
    }
  }

  // Honour an implementer's stop-and-surface.
  const blockedHere = implResults.filter(r => r && r.status === 'blocked' && group.tasks.some(t => t.id === r.task_id) && !unblocked.includes(r.task_id))
  for (const r of implResults) if (r && r.status === 'blocked' && unblocked.includes(r.task_id) && group.tasks.some(t => t.id === r.task_id)) log(`Group "${group.name}": ${r.task_id} is blocked in the cache but listed in args.unblock — continuing (ruling recorded)`)
  if (blockedHere.length) {
    log(`Group "${group.name}": ${blockedHere.map(r => r.task_id).join(', ')} reported BLOCKED — stopping the stage`)
    return {
      stage: 'build', phase: phaseNum, slug: SLUG, status: 'BLOCKED',
      blocked_group: group.name, blocked_tasks: blockedHere, impl_results: implResults,
      next: 'A task reported status "blocked". Read its notes, take the decision it asks for, then re-run stage "build" with resumeFromRunId and args.unblock: ["<task_id>"].',
    }
  }

  // Gate after every group so errors localize to the group that caused them.
  phase('Build+Test')
  const isLastGroup = group === dispatch.groups[dispatch.groups.length - 1]
  const files = groupFiles(group)
  // The last group always runs every lane the phase can reach; earlier groups run only the lanes their files touch
  // (a group that touches neither, e.g. docs only, still runs the app lane as a no-regression check).
  const phaseFiles = dispatch.groups.flatMap(groupFiles)
  const runApp = isLastGroup ? true : (touchesApp(files) || !touchesPipeline(files))
  const runPipeline = isLastGroup ? (touchesPipeline(phaseFiles) || dispatch.cmake_targets.length > 0) : touchesPipeline(files)
  log(`Gate for "${group.name}": ${[runApp && 'app', runPipeline && 'pipeline'].filter(Boolean).join(' + ')}`)

  let attempt = 0
  while (attempt < 4) {
    attempt++
    const lanes = `${runApp ? APP_LANE : ''}${runPipeline ? PIPELINE_LANE(dispatch.cmake_targets, gateOut(group.name, attempt)) : ''}`
    buildLog = await run(
      `${CONTEXT_LITE}\n\nBuild and test the current tree. Run these lanes, in order:${lanes}\n\nReport verbatim results. Fix NOTHING — you are a reporter.\n\n${EXEC_RULES}\n\nGATE SEMANTICS — this gate follows group "${group.name}"; later groups are NOT implemented yet:\n- A target, crate, script or directory that a LATER group of ${TASKS} creates is NOT a failure. Skip it, name it in summary as "not yet created (later group)".\n- TDD expected-reds: before reporting tests_ok=false, open ${TASKS} and read the sections of the tasks implemented so far (up to and including group "${group.name}"). A failing test a task section EXPLICITLY documents as expected-red until a later task lands is NOT a failure: count it green and cite the tasks.md line in summary. Any other failing test is a real red.\n- Expected compile-reds: a test-first group may leave a NEW test that does not compile because the item it exercises lands in a LATER task. If EVERY compile error sits in a file whose tasks.md section says so, set build_ok=true and tests_ok=true and name each such file and the tasks.md line. One undocumented error makes it a real red.\n\nREPORTING CONTRACT:\n- Wait for every command to finish; an unfinished or timed-out run is NOT a result — run it to completion.\n- NEVER return placeholder text. summary carries the real last result line of each lane step; tests_ok=false REQUIRES the failing test names + assertion output in failures.\n(retry-epoch ${gateEpoch(group.name)}: the tree may have been repaired outside the workflow since the last attempt — measure fresh.)`,
      { label: `build:${group.name}:a${attempt}`, phase: 'Build+Test', schema: BUILD_RESULT, model: MECH },
    )
    if (!buildLog) throw new Error('Build agent died — aborting to avoid blind fixes')
    if (buildLog.failures == null) buildLog.failures = ''
    if (buildLog.build_ok && buildLog.tests_ok) { log(`Group "${group.name}" green: ${buildLog.summary}`); break }
    if (attempt >= 4) break
    log(`Group "${group.name}" red (attempt ${attempt}) — dispatching fixer`)
    const fixNote = await run(
      `${CONTEXT}\n\nThe gate is red after implementing group "${group.name}" of ${TASKS}. FIRST read the "Defects found" section at the bottom of ${TASKS} (and the spec's Clarifications): a red already triaged there for a LATER group, or already ruled by the user, is NOT yours — note it in one line and leave it. Then diagnose and fix the ROOT CAUSE of what remains. Rules: never weaken or delete a failing test to make it pass unless it demonstrably contradicts ${SPEC} (then say so loudly in your notes); never silence a warning with an allow/ignore attribute without a reason; fix all failures, not just the first. NOT failures: items a LATER group creates, tests documented as expected-red until a later task, new tests documented as not compiling until a later task. You MAY run the lanes yourself while iterating (logs under F:/tmp/gate/, tail them).\n\nLANES:${lanes}\n\n${EXEC_RULES}\n\nFAILURES:\n${buildLog.failures}\n\nTASK NOTES SO FAR:\n${JSON.stringify(implResults, null, 2)}`,
      { label: `fix:${group.name}:a${attempt}`, phase: 'Build+Test' },
    )
    // A fixer that concludes the red needs a user ruling stops the stage instead of being re-dispatched.
    const fixText = typeof fixNote === 'string' ? fixNote : JSON.stringify(fixNote || '')
    const fixId = `fix:${groupKey(group.name)}`
    if (/STOP AND SURFACE|user ruling|not fixable in code|NOT A CODE DEFECT/i.test(fixText) && !unblocked.includes(fixId)) {
      log(`Group "${group.name}": fixer reports the red needs a user ruling — stopping the stage`)
      return {
        stage: 'build', phase: phaseNum, slug: SLUG, status: 'BLOCKED',
        blocked_group: group.name,
        blocked_tasks: [{ task_id: fixId, status: 'blocked', files_written: [], notes: fixText }],
        last_build: buildLog, impl_results: implResults,
        next: `The fixer reports a spec contradiction. Take the ruling, amend spec/plan/tasks and the test, then re-run stage "build" with resumeFromRunId, args.unblock including "${fixId}" and args.gate_epochs {"${groupKey(group.name)}": 2}.`,
      }
    }
  }
  if (!buildLog.build_ok || !buildLog.tests_ok) {
    return {
      stage: 'build', phase: phaseNum, slug: SLUG, status: 'FAILED_RED_TREE',
      failed_group: group.name, last_build: buildLog, impl_results: implResults,
      next: 'Tree is red after 4 fix attempts. Inspect failures, fix in the main loop, then re-run stage "build" with resumeFromRunId and args.gate_epochs for this group.',
    }
  }
}

// ---------------------------------------------------------------------------
// Comply — adversarial verification of every FR/SC against the real tree.
// ---------------------------------------------------------------------------
phase('Comply')
const complyLenses = [
  { key: 'fr', what: `every FR-xxx in ${SPEC}: open the implementation, read the code, confirm the requirement is met. Evidence = file:line + a one-sentence proof from the code you quote. A per-OS FR needs the code of EACH OS implementation; a macOS/Linux branch whose behaviour only CI can prove is "pending-ci" naming the job, never "pass".` },
  { key: 'sc', what: `every SC-xxx in ${SPEC}: RUN the specific test or measurement yourself (one test by name: cargo test --manifest-path ${APP_DIR}/src-tauri/Cargo.toml <test_name>, or npx vitest run -t "<test name>" in ${APP_DIR}, or the named node test) and paste the ACTUAL output and numbers vs the threshold. Never paraphrase output. Never run a whole C++ executable, a full tauri bundle build, or a WSL/clang-tidy lane — take those readings from ${DIR}/evidence/ (quote file and line). A macOS/Linux SC is "pending-ci" with the CI job and assertion named, plus whatever Windows-side evidence exists.` },
  { key: 'constraints', what: `the cross-cutting constraints, each as its own item: CC-system (grep the tests and code for any real plug-in folder, ProgramData, registry, github.com or real elevation use outside the injected OS layer; quote what you find), CC-verify (every download path verifies sha256 + signature before extract/execute; quote the call order), CC-zipslip (the extraction guard and its test), CC-helper (the elevated helper's accepted operations and their validation), CC-capabilities (the Tauri capability files: list every permission granted and whether the spec needs it), CC-signing (no step needs a certificate), CC-warnings (run cargo clippy --all-targets -- -D warnings, cargo fmt --check, tsc --noEmit in ${APP_DIR}; quote the results), CC-lints (node tools/lint-serial-test-runs.js, plus node tools/check-portability.js if C++ changed; quote verbatim), CC-feed-compat (if the feed changed: the schema_version 1 fields still parse with plugins/shared/src/update/update_checker.cpp; quote both sides).` },
]
const complyResults = await parallel(
  complyLenses.map(l => () =>
    run(
      `${CONTEXT}\n\nYou are the COMPLIANCE verifier. Default verdict is FAIL until you personally verify otherwise — assume the implementers cut corners. Verify ${l.what}\n\nAn item without concrete evidence (real file:line, real measured output) must be verdict "fail" with evidence explaining what is missing.`,
      { label: `comply:${l.key}`, phase: 'Comply', schema: COMPLY_SCHEMA },
    ).then(r => r.items.map(i => ({ ...i, lens: l.key }))),
  ),
)
const complyItems = complyResults.filter(Boolean).flat()
const isOpen = (i) => i.verdict === 'fail' || i.verdict === 'partial'
const failures = complyItems.filter(isOpen)
log(`Compliance: ${complyItems.filter(i => i.verdict === 'pass').length} pass, ${complyItems.filter(i => i.verdict === 'pending-ci').length} pending-ci, ${failures.length} fail/partial`)

// One bounded remediation round for compliance failures, then re-verify just those.
let finalItems = complyItems
if (failures.length > 0) {
  log('Dispatching remediation for failing compliance items...')
  const allLanes = `${APP_LANE}${(touchesPipeline(dispatch.groups.flatMap(groupFiles)) || dispatch.cmake_targets.length) ? PIPELINE_LANE(dispatch.cmake_targets, `F:/tmp/gate/${SLUG}-remediate`) : ''}`
  await run(
    `${CONTEXT}\n\nFix these compliance failures against ${SPEC}. Rules: failing test first where a behaviour is wrong, no threshold relaxing, no test deletion, zero warnings, the gate must stay green.\n\nTRIAGE FIRST: sort every item into one of three classes before touching anything.\n- WORDING: the FR/SC row still says what a recorded Clarifications ruling superseded. Fix = amend the row to the ruling in ${SPEC}; never change code to meet superseded wording.\n- EVIDENCE: the behaviour is met but the verifier lacked output. Fix = gather it (run the one test by name, or quote ${DIR}/evidence/).\n- BEHAVIOUR: a real gap. Fix in code with a failing test first.\nA macOS/Linux-only item is not yours to "pass" locally: make sure the CI job that proves it exists in the workflow file, and leave it pending-ci. Never start a measurement longer than ~20 minutes: report it as "needs the main loop".\n\nLANES (run them when done):${allLanes}\n\n${EXEC_RULES}\n\nFAILING ITEMS:\n${JSON.stringify(failures, null, 2)}`,
    { label: 'remediate', phase: 'Comply' },
  )
  const reverify = await run(
    `${CONTEXT}\n\nRe-verify ONLY these previously-failing compliance items with the same evidence standard (real file:line, real test output run by you). Also run the app lane once and quote its result lines.\n\nITEMS:\n${JSON.stringify(failures, null, 2)}`,
    { label: 'reverify', phase: 'Comply', schema: COMPLY_SCHEMA },
  )
  if (reverify) {
    const reMap = new Map(reverify.items.map(i => [i.id, i]))
    finalItems = complyItems.map(i => reMap.has(i.id) ? { ...reMap.get(i.id), lens: i.lens } : i)
  }
}
const finalFailures = finalItems.filter(isOpen)
const pendingCi = finalItems.filter(i => i.verdict === 'pending-ci')

// ---------------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------------
phase('Report')
const report = await run(
  `${CONTEXT_LITE}\n\nWrite the compliance report to ${COMPLIANCE} and return the same markdown. Content:\n- Header: phase ${phaseNum} (${SLUG}), overall status: COMPLETE only if zero fail/partial items; COMPLETE (PENDING-CI) if the only open items are pending-ci; otherwise INCOMPLETE with the honest gap list first.\n- Compliance table: one row per item below, with its verbatim evidence. Do not soften any fail.\n- PENDING-CI list: each pending-ci item with the CI job and assertion that will prove it (the user pushes; nothing here pushed).\n- Implementation notes: deviations reported by task agents.\n- Remaining steps for the human loop: review, the main-loop lanes listed in ${DIR}/evidence/, commit, and the push that runs the CI proof.\n\nITEMS:\n${JSON.stringify(finalItems, null, 2)}\n\nTASK NOTES:\n${JSON.stringify(implResults, null, 2)}`,
  { label: 'report', phase: 'Report', schema: DOC_SCHEMA, model: MECH },
)

// Mark the roadmap phase — ONLY with zero fail/partial items. Pending-CI is recorded as such, never as plain complete.
if (finalFailures.length === 0) {
  const mark = pendingCi.length ? `✅ COMPLETE (PENDING-CI: ${pendingCi.length} item(s))` : '✅ COMPLETE'
  await run(
    `You are in the Krate Audio monorepo at f:/projects/iterum. Krate Manager roadmap Phase ${phaseNum} is verified. Update ${ROADMAP}:\n1. Directly under the "### Phase ${phaseNum}: ..." heading, insert the status line: "**Status: ${mark} (<today's date, YYYY-MM-DD — get it by running the date command>)** — see ${COMPLIANCE}".\n2. In the Dependency Graph section, if practical, add a ✅ next to this phase's node WITHOUT breaking the ASCII alignment; otherwise skip the diagram.\n3. Touch NOTHING else.`,
    { label: 'mark-roadmap', phase: 'Report', model: TRIVIAL },
  )
  log(`Roadmap phase ${phaseNum} marked ${mark}`)
} else {
  log(`Phase INCOMPLETE — roadmap NOT updated (${finalFailures.length} failing item(s))`)
}

// Standing rule (user, 2026-10-20): every phase close cleans up its own test output.
phase('Cleanup')
const cleanup = await run(
  `You are in the Krate Audio monorepo at f:/projects/iterum. Run exactly this command from the repo root and return its last line verbatim: node tools/clean-test-output.js --apply\nDo nothing else: no other deletions, no edits.`,
  { label: 'cleanup', phase: 'Cleanup', model: TRIVIAL },
)
log(`Cleanup: ${typeof cleanup === 'string' ? cleanup.trim().split('\n').pop() : JSON.stringify(cleanup)}`)

return {
  stage: 'build', phase: phaseNum, slug: SLUG,
  status: finalFailures.length === 0 ? (pendingCi.length ? 'COMPLETE_PENDING_CI' : 'COMPLETE') : 'INCOMPLETE',
  roadmap_updated: finalFailures.length === 0,
  compliance: {
    pass: finalItems.filter(i => i.verdict === 'pass').length,
    pending_ci: pendingCi.length,
    fail_or_partial: finalFailures.length,
    failures: finalFailures,
    pending: pendingCi,
  },
  last_build: buildLog,
  report_path: COMPLIANCE,
  report_markdown: report ? report.markdown : null,
  next: finalFailures.length === 0
    ? 'Review diff + compliance.md, run the main-loop lanes in the evidence folder, then commit; the user pushes to run the CI proof of the pending-ci items.'
    : 'INCOMPLETE — read failures above; fix in the main loop or re-run stage "build" after addressing.',
}
