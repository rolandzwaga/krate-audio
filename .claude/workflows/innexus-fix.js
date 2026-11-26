export const meta = {
  name: 'innexus-fix',
  description: 'Execute one phase of the Innexus audit fix plan with independent tasks running at the same time in git worktree lanes: a preflight loads the task records from the briefs (specs/innexus-audit/tasks/), each lane has its own warm build tree, tasks that touch different functions run together (red test, fix, capped build, sharded gate, pluginval, tidy), a Sonnet reviewer checks each diff and commits it in the lane, an integrator merges each wave into the main checkout, runs the merged gate and leaves the result STAGED. Two reds escalate once to Fable. Nothing is committed on the audit branch.',
  whenToUse: 'When executing the Innexus audit fix plan one phase at a time, after .claude/workflows/innexus-audit.js has written specs/innexus-audit/fix-plan.md and one brief per task under specs/innexus-audit/tasks/. args: { phase: N, tasks?: ["T1.3", ...] (run only these), start_at?: "T1.7" (skip tasks before it), skip?: ["T1.9"], lanes?: 1-4 (default 3: how many tasks run at once), notes?: {"T1.7": "context for a relaunch"} }. It runs only on a FINISHED audit: the preflight stops, before anything changes, when report.md or fix-plan.md is missing, a part folder is left, a plan task has no brief or a MISSING marker is present. The main checkout must have no uncommitted tracked changes when the run starts (commit the previous run\'s staged work first). Record the user\'s answers in the plan\'s "Tasks that stop for the user\'s ruling" table (Ruling column): a task that needs a ruling before it starts and has none is held, with everything that depends on it, while the rest of the phase runs. Accepted work ends up STAGED in the main checkout; the main loop commits. A task that waits for the user to accept its result stays on a branch innexus-fix/<task> in its lane.',
  phases: [
    { title: 'Preflight', detail: 'task records from the briefs, recorded rulings, a clean main checkout, tasks already committed', model: 'sonnet' },
    { title: 'Lanes', detail: 'one git worktree with its own warm build tree per lane', model: 'sonnet' },
    { title: 'Implement', detail: 'up to <lanes> independent tasks at once, each in its lane, on the model its brief names' },
    { title: 'Review', detail: 'diff vs brief, red-first evidence, no relaxed thresholds, flag obligations; commits the task in its lane', model: 'sonnet' },
    { title: 'Integrate', detail: 'per wave: cherry-pick into the main checkout, regenerate artifacts, merged build + gate; left staged', model: 'opus' },
    { title: 'Escalate', detail: 'a task red twice gets one Fable attempt', model: 'fable' },
  ],
}

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
const MAIN = 'f:/projects/iterum'
const PLAN = 'specs/innexus-audit/fix-plan.md'
const TASKS_DIR = 'specs/innexus-audit/tasks'
const REPORT = 'specs/innexus-audit/report.md'
const LANE_ROOT = 'f:/wt/innexus-fix'
const LOG_ROOT = 'f:/tmp/innexus-fix'
const CMAKE = '"C:/Program Files/CMake/bin/cmake.exe"'
const BUILD_DIR = 'build/windows-x64-release'
const GATE_FILTER = "'~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]'"
const A = (args && typeof args === 'object') ? args : {}
const LANES = Math.min(4, Math.max(1, Number(A.lanes) || 3))
const TIDY_JOBS = LANES >= 4 ? 3 : 4
const lanePath = n => `${LANE_ROOT}/lane${n}`

// ---------------------------------------------------------------------------
// Schemas
// ---------------------------------------------------------------------------
const TASK_RECORD = {
  type: 'object',
  required: ['task_id', 'phase', 'title', 'finding_ids', 'prerequisites', 'flags', 'model', 'exclusive', 'ruling', 'ruling_question', 'estimate_min', 'touches', 'creates'],
  properties: {
    task_id: { type: 'string' },
    phase: { type: 'integer' },
    title: { type: 'string' },
    finding_ids: { type: 'array', items: { type: 'string' } },
    prerequisites: { type: 'array', items: { type: 'string' } },
    flags: { type: 'array', items: { type: 'string' } },
    model: { type: 'string', enum: ['opus', 'sonnet'] },
    exclusive: { type: 'boolean' },
    ruling: { type: 'string', enum: ['none', 'before', 'after_verification', 'after_result'] },
    ruling_question: { type: 'string' },
    estimate_min: { type: 'number' },
    touches: { type: 'array', items: { type: 'object', required: ['file', 'symbols'], properties: { file: { type: 'string' }, symbols: { type: 'array', items: { type: 'string' } } } } },
    creates: { type: 'array', items: { type: 'string' } },
  },
}

const PREFLIGHT_SCHEMA = {
  type: 'object',
  required: ['tool_ok', 'tool_output', 'tasks', 'clean', 'dirty', 'head', 'committed', 'rulings'],
  properties: {
    tool_ok: { type: 'boolean', description: 'node tools/audit-fix-tasks.js exited 0' },
    tool_output: { type: 'string', description: 'when it did not exit 0: its stderr lines verbatim; otherwise ""' },
    tasks: { type: 'array', items: TASK_RECORD, description: 'the task records the tool printed, copied field for field' },
    clean: { type: 'boolean', description: 'the main checkout has no staged or unstaged change to a tracked file' },
    dirty: { type: 'array', items: { type: 'string' }, description: 'the git status lines of tracked files when not clean' },
    head: { type: 'string', description: 'git rev-parse HEAD of the main checkout (full sha)' },
    committed: { type: 'array', items: { type: 'string' }, description: 'task ids (selected tasks and their prerequisites) that an Innexus commit on this branch already names' },
    rulings: {
      type: 'array',
      description: 'one entry per selected task whose record has ruling other than "none"',
      items: {
        type: 'object',
        required: ['task', 'recorded', 'ruling'],
        properties: {
          task: { type: 'string' },
          recorded: { type: 'boolean' },
          ruling: { type: 'string', description: 'the Ruling cell verbatim when recorded, else ""' },
        },
      },
    },
  },
}

const LANE_SCHEMA = {
  type: 'object',
  required: ['lane', 'ready', 'problem', 'summary'],
  properties: {
    lane: { type: 'integer' },
    ready: { type: 'boolean', description: 'the worktree exists, is clean, is configured with both system-install options OFF, and innexus_tests + Innexus are built' },
    problem: { type: 'string', description: 'what stopped the setup, with the failing command and its last output lines; "" when ready' },
    summary: { type: 'string', description: 'what was created vs already there, the build wall clock, the log paths' },
  },
}

