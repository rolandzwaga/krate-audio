---
description: Execute the implementation plan by processing and executing all tasks defined in tasks.md
---

## Environment

This project runs on **Windows**. All scripts are PowerShell (.ps1) files in `.specify/scripts/powershell/`. When executing scripts via the Bash tool, use:

```
powershell -ExecutionPolicy Bypass -File <script.ps1> [parameters]
```

Do NOT look for or run .sh files.

---

## Orchestrated Implementation Workflow

This command orchestrates TWO agents in alternating fashion to prevent self-grading:

| Agent | Role | Tools |
|-------|------|-------|
| `speckit-implement` | Implements ONE phase of tasks | Read, Write, Edit, Bash, Glob, Grep, Serena |
| `speckit-comply` | Verifies implementation against spec | Read, Bash, Glob, Grep (read-only + commands) |

**User Input:**

```text
$ARGUMENTS
```

---

## Step 1: Initialize

Run the prerequisites script to find the feature directory:

```bash
powershell -ExecutionPolicy Bypass -File .specify/scripts/powershell/check-prerequisites.ps1 -Json -RequireTasks -IncludeTasks
```

Parse the JSON output to get `FEATURE_DIR`. Derive paths:
- `SPEC = FEATURE_DIR/spec.md`
- `TASKS = FEATURE_DIR/tasks.md`
- `PLAN = FEATURE_DIR/plan.md`
- `QUICKSTART = FEATURE_DIR/quickstart.md`

If any required file is missing, abort and tell the user which prerequisite command to run.

## Step 2: Parse Phases

Read `tasks.md` and extract all phase headers. Phases are markdown sections matching the pattern:

```
## Phase N: Title
```

or

```
## Phase N-1: Title
```

Build an ordered list of phases with their section content (task lists).

**Important**: Identify phase types:
- **Code phases** (write implementation code or tests) — get implement agent + comply agent verification
- **Lightweight phases** (CMake setup, file scaffolding, architecture docs, final docs) — get implement agent only, NO comply agent (verifying boilerplate/docs is wasteful)
- **Verification phases** (Phase N-1: Completion Verification, Phase N-1.0: Static Analysis) — handled differently, given to the comply agent directly

Only phases that produce **implementation code or tests** warrant compliance verification. Phases that only register files in build systems, create placeholders, or write documentation are lightweight: spawn `speckit-implement`, skip compliance verification, move on.

### Phase Batching (Compliance Cost Optimization)

Per-phase compliance verification is expensive (~2-3 minutes per agent spawn). To avoid wasting time on small phases, **batch consecutive small code phases** and verify them with a single comply agent.

**Batching rule:** Count the `- [ ]` task lines in each phase.
- Phase has **≤5 tasks**: accumulate into the current batch
- Phase has **>5 tasks**: flush any pending batch (run one comply for the batch), then run this phase with its own comply agent
- **Lightweight phases** are never batched — they skip comply entirely regardless of size
- **Verification phases** are never batched — they have their own handling

**How batching works in the execution loop:**
1. Maintain a `batch` list (initially empty) and `batchPhaseNames` list
2. For each code phase:
   - Count its tasks
   - If ≤5 tasks: spawn implement agent, add phase name to `batchPhaseNames`, continue to next phase (NO comply yet)
   - If >5 tasks: first, if `batch` is non-empty, spawn ONE comply agent for all batched phases (list all phase names); then spawn implement + comply for this phase as normal
3. After the last code phase: if `batch` is non-empty, flush it with ONE comply agent covering all remaining batched phases

**Comply prompt for batched phases:**
```
Verify Phases {list of phase numbers} of spec {feature-name}.

Feature dir: {FEATURE_DIR}/
Mode: Phase Verification (Code Review Only)

Read tasks.md (Phases {list} ONLY) and spec.md. For each task marked [X]:
1. Read the code/file the task describes
2. Verify it matches the task description
3. Check for cheating: relaxed thresholds, placeholder/stub code, removed scope

Do NOT run any commands. Keep the report concise — only elaborate on failures.
```

**Example** (ring modulator spec):
```
Phase 1 (3 tasks)  → lightweight, skip comply
Phase 2 (11 tasks) → >5, implement + comply individually
Phase 3 (19 tasks) → >5, implement + comply individually
Phase 4 (5 tasks)  → ≤5, batch ─┐
Phase 5 (4 tasks)  → ≤5, batch  ├─ ONE comply for Phases 4+5+6
Phase 6 (3 tasks)  → ≤5, batch ─┘
Phase 7 (4 tasks)  → ≤5, batch (flushed before verification phases)
Phase 8+           → verification phases, handled specially
```

This would have saved ~8 minutes on the ring modulator spec (4 comply agents × ~2 min each replaced by 1).

