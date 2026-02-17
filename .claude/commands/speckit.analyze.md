---
description: Analyze spec/plan/tasks for consistency issues, then remediate with user approval.
---

## Environment

This project runs on **Windows**. All scripts are PowerShell (.ps1) files in `.specify/scripts/powershell/`. When executing scripts via the Bash tool, use:

```
powershell -ExecutionPolicy Bypass -File <script.ps1> [parameters]
```

Do NOT look for or run .sh files.

---

Spawn the `speckit-analyze` agent to handle this task.

**User Input:**

```text
$ARGUMENTS
```

The agent has access to: Read, Write, Edit, Bash, Glob.

Execute the full analysis and remediation workflow as defined in the agent. The agent will:
1. Analyze all artifacts (read-only)
2. Present findings report
3. Ask user for approval to fix issues
4. If approved: apply all edits
