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

**Important**: Identify which phases are "implementation phases" (write code) vs "verification phases" (Phase N-1: Completion Verification, Phase N-1.0: Static Analysis, Phase N-2: Final Documentation). The verification phases are handled differently — they are given to the comply agent directly.

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

**Spawn `speckit-comply` agent** to verify the phase (code review only — no build/test):

```
Verify Phase {N} of spec {feature-name} implementation.

Feature dir: {FEATURE_DIR}/
Mode: Phase Verification (Code Review Only)

Read these files:
- tasks.md (Phase {N} — check all tasks are marked [X])
- spec.md (FR-xxx and SC-xxx requirements covered by this phase)
- plan.md (architecture decisions that should be reflected)

Then verify (CODE REVIEW ONLY — do NOT build, run tests, run clang-tidy, or run pluginval):
1. Check every task marked [X] in Phase {N} — read the actual code to verify work was done
2. For each FR-xxx/SC-xxx covered by this phase: read the code, cite file:line evidence
3. Check for constitution violations (XVI cheating, XIII test-first if TDD specified)
4. Verify the implement agent reported a clean build (0 warnings) and all tests passing

Do NOT build the project, run tests, run clang-tidy, or run pluginval. The implement agent
already did build+test as a mandatory gate. Your job is independent code review only.

Output the compliance report. Do NOT modify any files.
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

Fix ONLY the issues listed above. Do NOT re-implement tasks that already passed.

BUILD+TEST GATE (mandatory before finishing):
1. Build ONCE using the build command from quickstart.md — ZERO warnings required.
2. Run each test executable ONCE (no tag filters) — ALL must pass.
3. If ANY test fails, fix it before returning (Constitution Section VIII).
4. Do NOT run pluginval or clang-tidy.

When done, summarize what you changed, build result, test result.
```

Wait for the agent to return.

**Spawn `speckit-comply` agent** again to re-verify (same Code Review Only prompt as above):

```
Re-verify Phase {N} of spec {feature-name} after fixes.

{same Code Review Only Phase Verification prompt as before — no build/test/clang-tidy/pluginval}
```

- **If PASS**: Move to the next phase.
- **If FAIL again**: Report the remaining issues to the user. Ask whether to:
  1. Continue to the next phase (defer fixes)
  2. Stop implementation (user will fix manually)

### 3c. Verification Phases (Static Analysis, Completion Verification, Final Documentation)

For phases like "Phase N-1.0: Static Analysis", "Phase N-1: Completion Verification", and "Phase N-2: Final Documentation":

**Static Analysis (Phase N-1.0)**: Spawn `speckit-comply` agent directly:

```
Run static analysis for spec {feature-name}.

Feature dir: {FEATURE_DIR}/
Mode: Static Analysis

Steps:
1. Run clang-tidy: ./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja
2. Record all findings (file count, errors, warnings)
3. If warnings exist: list them ALL with file paths and line numbers

Output the static analysis report.
Do NOT modify any files — only report findings.
```

If clang-tidy finds issues, spawn `speckit-implement` to fix them:

```
Fix clang-tidy findings for spec {feature-name}.

Feature dir: {FEATURE_DIR}/

The static analysis found these issues:
{paste clang-tidy findings here}

Fix ALL warnings and errors. Use NOLINT with documented justification ONLY when a warning
is genuinely unfixable (e.g., Highway macro patterns). Build after fixing to verify.
Mark the static analysis tasks [X] in tasks.md when done.
```

Then re-run comply to verify the fixes.

**Completion Verification (Phase N-1)**: Spawn `speckit-comply` agent.

This is the ONE phase where build, tests, and pluginval actually run as verification.
Per-phase comply agents do NOT build/test — this final sweep catches any regressions.

```
Perform final completion verification for spec {feature-name}.

Feature dir: {FEATURE_DIR}/
Mode: Final Completion Verification

Read quickstart.md for build and test commands.

Steps:
1. Read spec.md — list ALL FR-xxx and SC-xxx requirements
2. For EACH requirement individually:
   a. Find the implementation code — cite file:line
   b. Find the test — cite test name and actual result
   c. For SC-xxx with numeric targets — run/read actual values, compare against spec
3. Build the project using commands from quickstart.md — verify 0 warnings
4. Run full test suite — ALL tests must pass (Constitution Section VIII)
5. Run pluginval if plugin code was changed:
   tools/pluginval.exe --strictness-level 5 --validate "<path to built .vst3>"
   (Check quickstart.md for the correct plugin path)
6. Check for cheating patterns (relaxed thresholds, stubs, removed scope)
7. Produce the full compliance table with REAL evidence

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

**Final Documentation (Phase N-2)**: Spawn `speckit-implement` as normal (architecture docs are implementation work).

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
2. **NEVER skip the comply agent** — every implementation phase gets verified
3. **NEVER let the impl agent grade its own work** — that's the comply agent's job
4. **Report progress to user** after each phase (phase name + PASS/FAIL)
5. **Max 1 retry per phase** — don't loop endlessly on failures
6. **The comply agent's report is authoritative** — if it says FAIL, it's FAIL
7. **User can override** — if the user says to skip verification or continue despite failures, respect that
8. **Build+test responsibility belongs to the implement agent** — the per-phase comply agent does code review only. The final verification phase (Phase N-1) is the only comply phase that builds, tests, and runs pluginval.
9. **No pluginval or clang-tidy in implementation phases** — these run ONCE at the end (clang-tidy in Phase N-1.0, pluginval in Phase N-1). Do NOT generate or execute these in per-phase work.
10. **ALL test failures must be fixed immediately** — if the implement agent encounters ANY failing test (including ones outside the current spec's scope), it MUST fix the test before returning. "Pre-existing" is not an excuse (Constitution Section VIII). If an implement agent returns with known failing tests, the orchestrator MUST send it back to fix them before proceeding.