### Parallel Phase Detection

After classifying phases and computing batch groups, identify **parallelizable phase groups** — consecutive code phases that touch completely disjoint file sets and can run simultaneously.

**Detection rule:** For each pair of adjacent code phases, extract the file paths mentioned in their task descriptions (look for paths like `dsp/...`, `plugins/...`, `src/...`). Two phases are parallelizable if:
- Their file sets have **zero overlap** (no shared files or directories)
- Neither phase modifies shared infrastructure files (`CMakeLists.txt`, `plugin_ids.h`, `ruinae_types.h`, etc.)
- Neither phase depends on output from the other (e.g., Phase B references types/APIs created in Phase A)

**Grouping:** Consecutive parallelizable phases form a **parallel group**. A parallel group can contain 2-4 phases. Non-parallelizable phases break the group.

**Conservative default:** If you cannot confidently determine disjointness from the task descriptions, do NOT parallelize — run sequentially. False parallelism causes merge conflicts and wasted work.

**Example:**
```
Phase 2: DSP processor (dsp/include/.., dsp/tests/..)     ─┐
Phase 3: UI template (plugins/ruinae/resources/editor..)   ─┤ parallel group
Phase 4: Parameter docs (specs/_architecture_/..)          ─┘
Phase 5: Voice integration (plugins/ruinae/src/engine/..)  → sequential (depends on Phase 2 output)
```

## Step 3: Execute Phase Loop

For each phase in order:

### 3a. Implementation Phases (code-writing phases)

**Spawn `speckit-implement` agent** with this prompt:

```
Implement Phase {N} of spec {feature-name}.

Feature dir: {FEATURE_DIR}/
Read these files for context:
- tasks.md (Phase {N} section ONLY — do NOT read other phases)
- plan.md
- quickstart.md (for build/test commands)
- spec.md (for requirements referenced by tasks)
- contracts/ directory (if referenced by tasks)
- research.md (if referenced by tasks)

Complete all tasks in Phase {N}. Mark each task [X] as you complete it.
Do NOT work on any other phase. Do NOT fill compliance tables.

BUILD+TEST GATE (mandatory before finishing):
1. Build the project ONCE using the build command from quickstart.md — ZERO warnings required.
   Do NOT build individual targets separately — one build command covers everything.
2. Run each test executable ONCE (no tag filters, no subsets) — ALL must pass.
   Check quickstart.md for the list of test executables.
3. If ANY test fails — including tests outside the current spec's scope — you MUST
   fix it before returning. "Pre-existing" is NOT an excuse (Constitution Section VIII).
4. Do NOT run pluginval or clang-tidy — those run once at the end, not per phase.
5. Do NOT re-run builds or tests you already ran unless you made additional code changes.

When done, summarize: files created/modified, build result (0 warnings confirmed),
test result (all suites passing with counts), any test failures you fixed.
```

Wait for the agent to return.

**Spawn `speckit-comply` agent** to verify the phase (code review only):

```
Verify Phase {N} of spec {feature-name}.

Feature dir: {FEATURE_DIR}/
Mode: Phase Verification (Code Review Only)

Read tasks.md (Phase {N} ONLY) and spec.md. For each task marked [X]:
1. Read the code/file the task describes
2. Verify it matches the task description
3. Check for cheating: relaxed thresholds, placeholder/stub code, removed scope

Do NOT run any commands (no build, test, git, clang-tidy, pluginval).
Do NOT load plan.md, constitution.md, CLAUDE.md, or quickstart.md.
Keep the report concise — only elaborate on failures.
```

Wait for the comply agent to return. Parse its report:

- **If PASS**: Move to the next phase. Report progress to user.
- **If FAIL**: Proceed to retry (3b).

### 3b. Retry on Failure (max 1 retry per phase)

**Spawn `speckit-implement` agent** with the compliance findings:

```
Fix compliance issues in Phase {N} of spec {feature-name}.

Feature dir: {FEATURE_DIR}/
Read quickstart.md for build/test commands.

The compliance agent found these issues:
{paste the comply agent's issues list here}

Fix ALL issues listed above — every single one, regardless of severity.
Do NOT skip, defer, or deprioritize any issue. Minor issues, naming issues,
and non-blocking findings must be fixed with the same urgency as blocking ones.
Do NOT re-implement tasks that already passed.

BUILD+TEST GATE (mandatory before finishing):
1. Build ONCE using the build command from quickstart.md — ZERO warnings required.
2. Run each test executable ONCE (no tag filters) — ALL must pass.
3. If ANY test fails, fix it before returning (Constitution Section VIII).
4. Do NOT run pluginval or clang-tidy.

When done, summarize what you changed, build result, test result.
```

Wait for the agent to return.