const IMPL_SCHEMA = {
  type: 'object',
  properties: {
    status: { type: 'string', enum: ['done', 'refuted', 'blocked', 'failed'] },
    blocker_kind: { type: 'string', enum: ['none', 'needs_user', 'prerequisite', 'threshold_unmet', 'scope', 'environment', 'other'] },
    red_evidence: { type: 'string', description: 'step 1: the failing assertion line(s) of the new test on UNCHANGED production code, verbatim, with the command that produced them; for refuted: the passing line' },
    gate_line: { type: 'string', description: 'the verbatim last line of the run-gate.js command (GATE GREEN ... / GATE RED ...), plus the header-consumers.js last line when it ran' },
    pluginval: { type: 'string', description: 'exit code and verdict, or "not required" with the reason' },
    tidy: { type: 'string', description: 'clang-tidy targets run and their Errors/Warnings totals, or "not required"' },
    findings_outcome: { type: 'string', description: 'one line per INX id of the task: fixed (file:line, test name, the finding\'s own symptom re-measured with numbers) or refuted (test name, passing output)' },
    flag_obligations: { type: 'string', description: 'per flag on the task, what its obligations required and the verbatim result lines (CROSS-PLUGIN consumer grep + executables gated; STATE-COMPAT the four readers / writers and the reader-parity test; THREADING sanitizer runs; MACOS proxies and lints; UI screenshots and uidesc check; LITERATURE sources in comments and the test). "none" when the task has no flag' },
    preset_table: { type: 'string', description: 'PRESET-IMPACT: the before/after audibility table (per affected preset: peak, rms, tail, centroid, limiter share, inert features that appeared or disappeared), or "not PRESET-IMPACT"' },
    ruling_needed: { type: 'string', description: 'empty when none. Otherwise the question the user must rule on before this task is merged, with the evidence (table, verification result) and your recommended answer' },
    extra_checks: { type: 'string', description: 'probes re-run, ASan, generator runs: what ran and the verbatim result lines' },
    files: { type: 'array', items: { type: 'string' }, description: 'every repo file this attempt changed or created (repo-relative), regenerated artifacts included; never a temporary probe file' },
    touched_outside_record: { type: 'string', description: 'every function you edited that the task record\'s touches does not name, with the reason; "none" when you stayed inside the record' },
    tests_added: { type: 'array', items: { type: 'string' }, description: 'file :: TEST_CASE name for every new or rewritten test' },
    deviations: { type: 'string', description: 'everything done differently from the brief, and why; "none" if none' },
    blocker: { type: 'string', description: 'for blocked/failed: what stops the task, with measured numbers; for needs_user the question and your recommended answer' },
    notes: { type: 'string', description: 'facts the main loop needs for the commit message (consumers per CROSS-PLUGIN, presets regenerated and why, sanitizer runs left to CI)' },
    callers_checked: { type: 'string', description: 'for every function/method whose behaviour or signature this attempt changed: "Symbol (file): N callers via find_referencing_symbols -> caller list; how each is covered (unchanged contract / updated / tested)". Or "Serena unavailable: Grep used" with the grep commands. "none" only when no function changed (tests, data, docs only).' },
  },
  required: ['status', 'blocker_kind', 'red_evidence', 'gate_line', 'findings_outcome', 'flag_obligations', 'preset_table', 'ruling_needed', 'files', 'touched_outside_record', 'deviations', 'notes', 'callers_checked'],
}

const REVIEW_SCHEMA = {
  type: 'object',
  properties: {
    ok: { type: 'boolean' },
    problems: { type: 'array', items: { type: 'string' }, description: 'each concrete problem with file:line; a commit the hooks blocked is a problem (quote the hook output); empty when ok' },
    commit: { type: 'string', description: 'the full sha of the commit you made in the lane (only when ok), else ""' },
    files: { type: 'array', items: { type: 'string' }, description: 'the files in that commit (only when ok)' },
    summary: { type: 'string' },
  },
  required: ['ok', 'problems', 'commit', 'files', 'summary'],
}

const INTEGRATE_SCHEMA = {
  type: 'object',
  properties: {
    integrated: { type: 'array', items: { type: 'string' }, description: 'task ids whose changes are now staged in the main checkout' },
    rejected: {
      type: 'array',
      description: 'tasks whose pick was backed out: an unresolvable conflict, or the merged gate went red because of it',
      items: { type: 'object', required: ['task', 'reason'], properties: { task: { type: 'string' }, reason: { type: 'string', description: 'what conflicted or which merged case failed, with file:line, so the task can be redone on the new base' } } },
    },
    snapshot: { type: 'string', description: 'the full sha that holds the main checkout\'s state after this wave (git stash create, or HEAD when nothing is staged); the next wave starts from it' },
    main_ok: { type: 'boolean', description: 'the main checkout builds and its merged gate is GREEN with everything that is staged now' },
    gate_line: { type: 'string', description: 'the verbatim last line of the merged run-gate.js run, or why it did not run' },
    pluginval: { type: 'string', description: 'exit code and verdict of the merged bundle, or "not required"' },
    conflicts: { type: 'array', items: { type: 'string' }, description: 'each conflict met: file, the tasks involved, how it was resolved' },
    summary: { type: 'string' },
  },
  required: ['integrated', 'rejected', 'snapshot', 'main_ok', 'gate_line', 'pluginval', 'conflicts', 'summary'],
}

// ---------------------------------------------------------------------------
// Prompts
// ---------------------------------------------------------------------------
const CONTEXT = `Repository: ${MAIN} (Krate Audio monorepo, VST3 SDK + VSTGUI, C++20). Plugin: Innexus (plugins/innexus/), a harmonic analysis / resynthesis instrument that consumes the shared KrateDSP library (dsp/include/krate/dsp/), plugins/shared/ and the preset generator tools/innexus_preset_generator.cpp + tools/innexus_preset_format.h.
The Innexus audit (${REPORT}) produced a fix plan (${PLAN}) and ONE BRIEF PER TASK under ${TASKS_DIR}/. This run executes tasks of that plan IN PARALLEL: up to ${LANES} at a time, each in its own git worktree ("lane") with its own build tree, on a base that holds every task accepted so far. A reviewer commits each finished task in its lane; an integrator then merges the lane commits into the main checkout, where they are left staged for the user.`

function laneFacts(n) {
  const L = lanePath(n)
  return `YOUR LANE: ${L} (lane ${n} of ${LANES}). It is a full checkout of the repository with its own build tree ${L}/${BUILD_DIR} and its own compile database ${L}/build/windows-ninja. EVERYTHING you do happens there:
- Files: Read / Edit / Write / Grep / Glob with paths under ${L.replace(/\//g, '\\')}\\ (Windows backslashes for those tools). NEVER edit, stage or build anything under ${MAIN}: that is the main checkout, other agents and the integrator own it.
- Shell commands: the shell does not stay in the lane between calls, so start every command with  cd "${L}" &&  (git may use  git -C "${L}"  instead). Run the repo's tools as the lane's own copies (node tools/...), never ${MAIN}/tools/....
- Other lanes run other tasks right now. They edit other functions, possibly in the same files as you. Stay inside the functions your task record names.`
}

const READ_FIRST = n => `READ FIRST (in your lane):
1. Your brief ${lanePath(n)}/${TASKS_DIR}/<task id>.md, whole. It is self-contained: the task record (json block at the top), the findings with their evidence, the code anchors, the failing test, the fix, the acceptance check and the scope. Do not read other tasks' briefs.
2. In ${lanePath(n)}/${PLAN}: the sections "## How to execute a task", "## Flags" and "## Parallel execution" (find each with Grep for its heading and read that section only). The plan's step commands name the main checkout's paths and caps; the LANE OVERRIDES below replace them.
Open ${REPORT} only where your brief sends you to a specific section. Anchors carry line numbers from the audited commit; earlier tasks moved lines. Find the quoted snippet with Grep; never edit by line number alone.`

