# Feature Specification: Krate Manager Phase 4 — Scaffold, Platform Abstraction and the Manager's CI

**Spec slug:** `krate-manager-phase4`
**Roadmap:** `specs/Krate-manager-roadmap.md` → Phase 4 (lines 463-519); Decisions log Q1 (line 24), Q3 (line 26),
Q5 (line 28), Q6 (line 29), ROSTER (line 33); Architecture Overview "Processes" (lines 104-110), per-OS table (lines
114-118), "Where state lives" (lines 158-163); Key design decisions 1 and 5 (lines 167-169, 177-181); Reuse Inventory
row "Probe script" (line 208); Repository Layout (lines 213-250); Dependency Graph (lines 916, 933); Cross-Cutting
Constraints (lines 961-1002)
**Depends on:** nothing in Part B. Phase 4 "precedes every Part B phase" (roadmap line 916; edges 4→5 … 4→11, line
933). It reads no Phase 1-3 output except the feed-tools lane it extends (`.github/workflows/feed-tools.yml`).
**Consumed by:** Phase 5 (feed URL injection, the state dir for the feed cache), Phase 6 (`Platform`, scan roots),
Phase 7 (staging dir), Phase 8 (`temp_roots()`, macOS temp canonicalization), Phase 9 (the settings model, roadmap
lines 655-658), Phase 10 (state dir / `staging/` as the helper's only source root, roadmap lines 719-726), Phase 11
(`tauri.conf.json`, `version.json`, the version lint), Phase 12 (the settings screen over this model).
**Status:** DRAFT — specification only, no implementation
**Date:** 2026-10-30 · verified against `feat/krate-manager` at `10593783` ("feat(krate-manager): Phase 3 feed
pipeline", `git log --oneline -3`); `apps/` does not exist yet (`ls apps` this session: no such directory)

---

## Overview

Phase 4 creates the Krate Manager app under `apps/krate-manager/` as a Tauri 2 project scaffolded from
`create-tauri-app`'s vanilla TypeScript template, with a Cargo workspace of three crates (`crates/core`,
`crates/helper`, `src-tauri`), and gives it the three things every later phase stands on: (1) a `Platform`
abstraction in the Tauri-free core with a Windows, a macOS and a Linux implementation, and a `Roots` value (plug-in
roots, preset roots, state dir, staging dir) that is always passed in, guarded mechanically so no test can reach a real
system folder; (2) a versioned, atomically persisted settings model in the injected state dir (the Linux plug-in root
and the Linux graphics toggle's stored value); (3) process-level plumbing: a single-instance lock on the state dir,
file logging, a live-reassemblable application state and a release-excluded dev-feed override (FR-100 to FR-107);
(4) the manager's own CI, `.github/workflows/manager-ci.yml`, running
fmt, clippy, cargo test, tsc, the linter, vitest and an unsigned `tauri build` on Windows, macOS and Linux, with the
macOS leg turning the probes' Info.plist and ad-hoc-signature results into regression gates. It also fixes the app's
security posture (a capability that grants the webview only the app's own command, a CSP with no remote origin) and
its version source (`apps/krate-manager/version.json`, checked by `tools/lint-manager-version.js`).

The app this phase builds shows one window with the manager's version and nothing else (a second instance shows only
"Krate Manager is already running", FR-101). It fetches no feed, reads no plug-in folder, writes nothing outside its
state dir (the Tauri identifier dir, D7: settings, lock file and logs), and has no release pipeline (roadmap line 519).

---

## Scope

**In scope (roadmap Phase 4 components, lines 467-501):**

1. The Tauri 2 app from `create-tauri-app` (Vite + TypeScript); this spec picks the UI framework and the linter
   (roadmap lines 468-469; D1, D2).
2. The Cargo workspace (`crates/core`, `crates/helper`, `src-tauri`) with an empty but wired command surface (line
   470; D11).
3. `Platform` in `core` with Windows, macOS and Linux implementations behind `cfg`, the `Roots` value, and the three
   mechanical guards (lines 471-481; D6-D9).
4. The settings model: versioned schema, atomic persistence, per-OS defaults, injected in tests; no UI (lines
   482-486; D10).
5. The capability file and the CSP (line 487; D11-D13).
6. `tauri.conf.json`: `signingIdentity: "-"`, `minimumSystemVersion: "15.0"`, NSIS `installMode: "currentUser"`, no
   `LSFileQuarantineEnabled` (lines 488-490; D5, D15).
7. The version source and `tools/lint-manager-version.js`, run in `manager-ci.yml`, tested in `feed-tools.yml`
   (lines 491-494; D18).
8. `.github/workflows/manager-ci.yml` on three legs with the macOS regression gates (lines 495-501; D16, D19, D21).
9. Clarified in the 2026-10-30 session (not roadmap bullets): the single-instance `StateLock` (D22), the `log` facade
   and the rotating file sink (D23), the managed `SharedState` and `reassemble` with the folded `Configured` Linux root
   (D17, D8), the `save` refusal over a newer settings file (D10), and the `dev-feed` cargo feature (D24).

**Non-goals (owned elsewhere):**

| Not in Phase 4 | Owner |
|---|---|
| Fetching or parsing `versions.json`, the offline cache, SemVer and platform-key resolution, the HTTP client, the fixture HTTP server | Phase 5 (roadmap lines 523-546) |
| Reading a plug-in folder, `moduleinfo.json`, the registry, `pkgutil`; the status screen | Phase 6 (lines 548-583) |
| Downloading, hashing, signature verification, the embedded public key | Phase 7 (lines 585-616) |
| Extraction | Phase 8 |
| Writing into any plug-in or preset root, install manifests, the swap journal | Phase 9 |
| The helper's operation set, the job file, the elevation launchers, `bundle.externalBin` | Phase 10 (lines 689-787) |
| `tauri-plugin-updater`, `createUpdaterArtifacts`, `release-manager.yml`, the universal macOS build, the manager's pkg / AppImage / `.deb` release packaging, the `ubuntu:22.04` container and the glibc floor | Phase 11 (lines 789-854) |
| The settings screen, the Linux NVIDIA/WebKitGTK workaround taking effect, first-launch docs, the landing-page card | Phase 12 (lines 856-905) |
| Q6's Windows floor, "Windows 10 22H2 and Windows 11 x64" (roadmap line 29, Phase 4 bullet lines 488-489) | Not expressible in `tauri.conf.json`: `WindowsConfig` / `NsisConfig` have no minimum-OS key (only `minimumWebview2Version`; schema at `tauri-v2.12.5`). Declared as text in Phase 12's first-launch documentation (roadmap lines 882-886), which the plan adds to that phase's bullet. The x64 half is already what `windows-latest` builds (`Krate Manager_<ver>_x64-setup.exe`, FR-073) |
| Q6's Linux floor, the glibc of the `ubuntu:22.04` build | Phase 11 (roadmap lines 821-822, `objdump -T` on the built binary) |
| The HTTP client base as an injected `AppContext` field | Phase 5 (roadmap line 526: "the injected URL"; the client's shape is that phase's), see D17 |
| The elevation launcher as an injected `AppContext` field | Phase 10 (roadmap lines 729-731: "Launchers behind one trait"), see D17 |
| App icon artwork | No roadmap phase names it; the scaffold keeps the template's icon set (`templates/_base_/src-tauri/icons/`) |
| Any plugin source, installer, `ci.yml`, `release.yml`, `feed.yml`, the feed contracts | Never in this phase (FR-090) |

---

## Existing repo pieces (verified this session)

| File (lines) | What it is | What Phase 4 does with it |
|---|---|---|
| `specs/Krate-manager-roadmap.md:213-250` | Repository Layout: `apps/krate-manager/` with `Cargo.toml` (workspace), `package.json`, `tsconfig.json` (strict), `version.json`, `CHANGELOG.md`, `src/`, `src-tauri/`, `crates/core/`, `crates/helper/`, `tests/fixtures/`, plus `.github/workflows/manager-ci.yml` and `tools/lint-manager-version.js` | ✅ Followed exactly (FR-001). |
| `specs/Krate-manager-roadmap.md:246-250` | `apps/` is outside `plugins/`, so the roster lints and `ci.yml`'s path filter do not fire on manager-only changes | ✅ Confirmed: `tools/lint-plugin-roster.js:35,69` and `tools/lint-plugin-editor-baseline.js:42,129` read only `plugins/`; `.github/workflows/ci.yml:4-23` lists `dsp/**`, `plugins/**`, `tests/**`, `extern/**`, `CMakeLists.txt`, `CMakePresets.json`, `ci.yml` and no `apps/**`. |
| `.gitignore:92-94` | `node_modules/`, `/package.json`, `/package-lock.json` (root-anchored; the root `package.json` is the user's local file) | ✅ Unchanged. `git check-ignore` this session: `apps/krate-manager/package.json` and `package-lock.json` are **not** ignored; `apps/krate-manager/node_modules/` is; `target/` and `dist/` are not ignored anywhere, so the app gets its own `.gitignore` (FR-003). |
| `.github/workflows/feed-tools.yml:3-34` | Push / pull_request path filters (the Phase 1-3 tools, `tools/feed-contract/**`, `release.yml`, `backfill-sign.yml`, `feed.yml`) and `workflow_dispatch` | 🔶 Adds `tools/lint-manager-version.js`, `.github/workflows/manager-ci.yml`, `apps/krate-manager/package.json` (FR-081). |
| `.github/workflows/feed-tools.yml:40-53` | `node-test` on `ubuntu-latest`, `macos-latest`, `windows-latest`; `actions/checkout@v6`, `actions/setup-node@v4` with Node `'24'` | ✅ The action versions and Node major `manager-ci.yml` uses (FR-070). |
| `.github/workflows/feed-tools.yml:71-113` | `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js"`, TAP check `PLAN = "1..57"` (`:78`) and the required `NAMES` list (`:79-113`) | 🔶 `PLAN` becomes `1..59` and `NAMES` gains this phase's two top-level tests (FR-081). |
| `.github/workflows/feed-tools.yml:142-144` | "Checkout unchanged": `test -z "$(git status --porcelain)"` | ✅ The pattern `manager-ci.yml` repeats (FR-075). |
| `tools/feed-contract/tests/hygiene.test.js:2`, `:34-53`, `:74-81` | Every test file's first statement is `require('./helpers/write-guard.js');`; `scannedFiles()` (the named tool list plus every `tests/*.test.js`) is scanned for network modules, `fetch(` and `child_process`; `CHILD_EXEMPT` | 🔶 `tools/lint-manager-version.js` joins the `single` list (FR-082); the two new test files are picked up by the glob and follow the first-statement rule. Neither is child-exempt. |
| `tools/lint-ieee-flag-sets.js:1-17` | The repo's lint form: `#!/usr/bin/env node`, a header with WHY, usage and exit codes (0 clean, 1 violations, 2 internal error) | ✅ The form `tools/lint-manager-version.js` follows (FR-060). |
| `plugins/membrum/version.json`, `plugins/vorago/version.json` | `{version, name, description, publisher, url, copyright[, preset_subdir]}` | ✅ The shape `apps/krate-manager/version.json` borrows its first two keys from (FR-061). |
| `cmake/KratePlugin.cmake:35-48` | `krate_plugin_read_version`: `version.json` is the single version source, split into major/minor/patch | ✅ The precedent Q1 names ("like `plugins/<p>/version.json`", roadmap line 219). No CMake is involved here. |
| `plugins/vorago/CHANGELOG.md:1-8` | Keep a Changelog 1.1.0 header, SemVer, `## [x.y.z] - date` | ✅ The format of `apps/krate-manager/CHANGELOG.md` (FR-062). |
| `plugins/shared/src/platform/preset_paths.cpp:8-31`, `:33-51`, `:53-80` | User presets `%USERPROFILE%` / `$HOME` + `Documents/Krate Audio/<P>`; factory presets `%PROGRAMDATA%\Krate Audio\<P>`, `/Library/Application Support/Krate Audio/<P>`, Linux `/usr/share/krate-audio/<lower>`; plugin settings `%APPDATA%\Krate Audio\<P>`, `~/Library/Application Support/Krate Audio/<P>`, `$XDG_CONFIG_HOME` or `~/.config` + `krate-audio/<P>` | ✅ The preset roots `default_roots()` computes come from here (D8). The state dir does not follow the plugins' settings naming: it is the Tauri identifier dir (D7). Not modified. |
| `specs/krate-manager/probes/macos-gatekeeper-probe.js:135-141` | Q6: `plutil -p <app>/Contents/Info.plist` has no `LSFileQuarantineEnabled`; `codesign --verify --deep --strict -v`; `codesign -dv` shows `Signature=adhoc` | 🔶 Reused as `manager-ci` macOS steps (roadmap line 208; FR-074). The probe file is not modified. |
| `specs/krate-manager/probes/results.md:11`, `:17`, `:18` | The bundler writes no `LSFileQuarantineEnabled`; `codesign --verify --deep --strict` passes on the ad-hoc Tauri bundle (tauri-cli 2.12.4, `signingIdentity: "-"`) on Sequoia and Tahoe; a quarantined ad-hoc app is blocked at launch | ✅ The facts the macOS gates freeze. |
| `.github/workflows/ci.yml:41-43` (plan SI-5) | `concurrency: group: ${{ github.workflow }}-${{ github.ref }}`, `cancel-in-progress: true` | ✅ Pattern for `manager-ci.yml` (FR-070). `ci.yml` is not modified. |
| `tools/lint-serial-test-runs.js:17-18`, `:105-113` | Scans `.claude/{workflows,agents,skills}`, the root, `dsp/` and `plugins/*/CLAUDE.md` for serial test runs | ✅ Does not reach `apps/` or `.github/workflows/`; the serial-flag ban for the manager is FR-080's shape test instead. |
| `tools/gen-specs-index.js:1-8`; `specs/INDEX.md:202-204` | Generates `specs/INDEX.md`; `--check` runs in `ci.yml:172`; Phases 1-3 are listed | 🔶 `specs/INDEX.md` is regenerated with this phase's row (FR-091). `node tools/gen-specs-index.js --check` this session: up to date (before this file existed). |

---

## External facts used (verified this session)

| Fact | Source |
|---|---|
| Current versions: `tauri` 2.12.5 (2026-10-21, `rust_version` 1.90), `tauri-build` 2.7.4, `tauri-cli` 2.12.4, `tauri-bundler` 2.10.4, `tauri-utils` 2.10.2 (`tauri` 2.12.5 requires `~2.10.2`), `serde` 1.0.229, `serde_json` 1.0.154, `tempfile` 3.27.0, `thiserror` 2.0.21 | crates.io API `https://crates.io/api/v1/crates/<name>` and `/crates/tauri/2.12.5/dependencies`, fetched 2026-10-30 |
| npm: `@tauri-apps/cli` 2.12.4, `@tauri-apps/api` 2.12.4 (exports `./*`, so `@tauri-apps/api/mocks`), `vite` 8.3.5 (engines `^20.19.0 \|\| >=22.12.0`), `vitest` 5.0.5 (engines `^22.12.0 \|\| ^24.0.0 \|\| >=26.0.0`; peer `vite ^6.4.0 \|\| ^7.0.0 \|\| ^8.0.0`), `typescript` latest 7.0.2 (2026-07-08) and 6.0.3 (2026-04-16), `eslint` 10.14.0, `typescript-eslint` 8.72.1 (peer `typescript >=4.8.4 <6.1.0`, `eslint ^8.57.0 \|\| ^9.0.0 \|\| ^10.0.0`), `@eslint/js` 10.0.1, `jsdom` 30.1.3 (engines `^22.22.2 \|\| ^24.15.0 \|\| >=26.0.0`) and 29.1.1 (engines `^20.19.0 \|\| ^22.13.0 \|\| >=24.0.0`), `@types/node` 26.6.6 | `npm view <pkg> version engines peerDependencies time`, 2026-10-30 |
| This box: Node v24.7.0, npm 11.10.0, rustc / cargo 1.99.1 (`rustc 1.99.1 (58cf8f9c3 2026-10-13)`, `cargo 1.99.1 (5f94df478 2026-08-27)`, stable-x86_64-pc-windows-msvc), clippy 0.1.99 (recorded by tasks T001, 2026-10-30; plan SI-6: D4's `rustup update stable` has run, SC-001's "after `rustup update stable`" stays the rule); no tauri CLI installed. Current Rust stable is 1.99.1 (2026-10-13) | `node --version`, `rustup show`, `cargo clippy --version`; `https://static.rust-lang.org/dist/channel-rust-stable.toml` (date 2026-10-15) |
| `create-tauri-app` 4.8.0 (crate) / 4.7.5 (npm) offers `template-vanilla-ts` among `angular, preact(-ts), react(-ts), solid(-ts), svelte(-ts), vanilla(-ts), vue(-ts)` and Rust templates. At `dev` commit `c6f5cf68` (2026-10-26) the v2 base generates `src-tauri/{Cargo.toml, build.rs, tauri.conf.json, capabilities/default.json, icons/, src/main.rs, src/lib.rs, _gitignore}`; `tauri.conf.json` has `"security": {"csp": null}` and `"bundle": {"targets": "all"}`; `capabilities/default.json` grants `core:default` and `opener:default` to window `main`; `Cargo.toml` uses `edition = "2024"`, a `[lib]` with `crate-type = ["staticlib", "cdylib", "rlib"]`, `tauri-plugin-opener`; `src-tauri/_gitignore` ignores `/target/` and `/gen/schemas`; the vanilla-ts `package.json` pins `"typescript": "~6.0.3"` and `"vite": "^8.0.16"`; `tsconfig.json` has `strict`, `noUnusedLocals`, `noUnusedParameters`, `noFallthroughCasesInSwitch`, `include: ["src"]`; `vite.config.ts` carries `// @ts-expect-error type error without @types/node package`; `index.html` links `https://vite.dev`, `https://tauri.app` | `gh api repos/tauri-apps/create-tauri-app/contents/templates/...?ref=c6f5cf68942f187ba7bb738a49ee505c28e6ca5a`; https://v2.tauri.app/start/create-project/ (tauri_feasibility.md Q7 line 203) |
| App commands registered with `invoke_handler` "are allowed to be used by all the windows and webviews of the app"; to restrict them use `AppManifest::commands` in `build.rs` | https://v2.tauri.app/security/capabilities/ |
| `AppManifest::commands(&'static [&'static str])` autogenerates `allow-$command` / `deny-$command`; `Attributes::app_manifest(AppManifest)`, `try_build(Attributes)` | `tauri-build` 2.7.4 `src/acl.rs:91-120`, `src/lib.rs:466-552`, `:652` (tag `tauri-build-v2.7.4`) |
| The generated identifier slugifies `_` to `-`: `identifier = "allow-{slugified_command}"` | `tauri-utils` 2.10.2 `src/acl/build.rs:306-313` (tag `tauri-utils-v2.10.2`) |
| With an app ACL manifest, every app command is ACL-checked; the channel fetch command is exempt: `if (plugin_command.is_some() \|\| has_app_acl_manifest \|\| !is_local) && request.cmd != FETCH_CHANNEL_DATA_COMMAND && invoke.acl.is_none()` → reject | `tauri` 2.12.5 `crates/tauri/src/webview/mod.rs:2076-2108`; `FETCH_CHANNEL_DATA_COMMAND = "plugin:__TAURI_CHANNEL__\|fetch"` at `crates/tauri/src/ipc/channel.rs:31` (tag `tauri-v2.12.5`) |
| `core:default` adds `core:app:default`, `core:event:default`, `core:image:default`, `core:menu:default`, `core:path:default`, `core:resources:default`, `core:tray:default`, `core:webview:default`, `core:window:default` | https://v2.tauri.app/reference/acl/core-permissions/ |
| `app.security.csp`: "injected on all HTML files on the built application"; if `devCsp` is unset it also applies in development; Tauri appends nonces and hashes for its own scripts and styles at build time; `capabilities` default: all files in `./capabilities/` | https://v2.tauri.app/reference/config/ ; https://v2.tauri.app/security/csp/ ("connect-src ipc: http://ipc.localhost"; "Avoid loading remote content such as scripts served over a CDN") |
| Config keys: `bundle.macOS.signingIdentity` (`string \| null`), `bundle.macOS.minimumSystemVersion` (default `"10.13"`), `bundle.windows.nsis.installMode` (default `"currentUser"`: "Install the app by default in a directory that doesn't require Administrator access"), `bundle.targets` (default `"all"`; `deb, rpm, appimage, msi, nsis, app, dmg`), `identifier` ("only alphanumeric characters (A-Z, a-z, and 0-9), hyphens (-), and periods (.)") | https://v2.tauri.app/reference/config/ |
| Platform config files `tauri.windows.conf.json`, `tauri.macos.conf.json`, `tauri.linux.conf.json` are merged with JSON Merge Patch (RFC 7396); arrays are replaced whole | https://v2.tauri.app/develop/configuration-files/ |
| `generate_context!` panics "The `frontendDist` configuration is set to `{path:?}` but this path doesn't exist" only outside dev mode with a `devUrl`; dev mode is `cfg!(not(feature = "custom-protocol"))` | `tauri-codegen` `src/context.rs:175-190`, `tauri-macros` `src/context.rs:155` (tag `tauri-v2.12.5`) |
| In release (non-dev) builds `tauri-build` emits `cargo:rustc-env=MACOSX_DEPLOYMENT_TARGET=<minimumSystemVersion>` | `tauri-build` 2.7.4 `src/lib.rs:779-782` |
| The NSIS bundler downloads `nsis-3.13.zip` and `nsis_tauri_utils.dll` from GitHub (SHA-1 pinned) into `dirs::cache_dir()/tauri` | `tauri-bundler` 2.10.4 `src/bundle/windows/nsis/mod.rs:38-44`, `:82-90` (tag `tauri-bundler-v2.10.4`) |
| `tauri::plugin::Builder::on_navigation(Fn(&Webview<R>, &Url) -> bool)` (`Url` = `url::Url`, lib.rs:92) | `tauri` 2.12.5 `crates/tauri/src/plugin.rs:120`, `:459` |
| `@tauri-apps/api/mocks`: `mockIPC((cmd, args) => …)`, `clearMocks()`; examples use Vitest with jsdom | https://v2.tauri.app/develop/tests/mocking/ |
| Debian/Ubuntu prerequisites: `libwebkit2gtk-4.1-dev build-essential curl wget file libxdo-dev libssl-dev libayatana-appindicator3-dev librsvg2-dev`; the GitHub pipeline page's `ubuntu-22.04` job uses `libwebkit2gtk-4.1-dev libappindicator3-dev librsvg2-dev patchelf xdg-utils` | https://v2.tauri.app/start/prerequisites/ ; https://v2.tauri.app/distribute/pipelines/github/ |
| Runner labels: `ubuntu-latest` = Ubuntu 24.04 (also `ubuntu-24.04`; `ubuntu-26.04` exists), `macos-latest` = macOS 26 arm64, `windows-latest` = Windows Server 2025 with VS 2026; "-latest" migrations take 1-2 months | `actions/runner-images` `README.md` lines 23-37, 95-97 (fetched 2026-10-30) |
| Hosted runners: Linux and macOS use passwordless `sudo`; Windows VMs run "as administrators with User Account Control (UAC) disabled" | tauri_plist_quarantine_and_runners.md lines 179-180 (docs.github.com, github-hosted-runners) |
| `clippy.toml` is searched from `CLIPPY_CONF_DIR`, else `CARGO_MANIFEST_DIR`, else the current directory, then each parent up to the root | https://doc.rust-lang.org/clippy/configuration.html |
| `disallowed-methods` entries: a path string or a table `{path, reason, replacement, allow-invalid}`; the lint `disallowed_methods` is warn-by-default and "will only trigger if methods are defined in the clippy.toml file" | https://doc.rust-lang.org/clippy/lint_configuration.html ; `cargo clippy --explain disallowed_methods` (clippy 0.1.98) |
| An unresolvable `disallowed-methods` path is **not** a build failure: a missing item in a resolvable crate (`std::fs::nonexistent_fn`) gives only `warning: … does not refer to a reachable function` and the build finishes with exit 0 under `-D warnings`; a path whose first segment is a crate outside the compiled crate's dependency graph gives no diagnostic at all. So `allow-invalid = false` is not a detector; a mistyped path is caught only by the unfulfilled `#[expect]` sites and by the clippy-guard fixture | Challenge review of this spec, clippy 0.1.98 on this box (reported 2026-10-30) |
| Resolution that does work (clippy 0.1.98, scratch workspace run this session, then deleted): an inherent-method path of the crate being linted (`kma::roots::Roots::new` called as `crate::roots::Roots::new` inside `kma`), the same path from a dependent crate, and `std::env::var_os` reached through `use std::env; env::var_os(…)` and through `use std::env::var_os as v; v(…)` each give `error: use of a disallowed method`; a statement-level `#[expect(clippy::disallowed_methods, reason = …)]` on the one allowed call is fulfilled (no `unfulfilled_lint_expectations`) | `cargo clippy --workspace --all-targets -- -D warnings` on a two-crate scratch workspace, 2026-10-30 |
| `tauri::is_dev()` is `pub const fn is_dev() -> bool { !cfg!(feature = "custom-protocol") }`; `tauri` re-exports `pub use url::Url;` and depends on `url ^2`; `url` latest is 2.5.8 | `tauri` 2.12.5 `crates/tauri/src/lib.rs:92`, `:316-319` (tag `tauri-v2.12.5`); crates.io `/crates/tauri/2.12.5/dependencies` and `/crates/url`, fetched 2026-10-30 |
| `tauri-build` writes the Windows version resource from `config.version`: `res.set("FileVersion", version_str); res.set("ProductVersion", version_str);` | `tauri-build` 2.7.4 `src/lib.rs:823-830` |
| Windows test executables that link `tauri` can fail at load with `STATUS_ENTRYPOINT_NOT_FOUND` (0xc0000139), because `tauri-build`'s resource and manifest are linked into binaries only: tauri-apps/tauri#13419, #14580, #13948 (all open); PR #16320 "option to link the Windows resource into all targets" (open) | `gh api repos/tauri-apps/tauri/issues/<n>`, 2026-10-30 (titles and state read; the reproductions involve `cargo test` on crates that use tauri) |
| `bundle.windows` (`WindowsConfig`) keys: `digestAlgorithm, certificateThumbprint, timestampUrl, tsp, webviewInstallMode, allowDowngrades, minimumWebview2Version, wix, nsis, signCommand, bundleVCRuntime`; `NsisConfig` adds `installMode`, `minimumWebview2Version` and UI keys. No key declares a minimum Windows version | `crates/tauri-schema-generator/schemas/config.schema.json` at tag `tauri-v2.12.5` |
| Cargo target fields `test = false` / `doctest = false` keep a target out of `cargo test`; `--all-targets` covers lib, bins, tests, benches, examples, and `--tests` takes only targets with `test = true` | https://doc.rust-lang.org/cargo/reference/cargo-targets.html#the-test-field ; https://doc.rust-lang.org/cargo/commands/cargo-test.html#target-selection |
| `#[path = "x.rs"] mod m;` in a non-inline position resolves relative to the directory of the declaring source file (`src/a/b.rs` → `src/a/x.rs`) | https://doc.rust-lang.org/reference/items/modules.html#the-path-attribute |
| libtest prints one `test <module path>::<name> ... ok` line per passing test and captures each passing test's own stdout (shown only with `--nocapture` / `--show-output`) | https://doc.rust-lang.org/rustc/tests/index.html |
| Vitest `json` reporter: Jest-compatible `testResults[].assertionResults[].{title, fullName, status}`; written to `.vitest/json/output.json` unless `outputFile` (CLI `--outputFile.json=<path>`) is set | https://vitest.dev/guide/reporters (vitest `docs/guide/reporters.md`, `main`, read 2026-10-30) |
| Inherent `impl` blocks may only be written in the crate that defines the type (error E0116) | https://doc.rust-lang.org/error_codes/E0116.html |
| `std::fs::rename` replaces an existing file; on Windows it is `MoveFileExW` with a `SetFileInformationByHandle` fallback | https://doc.rust-lang.org/std/fs/fn.rename.html |
| `tempfile::NamedTempFile::new_in(dir)`; `persist` "will atomically replace" the target, but "neither the file contents nor the containing directory are synchronized"; a process that exits before the destructor runs leaves the temp file | https://docs.rs/tempfile/3.27.0/tempfile/struct.NamedTempFile.html |
| `$XDG_DATA_HOME` defaults to `$HOME/.local/share` when unset or empty; "All paths set in these environment variables must be absolute … consider the path invalid and ignore it" | XDG Base Directory Specification 0.8, https://specifications.freedesktop.org/basedir/latest/ |
| A 64-bit process sees `CommonProgramW6432=%CommonProgramFiles%`; a 32-bit process sees `CommonProgramFiles=%CommonProgramFiles(x86)%` and `CommonProgramW6432=%CommonProgramFiles%` | https://learn.microsoft.com/en-us/windows/win32/winprog64/wow64-implementation-details (updated 2025-04-15) |
| rustup toolchain file `rust-toolchain.toml`, `[toolchain]` keys `channel`, `components`, `profile`; found by walking up the directory tree; rustup 1.28.1 restored automatic install, and `rustup toolchain install` with no arguments installs the active toolchain | https://rust-lang.github.io/rustup/overrides.html ; rustup `CHANGELOG.md` 1.28.0 / 1.28.1 |
| ESLint flat config with `defineConfig` from `eslint/config`, `tseslint.configs.strictTypeChecked`, `parserOptions.projectService: true` | https://typescript-eslint.io/getting-started/ ; https://typescript-eslint.io/getting-started/typed-linting/ |
| Vitest `fileParallelism`: default `true`; `false` forces one worker | https://vitest.dev/config/fileparallelism |
| npm can drop other platforms' optional dependencies from `package-lock.json` when it is regenerated with `node_modules` present | https://github.com/npm/cli/issues/4828 |
| `Swatinem/rust-cache` latest `v2.9.3` (2026-10-20) | `gh api repos/Swatinem/rust-cache/releases/latest` |

---

## Decisions taken in this spec (open to challenge in the clarify scan)

The roadmap leaves two decisions to this spec by name: the UI framework and the linter (line 469). D1-D2 settle
them. D3-D21 (and D12a) settle points the roadmap names but does not pin down. Each is binding unless the grill overturns it.

- **D1, UI: vanilla TypeScript (`template-vanilla-ts`), DOM built only with `createElement` / `textContent`.** The
  roadmap's type gate is `tsc --noEmit` (lines 498, 989), and `tsc` checks `.ts` / `.tsx` only: a Svelte or Vue
  template would need `svelte-check` / `vue-tsc` beside it, or its components would go unchecked. Among the `.ts`
  options vanilla has the smallest dependency set (`@tauri-apps/api` only at runtime) for an app whose helper later
  runs installers as administrator, and the v1 UI is one list, one settings screen and a few dialogs (roadmap lines
  564-565, 861-868). `innerHTML`, `outerHTML`, `insertAdjacentHTML`, `document.write` and `new Function` are banned by
  lint rule (FR-052), so no string from the feed can ever become markup.
- **D2, linter: ESLint 10.14.0 + typescript-eslint 8.72.1 `strictTypeChecked`, and TypeScript pinned to `6.0.3`.**
  typescript-eslint's peer range is `typescript >=4.8.4 <6.1.0`, so TypeScript 7.0.2 (latest) cannot be linted with
  type information; 6.0.3 is also exactly what the template pins (`~6.0.3`). No formatter is added: the roadmap names
  none for TypeScript (line 989), and `cargo fmt` covers Rust.
- **D3, test DOM: Vitest 5.0.5 with jsdom 29.1.1; IPC through `@tauri-apps/api/mocks`.** jsdom 30 requires Node
  `^24.15.0` and this box has 24.7.0; 29.1.1 accepts `>=24.0.0`. Tauri's mocking page uses Vitest with jsdom.
- **D4, Rust toolchain: `rust-toolchain.toml` with `channel = "stable"`, `components = ["clippy", "rustfmt"]`,
  `profile = "minimal"`; no numbered pin.** The user rules toolchains are updated in place, never pinned side by side
  (memory `feedback_update_wsl_toolchains_in_place.md`, 2026-10-30). Consequence, accepted: a new stable can add a
  clippy lint and turn `-D warnings` red on unchanged code; the fix is the code, never an `allow`. The dev box runs
  `rustup update stable` (1.98.1 → 1.99.1) before the first gate so it matches CI. `rust-version = "1.90"` in the
  workspace (Tauri 2.12.5's floor).
- **D5, names.** Packages `krate-manager-core` (`crates/core`, lib `krate_manager_core`), `krate-manager-helper`
  (`crates/helper`), `krate-manager` (`src-tauri`, lib `krate_manager_lib`, binary `krate-manager`). `productName`
  `"Krate Manager"`. `identifier` `"com.krateaudio.krate-manager"`: the hyphen keeps the manager's own bundle and pkg
  identifier outside the pattern Phase 10's `uninstall_pkg` accepts (`^com\.krateaudio\.[a-z0-9]+$`, roadmap line
  709), so the elevated helper can never be pointed at the manager's own receipt.
- **D6, the guard, as the Rust language allows it.** `Roots` lives in `core`; an inherent `impl Roots` in `src-tauri`
  is error E0116. So the production constructor is an extension trait in `src-tauri`, `os_roots::FromOs` with
  `fn from_os() -> Result<Roots, RootsError>`, implemented for `Roots`: the call site reads `Roots::from_os()` as the
  roadmap writes it (line 476). The workspace `clippy.toml` disallows the two constructors,
  `krate_manager_lib::os_roots::FromOs::from_os` (roadmap guard 2) and `krate_manager_core::roots::Roots::new` (the
  mechanical form of guard 3: "every test builds roots through `temp_roots()`", line 480), and the six process-
  environment reads `std::env::var`, `std::env::var_os`, `std::env::vars`, `std::env::vars_os`, `std::env::home_dir`
  and `std::env::temp_dir` (D9: clippy resolves paths, so an import or an alias cannot evade it, verified this session;
  a text scan can be evaded). Exactly seven call sites carry a suppression, each a statement-level
  `#[expect(clippy::disallowed_methods, reason = "<one line>")]` on that one call: (1) the app's setup
  (`Roots::from_os()`); (2) `from_os`'s `std::env::var_os` statement; (3) `from_os`'s `Roots::new`; (4)
  `temp_roots_in`'s `Roots::new`; (5) `canonical_temp_dir`'s `std::env::temp_dir()` (FR-020); (6) the one `Roots::new`
  validation test (SC-005); (7) the symlink case's `std::env::var_os("CI")` read (SC-007). Not `#[allow]`: an `expect`
  that stops firing is itself a warning (`unfulfilled_lint_expectations`). That is the detector for a mistyped
  `clippy.toml` path, together with the clippy-guard fixture (FR-072), because clippy itself reports a mistyped path
  only as a non-fatal warning, or not at all when the path's crate is not a dependency (External facts). The workspace
  denies `clippy::allow_attributes` and `clippy::allow_attributes_without_reason`, so every suppression in the
  workspace is an `expect` with a reason (the roadmap's "one-line reason", lines 480, 990).
- **D7, the state dir (clarified 2026-10-30, Q2: the Tauri identifier dir on all three OSes).** Windows
  `%LOCALAPPDATA%\com.krateaudio.krate-manager` (local, not roaming: it holds staged downloads); macOS
  `$HOME/Library/Application Support/com.krateaudio.krate-manager`; Linux `$XDG_DATA_HOME/com.krateaudio.krate-manager`,
  with `$HOME/.local/share` when `XDG_DATA_HOME` is unset, empty or relative (XDG 0.8). The final component is the
  `identifier` of `tauri.conf.json` (D5), held in core as `platform::APP_IDENTIFIER = "com.krateaudio.krate-manager"`
  and asserted equal to the config's by FR-045; it is computed in core from the `OsEnv` with no Tauri call. One
  per-user dir per OS, which the NSIS "Delete app data" box removes on Windows (`RmDir /r "$LOCALAPPDATA\${BUNDLEID}"`).
  XDG data, not state or config, because the dir will hold the Linux install manifests, the only record of a Linux
  install (roadmap line 667). The staging dir is always `<state>/staging`, which Phase 10 relies on (roadmap lines
  724-726). Phase 4 names these entries in it: `<state>/settings.json`, `<state>/manager.lock` (FR-100) and
  `<state>/logs/` (FR-104); later phases add their own names.
- **D8, what `Roots` holds (roadmap: "plug-in roots, preset roots, state dir, staging dir", line 472).**
  `plugin_roots: Vec<PluginRoot { kind, path }>` in Steinberg priority order, `kind` one of `Configured`, `Vst3User`,
  `Vst3Global`, `AuUser`, `AuGlobal`; `presets: PresetRoots { factory, user }`; `state_dir`; `staging_dir`. `default_roots`
  never produces a `Configured` entry. Only `assemble_context` / `reassemble` (FR-102) add one, and only on Linux: a
  configured `linux_plugin_root` becomes the first `plugin_roots` entry with kind `Configured` (clarified 2026-10-30,
  Q3), `~/.vst3` (`Vst3User`) stays listed, and Phase 6's detection and Phase 9's allowed-root set read this one list.
  Per OS:

  | | Windows | macOS | Linux |
  |---|---|---|---|
  | `Vst3User` | `%LOCALAPPDATA%\Programs\Common\VST3` | `$HOME/Library/Audio/Plug-Ins/VST3` | `$HOME/.vst3` |
  | `Vst3Global` | `%CommonProgramW6432%\VST3` | `/Library/Audio/Plug-Ins/VST3` | `/usr/lib64/vst3`, `/usr/lib/vst3`, `/usr/local/lib64/vst3`, `/usr/local/lib/vst3` |
  | `AuUser` / `AuGlobal` | — | `$HOME/Library/Audio/Plug-Ins/Components` / `/Library/Audio/Plug-Ins/Components` | — |
  | `presets.factory` | `%ProgramData%\Krate Audio` | `/Library/Application Support/Krate Audio` | `$HOME/Documents/Krate Audio` |
  | `presets.user` | `%USERPROFILE%\Documents\Krate Audio` | `$HOME/Documents/Krate Audio` | `$HOME/Documents/Krate Audio` |

  Sources: VST3 folders per Steinberg (tauri_feasibility.md Q2 line 65; per_os_install_mechanics.md line 13); the
  installers' paths (roadmap lines 116-118); presets `preset_paths.cpp:8-51`. Linux `presets.factory` is the user
  folder because that is where the manager places factory presets (Q3, roadmap lines 26, 118, 120-126); the plugins'
  `/usr/share/krate-audio` (`preset_paths.cpp:45-49`) is not written by anyone in v1. `CommonProgramW6432` is used
  because it names the 64-bit Common Files folder for a 32-bit and a 64-bit process alike (WOW64 table). The Windows
  `Krate Audio\<P>` subfolder is a detection detail of Phase 6 (roadmap line 554), not a root.
- **D9, the injected environment.** `default_roots(&OsEnv)` reads only an `OsEnv` value: a map from variable name to
  `OsString`, built in tests from literal pairs and in production by `from_os()` from `std::env::var_os` over a fixed
  key list (Windows `LOCALAPPDATA`, `CommonProgramW6432`, `ProgramData`, `USERPROFILE`; macOS `HOME`; Linux `HOME`,
  `XDG_DATA_HOME`). On Windows the lookup is ASCII-case-insensitive, as Windows environment names are. A missing or
  empty required variable, or a relative value, is `RootsError::Env { var }`; nothing falls back to a guessed path.
  `core` itself reads no environment, no home directory and no temp directory outside `test_support`; this is enforced
  by clippy's `disallowed-methods` over the six `std::env` reads (D6, FR-015), not by a text scan, and the only
  workspace sites that may read them are the seven D6 `#[expect]` sites.
- **D10, settings schema v1.** `settings.json` = `{"schema_version": 1, "linux_plugin_root": <absolute path string
  or null>, "linux_graphics_workaround": <bool>}`. `null` means "the default", resolved through `Roots` (the Linux
  `Vst3User` root, `~/.vst3`), so the stored defaults are identical on every OS and the Windows / macOS defaults simply
  never use the two Linux keys (there is no install-scope key and no per-user opt-in, roadmap lines 483-484).
  `linux_graphics_workaround` defaults to `false` (roadmap line 867: never shipped unconditionally). Unknown fields are
  rejected: a newer manager that adds a field raises `schema_version` (this strictness stands, Q4). Clarified
  2026-10-30 (Q4): `save` re-reads the file on disk first and refuses with `E_SETTINGS_FUTURE_VERSION` while that file
  has `schema_version` > 1, so an older manager never destroys a newer manager's settings; a `Corrupt` or `Invalid`
  file on disk is overwritten (FR-031, FR-036).
- **D11, the command surface: two read-only commands.** "Empty but wired" (line 470) is `app_version`, returning
  `env!("CARGO_PKG_VERSION")` of `src-tauri`, which the window shows, and (added 2026-10-30 with Q1) `startup_error`,
  returning the fixed notice text of a refused start (`"Krate Manager is already running"`, FR-101) or `None`. With
  zero commands the ACL wiring (`AppManifest::commands`, the capability, the generated permission) would have nothing
  to prove itself on. `build.rs` calls `tauri_build::try_build(Attributes::new().app_manifest(AppManifest::new()
  .commands(&["app_version", "startup_error"])))`, which makes every app command ACL-checked
  (`webview/mod.rs:2076-2083`); the capability `capabilities/main.json` grants window `main` exactly
  `["allow-app-version", "allow-startup-error"]`: no `core:default`, no plugin permission, no `remote`. The template's
  `tauri-plugin-opener` is removed. Channels need no permission (the fetch command is exempt, same lines), so Phase 7
  can stream progress without widening this file.
- **D12, a navigation guard.** CSP does not stop a top-level navigation. A small inline plugin registered with
  `tauri::plugin::Builder::on_navigation` allows a navigation only when the target URL's scheme, host and port equal
  the app's own origin (`tauri://localhost`, or `http://tauri.localhost` on Windows; the `devUrl` origin in dev mode);
  everything else returns `false`. Both decisions are pure functions in the Tauri-free `core::navigation`
  (`app_origin(dev, os)` and `is_app_origin(target, app)`), unit-tested there (FR-044, SC-016). "Dev mode" is
  `tauri::is_dev()` (`!cfg!(feature = "custom-protocol")`, lib.rs:316-319), not `debug_assertions`: `tauri build
  --debug` enables `custom-protocol` and serves the bundled assets, so a `debug_assertions` test would pick the wrong
  origin. The app passes the flag in at runtime, so no `cfg`-dependent branch exists in the app code.
- **D12a, no `tauri`-linked test binary.** Every test of the workspace runs in a binary that does not link `tauri`:
  Windows test executables that link it can fail to load with `STATUS_ENTRYPOINT_NOT_FOUND` (External facts,
  tauri#13419 / #14580 / #13948, PR #16320 open). So the navigation logic (D12), the startup assembly (FR-017) and the
  static config guard (FR-045) live in or under `crates/core`, and `src-tauri`'s `[lib]` sets `test = false` and
  `doctest = false` and its `[[bin]]` `test = false`, so `cargo test --workspace` builds no `src-tauri` test harness.
  `cargo clippy --all-targets` still checks `src-tauri`'s lib and bin; the clippy-guard fixture (FR-016) is linted,
  never run. Whether a bare link of `tauri` triggers the load failure is unverified; this decision makes the question
  moot rather than answering it.
- **D13, the CSP.** `default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self'; font-src 'self';
  connect-src ipc: http://ipc.localhost; object-src 'none'; base-uri 'none'; form-action 'none'; frame-src 'none';
  frame-ancestors 'none'`. No `http(s)://` origin appears except `http://ipc.localhost` (Tauri's IPC, CSP page). No
  `devCsp`, so the same policy applies in development. `dangerousDisableAssetCspModification` is absent, so Tauri's
  nonces and hashes stay.
- **D14, `devUrl` keeps cargo builds independent of the frontend.** `build.devUrl` is `http://localhost:1420` (the
  template's Vite port). `cargo clippy` and `cargo test` build without `custom-protocol`, so `generate_context!` embeds
  nothing and needs no `dist/` (`tauri-codegen` `context.rs:175-190`). Only `tauri build` (which enables
  `custom-protocol` and runs `beforeBuildCommand: "npm run build"`) needs the built frontend.
- **D15, bundle targets per OS.** The base `tauri.conf.json` sets no `targets`; `tauri.windows.conf.json` sets
  `["nsis"]`, `tauri.macos.conf.json` `["app"]`, `tauri.linux.conf.json` `["deb"]`. That is the narrowest build that
  proves the Phase 4 config (NSIS `currentUser`, the macOS plist and signature). The macOS pkg, the AppImage, the
  universal build and the `ubuntu:22.04` container are Phase 11 (roadmap lines 816-822). The template's `"all"` would
  also build MSI (WiX + VBScript, tauri_feasibility.md Q5 line 159), which Q5 does not ship.
- **D16, the Ubuntu leg is `ubuntu-24.04`, pinned.** `ubuntu-latest` is 24.04 today but `ubuntu-26.04` exists and
  "-latest" migrates over 1-2 months; a WebKitGTK version jump should arrive by a deliberate edit. The apt line is the
  current prerequisites page's (`libwebkit2gtk-4.1-dev build-essential curl wget file libxdo-dev libssl-dev
  libayatana-appindicator3-dev librsvg2-dev`); the report's `libappindicator3-dev … patchelf` line (report line 65)
  is the pipeline page's `ubuntu-22.04` set and belongs to Phase 11's container. The Windows and macOS legs stay
  `windows-latest` and `macos-latest` as the roadmap names them (line 496). **This departs from roadmap line 497**,
  which names the `libappindicator3-dev … patchelf` set for this leg; the plan edits line 497 to "the current Tauri
  prerequisites apt line (https://v2.tauri.app/start/prerequisites/)" so the roadmap and this spec agree (FR-090
  permits that edit; recorded under "Additions and departures").
- **D17, one injection point.** `core::context::AppContext { roots: Roots, settings: Settings, feed_url: String }` is
  the value every later command receives, held in Tauri managed state as `core::context::SharedState =
  std::sync::RwLock<AppState>` with `AppState { context: Result<AppContext, StartupError>, settings_error:
  Option<SettingsError>, lock: Option<Arc<StateLock>> }` (FR-017, FR-103; clarified 2026-10-30, Q6: the lock lets a
  settings change apply without a restart, and later phases add fields to `AppContext` without changing the managed
  type). Its production inputs are built only in `src-tauri` setup: `Roots::from_os()` and the feed URL, the constant
  `https://krateaudio.com/versions.json` (roadmap line 526) unless the `dev-feed` feature overrides it (D24), passed to
  the pure-of-the-OS `core::context::assemble_context`, which takes the `StateLock` (D22) and calls
  `settings::load(roots.state_dir())`. Phase 4 delivers the feed URL injection only. **Departure from roadmap line 473** ("Feed URL, HTTP client base and the
  elevation launcher are injected the same way"): the HTTP client base is deferred to Phase 5 (roadmap line 526, whose
  client and fixture server define its shape) and the elevation launcher to Phase 10 (roadmap lines 729-731, "Launchers
  behind one trait"). Each joins `AppContext` as a field in that phase, with its production value built in the same
  setup. Phase 4 defines no placeholder for either: a trait with no caller would be a guess at a shape those phases
  specify. The plan edits roadmap line 473 to say so (FR-090), and the deferral is listed under "Additions and
  departures" and in Non-goals.
- **D18, the lint's grammar.** `tools/lint-manager-version.js` reads `version.json` (`version` must match
  `^(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$`, the bounded `X.Y.Z` of the tag `manager/vX.Y.Z`
  that Phase 3's collector accepts), `src-tauri/tauri.conf.json` (`version`, a literal string), every
  `src-tauri/tauri.*.conf.json` (must carry no `version` key), the root `Cargo.toml` (`[workspace.package] version`
  and the `members` array) and each member's `Cargo.toml` (`[package] version`, either `version.workspace = true` or
  a literal equal string), and `package.json` (must carry no `version` key: npm makes `version` optional for a package
  that is never published, so a second, drifting copy is simply not written). It parses only that TOML subset (section
  headers, `[[table]]` headers, `key = "string"`, `key = true` / `key = false`, `key.workspace = true`, single- or
  multi-line string arrays; plan SI-3, PD-13: FR-002's `publish = false` and `src-tauri`'s `[[bin]]`) and refuses anything else in
  the lines it reads with a named error, rather than guessing; it needs no TOML dependency.
- **D19, the manager CI's shape is tested offline.** Like Phases 2 and 3 (`tools/feed-contract/tests/workflow-shape.test.js`),
  a Node test reads `manager-ci.yml` and asserts its legs, steps, path filter, permissions, absence of secrets and of
  serial test flags (FR-080). It runs in `feed-tools.yml`, the repo's `node --test` lane (roadmap lines 250, 494).
- **D20, the helper crate is a stub.** `crates/helper` builds a library with no public item and a binary that prints
  `krate-manager-helper: no operations in this build` to stderr and exits 2, whatever its arguments. It is not a
  sidecar yet and depends on nothing. Phase 10 gives it its operation set (roadmap lines 694-715).
- **D21, CI verdicts are annotations.** The token this environment has cannot read job logs or download artifacts
  (memory `reference_gh_token_scope_no_logs.md`); every assertion step in `manager-ci.yml` therefore prints its
  verdict as a `::notice` (pass) or `::error` (fail) workflow command naming the leg and the check, so compliance can
  read PENDING-CI rows from check-run annotations. A green `cargo test` or `npm test` step is not such a verdict: it
  emits no annotation, and a test compiled out by a mistyped `#[cfg(target_os = …)]` leaves it green. So every leg has a
  "required tests ran" step (FR-077) that checks, by name, that each test a PENDING-CI row relies on ran and passed on
  that leg, and annotates the result; that `::notice` is what a Rust or Vitest PENDING-CI row closes on.
- **D22, single instance by a core file lock (clarified 2026-10-30, Q1).** `core::state_lock::StateLock` holds an
  exclusive `std::fs::File::try_lock` (stable since Rust 1.89, inside the `rust-version = "1.90"` floor) on
  `<state>/manager.lock`, taken inside `assemble_context`. A second instance gets `E_STARTUP_LOCKED`, shows "Krate
  Manager is already running" and exits. No Tauri plugin is used (FR-004 stands) and the behaviour is tested in core
  with two locks on one temp state dir (D12a). The file is never deleted (deleting a lock file races); the OS releases
  the lock when the process ends. How the notice is shown without a Tauri plugin or a native popup: the refused
  instance still opens its one window, the frontend reads `startup_error` (D11) and renders the text, and setup ends the
  process after a fixed delay (FR-101). This is the spec's reading of "shows ... and exits"; the user decided the behaviour, not the mechanism.
- **D23, logging is Phase 4's (clarified 2026-10-30, Q5).** The `log` 0.4.34 facade is a dependency of core and of
  `src-tauri`. A small file sink in `src-tauri` implements `log::Log` and writes `<state>/logs/krate-manager.log`,
  size-capped with one rotation (`krate-manager.log.1`). Because `src-tauri` has no test binary (D12a), the
  size-capped rotating writer itself is core code (`core::log_sink::RotatingLog`, tested there) and the `src-tauri`
  part is the `log::Log` glue and the one-time `log::set_logger` call (the user named the sink as `src-tauri`'s; the
  rotation logic sits in core so that it can be tested). Startup error codes are written
  to the log in place of the earlier `eprintln!` (FR-017, FR-105); later phases log through the same facade.
- **D24, a release-excluded feed override (clarified 2026-10-30, Q7).** `src-tauri` has a cargo feature `dev-feed`,
  off by default and never enabled by `tauri build` in CI or by `release-manager.yml`. With it on, setup reads the
  feed URL from the file `<state>/dev-feed-url.txt` instead of the `FEED_URL` constant, so a manual audition in Phases
  6-10 can point at a local fixture server. It adds no environment read to the clippy-guarded set (D6 stays at seven
  sites): the source is a file, never a process environment variable. The parsing of the file's text is a pure core
  function (`core::context::parse_dev_feed_url`, tested in core); the file read is the one feature-gated `std::fs`
  call in `src-tauri`. A `manager-ci` step asserts the feature is absent from the release build (FR-107), the pattern
  Phase 10 uses for `ci-temp-roots`.

---

## Functional Requirements

### A. Scaffold and workspace (roadmap lines 468-470; D1, D5, D14, D15, D20)

- **FR-001** `apps/krate-manager/` follows the Repository Layout (roadmap lines 216-228): `Cargo.toml` (workspace),
  `Cargo.lock`, `clippy.toml`, `rust-toolchain.toml`, `version.json`, `CHANGELOG.md`, `package.json`,
  `package-lock.json`, `tsconfig.json`, `tsconfig.node.json`, `vite.config.ts`, `eslint.config.mjs`, `index.html`,
  `.gitignore`, `src/`, `src-tauri/`, `crates/core/`, `crates/helper/`, `tests/fixtures/clippy-guard/`. It is
  built from the `template-vanilla-ts` and `_base_` files at create-tauri-app commit `c6f5cf68`, the commit External
  facts cites, read with `gh api` (tasks TA-6), and then reduced as FR-002 to FR-005 say.
- **FR-002** The root `Cargo.toml` is a virtual workspace: `members = ["crates/core", "crates/helper", "src-tauri"]`,
  `exclude = ["tests/fixtures/clippy-guard"]`, `resolver = "3"`, `[workspace.package]` with `version` (= `version.json`),
  `edition = "2024"`, `rust-version = "1.90"`, `license`, `publish = false`; `[workspace.lints.clippy]` with
  `allow_attributes = "deny"` and `allow_attributes_without_reason = "deny"`; each member sets `version.workspace =
  true`, `edition.workspace = true` and `[lints] workspace = true`. The `[profile.release]` block of the template is
  kept. `Cargo.lock` is committed and every CI cargo command passes `--locked`.
- **FR-003** `apps/krate-manager/.gitignore` ignores `/target/`, `/dist/`, `/src-tauri/gen/` and the permission files
  `tauri-build` autogenerates under `src-tauri/`; no build, test or lint step leaves a tracked file changed or an
  untracked file unignored (SC-020).
- **FR-004** `src-tauri` is desktop-only: `[lib] name = "krate_manager_lib"` without the template's
  `staticlib` / `cdylib` crate types and without `#[cfg_attr(mobile, tauri::mobile_entry_point)]`; dependencies
  `tauri` 2.12.5 (no features), `tauri-build` 2.7.4, `serde`, `serde_json`, `log` 0.4.34, `krate-manager-core`; a
  `[features]` table holding only `dev-feed = []` (D24, FR-106; `default` is absent, so no feature is on by default); no Tauri plugin
  crate dependency and no plugin permission; the only registered plugin is FR-044's inline `navigation-guard`, which
  exposes no commands. `[lib]` sets `test = false` and `doctest = false`, and the `[[bin]]` `krate-manager` sets
  `test = false` (D12a); `src-tauri` has no `tests/` directory and no `#[test]`. The template's `greet` command, `tauri-plugin-opener`, its capability entry and the `@tauri-apps/plugin-opener`
  npm dependency are removed. `main.rs` keeps the template's `windows_subsystem = "windows"` line.
- **FR-005** `tauri.conf.json`: `productName` `"Krate Manager"`, `version` = `version.json`, `identifier`
  `"com.krateaudio.krate-manager"`, `build.devUrl` `"http://localhost:1420"`, `build.beforeDevCommand` `"npm run dev"`,
  `build.beforeBuildCommand` `"npm run build"`, `build.frontendDist` `"../dist"`, one window labelled `main`,
  `app.withGlobalTauri` `false`, `app.security.csp` the D13 string, no `devCsp`, no `capabilities` key (so
  `./capabilities/` is read), `bundle.active` `true`, `bundle.icon` the template set, `bundle.macOS.signingIdentity`
  `"-"`, `bundle.macOS.minimumSystemVersion` `"15.0"`, `bundle.windows.nsis.installMode` `"currentUser"`; no
  `bundle.macOS.infoPlist`, no `src-tauri/Info.plist`, no `createUpdaterArtifacts`, no `externalBin`, no `plugins`
  block. The three platform files set only `bundle.targets` (D15).
- **FR-006** `crates/helper` is the D20 stub: `cargo run -p krate-manager-helper -- anything` exits 2 with the one
  stderr line.
- **FR-007** No crate depends on a crate that reads OS folders: `krate-manager-core`'s dependency tree
  (`cargo tree -p krate-manager-core -e normal`) contains no `tauri*`, `dirs*`, `directories*` or `home` crate (core
  stays Tauri-free so `cargo test` needs no webview stack, roadmap lines 224-226). Its `[dependencies]` are exactly
  `serde`, `serde_json`, `thiserror`, `tempfile` (FR-031), `url` 2.5.8 (FR-044: the crate `tauri` re-exports as
  `tauri::Url` and depends on as `^2`, so the lockfile holds one copy) and `log` 0.4.34 (FR-104; clarified 2026-10-30,
  Q5); the `test-support` feature (FR-022) adds nothing. The `StateLock` (FR-100) uses `std::fs::File::try_lock`, so it
  adds no crate.

### B. Platform and Roots (roadmap lines 471-481; D6-D9)

- **FR-010** `core::platform` defines `pub enum Os { Windows, Macos, Linux }` and `pub trait Platform { fn os(&self)
  -> Os; fn default_roots(&self, env: &OsEnv) -> Result<RootPaths, RootsError>; }`, with `WindowsPlatform`,
  `MacosPlatform` and `LinuxPlatform` each behind `#[cfg(target_os = …)]` and `pub type NativePlatform` naming the
  one compiled. Later phases add methods to this trait; Phase 4 adds no other method.
- **FR-011** `default_roots` is pure: it computes the D8 table from the `OsEnv` alone and touches no filesystem, no
  process environment and no registry. Paths are joined component by component with `PathBuf::push`; nothing is
  canonicalized or created. Proven by behaviour (SC-005's `OsEnv`-into-temp case) and by FR-021's token scan of
  `platform/` and `roots.rs`.
- **FR-012** `OsEnv` (D9): `OsEnv::from_pairs(impl IntoIterator<Item = (&str, &OsStr)>)` and `fn get(&self, name)`;
  on Windows `get` compares names ASCII-case-insensitively. `RootsError::Env { var: &'static str }` is returned for a
  required variable that is missing, empty, not absolute, or with a `..` component (plan SI-10, PD-26; `HOME=/` stays
  accepted, since the roots derived from it have normal components). `XDG_DATA_HOME` is optional: missing, empty or relative
  selects `$HOME/.local/share`.
- **FR-013** `Roots::new(RootPaths) -> Result<Roots, RootsError>` is the only constructor of `Roots` from paths. It requires every
  path absolute (`RootsError::Relative { which }`) and normal (no `..` component, and at least one normal component in
  every plug-in, preset, state and staging root, so no filesystem root such as `/` or `C:\`; `RootsError::NotNormal {
  which }`; plan SI-10, PD-26), at least one `Vst3User` and one `Vst3Global` root (`RootsError::MissingRoot { kind }`
  for a missing `Vst3User` or `Vst3Global` root; plan SI-4, PD-4), and
  `staging_dir == state_dir.join("staging")` (`RootsError::Staging`). It touches no filesystem. `Roots` has no
  `Default`, no `From<…>`, no public field (read through accessors), and is `Clone + Debug + PartialEq`. Its one other
  producer is `Roots::with_configured_plugin_root(&self, root: Option<&Path>) -> Result<Roots, RootsError>` (FR-102),
  which builds the result inside `roots.rs` from the existing value (it does not call `Roots::new`, so the D6 site
  count stays seven) and applies the same absolute-and-normal rule. `Roots` carries a private `sandbox:
  Option<PathBuf>`, `None` from `new`, set by `test_support::temp_roots_in` to the canonical, guard-checked base through
  the crate-private `Roots::sandboxed(self, base: PathBuf) -> Roots`; `Clone` and every derivation keep it;
  `with_configured_plugin_root` on a sandboxed value panics on a path that is not a strict, component-wise descendant
  of the sandbox base, with a message naming both paths. Production `Roots` (from `from_os`) always carry `None` (plan
  SI-8, PD-24).
- **FR-014** `src-tauri/src/os_roots.rs` defines `pub trait FromOs { fn from_os() -> Result<Roots, RootsError>; }`
  implemented for `Roots`: it builds an `OsEnv` from `std::env::var_os` over the D9 key list and returns
  `Roots::new(NativePlatform.default_roots(&env)?)`. This is the only code in the workspace that reads the process
  environment for roots.
- **FR-015** `apps/krate-manager/clippy.toml`: `disallowed-methods = [{ path = "krate_manager_lib::os_roots::FromOs::from_os",
  reason = "production roots only in app setup; tests use temp_roots()" }, { path = "krate_manager_core::roots::Roots::new",
  reason = "tests build roots with temp_roots(); production with Roots::from_os()" }]` plus one entry per process-
  environment read, `std::env::var`, `std::env::var_os`, `std::env::vars`, `std::env::vars_os`, `std::env::home_dir`,
  `std::env::temp_dir`, each with `reason = "the environment is read only in os_roots.rs and test_support (D9)"`. The
  only suppressions are the seven D6 sites, each a statement-level `#[expect(clippy::disallowed_methods, reason =
  "…")]` on that one call. `allow-invalid` is left at its default and is not relied on (External facts: an
  unresolvable path is a non-fatal warning or silent); the detectors of a mistyped path are the unfulfilled `#[expect]`
  warnings under `-D warnings` (SC-001) and the fixture (FR-072).
- **FR-016** `tests/fixtures/clippy-guard/` is a crate outside the workspace (`[workspace]` root `exclude`, FR-002)
  with its own committed `Cargo.lock`, a path dependency on `src-tauri` and `crates/core`, and three `#[test]`s with no
  suppression: one calls `Roots::from_os()`, one calls `Roots::new(…)`, one calls `std::env::var("HOME")` through
  `use std::env; env::var(…)` (the import form a text scan would miss). It is linted only, never run. It inherits
  `apps/krate-manager/clippy.toml` because clippy walks up from its `CARGO_MANIFEST_DIR` (clippy configuration page).
- **FR-017** Startup state is assembled by a pure function in core, so it can be tested without launching the app:
  `core::context::assemble_context(roots: Result<Roots, RootsError>, feed_url: &str) -> AppState`. On `Err(e)` it
  returns `AppState { context: Err(StartupError::Roots(e)), settings_error: None, lock: None }`. On `Ok(roots)` it
  first takes the `StateLock` on `roots` (`StateLock::acquire(&roots)`, state dir `roots.state_dir()`; FR-100; plan
  SI-9, PD-25); if that fails it returns `AppState { context:
  Err(StartupError::Locked | StartupError::LockIo(…)), settings_error: None, lock: None }` and does not load settings.
  With the lock held it calls `settings::load(&roots)` (plan SI-9, PD-25) and returns `AppState { context: Ok(AppContext {
  roots: <roots with the Configured Linux root folded in, FR-102>, settings: outcome.settings, feed_url }),
  settings_error: outcome.error, lock: Some(Arc<StateLock>) }`. It creates exactly two things, and only when `roots` is
  `Ok`: the state dir (if absent) and `manager.lock` inside it (clarified 2026-10-30, Q1; `load` writes nothing,
  FR-032). `StartupError` has a stable `code()` per variant: `E_STARTUP_ROOTS` (wraps the `RootsError`),
  `E_STARTUP_LOCKED`, `E_STARTUP_LOCK_IO`. The `src-tauri` `lib.rs` `run()` is glue only: `let roots = Roots::from_os();`
  (D6 site 1), the feed URL (`FEED_URL`, or the D24 override under the `dev-feed` feature), `let state =
  assemble_context(roots, &feed_url);`, the log sink initialised on the roots when they were built (`log_sink::init`
  on a clone of the `Ok` roots taken before `assemble_context` consumes them, FR-105; plan SI-9, PD-25), a `log::error!` of each startup error's code, the locked-instance handling (FR-101); `run()` assembles the
  state before the builder and registers it with `tauri::Builder::manage(SharedState::new(state))`; `.setup` only
  schedules the locked-instance exit (plan SI-7, PD-1; FR-103); the window opens either way (later phases surface the
  other errors). The glue creates no directory
  itself; the only directories and files created at startup are the state dir and `manager.lock` (core), then
  `logs/` and the log file (the sink, FR-105). FR-045's static guard asserts that `lib.rs` contains the
  `assemble_context(` call, the `.manage(` call, `SharedState` and the `navigation-guard` plugin registration
  (SC-015); the assembly itself is SC-024.

### C. Test support (roadmap line 480)

- **FR-020** `core::test_support` (compiled under `cfg(any(test, feature = "test-support"))`) provides:
  - `pub fn canonical_temp_dir() -> PathBuf`: `std::env::temp_dir()` canonicalized (D6 site 5; the only temp-dir read
    in the workspace).
  - `pub fn check_under_temp(base: &Path, temp_root: &Path) -> Result<PathBuf, GuardError>`: canonicalizes both
    (`GuardError::Canonicalize { which, source }` when either fails, e.g. a base that does not exist) and returns the
    canonical base only when it is a **strict** descendant of the canonical temp root (`base != temp_root &&
    base.starts_with(temp_root)`, component-wise `Path::starts_with`), else `GuardError::Outside { base, temp_root }`.
    It reads metadata only and creates nothing.
  - `fn temp_roots_in(base: &Path, temp_root: &Path) -> Roots` (crate-private; its tests are a child module): calls
    `check_under_temp` **first** and panics on `Err` before any directory is created; then builds every D8 root as a
    subdirectory of the canonical base (`plugins/vst3-user`, `plugins/vst3-global`, `plugins/au-user`,
    `plugins/au-global` on macOS only, `presets/factory`, `presets/user`, `state`, `state/staging`) through
    `Roots::new` (D6 site 4) and creates those subdirectories, never anything outside the base.
  - `pub fn temp_roots(base: &Path) -> Roots` = `temp_roots_in(base, &canonical_temp_dir())`, and `pub fn
    fresh_temp_roots() -> (tempfile::TempDir, Roots)` (a new `TempDir`, then `temp_roots` on it).
  Canonicalizing both sides makes macOS `/var` → `/private/var` and Windows 8.3 short names (`RUNNER~1`) compare
  correctly. The strict-descendant rule keeps tests out of the shared system temp itself, where parallel test threads
  (SC-021) would share one `state/` and leave directories behind. The guard's negative tests run against an injected
  fake temp root inside a fresh `TempDir` (SC-007), so a regressed guard can only write inside that `TempDir`.
- **FR-021** Mechanical injection check (text scan for literals; the environment is clippy's job, FR-015): an
  integration test in `crates/core/tests/injection.rs` scans every `*.rs` file under `crates/core/src/` except
  `test_support.rs` and any file whose name ends in `_tests.rs`. Every unit-test module of a scanned file lives in such
  a file, declared `#[cfg(test)] #[path = "<stem>_tests.rs"] mod tests;` (the path resolves beside the declaring file,
  External facts), so test-vector literals (SC-002's Windows paths, SC-009's `C:\\vst`, SC-016's foreign hosts) never
  sit in a scanned file; the scan fails on a scanned file containing `mod tests {` (an inline test module). Rules:
  - outside `platform/`: no `"C:\\`, `"/Library`, `"/usr/`, `"/home`, `"/Users`, `krateaudio.com` or `api.github.com`;
  - inside `platform/`: path string literals only from an allow-list of the exact D8 literals and `APP_IDENTIFIER` (D7);
  - inside `platform/` and in `roots.rs` (FR-011, FR-013: they touch no filesystem): none of the word-boundary tokens
    `fs`, `File`, `OpenOptions`, `canonicalize`, `metadata`, `symlink_metadata`, `exists`, `try_exists`, `is_dir`,
    `is_file`, `read_dir`, `read_link`, `create_dir`, `create_dir_all`.
  The same test asserts that the non-test files of `src-tauri/src/` contain `krateaudio.com` exactly once (the D17
  `FEED_URL` constant in `lib.rs`) and `var_os` only in `os_roots.rs` (a second net under FR-015). The scanner is a
  function the test also runs on in-memory strings (SC-008).
- **FR-022** `krate-manager-core` has a `test-support` feature exposing `test_support` to other crates' tests;
  `src-tauri` and `crates/helper` enable it only under `[dev-dependencies]`.

### D. Settings model (roadmap lines 482-486; D10)

- **FR-030** `core::settings::Settings { linux_plugin_root: Option<PathBuf>, linux_graphics_workaround: bool }` with
  `Settings::default()` = `{None, false}` on every OS, and `fn linux_plugin_root_or_default(&self, roots: &Roots) ->
  &Path` returning the stored path or the `Vst3User` root. The file format is the D10 JSON, written by `serde_json`
  pretty-printed with a final LF, keys in the order shown; `schema_version` is written as `1`.
- **FR-031** `save(roots: &Roots, settings: &Settings) -> Result<(), SettingsError>` (state dir `roots.state_dir()`,
  written below as `state_dir`; plan SI-9, PD-25) first validates the value
  (a non-UTF-8 `linux_plugin_root` is `NotUtf8`, a relative or empty one is `Invalid`, so `save` never writes a file
  `load` would reject; `validate` checks UTF-8 first, so a non-UTF-8 value is `NotUtf8` even when it is also relative,
  tasks TA-7), then applies the FR-036 refusal check on the file already on disk and, only then, creates
  `state_dir` if needed,
  writes a `tempfile::NamedTempFile::new_in(state_dir)` with the file prefix `.settings.json.`, calls `sync_all` on it,
  persists it over `settings.json`, and on Unix then opens `state_dir` and calls `sync_all` on it (persist does not sync
  the directory, tempfile docs). Every step goes through an injected `SettingsIo` trait whose production implementation
  is those calls and whose test implementation can fail or abort after any step (FR-034).
- **FR-032** `load(roots: &Roots) -> LoadOutcome { settings: Settings, error: Option<SettingsError> }` (reads
  `roots.state_dir()`; plan SI-9, PD-25): no `settings.json` → defaults, no error, nothing written; a valid v1 file → its values; otherwise defaults **and** the
  named error, and the file is left byte-for-byte untouched (it is never "repaired", so an older manager never
  overwrites a newer manager's file). Stray `.settings.json.*` temp files are ignored.
- **FR-033** `SettingsError` variants, each with a stable `code()`: `Corrupt` (`E_SETTINGS_CORRUPT`: not JSON, not an
  object, a wrong type, an unknown field, a missing field), `FutureVersion { found }` (`E_SETTINGS_FUTURE_VERSION`:
  `schema_version` > 1, returned by `load` and, for the file already on disk, by `save`, FR-036), `Invalid { reason }` (`E_SETTINGS_INVALID`: `schema_version` < 1, a relative or empty
  `linux_plugin_root`, a `linux_plugin_root` with a `..` component (`reason` "linux_plugin_root is not normalised") or
  naming a filesystem root (`reason` "linux_plugin_root is a filesystem root"); plan SI-10, PD-26), `NotUtf8` (`E_SETTINGS_PATH_NOT_UTF8`: `save` given a non-UTF-8 path), `Io { op, source }`
  (`E_SETTINGS_IO`).
- **FR-034** `SettingsIo` steps, each a fault point: read existing (FR-036), create dir, create temp, write, sync temp,
  persist (rename), sync dir (Unix only; on Windows the step list has six entries, on Unix seven). A failure at any step returns `E_SETTINGS_IO` naming the step and leaves the previous `settings.json` intact; an
  abort (the test double stops without running destructors, leaving its temp file) also leaves it intact, and the next
  `load` returns the previous values.
- **FR-035** Tests inject `Settings` values directly (`AppContext` takes a `Settings`); no test reads or writes a
  settings file outside a `fresh_temp_roots()` state dir.
- **FR-036** (clarified 2026-10-30, Q4) `save` never overwrites a settings file written by a newer manager. Before any
  write it reads `settings.json` in `state_dir` through the `SettingsIo` "read existing" step and classifies it exactly
  as `load` does (FR-032, FR-033). If the file is classified `FutureVersion` (`schema_version` > 1) `save` returns
  `SettingsError::FutureVersion { found }` (`E_SETTINGS_FUTURE_VERSION`) and writes nothing: the file's bytes and mtime
  are unchanged and no temp file is created. A missing file, a valid v1 file, a `Corrupt` file and an `Invalid` file
  are all overwritten by a successful `save`. A file that exists but cannot be read fails with `E_SETTINGS_IO` naming
  the "read existing" step. D10's rejection of unknown fields is unchanged.

### E. Webview security (roadmap lines 104-110, 487; D11-D13)

- **FR-040** `src-tauri/build.rs` registers the app manifest with `commands(&["app_version", "startup_error"])` (D11).
- **FR-041** `src-tauri/capabilities/main.json` is the only capability file: `identifier` `"main"`, `windows`
  `["main"]`, `permissions` `["allow-app-version", "allow-startup-error"]`, no `remote`, no `platforms`, no `webviews`.
- **FR-042** `#[tauri::command] fn app_version() -> &'static str` returns `env!("CARGO_PKG_VERSION")`, and
  `#[tauri::command] fn startup_error(state: State<SharedState>) -> Option<&'static str>` returns
  `core::context::startup_notice(&state.read())` (FR-101); they are the only two entries of `generate_handler!`.
- **FR-043** `app.security.csp` is the D13 string; FR-044's guard and FR-045's static test hold it.
- **FR-044** `crates/core/src/navigation.rs` (Tauri-free, D12a): `pub const DEV_URL: &str = "http://localhost:1420"`;
  `pub fn app_origin(dev: bool, os: Os) -> url::Url` returning `DEV_URL` when `dev`, else `http://tauri.localhost` for
  `Os::Windows` and `tauri://localhost` otherwise; `pub fn is_app_origin(target: &url::Url, app: &url::Url) -> bool`
  (scheme, host and `port_or_known_default` equal). `src-tauri/src/navigation.rs` holds only `pub fn guard<R:
  Runtime>() -> TauriPlugin<R>`, built with `tauri::plugin::Builder::new("navigation-guard").on_navigation(…)`, whose
  closure returns `is_app_origin(target, &app_origin(tauri::is_dev(), NativePlatform.os()))`; `lib.rs` registers it
  with `.plugin(navigation::guard())`. The plugin exposes no command.
- **FR-045** A static guard test, `crates/core/tests/config_guard.rs` (in core so it links no `tauri`, D12a), reads
  `tauri.conf.json`, the three platform files, `capabilities/`, `build.rs`, `Cargo.toml` and `src/lib.rs` from
  `CARGO_MANIFEST_DIR/../../src-tauri` and asserts: FR-005's keys and values; `build.devUrl` equals
  `krate_manager_core::navigation::DEV_URL`; `src-tauri/Cargo.toml`'s `[lib]` has `test = false` and `doctest = false`
  and its `[[bin]]` `test = false`, and `src-tauri/tests/` does not exist (FR-004); `lib.rs` contains
  `.plugin(navigation::guard())`, `assemble_context(`, `.manage(`, `SharedState` and `startup_notice`,
  `startup_error`, `LOCKED_EXIT_DELAY`, `log_sink::init(` after `assemble_context(`, exactly one `set_logger` and exactly
  one `eprintln!` among its non-test files (FR-017, FR-042, FR-044, FR-101, FR-105); `src-tauri/Cargo.toml` has no `default` feature and `dev-feed = []`
  as its only feature, no `features = [...]` key naming `dev-feed` anywhere under `src-tauri/` or in `package.json`
  (FR-106, FR-107); `platform::APP_IDENTIFIER` equals `tauri.conf.json`'s `identifier` (D7); the CSP has no
  `http://` or `https://` source other than `http://ipc.localhost`, no `'unsafe-inline'`, no `'unsafe-eval'`, no `*`;
  `capabilities/` holds exactly `main.json` with FR-041's content; no file under `src-tauri/` contains the string
  `LSFileQuarantineEnabled`; no `Info.plist` exists under `src-tauri/`; the platform files set only `bundle.targets`
  to the D15 values. It also scans `apps/krate-manager/src/` and `index.html` for `@ts-ignore`, `@ts-nocheck`, an
  `eslint-disable` without ` -- <reason>`, any `http://` / `https://` URL, and the FR-052 markup sinks (`innerHTML`,
  `outerHTML`, `insertAdjacentHTML`, `document.write`), and fails on a hit (a second, lint-independent net under the
  ESLint ban).
- **FR-046** `index.html` loads only `/src/main.ts` and `/src/styles.css`, contains no external link, image or font,
  and its body is one `<main id="app">`. `src/main.ts` renders the window shell with `document.createElement` and
  `textContent`: it first calls `invoke<string | null>("startup_error")`; when that returns text, the window shows the
  title "Krate Manager" and that text (FR-101) and nothing else; otherwise it shows the title and a version line filled
  from `invoke<string>("app_version")`; on an `app_version` rejection the line reads "version unavailable". No other UI.

### F. Frontend toolchain (roadmap lines 468-469, 498, 989-991; D1-D3)

- **FR-050** `package.json`: `"private": true`, `"type": "module"`, no `version` key (D18), no `dependencies` other
  than `@tauri-apps/api` 2.12.4; `devDependencies` `@tauri-apps/cli` 2.12.4, `vite` 8.3.5, `vitest` 5.0.5, `jsdom`
  29.1.1, `typescript` 6.0.3, `eslint` 10.14.0, `@eslint/js` 10.0.1, `typescript-eslint` 8.72.1, `@types/node` 26.6.6,
  all exact versions; scripts `dev` (`vite`), `build` (`vite build`), `typecheck` (`tsc --noEmit -p tsconfig.json && tsc
  --noEmit -p tsconfig.node.json`), `lint` (`eslint . --max-warnings 0`), `test` (`vitest run`), `tauri` (`tauri`).
  `package-lock.json` is generated by `npm install` from an empty `node_modules` (npm/cli#4828), committed, and CI uses
  `npm ci`.
- **FR-051** `tsconfig.json` keeps the template's compiler options (`strict`, `noUnusedLocals`,
  `noUnusedParameters`, `noFallthroughCasesInSwitch`, `noEmit`, `moduleResolution: "bundler"`, `lib: ["ES2020",
  "DOM"]`), adds `noUncheckedIndexedAccess` and `exactOptionalPropertyTypes`, includes `src` only, and has no `types`
  entry for Node (browser code cannot reach `process` or `node:*`). `tsconfig.node.json` covers `vite.config.ts` with
  `types: ["node"]`. The template's `// @ts-expect-error` in `vite.config.ts` is deleted: `@types/node` makes it
  unnecessary.
- **FR-052** `eslint.config.mjs` (flat config, `defineConfig` from `eslint/config`): `@eslint/js` recommended,
  `tseslint.configs.strictTypeChecked` with `parserOptions.projectService: true`, `linterOptions:
  { reportUnusedDisableDirectives: "error" }`, and `no-restricted-properties` / `no-restricted-syntax` rules banning
  `innerHTML`, `outerHTML`, `insertAdjacentHTML`, `document.write`, `document.writeln`, `eval` and `new Function`;
  ignores `dist/`, `src-tauri/`, `crates/`, `target/`, `tests/fixtures/`, and the two root config files
  `eslint.config.mjs` and `vite.config.ts`. Reason: typed linting with `projectService: true` finds each file's
  nearest `tsconfig.json`, which includes `src` only (FR-051), so a root file outside it is reported "not found by the
  project service" (https://typescript-eslint.io/getting-started/typed-linting/); `allowDefaultProject` would lint
  `vite.config.ts` against default compiler options without the Node types it uses. `vite.config.ts` stays
  type-checked by `npm run typecheck` (`tsconfig.node.json`, FR-051); `eslint.config.mjs` is plain JS configuration.
  `npm run lint` on the real tree, with both files present, reports 0 problems (SC-001).
- **FR-053** `vite.config.ts` keeps the template's Tauri settings (port 1420, `strictPort`, `clearScreen: false`,
  ignore `src-tauri`) and adds the Vitest block: `environment: "jsdom"`, `include: ["src/**/*.test.ts"]`, no
  `fileParallelism: false`, no `pool` or `maxWorkers` override.
- **FR-054** `src/main.test.ts` uses `mockIPC` / `clearMocks` from `@tauri-apps/api/mocks` and covers FR-046's three
  states (version shown, version unavailable, startup notice shown).

### G. Version source (roadmap lines 491-494; Q1 line 24; D18)

- **FR-060** `tools/lint-manager-version.js` follows the `tools/*.js` form (`#!/usr/bin/env node`, header with WHY,
  usage and exit codes, `'use strict'`), uses only `node:fs` and `node:path`, exports `lint(root) -> {errors: [{code,
  file, message}]}` and `main(argv, io) -> exitCode`, and runs as `node tools/lint-manager-version.js [--root <dir>]`
  (default `apps/krate-manager` relative to the repo root). Exit 0 clean, 1 with at least one error (one stderr line
  each, `lint-manager-version: <CODE>: <file>: <message>`, in a fixed order), 2 for a usage error.
- **FR-061** `apps/krate-manager/version.json` = `{"version": "0.1.0", "name": "Krate Manager"}` (Q1: first public
  version 0.1.0).
- **FR-062** `apps/krate-manager/CHANGELOG.md` has the Keep a Changelog header (`plugins/vorago/CHANGELOG.md:1-6`
  wording, product name changed) and one entry `## [0.1.0] - Unreleased` with an `### Added` line for the scaffold.
  Phase 11's release refuses a version without a dated entry (roadmap line 798); dating it is that release's step.
- **FR-063** Error codes: `E_VERSION_FILE` (`version.json` missing, not JSON, or `version` not a string),
  `E_VERSION_FORMAT` (not the D18 `X.Y.Z`), `E_TAURI_CONF` (missing, not JSON, `version` missing or not a string),
  `E_VERSION_MISMATCH` (any checked version differs; names the file and both values), `E_VERSION_SOURCE` (a `version`
  key in a `tauri.*.conf.json` or in `package.json`), `E_CARGO_TOML` (missing file, no `[workspace.package] version`, a
  `members` entry with a glob or a missing `Cargo.toml`, a member with neither form), `E_TOML_UNSUPPORTED` (a line in
  a read section outside the D18 subset). Each error names its file. The lint checks every file and reports every
  error, not only the first.

### H. `.github/workflows/manager-ci.yml` (roadmap lines 495-501; D16, D19, D21)

- **FR-070** Triggers: `push` and `pull_request` with `paths: ['apps/krate-manager/**', '.github/workflows/manager-ci.yml',
  'tools/lint-manager-version.js']` (the lint is one of its steps), and `workflow_dispatch`. `permissions: contents:
  read`; no `secrets.*`, no `environment`; `concurrency: group: manager-ci-${{ github.ref }}`, `cancel-in-progress:
  true` (as `ci.yml:41-43`, plan SI-5). One job `check`, `name: check (${{ matrix.os }})`, `strategy.fail-fast: false`, matrix
  `os: [windows-latest, macos-latest, ubuntu-24.04]`, `defaults.run.working-directory: apps/krate-manager`, every
  `run:` step `shell: bash` with `set -euo pipefail` first (Phase 3 D13's reason: explicit bash gets `pipefail`).
- **FR-071** Steps, in order: `actions/checkout@v6`; `actions/setup-node@v4` (Node `'24'`); on Ubuntu only, `sudo
  apt-get update` and the D16 apt line; `rustup toolchain install` (installs the `rust-toolchain.toml` toolchain);
  `Swatinem/rust-cache@v2` with `workspaces: apps/krate-manager -> target`; `npm ci`; `node ../../tools/lint-manager-version.js`;
  `cargo fmt --all --check`; `cargo clippy --workspace --all-targets --locked -- -D warnings`; "clippy dev-feed"
  (`cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings`, FR-106); the clippy-guard
  step (FR-072); `cargo test --workspace --locked 2>&1 | tee "$RUNNER_TEMP/cargo-test.txt"` (never with
  `--all-targets`, which builds a test harness for `src-tauri`'s lib and bin despite `test = false` (D12a); tasks TA-2);
  `npm run typecheck`; `npm run
  lint`; `npm test -- --reporter=default --reporter=json --outputFile.json="$RUNNER_TEMP/vitest.json"`; "required
  tests ran" (FR-077); `npm run tauri build 2>&1 | tee "$RUNNER_TEMP/tauri-build.txt"` (D15 targets, no signing env)
  followed by "build warnings", which counts lines matching `^warning:` in that log and passes only at 0 (the release
  profile with `custom-protocol` is compiled only here, so `cargo clippy`'s dev-profile count does not cover it); "dev-feed
  absent" (FR-107); the per-OS artifact checks (FR-073, FR-074); "Checkout unchanged" (FR-075). Every tee target is under `$RUNNER_TEMP`,
  outside the checkout.
- **FR-072** The clippy-guard step runs `cargo clippy --manifest-path tests/fixtures/clippy-guard/Cargo.toml
  --all-targets --locked -- -D warnings` with `CARGO_TARGET_DIR` set to the workspace `target` (so the dependencies
  are reused, not rebuilt), captures exit code and output (`rc=0; … > "$RUNNER_TEMP/guard.txt" 2>&1 || rc=$?`), and
  passes only when `rc != 0`, the output holds exactly three `use of a disallowed method` diagnostics, one each naming
  `from_os`, `Roots::new` and `std::env::var` (one per FR-016 test), names `disallowed-methods` / `disallowed_methods`,
  and holds no other `error[E…]` or `error:` line apart from clippy's final "could not compile" summary. It emits a
  `::notice` or `::error` with the counts (D21).
- **FR-073** Every leg checks the built artifact's version equals `version.json`, each with a `::notice` / `::error`:
  Windows, the `ProductVersion` string of the built binary's version resource, read with `powershell -NoProfile
  -Command "(Get-Item 'target/release/krate-manager.exe').VersionInfo.ProductVersion"` (`tauri-build` writes it from
  `config.version`, `src/lib.rs:823-830`), and the NSIS file `target/release/bundle/nsis/Krate Manager_<ver>_x64-setup.exe`
  exists; macOS, `CFBundleShortVersionString` of `target/release/bundle/macos/Krate Manager.app`; Linux, `dpkg-deb -f
  <deb> Version` of the one `.deb` under `target/release/bundle/deb/`.
- **FR-074** The macOS leg (roadmap lines 499-500, probe Q6 lines 135-141), each with a `::notice` / `::error`:
  `plutil -extract LSMinimumSystemVersion raw` on the built `Info.plist` prints `15.0`; `plutil -p` output contains no
  `LSFileQuarantineEnabled`; `codesign --verify --deep --strict -v` on the `.app` exits 0; `codesign -dv` output
  contains `Signature=adhoc`.
- **FR-075** "Checkout unchanged": `test -z "$(git status --porcelain)"` from the repository root, last step on every
  leg.
- **FR-076** No step runs the built app, the NSIS installer or anything elevated; no step passes
  `--test-threads=1`, `--no-file-parallelism`, `--fileParallelism=false`, `--maxWorkers=1` or `-j 1`.
- **FR-077** "Required tests ran", after `npm test` on every leg: an inline `node -` script (the `feed-tools.yml:75-128`
  TAP-check form) reads `apps/krate-manager/tests/required-tests.json` = `{"cargo": {"all": […], "windows": […],
  "macos": […], "linux": […]}, "vitest": […]}`, picks `all` plus the key for `$RUNNER_OS` (`Windows` / `macOS` /
  `Linux`), and requires, for each cargo name, exactly one line `test <name> ... ok` or `test <name> - should panic ...
  ok` in `$RUNNER_TEMP/cargo-test.txt` (plan SI-1, PD-12; libtest's form for a `#[should_panic]` test) (so every listed
  name is unique across the workspace's test binaries), and for each Vitest title an
  `assertionResults` entry with that `title` and `status: "passed"` in `$RUNNER_TEMP/vitest.json`. It prints one
  `::notice title=required tests ran (<os>)::<n> cargo + <m> vitest` naming the count, or one `::error` per missing
  name, and exits 1 on any miss. The lists hold every test a Rust or Vitest SC names: `all` the OS-independent ones
  (SC-005 to SC-011, SC-015, SC-016, SC-018, SC-024 to SC-031), `windows` / `macos` / `linux` the `platform::<os>::tests::…`
  test of SC-002 / SC-003 / SC-004, and `vitest` SC-017's three titles. The `all` list also holds the tests of SC-025 to
  SC-031 that run in core, and the five tests the plan adds to SC-008, SC-015 and SC-027
  (`disallowed_methods_expectations_are_the_seven_d6_sites`, `real_app_metadata_passes_the_guard`,
  `planted_app_metadata_changes_fail_the_guard`, `sandboxed_roots_refuse_an_outside_configured_root`,
  `sandbox_holds_through_assemble_and_reassemble`; plan SI-11, §4.18); the PD-18 Vitest case
  `shows the version when startup_error fails` stays outside the pinned `vitest` list (plan SI-11).

### I. Tests, lane and repo plumbing (roadmap line 494; D19)

- **FR-080** `tools/feed-contract/tests/manager-ci-shape.test.js` (first statement the write-guard require; reads
  files only) asserts FR-070 to FR-076 on `manager-ci.yml` as parsed text (the triggers and path list, the matrix, the
  step order and the commands named in FR-071, the clippy-guard assertions (three diagnostics), the "build warnings"
  count, the Windows `ProductVersion` check, the macOS checks, the "required tests ran" step after `npm test` reading
  `tests/required-tests.json`, the absence of `secrets.` / `environment:` / the FR-076 flags, `permissions: contents:
  read`), that `apps/krate-manager/package.json` `scripts` carry none of the FR-076 flags, and that
  `apps/krate-manager/tests/required-tests.json` lists `platform::windows::tests::default_roots_from_env`,
  `platform::macos::tests::default_roots_from_env` and `platform::linux::tests::default_roots_from_env` under their own
  OS keys, every other Rust test name an SC of this spec names under `all`, and SC-017's three titles under `vitest`. It also asserts that `manager-ci.yml` carries the "clippy dev-feed" and
  "dev-feed absent" steps (FR-071, FR-106, FR-107). Its single top-level test is named `manager-ci.yml keeps the Phase 4 shape`.
- **FR-081** `tools/feed-contract/tests/lint-manager-version.test.js` (write-guard first; every fixture tree built in
  `fs.mkdtempSync(os.tmpdir())`) has one top-level test `lint-manager-version accepts equal versions and names every
  drift` with one subtest per SC-012 case. `feed-tools.yml`: both path lists gain `tools/lint-manager-version.js`,
  `.github/workflows/manager-ci.yml`, `apps/krate-manager/package.json` and `apps/krate-manager/tests/required-tests.json`; `PLAN` becomes `"1..59"`; `NAMES` gains the
  two test names.
- **FR-082** `tools/feed-contract/tests/hygiene.test.js` `scannedFiles()` lists `tools/lint-manager-version.js` in
  `single`, so the offline scan covers it (no network module, no `fetch(`, no `child_process`).
- **FR-083** No Rust or TypeScript test reads a real plug-in, preset or state folder, the registry, or the network:
  core and helper tests (`src-tauri` has none, D12a) get roots only from `temp_roots` / `fresh_temp_roots` (FR-015 makes any other `Roots` constructor a clippy
  error, and FR-013's sandbox makes every derivation of a test `Roots` panic outside its temp base; plan SI-8, PD-24),
  settings tests only `fresh_temp_roots().state_dir`, frontend tests only `mockIPC`.

### J. Scope fences

- **FR-090** Files outside `apps/krate-manager/**` that this phase may change: `.github/workflows/manager-ci.yml` (new),
  `.github/workflows/feed-tools.yml` (FR-081), `tools/lint-manager-version.js` (new), the two new test files,
  `tools/feed-contract/tests/hygiene.test.js` (FR-082), `specs/krate-manager-phase4/**`, in
  `specs/Krate-manager-roadmap.md` only the Phase 4 status line, line 473 (the D17 deferral of the HTTP client base and
  the launcher), line 497 (the D16 apt line) and the Phase 12 first-launch bullet's added Windows-floor sentence (Q6,
  Non-goals), and `specs/INDEX.md` (FR-091). Nothing under `plugins/`, `dsp/`, `cmake/`, `extern/`, no other workflow, no
  other `tools/` file and no `tools/feed-contract/` contract file.
- **FR-091** `specs/INDEX.md` is regenerated with `node tools/gen-specs-index.js`, so `ci.yml`'s `--check` (`:172`)
  stays green once this directory exists.

### K. Single instance, logging, live state, dev feed (clarified 2026-10-30, Q1, Q3, Q5, Q6, Q7; D17, D22-D24)

- **FR-100** `core::state_lock::StateLock::acquire(roots: &Roots) -> Result<StateLock, LockError>` (state dir
  `roots.state_dir()`, written below as `state_dir`; plan SI-9, PD-25) creates `state_dir` if absent (`create_dir_all`), opens `<state_dir>/manager.lock` (create, write, no truncate) and calls
  `std::fs::File::try_lock` on it. A lock that is already held (by this or another process) is `LockError::Held`; any
  other failure is `LockError::Io { op, source }` naming the step. The value owns the `File`; dropping it, or the
  process ending, releases the lock. The lock file is never deleted and never written. Two acquires on one state dir
  at the same time: the second is `Held`; after the first is dropped, a new acquire succeeds. `StartupError` maps
  `Held` to `E_STARTUP_LOCKED` and `Io` to `E_STARTUP_LOCK_IO` (FR-017). `krate-manager-core` adds no dependency for
  this (`std` only, FR-007). It is tested in core with two locks on one temp state dir (SC-025).
- **FR-101** A refused start shows the notice and exits. `core::context::startup_notice(state: &AppState) ->
  Option<&'static str>` returns `Some(LOCKED_NOTICE)`, `LOCKED_NOTICE = "Krate Manager is already running"`, exactly
  when `state.context` is `Err(StartupError::Locked)`, else `None` (a `Roots` or `LockIo` error stays unsurfaced until
  a later phase). The `startup_error` command (FR-042) returns it and the frontend renders it (FR-046). The locked
  instance loads no settings, reads no feed and writes nothing beyond the log line of FR-105. `src-tauri` setup, when
  `startup_notice` is `Some`, schedules `app.exit(1)` after `LOCKED_EXIT_DELAY` (a core `Duration` constant of 5
  seconds) on a spawned thread, so the refused instance ends by itself after showing the text; the window may be closed
  sooner. No native dialog and no Tauri plugin is used (cross-platform rule, FR-004).
- **FR-102** (Q3) `Roots::with_configured_plugin_root` (FR-013) returns a copy of the roots in which the entry of kind
  `Configured`, if any, is replaced by `PluginRoot { kind: Configured, path }` placed first in `plugin_roots`, or
  removed when the argument is `None`; all other entries keep kind, path and order, and the presets, `state_dir` and
  `staging_dir` are unchanged. A relative path is `RootsError::Relative { which: "configured" }`; a path with a `..`
  component, or with no normal component, is `RootsError::NotNormal { which: "configured" }` (plan SI-10, PD-26). A path equal
  (component-wise) to an existing entry's path adds no second entry; the existing entry keeps its kind.
  `assemble_context` and `reassemble` apply it with `settings.linux_plugin_root` only when the OS is `Os::Linux`; on
  Windows and macOS the stored value is ignored and no `Configured` entry exists. The `Vst3User` (`~/.vst3`) entry stays
  listed on Linux, so installs made at an earlier root stay visible while that root is still listed. The OS is an
  internal parameter (`assemble_with_os`, `reassemble_with_os`; the public functions pass `NativePlatform.os()`), so the
  Linux behaviour is tested on every host.
- **FR-103** (Q6) `core::context::SharedState` is `std::sync::RwLock<AppState>`, the one type `src-tauri` manages
  (`run()` assembles the state before the builder and registers it with `tauri::Builder::manage(SharedState::new(state))`;
  `.setup` only schedules the locked-instance exit (plan SI-7, PD-1); commands take `State<SharedState>` and read it).
  `core::context::apply_settings(shared: &SharedState, settings: Settings)` is the one way to swap in new settings: it
  takes the read guard, computes `reassemble`, drops the read guard before the write guard is taken, then stores the
  result (plan SI-2, PD-2). `core::context::
  reassemble(prev: &AppState, settings: Settings) -> AppState` rebuilds the `AppContext` for new settings with no I/O:
  same `feed_url`, same `Arc<StateLock>` (the lock is never re-taken or released), roots re-derived from the previous
  context's roots with the `Configured` entry replaced per FR-102, `settings_error: None`. A `prev` whose context is
  `Err` is returned unchanged. Settings that fail validation (a relative or empty `linux_plugin_root`) return `prev`'s
  context unchanged with `settings_error: Some(SettingsError::Invalid { .. })`. Later phases add fields to
  `AppContext` without changing the managed type; a changed Linux root applies without a restart.
- **FR-104** (Q5) Logging in core. `krate-manager-core` depends on `log` 0.4.34 (FR-007) and exposes
  `core::log_sink`: `format_line(unix_secs: u64, level: log::Level, target: &str, message: &str) -> String`
  (`<unix_secs> <LEVEL> <target>: <message>` plus LF; control characters in the message, including CR and LF, become
  spaces so every record is one line; the line is truncated to `MAX_LINE_BYTES = 4096` bytes at a character boundary)
  and `RotatingLog { open(roots: &Roots), open_with_cap(roots, cap_bytes), write_line(&mut self, &str) }` writing
  `<dir>/krate-manager.log` with `dir` = `roots.logs_dir()` (plan SI-9, PD-25), creating `dir` if absent, in append mode. `LOG_CAP_BYTES` is 1 MiB. Before a write that
  would take the file past the cap, the file is renamed over `krate-manager.log.1` (replacing any previous one) and a
  new `krate-manager.log` is started: one rotation, at most two files, neither over the cap. `Roots` exposes
  `logs_dir()` = `<state>/logs`. `core::context::log_startup_errors(&AppState)` emits one `log::error!` (target
  `krate_manager_core::startup`) per startup or settings error present, whose message contains the error's `code()`.
- **FR-105** (Q5) The sink in `src-tauri`. `src-tauri/src/log_sink.rs` holds a `log::Log` implementation over a
  `Mutex<RotatingLog>` that formats with `format_line`, ignores its own I/O errors (it never panics or blocks the
  app), filters at `Info`, and `pub fn init(roots: &Roots)` (opening `roots.logs_dir()`; plan SI-9, PD-25) calling
  `log::set_logger` and `log::set_max_level` once. `run()` keeps a clone of the `Ok` roots (`let sink_roots =
  roots.as_ref().ok().cloned();`, because `assemble_context` consumes `roots`) and calls `init` on that clone immediately
  after `assemble_context` whenever `Roots` was built (including a
  locked or lock-failed start), then `log_startup_errors`, so startup error codes land in
  `<state>/logs/krate-manager.log`. When `Roots` could not be built there is no state dir: that one code goes to
  stderr with `eprintln!`, the only `eprintln!` in `src-tauri`. Later phases log through the `log` macros and need no
  new sink. FR-045's static guard asserts the wiring (SC-030).
- **FR-106** (Q7) The `dev-feed` cargo feature. `src-tauri/Cargo.toml` declares `dev-feed = []` as its only feature and
  no `default`; no `tauri build` invocation, npm script, `tauri.conf.json` key or workflow step in this phase enables it
  except the two named `manager-ci.yml` steps of FR-071/FR-107. Under `#[cfg(feature = "dev-feed")]`, module
  `src-tauri/src/dev_feed.rs` reads `<state>/dev-feed-url.txt` (constant `DEV_FEED_FILE`) with `std::fs::read_to_string`
  and passes the text to the core function `core::context::parse_dev_feed_url(text: &str) -> Option<String>`, which
  trims surrounding whitespace and returns `Some` only for a single-line absolute `http://` or `https://` URL with a
  host and at most 2048 bytes (else `None`). Setup uses `Some(url)` in place of `FEED_URL` and logs one `warn!` naming
  it; `None` or a missing file keeps `FEED_URL`. The override adds no environment read (D6 stays seven sites, FR-015)
  and no code outside the feature: without it neither the module nor the string `dev-feed-url.txt` is compiled.
  "clippy dev-feed" (FR-071) lints the feature build on every leg.
- **FR-107** (Q7) `manager-ci.yml` step "dev-feed absent" runs after `tauri build` on every leg and passes only when
  (a) `grep -cF 'dev-feed-url.txt' src-tauri/src/dev_feed.rs` is at least 1 (the marker exists, so the check is not
  vacuous), and (b) `grep -acF 'dev-feed-url.txt'` over the built release binary (`target/release/krate-manager.exe`
  on Windows, `target/release/krate-manager` elsewhere) prints 0. It emits a `::notice` / `::error` (D21). The repo's
  shape test (FR-080) asserts that in `manager-ci.yml` the word `dev-feed` appears only in the "clippy dev-feed" and
  "dev-feed absent" steps and never on the `tauri build` line, that no `package.json` script contains it, and that
  `.github/workflows/release-manager.yml`, when it exists (Phase 11), does not contain it.

---

## Success Criteria

Rust SCs run locally on Windows with `cargo test --workspace --locked` from `apps/krate-manager/` (default test
threads); a run counts only with every binary reporting `0 failed`. Frontend SCs run with `npm test` (Vitest, parallel
files). Node SCs run with `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js"` and count only with
`# fail 0` and the FR-081 `PLAN` line. macOS and Linux rows are **PENDING-CI** on `manager-ci.yml` jobs
`check (macos-latest)` and `check (ubuntu-24.04)` (or `feed-tools.yml` `node-test (…)`), which the user's push of the
branch triggers; the step and assertion are named in each row. A Rust or Vitest PENDING-CI row closes only on that
leg's `required tests ran (<os>)` `::notice` (FR-077), whose list contains the row's test names; a green `cargo test`
step alone closes nothing (D21).

- **SC-001 (Windows local gate, zero warnings).** From `apps/krate-manager/` on this box, after `rustup update
  stable`: `cargo fmt --all --check` exits 0; `cargo clippy --workspace --all-targets --locked -- -D warnings` exits 0
  with 0 `warning:` lines (every D6 `#[expect]` fulfilled); `cargo clippy -p krate-manager --all-targets --features
  dev-feed --locked -- -D warnings` exits 0 (FR-106); `cargo test --workspace --locked` exits 0 (never with
  `--all-targets`, which builds a test harness for `src-tauri`'s lib and bin despite `test = false` (D12a); tasks
  TA-2); `npm run
  typecheck`, `npm run lint` (0 problems, `--max-warnings 0`, with `vite.config.ts` and `eslint.config.mjs` present in
  the tree and ignored per FR-052), `npm test` exit 0; `node ../../tools/lint-manager-version.js` exits 0; `npm run
  tauri build` exits 0 with 0 lines matching `^warning:` in its output (the release profile with `custom-protocol`,
  compiled only here) and produces `target/release/bundle/nsis/Krate Manager_0.1.0_x64-setup.exe`;
  `target/release/krate-manager.exe`'s `ProductVersion` is `0.1.0` (FR-073) and `grep -acF 'dev-feed-url.txt'` over it
  prints 0 (FR-107); `git status --porcelain` is empty
  afterwards. Threshold: every command exit 0, 0 warnings. (Roadmap line 504.)
- **SC-002 (Windows `default_roots`).** Test `platform::windows::tests::default_roots_from_env` (in
  `platform/windows_tests.rs`, FR-021): an `OsEnv` with absolute fixture paths under a `C:\km-fixture` prefix that is no real folder,
  `LOCALAPPDATA=C:\km-fixture\Users\u\AppData\Local`, `CommonProgramW6432=C:\km-fixture\Common Files`,
  `ProgramData=C:\km-fixture\ProgramData`, `USERPROFILE=C:\km-fixture\Users\u`, yields exactly the D8 Windows column
  over those values (vectors compared in full, order included) and `state_dir`
  `C:\km-fixture\Users\u\AppData\Local\com.krateaudio.krate-manager`, `staging_dir`
  `…\com.krateaudio.krate-manager\staging` (D7, Q2); the same
  values with lower-cased names (`localappdata`, …) give the same result; each of the four variables missing, empty or
  relative (`Local`) gives `RootsError::Env { var }` naming it. No real system folder is named, so a purity regression
  in `default_roots` cannot reach one. Threshold: exact equality, every case. Local, Windows; PENDING-CI
  `check (windows-latest)` step "required tests ran" (`windows` list). (Roadmap line 505.)
- **SC-003 (macOS `default_roots`).** Test `platform::macos::tests::default_roots_from_env`: `HOME=/Users/u` yields the
  D8 macOS column and `state_dir` `/Users/u/Library/Application Support/com.krateaudio.krate-manager`; `HOME` missing,
  empty or relative gives `RootsError::Env { var: "HOME" }`. PENDING-CI: `manager-ci.yml` `check (macos-latest)`,
  step "required tests ran" `::notice`, whose `macos` list holds this name (a mistyped `cfg` that compiled it out
  makes that step `::error`).
- **SC-004 (Linux `default_roots`).** Test `platform::linux::tests::default_roots_from_env`: `HOME=/home/u` with no
  `XDG_DATA_HOME` yields the D8 Linux column and `state_dir` `/home/u/.local/share/com.krateaudio.krate-manager`;
  `XDG_DATA_HOME=/data` gives `/data/com.krateaudio.krate-manager`; `XDG_DATA_HOME` empty or `rel/path` falls back to
  `/home/u/.local/share/com.krateaudio.krate-manager`; `HOME` missing gives `RootsError::Env { var: "HOME" }`. PENDING-CI: `check (ubuntu-24.04)`,
  step "required tests ran" `::notice` (`linux` list).
- **SC-005 (`default_roots` and `Roots::new` touch nothing).** Two tests. (a)
  `roots::tests::new_validates_and_touches_nothing` (the one D6 `Roots::new` test site): inside a `fresh_temp_roots()`
  base, `Roots::new` on a `RootPaths` naming non-existent subdirectories succeeds and the base's recursive listing is
  identical before and after; `Roots::new` rejects a relative path (`Relative`), a missing `Vst3User` root and a
  missing `Vst3Global` root (`MissingRoot { kind }`; plan SI-4, PD-4), a `staging_dir` other than `<state>/staging`
  (`Staging`), `<base>/a/../b` as a plug-in root (`NotNormal { which: "plugin_root" }`) and the filesystem root
  (`base.ancestors().last()`) as `presets.user` (`NotNormal { which: "presets.user" }`) (plan SI-10, PD-26, §7.1). (b) `platform::tests::default_roots_touches_nothing`: an
  `OsEnv` whose every D9 variable points into a `fresh_temp_roots()` base (`<base>/env/LocalAppData`,
  `<base>/env/Common Files`, `<base>/env/ProgramData`, `<base>/env/UserProfile` on Windows; `HOME=<base>/env/home`,
  `XDG_DATA_HOME=<base>/env/xdg` on Unix), passed to `NativePlatform.default_roots`, returns `Ok` and leaves the base's
  recursive listing (names, kinds, sizes) identical before and after: nothing created, nothing canonicalized into
  existence. Local, Windows; PENDING-CI on the macOS and Linux legs, step "required tests ran" (`all` list).
- **SC-006 (the clippy guard fails a test that calls the production constructor).** `manager-ci.yml` step
  "clippy-guard" on every leg: `cargo clippy` on `tests/fixtures/clippy-guard/` exits non-zero with exactly three `use
  of a disallowed method` diagnostics, one naming `from_os`, one naming `Roots::new` and one naming `std::env::var`
  (reached through `use std::env;`), the lint named, and no other error (FR-072). Local, Windows: the same command and
  assertions run by hand for the compliance record (output saved under `specs/krate-manager-phase4/evidence/`).
  PENDING-CI: `check (macos-latest)`, `check (ubuntu-24.04)`, step "clippy-guard" `::notice`. Also: the workspace
  clippy run (SC-001) is clean, which proves each of the seven D6 `#[expect]` sites is fulfilled (an unfulfilled
  expectation is a warning under `-D warnings`). (Roadmap lines 507-510.)
- **SC-007 (`temp_roots` refuses non-temp paths, and a regressed guard cannot reach the real system).** All negative
  cases run against an injected fake temp root, never against the real temp dir's parent. In a fresh
  `tempfile::TempDir` `td`, the test makes `td/fake_tmp/`, `td/fake_tmp/sub/` and a sibling `td/outside/`:
  - `test_support::tests::check_under_temp_cases`: with temp root `td/fake_tmp`, `sub` → `Ok(canonical sub)`;
    `td/outside` → `Err(Outside)`; `td/fake_tmp` itself → `Err(Outside)` (strict descendant); `td/fake_tmp/sub/../..`
    → `Err(Outside)`; a non-existent `td/fake_tmp/none` → `Err(Canonicalize)`.
  - `test_support::tests::check_under_temp_symlink_escape`: `td/fake_tmp/link -> td/outside` (`std::os::unix::fs::
    symlink`, Windows `std::os::windows::fs::symlink_dir`) passed as the base → `Err(Outside)`. When creating the link
    fails with `ERROR_PRIVILEGE_NOT_HELD` (raw OS error 1314; Windows without Developer Mode) and the `CI` environment
    variable is unset, the case returns early after an `eprintln!`; when `CI` is set (D6 site 7), that failure fails the
    test, on all three legs (`windows-latest` runs as administrator with UAC disabled, External facts, so it can make
    the link).
  - `test_support::tests::temp_roots_in_outside_panics` (`#[should_panic]`, the roadmap's case): `temp_roots_in(
    td/outside, td/fake_tmp)`, the real `temp_roots` body with the injected root.
  - `test_support::tests::temp_roots_in_refusal_creates_nothing`: `std::panic::catch_unwind` around `temp_roots_in` for
    `td/outside` and for the symlink base (when made); each is `Err`, and `td/outside`'s recursive listing is
    unchanged (empty).
  - `test_support::tests::temp_roots_on_temp_dir_itself_panics` (`#[should_panic]`): `temp_roots(&canonical_temp_dir())`
    (FR-020's strict-descendant rule on the real temp root; a regression could only write into the system temp).
  - `test_support::tests::temp_roots_inside_temp_ok`: `fresh_temp_roots()`; every root starts with
    `canonical_temp_dir()` and with the `TempDir`'s canonical path; on macOS the `/var` → `/private/var`
    canonicalization is exercised here.
  Threshold: every case. Local, Windows (compliance records whether the symlink case ran, from `cargo test -p
  krate-manager-core check_under_temp_symlink_escape -- --nocapture`); PENDING-CI on all three legs, step "required
  tests ran" (`all` list, all six names). (Roadmap line 511.)
- **SC-008 (no production root or URL in core).** Test `crates/core/tests/injection.rs` (FR-021) passes; its scanner
  function, run on in-memory source strings, reports each of: `"C:\\Program Files"` outside `platform/`,
  `krateaudio.com` outside `platform/`, `std::fs::metadata(p)` and `p.canonicalize()` in a `platform/` file, a
  non-allow-listed path literal in a `platform/` file, and `mod tests {` in a scanned file (so no tracked file is
  edited to prove it). The same test parses `crates/core/Cargo.toml` and asserts `[dependencies]` is exactly the
  FR-007 set (`serde`, `serde_json`, `thiserror`, `tempfile`, `url`, `log`), and
  `disallowed_methods_expectations_are_the_seven_d6_sites` passes (the D6 count is exactly seven, per file as the plan
  §4.1 table, with no lint-group suppression of `clippy::style`, `clippy::all` or `warnings`; a planted eighth site and
  a planted group `expect` are each reported; plan SI-11, PD-22). Environment reads are SC-006's (clippy). Compliance records `cargo tree -p krate-manager-core -e normal`
  with no `tauri`, `dirs`, `directories` or `home` crate in it. Local; PENDING-CI on both legs, step "required tests
  ran" (`all` list).
- **SC-009 (settings round-trip and per-OS defaults).** Tests in `settings::tests` (in `settings_tests.rs`): `save`
  then `load` returns equal values for `{None,false}`, `{Some("/home/u/custom"), true}` (on Windows
  `{Some("C:\\vst"), true}`), all inside a `fresh_temp_roots()` state dir; the written file is exactly the D10 bytes
  with a final LF; a missing file yields defaults, `error: None`, and leaves the state dir empty;
  `linux_plugin_root_or_default` returns the `Vst3User` root for `None`. Local; PENDING-CI on both legs, step
  "required tests ran" (`all` list). (Roadmap line 512.)
- **SC-010 (rejections keep defaults and the file).** `load` of each of: `schema_version: 2`
  (`E_SETTINGS_FUTURE_VERSION`), `schema_version: 0`, a relative `linux_plugin_root` and `linux_plugin_root: ""`
  (`E_SETTINGS_INVALID`), not JSON, a JSON array, a string `linux_graphics_workaround`, an unknown field
  `install_scope`, a missing `schema_version` (`E_SETTINGS_CORRUPT`) returns `Settings::default()` with that code, and
  the file's bytes and mtime are unchanged afterwards. `save` of a non-UTF-8 `linux_plugin_root` (Unix:
  `OsStr::from_bytes(b"/x\xff")`; Windows: `OsString::from_wide(&[0xD800])`, an unpaired surrogate) returns
  `E_SETTINGS_PATH_NOT_UTF8`, and `save` of a relative or empty one returns `E_SETTINGS_INVALID`; a
  `linux_plugin_root` with a `..` component and one naming the filesystem root give `E_SETTINGS_INVALID` for both
  `load` (defaults, file unchanged) and `save` (nothing written) (plan SI-10, PD-26, §7.1); in each `save` case
  the state dir's recursive listing is unchanged (nothing written, not even the directory). Q4 cases (FR-036):
  `save` over a planted `settings.json` with `schema_version: 2` (also one that is otherwise corrupt, e.g. with an
  unknown field) returns `E_SETTINGS_FUTURE_VERSION`, the file's bytes and mtime are unchanged and the state dir's
  listing is unchanged (no temp file); `save` over a `Corrupt` file (not JSON) and over an `Invalid` file
  (`schema_version: 0`) succeeds and the file then holds the D10 bytes of the saved value; `save` over a valid v1 file
  and into an empty state dir succeeds. Threshold: every case. Local; PENDING-CI on both legs, step "required tests
  ran" (`all` list). (Roadmap line 513.)
- **SC-011 (crash between temp write and rename).** With the fault-injecting `SettingsIo` over a state dir holding a
  valid previous file: a failure at each of read-existing, create-dir, create-temp, write, sync-temp, persist and (Unix)
  sync-dir returns `E_SETTINGS_IO` naming the step (for read-existing nothing at all is written: no temp file, the
  listing is unchanged), and `settings.json` is byte-identical to before (for sync-dir, which runs
  after the rename, it holds the new bytes and is a complete file); an abort after write and after sync-temp (temp
  left behind) leaves `settings.json` byte-identical and the next `load` returns the previous values, ignoring the
  stray `.settings.json.*`. Threshold: every step. Local; PENDING-CI on both legs (POSIX rename and directory sync),
  step "required tests ran" (`all` list). (Roadmap line 513.)
- **SC-012 (version lint).** `lint-manager-version accepts equal versions and names every drift` over temp fixture
  trees: the all-equal tree (literal member versions and `version.workspace = true` members) → exit 0; then one
  subtest each, with the exact first stderr line asserted: `tauri.conf.json` differing (`E_VERSION_MISMATCH`), the
  root `[workspace.package]` version differing, and one subtest per workspace member with a differing literal
  version, `crates/core/Cargo.toml`, `crates/helper/Cargo.toml` and `src-tauri/Cargo.toml` (each
  `E_VERSION_MISMATCH` naming that file), missing `version.json` (`E_VERSION_FILE`),
  `version` `"0.1"`, `"v0.1.0"`, `"0.1.0-beta.1"`, `"01.0.0"` (`E_VERSION_FORMAT`), `version` in
  `tauri.macos.conf.json` and in `package.json` (`E_VERSION_SOURCE`), a glob in `members` (`E_CARGO_TOML`), a member
  with no version line (`E_CARGO_TOML`), an inline-table `version = { workspace = true }` form (`E_TOML_UNSUPPORTED`),
  two independent drifts at once (both reported, exit 1), an unknown argument (exit 2 with the usage line).
  Threshold: every case. Local; PENDING-CI `feed-tools.yml` `node-test (macos-latest | ubuntu-latest)`. The real tree
  passes: `manager-ci.yml` step "version lint" on all three legs. (Roadmap lines 515-516.)
- **SC-013 (built macOS bundle: minimum OS, no quarantine key, ad-hoc signature).** PENDING-CI, `manager-ci.yml`
  `check (macos-latest)`, steps "plist: LSMinimumSystemVersion" (`15.0`), "plist: no LSFileQuarantineEnabled",
  "codesign verify" (exit 0), "codesign ad-hoc" (`Signature=adhoc`), each a `::notice`. (Roadmap lines 499-500, 516-517;
  probes lines 11, 17.)
- **SC-014 (built artifact versions).** FR-073 on all three legs, each its own `::notice`: Windows `ProductVersion`
  `0.1.0` of `target/release/krate-manager.exe` plus the NSIS file, local (SC-001) and `check (windows-latest)` step
  "artifact version"; macOS `CFBundleShortVersionString` `0.1.0` and Linux `dpkg-deb -f … Version` `0.1.0` PENDING-CI,
  step "artifact version" on `check (macos-latest)` / `check (ubuntu-24.04)`.
- **SC-015 (static app security).** `crates/core/tests/config_guard.rs` (FR-045) passes; each of these planted
  changes, applied to an in-memory copy of the parsed config inside the test, makes the corresponding assertion fail: a
  CSP with `https://cdn.example`, `connect-src *`, `script-src 'self' 'unsafe-inline'`; a second capability permission
  `core:default`; a `remote` block; `installMode: "perMachine"`; `signingIdentity` absent; `minimumSystemVersion`
  `"10.13"`; a `LSFileQuarantineEnabled` string; a `devUrl` of `http://localhost:1421`; a `src-tauri/Cargo.toml` text
  without `test = false`; a `src-tauri/Cargo.toml` text with `default = ["dev-feed"]`; a capability without
  `allow-startup-error`; an `APP_IDENTIFIER` unequal to the config's `identifier`; a `lib.rs` text without
  `.plugin(navigation::guard())`, without `SharedState`, without `startup_notice`, without `LOCKED_EXIT_DELAY` or
  with a second `eprintln!`; and, for the source scan, an
  in-memory `src` file holding `el.innerHTML = s`, `// @ts-ignore`, `// eslint-disable-next-line` with no reason, and
  `https://cdn.example/x.js` (each reported). `real_app_metadata_passes_the_guard` and
  `planted_app_metadata_changes_fail_the_guard` (plan SI-11, PD-23): the real manifests, `package.json`,
  `tsconfig.json`, `eslint.config.mjs` and `CHANGELOG.md` pass `check_app_meta`, and each planted manifest, dependency,
  `package.json`, `tsconfig.json`, ESLint and CHANGELOG mutation of plan §4.10 fails it. Local; PENDING-CI on both legs, step "required tests ran" (`all` list).
- **SC-016 (navigation guard and app origin).** `navigation::tests::is_app_origin_cases` (in
  `crates/core/src/navigation_tests.rs`): `tauri://localhost/index.html` against `tauri://localhost` → true;
  `http://tauri.localhost/x` against `http://tauri.localhost` → true; `http://localhost:1420/` against `DEV_URL` →
  true; `https://krateaudio.com/`, `http://tauri.localhost.evil.com/`, `http://localhost:1421/`, `file:///etc/passwd`,
  `tauri://evil/` → false. `navigation::tests::app_origin_cases`: `app_origin(true, os)` is `DEV_URL` for each `Os`;
  `app_origin(false, Os::Windows)` is `http://tauri.localhost`; `app_origin(false, Os::Macos)` and `(false,
  Os::Linux)` are `tauri://localhost`. Threshold: every case. Both run in core's test binary, which links no `tauri`
  (D12a). Local; PENDING-CI on both legs, step "required tests ran" (`all` list). The registration of the guard on the
  builder is SC-015's `lib.rs` assertion.
- **SC-017 (frontend).** Vitest `src/main.test.ts`, three titled tests: with `mockIPC` answering `startup_error` with
  `null` and `app_version` with `"0.1.0"`, the window shows the title and `0.1.0`, and `mockIPC` recorded exactly the
  commands `startup_error` then `app_version`; with `app_version` throwing, it shows "version unavailable"; with
  `startup_error` answering `"Krate Manager is already running"` (FR-101), the window shows the title and exactly that
  text, no version line, and `mockIPC` recorded only `startup_error`. Local; PENDING-CI on both legs, step "required
  tests ran" (`vitest` list, all three titles `passed`). The markup-sink ban is proven by SC-015's planted-string case (FR-045's source scan), independent of
  ESLint.
- **SC-018 (helper stub).** Test `crates/helper/tests/stub.rs`: the built binary (`env!("CARGO_BIN_EXE_krate-manager-helper")`)
  run with no argument and with `--anything` exits 2 and prints the one D20 line. Local; PENDING-CI on both legs, step
  "required tests ran" (`all` list).
- **SC-019 (CI shape).** `manager-ci.yml keeps the Phase 4 shape` (FR-080) passes; and it fails on a scratch copy with
  any of: an `ubuntu-latest` leg, a missing clippy-guard step, `cargo test -- --test-threads=1`, a `secrets.` reference,
  `permissions: contents: write`, the macOS codesign step removed, the "required tests ran" step removed, the "clippy dev-feed" step removed, the "dev-feed absent" step removed, a
  `dev-feed` on the `tauri build` line, and a
  `required-tests.json` without `platform::macos::tests::default_roots_from_env` under `macos`. Local; PENDING-CI
  `feed-tools.yml` on all legs.
- **SC-020 (builds leave the checkout clean).** `manager-ci.yml` step "Checkout unchanged" passes on all three legs
  (PENDING-CI); SC-001's final `git status --porcelain` is empty locally.
- **SC-021 (tests in parallel).** No `--test-threads`, `--no-file-parallelism`, `fileParallelism: false`, `pool` /
  `maxWorkers` override anywhere under `apps/krate-manager/` or in `manager-ci.yml` (FR-080's scan plus a grep over
  `apps/krate-manager/` recorded in compliance). (Roadmap line 514.)
- **SC-022 (feed-tools lane).** `node --test` locally: `# fail 0`, plan `1..59`, both new names present as `ok`; the
  hygiene scan covers `tools/lint-manager-version.js`. PENDING-CI: `feed-tools.yml` `node-test (ubuntu-latest |
  macos-latest | windows-latest)` green.
- **SC-023 (scope fence).** The modified **and untracked** paths outside `apps/krate-manager/`
  (`git diff --name-only c66c8557 -- . ':!apps/krate-manager'` plus `git ls-files --others --exclude-standard -- .
  ':!apps/krate-manager'`) are only FR-090 paths, and the `git diff -U0 c66c8557 -- specs/Krate-manager-roadmap.md`
  hunks touch only the four FR-090 places (the Phase 4 status line, line 473, line 497 and the Phase 12 first-launch
  bullet) (tasks TA-5, plan §4.19); `node tools/gen-specs-index.js --check` exits 0.
- **SC-024 (startup assembly creates only the state dir and the lock file).** `context::tests::assemble_cases` (in `crates/core/src/context_tests.rs`):
  (a) with `e = NativePlatform.default_roots(&OsEnv::from_pairs([])).unwrap_err()` (an empty `OsEnv`, so
  `default_roots` fails and no `Roots` is ever built), `assemble_context(Err(e), "http://127.0.0.1:9/versions.json")` gives `context: Err(StartupError::Roots(
  RootsError::Env { var }))` naming the first required D9 variable for the OS (`LOCALAPPDATA` on Windows, `HOME` on
  macOS and Linux), `code()` `E_STARTUP_ROOTS`, `settings_error: None`, `lock: None`; (b) `assemble_context(Ok(roots), url)` with
  `fresh_temp_roots()` gives `Ok(AppContext)` whose roots equal the input (no `Configured` entry, default settings),
  settings `Settings::default()`, `feed_url` equal to `url`, `settings_error: None`, `lock: Some(_)`, and the base's
  recursive listing differs from before by exactly one new file, `state/manager.lock` (no other directory or file
  created; the state dir itself already exists in `fresh_temp_roots()`); (b2) the same on a base whose state dir is
  then removed gives `Ok` and the listing holds exactly `state/` and `state/manager.lock`; (c) the same with a corrupt
  `settings.json` planted in the state dir gives `Ok` with default settings, `settings_error` code
  `E_SETTINGS_CORRUPT`, and the file byte-identical. The test never builds roots from
  the OS. Threshold: every case. Local; PENDING-CI on both legs, step "required tests ran" (`all` list). The setup glue
  calling it is SC-015's `lib.rs` assertion.
- **SC-025 (StateLock, Q1).** Tests in `state_lock::tests` (`state_lock_tests.rs`), over a `fresh_temp_roots()` state
  dir: `second_acquire_is_held_while_first_lives` (`StateLock::acquire` twice: the first `Ok`, the second
  `Err(LockError::Held)`; after dropping the first, a third `Ok`); `acquire_creates_state_dir_and_lock_file_only`
  (state dir removed first: after `acquire` the listing is exactly `state/` and a zero-byte `state/manager.lock`); the
  lock file is not deleted by dropping the lock; `acquire_io_error_is_named` (`state_dir` naming an existing regular file
  gives `Err(LockError::Io)` naming the step). Threshold: every case. Local; PENDING-CI on both legs (POSIX `flock`
  versus Windows `LockFileEx` semantics), step "required tests ran" (`all` list).
- **SC-026 (a second instance is refused, Q1).** `context::tests::assemble_locked_instance`: with a `StateLock` held on
  the state dir and a corrupt `settings.json` planted, `assemble_context(Ok(roots), url)` gives `context:
  Err(StartupError::Locked)` with `code()` `E_STARTUP_LOCKED`, `settings_error: None` (proof that settings were not
  loaded), `lock: None`, the settings file byte-identical and no new file. `context::tests::startup_notice_cases`:
  `startup_notice` is `Some("Krate Manager is already running")` for that state and `None` for a healthy state, for a
  `Roots` error state and for an `E_STARTUP_LOCK_IO` state; `LOCKED_EXIT_DELAY` equals 5 seconds. The three-state
  frontend is SC-017; the `lib.rs` wiring (`startup_notice`, `LOCKED_EXIT_DELAY`) is SC-015. Threshold: every case.
  Local; PENDING-CI on both legs, step "required tests ran" (`all` list).
- **SC-027 (the configured Linux root is folded into `Roots`, Q3).** `context::tests::configured_root_cases` and
  `roots::tests::with_configured_plugin_root_cases`, with an absolute path `X` inside a `fresh_temp_roots()` base and the
  OS injected (FR-102): Linux with `linux_plugin_root = Some(X)` gives `plugin_roots[0] == {Configured, X}` and the
  rest equal to the input in order, `Vst3User` included; Linux with `None` equals the input; Windows and macOS with
  `Some(X)` equal the input (ignored); `X` equal to the `Vst3User` path adds no entry; two successive calls with
  different roots leave only the last `Configured` entry; a relative root gives `RootsError::Relative`;
  `<vst3-user>/../vst3-user` gives `RootsError::NotNormal { which: "configured" }`, so no duplicate entry (plan SI-10,
  PD-26); presets, `state_dir`, `staging_dir` unchanged in every case. Sandbox case (plan SI-8, SI-11, PD-24):
  `roots::tests::sandboxed_roots_refuse_an_outside_configured_root` and
  `context::tests::sandbox_holds_through_assemble_and_reassemble`: a sandboxed `Roots` (from `fresh_temp_roots()`)
  given a configured root outside its temp base panics, directly through `with_configured_plugin_root`, through
  `assemble_with_os` reading a planted `settings.json`, and through `reassemble_with_os`. Threshold: every case. Local;
  PENDING-CI on both legs, step "required tests ran" (`all` list).
- **SC-028 (live reassembly, Q6).** `context::tests::reassemble_cases`: from an assembled state (Linux injected,
  `fresh_temp_roots()`), `reassemble` with new settings gives a context whose roots carry the new `Configured` entry
  first, the new settings, an equal `feed_url`, and the same lock (`Arc::ptr_eq`; a further `StateLock::acquire` on the
  state dir is still `Held`), and the base's recursive listing is identical before and after (no I/O); reassembling to
  `linux_plugin_root: None` removes the entry; a relative root returns the previous context unchanged with
  `settings_error` code `E_SETTINGS_INVALID`; an `Err` context comes back unchanged; after `context::apply_settings(&shared,
  s)` on a `SharedState` (which drops the read guard before taking the write guard; plan SI-2, PD-2), a following
  `shared.read()` sees the new roots. Threshold: every
  case. Local; PENDING-CI on both legs, step "required tests ran" (`all` list).
- **SC-029 (the log writer, Q5).** `log_sink::tests::format_line_cases` (one line per record, LF-terminated, CR / LF /
  tab in the message replaced by spaces, level and target present, a 10 000-byte message truncated so the line is at most
  `MAX_LINE_BYTES` and still valid UTF-8 when cut inside a multi-byte character) and
  `log_sink::tests::rotating_log_cases` (over a `fresh_temp_roots()` logs dir, absent at first, with `open_with_cap` of
  200 bytes: the dir is created; after enough lines the dir holds exactly `krate-manager.log` and `krate-manager.log.1`;
  each is at most 200 bytes; the newest line is in `krate-manager.log`; a third rotation replaces `.1` with the second
  generation's content; no other file appears; reopening appends rather than truncates; a line longer than the cap
  leaves no file over the cap, both files valid UTF-8; a failed rotation rename (`krate-manager.log.1` a directory)
  returns `Err`, writes nothing and leaves the log byte-identical; plan SI-11, PD-10). Threshold: every case. Local;
  PENDING-CI on both legs, step "required tests ran" (`all` list).
- **SC-030 (startup error codes are logged, Q5).** `context::tests::startup_errors_are_logged`: with a capture logger
  installed once in that test (the only test that installs one) and records filtered to target
  `krate_manager_core::startup`, `log_startup_errors` emits one Error record containing `E_STARTUP_ROOTS` for a `Roots`
  error state, one containing `E_STARTUP_LOCKED` for a locked state, one containing `E_SETTINGS_CORRUPT` for a state
  with a corrupt `settings.json`, and none for a healthy state. The wiring is SC-015's `lib.rs` assertions
  (`log_sink::init(` after `assemble_context(`, one `set_logger`, one `eprintln!`) and the sink path is
  `Roots::logs_dir()` = `<state>/logs` (asserted in `roots::tests`). Threshold: every case. Local; PENDING-CI on both
  legs, step "required tests ran" (`all` list).
- **SC-031 (the dev-feed override stays out of releases, Q7).** `context::tests::parse_dev_feed_url_cases`:
  `"http://127.0.0.1:8080/versions.json\n"` gives `Some("http://127.0.0.1:8080/versions.json")`, `"https://example.invalid/v.json"`
  gives `Some`; `""`, `"   "`, `"ftp://x/y"`, `"file:///x"`, `"not a url"`, `"http://"`, a two-line text and a text
  over 2048 bytes give `None`. `manager-ci.yml` steps "clippy dev-feed" and "dev-feed absent" (FR-071, FR-107) on every
  leg, each a `::notice` (PENDING-CI on `check (macos-latest)` and `check (ubuntu-24.04)`; Windows local via SC-001's
  `grep -acF` over `target/release/krate-manager.exe` = 0 and `cargo clippy ... --features dev-feed` exit 0). The static
  part is SC-015's (`default` feature absent, `dev-feed = []` only) and SC-019's shape test. Threshold: every case.
  Local; PENDING-CI on both legs, step "required tests ran" (`all` list) for the Rust test.

---

## Safety

- **Injected roots.** Every root (plug-in roots, preset roots, state dir, staging dir) reaches code only through a
  `Roots` value; `core` cannot construct one from the OS at all (FR-011, FR-021), production builds the one instance
  in `src-tauri` setup (FR-014, FR-017), and tests can only use `temp_roots` / `fresh_temp_roots`, which panic outside
  the temp dir (FR-020) and whose negative tests use an injected fake temp root inside a fresh `TempDir`, so a
  regressed guard cannot write outside it (SC-007). Calling either constructor, or reading the process environment,
  anywhere else is a clippy error under `-D warnings`, proven by a fixture that must fail (FR-015, FR-016, SC-006).
  Test vectors name only fixture paths that are not a real system or user folder here or on the hosted runners
  (`C:\km-fixture\…`, `/Users/u`, `/home/u`; the runners' user is `runner`) or paths inside a fresh `TempDir`
  (SC-002, SC-005), and `default_roots` is pure in any case (FR-011). The feed URL is a field of `AppContext` whose production value is a
  constant in `src-tauri` (D17); the HTTP base and the elevation launcher join the same struct in Phases 5 and 10.
- **What is verified before extraction or execution.** Nothing is downloaded, extracted or executed in this phase.
- **The elevated helper.** A stub that does nothing and exits 2 (D20). It is not bundled, not launched, and has no
  operation.
- **The webview.** One capability granting window `main` only `allow-app-version` and `allow-startup-error`; every app command is ACL-checked
  because an app manifest exists (D11); no plugin, no `core:*` permission, no `remote`; a CSP with no remote origin
  (D13); a navigation guard (D12); markup sinks banned by lint (FR-052). The macOS bundle never gains
  `LSFileQuarantineEnabled` (FR-045, SC-013).
- **No certificate, no key.** `signingIdentity: "-"` (ad-hoc); no step reads a signing secret; `manager-ci.yml` uses
  no secret and no environment (FR-070).
- **What tests must never touch.** Any real VST3, Components or preset folder, `ProgramData`, the registry, the user's
  real `%LOCALAPPDATA%\com.krateaudio.krate-manager` or `~/Library/Application Support/com.krateaudio.krate-manager`
  (the real state dir and its `manager.lock` and `logs/`), the network. No test launches the
  app (which would create WebView2 user data on Windows) or the NSIS installer. The local `tauri build` (SC-001) is a
  build, not a test: it downloads the SHA-1-pinned NSIS toolset from GitHub into `%LOCALAPPDATA%\tauri` once
  (`tauri-bundler` `nsis/mod.rs:38-44, 82-90`), writes its outputs under `apps/krate-manager/target/`, and installs
  nothing.

---

## Edge cases

| Case | Behaviour | Where |
|---|---|---|
| Interrupted download | Not applicable: nothing is downloaded | Phase 7 |
| Disk full / write error while saving settings | `E_SETTINGS_IO` naming the step; the previous `settings.json` intact (temp written beside it, renamed only after a successful sync) | FR-031, FR-034, SC-011 |
| Crash between temp write and rename | Previous file intact; stray `.settings.json.*` ignored on load | FR-032, FR-034, SC-011 |
| `settings.json` locked by another process on Windows (antivirus) | The rename fails → `E_SETTINGS_IO` at `persist`, previous file intact | FR-034 |
| A second manager instance on the same state dir | `E_STARTUP_LOCKED`: it loads nothing, shows "Krate Manager is already running" and exits after `LOCKED_EXIT_DELAY`; two instances never save settings concurrently | FR-100, FR-101, SC-025, SC-026 |
| The lock file left behind by a crashed instance | Harmless: the OS released the lock with the process, the file is reused and never deleted | FR-100, SC-025 |
| A stale `Configured` Linux root that no longer exists | Kept in `plugin_roots` (Phase 6 decides what a missing folder means); `Roots` touches no filesystem | FR-102 |
| The log file grows | At the 1 MiB cap it rotates once to `krate-manager.log.1`, replacing the older one; at most two files | FR-104, SC-029 |
| A `dev-feed-url.txt` in a release build's state dir | Ignored: neither the reading code nor the file name exists in a release binary | FR-106, FR-107, SC-031 |
| A newer manager's `settings.json` meets an older manager's `save` | `E_SETTINGS_FUTURE_VERSION`, nothing written | FR-036, SC-010 |
| File locked by a running DAW (Windows) | Not applicable: nothing is installed | Phase 10 |
| Partial install | Not applicable | Phases 9-10 |
| Downgrade attempt: an older manager reads a newer manager's settings | `E_SETTINGS_FUTURE_VERSION`, defaults used, the file never rewritten by load, and `save` refuses to overwrite it | FR-032, FR-036, SC-010 |
| Corrupt settings file | `E_SETTINGS_CORRUPT`, defaults used, file untouched | FR-033, SC-010 |
| Corrupt feed / offline / feed `schema_version` 1 vs 2 | Not applicable: no feed is fetched; the feed URL is only stored | Phase 5 |
| Bundles that still say `0.8.0` | Not applicable: no bundle is read | Phase 6 |
| Required environment variable missing (`HOME` unset, a stripped Windows environment) | `RootsError::Env { var }`, no guessed path; `AppState.context` is `Err(StartupError::Roots(…))` (no lock, no log sink: the code goes to stderr), the window opens and later phases show the error | FR-012, FR-017, FR-105, SC-002-SC-004, SC-024 |
| `XDG_DATA_HOME` relative or empty | Ignored per XDG 0.8; `$HOME/.local/share` used | FR-012, SC-004 |
| Windows environment names in another case | Matched case-insensitively | FR-012, SC-002 |
| 32-bit vs 64-bit view of Common Files | `CommonProgramW6432` names the 64-bit folder for either | D8 |
| macOS temp dir behind `/var` → `/private/var`; Windows runner temp with an 8.3 short name | Both sides canonicalized in `temp_roots` | FR-020, SC-007 |
| A Linux plug-in root that is not UTF-8 | `save` refuses with `E_SETTINGS_PATH_NOT_UTF8` before any IO step (the file is JSON) | FR-031, FR-033, SC-010 |
| `frontendDist` missing when running `cargo test` / `clippy` | Not an error: dev mode embeds nothing (D14); only `tauri build` builds `dist/` first | D14 |
| A new Rust stable adds a clippy lint | `manager-ci` turns red on unchanged code; fixed in code, never with an `allow` (D4) | D4 |
| `package-lock.json` regenerated with `node_modules` present on one OS | Other OSes' optional native packages can drop out; regenerate from an empty `node_modules`; `npm ci` on three legs catches it | FR-050 |
| A Tauri macro expansion (`generate_handler!`, `generate_context!`) carrying its own `#[allow]` | Clippy does not lint code from external macros; if `allow_attributes` ever fires on one, the build stops and the plan decides with the user (no workspace-wide `allow`) | D6, FR-002 |
| A mistyped `clippy.toml` path | clippy itself reports it only as a non-fatal `does not refer to a reachable function` warning (exit 0 under `-D warnings`), or not at all when the path's first segment is a crate the linted crate does not depend on (e.g. `krate_manager_lib::…` seen from `crates/core`). The detectors are the D6 `#[expect]` sites, which become `unfulfilled_lint_expectations` warnings that `-D warnings` fails (SC-001), and the clippy-guard fixture, which must show all three diagnostics (FR-072, SC-006) | FR-015, D6 |
| A test binary that links `tauri` fails to load on Windows (`STATUS_ENTRYPOINT_NOT_FOUND`, tauri#13419) | Avoided by construction: no workspace test binary links `tauri` (`src-tauri` has `test = false` / `doctest = false` and no tests; navigation, startup assembly and the config guard are tested in `crates/core`); the clippy-guard fixture is linted, never run | D12a, FR-004 |
| A `#[cfg(target_os = …)]` typo compiles a per-OS test out, and `cargo test` stays green | Step "required tests ran" fails that leg by name (FR-077) | D21, FR-077 |
| Windows box without Developer Mode cannot create a symlink | The symlink case returns early locally (recorded in compliance); with `CI` set it fails instead, so every hosted leg runs it | SC-007 |
| A second version source added (a `version` in `package.json` or a platform conf file) | `E_VERSION_SOURCE` | FR-063, SC-012 |

---

## New components

| Path | Purpose |
|---|---|
| `apps/krate-manager/Cargo.toml`, `Cargo.lock`, `clippy.toml`, `rust-toolchain.toml`, `.gitignore` | Workspace, lockfile, the two disallowed constructors, the stable toolchain, ignores (FR-001 to FR-003, FR-015, D4) |
| `apps/krate-manager/version.json`, `CHANGELOG.md` | Single version source 0.1.0, changelog with an unreleased 0.1.0 entry (FR-061, FR-062) |
| `apps/krate-manager/package.json`, `package-lock.json`, `tsconfig.json`, `tsconfig.node.json`, `vite.config.ts`, `eslint.config.mjs`, `index.html`, `src/main.ts`, `src/styles.css`, `src/main.test.ts` | Vanilla TS UI shell, strict typecheck, ESLint, Vitest (FR-046, FR-050 to FR-054) |
| `apps/krate-manager/crates/core/` (`krate-manager-core`): `src/lib.rs`, `platform/{mod,windows,macos,linux}.rs` with `platform/{mod,windows,macos,linux}_tests.rs`, `roots.rs` + `roots_tests.rs`, `settings.rs` + `settings_tests.rs`, `context.rs` + `context_tests.rs`, `state_lock.rs` + `state_lock_tests.rs`, `log_sink.rs` + `log_sink_tests.rs`, `navigation.rs` + `navigation_tests.rs`, `test_support.rs` + `test_support_tests.rs`, `tests/injection.rs`, `tests/config_guard.rs` | `Platform`, `OsEnv`, `RootPaths`, `Roots`, `Settings` + atomic persistence, `AppContext` / `AppState` / `assemble_context`, `app_origin` / `is_app_origin`, `temp_roots` / `check_under_temp`, the injection scan, the static config guard (FR-007, FR-010 to FR-013, FR-017, FR-020 to FR-022, FR-030 to FR-035, FR-044, FR-045) |
| `apps/krate-manager/crates/helper/` (`krate-manager-helper`): `src/lib.rs`, `src/main.rs`, `tests/stub.rs` | The D20 stub (FR-006, SC-018) |
| `apps/krate-manager/src-tauri/` (`krate-manager`): `Cargo.toml` (`test = false`), `build.rs`, `tauri.conf.json`, `tauri.{windows,macos,linux}.conf.json`, `capabilities/main.json`, `icons/`, `src/{main,lib,os_roots,commands,navigation}.rs`; no `tests/` | The thin shell: `Roots::from_os()`, `FEED_URL`, setup glue, `app_version`, `startup_error`, `log_sink.rs` (the `log::Log` glue), `dev_feed.rs` (feature `dev-feed` only), the navigation-guard plugin (FR-004, FR-005, FR-014, FR-017, FR-040 to FR-044) |
| `apps/krate-manager/tests/required-tests.json` | Per-leg list of test names the "required tests ran" step checks (FR-077) |
| `apps/krate-manager/tests/fixtures/clippy-guard/` (`Cargo.toml`, `Cargo.lock`, `src/lib.rs`) | Must fail clippy with three `disallowed_methods` diagnostics (FR-016, SC-006) |
| `.github/workflows/manager-ci.yml` | Three-leg fmt / clippy / clippy-guard / test / typecheck / lint / vitest / `tauri build` / macOS plist + codesign gates (FR-070 to FR-076) |
| `tools/lint-manager-version.js` | `version.json` vs `tauri.conf.json` vs workspace `Cargo.toml` lint (FR-060, FR-063) |
| `tools/feed-contract/tests/lint-manager-version.test.js`, `tools/feed-contract/tests/manager-ci-shape.test.js` | SC-012, SC-019 (FR-080, FR-081) |

---

## Traceability

| Roadmap statement (line) | FR / SC |
|---|---|
| Tauri 2 from `create-tauri-app` (Vite + TS); spec picks UI framework and linter (468-469) | D1, D2, FR-001, FR-050 to FR-053 |
| Cargo workspace, empty but wired command surface (470) | D11, FR-002, FR-004, FR-040 to FR-042, SC-017 |
| `Platform` with three `cfg` implementations; `Roots` always passed in (471-473) | FR-010 to FR-013, SC-002 to SC-005 |
| Feed URL, HTTP base, elevation launcher injected the same way (473) | Feed URL: D17, FR-017, FR-021, SC-008, SC-024. HTTP client base: deferred to Phase 5 (roadmap 526). Elevation launcher: deferred to Phase 10 (roadmap 729-731). Departure recorded under "Additions and departures"; the plan edits line 473 (FR-090) |
| Guard (1): no OS-root constructor in core; `default_roots()` pure (474-475) | FR-011, FR-015 (environment reads disallowed), FR-021, SC-005, SC-006, SC-008 |
| Guard (2): `Roots::from_os()` in `src-tauri`, `disallowed_methods`, one allowed call site with a reason (475-480) | D6, FR-014, FR-015, SC-006 |
| Guard (3): `temp_roots()` panics outside `temp_dir()` (480-481) | FR-020, FR-015 (`Roots::new` disallowed), SC-007 |
| Settings model: schema, atomic persistence, per-OS defaults, injected, no UI (482-486) | D10, FR-030 to FR-036, SC-009 to SC-011 |
| "Where state lives": one per-user state dir, atomic writes, logs (158-163) | D7 (identifier dir), D22, D23, FR-100, FR-104, FR-105, SC-002 to SC-004, SC-025, SC-029, SC-030 |
| Capability: only the app's commands; CSP: no remote origin (487) | D11 to D13, D12a, FR-041 to FR-045, SC-015, SC-016 |
| `tauri.conf.json`: `signingIdentity "-"`, `minimumSystemVersion "15.0"`, NSIS `currentUser`, no `LSFileQuarantineEnabled` (488-490) | FR-005, FR-045, SC-013, SC-015 |
| Version source, tag form, CHANGELOG; lint in manager-ci, tests in feed-tools (491-494) | D18, FR-060 to FR-063, FR-071, FR-081, SC-012 |
| `manager-ci.yml`: path filter, three legs, the checks, unsigned `tauri build`; macOS plist + codesign; native arm64 (495-501) | D15, D16, FR-070 to FR-077, SC-013, SC-014, SC-020. Departure: the Ubuntu apt line is the current prerequisites page's, not line 497's `libappindicator3-dev … patchelf` set (an `ubuntu-22.04` set, Phase 11's container); the plan edits line 497 (D16, FR-090) |
| All checks green on Windows, zero warnings; macOS / Linux PENDING-CI (504) | SC-001, every PENDING-CI row (closed on the named step's annotation, FR-077) |
| `default_roots()` per OS from injected env (505-506) | SC-002 to SC-004 |
| Deliberate `Roots::from_os()` in a test target fails clippy, fixture excluded from the workspace, same `clippy.toml`, a step on every leg (507-511) | FR-016, FR-072, SC-006 |
| Settings round-trip, future version rejected with defaults kept, crash between temp write and rename (512-513) | SC-009 to SC-011 |
| Tests in parallel (514) | FR-053, FR-076, SC-021 |
| Lint accepts / rejects with named errors; `LSMinimumSystemVersion` 15.0 (515-517) | SC-012, SC-013 |
| Does NOT: fetch a feed, read a plug-in folder, release pipeline (519) | Non-goals, FR-090 |
| Q1 (24): `apps/krate-manager/`, `version.json`, lint, tag, CHANGELOG, 0.1.0 | FR-001, FR-060 to FR-063 |
| Q3 (26): Linux root `~/.vst3`, factory presets in the user folder; no per-user opt-in on Windows / macOS | D8, D10, FR-030 |
| Q5 (28): NSIS `currentUser` | FR-005, SC-015 |
| Q6 (29): macOS 15 and later; Windows 10 22H2 / 11 x64; Linux at the `ubuntu:22.04` glibc | macOS 15: FR-005, SC-013. Windows 10 22H2 / 11 x64: not expressible in `tauri.conf.json` (no minimum-OS key in `WindowsConfig` / `NsisConfig`); declared in Phase 12's first-launch documentation (roadmap 882-886), whose bullet the plan extends (FR-090); x64 is what FR-073 checks. Linux glibc floor: Phase 11 (roadmap 821-822). Both recorded in Non-goals |
| ROSTER (33) | Nothing in this phase names a plugin; FR-021's scan has no plugin name to find |
| Probe Q6 checks reused (208) | FR-074, SC-013 |
| Cross-cutting: never touch the real system; zero warnings; parallel tests; naming (961-1000) | Safety, FR-015, FR-020, FR-083, SC-001, SC-021 |

**Additions and departures.** Additions from the 2026-10-30 clarification session: the `StateLock` and the refused-start
notice (D22, FR-100, FR-101), the `Configured` Linux root (FR-102), `SharedState` / `reassemble` (FR-103), the `save`
refusal over a newer file (FR-036), logging (D23, FR-104, FR-105), the `dev-feed` feature and its CI assertions (D24,
FR-106, FR-107), and the second command `startup_error` (D11). Earlier additions: `Roots::new` and the six `std::env` reads also disallowed outside the seven
D6 sites, and `#[expect]` instead of `#[allow]` with `allow_attributes(_without_reason)` denied (D6); the navigation
guard (D12) and no `tauri`-linked test binary (D12a); the `app_version` command as the wired surface (D11); per-OS
bundle targets limited to NSIS / app / deb (D15); the `package.json` and platform-conf `version` ban in the lint
(D18); the offline shape test of `manager-ci.yml` (D19, FR-080); annotation verdicts and the "required tests ran" step
(D21, FR-077); the static config guard test (FR-045); the frontend markup-sink ban (FR-052); the "build warnings"
count and the Windows `ProductVersion` check (FR-071, FR-073). Departures, each with a roadmap edit the plan makes
(FR-090): (1) roadmap line 473: the HTTP client base and the elevation launcher are deferred to Phases 5 and 10 (D17);
(2) roadmap line 497: the Ubuntu leg's apt line is the current Tauri prerequisites line, and line 497's
`libappindicator3-dev … patchelf` set moves to Phase 11's `ubuntu:22.04` container (D16); (3) Q6's Windows floor,
which no `tauri.conf.json` key can carry, gets a sentence in Phase 12's first-launch documentation bullet (Non-goals).

---

## Open questions

None; the seven clarify-scan questions are decided (see Clarifications). The roadmap defers two decisions to this spec, the UI framework and the linter (line 469), and D1 and D2 take
them. D3-D21 (with D12a) are this spec's own choices and are open to challenge in the clarify scan; the ones most
worth the user's eye are D4 (stable channel instead of a pinned toolchain, at the cost of new-lint reds), D7 (state
dir locations, which Phase 10's helper will treat as the only source of installers), D15 (CI builds only NSIS / app /
deb until Phase 11) and the three roadmap departures above (lines 473, 497, the Q6 Windows floor).

---

## Review notes

Challenge round 1 (2026-10-30): all seventeen issues accepted; none rejected. Where the review offered alternatives,
the spec took:
- FR-021 test-literal exemption: option (a), every unit-test module in a `<stem>_tests.rs` file excluded by name,
  plus a scan failure on any inline `mod tests {`. SC-016 keeps `https://krateaudio.com/` as a foreign host because the
  feed host being non-navigable is a real case, and the test file is not scanned.
- Core's environment ban: clippy `disallowed-methods` (verified on clippy 0.1.98 against imports and aliases this
  session), with the text scan kept for path and URL literals and for filesystem tokens in `platform/` and `roots.rs`.
- `tauri`-linked test binaries on Windows: moved the tested logic and the config guard into `crates/core` and set
  `test = false` on `src-tauri` (D12a) rather than the manual manifest-embedding workaround, which would be a
  workaround for a bug the design can avoid.
- Release-profile warnings: counting `^warning:` in the `tauri build` log rather than a second `cargo clippy
  --release` run; the build already compiles exactly the release + `custom-protocol` configuration, so the count
  covers it at no extra compile. The dev / release branch of D12 is now a runtime `tauri::is_dev()` argument, so no
  app code is compiled only in one profile.
- ESLint root config files: ignored (FR-052) rather than `allowDefaultProject`, which would lint `vite.config.ts`
  without the Node types it needs; `tsc` still checks it.
- The D17 deferrals and the D16 apt line: recorded as departures with roadmap edits (FR-090) rather than adding
  placeholder `http_base` / `launcher` fields whose shapes Phases 5 and 10 own.

---

## Clarifications

### Session 2026-10-30

- Q: Is the manager single-instance, and by what mechanism? → A: Yes, in Phase 4: a core `StateLock` takes an exclusive
  `std::fs::File::try_lock` on `<state>/manager.lock` inside `assemble_context`; a second instance gets
  `E_STARTUP_LOCKED`, shows "Krate Manager is already running" and exits; tested in core with two locks on one temp
  state dir; startup now creates only the state dir and the lock file. [D22, FR-017, FR-100, FR-101, FR-042, FR-046,
  SC-024, SC-025, SC-026, SC-017]
- Q: Where does the state dir live? → A: The Tauri identifier dir on all three OSes, computed in core with no Tauri
  call: `%LOCALAPPDATA%\com.krateaudio.krate-manager`, `~/Library/Application Support/com.krateaudio.krate-manager`,
  `$XDG_DATA_HOME/com.krateaudio.krate-manager` (fallback `~/.local/share`); one per-user dir per OS, cleared by the
  NSIS "Delete app data" box on Windows. [D7, D8, FR-021, FR-045, SC-002, SC-003, SC-004]
- Q: How does a configured `linux_plugin_root` reach the `Roots` that detection and installs read? → A: `assemble_context`
  adds it to `Roots.plugin_roots` on Linux as the new kind `Configured`, first in priority, and keeps `~/.vst3`
  (`Vst3User`); detection (Phase 6) and Phase 9's allowed-root set read the one list. [D8, FR-013, FR-102, SC-027]
- Q: What does `save` do over a `settings.json` that `load` rejected? → A: `save` re-reads first and refuses with
  `E_SETTINGS_FUTURE_VERSION` while the file on disk has `schema_version` > 1; a `Corrupt` or `Invalid` file is
  overwritten; D10 stays strict (unknown fields rejected). [D10, FR-031, FR-033, FR-034, FR-036, SC-010, SC-011]
- Q: Who owns logging? → A: Phase 4: the `log` 0.4.34 facade in core and a file sink writing
  `<state>/logs/krate-manager.log` (size-capped, one rotation) in `src-tauri`; startup error codes are written there;
  FR-007's core dependency set gains `log`. The rotation logic sits in core so it can be tested (D12a). [D23, FR-004,
  FR-007, FR-017, FR-104, FR-105, SC-029, SC-030]
- Q: Is `AppState` immutable or behind a lock? → A: `app.manage(RwLock<AppState>)` from Phase 4 (`SharedState`), with a
  core `reassemble(settings)` that rebuilds `AppContext` including the folded `Roots` of the Linux-root answer; later
  phases add fields without changing the managed type and a changed Linux root applies without a restart. [D17,
  FR-017, FR-103, SC-028]
- Q: May a non-release build point at a non-production feed? → A: Via a cargo feature `dev-feed`, never enabled by
  `tauri build` in CI or by `release-manager.yml`, that reads the feed URL from a file in the state dir; a `manager-ci`
  step asserts the feature is absent from the release build; no environment read joins the guarded set. [D24, FR-004,
  FR-045, FR-071, FR-106, FR-107, SC-001, SC-019, SC-031]

Choices the answers left open and this spec made (flagged for the plan review, not user decisions): the notice is shown
through a second read-only command `startup_error` and the refused instance ends itself after a 5-second
`LOCKED_EXIT_DELAY` (FR-101); the log cap is 1 MiB with `MAX_LINE_BYTES` 4096 (FR-104); the override file is named
`dev-feed-url.txt` (FR-106); `reassemble` returns the previous context with `E_SETTINGS_INVALID` on a relative root
(FR-103); an equal-path `Configured` root is not duplicated (FR-102). [FR-101, FR-104, FR-106, FR-103, FR-102]

### Session 2026-10-30 (plan)

- Q: Accept the plan's spec edits SI-1 to SI-11 and tasks.md TA-1 to TA-8? -> A: Yes, all of them; T001 writes the
  spec-changing ones into the FR/SC rows. [P-1]
- Q: One manual launch of the built app on Windows before close (plan §10.1)? -> A: (a) No. Phase 4 closes on the
  static checks (SC-015, SC-016, SC-017, SC-026); the two-launch check is done when Phase 6's status screen exists. [P-2]
- Q: May the build install the Rust targets x86_64-unknown-linux-gnu and aarch64-apple-darwin on this box? -> A: (a)
  Yes. The main loop ran `rustup target add` once before the build stage; L-XCHECK runs. It is a compile proxy and
  closes no PENDING-CI row. [P-3]
- Q: The phase workflow's generic app lane ran `cargo test --manifest-path src-tauri/Cargo.toml --all-targets`? -> A:
  (a) The main loop changed that line of `.claude/workflows/krate-manager-phase.js` to `cargo test --workspace --locked`
  (run from `apps/krate-manager/`) before the build stage. [P-4]
