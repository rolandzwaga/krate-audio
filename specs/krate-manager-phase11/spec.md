# Feature Specification: Krate Manager Phase 11 — Self-Update and the Manager's Release Pipeline

**Spec slug:** `krate-manager-phase11`
**Roadmap:** `specs/Krate-manager-roadmap.md` → Phase 11 (lines 818-888); Decisions Q1 (line 24: `apps/krate-manager/version.json`
single source, tag `manager/vX.Y.Z`, `CHANGELOG.md`, first public version 0.1.0, "released by manual dispatch of
`release-manager.yml`"), Q5 (line 28: Windows NSIS `currentUser`, macOS unsigned pkg to `/Applications` plus a `curl`
one-liner, Linux AppImage plus `.deb` built in `ubuntu:22.04`), Q6 (line 29: macOS 15+, universal build, Linux at the
`ubuntu:22.04` glibc); Positioning "Self-updating with Tauri's minisign updater, no platform certificate" (line 42);
Architecture Overview "Self-update path" (lines 150-156) and the feed model's `manager/latest.json` (lines 136-140);
Key design decisions 1 and 4 (lines 167-169, 175-176); Repository Layout (lines 232, 239-240); Phase 10's amended Phase
11 text (lines 845-848, 875, Phase 10 FR-123 (d)); Dependency Graph edges 3→11, 4→11, 7→11, 11→12 (line 971-972);
Cross-Cutting Constraints (lines 999-1040, in particular the named hosted-runner exception, lines 1006-1008).
**Depends on:** Phase 2 (`tools/feed-contract/keys/krate-signing.pub`, the `signing` environment and its two secrets,
`tools/minisign.js`, `tools/release-facts.js`), Phase 3 (`feed.yml` `workflow_call`, `tools/feed-publish.js` `collect`
copying `manager/latest.json`), Phase 4 (`version.json`, `lint-manager-version.js`, `tauri.conf.json`, `manager-ci.yml`,
D12a "no `tauri`-linked test binary"), Phase 5 (`HttpConfig`, the proxy resolution, `Feed.manager`), Phase 7
(`verify::RELEASE_KEY_TEXT`, the one release key), Phase 10 (`scripts/bundle.js`, `tauri.helper.conf.json`, the helper
sidecar, the elevation guard).
**Consumed by:** Phase 12 (the landing card links this phase's release assets; the user-performed checks; "release
readiness for manager 0.1.0" closes this phase's PENDING-CI rows).
**Status:** DRAFT, revision 4 (plan §0.2 SI-1 to SI-40 and tasks TA rows encoded, 2026-11-01): revision 3 (review issues resolved, see Review notes; clarifications of 2026-11-01 encoded, see Clarifications) plus the plan's spec issues (`plan.md` §0.2; each edit tagged `(plan SI-n)`; SI-19 is withdrawn by the plan and superseded by SI-20) and the spec-marked task-list amendments (`tasks.md` "Task-list amendments (TA)"; each tagged `(tasks TA-n)`); specification only, no implementation.
**Date:** 2026-11-01 · verified against `feat/krate-manager` at `f6022bd5` ("feat(krate-manager): Phase 10 elevated helper
for Inno and pkg installers (stopped at the M-1 fence)", `git rev-parse --short HEAD`, clean tree);
`specs/krate-manager-phase11/` did not exist before this file.

---

## Overview

The manager can install, update and remove plugins (Phases 9, 10) but cannot update itself, and nothing builds or
publishes it: `apps/krate-manager/src-tauri/Cargo.toml:24-31` has no updater dependency, `tauri.conf.json:1-25` has no
`plugins` key, and `.github/workflows/` holds no `release-manager.yml`. Phase 11 adds both halves.

**Self-update.** The app registers `tauri-plugin-updater` 2.14.1 with `requireSignedVersion: true` and downgrades
refused, checks one compiled-in endpoint (`https://krateaudio.com/manager/latest.json`), and installs an update only
when the user asks: Windows runs the per-user NSIS setup in `passive` mode (no UAC, the app exits and the setup
relaunches it), macOS swaps the bundle and the app relaunches, Linux replaces the AppImage and relaunches; a `.deb`
install never updates itself: its banner offers the new `.deb` on the release page instead (plan SI-21). The webview gets two new commands of the app's own and no `updater:*`
permission, so it can neither pick a URL nor turn a downgrade on (the CVE-2026-95624 lesson, report line 46). Every
security property the roadmap names (a tampered package, another key, a signed-version mismatch, a downgrade) is proven
against a local fixture server by a harness that calls only `check`, `download` and `verify`; `install` is reachable only
through the manager's own `UpdateInstaller` trait, whose harness implementations panic (or, in the two kept-update
scenarios, return an error without touching `Update::install`).

**Release pipeline.** `release-manager.yml` (manual dispatch only) checks the version sources, the CHANGELOG entry and
the tag before any build; builds Windows (NSIS), one universal macOS `.app` (with a universal helper sidecar) wrapped in
an unsigned pkg, and Linux AppImage plus `.deb` inside an `ubuntu:22.04` container, each leg with a throwaway key;
re-signs every updater artifact with the one release key in a separate `sign` job that builds nothing (plan SI-20); proves the download → verify → install → relaunch path end to end on all three OSes and the
first-install artifacts on fresh runners; and only then publishes the release and its `latest.json` and calls
`feed.yml`, which already copies `manager/latest.json` to Pages (`tools/feed-publish.js:376-393`). A `dry_run` input does
everything except publishing, with a throwaway key.

One research premise no longer holds: `tauri-action` 1.0.0 (2026-06-29) writes `latest.json` download URLs as
`https://api.github.com/repos/<owner>/<repo>/releases/assets/<id>` (PR #1315) and no longer lets `tagName` choose them,
so the roadmap's "tagName set so `latest.json` carries per-tag URLs" (line 829) cannot be met with it, and the manager
would call `api.github.com` (forbidden, roadmap lines 141-143). D4 builds with the repo's own `bundle.js` and writes
`latest.json` with a new Node tool instead.

---

## Scope

**In scope (roadmap Phase 11 components, lines 822-862):**

1. `tauri-plugin-updater` with `requireSignedVersion: true`, `allowDowngrades` false, `createUpdaterArtifacts: true`,
   Windows `passive`, relaunch on macOS / Linux (lines 823-824; D1 to D3, D7).
2. The minimal self-update UI: one "update available" line with one action, confirmation, progress, named failures
   (needed for the user to reach item 1; D7).
3. `release-manager.yml`: manual dispatch, the preflight refusals, `create-release`, the per-OS build matrix, the
   signing secrets of the `signing` environment, the updater `pubkey` from `tools/feed-contract/keys/krate-signing.pub`,
   then `feed.yml` (lines 825-832; D4, D14).
4. One universal macOS build with a universal helper sidecar; `latest.json` with `darwin-aarch64`, `darwin-x86_64` and
   `darwin-universal` (lines 833-848; D6, D13).
5. First-install packaging: Windows NSIS `currentUser`; macOS unsigned pkg of the universal `.app` to `/Applications`
   plus the `curl` one-liner; Linux AppImage plus `.deb` in an `ubuntu:22.04` container; the glibc floor (lines
   849-855; D11, D12).
6. The one updater endpoint (lines 856-858; D5, OQ-1 decided in Clarifications).
7. The end-to-end self-replace gate `updater-e2e` and the first-install artifact checks (lines 859-886; D10).
8. The local refusal tests (lines 865-872; D9).

**Non-goals (owned elsewhere):**

| Not in Phase 11 | Owner |
|---|---|
| A public manager release (the user's dispatch after Phase 12) | Phase 12 / the user (line 888) |
| "Update all", the plugin download queue, the settings screen, orphan-manifest Remove | Phase 12 (lines 895-903) |
| First-launch documentation (SmartScreen, Smart App Control, macOS Open Anyway), the landing-page card, the RustSec check before each release | Phase 12 (lines 917-928). Phase 11 puts only the provisional `curl` one-liner into the release body (Q5) |
| The user-performed macOS GUI pkg install and the real UAC / osascript checks | Phase 12 (lines 904-916); FR-093 (c) adds the macOS self-update of a pkg-installed manager to that list (D11) |
| A beta channel, rollback / "install another version" | Not in v1 (lines 986, 993) |
| Plugin installs, plugin assets, `release.yml`, `backfill-sign.yml`, the plugin feed format | Phases 1-10; unchanged here (FR-092) |
| Any certificate, notarization, Developer ID or Authenticode step | Never (line 42) |

---

## Existing repo pieces (verified this session)

| File (lines) | What it is | What Phase 11 does with it |
|---|---|---|
| `specs/Krate-manager-roadmap.md:818-888` | Phase 11 components, outcomes, "Does NOT" | Source of every FR / SC (Traceability) |
| `specs/Krate-manager-roadmap.md:150-156` | Updater ≥ 2.14.1, `allowDowngrades` false, `requireSignedVersion: true`; NSIS `currentUser` + `passive`; macOS `renamex_np(RENAME_SWAP)` with an AppleScript fallback on `PermissionDenied`; Linux AppImage overwrite or `pkexec dpkg -i`; "Phase 11 makes it a CI gate" | ✅ D1 to D3, D7, D10 |
| `specs/Krate-manager-roadmap.md:845-848`, `:874-877` | The universal helper sidecar and its `lipo -archs` row (added by Phase 10 FR-123 (d)) | ✅ D13, FR-034, SC-013 |
| `specs/Krate-manager-roadmap.md:1006-1008` | "Phase 11 `updater-e2e` and the first-install artifact checks" are the named hosted-runner exceptions | ✅ FR-076, FR-090 |
| `apps/krate-manager/version.json` | `{"version": "0.1.0", "name": "Krate Manager"}` | ✅ The release version (FR-050) |
| `apps/krate-manager/CHANGELOG.md:8` | `## [0.1.0] - Unreleased` | ✅ A real run requires a dated heading (D15; Phase 4 spec FR-062: "Phase 11's release refuses a version without a dated entry"); dating it is the user's release step |
| `tools/lint-manager-version.js:1-20` | `version.json` vs `tauri.conf.json` vs every `Cargo.toml`; exit 0 / 1 / 2 | ✅ Re-run by the preflight (FR-050) |
| `apps/krate-manager/src-tauri/tauri.conf.json:1-25` | No `plugins` key; `bundle.macOS { signingIdentity "-", minimumSystemVersion "15.0" }` (`:22`); `bundle.windows.nsis.installMode "currentUser"` (`:23`); CSP with no remote origin (`:16`) | 🔶 Gains `plugins.updater` (FR-002); nothing else |
| `src-tauri/tauri.helper.conf.json:1`; `tauri.windows.conf.json`, `tauri.macos.conf.json`, `tauri.linux.conf.json` (one line each: `nsis`, `app`, `deb`) | Phase 10 D12's sidecar merge; per-OS bundle targets | ✅ Unchanged; a new `tauri.release.conf.json` is merged only by release builds (D3) |
| `src-tauri/Cargo.toml:24-31`, `:44-47` | Normal dependencies (`tauri`, `tauri-plugin-opener`, `serde`, `serde_json`, `log`, core); features exactly `dev-feed`, `manual-elevation` | 🔶 `tauri-plugin-updater` (FR-001); feature `updater-e2e` (FR-070) |
| `apps/krate-manager/Cargo.toml:13-50`, `:56-61` | Workspace dependencies (`tauri` 2.12.5, `tauri-build` 2.7.4, `windows-sys` 0.61.2 …); release profile | 🔶 `tauri-plugin-updater` entry (FR-001) |
| `src-tauri/src/lib.rs:29`, `:87-113` | `FEED_URL`; the builder: `navigation::guard()` plugin, managed state, eight commands in `generate_handler!` (`:103-112`) | 🔶 Registers the updater plugin and two commands (FR-001, FR-020) |
| `src-tauri/build.rs:1-15` | The app manifest lists the eight commands | 🔶 Ten (FR-020) |
| `src-tauri/capabilities/main.json:1-14` | Eight `allow-*` permissions, nothing else | 🔶 Two more `allow-*`; no `updater:*` (FR-021) |
| `src-tauri/src/commands.rs:18-21` | `app_version` returns `CARGO_PKG_VERSION` | ✅ Equal to the updater's current version in a real build by the version lint |
| `crates/core/src/verify.rs:15-16`, `:50-51` | `RELEASE_KEY_TEXT = include_str!("…/tools/feed-contract/keys/krate-signing.pub")`; `SigningKey::release()` | ✅ The one key; the updater `pubkey` is its base64 (D2, FR-003) |
| `tools/feed-contract/keys/krate-signing.pub` | Two-line minisign public key, key id `9FEB36F9114273BC` | ✅ Read, never changed |
| `tools/feed-contract/keys/README.md` §2-§3 | The `signing` environment (main only); secrets `TAURI_SIGNING_PRIVATE_KEY` (the one-line base64 key file) and `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`; "Phase 11 … passes the same password to Tauri … and signs manager updates with the same key" | ✅ FR-055 |
| `crates/core/src/feed.rs:41`, `:131-132`, `:373-389` | `Feed.manager: Option<ManagerInfo { latest_json_url }>` (Phase 5 FR-028: carried, unused) | ✅ Not consulted for the endpoint (D5); SC-004 pins equality of the compiled URL with the feed tool's |
| `crates/core/src/http.rs:33-41`, `:103-107`, `:276-300`; `crates/core/src/pac.rs:141-144` | `HttpConfig { policy, …, proxy, pac, pac_host }`; `ProxyResolution`; `check_url(UrlPolicy, &Url)`; `ProxyChoice::{Direct, Proxy}` | ✅ The updater's URL policy and proxy follow the core's (FR-011, FR-012) |
| `crates/core/src/http.rs:285-300`, `:374-382`; `crates/core/src/fixture_server.rs:17-38`, `:142-147`, `:443-448` | `check_url`'s `LoopbackOnly` arm refuses any non-loopback host ("only 127.0.0.1 and [::1] are allowed"); `next_hop` re-checks every redirect `Location`; the fixture server binds `127.0.0.1:0` only, scripts `Redirect`, `ConnectOk`, `ConnectRefused`, answers a non-loopback `CONNECT` 403 without dialling and records every request | ✅ FR-011's rule order, FR-011a, the harness trap proxy (FR-080) |
| `apps/krate-manager/Cargo.toml:40`; `src-tauri/Cargo.toml:25`; `src-tauri/src/lib.rs:40`, `:45`, `:70` | `tauri = { version = "2.12.5", features = [] }` (default features kept); `tauri = { workspace = true }`; `pub fn run`; `Roots::from_os()`; `assemble_context(roots, …, SigningKey::release())` | ✅ D9 (wry is linked into the harness), FR-081 (a), FR-094 |
| `.gitattributes:24-26`; `git config --get core.autocrlf` → `true`; `git check-attr -a tools/feed-contract/keys/krate-signing.pub` → nothing; `git ls-files --eol` → `i/lf w/lf` | Only `tools/feed-contract/fixtures/**` is `-text` (at `f6022bd5`) | ✅ FR-097, already met by Phase 10's close (`.gitattributes:29`; tasks TA-2) |
| `.github/workflows/manager-ci.yml:300-304` | `ci-temp-roots absent`: `cargo tree -p krate-manager-helper -e features --locked`, the `--ci-roots` marker | ✅ The pattern of FR-056's release-build step |
| `crates/core/src/context.rs:17`, `:53-61`, `:206-209`; `crates/core/src/state_lock.rs:41`, `:53` | `LOCKED_NOTICE`; `AppContext`; a second instance sees the lock and quits; `StateLock::acquire` | 🔶 The lock is released before a relaunch (FR-016) |
| `crates/core/src/install.rs:587-605`, `:3841` | `Elevation::guard() -> &ElevationGuard`; the per-plugin `BUSY` set | ✅ Read by the self-update busy rule (FR-015) |
| `crates/core/src/notes.rs:52` | `blocks(markdown) -> Vec<Block>` (no HTML writer) | ✅ Renders the update's notes (FR-024) |
| `apps/krate-manager/scripts/bundle.js:26-28`, `:36-38`, `:84-103` | `sidecarName(tuple)`, `tauriArgs(platform)`; `main` refuses any argument (exit 2), builds the host-triple helper, runs `tauri build [--config tauri.helper.conf.json]` | 🔶 A release mode (FR-030 to FR-033) |
| `.github/workflows/manager-ci.yml:31-37`, `:49-54`, `:155-170`, `:228-231`, `:344-366`, `:391-429` | `check` on three legs; Ubuntu apt line; `core dependency tree` bans `tauri*`, `tokio`, `reqwest`, `hyper` from **core** only; `tauri build` = `npm run bundle`; `roster absent` scans the release binary for plugin names and `api.github.com` (`:361`); macOS plist / codesign steps | 🔶 One `check` step runs the refusal harness (FR-084); `updater-e2e absent` step (FR-073); nothing else |
| `.github/workflows/feed.yml:8-21`, `:102-125` | `workflow_call` with `dry_run`; `deploy` only on `refs/heads/main`, `keep_files: true`, `cname: krateaudio.com` | ✅ Called by `release-manager.yml` (FR-058) |
| `tools/feed-publish.js:63-64`, `:333-361`, `:363-393` | `MANAGER_TAG_RE`, `MANAGER_LATEST_URL = 'https://krateaudio.com/manager/latest.json'`; `checkLatestJson` (version equals the tag's, non-empty `platforms`, each `signature` a string, each `url` passes `contract.checkUrl`); `managerRelease` takes the highest non-draft, non-prerelease `manager/v*` | 🔶 `checkLatestJson` also requires the per-tag GitHub URL form (FR-043) |
| `tools/feed-contract/contract.js:210-219` | `checkUrl`: https, no whitespace, no `/releases/latest/`; it accepts `api.github.com` | ✅ Reused; FR-043 adds the stricter manager rule |
| `tools/gen-feed.js:311`, `:619-631`, `:694` | `--manager-latest-url` must end `/manager/latest.json`; emits `manager.latest_json_url` | ✅ Unchanged |
| `tools/feed-contract/CONTRACT.md:30`, `:102-103` | `manager.latest_json_url`, read by "manager (Phase 11 self-update)" | ✅ D5 records that the manager does not use it as the endpoint |
| `tools/minisign.js` (Phase 2) | Node minisign verify of the prehashed `ED` form over `node:crypto` | ✅ Verifies every Tauri `.sig` against `krate-signing.pub` before publishing (FR-042) |
| `tools/release-facts.js` exports `machoFacts`, `glibcFloor` | Mach-O arch set and minimum macOS; glibc floor from `objdump -T` text | ✅ Reused for the manager's facts (FR-062, FR-065) |
| `.github/workflows/release.yml:323-341`, `:578-588`, `:685-692` | Plugin pkg: `pkgbuild --root staging … --install-location /`; "Tag must not exist" (git ref and release list); `softprops/action-gh-release@v2` with `make_latest: false` | ✅ The patterns FR-051, FR-057 and FR-061 follow; `release.yml` itself unchanged |
| `.github/workflows/feed-tools.yml:3-40`, `:80-122` | Path filter; `node --test "tools/feed-contract/tests/*.test.js"` with `PLAN = "1..59"` and a fixed `NAMES` list | 🔶 New tool files and tests in the filter, the plan and the names (FR-091) |
| `tools/feed-contract/tests/manager-ci-shape.test.js:25`, `:1279` | Already reads `release-manager.yml` and refuses `dev-feed` in it (Phase 4 FR-107) | 🔶 Gains the `updater-e2e absent` step; a new `release-manager-shape.test.js` (FR-091) |
| `crates/core/tests/config_guard.rs:826-828`, `:885-887`, `:901-902` | `src-tauri/tests/` must not exist (D12a); `test = false` pins; `src-tauri` `[features]` pinned exactly | 🔶 Pins widen exactly so far (FR-094) |
| `specs/krate-manager-phase4/spec.md:275-282` (D12a) | No `tauri`-linked **test** binary: Windows test executables that link `tauri` can fail with `STATUS_ENTRYPOINT_NOT_FOUND` | ✅ D9: the updater refusals run in a fixture **bin** crate, not a test |
| `specs/krate-manager-phase5/plan.md:239-250` | Record: `tauri-plugin-updater` 2.14.1 uses `reqwest` 0.13 `rustls-no-provider`, `rustls-platform-verifier`, installs `ring`; agrees with `ureq`; its default `system-proxy` would add a second `system-configuration`, "unless Phase 11 turns that feature off and passes this phase's resolved proxy" | ✅ D1, D17 |
| `specs/krate-manager-phase10/spec.md:326-342` (D19) | The per-user helper is replaceable before the prompt (accepted); "whether the installed helper is root-owned and not user-writable is Phase 11's to assert" | ✅ SC-016, D11 |
| `specs/krate-manager/probes/macos-gatekeeper-probe.js:148-158` | Launch check: `open -n`, 12 s, `pgrep -f "<app>/Contents/MacOS/<exe>"`, `pkill` | ✅ The first-install and e2e macOS launch method (FR-075, FR-077) |
| `specs/krate-manager/probes/results.md:14`, `:17-19` | `sudo installer -pkg` installs an unquarantined payload; ad-hoc app verifies; a quarantined ad-hoc app is blocked; the self-replace is untested | ✅ The regression gates of FR-077 and FR-074 |

---

## External facts used (verified this session)

| Fact | Source |
|---|---|
| `tauri-plugin-updater` **2.14.1**, released 2026-10-28; depends on `tauri ^2.12`, `reqwest ^0.13`, `minisign-verify ^0.3`, `tar`, `flate2`, `zip ^9` (optional), `osakit ^0.3`, `windows-sys ^0.61.0`, `dirs ^7`; features `default = ["rustls-tls", "system-proxy", "zip"]`, `zip` enables `flate2` + `tar` + `zip`, `rustls-tls` enables `rustls` + `reqwest/rustls-no-provider`, `system-proxy` enables `reqwest/system-proxy` | https://docs.rs/crate/tauri-plugin-updater/2.14.1, https://docs.rs/crate/tauri-plugin-updater/2.14.1/features |
| Config keys (2.14.1 `src/config.rs`): `pubkey` (no default), `endpoints`, `requireSignedVersion` (default `false`; "Releases signed before the Tauri CLI started recording it carry no version, so enabling this rejects them"), `allowDowngrades` (default `false`; "Ignored when the application sets a custom" comparator), `dangerousInsecureTransportProtocol`, `dangerousAcceptInvalidCerts`, `dangerousAcceptInvalidHostnames`, `windows { installMode, installerArgs }`; `installMode` `passive` (default, `/P`), `quiet` (`/S`, "Requires admin privileges if the installer does"), `basicUi` | https://docs.rs/crate/tauri-plugin-updater/2.14.1/source/src/config.rs |
| `Update` (2.14.1): public fields `body`, `current_version`, `version`, `date`, `target`, `download_url`, `signature`, `raw_json`, `timeout`, `proxy`, `no_proxy`, `headers`; `download(on_chunk, on_finish) -> Result<Vec<u8>>` "downloads the updater package, verifies it, and returns the bytes"; `verify(&self, bytes)` "without making a request or installing anything"; `install(bytes)` "runs `verify` first … On Windows, the app exits after the installer launches. On macOS and Linux, the app must be relaunched"; `download_and_install`; `restart_after_install(bool)`, default `true` | https://docs.rs/tauri-plugin-updater/2.14.1/tauri_plugin_updater/struct.Update.html |
| `Updater::check(&self) -> Result<Option<Update>>`: `None` on 204 or "if the release is not newer than the current version by default"; `UpdaterExt::{updater, updater_builder}` on any `Manager<R>`; `UpdaterBuilder::{endpoints, pubkey, proxy, no_proxy, timeout, target, executable_path, on_before_exit, restart_after_install, version_comparator, configure_client, build}`; `endpoints` "returns an error on release builds if an endpoint isn't HTTPS, unless `dangerousInsecureTransportProtocol` is enabled"; plugin `Builder::{new, pubkey, target, installer_args, header, default_version_comparator, build}` | https://docs.rs/tauri-plugin-updater/2.14.1/tauri_plugin_updater/struct.Updater.html, `…/struct.UpdaterBuilder.html`, `…/trait.UpdaterExt.html`, `…/struct.Builder.html` |
| `Error` variants (32), among them `Minisign`, `Base64`, `SignatureUtf8`, `SignedVersionMismatch { signed, announced }`, `MissingSignedVersion`, `Reqwest`, `Network`, `Http`, `Serialization`, `ReleaseNotFound`, `TargetNotFound`, `TargetsNotFound`, `InsecureTransportProtocol`, `Io`, `TempDirNotOnSameMountPoint`, `PreviousAppNotRestored`, `BinaryNotFoundInArchive`, `AuthenticationFailed`, `DebInstallFailed`, `PackageInstallFailed`, `InvalidUpdaterFormat` | https://docs.rs/tauri-plugin-updater/2.14.1/tauri_plugin_updater/enum.Error.html |
| Updater source at tag `updater-v2.14.1`: `verify_signature` decodes the base64 `pubkey` and signature, `public_key.verify(data, &signature, true)`, then `verify_signed_version(trusted_comment, announced, require)` (`src/updater.rs:1893-1917`); target keys tried `{os}-{arch}-{installer}` then `{os}-{arch}` (`:743-761`), installer names `appimage`, `deb`, `rpm`, `app`, `msi`, `nsis` (`:50-75`); the download sets `Accept: application/octet-stream`; macOS: temp dirs on the app's volume (system temp, else the app's parent), `renamex_np(RENAME_SWAP)`, any error but `PermissionDenied` falls back to two renames, `PermissionDenied` runs the swap through `do shell script … with administrator privileges`, the extracted tree is chmod `0755` and owned by the extracting user (`:1486-1600`); Windows: `ShellExecuteW` verb `open`, `SW_SHOW`, NSIS `/UPDATE` (+ `/R` when restarting), then `exit(0)` (`:1009-1103`); `.deb`: `pkexec dpkg -i`, then zenity / kdialog → `sudo -S`, then plain `sudo` (`:1329-1460`) | https://github.com/tauri-apps/plugins-workspace/blob/updater-v2.14.1/plugins/updater/src/updater.rs (read 2026-11-01) |
| Tauri CLI 2.12.0 (2026-09-26): "Record the app version in the trusted comment of updater signatures … Enable `requireSignedVersion`"; `tauri build` fills it in. The trusted comment is `timestamp:<unix>\tfile:<file name>\tversion:<version>` (`crates/tauri-cli/src/helpers/updater_signature.rs:126-170`, tag `tauri-cli-v2.12.4`). The repo pins `@tauri-apps/cli` 2.12.4 (`package.json:17`) | https://github.com/tauri-apps/tauri/releases/tag/tauri-cli-v2.12.0; source read 2026-11-01 |
| `tauri build` signing (`crates/tauri-cli/src/bundle.rs:236-322`, `tauri-cli-v2.12.4`): when the bundler has updater settings it signs NSIS, MSI, AppImage, Deb, Rpm and the macOS updater archive; it fails "A public key has been found, but no private key. Make sure to set `TAURI_SIGNING_PRIVATE_KEY`"; the password is `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`; a secret key that does not match `plugins > updater > pubkey` is only a **warning**; `--no-sign` skips updater signing ("Updater signing is skipped due to --no-sign flag") | Source read 2026-11-01 |
| `tauri build` options: `-t/--target`, `-c/--config` ("JSON strings or paths to JSON, JSON5 or TOML files to merge"), `-b/--bundles`, `--no-sign` ("Skip code signing when bundling the app"), `--ci`; `tauri signer generate -w -p -f --ci`; `tauri signer sign --app-version` | https://v2.tauri.app/reference/cli/ (`@tauri-apps/cli` 2.12.4) |
| The bundler expects a universal sidecar named `<name>-universal-apple-darwin` for a universal build (`crates/tauri-bundler/src/bundle/settings.rs:710-713`); `tauri-build` 2.7.4 copies each sidecar for the cargo `TARGET` of each per-arch build and fails when one is missing (`src/lib.rs:662`, `:735-737`, local registry) | Source read 2026-11-01 |
| `BundleType` (tauri-utils 2.10.2): `Deb, Rpm, AppImage, Msi, Nsis, App, Dmg`; no pkg (`src/config.rs:132-147`); `bundle.createUpdaterArtifacts: Updater` (`:1553-1592`) | Local registry, read 2026-11-01 |
| Static updater JSON: required `version`, `platforms.[target].url`, `platforms.[target].signature` (the `.sig` content, "A path or URL does not work!"); `notes`, `pub_date` (RFC 3339) optional; "Tauri validates the whole file before checking the version"; the plugin is registered with `tauri_plugin_updater::Builder::new().build()`; its JS permission set is `updater:default` | https://v2.tauri.app/plugin/updater/ (last updated 2025-11-28) |
| `tauri-action` releases: `action-v1.0.0` 2026-06-29, `action-v0.6.2` 2026-03-14 (branch `legacy/v0`). 1.0.0: "The download urls in `latest.json` will now use the github url instead of the browser download url" (PR #1315: `url: ${githubBaseUrl}/repos/${owner}/${repo}/releases/assets/${data.id}`, and the `tagName` replacement of untagged URLs removed); "Removed `updaterJsonKeepUniversal`. This is now always enabled"; `releaseAssetNamePattern`, `uploadUpdaterJson` inputs | `gh api repos/tauri-apps/tauri-action/releases`, `…/pulls/1315/files` (read 2026-11-01); https://raw.githubusercontent.com/tauri-apps/tauri-action/dev/action.yml |
| `tauri` 2.12.5: feature `test`; `tauri::test::{mock_builder, mock_context, noop_assets, mock_app}` (`src/test/mod.rs:101-186`); `Context::{config_mut, package_info_mut}` (`src/lib.rs:438`, `:490`); `AppHandle::restart` cleans up, then `process::restart` **spawns the new process before** `exit(0)` (`src/app.rs:606-631`, `src/process.rs:74-89`) | Local registry, read 2026-11-01 |
| NSIS `currentUser`: the installer "does not require Administrator privileges" and installs to `%LOCALAPPDATA%` | https://v2.tauri.app/distribute/windows-installer/ |
| `pkgbuild`: `--ownership` default "the recommended UID and GID … generally root:wheel"; `--root`, `--install-location`, `--identifier`, `--version`, `--scripts`; `BundleIsRelocatable` "determines whether the installer should search for and update the bundle when it has been relocated", default false only for packages built on macOS 27 and later; `--component-plist` | https://keith.github.io/xcode-man-pages/pkgbuild.1.html |
| `rename(2)` on macOS: `EACCES` when "the requested operation requires writing in a directory (e.g., new, new/.., or old/..) whose modes disallow this" | macOS `man 2 rename` (Apple). With the updater source above: swapping a root-owned `/Applications/Krate Manager.app` with a user-owned temp tree yields `PermissionDenied` and Tauri's admin prompt (inference; the refused-prompt half is gated by `updater-e2e` case `pkg-installed`, FR-072, and the accepted-prompt half by the Phase 12 user check, D11) |
| GitHub renames release asset file names "that have special characters, non-alphanumeric characters, and leading or trailing periods"; a duplicate name is an error | https://docs.github.com/en/rest/releases/assets?apiVersion=2022-11-28#upload-a-release-asset |
| `gh release create`: `--draft`, `--latest=false` ("to explicitly NOT set as latest"), `--target`, `--notes-file`, `--title`, `--verify-tag` | https://cli.github.com/manual/gh_release_create |
| GitHub-hosted runners: passwordless `sudo` on Linux and macOS; Windows runs as administrator with UAC disabled | https://docs.github.com/en/actions/reference/runners/github-hosted-runners (as cited by Phase 10, fetched 2026-11-01) |
| The `ubuntu-22.04` image is deprecated from 2026-09-17 and unsupported from 2027-04-17; build in a 22.04 container on a newer runner | report line 65 (actions/runner-images #14254) |
| The CVEs: CVE-2026-95624 (webview could pass `allowDowngrades: true`), CVE-2026-95625 (unsigned manifest allowed a forced downgrade to an older validly signed release), fixed in updater 2.12.0 | report line 46 (OSV links there) |

---

## Decisions taken in this spec

The roadmap defers one decision to this spec by name (a second updater endpoint, lines 856-858: OQ-1, D5). D1 to D3 and
D6 to D17 settle what its Phase 11 text names but does not pin down. D4, D11 (the ownership record and the
`BundleIsRelocatable=false` component plist), D16 (the feed-side per-tag rule) and FR-077's live-feed launches depart
from or go beyond roadmap text and are listed under "Departures and findings for the clarify scan".

- **D1, the plugin and its features.** `tauri-plugin-updater = { version = "=2.14.1", default-features = false, features
  = ["rustls-tls"] }` in the workspace (an exact pin, FR-003 (f)), a normal dependency of `src-tauri` only. Only
  `rustls-tls` is on (plan SI-3): the updater depends on `tar` + `flate2` unconditionally on macOS (its `Cargo.toml`
  `[target."cfg(target_os = \"macos\")".dependencies]`), which is what unpacks the macOS `.app.tar.gz`; the optional
  archive feature adds only the v1 updater formats (a Windows archive bundle and a gzip-compressed AppImage), and
  `createUpdaterArtifacts: true` writes the raw setup `.exe`, the raw `.AppImage` and the `.app.tar.gz` (D6), so it is
  not enabled. `system-proxy` is off (Phase 5
  plan record: it would add a second `system-configuration` and ignore the manager's resolved proxy); D17 passes the
  core's decision instead. `rustls-tls` keeps the `rustls-platform-verifier` / `ring` stack Phase 5 recorded as agreeing
  with `ureq`. The plugin is registered with `tauri_plugin_updater::Builder::new().build()`. No
  `@tauri-apps/plugin-updater` or `@tauri-apps/plugin-process` npm package; no `tauri-plugin-process` crate: the relaunch
  is `AppHandle::restart` (External facts).
- **D2, one config, pinned.** `tauri.conf.json` gains exactly `"plugins": { "updater": { "pubkey": "<P>", "endpoints":
  ["https://krateaudio.com/manager/latest.json"], "requireSignedVersion": true, "allowDowngrades": false, "windows": {
  "installMode": "passive" } } }`, where `<P>` is the standard base64 of the bytes of
  `tools/feed-contract/keys/krate-signing.pub` (the form `verify_signature` base64-decodes, External facts; the keys README
  §1 records that the committed file is the Tauri `.pub` decoded). No `dangerous*` key, no `installerArgs`, no
  `version_comparator` / `default_version_comparator` call anywhere (either would make `allowDowngrades` moot). One
  source: the same file `verify.rs:15-16` compiles in; a guard fails if `<P>` and that file disagree (FR-003).
- **D3, updater artifacts only in release builds.** `bundle.createUpdaterArtifacts: true` lives in a new
  `src-tauri/tauri.release.conf.json` that only `bundle.js --release` merges. With it in the base config, every plain
  `npm run bundle` (manager-ci's `tauri build` step, a developer's build) would fail for want of
  `TAURI_SIGNING_PRIVATE_KEY` (External facts, `bundle.rs:288-290`). Whether the bundler has updater settings with
  `createUpdaterArtifacts` false is not documented; manager-ci's existing unsigned `tauri build` step, which keeps running
  unchanged with the new `plugins.updater` block, is the check that it does not (SC-017).
- **D4, no `tauri-action`; the repo builds, uploads and writes `latest.json` itself (departs from roadmap lines 828-830,
  836-837).** `tauri-action` 1.0.0 points `latest.json` at `api.github.com` asset URLs and dropped the `tagName`
  rewrite (External facts), which breaks two roadmap rules at once: the manager never calls `api.github.com`
  (60 requests/hour per IP, lines 141-143) and URLs are per-tag. The legacy 0.6.x line still writes per-tag URLs but is a
  frozen branch. Neither runs `scripts/bundle.js`, which must stage the helper sidecar first (Phase 10 D12), and neither
  builds the pkg or runs in the `ubuntu:22.04` container. So each build job runs `node scripts/bundle.js --release …`
  (the bundler signs, D14), the publish job uploads with `gh release upload`, and `tools/gen-manager-latest.js` writes
  `latest.json` from the uploaded asset names and the `.sig` files, with per-tag
  `https://github.com/<repo>/releases/download/manager/v<ver>/<asset>` URLs. `updaterJsonKeepUniversal` (line 836) no
  longer exists; the `darwin-universal` entry it produced is written by the tool (D6).
- **D5, one updater endpoint (OQ-1).** `https://krateaudio.com/manager/latest.json`, compiled in through D2. No stable
  second host exists: `rolandzwaga.github.io/krate-audio/manager/latest.json` is the same Pages deploy behind a 301
  (roadmap line 54), a per-tag release asset cannot be compiled in, and `/releases/latest/` is forbidden in a monorepo
  (line 857). The feed's `manager.latest_json_url` (Phase 5 `Feed.manager`) is not used to choose the endpoint (a
  hijacked feed must not redirect self-update), and is unchanged; SC-004 asserts the compiled endpoint equals
  `tools/feed-publish.js:64` `MANAGER_LATEST_URL`, so the URL the feed advertises and the one the app reads cannot drift.
- **D6, asset names and `latest.json` keys.** GitHub rewrites spaces in asset names (External facts), and the bundler's
  names carry the product name `Krate Manager`. So the build jobs rename before upload, to fixed space-free names (`<v>`
  the version):

  | Asset | Built by | Release asset name | `latest.json` keys |
  |---|---|---|---|
  | NSIS setup + `.sig` | `tauri build` (`nsis`) | `Krate-Manager-<v>-Windows-x64-setup.exe` (+ `.sig`) | `windows-x86_64`, `windows-x86_64-nsis` |
  | macOS updater archive + `.sig` | `tauri build --target universal-apple-darwin` (`app`, updater artifact) | `Krate-Manager-<v>-macOS-universal.app.tar.gz` (+ `.sig`) | `darwin-aarch64`, `darwin-x86_64`, `darwin-universal` (all three the same entry) |
  | macOS first-install pkg | `pkgbuild` (FR-061) | `Krate-Manager-<v>-macOS.pkg` | none (not an updater artifact) |
  | AppImage + `.sig` | `tauri build --bundles appimage` (container) | `Krate-Manager-<v>-Linux-x86_64.AppImage` (+ `.sig`) | `linux-x86_64`, `linux-x86_64-appimage` |
  | `.deb` (first-install only, plan SI-21) | `tauri build --bundles deb` (container) | `Krate-Manager-<v>-Linux-amd64.deb` (no `.sig`) | none (a system-package install never self-updates) |
  | updater manifest | `tools/gen-manager-latest.js` | `latest.json` | — |

  So `latest.json` carries seven keys: `windows-x86_64`, `windows-x86_64-nsis`, `darwin-aarch64`, `darwin-x86_64`,
  `darwin-universal`, `linux-x86_64`, `linux-x86_64-appimage` (plan SI-21). The updater tries `{os}-{arch}-{installer}`
  then `{os}-{arch}` (External facts), so an AppImage takes the `-appimage` entry or the plain one; a `.deb` install
  finds the plain `linux-x86_64` entry too, but the manager never installs it (FR-013, FR-014: the banner offers the
  release page instead). Renaming changes no signed byte: the `.sig` covers the file content, and its
  `file:` field is informational (the updater checks only `version:`). The `.sig` uploads are the base64 text the
  bundler wrote; `latest.json` carries their content inline.
- **D7, the self-update flow.** Two commands, no argument from the webview but one progress channel:
  `manager_update_check() -> ManagerUpdateView` and `manager_update_install(on_progress:
  tauri::ipc::Channel<SelfUpdateProgress>) -> Result<(), String>` (plan SI-22). The UI calls the check once per launch after
  its first `plugin_status`, and again with every "check now". The check builds the updater from the plugin config with
  D17's proxy and FR-011's URL policy, calls `check()`, and keeps the returned `Update` in Tauri state (a single slot).
  Install first refuses a system-package install and, on macOS, an executable path it could not relaunch from
  (`E_SELF_UPDATE_UNSUPPORTED`, FR-013; plan SI-21, SI-39), then refuses under FR-015's busy rule before it takes the
  kept update (so that refusal keeps it, FR-023; plan SI-36), takes that kept `Update` (none → `E_SELF_UPDATE_NONE`),
  sets the self-update in-flight flag (FR-017: from then on plugin actions are refused, FR-018), calls `download` (which verifies), re-runs
  FR-015's busy rule (FR-019), then hands the bytes to the manager's `UpdateInstaller`. The check runs once `plugin_status`
  settles, success or rejection, unless the context is `Locked` (FR-026). The production installer calls
  `Update::install(bytes)` (it verifies again, External facts); on Windows the process exits after launching the setup
  (`restart_after_install` left at its default `true`, so NSIS relaunches the app); on macOS and Linux it releases the
  state lock (FR-016) and calls `AppHandle::restart`. No silent or automatic install: the user starts every install.
- **D8, Tauri-free logic lives in `core::self_update`.** The URL policy, the busy rule, the proxy choice, the error-code
  table and the view model are plain functions of `krate-manager-core` (cargo-tested on every leg, no Tauri); the
  `src-tauri` module `self_update.rs` only adapts `tauri_plugin_updater` types to them (as `os_facts.rs` does for the
  registry).
- **D9, the refusal tests run in a fixture bin crate.** D12a forbids a `tauri`-linked **test** binary, and every
  updater call needs an `AppHandle`. So `apps/krate-manager/tests/fixtures/updater-refusals/` is a `[[bin]]` crate
  excluded from the workspace members (like `clippy-guard`, `Cargo.toml:3`), with its own `Cargo.lock`, depending on
  `krate-manager` (the `src-tauri` lib), `krate-manager-core` (feature `test-support`: the fixture server, Phase 5) and
  `tauri` with `features = ["test"]` (the mock runtime). The mock runtime opens no window or webview at run time, but it
  does **not** keep the webview stack out of the binary: `krate-manager` takes `tauri = { workspace = true }`
  (`src-tauri/Cargo.toml:25`) and the workspace entry keeps tauri's default features (`apps/krate-manager/Cargo.toml:40`,
  `features = []` without `default-features = false`), and Cargo unifies features, so wry and the webview libraries are
  linked into the fixture bin. D12a's load-failure class (tauri#13419 / #14580 / #13948) therefore applies to it. So
  the fixture has its own `build.rs` that, on Windows only, embeds the same application manifest `tauri-build` embeds
  for `src-tauri` (the plan reads the cited issues, names the manifest content and the mechanism, `tauri-build` 2.7.4's
  `WindowsAttributes` or another crate, with its docs page and version). It builds a mock app whose context carries a
  test `plugins.updater` config (a throwaway key made in a temp dir with the `minisign` 0.10.0 dev pattern of Phase 7,
  a loopback endpoint), runs each scenario through the manager's own `self_update` functions, prints one `ok <name>` /
  `not ok <name> <reason>` line per scenario and exits 0 only when all pass. Its default `UpdateInstaller` panics if
  called. **Fallback, defined now (plan SI-33, PD-33: ruling P-5 of `spec.md:1138-1145`; tasks TA-22):** if the first local
  run shows the bin fails to load on Windows with the manifest embedded, the build goes on: the measurement (exit code,
  loader message) is recorded in `specs/krate-manager-phase11/evidence/d9-windows.txt` (one line, `D9 WINDOWS LOADS` or
  `D9 WINDOWS LOAD FAILURE exit=<code> <loader message>`); the harness then runs on macOS and Linux only, locally in
  WSL and on `check (macos-latest)` / `check (ubuntu-24.04)`; it is still built and linted on Windows; SC-007's
  Windows row is a recorded gap quoting the load error, never a pass. No other workaround is chosen silently.
- **D10, the e2e gate builds two throwaway versions.** A `src-tauri` feature `updater-e2e` (never default, never in a
  release build, checked absent like Phase 10's `ci-temp-roots`) compiles in, from compile-time `env!` values the job
  sets: a loopback endpoint, a loopback plugin feed URL in place of `FEED_URL` (`lib.rs:29`; the e2e server answers
  it with a fixture `versions.json`), permission for loopback `http` (FR-011), and a marker directory. So an e2e build
  contacts no live host. A build with it writes `<marker>/launched-<package version>` after a successful start (state
  lock held), and when its version is the job's "old" version it runs check → download → the production
  `UpdateInstaller`, unattended; a self-update failure writes `<marker>/error-<code>` (the `SelfUpdateError` code). The job builds "old" and
  "new" from the same commit, the two tauri versions set by `--config` (the real `version.json` untouched), with a
  throwaway key in `$RUNNER_TEMP` and a `--config` JSON naming that key's pubkey, the loopback endpoint and
  `dangerousInsecureTransportProtocol: true`; it serves the new artifacts and a `latest.json` from a Node static server
  on `127.0.0.1`. Nothing of this reaches a release: the feature, the config JSON and the throwaway key exist only in
  that job.
- **D11, macOS ownership and the first self-update (goes beyond the roadmap; for the clarify scan).** The pkg is built
  with `pkgbuild`'s default ownership, so `/Applications/Krate Manager.app` and its helper are `root:wheel` after a first
  install (Phase 10 D19 asks Phase 11 to assert that, SC-016). From the updater source and `rename(2)`, the first
  self-update of that copy then hits `PermissionDenied` and Tauri raises its own admin prompt; the swapped-in tree was
  extracted by the user, so after that update the bundle and the helper are user-owned, the state Phase 10 D19 already
  accepted on Windows. Decided 2026-11-01: Phase 11 changes nothing here (no pkg `postinstall` script, no `chown`, no ownership flag): it records the
  behaviour, asserts the first-install ownership on CI, and adds the macOS self-update of a pkg-installed manager to
  Phase 12's user-performed checks (FR-093 (c)), because a headless runner cannot answer the prompt. The refused-prompt
  half (the app stays as it was) is gated on CI by the `updater-e2e (macos-latest)` case `pkg-installed` (FR-072).
  `BundleIsRelocatable` is set `false` through `--component-plist`, so the Installer never "updates" a copy found
  elsewhere instead of `/Applications` (External facts: the default flips only on macOS 27). This goes beyond the
  roadmap's "made with `pkgbuild` like the plugins' pkgs (`release.yml:331-337`)" (line 850-851), whose pkgbuild call
  takes no component plist (Departures 7).
- **D12, Linux in an `ubuntu:22.04` container.** The Linux build job runs on `ubuntu-24.04` with `container:
  ubuntu:22.04`, installs the prerequisites (the report line 65 set `libwebkit2gtk-4.1-dev libappindicator3-dev
  librsvg2-dev patchelf xdg-utils`, plus `build-essential curl file binutils`), Node 24 and the toolchain from
  `rust-toolchain.toml`, and runs `bundle.js --release --bundles appimage,deb`. The floor is the highest `GLIBC_`
  symbol version in `objdump -T` of the built `krate-manager` binary, read with `release-facts.js`'s `glibcFloor`,
  required ≤ the container's `ldd --version` glibc, and recorded in the job summary. Whether linuxdeploy needs
  `APPIMAGE_EXTRACT_AND_RUN=1` inside the container is the plan's to measure; the job fails loudly either way.
- **D13, the universal macOS build and its helper.** The macOS build job adds both rustup targets, and `bundle.js
  --release --target universal-apple-darwin` builds the helper for `aarch64-apple-darwin` and `x86_64-apple-darwin`,
  stages `binaries/krate-manager-helper-aarch64-apple-darwin`, `…-x86_64-apple-darwin` (each per-arch cargo build's
  `tauri-build` copies its own, External facts) and `…-universal-apple-darwin` (`lipo -create` of the two, what the
  bundler copies into the universal `.app`), then runs `tauri build --target universal-apple-darwin --config
  tauri.helper.conf.json --config tauri.release.conf.json`.
- **D14, the release flow and the key.** Jobs in order: `preflight` (no secret, no environment) → `main-only` →
  `build (windows | macos | linux)` → `sign` / `sign-dry` and `package-macos` (the pkg) → `updater-e2e (…)` and
  `first-install (…)` (no secret) → `publish` / `dry-publish` → `feed` / `feed-dry` (`uses:
  ./.github/workflows/feed.yml`). Every job that touches the real key carries `environment: signing`
  (the README §2 rule: main only), as `release.yml`'s `release` job does (`:479`); the jobs are `main-only`, `sign` and
  `publish` (plan SI-20, PD-23). The real key never meets a build: `build` carries no environment, and every leg, dry
  or real, bundles with a throwaway key made inside its bundler step (no `--config` pubkey: `KRATE_BUNDLE_EXTRA_CONFIG`
  is empty on every build leg, so the built app always carries the committed pubkey; the bundler signs as it bundles,
  D3, and `--no-sign` would also drop the macOS ad-hoc signature, External facts). `main-only` (gate,
  `needs: preflight`, `environment: signing`, one `echo` step, no secret reference) keeps dry runs to `main` (as Phase
  2's are) before any build; `sign` (`if: ${{ !inputs.dry_run }}`, `environment: signing`) re-signs the three legs'
  updater artifacts with the real key in the plugin pipeline's way (`node tools/minisign.js decrypt-key` into
  `$RUNNER_TEMP`, `rsign sign`, the key deleted in the next step); its twin `sign-dry` (`if: ${{ inputs.dry_run }}`, no
  environment, no secret) runs the same steps with an `rsign generate` throwaway key and never publishes.
  `preflight` does **no ref check**: it runs outside the environment on any ref, so a dispatch from a
  branch reports preflight's own refusals with their own codes (`E_PREFLIGHT_VERSION` for a drifted `version.json`,
  SC-019), and a branch dispatch that passes preflight is stopped at `main-only`, its first `environment: signing` job,
  by the environment's main-only rule (README §2).
- **D15, the CHANGELOG entry.** A real run requires exactly one heading `## [<v>] - YYYY-MM-DD` in
  `apps/krate-manager/CHANGELOG.md` for `version.json`'s version; `## [<v>] - Unreleased` is refused
  (`E_PREFLIGHT_CHANGELOG_UNDATED`); a dry run accepts it with a `::warning`. The section's body is the release body, the
  `latest.json` `notes`, and what the update line shows (FR-024); the release body appends the macOS `curl` one-liner
  (FR-063).
- **D16, the feed side refuses non-per-tag manager URLs.** `checkLatestJson` already refuses a non-https or
  `/releases/latest/` URL (`contract.checkUrl`); it gains: every `platforms.*.url` equals
  `https://github.com/<--repo>/releases/download/<tag>/<name>` with `<name>` one path segment, so an `api.github.com` or
  foreign-host `latest.json` is degraded (not copied), with the existing `E_MANAGER_LATEST` code and the Phase 3
  degrade path (`feed-publish.js:385-391`).
- **D17, the updater's proxy follows the core's decision.** For the endpoint, and again for the download URL before
  `download` (the `Update.proxy` / `no_proxy` fields are public), `core::self_update::proxy_choice(&HttpConfig, &Url)
  -> Result<ProxyChoice, SelfUpdateError>` returns the decision the core's own client makes for that URL (the static
  proxy with its `no_proxy` bypass, else the PAC answer's first choice, else direct), through `http::route_choice`
  (plan PD-6); where the core's client would refuse to connect (a proxy `check_proxy` refuses) it is
  `E_SELF_UPDATE_NETWORK` and no request is made (plan SI-26); `Proxy(url)` → `UpdaterBuilder::proxy`, `Direct` →
  `no_proxy()`. The updater's client takes every choice `route_choice` returns: direct, an `http` / `https` proxy, or a
  `socks4` / `socks5` proxy (reqwest's `socks` feature, FR-001; plan SI-30: ruling P-1 of `spec.md:1138-1145`).

### Departures and findings for the clarify scan

These go beyond or against roadmap text; the spec takes a position on each so the grill can confirm or overturn it.
They are not Open questions (the roadmap does not defer them):

1. D4 (decided 2026-11-01, option A): no `tauri-action` at all (lines 828-830, 836-837 name it and
   `updaterJsonKeepUniversal`). `bundle.js --release` builds, `gh release upload` uploads, `tools/gen-manager-latest.js`
   writes `latest.json` with per-tag github.com URLs. FR-093 (a) amends the roadmap text; the shape test pins the absence
   (FR-091).
2. D11 (decided 2026-11-01, option A): after a first self-update the macOS bundle is user-owned (source inference); the
   first self-update of a pkg-installed copy raises Tauri's admin prompt. No ownership change and no pkg `postinstall`
   script; Phase 12 records it (FR-093 (c)); the `pkg-installed` e2e case gates the refused-prompt half (FR-072).
3. D15 (decided 2026-11-01): "has no entry for that version" (line 827) read as "has no dated entry"; `Unreleased` passes a dry run only.
4. D9: the local refusal tests are a fixture bin crate, not `cargo test`, because of Phase 4 D12a. It is not a serial
   process: FR-080 runs its scenarios concurrently (roadmap lines 1029-1031); it does link wry (D9), and its Windows
   fallback is defined in D9.
5. D12 / FR-072: a `.deb` install never self-updates (plan SI-21: the updater would hand root, through `pkexec dpkg
   -i`, a user-writable file nothing re-verifies); the e2e Linux leg's `.deb` case proves the refusal
   (`E_SELF_UPDATE_UNSUPPORTED`, zero package requests), and the user installs the new `.deb` from the release page.
6. D16 / FR-043: a feed-side change roadmap Phase 11 (lines 818-888) does not ask for: Phase 3's `tools/feed-publish.js`
   `checkLatestJson` also requires per-tag `https://github.com/<repo>/releases/download/<tag>/<name>` URLs. Reason: a
   `tauri-action`-style `latest.json` with `api.github.com` asset URLs (External facts) passes `contract.checkUrl`
   today (`contract.js:210-219`) and must never deploy, whatever produced it.
7. D11 / FR-061 (decided 2026-11-01, option A): the pkg is built with a `--component-plist` that sets `BundleIsRelocatable=false`, a departure from
   "made with `pkgbuild` like the plugins' pkgs (`release.yml:331-337`)" (line 850-851), whose pkgbuild call takes no
   component plist. Reason: without it a pre-macOS-27 Installer may "update" a copy the user moved instead of placing
   the app in `/Applications` (External facts, `pkgbuild` man page).
8. FR-077 / FR-078 (DECIDED 2026-11-01, option B; the hosted-runner exception is NOT extended): the `first-install`
   launches run the release builds as shipped, which would fetch the live `https://krateaudio.com/versions.json` and
   `https://krateaudio.com/manager/latest.json`. Before each launch the leg points `krateaudio.com` at loopback in the
   runner's hosts file (FR-078), so the shipped artifact reaches nothing live and the fetch-failure path is
   deterministic. No never-touch exception is needed and none is added (FR-093 (f) is dropped). A separate first-install
   build with a loopback feed was rejected: it would no longer be the artifact a user downloads. The `updater-e2e` builds
   contact no live host (D10).
9. (Found at compliance 2026-11-02; RULED by the user 2026-11-02: accepted, signature only, no hash or size field.) The manager's own update package gets no size and
   no SHA-256 check, only the minisign signature: a departure from the Cross-Cutting "size, SHA-256, minisign signature"
   wording (roadmap lines 1013-1014). `latest.json` keeps Tauri's exact schema (no hash or size field;
   `self_update.md` KQ5 inference, line 180; roadmap Phase 11's stock `tauri-plugin-updater`, lines 826-827), and
   tauri-plugin-updater 2.14.1 verifies the downloaded bytes before returning them and again in `install`
   (`updater.rs:891`, `:923`): `PublicKey::verify(data, &signature, true)` over every byte, then, with
   `requireSignedVersion: true`, the signed trusted-comment version against the announced one (`updater.rs:1893-1917`).
   The spec's position: a SHA-256 read from the same unsigned `latest.json` adds no integrity the signature lacks, so
   none is added; the alternative is a non-Tauri `sha256` / `size` field per platform entry, written by
   `gen-manager-latest.js` and checked in `self_update.rs` on `Update::raw_json` before `UpdateInstaller::install`.

---

## Functional Requirements

### A. The updater plugin and its configuration (roadmap lines 823-824; D1 to D3)

- **FR-001** Workspace `Cargo.toml` `[workspace.dependencies]` gains `tauri-plugin-updater = { version = "=2.14.1",
  default-features = false, features = ["rustls-tls"] }` with a one-line comment naming this FR (D1; plan SI-1, SI-3);
  `src-tauri` `[dependencies]` gains `tauri-plugin-updater = { workspace = true }` and the direct line
  `reqwest = { version = "=0.13.5", default-features = false, features = ["socks"] }` (the updater does not re-export reqwest, and
  `redirect::Policy` and `ClientBuilder::read_timeout` must be named, plan SI-28; `features = ["socks"]` is unified into
  the updater's client; it adds no crate (reqwest 0.13.5 `socks = []`), plan SI-30: ruling P-1 of `spec.md:1138-1145`);
  no other member depends on either. `lib.rs` registers `tauri_plugin_updater::Builder::new().build()` beside
  `navigation::guard()`. `Cargo.lock` resolves exactly 2.14.1.
- **FR-002** `src-tauri/tauri.conf.json` gains exactly D2's `plugins.updater` object and nothing else.
- **FR-003** `config_guard.rs` pins: (a) `plugins` has exactly the key `updater`, whose keys are exactly `pubkey`,
  `endpoints`, `requireSignedVersion`, `allowDowngrades`, `windows`; (b) `requireSignedVersion === true`,
  `allowDowngrades === false`, `windows` equals `{"installMode": "passive"}`, `endpoints` equals
  `["https://krateaudio.com/manager/latest.json"]`; (c) `pubkey` equals standard base64 of the bytes of
  `tools/feed-contract/keys/krate-signing.pub` (read from disk by the guard, the same path `verify.rs:15-16` includes),
  and that file contains no `\r` byte (a planted CRLF copy is reported), so the comparison and `include_str!` cannot
  depend on how the tree was checked out (FR-097);
  (d) no `createUpdaterArtifacts` in `tauri.conf.json` or in any `tauri.<os>.conf.json`, and
  `src-tauri/tauri.release.conf.json` is exactly `{"bundle":{"createUpdaterArtifacts":true}}`; (e) no file of the
  workspace names `version_comparator`, `default_version_comparator`, `dangerousInsecureTransportProtocol`,
  `dangerous_insecure_transport_protocol`, `dangerousAccept` or `allowDowngrades": true` outside
  `src-tauri/src/self_update.rs`'s `updater-e2e` block (FR-070), `tests/fixtures/updater-refusals/` and the e2e job;
  (f) the workspace `tauri-plugin-updater` entry is exactly `version = "=2.14.1"` (the drift protection of FR-013's
  error map, plan SI-1). Each rule with a planted mutation reported.
- **FR-004** The macOS `Info.plist` of every build still has no `LSFileQuarantineEnabled` (the existing manager-ci step,
  `manager-ci.yml:398-408`, and the same check in `release-manager.yml`'s macOS build, FR-054).

### B. The core: `core::self_update` (D8)

- **FR-010** New module `crates/core/src/self_update.rs` (tests in `self_update_tests.rs`), Tauri-free.
- **FR-011** `check_update_url(policy: UrlPolicy, url: &Url) -> Result<(), SelfUpdateError>`: first refuse a host
  `api.github.com` (ASCII case-insensitive; reason `UrlRefusal::ApiHost`) and any path containing `/releases/latest/`
  (reason `UrlRefusal::LatestPath`), then `http::check_url(policy, url)` (https only in production, `HttpConfig.policy`;
  reason `UrlRefusal::Policy`). All are `E_SELF_UPDATE_URL { reason }`. The manager's own rules run before the policy
  check so that they are observable under `LoopbackOnly` too (`http.rs:293-298` would otherwise refuse every
  non-loopback host first, and the harness scenarios of FR-080 would pass with the rule deleted). Applied to the
  endpoint before `check`. The package URL has a fixed origin, this project's own release of the announced version
  (plan SI-27, SI-40, PD-27): `check_update_package_url(policy, url, announced)` runs `check_update_url` first, then,
  under both policies, requires the path `.../<MANAGER_REPO>/releases/download/manager/v<announced>/<name>`: exactly
  seven non-empty segments, `owner/repo` equal to the `core::self_update` constant `MANAGER_REPO`
  (`"rolandzwaga/krate-audio"`, case-sensitive), the tag segment equal to `"v" + Update.version`, no query and no
  fragment, and under `HttpsOnly` the host `github.com` (under `LoopbackOnly` any loopback host); anything else is
  `E_SELF_UPDATE_URL { reason: UrlRefusal::Host }`. It is applied to `Update.download_url` before `download` (in place
  of the plain `check_update_url`) and inside `update_action` (FR-014), so a `latest.json` can name no foreign
  repository, no other tag (no downgrade by URL) and no GitHub asset outside that release. The `updater-e2e` build and
  the harness pass `UrlPolicy::LoopbackOnly` (D10, FR-080); production passes `HttpsOnly`. No host is spelled as a
  literal in `crates/core/src/self_update.rs`: the `ApiHost` rule matches the host's DNS labels (`api`, `github`,
  `com`, ASCII case-insensitive, one trailing root dot ignored) and the endpoint host is a parameter (plan SI-4, PD-4;
  FR-094).
- **FR-011a** Redirects. The core client checks every hop (`http.rs:375-382` `next_hop`); the updater's reqwest client
  would follow its own default policy. So `core::self_update::check_update_hop(policy, kind, from: &Url, to: &Url) ->
  Result<(), SelfUpdateError>` (pure) runs `check_update_url` on `to` and then a host rule per fetch `kind`: the
  endpoint fetch may be redirected only within the endpoint's host (`HopRules.endpoint_host`, read at run time from
  the compiled `plugins.updater.endpoints[0]`: `krateaudio.com` in production) or loopback (`LoopbackOnly`); the
  package fetch only between `github.com` and the GitHub release-asset host `release-assets.githubusercontent.com`
  (GitHub Docs, https://docs.github.com/en/actions/reference/runners/self-hosted-runners: "Needed for downloading
  release assets"); any other host is `E_SELF_UPDATE_URL { reason: UrlRefusal::Host }`. The package URL itself is
  first held to its origin, `.../<MANAGER_REPO>/releases/download/manager/v<announced>/<name>` (FR-011,
  `check_update_package_url`; plan SI-40). The updater is built with `UpdaterBuilder::configure_client` (External
  facts) setting one redirect policy for both fetches, whose decision is the pure `redirect_decision(rules, endpoint,
  previous, to)`: the sixth hop (`previous.len() > http::MAX_REDIRECTS`, 5) is `Policy`; the fetch kind is
  `hop_kind(previous[0], endpoint)` (`Endpoint` iff the chain starts at the endpoint, else `Package`); then
  `check_update_hop` (plan SI-27, PD-5). It stops at the first refusal; the refusal is recorded so the error mapping
  (FR-013) reports `E_SELF_UPDATE_URL { reason }` rather than the generic network code. The plan cites
  `configure_client`'s 2.14.1 signature and the reqwest 0.13 redirect-policy API (docs page and version) before
  relying on them.
- **FR-012** `proxy_choice(http: &HttpConfig, url: &Url) -> Result<ProxyChoice, SelfUpdateError>` (D17; plan SI-26,
  PD-6): it calls `http::route_choice` and returns its `Result`; an `Err` (a proxy the core refuses) is
  `E_SELF_UPDATE_NETWORK { detail }`, and the caller makes no request (never `Direct`, which would bypass the
  configured proxy).
- **FR-013** `SelfUpdateError` codes, each with a fixed user sentence: `E_SELF_UPDATE_NETWORK` (no connection, timeout, a stalled transfer (FR-027),
  HTTP error), `E_SELF_UPDATE_MANIFEST` (unparsable or incomplete `latest.json`, no entry for this platform),
  `E_SELF_UPDATE_SIGNATURE` (bad signature, another key, a signed version that is missing or differs from the announced
  one), `E_SELF_UPDATE_URL { reason }` (FR-011, FR-011a), `E_SELF_UPDATE_BUSY` (FR-015, FR-019), `E_SELF_UPDATE_NONE` (install with no kept
  update), `E_SELF_UPDATE_INSTALL { detail }` (the install step failed: I/O, temp dir, restore, package manager,
  refused authorization), `E_SELF_UPDATE_UNSUPPORTED` (the running OS / arch has no updater support; the manager was
  installed by the system package manager, plan SI-21; on macOS, the running executable's path cannot be resolved
  without a symlink, so the manager could not relaunch itself, plan SI-39; each refused by `manager_update_install`
  before any download, and its sentence is "Krate Manager cannot update itself on this computer. Install the new
  version from its release page."; for a system-package install it stays the install backstop behind FR-014's
  release-page action, plan SI-31). The mapping from each of the 32 `tauri_plugin_updater::Error` variants (2.14.1
  without the archive feature) lives in `src-tauri/src/self_update.rs` (FR-022) and is one `match` naming every
  variant plus one `_` arm (→ `E_SELF_UPDATE_INSTALL { detail: "unmapped updater error: {e}" }`): `Error` is
  `#[non_exhaustive]` (`src/error.rs:9-11` at `updater-v2.14.1`), so outside the crate a `match` without `_` does not
  compile (E0004). The drift protection is FR-001's exact pin `=2.14.1`, pinned by `config_guard` (FR-003 (f)), and
  the table is pinned row by row by the harness line `map-errors` (FR-080) (plan SI-1).
- **FR-014** `ManagerUpdateView { os: "windows" | "macos" | "linux", current: String, available:
  Option<AvailableView { version, date: Option<String>, notes: Vec<notes::Block>, action: UpdateAction }>, error:
  Option<SelfUpdateError> }` (`Serialize`), built by a pure function of the kept update's fields; `notes` from
  `notes::blocks` over `Update.body` (no HTML, no remote content). `os` lets FR-024's macOS-only sentence render when
  `plugin_status` was rejected (plan SI-6). `action` (plan SI-31: ruling P-3 of `spec.md:1138-1145`; PD-31) is
  `UpdateAction` = `InstallAndRestart` | `OpenReleasePage { url }` (serialized `{"kind": "install_and_restart"}` /
  `{"kind": "open_release_page", "url": …}`), chosen by the core function `update_action(packaging, policy,
  download_url, announced)`: it first holds the download URL to FR-011's origin rule (a URL that fails it is a check
  error, no banner); a self-contained install → `InstallAndRestart`; a system-package install → `OpenReleasePage` with
  `https://github.com/<MANAGER_REPO>/releases/tag/manager/v<announced>` (plan SI-40), built from the compiled repo and
  the announced version, never from the URL's own segments, and kept only if `status::release_page_url` admits it
  unchanged (the check `open_release_page` applies).
- **FR-015** Busy rule: `self_update_busy(elevation: &dyn Elevation, …) -> bool` is true while the elevation guard is
  held (including the stale hold after a timeout, Phase 10 FR-078a), while any `install::BUSY` entry exists, or while a
  plugin download runs (the plan names the existing in-flight record, or adds one counter in `download`). Install is
  refused `E_SELF_UPDATE_BUSY` before `download` starts. Reason: the Windows path exits the process, and the NSIS setup
  replaces `krate-manager-helper.exe`, which may be running elevated. The rule is evaluated twice: before `download`
  (this FR) and again after `download`, just before the bytes go to `UpdateInstaller` (FR-019). With no `AppContext` (FR-026) there
  is no elevation guard, no `BUSY` entry and no plugin download, so the rule reads "not busy".
- **FR-017** Mutual exclusion flag (decided 2026-11-01). `core::self_update` owns a `SelfUpdateInFlight` flag (an atomic held
  in the core's shared state, set by RAII guard). `manager_update_install` sets it after FR-015's first busy check passes and
  clears it when the install returns an error, when the kept update is cleared, and never otherwise (a successful install
  ends in process exit or restart). The flag is false at startup. It is a plain function of the core, cargo-tested (SC-023).
- **FR-018** While `SelfUpdateInFlight` is set, `download_plugin`, `install_plugin`, `remove_plugin` and the start of an
  elevated batch refuse with the new code `E_INSTALL_SELF_UPDATING` (fixed user sentence: "Krate Manager is updating itself.
  Try again after it restarts.") before any network, filesystem or elevation work, and leave no `BUSY` entry. The code joins
  two tables, `InstallError` and `DownloadError` (`download_plugin` returns `DownloadError`): both gain a `SelfUpdating`
  variant converted from the one `core::self_update::SelfUpdating`, which only `action_gate` constructs (plan SI-15,
  PD-7). This is the single widening of FR-092's fence into Phase 9/10 core behaviour (the action
  gate), in addition to FR-015 / FR-016.
- **FR-019** After `download` returns (and so after the signature check) and just before the bytes go to `UpdateInstaller`,
  `manager_update_install` re-runs FR-015's busy rule. Busy gives `E_SELF_UPDATE_BUSY`, clears the kept update (FR-023) and
  clears `SelfUpdateInFlight`; the installer is not called. (With FR-018 in place this guards the actions already running
  when the flag was set, e.g. an elevated batch that began just before.)
- **FR-016** Relaunch without the lock race: `context::release_for_restart(state: &SharedState)` takes the
  `Arc<StateLock>` out of the shared state and drops the last reference (the plan proves no other clone is held), so the
  process that `AppHandle::restart` spawns before its own `exit(0)` (External facts) acquires the lock instead of
  showing `LOCKED_NOTICE` and quitting. Windows needs none: the NSIS setup relaunches after the old process exited. With no `AppContext` (FR-026) there is no state lock and `release_for_restart` is a no-op.

### C. `src-tauri`: commands, state and UI (roadmap lines 104-110; D7)

- **FR-020** `commands.rs` gains `manager_update_check` (no parameters but managed state and the `AppHandle`) and
  `manager_update_install` (managed state, the `AppHandle` and one `on_progress: tauri::ipc::Channel<SelfUpdateProgress>`,
  as `download_plugin` takes one; core `SelfUpdateProgress { Downloading, Installing }`, plan SI-22, PD-25); `build.rs`
  and `generate_handler!` list ten commands.
- **FR-021** `capabilities/main.json` gains exactly `allow-manager-update-check` and `allow-manager-update-install`; it
  holds no `updater:*`, `process:*`, `shell:*`, `fs:*` or `http:*` permission (pinned by `config_guard.rs`). The existing
  `allow-download-plugin` stays although today's UI downloads only through `install_plugin`: the user ruled
  `download_plugin` a permanent webview command for the later two-step queue (Krate Manager Phase 7 Q6, FR-066,
  `specs/krate-manager-phase7/spec.md:722`, `:1449`; Phase 9 `spec.md:236`; roadmap Phase 12 "Update all and the
  download queue"), and FR-092 fences Phase 7-10 contracts; FR-025's "download" action is the download inside
  `install_plugin`.
- **FR-022** `src-tauri/src/self_update.rs`: the error mapping (FR-013), the builder (`app.updater_builder()` with
  FR-011's endpoint check, FR-011a's redirect policy, FR-012's proxy, FR-027's timeouts and stall bound), the kept-update slot, `trait UpdateInstaller { fn
  install(&self, app: &AppHandle<R>, update: &Update, bytes: Vec<u8>) -> Result<(), SelfUpdateError> }` (generic over
  `R: Runtime`, so D9's mock app drives the same functions) and its production `NativeUpdateInstaller` (D7). Every
  call to `Update::install` or `download_and_install` in the workspace and the harness is inside
  `NativeUpdateInstaller` (guard, FR-094; plan SI-28). The updater's client takes every choice `route_choice` returns:
  direct, an `http` / `https` proxy, or a `socks4` / `socks5` proxy (D17; plan SI-30: ruling P-1 of
  `spec.md:1138-1145`).
- **FR-023** The kept update is replaced by every check, and cleared after every install failure that took the update
  (FR-019's busy refusal and every later error); an FR-015 refusal, made before the update is taken, keeps it (PD-28;
  edge case "retry when it finishes"); an install never uses an update older than the last check (plan SI-36).
- **FR-024** UI (`src/`): a line in the header, shown only when `available` is set: "Krate Manager {version} is
  available." with the notes below it and one button, chosen by `available.action` (FR-014; plan SI-31: ruling P-3 of
  `spec.md:1138-1145`). "Update and restart" is shown only for `install_and_restart`. When `available.action` is
  `open_release_page` the line shows the version and notes and one button "Download the new .deb", which calls the
  existing `open_release_page` command with the view's `url` (no confirmation, no progress, plugin actions stay
  enabled; an open failure shows the existing "Could not open the release page: {text}" line). The "Update and
  restart" button opens a confirmation: "Download
  Krate Manager {version}, verify its signature, then close and restart. Plugin installs must be finished first." plus on
  macOS, and only on macOS (the view's `os`, plan SI-6), "macOS may ask for your administrator password." Progress text
  while it runs, driven by the `manager_update_install` progress channel (`tauri::ipc::Channel<SelfUpdateProgress>`,
  FR-020; plan SI-22): "Downloading and verifying…" on `Downloading`, then
  "Installing… Krate Manager will restart." on `Installing`. Every failure line starts with its code (Phase 9 FR-098 style). A check
  error shows no banner and one muted line "Could not check for a Krate Manager update ({code})." The existing calls
  (`startup_error`, `app_version`, `plugin_status`, `check_now`) keep their order; `manager_update_check` follows the
  first `plugin_status` once it settles (FR-026) and each `check_now`.
- **FR-025** While a self-update runs (from the click on "Update and restart" until it errors), the UI disables every plugin
  action (download, install, remove, and "check now") and shows the progress text; on a failure it re-enables them, and the
  failure line starts with its code. A plugin action refused with `E_INSTALL_SELF_UPDATING` (FR-018) shows its code and
  sentence like any install failure.
- **FR-026** When the check runs (decided 2026-11-01). `manager_update_check` runs once per launch as soon as the first
  `plugin_status` call settles, whether it succeeded or was rejected, unless the startup context is `Locked` (another
  instance holds the state lock: no check, no banner). With no `AppContext` (`StartupError::Roots` or `LockIo`) it still
  runs and installs: it uses `HttpConfig::production()` plus Phase 5's proxy resolution, the busy rule reads "not busy"
  (FR-015), and `release_for_restart` is a no-op (FR-016). It never reads or writes a plugin root, the state dir or the
  feed cache in that mode.
- **FR-027** Timeouts (decided 2026-11-01), named constants of `core::self_update`: the **check** is bounded to 30 s total
  (`UpdaterBuilder::timeout`); the **download** is bounded by `Update.timeout` = 3600 s total (set on the kept `Update`
  before `download`, because the builder `timeout` reaches only the check request: updater 2.14.1 `updater.rs:591-592`,
  `:735`, `:849-851`) plus a 30 s per-read stall bound through `UpdaterBuilder::configure_client` →
  reqwest `ClientBuilder::read_timeout`. There is **no automatic retry** of either. A timeout, a stall or a connection error is
  `E_SELF_UPDATE_NETWORK`, the kept update is cleared (FR-023) and `SelfUpdateInFlight` is cleared. The stall bound is a
  parameter of the same builder function, production passes 30 s (pinned by SC-024) and the harness scenario `stall`
  passes a shorter one; the plan cites `read_timeout`'s reqwest 0.13.5 docs page. The stalling route already exists:
  the core fixture server's `Reply::HeadersThenStall { declared_len, sent, stall }` (`fixture_server.rs:39-44`, Phase 7
  SI-22) sends headers and part of the body and then stalls; the fixture server is not changed (plan SI-5).

### D. Bundling for release (D3, D6, D13; roadmap lines 845-848)

- **FR-030** `scripts/bundle.js` keeps its no-argument behaviour byte for byte (manager-ci, Phase 10 FR-092) and gains
  `--release` with optional `--target universal-apple-darwin` (macOS only), `--bundles <list>` (Linux only) and
  `--features updater-e2e` (that exact value, appended to the `tauri build` argv; for the e2e job, which builds through
  `bundle.js` so the helper sidecar is staged as in a release build; any other `--features` value exits 2; plan SI-17,
  PD-20), any other argument a usage error (exit 2). `--release` adds `--config src-tauri/tauri.release.conf.json` to the `tauri build`
  argv after the helper config (Windows, macOS) or alone (Linux), and passes `--config <file>` values from
  `KRATE_BUNDLE_EXTRA_CONFIG` (a path list the dry run and the e2e job set; empty in a real run, asserted by FR-056).
- **FR-031** With `--target universal-apple-darwin`, the helper is built for both Apple targets and staged under the
  three names of D13; `lipo -create` makes the universal one. The pure functions `releaseTauriArgs(platform, opts)` and
  `universalSidecars()` are exported and covered by `bundle.test.js`.
- **FR-032** After the build, `bundle.js --release` copies the bundler outputs to `<app>/target/release-assets/` under
  D6's names (sources found by fixed bundler paths; zero or more than one match is an error naming the pattern).
- **FR-033** The macOS release bundle carries `Contents/MacOS/krate-manager-helper` with `lipo -archs` `x86_64 arm64`
  and `Contents/MacOS/krate-manager` with the same (FR-054).

### E. `latest.json` and the feed check (D4, D6, D16)

- **FR-040** New `tools/gen-manager-latest.js` (`#!/usr/bin/env node`, usage line, `node --test`):
  `node tools/gen-manager-latest.js --version <v> --tag manager/v<v> --repo <owner/repo> --notes <file> --pub-date
  <RFC 3339> --assets <dir> --pubkey tools/feed-contract/keys/krate-signing.pub --out <file>`. Pure over its inputs; no
  network, no child process. Test-only for the e2e job: `--repo-url-base http://127.0.0.1:<port>` and `--only <os>`
  (`windows` | `macos` | `linux`, allowed only together with `--repo-url-base`, else exit 2), which restricts D6's
  asset table to that OS, because each `updater-e2e` leg has only its own OS's assets; FR-091's shape test confines
  both to `updater-e2e` (plan SI-18, PD-21). For the build legs' `--check-only` run: `--platform-subset <os>` (allowed
  only with `--check-only`), so a leg checks only its own assets against its own throwaway key (the part of the
  withdrawn plan SI-19 that SI-20 keeps).
- **FR-041** Output: `{ "version": "<v>", "notes": <section text>, "pub_date": <date>, "platforms": { <D6's seven keys> } }`
  (no `.deb` key: a system-package install never self-updates, plan SI-21),
  each entry `{ "signature": <.sig file text, trimmed of a trailing LF>, "url":
  "https://github.com/<repo>/releases/download/manager/v<v>/<asset>" }`, keys in bytewise order, output
  deterministic (same inputs → same bytes).
- **FR-042** Before writing, for every `.sig`: it base64-decodes to a four-line minisign signature; `tools/minisign.js`
  verifies it over the asset's bytes against `--pubkey` (key id and signature); its trusted comment, as Tauri writes it
  (External facts), has a `version:` field equal to `<v>`. Any failure: exit 1 with `E_LATEST_SIGNATURE`, naming the
  asset, nothing written. Missing or extra assets for D6's table: `E_LATEST_ASSETS`, naming each (tasks TA-11): for
  each selected OS the updater asset and its `.sig` are required; the OS's release-only asset
  (`Krate-Manager-<v>-Linux-amd64.deb`, `Krate-Manager-<v>-macOS.pkg`) may be present or absent and is never named in
  `latest.json` (a build leg has no pkg yet, `publish` has it); any other file, a `.deb.sig` included (plan SI-21), is
  extra.
- **FR-043** `tools/feed-publish.js` `checkLatestJson(bytes, tag, version)` additionally requires each
  `platforms.*.url` to equal `https://github.com/<repo>/releases/download/<tag>/<one segment>` (the repo from
  `collect`'s `--repo`); otherwise `E_MANAGER_LATEST` and the existing degrade (D16). The Phase 3 fixture test `collect
  copies only a valid manager latest.json` gains an `api.github.com` case and a foreign-host case.
- **FR-044** Notes are the newest version's section only (decided 2026-11-01). `latest.json` `notes`, the release body's
  CHANGELOG part (FR-063) and the update line (FR-024) carry exactly the one `## [<v>] - <date>` section of the version being
  released, from its heading's following line to the next `## [` heading (exclusive), and no older section even when the
  running version is several releases behind. `manager-release-preflight.js` writes that section to the notes file (FR-051);
  `gen-manager-latest.js` copies the file as is (FR-041). There is no per-version aggregation in `core::self_update`.

### F. `release-manager.yml` (roadmap lines 825-832; Q1; D14)

- **FR-050** `on: workflow_dispatch` only, input `dry_run` (boolean, default `true`); `permissions: contents: read` at
  the top; `concurrency: { group: krate-manager-release, cancel-in-progress: false }`.
- **FR-051** Job `preflight` (`ubuntu-24.04`, no `environment`, no secret) runs `node tools/manager-release-preflight.js
  --app-dir apps/krate-manager --dry-run <bool> --tags <file> --repo "${{ github.repository }}"`, where `<file>` holds the remote tag names and release tag
  names (`git ls-remote --tags origin` and `gh release list --limit 1000 --json tagName`, as `release.yml:578-588`).
  The tool: runs `lint-manager-version.js`'s exported check (`E_PREFLIGHT_VERSION`); requires D15's CHANGELOG heading
  (`E_PREFLIGHT_CHANGELOG`, `E_PREFLIGHT_CHANGELOG_UNDATED`: a real release requires a dated heading, `Unreleased` passes only a dry run, decided 2026-11-01);
  refuses `E_PREFLIGHT_REPO` unless its `--repo` (`${{ github.repository }}`, the value every `latest.json` URL is
  built from) equals the `MANAGER_REPO` line of `crates/core/src/self_update.rs` (exactly one such line; both kinds of
  run), checked before `E_PREFLIGHT_TAG_EXISTS`, so the compiled constant and the release workflow cannot drift
  (plan SI-40, PD-27); refuses an existing `manager/v<v>` tag or release
  (`E_PREFLIGHT_TAG_EXISTS`) on dry runs and real runs alike, as roadmap lines 826-828 and 884-886 require (a dry run
  with an existing tag fails before any build). This departs from the plugin pipeline, whose "Tag must not exist" step
  is real-run only (`release.yml:578-579`, `if: ${{ !inputs.dry_run }}`); the roadmap text wins. It fails when the
  release list reaches the 1000-entry limit (the list could be truncated), as `release.yml:587` does
  (`E_PREFLIGHT_TAG_LIST`), also on both kinds of run. It does no ref check (D14). It requires FR-003's
  `plugins.updater` values; on a real run refuses a test key in
  `krate-signing.pub` (the `gen-release-manifest.js --signing-config` rule, Phase 2 PD-23). The key it checks is
  `--pubkey <file>` (default `tools/feed-contract/keys/krate-signing.pub`, resolved from the tool's own directory), and
  the exported `preflight(opts)` takes `pubkeyText`, so SC-011's fixture-key case runs without replacing the committed
  file (tasks TA-12). It outputs `version`, `tag`
  and the notes file. Every other job `needs` it, directly or through `build`.
- **FR-052** Job `build` (matrix `windows-latest`, `macos-latest`, and `linux` = `ubuntu-24.04` with `container:
  ubuntu:22.04`; `needs: [preflight, main-only]`; **no environment**; `permissions: contents: read`; no `secrets.*`
  anywhere in the job): checkout, Node 24, toolchain, `npm ci`, D13's targets on macOS, D12's packages in the
  container; then `node scripts/bundle.js --release` with D13 / D12's options in one bundler step, the same on both
  kinds of run, which makes a per-leg throwaway key (`tauri signer generate --ci` into `$RUNNER_TEMP`, a random
  masked password that never leaves the step) and passes it to the bundler; `KRATE_BUNDLE_EXTRA_CONFIG` is empty on
  every build leg, so the app is always built with the committed pubkey (plan SI-20, PD-23). Uploads
  `target/release-assets/` as a workflow artifact. The real key is used only after the build, by job `sign`
  (`needs: [preflight, build]`, `if: ${{ !inputs.dry_run }}`, `environment: signing`): it downloads the three legs'
  updater artifacts and re-signs each with `rsign sign`, the `.sig` being `base64 -w0` of the `.minisig`, as
  `release.yml` signs plugin assets; the only `env` carrying the two secrets is the `sign` job's `signing key` step,
  and the step after `sign updater artifacts` deletes the key. Its twin `sign-dry` (`if: ${{ inputs.dry_run }}`, no
  environment, no secret) runs the same steps with an `rsign generate` throwaway key in place of the `signing key`
  step and uploads only its public key. Job `main-only` (`needs: preflight`, `environment: signing`, one `echo` step,
  no checkout, no secret reference) keeps dry and real runs alike to `main` before any build (D14). No job-level or
  workflow-level `env` names a secret.
- **FR-053** Each build leg verifies, before upload: every `.sig` with `tools/minisign.js` against **its own**
  throwaway public key and its `version:` field (FR-042's check, run as `gen-manager-latest.js --check-only
  --platform-subset <os>`); the `sign` / `sign-dry` job verifies the release `.sig`s it makes against the committed key
  / its throwaway key (plan SI-20);
  zero `^warning:` lines in the build log (the manager-ci `build warnings` rule, `:233-246`); the artifact version equals
  `version.json`'s (`manager-ci.yml:368-389` rule).
- **FR-054** The macOS build leg also asserts: `lipo -archs` of `Contents/MacOS/krate-manager` and of
  `Contents/MacOS/krate-manager-helper` each list `x86_64 arm64`; `LSMinimumSystemVersion` is `15.0`; no
  `LSFileQuarantineEnabled`; `codesign --verify --deep --strict` passes and `codesign -dv` reports `Signature=adhoc`
  (the manager-ci steps `:391-429`, on the universal bundle).
- **FR-055** The real key reaches only the `sign` job's `signing key` step env, which writes it decrypted under
  `$RUNNER_TEMP` for the next step only, `rsign sign`, after which the following step deletes it; that job checks out
  no app source, runs no `npm` other than `npm ci --prefix tools/feed-contract --ignore-scripts` and no `cargo` other
  than an uncached `cargo install rsign2 --version 0.6.7 --locked` (no `actions/cache` or `Swatinem/rust-cache` step in
  the job, plan PD-35), both before the key step, and no `bundle.js` / `tauri` (plan SI-20). No step echoes the
  secrets, writes them to a file outside `$RUNNER_TEMP`, or passes them on a command line; `build`, `sign-dry`,
  `main-only` and `publish` receive no private key.
- **FR-056** Every build leg, dry or real, refuses a non-empty `KRATE_BUNDLE_EXTRA_CONFIG` (plan SI-20) and any `updater-e2e` in `cargo tree
  -p krate-manager -e features`; the `updater-e2e absent` check of FR-073 runs on every build leg, dry or real (a dry
  run builds the same binaries). Every `build` leg also carries a `ci-temp-roots absent` step (the roadmap's Phase 10 rule that `--features ci-temp-roots` is "a feature
  that `release-manager.yml` builds never enable", lines 800-801; manager-ci's own step is `manager-ci.yml:300-304`):
  `cargo tree -p krate-manager-helper -e features --locked` lists no `ci-temp-roots`, and the marker `--ci-roots`
  occurs zero times in the release helper binary that ships: the staged sidecar under `src-tauri/binaries/`, and on
  macOS also the universal bundle's `Contents/MacOS/krate-manager-helper`. On Linux no helper is staged
  (`bundle.js:89`, Phase 10 D18), so the Linux leg asserts that `src-tauri/binaries/` holds no
  `krate-manager-helper-*` and that `cargo tree` lists no `ci-temp-roots`, as `manager-ci.yml:306-313` does
  (plan SI-10).
- **FR-057** Job `publish` (`ubuntu-24.04`, `environment: signing`, `permissions: contents: write`, `needs: [preflight,
  build, sign, package-macos, updater-e2e, first-install]`, `if: !inputs.dry_run`, no `secrets.*`): `gh release create manager/v<v> --draft
  --latest=false --title "Krate Manager <v>" --notes-file <FR-063 body> --target ${{ github.sha }}`; `gh release upload`
  of every D6 asset and the `sign` job's `.sig`s (no `--clobber`; plan SI-20); `gen-manager-latest.js` over the
  downloaded build artifacts and those `.sig`s, against the committed key; upload
  `latest.json`; download the release's assets back with `gh release download` and compare each SHA-256 with the built
  file; then `gh release edit manager/v<v> --draft=false --latest=false`. Any failure before the edit leaves a draft
  (deletable by the user); nothing is published half-made.
- **FR-058** Job `feed` (`needs: publish`, `if: !inputs.dry_run`) is `uses: ./.github/workflows/feed.yml` with
  `dry_run: false` and `permissions: contents: write` (what `feed.yml`'s `deploy` needs); a dry run instead calls it with
  `dry_run: true` after `dry-publish` (FR-059), so the feed build and check run without a deploy.
- **FR-059** Job `dry-publish` (`if: inputs.dry_run`, no environment, no secret, `contents: read`, `needs` gains
  `sign-dry`, plan SI-20): runs `gen-manager-latest.js` over the dry-run artifacts and `sign-dry`'s `.sig`s with `--tag
  manager/v<v>` and `sign-dry`'s throwaway public key, then
  `feed-publish.js`'s `checkLatestJson` over the result (exported for the check), and uploads `latest.json` and every
  asset as the workflow artifact `manager-release-dry-run`. It creates no release, tag or Pages commit.

### G. First-install packaging (roadmap lines 849-855; Q5, Q6; D11, D12)

- **FR-060** Windows: the NSIS setup of FR-052 (`installMode: currentUser`, `tauri.conf.json:23`), unchanged.
- **FR-061** Job `package-macos` (`macos-latest`, `needs: build`, no secret): stages `Applications/Krate Manager.app`
  from the build artifact with `ditto` (keeps the signature and modes), writes a component plist with
  `pkgbuild --analyze --root <stage>` and sets `BundleIsRelocatable` to `false` for the app, then `pkgbuild --root
  <stage> --component-plist <plist> --identifier com.krateaudio.krate-manager --version <v> --install-location /
  Krate-Manager-<v>-macOS.pkg`. No `--scripts` (no `postinstall`), no `--ownership` flag (D11, decided 2026-11-01: the root-owned bundle is the accepted state). The identifier contains a hyphen, so the
  helper's `uninstall_pkg` pattern `^com\.krateaudio\.[a-z0-9]+$` (Phase 10 FR-033) can never name the manager itself.
- **FR-062** `package-macos` asserts on the pkg: `pkgutil --payload-files` lists `./Applications/Krate Manager.app/...`
  only; `release-facts.js`'s `machoFacts` over the expanded payload reports `x86_64` and `arm64` for both executables.
- **FR-063** The release body is the CHANGELOG section (D15) followed by a fixed "Install" block naming each asset per
  OS and, for macOS, the provisional one-liner `curl -fLO
  https://github.com/<repo>/releases/download/manager/v<v>/Krate-Manager-<v>-macOS.pkg && sudo installer -pkg
  Krate-Manager-<v>-macOS.pkg -target /`, marked provisional until Phase 12's Open Anyway check (Q5). Built by
  `manager-release-preflight.js --body <out>`, tested for its exact text.
- **FR-064** Linux: AppImage and `.deb` from the container (D12). The `.deb` declares no dependency the 22.04 build
  cannot satisfy (the bundler's own control file, unchanged), proven by an install on 22.04: `first-install`'s leg
  `ubuntu-22.04-container` (FR-076).
- **FR-065** The Linux build leg records the glibc floor (D12) as `::notice title=glibc floor::<x.y> (container <x.y>)`
  and fails when the binary's floor exceeds the container's.

### H. The end-to-end self-replace gate (roadmap lines 859-862, 873; D10)

- **FR-070** `src-tauri` `[features]` gains `updater-e2e = []` (never default). Code behind it lives in
  `src-tauri/src/self_update.rs` inside one `#[cfg(feature = "updater-e2e")]` module, which contains the marker string
  `krate-e2e-marker-v1` and uses it at run time (FR-073), plus exactly two single-statement `#[cfg(feature =
  "updater-e2e")]` call sites in `src-tauri/src/lib.rs` (the feed / HTTP override and the `setup` call) that only call
  into that module; pinned by a per-file count (FR-094) (plan SI-25).
- **FR-071** Job `updater-e2e` (matrix `windows-latest`, `macos-latest`, `ubuntu-24.04`; name `updater-e2e (${{
  matrix.os }})`; `needs: preflight`; no environment, no secret; `contents: read`): generates a throwaway key, builds
  "old" (`<v>`) and "new" (`<v>` with patch + 1) with `bundle.js --release --features updater-e2e` and D10's
  `--config`, signs the "new" updater asset with the release signing steps (`install rsign2`, `throwaway signing key`,
  `updater asset list`, `sign updater artifacts`, `delete signing key (after signing)`, byte-equal to `sign-dry`'s),
  the "old" build trusting that rsign key; the Tauri CLI's own `.sig`s of the e2e builds are overwritten and never
  served, and each leg's `rsign signature served` notice records that the `.sig` served is the rsign one (plan SI-34,
  PD-35); writes a `latest.json` for "new" from those `.sig`s with `gen-manager-latest.js --repo-url-base
  http://127.0.0.1:<port> --only <os>` (test-only flags that the shape test confines to this job, plan SI-18), serves it with `apps/krate-manager/scripts/e2e-update-server.js` (Node, binds
  `127.0.0.1` only, serves one directory, logs each request; the package path is the per-tag
  `/<MANAGER_REPO>/releases/download/manager/v<new>/<asset>` FR-011 requires, plan SI-40), installs or places "old", launches it,
  and waits at most 300 s for `launched-<new>` (the `.deb` case: for its refusal, FR-072).
- **FR-072** Every leg serves the rsign-made `.sig` of FR-071's copied `sign updater artifacts` step, so each
  `launched-<new>` is the updater's own verifier accepting a `.sig` made by the release signing steps (step notice
  `rsign signature served`; plan SI-34). Per leg: **Windows**: "old" installed with its NSIS setup `/S` (`Start-Process -Wait -PassThru`, exit 0);
  after the run, the installed `krate-manager.exe` ProductVersion is "new" and the server log shows the setup
  downloaded once. **macOS**: "old" `.app` placed with `ditto` in `$RUNNER_TEMP/apps/` (user-owned, so the unprivileged
  swap applies); after the run, `CFBundleShortVersionString` is "new", `codesign --verify --deep --strict` passes,
  `xattr -lr` shows no `com.apple.quarantine`, and the app is still at the same path. **macOS, second case
  `pkg-installed`** (the state both documented install routes produce, D11): "old" wrapped in a pkg by FR-061's
  `pkgbuild` call and installed with `sudo installer -pkg … -target /`, so `/Applications/Krate Manager.app` is
  root-owned; a SHA-256 manifest of every file in the bundle is recorded; "old" is launched and the job asserts, within
  60 s, that the updater has reached the administrator branch (the authorization prompt is pending, observed as the
  `SecurityAgent` process: the updater runs `do shell script … with administrator privileges` in-process through
  `osakit`, so no `osascript` child exists before authorization, and the dialog is drawn by `SecurityAgent`; the job
  polls `pgrep -x SecurityAgent`, plan SI-13, PD-13). The job then cancels the prompt by ending that
  process (`sudo pkill -x SecurityAgent`), which is the refused prompt, and asserts: `<marker>/error-E_SELF_UPDATE_INSTALL` appears within 30 s; no
  `launched-<new>`; the bundle's SHA-256 manifest is unchanged; `CFBundleShortVersionString` is still "old";
  `codesign --verify --deep --strict` exit 0; "old" still launches by the probe method. If the plan's measurement
  shows a hosted runner cannot produce or cancel the prompt, the build stage records that measurement and surfaces it
  to the user; the case is never dropped or weakened silently. **Linux**: two cases, the AppImage run under `xvfb-run`
  from `$RUNNER_TEMP/apps/` (afterwards the file at that path is "new"), and the `.deb` case (plan SI-21): "old"
  installed with `sudo apt-get install -y ./…deb`, launched under `xvfb-run`; asserts
  `<marker>/error-E_SELF_UPDATE_UNSUPPORTED` within 300 s, no `launched-<new>`, `dpkg-query -W -f '${Version}'
  krate-manager` still "old", and zero package GETs in the server log. The job installs `libfuse2t64` (Ubuntu 24.04 ships no libfuse2, which a type-2 AppImage needs) and does
  **not** set `APPIMAGE_EXTRACT_AND_RUN`, so the AppImage runs, and self-updates, through the same runtime path as a
  user's double-click.
- **FR-073** A `check`-job step in `manager-ci.yml` and a step in each `build` leg (FR-056), `updater-e2e absent`: `cargo
  tree -p krate-manager -e features --locked` lists no `updater-e2e`; the marker occurs at least once in
  `src-tauri/src/self_update.rs` and zero times in the release binary (and, on macOS, in the bundle's executable).
  The `updater-e2e` job asserts the marker occurs at least once in its own e2e `krate-manager` binary (step `marker
  present`), so the release legs' zero count is known to be meaningful (plan SI-25). `manager-ci.yml` `check` also
  runs `clippy updater-e2e` on every leg (PD-32: dummy `KRATE_E2E_*` values, compiled and linted, never run), which
  writes only debug-profile check metadata, so `updater-e2e absent` reads the same (plan SI-32: ruling P-4 of
  `spec.md:1138-1145`; tasks TA-14).
- **FR-074** Each leg annotates the old and new versions, the run time and the server's request log; any assertion
  failure is an `::error` naming the leg.

### I. First-install artifact checks (roadmap lines 878-883; Q5)

- **FR-075** Job `first-install` (matrix FR-071's three runners plus `ubuntu-22.04-container`, `needs: [build,
  package-macos]`, no secret, `contents: read`) runs on the artifacts of this run, on fresh runners, never the dev box.
- **FR-076** **macOS**: copy the pkg, give the copy `com.apple.quarantine` (`xattr -w com.apple.quarantine
  "0083;<hex time>;Safari;<uuid>"`, as a browser would), `sudo installer -pkg <copy> -target /`, exit 0; then
  `xattr -lr "/Applications/Krate Manager.app"` shows no `com.apple.quarantine`; the whole installed tree is root-owned
  and not group- or other-writable, so no file the elevated path executes can be replaced or renamed away (Phase 10
  D19's question is about replaceability, not one file's mode): `find "/Applications/Krate Manager.app" \( ! -user root
  -o -perm -g+w -o -perm -o+w \) -print` prints nothing, and `stat -f '%Su %Lp' /Applications` shows owner `root` and
  no other-write bit; `pkgutil --pkg-info com.krateaudio.krate-manager` succeeds; the app launches by the probe method
  (`open -n`, running after 12 s by `pgrep`, then `pkill`). **Linux** (`ubuntu-24.04`): `libfuse2t64` installed,
  `APPIMAGE_EXTRACT_AND_RUN` not set (as FR-072); `sudo apt-get install -y ./Krate-Manager-<v>-Linux-amd64.deb` exit 0,
  `krate-manager` runs under `xvfb-run` for 12 s; the AppImage (`chmod +x`) the same. **Linux, leg
  `ubuntu-22.04-container`** (`ubuntu-24.04` runner, `container: ubuntu:22.04`; the Q6 floor): `apt-get update`, then
  `apt-get install -y ./Krate-Manager-<v>-Linux-amd64.deb xvfb xauth` exit 0, and `krate-manager` runs under
  `xvfb-run` for 12 s. **Windows**: the NSIS
  setup with `/S` (`Start-Process -Wait -PassThru`, exit 0); the per-user install is
  `$LOCALAPPDATA\Krate Manager\krate-manager.exe` (NSIS template `installer.nsi:546-547` at `tauri-cli-v2.12.4`; `MAINBINARYNAME` is the
  cargo bin `krate-manager`; plan SI-14), which exists, and nothing is under `Program Files`; it runs for 12 s, then is stopped.
- **FR-077** The `first-install` launches run the release builds as shipped, so they try the feed
  (`https://krateaudio.com/versions.json`, `lib.rs:29`) and endpoint (D7 checks once per launch), which FR-078 has pointed at
  loopback: they reach nothing live. The `updater-e2e` builds fetch nothing live (D10). None of these launches installs anything (self-update needs a click, D7). Every
  `first-install` verdict is independent of the live fetch: the launch assertion is process presence only (`pgrep` /
  the running process after 12 s), no assertion reads feed or `latest.json` content or the app's log, and a failed
  fetch (the expected outcome behind FR-078) cannot turn the job red (the app shows a muted line and keeps running, FR-024).
  No extension of the roadmap's hosted-runner exception is made (Departures 8).
  No `run` step of `first-install` or `updater-e2e` invokes `gh`, `curl` or `wget`, or names `github.com` or
  `api.github.com`; artifacts arrive only through `actions/download-artifact` (asserted by FR-091's shape test).
- **FR-078** Live host blocked (decided 2026-11-01). Before the launch step of each `first-install` leg (and of no other job),
  a step appends `127.0.0.1 krateaudio.com` and `127.0.0.1 www.krateaudio.com` to the runner's hosts file (`/etc/hosts` with
  `sudo` on macOS and Linux, as root in the `ubuntu-22.04-container` leg; `C:\Windows\System32\drivers\etc\hosts` from the
  elevated runner shell on Windows), flushes the resolver cache where the OS has one, and then asserts that `krateaudio.com`
  resolves to `127.0.0.1` (`getent hosts` / `dscacheutil -q host` / `Resolve-DnsName`); the step fails the leg if it does not,
  so the block cannot silently not apply. The hosts edit is on the ephemeral runner only (FR-090). The shape test pins the
  step and its order before the launch step on every `first-install` leg and its absence elsewhere (FR-091; plant: the step
  removed from one leg).

### J. The refusal harness (roadmap lines 865-872; D9)

- **FR-080** `tests/fixtures/updater-refusals/` (bin crate, excluded from the workspace, own `Cargo.lock`, the Windows
  manifest `build.rs` of D9) runs these scenarios, each against its own core fixture server on `127.0.0.1` with a
  throwaway key made in a temp dir, through `krate_manager_lib::self_update`, under `UrlPolicy::LoopbackOnly` (the
  fixture server is plain http on loopback, `fixture_server.rs:144-145`). Every scenario's `HttpConfig` has a static
  proxy pointing at a second fixture server, the **trap proxy** (routes none; it records every request, including
  `CONNECT`, `fixture_server.rs:443`), with a `no_proxy` bypass for `127.0.0.1`, so D17's `proxy_choice` sends loopback
  traffic direct and any request for a non-loopback host lands on the trap instead of the network. The scenarios run
  **concurrently**: the harness runs one child process per scenario (`--scenario <name>`), all started at once
  (plan SI-9, PD-11), each child with its own mock app on its own main thread, its own fixture servers (own ports), its own
  temp dir and its own process-wide registries; the parent prints the result lines sorted by scenario name. Never one
  scenario after another. Every package URL a downloading scenario serves has the per-tag path
  `/<MANAGER_REPO>/releases/download/manager/v<announced>/<asset>` FR-011 requires under `LoopbackOnly` too
  (plan SI-40).

  | Scenario | Expected |
  |---|---|
  | `good` | `check` → `Some(update)`; `download` → bytes; `verify(bytes)` → Ok; `Update.timeout == Some(DOWNLOAD_TIMEOUT)` after `prepare_download` with `UpdateTimeouts::production()` (plan SI-23); the view's action is `install_and_restart` (plan SI-31) |
  | `tampered` (the served package differs by one byte from the signed one) | `download` → `E_SELF_UPDATE_SIGNATURE` |
  | `other-key` (signed by a second throwaway key) | `download` → `E_SELF_UPDATE_SIGNATURE` |
  | `replayed-old` (announced `0.2.0`, url and signature of a valid `0.0.9` package signed `version:0.0.9`; the CVE-2026-95625 shape) | `download` → `E_SELF_UPDATE_SIGNATURE` (from `SignedVersionMismatch`) |
  | `unversioned` (valid signature without a `version:` field) | `download` → `E_SELF_UPDATE_SIGNATURE` (from `MissingSignedVersion`) |
  | `downgrade` (announced `0.0.9`, validly signed `version:0.0.9`, current `0.1.0`) | `check` → `None`: no update offered |
  | `same-version` (announced = current) | `check` → `None` |
  | `api-host` (the fixture `latest.json` names `https://api.github.com/repos/o/r/releases/assets/1` as the download URL) | `E_SELF_UPDATE_URL { reason: ApiHost }` before `download` (a `Policy` reason reads `not ok`); the trap proxy records zero requests, `CONNECT` included, and the fixture server zero package requests |
  | `latest-path` (download URL `http://127.0.0.1:<port>/o/r/releases/latest/download/x`, loopback so only FR-011's own rule can refuse it) | `E_SELF_UPDATE_URL { reason: LatestPath }`; zero requests for that path |
  | `redirect-to-api-host` (the package route answers `Reply::Redirect` 302 to `https://api.github.com/repos/o/r/releases/assets/1`) | `download` → `E_SELF_UPDATE_URL { reason: ApiHost }` (FR-011a); the trap proxy records zero requests |
  | `redirect-to-http` (the package route answers 302 to `http://example.invalid/pkg`) | `download` → `E_SELF_UPDATE_URL { reason: Policy }` (FR-011a runs `check_update_url` first; under `LoopbackOnly` a non-loopback `http` hop is a `Policy` refusal; the https → http refusal itself is SC-001's `HttpsOnly` hop row); the trap proxy records zero requests |
  | `recheck-replaces` (a check offers `0.2.0`; the fixture then serves a `latest.json` for `0.3.0`; a second check; then install) | install downloads the `0.3.0` package URL and never the `0.2.0` one (the fixture's request log); the scenario's installer is the refusing stub |
  | `failed-install-clears` (check offers `0.2.0`; install reaches the refusing stub, which returns `E_SELF_UPDATE_INSTALL`) | the first install → `E_SELF_UPDATE_INSTALL`, with the progress events `[Downloading, Installing]` (plan SI-23); a second install with no new check → `E_SELF_UPDATE_NONE`, zero further package requests |
  | `corrupt-manifest` (truncated JSON; a platform entry without `signature`) | `check` → `E_SELF_UPDATE_MANIFEST` |
  | `no-platform` (no key for this OS) | `check` → `E_SELF_UPDATE_MANIFEST` |
  | `offline` (port closed) | `check` → `E_SELF_UPDATE_NETWORK` |
  | `busy` (the harness holds a fake elevation guard) | install → `E_SELF_UPDATE_BUSY`, zero requests for the package; then, the guard released and no new check, a second install reaches the refusing stub (`E_SELF_UPDATE_INSTALL`) after exactly one package request: the FR-015 refusal kept the update (FR-023; plan SI-23, SI-36) |
| `busy-after-download` (FR-019: the fake elevation guard is free at install start and becomes held when the package route is served) | install → `E_SELF_UPDATE_BUSY` after exactly one package request; the installer is never reached; while the package route is served, the watcher thread sees `self_update::in_flight(state_dir)` and `action_gate(state_dir) == Err(SelfUpdating)` for the `state_dir` passed to `install`; the progress events are `[Downloading]` (plan SI-23); a second install with no new check → `E_SELF_UPDATE_NONE` (kept update cleared); `SelfUpdateInFlight` is false afterwards |
| `stall` (FR-027: the package route sends headers and part of the body, then stalls; the harness passes a 1 s stall bound to the production builder function) | `download` → `E_SELF_UPDATE_NETWORK` within 10 s of the request; no retry (the fixture records exactly one request for the package path); while the package route is served, `in_flight(state_dir)` and `action_gate(state_dir) == Err(SelfUpdating)` (plan SI-23); the kept update is cleared |
| `endpoint-latest-path` (the mock config's endpoint is `http://127.0.0.1:<port>/o/r/releases/latest/download/latest.json`) | `check` → `E_SELF_UPDATE_URL { reason: LatestPath }`; zero requests on the fixture server and the trap proxy (plan SI-23) |
| `redirect-loop` (the package route answers 302 to itself) | `download` → `E_SELF_UPDATE_URL { reason: Policy }` after exactly `MAX_REDIRECTS + 1` = 6 requests to that path (plan SI-23) |
| `deb-install` (`UpdateInputs.packaging = SystemPackage`) | the view's action is `open_release_page` with the `github.com` page of the announced version (plan SI-31); install → `E_SELF_UPDATE_UNSUPPORTED`, zero package requests (plan SI-21, SI-23) |
| `map-errors` (no server) | `map_error` over a constructed instance of every `tauri_plugin_updater::Error` variant constructible outside the crate (31 of the 32; `Reqwest` is reached by the network scenarios), each asserted to its code and reason (FR-013; plan SI-23) |
| `socks-proxy` (the good path through the harness's own SOCKS relay, `src/socks.rs`, once with `socks5://` and once with `socks4://`) | `check` → `Some`, `prepare_download` sets `Update.proxy` to the relay URL and `no_proxy` false, `download` + `verify` → Ok; the relay records a CONNECT of its version byte (5, then 4) to the fixture server's address for each fetch and nothing else; the fixture server records `GET /latest.json` and the package GET once per run; the trap proxy records nothing; the panicking installer is never reached (plan SI-30: ruling P-1 of `spec.md:1138-1145`) |
| `rsign-format` (the good path with the committed rsign-made fixture, `fixtures/rsign/`: its `rsign.pub` as the mock `pubkey`, `package.bin.minisig` in the `base64 -w0` framing as the `latest.json` signature) | `check` → `Some`, `download` + `verify` → Ok; the fixture key is refused if it equals the committed key (plan SI-34) |
| `production-inputs` (no server, no mock app) | `UpdateInputs::production(HttpConfig::production())` has `policy == HttpsOnly` and `timeouts == UpdateTimeouts::production()` (plan SI-35) |

  In every scenario but `recheck-replaces`, `failed-install-clears` and `busy` (plan SI-23) the harness's
  `UpdateInstaller` panics if called; those three use a refusing stub that records the call and returns `E_SELF_UPDATE_INSTALL { detail: "harness" }` and
  never touches `Update::install` (FR-094 confines that call to `NativeUpdateInstaller`). The `good` scenario ends at
  `verify` and never calls install. The mock context sets `package_info.version` to the scenario's current version.
  The `busy` scenario's elevation guard is a fake `&dyn Elevation` of its own (FR-015's parameter) and its `BUSY` key is
  its own temp state dir, so concurrent scenarios do not share busy state.
- **FR-081** Confinement. (a) Every root the harness passes (the `Roots`, the state dir, the marker and download
  dirs) is built from its temp dir with `krate_manager_core::test_support::temp_roots` (`test_support.rs:206`), which
  panics outside the temp dir; temp dirs come from `tempfile::TempDir::new()` (`Roots::new`, `std::env::var` and
  `temp_dir` are disallowed by `clippy.toml:3-9` for every crate under `apps/krate-manager/`, the harness included;
  plan SI-7); it never calls `Roots::from_os` (`lib.rs:45`),
  `SigningKey::release`, `assemble_context` or `krate_manager_lib::run` (pinned by FR-094). (b) every scenario
  process is started with `LOCALAPPDATA`, `APPDATA`, `USERPROFILE`, `HOME`, `TMP`, `TEMP`, `TMPDIR` and every
  `XDG_*_HOME` / `XDG_RUNTIME_DIR` set to subdirectories of its temp dir (plan SI-8), so even a default-path call
  lands there; on Windows this does not move known-folder lookups (dirs-sys resolves `FOLDERID_RoamingAppData` /
  `FOLDERID_LocalAppData` with `SHGetKnownFolderPath`, which ignores them), a recorded limit (plan SI-29, PD-11). (c) It
  binds only through one helper that refuses any non-loopback address (`E_HARNESS_BIND`); the SOCKS relay of
  `socks-proxy` binds loopback through the same check and relays only to `127.0.0.1` (plan SI-30). (d) Its public key
  is never the committed one: it refuses to start if it equals `krate-signing.pub` (`E_HARNESS_KEY`); the refusal runs
  in every scenario child after key generation and before any server starts, and `--plant committed-key` makes the
  `good` child adopt the committed public key through the same key-producing function (plan SI-23). (e) Self-check:
  after the run it walks the redirected env dirs and asserts every file it created is under the temp dir, and that
  every path it recorded as written (state lock, downloads, markers) starts with the temp dir, printing `ok
  confinement` (counted as a scenario line) or `not ok confinement <path>`. (e′) On Windows the parent snapshots,
  read-only, the entries of `<FOLDERID_LocalAppData>\com.krateaudio.krate-manager` and
  `<FOLDERID_RoamingAppData>\com.krateaudio.krate-manager` (`dirs::data_local_dir()` / `dirs::data_dir()`, dirs 7.0.0
  `src/lib.rs:122`, `:134`) before spawning and after the last child exits; any new entry is `not ok confinement
  <path>` (plan SI-29). (f) Plants: `--plant committed-key` (the committed public key as
  its own) exits 1 printing `E_HARNESS_KEY` before any scenario runs; `--plant bind-any` (asks the bind helper for
  `0.0.0.0:0`) exits 1 printing `E_HARNESS_BIND`; neither counts toward FR-082's scenario count.
- **FR-082** It prints one line per scenario (the 26 of FR-080's table plus `confinement`, FR-081 (e)) and a final
  `harness: <n> ok, <m> not ok`; exit 0 iff `m == 0` and `n` equals the line count (27), so a green run prints
  `harness: 27 ok, 0 not ok` (plan SI-35, which supersedes SI-23's and SI-30's count).
- **FR-083** It is clippy-clean (`cargo clippy --manifest-path tests/fixtures/updater-refusals/Cargo.toml
  --all-targets --locked -- -D warnings`) and `cargo fmt --check`-clean. Beside it, `manager-ci.yml` `check` runs
  `clippy updater-e2e` on every leg (PD-32: dummy `KRATE_E2E_*` values, compiled and linted, never run); FR-091's
  manager-ci shape test pins it in `STEPS`, `ASSERTION_STEPS` and `COMMANDS`, and confines `--features updater-e2e`
  and `KRATE_E2E_*` in `check` to that step (plan SI-32: ruling P-4 of `spec.md:1138-1145`; tasks TA-14).
- **FR-084** A `check`-job step `updater refusals` in `manager-ci.yml` (every leg) builds and runs the harness with
  `CARGO_TARGET_DIR="$PWD/target"` (as `clippy-guard`, `:123`), and passes only on exit 0 with every scenario name of
  FR-082 printed `ok` exactly once and the summary `harness: 27 ok, 0 not ok`, and on each of FR-081 (f)'s two plants
  exiting 1 with its named code; the lint step of FR-083 runs beside it. Under D9's fallback (plan SI-33, PD-33; tasks
  TA-22) the build goes on: if the first local run shows the bin fails to load on Windows with the manifest embedded,
  the measurement (exit code, loader message) is recorded in `evidence/d9-windows.txt`; the harness then runs on macOS
  and Linux only, locally in WSL and on `check (macos-latest)` / `check (ubuntu-24.04)` (the step gets `if: runner.os
  != 'Windows'`); it is still built and linted on Windows; SC-007's Windows row is a recorded gap quoting the load
  error, never a pass.

### K. Guards, scope fences and roadmap amendments (Cross-Cutting lines 999-1040)

- **FR-090** No test, gate or agent on the dev box installs or replaces the manager, runs an NSIS setup, `installer`,
  `pkgbuild`, `dpkg`, `apt-get install`, `sudo`, `tauri signer` with a real key, or reads the signing secrets. The only
  exceptions are `release-manager.yml`'s `build`, `sign`, `sign-dry` (plan SI-20), `package-macos`, `updater-e2e`,
  `first-install`, `publish` and `feed` jobs on ephemeral GitHub-hosted runners, user-dispatched; `updater-e2e` and `first-install` write
  only under `$RUNNER_TEMP`, the runner user's `%LOCALAPPDATA%` / `/Applications` / the dpkg database of that VM, the
  runner user's per-user directories of the app identifier `com.krateaudio.krate-manager` (the manager's state dir:
  macOS `~/Library/Application Support/com.krateaudio.krate-manager`, `platform/macos.rs:34-35`; Linux
  `$XDG_DATA_HOME/com.krateaudio.krate-manager` or `~/.local/share/com.krateaudio.krate-manager`,
  `platform/linux.rs:31-37`; Windows under `%LOCALAPPDATA%`, `platform/windows.rs:37-41`, already listed) and the
  per-user data and caches the OS webview keeps for that app, all on the ephemeral VM (plan SI-38: chosen over
  redirecting `HOME` / `XDG_DATA_HOME`, because the macOS cases launch through `open -n`), and, in `first-install`
  only, that VM's hosts file (FR-078).
- **FR-091** `feed-tools.yml`: path filter gains `tools/gen-manager-latest.js`, `tools/manager-release-preflight.js`,
  `.github/workflows/release-manager.yml`; `PLAN` and `NAMES` gain the new top-level tests: `gen-manager-latest writes
  per-tag latest.json`, `gen-manager-latest refuses what it cannot verify`, `manager release preflight refuses by
  name`, `manager release body is exact`, `release-manager.yml keeps the Phase 11 shape`. The shape test
  (`tools/feed-contract/tests/release-manager-shape.test.js`) asserts FR-050 to FR-059, FR-061, FR-071, FR-075: the
  trigger set; job names and `needs`; `environment: signing` exactly on `main-only`, `sign` and `publish`; every
  `secrets.TAURI_SIGNING_*` reference sits in the env of the `sign` job's `signing key` step, and no job-level or
  workflow-level `env` names a secret; no step of `sign` / `sign-dry` runs `npm` (but the one `npm ci --prefix
  tools/feed-contract --ignore-scripts`), `cargo` (but the `rsign2` install), `bundle.js`, `tauri` or `npx`, and none
  of those follows the key step; neither job uses `actions/cache` or `Swatinem/rust-cache` (PD-35); the step after
  `sign updater artifacts` deletes the key; the two jobs' step lists are equal but for the key step; `main-only` runs
  only `echo` (plants: a secret added to a `build` step; an `npm ci` added after the key step; the delete step
  removed; a cache step added back) (plan SI-20); `updater-e2e`'s copies of `install rsign2`, `throwaway signing key`,
  `updater asset list`, `sign updater artifacts` and `delete signing key (after signing)` are byte-equal to
  `sign-dry`'s in `name` and `run` (plant: one character changed in `updater-e2e`'s copy; plan SI-34);
  `ci-temp-roots` and `--ci-roots` occur nowhere in `release-manager.yml` except FR-056's `ci-temp-roots absent`
  step, and no `--features` argument anywhere names `ci-temp-roots` (plant: `--features ci-temp-roots` added to a
  build step); FR-056's `ci-temp-roots absent` step is present on `build`; no `tauri-apps/tauri-action`; no
  `APPIMAGE_EXTRACT_AND_RUN` in `first-install` / `updater-e2e`; no `run` step of `first-install` / `updater-e2e`
  invokes `gh`, `curl`, `wget` or names `github.com` (FR-077); the FR-078 hosts step on every `first-install` leg before its
  launch step and in no other job (plant: removed from one leg); the hosts file edited nowhere else; no `/releases/latest/` and no `api.github.com`
  anywhere; `--features updater-e2e`, `--repo-url-base`, `--only` and `dangerousInsecureTransportProtocol` only in
  `updater-e2e` (plan SI-18);
  `sudo`, `installer -pkg`, `apt-get install ./` only in `updater-e2e` / `first-install` (and `package-macos` uses no
  `sudo`); `--latest=false` on create and edit; no `--clobber`; `dev-feed` absent (Phase 4 FR-107); each with a planted
  mutation reported. `manager-ci-shape.test.js` gains FR-073's and FR-084's steps on `check`, and the `clippy
  updater-e2e` step of FR-083 in `STEPS`, `ASSERTION_STEPS` and `COMMANDS`, with `--features updater-e2e` and the
  three `KRATE_E2E_*` names confined, within `check`, to that step (plan SI-32).
- **FR-092** No change to `plugins/`, `dsp/`, `cmake/`, `extern/`, `release.yml`, `backfill-sign.yml`, `ci.yml`,
  `docs.yml`, `feed.yml`, `tools/gen-feed.js`, `tools/feed-contract/` schemas and `CONTRACT.md`, the helper crate's
  behaviour, or any Phase 5-10 core behaviour beyond FR-015 / FR-016 and the action gate of FR-017 / FR-018 (decided 2026-11-01: `download_plugin`, `install_plugin`, `remove_plugin` and the elevated batch start refuse while a self-update is in flight; nothing else of Phases 9 and 10 changes). `tools/feed-publish.js` changes only by FR-043.
- **FR-093** Roadmap amendments (`specs/Krate-manager-roadmap.md`), made by the build stage: (a) Phase 11 lines
  828-830 and 836-837 (decided 2026-11-01: no `tauri-action`): the build, upload and `latest.json` route of D4 (no `tauri-action`, no
  `updaterJsonKeepUniversal`; the reason: `tauri-action` 1.0.0's `api.github.com` URLs); (b) the Phase 11 endpoint
  bullet (856-858): "one endpoint, `https://krateaudio.com/manager/latest.json`; no stable second host (D5)" (decided 2026-11-01, OQ-1); (c) Phase 12's user-performed checks gain: "the
  self-update of a pkg-installed manager on macOS: record the admin prompt and that the swapped bundle is then
  user-owned (Phase 11 D11)"; (d) the Phase 11 status line, written by the workflow's `mark-roadmap` step only after
  compliance has zero fail / partial items (`.claude/workflows/krate-manager-phase.js:766-773`; tasks TA-15), never by a
  task, so it is absent until that step runs; (e) Repository Layout line 232 becomes
  "`.github/workflows/release-manager.yml  bundle.js --release matrix, manual dispatch, tag manager/vX.Y.Z,
  tools/gen-manager-latest.js writes latest.json → feed.yml (Phase 11 D4)`", the Repository Layout tool list
  (lines 234-243) gains `tools/gen-manager-latest.js` and `tools/manager-release-preflight.js` with one-line purposes,
  and the 🆕 row at line 211 gains the same two tools; (f) dropped (decided 2026-11-01: the hosted-runner exception, lines
  1006-1008, is not widened; FR-078 blocks the live host instead). No other line changes.
- **FR-094** `config_guard.rs` and `injection.rs` widen exactly so far: `src-tauri` `[features]` becomes exactly
  `["dev-feed = []", "manual-elevation = []", "updater-e2e = []"]`; the workspace `exclude` (`Cargo.toml:3`) gains
  `tests/fixtures/updater-refusals` (`exclude` only: a member would link `tauri` into `cargo test --workspace`, D12a),
  and `apps/krate-manager/.gitignore` gains `/tests/fixtures/updater-refusals/target/` beside the clippy-guard line
  (plan SI-11); `tauri-plugin-updater` and `tauri_plugin_updater` occur only in the workspace
  `Cargo.toml`, `src-tauri/Cargo.toml`, `src-tauri/src/lib.rs`, `src-tauri/src/self_update.rs`, `commands.rs` (through
  `self_update`, if needed) and the harness; `reqwest` only in `src-tauri/Cargo.toml` and
  `src-tauri/src/self_update.rs`, and `config_guard` pins FR-001's `reqwest` line exactly (plan SI-28); `.install(` on
  an `Update`, `download_and_install`, `restart(`, `process::restart` and `NativeUpdateInstaller` in the workspace and
  the harness: only inside `impl … for NativeUpdateInstaller` (the harness names none of them) (plan SI-28); the
  `. install (` / `download_and_install` / `. restart (` / `process :: restart` token scan covers `src-tauri/src/**`
  only, plus a separate harness rule, because core cannot name the updater and `install_tests.rs` holds a Phase 9
  fixture method `fx.install(..)` (tasks TA-13); the `E_INSTALL_SELF_UPDATING` code literal occurs only in
  `core::self_update`, and `SelfUpdating` is constructed only in `action_gate` (plan SI-15; plant: the gate removed
  from one of the four entry points); `feature = "updater-e2e"` counted per file: `src-tauri/src/self_update.rs` 1,
  `src-tauri/src/lib.rs` 2, every other file under `crates/*/src` and `src-tauri/src` 0 (plan SI-25); the PD-36 pins
  (plan SI-35): the body of `UpdateInputs::production` is exactly `Self { http, policy: UrlPolicy::HttpsOnly,
  timeouts: UpdateTimeouts::production(), packaging: packaging() }`, `commands.rs` builds inputs only through
  `UpdateInputs::production(`, and `UrlPolicy::LoopbackOnly` occurs in `src-tauri/src/self_update.rs` only inside `mod
  e2e`; FR-003 (e)'s tokens as stated; the harness source
  (`tests/fixtures/updater-refusals/`) names none of `Roots::from_os`, `SigningKey::release`, `assemble_context`,
  `krate_manager_lib::run` (FR-081 (a)), no `dirs::` outside `src/confinement.rs`, no `app.path()` and no
  `BaseDirectory` (plan SI-28). Phase 8's `tar` rule widens (plan SI-2; tasks TA-4): no workspace manifest names
  `tar` (unchanged); per lockfile (`Cargo.lock`, the clippy-guard lock and `tests/fixtures/updater-refusals/Cargo.lock`),
  (1) a walk over `[[package]].dependencies` from the lock's path packages (`krate-manager`, `krate-manager-core`,
  `krate-manager-helper`, `clippy-guard`, `updater-refusals`), never expanding `tauri-plugin-updater`, that reaches
  `tar` is a finding (so a locked `tar` is allowed only when every path from a workspace member to it passes through
  `tauri-plugin-updater`, which depends on `tar` + `flate2` unconditionally on macOS), and (2) the number of `[[package]]
  name = "tar"` entries must not exceed the number of `tar` entries in `tauri-plugin-updater`'s own `dependencies` list
  (zero when the updater is absent), so an orphan `tar` entry stays reported; plants: `tar` under core's
  `[dependencies]`, a lock where `krate-manager-core` reaches `tar`, an orphan `tar` entry. The literal scans stay
  unchanged and gain no exemption (plan SI-4): `injection.rs:23-31` (`krateaudio.com`, `api.github.com` in core outside
  `platform/`), `roster.rs:11` and the release-binary scan (`manager-ci.yml:358-359`, FR-096); core matches the API
  host by labels and takes the endpoint host as a parameter (FR-011). Each rule with a plant.
- **FR-095** `tests/required-tests.json` and `manager-ci-shape.test.js` `EXPECTED_REQUIRED` gain every Rust and vitest
  name of SC-001 to SC-012, in disjoint lanes as Phase 10 FR-108.
- **FR-096** The `roster absent` step (`manager-ci.yml:344-366`) keeps scanning the release binary, which now links the
  updater, for plugin names and `api.github.com`; a hit is a finding to surface, never a scan exemption.
- **FR-097** The requirement is already met (`.gitattributes:29`) (tasks TA-2): the root `.gitattributes` holds
  `tools/feed-contract/keys/** -text` beside the existing `tools/feed-contract/fixtures/** -text`
  (`.gitattributes:26`), with its comment at `:27-28` (added by Phase 10's close), and `git check-attr -a
  tools/feed-contract/keys/krate-signing.pub` prints `text: unset`; this phase adds no line for it (the one
  `.gitattributes` line this phase adds is the refusal harness's rsign fixture,
  `apps/krate-manager/tests/fixtures/updater-refusals/fixtures/** -text`). The original reason, at `f6022bd5`: this
  checkout runs `core.autocrlf=true` and `git check-attr -a tools/feed-contract/keys/krate-signing.pub` reported no
  attribute then, so a fresh Windows checkout would write CRLF bytes, and FR-003 (c)'s base64 comparison and
  `verify.rs:15-16`'s `include_str!` would depend on the checkout (the file is LF in the index and the working tree
  today: `git ls-files --eol` → `i/lf w/lf`). `.gitattributes` is outside FR-092's fence.

---

## Success Criteria

Rust SCs run locally on Windows with `cargo test --workspace --locked` from `apps/krate-manager/` (default threads;
every binary `0 failed`); vitest with `npm test`; Node with `node --test "tools/feed-contract/tests/*.test.js"` from the
repo root (parallel files); the harness with `cargo run --manifest-path tests/fixtures/updater-refusals/Cargo.toml
--locked`. macOS and Linux rows are **PENDING-CI** on `manager-ci.yml` `check (macos-latest)` / `check (ubuntu-24.04)`
(closed by that leg's `required tests ran` notice) or on the named `release-manager.yml` job, which only the user
dispatches.

- **SC-001 (URL policy).** `self_update::tests::url_policy` (core), over `check_update_url` and
  `check_update_package_url` with `announced = "0.2.0"`: `https://krateaudio.com/manager/latest.json` accepted by
  `check_update_url` under `HttpsOnly`; the package-URL rows (plan SI-27, SI-40):
  `https://github.com/rolandzwaga/krate-audio/releases/download/manager/v0.2.0/x` (`MANAGER_REPO`, tag `v<announced>`)
  → `Ok` under `HttpsOnly`, and `http://127.0.0.1:9/rolandzwaga/krate-audio/releases/download/manager/v0.2.0/x` → `Ok`
  under `LoopbackOnly`; `Url { Host }` for `https://example.org/pkg`, `https://github.com/rolandzwaga/krate-audio/archive/x`,
  a query or a fragment, a foreign `owner/repo` (`https://github.com/attacker/x/releases/download/manager/v0.2.0/a`, and
  its loopback twin under `LoopbackOnly`; `o/r` itself is now `Host`), the owner differing only in case, a tag that
  differs from the announced version (`…/manager/v0.1.9/x`, `…/manager/v9.9.9/x`), and `announced = "0.2.0-rc.1"`
  against `…/v0.2.0/x`; `MANAGER_REPO` itself matches `feed-publish.js:66`'s `REPO_RE`. Refused `E_SELF_UPDATE_URL`:
  `http://…`, `https://api.github.com/repos/o/r/releases/assets/1`, `https://API.GITHUB.COM/x`,
  `https://github.com/o/r/releases/latest/download/x`, a URL with userinfo; under `LoopbackOnly` `http://127.0.0.1:9/x`
  accepted and `http://example.com/x` refused. Each refusal row also asserts its `UrlRefusal` reason: under
  `HttpsOnly` the two `api.github.com` rows are `ApiHost`, the `/releases/latest/` row is `LatestPath`, `http://…` is
  `Policy`; under `LoopbackOnly`, `https://api.github.com/x` is `ApiHost` (not `Policy`, so FR-011's order is pinned)
  and `http://127.0.0.1:9/o/r/releases/latest/download/x` is `LatestPath`. `self_update::tests::redirect_hops`
  (FR-011a, `HttpsOnly`): a package hop `github.com` → `release-assets.githubusercontent.com` over https accepted;
  https → `http` hop refused `Policy`; a hop to `api.github.com` refused `ApiHost`; a hop to a foreign host refused
  `Host`; an endpoint hop away from the endpoint host refused `Host`; `redirect_decision` (plan SI-27): the sixth hop
  (`previous` of length 6) → `Policy` whatever `to` is, length 5 → the hop rule; both `hop_kind` outcomes
  (`hop_kind(endpoint, endpoint)` → `Endpoint`, `hop_kind(other, endpoint)` → `Package`). Threshold: every row, code
  and reason.
- **SC-002 (proxy choice).** `self_update::tests::proxy_choice_matches_core`: for a static proxy, a bypassed host, a
  direct config and a PAC fixture answer, `proxy_choice` equals the route the core's own `http::get` takes for the
  same URL, observed by which fixture server (the proxy or the target) receives the request (plan SI-16); a fifth row,
  a static proxy the core refuses (scheme `ftp`) → `E_SELF_UPDATE_NETWORK`, zero requests on the target and the proxy
  (plan SI-26). Threshold: 5 / 5.
- **SC-003 (error table and view).** `self_update::tests::codes_and_sentences` (every code has its sentence and starts
  with `E_SELF_UPDATE_`); `self_update::tests::view_from_update` (fields, `os`, notes through `notes::blocks`, no HTML
  tag in any block, and the action rows `{"kind":"install_and_restart"}` / `{"kind":"open_release_page","url":"…"}`
  under `available.action`); `self_update::tests::packaging_rule` (plan SI-31: `SystemPackage` →
  `E_SELF_UPDATE_UNSUPPORTED` with its sentence, `SelfContained` → `Ok`; `update_action` rows with `announced =
  "0.2.0"`: `SelfContained` with the per-tag URL → `InstallAndRestart`, with a foreign-repo or wrong-tag URL →
  `Err(Url { Host })`; `SystemPackage` under `HttpsOnly` with
  `https://github.com/rolandzwaga/krate-audio/releases/download/manager/v0.2.0/x` → `OpenReleasePage { url:
  "https://github.com/rolandzwaga/krate-audio/releases/tag/manager/v0.2.0" }`, that `url` passing
  `status::release_page_url` unchanged, and the same page under `LoopbackOnly` from the loopback per-tag URL;
  `SystemPackage` with `https://github.com/attacker/x/releases/download/manager/v9.9.9/a` and `announced = "9.9.9"`
  (no page is ever built from a foreign repo), with the project's URL for `v0.1.9` and `announced = "0.2.0"` (no
  downgrade by URL), with `https://example.org/pkg`, with `https://github.com/rolandzwaga/krate-audio/archive/x` and
  with `http://127.0.0.1:9/x` → `Err(Url { Host })`, plan SI-40); harness `map-errors` and `deb-install` (FR-080).
  vitest `renders the manager update line`, `confirms before updating`, `manager update confirmation names
  the admin prompt on macOS only` (with `os` = macos the sentence is present; with windows and with linux it is
  absent), `shows a failed update by code`,
  `shows no banner when the check fails`, `shows downloading then installing progress` (plan SI-22),
  `offers the new .deb instead of an update on a system-package install` and
  `a failed .deb release-page open shows its code` (plan SI-31), and the existing order tests updated with
  `manager_update_check` after the first `plugin_status` and after each `check_now`.
- **SC-004 (one endpoint, one key).** The `config_guard` integration tests `updater_config_pinned` and
  `planted_updater_config_fails_the_guard` (FR-003 (a)-(f), planted mutations each reported; bare names, as
  `crates/core/tests/*.rs` print them and `required-tests.json` lists them, plan SI-12);
  `self_update::tests::endpoint_matches_feed_tool`: the endpoint in `tauri.conf.json` equals the
  `MANAGER_LATEST_URL` literal read from `tools/feed-publish.js`; the `pubkey` base64-decodes to exactly
  `RELEASE_KEY_TEXT`; `krate-signing.pub` contains no `\r` (FR-003 (c), FR-097), with a planted CRLF copy reported.
- **SC-005 (busy rule).** `self_update::tests::busy_rule`: held elevation guard, stale hold, a `BUSY` entry, a running
  download → busy; none → not busy; with no `AppContext` → not busy (FR-026).
- **SC-006 (lock released before relaunch).** `context::tests::release_for_restart_frees_the_lock`: after
  `release_for_restart`, `StateLock::acquire` on the same temp roots succeeds in the same process; before it, it fails
  `Locked`.
- **SC-007 (refusal harness, Windows local, or WSL under D9's fallback with the Windows row a recorded gap quoting
  `evidence/d9-windows.txt`; PENDING-CI on the other legs; plan SI-33: ruling P-5 of `spec.md:1138-1145`).** The
  harness prints all 27 FR-082 line names `ok` (the 26 of FR-080, including `busy-after-download`, `stall`,
  `socks-proxy`, `rsign-format` and `production-inputs`, plus `confinement`) and `harness: 27 ok, 0 not ok`
  (plan SI-23, SI-30, SI-34, SI-35), exit 0, with the panicking installer
  never reached and the scenarios run concurrently (FR-080; the run's wall clock is recorded). Confinement rows, each
  measured: `--plant committed-key` exits 1 printing `E_HARNESS_KEY` before any scenario; `--plant bind-any` exits 1
  printing `E_HARNESS_BIND`; `ok confinement` (every created and every recorded written path under the temp dir,
  FR-081 (e), (e′)); the FR-094 harness-source rule green with its plant. Locally on Windows (the load measurement of
  D9, recorded in `specs/krate-manager-phase11/evidence/d9-windows.txt`; tasks TA-22): if the first run shows the bin
  fails to load on Windows with the manifest embedded, the build goes on, the harness runs on macOS and Linux only,
  locally in WSL and on `check (macos-latest)` / `check (ubuntu-24.04)`, it is still built and linted on Windows, and
  SC-007's Windows row is a recorded gap quoting the load error, never a pass; `manager-ci.yml` step `updater
  refusals` on `check (windows-latest)`, `check (macos-latest)`, `check (ubuntu-24.04)` (macOS and Linux only under
  the fallback).
- **SC-008 (bundle.js release mode).** `bundle.test.js`: no-argument argv unchanged; `--release` adds the release
  config after the helper config (Windows, macOS) or alone (Linux); `--target universal-apple-darwin` yields the three
  sidecar names; unknown argument, `--target` on Linux and `--bundles` on macOS exit 2; D6's rename table for each OS
  (no `.deb.sig` row, plan SI-21); `--features updater-e2e` is appended to the `tauri build` argv and any other
  `--features` value exits 2 (plan SI-17).
- **SC-009 (latest.json).** `gen-manager-latest writes per-tag latest.json` (node): from fixture assets signed by
  `tools/minisign.js` with a throwaway key and a Tauri-shaped trusted comment, the output has exactly D6's seven keys
  (plan SI-21),
  per-tag URLs, inline signatures equal to the `.sig` texts, and is byte-identical on a second run. FR-044: from a fixture CHANGELOG with three dated sections, the output `notes` equals
  exactly the newest version's section text and contains neither older heading nor older body line.
  `gen-manager-latest refuses what it cannot verify`: a one-byte tamper, another key, `version:` ≠ `<v>`, no `version:`
  field, a missing asset, an extra asset → each its named code, no output file.
- **SC-010 (feed check).** `collect copies only a valid manager latest.json` (Phase 3 test, extended): an
  `api.github.com` URL, a foreign host, a URL under another tag → `degraded`, nothing copied; a per-tag URL → `copied`.
- **SC-011 (preflight and body).** `manager release preflight refuses by name` (node, fixture app dirs): version drift
  in `tauri.conf.json` (`E_PREFLIGHT_VERSION`), no CHANGELOG entry, an `Unreleased` entry on a real run
  (`E_PREFLIGHT_CHANGELOG_UNDATED`; accepted with a warning on a dry run), `--repo attacker/x` against the fixture's
  `MANAGER_REPO` line and a fixture `self_update.rs` without the line or with two (`E_PREFLIGHT_REPO`, both kinds of
  run, checked before the tag; the real file's line, read through the same parser, equals `rolandzwaga/krate-audio`;
  plan SI-40), an existing tag and an existing release
  (`E_PREFLIGHT_TAG_EXISTS`, each asserted with `--dry-run false` **and** with `--dry-run true`), a 1000-entry release
  list (`E_PREFLIGHT_TAG_LIST`, both kinds of run), a `plugins.updater` drift, a fixture key passed as `--pubkey` (in
  place of `krate-signing.pub`, tasks TA-12) on a real run: each its code, exit 1; the valid fixture exits 0 with `--dry-run true` and with `--dry-run false`; its notes file holds only the newest version's section (FR-044) when the CHANGELOG has older dated sections. `manager release body is exact`: the body equals a committed expected text, including the one-liner.
- **SC-012 (guards).** `config_guard` and `injection` tests for FR-094 and FR-003 (e), each plant reported;
  `release-manager.yml keeps the Phase 11 shape` and `manager-ci.yml keeps the Phase 4 shape` (node) green with every
  planted mutation reported; `cargo clippy --all-targets -- -D warnings`, `cargo fmt --check`, `tsc --noEmit`, the
  linter, all green with zero warnings on Windows.
- **SC-013 (PENDING-CI, `release-manager.yml` job `build (macos-latest)`).** `lipo -archs` on `Contents/MacOS/krate-manager`
  and on `Contents/MacOS/krate-manager-helper` lists `x86_64 arm64`; `LSMinimumSystemVersion` 15.0; no
  `LSFileQuarantineEnabled`; `codesign --verify --deep --strict` exit 0 and `Signature=adhoc`; every `.sig` verifies
  against the leg's own throwaway key with the right `version:` (FR-053; plan SI-20); SC-022's row for this leg.
- **SC-014 (PENDING-CI, `release-manager.yml` jobs `dry-publish` and, after the user's real dispatch, `publish`).**
  `latest.json` carries exactly the seven keys `darwin-aarch64`, `darwin-x86_64`, `darwin-universal`,
  `windows-x86_64`, `windows-x86_64-nsis`, `linux-x86_64`, `linux-x86_64-appimage` (plan SI-21), each a per-tag
  `https://github.com/…/releases/download/manager/v<v>/…` URL; none contains `/releases/latest/` or
  `api.github.com`; `checkLatestJson` accepts it. (Seven keys, three distinct URLs: the two Windows keys, the three
  darwin keys and the two AppImage keys each share one.)
- **SC-015 (PENDING-CI, `release-manager.yml` job `updater-e2e`, named per leg).** Every leg's `rsign signature
  served` notice: the "new" asset's `.sig` was made by the leg's copy of `sign updater artifacts` (byte-equal to
  `sign-dry`'s, its static half held by the shape test, SC-012), so every `launched-<new>` below is the updater's own
  verifier accepting an rsign-made `.sig` (plan SI-34). `updater-e2e (windows-latest)`:
  installed ProductVersion goes old → new, `launched-<new>` appears within 300 s, one setup download. `updater-e2e
  (macos-latest)`: version old → new at the same path, `codesign --verify --deep --strict` exit 0, no
  `com.apple.quarantine` anywhere in the bundle, `launched-<new>` appears; case `pkg-installed` (FR-072): the
  authorization prompt is observed pending within 60 s, and after it is cancelled `error-E_SELF_UPDATE_INSTALL`
  appears within 30 s, no `launched-<new>`, the bundle's SHA-256 manifest is unchanged, `CFBundleShortVersionString`
  is still old, `codesign --verify --deep --strict` exit 0, and old still launches. `updater-e2e (ubuntu-24.04)`:
  AppImage case (with `libfuse2t64`, without `APPIMAGE_EXTRACT_AND_RUN`) old → new and `launched-<new>`; `.deb` case
  (plan SI-21): `<marker>/error-E_SELF_UPDATE_UNSUPPORTED` within 300 s, no `launched-<new>`, `dpkg-query` still
  old, zero package GETs in the server log.
- **SC-016 (PENDING-CI, `release-manager.yml` job `first-install`, named per leg).** `first-install (macos-latest)`:
  `installer` exit 0 from a quarantined copy; no `com.apple.quarantine` on the installed `.app`; the whole-tree `find`
  of FR-076 prints nothing (every file and directory of the bundle root-owned, none group- or other-writable) and
  `/Applications` is root-owned and not other-writable; the receipt exists; the app runs after 12 s.
  `first-install (ubuntu-24.04)`: `.deb` installs and runs, the AppImage runs, under `xvfb-run`, with `libfuse2t64` and
  without `APPIMAGE_EXTRACT_AND_RUN`. `first-install (ubuntu-22.04-container)`: `apt-get install -y ./…deb` exit 0 in
  `ubuntu:22.04` and the binary runs under `xvfb-run` after 12 s (FR-064). No `first-install` verdict reads the live
  fetch's result (FR-077), and each leg's FR-078 hosts step ran before its launch and resolved `krateaudio.com` to
  `127.0.0.1` (SC-026).
  `first-install (windows-latest)`: `/S` exit 0, the exe at `$LOCALAPPDATA\Krate Manager\krate-manager.exe`
  (plan SI-14), not under Program Files, running after 12 s.
- **SC-017 (PENDING-CI, `manager-ci.yml` `check` on every leg).** With `plugins.updater` in the base config, the
  existing unsigned `tauri build` step stays green with no key set (D3), `build warnings` reads 0, and the new steps
  `clippy updater-e2e` (plan SI-32), `updater-e2e absent` and `updater refusals` pass (`updater refusals` on Windows
  only on `D9 WINDOWS LOADS`, D9).
- **SC-018 (PENDING-CI, `release-manager.yml` job `build (linux)`).** The container is `ubuntu:22.04`; the binary's
  highest `GLIBC_` version is ≤ the container's glibc and is annotated; both an AppImage (with a valid `.sig` for the
  leg's throwaway key) and a `.deb` (no `.sig`, plan SI-21) exist; SC-022's row for this leg.
- **SC-019 (PENDING-CI, `release-manager.yml` dispatched with `dry_run: true`).** From a branch whose `version.json`
  disagrees with `tauri.conf.json`, from `main` with an existing `manager/v<v>` tag (simulated by a test tag the user
  creates and deletes, or by the tag of a real release), and with no CHANGELOG entry: each fails in `preflight` with its
  named code (`E_PREFLIGHT_VERSION`, `E_PREFLIGHT_TAG_EXISTS`, `E_PREFLIGHT_CHANGELOG`), before any `build` job starts
  (FR-051 refuses an existing tag on dry runs). A clean `dry_run` from `main` is green end to end, uploads
  `manager-release-dry-run`, and its `feed` call runs `feed.yml` with `dry_run: true`. It creates no release, no tag
  and no Pages commit, measured by commands whose output is pasted into the compliance row: before dispatch `git
  ls-remote origin refs/heads/gh-pages` records the `gh-pages` head SHA; after the run `gh release view manager/v<v>`
  exits non-zero, `git ls-remote --tags origin manager/v<v>` prints nothing, and `git ls-remote origin
  refs/heads/gh-pages` prints the recorded SHA (`feed.yml:119-125` deploys to `gh-pages`). The secrets never reached a
  dry run: every `.sig` in `manager-release-dry-run` verifies against `sign-dry`'s throwaway public key and does not
  verify against `krate-signing.pub` (`E_MINISIG_KEY_ID`) (plan SI-20).
- **SC-020 (PENDING-CI, after the user's first real dispatch; the exit criterion of Phase 12, not of this phase).**
  The `manager/v<v>` release is published, not marked latest, with D6's assets; `feed.yml` deploys
  `manager/latest.json` and the `manager` block; a downloaded asset's SHA-256 equals the built one.
- **SC-021 (PENDING-CI, `release-manager.yml` job `package-macos`).** `pkgutil --payload-files` of the pkg lists only
  paths under `./Applications/Krate Manager.app`; `release-facts.js`'s `machoFacts` over the expanded payload reports
  {`x86_64`, `arm64`} for `Contents/MacOS/krate-manager` and for `Contents/MacOS/krate-manager-helper`; the component
  plist read back from the expanded package (`pkgutil --expand`, its `PackageInfo`) shows the app not relocatable:
  `xmllint --xpath 'count(//relocate/*)' PackageInfo` prints `0` (no `relocate` element, or an empty one; a
  `<relocate>` with any child is red), and the expanded `PackageInfo` of the first dry run is recorded as
  `specs/krate-manager-phase11/evidence/packageinfo-dry-run.xml`, the fixture that names the element (plan SI-37).
  Threshold: all three (FR-061, FR-062).
- **SC-022 (PENDING-CI, `release-manager.yml` jobs `build (windows-latest)`, `build (macos-latest)`, `build (linux)`;
  closable on a dry run except the last clause, which needs a real run).** Per leg: `updater-e2e absent` (FR-073) and `ci-temp-roots absent`
  (FR-056) pass: no `updater-e2e` in `cargo tree -p krate-manager -e features --locked`, no `ci-temp-roots` in `cargo
  tree -p krate-manager-helper -e features --locked`, zero occurrences of `krate-e2e-marker-v1` in the release
  `krate-manager` binary and of `--ci-roots` in the staged helper sidecar (on macOS also in the universal bundle's
  `Contents/MacOS/` executables; on Linux `src-tauri/binaries/` holds no `krate-manager-helper-*`, plan SI-10);
  `KRATE_BUNDLE_EXTRA_CONFIG` empty on every leg (plan SI-20). The positive control is `updater-e2e (…)` step `marker
  present` (plan SI-25). The FR-091 shape test (SC-012) holds the static
  half locally (roadmap lines 800-801).
- **SC-023 (mutual exclusion; local, core + vitest + harness).** `self_update::tests::in_flight_flag` (FR-017: set by the
  install guard, cleared on error and on FR-019's busy refusal, false at startup); `install::tests::refuse_while_self_updating`
  (FR-018: with the flag set, each of `download_plugin`, `install_plugin`, `remove_plugin` and the elevated batch start returns
  `E_INSTALL_SELF_UPDATING` with its sentence, performs zero network, filesystem and elevation calls and leaves no `BUSY` entry;
  with the flag clear each proceeds); harness scenarios `busy-after-download` (FR-019) and `stall`, each asserting from
  the watcher thread that the flag `install` sets is the one `action_gate` reads for the same `state_dir` (plan SI-23); vitest `disables plugin actions while the
  manager updates itself` and `re-enables plugin actions after a failed manager update` (FR-025). Threshold: every row.
- **SC-024 (timeouts and stall; local, core + harness).** `self_update::tests::timeouts` pins
  `UpdateTimeouts::production()` and the three constants in core (check 30 s, download `Update.timeout` 3600 s, stall
  bound 30 s); "the production builder passes them and sets no retry" is held by the harness `good` assertion
  (`Update.timeout == Some(DOWNLOAD_TIMEOUT)` after `prepare_download`) and three `injection.rs` rules on
  `src-tauri/src/self_update.rs` (`build_updater` holds `.timeout(inputs.timeouts.check)` and
  `.read_timeout(inputs.timeouts.stall)`; `prepare_download` holds `update.timeout = Some(inputs.timeouts.download)`;
  no `retry` token; a plant each) (plan SI-24); harness scenario `stall` (FR-027) reads `ok` with exactly one package
  request recorded; the `UpdateInputs::production` body pin of FR-094, its body exactly `Self { http, policy:
  UrlPolicy::HttpsOnly, timeouts: UpdateTimeouts::production(), packaging: packaging() }`, with
  `update_inputs_only_from_production` / `planted_update_inputs_fail_the_pin` (`commands.rs` builds inputs only
  through it, `LoopbackOnly` only in `mod e2e`) and the harness line `production-inputs` (plan SI-35). Threshold:
  every row.
- **SC-025 (check without a context; local, core + vitest).** `self_update::tests::no_context_defaults` (FR-026: with no
  `AppContext` the config is `HttpConfig::production()` with Phase 5's proxy resolution, the busy rule is "not busy",
  `release_for_restart` is a no-op): it tests `self_update_http(None, &fallback) == fallback`, that `fallback` built as
  `lib.rs:64-67` builds it, plus the `lib.rs` pin of PD-9 in `injection.rs` (plan SI-24); vitest `checks for a manager update after plugin_status is rejected`, `checks for a manager
  update after plugin_status succeeds` and `does not check for a manager update when the context is locked`. Threshold: every row.
- **SC-026 (PENDING-CI, `release-manager.yml` job `first-install`, every leg).** The FR-078 hosts step ran before the launch
  step, `krateaudio.com` resolved to `127.0.0.1` (the step's own assertion is green), and the shape test (SC-012) holds the
  static half: the step present on each `first-install` leg before the launch step and absent from every other job.

---

## Safety

- **Injected or confined.** The endpoint is one compiled constant (D2, D5), pinned by a guard; every test path uses a
  loopback fixture server, a throwaway key made in a temp dir and a mock app (D9) or a feature build that release
  builds are checked not to contain (FR-073, FR-056). The core's roots, feed URL and launcher stay injected as in Phases
  4-10; this phase adds no root.
- **Verified before anything runs.** The updater verifies the package's minisign signature against the compiled
  `pubkey` and requires the signed `version:` to equal the announced version, in `download` and again in `install`
  (External facts); a downgrade or same version is never offered (`allowDowngrades` false, no custom comparator). The
  manager adds the URL policy (FR-011), applied to every redirect hop too (FR-011a; harness scenarios
  `redirect-to-api-host`, `redirect-to-http`, SC-001's hop rows), and keeps the webview out of every choice (FR-020,
  FR-021). The package URL can only be this project's release of the announced version (FR-011; plan SI-40). In CI
  every release `.sig` is verified against `krate-signing.pub` by the `sign` job and again before `latest.json` is
  written (FR-042, FR-053); the feed refuses a non-per-tag `latest.json` (FR-043).
- **Keys.** The real private key exists only as two environment secrets of the main-only `signing` environment, read
  only by the `sign` job's `signing key` step of a real run, which builds nothing (FR-055; plan SI-20); every build
  leg, `sign-dry` and the e2e gate make a throwaway key in `$RUNNER_TEMP`; the harness refuses the committed public key
  as its own (FR-081). No key is generated into the repo.
- **What the self-update may do.** Exactly what `tauri-plugin-updater` 2.14.1 does on install (External facts): run
  the per-user NSIS setup (no UAC), swap the macOS bundle (an admin prompt only on `PermissionDenied`), replace the
  AppImage. It never installs a `.deb` (`E_SELF_UPDATE_UNSUPPORTED`; plan SI-21). It never runs while a plugin install, removal, download or elevated
  batch is active (FR-015, FR-019), and no plugin action starts while it runs (FR-017, FR-018). It writes no quarantine (probes line 12; asserted by SC-015).
- **What tests never touch.** The dev box's installed manager (if any), `%LOCALAPPDATA%`, `/Applications`, the real
  state dir, the dev box's hosts file (only a hosted `first-install` runner's, FR-078), the signing secrets, `github.com` and `api.github.com`; no NSIS setup, `installer`, `pkgbuild`, `dpkg`,
  `sudo` locally (FR-090). The install-and-launch jobs run only on ephemeral hosted runners.
- **Unsigned reality.** No certificate anywhere; macOS stays ad-hoc (`signingIdentity "-"`, asserted on the universal
  bundle, FR-054); the pkg is unsigned; `--no-sign` is never used (it would drop the ad-hoc signature).

---

## Edge cases

| Case | Behaviour | Where |
|---|---|---|
| Interrupted download | `download` holds the package in memory and has no resume (External facts); the failure is `E_SELF_UPDATE_NETWORK`, nothing is written, the kept update is cleared, the next click starts again; a stalled transfer fails after 30 s without data, no retry | FR-013, FR-023, FR-027, SC-024 |
| Disk full during install | macOS: the extraction fails before the swap (Tauri removes its temp dir) or `PreviousAppNotRestored` keeps the old app beside the target; Windows: the setup cannot be written to temp and nothing launches; Linux AppImage: the write fails and the backup is renamed back. Each `E_SELF_UPDATE_INSTALL { detail }` | FR-013 |
| A plugin install, download or elevated batch running | `E_SELF_UPDATE_BUSY`, nothing downloaded, the kept update kept; retry when it finishes (plan SI-36) | FR-015, FR-023, SC-005 |
| A plugin action started while the self-update downloads | Refused `E_INSTALL_SELF_UPDATING`; the UI has the actions disabled | FR-017, FR-018, FR-025, SC-023 |
| An elevated batch begins between the first busy check and the install | The busy rule re-run after `download` gives `E_SELF_UPDATE_BUSY`, the kept update is cleared | FR-019, SC-023 |
| Startup context failed (`Roots`, `LockIo`) | The check and install still work (production config, "not busy", no lock to release); a `Locked` context neither checks nor shows a banner | FR-026, SC-025 |
| Partial install | macOS swap is one `renamex_np(RENAME_SWAP)`, else two renames with restore; Linux move-aside-then-write with restore; Windows is the NSIS setup's own; the next launch is the old or the new version, never neither (e2e proves the success path on every leg, and the refused-prompt path of a pkg-installed macOS copy, case `pkg-installed`) | External facts; SC-015 |
| Relaunch races the state lock | The lock is released before `restart` spawns (FR-016); the e2e marker is written only after the lock is acquired, so a lost race reads red | FR-016, SC-006, SC-015 |
| Downgrade attempt (older signed release announced, or announced as newer) | Older or equal: `check` returns `None`. Announced newer with an older signed package: `SignedVersionMismatch` → `E_SELF_UPDATE_SIGNATURE` | SC-007 |
| Signature without `version:` (made before Tauri CLI 2.12.0) | `MissingSignedVersion` → refused; every manager release is built with CLI 2.12.4 | SC-007 |
| Corrupt or incomplete `latest.json` on Pages | `E_SELF_UPDATE_MANIFEST`, muted line, no banner; the plugin list is unaffected. CI never deploys one that fails `checkLatestJson` (degrade keeps the previous file) | FR-043, SC-007 |
| Offline / proxy refusing | `E_SELF_UPDATE_NETWORK`, muted line; the plugin list follows Phase 5's cached-feed rule | FR-013 |
| `versions.json` schema 1, or v2 without a `manager` block | Irrelevant to self-update: the endpoint is compiled in (D5) | D5 |
| Bundles that still say `0.8.0` | Irrelevant to self-update (plugin detection, Phase 6) | — |
| File locked by a running DAW (Windows) | Not applicable to the manager's own files; a running elevated helper is excluded by FR-015 | FR-015 |
| Intel Mac | `darwin-x86_64` resolves to the universal archive; plugins without an Intel build show "not available for this Mac" (Phase 5) while the manager still updates | D6, roadmap 841-843 |
| pkg-installed macOS manager (root-owned) | The first self-update raises Tauri's admin prompt; afterwards the bundle is user-owned; a refused prompt is `E_SELF_UPDATE_INSTALL` and the app stays as it was | D11, FR-072 `pkg-installed`, SC-015, FR-093 (c) |
| User moved the `.app` out of `/Applications` | The updater replaces it where it is (its `extract_path` is the running bundle); a later pkg install still targets `/Applications` (`BundleIsRelocatable` false) | D11, FR-061 |
| macOS app run from a read-only or translocated location | `TempDirNotOnSameMountPoint` or an I/O error → `E_SELF_UPDATE_INSTALL`; not expected for the pkg or e2e locations | External facts |
| Manager installed from the `.deb` (plan SI-21) | Banner shown with the new version, its notes and a "Download the new .deb" button that opens the release page (no "Update and restart"); a direct `manager_update_install` is refused `E_SELF_UPDATE_UNSUPPORTED` before any download; the user installs the new `.deb` | FR-013, FR-014, FR-024, FR-072, Departures 5 |
| macOS manager started through a symlinked path (plan SI-39) | `E_SELF_UPDATE_UNSUPPORTED` before any download: the app could not relaunch itself after the swap | FR-013 |
| Unsupported OS or arch | `E_SELF_UPDATE_UNSUPPORTED`, no banner | FR-013 |
| A second manager release dispatched while one runs | `concurrency` queues it; its preflight then refuses the existing tag | FR-050, FR-051 |
| A real run fails after `gh release create` | The release stays a draft; the user deletes it (and any tag) before re-dispatching, as the keys README §3 says for plugins | FR-057 |
| `release-manager.yml` dispatched from a branch | `preflight` runs (no ref check) and reports its own refusals by code (`E_PREFLIGHT_VERSION` for a drifted `version.json`); a branch run that passes preflight is stopped at `main-only`, its first `environment: signing` job, by the environment's main-only rule (plan SI-20) | D14, FR-051, SC-019 |
| `latest.json` or the package URL redirects to `api.github.com`, plain `http` or a foreign host | The hop is refused before it is fetched: `E_SELF_UPDATE_URL { reason }` | FR-011a, SC-001, SC-007 |
| `krateaudio.com` during a `first-install` run | Pointed at loopback by the hosts step, so the fetch fails deterministically; the app shows its muted check line and keeps running; the job stays green (process presence only) | FR-077, FR-078, SC-016, SC-026 |

---

## New components

| Module / crate / file | Purpose |
|---|---|
| `apps/krate-manager/crates/core/src/self_update.rs`, `self_update_tests.rs` | URL policy, proxy choice, busy rule, codes and sentences, `ManagerUpdateView` (FR-010 to FR-015) |
| `crates/core/src/context.rs` (🔶) | `release_for_restart` (FR-016) |
| `apps/krate-manager/src-tauri/src/self_update.rs` | Updater builder, kept-update slot, error mapping, `UpdateInstaller` / `NativeUpdateInstaller`, the `updater-e2e` module (FR-022, FR-070) |
| `src-tauri/src/lib.rs`, `commands.rs`, `build.rs`, `Cargo.toml`, `capabilities/main.json`, `tauri.conf.json` (🔶) | Plugin registration, two commands, feature `updater-e2e`, D2's config |
| `src-tauri/tauri.release.conf.json` | `createUpdaterArtifacts: true`, release builds only (D3) |
| `apps/krate-manager/Cargo.toml`, `Cargo.lock` (🔶) | `tauri-plugin-updater` 2.14.1; `exclude` gains the harness |
| `src/view.ts`, `src/main.ts`, tests (🔶) | The update line, confirmation, progress, failures (FR-024) |
| `apps/krate-manager/scripts/bundle.js`, `bundle.test.js` (🔶) | Release mode, universal helper sidecars, rename to D6's names |
| `apps/krate-manager/scripts/e2e-update-server.js` | Loopback static server for `updater-e2e` |
| `apps/krate-manager/tests/fixtures/updater-refusals/` | The refusal harness (D9, FR-080 to FR-084) |
| `tools/gen-manager-latest.js` | `latest.json` with per-tag URLs, every signature verified (FR-040 to FR-042) |
| `tools/manager-release-preflight.js` | Version, CHANGELOG, tag, updater-config and key checks; the release body (FR-051, FR-063) |
| `tools/feed-publish.js` (🔶) | Per-tag URL rule in `checkLatestJson` (FR-043) |
| `tools/feed-contract/tests/gen-manager-latest.test.js`, `manager-release-preflight.test.js`, `release-manager-shape.test.js` (new); `feed-publish-collect.test.js`, `manager-ci-shape.test.js` (🔶) | SC-009 to SC-012 |
| `.github/workflows/release-manager.yml` | `preflight`, `main-only`, `build`, `sign` / `sign-dry`, `package-macos`, `updater-e2e`, `first-install`, `publish` / `dry-publish`, `feed` / `feed-dry` (plan SI-20) |
| `.github/workflows/manager-ci.yml`, `feed-tools.yml` (🔶) | `clippy updater-e2e` (plan SI-32), `updater-e2e absent`, `updater refusals`; the node test plan |
| `crates/core/tests/config_guard.rs`, `injection.rs`, `tests/required-tests.json` (🔶) | FR-003, FR-094, FR-095 |
| `crates/core/src/install*.rs`, `download.rs` (🔶) | The action gate `E_INSTALL_SELF_UPDATING` (FR-018). The stalling route of FR-027 already exists (`fixture_server.rs:39-44`); `fixture_server.rs` is unchanged (plan SI-5) |
| `.gitattributes` (🔶) | `tools/feed-contract/keys/** -text` (FR-097) is already at `.gitattributes:29` (tasks TA-2); the one line this phase adds is `apps/krate-manager/tests/fixtures/updater-refusals/fixtures/** -text` |

---

## Traceability

| Roadmap statement (line) | Requirements | Success criteria |
|---|---|---|
| Updater ≥ 2.14.1, `requireSignedVersion`, `allowDowngrades` false, `createUpdaterArtifacts`, Windows `passive`, relaunch on macOS / Linux (823-824, 150-156) | D1 to D3, D7, FR-001 to FR-004, FR-016, FR-022 | SC-004, SC-006, SC-007, SC-017 |
| `release-manager.yml` manual dispatch; version lint, CHANGELOG and tag refusals (825-828) | D15, FR-050, FR-051 | SC-011, SC-019 |
| `create-release` (tag `manager/v<ver>`), matrix, per-tag `latest.json` (828-830) | D4 (departure), D6, FR-052, FR-057, FR-040 to FR-042 | SC-009, SC-014 |
| Signing secrets of the `signing` environment; updater `pubkey` from `krate-signing.pub` (830-832) | D2, D14, FR-003, FR-052, FR-055 | SC-004, SC-013 |
| Then `feed.yml` publishes `manager/latest.json` and the `manager` block (832) | D16, FR-043, FR-058, FR-059 | SC-010, SC-019, SC-020 |
| One universal macOS build, `--target universal-apple-darwin`, three darwin keys (833-844) | D6, D13, FR-031, FR-033, FR-054 | SC-013, SC-014 |
| Universal helper sidecar, `tauri.helper.conf.json`, `lipo -archs` on the helper (845-848) | D13, FR-031, FR-033, FR-054 | SC-013 |
| First-install packaging: NSIS `currentUser`; unsigned pkg to `/Applications` + `curl` one-liner; AppImage + `.deb` in `ubuntu:22.04`; glibc floor (849-855) | D11, D12, FR-060 to FR-065 | SC-016, SC-018, SC-021 |
| Phase 10: `--features ci-temp-roots` is "a feature that `release-manager.yml` builds never enable" (800-801) | FR-056, FR-091 | SC-012, SC-013, SC-018, SC-022 |
| One endpoint unless a stable second host (856-858) | D5, OQ-1 (decided), FR-002, FR-003 | SC-004 |
| (Phase 11 decisions, 2026-11-01) Self-update exclusion, timeouts, no-context check | FR-015, FR-017 to FR-019, FR-025 to FR-027, FR-044 | SC-023 to SC-025, SC-009 |
| End-to-end self-replace gate with a throwaway key and a local server; macOS codesign and no quarantine; version change on Windows and Linux (859-862, 873) | D10, FR-070 to FR-074 | SC-015, SC-022 |
| Tampered, other-key and downgrade refused locally through `check` / `download` / `verify` only; `install` only through `UpdateInstaller`, whose test impl panics (864-872) | D9, FR-011a, FR-022, FR-023, FR-080 to FR-084 | SC-001, SC-007 |
| `lipo -archs` and three darwin entries with per-tag URLs (874-877) | FR-054, FR-041 | SC-013, SC-014 |
| First-install artifacts on fresh hosted runners (878-883) | FR-075 to FR-077 | SC-016 |
| Dry-run refusals (version, existing tag, CHANGELOG; refused on dry runs too) and the glibc check (884-886) | D14, FR-051, FR-065 | SC-011, SC-018, SC-019 |
| Does NOT publish a public release (888) | FR-057 (user dispatch only) | SC-020 (Phase 12) |
| Never touch the real system; hosted-runner exceptions; live-network tests never in a gate (1001-1008) | FR-077, FR-078, FR-081, FR-090; Departures 8 (exception not widened) | SC-007, SC-012, SC-016, SC-026 |
| Security: verify before execute; narrowest capability; no key in the repo (1009-1014) | FR-003 (c), FR-011, FR-011a, FR-021, FR-042, FR-052, FR-055, FR-081, FR-097 | SC-001, SC-004, SC-007, SC-009, SC-019 |
| Unsigned reality (1020-1022) | FR-004, FR-054, FR-076 | SC-013, SC-016 |
| Zero warnings; parallel tests (1027-1031) | FR-053, FR-080 (concurrent scenarios), FR-083, FR-095 | SC-007, SC-012 |
| Phase 10 D19's open assertion (installed helper ownership) | D11, FR-076 | SC-016 |

---

## Review notes

Revision 2 resolves the challenge review (22 issues; none rejected). Where an issue offered two resolutions, the one
taken and why:

- **Existing tag on a dry run (blocker, and the duplicate testability major).** Taken: FR-051 refuses on dry and real
  runs, as roadmap lines 826-828 and 884-886 require; SC-011 asserts both. Not taken: a roadmap amendment to tolerate
  the tag on dry runs, because it would weaken a stated outcome to match the plugin pipeline's `release.yml:578-579`.
- **Branch dispatch (D14).** Taken: no ref check in preflight, so SC-019's branch case reports `E_PREFLIGHT_VERSION`;
  the dangling "SC-024" reference is gone.
- **Live network in `first-install` / `updater-e2e`.** Both halves taken: the `updater-e2e` builds also compile a
  loopback feed URL (D10, no live host), and the `first-install` verdicts are independent of the live fetch, with the
  extension of the hosted-runner exception listed as Departures 8; decided 2026-11-01 (option B, FR-078): the hosts file blocks the live host and no exception is made.
- **Harness parallelism.** Taken: concurrent scenarios (FR-080), with a defined child-process fallback; no serial
  exception is requested.
- **D9's "no webview" premise.** Withdrawn: wry is linked through feature unification. The Windows manifest
  `build.rs` is now required, and the fallback for a remaining load failure (SC-007's Windows row a recorded gap) is
  defined in D9.
- **`api-host` / `latest-path` vacuity.** FR-011's own rules now run before the policy check and carry a reason, the
  scenarios assert the reason and a trap proxy, and SC-001 pins the order under both policies.
- **macOS `pkg-installed` e2e case.** Added. The observable "prompt pending" process is not assumed: the spec names
  `osascript` only as one candidate, because the updater depends on `osakit` 0.3 (External facts), which may run the
  AppleScript in-process. The plan measures which process to observe, with a stop-and-surface rule if a hosted
  runner cannot produce or cancel the prompt.
- **FR-064.** Kept, made verifiable by the `first-install (ubuntu-22.04-container)` leg rather than removed.
- **FR-077's "no step calls github.com".** Kept as a shape-test assertion over `run` steps (`actions/checkout` and
  `actions/download-artifact` reach GitHub by design and are not `run` steps).

Revision 4 (2026-11-01, tasks T002) writes the plan's spec issues (`plan.md` §0.2) into the rows they name, worded as
the plan words them and tagged `(plan SI-n)`, and the spec-marked task-list amendments, tagged `(tasks TA-n)`. Where a
later SI supersedes an earlier one's value, only the later value is written (SI-35's 27 harness lines over SI-23's and
SI-30's 25; SI-40's `MANAGER_REPO` over SI-27's `o/r`). Citations of the plan-precondition rulings as
`spec.md:1138-1145` (SI-30 to SI-33) refer to revision 3's line numbers of "Session 2026-11-01 (plan preconditions)";
this revision moved that section down. Where each landed:

- SI-1 FR-003 (f), FR-013; SI-2 FR-094; SI-3 D1, FR-001; SI-4 FR-011, FR-094; SI-5 FR-027, New components; SI-6
  FR-014, FR-024; SI-7 FR-081 (a); SI-8 FR-081 (b); SI-9 FR-080; SI-10 FR-056, SC-022; SI-11 FR-094; SI-12 SC-004;
  SI-13 FR-072; SI-14 FR-076, SC-016; SI-15 FR-018, FR-094; SI-16 SC-002; SI-17 FR-030, SC-008; SI-18 FR-040,
  FR-071, FR-091; SI-19 withdrawn by the plan (superseded by SI-20), not encoded; its remaining `--platform-subset`
  flag is in FR-040 and FR-053; SI-20 D14, FR-052, FR-053, FR-055, FR-056, FR-057, FR-059, FR-090, FR-091, SC-013,
  SC-019, SC-022, Safety, Edge cases (and the keys README §2 sentence, written with TA-17's wording); SI-21 Overview,
  D6, D7, Departures 5, FR-013, FR-041, FR-042, FR-072, SC-008, SC-009, SC-014, SC-015, SC-018, Safety, Edge cases;
  SI-22 D7, FR-020, FR-024, SC-003; SI-23 FR-080, FR-081 (d), SC-007, SC-023; SI-24 SC-024, SC-025; SI-25 FR-070,
  FR-073, FR-094, SC-022; SI-26 D17, FR-012, SC-002; SI-27 FR-011, FR-011a, SC-001; SI-28 FR-001, FR-022, FR-094;
  SI-29 FR-081 (b), (e′); SI-30 D17, FR-001, FR-022, FR-080, FR-081 (c), SC-007; SI-31 FR-013, FR-014, FR-024,
  FR-080, SC-003, Edge cases; SI-32 FR-073, FR-083, FR-091, SC-017; SI-33 D9, FR-084, SC-007, the D9 clarification
  (annotated, the recorded answer kept); SI-34 FR-071, FR-072, FR-080, FR-091, SC-007, SC-015; SI-35 FR-080,
  FR-082, FR-084, FR-094, SC-007, SC-024; SI-36 D7, FR-023, FR-080 `busy`, Edge cases; SI-37 SC-021; SI-38 FR-090;
  SI-39 D7, FR-013, Edge cases; SI-40 FR-011, FR-011a, FR-014, FR-051, FR-071, FR-080, SC-001, SC-003, SC-011.
- TA-2 FR-097 ("already met"), Existing repo pieces, New components; TA-4 FR-094 (the `tar` rule); TA-11 FR-042;
  TA-12 FR-051, SC-011; TA-13 FR-094 (the install / restart token scan); TA-14 FR-073, FR-083 (with SI-32); TA-22
  D9, FR-084, SC-007 (`evidence/d9-windows.txt`).

---

## Clarifications

### Session 2026-11-01

- Q: While a self-update sits between its busy check and `Update::install`, what happens if the user starts a plugin download, install or remove, or an elevated batch begins? → A: Mutual exclusion (B). A self-update in flight sets a core flag; `download_plugin` / `install_plugin` / `remove_plugin` and an elevated batch refuse with the new code `E_INSTALL_SELF_UPDATING`; the UI disables plugin actions meanwhile; FR-015's busy rule is also re-run after `download`, just before the bytes go to `UpdateInstaller` (busy → `E_SELF_UPDATE_BUSY`, kept update cleared); FR-092's fence widens into the Phase 9/10 action gate for this. [FR-015, FR-017, FR-018, FR-019, FR-023, FR-025, FR-092, SC-023]
- Q: What timeout, stall and retry policy does the self-update package download use? → A: (A) Check 30 s total; download `Update.timeout` = 3600 s plus a 30 s per-read stall bound via `configure_client` → reqwest `ClientBuilder::read_timeout`; no automatic retry; failure `E_SELF_UPDATE_NETWORK`; a stalling fixture route tests it. [FR-013, FR-022, FR-027, SC-024, SC-007]
- Q: Can a manager whose startup context failed still check for and install its own update? → A: (B) Yes. The check runs once `plugin_status` settles (success or rejection) unless the context is `Locked`; with no context it uses `HttpConfig::production()` plus Phase 5's proxy resolution, the busy rule is "not busy", and `release_for_restart` is a no-op. [FR-015, FR-016, FR-024, FR-026, SC-005, SC-025]
- Q: How does a pkg-installed, root-owned `/Applications/Krate Manager.app` update itself? → A: (A) As D11. The first self-update raises Tauri's admin prompt, after which the app and helper are user-owned (Phase 10 D19's accepted state); Phase 12 records it; the `pkg-installed` updater-e2e case gates the refused-prompt half. No pkg `postinstall` script. [FR-061, FR-072, FR-093 (c), SC-015, SC-016]
- Q: How do the `first-install` launches avoid the live `krateaudio.com` fetch? → A: (B) Before launch each leg points `krateaudio.com` at loopback in the runner's hosts file (sudo/admin), pinned by the shape test; the shipped artifact reaches nothing live; no never-touch exception (FR-093 (f)'s widening is dropped). [FR-077, FR-078, FR-090, FR-091, FR-093, SC-016, SC-026]
- Q: Does `release-manager.yml` use `tauri-action`? → A: (A) No. `bundle.js --release` builds, `gh release upload` uploads, `tools/gen-manager-latest.js` writes `latest.json` with per-tag github.com URLs; FR-093 (a) amends the roadmap. [FR-040, FR-041, FR-057, FR-091, FR-093 (a), SC-009, SC-012]
- Q: Which release notes does the update line show to a user who skipped versions? → A: (A) Only the newest version's CHANGELOG section, as `latest.json` `notes`. [FR-044, FR-024, SC-009, SC-011]
- Q: Is the manager pkg built with `BundleIsRelocatable=false`? → A: (A) Yes, through `pkgbuild --analyze` and a component plist. [FR-061, FR-062, SC-021]
- OQ-1: One updater endpoint, `https://krateaudio.com/manager/latest.json` (D5), accepted. [FR-002, FR-003, FR-093 (b), SC-004]
- D15: A real release requires a dated CHANGELOG heading; `Unreleased` passes only a dry run. Accepted. [FR-051, SC-011]
- D9: The local updater refusal tests are a workspace-excluded fixture bin crate with tauri's mock runtime (Phase 4 D12a); a Windows load failure stops the build and is surfaced to the user. Accepted. [FR-080, FR-084, SC-007] — Its Windows-load-failure half is superseded by ruling P-5 (the D9 bullet of "Session 2026-11-01 (plan preconditions)" below, revision 3's `spec.md:1144`; plan SI-33): if the first local run shows the bin fails to load on Windows with the manifest embedded, the build goes on: the measurement (exit code, loader message) is recorded in `evidence/d9-windows.txt`; the harness then runs on macOS and Linux only, locally in WSL and on `check (macos-latest)` / `check (ubuntu-24.04)`; it is still built and linted on Windows; SC-007's Windows row is a recorded gap quoting the load error, never a pass.
- D4: No `tauri-action` (see Q6). Accepted. [FR-091, FR-093 (a), SC-009]
- D11: As Q4. Accepted. [FR-072, FR-093 (c), SC-015]

### Session 2026-11-01 (plan preconditions)

- Q: SOCKS proxies for self-update (plan §10 Q1, R-6)? → A: Enable reqwest's `socks` feature in `src-tauri` (a direct `reqwest = "=0.13.5"` dependency with `socks`, unified into the updater's client), so self-update uses every proxy type plugin downloads use; the refusal harness gains a SOCKS route case. Overturns the plan's "built without it". [FR-022, D17]
- Q: The release key's home (PD-23, SI-20)? → A: Accepted as planned: every `build` leg bundles with a throwaway key; only a `sign` job (`environment: signing`) re-signs the updater artifacts with the real key, `sign-dry` uses a throwaway key, and a `main-only` gate job keeps dry runs to `main`. [D14, FR-052, FR-053, FR-055, FR-057]
- Q: What does a `.deb`-installed manager do when an update exists (PD-24, SI-21)? → A: Never self-update a system-package install (as PD-24), but the banner offers no install button: it shows the new version and notes with a "Download the new .deb" action that opens the release page through the existing `open_release_page` command (the view model carries the packaging so the UI picks the action). `manager_update_install` still refuses `E_SELF_UPDATE_UNSUPPORTED` as the backstop. Overturns the plan's button-that-refuses UI. [FR-013, FR-014, FR-024, FR-041, FR-072]
- Q: Lint the updater-e2e feature build in Manager CI (TA-14)? → A: Yes: `manager-ci.yml` `check` gains a `clippy updater-e2e` step on all three legs (dummy `KRATE_E2E_*` values, never run), pinned by the shape test, so a break shows on push rather than in the release workflow. Overturns plan §4.11's "no other step changes". [FR-030, SC-026]
- Q: D9: if the refusal harness cannot load on Windows (0xc0000139 with the manifest embedded)? → A: Pre-ruled fallback, no stop: the harness then runs on macOS and Linux only (locally in WSL, and on those CI legs); compliance records the Windows row as a gap with the load error quoted. Supersedes D9's "stops the build". [FR-080, FR-081, SC-007]
- Q: R-17: WSL has cargo but no WebKitGTK packages, which the P-5 fallback needs? → A: (a) The user installs the CI Linux leg's package line in WSL (`manager-ci.yml:53-54`) before the build stage; T014's WSL harness run may assume them. [FR-080, SC-007]
- Q: D-20: rsign's default signature comment trips `no_secret_key_in_app_tree`? → A: As TA-25: the committed rsign fixture is signed with its own comment (`rsign -c`); the leak scan is not loosened; rsign's default comment is checked only on the CI e2e legs. [FR-052, SC-019]
- Q: R-15 follow-up (outside this phase): `release.yml`'s signing job restores `rsign2` from a cache `feed-tools.yml` also writes? → A: Compile it fresh: drop the cache restore from `release.yml`'s signing job, as its own commit after Phase 11 closes (not a Phase 11 task). [plan §9 R-15]
- Q: Departure 9: the manager's own update package gets minisign only, no size / SHA-256 (Cross-Cutting "size, SHA-256, minisign")? → A: Accepted as a ruled exception (2026-11-02): full-byte minisign after download and again in `install`, plus the `requireSignedVersion` check; `latest.json` keeps Tauri's exact schema. A hash read from the same unsigned `latest.json` adds no integrity. [CC-verify]
- Q: Drop the updater's `zip` feature (SI-3, PD-2); T002 writes SI-1 to SI-29 in G2; TA-11 (`.deb` / `.pkg` optional release-only assets, any `.deb.sig` extra) and TA-12 (`--pubkey` on `manager-release-preflight.js`)? → A: All accepted as planned. [D1, FR-042, FR-051, tasks T002]