const overrides = n => {
  const L = lanePath(n)
  return `LANE OVERRIDES OF THE PLAN (these win over the plan text and the brief):
- Step 6 "Commit" is REPLACED: do NOT commit, do NOT stage (no git add), do NOT stash, amend or push. Leave your changes uncommitted in the lane; a reviewer commits them there and an integrator merges them.
- Build, capped and without MSBuild node reuse:  cd "${L}" && ${CMAKE} --build ${BUILD_DIR} --config Release --target <targets> -- -m:2 /nr:false  > ${LOG_ROOT}/<task>/build.log 2>&1   (the generator target generate_innexus_presets and an ASan build use the same cap). The lane is configured with -DVSTWORK_COPY_TO_VST3_FOLDER=OFF -DVSTWORK_INSTALL_PRESETS=OFF: its build never writes a system folder, and must not be reconfigured otherwise.
- Gate, with this run's share of the machine:  cd "${L}" && node tools/run-gate.js --share ${LANES} --filter ${GATE_FILTER} --out ${LOG_ROOT}/<task>/gate <exes>  (the plan's filter plus ~[long]~[.]). It runs the cases your uncommitted changes reach. Always pass --out: the default log directory is shared with the other lanes. An audit-probe procedure ('[audit-probe]' filter) and a [long] case your task adds run by their own filter or name, once.
- pluginval:  ${MAIN}/tools/pluginval.exe --strictness-level 5 --validate "${L}/${BUILD_DIR}/VST3/Release/Innexus.vst3"  (the lane's bundle; the exit code is the verdict).
- clang-tidy:  cd "${L}" && pwsh -NoProfile -File ./tools/run-clang-tidy.ps1 -Target <innexus|dsp|shared> -BuildDir build/windows-ninja -Jobs ${TIDY_JOBS}  once per target your diff touches. It analyzes only your uncommitted changes; never ask for a whole-target sweep (a hook blocks that). Phantom warnings from a stale compile database: reconfigure the lane's ninja preset once, then re-run.
- Timing cases ([performance], [perf], [benchmark]) and tools/run-cpu-tests.js need an idle machine. Run them only if your task record says "exclusive": true (then no other lane is working); otherwise leave them out and say so in notes.
- Generated artifacts: before you return, run  cd "${L}" && node tools/gen-repo-map.js --check ,  node tools/gen-symbols.js --check  and  node tools/gen-specs-index.js --check ; regenerate each stale one (the same command without --check) and list it in files. The reviewer's commit is blocked while one is stale.
- After any run of dsp_systems_tests:  git -C "${L}" status --porcelain specs/  and restore every changed file under specs/profundum-*/evidence/ with  git -C "${L}" checkout -- <those paths>  (those cases rewrite their evidence CSVs). They are never part of your files.
- Temporary audit-probe files (plugins/innexus/tests/audit/ and the CMake line) and every A/B diagnostic edit are removed before you return;  git -C "${L}" status --porcelain  must then show only the task's own files.
- SCOPE (this is what makes parallel work safe): edit only the functions and files your task record's touches / creates name. "+" means you only ADD lines there (one source-list line, a new member, a new case). If the fix needs a function the record does not name: when it is a small, clearly necessary edit, make it and report it in touched_outside_record (the integrator then checks it against the other lanes); when it is more than that, return status "blocked", blocker_kind "scope", naming the functions, without leaving half a fix behind. Never reformat, reorder or tidy code you are not fixing: every changed line that is not the fix is a merge conflict with another lane.
- If the root cause is in SHARED code (dsp/include, plugins/shared, tests/test_helpers), fix it there when your record's touches name that file. Do the CROSS-PLUGIN IMPACT obligations: grep -rl the header across plugins/, dsp/include and tests/, list every consumer in notes, build and gate every executable header-consumers.js names. If the record does not name the shared file, that is a scope blocker: return it, do not edit shared code unannounced.
- A red gate case your change cannot reach: run that one case by name once. Green alone + a wall-clock assertion = load from the other lanes: record it in notes. Red alone = it is yours to fix, or status "failed" with the evidence. Never call a failure "pre-existing" and move on.
- PRESET-IMPACT: measure the audibility probe before and after as the flag says and put the table in preset_table. An audible change that IS the finding's stated purpose does not stop the task: leave ruling_needed empty; the user reviews every table at the end of the run. Only a change the user would hear as a different preset BEYOND the finding's stated purpose goes into ruling_needed, after the task is finished (gate green, pluginval, tidy).`
}

const EXEC_RULES = `EXECUTION RULES:
- ONE heavy job at a time IN YOUR LANE: your build, gate, pluginval, clang-tidy, generator, ASan build and WSL tools never overlap each other. The other lanes run theirs at the same time; the caps above are what keeps the machine usable, never widen them.
- Every long command runs in the FOREGROUND (Bash timeout 600000) with stdout+stderr redirected to a log under ${LOG_ROOT}/<task id>/, then tail that log. Read a failing shard's log from the gate --out directory; never re-run a slow command to re-read its output.
- NEVER use run_in_background, the Monitor tool, sleep, or polling. NEVER run find /c, find /, du or any disk-wide walk; never cat a whole log. Never run tools/clean-test-output.js.
- WSL tools (macos-fastmath-repro.sh, check-portability.js, a TSan build) may be used, but NEVER run wsl --shutdown, wsl --terminate or anything that stops or restarts WSL: the user and the other lanes use WSL at the same time. A hook blocks it.
- CALLER CHECK (required, before editing): load Serena once with ToolSearch "select:mcp__serena__find_symbol,mcp__serena__find_referencing_symbols,mcp__serena__get_symbols_overview". Serena indexes the MAIN checkout, whose code equals your lane's base: use it to look callers up, then edit in your lane. For EVERY function or method whose behaviour or signature you change, run mcp__serena__find_referencing_symbols on it and account for every caller. A method declared in a header and defined in a .cpp: query BOTH files and take the union; get each side's exact name_path with find_symbol first. Never accept an empty answer on the first try: re-run once, and if it is still empty, confirm with Grep in your lane before writing "0 callers". Report the result in callers_checked. Only if the Serena tools are absent or error out, fall back to Grep and say so.
- New files with the Write tool (no heredocs). Read a file before editing it. Before creating any class or struct, grep dsp/ and plugins/ for the name (ODR). Node.js for any helper script, never Python. One git operation per command, never chained. Factory presets change only through the generator; never hand-edit a .vstpreset, never copy anything into an install folder.
- Thresholds in the brief are final. Never relax a threshold, weaken an assertion, tag a test hidden/[.], or shrink a workload to get green. If a threshold cannot be met, stop with status "blocked", blocker_kind "threshold_unmet", and the measured numbers. Nothing is removed (control, interaction, parameter, preset, feature) unless the brief says so and the user ruled on it. The mono harmonic sieve is never disabled globally and nothing may inflate per-partial bandwidth.
- VERIFY-FIRST (PLAUSIBLE findings): per finding, if its new test PASSES on unchanged code, keep the test, change no production code for that finding, and record it as refuted in findings_outcome. When every finding of the task is refuted, return status "refuted" with the passing lines as red_evidence.
- Finish every step of the plan loop (1 red test, 2 fix, 3 capped build with zero warnings, 4 sharded gate GREEN, 5 pluginval + clang-tidy, the generated artifacts in sync, plus every flag obligation) before returning "done". Return via StructuredOutput the moment you have the results.`

