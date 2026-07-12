---
name: release
description: Guardrailed version bump + release commit for a single Krate Audio plugin. Edits ONLY version.json, adds the matching CHANGELOG entry, builds, runs that plugin's tests + pluginval, then commits — never pushes. Use when the user asks to "release", "bump version", "cut a release", or "ship X.Y.Z" for a plugin.
allowed-tools: Read, Edit, Bash
---

# /release — Plugin Release Bump

Perform a clean, guardrailed release for **one** plugin. This exists because the version-bump
sequence has been botched repeatedly (editing generated files, forgetting the changelog, pushing
without permission). Follow every step; do not skip or reorder.

## Inputs

- **Plugin** (required): one of `iterum`, `disrumpo`, `ruinae`, `innexus`, `gradus`, `membrum`.
  If not given in the invocation, ask which plugin — do not guess.
- **New version** (required): `X.Y.Z`. If not given, read the current `plugins/<plugin>/version.json`
  version and ask the user for the target (or confirm the intended semver bump). Do not invent it.

## Plugin → target/bundle map

| Plugin | Test target | pluginval bundle |
|--------|-------------|------------------|
| iterum | `plugin_tests` + `approval_tests` | `Iterum.vst3` |
| disrumpo | `disrumpo_tests` | `Disrumpo.vst3` |
| ruinae | `ruinae_tests` | `Ruinae.vst3` |
| innexus | `innexus_tests` | `Innexus.vst3` |
| gradus | `gradus_tests` | `Gradus.vst3` |
| membrum | `membrum_tests` | `Membrum.vst3` |

## Steps (in order)

1. **Bump version.json ONLY.** Edit `plugins/<plugin>/version.json` — change the `"version"` field to
   the new `X.Y.Z`. **NEVER edit `version.h`** or any other generated file — it is produced from
   `version.json` at build time. Touch nothing else in this file.

2. **Add the CHANGELOG entry in the SAME change.** Prepend a new section to
   `plugins/<plugin>/CHANGELOG.md` directly above the most recent `## [` section:
   ```
   ## [X.Y.Z] - <today's date, YYYY-MM-DD>

   ### Added / Changed / Fixed
   - <concise bullets describing what shipped>
   ```
   Use only the Keep-a-Changelog subsections that apply. Get the changes from the user or from the
   commits since the last release (`git log --oneline` scoped to `plugins/<plugin>/`). Use the real
   current date — do NOT hardcode a stale one.

3. **Build.** (Full CMake path required on Windows.)
   ```bash
   CMAKE="/c/Program Files/CMake/bin/cmake.exe"
   "$CMAKE" --build build/windows-x64-release --config Release --target <test-target>
   ```
   Fix any compilation errors/warnings before proceeding. No tests without a clean build.

4. **Run that plugin's tests.** (For iterum, run BOTH `plugin_tests` and `approval_tests`.)
   ```bash
   build/windows-x64-release/bin/Release/<test-target>.exe 2>&1 | tail -5
   ```
   The last line must read "All tests passed". If anything fails, STOP and report — do not commit.

5. **Run pluginval.**
   ```bash
   tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/<Bundle>.vst3"
   ```
   Must pass. If the built bundle isn't present (post-build copy can fail on permissions), that copy
   failure is fine — validate the bundle in the build tree above.

6. **Stage exactly the two files** and commit — do NOT `git add -A`:
   ```bash
   git add plugins/<plugin>/version.json plugins/<plugin>/CHANGELOG.md
   git commit -m "chore(<plugin>): release X.Y.Z"
   ```
   (End the commit body with the standard `Co-Authored-By` trailer.)

7. **STOP. Never push.** Report the new version, the test/pluginval results, and the commit hash.
   Pushing requires explicit user permission every time.

## Guardrails (why this skill exists)

- version.json is the single source of truth; `version.h` is generated — editing it is always wrong.
- The changelog entry ships in the same commit as the bump, never split.
- Never push without explicit permission.
- Never bump on `main` if the workflow calls for a branch — verify the branch first.
