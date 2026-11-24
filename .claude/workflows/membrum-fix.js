export const meta = {
  name: 'membrum-fix',
  description: 'Execute one phase of specs/membrum-audit/fix-plan.md: a preflight checks the user rulings the phase needs, then per task a pre-routed implementer runs the plan loop (red test, fix, capped build, sharded gate, pluginval, tidy); a Sonnet reviewer checks the diff and stages it; two reds escalate once to Fable. Nothing commits.',
  whenToUse: 'When executing the Membrum audit fix plan one phase at a time. args: { phase: 1-8, tasks?: ["T1.3", ...] (run only these, in plan order), start_at?: "T1.7" (skip tasks before it, e.g. after a stop), skip?: ["T1.9"], notes?: {"T1.7": "relaunch context"} }. Before a phase, record the user\'s answers for its tasks in the plan\'s "Tasks that stop for the user\'s ruling" table (Ruling column): the preflight stops the run, before any code changes, when a selected task has no recorded ruling. Tasks run strictly one after another (they share build/windows-x64-release). Approved changes are left STAGED; the main loop commits after the phase, or at a needs_ruling stop once the user has ruled.',
  phases: [
    { title: 'Preflight', detail: 'rulings the selected tasks need are recorded in the plan', model: 'sonnet' },
    { title: 'Implement', detail: 'one task at a time, on the model the routing table assigns' },
    { title: 'Review', detail: 'diff vs plan, red-first evidence, no relaxed thresholds, flag obligations; stages approved files', model: 'sonnet' },
    { title: 'Escalate', detail: 'a task red twice gets one Fable attempt', model: 'fable' },
  ],
}

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
const PLAN = 'specs/membrum-audit/fix-plan.md'
const REPORT = 'specs/membrum-audit/report.md'
const LOG_ROOT = 'f:/tmp/membrum-fix'
const GATE_FILTER = "'~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]'"
const A = (args && typeof args === 'object') ? args : {}

