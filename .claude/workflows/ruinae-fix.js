export const meta = {
  name: 'ruinae-fix',
  description: 'Execute one phase of specs/ruinae-audit/fix-plan.md: per task, a pre-routed implementer runs the plan loop (red test, fix, capped build, sharded gate, pluginval, tidy); a Sonnet reviewer checks the diff and stages it; two reds escalate once to Fable. Nothing commits.',
  whenToUse: 'When executing the Ruinae audit fix plan one phase at a time. args: { phase: 1-6 (phase 1 also runs T0.1 first), tasks?: ["T1.3", ...] (run only these, in plan order), start_at?: "T1.7" (skip tasks before it, e.g. after a stop), skip?: ["T1.9"] }. Tasks run strictly one after another (they share build/windows-x64-release). Approved changes are left STAGED; the main loop commits after the phase.',
  phases: [
    { title: 'Implement', detail: 'one task at a time, on the model the routing table assigns' },
    { title: 'Review', detail: 'diff vs plan, red-first evidence, no relaxed thresholds; stages approved files', model: 'sonnet' },
    { title: 'Escalate', detail: 'a task red twice gets one Fable attempt', model: 'fable' },
  ],
}

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
const PLAN = 'specs/ruinae-audit/fix-plan.md'
const LOG_ROOT = 'f:/tmp/ruinae-fix'
const PERF_FILTER = "'~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]'"
const A = (args && typeof args === 'object') ? args : {}

// ---------------------------------------------------------------------------
// Routing table, decided up front (user, 2026-11-17).
// opus  : critical/high fixes, the ruling-driven tasks, anything flagged CROSS-PLUGIN, GRADUS or STATE-COMPAT,
//         RT/DSP behaviour changes, editor lifetime work.
// sonnet: mechanical tasks: labels, docs, dead code, test-gap fills, probe promotions.
// fable : never first; only the single escalation attempt after two reds (see runTask).
// Every task's review runs on sonnet.
// ---------------------------------------------------------------------------
const TASKS = [
  // Phase 0 runs at the head of phase 1 (hard prerequisite: the gate must be green).
  ['T0.1', 1, 'opus'],
  ['T1.1', 1, 'opus'], ['T1.2', 1, 'opus'], ['T1.3', 1, 'opus'], ['T1.4', 1, 'opus'], ['T1.5', 1, 'opus'],
  ['T1.6', 1, 'opus'], ['T1.7', 1, 'opus'], ['T1.8', 1, 'opus'], ['T1.9', 1, 'opus'], ['T1.10', 1, 'opus'],
  ['T1.11', 1, 'opus'], ['T1.12', 1, 'opus'], ['T1.13', 1, 'opus'], ['T1.14', 1, 'opus'], ['T1.15', 1, 'opus'],
  ['T1.16', 1, 'opus'], ['T1.17', 1, 'opus'], ['T1.18', 1, 'opus'], ['T1.19', 1, 'opus'], ['T1.20', 1, 'opus'],
  ['T1.20a', 1, 'opus'], ['T1.21', 1, 'opus'], ['T1.22', 1, 'opus'], ['T1.22a', 1, 'opus'], ['T1.23', 1, 'opus'], ['T1.24', 1, 'opus'], ['T1.25', 1, 'opus'],
  ['T1.26', 1, 'opus'], ['T1.27', 1, 'opus'], ['T1.28', 1, 'opus'], ['T1.29', 1, 'opus'], ['T1.30', 1, 'opus'],
  ['T1.31', 1, 'opus'], ['T1.32', 1, 'opus'], ['T1.33', 1, 'opus'], ['T1.34', 1, 'opus'],
  // Phase 1b (added 2026-11-22): one preset-load path in the other plugins (R-200 to R-209).
  ['T1.35', 1, 'opus'], ['T1.36', 1, 'opus'], ['T1.37', 1, 'opus'], ['T1.38', 1, 'opus'], ['T1.39', 1, 'opus'],
  ['T1.40', 1, 'opus'], ['T1.41', 1, 'opus'],

  ['T2.1', 2, 'opus'], ['T2.2', 2, 'opus'], ['T2.3', 2, 'sonnet'], ['T2.4', 2, 'sonnet'], ['T2.5', 2, 'opus'],
  ['T2.6', 2, 'sonnet'], ['T2.7', 2, 'opus'], ['T2.8', 2, 'sonnet'], ['T2.9', 2, 'opus'], ['T2.10', 2, 'opus'],
  ['T2.11', 2, 'opus'], ['T2.12', 2, 'opus'], ['T2.13', 2, 'opus'], ['T2.14', 2, 'sonnet'], ['T2.15', 2, 'opus'],
  ['T2.16', 2, 'opus'], ['T2.17', 2, 'opus'], ['T2.18', 2, 'opus'], ['T2.19', 2, 'opus'], ['T2.20', 2, 'opus'],
  ['T2.21', 2, 'opus'], ['T2.22', 2, 'opus'], ['T2.23', 2, 'sonnet'], ['T2.24', 2, 'opus'], ['T2.25', 2, 'opus'],
  ['T2.26', 2, 'opus'], ['T2.27', 2, 'opus'], ['T2.28', 2, 'sonnet'], ['T2.29', 2, 'sonnet'], ['T2.30', 2, 'opus'],
  ['T2.31', 2, 'opus'], ['T2.32', 2, 'opus'], ['T2.33', 2, 'opus'], ['T2.34', 2, 'sonnet'], ['T2.35', 2, 'opus'],

  ['T3.1', 3, 'opus'], ['T3.2', 3, 'opus'], ['T3.3', 3, 'sonnet'], ['T3.4', 3, 'sonnet'], ['T3.5', 3, 'opus'],
  ['T3.6a', 3, 'opus'], ['T3.6b', 3, 'opus'],

  ['T4.1', 4, 'sonnet'], ['T4.2', 4, 'sonnet'], ['T4.3', 4, 'sonnet'], ['T4.4', 4, 'opus'], ['T4.5', 4, 'opus'],
  ['T4.6', 4, 'opus'], ['T4.7', 4, 'opus'], ['T4.8', 4, 'opus'],

  ['T5.1', 5, 'sonnet'], ['T5.2', 5, 'sonnet'], ['T5.3', 5, 'sonnet'], ['T5.4', 5, 'sonnet'], ['T5.5', 5, 'sonnet'],
  ['T5.6', 5, 'sonnet'], ['T5.7', 5, 'sonnet'], ['T5.8', 5, 'opus'], ['T5.9', 5, 'sonnet'],

  ['T6.1', 6, 'sonnet'], ['T6.2', 6, 'sonnet'], ['T6.3', 6, 'sonnet'], ['T6.4', 6, 'sonnet'], ['T6.5', 6, 'sonnet'],
  ['T6.6', 6, 'sonnet'],
].map(([id, ph, model]) => ({ id, phase: ph, model }))