function rulingNote(t, recorded) {
  if (recorded) return `\n\nRULING RECORDED BY THE USER for ${t.task_id} (final: build this option, never ask again): ${recorded}`
  if (t.ruling === 'after_verification') {
    return `\n\nRULING AFTER VERIFICATION (question: ${t.ruling_question}): run your VERIFY-FIRST test(s) first. If the finding is refuted, finish as refuted. If it is confirmed, no ruling is recorded for ${t.task_id}: stop with status "blocked", blocker_kind "needs_user": keep the verification test in the lane, change no production code, and put the measured result, the question and your recommended answer in blocker.`
  }
  if (t.ruling === 'after_result') {
    return `\n\nRULING AFTER THE RESULT (question: ${t.ruling_question}): finish the task completely, then ALWAYS fill ruling_needed with the evidence (the before/after table) and the question; the task then waits on its lane branch for the user instead of being merged.`
  }
  return ''
}

function implPrompt(t, n, base, attempt, feedback, ctx) {
  const L = lanePath(n)
  const prepare = attempt === 1 && !ctx.redo
    ? `PREPARE THE LANE (first, one command at a time): (1)  git -C "${L}" status --porcelain  — if it lists anything, the lane holds leftovers of an earlier task that is already committed or abandoned: discard them with  git -C "${L}" reset --hard  then  git -C "${L}" clean -fd  (build/ is ignored and survives). (2)  git -C "${L}" checkout --detach ${base}  — your base: the main checkout with every task accepted so far. (3)  git -C "${L}" submodule update --init --recursive  (normally a no-op). (4)  mkdir -p ${LOG_ROOT}/${t.task_id} .`
    : ctx.redo
      ? `THIS TASK IS BEING REDONE ON A NEW BASE. Its earlier commit ${ctx.redo.commit} passed review but was backed out when the wave was merged: ${ctx.redo.reason}\nPREPARE THE LANE (one command at a time): (1)  git -C "${L}" reset --hard  then  git -C "${L}" clean -fd . (2)  git -C "${L}" checkout --detach ${base}  — the new base, which now holds the tasks yours collided with. (3)  git -C "${L}" cherry-pick -n ${ctx.redo.commit}  to bring your earlier work back as uncommitted changes (resolve what conflicts, keeping the other tasks' code), then  git -C "${L}" reset  so nothing is staged. (4) Re-run the whole loop on this base: the red test must still be red without your fix, and the gate green with it.`
      : `THIS IS ATTEMPT ${attempt}. A previous attempt did not pass. Its report and the reviewer's problems:\n${feedback}\nThe lane still holds that attempt's uncommitted changes. Inspect them with  git -C "${L}" status --porcelain  and  git -C "${L}" diff ; continue from them or restore single files with  git -C "${L}" checkout -- <file> . Fix every listed problem.`
  const note = A.notes && A.notes[t.task_id] ? `\n\nNOTE FROM THE MAIN LOOP (relaunch): ${A.notes[t.task_id]}` : ''
  const done = `\n\nYour base already contains these tasks (they count as done for prerequisites): ${ctx.done.length ? ctx.done.join(', ') : '(none)'}. Your prerequisites: ${t.prerequisites.length ? t.prerequisites.join(', ') : '(none)'}.`
  return `${CONTEXT}\n\nYOUR TASK: ${t.task_id} — ${t.title} (fix-plan phase ${t.phase}).\nTASK RECORD: ${JSON.stringify(t)}${done}${note}${rulingNote(t, ctx.ruling)}\n\n${laneFacts(n)}\n\n${prepare}\n\n${READ_FIRST(n).replace('<task id>', t.task_id)}\n\n${overrides(n).replace(/<task>/g, t.task_id)}\n\n${EXEC_RULES}`
}

function reviewPrompt(t, n, base, impl) {
  const L = lanePath(n)
  return `${CONTEXT}\n\nYou are the REVIEWER for task ${t.task_id} — ${t.title}, which was implemented in lane ${n}: ${L}. The lane's base is ${base}; everything uncommitted there is this task's work and nothing else.\nTASK RECORD: ${JSON.stringify(t)}\nThe implementer reported:\n${JSON.stringify(impl, null, 2)}\n
Read the brief ${L}/${TASKS_DIR}/${t.task_id}.md and, in ${L}/${PLAN}, the "## Flags" section (it defines the obligations of the flags on the task record). Then inspect the work:  git -C "${L}" status --porcelain  and  git -C "${L}" diff  plus every untracked file in the implementer's file list. Use paths under ${L} for every read; never read or touch ${MAIN}.
Check, and list every violation as a concrete problem with file:line:
1. Scope: every changed function is named by the task record's touches (a "+" entry allows added lines only) or justified in touched_outside_record as a small necessary edit; every new file is in creates or is a test the brief names. Other lanes edit other functions of the same files right now: a reformatted, reordered or "improved" region outside the fix, a moved function, a whitespace-only hunk are problems because they collide at merge. A fix in SHARED code (dsp/include, plugins/shared, tests/test_helpers) must be named by the record; then notes must list every consumer from grep -rl and the gate must have run every executable header-consumers.js named. A temporary probe file (plugins/innexus/tests/audit/, its CMake line), an A/B diagnostic edit, a changed file under specs/profundum-*/evidence/, a hand-edited .vstpreset are problems. Regenerated specs/_architecture_/symbols.json, repo-map.json and specs/INDEX.md are expected when the task added, renamed or removed a type or a spec.
2. Red first: the test(s) the brief names exist with the named TEST_CASE and tags, and red_evidence is a credible failing assertion from that test on unchanged code. For a refuted finding: a passing line and NO production change for that finding.
3. Nothing relaxed: no threshold looser than the brief's, no weakened or deleted assertion, no [.]/hidden tag, no shrunk workload, no test rewritten to accept the bug. Nothing removed that the brief does not name. No test reads a file from outside the repository.
4. Plan conformance: the fix matches the brief and the recorded ruling the implementer was given; deviations are justified.
5. Flag obligations: flag_obligations covers every flag of the record with result lines (CROSS-PLUGIN consumer list and gated executables; STATE-COMPAT: getState, setState, Controller::readComponentState and tools/innexus_preset_format.h changed together, new fields appended at the end or version-gated, a reader-parity test, presets regenerated when a default or the layout changed; THREADING interleaving + stress tests and the sanitizer statement; MACOS proxies and lints; UI XSLT for bulk uidesc edits, lint-plugin-editor-baseline, uidesc inventory, screenshots; LITERATURE sources in comments and a test typed from the source); PRESET-IMPACT tasks have a real before/after table in preset_table.
6. Real-time safety on audio-thread paths (no allocation, lock, exception or I/O added; nothing freed on the audio thread), layer rules for dsp/ headers, macOS fast-math rules (detail::isFinite, no numeric_limits infinity/NaN constants, exact values by a branch or clamp), the load-bearing constraints (the mono harmonic sieve not disabled globally, per-partial bandwidth not inflated).
7. Evidence: gate_line ends GATE GREEN; pluginval exit 0 when plugin sources changed; clang-tidy 0 warnings when it was required; findings_outcome has a line per INX id of the task.
8. Callers (quick check; do NOT load or run Serena yourself): for every function or method whose behaviour or signature the diff changes, run a files-only grep in the lane (grep -rlw <name> over the plugin/dsp dirs the symbol can reach) and compare the file list with the implementer's callers_checked. A caller the implementer did not account for, whose contract the change breaks or leaves untested, is a problem (file:line). A missing or "none" callers_checked while production functions changed is a problem.
Do NOT edit any file and do NOT re-run builds or gates. You may run git and grep/read commands.
If and only if there are no problems, COMMIT THE TASK IN THE LANE, one command at a time: (1) stage exactly this task's files:  git -C "${L}" add -- <file> <file> ...  (the changed tracked files plus the new files that belong to the task; never git add -A or git add .). (2) Write the commit message with the Write tool to ${LOG_ROOT}/${t.task_id}/commit-msg.txt : first line "fix(innexus): audit ${t.task_id}, <the title, shortened to fit 72 characters>", a blank line, then one line per INX id with what was fixed or refuted. (3)  git -C "${L}" commit -F ${LOG_ROOT}/${t.task_id}/commit-msg.txt  — the commit hooks then check the lane's tree (generated artifacts, lints, GCC portability). If a hook BLOCKS the commit, do not work around it: report ok=false with the hook's output as the problem (the implementer fixes it). (4)  git -C "${L}" branch -f innexus-fix/${t.task_id} HEAD  (a name for this commit). (5)  git -C "${L}" rev-parse HEAD  -> commit. Never amend, never push, never commit anywhere else.
Report ok=true with the commit sha and its file list, or ok=false, commit "", and the problems.`
}

