# Tasks: Krate Manager Phase 11 — Self-Update and the Manager's Release Pipeline

**Spec:** `specs/krate-manager-phase11/spec.md` (DRAFT revision 3: D1-D17, FR-001 to FR-097, SC-001 to SC-026;
Clarifications of 2026-11-01, `spec.md:1120-1137`, and the plan-precondition rulings, `spec.md:1138-1145`) · **Plan:**
`specs/krate-manager-phase11/plan.md` (DRAFT revision 4: PD-1 to PD-38 in §0.1, SI-1 to SI-40 in §0.2, error model §3,
module design §4, dependencies §5, safety §6, test plan §7, lanes §8, risks §9 (R-1 to R-22), the precondition table
§10 (P-1 to P-6 decided, R-17 open). Revisions 3 and 4 moved most plan line numbers: this file names plan rows by
section and id, and a `plan.md:<n>` citation below that predates revision 4 is to be re-found by its section and id)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 11 (now lines 819-889: Phase 10's close added a status line, so the
spec's and plan's "818-888" read one line low); Repository Layout lines 211, 232, 234-243; Phase 12 user-performed checks
lines 904-916.
**Status:** DRAFT task list, revision 3, 2026-11-01: revision 2 rebuilt on plan revision 4 (the second plan-challenge
round: PD-34 to PD-38, SI-34 to SI-40, R-20 to R-22) and on the follow-ups that round left for this file (plan "Review
notes", last bullet): cargo required names +2 (TA-1: 355 `all`), harness line count 27 (T013, T014, T016, T017, T018),
the rsign fixture and its generator before the harness's first build (T014, TA-25), the new shape-test rules (T013,
T015), `E_PREFLIGHT_REPO` (T011), the `.gitattributes` line (T001, TA-2); plus TA-25 to TA-28 and defects D-20 to D-23
found while writing it. Re-read this session at `30878b36` (`git rev-parse --short HEAD`; `git status --porcelain` shows
only `?? specs/krate-manager-phase11/`; `spec.md` unchanged since revision 2, so its line numbers below stand).
Revision 2 (2026-11-01) was revision 1 plus the user's six plan-precondition rulings: TA-1,
TA-14 rewritten, TA-22 to TA-24 new, T001, T002, T003, T006, T007, T013, T014, T016, T017, T018, the lanes, the
Preconditions and PENDING-CI updated; `HEAD` now `30878b36`, which adds only Phase 10 docs over `4f04dc03`
(`git diff --name-only 4f04dc03 30878b36` → `specs/Krate-manager-roadmap.md`, `specs/krate-manager-phase10/compliance.md`,
which is why every diff base below is `30878b36`, D-23). Revision 1
was written against `feat/krate-manager` at `4f04dc03` (`git rev-parse
--short HEAD`; `git status` shows only `?? specs/krate-manager-phase11/`). The plan was verified at `f6022bd5`; between
the two, `git diff --stat f6022bd5 HEAD` changes only `.gitattributes` (+5), `.github/workflows/manager-ci.yml` (+53, the
Phase 10 `inno-locked-file` M-1 steps, after line 877; the `check` job's lines the plan cites are unchanged, re-read
this session: `clippy-guard` :118-136, `cargo test` :138-154, `test ci-temp-roots` :156-166, `dev-feed absent` :345-355,
`roster absent` :357-379), `apps/krate-manager/tests/required-tests.json` (four names swapped), `manager-ci-shape.test.js`
(`EXPECTED_REQUIRED` now at `:605`), two core test files, helper files and the roadmap. Every file:line below was read
this session at `4f04dc03`.
**Spec state:** none of plan §0.2's edits is in `spec.md` yet (`grep -c "SI-" spec.md` → 0; `spec.md:861` still says
"`harness: 20 ok`", `spec.md:873` "eight keys", `spec.md:183-185` D1 still names `zip`). Plan line 18 asks for them before
this file. Following the Phase 8 / 9 / 10 precedent (`specs/krate-manager-phase10/tasks.md:12-18`), every task below
states its assertions from the plan's §0.1 / §0.2 / §4 / §7 rows, which are **binding as written**, and T002 writes the
edits (SI-1 to SI-40) into `spec.md` in G2 beside the code (ruling P-6; plan header: revision 4's SI-34 to SI-40 "land
in the same edit"). Re-checked this session: `grep -c "SI-" spec.md` → 3, all three inside the plan-precondition
clarification bullets (`spec.md:1141`, `:1142`, `:1145`), so none of §0.2 is in the spec yet. Revision 2's three open questions are ruled
(`spec.md:1138-1145`; plan §10 P-1 to P-6) and built as ruled: `socks` on (PD-30), PD-23 as written, the `.deb` banner
action (PD-24, PD-31), `clippy updater-e2e` in CI (PD-32), D9 without a stop (PD-33), no `zip`. Plan §10 R-17 (WebKitGTK
in WSL) is the user's; this file is written to its recommendation (a).
**Lanes in this phase:** the **app lane** and the **pipeline lane** (plan header "Lanes"), plus three **main-loop lanes**
(L-WSL, L-HARNESS's cold first build if it overruns, including its WSL build under D9's fallback, L-CLOSE). No file under `plugins/`, `dsp/`, `cmake/` or `extern/`
changes (spec FR-092, `spec.md:786-788`), so there is **no C++ lane**: no `run-gate.js`, no `header-consumers.js`, no
`check-portability.js`, no clang-tidy, no pluginval, no `lint-plugin-editor-baseline.js` (no plugin editor changes).

---

## How to read this file

- Groups run in order. A group marked **[P]** runs its tasks at the same time in one working tree. A task is
  **[P]-safe** when no other task in its group touches any of its files and none of its files is a **shared file**: any
  `Cargo.toml`, any `Cargo.lock` (the workspace's, `tests/fixtures/clippy-guard/Cargo.lock`,
  `tests/fixtures/updater-refusals/Cargo.lock`), a crate root (`crates/core/src/lib.rs`, `src-tauri/src/lib.rs`),
  `src-tauri/src/commands.rs`, `src-tauri/build.rs`, any `tauri*.conf.json`, the capability file, `package.json`,
  `.gitignore`, `.gitattributes`, an existing workflow, `tests/required-tests.json`, `manager-ci-shape.test.js`,
  `hygiene.test.js`.
- **Every shared file G2 needs is edited by one task, T001, which is G1 on its own (sequential).** It writes every
  manifest and lockfile line of the phase (both new dependencies, the new feature, the workspace `exclude`, the harness
  manifest and its lock), every config file, **every `pub mod` / `mod` line** (with a compiling placeholder file per new
  module), the whole public surface of `core::self_update` and of `src-tauri/src/self_update.rs` (placeholder bodies,
  `todo!("T0nn")` naming the owner), the small real pieces other G2 tasks compile against (the in-flight registry, the
  four test seams, the two `SelfUpdating` variants), the existing guard pins its own edits trip, and the required-test
  lists. The fifteen G2 tasks then touch only their own files: a G2 file is either new in this phase (created by T001 as
  a placeholder for exactly one G2 task, or created by that task) or an existing non-shared file that exactly one G2
  task edits. The file-ownership table is in "Dependency overview".
- Each task section is the executor's authoritative instruction: files, the failing test to write first (file, test
  name, assertions), the implementation intent, the lane. Where a block is too long to copy without drift (a type list,
  a scenario table, a job table), the section names the `plan.md` section or §7.1 row that holds it; that text is then
  **binding, verbatim**, as amended by plan §0.1 / §0.2 and this file's TA rows. Where this file and the plan differ,
  this file wins and the TA row says why.
- Canonical order in every task: **failing test → implement → zero warnings → tests pass.** "Red first" means the test
  was run and failed for the stated reason before the implementation existed; the notes quote the failing line. The red
  run normally uses T001's placeholder, so the test compiles and fails on the `todo!()` panic ("not yet implemented:
  T0nn") or on its assertions. A test that cannot run on this box (`cfg(unix)`) has no local red: it is recorded "not
  compiled here", read twice against its SC row, and first runs in the gate's WSL step.
- **In-group expected reds (G2 only)**, each raised from the named file and gone by the G2 gate:
  (a) T003's `self_update::tests::proxy_choice_matches_core` and `no_context_defaults` → `todo!("T004")` in
  `crates/core/src/http.rs` (`route_choice`) and `crates/core/src/context.rs` (`release_for_restart`);
  (b) T013's real-tree rows report `src-tauri/src/self_update.rs` (the timeout pins, the `NativeUpdateInstaller` homes,
  the PD-36 `UpdateInputs::production` body pin, the PD-37 `current_binary(` pin) until T006 lands, and
  `tests/fixtures/updater-refusals/src/` (the "six harness files" completeness row) until T014 lands;
  (c) T014's scenario lines read `not ok … not yet implemented: T003` / `T006` until those land.
  Those texts are the only failure texts that count as expected; **any red at the G2 gate is a real red**. A compile
  error is never an expected red (Rule 9).
- **Main-loop lanes.** Steps longer than ~5 minutes run only in the main loop, from scripts T016 writes under
  `specs/krate-manager-phase11/evidence/`: `close-lane.js` (L-CLOSE; Phase 10's took 225.9 s,
  `specs/krate-manager-phase10/evidence/timings.md:3`) and `wsl-lane.js` (L-WSL; Phase 10's row A10 took 109.4 s,
  `timings.md:39`). T018 returns with notes "deferred to the main loop at close: run `node
  specs/krate-manager-phase11/evidence/close-lane.js`".
- Never commit, never push, never trigger or dispatch CI, never run `release-manager.yml`. Those are the user's steps.

### Rules every task obeys (a task that breaks one is defective)

1. **Never touch the real system (spec FR-090, `spec.md:761-766`; plan §7.4 last paragraph).** No test, gate or agent
   on this box runs `npm run bundle`, `node scripts/bundle.js` (with or without `--release`), `tauri build`, `tauri
   signer` with any key, `pkgbuild`, `installer`, `dpkg`, `apt-get install`, `sudo`, an NSIS setup, the e2e update server
   outside its own node test, or edits a hosts file; none installs, launches or replaces the manager, reads the signing
   secrets, or writes under `%LOCALAPPDATA%`, `%APPDATA%`, `C:\Program Files`, `C:\ProgramData`, `/Applications`, a real
   VST3 / preset folder or the real state dir. Core tests build roots from `test_support::fresh_temp_roots()` /
   `temp_roots(base)` (`crates/core/src/test_support.rs:206-216`) and use `FakeElevation` (`:417-486`), never
   `NativeElevation` or `OsHostIo`. The harness builds its roots with `test_support::temp_roots`, binds loopback only,
   redirects every per-user env var per child, and never names the real installer or a restart (plan §4.7). The only
   network peers are the loopback `FixtureServer` (core, harness) and the loopback server of
   `scripts/e2e-update-server.test.js`. No test calls `github.com`, `api.github.com` or `krateaudio.com` (URLs naming
   them are **data** in URL-policy tables, never requested). No private key is written to any file: core and harness keys
   live in memory (`minisign::KeyPair::generate_unencrypted_keypair`), node keys come from `tools/minisign.js`
   `generateKeyPair` in memory or under `os.tmpdir()`. The one network access a task makes is T001's crate download from
   crates.io (`tauri-plugin-updater` 2.14.1 is not in the local registry: `ls -d ~/.cargo/registry/src/*/tauri-plugin-updater-*`
   this session → "No such file or directory", while `tauri-2.12.5` is there). **The one local signer run** is T014's
   `make-rsign-fixture.js` (plan §4.7 `rsign-format`, §7.4 step 3): `rsign` (`/c/Users/scab/.cargo/bin/rsign.exe`,
   `rsign --version` → `rsign2 0.6.7` this session) generates a throwaway key pair inside an `fs.mkdtempSync(os.tmpdir())`
   dir and signs one fixture file; the dir, secret key included, is deleted in a `finally`; only the public key and the
   signature are copied into the repository (the KAT generator's pattern, `tools/feed-contract/fixtures/kat/README.md`
   "2-5. Throwaway key, signature, public key"). No other task runs `rsign`, and nothing runs `rsign` with any other key.
2. **No lint suppression added.** `[workspace.lints.clippy] allow_attributes = "deny"`, `allow_attributes_without_reason
   = "deny"` (`apps/krate-manager/Cargo.toml:52-54`): never `#[allow]`. The one new `#[expect(…)]` is the harness
   `build.rs`'s `clippy::disallowed_methods` with `reason = "a build script reads cargo's CARGO_CFG_TARGET_OS"` (plan §4.7,
   `plan.md:846-849`); any other that proves unavoidable carries `reason = "…"` and is listed in the task's notes. No
   `// @ts-ignore`, `@ts-nocheck`, `eslint-disable` (`crates/core/tests/config_guard.rs:158-165`, `:1144-1153`).
3. **Unit tests live in `<stem>_tests.rs`**, declared at the end of `<stem>.rs` as `#[cfg(test)] #[path =
   "<stem>_tests.rs"] mod tests;` (an inline `mod tests {` is reported, `injection.rs` `RULE_INLINE_TESTS`).
4. **Literal hygiene.** (a) `crates/core/src/**` outside `platform/` holds none of `FORBIDDEN_OUTSIDE_PLATFORM`
   (`crates/core/tests/injection.rs:23-31`: `krateaudio.com`, `api.github.com`, `"C:\\`, `"/Library`, `"/usr/`, `"/home`,
   `"/Users`), **comments included**; `*_tests.rs` and `test_support.rs` are not scanned (`injection.rs` `scan_core_file`).
   So `core::self_update` matches the API host by labels and takes the endpoint host as a parameter (PD-4). (b)
   `crates/core/tests/roster.rs:11` bans the raw text `api.github.com` in core, helper, `src-tauri/src`, capabilities,
   `src/`, `src-tauri/tauri*.conf.json`, `index.html` (skipping `*_tests.rs`, `test_support.rs`, `*.test.ts`); comments in
   both `self_update.rs` files spell neither host. (c) `PHASE7_HOST_NEEDLES` (`injection.rs:258-263`: `github.com`,
   `githubusercontent.com`, `krateaudio.com`, `--test-threads`) may not occur in `install_tests.rs`, `download_tests.rs`,
   `test_support.rs` (`:249-256`). (d) `FR088_FILES` (`:319-328`: `install_tests.rs`, `test_support.rs`,
   `src/status.test.ts`, `src/main.test.ts`, …) match no `FR088_PATTERNS` (`:330-342`: `127\.0\.0\.1:[0-9]+`, `AppData`,
   `Program Files`, `/usr/`, …), comments included. (e) No file under `src/` (tests included) or `index.html` holds
   `http://` or `https://` (`config_guard.rs:1117-1160` `check_frontend`); the UI names no URL. (f) No file under
   `apps/krate-manager/` names any of the nine shipped plugins (`roster.rs`); use invented names (`Alpha`).
5. **Zero warnings.** `cargo fmt --all --check`; every `cargo clippy … -- -D warnings` with 0 `warning:` lines (lib,
   test, feature, example and harness builds); `npm run typecheck`; `npm run lint`; `node --check` on every new `.js`.
6. **Parallel tests.** Never `--test-threads`, `--no-file-parallelism`, `--test-concurrency=1`, `-j 1`. `cargo test` uses
   its default threads, vitest its parallel files, `node --test` parallel files. No test holds a `static` lock, a fixed
   port or a real sleep beyond a scripted fixture delay; each core test owns its `TempDir` and its own `FakeElevation`
   (each fake owns its guard, `test_support.rs:417-450`), and every registry (`BUSY`, `IN_FLIGHT`, `SELF_UPDATING`) is
   keyed by the test's own temp path, so parallel tests never contend. The harness runs its scenarios as concurrent child
   processes (PD-11).
7. **`cargo test` is `cargo test --workspace --locked` (or `-p <crate> --locked …`), never with `--all-targets`.**
   `cargo clippy --workspace --all-targets` is correct. `src-tauri` has `[lib] test = false` and `[[bin]] test = false`
   (`src-tauri/Cargo.toml:10-19`); no task adds a test target to it, and `src-tauri/tests/` must not exist
   (`config_guard.rs:826-828`).
8. **Lockfiles and dependencies.** Only T001 changes a `Cargo.toml` dependency or feature, a `Cargo.lock` or
   `package.json` (no npm package is added: plan §5). Every other cargo command uses `--locked`. A task that finds it
   needs a crate, a feature or an npm package stops, returns `blocked` and names it.
9. **Placeholder discipline (T001) and one working tree (G2).** T001's placeholders compile with zero warnings in every
   build: every parameter of a `todo!()` body is `_`-prefixed; every `todo!` names its owner (`todo!("T003")`); a
   placeholder never imports what only its future body uses. The owning task renames parameters and removes its
   `todo!`s; at close no `todo!(` or `unimplemented!(` remains outside `*_tests.rs` and `tests/` (T018 step 5). In G2,
   write each file whole so it compiles at every save (types and signatures first, bodies after). Never edit a file
   another task owns; when `cargo` fails on a file you do not own, wait 60 s and retry, three times, then record it and
   continue with what you can verify. **A compile error is never an expected red.**
10. **Target slots (disk: `df -h /f` this session shows 5.8 GB free; `apps/krate-manager/target` holds 14 GB).** The
    shared `apps/krate-manager/target` with `CARGO_BUILD_JOBS=16` for T001, T006 and T014 (the harness must use it, as
    CI does: `CARGO_TARGET_DIR="$PWD/target"`, plan §4.11). `CARGO_TARGET_DIR=F:/tmp/km11-target/core` with
    `CARGO_BUILD_JOBS=16` for T003, T004, T005, T013 (they queue on its build lock, accepted; one cold core graph is about
    1.5 GB). Before the first build in the slot run `df -h /f`; with less than 4 GB free use the shared target and say
    so. The last of T003 / T004 / T005 / T013 to finish deletes the slot (check the other three first); T018 confirms
    `F:/tmp/km11-target/` is gone.
11. **Scope fence (spec FR-092, `spec.md:786-788`; plan §2 `plan.md:319-374`).** Outside `apps/krate-manager/**` only
    these paths change: `specs/krate-manager-phase11/**`; `specs/Krate-manager-roadmap.md` (FR-093 (a), (b), (c), (e),
    T002); `specs/INDEX.md` (regenerated only, T018); `tools/gen-manager-latest.js`, `tools/manager-release-preflight.js`
    (new); `tools/feed-publish.js` (FR-043 only); `tools/feed-contract/tests/{gen-manager-latest,manager-release-preflight,
    release-manager-shape}.test.js` and `tools/feed-contract/tests/release-body.expected.md` (new);
    `tools/feed-contract/tests/{feed-publish-collect,manager-ci-shape,hygiene}.test.js`;
    `tools/feed-contract/keys/README.md` (§2's Phase 11 sentence); `.github/workflows/release-manager.yml` (new),
    `manager-ci.yml`, `feed-tools.yml`; `.gitattributes` **one added line only** (TA-2, T001:
    `apps/krate-manager/tests/fixtures/updater-refusals/fixtures/** -text`). **Not** any other `.gitattributes` line,
    `feed.yml`, `release.yml`, `ci.yml`, `docs.yml`, `backfill-sign.yml`, `tools/gen-feed.js`, `tools/feed-contract/`
    schemas, fixtures or `CONTRACT.md`, the helper crate.
12. **Line endings.** This checkout runs `core.autocrlf=true`. Measured this session (`tr -cd '\r' < f | wc -c` against
    `wc -l`): **CRLF** — root `Cargo.toml` 61/61, `src-tauri/Cargo.toml` 55/55, `crates/core/Cargo.toml` 58/58,
    `src-tauri/src/lib.rs` 115/115, `crates/core/src/lib.rs` 32/32, `crates/core/src/context.rs` 236/236,
    `crates/core/src/context_tests.rs` 708/708, `crates/core/src/test_support.rs` 3355/3355,
    `crates/core/tests/injection.rs` 6327/6327, `src/main.ts` 28/28, `src/main.test.ts` 253/253,
    `specs/Krate-manager-roadmap.md` 1077/1077, `.gitattributes` 31/31 (re-measured this revision); **LF** — `Cargo.lock` (0/5444), `tests/fixtures/clippy-guard/Cargo.lock`
    (0/5338), `.gitignore` (0/6), `tauri.conf.json` (0/25), `build.rs` (0/15), `capabilities/main.json` (0/15),
    `commands.rs` (0/219), `http.rs` (0/1353), `install.rs` (0/4233), `install_tests.rs` (0/6367), `download.rs`
    (0/1627), `config_guard.rs` (0/2655), `tests/required-tests.json` (0/516), `scripts/bundle.js` (0/106),
    `bundle.test.js` (0/21), `src/status.ts` (0/587), `src/status.test.ts` (0/1074), `package.json` (0/26),
    `manager-ci.yml` (0/1046), `feed-tools.yml` (0/154), `tools/feed-publish.js` (0/1043), `manager-ci-shape.test.js`
    (0/1632), `hygiene.test.js` (0/624), `feed-publish-collect.test.js` (0/1052), `keys/README.md` (0/77), `spec.md`
    (0/1136). An edit keeps the file's ending on every line it adds. New files are written LF.
13. **Shell discipline.** Every command runs in the Bash tool (Git Bash), foreground, output to a log under
    `F:/tmp/gate/km11-<task id>/`, then `tail`; never `cat` a whole log, never a drive-wide `find` / `du`. **No heredoc in
    a Bash command** (a repo hook blocks them): write scripts with the Write tool and run them with `node`. Never chain
    git commands. App-lane commands run from `apps/krate-manager/`; repo-lane commands from the repo root.
14. **No invented APIs.** Every crate, std and Tauri API used is one plan §1.2 (`plan.md:283-315`) or §5 verified:
    `tauri-plugin-updater` 2.14.1 (`UpdaterExt::updater_builder`, `UpdaterBuilder::{timeout, proxy, no_proxy,
    configure_client, build}`, `Updater::check`, `Update::{download, verify, install}` and its public fields, `Error`'s
    32 variants, `Builder::new().build()`), reqwest 0.13.5 (`redirect::Policy::custom`, `Attempt::{url, previous, follow,
    error}`, `ClientBuilder::{redirect, read_timeout}`), tauri 2.12.5 (`test::{mock_builder, mock_context, noop_assets}`,
    `Context::{config_mut, package_info_mut}`, `AppHandle::restart`, `utils::platform::bundle_type`, `ipc::Channel`),
    tauri-winres 0.3.7 (`WindowsResource::new().set_manifest(..).compile()`), minisign 0.10.0
    (`KeyPair::generate_unencrypted_keypair`, `sign`, `PublicKey::to_box`, `SignatureBox::to_string`), Node 24 built-ins.
    T001 fetches the updater source; any task that reads an updater item it relies on cites the line in
    `~/.cargo/registry/src/*/tauri-plugin-updater-2.14.1/`. An API that turns out to differ is a defect of the plan:
    record it under "Defects found" with the source line, and use the real API.
15. **Every claim cites what was read.** A task's notes cite file:line for every fact about existing code it relied on,
    read in that task's session, and quote the test output lines (`test result:` and the named tests) that close it.

### Lanes (referenced by every task)

| Lane | Commands (from `apps/krate-manager/` unless "repo root") | Pass |
|---|---|---|
| **L-RS** | `cargo fmt --all --check`; `cargo clippy --workspace --all-targets --locked -- -D warnings`; `cargo test --workspace --locked 2>&1 \| tee F:/tmp/gate/km11-<task>/cargo-test.txt` (Rule 7; Phase 10's took 57.4 s, `timings.md:18`) | fmt exit 0; clippy exit 0 with 0 `warning:` lines; every test binary `0 failed` |
| **L-OWN** | `CARGO_TARGET_DIR=<slot> CARGO_BUILD_JOBS=16 cargo test -p krate-manager-core --locked --lib <filter>` (or `--test config_guard` / `--test injection`) and `cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings` (a working loop, not a gate) | the task's own tests and 0 `warning:` lines |
| **L-APP** | `cargo clippy -p krate-manager --all-targets --locked -- -D warnings` | exit 0, 0 `warning:` lines |
| **L-DEVFEED** | `cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings` | exit 0, 0 `warning:` lines |
| **L-MANUAL** | `cargo clippy -p krate-manager --example manual_elevation --features manual-elevation --locked -- -D warnings` | exit 0, 0 `warning:` lines |
| **L-E2E** (TA-14) | `KRATE_E2E_FEED_URL=http://127.0.0.1:9/versions.json KRATE_E2E_MARKER_DIR=never-run KRATE_E2E_OLD_VERSION=0.0.0 cargo clippy -p krate-manager --all-targets --features updater-e2e --locked -- -D warnings` (compiles and lints the e2e module; nothing runs it; the same command and dummy values as `manager-ci.yml`'s `clippy updater-e2e` step, PD-32) | exit 0, 0 `warning:` lines |
| **L-GUARD** | the `run:` text of `manager-ci.yml` step `clippy-guard` (`.github/workflows/manager-ci.yml:118-136`), extracted into a file by a throwaway Node script (Rule 13) and executed with `"C:/Program Files/Git/bin/bash.exe"` with `RUNNER_TEMP=F:/tmp/gate/km11-<task>` and `RUNNER_OS=Windows` | `::notice title=clippy-guard (Windows)::rc=<non-zero> disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=<≥1> other-errors=0`, no `::error`; a `--locked` refusal is red |
| **L-HARNESS** (T016's `harness-lane.js`) | `cargo fmt --check --manifest-path tests/fixtures/updater-refusals/Cargo.toml`; `CARGO_TARGET_DIR="$PWD/target" cargo clippy --manifest-path tests/fixtures/updater-refusals/Cargo.toml --all-targets --locked -- -D warnings`; `CARGO_TARGET_DIR="$PWD/target" cargo run --manifest-path tests/fixtures/updater-refusals/Cargo.toml --locked`; the same with `-- --plant committed-key` and `-- --plant bind-any` (FR-083, FR-084, SC-007). **Under `D9 WINDOWS LOAD FAILURE`** (`evidence/d9-windows.txt`, TA-22): fmt and clippy still on Windows; the run and both plants in WSL Ubuntu 24.04 instead (`CARGO_TARGET_DIR=$HOME/km-target-harness`, plan §7.4 step 3; R-17's packages) | fmt / clippy exit 0, 0 `warning:` lines; the run exits 0 with each of the 27 names of T014 `ok` exactly once, `harness: 27 ok, 0 not ok` as the summary, the wall clock printed; each plant exits 1 with `E_HARNESS_KEY` / `E_HARNESS_BIND` as its first and only stdout line and no `ok ` line (PD-38: a plant run spawns only the `good` child). Last line `HARNESS LANE GREEN (Windows, <n> ok, <ms> ms)`, or under the fallback `HARNESS LANE GREEN (WSL, <n> ok, <ms> ms; Windows gap: <d9 line>)`; a load failure is never a stop (PD-33) and never a Windows pass |
| **L-TS** | `npm run typecheck`; `npm run lint`; `npm test` | each exit 0; vitest 57 titles before T007, 70 after (TA-1) |
| **L-NAPP** | `node --check` on each new / changed `scripts/*.js`; `node --test --test-reporter=tap "scripts/*.test.js" > F:/tmp/gate/km11-<task>/scripts.tap` (quoted glob) | TAP `# fail 0`; the six `node` names of TA-1 each `ok` once; no `Warning:` line |
| **L-NODE** | `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" > F:/tmp/gate/km11-<task>/all.tap` (repo root); `tail -12` | TAP `# fail 0`; no `Warning:` / `DeprecationWarning` / `ExperimentalWarning` line |
| **L-TAP** | `node specs/krate-manager-phase2/evidence/tap-check.js F:/tmp/gate/km11-<task>/all.tap` (repo root; runs the TAPCHECK body of `feed-tools.yml` itself) | before T017: `TAP plan=1..59` is no longer true once G2 adds four top-level tests, so L-TAP is run **only from T017 on**: exit 0 and `TAP plan=1..64 top-level=64` |
| **L-YAML** | `npx --yes yaml@2.6.1 valid < .github/workflows/<file>.yml` for `release-manager.yml`, `manager-ci.yml`, `feed-tools.yml` (repo root) | exit 0 each |
| **L-LINT** | `node tools/lint-serial-test-runs.js` (repo root) | exit 0 |
| **L-VLINT** | `node tools/lint-manager-version.js` (repo root) | exit 0, no stderr |
| **L-INDEX** | `node tools/gen-specs-index.js --check` (repo root) | exit 0 |
| **L-SCOPE** | `node specs/krate-manager-phase11/evidence/scope-check.js` (repo root; T016) | exit 0 |
| **L-REQ** | the `run:` text of `manager-ci.yml` step `required tests ran` (`:208-239`) extracted as L-GUARD does and run over L-RS's `cargo-test.txt`, a `npm test -- --reporter=default --reporter=json --outputFile.json=<dir>/vitest.json` report and L-NAPP's TAP, `RUNNER_OS=Windows` | `::notice title=required tests ran (Windows)::372 cargo + 70 vitest + 6 node` (355 `all` + 17 `windows`, TA-1), no `::error` |
| **L-WSL** (main loop) | `node specs/krate-manager-phase11/evidence/wsl-lane.js` (T016): in WSL, `CARGO_TARGET_DIR=$HOME/km-target cargo clippy -p krate-manager-core -p krate-manager-helper --all-targets --locked -- -D warnings && cargo clippy -p krate-manager-helper --all-targets --features ci-temp-roots --locked -- -D warnings && cargo test -p krate-manager-core -p krate-manager-helper --locked` | last line `WSL LANE GREEN …`; it closes no PENDING-CI row (it compiles the non-Windows PAC branch T004 refactors) |
| **L-CLOSE** (main loop) | `node specs/krate-manager-phase11/evidence/close-lane.js` (T016) | last line `CLOSE LANE GREEN` |