// ---------------------------------------------------------------------------
// Routing table (same rule as ruinae-fix, user 2026-11-17).
// opus  : critical/high fixes, ruling-driven tasks, CROSS-PLUGIN, STATE-COMPAT, THREADING, MACOS, PHYSICS,
//         RT/DSP behaviour changes, kit retunes, editor work.
// sonnet: mechanical tasks: display-only, docs, comments, dead code, lint fixes, test-gap fills, probe promotions.
// fable : never first; only the single escalation attempt after two reds (see runTask).
// Every task's review runs on sonnet.
// ---------------------------------------------------------------------------
const TASKS = [
  ['T1.1', 1, 'opus'], ['T1.2', 1, 'opus'], ['T1.3', 1, 'opus'], ['T1.4', 1, 'opus'], ['T1.5', 1, 'opus'],
  ['T1.6', 1, 'opus'], ['T1.7', 1, 'opus'], ['T1.8', 1, 'opus'], ['T1.9', 1, 'opus'], ['T1.10', 1, 'opus'],
  ['T1.11', 1, 'opus'], ['T1.12', 1, 'opus'], ['T1.13', 1, 'opus'], ['T1.14', 1, 'opus'], ['T1.15', 1, 'opus'],
  ['T1.16', 1, 'opus'], ['T1.17', 1, 'opus'], ['T1.18', 1, 'opus'], ['T1.19', 1, 'opus'], ['T1.20', 1, 'opus'],
  ['T1.21', 1, 'opus'], ['T1.22', 1, 'opus'], ['T1.23', 1, 'opus'], ['T1.24', 1, 'opus'], ['T1.25', 1, 'opus'], ['T1.26', 1, 'opus'],

  ['T2.1', 2, 'opus'], ['T2.2', 2, 'opus'], ['T2.3', 2, 'opus'], ['T2.4', 2, 'opus'], ['T2.5', 2, 'opus'],
  ['T2.6', 2, 'opus'], ['T2.7', 2, 'opus'], ['T2.8', 2, 'opus'], ['T2.9', 2, 'opus'], ['T2.10', 2, 'opus'],
  ['T2.11', 2, 'opus'], ['T2.12', 2, 'opus'], ['T2.13', 2, 'opus'], ['T2.14', 2, 'opus'], ['T2.15', 2, 'opus'],
  ['T2.16', 2, 'opus'], ['T2.17', 2, 'opus'], ['T2.18', 2, 'sonnet'], ['T2.19', 2, 'opus'], ['T2.20', 2, 'opus'],
  ['T2.21', 2, 'opus'], ['T2.22', 2, 'opus'], ['T2.23', 2, 'opus'], ['T2.24', 2, 'opus'], ['T2.25', 2, 'opus'],
  ['T2.26', 2, 'opus'], ['T2.27', 2, 'opus'], ['T2.28', 2, 'opus'], ['T2.29', 2, 'sonnet'], ['T2.30', 2, 'sonnet'],
  ['T2.31', 2, 'opus'], ['T2.32', 2, 'sonnet'],

  ['T3.1', 3, 'opus'], ['T3.2', 3, 'sonnet'], ['T3.3', 3, 'opus'], ['T3.4', 3, 'opus'], ['T3.5', 3, 'opus'], ['T3.6', 3, 'opus'],

  ['T4.1', 4, 'opus'], ['T4.2', 4, 'opus'], ['T4.3', 4, 'opus'], ['T4.4', 4, 'opus'], ['T4.5', 4, 'opus'],
  ['T4.6', 4, 'opus'], ['T4.7', 4, 'opus'], ['T4.8', 4, 'opus'], ['T4.9', 4, 'opus'], ['T4.10', 4, 'opus'],
  ['T4.11', 4, 'opus'], ['T4.12', 4, 'opus'], ['T4.13', 4, 'opus'], ['T4.14', 4, 'opus'], ['T4.15', 4, 'opus'],
  ['T4.16', 4, 'opus'], ['T4.17', 4, 'sonnet'], ['T4.18', 4, 'opus'], ['T4.19', 4, 'opus'], ['T4.20', 4, 'opus'],
  ['T4.21', 4, 'opus'], ['T4.22', 4, 'sonnet'], ['T4.23', 4, 'opus'], ['T4.24', 4, 'opus'], ['T4.25', 4, 'opus'],
  ['T4.26', 4, 'opus'],

  ['T5.1', 5, 'opus'], ['T5.2', 5, 'opus'], ['T5.3', 5, 'opus'], ['T5.4', 5, 'opus'], ['T5.5', 5, 'opus'],
  ['T5.6', 5, 'opus'], ['T5.7', 5, 'opus'], ['T5.8', 5, 'opus'], ['T5.9', 5, 'opus'], ['T5.10', 5, 'opus'],
  ['T5.11', 5, 'opus'], ['T5.12', 5, 'opus'], ['T5.13', 5, 'opus'],

  ['T6.1', 6, 'opus'], ['T6.2', 6, 'opus'], ['T6.3', 6, 'opus'], ['T6.4', 6, 'sonnet'], ['T6.5', 6, 'opus'],
  ['T6.6', 6, 'opus'], ['T6.7', 6, 'sonnet'], ['T6.8', 6, 'sonnet'], ['T6.9', 6, 'sonnet'], ['T6.10', 6, 'sonnet'],

  ['T7.1', 7, 'sonnet'], ['T7.2', 7, 'opus'], ['T7.3', 7, 'sonnet'], ['T7.4', 7, 'sonnet'],

  ['T8.1', 8, 'sonnet'], ['T8.2', 8, 'sonnet'], ['T8.3', 8, 'sonnet'], ['T8.4', 8, 'sonnet'], ['T8.5', 8, 'sonnet'],
].map(([id, ph, model]) => ({ id, phase: ph, model }))

// The plan's "Tasks that stop for the user's ruling" table, by when the question is asked.
// before: the preflight requires a recorded ruling before the run starts.
// after_verification: the task runs its VERIFY-FIRST test, then stops for the ruling if the finding is confirmed.
// after_table: the task always stops after review (changes staged) with its before/after kit table.
const RULING_BEFORE = ['T1.20', 'T1.22', 'T1.23', 'T2.8', 'T2.9', 'T4.1', 'T4.5', 'T4.25', 'T4.26', 'T5.3', 'T5.4', 'T5.5', 'T6.10']
const RULING_AFTER_VERIFICATION = ['T4.20', 'T4.24']
const RULING_AFTER_TABLE = ['T5.7', 'T5.8', 'T5.9', 'T5.10', 'T5.11', 'T5.12', 'T5.13']