function feedbackOf(impl, review) {
  const parts = []
  if (impl) parts.push(`IMPLEMENTER REPORT:\n${JSON.stringify(impl, null, 2)}`)
  else parts.push('IMPLEMENTER: returned nothing (agent died or was skipped).')
  if (review) parts.push(`REVIEWER PROBLEMS:\n- ${(review.problems || []).join('\n- ') || '(none)'}`)
  return parts.join('\n\n')
}

// ---------------------------------------------------------------------------
// Preflight: the task records come from the briefs, through the repo's own tool.
// ---------------------------------------------------------------------------
const phaseNum = Number(A.phase)
const wantTasks = Array.isArray(A.tasks) && A.tasks.length ? A.tasks.map(String) : null
if (!wantTasks && !(phaseNum >= 1)) throw new Error('args.phase must be a phase number (or pass args.tasks). Example: {"phase": 1}')
const selector = wantTasks ? `--tasks ${wantTasks.join(',')}` : `--phase ${phaseNum}`

phase('Preflight')
const pf = await agent(`Read-only check in ${MAIN} (the main checkout of the Krate Audio monorepo). Run each command separately and report exactly what it shows; edit nothing.
1. TASK RECORDS:  cd "${MAIN}" && node tools/audit-fix-tasks.js ${TASKS_DIR} --complete ${selector}  . It validates every task brief, checks that the audit which wrote them ran to its end (--complete: report and plan joined, no part folders left, every plan task with a brief, no MISSING marker), and prints one line of JSON, {"tasks":[...]}. Do not drop --complete and do not repair anything it reports. Exit code 0: copy every task record into tasks, field for field, changing nothing (ids, paths and symbol names are matched by exact text later). Any other exit code: tool_ok=false, tasks=[], and its stderr lines verbatim in tool_output; then skip steps 3 and 4.
2. CLEAN CHECKOUT:  git -C "${MAIN}" status --porcelain --untracked-files=no  . No output = clean. Otherwise list the lines in dirty. Also  git -C "${MAIN}" rev-parse HEAD  -> head.
3. ALREADY COMMITTED: for every task id in the records AND every id on their prerequisites lines, check whether an Innexus commit on this branch names it:  git -C "${MAIN}" log --oneline -i --all-match --grep "innexus" --grep "<id as a regular expression with the dot escaped, followed by a word boundary, e.g. T1\\.3\\b>" origin/main..HEAD  (the branch also carries Membrum audit commits that use the same T<phase>.<n> ids: only a commit whose message says innexus counts; read the subject lines to be sure the id is a whole word, T1.3 is not T1.31). List the ids that have such a commit in committed.
4. RULINGS: in ${MAIN}/${PLAN} find the table under the line "Tasks that stop for the user's ruling" (inside "## Phases"; grep for that phrase and read the table). Its columns are Task | When | Question | Default this plan assumes | Ruling; a row can name several tasks. For every task record whose "ruling" is not "none", report whether a user ruling is RECORDED: the Ruling cell for it is non-empty and is not "ask", "-", "None" or similar. The "Default this plan assumes" column is NOT a ruling. Also accept a line starting "**Ruling" in the task's own brief (${MAIN}/${TASKS_DIR}/<id>.md, first 40 lines). Give the ruling text verbatim.`, {
  label: 'preflight',
  phase: 'Preflight',
  schema: PREFLIGHT_SCHEMA,
  model: 'sonnet',
  agentType: 'general-purpose',
})
const stopEarly = (reason, extra = {}) => ({ phase: A.phase ?? null, stopped_at: 'preflight', reason, nothing_changed: true, ...extra })
if (!pf) return stopEarly('the preflight agent returned nothing')
// The fix runs only on a FINISHED audit with valid briefs; the tool's --complete check decides, not an agent's reading.
if (!pf.tool_ok) return stopEarly(`the Innexus audit is not finished or its task briefs do not validate (node tools/audit-fix-tasks.js ${TASKS_DIR} --complete lists why): let the audit finish, or rerun its synthesis, then relaunch`, { tool_output: pf.tool_output })
if (!pf.clean) return stopEarly('the main checkout has uncommitted changes to tracked files: commit (or set aside) them first, the lanes start from a commit and the merged result is staged on top of it', { dirty: pf.dirty })
if (!/^[0-9a-f]{40}$/.test(String(pf.head).trim())) return stopEarly(`the preflight did not return a commit sha for HEAD: ${pf.head}`)

