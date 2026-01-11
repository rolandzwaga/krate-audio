---
description: Identify underspecified areas in the current feature spec by asking up to 5 highly targeted clarification questions and encoding answers back into the spec.
handoffs:
  - label: Build Technical Plan
    agent: speckit.plan
    prompt: Create a plan for the spec. I am building with...
---

Spawn the `speckit-clarify` agent to handle this task.

**User Input:**

```text
$ARGUMENTS
```

The agent has access to: Read, Write, Edit, Bash, Glob (no MCP tools - analysis only).

Execute the full clarification workflow as defined in the agent.
