# Claude Code enforcement hooks

Hooks wired in [`.claude/settings.json`](../../.claude/settings.json). Each reads the tool-call JSON
envelope on stdin. **`PreToolUse` exit code 2 blocks the call**, surfacing the stderr message to the
agent; the `PostToolUse` reminder is non-blocking and surfaces context via `additionalContext`. All
hooks **fail open** (exit 0) on any parse/IO error — a hook must never wedge the session.

| Hook | Event | Fires on | Effect |
|------|-------|----------|--------|
| [`guard-version-h.js`](guard-version-h.js) | PreToolUse | `Edit` / `Write` / `MultiEdit` targeting `plugins/*/src/version.h` (generated) | **Blocks** — redirects to `version.json`, then rebuild |
| [`guard-changelog.js`](guard-changelog.js) | PreToolUse | `Bash` / `PowerShell` `git commit` whose message (`-m`, or the file of `-F` / `--file`) says `release X.Y.Z` for a plugin whose `CHANGELOG.md` has no `## [X.Y.Z]` | **Blocks** — add the CHANGELOG entry in the same commit |
| [`guard-ci-gates.js`](guard-ci-gates.js) | PreToolUse | `Bash` / `PowerShell` `git commit` | **Blocks** on stale generated artifacts, a failing CI lint, or a failing `node --test` suite of `feed-tools.yml` / `manager-ci.yml` (run only when a staged path matches that workflow's `on.push.paths`, ~25 s) |
| [`guard-portability.js`](guard-portability.js) | PreToolUse | `Bash` / `PowerShell` `git commit` with staged `.cpp` / `.cc` files | **Blocks** when a staged translation unit does not compile under GCC (`tools/check-portability.js --staged`, in WSL); skips when WSL / g++ is absent |
| [`guard-heredoc.js`](guard-heredoc.js) | PreToolUse | `Bash` command containing a heredoc (`<<WORD ... WORD`) | **Blocks** — write the file with the Write tool, a commit message with `git commit -F <file>` |
| [`guard-wsl-shutdown.js`](guard-wsl-shutdown.js) | PreToolUse | `Bash` / `PowerShell` running `wsl --shutdown` / `--terminate` / `-t` | **Blocks** — the user and parallel agents use WSL at the same time |
| [`guard-tidy-scope.js`](guard-tidy-scope.js) | PreToolUse | `Bash` / `PowerShell` running `run-clang-tidy.ps1 -Full` / `run-clang-tidy.sh --full` | **Blocks** unless the line carries `# tidy-full-ok: <reason>`: the runners analyze only changed files by default (`tools/tidy-changed-files.js`); CI twin `tools/lint-tidy-scope.js` |
| [`guard-test-scope.js`](guard-test-scope.js) | PreToolUse | `Bash` / `PowerShell` running `run-gate.js` / `run-cpu-tests.js --scope all`, `node tools/run-close-lanes.js`, or an unselected `ctest` | **Blocks** unless the line carries `# gate-all-ok: <reason>`: the runners run only the cases the change reaches by default (`tools/affected-tests.js`); CI twin `tools/lint-test-scope.js` |
| [`remind-rt-safety.js`](remind-rt-safety.js) | PostToolUse | `Edit` / `Write` / `MultiEdit` under `**/src/processor/**`, `**/src/engine/**`, or `dsp/` (excl. `dsp/tests/`) | **Reminds** (once per session, and once per subagent: each parallel implementer gets it) to apply the RT-safety review lens + layer rule; never blocks |

**The commit hooks judge the checkout that commits.** `guard-changelog.js`, `guard-ci-gates.js` and
`guard-portability.js` take the repo root from the commit itself ([`commit-root.js`](commit-root.js)):
starting from the tool call's `cwd`, every `cd <dir>` / `pushd` / `Set-Location` before the commit (also
inside `( )` or `{ }`), then `git -C <dir>`, then `--work-tree` / `GIT_WORK_TREE=`; the checkout this
directory lives in only when that is no git checkout. A commit made in a git worktree (a parallel fix lane
of `.claude/workflows/innexus-fix.js`) is therefore checked against that worktree's index and tree, with its
own copies of the gate scripts.

**Every hook script here is wired, and every hook that guards a shell command matches both shell tools**
(`Bash|PowerShell` in `.claude/settings.json`): a commit or a WSL shutdown issued through the PowerShell tool
must not slip past. Tests for all of the above:
`node --test tools/hooks/tests/commit-root.test.js tools/hooks/tests/hooks.test.js`.

Hooks **fail open on a timeout** too. With several lanes building, a slow gate can let a lane commit through;
the gates run again when the merged result is committed in the main checkout.

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