const ID_RE = /^T(\d+)\.(\d+)$/
const badRecords = pf.tasks.filter(t => !ID_RE.test(t.task_id) || !Array.isArray(t.touches) || !Array.isArray(t.creates) || !Array.isArray(t.prerequisites)).map(t => String(t.task_id))
if (badRecords.length) return stopEarly(`malformed task records came back from the preflight: ${badRecords.join(', ')}`)
if (wantTasks) {
  const missing = wantTasks.filter(id => !pf.tasks.some(t => t.task_id === id))
  if (missing.length) return stopEarly(`unknown task ids: ${missing.join(', ')}`)
} else if (pf.tasks.some(t => t.phase !== phaseNum)) {
  return stopEarly(`the preflight returned tasks outside phase ${phaseNum}`)
}
const idOrder = (a, b) => { const x = ID_RE.exec(a.task_id), y = ID_RE.exec(b.task_id); return x[1] - y[1] || x[2] - y[2] }
let list = [...pf.tasks].sort(idOrder)
const committed = new Set((pf.committed || []).map(String))
const alreadyDone = list.filter(t => committed.has(t.task_id)).map(t => t.task_id)
if (alreadyDone.length) log(`Already committed, skipped: ${alreadyDone.join(', ')}`)
list = list.filter(t => !committed.has(t.task_id))
if (A.start_at) {
  const i = list.findIndex(t => t.task_id === String(A.start_at))
  if (i < 0) return stopEarly(`start_at ${A.start_at} is not in the selected list`)
  if (i > 0) log(`start_at ${A.start_at}: skipping ${list.slice(0, i).map(t => t.task_id).join(', ')}`)
  list = list.slice(i)
}
const skip = Array.isArray(A.skip) ? A.skip.map(String) : []
if (skip.length) {
  log(`Skipping by request: ${skip.join(', ')}`)
  list = list.filter(t => !skip.includes(t.task_id))
}
if (!list.length) return stopEarly('nothing to do: every selected task is already committed or skipped', { already_committed: alreadyDone })
const rulingOf = new Map((pf.rulings || []).filter(r => r.recorded && String(r.ruling).trim()).map(r => [r.task, String(r.ruling).trim()]))

// ---------------------------------------------------------------------------
// Scheduling: which tasks may run at the same time.
// Two tasks collide when they create the same file or edit the same (file, symbol); "*" is the whole file,
// "+" (lines added at a shared place) never collides. Colliding tasks run in task-id order.
// ---------------------------------------------------------------------------
const collide = (a, b) => {
  if (a.creates.some(f => b.creates.includes(f))) return true
  return a.touches.some(x => b.touches.some(y => y.file === x.file && (x.symbols.includes('*') || y.symbols.includes('*') || x.symbols.some(s => s !== '+' && y.symbols.includes(s)))))
}
// pending | accepted | parked (waits for the user on its lane branch) | blocked | failed | held (waits for one of those)
const state = new Map(list.map(t => [t.task_id, 'pending']))
const held = new Map()
const results = new Map()
const redo = new Map() // task id -> { commit, reason }: backed out at merge, to be redone on the new base
const redone = new Set()
const done = () => [...committed, ...list.filter(t => state.get(t.task_id) === 'accepted').map(t => t.task_id)]
const isDone = id => committed.has(id) || state.get(id) === 'accepted'
const inList = id => state.has(id)

for (const t of list) {
  if (t.ruling === 'before' && !rulingOf.has(t.task_id)) {
    state.set(t.task_id, 'blocked')
    results.set(t.task_id, { id: t.task_id, outcome: 'blocked', blocker_kind: 'needs_user', blocker: `no ruling recorded: ${t.ruling_question}`, attempts: [] })
    log(`${t.task_id}: needs a ruling before it starts and none is recorded: ${t.ruling_question}`)
  }
}

// A pending task that can no longer run: a prerequisite, or an earlier task it collides with, did not get accepted.
function settleHeld() {
  let changed = true
  while (changed) {
    changed = false
    for (const t of list) {
      if (state.get(t.task_id) !== 'pending') continue
      const stuck = s => s && s !== 'pending' && s !== 'accepted'
      const by = t.prerequisites.find(p => (inList(p) ? stuck(state.get(p)) : !committed.has(p)))
        || (list.slice(0, list.indexOf(t)).find(u => stuck(state.get(u.task_id)) && collide(t, u)) || {}).task_id
      if (by) {
        state.set(t.task_id, 'held')
        held.set(t.task_id, by)
        changed = true
      }
    }
  }
}

function nextWave() {
  settleHeld()
  const wave = []
  for (const t of list) {
    if (wave.length >= LANES) break
    if (state.get(t.task_id) !== 'pending') continue
    if (!t.prerequisites.every(isDone)) continue
    // An earlier task that edits the same code goes first.
    if (list.slice(0, list.indexOf(t)).some(u => state.get(u.task_id) === 'pending' && collide(t, u))) continue
    if (t.exclusive) {
      if (wave.length === 0) return [t]
      continue
    }
    if (wave.every(w => !collide(t, w))) wave.push(t)
  }
  return wave
}

// ---------------------------------------------------------------------------
// Lanes: long-lived worktrees, each with its own build tree (only the first build in a lane is cold).
// ---------------------------------------------------------------------------
phase('Lanes')
log(`innexus-fix: ${list.length} task(s), ${LANES} lane(s): ${list.map(t => `${t.task_id}(${t.model}${t.exclusive ? ', alone' : ''})`).join(' ')}`)
let base = String(pf.head).trim()
const laneSetup = await parallel(
  Array.from({ length: Math.min(LANES, list.length) }, (_, i) => i + 1).map(n => () =>
    agent(`You prepare lane ${n}: a git worktree of ${MAIN} at ${lanePath(n)} in which a coding agent will fix one task at a time, with its own build tree. Other agents prepare the other lanes at the same time, so keep to the caps. Capture every long command to a log under ${LOG_ROOT}/lanes/lane${n}/ (create it) and read the log; run in the FOREGROUND (Bash timeout 600000); never use run_in_background, sleep or polling. One git operation per command. Never touch a file under ${MAIN} except through git worktree.
1. WORKTREE. If  git -C "${MAIN}" worktree list  does not list ${lanePath(n)}:  git -C "${MAIN}" worktree add --detach "${lanePath(n)}" ${base}  (create ${LANE_ROOT} first). If it is listed:  git -C "${lanePath(n)}" status --porcelain ; leftovers of an earlier run are discarded with  git -C "${lanePath(n)}" reset --hard  then  git -C "${lanePath(n)}" clean -fd  (build/ is ignored and survives; this is the only place where a reset is allowed), then  git -C "${lanePath(n)}" checkout --detach ${base} .
2. SUBMODULES:  git -C "${lanePath(n)}" submodule update --init --recursive  (the VST3 SDK; a first run downloads it).
3. CONFIGURE, only when ${lanePath(n)}/${BUILD_DIR}/CMakeCache.txt is missing or does not hold both VSTWORK_COPY_TO_VST3_FOLDER:BOOL=OFF and VSTWORK_INSTALL_PRESETS:BOOL=OFF:  cd "${lanePath(n)}" && ${CMAKE} --preset windows-x64-release -DVSTWORK_COPY_TO_VST3_FOLDER=OFF -DVSTWORK_INSTALL_PRESETS=OFF  . Both must be OFF: a lane build must never write the system VST3 or preset folders.
4. BUILD (cold the first time, incremental afterwards), capped at two MSBuild nodes without node reuse:  cd "${lanePath(n)}" && ${CMAKE} --build ${BUILD_DIR} --config Release --target innexus_tests Innexus -- -m:2 /nr:false  . Zero errors is required; record the wall clock.
5. COMPILE DATABASE for clang-tidy, only when ${lanePath(n)}/build/windows-ninja/compile_commands.json is missing: configure the ninja preset in the lane as .claude/skills/clang-tidy-setup/SKILL.md describes (${CMAKE} --list-presets shows the name), with the same two options OFF.
Report ready=true only when all five are in place; otherwise the failing command and its last output lines in problem.`, {
      label: `lane${n}:setup`,
      phase: 'Lanes',
      schema: LANE_SCHEMA,
      model: 'sonnet',
      agentType: 'build-test-runner',
    }),
  ),
)
const lanes = laneSetup.map((r, i) => ({ n: i + 1, ready: !!(r && r.ready), problem: r ? r.problem : 'the setup agent returned nothing' })).filter(l => l.ready).map(l => l.n)
const laneProblems = laneSetup.map((r, i) => (r && r.ready ? null : `lane ${i + 1}: ${r ? r.problem : 'the setup agent returned nothing'}`)).filter(Boolean)
if (laneProblems.length) log(`Lane setup problems: ${laneProblems.join(' | ')}`)
if (!lanes.length) return stopEarly('no lane could be prepared', { lane_problems: laneProblems, nothing_changed: false })

