# Claude Code enforcement hooks

`PreToolUse` hooks wired in [`.claude/settings.json`](../../.claude/settings.json). Each reads the
tool-call JSON envelope on stdin and **exit code 2 blocks the call**, surfacing the stderr message to
the agent. All hooks **fail open** (exit 0) on any parse/IO error — a hook must never wedge the
session.

| Hook | Fires on | Blocks when | Redirects to |
|------|----------|-------------|--------------|
| [`guard-version-h.js`](guard-version-h.js) | `Edit` / `Write` / `MultiEdit` | target is `plugins/*/src/version.h` (generated, overwritten on configure) | edit `version.json`, then rebuild |
| [`guard-changelog.js`](guard-changelog.js) | `Bash` | a `git commit` message says `release X.Y.Z` for a plugin whose `CHANGELOG.md` has no `## [X.Y.Z]` section | add the CHANGELOG entry in the same commit |

These mechanize two "remember to" rules (version bumps go through `version.json`; a release carries its
CHANGELOG entry). Test a hook by piping a synthetic envelope:

```bash
node tools/hooks/guard-changelog.js <<'JSON'
{"tool_input":{"command":"git commit -m \"chore(membrum): release 9.9.9\""}}
JSON
# -> BLOCKED (no "## [9.9.9]" in plugins/membrum/CHANGELOG.md), exit 2
```

> **Not a clang-format hook.** The repo has no `.clang-format` and does not mandate clang-format; the
> "zero warnings" rule is about *compiler* warnings. If a lint gate is ever wanted, wire the existing
> `tools/run-clang-tidy.ps1` as a **non-blocking advisory**, not a blocking hook.

## Deferred: permission-allowlist curation (X15b)

`.claude/settings.local.json` has grown a large auto-accumulated allowlist. Consolidating it into a
curated `settings.json` allow-list is a separate, interactive cleanup (run the `fewer-permission-prompts`
flow) — not wired here.
