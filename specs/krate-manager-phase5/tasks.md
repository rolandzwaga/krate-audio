# Tasks: Krate Manager Phase 5 — Feed Client and Version Model

**Spec:** `specs/krate-manager-phase5/spec.md` (D1-D16, FR-001 to FR-095, SC-001 to SC-044; Clarifications Q1-Q8 of
2026-10-30, R-1 to R-7 and R-8 to R-15 of 2026-10-31, `spec.md:1547-1638`) · **Plan:** `specs/krate-manager-phase5/plan.md`
(revised again 2026-10-31 for R-8 to R-15, then review round 3: PD-1 to PD-36, SI-1 to SI-41 with SI-23 and SI-32
withdrawn, rulings R-1 to R-17 with R-11 withdrawn and **R-16, R-17 open**, `plan.md:1-43`, `:143-163`, `:2019-2165`)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 5 (lines 527-552); Phase 6 Components (lines 558-571); Phase 12
Components (lines 866-897); Cross-Cutting Constraints (lines 968-1009)
**Status:** DRAFT task list, 2026-10-31, regenerated for the plan's R-15 revision (plan §10: "`tasks.md` predates this
revision and the tasks stage regenerates it", `plan.md:2031`). Written against `feat/krate-manager` at `a145271b`
(`git log --oneline -1`); the last commit touching `apps/krate-manager/` is `ac307f7e` (`git log -- apps/krate-manager`),
so every Phase 4 file:line the plan cites at `a28b1181` still holds; the ones this file quotes were re-read this session.
**Lanes in this phase:** the **app lane** (cargo fmt / clippy / clippy dev-feed / clippy live-network / clippy-guard /
cargo test, npm typecheck / lint / test, the version lint, `tauri build`, the roster scan), the **pipeline lane**
(`node --test` over `tools/feed-contract/tests/`, YAML parse, the repo lints) and **L-WSL** (core clippy + tests in WSL
Ubuntu 24.04: the only local compile and run of the macOS / Linux PAC engine, plan §8; a main-loop lane, TA-18). No file
under `plugins/`, `dsp/`, `cmake/` or `extern/` changes (spec FR-090), so there is **no C++ lane**: no `run-gate.js`, no
`header-consumers.js`, no `check-portability.js`, no clang-tidy, no pluginval (plan §8). `npm run tauri build`, L-WSL and
the full close procedure are main-loop lanes (T001 and T016 write their scripts).

**What changed from the previous list** (for the reviewer; the old list was written against PD-1 to PD-34): on Windows
the OS evaluates PAC (`WinHttpGetProxyForUrlEx`, PD-35) and `rquickjs` moves to `[target.'cfg(not(windows))'.dependencies]`
(PD-25), so the engine half of `pac.rs` and every engine / PAC-fetch test is `cfg(not(windows))` and runs locally only in
L-WSL; `windows-sys` keeps exactly two features (SI-32 withdrawn: no `Win32_System_Time`, no `win_utc_offset`, no
`WinHttpDetectAutoProxyConfigUrl`); `PacHost` gains `os_evaluates` / `os_find_proxy` and `pac.rs` gains `OsProxyKind`,
`OsProxyEntry`, `os_answer` (PD-36); `feed.rs` gains a fourth newtype `PageUrl` (PD-22); the test names follow spec
SC-033 to SC-043 (`script_world_cases`, `wpad_candidate_cases`, `dns_bound_cases`, `pac_clock_cases`,
`parse_utc_offset_cases`, `offset_range_cases`, `winhttp_answer_cases`, `os_pac_walk`, `os_pac_failure_cases`); the two
guard tasks moved from G3 to G2 (TA-20); G1's two tasks run in parallel (TA-17); `results.md` gains its one R-15 row in
the build (SI-41).

---

## How to read this file

- Groups run in order. A group marked **[P]** runs its tasks at the same time in one working tree. A task is
  **[P]-safe** when no other task in its group touches any of its files and none of its files is a **shared file**:
  any `Cargo.toml`, `Cargo.lock`, `clippy.toml`, `crates/core/src/lib.rs`, `src-tauri/src/lib.rs`, a `mod.rs`,
  `package.json`, `tauri*.conf.json`, a capability file, a workflow, `tests/required-tests.json`,
  `manager-ci-shape.test.js`, `spec.md`, the roadmap or `specs/INDEX.md`. Every app-side shared file is edited in one
  task only (T002, before anything else in the app), so the module tasks after it are [P]: T002 writes **every** `pub
  mod` line, every manifest entry and a compiling placeholder file per module up front (the "one task writes all shared
  lines" rule). The spec, roadmap and INDEX are touched only by T001, in parallel with T002 (disjoint files, TA-17); the
  workflow, `required-tests.json` and the shape test only by T017 (G4). The files a [P] task owns are either new
  placeholders T002 created for it, or an existing test file that exactly one task in the group edits
  (`injection.rs`, `config_guard.rs`); that second kind is a deliberate reading (TA-6): those files are not shared
  lists, and only their one owner in the group writes them.
- Each task section is the executor's authoritative instruction: files, the failing test to write first (file, test
  name, assertions), the implementation intent, the lane. Where a block is too long to copy without drift (an API, a
  manifest, a test table, the PD-35 call sequence), the section names the `plan.md` section or §7.1 row that holds it;
  that text is then **binding, verbatim**, as amended by plan §0.2 (SI rows) and this file's TA rows. Where this file
  and the plan differ, this file wins and the TA row says why.
- Canonical order in every task: **failing test → implement → zero warnings → tests pass.** "Red first" means the test
  was run and failed for the stated reason before the implementation existed; the notes quote the failing line. The
  red run normally uses T002's placeholder, so the test compiles and fails on the `todo!()` panic ("not yet
  implemented") or on its assertions. A changed signature is red when the test does not compile against the old
  signature; quote the compiler's error line. A `cfg(not(windows))` test cannot run on this box: its red-first is its
  L-WSL-OWN run (TA-18), or, when L-WSL-OWN times out, the next gate's L-WSL; say which in the notes.
- **Expected reds.** A test written in one task that can go green only when another task lands is listed under
  "Expected red until" in its task, with the task that turns it green and the only failure text that counts as
  expected. Any other red at a gate is a real red. In-group expected reds are green at their own group gate.
- **Main-loop lanes.** Steps longer than ~5 minutes run only in the main loop, from scripts the tasks write:
  `specs/krate-manager-phase5/evidence/wsl-lane.js` (T001; L-WSL, a cold WSL build at the G1 gate) and
  `specs/krate-manager-phase5/evidence/close-lane.js` (T016; the release `npm run tauri build`, the roster scan of the
  built binary, L-WSL again, the full plan §7.4 procedure). T018 returns with notes "deferred to the main loop at close:
  run `node specs/krate-manager-phase5/evidence/close-lane.js`".
- Never commit, never push, never trigger or dispatch CI. Those are the user's steps.

### Rules every task obeys (a task that breaks one is defective)

1. **Never touch the real system.** Every test that writes builds its `AppContext` / roots from
   `test_support::fresh_temp_roots()` (`crates/core/src/test_support.rs:122`, returns `(TempDir, Roots)`) and writes
   only into that state dir (spec FR-071, plan §6). Every network test uses `HttpConfig::loopback(…)` and a
   `FixtureServer` bound to `127.0.0.1:0` or `[::1]:0`; no fixed port, no global mutex (FR-086). No test calls
   `NativePlatform.system_proxy(…)`, `proxy_env::read()`, `os_proxy::pac_host()`, `OsPacHost`, `http::std_resolve`,
   `http::std_my_ip`, `sysproxy_cf::system_utc_offset`, any WinHTTP function (`WinHttpGetIEProxyConfigForCurrentUser`,
   `WinHttpOpen`, `WinHttpGetProxyForUrlEx` and the rest of PD-35), SystemConfiguration or `CFTimeZone`, or spawns
   `gsettings` or `date` (FR-049, FR-087, plan §6 "What tests never touch"); the bounded runner is tested only with
   `sleep` and `printf`. Every PAC test uses the `FakePacHost` of `pac_tests.rs` (TA-12) or `pac::InertPacHost` and
   loopback PAC URLs: no test resolves a real name, reads the real local address, clock or time zone, runs the real
   WPAD discovery or reaches the WinHTTP auto-proxy service. No test reads the process environment (core has no
   environment access: `apps/krate-manager/clippy.toml` `disallowed-methods` lists `std::env::var`, `var_os`, `vars`,
   `vars_os`, `home_dir`, `temp_dir`). No test, task or gate writes `%LOCALAPPDATA%\com.krateaudio.krate-manager`, a
   VST3 / Components / preset folder, `ProgramData` or the registry, launches the built app or contacts a
   non-loopback host. L-WSL runs the same core tests inside WSL (loopback only: `~/.wslconfig` `networkingMode=mirrored`,
   plan §1.2) and writes only its target dir `$HOME/km-target` inside WSL. Network use outside tests is limited to
   package registries (crates.io in T002, npm for `npx yaml`). `live_feed_parses` is never run by any task or gate
   (FR-072).
2. **`clippy::disallowed_methods` suppressions** are only `#[expect(clippy::disallowed_methods, reason = "…")]` on one
   statement, at the sites `injection.rs` `D6_SITES` lists (`crates/core/tests/injection.rs:70-77`: Phase 4's 7 sites;
   T002 adds `src-tauri/src/proxy_env.rs`, total 8, plan SI-3). Never `#[allow]` anything (`allow_attributes` /
   `allow_attributes_without_reason` are denied, `apps/krate-manager/Cargo.toml:24-26`); never an `expect` of
   `clippy::style`, `clippy::all` or `warnings`. Any other `#[expect]` needs a `reason` the reviewer accepts.
3. **Unit tests live in `<stem>_tests.rs`**, declared in `<stem>.rs` as `#[cfg(test)] #[path = "<stem>_tests.rs"] mod
   tests;` (no inline `mod tests {`, `injection.rs` `RULE_INLINE_TESTS`). The one exception is `pac.rs`, whose
   declaration is `#[cfg(test)] #[path = "pac_tests.rs"] pub(crate) mod tests;` so other test modules can use
   `FakePacHost` (TA-12).
4. **Literal hygiene in core** (Phase 4, `injection.rs:12-37`, enforced by `core_sources_hold_no_production_literal`):
   no new `crates/core/src` file except `test_support.rs` and `*_tests.rs` contains `krateaudio.com`,
   `api.github.com` or one of the forbidden path prefixes (`"C:\\`, `"/Library`, `"/usr/`, `"/home`, `"/Users`),
   comments included. The five platform-key strings are contract data and are allowed in `contract.rs` / `feed.rs`
   (plan §4.2.1). **No file under `apps/krate-manager/` names any of the nine shipped plugins**, tests included
   (ROSTER, FR-027, FR-082): test fixtures use `Alpha`, `Bravo`, `Charlie`, `Delta` (TA-16). The literal `gsettings`
   appears in no test source (plan §4.11.1).
5. **Zero warnings.** `cargo fmt --all --check`, `cargo clippy … -- -D warnings` with 0 `warning:` lines (on Windows and
   in L-WSL), `npm run typecheck`, `npm run lint`, `node --check`. Every `#[expect]` must be fulfilled.
6. **Parallel tests.** Never `--test-threads`, `--no-file-parallelism`, `--fileParallelism=false`, `--maxWorkers`,
   `-j 1`, `--test-concurrency=1`, `--test-isolation=none` (FR-086, SC-021). `cargo test` uses its default threads;
   `node --test` runs files in parallel.
7. **`cargo test` is `cargo test --workspace --locked` (or `-p krate-manager-core --locked`), never with
   `--all-targets`**: that flag builds a test harness for `src-tauri`'s lib and bin despite their `test = false`
   (`src-tauri/Cargo.toml:13,19`; Phase 4 ruling P-4, `.claude/workflows/krate-manager-phase.js:593`).
   `cargo clippy --workspace --all-targets` is correct.
8. **Lockfile.** Only T002 changes `Cargo.lock`, once, without `--locked` (`cargo metadata --format-version 1 >
   /dev/null`); every other cargo command in every task uses `--locked`. Never `cargo update` or `cargo
   generate-lockfile`. **No task after T002 adds a dependency or a feature**: a task that finds it needs one stops,
   returns `blocked` and names the crate and why (FR-080 makes every dependency the user's ruling).
9. **Placeholder discipline (TA-3).** T002's placeholders compile with zero warnings because: every parameter of a
   `todo!()` body is `_`-prefixed (rustc 1.99.1 warns `unused_variables` for a `todo!()` body's named parameters); a
   placeholder declares only `pub` items (an unused `pub(crate)` fn warns `dead_code`), except where a `pub` item of the
   same placeholder calls it; a placeholder struct has no private field that nothing reads (a `_`-prefixed field is
   fine). The owning task renames the parameters as it implements. At close no `todo!(` or `unimplemented!(` remains
   in any workspace source (TA-4, close-lane row A10): `run()` is never executed by a test, so a stub on its path would
   ship unnoticed.
10. **One working tree, many writers (G2, G3).** Write each file whole, so that it compiles at every save (types and
    signatures first, bodies after). Never edit a file another task owns; when `cargo` fails on a file you do not own,
    wait 60 s and retry, three times, then report it in your notes and continue with what you can verify. Run your
    own-module loop (L-OWN) in a private target dir with a share of the cores (TA-14):
    `CARGO_TARGET_DIR=F:/tmp/km5-target/<task id> CARGO_BUILD_JOBS=<share> cargo test -p krate-manager-core --locked
    <filter>` (share 3 for G2's eleven tasks, 16 for G3's one Rust task), so parallel tasks neither queue on one build
    lock nor oversubscribe the 32 cores. The first run pays one cold build of core's dependencies (`ring`, `ureq`,
    `rustls`, `regex`, `url`; `rquickjs-sys` is not built on Windows, PD-25); record its wall clock. Run the task's
    final L-RS once in the shared `apps/krate-manager/target` (warm `tauri`). Delete `F:/tmp/km5-target/<task id>`
    when the task ends (17 GB free on `F:` this session, `df -h /f`).
11. **Scope fence (spec FR-090, as amended by plan SI-41).** Outside `apps/krate-manager/**` only these paths change:
    `.github/workflows/manager-ci.yml`, `tools/feed-contract/tests/manager-ci-shape.test.js`,
    `.github/workflows/feed-tools.yml` (only its `PLAN` / names lines, and only if the TAP count moves),
    `specs/krate-manager-phase5/**`, `specs/INDEX.md` (regenerated), `specs/krate-manager/probes/results.md` (exactly one
    added table row, the R-15 measurement, plan §4.14; no other change), and in `specs/Krate-manager-roadmap.md` only
    the three places T001 edits plus the Phase 5 status line (main loop at close). No `tools/feed-contract/` schema,
    contract, listing or fixture file changes; fixtures are read in place.
12. **Line endings.** This checkout runs `core.autocrlf=true`. Every pre-existing file this phase edits is CRLF in the
    working tree (measured this session, `grep -c $'\r$'` = `wc -l`): `Cargo.toml` 33/33, `crates/core/Cargo.toml`
    22/22, `src-tauri/Cargo.toml` 38/38, `crates/core/src/lib.rs` 11/11, `context.rs` 203/203, `context_tests.rs`
    554/554, `test_support.rs` 130/130, `tests/injection.rs` 849/849, `tests/config_guard.rs` 1308/1308,
    `src-tauri/src/lib.rs` 70/70, `src-tauri/src/dev_feed.rs` 12/12, `tests/required-tests.json` 62/62,
    `manager-ci.yml` 266/266, `manager-ci-shape.test.js` 671/671, `feed-tools.yml` 154/154, the roadmap 1045/1045,
    `spec.md` 1638/1638, `specs/INDEX.md` 376/376, `specs/krate-manager/probes/results.md` 27/27. An edit keeps the
    file's ending on every line it adds. New files are written LF. `specs/INDEX.md` is written only by
    `tools/gen-specs-index.js`.
13. **Shell discipline.** Every command runs in the Bash tool (Git Bash), foreground, output to a log under
    `F:/tmp/gate/km5-<task id>/`, then `tail`; never `cat` a whole log, never a drive-wide `find` / `du`. No heredoc in
    a Bash command (a repo hook blocks them): write scripts with the Write tool and run them with `node`.
14. **No invented APIs.** Every crate API used is the one plan §1.2 verified: `ureq` 3.5.0, `rustls` 0.23.46,
    `rustls-platform-verifier` 0.7.1, `rquickjs` / `rquickjs-core` / `rquickjs-sys` 0.14.0, `windows-sys` 0.61.2,
    `system-configuration` 0.8.0, `core-foundation` 0.10.1, `tempfile` 3.27.0, `regex` 1.13.1, `base64` 0.23.1,
    `flate2` 1.1.10. Spot-checked this session in the local registry: `windows-sys-0.61.2`
    `src/Windows/Win32/Networking/WinHttp/mod.rs:7` (`WinHttpCreateProxyResolver`), `:10` (`WinHttpFreeProxyResult`),
    `:16` (`WinHttpGetIEProxyConfigForCurrentUser`), `:18` (`WinHttpGetProxyForUrlEx(hresolver: *const c_void, pcwszurl:
    PCWSTR, …)`), `:20` (`WinHttpGetProxyResult`), `:42` (`WinHttpSetOption`), `:44` (`WinHttpSetStatusCallback`), `:326`
    (`WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING = 2048`), `:643` (`WINHTTP_OPTION_CONTEXT_VALUE = 45`);
    `Foundation/mod.rs:7` (`GlobalFree`), `:2870` (`ERROR_IO_PENDING = 997`); `WINHTTP_CALLBACK_FLAG_HANDLES` is **not**
    exported (grep: no hit), so `os_proxy.rs` defines it as `winhttp.h:1089` does (plan §1.2); `rquickjs-core-0.14.0`
    `src/runtime/base.rs:50,124,158,167` (`new`, `set_interrupt_handler`, `set_memory_limit`, `set_max_stack_size`).
    An API that turns out to differ is a defect of the plan: record it under "Defects found" with the crate source line,
    and use the real API.
15. **Elapsed-time assertions.** The tests that bound wall clock (`offline_returns_cached_flagged_stale` < 5 s,
    `chain_shares_one_deadline` < 1.5 s, the `HeadersThenStall` case < 2 s, `script_failure_cases` and the
    `dns_bound_cases` engine row < `PAC_EVAL_BUDGET` + 1 s, the `dns_bound_cases` hang row < `PAC_DNS_WAIT` + 1 s,
    `one_attempt_per_check`, SC-034 and `os_pac_failure_cases`' blocking row < total + 1 s, the non-advancing row (c) <
    3 s, `bounded_call_abandons_after_wait` < 1 s, `bounded_run_abandons_and_reaps` < 0.9 s) assert deadlines the code
    enforces, not machine speed (plan §7.4). A group gate's L-RS runs with no other lane executing except L-WSL (4 WSL
    cores, plan §8). An L-OWN red on one of these while other tasks compile is rerun once alone before it is called a
    defect; the notes record both runs. Never widen a bound.
16. **`cfg` discipline (PD-25, plan §4.15).** Every item of `pac.rs` that names `rquickjs` or is used only by the engine,
    every PAC-fetch item of `http.rs` (`AgentPurpose::PacFetch`, `next_pac_hop`, `fetch_direct`, `PacState::Engine`,
    `std_resolve`, `std_my_ip`), and every test of them carries `#[cfg(not(windows))]`; the shared pure half compiles on
    every OS. `rquickjs` is absent from the Windows dependency graph, so an un-gated use fails to compile here, which is
    the mechanical proof. A `use` that only gated code needs is gated the same way (an unused import is a warning on
    Windows). Rust 2024 (`apps/krate-manager/Cargo.toml:8`) warns `unsafe_op_in_unsafe_fn`: inside an `unsafe fn` or an
    `unsafe extern "system" fn`, every unsafe operation sits in its own `unsafe { }` block with a `// SAFETY:` comment.

### Lanes (referenced by every task)

All app-lane commands run from `apps/krate-manager/`.