// ---------------------------------------------------------------------------
// One task in one lane: attempt 1 and 2 on the model its record names, attempt 3 on Fable.
// needs_user / prerequisite / scope / environment blockers stop at once: another model cannot answer them.
// ---------------------------------------------------------------------------
async function runTask(t, n, taskBase) {
  const plan = [
    { model: t.model, phaseName: 'Implement' },
    { model: t.model, phaseName: 'Implement' },
    { model: 'fable', phaseName: 'Escalate' },
  ]
  const attempts = []
  const ctx = { done: done(), ruling: rulingOf.get(t.task_id) || '', redo: redo.get(t.task_id) || null }
  let feedback = null
  for (let i = 0; i < plan.length; i++) {
    const a = i + 1
    const impl = await agent(implPrompt(t, n, taskBase, a, feedback, i === 0 ? ctx : { ...ctx, redo: null }), {
      label: `impl:${t.task_id}:lane${n}:a${a}:${plan[i].model}`,
      phase: plan[i].phaseName,
      schema: IMPL_SCHEMA,
      model: plan[i].model,
      agentType: 'general-purpose',
    })
    let review = null
    if (impl && (impl.status === 'done' || impl.status === 'refuted')) {
      review = await agent(reviewPrompt(t, n, taskBase, impl), {
        label: `review:${t.task_id}:lane${n}:a${a}`,
        phase: 'Review',
        schema: REVIEW_SCHEMA,
        model: 'sonnet',
        agentType: 'general-purpose',
      })
    }
    attempts.push({ attempt: a, model: plan[i].model, impl, review })
    if (review && review.ok && /^[0-9a-f]{40}$/.test(String(review.commit).trim())) {
      const ruling = String(impl.ruling_needed || '').trim()
      log(`${t.task_id}: ${impl.status} on attempt ${a} (${plan[i].model}) in lane ${n}; commit ${String(review.commit).slice(0, 9)}${ruling ? '; WAITS FOR A RULING, not merged' : ''}`)
      return { id: t.task_id, outcome: ruling ? 'parked' : impl.status, lane: n, commit: String(review.commit).trim(), model: plan[i].model, attempts }
    }
    if (impl && impl.status === 'blocked' && ['needs_user', 'prerequisite', 'scope', 'environment'].includes(impl.blocker_kind)) {
      log(`${t.task_id}: blocked (${impl.blocker_kind}) on attempt ${a}: ${impl.blocker}`)
      return { id: t.task_id, outcome: 'blocked', blocker_kind: impl.blocker_kind, blocker: impl.blocker, lane: n, model: plan[i].model, attempts }
    }
    feedback = feedbackOf(impl, review)
    log(`${t.task_id}: attempt ${a} (${plan[i].model}) not accepted: ${impl ? impl.status : 'no result'}${review ? `, reviewer: ${(review.problems || []).length} problem(s)` : ''}`)
  }
  return { id: t.task_id, outcome: 'failed', lane: n, model: 'fable', attempts }
}