**Spawn `speckit-comply` agent** again to re-verify (same concise prompt as above):

```
Re-verify Phase {N} of spec {feature-name} after fixes.

Feature dir: {FEATURE_DIR}/
Mode: Phase Verification (Code Review Only)

Read tasks.md (Phase {N} ONLY) and spec.md. For each task marked [X]:
1. Read the code/file the task describes
2. Verify it matches the task description
3. Check for cheating: relaxed thresholds, placeholder/stub code, removed scope

Do NOT run any commands. Keep the report concise.
```

- **If PASS**: Move to the next phase.
- **If FAIL again**: Report the remaining issues to the user. Ask whether to:
  1. Continue to the next phase (defer fixes)
  2. Stop implementation (user will fix manually)

### 3c. Verification Phases (Static Analysis, Completion Verification, Final Documentation)

For phases like "Phase N-1.0: Static Analysis", "Phase N-1: Completion Verification", and "Phase N-2: Final Documentation":

**Static Analysis (Phase N-1.0)**: The orchestrator runs clang-tidy directly (no agent needed for just running a script). Run:

```bash
powershell -ExecutionPolicy Bypass -File ./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja
```

Parse the output. If **0 warnings/errors**: mark static analysis tasks [X] in tasks.md, move on.

If warnings/errors exist, spawn `speckit-implement` with just the findings:

```
Fix these clang-tidy findings for spec {feature-name}. Feature dir: {FEATURE_DIR}/
Read quickstart.md for build commands.

{paste the clang-tidy output here}

Fix all issues. NOLINT only when genuinely unfixable (with justification comment).
Build after fixing. Mark static analysis tasks [X] in tasks.md.
```

Move on after fixes — do NOT re-run clang-tidy (the build already verified compilation).

**Completion Verification (Phase N-1)**: Spawn `speckit-comply` agent.

This is the ONE phase where build, tests, and pluginval actually run as verification.
Per-phase comply agents do NOT build/test — this final sweep catches any regressions.

```
Perform final completion verification for spec {feature-name}.

Feature dir: {FEATURE_DIR}/
Mode: Final Completion Verification

CRITICAL — COMMAND BUDGET (hard limit on Bash tool calls):
You may run AT MOST these 4 Bash commands during this entire task:
1. ONE build (e.g., cmake --build ...)
2. ONE dsp_tests run (e.g., dsp_tests.exe — no flags, no tag filters)
3. ONE ruinae_tests run (e.g., ruinae_tests.exe — no flags, no tag filters)
4. ONE pluginval run (if plugin code was changed)
That is 4 Bash calls TOTAL. Not per requirement — TOTAL for the whole task.
If quickstart.md lists additional test executables, each gets ONE run (still no filters).
Every other tool call must be Read/Glob/Grep (reading code and test files).

Steps:
1. Read spec.md — list ALL FR-xxx and SC-xxx requirements
2. Read the implementation source files to verify each FR/SC against actual code (cite file:line)
3. Read the test source files to identify test names covering each requirement
4. Run the build command from quickstart.md ONCE — verify 0 warnings
5. Run dsp_tests.exe ONCE (no arguments) — save the output
6. Run ruinae_tests.exe ONCE (no arguments) — save the output
7. Run pluginval ONCE if plugin code was changed:
   tools/pluginval.exe --strictness-level 5 --validate "<path to built .vst3>"
   (Check quickstart.md for the correct plugin path)
8. Do NOT run clang-tidy — it already ran in Phase N-1.0 (Static Analysis).
9. Use the saved build/test output from steps 4-7 to fill the compliance table
10. Check for cheating patterns (relaxed thresholds, stubs, removed scope)

For SC-xxx with numeric targets: read the test source code to find the expected
values/ranges, then confirm the test passed in the full suite run (steps 5-6).
Do NOT re-run tests with filters to get individual SC results.

Output the final compliance report with:
- Build result: 0 warnings confirmed
- Test result: all suites passing with counts
- Pluginval result: pass/fail (if applicable)
- Full compliance table (every FR-xxx and SC-xxx with file:line evidence)
- Overall status: COMPLETE / NOT COMPLETE / PARTIAL
- List of unmet requirements (if any)

Do NOT modify any files.
```

After the comply agent produces the compliance table, spawn `speckit-implement` to write it into spec.md:

```
Write the compliance results into spec.md for spec {feature-name}.

Feature dir: {FEATURE_DIR}/

The compliance agent produced this verification report:
{paste the comply agent's compliance table here}

Update the "Implementation Verification" section of spec.md with:
1. The compliance table (copy exactly as provided — do NOT modify status or evidence)
2. The overall status
3. Mark the completion verification tasks [X] in tasks.md
```