// ---------------------------------------------------------------------------
// Select the work list
// ---------------------------------------------------------------------------
const phaseNum = Number(A.phase)
if (!A.tasks && !(phaseNum >= 1 && phaseNum <= 6)) {
  throw new Error('args.phase must be 1-6 (or pass args.tasks). Example: {"phase": 1}')
}
let list = Array.isArray(A.tasks) && A.tasks.length
  ? TASKS.filter(t => A.tasks.map(String).includes(t.id))
  : TASKS.filter(t => t.phase === phaseNum)
const unknown = (Array.isArray(A.tasks) ? A.tasks.map(String) : []).filter(id => !TASKS.some(t => t.id === id))
if (unknown.length) throw new Error(`Unknown task ids: ${unknown.join(', ')}`)
if (A.start_at) {
  const i = list.findIndex(t => t.id === String(A.start_at))
  if (i < 0) throw new Error(`start_at ${A.start_at} is not in the selected list`)
  if (i > 0) log(`start_at ${A.start_at}: skipping ${list.slice(0, i).map(t => t.id).join(', ')}`)
  list = list.slice(i)
}
const skip = Array.isArray(A.skip) ? A.skip.map(String) : []
if (skip.length) {
  log(`Skipping by request: ${skip.join(', ')}`)
  list = list.filter(t => !skip.includes(t.id))
}
log(`ruinae-fix: ${list.length} task(s): ${list.map(t => `${t.id}(${t.model})`).join(' ')}`)

