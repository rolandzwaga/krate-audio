# Implementation Plan: Krate Manager Phase 10 — The Elevated Helper and the Windows / macOS Install Drivers

**Spec:** `specs/krate-manager-phase10/spec.md` (DRAFT revision 3; decisions D1-D19, clarifications Q1-Q8 of 2026-11-01,
`spec.md:23`, `:1361-1376`)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 10 (lines 709-810); Decisions STRATEGY (23), Q4 (27), Q5 (28), Q7 (30),
ROSTER (33); Phase 9's `InstallDriver` (673-674); Phase 11 (812-877, the universal helper handed over by FR-123 (d)); Phase 12
(879-933, the user-performed launcher checks); edges 4→10, 6→10, 7→10, 9→10, 10→12 (954-960); Cross-Cutting Constraints
(987-1028, in particular the hosted-runner exception, 994-996).
**Status:** DRAFT plan, revision 2, 2026-11-01 (review round 1 applied; see Review notes). Verified against `feat/krate-manager` at `4a8b3e2a` ("feat(workflows):
krate-manager app lane runs core's clippy and tests in WSL", `git rev-parse --short HEAD`; the spec was verified at its parent
`2be37176`; `4a8b3e2a` touches only `.claude/workflows/krate-manager-phase.js`). Untracked: `specs/krate-manager-phase10/`.
**Lanes:** the **app lane** (fmt / clippy / clippy-guard / `cargo test --workspace` / npm typecheck / lint / vitest /
`node --test "scripts/*.test.js"` / the version lint, plus the WSL step, which must also cover `-p krate-manager-helper`,
SI-22) and the **pipeline lane** (`node --test tools/feed-contract/tests/`, because `manager-ci-shape.test.js` changes;
YAML parse of `manager-ci.yml`; and `feed-tools.yml`'s own TAPCHECK replayed locally, §7.4 step 3, so a changed top-level
plan count (`const PLAN = "1..59"`, `.github/workflows/feed-tools.yml:86`, refused otherwise at `:126-127`) is red before
the push, not after it). No C++, CMake, plugin or DSP file changes (FR-122), so there is no `run-gate.js` /
`header-consumers.js` lane.

**Build preconditions.** The spec edits of §0.2 land before `tasks.md`. SI-1 to SI-8 are **blocking**: built as the spec
reads, SI-1 makes two required test names impossible (`main::tests::*` cannot be a library path while `main.rs` is the
binary root); SI-2 makes the `changes` job fail the existing shape test (`manager-ci-shape.test.js:797` bans
`ubuntu-latest`); SI-3 makes SC-029's "fresh `CommandEnv` is unblocked" unpassable with a process-static guard and makes
parallel driver tests refuse each other; SI-4 makes FR-076a read Windows facts in a macOS-target test; SI-5 to SI-8 make
the existing guards (`injection.rs`, `roster.rs`, `config_guard.rs`) red on code the spec requires.

---

## 0. What this plan adds to the spec

The spec fixes requirements, names, codes and test names. This plan fixes the helper crate's module map and signatures,
the `run` algorithm with its exit points, the job / result codecs on both sides, the seams and their fakes, the core's
drivers, guard and job writer, the src-tauri launcher, the bundle script, the CI jobs step by step, every existing guard
that has to widen and why, and the per-SC test design. Where the tree contradicts a spec sentence, the conflict is in §0.2.

### 0.1 Plan decisions