| Lane | Commands | Pass |
|---|---|---|
| **L-RS** | `cargo fmt --all --check`; `cargo clippy --workspace --all-targets --locked -- -D warnings`; `cargo test --workspace --locked` (Rule 7) | fmt exit 0; clippy exit 0 with 0 `warning:` lines; every test binary `0 failed` except the named expected reds |
| **L-OWN** | `CARGO_TARGET_DIR=F:/tmp/km5-target/<task> CARGO_BUILD_JOBS=<share> cargo test -p krate-manager-core --locked <filter>` (Rule 10; a working loop, not a gate) | the task's own tests |
| **L-DEVFEED** | `cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings` | exit 0, 0 `warning:` lines |
| **L-LIVE** | `cargo clippy -p krate-manager-core --all-targets --features live-network --locked -- -D warnings` (compiles `tests/live_feed.rs`; never runs it) | exit 0, 0 `warning:` lines |
| **L-GUARD** | the `run:` text of `manager-ci.yml` step `clippy-guard` (`:95-113`), executed with `C:/Program Files/Git/bin/bash.exe -c` from `apps/krate-manager/` with `RUNNER_TEMP=F:/tmp/gate/km5-<task>` and `RUNNER_OS=Windows` | `::notice title=clippy-guard (Windows)::rc=<non-zero> … other-errors=0` (Phase 4 form; unchanged by this phase) |
| **L-WSL** (main loop) | `node specs/krate-manager-phase5/evidence/wsl-lane.js` from the repo root (T001): in WSL, `CARGO_TARGET_DIR=$HOME/km-target`, `cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings && cargo test -p krate-manager-core --locked` (plan §7.4) | last line `WSL LANE GREEN …`; it never closes a PENDING-CI row (plan §7.3, §8). The first run (G1 gate) is cold: `$HOME/km-target` does not exist yet (`ls` this session) |
| **L-WSL-OWN** | after the G1 gate primed `$HOME/km-target` only: `timeout 300 wsl -e bash -lc 'cd /mnt/f/projects/iterum/apps/krate-manager && CARGO_TARGET_DIR=$HOME/km-target cargo test -p krate-manager-core --locked <filter>'` (and the same with `cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings`) | the task's `cfg(not(windows))` tests; a timeout (exit 124) is not a red: record it and leave the verdict to the next gate's L-WSL (TA-18). Incremental only: never run it on a fresh target dir |
| **L-XCHECK** | `cargo clippy -p krate-manager-core --all-targets --locked --target x86_64-unknown-linux-gnu -- -D warnings` and the same with `--target aarch64-apple-darwin` | **Close only (TA-7).** Both stop in a C build script (`ring`, and for these targets `rquickjs-sys`) with `failed to find tool "x86_64-linux-gnu-gcc"` / `failed to find tool "cc"` (measured, plan §1.2 R-8 row). That line is recorded verbatim as the skip; any error in our code, or any other build-script error, is a red. It never closes a PENDING-CI row |
| **L-TS** | `npm run typecheck`; `npm run lint`; `npm test` (no frontend file changes in this phase, FR-062; run because SC-001 names them) | each exit 0 |
| **L-NODE** | `node --check <each new or changed .js>`; `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" > F:/tmp/gate/km5-<task>/all.tap` from the repo root (quoted glob); read with `tail -12` | TAP `# fail 0`; no `Warning:` / `DeprecationWarning` / `ExperimentalWarning` line |
| **L-TAP** | `node specs/krate-manager-phase2/evidence/tap-check.js F:/tmp/gate/km5-<task>/all.tap` | exit 0 and `TAP plan=1..59 top-level=59` (plan §4.12: this phase adds only subtests; `feed-tools.yml:86` `PLAN = "1..59"`) |
| **L-YAML** | `npx --yes yaml@2.6.1 valid < <workflow file>` | exit 0 |
| **L-LINT** | `node tools/lint-serial-test-runs.js` (repo root) | exit 0 |
| **L-VLINT** | `node tools/lint-manager-version.js` (repo root) | exit 0, no stderr |
| **L-INDEX** | `node tools/gen-specs-index.js --check` (repo root) | exit 0 |
| **L-SCOPE** | `node specs/krate-manager-phase5/evidence/scope-check.js` (T015) | exit 0 |
| **L-CLOSE** | `node specs/krate-manager-phase5/evidence/close-lane.js` (T016; **main loop only**) | last line `CLOSE LANE GREEN` |