// ---------------------------------------------------------------------------
// Schemas
// ---------------------------------------------------------------------------
const IMPL_SCHEMA = {
  type: 'object',
  properties: {
    status: { type: 'string', enum: ['done', 'refuted', 'blocked', 'failed'] },
    blocker_kind: { type: 'string', enum: ['none', 'needs_user', 'threshold_unmet', 'environment', 'other'] },
    red_evidence: { type: 'string', description: 'step 1: the failing assertion line(s) of the new test on UNCHANGED production code, verbatim, with the command that produced them; for refuted: the passing line' },
    gate_line: { type: 'string', description: 'the verbatim last line of the run-gate.js command (GATE GREEN ... / GATE RED ...), plus the header-consumers.js last line when it ran' },
    pluginval: { type: 'string', description: 'exit code and verdict, or "not required" with the reason' },
    tidy: { type: 'string', description: 'clang-tidy targets run and their Errors/Warnings totals, or "not required"' },
    extra_checks: { type: 'string', description: 'macOS-SENSITIVE tools, CPU lane, approval_tests, gradus_tests, golden regeneration: what ran and the verbatim result lines' },
    files: { type: 'array', items: { type: 'string' }, description: 'every repo file this attempt changed or created (repo-relative)' },
    tests_added: { type: 'array', items: { type: 'string' }, description: 'file :: TEST_CASE name for every new or rewritten test' },
    deviations: { type: 'string', description: 'everything done differently from the plan section, and why; "none" if none' },
    blocker: { type: 'string', description: 'for blocked/failed: what stops the task, with measured numbers' },
    notes: { type: 'string', description: 'facts the main loop needs for the phase commit message (consumers listed per CROSS-PLUGIN, goldens regenerated and why, findings refuted)' },
    callers_checked: { type: 'string', description: 'for every function/method whose behaviour or signature this attempt changed: "Symbol (file): N callers via find_referencing_symbols -> caller list; how each is covered (unchanged contract / updated / tested)". Or "Serena unavailable: Grep used" with the grep commands. "none" only when no function changed (tests, data, docs only).' },
  },
  required: ['status', 'blocker_kind', 'red_evidence', 'gate_line', 'files', 'deviations', 'notes', 'callers_checked'],
}

const REVIEW_SCHEMA = {
  type: 'object',
  properties: {
    ok: { type: 'boolean' },
    problems: { type: 'array', items: { type: 'string' }, description: 'each concrete problem with file:line; empty when ok' },
    staged: { type: 'array', items: { type: 'string' }, description: 'files staged with git add (only when ok)' },
    summary: { type: 'string' },
  },
  required: ['ok', 'problems', 'staged', 'summary'],
}

// ---------------------------------------------------------------------------
// Prompts
// ---------------------------------------------------------------------------
const CONTEXT = `Repository: f:/projects/iterum (Krate Audio monorepo, VST3 SDK + VSTGUI, C++20). Branch: audit/ruinae. Plugin: Ruinae (plugins/ruinae/), which consumes the shared KrateDSP library (dsp/include/krate/dsp/) and plugins/shared/.
You are executing ONE task of the fix plan ${PLAN}. The plan was written so you do not have to re-derive context: follow it.

READ FIRST, in this order (Read tool, by line ranges; do not read the whole 1,200-line file):
1. The sections "Scope", "How to read an anchor", "Rulings needed before the affected task starts" (the Ruling column is FINAL: recorded from the user on 2026-11-16; never ask, never re-litigate), "Workflow every task follows", "Flags used in the tasks", "Shared test conventions".
2. Your task's own section (grep '^### <id> ' for its line) and its rows in the "Findings → tasks" table.
3. For each R-id your task covers, the finding in specs/ruinae-audit/report.md (grep '^### R-nnn'): problem, evidence, verifier notes.
Anchors carry line numbers from commit f8ed81e1; earlier tasks moved lines. Find the quoted symbol with Grep; never edit by line number alone.
Phase 1b tasks (T1.35-T1.41) work in the plugin their section names, not Ruinae: read the "Phase 1b" preamble (grep '^## Phase 1b') and that plugin's leaf CLAUDE.md. Their findings R-200 to R-209 exist only as rows in the plan's "Findings → tasks" table (not in report.md); their anchors are from 2026-11-22.`

