---
description: Create or update the feature specification from a natural language feature description.
handoffs:
  - label: Build Technical Plan
    agent: speckit.plan
    prompt: Create a plan for the spec. I am building with...
  - label: Clarify Spec Requirements
    agent: speckit.clarify
    prompt: Clarify specification requirements
    send: true
---

## Environment

This project runs on **Windows**. All scripts are PowerShell (.ps1) files in `.specify/scripts/powershell/`. When executing scripts via the Bash tool, use:

```
powershell -ExecutionPolicy Bypass -File <script.ps1> [parameters]
```

Do NOT look for or run .sh files.

---

Spawn the `speckit-specify` agent to handle this task.

**User Input:**

```text
$ARGUMENTS
```

The agent has access to: Read, Write, Edit, Bash, Glob, Grep, WebSearch, WebFetch, and context7 MCP tools.

Execute the full specification workflow as defined in the agent.
