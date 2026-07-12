# Claude Code enforcement hooks

Hooks wired in [`.claude/settings.json`](../../.claude/settings.json). Each reads the tool-call JSON
envelope on stdin. **`PreToolUse` exit code 2 blocks the call**, surfacing the stderr message to the
agent; the `PostToolUse` reminder is non-blocking and surfaces context via `additionalContext`. All
hooks **fail open** (exit 0) on any parse/IO error — a hook must never wedge the session.

| Hook | Event | Fires on | Effect |
|------|-------|----------|--------|
| [`guard-version-h.js`](guard-version-h.js) | PreToolUse | `Edit` / `Write` / `MultiEdit` targeting `plugins/*/src/version.h` (generated) | **Blocks** — redirects to `version.json`, then rebuild |
| [`guard-changelog.js`](guard-changelog.js) | PreToolUse | `Bash` `git commit` saying `release X.Y.Z` for a plugin whose `CHANGELOG.md` has no `## [X.Y.Z]` | **Blocks** — add the CHANGELOG entry in the same commit |
| [`remind-rt-safety.js`](remind-rt-safety.js) | PostToolUse | `Edit` / `Write` / `MultiEdit` under `**/src/processor/**`, `**/src/engine/**`, or `dsp/` (excl. `dsp/tests/`) | **Reminds** (once/session) to apply the RT-safety review lens + layer rule; never blocks |

The RT-safety reminder deliberately does **not** grep for `new`/`malloc`/`lock` — those appear
legitimately in `prepare()`/ctors/member decls, so a content grep produces false-positive noise the
agent learns to ignore. It only nudges toward the review when an audio/DSP path is touched; the hard
gate for the layer rule is the CI lint (`tools/lint-layers.js`).

These mechanize three "remember to" rules (version bumps go through `version.json`; a release carries
its CHANGELOG entry; audio-path edits get the RT-safety lens). Test a hook by piping a synthetic
envelope:

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
