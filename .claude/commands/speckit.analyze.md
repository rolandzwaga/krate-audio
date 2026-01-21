---
description: Perform a non-destructive cross-artifact consistency and quality analysis across spec.md, plan.md, and tasks.md after task generation.
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

The agent has access to: Read, Bash, Glob (read-only - no Write/Edit tools).

Execute the full analysis workflow as defined in the agent. This is a READ-ONLY operation.
