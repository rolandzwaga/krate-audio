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
When done, summarize: files created/modified, tests written/run, builds performed.
```

Wait for the agent to return.

**Spawn `speckit-comply` agent** to verify the phase:

```
Verify Phase {N} of spec {feature-name} implementation.

Feature dir: {FEATURE_DIR}/
Mode: Phase Verification

Read these files:
- tasks.md (Phase {N} — check all tasks are marked [X])
- spec.md (FR-xxx and SC-xxx requirements covered by this phase)
- plan.md (architecture decisions that should be reflected)
- quickstart.md (build and test commands)

Then verify:
1. Check every task marked [X] in Phase {N} — verify the work was actually done
2. Build the project using commands from quickstart.md
3. Run tests using commands from quickstart.md
4. For each FR-xxx/SC-xxx covered by this phase: read the code, cite evidence
5. Check for constitution violations (Section VIII warnings, XVI cheating, XIII test-first)

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

The compliance agent found these issues:
{paste the comply agent's issues list here}

Fix ONLY the issues listed above. Do NOT re-implement tasks that already passed.
When done, summarize what you changed.
```

Wait for the agent to return.

**Spawn `speckit-comply` agent** again to re-verify:

```
Re-verify Phase {N} of spec {feature-name} after fixes.

{same prompt as before}
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

**Completion Verification (Phase N-1)**: Spawn `speckit-comply` agent:

```
Perform final completion verification for spec {feature-name}.

Feature dir: {FEATURE_DIR}/
Mode: Final Completion Verification

Steps:
1. Read spec.md — list ALL FR-xxx and SC-xxx requirements
2. For EACH requirement individually:
   a. Find the implementation code — cite file:line
   b. Find the test — cite test name and actual result
   c. For SC-xxx with numeric targets — run/read actual values, compare against spec
3. Run full test suite
4. Run pluginval if plugin code was changed:
   tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Iterum.vst3"
5. Check for cheating patterns (relaxed thresholds, stubs, removed scope)
6. Produce the full compliance table with REAL evidence

Output the final compliance report with:
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