const OVERRIDES = `OVERRIDES OF THE PLAN (these win over the plan text):
- Step 6 "Commit" is REPLACED: do NOT commit, do NOT stage (no git add), do NOT stash, reset, checkout, clean, amend or push. Leave your changes in the working tree; a reviewer stages them and the main loop commits once per phase.
- Changes already STAGED belong to earlier, approved tasks of this phase. Never unstage or rewrite them. \`git diff\` (unstaged) is your work; \`git diff --cached\` is theirs. A file you edit that is already staged is fine: your hunks show in the unstaged diff.
- Build with the cap and without MSBuild node reuse: "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target <targets> -- -m:2 /nr:false
- The gate filter is ${PERF_FILTER} (the plan's filter plus ~[long]~[.]): node tools/run-gate.js --share 4 --filter ${PERF_FILTER} --out ${LOG_ROOT}/<task>/gate <exes>
  Run a [long] case your task adds by its name, directly, once. Timing cases ([performance] etc.) run only through node tools/run-cpu-tests.js <exe>, and only when your task section asks for it (nothing else is running, so it may run inside the task).
- clang-tidy: ./tools/run-clang-tidy.ps1 -Target <ruinae|dsp|shared|the plugin a Phase 1b task names> -BuildDir build/windows-ninja -Jobs 4 (PowerShell). If it reports phantom warnings from a stale compile db, reconfigure the ninja preset once, then re-run.
- If the root cause is in SHARED code (dsp/include, plugins/shared), fix it there, even when the task section names only plugin files or says "fixtures only": the fix then lands for every plugin that uses it (user ruling 2026-11-17). Do the CROSS-PLUGIN steps: grep -rl the header across plugins/ and dsp/include, list every consumer in notes, build and gate each consumer's suite (gradus_tests for the arp core; the executables header-consumers.js names; Iterum plugin_tests approval_tests for DSP Iterum uses), and explain the root cause in deviations. Restore every temporary diagnostic edit (A/B probes) before returning.
- A red gate case your change cannot reach: run that one case by name once. Green alone + a wall-clock assertion = load: record it in notes. Red alone = it is yours to fix, or status "failed" with the evidence. Never call a failure "pre-existing" and move on.`

const EXEC_RULES = `EXECUTION RULES:
- ONE heavy job at a time: build, gate, pluginval, clang-tidy, CPU lane and WSL tools never run concurrently.
- Every long command runs in the FOREGROUND (Bash timeout 600000) with stdout+stderr redirected to a log under ${LOG_ROOT}/<task id>/, then tail that log. Read a failing shard's log from the gate --out directory; never re-run a slow command to re-read its output.
- NEVER use run_in_background, the Monitor tool, sleep, or polling. NEVER run find /c, find /, du or any disk-wide walk; never cat a whole log. Never run tools/clean-test-output.js.
- WSL tools (macos-fastmath-repro.sh, check-portability.js) may be used, but NEVER run wsl --shutdown, wsl --terminate or anything that stops or restarts WSL: the user runs WSL in other sessions at the same time (user, 2026-11-18). A hook blocks it.
- CALLER CHECK (required, before editing): load Serena once with ToolSearch "select:mcp__serena__find_symbol,mcp__serena__find_referencing_symbols,mcp__serena__get_symbols_overview". For EVERY function or method whose behaviour or signature you change, run mcp__serena__find_referencing_symbols on it and account for every caller. A method declared in a header and defined in a .cpp: query BOTH files and take the union (one side can return fewer callers); get each side's exact name_path with find_symbol first ("Class/method" in the header, "Class::method" in the .cpp). Never accept an empty answer on the first try: an empty result right after startup can be an unfinished clangd index; re-run once, and if it is still empty, confirm with Grep before writing "0 callers". Serena resolves calls through macros, templates and virtuals that Grep misses (example: RuinaeEngine::setAmpRelease is called only through the RUINAE_APPLY_ENVELOPE macro). Use find_symbol / get_symbols_overview to locate symbols instead of reading whole files. Report the result in callers_checked. Only if the Serena tools are absent or error out, fall back to Grep and say so in callers_checked.
- New files with the Write tool (no heredocs). Read a file before editing it. Before creating any class or struct, grep dsp/ and plugins/ for the name (ODR). Node.js for any helper script, never Python. One git operation per command, never chained.
- Thresholds in the plan are final. Never relax a threshold, weaken an assertion, tag a test hidden/[.], or shrink a workload to get green. If the plan's threshold cannot be met, stop with status "blocked", blocker_kind "threshold_unmet", and the measured numbers.
- If the task needs a decision only the user can make that the Rulings table does not cover, stop with status "blocked", blocker_kind "needs_user", and state the question with your recommended answer.
- PLAUSIBLE findings ("verify first"): if the new test PASSES on unchanged code, keep the test, change no production code, and return status "refuted" with the passing line as red_evidence.
- Finish every step of the plan loop (1 red test, 2 fix, 3 capped build with zero warnings, 4 sharded gate GREEN, 5 pluginval + clang-tidy) before returning "done". Return via StructuredOutput the moment you have the results.`