**Group gates** (run by the workflow's gate reporter; it runs the app lane, the WSL step and, for groups that touch
`tools/` or `.github/`, the pipeline lane). In addition each task runs the lanes its section names. After G1: L-RS,
L-APP, L-DEVFEED, L-MANUAL, L-E2E, L-GUARD, L-TS, L-NODE, L-VLINT. After G2: the same plus L-NAPP, L-HARNESS (T014's
own), L-SCOPE, `--self-test` of T016's scripts. After G3: everything T018 lists, then L-CLOSE (main loop).

---

## Preconditions — rulings before the build stage

- **Spec rulings (plan header "Build preconditions", §10).** The user ruled plan §10 P-1 to P-6 on 2026-11-01
  (`spec.md:1138-1145`); this file is written to them, and T002 writes SI-1 to SI-40 into the spec (SI-30 to SI-33 encode
  P-1, P-3, P-4, P-5; SI-34 to SI-40 are plan revision 4's review fixes, no new user decision, plan header). **Precondition check (T001 and T002 run it first; on failure the task changes nothing and returns
  `blocked`, quoting the item):** in `spec.md` `## Clarifications` (`spec.md:1120-1145`), a bullet dated after
  `### Session 2026-11-01 (plan preconditions)` that rules **against** a plan PD / SI row or a TA row of this file, or
  any bullet that rules P-1 to P-6 otherwise than plan §10 records them (no `socks`, keeping `zip`, a `.deb` self-update
  through `pkexec`, a refusing install button for a `.deb`, the release key in the build legs, no `clippy updater-e2e`
  step, a stop on a Windows load failure). Absence of a bullet is acceptance. The main loop then re-plans the named
  sections.