// ---------------------------------------------------------------------------
// Select the work list
// ---------------------------------------------------------------------------
const phaseNum = Number(A.phase)
if (!A.tasks && !(phaseNum >= 1 && phaseNum <= 8)) {
  throw new Error('args.phase must be 1-8 (or pass args.tasks). Example: {"phase": 1}')
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
log(`membrum-fix: ${list.length} task(s): ${list.map(t => `${t.id}(${t.model})`).join(' ')}`)

// ---------------------------------------------------------------------------
// Schemas
// ---------------------------------------------------------------------------
const PREFLIGHT_SCHEMA = {
  type: 'object',
  properties: {
    missing: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          task: { type: 'string' },
          question: { type: 'string', description: 'the question, verbatim from the table' },
          plan_default: { type: 'string', description: 'the "Default this plan assumes" cell, verbatim' },
        },
        required: ['task', 'question', 'plan_default'],
      },
      description: 'every checked task whose row has no recorded user ruling',
    },
    recorded: { type: 'array', items: { type: 'string' }, description: '"<task>: <ruling cell>" for every checked task that has one' },
  },
  required: ['missing', 'recorded'],
}

const IMPL_SCHEMA = {
  type: 'object',
  properties: {
    status: { type: 'string', enum: ['done', 'refuted', 'blocked', 'failed'] },
    blocker_kind: { type: 'string', enum: ['none', 'needs_user', 'prerequisite', 'threshold_unmet', 'environment', 'other'] },
    red_evidence: { type: 'string', description: 'step 1: the failing assertion line(s) of the new test on UNCHANGED production code, verbatim, with the command that produced them; for refuted: the passing line' },
    gate_line: { type: 'string', description: 'the verbatim last line of the run-gate.js command (GATE GREEN ... / GATE RED ...), plus the header-consumers.js last line when it ran' },
    pluginval: { type: 'string', description: 'exit code and verdict, or "not required" with the reason' },
    tidy: { type: 'string', description: 'clang-tidy targets run and their Errors/Warnings totals, or "not required"' },
    findings_outcome: { type: 'string', description: 'one line per MB id of the task: fixed (file:line, test name, the finding\'s own symptom re-measured with numbers) or refuted (test name, passing output)' },
    flag_obligations: { type: 'string', description: 'per flag on the task, what its obligations required and the verbatim result lines (CROSS-PLUGIN consumer grep + executables gated; STATE-COMPAT versions and the v5 read-path test; THREADING sanitizer runs; MACOS proxies and lints; UI screenshots and uidesc check). "none" when the task has no flag' },
    preset_table: { type: 'string', description: 'PRESET-IMPACT: the before/after kit-quality table (per affected pad: peak, rms500, decay, centroid, limiter margin; near-duplicate pairs that appeared or disappeared), or "not PRESET-IMPACT"' },
    ruling_needed: { type: 'string', description: 'empty when none. Otherwise the question the user must rule on before this task is committed, with the evidence (table, verification result) and your recommended answer' },
    extra_checks: { type: 'string', description: 'CPU lane, ASan, golden re-baselines, generator runs: what ran and the verbatim result lines' },
    files: { type: 'array', items: { type: 'string' }, description: 'every repo file this attempt changed or created (repo-relative); never a temporary probe file' },
    tests_added: { type: 'array', items: { type: 'string' }, description: 'file :: TEST_CASE name for every new or rewritten test' },
    deviations: { type: 'string', description: 'everything done differently from the plan section, and why; "none" if none' },
    blocker: { type: 'string', description: 'for blocked/failed: what stops the task, with measured numbers; for needs_user the question and your recommended answer' },
    notes: { type: 'string', description: 'facts the main loop needs for the commit message (consumers per CROSS-PLUGIN, goldens re-baselined and why, existing tests that gained a process() block, sanitizer runs left to CI)' },
    callers_checked: { type: 'string', description: 'for every function/method whose behaviour or signature this attempt changed: "Symbol (file): N callers via find_referencing_symbols -> caller list; how each is covered (unchanged contract / updated / tested)". Or "Serena unavailable: Grep used" with the grep commands. "none" only when no function changed (tests, data, docs only).' },
  },
  required: ['status', 'blocker_kind', 'red_evidence', 'gate_line', 'findings_outcome', 'flag_obligations', 'preset_table', 'ruling_needed', 'files', 'deviations', 'notes', 'callers_checked'],
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
const CONTEXT = `Repository: f:/projects/iterum (Krate Audio monorepo, VST3 SDK + VSTGUI, C++20). Branch: audit/membrum. Plugin: Membrum (plugins/membrum/), a physically-modelled drum synthesizer that consumes the shared KrateDSP library (dsp/include/krate/dsp/), plugins/shared/ and the kit generator tools/membrum_preset_generator.cpp.
You are executing ONE task of the fix plan ${PLAN}. The plan was written so you do not have to re-derive context: follow it.

READ FIRST, in this order (Read tool, by line ranges; the plan is about 1 MB, never read it whole; find each part with Grep for its heading):
1. "## How to execute a task" (steps 1-6, the rules that hold for every task, and "Measuring with the audit probes"), "## Flags", and in "## Phases" the bullets "How the phases are cut" and the table "Tasks that stop for the user's ruling before their commit". A Ruling column cell in that table is FINAL: recorded from the user; never ask again, never re-litigate.
2. Your task's own section (grep '^### <id> — ' for its line; it ends at the next '^### ' or '^## ') and its rows in "## Findings to tasks" (grep '| <id> |').
3. For each MB id your task covers, the finding in ${REPORT} (grep '^#### MB-nnn '): problem, evidence, verifier notes. Where the task cites report sections 3-5 (measurement baseline, physical-model sources), read those parts.
Anchors carry line numbers from commit 4a14376d; earlier tasks moved lines. Find the quoted symbol with Grep; never edit by line number alone.`

const OVERRIDES = `OVERRIDES OF THE PLAN (these win over the plan text):
- Step 6 "Commit" is REPLACED: do NOT commit, do NOT stage (no git add), do NOT stash, reset, checkout, clean, amend or push. Leave your changes in the working tree; a reviewer stages them and the main loop commits.
- Changes already STAGED belong to earlier, approved tasks of this run. Never unstage or rewrite them. \`git diff\` (unstaged) is your work; \`git diff --cached\` is theirs. A file you edit that is already staged is fine: your hunks show in the unstaged diff.
- PREREQUISITES are hard. Before step 1, check every task on your Prerequisites line: it counts as done when it is in the "accepted earlier in this run" list below, or when \`git log --oneline origin/main..HEAD\` shows a commit whose message names it (git log --grep "<id>" helps). The plan's "Kit-data gate" (T5.6-T5.13) is a prerequisite too. A missing prerequisite: return status "blocked", blocker_kind "prerequisite", naming it, without changing any file.
- Build with the cap and without MSBuild node reuse: "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target <targets> -- -m:2 /nr:false (the generator target generate_membrum_presets and the ASan build use the same cap).
- The gate filter is ${GATE_FILTER} (the plan's filter plus ~[long]~[.]): node tools/run-gate.js --share 4 --filter ${GATE_FILTER} --out ${LOG_ROOT}/<task>/gate <exes>
  The audit-probe procedure ('[audit-probe]' filter) and a [long] case your task adds run by their own filter or name, once. Timing cases ([performance] etc.) run only through node tools/run-cpu-tests.js <exe>, and only when your task section asks for it (nothing else is running, so it may run inside the task).
- After any run of dsp_systems_tests: \`git status --porcelain specs/\` and restore every changed file under specs/profundum-*/evidence/ with git checkout -- <those paths> (those cases rewrite their evidence CSVs). They are never part of your files.
- Temporary audit-probe files (plugins/membrum/tests/audit/ and the CMake line) and every A/B diagnostic edit are removed before you return; \`git status --porcelain plugins/membrum\` must then show only the task's own files.
- clang-tidy: ./tools/run-clang-tidy.ps1 -Target <membrum|dsp|shared|membrum-fit> -BuildDir build/windows-ninja -Jobs 4 (PowerShell), once per target your diff touches. It analyzes only your uncommitted changes (changed .cpp files + one includer per changed header); never ask it for a whole-target sweep (a hook blocks that). If it reports phantom warnings from a stale compile db, reconfigure the ninja preset once, then re-run.
- If the root cause is in SHARED code (dsp/include, plugins/shared, tests/test_helpers), fix it there, even when the task section names only Membrum files: the fix then lands for every plugin that uses it (user ruling 2026-11-17). Do the CROSS-PLUGIN IMPACT obligations: grep -rl the header across plugins/, dsp/include and tests/, list every consumer in notes, build and gate every executable header-consumers.js names, and explain the root cause in deviations. Where the plan promises another plugin stays bit-identical, the new behaviour is opt-in.
- A red gate case your change cannot reach: run that one case by name once. Green alone + a wall-clock assertion = load: record it in notes. Red alone = it is yours to fix, or status "failed" with the evidence. Never call a failure "pre-existing" and move on.
- RULINGS: read your task's row in the rulings table. A recorded Ruling is final and decides the option you build. If your task needs a ruling the table does not hold (or the section offers options the user must choose between), return status "blocked", blocker_kind "needs_user", with the question and your recommended answer in blocker, before changing production code.
- PRESET-IMPACT (user ruling 2026-11-24, last row of the rulings table): measure the kit-quality probe before and after as the flag says and put the table in preset_table. An audible change that IS the finding's stated purpose does NOT stop: leave ruling_needed empty; the user reviews every table of the phase at its end. This overrides any "stop for the ruling" line in your task section about such a purposeful change. Only a change the user would hear as a different kit BEYOND the finding's stated purpose (or a stop your section requires for another reason, e.g. a generator clamp report) goes into ruling_needed, after the task is finished (gate green, pluginval, tidy); the run then stops after review so the user can listen.`

const EXEC_RULES = `EXECUTION RULES:
- ONE heavy job at a time: build, gate, pluginval, clang-tidy, the generator, the ASan build, CPU lane and WSL tools never run concurrently.
- Every long command runs in the FOREGROUND (Bash timeout 600000) with stdout+stderr redirected to a log under ${LOG_ROOT}/<task id>/, then tail that log. Read a failing shard's log from the gate --out directory; never re-run a slow command to re-read its output.
- NEVER use run_in_background, the Monitor tool, sleep, or polling. NEVER run find /c, find /, du or any disk-wide walk; never cat a whole log. Never run tools/clean-test-output.js.
- WSL tools (macos-fastmath-repro.sh, check-portability.js, a TSan build) may be used, but NEVER run wsl --shutdown, wsl --terminate or anything that stops or restarts WSL: the user runs WSL in other sessions at the same time (user, 2026-11-18). A hook blocks it.
- CALLER CHECK (required, before editing): load Serena once with ToolSearch "select:mcp__serena__find_symbol,mcp__serena__find_referencing_symbols,mcp__serena__get_symbols_overview". For EVERY function or method whose behaviour or signature you change, run mcp__serena__find_referencing_symbols on it and account for every caller. A method declared in a header and defined in a .cpp: query BOTH files and take the union (one side can return fewer callers); get each side's exact name_path with find_symbol first ("Class/method" in the header, "Class::method" in the .cpp). Never accept an empty answer on the first try: an empty result right after startup can be an unfinished clangd index; re-run once, and if it is still empty, confirm with Grep before writing "0 callers". Serena resolves calls through macros, templates and virtuals that Grep misses. Use find_symbol / get_symbols_overview to locate symbols instead of reading whole files. Report the result in callers_checked. Only if the Serena tools are absent or error out, fall back to Grep and say so in callers_checked.
- New files with the Write tool (no heredocs). Read a file before editing it. Before creating any class or struct, grep dsp/ and plugins/ for the name (ODR). Node.js for any helper script, never Python. One git operation per command, never chained. Kit presets change only through the generator; never hand-edit a .vstpreset, never hand-copy into an install folder.
- Thresholds in the plan are final. Never relax a threshold, weaken an assertion, tag a test hidden/[.], or shrink a workload to get green. If the plan's threshold cannot be met, stop with status "blocked", blocker_kind "threshold_unmet", and the measured numbers. Nothing is removed (control, interaction, parameter, exciter, body, kit) unless the task says so and the user ruled on it.
- VERIFY-FIRST (PLAUSIBLE findings): per finding, if its new test PASSES on unchanged code, keep the test, change no production code for that finding, and record it as refuted in findings_outcome. When every finding of the task is refuted, return status "refuted" with the passing lines as red_evidence.
- Finish every step of the plan loop (1 red test, 2 fix, 3 capped build with zero warnings, 4 sharded gate GREEN, 5 pluginval + clang-tidy, plus every flag obligation) before returning "done". Return via StructuredOutput the moment you have the results.`

function rulingNote(t) {
  if (RULING_AFTER_VERIFICATION.includes(t.id)) {
    return `\n\nRULING AFTER VERIFICATION: run your VERIFY-FIRST test(s) first. If the finding is refuted, finish as refuted. If it is confirmed and the rulings table holds no Ruling for ${t.id}, stop with status "blocked", blocker_kind "needs_user": keep the verification test in the tree, change no production code, and put the measured result, the question and your recommended answer in blocker.`
  }
  if (RULING_AFTER_TABLE.includes(t.id)) {
    return `\n\nKIT RETUNE: the plan requires the user's ruling on every kit retune. Finish the task completely, then ALWAYS fill ruling_needed with the per-kit before/after table and the question "accept this retune?"; the run stops after review so the user can listen.`
  }
  return ''
}

function implPrompt(t, attempt, feedback, accepted) {
  const retry = feedback
    ? `\n\nTHIS IS ATTEMPT ${attempt}. A previous attempt did not pass. Its report and the reviewer's problems:\n${feedback}\nThe working tree still holds that attempt's UNSTAGED changes. Inspect them with git diff and git status; continue from them or revert only those unstaged hunks of this task (git checkout -- <file> restores a file to its STAGED version, which keeps earlier tasks' approved work). Fix every listed problem.`
    : ''
  const note = A.notes && A.notes[t.id] ? `\n\nNOTE FROM THE MAIN LOOP (relaunch): ${A.notes[t.id]}` : ''
  const acc = `\n\nAccepted earlier in this run (staged, count as done for prerequisites): ${accepted.length ? accepted.join(', ') : '(none)'}.`
  return `${CONTEXT}\n\nYOUR TASK: ${t.id} (fix-plan phase ${t.phase}).${acc}${note}${rulingNote(t)}${retry}\n\n${OVERRIDES}\n\n${EXEC_RULES}`
}

function reviewPrompt(t, impl) {
  return `${CONTEXT}\n\nYou are the REVIEWER for task ${t.id}. The implementer reported:\n${JSON.stringify(impl, null, 2)}\n
Read the task section in ${PLAN} (its Flags line decides which flag obligations apply; "## Flags" defines them) and the rows for its MB ids. Then inspect ONLY this task's work: \`git status --porcelain\` and \`git diff\` (unstaged) plus every untracked file in the implementer's file list. Staged changes (\`git diff --cached\`) are earlier approved tasks: ignore them.
Check, and list every violation as a concrete problem with file:line:
1. Scope: every changed production file is named by the task section or required by it (CROSS-PLUGIN consumers, STATE-COMPAT load paths and version constants, CMakeLists test registration, regenerated kits under plugins/membrum/resources/presets/ when the generator changed, refreshed plugins/membrum/images/ for UI tasks). A fix in SHARED code (dsp/include, plugins/shared, tests/test_helpers) that the section did not name is VALID when the root cause is there (user ruling 2026-11-17): accept it when deviations names the root cause with evidence, notes lists every consumer from grep -rl, and the gate ran every executable header-consumers.js named. Missing evidence is the problem, not the location. A temporary probe file (plugins/membrum/tests/audit/, its CMake line), an A/B diagnostic edit, a changed file under specs/profundum-*/evidence/, a hand-edited .vstpreset, unrelated edits, reformatting or "improvements" are problems.
2. Red first: the test(s) the section names exist with the named TEST_CASE and tags, and red_evidence is a credible failing assertion from that test on unchanged code (phase-6 refactors: the red case and the characterization case the section defines). For a refuted finding: a passing line and NO production change for that finding.
3. Nothing relaxed: no threshold looser than the plan's, no weakened or deleted assertion, no [.]/hidden tag, no shrunk workload, no test rewritten to accept the bug. Existing tests that gained only an empty process() block after a load (T1.1's new contract) with unchanged expected values are allowed. Nothing removed that the task does not name.
4. Plan conformance: the fix matches the section and any recorded Ruling in the rulings table; deviations are justified.
5. Flag obligations: flag_obligations covers every flag on the task's Flags line with result lines (CROSS-PLUGIN consumer list and gated executables; STATE-COMPAT four version constants equal and a version-5 read-path test; THREADING interleaving + stress tests and the sanitizer statement; MACOS proxies and lints; UI XSLT for bulk uidesc edits, lint-plugin-editor-baseline, uidesc inventory, screenshots; PHYSICS sources in comments and a test typed from the source); PRESET-IMPACT tasks have a real before/after table in preset_table.
6. Real-time safety on audio-thread paths (no allocation, lock, exception or I/O added), layer rules for dsp/ headers, macOS fast-math rules (detail::isFinite, no numeric_limits infinity/NaN constants, exact values by a branch or clamp).
7. Evidence: gate_line ends GATE GREEN; pluginval exit 0 when plugin sources changed; clang-tidy 0 warnings when it was required; findings_outcome has a line per MB id of the task.
8. Callers (quick check; do NOT load or run Serena yourself): for every function or method whose behaviour or signature the unstaged diff changes, run a files-only grep (grep -rlw <name> over the plugin/dsp dirs the symbol can reach) and compare the file list with the implementer's callers_checked. A file the grep lists that callers_checked does not account for must be opened and explained (a real caller, or a same-named unrelated symbol). A caller the implementer did not account for, whose contract the change breaks or leaves untested, is a problem (file:line). A missing or "none" callers_checked while production functions changed is a problem.
Do NOT edit any file and do NOT re-run builds or gates. You may run git and grep/read commands.
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
// Preflight: every selected task that needs a ruling before it starts has one recorded in the plan.
// ---------------------------------------------------------------------------
const needBefore = list.filter(t => RULING_BEFORE.includes(t.id)).map(t => t.id)
if (needBefore.length) {
  phase('Preflight')
  const pf = await agent(`Read-only check in f:/projects/iterum. In ${PLAN}, find the table under the line "Tasks that stop for the user's ruling before their commit" (inside "## Phases"; grep for that phrase, then read about 25 lines from it). Its rows have the columns Task | Question | Default this plan assumes, and may have a later "Ruling" column that the main loop fills with the user's answers. A row can name several tasks (e.g. "T5.3, T5.4, T5.5").
For each of these tasks: ${needBefore.join(', ')} decide whether a user ruling is RECORDED: the row has a Ruling column whose cell for it is non-empty and is not "None: ask", "ask", "-" or similar. The "Default this plan assumes" column is NOT a ruling. Also accept a ruling written in the task's own section as a line starting "**Ruling" (grep '^### <id> — ' and read that section's first 15 lines).
Do not edit anything. Report every task without a recorded ruling in missing (question and default verbatim), and the others in recorded.`, {
    label: 'preflight:rulings',
    phase: 'Preflight',
    schema: PREFLIGHT_SCHEMA,
    model: 'sonnet',
    agentType: 'general-purpose',
  })
  if (!pf) {
    return { phase: A.phase ?? null, stopped_at: 'preflight', reason: 'the preflight agent returned nothing; nothing was changed', not_started: list.map(t => t.id) }
  }
  if (pf.missing.length) {
    log(`PREFLIGHT: ${pf.missing.length} ruling(s) missing: ${pf.missing.map(m => m.task).join(', ')}. Nothing was changed.`)
    return {
      phase: A.phase ?? null,
      stopped_at: 'preflight',
      reason: 'rulings missing: record the user\'s answers in the plan\'s rulings table (Ruling column), then relaunch',
      missing_rulings: pf.missing,
      recorded_rulings: pf.recorded,
      not_started: list.map(t => t.id),
    }
  }
  log(`PREFLIGHT: rulings recorded for ${needBefore.join(', ')}`)
}

// ---------------------------------------------------------------------------
// One task: attempt 1 and 2 on the routed model, attempt 3 on Fable.
// needs_user / prerequisite / environment blockers stop at once: another model cannot answer them.
// ---------------------------------------------------------------------------
async function runTask(t, accepted) {
  const plan = [
    { model: t.model, phaseName: 'Implement' },
    { model: t.model, phaseName: 'Implement' },
    { model: 'fable', phaseName: 'Escalate' },
  ]
  const attempts = []
  let feedback = null
  for (let i = 0; i < plan.length; i++) {
    const a = i + 1
    const impl = await agent(implPrompt(t, a, feedback, accepted), {
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
      const ruling = String(impl.ruling_needed || '').trim()
      if (ruling) {
        log(`${t.id}: ${impl.status} on attempt ${a} (${plan[i].model}); staged ${review.staged.length} file(s); NEEDS RULING before commit`)
        return { id: t.id, outcome: 'needs_ruling', model: plan[i].model, attempts }
      }
      log(`${t.id}: ${impl.status} on attempt ${a} (${plan[i].model}); staged ${review.staged.length} file(s)`)
      return { id: t.id, outcome: impl.status, model: plan[i].model, attempts }
    }
    if (impl && impl.status === 'blocked' && ['needs_user', 'prerequisite', 'environment'].includes(impl.blocker_kind)) {
      log(`${t.id}: blocked (${impl.blocker_kind}) on attempt ${a}: ${impl.blocker}`)
      return { id: t.id, outcome: 'blocked', blocker_kind: impl.blocker_kind, model: plan[i].model, attempts }
    }
    feedback = feedbackOf(impl, review)
    log(`${t.id}: attempt ${a} (${plan[i].model}) not accepted: ${impl ? impl.status : 'no result'}${review ? `, reviewer: ${(review.problems || []).length} problem(s)` : ''}`)
  }
  return { id: t.id, outcome: 'failed', model: 'fable', attempts }
}

// ---------------------------------------------------------------------------
// Main loop: strictly serial. Stop at the first task that is not accepted (later tasks build on it; its
// unstaged changes stay in the tree for the main loop), and at a needs_ruling task (staged, awaiting the user).
// ---------------------------------------------------------------------------
const results = []
const accepted = []
let stoppedAt = null
for (const t of list) {
  phase('Implement')
  const r = await runTask(t, accepted)
  results.push(r)
  if (r.outcome === 'done' || r.outcome === 'refuted') {
    accepted.push(t.id)
    continue
  }
  stoppedAt = t.id
  const rest = list.slice(list.indexOf(t) + 1).map(x => x.id)
  log(`STOPPED at ${t.id} (${r.outcome}). Not started: ${rest.join(', ') || '(none)'}`)
  break
}

const last = r => r.attempts[r.attempts.length - 1]
const isStaged = r => r.outcome === 'done' || r.outcome === 'refuted' || r.outcome === 'needs_ruling'
return {
  phase: A.phase ?? null,
  stopped_at: stoppedAt,
  not_started: stoppedAt ? list.slice(list.findIndex(t => t.id === stoppedAt) + 1).map(t => t.id) : [],
  summary: results.map(r => ({
    id: r.id,
    outcome: r.outcome,
    accepted_on: isStaged(r) ? `attempt ${last(r).attempt} (${last(r).model})` : null,
    escalated_to_fable: r.attempts.some(a => a.model === 'fable'),
    staged: last(r).review ? last(r).review.staged : [],
    ruling_needed: last(r).impl ? (last(r).impl.ruling_needed || '') : '',
  })),
  // For the user's end-of-phase review of purposeful kit changes (ruling 2026-11-24).
  preset_tables: results
    .filter(r => isStaged(r) && last(r).impl && last(r).impl.preset_table && !/^not PRESET-IMPACT/i.test(last(r).impl.preset_table))
    .map(r => ({ id: r.id, table: last(r).impl.preset_table })),
  details: results.map(r => ({
    id: r.id,
    outcome: r.outcome,
    blocker_kind: r.blocker_kind || null,
    final: last(r).impl,
    review: last(r).review,
    earlier_attempts: r.attempts.slice(0, -1).map(a => ({ attempt: a.attempt, model: a.model, status: a.impl ? a.impl.status : null, problems: a.review ? a.review.problems : null })),
  })),
}