function implPrompt(t, attempt, feedback) {
  const retry = feedback
    ? `\n\nTHIS IS ATTEMPT ${attempt}. A previous attempt did not pass. Its report and the reviewer's problems:\n${feedback}\nThe working tree still holds that attempt's UNSTAGED changes. Inspect them with git diff and git status; continue from them or revert only those unstaged hunks of this task (git checkout -- <file> restores a file to its STAGED version, which keeps earlier tasks' approved work). Fix every listed problem.`
    : ''
  // args.notes: { "T3.5": "..." } — main-loop context for a relaunch after a block (e.g. the stopped run's work is in the tree).
  const note = A.notes && A.notes[t.id] ? `\n\nNOTE FROM THE MAIN LOOP (relaunch): ${A.notes[t.id]}` : ''
  return `${CONTEXT}\n\nYOUR TASK: ${t.id} (fix-plan phase ${t.phase}).${note}${retry}\n\n${OVERRIDES}\n\n${EXEC_RULES}`
}

function reviewPrompt(t, impl) {
  return `${CONTEXT}\n\nYou are the REVIEWER for task ${t.id}. The implementer reported:\n${JSON.stringify(impl, null, 2)}\n
Read the task section in ${PLAN} and the rows for its R-ids. Then inspect ONLY this task's work: \`git status --porcelain\` and \`git diff\` (unstaged) plus every untracked file in the implementer's file list. Staged changes (\`git diff --cached\`) are earlier approved tasks: ignore them.
Check, and list every violation as a concrete problem with file:line:
1. Scope: every changed production file is named by the task section or required by it (CROSS-PLUGIN consumers grep, STATE-COMPAT load paths, CMakeLists test registration). A fix in SHARED code (dsp/include, plugins/shared) that the task section did not name is VALID when the root cause is there: the bug is then fixed for every plugin that uses it (user ruling 2026-11-17). Accept it when deviations names the root cause with evidence, notes lists every consumer from grep -rl, and the gate ran each consumer's suite (gradus_tests for the arp core, header-consumers.js executables, Iterum plugin_tests/approval_tests for Iterum-consumed DSP). Missing evidence is the problem, not the location. A temporary diagnostic edit (an A/B probe) left in the tree, unrelated edits, reformatting or "improvements" are problems.
2. Red first: the test(s) the section names exist with the named TEST_CASE and tags, and red_evidence is a credible failing assertion from that test on unchanged code (or, for status "refuted", a passing line and NO production change).
3. Nothing relaxed: no threshold looser than the plan's, no weakened or deleted assertion, no [.]/hidden tag, no shrunk workload, no test rewritten to accept the bug. The plan's own explicit test rewrites (e.g. T4.8 R-076) are allowed.
4. Plan conformance: the fix matches the section and the recorded Ruling; deviations are justified.
5. Real-time safety on audio-thread paths (no allocation, lock, exception or I/O added), layer rules for dsp/ headers, macOS fast-math rules (detail::isFinite, no numeric_limits infinity/NaN constants, std::llround for musical time).
6. Evidence: gate_line ends GATE GREEN; pluginval exit 0 when plugin sources changed; clang-tidy 0 warnings when it was required.
7. Callers: load Serena once (ToolSearch "select:mcp__serena__find_referencing_symbols,mcp__serena__find_symbol"). For every function or method whose behaviour or signature the unstaged diff changes, run mcp__serena__find_referencing_symbols yourself (header and .cpp side, union) and compare with the implementer's callers_checked. Then cross-check cheaply with Grep, files only (grep -rlw <name> over the plugin/dsp dirs the symbol can reach): a file Grep lists that neither Serena nor callers_checked accounts for must be opened and explained. A caller the implementer did not account for, whose contract the change breaks or leaves untested, is a problem (file:line). A missing or "none" callers_checked while production functions changed is a problem. Only if Serena is unavailable, use Grep and say so in the summary.
Do NOT edit any file and do NOT re-run builds or gates. You may run git, grep/read and Serena (read-only query tools only) commands.
If and only if there are no problems: stage exactly this task's files with ONE command, git add -- <file> <file> ... (the changed tracked files plus the new untracked files that belong to the task; never git add -A or git add .), then report ok=true and the staged list. Otherwise report ok=false, staged=[], and the problems.`
}

