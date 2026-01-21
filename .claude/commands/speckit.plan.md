---
description: Execute the implementation planning workflow using the plan template to generate design artifacts.
handoffs:
  - label: Create Tasks
    agent: speckit.tasks
    prompt: Break the plan into tasks
    send: true
  - label: Create Checklist
    agent: speckit.checklist
    prompt: Create a checklist for the following domain...
---

## Environment

This project runs on **Windows**. All scripts are PowerShell (.ps1) files in `.specify/scripts/powershell/`. When executing scripts via the Bash tool, use:

```
powershell -ExecutionPolicy Bypass -File <script.ps1> [parameters]
```

Do NOT look for or run .sh files.

---

Spawn the `speckit-plan` agent to handle this task.

**User Input:**

```text
$ARGUMENTS
```

The agent has access to: Read, Write, Edit, Bash, Glob, Grep, WebSearch, WebFetch, and context7 MCP tools.

Execute the full planning workflow as defined in the agent.