**Group gates** (the main loop runs them). After G1: L-RS (nothing else running, Rule 15), L-DEVFEED, L-LIVE (the
feature compiles; no `live_feed.rs` yet), L-GUARD, L-LINT, L-INDEX, and **L-WSL** (cold; it primes `$HOME/km-target`
for G2's L-WSL-OWN; expected green, the engine placeholders compile there for the first time). After G2 and G3: L-RS,
L-DEVFEED, L-LIVE, L-GUARD, L-LINT, L-WSL (beside L-RS). After G4: everything T018 lists, then L-CLOSE in the main loop.

---

## Preconditions — rulings needed before the build stage

R-1 to R-10 and R-12 to R-15 are decided (`spec.md:1585-1638`); R-11 is withdrawn. The plan leaves **two open rulings**,
both opened by R-15 (plan §0.3, `plan.md:162-163`; §10 `:2019-2032`), and asks that the spec edits of §0.2 land before
the build (`plan.md:41-43`). The R-15 measurement is already on record (`spec.md:1636-1638`); T001 copies it into
`results.md` (SI-41), so there is no measurement precondition any more.

- **R-16 (which WinHTTP call evaluates PAC on Windows):** recommended `WinHttpGetProxyForUrlEx` (PD-35: documented
  "always executes out-of-process"). This file is written against it.
- **R-17 (`fAutoLogonIfChallenged`):** recommended `FALSE` (no domain credentials offered to a PAC server). This file is
  written against it.
- **The spec edits:** plan §0.2 SI-1 to SI-41 and this file's TA rows marked **spec**. Starting the build stage after
  the user's review of spec, plan and tasks is their acceptance (`plan.md:41-43`); T001 applies every row the spec does
  not already carry.

**Precondition check (T001 and T002 both run it first; on failure they change nothing and return `blocked`, quoting
the missing or differing item verbatim):** `spec.md` `## Clarifications` holds a bullet starting `- R-16` and one
starting `- R-17`, each recording the user's answer. Blocked also when R-16's answer is not `WinHttpGetProxyForUrlEx`
(plan §0.3 R-16 alternatives (i) / (ii) change `os_proxy.rs`'s call, the `OsProxyEntry` shape in `pac.rs`, the
`winhttp_answer_cases` rows, and withdraw SI-38: T002, T008, T010 and T001's SI-38 edit need re-planning), when R-17's
answer is not `FALSE` (alternatives (i) / (ii) change `os_proxy.rs`'s options and, for (ii), FR-059's one-attempt
rule), or when a bullet in `## Clarifications` marks any SI or TA row of this file rejected (every task is written
against the accepted rows). The main loop then re-plans the named sections and regenerates the affected tasks.

### Task-list amendments (TA)

Rows marked **spec** change spec text (T001 writes them, tagged `(tasks TA-n)`); the others explain why a task differs
from a plan block.

| Id | Amendment | Why | Spec? |
|---|---|---|---|
| TA-1 | T002 amends the existing exact-`[features]` check of `crates/core/Cargo.toml` (`config_guard.rs:725-731`, `if features != ["test-support = []"]` at `:726`) to `["test-support = []", "live-network = [\"test-support\"]"]` with the message updated; T012 adds no second copy | Without it T002's manifest edit turns `real_app_metadata_passes_the_guard` (`config_guard.rs:1178`) red; plan §4.11.2 did not name the existing check | spec: FR-063 gains "the Phase 4 check of `crates/core/Cargo.toml` `[features]` (`config_guard.rs:725-731`) is that assertion, amended to exactly the two lines" |
| TA-2 | SC-002's `accept-without-platforms` "the named plugin resolves `NotThroughManager`" moves from `contract::tests::c1_cases_reproduce` (which keeps "0 findings") to `feed::tests::accept_rows_parse_through_the_client`, over the four FR-034 pairs | `contract` cannot call `version::resolve` on a model it does not build | spec: SC-002 |
| TA-3 | Rule 9 (placeholder discipline) | rustc 1.99.1 (`rustc --version`): named `todo!()` parameters warn `unused_variables`; an unread private field and an unused `pub(crate) fn` warn `dead_code` | no |
| TA-4 | SC-001 adds: "no `todo!(` or `unimplemented!(` occurs in any workspace `.rs` file outside `*_tests.rs` and `tests/` (`grep -rn`, 0 lines)" | a placeholder on `run()`'s path is never executed by a test | spec: SC-001 |
| TA-5 | `ProxyInputs`' three Linux fields are `#[cfg(target_os = "linux")]` | on Windows and macOS nothing reads them and a derived `Debug` is not a read, so they would warn `dead_code` (plan §4.10 declares them unconditionally) | no |
| TA-6 | The [P] reading of "How to read this file" | placeholder files exist before their [P] owner touches them; the instruction's own pattern makes them the owner's files | no |
| TA-7 | L-XCHECK runs at close only and records the build-script skip | `ring` (through `ureq`'s `_ring`) and, for the two foreign targets, `rquickjs-sys` compile C for the target, so from this box neither cross target reaches our code (plan §1.2 R-8 row measured both failures, §8) | no |
| TA-8 | New `fixture_server::tests::replies_behave_as_scripted` (every `Reply` variant, `set_route`, `requests()`, the `0x16` answer, `ConnectOk` re-serve, `Drop` ending `Stall` / `Delayed` / `HeadersThenStall` at once) joins FR-085's `cargo.all` | some seventy socket tests trust the server's replies; nothing else tests them | spec: SC-021, FR-070, FR-085 |
| TA-9 | SC-030's "`check` returns the cached copy" for `ConnectRefused(407)` and `Close` moves from `http::tests::proxy_path_connect` (which asserts `http::get` → `E_FEED_OFFLINE`) to `feed::tests::check_through_proxy_writes_cache` | `http` has no cache | spec: SC-030 |
| TA-10 | T002 writes `src-tauri/src/lib.rs`'s final wiring (plan §4.10) against placeholders; T012 writes its guard afterwards with plants as the red | `lib.rs` is a shared file and is edited once, in G1; the guard's red-first evidence is its plants (the Phase 4 `injection.rs` pattern) | no |
| TA-11 | T002 also implements the shared capture logger (`test_support::install_capture` / `take_mine`, plan §4.8) and migrates `context_tests.rs`, implements `src-tauri/src/proxy_env.rs` whole (a copy step, plan §4.10), raises `D6_SITES` / `D6_TOTAL`, sets `FR007_DEPENDENCIES` to FR-080's ten and widens the Phase 4 env assertion's file list to `["os_roots.rs", "proxy_env.rs"]`; T013 later replaces that assertion with plan §4.11.1's `src_tauri_env_findings` rules | every G2 test task asserts `warn!`s through the logger, and the manifest edit and `proxy_env.rs`'s one `#[expect]` turn three Phase 4 guards red unless the constants change in the same task | no |
| TA-12 | `pac.rs` declares `pub(crate) mod tests`; T002 writes `FakePacHost` in `pac_tests.rs` (with its self-check `pac::tests::fake_pac_host_answers_from_its_table`, not in `required-tests.json`) and its constructor `FakePacHost::handle`, the only `PacHostHandle::new(` in any test source; `http_tests.rs` and `feed_tests.rs` use `crate::pac::tests::FakePacHost` | plan §4.15 puts the double in `pac_tests.rs`, but `http_tests.rs` (SC-018, SC-034, SC-035, SC-041) and `feed_tests.rs` (SC-036) need it, and a private `tests` module is unreachable from them; one constructor makes §4.11.1's "`PacHostHandle :: new (` only with a `FakePacHost` argument" mechanically checkable | spec: FR-087 (the `PacHostHandle::new(` rule reads "only inside `impl FakePacHost` in `pac_tests.rs`") |
| TA-13 | **Withdrawn.** The previous list's FR-090 amendment is no longer needed: FR-090 already admits the Phase 12 RustSec sentence (`spec.md:886-898`), and SI-41 admits the `results.md` row | the spec revision of 2026-10-31 and plan SI-41 | no |
| TA-14 | L-OWN runs with `CARGO_BUILD_JOBS` 3 (G2, eleven tasks) or 16 (G3, one Rust task) in a private target dir | eleven cold builds of `ring` + `ureq` + `regex` + `url` at full width would put ~350 jobs on 32 cores (CLAUDE.md "Concurrent gates share the box") | no |
| TA-15 | Every new manifest entry is written on one line and carries no trailing comment: a reason goes on a full-line `#` comment above the entry | `section_lines` / `key_of` (`config_guard.rs:273-292`) and `section_keys` (`injection.rs:537-560`) read one line per entry and compare lines exactly (`core-foundation = { workspace = true }`, plan §4.11.1); plan §4.1's trailing `# R-n` comments would fail those exact matches | no |
| TA-16 | `asset_name_cases`' kept-name row uses `Alpha-1.0.0-Windows-x64.exe`, not plan §7.1 SC-005's `Iterum-1.0.0-Windows-x64.exe` | Rule 4 (ROSTER): no file under `apps/krate-manager/` names a shipped plugin; FR-082's scan excludes `*_tests.rs`, so only this rule catches it | no |
| TA-17 | G1 runs T001 (spec, roadmap, probes row, INDEX, WSL lane script) and T002 (the app foundation) in parallel; each runs the precondition check itself | their files are disjoint and T002 needs no spec text, only the rulings; serialising them would put T001's ~40 spec edits on the critical path for nothing | no |
| TA-18 | L-WSL is a main-loop lane (`evidence/wsl-lane.js`, T001), run at every group gate and at close; the G1 gate's run is the cold build that primes `$HOME/km-target`. Tasks with `cfg(not(windows))` code (T006, T008, T009, T014) may use the incremental L-WSL-OWN loop under `timeout 300`; a timeout is recorded, not a red | the workflow rule makes a WSL Linux build a main-loop lane, but the engine half of `pac.rs` and the PAC fetch compile nowhere else on this box (R-15, plan §8), so an incremental local signal before the gate saves a gate-fixer round | no |
| TA-19 | The Phase 12 RustSec bullet is written as **one physical line** (not wrapped) | SC-044 asks for "one line naming `rustsec.org`, `rquickjs`, `rquickjs-core` and `rquickjs-sys` and a pin bump" and plan §7.4 records it with `grep -n 'rustsec.org'`; a wrapped bullet would split the crate names across lines | no |
| TA-20 | The two guard tasks (`config_guard.rs`, `injection.rs`) run in G2 beside the modules they scan, not in G3 | their red-first evidence is their plants, and their real-tree verdict is the G2 / G3 gates'; in G3 they would sit beside the `feed` long pole for nothing, in G2 they shorten G3 to `feed` alone | no |
| TA-21 | In the PAC and OS-path tests a static proxy enters the config by struct update (`HttpConfig { proxy: Some(..), ..cfg }`), never `.with_proxy(` | FR-087 keeps `.with_proxy(` to the five proxy-path bodies (plan §4.11.1); `pac_path_direct`, `pac_fetched_once_never_via_proxy`, `pac_fallback_to_static_proxy`, `os_pac_walk` and `os_pac_failure_cases` all need `proxy: Some(B)` | no |
| TA-22 | `FakePacHost`'s hanging `resolve` bounds itself (its own worker thread plus `recv_timeout(wait)`, the shape of `http::std_resolve`), and its OS mode answers `os_find_proxy` from a scripted queue (entries, a `PacError`, or a call that blocks on a channel the test holds) | T002's self-check must not wait on T009's `http::bounded`; `dns_bound_cases` (SC-042) needs a lookup that "never answers" whose worker ends later, and `os_pac_failure_cases` (SC-041) a call that never returns, both without a kill (plan §4.15 last paragraph) | no |
| TA-23 | `os_pac_walk` gains one row: under `LoopbackOnly`, the injected `[Http example.invalid:80]` → `E_FEED_URL`, no fixture server records anything | plan §4.5.1 "Either evaluator: each proxy of `Ok(choices)` is checked by `check_proxy` before any socket" has a test only on the engine path (`pac_policy_cases`' `cfg(not(windows))` block); on this box the OS path is the one that runs | no |
| TA-24 | The release-binary base size is read now from the existing `target/release/krate-manager.exe` (4,269,056 bytes, mtime 2026-10-30 22:21, built by the Phase 4 close lane from the tree committed as `ac307f7e`, the last app commit) and written by T002 to `evidence/base-binary-size.txt` before anything rebuilds it | plan §4.1 asks for the size "at the phase's base commit and at this close"; the app sources at `a145271b` equal `ac307f7e`'s, so no base rebuild is needed | no |
| TA-25 | SI-38's rewrite to `WinHttpGetProxyForUrlEx` also covers the two spec sentences SI-38 does not list that name the old call: D16 (`spec.md:268`) and FR-066 (`:590`, "reached through `WinHttpGetProxyForUrl` with auto-detect") | `grep -n WinHttpGetProxyForUrl spec.md` this session finds them beside FR-092 / FR-073(b); left alone they would contradict the ruled call | spec: D16, FR-066 |

---

## Dependency overview

| Group | Mode | Tasks | Needs |
|---|---|---|---|
| G1 Foundation | **[P]** (2) | T001 precondition, spec rows, roadmap, probes row, INDEX, `wsl-lane.js` · T002 precondition, base size, manifests, lockfile, every shared line, all placeholders, capture logger, `FakePacHost`, `proxy_env.rs`, `AppContext.http`, `lib.rs` wiring | Preconditions |
| G2 Modules and guards | **[P]** (11) | T003 `fixture_server` · T004 `version` · T005 `contract` · T006 `sysproxy` · T007 `sysproxy_cf` · T008 `pac` · T009 `http` · T010 `src-tauri/src/os_proxy.rs` · T011 `tests/roster.rs` + `tests/live_feed.rs` · T012 `config_guard.rs` · T013 `injection.rs` | G1 |
| G3 Feed and close scripts | **[P]** (3) | T014 `feed` · T015 `evidence/scope-check.js` · T016 `evidence/close-lane.js` + `evidence/model-range-node.js` | G2 |
| G4 Integration | sequential | T017 `manager-ci.yml`, `required-tests.json`, the shape test · T018 the short close run, lints, cleanup, PENDING-CI | G3 |

Why this shape: T002 writes every shared line up front, so G2 holds every module whose dependencies are only T002's
types and the two guards that scan them; the long poles (`contract`, `pac`, `http`, `os_proxy`) run side by side.
`http`'s socket tests wait only for the small `fixture_server` and its OS-path tests for `pac`'s `os_answer` /
`parse_answer`, all in the same group (in-group expected reds). `feed` needs a working `contract`, `http`, `pac`,
`fixture_server` and `version`, so it is G3's only Rust task, beside the two close scripts (new files). G1 is two
independent tasks in parallel; G4 two dependent tasks (T018 runs T017's workflow steps); no group is a chain of
independent tasks.

**SC owners:** SC-001 T018 (+ L-CLOSE) · SC-002 T005 (`c1_cases_reproduce`) + T014 (`accept_rows_parse_through_the_client`,
TA-2) · SC-003 T005 + T014 · SC-004, SC-005 T014 · SC-006, SC-007 T004 (green once T014 lands) · SC-008 T004 · SC-009 to
SC-011 T014 · SC-012 T009 · SC-013, SC-014 T014 · SC-015 T011 (Phase 4 tests stay green) · SC-016 T017 (step) + L-CLOSE
B5 · SC-017 T002 (manifests) + T013 (guards) + L-CLOSE (`cargo tree`) · SC-018 T002 (`http_config_is_carried`) + T009
(`config_for_dev_url_cases`) + T012 (guard) · SC-019 T011 + T017 (shape ban) + L-CLOSE (`--list` records) · SC-020 T005 ·
SC-021 T003 + T013 + T017 · SC-022 T002 (logger) + T014 · SC-023 T017 + T018 · SC-024 T014 · SC-025 T005 · SC-026 T009 +
T013 · SC-027 T004 · SC-028 T009 · SC-029 T006 (+ T010, the readers' compile) · SC-030 T003 + T009 + T014 (TA-9) · SC-031,
SC-032 T014 · SC-033 T008 · SC-034 T009 · SC-035 T008 (`wpad_candidate_cases`) + T009 · SC-036 T008 + T009 + T014 ·
SC-037 T014 (+ T016's Node parity record) · SC-038 T002 (manifests) + T013 + L-CLOSE (`cargo tree`) · SC-039 T006 + T013 ·
SC-040 T007 + T013 · SC-041 T008 (`winhttp_answer_cases`) + T009 (`os_pac_walk`, `os_pac_failure_cases`) + T010 (the real
host, untested by design) · SC-042 T008 · SC-043 T008 (`pac_clock_cases`) + T006 (`parse_utc_offset_cases`,
`offset_range_cases`) · SC-044 T001 (the sentence) + L-CLOSE (the record).

---

## G1 — Foundation [P] (two tasks, disjoint files, TA-17)

### T001 [P] — Precondition check, the spec amendments, the roadmap edits, the probes row, `specs/INDEX.md` and `evidence/wsl-lane.js` (spec FR-090, FR-091, FR-093, SC-044 sentence; plan §0.2, §0.3, §4.14, §7.4 L-WSL, §8, SI-41, TA-18, TA-19)

**Files:** `specs/krate-manager-phase5/spec.md` (edit, CRLF), `specs/Krate-manager-roadmap.md` (edit, CRLF),
`specs/krate-manager/probes/results.md` (one added line, CRLF), `specs/INDEX.md` (regenerated), new
`specs/krate-manager-phase5/evidence/wsl-lane.js` (LF).

**Precondition.** Run the check of "Preconditions" above first; on failure return `blocked`, changing nothing.

**Test first.** Docs: before editing, record in the notes that the amendments are absent (`grep -c "(plan SI-"
spec.md` prints `0`; the spec still names `platform::tests::linux_desktop_choice_cases` at `spec.md:1305` and
`WinHttpGetProxyForUrl` without `Ex` in FR-092 at `:634`); `git rev-parse --short HEAD` (expected `a145271b`): the
**scope base** T015's scope check diffs against; and `node tools/gen-specs-index.js --check`'s exit code. For
`wsl-lane.js`: write its `--self-test` first against a stub `verdict()` that returns GREEN for every log; it must fail;
quote the line.

**Implement — spec rows.** A row counts as already applied when the spec text already says what its fix says (tagged
or not; note "already present" and the line). Otherwise edit it in place and append `(plan SI-n)` or `(tasks TA-n)` to
the changed sentence. Plan §0.2's "Proposed fix" column is the binding wording for SI-1 to SI-41, with these readings:
SI-13 and SI-14 change nothing; SI-23 and SI-32 are withdrawn (change nothing); SI-24, SI-30, SI-33 and SI-37 were
applied by the spec revision (verify, note the lines); SI-5 points D15 at "plan §3, the full code table"; **SI-16's new
FR already exists as FR-029** (`spec.md:388`, ruled R-7): add only what FR-029 lacks (the `"size": 2.0` sentence and
FR-056's "one per asset or v1 entry the model drops or sanitises (PD-8, PD-21, PD-22)") and note "already present" for
the rest; SI-25: FR-064's `pac::evaluate(…, host: &dyn PacHost)` (`spec.md:563`) becomes `host: PacHostHandle` and the
`PacHost` method list of SI-25's fix is written into FR-064's "the plan fixes the signatures" sentence; SI-38 rewrites
FR-092 (`:633-645`), FR-073(b) (`:790`) and SC-041's `winhttp_answer_cases` rows (`:1329-1332`) to the Ex call and
the entry list, as R-16 ruled, and per TA-25 also D16's and FR-066's mentions of the call (`:268`, `:590`; the
Clarifications bullet `:1632` is the ruling record and is not edited); SI-39 amends FR-085 (`:849-855`) and SC-035; SI-40 amends SC-041's
`os_pac_failure_cases` warn codes; SI-41 adds the `results.md` sentence to FR-090 (`:886-898`) and the `1 0` numstat to
SC-044 (`:1348-1352`). Then the TA rows marked **spec**: TA-1 (FR-063), TA-2 (SC-002), TA-4 (SC-001), TA-8 (SC-021,
FR-070, FR-085), TA-9 (SC-030), TA-12 (FR-087), TA-25 (D16, FR-066).

**Implement — roadmap (plan §4.14, FR-090, FR-093).** Keep CRLF on every changed or added line.
- Lines 534-535: replace the sentence that starts mid-line 534, `A manual "check now" bypasses caches (Pages edge cache
  is` … `~10 minutes, feed_and_ci.md "Hosting").`, with `A manual "check now" always fetches from the network with
  \`Cache-Control: no-cache\` / \`Pragma: no-cache\`; the Pages edge ignores them and may serve a copy up to 600 s old
  (measured 2026-10-30).` The text of line 534 before `A manual` stays byte-identical; re-wrap at the file's ~120
  columns, continuation lines indented two spaces.
- After line 571 (the Phase 6 bullet ending `…rendered with no remote content), read-only.`) insert one bullet, wrapped
  the same way: `- 🆕 Phase 6 owns the "check now" control, whose text states that a new release can take up to ten
  minutes to appear (\`feed::EDGE_MAX_AGE_SECS\`). Phase 6 reads the running OS version through \`Platform\` and passes
  it to Phase 5's \`version::resolve\`, which compares it with the asset's \`min_os\` (\`Availability::NeedsNewerOs\`).`
- After line 897 (the Phase 12 "Release readiness" bullet ending `…with \`platforms\` (no plugin re-release).`) insert
  one bullet as **one physical line** (TA-19): `- 🆕 Before every manager release, check the RustSec advisory database
  (https://rustsec.org/) for \`rquickjs\`, \`rquickjs-core\` and \`rquickjs-sys\`, the vendored QuickJS-ng PAC engine
  that Phase 5 pins for the macOS and Linux builds (R-15); an advisory is answered with a pin bump before the release.`
  (plan §4.14's sentence verbatim, written on a single line).
- Leave the Phase 5 status line to the main loop at close.

**Implement — `results.md` (plan §4.14, SI-41).** Insert exactly one table row after line 20 (the last row, `| Which
\`moduleinfo.json\` Version should the manager trust? | …`), in the file's three-column form (`results.md:6-7`):
`| Windows: is "Automatically detect settings" (WPAD) on for a user profile? | **On, on the user's own (not fresh) Windows
11 profile**: no PAC URL, no manual proxy. A fresh-profile default was not measured. | Read-only, 2026-10-31: HKCU
\`…\Internet Settings\Connections\` value \`DefaultConnectionSettings\`, flags byte \`0x09\` (spec Clarifications R-15).
Consequence: the Windows PAC path (the OS evaluation, plan PD-35) runs by default on such a profile when no environment
proxy is set. |` on one line, CRLF. Nothing else in that file changes (`git diff --numstat -- results.md` = `1 0`).

**Implement — `evidence/wsl-lane.js`** (L-WSL, plan §7.4 and §8; follow `specs/krate-manager-phase4/evidence/
close-lane.js`'s form: shebang, a header comment with WHY / usage, `'use strict'`, `child_process.spawnSync` with an
argument array, exit 0 / 1 / 2, `--self-test`):
- `toWslPath(winPath)` (pure): `F:\projects\iterum` → `/mnt/f/projects/iterum` (drive letter lower-cased, `\` → `/`);
  the repo root is `path.resolve(__dirname, '..', '..', '..')`, never a hard-coded path.
- Preflight: `wsl -e bash -lc 'command -v cargo && command -v cc'`; failing → last line `WSL LANE SKIPPED: <reason>`,
  exit 2 (recorded, never green).
- Run: `wsl -e bash -lc '<cmd>'` with `cmd` = `cd <wslRoot>/apps/krate-manager && export CARGO_TARGET_DIR=$HOME/km-target
  && cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings && cargo test -p krate-manager-core
  --locked`, stdout and stderr streamed into the log file (`--out <file>`, default
  `F:/tmp/gate/km5-wsl/wsl-core-<timestamp>.txt`; the close lane passes `evidence/wsl-core.txt`), a 40-minute ceiling
  (the cold build) after which our own child process tree is ended and the verdict is RED "ceiling".
- `verdict(log, exitCode)` (pure): GREEN only when the exit code is 0, the log holds at least one `test result:` line,
  every `test result:` line says `0 failed`, and no line starts with `warning:`; otherwise RED with the first reason.
  It also prints which of the thirteen `cfg(not(windows))` names of plan §4.13 appeared as `test <name> ... ok` (an
  information line for the gate, not a verdict). Last line `WSL LANE GREEN (<n> passed, <s> s)` or `WSL LANE RED:
  <reason>`; exit 0 / 1.
- `--self-test`: `toWslPath` over `F:\projects\iterum` and `C:\x\y`; `verdict` over five in-memory logs: a green log →
  GREEN; `test result: FAILED. 1 passed; 1 failed` → RED; a `warning: unused import` line → RED; exit code 101 → RED; no
  `test result:` line → RED.
It writes nothing but its log and stdout; it never runs `--ignored` and never passes `--features live-network`.

Then `node tools/gen-specs-index.js` and `--check` → exit 0.

**Done when:** every amendment carries its change and source tag (or "already present" with its line); the notes list
each edited row id, the scope base and the three roadmap hunk headers (`git diff -U0 -- specs/Krate-manager-roadmap.md
| grep '^@@'`: old lines 534-535, an insertion at 571, an insertion at 897); `grep -n 'rustsec.org'
specs/Krate-manager-roadmap.md` prints exactly one line, holding `rquickjs`, `rquickjs-core`, `rquickjs-sys` and `pin
bump`; `git diff --numstat -- specs/krate-manager/probes/results.md` is `1	0`; `grep -c $'\r$'` equals `wc -l` for
`spec.md`, the roadmap and `results.md`; `node --check` and `wsl-lane.js --self-test` exit 0 (do not run the lane
itself: it is the G1 gate's, TA-18). **Lane:** L-INDEX, L-NODE (`node --check` only).

### T002 [P] — Precondition, base size, manifests, lockfile, every shared line, the placeholder skeleton, the test doubles and `AppContext.http` (spec FR-040, FR-060, FR-063 part, FR-080 part, FR-088, SC-017 part, SC-018 part, SC-022 part, SC-038 part; plan §2, §4.1, §4.8, §4.9, §4.10, §4.15 API, PD-12, PD-14, PD-19, PD-25, PD-32, SI-3, TA-1, TA-3, TA-5, TA-10, TA-11, TA-12, TA-15, TA-22, TA-24)

This task makes every edit to a shared app file the phase needs (except the workflows, `required-tests.json` and the
shape test, T017), so G2 and G3 touch only their own files.

**Files (edit):** `apps/krate-manager/Cargo.toml`, `crates/core/Cargo.toml`, `src-tauri/Cargo.toml`, `Cargo.lock`,
`crates/core/src/lib.rs`, `crates/core/src/context.rs`, `crates/core/src/context_tests.rs`,
`crates/core/src/test_support.rs`, `src-tauri/src/lib.rs`, `src-tauri/src/dev_feed.rs`, `crates/core/tests/injection.rs`
(three constants and one assertion list), `crates/core/tests/config_guard.rs` (one check).
**Files (new):** placeholders `crates/core/src/{contract,feed,version,http,pac,sysproxy,fixture_server}.rs` and their
`_tests.rs`; `crates/core/src/sysproxy_cf.rs` and `sysproxy_cf_tests.rs`; `src-tauri/src/os_proxy.rs` (placeholder);
`src-tauri/src/proxy_env.rs` (final); `specs/krate-manager-phase5/evidence/base-binary-size.txt`.

**Precondition.** Run the check of "Preconditions" above first; on failure return `blocked`, changing nothing.
**Then, before any build (TA-24):** write `ls -l --time-style=long-iso target/release/krate-manager.exe` and `git log
--format='%h %ci' -1 -- apps/krate-manager` into `evidence/base-binary-size.txt` (expected 4269056 bytes, 2026-10-30
22:21, `ac307f7e`). Nothing in G1-G3 builds the release binary; only L-CLOSE does.

**Test first.**
- `context_tests.rs`: write `context::tests::http_config_is_carried` (plan §4.9, SC-018): with one helper `fn http() ->
  HttpConfig { HttpConfig::loopback(Duration::from_secs(1), Duration::from_secs(1)) }`, `assemble_with_os` stores it in
  `AppContext.http`; a second call with `HttpConfig { proxy: Some(ProxyConfig { url: "http://127.0.0.1:9".into(),
  no_proxy: vec![] }), ..http() }` stores the proxy; a third with `HttpConfig { pac: Some(PacSource { auto_detect: true,
  url: Some("http://127.0.0.1:9/p.pac".into()) }), ..http() }` stores the PAC source (struct update, never `.with_proxy(`
  / `.with_pac(`, FR-087); `reassemble_with_os` with new settings keeps `http` unchanged. Red: it does not compile
  against Phase 4's two-argument `assemble_with_os` (`context.rs:72`); quote the error line.
- `context_tests.rs`: change `startup_errors_are_logged` (`:496`) to call `test_support::install_capture()` and filter
  `test_support::take_mine()` by `STARTUP_LOG_TARGET` (`context.rs:17`), keeping its assertions; delete the file's own
  `Capture`, `CAPTURE`, `take_mine` (`:446-484`) and the `log::set_logger(&CAPTURE)…` line (`:497`) (the `static
  CAPTURE` holds a `Mutex`, which T013's FR-086 rule bans from test sources). Red: with `install_capture` / `take_mine`
  as `todo!()` it panics `not yet implemented`.
- `pac_tests.rs`: `pac::tests::fake_pac_host_answers_from_its_table` (TA-12, TA-22), every host, through
  `FakePacHost::handle()`'s `(Arc<FakePacHost>, PacHostHandle)`: names `{"a.test" → Some(10.0.0.1)}` → `resolve("a.test",
  1 s) == Some(10.0.0.1)`, `resolve("b.test", 200 ms) == None`, the log holds both `(name, wait)` pairs in order; a
  `hang("slow.test")` name → `resolve("slow.test", 200 ms) == None` within 1 s, `hangs_finished() == 0`, then
  `release_hangs()` → `hangs_finished() == 1` within 2 s (polled, never a fixed sleep); `my_ip()` and `now()` return the
  fixed values (`192.0.2.7`, `PacClock { unix_secs: 1_700_000_000, local_offset_secs: Some(3600) }`) and `now_calls()
  == 1` after one call; `wpad_candidates(5 s)` returns the injected `["http://127.0.0.1:1/w.dat"]` and logs the wait; a
  default fake has `os_evaluates() == false` and `os_find_proxy` → `Err` code `PacCode::Fetch`; an `os_mode()` fake with
  the queue `[Entries([Direct entry]), Fail(PacError Script), Block(rx)]` answers the first two in order, and the third,
  called on a spawned thread, returns `Err` within 1 s of the test dropping its `Sender` (`os_finished()` counts it); an
  empty queue → `Err` `Fetch`; `os_calls()` holds each `(url, wait)`; two clones of one handle are `==`, a handle of a
  second fake is `!=` (`Arc::ptr_eq`); `format!("{:?}", handle) == "PacHostHandle"`. Red: `PacHostHandle::new` as
  `todo!()` panics.

**Implement.**
1. **Manifests**, plan §4.1 with TA-15 (one line per entry, reasons on a full-line `#` comment above).
   `[workspace.dependencies]` (`Cargo.toml:13-22`, alphabetical) gains `base64 = "0.23.1"`, `core-foundation =
   "0.10.1"`, `flate2 = "1.1.10"`, `regex = "1.13.1"`, `rquickjs = "0.14.0"`, `rustls = { version = "0.23.46",
   default-features = false }`, `system-configuration = "0.8.0"`, `ureq = { version = "3.5.0", features =
   ["platform-verifier", "socks-proxy"] }`, `windows-sys = { version = "0.61.2", features = ["Win32_Foundation",
   "Win32_Networking_WinHttp"] }` (exactly two features; no `Win32_System_Time`, SI-32 withdrawn).
   `crates/core/Cargo.toml` (`:10-19`): `[features]` exactly `test-support = []` and `live-network = ["test-support"]`;
   `[dependencies]` exactly the ten FR-080 keys `base64`, `log`, `regex`, `rustls`, `serde`, `serde_json`, `tempfile`,
   `thiserror`, `url`, `ureq`, each `= { workspace = true }`; `[dev-dependencies] flate2 = { workspace = true }`;
   `[target.'cfg(not(windows))'.dependencies] rquickjs = { workspace = true }` (the only entry, no `features` /
   `default-features` key: PD-25, R-15); `[target.'cfg(target_os = "macos")'.dependencies] core-foundation = {
   workspace = true }`. `src-tauri/Cargo.toml` (its `[dependencies]` stays the five keys of `config_guard.rs:32-33`):
   `[target.'cfg(windows)'.dependencies] windows-sys = { workspace = true }` and `[target.'cfg(target_os =
   "macos")'.dependencies] system-configuration = { workspace = true }`. Then `cargo metadata --format-version 1 >
   /dev/null` once (Rule 8; the lock is target-independent, so it gains `rquickjs*` although Windows never builds them,
   plan §4.1) and a first `cargo build --workspace --locked`; record the cold wall clock (`ring`, `ureq`). A
   build-script failure of `ring` on this box is a precondition stop (plan §9 row 1): return `blocked` with the error
   line.
2. **Phase 4 guards the manifest and `proxy_env.rs` would turn red** (keep them green, nothing else in either file
   changes here): `injection.rs:83-84` `FR007_DEPENDENCIES` becomes `[&str; 10]` = `["base64", "log", "regex",
   "rustls", "serde", "serde_json", "tempfile", "thiserror", "url", "ureq"]` (FR-080; `rquickjs` is not in it);
   `D6_SITES` (`:70-76`) gains `("src-tauri/src/proxy_env.rs", 1)` (array length 6) and `D6_TOTAL` (`:77`) becomes `8`
   (the test keeps its name `disallowed_methods_expectations_are_the_seven_d6_sites`, `:732`, so `required-tests.json`
   keeps its line); in `src_tauri_names_feed_host_once_and_reads_env_only_in_os_roots` (`:826-849`) the expected
   `var_os` list becomes `["os_roots.rs", "proxy_env.rs"]` (`rs_files` sorts, `:506-511`); `config_guard.rs:725-731`
   per TA-1.
3. **`crates/core/src/lib.rs`** (`:1-11`): add `pub mod contract; pub mod feed; pub mod http; pub mod pac; pub mod
   sysproxy; pub mod version;`, `#[cfg(target_os = "macos")] pub mod sysproxy_cf;` and `#[cfg(any(test, feature =
   "test-support"))] pub mod fixture_server;` (plan §2), alphabetical with the Phase 4 lines.
4. **Placeholders** (Rule 9, Rule 16). Each `<stem>.rs` starts with a `//!` line naming its plan section, declares its
   test module (Rule 3; `pac.rs` per TA-12), and holds the module's **`pub` surface exactly as the plan section gives
   it** (§4.2.1, §4.3.1-§4.3.3, §4.4, §4.5, §4.6, §4.7, §4.15: names, fields, derives, signatures, and each `cfg`
   attribute). Where the plan lists no derives and a test compares or prints the value, derive `Clone, Debug, PartialEq`
   (plus `Eq` where every field allows): `MinisigText`, `TrustedComment`, `Hop`, `WinIeProxyConfig` (also `Default`),
   `GnomeRaw` (also `Default`), `BoundedRun` (`Debug`), `WinIeRaw` (`Clone, Copy, Debug`). Each `<stem>_tests.rs`
   holds a `//!` line (and `use super::*;` only where it compiles without an unused-import warning).
   **Implemented now** (plain data other modules' types or this task's tests need): every constant of §4.3.1, §4.5 and
   §4.15 (`http::MAX_PAC_BYTES` is `pub use crate::pac::MAX_PAC_BYTES;`; `pac::helpers::MAX_SHEXP_SUBJECT`,
   `MAX_SHEXP_PATTERN`, `MAX_HOST_ARG`); `RuleCode::as_str`; `ContractError` and its `#[error]` texts;
   `PlatformKey::as_str` / `parse`; the four newtypes `PlainName`, `AssetUrl`, `PageUrl`, `AssetName` declared `pub
   struct <T>(String);` with `as_str` and no derive but `Clone, Debug, PartialEq, Eq`; `PluginEntry.download_url:
   Option<PageUrl>` (PD-22); `CacheStep::as_str` (the `SyncDir` arm `#[cfg(unix)]`); `FeedError` and `HttpError` with
   the §3 variants, `Display` beginning with the code, `code(&self) -> &'static str`, `Clone` (`FeedError::Http(#[from]
   HttpError)` transparent, PD-16); `Arch::native()`; `OsVersion::components()`; the `OsVersion` `PartialEq` / `Eq` /
   `PartialOrd` impls delegating to `Ord::cmp` (whose body is `todo!()`); `version::resolve_native` as its final one
   line `resolve(entry, NativePlatform.os(), Arch::native(), running)` (FR-036's single call site);
   `HttpConfig::production()` / `loopback(…)` / `with_proxy(…)` / `with_pac(…)` / `with_pac_host(…)` (T009's
   `production_values` pins them); `http::redact_userinfo` and `http::pac_url_for_log` (`pub(crate)`, PD-19, both used by
   the `Debug` impls below; T009 tests them); the hand-written `Debug` of `ProxyConfig` (and its `Display`),
   `ProxyVars`, `ProxyEnv`, `PacSource` and `sysproxy::SystemProxy`, exactly as PD-19 states; in `pac.rs` (shared
   half): `PacClock`, `OsProxyKind`, `OsProxyEntry`, the `PacHost` trait with plan §4.15's six defaulted methods,
   `PacHostHandle` (`new`, `get`, `Debug` = `"PacHostHandle"`, `PartialEq` = `Arc::ptr_eq`), `InertPacHost` (`impl
   PacHost for InertPacHost {}`; it reads no clock), `ProxyChoice`, `PacCode::as_str`, `PacError`; engine half (each
   item `#[cfg(not(windows))]`): `PacLimits` and `PacLimits::production()`. **Everything else is `todo!()`:**
   `contract::{validate, parse_minisig_text, parse_trusted_comment, check_install_value}`, `feed::{parse, check}`, every
   `version` function except the two above, `http::{ProxyEnv::from_vars, check_url, check_proxy, check_pac_url,
   next_hop, bypasses, resolve_proxy, config_for_dev_url, get, bounded}` and the `#[cfg(not(windows))]`
   `http::{std_resolve, std_my_ip}`, `pac::{wpad_dns_candidates, clock_civil}`, every `pac::helpers` function (`pub
   struct Civil { _private: () }`, `pub enum Arg { Num(f64), Text(String) }`: T008 may change both), the
   `#[cfg(not(windows))]` `pac::{PacSession::open, PacSession::find_proxy, evaluate}` (`pub struct PacSession {
   _private: () }`; T008 replaces the fields), every `sysproxy` function (`take_win_ie` and `read_wide` stay `pub
   unsafe fn` with their `# Safety` docs), `sysproxy_cf::{from_cf_proxies, system_utc_offset}`, and every
   `FixtureServer` method (`pub struct FixtureServer { _private: () }`). No other `pub(crate)` item: each owner adds its
   own (`Compiled`, `compile_schema_text`, `ecma_pattern_to_rust`, `pointer`, `check_with`, `CacheIo`, `StdCacheIo`,
   `LOG_TARGET`s, the `MODEL_*` codes, `classify_schema_version`, `drop_sibling_findings`, `json_u64`, the newtypes'
   `checked` / `from_url`, `to_ureq_proxy`, `AgentPurpose`, `agent_config`, `map_error`, `is_no_response`,
   `next_pac_hop`, `pac_url_argument`, `parse_answer`, `os_answer`, `log_text`, `DnsMemo`, `gvariant_*`).
   `contract.rs`'s two `include_str!` paths are `"../../../../../tools/feed-contract/versions.v2.schema.json"` and
   `…v1…` (plan §4.2.1), so the build reads the committed schemas in place.
5. **`test_support.rs`** (plan §4.8, FR-088): `pub fn install_capture()` installs one process-wide logger through a
   `OnceLock` (`log::set_logger` once, `log::set_max_level(Trace)`), recording `(ThreadId, target, level, message)` for
   every target starting `krate_manager_core`; `pub fn take_mine() -> Vec<(String, log::Level, String)>` returns and
   removes the calling thread's records. The only `log::set_logger` in the workspace (T013 enforces it).
6. **`pac_tests.rs` `FakePacHost`** (TA-12, TA-22, plan §4.15 last paragraph, compiled on every OS): `pub(crate) struct
   FakePacHost` with a name → `Option<IpAddr>` table, a set of hanging names sharing one release gate (`Mutex<bool>` +
   `Condvar` fields, never a `static`), a fixed `my_ip`, a fixed `PacClock` with a `now()` call counter, an injected
   candidate list, an `os_mode` flag and a `Mutex<VecDeque<OsAnswer>>` with `pub(crate) enum OsAnswer {
   Entries(Vec<OsProxyEntry>), Fail(PacError), Block(std::sync::mpsc::Receiver<()>) }` (popped under the lock, blocked on
   outside it; `Block` returns `Err(PacError { Fetch, "released" })` when the test's `Sender` drops), logs of `(name,
   wait)` per `resolve`, the `wait` per `wpad_candidates` and `(url, wait)` per `os_find_proxy`, and `AtomicUsize`
   counters `hangs_finished` / `os_finished`. A hanging `resolve` spawns a worker that waits on the gate and itself waits
   `recv_timeout(wait)` for the worker's answer, returning `None` on timeout (TA-22). Builder methods (`new`, `name`,
   `hang`, `my_ip`, `clock`, `candidates`, `os_mode`, `os_answer`) and inspectors (`resolve_log`, `candidate_waits`,
   `now_calls`, `os_calls`, `release_hangs`, `hangs_finished`, `os_finished`); `pub(crate) fn handle(self) ->
   (Arc<FakePacHost>, PacHostHandle)` holding the file's only `PacHostHandle::new(`. Every PAC test of T008, T009 and
   T014 builds its host this way.
7. **`src-tauri`**: `proxy_env.rs` final (plan §4.10 verbatim, TA-5): `NAMES` (the three Linux names behind
   `#[cfg(target_os = "linux")]`), one `var_os` loop under one `#[expect(clippy::disallowed_methods, reason = "the proxy
   environment, read once at startup (FR-048, FR-063)")]`, a non-UTF-8 value stored as `None`, the six `ProxyVars`
   fields written as the exact pairs `https_upper: got("HTTPS_PROXY")`, `https_lower: got("https_proxy")`, `all_upper:
   got("ALL_PROXY")`, `all_lower: got("all_proxy")`, `no_upper: got("NO_PROXY")`, `no_lower: got("no_proxy")` (T012's
   needles), then `ProxyEnv::from_vars(&vars)`; on Linux `desktop`, `config_home`, `home`; no other logic.
   `os_proxy.rs` placeholder: `pub trait SystemProxyFromOs { fn system_proxy(&self, inputs: &ProxyInputs) ->
   SystemProxy; }`, `impl SystemProxyFromOs for NativePlatform { … todo!() }`, `pub struct OsPacHost;`, `#[cfg(windows)]
   impl PacHost for OsPacHost` overriding `os_evaluates` and `os_find_proxy` (`todo!()` bodies), `#[cfg(not(windows))]
   impl PacHost for OsPacHost` overriding `resolve`, `my_ip`, `now`, `wpad_candidates` (`todo!()` bodies), and `pub fn
   pac_host() -> PacHostHandle { PacHostHandle::new(Arc::new(OsPacHost)) }`. `dev_feed.rs` gains its final `pub fn
   http_config(url: &str, resolution: ProxyResolution, pac_host: Option<PacHostHandle>) -> HttpConfig {
   http::config_for_dev_url(url, resolution, pac_host) }`. `lib.rs` gains `mod os_proxy;`, `mod proxy_env;`, `use
   os_proxy::SystemProxyFromOs;` and the final `run()` wiring of plan §4.10 (`proxy_env::read()`,
   `resolve_proxy(&inputs.env, &NativePlatform.system_proxy(&inputs), &feed_url)`, the two `cfg(feature = "dev-feed")`
   arms with `HttpConfig::production().with_proxy(resolution.proxy).with_pac(resolution.pac)
   .with_pac_host(Some(os_proxy::pac_host()))` and `dev_feed::http_config(&feed_url, resolution,
   Some(os_proxy::pac_host()))`, then `assemble_context(roots, &feed_url, http)` replacing `lib.rs:34`; TA-10). Name the
   imports so neither build has an unused import (`HttpConfig` behind `#[cfg(not(feature = "dev-feed"))]`); the texts
   `HttpConfig::production()`, `.with_proxy(`, `.with_pac(`, `.with_pac_host(`, `os_proxy::pac_host()` and
   `proxy_env::read()` must appear in `lib.rs` (FR-063, T012's needles).
8. **`context.rs`** (plan §4.9, FR-060): `AppContext` (`:49-53`) gains `pub http: HttpConfig`;
   `assemble_context(roots, feed_url: &str, http: HttpConfig)` (`:66`), `assemble_with_os(os, roots, feed_url, http)`
   (`:72`); `reassemble_with_os` (`:133`) copies `prev`'s `http` like `feed_url`. Every two-argument call site in
   `context_tests.rs` (33 lines match `assemble_\|reassemble`, `grep -c` this session) passes `http()`; Phase 4's
   assertions are otherwise unchanged.

**Green:** `http_config_is_carried`, `startup_errors_are_logged`, `fake_pac_host_answers_from_its_table` and every
Phase 4 test pass; L-RS (0 warnings), L-DEVFEED, L-LIVE (the feature exists; `tests/live_feed.rs` does not yet),
L-GUARD. `cargo tree -p krate-manager-core -e normal` shows `ureq v3.5.0`, one `rustls v0.23.x`,
`rustls-platform-verifier v0.7.1` and no `rquickjs`; `cargo tree -p krate-manager-core -e normal --target
x86_64-unknown-linux-gnu` shows `rquickjs v0.14.0` (`cargo tree` resolves a foreign target without building it, plan
§4.1); record those lines. `grep -rn "set_logger" apps/krate-manager/crates apps/krate-manager/src-tauri` lists only
`test_support.rs`. The `cfg(not(windows))` placeholders are first compiled by the G1 gate's L-WSL.

**Done when:** all of the above; the notes list every placeholder file, the base size line, the cold-build wall clock
and the `cargo tree` lines.

---

## G2 — Modules and guards [P] (each task touches only its own files; L-OWN share 3)

### T003 [P] — `core::fixture_server` (spec FR-070, FR-086, SC-021 part, SC-030 part; plan §4.7, PD-7, SI-2, SI-22, TA-8)

**Files:** `crates/core/src/fixture_server.rs`, `crates/core/src/fixture_server_tests.rs`.

**Test first** (`fixture_server_tests.rs`, raw `std::net::TcpStream` clients, each test its own servers):
- `binds_loopback_port_zero`: `start(vec![])`'s `url("/x")` starts `http://127.0.0.1:` and `port() != 0`; two servers
  started at once have different ports; `start_v6(vec![])`'s `url("/x")` starts `http://[::1]:` (a bind failure
  panics: never skipped, plan §9 last row).
- `connect_to_remote_host_is_refused`: a server whose route table maps `example.invalid:443` and `localhost:443` to
  `Reply::ConnectOk`; a raw stream sends `CONNECT example.invalid:443 HTTP/1.1\r\nHost: example.invalid:443\r\n\r\n`;
  the answer starts `HTTP/1.1 403`, the stream reaches EOF, `requests()` holds one `CONNECT` with path
  `example.invalid:443`; the same on a second connection for `localhost:443`.
- `replies_behave_as_scripted` (TA-8): one case per `Reply` variant over a raw stream: `Ok` (200, `Content-Length`, the
  body, `Content-Type`); `Status(503)` (empty body); `Redirect { 302, "/b" }` (`Location: /b`); `Truncated {
  declared_len: 100, sent: 10 bytes }` (header 100, EOF after 10); `Close` (EOF, no bytes); `Garbage(b"xx")` (exactly
  those bytes); `Gzip { body }` (`Content-Encoding: gzip`, the bytes verbatim); `Stall(30 s)`, `Delayed { after: 30 s,
  … }` and `HeadersThenStall { 100, 10 bytes, 30 s }` each end within 1 s of dropping the server (`Instant`); an
  unknown path → `404`; a first byte `0x16` then a 5-byte record header declaring 3 bytes and those bytes → exactly
  `HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n`; `CONNECT 127.0.0.1:<port>` →
  `ConnectOk` → `HTTP/1.1 200 Connection established`, then a `GET /v` on the same stream is answered from the route
  table; `ConnectRefused(407)` → `HTTP/1.1 407`; `set_route` replaces a reply at the same port; `requests()` records
  method, path and headers in order; every response carries `Connection: close`.
Red: the placeholder methods panic `not yet implemented`.

**Implement** plan §4.7 verbatim: `std` only (no new dependency); one thread per connection; every read bounded (64 KiB
head, 16 KiB TLS record); one `Condvar` + shutdown flag that `Drop` signals for `Stall`, `Delayed` and
`HeadersThenStall`; `Drop` = set shutdown, `notify_all`, wake `accept` with one loopback `TcpStream::connect` (the only
`TcpStream::connect` in `fixture_server.rs`, inside `impl Drop`; T013 enforces it), join. Bind exactly `"127.0.0.1:0"`
and `"[::1]:0"` (T013's FR-086 port rule reads those literals). A `CONNECT` whose authority is not `127.0.0.1:<port>`
or `[::1]:<port>` is answered `403` whatever the route table says, and nothing is dialled. `Reply::Gzip` sends the
given bytes; tests build them with `flate2::write::GzEncoder` (dev-dependency).

**Green:** L-OWN `fixture_server::`; L-RS at the group gate.

**Done when:** the three tests pass, each in under 2 s (record the times). Land early: T009's socket tests wait on it.

### T004 [P] — `core::version` (spec FR-030 to FR-038, SC-006, SC-007, SC-008, SC-027; plan §4.4)

**Files:** `crates/core/src/version.rs`, `crates/core/src/version_tests.rs`.

**Test first** (`version_tests.rs`; fixtures read in place as `env!("CARGO_MANIFEST_DIR")` +
`/../../../../tools/feed-contract/fixtures/feeds/<file>`, FR-071):
- `parse_core_cases` (SC-008): `0.0.0`, `1.10.2`, `999999999.0.1` parse; `1.2`, `1.2.3.4`, `01.2.3`, `1.02.3`,
  `1.2.3-beta.1`, `1.2.3+b1`, ` 1.2.3`, `1000000000.0.0`, `` do not; `Display` round-trips each accepted string;
  `0.12.2 < 0.12.10 < 1.0.0 < 1.9.9 < 1.10.0`.
- `reduce_class_version_cases` (SC-008): `1.0.0.1` → `1.0.0`; `1.0.4.0` → `1.0.4`; `0.8.0.3` → `0.8.0`; `1.2.3` →
  `1.2.3`; `1.2.3.0123456789`, `1.2.3.x`, `1.2.3.4.5`, `1.2`, `1.2.3-beta`, `1.2.3.`, `01.2.3.4` → `None`.
- `compare_cases` (SC-008, FR-032): `No` → `NotInstalled`; `VersionUnknown` → `Unknown`; installed with `available:
  None` → `Unknown`; equal → `UpToDate`; available greater → `UpdateAvailable`; installed `1.0.4` vs available `1.0.3`
  → `InstalledNewer`.
- `os_version_cases` (SC-027, plan §7.1 row SC-027 "as parsed"): `11.0`, `2.31`, `10.0.19045`, `10.0.19045.1` parse and
  `OsVersion::parse(s).unwrap().to_string() == s` for each, with `components()` lengths 2, 2, 3, 4 and the parsed values;
  `11`, `11.`, `1.2.3.4.5`, `a.b`, `-1.0`, `11.0 `, `4294967296.0` do not parse; `11.0 == 11.0.0`, `2.31 < 2.34`,
  `10.9 < 10.10`, `12.0 > 11.7.10`.
- `resolve_table` (SC-006): `platform_candidates` equals the FR-034 table for all nine `(Os, Arch)` pairs; every cell of
  SC-006 over `full-v2.json`, `mixed.json`, `unknown-platform-key.json`, `v1-only.json` (parsed with `feed::parse`),
  `running = None`.
- `needs_newer_os_table` (SC-027): every cell of SC-027 over `full-v2.json`, including the in-memory Charlie copy whose
  native `darwin-aarch64` asset needs `13.0` on a `12.0` machine (`NeedsNewerOs`, not replaced by `darwin-universal`),
  each `NeedsNewerOs` cell checked by `min_os.to_string()` (`"11.0"`, `"2.31"`, `"10.0"`) and `min_os.components()`, not
  only `==`; `running = None` → `Available` on every pair; `NotForThisMachine` / `NotThroughManager` unaffected by
  `running`.
- `native_candidates_windows` / `_macos` / `_linux` (SC-007), each `#[cfg(target_os = "…")]`: `Arch::native()` matches
  `cfg!(target_arch)`; `resolve_native(entry, None)` over `full-v2.json` equals the SC-006 row of `(that Os, native
  arch)`.
Red: the pure four panic `not yet implemented` in `version.rs`.

**Implement** plan §4.4 verbatim: hand-written byte parsers (no `regex`); `CoreVersion` derived `Ord` (field order);
`OsVersion::cmp` with missing trailing components as 0; `Display` as parsed; `resolve` (release `None` or empty
platforms → `NotThroughManager`; the first present candidate is final; `min_os > running` → `NeedsNewerOs { min_os }`,
`running` `None` never gates). `resolve_native` stays T002's one line; `Arch::native()` / `NativePlatform` occur
nowhere else in `version.rs` (T013 enforces it). Reads no environment, file or process (FR-038).

**Expected red until T014 (G3):** `resolve_table`, `needs_newer_os_table` and `native_candidates_windows` (locally; on
L-WSL `native_candidates_linux`), only with the panic `not yet implemented` raised in `crates/core/src/feed.rs`
(`feed::parse`).

**Green:** L-OWN `version::` — the four pure tests pass; L-RS at the group gate with the expected reds above.

**Done when:** the four pure tests pass and the fixture tests fail only with the expected panic.

### T005 [P] — `core::contract`, the C1 interpreter and the code rules (spec FR-001 to FR-008, SC-002 contract half, SC-003 contract half, SC-020, SC-025; plan §4.2, PD-9, PD-10, PD-11, PD-24, SI-8, TA-2)

**Files:** `crates/core/src/contract.rs`, `crates/core/src/contract_tests.rs`.

**Test first** (`contract_tests.rs`, fixtures in place under `tools/feed-contract/fixtures/`):
- `embedded_schemas_compile`: `compile_schema_text(V2_SCHEMA_TEXT)` and `(V1_SCHEMA_TEXT)` are `Ok`.
- `unsupported_schema_constructs_are_refused`: plan §7.1 row "(FR-001)" verbatim (seven `Err(Unsupported { pointer,
  keyword })` pairs and the `Ok` control).
- `keyword_semantics_cases` (FR-002, FR-003): plan §7.1 row "(FR-002, FR-003)" verbatim: the six synthetic schemas
  through `compile_schema_text`, each instance's **exact** sorted `(code, instance_path, keyword)` list (`if` / `then` /
  `else` with the extra `if` finding, `propertyNames` reported at the object's path, `enum` / `const` with numbers by
  value, `integer` for `2.0` and `2.5`).
- `c1_cases_reproduce` (SC-002, as amended by TA-2): every row of `feeds/cases.json` (the test counts rows from the file
  and asserts it visited all of them, 90 today, never a literal in the loop): `accept` → 0 findings under its schema,
  and a `v2` accept row also 0 under `V1`; `accept-without-platforms` → 0 findings; `{"error": X}` → `X` among the
  findings' codes or their `"<instance_path> <keyword>"` pairs (fixtures `README.md:51-65`; a root finding's pair
  starts with a space); `malformed-json` → `serde_json` fails (the client's `E_INPUT`). For the three fixtures whose
  plugin keys are not in bytewise order (`dup-vst3_bundle-casefold`, `plugin-key-pattern`, `plugin-key-reserved`, plan
  §1.1) the duplicate row compares codes, not paths (SI-8).
- `gen_feed_outputs_accepted` (SC-003): every `fixtures/gen-feed/*/expected.v2.json` has 0 findings under `V2`, every
  `expected.legacy.json` (16, plan §1.1) 0 under `V1`; the counts are printed and each is ≥ 1.
- `ecma_pattern_translation` (SC-025): through `ecma_pattern_to_rust` and the schema's own `$defs.url.pattern` and
  `$defs.reservedStem.pattern`: `https://a\u{FEFF}b` no match, `https://a\u{0085}b` match, `CON.txt` match, `CON.\r`
  and `CON.\u{2028}` no match; `\d`, `\w`, `\b` → `Unsupported`.
- `trusted_comment_cases` (SC-020): every `fixtures/trusted-comments.json` `valid` row parses to its four fields; every
  `invalid` row fails with its `code`.
- `minisig_text_cases` (SC-020): `fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz.minisig` parses with key id
  `440FA75168B852F7` and its trusted comment passes `parse_trusted_comment`; every `valid/full-v2.json` platform
  `signature` parses; Alpha `linux-x86_64`'s signature in `invalid/signature-legacy-Ed.json`,
  `signature-blob-length.json`, `signature-noncanonical-base64.json` → `E_SIGNATURE_TEXT`; in
  `signature-trusted-comment.json` it parses and its trusted comment → `E_TC_FIELDS`; in `signature-cr.json` and
  `signature-no-final-lf.json` it parses (they are rejected only inside a feed, by FR-004's CR rule and the schema
  `pattern`).
Red: the placeholders panic `not yet implemented`.

**Implement** plan §4.2 verbatim: the two-pass arena compiler `compile_schema_text` (PD-24; every unknown keyword, other
`type`, non-local or missing `$ref`, or refused pattern → `Unsupported { pointer, keyword }`), the evaluator with the
PD-10 reporting table (all keywords, `allErrors`), `ecma_pattern_to_rust` (PD-11), `pointer`, the code rules of §4.2.3
as a port of `contract.js:421-478` (first failing install field only; duplicates in bytewise order with
`DuplicateSides`, PD-9), `parse_minisig_text` and `parse_trusted_comment` with their regexes in `OnceLock<Regex>`.
`validate` panics with the compile error if an embedded schema fails to compile. The `include_str!` text becomes part
of the release binary, which T017's `roster absent` step scans: the two schemas name no shipped plugin and no
`api.github.com`; keep it so.

**Green:** L-OWN `contract::` all eight pass; L-RS at the group gate.

**Done when:** all eight pass; the notes print the row count and the two gen-feed counts.

### T006 [P] — `core::sysproxy`, the pure proxy parsers, the reader seams and the offset parsers (spec FR-049, FR-074, FR-095 part, SC-029 part, SC-039 part, SC-043 part; plan §4.6, PD-1, PD-17, PD-18, PD-31, SI-1, SI-9, SI-21, SI-36, R-2, R-4, R-13)

**Files:** `crates/core/src/sysproxy.rs`, `crates/core/src/sysproxy_tests.rs`.

**Test first** (`sysproxy_tests.rs`; warn assertions through `test_support::install_capture()` / `take_mine()`; plan
§7.1 rows SC-029, SC-039 and SC-043 are binding for every case):
- `windows_proxy_parser_cases` over `WinIeProxyConfig`: all off → default; `proxy:3128` → `http://proxy:3128`;
  `http=a:1;https=b:2` → `http://b:2`; `http=a:1` only → no static proxy; bypass `<local>;*.corp;10.*` → those three
  entries; `auto_config_url` → `pac_url`; `auto_detect: true` → `auto_detect`; a static proxy **and** a PAC source
  together → both kept (`proxy: Some`, `pac_url: Some`); a malformed value → default plus one `warn!`.
- `macos_proxy_parser_cases` over `(String, ScValue)` pairs with SystemConfiguration's key names (`HTTPSEnable`,
  `HTTPSProxy`, `HTTPSPort`, `ExceptionsList`, `ProxyAutoConfigEnable`, `ProxyAutoConfigURLString`,
  `ProxyAutoDiscoveryEnable`): every `…Enable` key once as `Int(1)` and once as `Bool(true)` → on, `Int(0)` /
  `Bool(false)` → off, `Text("1")` → absent; enable + host + port → `http://host:port`; no port → `:443`; exceptions →
  bypass; PAC and discovery flags; a static proxy beside a PAC URL → both kept; a wrongly typed value counts as absent.
- `linux_proxy_parser_cases`: GNOME `GnomeRaw` texts (`'none'`; `'manual'` with `'proxy.corp'` and `3128`; port `0` →
  no static proxy; `'auto'` with an `autoconfig-url` → `pac_url`, without → `auto_detect`; `ignore-hosts`
  `['localhost', '127.0.0.0/8', '::1']` and `@as []`; malformed GVariant → default plus one `warn!`) and KDE
  `kioslaverc` texts (`ProxyType=0`; `1` with `httpsProxy=http://host 8080` → `http://host:8080`, and a schemeless value
  prefixed `http://`; `2` → `pac_url` from `Proxy Config Script`; `3` → `auto_detect`; `4` → none; `NoProxyFor=a,b`;
  `ReversedException=true` → ignored plus one `warn!`).
- `win_ie_strings_are_read_and_freed_once` (every leg): three test-owned `Vec<u16>` buffers (ASCII, one non-BMP
  character, a lone `0xD800` → U+FFFD) and one null pointer through `take_win_ie` with a recording `free`: each non-null
  pointer freed exactly once, the null one never, the strings as expected; and `read_wide` over the same kinds of
  buffer (PD-35's entry host): the string, nothing freed, a null pointer → `None`.
- `linux_desktop_choice_cases` (SC-039, SI-36): `linux_uses_kde` for `Some("KDE")`, `Some("X-Cinnamon:KDE")`,
  `Some("KDE:plasma")` → `true`; `Some("GNOME")`, `Some("ubuntu:GNOME")`, `Some("XFCE")`, `Some("KDEPlasma")`,
  `Some("kde")`, `Some("")`, `None` → `false`.
- `kioslaverc_path_cases` (every leg, portable fixtures, plan §4.6): with `t = tempfile::tempdir()`,
  `kioslaverc_path(Some(&t.path().join("cfg")), Some(t.path())) == Some(t.path().join("cfg").join("kioslaverc"))`; a
  relative `Some(Path::new("rel"))` with home `t.path()` → `t.path().join(".config").join("kioslaverc")`; `None` with
  home → the same; `None` without home → `None`; no `/home/…` literal (not absolute on Windows, and Rule 4).
- `kde_file_read_cases`: in a `tempfile::tempdir()`, a missing file → default, no `warn!`; a written `ProxyType=1` file
  → the proxy.
- `parse_utc_offset_cases` (SC-043): `+0130` → 5400, `-0800` → -28800, `+0000` → 0, each also with one trailing LF;
  `""`, `garbage`, `+2500`, `0100`, `+01:00` → `None`.
- `offset_range_cases` (SC-043): `cf_offset_secs(3600.4) == Some(3600)`, `-50_400.0` → `Some(-50_400)`, `50_401.0` →
  `None`, `f64::NAN` and `f64::INFINITY` → `None` (through `f64::is_finite`).
- `bounded_run_abandons_and_reaps` (`#[cfg(unix)]`; L-WSL-OWN locally, PENDING-CI): `[printf "'manual'"]` →
  `Some(["'manual'"])`; `[sleep 1]` with bound 300 ms → `outputs: None` in < 0.9 s, one pid in `abandoned`; on Linux
  `/proc/<pid>` is gone within 3 s. Only `Command::new("sleep")` and `Command::new("printf")` (FR-087); the word
  `gsettings` appears nowhere in this file (Rule 4).
Red: the placeholders panic `not yet implemented`.

**Implement** plan §4.6 verbatim (the per-OS rules paragraph, `kioslaverc_path`, `linux_uses_kde` as `split(':').any(|e|
e == "KDE")`, `gvariant_string`, `gvariant_string_list` accepting `@as []`, `read_wide` and `take_win_ie` over raw
pointers with `String::from_utf16_lossy` (Rule 16: each pointer read in its own `unsafe { }` block), `run_bounded`
polling `try_wait` every 20 ms and handing a late child to a detached reaper thread after dropping its piped stdout,
never killing it (PD-17), `parse_utc_offset`, `cf_offset_secs`). No `win_utc_offset` (withdrawn, R-13 / R-15). All
compile on every OS. No `std::env` read (core has none). Every `warn!` (target `krate_manager_core::sysproxy`) names the
field and the rule, never the raw value (FR-056). `sysproxy.rs` names no `core_foundation` path (T013's plant targets
exactly this file).

**Green:** L-OWN `sysproxy::` (nine pass on Windows); L-WSL-OWN `sysproxy::` (ten, the reaper test included) when the
G1 gate has primed WSL; L-RS at the group gate.

**Done when:** the nine pass locally; the Unix test passed in L-WSL-OWN or is left to the G2 gate's L-WSL (say which);
it is a PENDING-CI row (T018).

### T007 [P] — `core::sysproxy_cf`, the macOS `CFDictionary` conversion and local offset (spec FR-049, FR-075, SC-040; plan §4.6 last paragraph, PD-31, R-6, SI-21, SI-37)

**Files:** `crates/core/src/sysproxy_cf.rs`, `crates/core/src/sysproxy_cf_tests.rs` (both compiled only on macOS).

**Test first.** `sysproxy_cf::tests::from_cf_proxies_cases` (plan §7.1 row SC-040 verbatim): dictionaries from
`CFDictionary::from_CFType_pairs` (core-foundation 0.10.1 `src/dictionary.rs:43`): `HTTPSEnable` = `CFNumber 1` →
`Int(1)`; `ProxyAutoConfigEnable` = `CFBoolean::true_value()` → `Bool(true)`; `ProxyAutoDiscoveryEnable` =
`CFBoolean::false_value()` → `Bool(false)` (`src/boolean.rs:28,32`); `HTTPSProxy` and `ProxyAutoConfigURLString` =
`CFString` → `Text`; `HTTPSPort` = `CFNumber 3128` → `Int(3128)`; `ExceptionsList` = `CFArray<CFString>` → `List`;
skipped, no panic: a `CFData::from_buffer(b"x")` value (`src/data.rs:30`), a `CFArray` holding a `CFNumber`; a missing
key is simply absent; `sysproxy::from_macos` of the converted pairs gives the static proxy, the bypass list, `pac_url`
and `auto_detect: false`. It never calls `system_utc_offset` (Rule 1, plan §4.11.1). Its red-first is the macOS leg's
first run (PENDING-CI): it cannot compile on this box (`cfg(target_os = "macos")`, and L-XCHECK stops in `ring`'s build
script, TA-7); record that in the notes.

**Implement** plan §4.6: `from_cf_proxies` copies every key as a `String`; `CFType::downcast` (`src/base.rs:84`) to
`CFBoolean` → `Bool` (`bool::from`, `src/boolean.rs:47-50`), to `CFNumber` → `Int` via `to_i64` (`src/number.rs:45`), to
`CFString` → `Text`, to a `CFArray` whose every element is a `CFString` → `List`; anything else skipped. No key name and
no string literal. `system_utc_offset() -> Option<i32>` = `sysproxy::cf_offset_secs(CFTimeZone::system()
.seconds_from_gmt(CFDate::now()))` (`src/timezone.rs:45,52`, `src/date.rs:35`), read-only, named in no test source.

**Green:** L-RS on Windows (the module is not compiled there). No local compile is possible: the first compile is
`clippy` on `check (macos-latest)`. Review the file against core-foundation 0.10.1's source in the local registry (the
cited lines) instead and say so in the notes.

**Done when:** the file holds no `todo!()`; the PENDING-CI row names `sysproxy_cf::tests::from_cf_proxies_cases` on
`check (macos-latest)`.

### T008 [P] — `core::pac`: the shared pure half (helpers, clock, answer parser, Windows answer renderer) and the macOS / Linux engine (spec FR-064, FR-067, FR-068, FR-092 part, FR-094, FR-095, SC-033, SC-035 part, SC-036 part, SC-041 part, SC-042, SC-043 part; plan §4.15, PD-25 to PD-27, PD-29 (memo and cap), PD-31, PD-33 (answer mapping), PD-36, SI-25, SI-28, SI-29, SI-35, SI-38, R-8, R-12 to R-16)

**Files:** `crates/core/src/pac.rs`, `crates/core/src/pac_tests.rs` (keep T002's `FakePacHost`, `OsAnswer`, `handle()`
and `fake_pac_host_answers_from_its_table` unchanged: T009 and T014 use them).

**Test first** (`pac_tests.rs`, one flat file; every host a `FakePacHost` through `handle()`; warn assertions through
`install_capture()` / `take_mine()`; plan §7.1 rows SC-033, SC-035, SC-036, SC-041, SC-042, SC-043 are binding).
**Every host:**
- `pac_helper_cases` (SC-033, plan §7.1 row SC-033): the `helpers::*` functions called directly with injected values,
  `dnsResolve` / `isResolvable` / `isInNet` by name / `myIpAddress` through `helpers::dns_resolve` / `my_ip_address`
  over a `FakePacHost` (an IPv6-only answer → `None` / `127.0.0.1`): the spec's table (`spec.md:1225-1230`) with expected
  values written from MDN's definitions (plan §1.2 "PAC helpers and answers" row), one reversed range per time helper
  matching only its endpoints (SI-35), `dateRange(x)` with one number above 31 read as a year, `"GMT"` vs local over
  `clock_civil` with `PacClock { unix_secs: 1_700_000_000, local_offset_secs: Some(3600) }`; the helper cost-bound rows
  (`sh_exp_match` over `"a".repeat(4096)` with `"*a".repeat(20) + "*b"` and the 1024-byte `"*a".repeat(511) + "*b"` →
  `Some(false)`, against `"*a*"` → `Some(true)`; the algorithm-sanity rows `"abc"` vs `"a?c"`, `"a*c"`, `"*"`, `""` vs
  `"*"` → true, `"abc"` vs `"a*d"`, `""` vs `"?"` → false; one byte over each cap (4097 / 1025 / 254) → `None`, exactly
  at it → `Some(_)`).
- `find_proxy_result_cases` (`parse_answer`): the spec rows (`PROXY a:1; DIRECT` → [http a:1, direct]; case-insensitive;
  `HTTPS p:3128`; `SOCKS s` → socks4 `s:1080`; `SOCKS4 s:2`; `SOCKS5 s:3`; ` PROXY a ; ; DIRECT` → http `a:80` then
  direct, no warn; `BOGUS x; DIRECT` → [direct] + one `E_PAC_RESULT` warning; `""` → `E_PAC_RESULT`) plus `[::1]:3128`
  accepted, port `0` / `65536` skipped with a warning, a 64 KiB + 1 answer → `E_PAC_RESULT`, 20 `DIRECT` entries → 16
  kept and one warning (SI-29). (The non-string results `undefined`, a number, an object, `null` are engine rows of
  `script_failure_cases`, since `parse_answer` takes a `&str`.)
- `wpad_candidate_cases` (SC-035, PD-30): `WPAD_DNS_URL == "http://wpad/wpad.dat"`, it passes
  `http::check_pac_url(UrlPolicy::HttpsOnly, …)` (a pure call, FR-087 as amended by SI-26), its host is the single label
  `wpad`, `wpad_dns_candidates() == [WPAD_DNS_URL]` (a function of no host name: no devolved candidate, no DHCP), and
  `InertPacHost.wpad_candidates(d)` is empty.
- `winhttp_answer_cases` (SC-041 as amended by SI-38, PD-36; `os_answer` then `parse_answer`): `[Direct]` → [direct];
  `[Http a:8080, Http b:3128]` → [http a:8080, http b:3128] in order; `[Http a:8080, Direct]` → [http a:8080, direct];
  `[Https p:3128]` → https; `[Socks s:1080]` → socks4 `s:1080`; `[Http a:0]` → http `a:80`; `[Http ::1:3128]` → http
  `[::1]:3128`; `Other(3)`, an empty host and the host `a;b` each skipped with one warning; `[]` → `E_PAC_RESULT`; 20
  `Direct` → 16 kept and one warning.
- `pac_clock_cases` (SC-043, plan §7.1 row SC-043): over `clock_civil` and the time helpers at 2026-10-31 23:30:00 UTC (a
  Saturday) with offset +2 h: without `"GMT"` `weekdayRange("SUN")` and `timeRange(1, 2)` true; with `"GMT"`
  `weekdayRange("SAT")` true, `weekdayRange("SUN")` and `timeRange(1, 2)` false; `local_offset_secs: None` → the UTC
  fields and, with one `warned` flag across two calls, exactly one `warn!`; `helpers::civil` at 0, 951_782_400
  (2000-02-29), 4_102_444_800 (2100-01-01) and a negative value.
**`#[cfg(not(windows))]` (engine; L-WSL-OWN locally, PENDING-CI):**
- `url_argument_cases`: a script branching on `url` sees the full URL for `http` and `scheme://host:port/` for `https`
  (path and query dropped, `pac_url_argument`); `host` is the hop's host.
- `script_world_cases` (R-14, plan §7.1 row SC-033): `typeof` of `Date`, `require`, `std`, `os`, `fetch`, `setTimeout`,
  `print`, `console` → eight `undefined`; `alert("x")` returns without effect; scripts calling `isResolvableEx`,
  `dnsResolveEx`, `isInNetEx`, `myIpAddressEx`, `sortIpAddressList`, `convert_addr` → each `E_PAC_SCRIPT`; the
  reversed-range rows through a `FakePacHost` clock; the engine cost rows (`shExpMatch('a'.repeat(4096), '*a'.repeat(511)
  + '*b')` → `false` within `PAC_EVAL_BUDGET`; `shExpMatch('a'.repeat(1e6), '*a*b')` and `dnsResolve('h'.repeat(254))`
  uncaught → `E_PAC_SCRIPT` with zero `resolve` calls, caught → `[Direct]`); the once-per-session clock (a `PacSession`
  over a fake with `local_offset_secs: None` whose script calls `weekdayRange("SAT")`, `timeRange(1, 2)` and
  `dateRange(31)`, `find_proxy` for two hops → `now_calls() == 1` and exactly one offset `warn!`; a script calling no
  time helper → `now_calls() == 0`).
- `script_failure_cases` (SC-036, plan §7.1 row SC-036): a syntax error, a throw, no `FindProxyForURL`, a non-string
  result (`undefined`, a number, an object, `null` → `E_PAC_RESULT`), `function FindProxyForURL(){ while (true) {} }`,
  `function f(){ f() } f()` (stack), `try { while(true){} } catch(e) {}` (the interrupt is uncatchable), `/^(a+)+$/` over
  40 `a` and a `b`, and `var a=[]; while(true) a.push('x'.repeat(1e5))` (heap) → each `E_PAC_SCRIPT` (or `E_PAC_RESULT`),
  each returning in < `PAC_EVAL_BUDGET` + 1 s, and a fresh `evaluate` of a good script on the same thread afterwards
  succeeds. **Log escaping:** a `FindProxyForURL` throwing `"a\nb\rc"` and one returning `"PROXY x\ny; DIRECT"` → every
  captured warn message has no raw `\n` / `\r` and contains the escaped `\\n`; a 300-char multi-byte exception message is
  cut to 200 `char`s (plus `…`) without a panic (`log_text`).
- `dns_bound_cases` (SC-042, plan §7.1 row SC-042): through `DnsMemo` over a `FakePacHost` with `hang` names (TA-22): a
  hanging name is unresolvable after `PAC_DNS_WAIT` (returns within `PAC_DNS_WAIT` + 1 s), `hangs_finished()` is still 0,
  the next distinct name still resolves, and after `release_hangs()` the worker ends on its own; the 17th distinct name →
  unresolvable with no `resolve` call and exactly one `warn!`; the same name twice → one call, a failed answer
  included; an IP literal → no call; every logged `wait` ≤ `PAC_DNS_WAIT` and ≤ the remaining budget; a script calling
  `dnsResolve` on 20 distinct hanging names returns within `PAC_EVAL_BUDGET` + 1 s.
Red: the pure functions and `PacSession::open` / `evaluate` panic `not yet implemented`.

**Implement** plan §4.15 verbatim. Shared half (every OS): `wpad_dns_candidates`, `parse_answer` (64 KiB cap, `;` split,
keyword mapping and default ports 80 / 443 / 1080, 16-entry cap, zero usable entries → `E_PAC_RESULT`), `os_answer`
(PD-36's rendering), `pac_url_argument`, `log_text` (`escape_debug`, ≤ 200 `char`s, `…`), `clock_civil`, `helpers::*`
pure and hand-written (no regex; `civil` = days-from-civil; `sh_exp_match` the iterative single-backtrack matcher of
§4.15, no recursion, no allocation; the input caps). The `pub(crate)` items `parse_answer`, `os_answer`, `log_text`,
`pac_url_argument` are used by `http.rs`'s OS path on every OS, so Windows warns no `dead_code` (plan §4.15 "Zero
warnings on Windows"). Engine half (`#[cfg(not(windows))]` per item, Rule 16): `PacSession::open` in the stated order
(size check, `Runtime::new`, `set_memory_limit(PAC_MEMORY_LIMIT)`, `set_max_stack_size(PAC_STACK_LIMIT)`,
`set_interrupt_handler` over an `Rc<Cell<Instant>>` deadline, `Context::custom::<(Eval, RegExpCompiler, RegExp, Json)>`,
the helpers on `globals()` through `Function::new` with closures owning an `Arc` clone of the host, a per-session
`Rc<RefCell<DnsMemo>>`, one `Rc<OnceCell<PacClock>>` and one `Rc<Cell<bool>>` `warned` flag, `ctx.eval`,
`FindProxyForURL` present); every `rquickjs::Error` → `E_PAC_SCRIPT` with `Ctx::catch`'s message through `log_text`;
`find_proxy` (deadline, call, non-string → `E_PAC_RESULT`, `parse_answer`, one warn per parse warning); `evaluate` as a
one-shot over `PacSession`; `DnsMemo` (≤ `PAC_MAX_LOOKUPS`, memoised, `min(PAC_DNS_WAIT, deadline - now)`). Never
`rust-alloc`, `parallel`, `loader`, `futures` (no feature can be added, Rule 8). `pac.rs` opens no socket and resolves
no name (`UdpSocket` / `to_socket_addrs` only in `http.rs`, T013), and no `pub` item's signature names `rquickjs::`.

**Expected red until T009 (same group):** `wpad_candidate_cases`, only with the panic `not yet implemented` raised in
`crates/core/src/http.rs` (`check_pac_url`).

**Green:** L-OWN `pac::` (the five every-host tests and the self-check); L-WSL-OWN `pac::` (all ten) when available;
L-RS and L-WSL at the group gate.

**Done when:** the every-host tests pass locally; the notes quote the L-WSL-OWN (or G2-gate L-WSL) elapsed times of the
loop, regex, heap and DNS-hang cases.

### T009 [P] — `core::http`: URL policy, redirects, body cap, error mapping, proxy resolution, transport, the PAC fetch (macOS / Linux) and the OS-evaluated path (spec FR-040 to FR-048, FR-059 part, FR-061 part, FR-064 to FR-069 transport half, FR-092 transport half, SC-012, SC-018 part, SC-026, SC-028, SC-030 part, SC-034, SC-035 part, SC-036 part, SC-041 part; plan §4.5, §4.5.1, PD-2 to PD-5, PD-12, PD-13, PD-18, PD-19, PD-23, PD-28 to PD-30 (fetch, bounded, std_*), PD-33 to PD-36, SI-2, SI-10, SI-11, SI-20, SI-26, SI-27, SI-34, SI-39, SI-40, TA-9, TA-21, TA-23)

**Files:** `crates/core/src/http.rs`, `crates/core/src/http_tests.rs`.

Order of work: the pure functions first (`check_url`, `check_proxy`, **`check_pac_url`**, which T008 waits on,
`next_hop`, `next_pac_hop`, `bypasses`, `resolve_proxy`, `ProxyEnv::from_vars`, `config_for_dev_url`, `agent_config`,
`map_error`, `is_no_response`, `bounded`), then the transport, then the OS path, then the PAC fetch.

**Test first** (`http_tests.rs`; socket tests use T003's `FixtureServer` and `HttpConfig::loopback(1 s, 1 s)` unless
stated; PAC hosts are `crate::pac::tests::FakePacHost::handle()` (TA-12); a static proxy in a PAC / OS-path test enters
by struct update (TA-21); warn assertions through `install_capture()` / `take_mine()`; plan §7.1 rows SC-012, SC-018,
SC-026, SC-028, SC-030, SC-034, SC-035, SC-036, SC-041 are binding).
**Every host (21):**
- Pure: `production_values` (`HttpConfig::production() == HttpConfig { policy: HttpsOnly, connect_timeout:
  Duration::from_secs(10), total_timeout: Duration::from_secs(30), proxy: None, pac: None, pac_host: None }` as
  literals; `CONNECT_TIMEOUT`, `TOTAL_TIMEOUT` as `Duration::from_secs(10)` / `(30)`); `url_policy_cases`,
  `next_hop_cases` (SC-012's tables); `proxy_scheme_cases` (`check_proxy` under `HttpsOnly`: `http://`, `https://`,
  `socks4://`, `socks5://` on `proxy.example.invalid:3128` accepted; `ftp://`, `socks4a://`, `socks5h://` →
  `E_FEED_URL`; PD-34); `agent_trusts_the_os_store` (plan §7.1 row SC-026: `RootCerts::PlatformVerifier` for both
  policies × every `AgentPurpose` this OS compiles; `agent_config(&HttpConfig::production(), None,
  AgentPurpose::Feed).https_only() == true`; in a `#[cfg(not(windows))]` block `…PacFetch).https_only() == false`;
  `LoopbackOnly` → `false`); `config_for_dev_url_cases` (plan §7.1 row SC-018: a `ProxyResolution` with a static proxy
  and one with a PAC source plus a `FakePacHost` handle: loopback URL → `proxy`, `pac`, `pac_host` all `None`;
  non-loopback → all three kept; `http://localhost:8080/` → production, which then refuses it); `resolve_proxy_cases`
  (plan §7.1 row SC-028 verbatim, FR-069 as written); `proxy_env_from_vars_cases` (plan §7.1 row SC-028's list, including
  the redacted `Debug` of `ProxyVars`, `ProxyEnv` and a `sysproxy::SystemProxy` whose `pac_url` is
  `http://u:p@127.0.0.1/x.pac`); `no_response_class_cases` (plan §7.1 row SC-012, PD-33); `bounded_call_abandons_after_wait`
  (`bounded(|| { sleep(2 s); 1 }, 100 ms)` → `None` in < 1 s; `bounded(|| 7, 1 s)` → `Some(7)`); `pac_policy_cases`' pure
  rows (`check_pac_url` under `HttpsOnly` accepts `http` / `https`, refuses `file:`, `ftp:`, `data:` and userinfo; under
  `LoopbackOnly` a non-loopback host is refused).
- Transport (socket): `redirects_are_rechecked`, `status_and_body_errors`, `https_only_refuses_before_connecting`,
  `chain_shares_one_deadline` (plan §7.1 row SC-012 verbatim: 5 hops of 301/302/303/307/308 succeed, 6 →
  `E_FEED_REDIRECT`; no `Location` → `E_FEED_REDIRECT`; a hop to `https://example.invalid/…` → `E_FEED_URL` with no
  further request; relative `/v2.json` followed; 404/500/304 → `E_FEED_HTTP_STATUS`; `MAX_FEED_BYTES` / `+1` plain and
  gzip (compressed length < 64 KiB asserted); `Truncated` → `E_FEED_BODY`; `HeadersThenStall { 100, 10 bytes, 30 s }` →
  `E_FEED_TIMEOUT` in < 2 s; `Gzip { b"not gzip" }` → `E_FEED_BODY`; `https://127.0.0.1:<port>/` → `E_FEED_TLS`;
  `HttpConfig::production()` with the server's `http://` URL → `E_FEED_URL` and `requests()` empty; the delayed
  three-hop chain → `E_FEED_TIMEOUT` in < 1.5 s, at most three requests, none for `/ok`).
- Proxy (socket): `loopback_proxy_only`, `proxy_path_connect` (as amended by TA-9), `proxy_path_http` (SI-2),
  `no_proxy_bypass` (with the per-hop `start_v6` case): plan §7.1 rows SC-028 and SC-030 verbatim.
- OS path (SC-041 as amended by SI-40; `HttpConfig::loopback(1 s, 2 s).with_pac(Some(PacSource { auto_detect: true,
  url: Some(P.url("/p.pac?token=s")) })).with_pac_host(Some(fake))` with an `os_mode()` fake): `os_pac_walk` (plan §7.1
  row SC-041: `[Http B1, Http B2, Direct]` as SC-034's `pac_list_walk`; the non-advancing rows (a) `E_FEED_TLS`, (b)
  `E_FEED_HTTP_STATUS { status: 503 }`, (c) `E_FEED_TIMEOUT` each with B2 and C recording zero requests; `[Direct]` with
  `proxy: Some(B)` reaches C and B records nothing; a redirect C → C2 makes exactly two `os_find_proxy` calls with the
  hops' URLs; the `https` hop's URL arrives as exactly `https://127.0.0.1:<C>/`; TA-23's `[Http example.invalid:80]` →
  `E_FEED_URL` with nothing recorded; P records zero requests in every row; no captured line contains `token=s`; last,
  all entries failing → `E_FEED_OFFLINE` and `feed::check` returns the seeded cache as `Cached`); `os_pac_failure_cases`
  (injected `Fail(E_PAC_FETCH "WinHTTP error 12180")`, `Fail(E_PAC_SCRIPT "WinHTTP error 12166")`, `Entries([])` and
  `Entries([Other(3) ftp:21])` → exactly one `warn!` naming the code (`E_PAC_RESULT` for the two unusable answers) that
  contains `/p.pac` and neither `token=s` nor `?`, through the static proxy with `proxy: Some(B)` else direct; a `Block`
  answer → `get` returns within total + 1 s, the warn is `E_PAC_FETCH` "no answer from the OS PAC evaluator", the fake's
  logged `wait` ≤ `PAC_PHASE_BUDGET` and ≤ the total timeout, then the test drops its `Sender` and `os_finished()`
  reaches 1 on its own; last, `feed::check` over each failing config → `Fresh` with `error: None`).
**`#[cfg(not(windows))]` (8; L-WSL-OWN locally, PENDING-CI; plan §7.1 rows SC-034 to SC-036; every config
`HttpConfig::loopback(1 s, 2 s).with_pac(Some(PacSource { auto_detect: false, url: Some(A.url("/p.pac")) }))
.with_pac_host(Some(fake))` unless the case says otherwise):** `pac_path_proxy` (B records `CONNECT 127.0.0.1:<C>`
then the tunnelled `GET /versions.json`, SI-34); `pac_path_direct`; `pac_list_walk` (with the non-advancing rows (a),
(b), (c); last, `feed::check` over the all-failing config returns the seeded cache); `pac_per_hop`;
`pac_fetched_once_never_via_proxy`; `wpad_discovery_walk`; `pac_fallback_to_static_proxy`; `pac_body_cap`. Plus
`pac_policy_cases`' `#[cfg(not(windows))]` block: a non-loopback PAC host under `LoopbackOnly` refused with the server
recording nothing; a script answer naming `example.invalid:80` under `LoopbackOnly` → `E_FEED_URL`, no socket; the
redirect rows (d) and (e); the pure `next_pac_hop` rows. Every PAC case returns within the total timeout + 1 s.
The `.with_proxy(` call appears only in the bodies of `proxy_path_connect`, `proxy_path_http`, `no_proxy_bypass`,
`loopback_proxy_only`; `.with_pac(` / `.with_pac_host(` only in the PAC-path and OS-path bodies of plan §4.11.1's list;
`HttpConfig::production()` / `UrlPolicy::HttpsOnly` only in `https_only_refuses_before_connecting`,
`config_for_dev_url_cases`, `url_policy_cases`, `next_hop_cases`, `agent_trusts_the_os_store`, `proxy_scheme_cases`,
`production_values`, `pac_policy_cases` (FR-087 as amended by SI-10 and SI-26; T013 enforces it). Red: the
placeholders panic `not yet implemented`.

**Implement** plan §4.5 and §4.5.1 verbatim: `agent_config(config, proxy, purpose)` (the workspace's only
`config_builder()`, `https_only(purpose == Feed && policy == HttpsOnly)`, `RootCerts::PlatformVerifier`); `get` with
one chain deadline (PD-3), `to_ureq_proxy` before any socket (PD-23), per-hop bypass (PD-4); the private `PacState`
opened once per call when `config.pac` is `Some` (host `config.pac_host`, else `InertPacHost`): `os_evaluates()` →
`PacState::Os` with no I/O; else on macOS / Linux the PAC phase (WPAD candidates from the host, then `pac.url`, each
through `check_pac_url` and `fetch_direct` with `AgentPurpose::PacFetch` and `next_pac_hop` on every redirect hop,
`take(MAX_PAC_BYTES + 1)`, the first that fetches and compiles wins, inside `min(PAC_PHASE_BUDGET, chain deadline)`);
else on Windows one `warn!` "no PAC evaluator" and `Failed`. Per hop: the engine's `find_proxy`, or on the OS path
`http::bounded(move || h.get().os_find_proxy(&s, &pac::pac_url_argument(&hop_url), wait), wait)` with `wait = min(
PAC_PHASE_BUDGET, remaining)` → `os_answer` → one `warn!` per rendering warning → `parse_answer`; every proxy of the
answer through `check_proxy` first; the walk advancing only on `is_no_response` (PD-33); one `warn!` per failure in the
§4.5.1 format through `pac_url_for_log` (never the raw URL); the fallback (`[Proxy(config.proxy)]` else `[Direct]`; a
failed session or OS evaluation is not asked again in the call); `next_hop` on every hop; `take(max_bytes + 1)` over the
decoding reader; `map_error` with the `rustls::Error` downcast and the `ureq::Error::from(io::Error)` recovery (FR-044);
`resolve_proxy` (PD-5, FR-069); `ProxyEnv::from_vars`; `config_for_dev_url` (PD-12); `bounded` (a spawned thread and
`recv_timeout`; an unanswered thread is abandoned, never killed); `#[cfg(not(windows))]` `std_resolve` (an IP literal
without DNS, else `to_socket_addrs` through `bounded`) and `std_my_ip` (UDP `connect` to `198.51.100.1:80`,
`local_addr`, else `127.0.0.1`). `ureq` and `rustls` are named only in `http.rs` and `http_tests.rs`; no `pub` item
exposes a `ureq` / `rustls` type (FR-081). The user agent is `concat!("krate-manager/", env!("CARGO_PKG_VERSION"))`.

**Expected red until T003 (same group):** every socket test, only with the panic `not yet implemented` raised in
`crates/core/src/fixture_server.rs`. **Until T008 (same group):** `os_pac_walk`, `os_pac_failure_cases` and the eight
`cfg(not(windows))` tests, only with that panic raised in `crates/core/src/pac.rs`. **Until T014 (G3):** `os_pac_walk`,
`os_pac_failure_cases` (and on L-WSL `pac_list_walk`), only at their final `feed::check` rows, only with that panic
raised in `crates/core/src/feed.rs` (every assertion before those rows must already pass: write the `check` rows last).

**Green:** L-OWN `http::` (21 on Windows, the two OS-path tests up to their `feed::check` rows); L-WSL-OWN `http::`
(29) when available; L-RS and L-WSL at the group gate with the expected reds above.

**Done when:** the above; the notes quote the elapsed times of `chain_shares_one_deadline`, the `HeadersThenStall` case,
the OS-path blocking row and the slowest PAC-fetch case.

### T010 [P] — `src-tauri/src/os_proxy.rs`: the three real proxy readers and the real PAC host (spec FR-049, FR-066, FR-073, FR-074, FR-092 host; plan §4.10 `os_proxy.rs` and "The real PAC host", PD-1, PD-17, PD-30 to PD-32, PD-35, R-2, R-3, R-4, R-9, R-13, R-15 to R-17)

**Files:** `src-tauri/src/os_proxy.rs`.

**Test first.** `src-tauri` has no test binary (`src-tauri/Cargo.toml:13,19`, Phase 4 D12a), and FR-049 / FR-087 forbid
any test of a real reader or of `OsPacHost` (the WinHTTP service, the OS resolver and zone are real). Every step but
the OS calls is a tested core seam (T006 `take_win_ie` / `read_wide` / the parsers, T007, T008 `os_answer`, T009 the OS
path over a fake). The red-first evidence for this file is the compile: write the `cfg` arms calling the core seams by
their final names and record that L-RS and L-DEVFEED compile the Windows arm with 0 warnings.

**Implement** plan §4.10 and PD-35 verbatim, one OS call per OS step, each failure → `SystemProxy::default()` / `None` /
an `E_PAC_FETCH` `PacError` / empty plus one `warn!` naming the step, never a value (and never script or OS text except
through `pac::log_text`, which is `pub(crate)`: name the step and the numeric code only):
- `SystemProxyFromOs for NativePlatform`. Windows (`#[cfg(windows)]`): a zeroed
  `WINHTTP_CURRENT_USER_IE_PROXY_CONFIG`, one `unsafe` `WinHttpGetIEProxyConfigForCurrentUser` (0 → default + warn),
  then `unsafe { sysproxy::take_win_ie(WinIeRaw { … }, &mut |p| { GlobalFree(p.cast()); }) }` →
  `sysproxy::from_windows`. macOS: `SCDynamicStoreBuilder::new("krate-manager").build()` → `get_proxies()` →
  `sysproxy_cf::from_cf_proxies(&dict)` → `sysproxy::from_macos`. Linux: `linux_uses_kde(inputs.desktop.as_deref())` →
  `kioslaverc_path(…)` → `read_kde`; else `run_bounded` over the five `Command::new("gsettings").args(["get", schema,
  key])` of plan §1.2's GNOME row (`org.gnome.system.proxy` `mode`, `autoconfig-url`, `ignore-hosts`;
  `org.gnome.system.proxy.https` `host`, `port`), bound `Duration::from_secs(2)` → `GnomeRaw` → `from_gnome`.
- `OsPacHost`, `#[cfg(not(windows))]` impl (PD-32): `resolve` → `http::std_resolve`; `my_ip` → `http::std_my_ip`; `now`
  → `PacClock { unix_secs: SystemTime::now()` since the epoch as `i64`, `local_offset_secs: local_offset() }`;
  `wpad_candidates(_wait)` → `pac::wpad_dns_candidates()`. `local_offset()`: macOS
  `krate_manager_core::sysproxy_cf::system_utc_offset()` (so `os_proxy.rs` names no `core_foundation` path, FR-075);
  Linux `sysproxy::run_bounded(vec![Command::new("date").arg("+%z")], 2 s)` → `sysproxy::parse_utc_offset`.
- `OsPacHost`, `#[cfg(windows)]` impl (R-15, R-16, PD-35): `os_evaluates` → `true`; `os_find_proxy(source, url, wait)`
  → `win_find_proxy`, the one `unsafe` sequence of PD-35 and plan §4.10 verbatim: `WinHttpOpen(…,
  WINHTTP_ACCESS_TYPE_NO_PROXY, null, null, WINHTTP_FLAG_ASYNC)`; `WinHttpSetStatusCallback(session, Some(on_status),
  WINHTTP_CALLBACK_FLAG_REQUEST_ERROR | WINHTTP_CALLBACK_FLAG_GETPROXYFORURL_COMPLETE | WINHTTP_CALLBACK_FLAG_HANDLES,
  0)` with `const WINHTTP_CALLBACK_FLAG_HANDLES: u32 = WINHTTP_CALLBACK_STATUS_HANDLE_CREATED |
  WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING;` defined here (Rule 14); `WinHttpCreateProxyResolver`; the capacity-1
  `sync_channel::<(u32, u32)>` sender boxed as `ctx`, bound with `WinHttpSetOption(resolver,
  WINHTTP_OPTION_CONTEXT_VALUE, …)` (FALSE → deliberate leak, the call proceeds); `WINHTTP_AUTOPROXY_OPTIONS` per PD-30
  with `fAutoLogonIfChallenged: 0` (R-17); `WinHttpGetProxyForUrlEx(resolver, wide(url), &options, ctx as usize)` (any
  return but `ERROR_IO_PENDING` → close the resolver, then the session, that code's error); `rx.recv_timeout(wait)`;
  `GETPROXYFORURL_COMPLETE` → `WinHttpGetProxyResult`, each entry into `pac::OsProxyEntry` (`fProxy == 0` → `Direct`;
  `ProxyScheme` HTTP / HTTPS / SOCKS → `Http` / `Https` / `Socks`, other → `Other(n)`; host via `sysproxy::read_wide`;
  `ProxyPort`), `WinHttpFreeProxyResult`; `REQUEST_ERROR` → PD-35's error classes (`ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT`
  → `E_PAC_SCRIPT`, every other code → `E_PAC_FETCH` with the number); timeout → "no answer". On every path after the
  Ex call the waiting thread does exactly `WinHttpCloseHandle(resolver)` then `WinHttpCloseHandle(session)` and **never
  frees `ctx`**. `on_status`: context 0 → return; `HANDLE_CLOSING` → `drop(Box::from_raw(ctx))` and return; otherwise
  `try_send((status, dwError for REQUEST_ERROR else 0))`, ignoring `Full` / `Disconnected`; it allocates, logs and panics
  nothing. Inputs to UTF-16 with `str::encode_utf16` plus a NUL. No `WinHttpGetProxyForUrl`, no
  `WinHttpDetectAutoProxyConfigUrl`, no `GetTimeZoneInformation`, no `WinHttpSetTimeouts` (T013 bans the first three
  tree-wide).
- Every `unsafe` block carries a `// SAFETY:` comment citing the ownership rule it relies on (the WinHTTP pages and the
  WinHttpCloseHandle remarks of plan §1.2; the caller frees each non-NULL IE string with `GlobalFree`; Rule 16). It
  reads, never writes, never kills a process (PD-17), and names no environment variable (FR-063).

**Green:** L-RS (the Windows arm compiles, 0 warnings), L-DEVFEED. The macOS and Linux arms are first compiled by the
`clippy` step of `check (macos-latest)` and `check (ubuntu-24.04)` (PENDING-CI); neither L-XCHECK nor L-WSL covers
`src-tauri` (no WebKitGTK in WSL, plan §4.1).

**Done when:** the file holds no `todo!()`; the notes quote the clippy result line and list, for the reviewer, each
`unsafe` block with the PD-35 step it implements (plan §6 "Callback memory safety": reviewed, not proven by a test).

### T011 [P] — `crates/core/tests/roster.rs` and `crates/core/tests/live_feed.rs` (spec FR-072, FR-082, SC-015, SC-019 part; plan §4.11.3, §4.11.4, PD-14, PD-20)

**Files (new):** `crates/core/tests/roster.rs`, `crates/core/tests/live_feed.rs`.

**Test first.**
- `roster.rs` (plan §4.11.3): `plugin_names()` reads the `name` of every `plugins/*/version.json` under the repo root
  `env!("CARGO_MANIFEST_DIR")/../../../..` and asserts at least nine (read from the files, never written in this file);
  `scan(rel, text, needles) -> Vec<String>` searches raw text (comments included), case-sensitively, returning
  `"<rel>:<line>: <needle>"`. `roster_and_hosts_absent_from_sources` scans `crates/core/src/**`, `crates/helper/src/**`,
  `src-tauri/src/**`, `src-tauri/capabilities/**`, `src-tauri/tauri*.conf.json`, `src/**`, `index.html`, excluding
  `*_tests.rs`, `test_support.rs`, `*.test.ts`, for the names and `api.github.com`: 0 hits.
  `roster_scanner_reports_planted_names` runs `scan` on in-memory text holding one planted name (`plugin_names()[0]`)
  and `api.github.com`, requiring both reported with their line numbers. Red: `scan` as a stub returning an empty vector
  fails the plant test. The needle `api.github.com` lives in this file, outside every scanned path.
- `live_feed.rs` (plan §4.11.4): `#![cfg(feature = "live-network")]`, one `#[test] #[ignore = "live network; opt-in
  only, never in a gate"] fn live_feed_parses()` that reads the `const FEED_URL: &str = "…";` line of
  `src-tauri/src/lib.rs` (`:16`, a file read, `CARGO_MANIFEST_DIR`-relative), assembles a context from
  `fresh_temp_roots()` with that URL and `HttpConfig::production()`, runs `feed::check(&ctx, FetchMode::Normal, now)`
  and asserts a `Fresh` snapshot with at least one plugin. **Never run** (Rule 1); its evidence is L-LIVE.

**Implement** the scanner (walk with `std::fs::read_dir`, no new dependency).

**Green:** `cargo test -p krate-manager-core --test roster --locked` both pass on the real tree (other G2 files are in
flight: a hit in another task's file is quoted for the gate's fixer, not edited); L-LIVE (0 warnings); `cargo test -p
krate-manager-core --locked -- --list` does not list `live_feed_parses`, and `cargo test -p krate-manager-core
--features live-network --test live_feed --locked -- --list --ignored` lists it once (SC-019; record both counts in the
notes; L-CLOSE writes the evidence files).

**Done when:** all of the above; nothing ran `--ignored` without `--list`.

### T012 [P] — `config_guard.rs`: the HTTP, proxy and PAC injection assertions (spec FR-061, FR-063, SC-018 part; plan §4.11.2, PD-12, PD-13, SI-3, SI-6, SI-7, SI-26, TA-1, TA-10, TA-20)

**Files:** `crates/core/tests/config_guard.rs`.

**Test first.** Add the plan §4.11.2 checks to the checkers `real_src_tauri_config_passes_the_guard` (`:946`) and
`planted_config_changes_fail_the_guard` (`:956`) already run (through `check_config_against` / `check_src_tauri_manifest`,
`:343`, `:586-646`, or a new `check_http_injection(&ConfigFiles)` called from them; `load_config`, `:169`, reads
`proxy_env.rs`, `os_proxy.rs`, `dev_feed.rs` and every `src-tauri/src` file it does not read yet):
- `LIB_RS_STRINGS` (`:35-43`) gains `"HttpConfig::production()"`, `"resolve_proxy("`, `".with_proxy("`, `".with_pac("`,
  `".with_pac_host("`, `"os_proxy::pac_host()"`, `"proxy_env::read()"` (array length 14);
- the `assemble_context(` call in `lib.rs` has three top-level arguments (two top-level commas between its parentheses);
- `config_for_dev_url`, `LoopbackOnly`, `HttpConfig::loopback` occur in `src-tauri/src` only in `dev_feed.rs`;
- `.with_proxy(`, `.with_pac(`, `.with_pac_host(` and `resolve_proxy(` occur in no `src-tauri` file but `lib.rs`;
  `OsPacHost` only in `os_proxy.rs`;
- `HTTPS_PROXY`, `ALL_PROXY`, `NO_PROXY` occur only in `proxy_env.rs` and in no `crates/core/src` file (case-sensitive);
- `proxy_env.rs` holds each of the six `<field>: got("<NAME>")` pairs (whitespace-normalised);
- `src-tauri/Cargo.toml`'s target sections are exactly `[target.'cfg(windows)'.dependencies]` holding `windows-sys = {
  workspace = true }` and `[target.'cfg(target_os = "macos")'.dependencies]` holding `system-configuration = {
  workspace = true }` (TA-15: one line each).
`planted_config_changes_fail_the_guard` gains, each required to be reported: a `lib.rs` without
`HttpConfig::production()`; `LoopbackOnly` in `commands.rs`; a `lib.rs` without `.with_proxy(`; a `lib.rs` without
`.with_pac(`; `.with_pac_host(` in `commands.rs`; `OsPacHost` in `lib.rs`; `std::env::var("HTTPS_PROXY")` in
`os_proxy.rs`; a third target section in `src-tauri/Cargo.toml`; `proxy_env.rs` with `https_upper: got("https_proxy")`
(a swapped pair). The core `[features]` rule is T002's amended check (TA-1); add one plant `default = ["live-network"]`
in `crates/core/Cargo.toml` to `planted_app_metadata_changes_fail_the_guard` (`:1184`). Red: the new check as a stub
returning no findings fails the plant tests; quote one line.

**Implement** the check over the files' text (reuse `section_lines`, `key_of`, `insert_after`, `remove_lines`,
`replace_key`, `:273-292`, `:869-918`).

**Green:** `CARGO_TARGET_DIR=F:/tmp/km5-target/T012 CARGO_BUILD_JOBS=3 cargo test -p krate-manager-core --test
config_guard --locked`: all five tests pass on the real tree; L-RS at the group gate.

**Done when:** all pass. If a real-tree assertion is red because of another task's file (T010 writing `os_proxy.rs`),
do not edit that file: quote the finding in the notes for the gate's fixer.

### T013 [P] — `injection.rs`: confinement, seams, test-body, environment, FR-086 and dependency guards (spec FR-026, FR-036, FR-045, FR-063, FR-073 to FR-075, FR-080, FR-081, FR-086, FR-087, FR-088 part, SC-017, SC-021 part, SC-026 part, SC-038, SC-039 part, SC-040 part; plan §4.11.1, PD-20, PD-21, PD-22, PD-25, SI-10, SI-26, SI-31, SI-33, TA-12, TA-20)

**Files:** `crates/core/tests/injection.rs`.

**Test first.** Plan §4.11.1 bullets verbatim, reusing the file's lexer (`strip_comments_and_literals`, `tokens`,
`:112-165`, `:314-337`; "in code" = after blanking comments and literals), each new rule with its in-memory plants
reported, through one shared token-confinement helper:
- `core_dependencies_are_the_fr007_set` (`:818`, keeps its name): `[dependencies]` == the ten FR-080 names (T002 set the
  constant); `[dev-dependencies]` keys == `{"flate2"}`; the core target sections are exactly two:
  `[target.'cfg(not(windows))'.dependencies]` holding exactly `rquickjs = { workspace = true }` (no `features` /
  `default-features` key) with the root pin exactly `rquickjs = "0.14.0"`, and `[target.'cfg(target_os =
  "macos")'.dependencies]` holding exactly `core-foundation = { workspace = true }`; and the `core_foundation`
  confinement (the token only in `crates/core/src/sysproxy_cf.rs` and `sysproxy_cf_tests.rs`; plant `use
  core_foundation::string::CFString;` in `crates/core/src/sysproxy.rs`).
- `src_tauri_names_feed_host_once_and_reads_env_only_in_os_roots` (`:826`, keeps its name; replaces T002's widened
  list): `fn src_tauri_env_findings(files: &[(String, String)]) -> Vec<String>` with plan §4.11.1's rules 1-4, zero
  findings on the real files, and the three plants (`std::env::var("XDG_CURRENT_DESKTOP")` in an in-memory
  `os_proxy.rs`, `let _ = std::env::vars();` in `lib.rs`, an extra `"LANG"` literal in `proxy_env.rs`) reported.
- New `ureq_is_confined_to_http_rs` (FR-081, SC-026): the `ureq` / `rustls` / `TcpStream` / `TcpListener` token rules;
  the one-agent rules for `http.rs` (`config_builder` once inside `fn agent_config`; `new_with_config` once, followed by
  `( agent_config (`; none of `new_with_defaults`, `Agent :: new`, `ureq :: get`, `ureq :: post`, `ureq :: agent`,
  `ureq :: run`); `TcpStream :: connect` once in `fixture_server.rs` inside `impl Drop`; the PAC extension (`rquickjs`
  only in `pac.rs` / `pac_tests.rs`, no `pub` signature naming `rquickjs::`; `UdpSocket` and `to_socket_addrs` only in
  `http.rs`); **the R-15 / R-16 extension** (`GetTimeZoneInformation`, `WinHttpDetectAutoProxyConfigUrl` and
  `WinHttpGetProxyForUrl` in **no** workspace `.rs` file, the lexer's tokens keeping the last apart from
  `WinHttpGetProxyForUrlEx`; `WinHttpGetProxyForUrlEx`, `WinHttpCreateProxyResolver`, `WinHttpGetProxyResult`,
  `WinHttpFreeProxyResult`, `WinHttpSetStatusCallback`, `WinHttpSetOption` only in `src-tauri/src/os_proxy.rs`); the
  OS-crate extension (`windows_sys` and `system_configuration` only in `src-tauri/src/os_proxy.rs`; TOML keys
  `windows-sys` / `system-configuration` only in `src-tauri/Cargo.toml` and the root, `core-foundation` only in
  `crates/core/Cargo.toml` and the root, `ureq` / `rustls` / `rquickjs` only in `crates/core/Cargo.toml` and the root);
  the plants of plan §4.11.1 (`use ureq as u;`, `std::net::TcpStream::connect` in another file, a second
  `config_builder()`, `Agent::new_with_config(other)`, `use rquickjs::Runtime;` in `http.rs`,
  `std::net::UdpSocket::bind` in `pac.rs`, `("x", 0).to_socket_addrs()` in `pac.rs`, `GetTimeZoneInformation(&mut t)`
  and `WinHttpGetProxyForUrl(s, u, o, i)` in an in-memory `os_proxy.rs`, `WinHttpGetProxyForUrlEx(r, u, o, 0)` in
  `lib.rs`, `use windows_sys::Win32::Foundation::GlobalFree;` in `lib.rs`, `use
  system_configuration::dynamic_store::SCDynamicStoreBuilder;` in `lib.rs`).
- New `feed_model_seams_hold` (FR-026, FR-036, PD-21, PD-22): each of `PlainName`, `AssetUrl`, `PageUrl`, `AssetName`
  declared `pub struct <T> ( String ) ;`, no `#[derive]` naming `Deserialize` or `Default`, no `impl From <…> for <T>` /
  `impl FromStr for <T>`, every `fn` in its `impl` block returning `Self` or `Option < Self >` `pub ( crate )` or
  private; no `impl From < PageUrl >` / `impl From < AssetUrl >` and no `fn` taking one of the two URL types returns the
  other; `AssetUrl` in a field type only inside `struct PlatformAsset`; in `version.rs`, `Arch :: native (` and
  `NativePlatform` once each, inside `fn resolve_native`; plants: a `pub fn new` on `AssetName`, a second
  `Arch::native()` in `fn resolve`, `pub download_url: Option<AssetUrl>` in `PluginEntry`.
- New `production_config_only_in_allowed_tests` (FR-087 + SI-10 + SI-26 + TA-12), plan §4.11.1's bullet verbatim:
  `HttpConfig::production()` / `UrlPolicy::HttpsOnly` only in the eight `http_tests.rs` bodies of T009's list and
  `wpad_candidate_cases` (`pac_tests.rs`), plus all of `tests/live_feed.rs`; literals allowed in `config_guard.rs` and
  `injection.rs`; `.with_proxy(` only in `proxy_path_connect`, `proxy_path_http`, `no_proxy_bypass`,
  `loopback_proxy_only`, `check_through_proxy_writes_cache`; `.with_pac(` / `.with_pac_host(` only in the twelve
  PAC-path and OS-path bodies of plan §4.11.1 (`pac_path_proxy`, `pac_path_direct`, `pac_list_walk`, `pac_per_hop`,
  `pac_fetched_once_never_via_proxy`, `pac_policy_cases`, `wpad_discovery_walk`, `pac_fallback_to_static_proxy`,
  `pac_body_cap`, `check_survives_pac_failure`, `os_pac_walk`, `os_pac_failure_cases`); `.system_proxy(` in no test
  source; `OsPacHost`, `std_resolve (`, `std_my_ip (`, `pac_host (` and `system_utc_offset (` in no test source;
  `PacHostHandle :: new (` in test sources only inside `impl FakePacHost` in `pac_tests.rs` (TA-12); `Command::new(` in
  `crates/core` test sources only with `"sleep"` or `"printf"`; `gsettings` in no test source; `log::set_logger` only in
  `test_support.rs`; one plant per rule (a `HttpConfig::production()` in another body, a `.with_pac(` in another body,
  `http::std_resolve("example.invalid", …)`, `os_proxy::pac_host()`, `sysproxy_cf::system_utc_offset()`, a
  `PacHostHandle::new(` in `http_tests.rs`, a `Command::new("gsettings")`, a second `log::set_logger`). **The FR-086
  extension** (plan §4.11.1, over every test source plus `fixture_server.rs`): (1) a `static` item (token `static` to
  its `;`) whose tokens include `Mutex` or `RwLock` is reported (a `'static` lifetime is not a `static` item: the
  lexer's lifetime rule, `injection.rs:253`, already keeps `&'static str` in `platform/mod_tests.rs:25,35` apart;
  `test_support.rs` is not scanned by this rule); (2) `TcpListener :: bind` only in `fixture_server.rs`; (3) every
  `bind (` whose first argument is a string literal ends in `:0`, and a `( <literal> , <integer> )` tuple has `0`;
  plants: `static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());` in an in-memory `feed_tests.rs`, `static
  GATE: RwLock<()>` in an in-memory `http_tests.rs`, `TcpListener::bind("127.0.0.1:0")` in an in-memory `http_tests.rs`,
  `std::net::UdpSocket::bind("127.0.0.1:8080")` in an in-memory `fixture_server_tests.rs`.
- New `reader_dependencies_are_the_ruled_set` (SC-038): plan §4.11.1's bullet verbatim (`ureq` `3.5.0` with features
  exactly `["platform-verifier", "socks-proxy"]`, no `default-features = false`; `windows-sys` `0.61.2` with features
  exactly `["Win32_Foundation", "Win32_Networking_WinHttp"]` under a Windows target table of exactly one crate;
  `system-configuration` `0.8.0` under a macOS target table of exactly one crate; `rquickjs` `0.14.0` with no
  `features` key and no `default-features = false`, only in core's `[target.'cfg(not(windows))'.dependencies]`, absent
  from `[dependencies]`, from every Windows target table and from `FR007_DEPENDENCIES`); plants: `Win32_System_Time`
  added, a second crate under the Windows table, `rquickjs = { …, features = ["rust-alloc"] }`, `rquickjs` in core
  `[dependencies]`, `rquickjs` under `[target.'cfg(windows)'.dependencies]`.
- `core_sources_hold_no_production_literal` keeps passing unchanged.
Red: the new scanning functions as stubs returning empty vectors fail the plant assertions; quote one line.

**Implement** the scanners (one shared token-confinement helper used by `ureq_is_confined_to_http_rs` and
`core_dependencies_are_the_fr007_set`).

**Green:** `CARGO_TARGET_DIR=F:/tmp/km5-target/T013 CARGO_BUILD_JOBS=3 cargo test -p krate-manager-core --test
injection --locked`: every plant reported; the real-tree assertions pass as the tree stands, or the only reds are
findings in files another G2 task is still writing (quoted in the notes for the gate's fixer, never edited); L-RS at the
group gate.

**Done when:** the above.

**G2 gate:** L-RS (nothing else running but L-WSL) with only these expected reds: T004's three (feed panic) and T009's
`os_pac_walk` / `os_pac_failure_cases` at their `feed::check` rows (feed panic); L-DEVFEED; L-LIVE; L-GUARD; L-LINT;
L-WSL with the same expected reds plus `version::tests::native_candidates_linux` and `http::tests::pac_list_walk` (feed
panic).

---

## G3 — Feed and close scripts [P] (L-OWN share 16)

### T014 [P] — `core::feed`: parse, the model, `check`, the cache and offline (spec FR-020 to FR-029, FR-050 to FR-059, SC-002 client half, SC-003 client half, SC-004, SC-005, SC-009 to SC-011, SC-013, SC-014, SC-022, SC-024, SC-030 part, SC-031, SC-032, SC-036 part, SC-037; plan §4.3, PD-6, PD-8, PD-9, PD-15, PD-21, PD-22, SI-8, SI-16, SI-18, SI-19, TA-2, TA-9, TA-16)

**Files:** `crates/core/src/feed.rs`, `crates/core/src/feed_tests.rs`.

**Test first** (`feed_tests.rs`; fixtures in place; every `check` test its own `FixtureServer`, its own
`fresh_temp_roots()` and `HttpConfig::loopback(1 s, 1 s)`; log assertions through `install_capture()` / `take_mine()`;
plan §7.1 rows SC-002 to SC-037 are binding for each):
- **Parse and model:** `accept_rows_parse_through_the_client` (SC-002 client half + TA-2: every `accept` and
  `accept-without-platforms` row's file parses, keys and `schema_version` equal the document's, and each
  `accept-without-platforms` row's named plugin resolves `NotThroughManager` on the four FR-034 pairs);
  `gen_feed_outputs_parse` (SC-003); `schema_version_dispatch` (SC-004, PD-15: absent / `1` / `2` / `3` → `found: 3` /
  `0`, `-1`, `2.5`, `"2"`, `null` / `2.0`; `schema_version-const.json` accepted with 3 entries all `NotThroughManager`;
  malformed JSON, BOM, a `0xFF` byte, an HTML page → `E_INPUT`); `model_of_full_v2` (SC-005: the full expected `Feed`
  built in the test from the README facts, `assert_eq!`); `unknown_keys_do_not_change_the_model`;
  `prerelease_sibling_is_ignored`; `asset_name_cases` (PD-21, plan §7.1 row SC-005 with TA-16's kept name
  `Alpha-1.0.0-Windows-x64.exe`); `v1_entries_are_sanitized` (PD-22, FR-029(c), plan §7.1 row SC-005: `file:///etc/passwd`
  and `javascript:x` `download_url`s drop their entries with one `E_MODEL_V1_ENTRY …/download_url` warn each, an `https`
  one is kept as `Some(PageUrl)`, no `download_url` is kept as `None`, the key `"../X"` is dropped with one warn at
  `/plugins/..~1X`); `model_range_cases` (SC-037: the seven rows of plan §7.1 row SC-037 verbatim, each modified document
  first asserted valid with `contract::validate(&doc, Schema::V2)` (row 6 under `V1`) returning no findings; row 7's
  `check` over a fixture server for rows 1, 3 and 4); `roster_follows_the_feed` (SC-014: 10 / 9 / 8 / 3 against the
  documents' own keys, no list of names in the test); `sibling_prefix_filter` and `broken_sibling_never_blocks_stable`
  (SC-010, PD-9: both key orders of the duplicate boundary case).
- **Check and cache:** `check_fresh_writes_cache` (SC-009: the hand-written expected cache text, the four keys in D8
  order, two-space pretty print, final LF; `User-Agent` from `env!("CARGO_PKG_VERSION")`;
  `apps/krate-manager/version.json`'s `version` equals it); `check_now_sends_no_cache` (two GETs, the two headers,
  `EDGE_MAX_AGE_SECS == 600`); `rejected_feed_keeps_last_good_copy` (every rejection row of `cases.json`, bytes and
  mtime unchanged, the `E_FEED_INVALID` `Display` with code, count and first path); `offline_returns_cached_flagged_stale`
  (SC-011, whole test < 5 s, one request per case, cache byte-identical); `cache_rules` (SC-013);
  `cache_write_fault_keeps_previous` (a `FaultCacheIo` per `CacheStep`, the `settings_tests.rs:393-420` pattern;
  `SyncDir` on Unix only; one `warn!` per fault naming `E_FEED_CACHE_IO` and the step); `check_logs_one_line` (SC-022);
  `concurrent_checks_leave_one_whole_file` (SC-024, `Barrier(8)`, two servers, listing before/after);
  `fresh_lower_version_replaces_cache` (SC-031); `one_attempt_per_check` (SC-032); `check_through_proxy_writes_cache`
  (SC-030 + TA-9: through a loopback fixture proxy, `ConnectOk` + tunnelled GET writes the cache as SC-009; then
  `set_route` to `ConnectRefused(407)` and to `Close` each give `Cached` with the written model and `E_FEED_OFFLINE`; the
  file's only `.with_proxy(`).
- `#[cfg(not(windows))]`: `check_survives_pac_failure` (SC-036: a `404` PAC and an endless-loop PAC, each URL with a
  planted `?k=v`, through `.with_pac(…).with_pac_host(Some(FakePacHost…))` → `Fresh`, `error: None`, cache written,
  exactly one warn each naming the code and the PAC URL's path without `k=v`; a good PAC adds no warn; the file's only
  `.with_pac(` / `.with_pac_host(`). L-WSL-OWN locally, PENDING-CI.
- Doc tests: one `compile_fail` doctest on each of `PlainName`, `AssetUrl`, `PageUrl`, `AssetName`
  (`krate_manager_core::feed::PlainName(String::new())` does not compile outside the crate). Not listed in
  `required-tests.json` (plan §4.13).
Red: `feed::parse` / `feed::check` panic `not yet implemented`.

**Implement** plan §4.3 verbatim: `parse` (`from_utf8` → `from_slice` → `classify_schema_version` → `contract::validate`
→ `drop_sibling_findings` with one `warn!` each → `E_FEED_INVALID` → the builder); the builder with `PlainName::checked`
(re-running `contract::check_install_value`), `AssetUrl::checked` and `PageUrl::checked` (one private C1 `url` rule,
two types, no conversion between them), `AssetName::from_url` (`url::Url::path_segments`, not decoded), hand hex
decoding, `json_u64`, the per-asset drops with `E_MODEL_RANGE` / `E_MODEL_ASSET_NAME` and the v1 sanitising with
`E_MODEL_V1_ENTRY` (R-7, FR-029(c): a bad v1 key **or** a bad present `download_url` drops the whole entry); `check` =
`check_with(&mut StdCacheIo, …)`; `CacheIo` / `StdCacheIo` (PD-6, temp prefix `.feed-cache.json.`, `persist` drops
`PersistError.file`); exactly one summary line per check (FR-056); one `http::get` per check, no lock, no retry, no
comparison with the cache (FR-057 to FR-059). The newtypes keep T002's declarations (`pub struct <T>(String);`, no
`Deserialize` / `Default` derive, no `From` / `FromStr`, constructors `pub(crate)`): T013's `feed_model_seams_hold`
checks them.

**Green:** L-OWN `feed::` (23 on Windows); then `cargo test -p krate-manager-core --locked` in the same target dir: T004's
and T009's expected reds now pass; L-WSL-OWN `feed::` (24) when available; L-RS (doc tests included) and L-WSL at the
group gate.

**Done when:** all of the above, with `0 failed` in every binary; the notes record the L-OWN wall clock.

### T015 [P] — `evidence/scope-check.js` (spec FR-090 as amended by SI-41, FR-091, SC-044 diff half; plan §4.14, §7.2 FR-090, §8)

**Files (new):** `specs/krate-manager-phase5/evidence/scope-check.js`.

**Test first.** Read `specs/krate-manager-phase4/evidence/scope-check.js` first and keep its form (shebang, header
comment with WHY / usage, `'use strict'`, `childProcess.spawnSync('git', [...])` with an argument array, exit 0 / 1 / 2,
`--self-test`). Write `--self-test` against stub checkers that accept everything: it must fail; quote the line. Then
implement.

**Implement.** Base = the scope base T001 recorded (expected `a145271b`). Paths = `git diff --name-only <base>` plus
`git ls-files --others --exclude-standard`. Every path outside `apps/krate-manager/` must be one of Rule 11's set; no
path may start with `plugins/`, `dsp/`, `cmake/`, `extern/` or `tools/feed-contract/` other than
`tools/feed-contract/tests/manager-ci-shape.test.js`; no other workflow. Roadmap hunks (`git diff -U0 <base> --
specs/Krate-manager-roadmap.md`, old-side ranges) must each lie in: an insertion-only hunk at old line 528 or 529 (the
Phase 5 status line, main loop at close); old lines 534-535 (the "check now" sentence); an insertion-only hunk at old
line 571 (the Phase 6 bullet); an insertion-only hunk at old line 897 adding exactly one line (the RustSec bullet,
TA-19). `specs/krate-manager/probes/results.md` (SI-41): `git diff --numstat <base> -- <file>` is exactly `1 0`, and
the added line starts `| Windows: is "Automatically detect settings" (WPAD) on for a user profile? |`.
`--self-test`: allowed inputs pass; `plugins/x.cpp`, `tools/feed-contract/versions.v2.schema.json`,
`.github/workflows/ci.yml`, a roadmap hunk at old line 540, one at 572, a modified (not inserted) line at 897, a
two-line insertion at 897, a deleted line in `results.md`, two added lines in `results.md`, and one added `results.md`
line with other text each fail.

**Green:** `node --check`; `--self-test` exit 0. (The real run is T018's and L-CLOSE's.)

**Done when:** both pass.

### T016 [P] — `evidence/close-lane.js` and `evidence/model-range-node.js` (spec SC-001, SC-016 local, SC-017, SC-019, SC-023, SC-037 Node record, SC-038 records, SC-044 record; plan §4.1, §7.4, §8, TA-4, TA-7, TA-18, TA-24)

**Files (new):** `specs/krate-manager-phase5/evidence/close-lane.js`, `specs/krate-manager-phase5/evidence/model-range-node.js`.

**Test first.** Read `specs/krate-manager-phase4/evidence/close-lane.js` first and keep its form (header listing every
row, `run()` with a per-child wall-clock ceiling and `killTree` of its own child, `record()`, `extractStep(yamlText,
name)`, raw logs under `F:/tmp/gate/krate-manager-phase5-close/<timestamp>/`, summary `evidence/close-lane.log`,
`evidence/timings.md` with each row's wall clock, `--skip-tauri-build` → last line `CLOSE LANE PARTIAL …`, otherwise
`CLOSE LANE GREEN | CLOSE LANE RED`, exit 0 / 1). Its red-first: `node close-lane.js --self-test` (add it:
`extractStep` over an in-memory YAML with two steps returns the second's `run:` block de-indented, and a missing step
throws) fails against a stub; quote the line. `model-range-node.js`'s red-first: run it before the edits exist (a stub
printing nothing) and see the six expected verdict lines missing.

**`model-range-node.js`** (plan §7.4, SC-037 Node parity): reads `tools/feed-contract/fixtures/feeds/valid/full-v2.json`
and `v1-only.json`, applies the six edits of `model_range_cases` rows 1-6 (plan §7.1 row SC-037) in memory, prints
`validateFeed(doc, { schema })` (`tools/feed-contract/contract.js:485`) for each, one line per row (`row <n> <schema>
findings=<count>`), and exits 1 unless every count is 0. It writes nothing but stdout.

**`close-lane.js` rows.** Wherever a row says "step `<name>`", it extracts that step's `run:` block from
`.github/workflows/manager-ci.yml`, removes the indentation and runs it with `C:/Program Files/Git/bin/bash.exe -c`,
cwd `apps/krate-manager/`, env `RUNNER_OS=Windows`, `RUNNER_TEMP=<log dir>`.
- **A0:** `rustc --version`, `node --version`, `df -h` of the repo root only. **A0b:** `git status --porcelain`
  snapshot. **A0c:** the base binary size from `evidence/base-binary-size.txt` (T002, TA-24).
- **A (concurrent):**
  - **A1** (one chain; the cargo commands share one target-dir lock): step `cargo fmt` → step `clippy` → step `clippy
    dev-feed` → L-LIVE → step `clippy-guard` → step `cargo test` → `cargo test -p krate-manager-core --locked -- --list
    > evidence/list-default.txt` (0 lines naming `live_feed_parses`) → `cargo test -p krate-manager-core --features
    live-network --test live_feed --locked -- --list --ignored > evidence/list-live.txt` (exactly 1) → the nine `cargo
    tree` records of plan §7.4 into `evidence/`: `tree-core.txt` (`ureq v3.5.0`, exactly one `rustls v` version,
    `rustls-platform-verifier v0.7.1`, `ring`, `webpki-roots`, no `rquickjs`, and none of `tauri`, `tokio`, `reqwest`,
    `hyper`, `dirs`, `directories`, `home`); `tree-core-windows.txt` (`--target x86_64-pc-windows-msvc`: no line
    containing `rquickjs`); `tree-core-linux.txt` (`--target x86_64-unknown-linux-gnu`: `rquickjs v0.14.0`,
    `rquickjs-core v0.14.0`, `rquickjs-sys v0.14.0`); `tree-app.txt` (`-p krate-manager --target all`: `windows-sys`,
    `system-configuration v0.8.0`, exactly one `core-foundation v` version); `tree-rquickjs-features-linux.txt` and
    `tree-rquickjs-features-macos.txt` (`-e features -i rquickjs`: feature `std` only); `tree-webpki.txt`;
    `tree-dups.txt` (`--workspace -d --target all`: no second `rustls` or `core-foundation`); `tree-core-foundation.txt`
    (`-i core-foundation --target all`: one version).
  - **A2** (one chain): `npm ci` only if `node_modules` is absent → step `typecheck` → step `lint` → step `vitest`.
  - **A3** (repo root): L-NODE whole set to a TAP file → L-TAP; L-VLINT; `node
    specs/krate-manager-phase5/evidence/model-range-node.js > evidence/model-range-node.txt` (exit 0).
  - **A4:** L-YAML on `manager-ci.yml` and `feed-tools.yml`. **A5:** L-LINT. **A6:** L-INDEX (if red only because this
    phase's spec files changed the index, rerun `node tools/gen-specs-index.js` once and re-check). **A7:**
    `scope-check.js --self-test`, then L-SCOPE. **A8:** L-XCHECK (TA-7: the expected build-script line recorded verbatim
    as the skip; anything else red). **A9:** the SC-021 grep `grep -rnE --
    '--test-threads|--no-file-parallelism|fileParallelism|maxWorkers|pool:' apps/krate-manager
    --exclude-dir=node_modules --exclude-dir=target` prints nothing. **A10 (TA-4):** `grep -rnE
    'todo!\(|unimplemented!\(' apps/krate-manager/crates apps/krate-manager/src-tauri/src --include='*.rs'` excluding
    `*_tests.rs` and `*/tests/*` prints nothing. **A11 (L-WSL, TA-18):** `node
    specs/krate-manager-phase5/evidence/wsl-lane.js --out specs/krate-manager-phase5/evidence/wsl-core.txt`; last line
    `WSL LANE GREEN`, and its information line names all thirteen `cfg(not(windows))` tests of plan §4.13 as passed (an
    early signal recorded beside the PENDING-CI rows, never instead of them). **A12 (SC-044):** `grep -n 'rustsec.org'
    specs/Krate-manager-roadmap.md > evidence/rustsec-line.txt`: exactly one line, holding `rquickjs`, `rquickjs-core`,
    `rquickjs-sys` and `pin bump`; `git diff --numstat <base> -- specs/krate-manager/probes/results.md` is `1 0`.
- **B (after A1 and A2):** **B1** step `required tests ran` (expects `::notice title=required tests ran (Windows)::132
  cargo + 3 vitest`); **B2** step `tauri build` (40 min ceiling); **B3** step `build warnings`; **B4** step `dev-feed
  absent`; **B5** step `roster absent` (`::notice title=roster absent (Windows)::<n ≥ 9> names, 0 hits in <m> files`,
  the positive control found), output copied to `evidence/roster-absent.txt`; **B6** step `artifact version`; **B7**
  `ls -l target/release/krate-manager.exe` → `evidence/binary-size.txt` beside A0c's base value (plan §4.1: with R-15
  the Windows binary carries no QuickJS; the growth is `ureq` / `rustls` / `ring` and the phase's code).
- **C (last):** **C1** `git status --porcelain` again, minus lines under `specs/krate-manager-phase5/evidence/`, equal
  to A0b (SC-001). **C2** L-SCOPE again.

**Green:** `node --check` on both; `close-lane.js --self-test` exit 0. `model-range-node.js` needs no app build: run it
once now (it reads fixtures only) and record its six lines; it is expected green (the Node validator accepts the six
documents, plan PD-8).

**Done when:** all three pass. **G3 gate:** L-RS (nothing else running but L-WSL) with no expected red; L-DEVFEED;
L-LIVE; L-GUARD; L-LINT; L-WSL with no expected red.

---

## G4 — Integration (sequential)

### T017 — `manager-ci.yml`, `required-tests.json` and the shape test (spec FR-083, FR-084, FR-085, SC-016 CI part, SC-019 part, SC-021 part, SC-023; plan §4.12, §4.13, SI-4, SI-13, SI-17, SI-39, TA-8)

**Files:** `.github/workflows/manager-ci.yml` (CRLF), `apps/krate-manager/tests/required-tests.json` (CRLF),
`tools/feed-contract/tests/manager-ci-shape.test.js` (CRLF), `.github/workflows/feed-tools.yml` (only if L-TAP's
top-level count moves; plan §4.12 expects it not to).

**Test first.** In `manager-ci-shape.test.js` (plan §4.12, SI-4): `PATHS` (`:147`) gains `"- 'tools/feed-contract/**'"`;
`STEPS` (`:156-…`) gains `'roster absent'` after `'dev-feed absent'`; `ASSERTION_STEPS` (`:192-202`) gains `'roster
absent'`; `COMMANDS` (`:205-…`) gains `'roster absent': ['node - "$bin" <<\'ROSTER\'']`; `EXPECTED_REQUIRED` (`:318-…`)
becomes the plan §4.13 lists plus TA-8's `fixture_server::tests::replies_behave_as_scripted` in `cargo.all`; inside the
existing top-level test `manager-ci.yml keeps the Phase 4 shape` (`:636`) add subtests asserting the string
`live-network` occurs in neither `manager-ci.yml` nor `apps/krate-manager/package.json`, plus mutations (the file's
`mutation: ${name}` loop, `:663`) for: the path line removed, the `roster absent` step removed, `live-network` added to
a step, a required-test name missing. Run L-NODE on this file alone: red (the real workflow lacks the line and the
step, `required-tests.json` differs); quote the first failing assertion.

**Implement.**
- `manager-ci.yml`: both path filters (`:7-10`, `:12-15`) add `- 'tools/feed-contract/**'`; the step `roster absent` of
  plan §4.12, verbatim, after `dev-feed absent` (`:182-192`). No other change; no step names `live-network`; no serial
  flag (Rule 6).
- `required-tests.json` (counted this session from plan §4.13; the Phase 4 entries keep their order, the new names follow
  them in printed form, SI-13): `cargo.all` 48 → **130** (81 from §4.13 by module: contract 8, feed 23, version 6, http
  21, sysproxy 9, pac 5, fixture_server 2, context 1, integration 6; plus TA-8's 1); `cargo.windows` 1 → **2**
  (`version::tests::native_candidates_windows`); `cargo.macos` 1 → **17** (`version::tests::native_candidates_macos`,
  `sysproxy_cf::tests::from_cf_proxies_cases`, `sysproxy::tests::bounded_run_abandons_and_reaps`, and the thirteen
  `cfg(not(windows))` names of §4.13, SI-39); `cargo.linux` 1 → **16** (`version::tests::native_candidates_linux`,
  `sysproxy::tests::bounded_run_abandons_and_reaps`, and the same thirteen); `vitest` unchanged (3). The step's notice
  then reads `132 cargo + 3 vitest` on Windows, `147` on macOS, `146` on Linux.
  `pac::tests::fake_pac_host_answers_from_its_table`, `live_feed_parses` and the four doctests are not listed.
- **Name check (local):** every `cargo.all` and `cargo.windows` name occurs exactly once as a `test <name> ... ok` line
  of a fresh `cargo test --workspace --locked 2>&1 | tee F:/tmp/gate/km5-T017/cargo-test.txt` (run the workflow's
  `required tests ran` step text against it, as L-CLOSE B1 does); every `cargo.macos` / `cargo.linux` name exists as a
  `fn` under its `#[cfg]` in its test file (`grep -n`), and every `cargo.linux` name occurs as a passing line in the
  last L-WSL log (G3 gate; the macOS-only names cannot be checked locally).

**Green:** L-NODE (whole set, `# fail 0`), L-TAP (`TAP plan=1..59 top-level=59`), L-YAML on `manager-ci.yml` (and
`feed-tools.yml` if touched), L-LINT, the name check's `::notice title=required tests ran (Windows)::132 cargo + 3
vitest`.

**Done when:** all of the above.

### T018 — The short close run, the repo lints, cleanup and the PENDING-CI list (spec SC-001, SC-019 records, SC-023, SC-037 record, FR-090, FR-091; plan §7.3, §7.4, §8, TA-4, TA-7)

**Files:** `specs/krate-manager-phase5/evidence/*` outputs only (no source change).

**Run in this task** (each short; record wall clock and last result line in the notes): `node --check` on the four
evidence scripts; `wsl-lane.js --self-test`, `scope-check.js --self-test` and L-SCOPE; close-lane rows A3 to A10 and A12
by hand (the same commands); L-RS, L-DEVFEED, L-LIVE, L-GUARD and L-TS once; L-LINT (`node
tools/lint-serial-test-runs.js`). Then `node tools/clean-test-output.js --apply`, and remove every
`F:/tmp/km5-target/` left by G2-G3 (keep WSL's `$HOME/km-target`: the close lane reuses it warm, plan §8).

**Not run here**, with the reason in the notes: `npm run tauri build`, the roster scan of the built binary, rows B1-B7
and A11 (L-WSL), the `cargo tree` records of A1 (deferred to the main loop at close: run `node
specs/krate-manager-phase5/evidence/close-lane.js`); `node tools/check-portability.js` (no C++ file changed, FR-090);
`node tools/lint-plugin-editor-baseline.js` (no plugin editor changed; `apps/` is outside its `plugins/` walk);
`run-gate.js` / `header-consumers.js` (no C++ lane, plan §8); pluginval, clang-tidy, the CPU lane (no plugin binary, no
C++, no CPU-budget test).

**Done when:** every command above passes; the notes say "deferred to the main loop at close: run `node
specs/krate-manager-phase5/evidence/close-lane.js`"; the PENDING-CI table below matches the job names in
`manager-ci.yml` (`check (${{ matrix.os }})`, matrix `[windows-latest, macos-latest, ubuntu-24.04]`, `:27-32`) and
`feed-tools.yml` (`node-test (${{ matrix.os }})`, matrix `[ubuntu-latest, macos-latest, windows-latest]`, `:49-54`).

**Main loop at close (not a task):** run L-CLOSE; add the Phase 5 status line to the roadmap (Phase 4's form, roadmap
line 467); write `compliance.md`.

---

## PENDING-CI (macOS and Linux evidence; only the user can trigger it)

`manager-ci.yml` runs on the user's push: after T017 its path filter lists `apps/krate-manager/**`, the workflow itself,
`tools/lint-manager-version.js` and `tools/feed-contract/**`. `feed-tools.yml` runs on the same push. A Rust row closes
only on its leg's `required tests ran (<os>)` `::notice`, whose list holds the row's test names; a green `cargo test`
step alone closes nothing. L-WSL greens are recorded beside these rows and close none of them (plan §7.3, §8).

| SC | Workflow · job (exact name) · step | Assertion that closes the row |
|---|---|---|
| SC-002 to SC-006, SC-008 to SC-015, SC-017, SC-018, SC-020 to SC-022, SC-024 to SC-028, SC-030, SC-031, SC-032, SC-035 (pure half), SC-037 to SC-039, SC-041, SC-043 (Rust, `cargo.all`) | `manager-ci.yml` · `check (macos-latest)`, `check (ubuntu-24.04)` · `required tests ran` | `::notice title=required tests ran (macOS)::147 cargo + 3 vitest` and `… (Linux)::146 cargo + 3 vitest` (the 130 `all` names). Covers POSIX rename / directory fsync for the cache, the C1 parity, the OS-path walk over a fake (SC-041 "on every host"), and `ring` / `rustls-platform-verifier` building there |
| SC-007 native resolution | `check (macos-latest)` · `required tests ran` (`macos` list: `version::tests::native_candidates_macos`); `check (ubuntu-24.04)` (`linux` list: `version::tests::native_candidates_linux`) | the two notices above. The arm64 runner proves the `aarch64` branch; the macOS `x86_64` branch stays unproven until an Intel host runs it (plan §7.3) |
| SC-013 `SyncDir` fault step | `check (macos-latest)`, `check (ubuntu-24.04)` · `required tests ran` | `feed::tests::cache_write_fault_keeps_previous` in the `all` list (its `#[cfg(unix)]` `SyncDir` case runs only there) |
| SC-016 built-app scan | `check (macos-latest)`, `check (ubuntu-24.04)` · `roster absent` | `::notice title=roster absent (macOS)::<n ≥ 9> names, 0 hits in <m> files` and `… (Linux)…`. Windows closes locally (L-CLOSE B5) and on `check (windows-latest)` |
| SC-029 reader seams on their own OS | `check (macos-latest)` · `required tests ran` (`macos` list: `sysproxy::tests::bounded_run_abandons_and_reaps`); `check (ubuntu-24.04)` · `required tests ran` (`linux` list: the same, with its `/proc` reap assertion) | the two notices. `win_ie_strings_are_read_and_freed_once`, `kioslaverc_path_cases`, `kde_file_read_cases` run on every leg (`all`; Windows proves the portable-fixture rule locally). The OS calls in `src-tauri/src/os_proxy.rs` (WinHTTP, `SCDynamicStoreCopyProxies`, spawning `gsettings`) stay untested by design (FR-049); their compile is proven by `clippy` and `tauri build` on each leg |
| SC-030 per-hop bypass over IPv6 loopback | `check (macos-latest)`, `check (ubuntu-24.04)` (and `check (windows-latest)`) · `required tests ran` | `http::tests::no_proxy_bypass` and `fixture_server::tests::binds_loopback_port_zero` in the list; a leg without `[::1]` fails them (no skip) |
| SC-033 to SC-036, SC-042: the in-process engine and the manager's PAC fetch (R-8, R-15) | `check (macos-latest)`, `check (ubuntu-24.04)` · `required tests ran` | the `macos` / `linux` lists hold the thirteen `cfg(not(windows))` names of plan §4.13 (`pac::tests::url_argument_cases`, `script_world_cases`, `script_failure_cases`, `dns_bound_cases`; `http::tests::pac_path_proxy`, `pac_path_direct`, `pac_list_walk`, `pac_per_hop`, `pac_fetched_once_never_via_proxy`, `wpad_discovery_walk`, `pac_fallback_to_static_proxy`, `pac_body_cap`; `feed::tests::check_survives_pac_failure`). **The only proof of the in-process evaluator:** this box cannot compile it (R-15), L-XCHECK cannot cross-build it (TA-7), L-WSL is an early signal only. The engine's build there (`rquickjs-sys`'s C compile with the runner's `cc`, the bundled bindings) is proven by the same legs' `cargo test` and `tauri build`; the macOS leg alone proves the Apple target |
| SC-037 model drops | `check (macos-latest)`, `check (ubuntu-24.04)` · `required tests ran` | `feed::tests::model_range_cases` in the list |
| SC-038 dependency set | every leg · `required tests ran` | `reader_dependencies_are_the_ruled_set` in the list; the Windows leg's green `cargo test` with `rquickjs` absent from its graph and the local `tree-core-windows.txt` record prove "not linked on Windows"; the macOS leg's `tauri build` is the only proof that the macOS app links `rquickjs-sys` |
| SC-039 desktop choice and env guard | every leg · `required tests ran` | `sysproxy::tests::linux_desktop_choice_cases` and `src_tauri_names_feed_host_once_and_reads_env_only_in_os_roots` in the list |
| SC-040 CF conversion | `check (macos-latest)` · `required tests ran` | the `macos` list holds `sysproxy_cf::tests::from_cf_proxies_cases`; its first compile is that leg's `clippy` (TA-7); the `core_foundation` confinement half runs on every leg (`core_dependencies_are_the_fr007_set`) |
| SC-041 OS-path walk and failure rules (FR-092) | every leg (`cargo.all`) · `required tests ran` | `http::tests::os_pac_walk`, `http::tests::os_pac_failure_cases`, `pac::tests::winhttp_answer_cases` in each list (local on this box too; the macOS and Linux legs prove the path is OS-independent) |
| SC-043 PAC clock and offset parsers | every leg (`cargo.all`) · `required tests ran` | `pac::tests::pac_clock_cases`, `sysproxy::tests::parse_utc_offset_cases`, `sysproxy::tests::offset_range_cases` in each list (local too) |
| T007 / T010 per-OS code compiles | `check (macos-latest)`, `check (ubuntu-24.04)` · `clippy`, `tauri build` | both steps green: the first compile of `sysproxy_cf.rs` and of `os_proxy.rs`'s macOS / Linux arms (`OsPacHost` included) |
| SC-019 (shape ban) and SC-023 | `feed-tools.yml` · `node-test (ubuntu-latest)`, `node-test (macos-latest)`, `node-test (windows-latest)` · `node --test` | step green with `TAP plan=1..59 top-level=59`; read the job conclusion through the jobs API (the step emits no annotation, Phase 4 D21) |
| SC-001 (CI half) | `check (windows-latest)` · every step | every step green, with `::notice title=required tests ran (Windows)::132 cargo + 3 vitest`, `roster absent (Windows)` and `build warnings (Windows)`; the local half closes on L-CLOSE |

**Untested by design, not PENDING-CI** (plan §7.3 last rows, §9): the real PAC host's OS calls (`OsPacHost`: on Windows
the `WinHttpGetProxyForUrlEx` sequence and its callback, about 80 lines of `unsafe` compiled and linted here and
reviewed against PD-35; on macOS / Linux the WPAD candidate, local offset, real DNS, `myIpAddress`) and each reader's
OS call; only their pure steps are tested.

**Every Windows row closes locally:** SC-001, SC-016 (Windows), SC-017, SC-019, SC-038 and SC-044 records from L-CLOSE;
SC-002 to SC-015, SC-018, SC-020 to SC-032, SC-035 pure half, SC-037 to SC-039, SC-041, SC-043 (the rows that run on
Windows) from L-RS plus L-CLOSE B1's notice; SC-023 from L-NODE + L-TAP; SC-037's Node record from
`model-range-node.txt`; FR-090 / FR-091 from L-SCOPE and L-INDEX. SC-029 closes locally for the tests that run on every
leg; its Unix test is PENDING-CI. SC-033 to SC-036 and SC-042 have no local closure beyond their every-host rows (their
engine rows are PENDING-CI, with L-WSL as the early signal). SC-040 has no local half beyond its guard.

**User steps after the push** (not exit criteria): read the annotations above. The opt-in live test is a manual user
step only: `cargo test -p krate-manager-core --features live-network --test live_feed -- --ignored` (FR-072).

---

## Defects found

A gate fixer or the main loop appends here: the red, its cause, the task that owns the file, and the fix or
the ruling it needs.

- G2 gate, `config_guard::real_src_tauri_config_passes_the_guard` (T012's guard, T010's `os_proxy.rs`, T009's
  `http_tests.rs`): the guard matched `HTTPS_PROXY` / `ALL_PROXY` / `NO_PROXY` as substrings, so the WinHTTP constant
  `WINHTTP_ACCESS_TYPE_NO_PROXY` (`os_proxy.rs`) read as `NO_PROXY`, although FR-063 names tokens. Two comments also
  named `NO_PROXY` as a whole word (`os_proxy.rs` SAFETY comment, `http_tests.rs` FR-048 (c) comment). Fix: the guard's
  confinement checks match whole tokens (`holds_token`, comments still counted), with a core plant and a quiet
  `WINHTTP_ACCESS_TYPE_NO_PROXY` / `MY_HTTPS_PROXY_X` check; both comments reworded. No test weakened.
- G4 / T018, L-GUARD (`clippy-guard` step, `manager-ci.yml:97-115`; close-lane A1e), owner T002: the fixture crate
  `tests/fixtures/clippy-guard` has its own `Cargo.lock` (last changed ac307f7e) that never received Phase 5's
  dependencies, so `--locked` refused ("cannot update the lock file"); red since G1 (T002's guard.txt). Fix (main loop,
  2026-10-31): `cargo metadata --manifest-path tests/fixtures/clippy-guard/Cargo.toml --format-version 1 --offline` added
  41 packages; the fixture lock now holds the same 454 name@version pairs as the workspace lock (only the fixture root
  differs). L-GUARD re-run: `rc=101 disallowed=3 from_os=1 Roots::new=1 std::env::var=1 other-errors=0`, 5 s.