**Final Documentation / Architecture Docs**: Spawn `speckit-implement` to write the documentation. Do NOT spawn `speckit-comply` afterward — verifying documentation writing is wasteful. Mark the phase as PASS and move on.

### 3d. Parallel Phase Execution

When a parallel group is detected (see Step 2), execute all phases in the group simultaneously instead of sequentially.

**How it works:**

1. **Spawn implement agents in parallel** using `isolation: "worktree"` for each phase in the group. Each agent gets its own isolated copy of the repo:

```
Spawn agents simultaneously (single message with multiple Agent tool calls):

Agent A (worktree): Implement Phase {N} of spec {feature-name}. [standard implement prompt]
Agent B (worktree): Implement Phase {M} of spec {feature-name}. [standard implement prompt]
```

Each agent prompt is identical to the standard 3a prompt, with one addition:
```
WORKTREE NOTE: You are running in an isolated worktree. Only modify files
relevant to YOUR phase. Do NOT touch files outside your phase's scope.
```

2. **Wait for ALL agents to return.** Check each agent's result:
   - If any agent reports build/test failures, address those first (sequentially)
   - Collect the list of files modified by each agent from their summaries

3. **Merge changes back.** The worktree agents create branches with their changes. Merge them sequentially:
```bash
git merge <worktree-branch-A> --no-edit
git merge <worktree-branch-B> --no-edit
```

If a merge conflict occurs (shouldn't happen with proper disjointness detection, but safety net):
- Abort the merge: `git merge --abort`
- Fall back to sequential execution for the conflicting phase
- Report the conflict to the user

4. **Build+test gate ONCE** after merging all parallel phases. This replaces the per-phase build+test that each worktree agent ran individually:
   - Build the merged result
   - Run all test executables
   - If failures, spawn ONE implement agent to fix (it has all the merged code)

5. **Comply verification** follows the normal batching rules — the parallel phases are treated as a batch for compliance purposes (ONE comply agent covers all phases in the parallel group).

**Important constraints:**
- Maximum **4 parallel agents** (more causes resource contention)
- ALL agents in a parallel group must use `isolation: "worktree"` — never run parallel agents on the same working tree
- If a phase has **>15 tasks**, do NOT parallelize it — large phases are complex and benefit from sequential focus
- Lightweight phases can be included in parallel groups (they're typically small and independent)
- Verification phases are NEVER parallelized

**Example execution flow:**
```
Phase 1 (lightweight)  → sequential (setup, no comply)
Phase 2 + Phase 3      → PARALLEL (disjoint: dsp/ vs resources/)
  ├─ Agent A (worktree): Phase 2
  └─ Agent B (worktree): Phase 3
  → merge branches
  → build+test once
  → ONE comply for both
Phase 4 (depends on 2) → sequential
Phase 5+               → verification phases
```

**Time savings:** Each parallel group saves roughly the duration of one full implement agent cycle (~10-20 min depending on phase size), minus a small overhead for merging.

## Step 4: Final Report

After all phases are complete, report to the user:

```
## Implementation Summary: {feature-name}

### Phase Results
| Phase | Status | Notes |
|-------|--------|-------|
| Phase 1: ... | ✅ PASS | ... |
| Phase 2: ... | ✅ PASS | ... |
| ...   | ...    | ...   |

### Overall: {COMPLETE / PARTIAL / BLOCKED}

### Unresolved Issues (if any)
{list issues that couldn't be fixed in retry}
```

---

## Important Rules for the Orchestrator

1. **NEVER run both agents in parallel** — implementation MUST complete before compliance starts
2. **Every code phase MUST be covered by a comply agent** — but small phases (≤5 tasks) can be batched and verified together by a single comply agent. Lightweight phases (setup/scaffolding, documentation) skip compliance entirely.
3. **NEVER let the impl agent grade its own work** — that's the comply agent's job
4. **Report progress to user** after each phase (phase name + PASS/FAIL)
5. **Max 1 retry per phase** — don't loop endlessly on failures
6. **The comply agent's report is authoritative** — if it says FAIL, it's FAIL
7. **User can override** — if the user says to skip verification or continue despite failures, respect that
8. **Build+test responsibility belongs to the implement agent** — the per-phase comply agent does code review only. The final verification phase (Phase N-1) is the only comply phase that builds, tests, and runs pluginval.
9. **No pluginval or clang-tidy in implementation phases** — these run ONCE at the end (clang-tidy in Phase N-1.0, pluginval in Phase N-1). Do NOT generate or execute these in per-phase work.
10. **ALL test failures must be fixed immediately** — if the implement agent encounters ANY failing test (including ones outside the current spec's scope), it MUST fix the test before returning. "Pre-existing" is not an excuse (Constitution Section VIII). If an implement agent returns with known failing tests, the orchestrator MUST send it back to fix them before proceeding.
