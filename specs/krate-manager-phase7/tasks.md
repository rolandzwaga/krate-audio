# Tasks: Krate Manager Phase 7 — Download and Verify

**Spec:** `specs/krate-manager-phase7/spec.md` (DRAFT revision 3: D1-D13, FR-001 to FR-091, SC-001 to SC-037;
Clarifications Q1-Q6 and the plan-precondition rulings Q-A / Q-B of 2026-10-31, `spec.md:1226-1250`) · **Plan:**
`specs/krate-manager-phase7/plan.md` (revision 4: PD-1 to PD-20 `plan.md:40-61`, SI-1 to SI-30 `plan.md:63-96`, the
precondition table Q-A / Q-B decided and R-1 open `plan.md:1464-1470`, review notes `plan.md:1474-1592`)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 7 (lines 599-630); Repository Layout (lines 213-244); Cross-Cutting
Constraints (lines 980-1021)
**Status:** DRAFT task list, revision 2, 2026-10-31: **regenerated from plan revision 4** (it replaces the revision 1 list,
which was written against plan revision 2 and its two `#[expect(clippy::too_many_arguments)]`, now forbidden by the
user's Q-B ruling, `spec.md:1246-1250`). Written against `feat/krate-manager` at `2f3c93d7` (`git rev-parse --short HEAD`
this session; `git status --porcelain` shows only `?? specs/krate-manager-phase7/`). The last commit touching
`apps/krate-manager/` is `03d97890`, and every app file:line quoted below was re-read this session at `2f3c93d7`.
**Spec state:** none of plan §0.2's edits is in `spec.md` yet. Counted this session with `grep -c` over `spec.md`:
`kat_signing_key` 0, `DiscardBusy` 0, `staged_symlinks_are_not_followed` 0, `5b980e5b` 0, `assert_progress_invariants` 0,
`TAURI_SIGNING_PRIVATE_KEY` 0, `read_dir_entries` 0, `kind_of` 0, `too_many_arguments_is_never_suppressed` 0,
`prune_spares_in_flight_artifact` 0, `staged_links_are_not_followed_windows` 0, `non_retryable_transport_errors` 0
(`CachedInvalid` 1, `DownloadEnv` 1 and `HeadAction` 4 occur only in the Clarifications and the old FR text). The plan
requires them before the build (`plan.md:17-26`; SI-1, SI-2, SI-3, SI-4, SI-18, SI-25 and SI-27 are blocking). T001
applies them in G2, in parallel with the module tasks: every task section below states its assertions from the plan
§7.1 rows, which are binding, so no G2 task waits for the spec text.
**Lanes in this phase:** the **app lane** (cargo fmt / clippy / clippy dev-feed / clippy-guard / cargo test, npm
typecheck / lint / test, the version lint, `tauri build`, the `cargo tree` record), the **pipeline lane** (`node --test`
over `tools/feed-contract/tests/`, because `manager-ci-shape.test.js`'s `EXPECTED_REQUIRED` changes; the repo lints)
and **L-WSL** (core clippy + tests in WSL Ubuntu: the only local compile of the `cfg(unix)` tests, plan §8
`plan.md:1419-1423`; a main-loop lane). No file under `plugins/`, `dsp/`, `cmake/`, `extern/` or `.github/workflows/`
changes (spec FR-090, `spec.md:714`), so there is **no C++ lane**: no `run-gate.js`, no `header-consumers.js`, no
`check-portability.js`, no clang-tidy, no pluginval, no `lint-plugin-editor-baseline.js` (no plugin editor changes), and
**no workflow edit**: `manager-ci.yml` already runs every step this phase needs on all three OSes, and the per-OS CI
additions are the `required-tests.json` lists (T010; plan §8 `plan.md:1414-1416`). `npm run tauri build`, L-WSL and the
full close procedure are main-loop lanes (T009 writes their scripts, T011 hands them over).

---

## How to read this file

- Groups run in order. A group marked **[P]** runs its tasks at the same time in one working tree. A task is
  **[P]-safe** when no other task in its group touches any of its files and none of its files is a **shared file**:
  any `Cargo.toml`, `Cargo.lock` (the workspace's and `tests/fixtures/clippy-guard/Cargo.lock`), `clippy.toml`,
  `crates/core/src/lib.rs`, `crates/core/src/context.rs`, `src-tauri/src/lib.rs`, `src-tauri/src/commands.rs`,
  `src-tauri/build.rs`, a capability file, `tauri*.conf.json`, `package.json`, a workflow, `tests/required-tests.json`,
  `manager-ci-shape.test.js`, the roadmap or `specs/INDEX.md`. **Every app-side shared file is edited in one task only,
  T002, which is G1 on its own (sequential)**: it writes every `mod` line, every manifest entry and both lockfiles,
  every public type and signature of the three new core modules (with `todo!()` bodies where logic belongs to a G2
  task), `DownloadEnv`, the shared test helpers every G2 task calls (`test_signer.rs`, the `test_support.rs` additions
  with `DownloadRig`), the `AppContext` field with every one of its Phase 4-6 construction sites, the three new
  `Reply` variants, the final `src-tauri` wiring, and every existing guard expectation those edits move. The G2 tasks
  then touch only their own files. A G2 file is either a placeholder T002 created for its task or an existing file
  exactly one G2 task edits (`spec.md` → T001; `http.rs` + `http_tests.rs` → T003; `verify.rs` + `verify_tests.rs` →
  T004; `download.rs` + `download_tests.rs` → T005; `fixture_server.rs` + `fixture_server_tests.rs` → T006;
  `injection.rs` → T007; `config_guard.rs` + `roster.rs` → T008; the three evidence scripts → T009). The
  `required-tests.json` file and the shape test are touched only by T010 (G3).
- Each task section is the executor's authoritative instruction: files, the failing test to write first (file, test
  name, assertions), the implementation intent, the lane. Where a block is too long to copy without drift (a type
  list, a test table, an algorithm), the section names the `plan.md` section or §7.1 row, or the spec FR / SC line
  range, that holds it; that text is then **binding, verbatim**, as amended by plan §0.2 (`plan.md:63-96`) and this
  file's TA rows. Where this file and the plan differ, this file wins and the TA row says why.
- Canonical order in every task: **failing test → implement → zero warnings → tests pass.** "Red first" means the test
  was run and failed for the stated reason before the implementation existed; the notes quote the failing line. The red
  run normally uses T002's placeholder, so the test compiles and fails on the `todo!()` panic ("not yet implemented") or
  on its assertions. A test that cannot run on this box (`cfg(unix)`, `cfg(target_os = "macos")`) has no local red: its
  red-first is recorded as "not compiled here" and it is read twice against its SC before the task ends (Rule 15).
- **In-group expected reds.** In G2 the download tests (T005) run T003's transport, T004's verifier and T006's fixture
  replies, and T007's guards scan T005's source. A test that can go green only when another G2 task lands is listed
  under "Expected red until" in its task, with the task that turns it green and the only failure text that counts as
  expected. **Every such red is gone by the G2 gate** (all of G2 has landed by then); any red at a gate is a real red.
  No red is carried across a group boundary. A **compile error is never an expected red**: every core unit test lives in
  one test binary, so a file that does not compile stops every G2 task's loop (Rule 10).
- **Main-loop lanes.** Steps longer than ~5 minutes run only in the main loop, from scripts T009 writes:
  `specs/krate-manager-phase7/evidence/wsl-lane.js` (L-WSL; Phase 6 measured 30.3 s for 192 tests,
  `specs/krate-manager-phase6/evidence/timings.md:34` row A10, but this phase's first WSL run compiles `sha2` 0.11,
  `minisign-verify` and the `minisign` dev graph cold) and `specs/krate-manager-phase7/evidence/close-lane.js`
  (L-CLOSE: the release `npm run tauri build`, 98.4 s in Phase 6's row B2, `timings.md:37`, plus L-WSL and the full plan
  §7.4 procedure). T011 returns with notes "deferred to the main loop at close: run `node
  specs/krate-manager-phase7/evidence/close-lane.js`".
- Never commit, never push, never trigger or dispatch CI. Those are the user's steps.

### Rules every task obeys (a task that breaks one is defective)

1. **Never touch the real system.** Every test builds its roots and `AppContext` from `test_support::fresh_temp_roots()`
   (`crates/core/src/test_support.rs:132-136`, returns `(TempDir, Roots)` sandboxed to the `TempDir`) and writes only
   inside that `TempDir`; paths outside the staging dir that a test needs (`outside/` in the link cases) are built with
   `Path::join` from the same base. The state dir, staging dir and feed URL come from `AppContext`; the signing key is a
   `TestSigner` key or the KAT key (`test_support::kat_signing_key()`, T002), never `SigningKey::release()` outside
   `verify::tests::release_key_is_the_committed_key_and_not_a_test_key` (FR-087). Every request goes to the test's own
   `FixtureServer` on `127.0.0.1` with `HttpConfig::loopback(…)`, through D13's seam (`request.with_fixture_url(&server.url("/<asset>"))`);
   test feeds carry `https://assets.example.invalid/…` URLs, which the loopback policy refuses before any connection.
   No test calls `github.com`, `githubusercontent.com`, `krateaudio.com` or `api.github.com`;
   `crates/core/tests/live_feed.rs` stays behind the `live-network` feature, which no task, script or gate enables
   (`crates/core/Cargo.toml:12-13`). No test acquires the real state lock, reads a real VST3 / Components / preset folder,
   `ProgramData`, the registry, or the real state dir. The only processes a `crates/core` test starts are `sleep` /
   `printf` (`injection.rs:338`), the two existing TA-1 programs in their two bodies (`injection.rs:341-344`) and, after
   T002, `/usr/bin/xattr` in exactly `download::tests::verified_file_has_no_quarantine_macos` and `cmd` (for `mklink /J`)
   in exactly `download::tests::staged_links_are_not_followed_windows` (SI-4 a, SI-29). No test calls
   `std::env::temp_dir`, `var`, `var_os`, `home_dir` (`apps/krate-manager/clippy.toml:1-10`). No private key, public or
   secret, is written to any file by any test (FR-083): `TestSigner` keys live in memory only. No task launches the
   built app, the elevated helper or an installer.
2. **No lint suppression added, of any lint (Q-B).** `[workspace.lints.clippy] allow_attributes = "deny"`,
   `allow_attributes_without_reason = "deny"` (`apps/krate-manager/Cargo.toml:44-46`): never `#[allow]`. This phase also
   adds **no `#[expect(…)]`** anywhere, and in particular nothing names `clippy::too_many_arguments` or
   `clippy::complexity` (plan PD-11, SI-26; `too_many_arguments_is_never_suppressed`, T007). Every public download entry
   point stays within clippy's default 7 parameters: `obtain_and` 5, `run_command` 4, `for_command` 6,
   `command_request` 5, `discard` 2, `channel_progress` 1, `http::get_streamed` 7; a private helper that would exceed 7
   takes `env` plus a private per-call state struct (plan §4.4.8 `plan.md:760-766`). No new `clippy::disallowed_methods`
   site: `D6_SITES` / `D6_TOTAL` stay at 6 files / 8 sites (`injection.rs:87-95`). No `// @ts-ignore`, no
   `eslint-disable`. A warning is fixed in the code, never silenced.
3. **Unit tests live in `<stem>_tests.rs`**, declared at the end of `<stem>.rs` as `#[cfg(test)] #[path =
   "<stem>_tests.rs"] mod tests;` (the existing pattern; an inline `mod tests {` is reported by `injection.rs`
   `RULE_INLINE_TESTS`, `:423`). `test_signer.rs` is itself `#[cfg(test)]` and holds no tests.
4. **Literal hygiene.** (a) No non-test `crates/core/src` file outside `platform/` holds, comments included, any
   `FORBIDDEN_OUTSIDE_PLATFORM` substring (`injection.rs:18-26`: `"C:\\`, `"/Library`, `"/usr/`, `"/home`, `"/Users`,
   `krateaudio.com`, `api.github.com`): this covers `download.rs`, `verify.rs` and `test_signer.rs`. (b) `download.rs` and
   `verify.rs` hold no `http://` or `https://` literal, no `read_to_end`, `read_to_string`, `fs :: read (`, and `read_dir`
   only once, in `FsStagingIo::read_dir_entries` (FR-082). (c) The token `sandboxed` appears only in `roots.rs`
   (`injection.rs:1134-1136`); call `ctx.roots.check_sandboxed(…)`, never name a local `sandboxed`. (d) **No file under
   `apps/krate-manager/` names any of the nine shipped plugins**, tests included (ROSTER, `roster.rs`): invented names
   `Alpha`, `Bravo`, … only; the KAT's `KatPlugin` is read from `tools/feed-contract/fixtures/` in place, never copied.
   (e) No non-test source holds `com.krateaudio.<x>`, an Inno GUID shape or `_is1` (Phase 6 FR-089); the
   `test_support::test_feed` builder may (plan §4.7, `plan.md:913-921`). (f) `ureq`, `rustls`, `TcpStream`,
   `TcpListener` stay in their files (`CONFINED_TOKENS`, `injection.rs:223-248`): `download.rs` / `download_tests.rs`
   reach the network only through `http::get_streamed` and the fixture server.
5. **Zero warnings.** `cargo fmt --all --check`; every `cargo clippy … -- -D warnings` with 0 `warning:` lines in both
   the lib and the test builds; `npm run typecheck`; `npm run lint`; `node --check`.
6. **Parallel tests.** Never `--test-threads`, `--no-file-parallelism`, `--fileParallelism=false`, `--maxWorkers`, `-j
   1`, `--test-concurrency=1`. `cargo test` uses its default threads; `vitest run` its parallel files; `node --test` runs
   files in parallel. No new test holds a global mutex, a fixed port, or a sleep longer than 1 s (FR-087); the
   wall-clock bounds in SC rows are hang guards (Rule 16).
7. **`cargo test` is `cargo test --workspace --locked` or `cargo test -p krate-manager-core --locked …`, never with
   `--all-targets`** (that builds a harness for `src-tauri`'s lib and bin despite `test = false`,
   `src-tauri/Cargo.toml:10-19`; Phase 4 ruling P-4). `cargo clippy --workspace --all-targets` is correct.
8. **Lockfiles.** Only T002 changes `Cargo.lock` and `tests/fixtures/clippy-guard/Cargo.lock`, once each, additively
   (plan §4.1, TA-5). Every other cargo command in every task uses `--locked`. **No task after T002 adds a dependency or
   a feature**: a task that finds it needs one stops, returns `blocked` and names the crate and why.
9. **Placeholder discipline (TA-1).** T002's placeholders compile with zero warnings in the lib build and the test
   build: every parameter of a `todo!()` body is `_`-prefixed; a placeholder declares only `pub` items, or the
   `pub(crate)` items TA-1 lists with the anchor that keeps each used; a placeholder struct whose real fields belong to a
   G2 task holds only `_private: ()`; a placeholder imports only what it uses. The owning task renames parameters,
   replaces `_private`, and removes the anchors as it implements. At close no `todo!(` or `unimplemented!(` remains in
   any workspace `.rs` file outside `*_tests.rs` and `tests/` (T011 step 4, L-CLOSE row A9).
10. **One working tree, many writers (G2).** Write each file whole, so that it compiles at every save (types and
    signatures first, bodies after; a test that names an item you have not written yet is saved only after the item).
    Never edit a file another task owns; when `cargo` fails on a file you do not own, wait 60 s and retry, three times,
    then record it in your notes and continue with what you can verify. Run your own-module loop (L-OWN) in the
    **target slot** your task names, with its `CARGO_BUILD_JOBS` (TA-9): the shared `apps/krate-manager/target` for T005
    (12 jobs, warm from T002, the long pole), slot `F:/tmp/km7-target/lib` for T003, T004, T006 (10 jobs), slot
    `F:/tmp/km7-target/guard` for T007, T008 (8 jobs). Tasks sharing a target dir queue on its build lock; that is
    accepted. The first run in a slot pays one cold build of core's dependencies; record its wall clock. Before the
    first build run `df -h /f` (9.4 GB free this session): with less than 5 GB free, use the shared target instead of a
    slot and say so in the notes. Delete a slot directory when the last task using it ends (check `ls F:/tmp/km7-target/`
    and the group's task states).
11. **Scope fence (spec FR-090, FR-091, `spec.md:714-723`).** Outside `apps/krate-manager/**` only these paths change:
    `specs/krate-manager-phase7/**`, `specs/INDEX.md` (regenerated only), `tools/feed-contract/tests/manager-ci-shape.test.js`
    (`EXPECTED_REQUIRED` only, T010), and in `specs/Krate-manager-roadmap.md` only the Phase 7 status line (main loop at
    close). Inside `apps/krate-manager/`: no change to `src/` (TypeScript, FR-063: neither a tracked diff nor an untracked
    file), `index.html`, `tauri*.conf.json`, `package.json`, `package-lock.json`, `crates/helper/**`,
    `eslint.config.mjs`, `vite.config.ts`, `tsconfig*.json`, `clippy.toml`, `CHANGELOG.md`, `version.json`,
    `src-tauri/Cargo.toml`, or any workflow (plan §2, `plan.md:230-231`).
12. **Line endings.** This checkout runs `core.autocrlf=true` (`git config core.autocrlf` = `true` this session). Every
    pre-existing file this phase edits is CRLF in the working tree (measured this session, `grep -c $'\r$'` = `wc -l`):
    `Cargo.toml` 53/53, `Cargo.lock` 5265/5265, `tests/fixtures/clippy-guard/Cargo.lock` 5265/5265,
    `crates/core/Cargo.toml` 43/43, `crates/core/src/lib.rs` 24/24, `http.rs` 1141/1141, `http_tests.rs` 2257/2257,
    `context.rs` 213/213, `context_tests.rs` 570/570, `fixture_server.rs` 470/470, `fixture_server_tests.rs` 491/491,
    `test_support.rs` 276/276, `test_support_tests.rs` 269/269, `src-tauri/src/commands.rs` 52/52,
    `tests/required-tests.json` 261/261, `manager-ci-shape.test.js` 941/941, `spec.md` 1250/1250, the roadmap
    1057/1057 (the revision 1 list called `test_support_tests.rs` LF; this session's count says CRLF, and it is treated
    as CRLF). The other edited files (`detect_tests.rs`, `feed_tests.rs`, `status_tests.rs`, `tests/injection.rs`,
    `tests/config_guard.rs`, `tests/roster.rs`, `tests/live_feed.rs`, `src-tauri/src/lib.rs`, `src-tauri/build.rs`,
    `capabilities/main.json`, `specs/INDEX.md`) are checked the same way by the task that edits them before its first
    edit. An edit keeps the file's ending on every line it adds. New files are written LF. `specs/INDEX.md` is written
    only by `tools/gen-specs-index.js`.
13. **Shell discipline.** Every command runs in the Bash tool (Git Bash), foreground, output to a log under
    `F:/tmp/gate/km7-<task id>/`, then `tail`; never `cat` a whole log, never a drive-wide `find` / `du`. No heredoc in a
    Bash command (a repo hook blocks them): write scripts with the Write tool and run them with `node`. All app-lane
    commands run from `apps/krate-manager/`; repo-lane commands from the repo root.
14. **No invented APIs.** Every crate API used is one plan §1.2 verified (`plan.md:176-190`): `minisign-verify` 0.3.0
    (`PublicKey::decode`, `Signature::decode`, `Signature::trusted_comment`, `PublicKey::verify_stream`,
    `StreamVerifier::update` / `finalize`, `Error::{UnexpectedKeyId, UnsupportedLegacyMode, …}`;
    https://docs.rs/minisign-verify/0.3.0), `minisign` 0.10.0 (dev only: `KeyPair::generate_unencrypted_keypair`, `pk` /
    `sk`, `PublicKey::keynum`, `to_base64`, `minisign::sign(Some(pk), sk, reader, Some(tc), None)`,
    `SignatureBox::to_string`; https://docs.rs/minisign/0.10.0/minisign/fn.sign.html), `sha2` 0.11.1 (`Sha256::new`,
    `update`, `finalize` into `[u8; 32]`; https://docs.rs/sha2/0.11.1), `ureq` 3.5.0 (`ConfigBuilder::timeout_per_read`,
    `accept`, `accept_encoding`, `Config::timeout_per_read`, `timeouts`, `https_only`, `Error::Timeout(Timeout::PerRead)`,
    `Body::as_reader`; https://docs.rs/ureq/3.5.0), `tauri` 2.12.5 (`ipc::Channel<T>`, `Channel::send`,
    `async_runtime::spawn_blocking`; https://docs.rs/tauri/2.12.5/tauri/ipc/struct.Channel.html), `base64`
    (`engine::general_purpose::STANDARD`), std (`DirEntry::file_type`, `fs::symlink_metadata`, `fs::remove_dir_all`,
    `OpenOptionsExt::share_mode`; https://doc.rust-lang.org/std/fs/). An API that turns out to differ is a defect of the
    plan: record it under "Defects found" with the crate source line, and use the real API.
15. **`cfg` discipline.** The `cfg(unix)` tests (`readonly_staging_dir_unix`, `staged_symlinks_are_not_followed`,
    `streamed_pac_fetch_keeps_the_feed_bound_unix`) compile only in L-WSL and CI, and `verified_file_has_no_quarantine_macos`
    (`cfg(target_os = "macos")`) only in CI (plan §7.3, §8). Write them with the same care as the Windows code, read each
    against its SC twice, and gate every `use` the way its only users are gated (an unused import is a warning on the leg
    that compiles it). The `cfg(windows)` tests (`prune_skips_a_file_open_without_share_delete_windows`,
    `verified_file_has_no_zone_identifier_windows`, `staged_links_are_not_followed_windows`) run here and on CI.
    `http.rs`'s `AgentPurpose::PacFetch` exists only under `cfg(not(windows))` (`http.rs:622-627`); the `Download` arm
    must compile on both sides. The prune's Windows-only `remove_dir_all` retry is `#[cfg(windows)]` (plan §4.4.5
    `plan.md:697-700`); its local reader on Unix must not leave an unused binding.
16. **Bounded waits are hang guards, not speed bars.** The `< 1 s` / `< 2 s` / `< 3 s` / `< 5 s` bounds of SC-006,
    SC-009, SC-010, SC-034 and SC-037 guard against a hang; the `≥` bounds (SC-009's 150 ms and 1.5 s, SC-037's
    `elapsed > policy.stall` and ≥ 1.35 s) prove a mechanism. Never widen a bound; a timing red while other tasks
    compile is rerun once alone before it is called a defect, and the notes record both runs. A red that names
    `E_DOWNLOAD_STALLED` in a non-stall row is read as plan §9's scheduler risk first (`plan.md:1448`). Rust has no test
    tags, so these tests are written with the plan's margins (expected durations under 60 % of each bar).

### Lanes (referenced by every task)

| Lane | Commands | Pass |
|---|---|---|
| **L-RS** | `cargo fmt --all --check`; `cargo clippy --workspace --all-targets --locked -- -D warnings`; `cargo test --workspace --locked 2>&1 \| tee F:/tmp/gate/km7-<task>/cargo-test.txt` (Rule 7; this also runs the `verify.rs` doctests) | fmt exit 0; clippy exit 0 with 0 `warning:` lines; every test binary and the doctests `0 failed` |
| **L-OWN** | `CARGO_TARGET_DIR=<slot> CARGO_BUILD_JOBS=<n> cargo test -p krate-manager-core --locked --lib <filter>` (or `--test <name>` for T007 / T008, `--doc` for T004's doctests) and `CARGO_TARGET_DIR=… CARGO_BUILD_JOBS=… cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings` (Rule 10; a working loop, not a gate). `<slot>` per Rule 10; for T005 unset (shared target) | the task's own tests and 0 `warning:` lines |
| **L-APP** | `CARGO_BUILD_JOBS=12 cargo clippy -p krate-manager --all-targets --locked -- -D warnings` (shared target) | exit 0, 0 `warning:` lines |
| **L-DEVFEED** | `cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings` | exit 0, 0 `warning:` lines |
| **L-GUARD** | the `run:` text of `manager-ci.yml` step `clippy-guard` (`.github/workflows/manager-ci.yml:97-115`), executed with `C:/Program Files/Git/bin/bash.exe -c` from `apps/krate-manager/` with `RUNNER_TEMP=F:/tmp/gate/km7-<task>` and `RUNNER_OS=Windows` | a `::notice title=clippy-guard (Windows)::rc=<non-zero> disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=<≥1> other-errors=0` line (Phase 6's reading: `lint-named=5`, `specs/krate-manager-phase6/evidence/timings.md:14` row A1d), no `::error` |
| **L-TS** | `npm run typecheck`; `npm run lint`; `npm test` | each exit 0 (the frontend is unchanged, FR-063: 29 vitest titles pass) |
| **L-NODE** | `node --check <each new or changed .js>`; `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" > F:/tmp/gate/km7-<task>/all.tap` from the repo root (quoted glob); read with `tail -12` | TAP `# fail 0`; no `Warning:` / `DeprecationWarning` / `ExperimentalWarning` line |
| **L-TAP** | `node specs/krate-manager-phase2/evidence/tap-check.js F:/tmp/gate/km7-<task>/all.tap` | exit 0 and `TAP plan=1..59 top-level=59` (only `EXPECTED_REQUIRED`'s data changes, no new `node:test` case, plan §8; `.github/workflows/feed-tools.yml:86` `PLAN = "1..59"`) |
| **L-LINT** | `node tools/lint-serial-test-runs.js` (repo root) | exit 0 |
| **L-VLINT** | `node tools/lint-manager-version.js` (repo root) | exit 0, no stderr |
| **L-INDEX** | `node tools/gen-specs-index.js --check` (repo root) | exit 0 |
| **L-SCOPE** | `node specs/krate-manager-phase7/evidence/scope-check.js` (T009) | exit 0 |
| **L-WSL** (main loop) | `node specs/krate-manager-phase7/evidence/wsl-lane.js` (T009; TA-10): in WSL, `CARGO_TARGET_DIR=$HOME/km-target` (Phase 5/6's), `cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings && cargo test -p krate-manager-core --locked` | last line `WSL LANE GREEN …` and the three Linux-run Phase 7 names of TA-10 seen `ok`; it never closes a PENDING-CI row (plan §7.3, §8) |
| **L-CLOSE** | `node specs/krate-manager-phase7/evidence/close-lane.js` (T009; **main loop only**) | last line `CLOSE LANE GREEN` |

**Group gates** (run by the workflow's gate reporter). After G1: L-RS (nothing else executing), L-APP, L-DEVFEED,
L-GUARD, L-TS, L-VLINT; beside it the main loop may run L-WSL's two commands by hand (WSL's own cores): the first Linux
compile of `sha2` 0.11, `minisign-verify`, `minisign` and T002's code. After G2: L-RS, L-APP, L-DEVFEED, L-GUARD, L-TS,
L-VLINT, L-INDEX, `node --check` and `--self-test` on T009's three scripts, and L-WSL beside it (main loop). After G3:
everything T011 lists, then L-CLOSE (main loop).

---

## Preconditions — rulings before the build stage

Plan §10 (`plan.md:1464-1470`):

- **Q-A (plan SI-9, PD-5): decided** 2026-10-31, as recommended (`spec.md:1244-1245`): after a cached verified file
  fails re-verification, emit `Restarted { reason: CachedInvalid }` after its `Verifying` and before the fresh
  download's first request. T001 writes it into FR-020 / FR-046 / SC-029, T002 declares the variant, T005 builds and
  tests it (SC-029 b).
- **Q-B (plan PD-11): decided** 2026-10-31, **against** the revision 2 recommendation (`spec.md:1246-1250`): no
  `#[expect(clippy::too_many_arguments)]`; the shared inputs travel in `DownloadEnv<'a> { ctx, io, policy, cancel }`.
  T001 writes SI-25 / SI-26 into the spec, T002 declares `DownloadEnv`, `test_policy()` and `DownloadRig` and every
  signature at its Q-B arity, T007 adds `too_many_arguments_is_never_suppressed`. This file is the regeneration the
  ruling names, so it does not block T001 / T002.
- **R-1 (opened by Q-B): open, recommendation applied.** The `CancelToken` travels inside the env (`DownloadEnv::cancel`),
  not as a separate parameter; behaviour is unchanged (FR-017); `get_streamed` keeps its `&CancelToken` parameter.
  This file is written to that recommendation (plan §10 `plan.md:1470`). Starting the build stage after the user's
  review of spec, plan and tasks is their acceptance of it unless they overrule it.

**Precondition check (T002 runs it first, T001 too; on failure the task changes nothing and returns `blocked`, quoting
the item):** in `spec.md` `## Clarifications` (`spec.md:1226-1250`), a bullet that rules **against** R-1's
recommendation, or that marks any plan SI row or this file's TA row rejected. (Absence of a bullet is acceptance.) The
main loop then re-plans the named sections and regenerates the affected tasks.

### Task-list amendments (TA)

Rows marked **spec** change spec text (T001 writes them, tagged `(tasks TA-n)`); the others explain why a task differs
from a plan block.

| Id | Amendment | Why | Spec? |
|---|---|---|---|
| TA-1 | **Placeholder anchors (Rule 9).** T002 writes three `pub(crate)` helpers with their real bodies, because G2 tasks call them and a missing item is a compile error for everyone: `verify::hex_lower`, `VerifiedArtifact::relocated` (plan §4.3 `plan.md:505`, `:510`) and `#[cfg(test)] verify::sha256_of` (PD-13). SI-28 makes `verify::verify_file`, `verify::Expected` (type, fields, `from_request`) and `SigningKey::from_text` `pub(crate)` too (`plan.md:461`, `:468-473`, `:508-509`), so each needs a reader in G1 or it warns `dead_code`: (1) the placeholder `download::discard` body is `let _anchor = (crate::verify::hex_lower(artifact.sha256()), crate::verify::verify_file, crate::verify::Expected::from_request); let _moved = artifact.relocated(std::path::PathBuf::new()); let _ = _io; todo!()` (two fn items named as values, not called, so T007's "`verify_file (` only in …" rule holds; lib and test builds); (2) the placeholder `verify::verify_file(_path, expected, key)` body is `let _anchor = (&key.public, &expected.name, &expected.version, &expected.platform, &expected.asset, &expected.format, &expected.size, &expected.sha256, &expected.signature); todo!()` (field reads; derived impls do not count); (3) `SigningKey::from_text` is read by `release()` and by `test_support::kat_signing_key()`; (4) `sha256_of` is read by T002's `verify::tests::digest_helpers_cases`. `Expected::from_request(_request)` is `todo!()` in G1 (T004 writes it). `DownloadRequest` is `#[derive(Clone, Debug, PartialEq)] pub struct DownloadRequest { _private: () }` with `todo!()` accessors in G1; T005 gives it plan §4.4.1's fields (`plan.md:566-576`). T004 removes `verify_file`'s anchor, T005 `discard`'s. The anchor in `download.rs` also keeps T007's rule "`relocated (` called only in `download.rs`" true from G1 on | an uncalled `pub(crate)` item or an unread field warns `dead_code` at the G1 gate (Phase 6 TA-3 / TA-4, same cause); SI-28 turned three formerly `pub` items crate-private | no |
| TA-2 | **`test_signer.rs` is written whole by T002** (plan §4.6 `plan.md:882-902`, all items with real bodies), proven by `verify::tests::test_signer_round_trip` (T002, not in `required-tests.json`), and declared in `lib.rs` as `#[cfg(test)] mod test_signer;` | T004 and T005 both sign with it in G2; a helper written by one of them would be a compile dependency of the other inside the one core test binary | no |
| TA-3 | **The `test_support.rs` additions are written whole by T002** (plan §4.7 `plan.md:904-964`: `kat_signing_key`, `native_key`, `TestPlugin`, `test_feed`, `FaultyStagingIo` with `Op` (11 ops, `KindOf` included) and its constructors `healthy` / `failing` / `panicking_after` and readers `calls` / `calls_of` / `written`, `test_policy`, `DownloadRig`), proven by `test_support::tests::phase7_helpers_cases` (T002, not in `required-tests.json`). `FaultyStagingIo` delegates to `download::FsStagingIo`, whose bodies T005 writes; the G1 test covers only the paths that do not delegate (injected failures, call counting, `panic_after`) and the rig's wiring | every context builder of Phases 4-6 needs `kat_signing_key()` in G1 (SI-16, PD-14), and T005's tests need the rest; `test_support.rs` is shared by every test file | no |
| TA-4 | **T002 declares the three new `Reply` variants** (plan §4.8 `plan.md:968-974`: `Ranged { body }`, `Script(Vec<Reply>)`, `Paced { body, chunk, interval }`) with one `answer` arm `Reply::Ranged { .. } \| Reply::Script(_) \| Reply::Paced { .. } => todo!("T006")`; T006 replaces that arm and the routing | T005's tests name the variants; without them `download_tests.rs` does not compile | no |
| TA-5 | **The clippy-guard fixture is re-locked with `cargo metadata --manifest-path tests/fixtures/clippy-guard/Cargo.toml --format-version 1 > /dev/null`** (from `apps/krate-manager/`), not plan §4.1 step 2's `cargo update --workspace --manifest-path …` (`plan.md:313-314`), followed by the pair check of T002 step 1 | the workflow's ruled re-lock (the same command fixed Phase 5's and Phase 6's L-GUARD `--locked` refusal, commit `2f3c93d7` "krate-manager tasks re-lock the clippy-guard fixture"); `cargo metadata` resolves the fixture's missing entries and changes nothing it already has | spec: the New components row SI-3 adds names this command |
| TA-6 | **`SigningKey` (`from_text`, `release`, `key_id_hex`), `VerifyError`, `CancelToken`, `Target`'s `Display`, `DownloadError` with `code()`, `StagingStep::as_str`, `DownloadEnv` and the trivial accessors of `VerifiedArtifact` are real in T002**, red-first through `verify::tests::signing_key_text_cases` and `verify::tests::release_key_is_the_committed_key_and_not_a_test_key` (both in `required-tests.json`, both owned by T002) | `AppContext.signing_key` (FR-041) makes every Phase 4-6 context builder call `kat_signing_key()` → `SigningKey::from_text` at the G1 gate, and `src-tauri` calls `SigningKey::release()`, `CancelToken::new()` and builds a `DownloadEnv`; a `todo!()` there would turn ~170 existing tests red | no |
| TA-7 | **Guard table rows move with the code that needs them, in T002:** in `injection.rs`, `FR007_DEPENDENCIES` (13), the dev-dependency assertion `{flate2, minisign}`, `CONFINED_TOML_KEYS` (12), `PRODUCTION_BODIES` + `(HTTP_TESTS_RS, "streamed_policy_cases")` (10), `PAC_BODIES` + `(HTTP_TESTS_RS, "streamed_pac_fetch_keeps_the_feed_bound_unix")` (13), `TEST_PROGRAM_BODIES` + `(DOWNLOAD_TESTS_RS, "verified_file_has_no_quarantine_macos", "/usr/bin/xattr")` and `(DOWNLOAD_TESTS_RS, "staged_links_are_not_followed_windows", "cmd")` (4) with its doc comment "the program literal" (SI-15, SI-29) and the one const `DOWNLOAD_TESTS_RS` those rows use; in `config_guard.rs`, every existing expectation the wiring moves (plan §4.10.2 bullets 1, 2, 4, 5, 6, `plan.md:1063-1073`). T007 adds the 13 new `injection.rs` tests and their other consts (including `ARITY_SUPPRESSIONS`); T008 adds `config_guard.rs`'s handler pin and signing-crate walk | the G1 gate runs both guard binaries against T002's manifests and wiring; T003's and T005's new tests would otherwise be reported by Phase 5's body rules (`injection.rs:1759-1771`) and the TA-1 program rule (`:1794-1815`) inside G2. The rule checks only that a `Command::new` literal sits in a listed body (`:1797-1815`), not that a listed body exists, so the rows are safe before the tests exist | no |
| TA-8 | **`crates/core/tests/live_feed.rs`** gains its fourth argument (`kat_signing_key()`, PD-14) by a text edit that T002 checks with `grep -n "assemble_context(" crates/core/tests/live_feed.rs` (one hit this session, four top-level arguments after the edit) and records; it is never compiled with `live-network` | the feature is never enabled by any workflow, script or gate (`crates/core/Cargo.toml:12-13`), so no lane compiles the file (SI-16's "rot silently") | no |
| TA-9 | Rule 10's target slots (shared target for T005, `lib` for T003 / T004 / T006, `guard` for T007 / T008) | six G2 tasks build core; one private slot each would need six cold builds on a drive with 9.4 GB free (`df -h /f` this session); two slots cost two cold builds (~1.5 GB each, Phase 6 TA-10's measure) and T005, the long pole, keeps the warm shared target | no |
| TA-10 | L-WSL runs the full `cargo test -p krate-manager-core --locked` (Phase 6's form: `WSL LANE GREEN (192 passed, 24 s)`, `phase6/evidence/timings.md:34`) instead of plan §8's filter (`plan.md:1422`), and its information line counts the Linux-run Phase 7 names `download::tests::readonly_staging_dir_unix`, `download::tests::staged_symlinks_are_not_followed`, `http::tests::streamed_pac_fetch_keeps_the_feed_bound_unix` | the guard binaries (`injection`, `config_guard`, `roster`) lex sources and walk `Cargo.lock`, which can differ on Linux (path separators, the `cfg(not(windows))` `rquickjs` graph); the full run is a superset at under a minute warm | no |
| TA-11 | T001 records the base release binary (7,650,304 bytes, `target/release/krate-manager.exe`, 2026-10-31 19:07, `ls -l` this session; built by Phase 6's close lane, `phase6/evidence/timings.md:42` row B7) and the scope base `2f3c93d7` into `evidence/base-binary-size.txt`. T001 runs in G2; that is safe because no G1 or G2 lane builds the release profile (only L-CLOSE's `tauri build` does) | L-CLOSE reports the release-binary delta (Phase 6 row B7's form); the app sources at HEAD equal that build's, so no base rebuild is needed | no |
| TA-12 | The placeholder `download::channel_progress` returns a closure, `\|_event: Progress\| todo!()`, not a bare `todo!()` | a function returning `impl FnMut(Progress)` whose body is `todo!()` does not compile (the never type implements no `FnMut`) | no |
| TA-13 | Spec FR-033 (`spec.md:469-470`) says `key.public().verify_stream(…)`; plan §4.3 step 3 uses the private field `key.public` and declares no accessor (`plan.md:457`, `:538`). T001 aligns FR-033's wording to the field (no public accessor, so core's public API names no `minisign_verify` type) | one wording, one implementation; an unused `pub(crate) fn public` would warn and a `pub` one would leak the crate's type | spec: FR-033 |
| TA-14 | `test_support` also gains `pub fn test_feed_document(plugins: &[TestPlugin<'_>]) -> Vec<u8>` (the v2 JSON bytes plan §4.7's builder makes), and `test_feed` becomes `feed::parse(&test_feed_document(plugins))` plus the panic on rejection | SC-028 (`spec.md:956-972`) serves "a contract-valid v2 document" from the fixture server and then downloads its signed asset, so the test needs the bytes of a document whose signature is a `TestSigner` one; plan §4.7 returns only the parsed `Feed`. Row (c)'s v1-only entry is served from `tools/feed-contract/fixtures/feeds/valid/mixed.json` read in place | no |
| TA-15 | **`streamed_body_cases`' over-limit row asserts "at most `max_body` bytes, a prefix of the body", not "exactly `n - 1000`".** Plan §4.2.4 step 8 (`plan.md:438-439`) says a chunk that would take the total past `max_body` "is not passed on" and ends `TooLarge`, while §7.1's row (`plan.md:1308`) says "exactly `n - 1000` bytes passed to `sink`". With a 64 KiB read buffer a small body arrives in one read, so step 8 passes 0 bytes and the "exactly" row cannot hold. The algorithm of §4.2.4 is the module design and FR-014's rule ("nothing past `size` written") holds under it; T003 implements step 8 as written and asserts `passed ≤ n - 1000`, `passed` bytes equal the body's prefix, the result `Http(TooLarge { max: n - 1000 })`, and `StreamEnd` never returned. The companion row (`Read { max_body: n }` streams all `n` bytes) still proves that the limit comes from `on_head` (SI-27) | the two plan sentences cannot both hold for a one-read body; the stronger safety property (never past the limit) is kept | no |

---

## Dependency overview

| Group | Mode | Tasks | Needs |
|---|---|---|---|
| G1 Foundation | **sequential** (1) | T002 precondition, manifests, both lockfiles, every shared line, every public type of `download` / `verify` / the `http` additions, `DownloadEnv`, `SigningKey`, `test_signer.rs`, `test_support` additions (`DownloadRig`, `FaultyStagingIo`, `test_policy`, `test_feed`), `AppContext.signing_key` and its ~45 construction sites, the `Reply` variants, `src-tauri` wiring, the guard expectations the edits move | Preconditions |
| G2 Spec, modules, guards, scripts | **[P]** (8) | T001 spec amendments + base records · T003 `http::get_streamed` · T004 `verify::verify_file` + KAT + doctests · T005 `core::download` (the long pole) · T006 fixture-server replies · T007 `injection.rs` guards · T008 `config_guard.rs` + `roster.rs` guards · T009 evidence scripts | G1 |
| G3 Integration | sequential (2, dependent) | T010 `required-tests.json` + the shape test (the per-OS CI additions) · T011 INDEX, lints, scope, cleanup, the PENDING-CI list, hand-over of L-CLOSE | G2 |

Why this shape: T002 is the only task that edits shared files, so it is a group of its own; it fixes every shared line,
every public type and signature, and the shared test helpers up front, so G2 holds the spec work, every module, every
guard and the scripts side by side (eight at once). The long pole is `download` (T005), which cannot be split: the
plan names all its tests `download::tests::*` in one module over one test file (`plan.md:1098-1164`), and the guards
pin single-file facts of `download.rs` (`read_dir` once, `relocated (` only there, the seam declarations; plan §4.10.1
`plan.md:1049-1055`), so a second writer of either file would be a conflict, not parallelism. The in-group
dependencies run one way only and settle before the gate: T005's tests run T003's, T004's and T006's code, T007 scans
T005's file. G3 is two dependent tasks (T011 checks T010's lists). No group is a chain of independent tasks, and no
G2 task adds a line to a shared file.

**SC owners:** SC-001 T007 (guard) + T011 (+ L-CLOSE) · SC-002, SC-003, SC-005 to SC-009, SC-011 to SC-014, SC-026 to
SC-037 T005 (SC-034 e through `http::get_streamed`) · SC-004 T004 · SC-010 T003 · SC-015 T004 (doctests) + T007 (guard)
· SC-016 T002 · SC-017 T007 · SC-018 T002 (manifests, `FR007_DEPENDENCIES`) + T007 (confinement) + T008 (signing-crate
walk, roster lists) + L-CLOSE (`cargo tree` record) · SC-019 T002 (wiring, permissions, `build.rs`) + T008 (handler pin)
+ T005 (`request_for_cases`) · SC-020 T006 · SC-021, SC-022, SC-023 T010 (lists) + CI · SC-024 L-CLOSE (T009's row) ·
SC-025 T009 (scope check, FR-063 check) + T011 · SC-032 T005 (seam cases) + T007 (guard) · FR-041 `reassemble` T002 ·
FR-043 minting routes (SI-28) T002 (visibility) + T007 (guard).

---

## G1 — Foundation (sequential: one task, every shared file)

### T002 — Precondition, manifests, both lockfiles, every shared line, the public types, `DownloadEnv`, the shared test helpers, `AppContext.signing_key` and the `src-tauri` wiring (spec FR-017 type, FR-020, FR-039 to FR-042, FR-045 / FR-060 signatures, FR-050 to FR-052 types, FR-060 to FR-064 wiring, FR-080, FR-083 helpers, FR-084 variants; SC-016, SC-018 part, SC-019 part; plan §2, §3, §4.1, §4.2.1, §4.2.4 types, §4.3 types, §4.4.1, §4.4.4 types, §4.4.8 to §4.4.10 signatures, §4.5 to §4.7, §4.8 variants, §4.9, §4.10.1 table rows, §4.10.2 bullets 1, 2, 4-6; PD-1, PD-3 to PD-6, PD-10, PD-11, PD-13, PD-14, PD-18; SI-2 to SI-4, SI-13, SI-16, SI-17, SI-25, SI-27 types, SI-28 visibility, SI-29 trait; TA-1 to TA-8, TA-12, TA-14)

This task makes every edit to a shared app file the phase needs (except `required-tests.json` and the shape test, T010),
so G2 touches only its own files.

**Files (edit):** `apps/krate-manager/Cargo.toml`, `Cargo.lock`, `tests/fixtures/clippy-guard/Cargo.lock`,
`crates/core/Cargo.toml`, `crates/core/src/lib.rs`, `crates/core/src/http.rs` (public types only),
`crates/core/src/context.rs`, `crates/core/src/context_tests.rs`, `crates/core/src/{detect,feed,http,status}_tests.rs`
(the `AppContext` literals and assemble calls only), `crates/core/tests/live_feed.rs` (one argument),
`crates/core/src/fixture_server.rs` (three variants, one arm), `crates/core/src/test_support.rs`,
`crates/core/src/test_support_tests.rs`, `crates/core/tests/injection.rs` (tables only, TA-7),
`crates/core/tests/config_guard.rs` (existing expectations only, TA-7), `src-tauri/src/commands.rs`,
`src-tauri/src/lib.rs`, `src-tauri/build.rs`, `src-tauri/capabilities/main.json`.
**Files (new, LF):** `crates/core/src/verify.rs`, `crates/core/src/verify_tests.rs`, `crates/core/src/download.rs`,
`crates/core/src/download_tests.rs` (one `//!` line), `crates/core/src/test_signer.rs`.

**Precondition.** Run the check of "Preconditions" above first; on failure return `blocked`, changing nothing. Then
`df -h /f` into the notes (9.4 GB free this session; below 5 GB, return `blocked` naming the number). ODR check (plan
§1.1 `plan.md:174`): `grep -rn "struct DownloadEnv\|struct DownloadRig\|fn test_policy\|struct TestSigner\|struct
FaultyStagingIo\|struct VerifiedArtifact\|struct SigningKey\|struct CancelToken" apps/ tools/` is empty before the
first write; record it.

**Test first.**
- **The guards are the first red.** Edit the guard expectations of step 2 **before** the manifests and sources, then
  run `cargo test -p krate-manager-core --locked --test injection --test config_guard` (shared target) and quote the red
  lines: `core_dependencies_are_the_fr007_set` (`[dependencies]` lacks `minisign-verify`, `sha2`; dev set lacks
  `minisign`), `real_src_tauri_config_passes_the_guard` (five permissions / commands, three `assemble_context(`
  arguments, `SigningKey::release()` missing from `lib.rs`), `planted_config_changes_fail_the_guard` (the
  `assemble_context(` plant's anchor text is absent from the real `lib.rs`). They go green when steps 1 and 3-10 land.
- `verify_tests.rs` (written before `verify.rs`'s bodies; the file compiles against the step 4 signatures with
  `todo!()` bodies first):
  - `verify::tests::signing_key_text_cases` (plan §7.1 row "(plan)", `plan.md:1338`, binding): `from_text("")`, a
    one-line text, a text whose line 2 is not base64, and a 41-byte key → each `code() == "E_VERIFY_KEY_TEXT"`; the KAT
    key (`tools/feed-contract/fixtures/kat/kat.pub`, read in place with `fs::read_to_string` from
    `CARGO_MANIFEST_DIR/../../../../tools/…`) parsed from its LF text and from a CRLF copy built in memory → equal;
    parsed twice from two separate reads → equal; `!= from_text(<fixtures/phase2/kat-secret/kat.pub>)`;
    `key_id_hex() == "440FA75168B852F7"`.
  - `verify::tests::release_key_is_the_committed_key_and_not_a_test_key` (SC-016, `plan.md:1315`, binding):
    `tools/feed-contract/keys/krate-signing.pub` read at test time as `file`; `SigningKey::release() ==
    SigningKey::from_text(&file)?`; `!=` both KAT keys parsed with `from_text`; `key_id_hex() == "9FEB36F9114273BC"` and
    differs from `440FA75168B852F7` and `63861317F7D60239`; line 2 of `RELEASE_KEY_TEXT` decodes (`STANDARD`) to the
    same 42 bytes as line 2 of `file`, and its bytes 10..42 differ from both KAT keys' bytes 10..42. This is the only
    test body that may name `SigningKey::release` (FR-087).
  - `verify::tests::digest_helpers_cases` (TA-1; not in `required-tests.json`): `sha256_of(b"")` =
    `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`, `sha256_of(b"abc")` =
    `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad` (the FIPS 180-2 vectors), each through
    `hex_lower`; `hex_lower(&[0x00, 0xab, 0xff]) == "00abff"`.
  - `verify::tests::test_signer_round_trip` (TA-2; not in `required-tests.json`): `TestSigner::new()` twice → different
    `key_id()`; `signer.key().key_id_hex()` equals `format!("{:016X}", u64::from_le_bytes(signer.key_id()))`;
    `sign_c3(data, "Alpha", "1.2.3", native_key(), "Alpha-1.2.3-test.bin")` parses with `contract::parse_minisig_text`
    (four LF lines, final LF), its `key_id` equals `signer.key_id()`, its trusted comment equals `c3_comment(…)` and
    parses with `contract::parse_trusted_comment` to those four fields; `with_key_id(sig, other_id)` parses, carries
    `other_id`, and differs from `sig` only in line 2.
  Red: before step 4 the file does not compile; after the signatures land, each test fails on `not yet implemented`;
  quote one line per test.
- `test_support_tests.rs` (CRLF, Rule 12): `test_support::tests::phase7_helpers_cases` (TA-3; not in
  `required-tests.json`): `kat_signing_key().key_id_hex() == "440FA75168B852F7"`; `native_key()` is the first element of
  `version::platform_candidates(NativePlatform.os(), Arch::native())`; `test_feed(&[alpha, bravo])` (two `TestPlugin`s
  on `native_key()` with the KAT `.minisig` text as `signature`, read in place) parses (the builder panics otherwise) and
  yields exactly the keys `Alpha`, `Bravo`, each with one `platforms` entry under `native_key()` whose `url` is
  `https://assets.example.invalid/alpha/v1.2.3/<asset>` (resp. `bravo`), `size`, `sha256` and `signature` equal the
  inputs, and the `format` the key's prefix requires; `feed::parse(&test_feed_document(&[alpha, bravo]))` equals that
  `Feed` (TA-14); `FaultyStagingIo::failing(Op::CreateDir, 1, ErrorKind::PermissionDenied)` → the first
  `create_dir_all` returns that kind without touching the disk (the target path, under the `fresh_temp_roots()` base,
  does not exist afterwards), `calls() == 1` and `calls_of(Op::CreateDir) == 1`, `calls_of(Op::KindOf) == 0`;
  `failing(Op::KindOf, 1, PermissionDenied)` → the first `kind_of` returns that kind; `panicking_after(1)` around a
  failing call → `catch_unwind` reports a panic after the call returned; `test_policy() == DownloadPolicy { attempts: 4,
  backoff: vec![Duration::ZERO], stall: Duration::from_secs(2), attempt_deadline: Duration::from_secs(10) }`;
  `DownloadRig::new(ctx)` (a `fresh_temp_roots()` `AppContext` literal with `signing_key: kat_signing_key()`): `rig.env()`'s
  four fields point at the rig's own parts (`std::ptr::eq` on `ctx`, `policy`, `cancel`; `io` by address of
  `rig.io` cast to `*const ()`), `rig.policy == test_policy()`, `!rig.cancel.is_cancelled()`, and a compile-time
  `fn sync<T: Sync>() {}` / `sync::<DownloadRig>()` holds (plan §4.7 `plan.md:957-960`). Red: does not compile before
  step 7; quote the error.

**Implement.**
1. **Manifests and lockfiles** (plan §4.1 `plan.md:290-316`, TA-5; one line per entry, the reason on a full-line `#`
   comment above). Root `[workspace.dependencies]` (`Cargo.toml:13-42`, alphabetical): add `minisign = "0.10.0"` and
   `minisign-verify = "0.3.0"` after `log` (`:19`) and `sha2 = "0.11.1"` after `serde_json` (`:29`), each with plan
   §4.1's comment line (`plan.md:295-300`). `crates/core/Cargo.toml`: `[dependencies]` (`:15-28`) gains
   `minisign-verify = { workspace = true }` after `log` and `sha2 = { workspace = true }` after `serde_json`, each under
   the comment `# Phase 7 D1: verify.rs only (FR-082)` (13 keys); `[dev-dependencies]` (`:30-31`) gains `minisign = {
   workspace = true }` under `# Phase 7 D1: test_signer.rs only, never a normal dependency (FR-081)`. No features, no
   target section. Then, from `apps/krate-manager/`: `cargo update --workspace` (plan §4.1 step 1; it downloads
   `minisign-verify`, `sha2-0.11.1`, `minisign-0.10.0` from crates.io). **Additive check:** a throwaway Node script under
   `F:/tmp/gate/km7-T002/` (Write tool) compares the `[[package]]` (name, version) pairs of `git show
   HEAD:apps/krate-manager/Cargo.lock` with the new file: every old pair must remain; list the added pairs in the notes
   (plan §4.1 names the expected set). A missing old pair is restored with `cargo update -p <name> --precise <old>`
   before anything else runs. Then the fixture lock (TA-5): `cargo metadata --manifest-path
   tests/fixtures/clippy-guard/Cargo.toml --format-version 1 > /dev/null`, and the same script checks that every (name,
   version) pair of the fixture lock is in the workspace lock and every old fixture pair remains, and lists the
   workspace-only pairs (expected: `minisign` and the packages only it reaches, since the fixture reaches core through a
   path dependency, whose dev-dependencies it does not resolve). Then `cargo build --workspace --locked` and record its
   wall clock, and `cargo tree -p krate-manager-core -e normal --locked --prefix none --format "{p}"` into the notes:
   `minisign-verify v0.3.0` and `sha2 v0.11.1` present, no `minisign `, `rpassword`, `scrypt` line (plan §4.1 step 3).
2. **The guard expectations the edits move** (TA-7; written first, see Test first):
   - `injection.rs`: `FR007_DEPENDENCIES` (`:101-113`) → `[&str; 13]` with `"minisign-verify"` after `"log"` and
     `"sha2"` after `"serde_json"`; `core_dependencies_are_the_fr007_set`'s dev set (`:2630-2634`) →
     `BTreeSet::from(["flate2".to_owned(), "minisign".to_owned()])`; `CONFINED_TOML_KEYS` (`:252-268`) → `[_; 12]` with
     `("minisign-verify", &["Cargo.toml", "crates/core/Cargo.toml"])`, `("sha2", same)`, `("minisign", same)`; `const
     DOWNLOAD_TESTS_RS: &str = "crates/core/src/download_tests.rs";` beside `DETECT_TESTS_RS` (`:142`);
     `PRODUCTION_BODIES` (`:284-294`) → 10 with `(HTTP_TESTS_RS, "streamed_policy_cases")`; `PAC_BODIES` (`:304-317`) →
     13 with `(HTTP_TESTS_RS, "streamed_pac_fetch_keeps_the_feed_bound_unix")`; `TEST_PROGRAM_BODIES` (`:341-344`) → 4
     with `(DOWNLOAD_TESTS_RS, "verified_file_has_no_quarantine_macos", "/usr/bin/xattr")` and `(DOWNLOAD_TESTS_RS,
     "staged_links_are_not_followed_windows", "cmd")`, and its doc comment (`:339-340`) ends "the program literal"
     instead of "bare program name resolved through `PATH`" (SI-15). Nothing else in the file changes here.
   - `config_guard.rs`: the `permissions` expectation (`:607-626`) gains `"allow-download-plugin"` last; the `build.rs`
     check (`:640-653`) expects `commands(&["app_version","startup_error","plugin_status","check_now","open_release_page",
     "download_plugin"])` and its message names the six (FR-061); the `assemble_context(` count (`:831-838`) expects four,
     message "expected four (FR-061, Phase 7 FR-041)"; the plant at `:1704-1712` becomes "assemble_context( with three
     arguments", replacing `assemble_context(roots, &feed_url, http, SigningKey::release())` with
     `assemble_context(roots, &feed_url, http)` and expecting `calls \`assemble_context(\` with 3 top-level arguments`;
     `LIB_RS_STRINGS` (`:46-61`) → `[&str; 15]` with `"SigningKey::release()"`; the plant labelled "sixth capability
     permission allow-planted" (`:1543`) is relabelled "seventh capability permission allow-planted" (body unchanged).
3. **`crates/core/src/lib.rs`** (`:1-24`), alphabetical as the file is: `pub mod download;` right after `pub mod
   detect;` (`:5`); `#[cfg(test)]` + `mod test_signer;` right before the `test_support` pair (`:22-23`; `test_signer` <
   `test_support`); `pub mod verify;` right before `pub mod version;` (`:24`). Nothing else.
4. **`verify.rs`** — plan §4.3 (`plan.md:451-512`) exactly, with: `RELEASE_KEY_TEXT`; `SigningKey` (`Clone, Debug,
   PartialEq, Eq`; fields `public`, `key_id`) with real `pub(crate) fn from_text` (`minisign_verify::PublicKey::decode`,
   then line 2 decoded with `STANDARD` for `key_id = bytes[2..10]`; any failure → `VerifyError::KeyText { detail }`),
   `pub fn release()` (`from_text(RELEASE_KEY_TEXT)`, panicking with the error), and `pub fn key_id_hex()`;
   `pub(crate) struct Expected<'a>` with `pub(crate)` fields including `format: AssetFormat` (PD-4, SI-28) and
   `pub(crate) fn from_request` as `todo!()`; `VerifiedArtifact` (`Clone, Debug`) with its eight private fields, the
   eight real accessors and the real `pub(crate) fn relocated`; `pub(crate) fn verify_file` as the TA-1 anchor +
   `todo!()`; real `pub(crate) fn hex_lower`; real `#[cfg(test)] pub(crate) fn sha256_of` (`sha2`); `VerifyError`
   exactly plan §3's table (`plan.md:269-281`) with `code()`; the Rule 3 test declaration. No doctest yet (T004's).
   `CHUNK` is T004's (a private const nobody reads would warn).
5. **`http.rs`** — public types only: `CancelToken` exactly plan §4.2.1 (`plan.md:322-332`) with real bodies (TA-6);
   `StreamLimits { stall, deadline }` (no `max_body`, SI-27), `ResponseHead`, `HeadAction::{Read { max_body: u64 },
   Stop}`, `StreamEnd`, `StreamError` exactly plan §4.2.4 (`plan.md:394-413`); `pub fn get_streamed(…)` with the plan's
   seven-parameter signature (`plan.md:415-420`) and a `todo!()` body (`_`-prefixed parameters). `AgentPurpose`,
   `agent_config`, `walk`, `run_chain`, `ChainError` stay untouched (T003's).
6. **`download.rs`** — every `pub` item of plan §4.4.1 (`plan.md:557-622`): the five `pub const`s, `pub use
   crate::http::CancelToken;`, `DownloadRequest { _private: () }` (TA-1) with `todo!()` accessors, `DownloadPolicy`
   (`production()` as `todo!()`), **`DownloadEnv<'a>` real** (`#[derive(Clone, Copy)]`, four `pub` borrow fields `ctx`,
   `io: &'a dyn StagingIo`, `policy`, `cancel`, with plan §4.4.1's doc comment), `RestartReason` (five variants with
   `CachedInvalid`, PD-5), `Progress`, `Obtained`, `DownloadSummary`, `HeadVerdict`, `Target` (real `Display`); §3's
   `DownloadError` (`plan.md:242-264`, PD-10's `Box<Target>`, PD-18's `DiscardBusy { name, asset }`) with `code()` and
   the code-first `Display` (`<CODE>: <name> <asset> (<url>): <what>`), `Unavailability`, `StagingStep` (+ real
   `as_str`), `StagingCode`; §4.4.4's (`plan.md:652-669`) `PartialFile`, `EntryKind`, `StagedEntry`, `StagingIo` with
   its **eight** methods (`create_dir_all`, `kind_of`, `len_of`, `open_partial`, `rename`, `remove_file`,
   `remove_dir_all`, `read_dir_entries`; SI-29) and `pub struct FsStagingIo;` with an `impl StagingIo for FsStagingIo`
   whose eight methods are `todo!()`; and `pub fn` placeholders at their Q-B signatures (no attribute, Rule 2):
   `request_for`, `classify_head(requested_from, size, head)`, `obtain_and<T>(env: DownloadEnv<'_>, feed, request,
   progress, next)` (`plan.md:753-757`), `discard(artifact, io)` (the TA-1 anchor), `command_request(ctx, name,
   expected_version, now_unix, host)`, `run_command(env, feed, request, progress)`, `for_command(env, name,
   expected_version, now_unix, host, progress)` (`plan.md:851-858`) and `channel_progress` (TA-12). No private item
   (`BACKOFF_SLICE`, `InFlight`, `IN_FLIGHT`, `Meter`, `scrub`, `StagedPaths`, `staged_paths`, `with_fixture_url`,
   `fetch_url`, `fetch_target`, `obtain_body`, `prune`, `wait_or_cancel` are T005's). The Rule 3 declaration;
   `download_tests.rs` is one `//!` line.
7. **`test_signer.rs`** (TA-2) exactly plan §4.6 (`plan.md:887-900`), real bodies, `pub(crate)` items, no file I/O; a
   `//!` line saying it is `#[cfg(test)]` only (D1). **`test_support.rs`** (TA-3, TA-14) exactly plan §4.7
   (`plan.md:906-964`): `kat_signing_key()` (`include_str!("../../../../../tools/feed-contract/fixtures/kat/kat.pub")`,
   the depth `contract.rs:11-14` uses, then `SigningKey::from_text`), `native_key()`, `TestPlugin`,
   `test_feed_document` (a `serde_json::json!` v2 document satisfying `tools/feed-contract/versions.v2.schema.json`,
   serialised to bytes) and `test_feed` (`feed::parse` of those bytes, panicking with the findings), `FaultyStagingIo`
   with `Op` (11 variants `CreateDir | KindOf | LenOf | Open | Truncate | Write | Sync | Rename | RemoveFile | RemoveDir
   | ReadDir`; std atomics only; a private wrapper `PartialFile` that counts `write_all` / `sync`; no `static`, no
   `Mutex`: the static-lock rule, `injection.rs:1846-1900`), its constructors and readers, `test_policy()` and
   `DownloadRig` (`new`, `env`) as `plan.md:937-950`. It uses only std and core's normal dependencies (no `minisign`,
   no `TestSigner`, no `sha256_of`: those are `#[cfg(test)]` and `test_support` also compiles under `test-support`,
   D1). No `127.0.0.1:<port>`, no `/usr/`, no `ProgramData` (`FR088_PATTERNS`, `injection.rs:207-221`), no
   `SigningKey::release`.
8. **`AppContext.signing_key`** (plan §4.5 `plan.md:876-880`, SI-16, PD-14): `context.rs` gains `pub signing_key:
   SigningKey` (`:49-56`), the fourth parameter of `assemble_context` (`:69-75`) and `assemble_with_os` (`:79-84`)
   passed into the literal (`:102-107`), and `reassemble_with_os` copying `ctx.signing_key.clone()` (`:162-167`). Then
   every construction site (located this session): the `AppContext` literals at `detect_tests.rs:101`, `:1985`;
   `feed_tests.rs:249`, `:1673`, `:1756`; `http_tests.rs:130`, `:1644`, `:1972`; `status_tests.rs:79` gain `signing_key:
   kat_signing_key()` (or `crate::test_support::kat_signing_key()` where the file has no import); every
   `assemble_context(` / `assemble_with_os(` call in `context_tests.rs` (28 sites), `detect_tests.rs` (2) and
   `tests/live_feed.rs` (1, TA-8) gains `kat_signing_key()` as its last argument. `context::tests::reassemble_cases`
   gains `assert_eq!(<reassembled ctx>.signing_key, <prev ctx>.signing_key)` on each success row (plan §7.2 FR-041 row,
   `plan.md:1356`).
9. **`fixture_server.rs`** (TA-4): the three variants of plan §4.8 appended to `Reply` (`:15-50`) with their doc
   comments, and one `answer` arm (`:288`) `Reply::Ranged { .. } | Reply::Script(_) | Reply::Paced { .. } =>
   todo!("T006")`. Nothing else.
10. **`src-tauri`** (plan §4.9 `plan.md:987-1025`): `commands.rs` gains `download_plugin` exactly as the plan writes it
    (`plan.md:991-1012`: the `DownloadEnv` built inside the `spawn_blocking` closure from `ctx`, `policy =
    DownloadPolicy::production()` and `cancel = CancelToken::new()` that the closure owns; the `use` lines for
    `krate_manager_core::download::{self, CancelToken, DownloadEnv, DownloadError, DownloadPolicy, DownloadSummary,
    FsStagingIo, Progress}`); `lib.rs` gains `use krate_manager_core::verify::SigningKey;`, the call
    `assemble_context(roots, &feed_url, http, SigningKey::release())` (`:56`) and `commands::download_plugin` last in
    `generate_handler!` (`:86-92`); `build.rs` gains `"download_plugin"` last (`:3-9`); `capabilities/main.json` gains
    `"allow-download-plugin"` last (`:5-11`), key order and description unchanged.

**Done when:** the three guard tests above, `signing_key_text_cases`, `release_key_is_the_committed_key_and_not_a_test_key`,
`digest_helpers_cases`, `test_signer_round_trip` and `phase7_helpers_cases` are green; L-RS green with every existing
test still green (no placeholder is reached: nothing calls `get_streamed`, `verify_file` or any `download` function in
G1); L-APP, L-DEVFEED, L-GUARD (the `--locked` fixture run passes resolution: `other-errors=0`), L-TS (frontend
unchanged), L-VLINT green; `grep -rn "too_many_arguments\|#\[expect(" crates src-tauri/src --include=*.rs` shows no
line this task added (Rule 2); the notes hold `git diff --stat Cargo.lock`, the additive-check and fixture-pair output,
the `cargo tree` lines, TA-8's grep line, and the cold build's wall clock. **Lane:** L-RS (shared target, alone), L-APP,
L-DEVFEED, L-GUARD, L-TS, L-VLINT.

---

## G2 — Spec, modules, guards and scripts [P] (eight tasks, disjoint files, Rule 10 slots)

### T001 [P] — Precondition check, the spec amendments and the base records (plan §0.2 SI-1 to SI-30, §10; TA-5, TA-11, TA-13)

**Files:** `specs/krate-manager-phase7/spec.md` (edit, CRLF); new `specs/krate-manager-phase7/evidence/base-binary-size.txt`
(LF). `specs/INDEX.md` is not touched here (T011 regenerates it once).

**Precondition.** Run the check of "Preconditions" above first; on failure return `blocked`, changing nothing.

**Test first.** Docs: before editing, record in the notes that the amendments are absent (the twelve `grep -c` counts of
the header's "Spec state", each `0`), `git rev-parse --short HEAD` (expected `2f3c93d7`: the **scope base** T009's
scope check diffs against), and `node tools/gen-specs-index.js --check`'s exit code.

**Implement — base records (TA-11).** Write into `evidence/base-binary-size.txt` the output of `ls -l
--time-style=long-iso apps/krate-manager/target/release/krate-manager.exe` (expected 7650304 bytes, 2026-10-31 19:07),
`git log --format='%h %ci' -1 -- apps/krate-manager` (expected `03d97890 …`), `git diff --stat 03d97890 2f3c93d7 --
apps/` (expected empty) and the line `scope base: 2f3c93d7`. A different value or a missing file is recorded as found,
not fixed.

**Implement — spec rows.** A row counts as already applied when the spec text already says what its fix says (note
"already present" and the line). Otherwise edit it in place and append `(plan SI-n)` or `(tasks TA-n)` to the changed
sentence. Plan §0.2's "Proposed spec edit" column (`plan.md:67-96`) is the binding wording, with these targets:
- SI-1 → SC-004 (`spec.md:773-785`) and FR-085 (`:684-692`): the expected archive SHA-256 is
  `5b980e5b8ae7811a064966a1ec1380df016081a12bdeaf643486026007b62890` (`fixtures/kat/README.md`), recomputed with
  `verify::sha256_of`; the `.sha256` file is not read.
- SI-2 → FR-041 (`:507-516`), FR-061 (`:604-607`) and the Existing-pieces table (`:81-134`, one row for
  `config_guard.rs:831-838`, `:1704-1712`, `:46-61`).
- SI-3 → New components (`:1124-1145`, one row for `tests/fixtures/clippy-guard/Cargo.lock`) with TA-5's command.
- SI-4 → FR-086 (`:693-701`) and FR-087 (`:702-707`).
- SI-5, SI-10, SI-21 → FR-082 (`:655-663`) and FR-050 (`:559-562`).
- SI-6 → FR-010 (`:345-364`), FR-011 (`:365-375`), FR-012 (`:376-378`), SC-010 (`:839-853`).
- SI-7 → FR-001 (`:307-315`), FR-005 (`:332-337`). SI-8 → FR-030 (`:460-463`).
- SI-9 (Q-A) → FR-020 (`:428-432`), FR-046 (`:534-537`), SC-029 (`:973-980`).
- SI-11, SI-18 → FR-013 (`:379-389`); SI-18 also the External facts `ureq` row (`:143`) and SC-009 (e) (`:825-838`,
  rewritten as rows e1-e4).
- SI-12 → FR-015 (`:396-398`), FR-023 (`:447-457`). SI-13, SI-20 → FR-052 (`:572-577`); SI-20 also FR-049
  (`:545-556`) and SC-035 (`:1029-1036`, a new (f)).
- SI-14, SI-23, SI-29 and SI-30 → FR-086: the three OS lists become exactly plan §4.11's (`plan.md:1158-1165`):
  `windows` + `download::tests::prune_skips_a_file_open_without_share_delete_windows`,
  `download::tests::verified_file_has_no_zone_identifier_windows`, `download::tests::staged_links_are_not_followed_windows`;
  `macos` + `download::tests::readonly_staging_dir_unix`, `download::tests::verified_file_has_no_quarantine_macos`,
  `http::tests::streamed_pac_fetch_keeps_the_feed_bound_unix`, `download::tests::staged_symlinks_are_not_followed`;
  `linux` + `download::tests::readonly_staging_dir_unix`, `http::tests::streamed_pac_fetch_keeps_the_feed_bound_unix`,
  `download::tests::staged_symlinks_are_not_followed`; SI-14's names (§4.11's guard self-tests and the three plan tests),
  SI-26's `too_many_arguments_is_never_suppressed` and SI-30's `download::tests::prune_spares_in_flight_artifact` and
  `download::tests::non_retryable_transport_errors` join FR-086's `cargo.all` sentence. SI-23 also FR-005, FR-031
  (`:464-465`). SI-29 also FR-050 (`StagingIo::kind_of`; the Windows `remove_dir_all` retry), FR-046 (the
  `kind_of(<hex>)` probe) and TA-1's `TEST_PROGRAM_BODIES` row in FR-086.
- SI-15 → no edit ("already present": SC-031 names `/usr/bin/xattr`, `:990-1000`).
- SI-16, SI-17 → New components (one row listing the Phase 4-6 test files that gain `signing_key: kat_signing_key()` or
  the fourth argument) and FR-041.
- SI-19 → SC-037 (`:1046-1056`, as revised in review round 2: `stall = 1 s`, 10 chunks 150 ms apart, `attempt_deadline
  = 5 s`, `elapsed > policy.stall` and ≥ 1.35 s, no assertion on `Retrying`). SI-22 → FR-083 (`:664-675`) and SC-030
  (`:981-989`, the new (d), the FR-053 counts).
- SI-24 → SC-002, SC-006 to SC-009, SC-029, SC-034: one sentence each that every recorded run passes
  `assert_progress_invariants(events, size, outcome)` (plan §7 `plan.md:1283-1291`).
- SI-25 (Q-B) → FR-045 (`:526-533`: new first sentence on `DownloadEnv` and the five-parameter signature), FR-060
  (`:585-603`: `run_command` / `for_command` signatures and the command's call), FR-017 (`:408-419`: R-1's wording),
  and the New components `download.rs` / `test_support.rs` rows (`DownloadEnv`, `test_policy()`, `DownloadRig`).
- SI-26 → FR-086 (`cargo.all` gains `too_many_arguments_is_never_suppressed`) and SC-001's threshold (`:734-740`).
- SI-27 → FR-010 (`StreamLimits { stall, deadline }`; `HeadAction::{Read { max_body }, Stop}`), FR-013 (`:382`, the
  per-verdict `Read` limits), FR-014 (`:390-395`).
- SI-28 → FR-030 (`pub(crate) fn verify_file`, `pub(crate) struct Expected`), FR-040 (`:499-506`, `pub(crate) fn
  from_text`), FR-043 (`:517-523`, the call-site rules and plants).
- SI-30 → SC-014 (`:880-889`, the in-flight prune case), SC-009 (the non-retryable transport rows), SC-028
  (`:956-972`, row f).
- TA-13 → FR-033 (`:469-470`): `key.public.verify_stream(&signature)` (the private field, no accessor).

**Done when:** every row carries its change and tag (or "already present" with its line); `grep -c $'\r$' spec.md` equals
`wc -l < spec.md`; the twelve "Spec state" needles each count ≥ 1; `grep -c "too_many_arguments" spec.md` shows the
lint named only as forbidden (no sentence still prescribing `#[expect]`); the notes list each edited row id, the scope
base and the base-record lines. **Lane:** none (docs; L-INDEX runs in T011).

### T003 [P] — `http::get_streamed`, `AgentPurpose::Download` and the generic hop loop (spec FR-010 to FR-012, FR-017 transport part; SC-010; plan §4.2.2 to §4.2.5, §7.1 rows SC-010 and "(plan) streamed_body_cases"; PD-1, PD-2, PD-17, SI-27; TA-15)

**Files:** `crates/core/src/http.rs` (replace `get_streamed`'s placeholder body; add the private items),
`crates/core/src/http_tests.rs`.
**Slot:** `lib`, `CARGO_BUILD_JOBS=10`. Filter: `--lib http::`.

**Order (Rule 10).** First add `AgentPurpose::Download { stall: Duration }` (`http.rs:622-627`) with an `agent_config`
arm that, for now, builds the `Feed` configuration, and `hop_rule`'s `Download` arm (`:976`); the crate compiles. Then
the tests (they compile and fail). Then the implementation.

**Test first** (`http_tests.rs`; every server is a `FixtureServer` on loopback; the existing Phase 5 helpers `gzip`
(`:110-114`), the `context` builder (`:129`) and the polling helper are reused; no new `Reply` variant is used, so this
task does not wait for T006):
- `http::tests::streamed_policy_cases` (SC-010, `plan.md:1307`, binding): loopback config refuses
  `https://example.invalid/x` with 0 server requests (`E_FEED_URL`); a redirect to `https://example.invalid/` →
  `StreamError::Http(HttpError::Url { .. })` (`code() == "E_FEED_URL"`); a 6-hop redirect chain → `E_FEED_REDIRECT`;
  `agent_config(&cfg_3600, None, AgentPurpose::Download { stall: Duration::from_secs(30) })` (three arguments, PD-1):
  the accept / accept-encoding values `application/octet-stream` / `identity`, `timeout_per_read() == Some(30 s)`,
  `timeouts().global == Some(3600 s)`, `max_redirects() == 0`, the `PlatformVerifier` root setting as
  `agent_trusts_the_os_store` checks it (`:274-304`), and `https_only()` true under `HttpConfig::production()` (the
  body `PRODUCTION_BODIES` allows after T002, TA-7); the `Feed` agent unchanged (`timeout_per_read() == None`,
  `application/json`).
- `http::tests::streamed_pac_fetch_keeps_the_feed_bound_unix` (`#[cfg(unix)]`, SC-010's `_unix` case): loopback config
  with `total_timeout = 300 ms` and `.with_pac(PacSource { url: Some(server.url("/pac")), .. })` (the body `PAC_BODIES`
  allows) on a route `Stall(5 s)`, `limits.deadline = 10 s` → returns (any error) in < 2 s.
- `http::tests::streamed_body_cases` (plan row `plan.md:1308`, binding, as amended by TA-15): `Reply::Gzip { body:
  gzip(b) }` reaches `on_head` with `content_encoding == None` and `content_length == None` and the sink receives
  exactly `b` (pins PD-17; name the pin in a comment); `Reply::Garbage` with the exact head `HTTP/1.1 200
  OK\r\nContent-Length: <n>\r\nContent-Encoding: br\r\nConnection: close\r\n\r\n` + bytes → `on_head` sees
  `content_encoding == Some("br")`; a `Reply::Garbage` 206 head (`HTTP/1.1 206 Partial Content`, `Content-Range: bytes
  10-19/20`, `Content-Length: 10`) reaches `on_head` with `status == 206` and streams its 10 bytes; `on_head` → `Stop`
  → `HeadStopped`, sink never called; a sink returning `Err(StorageFull)` on its first call → `Sink { kind:
  StorageFull }` and exactly one sink call; **per-head limit (SI-27):** a request with header `Range: bytes=1000-` to a
  `Reply::Ok` route of `n` bytes (`n` > 1000, the route ignores the range) with `on_head` returning `HeadAction::Read {
  max_body: n }` → `Ok(StreamEnd { bytes: n })` and all `n` bytes passed; the same request with `Read { max_body: n -
  1000 }` → `Http(HttpError::TooLarge { max: n - 1000 })`, the bytes passed total ≤ `n - 1000` and equal the body's
  prefix, never a `StreamEnd` (TA-15); `Reply::HeadersThenStall` with `stall = 100 ms` → `Stalled`; `on_head` called
  once and before the first sink call (a shared counter); a token cancelled before the call → `Cancelled` with 0 server
  requests.
- Every Phase 5 `http::tests` case still passes unchanged (FR-011, FR-012).
Red: each new test fails on `not yet implemented` (from `get_streamed`) or on the placeholder `agent_config` arm's
values; quote one line per test. The `_unix` test is "not compiled here" (Rule 15).

**Implement:** plan §4.2.2 to §4.2.4 (`plan.md:334-442`) exactly: the `Download` arm of `agent_config` (the shared chain,
then `accept("application/octet-stream")`, `accept_encoding("identity")`, `timeout_per_read(Some(stall))`) with
`https_only` for `Feed | Download { .. }` under `HttpsOnly`; the private `ChainError` trait and its two impls; `walk<E:
ChainError>` (the single `new_with_config(agent_config(` site stays the only one, `injection.rs:1560-1595`);
`final_response<E>`; `run_chain` as `final_response::<HttpError>` + the unchanged 200-only rule + `read_body` (still 7
parameters, `http.rs:986-994`); the private `prepare` shared by `get` and `get_streamed`; `get_streamed` in the plan's
nine-step order (`plan.md:427-440`) with a 64 KiB heap buffer, the PAC bound `start +
config.total_timeout.min(limits.deadline)`, and the body limit taken from `on_head`'s `HeadAction::Read { max_body }`
(step 7; a chunk that would take the total past it is not passed on, step 8). No pub signature names a `ureq` or
`rustls` type (`injection.rs:1536-1552`); `get_streamed` keeps exactly seven parameters (Rule 2). `HttpError`,
`map_error`, `check_url`, `next_hop`, `read_body` and `get`'s behaviour stay as they are (plan §4.2.5).

**Done when:** the three new tests and every existing `http::tests` case green in L-OWN; 0 clippy warnings in slot `lib`;
`ureq_is_confined_to_http_rs` and `production_config_only_in_allowed_tests` green (`--test injection`, same slot).
**Lane:** L-OWN (slot `lib`); verified at the G2 gate by L-RS.

### T004 [P] — `verify::verify_file`, `Expected::from_request`, the KAT and the doctests (spec FR-030 to FR-039, FR-085 KAT part; SC-004, SC-015 doctests; plan §4.3, §7.1 rows SC-004, SC-015 and "(plan) verify_steps_stop_at_first_failure", §7.2 FR-034; PD-4, PD-19 step 1, SI-1, SI-28)

**Files:** `crates/core/src/verify.rs` (replace the placeholder bodies, remove TA-1's `verify_file` anchor, add `CHUNK`
and the doctests), `crates/core/src/verify_tests.rs` (append; T002's four tests stay as they are).
**Slot:** `lib`, `CARGO_BUILD_JOBS=10`. Filter: `--lib verify::`, plus `--doc verify`.

**Test first** (`verify_tests.rs`; KAT files read in place from `CARGO_MANIFEST_DIR/../../../../tools/feed-contract/fixtures/`,
never copied into the repo; tamper copies only inside a `fresh_temp_roots()` `TempDir`):
- `verify::tests::kat_verifies_and_one_byte_changes_fail` (SC-004 as amended by SI-1, `plan.md:1300`, binding): first
  `assert_eq!(hex_lower(&sha256_of(&archive)), "5b980e5b8ae7811a064966a1ec1380df016081a12bdeaf643486026007b62890")`;
  `Expected { name: "KatPlugin", version: "1.2.3", platform: PlatformKey::LinuxX86_64, asset:
  &AssetName::from_url(&AssetUrl::checked("https://assets.example.invalid/kat/KatPlugin-1.2.3-Linux-x64.tar.gz")?)?,
  format: AssetFormat::TarGz, size: 452, sha256: <that digest>, signature: <the .minisig text> }`, key
  `SigningKey::from_text(<kat.pub>)` → `Ok`, and every accessor of the returned artifact equals the input; the archive
  with one byte flipped → `E_VERIFY_SHA256`, and with the hash recomputed for the flipped file → `E_VERIFY_SIGNATURE`;
  the signature line (line 2) with character 50 replaced by another base64 character → `E_VERIFY_SIGNATURE`; the trusted
  comment edited to `…:1.2.4:…` → `E_VERIFY_SIGNATURE`. The `.sha256` file is not read (SI-1).
- `verify::tests::kat_secret_message_verifies` (SC-004): `fixtures/phase2/kat-secret/message.txt` (25 bytes, SHA-256
  computed with `sha256_of`) with its `.minisig` and `kat.pub`, binding `KatPlugin`, `1.2.3`, `linux-x86_64`,
  `message.txt` → `Ok`; `kat.key` is never opened.
- `verify::tests::invalid_trusted_comment_is_named` (SC-004 step 7, `plan.md:1300`): `TestSigner` signatures with the
  comment `timestamp:1` → `E_VERIFY_TRUSTED_COMMENT` with `code == "E_TC_FIELDS"`; with
  `krate:1:alpha:1.2.3:linux-x86_64:a.bin` → `code == "E_TC_NAME"`.
- `verify::tests::verify_steps_stop_at_first_failure` (plan row `plan.md:1337`, binding): a size mismatch reported
  before a malformed signature in the same `Expected` (code `E_VERIFY_SIZE`); a `TestSigner` signature checked with
  another signer's key → `E_VERIFY_KEY_ID` with `expected` / `found` the two 16-digit ids; a legacy `Ed` signature text
  → `E_VERIFY_SIGNATURE_TEXT`; a path that does not exist → `E_VERIFY_IO { kind: NotFound }`; **FR-038's order (review
  round 2):** a signature bound to another name **and** another version → `E_TC_BINDING { field: "name" }`; another
  version and another asset → `field: "version"`; another platform and another asset → `field: "platform"`; another
  asset only → `field: "asset"`; each row also asserts `signed` and `expected` of the reported field.
- **Doctests** on `VerifiedArtifact` (SC-015, plan §4.3 `plan.md:475-495` and `:525-530`, binding): the two
  `compile_fail` blocks (the complete eight-field literal with `asset` taken as a parameter, and `Default::default()`)
  and the `no_run` twin that must compile.
Red: each unit test fails on `not yet implemented` (T002's anchor body); quote one line per test. The `compile_fail`
doctests pass already (privacy holds from T002); record that the `no_run` twin compiles (`cargo test -p
krate-manager-core --doc --locked verify` output lines).

**Implement:** plan §4.3 `verify_file` steps 1-9 (`plan.md:532-549`) exactly, in that order, one sequential read of
`CHUNK = 64 * 1024` bytes into a heap buffer feeding `Sha256::update` and `StreamVerifier::update`, step 1 through
`fs::symlink_metadata` (not a regular file → `Io { kind: InvalidInput }`, PD-19) then `File::open` and the handle's
`metadata().len()`; `Expected::from_request` from `DownloadRequest`'s public accessors (`name()`, `version_text()`,
`key()`, `asset()` → `asset_name`, `format`, `size`, `sha256`, `signature`); remove the TA-1 anchor (the fields are now
read by steps 1-8). Visibility stays as T002 wrote it (SI-28: `verify_file`, `Expected`, its fields, `from_request`,
`from_text` all `pub(crate)`). The file is never read whole; the key is never read from disk at run time.

**Done when:** the five tests above, T002's four `verify::tests`, and the three doctests green in slot `lib`; 0 clippy
warnings. **Lane:** L-OWN (slot `lib`); verified at the G2 gate by L-RS.

### T005 [P] — `core::download`: staging, the transfer loop, verification hand-off, `discard` and the command functions (spec FR-001 to FR-006, FR-013 to FR-018, FR-020 to FR-023, FR-045 to FR-053, FR-060, FR-062, FR-064, FR-065; SC-002, SC-003, SC-005 to SC-009, SC-011 to SC-014, SC-019 request rows, SC-026 to SC-037; plan §3, §4.4, §7, §7.1 rows SC-002 to SC-037; PD-3, PD-5 to PD-12, PD-15, PD-17 to PD-20; SI-7, SI-9 to SI-13, SI-18 to SI-20, SI-23 to SI-25, SI-27, SI-29, SI-30)

**Files:** `crates/core/src/download.rs` (replace T002's placeholder bodies and `DownloadRequest`'s `_private` field;
remove TA-1's `discard` anchor; add the private items), `crates/core/src/download_tests.rs`.
**Target:** the shared `apps/krate-manager/target`, `CARGO_BUILD_JOBS=12` (TA-9). Filter: `--lib download::`.

**Test first** (`download_tests.rs`; the shared rules of plan §7 (`plan.md:1271-1291`) are binding:
`HttpConfig::loopback(SEC, SEC)`, every request through `with_fixture_url`, feeds from `test_support::test_feed`, **every
environment through `test_support::DownloadRig`** (`let rig = DownloadRig::new(ctx); obtain_and(rig.env(), &feed,
&request, &mut record, |a| a)`; a row varies the rig by assigning a public field before the call or by struct update on
one call, `DownloadEnv { io: &FsStagingIo, ..rig.env() }`; no test spells a full `DownloadEnv` literal), the test policy
`test_support::test_policy()` unless a row assigns `rig.policy = DownloadPolicy { …, ..test_policy() }`, assets from a
seeded xorshift with sizes that are not multiples of 64 KiB, signed by `TestSigner::sign_c3`, the context's
`signing_key` that signer's `key()`, errors asserted by `code()` and named fields, never message text). Write first the
helpers: `fn assert_progress_invariants(events: &[Progress], size: u64, ok: bool)` exactly as plan §7 states it
(`plan.md:1283-1291`), called on **every** recorded run of the rows below that record events (SI-24); `fn
rejection_rows() -> Vec<Row>` (shared by SC-003 and SC-031). Then, each with the assertion strategy of its plan §7.1 row
(`plan.md:1295-1338`, binding verbatim):
- SC-002 `good_artifact_verifies` · SC-003 `rejections_never_reach_next_stage` (row c on the rig's counting
  `FaultyStagingIo`: `written() == 0` and `calls_of(Op::Open) + calls_of(Op::Truncate) == 0`; row f both `E_VERIFY_KEY_ID`
  fields, distinct, each `^[0-9A-F]{16}$`; rows h-k assert `field`, `signed` and `expected`) · SC-005
  `progress_is_monotonic_and_ends_at_size` · SC-006 `interrupted_download_resumes`, `partial_from_a_previous_run_resumes`
  (PD-20: first `Bytes.on_disk > 400000`, `transferred == size - 400000`), `stall_resumes` (`policy.stall = 200 ms`;
  asserts the outcome, the first `Retrying.code == "E_DOWNLOAD_STALLED"`, the ranged second request and at most
  `policy.attempts` requests, **not** "exactly one `Retrying`", plan §9 `plan.md:1448`) · SC-007
  `ignored_range_falls_back_to_full` (SI-27: the `200` after the ranged request is written whole, the run ends `Ok`,
  `transferred == 400000 + size`), `truncate_failure_after_ignored_range` · SC-008 `range_edge_cases`,
  `classify_head_table` (no socket) · SC-009 `retries_are_bounded` (with SI-18's e1-e4: e2-e4 use `Reply::Gzip` and
  core's dev-dependency `flate2`, as `http_tests.rs:110-114`), `stall_and_deadline_are_named` · SC-009 / FR-016 / D5
  (SI-30) `non_retryable_transport_errors` (three rows: `E_DOWNLOAD_URL`, `E_DOWNLOAD_REDIRECT` over `/r1` … `/r6`,
  `E_DOWNLOAD_TLS` via a redirect to `https` on the same fixture server's port, the location built from
  `server.url("/x")` at run time, never a port literal; each 1 asset-route request, no `Retrying`, no `.part`, `next`
  not called, `Display` naming the feed URL) · SC-011 `staging_errors_are_named` (`rig.io =
  FaultyStagingIo::failing(op, n, kind)` per row), `crash_after_each_step_recovers` (`catch_unwind(AssertUnwindSafe(..))`
  with `FaultyStagingIo::panicking_after(k)` on a fresh rig per `k`) · SC-012 `readonly_staging_dir_unix`
  (`#[cfg(unix)]`; the `uid() == 0` refusal; mode restored before the `TempDir` drops) · SC-013
  `prune_skips_a_file_open_without_share_delete_windows` (`#[cfg(windows)]`, `OpenOptionsExt::share_mode(0x1)`) · SC-014
  `prune_keeps_only_live_artifacts`, `same_artifact_twice_is_busy` (`thread::scope`, the `ready` / `go` `mpsc` channels
  inside A's progress closure on `Started`; each scoped thread calls `rig.env()` on one shared `&rig`),
  `busy_registry_is_released_on_error` · SC-014 / FR-005 (SI-30, PD-12) `prune_spares_in_flight_artifact` (A held at its
  **first `Bytes`**, Y obtained under feed F2, `<hexX>.part` survives with length ≥ A's `on_disk`, A ends `Ok`; the
  control run removes `<hexX>/` once A is out of flight) · SC-019 `request_for_cases` (`v1-only.json` and `mixed.json`
  read in place with `include_bytes!` from `tools/feed-contract/fixtures/feeds/valid/`, plus `test_feed` rows for a
  non-native key, a `min_os` above `FakeHostFacts`' version, a native entry) · SC-026 `two_artifacts_reach_one_consumer`
  · SC-027 `channel_adapter_forwards_in_order` · SC-028 `command_flow_cases` (feed URL `server.url("/versions.json")` in
  the rig's `ctx`; `for_command(rig.env(), …)`, `run_command(rig.env(), …)`, `command_request(&rig.ctx, …)`; rows b, d,
  e serve `test_support::test_feed_document(…)`'s bytes with a `TestSigner` signature, row c serves
  `fixtures/feeds/valid/mixed.json` read in place, TA-14; **row f** (SI-30): `for_command` on a good entry with a stale
  `downloads/<other-hex>.part` → `E_DOWNLOAD_URL` (the feed's `assets.example.invalid` URL refused by the loopback
  policy), the feed request recorded, 0 asset-route requests, exactly one `Started`, no `Retrying`, the stale `.part`
  gone) · SC-029 `cached_verified_file_is_reverified` (b: `Restarted { CachedInvalid }`, Q-A) · SC-030
  `redirect_target_never_logged` (rows a-d, the FR-053 counts and fields through `test_support::install_capture()` /
  `take_mine()`, and the socket-less `scrub` row) · SC-031 `writes_nothing_outside_downloads`,
  `verified_file_has_no_quarantine_macos` (`#[cfg(target_os = "macos")]`, `Command::new("/usr/bin/xattr").arg("-l")`,
  the one body TA-7 allows), `verified_file_has_no_zone_identifier_windows` (`#[cfg(windows)]`,
  `File::open("<file>:Zone.Identifier")` → `NotFound`) · SC-031 / FR-050 (SI-23) `staged_symlinks_are_not_followed`
  (`#[cfg(unix)]`, `std::os::unix::fs::symlink`; four cases, the fourth with `rig.io =
  FaultyStagingIo::failing(Op::RemoveFile, 1, PermissionDenied)` → `E_STAGING_IO`, `step == "open"`,
  `rig.io.calls_of(Op::LenOf) == 0`) · SC-031 / FR-050 Windows (SI-29) `staged_links_are_not_followed_windows`
  (`#[cfg(windows)]`; junctions made with `Command::new("cmd").args(["/C", "mklink", "/J"])` on plain drive paths, the
  `\\?\` prefix stripped, `assert!(made.status.success())`, exactly as `detect_tests.rs:2578-2594`; three cases, the
  second with `rig.io = FaultyStagingIo::failing(Op::RemoveDir, 1, PermissionDenied)`; every junction left is removed
  with `fs::remove_dir` before the `TempDir` drops; this test runs on this box) · SC-032 `fixture_url_seam_cases` ·
  SC-033 `staged_paths_cases` (`#[cfg(windows)]` adds `C:x`, `C:\x`) · SC-034 `cancellation_cases` (a)-(e): (a)
  `rig.cancel.cancel()` before the call; (c) `DownloadEnv { cancel: &CancelToken::new(), ..rig.env() }`; (d) a scoped
  thread holding `&rig.cancel`; (e) calling `http::get_streamed` with a sink that cancels · SC-035 `discard_cases`
  (a)-(f), (f) per PD-18 · SC-036 `retrying_events_are_ordered_and_serialize` · SC-037 `policy_has_no_overall_budget` and `slow_moving_download_is_not_an_error`
  (the full struct literal equals `production()`, the one test that spells the production policy; SI-19 as revised:
  `Reply::Paced`, 10 chunks 150 ms apart, `stall = 1 s`, `attempt_deadline = 5 s` → `Ok`, verified, at most
  `policy.attempts` requests, `elapsed > policy.stall` and ≥ 1.35 s, < 3 s; a `Retrying` is printed, not asserted).
All test functions are `download::tests::<name>` (the names `required-tests.json` will list, T010). No test sleeps more
than 1 s; no global mutex; the busy / discard / in-flight tests synchronise only through their channels (FR-087).
Red: each test fails on `not yet implemented` (T002's placeholders) or, once a body exists, on its assertions; quote one
line per test. The `cfg(unix)` / `cfg(target_os = "macos")` tests are "not compiled here" (Rule 15).

**Implement:** plan §4.4 (`plan.md:553-874`) exactly: `DownloadRequest`'s fields of §4.4.1 (with `running`, PD-3, and the
`#[cfg(test)]` `fetch_url` with `with_fixture_url` and `fetch_target`, D13: each declaration directly preceded by
`#[cfg(test)]`, so T007's seam guard holds); `request_for` (§4.4.2); `staged_paths` and `StagedPaths` (§4.4.3);
`FsStagingIo` (§4.4.4 `plan.md:672-681`, PD-6, PD-19, SI-29: `create_dir_all` then `symlink_metadata` must be a
directory; `kind_of` and `len_of` through `symlink_metadata`, never following; the one `read_dir` inside
`read_dir_entries`; no mode, attribute or timestamp set, FR-004); the in-flight registry and `prune` under its lock
(§4.4.5 `plan.md:683-710`, PD-12, PD-19: kind checks, `remove_file` for `File` / `Other`, the `#[cfg(windows)]`
`remove_dir_all` retry after a failed `remove_file` of an `Other` entry, `lock().unwrap_or_else(PoisonError::into_inner)`,
the `InFlight` guard removes its key on drop); `classify_head` (§4.4.6, PD-9, PD-17's table); the `Meter` (§4.4.7,
PD-20); `obtain_and(env, …)` steps 1-8 (§4.4.8 `plan.md:768-830`) with step 4's `kind_of(<hex>)` probe before
`len_of(<hex>/<asset>)` (SI-29), step 7's `on_head` returning `HeadAction::Read { max_body: size }` for `Full` and
`Read { max_body: size - from }` for `Continue` (SI-27), PD-7's request budget, PD-8's "more than one response" rule
and the one `info!` / `warn!` wrapper (`obtain_body`, FR-053, target `LOG_TARGET`); `discard` (§4.4.9, PD-18);
`scrub` (PD-15) applied to every foreign `detail`; `DownloadPolicy::production()` (FR-018); the command functions
`command_request`, `run_command`, `for_command` (exactly `command_request(env.ctx, …)` then `run_command(env, …)`),
`channel_progress` (§4.4.10 `plan.md:848-874`). Every private helper stays within 7 parameters (Rule 2). Remove the
TA-1 anchor. No URL literal, no URL built from parts (FR-002), no whole-file read (FR-082), no `minisign` / `sha2` /
`minisign_verify` / `ureq` path (hashing goes through `verify`, transport through `http`), no plugin name (Rule 4d).

**Expected red until** (in-group; only these texts count): every transfer test panics `not yet implemented` inside
`http.rs` until **T003** lands `get_streamed`; every test that reaches verification panics `not yet implemented` inside
`verify.rs` until **T004** lands `verify_file` / `Expected::from_request`; every test whose route uses `Reply::Ranged`,
`Reply::Script` or `Reply::Paced` panics `T006` (TA-4's arm) inside `fixture_server.rs` until **T006** lands. Write
`classify_head_table`, `staged_paths_cases`, `fixture_url_seam_cases` and the socket-less `scrub` row first: they need
none of the three and are the early proof of this file.

**Done when:** every `download::tests` case green in L-OWN (after T003, T004, T006 landed; their state in the notes),
`staged_links_are_not_followed_windows` among them (it runs here); 0 clippy warnings in the shared target. **Lane:**
L-OWN (shared target); verified at the G2 gate by L-RS.

### T006 [P] — The fixture server's `Ranged`, `Script` and `Paced` replies (spec FR-084; SC-020; plan §4.8, §7.1 row SC-020)

**Files:** `crates/core/src/fixture_server.rs` (replace TA-4's `todo!` arm; the routing, the hit counter, the reason
lines), `crates/core/src/fixture_server_tests.rs`.
**Slot:** `lib`, `CARGO_BUILD_JOBS=10`. Filter: `--lib fixture_server::`.

**Test first** (`fixture_server_tests.rs`, raw-socket requests with the file's existing helpers `send`, `read_all`, `parse`):
- `fixture_server::tests::ranged_cases` (SC-020): no `Range` → `200`, `Content-Length: <len>`, the full body;
  `Range: bytes=10-` → `206`, `Content-Range: bytes 10-<len-1>/<len>`, `Content-Length: <len-10>`, the tail; the header
  name in lower case (`range: bytes=10-`) → the same 206; `bytes=<len>-` → `416` with `Content-Range: bytes */<len>`;
  `bytes=5-9` (not of the form `bytes=<n>-`) → `200` with the full body.
- `fixture_server::tests::script_advances_per_request` (SC-020): `Script([Status(503), Status(500), Ok { body }])` on
  one route → the first three requests get 503, 500, 200, the fourth 200 again (the last repeats); a second route's
  count is independent.
- `fixture_server::tests::paced_delivers_all_bytes_in_chunks` (SC-020): `Paced { body: 7 × 10 bytes, chunk: 10,
  interval: 50 ms }` → `200`, `Content-Length: 70`, all 70 bytes; per-chunk read timestamps show ≥ 6 gaps of ≥ 40 ms
  and the whole read takes < 2 s.
- Every existing `fixture_server::tests` case still passes unchanged.
Red: each new test fails on the `T006` `todo!` panic (or a dropped connection); quote one line per test.

**Implement:** plan §4.8 (`plan.md:966-985`) exactly: `Shared.hits: Mutex<BTreeMap<String, usize>>` (a field of `Shared`,
`fixture_server.rs:72`, not a `static`); `reply_for(path)` resolves a `Script` by the route's hit count under that lock,
so `answer` never sees a `Script`; `answer(&mut self, reply: &Reply, request: &RecordedRequest)` (`:288`; `Delayed`
passing `request` on); the `Ranged` and `Paced` arms (`Paced` answers every request, ranged or not, with the full `200`
body, which SC-037 relies on, `plan.md:1336`); `reason` (`:424`) gains `206 Partial Content`, `408 Request Timeout`,
`416 Range Not Satisfiable`, `429 Too Many Requests`. The TLS answer, the `CONNECT` rule and every existing reply are
unchanged. No fixed port, no `127.0.0.1:<port>` literal (FR-087).

**Done when:** the three new and every existing `fixture_server::tests` case green; 0 clippy warnings in slot `lib`;
`--test injection` green in the same slot (the static-lock rule over `fixture_server.rs`, `injection.rs:1846-1900`).
**Lane:** L-OWN (slot `lib`); verified at the G2 gate by L-RS.

### T007 [P] — `injection.rs`: the seam and minting-route, secret-key, real-host, confinement, URL / whole-read, arity and fixture-seam guards (spec FR-043, FR-082, FR-083, FR-086, FR-087; SC-001 guard, SC-015 guard, SC-017, SC-018 confinement, SC-032 guard; plan §4.10.1 new tests; SI-14, SI-21, SI-22, SI-26, SI-28)

**Files:** `crates/core/tests/injection.rs` (new consts `DOWNLOAD_RS`, `VERIFY_RS`, `VERIFY_TESTS_RS`, `TEST_SIGNER_RS`,
`ARITY_SUPPRESSIONS` and the 13 new tests; T002's table rows stay as they are).
**Slot:** `guard`, `CARGO_BUILD_JOBS=8`. Filter: `--test injection`.

**Test first:** the thirteen tests of plan §4.10.1 (`plan.md:1047-1055`), each a plain `#[test]`, with the "Checks"
column as the binding assertion and each self-test planting **every** listed violation into an in-memory copy of the
file set through the existing `plant` / `assert_reported` helpers (`injection.rs:2056`, `:2104`) and requiring each to
be reported:
- `verified_artifact_has_no_public_constructor` / `verified_artifact_scanner_reports_plants` (FR-043), **including the
  SI-28 minting routes**: `verify_file` declared once, as `pub ( crate ) fn verify_file`, in `verify.rs`, and `verify_file
  (` elsewhere in code only in `download.rs`, `verify_tests.rs`, `download_tests.rs`; `from_text` declared once, as `pub
  ( crate ) fn from_text`, in `verify.rs`, and `from_text (` elsewhere only in `test_signer.rs`, `test_support.rs`,
  `verify_tests.rs`, `download_tests.rs`; `struct Expected` declared `pub ( crate ) struct Expected` with no `pub` token
  inside its braces not followed by `( crate )`. Plants (all of `plan.md:1049`): a `pub` field, `#[derive(Default)]`,
  `impl From<PathBuf> for VerifiedArtifact` in `download.rs`, a struct literal in `download.rs`, a `relocated(` call in
  `test_support.rs`, `pub fn verify_file` in `verify.rs`, `pub fn from_text` in `verify.rs`, a `verify_file(` call and a
  `SigningKey::from_text(` call in `src-tauri/src/commands.rs`, a bare `pub name` field in `Expected`.
- `no_secret_key_in_app_tree` / `no_secret_key_scanner_reports_plants` (FR-083 with SI-22's rules (1)-(5),
  `plan.md:1050`): the walk covers every file under `apps/krate-manager/` except any `target/`, `node_modules/`,
  `dist/` directory, links not followed, read lossily; every plant is built at run time (`STANDARD.encode` of a
  `format!`-assembled header, the variable name by `format!("{}={}", NAME, value)`), so `injection.rs`'s own text holds
  none of them; the two must-not-report plants (`TAURI_SIGNING_PRIVATE_KEY: ${{ secrets.TAURI_SIGNING_PRIVATE_KEY }}`,
  a base64 public-key text) are asserted unreported.
- `phase7_tests_hold_no_real_host` / `phase7_host_scanner_reports_plants` (FR-087): over `download_tests.rs`,
  `verify_tests.rs`, `test_signer.rs`, `test_support.rs`; `SigningKey :: release` allowed only inside `verify_tests.rs`
  `fn release_key_is_the_committed_key_and_not_a_test_key`; a missing file is a finding.
- `verify_crates_are_confined` / `verify_confinement_scanner_reports_plants` (FR-082 via `confinement()`,
  `injection.rs:748`).
- `download_and_verify_hold_no_url_or_whole_read` / `download_verify_scanner_reports_plants` (FR-082).
- `too_many_arguments_is_never_suppressed` (Q-B, PD-11, SI-26; one test, plants inline as in
  `disallowed_methods_expectations_are_the_seven_d6_sites`, `injection.rs:2559-2622`; `plan.md:1054`, binding): a pure
  `fn arity_problems(rs, manifests, clippy_configs) -> Vec<String>` over (1) every file of `workspace_rs_files()`
  (`:1315`) plus the clippy-guard fixture's `src/`, reading every attribute with `suppressed_lints` (`:1160`) and
  reporting an `ARITY_SUPPRESSIONS` name (`["clippy::too_many_arguments", "clippy::complexity"]`) in `allow` or
  `expect`, outer or inner, inside `cfg_attr` or not; (2) `section_keys` (`:1289`) of `[workspace.lints.clippy]` in the
  root manifest and `[lints.clippy]` in every member manifest and the fixture manifest holding `too_many_arguments` or
  `complexity`; (3) a `too-many-arguments-threshold` key in any `clippy.toml` / `.clippy.toml` of
  `apps/krate-manager/`, `crates/*/`, `src-tauri/` or the fixture directory. The real tree gives zero problems. Plants,
  each required to be reported: `#[expect(clippy::too_many_arguments, reason = "x")]` before a `fn` in
  `crates/core/src/download.rs`'s copy; `#[cfg_attr(test, allow(clippy::too_many_arguments))]` in
  `src-tauri/src/commands.rs`'s copy; `#![expect(clippy :: complexity)]` in `context.rs`'s copy; `too_many_arguments =
  "allow"` under the root `[workspace.lints.clippy]`; `too-many-arguments-threshold = 9` in the copy of `clippy.toml`.
  Quiet case (not reported): the lint name only in a `//` comment and in a string literal. The plant strings live in
  literals, which the scanner blanks, so `injection.rs`'s own text is clean.
- `fixture_url_seam_is_test_only` / `fixture_url_seam_scanner_reports_plants` (SC-032).
Red: the self-tests fail until their scanners exist (write the scanner functions with empty bodies first so the file
compiles, then the tests, then the bodies); quote one line per test.

**Implement:** the scanners, reusing the file's lexer helpers (`strip_comments_and_literals` `:475`, `tokens` `:677`,
`fn_items` `:823`, `enclosing_fn` `:874`, `seq_at` `:727`, `impl_blocks` `:914`, `confinement` `:748`,
`lint_suppressions` `:1142`, `suppressed_lints` `:1160`, `section_keys` `:1289`, `workspace_rs_files` `:1315`,
`is_test_source` `:1365`); the `.key`-name, plaintext, decoded-base64 and `TAURI_SIGNING_PRIVATE_KEY` rules of SI-22 with
`base64::engine::general_purpose::STANDARD` (canonical decode only). No new dependency (Rule 8).

**Expected red until** (in-group; only these texts count): `download_and_verify_hold_no_url_or_whole_read` reports
"`read_dir` 0 time(s) in download.rs" until **T005** writes `FsStagingIo::read_dir_entries`;
`fixture_url_seam_is_test_only` reports `with_fixture_url` / `fetch_url` "not declared in download.rs" until **T005**.
Every other new test is green against the G1 tree (T002's anchors keep "`relocated (` only in `download.rs`" true and
name `verify_file` / `from_request` without a call; `verify_file (` has no caller until T004 / T005 add theirs, all in
the allowed files).

**Done when:** the 13 new tests and every existing `injection` test green in slot `guard` against the tree with all of
G2 landed (the two above after T005); 0 clippy warnings. **Lane:** L-OWN (slot `guard`); verified at the G2 gate by
L-RS.

### T008 [P] — `config_guard.rs`'s handler pin and signing-crate walk, `roster.rs`'s file lists (spec FR-061, FR-081, FR-088; SC-018 walk and roster, SC-019 handler pin; plan §4.10.2 bullets 3 and 7, §4.10.3; SI-2, SI-21)

**Files:** `crates/core/tests/config_guard.rs` (T002's expectation edits stay as they are), `crates/core/tests/roster.rs`.
**Slot:** `guard`, `CARGO_BUILD_JOBS=8`. Filters: `--test config_guard`, `--test roster`.

**Test first:**
- In `real_src_tauri_config_passes_the_guard`'s checks: the squeezed `lib.rs` contains
  `generate_handler![commands::app_version,commands::startup_error,commands::plugin_status,commands::check_now,commands::open_release_page,commands::download_plugin]`;
  in `planted_config_changes_fail_the_guard`: a new plant removing `commands::download_plugin` (and its comma) from the
  handler list → reported (plan §4.10.2 bullet 3, `plan.md:1066-1068`).
- `core_has_no_signing_crate` / `signing_crate_scan_reports_plants` (FR-081, plan §4.10.2 last bullet, `plan.md:1074-1085`,
  binding): roots per member from `MEMBERS` = `[dependencies]`, every `[target.'…'.dependencies]`, `[build-dependencies]`,
  never `[dev-dependencies]`; the walk over the committed `Cargo.lock` (`[[package]]` `name`, `version`, `source`,
  `dependencies`; `"<name>"` / `"<name> <version>"` with an optional `" (<source>)"`; a source-less package is a member
  expanded through **its manifest's** normal roots); a reached `minisign`, `rpassword` or `scrypt` is a finding naming
  the member and the path. Plants (in memory): `minisign = { workspace = true }` in core's `[dependencies]` (reported);
  an `"rpassword"` string added to the `log` package's lock `dependencies` (reported); a copy of core's manifest whose
  `[dev-dependencies]` is exactly `minisign = { workspace = true }` (must report **nothing**, SI-21). The real tree: zero
  findings.
- `roster.rs`: `roster_and_hosts_absent_from_sources`'s `must` list (`roster.rs:101-118`) and `identifier_files()` with
  its `must` list (`:245-262`, `:270-289`) gain `crates/core/src/download.rs` and `crates/core/src/verify.rs` (FR-088,
  plan §4.10.3 `plan.md:1089-1091`).
Red: the new plant is not reported and the two new tests fail until the scanner exists (empty scanner first so the file
compiles); quote one line each. The roster additions are green at once (both files exist from T002); record that.

**Implement:** the handler check beside the `build.rs` check (`config_guard.rs:640-653`); the lock walker as a pure
function over (manifest texts, lock text) so the plants run in memory; nothing else in either file changes (the Q-B
ruling changes nothing here, `plan.md:1059-1061`).

**Done when:** both binaries' tests green in slot `guard` (`config_guard`, `roster`); 0 clippy warnings. **Lane:**
L-OWN (slot `guard`); verified at the G2 gate by L-RS.

### T009 [P] — Evidence scripts: scope check, L-WSL, L-CLOSE (spec SC-001, SC-018 record, SC-024, SC-025, FR-063; plan §7.4, §8; TA-10, TA-11)

**Files (new, LF):** `specs/krate-manager-phase7/evidence/scope-check.js`, `specs/krate-manager-phase7/evidence/wsl-lane.js`,
`specs/krate-manager-phase7/evidence/close-lane.js`. No cargo command; Node only, no dependencies.

**Test first:** each script's `--self-test` (pure checks over synthetic inputs and the real workflow text), written
before its main path and run red (quote the first failing check):
- `scope-check.js --self-test`: allowed paths (`apps/krate-manager/**` minus Rule 11's exclusions,
  `specs/krate-manager-phase7/**`, `specs/INDEX.md`, `tools/feed-contract/tests/manager-ci-shape.test.js`) accepted;
  `apps/krate-manager/src/main.ts` (tracked change **and** untracked new file, FR-063), `apps/krate-manager/package.json`,
  `src-tauri/Cargo.toml`, `src-tauri/tauri.conf.json`, `apps/krate-manager/clippy.toml`, `.github/workflows/manager-ci.yml`,
  `plugins/x`, `tools/feed-contract/versions.v2.schema.json` refused; a roadmap hunk only on the Phase 7 status line
  accepted, any other roadmap hunk refused; in `manager-ci-shape.test.js` a hunk outside `EXPECTED_REQUIRED` refused.
- `wsl-lane.js --self-test`: `toWslPath` and the verdict over sample cargo output (3/3 TA-10 names `ok` → GREEN; one
  missing → still GREEN with the info line `2/3`; any `FAILED` / `warning:` → RED).
- `close-lane.js --self-test`: `extractStep` over the real `manager-ci.yml` for every step it runs; the SC-024 delta
  parser over a sample pair of `finished in` lines; the `cargo tree` verdict (A1h below) over sample trees; the SC-001
  suppression scan (A1j below) over a sample `diff -U0` (an added `#[expect(clippy::x, reason = "y")]` line → RED; an
  added `// see allow(` comment line inside a non-attribute → GREEN; an added `// @ts-ignore` → RED).

**Implement:** clone the three Phase 6 scripts (`specs/krate-manager-phase6/evidence/{scope-check,wsl-lane,close-lane}.js`,
read whole first) and change only what this phase needs:
- **scope-check.js:** `BASE = '2f3c93d7'`; the allowed set of spec FR-090 / FR-091 and Rule 11 (tracked diff against the
  base plus `git ls-files --others --exclude-standard`); one roadmap window, the Phase 7 status line under `### Phase 7:
  Download and Verify` (roadmap line 599; Phase 6's form at line 559); the shape-test hunk must lie inside
  `EXPECTED_REQUIRED` (`manager-ci-shape.test.js:337` to its closing brace).
- **wsl-lane.js:** the log default `F:/tmp/gate/km7-wsl/`; the TA-10 names; the same `CARGO_TARGET_DIR=$HOME/km-target`;
  a 40 min ceiling (the first run builds the new crates cold).
- **close-lane.js:** Phase 6's rows minus A1g / A2e / A11 (the opener tree, the opener npm check, Phase 6's SC-028
  roadmap needles), with: `BASE = '2f3c93d7'`; `REQUIRED_NOTICE = '::notice title=required tests ran (Windows)::233
  cargo + 29 vitest'` (plan §4.11 and §7.3 totals, `plan.md:1364`); A1h `cargo tree -p krate-manager-core -e normal
  --locked --prefix none --format "{p}" > evidence/core-tree-phase7.txt` with `minisign-verify v0.3.0` and `sha2
  v0.11.1` present and no line starting `minisign `, `rpassword ` or `scrypt ` (SC-018 record, plan §4.1 step 3); a new
  row A1i, SC-024: the sum of the `finished in` seconds of the core lib unit-test binary and the `injection` and
  `config_guard` binaries in this run's `cargo-test.txt` minus the same sum in
  `specs/krate-manager-phase6/evidence/cargo-test.txt`, GREEN when the delta is < 20 s (plan §7.1 SC-024
  `plan.md:1321`; the remedy of plan §8 `plan.md:1424-1428` is a main-loop decision, never automatic); a new row A1j,
  SC-001 (Q-B): `git diff -U0 2f3c93d7 -- apps/krate-manager > evidence/diff-u0.txt` searched for added lines holding
  `allow(` / `expect(` inside an attribute, `@ts-ignore` or `eslint-disable` (expected none, plan §7.4 step 5
  `plan.md:1401-1403`); a new row A1k, FR-063: `git diff --name-only 2f3c93d7 -- apps/krate-manager/src` and `git
  ls-files --others --exclude-standard -- apps/krate-manager/src` both empty; A0c reads `evidence/base-binary-size.txt`
  (T001, TA-11); B7 records the release binary against it; A10 calls this phase's `wsl-lane.js`; the raw log root
  `F:/tmp/gate/krate-manager-phase7-close/`; C3 `node tools/clean-test-output.js --apply` last. Every child under a
  wall-clock ceiling; independent rows concurrent (A1 and A2 as chains, the rest beside them), as in Phase 6.

**Done when:** `node --check` and `--self-test` green for all three (`SELF-TEST GREEN (<n> cases)`); `node
specs/krate-manager-phase7/evidence/scope-check.js` exits 0 against the current tree (or reports only paths a G2 task is
still writing, recorded). Not run here: `wsl-lane.js` and `close-lane.js` main paths (main loop). **Lane:** L-NODE
(`node --check` only), the three self-tests.

---

## G3 — Integration (sequential: T010, then T011)

### T010 — `required-tests.json` and the shape test's `EXPECTED_REQUIRED`: the per-OS CI additions (spec FR-086, FR-091; SC-021 to SC-023 lists; plan §4.11, §7.3)

No workflow file changes (FR-090): `manager-ci.yml`'s `required tests ran` step (`.github/workflows/manager-ci.yml:153-178`)
already asserts, on each leg, that every name of `cargo.all` plus that leg's list passed exactly once (the regex at
`:166`), so these lists **are** the per-OS CI additions for this phase's SCs.

**Files:** `apps/krate-manager/tests/required-tests.json`, `tools/feed-contract/tests/manager-ci-shape.test.js` (both
CRLF, Rule 12; the shape test only inside `EXPECTED_REQUIRED`, `:337` to its closing brace).

**Test first:** append plan §4.11's names (`plan.md:1095-1165`) to `EXPECTED_REQUIRED` only (`cargo.all` +58 names in the
plan's order, `plan.md:1098-1155`; `windows` +3, `macos` +4, `linux` +3); run L-NODE: `checkRequired` (`:739-748`,
`util.isDeepStrictEqual`, order matters) reports that `required-tests.json` differs (quote it).

**Implement:**
1. `required-tests.json`: exactly the same additions (resulting lengths `all` 223, `windows` 10, `macos` 28, `linux` 25,
   `vitest` 29 unchanged; today 165 / 7 / 24 / 22 / 29, counted with `node` this session; check with `node -e` and
   record). No name of a test that does not exist: verify each `cargo.all` and `cargo.windows` name against `cargo test
   --workspace --locked -- --list 2>&1 | tee F:/tmp/gate/km7-T010/list.txt` (shared target; Rule 7) — every name
   appears exactly once across the binaries (the CI regex wants exactly one `ok` line); the `macos` / `linux` names
   against `grep -n "fn <name>"` in their files with the right `cfg` (`#[cfg(unix)]` for the `_unix` names and
   `staged_symlinks_are_not_followed`, `#[cfg(target_os = "macos")]` for `verified_file_has_no_quarantine_macos`).
2. Run the `required tests ran` step locally: `cargo test --workspace --locked 2>&1 | tee F:/tmp/gate/km7-T010/cargo-test.txt`,
   `npm test -- --reporter=default --reporter=json --outputFile.json=F:/tmp/gate/km7-T010/vitest.json`, then the step's
   `run:` text through Git Bash with `RUNNER_TEMP=F:/tmp/gate/km7-T010` and `RUNNER_OS=Windows` (the step's heredoc
   lives in the workflow; run it by extracting the step text into a file with a throwaway Node script, Rule 13) →
   exactly `::notice title=required tests ran (Windows)::233 cargo + 29 vitest` and no `::error`
   (`staged_links_are_not_followed_windows` among the 233: Windows junction evidence is local as well as CI).

**Done when:** L-NODE `# fail 0`, L-TAP `TAP plan=1..59 top-level=59`, L-LINT green; the notice recorded. **Lane:**
L-NODE, L-TAP, L-LINT.

### T011 — INDEX, lints, scope, cleanup, the PENDING-CI list and the close hand-over (spec FR-090, FR-091, SC-001 local part, SC-025; plan §7.3, §7.4, §8)

**Files:** `specs/INDEX.md` (regenerated only). Nothing else is edited; this task runs and records.

**Test first:** `node tools/gen-specs-index.js --check` (quote its exit code and, if red, the first differing line).

**Implement:**
1. `node tools/gen-specs-index.js`, then `--check` → exit 0 (FR-091).
2. Repo lints: L-LINT (`node tools/lint-serial-test-runs.js`), L-VLINT. `node tools/check-portability.js` and `node
   tools/lint-plugin-editor-baseline.js` are **not** run: no C++ file and no plugin editor changed (Rule 11; `git diff
   --name-only 2f3c93d7` plus `git status --porcelain` show nothing under `plugins/`, `dsp/`, `cmake/`, `extern/`,
   `.github/`); record that output.
3. L-SCOPE (T009's script) → exit 0.
4. The TA-1 / Rule 9 grep: no `todo!(` or `unimplemented!(` in `crates/` or `src-tauri/src` outside `*_tests.rs` and
   `tests/`; no `_anchor` / `_moved` / `_private` left in `download.rs` or `verify.rs` (`grep -n`). The Rule 2 grep: no
   `#[expect(` or `#[allow(` line added since `2f3c93d7` in `apps/krate-manager` (`git diff -U0 2f3c93d7 --
   apps/krate-manager | grep -n '^+.*#!\?\[\(expect\|allow\)('` empty), and `too_many_arguments_is_never_suppressed`
   green in the last `cargo test` log.
5. Check the PENDING-CI list below against `required-tests.json` (every `cargo.macos` / `cargo.linux` name appears in a
   row) and record "PENDING-CI: N item(s)" with N = the row count.
6. Scratch cleanup: delete `F:/tmp/km7-target/` (any slot left) and the `F:/tmp/gate/km7-*` logs once their results are
   in the notes; `node tools/clean-test-output.js --apply` (repo rule) runs inside L-CLOSE (row C3).

**Done when:** L-INDEX, L-LINT, L-VLINT, L-SCOPE green; step 4's greps empty; the notes end with "deferred to the main
loop at close: run `node specs/krate-manager-phase7/evidence/close-lane.js`" (the release `tauri build`, L-WSL and the
full plan §7.4 procedure; Phase 6's took about two minutes, this one adds the cold WSL compile of the new crates), and
the main loop then writes the roadmap's Phase 7 status line `**Status: ✅ COMPLETE (PENDING-CI: N item(s)) (<date>)** —
see specs/krate-manager-phase7/compliance.md` under `### Phase 7: Download and Verify` (roadmap line 599, the form of
line 559). **Lane:** L-INDEX, L-LINT, L-VLINT, L-SCOPE.

---

## PENDING-CI (macOS and Linux evidence; only the user can trigger it)

Every row closes only on a pushed commit's `manager-ci.yml` run, under the spec's three-part rule (`spec.md:726-733`):
the committed `required-tests.json` lists the name, the leg's `required tests ran` step passed with no `::error title=
required tests ran (<os>)::…` line, and its notice reads `len(cargo.all) + len(cargo.<os>)` cargo and `len(vitest)`
vitest: **`::notice title=required tests ran (macOS)::251 cargo + 29 vitest`** on `check (macos-latest)`,
**`::notice title=required tests ran (Linux)::248 cargo + 29 vitest`** on `check (ubuntu-24.04)` and
**`::notice title=required tests ran (Windows)::233 cargo + 29 vitest`** on `check (windows-latest)` (plan §4.11 and §7.3
totals, `plan.md:1364`). A red there is a Phase 7 defect fixed before close (the `cfg(unix)` / macOS code is compiled
for the first time on those legs).

| # | SC | Job | Assertion that closes it |
|---|---|---|---|
| 1 | SC-001 (macOS) | `check (macos-latest)` | steps `cargo fmt`, `clippy`, `clippy dev-feed`, `clippy-guard` (`rc≠0`, `disallowed=3`, `other-errors=0`), `cargo test`, `typecheck`, `lint`, `vitest`, `tauri build`, `build warnings` (`warnings=0`) green; `too_many_arguments_is_never_suppressed ... ok` |
| 2 | SC-001 (Linux) | `check (ubuntu-24.04)` | the same steps green |
| 3 | SC-001, SC-023 (Windows CI) | `check (windows-latest)` | the same steps green and `required tests ran` notice `233 cargo + 29 vitest`, including `prune_skips_a_file_open_without_share_delete_windows`, `verified_file_has_no_zone_identifier_windows` (SC-013, SC-031 Windows) and `staged_links_are_not_followed_windows` (SI-29; also run locally, T005 / T010) |
| 4 | SC-021: SC-002 to SC-020, SC-026 to SC-037 on APFS | `check (macos-latest)` | `required tests ran` notice `251 cargo + 29 vitest`, no `::error`; every path check runs under `/var/folders/…` (the `/var` → `/private/var` link) |
| 5 | SC-022: the same SCs on ext4 | `check (ubuntu-24.04)` | `required tests ran` notice `248 cargo + 29 vitest`, no `::error` |
| 6 | SC-012 | both legs | `test download::tests::readonly_staging_dir_unix ... ok` once (`macos`, `linux` lists) |
| 7 | SC-010 `_unix` | both legs | `test http::tests::streamed_pac_fetch_keeps_the_feed_bound_unix ... ok` once |
| 8 | SC-031 (macOS) | `check (macos-latest)` | `test download::tests::verified_file_has_no_quarantine_macos ... ok` once (no `com.apple.quarantine` line from `/usr/bin/xattr -l`) |
| 9 | SC-031 / FR-050 (SI-23, PD-19) | both legs | `test download::tests::staged_symlinks_are_not_followed ... ok` once: `outside/` byte-identical, both links gone, the artifact a regular file under the canonical `downloads/`, the linked `downloads/` refused at `create_dir`, the unremovable `<hex>` link refused at step 4 with `calls_of(LenOf) == 0` |
| 10 | SC-018 (tree) | all three legs | step `core dependency tree`: `::notice title=core dependency tree (<os>)::<n> crates, none excluded`, no `::error` (`minisign-verify`, `sha2` counted; `minisign` absent from `-e normal`) |
| 11 | SC-018 (signing-crate walk, Linux / macOS lock graph) | both legs | `test core_has_no_signing_crate ... ok` and `test signing_crate_scan_reports_plants ... ok` under rows 4 / 5 (the `cfg(not(windows))` `rquickjs` graph and the macOS `core-foundation` / `plist` roots walked for real) |
| 12 | SC-019 (the six-command wiring compiled on Unix) | both legs | steps `clippy` and `clippy dev-feed` green (the first Unix compile of `download_plugin`, `DownloadEnv`, `Channel<Progress>`, `SigningKey::release()` at setup) |

L-WSL (TA-10) gives an early local signal for rows 2, 5, 6, 7, 9 and 11's core half before the push; it closes none of
them.

---

## Defects found (in the plan, while writing this list)

- **D-1 (TA-5).** Plan §4.1 step 2 (`plan.md:313-314`) re-locks the clippy-guard fixture with `cargo update --workspace
  --manifest-path tests/fixtures/clippy-guard/Cargo.toml`; the workflow's ruled command, which fixed the same L-GUARD
  `--locked` refusal in Phases 5 and 6 (commit `2f3c93d7`), is `cargo metadata --manifest-path
  tests/fixtures/clippy-guard/Cargo.toml --format-version 1 > /dev/null`. T002 uses it and checks the pairs.
- **D-2 (TA-1, TA-12).** The plan places `hex_lower`, `relocated`, `sha256_of`, and since SI-28 also `verify_file`,
  `Expected` (with its fields and `from_request`) and `SigningKey::from_text`, as `pub(crate)` items whose only readers
  are G2 code, and `SigningKey.public` is read only by `verify_file`; written as foundation placeholders they warn
  `dead_code`, and `channel_progress`'s `impl FnMut` return cannot be a bare `todo!()`. Neither is a plan error for a
  serial build; both break a zero-warning G1 gate, so TA-1's anchors and TA-12's closure exist.
- **D-3 (TA-13).** Spec FR-033 (`spec.md:469-470`) calls `key.public()`; plan §4.3 (`plan.md:457`, `:538`) has a private
  field and no accessor. T001 aligns the spec.
- **D-4 (TA-2, TA-3).** Plan §2 (`plan.md:208`, `:214`) lists `test_signer.rs` and the `test_support.rs` additions as
  separate pieces with no owner order; both are compile dependencies of two G2 modules' tests in the one core test
  binary, so T002 writes them whole and proves them with tests that are not in `required-tests.json`
  (`verify::tests::test_signer_round_trip`, `test_support::tests::phase7_helpers_cases`, and
  `verify::tests::digest_helpers_cases` for TA-1's helpers). The workflow's required-test totals (233 / 251 / 248) are
  unchanged by them.
- **D-5 (stray text).** Plan §4.11 (`plan.md:1165-1166`) repeats "The" across a line break ("(`#[cfg(unix)]`, PD-19).
  The / The `windows`, …"); no content is affected.
- **D-6 (TA-15).** Plan §4.2.4 step 8 (`plan.md:438-439`: an over-limit chunk "is not passed on") and the §7.1
  `streamed_body_cases` row (`plan.md:1308`: "exactly `n - 1000` bytes passed to `sink`") cannot both hold when the body
  arrives in one 64 KiB read. T003 follows step 8 and asserts "at most `n - 1000`, a prefix, `TooLarge`".
- **D-7 (line endings).** The revision 1 list recorded `crates/core/src/test_support_tests.rs` as LF (0 CR); this
  session's count is 269 CR lines of 269 (Rule 12). T002 treats it as CRLF.