- **R-17 (plan §10, the user's, open).** Written to recommendation (a): before the build stage the user installs the CI
  Linux leg's packages in WSL (`manager-ci.yml:53-54`). No task installs them, checks for them before T014, or stops on
  their absence; they matter only under `D9 WINDOWS LOAD FAILURE`, where L-HARNESS's WSL run reports `HARNESS LANE RED:
  WSL …` without them (plan R-18) and the main loop takes it to the user.
- **D9 without a stop (ruling P-5, PD-33).** If T014's first run of the harness exits `0xc0000139` (or any loader failure
  before the first output line) on Windows with the manifest embedded, T014 writes the measurement to
  `specs/krate-manager-phase11/evidence/d9-windows.txt` (TA-22) and **goes on**: it never returns `blocked` for it. The
  harness stays formatted and linted on Windows; its run and plants go to WSL (L-HARNESS) and to `check (macos-latest)` /
  `check (ubuntu-24.04)` (T017's `if:`); SC-007's Windows row is a recorded gap quoting the d9 line.

### Task-list amendments (TA)

Rows marked **spec** change spec text (T002 writes them, tagged `(tasks TA-n)`); the others explain why a task differs
from a plan block or fix what the plan left open.

| Id | Amendment | Why | Spec? |
|---|---|---|---|
| TA-1 | **`tests/required-tests.json` and `EXPECTED_REQUIRED` are written in T001** with exactly plan §4.13's names. Counted this session (`node -e` over the file): `cargo.all` 331, `cargo.windows` 17, `cargo.macos` 49, `cargo.linux` 43, `vitest` 57, `node` 3 (re-counted this revision, unchanged). After T001: `cargo.all` 331 + 24 = **355** (plan §4.13: the 22 of revision 2 plus PD-36's `update_inputs_only_from_production`, `planted_update_inputs_fail_the_pin`; ruling P-3's and SI-40's core rows live inside the existing `url_policy`, `packaging_rule` and `view_from_update`, and PD-37's pin inside `self_update_confinement_holds`, so no other cargo name is added), the per-OS lists unchanged, `vitest` 57 + 13 = **70** (revision 4 moved `disables plugin actions while the manager updates itself` from `status.test.ts` to `main.test.ts` under the same title, plan §4.6: count unchanged), `node` 3 + 3 = **6**. The 24 cargo names, the 13 vitest titles and the 3 node titles are listed in T001 step 11. The harness's 27 line names are checked by the `updater refusals` step, not by this file (plan §4.13) | both files are shared, so they belong to the one sequential task; `checkRequired` deep-compares them (`manager-ci-shape.test.js:1264-1276`), so they move together | no |
| TA-2 | **FR-097 is already in the tree:** `.gitattributes:29` holds `tools/feed-contract/keys/** -text` (its comment `:27-28`; added by Phase 10's close, `git diff f6022bd5 HEAD -- .gitattributes`), and `git check-attr -a tools/feed-contract/keys/krate-signing.pub` prints `text: unset`. **Revision 3 (plan §2, revision 4):** T001 adds exactly one line after `:31` (`apps/krate-manager/tests/fixtures/helper/inno-locked/** -text`), with a one-line comment above it: `# The rsign-format fixture of the Krate Manager Phase 11 refusal harness: its signature covers the raw bytes.` and `apps/krate-manager/tests/fixtures/updater-refusals/fixtures/** -text`, both CRLF like the file (31/31, Rule 12). No other line changes; T018 records `git check-attr -a` for `krate-signing.pub` (FR-097) and for `…/fixtures/rsign/package.bin` (`text: unset`) | the plan's revision-1 §2 row predates `4f04dc03`; revision 4 adds the fixture line | spec: FR-097 "already met" |
| TA-3 | **T001 widens the existing `config_guard.rs` pins its edits trip**, each to the new exact value (never loosened to "any"): `must_be_absent` drops `/plugins` (`config_guard.rs:660-668`; `/bundle/createUpdaterArtifacts` stays absent); the platform-file loop admits `tauri.release.conf.json` only when it equals `{"bundle":{"createUpdaterArtifacts":true}}` (`:683-714`); the capability `permissions` list becomes the ten (`:738-748`); the `build.rs` commands string becomes the ten (`:773-787`); `HANDLER_LIST` becomes the ten (`:169`); `SRC_TAURI_DEPENDENCIES` becomes the eight sorted keys (`:50-57`); the "one tauri-plugin line" rule accepts exactly `OPENER_DEPENDENCY_LINE` and `tauri-plugin-updater = { workspace = true }` inside `[dependencies]` (`:937-952`); the `[features]` pin becomes the three (`:901-903`); `CONFINED` becomes `(&str, &[&str], &str)` with `LoopbackOnly` and `HttpConfig::loopback` homed in `src/dev_feed.rs` **and** `src/self_update.rs` (`:81-94`, checked at `:998-1006`) | without these the G1 gate is red on `real_src_tauri_config_passes_the_guard` and its siblings the moment the manifests and configs change; the plan's §4.12 names only the new pins. `LoopbackOnly` / `HttpConfig::loopback` are needed by the e2e module (plan §4.5) and are not in the plan at all (Defect D-1) | no |
| TA-4 | **T001 writes SI-2's widened `tar` rule and its test** (`updater_tar_only_through_the_plugin`), because the real lock gains `tar` the moment the updater is locked (macOS dependency, plan §1.2 `plan.md:287`). Rule, per lockfile: (1) BFS over `[[package]].dependencies` from the lock's root packages (path packages: `krate-manager`, `krate-manager-core`, `krate-manager-helper`, `clippy-guard`, `updater-refusals`), **never expanding `tauri-plugin-updater`**: any `tar` reached is a finding; (2) the number of `[[package]] name = "tar"` entries must not exceed the number of `tar` entries in `tauri-plugin-updater`'s own `dependencies` list (zero when the updater is absent): the excess is a finding. The manifest-key half (`toml_dependency_names(..).contains("tar")`) is unchanged. `locks()` (`injection.rs:3129-3135`) gains `tests/fixtures/updater-refusals/Cargo.lock` | (2) keeps the existing plant of an orphan `[[package]] name = "tar"` reported (`injection.rs:6056-6058`), which a reachability-only rule would pass vacuously; SI-2's "every path passes through the updater" is (1) | spec: FR-094 states the rule |
| TA-5 | **The harness manifest, its lock and a placeholder `src/main.rs` are T001's** (plan §4.7 `plan.md:733-755` manifest verbatim). Lock procedure: `cp Cargo.lock tests/fixtures/updater-refusals/Cargo.lock`, then `cargo metadata --manifest-path tests/fixtures/updater-refusals/Cargo.toml --format-version 1 > /dev/null`, which keeps every locked version, prunes what the harness does not use and adds only what it needs (tauri's `test` feature); then a throwaway Node script compares `name`+`version` of every `[[package]]` present in both locks (must be equal) and prints what the harness lock adds | dependency files are shared (Rule 8), and the tar rule (TA-4) reads the lock; the plan's "generate-lockfile then trim with `--precise`" (`plan.md:754-755`) re-resolves everything first and is replaced by a procedure that cannot move a shared version | no |
| TA-6 | **`http::redact_userinfo` is `pub(crate)`** (`crates/core/src/http.rs:196`), so `src-tauri`'s `map_error` cannot call it (plan §3.2 `plan.md:441`). `core::self_update` gains `pub fn redacted(text: &str) -> String` (calls `crate::http::redact_userinfo`); `map_error` uses it for every `detail` | http.rs visibility stays as it is | no |
| TA-7 | **`status::os_name` is private** (`crates/core/src/status.rs:367-373`); `self_update::view` maps `Os` → `status::OsName` (`status.rs:33-39`, public) with its own three-arm `match`. `status.rs` is not edited | plan §4.1's `view(os: Os, …)` returns `ManagerUpdateView { os: OsName }` | no |
| TA-8 | **Four test seams, all `#[cfg(any(test, feature = "test-support"))] pub`, written in T001:** `ElevationGuard::hold_for_test(&self) -> GuardHeldForTest<'_>` (sets `Hold::Running`; `Drop` restores `Free`), `ElevationGuard::hold_stale_for_test(&self, job: PathBuf, result: PathBuf) -> GuardHeldForTest<'_>` (sets `Hold::Stale { job, result }`; `Drop` restores `Free`), `install::hold_busy_for_test(state_dir: &Path, stem: &str) -> BusyHeldForTest` (wraps the private `Busy::enter`, `install.rs:3849`, panics "busy key already held" on `None`), `download::hold_in_flight_for_test(downloads: &Path) -> InFlightHeldForTest` (inserts `(downloads, [0xA5; 32])` into `IN_FLIGHT`, `download.rs:647-672`; `Drop` removes it) | plan §4.7 names only `hold_for_test` (`plan.md:825-830`); SC-005's stale, `BUSY` and download rows need the other three (plan §7.1 `plan.md:1143` leaves them open). `pub` under that cfg is never dead code; `ElevationGuard::enter`, `Busy::enter` and `InFlight::enter` stay private | no |
| TA-9 | **T001 makes the cross-task items real:** in `core::self_update`, the `SELF_UPDATING` registry, `InFlightGuard`, `in_flight`, `action_gate`, `SelfUpdating`, `SELF_UPDATING_CODE`; in `install.rs`, `is_held`, `any_busy`, the seams, `InstallError::SelfUpdating`; in `download.rs`, `any_in_flight`, the seam, `DownloadError::SelfUpdating`. So T003's `in_flight_flag` and T003's `busy_rule` have no red-first for those items; their notes say so and their assertions are written against plan §4.1 / §7.1, never copied from the code | T005's gate test and T003's busy rule both need them; split across two G2 writers they would be a cross-task compile dependency | no |
| TA-10 | **A non-required red-first test for T004:** `http::tests::route_choice_static_direct_and_refused` (static proxy → `Proxy`, a bypassed host → `Direct`, no proxy → `Direct`, an `ftp://` static proxy → `Err`) | T004's only required test (`release_for_restart_frees_the_lock`) does not reach `route_choice`; T003's `proxy_choice_matches_core` does, but in another task. Totals stay TA-1's | no |
| TA-11 | **`gen-manager-latest.js`'s asset-set rule:** for each selected OS, the updater asset and its `.sig` are required; the OS's release-only asset (`Krate-Manager-<v>-Linux-amd64.deb`, `Krate-Manager-<v>-macOS.pkg`) may be present or absent and is never named in `latest.json`; any other file (a `.deb.sig` included, SI-21) is extra. Missing or extra → `E_LATEST_ASSETS` naming each | plan §4.9's "exactly the D6 names" (`plan.md:884`) cannot hold on a build leg, which has no pkg yet (`package-macos` makes it), and on `publish`, which has the pkg | spec: FR-042 |
| TA-12 | **`manager-release-preflight.js` takes `--pubkey <file>`** (default `tools/feed-contract/keys/krate-signing.pub`, resolved from the tool's own directory) and `preflight(opts)` takes `pubkeyText` | SC-011's "a fixture key as `krate-signing.pub` on a real run" (`spec.md:884-885`) cannot be tested without replacing the committed file, which the write guard forbids | spec: FR-051 |
| TA-13 | **The `. install (` / `download_and_install` / `. restart (` / `process :: restart` confinement scans `src-tauri/src/**` only** (plus T013's separate harness rule) | core cannot name the updater (`tauri_plugin_updater` is confined, FR-094), and `crates/core/src/install_tests.rs` holds `fx.install(ALPHA, V12)` at `:1154`, `:1450`, … (a Phase 9 fixture method), which a workspace-wide token rule would report | spec: FR-094 |
| TA-14 | **Rewritten by ruling P-4 (PD-32).** L-E2E stays the local lane, and `manager-ci.yml` `check` gains the same command as step `clippy updater-e2e` on all three legs (T017), with the same dummy `KRATE_E2E_*` values, never run, pinned in `manager-ci-shape.test.js` `STEPS` / `ASSERTION_STEPS` / `COMMANDS` and by `E2E_FEATURE_STEPS` | the user's ruling (`spec.md:1143`): a break shows on push, not first in a release dispatch | spec: SI-32 |
| TA-15 | **FR-093 (d) (the Phase 11 status line) is not a task:** the workflow's roadmap stage writes it after compliance (`.claude/workflows/krate-manager-phase.js:769`) | a task-written status line would be replaced anyway | no |
| TA-16 | `http::MAX_REDIRECTS` is `u32` (`http.rs:24`): `redirect_decision` compares `previous.len() > MAX_REDIRECTS as usize`; `download::DOWNLOADS_DIR` is at `download.rs:29` (the plan cites `:1370`) | the plan's text | no |
| TA-17 | **The keys README §2 Phase 11 sentence (SI-20) keeps `TAURI_SIGNING_PRIVATE_KEY_PASSWORD` within 200 characters after `Phase 11`**: `workflow-shape.test.js:602-604` asserts `/Phase 11[\s\S]{0,200}?TAURI_SIGNING_PRIVATE_KEY_PASSWORD/` | SI-20's suggested wording (`plan.md:158-160`) drops the variable name and would turn that test red | no |
| TA-18 | **`release-manager.yml` and its shape test are a G2 [P] task (T015)**: both files are new and have exactly one writer; nothing else in G2 reads or writes them | it is the phase's largest single artifact; a later sequential group would add its whole duration to the wall clock | no |
| TA-19 | **The e2e module writes no `eprintln!`** (`config_guard.rs:812-824` requires exactly one `eprintln!` in `src-tauri/src`, the one at `lib.rs:82`); it logs through `log::` | the plan does not say | no |
| TA-20 | **The harness reads its target key from the fetched updater source.** If 2.14.1 has no public `tauri_plugin_updater::target()` (plan §4.7 `plan.md:832-834` assumes one), the harness builds `<os>-<arch>` from `std::env::consts::{OS, ARCH}` mapped as the updater maps them (cite the updater lines), and records it under "Defects found" | the source is not in the local registry until T001 fetches it (Rule 1) | no |
| TA-21 | **`ManagerUpdateView.error` is `Option<ErrorView>`** (`ErrorView { code: &'static str, sentence: &'static str, reason: Option<UrlRefusal>, detail: Option<String> }`, the last two `skip_serializing_if = "Option::is_none"`), built by `SelfUpdateError::view(&self) -> ErrorView`; `manager_update_install`'s `Err(String)` is `serde_json::to_string(&err.view())` | plan §3.1 (`plan.md:402-404`) describes the wrapper and §4.1 (`plan.md:494`) uses it; this fixes the signature both sides compile against | no |
| TA-22 | **`specs/krate-manager-phase11/evidence/d9-windows.txt` is T014's** (new, LF, exactly one line: `D9 WINDOWS LOADS`, or `D9 WINDOWS LOAD FAILURE exit=<code as printed by the shell, and as 0x… hex> <loader message or "no output">`). T016's `harness-lane.js` and T017 read it; T017 writes the `updater refusals` `if:` and the shape test's `HARNESS_ON_WINDOWS` from it, quoting the line in a comment; T018 copies it into the notes | PD-33 makes the G3 workflow edit and the close verdict functions of the measurement; one owner, one format | spec: SI-33 |
| TA-23 | **`status.ts`'s `releaseButton` is exported with an optional `label`** (default `STATUS_TEXTS.openReleasePage`, so every existing call and test is unchanged, `status.ts:250-280`); `manager_update.ts` calls it for the `.deb` action. Both files are T007's | plan §4.6 (PD-31): one open-and-report-failure path, not a copy | no |
| TA-24 | **Every harness package URL uses the per-tag path of this project** `<fixture>/<MANAGER_REPO>/releases/download/manager/v<version>/<asset>` (`MANAGER_REPO` read from `krate_manager_core::self_update`, never spelled in the harness; `<version>` the version that scenario's `latest.json` announces), and the fixture server routes are named by it. Revision 3: was `o/r`, which PD-27 (revision 4) now refuses under `LoopbackOnly` too | `check_update_package_url` / `update_action` require the 7-segment path with `MANAGER_REPO` and `v<announced>` under both policies (plan §4.1, PD-27, SI-40); any other path makes `check` a view error and every downloading scenario red | no |
| TA-25 | **The rsign fixture's untrusted comment is set with `-c`.** `make-rsign-fixture.js` signs with `rsign sign -W -s <tmp>/k.key -c "rsign-format fixture (Krate Manager Phase 11)" -t "timestamp:0<TAB>file:package.bin<TAB>version:0.2.0" -x <tmp>/package.bin.minisig fixtures/rsign/package.bin` (`rsign sign --help` this session lists `-c, --untrusted-comment <UNTRUSTED_COMMENT>  add a one-line untrusted comment`), never with rsign's default comment | rsign's default first line is `untrusted comment: signature from rsign secret key` (the KAT's committed `.minisig`, `head -1` this session), and the existing FR-083 tree scan `no_secret_key_in_app_tree` (`crates/core/tests/injection.rs:5746`) walks every file of `apps/krate-manager/` but `target` / `node_modules` / `dist` (`:2874-2904`) and reports any `untrusted comment:` line containing `secret key` as `a secret-key comment` (`secret_text_hits`, `:2917-2936`; also inside any ≥ 40-character base64 run, `:2987-2996`), so a fixture with the default comment turns an existing guard red (D-20). Untrusted comments are not covered by the signature; the verifier needs only the `untrusted comment:` prefix line. What the fixture does **not** prove locally, rsign's default comment line, is proved on all three CI legs by `updater-e2e`'s copied `sign updater artifacts` step (PD-35 (a), no `-c`), recorded as the scenario's limit | no |
| TA-26 | **PD-36's `UpdateInputs {` literal rule counts struct-literal expressions only:** an `UpdateInputs {` token pair directly preceded by `struct` or `impl` (the type's own definition and its `impl` block, both in `src-tauri/src/self_update.rs`) is not a literal | plan §4.12 "in `src-tauri/src` the only other `UpdateInputs {` literal is inside `mod e2e`" would otherwise report the definition `pub struct UpdateInputs {` and `impl UpdateInputs {` (plan §4.3) as literals (D-21) | no |
| TA-27 | **The e2e module's loopback `HttpConfig` is `HttpConfig::loopback(http::CONNECT_TIMEOUT, http::TOTAL_TIMEOUT)`** (`crates/core/src/http.rs:22-23`, `:58`; the same pair `config_for_dev_url` passes, `:604`) | revision 2 told T006 to copy "the timeouts `dev_feed.rs` uses", but `dev_feed.rs` calls `http::config_for_dev_url` (`src-tauri/src/dev_feed.rs:22`), a token `config_guard` confines to that file (`config_guard.rs:82`) | no |
| TA-28 | **The rsign fixture is T014's** (`tests/fixtures/updater-refusals/make-rsign-fixture.js`, `fixtures/rsign/{package.bin, package.bin.minisig, rsign.pub}`, and its printed record `specs/krate-manager-phase11/evidence/rsign-fixture.txt`), made in T014's step 0 before the harness's first build. The script is ESM (the app's `package.json` has `"type": "module"`, `:4`), Node built-ins only, outside eslint (`eslint.config.mjs` ignores `tests/fixtures/`) and outside both tsconfigs; it normalises CRLF to LF in the two text files rsign writes and records both counts (as the KAT generator did, `fixtures/kat/README.md` "6. CRLF -> LF normalisation") | the harness `include_bytes!`es the files, so they must exist before T014 compiles `rsign-format`; nothing else reads them, so a separate task would only add a cross-task compile dependency inside G2 | no |

---

## Dependency overview

| Group | Mode | Tasks | Needs |
|---|---|---|---|
| G1 Foundation | **sequential** (1) | T001: both new dependencies and the feature, the three lockfiles, the harness manifest, `.gitignore`, the `.gitattributes` line (TA-2), the four config files, every crate-root line, the `core::self_update` and `src-tauri` `self_update` surfaces, the commands, the cross-task real items and seams, the trip-wire guard pins (TA-3, TA-4), the required-test lists (TA-1) | Preconditions |
| G2 Docs, core, shell, UI, scripts, tools, guards, harness, release workflow, evidence | **[P]** (15) | T002 spec + roadmap + keys README · T003 `core::self_update` + its 12 tests · T004 `http::route_choice` + `context::release_for_restart` · T005 the four action gates + `refuse_while_self_updating` · T006 `src-tauri` `self_update.rs` (adapter, error map, installer, e2e module) · T007 the UI · T008 `bundle.js --release` · T009 `e2e-update-server.js` · T010 `gen-manager-latest.js` · T011 `manager-release-preflight.js` · T012 `feed-publish.js` per-tag rule · T013 the new guard rules · T014 the refusal harness · T015 `release-manager.yml` + its shape test · T016 evidence scripts | G1 |
| G3 Integration | **sequential** (2, in order) | T017 `manager-ci.yml` (four `check` steps) + `manager-ci-shape.test.js` + `feed-tools.yml` + `hygiene.test.js` · T018 L-HARNESS, L-REQ, lints, scope, greps, INDEX, the PENDING-CI list, the hand-over | G2 |

**File ownership in G2 (each file has exactly one G2 writer).** T002: `specs/krate-manager-phase11/spec.md`,
`specs/Krate-manager-roadmap.md`, `tools/feed-contract/keys/README.md`. T003: `crates/core/src/self_update.rs`,
`self_update_tests.rs`. T004: `crates/core/src/http.rs`, `http_tests.rs`, `context.rs`, `context_tests.rs`. T005:
`crates/core/src/install.rs`, `install_tests.rs`, `download.rs`. T006: `src-tauri/src/self_update.rs`. T007:
`src/manager_update.ts`, `src/manager_update.test.ts` (new), `src/main.ts`, `src/main.test.ts`, `src/status.ts`,
`src/status.test.ts`. T008: `scripts/bundle.js`, `scripts/bundle.test.js`. T009: `scripts/e2e-update-server.js`,
`scripts/e2e-update-server.test.js` (new). T010: `tools/gen-manager-latest.js`,
`tools/feed-contract/tests/gen-manager-latest.test.js` (new). T011: `tools/manager-release-preflight.js`,
`tools/feed-contract/tests/manager-release-preflight.test.js`, `tools/feed-contract/tests/release-body.expected.md` (new).
T012: `tools/feed-publish.js`, `tools/feed-contract/tests/feed-publish-collect.test.js`. T013:
`crates/core/tests/config_guard.rs`, `crates/core/tests/injection.rs`. T014:
`tests/fixtures/updater-refusals/build.rs`, `src/{main,scenarios,keys,map_errors,confinement,socks}.rs`,
`make-rsign-fixture.js`, `fixtures/rsign/{package.bin,package.bin.minisig,rsign.pub}` (TA-28),
`specs/krate-manager-phase11/evidence/d9-windows.txt` (TA-22), `…/evidence/rsign-fixture.txt` (TA-28). T015:
`.github/workflows/release-manager.yml`, `tools/feed-contract/tests/release-manager-shape.test.js` (new). T016:
`specs/krate-manager-phase11/evidence/{scope-check,harness-lane,wsl-lane,close-lane}.js` (new). Paths without a prefix
are under `apps/krate-manager/`.

**Why this shape.** T001 is the only task that edits shared files, so it is a group of its own; it writes every `pub mod`
/ `mod` line, every manifest line and both self-update surfaces up front, so fifteen G2 tasks run side by side and none
adds a line to a shared file (Phase 4's seven core modules ran one after another because each added its own `pub mod`
line). The core is cut along its dependency seams: the self-update logic (T003) is separate from the two pieces of
existing core it calls (T004: the route decision and the lock release) and from the four existing entry points that
call it (T005); the in-flight registry, the seams and the error variants they share are real in T001 (TA-9), so the
only in-group expected reds are the two T003 tests that cross T004's placeholders. The `src-tauri` adapter (T006) has no
test target; it is proven by the harness (T014), which compiles against T001's frozen surface and goes green once T003
and T006 land, and by the guards (T013). The harness's rsign fixture is made inside T014 (step 0, seconds) rather than
by its own task: only T014 reads it, through `include_bytes!`, so a separate G2 task would be a compile dependency
inside the group, and T001 would lengthen the one sequential group (TA-28). The release workflow (T015) is new, single-writer and the largest artifact, so
it runs beside the code (TA-18). G3 is two dependent tasks: T017 edits three existing workflows and two shared test
files whose expected values (the TAP plan of 64, the new step names) depend on G2's files, and T018's checks read the
whole tree after it. No group is a chain of independent tasks.

**SC owners:** SC-001, SC-002, SC-003 core rows, SC-004 `endpoint_matches_feed_tool`, SC-005, SC-023 `in_flight_flag`,
SC-024 `timeouts`, SC-025 `no_context_defaults` T003 · SC-006, TA-10 T004 · SC-023 `refuse_while_self_updating` T005 ·
FR-022 / FR-023 / FR-027 code, FR-070, PD-36 body, PD-37 pre-check T006 (proven by T014, T013) · SC-003 / SC-023 /
SC-025 vitest rows T007 · SC-008 T008 · FR-071 server T009 · SC-009 T010 · SC-011 (with `E_PREFLIGHT_REPO`, SI-40) T011
· SC-010 T012 · SC-004 / SC-012 / SC-024 guard rows (PD-36, PD-37 pins included) T013 (+ T001's
`updater_tar_only_through_the_plugin`) · SC-007, SC-023 / SC-024 harness rows (`rsign-format`, `production-inputs`
included) T014 (measured again by T018) · SC-012 / SC-015 / SC-021 / SC-022 / SC-026 static halves T015 · SC-017 static
half, FR-091 TAP plan T017 · SC-012 zero-warning row, FR-092, FR-097 T018 + L-CLOSE · SC-013 to SC-022, SC-026
PENDING-CI (list at the end).

---

## G1 — Foundation (sequential: one task, every shared file)

### T001 — Precondition, manifests and locks, the harness manifest, `.gitattributes`, configs, crate roots, both self-update surfaces, the commands, the cross-task items and seams, the trip-wire guard pins, the required-test lists (spec FR-001, FR-002, FR-003 (d), FR-017, FR-018 types, FR-020, FR-021, FR-070, FR-094 (members / exclude, tar), FR-095; plan §2, §3.1, §4.1 surface, §4.2, §4.3 surface, §4.4, §4.5 surface, §4.7 manifest, §4.13; PD-1, PD-2, PD-7 to PD-10, PD-25, PD-26, PD-27 constant, PD-29, PD-36 call form; SI-1, SI-2, SI-3, SI-11, SI-22, SI-28; TA-1 to TA-5, TA-8, TA-9, TA-21)

Target: the shared `apps/krate-manager/target`, `CARGO_BUILD_JOBS=16`. Logs under `F:/tmp/gate/km11-T001/`.

**Files (edit):** `Cargo.toml` (CRLF), `Cargo.lock`, `src-tauri/Cargo.toml` (CRLF), `tests/fixtures/clippy-guard/Cargo.lock`,
`.gitignore`, the repo root's `.gitattributes` (CRLF, one comment + one rule line, TA-2), `src-tauri/tauri.conf.json`, `src-tauri/build.rs`, `src-tauri/capabilities/main.json`,
`src-tauri/src/lib.rs` (CRLF), `src-tauri/src/commands.rs`, `crates/core/src/lib.rs` (CRLF), `crates/core/src/http.rs`,
`crates/core/src/context.rs` (CRLF), `crates/core/src/install.rs`, `crates/core/src/download.rs`,
`crates/core/tests/config_guard.rs`, `crates/core/tests/injection.rs` (CRLF), `tests/required-tests.json`,
`tools/feed-contract/tests/manager-ci-shape.test.js` (`EXPECTED_REQUIRED` only). **Files (new):**
`src-tauri/tauri.release.conf.json`, `src-tauri/src/self_update.rs` (placeholder for T006),
`crates/core/src/self_update.rs` (surface + TA-9 items, bodies for T003), `crates/core/src/self_update_tests.rs` (a
one-line `//!` doc comment only, for T003), `tests/fixtures/updater-refusals/Cargo.toml`, `…/Cargo.lock`,
`…/src/main.rs` (placeholder for T014).

0. **Precondition check** (Preconditions above). On failure change nothing and return `blocked`.
1. **Red first (guards).** Before any manifest edit, write `updater_tar_only_through_the_plugin` in `injection.rs`
   (step 10's rule and plants) and run `cargo test -p krate-manager-core --locked --test injection
   updater_tar_only_through_the_plugin`: it fails (the new plant "a lock where `krate-manager-core` reaches `tar`" is
   reported by the old rule as a bare package name, but the new real-tree row "the updater's `tar` is allowed" cannot be
   built yet — record the actual failing line). Then make the manifest edits of steps 2-3 and run `cargo test
   --workspace --locked`: record which existing guard tests go red (expected: `real_src_tauri_config_passes_the_guard`
   and the extract / verify confinement test that runs the tar rule, `injection.rs:5925-5935`); those reds are what
   steps 9-10 fix.
2. **Manifests.**
   - Root `Cargo.toml`: `[workspace] exclude` (`:3`) becomes `["tests/fixtures/clippy-guard", "tests/fixtures/argv-recorder",
     "tests/fixtures/locked-dll", "tests/fixtures/updater-refusals"]` (SI-11: exclude only, never a member). In
     `[workspace.dependencies]`, after the `tauri-plugin-opener` line (`:43`): `# Phase 11 PD-2: src-tauri self_update.rs
     only; exact pin (SI-1), no zip (SI-3), no system-proxy (D1)` and `tauri-plugin-updater = { version = "=2.14.1",
     default-features = false, features = ["rustls-tls"] }`.
   - `src-tauri/Cargo.toml` `[dependencies]` (`:24-31`), after `krate-manager-core`: `# Phase 11 PD-5, PD-30, SI-28:
     self_update.rs only (redirect::Policy, ClientBuilder::read_timeout); the updater's own reqwest, socks unified into its
     client`, `reqwest = { version = "=0.13.5", default-features = false, features = ["socks"] }` (ruling P-1),
     `# Phase 11 FR-001: self_update.rs only`, `tauri-plugin-updater = { workspace = true }`
     (neither comment may contain the text `tauri-plugin`, `config_guard.rs:937-952`). `[features]` (`:44-47`) gains
     `# Phase 11 FR-070: never default; only release-manager.yml's updater-e2e job enables it` and `updater-e2e = []`
     after `manual-elevation = []`.
3. **Locks.** From `apps/krate-manager/`: `cargo metadata --format-version 1 > /dev/null` (downloads the updater from
   crates.io, Rule 1). Assert, by reading `Cargo.lock`: exactly one `name = "tauri-plugin-updater"` with `version =
   "2.14.1"`; exactly one `name = "reqwest"`, still `0.13.5` (`Cargo.lock:2989-2990` before); no existing package changed
   version (a throwaway Node script diffs `[[package]]` `name`+`version` pairs against `git show HEAD:apps/krate-manager/Cargo.lock`
   and prints only additions; any change or removal → stop, `blocked`, naming it). Record the added package names in the
   notes (plan §5 expects `tar`, `osakit`, `objc2-osa-kit` among them; `infer 0.22.0` and `dirs 7.0.0` are already locked,
   `Cargo.lock:1791`, `:794`). Then `cargo metadata --manifest-path tests/fixtures/clippy-guard/Cargo.toml --format-version
   1 > /dev/null` (the clippy-guard depends on `src-tauri`, `tests/fixtures/clippy-guard/Cargo.toml:6-7`). **`socks`
   (PD-30) re-locks to no package change** in all three locks (the harness lock in step 4): reqwest 0.13.5's `socks = []`
   adds no dependency and Cargo.lock records no features, so no added package may be attributable to it; record
   `cargo tree -p krate-manager -e features -i reqwest --locked` showing `reqwest feature "socks"` (the unification
   evidence) in the notes. Any lock package that appears only because of `socks` is a stop: `blocked`, naming it.
4. **The harness manifest (TA-5).** `tests/fixtures/updater-refusals/Cargo.toml` exactly plan §4.7 (`plan.md:734-753`)
   with `[lints.clippy] allow_attributes = "deny"` and `allow_attributes_without_reason = "deny"` written as two lines
   under `[lints.clippy]`. `src/main.rs`: `//! Phase 11 refusal harness (D9, FR-080); the body is T014's.` then `fn main()
   {` / `    todo!("T014")` / `}`. No `build.rs` yet (T014's). Lock by TA-5's procedure; then `cargo metadata --manifest-path
   tests/fixtures/updater-refusals/Cargo.toml --format-version 1 --locked > /dev/null` must exit 0. `.gitignore` gains
   `/tests/fixtures/updater-refusals/target/` after `/tests/fixtures/clippy-guard/target/` (`.gitignore:5`). The repo
   root's `.gitattributes` gains TA-2's two lines after `:31`, CRLF (a throwaway Node script appends them and then asserts
   33 lines, 33 CR bytes; `git diff -- .gitattributes` shows exactly two `+` lines); `git check-attr text --
   apps/krate-manager/tests/fixtures/updater-refusals/fixtures/rsign/package.bin` → `text: unset` (the path need not
   exist for `check-attr`).
5. **Configs.** `tauri.conf.json`: a top-level `"plugins"` after `"bundle"`, exactly plan §4.4 (`plan.md:660-663`), with
   the pubkey `dW50cnVzdGVkIGNvbW1lbnQ6IG1pbmlzaWduIHB1YmxpYyBrZXk6IDlGRUIzNkY5MTE0MjczQkMKUldTOGMwSVIrVGJybi9pWUNqR2RudFF0N3ZuQm9LcE43VVZPdmhIVWVNa1FpN0t5REdBMkpxeUoK`
   (`base64 -w0 tools/feed-contract/keys/krate-signing.pub` this session; equals `plan.md:247`), endpoints
   `["https://krateaudio.com/manager/latest.json"]`, `"requireSignedVersion": true`, `"allowDowngrades": false`,
   `"windows": { "installMode": "passive" }`. `tauri.release.conf.json`: the one line
   `{"bundle":{"createUpdaterArtifacts":true}}` and a newline. `build.rs`: the `commands(&[…])` list (`:4-12`) gains
   `"manager_update_check", "manager_update_install"` after `"remove_plugin"`. `capabilities/main.json`: `"allow-manager-update-check"`,
   `"allow-manager-update-install"` after `"allow-remove-plugin"` (`:12`); nothing else.
6. **`crates/core/src/self_update.rs` (surface; plan §3.1, §4.1).** Module doc `//! Self-update rules (Phase 11 D8): URL,
   package-origin and redirect rules, proxy choice, busy and packaging rules, the in-flight registry and action gate,
   codes and sentences, progress events, the view model, timeouts. Tauri-free.` (no host names, Rule 4). Real: the
   constants `CHECK_TIMEOUT` (30 s), `DOWNLOAD_TIMEOUT` (3600 s), `STALL_BOUND` (30 s), `API_HOST_LABELS`, `GITHUB_HOST`,
   `MANAGER_REPO` (PD-27, written as exactly one line `pub const MANAGER_REPO: &str = "rolandzwaga/krate-audio";`, the
   form `manager-release-preflight.js`'s `E_PREFLIGHT_REPO` parses (T011); the value is the origin remote's slug,
   `git remote get-url origin` → `https://github.com/rolandzwaga/krate-audio.git` this session),
   `RELEASE_ASSET_HOST`, `LATEST_SEGMENT`; the types `UrlRefusal`, `SelfUpdateError` (eight variants, derives and
   `#[error]` texts exactly §3.1 / `plan.md:383-397` with the eight fixed sentences of `plan.md:406-415` as `const
   …_SENTENCE: &str` items, one `URL_SENTENCE` for every `Url` reason), `ErrorView` (TA-21), `pub const CODES: [&str; 8]`,
   `SelfUpdateProgress` (`plan.md:420-421`), `UpdateTimeouts` (+ `production()` real: it is a constructor of constants),
   `Packaging`, `HopKind`, `HopRules`, `ManagerUpdateView`, `AvailableView` (with `action`), `UpdateAction` (PD-31,
   plan §4.1), `UpdateFacts` (with `action`); and the TA-9 items:
   `static SELF_UPDATING: Mutex<BTreeSet<Option<PathBuf>>>` (lock poisoning ignored, as `BUSY`), `pub struct
   InFlightGuard { key: Option<PathBuf> }` with `enter(state_dir: Option<&Path>) -> InFlightGuard` (inserts) and `Drop`
   (removes), `pub fn in_flight(state_dir: &Path) -> bool`, `pub struct SelfUpdating` (derives `Clone, Copy, Debug,
   PartialEq, Eq, thiserror::Error`, `#[error("E_INSTALL_SELF_UPDATING: Krate Manager is updating itself. Try again after
   it restarts.")]`), `pub const SELF_UPDATING_CODE: &str = "E_INSTALL_SELF_UPDATING";`, `pub fn action_gate(state_dir:
   &Path) -> Result<(), SelfUpdating>`. Placeholders (`todo!("T003")`, `_` parameters): `SelfUpdateError::{code,
   sentence, view}`, `self_update_http`, `self_install_supported`, `check_update_url`, `check_update_package_url`,
   `check_update_hop`, `hop_kind`, `redirect_decision`, `proxy_choice`, `self_update_busy`, `update_action`, `view`,
   `redacted` (TA-6). Signatures exactly plan §4.1's code block as of revision 4 (in particular
   `check_update_package_url(policy: UrlPolicy, url: &Url, announced: &str)` and `update_action(packaging: Packaging,
   policy: UrlPolicy, download_url: &Url, announced: &str) -> Result<UpdateAction, SelfUpdateError>`). End of file: `#[cfg(test)] #[path = "self_update_tests.rs"] mod tests;`.
7. **Core placeholders and real items outside `self_update` (plan §4.2, TA-8, TA-9).** `crates/core/src/lib.rs`: `pub mod
   self_update;` between `pub mod roots;` and `pub mod settings;` (`:18-19`). `http.rs`: `pub fn route_choice(_config:
   &HttpConfig, _url: &url::Url) -> Result<crate::pac::ProxyChoice, HttpError> { todo!("T004") }`. `context.rs`: `pub enum
   LockRelease { Released, NoLock, StillShared }` (derives `Clone, Copy, Debug, PartialEq, Eq`) and `pub fn
   release_for_restart(_state: &SharedState) -> LockRelease { todo!("T004") }`. `install.rs` (real): `impl ElevationGuard
   { pub fn is_held(&self) -> bool }` (hold ≠ `Free`, `install.rs:594-603`), the two guard seams and
   `GuardHeldForTest<'_>`, `pub fn any_busy(state_dir: &Path) -> bool` (`BUSY.iter().any(|(d, _)| d == state_dir)`,
   `:3841`), `hold_busy_for_test` and `BusyHeldForTest`, and `InstallError::SelfUpdating(#[from] crate::self_update::SelfUpdating)`
   with `#[error(transparent)]` and the `code()` arm (`:207-209`) returning `crate::self_update::SELF_UPDATING_CODE`.
   `download.rs` (real): `pub fn any_in_flight(downloads: &Path) -> bool` (`registry().iter().any(|(d, _)| d ==
   downloads)`, `:649`), `hold_in_flight_for_test` and `InFlightHeldForTest`, `DownloadError::SelfUpdating(#[from]
   crate::self_update::SelfUpdating)` transparent with its `code()` arm (`:442`). **No gate call** (T005's).
8. **`src-tauri` (plan §4.3 surface, §4.4, §4.5 surface, PD-26).** `src/self_update.rs` (new): module doc (no host names);
   real: `pub struct SelfUpdateHttp(pub HttpConfig);`, `#[derive(Default)] pub struct KeptUpdate(Mutex<Option<Update>>);`,
   `pub struct UpdateInputs { pub http: HttpConfig, pub policy: UrlPolicy, pub timeouts: UpdateTimeouts, pub packaging:
   Packaging }`, `pub trait UpdateInstaller<R: Runtime>: Send + Sync { fn install(&self, app: &AppHandle<R>, update:
   &Update, bytes: Vec<u8>) -> Result<(), SelfUpdateError>; }`, `pub struct NativeUpdateInstaller;`; placeholders
   (`todo!("T006")`): `UpdateInputs::production`, `packaging`, `endpoint_of`, `build_updater`, `check`, `prepare_download`,
   `install`, `map_error`, `impl<R: Runtime> UpdateInstaller<R> for NativeUpdateInstaller`; signatures exactly
   `plan.md:561-584` (`HopRefused` is T006's, not here); and `#[cfg(feature = "updater-e2e")] pub mod e2e { #[cfg(feature
   = "dev-feed")] compile_error!("updater-e2e and dev-feed are never combined"); pub fn override_feed(_production_feed:
   String, _production_http: HttpConfig) -> (String, HttpConfig) { todo!("T006") } pub fn start<R: Runtime>(_app:
   &AppHandle<R>) { todo!("T006") } }` (no `env!` consts yet, so the placeholder has no unused item; T006 adds them).
   `src/lib.rs`: `pub mod self_update;` after `mod proxy_env;` (`:12`); after `:69`, `#[cfg(feature = "updater-e2e")]`
   `let (feed_url, http) = self_update::e2e::override_feed(feed_url, http);`; then `let self_update_http =
   self_update::SelfUpdateHttp(http.clone());` immediately before `let state = assemble_context(roots, &feed_url, http,
   SigningKey::release());` (`:70`, PD-9); the builder gains `.plugin(tauri_plugin_updater::Builder::new().build())`
   right after `.plugin(navigation::guard())` (`:88`), `.manage(self_update_http)` and
   `.manage(self_update::KeptUpdate::default())`; in `setup`, `#[cfg(feature = "updater-e2e")]`
   `self_update::e2e::start(app.handle());` before `Ok(())` (`:101`); `generate_handler!` gains
   `commands::manager_update_check, commands::manager_update_install` after `commands::remove_plugin` (`:111`). Exactly two
   `feature = "updater-e2e"` sites in `lib.rs`; the `dev-feed` arms (`:4-5`, `:17-18`, `:48-55`, `:63-69`, `:75-78`) are
   untouched. `src/commands.rs`: the two commands exactly plan §4.4 (real thin wrappers over T006's functions;
   inputs built **only** as `UpdateInputs::production(self_update_http(ctx.as_ref(), &http.0))`, so the file holds
   `UpdateInputs::production(` exactly twice and no `UpdateInputs {` (PD-36, T013's pin reads this file);
   the context cloned out of the read guard before any await, as `commands.rs:69-72`; `state_dir =
   ctx.as_ref().map(|c| c.roots.state_dir())`, `Roots::state_dir() -> &Path`, `roots.rs:195`; the `Err` string
   `serde_json::to_string(&e.view())`, TA-21; progress `|p| { let _ = on_progress.send(p); }`).
9. **Trip-wire guard pins (TA-3)** in `config_guard.rs`, each with its rule comment naming Phase 11; and in the existing
   plant tests, any anchor the new lines made ambiguous is fixed by the smallest anchor change.
10. **The tar rule (TA-4)** in `injection.rs`: a lock parser `lock_packages(lock) -> Vec<LockPkg { name, version, source:
    Option<String>, dependencies: Vec<String> }>` beside `lock_package_names` (`:3138-3146`), the two-part rule replacing the
    lock half of `:3200-3210`, `locks()` with the harness lock, and the test `updater_tar_only_through_the_plugin`: the real
    locks give no finding; plants (each in memory, each reported with `RULE_TAR`): `"tar"` appended to
    `krate-manager-core`'s `dependencies` in `Cargo.lock`; a second `[[package]] name = "tar"` (the existing orphan plant);
    `tauri-plugin-updater` removed from the lock while `tar` stays; `tar = "0.4"` under core's `[dependencies]` (manifest
    half). The existing `verify_confinement_scanner_reports_plants` rows (`:6045-6058`) stay unchanged and green.
11. **Required-test lists (TA-1).** `tests/required-tests.json` `cargo.all` gains, appended in this order:
    `self_update::tests::url_policy`, `self_update::tests::redirect_hops`, `self_update::tests::proxy_choice_matches_core`,
    `self_update::tests::codes_and_sentences`, `self_update::tests::view_from_update`,
    `self_update::tests::endpoint_matches_feed_tool`, `self_update::tests::busy_rule`, `self_update::tests::in_flight_flag`,
    `self_update::tests::timeouts`, `self_update::tests::no_context_defaults`, `self_update::tests::packaging_rule`,
    `self_update::tests::progress_events_serialize`, `context::tests::release_for_restart_frees_the_lock`,
    `install::tests::refuse_while_self_updating`, `updater_config_pinned`, `planted_updater_config_fails_the_guard`,
    `self_update_confinement_holds`, `planted_self_update_confinement_fails`, `updater_tar_only_through_the_plugin`,
    `updater_e2e_cfg_sites_are_counted`, `planted_updater_e2e_cfg_site_fails`, `harness_source_names_no_installer_or_root`,
    `update_inputs_only_from_production`, `planted_update_inputs_fail_the_pin` (24; PD-36, plan §4.13).
    `vitest` gains: `renders the manager update line`, `confirms before updating`, `manager update confirmation names the
    admin prompt on macOS only`, `shows a failed update by code`, `shows no banner when the check fails`, `shows
    downloading then installing progress`, `offers the new .deb instead of an update on a system-package install`, `a
    failed .deb release-page open shows its code` (PD-31), `disables plugin actions while the manager updates itself`,
    `re-enables plugin actions after a failed manager update`, `checks for a manager update after plugin_status is
    rejected`, `checks for a manager update after plugin_status succeeds`, `does not check for a manager update when the
    context is locked` (13). `node`
    gains: `bundle release mode argv`, `bundle release asset table`, `e2e update server serves only its directory`.
    `manager-ci-shape.test.js` `EXPECTED_REQUIRED` (`:605`) mirrors the file exactly.
12. **Verify:** L-RS (every pre-existing test green; nothing calls a placeholder), L-APP, L-DEVFEED, L-MANUAL, L-E2E,
    L-GUARD (the re-locked clippy-guard), `CARGO_TARGET_DIR="$PWD/target" cargo clippy --manifest-path
    tests/fixtures/updater-refusals/Cargo.toml --all-targets --locked -- -D warnings` (the placeholder harness: 0
    warnings; this warms the shared target with tauri's `test` feature for T014; record its wall clock), L-TS (unchanged,
    57 titles), L-NODE (`manager-ci.yml keeps the Phase 4 shape` green with the new `EXPECTED_REQUIRED`), L-VLINT.
    `grep -c 'feature = "updater-e2e"'` → `src-tauri/src/self_update.rs` 1, `src-tauri/src/lib.rs` 2.

**Lane:** app lane (above) + pipeline lane (L-NODE). Notes: the added lock packages, the guard tests that went red in
step 1 and the TA-3 rows that made them green, the harness clippy wall clock.

---

## G2 — Docs, core, shell, UI, scripts, tools, guards, harness, release workflow, evidence [P] (fifteen tasks, disjoint files)

### T002 [P] — Spec amendments, roadmap amendments, keys README (plan §0.2 SI-1 to SI-40; spec FR-093 (a), (b), (c), (e); TA rows marked spec; SI-20's README sentence)

**Files:** `specs/krate-manager-phase11/spec.md` (LF), `specs/Krate-manager-roadmap.md` (CRLF),
`tools/feed-contract/keys/README.md` (LF). No code.

1. Precondition check (as T001 step 0).
2. **Red first.** A throwaway Node script (under `F:/tmp/gate/km11-T002/`) that asserts the target texts below are
   present in the three files; run it, it fails on every row; keep it, it is this task's check.
3. **`spec.md`:** write each plan §0.2 item SI-1 to SI-40 into the FR / SC / D row it names, worded as the plan words
   it, each edit tagged `(plan SI-n)`; SI-19 is withdrawn (note it only); SI-30 to SI-33 encode the plan-precondition
   rulings and also cite `spec.md:1138-1145`; where a later SI supersedes an earlier one's number (SI-23's and SI-30's
   25 lines → SI-35's 27; SI-27's `o/r` rows → SI-40's `MANAGER_REPO`), write the later value only. Then the TA rows
   marked spec (TA-2, TA-4, TA-11, TA-12, TA-13, TA-14, TA-22), tagged `(tasks TA-n)`. Key results the check script
   asserts: D1 names `=2.14.1` and no `zip`; FR-001 names
   `features = ["socks"]` on the `reqwest` line; FR-013 names "one `_` arm", the harness line `map-errors`, and
   `E_SELF_UPDATE_UNSUPPORTED` for a system-package install and for a macOS executable path that needs a symlink
   (SI-21, SI-39); FR-080 names `socks-proxy`, `deb-install`, `rsign-format` and `production-inputs`; FR-082 / FR-084 /
   SC-007 read `harness: 27 ok, 0 not ok` and no `25 ok` remains anywhere in the file; FR-011 / FR-011a name
   `MANAGER_REPO` and `v<announced>` (SI-40); FR-051 / SC-011 name `E_PREFLIGHT_REPO` before `E_PREFLIGHT_TAG_EXISTS`
   (SI-40); FR-023 holds "an FR-015 refusal, made before the update is taken, keeps it" (SI-36); FR-071 / FR-072 /
   SC-015 name the copied `sign updater artifacts` steps and `rsign signature served` (SI-34); FR-090 names the
   per-user directories of `com.krateaudio.krate-manager` in the CI write set (SI-38); SC-021 names `count(//relocate/*)`
   and `evidence/packageinfo-dry-run.xml` (SI-37); FR-094 / SC-024 name the `UpdateInputs::production` body pin
   (SI-35); FR-014 names `action` /
   `UpdateAction`; FR-024 holds "Download the new .deb" and `open_release_page`; FR-073 / FR-083 name `clippy
   updater-e2e`; D9 and FR-084 hold no "stops the build" and name `d9-windows.txt`; FR-041 / SC-009 / SC-014 read seven
   keys (no `linux-x86_64-deb`); FR-052 / FR-055 / FR-091 name the `sign`, `sign-dry` and `main-only` jobs and
   `environment: signing` exactly on `main-only`, `sign`, `publish`; FR-081 (a) names `test_support::temp_roots`; FR-080
   names the child-process form; FR-097 reads "already met (`.gitattributes:29`)". Status line: `DRAFT, revision 4
   (plan §0.2 SI-1 to SI-40 and tasks TA rows encoded, 2026-11-01)`.
4. **Roadmap (FR-093 (a), (b), (c), (e) only; TA-15 leaves (d) to the workflow):** (a) the `release-manager.yml` bullet
   (`specs/Krate-manager-roadmap.md:826-833`) and the universal-build sentence naming `updaterJsonKeepUniversal`
   (`:834-839`) read plan §4.14 (a) (`plan.md:1046-1048`): "`bundle.js --release` builds each leg (no `tauri-action`:
   1.0.0 writes `api.github.com` asset URLs and dropped the `tagName` rewrite, Phase 11 D4); `gh release upload`;
   `tools/gen-manager-latest.js` writes `latest.json` with per-tag github.com URLs and the three darwin keys"; (b) the
   endpoint bullet (`:857-859`): "one endpoint, `https://krateaudio.com/manager/latest.json`; no stable second host (D5)";
   (c) Phase 12's user-performed checks (`:904-916`) gain the bullet "the self-update of a pkg-installed manager on macOS:
   record the admin prompt and that the swapped bundle is then user-owned (Phase 11 D11)"; (e) Repository Layout line 232
   becomes `.github/workflows/release-manager.yml  bundle.js --release matrix, manual dispatch, tag manager/vX.Y.Z,
   tools/gen-manager-latest.js writes latest.json → feed.yml (Phase 11 D4)`, the tool list (`:234-243`) gains
   `tools/gen-manager-latest.js` (`latest.json` with per-tag URLs, every signature verified) and
   `tools/manager-release-preflight.js` (version, CHANGELOG, tag, updater-config and key checks; the release body), and
   the 🆕 row (`:211`) gains the same two tools. Anchor every edit by its text, never by line number; nothing else
   changes (no Dependency Graph hunk).
5. **Keys README §2 (SI-20, TA-17):** replace the sentence at `tools/feed-contract/keys/README.md:47-48` with: "Phase 11
   (the manager's own release) reads the same two secrets in one place only, the `sign` job of `release-manager.yml`:
   `TAURI_SIGNING_PRIVATE_KEY_PASSWORD` decrypts the key with `node tools/minisign.js decrypt-key`, and `rsign` signs
   manager updates with it, as `release.yml` does; no manager build job receives them."
6. **Verify:** the check script exits 0; repo root `node --test tools/feed-contract/tests/workflow-shape.test.js` (the
   keys README test, `workflow-shape.test.js:583-626`, green); `git diff --stat` lists only the three files.

**Lane:** pipeline lane (`workflow-shape.test.js`). Notes: the SI rows written and where (FR / SC id per SI).

### T003 [P] — `core::self_update`: the rules, the view, the busy rule, timeouts, and its twelve tests (spec FR-010 to FR-017, FR-026, FR-027; SC-001 to SC-005, SC-023 / SC-024 / SC-025 core rows; plan §3.1, §4.1, §7.1; PD-4 to PD-9, PD-24, PD-25, PD-27, PD-29, PD-31; SI-16, SI-24, SI-26, SI-27, SI-40; TA-6, TA-7, TA-9, TA-16, TA-21)

Target slot `F:/tmp/km11-target/core` (Rule 10). Logs `F:/tmp/gate/km11-T003/`.
**Files:** `crates/core/src/self_update.rs` (bodies only; never change a T001 signature), `crates/core/src/self_update_tests.rs`.

**Red first:** write all twelve tests (below) in `self_update_tests.rs` (`use super::*;`), run L-OWN `self_update::`:
each fails on `not yet implemented: T003` (or, for `in_flight_flag` and the seam rows of `busy_rule`, passes at once:
TA-9, say so), and `proxy_choice_matches_core` / `no_context_defaults` additionally on `T004` (expected red (a)).

Tests (names exact; assertions from plan §7.1 `plan.md:1139-1163`, binding):
- `url_policy` (SC-001, SI-27, SI-40; plan §7.1 SC-001 row as of revision 4, binding): table rows `(policy, url) → Ok | Url { reason }` over `check_update_url`:
  `HttpsOnly` accepts `https://krateaudio.com/manager/latest.json` and `https://github.com/o/r/releases/download/manager/v1.2.3/x`;
  refuses `http://krateaudio.com/x` (`Policy`), `https://api.github.com/repos/o/r/releases/assets/1` (`ApiHost`),
  `https://API.GITHUB.COM./x` (`ApiHost`: case and trailing dot), `https://github.com/o/r/releases/latest/download/x`
  (`LatestPath`), `https://u:p@github.com/x` (`Policy`); `LoopbackOnly` accepts `http://127.0.0.1:9/x`, refuses
  `http://example.com/x` (`Policy`), `https://api.github.com/x` (`ApiHost`, so the order is pinned),
  `http://127.0.0.1:9/o/r/releases/latest/download/x` (`LatestPath`). Over `check_update_package_url(policy, url,
  "0.2.0")` (`P` below = `MANAGER_REPO`, the test builds every URL with `format!` from the constant): `HttpsOnly`
  `https://github.com/{P}/releases/download/manager/v0.2.0/x` → `Ok`; `LoopbackOnly`
  `http://127.0.0.1:9/{P}/releases/download/manager/v0.2.0/x` → `Ok`; → `Host` each: `https://example.org/pkg`,
  `https://github.com/{P}/archive/x`, the good URL with `?a=1` and with `#f`, with `v0.2` (not semver core), with an
  extra segment; `https://github.com/attacker/x/releases/download/manager/v0.2.0/a` under `HttpsOnly` and its
  `http://127.0.0.1:9/attacker/x/…` twin under `LoopbackOnly`; the former `o/r` form
  `https://github.com/o/r/releases/download/manager/v0.2.0/x`; the owner differing only in case
  (`RolandZwaga/krate-audio`); `…/manager/v0.1.9/x` and `…/manager/v9.9.9/x` (tag ≠ announced); the good URL with
  `announced = "0.2.0-rc.1"`; `LoopbackOnly` `http://127.0.0.1:9/any/path` (no per-tag path: `Host` since revision 4).
  Plus `MANAGER_REPO` matches `^[A-Za-z0-9-]+/[A-Za-z0-9._-]+$` (the `REPO_RE` shape of `tools/feed-publish.js:66`;
  checked with a hand-written char test, no regex crate). Every row asserts code **and** reason.
- `redirect_hops` (SC-001, FR-011a, PD-5): `HopRules { policy: HttpsOnly, endpoint_host: "update.example".into() }`;
  `check_update_hop` package hop `github.com` → `release-assets.githubusercontent.com` (https) `Ok`; `github.com` →
  `github.com` `Ok`; any → `http://…` `Policy`; any → `api.github.com` `ApiHost`; package hop to `example.org` `Host`;
  package hop from `example.org` to the asset host `Host`; endpoint hop `update.example` → `update.example` `Ok`, →
  `other.example` `Host`. `redirect_decision`: `previous` of 6 URLs → `Err(Policy)` whatever `to` is; of 5 → the hop rule;
  a chain starting at the endpoint judged by the endpoint host rule; `hop_kind(endpoint, endpoint)` → `Endpoint`,
  `hop_kind(other, endpoint)` → `Package`. `LoopbackOnly` rules: any loopback hop `Ok`.
- `proxy_choice_matches_core` (SC-002, SI-16, SI-26): two `FixtureServer`s (target, proxy); five configs: static proxy;
  static proxy with `no_proxy = ["127.0.0.1"]`; no proxy; PAC (reuse the Phase 5 fixture PAC and host exactly as
  `http_tests.rs` does: read it, cite the lines); static proxy with scheme `ftp`. For each, the request goes through
  `http::get` and the server whose `requests()` grew is the one `proxy_choice` names (`Proxy(p)` ↔ the proxy server,
  `Direct` ↔ the target); the `ftp` row: `proxy_choice` → `Err(SelfUpdateError::Network { .. })`, `http::get` errs, both
  servers record zero requests. Threshold 5 / 5.
- `codes_and_sentences` (SC-003): `CODES` has 8 distinct entries, each starts `E_SELF_UPDATE_`; one value of every
  variant maps by `code()` into `CODES` (all 8 reached); `sentence()` equals `plan.md:406-415` exactly per code;
  `to_string()` is `"<code>: <sentence>"`; `SelfUpdating.to_string()` is exactly the FR-018 text and
  `SELF_UPDATING_CODE == "E_INSTALL_SELF_UPDATING"`; `serde_json::to_value(Url { reason: LatestPath }.view())` is
  `{"code":"E_SELF_UPDATE_URL","sentence":"<URL sentence>","reason":"latest_path"}` and `Busy.view()` has no `reason` or
  `detail` key; `redacted("https://u:p@h/x")` holds no `u:p`.
- `view_from_update` (SC-003, FR-014, TA-7, PD-31): `view(Os::Windows | Macos | Linux, "0.1.0", Some(UpdateFacts {
  version: "0.2.0", date_rfc3339: Some("2026-11-01T00:00:00Z".into()), body: Some("# New\n\n<b>bold</b> text\n- a"),
  action: UpdateAction::InstallAndRestart }), None)` → `os` the matching `OsName`, `current` "0.1.0",
  `available.version` "0.2.0", `date` as given, `notes == notes::blocks(body)`, `action` as given, and the serialized
  notes hold no `<`; serialized `available.action` is `{"kind":"install_and_restart"}`, and for `OpenReleasePage { url }`
  `{"kind":"open_release_page","url":"<url>"}`; `None` update → `available: None`; an error → `error: Some(e.view())`.
- `endpoint_matches_feed_tool` (SC-004): reads `<CARGO_MANIFEST_DIR>/../../src-tauri/tauri.conf.json` and
  `<CARGO_MANIFEST_DIR>/../../../../tools/feed-publish.js` (via `env!("CARGO_MANIFEST_DIR")`, never `std::env::var`);
  `plugins.updater.endpoints == [MANAGER_LATEST_URL]` where the URL is the `'…'` literal after `const MANAGER_LATEST_URL =`
  (`tools/feed-publish.js:64`); the `pubkey` base64-decodes to exactly `verify::RELEASE_KEY_TEXT` (`verify.rs:15-16`); the
  key file's bytes hold no `\r`; a check function the test defines reports an in-memory CRLF copy of the key and passes
  the real bytes.
- `busy_rule` (SC-005, PD-8, TA-8): own `FakeElevation::refusing()` per row and an assembled temp context (build it as
  `context_tests.rs:132` does: `assemble_context(Ok(roots), FEED, http(), kat_signing_key())`, read and cite; take
  `context.as_ref().ok()`): guard `hold_for_test` → busy (with and without a context); `hold_stale_for_test` → busy;
  `install::hold_busy_for_test(roots.state_dir(), "alpha")` → busy; `download::hold_in_flight_for_test(&roots.staging_dir().join(download::DOWNLOADS_DIR))`
  → busy; none → not busy; `None` context and a free guard → not busy; each seam dropped → not busy again.
- `in_flight_flag` (SC-023 core, FR-017): a fresh temp state dir reads `!in_flight`; `InFlightGuard::enter(Some(dir))` →
  `in_flight(dir)` and `action_gate(dir) == Err(SelfUpdating)`; a second temp dir is unaffected; `enter(None)` affects no
  dir; dropping clears.
- `timeouts` (SC-024): the three constants equal 30 s, 3600 s, 30 s and `UpdateTimeouts::production() ==
  UpdateTimeouts { check: CHECK_TIMEOUT, download: DOWNLOAD_TIMEOUT, stall: STALL_BOUND }`.
- `no_context_defaults` (SC-025, PD-9): a `fallback` built as `src-tauri/src/lib.rs:64-67` builds it
  (`HttpConfig::production().with_proxy(Some(<fixture proxy cfg>)).with_pac(None).with_pac_host(None)`):
  `self_update_http(None, &fallback) == fallback` and `!= HttpConfig::production()`; with an assembled temp context →
  `ctx.http`; `self_update_busy(&FakeElevation::refusing(), None)` is false; `context::release_for_restart` of a
  `SharedState` whose `AppState { context: Err(..), settings_error: None, lock: None }` → `LockRelease::NoLock`.
- `packaging_rule` (SC-003, PD-24, PD-31, SI-40): `SystemPackage` → `Err(Unsupported)` with the §3.1 sentence;
  `SelfContained` → `Ok`. Then `update_action(packaging, policy, url, announced)` rows (plan §7.1 SC-003 as of revision
  4, `announced = "0.2.0"` unless stated, `P` = `MANAGER_REPO`): `SelfContained`, `HttpsOnly`, the good per-tag URL →
  `Ok(InstallAndRestart)`; `SelfContained` with a foreign-repo URL and with a wrong-tag URL → `Err(Url { Host })`
  (revision 4: no banner for a URL PD-27 refuses, on a self-contained install too); `SystemPackage`, `HttpsOnly`,
  `https://github.com/{P}/releases/download/manager/v0.2.0/x` → `Ok(OpenReleasePage { url })` with `url ==
  format!("https://github.com/{P}/releases/tag/manager/v0.2.0")` and `status::release_page_url(&url) ==
  Some(url.clone())`; `SystemPackage`, `LoopbackOnly`, `http://127.0.0.1:9/{P}/releases/download/manager/v0.2.0/x` →
  the same `github.com` page; `SystemPackage` with `https://github.com/attacker/x/releases/download/manager/v9.9.9/a`
  and `announced = "9.9.9"` → `Err(Url { Host })` (no page is ever built from a foreign repo); `SystemPackage` with the
  project's URL for `v0.1.9` and `announced = "0.2.0"` → `Err(Url { Host })` (downgrade by URL); `SystemPackage` with
  `https://example.org/pkg`, with `https://github.com/{P}/archive/x`, and with `http://127.0.0.1:9/x` →
  `Err(Url { Host })`. Every row asserts the variant and, for `Err`, the reason.
- `progress_events_serialize` (PD-25): `{"event":"downloading"}`, `{"event":"installing"}`.

**Implement** plan §4.1 (`plan.md:501-536`) exactly: `check_update_url` in its three-step order, host compared on
`host_str()` lower-cased with one trailing `.` stripped, the API host by labels (PD-4); `check_update_package_url` (PD-27
as of revision 4, plan §4.1 "`check_update_package_url`": `check_update_url` first, then under **both** policies no
query / fragment and exactly 7 non-empty segments `[owner, repo, "releases", "download", "manager", "v<x.y.z>", name]`
with `owner/repo == MANAGER_REPO` (exact, case-sensitive) and the tag `== format!("v{announced}")`, and under `HttpsOnly`
the host `== GITHUB_HOST`; every failure `Url { Host }`);
`check_update_hop`; `hop_kind`; `redirect_decision` (TA-16); `proxy_choice` = `crate::http::route_choice` mapped to
`Network { detail }` (no request on `Err`); `self_install_supported`; `self_update_http`; `self_update_busy`
(`elevation.guard().is_held()` ∨ `install::any_busy(c.roots.state_dir())` ∨ `download::any_in_flight(&c.roots.staging_dir().join(download::DOWNLOADS_DIR))`);
`update_action` (plan §4.1 as of revision 4: `check_update_package_url(policy, download_url, announced)?` first for both
packagings; `SelfContained` → `InstallAndRestart`; `SystemPackage` → the page `format!("https://{}/{}/releases/tag/manager/v{}",
status::RELEASE_PAGE_HOST, MANAGER_REPO, announced)`, built from the constant and the announced version, never from the
URL's segments, and kept only when `status::release_page_url` returns it unchanged; `status.rs` is not edited); `view`; `code` / `sentence` / `view`; `redacted`. No literal host spelled in code or comments (Rule 4): the page
host comes from `status::RELEASE_PAGE_HOST`, the package host from `GITHUB_HOST`.

**Lane:** L-OWN, then L-RS once T004 has landed (both expected reds gone). App lane at the gate (WSL step included).

### T004 [P] — `http::route_choice` / `pac_choices` and `context::release_for_restart` (spec FR-012 / D17 reuse, FR-016; SC-006; plan §4.2, PD-6, PD-10; TA-10)

Target slot `F:/tmp/km11-target/core`. **Files:** `crates/core/src/http.rs`, `http_tests.rs`, `context.rs` (CRLF),
`context_tests.rs` (CRLF).

**Red first:** `http::tests::route_choice_static_direct_and_refused` (TA-10; in `http_tests.rs`, with `FixtureServer` URLs
only, no literal `127.0.0.1:<port>`): static proxy → `Ok(ProxyChoice::Proxy(cfg))`; a host on the static `no_proxy` →
`Ok(Direct)`; no proxy → `Ok(Direct)`; an `ftp://` static proxy → `Err(_)` (the `check_proxy` refusal in `prepare`,
`http.rs:949-966`). `context::tests::release_for_restart_frees_the_lock` (SC-006, plan §7.1 row): `fresh_temp_roots`,
`assemble_context` as `context_tests.rs:132`, `StateLock::acquire` on the same roots → `Held`
(`state_lock.rs:49-71`), `release_for_restart(&state)` → `Released`, `acquire` → `Ok`; a second call →
`NoLock`; a state whose lock `Arc` is cloned first → `StillShared`. Both fail on `T004`.

**Implement:** plan §4.2's `http.rs` bullet (`plan.md:540-545`) exactly: split `pac_route` (`http.rs:904-946`) into a
private `pac_choices(…) -> Result<Vec<ProxyChoice>, HttpError>` and the unchanged ureq conversion over it, add
`route_choice` (`prepare`, then `open_pac` when `config.pac` is set, then the hop's first choice from `static_route` /
`pac_choices`), and keep `get`'s route closure (`:976-982`) the same decision. Every existing `http_tests.rs` PAC / proxy
case passes unchanged (the regression net: no expected value edited). `context.rs`: `release_for_restart` takes the write
guard (poison ignored), `take()`s `lock`, `Arc::into_inner` → `Released` (drop closes the file, `state_lock.rs:38-43`), a
`None` lock → `NoLock`, an `Arc` still shared → put it back and `StillShared` (plan PD-10).

**Lane:** L-OWN (`http::`, `context::`), L-RS; the gate's WSL step compiles the non-Windows PAC branch this refactors
(record its `test result:` lines); `cfg(not(windows))` `pac_choices` code paths are read twice against `http.rs:904-946`.

### T005 [P] — The four action gates and `refuse_while_self_updating` (spec FR-018; SC-023 core row; plan §4.1 "action_gate", §4.2, §7.1 SC-023 row; PD-7; SI-15)

Target slot `F:/tmp/km11-target/core`. **Files:** `crates/core/src/install.rs`, `install_tests.rs`, `download.rs`.

**Red first:** `install::tests::refuse_while_self_updating` (plan §7.1 SC-023 row, `plan.md:1161`, binding): rows
`download_plugin` (`download::for_command`), `install_plugin` (`install::for_command`), `remove_plugin`
(`install::remove_for_command`), each with `InFlightGuard::enter(Some(state_dir))` held on a temp context built with the
existing `install::tests` builders (read and cite them), asserting `Err` whose `code()` is `E_INSTALL_SELF_UPDATING` and
whose `to_string()` is the FR-018 sentence, zero fixture-server requests, zero recorded `InstallIo` mutations, zero
`FakeElevation` launches, `!install::any_busy(state_dir)`; with the flag clear each proceeds past the gate (its first
later step runs, e.g. a fixture request recorded). Row **elevated batch**: the flag clear at `install::for_command`'s
start, set mid-flight by a wrapping `InstallIo` whose `canonicalize` of the staging dir (the first `InstallIo` call of
the elevated path, `install.rs:1130`, before `elevate` at `:1157`; read and cite) enters and keeps an `InFlightGuard`;
asserts `E_INSTALL_SELF_UPDATING` from `elevate`'s gate, zero launches, no file under `state_dir.join(job::JOBS_DIR)`,
`!any_busy(state_dir)` after return. It fails because no gate exists. Text hygiene: Rule 4 (c), (d).

**Implement:** `crate::self_update::action_gate(<state_dir>)?` as the **first statement** of `download::for_command`
(before `command_request`, `download.rs:1611`), `install::for_command` (before `install_command`, `install.rs:3881`),
`install::remove_for_command` (before `command_remove`, `:4080`), and in `elevate` before `elevation.helper()` (`:1298`,
so before `guard().enter`, `:1299`). The `state_dir` is `env.ctx.roots.state_dir()` / `ctx.roots.state_dir()`. No `Busy`
entry is made before a gate. Nothing else in the three files changes (FR-092).

**Lane:** L-OWN (`install::tests::refuse_while_self_updating`, `install::`, `download::`), L-RS.

### T006 [P] — `src-tauri/src/self_update.rs`: adapter, redirect policy, kept slot, error map, installer, the `updater-e2e` module (spec FR-011 / FR-011a application, FR-013 map, FR-015 / FR-019 order, FR-016 call, FR-022, FR-023, FR-027, FR-070; plan §3.2, §4.3, §4.5; PD-3, PD-5, PD-13, PD-14, PD-24, PD-26 to PD-29, PD-31, PD-36, PD-37; SI-1, SI-24, SI-25, SI-35, SI-36, SI-39; TA-6, TA-19, TA-21, TA-27)

Target: shared, `CARGO_BUILD_JOBS=16`. **Files:** `src-tauri/src/self_update.rs` only (bodies; T001's signatures fixed).

**Red first:** `src-tauri` has no test target (Rule 7); its proof is the harness (T014) and the guards (T013). Before
writing bodies, run L-APP and record that it is green on placeholders; the red for this file is T014's scenario lines
reading `not yet implemented: T006` (expected red (c)) and T013's timeout-pin rows (expected red (b)). Cite in the notes
the updater source lines (`~/.cargo/registry/src/*/tauri-plugin-updater-2.14.1/src/{updater.rs,error.rs,lib.rs}`) for
every item used.

**Implement** plan §4.3 (`plan.md:561-634`) and §3.2 (`plan.md:426-447`) exactly:
- `UpdateInputs::production(http)` whose body is **exactly** `Self { http, policy: UrlPolicy::HttpsOnly, timeouts:
  UpdateTimeouts::production(), packaging: packaging() }` (PD-36; T013 pins it whitespace-normalised, and the harness
  line `production-inputs` reads it); `packaging()` from `tauri::utils::platform::bundle_type()` (`Some(Deb | Rpm)` →
  `SystemPackage`, else `SelfContained`; PD-24).
- `endpoint_of` (`config.plugins` → `updater.endpoints[0]`, parse error → `Manifest`); `build_updater` (`check_update_url`
  first; `.timeout(inputs.timeouts.check)`; `proxy_choice(&inputs.http, &endpoint)?` → `.proxy(url)` / `.no_proxy()`;
  `.configure_client(move |b| b.read_timeout(inputs.timeouts.stall).redirect(<policy>))` — the closure captures the
  values it needs by move, and the body of `build_updater` must hold the token sequences `.timeout(inputs.timeouts.check)`
  and `.read_timeout(inputs.timeouts.stall)` (T013's pins); `.build()` mapped). The redirect policy is
  `reqwest::redirect::Policy::custom(move |a| match redirect_decision(&rules, &endpoint, a.previous(), a.url()) { Ok(())
  => a.follow(), Err(r) => a.error(HopRefused(r)) })` with `#[derive(Debug)] struct HopRefused(UrlRefusal)` implementing
  `Display` and `std::error::Error`. No `version_comparator`, no `installer_args`, no `restart_after_install`; the word
  "retry" appears nowhere in the file, comments included.
- `check` (on `Ok(Some(u))`, `update_action(inputs.packaging, inputs.policy, &u.download_url, &u.version)` first (PD-31,
  PD-27; `u.version` is the updater's `String` field, plan §1.2 `Update` public fields): `Ok(a)` →
  stores `u` in `KeptUpdate` via `app.state::<KeptUpdate>()` and the view's `action` is `a`; `Err(e)` → clears the slot,
  view error `e`; every other error a view error too; never panics);
  `prepare_download` (`check_update_package_url(inputs.policy, &update.download_url, &update.version)` →
  `update.timeout = Some(inputs.timeouts.download)` — the **only** assignment to `update.timeout` in the file — → proxy
  fields, PD-29); `install` in the steps of plan §4.3 "`install`" as of revision 4: (1) `self_install_supported(inputs.packaging)?`;
  (1b) `#[cfg(target_os = "macos")] tauri::process::current_binary(&app.env()).map_err(|_| SelfUpdateError::Unsupported)?;`
  (PD-37, SI-39: `current_binary(_env: &Env) -> std::io::Result<PathBuf>`, `tauri-2.12.5/src/process.rs:48-56`, read
  this session; it must be the file's only `current_binary(` and come before `.take()`, T013's pin; no other
  `cfg(target_os = "macos")` code is added, so non-macOS builds have no unused import); (2) busy → `Busy`, the slot
  untouched (PD-28, SI-36); (3) `kept.take()` → `None` if empty; (4) `InFlightGuard::enter(state_dir)`; (5)
  `Downloading`; (6) prepare; (7) `download(|_, _| {}, || {})`; (8) busy again; (9) `Installing`; (10) the installer.
- `map_error`: one `match` naming all 32 variants of `plan.md:288` / `:313` grouped as §3.2, plus one `_` arm (PD-3; the
  enum is `#[non_exhaustive]`); `Reqwest(e)` walks `std::error::Error::source()` and downcasts to `HopRefused`; every
  `detail` through `krate_manager_core::self_update::redacted` (TA-6).
- `NativeUpdateInstaller::install`: `update.install(bytes).map_err(|e| map_error(&e))?;` then `#[cfg(windows)]` return
  `Ok(())` (never reached in practice) and `#[cfg(not(windows))]` `release_for_restart(&app.state::<SharedState>())` (log a
  `StillShared` as `E_SELF_UPDATE_LOCK_SHARED`) then `app.restart()` (`-> !`), structured so neither cfg has unreachable
  code. This impl is the only place in `src-tauri/src` holding `.install(`, `.restart(`, `download_and_install` (none).
- `e2e` module, plan §4.5: `const MARKER: &str = "krate-e2e-marker-v1";`, the three `env!` consts,
  `override_feed` (drops both inputs, returns `(ENDPOINT_FEED.to_owned(), HttpConfig::loopback(http::CONNECT_TIMEOUT,
  http::TOTAL_TIMEOUT))`, TA-27), `start` (marker `launched-<package_info().version>` with content `MARKER`
  when `SharedState`'s lock is `Some`; when the version equals `OLD_VERSION`, `tauri::async_runtime::spawn` → `check` with
  exactly `UpdateInputs { policy: UrlPolicy::LoopbackOnly, ..UpdateInputs::production(<the same loopback config>) }`
  (PD-36: the file's only `UpdateInputs {` literal, so the e2e build runs production's timeouts and `packaging()`) → when
  `available` is set, whatever its `action`, `install(…, &NativeUpdateInstaller, …)`; any error → `error-<code>`;
  `available: None` → `error-E_SELF_UPDATE_NONE`; `create_dir_all` first). Logging through `log::` only, no `eprintln!`
  (TA-19). `UrlPolicy::LoopbackOnly` and `HttpConfig::loopback` occur in this file only inside this module (TA-3 homes
  them in this file; PD-36's pin, T013).
- No host name in code or comments outside data the core supplies (Rule 4 (b)).

**Lane:** L-APP, L-DEVFEED, L-E2E, L-RS (the guards over this file; T013's rows may still be landing). Notes: the updater
source lines used; whether every one of the 32 variants matched by name compiled (it must).

### T007 [P] — The UI: update line, confirmation, progress, failures, mount order, disabled actions (spec FR-024 to FR-026; SC-003, SC-023, SC-025 vitest rows; plan §4.6; PD-18, PD-25, PD-31; SI-6, SI-22, SI-31; TA-23)

**Files:** `src/manager_update.ts`, `src/manager_update.test.ts` (new, LF), `src/main.ts` (CRLF), `src/main.test.ts` (CRLF),
`src/status.ts`, `src/status.test.ts`.

**Red first:** write the thirteen titles of TA-1 (exact): in `src/manager_update.test.ts` the eight SC-003 titles (the
six of revision 1 plus `offers the new .deb instead of an update on a system-package install` and `a failed .deb
release-page open shows its code`, PD-31); in
`src/status.test.ts` `re-enables plugin actions after a failed manager update` (the unit case of
`setActionsEnabled(false)` then `(true)` on a mounted section, each button's `disabled` read after each call); in
`src/main.test.ts` `disables plugin actions while the manager updates itself` (plan §4.6 revision 4: moved here from
`status.test.ts` so it drives the real `main.ts` wiring, FR-025: `renderShell` with a mocked `invoke` whose
`plugin_status` returns a view with one card that has an action button, `manager_update_check` an `install_and_restart`
view, `manager_update_install` a promise the test controls; click "Update and restart", confirm; every card action
button and "Check now" are `disabled`; reject the install; all enabled again — a `main.ts` that passes a no-op or another
object as `actions` is red), `checks for a manager update after plugin_status is rejected`, `checks for a
manager update after plugin_status succeeds`, `does not check for a manager update when the context is locked`; and
update the existing order tests (`main.test.ts:127-165`, "check now …") so `manager_update_check` follows the first
`plugin_status` and every `check_now` in the recorded call order. Assertions: plan §4.6 (`plan.md:701-729`) and §7.1 SC-003
/ SC-023 / SC-025 rows, binding: exact FR-024 texts (`Krate Manager {version} is available.`, `Update and restart`, the
confirmation sentence, `macOS may ask for your administrator password.` present only for `os: "macos"` and absent for
`windows` and `linux`, `Downloading and verifying…`, `Installing… Krate Manager will restart.`, `Could not check for a
Krate Manager update ({code}).` muted with no banner); a failure line starting with its code; the fake channel drives
`{event:"downloading"}` then `{event:"installing"}` and the texts appear in that order, never back; a rejection after
`installing` shows the failure line and re-enables the actions; a message failing the strict parser changes nothing;
notes rendered through `renderBlocks` (`src/notes.ts:45`), no `<` injected. The two PD-31 titles (plan §4.6 "The action
decides the button", binding): a view with `os: "linux"` and `action: { kind: "open_release_page", url }` shows the
button "Download the new .deb", no "Update and restart" text anywhere in the line, a click records exactly
`["open_release_page", { url }]` after the check call and never `manager_update_install`, `setEnabled` is never called;
`open_release_page` rejecting `"E_OPEN_REJECTED"` shows "Could not open the release page: E_OPEN_REJECTED" after the
button and leaves it enabled. The test's `url` is an opaque token (`release-page-0.2.0`): the UI passes it through
unvalidated (the URL rule is core's, PD-31), and Rule 4 (e) keeps `http://` / `https://` out of `src/`, tests included.
Run `npm test`: they fail (module missing / behaviour absent).

**Implement:** `src/manager_update.ts` exactly the plan §4.6 surface (`ManagerUpdateView` with `os`, `ErrorView`,
`toManagerUpdateView` strict like `toStatusView`, `src/view.ts:311`; `createUpdateLine(invoke, actions, makeChannel?)`
using `ChannelMaker` from `status.ts:145`; `UPDATE_TEXTS`); the confirmation is an in-page two-button dialog, never
`window.confirm`; `invoke("manager_update_install", { onProgress: makeChannel(onMessage) })`. `status.ts`:
`mountStatus` gains an `afterSettle` callback called once its first `plugin_status` settles (success or rejection,
`status.ts:576-587`); `createStatusSection`'s `checkNow` (`:551-565`) calls it again after `check_now` settles;
`StatusSection` gains `setActionsEnabled(enabled: boolean)` disabling every card action button and "Check now".
`main.ts` (`:4-25`): `startup_error` → (Locked: notice, return, no check) → `app_version` → `mountStatus(…, () =>
updateLine.check())`, the update line appended in the header. `status.ts`: `releaseButton` exported with an optional
`label` (TA-23; default `STATUS_TEXTS.openReleasePage`, every existing call unchanged); `manager_update.ts` picks the
button from `available.action` only (`install_and_restart` → "Update and restart" and its confirmation; `open_release_page`
→ `releaseButton(action.url, invoke, UPDATE_TEXTS.downloadDeb)`, no confirmation, no `setEnabled(false)`), and
`toManagerUpdateView` rejects any other `action` shape. No URL, no `innerHTML` family, no
`@ts-ignore` (Rule 4 (e)); `main.test.ts` / `status.test.ts` match no FR088 pattern (Rule 4 (d)).

**Lane:** L-TS (70 titles, each new title passing once); `cargo test -p krate-manager-core --locked --test config_guard`
(the frontend scan over `src/`).

### T008 [P] — `scripts/bundle.js --release` (spec FR-030 to FR-033; SC-008; plan §4.8; PD-15, PD-20, PD-22; SI-17, SI-21)

**Files:** `scripts/bundle.js`, `scripts/bundle.test.js` (both LF, ESM like today).

**Red first:** in `bundle.test.js` add two top-level tests (exact titles): `bundle release mode argv` — `parseArgs`
(no args → `{ release: false }`; `['--release']`, `['--release', '--target', 'universal-apple-darwin']` on `darwin`,
`['--release', '--bundles', 'appimage,deb']` on `linux`, `['--release', '--features', 'updater-e2e']`; refusals throw a
usage error with exit code 2: an unknown argument, `--target` on `win32` / `linux`, `--target x86_64-apple-darwin`,
`--bundles` on `darwin` / `win32`, `--features other`, `--target` without `--release`); `releaseTauriArgs` per plan
§4.8 (`plan.md:869-876`): Windows `['build', '--config', 'src-tauri/tauri.helper.conf.json', '--config',
'src-tauri/tauri.release.conf.json']`, macOS the same plus `'--target', 'universal-apple-darwin'`, Linux `['build',
'--config', 'src-tauri/tauri.release.conf.json', '--bundles', 'appimage,deb']`, then each extra config as `'--config',
<file>`, then `'--features', 'updater-e2e'` when set; `extraConfigs({})` → `[]`, a list split on `path.delimiter`, a
missing file throws; `universalSidecars()` → the three names; `tauriArgs` unchanged (the existing test stays). `bundle
release asset table` — `assetTable(platform, '0.2.0', opts)` rows `{ dir, glob, name }` per PD-15 (`plan.md:58`) with the
D6 names `Krate-Manager-0.2.0-Windows-x64-setup.exe` (+ `.sig`), `Krate-Manager-0.2.0-macOS-universal.app.tar.gz` (+
`.sig`), `Krate-Manager-0.2.0-Linux-x86_64.AppImage` (+ `.sig`), `Krate-Manager-0.2.0-Linux-amd64.deb` and **no
`.deb.sig` row** (SI-21); `resolveAsset(listing, glob)`: zero matches → throws `E_BUNDLE_ASSET <glob> matched 0`, two →
`matched 2`, one → that file name. Both fail (functions not exported).

**Implement** plan §4.8 exactly; `main()` with no argument keeps today's path byte for byte (`bundle.js:84-103`); the
helper is built per Apple target with `cargo build -p krate-manager-helper --release --locked --target <t>` and the
universal one made with `lipo -create -output`; `copyAssets` to `target/release-assets/` through `resolveAsset`. The test
never spawns a process and never runs `main`.

**Lane:** L-NAPP (`bundle sidecar name per host tuple`, `bundle release mode argv`, `bundle release asset table` each `ok`).

### T009 [P] — `scripts/e2e-update-server.js` (spec FR-071 server; plan §4.9 last bullet)

**Files:** `scripts/e2e-update-server.js`, `scripts/e2e-update-server.test.js` (new, LF, ESM like `bundle.js`).

**Red first:** top-level test `e2e update server serves only its directory`: the exported `startServer({ dir, log })`
(in-process; binds `127.0.0.1:0`; returns `{ port, close }`) over a temp dir holding `latest.json`, `versions.json`,
`pkg.bin`: `GET /latest.json` 200 with the file's bytes; `GET /versions.json` 200; `GET /o/r/releases/download/manager/v0.1.1/pkg.bin`
200 with `pkg.bin`'s bytes; 404 for `/o/r/releases/download/manager/v0.1.1/../latest.json`, a percent-encoded `..`,
`/sub/pkg.bin` with a subdir present, an unknown name, `/`; a `POST` 404; the server's address is `127.0.0.1`; each
request appended to the log as `<method> <path> <status> <bytes>`; `parseArgs(['--host', '0.0.0.0', …])` throws exit 2.
Client requests use `node:http` against the returned port only. Fails (module missing).

**Implement** plan §4.9's bullet (`plan.md:916-920`): CLI `--dir --port-file --log`, writes the port file after
`listen`, serves only files directly in `--dir`, never follows a symlink out of it.

**Lane:** L-NAPP (`e2e update server serves only its directory` `ok`).

### T010 [P] — `tools/gen-manager-latest.js` (spec FR-040 to FR-042, FR-044 copy; SC-009; plan §4.9; PD-16, PD-21; SI-18, SI-19, SI-21; TA-11)

**Files:** `tools/gen-manager-latest.js`, `tools/feed-contract/tests/gen-manager-latest.test.js` (new, CJS, LF).

**Red first:** the test file's first statement is `require('./helpers/write-guard.js');` (`hygiene.test.js:153-162`);
no `child_process`, no destructured `fs` require (`hygiene.test.js:65-79`); two top-level tests (exact titles), every
other check a `t.test` subtest (the TAP plan counts top-level tests only):
- `gen-manager-latest writes per-tag latest.json`: a temp assets dir with the three updater assets (random bytes) and
  their `.sig`s made with `tools/minisign.js` `generateKeyPair` / `signBuffer` and trusted comment
  `timestamp:1\tfile:<name>\tversion:0.2.0`, `.sig` = base64 of the minisig text (Tauri's form); plus the `.deb` and the
  `.pkg`; a notes file holding a newest-version section; `main([...], io)` exit 0; the output's `platforms` keys are
  exactly the seven of SI-21 in bytewise order; each `url` is `https://github.com/o/r/releases/download/manager/v0.2.0/<D6 name>`;
  each `signature` equals its `.sig` text trimmed of one trailing LF; `notes` equals the notes file; a second run is
  byte-identical; `--check-only` writes nothing; `--platform-subset linux` with only Linux assets → exit 0;
  `--repo-url-base http://127.0.0.1:7 --only windows` writes `http://127.0.0.1:7/o/r/releases/download/manager/v0.2.0/…`
  and only the two Windows keys; the form: shebang, `usage: node tools/gen-manager-latest.js` on exit 2.
- `gen-manager-latest refuses what it cannot verify`: a one-byte tamper, another key, `version:0.1.9`, no `version:`
  field, a `.sig` that is not base64 → `E_LATEST_SIGNATURE` naming the asset; a missing `.sig`, an extra file, a `.deb.sig`
  → `E_LATEST_ASSETS` (TA-11); `--only` without `--repo-url-base`, `--repo-url-base https://x`, `--platform-subset`
  without `--check-only` → exit 2; in every refusal no output file exists.
Run `node --test tools/feed-contract/tests/gen-manager-latest.test.js`: fails (module missing).

**Implement** plan §4.9 (`plan.md:880-893`) with TA-11's asset rule; `minisign.verifyBuffer(assetBytes, decodedSig,
pubkeyText)` (`tools/minisign.js:181-208`); written atomically (temp file + rename) only after every check; CJS, `'use
strict'`, `main(argv, io)`, `module.exports = { main, build, ASSET_TABLE }`, `if (require.main === module)
process.exitCode = main(process.argv.slice(2), process);` (the `tools/lint-manager-version.js:340-374` form); no network
module, no `child_process`.

**Lane:** pipeline lane (`node --check`, the test file, L-LINT).

### T011 [P] — `tools/manager-release-preflight.js` and the release body (spec FR-044, FR-051, FR-063, D15; SC-011; plan §4.9; PD-16, PD-27; SI-40; TA-12)

**Files:** `tools/manager-release-preflight.js`, `tools/feed-contract/tests/manager-release-preflight.test.js`,
`tools/feed-contract/tests/release-body.expected.md` (new, LF; outside `fixtures/`, plan `plan.md:372-374`).

**Red first:** first statement the write guard (as T010); two top-level tests:
- `manager release preflight refuses by name`: fixture app dirs written into temp dirs (copies of the real
  `apps/krate-manager/version.json`, `CHANGELOG.md`, `src-tauri/tauri.conf.json`, every `Cargo.toml` that
  `lint-manager-version.js` reads, then mutated; read `lint-manager-version.js:223-338` for the set); tags files in plan
  §4.9's two-section format. Rows, each its code and exit 1: version drift in `tauri.conf.json` → `E_PREFLIGHT_VERSION`;
  no CHANGELOG heading → `E_PREFLIGHT_CHANGELOG`; two headings → `E_PREFLIGHT_CHANGELOG`; `## [0.1.0] - Unreleased` with
  `--dry-run false` → `E_PREFLIGHT_CHANGELOG_UNDATED` (with `--dry-run true` → exit 0 and a `::warning` line); an existing
  tag `refs/tags/manager/v0.1.0` and an existing release `manager/v0.1.0` → `E_PREFLIGHT_TAG_EXISTS`, each with
  `--dry-run false` **and** `--dry-run true`; a release list of 1000 lines → `E_PREFLIGHT_TAG_LIST` (both kinds of run);
  `plugins.updater` drift (each of the five keys) → `E_PREFLIGHT_UPDATER_CONFIG`; `E_PREFLIGHT_REPO` (PD-27, SI-40;
  each with `--dry-run true` **and** `--dry-run false`): the fixture app dir's `crates/core/src/self_update.rs` is a
  test-written file holding one line `pub const MANAGER_REPO: &str = "o/r";` and `--repo attacker/x` → `E_PREFLIGHT_REPO`;
  that file without the line, and with the line twice → `E_PREFLIGHT_REPO`; with `--repo attacker/x` **and** an existing
  tag `manager/v0.1.0` the code is `E_PREFLIGHT_REPO` (the order: checked before `E_PREFLIGHT_TAG_EXISTS`); and the
  exported parser `managerRepo(text)` over the real `apps/krate-manager/crates/core/src/self_update.rs` (read only; T001
  wrote the line) returns `rolandzwaga/krate-audio`; on a real run `--pubkey` the fixture
  KAT key (`tools/feed-contract/fixtures/kat/kat.pub`) with a fixture `tauri.conf.json` whose `pubkey` is that key's
  base64 → `E_SIGNING_CONFIG` (TA-12; `refuseTestKey`, `tools/gen-release-manifest.js:373-394`). The valid fixture
  (`--repo o/r`, its `self_update.rs` line naming `o/r`) exits 0
  with `--dry-run true` and `--dry-run false` (dated heading); its notes file holds only the newest section when the
  CHANGELOG has three dated sections (FR-044); `--github-output` gets `version=0.1.0` and `tag=manager/v0.1.0`; the
  usage line on exit 2.
- `manager release body is exact`: `releaseBody({ version: '0.1.0', repo: 'o/r', notes })` equals
  `release-body.expected.md` (CRLF normalised before comparing), including the `curl -fLO … && sudo installer -pkg …
  -target /` one-liner of FR-063 and "(provisional: see the Krate Manager first-launch notes when they are published)".
Run: fails (module missing).

**Implement** plan §4.9 "`tools/manager-release-preflight.js`" as of revision 4 with TA-12: checks in the order
`E_PREFLIGHT_VERSION`, `E_PREFLIGHT_UPDATER_CONFIG`, `E_PREFLIGHT_CHANGELOG`, `E_PREFLIGHT_CHANGELOG_UNDATED`,
`E_PREFLIGHT_TAG_LIST`, `E_PREFLIGHT_REPO`, `E_PREFLIGHT_TAG_EXISTS`, `E_SIGNING_CONFIG`; `--repo <owner/repo>` is
required (exit 2 without it); `managerRepo(text)` returns the string of the one line matching
`/^pub const MANAGER_REPO: &str = "([^"]+)";\r?$/m` and throws when there are zero or several, exported beside
`preflight`, `releaseBody`, `changelogSection`; `lint` from `tools/lint-manager-version.js` (exported, `:372`);
`refuseTestKey` from `tools/gen-release-manifest.js` (exported, `:550`); `changelogSection` per FR-044; the tool's form
as T010's.

**Lane:** pipeline lane.

### T012 [P] — `feed-publish.js` per-tag `latest.json` rule (spec FR-043, D16; SC-010; plan §4.9; PD-17)

**Files:** `tools/feed-publish.js`, `tools/feed-contract/tests/feed-publish-collect.test.js`.

**Red first:** extend `collect copies only a valid manager latest.json` (`feed-publish-collect.test.js:857`) with
subtests (never a new top-level test): a `latest.json` whose `url` is `https://api.github.com/repos/o/r/releases/assets/1`,
one on a foreign host, one under another tag → `degraded`, nothing copied, the message naming "is not a per-tag release
asset URL of"; a per-tag `https://github.com/<repo>/releases/download/<tag>/x` → `copied`; and a direct subtest of the now
exported `checkLatestJson(bytes, tag, version, repo)` (a name with `/`, `?` or `#` after the prefix → throws
`E_MANAGER_LATEST`). Fails on the old rule.

**Implement** plan §4.9's `feed-publish.js` bullet (`plan.md:912-914`): after `contract.checkUrl` (`feed-publish.js:353-358`)
the prefix rule; `collectManager` (`:376-393`) passes `repo`; `checkLatestJson` added to `module.exports` (`:1029-1043`).
Nothing else in the file changes (FR-092).

**Lane:** pipeline lane (the extended test file; `feed-publish-*.test.js` all green).

### T013 [P] — The new guard rules: updater config pins and the self-update confinements (spec FR-003 (a)-(c), (e), FR-021, FR-022 confinement, FR-094; SC-004, SC-012, SC-024 guard rows; plan §4.12; PD-26, PD-36, PD-37; SI-1 (f), SI-15, SI-24, SI-25, SI-28, SI-29, SI-35, SI-39; TA-13, TA-26)

Target slot `F:/tmp/km11-target/core`. **Files:** `crates/core/tests/config_guard.rs` (LF), `crates/core/tests/injection.rs` (CRLF).

**Red first:** write the nine tests (two in `config_guard.rs`, seven in `injection.rs`), run them: the plants are reported only once the rules exist, and the real-tree rows
cross expected red (b) until T006 / T014 land.
- `config_guard.rs` `updater_config_pinned` (real tree passes) and `planted_updater_config_fails_the_guard` (each plant
  reported; use the file's `assert_plants`, `config_guard.rs:1693`): a new `check_updater_config(&ConfigFiles) ->
  Vec<String>` holding FR-003 (a)-(c) and (f): `plugins` has exactly the key `updater` with exactly `pubkey`, `endpoints`,
  `requireSignedVersion`, `allowDowngrades`, `windows`; `requireSignedVersion === true`, `allowDowngrades === false`,
  `windows == {"installMode":"passive"}`, `endpoints == ["https://krateaudio.com/manager/latest.json"]`; `pubkey` equals
  standard base64 of `tools/feed-contract/keys/krate-signing.pub`'s bytes, read from disk, and those bytes hold no `\r`;
  the workspace entry is exactly `tauri-plugin-updater = { version = "=2.14.1", default-features = false, features =
  ["rustls-tls"] }` and the `src-tauri` lines exactly `tauri-plugin-updater = { workspace = true }` and the `reqwest`
  line below; the capability holds no permission with prefix `updater:`,
  `process:`, `shell:`, `fs:`, `http:`. Plants: each FR-003 value changed; a sixth `updater` key
  (`dangerousInsecureTransportProtocol: true`); an in-memory CRLF copy of the key bytes; `createUpdaterArtifacts` in
  `tauri.windows.conf.json`; `"updater:default"` in the capability; a fourth feature `extra = []`; `"2.14.1"` without `=`;
  `features = ["rustls-tls", "zip"]`; the `reqwest` line pinned exactly as `reqwest = { version = "=0.13.5",
  default-features = false, features = ["socks"] }` (PD-30), with plants: `socks` dropped (`default-features = false }`
  only), a second feature (`features = ["socks", "blocking"]`), the version without `=`.
- `injection.rs` `self_update_confinement_holds` / `planted_self_update_confinement_fails`: the rules of plan §4.12
  (`plan.md:1004-1024`) over `workspace_rs_files()` (`injection.rs:1612`), `manifests()` (`:1643`) and the `src-tauri` JSON:
  (1) `tauri_plugin_updater` code token only in `src-tauri/src/lib.rs`, `src-tauri/src/self_update.rs`,
  `src-tauri/src/commands.rs`; `CONFINED_TOML_KEYS` (`:378-412`) gains `("tauri-plugin-updater", &["Cargo.toml",
  "src-tauri/Cargo.toml"])` and `("reqwest", &["src-tauri/Cargo.toml"])`; (2) the `reqwest` code token only in
  `src-tauri/src/self_update.rs`; (3) TA-13's tokens only inside `impl … for NativeUpdateInstaller` in
  `src-tauri/src/**`; (4) the literal `E_INSTALL_SELF_UPDATING` only in `crates/core/src/self_update.rs`; the token
  `SelfUpdating` followed by `)` / `,` / `;` / `}` (a value) only inside `fn action_gate` of that file, test sources
  excluded; each of `download::for_command`, `install::for_command`, `install::remove_for_command`, `fn elevate` holds
  `action_gate (` exactly once; (5) FR-003 (e)'s six tokens nowhere in the workspace `.rs` / `src-tauri` JSON except
  inside the `e2e` module of `src-tauri/src/self_update.rs`; (6) `krate-e2e-marker-v1` only inside that module; (7) the
  SI-24 pins over `src-tauri/src/self_update.rs`: `fn build_updater`'s body holds `. timeout ( inputs . timeouts . check
  )` and `. read_timeout ( inputs . timeouts . stall )`, `update . timeout =` occurs exactly once, inside `fn
  prepare_download`, as `update . timeout = Some ( inputs . timeouts . download )`, and the file's raw text holds no
  `retry` (case-insensitive); (8) the PD-9 pin over `src-tauri/src/lib.rs`: `SelfUpdateHttp ( http . clone ( ) )` exactly
  once and before `assemble_context ( roots , & feed_url , http ,`; (9) the PD-37 pin over `src-tauri/src/self_update.rs`:
  `current_binary (` exactly once, inside `fn install`'s body, at a token position before that body's `. take (`.
  Plants (one each, in memory): the `current_binary(` line removed; a `tauri_plugin_updater`
  use in `crates/core/src/http.rs`; a `reqwest` use in `src-tauri/src/commands.rs`; `x.install(b)` in `commands.rs`;
  `E_INSTALL_SELF_UPDATING` in `install.rs`; the `action_gate(` call removed from `elevate`; `version_comparator` in
  `self_update.rs` outside the module; the marker in `lib.rs`; the `.read_timeout(...)` call deleted; a second
  `update.timeout =`; a `// retry` comment; `SelfUpdateHttp(HttpConfig::production())` in `lib.rs`.
- `update_inputs_only_from_production` / `planted_update_inputs_fail_the_pin` (PD-36, SI-35, plan §4.12 "the
  production-inputs pins"): over `src-tauri/src/self_update.rs`, the body of `fn production` inside `impl UpdateInputs`
  is, whitespace-normalised (the other body pins' normalisation, read and cite it), exactly `Self { http , policy :
  UrlPolicy :: HttpsOnly , timeouts : UpdateTimeouts :: production ( ) , packaging : packaging ( ) }`; over
  `src-tauri/src/commands.rs`, `UpdateInputs :: production (` exactly twice and no `UpdateInputs {` literal; over every
  file of `src-tauri/src`, the only `UpdateInputs {` literal (TA-26: a pair directly after `struct` or `impl` is not
  one) is inside `mod e2e` of `self_update.rs` and holds `.. UpdateInputs :: production (`; `UrlPolicy :: LoopbackOnly`
  occurs in `src-tauri/src` only inside `mod e2e` (today no `src-tauri/src` file names it: `grep -rn LoopbackOnly
  src-tauri/src` this session prints nothing). Plants (in memory, one each): `HttpsOnly` → `LoopbackOnly` in the body;
  `timeouts: UpdateTimeouts { stall: Duration::from_secs(300), ..UpdateTimeouts::production() }` in the body;
  `packaging: Packaging::SelfContained` in the body; an `UpdateInputs { … }` literal in `commands.rs`; a third
  `UpdateInputs::production(` in `commands.rs`; a `UrlPolicy::LoopbackOnly` outside `mod e2e`; the e2e literal without
  its `..UpdateInputs::production(` base. The real tree passes once T006 lands (expected red (b)).
- `updater_e2e_cfg_sites_are_counted` / `planted_updater_e2e_cfg_site_fails` (PD-26): occurrences of `feature =
  "updater-e2e"` per file over `workspace_rs_files()`: `src-tauri/src/self_update.rs` exactly 1, `src-tauri/src/lib.rs`
  exactly 2, every other file 0; plants: an extra `#[cfg(feature = "updater-e2e")]` arm in `commands.rs`, a third one in
  `lib.rs`.
- `harness_source_names_no_installer_or_root` (FR-081 (a), SI-28, SI-29): `harness_source_findings` over every `.rs` under
  `tests/fixtures/updater-refusals/src/` (the scan must reach the six files of plan §2, else a completeness
  finding: expected red (b) until T014): no `Roots :: from_os`, `from_os (`, `SigningKey :: release`, `assemble_context`,
  `krate_manager_lib :: run`, `NativeUpdateInstaller`, `. install (`, `download_and_install`, `. restart (`,
  `process :: restart`, `app . path (`, `BaseDirectory`, no `dirs ::` outside `src/confinement.rs`, and no `TcpStream ::
  connect` outside `src/socks.rs` (PD-30: the relay's loopback dial is the harness's only one); a plant per token on an
  in-memory copy of `scenarios.rs`. The completeness row requires the six files of plan §2 (`socks.rs` included).
The existing tests stay green; `roster.rs` is not edited (plan §4.12 last paragraph).

**Lane:** L-OWN (`--test config_guard`, `--test injection`), L-RS once T006 and T014 have landed.

### T014 [P] — The rsign fixture and the refusal harness (spec D9, FR-080 to FR-084; SC-007, SC-023 / SC-024 harness rows; plan §4.7, §3.2's `map-errors`, §7.4 step 3; PD-11, PD-12, PD-30, PD-31, PD-33, PD-35 (b), PD-36, PD-38; SI-7 to SI-9, SI-23, SI-29, SI-30, SI-33 to SI-35; TA-20, TA-22, TA-24, TA-25, TA-28)

Target: shared, `CARGO_TARGET_DIR="$PWD/target"`, `CARGO_BUILD_JOBS=16`. Logs `F:/tmp/gate/km11-T014/`.
**Files:** `tests/fixtures/updater-refusals/build.rs`, `src/main.rs` (replacing T001's placeholder), `src/scenarios.rs`,
`src/keys.rs`, `src/map_errors.rs`, `src/confinement.rs`, `src/socks.rs` (all LF); `make-rsign-fixture.js` and
`fixtures/rsign/{package.bin, package.bin.minisig, rsign.pub}` (TA-28; byte-exact under T001's `.gitattributes` line);
`specs/krate-manager-phase11/evidence/d9-windows.txt` (TA-22, LF, one line) and `…/evidence/rsign-fixture.txt` (the
generator's printed record). Not the manifest or the lock (T001's; the relay and the new scenarios are `std` / already
locked crates only, so neither changes).

**Step 0 — the rsign fixture (before any harness build; plan §7.4 step 3, PD-35 (b), TA-25, TA-28).** Write
`make-rsign-fixture.js` (ESM, Node built-ins only; `node:child_process` `spawnSync` with an argv array, never a shell
string). Red first: its `--self-test` (pure checks: the argv it would pass to `rsign` for generate and sign, the trusted
comment `timestamp:0\tfile:package.bin\tversion:0.2.0` with real tab characters, the `-c` untrusted comment of TA-25,
the refusal when any of the three outputs exists, the CRLF → LF normaliser over a canned CRLF text) is written first and
fails (functions missing). Then the body: refuse (exit 1, naming the file) when any output exists; write
`fixtures/rsign/package.bin` as the fixed bytes `krate-manager rsign-format fixture\n` (no plugin name, Rule 4 (f));
`const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'km11-rsign-'))`; in `try`: `rsign --version` must print
`rsign2 0.6.7`; `rsign generate -W -p <dir>/rsign.pub -s <dir>/k.key` (stdin closed, as `release.yml:636` feeds
`< /dev/null`); `rsign sign -W -s <dir>/k.key -c "rsign-format fixture (Krate Manager Phase 11)" -t <tc> -x
<dir>/package.bin.minisig fixtures/rsign/package.bin`; copy `rsign.pub` and `package.bin.minisig` out, each with CRLF →
LF and both counts recorded; `finally`: `fs.rmSync(dir, { recursive: true, force: true })` and assert
`!fs.existsSync(dir)`. Print every command, its exit code and output, the CR / LF counts, the minisig's first and
second lines, and `node:crypto` SHA-256 of the three files; the main loop of this task redirects that output into
`specs/krate-manager-phase11/evidence/rsign-fixture.txt`. Verify: `node make-rsign-fixture.js --self-test` exit 0; the
real run exit 0; a second real run exit 1 (refuses to overwrite); `head -1 fixtures/rsign/package.bin.minisig` holds no
`secret key` (TA-25); `cargo test -p krate-manager-core --locked --test injection no_secret_key_in_app_tree` green with
the fixture present (`injection.rs:5746`); `git check-attr text -- tests/fixtures/updater-refusals/fixtures/rsign/package.bin.minisig`
→ `text: unset`. No file named `*.key` is ever under the repository.

**Red first (harness):** write `main.rs`'s parent / child model and the 26 scenario stubs reporting `not ok <name> todo`
before their bodies, build and run (L-HARNESS's run line): the run prints 27 lines, `not ok` for every scenario, exit 1 —
that is the red. **This first run is D9's measurement (PD-33, ruling P-5: no stop):** write `evidence/d9-windows.txt` at once,
`D9 WINDOWS LOADS` when the run printed its lines, or `D9 WINDOWS LOAD FAILURE exit=<code> <loader message>` on a
`0xc0000139` exit or any loader failure before the first line. A failure **never** makes this task `blocked`: keep
writing the bodies, keep L-HARNESS's fmt and clippy rows green on Windows, and take the run rows to WSL (Lane below).
Then write the bodies; scenario lines that cross T003 / T006 placeholders read `not ok … not yet implemented: T003` /
`T006` until those land (expected red (c)).

**Implement** plan §4.7 (revision 4) exactly, with the scenario table of spec FR-080 (`spec.md:710-730`) plus
SI-23's four (`endpoint-latest-path`, `redirect-loop`, `deb-install`, `map-errors`), SI-30's `socks-proxy`, SI-34's
`rsign-format`, SI-35's `production-inputs`, their added assertions (plan §4.7 "Assertions the scenario table gains",
`good` and `deb-install` with PD-31's action rows, `busy`'s second install of SI-36), and `confinement`: the **27** line
names, sorted: `api-host`, `busy`, `busy-after-download`, `confinement`, `corrupt-manifest`, `deb-install`, `downgrade`,
`endpoint-latest-path`, `failed-install-clears`, `good`, `latest-path`, `map-errors`, `no-platform`, `offline`,
`other-key`, `production-inputs`, `recheck-replaces`, `redirect-loop`, `redirect-to-api-host`, `redirect-to-http`,
`replayed-old`, `rsign-format`, `same-version`, `socks-proxy`, `stall`, `tampered`, `unversioned`. Points the plan leaves
to this task:
- `rsign-format` (PD-35 (b), plan §4.7): the three fixture files compiled in with `include_bytes!("../fixtures/rsign/…")`;
  the fixture public key goes through `keys::refuse_committed` first; mock config `pubkey` = standard base64 of
  `rsign.pub`'s bytes; `latest.json` `signature` = standard base64 of `package.bin.minisig`'s bytes (the `base64 -w0`
  framing of `sign updater artifacts`); version `0.2.0`, current `0.1.0`, package URL per TA-24 serving `package.bin`;
  asserts `check` → `Some`, `download` and `verify` → Ok, exactly one package GET, the trap proxy zero, the panicking
  installer never reached. Limit recorded in the notes: the fixture's untrusted comment is TA-25's, not rsign's default.
- `production-inputs` (PD-36, SI-35; no server, no mock app): `UpdateInputs::production(HttpConfig::production())` →
  `policy == UrlPolicy::HttpsOnly`, `timeouts == UpdateTimeouts::production()`, `packaging == packaging()`; `not ok
  production-inputs <field>` on any difference. It sends no request (`HttpConfig::production()` is only constructed).
- Plant mode (PD-38): with `--plant committed-key` or `--plant bind-any` the parent spawns exactly **one** child,
  `--scenario good --plant <name>`, and no other process; it prints that child's first stdout line and exits 1; a first
  line other than the plant's code, or a child exit 0, prints `not ok plant <name>: <line>` and exits 1. The child
  refuses at its first key check (`committed-key`) or its first bind, `0.0.0.0:0` (`bind-any`), before any fixture
  server, relay or mock app exists.
- Mock app per child: `mock_builder().plugin(tauri_plugin_updater::Builder::new().build()).manage(KeptUpdate::default())`
  with `mock_context(noop_assets())` whose `config_mut().plugins` holds the throwaway `plugins.updater` object of
  `plan.md:783-785` and `package_info_mut().version` the scenario's current version; `check` / `install` /
  `prepare_download` from `krate_manager_lib::self_update` run under `tauri::async_runtime::block_on`.
- Roots only from `krate_manager_core::test_support::temp_roots(<child temp dir>)` (SI-7; `clippy.toml:3` disallows
  `Roots::new`); temp dirs from `tempfile::TempDir::new()`; the child's env through `Command::envs` (PD-11), never
  `std::env::set_var`.
- Keys only through `keys::scenario_key(plant)` and checked by `keys::refuse_committed` before any server starts, in every
  child (FR-081 (d), SI-23); the committed key text by `include_str!("../../../../../tools/feed-contract/keys/krate-signing.pub")`
  (count the `../` from this file; it must name the same file `verify.rs:15` includes).
- `bind_loopback` (fixture servers) and `bind_loopback_listener` (the SOCKS relay) the only bind paths, one address check
  shared (`E_HARNESS_BIND`); the trap proxy a second `FixtureServer` with no routes; request counts from
  `FixtureServer::requests()` (`fixture_server.rs:144-195`); `Reply::{Redirect, HeadersThenStall, Delayed}` as the plan
  names (cite `fixture_server.rs:17-62`). Every package URL and route uses the per-tag path (TA-24).
- `socks-proxy` and `src/socks.rs` exactly plan §4.7 (PD-30): the std-only SOCKS4 / SOCKS5 relay (no-auth CONNECT, IPv4
  `127.0.0.1` targets only, refusal reply and record otherwise, a non-SOCKS first byte recorded as `not socks`,
  `std::io::copy` both ways, `Drop` stops it); `TcpStream::connect` only in this file (T013's rule). The scenario runs
  `socks5://` then `socks4://` (`no_proxy` empty) through `check`, `prepare_download`, `download`, `verify`, and asserts
  the relay's CONNECT records (version byte, the fixture server's address, nothing refused, nothing `not socks`), the
  fixture server's request counts, `u.proxy` / `u.no_proxy`, the trap proxy at zero. Cite in the notes the reqwest lines
  that make it pass (`reqwest-0.13.5/src/connect.rs:538-541`, `:783-792`).
- `busy` / `busy-after-download` hold their own `FakeElevation::refusing()`'s guard through `hold_for_test` (TA-8); the
  watcher thread of `busy-after-download` and `stall` asserts `self_update::in_flight(state_dir)` and
  `action_gate(state_dir) == Err(SelfUpdating)` for the `state_dir` passed to `install`.
- `map-errors` (`src/map_errors.rs`): the 31-row table of `plan.md:814-824`; every constructor confirmed against the
  locked crates (a row that does not compile is fixed, never dropped: the row count is asserted); the target key per
  TA-20.
- `confinement.rs` the only file naming `dirs::` (the Windows known-folder snapshot, SI-29); the parent's walk of each
  child's redirected dirs and recorded-writes file (FR-081 (e)).
- `build.rs` per PD-12 (`plan.md:837-849`): on `CARGO_CFG_TARGET_OS == "windows"`, embed tauri-build 2.7.4's default
  manifest bytes (copy them from `~/.cargo/registry/src/*/tauri-build-2.7.4/src/windows-app-manifest.xml`, cite) with
  `tauri_winres::WindowsResource::new().set_manifest(MANIFEST).compile()`; the one `#[expect(clippy::disallowed_methods,
  reason = "a build script reads cargo's CARGO_CFG_TARGET_OS")]`.
- Installers: `PanickingInstaller` (default) and `RefusingStub` (`recheck-replaces`, `failed-install-clears`, `busy`);
  neither names `Update::install`, `download_and_install`, a restart or `NativeUpdateInstaller` (T013's rule).
- Output: per-scenario lines sorted, then `ok confinement` / `not ok confinement <path>`, then `harness: <n> ok, <m> not
  ok` and `harness: wall <ms> ms`; exit 0 iff `m == 0 && n == 27`; a plant prints its code as the first and only line
  and exits 1, having spawned only the `good` child (PD-38).

**Lane:** L-HARNESS (after T003 and T006 have landed: `grep -c 'todo!("T00[36]")' crates/core/src/self_update.rs
src-tauri/src/self_update.rs` → 0 each; before that, run it, record the expected reds, and retry once T003 / T006 report
done, up to three times 60 s apart, Rule 9). **Under `D9 WINDOWS LOAD FAILURE`:** fmt and clippy on Windows as usual;
the run and both plants in WSL with the commands of plan §7.4 step 3 (`wsl.exe -e bash -lc 'cd /mnt/f/projects/iterum/apps/krate-manager
&& CARGO_TARGET_DIR=$HOME/km-target-harness cargo run --manifest-path tests/fixtures/updater-refusals/Cargo.toml --locked'`,
then `-- --plant committed-key` and `-- --plant bind-any`), foreground, log under `F:/tmp/gate/km11-T014/`, ceiling 9 min
per command (the tool's limit). WSL without R-17's packages (a `webkit2gtk-4.1` pkg-config error) or a ceiling hit is
recorded in the notes as "WSL harness run deferred to the main loop (L-HARNESS)" and the task returns done, not
`blocked` (the cold WSL build is a main-loop lane, plan §8.4). Notes: the d9 line verbatim, the wall clock, the 27 `ok`
lines and where they ran (Windows or WSL), the two plant outputs, the rsign fixture's SHA-256 lines and its CR / LF
counts (`evidence/rsign-fixture.txt`), and the reqwest / updater source lines `rsign-format` relies on
(`~/.cargo/registry/src/*/tauri-plugin-updater-2.14.1/src/updater.rs` `verify_signature`, plan §1.2 `:1893-1960`).

### T015 [P] — `release-manager.yml` and its shape test (spec FR-050 to FR-078, D14 as amended; SC-012, SC-015 / SC-021 / SC-022 / SC-026 static halves; plan §4.10, §4.12 last paragraph, §7.3; PD-13, PD-19 to PD-24, PD-27, PD-34, PD-35; SI-10, SI-13, SI-14, SI-20, SI-21, SI-25, SI-34, SI-37, SI-38, SI-40; TA-18)

**Files:** `.github/workflows/release-manager.yml`, `tools/feed-contract/tests/release-manager-shape.test.js` (new, LF).

**Red first:** the shape test (CJS; first statement the write guard; no `child_process`; YAML read as text with helpers
copied verbatim from `manager-ci-shape.test.js:30-151`, this file's own copy) with exactly **one** top-level test
`release-manager.yml keeps the Phase 11 shape` and subtests: "the real file gives no finding", "a CRLF copy gives no
finding", and one subtest per planted mutation. Run `node --test tools/feed-contract/tests/release-manager-shape.test.js`:
fails (the workflow is missing). The `check(text)` function asserts, each with a planted mutation that it reports
(plan §4.10's job table and §4.12's `release-manager-shape.test.js` paragraph, both as of revision 4, and FR-091 as
amended by SI-20 / SI-34, binding):
- top: `on:` only `workflow_dispatch` with `dry_run` (boolean, default `true`); `permissions: contents: read`;
  `concurrency` group `krate-manager-release`, `cancel-in-progress: false`; `defaults.run.shell: bash`;
- jobs exactly `preflight`, `main-only`, `build`, `sign`, `sign-dry`, `package-macos`, `updater-e2e`, `first-install`,
  `publish`, `dry-publish`, `feed`, `feed-dry`, each with the `needs` and `if` of the table; `environment: signing`
  exactly on `main-only`, `sign`, `publish`; `main-only` has exactly one step, an `echo`, and no `secrets.`;
- every `secrets.TAURI_SIGNING_*` reference sits in the `env` of `sign`'s step `signing key`; no job- or workflow-level
  `env` names a secret; no `build`, `sign-dry`, `updater-e2e`, `first-install`, `dry-publish`, `publish` step names
  `secrets.`; the step after `sign updater artifacts` is `delete signing key (after signing)` and removes
  `$RUNNER_TEMP/krate.key`; no step of `sign` / `sign-dry` runs `npm` (but `npm ci --prefix tools/feed-contract
  --ignore-scripts`), `cargo` (but the step `install rsign2`, whose `run` is exactly `cargo install rsign2 --version
  0.6.7 --locked` then `test "$(rsign --version)" = "rsign2 0.6.7"`), `bundle.js`, `tauri` or `npx`, and none of those
  follows the key step; `sign` and `sign-dry` step lists are equal but for `signing key` / `throwaway signing key`; `sign`
  checks out with `sparse-checkout: tools`;
- **PD-35 (revision 4):** no step of `sign` or `sign-dry` uses `actions/cache` or `Swatinem/rust-cache` (unlike
  `release.yml:610-617`, whose `rsign2-0.6.7-…` cache key `feed-tools.yml:65-72` also writes after a scripted `npm ci`,
  `:62`), and `install rsign2` precedes the key step (plants: an `actions/cache` step with the `rsign2-0.6.7-…` key added
  to `sign`; `install rsign2` moved after `signing key`); the four steps `install rsign2`, `updater asset list`, `sign
  updater artifacts`, `delete signing key (after signing)` exist in `sign`, `sign-dry` and `updater-e2e` with byte-equal
  `name` and `run` (they may differ only in step `env:` values), and `throwaway signing key` is byte-equal in `sign-dry`
  and `updater-e2e` (plant: one character of `updater-e2e`'s `sign updater artifacts` `run` changed);
- `preflight` passes `--repo "${{ github.repository }}"` to `tools/manager-release-preflight.js` (SI-40; plant: the
  argument dropped);
- `build`: matrix `windows-latest`, `macos-latest`, `linux` (`ubuntu-24.04` + `container: ubuntu:22.04`); the one
  `bundle` step makes a throwaway key with `npx tauri signer generate --ci` and sets `TAURI_SIGNING_PRIVATE_KEY` to a
  `$RUNNER_TEMP` path, the password masked and never written; `KRATE_BUNDLE_EXTRA_CONFIG` empty (`extra config empty`
  step); steps `leg key is not the release key`, `updater-e2e absent`, `ci-temp-roots absent` (Linux: SI-10's form),
  `build warnings`, `signatures` (`--check-only --platform-subset`), `artifact version`, macOS `lipo` / `plist:
  LSMinimumSystemVersion` / `plist: no LSFileQuarantineEnabled` / `codesign verify` / `codesign ad-hoc`, Linux `glibc
  floor` (FR-065, `::notice title=glibc floor::`); uploads exclude `*.sig`; `build warnings` holds `grep -c
  '^warning:'` and no exclusion (the `manager-ci.yml:246-259` rule copied unchanged; plant: a `grep -v` added);
- **PD-34:** the Linux `bundle` step of `build` and both Linux bundle steps (`build old`, `build new`) of `updater-e2e`
  carry step `env:` `NO_STRIP: 'true'` (plant: dropped from one of the three); `updater-e2e`'s Linux leg has no
  `container:` (it runs on the bare `ubuntu-24.04` runner, so the AppImage runs through FUSE as a user's does);
- **SI-37:** `package-macos`'s `pkg facts` step holds `xmllint --xpath 'count(//relocate/*)'` and requires it to print
  `0`, and uploads the expanded `PackageInfo` as artifact `packageinfo` (plant: the `xmllint` call replaced by a
  `grep`);
- `ci-temp-roots` / `--ci-roots` only in the `ci-temp-roots absent` steps; no `--features` names `ci-temp-roots`;
  `--features updater-e2e`, `--repo-url-base`, `--only`, `dangerousInsecureTransportProtocol` only in `updater-e2e`;
  `sudo`, `installer -pkg`, `apt-get install ./` only in `updater-e2e` / `first-install`; `package-macos` has no `sudo`;
  the `pkg-installed` case's `pkgbuild` block equals `package-macos`'s (FR-061 text, `--component-plist`,
  `BundleIsRelocatable` set `NO`, no `--scripts`, no `--ownership`);
- `updater-e2e`: matrix `windows-latest`, `macos-latest`, `ubuntu-24.04`, name `updater-e2e (${{ matrix.os }})`, needs
  only `preflight`; its release-signing key is its own rsign throwaway (`throwaway signing key`, before `build old`),
  and each of its two builds bundles with a second per-build `npx tauri signer generate --ci` key under a different
  `$RUNNER_TEMP` path; `build old`'s `--config` `pubkey` is `base64 -w0 < "$RUNNER_TEMP/krate.pub"` (the rsign key);
  steps in this order: `install rsign2`, `throwaway signing key`, `server`, `build old`, `marker present`, `build new`,
  `updater asset list`, `sign updater artifacts`, `delete signing key (after signing)`, `latest.json`, `rsign signature
  served`, the FR-072 cases; `latest.json` names `--pubkey "$RUNNER_TEMP/krate.pub"` and `--repo ${{ github.repository
  }}` and comes after `sign updater artifacts` (plant: moved before it); `rsign signature served` compares the served
  entry's `signature` byte for byte with `base64 -w0 < "$RUNNER_TEMP/<name>.minisig"` (PD-35, SI-34); the
  FR-072 cases (Windows `/S` + `$env:LOCALAPPDATA\Krate Manager\krate-manager.exe` (SI-14); macOS `user-placed` and
  `pkg-installed` with the `SecurityAgent` observe-and-cancel loop of PD-13; Linux `appimage` (with `libfuse2t64`) and
  `deb` asserting `error-E_SELF_UPDATE_UNSUPPORTED` (PD-24)), `annotate`; no `APPIMAGE_EXTRACT_AND_RUN`;
- `first-install`: four legs incl. `ubuntu-22.04-container`; the `block live host` step (plan `plan.md:973-978`) on every
  leg, directly before `launch`, and in no other job (plant: removed from one leg); the hosts file edited nowhere else;
  launch is process presence only (FR-077);
- no `run` step of `first-install` / `updater-e2e` invokes `gh`, `curl`, `wget` or names `github.com`; no
  `tauri-apps/tauri-action`; no `/releases/latest/`, no `api.github.com`, no `dev-feed` anywhere; `--latest=false` on
  `gh release create` and `gh release edit`; no `--clobber`; `feed` / `feed-dry` `uses: ./.github/workflows/feed.yml` with
  `dry_run: false` / `true` and `permissions: contents: write` (PD-19); `dry-publish` has the `sigs not committed key`
  step expecting `minisign: E_MINISIG_KEY_ID` (`tools/minisign.js:186`).

**Implement** `release-manager.yml` exactly plan §4.10 as of revision 4 (the job table, "Where keys exist", the
`updater-e2e` per-leg cases with every leg's `rsign signature served` step, `first-install`) with SI-21's `.deb` case;
the `sign updater artifacts` `run` is plan §4.10's `sign` text (`rsign sign -W -s "$RUNNER_TEMP/krate.key" -t
"timestamp:$(date +%s)"$'\t'"file:$name"$'\t'"version:$VERSION" -x "$RUNNER_TEMP/$name.minisig" "$ASSET_DIR/$name" <
/dev/null`, then `base64 -w0 < "$RUNNER_TEMP/$name.minisig" > "$ASSET_DIR/$name.sig"`, no `-c`, so CI signs with rsign's
default untrusted comment), written once and copied byte for byte into `sign-dry` and `updater-e2e`; every assertion
prints `::notice` / `::error` naming the leg (FR-074). Actions only as the repo uses them (`actions/checkout@v6`,
`actions/setup-node@v4`, `actions/upload-artifact@v7`, `actions/download-artifact@v8`, `Swatinem/rust-cache@v2` in
`build` / `updater-e2e` only, never in `sign` / `sign-dry`; plan §5). The file names no plugin. It is never dispatched
(Rule 1).

**Lane:** pipeline lane (the shape test, L-YAML on `release-manager.yml`); repo root `node --test
tools/feed-contract/tests/manager-ci-shape.test.js` (its `dev-feed` check reads this file, `manager-ci-shape.test.js:1286-1288`).

### T016 [P] — Evidence scripts: scope check, harness lane, WSL lane, close lane (spec FR-092; SC-007, SC-012 local rows; plan §7.4, §8)

**Files (new):** `specs/krate-manager-phase11/evidence/scope-check.js`, `harness-lane.js`, `wsl-lane.js`, `close-lane.js`.

**Red first:** each script's `--self-test` (pure checks) is written before its body and run: it fails.
- `scope-check.js`: `git diff --name-only 30878b36` plus untracked files (`git status --porcelain --untracked-files=all`)
  checked against Rule 11's allow list (base `30878b36`, the phase's starting `HEAD`, never `4f04dc03`, whose diff holds
  Phase 10's committed `compliance.md`, D-23); the `feed.yml`, `release.yml`, `ci.yml`, `docs.yml`,
  `backfill-sign.yml`, `tools/gen-feed.js`, `CONTRACT.md`, `tools/feed-contract/fixtures/**`, `crates/helper/**`,
  `plugins/`, `dsp/`, `cmake/`, `extern/` fence; `.gitattributes` is allowed only when `git diff 30878b36 --
  .gitattributes` adds exactly TA-2's two lines and removes none (Phase 10's `AFTER_M1_EXACT` pattern,
  `specs/krate-manager-phase10/evidence/scope-check.js:48`); no file named `*.key` anywhere in the list; exit 1 naming
  each path out of scope. Self-test: the matcher over a fixed list of in / out paths and the `.gitattributes` hunk rule
  over a canned good and a canned bad diff.
- `harness-lane.js`: L-HARNESS's five commands in order with a wall-clock ceiling each; parses the run output (the 27
  names `ok` exactly once, the summary line, `harness: wall`), the plants (first and only line, exit 1). It reads
  `evidence/d9-windows.txt` (TA-22; missing → it runs the Windows run, and a `0xc0000139` / pre-output loader failure
  there is reported `RED: d9 not recorded`, because T014 owns the measurement). On `D9 WINDOWS LOAD FAILURE` it runs fmt
  and clippy on Windows, then the run and both plants in WSL (`wsl.exe`, `/mnt/<drive>/…` path from Phase 10's
  `toWslPath`, `CARGO_TARGET_DIR=$HOME/km-target-harness`, `timeout --kill-after=30` inside WSL with a 40-minute
  ceiling, as `specs/krate-manager-phase10/evidence/wsl-lane.js:35`, `:68`; plan §7.4 step 3); a WSL that cannot build
  (no `webkit2gtk-4.1`, R-17) is `RED: WSL <reason>` (plan R-18), never green and never skipped. Last line `HARNESS LANE
  GREEN (Windows, <n> ok, <ms> ms)` / `HARNESS LANE GREEN (WSL, <n> ok, <ms> ms; Windows gap: <d9 line>)` / `RED:
  <rows>`. Self-test: the parser over canned green, red and plant outputs, the d9-line parser over both forms and a
  malformed one, and the verdict for each combination.
- `wsl-lane.js`: a copy of `specs/krate-manager-phase10/evidence/wsl-lane.js` with Phase 11 paths and log names; it reads
  no `cfg(unix)` test list of its own beyond Phase 10's (none is added this phase).
- `close-lane.js`: adapted from `specs/krate-manager-phase10/evidence/close-lane.js` (read its header `:1-60`): rows
  L-RS, L-APP, L-DEVFEED, L-MANUAL, L-E2E, L-GUARD (the workflow's step text), L-HARNESS (runs `harness-lane.js`; its WSL
  half, when D9's fallback applies, concurrently with L-WSL: separate target dirs), L-TS with the vitest JSON report,
  L-NAPP, L-NODE, L-TAP (`1..64`), L-YAML ×3, L-LINT, L-VLINT, L-INDEX, L-SCOPE, L-REQ (the workflow's step text; `372
  cargo + 70 vitest + 6 node`), L-WSL beside the cargo chain; `node apps/krate-manager/tests/fixtures/updater-refusals/make-rsign-fixture.js
  --self-test` (never the real run: the fixture is committed); the d9 line copied into `timings.md`; the greps of T018
  step 5; FR-097's and the rsign fixture's `git check-attr`; a `timings.md` table written to `evidence/`; independent chains concurrent (CLAUDE.md "Optimise
  every build and test run"), every child under a ceiling; last line `CLOSE LANE GREEN` / `RED: <rows>`.
None of the four runs a cargo or npm command in its self-test; none writes outside the repo's `specs/krate-manager-phase11/evidence/`
and `F:/tmp/gate/`.

**Lane:** `node --check` and `--self-test` of the four (repo root); L-LINT (the serial-run lint scans `CLAUDE.md`,
workflows, agents, skills; these scripts run `cargo test` / `node --test` in their parallel default form).

---

## G3 — Integration (sequential: T017, then T018)

### T017 — `manager-ci.yml` steps, `manager-ci-shape.test.js`, `feed-tools.yml`, `hygiene.test.js` (spec FR-073, FR-083, FR-084, FR-091; SC-017 static half; plan §4.11; PD-16, PD-32, PD-33, PD-38; SI-23, SI-25, SI-30, SI-32, SI-33, SI-35; TA-14, TA-22)

**Files:** `.github/workflows/manager-ci.yml`, `tools/feed-contract/tests/manager-ci-shape.test.js`,
`.github/workflows/feed-tools.yml`, `tools/feed-contract/tests/hygiene.test.js` (all LF).

**Red first:** read `specs/krate-manager-phase11/evidence/d9-windows.txt` (TA-22; missing or malformed → `blocked`,
naming T014, because the `if:` below depends on it). In `manager-ci-shape.test.js` add the four step names to `STEPS`
(`:167-201`) in order (`clippy updater-e2e` after `clippy manual-elevation example`, `clippy updater-refusals` after
`clippy-guard`, `updater refusals` after `test ci-temp-roots`, `updater-e2e absent` after `dev-feed absent`) and to
`ASSERTION_STEPS` (`:211-229`), with their `COMMANDS` lines (`:232-373`); a constant `HARNESS_ON_WINDOWS` (`true` for
`D9 WINDOWS LOADS`, `false` for `LOAD FAILURE`) commented with the d9 line verbatim, from which the check requires the
`updater refusals` step to have no `if:` (`true`) or exactly `if: runner.os != 'Windows'` (`false`); an
`E2E_FEATURE_STEPS = { 'clippy updater-e2e': 'cargo clippy -p krate-manager ' }` rule beside `CI_FEATURE_STEPS`
(`:438-443`): within `check`, `--features updater-e2e` and the names `KRATE_E2E_FEED_URL`, `KRATE_E2E_MARKER_DIR`,
`KRATE_E2E_OLD_VERSION` only in that step, its `env:` holding exactly the three dummy values of plan PD-32, and no
`dev-feed` in it; and one mutation each in `MUTATIONS` (`:1319`): each new step removed; `clippy updater-e2e` moved after
`clippy-guard`; its `env:` dropped; `--features updater-e2e` added to `clippy`; the harness count changed from 27; the
plant check dropped; the marker count check dropped; the `updater refusals` `if:` flipped against `HARNESS_ON_WINDOWS`.
In `hygiene.test.js` add `tools/gen-manager-latest.js` and `tools/manager-release-preflight.js` to `scannedFiles`
(`:33-53`) and the completeness list (`:119-134`). Run L-NODE: `manager-ci.yml keeps the Phase 4 shape` fails on the
missing steps.

**Implement:**
- `manager-ci.yml` `check` (plan §4.11): `clippy updater-e2e` after `clippy manual-elevation example` (`:109-116`), plan
  PD-32 exactly (step `env:` `KRATE_E2E_FEED_URL: 'http://127.0.0.1:9/versions.json'`, `KRATE_E2E_MARKER_DIR:
  'never-run'`, `KRATE_E2E_OLD_VERSION: '0.0.0'`; `if cargo clippy -p krate-manager --all-targets --features updater-e2e
  --locked -- -D warnings; then` `::notice title=clippy updater-e2e ($RUNNER_OS)::clean (compiled and linted, never run)`,
  else `::error` and exit 1; no `if:`, all three legs); `clippy updater-refusals` after `clippy-guard` (`:118-136`):
  `CARGO_TARGET_DIR="$PWD/target" cargo clippy --manifest-path tests/fixtures/updater-refusals/Cargo.toml --all-targets
  --locked -- -D warnings` and `cargo fmt --check --manifest-path tests/fixtures/updater-refusals/Cargo.toml`, `::notice` /
  `::error`, all three legs whatever D9 measured; `updater refusals` after `test ci-temp-roots` (`:156-166`): build + run with `CARGO_TARGET_DIR="$PWD/target"`,
  each of the 27 names of T014 (`rsign-format` and `production-inputs` included) `ok` exactly once, the line `harness:
  27 ok, 0 not ok`, then `--plant committed-key` and `--plant
  bind-any` each exit 1 with `E_HARNESS_KEY` / `E_HARNESS_BIND` as the only stdout line, `::notice title=updater refusals
  ($RUNNER_OS)::27 ok, 0 not ok; plants exit 1` (each plant run prints no `ok ` line: PD-38 starts only the `good`
  child), with `if: runner.os != 'Windows'` exactly when the d9 line is `LOAD
  FAILURE` (PD-33); `updater-e2e absent` after `dev-feed absent` (`:345-355`): `cargo tree -p
  krate-manager -e features --locked` holds no `updater-e2e`, the marker occurs ≥ 1 in `src-tauri/src/self_update.rs` and
  0 times in `target/release/krate-manager(.exe)` and, on macOS, in `target/release/bundle/macos/Krate
  Manager.app/Contents/MacOS/krate-manager`. No step beyond these four changes (the existing `tauri build` step is
  SC-017's check of D3). No serial test flag (L-LINT).
- `feed-tools.yml` (plan §4.11 `plan.md:990-992`): both path lists (`:4-21`, `:23-40`) gain `tools/gen-manager-latest.js`,
  `tools/manager-release-preflight.js`, `.github/workflows/release-manager.yml`; `PLAN` (`:86`) becomes `"1..64"`; `NAMES`
  (`:87-122`) gains `gen-manager-latest writes per-tag latest.json`, `gen-manager-latest refuses what it cannot verify`,
  `manager release preflight refuses by name`, `manager release body is exact`, `release-manager.yml keeps the Phase 11
  shape`.

**Lane:** pipeline lane: L-NODE, L-TAP (`TAP plan=1..64 top-level=64`), L-YAML (`manager-ci.yml`, `feed-tools.yml`),
L-LINT.

### T018 — Integration checks, the harness on the whole tree, INDEX, the PENDING-CI list, the hand-over (spec FR-092, FR-095, FR-097; SC-007 local, SC-012; plan §7.4; TA-2)

**Files:** `specs/INDEX.md` (regenerated by `node tools/gen-specs-index.js` only if L-INDEX fails), and this task's notes.
No code: a red here is reported against its owning task (file ownership table), never fixed in this task.

1. L-RS, L-APP, L-DEVFEED, L-MANUAL, L-E2E, L-GUARD, L-TS (70 titles), L-NAPP (6 node names).
2. L-HARNESS via `node specs/krate-manager-phase11/evidence/harness-lane.js` (SC-007's local row: Windows, or WSL under
   `D9 WINDOWS LOAD FAILURE`; record the wall clock, where it ran and the 27 `ok` lines; a D9 load failure is never
   `blocked`: it is the Windows gap, the d9 line copied into the notes verbatim, PD-33; a WSL run that cannot start or
   overruns this task's tool ceiling is deferred to L-CLOSE in the main loop, noted).
3. L-NODE + L-TAP (`1..64`), L-YAML ×3, L-LINT, L-VLINT, L-INDEX (regenerate if stale), L-SCOPE.
4. L-REQ: `::notice title=required tests ran (Windows)::372 cargo + 70 vitest + 6 node`.
5. Greps (each over `git diff --name-only 30878b36` plus untracked files, D-23): no `todo!(` / `unimplemented!(` outside
   `*_tests.rs` and `tests/` (the harness's own `src/` counts as code: none there either); no added `#[allow(`, no added
   `#[expect(` but the harness `build.rs` one, no `@ts-ignore` / `eslint-disable`; no `--test-threads` /
   `--no-file-parallelism`; no file named `*.key` and no secret-key text under `apps/krate-manager/`; `git check-attr -a
   tools/feed-contract/keys/krate-signing.pub` prints `text: unset` (FR-097, TA-2), and the same for each of the three
   files under `apps/krate-manager/tests/fixtures/updater-refusals/fixtures/rsign/`; `git diff 30878b36 -- .gitattributes`
   adds exactly TA-2's two lines; `head -1` of the fixture `.minisig` holds no `secret key` (TA-25); `node
   apps/krate-manager/tests/fixtures/updater-refusals/make-rsign-fixture.js --self-test` exit 0 (never the real run);
   `rm -rf F:/tmp/km11-target` done (Rule 10).
6. Return with notes "deferred to the main loop at close: run `node specs/krate-manager-phase11/evidence/close-lane.js`"
   and the PENDING-CI list below copied into the notes.

**Lane:** app lane + pipeline lane (the workflow's last-group gate runs both).

---

## PENDING-CI (macOS and Linux evidence, and every hosted-runner row; only the user can trigger it)

Each row closes only on the named green job of a run the user starts; none closes on a local pass (plan §7.3 as of
revision 4).

| Row | Job (name as GitHub shows it) | Assertion closing it |
|---|---|---|
| SC-007 macOS / Linux, SC-017 | `Manager CI / check (macos-latest)`, `check (ubuntu-24.04)` (and, only on `D9 WINDOWS LOADS`, `check (windows-latest)` as the CI twin of the local row) | `updater refusals` notice `27 ok, 0 not ok; plants exit 1` (`socks-proxy` among them: PD-30 on macOS / Linux; `rsign-format` and `production-inputs` too: PD-35 (b), PD-36; on macOS the scenarios that reach `install` also show PD-37's pre-check passing a real, symlink-free path, plan §4.16 (4)); `clippy updater-refusals` notice; `updater-e2e absent` notice; `tauri build` + `build warnings` green with `plugins.updater` in the base config and no key (D3) |
| SC-007 Windows under `D9 WINDOWS LOAD FAILURE` | none: the step is skipped by its `if:` | compliance records the gap with `evidence/d9-windows.txt` quoted (PD-33); the local WSL run (L-HARNESS) is the early signal, not a closing row |
| PD-32 | `Manager CI / check (windows-latest \| macos-latest \| ubuntu-24.04)` | `clippy updater-e2e` notice `clean (compiled and linted, never run)` on all three legs (the macOS / Linux compile of the e2e module; Windows also runs locally as L-E2E) |
| SC-001 to SC-006, SC-023 to SC-025 core rows on macOS / Linux | `Manager CI / check (macos-latest)`, `check (ubuntu-24.04)` | `required tests ran` notice (the 24 new cargo names in `cargo.all`, TA-1) |
| `src-tauri/src/self_update.rs` on macOS / Linux (the `#[cfg(not(windows))]` restart path, `packaging()`'s `Deb` / `Rpm` arm, PD-37's `#[cfg(target_os = "macos")]` `current_binary` pre-check): no local compile, WSL builds core and helper only | `Manager CI / check (macos-latest)`, `check (ubuntu-24.04)` | `clippy` step 0 warnings (`manager-ci.yml:86-90`) |
| SC-013, SC-022 macOS | `release-manager / build (macos-latest)` | the `lipo` (both executables `x86_64 arm64`), `plist: LSMinimumSystemVersion`, `plist: no LSFileQuarantineEnabled`, `codesign verify`, `codesign ad-hoc`, `signatures`, `updater-e2e absent`, `ci-temp-roots absent`, `leg key is not the release key` notices |
| SC-018, SC-022 Linux | `release-manager / build (linux)` | `glibc floor` notice `<x.y> (container <x.y>)`, `signatures`, `leg key is not the release key`, both `.AppImage` and `.deb` present |
| PD-34 / R-21 | `release-manager / build (linux)` and `updater-e2e (ubuntu-24.04)` (both builds) | the `bundle` steps succeed with `NO_STRIP: 'true'` and the AppImage asset exists; a linuxdeploy failure is the step's non-zero exit ("failed to run linuxdeploy"), recorded and surfaced, never retried silently; the first AppImage build anywhere is this dispatch (no push-time lane builds one) |
| SC-022 Windows | `release-manager / build (windows-latest)` | `updater-e2e absent`, `ci-temp-roots absent`, `leg key is not the release key` notices |
| SC-022 positive control | `release-manager / updater-e2e (windows-latest \| macos-latest \| ubuntu-24.04)` | `marker present` notice (≥ 1 in the e2e binary) |
| SC-015 | `release-manager / updater-e2e (windows-latest)`, `(macos-latest)`, `(ubuntu-24.04)` | `rsign signature served` notice on each leg (PD-35 (a): the served `.sig` is the one the copied `sign updater artifacts` step made with rsign's default untrusted comment, so every `launched-<new>` is the updater's verifier accepting the release signing path; this also closes TA-25's local limit); per-case notices: Windows ProductVersion old → new and one setup GET; macOS `user-placed` (version, same path, `codesign`, no quarantine) and `pkg-installed` (prompt observed via `SecurityAgent` within 60 s, `error-E_SELF_UPDATE_INSTALL` within 30 s, manifest unchanged); Linux `appimage` old → new and `deb` refused `E_SELF_UPDATE_UNSUPPORTED` with zero package GETs (SI-21) |
| SC-021 | `release-manager / package-macos` | `payload` notice; `pkg facts` notice `count(//relocate/*) = 0` naming whether `relocate` was absent or empty (SI-37); `PackageInfo` in the job summary and the `packageinfo` artifact, which compliance copies to `specs/krate-manager-phase11/evidence/packageinfo-dry-run.xml` (no local run can make it: FR-090 bars `pkgbuild` here) |
| SC-016, SC-026 | `release-manager / first-install (windows-latest \| macos-latest \| ubuntu-24.04 \| ubuntu-22.04-container)` | `block live host`, install and `launch` notices on all four legs |
| FR-055 / SC-019 key half | `release-manager / sign-dry` (dry run); `sign` (the user's real run, Phase 12) | `install rsign2` compiled fresh (no cache step in the job, PD-35 (c)) and printed `rsign2 0.6.7`; `verify` notice (3 `.sig`s verified against the dry key / the committed key); `delete signing key (after signing)` ran |
| SI-40 | `release-manager / preflight` on every dispatch | the `preflight` notice shows `--repo` equal to `MANAGER_REPO` (`rolandzwaga/krate-audio`); a fork's dispatch would fail `E_PREFLIGHT_REPO` before any build |
| SC-014, SC-019 | `release-manager` dispatched `dry_run: true` from `main` (clean); from a branch with a drifted `version.json`; from `main` with a test tag `manager/v0.1.0` the user creates and deletes; from a branch with the CHANGELOG heading removed | each refusal a `preflight` `::error` with its code and no `build` job started; the clean run's `manager-release-dry-run` artifact, `feed-dry` green, `sigs not committed key` notice (3 of 3 refused `E_MINISIG_KEY_ID`), the `gh release view` / `git ls-remote` evidence of spec SC-019 pasted |
| SC-020 | the user's first real dispatch (Phase 12) | the published release, `feed` deploying `manager/latest.json` |

---

## Defects found (in the plan, while writing this list)

- **D-1.** `config_guard.rs:81-94` `CONFINED` homes `LoopbackOnly` and `HttpConfig::loopback` in `src/dev_feed.rs` only;
  the plan's e2e module (§4.5) uses both in `src-tauri/src/self_update.rs` and does not widen the rule. Fixed by TA-3.
- **D-2.** Plan §4.12 lists only the new pins; the existing ones the phase's edits trip (`config_guard.rs:50-57`, `:169`,
  `:660-668`, `:683-714`, `:738-748`, `:773-787`, `:901-903`, `:937-952`) are not named. Fixed by TA-3.
- **D-3.** `http::redact_userinfo` is `pub(crate)` (`http.rs:196`), so plan §3.2's `map_error` cannot call it. TA-6.
- **D-4.** `status::os_name` is private (`status.rs:367`). TA-7.
- **D-5.** FR-097 is already in `.gitattributes:29` (comment `:27-28`) at `4f04dc03`. TA-2.
- **D-6.** `download::DOWNLOADS_DIR` is `download.rs:29`, not `:1370`; `MAX_REDIRECTS` is `u32` (`http.rs:24`). TA-16.
- **D-7.** SI-2's "every lock path passes through the updater" alone passes the existing orphan-`tar` plant vacuously
  (`injection.rs:6056-6058`). TA-4 adds the count half.
- **D-8.** A workspace-wide `.install(` rule reports `install_tests.rs:1154` and its siblings (`fx.install`). TA-13.
- **D-9.** SI-20's README wording drops `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`, which `workflow-shape.test.js:602-604`
  requires within 200 characters of "Phase 11". TA-17.
- **D-10.** SC-005's stale, `BUSY` and download rows need seams the plan does not define (`plan.md:1143`). TA-8.
- **D-11.** The plan's harness-lock procedure (`plan.md:754-755`) re-resolves the whole graph first. TA-5.
- **D-12.** "Exactly the D6 names" (`plan.md:884`) fails on a build leg without the pkg. TA-11.
- **D-13.** The preflight's fixture-key case needs a key option the plan's CLI lacks. TA-12.
- **D-14.** The e2e module may not add an `eprintln!` (`config_guard.rs:812-824`). TA-19.
- **D-15.** `tauri-plugin-updater` 2.14.1 is not in the local registry; T001's lock step downloads it (Rule 1).
- **D-16.** The roadmap's Phase 11 lines moved by one (Phase 10's status line); edits anchor by text (T002).
- **D-17 (revision 2).** Plan revision 2's R-6 said `reqwest::Proxy::all` of a SOCKS URL fails without the `socks`
  feature; at 0.13.5 `IntoProxy` accepts it (`proxy.rs:124-137`) and the failure is at connect (`connect.rs:783-792`).
  Moot under ruling P-1 (PD-30); corrected in plan R-6.
- **D-18 (revision 2).** Ruling P-5's "locally in WSL" cannot run today: WSL has no WebKitGTK (measured, plan §1.1;
  Phase 10 `wsl-lane.js:11`). Plan §10 R-17 (the user's); this file is written to its recommendation (a).
- **D-19 (revision 2).** Ruling P-3's release page needs a URL the UI may not name (`config_guard.rs:1117-1160`); plan
  PD-31 has core build it from the per-tag path, which in turn needs per-tag harness routes (TA-24).
- **D-20 (revision 3).** Plan §4.7 `rsign-format` commits `package.bin.minisig` "untrusted comment as rsign writes it":
  rsign's default first line is `untrusted comment: signature from rsign secret key` (the KAT `.minisig`, read this
  session), which the existing FR-083 app-tree scan `no_secret_key_in_app_tree` (`injection.rs:5746`, rule
  `secret_text_hits` `:2917-2936`) reports as a secret-key comment. Fixed by TA-25 (`-c` comment; CI proves the default
  comment, PD-35 (a)). The other option, an exemption in FR-083 rule (2) for `signature from` lines, loosens a
  key-leak guard and is left to the user (open question).
- **D-21 (revision 3).** Plan §4.12's PD-36 rule "the only other `UpdateInputs {` literal is inside `mod e2e`" counts the
  type's own `pub struct UpdateInputs {` and `impl UpdateInputs {` (plan §4.3) as literals. TA-26.
- **D-22 (revision 3).** Revision 2's T006 took the e2e loopback timeouts from `dev_feed.rs`, which has none: it calls
  `http::config_for_dev_url` (`dev_feed.rs:22`), a `config_guard`-confined token (`config_guard.rs:82`). TA-27 uses
  `http::CONNECT_TIMEOUT` / `TOTAL_TIMEOUT` (`http.rs:22-23`).
- **D-23 (revision 3).** Revision 2's scope check and T018 greps diffed against `4f04dc03`; `git diff --name-only
  4f04dc03 30878b36` lists `specs/krate-manager-phase10/compliance.md`, which the allow list fences, so L-SCOPE would be
  red on a committed Phase 10 file. Base is `30878b36`.
- **Plan follow-ups applied (plan "Review notes", revision 4, last bullet):** cargo required +2 (TA-1, T001 step 11,
  L-REQ 372); harness count 27 (T013 completeness unchanged at six `.rs` files; T014, T016, T017, T018, PENDING-CI); the
  `make-rsign-fixture.js` step before the harness's first build (T014 step 0, TA-28); the new shape-test rules (T013's
  PD-36 / PD-37 pins, T015's PD-34 / PD-35 / SI-37 / SI-40 rules); `E_PREFLIGHT_REPO` (T011); the `.gitattributes` line
  (T001, TA-2).
