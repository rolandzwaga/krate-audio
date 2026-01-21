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

Spawn the `speckit-implement` agent to handle this task.

**User Input:**

```text
$ARGUMENTS
```

The agent has access to: Read, Write, Edit, Bash, Glob, Grep (no MCP tools - coding only).

Execute the full implementation workflow as defined in the agent.