function feedbackOf(impl, review) {
  const parts = []
  if (impl) parts.push(`IMPLEMENTER REPORT:\n${JSON.stringify(impl, null, 2)}`)
  else parts.push('IMPLEMENTER: returned nothing (agent died or was skipped).')
  if (review) parts.push(`REVIEWER PROBLEMS:\n- ${(review.problems || []).join('\n- ') || '(none)'}`)
  return parts.join('\n\n')
}

// ---------------------------------------------------------------------------
// One task: attempt 1 and 2 on the routed model, attempt 3 on Fable.
// needs_user / environment blockers stop at once: another model cannot answer them.
// ---------------------------------------------------------------------------
async function runTask(t) {
  const plan = [
    { model: t.model, phaseName: 'Implement' },
    { model: t.model, phaseName: 'Implement' },
    { model: 'fable', phaseName: 'Escalate' },
  ]
  const attempts = []
  let feedback = null
  for (let i = 0; i < plan.length; i++) {
    const a = i + 1
    const impl = await agent(implPrompt(t, a, feedback), {
      label: `impl:${t.id}:a${a}:${plan[i].model}`,
      phase: plan[i].phaseName,
      schema: IMPL_SCHEMA,
      model: plan[i].model,
      agentType: 'general-purpose',
    })
    let review = null
    if (impl && (impl.status === 'done' || impl.status === 'refuted')) {
      review = await agent(reviewPrompt(t, impl), {
        label: `review:${t.id}:a${a}`,
        phase: 'Review',
        schema: REVIEW_SCHEMA,
        model: 'sonnet',
        agentType: 'general-purpose',
      })
    }
    attempts.push({ attempt: a, model: plan[i].model, impl, review })
    if (review && review.ok) {
      log(`${t.id}: ${impl.status} on attempt ${a} (${plan[i].model}); staged ${review.staged.length} file(s)`)
      return { id: t.id, outcome: impl.status, model: plan[i].model, attempts }
    }
    if (impl && impl.status === 'blocked' && (impl.blocker_kind === 'needs_user' || impl.blocker_kind === 'environment')) {
      log(`${t.id}: blocked (${impl.blocker_kind}) on attempt ${a}: ${impl.blocker}`)
      return { id: t.id, outcome: 'blocked', blocker_kind: impl.blocker_kind, model: plan[i].model, attempts }
    }
    feedback = feedbackOf(impl, review)
    log(`${t.id}: attempt ${a} (${plan[i].model}) not accepted: ${impl ? impl.status : 'no result'}${review ? `, reviewer: ${(review.problems || []).length} problem(s)` : ''}`)
  }
  return { id: t.id, outcome: 'failed', model: 'fable', attempts }
}

// ---------------------------------------------------------------------------
// Main loop: strictly serial. Stop at the first task that is not accepted: later tasks build on it,
// and its unstaged changes are left in the tree for the main loop to inspect.
// ---------------------------------------------------------------------------
const results = []
let stoppedAt = null
for (const t of list) {
  phase('Implement')
  const r = await runTask(t)
  results.push(r)
  if (r.outcome !== 'done' && r.outcome !== 'refuted') {
    stoppedAt = t.id
    const rest = list.slice(list.indexOf(t) + 1).map(x => x.id)
    log(`STOPPED at ${t.id} (${r.outcome}). Not started: ${rest.join(', ') || '(none)'}`)
    break
  }
}

const last = r => r.attempts[r.attempts.length - 1]
return {
  phase: A.phase ?? null,
  stopped_at: stoppedAt,
  not_started: stoppedAt ? list.slice(list.findIndex(t => t.id === stoppedAt) + 1).map(t => t.id) : [],
  summary: results.map(r => ({
    id: r.id,
    outcome: r.outcome,
    accepted_on: r.outcome === 'done' || r.outcome === 'refuted' ? `attempt ${last(r).attempt} (${last(r).model})` : null,
    escalated_to_fable: r.attempts.some(a => a.model === 'fable'),
    staged: last(r).review ? last(r).review.staged : [],
  })),
  details: results.map(r => ({
    id: r.id,
    outcome: r.outcome,
    blocker_kind: r.blocker_kind || null,
    final: last(r).impl,
    review: last(r).review,
    earlier_attempts: r.attempts.slice(0, -1).map(a => ({ attempt: a.attempt, model: a.model, status: a.impl ? a.impl.status : null, problems: a.review ? a.review.problems : null })),
  })),
}