| Id | Decision | Why (evidence) | Spec text touched |
|---|---|---|---|
| PD-1 | **Helper module map.** `crates/helper/src/`: `lib.rs` (`run`, `HelperOs`, `Exit`), `cli.rs` (arguments, `main_with`, the `--ci-roots` reader), `error.rs` (`HelperError`, the code catalogue `CODES`), `job.rs`, `result.rs`, `roots.rs`, `validate.rs`, `digest.rs` (the only `sha2` user), `ops.rs`, `classify.rs`, `seams.rs` (traits + `pub mod fake`), `os.rs` (the real seams, the only `Instant` / FFI home), `main.rs`. Tests in `<stem>_tests.rs` files (`#[cfg(test)] #[path = "<stem>_tests.rs"] mod tests;`), the core's convention, so the roster scan skips them (`roster.rs:80-81`). | `cli.rs`: SI-1. `digest.rs`: the workspace `sha2` token confinement (`injection.rs:2969-2976`) needs one named home. | New components table |
| PD-2 | **Job parse without a hand-written duplicate-key scanner.** `RawJob { job_version: u64, ops: Vec<RawOp> }` and `RawOp { op: String, #[serde(default)] file: Slot, sha256: Slot, app_id: Slot, pkg_id: Slot }`, both `#[serde(deny_unknown_fields)]`; `enum Slot { Absent (default), Present(serde_json::Value) }` with a `Deserialize` that always yields `Present` (null included). serde_derive's struct visitor already returns `duplicate_field` on a repeated key (`serde_derive-1.0.229/src/de/struct_.rs:269`), for `op` as for every field; `deny_unknown_fields` refuses any other key; `Slot` distinguishes an explicit `"app_id": null` from an absent key, which `Option<T>` cannot. Per kind, exactly its slots must be `Present` and strings, the rest `Absent`. | FR-002 asks for a duplicate-key check "over the raw object"; the derive gives the same guarantee for a flat, fixed key set. | FR-002 |
| PD-3 | **`run` order and exit points** (§4.1.2): roots canonicalized → job location → result file reserved (`create_new`) → job read (≤ 64 KiB) → hash → parse → ops. Every exit-4 point is before the reservation; every exit-3 point after it, so an exit-3 result file always exists. | D15 says exit 3 writes `job_error`, exit 4 writes nothing; FR-003's `E_HELPER_RESULT_EXISTS` must win over a hash or parse error. | FR-003, FR-021, FR-023, D15 |
| PD-4 | **`run` returns `Exit { status: u8, code: Option<&'static str> }`**, `impl From<Exit> for ExitCode`. | `std::process::ExitCode` exposes no code to compare; SC-001's exit-4 rows must name `E_HELPER_JOB_LOCATION` with nothing written. | FR-050 (`RunFn`) |
| PD-5 | **`AllowedRoots { os: OsRoots, state_dir: StateDirRule }`, `OsRoots::{Windows(WindowsRoots), Macos(MacosRoots), None}`.** `WindowsRoots { vst3_vendor, preset_vendor, run_base }`; `MacosRoots { vst3, components, presets, applications, volume, run_base, run_ancestors: Vec<PathBuf> }`. `run_base` (D7's parent of the run directory, Windows `%SystemRoot%\Temp`, macOS the helper folder), `run_ancestors` (D7's root-owned chain above the helper folder; production `["/Library", "/Library/Application Support"]`) and `volume` (the base `pkgutil`'s `location` is relative to, production `/`) are roots too, so every op runs against temp dirs on every leg: the `run_dir_ancestors_ok` chain is `run_ancestors` + `run_base` + the new run directory, never a literal `/Library` path (a literal chain would lstat absent paths on the Windows and Linux legs and user-owned temp dirs on macOS, failing every pkg row `E_HELPER_RUN_DIR`). `--ci-roots` replaces the plug-in, preset and application roots and the state dir only, never `run_base`, `run_ancestors` or `volume` (the CI jobs prove the real run directory, SC-017 / SC-018). `production()` on Linux is `OsRoots::None`. | D7, D18; FR-033's "joined to `/<location>`" is Unix text that cannot run against a Windows temp dir. | D3, D7, FR-033, FR-052 (SI-16) |
| PD-6 | **The job and the installer source are opened without following a final link, without blocking, regular files only.** One function, `os::open_regular_no_follow(path) -> io::Result<File>`, used by `OsHelperIo::{open_source, read_bounded, read_tail}` **and** by `seams::fake::TempIo` (so SC-005's hook rows exercise the production open, not a fake one). Unix: `OpenOptions::new().read(true).custom_flags(O_NONBLOCK \| O_NOFOLLOW)` (`std::os::unix::fs::OpenOptionsExt::custom_flags`), constants defined in `os.rs` per target (the workspace has no `libc`; values from `libc-0.2.191` source: Apple `O_NONBLOCK = 0x4`, `O_NOFOLLOW = 0x100`, `src/unix/bsd/mod.rs:275-276`; Linux x86_64 `O_NONBLOCK = 2048`, `O_NOFOLLOW = 0x20000`, `src/unix/linux_like/linux/gnu/b64/x86_64/mod.rs:294`, `:448`; Linux aarch64 `2048`, `0x8000`, `…/aarch64/mod.rs:206`, `:359`; any other Unix target is a `compile_error!`), then `file.metadata()` must be a regular file and its `(dev, ino)` (`MetadataExt`) must equal `symlink_metadata(path)`'s; a final link fails the open (`ELOOP`), a FIFO opens at once under `O_NONBLOCK` and fails the regular-file check. Windows: `custom_flags(FILE_FLAG_OPEN_REPARSE_POINT)` (`windows-sys` `Win32/Storage/FileSystem/mod.rs:1501`), then the handle's metadata must be a regular file, not a symlink. Any mismatch is `E_HELPER_SOURCE` (installer) / `E_HELPER_JOB_READ` (job). | POSIX `open(2)`: a read-only open of a FIFO without `O_NONBLOCK` "shall block the calling thread until a thread opens the file for writing" (https://pubs.opengroup.org/onlinepubs/9799919799/functions/open.html), and a plain `File::open` follows a final link, so a FIFO swapped into the user-writable staging or `jobs/` dir after the lstat would hang the root helper in `open` with no timeout. The hash binding keeps integrity either way; this keeps the helper from hanging. | FR-023, FR-030 step 2, SC-005 |
| PD-7 | **The real seams are `cfg(unix)` / `cfg(windows)`, not `cfg(target_os = "macos")`.** `OsHelperIo`, `OsRunner` compile on Linux too (the Linux binary rejects every op, D18), so the WSL step and `check (ubuntu-24.04)` compile and lint the macOS code paths. Only `AllowedRoots::production()` branches on `target_os`. | The WSL gate exists because Windows gates never compile `cfg(unix)` code (`krate-manager-phase.js:596-599`). | D18 |
| PD-8 | **The helper's fakes live in `seams::fake` behind a helper feature `test-support`**, and `crates/core` takes `krate-manager-helper = { workspace = true, features = ["test-support"] }` as a **dev**-dependency. The helper already dev-depends on core (`crates/helper/Cargo.toml:10-11`); a mutual dev-dependency builds and tests on cargo 1.99.1 (scratch probe this session: two crates, each dev-depending on the other with `test-support`, `cargo test --workspace` ran both suites green). Neither normal graph contains the other crate, so no type is duplicated. The core's loopback `Elevation` is `#[cfg(test)]` code in core (`install_loopback_tests.rs`), never in `test_support.rs`, which the helper's dev build compiles. | SC-003, SC-014, SC-016 need the helper library in-process in core tests; `test_support` cannot name the helper without a normal-dependency cycle. | D1, New components (SI-19) |
| PD-9 | **The elevation guard lives in the `Elevation` object.** `trait Elevation: Sync { fn helper(&self) -> Option<&Path>; fn launch(&self, &ElevatedCommand, Duration) -> LaunchOutcome; fn guard(&self) -> &ElevationGuard; }`; `ElevationGuard` is a core type (all logic in core). Production has one `NativeElevation` per process, in Tauri state as `Arc<NativeElevation>`; each test fake owns its own guard, so a "fresh `CommandEnv`" with a fresh fake is a restart, and parallel tests never share a guard. | A `static` like `BUSY` (`install.rs:2820`) survives a fresh env (SC-029 fails) and is shared by every test thread (two unrelated Windows-driver tests would refuse each other). | D11, FR-071, FR-078, FR-078a (SI-3) |
| PD-10 | **`InstallEnv` gains `elevation`, `host_io` and `host: &dyn HostFacts`; `CommandEnv` gains `elevation` and `host_io`** (the commands already take `host`, `install.rs:2850-2858`, and pass it into `InstallEnv`). | FR-076a re-runs `detect_plugin(os, ctx, entry, host, listing)` (`detect.rs:981-987`); `PlatformFacts` dispatches to `NativePlatform` (`detect.rs:189-204`), so building it from `host_io` would read Windows facts in a macOS-target test. | FR-071, FR-076a (SI-4) |
| PD-11 | **The core keeps its own copies of the helper's timeouts and code catalogue**, cross-checked by SC-011's `core_written_jobs_parse` (one more block of assertions in that test: every `krate_manager_helper::error::CODES` entry is known to `install::job::helper_code`, and the five timeouts are equal). An unknown code in a result maps to `E_INSTALL_NO_RESULT`. | D1: the core never depends on the helper; `InstallError` variants hold `&'static str` codes, never a string from a file. | D14, FR-076, FR-079 |
| PD-12 | **The core's job writer takes plain data**: `install::job::JobOp::{RunInnoInstaller { file, sha256 }, RunInnoUninstaller { app_id }, RunPkgInstaller { file, sha256 }, UninstallPkg { pkg_id }}`; the drivers build `JobOp`s only from a `VerifiedArtifact` or a gate `RemoveBasis`. | SC-011 (a helper integration test) must write jobs through the core; it cannot mint a `VerifiedArtifact` (`injection.rs:2629-2700`). | FR-073 |
| PD-13 | **`app_id` is the uninstall key up to and including its first `}`.** The contract already fixed the key to `^\{GUID\}_is1$` at feed parse (`contract.rs:1049-1055`); the helper re-validates `app_id` (FR-031). | `roster.rs:201-207` bans the `_is1` text in `install.rs`; no literal is needed this way. | D4 (SI-7) |
| PD-14 | **`platform/mod.rs` gains `pub const KILLALL_ARGV: [&str; 3] = ["/usr/bin/killall", "-9", "AudioComponentRegistrar"];`** (cfg-free, so the macOS driver is tested on every leg); `injection.rs` `PLATFORM_LITERALS` gains the three literals. `/usr/bin/osascript` lives in `src-tauri/src/os_elevation.rs`. | `install.rs` may not hold `"/usr/` (`injection.rs:23-31`, `:1316-1320`); `platform/macos.rs` is `cfg(target_os = "macos")` (`platform/mod.rs:104-110`). | FR-077, FR-112 (SI-5) |
| PD-15 | **The new `InstallError` `Display` texts are FR-133's sentences** (`E_INSTALL_REFUSED: Administrator permission was not given; nothing was changed.`, `E_INSTALL_LOCKED: A plug-in file is in use. Close your DAW and retry.`, …). The UI shows a rejection verbatim (`status.ts:150-154`), so no code-to-text table is added in TypeScript. `E_INSTALL_FORMAT`'s text becomes "the artifact is not this system's installer format" (no test asserts the old text; the only use is the unit pattern at `install_tests.rs:2459`). | One text source; FR-079 and FR-133 disagree on `E_INSTALL_LOCKED` (SI-10). | FR-079, FR-133 |
| PD-16 | **The notice is chosen by `env.os` in `obtain_and_run`** (`install.rs:3035` today hard-codes `RescanDaw`): `Os::Macos` → `RescanDawMacos`, else `RescanDaw`; Remove keeps `None` (`install.rs:3124`). | FR-084 | FR-084 |
| PD-17 | **Batch ceiling** = Σ op ceilings + `ELEVATION_MARGIN` (120 s): `run_inno_installer` 600 s, `run_pkg_installer` 600 s, `run_inno_uninstaller` 2 × 120 s (the uninstaller's own wait is bounded by `UNINSTALL_POLL_TIMEOUT`, then the poll), `uninstall_pkg` 4 × 30 s (three reads and `--forget`). | D14 names the sum; the uninstaller's own wait had no bound in FR-031 (PD-24). | D14, FR-076 |
| PD-18 | **macOS `launch` measures its timeout from the `osascript` spawn**, so the password dialog counts against the ceiling; Windows measures from `ShellExecuteExW`'s return (with `SEE_MASK_NOASYNC` the call returns after the prompt). A dialog left open past the ceiling is `TimedOut` (R-7). | No API tells the two apart on macOS. | D14 |
| PD-19 | **Names from `std::collections::hash_map::RandomState`.** `job-<32 hex>` (core) and `run-<32 hex>` (helper, through `HelperIo::random_hex`, so tests are deterministic) come from two `RandomState::new()` hashers ("initialized with random keys", std docs) over a process counter; a collision is caught by `create_new` / `CreateDirectoryW` and retried up to 4 times. The names are not secrets: the job's hash travels on the elevated argv (D2); a predicted run-directory name only lets a local user pre-create it, which fails the op closed (`E_HELPER_RUN_DIR`, R-9). | No RNG crate in the workspace (`Cargo.toml:13-51`); none is added (spec "External facts": Phase 10 adds no crate). | FR-073, D7 (SI-9) |
| PD-20 | **`scripts/bundle.js`** (§4.6, an ES module like every script under `apps/krate-manager/scripts/`, because `package.json:4` declares `"type": "module"`): `cargo build -p krate-manager-helper --release --locked` (Windows and macOS only: no Linux helper binary is ever built or bundled, D18, so the Linux leg of FR-103 scans no binary, §8.2), copy to `src-tauri/binaries/krate-manager-helper-<host tuple>[.exe]`, then `npx tauri build --config src-tauri/tauri.helper.conf.json` (Windows, macOS) or `npx tauri build` (Linux); stdio inherited, exit code propagated. The CI step keeps its name `tauri build` and its `tee`, so `build warnings` also sees the helper's release build. | `copy_binaries` strips the triple and fails on a missing sidecar (`tauri-build-2.7.4/src/lib.rs:139-166`, `:735-741`); `TAURI_CONFIG` is merged into the config (`:681-684`). | FR-092, FR-104 |
| PD-21 | **The `changes` job** runs on `ubuntu-24.04`, `actions/checkout@v6` with `fetch-depth: 0`, one bash step: `workflow_dispatch` → true; `push` → `git diff --name-only "$BEFORE" "$SHA"`; `pull_request` → base sha to head sha; an all-zero `before` or a failing `git diff` (history rewritten) → true (fail open: the elevated jobs run). Event values reach the script through `env:`, never `${{ }}` inside `run:`. The path list is FR-102b's plus `apps/krate-manager/Cargo.toml` and `apps/krate-manager/Cargo.lock` (SI-27: they fix the helper's dependency versions and `windows-sys` features). Each branch and the fail-open has a shape-test mutation (§4.8.4). | SI-2; the workflow also triggers on `pull_request` (`manager-ci.yml:12-17`), which FR-102b does not cover (SI-18). | FR-102b |
| PD-22 | **The `node --test` step sits between `lint` and `vitest`**, so `required tests ran` still directly follows `vitest`. | `manager-ci-shape.test.js:878-880` requires that adjacency. | FR-109 (SI-15) |
| PD-23 | **`NativeElevation` is built only by `krate_manager_lib::resolve_elevation(exe: Option<&Path>) -> NativeElevation`** in `src-tauri/src/lib.rs`; `run()` passes `std::env::current_exe()`; the manual example passes a synthetic exe path inside the cargo profile directory (`<examples dir>/../manual_elevation`, §4.5), because `Path::parent` is lexical and the helper sits in `target/debug`, not `target/debug/examples`. The path rule itself is the pure `krate_manager_core::install::helper_beside(os: Os, exe: Option<&Path>, io: &dyn InstallIo) -> Option<PathBuf>` (in core because `src-tauri`'s library has `test = false`, `src-tauri/Cargo.toml:10-13`, so no src-tauri test would ever run) (`exe.parent()` must be non-empty; joined with `krate-manager-helper` + `.exe` iff `Os::Windows`; `None` when `io.kind_of` of that path is not `Some(File)`, when `exe` is `None` or has no non-empty parent (a bare name never resolves against the CWD), or for `Os::Linux`), which `resolve_elevation` calls with the native `Os` (the platform `os()`, `platform/windows.rs:32`, `macos.rs:29`, `linux.rs:26`) and `FsInstallIo`, and which `install::tests::helper_resolved_beside_exe` tests for all three `Os` values on every leg (§7.1 SC-028). | FR-112's "constructed only in `lib.rs`" and FR-105's example both hold; FR-090's resolution rule gets a test. | FR-090, FR-105, FR-112 (SI-14) |
| PD-24 | **The uninstaller's own wait** (the process the helper spawned) is bounded by `UNINSTALL_POLL_TIMEOUT`; a non-exit there is `E_HELPER_UNINSTALL_TIMEOUT`, not `E_HELPER_TIMEOUT` (the batch continues: an uninstaller does not block the next installer the way a running setup does), and no poll follows. Tested by `ops::tests::uninstaller_never_exits` (§7.1 SC-004). | Inno's uninstaller returns at once (spec External facts); an unbounded wait would hang the batch. | FR-031, FR-042 |
| PD-25 | **A malformed `--job-sha256` (not 64 lower-case hex) is a usage error, exit 2.** Tested in-process by `cli::tests::malformed_hash_is_usage_error` (`main_with` with a recording `RunFn`: `--job-sha256 xyz`, 63 hex, upper-case 64 hex → exit 2, `RunFn` never called, nothing written; §7.1 SC-023), not by spawning the binary with arguments, so FR-106's "no arguments" rule for `check` holds. | A malformed hash can never match; exit 3 would write a result whose `job_sha256` is not a hash. | FR-005 |
| PD-26 | **The macOS helper folder** `/Library/Application Support/com.krateaudio.krate-manager-helper/` is created by the helper when absent (`DirBuilder` mode `0o755`), then checked with the run directory by `run_dir_ancestors_ok` (D7); the run directory is `0o700`. | D7 names the folder but not who creates it. | D7 |
| PD-27 | **`required tests ran` reads TAP leniently:** a line `^ *ok \d+ - (.*?)(?: # .*)?$` counts for its name at any depth; each `node` name must count exactly once. | Node warns the reporter output "is subject to change between versions" (test runner docs, v24); the regex tolerates indentation and directives. | FR-109 |

### 0.2 Spec issues found while planning (for the main loop to fix in the spec before tasks)

| Id | Spec text | Problem (evidence) | Proposed edit |
|---|---|---|---|
| SI-1 | SC-019, FR-103a: `main::tests::ci_roots_flag_is_usage_error`, `main::tests::ci_roots_file_is_read`; FR-103: the marker `--ci-roots` "at least once in `crates/helper/src/main.rs`"; FR-050: `main_with` lives in the library | A library module cannot be `main.rs` (that file is the binary root); tests in the binary target print as `tests::…`; the literal cannot sit in `main.rs` when `main_with` parses it in the library. | Module `cli` (`crates/helper/src/cli.rs`): tests `cli::tests::ci_roots_flag_is_usage_error`, `cli::tests::ci_roots_file_is_read`; FR-103's marker file `crates/helper/src/cli.rs` |
| SI-2 | FR-102b: `changes` job on `ubuntu-latest` | `manager-ci-shape.test.js:797` fails any `ubuntu-latest` in the file (Phase 4 D16) | `ubuntu-24.04` |
| SI-3 | FR-071 (two methods), D11 / FR-078 "process-wide guard", SC-029 "a fresh `CommandEnv` (manager restart) is unblocked" | A static guard survives a fresh env and is shared by parallel test threads | PD-9: `Elevation::guard()`; "process-wide" = one `NativeElevation` per process |
| SI-4 | FR-071: `InstallEnv` gains `elevation` and `host_io` only | FR-076a needs `HostFacts` for the action's target OS | PD-10: `InstallEnv` also gains `host: &dyn HostFacts` |
| SI-5 | FR-112: `killall` only in `install.rs`; FR-090 / FR-112: `osascript` constants in `install.rs` | `"/usr/` is banned in core files outside `platform/` (`injection.rs:23-31`, `:1316-1320`) | PD-14: `KILLALL_ARGV` in `platform/mod.rs`; the `/usr/bin/osascript` path in `os_elevation.rs`; FR-112's `killall` rule names `platform/mod.rs` (literal) and `install_tests.rs` (the assertion) |
| SI-6 | FR-093 / FR-112 list only `READER_PINS` and new confinements | Existing workspace-wide rules the helper and `os_elevation.rs` would break: `HOST_FFI` (`RegOpenKeyExW`, `RegGetValueW`, `RegCloseKey` only in `os_facts.rs`, `injection.rs:208-213`); `HOST_LITERALS` (`/usr/sbin/pkgutil` only in `platform/macos.rs`, the Uninstall key only in `platform/windows.rs`, `:217-227`, scanned workspace-wide, `:2425-2446`); the test-source "no host literal" rule (`:2451-2480`, test sources = `crates/*/src/**/_tests.rs` and `crates/*/tests/*.rs`, `:1560-1567`); `CONFINED_TOKENS` `windows_sys` (`:302`); `CONFINED_TOML_KEYS` `windows-sys`, `sha2` (`:314`, `:326`); `sha2` tokens only in `verify.rs` (`:2974`) | FR-112 gains: HOST_FFI homes + `crates/helper/src/os.rs`; HOST_LITERALS homes + `crates/helper/src/ops.rs` (constants) and `crates/helper/src/ops_tests.rs` (the exact-argv assertions); `windows_sys` + `src-tauri/src/os_elevation.rs`, `crates/helper/src/os.rs`; TOML `windows-sys`, `sha2` + `crates/helper/Cargo.toml`; `sha2` tokens + `crates/helper/src/digest.rs` |
| SI-7 | D4 "`app_id` is the feed key minus `_is1`" | `roster.rs:201-207` bans `_is1` in `install.rs` (non-test) | PD-13 (no guard change) |
| SI-8 | FR-091, FR-092, FR-105 | `config_guard.rs` pins: exactly three `tauri.*.conf.json` platform files (`:25-29`, `:666-672`); src-tauri `[features]` exactly `dev-feed = []` (`:864-872`); `package.json` scripts exactly six (`:1295-1307`); `test-support` only in `[dev-dependencies]` except core's `[features]` (`:1247-1251`) | FR-123-style list of guard edits (§4.8.2): `tauri.helper.conf.json` allowed with exactly `{"bundle":{"externalBin":["binaries/krate-manager-helper"]}}`; `[features]` `["dev-feed = []", "manual-elevation = []"]`; scripts + `"bundle": "node scripts/bundle.js"`; helper `[features]` exactly `["ci-temp-roots = []", "test-support = []"]` |
| SI-9 | FR-073 "16 bytes of OS randomness"; D7 "32 lower-hex random" | No RNG crate; none may be added | PD-19 wording: "a `RandomState`-derived, collision-checked name; not a secret" |
| SI-10 | FR-079 `E_INSTALL_LOCKED` text "a plug-in file is in use; close your DAW and retry" vs FR-133 "A plug-in file is in use. Close your DAW and retry." | Two texts for one error | PD-15: FR-133's sentence is the `Display` |
| SI-11 | FR-003 "one entry per job op"; FR-076 "its `ops` length the job's, else `E_INSTALL_NO_RESULT`; `job_error` set → `E_INSTALL_HELPER`" | A refused job (exit 3) has no parsed ops, so the length check would turn every exit 3 into `NO_RESULT` (SC-015 expects `E_INSTALL_HELPER`) | With `job_error` set, `ops` is `[]`; the core checks `job_sha256`, then `job_error`, then the length |
| SI-12 | Code list | Missing: `E_HELPER_SPAWN` (an installer, uninstaller or `pkgutil` could not be started: `failed`), `E_HELPER_RESULT_WRITE` (the result file cannot be created for a reason other than existence: exit 4) | Add both to FR-003 / FR-030 to FR-033 |
| SI-13 | SC-015 `pre_job_refusals`: "a test-built `VerifiedArtifact` through `test-support`" | Minting is confined to `verify.rs` / `download.rs` (`injection.rs:2629-2700`); `install_tests.rs` may call `verify_file` (`:2700-2709`), and Phase 9's `Fx::verified` already writes artifacts **outside** staging (`install_tests.rs:739-770`) | "a `VerifiedArtifact` verified outside `staging_dir()` (`Fx::verified`)" |
| SI-14 | FR-105: expected `Exited(4)`; the job is "`uninstall_pkg` / `run_inno_uninstaller` for an id that has no receipt or key" | On macOS `<empty dir>` is the user's own, so `SameOwnerNonRoot` passes and the op **runs as root**: with a valid-looking id that happens to be installed (`com.krateaudio.iterum`) it would delete the receipt-listed plug-in files and `--forget` the receipt; "an id that has no receipt" is a hope, not a check. `NativeElevation` construction in the example conflicts with FR-112 | The example's single op is one the helper's own matcher refuses before any spawn, registry read or filesystem effect: macOS `uninstall_pkg { pkg_id: "com.krateaudio.NOT-VALID" }` (fails `is_pkg_id`: upper case and `-`) → `refused E_HELPER_PKG_ID`; Windows `run_inno_uninstaller { app_id: "not-a-guid" }` (fails `is_app_id`) → `refused E_HELPER_APP_ID` should the location ever pass. Expected: Windows `Exited(4)` (`E_HELPER_JOB_LOCATION`, FR-025); macOS `Exited(0)` with exactly one `refused E_HELPER_PKG_ID` entry, zero spawns, no run directory (an uninstall-only job creates none). The example refuses to start unless its argument is an existing, empty directory, and removes only the files and directories it created (§4.5); it builds its launcher through `resolve_elevation` (PD-23) |
| SI-15 | FR-109: `node --test` "before `required tests ran`" | Must not sit between `vitest` and `required tests ran` (`manager-ci-shape.test.js:878-880`) | "before `vitest`" (PD-22) |
| SI-16 | FR-033 "joined to `/<location>`", D3 root list | Needs an injectable base for every-leg tests | PD-5: `MacosRoots.volume` (production `/`, not settable by `--ci-roots`) |
| SI-17 | FR-050 `RunFn … -> ExitCode` | PD-4 | `-> Exit` |
| SI-18 | FR-102b covers push and dispatch | The workflow runs on `pull_request` too | PD-21 |
| SI-19 | New components: "`test_support.rs` … the loopback elevation over the helper library" | PD-8 | Loopback in `crates/core/src/install_loopback_tests.rs` (`#[cfg(test)]`); `FakeElevation` (no helper) in `test_support.rs` |
| SI-20 | SC-010, SC-014 "verified artifacts" | FR-072 refuses an artifact outside canonical staging; `Fx::verified` writes under `<base>/artN` | Phase 10 tests verify artifacts at `<staging>/downloads/<sha hex>/<asset>` (the Phase 7 layout, `download.rs:29`, `:115-131`) |
| SI-21 | SC-014 / SC-029 through the loopback | `test_support` roots name the state dir `<base>/state` (`test_support.rs:126-131`); FR-021 requires the last component `APP_IDENTIFIER` | New `test_support::fresh_elevated_roots()` (state dir `<base>/com.krateaudio.krate-manager`, spelled with `platform::APP_IDENTIFIER`) |
| SI-22 | (lanes) | The app lane's WSL step runs `-p krate-manager-core` only (`krate-manager-phase.js:596-599`); the helper's `cfg(unix)` code and `_unix` rows reach no local gate | Main loop: extend that step to `-p krate-manager-core -p krate-manager-helper` (a workflow edit, not a Phase 10 task) |
| SI-23 | FR-053 `HelperIo` method list | `random_hex` (PD-19), `create_helper_folder` (PD-26) and `read_tail` (SI-28) are missing | Add all three |
| SI-24 | SC-015 `elevating_event_order`: "over `[Install A, Install B]` the recorded progress is every `Download` event of A and B, then exactly one `Elevating`, then `launch`" | A driver receives `Action::Install { entry, artifact: VerifiedArtifact }` already verified (`install.rs:254-266`); downloads happen only in `obtain_and_run`, one action per command, before `driver.run` (`install.rs:2969-3037`, `obtain_and` at `:2989`, `driver.run` at `:3007`); the batch download queue is Phase 12. No Phase 10 path emits `Download` events for two actions, so the sentence is untestable and a driver-only test passes vacuously | Two parts: (1) through `for_command` for one `Install`: every `Download` event, then exactly one `Elevating`, then the `launch`; (2) `WindowsDriver::run` / `MacosDriver::run` direct with two pre-verified installs: exactly one `Elevating`, before the `launch`; plus the existing uninstall-only row (§7.1) |
| SI-25 | FR-112: "`OsRunner`, `OsRegistry`, `OsHelperIo` constructed … only in `crates/helper/src/main.rs`"; "`Instant::now` and `SystemTime` only in `crates/helper/src/os.rs` within the helper" | Read as covering test code, no test can build the real seams, so PD-6's open, `read_bounded`'s bound, `create_run_dir`'s mode and `OsRunner::wait`'s no-kill timeout reach no local or CI gate; and SC-004's wall-time bound needs `Instant` in `ops_tests.rs` | Both rules apply to non-test sources only (`is_test_source`, `injection.rs:1559-1567`): the real seams may also be built in `crates/helper/src/os_tests.rs`, `Instant` / `SystemTime` may occur in `crates/helper/src/*_tests.rs`; the plants go into `ops.rs`. New rows `os::tests::*` (§7.1 SC-005, SC-027) |
| SI-26 | FR-061 fixture set `exit_code.txt`, `log.txt`, `seconds.txt`; FR-102a "the DLL and unlocked-file classes equal the ones recorded in M-1", "the recorded file-in-use line's fixed part" | The spec's markdown is not a machine input; the CI job has nowhere to read the expected classes or line from | `inno-locked/` also holds `classes.json` (`{"dll": "new" \| "old" \| "scheduled", "unlocked": …}`) and `locked_line.txt` (the fixed part, one line, no newline); `inno-locked-file` compares against those files; `classify::LOCKED_LINE` equals `locked_line.txt`, asserted by `classify::tests::recorded_locked_case` |
| SI-27 | FR-102b changed-paths list | It omits `apps/krate-manager/Cargo.toml` and `apps/krate-manager/Cargo.lock`, which fix the helper's `serde`, `serde_json`, `sha2` and `windows-sys` versions and features (FR-093 changes them in this phase): a dependency bump rebuilds the elevated binary and skips `helper-elevated` | The list gains both files |
| SI-28 | FR-030 step 5 "read the log tail and map `(exit_code, log)` through `seams.classify`" | Read from the start, a log over 1 MiB yields the tail of its first MiB; classified over only the 40-line `log_tail`, a file-in-use line followed by many rollback / preset lines (a real plug-in install has dozens of preset files) is missed | The helper reads the **last** 1 MiB of the log (`HelperIo::read_tail`) and passes **every** line of it to `classify`; `log_tail` (FR-003's last 40 lines, empty lines counted) is for the result only |
| SI-29 | FR-112 (no FR-024 guard); FR-107 (no FR-106 bans) | FR-024 (no xattr, no owner change, no ACL change but the run-dir DACL) and FR-106 (no Inno compile, `pkgbuild`, `installer` or helper run outside the two elevated jobs) have no test: the existing `ELEVATION` list is `['Start-Process', 'RunAs', 'runas ', 'msiexec']` (`manager-ci-shape.test.js:329`) | FR-112 gains: `xattr`, `setxattr`, `removexattr`, `chown`, `fchown`, `lchown`, `SetNamedSecurityInfo`, `SetFileSecurity`, `SetSecurityInfo`, `com.apple.quarantine` never in `crates/helper/src/` (non-test), with a plant; FR-107 gains: `pkgbuild`, `/usr/sbin/installer`, `installer -pkg`, `Minionguyjpro/Inno-Setup-Action`, `ISCC`, and a run of the helper binary with arguments, only in `helper-elevated` / `inno-locked-file`, with plants in `check` and `changes` |
| SI-30 | SC-025's eleven titles; FR-131 "on Windows and macOS" | Only `windows remove confirmation names the uninstaller` exists; a macOS card that kept Phase 9's manifest text passes | SC-025 gains a twelfth title `macos remove confirmation names the uninstaller` |
| SI-31 | SC rows name no test for: FR-133's / FR-079's `Display` texts in the core; FR-090's resolution rule; PD-24's uninstaller wait; SI-12's two codes; the job's `sha256` binding to the verified digest; the macOS registrar kill after a not-applied `done` | Each is a behaviour the plan adds or the spec requires with no required test, so a regression passes every gate | New required names (§4.9, §7.1): `install::tests::new_error_texts`, `install::tests::job_hash_is_the_verified_digest`, `install::tests::helper_resolved_beside_exe`, `ops::tests::uninstaller_never_exits`, `ops::tests::spawn_error_is_named`, `ops::tests::classify_sees_whole_bounded_log`, `ops::tests::temp_io_refuses_outside_base`, `cli::tests::malformed_hash_is_usage_error`, the `os::tests::*` rows of SI-25, node `write test job prints verbatim windows paths`; extended rows in `rejects_crafted_jobs` (result-file `PermissionDenied`), `not_applied_detected` (macOS `killall`), `log_tail_bounds` (interleaved empty lines), `one_launch_per_batch` / `native_wiring_*` (job bytes) |

---

## 1. Inputs verified this session

### 1.1 Repo facts (file:line, read 2026-11-01 at `4a8b3e2a`)

| Fact | Where |
|---|---|
| Helper stub: no `[lib]` section, `[dev-dependencies]` core with `test-support`; `lib.rs` is one doc line; `main.rs` prints one line, exit 2; `tests/stub.rs` asserts it | `crates/helper/Cargo.toml:1-15`, `src/lib.rs:1`, `src/main.rs:1-4`, `tests/stub.rs:1-23` |
| Workspace: members core, helper, src-tauri; `exclude = ["tests/fixtures/clippy-guard"]`; `windows-sys` 0.61.2 with five features; `sha2` 0.11.1; `tempfile` 3.27.0; `serde` 1.0.229; `serde_json` 1.0.154; release profile `panic = "abort"`, `strip = true` | `Cargo.toml:1-4`, `:13-51`, `:55-60` |
| `clippy.toml` bans `std::env::{var, var_os, vars, vars_os, home_dir, temp_dir}`, `Roots::new`, `FromOs::from_os` | `clippy.toml:1-10` |
| `InstallError` (`Format` text "not a tar.gz" at `:143`), `code()`, `io_err` | `install.rs:100-207` |
| `Action`, `Outcome`, `ActionResult`, `Notice::RescanDaw`, `ActionSummary`, `InstallProgress` | `install.rs:253-332` |
| `InstallDriver: Sync`, `DriverResolver`, `NativeDrivers`, `LinuxDriver`, `driver_for` Linux only | `install.rs:334-413` |
| `InstallEnv { ctx, io, extract, progress }`; `CommandEnv` (`Copy`, `#[cfg(test)] request_seam`); `native_env(ctx, io, extract, download)` | `install.rs:415-457` |
| `InstallIo` read + mutating methods; `FsInstallIo::canonicalize` = `std::fs::canonicalize` | `install.rs:471-494`, `:578-580` |
| `recover` (journal pass, `.json.tmp` leftovers, extract prune); `RecoveryReport { journals, temp_removed, extract_pruned }` (no literal construction anywhere) | `install.rs:2407-2462` |
| `ActionGate::{Blocked, Open { manifest }}`; `gate` returns `Ok(None)` off Linux after the journal check | `install.rs:2692-2740` |
| `allowed_actions`: `Remove` iff a manifest | `install.rs:2742-2766` |
| `BUSY` static per `(state_dir, stem)` | `install.rs:2820-2845` |
| `install_command`: `Destinations::of` `:2925`, gate `:2932`, listing `:2933-2937`, detection `:2938`; `obtain_and_run` hard-codes `RescanDaw` `:3035`; `command_remove`: gate match `:3096-3104` | `install.rs:2850-3128` |
| `Destinations::of` is pure and runs on every OS (the Linux plug-in root default `vst3_user()`) | `install.rs:769-860`; `settings.rs:28-32` |
| `InnoValues`, `PkgOutputs`, `InstallerRecord::{Inno { install_location: Option<PathBuf> }, Pkg, Manifest}`, `Detection::Checked { copies, record, … }`, `HostFacts`, `PlatformFacts` (native dispatch) | `detect.rs:131-204` |
| Windows candidates: the copy at `InstallLocation\<bundle>` is managed; macOS: `Vst3Global` and `AuGlobal` copies managed | `detect.rs:369-399`, `:400-433` |
| `same_path`, `usable_install_location` | `detect.rs:479-506` |
| `detect_plugin(os, ctx, entry, host, listing)`; `check_sandboxed` on the record location | `detect.rs:981-1072` (`:1047-1050`) |
| `APP_IDENTIFIER`, `VENDOR_DIR`; `Os`; `RunOutcome`; `HostIo` (not `Sync`) | `platform/mod.rs:12-15`, `:63-84` |
| Windows state dir `%LOCALAPPDATA%\com.krateaudio.krate-manager`, `Vst3Global` = `%CommonProgramW6432%\VST3`, `UNINSTALL_KEY` | `platform/windows.rs:9-14`, `:36-60` |
| macOS `PKGUTIL`, `PKGUTIL_WAIT` 5 s; state dir under `~/Library/Application Support` | `platform/macos.rs:11-20`, `:35` |
| Roots: `vst3_user()` accessor, no `vst3_global()`; `check_sandboxed` panics outside a test sandbox | `roots.rs:173-218` |
| `test_support`: `fresh_temp_roots`, roots under `<base>/{plugins,presets,state}`; `FakeHostFacts` / `FakeHostIo` (`RefCell`, maps); `InstallRig::command_env(os, arch, io, extract_io, drivers)`; `OneDriver` | `test_support.rs:126-131`, `:198-207`, `:270-345`, `:3007-3075` |
| `test_feed_document`: a `darwin-*` key yields `pkg` + `macos_pkg_id` `com.krateaudio.<lower>`, a `windows-*` key `inno-exe` + `windows_uninstall_key` `{0000000N-0000-4000-8000-00000000000N}_is1` | `test_support.rs:375-432` |
| `Fx`: `verified(entry, key, asset, bytes, sig)` writes under `<base>/artN` and `verify_file`s; Phase 9 already verifies a `windows-x86_64` inno-exe artifact this way | `install_tests.rs:724-770`, `:2450-2460` |
| Phase 9 concurrency tests build each `CommandEnv` inside its own thread | `install_tests.rs:3470-3570` |
| `native_wiring_offers_nothing_{windows,macos}`; `native_wiring_installs_linux` | `install_tests.rs:3850-3925` |
| `status::build_with_os`: `driver_present` from `NativeDrivers` `:213`, gates only with a driver `:231-233`; `plugin_view` | `status.rs:197-257`, `:364-439` |
| `verify::Sha256Stream` (`pub(crate)`), `hex_lower` (`pub(crate)`) | `verify.rs:290-315` |
| `download::obtain_and(env, feed, request, progress, next)`; verified artifacts under `<staging>/downloads/<sha>/` | `download.rs:1331-1337`, `:29`, `:115-131` |
| `contract::check_install_value(field, value)` is `pub` (the uninstall-key and pkg-id patterns) | `contract.rs:267`, `:1049-1061` |
| src-tauri: eight commands; `install_plugin` / `remove_plugin` over `native_env`; `status_for` over `PlatformFacts { io: &OsHostIo }`; `OsHostIo` registry read pattern | `src-tauri/src/lib.rs:27-99`, `commands.rs:104-199`, `os_facts.rs:46-150` |
| `tauri.conf.json` (`signingIdentity "-"`, NSIS `currentUser`); platform files one line each; capability eight `allow-*` | `src-tauri/tauri.conf.json:19-24`; `tauri.{windows,macos,linux}.conf.json`; `capabilities/main.json:1-12` |
| `.gitignore` has no `src-tauri/binaries/` | `apps/krate-manager/.gitignore:1-5` |
| `package.json` six scripts | `package.json:5-12` |
| ESLint ignores `src-tauri/`, `crates/`, `tests/fixtures/`, not `scripts/` (scripts are linted by `typescript-eslint` `strictTypeChecked` with `projectService`) | `eslint.config.mjs:7-10` |
| `view.ts` `object()` refuses a missing **and** an extra key (so `remove_hint` needs the 13 fixtures) | `src/view.ts:125-131` |
| `status.ts`: `INSTALL_TEXTS`, `confirmLines(p, action)`, `progressText`, `outcomeLines`, `actionRow`, `rejectionText` | `src/status.ts:66-86`, `:150-154`, `:267-392` |
| `config_guard.rs` pins (§4.8.2) | `crates/core/tests/config_guard.rs:25-29`, `:88-96`, `:505-540`, `:640-658`, `:666-684`, `:864-872`, `:1247-1259`, `:1295-1307` |
| `injection.rs` rules (§4.8.1) | `crates/core/tests/injection.rs:23-31`, `:51-90`, `:208-227`, `:280-327`, `:411-419`, `:562-591`, `:1286-1320`, `:1560-1567`, `:2416-2450`, `:2629-2709`, `:2966-2980` |
| Roster and install-identifier scans (`crates/helper/src` scanned for plugin names; `_tests.rs` excluded; `install.rs` scanned for `com\.krateaudio\.`, the GUID head, `_is1`) | `crates/core/tests/roster.rs:80-121`, `:200-252` |
| `manager-ci.yml`: one `check` job, steps in order, `required tests ran` (cargo + vitest), `tauri build` with `tee` | `.github/workflows/manager-ci.yml:27-308` |
| Shape test: `PATHS`, `TRIGGERS`, `STEPS`, `ASSERTION_STEPS`, `COMMANDS`, `GATED`, `ELEVATION`, `ubuntu-latest` ban, `jobs == ['check']`, per-step `sudo` / `Start-Process` bans, `required tests ran` after `vitest`, `EXPECTED_REQUIRED` | `tools/feed-contract/tests/manager-ci-shape.test.js:148-336`, `:337`, `:797`, `:806-808`, `:850-857`, `:878-880`, `:909-921` |
| `release.yml` compiles Inno with `Minionguyjpro/Inno-Setup-Action@v1.2.7`; pkg staging and `pkgbuild --install-location /` | `.github/workflows/release.yml:244-247`, `:294-341` |
| Iterum `setup.iss`: `AppId`, `DefaultDirName`, `PrivilegesRequired=admin`, `UsePreviousAppDir=yes`, architectures, `[Files]` flags, `[UninstallDelete]`; `[Code]` shows no `MsgBox` (nothing a silent run could block on) | `plugins/iterum/installers/windows/setup.iss:27`, `:37`, `:50`, `:53`, `:63-64`, `:72-77`, `:102-114` |
| `lint-manager-version.js` reads only workspace members' manifests and every `tauri.*.conf.json` for a `version` key | `tools/lint-manager-version.js:247-330` |

### 1.2 External facts (verified 2026-11-01)

| Fact | Source |
|---|---|
| `tauri-build` 2.7.4 merges `TAURI_CONFIG` into the config and copies every `bundle.externalBin` sidecar (triple stripped) beside the build output, failing when one is missing | Local crate source `tauri-build-2.7.4/src/lib.rs:139-166` (`copy_binaries`), `:681-684`, `:735-741` |
| windows-sys 0.61.2: `ShellExecuteExW` behind `Win32_System_Registry` (and `Win32_UI_Shell`), `SHELLEXECUTEINFOW.nShow: i32`, `hkeyClass` from `System::Registry`; `SHGetKnownFolderPath(rfid, dwflags: u32, htoken, *mut PWSTR) -> HRESULT`; `FOLDERID_ProgramData`, `_ProgramFilesCommon`, `_UserProfiles`, `_Windows`; `SEE_MASK_NOCLOSEPROCESS = 64`, `SEE_MASK_NOASYNC = 256`, `SEE_MASK_FLAG_NO_UI = 1024`; `CreateDirectoryW` behind `Win32_Security`; `ConvertStringSecurityDescriptorToSecurityDescriptorW`, `SDDL_REVISION_1`; `COINIT_APARTMENTTHREADED = 2`, `COINIT_DISABLE_OLE1DDE = 4`; `CoTaskMemFree`, `CoUninitialize`; `WaitForSingleObject`, `GetExitCodeProcess`; `WAIT_OBJECT_0`, `WAIT_TIMEOUT`, `ERROR_CANCELLED = 1223`, `LocalFree`, `CloseHandle`, `GetLastError`; `FILE_FLAG_OPEN_REPARSE_POINT = 0x0020_0000` | Local crate source `windows-sys-0.61.2/src/Windows/Win32/UI/Shell/mod.rs:573`, `:755-756`, `:3072`, `:3074`, `:3134`, `:3141`, `:5136-5143`, `:5717-5733`; `Storage/FileSystem/mod.rs:51-52`, `:1501`; `Security/Authorization/mod.rs:56`, `:1086`; `System/Com/mod.rs:52`, `:84`, `:88`, `:375-376`; `System/Threading/mod.rs:142`, `:380`; `Foundation/mod.rs:1`, `:6`, `:8`, `:1172`, `:10027-10028`; features `Cargo.toml:137,141,163,184,250,272`; https://docs.rs/windows-sys/0.61.2/ |
| serde_derive 1.0.229: a struct visitor returns `duplicate_field` on a repeated key | Local crate source `serde_derive-1.0.229/src/de/struct_.rs:269` |
| A mutual dev-dependency (A dev→ B[test-support], B dev→ A[test-support]) resolves, builds and tests | Scratch workspace, `cargo test --workspace --offline`, cargo 1.99.1, this session (two suites green; removed afterwards) |
| `RandomState::new` "is initialized with random keys"; two instances' hashers "are unlikely to produce the same result for the same values" | https://doc.rust-lang.org/std/collections/hash_map/struct.RandomState.html (std 1.99.1, fetched) |
| `node --test` accepts quoted glob patterns as its final arguments; reporter `tap`; one reporter defaults to stdout; reporter output "is subject to change between versions" | https://nodejs.org/docs/latest-v24.x/api/test.html (fetched; Node 24 per `manager-ci.yml:42-44`) |
| `std::fs::canonicalize` yields `\\?\` on Windows; `CommandExt::raw_arg`; `OpenOptions::create_new`; `Child::try_wait`; `std::os::unix::fs::{MetadataExt, DirBuilderExt, PermissionsExt}`; `std::os::windows::fs::OpenOptionsExt::custom_flags` | https://doc.rust-lang.org/std/ (std 1.99.1, `rustc --version` 1.99.1 this session); the first three as cited by the spec (`spec.md:184-186`) |
| Inno command line, exit codes, uninstaller behaviour; ShellExecuteEx, KNOWNFOLDERID, MS argument parsing; hosted runners' passwordless sudo / UAC disabled; osascript, TN2065, error -128; `installer`, `pkgutil`; Tauri sidecars; `tauri build --config`; `Start-Process -Wait -PassThru`; `required-features`; `CARGO_BIN_EXE_<name>` | As verified by the spec this session, `spec.md:160-191` (not re-fetched; every behaviour that matters is asserted by a CI job, §7.3) |

---

## 2. File layout

| File | Change |
|---|---|
| `apps/krate-manager/crates/helper/Cargo.toml` | 🔶 description; `[lib] path = "src/lib.rs"`; `[[bin]] name = "krate-manager-helper"`; `[features] ci-temp-roots = []`, `test-support = []`; `[dependencies] serde`, `serde_json`, `sha2`; `[target.'cfg(windows)'.dependencies] windows-sys`; `[dev-dependencies]` core (`test-support`), `tempfile` |
| `crates/helper/src/{lib,cli,error,job,result,roots,validate,digest,ops,classify,seams,os}.rs`, `main.rs` | 🆕 / 🔶 §4.1 |
| `crates/helper/src/{cli,job,result,roots,validate,ops,classify,os}_tests.rs` | 🆕 the helper SC tests (§7.1); `os_tests.rs` builds the real seams over a `TempDir`, unelevated (SI-25) |
| `crates/helper/tests/usage.rs`, `tests/core_jobs.rs` | 🆕 SC-023, SC-011; `tests/stub.rs` deleted |
| `crates/core/Cargo.toml` | 🔶 `[[bin]] name = "argv-echo"`, `path = "tests/bin/argv_echo.rs"`, `test = false`; `[dev-dependencies] krate-manager-helper = { workspace = true, features = ["test-support"] }` |
| root `Cargo.toml` | 🔶 `[workspace.dependencies] krate-manager-helper = { path = "crates/helper" }`; `windows-sys` features + six (FR-093) and its comment; `exclude` + `tests/fixtures/argv-recorder`, `tests/fixtures/locked-dll` |
| `crates/core/src/install.rs` | 🔶 §4.2 (drivers, `Elevation`, guard, `pub mod job { … }` inline, gate, commands, recover, codes, notice, progress) |
| `crates/core/src/install_tests.rs`, `install_loopback_tests.rs` (🆕, declared in `install.rs` as `#[cfg(test)] #[path = "install_loopback_tests.rs"] mod loopback;`) | 🔶 / 🆕 §7.1 |
| `crates/core/src/platform/mod.rs` | 🔶 `KILLALL_ARGV` (PD-14) |
| `crates/core/src/roots.rs` | 🔶 `pub fn vst3_global(&self) -> &Path` (mirrors `vst3_user`, `roots.rs:207-217`) |
| `crates/core/src/status.rs`, `status_tests.rs` | 🔶 gate over the detection, `remove_hint` (FR-083) |
| `crates/core/src/test_support.rs` | 🔶 `FakeElevation`, `LiveHostFacts`, `fresh_elevated_roots`, `InstallRig` additions (§4.4) |
| `crates/core/tests/windows_quoting.rs`, `crates/core/tests/bin/argv_echo.rs` | 🆕 SC-015's real-parser round trip |
| `crates/core/tests/injection.rs`, `config_guard.rs`, `roster.rs` | 🔶 §4.8 |
| `src-tauri/src/os_elevation.rs` | 🆕 §4.5 (no tests in src-tauri: its lib is `test = false`; the resolution rule is core's `install::helper_beside`, PD-23) |
| `src-tauri/src/lib.rs`, `commands.rs`, `src-tauri/Cargo.toml` | 🔶 `resolve_elevation`, `.manage(Arc<NativeElevation>)`, two commands; `[features] manual-elevation = []`; `[[example]] name = "manual_elevation"`, `required-features = ["manual-elevation"]` |
| `src-tauri/examples/manual_elevation.rs` | 🆕 FR-105 |
| `src-tauri/tauri.helper.conf.json` | 🆕 FR-092 |
| `apps/krate-manager/scripts/bundle.js`, `bundle.test.js`, `write-test-job.js`, `write-test-job.test.js`, `inno-fixture.test.js` | 🆕 §4.6 (ES modules) |
| `apps/krate-manager/package.json`, `.gitignore` | 🔶 `"bundle"` script; `/src-tauri/binaries/` |
| `apps/krate-manager/tsconfig.node.json` | 🔶 `allowJs: true`, `checkJs: true`; `include` gains `scripts/*.js` (§4.6) |
| `src/status.ts`, `src/view.ts`, `src/status.test.ts`; `tests/fixtures/status/*.json` (13) | 🔶 §4.7 |
| `tests/fixtures/inno/fixture.iss`, `tests/fixtures/locked-dll/{Cargo.toml,Cargo.lock,src/lib.rs}`, `tests/fixtures/argv-recorder/{Cargo.toml,Cargo.lock,src/main.rs}`, `tests/fixtures/helper/` (`.gitkeep` until M-1; at the resume `inno-locked/{exit_code.txt,log.txt,seconds.txt,classes.json,locked_line.txt}`, SI-26) | 🆕 CI fixtures |
| `apps/krate-manager/tests/required-tests.json`, `tools/feed-contract/tests/manager-ci-shape.test.js` | 🔶 §4.9, §4.8.4 |
| `.github/workflows/manager-ci.yml` | 🔶 §8.2 |
| `tests/fixtures/clippy-guard/Cargo.lock` | 🔶 re-locked (the task that changes a manifest re-runs `cargo metadata --manifest-path tests/fixtures/clippy-guard/Cargo.toml --format-version 1`) |
| `specs/Krate-manager-roadmap.md`, `specs/krate-manager-phase10/spec.md` (M-1) | 🔶 FR-123 (a) to (h) and M-1, by the build stage (§4.10) |

No change to `plugins/`, `dsp/`, `cmake/`, `extern/`, `release.yml`, `feed.yml`, `ci.yml`, `feed-tools.yml` (its
top-level plan stays `1..59` and its `NAMES` list unchanged because every new shape check is a `t.test` subtest of the
existing top-level `manager-ci.yml keeps the Phase 4 shape`, §4.8.4), any plugin installer script, `extract.rs`, `download.rs`, `verify.rs`, `feed.rs`, `detect.rs`, `context.rs` (FR-121, FR-122).

---

## 3. Error model

### 3.1 Helper (`crates/helper/src/error.rs`)

```rust
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct HelperError { pub code: &'static str }
pub const CODES: [&str; 25] = [ /* every code below, for PD-11's cross-check */ ];
```

| Code | Where | Result |
|---|---|---|
| (none) | wrong / missing / repeated / extra argument, `--ci-roots` without the feature, malformed `--job-sha256` (PD-25) | exit 2, one usage line on stderr, nothing written |
| `E_HELPER_ROOTS` | a known-folder lookup failed; a root with no existing ancestor or a non-plain missing component (FR-026); a `--ci-roots` root outside the file's directory (FR-052) | exit 4, nothing written |
| `E_HELPER_JOB_LOCATION` | FR-021, FR-025 | exit 4, nothing written |
| `E_HELPER_RESULT_EXISTS` / `E_HELPER_RESULT_WRITE` (SI-12) | the result name exists (a dangling link included) / cannot be created otherwise | exit 4, nothing written |
| `E_HELPER_JOB_READ`, `E_HELPER_JOB_HASH`, `E_HELPER_JOB_PARSE` | FR-023, FR-001, FR-036 | exit 3, result with `job_error`, `ops: []` (SI-11) |
| `E_HELPER_SOURCE`, `E_HELPER_APP_ID`, `E_HELPER_UNINSTALLER`, `E_HELPER_PKG_ID`, `E_HELPER_PKG_PATH` | per-op validation | op `refused` |
| `E_HELPER_RUN_DIR`, `E_HELPER_COPY`, `E_HELPER_HASH`, `E_HELPER_SPAWN` (SI-12), `E_HELPER_EXIT`, `E_HELPER_TIMEOUT`, `E_HELPER_NO_KEY`, `E_HELPER_UNINSTALL_TIMEOUT`, `E_HELPER_PKG_RECEIPT`, `E_HELPER_REMOVE`, `E_HELPER_FORGET` | per-op execution | op `failed` |
| `E_HELPER_LOCKED` | `classify` → `Locked` | op `locked` |
| `E_HELPER_AFTER_TIMEOUT` | every op after a timed-out installer (FR-042) | op `not_run` |

### 3.2 Core (`install.rs`, FR-079, PD-15)

New `InstallError` variants, each `Display` beginning with its code and holding no path:
`Refused` ("Administrator permission was not given; nothing was changed."), `Elevation { code: &'static str }`,
`ElevationBusy { stale: bool }` (FR-133's two sentences), `NotApplied`, `HelperMissing` ("the elevated helper is not
installed beside the manager"), `NoResult`, `Helper { code: &'static str }` ("the helper reported {code}"), `Locked`,
`InstallerFailed { exit_code: i32 }`, `RemoveElsewhere` (FR-079's text). `Step` gains `JobWritten` (`job_written`),
`JobRemoved` (`job_removed`). `install::tests::new_error_texts` (cargo.all) asserts `to_string()` of every new
variant equals its code, `": "`, and FR-133's sentence (`Refused`, `Locked`, `ElevationBusy { stale: false }`,
`ElevationBusy { stale: true }`, `NotApplied`) or FR-079's, completed by this section where FR-079 gives none
(`RemoveElsewhere`, `HelperMissing`, `NoResult`, `Helper`, `InstallerFailed`, `Elevation`), each sentence written as a literal in the test, so the vitest rows that echo a
rejection string (SC-025) are backed by the core's own text. Existing kinds are reused: `E_INSTALL_FORMAT`, `E_INSTALL_PATH`, `E_INSTALL_NOT_MANAGED`,
and the PD-10 I/O mapping (`install.rs:199-207`) for job writes.

---

## 4. Module design

### 4.1 The helper crate

#### 4.1.1 Public surface (`lib.rs`)

```rust
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HelperOs { Windows, Macos, Linux }
impl HelperOs { pub fn current() -> HelperOs /* cfg!(windows) / cfg!(target_os = "macos") / else; the only cfg-derived OS value (D18) */ }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Exit { pub status: u8, pub code: Option<&'static str> }      // PD-4
impl From<Exit> for std::process::ExitCode { /* ExitCode::from(status) */ }

pub fn run(args: &cli::Args, os: HelperOs, roots: &roots::AllowedRoots, seams: &seams::Seams<'_>) -> Exit;
```

#### 4.1.2 `run` (PD-3)

1. `roots::canonical(seams.io, roots)` (FR-026): each root canonicalized once; a missing root as its deepest existing
   ancestor plus the plain missing components → `CanonicalRoots`; failure exit 4 `E_HELPER_ROOTS`. `OsRoots::None` (Linux)
   has nothing to canonicalize.
2. `validate::job_location(seams.io, &args.job, &canonical, os)` (FR-021, FR-025, FR-027): `canonicalize(job) == job`
   (component-wise; ASCII case-insensitive with `os == Windows`, the `Prefix` included); name `job-<32 lower hex>.json`;
   parent named `jobs`; its parent `<S>` named `APP_IDENTIFIER` (a helper constant
   `pub const STATE_DIR_NAME: &str = "com.krateaudio.krate-manager"`, equal to core's, asserted by SC-011); `<S>` passes the
   `StateDirRule` (`UnderProfiles(p)`: `<S> == p/<one plain>/AppData/Local/<STATE_DIR_NAME>`; `SameOwnerNonRoot`: `lstat` of
   `<S>`'s parent, `<S>`, `<S>/jobs`, `<S>/staging` and the job report one uid, not 0; `Exact(d)`: `<S> == d`). Result:
   `JobPlace { stem, result_path: <S>/jobs/<stem>.result.json, staging_root: <S>/staging }`. Failure exit 4
   `E_HELPER_JOB_LOCATION`.
3. `seams.io.create_new(&place.result_path)` → `Box<dyn WriteSync>`; `AlreadyExists` → exit 4 `E_HELPER_RESULT_EXISTS`;
   other → exit 4 `E_HELPER_RESULT_WRITE`. From here on every exit writes this file.
4. `seams.io.read_bounded(job, MAX_JOB_BYTES)`; more than 64 KiB (the read takes at most `MAX + 1` bytes) or an I/O error
   → exit 3 `E_HELPER_JOB_READ`.
5. `digest::sha256(bytes) != args.job_sha256` → exit 3 `E_HELPER_JOB_HASH`.
6. `job::parse(bytes, os)` → `Vec<Op>`; any defect exit 3 `E_HELPER_JOB_PARSE`.
7. `ops::run_batch(&ops, &OpCtx { os, roots: &canonical, place: &place }, seams)` → `Vec<OpEntry>`; then
   `result::write(out, &ResultFile { result_version: 1, job_sha256: hex(arg), job_error, ops })` (`sync_all`); exit 0.

`job_sha256` in every result is the argv value, so the core's check (FR-076) compares against what it launched.

#### 4.1.3 `cli.rs` (FR-005, FR-050, FR-052, SI-1)

```rust
pub struct Args { pub job: PathBuf, pub job_sha256: [u8; 32], #[cfg(feature = "ci-temp-roots")] pub ci_roots: Option<PathBuf> }
pub const USAGE: &str = "usage: krate-manager-helper --job <path> --job-sha256 <64 lower-case hex>";
pub fn parse_args(args: &[OsString]) -> Option<Args>;            // argv without argv[0]
pub type RunFn = fn(&Args, HelperOs, &AllowedRoots, &Seams<'_>) -> Exit;
pub fn main_with(args: Vec<OsString>, seams: &Seams<'_>, run: RunFn) -> Exit;
```

`main_with`: `parse_args` (`None` → `eprintln!("{USAGE}")`, exit 2); roots = `AllowedRoots::production()` or, with the
feature and `--ci-roots`, `roots::from_ci_file(seams.io, path)`; then `run(&args, HelperOs::current(), &roots, seams)`.
The `"--ci-roots"` literal and `from_ci_file` are `#[cfg(feature = "ci-temp-roots")]`, so the default binary holds the
marker zero times (FR-103). `--ci-roots` file (FR-052): JSON, `deny_unknown_fields`; Windows `{ "state_dir",
"vst3_vendor", "preset_vendor" }`, macOS `{ "state_dir", "vst3", "components", "presets", "applications" }`; each
absolute; each canonical form (FR-026) under the canonical directory holding the file, else exit 4 `E_HELPER_ROOTS`;
`run_base` and `volume` stay production (PD-5); `state_dir` becomes `Exact`.

`main.rs` (FR-050):

```rust
#![cfg_attr(windows, windows_subsystem = "windows")]
fn main() -> std::process::ExitCode {
    let io = krate_manager_helper::os::OsHelperIo::new();
    let runner = krate_manager_helper::os::OsRunner::new();
    let seams = krate_manager_helper::seams::Seams { io: &io, runner: &runner, registry: &krate_manager_helper::os::OsRegistry,
        classify: krate_manager_helper::classify::classify_inno };
    krate_manager_helper::cli::main_with(std::env::args_os().skip(1).collect(), &seams, krate_manager_helper::run).into()
}
```

`main.rs` declares no module, so no library test runs twice.

#### 4.1.4 `job.rs` (FR-001, FR-002, FR-036; PD-2)

```rust
pub const MAX_JOB_BYTES: u64 = 64 * 1024;
pub const MAX_OPS: usize = 32;
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Op {
    RunInnoInstaller { file: String, sha256: [u8; 32] },
    RunInnoUninstaller { app_id: String },
    RunPkgInstaller { file: String, sha256: [u8; 32] },
    UninstallPkg { pkg_id: String },
}
impl Op { pub fn kind(&self) -> &'static str; pub fn target(&self) -> HelperOs; }
/// FR-001's key set per kind, for `job::tests::no_field_adds_a_root`.
pub const KEYS: [(&str, &[&str]); 4] = [("run_inno_installer", &["op", "file", "sha256"]), /* … */];
pub fn parse(bytes: &[u8], os: HelperOs) -> Result<Vec<Op>, HelperError>;
```

`parse`: `serde_json::from_slice::<RawJob>`; `job_version == 1`; `1 ≤ ops.len() ≤ 32`; per `RawOp`: known `op`, exactly
its slots present and strings; `sha256` 64 lower hex; `op.target() == os` (so `os == Linux` refuses every op, FR-036).
`app_id`, `pkg_id` and `file` contents are not checked here: their defects are per-op refusals (FR-020, FR-031, FR-033).

#### 4.1.5 `result.rs` (FR-003)

```rust
#[derive(Serialize)] pub struct ResultFile { pub result_version: u32, pub job_sha256: String, pub job_error: Option<&'static str>, pub ops: Vec<OpEntry> }
#[derive(Serialize)] pub struct OpEntry { pub index: u32, pub op: &'static str, pub outcome: OpOutcome, pub code: Option<&'static str>,
    pub exit_code: Option<i32>, pub seconds: f64, pub log_tail: Vec<String> }
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)] #[serde(rename_all = "snake_case")]
pub enum OpOutcome { Done, Locked, Failed, Refused, NotRun }
pub fn write(out: &mut dyn WriteSync, file: &ResultFile) -> io::Result<()>;   // serde_json::to_vec, write_all, sync_all
pub fn log_lines(bytes: &[u8]) -> Vec<String>;  // every line (split on \n, trailing \r stripped), lossy UTF-8: classify's input
pub fn log_tail(bytes: &[u8]) -> Vec<String>;   // FR-003: the last 40 lines (empty lines counted), each cut to ≤ 512 bytes at a char boundary, lossy UTF-8
```

`log_tail` is the result's field only; classification sees `log_lines` of the whole bounded log tail (SI-28, §4.1.10).

#### 4.1.6 `roots.rs` (D3, D7, D17, FR-026, FR-051; PD-5)

```rust
#[derive(Clone, Debug, PartialEq, Eq)] pub enum StateDirRule { UnderProfiles(PathBuf), SameOwnerNonRoot, Exact(PathBuf) }
#[derive(Clone, Debug, PartialEq, Eq)] pub struct WindowsRoots { pub vst3_vendor: PathBuf, pub preset_vendor: PathBuf, pub run_base: PathBuf }
#[derive(Clone, Debug, PartialEq, Eq)] pub struct MacosRoots { pub vst3: PathBuf, pub components: PathBuf, pub presets: PathBuf,
    pub applications: PathBuf, pub volume: PathBuf, pub run_base: PathBuf, pub run_ancestors: Vec<PathBuf> }   // PD-5
#[derive(Clone, Debug, PartialEq, Eq)] pub enum OsRoots { Windows(WindowsRoots), Macos(MacosRoots), None }
#[derive(Clone, Debug, PartialEq, Eq)] pub struct AllowedRoots { pub os: OsRoots, pub state_dir: StateDirRule }
impl AllowedRoots { pub fn production() -> Result<AllowedRoots, HelperError>; }   // takes no argument (FR-051)
#[cfg(feature = "ci-temp-roots")] pub fn from_ci_file(io: &dyn HelperIo, file: &Path) -> Result<AllowedRoots, HelperError>;
pub(crate) struct CanonicalRoots { /* the same shape, canonical */ }
pub(crate) fn canonical(io: &dyn HelperIo, roots: &AllowedRoots) -> Result<CanonicalRoots, HelperError>;
pub const HELPER_FOLDER: &str = "com.krateaudio.krate-manager-helper";
```

`production()`: Windows `os::known_folder(&FOLDERID_ProgramFilesCommon)` + `VST3\Krate Audio`,
`FOLDERID_ProgramData` + `Krate Audio`, `run_base` `FOLDERID_Windows` + `Temp`, `UnderProfiles(FOLDERID_UserProfiles)`;
macOS the four literals of D3, `volume` `/`, `run_base` `/Library/Application Support/<HELPER_FOLDER>`,
`run_ancestors` `["/Library", "/Library/Application Support"]`, `SameOwnerNonRoot`; Linux `OsRoots::None`, `SameOwnerNonRoot`. No input from the job or any settings file (SC-007).

#### 4.1.7 `validate.rs` (FR-020 to FR-027, FR-031, FR-034, D7)

```rust
#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub enum Kind { File, Dir, Symlink, Other }
#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub struct LstatFacts { pub kind: Kind, pub uid: Option<u32>, pub mode: Option<u32> }
pub fn run_dir_ancestors_ok(facts: &[LstatFacts]) -> Result<(), HelperError>;  // each Dir, uid Some(0), mode & 0o022 == 0
pub(crate) fn job_location(io, job: &Path, roots: &CanonicalRoots, os: HelperOs) -> Result<JobPlace, HelperError>;
pub(crate) fn source(io, file: &str, place: &JobPlace, os: HelperOs, ext: &str) -> Result<PathBuf, HelperError>;
pub(crate) fn uninstaller(io, roots: &CanonicalWindows, values: (Option<String>, Option<String>)) -> Result<Uninstaller, HelperError>;
pub(crate) fn pkg_plan(io, roots: &CanonicalMacos, location: &str, files: &[u8], dirs: &[u8]) -> Result<PkgPlan, HelperError>;
pub(crate) fn same_path(a: &Path, b: &Path, os: HelperOs) -> bool;           // component-wise; ASCII case-insensitive on Windows
pub(crate) fn under(child: &Path, root: &Path, os: HelperOs) -> bool;        // strict descendant, same rule
pub fn is_app_id(s: &str) -> bool; pub fn is_pkg_id(s: &str) -> bool; pub fn is_plugin_name(s: &str) -> bool;
pub fn is_unins_name(s: &str) -> bool; pub fn is_job_name(s: &str) -> bool;  // byte matchers, no regex crate
pub const RUN_DIR_SDDL: &str = "D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
```

- `source` (FR-020, FR-027): absolute, UTF-8, no `..`, `canonicalize(file)` equal to `file` under `same_path`, strictly
  under `staging_root`, `lstat` kind `File`, extension `.exe` / `.pkg` (ASCII case-insensitive).
- `uninstaller` (FR-031): `InstallLocation` trimmed of one trailing `\`, canonical, a direct child of `vst3_vendor`;
  `UninstallString` with one pair of surrounding quotes removed and nothing after the closing quote, canonical, parent
  `same_path` the install dir, name `is_unins_name`. Returns `{ exe, install_dir }`.
- `pkg_plan` (FR-033, FR-034, D16): every non-empty line (trailing `\r` stripped) split on `/`, each segment plain (no
  `..`, `.`, empty, NUL), joined onto `volume` + the `location` segments (absent or empty location = `volume`); each
  entry's **parent** canonicalized (deepest existing ancestor rule of FR-026; the entry itself never followed), the last
  component re-joined. Files: allowed iff strictly under `vst3` / `components` / `presets`, or under
  `applications/<N> AUv3.app` with `is_plugin_name(N)`. Directories: removable iff strictly under those roots or (under)
  `applications/<N> AUv3.app`; structural iff equal to a root, an ancestor of one, or `applications` itself. Anything else
  refuses the whole op. `PkgPlan { files: Vec<PathBuf>, dirs: Vec<PathBuf> /* removable, deepest first */ }`.
- `is_app_id`: `^\{[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}\}$`; `is_pkg_id`:
  `^com\.krateaudio\.[a-z0-9]+$`; `is_plugin_name`: `^[A-Z][A-Za-z0-9]{0,63}$`; `is_unins_name`: `^unins[0-9]{3}\.exe$`
  case-insensitive; `is_job_name`: `^job-[0-9a-f]{32}\.json$`. The first two are cross-checked against
  `krate_manager_core::contract::check_install_value` (`contract.rs:267`, the key form `{app_id}_is1`) inside
  `validate::tests::rejects_crafted_jobs` (dev-dependency on core).

#### 4.1.8 `digest.rs`

`pub struct Sha256Stream` (`new`, `update`, `finish -> [u8; 32]`), `pub fn sha256(&[u8]) -> [u8; 32]`,
`pub fn hex(&[u8; 32]) -> String`, `pub fn parse_hex64(&str) -> Option<[u8; 32]>`. The only file naming `sha2` (SI-6).

#### 4.1.9 `seams.rs` (FR-053, SI-23)

```rust
pub trait WriteSync: std::io::Write { fn sync_all(&mut self) -> io::Result<()>; }
pub trait HelperIo {
    fn read_bounded(&self, path: &Path, max: u64) -> io::Result<Vec<u8>>;  // from the start, at most max + 1 bytes (PD-6 open)
    fn read_tail(&self, path: &Path, max: u64) -> io::Result<Vec<u8>>;     // the last max bytes (seek from the end; SI-28)
    fn canonicalize(&self, path: &Path) -> io::Result<PathBuf>;
    fn lstat(&self, path: &Path) -> io::Result<Option<LstatFacts>>;        // None = absent; uid/mode None on Windows
    fn open_source(&self, path: &Path) -> io::Result<Box<dyn Read + '_>>;  // PD-6
    fn create_new(&self, path: &Path) -> io::Result<Box<dyn WriteSync + '_>>;
    fn create_helper_folder(&self, path: &Path) -> io::Result<()>;          // PD-26 (macOS); Ok when present
    fn create_run_dir(&self, path: &Path) -> io::Result<()>;                // D7: fails on an existing name
    fn list_dir(&self, dir: &Path) -> io::Result<Vec<OsString>>;
    fn remove_file(&self, path: &Path) -> io::Result<()>;
    fn remove_dir(&self, path: &Path) -> io::Result<()>;
    fn random_hex(&self) -> String;                                         // 32 lower hex (PD-19)
    fn sleep(&self, d: Duration);
    fn now(&self) -> Duration;                                              // monotonic since an arbitrary origin
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub struct ChildId(pub u64);
pub trait ProcessRunner {
    fn spawn(&self, argv: &[OsString]) -> io::Result<ChildId>;
    fn wait(&self, child: ChildId, timeout: Duration) -> io::Result<Option<i32>>;       // None = still running
    fn output(&self, argv: &[OsString], timeout: Duration) -> io::Result<Option<(i32, Vec<u8>)>>;
}
pub trait Registry {
    fn uninstall_values(&self, subkey: &str) -> Option<(Option<String>, Option<String>)>;  // (UninstallString, InstallLocation)
    fn key_exists(&self, subkey: &str) -> bool;
}
pub struct Seams<'a> { pub io: &'a dyn HelperIo, pub runner: &'a dyn ProcessRunner, pub registry: &'a dyn Registry,
    pub classify: fn(i32, &[String]) -> InnoVerdict }
#[cfg(any(test, feature = "test-support"))] pub mod fake;
```

`seams::fake` (one shared `World`): `World { clock: Mutex<Duration>, events: Mutex<Vec<(Duration, Event)>>, owners:
Mutex<BTreeMap<PathBuf, (u32, u32)>> /* uid, mode overrides */, faults: Mutex<Vec<Fault>>, calls: Mutex<Vec<Call>> }`;
`Event::{RemovePath(PathBuf), RemoveKey(String)}` fire when the clock passes their time. `TempIo<'w>` (`TempIo::new(world,
base)`) = the real filesystem under a temp dir (the tree assertions of SC-001 compare real bytes) plus the world's
overrides, faults (`create_run_dir` fails, `create_new` fails with a given `io::ErrorKind`, a `StorageFull` write fault
after N bytes, `remove_file` fails for a path), copy hooks (rewrite the source after / during the copy, replace it by a
link or, on Unix, a FIFO before the open), the fake clock (`sleep` advances, `now` reads) and a counter-based
`random_hex`. Its `open_source`, `read_bounded` and `read_tail` call the production `os::open_regular_no_follow` (PD-6)
after the hooks, so the hook rows test the real open. **Sandbox:** every mutating call (`create_new`,
`create_helper_folder`, `create_run_dir`, `remove_file`, `remove_dir`) and every `open_source` / `read_bounded` /
`read_tail` whose path is not a strict descendant of `base` (lexical, as `Roots::check_sandboxed`, `roots.rs:173-184`)
panics with "outside the test sandbox", so a test left on a production root (`C:\Windows\Temp`, `/Library/…`, `volume`
`/`) panics instead of writing to the dev box; `canonicalize`, `lstat` and `list_dir` stay unrestricted (read-only, and
the roots' canonicalization reads ancestors above `base`). **Owners:** `owners` overrides apply to a path, and
`World::root_owned(path)` marks a directory and everything `TempIo` later creates under it as uid 0 (mode `0o700` for a
run directory, `0o755` otherwise), so the macOS `run_dir_ancestors_ok` chain (`run_ancestors` + `run_base` + the new run
directory, PD-5) passes on every leg, including Windows and Linux, where the real `lstat` has no uid; `lstat` returns the
real kind (an absent path stays `None`) with the override's uid / mode. `ScriptedRunner<'w>`: per argv prefix a `Script { exit: i32, takes: Duration, never_exits:
bool, effect: Option<Box<dyn Fn(&[OsString]) + Sync + 'w>>, stdout: Vec<u8> }`; `wait` advances the clock by `takes` (or by
`timeout` when `never_exits`) and records every call. `FakeRegistry<'w>`: map + `RemoveKey` events. Everything is `Mutex`
based so the core's loopback can hold it behind `Elevation: Sync`.

#### 4.1.10 `ops.rs` (FR-030 to FR-036, FR-040 to FR-043; PD-17, PD-24)

```rust
pub const INNO_INSTALL_TIMEOUT: Duration = Duration::from_secs(600);
pub const PKG_INSTALL_TIMEOUT: Duration = Duration::from_secs(600);
pub const UNINSTALL_POLL_TIMEOUT: Duration = Duration::from_secs(120);
pub const UNINSTALL_POLL_INTERVAL: Duration = Duration::from_millis(500);
pub const PKGUTIL_TIMEOUT: Duration = Duration::from_secs(30);
pub const INNO_FLAGS: [&str; 4] = ["/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOCLOSEAPPLICATIONS"];
pub const UNINSTALL_FLAGS: [&str; 3] = ["/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"];
pub const INSTALLER: &str = "/usr/sbin/installer";
pub const PKGUTIL: &str = "/usr/sbin/pkgutil";
pub const UNINSTALL_KEY: &str = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
pub const INNO_KEY_SUFFIX: &str = "_is1";
pub fn copy_hashed(io: &dyn HelperIo, src: &Path, dest: &Path) -> Result<[u8; 32], HelperError>;  // FR-035
pub(crate) fn run_batch(ops: &[Op], ctx: &OpCtx<'_>, seams: &Seams<'_>) -> Vec<OpEntry>;
```

`run_batch`: strictly in order (FR-040). The run directory is created lazily before the first installer op:
`<run_base>/run-<random_hex>` via `create_run_dir`. `os == Windows`: nothing more (the DACL is set atomically at creation,
D7). `os == Macos`: `create_helper_folder(run_base)` first, then the `lstat` facts of every `roots.run_ancestors` entry, of
`run_base` and of the new directory through `run_dir_ancestors_ok` (PD-5; production `/Library`,
`/Library/Application Support`, the helper folder, `run-*`; tests the temp equivalents marked `root_owned`); failure marks
that op and every later installer op `failed E_HELPER_RUN_DIR` (uninstall ops still run). Per op `start = io.now()`,
`seconds = (io.now() - start).as_secs_f64()`.

- Installer ops (FR-030, FR-032): `validate::source` → `copy_hashed(open_source(file), <run>/installer-<i>.<ext>)`
  (64 KiB chunks, digest over the bytes written, `sync_all`; an I/O error → remove the partial, `E_HELPER_COPY`; digest ≠
  `sha256` → remove, `E_HELPER_HASH`) → `spawn` the copy with exactly `[copy, INNO_FLAGS…, "/LOG=<run>\inno-<i>.log"]`
  (Windows) or `[INSTALLER, "-pkg", copy, "-target", "/"]` (macOS); spawn error `E_HELPER_SPAWN` → `wait(timeout)`:
  `None` → `failed E_HELPER_TIMEOUT`, the batch stops (later ops `not_run E_HELPER_AFTER_TIMEOUT`), no kill (FR-042);
  `Some(code)` → Windows: `bytes = read_tail(log, 1 MiB)` (the **last** MiB, SI-28), `(seams.classify)(code,
  &log_lines(&bytes))` over every line of it, the entry's `log_tail = log_tail(&bytes)`; macOS: `0` → `done`, else `failed
  E_HELPER_EXIT`.
- `run_inno_uninstaller` (FR-031, D6): `is_app_id` → `registry.uninstall_values(UNINSTALL_KEY\<app_id>_is1)` (`None` →
  `failed E_HELPER_NO_KEY`) → `validate::uninstaller` → spawn `[exe, UNINSTALL_FLAGS…]` → `wait(UNINSTALL_POLL_TIMEOUT)`
  (`None` → `E_HELPER_UNINSTALL_TIMEOUT`; non-zero → `failed E_HELPER_EXIT`, no poll) → poll: check `lstat(exe)` absent,
  no `list_dir(install_dir)` name ending `.vst3` (NotFound = none), `!registry.key_exists(subkey)`; done when all three hold,
  else `sleep(UNINSTALL_POLL_INTERVAL)`; 241 checks / 240 sleeps reach 120 s → `E_HELPER_UNINSTALL_TIMEOUT` (SC-004).
- `uninstall_pkg` (FR-033): `is_pkg_id` → `output([PKGUTIL, "--pkg-info", id], PKGUTIL_TIMEOUT)` (non-zero or none →
  `failed E_HELPER_PKG_RECEIPT`; `package-id:` must equal `id`, `location:` read) → `--files id --only-files` and
  `--files id --only-dirs` → `validate::pkg_plan` (refusal → `refused E_HELPER_PKG_PATH`, nothing removed) → remove every
  file entry (`remove_file`, which removes a link as a link; NotFound counts as removed), then every removable directory
  deepest first with `remove_dir` (a non-empty one is left) → any file entry still present (`lstat`) → `failed
  E_HELPER_REMOVE`, no `--forget` → else `[PKGUTIL, "--forget", id]` (non-zero → `failed E_HELPER_FORGET`) → `done`.
- After the last op (and after a timeout) every file created in the run directory, then the directory, is removed
  (FR-043); a failure is ignored unless no op ran.

#### 4.1.11 `classify.rs` (D10, FR-062)

`#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub enum InnoVerdict { Installed, Locked, Failed }`;
`pub fn classify_inno(exit_code: i32, log: &[String]) -> InnoVerdict` — until M-1: `0` → `Installed`, else `Failed`.
After M-1 (the resume, FR-063): `pub const LOCKED_EXIT: i32 = <recorded>; pub const LOCKED_LINE: &str = "<recorded fixed
part>";` and `Locked` iff `exit_code == LOCKED_EXIT && log.iter().any(|l| l.contains(LOCKED_LINE))`.

#### 4.1.12 `os.rs` (PD-6, PD-7)

`OsHelperIo { origin: Instant }`: `std::fs` for files; `lstat` via `symlink_metadata` + `MetadataExt::{uid, mode}` on Unix;
`create_run_dir`: Unix `DirBuilder::new().mode(0o700).create`; Windows `ConvertStringSecurityDescriptorToSecurityDescriptorW
(RUN_DIR_SDDL, SDDL_REVISION_1, …)` → `SECURITY_ATTRIBUTES { nLength, lpSecurityDescriptor, bInheritHandle: 0 }` →
`CreateDirectoryW` → `LocalFree`. `random_hex`: two `RandomState::new()` hashers over an `AtomicU64` counter and the
process id, 16 bytes → hex. `sleep` / `now` over `std::thread::sleep` / `Instant` (the only `Instant` home in the helper,
FR-041). `OsRunner { children: Mutex<BTreeMap<u64, Child>> }`: `Command::new(argv[0]).args(&argv[1..])` with all three
stdio `null()`, never a shell; `wait` polls `try_wait` every 100 ms until the timeout; a signal-terminated child is
`128 + signal` on Unix; `output` the same with stdout piped. `OsRegistry`: Windows `RegOpenKeyExW(HKEY_LOCAL_MACHINE, …,
KEY_READ | KEY_WOW64_64KEY)` + `RegGetValueW(RRF_RT_REG_SZ)`, the `os_facts.rs:46-150` pattern; elsewhere `None` / `false`.
`pub(crate) fn known_folder(id: &GUID) -> Option<PathBuf>` (Windows): `SHGetKnownFolderPath(id, KF_FLAG_DEFAULT as u32,
null, &mut p)`, copy the wide string, `CoTaskMemFree(p)` on every path. `pub(crate) fn open_regular_no_follow(path) ->
io::Result<File>` (PD-6; shared with `TempIo`); `read_bounded` reads at most `max + 1` bytes from it; `read_tail` seeks to
`len.saturating_sub(max)` and reads to the end.

`os_tests.rs` (unelevated, over a `TempDir`, SI-25; the real seams may be built here, §4.8.1):
`os::tests::read_bounded_stops_at_bound` (cargo.all: a `MAX + 1`-byte read of a 2 × `MAX` file returns `MAX + 1` bytes;
`read_tail` of the same file returns its last `MAX`); `os::tests::open_source_refuses_link_windows` (cargo.windows: a
`std::os::windows::fs::symlink_file` link to a regular file → error, the target opens); and, `cfg(unix)`, in both
cargo.macos and cargo.linux: `os::tests::open_source_refuses_links_and_fifos_unix` (a `std::os::unix::fs::symlink` link →
error; a FIFO made with `/usr/bin/mkfifo` → error, returned in under 1 s; a regular file opens),
`os::tests::run_dir_mode_unix` (`create_run_dir` → `symlink_metadata().mode() & 0o777 == 0o700`; a second call on the
name fails), `os::tests::wait_timeout_leaves_child_unix` (`OsRunner::spawn(["/bin/sleep", "30"])`, `wait(200 ms)` →
`Ok(None)`, a second `wait(Duration::ZERO)` → still `Ok(None)` (the child was not killed), then the test kills and reaps it
through the module-private `children` map). The Windows link row needs the symbolic-link privilege: held on
`windows-latest` (the runner account is an administrator) and on this dev box (Developer Mode on:
`HKLM\…\AppModelUnlock\AllowDevelopmentWithoutDevLicense = 1`, and an unelevated symlink create succeeded, measured
2026-11-01). Without it the row **fails** with "symbolic-link privilege not held: enable Developer Mode" (it never skips),
so `required tests ran` cannot be satisfied by a silent skip; `validate::tests::symlinked_parent_root_windows` follows the
same rule.

### 4.2 Core: `install.rs`

#### 4.2.1 Elevation (FR-071, FR-090, FR-091; PD-9, PD-18)

```rust
#[derive(Clone, Debug, PartialEq, Eq)] pub struct ElevatedCommand { pub program: PathBuf, pub args: Vec<String> }
#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub enum LaunchOutcome { Exited(i32), Refused, Failed(&'static str), TimedOut }
pub trait Elevation: Sync {
    fn helper(&self) -> Option<&Path>;
    fn launch(&self, command: &ElevatedCommand, timeout: Duration) -> LaunchOutcome;
    fn guard(&self) -> &ElevationGuard;
}
pub struct ElevationGuard { hold: Mutex<Hold> }                // Hold::{Free, Running, Stale { job: PathBuf, result: PathBuf }}
impl ElevationGuard { pub const fn new() -> ElevationGuard; }
pub const OSASCRIPT_LINES: [&str; 3] = [
    "on run argv",
    "do shell script (quoted form of item 1 of argv) & \" \" & (quoted form of item 2 of argv) & \" \" & (quoted form of item 3 of argv) & \" \" & (quoted form of item 4 of argv) & \" \" & (quoted form of item 5 of argv) with administrator privileges",
    "end run",
];
pub fn osascript_outcome(status: Option<i32>, stderr: &str) -> LaunchOutcome;   // 0 → Exited(0); "(-128)" → Refused; "(<n>)" at the end → Exited(n); else Failed("E_ELEVATION_OSASCRIPT")
pub fn windows_parameters(args: &[String]) -> Result<String, InstallError>;    // FR-091
```

`ElevationGuard::enter(&self, io: &dyn InstallIo) -> Result<Held<'_>, InstallError>`: `Running` → `ElevationBusy {
stale: false }`; `Stale` → `io.kind_of(result)` is `Some(File)` → remove job and result (`warn!` on error), become
`Running`; else `ElevationBusy { stale: true }`; `Free` → `Running`. `Held::stale(self, job, result)` records `Stale`;
dropping a `Held` otherwise sets `Free` (so on unwind too, FR-078). Lock poisoning is ignored (`PoisonError::into_inner`,
as `BUSY`).

#### 4.2.2 `pub mod job` (inline in `install.rs`; FR-073, FR-076; PD-11, PD-12, SI-11)

```rust
pub const JOBS_DIR: &str = "jobs";
pub const MAX_RESULT_BYTES: u64 = 1 << 20;
pub const INNO_INSTALL_TIMEOUT: Duration = …; /* the five helper timeouts, PD-11 */ pub const ELEVATION_MARGIN: Duration = Duration::from_secs(120);
#[derive(Clone, Debug, PartialEq, Eq, Serialize)] #[serde(tag = "op", rename_all = "snake_case")]
pub enum JobOp { RunInnoInstaller { file: String, sha256: String }, RunInnoUninstaller { app_id: String },
    RunPkgInstaller { file: String, sha256: String }, UninstallPkg { pkg_id: String } }
#[derive(Serialize)] struct JobFile<'a> { job_version: u32, ops: &'a [JobOp] }          // FR-001 key order
#[derive(Clone, Debug, PartialEq, Eq)] pub struct WrittenJob { pub path: PathBuf, pub result: PathBuf, pub stem: String, pub sha256: String, pub ops: Vec<JobOp> }
pub fn write(io: &dyn InstallIo, state_dir: &Path, ops: Vec<JobOp>) -> Result<WrittenJob, InstallError>;
pub fn ceiling(ops: &[JobOp]) -> Duration;                                                      // PD-17
#[derive(Clone, Debug, PartialEq, Eq)] pub enum OpReport { Done, Locked, Failed { code: &'static str, exit_code: Option<i32> }, Refused { code: &'static str }, NotRun { code: &'static str } }
pub fn read_result(bytes: &[u8], job: &WrittenJob) -> Result<Vec<OpReport>, ResultError>;      // ResultError::{NoResult, Helper(&'static str)}
pub fn helper_code(s: &str) -> Option<&'static str>;                                          // the catalogue, PD-11
```

`write`: `kind_of(<state>/jobs)` must be `None` (→ `create_dir`) or `Dir`; canonicalize the jobs dir; name from
`job_stem()` (PD-19), `write_new` (`AlreadyExists` → next name, at most 4), `sync_file`; bytes from
`serde_json::to_vec(&JobFile)`; hash with `verify::Sha256Stream` over the written bytes. Errors map through `io_err(Step::
JobWritten, …)`. `read_result`: `serde_json` into a private `deny_unknown_fields` mirror of the helper's `ResultFile`;
`result_version == 1`, `job_sha256 == job.sha256`, else `NoResult`; `job_error: Some(c)` → `Helper(helper_code(c)?)`
(unknown → `NoResult`); `ops.len() == job.ops.len()` and each `index` in order, `op` equal to the job's kind, else
`NoResult`.

#### 4.2.3 The drivers (FR-070, FR-072 to FR-078a, FR-114; PD-16, PD-17)

`pub struct WindowsDriver; pub struct MacosDriver;` both delegate to `fn run_elevated(os, env: InstallEnv<'_>, batch)`:

1. Per action, in order: `Install` / `Update`: format `InnoExe` (Windows) / `Pkg` (macOS), else `Format`; `canonical =
   io.canonicalize(artifact.path())` under `io.canonicalize(staging_dir())` (`Path::starts_with` on canonical forms), else
   `Path { reason: "the artifact is not in staging" }`; UTF-8 else `PathNotUtf8 { which: "staging" }` → `JobOp::
   RunInnoInstaller { file: canonical, sha256: verify::hex_lower(artifact.sha256()) }` (resp. `RunPkgInstaller`). The
   op's `sha256` is **always** the digest Phase 7 checked against the minisign-signed feed (`VerifiedArtifact::sha256`,
   `verify.rs:150`); the driver never re-reads or re-hashes the staged file, so a staged file rewritten after
   verification (staging is user-writable) fails the helper's hash-while-copy check (`E_HELPER_HASH`, zero spawns)
   instead of being re-blessed by the job (§6 "Verify before execute"; §7.1 SC-014 `job_hash_is_the_verified_digest`).
   `Uninstall`: `action_gate(os, ctx, io, entry, &detection)` with a fresh detection (§4.2.4) → `Inno { app_id }` /
   `Pkg { pkg_id }` → `JobOp`; `OutsideRoot` → `RemoveElsewhere`; `NotManaged` / `Manifest` → `NotManaged`; `Blocked(e)` → `e`.
2. No op → return. `env.elevation.helper()` `None` → `HelperMissing` for every op-bearing action, nothing written.
3. `guard().enter(io)` → refusal for every op-bearing action, zero mutating calls.
4. `ctx.roots.check_sandboxed(&state_dir.join(job::JOBS_DIR))` (a test `Roots` panics on a jobs dir outside its
   sandbox, as Phase 9 does for its journal and manifest dirs, `install.rs:2421-2422`), then `job::write` → error for every
   op-bearing action. `progress(InstallProgress::Elevating)` once.
5. `launch(ElevatedCommand { program: helper.to_path_buf(), args: ["--job", <job path>, "--job-sha256", <hex>] },
   job::ceiling(&ops))` (FR-076). One `info!` with the outcome (FR-114).
6. Map: `Refused` → `Refused`; `Failed(c)` → `Elevation { code: c }`; `TimedOut` → `NoResult` for all, `Held::stale(job,
   result)`, files kept, go to 11 (no `done` report, no `Ok` action); `Exited(0 | 3)` → `io.read_file(result, MAX_RESULT_BYTES)` + `job::read_result` →
   per op: `Done` → `Ok(Outcome { version: Some(entry.version_text) for install/update, None for remove, 0, 0, [] })`,
   `Locked` → `Locked`, `Failed { code: "E_HELPER_EXIT", exit_code: Some(n) }` → `InstallerFailed { exit_code: n }`, every
   other report → `Helper { code }`; `NoResult` / any other exit → `NoResult`; `Helper(c)` → `Helper { code: c }` for all.
   One `info!` per op (code, exit code, seconds) and the tail as `debug!` (FR-114).
7. Remove the job and the result (`remove_file`, step `JobRemoved`, `warn!` on error), on every path but `TimedOut`.
8. Drop the hold.
9. macOS, any install / update whose helper report (step 6) is `Done`: `env.host_io.run(&platform::KILLALL_ARGV,
   KILLALL_WAIT)` once, outcome ignored (FR-077: "at least one install or update is `done`"). Keyed on the helper's `done`
   **before** re-detection, so an install that step 10 turns into `NotApplied` (the VST3 `moduleinfo.json` not updated,
   though the pkg may have placed the AU component) still gets the registrar kill.
10. FR-076a: for each `Ok` action `Listing::scan` (its `vst3_bundle`) and `detect_plugin(os, ctx, entry, env.host, &l)`;
   install / update: the managed VST3 copy's `CopyVersion::Known { version }` must equal `entry.version`, else `NotApplied`;
   uninstall: no managed copy and no record, else `NotApplied`.
11. `log_result` per action (Phase 9 format), results in batch order.

`driver_for(os)`: `Windows` → `&WINDOWS`, `Macos` → `&MACOS`, `Linux` → `&LINUX` (FR-070).

#### 4.2.4 Gate and commands (FR-080 to FR-084; PD-13, PD-16)

```rust
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum RemoveBasis { NotManaged, Manifest, Inno { app_id: String }, Pkg { pkg_id: String }, OutsideRoot }
pub enum ActionGate { Blocked(InstallError), Open { manifest: Option<Manifest>, remove: RemoveBasis } }
pub fn action_gate(os: Os, ctx: &AppContext, io: &dyn InstallIo, entry: &PluginEntry, detection: &Detection) -> ActionGate;
```

Phase 9's checks keep their order (`install.rs:2712-2740`); then `remove` per FR-080: Linux `Manifest` iff a manifest;
Windows an `InstallerRecord::Inno { install_location: Some(loc) }` with the entry's key and `loc.parent()` `same_path`
`ctx.roots.vst3_global().join(VENDOR_DIR)` → `Inno { app_id: key up to its first '}' }` (PD-13), a location elsewhere →
`OutsideRoot`; macOS a `Pkg` record with the entry's `macos_pkg_id` → `Pkg`. `allowed_actions` offers `Remove` iff
`Manifest | Inno | Pkg` (FR-081). `install_command`: the listing and detection move before the gate (FR-082);
`remove_for_command(env, name, now_unix, host, progress)` builds the listing and detection, then the gate;
`NotManaged` / `OutsideRoot` refuse before any driver run. `obtain_and_run` passes `InstallEnv { ctx, io, extract,
progress, elevation, host_io, host }` and the PD-16 notice. `native_env(ctx, io, extract, download, elevation, host_io)`.

#### 4.2.5 Recovery, progress, notices (FR-074, FR-075, FR-076)

`recover` gains a pass over `<state>/jobs/`, preceded by `ctx.roots.check_sandboxed(&jobs_dir)` beside the two existing
calls (`install.rs:2421-2422`): `kind_of` must be `Dir` (else nothing); every `read_dir` entry of kind `File`
whose name is `job-<32 lower hex>.json` or `job-<32 lower hex>.result.json` is removed; `RecoveryReport.jobs_removed`
counts them and `log_recovery` prints it. `InstallProgress::Elevating` (`{"kind":"elevating"}`); `Notice::RescanDawMacos`
(`"rescan_daw_macos"`).

### 4.3 `status.rs` (FR-083)

`build_with_os`: `gates.insert(key, install::action_gate(os, ctx, io, entry, &detection))` over the detection it already
holds (`status.rs:229-233`). `PluginView.remove_hint: Option<RemoveHint>` (last field, after `actions`),
`#[derive(Serialize)] #[serde(rename_all = "snake_case")] pub enum RemoveHint { WindowsSettings }`, `Some` iff the gate is
`Open { remove: OutsideRoot }`.

### 4.4 `test_support.rs` additions (SI-19 to SI-21)

- `pub fn fresh_elevated_roots() -> (TempDir, Roots)`: `temp_roots` layout with the state dir
  `<base>/<APP_IDENTIFIER>` and staging under it.
- `pub struct FakeElevation { pub helper: Option<PathBuf>, pub script: Mutex<VecDeque<Scripted>>, pub launches:
  Mutex<Vec<(ElevatedCommand, Duration)>>, pub jobs: Mutex<Vec<Vec<u8>>>, guard: ElevationGuard }`, `Scripted { outcome:
  LaunchOutcome, result: Option<Box<dyn Fn(&Path /*job*/) -> Option<Vec<u8>> + Send>> }` (writes `<job stem>.result.json`
  before returning); constructors `FakeElevation::refusing()`, `::returning(outcome)`, `::missing()`. Every `launch` first
  reads the file named by `args[1]` (the `--job` value) with `std::fs::read` and pushes its bytes onto `jobs`, before the
  scripted outcome, so a test sees the job's contents although the driver removes the file afterwards (SC-010, SC-024,
  `job_hash_is_the_verified_digest`).
- `pub struct LiveHostFacts { pub os_version, pub inno: Mutex<BTreeMap<String, RecordRead<InnoValues>>>, pub pkg:
  Mutex<…> }` implementing `HostFacts`, mutable by a loopback effect.
- `InstallRig` gains `pub elevation: FakeElevation` (`missing()`) and `pub host_io: FakeHostIo`; `command_env(…)` keeps its
  signature and borrows them; `command_env_with(os, arch, io, extract_io, drivers, elevation, host_io)` for Phase 10.

`install_loopback_tests.rs` (core, `#[cfg(test)]`): `Loopback { os, roots: AllowedRoots /* Windows: vst3_vendor =
<temp Vst3Global>\Krate Audio, preset_vendor = the temp factory root, run_base = <base>\run (never the production
C:\Windows\Temp, which unelevated users can write, R-9); macOS: vst3 / components = the temp globals, presets = the temp
factory root, applications = <base>/Applications, volume = <base>, run_ancestors = [<base>/Library,
<base>/Library/Application Support], run_base = <base>/Library/Application Support/<HELPER_FOLDER>, all marked
World::root_owned */, world: seams::fake::World, host: &LiveHostFacts, guard, classify }` implements `Elevation`: `launch`
parses the argv with `krate_manager_helper::cli::parse_args`, calls `krate_manager_helper::run(&args, os, &roots, &seams)`
with `TempIo::new(&world, <base>)` (so a root left on a production value panics, §4.1.9), a `ScriptedRunner` whose effects "install" (write the
record into `LiveHostFacts` / the fake registry and the bundle with a `moduleinfo.json` under the temp roots) and
"uninstall" (remove both), and returns `Exited(exit.status)`. `StateDirRule::Exact(state_dir)`.

### 4.5 src-tauri (FR-090, FR-105; PD-18, PD-23)

`src-tauri/src/os_elevation.rs`:

```rust
pub struct NativeElevation { helper: Option<PathBuf>, guard: ElevationGuard }
impl Elevation for NativeElevation { /* helper(); launch(); guard() */ }
```

- Windows `launch`: a `std::thread` that `CoInitializeEx(null, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)`, fills
  `SHELLEXECUTEINFOW { cbSize, fMask: SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI, lpVerb: "runas",
  lpFile: helper, lpParameters: windows_parameters(args)?, nShow: 0 /* SW_HIDE */, ..Default::default() }`, calls
  `ShellExecuteExW`; `FALSE` and `GetLastError() == ERROR_CANCELLED` → `Refused`, else `Failed("E_ELEVATION_SHELLEXECUTE")`;
  a null `hProcess` → `Failed("E_ELEVATION_NO_PROCESS")`; `WaitForSingleObject(h, ms)` `WAIT_TIMEOUT` → `TimedOut`, else
  `GetExitCodeProcess` → `Exited(code as i32)`; `CloseHandle`, `CoUninitialize`. Every `unsafe` block carries a `SAFETY:`
  comment (the `os_facts.rs` style).
- macOS `launch`: `Command::new("/usr/bin/osascript").args(["-e", L0, "-e", L1, "-e", L2, helper, a0, a1, a2, a3])`,
  stdout null, stderr piped, polled with `try_wait` until the timeout (→ `TimedOut`, the child left running), then
  `install::osascript_outcome(status.code(), &stderr)`.
- Linux: `Failed("E_ELEVATION_NONE")`.

`lib.rs`: `pub fn resolve_elevation(exe: Option<&Path>) -> NativeElevation` (helper =
`install::helper_beside(<native Os>, exe, &FsInstallIo)`: `exe.parent()` + `krate-manager-helper[.exe]`, `None` when
absent, when `exe` has no parent, or on Linux; PD-23); `run()` `.manage(Arc::new(resolve_elevation(
std::env::current_exe().ok().as_deref())))`. `commands.rs`: `install_plugin` / `remove_plugin` gain
`elevation: tauri::State<'_, Arc<NativeElevation>>` (allowed by `config_guard.rs:525-531`), clone the `Arc` into the
blocking closure, pass `&*elevation` and `&OsHostIo` to `native_env`, and `&PlatformFacts { io: &OsHostIo }` to
`remove_for_command`. No new command, no capability change (FR-092).

`examples/manual_elevation.rs` (FR-105, SI-14), fail-closed on both OSes:

1. Takes exactly one argument `<dir>`; refuses to start (exit 2, nothing written) unless `<dir>` exists, is a directory
   (`symlink_metadata`, not a link) and `read_dir` yields no entry. So `%LOCALAPPDATA%`, `~/Library/Application Support`
   or any parent of the real state dir is refused: none is empty.
2. Creates `<dir>/com.krateaudio.krate-manager/` and its `jobs/` with `create_dir` (not `create_dir_all`), recording each.
3. Writes the job through `install::job::write` with `FsInstallIo`: one op the helper's own matcher refuses before any
   spawn, registry read or filesystem effect: macOS `JobOp::UninstallPkg { pkg_id: "com.krateaudio.NOT-VALID" }`
   (`is_pkg_id` fails → `refused E_HELPER_PKG_ID`, no `pkgutil` call); Windows `JobOp::RunInnoUninstaller { app_id:
   "not-a-guid" }` (`is_app_id` fails → `refused E_HELPER_APP_ID`, no registry read). No installer op, so no run
   directory is ever created.
4. Resolves `resolve_elevation(Some(&exe_dir.parent()?.join("manual_elevation")))`, `exe_dir` = `current_exe()`'s parent
   (`target/debug/examples`), so `helper_beside` looks in `target/debug`, where `cargo build -p krate-manager-helper` put
   the helper (`Path::parent` is lexical: passing `<exe dir>/..` would look in `examples/`). A missing helper prints that
   and exits 1.
5. Launches with a 120 s bound and prints the `LaunchOutcome` and the result file. Expected: Windows `Exited(4)`
   (`E_HELPER_JOB_LOCATION`: `<dir>` is not `<profile>\AppData\Local`, FR-025); macOS `Exited(0)` with one `refused
   E_HELPER_PKG_ID`.
6. Removes exactly what it recorded plus the two names the helper may write: the job file, `<stem>.result.json` (if
   present), `jobs/`, then `com.krateaudio.krate-manager/`, each with `remove_file` / `remove_dir` (never
   `remove_dir_all`); `<dir>` itself stays. A removal error is printed, not retried.

### 4.6 Bundling and Node scripts (FR-092, FR-100, FR-101, FR-104; PD-20)

- `src-tauri/tauri.helper.conf.json`: `{ "bundle": { "externalBin": ["binaries/krate-manager-helper"] } }`.
- **Module form (all four scripts and their tests).** `apps/krate-manager/package.json:4` declares `"type": "module"`, so
  every `.js` file under `apps/krate-manager/` is an ES module: `require` and `module` are undefined there and
  `@typescript-eslint/no-require-imports` (in `strictTypeChecked`) refuses them. The scripts are therefore ESM:
  `import … from 'node:…'` statements, no `'use strict'` (implicit in modules), paths from `import.meta.dirname` (Node
  ≥ 20.11; CI pins Node 24, `manager-ci.yml:42-44`), and the main-module guard
  `if (process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href) main();`
  (`node:url`). The extension stays `.js` so the `scripts/*.test.js` glob, `required-tests.json` and the shape test are
  unchanged. (The repo-root `tools/*.js` are CommonJS only because the root has no `"type": "module"`.)
- **Type-checking and lint of the scripts.** `tsconfig.node.json` today is `include: ["vite.config.ts"]`, `module:
  ESNext`, `moduleResolution: bundler`, `strict`, `types: ["node"]`, with no `allowJs` / `checkJs`; `eslint.config.mjs:7`
  does not ignore `scripts/`. The first script task edits `tsconfig.node.json` (`"allowJs": true`, `"checkJs": true`,
  `"include": ["vite.config.ts", "scripts/*.js"]`), writes `scripts/bundle.js` alone, and runs `npm run typecheck && npm
  run lint` on it **before** the other three scripts are written; any finding there is fixed in code (JSDoc types, an
  explicit `r.status ?? 1` for `spawnSync`'s `number | null` status, a `process.argv[n] === undefined` check), never with a
  `// @ts-ignore` or `eslint-disable`. A suppression that proves unavoidable is recorded in this section with its one-line
  reason before it is written (none is expected).
- `scripts/bundle.js` (`#!/usr/bin/env node`, ESM, usage line): exports `sidecarName(hostTuple)` (`…-<tuple>`
  plus `.exe` iff the tuple contains `-windows-`) and `tauriArgs(platform)` (`['build', '--config',
  'src-tauri/tauri.helper.conf.json']` off Linux, `['build']` on Linux); `main()` runs `rustc --print host-tuple`, `cargo
  build -p krate-manager-helper --release --locked` (skipped on Linux), copies `target/release/krate-manager-helper[.exe]`
  to `src-tauri/binaries/<sidecarName>`, then `npx tauri …`; `child_process.spawnSync` with `stdio: 'inherit'`, exit code
  propagated. The ESM main-module guard (above) calls `main()`.
- `scripts/bundle.test.js` (`node:test`): `bundle sidecar name per host tuple`.
- `scripts/write-test-job.js`: `--state <S> --op <kind> [--file <canonical> --sha256 <hex> | --app-id <id> | --pkg-id <id>]`
  (repeatable `--op` groups) → writes `<S>/jobs/job-<32 hex>.json` with FR-001's keys in order (`JSON.stringify` of objects
  built in that order), prints `<path> <sha256>` (`node:crypto`). CI only. **Windows path form (FR-027, FR-021):** the
  helper accepts only the verbatim `\\?\` form for `file` and `--job` (a plain `C:\…` `file` is `E_HELPER_SOURCE`, a
  plain `--job` fails `canonicalize(job) == job`), while `fs.realpathSync.native` and PowerShell's `Resolve-Path` return
  plain `D:\a\_temp\…`. So on `process.platform === 'win32'` the script canonicalizes `--state` and every `--file` with
  `fs.realpathSync.native` and passes both through the exported pure `verbatim(p, platform)`: `win32` → `\\?\` + `p` for
  a drive path, `\\?\UNC\` + the rest for `\\server\share\…`, unchanged when already `\\?\…`; any other platform →
  unchanged. The `file` values written into the job and the printed job path are the verbatim strings, and the
  `pwsh` steps pass the printed path unchanged to `Start-Process -ArgumentList` (§8.2). `scripts/write-test-job.test.js`
  (`node:test`): `write test job prints verbatim windows paths` (drive, UNC, already-verbatim, and non-win32 rows of
  `verbatim`; a job written into a temp dir with `platform = 'win32'` forced holds `\\?\` `file` values and FR-001's key
  order).
- `scripts/inno-fixture.test.js`: `fixture inno script keeps the plugin directives` (FR-101): normalises
  `tests/fixtures/inno/fixture.iss` and `plugins/iterum/installers/windows/setup.iss` by reading each file's own
  `#define AppName "<name>"` line (`setup.iss:16` holds `#define AppName "Iterum"`; `fixture.iss` holds the same line for
  the fixture name) and replacing that name with the token `<APP>` in its file, so the test source names neither the
  plugin nor the fixture (the roster scan and `fixture_names_confined`, §4.8.3, stay clean without an exemption); it
  compares the listed directive lines, bans `{commonpf}`, `{commonappdata}`, `{pf}`, `{sys}`, `{win}` in the fixture, and
  reports a planted drift per line. `write-test-job.js` takes the fixture's ids as arguments and names no fixture either.

### 4.7 UI (`src/view.ts`, `src/status.ts`; FR-130 to FR-135)

- `view.ts`: `PluginView.remove_hint: "windows_settings" | null` (parser key list + `nullableOneOf`); `ActionNotice` +
  `"rescan_daw_macos"`; `InstallProgress` + `{ kind: "elevating" }`.
- `status.ts` `INSTALL_TEXTS` gains `adminWindows`, `adminMacos`, `confirmRemoveInstaller` (FR-131), `elevating` (FR-132),
  `rescanMacos1` … `rescanMacos4` and `appleGuide` (FR-133, in Apple's order), `removeElsewhere` (FR-134).
  `confirmLines(os, p, action)` appends the admin line on `windows` / `macos` and uses `confirmRemoveInstaller` on both
  (FR-131; tested per OS by `windows remove confirmation names the uninstaller` and `macos remove confirmation names the
  uninstaller`, SI-30);
  `progressText` maps `elevating`; `outcomeLines` adds the four steps and the guide line after `rescan` for
  `rescan_daw_macos`; `renderCard` appends `removeElsewhere` when `remove_hint` is set. Errors keep `rejectionText`
  (PD-15). `actionRow` receives `os`.
- The 13 `tests/fixtures/status/*.json` gain `"remove_hint": null` as each plugin object's last key.

### 4.8 Guards

#### 4.8.1 `injection.rs` (FR-093, FR-112, SI-5, SI-6)

New or widened rules, each with a plant in `injection_scanner_reports_planted_violations`:
`ShellExecuteExW`, `runas`, `osascript`, `administrator privileges` only in `src-tauri/src/os_elevation.rs` and
`crates/core/src/install.rs`; `/usr/sbin/installer`, `--forget`, `CreateDirectoryW`, `SHGetKnownFolderPath` only in
`crates/helper/src/` (tests excepted for `installer` / `--forget` in `ops_tests.rs`); in **non-test** sources
(`is_test_source`, `injection.rs:1559-1567`, SI-25) `OsRunner`, `OsRegistry`, `OsHelperIo` construction and `Seams {`
only in `crates/helper/src/main.rs`, while `crates/helper/src/os_tests.rs` may build the real seams over a `TempDir`
(and is the only test source that may: a construction in another `_tests.rs` is reported); in non-test sources
`Instant :: now` and `SystemTime` only in `crates/helper/src/os.rs` within the helper, `crates/helper/src/*_tests.rs`
exempt (SC-004's wall-time bound), the plant in `ops.rs`; `NativeElevation` construction only in `src-tauri/src/lib.rs`;
FR-024 (SI-29): `xattr`, `setxattr`, `removexattr`, `chown`, `fchown`, `lchown`, `SetNamedSecurityInfo`,
`SetFileSecurity`, `SetSecurityInfo` and `com.apple.quarantine` never in a non-test `crates/helper/src/` file (the run
directory's DACL is given at creation through `SECURITY_ATTRIBUTES`, never changed afterwards; `std::os::unix::fs::chown`
is caught by the `chown` token), with a plant per token family in `ops.rs`;
`/usr/bin/killall` only in `platform/mod.rs` and `install_tests.rs`; Phase 9's `install.rs` bans unchanged.
Widened: `HOST_FFI` homes + `crates/helper/src/os.rs`; `HOST_LITERALS` pkgutil and Uninstall-key homes +
`crates/helper/src/ops.rs` and `crates/helper/src/ops_tests.rs`; the test-source host-literal rule exempts
`crates/helper/src/ops_tests.rs`; `CONFINED_TOKENS` `windows_sys` + `src-tauri/src/os_elevation.rs`,
`crates/helper/src/os.rs`, `crates/helper/src/roots.rs`; `CONFINED_TOML_KEYS` `windows-sys` and `sha2` +
`crates/helper/Cargo.toml`; the `sha2` token rule + `crates/helper/src/digest.rs`; `PLATFORM_LITERALS` +
`"/usr/bin/killall"`, `"-9"`, `"AudioComponentRegistrar"`; `READER_PINS` `windows-sys` features = the eleven of FR-093,
homes `src-tauri/Cargo.toml` and `crates/helper/Cargo.toml` (a planted line in `crates/core/Cargo.toml` is reported).

#### 4.8.2 `config_guard.rs` (SI-8)

The platform-file rule allows `tauri.helper.conf.json` with exactly `{"bundle":{"externalBin":["binaries/krate-manager-helper"]}}`
(and `/bundle/externalBin` stays banned in `tauri.conf.json`, `:648`); src-tauri `[features]` `["dev-feed = []",
"manual-elevation = []"]`, an `[[example]]` with `required-features = ["manual-elevation"]`; scripts gain `"bundle": "node
scripts/bundle.js"`; helper `[features]` exactly `["ci-temp-roots = []", "test-support = []"]`, no `default`; core's
dev-dependency on the helper with `test-support` accepted; `MEMBERS` unchanged.

#### 4.8.3 `roster.rs` (FR-113)

The roster scan already covers `crates/helper/src` and `src-tauri/src` (`roster.rs:97-106`); a new test
`fixture_names_confined` requires `Fixture` and `com.krateaudio.fixture` only under `apps/krate-manager/tests/fixtures/` and
in the two elevated jobs of `manager-ci.yml`, with a plant. `apps/krate-manager/scripts/` holds neither name
(`inno-fixture.test.js` reads both `AppName`s from their files, `write-test-job.js` takes ids as arguments, §4.6), so the
two required tests agree without an exemption. (One more test name in `required-tests.json`, §4.9.)

#### 4.8.4 `manager-ci-shape.test.js` (FR-107, SC-031)

`STEPS` gains, in order: `clippy ci-temp-roots`, `clippy manual-elevation example` (after `clippy dev-feed`), `test
ci-temp-roots` (after `cargo test`), `node --test` (after `lint`), `helper bundled`, `ci-temp-roots absent` (after
`build warnings`); `COMMANDS` gains their exact lines and `tauri build` becomes `npm run bundle 2>&1 | tee
"$RUNNER_TEMP/tauri-build.txt"`; `ASSERTION_STEPS` gains the six; the `required tests ran` lines gain the TAP file and
`req.node`. `checkJob` accepts `['check', 'changes', 'helper-elevated', 'inno-locked-file']`; new `checkElevatedJobs`:
each new job `permissions: contents: read`, no `environment:`, no secret; the two elevated jobs `needs: changes`,
`if: needs.changes.outputs.elevated == 'true'`, their matrix / `runs-on`; `check` has neither; the changed-paths list equals
FR-102b's; `--features ci-temp-roots` only in the two jobs and `check`'s two steps; `manual-elevation` only in its step;
`sudo` only in `helper-elevated`; every Windows helper step `shell: pwsh` with `Start-Process … -Wait -PassThru`. The
check-job bans (`sudo`, `Start-Process`) stay scoped to `check`. FR-106 (SI-29): `pkgbuild`, `/usr/sbin/installer`,
`installer -pkg`, `Minionguyjpro/Inno-Setup-Action`, `ISCC`, and the helper binary invoked with any argument
(`krate-manager-helper` followed by `--job`, `--ci-roots` or a `$`-variable) occur only in `helper-elevated` /
`inno-locked-file`. The `changes` script (PD-21) must hold its three branches: `workflow_dispatch` → true, the
`pull_request` base → head diff, and the fail-open (`before` all zero, or a failing `git diff`) → true; its path list
equals FR-102b's plus `apps/krate-manager/Cargo.toml` and `apps/krate-manager/Cargo.lock` (SI-27). Mutations: a removed
`needs`, a removed `if`, a feature token in `check`'s `clippy` step, `sudo` in `inno-locked-file`, a bash Windows helper
step, `ubuntu-latest` in `changes`, `pkgbuild` in `check`, `Minionguyjpro/Inno-Setup-Action` in `changes`, `installer
-pkg` in `check`, a helper run with `--job` in `check`, the `pull_request` branch removed, the fail-open removed, and
`Cargo.lock` dropped from the path list.

**Placement (feed-tools TAP).** `feed-tools.yml` pins the top-level plan `1..59` and a `NAMES` list of top-level tests
(`.github/workflows/feed-tools.yml:86-122`, checked at `:126-127`); the shape file declares one top-level test, `manager-ci.yml
keeps the Phase 4 shape` (`manager-ci-shape.test.js:1068`), with everything else as `t.test` subtests (`:1071-1109`). So
`elevated jobs are path-gated` (SC-031) is an `await t.test(…)` **subtest** of that top-level test, holding the shape
checks above and its own `mutation: …` subtests; the parent sets `let pathGatedRan = false` before it, the subtest's last
statement sets it `true`, and the parent ends with `assert.equal(pathGatedRan, true, 'elevated jobs are path-gated did not
run')`. The plan count stays `1..59`, `NAMES` and `feed-tools.yml` are unchanged, and the subtest cannot be dropped
silently. §7.4 step 3 replays the TAPCHECK locally.

### 4.9 `tests/required-tests.json` and `EXPECTED_REQUIRED` (FR-108)

Remove `helper_stub_exits_2_with_one_line`, `install::tests::no_driver_off_linux`,
`install::tests::native_wiring_offers_nothing_windows`, `…_macos`, vitest `renders no install buttons on windows or macos`.
Add — `cargo.all`: every helper row of §7.1 without an OS suffix, `core_written_jobs_parse`, `helper_usage_exits_2`,
`cli::tests::ci_roots_flag_is_usage_error`, `classify::tests::provisional_rule` (removed again at the resume and replaced by
the three replay tests), the core rows, `fixture_names_confined`; `cargo.windows`: `validate::tests::verbatim_paths_windows`,
`roots::tests::production_roots_windows`, `validate::tests::symlinked_parent_root_windows`,
`windows_parameters_parse_back_windows`, `install::tests::native_wiring_windows`; `cargo.macos`:
`roots::tests::production_roots_macos`, `install::tests::native_wiring_macos`, `validate::tests::symlinked_parent_root_unix`;
`cargo.linux`: `validate::tests::symlinked_parent_root_unix`; `vitest`: the twelve SC-025 titles (SI-30); new `node`:
`bundle sidecar name per host tuple`, `fixture inno script keeps the plugin directives`, `write test job prints verbatim
windows paths`. Added by review round 1 (SI-25, SI-31): `cargo.all` + `install::tests::new_error_texts`,
`install::tests::job_hash_is_the_verified_digest`, `install::tests::helper_resolved_beside_exe`,
`ops::tests::uninstaller_never_exits`, `ops::tests::spawn_error_is_named`, `ops::tests::classify_sees_whole_bounded_log`,
`ops::tests::temp_io_refuses_outside_base`, `cli::tests::malformed_hash_is_usage_error`,
`os::tests::read_bounded_stops_at_bound`; `cargo.windows` + `os::tests::open_source_refuses_link_windows`; `cargo.macos`
and `cargo.linux` + `os::tests::open_source_refuses_links_and_fifos_unix`, `os::tests::run_dir_mode_unix`,
`os::tests::wait_timeout_leaves_child_unix`. `cli::tests::ci_roots_file_is_read`
is not listed (feature run only; `test ci-temp-roots` asserts it, SC-019). Every name is unique across the workspace (the
helper's `roots::tests::*` names differ from core's).

### 4.10 Roadmap amendments (FR-123), made by the build stage

(a) line 477-479: the launcher through `CommandEnv` (D8, PD-9); (b) the M-1 result and measured rule (at the resume);
(c) + (h) the Phase 10 status line, first "stopped at the M-1 fence (Phase 10 built; `inno-locked-file` must run after the
user's push)", rewritten at the resume; (d) Phase 11's universal helper sentence and outcome; (e) D16's structural-directory
parenthesis after line 735; (f) the residual-risk line (Q1); (g) the redirected-`%LOCALAPPDATA%` line in Phase 12's
first-launch documentation. No other roadmap line changes.

### 4.11 State layout

| Path | Written by | Lifetime |
|---|---|---|
| `<state>/jobs/job-<32 hex>.json` | core (user) | removed after the result is read; kept after `TimedOut` until the result appears or restart (FR-078a); `recover` removes leftovers (FR-074) |
| `<state>/jobs/job-<32 hex>.result.json` | helper (Administrators / root owner; the user may read and unlink it: inherited user ACL on Windows, the user's `jobs/` directory on macOS) | as above |
| `<state>/staging/downloads/<sha>/<asset>` | Phase 7 | unchanged; kept after a refusal so a retry downloads nothing (SC-009) |
| Windows `%SystemRoot%\Temp\run-<32 hex>\installer-<i>.exe`, `inno-<i>.log` | helper, protected SYSTEM + Administrators DACL | removed after the job (FR-043) |
| macOS `/Library/Application Support/com.krateaudio.krate-manager-helper/run-<32 hex>/installer-<i>.pkg` | helper (root, 0700) | removed after the job; the helper folder stays |
| HKLM `…\Uninstall\{GUID}_is1`, the pkg receipt database | the plugin installers | read by detection and the helper; removed by the uninstaller / `--forget` |

### 4.12 The OS abstraction

One trait per concern, three implementations each:

| Concern | Windows | macOS | Linux |
|---|---|---|---|
| `InstallDriver` (core) | `WindowsDriver` (Inno ops) | `MacosDriver` (pkg ops, registrar kill) | `LinuxDriver` (Phase 9, unchanged) |
| `Elevation` (core trait, src-tauri impl) | `ShellExecuteExW` `runas` | `osascript … with administrator privileges` | `Failed("E_ELEVATION_NONE")`, never called |
| `AllowedRoots::production()` (helper) | known folders | literals | `OsRoots::None` (every op refused, FR-036) |
| `HelperIo` / `ProcessRunner` / `Registry` (helper) | `cfg(windows)`: DACL run dir, reparse-point-safe open, HKLM 64-bit | `cfg(unix)`: 0700 run dir, `O_NONBLOCK | O_NOFOLLOW` open + `(dev, ino)` check | `cfg(unix)` (same code, compiled and linted on Linux) |

The drivers and the helper library take the OS as a value (`os: Os` / `HelperOs`), so every implementation's logic runs in
every leg's tests; only the real seams and `production()` are cfg-gated.

---

## 5. Dependencies

No new crate, npm package or Tauri plugin.

| Dependency | Version | Use | Verified against |
|---|---|---|---|
| `serde` (derive) | 1.0.229 | helper job / result / ci-roots codecs | `Cargo.toml:33`; `serde_derive-1.0.229/src/de/struct_.rs:269` |
| `serde_json` | 1.0.154 | the same | `Cargo.toml:34` |
| `sha2` | 0.11.1 | helper `digest.rs` (hash while copying, the job hash) | `Cargo.toml:36`; core's `verify.rs:302-315` uses the same `new` / `update` / `finalize().into()` |
| `windows-sys` | 0.61.2, features + `Win32_UI_Shell`, `Win32_System_Com`, `Win32_System_Threading`, `Win32_Security`, `Win32_Security_Authorization`, `Win32_Storage_FileSystem` | src-tauri launcher, helper roots / run dir / registry / reparse-safe open; never core | local crate source (§1.2); https://docs.rs/windows-sys/0.61.2/ |
| `tempfile` (helper dev-dependency) | 3.27.0 | helper test temp dirs | `Cargo.toml:43` |
| `krate-manager-core` (helper dev-dependency, `test-support`) | path | SC-011, the contract cross-check | `crates/helper/Cargo.toml:10-11` |
| `krate-manager-helper` (core dev-dependency, `test-support`) | path | the loopback (PD-8) | scratch probe, §1.2 |
| `tauri-build` | 2.7.4 | the sidecar copy when `externalBin` is merged | `tauri-build-2.7.4/src/lib.rs:139-166`, `:681-684`, `:735-741` |
| `@tauri-apps/cli` | 2.12.4 | `tauri build --config` | `package.json:16`; https://v2.tauri.app/reference/cli/ |
| Node built-ins (`node:test`, `node:fs`, `node:path`, `node:crypto`, `node:child_process`) | Node 24 | scripts | https://nodejs.org/docs/latest-v24.x/api/test.html |
| `Minionguyjpro/Inno-Setup-Action` | v1.2.7 | the CI fixture setups | `release.yml:244-247` |

---

## 6. Safety design

- **Injected roots.** Helper: every library function takes `AllowedRoots` / `CanonicalRoots` and a `JobPlace`; the binary
  builds them from constants (PD-5) and the job's canonical location; `--ci-roots` exists only in the feature build,
  checked absent from the bundle (FR-103). Core: roots, feed URL, `Elevation`, `HostIo`, `HostFacts` injected; tests use
  `fresh_temp_roots` / `fresh_elevated_roots`, the fixture server, `FakeElevation`, the loopback, `FakeHostIo`.
- **Verify before execute.** An installer op is built only from a `VerifiedArtifact` (size, SHA-256, minisign + C3 binding,
  Phase 7) whose canonical path lies in staging (FR-072), and its `sha256` is that artifact's verified digest, never a
  re-hash of the staged file (§4.2.3 step 1); the job's hash is bound on the elevated argv (D2); the helper
  reads the job once, hashes it, then copies each installer into an admin-only directory hashing as it writes and spawns
  only the copy (FR-030, FR-035, PD-6). No archive is extracted in this phase (Phase 8's zip-slip guard is untouched).
- **The helper's operation set** is the four ops, absolute tool paths, fixed argv, no shell, no argument / root / path from
  the job but the staged `file`; removals only of receipt-listed paths, all validated first (FR-033, FR-034); nothing reads
  the environment (`clippy.toml`), writes the registry, adds xattrs or starts a service (FR-022, FR-024, FR-036). Linux has
  no op.
- **How the UI reaches it.** The webview calls the unchanged `install_plugin` / `remove_plugin` commands with a name, a
  version and an action word only (`config_guard.rs:505-540`); the core builds the job; `NativeElevation` executes exactly
  the core's `ElevatedCommand`, whose program is the path resolved at startup (SC-028). The capability file keeps eight
  `allow-*` permissions, no `shell:*` / `fs:*` / `http:*`, no `tauri-plugin-shell`, no remote content.
- **Never touch the real system.** No test, gate or agent runs the helper elevated, raises a prompt, runs an installer,
  `pkgutil --forget`, `killall`, or writes HKLM on this box (FR-110); `AllowedRoots::production()` is called in tests only
  for read-only known-folder lookups (SC-007); the only spawns in local tests are the helper binary without arguments
  (SC-023), `argv-echo` (SC-015) and, in the Unix `os::tests` rows only (WSL and CI, unelevated, inside a `TempDir`),
  `/usr/bin/mkfifo <tmp>/fifo` and `/bin/sleep 30` (killed by the test). The helper's test I/O (`TempIo`) panics on any
  write, open or read outside its temp base (§4.1.9), and the loopback's roots, including Windows `run_base`, are all
  under that base (§4.4), so no helper test can reach `C:\Windows\Temp` or `/Library` by a forgotten root; the core's
  job writer and `recover` call `check_sandboxed` on the jobs dir (§4.2.3 step 4, §4.2.5). The two elevated CI jobs run on ephemeral hosted runners and write under
  `$RUNNER_TEMP`, the run directory, the fixture receipt and the fixture's own HKLM key (FR-111).
- **Residual risk (D19, accepted by the user).** The per-user helper can be replaced by any user process before the prompt;
  the core neither hashes nor copies it (SC-028).

---

## 7. Test plan

Rust: `cargo test --workspace --locked` from `apps/krate-manager/` (default threads); vitest: `npm test`; Node: `node --test
"scripts/*.test.js"`. Helper tests use `TempDir` + `seams::fake` with an explicit `HelperOs`; core tests use
`fresh_temp_roots` / `fresh_elevated_roots`, the fixture server, a throwaway `TestSigner` key, `FakeElevation` or the
loopback.

### 7.1 SC → test

| SC | Test (file) | Assertion strategy |
|---|---|---|
| SC-001 | `validate::tests::rejects_crafted_jobs`, `validate::tests::performs_valid_jobs` (`validate_tests.rs`); `validate::tests::verbatim_paths_windows` | One table row per spec defect, each through `run` with its `os`; recorded spawns = 0, removals = 0; `snapshot` of the temp tree (incl. `outside/`) equal before / after; exit and code from `Exit` (PD-4); `UnderProfiles(<tmp>/profiles)` for Windows rows, `SameOwnerNonRoot` with the world's uid 501 overrides for macOS rows; the matcher cross-check against `contract::check_install_value`; a `TempIo` `create_new` fault with `PermissionDenied` on the result name → exit 4 `E_HELPER_RESULT_WRITE`, nothing written (SI-12). Valid rows: each op `done`, including a macOS `run_pkg_installer` row that reaches `done` on **every** leg (Windows and Linux included) because its run-directory chain is the temp `run_ancestors` + `run_base` marked `root_owned` (PD-5, §4.1.9). Windows-only row: real `std::fs::canonicalize` of a temp dir |
| SC-002 | `ops::tests::exact_argv` (`ops_tests.rs`) | Recorded argv vectors compared to literal vectors (the `ops_tests.rs` host-literal exemption, §4.8.1); `digest::sha256` of the file at the spawned path equals the job's; no spawn path under staging |
| SC-003 | `ops::tests::batch_of_three_middle_locked`, `batch_timeout_stops_the_rest`, `uninstall_only_job_has_no_run_dir`, `run_dir_failure_fails_installers`, `copy_error_is_named`; core `install::tests::batch_locked_middle_maps_each` (loopback) | Test classifier (99 → `Locked`); call-order log; fake-clock delta = `INNO_INSTALL_TIMEOUT`; one `create_run_dir`, one directory removal. `ops::tests::spawn_error_is_named` (SI-12): a `ScriptedRunner` whose `spawn` returns `Err` for op 1 of 2 → op 1 `failed E_HELPER_SPAWN`, op 2 runs and is `done`. `ops::tests::classify_sees_whole_bounded_log` (SI-28): a recording classifier receives every line of a log whose matching line is followed by 500 more lines (the result's `log_tail` holds only the last 40); a 3 MiB log hands over only its last MiB |
| SC-004 | `ops::tests::uninstall_polls_until_gone`, `uninstall_poll_times_out`, `uninstaller_never_exits` | `RemovePath` / `RemoveKey` events at 3 s; sleeps counted (6 / 240), checks 241, `seconds` 3.0 / 120.0; `Instant` around the test < 5 s (allowed in `_tests.rs`, §4.8.1). `uninstaller_never_exits` (PD-24): the spawned uninstaller's script `never_exits` → `wait(UNINSTALL_POLL_TIMEOUT)` `None` → `failed E_HELPER_UNINSTALL_TIMEOUT`, zero poll checks, the next op runs |
| SC-005 | `ops::tests::runs_the_copy_never_the_original`; `os::tests::open_source_refuses_links_and_fifos_unix`, `os::tests::open_source_refuses_link_windows`, `os::tests::read_bounded_stops_at_bound` (SI-25) | `TempIo` copy hooks over the production `open_regular_no_follow` (PD-6): rewrite after copy (spawn names the copy, `done`), rewrite during copy (`E_HELPER_HASH`, 0 spawns), link before open (`E_HELPER_SOURCE`), and on Unix a FIFO swapped in before the open (`E_HELPER_SOURCE`, the op returns in under 1 s of wall time, 0 spawns). The `os::tests` rows build `OsHelperIo` itself over a `TempDir` (§4.1.12) |
| SC-006 | `ops::tests::pkg_paths_classified`, `pkg_failure_paths`, `run_dir_removed_after_job`; `result::tests::log_tail_bounds` | Fake `pkgutil` stdout per argv; `volume` = temp base; real removals in the temp tree; `--forget` last / absent; tail bounds on literal byte inputs: 45 lines → the last 40; 45 lines interleaved with empty lines → the last 40 lines **counting** the empty ones (FR-003); a 600-byte line cut to ≤ 512 at a char boundary; invalid UTF-8 replaced |
| SC-007 | `roots::tests::binary_roots_ignore_job_and_settings`, `job::tests::no_field_adds_a_root`, `roots::tests::production_roots_windows` / `_macos` | `cli::main_with` with a recording `RunFn`; fixtures naming `C:\Windows`, `/etc`, a temp dir in every settings / job key; captured roots == `production()`; `job::KEYS` == FR-001's; a `roots` key refused; literal production values |
| SC-008 | `validate::tests::symlinked_parent_root_unix`, `validate::tests::symlinked_parent_root_windows` | `real/` + `link → real` (`std::os::unix::fs::symlink` / `std::os::windows::fs::symlink_dir`); macOS uses the raw `/var` temp spelling; the six table rows. The Windows row needs the symbolic-link privilege and fails with a named message without it, never skips (§4.1.12) |
| SC-009 | `install::tests::refused_prompt_changes_nothing`; vitest `refused permission shows nothing changed and keeps the actions` | `FakeElevation::refusing()`; `RecordingInstallIo` shows only the job write and its removal; a retry's asset-request count from the fixture server = 0; `jobs/` empty |
| SC-010 | `install::tests::one_launch_per_batch` | `WindowsDriver::run` / `MacosDriver::run` direct over staged artifacts (SI-20); one launch; `FakeElevation.jobs[0]` (the job bytes read at launch, §4.4) parsed back with `serde_json` holds exactly the three ops in order |
| SC-011 | `core_written_jobs_parse` (`crates/helper/tests/core_jobs.rs`) | `install::job::write` → helper `run` per op `os` (both values); hashes equal; `result::write` → `install::job::read_result`; a flipped byte each way fails; PD-11's code and timeout equality |
| SC-012 | `install::tests::drivers_per_os`, `gate_remove_basis`, `remove_elsewhere_refused`, `record_outside_sandbox_panics` | `FakeHostFacts` records under `<sandbox>\elsewhere\…`; `#[should_panic(expected = "outside the test sandbox")]` |
| SC-013 | `status::tests::actions_per_state` (extended) | The table over `(Os::Windows, X86_64)` and `(Os::Macos, Aarch64)`; refusals with zero asset requests and no launch |
| SC-014 | `install::tests::windows_install_update_remove`, `macos_install_update_remove` (loopback) | `for_command` / `remove_for_command` end to end with `os` explicit; `LiveHostFacts` + temp bundles; `status::build_with_os` afterwards; `FakeHostIo.calls` for `killall`. Both run in `cargo.all`, so the macOS pkg path reaches `done` on the Windows and Linux legs too (PD-5). `install::tests::job_hash_is_the_verified_digest` (loopback, cargo.all): after `verify_file` and before the batch, the test rewrites the staged artifact's bytes in the temp root; the launched job's `sha256` (the loopback keeps the job bytes it reads before calling `run`) equals `hex_lower(artifact.sha256())`, the helper reports `failed E_HELPER_HASH` with zero spawns, the action is `Helper { code: "E_HELPER_HASH" }` |
| SC-015 | `install::tests::launch_outcomes_map`, `pre_job_refusals`, `elevating_event_order`, `osascript_outcome_parses`, `windows_parameters_quote`; `windows_parameters_parse_back_windows` (`crates/core/tests/windows_quoting.rs`) | `FakeElevation` scripts per outcome (result bytes written by the script); the concurrent batch held on `Elevating` over a channel; `Fx::verified` outside staging (SI-13); `raw_arg` + `argv-echo`. `elevating_event_order` in two parts (SI-24): (1) `for_command` for one `Install` over the fixture server, with a recording progress sink that also records the `FakeElevation` launch as an event: every `Download` event, then exactly one `Elevating`, then the launch, in that order (a regression emitting `Elevating` before the download is red); (2) `WindowsDriver::run` and `MacosDriver::run` direct with two pre-verified installs: exactly one `Elevating`, before the single launch; plus an uninstall-only batch: one `Elevating`, no `Download` |
| SC-016 | `classify::tests::provisional_rule`; at the resume `classify::tests::recorded_locked_case`, `ops::tests::recorded_locked_case_through_runner`, `install::tests::recorded_locked_case_maps_to_locked`, `install::tests::unrecognised_log_falls_back` | Exit codes 0-8, 99; then the `tests/fixtures/helper/inno-locked/` replay through the production `classify_inno`: `recorded_locked_case` also asserts `classify::LOCKED_LINE == locked_line.txt` (SI-26) and has a variant with 500 rollback lines appended after the recorded line, classified over `log_lines` of the bounded tail (SI-28) |
| SC-017 to SC-020 | CI (§7.3); `cli::tests::ci_roots_flag_is_usage_error`, `cli::tests::ci_roots_file_is_read` (feature); `bundle sidecar name per host tuple`, `fixture inno script keeps the plugin directives` | `main_with` exit 2 on `--ci-roots` without the feature; ci-roots file in a temp dir |
| SC-021 | `injection_scanner_reports_planted_violations` and the `READER_PINS` test (existing names), `roster_and_hosts_absent_from_sources`, `fixture_names_confined` | Plants per §4.8.1 |
| SC-022 | `install::tests::job_files_written_and_removed`, `recover_removes_stale_jobs` | `FaultyInstallIo` `WriteNew` `StorageFull` → `E_INSTALL_FULL { step: job_written }`; planted files and a symlink |
| SC-023 | `helper_usage_exits_2` (`crates/helper/tests/usage.rs`); `cli::tests::malformed_hash_is_usage_error` (PD-25) | `CARGO_BIN_EXE_krate-manager-helper`, no arguments, `current_dir` a `TempDir`, its listing empty afterwards; the malformed-hash rows run in-process through `main_with` (exit 2, `RunFn` not called), never by spawning the binary with arguments (FR-106) |
| SC-024 | `install::tests::native_wiring_windows` / `_macos` | `native_env(…, &FakeElevation::refusing(), &FakeHostIo)`; one launch; `FakeElevation.jobs[0]` parsed back holds one op whose `op` is the OS's installer kind (`run_inno_installer` / `run_pkg_installer`) |
| SC-025 | vitest (`src/status.test.ts`), the twelve titles (SI-30 adds `macos remove confirmation names the uninstaller`); `install::tests::new_error_texts` | DOM assertions with injected `invoke` / `makeChannel` (Phase 9 rig); the rejection strings the vitest fixtures inject are FR-133's sentences, and `new_error_texts` proves the core's `Display` produces exactly those sentences (§3.2), so the pair covers core text and UI echo |
| SC-026 | lane commands (§7.4) | Zero warnings, no `--test-threads` |
| SC-027 | `validate::tests::run_dir_ancestors`, `validate::tests::run_dir_sddl`, `ops::tests::run_dir_refusals_spawn_nothing`; `os::tests::run_dir_mode_unix`, `os::tests::wait_timeout_leaves_child_unix` (SI-25); `ops::tests::temp_io_refuses_outside_base` | Pure `LstatFacts` rows; the SDDL constant; world uid / mode overrides; the real `create_run_dir` mode `0o700` and `OsRunner::wait`'s no-kill timeout (FR-042) over a `TempDir`; `temp_io_refuses_outside_base` (`#[should_panic(expected = "outside the test sandbox")]`): `TempIo::new(world, <tmp A>)` asked to `create_run_dir` under a second, unrelated `TempDir` B panics; B is a temp dir, so even a missing guard writes nothing outside the temp area (the row never aims at a production path, which on Linux would not even reach the run directory because `OsRoots::None` refuses every op first). The non-panicking companion rows in `ops_tests.rs` assert each other mutating method and `open_source` / `read_bounded` / `read_tail` outside `base` panic too, via `std::panic::catch_unwind` per method |
| SC-028 | `install::tests::launch_program_is_the_resolved_helper`, `helper_file_never_opened`, `helper_resolved_beside_exe` | Recorded `ElevatedCommand`s; `RecordingInstallIo` / `FakeHostIo` see no access to the helper path. `helper_resolved_beside_exe` (FR-090, PD-23) over a `TempDir` with `FsInstallIo`, every `Os` on every leg: `<d>/krate-manager[.exe]` with the helper file beside it → `Some(<d>/krate-manager-helper)` (`.exe` iff `Os::Windows`); helper absent → `None`; a directory of that name → `None`; `Os::Linux` with the file present → `None`; `exe = None` → `None`; a bare `krate-manager` (empty parent) with a helper in the test's temp dir → `None` (never the CWD or `PATH`) |
| SC-029 | `install::tests::timed_out_holds_guard` | One `FakeElevation` across batches 1-3; a fresh fake for the restart; a concurrent batch for `stale: false` |
| SC-030 | `install::tests::not_applied_detected` | `FakeElevation` returning a `done` result; `LiveHostFacts` and temp bundles set per row; the macOS install row that becomes `E_INSTALL_NOT_APPLIED` asserts `FakeHostIo.calls` holds exactly one `run` of `KILLALL_ARGV` (FR-077 keys on the helper's `done`, §4.2.3 step 9) |
| SC-031 | `elevated jobs are path-gated`, a `t.test` subtest of `manager-ci.yml keeps the Phase 4 shape` (`manager-ci-shape.test.js`, run by `feed-tools.yml` and the pipeline lane) | Shape checks + mutations (§4.8.4); the parent asserts the subtest ran; `feed-tools.yml`'s top-level plan stays `1..59` (§7.4 step 3 replays the check) |
| SC-032 | The build's final report and the absence checks | `classify::tests::provisional_rule` green; no `inno-locked/` content |

### 7.2 FRs without their own SC

FR-005 (SC-023 + `cli` tests), FR-022 (`clippy.toml`, SC-007), FR-024 (SC-018's `xattr -l` in CI; locally the
`injection.rs` rule of §4.8.1 / SI-29: no `xattr`, `chown`, ACL-setter or `com.apple.quarantine` token in the helper's
non-test sources, each with a plant in `injection_scanner_reports_planted_violations`; the run-dir DACL by SC-017's SDDL
assertion), FR-106 (the shape-test bans and mutations of §4.8.4, SI-29), FR-040 (SC-003's call order), FR-041 (SC-004; the `Instant` rule), FR-114 (log capture via
`test_support::install_capture` in `launch_outcomes_map`: no absolute path in an `info!` line), FR-120 (every Phase 9 name
keeps its verdict in the same `required tests ran`), FR-121 / FR-122 (`git diff --name-only` at close).

### 7.3 PENDING-CI rows (exact job and assertion)

| Row | Job | Assertion (`::notice` on pass, `::error` on fail) |
|---|---|---|
| macOS / Linux helper and core rows | `check (macos-latest)`, `check (ubuntu-24.04)`, step `cargo test` | `required tests ran`: every `cargo.all` + leg name exactly once |
| SC-017 | `helper-elevated (windows-latest)` | token elevated; recorder argv == SC-002's Inno argv, `current_exe` in `C:\Windows\Temp\run-*`; recorder SDDL: `P` among `D:` flags, exactly `(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)`, no `ID`; fixture installed under `$RUNNER_TEMP\real\root\VST3\Krate Audio\Fixture\Fixture.vst3`, `{fixture AppId}_is1` in HKLM 64-bit; uninstall `done`, tree and key gone, `seconds` annotated; link job: canonical accepted, linked `E_HELPER_SOURCE`; no `run-*` left; every `Start-Process` reported the helper's own exit code and a result file existed |
| SC-018 | `helper-elevated (macos-latest)` | four trees placed; raw `--only-dirs` listing annotated; structural dirs kept; fixture files gone; `pkgutil --pkgs` lacks `com.krateaudio.fixture`; `xattr -l` shows no `com.apple.quarantine`; preinstall record: `$1` annotated, parent uid 0, `drwx------`, `700`, under `/Library/Application Support/com.krateaudio.krate-manager-helper/`; link job as on Windows; no `run-*` left |
| SC-016 (measurement) | `inno-locked-file (windows-latest)` | every FR-060 value annotated; after M-1 FR-102a's three equalities against the committed `inno-locked/exit_code.txt`, `locked_line.txt` and `classes.json` (SI-26) |
| SC-019 | `check (*)` steps `ci-temp-roots absent`, `clippy ci-temp-roots`, `test ci-temp-roots` (log holds `cli::tests::ci_roots_file_is_read ... ok` once), `clippy manual-elevation example` | as FR-103, FR-103a, FR-105 |
| SC-020 | `check (windows-latest | macos-latest)` step `helper bundled`, `check (ubuntu-24.04)` `.deb` check, `node --test` + `required tests ran` (node list) | `7z l` entry beside `krate-manager.exe`, extracted SHA-256 == staged; `Contents/MacOS/krate-manager-helper` exists and `codesign verify` passes; `dpkg-deb -c` lists no helper |
| SC-031 | any push | a push touching none of FR-102b's paths shows both elevated jobs skipped; one touching `crates/helper/` runs both |

### 7.4 Local close procedure (Windows, this box)

1. `cargo fmt --all --check`; `cargo clippy --workspace --all-targets --locked -- -D warnings`; `cargo clippy -p
   krate-manager --all-targets --features dev-feed --locked -- -D warnings`; `cargo clippy -p krate-manager-helper
   --all-targets --features ci-temp-roots --locked -- -D warnings`; `cargo clippy -p krate-manager --example
   manual_elevation --features manual-elevation --locked -- -D warnings`; the clippy-guard run.
2. `cargo test --workspace --locked` (log to `F:/tmp/gate/`); `cargo test -p krate-manager-helper --features ci-temp-roots
   --locked` (separate log).
3. `npm run typecheck`, `npm run lint`, `npm test`, `node --test "scripts/*.test.js"`, `node --test --test-reporter=tap
   "tools/feed-contract/tests/*.test.js"` (from the repo root, output to `F:/tmp/gate/feed-tap.txt`) followed by the
   TAPCHECK script body of `feed-tools.yml:85-146` (the heredoc between `:84` and `:147`; its `process.env.CI` block is
   inert locally) run with `node -` over that file (copied verbatim into a scratch file
   under `F:/tmp/gate/`, never edited), so a changed top-level plan count or a missing top-level name is red locally;
   `node tools/lint-manager-version.js`.
4. WSL: `cargo clippy -p krate-manager-core -p krate-manager-helper --all-targets --locked -- -D warnings && cargo test -p
   krate-manager-core -p krate-manager-helper --locked` (SI-22).
5. `npm run bundle` is a main-loop lane (minutes; it writes `src-tauri/binaries/`, git-ignored), never a gate; never run
   the bundled helper.
6. `node tools/clean-test-output.js --apply`; `git status --porcelain` shows only intended files.
Cores: steps 1-3 run concurrently (independent target dirs are not needed: cargo serialises on the build lock, so run the
cargo commands in one chain and the npm / node commands beside it); the WSL step uses its own target dir and runs beside
them.

---

## 8. Build integration and gate lanes

### 8.1 Workspace and scripts

Workspace members unchanged (`core`, `helper`, `src-tauri`); `exclude` gains the two Rust fixtures (each with its own
committed `Cargo.lock`, no dependencies). `package.json` gains `"bundle": "node scripts/bundle.js"`. `.gitignore` gains
`/src-tauri/binaries/`. The gate lanes: the app lane (§7.4 steps 1-4) for every group that touches `apps/krate-manager/`;
the pipeline lane (`node --test tools/feed-contract/tests/`) for the group that edits `manager-ci-shape.test.js` and
`manager-ci.yml`.

### 8.2 `manager-ci.yml`

- `check` (all legs): steps per §4.8.4. `test ci-temp-roots` writes `$RUNNER_TEMP/cargo-test-ci-roots.txt` and asserts the
  one `cli::tests::ci_roots_file_is_read ... ok`. `node --test`: `node --test --test-reporter=tap "scripts/*.test.js" | tee
  "$RUNNER_TEMP/node-tap.txt"` (`set -o pipefail`). `helper bundled`: `case "$RUNNER_OS"` — Windows `7z l` / `7z e` of
  `target/release/bundle/nsis/*-setup.exe` into `$RUNNER_TEMP/nsis-helper/`, entry directory compared with
  `krate-manager.exe`'s, `sha256sum` equal to `src-tauri/binaries/krate-manager-helper-x86_64-pc-windows-msvc.exe`; macOS
  `test -f "…/Krate Manager.app/Contents/MacOS/krate-manager-helper"`; Linux `dpkg-deb -c` has no `krate-manager-helper`.
  `ci-temp-roots absent`: `cargo tree -p krate-manager-helper -e features --locked` without `ci-temp-roots`; `grep -cF --
  '--ci-roots' crates/helper/src/cli.rs` ≥ 1; Windows and macOS: `grep -acF -- '--ci-roots'` = 0 in the extracted /
  bundled helper and the staged sidecar. Linux: no helper binary exists to scan (PD-20 builds none; the workspace has no
  `default-members`, `Cargo.toml:1-4`, and the check job builds only debug test binaries), so the Linux leg asserts the
  `cargo tree` and `cli.rs` counts only and prints `::notice::no Linux helper binary is built (D18)`; it never greps a
  path that does not exist.
- `changes` (PD-21): `ubuntu-24.04`, `permissions: contents: read`, outputs `elevated`.
- `helper-elevated` (`needs: changes`, gated; matrix `macos-latest`, `windows-latest`; `contents: read`): checkout,
  setup-node, Rust toolchain, rust-cache; `cargo build -p krate-manager-helper --release --locked --features
  ci-temp-roots`; Windows: build `argv-recorder` (`ARGV_RECORDER_OUT` compile-time path under `$RUNNER_TEMP`) and
  `locked-dll` (version 1.0.0), compile `fixture.iss` with the Inno action (`options: /DVersion=1.0.0
  /DRoot=${{ runner.temp }}\real\root /DPayload=…`), then `shell: pwsh` steps: elevation check, temp roots + ci-roots
  files (node), staging copies, `write-test-job.js` (which writes verbatim `\\?\` `file` values and prints the verbatim
  job path, §4.6), `Start-Process -FilePath $helper -ArgumentList '--job', $job, '--job-sha256', $sha, '--ci-roots',
  $roots -Wait -PassThru` with `$job` the printed verbatim path unchanged, assertions (SC-017); macOS: stage +
  `pkgbuild --scripts` fixture, `sudo "$helper" --job … --job-sha256 … --ci-roots …` steps under bash, assertions
  (SC-018), and an `if: always()` step `sudo pkgutil --forget com.krateaudio.fixture || true` (FR-111).
- `inno-locked-file` (`windows-latest`, gated): two `locked-dll` builds, two fixture compiles (1.0.0 / 1.0.1), the FR-060 /
  FR-102 measurement in `pwsh` steps, every value a `::notice`; after M-1 the FR-102a equalities, read from the committed
  `tests/fixtures/helper/inno-locked/exit_code.txt`, `locked_line.txt` (a substring check of the measured log) and
  `classes.json` (`dll`, `unlocked`), each mismatch an `::error` (SI-26).

---

## 9. Risks and mitigations

| Id | Risk | Mitigation |
|---|---|---|
| R-1 | **Windows file locks while a DAW runs**: the Inno behaviour under `/NOCLOSEAPPLICATIONS` is unmeasured; it may skip, schedule a reboot rename, hang on a suppressed box, or fail | The `inno-locked-file` measurement (M-1) and the build fence (FR-063); until then every non-zero exit is `E_INSTALL_INSTALLER_FAILED { exit_code }`; a hang is bounded by 600 s with the stale hold (FR-078a); FR-076a turns a silent skip into `E_INSTALL_NOT_APPLIED` at the `moduleinfo.json` level |
| R-2 | **Admin prompts**: UAC consent may open behind the window (no owner `hwnd`); macOS password dialog; Smart App Control blocks the unsigned helper; a DAW holding the osascript port | Phase 12's user-performed checks; the named outcomes `E_INSTALL_REFUSED` / `E_INSTALL_ELEVATION { E_ELEVATION_SHELLEXECUTE | E_ELEVATION_OSASCRIPT }`; one prompt per batch |
| R-3 | **WebKitGTK quirks on Linux** | Phase 10 adds no webview feature; Linux keeps Phase 9's driver; the new UI branches render only on `windows` / `macos` views; vitest runs on jsdom on every leg |
| R-4 | **Offline**: install / update need the artifact; the helper needs no network | A cached verified artifact is reused (Phase 7); Remove works from the cached feed and the host record; the helper never opens a socket (no HTTP stack in its graph, D1) |
| R-5 | A `pkgutil --files` listing that holds `.` or the structural ancestors | D16 tolerates the ancestors; a `.` line refuses the whole op (FR-033); SC-018 annotates the raw listing, so a red run shows which case it is and the rule is re-ruled from evidence |
| R-6 | `node --test` TAP output changes between Node versions | PD-27's lenient regex; Node pinned to 24 in CI (`manager-ci.yml:42-44`) |
| R-7 | macOS dialog open past the ceiling → `TimedOut`, then a stale hold until restart if the user cancels | PD-18; the hold's text says "an installer is still running"; restart clears it (FR-078a); the ceiling for one item is ≥ 12 min |
| R-8 | The elevated result file is owned by Administrators / root | The core only reads and unlinks it (inherited user ACL / the user's own `jobs/` directory); an unlink failure is a `warn!`, and `recover` retries at the next start |
| R-9 | `C:\Windows\Temp` lets users create entries, so a predicted `run-*` name can be squatted | `CreateDirectoryW` fails on an existing name → `E_HELPER_RUN_DIR` (fail closed, no reuse, D7); the DACL is applied atomically at creation; names are `RandomState`-derived (PD-19) |
| R-10 | Mutual dev-dependency (PD-8) confuses a future `cargo` or a tool (`cargo tree`, the version lint) | Probed on cargo 1.99.1; `core dependency tree` uses `-e normal` (`manager-ci.yml:126`), which excludes dev edges; the version lint reads members only |
| R-11 | A timed-out installer still runs while the helper removes its run directory | The removal fails and is ignored (FR-043); the directory stays admin-only; later batches are refused by the stale hold |
| R-12 | The NSIS bundler or the macOS ad-hoc signer treats the sidecar differently from the docs | `helper bundled` asserts placement and bytes (Windows) and `codesign verify` (macOS) on every push |
| R-13 | The hand-defined `O_NONBLOCK` / `O_NOFOLLOW` values (PD-6) are wrong for a target | Values copied from `libc-0.2.191` per target (Apple, Linux x86_64, Linux aarch64; they differ: Linux aarch64 `O_NOFOLLOW` is `0x8000`, x86_64 `0x20000`); any other Unix target is a `compile_error!`; `os::tests::open_source_refuses_links_and_fifos_unix` runs on `check (macos-latest)`, `check (ubuntu-24.04)` and WSL, so a wrong value (a link followed, a FIFO open blocking) is red there |
| R-14 | Windows symlink rows need the symbolic-link privilege | Held on `windows-latest` (administrator) and on this box (Developer Mode, measured 2026-11-01); without it the rows fail with a named message, never skip (§4.1.12), so `required tests ran` cannot pass on a silent skip |

---

## 10. Precondition table (for the user)

| When | What the user does | What it unblocks |
|---|---|---|
| Before tasks | Rule SI-1 to SI-23 into the spec (the main loop edits) | `tasks.md` |
| After the build reports "stopped at the M-1 fence" | Commit and push (the user only); optionally `workflow_dispatch` `manager-ci.yml` | `inno-locked-file` and `helper-elevated` run; their annotations become M-1 and SC-017 / SC-018 evidence |
| At the resume | Nothing beyond the push; the build writes M-1, the fixtures and the measured rule; a silent skip or hang is re-ruled with the user (FR-062) | Phase 10 close |

---

## Review notes

Revision 1, 2026-11-01: first draft.

Revision 2, 2026-11-01: review round 1 (27 issues: 12 major, 15 minor). Every issue was **applied**; none was rejected
and no threshold was relaxed. Where the plan chose one of the suggested resolutions, the choice and why:

- feed-tools plan count: `elevated jobs are path-gated` is a `t.test` subtest with a ran-flag assertion (§4.8.4), not a
  new top-level test, so `feed-tools.yml` stays frozen (FR-122 unchanged); the TAPCHECK is replayed locally (§7.4 step 3).
- `elevating_event_order`: split as suggested; the spec's two-action `Download` wording is amended by SI-24.
- Real seams in tests: construction ban and `Instant` rule scoped to non-test sources (SI-25), with `os_tests.rs` rows.
  `OsRunner::wait` is proven on Unix only (a sleeping child via `/bin/sleep`); a Windows spawn row would need a second
  test-only binary in the helper crate and the no-kill logic is the same `try_wait` poll on both, so it is left to
  SC-017's elevated run.
- Run-directory ancestors: `MacosRoots.run_ancestors` (derived from the roots) **and** `World::root_owned`, because the
  temp chain is real directories with the fake's uid (Windows has none).
- `resolve_elevation` test: the pure rule moved into core (`install::helper_beside`), because `src-tauri`'s library is
  `test = false` (`src-tauri/Cargo.toml:10-13`) and a src-tauri test would never run; the suggested CWD row is a bare-name
  row instead of a `chdir`, since changing the process CWD races every parallel test. No separate `cargo.linux` row: the
  OS is a value, so the Linux branch runs on every leg.
- Malformed `--job-sha256`: tested in-process (`cli::tests::malformed_hash_is_usage_error`) rather than as a
  `helper_usage_exits_2` row, so `check` still runs the binary only without arguments (FR-106).
- `TempIo` sandbox `#[should_panic]` row: aimed at a second temp dir, not at production `AllowedRoots`, so a missing guard
  still writes nothing real, and the row panics on the Linux leg too (where production roots refuse every op first).
- Manual example: both suggested fail-closed ops adopted (SI-14 rewritten); the example also refuses a non-empty argument
  and removes only what it recorded.
- FIFO / link open: `O_NONBLOCK | O_NOFOLLOW` with per-target constants from `libc-0.2.191` source (the suggested Linux
  `0o400000` is x86_64 only; aarch64 is `0x8000`), replacing PD-6's "no `O_NOFOLLOW` constant" workaround.
- Scripts: ESM with `.js` kept (no glob changes), plus the `tsconfig.node.json` task run on one script first.
- Windows symlink rows: fail with a named message when the privilege is missing (no skip path at all), rather than a
  junction fallback, since both the runner and this box hold the privilege (measured).
- Fixture names: `inno-fixture.test.js` reads both files' `#define AppName` (`setup.iss:16`), so neither name appears in
  `scripts/`; `fixture_names_confined` needs no exemption.

Spec edits this revision asks the main loop for: SI-24 to SI-31 (§0.2), plus the widened SI-14 and SI-23.
