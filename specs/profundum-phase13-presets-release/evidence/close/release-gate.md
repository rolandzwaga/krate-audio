# Profundum Phase 13 — Release gate (T036; FR-055, SC-025)

Run by the main loop at close, **after T032 (`suites.sh`) has finished** and before the T035 CPU lane (close procedure step 9).
This file records the command and the matrix it must produce. The reading itself is quoted into
`evidence/run-log/T036.txt`. Cutting the release (version bump, tag, push) is the user's decision and is **not** part of this phase.

## Command

Invoke the `release-readiness` workflow with:

```
args: {plugins: ["profundum"]}
```

No `versions` entry is passed: `version.json` is not edited in this phase (FR-054), so the workflow's `versionMatches` is
`true` by construction (`.claude/workflows/release-readiness.js`, "No specific version requested — set versionMatches true").

`PLUGIN_MAP` maps `profundum` to `{ testTarget: 'profundum_tests', bundle: 'Profundum.vst3' }`
(`.claude/workflows/release-readiness.js:23`). The one agent it spawns (`release:profundum`) does, with no file edits:

1. Reads `plugins/profundum/version.json` → `0.1.0`.
2. Builds `"/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target profundum_tests`;
   warning count must be 0.
3. Runs `profundum_tests.exe`; reports the Catch2 summary line.
4. Builds the bundle and runs
   `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Profundum.vst3"`.
5. Runs `node tools/check-changelog-coverage.js profundum` and reconciles the commits it lists.

Do not run it while T032's lanes are still executing: steps 2–4 rebuild and re-run what T032 runs, and the shared build tree and
pluginval would contend.

## Expected matrix

The workflow's Matrix phase computes six gates (`release-readiness.js`, `gates = {...}`); FR-055's four criteria map onto them:

| FR-055 / SC-025 criterion | Workflow gate | Expected | Why |
|---|---|---|---|
| build | `build` (`built && warnings === 0`) | ok | Zero-warning build of `profundum_tests` (FR-056). |
| tests | `tests` (`testsPassed`) | ok | `All tests passed` for `profundum_tests` (T032 lane (a) is the scoped per-push reading; this is the whole exe). |
| pluginval strictness 5 | `pluginval` (`pluginvalPassed`) | ok | Exit 0 on `Profundum.vst3` (also T032 lane (e)). |
| version.json / CHANGELOG sync | `changelog` (`changelogHasVersion`) | ok | `version.json` reads `0.1.0`; `plugins/profundum/CHANGELOG.md` has `## [0.1.0] - Unreleased`. |
| version.json / CHANGELOG sync | `changelogComplete` (`changelogUnreconciled === 0`) | ok | `[0.1.0]` is the first CHANGELOG entry, so `check-changelog-coverage.js` prints "First changelog entry — nothing earlier to diff against." and exits 0; there is no commit range to reconcile. The FR-054 bullets (factory library, preset browser, manual) must still be present in the open entry; the agent and the comply stage check them by reading the entry. |
| version.json / CHANGELOG sync | `version` (`versionMatches`) | ok | No requested version; `version.json` unchanged at `0.1.0` (memory rule: the unreleased version takes new changes, never re-bump). |

Expected log line:

```
READY  profundum 0.1.0  build:ok tests:ok pluginval:ok changelog:ok changelog-complete:ok version:ok
```

and the return value `ready: true, readyCount: 1, total: 1, blocked: []`.

## On a red gate

- `build` / `tests` red: a Phase 13 defect — own it, fix it, re-run only the affected case, then re-run this flow.
- `tests` red on a timing-budget case of untouched code while anything else was running: recorded as load, not re-run
  (CLAUDE.md "Run only the tests the change set can reach").
- `pluginval` red: a defect; quote the failing line into `evidence/run-log/T036.txt` and the tasks.md "Defects found" list.
- `changelog` / `changelogComplete` red: add the missing bullets to the open `[0.1.0] - Unreleased` entry; never edit
  `version.json`.
- `version` red: cannot happen without a `versions` arg; if it does, `version.json` was edited — revert that edit.

The macOS / Linux CI legs (and the AU / auval leg) are **PENDING-CI** until the user pushes; this gate is the Windows reading only.