// ---------------------------------------------------------------------------
// Main loop: wave by wave. A wave's tasks run together, one per lane, on the same base; then one integrator
// merges their commits into the main checkout and gates the merged result. The next wave starts from that.
// ---------------------------------------------------------------------------
const last = r => r.attempts[r.attempts.length - 1] || {}
const waves = []
let stopped = null
for (let w = 1; ; w++) {
  const wave = nextWave().slice(0, lanes.length)
  if (!wave.length) break
  phase('Implement')
  log(`Wave ${w}: ${wave.map((t, i) => `${t.task_id} -> lane ${lanes[i]}`).join(', ')} (base ${base.slice(0, 9)})`)
  const ran = await parallel(wave.map((t, i) => () => runTask(t, lanes[i], base)))
  const ready = []
  ran.forEach((r, i) => {
    const t = wave[i]
    const res = r || { id: t.task_id, outcome: 'failed', lane: lanes[i], attempts: [] }
    results.set(t.task_id, res)
    if (res.outcome === 'done' || res.outcome === 'refuted') ready.push({ t, res })
    else state.set(t.task_id, res.outcome === 'parked' ? 'parked' : res.outcome === 'blocked' ? 'blocked' : 'failed')
  })
  waves.push({ wave: w, base, tasks: wave.map(t => t.task_id) })
  if (!ready.length) continue

  phase('Integrate')
  const integ = await agent(`${CONTEXT}\n\nYou are the INTEGRATOR for wave ${w}. Lanes are idle while you work, so the machine is yours; you work in the MAIN checkout ${MAIN} and nowhere else. It currently holds, STAGED, every task accepted in earlier waves; you add this wave's tasks to that and leave everything staged. You never commit, never push, never amend, never touch a lane's files. One git operation per command; long commands in the FOREGROUND (Bash timeout 600000) with output captured under ${LOG_ROOT}/wave${w}/ (create it), then read the log.

THIS WAVE'S TASKS, in the order to merge them (each was reviewed and committed in its lane on base ${base}; they were scheduled together because their records say they edit different functions):
${ready.map(({ t, res }) => `- ${t.task_id} — ${t.title}\n  commit ${res.commit} (lane ${res.lane}); record touches: ${JSON.stringify(t.touches)}; creates: ${JSON.stringify(t.creates)}; edited outside the record: ${last(res).impl ? last(res).impl.touched_outside_record : 'unknown'}; flags: ${t.flags.join(', ') || 'none'}; brief: ${TASKS_DIR}/${t.task_id}.md`).join('\n')}

PROCEDURE:
1. Before each task, note a restore point:  git -C "${MAIN}" stash create  prints a sha that holds the checkout's current state (no output = nothing staged yet: the restore point is then HEAD, ${String(pf.head).trim()}). It changes nothing.
2. Apply the task without committing:  git -C "${MAIN}" cherry-pick -n <its commit> .
3. CONFLICTS. The generated files specs/_architecture_/symbols.json, specs/_architecture_/repo-map.json and specs/INDEX.md: take either side (git -C "${MAIN}" checkout --theirs -- <file>, then git add it); they are regenerated in step 5. Lines ADDED by two tasks at the same place (a CMake source list, class members, parameter cases, includes): keep both. A real overlap inside a function: read both briefs' "2. Fix" and "Scope" sections and both diffs (git -C "${MAIN}" show <commit> -- <file>) and merge so that BOTH fixes hold; if you cannot be certain, back this task out (step 7) and reject it with the exact file:line, so it is redone on top of the others. Stage every resolved file with git add.
4. After the last task: nothing may be left unmerged ( git -C "${MAIN}" status --porcelain  shows no "UU" / "AA" lines) and everything is staged.
5. Regenerate the artifacts in the main checkout and stage them:  cd "${MAIN}" && node tools/gen-repo-map.js ,  node tools/gen-symbols.js ,  node tools/gen-specs-index.js , then git add the three files that changed.
6. MERGED BUILD AND GATE (the tasks were green alone; this proves them together):  cd "${MAIN}" && ${CMAKE} --build ${BUILD_DIR} --config Release --target innexus_tests Innexus <plus the dsp_<layer>_tests / other test targets of every shared file this wave changed: node tools/header-consumers.js --tag '[innexus]' names the executables> -- -m:4 /nr:false  — zero errors and zero warnings; then  cd "${MAIN}" && node tools/run-gate.js --filter ${GATE_FILTER} --out ${LOG_ROOT}/wave${w}/gate innexus_tests <those executables>  — it runs the cases the staged changes reach; quote its last line in gate_line. When plugin sources changed:  ${MAIN}/tools/pluginval.exe --strictness-level 5 --validate "${MAIN}/${BUILD_DIR}/VST3/Release/Innexus.vst3"  (exit code is the verdict). A build of the main checkout may fail only on its post-build copy into a system folder: that is not an error.
7. A RED MERGED RESULT (build error, a red case, pluginval): find which task of THIS wave causes it (run the red case by name; read the failing assertion against each task's diff). A wall-clock assertion that is green when run alone is load, not a defect: say so. If two tasks' fixes interact and the repair is small and certain (both are right, a shared expected value or a merge slip needs adjusting), repair it in the main checkout, stage it, and describe it in conflicts. Otherwise BACK THE CULPRIT OUT: return the checkout to the restore point you noted before that task with  git -C "${MAIN}" read-tree -u --reset <restore point sha>  (it restores index and files to exactly that state and keeps HEAD), then re-apply the LATER tasks of this wave (step 2) and repeat steps 5 and 6. Report the task in rejected with the failing case and file:line. Never weaken a test, relax a threshold or delete a fix to get green, and never leave the main checkout red: if it stays red with every task of this wave backed out, report main_ok=false and stop.
8. Finally:  git -C "${MAIN}" status --porcelain  must show only staged changes. Then  git -C "${MAIN}" stash create  -> snapshot (the full sha; if it prints nothing, use  git -C "${MAIN}" rev-parse HEAD ), and keep it reachable:  git -C "${MAIN}" update-ref refs/innexus-fix/base <snapshot> .
Report integrated (task ids staged now), rejected, snapshot, main_ok, the gate line, pluginval and every conflict.`, {
    label: `integrate:wave${w}`,
    phase: 'Integrate',
    schema: INTEGRATE_SCHEMA,
    model: 'opus',
    agentType: 'general-purpose',
  })
  const okSha = integ && /^[0-9a-f]{40}$/.test(String(integ.snapshot).trim())
  if (!integ || !integ.main_ok || !okSha) {
    stopped = { wave: w, reason: !integ ? 'the integrator returned nothing: the state of the main checkout is unknown, inspect it before relaunching' : !integ.main_ok ? `the main checkout is not green after merging: ${integ.summary}` : `the integrator returned no usable snapshot sha: ${integ.snapshot}`, integrator: integ || null, tasks_of_wave: ready.map(x => x.t.task_id) }
    for (const { t } of ready) state.set(t.task_id, 'failed')
    log(`STOPPED in wave ${w}: ${stopped.reason}`)
    break
  }
  base = String(integ.snapshot).trim()
  waves[waves.length - 1].integration = { integrated: integ.integrated, rejected: integ.rejected, gate_line: integ.gate_line, pluginval: integ.pluginval, conflicts: integ.conflicts, snapshot: base }
  for (const { t, res } of ready) {
    const rej = (integ.rejected || []).find(x => x.task === t.task_id)
    if (!rej && (integ.integrated || []).includes(t.task_id)) {
      state.set(t.task_id, 'accepted')
      redo.delete(t.task_id)
    } else if (!redone.has(t.task_id)) {
      // Backed out at merge: once more, on the new base, with the integrator's reason. It stays pending.
      redone.add(t.task_id)
      redo.set(t.task_id, { commit: res.commit, reason: rej ? rej.reason : 'the integrator did not report this task as integrated' })
      log(`${t.task_id}: backed out at merge, queued again on the new base: ${rej ? rej.reason : 'not reported as integrated'}`)
    } else {
      state.set(t.task_id, 'failed')
      results.set(t.task_id, { ...res, outcome: 'failed', merge_rejection: rej ? rej.reason : 'not integrated' })
      log(`${t.task_id}: backed out at merge a second time: failed`)
    }
  }
}
settleHeld()

// ---------------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------------
const idsIn = s => list.filter(t => state.get(t.task_id) === s).map(t => t.task_id)
const detail = id => {
  const r = results.get(id) || { attempts: [] }
  const f = last(r)
  return {
    id,
    state: state.get(id),
    outcome: r.outcome || null,
    lane: r.lane || null,
    commit: r.commit || null,
    accepted_on: r.commit ? `attempt ${f.attempt} (${f.model})` : null,
    escalated_to_fable: r.attempts.some(a => a.model === 'fable'),
    redone_after_merge: redone.has(id),
    blocker_kind: r.blocker_kind || null,
    blocker: r.blocker || r.merge_rejection || null,
    ruling_needed: f.impl ? (f.impl.ruling_needed || '') : '',
    held_by: held.get(id) || null,
    final: f.impl || null,
    review: f.review || null,
    earlier_attempts: r.attempts.slice(0, -1).map(a => ({ attempt: a.attempt, model: a.model, status: a.impl ? a.impl.status : null, problems: a.review ? a.review.problems : null })),
  }
}
return {
  phase: A.phase ?? null,
  lanes: lanes.length,
  lane_problems: laneProblems,
  stopped: stopped,
  // Staged in the main checkout, merged and gated together: the main loop reviews and commits these.
  staged_in_main: idsIn('accepted'),
  // Finished and committed on a lane branch, waiting for the user's ruling before it is merged.
  waiting_for_ruling: idsIn('parked').map(id => ({ id, branch: `innexus-fix/${id}`, lane: (results.get(id) || {}).lane, bundle: `${lanePath((results.get(id) || {}).lane)}/${BUILD_DIR}/VST3/Release/Innexus.vst3`, question: detail(id).ruling_needed })),
  blocked: idsIn('blocked').map(id => ({ id, kind: detail(id).blocker_kind, blocker: detail(id).blocker })),
  failed: idsIn('failed'),
  // Not started: they wait for the task named, which was not accepted.
  held: idsIn('held').map(id => ({ id, waits_for: held.get(id) })),
  not_started: idsIn('pending'),
  already_committed: alreadyDone,
  waves: waves,
  // For the user's review of purposeful preset changes.
  preset_tables: list
    .filter(t => ['accepted', 'parked'].includes(state.get(t.task_id)))
    .map(t => ({ id: t.task_id, table: (last(results.get(t.task_id) || { attempts: [] }).impl || {}).preset_table || '' }))
    .filter(x => x.table && !/^not PRESET-IMPACT/i.test(x.table)),
  details: list.map(t => detail(t.task_id)),
}
